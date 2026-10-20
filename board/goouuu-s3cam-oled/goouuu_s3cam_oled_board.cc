#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "led/single_led.h"
#include "esp32_camera.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_http_server.h>
#include <img_converters.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "GoouuuS3CamOledBoard"

/*
 * 摄像头网页服务（与小智共存于同一固件，端口 8080）：
 *   GET /        预览页
 *   GET /stream  MJPEG 实时视频流
 *   GET /capture 单张 JPEG 照片
 * ctrl_port 用 32769，避免与小智配网模式(80端口)的默认控制端口 32768 冲突。
 */
static esp_err_t cam_index_handler(httpd_req_t* req) {
    static const char* page =
        "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32-S3-CAM</title></head>"
        "<body style='text-align:center;background:#111;color:#eee;font-family:sans-serif'>"
        "<h3>Goouuu ESP32-S3-CAM</h3>"
        "<img src='/stream' style='max-width:100%' alt='loading...'>"
        "<p><a href='/capture' target='_blank' style='color:#8cf'>/capture 单张照片</a></p>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t cam_stream_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char part[80];
    while (true) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {  // 小智 AI 正在占用帧缓冲，稍后重试
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        uint8_t* jpg = nullptr;
        size_t jpg_len = 0;
        if (!frame2jpg(fb, 12, &jpg, &jpg_len)) {
            esp_camera_fb_return(fb);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        size_t n = snprintf(part, sizeof(part),
                            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                            (unsigned)jpg_len);
        if (httpd_resp_send_chunk(req, part, n) != ESP_OK ||
            httpd_resp_send_chunk(req, (const char*)jpg, jpg_len) != ESP_OK ||
            httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) {
            free(jpg);
            esp_camera_fb_return(fb);
            return ESP_FAIL;  // 客户端断开连接
        }
        free(jpg);
        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(100));  // 约 5~10 fps
    }
}

static esp_err_t cam_capture_handler(httpd_req_t* req) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "capture failed");
        return ESP_FAIL;
    }
    uint8_t* jpg = nullptr;
    size_t jpg_len = 0;
    bool ok = frame2jpg(fb, 12, &jpg, &jpg_len);
    esp_camera_fb_return(fb);
    if (!ok || !jpg) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "jpeg encode failed");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    esp_err_t ret = httpd_resp_send(req, (const char*)jpg, jpg_len);
    free(jpg);
    return ret;
}

// 等设备拿到 IP 后，把预览地址显示到 OLED 上
static void cam_ip_notify_task(void* arg) {
    auto* board = static_cast<GoouuuS3CamOledBoard*>(arg);
    for (int i = 0; i < 200; i++) {
        esp_netif_ip_info_t info = {};
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0) {
            char url[64];
            snprintf(url, sizeof(url), "Cam: http://" IPSTR ":8080", IP2STR(&info.ip));
            board->GetDisplay()->ShowNotification(url, 15000);
            ESP_LOGI(TAG, "%s", url);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    vTaskDelete(nullptr);
}

/*
 * GOOUUU ESP32-S3-CAM V1.5 定制板型：
 * - 0.91寸 SSD1306 128x32 I2C OLED（I2C0：SDA=IO21 / SCL=IO47）
 * - INMP441 + MAX98357A（I2S 引脚避开摄像头）
 * - OV3660 摄像头（DVP + SCCB 独占 I2C1）
 */
class GoouuuS3CamOledBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    Esp32Camera* camera_;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .scl_speed_hz = 400 * 1000,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeCamera() {
        camera_config_t config = {};
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = CAMERA_PIN_SIOD;
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.sccb_i2c_port = CAMERA_SCCB_I2C_PORT;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        camera_ = new Esp32Camera(config);
    }

    void InitializeCameraWebServer() {
        httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
        cfg.server_port = 8080;
        cfg.ctrl_port = 32769;  // 避开小智配网模式(80端口)默认 ctrl 端口 32768
        cfg.stack_size = 8192;
        httpd_handle_t server = nullptr;
        if (httpd_start(&server, &cfg) == ESP_OK) {
            const httpd_uri_t uris[] = {
                { .uri = "/",          .method = HTTP_GET, .handler = cam_index_handler,   .user_ctx = nullptr },
                { .uri = "/stream",    .method = HTTP_GET, .handler = cam_stream_handler,  .user_ctx = nullptr },
                { .uri = "/capture",   .method = HTTP_GET, .handler = cam_capture_handler, .user_ctx = nullptr },
            };
            for (auto& u : uris) {
                httpd_register_uri_handler(server, &u);
            }
            ESP_LOGI(TAG, "Camera web server started on port 8080");
            xTaskCreate(cam_ip_notify_task, "camsrv_ip", 4096, this, 3, nullptr);
        } else {
            ESP_LOGE(TAG, "Failed to start camera web server");
        }
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

public:
    GoouuuS3CamOledBoard() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeCamera();
        InitializeCameraWebServer();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }
};

DECLARE_BOARD(GoouuuS3CamOledBoard);

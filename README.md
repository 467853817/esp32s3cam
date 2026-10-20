# ESP32-S3CAM 小智AI 语音机器人

基于 GOOUUU ESP32-S3-CAM 自制的 [小智AI（xiaozhi-esp32）](https://github.com/78/xiaozhi-esp32) 语音对话机器人。

**定制固件板型：`goouuu-s3cam-oled`** —— 0.91" I2C OLED 可用 + OV3660 摄像头保留，由本仓库 GitHub Actions 自动编译。

## 一、材料清单

| 材料 | 型号/规格 | 作用 |
|---|---|---|
| 主控板 | GOOUUU ESP32-S3-CAM V1.5（ESP32-S3-WROOM-1 **N16R8**，16MB Flash + 8MB PSRAM） | 主控，带 BOOT/RST 键、OTG/TTL 双 Type-C 口 |
| 麦克风 | INMP441（I2S 数字麦） | 语音输入 |
| 功放 | MAX98357A（I2S 功放）+ 4Ω/8Ω 2~3W 喇叭 | 语音输出 |
| 屏幕 | 0.91 寸 OLED，**I2C 接口，SSD1306 驱动，128×32**，地址 0x3C | 显示状态/对话内容 |
| 摄像头 | 板载 **OV3660**（DVP） | 拍照/视觉（固件已保留） |

## 二、接线表（定制固件 goouuu-s3cam-oled）

> **丝印说明**：这块 GOOUUU ESP32-S3-CAM V1.5 的排针丝印以 **G** 开头（G1、G2、G42…），**Gx 就是 GPIOx**，与代码里的 IO 编号一一对应。下表两列都给，按「板载丝印」插线即可。
> 电源丝印：`3V3` = 3.3V，`5V0` = 5V，`GND` = 地。

### 引脚速查（丝印 → GPIO）
| 板载丝印 | GPIO | 用途（本固件） |
|---|---|---|
| **G1** | IO1 | INMP441 WS |
| **G2** | IO2 | INMP441 SCK |
| **G42** | IO42 | INMP441 SD |
| **G39** | IO39 | MAX98357A DIN |
| **G40** | IO40 | MAX98357A BCLK |
| **G41** | IO41 | MAX98357A LRC |
| **G21** | IO21 | OLED SDA |
| **G47** | IO47 | OLED SCL |
| **G0** | IO0 | BOOT 键（唤醒/打断） |
| **G48** | IO48 | 板载 WS2812 彩灯（无需接线） |

### INMP441 麦克风
| INMP441 | 板载丝印 | GPIO |
|---|---|---|
| VDD | 3V3 | 3.3V |
| GND | GND | GND |
| L/R | GND | GND（左声道） |
| SCK (BCLK) | **G2** | IO2 |
| WS (LRCK) | **G1** | IO1 |
| SD | **G42** | IO42 |

### MAX98357A 功放
| MAX98357A | 板载丝印 | GPIO |
|---|---|---|
| VIN | 5V0 | 5V |
| GND | GND | GND |
| DIN | **G39** | IO39 |
| BCLK | **G40** | IO40 |
| LRC | **G41** | IO41 |
| GAIN | 悬空 | — |
| 喇叭 +/− | 4Ω 或 8Ω 2~3W 喇叭 | — |

> G38~G41 兼作板载 TF 卡槽引脚（G38=CMD、G39=CLK、G40=DAT0），**不插 TF 卡**即可放心用作功放。

### 0.91" OLED（SSD1306 128×32）
| OLED | 板载丝印 | GPIO |
|---|---|---|
| VCC | 3V3 | 3.3V |
| GND | GND | GND |
| SCL | **G47** | IO47 |
| SDA | **G21** | IO21 |

> OLED 走 I2C0；摄像头 SCCB 独占 I2C1（G4/G5），互不冲突。
> 所有音频/OLED 引脚均避开摄像头 DVP 引脚，OV3660 随时可用。
> 唤醒/打断键不用接：板载 **BOOT 键（G0）** 即唤醒键。板载 WS2812 彩灯 = G48。

## 三、固件编译（GitHub Actions 云端自动）

1. 推送到 main 分支自动触发，或手动 `Actions → Build XiaoZhi Firmware → Run workflow`
2. 编译流程：检出 xiaozhi-esp32 **v2.5.2** → 应用 `goouuu-s3cam-oled.patch` + `board/` 板型文件 → `espressif/idf:v6.1` 容器内 `scripts/build.py` 编译
3. 编译完成后在运行页 **Artifacts** 下载 `goouuu-s3cam-oled-firmware`
4. 解压得到 `merged-binary.bin`，整包烧录，**地址 0x0**

## 四、烧录步骤

1. Type-C 数据线连 **TTL 口**（CH343 串口）到电脑
2. 按住 **BOOT** → 短按 **RST** → 松开 BOOT，进入下载模式
3. 烧录工具（二选一）：
   - 网页版 [ESP Launchpad](https://espressif.github.io/esp-launchpad/)：选 bin、地址填 0、先 Erase Flash 再 Program
   - [Flash Download Tool](https://www.espressif.com.cn/zh-hans/support/download/other-tools)：选 ESP32-S3、地址 0x0、40MHz、DoNotChgBin
4. 烧完按 RST 重启

## 五、配网与激活

1. 连接热点 `Xiaozhi-XXXX` → 浏览器访问 `192.168.4.1` → 填家中 Wi-Fi（仅 2.4GHz）
2. 设备播报 6 位验证码 → [xiaozhi.me](https://xiaozhi.me) 控制台添加设备绑定
3. 喊 **"你好小智"** 开始对话；BOOT 键 = 唤醒/打断键

## 六、摄像头网页预览（固件内置）

固件已内置 HTTP 摄像头服务（与小智共存，端口 **8080**），设备联网后：

- `http://<设备IP>:8080/` —— 浏览器实时预览（MJPEG，约 5~10fps）
- `http://<设备IP>:8080/stream` —— MJPEG 视频流
- `http://<设备IP>:8080/capture` —— 单张 JPEG 照片

设备 IP 可在路由器后台查看；联网成功后 **OLED 会显示一次预览地址**（约 15 秒）。注意与小智的 AI 拍照共用帧缓冲，边看流边让 AI"看"偶尔会有一方丢一帧，属正常现象。

## 七、TODO

- [x] 定制板型编译通过（OLED + 摄像头共存）
- [ ] 接线并烧录，点亮 0.91" OLED
- [ ] 配网绑定 xiaozhi.me，跑通语音对话
- [ ] 启用 OV3660 摄像头（"看看这个"视觉对话）
- [ ] 如摄像头画面方向不对，调整 config.json 中 camera_hmirror / camera_vflip

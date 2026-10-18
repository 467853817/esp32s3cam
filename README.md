# ESP32-S3CAM 小智AI 语音机器人

基于 GOOUUU ESP32-S3-CAM 自制的 [小智AI（xiaozhi-esp32）](https://github.com/78/xiaozhi-esp32) 语音对话机器人。

**当前路线：优先让屏幕亮起来** —— 使用 `bread-compact-wifi` 固件（支持 0.91寸 I2C OLED），摄像头功能暂不启用。

## 一、材料清单

| 材料 | 型号/规格 | 作用 |
|---|---|---|
| 主控板 | GOOUUU ESP32-S3-CAM V1.5（ESP32-S3-WROOM-1 **N16R8**，16MB Flash + 8MB PSRAM） | 主控，带 BOOT/RST 键、OTG/TTL 双 Type-C 口 |
| 麦克风 | INMP441（I2S 数字麦） | 语音输入 |
| 功放 | MAX98357A（I2S 功放）+ 4Ω/8Ω 2~3W 喇叭 | 语音输出 |
| 屏幕 | 0.91 寸 OLED，**I2C 接口，SSD1306 驱动，128×32**，地址 0x3C | 显示状态/对话内容 |
| 其他 | 杜邦线、面包板、Type-C 数据线 | 连接与烧录 |

## 二、接线表（面包板 WiFi 版引脚，屏幕优先）

### INMP441 麦克风
| INMP441 | ESP32-S3 |
|---|---|
| VDD | 3V3 |
| GND | GND |
| L/R | GND（左声道） |
| SCK (BCLK) | IO5 |
| WS (LRCK) | IO4 |
| SD | IO6 |

### MAX98357A 功放
| MAX98357A | ESP32-S3 |
|---|---|
| VIN | 5V |
| GND | GND |
| DIN | IO7 |
| BCLK | IO15 |
| LRC | IO16 |
| GAIN | 悬空 |
| 喇叭 +/− | 4Ω 或 8Ω 2~3W 喇叭 |

### 0.91" OLED（SSD1306 128×32）
| OLED | ESP32-S3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SCL | IO42 |
| SDA | IO41 |

> 唤醒/打断键不用接：板载 **BOOT 键（IO0）** 即唤醒键。
> 注意：IO4/5/6/7/15/16 与板载摄像头 DVP 引脚共用，本固件不初始化摄像头，实测音频正常；摄像头功能留待后续。

## 三、固件

- 固件：[xiaozhi-esp32 Releases](https://github.com/78/xiaozhi-esp32/releases) → `vx.x.x_bread-compact-wifi.zip`
  - 0.91寸（SSD1306 128×32）选 `bread-compact-wifi`；0.96寸（128×64）才选 `bread-compact-wifi-128x64`
- 解压得到 `merged-binary.bin`，整包烧录，**地址 0x0**

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

## 六、TODO

- [ ] 按接线表完成 INMP441 / MAX98357A / OLED 接线
- [ ] 烧录 bread-compact-wifi 固件，点亮 0.91" OLED
- [ ] 配网并绑定 xiaozhi.me，跑通语音对话
- [ ] （后续）自编译固件：在 s3cam 板型基础上加 I2C OLED 支持，同时启用摄像头

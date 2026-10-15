#ifndef ADBLOCK_CONFIG_H
#define ADBLOCK_CONFIG_H

#include <Arduino.h>

// ── 板型配置 ─────────────────────────────────────────────────
// ESP32-S2 mini 连接 7.5" JDY79668 面板的引脚定义，
// 沿自 InkSight 的 S2 板型配置。
#if defined(BOARD_PROFILE_ESP32_S2_MINI)
  #define PIN_EPD_MOSI   11
  #define PIN_EPD_SCK    7
  #define PIN_EPD_CS     12
  #define PIN_EPD_DC     17
  #define PIN_EPD_RST    16
  #define PIN_EPD_BUSY   4
  #define PIN_BAT_ADC    6
  #define PIN_CFG_BTN    0
  #define PIN_LED        15
#elif defined(BOARD_PROFILE_ESP32_C3)
  #define PIN_EPD_MOSI   6
  #define PIN_EPD_SCK    4
  #define PIN_EPD_CS     7
  #define PIN_EPD_DC     1
  #define PIN_EPD_RST    2
  #define PIN_EPD_BUSY   10
  #define PIN_BAT_ADC    0
  #define PIN_CFG_BTN    9
  #define PIN_LED        3
#else
  #error "Unsupported board profile"
#endif

// ── 面板几何参数 ─────────────────────────────────────────────
// 768x552 四色，每像素 2 位。
#define EPD_WIDTH   768
#define EPD_HEIGHT  552
#define EPD_BPP     2

static const int W = EPD_WIDTH;
static const int H = EPD_HEIGHT;

// 每个 band 渲染并流式发送的行数。驱动为每一行设置各自的
// 面板窗口（厂商时序也是如此），因此这里只决定进入下一个
// band 之前重绘多少屏幕区域。
#define EPD_CHUNK_ROWS 64

// 完整的 2bpp 画布需要 (768*552)/4 = 105,984 字节，S2 在同时
// 运行 WiFi 的情况下拿不出这么多内存。图像改为按 band 逐块
// 渲染到这么大的缓冲区并直接流式发送到面板；参见
// epd_driver.cpp 中的 epdSendFrame() 与 display.cpp 中的
// canvasBindBand()。
static const int BAND_ROW_BYTES = W / 4;                        // 192
static const int BAND_BUF_LEN   = EPD_CHUNK_ROWS * (W / 4);     // 12,288

// 当前正在绘制的 band 的基地址。
extern uint8_t* colorBuf;

// ── 刷新策略 ─────────────────────────────────────────────────
// 四色全刷会让面板闪烁，并消耗数秒的 bit-bang 方式模拟 SPI 的
// 时间。仪表盘是被人偶尔查看的，不是被人一直盯着的，因此刷新
// 周期以分钟计。
static const uint32_t DASH_REFRESH_MS = 3UL * 60UL * 1000UL;

// ── 文件系统 ─────────────────────────────────────────────────
// Arduino 的 LittleFS.begin() 默认使用分区标签 "spiffs"，对其他
// 名称的分区既无法挂载也无法格式化，因此必须显式传入
// partitions.csv 中的标签。
#define LFS_PARTITION "littlefs"

// ── 网络默认值 ───────────────────────────────────────────────
#define AP_SSID_DEFAULT   "AdBlock-Setup"
static const int WIFI_TIMEOUT = 15000;      // ms
static const uint32_t LIVE_WIFI_RETRY_MS = 30000;  // 调用 WiFi.reconnect() 之前的退避等待
static const uint32_t HTTP_TIMEOUT_MS = 30000;
#define NTP_UTC_OFFSET  (8 * 3600)

// 配置按键的去抖时间，避免机械抖动被计为一次按压。
static const uint32_t SHORT_PRESS_MIN_MS = 50;

// 面板手势阈值。短按重绘仪表盘；再多按住这么久则重新打开
// 配置门户——当板子离开了它当初配置的 WiFi 网络的覆盖范围时，
// 这是回到 WiFi 配置表单的唯一途径。
static const uint32_t CFG_BTN_HOLD_MS    = 3000;
static const uint32_t BTN_POLL_MS        = 20;

// 连续重连失败多少次（每 LIVE_WIFI_RETRY_MS 一次）后，板子才
// 放弃已存凭据并自行打开配置门户。守着一个根本不该连的网络
// 反复重试，还不如直接向人要一个新密码。
static const uint8_t WIFI_RETRY_BEFORE_PORTAL = 6;

// 面板把「无 WiFi」提示多显示这么多秒，之后才自行转入配置
// 门户。无论走哪条路，设备此时都已无法充当 sinkhole，所以这个
// 提示只是给人一个去修路由器的机会；超过时限后它转而让自己
// 变得可访问。
static const uint32_t WIFI_GIVEUP_MS = 300000;

// ── DNS 引擎 ─────────────────────────────────────────────────
static const char* DNS_HOSTNAME = "adblock";

#endif // ADBLOCK_CONFIG_H

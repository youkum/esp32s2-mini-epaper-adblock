#ifndef ADBLOCK_CONFIG_H
#define ADBLOCK_CONFIG_H

#include <Arduino.h>

// ── Board profile ────────────────────────────────────────────
// Pins for the ESP32-S2 mini wired to the 7.5" JDY79668 panel,
// carried over from InkSight's S2 profile.
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

// ── Panel geometry ───────────────────────────────────────────
// 768x552 four-colour, 2 bits per pixel.
#define EPD_WIDTH   768
#define EPD_HEIGHT  552
#define EPD_BPP     2

static const int W = EPD_WIDTH;
static const int H = EPD_HEIGHT;

// Rows rendered and streamed per band. The driver gives every row its
// own panel window (the vendor sequence does the same), so this only
// sets how much of the screen is redrawn before the next band.
#define EPD_CHUNK_ROWS 64

// A full 2bpp canvas would be (768*552)/4 = 105,984 bytes, which the S2
// cannot spare alongside WiFi. The image is rendered one band at a time
// into a buffer this size and streamed straight to the panel; see
// epdSendFrame() in epd_driver.cpp and canvasBindBand() in display.cpp.
static const int BAND_ROW_BYTES = W / 4;                        // 192
static const int BAND_BUF_LEN   = EPD_CHUNK_ROWS * (W / 4);     // 12,288

// Base of the band currently being drawn into.
extern uint8_t* colorBuf;

// ── Refresh policy ───────────────────────────────────────────
// A four-colour full refresh flashes the panel and costs seconds of
// bit-banged SPI. Dashboards are polled, not watched, so the period
// is measured in minutes.
static const uint32_t DASH_REFRESH_MS = 3UL * 60UL * 1000UL;

// ── Filesystem ───────────────────────────────────────────────
// Arduino's LittleFS.begin() defaults to the partition label "spiffs" and
// fails to both mount and format any partition named otherwise, so the
// label from partitions.csv has to be passed explicitly.
#define LFS_PARTITION "littlefs"

// ── Network defaults ─────────────────────────────────────────
#define AP_SSID_DEFAULT   "AdBlock-Setup"
static const int WIFI_TIMEOUT = 15000;      // ms
static const uint32_t LIVE_WIFI_RETRY_MS = 30000;  // back-off before WiFi.reconnect()
static const uint32_t HTTP_TIMEOUT_MS = 30000;
#define NTP_UTC_OFFSET  (8 * 3600)

// Debounce for the config button, so a bounce does not count as a press.
static const uint32_t SHORT_PRESS_MIN_MS = 50;

// Panel gesture thresholds. A short press redraws the dashboard; holding
// the button for this much longer reopens the setup portal, which is the
// only way back into the WiFi form once the board is out of reach of the
// network it was configured for.
static const uint32_t CFG_BTN_HOLD_MS    = 3000;
static const uint32_t BTN_POLL_MS        = 20;

// How many consecutive failed reconnect attempts (one every
// LIVE_WIFI_RETRY_MS) before the board gives up on the stored credentials
// and opens the portal by itself. Sitting there retrying a network the
// board has no business on is worse than asking for a new password.
static const uint8_t WIFI_RETRY_BEFORE_PORTAL = 6;

// Seconds the panel keeps the "no WiFi" notice up before it takes the
// portal route on its own. The device is unusable as a sinkhole either
// way, so the notice exists only to give someone a chance to fix the
// router; after this it makes itself reachable instead.
static const uint32_t WIFI_GIVEUP_MS = 300000;

// ── DNS engine ───────────────────────────────────────────────
static const char* DNS_HOSTNAME = "adblock";

#endif // ADBLOCK_CONFIG_H

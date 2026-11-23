#include "epd_driver.h"
#include "config.h"

// 保留已实机验证的阶段日志；仅在状态切换时输出，便于核对 BUSY 与刷新耗时。
#define EPD_TRACE 1

// ── 软件 SPI（bit-bang 模拟）─────────────────────────────────
// 面板接在任意指定的 GPIO 上，而驱动必须精确复现厂商字节级的总线
// 行为，因此总线用 bit-bang 翻转 GPIO 来驱动，而不使用 SPI 外设。
//
// 曾短暂改用 SPI 外设以缩短帧时间：表面上帧快了 35 倍
// （16.8 s -> 0.47 s），但提速是假的——实测 digitalWrite（370 ns/次，
// 整帧 bit-bang 约 1.1 s）后发现，旧帧中约 15.4 s 是面板自身的
// busy 时间，而 SPI 版本的空闲等待骤降到 19 ms，说明面板已完全
// 停止刷新。因此保留 bit-bang：它在约 16.5 s 的刷新周期里只占
// 约 1.2 s，且是唯一有证据表明真正驱动了这块面板的路径。

// 以 MSB 在前的顺序逐位输出一个字节，面板在 SCK 上升沿采样。
static void spiWriteByte(uint8_t data) {
  for (int i = 0; i < 8; i++) {
    digitalWrite(PIN_EPD_MOSI, (data & 0x80) ? HIGH : LOW);
    data <<= 1;
    digitalWrite(PIN_EPD_SCK, HIGH);
    digitalWrite(PIN_EPD_SCK, LOW);
  }
}

static void epdSendCommand(uint8_t cmd) {
  digitalWrite(PIN_EPD_DC, LOW);   // DC 低电平 = 命令
  digitalWrite(PIN_EPD_CS, LOW);
  spiWriteByte(cmd);
  digitalWrite(PIN_EPD_CS, HIGH);
}

static void epdSendData(uint8_t data) {
  digitalWrite(PIN_EPD_DC, HIGH);  // DC 高电平 = 数据
  digitalWrite(PIN_EPD_CS, LOW);
  spiWriteByte(data);
  digitalWrite(PIN_EPD_CS, HIGH);
}

void gpioInit() {
  pinMode(PIN_EPD_BUSY, INPUT);
  pinMode(PIN_EPD_RST,  OUTPUT);
  pinMode(PIN_EPD_DC,   OUTPUT);
  pinMode(PIN_EPD_CS,   OUTPUT);
  pinMode(PIN_EPD_SCK,  OUTPUT);
  pinMode(PIN_EPD_MOSI, OUTPUT);
  pinMode(PIN_CFG_BTN,  INPUT_PULLUP);
  digitalWrite(PIN_EPD_RST, HIGH);
  digitalWrite(PIN_EPD_CS,  HIGH);
  digitalWrite(PIN_EPD_SCK, LOW);
}

// ── 分带传输 ─────────────────────────────────────────────────
// 厂商驱动一次写一行图像，每行都使用各自的 0x83 窗口。把它的循环
// 归纳为闭式：源行 d 在 d < H/2 时写入窗口 2d+1，在 d >= H/2 时
// 写入窗口 2(H-1-d)。也就是说，面板在窗口空间里把帧的上下两半
// 交错排布——奇数窗口承接图像上半部分，偶数窗口倒序承接下半部分。
//
// 这正是窗口必须由数据行（rBase + k）推导、而非使用递增计数器的
// 原因。若窗口序列只是沿面板 0,1,2,... 走下去，图像下半部分会被
// 反着写回，玻璃上于是出现两幅画面：上半幅正立，下半幅镜像且
// 倒置。两种写法面板都照单全收，并同样报告一次历时 15 s 的
// 「健康」刷新——只有给玻璃拍照才能区分二者。
//
// 流式发送时施加两个变换：一行的字节按从右到左的顺序发出，且字节
// 内的 4 个像素顺序反转。display.cpp 在绘制时施加配套的旋转与
// 镜像，因此调用方画出的正立内容最终显示出来也是正立的。

static uint8_t s_bandBuf[BAND_BUF_LEN];

// 选定一个单行窗口并进入其数据阶段。
static void epdRowWindow(int pRow) {
  epdSendCommand(0x83);
  epdSendData(0x00);
  epdSendData(0x00);
  epdSendData((W - 1) / 256);
  epdSendData((W - 1) % 256);
  epdSendData(pRow / 256);
  epdSendData(pRow % 256);
  epdSendData(pRow / 256);
  epdSendData(pRow % 256);
  epdSendData(0x01);
  epdSendCommand(0x10);
}

static void epdRowData(const uint8_t* row) {
  for (int i = W / 4 - 1; i >= 0; i--) {
    uint8_t src = row[i];
    uint8_t dst = ((src & 0x03) << 6) | ((src & 0x0C) << 2) |
                  ((src & 0x30) >> 2) | ((src & 0xC0) >> 6);
    epdSendData(dst);
  }
}

// 在触发刷新之前，把窗口交还给整幅帧。
static void epdRestoreFullWindow() {
  epdSendCommand(0x83);
  epdSendData(0x00);
  epdSendData(0x00);
  epdSendData((W - 1) / 256);
  epdSendData((W - 1) % 256);
  epdSendData(0x00);
  epdSendData(0x00);
  epdSendData((H - 1) / 256);
  epdSendData((H - 1) % 256);
  epdSendData(0x01);
}

// 每次只发送两条完整行；复位、上电、刷新与断电等待均用状态机服务。
// 单个字节与单行内部的 bit-bang 时序保持原样，不能拆到半个字节。
enum class Phase { Idle, ResetWait, ResetLow, ResetHigh, ResetBusy, InitDelay,
                   PowerBusy, PowerAgain, Stream, Refresh, Off, Retry, Failed };
static Phase phase = Phase::Idle;
static void (*renderer)(int, int, uint8_t*) = nullptr;
static uint32_t phaseMs = 0, frameMs = 0, refreshMs = 0;
static uint32_t faults = 0;
static uint8_t retries = 0;
static int rowDone = 0;
static bool sawBusy = false;
static const char* lastError = "无";
static void enter(Phase p) {
#if EPD_TRACE
  Serial.printf("[EPD-DIAG] 阶段 %u -> %u，历时 %lu ms，BUSY=%d，行=%d\n",
                unsigned(phase), unsigned(p), static_cast<unsigned long>(millis()-phaseMs),
                digitalRead(PIN_EPD_BUSY), rowDone);
#endif
  phase = p; phaseMs = millis();
}
static void fail(const char* reason) {
  lastError = reason; ++faults;
  digitalWrite(PIN_EPD_CS, HIGH);
  Serial.printf("[EPD] %s，重试 %u/2\n", reason, unsigned(retries));
  if (retries < 2) { ++retries; enter(Phase::Retry); }
  else { renderer = nullptr; enter(Phase::Failed); }
}
bool epdSendFrame(void (*renderBand)(int, int, uint8_t*)) {
  if (!renderBand || epdRefreshActive()) return false;
  renderer = renderBand; retries = 0; rowDone = 0; lastError = "无";
  frameMs = millis(); enter(Phase::ResetWait);
  return true;
}
bool epdRefreshActive() { return phase != Phase::Idle && phase != Phase::Failed; }
const char* epdStatus() {
  switch (phase) {
    case Phase::Idle: return "空闲";
    case Phase::Failed: return "故障，等待下次请求";
    case Phase::Retry: return "恢复重试";
    case Phase::Stream: return "分行发送";
    case Phase::Refresh: return "面板刷新";
    case Phase::Off: return "等待断电";
    default: return "初始化";
  }
}
const char* epdError() { return lastError; }
uint32_t epdFaults() { return faults; }
int epdRowsSent() { return rowDone; }
bool epdServiceRefresh() {
  if (!epdRefreshActive()) return true;
  uint32_t now = millis(), elapsed = now - phaseMs;
  if (phase != Phase::Retry && now - frameMs >= 120000) {
    fail("整帧超时"); return !epdRefreshActive();
  }
  bool busy = digitalRead(PIN_EPD_BUSY) == LOW;
  switch (phase) {
    case Phase::ResetWait:
      if (elapsed >= 20) { digitalWrite(PIN_EPD_RST, LOW); enter(Phase::ResetLow); } break;
    case Phase::ResetLow:
      if (elapsed >= 40) { digitalWrite(PIN_EPD_RST, HIGH); enter(Phase::ResetHigh); } break;
    case Phase::ResetHigh:
      if (elapsed >= 50) enter(Phase::ResetBusy);
      break;
    case Phase::ResetBusy:
      if (!busy) enter(Phase::InitDelay);
      else if (elapsed >= 45000) fail("复位 BUSY 超时");
      break;
    case Phase::InitDelay:
      if (elapsed >= 30) {
        epdSendCommand(0x00); epdSendData(0x0B);
        epdSendCommand(0x61); epdSendData(W/256); epdSendData(W%256);
        epdSendData(H/256); epdSendData(H%256);
        epdSendCommand(0x04); enter(Phase::PowerBusy);
      } break;
    case Phase::PowerBusy:
      if (elapsed < 10) break;
      if (!busy) { epdSendCommand(0x04); enter(Phase::PowerAgain); }
      else if (elapsed >= 45000) fail("上电 BUSY 超时");
      break;
    case Phase::PowerAgain:
      if (elapsed < 10) break;
      if (!busy) { rowDone = 0; enter(Phase::Stream); }
      else if (elapsed >= 45000) fail("二次上电 BUSY 超时");
      break;
    case Phase::Stream:
      for (int n=0; n<2 && rowDone<H; ++n) {
        int base = rowDone / EPD_CHUNK_ROWS * EPD_CHUNK_ROWS;
        if (rowDone == base) renderer(base, min(EPD_CHUNK_ROWS, H-base), s_bandBuf);
        int window = rowDone < H/2 ? 2*rowDone+1 : 2*(H-1-rowDone);
        epdRowWindow(window);
        epdRowData(s_bandBuf + size_t(rowDone-base)*(W/4));
        ++rowDone;
      }
      if (rowDone == H) {
        epdRestoreFullWindow(); epdSendCommand(0x12); epdSendData(0x00);
        sawBusy = false; refreshMs = millis(); enter(Phase::Refresh);
      } break;
    case Phase::Refresh:
      if (busy) sawBusy = true;
      if (!busy && sawBusy) {
        epdSendCommand(0x02); epdSendData(0x00); enter(Phase::Off);
      } else if (!sawBusy && elapsed >= 2000) fail("刷新未观察到 BUSY");
      else if (elapsed >= 45000) fail("刷新 BUSY 超时");
      break;
    case Phase::Off:
      if (elapsed < 10) break;
      if (!busy) {
        Serial.printf("[EPD] refresh=%lu total=%lu ms，帧完成\n",
                      static_cast<unsigned long>(phaseMs-refreshMs),
                      static_cast<unsigned long>(now-frameMs));
        renderer = nullptr; lastError="无"; enter(Phase::Idle);
      } else if (elapsed >= 45000) fail("断电 BUSY 超时");
      break;
    case Phase::Retry:
      if (elapsed >= 2000) { frameMs = now; rowDone=0; enter(Phase::ResetWait); } break;
    default: break;
  }
  return !epdRefreshActive();
}

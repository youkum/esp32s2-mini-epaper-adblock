#include "epd_driver.h"
#include "config.h"

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

// 这块面板的 BUSY 为低电平有效：低电平表示仍在工作。整次刷新会把它
// 拉低约 15 s，因此这段等待的时长本身也是最廉价的佐证，证明面板
// 确实在执行刷新。
static void epdWaitBusy(unsigned long maxMs) {
  unsigned long t0 = millis();
  unsigned long timeoutMs = maxMs > 0 ? maxMs : 45000;
  while (digitalRead(PIN_EPD_BUSY) == LOW) {
    delay(10);
    if (millis() - t0 > timeoutMs) {
      Serial.println("[EPD] busy TIMEOUT!");
      return;
    }
  }
}
static void epdWaitBusy() { epdWaitBusy(0); }

static void epdReset() {
  delay(20);
  digitalWrite(PIN_EPD_RST, LOW);  delay(40);
  digitalWrite(PIN_EPD_RST, HIGH); delay(50);
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

// 从面板厂商的 epd7in3g-A0.cpp Init() 移植并精简的初始化序列。
void epdInit() {
  epdReset();
  epdWaitBusy();
  delay(30);

  epdSendCommand(0x00);
  epdSendData(0x0B);

  epdSendCommand(0x61);
  epdSendData(W / 256);
  epdSendData(W % 256);
  epdSendData(H / 256);
  epdSendData(H % 256);

  epdSendCommand(0x04);
  epdWaitBusy();
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

// epdSendFrame() 与 epdServiceRefresh() 之间半成品帧的状态。
static bool     s_refreshing  = false;
static uint32_t s_frameStartMs = 0;
static uint32_t s_armedMs      = 0;
static uint32_t s_rowsDoneMs   = 0;
static uint32_t s_streamMs     = 0;

// 不套重试循环：在这块面板上，厂商驱动的循环第一次迭代即返回，
// 没有什么可重试的。
//
// 一直执行到 0x12 刷新命令（含）为止，然后立即返回。面板接下来用
// 约 15 s 刷新玻璃，我们则回到网络事务中。
uint32_t epdSendFrame(void (*renderBand)(int rBase, int rows, uint8_t* buf)) {
  // epdInit() 会复位并重新给面板上电，上一帧仍在刷新期间绝不能这么做。
  if (s_refreshing) epdWaitRefresh();

  const int bytesPerRow = W / 4;
  uint32_t t0 = millis();

  epdInit();
  epdSendCommand(0x04);
  epdWaitBusy();
  uint32_t tArmed = millis();

  for (int rBase = 0; rBase < H; rBase += EPD_CHUNK_ROWS) {
    int rows = EPD_CHUNK_ROWS;
    if (rBase + rows > H) rows = H - rBase;

    renderBand(rBase, rows, s_bandBuf);

    for (int k = 0; k < rows; k++) {
      // 缓冲区行 d = rBase + k。这是厂商的映射关系，已逐窗口对照
      // 其原始循环核实：数据行 0..H/2-1 落入奇数窗口（2d+1，递增），
      // 行 H/2..H-1 落入偶数窗口（2(H-1-d)，递减）。这两半并不是
      // 面板上连续的分区——面板把它们交错排布——因此窗口必须由
      // 数据行计算得出，而不能用递增计数器。
      int d = rBase + k;
      int p = (d < H / 2) ? (2 * d + 1) : (2 * (H - 1 - d));
      epdRowWindow(p);
      epdRowData(s_bandBuf + (size_t)k * bytesPerRow);
    }
  }

  uint32_t tRows = millis();
  epdRestoreFullWindow();

  epdSendCommand(0x12);
  epdSendData(0x00);

  s_frameStartMs = t0;
  s_armedMs      = tArmed - t0;
  s_rowsDoneMs   = tRows - t0;
  s_streamMs     = millis() - t0;
  s_refreshing   = true;
  return s_streamMs;
}

bool epdRefreshActive() { return s_refreshing; }

bool epdServiceRefresh() {
  if (!s_refreshing) return true;
  if (digitalRead(PIN_EPD_BUSY) == LOW) return false;   // 玻璃仍在刷新中

  uint32_t tWait = millis();
  epdSendCommand(0x02);
  epdSendData(0x00);
  epdWaitBusy();
  uint32_t tOff = millis();

  // 面板自身的 busy 时长才是它确实刷新了玻璃的唯一可信证明：此处应
  // 约为 15 s；若只有几十毫秒，则视为「更新命令根本未被接受」。
  Serial.printf("[EPD] arm=%lu rows=%lu stream=%lu refresh=%lu off=%lu total=%lu ms",
                s_armedMs, s_rowsDoneMs - s_armedMs, s_streamMs,
                tWait - (s_frameStartMs + s_streamMs), tOff - tWait,
                tOff - s_frameStartMs);
  Serial.println();

  s_refreshing = false;
  return true;
}

void epdWaitRefresh() {
  while (!epdServiceRefresh()) delay(10);
}

// 深度休眠：0x07 命令 + 0xA5 校验码，随后延时确保命令生效。
void epdSleep() {
  epdSendCommand(0x07);
  epdSendData(0xA5);
  delay(200);
}

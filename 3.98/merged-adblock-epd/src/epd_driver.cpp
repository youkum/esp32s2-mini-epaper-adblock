#include "epd_driver.h"
#include "config.h"

// ── Software SPI (bit-bang) ──────────────────────────────────
// The panel hangs off arbitrary GPIOs and the driver has to reproduce the
// vendor's byte-level bus behaviour exactly, so the bus is driven by
// bit-banging rather than by the SPI peripheral.
//
// This was briefly moved to the SPI peripheral to cut the frame time. It
// made a frame look 35x faster (16.8 s -> 0.47 s) but the speed-up was
// fake: measuring digitalWrite (370 ns/op, so ~1.1 s for a whole frame of
// bit-bang) showed that ~15.4 s of the old frame was the PANEL's own busy
// time, and the SPI build's idle-wait had collapsed to 19 ms - the panel
// had stopped refreshing altogether. Bit-banging is therefore kept: it
// costs ~1.2 s out of a ~16.5 s refresh and is the only path with evidence
// of actually driving this panel.

static void spiWriteByte(uint8_t data) {
  for (int i = 0; i < 8; i++) {
    digitalWrite(PIN_EPD_MOSI, (data & 0x80) ? HIGH : LOW);
    data <<= 1;
    digitalWrite(PIN_EPD_SCK, HIGH);
    digitalWrite(PIN_EPD_SCK, LOW);
  }
}

static void epdSendCommand(uint8_t cmd) {
  digitalWrite(PIN_EPD_DC, LOW);   // DC low = command
  digitalWrite(PIN_EPD_CS, LOW);
  spiWriteByte(cmd);
  digitalWrite(PIN_EPD_CS, HIGH);
}

static void epdSendData(uint8_t data) {
  digitalWrite(PIN_EPD_DC, HIGH);  // DC high = data
  digitalWrite(PIN_EPD_CS, LOW);
  spiWriteByte(data);
  digitalWrite(PIN_EPD_CS, HIGH);
}

// BUSY is active-low on this panel: low means still working. A full refresh
// holds it low for roughly 15 s, so the duration of these waits is also the
// cheapest proof that the panel is really running its update.
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

// Simplified init sequence ported from the panel vendor's
// epd7in3g-A0.cpp Init().
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

// ── Band transfer ────────────────────────────────────────────
// The vendor driver writes the image one row at a time, every row with its
// own 0x83 window. Reducing its loop to a closed form: source row d goes to
// window 2d+1 for d < H/2, and to window 2(H-1-d) for d >= H/2. The panel
// therefore interleaves the two halves of the frame in the window space -
// the odd windows take the first half of the image, the even windows take
// the second half in reverse.
//
// That is why the window is derived from the DATA row (rBase + k) rather
// than from a running counter. A window sequence that merely walks
// 0,1,2,... down the panel writes the image back-to-front over its second
// half as well, and the glass then shows the picture twice: right way up
// on top, mirrored-and-inverted underneath. Every byte is still accepted,
// so the panel reports a perfectly healthy 15 s refresh either way - only
// a photo of the glass distinguishes them.
//
// Two transforms are applied while streaming: the bytes of a row go out
// right-to-left, and the four pixels inside each byte are reversed.
// display.cpp applies the matching rotation and mirror as it draws, so
// what the caller drew upright comes out upright.

static uint8_t s_bandBuf[BAND_BUF_LEN];

// Select a one-row window and start its data phase.
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

// Hand the window back to the full frame before triggering the update.
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

// State for the half-finished frame between epdSendFrame() and
// epdServiceRefresh().
static bool     s_refreshing  = false;
static uint32_t s_frameStartMs = 0;
static uint32_t s_armedMs      = 0;
static uint32_t s_rowsDoneMs   = 0;
static uint32_t s_streamMs     = 0;

// Not wrapped in a retry loop: the vendor's loop returned on its first
// iteration for this panel, so there is nothing to retry on.
//
// Everything up to and including the 0x12 update command, then out. The
// panel spends the next ~15 s on the glass and we get back to the network.
uint32_t epdSendFrame(void (*renderBand)(int rBase, int rows, uint8_t* buf)) {
  // epdInit() resets and re-powers the panel, which must not happen while
  // the previous frame is still being refreshed.
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
      // Buffer row d = rBase + k. This is the vendor mapping, verified
      // window-for-window against its own loop: data rows 0..H/2-1 land in
      // the ODD windows (2d+1, ascending) and rows H/2..H-1 in the EVEN
      // ones (2(H-1-d), descending). The two halves are not contiguous
      // bands of the panel - the panel interleaves them - so the window
      // must be computed from the data row, not from a running counter.
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
  if (digitalRead(PIN_EPD_BUSY) == LOW) return false;   // glass still moving

  uint32_t tWait = millis();
  epdSendCommand(0x02);
  epdSendData(0x00);
  epdWaitBusy();
  uint32_t tOff = millis();

  // The panel's own busy time is the only honest proof that it really
  // refreshed the glass: expect ~15 s here, and treat a few tens of ms as
  // "the update was never accepted".
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

void epdSleep() {
  epdSendCommand(0x07);
  epdSendData(0xA5);
  delay(200);
}

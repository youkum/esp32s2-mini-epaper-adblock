#ifndef ADBLOCK_EPD_DRIVER_H
#define ADBLOCK_EPD_DRIVER_H

#include <Arduino.h>

// ── JDY79668 7.5" four-colour e-paper driver ─────────────────
// Ported from InkSight's epd_driver.cpp (bit-bang SPI, panel-specific
// init sequence). Only the EPD_PANEL_75_JDY79668 path is kept.
//
// The panel is 768x552 at 2 bits per pixel, so a whole canvas would be
// 105,984 bytes. That does not fit in the DRAM of the ESP32-S2, so the
// image is rendered and streamed in bands instead.

void gpioInit();
void epdInit();
void epdSleep();

// ── Frame transfer, split so the panel can refresh in the background ──
// Setting up a frame and pushing all the pixels costs about 1.4 s; the
// panel then holds BUSY low for a further ~15 s while it moves the
// pigments. Waiting that out inside the drawing call would stop the DNS
// engine answering queries for 15 s every time the dashboard redraws.
//
// So epdSendFrame() does the blocking half - reset, power-on, the bands,
// the full-window restore and the 0x12 update command - and returns as
// soon as the glass starts refreshing. The main loop then calls
// epdServiceRefresh() until it returns true; that is what powers the
// panel back down (0x02) once BUSY has gone high. Neither call waits for
// the glass, so the network keeps running throughout the refresh.

// Renders the whole panel, one band of EPD_CHUNK_ROWS rows at a time.
// renderBand() is called once per band and must fill buf with rows
// pre-rotation rows starting at pre-rotation row rBase, in the 2bpp
// layout the drawing helpers use (4 pixels per byte, most significant
// pair leftmost, W/4 bytes per row). The buffer is owned by the driver
// and is BAND_BUF_LEN bytes long.
//
// Returns the milliseconds spent streaming, or 0 if it had to wait for a
// previous refresh first. Any frame already in flight is finished before
// the new one starts.
uint32_t epdSendFrame(void (*renderBand)(int rBase, int rows, uint8_t* buf));

// True while the panel is still refreshing a frame that was sent.
bool epdRefreshActive();

// Call from the main loop. Returns true when nothing is pending; on the
// first call after BUSY goes high it sends the power-off command and
// logs the frame timing. Returns false while the glass is still busy.
bool epdServiceRefresh();

// Blocks until any in-flight refresh has been serviced. Only needed where
// nothing else can service it.
void epdWaitRefresh();

#endif // ADBLOCK_EPD_DRIVER_H

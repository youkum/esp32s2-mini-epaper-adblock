#ifndef ADBLOCK_DISPLAY_H
#define ADBLOCK_DISPLAY_H

#include <Arduino.h>

// Colour constants for the four-colour panel. These ARE the panel's
// native 2bpp codes — packMonoPixelGroup() in epd_driver.cpp writes
// 0x00 for black and 0x01 for white, so no mapping layer is needed.
#define COLOR_BLACK   0x00
#define COLOR_WHITE   0x01
#define COLOR_RED     0x02
#define COLOR_YELLOW  0x03

// Look up glyph data for a character (5x7 pixel font).
const uint8_t* getGlyph(char c);

// Draw scaled text into the 2bpp canvas at (x, y).
void drawTextColor(const char* msg, int x, int y, int scale, uint8_t color);

// Filled rectangle in the 2bpp canvas.
void fillRectColor(int x, int y, int w, int h, uint8_t color);

// Pixel width of N characters at a given scale (ASCII only).
int textWidth(int charCount, int scale);

// Pixel width of a UTF-8 string, counting Chinese glyphs as full width.
int textWidthUtf8(const char* msg, int scale);

// Number of characters (not bytes) in a UTF-8 string.
int utf8Len(const char* msg);

// ── Banded canvas ────────────────────────────────────────────
// Point the drawing helpers at the band epdSendFrame() is about to
// stream. `buf` holds `rows` pre-rotation rows starting at
// pre-rotation row `rBase`, and is cleared to white. Draw calls that
// fall outside the band are dropped, so a caller can simply redraw the
// whole screen for every band.
void canvasBindBand(uint8_t* buf, int rBase, int rows);

#endif // ADBLOCK_DISPLAY_H

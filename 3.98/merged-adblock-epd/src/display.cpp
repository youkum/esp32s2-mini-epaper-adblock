#include "display.h"
#include "config.h"
#include "cjk_font.h"

// ── Unified 5x7 pixel font ───────────────────────────────────
// Each glyph is 5 columns x 7 rows, stored column-major.
// Bit 0 = top row, bit 6 = bottom row.

const uint8_t* getGlyph(char c) {
  static const uint8_t g_A[] = {0x7E,0x11,0x11,0x11,0x7E};
  static const uint8_t g_B[] = {0x7F,0x49,0x49,0x49,0x36};
  static const uint8_t g_C[] = {0x3E,0x41,0x41,0x41,0x22};
  static const uint8_t g_D[] = {0x7F,0x41,0x41,0x22,0x1C};
  static const uint8_t g_E[] = {0x7F,0x49,0x49,0x49,0x41};
  static const uint8_t g_F[] = {0x7F,0x09,0x09,0x09,0x01};
  static const uint8_t g_G[] = {0x3E,0x41,0x49,0x49,0x3A};
  static const uint8_t g_H[] = {0x7F,0x08,0x08,0x08,0x7F};
  static const uint8_t g_I[] = {0x00,0x41,0x7F,0x41,0x00};
  static const uint8_t g_J[] = {0x20,0x40,0x41,0x3F,0x01};
  static const uint8_t g_K[] = {0x7F,0x08,0x14,0x22,0x41};
  static const uint8_t g_L[] = {0x7F,0x40,0x40,0x40,0x40};
  static const uint8_t g_M[] = {0x7F,0x02,0x0C,0x02,0x7F};
  static const uint8_t g_N[] = {0x7F,0x04,0x08,0x10,0x7F};
  static const uint8_t g_O[] = {0x3E,0x41,0x41,0x41,0x3E};
  static const uint8_t g_P[] = {0x7F,0x09,0x09,0x09,0x06};
  static const uint8_t g_Q[] = {0x3E,0x41,0x51,0x21,0x5E};
  static const uint8_t g_R[] = {0x7F,0x09,0x19,0x29,0x46};
  static const uint8_t g_S[] = {0x26,0x49,0x49,0x49,0x32};
  static const uint8_t g_T[] = {0x01,0x01,0x7F,0x01,0x01};
  static const uint8_t g_U[] = {0x3F,0x40,0x40,0x40,0x3F};
  static const uint8_t g_V[] = {0x1F,0x20,0x40,0x20,0x1F};
  static const uint8_t g_W[] = {0x3F,0x40,0x38,0x40,0x3F};
  static const uint8_t g_X[] = {0x63,0x14,0x08,0x14,0x63};
  static const uint8_t g_Y[] = {0x07,0x08,0x70,0x08,0x07};
  static const uint8_t g_Z[] = {0x61,0x51,0x49,0x45,0x43};

  static const uint8_t g_a[] = {0x20,0x54,0x54,0x54,0x78};
  static const uint8_t g_b[] = {0x7F,0x48,0x44,0x44,0x38};
  static const uint8_t g_c[] = {0x38,0x44,0x44,0x44,0x28};
  static const uint8_t g_d[] = {0x38,0x44,0x44,0x28,0x7F};
  static const uint8_t g_e[] = {0x38,0x54,0x54,0x54,0x18};
  static const uint8_t g_f[] = {0x00,0x08,0x7E,0x09,0x02};
  static const uint8_t g_g[] = {0x18,0xA4,0xA4,0xA4,0x7C};
  static const uint8_t g_h[] = {0x7F,0x08,0x04,0x04,0x78};
  static const uint8_t g_i[] = {0x00,0x44,0x7D,0x40,0x00};
  static const uint8_t g_j[] = {0x00,0x40,0x84,0x7D,0x00};
  static const uint8_t g_k[] = {0x7F,0x10,0x28,0x44,0x00};
  static const uint8_t g_l[] = {0x00,0x41,0x7F,0x40,0x00};
  static const uint8_t g_m[] = {0x7C,0x04,0x18,0x04,0x78};
  static const uint8_t g_n[] = {0x7C,0x08,0x04,0x04,0x78};
  static const uint8_t g_o[] = {0x38,0x44,0x44,0x44,0x38};
  static const uint8_t g_p[] = {0x7C,0x14,0x14,0x14,0x08};
  static const uint8_t g_q[] = {0x08,0x14,0x14,0x18,0x7C};
  static const uint8_t g_r[] = {0x7C,0x08,0x04,0x04,0x08};
  static const uint8_t g_s[] = {0x48,0x54,0x54,0x54,0x24};
  static const uint8_t g_t[] = {0x04,0x3F,0x44,0x40,0x20};
  static const uint8_t g_u[] = {0x3C,0x40,0x40,0x20,0x7C};
  static const uint8_t g_v[] = {0x1C,0x20,0x40,0x20,0x1C};
  static const uint8_t g_w[] = {0x3C,0x40,0x30,0x40,0x3C};
  static const uint8_t g_x[] = {0x44,0x28,0x10,0x28,0x44};
  static const uint8_t g_y[] = {0x0C,0x50,0x50,0x50,0x3C};
  static const uint8_t g_z[] = {0x44,0x64,0x54,0x4C,0x44};

  static const uint8_t g_0[] = {0x3E,0x51,0x49,0x45,0x3E};
  static const uint8_t g_1[] = {0x00,0x42,0x7F,0x40,0x00};
  static const uint8_t g_2[] = {0x42,0x61,0x51,0x49,0x46};
  static const uint8_t g_3[] = {0x21,0x41,0x45,0x4B,0x31};
  static const uint8_t g_4[] = {0x18,0x14,0x12,0x7F,0x10};
  static const uint8_t g_5[] = {0x27,0x45,0x45,0x45,0x39};
  static const uint8_t g_6[] = {0x3C,0x4A,0x49,0x49,0x30};
  static const uint8_t g_7[] = {0x01,0x71,0x09,0x05,0x03};
  static const uint8_t g_8[] = {0x36,0x49,0x49,0x49,0x36};
  static const uint8_t g_9[] = {0x06,0x49,0x49,0x29,0x1E};

  static const uint8_t g_colon[] = {0x00,0x00,0x36,0x36,0x00};
  static const uint8_t g_dash[]  = {0x08,0x08,0x08,0x08,0x08};
  static const uint8_t g_dot[]   = {0x00,0x60,0x60,0x00,0x00};
  static const uint8_t g_slash[] = {0x20,0x10,0x08,0x04,0x02};
  static const uint8_t g_exclam[]= {0x00,0x00,0x5F,0x00,0x00};
  static const uint8_t g_pct[]   = {0x62,0x64,0x08,0x13,0x23};
  static const uint8_t g_comma[] = {0x00,0x60,0x40,0x00,0x00};
  static const uint8_t g_space[] = {0x00,0x00,0x00,0x00,0x00};

  switch (c) {
    case 'A': return g_A; case 'B': return g_B; case 'C': return g_C;
    case 'D': return g_D; case 'E': return g_E; case 'F': return g_F;
    case 'G': return g_G; case 'H': return g_H; case 'I': return g_I;
    case 'J': return g_J; case 'K': return g_K; case 'L': return g_L;
    case 'M': return g_M; case 'N': return g_N; case 'O': return g_O;
    case 'P': return g_P; case 'Q': return g_Q; case 'R': return g_R;
    case 'S': return g_S; case 'T': return g_T; case 'U': return g_U;
    case 'V': return g_V; case 'W': return g_W; case 'X': return g_X;
    case 'Y': return g_Y; case 'Z': return g_Z;
    case 'a': return g_a; case 'b': return g_b; case 'c': return g_c;
    case 'd': return g_d; case 'e': return g_e; case 'f': return g_f;
    case 'g': return g_g; case 'h': return g_h; case 'i': return g_i;
    case 'j': return g_j; case 'k': return g_k; case 'l': return g_l;
    case 'm': return g_m; case 'n': return g_n; case 'o': return g_o;
    case 'p': return g_p; case 'q': return g_q; case 'r': return g_r;
    case 's': return g_s; case 't': return g_t; case 'u': return g_u;
    case 'v': return g_v; case 'w': return g_w; case 'x': return g_x;
    case 'y': return g_y; case 'z': return g_z;
    case '0': return g_0; case '1': return g_1; case '2': return g_2;
    case '3': return g_3; case '4': return g_4; case '5': return g_5;
    case '6': return g_6; case '7': return g_7; case '8': return g_8;
    case '9': return g_9;
    case ':': return g_colon; case '-': return g_dash;
    case '.': return g_dot;   case '/': return g_slash;
    case '!': return g_exclam; case '%': return g_pct;
    case ',': return g_comma;
    default:  return g_space;
  }
}

// Pixel width of a UTF-8 string: Chinese glyphs are full width (16*scale),
// ASCII is 6*scale per character, minus the trailing spacing column.
int textWidth(int charCount, int scale) {
  if (charCount <= 0) return 0;
  return charCount * (5 * scale + scale) - scale;
}

int textWidthUtf8(const char* msg, int scale) {
  if (!msg || !*msg) return 0;
  int w = 0;
  for (const char* p = msg; *p; ) {
    bool wide = ((uint8_t)*p >= 0xE0);
    w += (wide ? CJK_GLYPH_W : 6) * scale;
    p += wide ? 3 : 1;
  }
  return w - scale;
}

int utf8Len(const char* msg) {
  int n = 0;
  for (const char* p = msg; p && *p; n++) p += ((uint8_t)*p >= 0xE0) ? 3 : 1;
  return n;
}

// ── Banded canvas ────────────────────────────────────────────
// colorBuf holds one band of the panel, not the whole picture. Rows
// are addressed in pre-rotation space: logical row py lands on
// pre-rotation row H-1-py, and the band starts at s_bandRBase.
static int s_bandRBase = 0;
static int s_bandRows  = 0;

void canvasBindBand(uint8_t* buf, int rBase, int rows) {
  colorBuf    = buf;
  s_bandRBase = rBase;
  s_bandRows  = rows;
  memset(buf, 0x55, (size_t)rows * (W / 4));   // 0x55 = all COLOR_WHITE
}

// ── Canvas writes ────────────────────────────────────────────
// The panel is driven through epdWriteMapped2bpp(), which applies its
// own 180-degree rotation. These helpers apply the matching rotation
// as they write, so callers work in plain logical coordinates with
// (0,0) at the top-left of the finished image.
//
// Canvas layout: 2 bits per pixel, 4 pixels per byte, most-significant
// pixel first (bit 6-7 = leftmost). COLOR_* values are the panel's raw
// codes, so they are written through unchanged.

// Write one pixel through the canvas transform, clipped to the band.
static inline void plotPixel(int x, int y, uint8_t color) {
  int px = W - 1 - x;
  int py = H - 1 - y;
  int r = py - s_bandRBase;             // row within the current band
  if (px < 0 || px >= W || r < 0 || r >= s_bandRows) return;
  int pos = r * (W / 4) + px / 4;
  int shift = 6 - ((px % 4) * 2);
  colorBuf[pos] = (colorBuf[pos] & ~(0x03 << shift)) | (color << shift);
}

// A 16x16 Chinese glyph scaled by `scale` (full width 16*scale), or a
// 5x7 ASCII glyph (advance 6*scale). Returns the advance consumed.
static int drawOneGlyph(const char* p, int x, int y, int scale, uint8_t color) {
  if ((uint8_t)p[0] >= 0xE0 && (uint8_t)p[1] && (uint8_t)p[2]) {
    const uint8_t* g = cjkGlyph(p);
    if (g) {
      for (int row = 0; row < CJK_GLYPH_H; row++) {
        uint8_t hi = g[row * 2], lo = g[row * 2 + 1];
        for (int col = 0; col < CJK_GLYPH_W; col++) {
          uint8_t bit = (col < 8) ? (hi >> (7 - col)) : (lo >> (7 - (col - 8)));
          if (!(bit & 1)) continue;
          for (int dy = 0; dy < scale; dy++)
            for (int dx = 0; dx < scale; dx++)
              plotPixel(x + col * scale + dx, y + row * scale + dy, color);
        }
      }
      return CJK_GLYPH_W * scale;
    }
    return CJK_GLYPH_W * scale;   // unknown character: leave a blank cell
  }

  const uint8_t* glyph = getGlyph(p[0]);
  for (int col = 0; col < 5; col++) {
    for (int row = 0; row < 7; row++) {
      if (!(glyph[col] & (1 << row))) continue;
      for (int dy = 0; dy < scale; dy++)
        for (int dx = 0; dx < scale; dx++)
          plotPixel(x + col * scale + dx, y + row * scale + dy, color);
    }
  }
  return 5 * scale + scale;   // 5 columns plus one column of spacing
}

// A line that contains at least one full-width glyph is laid out on a
// CJK_GLYPH_H * scale line box. The 5x7 ASCII face is less than half that
// tall, so centring it in the same box is what makes "累计 254" look like one
// line instead of digits floating at the top. Pure-ASCII lines keep y as
// their own top, so headings and the big counter are unaffected.
void drawTextColor(const char* msg, int x, int y, int scale, uint8_t color) {
  if (!colorBuf) return;
  int asciiDy = 0;
  for (const char* p = msg; *p; p++) {
    if ((uint8_t)*p >= 0xE0) { asciiDy = (CJK_GLYPH_H - 7) * scale / 2; break; }
  }
  int cx = x;
  for (const char* p = msg; *p; ) {
    bool wide = ((uint8_t)*p >= 0xE0);
    int adv = drawOneGlyph(p, cx, y + (wide ? 0 : asciiDy), scale, color);
    // UTF-8: a lead byte >= 0xE0 means a three-byte character; anything
    // else here is ASCII. (Two-byte sequences are not used by the UI.)
    p += wide ? 3 : 1;
    cx += adv;
  }
}

void fillRectColor(int x, int y, int w, int h, uint8_t color) {
  if (!colorBuf) return;

  for (int py = y; py < y + h; py++) {
    if (py < 0 || py >= H) continue;
    for (int px = x; px < x + w; px++) {
      if (px < 0 || px >= W) continue;

      int rpx = W - 1 - px;
      int r   = (H - 1 - py) - s_bandRBase;   // row within the band
      if (r < 0 || r >= s_bandRows) continue;

      int pos = r * (W / 4) + rpx / 4;
      int shift = 6 - ((rpx % 4) * 2);
      colorBuf[pos] = (colorBuf[pos] & ~(0x03 << shift)) | (color << shift);
    }
  }
}

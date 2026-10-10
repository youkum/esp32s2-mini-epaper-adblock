#ifndef ADBLOCK_CJK_FONT_H
#define ADBLOCK_CJK_FONT_H

#include <Arduino.h>

// The dashboard prints in Chinese, which the 5x7 ASCII font cannot do. A
// full CJK font is far too big for 4 MB of flash, so only the characters
// the UI actually uses are stored, each as a 16x16 1-bit bitmap.
#define CJK_GLYPH_W 16
#define CJK_GLYPH_H 16
#define CJK_GLYPH_BYTES (CJK_GLYPH_W / 8 * CJK_GLYPH_H)   // 32

// Look up a glyph by the UTF-8 bytes of one character. Returns nullptr
// when the character is not in the table.
const uint8_t* cjkGlyph(const char* utf8);

#endif // ADBLOCK_CJK_FONT_H

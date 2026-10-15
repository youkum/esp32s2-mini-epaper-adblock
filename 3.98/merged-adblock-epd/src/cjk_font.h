// cjk_font.h —— 仪表盘 UI 专用中文点阵字库（16x16 单色字形，按需子集）。
#ifndef ADBLOCK_CJK_FONT_H
#define ADBLOCK_CJK_FONT_H

#include <Arduino.h>

// 仪表盘需要显示中文，而 5x7 ASCII 字体做不到。完整 CJK 字体
// 远超 4 MB flash 的承受能力，因此只保存 UI 实际用到的字符，
// 每个字符以 16x16 的 1 位位图存储。
#define CJK_GLYPH_W 16
#define CJK_GLYPH_H 16
#define CJK_GLYPH_BYTES (CJK_GLYPH_W / 8 * CJK_GLYPH_H)   // 32

// 按单个字符的 UTF-8 字节查找字形。字符不在表中时返回 nullptr。
const uint8_t* cjkGlyph(const char* utf8);

#endif // ADBLOCK_CJK_FONT_H

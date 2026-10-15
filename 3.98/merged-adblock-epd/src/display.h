#ifndef ADBLOCK_DISPLAY_H
#define ADBLOCK_DISPLAY_H

#include <Arduino.h>

// 四色面板的颜色常量。这些值就是面板原生的 2bpp 编码——
// epd_driver.cpp 中的 packMonoPixelGroup() 以 0x00 表示黑色、
// 0x01 表示白色，因此无需额外的映射层。
#define COLOR_BLACK   0x00
#define COLOR_WHITE   0x01
#define COLOR_RED     0x02
#define COLOR_YELLOW  0x03

// 查找一个字符的字模数据（5x7 像素点阵字体）。
const uint8_t* getGlyph(char c);

// 在 2bpp 画布的 (x, y) 处绘制按比例缩放的文本。
void drawTextColor(const char* msg, int x, int y, int scale, uint8_t color);

// 在 2bpp 画布中绘制填充矩形。
void fillRectColor(int x, int y, int w, int h, uint8_t color);

// 给定缩放倍数下 N 个字符的像素宽度（仅限 ASCII）。
int textWidth(int charCount, int scale);

// UTF-8 字符串的像素宽度，中文字模按全宽计算。
int textWidthUtf8(const char* msg, int scale);

// UTF-8 字符串的字符数（不是字节数）。
int utf8Len(const char* msg);

// ── 分带画布（banded canvas）─────────────────────────────────
// 把各绘制辅助函数指向 epdSendFrame() 即将流式输出的分带。`buf` 保存
// 从旋转前行 `rBase` 开始的 `rows` 个旋转前行，并已被清成白色。
// 落在分带之外的绘制调用会被丢弃，因此调用方可以简单地在每个分带
// 都重绘整屏内容。
void canvasBindBand(uint8_t* buf, int rBase, int rows);

#endif // ADBLOCK_DISPLAY_H

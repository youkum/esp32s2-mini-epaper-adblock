#include "display.h"
#include "config.h"
#include "cjk_font.h"

// ── 统一的 5x7 像素点阵字体 ─────────────────────────────────
// 每个字模为 5 列 x 7 行，按列存储（column-major）。
// bit 0 = 顶行，bit 6 = 底行。

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

// UTF-8 字符串的像素宽度：中文字模为全宽（16*scale），ASCII 每字符
// 占 6*scale，末尾再减去一个间隔列。
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

// ── 分带画布（banded canvas）─────────────────────────────────
// colorBuf 只保存面板的一个分带，而非整幅画面。行地址按旋转前空间
// 计算：逻辑行 py 对应旋转前行 H-1-py，分带从 s_bandRBase 开始。
static int s_bandRBase = 0;
static int s_bandRows  = 0;

// 当前分带实际能显示的逻辑 y 行范围。plotPixel() 以 r = (H-1-y) -
// s_bandRBase 必须落在 [0, s_bandRows) 内做裁剪，反解后即此闭区间。
// 分带渲染器每带都会整屏重绘一遍，因此下面的辅助函数用它直接跳过
// 完全落在当前分带之外的字模与矩形，而不是逐像素走一遍、最后全被
// plotPixel() 丢弃。
static inline void bandYRanges(int* topIncl, int* botIncl) {
  *topIncl = H - s_bandRBase - s_bandRows;   // 最后一个分带时可能 < 0
  *botIncl = H - 1 - s_bandRBase;            // <= H-1
}

static inline bool rowsHitBand(int yTop, int height) {
  if (height <= 0) return false;
  int t, b;
  bandYRanges(&t, &b);
  int yBot = yTop + height - 1;
  return yBot >= t && yTop <= b;
}

void canvasBindBand(uint8_t* buf, int rBase, int rows) {
  colorBuf    = buf;
  s_bandRBase = rBase;
  s_bandRows  = rows;
  memset(buf, 0x55, (size_t)rows * (W / 4));   // 0x55 = 每个像素均为 COLOR_WHITE
}

// ── 画布写入 ─────────────────────────────────────────────────
// 面板经由 epdWriteMapped2bpp() 驱动，后者自带 180 度旋转。
// 这些辅助函数在写入时施加配套的旋转，因此调用方使用普通逻辑坐标
// 即可，(0,0) 位于成品图像的左上角。
//
// 画布布局：每像素 2 位，每字节 4 像素，最高位像素在前
// （bit 6-7 = 最左像素）。COLOR_* 各值即面板的原始编码，因此
// 原样写入、不做转换。

// 经画布变换写入单个像素，并裁剪到当前分带。
static inline void plotPixel(int x, int y, uint8_t color) {
  int px = W - 1 - x;
  int py = H - 1 - y;
  int r = py - s_bandRBase;             // 当前分带内的行号
  if (px < 0 || px >= W || r < 0 || r >= s_bandRows) return;
  int pos = r * (W / 4) + px / 4;
  int shift = 6 - ((px % 4) * 2);
  colorBuf[pos] = (colorBuf[pos] & ~(0x03 << shift)) | (color << shift);
}

// 绘制按 `scale` 缩放的 16x16 中文字模（全宽 16*scale），或 5x7
// ASCII 字模（步进 6*scale）。返回消耗的步进宽度。
static int drawOneGlyph(const char* p, int x, int y, int scale, uint8_t color) {
  if ((uint8_t)p[0] >= 0xE0 && (uint8_t)p[1] && (uint8_t)p[2]) {
    const uint8_t* g = cjkGlyph(p);
    if (g) {
      // 字模的行框不落在当前分带时整字跳过；无论是否真正绘制，
      // 步进都照常返回，以保证排版完全一致。
      if (rowsHitBand(y, CJK_GLYPH_H * scale)) {
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
      }
      return CJK_GLYPH_W * scale;
    }
    return CJK_GLYPH_W * scale;   // 未知字符：留一个空白格
  }

  const uint8_t* glyph = getGlyph(p[0]);
  if (rowsHitBand(y, 7 * scale)) {
    for (int col = 0; col < 5; col++) {
      for (int row = 0; row < 7; row++) {
        if (!(glyph[col] & (1 << row))) continue;
        for (int dy = 0; dy < scale; dy++)
          for (int dx = 0; dx < scale; dx++)
            plotPixel(x + col * scale + dx, y + row * scale + dy, color);
      }
    }
  }
  return 5 * scale + scale;   // 5 列字模加 1 列间隔
}

// 只要一行里含至少一个全宽字模，整行就按 CJK_GLYPH_H * scale 的
// 行框排版。5x7 的 ASCII 字面高度不到它的一半，因此把 ASCII 垂直
// 居中放进同一个行框，才能让「累计 254」看起来像一行，而不是数字
// 悬在顶部。纯 ASCII 行仍以 y 作为自身顶部，因此标题与大号计数器
// 不受影响。
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
    // UTF-8：首字节 >= 0xE0 表示三字节字符；此处其余均为 ASCII。
    // （UI 不使用两字节序列。）
    p += wide ? 3 : 1;
    cx += adv;
  }
}

void fillRectColor(int x, int y, int w, int h, uint8_t color) {
  if (!colorBuf) return;

  // 完全不落在当前分带内的矩形（顶栏、底部分隔线、大多数直方图柱）
  // 若不提前返回，其每个像素都会被遍历一遍，然后才在循环内被裁掉。
  if (w <= 0 || !rowsHitBand(y, h)) return;

  for (int py = y; py < y + h; py++) {
    if (py < 0 || py >= H) continue;
    for (int px = x; px < x + w; px++) {
      if (px < 0 || px >= W) continue;

      int rpx = W - 1 - px;
      int r   = (H - 1 - py) - s_bandRBase;   // 分带内的行号
      if (r < 0 || r >= s_bandRows) continue;

      int pos = r * (W / 4) + rpx / 4;
      int shift = 6 - ((rpx % 4) * 2);
      colorBuf[pos] = (colorBuf[pos] & ~(0x03 << shift)) | (color << shift);
    }
  }
}

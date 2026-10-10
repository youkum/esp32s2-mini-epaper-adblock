# Generate 16x16 1-bit glyph bitmaps from a real Chinese font.
# Hand-authored bitmaps produced look-alike hanzi, so the table is
# derived from SimHei and only the bytes are committed.
import os
from PIL import Image, ImageDraw, ImageFont

CHARS = "".join(dict.fromkeys(
    "今日拦截查询总数率次每小时暂无据域名排行榜记录累计运天更新配网模式浏览器打开启动中正在连接"))

SIZE = 16
# SimHei ships with Windows; override with CJK_FONT=... on other hosts.
FONT = os.environ.get("CJK_FONT", r"C:\Windows\Fonts\simhei.ttf")
font = ImageFont.truetype(FONT, SIZE)

out = []
for ch in CHARS:
    img = Image.new("L", (SIZE, SIZE), 0)
    d = ImageDraw.Draw(img)
    bbox = d.textbbox((0, 0), ch, font=font)
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    d.text(((SIZE - w) / 2 - bbox[0], (SIZE - h) / 2 - bbox[1]), ch, font=font, fill=255)
    rows = []
    for y in range(SIZE):
        hi = lo = 0
        for x in range(SIZE):
            if img.getpixel((x, y)) > 96:
                if x < 8:
                    hi |= 1 << (7 - x)
                else:
                    lo |= 1 << (7 - (x - 8))
        rows.append((hi, lo))
    out.append((ch, rows))

def cpp_bytes(rows):
    return ",\n  ".join("0x%02X,0x%02X" % r for r in rows)

parts = []
for i, (ch, rows) in enumerate(out):
    parts.append("static const uint8_t g%d[] = {\n  %s\n};" % (i, cpp_bytes(rows)))

table = []
for i, (ch, rows) in enumerate(out):
    table.append('  {"%s", g%d}' % (ch, i))

HEAD = """#include "cjk_font.h"

// Chinese glyphs for the dashboard labels.
//
// The 5x7 ASCII font cannot express Chinese and a full CJK font is far too
// large for 4 MB of flash, so only the characters the UI prints are stored:
// 16x16 1-bit bitmaps, row-major, two bytes per row (bits 7..0 = leftmost
// eight columns, then the next eight).
//
// These are NOT hand-drawn. An earlier hand-authored table produced
// look-alike characters (jin-ri-lan-jie came out as zi-ri-wan-yi): Chinese
// glyphs are dense enough that a nearby-but-wrong bitmap still reads as "a
// hanzi", so the error is invisible until real text is on the glass. The
// table is generated from SimHei by tools/gen_cjk_font.py instead.

"""

MID = """
struct CjkGlyph {
  const char*    utf8;
  const uint8_t* bits;
};

static const CjkGlyph g_glyphs[] = {
%s
};

static const int CJK_COUNT = sizeof(g_glyphs) / sizeof(g_glyphs[0]);

const uint8_t* cjkGlyph(const char* utf8) {
  for (int i = 0; i < CJK_COUNT; i++) {
    if (utf8[0] == g_glyphs[i].utf8[0] &&
        utf8[1] == g_glyphs[i].utf8[1] &&
        utf8[2] == g_glyphs[i].utf8[2]) {
      return g_glyphs[i].bits;
    }
  }
  return nullptr;
}
"""

src = HEAD + "\n\n".join(parts) + "\n" + (MID % ",\n".join(table))

path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    "src", "cjk_font.cpp")
open(path, "w", encoding="utf-8").write(src)
print("glyphs", len(out))
print("".join(c for c, _ in out))
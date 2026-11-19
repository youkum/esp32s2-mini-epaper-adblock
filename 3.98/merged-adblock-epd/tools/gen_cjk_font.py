# 从真实字体生成 16x16 单色点阵；生成工具的许可不替代输入字体的许可。
# 默认 SimHei 点阵的再分发许可尚未确认，公开发行前见根目录第三方声明。
import os
from PIL import Image, ImageDraw, ImageFont

CHARS = "".join(dict.fromkeys(
    "今日拦截查询总数率次每小时暂无据域名排行榜记录累计运天更新配网模式浏览器打开启动中正在连接"))

SIZE = 16
# 使用 CJK_FONT 指定有权使用的字体文件，不能假定所有 Windows 都安装了 SimHei。
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

// 仪表盘中文字形，由 tools/gen_cjk_font.py 从所选字体生成。
// 只保存实际使用的字符，避免把整套 CJK 字体装进 4 MB flash。
// 每字 16x16 单色点阵，按行存储；每行两字节，从高位到低位表示左右各八列。
// 字体来源和适用许可见根目录 THIRD-PARTY-NOTICES.md；不要手工绘制造型近似的汉字。

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

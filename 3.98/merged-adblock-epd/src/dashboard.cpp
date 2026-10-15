#include "dashboard.h"
#include "display.h"
#include "config.h"
#include "epd_driver.h"
#include "tracker.h"
#include <time.h>

extern uint32_t deviceUptimeSec();
extern const char* deviceIpStr();

// ── 屏幕选择 ──────────────────────────────────────────────────────
// epdSendFrame() 逐 band 绘制面板，并回调我们填充每一个 band。
// 一个渲染器绘制统计仪表盘，另一个绘制纯消息屏；二者都通过
// 同一组带裁剪的 canvas 辅助函数写入，因此每次调用都是对该
// band 的完整重绘。

enum Screen { SCREEN_DASHBOARD, SCREEN_MESSAGE };
static Screen s_screen = SCREEN_DASHBOARD;
static char   s_msg[3][40] = {{0}, {0}, {0}};

static void renderDashboard();
static void renderMessage();

static void paintBand(int rBase, int rows, uint8_t* buf) {
  canvasBindBand(buf, rBase, rows);
  if (s_screen == SCREEN_DASHBOARD) renderDashboard();
  else                              renderMessage();
}

// 流式发出完整一帧后立即返回，此时面板仍在迁移颜料；主 loop()
// 会调用 epdServiceRefresh() 完成收尾，因此玻璃面板所需的
// 约 15 s 不会妨碍 DNS 引擎应答查询。
static void flushScreen() {
  uint32_t ms = epdSendFrame(paintBand);
  Serial.printf("[DASH] frame streamed in %lu ms, panel refreshing\n",
                (unsigned long)ms);
}

// ── 布局网格 ───────────────────────────────────────────────────────────
// 整屏共用一套网格。所有位置都用这些常量表达，使分隔线、
// 左右两列与页脚都落在同一条边上。
//
//   +--------------------------------------------------+
//   | ADBLOCK DNS                          <device ip> |  黑色页眉
//   |                                                  |
//   |  今日拦截                 每小时拦截             |
//   |  1,234                    [ 24-bar histogram ]   |
//   |  -------------------      --------------------   |
//   |  查询总数          9,999                         |
//   |  拦截率            12.3%                         |
//   |  ----------------------------------------------  |
//   |  域名排行榜                                      |
//   |  1  doubleclick.net   42   6  ...                |
//   |  ----------------------------------------------  |
//   |  累计 1,234   运行 0天1时      更新 21:15        |
//   +--------------------------------------------------+

static const int PAD           = 24;
static const int HEADER_H      = 48;
static const int COL_L_X       = 24;                    // 左列
static const int COL_L_W       = 320;                   // 24 .. 344
static const int COL_R_X       = 400;                   // 右列
static const int COL_R_W       = W - COL_R_X - PAD;     // 400 .. 744
static const int SECTION_Y     = 68;                    // 区块标题行
static const int RULE_TOP_Y    = 176;                   // 数字下方的分隔线
static const int RULE_MID_Y    = 278;                   // 列表上方的分隔线
static const int LIST_TITLE_Y  = 294;
static const int LIST_Y0       = 334;
static const int LIST_PITCH    = 30;
static const int LIST_ROWS     = 5;                     // 每列行数（共 10 行）
static const int FOOTER_RULE_Y = 500;
static const int FOOTER_Y      = 512;

// ── 文本辅助函数 ──────────────────────────────────────────────────────

static void textCentered(const char* msg, int y, int scale, uint8_t color) {
  int x = (W - textWidthUtf8(msg, scale)) / 2;
  if (x < 2) x = 2;
  drawTextColor(msg, x, y, scale, color);
}

// 将一段 ASCII 文本按固定右边缘右对齐。
static void textRight(const char* msg, int right, int y, int scale, uint8_t color) {
  int x = right - textWidth(strlen(msg), scale);
  if (x < 0) x = 0;
  drawTextColor(msg, x, y, scale, color);
}

// 区块标题，左侧带一个小实心色块。这个色块提供了白底纯文字
// 无法表达的分组感：在中文字形宽 16 px 的 E-Paper 面板上，
// 除此之外没有别的办法体现层级。
static void sectionTitle(int x, int y, const char* text) {
  fillRectColor(x, y + 8, 6, 16, COLOR_BLACK);
  drawTextColor(text, x + 16, y, 2, COLOR_BLACK);
}

static void hRule(int x, int y, int w) {
  fillRectColor(x, y, w, 2, COLOR_BLACK);
}

// 千分位分组的整数，例如 1234567 -> "1,234,567"。
static void fmtGrouped(uint32_t v, char* out, size_t outLen) {
  char raw[16];
  snprintf(raw, sizeof(raw), "%lu", (unsigned long)v);
  int n = strlen(raw);
  int o = 0;
  for (int i = 0; i < n; i++) {
    if (i > 0 && ((n - i) % 3) == 0 && o < (int)outLen - 1) out[o++] = 0x2C;
    if (o < (int)outLen - 1) out[o++] = raw[i];
  }
  out[o] = 0;
}

// ── 页眉 ───────────────────────────────────────────────────────────────────

// 面板自身地址是用户最需要从屏上拿走的信息：既是在手机上输入
// 以访问仪表盘的地址，也是要把 DNS 指向的地址。因此它以白色
// 右对齐显示在黑色横条上——这是面板所能呈现的最高对比度。
static void drawHeader() {
  fillRectColor(0, 0, W, HEADER_H, COLOR_BLACK);
  drawTextColor("ADBLOCK DNS", 20, 17, 2, COLOR_WHITE);

  const char* ip = deviceIpStr();
  textRight(ip, W - 20, 17, 2, COLOR_WHITE);
}

// ── 今日数据 ────────────────────────────────────────────────────────

static void drawToday() {
  char buf[24];

  sectionTitle(COL_L_X, SECTION_Y, "今日拦截");

  fmtGrouped(trackerTodayBlocked(), buf, sizeof(buf));
  drawTextColor(buf, COL_L_X, 102, 8, COLOR_BLACK);

  hRule(COL_L_X, RULE_TOP_Y, COL_L_W);

  uint32_t tot = trackerTodayBlocked() + trackerTodayTotal();

  drawTextColor("查询总数", COL_L_X, 190, 2, COLOR_BLACK);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)tot);
  textRight(buf, COL_L_X + COL_L_W, 196, 3, COLOR_BLACK);

  uint32_t rate10 = tot ? (uint32_t)((uint64_t)trackerTodayBlocked() * 1000ULL / tot) : 0;
  drawTextColor("拦截率", COL_L_X, 234, 2, COLOR_BLACK);
  snprintf(buf, sizeof(buf), "%lu.%lu%%",
           (unsigned long)(rate10 / 10), (unsigned long)(rate10 % 10));
  textRight(buf, COL_L_X + COL_L_W, 240, 3, COLOR_BLACK);
}

// ── 每小时柱状条 ─────────────────────────────────────────────────

// 右列横向 24 个槽位，落在一条轴线上，与今日数字下方的分隔线
// 对齐。每根柱子都是纯黑实心，因此整条读起来是一个剪影。
static void drawHourly() {
  sectionTitle(COL_R_X, SECTION_Y, "每小时拦截");

  const int y0 = 104, h = 68;
  const int axisY = y0 + h;                 // == RULE_TOP_Y
  hRule(COL_R_X, axisY, COL_R_W);

  uint32_t peak = trackerPeakHour();
  if (peak == 0) {
    drawTextColor("暂无数据", COL_R_X, y0 + h / 2 - 16, 2, COLOR_BLACK);
    return;
  }

  int slotW = COL_R_W / HIST_SLOTS;
  const int gap = 3;
  for (int i = 0; i < HIST_SLOTS; i++) {
    uint32_t v = trackerHourBlocked(i);
    int bh = (int)((uint64_t)v * (h - 4) / peak);
    if (v > 0 && bh < 3) bh = 3;
    if (bh > 0) fillRectColor(COL_R_X + i * slotW, axisY - bh, slotW - gap, bh, COLOR_BLACK);
  }

  drawTextColor("00", COL_R_X, axisY + 6, 1, COLOR_BLACK);
  drawTextColor("12", COL_R_X + COL_R_W / 2 - 5, axisY + 6, 1, COLOR_BLACK);
  textRight("23", COL_R_X + COL_R_W, axisY + 6, 1, COLOR_BLACK);
}

// ── 拦截最多的域名列表 ──────────────────────────────────────────

// 一个标题、十行分两列：面板宽 768 px，单列只放五行会空出一半。
static void drawTopList() {
  sectionTitle(COL_L_X, LIST_TITLE_Y, "域名排行榜");

  TopEntry t[LIST_ROWS * 2];
  trackerGetTop(t, LIST_ROWS * 2);

  if (!t[0].domain[0] || t[0].count == 0) {
    drawTextColor("暂无拦截记录", COL_L_X, LIST_Y0, 2, COLOR_BLACK);
    return;
  }

  for (int i = 0; i < LIST_ROWS * 2; i++) {
    if (!t[i].domain[0] || t[i].count == 0) continue;

    const int col = i / LIST_ROWS;          // 0 = 左列, 1 = 右列
    const int row = i % LIST_ROWS;
    const int x0  = col ? COL_R_X : COL_L_X;
    const int w   = col ? COL_R_W : COL_L_W;
    const int y   = LIST_Y0 + row * LIST_PITCH;

    char rank[8];
    snprintf(rank, sizeof(rank), "%d", i + 1);
    drawTextColor(rank, x0, y, 2, COLOR_BLACK);

    char cnt[16];
    fmtGrouped(t[i].count, cnt, sizeof(cnt));
    textRight(cnt, x0 + w, y, 2, COLOR_BLACK);

    // 域名占用排名槽位与计数之间剩余的全部宽度。
    char name[64];
    strncpy(name, t[i].domain, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    const int nameX = x0 + 30;
    int maxChars = (x0 + w - textWidth(strlen(cnt), 2) - 12 - nameX) / 12;
    if (maxChars < 4) maxChars = 4;
    if ((int)strlen(name) > maxChars) name[maxChars] = 0;
    drawTextColor(name, nameX, y, 2, COLOR_BLACK);
  }
}

// ── 页脚 ───────────────────────────────────────────────────────────────────

static void drawFooter() {
  hRule(COL_L_X, FOOTER_RULE_Y, W - 2 * PAD);

  char buf[24];
  char line[64];

  fmtGrouped(trackerAllTimeBlocked(), buf, sizeof(buf));
  snprintf(line, sizeof(line), "累计 %s", buf);
  drawTextColor(line, COL_L_X, FOOTER_Y, 2, COLOR_BLACK);

  uint32_t up = deviceUptimeSec();
  snprintf(line, sizeof(line), "运行 %lu天%lu时",
           (unsigned long)(up / 86400), (unsigned long)((up % 86400) / 3600));
  textCentered(line, FOOTER_Y, 2, COLOR_BLACK);

  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm tmv; localtime_r(&now, &tmv);
    snprintf(line, sizeof(line), "更新 %02d:%02d", tmv.tm_hour, tmv.tm_min);
  } else {
    snprintf(line, sizeof(line), "更新 --:--");
  }
  int x = W - PAD - textWidthUtf8(line, 2);
  if (x < 500) x = 500;
  drawTextColor(line, x, FOOTER_Y, 2, COLOR_BLACK);
}

// ── 渲染器 ──────────────────────────────────────────────────────────────

static void renderDashboard() {
  drawHeader();
  drawToday();
  drawHourly();
  hRule(COL_L_X, RULE_MID_Y, W - 2 * PAD);
  drawTopList();
  drawFooter();
}

static void renderMessage() {
  fillRectColor(0, 0, W, HEADER_H, COLOR_BLACK);
  textCentered("ADBLOCK DNS", 17, 2, COLOR_WHITE);

  if (s_msg[0][0]) textCentered(s_msg[0], 150, 4, COLOR_BLACK);
  if (s_msg[1][0]) textCentered(s_msg[1], 260, 2, COLOR_BLACK);
  if (s_msg[2][0]) textCentered(s_msg[2], 320, 2, COLOR_BLACK);
}

static void copyLine(char* dst, const char* src) {
  if (!src) { dst[0] = 0; return; }
  strncpy(dst, src, 39);
  dst[39] = 0;
}

void dashboardDraw() {
  s_screen = SCREEN_DASHBOARD;
  flushScreen();
}

void dashboardMessage(const char* line1, const char* line2, const char* line3) {
  copyLine(s_msg[0], line1);
  copyLine(s_msg[1], line2);
  copyLine(s_msg[2], line3);
  s_screen = SCREEN_MESSAGE;
  flushScreen();
}

#include "tracker.h"
#include <LittleFS.h>
#include <time.h>

// ── 存储 ────────────────────────────────────────────────────
static TopEntry top[TOPN_KEEP];
static int      topCount = 0;

static uint32_t histBlocked[HIST_SLOTS];
static uint32_t histTotal[HIST_SLOTS];

static uint32_t todayBlocked = 0, todayTotal = 0;
static uint32_t allBlocked   = 0, allTotal   = 0;

static const char* TOTALS_PATH = "/totals.cfg";

// ── 辅助函数 ────────────────────────────────────────────────
// 当前本地小时，时钟尚未同步时返回 -1。直方图直接跳过
// 没有时间戳的流量，而不是把它塞进 0 时槽位——那会让午夜
// 显得人为地繁忙。
static int histHourNow() {
  time_t now = time(nullptr);
  if (now < 1700000000) return -1;   // 早于 2023 年 11 月 => NTP 尚未同步
  struct tm t;
  localtime_r(&now, &t);
  return t.tm_hour;
}

// 保持前 TOPN_KEEP 个条目按计数降序排列，这样 trackerGetTop()
// 只需原样复制一个前缀。用交换循环把一个条目下沉，在最长
// 12 个条目的列表上是 O(n)。
static void demoteIfNeeded(int idx) {
  while (idx + 1 < topCount && top[idx].count < top[idx + 1].count) {
    TopEntry tmp = top[idx];
    top[idx] = top[idx + 1];
    top[idx + 1] = tmp;
    idx++;
  }
}

// ── 初始化 / 持久化 ─────────────────────────────────────────
void trackerInit() {
  topCount = 0;
  for (int i = 0; i < HIST_SLOTS; i++) { histBlocked[i] = 0; histTotal[i] = 0; }
  todayBlocked = todayTotal = 0;
}

void trackerLoad() {
  File f = LittleFS.open(TOTALS_PATH, "r");
  if (!f) return;
  String b = f.readStringUntil('\n'); b.trim();
  String t = f.readStringUntil('\n'); t.trim();
  if (b.length()) allBlocked = (uint32_t)b.toInt();
  if (t.length()) allTotal   = (uint32_t)t.toInt();
  f.close();
}

void trackerSave() {
  File f = LittleFS.open(TOTALS_PATH, "w");
  if (!f) return;
  f.println(String(allBlocked));
  f.println(String(allTotal));
  f.close();
}

// ── 日切换 ──────────────────────────────────────────────────
void trackerRollDay() {
  todayBlocked = todayTotal = 0;
  topCount = 0;
  for (int i = 0; i < HIST_SLOTS; i++) { histBlocked[i] = 0; histTotal[i] = 0; }
}

// ── 记录 ────────────────────────────────────────────────────
// 每个 DNS 查询都会执行到这里，因此必须保持无内存分配且有
// 确定上界：只对最多 12 个条目做一次线性扫描，不用 String，
// 不用 malloc。
void trackerRecord(const char* domain, bool blocked) {
  if (blocked) { todayBlocked++; allBlocked++; } else { todayTotal++; allTotal++; }

  int hour = histHourNow();
  if (hour >= 0 && hour < HIST_SLOTS) {
    histTotal[hour]++;
    if (blocked) histBlocked[hour]++;
  }

  if (!blocked || !domain || !domain[0]) return;

  for (int i = 0; i < topCount; i++) {
    if (strcmp(top[i].domain, domain) == 0) { top[i].count++; demoteIfNeeded(i); return; }
  }
  if (topCount < TOPN_KEEP) {
    strncpy(top[topCount].domain, domain, sizeof(top[0].domain) - 1);
    top[topCount].domain[sizeof(top[0].domain) - 1] = 0;
    top[topCount].count = 1;
    topCount++;
    demoteIfNeeded(topCount - 1);
  }
}

// ── 访问器 ──────────────────────────────────────────────────
void trackerGetTop(TopEntry* out, int n) {
  if (n > TOPN_KEEP) n = TOPN_KEEP;
  for (int i = 0; i < n; i++) {
    if (i < topCount) out[i] = top[i];
    else { out[i].domain[0] = 0; out[i].count = 0; }
  }
}

uint32_t trackerHourBlocked(int h) { return (h >= 0 && h < HIST_SLOTS) ? histBlocked[h] : 0; }
uint32_t trackerHourTotal(int h)   { return (h >= 0 && h < HIST_SLOTS) ? histTotal[h]   : 0; }

uint32_t trackerTodayBlocked()  { return todayBlocked; }
uint32_t trackerTodayTotal()    { return todayTotal; }
uint32_t trackerAllTimeBlocked(){ return allBlocked; }
uint32_t trackerAllTimeTotal()  { return allTotal; }

uint32_t trackerPeakHour() {
  uint32_t peak = 0;
  for (int i = 0; i < HIST_SLOTS; i++) if (histBlocked[i] > peak) peak = histBlocked[i];
  return peak;
}

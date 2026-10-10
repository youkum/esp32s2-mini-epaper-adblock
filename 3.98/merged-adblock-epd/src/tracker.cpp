#include "tracker.h"
#include <LittleFS.h>
#include <time.h>

// ── Storage ──────────────────────────────────────────────────
static TopEntry top[TOPN_KEEP];
static int      topCount = 0;

static uint32_t histBlocked[HIST_SLOTS];
static uint32_t histTotal[HIST_SLOTS];

static uint32_t todayBlocked = 0, todayTotal = 0;
static uint32_t allBlocked   = 0, allTotal   = 0;

static const char* TOTALS_PATH = "/totals.cfg";

// ── Helpers ──────────────────────────────────────────────────
// Current local hour, or -1 if the clock has not synced yet. The
// histogram simply skips un-timed traffic rather than bucketing it
// into hour 0, which would make midnight look artificially busy.
static int histHourNow() {
  time_t now = time(nullptr);
  if (now < 1700000000) return -1;   // before Nov 2023 => NTP has not landed
  struct tm t;
  localtime_r(&now, &t);
  return t.tm_hour;
}

// Keep the first TOPN_KEEP entries sorted by count, descending, so
// trackerGetTop() is a plain copy of a prefix. Sinking one entry with
// a swap loop is O(n) on a list that is at most 12 long.
static void demoteIfNeeded(int idx) {
  while (idx + 1 < topCount && top[idx].count < top[idx + 1].count) {
    TopEntry tmp = top[idx];
    top[idx] = top[idx + 1];
    top[idx + 1] = tmp;
    idx++;
  }
}

// ── Init / persistence ───────────────────────────────────────
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

// ── Day rollover ─────────────────────────────────────────────
void trackerRollDay() {
  todayBlocked = todayTotal = 0;
  topCount = 0;
  for (int i = 0; i < HIST_SLOTS; i++) { histBlocked[i] = 0; histTotal[i] = 0; }
}

// ── Recording ────────────────────────────────────────────────
// Runs on every DNS query, so it stays allocation-free and bounded:
// one linear scan over at most 12 entries, no String, no malloc.
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

// ── Accessors ────────────────────────────────────────────────
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

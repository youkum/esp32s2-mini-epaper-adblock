#ifndef ADBLOCK_TRACKER_H
#define ADBLOCK_TRACKER_H

#include <Arduino.h>

// ── What the e-paper dashboard shows ─────────────────────────
// The DNS engine calls trackerRecord() for every query it decides on.
// Everything the screen renders is derived from that one call, so the
// counters here are the single source of truth for "what got blocked".

static const int TOPN_SLOTS   = 5;    // rows on the dashboard
static const int TOPN_KEEP    = 12;   // candidates kept in RAM (top N are a prefix)
static const int HIST_SLOTS   = 24;   // one bucket per hour of the current day

struct TopEntry {
  char     domain[64];
  uint32_t count;
};

void trackerInit();
void trackerRecord(const char* domain, bool blocked);
void trackerGetTop(TopEntry* out, int n);

// Current-hour / per-hour histogram for the bar strip.
uint32_t trackerHourBlocked(int hour);
uint32_t trackerHourTotal(int hour);

// Day-scoped totals (reset when the calendar day rolls over).
uint32_t trackerTodayBlocked();
uint32_t trackerTodayTotal();
uint32_t trackerAllTimeBlocked();
uint32_t trackerAllTimeTotal();

// Lifetime totals survive reboots (persisted to LittleFS).
void trackerLoad();
void trackerSave();

// Called from the DNS path when it notices the date changed.
void trackerRollDay();

// Highest single-hour block count today — used to scale the bar strip.
uint32_t trackerPeakHour();

#endif // ADBLOCK_TRACKER_H

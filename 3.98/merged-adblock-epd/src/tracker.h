#ifndef ADBLOCK_TRACKER_H
#define ADBLOCK_TRACKER_H

#include <Arduino.h>
#include "top_counter.h"

// ── E-Paper 仪表盘的数据来源 ─────────────────────────────────
// DNS 引擎对它裁定的每个查询都会调用 trackerRecord()。
// 屏幕渲染的一切都源自这一次调用，因此这里的计数器是
// 「哪些被拦截了」的唯一事实来源。

static const int TOPN_SLOTS   = 5;    // Web 展示的行数
static const int TOPN_KEEP    = 12;   // 固定候选容量（Top N 是其近似前缀）
static const int HIST_SLOTS   = 24;   // 当天每小时一个桶

void trackerInit();
void trackerRecord(const char* domain, bool blocked);
void trackerGetTop(TopEntry* out, int n);

// 供柱状条使用的当前小时 / 每小时直方图。
uint32_t trackerHourBlocked(int hour);
uint32_t trackerHourTotal(int hour);

// 按天统计的合计（日历日切换时清零）。
uint32_t trackerTodayBlocked();
uint32_t trackerTodayTotal();
uint32_t trackerAllTimeBlocked();
uint32_t trackerAllTimeTotal();

// 累计总量跨重启保留（持久化到 LittleFS）。
void trackerLoad();
void trackerSave();

// 由 DNS 路径在发现日期变化时调用。
void trackerRollDay();

// 今日单个小时的最高拦截数——用于给柱状条定标。
uint32_t trackerPeakHour();

#endif // ADBLOCK_TRACKER_H

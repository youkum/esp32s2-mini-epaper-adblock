#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
struct TopEntry { char domain[256]; uint32_t count; uint32_t error; };
// 固定容量 Space-Saving：计数为上界，count-error 为下界；满表后替换最小项。
// 保存完整 DNS 域名，避免长域名截断后无法再次命中。
template<size_t N> class TopCounter {
  TopEntry entries[N] = {};
  size_t used = 0;
public:
  void clear() { used = 0; }
  void record(const char* domain) {
    if (!domain || !*domain || strlen(domain) >= sizeof(entries[0].domain)) return;
    size_t i = 0;
    for (; i < used; ++i) if (!strcmp(entries[i].domain, domain)) break;
    if (i == used) {
      if (used < N) { ++used; entries[i].count = entries[i].error = 0; }
      else { i = N - 1; entries[i].error = entries[i].count; }
      strcpy(entries[i].domain, domain);
    }
    if (entries[i].count != UINT32_MAX) ++entries[i].count;
    while (i && entries[i].count > entries[i-1].count) {
      TopEntry t = entries[i]; entries[i] = entries[i-1]; entries[--i] = t;
    }
  }
  void get(TopEntry* out, int n) const {
    if (!out || n <= 0) return;
    for (int i=0; i<n; ++i) out[i] = size_t(i)<used ? entries[i] : TopEntry{};
  }
};

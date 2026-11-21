#pragma once
#include <stdint.h>
// 与 Python 构建器相同：加法、乘法使用 64 位中间值，不能先在 32 位回绕。
inline uint32_t bloomPosition(uint64_t h, uint32_t bits, uint8_t i) {
  uint32_t step = uint32_t(h >> 32);
  if (!step) step = UINT32_MAX;
  return uint32_t((uint64_t(uint32_t(h)) + uint64_t(i) * step) % bits);
}

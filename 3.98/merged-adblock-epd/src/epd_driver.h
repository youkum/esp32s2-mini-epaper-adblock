#pragma once
#include <Arduino.h>
// JDY79668 专用 bit-bang 驱动；唯一画布为 64 行、12,288 字节静态分带。
void gpioInit();
// 仅提交帧，不等待、不覆盖正在进行的帧。所有阶段由 loop() 服务。
bool epdSendFrame(void (*renderBand)(int rBase, int rows, uint8_t* buf));
bool epdRefreshActive();
bool epdServiceRefresh();
const char* epdStatus();
const char* epdError();
uint32_t epdFaults();
int epdRowsSent();

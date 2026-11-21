#pragma once
#include <stdint.h>
#include <stddef.h>
#include <algorithm>
using std::min;
constexpr int HIGH=1, LOW=0, INPUT=0, OUTPUT=1, INPUT_PULLUP=2;
unsigned long millis();
void digitalWrite(int,int);
int digitalRead(int);
void pinMode(int,int);
void delay(unsigned long);
struct FakeSerial { template<class... T> void printf(const char*,T...) {} void println() {} };
extern FakeSerial Serial;

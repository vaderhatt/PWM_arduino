#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
using std::max;
using std::min;
constexpr int HIGH = 1, OUTPUT = 1, INPUT_PULLUP = 2, FALLING = 2, HEX = 16;
constexpr int COM1A1 = 7, WGM11 = 1, WGM13 = 4, WGM12 = 3, CS10 = 0;
#define _BV(bit) (1U << (bit))
static uint16_t TCCR1A, TCCR1B, TCNT1, ICR1, OCR1A;
static uint32_t clockMs = 0;
static bool separateMicros = false;
static uint32_t clockUs = 0;
inline uint32_t millis() { return clockMs; }
inline uint32_t micros() { return separateMicros ? clockUs : clockMs * 1000U; }
inline void digitalWrite(int, int) {}
inline void pinMode(int, int) {}
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int, void (*)(), int) {}
inline void noInterrupts() {}
inline void interrupts() {}
inline long map(long x, long a, long b, long c, long d) {
  return (x - a) * (d - c) / (b - a) + c;
}
struct SerialStub {
  std::deque<char> input;
  void begin(int) {}
  int available() { return static_cast<int>(input.size()); }
  char read() { char value = input.front(); input.pop_front(); return value; }
  template<class T> void print(T) {}
  template<class T> void println(T, int) {}
  template<class T> void println(T) {}
  void receive(const std::string& bytes) { input.insert(input.end(), bytes.begin(), bytes.end()); }
};
static SerialStub Serial;

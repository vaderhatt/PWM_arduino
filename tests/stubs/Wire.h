#pragma once
#include "Arduino.h"
#define WIRE_HAS_TIMEOUT 1
struct WireStub {
  bool timeout = false;
  void begin() {}
  void beginTransmission(uint8_t) {}
  int endTransmission() { return 2; } // No display attached in host tests.
  void setWireTimeout(unsigned long, bool) {}
  bool getWireTimeoutFlag() { return timeout; }
  void clearWireTimeoutFlag() { timeout = false; }
};
static WireStub Wire;

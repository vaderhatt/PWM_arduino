#pragma once
#include "Arduino.h"
struct LiquidCrystal_I2C {
  LiquidCrystal_I2C(uint8_t, int, int) {}
  void init() {}
  void backlight() {}
  void clear() {}
  void setCursor(uint8_t, uint8_t) {}
  template<class T> void print(T) {}
};

#include "Arduino.h"
#include <cassert>
#include <iostream>

// Arduino generates these declarations before compiling an .ino file.
void updateFanRpm();
void updateFanKick();
void printStatus();
bool processMessage(const char*);
#include "../gpu_monitor.ino"

static void receive(const std::string& frame) {
  Serial.receive(frame);
  while (Serial.available()) loop();
}
static void fullSpeed() {
  assert(currentFanPwm == 255);
  assert(targetFanPwm == 255);
  assert(OCR1A == FAN_PWM_TOP);
  assert(!fanKicking);
  assert(!haveValidData);
}

int main() {
  setup();
  assert(!lcdReady);
  fullSpeed();
  clockMs = 10000;
  loop();
  fullSpeed();
  receive("40 0\n");
  assert(haveValidData && currentFanPwm == 0);
  receive("STATUS\n");
  assert(haveValidData && currentFanPwm == 0);
  clockMs += 2999;
  loop();
  assert(currentFanPwm == 0);
  clockMs++;
  receive("STATUS\n");
  fullSpeed();
  receive("60 20\r\n");
  assert(haveValidData && currentFanPwm > 0 && currentFanPwm < 255);
  receive("74 20\n");
  assert(currentFanPwm < 255);
  receive("75 20\n");
  assert(currentFanPwm == 255 && OCR1A == FAN_PWM_TOP);
  receive("90 20\n");
  assert(currentFanPwm == 255);
  receive("FAULT\n");
  fullSpeed();

  for (const char* invalid : {"garbage\n", "50\n", "40 0 95\n", "nan 0\n",
       "inf 0\n", "-1 0\n", "151 0\n", "40 -1\n", "40 101\n",
       "40 0junk\n", "40 0 95 nope\n", " \t\n"}) {
    receive("40 0\n");
    auto previousValidTime = lastMessageMs;
    clockMs += 10;
    receive(invalid);
    fullSpeed();
    assert(lastMessageMs == previousValidTime);
  }
  receive("40 0\n");
  receive(std::string("40 0") + std::string(300, ' ') + "95 100\n");
  fullSpeed();
  receive("40 0\n");
  receive(std::string("40 0\0 95 100\n", 13));
  fullSpeed();
  std::string many;
  for (int i = 0; i < 11; i++) many += "40 0 ";
  receive(many + "\n");
  fullSpeed();
  many.clear();
  for (int i = 0; i < 9; i++) many += "40 0 ";
  receive(many + "90 100\n");
  assert(haveValidData && currentFanPwm == 255);

  receive("40 0\n");
  receive("50 0\n");
  assert(fanKicking);
  receive("75 0\n");
  assert(currentFanPwm == 255 && !fanKicking);
  receive("40 0\n");
  receive("50 0\n");
  assert(fanKicking);
  receive("FAULT\n");
  clockMs += 1000;
  loop();
  fullSpeed(); // A pending kick must not restore the old target.

  clockMs = UINT32_MAX - 1000;
  receive("40 0\n");
  clockMs += 2999U;
  loop();
  assert(currentFanPwm == 0);
  clockMs++;
  loop();
  fullSpeed();

  receive("40 0\n");
  Serial.receive(std::string(10000, 'x'));
  clockMs += 3000;
  auto pending = Serial.input.size();
  loop();
  fullSpeed();
  assert(Serial.input.size() >= pending - SERIAL_BYTES_PER_LOOP);
  while (Serial.available()) loop();
  receive("\n40 0\n");
  assert(haveValidData && currentFanPwm == 0);

  LiquidCrystal_I2C display(0x27, 20, 4);
  lcd = &display;
  lcdReady = true;
  Wire.timeout = true;
  lcdPrintPadded(0, 0, "test");
  assert(!lcdReady);
  clockMs += 3000;
  loop();
  fullSpeed();
  separateMicros = true;
  for (unsigned int actualRpm : {600, 1000, 1500, 1800, 2000, 2400, 3000}) {
    haveTachPulse = false;
    tachIntervalSumUs = 0;
    tachIntervalCount = 0;
    fanRpm = 0;
    clockUs = 0;
    tachPulse();
    uint32_t period = 60000000UL / (actualRpm * TACH_PULSES_PER_REV);
    for (int i = 0; i < 100; i++) {
      clockUs += period;
      tachPulse();
      clockUs += 1000; // Noise between legitimate edges must not shift the period.
      tachPulse();
      clockUs -= 1000;
    }
    clockMs += 500;
    updateFanRpm();
    assert(fanRpm >= actualRpm - 2 && fanRpm <= actualRpm + 2);
  }
  clockUs += TACH_TIMEOUT_US + 1;
  clockMs += 500;
  updateFanRpm();
  assert(fanRpm == 0);
  tachPulse();
  assert(tachIntervalCount == 0);
  clockUs += 5000;
  tachPulse();
  clockMs += 500;
  updateFanRpm();
  assert(fanRpm == 3000);
  haveTachPulse = false;
  clockUs = UINT32_MAX - 2000;
  tachPulse();
  clockUs += 5000;
  tachPulse();
  clockMs += 500;
  updateFanRpm();
  assert(fanRpm == 3000);
  std::cout << "PASS: startup, silence, recovery, FAULT, malformed/oversized/NUL frames, "
               "GPU limit, hottest GPU, kick cancellation, clock wrap, input flood, LCD timeout, "
               "STATUS timeout, RPM 600-3000 with noise, tach stop/restart and micros wrap\n";
}

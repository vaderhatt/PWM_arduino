#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>

#define LCD_COLS    20
#define LCD_ROWS    4

// Fan PWM pin: Nano D9 is Timer1 OC1A. 4-wire PC fans expect ~25 kHz PWM.
const int fanPwmPin = 9;
const int fanTachPin = 2;  // INT0 on Nano
// Standard PC-fan tach setting. ~10 ms edges at full PWM agree with ~3000 RPM.
// Confirm absolute mechanical RPM with an independent tachometer if needed.
const unsigned int TACH_PULSES_PER_REV = 2;
const unsigned int FAN_PWM_TOP = 639;  // 16 MHz / (1 * (639 + 1)) = 25 kHz
const unsigned long TACH_TIMEOUT_US = 2000000UL;
const unsigned int FAN_MAX_VALID_RPM = 3500;
const uint32_t TACH_MIN_PULSE_US = 60000000UL / ((uint32_t)FAN_MAX_VALID_RPM * TACH_PULSES_PER_REV);

// Fan curve with hysteresis to prevent oscillation
const float FAN_START_TEMP = 50.0;
const float FAN_FULL_TEMP  = 75.0;
const float HYSTERESIS     = 3.0;
const int FAN_MIN_PWM      = 64;   // ~25%, ARCTIC curve is already ~1000 RPM here
const int FAN_KICK_PWM     = 180;  // short spin-up pulse when starting from 0
const unsigned int FAN_KICK_MS = 700;
const uint32_t DATA_TIMEOUT_MS = 3000UL;
const uint8_t SERIAL_BYTES_PER_LOOP = 64;

// Serial protocol: temperature/load pairs, or "FAULT\n" if GPU data is unavailable.
#define MAX_GPUS       10
#define MAX_MSG_LEN   256

LiquidCrystal_I2C* lcd = NULL;
static uint8_t lcdAddr = 0;
static bool lcdReady = false;

// Line buffer; discard an entire oversized frame, never its truncated prefix.
static char rxBuf[MAX_MSG_LEN];
static uint8_t rxIdx = 0;
static bool rxOverflow = false;

// Fan state for hysteresis
static float lastFanTemp = 0.0;
static int lastFanPwm    = 255;
static uint32_t lastMessageMs = 0;
static bool haveValidData = false;
static bool waitingShown = false;
static volatile bool haveTachPulse = false;
static volatile uint32_t lastTachPulseUs = 0;
static volatile uint32_t tachIntervalSumUs = 0;
static volatile unsigned int tachIntervalCount = 0;
static uint32_t lastRpmSampleMs = 0;
static uint32_t lastAvgTachUs = 0;
static unsigned int fanRpm = 0;
static int currentFanPwm = 255;
static int targetFanPwm = 255;
static bool fanKicking = false;
static unsigned long fanKickUntilMs = 0;

void tachPulse() {
  uint32_t now = micros();
  uint32_t interval = now - lastTachPulseUs;
  if (!haveTachPulse || interval > TACH_TIMEOUT_US) {
    lastTachPulseUs = now;
    haveTachPulse = true;
    return;  // The first edge after a stop is not a revolution interval.
  }
  if (interval < TACH_MIN_PULSE_US) return;
  if (tachIntervalCount < 1000) {
    tachIntervalSumUs += interval;
    tachIntervalCount++;
  }
  lastTachPulseUs = now;
}

void setupFanPwm25k() {
  digitalWrite(fanPwmPin, HIGH);
  pinMode(fanPwmPin, OUTPUT);
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;

  // Fast PWM, TOP=ICR1, non-inverting output on OC1A/D9, prescaler=1.
  ICR1 = FAN_PWM_TOP;
  OCR1A = FAN_PWM_TOP;  // Full speed until the first valid GPU sample.
  TCCR1A = _BV(COM1A1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);
}

void setFanPwm(int pwm) {
  if (pwm < 0) pwm = 0;
  if (pwm > 255) pwm = 255;
  currentFanPwm = pwm;
  OCR1A = (unsigned int)(((unsigned long)pwm * FAN_PWM_TOP) / 255UL);
}

void applyFanTarget(int pwm) {
  if (pwm < 0) pwm = 0;
  if (pwm > 255) pwm = 255;

  targetFanPwm = pwm;
  if (pwm == 255) {
    fanKicking = false;
    setFanPwm(255);
    return;
  }
  if (pwm > 0 && currentFanPwm == 0) {
    fanKicking = true;
    fanKickUntilMs = millis() + FAN_KICK_MS;
    setFanPwm(max(pwm, FAN_KICK_PWM));
    return;
  }

  if (!fanKicking) {
    setFanPwm(pwm);
  }
}

void enterFailsafe() {
  haveValidData = false;
  fanKicking = false;
  targetFanPwm = 255;
  lastFanPwm = 255;
  setFanPwm(255);
}

void checkDataTimeout() {
  if (!haveValidData || (uint32_t)(millis() - lastMessageMs) >= DATA_TIMEOUT_MS) {
    enterFailsafe();
  }
}

bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

uint8_t findLcdAddress() {
  // Common PCF8574 LCD backpacks are usually 0x20-0x27 or 0x3F.
  for (uint8_t addr = 0x20; addr <= 0x27; addr++) {
    if (i2cPresent(addr)) return addr;
  }
  if (i2cPresent(0x3F)) return 0x3F;
  if (i2cPresent(0x3E)) return 0x3E;
  return 0;
}

bool lcdHealthy() {
#if defined(WIRE_HAS_TIMEOUT)
  if (Wire.getWireTimeoutFlag()) {
    lcdReady = false;  // A failed display must not block fan supervision.
    Wire.clearWireTimeoutFlag();
  }
#endif
  return lcdReady;
}

void lcdPrintPadded(uint8_t col, uint8_t row, const char* text) {
  if (!lcdHealthy()) return;
  lcd->setCursor(col, row);
  if (!lcdHealthy()) return;
  uint8_t printed = 0;
  while (*text && printed < LCD_COLS) {
    lcd->print(*text++);
    if (!lcdHealthy()) return;
    printed++;
  }
  while (printed < LCD_COLS) {
    lcd->print(' ');
    if (!lcdHealthy()) return;
    printed++;
  }
}

void setup() {
  // Baud rate must match Python script.
  Serial.begin(9600);

  setupFanPwm25k();
  enterFailsafe();
  pinMode(fanTachPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(fanTachPin), tachPulse, FALLING);

  Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(25000UL, true);
#endif
  lcdAddr = findLcdAddress();
  if (lcdAddr != 0) {
    lcd = new LiquidCrystal_I2C(lcdAddr, LCD_COLS, LCD_ROWS);
    lcd->init();
    lcd->backlight();
    lcd->clear();
    lcdReady = true;
    lcdPrintPadded(0, 0, "GPU Monitor OK");
    char line[21];
    snprintf(line, sizeof(line), "LCD I2C: 0x%02X", lcdAddr);
    lcdPrintPadded(0, 1, line);
    lcdPrintPadded(0, 2, "Tach: D2 pullup");
    lcdPrintPadded(0, 3, "Fan 100%: no data");
    waitingShown = true;
  }

  Serial.print("GPU Monitor OK LCD=0x");
  Serial.println(lcdAddr, HEX);
}

void loop() {
  checkDataTimeout();
  updateFanRpm();
  updateFanKick();

  // Bound input work so a continuous stream cannot starve the timeout check.
  uint8_t bytesRead = 0;
  while (Serial.available() && bytesRead < SERIAL_BYTES_PER_LOOP) {
    bytesRead++;
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      // End of message — process it
      if (!rxOverflow && rxIdx > 0) {
        rxBuf[rxIdx] = '\0';
        if (strcmp(rxBuf, "STATUS") == 0) {
          checkDataTimeout();
          printStatus(); // Diagnostics do not refresh the temperature timeout.
        } else if (processMessage(rxBuf)) {
          lastMessageMs = millis();
          haveValidData = true;
        } else {
          enterFailsafe();
        }
        waitingShown = false;
      }
      rxIdx = 0;
      rxOverflow = false;
    } else if (rxOverflow) {
      continue;
    } else if (c == '\0') {
      rxOverflow = true;  // Do not accept a valid prefix before an embedded NUL.
      enterFailsafe();
      waitingShown = false;
    } else if (rxIdx < MAX_MSG_LEN - 1) {
      rxBuf[rxIdx++] = c;
    } else {
      rxOverflow = true;
      enterFailsafe();
      waitingShown = false;
    }
  }

  checkDataTimeout();
  if (lcdReady && !waitingShown && !haveValidData) {
    lcdPrintPadded(0, 0, "GPU data missing");
    char line[21];
    snprintf(line, sizeof(line), "LCD I2C: 0x%02X", lcdAddr);
    lcdPrintPadded(0, 1, line);
    snprintf(line, sizeof(line), "RPM %4u", fanRpm);
    lcdPrintPadded(0, 2, line);
    lcdPrintPadded(0, 3, "Fan 100%: FAILSAFE");
    waitingShown = true;
  }
}

void updateFanKick() {
  if (fanKicking && (long)(millis() - fanKickUntilMs) >= 0) {
    fanKicking = false;
    setFanPwm(targetFanPwm);
  }
}

void updateFanRpm() {
  uint32_t now = millis();
  uint32_t elapsed = now - lastRpmSampleMs;
  if (elapsed < 500) return;

  noInterrupts();
  uint32_t intervalSum = tachIntervalSumUs;
  unsigned int intervalCount = tachIntervalCount;
  uint32_t lastPulseUs = lastTachPulseUs;
  bool havePulse = haveTachPulse;
  tachIntervalSumUs = 0;
  tachIntervalCount = 0;
  interrupts();

  lastRpmSampleMs = now;

  if (!havePulse || (uint32_t)(micros() - lastPulseUs) > TACH_TIMEOUT_US) {
    fanRpm = 0;
    lastAvgTachUs = 0;
    return;
  }

  if (intervalCount == 0) {
    return;
  }

  uint32_t avgPulseUs = intervalSum / intervalCount;
  if (avgPulseUs == 0) return;
  lastAvgTachUs = avgPulseUs;

  unsigned long rpm = 60000000UL / (avgPulseUs * TACH_PULSES_PER_REV);
  if (rpm > FAN_MAX_VALID_RPM) {
    return;
  }

  unsigned int measured = (unsigned int)rpm;
  fanRpm = fanRpm == 0 ? measured : (unsigned int)((fanRpm * 3UL + measured) / 4UL);
}

void printStatus() {
  Serial.print("STATUS PWM="); Serial.print(currentFanPwm);
  Serial.print(" RPM="); Serial.print(fanRpm);
  Serial.print(" TACH_US="); Serial.print(lastAvgTachUs);
  Serial.print(" PPR="); Serial.print(TACH_PULSES_PER_REV);
  Serial.print(" VALID="); Serial.println(haveValidData ? 1 : 0);
}

bool parseValue(const char* token, float maximum, float& value) {
  if (token == NULL) return false;
  char* end = NULL;
  value = strtod(token, &end);
  return end != token && *end == '\0' && isfinite(value) && value >= 0 && value <= maximum;
}

bool processMessage(const char* msg) {
  // Skip empty messages
  const char* p = msg;
  while (*p == ' ' || *p == '\t') p++;
  if (*p == '\0' || strcmp(p, "FAULT") == 0) return false;

  // Parse pairs: temp0 load0 temp1 load1 ...
  float temps[MAX_GPUS] = {0};
  float loads[MAX_GPUS] = {0};
  int numGpus = 0;

  char buf[MAX_MSG_LEN];
  strncpy(buf, msg, sizeof(buf));
  buf[sizeof(buf) - 1] = '\0';

  char* token = strtok(buf, " \t");
  while (token != NULL) {
    if (numGpus >= MAX_GPUS) return false;
    float temp;
    float load;
    if (!parseValue(token, 150.0f, temp)) return false;
    token = strtok(NULL, " \t");
    if (!parseValue(token, 100.0f, load)) return false;

    temps[numGpus] = temp;
    loads[numGpus] = load;
    numGpus++;

    token = strtok(NULL, " \t");
  }

  if (numGpus == 0) return false;

  // Find hottest GPU for fan control
  float maxTemp = 0.0;
  int hotIdx = 0;
  for (int i = 0; i < numGpus; i++) {
    if (temps[i] > maxTemp) {
      maxTemp = temps[i];
      hotIdx = i;
    }
  }

  // Fan PWM with hysteresis
  int fanPwm;
  if (maxTemp >= FAN_FULL_TEMP) {
    fanPwm = 255;
  } else if (maxTemp <= FAN_START_TEMP - HYSTERESIS) {
    fanPwm = 0;
  } else if (maxTemp >= FAN_START_TEMP) {
    // Ramp from minimum stable duty to 255 between START and FULL temp.
    int range = (int)(FAN_FULL_TEMP - FAN_START_TEMP);
    if (range <= 0) { fanPwm = 255; }
    else { fanPwm = map((int)(maxTemp - FAN_START_TEMP), 0, range, FAN_MIN_PWM, 255); }
  } else {
    // Between FULL-HYST and START: hold last PWM
    fanPwm = lastFanPwm;
  }

  // Clamp and apply
  if (fanPwm < 0) fanPwm = 0;
  if (fanPwm > 255) fanPwm = 255;
  if (fanPwm > 0 && fanPwm < FAN_MIN_PWM) fanPwm = FAN_MIN_PWM;

  applyFanTarget(fanPwm);
  lastFanTemp = maxTemp;
  lastFanPwm = fanPwm;

  // === DISPLAY ===
  if (lcdReady) {
    char line[21];
    int dataLines = min(numGpus, LCD_ROWS - 1);
    for (int i = 0; i < dataLines; i++) {
      snprintf(line, sizeof(line), "GPU%d %2dC %3d%%", i, (int)temps[i], (int)loads[i]);
      lcdPrintPadded(0, i, line);
    }

    for (int lineNo = dataLines; lineNo < LCD_ROWS - 1; lineNo++) {
      lcdPrintPadded(0, lineNo, "");
    }

    int fanPct = map(fanPwm, 0, 255, 0, 100);
    if (fanKicking) {
      snprintf(line, sizeof(line), "Fan kick RPM %4u", fanRpm);
    } else {
      snprintf(line, sizeof(line), "Fan %3d%% RPM %4u", fanPct, fanRpm);
    }
    lcdPrintPadded(0, LCD_ROWS - 1, line);
  }
  return true;
}

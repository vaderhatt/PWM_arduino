#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define LCD_COLS    20
#define LCD_ROWS    4

// Fan PWM pin: Nano D9 is Timer1 OC1A. 4-wire PC fans expect ~25 kHz PWM.
const int fanPwmPin = 9;
const int fanTachPin = 2;  // INT0 on Nano
// Calibrated against the ARCTIC P12 Pro PST PWM/RPM curve in this build.
const unsigned int TACH_PULSES_PER_REV = 4;
const unsigned int FAN_PWM_TOP = 639;  // 16 MHz / (1 * (639 + 1)) = 25 kHz
const unsigned long TACH_MIN_PULSE_US = 8000;  // ARCTIC P12 Pro PST max is ~3000 RPM
const unsigned long TACH_TIMEOUT_US = 2000000UL;
const unsigned int FAN_MAX_VALID_RPM = 3500;

// Fan curve with hysteresis to prevent oscillation
const float FAN_START_TEMP = 50.0;
const float FAN_FULL_TEMP  = 85.0;
const float HYSTERESIS     = 3.0;
const int FAN_MIN_PWM      = 64;   // ~25%, ARCTIC curve is already ~1000 RPM here
const int FAN_KICK_PWM     = 180;  // short spin-up pulse when starting from 0
const unsigned int FAN_KICK_MS = 700;

// Serial protocol: space-separated float pairs "temp0 load0 temp1 load1\n"
#define MAX_GPUS       10
#define MAX_MSG_LEN   256

LiquidCrystal_I2C* lcd = NULL;
static uint8_t lcdAddr = 0;
static bool lcdReady = false;

// Ring buffer for serial data (avoids String heap fragmentation)
static char rxBuf[MAX_MSG_LEN];
static uint8_t rxIdx = 0;

// Fan state for hysteresis
static float lastFanTemp = 0.0;
static int lastFanPwm    = 0;
static unsigned long lastMessageMs = 0;
static bool waitingShown = false;
static volatile unsigned long tachPulses = 0;
static volatile unsigned long lastTachPulseUs = 0;
static volatile unsigned long tachIntervalSumUs = 0;
static volatile unsigned int tachIntervalCount = 0;
static unsigned long lastRpmSampleMs = 0;
static unsigned long lastTachPulses = 0;
static unsigned int fanRpm = 0;
static int currentFanPwm = 0;
static int targetFanPwm = 0;
static bool fanKicking = false;
static unsigned long fanKickUntilMs = 0;

void tachPulse() {
  unsigned long now = micros();
  unsigned long interval = now - lastTachPulseUs;

  if (lastTachPulseUs != 0) {
    if (interval < TACH_MIN_PULSE_US) {
      return;
    }
    tachIntervalSumUs += interval;
    if (tachIntervalCount < 1000) {
      tachIntervalCount++;
    }
  }

  lastTachPulseUs = now;
  tachPulses++;
}

void setupFanPwm25k() {
  pinMode(fanPwmPin, OUTPUT);
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;

  // Fast PWM, TOP=ICR1, non-inverting output on OC1A/D9, prescaler=1.
  ICR1 = FAN_PWM_TOP;
  OCR1A = 0;
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

void lcdPrintPadded(uint8_t col, uint8_t row, const char* text) {
  if (!lcdReady) return;
  lcd->setCursor(col, row);
  uint8_t printed = 0;
  while (*text && printed < LCD_COLS) {
    lcd->print(*text++);
    printed++;
  }
  while (printed < LCD_COLS) {
    lcd->print(' ');
    printed++;
  }
}

void setup() {
  // Baud rate must match Python script.
  Serial.begin(9600);

  setupFanPwm25k();
  pinMode(fanTachPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(fanTachPin), tachPulse, FALLING);

  Wire.begin();
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
    lcdPrintPadded(0, 3, "Waiting serial");
    waitingShown = true;
  }

  Serial.print("GPU Monitor OK LCD=0x");
  Serial.println(lcdAddr, HEX);
}

void loop() {
  updateFanRpm();
  updateFanKick();

  // Collect serial data into ring buffer
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      // End of message — process it
      if (rxIdx > 0) {
        rxBuf[rxIdx] = '\0';
        processMessage(rxBuf);
        lastMessageMs = millis();
        waitingShown = false;
        rxIdx = 0;
      }
    } else if (rxIdx < MAX_MSG_LEN - 1) {
      rxBuf[rxIdx++] = c;
    }
    // else: drop overflow
  }

  if (lcdReady && !waitingShown && millis() - lastMessageMs > 3000) {
    lcdPrintPadded(0, 0, "Waiting serial");
    char line[21];
    snprintf(line, sizeof(line), "LCD I2C: 0x%02X", lcdAddr);
    lcdPrintPadded(0, 1, line);
    snprintf(line, sizeof(line), "RPM %4u", fanRpm);
    lcdPrintPadded(0, 2, line);
    lcdPrintPadded(0, 3, "No GPU data yet");
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
  unsigned long now = millis();
  unsigned long elapsed = now - lastRpmSampleMs;
  if (elapsed < 500) return;

  noInterrupts();
  unsigned long intervalSum = tachIntervalSumUs;
  unsigned int intervalCount = tachIntervalCount;
  unsigned long lastPulseUs = lastTachPulseUs;
  tachIntervalSumUs = 0;
  tachIntervalCount = 0;
  interrupts();

  lastRpmSampleMs = now;

  if (currentFanPwm == 0 || TACH_PULSES_PER_REV == 0) {
    fanRpm = 0;
    return;
  }

  if (lastPulseUs == 0 || (unsigned long)(micros() - lastPulseUs) > TACH_TIMEOUT_US) {
    fanRpm = 0;
    return;
  }

  if (intervalCount == 0) {
    return;
  }

  unsigned long avgPulseUs = intervalSum / intervalCount;
  if (avgPulseUs == 0) return;

  unsigned long rpm = 60000000UL / (avgPulseUs * TACH_PULSES_PER_REV);
  if (rpm > FAN_MAX_VALID_RPM) {
    return;
  }

  unsigned int measured = (unsigned int)rpm;
  fanRpm = fanRpm == 0 ? measured : (unsigned int)((fanRpm * 3UL + measured) / 4UL);
}

void processMessage(const char* msg) {
  // Skip empty messages
  const char* p = msg;
  while (*p == ' ' || *p == '\t') p++;
  if (*p == '\0') return;

  // Parse pairs: temp0 load0 temp1 load1 ...
  float temps[MAX_GPUS] = {0};
  float loads[MAX_GPUS] = {0};
  int numGpus = 0;

  char buf[MAX_MSG_LEN];
  strncpy(buf, msg, sizeof(buf));
  buf[sizeof(buf) - 1] = '\0';

  char* token = strtok(buf, " \t");
  while (token != NULL && numGpus < MAX_GPUS) {
    float temp = atof(token);
    token = strtok(NULL, " \t");
    if (token == NULL) break;
    float load = atof(token);

    temps[numGpus] = temp;
    loads[numGpus] = load;
    numGpus++;

    token = strtok(NULL, " \t");
  }

  if (numGpus == 0) return;

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
}

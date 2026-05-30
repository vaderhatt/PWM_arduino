#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// LCD I2C address — scan with i2cdetect -y 1 if different
#define LCD_ADDR    0x27
#define LCD_COLS    20
#define LCD_ROWS    4

// Fan PWM pin (Arduino Nano pin 9 = Timer2 PWM)
const int fanPwmPin = 9;

// Fan curve with hysteresis to prevent oscillation
const float FAN_START_TEMP = 50.0;
const float FAN_FULL_TEMP  = 85.0;
const float HYSTERESIS     = 3.0;

// Serial protocol: space-separated float pairs "temp0 load0 temp1 load1\n"
#define MAX_GPUS       10
#define MAX_MSG_LEN   256

LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// Ring buffer for serial data (avoids String heap fragmentation)
static char rxBuf[MAX_MSG_LEN];
static uint8_t rxIdx = 0;

// Fan state for hysteresis
static float lastFanTemp = 0.0;
static int lastFanPwm    = 0;

void setup() {
  // Init I2C LCD with timeout detection
  Wire.begin();
  lcd.init();
  
  // Verify LCD is responsive — if init hangs, we're stuck anyway
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("GPU Monitor OK");
  delay(1000);
  lcd.clear();

  pinMode(fanPwmPin, OUTPUT);
  analogWrite(fanPwmPin, 0);
  
  // Baud rate must match Python script (9600)
  Serial.begin(9600);
}

void loop() {
  // Collect serial data into ring buffer
  while (Serial.available()) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') {
      // End of message — process it
      if (rxIdx > 0) {
        rxBuf[rxIdx] = '\0';
        processMessage(rxBuf);
        rxIdx = 0;
      }
    } else if (rxIdx < MAX_MSG_LEN - 1) {
      rxBuf[rxIdx++] = c;
    }
    // else: drop overflow
  }
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
    // Ramp from 0 to 255 between START and FULL temp
    int range = (int)(FAN_FULL_TEMP - FAN_START_TEMP);
    if (range <= 0) { fanPwm = 255; }
    else { fanPwm = map((int)(maxTemp - FAN_START_TEMP), 0, range, 0, 255); }
  } else {
    // Between FULL-HYST and START: hold last PWM
    fanPwm = lastFanPwm;
  }

  // Clamp and apply
  if (fanPwm < 0) fanPwm = 0;
  if (fanPwm > 255) fanPwm = 255;
  
  analogWrite(fanPwmPin, fanPwm);
  lastFanTemp = maxTemp;
  lastFanPwm = fanPwm;

  // === DISPLAY ===
  lcd.clear();

  // Show up to 3 GPUs per line, wrap to multiple lines
  int gpusPerLine = 2;
  int linesNeeded = (numGpus + gpusPerLine - 1) / gpusPerLine;
  
  for (int line = 0; line < LCD_ROWS && line < linesNeeded; line++) {
    lcd.setCursor(0, line);
    
    int start = line * gpusPerLine;
    int end = min(start + gpusPerLine, numGpus);
    
    for (int i = start; i < end; i++) {
      lcd.print("GPU");
      lcd.print(i);
      lcd.print(":");
      
      // Temperature: 2 digits + C
      if (temps[i] > 99.0) {
        lcd.print("99+");
      } else if (temps[i] >= 10.0) {
        lcd.print((int)temps[i]);
        lcd.print('C');
      } else {
        lcd.print(temps[i], 0);
        lcd.print('C');
      }
      
      // Space for load %
      lcd.print(" ");
    }
    
    // Pad remaining space
    for (int i = end; i < gpusPerLine; i++) {
      lcd.print("       ");
    }
  }

  // Last line: fan info + hottest GPU indicator
  if (linesNeeded < LCD_ROWS) {
    lcd.setCursor(0, linesNeeded);
    lcd.print("Fan:");
    lcd.print(fanPwm);
    lcd.print("% T:");
    if (maxTemp >= 10.0) {
      lcd.print((int)maxTemp);
    } else {
      lcd.print(maxTemp, 1);
    }
    lcd.print("C");
  }
}

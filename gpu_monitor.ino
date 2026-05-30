#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// LCD I2C address — 0x27 for most 20x4 modules
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Ocyplus Delta EH10 fan hub PWM control (pin 9 on Nano)
const int fanPwmPin = 9;

void setup() {
  lcd.init();
  lcd.backlight();
  pinMode(fanPwmPin, OUTPUT);
  analogWrite(fanPwmPin, 0); // Start fans off
  Serial.begin(9600);
}

void loop() {
  if (Serial.available()) {
    String data = Serial.readStringUntil('\n');
    data.trim();
    
    if (data.length() > 0) {
      int numValues = 0;
      float values[20]; // Max 10 GPUs (temp + load each)
      
      char buf[data.length() + 1];
      data.toCharArray(buf, sizeof(buf));
      char* token = strtok(buf, " ");
      while (token != NULL && numValues < 20) {
        values[numValues++] = atof(token);
        token = strtok(NULL, " ");
      }
      
      // Parse pairs: [temp0, load0, temp1, load1, ...]
      int numGpus = numValues / 2;
      if (numGpus > 10) numGpus = 10;
      
      float temps[10] = {0};
      float loads[10] = {0};
      for (int i = 0; i < numGpus; i++) {
        temps[i] = values[i * 2];
        loads[i] = values[i * 2 + 1];
      }
      
      // Fan curve: find hottest GPU, compute PWM
      float maxTemp = 0;
      for (int i = 0; i < numGpus; i++) {
        if (temps[i] > maxTemp) maxTemp = temps[i];
      }
      
      int fanPwm = 0;
      if (maxTemp >= 85.0) {
        fanPwm = 255;
      } else if (maxTemp >= 50.0) {
        fanPwm = map((int)(maxTemp - 50.0), 0, 35, 0, 255);
      }
      analogWrite(fanPwmPin, fanPwm);
      
      // === DISPLAY ===
      lcd.clear();
      
      for (int i = 0; i < numGpus && i < 3; i++) {
        lcd.setCursor(0, i);
        lcd.print("GPU");
        lcd.print(i);
        lcd.print(":");
        lcd.print(temps[i], 1);
        lcd.print("C ");
        lcd.print((int)loads[i]);
        lcd.print("%  ");
      }
      
      // Line 3: fan info
      lcd.setCursor(0, 3);
      lcd.print("Fan:");
      lcd.print(fanPwm);
      lcd.print("% T:");
      lcd.print(maxTemp, 1);
      lcd.print("C ");
    }
  }
}

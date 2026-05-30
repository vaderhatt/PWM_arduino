# PWM Arduino - GPU Monitor

Arduino-based GPU temperature monitor with LCD display and fan PWM control.

## Hardware
- Arduino Nano (CH340 clone)
- I2C LCD 20x4 (address 0x27)
- Ocyplus Delta EH10 fan hub (PWM pin 9)

## Setup on Gentoo
```bash
# Add overlay
git clone https://github.com/vaderhatt/PWM_arduino.git /var/db/repos/gentoo/pwm_arduino
emerge --sync pwm_arduino
emerge app-misc/pwm_arduino

# Start service
rc-update add gpu-monitor default
rc-service gpu-monitor start
```

## Manual Install
1. Flash `gpu_monitor.ino` via arduino-cli or Arduino IDE
2. Run `/opt/pwm_arduino/gpu_to_arduino.py` (needs pyserial)

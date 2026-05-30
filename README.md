# PWM Arduino - GPU Monitor

Arduino-based GPU temperature monitor with I2C LCD display and PWM fan control.
Reads NVIDIA GPU stats via `nvidia-smi` on a host server and displays them on an Arduino LCD while controlling fan speed.

## Hardware

| Component | Model | Notes |
|-----------|-------|-------|
| MCU | Arduino Nano (CH340) | ATmega328P, USB-to-serial CH340G |
| Display | I2C LCD 20x4 | Address 0x27 (most modules) |
| Fan Hub | Ocyplus Delta EH10 | PWM controlled via pin 9 |
| Connection | USB serial | /dev/ttyUSB1 on server |

## Wiring

### Arduino Nano → I2C LCD 20x4
```
Arduino Nano      I2C LCD 20x4
─────────────     ──────────────
A4 (SDA)    ────> SDA
A5 (SCL)    ────> SCL
5V          ────> VCC
GND         ────> GND
```

### Arduino Nano → Fan Hub PWM
```
Arduino Nano      Fan Hub
─────────────     ─────────────
D9 (PWM)      ────> PWM Input
5V            ────> VCC
GND           ────> GND
```

## Protocol

Python script sends space-separated float pairs per update:

```
temp0 load0 temp1 load1 temp2 load2 ...\n
```

Example: `50 0 75 45\n` → GPU0 at 50°C/0%, GPU1 at 75°C/45%

Update interval: 1 second. Baud rate: **9600**.

## Setup on Gentoo

```bash
# Add as local overlay
git clone https://github.com/vaderhatt/PWM_arduino.git /var/db/repos/gentoo/pwm_arduino

# Sync portage to pick up the new overlay
emerge --sync

# Install package
emerge app-misc/pwm_arduino
```

## Manual Install

```bash
# Clone repo
git clone https://github.com/vaderhatt/PWM_arduino.git /opt/pwm_arduino
cd /opt/pwm_arduino

# Install Python dependencies
pip3 install pyserial

# Flash Arduino sketch
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328 gpu_monitor.ino
avrdude -c arduino -p m328p -P /dev/ttyUSB1 -b 9600 -U flash:w:gpu_monitor.ino.hex:i

# Or use Arduino IDE: File > Upload

# Copy init script if not installed via ebuild
sudo cp gpu-monitor.init /etc/init.d/gpu-monitor
sudo chmod +x /etc/init.d/gpu-monitor

# Enable and start
sudo rc-update add gpu-monitor default
sudo rc-service gpu-monitor start

# Check status
sudo rc-service gpu-monitor status

# Add user to dialout group for serial access
sudo gpasswd -a $USER dialout
# Log out/in for changes to take effect
```

## Troubleshooting

- **LCD not showing**: Run `i2cdetect -y 1` (or `-y 0` on older kernels) and verify address. Adjust `LCD_ADDR` in `gpu_monitor.ino` if different from `0x27`. Verify wiring: SDA→A4, SCL→A5.
- **No serial data**: Check device exists (`ls -la /dev/ttyUSB1`) and permissions (`groups \| grep dialout`). Add to dialout group if missing.
- **Python can't connect**: Ensure pyserial is installed (`pip3 install pyserial`). Verify Python path in init script matches system Python.
- **Arduino not responding**: Verify Arduino is flashed and connected before starting service. Check serial connection: `cat /dev/ttyUSB1` (should see hex output). Verify baud rate matches in both Python script and Arduino sketch (9600). Check Arduino firmware is running: `dmesg \| grep ttyUSB`.

## Parameters

### Python Bridge
| Parameter | Default | Description |
|-----------|---------|-------------|
| Serial port | auto-detect | CH340 VID:PID lookup, fallback ttyUSB/ttyACM |
| Baud rate | 9600 | Serial communication speed |
| Update interval | 1 second | LCD refresh rate |
| GPU indices | all | All GPUs from nvidia-smi |

### Arduino Sketch
| Parameter | Default | Description |
|-----------|---------|-------------|
| LCD I2C addr | 0x27 | I2C address of LCD module |
| Fan PWM pin | 9 | PWM output for fan control |
| Fan start temp | 50°C | Fan ramp start temperature |
| Fan full speed | 85°C | Full PWM at this temp |
| Hysteresis | 3°C | Prevents fan oscillation |

## Directory Structure

```
pwm_arduino/
├── gpu_monitor.ino      # Arduino sketch (flashed to MCU)
├── gpu_to_arduino.py    # Python bridge (runs on server)
├── gpu-monitor.init     # OpenRC init script
├── pwm_arduino-0.1.ebuild  # Gentoo ebuild
└── README.md            # This file
```

## Dependencies

- NVIDIA driver + nvidia-smi
- Python 3.8+ with pyserial
- OpenRC (Gentoo) or systemd
- Arduino IDE or arduino-cli
- LiquidCrystal_I2C library (by Frank de Brabander)
- Wire.h (built-in I2C library)

## License

GPL-3

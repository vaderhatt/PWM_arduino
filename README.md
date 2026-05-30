# PWM Arduino - GPU Temperature Monitor

Arduino-based GPU temperature monitor with I2C LCD display and PWM fan control. Reads NVIDIA GPU stats via `nvidia-smi` on a host server and displays them on an Arduino LCD while controlling fan speed.

## Hardware Components

| Component | Model | Notes |
|-----------|-------|-------|
| MCU | Arduino Nano (CH340) | ATmega328P, USB-to-serial CH340G |
| Display | I2C LCD 20x4 | Address 0x27 (most modules) |
| Fan Hub | Ocyplus Delta EH10 | PWM controlled via pin 9 |
| Connection | USB serial | `/dev/ttyUSB1` on server |

## Wiring Diagram

```
Arduino Nano          I2C LCD 20x4
─────────────         ──────────────
A4 (SDA)  ───────────> SDA
A5 (SCL)  ───────────> SCL
5V        ───────────> VCC
GND       ───────────> GND

Arduino Nano          Fan Hub PWM
─────────────         ─────────────
D9 (PWM)  ───────────> PWM Input
5V        ───────────> VCC
GND       ───────────> GND
```

## Communication Protocol

Python script sends 6 bytes per update, Arduino displays on LCD:

| Byte | Content | Example |
|------|---------|---------|
| 1 | GPU0 temp (ASCII) | `50` = '2' |
| 2 | GPU0 temp (ASCII) | `37` = '7' → "27°C" |
| 3 | GPU1 temp (ASCII) | `55` = '5' |
| 4 | GPU1 temp (ASCII) | `18` = ' ' |
| 5 | Fan speed (ASCII) | `48` = 'H' |
| 6 | Fan speed (ASCII) | `39` = '9' → "H9" |

Update interval: 1 second. Baud rate: 9600.

## Server Setup (Gentoo)

### Option 1: Emerge (recommended)

```bash
# Add as local overlay
git clone https://github.com/vaderhatt/PWM_arduino.git /var/db/repos/gentoo/pwm_arduino

# Sync portage to pick up the new overlay
emerge --sync

# Install package
emerge app-misc/pwm_arduino
```

### Option 2: Manual Install

```bash
# Clone repo
git clone https://github.com/vaderhatt/PWM_arduino.git /opt/pwm_arduino
cd /opt/pwm_arduino

# Install Python dependencies
pip3 install pyserial

# Flash Arduino sketch
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328 gpu_monitor.ino
avrdude -c arduino -p m328p -P /dev/ttyUSB1 -b 57600 -U flash:w:gpu_monitor.ino.hex:i

# Or use Arduino IDE: File > Upload
```

### Service Management (OpenRC)

```bash
# Copy init script if not installed via ebuild
sudo cp gpu-monitor.init /etc/init.d/gpu-monitor
sudo chmod +x /etc/init.d/gpu-monitor

# Enable and start
sudo rc-update add gpu-monitor default
sudo rc-service gpu-monitor start

# Check status
sudo rc-service gpu-monitor status
```

### User Permissions

```bash
# Add user to dialout group for serial access
sudo gpasswd -a $USER dialout
# Log out/in for changes to take effect
```

## Configuration

### Python Bridge (`gpu_to_arduino.py`)

| Parameter | Default | Description |
|-----------|---------|-------------|
| Serial port | `/dev/ttyUSB1` | Arduino USB device |
| Baud rate | `9600` | Serial communication speed |
| Update interval | `1` second | LCD refresh rate |
| GPU indices | `[0, 1]` | Which GPUs to monitor (nvidia-smi) |

### Arduino Sketch (`gpu_monitor.ino`)

| Parameter | Default | Description |
|-----------|---------|-------------|
| LCD I2C addr | `0x27` | I2C address of LCD module |
| Fan PWM pin | `9` | PWM output for fan control |
| Fan min speed | `100` | Minimum PWM value (0-255) |
| Fan max speed | `255` | Maximum PWM value |
| Temp threshold | `60°C` | Fan speed ramp start temp |

## Troubleshooting

### LCD shows garbage characters
- Check I2C address: run `i2cdetect -y 1` (or `-y 0` on older kernels)
- Adjust address in `gpu_monitor.ino` if different from 0x27
- Verify wiring: SDA→A4, SCL→A5

### Python can't open serial port
```bash
# Check device exists
ls -la /dev/ttyUSB1

# Check permissions
groups | grep dialout

# Add to dialout group if missing
sudo gpasswd -a $USER dialout
```

### Service crashes on start
- Ensure pyserial is installed: `pip3 install pyserial`
- Check Python path in init script matches system Python
- Verify Arduino is flashed and connected before starting service

### LCD doesn't update
- Check serial connection: `cat /dev/ttyUSB1` (should see hex output)
- Verify baud rate matches in both Python script and Arduino sketch (9600)
- Check Arduino firmware is running: `dmesg | grep ttyUSB`

## File Structure

```
pwm_arduino/
├── gpu_monitor.ino          # Arduino sketch (flashed to MCU)
├── gpu_to_arduino.py        # Python bridge (runs on server)
├── gpu-monitor.init         # OpenRC init script
├── pwm_arduino-0.1.ebuild   # Gentoo ebuild
└── README.md                # This file
```

## Dependencies

### Server-side
- NVIDIA driver + nvidia-smi
- Python 3.8+ with pyserial
- OpenRC (Gentoo) or systemd

### Arduino-side
- Arduino IDE or arduino-cli
- LiquidCrystal_I2C library (by Frank de Brabander)
- Wire.h (built-in I2C library)

## License

GPL-3

# PWM Arduino - GPU Temperature Monitor

Arduino-based GPU temperature monitor with I2C LCD display, 25 kHz 4-wire fan PWM control, and tachometer RPM display. Reads NVIDIA GPU stats via `nvidia-smi` on a host server and displays them on an Arduino LCD while controlling fan speed.

## Hardware Components

| Component | Model | Notes |
|-----------|-------|-------|
| MCU | Arduino Nano (CH340) | ATmega328P, USB-to-serial CH340G |
| Display | I2C LCD 20x4 | Address auto-detected (0x20-0x27, 0x3E, 0x3F) |
| Fan | ARCTIC P12 Pro PST | PWM on D9, tach on D2 |
| Connection | USB serial | Auto-detected CH340 `/dev/ttyUSB*` |

## Wiring Diagram

```
Arduino Nano          I2C LCD 20x4
─────────────         ──────────────
A4 (SDA)  ───────────> SDA
A5 (SCL)  ───────────> SCL
5V        ───────────> VCC
GND       ───────────> GND

Arduino Nano          4-wire fan
─────────────         ──────────
D9 (PWM)  ───────────> PWM (blue)
D2        ───────────> TACH (green)
GND       ───────────> GND (black)
External 12V ────────> +12V (yellow)
```

## Communication Protocol

Python script sends space-separated temp/load pairs:

```text
temp0 load0 temp1 load1 temp2 load2\n
```

Example:

```text
49 0 55 3 47 0
```

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
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old .
arduino-cli upload -p /dev/ttyUSB2 --fqbn arduino:avr:nano:cpu=atmega328old .

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
| Serial port | auto-detected | Prefer CH340 Arduino `/dev/ttyUSB*` |
| Baud rate | `9600` | Serial communication speed |
| Update interval | `1` second | LCD refresh rate |
| GPU indices | all GPUs | Uses `nvidia-smi --query-gpu=temperature.gpu,utilization.gpu` |

### Arduino Sketch (`gpu_monitor.ino`)

| Parameter | Default | Description |
|-----------|---------|-------------|
| LCD I2C addr | auto-detected | Common PCF8574 addresses |
| Fan PWM pin | `D9` | Timer1 OC1A, 25 kHz PWM |
| Fan tach pin | `D2` | INT0, `INPUT_PULLUP` |
| Fan min PWM | `64` | ~25% duty, calibrated for ARCTIC P12 Pro PST |
| Fan kick | `180` for 700 ms | Spin-up pulse when starting from 0 |
| Temp ramp | `50°C` to `85°C` | Fan speed ramp range |
| Tach calibration | `4 pulses/rev` | Calibrated against ARCTIC P12 Pro PST PWM/RPM curve |

## Troubleshooting

### LCD shows garbage characters
- Check I2C address: run `i2cdetect -y 1` (or `-y 0` on older kernels)
- Firmware auto-detects common LCD backpack addresses at boot
- Verify wiring: SDA→A4, SCL→A5

### Python can't open serial port
```bash
# Check device exists
ls -la /dev/ttyUSB* /dev/ttyACM*

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
- Check serial connection: `lsof /dev/ttyUSB2`
- Verify baud rate matches in both Python script and Arduino sketch (9600)
- Check Arduino firmware is running: `dmesg | grep ttyUSB`

### RPM is wrong or zero
- ARCTIC P12 Pro PST should be roughly 600-3000 RPM.
- At 40-45% PWM expect roughly 1400-1600 RPM.
- Ensure fan GND and Arduino GND are common.
- If RPM is 0 while spinning, add a 10k pull-up from D2 to 5V.
- If RPM is wildly high, tach is noisy or the pulses/rev calibration needs adjustment.

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

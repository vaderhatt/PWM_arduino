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

For diagnostics, `STATUS\n` returns `STATUS PWM=... RPM=... TACH_US=... PPR=... VALID=...`.
PWM is 0–255; TACH_US is the mean accepted tach pulse interval. STATUS requests
do not refresh the GPU-data timeout or change the commanded speed. Stop the bridge
before opening a separate serial monitor; opening the port may reset the Nano.

On a failed, empty, or partially unreadable GPU query, the bridge sends `FAULT\n`.
It also reports a fault if the GPU count changes after the first successful query
in that bridge process. An intentional GPU configuration change requires restarting
the bridge. The initial GPU inventory still needs to be checked by the operator.

## Fan safety behavior

- At Arduino startup, PWM is 100% until a complete valid sample arrives.
- PWM reaches 100% at **75°C** on the hottest reported GPU; the normal ramp starts at 50°C.
- After **3 seconds without a valid sample**, PWM is forced to 100%, including with no LCD attached.
- `FAULT`, malformed/out-of-range/non-finite values, incomplete pairs, oversized frames,
  and more than 10 GPUs immediately force 100%. An invalid prefix is never accepted
  as a complete sample. Blank line delimiters do not refresh the timer.
- A fault cancels any pending spin-up kick. A valid sample resumes normal temperature control.
- With current Arduino AVR cores, a Wire/I2C timeout disables the LCD so a failed display
  does not indefinitely block fan supervision.

**Update both the Arduino sketch and the Python bridge.** The old bridge sends `0 0`
on a GPU query failure, which the new firmware cannot distinguish from a valid cold sample.
The old firmware does not understand the new `FAULT` message. Stop the bridge, flash
the new firmware, install the matching Python script, then restart the bridge.

This is a software timeout while the MCU is running, not a hardware watchdog.
Loss of Arduino power or an unrelated firmware hang must be verified on the actual
fan/controller wiring. 100% PWM commands maximum speed; it cannot guarantee that
a physically failed or unpowered fan rotates.

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
mkdir -p build-sketch/gpu_monitor
cp gpu_monitor.ino build-sketch/gpu_monitor/gpu_monitor.ino
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old build-sketch/gpu_monitor
arduino-cli upload -p /dev/ttyUSB2 --fqbn arduino:avr:nano:cpu=atmega328old build-sketch/gpu_monitor

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
| Serial port | auto-detected | Exactly one CH340 with VID:PID `1a86:7523`; rediscovered on reconnect |
| `ARDUINO_PORT` environment variable | unset | Explicit port or stable `/dev/serial/by-id/` or `/dev/serial/by-path/` link |
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
| Temp ramp | `50°C` to `75°C` | Fan speed ramp range |
| Data timeout | `3000 ms` | 100% PWM without a valid GPU sample |
| Tach calibration | `2 pulses/rev` | Standard PC-fan setting; ~10 ms edges observed at full PWM are consistent with ~3000 RPM |
| Tach noise threshold | derived from `3500 RPM` and pulses/rev | About 8571 µs at 2 pulses/rev; accepts 3000 RPM without dropping alternate edges |

## Troubleshooting

### LCD shows garbage characters
- Check I2C address: run `i2cdetect -y 1` (or `-y 0` on older kernels)
- Firmware auto-detects common LCD backpack addresses at boot
- Verify wiring: SDA→A4, SCL→A5

### Python can't open serial port

The bridge waits and retries if the Arduino is absent. It never automatically
selects an unrelated serial adapter. With multiple CH340 adapters, or a different
Arduino USB chip, set `ARDUINO_PORT` explicitly. After USB reconnection, automatic
discovery handles a changed ttyUSB number.
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
- The previous fixed 8000 µs noise threshold rejected valid pulses above 1875 RPM
  with the configured 4 pulses/rev. The threshold is now derived from the supported
  maximum RPM. Validate pulses/rev independently; matching a published PWM/RPM
  curve alone does not prove the physical rotation rate.
- The old 4 pulses/rev setting displayed roughly 1500 RPM at full PWM with an
  observed ~10 ms tach interval. The new default is the standard 2 pulses/rev,
  consistent with this fan's published ~3000 RPM maximum. An independent optical
  tachometer is still required to verify absolute mechanical RPM.

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

## Validation

Host tests execute the actual sketch logic with simulated Arduino interfaces and
the actual bridge loop with fake serial/GPU responses:

```bash
python3 -m unittest discover -s tests -v
g++ -std=c++11 -Wall -Wextra -I tests/stubs tests/firmware_failsafe_test.cpp -o /tmp/firmware_failsafe_test
/tmp/firmware_failsafe_test
```

They cover startup, the 3-second boundary, recovery, 75°C, interrupted spin-up,
invalid and oversized input, clock wraparound, serial flooding, display timeout,
failed/partial GPU reads, and GPU disappearance. Compile with the Arduino CLI as
shown above as well. These checks do not replace a physical test of PWM voltage,
frequency, fan response, and tachometer calibration.

Additional tests cover USB VID/PID selection, ambiguous adapters, reconnection to
a renamed port, RPM from 600 to 3000 with noise, tach restart, and micros rollover.

## License

GPL-3

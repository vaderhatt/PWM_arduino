# Changelog

## [0.2.1] - 2026-05-31

### Fixed
- Fixed OpenRC service startup by using `command_background` and a pidfile instead of relying on ad-hoc `start-stop-daemon` argument construction.

## [0.2.0] - 2026-05-30

### Added
- 25 kHz PWM output on Arduino Nano D9 for 4-wire PC fans.
- Tachometer input on D2 with interrupt-based RPM calculation.
- ARCTIC P12 Pro PST RPM calibration and tach noise filtering.
- Fan spin-up kick and minimum stable PWM duty.
- LCD I2C address auto-detection.
- Serial boot/status message from the Arduino firmware.
- Robust Python serial bridge with CH340 auto-detection and reconnect loop.

### Changed
- Serial protocol now sends space-separated temperature/load pairs for all GPUs.
- Fan control now ramps from 50°C to 85°C with hysteresis.
- LCD display now shows GPU temperature/load and fan RPM.

## [0.1.0] - 2024-05-30

### Added
- Initial release
- Arduino sketch for I2C LCD 20x4 display
- Python bridge script reading nvidia-smi data
- OpenRC init script for Gentoo
- Gentoo ebuild (pwm_arduino-0.1.ebuild)
- Fan PWM control via Arduino pin 9
- Dual GPU monitoring (GPU0 + GPU1)
- Real-time temperature display on LCD

### Technical Details
- Communication: USB serial @ 9600 baud
- Protocol: 6-byte ASCII packets per update
- Update rate: 1 second interval
- Fan control: PWM ramp starting at 60°C threshold

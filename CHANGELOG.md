# Changelog

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

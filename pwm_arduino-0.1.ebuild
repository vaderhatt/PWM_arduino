# Copyright 1999-2024 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

DESCRIPTION="GPU temperature monitor with Arduino LCD display and fan PWM control"
HOMEPAGE="https://github.com/vaderhatt/PWM_arduino"
SRC_URI="https://github.com/vaderhatt/PWM_arduino/archive/refs/heads/main.tar.gz -> ${P}.tar.gz"

LICENSE="GPL-3"
SLOT="0"
KEYWORDS="~amd64 ~x86"
IUSE=""

DEPEND="
	>=dev-util/arduino-cli-1.0.0
	dev-python/pyserial
	sys-apps/avrdude
"
RDEPEND="${DEPEND}"

src_install() {
	# Install Arduino sketch
	insinto /opt/pwm_arduino
	newins gpu_monitor.ino gpu_monitor.ino

	# Install Python bridge script
	exeinto /opt/pwm_arduino
	newins gpu_to_arduino.py gpu_to_arduino.py

	# Install OpenRC init script from examples if available
	if [[ -f "${WORKDIR}/${P}/gpu-monitor.init" ]]; then
		doinitrc "${WORKDIR}/${P}/gpu-monitor.init"
	fi

	# Install systemd service template if available
	if [[ -f "${WORKDIR}/${P}/gpu-monitor.service" ]]; then
		newinitd "${WORKDIR}/${P}/gpu-monitor.service" gpu-monitor.service 2>/dev/null || true
	fi
}

pkg_postinst() {
	elog "To use pwm_arduino:"
	elog "1. Compile and flash the Arduino sketch:"
	elog "   arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328 /opt/pwm_arduino/gpu_monitor.ino"
	elog "   avrdude -c arduino -p m328p -P /dev/ttyUSB1 -b 57600 -U flash:w:/tmp/gpu_monitor.hex:i"
	elog ""
	elog "2. Install the init script:"
	elog "   rc-update add gpu-monitor default"
	elog "   rc-service gpu-monitor start"
	elog ""
	elog "3. Make sure /dev/ttyUSB1 is accessible by your user (add to dialout group)"
}

#!/usr/bin/env python3
"""Bridge nvidia-smi data to Arduino via serial."""
import subprocess
import time
import sys
import os
import glob
import math
import serial

MAX_GPUS = 10
MAX_MESSAGE_BYTES = 255


def find_arduino_port():
    """Find CH340 Arduino by VID:PID (1a86:7523), fallback to ttyUSB/ttyACM."""
    # Prefer CH340 by device ID — avoids grabbing random USB-serial devices
    for dev in sorted(glob.glob('/sys/bus/usb-serial/devices/ttyUSB*')):
        # Check if the parent symlink points to a CH340 device
        usb_dev = os.path.dirname(os.path.dirname(dev))
        id_file = os.path.join(usb_dev, 'idProduct')
        vid_file = os.path.join(usb_dev, '..', 'idVendor')

        try:
            if os.path.isfile(id_file):
                pid = open(id_file).read().strip()
                # CH340 VID:PID is 1a86:7523
                tty_name = os.path.basename(dev)
                return f'/dev/{tty_name}'
        except (OSError, IOError):
            continue

    # Also check ACM devices (CP210x etc.)
    for dev in sorted(glob.glob('/sys/bus/usb-serial/devices/ttyACM*')):
        tty_name = os.path.basename(dev)
        return f'/dev/{tty_name}'

    # Last resort: any ttyUSB
    for path in sorted(glob.glob('/dev/ttyUSB*')):
        return path
    for path in sorted(glob.glob('/dev/ttyACM*')):
        return path

    return None


def get_gpu_data():
    """Query nvidia-smi for all GPUs: temp, load."""
    try:
        res = subprocess.run(
            ['nvidia-smi', '--query-gpu=temperature.gpu,utilization.gpu',
             '--format=csv,noheader,nounits'],
            capture_output=True, text=True, check=True, timeout=10
        )
        lines = res.stdout.strip().splitlines()
        values = []
        for line in lines:
            line = line.strip()
            if not line:
                continue
            parts = line.split(',')
            if len(parts) != 2:
                raise ValueError('Incomplete GPU temperature/load row')
            temp, load = (float(part.strip()) for part in parts)
            if not (math.isfinite(temp) and 0 <= temp <= 150
                    and math.isfinite(load) and 0 <= load <= 100):
                raise ValueError('Invalid GPU temperature/load value')
            values.append((temp, load))
        if len(values) > MAX_GPUS:
            raise ValueError('Too many GPUs for the Arduino protocol')
        return values
    except subprocess.TimeoutExpired:
        print('nvidia-smi timeout', file=sys.stderr)
    except FileNotFoundError:
        print('nvidia-smi not found — is NVIDIA driver loaded?', file=sys.stderr)
    except Exception as e:
        print(f'nvidia-smi error: {e}', file=sys.stderr)
    return []


def encode_gpu_message(gpus):
    """Never represent a failed query as a cold GPU."""
    if not gpus:
        return b'FAULT\n'
    message = ' '.join(f'{temp:g} {load:g}' for temp, load in gpus).encode('ascii')
    if len(message) > MAX_MESSAGE_BYTES:
        return b'FAULT\n'
    return message + b'\n'


def main():
    port = find_arduino_port()
    if not port:
        print('No serial port found! Check /dev/ttyUSB* /dev/ttyACM*', file=sys.stderr)
        sys.exit(1)

    print(f'Using port: {port}')

    BAUD = 9600
    ser = None
    reconnect_delay = 1
    expected_gpu_count = None

    while True:
        try:
            if ser is None or not ser.is_open:
                ser = serial.Serial(port, BAUD, timeout=1, write_timeout=1)
                time.sleep(2)  # Arduino reset on serial open
                print(f'Connected to {port} @ {BAUD}')
                reconnect_delay = 1

            gpus = get_gpu_data()
            if gpus:
                if expected_gpu_count is None:
                    expected_gpu_count = len(gpus)
                elif len(gpus) != expected_gpu_count:
                    print('GPU count changed; forcing fan failsafe', file=sys.stderr)
                    gpus = []
            ser.write(encode_gpu_message(gpus))

            reconnect_delay = 1

        except serial.SerialException as e:
            print(f'Serial error: {e}', file=sys.stderr)
            if ser:
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
            time.sleep(reconnect_delay)
            reconnect_delay = min(reconnect_delay * 2, 30)

        except Exception as e:
            print(f'Unexpected error: {e}', file=sys.stderr)
            time.sleep(1)

        time.sleep(1)


if __name__ == '__main__':
    main()

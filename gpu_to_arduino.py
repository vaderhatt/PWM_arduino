#!/usr/bin/env python3
"""Bridge nvidia-smi data to Arduino via serial."""
import subprocess
import time
import sys
import os
import math
import serial
from serial.tools import list_ports

MAX_GPUS = 10
MAX_MESSAGE_BYTES = 255


def find_arduino_port():
    """Use an explicit port or a single CH340; never guess another USB device."""
    explicit = os.environ.get('ARDUINO_PORT', '').strip()
    if explicit:
        return explicit
    matches = [p.device for p in list_ports.comports()
               if (p.vid, p.pid) == (0x1A86, 0x7523)]
    if len(matches) > 1:
        raise serial.SerialException('Multiple CH340 devices; set ARDUINO_PORT to a stable /dev/serial/by-id or by-path link')
    return matches[0] if matches else None


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
    BAUD = 9600
    ser = None
    reconnect_delay = 1
    expected_gpu_count = None

    while True:
        try:
            if ser is None or not ser.is_open:
                port = find_arduino_port()  # USB numbering can change after unplugging.
                if not port:
                    raise serial.SerialException('No CH340 found; waiting for Arduino')
                ser = serial.Serial(port, BAUD, timeout=1, write_timeout=1, exclusive=True)
                time.sleep(2)  # Arduino reset on serial open
                print(f'Connected to {port} @ {BAUD}', flush=True)
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

#!/usr/bin/env python3
"""Bridge nvidia-smi data to Arduino via serial."""
import subprocess
import time
import serial
import glob

def find_arduino_port():
    """Find CH340 Arduino on /dev/ttyUSB* first (1a86:7523)."""
    # Prefer ttyUSB (CH340) over ttyACM
    for path in sorted(glob.glob('/dev/ttyUSB*')):
        return path
    # Fallback to ACM
    for path in sorted(glob.glob('/dev/ttyACM*')):
        return path
    return None

def get_gpu_data():
    """Query nvidia-smi for all GPUs: temp, load."""
    try:
        res = subprocess.run(
            ['nvidia-smi', '--query-gpu=temperature.gpu,utilization.gpu',
             '--format=csv,noheader,nounits'],
            capture_output=True, text=True, check=True
        )
        lines = res.stdout.strip().split('\n')
        values = []
        for line in lines:
            if ',' in line:
                temp, load = line.split(',')
                values.append((temp.strip(), load.strip()))
        return values
    except Exception as e:
        print(f'nvidia-smi error: {e}')
        return []

def main():
    port = find_arduino_port()
    if not port:
        print('No serial port found! Check /dev/ttyUSB*')
        return

    BAUD = 9600
    
    try:
        ser = serial.Serial(port, BAUD)
        time.sleep(2)
        print(f'Connected to {port} @ {BAUD}')
    except serial.SerialException as e:
        print(f'Serial error: {e}')
        return

    while True:
        try:
            gpus = get_gpu_data()
            if gpus:
                parts = []
                for temp, load in gpus:
                    parts.append(f'{temp} {load}')
                msg = ' '.join(parts) + '\n'
                ser.write(msg.encode())
                print(f'Sent: {msg.strip()}')
            else:
                ser.write(b'0 0\n')
        except serial.SerialException as e:
            print(f'Serial write error: {e}')
            try:
                ser.close()
                time.sleep(1)
                ser = serial.Serial(port, BAUD)
                time.sleep(2)
            except Exception:
                pass
        time.sleep(1)

if __name__ == '__main__':
    main()

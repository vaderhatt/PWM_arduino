import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import types
import unittest
from unittest.mock import patch


fake_serial = types.ModuleType('serial')
fake_serial.SerialException = type('SerialException', (Exception,), {})
spec = importlib.util.spec_from_file_location(
    'gpu_bridge', Path(__file__).resolve().parents[1] / 'gpu_to_arduino.py')
bridge = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {'serial': fake_serial}):
    spec.loader.exec_module(bridge)


class StopLoop(BaseException):
    pass


class BridgeTests(unittest.TestCase):
    def query(self, output):
        with patch.object(bridge.subprocess, 'run', return_value=types.SimpleNamespace(stdout=output)):
            with contextlib.redirect_stderr(io.StringIO()):
                return bridge.get_gpu_data()

    def test_valid_multiple_gpus(self):
        data = self.query('50, 20\n90, 100\n')
        self.assertEqual(bridge.encode_gpu_message(data), b'50 20 90 100\n')

    def test_failed_or_partial_query_never_sends_cold_temperature(self):
        for output in ['', '50, 20\nN/A, 100\n', '50, 20\nbroken\n',
                       'nan, 0\n', 'inf, 0\n', '-1, 0\n', '151, 0\n',
                       '50, 101\n', '50, -1\n', '50, 20, 30\n', '50, 20\n' * 11]:
            with self.subTest(output=output):
                self.assertEqual(bridge.encode_gpu_message(self.query(output)), b'FAULT\n')

    def test_query_timeout_and_missing_binary(self):
        for error in [subprocess.TimeoutExpired('nvidia-smi', 10), FileNotFoundError()]:
            with patch.object(bridge.subprocess, 'run', side_effect=error):
                with contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(bridge.encode_gpu_message(bridge.get_gpu_data()), b'FAULT\n')

    def test_message_length(self):
        self.assertEqual(bridge.encode_gpu_message([(1.234567, 2.345678)] * 30), b'FAULT\n')

    def test_actual_bridge_loop_fault_and_gpu_disappearance(self):
        class Port:
            is_open = True
            def __init__(self):
                self.messages = []
            def write(self, message):
                self.messages.append(message)
                if len(self.messages) == 4:
                    raise StopLoop()
        port = Port()
        samples = [[(50., 20.), (90., 100.)], [], [(50., 20.)], [(55., 30.), (80., 70.)]]
        with patch.object(bridge, 'find_arduino_port', return_value='/dev/fake'), \
             patch.object(bridge.serial, 'Serial', return_value=port, create=True) as serial_open, \
             patch.object(bridge, 'get_gpu_data', side_effect=samples), \
             patch.object(bridge.time, 'sleep'), \
             contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(StopLoop):
                bridge.main()
        self.assertEqual(port.messages, [b'50 20 90 100\n', b'FAULT\n', b'FAULT\n', b'55 30 80 70\n'])
        self.assertEqual(serial_open.call_args.kwargs['write_timeout'], 1)


if __name__ == '__main__':
    unittest.main()

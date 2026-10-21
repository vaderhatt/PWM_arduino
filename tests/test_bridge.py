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
fake_tools = types.ModuleType('serial.tools')
fake_tools.list_ports = types.SimpleNamespace(comports=lambda: [])
spec = importlib.util.spec_from_file_location(
    'gpu_bridge', Path(__file__).resolve().parents[1] / 'gpu_to_arduino.py')
bridge = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {'serial': fake_serial, 'serial.tools': fake_tools}):
    spec.loader.exec_module(bridge)


class StopLoop(BaseException):
    pass


class BridgeTests(unittest.TestCase):
    def test_port_identification(self):
        def port(name, vid, pid):
            return types.SimpleNamespace(device=name, vid=vid, pid=pid)
        ch340 = port('/dev/ttyUSB2', 0x1a86, 0x7523)
        other = port('/dev/ttyUSB0', 0x10c4, 0xea60)
        with patch.dict(bridge.os.environ, {}, clear=True):
            with patch.object(bridge.list_ports, 'comports', return_value=[other, ch340]):
                self.assertEqual(bridge.find_arduino_port(), '/dev/ttyUSB2')
            with patch.object(bridge.list_ports, 'comports', return_value=[other]):
                self.assertIsNone(bridge.find_arduino_port())
            with patch.object(bridge.list_ports, 'comports', return_value=[ch340, port('/dev/ttyUSB3', 0x1a86, 0x7523)]):
                with self.assertRaises(bridge.serial.SerialException):
                    bridge.find_arduino_port()
        with patch.dict(bridge.os.environ, {'ARDUINO_PORT': '/dev/serial/by-path/chosen'}):
            self.assertEqual(bridge.find_arduino_port(), '/dev/serial/by-path/chosen')

    def test_missing_port_then_usb_renumbering(self):
        class Port:
            is_open = True
            def __init__(self, fails): self.fails = fails
            def close(self): self.is_open = False
            def write(self, message):
                if self.fails: raise bridge.serial.SerialException('unplugged')
                raise StopLoop()
        with patch.object(bridge, 'find_arduino_port', side_effect=[None, '/dev/ttyUSB0', '/dev/ttyUSB2']), \
             patch.object(bridge.serial, 'Serial', side_effect=[Port(True), Port(False)], create=True) as serial_open, \
             patch.object(bridge, 'get_gpu_data', return_value=[(60., 0.)]), \
             patch.object(bridge.time, 'sleep'), \
             contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(StopLoop): bridge.main()
        self.assertEqual([c.args[0] for c in serial_open.call_args_list], ['/dev/ttyUSB0', '/dev/ttyUSB2'])
        self.assertTrue(serial_open.call_args.kwargs['exclusive'])

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

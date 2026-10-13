#!/usr/bin/env python3
"""Run native command tests; optionally compare against the upstream Python code.

Usage: python3 test/test_lotus_protocol.py [--reference /path/to/lotus-lamp-python]
Requires Python 3 and g++, without BLE hardware or Python BLE dependencies.
"""
import argparse
import ast
import asyncio
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
reference = None
HELPER = r'''
#include <iostream>
#include <iomanip>
#include <string>
#include "lotus_protocol.h"
int main() {
  std::string line;
  while (std::getline(std::cin, line)) {
    lotus::Command command;
    if (!lotus::parse(line.c_str(), command)) {std::cout << "invalid\n"; continue;}
    uint8_t bytes[9]; lotus::encode(command, bytes);
    for (auto byte : bytes) std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
    std::cout << '\n';
  }
}
'''


class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        source = Path(cls.temp.name) / 'protocol.cpp'
        source.write_text(HELPER)
        cls.binary = Path(cls.temp.name) / 'protocol'
        subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'src'), str(source), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def encode(self, commands):
        result = subprocess.run([str(self.binary)], input='\n'.join(commands) + '\n',
                                text=True, capture_output=True, check=True)
        return result.stdout.splitlines()

    def test_documented_wire_examples(self):
        # Independent examples from upstream docs/PROTOCOL.md and controller.py.
        examples = {'on': '7e07040100ffff00ef', 'off': '7e07040000ffff00ef',
                    'color red': '7e070503ff000010ef',
                    'color #123456': '7e07050312345610ef',
                    'color white': '7e070503ffffff10ef',
                    'color black': '7e07050300000010ef',
                    'brightness 0': '7e070100ffffff00ef',
                    'brightness 100': '7e070164ffffff00ef',
                    'speed 100': '7e040264ffffff00ef',
                    'effect 0': '7e070300ffffff00ef',
                    'effect 143': '7e07038fffffff00ef',
                    'effect 212': '7e0703d4ffffff00ef'}
        self.assertEqual(self.encode(examples), list(examples.values()))

    def test_invalid_commands_do_not_produce_packets(self):
        commands = ['on now', 'power toggle', 'brightness -1', 'brightness 101',
                    'brightness 1x', 'brightness +2', 'brightness ', 'speed 101',
                    'effect 213', 'effect 9999999999999', 'effect -1',
                    'color #ffffff extra', 'color #ggffff', 'color 12345',
                    'color 1234567', 'color unknown', 'pair 1', 'scan', 'forget']
        self.assertEqual(self.encode(commands), ['invalid'] * len(commands))

    def test_python_reference(self):
        if reference is None:
            self.skipTest('supply --reference to check upstream Python methods')
        tree = ast.parse((reference / 'lotus_lamp/controller.py').read_text())
        controller = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'LotusLamp')
        names = {'set_rgb', 'set_color', 'set_brightness', 'set_speed', 'set_animation', 'power_on', 'power_off'}
        controller.body = [n for n in controller.body if isinstance(n, ast.AsyncFunctionDef) and n.name in names]
        namespace = {}
        exec(compile(ast.Module(body=[controller], type_ignores=[]), '<upstream command methods>', 'exec'), namespace)
        lamp = namespace['LotusLamp']()
        expected, commands = [], []

        async def capture(command, delay=0.1):
            expected.append(command.hex())
        lamp._send_command = capture

        async def compare_cases():
            for command, method in [('on', lamp.power_on), ('off', lamp.power_off)]:
                commands.append(command)
                await method()
            for color in ['red', 'green', 'blue', 'white', 'yellow', 'cyan', 'magenta', 'orange', 'purple', 'pink']:
                commands.append('color ' + color)
                await lamp.set_color(color)
            generator = random.Random(18)
            for _ in range(100):
                rgb = [generator.randrange(256) for _ in range(3)]
                commands.append('color #' + ''.join(f'{n:02x}' for n in rgb))
                await lamp.set_rgb(*rgb)
            for value in range(101):
                commands.append(f'brightness {value}')
                await lamp.set_brightness(value)
                commands.append(f'speed {value}')
                await lamp.set_speed(value)
            # Mode 0 is tested against the documented Auto Play packet above:
            # upstream's implementation clamps 0 to 1 despite its mode table.
            for value in range(1, 213):
                commands.append(f'effect {value}')
                await lamp.set_animation(value)
        asyncio.run(compare_cases())
        self.assertEqual(self.encode(commands), expected)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--reference', type=Path)
    arguments, remaining = parser.parse_known_args()
    reference = arguments.reference
    unittest.main(argv=[__file__] + remaining)

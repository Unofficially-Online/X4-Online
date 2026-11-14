"""Discover dormant VIEW fixtures and verify their metadata against the real ELF.

The six C programs use synthetic engine state. The optional ELF check only
reads the supported game executable; neither path launches X4.
"""
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'src/native/engine/linux'
sys.path.insert(0, str(ROOT / 'tools/dev'))
from check_lockstep_gate_linux_sites import Elf, parse_sites

HEADERS = {
    'gate_player_stock_view_capture.h': ('capture-floating', 'capture-fov'),
    'gate_player_stock_view_ui_selection.h': (
        'ui-softtarget', 'ui-controlled-current', 'ui-controlled-player',
        'ui-parent-walk', 'ui-floating', 'ui-fov'),
    'gate_player_stock_view_control_proof.h': (
        'control-player-method', 'control-cockpit-class',
        'control-room-class', 'control-ship-class'),
}
PIN = re.compile(r'\{"([a-z-]+)",\s*(0x[0-9a-f]+),\s*(0x[0-9a-f]+|[0-9]+),\s*"([0-9a-f]{64})"\}')
SLOT = re.compile(r'\{(0x[0-9a-f]+)u\s*\+\s*(0x[0-9a-f]+)u,\s*(0x[0-9a-f]+)u\}')
CLASS_WORD = re.compile(
    r'read_local\(LX\((0x[0-9a-f]+)u\s*\+\s*([0-9]+)u\),\s*&([a-z_]+),\s*4\)\s*&&\s*\3\s*==\s*(0x[0-9a-f]+|[0-9]+)')


class StockViewCaptureTests(unittest.TestCase):
    def run_fixture(self, name, expected):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / name
            build = subprocess.run([os.environ.get('CC', 'cc'), '-UNDEBUG', '-std=gnu11',
                            '-Wall', '-Wextra', '-Werror', str(ENGINE / (name + '.c')),
                            '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(expected, result.stdout)

    def test_capture_core(self):
        self.run_fixture('test_player_stock_view_capture', 'stock VIEW capture staging and drift: PASS')

    def test_capture_native_adapter(self):
        self.run_fixture('test_gate_player_stock_view_capture', 'native VIEW capture adapter gate: PASS')

    def test_ui_selection_core(self):
        self.run_fixture('test_player_stock_view_ui_selection', 'local UI softtarget lease: PASS')

    def test_ui_selection_native_adapter(self):
        self.run_fixture('test_gate_player_stock_view_ui_selection', 'native guest UI VIEW capture lease: PASS')

    def test_control_proof_core(self):
        self.run_fixture('test_player_stock_view_control_proof', 'read-only UI control identity and registry: PASS')

    def test_control_proof_native_adapter(self):
        self.run_fixture('test_gate_player_stock_view_control_proof', 'native UI control proof adapter: PASS')

    def pin_metadata(self):
        metadata = {}
        for header, names in HEADERS.items():
            text = (ENGINE / header).read_text()
            rows = PIN.findall(text)
            self.assertEqual(tuple(row[0] for row in rows), names, header)
            metadata[header] = [(name, int(address, 0), int(length, 0), digest)
                                for name, address, length, digest in rows]
        return metadata

    def test_pin_metadata_inventory(self):
        self.pin_metadata()
        text = (ENGINE / 'gate_player_stock_view_control_proof.h').read_text()
        self.assertEqual(len(SLOT.findall(text)), 4)
        self.assertEqual(len(CLASS_WORD.findall(text)), 3)

    def test_optional_real_linux_elf_pins_and_hash_typo_regression(self):
        default = Path.home() / '.local/share/Steam/steamapps/common/X4 Foundations/X4'
        executable = Path(os.environ.get('X4_LINUX_EXE', default))
        if not executable.is_file():
            self.skipTest('pinned Linux X4 ELF unavailable; set X4_LINUX_EXE')
        data = executable.read_bytes()
        constants, _ = parse_sites()
        # A present but different image is a failure, not an unavailable proof.
        self.assertEqual(len(data), constants['LX_FILE_SIZE'])
        self.assertEqual(hashlib.sha256(data).hexdigest(), constants['LX_FILE_SHA'])
        image = Elf(data)
        self.assertEqual((image.type, image.machine), (2, 62))
        metadata = self.pin_metadata()
        for header, rows in metadata.items():
            for name, address, length, expected in rows:
                with self.subTest(header=header, pin=name):
                    self.assertGreater(length, 0)
                    self.assertEqual(hashlib.sha256(image.read(address, length)).hexdigest(), expected)
        text = (ENGINE / 'gate_player_stock_view_control_proof.h').read_text()
        for base, offset, expected in SLOT.findall(text):
            address = int(base, 0) + int(offset, 0)
            with self.subTest(slot=hex(address)):
                self.assertEqual(struct.unpack('<Q', image.read(address, 8))[0], int(expected, 0))
        for base, offset, name, expected in CLASS_WORD.findall(text):
            address = int(base, 0) + int(offset, 0)
            with self.subTest(class_word=name):
                self.assertEqual(struct.unpack('<I', image.read(address, 4))[0], int(expected, 0))
        # The earlier e-for-c typo had valid SHA syntax and passed the C mock.
        # The actual ELF must distinguish it from the corrected metadata.
        ship = next(row for row in metadata['gate_player_stock_view_control_proof.h']
                    if row[0] == 'control-ship-class')
        actual = hashlib.sha256(image.read(ship[1], ship[2])).hexdigest()
        old_typo = '6cc846478894fc1636f1416467e973ff24c09d48b3207c8f8b4fa37d6bfd6e42'
        self.assertEqual(len(old_typo), 64)
        self.assertNotEqual(actual, old_typo)
        self.assertEqual(actual, ship[3])


if __name__ == '__main__':
    unittest.main()

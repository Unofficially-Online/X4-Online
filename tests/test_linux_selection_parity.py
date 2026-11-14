"""Read-only selection comparison and native capture admission contracts."""
from pathlib import Path
import hashlib
import os
import re
import subprocess
import tempfile
import unittest
from check_lockstep_gate_linux_sites import Elf

ENGINE = Path(__file__).resolve().parents[1] / 'src/native/engine/linux'


class SelectionParityTests(unittest.TestCase):
    def test_linux_reference_metadata_pins(self):
        default_executable = Path.home() / '.local/share/Steam/steamapps/common/X4 Foundations/X4'
        executable = Path(os.environ.get('X4_LINUX_EXE', default_executable))
        if not executable.is_file():
            self.skipTest('pinned Linux X4 executable unavailable')
        raw = executable.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         '113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20')
        elf = Elf(raw)
        sites = re.findall(r'\{"([a-z-]+)",\s*(0x[0-9a-f]+),\s*([0-9]+),\s*"([0-9a-f]{64})"\}',
                           (ENGINE / 'gate_player_selection_parity.h').read_text())
        self.assertEqual(len(sites), 3)
        for name, address, size, digest in sites:
            with self.subTest(pin=name):
                self.assertEqual(hashlib.sha256(elf.read(int(address, 16), int(size))).hexdigest(), digest)

    def test_core_and_native_admission(self):
        with tempfile.TemporaryDirectory() as tmp:
            for name in ('test_player_selection_parity', 'test_gate_player_selection_parity'):
                with self.subTest(fixture=name):
                    output = Path(tmp) / name
                    subprocess.run(['cc', '-std=c11', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                                    '-I', str(ENGINE), str(ENGINE / (name + '.c')), '-o', str(output)], check=True)
                    subprocess.run([str(output)], check=True, capture_output=True)

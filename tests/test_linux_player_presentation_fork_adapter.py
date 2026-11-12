"""Focused Linux fork adapter checks; never starts X4."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
import unittest


class PresentationForkAdapterTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[1]
        engine = root / 'src/native/engine'
        crypto = root / 'third_party/monocypher-4.0.2/src'
        cls.directory = tempfile.TemporaryDirectory()
        cls.executable = Path(cls.directory.name) / 'presentation-fork-adapter'
        subprocess.run(['cc', '-std=gnu11', '-O1', '-mcx16', '-ffp-contract=off',
            '-I', str(engine), '-I', str(engine / 'linux'), '-I', str(crypto),
            '-I', str(crypto / 'optional'),
            str(engine / 'linux/test_gate_player_presentation_fork.c'),
            str(crypto / 'monocypher.c'), str(crypto / 'optional/monocypher-ed25519.c'),
            '-ldl', '-lpthread', '-lm', '-o', str(cls.executable)], check=True, timeout=120)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_engine_free_reservation_and_graph(self):
        subprocess.run([str(self.executable)], check=True, timeout=10)

    def test_optional_pinned_elf_pin_diagnostics(self):
        executable_path = Path(os.environ.get('X4_LINUX_EXE',
            str(Path.home() / '.local/share/Steam/steamapps/common/X4 Foundations/X4')))
        if not executable_path.is_file():
            self.skipTest('pinned X4 Linux executable not available')
        digest = hashlib.sha256()
        with executable_path.open('rb') as binary:
            for chunk in iter(lambda: binary.read(1024 * 1024), b''):
                digest.update(chunk)
        if digest.hexdigest() != '113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20':
            self.skipTest('available X4 executable is not the pinned 9.00 image')
        result = subprocess.run([str(self.executable), str(executable_path)],
                                check=True, timeout=20, text=True, capture_output=True)
        self.assertIn('stage=function index=6 va=136c820', result.stderr)
        self.assertIn('stage=slot index=1 va=3d22938', result.stderr)
        self.assertIn('stage=class_word index=2 va=2c81d1c', result.stderr)

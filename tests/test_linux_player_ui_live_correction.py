"""Bounded live pose correction without loading the game."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LINUX = ROOT / 'src/native/engine/linux'


class LiveCorrectionTests(unittest.TestCase):
    def test_guard_and_real_dispatch(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            flags = ['-O2', '-Wall', '-Wextra', '-Werror']
            pure = output / 'pure'
            subprocess.run(['cc', '-std=c11', *flags, str(LINUX / 'test_player_ui_live_correction.c'),
                            '-lm', '-o', str(pure)], check=True)
            subprocess.run([str(pure)], check=True, timeout=10)
            native = output / 'native.o'
            guard = output / 'guard.o'
            subprocess.run(['cc', '-std=c11', *flags, '-Wno-unused-function', '-c',
                            str(LINUX / 'test_player_live_pose_correction.c'), '-o', str(native)], check=True)
            subprocess.run(['c++', '-std=c++17', *flags, '-c', str(LINUX / 'player_ui_escape_guard.cpp'),
                            '-o', str(guard)], check=True)
            binary = output / 'dispatch'
            subprocess.run(['c++', str(native), str(guard), '-ldl', '-pthread', '-lm', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

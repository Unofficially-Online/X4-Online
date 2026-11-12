"""Synthetic disposable UI escape checks; does not start X4."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class UiEscapeGuardTest(unittest.TestCase):
    def test_scoped_exceptions_and_longjmp(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux'
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            executable = directory / 'escape-test'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
                            str(source / 'test_player_ui_escape_guard.cpp'), '-ldl', '-o', str(executable)],
                           check=True, timeout=120)
            for lazy in (False, True):
                library = directory / ('guard-lazy.so' if lazy else 'guard.so')
                flags = ['-DPLAYER_UI_GUARD_TEST_NO_CONSTRUCTOR'] if lazy else []
                subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fPIC',
                                '-fvisibility=hidden', '-shared', *flags, str(source / 'player_ui_escape_guard.cpp'),
                                '-ldl', '-o', str(library)], check=True, timeout=120)
                env = dict(os.environ, LD_PRELOAD=str(library))
                subprocess.run([str(executable)], env=env, check=True, timeout=10)

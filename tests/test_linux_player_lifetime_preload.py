"""Exercise the lifetime preload's synthetic stubs without launching X4."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'src/native/engine/linux'


@unittest.skipUnless(shutil.which('cc'), 'requires a C compiler')
class PlayerLifetimePreloadTests(unittest.TestCase):
    def test_guard_drain_and_unwind(self):
        fixtures = {
            'test_player_lifetime_preload_emit': 'construction, foreign thread and final guards PASS',
            'test_player_lifetime_preload_register': 'construction ownership and final publication PASS',
            'test_player_lifetime_preload_drain': 'pre-publication native entry drained',
            'test_player_lifetime_preload_unwind': 'integer/vector returns and stack unwind PASS',
        }
        with tempfile.TemporaryDirectory() as directory:
            for name, expected in fixtures.items():
                with self.subTest(fixture=name):
                    binary = Path(directory) / name
                    subprocess.run([
                        'cc', '-UNDEBUG', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-fno-omit-frame-pointer', '-pthread', '-rdynamic', str(ENGINE / (name + '.c')),
                        '-ldl', '-o', str(binary),
                    ], check=True, timeout=30)
                    result = subprocess.run([str(binary)], check=True, capture_output=True, text=True, timeout=30)
                    self.assertIn(expected, result.stdout)

    def test_provider_rejects_changed_stubs_and_wrapper(self):
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / 'libx4mp_player_lifetime.so'
            binary = Path(directory) / 'pin-fixture'
            subprocess.run(['cc', '-std=gnu11', '-O2', '-fPIC', '-shared',
                            str(ENGINE / 'test_player_lifetime_pin_provider.c'), '-o', str(library)],
                           check=True, timeout=30)
            subprocess.run(['cc', '-UNDEBUG', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(ENGINE / 'test_player_lifetime_preload_pin.c'), '-ldl', '-o', str(binary)],
                           check=True, timeout=30)
            result = subprocess.run([str(binary), str(library)], check=True, capture_output=True, text=True, timeout=30)
            self.assertIn('provider and original-view pin PASS', result.stdout)

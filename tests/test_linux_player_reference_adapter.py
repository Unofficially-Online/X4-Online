"""Read-only native-adapter mock checks; no X4 functions or process is used."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/native/engine/linux/test_player_reference_adapter.c'
SCENARIOS = (
    'off', 'request_tick', 'request_owner', 'request_tls', 'request_observer',
    'request_depth', 'request_accept', 'observe', 'marker1', 'count1', 'count_drift',
    'no_join', 'worker', 'tls', 'inactive', 'boot', 'path', 'depth', 'wrapper',
    'observer_inactive', 'owned', 'uncertain', 'pending', 'overflow', 'thread_failure',
    'slot', 'flags', 'loading', 'pins', 'read', 'identity', 'identity_change',
    'pointer_change', 'reentry', 'primary_vtable', 'reference_vtable', 'zero_target',
)


class LinuxPlayerReferenceAdapterTests(unittest.TestCase):
    def test_read_only_adapter(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'reference-adapter'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(SOURCE), '-o', str(binary)], check=True)
            for scenario in SCENARIOS:
                with self.subTest(scenario=scenario):
                    subprocess.run([str(binary), scenario], check=True)

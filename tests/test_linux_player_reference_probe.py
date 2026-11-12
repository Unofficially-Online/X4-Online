"""Engine-free P1 reference ownership checks; never invokes X4."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/native/engine/linux/test_player_reference_probe.c'
SCENARIOS = (
    'off', 'request_guards', 'normal', 'stale_sample', 'short_sample',
    'initial_marker1', 'initial_count1', 'initial_overflow', 'primary_vtable', 'zero_target',
    'acquire_refused', 'acquire_uncertain', 'acquire_uncertain_no_write', 'acquire_no_write',
    'release_refused', 'release_uncertain', 'release_uncertain_no_write', 'release_no_write',
) + tuple(f'{failure}_{phase}' for failure in
          ('boundary', 'pins', 'sample', 'read', 'identity', 'player', 'count', 'marker', 'vtable')
          for phase in (2, 3, 4)) + ('boundary_1', 'pins_1', 'sample_1', 'read_1', 'vtable_1')


class LinuxPlayerReferenceProbeTests(unittest.TestCase):
    def test_reference_roundtrip_scenarios(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'player-reference-probe'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(SOURCE), '-o', str(binary)], check=True)
            for scenario in SCENARIOS:
                with self.subTest(scenario=scenario):
                    subprocess.run([str(binary), scenario], check=True)

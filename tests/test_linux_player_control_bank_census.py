"""Read-only bank census against fake image/maps; no X4 process is involved."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/native/engine/linux/test_player_control_bank_census.c'
SCENARIOS = (
    'off', 'request_refused', 'native_once', 'observe', 'neutral_not_required',
    'digest_pointer_exclusion', 'zone_unknown', 'null_zone', 'zone_read',
    'zone_change', 'zone_runtime_change',
    'mapping_cross_page', 'mapping_permissions', 'mapping_owner', 'pins',
    'identity', 'identity_change', 'value_change', 'value_read', 'shared_change', 'reentry',
    'boundary_thread', 'boundary_tls', 'boundary_join', 'boundary_depth',
    'boundary_wrapper', 'boundary_owned', 'boundary_pending', 'boundary_flags',
    'boundary_loading', 'boundary_slot', 'boundary_tick',
)


class PlayerControlBankCensusTests(unittest.TestCase):
    def test_fake_image_census_and_adapter(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'bank-census'
            subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            str(SOURCE), '-o', str(executable)], check=True, cwd=ROOT)
            for scenario in SCENARIOS:
                with self.subTest(scenario=scenario):
                    result = subprocess.run([str(executable), scenario], cwd=ROOT,
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

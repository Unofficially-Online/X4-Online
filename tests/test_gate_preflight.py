from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import gate_preflight  # noqa: E402


def overlay(path, extra=''):
    (path / 'ui').mkdir(parents=True)
    (path / 'ui/native_gate_probe.lua').write_bytes((ROOT / 'tools/dev/native_gate_probe.lua').read_bytes())
    (path / 'ui.xml.files').write_text('ui/native_gate_probe.lua\n')
    (path / 'ui/config.lua.append').write_text(
        'X4LAN_CONFIG.native_gate_test = true\nX4LAN_CONFIG.native_gate_hash = "clock"\n'
        'X4LAN_CONFIG.test_profile = "45841"\nX4LAN_CONFIG.port = @X4LANE_PORT@\nX4LAN_CONFIG.token = "@X4LANE_TOKEN@"\n'
        'X4LAN_CONFIG.slot = "@X4LANE_SLOT@"\nX4LAN_CONFIG.native_gate_snapshot_sha = "' + 'a' * 64 + '"\n' + extra)
    return path


class Preflight(unittest.TestCase):
    def test_clock_overlay_arms_and_requests_the_load(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(gate_preflight.main([str(overlay(Path(tmp) / 'o'))]), 0)

    def test_conflicting_keys_are_refused_before_a_launch(self):
        # script_rng 'key' replaces ai_rng: the probe refuses both together.
        with tempfile.TemporaryDirectory() as tmp:
            bad = overlay(Path(tmp) / 'o', 'X4LAN_CONFIG.native_gate_world_identities = true\n'
                          'X4LAN_CONFIG.native_gate_interrupt_diag = true\nX4LAN_CONFIG.native_gate_nested_actions = true\n'
                          "X4LAN_CONFIG.native_gate_script_rng = 'key'\nX4LAN_CONFIG.native_gate_ai_rng = true\n")
            self.assertEqual(gate_preflight.main([str(bad)]), 1)


if __name__ == '__main__':
    unittest.main()

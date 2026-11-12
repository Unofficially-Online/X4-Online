import sys
import tempfile
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import linux_gate_live as runner


class AncestryWiringTest(unittest.TestCase):
    def test_identity_comes_from_validated_separate_evidence(self):
        client = {'pass': True, 'presentation': {'pass': True, 'occupied_runtime': 55},
                  'frames': [{'bodies': [{'ship': 0xabc000, 'runtime': 66}]}]}
        with tempfile.TemporaryDirectory() as tmp:
            stderr = Path(tmp) / 'stderr'
            stderr.write_text('X4LAN KEYBOARD_SLOT1 tick=40 capture=38 saved=00000000000ad43a record=00 held_before=0 held_after=0\n')
            identity = runner.ancestry_expected_identity(client, stderr)
            self.assertEqual(identity, {'p1_runtime': 55, 'p2': 0xabc000,
                'p2_runtime': 66, 'p1_saved': 0xad43a, 'p2_saved': 0x175310})
            client['pass'] = False
            with self.assertRaises(ValueError):
                runner.ancestry_expected_identity(client, stderr)
            client['pass'] = True
            client['presentation']['pass'] = False
            with self.assertRaises(ValueError):
                runner.ancestry_expected_identity(client, stderr)


if __name__ == '__main__':
    unittest.main()

"""Single-local guest admission never asserts remote session equality."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_gate_live as live


class ExternalGuestTests(unittest.TestCase):
    def setUp(self):
        self.manifest = {'shared_identity_t40': True, 'source_dirty': False, 'snapshot_sha256': 'a' * 64,
                         'gate_so_sha256': 'b' * 64, 'commit': 'c' * 40}
        self.args = [True, True, [2], 2, True, self.manifest, 'a' * 64, 3,
                     '192.0.2.1', 2000000000, 'c' * 40]

    def test_explicit_guest_and_missing_remote_evidence(self):
        receipt = live.validate_external_guest(*self.args)
        self.assertEqual(receipt['remote_evidence'], 'pending')
        self.assertIsNone(receipt['cross_peer_equality'])
        self.assertIsNone(receipt['world_equality'])
        guest = {'slot': 2, 'pass': True, 'errors': [], 'validation_pending': False,
                 'slot_records': {'1': ['00'], '2': ['01']}}
        self.assertTrue(live.external_guest_local_physics({'2': guest}, 1))
        self.assertTrue(live.network_peer_errors({'2': guest}, 1))
        guest['slot_records']['1'] = []
        self.assertFalse(live.external_guest_local_physics({'2': guest}, 1))

    def test_every_admission_dependency_is_required(self):
        for index, invalid in [(1, False), (2, [1]), (2, [1, 2]), (3, 3),
                               (4, False), (6, 'd' * 64), (6, None), (7, 2),
                               (8, None), (9, None), (10, 'd' * 40)]:
            args = self.args.copy()
            args[index] = invalid
            with self.subTest(index=index, invalid=invalid), self.assertRaises(SystemExit):
                live.validate_external_guest(*args)
        args = self.args.copy()
        args[5] = dict(self.manifest, source_dirty=True)
        with self.assertRaises(SystemExit):
            live.validate_external_guest(*args)
        args[5] = dict(self.manifest, shared_identity_t40=False)
        with self.assertRaises(SystemExit):
            live.validate_external_guest(*args)

    def test_default_remains_pair_and_target_pins_are_not_silently_ignored(self):
        args = self.args.copy()
        args[0] = False
        args[6:8] = [None, None]
        args[10] = None
        self.assertIsNone(live.validate_external_guest(*args))
        args[6] = 'a' * 64
        with self.assertRaises(SystemExit):
            live.validate_external_guest(*args)


if __name__ == '__main__':
    unittest.main()

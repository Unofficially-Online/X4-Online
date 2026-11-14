import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_player_space_bundle as space
from linux_player_network_evidence import decode_record


def record(capture, active, events=()):
    data = bytearray(43)
    data[0] = 3
    data[1] = active
    data[2:4] = capture.to_bytes(2, 'little')
    data[42] = len(events)
    for kind, event_id, value in events:
        data.extend(struct.pack('<HI', (kind << 14) | event_id, value))
    return data.hex()


class SpaceEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / 'stderr'
        self.records = {'1': [record(38, 0, [(3, 8, 0)]), record(39, 1, [(3, 8, 1)])],
                        '2': [record(38, 1), record(39, 1)]}
        self.rows = [
            'X4LAN SPACE v=1 stage=local tick=38 worker=42 owner=30000 controller=10000 active=0 cf0_before=1\n',
            'X4LAN SPACE v=1 stage=local tick=39 worker=42 owner=30000 controller=10000 active=1 cf0_before=0\n',
            'X4LAN SPACE v=1 stage=replay tick=40 worker=42 slot=1 controller=10000 active=0 flight=0\n',
            'X4LAN SPACE v=1 stage=replay tick=41 worker=42 slot=1 controller=10000 active=1 flight=1\n',
        ]
        self.cursor = {'pass': True, 'worker': 42, 'canonical': 0x10000, 'guest': 0x20000}

    def tearDown(self):
        self.temp.cleanup()

    def collect(self, rows=None, records=None):
        self.path.write_text(''.join(self.rows if rows is None else rows))
        raw = self.path.read_bytes()
        physics = {'pass': True, 'slot': 1, 'slot_records': records or self.records,
                   'stderr_sha256': hashlib.sha256(raw).hexdigest()}
        return space.collect(self.path, True, physics, self.cursor)

    def test_ordered_effect_receipts_and_identity_are_required(self):
        self.assertTrue(self.collect()['pass'])
        self.assertFalse(self.collect(self.rows[:-1])['pass'])
        empty = {'1': [record(38, 1), record(39, 1)], '2': [record(38, 1), record(39, 1)]}
        self.assertFalse(self.collect([], empty)['pass'])
        self.assertFalse(self.collect(self.rows + [self.rows[-1]])['pass'])
        self.assertFalse(self.collect([self.rows[1], self.rows[0], *self.rows[2:]])['pass'])
        bad = list(self.rows)
        bad[-1] = bad[-1].replace('controller=10000', 'controller=20000')
        self.assertFalse(self.collect(bad)['pass'])
        bad = list(self.rows)
        bad[-1] = bad[-1].replace('flight=1', 'flight=0')
        self.assertFalse(self.collect(bad)['pass'])
        bad_records = dict(self.records, **{'1': [record(38, 1, [(3, 8, 0)]), self.records['1'][1]]})
        self.assertFalse(self.collect(records=bad_records)['pass'])

    def test_remote_slot_effect_uses_guest_identity(self):
        records = dict(self.records, **{'2': [record(38, 0, [(3, 8, 0)]),
                                                 record(39, 1, [(3, 8, 1)])]})
        rows = list(self.rows)
        rows.insert(3, 'X4LAN SPACE v=1 stage=replay tick=40 worker=42 slot=2 controller=20000 active=0 flight=0\n')
        rows.append('X4LAN SPACE v=1 stage=replay tick=41 worker=42 slot=2 controller=20000 active=1 flight=1\n')
        self.assertTrue(self.collect(rows, records)['pass'])
        rows[3] = rows[3].replace('controller=20000', 'controller=10000')
        self.assertFalse(self.collect(rows, records)['pass'])

    def test_version_seven_receipts_and_exact_legacy_delegation(self):
        root = Path(self.temp.name)
        files = {}
        for name in space.SOURCES:
            path = root / 'source-receipts' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(name)
            files['source-receipts/' + name] = hashlib.sha256(path.read_bytes()).hexdigest()
        for name in space.HELPERS:
            path = root / name
            path.write_bytes((root / 'source-receipts/tools/dev' / name).read_bytes())
            files[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        options = {'view_query_source_version': 7, 'player_space_context': True,
                   'player_space_context_evidence_version': 1, 'player_cursor_active_flight': True,
                   'player_startup_phases': 'v1-t40', 'player_ui_session_frames': 180,
                   'independent_keyboard_frames': 180, 'files': files}
        with patch.object(space.previous, 'validate_bundle', return_value=True) as previous:
            self.assertTrue(space.validate_bundle(root, options, True))
            self.assertEqual(previous.call_args.args[1]['view_query_source_version'], 6)
            broken = dict(options, files=dict(files))
            broken['files'].pop('source-receipts/' + space.SOURCES[0])
            with self.assertRaises(ValueError):
                space.validate_bundle(root, broken, True)
            with self.assertRaises(ValueError):
                space.validate_bundle(root, dict(options, view_query_source_version=6), True)
            space.validate_bundle(root, dict(options, view_query_source_version=6,
                                             player_space_context=False,
                                             player_space_context_evidence_version=None), True)
            self.assertEqual(previous.call_args.args[1]['view_query_source_version'], 6)

    def test_default_closed_and_only_explicit_context(self):
        inactive = self.records['1'][0]
        with self.assertRaises(ValueError):
            decode_record(inactive, 40, 0, allow_cursor=True)
        self.assertEqual(decode_record(inactive, 40, 0, allow_cursor=True, allow_space=True), 0)
        with self.assertRaises(ValueError):
            decode_record(inactive, 40, 0, allow_space=True)
        self.assertNotIn('X4_DEV_PLAYER_SPACE', space.environment({'X4_DEV_PLAYER_SPACE': 'context8'}, False))
        self.assertEqual(space.environment({}, True)['X4_DEV_PLAYER_SPACE'], 'context8')
        with self.assertRaises(ValueError):
            space.enabled({'player_space_context': True})
        self.path.write_text(self.rows[0])
        self.assertFalse(space.collect(self.path, False, {}, {})['pass'])


if __name__ == '__main__':
    unittest.main()

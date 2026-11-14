import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_cursor_input_trace as trace


class CursorInputTraceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / 'stderr'
        self.options = {'cursor_input_trace': True, 'player_cursor_active_flight': True,
                        'player_startup_phases': 'v1-t40', 'player_ui_session_frames': 180,
                        'independent_keyboard_frames': 180}
        self.samples = {38: self.row(38, 0), 40: self.row(40, 1, steer=1)}

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def row(tick, index, *, steer=0):
        return (f'X4 INPUT_MOUSE v=1 tick={tick} index={index} worker=42 input=10000 device=20000 '
                f'mode33=1 steer36={steer} transient37=0 direct38=0 block39=0 gate42=1 '
                'override_a010=0 context8=1 ignored=0 window_gate_present=1 window_gate=30000 '
                'window_gate_value=1 window_owner=40000 window_context_present=1 window_context=50000 '
                'window_modal=0 mutation=0\n')

    def collect(self, samples=None, *, enabled=True, options=None, edit=None):
        samples = self.samples if samples is None else samples
        lines = []
        for tick in range(38, 220):
            lines.append(f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=enter\n')
            if tick in samples:
                lines.extend(samples[tick] if isinstance(samples[tick], list) else [samples[tick]])
            if tick <= 217:
                lines.append(f'X4LAN CURSOR v=1 stage=capture tick={tick} focused=1\n')
            if tick == 219:
                lines.append('X4LAN CURSOR v=1 stage=close tick=219 captured=180 slot1=182 slot2=180 complete=1\n')
            else:
                lines.append(f'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick={tick} tail=1 event=release\n')
        if edit:
            edit(lines)
        self.path.write_text(''.join(lines))
        digest = hashlib.sha256(self.path.read_bytes()).hexdigest()
        physics = {'pass': True, 'stderr_sha256': digest, 'log_sha256': 'fixture',
                   'preapply_frames': [{'tick': tick, 'worker_thread': 42} for tick in (37, 38, 39)],
                   'frames': [{'tick': tick, 'worker_thread': 42} for tick in range(40, 220)]}
        cursor = {'pass': True, 'enabled': True, 'performed': True, 'stderr_sha256': digest, 'log_sha256': 'fixture', 'worker': 42,
                  'captures': [{'tick': tick} for tick in range(38, 218)],
                  'raw_receipts': ['X4LAN CURSOR v=1 stage=close tick=219 captured=180 slot1=182 slot2=180 complete=1\n']}
        return trace.collect(self.path, enabled, physics, cursor, options or self.options)

    def test_valid_samples_and_change_only(self):
        proof = self.collect()
        self.assertTrue(proof['pass'], proof['errors'])
        self.assertEqual([item['tick'] for item in proof['samples']], [38, 40])
        self.assertFalse(proof['movement_proven'])
        repeated = {38: self.row(38, 0), 40: self.row(40, 1)}
        self.assertFalse(self.collect(repeated)['pass'])

    def test_indices_workers_owner_ticks_and_presence_are_strict(self):
        for broken in (
            self.row(40, 2, steer=1),
            self.row(40, 1, steer=1).replace('worker=42', 'worker=43'),
            self.row(40, 1, steer=1).replace('input=10000', 'input=10008'),
            self.row(40, 1, steer=1).replace('window_gate_present=1', 'window_gate_present=0'),
            self.row(40, 1, steer=1).replace('steer36=1', 'steer36=256'),
            self.row(40, 1, steer=1).replace('mutation=0', 'mutation=1'),
            self.row(40, 1, steer=1).rstrip('\n'),
        ):
            with self.subTest(broken=broken):
                self.assertFalse(self.collect({38: self.row(38, 0), 40: broken})['pass'])
        self.assertFalse(self.collect({40: self.row(40, 0)})['pass'])
        self.assertFalse(self.collect({38: [self.row(38, 0), self.row(38, 1, steer=1)]})['pass'])

    def test_evidence_hash_mismatch_and_failed_parent_are_rejected(self):
        self.assertTrue(self.collect()['pass'])
        digest = hashlib.sha256(self.path.read_bytes()).hexdigest()
        physics = {'pass': True, 'stderr_sha256': digest, 'log_sha256': 'log',
                   'preapply_frames': [{'tick': tick, 'worker_thread': 42} for tick in (37, 38, 39)],
                   'frames': [{'tick': tick, 'worker_thread': 42} for tick in range(40, 220)]}
        cursor = {'pass': True, 'enabled': True, 'performed': True, 'stderr_sha256': digest, 'log_sha256': 'log', 'worker': 42,
                  'captures': [{'tick': tick} for tick in range(38, 218)],
                  'raw_receipts': ['X4LAN CURSOR v=1 stage=close tick=219 captured=180 slot1=182 slot2=180 complete=1\n']}
        self.assertFalse(trace.collect(self.path, True, dict(physics, stderr_sha256='wrong'), cursor, self.options)['pass'])
        self.assertFalse(trace.collect(self.path, True, physics, dict(cursor, log_sha256='wrong'), self.options)['pass'])
        self.assertFalse(trace.collect(self.path, True, physics, dict(cursor, **{'pass': False}), self.options)['pass'])
        self.assertFalse(trace.collect(self.path, True, dict(physics, frames=[]), cursor, self.options)['pass'])

    def test_tail_and_capture_order(self):
        def move_after_release(lines):
            row = self.row(40, 1, steer=1)
            lines.remove(row)
            position = lines.index('X4LAN PLAYER_CONTROL_RECURRING_WORKER tick=40 tail=1 event=release\n')
            lines.insert(position + 1, row)
        self.assertFalse(self.collect(edit=move_after_release)['pass'])
        def move_after_capture(lines):
            row = self.row(40, 1, steer=1)
            lines.remove(row)
            position = lines.index('X4LAN CURSOR v=1 stage=capture tick=40 focused=1\n')
            lines.insert(position + 1, row)
        self.assertFalse(self.collect(edit=move_after_capture)['pass'])

    def test_final_tick_uses_unique_cursor_close_instead_of_worker_release(self):
        final = {38: self.row(38, 0), 219: self.row(219, 1, steer=1)}
        self.assertTrue(self.collect(final)['pass'])
        close = 'X4LAN CURSOR v=1 stage=close tick=219 captured=180 slot1=182 slot2=180 complete=1\n'
        self.assertFalse(self.collect(final, edit=lambda lines: lines.remove(close))['pass'])
        self.assertFalse(self.collect(final, edit=lambda lines: lines.append(close))['pass'])
        def early_close(lines):
            lines.remove(close)
            lines.insert(lines.index(self.row(219, 1, steer=1)), close)
        self.assertFalse(self.collect(final, edit=early_close)['pass'])
        def unexpected_release(lines):
            lines.append('X4LAN PLAYER_CONTROL_RECURRING_WORKER tick=219 tail=1 event=release\n')
        self.assertFalse(self.collect(final, edit=unexpected_release)['pass'])

    def test_defaults_and_explicit_dependencies(self):
        self.assertFalse(trace.enabled({}))
        self.assertNotIn(trace.ENVIRONMENT, trace.environment({trace.ENVIRONMENT: '1'}, False))
        self.assertEqual(trace.environment({}, True)[trace.ENVIRONMENT], '1')
        for options in ({'cursor_input_trace': True}, dict(self.options, player_cursor_active_flight=False),
                        dict(self.options, player_startup_phases=None), dict(self.options, cursor_input_trace=1)):
            with self.assertRaises(ValueError):
                trace.enabled(options)
        self.path.write_text(self.row(38, 0))
        self.assertFalse(trace.collect(self.path, False, {}, {}, {})['pass'])


if __name__ == '__main__':
    unittest.main()

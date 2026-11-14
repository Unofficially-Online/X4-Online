"""Synthetic final-worker checkpoint admission; these are not native results."""
import unittest
from unittest.mock import patch
from pathlib import Path
import tempfile

import linux_gate_live as live
from linux_player_network_evidence import parse_network_physics_evidence, collect_network_physics_evidence
from test_linux_t40_evidence import common_trace


class SemaphoreCheckpointTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows, cls.stderr, cls.engine = common_trace(180)
        cls.index = max(i for i, row in enumerate(cls.rows) if 'physics=upkeep stage=leave ' in row) + 1
        cls.checkpoint = 'X4LAN SEMAPHORE OBSERVER CHECKPOINT tick=219 owner=77 main_parked=1 complete=1\n'

    def parse(self, rows, **kwargs):
        return parse_network_physics_evidence(rows, self.stderr, self.engine, 223,
            frame_count=180, slot=2, session_frames=180, startup='v1-t40', **kwargs)

    def inserted(self, checkpoint=None, index=None):
        i = self.index if index is None else index
        return self.rows[:i] + [checkpoint or self.checkpoint] + self.rows[i:]

    def test_explicit_admission_and_default_refusal(self):
        rows = self.inserted()
        self.assertFalse(self.parse(rows)['pass'])
        result = self.parse(rows, allow_semaphore_checkpoint=True)
        self.assertTrue(result['pass'], result['errors'][:8])
        self.assertEqual(result['semaphore_checkpoint'], {
            'tick': 219, 'owner_os_tid': 77, 'main_parked': True, 'complete': True})
        self.assertFalse(result['other_threads_excluded'])
        self.assertFalse(self.parse(rows, allow_semaphore_checkpoint=1)['pass'])

    def test_missing_duplicate_wrong_tick_and_fields(self):
        cases = [self.rows, self.inserted()[:self.index] + [self.checkpoint, self.checkpoint] + self.rows[self.index:]]
        for old, new in [('tick=219', 'tick=218'), ('owner=77', 'owner=0'),
                         ('owner=77', 'owner=4294967296'), ('main_parked=1', 'main_parked=0'),
                         ('complete=1', 'complete=0'), ('owner=77', 'owner=077')]:
            cases.append(self.inserted(self.checkpoint.replace(old, new)))
        cases.extend([self.inserted(self.checkpoint.rstrip('\n')), self.inserted(self.checkpoint + 'junk\n')])
        for rows in cases:
            with self.subTest(checkpoints=[row for row in rows if 'CHECKPOINT' in row]):
                self.assertFalse(self.parse(rows, allow_semaphore_checkpoint=True)['pass'])

    def test_early_late_and_wrong_worker_boundary(self):
        first = next(i for i, row in enumerate(self.rows) if 'physics=upkeep stage=leave ' in row) + 1
        for index in (0, first, self.index - 1, self.index + 1, len(self.rows) - 1, len(self.rows)):
            with self.subTest(index=index):
                self.assertFalse(self.parse(self.inserted(index=index), allow_semaphore_checkpoint=True)['pass'])

    def test_collector_threads_admission_through_both_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace, stderr, engine, output = [root / name for name in ('trace', 'stderr', 'engine', 'output')]
            for path, rows in ((trace, self.inserted()), (stderr, self.stderr), (engine, self.engine), (output, [])):
                path.write_text(''.join(rows))
            for active in (False, True):
                result = collect_network_physics_evidence(trace, root / str(active), engine, output, 223,
                    frame_count=180, stderr=stderr, slot=2, session_frames=180, startup='v1-t40',
                    allow_semaphore_checkpoint=active)
                self.assertEqual(result['pass'], active, result['errors'][:8])

    def test_runner_passes_explicit_opt_in_only(self):
        with tempfile.TemporaryDirectory() as directory:
            stderr = Path(directory) / 'stderr'
            stderr.write_text('')
            for active in (False, True):
                evidence = {'pass': True, 'errors': [], 'stderr_sha256': live.sha(stderr)}
                with patch.object(live, 'collect_network_physics_evidence', return_value=evidence) as collect:
                    live.collect_network_client((), stderr, 180, 2, allow_semaphore_checkpoint=active)
                self.assertEqual(collect.call_args.kwargs.get('allow_semaphore_checkpoint', False), active)

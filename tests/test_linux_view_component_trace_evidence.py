import sys
import unittest
import hashlib
import tempfile
from unittest.mock import patch
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
from linux_view_component_trace_evidence import parse_component_trace, collect_component_trace
import linux_player_network_evidence as network_evidence


class ComponentTraceEvidenceTests(unittest.TestCase):
    def test_verified_rows_and_mutations(self):
        ancestry = {'pass': True, 'tid': 22, 'bind_tick': 8, 'bind': {'p1': 0x10000, 'p2': 0x20000}}
        rows = [
            'X4 COMPONENT_TRACE v=1 stage=bind tick=8 tid=22 p1=10000 p2=20000\n',
            'X4 COMPONENT_TRACE v=1 stage=sample site=0 index=0 tick=8 ordinal=0 phase=2 object=30000 class=3c6a440 runtime=7 parent=20000 ancestor=0 valid=1 slot=2 failure=0\n',
            'X4 COMPONENT_TRACE v=1 stage=site site=0 calls=1 sampled=1 missed=1 capped=0 foreign_lower=0 rows=1\n',
            'X4 COMPONENT_TRACE v=1 stage=sample site=1 index=0 tick=8 ordinal=0 phase=1 object=40000 class=3ccad80 runtime=8 parent=10000 ancestor=10000 valid=1 slot=1 failure=0\n',
            'X4 COMPONENT_TRACE v=1 stage=site site=1 calls=1 sampled=1 missed=1 capped=0 foreign_lower=1 rows=1\n',
            'X4 COMPONENT_TRACE v=1 stage=close tick=8 tid=22 sampling=bounded foreign=lower_bound\n',
        ]
        result = parse_component_trace(rows, ancestry, 8)
        self.assertTrue(result['pass'], result['errors'])
        self.assertFalse(result['ownership_exhaustive'])
        for index, old, new in ((1, 'slot=2', 'slot=1'), (1, 'ordinal=0', 'ordinal=2'),
                                (3, 'class=3ccad80', 'class=3c6a440'), (5, 'tick=8', 'tick=9')):
            bad = rows.copy(); bad[index] = bad[index].replace(old, new)
            self.assertFalse(parse_component_trace(bad, ancestry, 8)['pass'])

    def test_raw_hash_binding_and_disabled_trace_rejection(self):
        raw = (b'X4 COMPONENT_TRACE v=1 stage=bind tick=8 tid=22 p1=10000 p2=20000\n'
               b'X4 COMPONENT_TRACE v=1 stage=site site=0 calls=0 sampled=0 missed=2 capped=0 foreign_lower=0 rows=0\n'
               b'X4 COMPONENT_TRACE v=1 stage=site site=1 calls=0 sampled=0 missed=2 capped=0 foreign_lower=0 rows=0\n'
               b'X4 COMPONENT_TRACE v=1 stage=close tick=8 tid=22 sampling=bounded foreign=lower_bound\n')
        digest = hashlib.sha256(raw).hexdigest()
        ancestry = {'pass': True, 'tid': 22, 'bind_tick': 8, 'bind': {'p1': 0x10000, 'p2': 0x20000},
                    'stderr_sha256': digest}
        physics = {'pass': True, 'stderr_sha256': digest}
        with tempfile.TemporaryDirectory() as tmp:
            stderr = Path(tmp) / 'stderr.log'
            stderr.write_bytes(raw)
            self.assertTrue(collect_component_trace(stderr, ancestry, physics, 8, True)['pass'])
            self.assertFalse(collect_component_trace(stderr, ancestry, physics, 8, False)['pass'])
            stderr.write_bytes(raw + b'unrelated changed raw line\n')
            proof = collect_component_trace(stderr, ancestry, physics, 8, True)
            self.assertFalse(proof['pass'])
            self.assertIn('stderr differs from accepted physics evidence', proof['errors'])
            self.assertIn('stderr differs from accepted ancestry evidence', proof['errors'])
            physics = {'pass': True, 'stderr_sha256': hashlib.sha256(stderr.read_bytes()).hexdigest()}
            self.assertFalse(collect_component_trace(stderr, ancestry, physics, 8, True)['pass'])
            stderr.write_bytes(b'no trace here\n')
            physics = {'pass': True, 'stderr_sha256': hashlib.sha256(stderr.read_bytes()).hexdigest()}
            self.assertTrue(collect_component_trace(stderr, None, physics, 8, False)['pass'])
            self.assertTrue(collect_component_trace(stderr, None, None, None, False)['pass'])
            self.assertFalse(collect_component_trace(stderr, None, {'pass': True, 'stderr_sha256': digest}, 8, False)['pass'])

    def test_physics_rejects_changed_stderr_between_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            retained, engine, stderr = (directory / name for name in ('retained', 'engine', 'stderr'))
            for path in (retained, engine, stderr):
                path.write_text('current\n')
            base = {'errors': [], 'pass': True, 'log_sha256': 'x',
                    'stderr_sha256': hashlib.sha256(b'earlier\n').hexdigest(),
                    'native_fd_path': 'native', 'retained_log': str(retained)}
            with patch.object(network_evidence, 'collect_recurring_physics_evidence', return_value=base), \
                 patch.object(network_evidence, 'parse_network_physics_evidence', return_value={'errors': []}):
                result = network_evidence.collect_network_physics_evidence(
                    'trace', retained, engine, 'output', 0, frame_count=180, stderr=stderr, slot=1)
            self.assertFalse(result['pass'])
            self.assertIn('worker stderr changed between physics evidence passes', result['errors'])

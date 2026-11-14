"""Synthetic trace model only; does not load or start X4."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ComponentIdentityTraceTest(unittest.TestCase):
    def test_rotating_ordinals_and_unstable_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'component-identity-trace'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'src/native/engine/linux/test_player_view_component_identity_trace.c'),
                            '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_preload_bridge_and_foreign_count(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'component-identity-preload'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-pthread',
                            str(ROOT / 'src/native/engine/linux/test_player_view_component_trace_preload.c'),
                            '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_gate_authenticates_phase_and_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'component-identity-gate'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'src/native/engine/linux/test_gate_player_view_component_trace.c'),
                            '-o', str(executable)], check=True)
            result = subprocess.run([str(executable)], check=True, capture_output=True, text=True)
            sys.path.insert(0, str(ROOT / 'tools/dev'))
            from linux_view_component_trace_evidence import parse_component_trace
            first = result.stderr.splitlines()[0].split()
            owner = int(next(part.split('=')[1] for part in first if part.startswith('tid=')))
            ancestry = {'pass': True, 'tid': owner, 'bind_tick': 8,
                        'bind': {'p1': 0x10000, 'p2': 0x20000}}
            proof = parse_component_trace(result.stderr.splitlines(keepends=True), ancestry, 8)
            self.assertTrue(proof['pass'], proof['errors'])

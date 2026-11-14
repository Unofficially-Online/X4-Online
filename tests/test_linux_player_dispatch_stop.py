"""Focused real gate finish/stop paths; no game or network execution."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class DispatchStopTests(unittest.TestCase):
    def test_actual_finish_public_stop_lua_stop_and_failure_paths(self):
        root = Path(__file__).resolve().parents[1]
        engine = root / 'src/native/engine'
        source = (engine / 'linux/gate_linux.c').read_text()
        finish = source[source.index('static void finish(unsigned reason) {'):]
        self.assertLess(finish.index('player_dispatch_stop_before_cleanup(reason);'),
                        finish.index('keyboard_probe_stop();'))
        dispatch = (engine / 'linux/gate_player_worker_dispatch.h').read_text()
        request = dispatch[dispatch.index('static void player_dispatch_request(void) {'):]
        self.assertLess(request.index('player_dispatch_stop_claim()'), request.index('dlsym('))
        self.assertLess(request.index('player_dispatch_stop_claim()'), request.index('pwda_request('))
        monocypher = root / 'third_party/monocypher-4.0.2/src'
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'stop'
            subprocess.run(['cc', '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror',
                            '-Wno-misleading-indentation', '-mcx16', '-ffp-contract=off', '-pthread',
                            '-I', str(engine), '-I', str(engine / 'linux'), '-I', str(monocypher),
                            '-I', str(monocypher / 'optional'),
                            str(engine / 'linux/test_gate_player_dispatch_stop.c'),
                            str(monocypher / 'monocypher.c'), str(monocypher / 'optional/monocypher-ed25519.c'),
                            '-Wl,--wrap=write', '-ldl', '-lm', '-o', str(executable)], check=True, timeout=90)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stderr.count('WORKER_DISPATCH_TERMINAL'), 45)
            self.assertIn('policy=terminate-only exit=225', result.stderr)
            self.assertNotIn('exit=223', result.stderr)

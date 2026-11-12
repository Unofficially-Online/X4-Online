"""Engine-free keyboard value-bank scope and owned cleanup regressions."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LINUX = ROOT / 'src/native/engine/linux'


class PlayerControlBanksTests(unittest.TestCase):
    def compile_and_run(self, directory, *, header=None):
        directory = Path(directory)
        source = directory / 'test_player_control_banks.c'
        source.write_text((LINUX / source.name).read_text())
        (directory / 'player_control_banks.h').write_text(
            header if header is not None else
            (LINUX / 'player_control_banks.h').read_text()
        )
        executable = directory / 'banks'
        command = ['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror']
        build = subprocess.run(command + [str(source), '-o', str(executable)],
                               cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
        return subprocess.run([str(executable)], cwd=ROOT,
                              capture_output=True, text=True)

    def test_scope_and_failure_injection(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.compile_and_run(directory)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('11 scenario groups passed', result.stdout)

    def test_regressions_detect_lost_obligations(self):
        original = (LINUX / 'player_control_banks.h').read_text()
        mutations = {
            'opaque_device_zero': (
                'if (policy->neutral_device[byte_index]) {',
                'if (pcb_device_byte(byte_index)) {',
            ),
            'forget_external_zone': (
                'if (scope->zone_pending) {',
                'if (0) {',
            ),
            'overwrite_foreign': (
                'scope->pending[flat_index] = PCB_FOREIGN;\n                clean = 0;',
                'scope->pending[flat_index] = PCB_OWNED;\n                clean = 0;',
            ),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(mutation=name), tempfile.TemporaryDirectory() as directory:
                self.assertEqual(original.count(before), 1)
                result = self.compile_and_run(directory, header=original.replace(before, after))
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)

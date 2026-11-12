from pathlib import Path
import subprocess
import tempfile
import unittest


class PlayerUIViewstateTest(unittest.TestCase):
    def test_bounded_reads_and_pointer_changes(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_player_ui_viewstate.c'
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'ui-viewstate-test'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(source),
                            '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

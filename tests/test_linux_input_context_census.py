"""Engine-free native input registration census checks with a bounded fake image."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/native/engine/linux/test_input_context_census.c'


class InputContextCensusTests(unittest.TestCase):
    def test_complete_tree_and_adversarial_images(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'input-context-census'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(SOURCE), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

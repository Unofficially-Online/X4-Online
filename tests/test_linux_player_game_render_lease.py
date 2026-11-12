import pathlib
import subprocess
import tempfile
import unittest


class PlayerGameRenderLeaseTest(unittest.TestCase):
    def test_forwarding_and_bounded_lease(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_player_game_render_lease.c'
        guard = root / 'src/native/engine/linux/player_ui_escape_guard.cpp'
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / 'game-render-lease'
            obj = pathlib.Path(directory) / 'fixture.o'
            subprocess.run(['cc', '-std=c11', '-pthread', '-c', str(source), '-o', str(obj)],
                           check=True, timeout=120)
            subprocess.run(['c++', '-std=c++17', '-pthread', str(obj), str(guard), '-ldl',
                            '-o', str(executable)], check=True, timeout=120)
            subprocess.run([str(executable)], check=True, timeout=30)

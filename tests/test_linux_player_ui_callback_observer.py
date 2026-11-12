import pathlib
import subprocess
import tempfile
import unittest


class PlayerUiCallbackObserverTest(unittest.TestCase):
    def test_scope_forwarding_and_foreign_thread(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_player_ui_callback_observer.c'
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / 'ui-observer-test'
            guard = root / 'src/native/engine/linux/player_ui_escape_guard.cpp'
            observer_object = pathlib.Path(directory) / 'observer.o'
            subprocess.run(['cc', '-std=c11', '-pthread', '-c', str(source),
                            '-o', str(observer_object)], check=True)
            subprocess.run(['c++', '-std=c++17', '-pthread', str(observer_object), str(guard),
                            '-ldl', '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

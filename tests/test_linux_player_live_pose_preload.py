"""Exercise the opt-in live pose preload without launching X4."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LINUX = ROOT / 'src/native/engine/linux'


class LivePosePreloadTests(unittest.TestCase):
    def test_dispatch_and_exception(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            flags = ['-O2', '-Wall', '-Wextra', '-Werror']
            guard = out / 'guard.o'
            subprocess.run(['c++', '-std=c++17', *flags, '-c', str(LINUX / 'player_ui_escape_guard.cpp'),
                            '-o', str(guard)], check=True)
            for name in ('test_player_live_pose_preload', 'test_player_live_pose_throw'):
                source = LINUX / (name + '.c')
                obj = out / (name + '.o')
                subprocess.run(['cc', '-std=c11', *flags, '-Wno-unused-function', '-c', str(source),
                                '-o', str(obj)], check=True)
                objects = [obj, guard]
                if name.endswith('throw'):
                    cpp_obj = out / 'throw_cpp.o'
                    subprocess.run(['c++', '-std=c++17', *flags, '-c',
                                    str(LINUX / (name + '.cpp')), '-o', str(cpp_obj)], check=True)
                    objects.append(cpp_obj)
                binary = out / name
                subprocess.run(['c++', *map(str, objects), '-ldl', '-pthread', '-o', str(binary)], check=True)
                subprocess.run([str(binary)], check=True, timeout=10)

"""Exercise Linux cursor convergence without loading X4 or enabling input."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class LinuxCursorConvergenceTest(unittest.TestCase):
    def test_both_slots_focus_and_failure(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_player_cursor_convergence.c'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'cursor'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(binary)], check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_native_bridge_preserves_live_registers_and_flags(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'bridge'
            subprocess.run(['cc', '-UNDEBUG', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-rdynamic', str(source / 'test_player_cursor_bridge.c'),
                            str(source / 'player_cursor_bridge.S'), '-o', str(binary)],
                           check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_stub_unwind_registration_and_patch(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux'
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            flags = ['-O2', '-Wall', '-Wextra', '-Werror']
            subprocess.run(['c++', '-std=c++17', *flags, '-c', str(source / 'player_ui_escape_guard.cpp'), '-o', str(out / 'guard.o')], check=True)
            subprocess.run(['cc', '-UNDEBUG', '-std=c11', *flags, '-Wno-unused-function', '-c', str(source / 'test_player_cursor_preload.c'), '-o', str(out / 'test.o')], check=True)
            subprocess.run(['cc', '-c', str(source / 'player_cursor_bridge.S'), '-o', str(out / 'bridge.o')], check=True)
            subprocess.run(['c++', str(out / 'test.o'), str(out / 'bridge.o'), str(out / 'guard.o'), '-ldl', '-pthread', '-o', str(out / 'test')], check=True)
            subprocess.run([str(out / 'test')], check=True, timeout=10)

    def test_provider_rejects_tampered_image_stub_and_unwind(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_player_cursor_preload_pin.c'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'pin'
            subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(binary)], check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_active_only_wire_staging(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_stock_keyboard_cursor.c'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'cursor-queue'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(source), '-pthread', '-lm', '-o', str(binary)], check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_cursor_and_view_prepare_before_publication(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_player_input_transaction.c'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'input-transaction'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(binary)], check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_gate_applies_both_slots_in_authenticated_scopes(self):
        root = Path(__file__).resolve().parents[1]
        source = root / 'src/native/engine/linux/test_gate_player_cursor_replay.c'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'gate-cursor'
            subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(source), '-pthread', '-lm', '-o', str(binary)], check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_mouse_observer_gate_and_admission(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            for name in ('test_gate_player_cursor_input_trace', 'test_gate_player_cursor_trace_request'):
                with self.subTest(fixture=name):
                    binary = Path(directory) / name
                    subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-unused-function',
                                    str(root / 'src/native/engine/linux' / (name + '.c')),
                                    '-o', str(binary)], check=True, timeout=30)
                    subprocess.run([str(binary)], check=True, timeout=10)

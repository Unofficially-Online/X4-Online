"""Dormant XS semantics and exact Linux ELF pins, with no native launch."""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EXECUTABLE = Path.home() / '.local/share/Steam/steamapps/common/X4 Foundations/X4'
ELF = Path(os.environ.get('X4_LINUX_EXE', DEFAULT_EXECUTABLE))


class ViewXsPolicyTest(unittest.TestCase):
    def test_semantics_and_preflight_refusal(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'view-xs'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'src/native/engine/linux/test_player_view_xs_policy.c'),
                            '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
            preload = Path(directory) / 'view-xs-preload'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'src/native/engine/linux/test_player_view_xs_preload.c'),
                            '-o', str(preload)], check=True)
            subprocess.run([str(preload)], check=True)
            pin = Path(directory) / 'view-xs-pin'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'src/native/engine/linux/test_player_view_xs_preload_pin.c'),
                            '-o', str(pin)], check=True)
            subprocess.run([str(pin)], check=True)
            adapter = Path(directory) / 'view-xs-adapter'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'src/native/engine'),
                            str(ROOT / 'src/native/engine/linux/test_gate_player_view_xs_adapter.c'),
                            '-o', str(adapter)], check=True)
            subprocess.run([str(adapter)], check=True)
            config = Path(directory) / 'view-rewrite-config'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'src/native/engine/linux/test_player_view_rewrite_config.c'),
                            '-o', str(config)], check=True)
            subprocess.run([str(config)], check=True)

    @unittest.skipUnless(ELF.is_file(), 'pinned local X4 executable unavailable')
    def test_elf_site_hashes(self):
        data = ELF.read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(),
                         '113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20')
        pins = ((0x129a680, 0x111, 'e491a0bd3d0a73760d66ada4215e20b07b4af0dc6b894d232dfba2472bb9142b'),
                (0x1c90380, 0x51, '47a88f24f0a75dc1801d682abe6c5d83b1a6c52f2d125428bb1d8e17b83b4c00'),
                (0x1e07020, 0x33, '7fbd4e7bdb604524025c4a46cc791ae7137b7c1f19270213781e35e5b7890f51'),
                (0x1dccd30, 0x34, '6398d63d2afe8dbdfcc384682ea75da888787d96b67df91fba9519d42bec94d0'),
                (0x1c98690, 0x1f8, '598e8e3b893cd530a4c413082a2bcaee43c3bd0ec3e728b4afddb2ec97eaba4b'))
        self.assertEqual(data[:6], b'\x7fELF\x02\x01')
        program_offset = struct.unpack_from('<Q', data, 32)[0]
        entry_size, entry_count = struct.unpack_from('<HH', data, 54)
        segments = []
        for index in range(entry_count):
            offset = program_offset + index * entry_size
            kind, _, file_offset, virtual, _, file_size, _, _ = struct.unpack_from('<IIQQQQQQ', data, offset)
            if kind == 1:
                segments.append((virtual, file_offset, file_size))

        def at(virtual, length):
            matches = [file_offset + virtual - base for base, file_offset, size in segments
                       if base <= virtual and virtual + length <= base + size]
            self.assertEqual(len(matches), 1, f'address {virtual:#x} has no unique file-backed PT_LOAD')
            return data[matches[0]:matches[0] + length]

        for address, length, expected in pins:
            with self.subTest(address=hex(address)):
                self.assertEqual(hashlib.sha256(at(address, length)).hexdigest(), expected)
        self.assertEqual(int.from_bytes(at(0x3d29e98, 8), 'little'), 0x129a680)
        for site, target in ((0x129a6be, 0x1c90380), (0x129a75c, 0x1e07020),
                             (0x129a769, 0x1dccd30), (0x129a779, 0x1c98690)):
            instruction = at(site, 5)
            self.assertEqual(instruction[0], 0xe8)
            relative = int.from_bytes(instruction[1:], 'little', signed=True)
            self.assertEqual(site + 5 + relative, target)
        self.assertEqual(at(0x2d25b20, 4), bytes.fromhex('0024744a'))

"""Synthetic ABI and fixed-address installer checks; never launches X4."""
import hashlib
import os
import struct
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / 'src/native/engine/linux'


class ViewAncestryPreloadTests(unittest.TestCase):
    def test_preload_fixtures(self):
        for source in ('test_player_view_ancestry_preload.c', 'test_player_view_ancestry_install.c',
                       'test_player_view_ancestry_preload_pin.c', 'test_gate_player_view_ancestry.c'):
            with self.subTest(source=source), tempfile.TemporaryDirectory() as directory:
                executable = Path(directory) / 'ancestry-fixture'
                subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                                '-Werror', '-pthread', str(NATIVE / source), '-o', str(executable)],
                               check=True)
                subprocess.run([str(executable)], check=True)

    def test_optional_pinned_elf_functions(self):
        default = Path.home() / '.local/share/Steam/steamapps/common/X4 Foundations/X4'
        executable = Path(os.environ.get('X4_LINUX_EXE', default))
        if not executable.is_file():
            self.skipTest('pinned Linux X4 ELF is unavailable')
        image = executable.read_bytes()
        self.assertEqual(len(image), 60459280)
        self.assertEqual(hashlib.sha256(image).hexdigest(),
                         '113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20')
        program_offset = struct.unpack_from('<Q', image, 32)[0]
        entry_size, entry_count = struct.unpack_from('<HH', image, 54)
        segments = [struct.unpack_from('<IIQQQQQQ', image, program_offset + i * entry_size)
                    for i in range(entry_count)]
        pins = (
            (0x11132d0, 133, '48dd904179d64a8b5f75a87b5b989a3f7c215342c71ac5132f07b7b86b8d3b58'),
            (0x1113360, 76, '5798f27c36fee251792a259c2e5b74ba1e647907ea96fb177bcccb73b8535fdc'),
            (0x1d6b1c0, 99, '28662e2439a706b8794ec0c9dbe9b2b31aef33695461446cfbba6466a58967f6'),
            (0x177f900, 224, 'd46a2e354ebd8f76037356afe53a778117f45401b2dfd3c64727ae42e8dfd0ef'),
            (0x1e07020, 51, '7fbd4e7bdb604524025c4a46cc791ae7137b7c1f19270213781e35e5b7890f51'),
            (0x129bad0, 456, 'cff3c5caae429ad58f93402f0d0b754c82d467823d8298c3641b1459a1605e50'),
        )
        for address, length, expected in pins:
            offsets = [offset + address - virtual for kind, flags, offset, virtual, _, size, _, _ in segments
                       if kind == 1 and flags & 1 and virtual <= address and address + length <= virtual + size]
            self.assertEqual(len(offsets), 1)
            self.assertEqual(hashlib.sha256(image[offsets[0]:offsets[0] + length]).hexdigest(), expected)


if __name__ == '__main__':
    unittest.main()

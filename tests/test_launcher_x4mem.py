"""lockstep.launcher.x4mem: a launcher's own X4 told a session's seed while it runs (GameClient+0x218 and its text at
+0x220, as -seed: sets them; reports/INPLACE-STUDY-20261126.txt section 3), on a fake process memory and a small fake
X4.exe: written only for the pinned build (its headers and pinned code equal the file's), read back, refused otherwise.
No X4 runs here (a real X4's write is a live check)."""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import x4mem  # noqa: E402

BASE = 0x140000000
STAMP, SIZE = 0x6a1eaded, 0x20000
CODE_RVA, CODE = 0x1100, bytes(range(64))       # stands in for the pinned code ranges
CLIENT_RVA = 0x1800                              # stands in for RVA_GAMECLIENT
CLIENT = 0x50000000                              # the GameClient object (heap)


def fake_exe():
    """A PE file: MZ, PE header (stamp, SizeOfImage), one section mapping RVA 0x1000.. to file offset 0x400.."""
    data = bytearray(0x2000)
    data[:2] = b'MZ'
    nt = 0x80
    data[0x3c:0x40] = nt.to_bytes(4, 'little')
    data[nt:nt + 4] = b'PE\0\0'
    data[nt + 6:nt + 8] = (1).to_bytes(2, 'little')
    data[nt + 8:nt + 12] = STAMP.to_bytes(4, 'little')
    data[nt + 20:nt + 22] = (0xF0).to_bytes(2, 'little')
    data[nt + 24 + 56:nt + 24 + 60] = SIZE.to_bytes(4, 'little')
    table = nt + 24 + 0xF0
    section = (b'.text\0\0\0' + (0x1000).to_bytes(4, 'little') + (0x1000).to_bytes(4, 'little')
               + (0x1000).to_bytes(4, 'little') + (0x400).to_bytes(4, 'little'))
    data[table:table + len(section)] = section
    data[0x400 + CODE_RVA - 0x1000:0x400 + CODE_RVA - 0x1000 + len(CODE)] = CODE
    return bytes(data)


class FakeMemory:
    """The running image as the file maps it, and the GameClient on the heap with -seed:276944566's fields."""

    def __init__(self, exe, code=CODE, seed_text=b'276944566', capacity=15):
        self.base = BASE
        self.regions = {}
        image = bytearray(0x2000)
        image[:0x400] = exe[:0x400]
        image[0x1000:0x2000] = exe[0x400:0x1400]
        image[CODE_RVA:CODE_RVA + len(code)] = code
        image[CLIENT_RVA:CLIENT_RVA + 8] = CLIENT.to_bytes(8, 'little')
        self.regions[BASE] = image
        client = bytearray(0x300)
        client[0x218:0x220] = int(seed_text or b'0').to_bytes(8, 'little')
        if capacity <= 15:   # the short string's buffer is in the object
            client[0x220:0x220 + len(seed_text)] = seed_text
        else:
            client[0x220:0x228] = (0x60000000).to_bytes(8, 'little')
            self.regions[0x60000000] = bytearray(seed_text + b'\0' * (capacity + 1 - len(seed_text)))
        client[0x230:0x238] = len(seed_text).to_bytes(8, 'little')
        client[0x238:0x240] = capacity.to_bytes(8, 'little')
        self.regions[CLIENT] = client
        self.writes = []
        self.closed = False

    def find(self, address, length):
        for start, data in self.regions.items():
            if start <= address and address + length <= start + len(data):
                return data, address - start
        raise x4mem.Refused(f'unmapped {address:#x}')

    def read(self, address, length):
        data, offset = self.find(address, length)
        return bytes(data[offset:offset + length])

    def write(self, address, value):
        data, offset = self.find(address, len(value))
        data[offset:offset + len(value)] = value
        self.writes.append(address)

    def close(self):
        self.closed = True


class ForceSeed(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.exe = Path(self.tmp.name) / 'X4.exe'
        self.exe.write_bytes(fake_exe())
        patches = [mock.patch.object(x4mem, 'SEED_PINS', ((CODE_RVA, len(CODE)),)),
                   mock.patch.object(x4mem, 'RVA_GAMECLIENT', CLIENT_RVA)]
        for patch in patches:
            patch.start()
            self.addCleanup(patch.stop)

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_pinned_build_takes_the_seed_as_minus_seed_gives_it(self):
        memory = FakeMemory(self.exe.read_bytes())
        self.assertEqual(x4mem.seed_of(memory), (276944566, '276944566'))
        logs = []
        x4mem.force_seed(4242, self.exe, 12345, memory=memory, log=logs.append)
        self.assertEqual(x4mem.seed_of(memory), (12345, '12345'))
        self.assertTrue(memory.closed)
        self.assertIn('takes the seed 12345', logs[0])
        long_string = FakeMemory(self.exe.read_bytes(), seed_text=b'276944566', capacity=31)   # the text on the heap
        x4mem.force_seed(4242, self.exe, 4000000000, memory=long_string)
        self.assertEqual(x4mem.seed_of(long_string), (4000000000, '4000000000'))

    def test_anything_else_is_refused_before_a_write(self):
        other_code = FakeMemory(self.exe.read_bytes(), code=bytes(64))       # patched or another build's code
        with self.assertRaisesRegex(x4mem.Refused, 'not the pinned build'):
            x4mem.force_seed(1, self.exe, 12345, memory=other_code)
        other_stamp = FakeMemory(self.exe.read_bytes())
        other_stamp.regions[BASE][0x80 + 8] ^= 1
        with self.assertRaisesRegex(x4mem.Refused, 'another X4 build'):
            x4mem.force_seed(1, self.exe, 12345, memory=other_stamp)
        for memory in (other_code, other_stamp):
            self.assertEqual(memory.writes, [])
            self.assertEqual(x4mem.seed_of(memory)[0], 276944566)
        tight = FakeMemory(self.exe.read_bytes(), seed_text=b'1', capacity=3)
        with self.assertRaisesRegex(x4mem.Refused, 'does not fit'):
            x4mem.force_seed(1, self.exe, 12345, memory=tight)
        for seed in (0, 1 << 32, -5):
            with self.assertRaises(x4mem.Refused):
                x4mem.force_seed(1, self.exe, seed, memory=FakeMemory(self.exe.read_bytes()))

    def test_the_pe_layout(self):
        stamp, size, sections = x4mem.pe_layout(self.exe.read_bytes())
        self.assertEqual((stamp, size, sections), (STAMP, SIZE, [(0x1000, 0x1000, 0x400, 0x1000)]))
        self.assertEqual(x4mem.file_bytes(self.exe.read_bytes(), (stamp, size, sections), CODE_RVA, 4), CODE[:4])
        with self.assertRaises(x4mem.Refused):
            x4mem.file_bytes(self.exe.read_bytes(), (stamp, size, sections), 0x9000, 4)
        with self.assertRaises(x4mem.Refused):
            x4mem.pe_layout(b'not a PE file')


if __name__ == '__main__':
    unittest.main()

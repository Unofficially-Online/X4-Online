"""The X4 a launcher started itself (x4ls lobby's, in a disposable profile) told a session's seed while it runs, so that
it takes a session of another seed in place instead of starting again with another -seed: (netlobby.LobbyGame.take).

reports/INPLACE-STUDY-20261126.txt section 3 (X4-Multiplayer-inplace): -seed:<v> is parsed once into GameClient+0x218
(the number) and +0x220 (its text, a std::string); GameClass::Load and NewGame read them at every load and new game,
before the databases are seeded (GodDB, JobDB), and Load does not take the seed from the save. Written before the
session's load they give exactly what -seed: gives (measured there: g_SessionSeed 276944566 after such a write).

Only the pinned X4 build is written to: the running image's TimeDateStamp and SizeOfImage are the X4.exe file's, and the
code that reads the fields (GameClass::Load's forced-seed test, GetForcedOrRandomSessionSeed) is byte for byte the
file's. Anything else, and every failed read back, refuses (the caller starts X4 again instead). Never used on an X4
the launcher did not start (the player's own)."""
import ctypes
import sys
from pathlib import Path

RVA_GAMECLIENT = 0x6cf1520    # GameClient*: +0x218 the forced seed (-seed:), +0x220 its std::string
SEED_NUMBER, SEED_TEXT = 0x218, 0x220
STRING_SIZE, STRING_CAPACITY = 0x10, 0x18   # MSVC std::string: 16 bytes of buffer or pointer, then size, capacity
SEED_PINS = ((0x9ee850, 0x60),   # GameClass::Load: the forced-seed test before ImportDatabases
             (0x975c20, 0x55))   # GetForcedOrRandomSessionSeed
PROCESS_ACCESS = 0x0008 | 0x0010 | 0x0020 | 0x0400   # VM_OPERATION, VM_READ, VM_WRITE, QUERY_INFORMATION


class Refused(Exception):
    """The seed was not written: the text says why."""


def pe_layout(data):
    """(TimeDateStamp, SizeOfImage, [(virtual address, virtual size, raw offset, raw size)]) of a PE file's bytes."""
    if data[:2] != b'MZ':
        raise Refused('not a PE file')
    nt = int.from_bytes(data[0x3c:0x40], 'little')
    if data[nt:nt + 4] != b'PE\0\0':
        raise Refused('no PE header')
    sections = int.from_bytes(data[nt + 6:nt + 8], 'little')
    stamp = int.from_bytes(data[nt + 8:nt + 12], 'little')
    optional_size = int.from_bytes(data[nt + 20:nt + 22], 'little')
    size = int.from_bytes(data[nt + 24 + 56:nt + 24 + 60], 'little')
    table = nt + 24 + optional_size
    rows = []
    for index in range(sections):
        row = data[table + 40 * index:table + 40 * index + 40]
        rows.append((int.from_bytes(row[12:16], 'little'), int.from_bytes(row[8:12], 'little'),
                     int.from_bytes(row[20:24], 'little'), int.from_bytes(row[16:20], 'little')))
    return stamp, size, rows


def file_bytes(data, layout, rva, length):
    """The file's bytes at an RVA (a range inside one section's raw data)."""
    for address, virtual, raw, raw_size in layout[2]:
        if address <= rva and rva + length <= address + min(virtual, raw_size):
            offset = raw + rva - address
            return data[offset:offset + length]
    raise Refused(f'RVA {rva:#x} is in no section of the file')


class Memory:
    """Another process's memory (ReadProcessMemory / WriteProcessMemory), its image base the first module's."""

    def __init__(self, pid):
        if sys.platform != 'win32':
            raise Refused('Windows only')
        from ctypes import wintypes
        self.k32 = ctypes.WinDLL('kernel32', use_last_error=True)
        psapi = ctypes.WinDLL('psapi', use_last_error=True)
        self.k32.OpenProcess.restype = wintypes.HANDLE
        self.k32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                               ctypes.POINTER(ctypes.c_size_t)]
        self.k32.WriteProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                                ctypes.POINTER(ctypes.c_size_t)]
        self.k32.CloseHandle.argtypes = [wintypes.HANDLE]
        psapi.EnumProcessModulesEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                                               ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
        self.handle = self.k32.OpenProcess(PROCESS_ACCESS, False, int(pid))
        if not self.handle:
            raise Refused(f'X4 pid {pid} cannot be opened ({ctypes.get_last_error()})')
        modules = (ctypes.c_void_p * 8)()
        need = wintypes.DWORD()
        if not psapi.EnumProcessModulesEx(self.handle, modules, ctypes.sizeof(modules), ctypes.byref(need), 3) or not modules[0]:
            self.close()
            raise Refused(f'X4 pid {pid}: its modules cannot be listed ({ctypes.get_last_error()})')
        self.base = modules[0]

    def read(self, address, length):
        buffer = ctypes.create_string_buffer(length)
        got = ctypes.c_size_t()
        if not self.k32.ReadProcessMemory(self.handle, ctypes.c_void_p(address), buffer, length, ctypes.byref(got)) \
                or got.value != length:
            raise Refused(f'X4 memory at {address:#x} cannot be read')
        return buffer.raw

    def write(self, address, data):
        got = ctypes.c_size_t()
        if not self.k32.WriteProcessMemory(self.handle, ctypes.c_void_p(address), data, len(data), ctypes.byref(got)) \
                or got.value != len(data):
            raise Refused(f'X4 memory at {address:#x} cannot be written')

    def close(self):
        if self.handle:
            self.k32.CloseHandle(self.handle)
            self.handle = None


def quad(memory, address):
    return int.from_bytes(memory.read(address, 8), 'little')


def pinned(memory, exe_data, ranges=SEED_PINS):
    """The running image is the X4.exe file at its headers and at every pinned range (Refused otherwise)."""
    layout = pe_layout(exe_data)
    head = memory.read(memory.base, 0x400)
    nt = int.from_bytes(head[0x3c:0x40], 'little')
    stamp = int.from_bytes(head[nt + 8:nt + 12], 'little')
    size = int.from_bytes(head[nt + 24 + 56:nt + 24 + 60], 'little')
    if (stamp, size) != layout[:2]:
        raise Refused(f'another X4 build is running (stamp {stamp:#x}, size {size:#x})')
    for rva, length in ranges:
        if memory.read(memory.base + rva, length) != file_bytes(exe_data, layout, rva, length):
            raise Refused(f'X4 code at {rva:#x} is not the pinned build\'s')


def seed_of(memory):
    """(the forced seed number, its text) of the running X4."""
    client = quad(memory, memory.base + RVA_GAMECLIENT)
    if not client:
        raise Refused('X4 has no GameClient yet')
    number = quad(memory, client + SEED_NUMBER)
    raw = memory.read(client + SEED_TEXT, 0x20)
    size = int.from_bytes(raw[STRING_SIZE:STRING_SIZE + 8], 'little')
    capacity = int.from_bytes(raw[STRING_CAPACITY:STRING_CAPACITY + 8], 'little')
    pointer = int.from_bytes(raw[:8], 'little') if capacity > 15 else client + SEED_TEXT
    text = memory.read(pointer, size).decode('ascii', 'replace') if size else ''
    return number, text


def force_seed(pid, exe, seed, memory=None, log=None):
    """The running X4 (pid; exe: the X4.exe file it was started from) loads the next game with this seed, as if it had
    been started with -seed:<seed>. Raises Refused (nothing written, or the write undone where it can be) unless the
    build is the pinned one and the fields read back as written."""
    seed = int(seed)
    if not 0 < seed < (1 << 32):
        raise Refused(f'{seed} is not a seed X4 takes')
    text = str(seed).encode('ascii')
    memory = memory or Memory(pid)
    try:
        pinned(memory, Path(exe).read_bytes(), SEED_PINS)
        client = quad(memory, memory.base + RVA_GAMECLIENT)
        if not client:
            raise Refused('X4 has no GameClient yet')
        raw = memory.read(client + SEED_TEXT, 0x20)
        capacity = int.from_bytes(raw[STRING_CAPACITY:STRING_CAPACITY + 8], 'little')
        if capacity < len(text):
            raise Refused('the seed text does not fit X4\'s string')
        pointer = int.from_bytes(raw[:8], 'little') if capacity > 15 else client + SEED_TEXT
        memory.write(client + SEED_NUMBER, seed.to_bytes(8, 'little'))
        memory.write(pointer, text + b'\0')
        memory.write(client + SEED_TEXT + STRING_SIZE, len(text).to_bytes(8, 'little'))
        if seed_of(memory) != (seed, text.decode()):
            raise Refused('the seed did not read back as written')
        if log:
            log(f'X4 pid {pid}: its next load takes the seed {seed} (GameClient+{SEED_NUMBER:#x}, as -seed: gives)')
    finally:
        if hasattr(memory, 'close'):
            memory.close()

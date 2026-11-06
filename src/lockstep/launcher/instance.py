"""Let several X4 processes run at once.

X4 creates the named mutex EGOSOFT_X4_INSTANCE at startup and a second process shows
"Game is already running!" and exits. Once a launcher-started client is up, its handle
to that mutex is closed from outside (DuplicateHandle with DUPLICATE_CLOSE_SOURCE), so
the next client can create it. Only processes the launcher started are touched; no
file and no other handle changes.
"""
import ctypes
from ctypes import wintypes

MUTEX_NAME = 'EGOSOFT_X4_INSTANCE'
SystemExtendedHandleInformation = 64
ObjectNameInformation, ObjectTypeInformation = 1, 2
STATUS_INFO_LENGTH_MISMATCH = 0xC0000004
PROCESS_DUP_HANDLE = 0x0040
DUPLICATE_CLOSE_SOURCE, DUPLICATE_SAME_ACCESS = 0x1, 0x2


class HandleEntry(ctypes.Structure):
    _fields_ = [('Object', ctypes.c_void_p), ('UniqueProcessId', ctypes.c_size_t), ('HandleValue', ctypes.c_size_t),
                ('GrantedAccess', wintypes.ULONG), ('CreatorBackTraceIndex', wintypes.USHORT),
                ('ObjectTypeIndex', wintypes.USHORT), ('HandleAttributes', wintypes.ULONG), ('Reserved', wintypes.ULONG)]


class UnicodeString(ctypes.Structure):
    _fields_ = [('Length', wintypes.USHORT), ('MaximumLength', wintypes.USHORT), ('Buffer', wintypes.LPWSTR)]


def _api():
    ntdll, kernel32 = ctypes.WinDLL('ntdll'), ctypes.WinDLL('kernel32', use_last_error=True)
    ntdll.NtQuerySystemInformation.argtypes = [wintypes.ULONG, ctypes.c_void_p, wintypes.ULONG, ctypes.POINTER(wintypes.ULONG)]
    ntdll.NtQuerySystemInformation.restype = wintypes.LONG
    ntdll.NtQueryObject.argtypes = [wintypes.HANDLE, wintypes.ULONG, ctypes.c_void_p, wintypes.ULONG, ctypes.POINTER(wintypes.ULONG)]
    ntdll.NtQueryObject.restype = wintypes.LONG
    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.DuplicateHandle.argtypes = [wintypes.HANDLE, wintypes.HANDLE, wintypes.HANDLE, ctypes.POINTER(wintypes.HANDLE),
                                         wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.GetCurrentProcess.restype = wintypes.HANDLE
    return ntdll, kernel32


def _handles(ntdll, pid):
    size = 1 << 20
    while True:
        buf = ctypes.create_string_buffer(size)
        needed = wintypes.ULONG()
        status = ntdll.NtQuerySystemInformation(SystemExtendedHandleInformation, buf, size, ctypes.byref(needed))
        if status & 0xFFFFFFFF == STATUS_INFO_LENGTH_MISMATCH:
            size = max(size * 2, needed.value + 65536)
            continue
        if status != 0:
            raise OSError(f'NtQuerySystemInformation failed: {status & 0xFFFFFFFF:#x}')
        count = ctypes.c_size_t.from_buffer(buf).value
        entries = (HandleEntry * count).from_buffer(buf, 2 * ctypes.sizeof(ctypes.c_size_t))
        return [e.HandleValue for e in entries if e.UniqueProcessId == pid]


def _query(ntdll, handle, kind):
    buf = ctypes.create_string_buffer(4096)
    if ntdll.NtQueryObject(handle, kind, buf, len(buf), None) != 0:
        return ''
    text = UnicodeString.from_buffer(buf)
    return ctypes.wstring_at(text.Buffer, text.Length // 2) if text.Buffer else ''


def release_instance_mutex(pid):
    """Close X4's EGOSOFT_X4_INSTANCE handle(s) in process `pid`; returns how many were closed."""
    ntdll, kernel32 = _api()
    process = kernel32.OpenProcess(PROCESS_DUP_HANDLE, False, pid)
    if not process:
        raise OSError(f'cannot open process {pid}: {ctypes.get_last_error()}')
    me, closed = kernel32.GetCurrentProcess(), 0
    try:
        for value in _handles(ntdll, pid):
            local = wintypes.HANDLE()
            if not kernel32.DuplicateHandle(process, value, me, ctypes.byref(local), 0, False, DUPLICATE_SAME_ACCESS):
                continue
            try:
                # Name queries can block on some object types (pipes); only mutants are asked for a name.
                if _query(ntdll, local, ObjectTypeInformation) != 'Mutant':
                    continue
                if not _query(ntdll, local, ObjectNameInformation).endswith('\\' + MUTEX_NAME):
                    continue
            finally:
                kernel32.CloseHandle(local)
            if kernel32.DuplicateHandle(process, value, None, None, 0, False, DUPLICATE_CLOSE_SOURCE):
                closed += 1
    finally:
        kernel32.CloseHandle(process)
    return closed

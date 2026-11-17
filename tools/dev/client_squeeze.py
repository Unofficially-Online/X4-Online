"""client_squeeze.py PROFILE [--cap MB] [--stall MS] [--every MS] [--which any|main|others] [--after S] [--seconds S]

Puts ONE X4 test client under the kind of pressure a PC short of memory gives it, without starving the PC: a check
that a session stays in sync when one client is slow in odd places (docs/PLAYTEST.md, 17 Nov).

  --cap MB     a hard maximum working set for that process (SetProcessWorkingSetSizeEx): it pages against its own
               limit, other processes keep their memory
  --stall MS   every --every milliseconds one of its threads is suspended for up to MS milliseconds (a thread that
               waits for a page stands still like that); --which picks the oldest thread (X4's main thread), the
               others, or any

The client is found by its -personalfolderid (the launcher's disposable profiles; a normal game has none and cannot be
named). Waits for the process, then --after seconds (the load), runs for --seconds or until the process ends, and
lifts the cap. Windows only. Too hard a squeeze stops the client's gate (fail closed), which is not a desync."""
import argparse
import ctypes
import ctypes.wintypes as w
import random
import subprocess
import sys
import time

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
PROCESS_SET_QUOTA, PROCESS_QUERY_INFORMATION, SYNCHRONIZE = 0x0100, 0x0400, 0x00100000
THREAD_SUSPEND_RESUME = 0x0002
QUOTA_LIMITS_HARDWS_MIN_DISABLE, QUOTA_LIMITS_HARDWS_MAX_ENABLE, QUOTA_LIMITS_HARDWS_MAX_DISABLE = 0x2, 0x4, 0x8
TH32CS_SNAPTHREAD = 0x4


class THREADENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', w.DWORD), ('cntUsage', w.DWORD), ('th32ThreadID', w.DWORD), ('th32OwnerProcessID', w.DWORD),
                ('tpBasePri', w.LONG), ('tpDeltaPri', w.LONG), ('dwFlags', w.DWORD)]


k32.OpenProcess.restype = w.HANDLE
k32.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k32.OpenThread.restype = w.HANDLE
k32.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k32.SetProcessWorkingSetSizeEx.argtypes = [w.HANDLE, ctypes.c_size_t, ctypes.c_size_t, w.DWORD]
k32.CreateToolhelp32Snapshot.restype = w.HANDLE
k32.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
k32.Thread32First.argtypes = [w.HANDLE, ctypes.POINTER(THREADENTRY32)]
k32.Thread32Next.argtypes = [w.HANDLE, ctypes.POINTER(THREADENTRY32)]
k32.SuspendThread.argtypes = [w.HANDLE]
k32.ResumeThread.argtypes = [w.HANDLE]
k32.CloseHandle.argtypes = [w.HANDLE]
k32.GetThreadTimes.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
k32.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]


def find(profile):
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
                          "Get-CimInstance Win32_Process -Filter \"Name='X4.exe'\" | ForEach-Object { '' + $_.ProcessId + ' ' + $_.CommandLine }"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        pid, _, command = line.partition(' ')
        if ('-personalfolderid %d' % profile) in command or ('-personalfolderid  %d' % profile) in command:
            return int(pid)
    return None


def threads(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    entry = THREADENTRY32()
    entry.dwSize = ctypes.sizeof(entry)
    out = []
    ok = k32.Thread32First(snap, ctypes.byref(entry))
    while ok:
        if entry.th32OwnerProcessID == pid:
            out.append(entry.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(entry))
    k32.CloseHandle(snap)
    return out


def oldest(ids):
    """The thread made first: X4's main thread."""
    best, when = None, None
    for t in ids:
        h = k32.OpenThread(0x0800, False, t)     # THREAD_QUERY_LIMITED_INFORMATION
        if not h:
            continue
        times = (w.FILETIME * 4)()
        if k32.GetThreadTimes(h, ctypes.byref(times[0]), ctypes.byref(times[1]), ctypes.byref(times[2]), ctypes.byref(times[3])):
            made = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
            if when is None or made < when:
                best, when = t, made
        k32.CloseHandle(h)
    return best


def main():
    p = argparse.ArgumentParser()
    p.add_argument('profile', type=int)
    p.add_argument('--cap', type=int, default=0)
    p.add_argument('--stall', type=int, default=0)
    p.add_argument('--every', type=int, default=200)
    p.add_argument('--after', type=float, default=150.0)
    p.add_argument('--seconds', type=float, default=400.0)
    p.add_argument('--seed', type=int, default=1)
    p.add_argument('--which', choices=('any', 'main', 'others'), default='any', help='which threads stall: the oldest (the main thread), the others, any')
    a = p.parse_args()
    if a.profile < 45800:
        sys.exit('a disposable test profile (45800 and up), not a player profile')
    pid = None
    for _ in range(600):
        pid = find(a.profile)
        if pid:
            break
        time.sleep(1)
    if not pid:
        sys.exit('no client of profile %d' % a.profile)
    print('client pid', pid, flush=True)
    time.sleep(a.after)
    process = k32.OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION | SYNCHRONIZE, False, pid)
    if not process:
        sys.exit('open failed %d' % ctypes.get_last_error())
    if a.cap:
        ok = k32.SetProcessWorkingSetSizeEx(process, 64 << 20, a.cap << 20, QUOTA_LIMITS_HARDWS_MIN_DISABLE | QUOTA_LIMITS_HARDWS_MAX_ENABLE)
        print('cap %d MB: %s' % (a.cap, 'on' if ok else 'failed %d' % ctypes.get_last_error()), flush=True)
    rng = random.Random(a.seed)
    stalls = 0
    first = None
    end = time.time() + a.seconds
    while time.time() < end and k32.WaitForSingleObject(process, 0) != 0:
        if not a.stall:
            time.sleep(1)
            continue
        ids = threads(pid)
        if not ids:
            break
        if first is None:
            first = oldest(ids)
        others = [first] if a.which == 'main' else [t for t in ids if t != first] if a.which == 'others' else ids
        if others:
            handle = k32.OpenThread(THREAD_SUSPEND_RESUME, False, rng.choice(others))
            if handle:
                if k32.SuspendThread(handle) != 0xFFFFFFFF:
                    time.sleep(rng.uniform(0.2, 1.0) * a.stall / 1000.0)
                    k32.ResumeThread(handle)
                    stalls += 1
                k32.CloseHandle(handle)
        time.sleep(rng.uniform(0.5, 1.5) * a.every / 1000.0)
    if a.cap and k32.WaitForSingleObject(process, 0) != 0:
        k32.SetProcessWorkingSetSizeEx(process, ctypes.c_size_t(-1).value, ctypes.c_size_t(-1).value, QUOTA_LIMITS_HARDWS_MAX_DISABLE)
        print('cap lifted', flush=True)
    print('stalls', stalls, flush=True)
    k32.CloseHandle(process)


if __name__ == '__main__':
    main()

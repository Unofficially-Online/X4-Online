"""In-place feasibility prototype (reports/INPLACE-STUDY-20261126.txt): can a running X4 that was started like the
Steam one (its own profile, no -seed, protected UI mode on, only a menu extension besides the DLCs) take a session
without a restart?

One X4 in its own lane view (C:/x4ls-lanes-inplace/c1), below normal priority, on display 3. Profile 45855 stands in for
the player's own profile, 45856 for the session's disposable profile (X4LS_FIRST_PROFILE=4, lock C:/x4ls-locks/x4-a).
This driver stands in for the gate's in-process steps from outside (ReadProcessMemory/WriteProcessMemory on its own
test X4 only, after the pinned-build checks; one byte of the personal folder string, the forced-seed field), and for
the helper (files copied into the extension, a DLL injected with CreateRemoteThread). The extension's Lua
(ext/ui/proto.lua) runs in protected UI mode and takes commands through loadfile.

Steps: start (the extension holds one placeholder patch, libraries/wares.xml = <diff/>) -> info (package?) -> control
load of an "own" save -> copy the session's md/ (a new script, a patch of stock md/notifications.xml that was not
there at the start), aiscripts/ (a new script) and libraries/ (the placeholder's real content) into the running extension -> audit the own profile -> redirect + seed -> info -> load the session save from the
disposable profile -> markers (MD, AI, library ware, g_SessionSeed) -> save -> config write -> inject a DLL -> quit ->
audit again. Prints a JSON summary. Usage: py -3.12 tools/dev/inplace_proto/proto.py --save SAVE.xml.gz
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import game, instance, profile, session, steam, steamplay  # noqa: E402

LANES = Path('C:/x4ls-lanes-inplace')
VIEW = LANES / 'c1'
EXT_ID = 'x4inplace_proto'
SLOTS = {'x4-a': (45855, 45856), 'x4-b': (45857, 45858)}   # C:/x4ls-locks/PROFILES.txt: one X4 per slot
OWN, SESSION_PROFILE = SLOTS['x4-a']
LOG = 'x4ls-c1.log'
SEED = 276944566
# X4 9.00 RVAs (x4-re labels; see the study notes)
RVA_FILEIO = 0x6cf1558        # FileIO*; +0x28 std::string personal folder (UTF-8, trailing backslash)
RVA_GAMECLIENT = 0x6cf1520    # GameClient*; +0x218 forced seed (-seed:), +0x220 its std::string
RVA_SESSION_SEED = 0x3d19860  # g_SessionSeed
PINNED_RANGES = ((0x23e290, 0x9a),   # GetSaveFolderPath: [FileIO]+0x28 + "save\" at every call
                 (0x9ee850, 0x60),   # GameClass::Load: the forced-seed test before ImportDatabases
                 (0x975c20, 0x55),   # GetForcedOrRandomSessionSeed
                 (0x14cc360, 0x60),  # XLib::RemoveFile: base[personal] = FileIO+8+0x20*personal
                 (0xaf8060, 0x50))   # a UI state's "uisafemode -> package = nil" branch (je at 0xaf8089)
RVA_PACKAGE_BRANCH = 0xaf8089   # 74 1f: je past the package removal; eb 1f keeps package for UI states made meanwhile
PROCESS_ALL = 0x0002 | 0x0008 | 0x0010 | 0x0020 | 0x0400   # CREATE_THREAD, VM_OPERATION, VM_READ, VM_WRITE, QUERY_INFORMATION
BELOW_NORMAL = 0x00004000

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
psapi = ctypes.WinDLL('psapi', use_last_error=True)
k32.OpenProcess.restype = wintypes.HANDLE
k32.VirtualAllocEx.restype = ctypes.c_void_p
k32.VirtualAllocEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
k32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.WriteProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.CreateRemoteThread.restype = wintypes.HANDLE
k32.CreateRemoteThread.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p]
psapi.EnumProcessModulesEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
psapi.GetModuleFileNameExW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_wchar_p, wintypes.DWORD]
k32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
k32.GetExitCodeThread.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
k32.CloseHandle.argtypes = [wintypes.HANDLE]
k32.VirtualProtectEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
k32.FlushInstructionCache.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t]
k32.GetModuleHandleW.restype = wintypes.HMODULE
k32.GetProcAddress.restype = ctypes.c_void_p
k32.GetProcAddress.argtypes = [wintypes.HMODULE, ctypes.c_char_p]


class Proc:
    def __init__(self, pid):
        self.pid = pid
        self.h = k32.OpenProcess(PROCESS_ALL, False, pid)
        if not self.h:
            raise SystemExit(f'OpenProcess {pid}: {ctypes.get_last_error()}')
        self.base = self.modules()[0][0]

    def modules(self):
        arr = (ctypes.c_void_p * 1024)()
        need = wintypes.DWORD()
        if not psapi.EnumProcessModulesEx(self.h, arr, ctypes.sizeof(arr), ctypes.byref(need), 3):
            raise SystemExit(f'EnumProcessModulesEx: {ctypes.get_last_error()}')
        out = []
        for i in range(need.value // ctypes.sizeof(ctypes.c_void_p)):
            name = ctypes.create_unicode_buffer(520)
            psapi.GetModuleFileNameExW(self.h, ctypes.c_void_p(arr[i]), name, 520)
            out.append((arr[i] or 0, name.value))
        return out

    def read(self, addr, n):
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t()
        if not k32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)) or got.value != n:
            raise SystemExit(f'ReadProcessMemory {addr:#x}: {ctypes.get_last_error()}')
        return buf.raw

    def write(self, addr, data):
        got = ctypes.c_size_t()
        if not k32.WriteProcessMemory(self.h, ctypes.c_void_p(addr), data, len(data), ctypes.byref(got)) or got.value != len(data):
            raise SystemExit(f'WriteProcessMemory {addr:#x}: {ctypes.get_last_error()}')

    def q(self, addr):
        return int.from_bytes(self.read(addr, 8), 'little')

    def string(self, addr):
        raw = self.read(addr, 32)
        size, cap = int.from_bytes(raw[16:24], 'little'), int.from_bytes(raw[24:32], 'little')
        ptr = int.from_bytes(raw[:8], 'little') if cap > 15 else addr
        return ptr, size, cap, self.read(ptr, size) if size else b''

    def patch(self, rva, expected, value):
        """Compare-and-write a few code bytes (fails closed when they are not the expected ones)."""
        addr = self.base + rva
        if self.read(addr, len(expected)) != expected:
            return False
        old = wintypes.DWORD()
        k32.VirtualProtectEx(self.h, ctypes.c_void_p(addr), len(value), 0x40, ctypes.byref(old))
        self.write(addr, value)
        k32.VirtualProtectEx(self.h, ctypes.c_void_p(addr), len(value), old.value, ctypes.byref(old))
        k32.FlushInstructionCache(self.h, ctypes.c_void_p(addr), len(value))
        return self.read(addr, len(value)) == value

    def inject(self, dll):
        path = (str(dll) + '\0').encode('utf-16-le')
        remote = k32.VirtualAllocEx(self.h, None, len(path), 0x3000, 0x04)
        self.write(remote, path)
        load = k32.GetProcAddress(k32.GetModuleHandleW('kernel32.dll'), b'LoadLibraryW')
        thread = k32.CreateRemoteThread(self.h, None, 0, load, remote, 0, None)
        if not thread:
            return f'CreateRemoteThread failed {ctypes.get_last_error()}'
        k32.WaitForSingleObject(thread, 30000)
        code = wintypes.DWORD()
        k32.GetExitCodeThread(thread, ctypes.byref(code))
        k32.CloseHandle(thread)
        loaded = [name for _, name in self.modules() if Path(name).name.lower() == Path(dll).name.lower()]
        return f'LoadLibraryW returned {code.value:#x}; module list: {loaded}'


def pinned_ok(proc, exe):
    """The running image is the pinned build at every byte range the prototype relies on (fail closed otherwise)."""
    import pefile
    pe = pefile.PE(str(exe), fast_load=True)
    head = proc.read(proc.base, 0x400)
    nt = int.from_bytes(head[0x3c:0x40], 'little')
    stamp = int.from_bytes(head[nt + 8:nt + 12], 'little')
    size = int.from_bytes(head[nt + 24 + 56:nt + 24 + 60], 'little')
    if stamp != pe.FILE_HEADER.TimeDateStamp or size != pe.OPTIONAL_HEADER.SizeOfImage:
        return False, f'header differs: stamp {stamp:#x} size {size:#x}'
    data = open(exe, 'rb').read()
    for rva, n in PINNED_RANGES:
        off = pe.get_offset_from_rva(rva)
        if proc.read(proc.base + rva, n) != data[off:off + n]:
            return False, f'bytes differ at {rva:#x}'
    return True, f'stamp {stamp:#x}, size {size:#x}, {len(PINNED_RANGES)} ranges equal'


def tree_state(folder):
    out = {}
    for path in sorted(Path(folder).rglob('*')) if Path(folder).is_dir() else []:
        if path.is_file():
            st = path.stat()
            out[path.relative_to(folder).as_posix()] = (st.st_size, st.st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest()[:16])
    return out


def diff_trees(before, after):
    return {'added': sorted(set(after) - set(before)), 'removed': sorted(set(before) - set(after)),
            'changed': sorted(k for k in set(before) & set(after) if before[k] != after[k])}


class Log:
    def __init__(self, path):
        self.path, self.offset, self.rows = Path(path), 0, []

    def poll(self):
        try:
            with self.path.open('rb') as f:
                f.seek(self.offset)
                data = f.read()
        except OSError:
            return
        cut = data.rfind(b'\n') + 1
        self.offset += cut
        for line in data[:cut].decode('utf-8', 'replace').splitlines():
            low = line.lower()
            if 'X4INPLACE' in line or ('rror' in line and ('x4inplace' in low or 'wares' in low or 'notifications' in low)):
                self.rows.append(line.strip())
                print('  log:', line.strip()[:220], flush=True)

    def wait(self, pattern, timeout, proc=None, start=None):
        end = time.time() + timeout
        rx = re.compile(pattern)
        seen = len(self.rows) if start is None else start
        while time.time() < end:
            self.poll()
            for row in self.rows[seen:]:
                if rx.search(row):
                    return row
            seen = len(self.rows)
            if proc is not None and proc.poll() is not None:
                return None
            time.sleep(0.5)
        return None


class Commands:
    def __init__(self, file, log):
        self.file, self.log, self.id = Path(file), log, 0

    def write(self, op, arg=''):
        self.id += 1
        text = f"return {{id = {self.id}, op = '{op}', arg = '{arg}'}}\n"
        part = self.file.with_suffix('.part')
        part.write_text(text)
        part.replace(self.file)
        return self.id

    def run(self, op, arg='', result=None, timeout=60, proc=None):
        start = len(self.log.rows)   # the ack and the result can come in one poll
        n = self.write(op, arg)
        ack = self.log.wait(rf'CMD {n} {op}', 60, proc, start)
        self.write('noop')   # a new UI state after a load must not run it again
        if ack is None:
            return None
        return self.log.wait(result or rf'RES {n} ', timeout, proc, start)


def refused_start(pid):
    """True when X4 `pid` shows its message box (class #32770) instead of starting: another X4 holds the mutex."""
    user32 = ctypes.windll.user32
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(hwnd, name, 64)
        if owner.value == pid and name.value == '#32770' and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True
    user32.EnumWindows(visit, 0)
    return bool(found)


def own_lanes_x4():
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
                          "Get-CimInstance Win32_Process -Filter \"Name='X4.exe'\" | ForEach-Object { '{0}|{1}' -f $_.ProcessId, $_.ExecutablePath }"],
                         capture_output=True, text=True).stdout
    return [int(pid) for pid, _, path in (row.partition('|') for row in out.splitlines()) if path.lower().startswith(str(LANES).lower().replace('/', '\\'))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True, help='a session save (never the player\'s own)')
    ap.add_argument('--display', type=int, default=3)
    ap.add_argument('--out', default=str(ROOT / '.local/inplace-run'))
    ap.add_argument('--lock', default='x4-a', choices=sorted(SLOTS))
    ap.add_argument('--quick', action='store_true',
                    help='no control load; package kept for the session UI state by a 2-byte code patch')
    ap.add_argument('--gate-dll', help="an x4lan_gate_64.dll to load with package.loadlib in the session's UI state")
    o = ap.parse_args()
    global OWN, SESSION_PROFILE
    OWN, SESSION_PROFILE = SLOTS[o.lock]
    out = Path(o.out)
    out.mkdir(parents=True, exist_ok=True)
    summary = {}
    if not Path(f'C:/x4ls-locks/{o.lock}/owner.txt').read_text(encoding='utf-8-sig').startswith('X4-Multiplayer-inplace'):
        raise SystemExit(f'lock {o.lock} is not ours')
    if Path('C:/x4ls-locks/USER-PLAYING.txt').exists():
        raise SystemExit('the user is playing: no X4')
    if shutil.disk_usage('C:/').free < 8 * 2**30:
        raise SystemExit('less than 8 GB free')
    ok, why = steam.status()
    if not ok:
        raise SystemExit(why)
    install = game.find_install()
    game.verify_exe(install)
    if VIEW.exists():
        game.remove_view(VIEW)
    LANES.mkdir(parents=True, exist_ok=True)
    game.create_view(install, VIEW)
    ext = VIEW / 'extensions' / EXT_ID
    shutil.copytree(HERE / 'ext', ext)
    # the stand-in "own" profile: windowed, protected UI mode as X4's default (no uisafemode node), DLCs + the menu
    own = profile.prepare(OWN, [*game.dlc_ids(VIEW), EXT_ID], 1280, 720)
    (own / 'config.xml').write_text(profile.config_xml(1280, 720).replace('<uisafemode>false</uisafemode>', ''))
    shutil.copyfile(o.save, own / 'save' / 'save_008.xml.gz')
    # the session's disposable profile: only the staged save, so that every other file there is X4's own write
    disposable = profile.profile_dir(SESSION_PROFILE)
    profile.refuse_in_use(SESSION_PROFILE)
    if disposable.exists():
        profile.remove_tree(disposable)
    (disposable / 'save').mkdir(parents=True)
    shutil.copyfile(o.save, disposable / 'save' / 'save_009.xml.gz')
    log = Log(own / LOG)
    cmds = Commands(ext / 'cmd.lua', log)
    cmds.write('noop')
    # X4 starts only while no other X4 holds its instance mutex (the launchers free theirs at the start menu)
    end = time.time() + 600
    while steamplay.instance_mutex_exists():
        if time.time() > end:
            cleanup(own, disposable)
            raise SystemExit('another X4 holds the instance mutex for 10 minutes')
        time.sleep(5)
    command = [str(VIEW / 'X4.exe'), '-personalfolderid', str(OWN), '-skipintro', '-noworkshopsync', '-logfile', LOG]
    summary['command'] = command[1:]
    x4 = subprocess.Popen(command, cwd=VIEW, env=steam.client_environment(), creationflags=BELOW_NORMAL,
                          startupinfo=session.no_activate_startup())
    print('X4 pid', x4.pid, flush=True)
    import threading
    placing = threading.Event()

    def place():   # on display 3, and the keyboard focus back to whatever the person works in
        while not placing.is_set():
            try:
                session.tile([x4.pid], 1280, 720, 2, o.display)
                session.yield_focus({x4.pid})
            except Exception:
                pass
            time.sleep(0.1)
    threading.Thread(target=place, daemon=True).start()
    try:
        end = time.time() + 300
        ready = None
        while time.time() < end and ready is None:
            ready = log.wait(r'LUA_READY', 2, x4)
            if x4.poll() is not None:
                raise SystemExit('X4 ended at its start')
            if not (own / LOG).exists() and refused_start(x4.pid):
                raise SystemExit('X4 says "Game is already running!" (another X4 took the instance mutex first)')
        summary['menu'] = ready
        summary['mutex_released'] = instance.release_instance_mutex(x4.pid)   # as the launcher does: others may start
        proc = Proc(x4.pid)
        summary['pinned'] = pinned_ok(proc, VIEW / 'X4.exe')
        if not summary['pinned'][0]:
            raise SystemExit(f'not the pinned build: {summary["pinned"][1]}')
        fileio = proc.q(proc.base + RVA_FILEIO)
        gameclient = proc.q(proc.base + RVA_GAMECLIENT)
        summary['folder_at_start'] = proc.string(fileio + 0x28)[3].decode('utf-8', 'replace')
        summary['forced_seed_at_start'] = proc.q(gameclient + 0x218)
        summary['info_start'] = cmds.run('info')
        if not o.quick:
            # control: the "own" save, loaded before the session's files exist (X4 looks for this extension's md,
            # aiscripts and library patches and finds none)
            summary['reload_own'] = cmds.run('reload', timeout=120)
            summary['load_own'] = cmds.run('load', 'save_008', result=r'LUA_READY .*startmenu=false', timeout=400, proc=x4)
            time.sleep(15)
            log.poll()
            summary['ware_own'] = cmds.run('ware')
            summary['seed_own_load'] = proc.q(proc.base + RVA_SESSION_SEED)
        # the helper's part: the session's start-time files into the running extension
        for part in ('md', 'aiscripts', 'libraries'):
            shutil.copytree(HERE / 'payload' / part, ext / part, dirs_exist_ok=True)
        if o.gate_dll:
            (ext / 'native').mkdir(exist_ok=True)
            shutil.copyfile(o.gate_dll, ext / 'native' / 'x4lan_gate_64.dll')
        time.sleep(3)
        before = tree_state(own)
        # the gate's part: personal folder 45855 -> 45856 (same length: one byte), forced seed as -seed: would set it
        ptr, size, cap, text = proc.string(fileio + 0x28)
        if not text.endswith(f'\\{OWN}\\'.encode()):
            raise SystemExit(f'unexpected personal folder {text!r}')
        proc.write(ptr + size - 2, str(SESSION_PROFILE)[-1].encode())
        summary['folder_redirected'] = proc.string(fileio + 0x28)[3].decode('utf-8', 'replace')
        proc.write(gameclient + 0x218, SEED.to_bytes(8, 'little'))
        sptr, ssize, scap, _ = proc.string(gameclient + 0x220)
        if scap >= 9:
            proc.write(sptr, str(SEED).encode() + b'\0')
            proc.write(gameclient + 0x230, (9).to_bytes(8, 'little'))
        summary['forced_seed_set'] = (proc.q(gameclient + 0x218), proc.string(gameclient + 0x220)[3].decode())
        summary['info_redirected'] = cmds.run('info')
        # protected UI mode off for the session's UI states, with X4's own option function (no DLL yet): its config
        # save now lands in the disposable profile
        summary['uisafe_off'] = cmds.run('uisafe')
        if o.quick:   # the helper's alternative: keep `package` for the UI states made from now on (2 bytes, CAS)
            summary['package_branch_patched'] = proc.patch(RVA_PACKAGE_BRANCH, b'\x74\x1f', b'\xeb\x1f')
        summary['reload_session'] = cmds.run('reload', timeout=120)
        summary['load_session'] = cmds.run('load', 'save_009', result=r'LUA_READY .*startmenu=false', timeout=400, proc=x4)   # its package= says whether the session's UI state may load the gate
        settle = time.time() + 45
        while time.time() < settle and x4.poll() is None:
            log.poll()
            time.sleep(0.5)
        for key, marker in (('md_new_script', 'MD_LOADED'), ('ai_new_script', 'AI_RUNS|MD_NO_PILOT|MD_STARTS_AI'),
                            ('md_patch_absent_at_start', 'MD_PATCH_APPLIED')):
            summary[key] = [row for row in log.rows if re.search(marker, row)]
        summary['ware_session'] = cmds.run('ware')
        if o.gate_dll:
            summary['gate_loadlib'] = cmds.run('gate', 'native/x4lan_gate_64.dll')
        if o.quick:
            summary['package_branch_restored'] = proc.patch(RVA_PACKAGE_BRANCH, b'\xeb\x1f', b'\x74\x1f')
        summary['seed_session_load'] = proc.q(proc.base + RVA_SESSION_SEED)
        summary['save'] = cmds.run('save', 'save_010', timeout=180)
        time.sleep(20)
        summary['autosave_toggle'] = cmds.run('autosave')
        time.sleep(3)
        dll = LANES / 'inject' / 'x4lan_net_64.dll'
        dll.parent.mkdir(exist_ok=True)
        shutil.copyfile(ROOT / 'src/lockstep/base/native/x4lan_net_64.dll', dll)
        summary['inject'] = proc.inject(dll)
        summary['quit'] = cmds.run('quit', timeout=10)
        x4.wait(180)
    except BaseException:
        placing.set()
        for pid in own_lanes_x4():
            session.kill(pid)
        time.sleep(3)
        try:
            shutil.copy2(own / LOG, out / 'x4-own-profile.log')
        except OSError:
            pass
        (out / 'summary.json').write_text(json.dumps(summary, indent=2, default=str))
        cleanup(own, disposable)
        raise
    placing.set()
    for pid in own_lanes_x4():
        session.kill(pid)
    time.sleep(3)
    after = tree_state(own)
    summary['own_profile_after_redirect'] = diff_trees(before, after) if 'before' in locals() else None
    summary['disposable_profile'] = {k: v[0] for k, v in tree_state(disposable).items()}
    if (disposable / 'config.xml').is_file():
        summary['disposable_config'] = (disposable / 'config.xml').read_text(errors='replace')
    summary['rows'] = log.rows
    shutil.copy2(own / LOG, out / 'x4-own-profile.log')
    (out / 'summary.json').write_text(json.dumps(summary, indent=2, default=str))
    print(json.dumps({k: v for k, v in summary.items() if k != 'rows'}, indent=2, default=str))
    cleanup(own, disposable)


def cleanup(*folders):
    """The view and both profiles (their saves and logs read or copied), the injected DLL's copy."""
    if VIEW.exists():
        game.remove_view(VIEW)
    for folder in folders:
        if Path(folder).exists():
            profile.remove_tree(folder)
    shutil.rmtree(LANES / 'inject', ignore_errors=True)


if __name__ == '__main__':
    main()

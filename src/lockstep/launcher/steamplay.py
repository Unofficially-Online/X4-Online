"""The helper behind "Play in Steam, then Multiplayer": no shortcut and no console for the player.

A kit with a server (tools/make_playtest_kit.py --server) holds a small menu extension (steam-menu/) and helper.pyw,
which runs this module without a window. The installer starts the helper and registers it to start with Windows.

- The helper copies the menu extension into X4's own extensions folder (x4mp_playtest_menu: one Lua file and its
  config; no Egosoft file is touched). An X4 that Steam starts then has Multiplayer in its main menu.
- That X4 runs the player's own profile, where a session must not run and cannot (its settings, its saves, no launch
  switches). The page there talks to this helper through named pipes, the one way out of X4's UI Lua that works with
  its protected UI mode on: the game opens \\\\.\\pipe\\x4mp-playtest-state (or -join) with loadfile, the helper answers
  every open with the state as a Lua table, and which pipe was opened is the request.
- On Join the helper answers "restart", the page quits X4, and the helper starts X4 again exactly as x4ls lobby does
  (the disposable profile, the kit's overlay and snapshot) with the join already chosen. The player sees X4 close and
  come back on the Multiplayer page, joined. When the session is over the helper waits for the next Join.

Nothing of the player's own profile, saves or settings is read or written. While the menu extension is installed X4
counts as modified, as with any extension; removing the installer removes it (helper.pyw --remove).

Windows only. Everything the helper starts runs without a console window; problems are shown in a message box and
written to repo/.local/helper.log."""
import ctypes
import json
import os
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

from . import game, lobby

PIPE = 'x4mp-playtest'
PREFIX = '\\\\.\\pipe\\'
MENU_ID = 'x4mp_playtest_menu'
TITLE = 'X4 Multiplayer Playtest'
HIDDEN = 0x08000000   # CREATE_NO_WINDOW

MENU_CONTENT = ('<?xml version="1.0" encoding="utf-8"?>\n'
                f'<content id="{MENU_ID}" name="X4 Multiplayer Playtest (menu)" '
                'description="Adds Multiplayer to the main menu. The session itself runs in a separate, disposable profile." '
                'author="X4 lockstep" version="100" date="2026-11-09" save="false">\n'
                '  <dependency version="900" />\n</content>\n')
MENU_UI = ('<?xml version="1.0" encoding="utf-8"?>\n'
           f'<addon name="{MENU_ID}">\n  <environment type="menus">\n    <dependency name="ego_detailmonitor" />\n'
           '    <file name="ui/config.lua" />\n    <file name="ui/native_gate_lobby.lua" />\n  </environment>\n</addon>\n')
MENU_CONFIG = f"X4LAN_CONFIG = {{enabled = false, lobby = {{pipe = '{PIPE}', server = true}}}}\n"


def write_menu(folder, lobby_lua=lobby.LOBBY_LUA):
    """The menu extension's files (a kit holds them as steam-menu/)."""
    folder = Path(folder)
    (folder / 'ui').mkdir(parents=True, exist_ok=True)
    (folder / 'content.xml').write_text(MENU_CONTENT, encoding='utf-8')
    (folder / 'ui.xml').write_text(MENU_UI, encoding='utf-8')
    (folder / 'ui' / 'config.lua').write_text(MENU_CONFIG, encoding='utf-8')
    shutil.copyfile(lobby_lua, folder / 'ui' / 'native_gate_lobby.lua')
    return folder


def tree(folder):
    return {p.relative_to(folder).as_posix(): p.read_bytes() for p in sorted(Path(folder).rglob('*')) if p.is_file()}


def install_menu(source, install):
    """Copy the menu extension into X4's extensions folder; True when something was written."""
    target = Path(install) / 'extensions' / MENU_ID
    if target.is_dir() and tree(target) == tree(source):
        return False
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(source, target)
    return True


def remove_menu(install):
    target = Path(install) / 'extensions' / MENU_ID
    if (target / 'content.xml').is_file() and f'id="{MENU_ID}"' in (target / 'content.xml').read_text(errors='replace'):
        shutil.rmtree(target)
        return True
    return False


def say(text, error=False):
    ctypes.windll.user32.MessageBoxW(None, text, TITLE, (0x10 if error else 0x40) | 0x10000)   # icon, to the foreground


class Pipes:
    """Named pipes that answer every open with a text: `answer(name)` gives it; the open itself is the message."""
    def __init__(self, prefix, names, answer, instances=2):
        self.prefix, self.answer, self.closing, self.names = prefix, answer, False, tuple(names)
        self.threads = [threading.Thread(target=self.serve, args=(name,), daemon=True) for name in names for _ in range(instances)]
        for thread in self.threads:
            thread.start()

    def serve(self, name):
        import _winapi
        while not self.closing:
            try:
                handle = _winapi.CreateNamedPipe(PREFIX + self.prefix + '-' + name, _winapi.PIPE_ACCESS_DUPLEX, 0,
                                                 _winapi.PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, _winapi.NULL)
            except OSError:
                time.sleep(0.5)
                continue
            try:
                try:
                    _winapi.ConnectNamedPipe(handle, _winapi.NULL)
                except OSError as error:
                    if error.winerror != 535:   # 535: the client was there before we waited
                        continue
                try:
                    _winapi.WriteFile(handle, self.answer(name).encode('utf-8'))
                    ctypes.windll.kernel32.FlushFileBuffers(ctypes.c_void_p(int(handle)))
                except OSError:
                    pass
            finally:
                _winapi.CloseHandle(handle)


    def close(self):
        """End the threads: each one waits for an open, so every pipe is opened until none is left."""
        self.closing = True
        for name in self.names:
            for _ in range(len(self.threads)):
                if ask(self.prefix, name, timeout=0) is None:
                    break
        for thread in self.threads:
            thread.join(2)


def ask(prefix, name, timeout=3):
    """What the game does: open the pipe and read the answer. None when no helper answers."""
    end = time.time() + timeout
    while True:
        try:
            with open(PREFIX + prefix + '-' + name, 'rb') as f:
                return f.read().decode('utf-8', 'replace')
        except OSError:
            if time.time() > end:
                return None
            time.sleep(0.1)


def x4_running():
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq X4.exe', '/FO', 'CSV', '/NH'], capture_output=True, text=True,
                         creationflags=HIDDEN)
    return 'x4.exe' in out.stdout.lower()


class Helper:
    def __init__(self, kit, snapshot, prefix=PIPE, log=None, start=None, running=x4_running):
        self.kit, self.snapshot, self.prefix = Path(kit), snapshot, prefix
        self.recipe = json.loads((self.kit / 'overlay' / 'play-recipe.json').read_text(encoding='utf-8'))
        self.lock = threading.Lock()
        self.stamp, self.phase, self.lines = 0, 'idle', ()
        self.wanted = self.stopping = False
        self.log_file = self.kit / 'repo' / '.local' / 'helper.log'
        self.log = log or self.write_log
        self.start, self.running = start or self.start_lobby, running
        self.show('idle')

    def write_log(self, text):
        try:
            self.log_file.parent.mkdir(parents=True, exist_ok=True)
            with self.log_file.open('a', encoding='utf-8') as f:
                f.write(f'{time.strftime("%Y-%m-%d %H:%M:%S")} {text}\n')
        except OSError:
            pass

    def show(self, phase, *lines):
        players = f'{self.recipe["players"]} players, {self.recipe.get("mode", "coop")}, {self.recipe.get("minutes", "?"):g} minutes'
        if phase == 'idle' and not lines:
            lines = (players, 'Join closes X4 and starts it again in its multiplayer profile, already joined.')
        with self.lock:
            self.stamp, self.phase, self.lines = self.stamp + 1, phase, lines

    def answer(self, name):
        """A pipe was opened: `name` is the request. Never waits: X4's menu reads this in its frame."""
        if name == 'join' and not self.stopping:
            with self.lock:
                if self.phase == 'idle':
                    self.wanted = True
                    self.stamp, self.phase = self.stamp + 1, 'restart'
                    self.lines = ('X4 closes now and comes back in a moment, in its multiplayer profile.',
                                  'It joins by itself; the game loads when every player is here.')
        elif name == 'stop':
            self.stopping = True
        with self.lock:
            return lobby.state_text(self.stamp, self.phase, self.lines)

    def python(self):
        own = self.kit / 'python' / 'python.exe'
        return own if own.is_file() else Path(sys.executable).with_name('python.exe')

    def start_lobby(self):
        """X4 in the disposable profile with the join already chosen, as PLAY.cmd would start it; its exit code."""
        base = [str(self.python()), 'repo/tools/x4ls.py']
        check = subprocess.run([*base, 'doctor'], cwd=self.kit, capture_output=True, text=True, creationflags=HIDDEN)
        if check.returncode:
            return check.returncode, (check.stdout + check.stderr).strip()
        out = self.log_file.with_name('helper-lobby.log')
        out.parent.mkdir(parents=True, exist_ok=True)
        with out.open('w', encoding='utf-8', errors='replace') as f:
            code = subprocess.run([*base, 'lobby', '--overlay', 'overlay', '--snapshot', self.snapshot,
                                   '--server-file', 'server.json', '--auto', 'join', '--show', '--close-when-over'],
                                  cwd=self.kit, stdout=f, stderr=subprocess.STDOUT, creationflags=HIDDEN).returncode
        return code, out.read_text(encoding='utf-8', errors='replace').strip()

    def session(self):
        self.log('join asked for: waiting for X4 to close')
        end = time.time() + 90
        while self.running() and time.time() < end:
            time.sleep(0.5)
        if self.running():
            self.log('X4 did not close')
            self.show('idle', 'X4 did not close by itself.', 'Close it, start it again and choose Join once more.')
            return
        self.show('playing', 'A multiplayer session runs in X4\'s multiplayer profile.',
                  'Close that window first if you want to play alone.')
        code, text = self.start()
        self.log(f'the session ended with code {code}')
        if code:
            self.log(text[-3000:])
            say('The multiplayer session did not start, or ended with a problem.\n\n' + text[-1200:]
                + f'\n\nMore in {self.log_file.parent}', error=True)
        self.show('idle')

    def run(self):
        pipes = Pipes(self.prefix, ('state', 'join', 'stop'), self.answer)
        self.log(f'helper ready (pid {os.getpid()}, kit {self.kit})')
        try:
            while not self.stopping:
                if self.wanted:
                    self.wanted = False
                    self.session()
                time.sleep(0.2)
        finally:
            pipes.close()
        self.log('helper stopped')


def first_instance(prefix=PIPE):
    """A pipe only one helper can own; None when another helper has it. Keep the handle."""
    import _winapi
    try:
        return _winapi.CreateNamedPipe(PREFIX + prefix + '-owner', _winapi.PIPE_ACCESS_DUPLEX | _winapi.FILE_FLAG_FIRST_PIPE_INSTANCE,
                                       0, 1, 256, 256, 0, _winapi.NULL)
    except OSError:
        return None


def stop(prefix=PIPE, wait=15):
    """Ask a running helper to end; True once none is running."""
    ask(prefix, 'stop', timeout=1)
    end = time.time() + wait
    while time.time() < end:
        handle = first_instance(prefix)
        if handle is not None:
            import _winapi
            _winapi.CloseHandle(handle)
            return True
        time.sleep(0.3)
    return False


def redirection_guarded():
    """True when this process refuses folder links made by a normal user (Windows' redirection guard). What Windows
    Installer starts inherits that, and an X4 started from there cannot open its game view (Win32 error 448)."""
    try:
        kernel = ctypes.WinDLL('kernel32')
        kernel.GetCurrentProcess.restype = ctypes.c_void_p
        kernel.GetProcessMitigationPolicy.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t]
        flags = ctypes.c_uint(0)
        if not kernel.GetProcessMitigationPolicy(kernel.GetCurrentProcess(), 16, ctypes.byref(flags), 4):   # ProcessRedirectionTrustPolicy
            return False
        return bool(flags.value & 1)   # EnforceRedirectionTrust
    except (OSError, AttributeError):
        return False


def main(kit, snapshot, argv=None):
    """helper.pyw of a kit: no argument starts the helper, without any message (--quiet is accepted and means the
    same); --stop ends a running helper; --remove ends it and takes the menu extension out of X4 again."""
    argv = sys.argv[1:] if argv is None else argv
    kit = Path(kit)
    if '--stop' in argv or '--remove' in argv:
        stop()
        if '--remove' in argv:
            try:
                remove_menu(game.find_install())
            except (SystemExit, OSError):
                pass
        return 0
    shortcut = Path(os.environ.get('APPDATA', '')) / 'Microsoft/Windows/Start Menu/Programs' / (TITLE + '.lnk')
    if redirection_guarded() and shortcut.is_file() and '--guarded' not in argv:
        subprocess.Popen(['explorer.exe', str(shortcut)])   # Explorer starts the helper again, without the guard
        return 0
    owner = first_instance()
    if owner is None:   # one is running already: nothing to do, and nothing to say
        return 0
    helper = Helper(kit, snapshot)
    try:
        install = game.find_install()
        changed = install_menu(kit / 'steam-menu', install)
        helper.log(f'menu extension {"installed" if changed else "up to date"} in {install}')
    except (SystemExit, OSError) as error:
        helper.log(f'menu extension not installed: {error}')
        say('The Multiplayer entry could not be put into X4:\n\n' + str(error), error=True)
        return 1
    helper.run()   # silent: the player sees Multiplayer in X4's main menu, and a message box only for a problem
    return 0

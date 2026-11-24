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

A release kit (release.json, no session; tools/make_release_kit.py) has a helper of its own (NetHelper): its menu
extension is x4online_menu and its pipes x4online-*, so a playtest install and a release install live on one PC side by
side. Its page spells netplay's requests (docs/NETPLAY.md) through fixed pipes (tbegin, t0..tf, tend); listing games happens
here, in the Steam-started X4, while hosting and joining start X4 again in the disposable profile with the request
already chosen (x4ls lobby --request), where netplay's flow runs (netlobby.py).

Windows only. Everything the helper starts runs without a console window; problems are shown in a message box and
written to repo/.local/helper.log."""
import ctypes
from dataclasses import dataclass
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

from . import game, lobby, netplay

PREFIX = '\\\\.\\pipe\\'
HIDDEN = 0x08000000   # CREATE_NO_WINDOW


@dataclass(frozen=True)
class Identity:
    """The menu extension, pipes and names of one kind of kit: a playtest kit's or a release's. Each kind has its own,
    so that both can be installed on one PC (two menu extensions, two helpers, two Start-menu entries)."""
    menu_id: str
    pipe: str
    title: str
    lobby: str   # the page's lobby table in the menu's config.lua

    def content(self):
        return ('<?xml version="1.0" encoding="utf-8"?>\n'
                f'<content id="{self.menu_id}" name="{self.title} (menu)" '
                'description="Adds Multiplayer to the main menu. The session itself runs in a separate, disposable profile." '
                'author="X4 lockstep" version="100" date="2026-11-09" save="false">\n'
                '  <dependency version="900" />\n</content>\n')

    def ui(self):
        return ('<?xml version="1.0" encoding="utf-8"?>\n'
                f'<addon name="{self.menu_id}">\n  <environment type="menus">\n    <dependency name="ego_detailmonitor" />\n'
                '    <file name="ui/config.lua" />\n    <file name="ui/native_gate_lobby.lua" />\n  </environment>\n</addon>\n')

    def config(self):
        return f'X4LAN_CONFIG = {{enabled = false, lobby = {self.lobby}}}\n'


PLAYTEST = Identity('x4mp_playtest_menu', 'x4mp-playtest', 'X4 Multiplayer Playtest',
                    "{pipe = 'x4mp-playtest', server = true, title = 'Multiplayer (playtest)'}")
RELEASE = Identity('x4online_menu', 'x4online', 'X4 Online', "{pipe = 'x4online', netplay = true}")
# The playtest kit's names, as they always were (its tests and kits use them)
PIPE = PLAYTEST.pipe
MENU_ID = PLAYTEST.menu_id
TITLE = PLAYTEST.title
MENU_CONTENT = PLAYTEST.content()
MENU_UI = PLAYTEST.ui()
MENU_CONFIG = PLAYTEST.config()


def identity_of(kit):
    """A release kit (release.json) has the release's identity; every other kit the playtest's."""
    return RELEASE if (Path(kit) / 'release.json').is_file() else PLAYTEST


def write_menu(folder, lobby_lua=lobby.LOBBY_LUA, identity=PLAYTEST, release=False):
    """The menu extension's files (a kit holds them as steam-menu/). A release kit: identity=RELEASE (or release=True,
    tools/make_release_kit.py's spelling)."""
    if release:
        identity = RELEASE
    folder = Path(folder)
    (folder / 'ui').mkdir(parents=True, exist_ok=True)
    (folder / 'content.xml').write_text(identity.content(), encoding='utf-8')
    (folder / 'ui.xml').write_text(identity.ui(), encoding='utf-8')
    (folder / 'ui' / 'config.lua').write_text(identity.config(), encoding='utf-8')
    shutil.copyfile(lobby_lua, folder / 'ui' / 'native_gate_lobby.lua')
    return folder


def tree(folder):
    return {p.relative_to(folder).as_posix(): p.read_bytes() for p in sorted(Path(folder).rglob('*')) if p.is_file()}


def install_menu(source, install, identity=PLAYTEST):
    """Copy the menu extension into X4's extensions folder; True when something was written."""
    target = Path(install) / 'extensions' / identity.menu_id
    if target.is_dir() and tree(target) == tree(source):
        return False
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(source, target)
    return True


def remove_menu(install, identity=PLAYTEST):
    """Take this kind's menu extension out of X4 (only its own: another kind's, or a folder of that name that is not
    ours, stays)."""
    target = Path(install) / 'extensions' / identity.menu_id
    content = target / 'content.xml'
    if content.is_file() and f'id="{identity.menu_id}"' in content.read_text(errors='replace'):
        shutil.rmtree(target)
        return True
    return False


def say(text, error=False, title=TITLE):
    ctypes.windll.user32.MessageBoxW(None, text, title, (0x10 if error else 0x40) | 0x10000)   # icon, to the foreground


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


start_label = lobby.start_label


def x4_running():
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq X4.exe', '/FO', 'CSV', '/NH'], capture_output=True, text=True,
                         creationflags=HIDDEN)
    return 'x4.exe' in out.stdout.lower()


class Helper:
    def __init__(self, kit, snapshot, prefix=PIPE, log=None, start=None, running=x4_running, status=None):
        self.kit, self.snapshot, self.prefix = Path(kit), snapshot, prefix
        self.recipe = json.loads((self.kit / 'overlay' / 'play-recipe.json').read_text(encoding='utf-8'))
        self.lock = threading.Lock()
        self.stamp, self.phase, self.lines = 0, 'idle', ()
        self.wanted = self.stopping = False   # wanted: True (the common load) or the open slot to join the running game in
        self.status = status or self.ask_status   # () -> the server's status, or None when it does not answer
        self.server = None                        # ... as last answered
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

    def ask_status(self):
        from . import lobby as _lobby
        server_file = self.kit / 'server.json'
        if not server_file.is_file():
            return None
        if not hasattr(self, 'kit_id'):
            self.kit_id = _lobby.kit_id(self.kit / 'overlay', self.recipe.get('snapshot_sha'))
            self.server_address = _lobby.read_server_file(server_file)
        answer = _lobby.ask_server(self.server_address, self.kit_id, 'status')
        return answer.get('status') if isinstance(answer, dict) and answer.get('ok') else None

    def idle_view(self):
        """What the page shows while nothing is asked: (lines, fields) from the session and the server's last status."""
        players = f'{self.recipe["players"]} players, {self.recipe.get("mode", "coop")}, {self.recipe.get("minutes", "?"):g} minutes'
        lines, fields = lobby.picker_view(self.recipe, self.server, 'X4 closes and starts again in its multiplayer profile.')
        if not lines:   # every player is there at the start (or the server does not answer)
            return (players, 'Join closes X4 and starts it again in its multiplayer profile, already joined.'), {}
        return lines, fields

    def show(self, phase, *lines, **fields):
        if phase == 'idle' and not lines:
            lines, fields = self.idle_view()
        with self.lock:
            if (phase, lines, fields) != (self.phase, self.lines, getattr(self, 'fields', None)):
                self.stamp, self.phase, self.lines, self.fields = self.stamp + 1, phase, lines, fields

    def answer(self, name):
        """A pipe was opened: `name` is the request. Never waits: X4's menu reads this in its frame."""
        slot = int(name[4:]) if name.startswith('join') and name[4:].isdigit() else None
        if (name == 'join' or slot) and not self.stopping:
            with self.lock:
                if self.phase == 'idle':
                    self.wanted = slot or True
                    self.stamp, self.phase, self.fields = self.stamp + 1, 'restart', {}
                    self.lines = ('X4 closes now and comes back in a moment, in its multiplayer profile.',
                                  'It loads the running game as it is now and joins it.' if slot else
                                  'It joins by itself; the game loads when every player is here.')
        elif name == 'stop':
            self.stopping = True
        with self.lock:
            return lobby.state_text(self.stamp, self.phase, self.lines, **getattr(self, 'fields', {}))

    def python(self):
        own = self.kit / 'python' / 'python.exe'
        return own if own.is_file() else Path(sys.executable).with_name('python.exe')

    def start_lobby(self, slot=None):
        """X4 in the disposable profile with the join already chosen, as PLAY.cmd would start it; its exit code. With an
        open slot: x4ls join --server-file, the hot join of the game that runs on the server."""
        base = [str(self.python()), 'repo/tools/x4ls.py']
        check = subprocess.run([*base, 'doctor'], cwd=self.kit, capture_output=True, text=True, creationflags=HIDDEN)
        if check.returncode:
            return check.returncode, (check.stdout + check.stderr).strip()
        out = self.log_file.with_name('helper-lobby.log')
        out.parent.mkdir(parents=True, exist_ok=True)
        command = ([*base, 'join', '--overlay', 'overlay', '--server-file', 'server.json', '--slot', str(slot), '--close-when-over']
                   if slot else
                   [*base, 'lobby', '--overlay', 'overlay', '--snapshot', self.snapshot,
                    '--server-file', 'server.json', '--auto', 'join', '--show', '--close-when-over'])
        with out.open('w', encoding='utf-8', errors='replace') as f:
            code = subprocess.run(command, cwd=self.kit, stdout=f, stderr=subprocess.STDOUT, creationflags=HIDDEN).returncode
        return code, out.read_text(encoding='utf-8', errors='replace').strip()

    def session(self, slot=None):
        self.log(('join of the running game in slot %d' % slot if slot else 'join') + ' asked for: waiting for X4 to close')
        end = time.time() + 90
        while self.running() and time.time() < end:
            time.sleep(0.5)
        if self.running():
            self.log('X4 did not close')
            self.show('idle', 'X4 did not close by itself.', 'Close it, start it again and choose Join once more.')
            return
        self.show('playing', 'A multiplayer session runs in X4\'s multiplayer profile.',
                  'Close that window first if you want to play alone.')
        code, text = self.start(slot) if slot else self.start()
        self.log(f'the session ended with code {code}')
        if code:
            self.log(text[-3000:])
            say('The multiplayer session did not start, or ended with a problem.\n\n' + text[-1200:]
                + f'\n\nMore in {self.log_file.parent}', error=True)
        self.show('idle')

    def run(self):
        pipes = Pipes(self.prefix, ('state', 'join', 'stop', *(f'join{k}' for k in range(2, 9))), self.answer)
        self.log(f'helper ready (pid {os.getpid()}, kit {self.kit})')
        asked = 0.0
        try:
            while not self.stopping:
                if self.wanted:
                    wanted, self.wanted = self.wanted, False
                    self.session(None if wanted is True else wanted)
                    asked = 0.0
                elif time.time() - asked > 3:   # the server's state for the page: a game running, the free starts
                    asked = time.time()
                    try:
                        self.server = self.status()
                    except Exception as error:   # the page still works without it
                        self.server = None
                        self.log(f'the server status could not be read: {error}')
                    if self.phase == 'idle' and not self.wanted:
                        self.show('idle')
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


def kit_python(kit):
    own = Path(kit) / 'python' / 'python.exe'
    return own if own.is_file() else Path(sys.executable).with_name('python.exe')


def second(kit, snapshot=None):
    """second.pyw of a kit: one more X4 on this PC for the multiplayer session, without Steam (which starts only one).
    It opens in the disposable profile on the Multiplayer page: a kit with a server's Start and starts of the running
    game; a release kit's netplay page (snapshot None)."""
    kit = Path(kit)
    identity = identity_of(kit)
    base = [str(kit_python(kit)), 'repo/tools/x4ls.py']
    check = subprocess.run([*base, 'doctor'], cwd=kit, capture_output=True, text=True, creationflags=HIDDEN)
    if check.returncode:
        say('X4 cannot be started for multiplayer:\n\n' + (check.stdout + check.stderr).strip()[-1200:], error=True,
            title=identity.title)
        return 1
    out = kit / 'repo' / '.local' / f'second-{os.getpid()}.log'
    out.parent.mkdir(parents=True, exist_ok=True)
    if snapshot is None:
        command = [*base, 'lobby', '--kit', str(kit), '--show', '--close-when-over']
    else:
        command = [*base, 'lobby', '--overlay', 'overlay', '--snapshot', snapshot, '--server-file', 'server.json',
                   '--show', '--close-when-over']
    with out.open('w', encoding='utf-8', errors='replace') as f:
        code = subprocess.run(command, cwd=kit, stdout=f, stderr=subprocess.STDOUT, creationflags=HIDDEN).returncode
    if code:
        say('The second window did not start, or ended with a problem.\n\n'
            + out.read_text(encoding='utf-8', errors='replace').strip()[-1200:], error=True, title=identity.title)
    else:
        out.unlink(missing_ok=True)
    return code


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


def main(kit, snapshot=None, argv=None):
    """helper.pyw of a kit: no argument starts the helper, without any message (--quiet is accepted and means the
    same); --stop ends a running helper; --remove ends it and takes the menu extension out of X4 again. A release kit
    (snapshot None, release.json) runs the netplay helper under the release's own names."""
    argv = sys.argv[1:] if argv is None else argv
    kit = Path(kit)
    identity = identity_of(kit)
    if '--stop' in argv or '--remove' in argv:
        stop(identity.pipe)
        if '--remove' in argv:
            try:
                remove_menu(game.find_install(), identity)
            except (SystemExit, OSError):
                pass
        return 0
    shortcut = Path(os.environ.get('APPDATA', '')) / 'Microsoft/Windows/Start Menu/Programs' / (identity.title + '.lnk')
    if redirection_guarded() and shortcut.is_file() and '--guarded' not in argv:
        subprocess.Popen(['explorer.exe', str(shortcut)])   # Explorer starts the helper again, without the guard
        return 0
    owner = first_instance(identity.pipe)
    if owner is None:   # one is running already: nothing to do, and nothing to say
        return 0
    if identity is RELEASE:
        helper = NetHelper(kit, prefix=identity.pipe)
    else:
        helper = Helper(kit, snapshot, prefix=identity.pipe)
    try:
        install = game.find_install()
        changed = install_menu(kit / 'steam-menu', install, identity)
        helper.log(f'menu extension {"installed" if changed else "up to date"} in {install}')
    except (SystemExit, OSError) as error:
        helper.log(f'menu extension not installed: {error}')
        say('The Multiplayer entry could not be put into X4:\n\n' + str(error), error=True, title=identity.title)
        return 1
    helper.run()   # silent: the player sees Multiplayer in X4's main menu, and a message box only for a problem
    return 0


# ---- a release kit's helper: netplay ----
# A pipe server answers only the names it made, so a request with typed text (a code, a game's name) cannot have a pipe
# of its own. Every netplay request is spelled through fixed pipes (docs/NETPLAY.md "Launcher notes"): <pipe>-tbegin
# clears the helper's buffer, <pipe>-t0 .. <pipe>-tf each add one hex digit, <pipe>-tend takes the buffer as the
# lowercase hex of the request's UTF-8 bytes. Every open is answered with the state, as every pipe is. (Begin and end
# cannot be tb and te: those are the digits b and e, and pipe names do not tell case apart.)
TEXT_BEGIN = 'tbegin'
TEXT_END = 'tend'
TEXT_DIGITS = tuple(f't{digit}' for digit in '0123456789abcdef')
TEXT_NAMES = (TEXT_BEGIN, TEXT_END, *TEXT_DIGITS)
TEXT_HEX_MAX = 400   # hex digits: a request of 200 bytes (the page's longest is a 100-byte code)
LEGACY_NAMES = ('state', 'stop', 'join', *(f'join{slot}' for slot in range(2, 9)))
STAMP = re.compile(r'^return \{stamp = \d+')


class TextRequests:
    """The spelled requests of the page (the helper's one client): one buffer, filled by the opens in their order (an
    open is answered before the page makes the next, so the digits cannot pass each other)."""

    def __init__(self, log):
        self.log = log
        self.lock = threading.Lock()
        self.digits = None      # None: no tbegin since the last tend
        self.too_long = False

    def open(self, name):
        """One open of a text pipe; the request's text at tend, else None."""
        with self.lock:
            if name == TEXT_BEGIN:
                self.digits = ''
                self.too_long = False
                return None
            if name != TEXT_END:
                if self.digits is None:
                    return None
                if len(self.digits) >= TEXT_HEX_MAX:
                    self.too_long = True
                else:
                    self.digits += name[1:]
                return None
            digits = self.digits
            too_long = self.too_long
            self.digits = None
        if digits is None:
            self.log('netplay: a spelled request ended without its begin (ignored)')
            return None
        text = netplay.request_from_hex(digits)
        if too_long or text is None:
            self.log(f'netplay: a spelled request is {"too long" if too_long else "not hex of UTF-8 text"} (ignored)')
            return None
        return text


class TextPipes:
    """The helper's named pipes, fixed names. One digit can come many times in a row (the page tries an open that finds
    no free instance again at once, up to 100 times), so a listening instance of every name is there at all times: two
    threads per name, and each makes its next instance before it answers the open it has."""
    THREADS_PER_NAME = 2

    def __init__(self, prefix, answer, names):
        self.prefix = prefix
        self.answer = answer
        self.names = tuple(names)
        self.closing = False
        self.threads = [threading.Thread(target=self.serve, args=(name,), daemon=True)
                        for name in self.names for _ in range(self.THREADS_PER_NAME)]
        for thread in self.threads:
            thread.start()

    def create(self, name):
        import _winapi
        return _winapi.CreateNamedPipe(PREFIX + self.prefix + '-' + name, _winapi.PIPE_ACCESS_DUPLEX, 0,
                                       _winapi.PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, _winapi.NULL)

    def serve(self, name):
        import _winapi
        handle = None
        while True:
            if handle is None:
                try:
                    handle = self.create(name)
                except OSError:
                    if self.closing:
                        return
                    time.sleep(0.5)
                    continue
            try:
                _winapi.ConnectNamedPipe(handle, _winapi.NULL)
            except OSError as error:
                if error.winerror != 535:   # 535: the client was there before we waited
                    _winapi.CloseHandle(handle)
                    handle = None
                    continue
            if self.closing:
                _winapi.CloseHandle(handle)
                return
            try:
                following = self.create(name)
            except OSError:
                following = None
            try:
                _winapi.WriteFile(handle, self.answer(name).encode('utf-8'))
                ctypes.windll.kernel32.FlushFileBuffers(ctypes.c_void_p(int(handle)))
            except OSError:
                pass
            finally:
                _winapi.CloseHandle(handle)
            handle = following

    def close(self):
        """End the threads: each waits for an open, so each name is opened until no instance answers."""
        self.closing = True
        for name in self.names:
            for _ in range(2 * self.THREADS_PER_NAME):
                if ask(self.prefix, name, timeout=0) is None:
                    break
        for thread in self.threads:
            thread.join(2)


class HelperBackend(netplay.Backend):
    """netplay's backend in the helper: listing games happens here; hosting and joining start X4 again in the
    disposable profile (the player's own profile must not run a session)."""

    def __init__(self, helper):
        self.helper = helper

    def restart_for(self, request):
        return request.kind == 'HOST' or request.joins

    def idle_lines(self):
        lines = [netplay.IDLE_LINES[0], 'Hosting or joining closes X4 and starts it again in its multiplayer profile.']
        if not self.helper.flow_config.master_url:
            lines.append('No master server for this release: games on the LAN, by invite code or by address.')
        return lines

    def work_folder(self):
        return self.helper.local


class NetHelper:
    """The helper of a release kit (see the module text): the page's state and requests through pipes; Host and the
    joins start X4 again with x4ls lobby --request <the request>, and while that runs the state is that lobby's."""

    def __init__(self, kit, prefix=RELEASE.pipe, log=None, start=None, running=x4_running, net=None, master_url=None):
        from . import netkit, netlobby
        self.kit_dir = Path(kit)
        self.prefix = prefix
        self.local = self.kit_dir / 'repo' / '.local'
        self.log_file = self.local / 'helper.log'
        self.mirror = self.local / 'helper-state.lua'
        self.log = log or self.write_log
        self.start = start or self.start_lobby
        self.running = running
        self.stopping = False
        self.text = TextRequests(self.log)
        self.lock = threading.Lock()
        self.mirrored = None       # (the lobby's state text, the stamp it is answered with) while a session runs
        self.stamp = 0
        self.kit = netkit.Kit(self.kit_dir)
        if master_url is None:
            master_url = netlobby.master_url(lobby.LobbyOptions(), self.kit)
        self.flow_config = netplay.FlowConfig(kit=self.kit, release=self.kit.release, master_url=master_url)
        self.flow = netplay.Flow(self.flow_config, HelperBackend(self), net=net, log=self.log)

    write_log = Helper.write_log

    @property
    def phase(self):
        return self.flow.state.phase

    def state_text(self):
        """The answer to an open: the flow's state, or while a session runs, the lobby's (restamped: the page tells a
        new state by its stamp, and the two count apart)."""
        with self.lock:
            if self.mirrored is not None:
                try:
                    text = self.mirror.read_text(encoding='utf-8')
                except OSError:
                    text = self.mirrored[0]
                if text != self.mirrored[0] and STAMP.match(text):
                    self.stamp += 1
                    self.mirrored = (text, self.stamp)
                return STAMP.sub(f'return {{stamp = {self.mirrored[1]}', self.mirrored[0], count=1)
            stamp = self.flow.state.view()[0]
            self.stamp = max(self.stamp, stamp)
            return self.flow.state.text(self.stamp)

    def answer(self, name):
        """A pipe was opened: its name is the request (a spelled one at tend). Never waits: X4's menu reads this in its
        frame."""
        if name == 'stop':
            self.stopping = True
        elif name in TEXT_NAMES:
            text = self.text.open(name)
            if text is not None:
                self.handle(text)
        elif name.startswith('join'):   # the older pipe names: join (the code on the clipboard), join<k>
            self.handle(name.upper())
        return self.state_text()

    def handle(self, text):
        if self.mirrored is not None:   # a game runs in the multiplayer profile; this page waits for it
            self.log(f'netplay: {text[:40]} while a game runs (ignored)')
            return
        self.flow.request(text)

    def start_lobby(self, request):
        """X4 in the disposable profile with the request already made (x4ls lobby --request); (exit code, output)."""
        base = [str(kit_python(self.kit_dir)), 'repo/tools/x4ls.py']
        check = subprocess.run([*base, 'doctor'], cwd=self.kit_dir, capture_output=True, text=True, creationflags=HIDDEN)
        if check.returncode:
            return check.returncode, (check.stdout + check.stderr).strip()
        out = self.local / 'helper-lobby.log'
        out.parent.mkdir(parents=True, exist_ok=True)
        command = [*base, 'lobby', '--kit', str(self.kit_dir), '--request', request, '--show', '--close-when-over',
                   '--state-mirror', str(self.mirror)]
        with out.open('w', encoding='utf-8', errors='replace') as f:
            code = subprocess.run(command, cwd=self.kit_dir, stdout=f, stderr=subprocess.STDOUT, creationflags=HIDDEN).returncode
        return code, out.read_text(encoding='utf-8', errors='replace').strip()

    def session(self, request):
        self.log(f'{request[:80]} asked for: waiting for X4 to close')
        end = time.time() + 90
        while self.running() and time.time() < end:
            time.sleep(0.5)
        if self.running():
            self.log('X4 did not close')
            self.flow.state.set('error', ('X4 did not close by itself.', 'Close it, start it again and choose once more.'))
            return
        self.mirror.parent.mkdir(parents=True, exist_ok=True)
        waiting = netplay.State()
        waiting.set('session', ('A multiplayer game runs in X4\'s multiplayer profile.',
                                'Close that window first if you want to play alone.'))
        lobby.write_atomic(self.mirror, waiting.text())
        with self.lock:
            self.stamp += 1
            self.mirrored = (waiting.text(), self.stamp)
        try:
            code, text = self.start(request)
        finally:
            with self.lock:
                self.mirrored = None
        self.log(f'the game ended with code {code}')
        if code:
            self.log(text[-3000:])
            say('The multiplayer game did not start, or ended with a problem.\n\n' + text[-1200:]
                + f'\n\nMore in {self.local}', error=True, title=RELEASE.title)
        self.flow.cancel()

    def run(self):
        pipes = TextPipes(self.prefix, self.answer, (*LEGACY_NAMES, *TEXT_NAMES))
        self.log(f'helper ready (pid {os.getpid()}, kit {self.kit_dir}, release {self.kit.release})')
        try:
            while not self.stopping:
                request = self.flow.take_handover()
                if request:
                    self.session(request)
                time.sleep(0.2)
        finally:
            self.flow.cancel()
            pipes.close()
        self.log('helper stopped')

#!/usr/bin/env python3
"""Capture a disposable Linux X4 fixture. Requires an already running desktop and Steam.

Example: python3 tools/dev/linux_snapshot_capture.py --game /path/to/X4 \
    --out .local/captures/example --display :0 --timeout 900
The output directory must be new and under this repository's .local directory.
No normal game profile is mounted writable. Logs and the private profile remain in
that directory, including on failure. A successful save is copied to its top level.
"""
import argparse
import gzip
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'src'))
sys.path.insert(0, str(REPO / 'tools/dev'))
from lockstep.launcher import extension
import linux_gate_live as live
import native_gate_scenarios as scenarios

SLOT = 'save_009.xml.gz'
LOCK_TARGET = Path('/tmp/EGOSOFT_X4.lock')


def validate_save(path):
    """Consume the entire gzip and XML stream, including the gzip CRC and trailer."""
    root = None
    with gzip.open(path, 'rb') as stream:
        for event, element in ET.iterparse(stream, events=('start', 'end')):
            if root is None:
                root = element.tag
            if event == 'end':
                element.clear()
    if root != 'savegame':
        raise ValueError(f'unexpected save XML root: {root!r}')


def output_path(path):
    local = (REPO / '.local').resolve()
    out = path.absolute()
    if out.exists() or out.is_symlink():
        raise ValueError('output already exists; choose a fresh directory')
    if not out.resolve().is_relative_to(local) or out.resolve() == local:
        raise ValueError('output must be inside the repository .local directory')
    return out


def prepare_case(out, game, home):
    case = out / 'case'
    profile = case / 'home/.config/EgoSoft/X4/45841'
    for path in (profile / 'save', case / 'tmp', case / 'cache', case / 'data', case / 'runtime',
                 case / 'game', case / 'extensions', case / 'home/.steam'):
        path.mkdir(parents=True, exist_ok=True)
    (case / 'runtime').chmod(0o700)
    (profile / 'config.xml').write_text(
        '<?xml version="1.0"?><root><autosave>false</autosave><uisafemode>false</uisafemode>'
        '<fullscreen>false</fullscreen><borderless>false</borderless><res_width>800</res_width>'
        '<res_height>600</res_height><antialiasing>0</antialiasing><vsync>false</vsync></root>\n')
    for name in ('root', 'steam.pid', 'sdk32', 'sdk64'):
        source = home / '.steam' / name
        if source.exists():
            (case / 'home/.steam' / name).symlink_to(source)
    (case / 'home/.steam/steam').symlink_to('root')
    binds = []
    for dlc in sorted((game / 'extensions').glob('ego_dlc_*')):
        if dlc.is_dir() and re.fullmatch(r'ego_dlc_[a-z0-9_]+', dlc.name):
            view = case / 'dlc-source' / dlc.name
            view.mkdir(parents=True)
            binds.append((dlc.resolve(strict=True), view))
            (case / 'extensions' / dlc.name).symlink_to(view)
    overlay = scenarios.build_capture_overlay('custom_creative', None, out / 'overlay')
    extension.compose(overlay, case / 'extensions', 1, 0, '0' * 32, 1,
        extra={'ui/x4ls_autostart.lua': REPO / 'src/lockstep/launcher/autostart.lua'},
        config_tail='X4LAN_CONFIG.x4ls_autostart = "custom_creative"\n')
    ids = sorted(p.name for p in (case / 'extensions').iterdir())
    (profile / 'content.xml').write_text('<?xml version="1.0"?><content>' + ''.join(
        f'<extension id="{name}" enabled="true" />' for name in ids) + '</content>\n')
    lock = case / 'x4.lock'
    lock.touch(exist_ok=False)
    return case, profile, binds, lock


def command_for(case, game, binds, lock, display, auth, username, seed=276944566):
    env = {'PATH': '/usr/local/bin:/usr/bin:/bin', 'HOME': str(case / 'home'), 'USER': username,
           'LOGNAME': username, 'DISPLAY': display, 'XAUTHORITY': str(auth),
           'XDG_CONFIG_HOME': str(case / 'home/.config'), 'XDG_CACHE_HOME': str(case / 'cache'),
           'XDG_DATA_HOME': str(case / 'data'), 'XDG_RUNTIME_DIR': str(case / 'runtime'),
           'TMPDIR': str(case / 'tmp'), 'SteamAppId': '392160', 'SteamGameId': '392160',
           'LD_LIBRARY_PATH': str(case / 'game/lib'), 'SDL_AUDIODRIVER': 'dummy', 'ALSOFT_DRIVERS': 'null'}
    program = [str(case / 'game/X4'), '-personalfolderid', '45841', '-skipintro', '-debug', 'scripts',
               '-logfile', 'engine-output.log', '-nocputhrottle', '-noworkshopsync', f'-seed:{seed}']
    return live.namespace_command(case, game, binds, lock, env, program, username)


def owned_group_members(group_id, started):
    """Report surviving members of the launched process group, without signalling them."""
    found = []
    for path in Path('/proc').glob('[0-9]*'):
        try:
            fields = (path / 'stat').read_text().rsplit(')', 1)[1].split()
            if (path.stat().st_uid == os.geteuid() and int(fields[2]) == group_id
                    and int(fields[19]) >= started):
                found.append(int(path.name))
        except (OSError, ValueError, IndexError):
            continue
    return sorted(found)


def stop_owned(proc, started):
    if proc is None:
        return []
    # Never signal a reaped process or a recycled PID. The namespace uses
    # --die-with-parent, and each launch owns its own process group.
    if proc.poll() is None:
        try:
            if live.start_ticks(proc.pid) == started:
                os.killpg(proc.pid, signal.SIGTERM)
                try:
                    proc.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    if proc.poll() is None and live.start_ticks(proc.pid) == started:
                        os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait(timeout=10)
        except ProcessLookupError:
            pass
    deadline = time.monotonic() + 5
    while True:
        remaining = owned_group_members(proc.pid, started)
        if not remaining or time.monotonic() >= deadline:
            return remaining
        time.sleep(0.1)


def cancel_capture(number, frame):
    raise InterruptedError(f'signal {number}')


def capture(args):
    if sys.platform != 'linux' or os.geteuid() == 0:
        raise ValueError('run as the unprivileged Steam desktop user on Linux')
    out = output_path(args.out)
    game = args.game.resolve(strict=True)
    if live.sha(game / 'X4') != live.EXE_SHA:
        raise ValueError('X4 executable is not the pinned Linux build')
    if not (game / 'extensions').is_dir() or not shutil.which('bwrap'):
        raise ValueError('game extensions directory or bwrap missing')
    if live.x4_executables():
        raise ValueError('another X4 process is running')
    if not re.fullmatch(r':[0-9]+(?:\.[0-9]+)?', args.display):
        raise ValueError('display must be a local X11 display such as :0')
    auth = Path(os.environ.get('XAUTHORITY', ''))
    if not auth.is_file() or not os.access(auth, os.R_OK):
        raise ValueError('a readable XAUTHORITY is required')
    auth = auth.resolve(strict=True)
    import pwd
    user = pwd.getpwuid(os.geteuid())
    home = Path(user.pw_dir)
    steam = live.steam_identity(home, user.pw_uid)
    if args.timeout <= 0:
        raise ValueError('timeout must be positive')
    if not 0 <= args.seed <= 0xffffffff:
        raise ValueError('seed must be an unsigned 32-bit integer')
    out.mkdir(parents=True, exist_ok=False)
    result = {'success': False, 'steam_before': steam, 'requested_seed': args.seed}
    proc = None
    started = None
    lock_stat = None
    previous = {}
    try:
        case, profile, binds, lock = prepare_case(out, game, home)
        # Atomic creation prevents taking ownership of another program's lock target.
        fd = os.open(LOCK_TARGET, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
        lock_stat = os.fstat(fd)
        os.close(fd)
        for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
            previous[sig] = signal.signal(sig, cancel_capture)
        with (out / 'process.log').open('wb') as log:
            proc = subprocess.Popen(command_for(case, game, binds, lock, args.display, auth, user.pw_name, args.seed),
                                    stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                    start_new_session=True)
            started = live.start_ticks(proc.pid)
            deadline = time.monotonic() + args.timeout
            engine_log = profile / 'engine-output.log'
            while True:
                if live.steam_identity(home, user.pw_uid) != steam:
                    raise RuntimeError('Steam identity changed during capture')
                text = live.bounded_tail(engine_log)
                script_error = re.search(
                    r'Error (?:while executing.*native_gate_snapshot|in MD cue md.X4LAN_NativeGate)',
                    text, re.I)
                if 'NATIVE_SNAPSHOT ERROR' in text or script_error:
                    raise RuntimeError('snapshot script or fixture error; inspect engine-output.log')
                if 'NATIVE_SNAPSHOT SAVED' in text:
                    break
                if proc.poll() is not None:
                    raise RuntimeError(f'X4 exited before save (status {proc.returncode}); inspect logs')
                if time.monotonic() >= deadline:
                    raise TimeoutError('capture timed out during start, load or save; inspect logs')
                time.sleep(1)
            save = profile / 'save' / SLOT
            validate_save(save)
            shutil.copyfile(save, out / SLOT)
            validate_save(out / SLOT)
            if live.snapshot_seed(out / SLOT) != args.seed:
                raise ValueError('captured save does not contain the requested world seed')
            result.update(success=True, save_sha256=live.sha(out / SLOT))
    except (Exception, KeyboardInterrupt) as error:
        result['error'] = str(error)
    finally:
        try:
            remaining = stop_owned(proc, started) if started is not None else []
            result['owned_processes_remaining'] = remaining
            if remaining:
                result.update(success=False, cleanup_error=f'owned processes remain: {remaining}')
        except (OSError, subprocess.TimeoutExpired) as error:
            result.update(success=False, cleanup_error=str(error))
        if lock_stat:
            try:
                current = LOCK_TARGET.lstat()
                if (current.st_dev, current.st_ino) == (lock_stat.st_dev, lock_stat.st_ino):
                    LOCK_TARGET.unlink()
                else:
                    result.update(success=False, cleanup_error='lock target replaced; left untouched')
            except FileNotFoundError:
                result.update(success=False, cleanup_error='lock target disappeared')
        for sig, handler in previous.items():
            signal.signal(sig, handler)
        try:
            result['steam_after'] = live.steam_identity(home, user.pw_uid)
        except (OSError, ValueError, RuntimeError) as error:
            result['steam_after'] = {'error': str(error)}
        if result['steam_after'] != steam:
            result.update(success=False, error='Steam identity changed; inspect logs')
        (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', required=True, type=Path, help='X4 installation directory')
    parser.add_argument('--out', required=True, type=Path, help='fresh directory under .local')
    parser.add_argument('--display', default=':0')
    parser.add_argument('--timeout', type=float, default=900)
    parser.add_argument('--seed', type=int, default=276944566, help='world seed, recorded in the captured save')
    args = parser.parse_args()
    try:
        result = capture(args)
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f'REFUSED: {error}\n')
    print(json.dumps(result, indent=2))
    return 0 if result['success'] else 1


if __name__ == '__main__':
    sys.exit(main())

"""Isolated Windows-under-Proton client for controlled X4 lockstep comparisons.

This is a developer test runner. Preflight is read-only and run creates a new
private directory beneath the repository's .local. No native Linux profile,
Steam compatdata, or installed game or Proton files are changed.
"""
import argparse
import gzip
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import select
import shutil
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.launcher import extension, game, profile, session
from linux_gate_live import LogWatch, start_ticks, stop_owned_process, wait_owned_groups


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def validate(args, now=None):
    """Validate the session and all immutable inputs without creating files."""
    try:
        host = str(ipaddress.IPv4Address(args.relay_host))
    except ipaddress.AddressValueError:
        raise ValueError('relay host must be a dotted IPv4 address') from None
    if not 1024 <= args.relay_port <= 65535 or not re.fullmatch(r'[0-9a-f]{32}', args.relay_token):
        raise ValueError('relay port must be 1024..65535 and token must be 32 lowercase hex digits')
    if not 1 <= args.slot <= args.clients <= 8 or args.clients < 2:
        raise ValueError('slot must be 1..clients and clients must be 2..8')
    if args.startup < 1 or args.timeout < 1 or args.width < 1 or args.height < 1 or args.expected_ticks < 1:
        raise ValueError('startup, timeout, expected ticks and window dimensions must be positive')
    if args.arm_at <= (time.time() if now is None else now) + args.startup:
        raise ValueError('arm time is too close for the requested startup budget')
    windows = args.windows.resolve(strict=True)
    data = args.data.resolve(strict=True)
    proton = args.proton.resolve(strict=True)
    overlay = args.overlay.resolve(strict=True)
    snapshot = args.snapshot.resolve(strict=True)
    game.verify_exe(windows)
    if not (windows / 'steam_api64.dll').is_file():
        raise ValueError('Windows depot is missing steam_api64.dll')
    if not (data / '01.cat').is_file() or not (data / '01.dat').is_file():
        raise ValueError('game data directory lacks 01.cat or 01.dat')
    if not (proton / 'proton').is_file() or not (proton / 'files/share/default_pfx').is_dir():
        raise ValueError('Proton directory lacks proton or its default prefix')
    manifest = extension.verify_overlay(overlay)
    if 'native/x4lan_gate_64.dll' not in manifest:
        raise ValueError('overlay has no manifest-verified Windows gate DLL')
    snapshot_hash = sha256(snapshot)
    config = (overlay / 'ui/config.lua.append').read_text(encoding='utf-8')
    pins = re.findall(r'^\s*X4LAN_CONFIG\.native_gate_snapshot_sha\s*=\s*["\']([0-9a-f]{64})["\']\s*$', config, re.M)
    if pins != [snapshot_hash]:
        raise ValueError('overlay must pin this snapshot exactly once')
    try:
        with gzip.open(snapshot, 'rb') as source:
            header = source.read(8192)
    except (OSError, EOFError) as error:
        raise ValueError('snapshot is not a readable gzip save') from error
    match = re.search(rb'<game\s[^>]*\bseed="(\d+)"', header)
    if not match or int(match.group(1)) > 0xffffffff:
        raise ValueError('snapshot header has no valid 32-bit world seed')
    if not args.display or not re.fullmatch(r':\d+(?:\.\d+)?', args.display):
        raise ValueError('display must be a local X11 display such as :0')
    authority = args.xauthority.resolve(strict=True)
    if not authority.is_file():
        raise ValueError('Xauthority must be a file')
    steam = args.steam.resolve(strict=True)
    if not (steam / 'legacycompat/steamclient64.dll').is_file():
        raise ValueError('Steam legacycompat/steamclient64.dll is missing')
    if shutil.which('bwrap') is None:
        raise ValueError('bubblewrap is missing')
    local = (ROOT / '.local').resolve(strict=True)
    out = args.out.absolute()
    if out.exists() or out.is_symlink() or not out.parent.resolve(strict=True).is_relative_to(local):
        raise ValueError('output must be a new directory directly within an existing .local subtree')
    return dict(relay_host=host, snapshot_sha256=snapshot_hash, seed=int(match.group(1)),
                overlay_manifest_sha256=sha256(overlay / 'x4lane-manifest.json'),
                windows_exe_sha256=game.EXE_SHA256, windows=str(windows), data=str(data),
                proton=str(proton), steam=str(steam), overlay=str(overlay), snapshot=str(snapshot),
                display=args.display, xauthority=str(authority), slot=args.slot, clients=args.clients,
                relay_port=args.relay_port, arm_at=args.arm_at, expected_ticks=args.expected_ticks, fast=args.fast)


def make_view(run, windows, data):
    """Point at read-only source files; extension directory is private and allowlisted."""
    view = run / 'game'
    view.mkdir()
    for source in (data, windows):
        for entry in source.iterdir():
            if entry.name == 'extensions':
                continue
            target = view / entry.name
            if target.is_symlink():
                target.unlink()
            elif target.exists():
                raise ValueError(f'conflicting game view entry: {target.name}')
            target.symlink_to(entry.resolve(strict=True), target_is_directory=entry.is_dir())
    extensions = view / 'extensions'
    extensions.mkdir()
    for source in sorted((data / 'extensions').glob('ego_dlc_*')):
        if source.is_dir():
            (extensions / source.name).symlink_to(source.resolve(strict=True), target_is_directory=True)
    return view


def namespace(run, env, program):
    """Only this run is writable; Wine's Z: drive sees a read-only host."""
    command = ['bwrap', '--ro-bind', '/', '/', '--proc', '/proc', '--dev-bind', '/dev', '/dev',
               '--bind', str(run), str(run), '--bind', str(run / 'tmp'), '/tmp',
               '--ro-bind', '/tmp/.X11-unix', '/tmp/.X11-unix', '--die-with-parent',
               '--chdir', str(run / 'game'), '--clearenv']
    for key, value in env.items():
        command += ['--setenv', key, str(value)]
    return command + ['--', *map(str, program)]


def prefix_processes(prefix):
    """List same-user processes explicitly carrying this private WINEPREFIX."""
    found = []
    for path in Path('/proc').glob('[0-9]*'):
        try:
            if path.stat().st_uid == os.geteuid() and prefix_env_matches((path / 'environ').read_bytes(), prefix):
                found.append(int(path.name))
        except (OSError, ValueError):
            continue
    return sorted(found)


def prefix_env_matches(environ, prefix):
    """Match an absolute WINEPREFIX after normalizing redundant slashes."""
    expected = os.path.normpath(os.fsencode(prefix))
    for item in environ.split(b'\0'):
        if item.startswith(b'WINEPREFIX='):
            value = item[len(b'WINEPREFIX='):]
            return os.path.isabs(value) and os.path.normpath(value) == expected
    return False


def prefix_process_matches(pid, prefix, started):
    """Recheck UID, start identity and exact prefix before signalling."""
    path = Path('/proc') / str(pid)
    try:
        return (start_ticks(pid) == started and path.stat().st_uid == os.geteuid()
                and prefix_env_matches((path / 'environ').read_bytes(), prefix)
                and start_ticks(pid) == started)
    except (OSError, ValueError):
        return False


def wait_pidfd(fd, timeout_ms):
    poller = select.poll()
    poller.register(fd, select.POLLIN)
    return bool(poller.poll(timeout_ms))


def stop_prefix_process(pid, prefix):
    """Signal only the authenticated process represented by this pidfd."""
    try:
        started = start_ticks(pid)
        fd = os.pidfd_open(pid, 0)
    except (FileNotFoundError, ProcessLookupError):
        return
    try:
        if not prefix_process_matches(pid, prefix, started):
            return
        signal.pidfd_send_signal(fd, signal.SIGTERM)
        if not wait_pidfd(fd, 2000) and prefix_process_matches(pid, prefix, started):
            signal.pidfd_send_signal(fd, signal.SIGKILL)
            if not wait_pidfd(fd, 2000):
                raise RuntimeError(f'private prefix process {pid} did not exit')
    except ProcessLookupError:
        pass
    finally:
        os.close(fd)


def monitor_game(process, logfile, timeout, slot, expected_ticks):
    """End on the gate's STOP or failure, not on the long-lived game process."""
    watch = LogWatch(logfile)
    deadline = time.monotonic() + timeout
    while True:
        watch.poll()
        if watch.failure:
            return {'status': 'failed', 'error': watch.failure, 'gate_armed': watch.armed}
        if watch.stopped:
            stop = None
            with logfile.open(encoding='utf-8', errors='replace') as source:
                for line in source:
                    match = re.search(r'NATIVE_GATE STOP slot=p(\d+) completed=(\d+) reason=(\d+) restored=(\d+)(?:\s|$)', line)
                    if match:
                        stop = tuple(map(int, match.groups()))
            valid = watch.armed and stop == (slot, expected_ticks, 1, 1)
            return {'status': 'stopped' if valid else 'failed', 'gate_armed': watch.armed,
                    'gate_stop': stop, 'error': None if valid else 'gate STOP did not match expected slot, tick, reason and restore'}
        if process.poll() is not None:
            watch.poll()
            if watch.stopped or watch.failure:
                continue
            return {'status': 'failed', 'error': 'game exited before gate STOP',
                    'exit_code': process.returncode, 'gate_armed': watch.armed}
        if time.monotonic() >= deadline:
            return {'status': 'failed', 'error': 'timeout waiting for gate STOP', 'gate_armed': watch.armed}
        time.sleep(0.2)


def cleanup_prefix(output, env, processes):
    """Stop only this private prefix and report survivors of our own groups."""
    errors = []
    prefix = output / 'compat/pfx'
    if prefix.is_dir():
        cleanup_env = dict(env, WINEPREFIX=str(prefix))
        server = output / 'proton/files/bin/wineserver'
        with (output / 'proton-cleanup.log').open('ab') as log:
            for option in ('-k', '-w'):
                try:
                    result = subprocess.run(namespace(output, cleanup_env, [server, option]),
                                            stdout=log, stderr=subprocess.STDOUT, timeout=10)
                    if result.returncode:
                        errors.append(f'wineserver {option} exited {result.returncode}')
                except (OSError, subprocess.TimeoutExpired) as error:
                    errors.append(f'wineserver {option}: {error}')
    for process, started in processes:
        try:
            stop_owned_process(process, started)
        except (OSError, subprocess.SubprocessError) as error:
            errors.append(f'owned process cleanup: {error}')
    for pid in prefix_processes(prefix) if prefix.is_dir() else []:
        try:
            stop_prefix_process(pid, prefix)
        except (OSError, RuntimeError) as error:
            errors.append(f'private prefix process cleanup: {error}')
    groups = wait_owned_groups({process.pid: started for process, started in processes}, timeout=5)
    remaining = prefix_processes(prefix) if prefix.is_dir() else []
    if groups or remaining:
        errors.append('owned processes remain after cleanup')
    return {'cleanup_errors': errors, 'owned_groups_remaining': groups, 'prefix_processes_remaining': remaining}


def run(args, info):
    output = args.out.absolute()
    output.mkdir(mode=0o700, exist_ok=False)
    output.chmod(0o700)
    receipt = dict(info, status='staging', steam_connected=None, gate_armed=False)
    receipt_path = output / 'result.json'
    env = None
    processes = []

    def save():
        receipt_path.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')

    save()
    try:
        for path in ('home', 'tmp', 'cache', 'data', 'runtime', 'compat'):
            (output / path).mkdir(mode=0o700)
        staged_overlay = output / 'overlay'
        staged_snapshot = output / 'snapshot.xml.gz'
        shutil.copytree(args.overlay, staged_overlay, symlinks=False)
        shutil.copyfile(args.snapshot, staged_snapshot)
        extension.verify_overlay(staged_overlay)
        if (sha256(staged_snapshot) != info['snapshot_sha256'] or
                sha256(staged_overlay / 'x4lane-manifest.json') != info['overlay_manifest_sha256']):
            raise ValueError('snapshot or overlay changed during staging')
        subprocess.run(['cp', '-a', '--reflink=auto', '--', info['proton'], str(output / 'proton')], check=True)
        view = make_view(output, Path(info['windows']), Path(info['data']))
        for name in ('root', 'steam.pid', 'steam.pipe', 'steam.token', 'bin32', 'bin64', 'sdk32', 'sdk64'):
            source = Path.home() / '.steam' / name
            if source.exists():
                (output / 'home/.steam').mkdir(exist_ok=True)
                (output / 'home/.steam' / name).symlink_to(source.resolve(strict=True))
        steam_home = output / 'home/.steam'
        steam_home.mkdir(exist_ok=True)
        (steam_home / 'steam').symlink_to('root')
        env = {'PATH': '/usr/local/bin:/usr/bin:/bin', 'HOME': str(output / 'home'),
               'XDG_CONFIG_HOME': str(output / 'home/.config'), 'XDG_CACHE_HOME': str(output / 'cache'),
               'XDG_DATA_HOME': str(output / 'data'), 'XDG_RUNTIME_DIR': str(output / 'runtime'),
               'TMPDIR': str(output / 'tmp'), 'DISPLAY': info['display'], 'XAUTHORITY': info['xauthority'],
               'STEAM_COMPAT_DATA_PATH': str(output / 'compat'), 'STEAM_COMPAT_CLIENT_INSTALL_PATH': info['steam'],
               'STEAM_COMPAT_INSTALL_PATH': str(view), 'STEAM_COMPAT_LIBRARY_PATHS': str(view.parent),
               'SteamAppId': game.APP_ID, 'SteamGameId': game.APP_ID, 'PYTHONDONTWRITEBYTECODE': '1'}
        proton = output / 'proton/proton'
        with (output / 'proton-init.log').open('wb') as log:
            proc = subprocess.Popen(namespace(output, env, [proton, 'run', 'wineboot.exe', '-u']),
                                    stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            processes.append((proc, start_ticks(proc.pid)))
            code = proc.wait(timeout=args.timeout)
            if code:
                raise RuntimeError(f'private Proton prefix initialization failed with exit code {code}')
        documents = output / 'compat/pfx/drive_c/users/steamuser/Documents'
        if not documents.resolve(strict=True).is_relative_to(output.resolve()):
            raise ValueError('Proton Documents resolves outside the private run')
        if time.time() + args.startup >= args.arm_at:
            raise ValueError('arm time is too close after prefix initialization')
        target = extension.compose(staged_overlay, view / 'extensions', args.slot, args.relay_port, args.relay_token,
                                   args.clients, profile_id=profile.PROFILES[args.slot - 1],
                                   config_tail=session.client_tail(args.arm_at, relay_host=info['relay_host']))
        root = documents / 'Egosoft/X4'
        if not root.resolve().is_relative_to(output.resolve()):
            raise ValueError('profile root resolves outside the private run')
        profile_dir = profile.prepare(profile.PROFILES[args.slot - 1], [*game.dlc_ids(view), extension.EXTENSION_ID],
                                      args.width, args.height, staged_snapshot, root=root, fast=args.fast)
        if sha256(profile_dir / 'save' / profile.SNAPSHOT_SLOT) != info['snapshot_sha256']:
            raise ValueError('staged snapshot differs from verified snapshot')
        if target.is_symlink():
            raise ValueError('staged extension is a symlink')
        receipt['status'] = 'starting'
        save()
        command = session.client_command(view, profile.PROFILES[args.slot - 1], args.slot, info['seed'])
        logfile = profile_dir / f'x4ls-c{args.slot}.log'
        receipt['game_log'] = str(logfile)
        with (output / 'proton.log').open('wb') as log:
            proc = subprocess.Popen(namespace(output, env, [proton, 'run', *command]), stdout=log,
                                    stderr=subprocess.STDOUT, start_new_session=True)
            processes.append((proc, start_ticks(proc.pid)))
            receipt.update(monitor_game(proc, logfile, args.timeout, args.slot, args.expected_ticks))
    except BaseException as error:
        receipt.update(status='failed', error=str(error))
        raise
    finally:
        if env is not None:
            try:
                cleanup = cleanup_prefix(output, env, processes)
                receipt.update(cleanup)
                if cleanup['cleanup_errors']:
                    receipt['status'] = 'failed'
            except Exception as error:
                receipt.update(status='failed', cleanup_errors=[str(error)])
        save()
    return receipt


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('preflight', 'run'))
    for name in ('windows', 'data', 'proton', 'overlay', 'snapshot', 'steam', 'out', 'xauthority'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--display', required=True)
    parser.add_argument('--relay-host', required=True)
    parser.add_argument('--relay-port', type=int, required=True)
    parser.add_argument('--relay-token', required=True)
    parser.add_argument('--slot', type=int, required=True)
    parser.add_argument('--clients', type=int, required=True)
    parser.add_argument('--arm-at', type=int, required=True)
    parser.add_argument('--startup', type=int, default=70)
    parser.add_argument('--timeout', type=int, default=3600)
    parser.add_argument('--expected-ticks', type=int, required=True, help='expected completed tick at clean gate STOP')
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--height', type=int, default=720)
    parser.add_argument('--fast', action='store_true', help='use the unattended diagnostic graphics/audio profile')
    args = parser.parse_args(argv)
    try:
        info = validate(args)
        if args.command == 'preflight':
            print(json.dumps(dict(info, status='preflight_ok'), indent=2))
        else:
            def interrupted(signum, _frame):
                raise RuntimeError(f'interrupted by signal {signum}')
            previous = {sig: signal.signal(sig, interrupted) for sig in (signal.SIGTERM, signal.SIGHUP)}
            try:
                result = run(args, info)
            finally:
                for sig, handler in previous.items():
                    signal.signal(sig, handler)
            print(json.dumps(result, indent=2))
            if result['status'] != 'stopped':
                parser.exit(1)
    except (ValueError, RuntimeError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, f'REFUSED: {error}\n')


if __name__ == '__main__':
    main()

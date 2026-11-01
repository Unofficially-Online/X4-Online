"""Steam client lookup for directly started X4 clients.

X4's steam_api64.dll finds the running client through
HKCU\\Software\\Valve\\Steam\\ActiveProcess (pid, SteamClientDll64). Current Steam
clients can leave pid at 0; then every directly started X4 stops with "Steam client
is required" and only Steam's own launch works, which allows one instance. The
launcher never writes that key itself: `x4ls steam-fix` is a separate command the
user runs, recording the old value so `x4ls steam-fix --restore` can put it back.
"""
import json
import os
from pathlib import Path
import subprocess

KEY = r'Software\Valve\Steam\ActiveProcess'
APP_ID = '392160'


def steam_pids():
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq steam.exe', '/FO', 'CSV', '/NH'],
                         capture_output=True, text=True, check=True).stdout
    return [int(line.split('","')[1]) for line in out.splitlines() if line.lower().startswith('"steam.exe"')]


def registered_pid():
    import winreg
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, KEY) as key:
        return int(winreg.QueryValueEx(key, 'pid')[0])


def status():
    """(ok, message): whether a directly started X4 can reach the Steam client."""
    running = steam_pids()
    if not running:
        return False, 'Steam is not running. Start Steam and log in first.'
    pid = registered_pid()
    if pid in running:
        return True, f'Steam pid {pid} is registered.'
    return False, (f'Steam runs as pid {running[0]} but ActiveProcess pid is {pid}; directly started X4 '
                   'clients will fail with "Steam client is required". Run: x4ls steam-fix '
                   '(and x4ls steam-fix --restore when done).')


def fix(state_file: Path):
    """Register the running Steam pid; keep the previous value in state_file."""
    import winreg
    running = steam_pids()
    if len(running) != 1:
        raise SystemExit(f'expected exactly one steam.exe, found {running}')
    previous = registered_pid()
    if not state_file.exists():
        state_file.parent.mkdir(parents=True, exist_ok=True)
        state_file.write_text(json.dumps({'previous_pid': previous}) + '\n')
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, KEY, 0, winreg.KEY_SET_VALUE) as key:
        winreg.SetValueEx(key, 'pid', 0, winreg.REG_DWORD, running[0])
    return previous, running[0]


def restore(state_file: Path):
    import winreg
    if not state_file.exists():
        raise SystemExit('nothing to restore: no recorded steam-fix')
    previous = int(json.loads(state_file.read_text())['previous_pid'])
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, KEY, 0, winreg.KEY_SET_VALUE) as key:
        winreg.SetValueEx(key, 'pid', 0, winreg.REG_DWORD, previous)
    state_file.unlink()
    return previous


def client_environment():
    """The process environment plus the app id. Nothing else is copied from Steam."""
    return dict(os.environ, SteamAppId=APP_ID, SteamGameId=APP_ID)

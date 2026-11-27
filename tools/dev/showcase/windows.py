"""X4 client windows of one lanes directory (the showcase's own clients only).

A window is taken only when its process is X4.exe started from LANES/c<K>; nothing else on the desktop is ever
named to the recorder, so a capture holds X4's picture and nothing of the PC around it."""
import ctypes
import json
import re
import subprocess
from ctypes import wintypes

user32 = ctypes.windll.user32 if hasattr(ctypes, 'windll') else None      # Windows only; importable anywhere
BACKSLASH = chr(92)


def processes():
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
                          "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine } | "
                          "Select-Object ProcessId,Name,CommandLine,ExecutablePath | ConvertTo-Json -Compress"],
                         capture_output=True, text=True, timeout=120).stdout
    rows = json.loads(out or '[]')
    return rows if isinstance(rows, list) else [rows]


def lane_processes(lanes, rows=None):
    """Every process whose image or command line names the lanes directory (for stopping one's own runs)."""
    want = lanes.replace('/', BACKSLASH).rstrip(BACKSLASH).lower()
    return [row for row in (rows if rows is not None else processes())
            if want in ((row.get('ExecutablePath') or '') + ' ' + (row.get('CommandLine') or ''))
            .replace('/', BACKSLASH).lower()]


def lane_clients(lanes):
    """{client number: pid} of the X4.exe processes whose image or command line lies in lanes/c<K>."""
    want = lanes.replace('/', BACKSLASH).rstrip(BACKSLASH).lower() + BACKSLASH
    found = {}
    for row in processes():
        if (row.get('Name') or '').lower() != 'x4.exe':
            continue
        text = ((row.get('ExecutablePath') or '') + ' ' + (row.get('CommandLine') or '')).replace('/', BACKSLASH).lower()
        at = text.find(want)
        if at < 0:
            continue
        match = re.match(r'[a-z]*?(\d+)', text[at + len(want):])
        if match:
            found[int(match.group(1))] = int(row['ProcessId'])
    return found


def main_window(pid):
    """The largest visible top-level window of pid: (hwnd, (width, height) of its client area)."""
    best = [0, (0, 0)]

    @ctypes.WINFUNCTYPE(ctypes.c_int, wintypes.HWND, wintypes.LPARAM)
    def each(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            rect = wintypes.RECT()
            user32.GetClientRect(hwnd, ctypes.byref(rect))
            if rect.right * rect.bottom > best[1][0] * best[1][1]:
                best[0], best[1] = hwnd, (rect.right, rect.bottom)
        return 1
    user32.EnumWindows(each, 0)
    return best[0], best[1]


def client_windows(lanes):
    """{client number: (hwnd, (w, h))} for every client of lanes that has a window."""
    if not getattr(client_windows, 'aware', False):
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(2)
        except OSError:
            pass
        client_windows.aware = True
    out = {}
    for number, pid in sorted(lane_clients(lanes).items()):
        hwnd, size = main_window(pid)
        if hwnd:
            out[number] = (hwnd, size)
    return out


if __name__ == '__main__':
    import sys
    for number, (hwnd, size) in client_windows(sys.argv[1]).items():
        print(number, hwnd, size)

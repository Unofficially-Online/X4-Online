"""Input for ONE X4 test client by window messages: no system cursor, no keyboard focus (tools/dev/station_build_test.py
--input window, the default). A person can use the PC while a test clicks through the game's menus, and two tests
on the same PC do not fight for the one mouse (tools/dev/ui_clicks.py moves the real cursor and takes the focus).

How X4 9.00 takes the mouse (read from the image, 18 Nov 2026): the window procedure (0x12aea30) takes the pointer's
place from lParam and the buttons from the MK_ flags of wParam of every mouse message; the per-frame mouse update
(0xdc8c0) does nothing unless GetForegroundWindow() is X4's own window; hit tests ask GetCursorPos. `on` points three
import slots of THAT test client's X4.exe image (GetForegroundWindow, GetCursorPos, SetCursorPos) at small stubs in
memory of its own: the foreground window is its own, the pointer is where this tool's last message put it, and X4
never moves the real pointer. `off` restores the slots. For disposable test clients only (the launcher's profiles);
nothing of this is part of the mod or runs in a player's game.

Limits: the wheel does not arrive (X4 reads it elsewhere), and of the keys only those X4 takes from messages while its
DirectInput keyboard is in use: Enter, Esc, Tab, the arrows. The map opens by the HUD's top bar (a click at the top
centre, then the map symbol). A click is down and up within 0.2 s: the station menu takes a longer press as a drag.

  ui_window_input.py PID on | off | move FX FY | click FX FY | rclick FX FY | dclick FX FY | drag FX FY FX2 FY2 [right] | key VK
Fractions of the client area (= of a PrintWindow picture), as ui_clicks.py. From Python: act(pid, action), release(pid)."""
import ctypes
import json
import struct
import sys
import time
from ctypes import wintypes
from pathlib import Path

ctypes.windll.shcore.SetProcessDpiAwareness(2)
u = ctypes.windll.user32
k = ctypes.windll.kernel32
psapi = ctypes.windll.psapi
u.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
u.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
u.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
u.MapVirtualKeyW.argtypes = [wintypes.UINT, wintypes.UINT]
k.OpenProcess.restype = wintypes.HANDLE
k.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
k.VirtualAllocEx.restype = ctypes.c_void_p
k.VirtualAllocEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
k.VirtualProtectEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
k.WriteProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
psapi.EnumProcessModules.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_void_p), wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
WM_MOUSEMOVE, WM_LDOWN, WM_LUP, WM_LDBL, WM_RDOWN, WM_RUP, WM_KEYDOWN, WM_KEYUP, WM_WHEEL = 0x200, 0x201, 0x202, 0x203, 0x204, 0x205, 0x100, 0x101, 0x20A
IAT = {'GetCursorPos': 0x1bc1918, 'SetCursorPos': 0x1bc1920, 'GetForegroundWindow': 0x1bc1948}      # X4 9.00 import slots (RVA)
STATE = Path(__file__).resolve().parents[2] / '.local' / 'ui-window-input'


def window_of(pid):
    found = []

    def each(hwnd, _):
        p = wintypes.DWORD()
        u.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(hwnd):
            r = wintypes.RECT()
            u.GetClientRect(hwnd, ctypes.byref(r))
            if r.right > 600:
                found.append(hwnd)
        return True

    u.EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(each), 0)
    return found[0]


def process(pid):
    h = k.OpenProcess(0x1F0FFF, False, pid)
    assert h, 'OpenProcess failed'
    return h


def base_of(h):
    mods = (ctypes.c_void_p * 4)()
    need = wintypes.DWORD()
    assert psapi.EnumProcessModules(h, mods, ctypes.sizeof(mods), ctypes.byref(need))
    return mods[0]


def read(h, address, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    assert k.ReadProcessMemory(h, ctypes.c_void_p(address), buf, size, ctypes.byref(n)), 'read failed at %x' % address
    return buf.raw


def write(h, address, data, protect=False):
    old = wintypes.DWORD()
    if protect:
        assert k.VirtualProtectEx(h, ctypes.c_void_p(address), len(data), 0x04, ctypes.byref(old))
    n = ctypes.c_size_t()
    ok = k.WriteProcessMemory(h, ctypes.c_void_p(address), data, len(data), ctypes.byref(n))
    if protect:
        k.VirtualProtectEx(h, ctypes.c_void_p(address), len(data), old.value, ctypes.byref(old))
    assert ok, 'write failed at %x' % address


def state_file(pid):
    STATE.mkdir(parents=True, exist_ok=True)
    return STATE / ('%d.json' % pid)


def on(pid, hwnd):
    if state_file(pid).exists():
        return json.loads(state_file(pid).read_text())
    h = process(pid)
    base = base_of(h)
    page = k.VirtualAllocEx(h, None, 0x1000, 0x3000, 0x40)
    assert page, 'VirtualAllocEx failed'
    point = page + 0x800                                    # the pointer's place on the screen (POINT), written before each message
    get_cursor = b'\x48\xb8' + struct.pack('<Q', point) + b'\x48\x8b\x00\x48\x89\x01\xb8\x01\x00\x00\x00\xc3'
    foreground = b'\x48\xb8' + struct.pack('<Q', hwnd) + b'\xc3'
    yes = b'\xb8\x01\x00\x00\x00\xc3'
    write(h, page, get_cursor)
    write(h, page + 0x40, foreground)
    write(h, page + 0x80, yes)
    p = wintypes.POINT(10, 10)
    u.ClientToScreen(hwnd, ctypes.byref(p))
    write(h, point, struct.pack('<ii', p.x, p.y))
    saved = {}
    for name, stub in (('GetCursorPos', page), ('GetForegroundWindow', page + 0x40), ('SetCursorPos', page + 0x80)):
        slot = base + IAT[name]
        saved[name] = struct.unpack('<Q', read(h, slot, 8))[0]
        write(h, slot, struct.pack('<Q', stub), protect=True)
    state = {'pid': pid, 'base': base, 'page': page, 'point': point, 'saved': saved}
    state_file(pid).write_text(json.dumps(state))
    return state


def off(pid):
    if not state_file(pid).exists():
        return
    state = json.loads(state_file(pid).read_text())
    h = process(pid)
    for name, original in state['saved'].items():
        write(h, state['base'] + IAT[name], struct.pack('<Q', original), protect=True)
    state_file(pid).unlink()


class Mouse:
    def __init__(self, pid):
        self.pid, self.hwnd = pid, window_of(pid)
        self.state = on(pid, self.hwnd)
        self.h = process(pid)
        self.last = None

    def at(self, fx, fy):
        r = wintypes.RECT()
        u.GetClientRect(self.hwnd, ctypes.byref(r))
        return int(fx * r.right), int(fy * r.bottom)

    def put(self, x, y, flags=0, message=WM_MOUSEMOVE):
        p = wintypes.POINT(x, y)
        u.ClientToScreen(self.hwnd, ctypes.byref(p))
        write(self.h, self.state['point'], struct.pack('<ii', p.x, p.y))
        u.PostMessageW(self.hwnd, message, flags, (y << 16) | (x & 0xffff))

    def move(self, fx, fy, flags=0, steps=8):
        x, y = self.at(fx, fy)
        x0, y0 = self.last or (x - 60, y - 40)
        for i in range(1, steps + 1):
            self.put(x0 + (x - x0) * i // steps, y0 + (y - y0) * i // steps, flags)
            time.sleep(0.03)
        self.last = (x, y)
        return x, y

    def press(self, fx, fy, down, up, flag, hover=0.5, hold=0.10):
        x, y = self.move(fx, fy)
        time.sleep(hover)
        self.put(x, y, flag, down)
        time.sleep(hold)
        self.put(x, y, 0, up)
        time.sleep(0.05)
        self.put(x, y)

    def drag(self, fx, fy, fx2, fy2, right=False):
        down, up, flag = (WM_RDOWN, WM_RUP, 2) if right else (WM_LDOWN, WM_LUP, 1)
        x, y = self.move(fx, fy)
        time.sleep(0.4)
        self.put(x, y, flag, down)
        time.sleep(0.15)
        x, y = self.move(fx2, fy2, flag, steps=20)
        time.sleep(0.2)
        self.put(x, y, 0, up)
        self.put(x, y)

    def key(self, vk):
        """A key X4 takes from messages while its DirectInput keyboard is in use (0xeac00): Enter, Esc, Tab, the arrows."""
        scan = u.MapVirtualKeyW(vk, 0)
        u.PostMessageW(self.hwnd, WM_KEYDOWN, vk, 1 | (scan << 16))
        time.sleep(0.1)
        u.PostMessageW(self.hwnd, WM_KEYUP, vk, 1 | (scan << 16) | (3 << 30))


MICE = {}


def mouse(pid):
    if pid not in MICE:
        MICE[pid] = Mouse(pid)
    return MICE[pid]


def act(pid, action):
    """One input from a tuple, as ui_clicks.act: ('click', fx, fy[, 'left'|'right'|'double'|'hover']), ('drag', fx, fy, fx2, fy2[, 'right']),
    ('key', vk), ('move', fx, fy)."""
    m = mouse(pid)
    name, rest = action[0], action[1:]
    if name == 'click':
        mode = rest[2] if len(rest) > 2 else 'left'
        if mode == 'hover':
            m.move(rest[0], rest[1])
        elif mode == 'right':
            m.press(rest[0], rest[1], WM_RDOWN, WM_RUP, 2)
        else:
            m.press(rest[0], rest[1], WM_LDOWN, WM_LUP, 1, hold=0.06 if mode == 'double' else 0.10)
            if mode == 'double':
                x, y = m.at(rest[0], rest[1])
                m.put(x, y, 1, WM_LDBL)
                time.sleep(0.06)
                m.put(x, y, 0, WM_LUP)
    elif name == 'drag':
        m.drag(rest[0], rest[1], rest[2], rest[3], right=len(rest) > 4 and rest[4] == 'right')
    elif name == 'key':
        m.key(rest[0])
    elif name == 'move':
        m.move(rest[0], rest[1])
    else:
        raise ValueError('ui_window_input: no %s by window messages' % name)
    return '%s %s window=%x' % (name, ' '.join(str(x) for x in rest), m.hwnd)


def release(pid):
    """X4's own imports again (a client that is closed anyway needs none)."""
    MICE.pop(pid, None)
    off(pid)


def main(argv):
    pid, what, a = int(argv[0]), argv[1], argv[2:]
    if what == 'off':
        release(pid)
        print('off')
        return 0
    if what == 'on':
        print('on window=%x' % mouse(pid).hwnd)
        return 0
    names = {'rclick': ('click', 'right'), 'dclick': ('click', 'double')}
    if what in names:
        action = (names[what][0], float(a[0]), float(a[1]), names[what][1])
    elif what == 'key':
        action = ('key', int(a[0], 0))
    else:
        action = (what,) + tuple(x if x == 'right' else float(x) for x in a)
    print(act(pid, action))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

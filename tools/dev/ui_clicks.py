"""Real mouse and keyboard input on one X4 test client's window, for tests that click through the stock menus.

    python tools/dev/ui_clicks.py PID info
    python tools/dev/ui_clicks.py PID click FX FY [left|right|double|hover]
    python tools/dev/ui_clicks.py PID drag FX1 FY1 FX2 FY2 [left|right]
    python tools/dev/ui_clicks.py PID wheel FX FY NOTCHES
    python tools/dev/ui_clicks.py PID key VK [MODIFIER_VK]

FX and FY are fractions of the window's client area, so a place read off a picture of the window (PrintWindow:
station_walk_test.window_shot) is the place clicked, whatever the window's size on the screen. The process is made
per-monitor DPI aware before it asks Windows for the window's place: without that the cursor lands beside the target on
a scaled display (17 Nov: a test window on a 125 % display, every click 40 % off).

The window is brought to the front and the real cursor is moved: this takes the mouse and the keyboard focus of the PC
for the moment of the input, as a person at that window would. With --back the window that was in front before and the
cursor's old place are restored afterwards. For scripted test sessions only.
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

if sys.platform != 'win32':          # the module is imported by tools that also run elsewhere; the calls need Windows
    user32 = kernel32 = None
else:
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    try:
        ctypes.WinDLL('shcore').SetProcessDpiAwareness(2)
    except OSError:
        user32.SetProcessDPIAware()
    EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
    user32.IsWindowVisible.argtypes = [wt.HWND]
    user32.GetClientRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
    user32.ClientToScreen.argtypes = [wt.HWND, ctypes.POINTER(wt.POINT)]
    user32.GetWindowTextLengthW.argtypes = [wt.HWND]
    user32.SetForegroundWindow.argtypes = [wt.HWND]
    user32.BringWindowToTop.argtypes = [wt.HWND]
    user32.GetForegroundWindow.restype = wt.HWND

BUTTON = {'left': (2, 4), 'right': (8, 16)}          # MOUSEEVENTF_*DOWN, *UP


def window_of(pid):
    """The largest visible titled window of that process."""
    found = []

    def each(hwnd, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd) and user32.GetWindowTextLengthW(hwnd) > 0:
            rect = wt.RECT()
            user32.GetClientRect(hwnd, ctypes.byref(rect))
            found.append((rect.right * rect.bottom, hwnd))
        return True

    user32.EnumWindows(EnumWindowsProc(each), 0)
    if not found:
        raise RuntimeError('no window of pid %d' % pid)
    return max(found)[1]


def client(hwnd):
    """The client area: its top left corner on the screen, its width and height, in real pixels."""
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    origin = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(origin))
    return origin.x, origin.y, rect.right, rect.bottom


def front(hwnd):
    if user32.GetForegroundWindow() == hwnd:
        return True
    me = kernel32.GetCurrentThreadId()
    target = user32.GetWindowThreadProcessId(hwnd, None)
    fg = user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), None)
    user32.keybd_event(0x12, 0, 0, 0)      # an Alt tap lets this process take the foreground
    user32.keybd_event(0x12, 0, 2, 0)
    user32.AttachThreadInput(me, fg, True)
    user32.AttachThreadInput(me, target, True)
    user32.BringWindowToTop(hwnd)
    ok = bool(user32.SetForegroundWindow(hwnd))
    user32.AttachThreadInput(me, target, False)
    user32.AttachThreadInput(me, fg, False)
    time.sleep(0.25)
    return ok


def at(hwnd, fx, fy):
    x, y, w, h = client(hwnd)
    return x + int(w * fx), y + int(h * fy)


def glide(x, y):
    """The cursor to (x, y), the last pixels by real relative moves: X4 takes its hover from those."""
    user32.SetCursorPos(x - 4, y - 4)
    time.sleep(0.06)
    for _ in range(4):
        user32.mouse_event(1, 1, 1, 0, 0)
        time.sleep(0.03)
    user32.SetCursorPos(x, y)
    time.sleep(0.05)


def press(button, hold=0.12):
    down, up = BUTTON[button]
    user32.mouse_event(down, 0, 0, 0, 0)
    time.sleep(hold)
    user32.mouse_event(up, 0, 0, 0, 0)


def click(pid, fx, fy, mode='left'):
    hwnd = window_of(pid)
    ok = front(hwnd)
    x, y = at(hwnd, fx, fy)
    glide(x, y)
    time.sleep(0.6)          # X4 marks the element under the cursor a frame or two later; a press before that is lost
    if mode == 'double':
        press('left')
        time.sleep(0.09)
        press('left')
    elif mode in BUTTON:
        press(mode)
    time.sleep(0.3)
    return 'click %s front=%s at=%d,%d' % (mode, ok, x, y)


def drag(pid, fx1, fy1, fx2, fy2, button='left'):
    hwnd = window_of(pid)
    ok = front(hwnd)
    (x1, y1), (x2, y2) = at(hwnd, fx1, fy1), at(hwnd, fx2, fy2)
    down, up = BUTTON[button]
    glide(x1, y1)
    time.sleep(0.2)
    user32.mouse_event(down, 0, 0, 0, 0)
    time.sleep(0.15)
    for i in range(1, 21):
        user32.SetCursorPos(x1 + (x2 - x1) * i // 20, y1 + (y2 - y1) * i // 20)
        user32.mouse_event(1, 1 if i % 2 else -1, 0, 0, 0)
        time.sleep(0.03)
    time.sleep(0.15)
    user32.mouse_event(up, 0, 0, 0, 0)
    time.sleep(0.3)
    return 'drag front=%s %d,%d -> %d,%d' % (ok, x1, y1, x2, y2)


def wheel(pid, fx, fy, notches):
    hwnd = window_of(pid)
    ok = front(hwnd)
    x, y = at(hwnd, fx, fy)
    glide(x, y)
    for _ in range(abs(notches)):
        user32.mouse_event(0x800, 0, 0, 120 if notches > 0 else (-120) & 0xffffffff, 0)
        time.sleep(0.08)
    time.sleep(0.3)
    return 'wheel front=%s at=%d,%d' % (ok, x, y)


def key(pid, vk, modifier=0):
    hwnd = window_of(pid)
    ok = front(hwnd)
    if modifier:
        user32.keybd_event(modifier, user32.MapVirtualKeyW(modifier, 0), 0, 0)
        time.sleep(0.06)
    user32.keybd_event(vk, user32.MapVirtualKeyW(vk, 0), 0, 0)
    time.sleep(0.09)
    user32.keybd_event(vk, user32.MapVirtualKeyW(vk, 0), 2, 0)
    if modifier:
        time.sleep(0.06)
        user32.keybd_event(modifier, user32.MapVirtualKeyW(modifier, 0), 2, 0)
    time.sleep(0.3)
    return 'key %#x front=%s' % (vk, ok)


def act(pid, action):
    """One input from a tuple: ('click', fx, fy[, mode]), ('drag', ...), ('wheel', fx, fy, n), ('key', vk[, modifier])."""
    name, rest = action[0], action[1:]
    return {'click': click, 'drag': drag, 'wheel': wheel, 'key': key}[name](pid, *rest)


def main(argv):
    back = '--back' in argv
    argv = [x for x in argv if x != '--back']
    pid, action, rest = int(argv[0]), argv[1], argv[2:]
    hwnd = window_of(pid)
    if action == 'info':
        print('hwnd=%x client=%s foreground=%s' % (hwnd, client(hwnd), user32.GetForegroundWindow() == hwnd))
        return 0
    before = user32.GetForegroundWindow()
    old = wt.POINT()
    user32.GetCursorPos(ctypes.byref(old))
    values = [x if x in ('left', 'right', 'double', 'hover') else (int(x, 0) if action == 'key' or (action == 'wheel' and i == 2) else float(x))
              for i, x in enumerate(rest)]
    print(act(pid, (action,) + tuple(values)))
    if back:
        user32.SetCursorPos(old.x, old.y)
        if before and before != hwnd:
            front(before)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

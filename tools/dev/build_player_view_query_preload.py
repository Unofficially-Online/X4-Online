"""Build the default-off Linux player view query diagnostic preload.

Example: python tools/dev/build_player_view_query_preload.py --zig /path/to/zig
The diagnostic is inert unless X4MP_VIEW_QUERY_OBSERVER=1. This builds only;
it never launches X4 or modifies Steam configuration.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/native/engine/linux/player_view_query_preload.c'
ESCAPE_GUARD = ROOT / 'src/native/engine/linux/player_ui_escape_guard.cpp'
VIEW_BRIDGE = ROOT / 'src/native/engine/linux/player_stock_view_fov_bridge.S'
CURSOR_BRIDGE = ROOT / 'src/native/engine/linux/player_cursor_bridge.S'
OUTPUT = ROOT / '.local/player-view-query/libx4mp_player_view_query.so'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', type=Path)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--cxx', default='c++')
    args = parser.parse_args()
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    cc = [str(args.zig), 'cc', '-target', 'x86_64-linux-gnu.2.17'] if args.zig else [args.cc]
    cxx = [str(args.zig), 'c++', '-target', 'x86_64-linux-gnu.2.17'] if args.zig else [args.cxx]
    flags = ['-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', '-fvisibility=hidden']
    # Resolve C++ ABI symbols from the game process; do not introduce a second runtime.
    with tempfile.TemporaryDirectory(prefix='x4-view-query-build-') as directory:
        preload_object = str(Path(directory) / 'preload.o')
        guard_object = str(Path(directory) / 'escape_guard.o')
        view_object = str(Path(directory) / 'stock_view_fov.o')
        cursor_object = str(Path(directory) / 'cursor_bridge.o')
        subprocess.run([*cc, '-std=c11', *flags, '-c', str(SOURCE), '-o', preload_object], check=True)
        subprocess.run([*cxx, '-std=c++17', *flags, '-c', str(ESCAPE_GUARD), '-o', guard_object], check=True)
        subprocess.run([*cc, *flags, '-c', str(VIEW_BRIDGE), '-o', view_object], check=True)
        subprocess.run([*cc, *flags, '-c', str(CURSOR_BRIDGE), '-o', cursor_object], check=True)
        subprocess.run([*cc, '-shared', preload_object, guard_object, view_object, cursor_object,
                        str(ROOT / 'src/native/engine/linux/player_stock_view_component_bridge.S'),
                        str(ROOT / 'src/native/engine/linux/player_stock_view_ship_bridge.S'),
                        '-o', str(OUTPUT), '-ldl'], check=True)
    print(OUTPUT)


if __name__ == '__main__':
    main()

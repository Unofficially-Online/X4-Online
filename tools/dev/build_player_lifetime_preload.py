"""Build the default-off Linux player lifetime diagnostic preload.

Example: python tools/dev/build_player_lifetime_preload.py --zig /path/to/zig
The diagnostic is inert unless X4MP_PLAYER_LIFETIME_PRELOAD=1. This builds only;
it never launches X4 or modifies Steam configuration.
"""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/native/engine/linux/player_lifetime_preload.c'
OUTPUT = ROOT / '.local/player-lifetime/libx4mp_player_lifetime.so'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', type=Path)
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    compiler = [str(args.zig), 'cc', '-target', 'x86_64-linux-gnu.2.17'] if args.zig else [args.cc]
    subprocess.run([*compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    '-fvisibility=hidden', str(SOURCE), '-o', str(OUTPUT)], check=True)
    print(OUTPUT)


if __name__ == '__main__':
    main()

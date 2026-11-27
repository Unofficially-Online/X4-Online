"""How far apart the two windows of a take draw the same simulation step (for cut.py's "lag").

  python tools/dev/showcase/align.py TAKE.mkv AT_SECONDS [--seconds 2] [--width 1920] [--max 10]

Both clients simulate the same step, but each draws it in a frame of its own, a frame or two apart, and so the
recorder (60 pictures a second of both windows at once) holds the same step in slightly different pictures. Prints
the mean difference of the two windows' pictures (grey, 480 px wide) for player 2's picture taken `lag` frames later;
the smallest is the lag at which both show the same step. Only meaningful while both cameras stand at the same place."""
import argparse
import subprocess
import sys

import numpy


def frames(take, at, seconds, x0, width, height):
    w, h = 480, 270
    raw = subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-ss', str(at), '-t', str(seconds), '-i', take,
                          '-vf', f'crop={width}:{height}:{x0}:0,scale={w}:{h},format=gray', '-f', 'rawvideo', '-'],
                         capture_output=True).stdout
    return numpy.frombuffer(raw, dtype=numpy.uint8).reshape(-1, h, w).astype(numpy.int16)


def lags(take, at, seconds=2.0, width=1920, height=1080, most=10):
    a, b = frames(take, at, seconds, 0, width, height), frames(take, at, seconds, width, width, height)
    n = min(len(a), len(b))
    out = {}
    for lag in range(-most, most + 1):
        i0, i1 = max(0, -lag), min(n, n - lag)
        if i1 - i0 < 10:
            continue
        out[lag] = float(numpy.abs(a[i0:i1] - b[i0 + lag:i1 + lag]).mean())
    return out


def main(argv):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('take')
    p.add_argument('at', type=float)
    p.add_argument('--seconds', type=float, default=2.0)
    p.add_argument('--width', type=int, default=1920)
    p.add_argument('--max', type=int, default=10)
    args = p.parse_args(argv)
    found = lags(args.take, args.at, args.seconds, args.width, most=args.max)
    best = min(found, key=found.get)
    print(' '.join(f'{lag}:{value:.2f}' for lag, value in sorted(found.items())))
    print(f'best lag {best} ({found[best]:.2f})')


if __name__ == '__main__':
    main(sys.argv[1:])

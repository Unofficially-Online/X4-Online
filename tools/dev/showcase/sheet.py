"""A timeline sheet of a take, to pick shots from: one small frame every STEP seconds with its time.

  python tools/dev/showcase/sheet.py TAKE.mkv OUT.png [--from S] [--step S] [--count N] [--cols N] [--width PX] [--crop x:y:w:h]"""
import argparse
import subprocess
import sys


def main(argv):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('take')
    p.add_argument('out')
    p.add_argument('--from', dest='start', type=float, default=0)
    p.add_argument('--step', type=float, default=10)
    p.add_argument('--count', type=int, default=14)
    p.add_argument('--cols', type=int, default=2)
    p.add_argument('--width', type=int, default=960)
    p.add_argument('--crop', default='')
    args = p.parse_args(argv)
    rows = -(-args.count // args.cols)
    chain = []
    if args.crop:
        chain.append('crop=' + ':'.join(args.crop.split(':')[2:] + args.crop.split(':')[:2]))
    chain += [f'fps=1/{args.step}:start_time=0:round=down', f'scale={args.width}:-2',
              "drawtext=fontfile='C\\:/Windows/Fonts/consola.ttf':text='%{eif\\:" + f'{args.start}+n*{args.step}' +
              "\\:d}s':x=6:y=6:fontsize=22:fontcolor=yellow:box=1:boxcolor=black@0.7",
              f'tile={args.cols}x{rows}']
    done = subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-ss', str(args.start), '-t',
                           str(args.step * args.count + 1), '-i', args.take, '-vf', ','.join(chain), '-frames:v', '1',
                           '-update', '1', args.out], capture_output=True, text=True)
    if done.returncode:
        raise SystemExit(done.stderr[-2000:])


if __name__ == '__main__':
    main(sys.argv[1:])

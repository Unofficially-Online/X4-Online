"""Cuts the showcase video from the takes (ffmpeg only).

  python tools/dev/showcase/cut.py EDIT.json OUT.mp4 [--only N,N] [--sheet OUT.png] [--list OUT.txt]

"cards": {"name.png": {"type": "title"|"end"} or {"labels": [[x, y, text, "left"|"right"|"center"]], "caption": ...,
"sub": ..., "divider": [x]}} are drawn into BASE/cards first (cards.py) and used as "image" or "overlay": "cards/name.png".

A take is one file with both players' windows side by side (take.py: client 1 left, client 2 right, each W x H,
60 fps). EDIT.json: {"size": [1920, 1080], "takes": {"name": "file.mkv"}, "shots": [...]}, each shot one of:

  {"kind": "card", "image": "title.png", "seconds": 2.5}
  {"kind": "full", "take": T, "at": seconds, "seconds": S, "side": 1|2}          one player's window
  {"kind": "split", "take": T, "at": ..., "seconds": ..., "x": [x1, x2]}        both windows, a 960 px column of each
  {"kind": "duo", ..., "w": 1200, "x": [x1, x2]}   both windows, a wider column of each, dark bands above and below
  {"kind": "fit", ...}                                                         both windows whole, side by side
  {"kind": "quad", ...}       player 1 | player 2 over both overlaid (50%) | their difference (grey = equal)
  {"kind": "blend", ...}      both windows overlaid, 50% each, full screen
  {"kind": "wipe", ...}       full screen: player 1 left of a line, player 2 right of it, the line sweeping
  {"kind": "pair", "take": T, "at": [a1, a2], ...}   two moments of ONE window side by side (the page of the host, of the joiner)

Common keys: "overlay": a transparent PNG over the shot (cards.py), "fade": [in, out] seconds from/to black, "lag":
frames by which player 2's picture is taken later (the two windows draw the same step a frame or two apart),
"crop": [x, y, w, h] of a window before anything else (a punch-in), "note": the shot list's line. A shot of a take
with tick marks ("ticks": {"name": "file.ticks.txt"}) may give "tick" (the relay's tick, or two for a pair) in place
of "at", and "speed": "game": the PC ran the session below real time while it recorded (several X4 at once), and the
shot is played at the game's own speed, 30 ticks a second; a number is a plain playback speed. Every shot is rendered to its own file and the files are joined without re-encoding twice (one final encode)."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

FPS = 60
sys.path.insert(0, str(Path(__file__).resolve().parent))


def run(cmd):
    done = subprocess.run(cmd, capture_output=True, text=True)
    if done.returncode:
        raise SystemExit('ffmpeg failed:\n' + ' '.join(str(c) for c in cmd) + '\n' + done.stderr[-3000:])


def window(label, side, width, height, shot, out):
    """Filter text: one player's window out of the take's picture, with the shot's crop and lag."""
    crop = shot.get('crop')
    x0 = (side - 1) * width
    text = f'[{label}]crop={width}:{height}:{x0}:0'
    if crop:
        text += f',crop={crop[2]}:{crop[3]}:{crop[0]}:{crop[1]},scale={width}:{height}:flags=lanczos'
    return text + f'[{out}]'


def shot_filter(shot, width, height, has_overlay):
    kind, W, H = shot['kind'], 1920, 1080
    parts = []
    if kind == 'card':
        parts.append(f'[0:v]scale={W}:{H},format=yuv420p[v0]')
    elif kind == 'full':
        parts.append(window('0:v', shot.get('side', 1), width, height, shot, 'a'))
        parts.append(f'[a]scale={W}:{H}:flags=lanczos[v0]')
    elif kind == 'pair':
        parts.append(window('0:v', shot.get('side', 1), width, height, shot, 'a'))
        parts.append(window('1:v', shot.get('side', 1), width, height, shot, 'b'))
        xs = shot.get('x', [0, 0])
        zoom = shot.get('zoom', 1.0)
        cw, ch = int(960 / zoom) // 2 * 2, int(H / zoom) // 2 * 2
        ys = shot.get('y', [0, 0])
        parts.append(f'[a]crop={cw}:{ch}:{xs[0]}:{ys[0]},scale=960:{H}:flags=lanczos[l];'
                     f'[b]crop={cw}:{ch}:{xs[1]}:{ys[1]},scale=960:{H}:flags=lanczos[r];[l][r]hstack[v0]')
    else:
        parts.append('[0:v]split[s1][s2]')
        parts.append(window('s1', 1, width, height, shot, 'a'))
        parts.append(window('s2', 2, width, height, shot, 'b0'))
        lag = int(shot.get('lag', 0))
        # player 2's picture `lag` frames later (positive) or player 1's (negative): the same step on both
        if lag > 0:
            parts.append(f'[b0]trim=start_frame={lag},setpts=PTS-STARTPTS[b]')
        elif lag < 0:
            parts.append(f'[a]trim=start_frame={-lag},setpts=PTS-STARTPTS[a2];[b0]null[b]')
        else:
            parts.append('[b0]null[b]')
        a = 'a2' if lag < 0 else 'a'
        if kind == 'split':
            xs = shot.get('x', [(width - 960) // 2] * 2)
            parts.append(f'[{a}]scale={W}:{H}:flags=lanczos,crop=960:{H}:{xs[0]}:0[l];'
                         f'[b]scale={W}:{H}:flags=lanczos,crop=960:{H}:{xs[1]}:0[r];[l][r]hstack[v0]')
        elif kind == 'duo':
            # a wider column of each window (w of its width, default 1200 of 1920) between two dark bands for the labels
            w = int(shot.get('w', 1200)) * width // 1920 // 2 * 2
            xs = shot.get('x', [(width - w) // 2] * 2)
            h = int(960 * height / w) // 2 * 2
            parts.append(f'[{a}]crop={w}:{height}:{xs[0]}:0,scale=960:{h}:flags=lanczos[l];'
                         f'[b]crop={w}:{height}:{xs[1]}:0,scale=960:{h}:flags=lanczos[r];'
                         f'[l][r]hstack,pad={W}:{H}:0:{(H - h) // 2}:color=0x04060a[v0]')
        elif kind == 'fit':
            parts.append(f'[{a}]scale=960:540:flags=lanczos[l];[b]scale=960:540:flags=lanczos[r];'
                         f'[l][r]hstack,pad={W}:{H}:0:270:color=0x04060a[v0]')
        elif kind == 'blend':
            parts.append(f'[{a}][b]blend=all_mode=average,scale={W}:{H}:flags=lanczos[v0]')
        elif kind == 'wipe':
            # the line swings between 22% and 78% of the width: left of it player 1, right of it player 2
            period = shot.get('period', 3.0)
            edge = f'W*(0.5+0.28*sin(2*PI*T/{period}))'
            parts.append(f'[{a}]format=gbrp[ga];[b]format=gbrp[gb];'
                         f"[ga][gb]blend=all_expr='if(lt(X,{edge}),A,if(lt(X,{edge}+3),255,B))',"
                         f'scale={W}:{H}:flags=lanczos[v0]')
        elif kind == 'quad':
            gain = shot.get('gain', 3)
            parts.append(f'[{a}]split=3[a1][a2][a3];[b]split=3[b1][b2][b3];'
                         '[a1]scale=960:540:flags=lanczos[p1];[b1]scale=960:540:flags=lanczos[p2];'
                         '[a2][b2]blend=all_mode=average,scale=960:540:flags=lanczos[p3];'
                         f"[a3]format=gbrp[d1];[b3]format=gbrp[d2];[d1][d2]blend=all_expr='clip(128+{gain}*(A-B),0,255)',"
                         'hue=s=0,scale=960:540:flags=lanczos,format=yuv420p[p4];'
                         '[p1][p2]hstack[top];[p3][p4]hstack[bottom];[top][bottom]vstack[v0]')
        else:
            raise SystemExit(f'unknown shot kind {kind!r}')
    last = 'v0'
    speed = shot.get('speed', 1.0)
    if speed != 1.0 and kind != 'card':
        parts.append(f'[v0]setpts=PTS/{speed:.5f}[vs]')
        last = 'vs'
    if has_overlay:
        index = 2 if kind == 'pair' else 1
        parts.append(f'[{last}]fps={FPS}[vf];[vf][{index}:v]overlay=0:0:format=auto[v1]')
        last = 'v1'
    seconds = shot['seconds']
    tail = f'[{last}]fps={FPS},format=yuv420p'
    fade = shot.get('fade') or [0, 0]
    if fade[0]:
        tail += f',fade=t=in:st=0:d={fade[0]}'
    if fade[1]:
        tail += f',fade=t=out:st={seconds - fade[1]:.3f}:d={fade[1]}'
    parts.append(tail + '[v]')
    return ';'.join(parts)


def tick_table(path):
    """take.py's NAME.ticks.txt: [(seconds into the take, tick)] in order."""
    rows = [line.split() for line in Path(path).read_text(encoding='utf-8').splitlines() if line.strip()]
    return [(float(a), int(b)) for a, b in rows]


def tick_time(table, tick):
    """The take's time at which the relay released that tick (between two marks: in proportion)."""
    before = [(t, k) for t, k in table if k <= tick]
    after = [(t, k) for t, k in table if k >= tick]
    if not before or not after:
        raise SystemExit(f'tick {tick} is outside the take ({table[0][1]}..{table[-1][1]})')
    (t0, k0), (t1, k1) = before[-1], after[0]
    return t0 if k1 == k0 else t0 + (t1 - t0) * (tick - k0) / (k1 - k0)


def placed(shot, edit, base):
    """The shot with "tick" turned into "at", and "speed": "game" into the factor that plays 30 ticks a second."""
    if shot['kind'] == 'card' or ('tick' not in shot and shot.get('speed') != 'game'):
        return shot
    table = tick_table(base / edit['ticks'][shot['take']])
    shot = dict(shot)
    if 'tick' in shot:
        ticks = shot['tick'] if isinstance(shot['tick'], list) else [shot['tick']]
        times = [tick_time(table, k) for k in ticks]
        shot['at'] = times if isinstance(shot['tick'], list) else times[0]
    if shot.get('speed') == 'game':
        at = shot['at'][0] if isinstance(shot['at'], list) else shot['at']
        first = min(table, key=lambda row: abs(row[0] - at))[1]
        shot['speed'] = max(1.0, (tick_time(table, first + round(shot['seconds'] * 30)) - at) / shot['seconds'])
    return shot


def render(shot, index, edit, base, work):
    out = work / f'shot{index:02d}.mp4'
    width, height = edit['size']
    shot = placed(shot, edit, base)
    seconds, speed = shot['seconds'], shot.get('speed', 1.0)
    inputs = []
    if shot['kind'] == 'card':
        inputs += ['-loop', '1', '-framerate', str(FPS), '-t', str(seconds), '-i', str(base / shot['image'])]
    else:
        take = str(base / edit['takes'][shot['take']])
        starts = shot['at'] if isinstance(shot['at'], list) else [shot['at']]
        for start in starts:
            inputs += ['-ss', f'{start:.3f}', '-t', f'{seconds * speed + 1:.3f}', '-i', take]
    if shot.get('lag') == 'auto':
        # the lag at which the two windows' pictures differ least over the shot (align.py)
        import align
        found = align.lags(take, starts[0], min(4.0, seconds * speed), width, height)
        shot = dict(shot, lag=min(found, key=found.get))
        print(f'    shot {index}: lag {shot["lag"]} (difference {found[shot["lag"]]:.2f}, at lag 0 {found[0]:.2f})', flush=True)
    overlay = shot.get('overlay')
    if overlay:
        inputs += ['-loop', '1', '-framerate', str(FPS), '-t', str(seconds + 1), '-i', str(base / overlay)]
    graph = shot_filter(shot, width, height, bool(overlay))
    run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', *inputs, '-filter_complex', graph, '-map', '[v]',
         '-t', f'{seconds:.3f}', '-r', str(FPS), '-c:v', 'libx264', '-preset', 'medium', '-crf', '10', '-pix_fmt', 'yuv420p',
         '-an', str(out)])
    return out


def main(argv):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('edit')
    p.add_argument('out')
    p.add_argument('--only', default='', help='render only these shots (1-based), for a look at them')
    p.add_argument('--sheet', help='a 3x3 contact sheet of the cut')
    p.add_argument('--list', help='the shot list as text')
    args = p.parse_args(argv)
    edit_path = Path(args.edit).resolve()
    edit = json.loads(edit_path.read_text(encoding='utf-8'))
    global FPS
    FPS = int(edit.get('fps', FPS))       # "fps": 30: a steady 30 where the takes hold fewer than 60 pictures a second
    base = Path(edit.get('base', edit_path.parent))
    if not base.is_absolute():
        base = (edit_path.parent / base).resolve()
    out = Path(args.out).resolve()
    work = out.parent / (out.stem + '-shots')
    work.mkdir(parents=True, exist_ok=True)
    if edit.get('cards'):
        # the edit's own title, end card and caption overlays (cards.py), drawn into BASE/cards
        import cards
        (base / 'cards').mkdir(parents=True, exist_ok=True)
        for name, spec in edit['cards'].items():
            target = base / 'cards' / name
            if spec.get('type') == 'title':
                cards.title(target)
            elif spec.get('type') == 'end':
                cards.end(target)
            else:
                cards.overlay(target, labels=[tuple(row) for row in spec.get('labels', [])], caption=spec.get('caption'),
                              sub=spec.get('sub'), divider=spec.get('divider', []))
    only = {int(n) for n in args.only.split(',') if n}
    files, lines, clock = [], [], 0.0
    for index, shot in enumerate(edit['shots'], 1):
        lines.append('%2d  %5.2f-%5.2f s  %-5s  %s' % (index, clock, clock + shot['seconds'], shot['kind'], shot.get('note', '')))
        clock += shot['seconds']
        if only and index not in only:
            continue
        files.append(render(shot, index, edit, base, work))
        done = placed(shot, edit, base)
        if done is not shot:
            at = done['at'] if isinstance(done['at'], list) else [done['at']]
            lines[-1] += '  [take %s at %s s, speed %.2f]' % (done['take'], '/'.join('%.1f' % a for a in at), done.get('speed', 1.0))
        print(lines[-1], flush=True)
    listing = work / 'concat.txt'
    listing.write_text(''.join(f"file '{f.as_posix()}'\n" for f in files), encoding='utf-8')
    run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', str(listing),
         # the takes and cards are BT.601 (swscale's RGB default): the film is BT.709 and says so
         '-vf', 'scale=in_color_matrix=bt601:out_color_matrix=bt709,format=yuv420p',
         '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709', '-color_range', 'tv',
         '-c:v', 'libx264', '-preset', 'slow', '-crf', '17', '-profile:v', 'high', '-pix_fmt', 'yuv420p', '-r', str(FPS),
         '-movflags', '+faststart', '-an', str(out)])
    print(f'{out}: {clock:.2f} s planned')
    if args.list:
        Path(args.list).write_text('\n'.join(lines) + f'\ntotal {clock:.2f} s\n', encoding='utf-8')
    if args.sheet:
        step = clock / 9
        select = '+'.join(f'eq(n,{int((i + 0.5) * step * FPS)})' for i in range(9))
        run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', str(out), '-vf',
             f"select='{select}',scale=640:360:flags=lanczos,tile=3x3:padding=6:margin=6:color=0x04060a",
             '-frames:v', '1', '-update', '1', args.sheet])


if __name__ == '__main__':
    main(sys.argv[1:])

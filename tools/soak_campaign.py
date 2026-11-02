"""Soak several session seeds one after another, unattended, and triage every divergence.

  python tools/soak_campaign.py --seeds 1111 2222 3333 [--ticks 216002] [--name NAME]

Per seed: capture a starting snapshot with that X4 session seed if .local/snapshots/seedS.xml.gz does not
exist yet (x4ls snapshot), build the soak overlay (tools/build_soak_overlay.py: src/lockstep/profiles/soak.lua,
play mode, shared camera, script pilots), run a two-client observe session, and for a diverged run
x4ls triage. One line per step goes to .local/campaigns/NAME.jsonl, so an interrupted campaign shows how
far it got; seeds already recorded there as finished are skipped when the same NAME runs again.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
PY = sys.executable
GATE = ROOT / '.local/native-lockstep/build/x4lan_gate_64.dll'
SOAK_ARGS = ['--play-mode', '--drop', 'native_gate_camera_anchor', '--set', 'native_gate_event_rng_play_rows=true',
             '--sandbox-arg=--device1=none', '--sandbox-arg=--device2=none']


def x4ls(*args):
    return subprocess.run([PY, str(ROOT / 'tools/x4ls.py'), *map(str, args)], cwd=ROOT, text=True, capture_output=True)


def record(log, **entry):
    entry['time'] = time.strftime('%Y-%m-%d %H:%M:%S')
    with log.open('a') as f:
        f.write(json.dumps(entry) + '\n')
    print(json.dumps(entry), flush=True)


def finished(log):
    if not log.is_file():
        return set()
    return {e['seed'] for e in map(json.loads, log.read_text().splitlines()) if e.get('step') == 'done'}


def latest_run():
    runs = [p for p in (ROOT / '.local/runs').iterdir() if p.is_dir() and p.name[:1].isdigit()]
    return max(runs, key=lambda p: p.name)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--seeds', type=int, nargs='+', required=True)
    p.add_argument('--ticks', type=int, default=216002, help='relay ticks per soak (216002: 2 hours at 30 Hz)')
    p.add_argument('--name', default=time.strftime('campaign-%Y%m%d-%H%M'))
    p.add_argument('--gate-dll', type=Path, default=GATE)
    args = p.parse_args(argv)
    log = ROOT / '.local/campaigns' / f'{args.name}.jsonl'
    log.parent.mkdir(parents=True, exist_ok=True)
    done = finished(log)
    for seed in args.seeds:
        if seed in done:
            continue
        snapshot = ROOT / '.local/snapshots' / f'seed{seed}.xml.gz'
        if not snapshot.exists():
            r = x4ls('snapshot', '--out', snapshot, '--seed', seed, '--steam-fix')
            record(log, seed=seed, step='snapshot', ok=snapshot.exists(), tail=(r.stdout + r.stderr).strip().splitlines()[-1:])
            if not snapshot.exists():
                continue
        overlay = ROOT / '.local/overlays' / f'{args.name}-seed{seed}'
        if not overlay.exists():
            r = subprocess.run([PY, str(ROOT / 'tools/build_soak_overlay.py'), '--snapshot', str(snapshot), '--gate-dll',
                                str(args.gate_dll), '--out', str(overlay), *SOAK_ARGS, '--window', str(args.ticks - 2)],
                               cwd=ROOT, text=True, capture_output=True)
            record(log, seed=seed, step='overlay', ok=r.returncode == 0, tail=(r.stdout + r.stderr).strip().splitlines()[-1:])
            if r.returncode:
                continue
        timeout = int(args.ticks / 30 / 0.7) + 900
        r = x4ls('session', '--clients', 2, '--overlay', overlay, '--snapshot', snapshot, '--ticks', args.ticks,
                 '--observe', '--lookahead', 1, '--timeout', timeout, '--fast', '--steam-fix')
        run = latest_run()
        summary = json.loads((run / 'summary.json').read_text()) if (run / 'summary.json').is_file() else {}
        record(log, seed=seed, step='session', run=run.name, outcome=summary.get('outcome'),
               first_mismatch=summary.get('first_digest_mismatch'), line=summary.get('line'), seconds=summary.get('seconds'))
        if summary.get('first_digest_mismatch'):
            t = x4ls('triage', run, '--steam-fix')
            record(log, seed=seed, step='triage', report=str(run.parent / f'{run.name}-triage.txt'), ok=t.returncode == 0)
        record(log, seed=seed, step='done')


if __name__ == '__main__':
    main()

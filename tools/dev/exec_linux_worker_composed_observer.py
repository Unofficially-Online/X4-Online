"""Exec one already staged X4 case with the separate, pinned wait observer.

The caller provides a freshly staged case configuration. This adapter is a
separate diagnostic, and its receipts do not prove native frame ownership.
"""
import hashlib
import json
import os
from pathlib import Path
import sys
import time

EXPECTED_IMAGE = '113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def observation_window(settings, exec_monotonic_ns):
    """Require an explicit bounded delay and window, measured from exec."""
    delay = settings.get('observe_delay_seconds')
    duration = settings.get('observe_duration_seconds')
    if (type(delay) is not int or not 0 <= delay <= 1800 or
            type(duration) is not int or not 1 <= duration <= 480 or
            type(exec_monotonic_ns) is not int or exec_monotonic_ns <= 0):
        raise ValueError('explicit observation delay 0..1800 and duration 1..480 seconds required')
    first = exec_monotonic_ns + delay * 1_000_000_000
    return first, first + duration * 1_000_000_000

def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: exec_linux_worker_composed_observer.py CASE_CONFIG')
    config_path = Path(sys.argv[1]).resolve(strict=True)
    settings = json.loads(config_path.read_text())
    case = config_path.parent
    if case.name not in ('case1', 'case2'):
        raise ValueError('expected an isolated case directory')
    program = settings['program']
    executable = Path(program[0])
    if executable != case / 'game/X4' or sha(executable) != EXPECTED_IMAGE:
        raise ValueError('case executable does not match the pinned X4')
    if '-noworkshopsync' not in program or '-personalfolderid' not in program:
        raise ValueError('isolated game arguments required')
    bundle = Path(settings['observer_bundle']).resolve(strict=True)
    manifest = json.loads((bundle / 'manifest.json').read_text())
    if manifest['version'] != 2 or manifest['pinned_x4_sha256'] != EXPECTED_IMAGE:
        raise ValueError('unexpected wait observer manifest')
    gate = case / 'extensions/x4_lan_lab/native/x4lan_gate.so'
    if gate.is_symlink() or sha(gate) != manifest['gate_sha256']:
        raise ValueError('case gate differs from pinned composed gate')
    library = bundle / 'worker_wait_observer.so'
    if library.is_symlink() or sha(library) != manifest['library_sha256']:
        raise ValueError('changed wait observer library')
    frozen_executor = bundle / 'source-receipts/tools/dev/exec_linux_worker_composed_observer.py'
    if Path(__file__).resolve() != frozen_executor.resolve():
        raise ValueError('must run the frozen wait observer executor')
    from build_linux_worker_composed_observer import SOURCES
    if set(manifest['sources']) != set(SOURCES):
        raise ValueError('wait observer source closure differs')
    for name in SOURCES:
        source = bundle / 'source-receipts' / name
        if source.is_symlink() or sha(source) != manifest['sources'][name]:
            raise ValueError('changed wait observer source: ' + name)
    pid = os.getpid()
    start = Path('/proc/self/stat').read_text().rsplit(')', 1)[1].split()[19]
    exec_time = time.monotonic_ns()
    begin, end = observation_window(settings, exec_time)
    environment = dict(os.environ)
    environment.update(LD_PRELOAD=str(library) + ' ' + settings.get('original_preloads', ''),
                       X4_DEV_WORKER_WAIT_OBSERVE='1', X4_PRIVATE_WORKER_PID=str(pid),
                       X4_PRIVATE_WORKER_START=start, X4_PRIVATE_WORKER_BEGIN_NS=str(begin),
                       X4_PRIVATE_WORKER_END_NS=str(end))
    receipt = {'kind':'bounded worker semaphore wait observation; not frame ownership proof',
               'pid':pid, 'start':start, 'exec_monotonic_ns':exec_time, 'begin_ns':begin, 'end_ns':end,
               'delay_seconds':settings['observe_delay_seconds'],
               'duration_seconds':settings['observe_duration_seconds'],
               'config_sha256':sha(config_path), 'manifest_sha256':sha(bundle / 'manifest.json'),
               'library_sha256':manifest['library_sha256']}
    with (case / 'private-worker-wait-exec-receipt.json').open('x') as output:
        json.dump(receipt, output, indent=2)
    os.execve(str(executable), program, environment)

if __name__ == '__main__':
    main()

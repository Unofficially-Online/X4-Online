"""Build an isolated, hash-receipted alternate-wait diagnostic; never launch X4."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('tools/dev/linux_worker_wait_composed_observer.c',
           'src/native/engine/lockstep_sha256.h',
           'tools/dev/build_linux_worker_composed_observer.py',
           'tools/dev/exec_linux_worker_composed_observer.py',
           'src/native/engine/linux/test_linux_worker_composed_observer.c',
           'src/native/engine/linux/gate_mass_traffic.h',
           'tests/test_linux_worker_composed_observer.py')

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def build(output, gate):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    sources = {}
    for name in SOURCES:
        source = ROOT / name
        if source.is_symlink() or not source.is_file():
            raise ValueError('missing or symlinked diagnostic source: ' + name)
        target = output / 'source-receipts' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        sources[name] = digest(target)
    cc = shutil.which('cc')
    if not cc:
        raise ValueError('C compiler is required')
    library = output / 'worker_wait_observer.so'
    frozen = output / 'source-receipts'
    subprocess.run([cc, '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-mcx16', '-I', str(frozen / 'src/native/engine'),
                    str(frozen / 'tools/dev/linux_worker_wait_composed_observer.c'), '-o', str(library), '-pthread'],
                   check=True, timeout=120)
    metadata = {'kind':'opt-in composed alternate wait, movement wait and post observer; not a session ownership proof',
                'version':2, 'sources':sources, 'gate_sha256':digest(gate), 'library_sha256':digest(library),
                'pinned_x4_sha256':'113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20'}
    (output / 'manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
    return metadata

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--gate', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.out, args.gate), indent=2))

if __name__ == '__main__':
    main()

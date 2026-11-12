"""Compile/fork the exact native trace functions; never load X4 or a profile."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--header', type=Path, default=Path(__file__).resolve().parents[2] /
                        'src/native/engine/linux/gate_player_lifecycle_discard.h')
    args = parser.parse_args()
    source = args.header.read_text()
    # Exercise production bodies verbatim without unrelated native game dependencies.
    first = source.index('static void lifecycle_discard_open_log(void)')
    last = source.index('static void lifecycle_discard_stage(', first)
    bodies = source[first:last]
    fixture = r'''
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int lifecycle_discard_log = -1;
''' + bodies + r'''
int main(int argc, char **argv) {
    lifecycle_discard_open_log();
    if (argc > 1 && !strcmp(argv[1], "badfd")) close(lifecycle_discard_log);
    lifecycle_discard_write("fixture_trace\n");
    lifecycle_discard_exit(1, "fixture_complete");
}
'''
    compiler = shutil.which('cc')
    if not compiler:
        raise SystemExit('C compiler required; no test skipped')
    with tempfile.TemporaryDirectory(prefix='x4-native-log-fixture-') as directory:
        root = Path(directory)
        cfile, binary = root / 'fixture.c', root / 'fixture'
        cfile.write_text(fixture)
        subprocess.run([compiler, '-std=c11', '-D_GNU_SOURCE', str(cfile), '-o', str(binary)],
                       check=True, capture_output=True, timeout=30)

        def run(path, expected, mode='normal'):
            env = os.environ.copy()
            env.pop('X4_DEV_PLAYER_LIFECYCLE_LOG', None)
            if path is not None:
                env['X4_DEV_PLAYER_LIFECYCLE_LOG'] = str(path)
            result = subprocess.run([str(binary), mode], env=env, capture_output=True, timeout=3)
            assert result.returncode == expected, (path, result.returncode, result.stderr)

        run(None, 224)
        run('relative-path', 224)
        run(root / 'missing-parent' / 'trace', 224)
        existing = root / 'existing'
        existing.write_bytes(b'original bytes\n')
        run(existing, 224)
        assert existing.read_bytes() == b'original bytes\n'
        linked = root / 'symlink'
        linked.symlink_to(existing)
        run(linked, 224)
        assert linked.is_symlink() and existing.read_bytes() == b'original bytes\n'
        badfd = root / 'badfd'
        run(badfd, 224, 'badfd')
        assert badfd.read_bytes() == b''
        good = root / 'fresh'
        run(good, 223)
        assert good.stat().st_mode & 0o777 == 0o600
        assert good.read_text() == ('fixture_trace\nX4LAN LIFECYCLE_DISCARD '
                                    'result=pass reason=fixture_complete exit=223 restored=0 resumed=0\n')
        run(good, 224)
        assert good.read_text().startswith('fixture_trace\n')
    print('PASS: missing/relative path, missing parent, existing file, symlink, bad FD, '
          'fresh private trace and repeat refusal; exact native functions, no X4')


if __name__ == '__main__':
    main()

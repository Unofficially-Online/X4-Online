#!/usr/bin/env python3
"""Exec an already staged private X4 with a private append-only receipt fd."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import sys

X4_SHA = '113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20'
PROVIDER = 'libx4mp_semaphore_observer.so'


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            value.update(block)
    return value.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--case', required=True, type=Path)
    parser.add_argument('--provider-sha256', required=True)
    parser.add_argument('--provider-size', required=True, type=int)
    parser.add_argument('program', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    case = args.case
    program = args.program[1:] if args.program[:1] == ['--'] else args.program
    if (not case.is_absolute() or case.is_symlink() or not case.is_dir() or case.stat().st_uid != os.getuid() or
            not re.fullmatch('[0-9a-f]{64}', args.provider_sha256) or args.provider_size <= 0 or
            not program or program[0] != str(case / 'game/X4')):
        raise SystemExit('semaphore exec: invalid case or command')
    provider = case / PROVIDER
    image = case / 'game/X4'
    if (provider.is_symlink() or not provider.is_file() or provider.stat().st_size != args.provider_size or
            digest(provider) != args.provider_sha256 or not image.is_file() or digest(image) != X4_SHA):
        raise SystemExit('semaphore exec: provider or executable pin mismatch')
    chain = os.environ.get('X4MP_SEMAPHORE_PRELOAD_CHAIN', '')
    directory = os.open(case, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        fd = os.open('semaphore-observer.log', os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_APPEND | os.O_NOFOLLOW,
                     0o600, dir_fd=directory)
    finally:
        os.close(directory)
    os.set_inheritable(fd, True)
    env = dict(os.environ)
    env.pop('X4MP_SEMAPHORE_PRELOAD_CHAIN', None)
    env.update(LD_PRELOAD=' '.join(filter(None, (str(provider), chain))), X4MP_SEMAPHORE_OBSERVER='1',
               X4MP_SEMAPHORE_OBSERVER_FD=str(fd), X4MP_SEMAPHORE_OBSERVER_SHA256=args.provider_sha256,
               X4MP_SEMAPHORE_OBSERVER_SIZE=str(args.provider_size))
    os.execve(program[0], program, env)


if __name__ == '__main__':
    main()

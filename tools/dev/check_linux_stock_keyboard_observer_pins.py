"""Verify stock keyboard observer pins in a private byte copy of the pinned ELF; never execute it."""
import argparse
import hashlib
from pathlib import Path
import struct

IMAGE_SHA256 = '113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20'
IMAGE_SIZE = 60459280
PINS = (
    ('action handler', 0x166C2F0, 1024, '6a18f87163e93138826c076d6d06b6db51f5aac46595e714a9c92459ac8bfa02'),
    ('state handler', 0x166C6F0, 2349, '369d1664b722dec42bee51f949b3c0c32c16bf1353e00412e271ffbedb6dd335'),
    ('state dispatcher', 0x1050FA0, 284, '86f89b2beec85f1893a4ad140d4d36bae995ea1fa107ef9136b5d66c50bc41d3'),
    ('action and range dispatch', 0xCEC2F0, 7470, 'c8bad5e7b80e24d1f60f5827e1857a7a9dc33aaf99e15f10710aadeeae3c2e9f'),
    ('registration', 0x1D95A30, 4874, 'f4d72eb3b84bc48a64f9bc683296683b065c70a801dd2669442f0267ebecb093'),
)


def inspect(path):
    image = path.read_bytes()  # Private bytes only; never map executable pages.
    if len(image) != IMAGE_SIZE or hashlib.sha256(image).hexdigest() != IMAGE_SHA256:
        raise ValueError('ELF image size or SHA-256 differs from the pinned build')
    if image[:6] != b'\x7fELF\x02\x01':
        raise ValueError('expected a 64-bit little-endian ELF image')
    program_offset = struct.unpack_from('<Q', image, 32)[0]
    entry_size, entry_count = struct.unpack_from('<HH', image, 54)
    segments = []
    for index in range(entry_count):
        offset = program_offset + index * entry_size
        kind, flags, file_offset, virtual, _, file_size, memory_size, _ = struct.unpack_from('<IIQQQQQQ', image, offset)
        if kind == 1:
            segments.append((flags, file_offset, virtual, file_size, memory_size))

    def resolve(virtual, length, *, writable=False):
        for flags, file_offset, start, file_size, memory_size in segments:
            offset = virtual - start
            if offset >= 0 and offset + length <= memory_size and (not writable or flags & 2):
                if not writable and offset + length > file_size:
                    raise ValueError('pin lies outside file-backed bytes')
                return file_offset + offset
        raise ValueError(f'address {virtual:#x} is outside the expected segment')

    for name, virtual, length, expected in PINS:
        offset = resolve(virtual, length)
        if hashlib.sha256(image[offset:offset + length]).hexdigest() != expected:
            raise ValueError(f'{name} pin differs')
        # The test copy, never the ELF, must detect a changed byte.
        changed = bytearray(image[offset:offset + length])
        changed[0] ^= 1
        if hashlib.sha256(changed).hexdigest() == expected:
            raise AssertionError(f'{name} mutation was not detected')
    resolve(0x3DAAAF8, 8, writable=True)
    print('PASS: copied ELF hash, five observer function pins and input-owner storage')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    inspect(parser.parse_args().elf)


if __name__ == '__main__':
    main()

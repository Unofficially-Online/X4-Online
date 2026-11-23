"""A game's files on the master: the session's data the host uploads and the join files of a drop-in game.

The master stores bytes and hashes them; it never opens, unpacks or runs what it stores (play-recipe.json is read as
JSON data by relays.recipe_settings, a join package is checked by its reader before it is offered). Every upload names
its SHA-256 in advance (X-X4MP-Sha256) and is kept only when the bytes match; every name has a size cap, and all files
together count against one budget (MasterLimits.data_bytes_max). A game's files are deleted with the game.

Names and caps (docs/NETPLAY.md): session-save.xml.gz 64 MiB, session.json and play-recipe.json 1 MiB each (the
contract lists the names and the caps in one row; the save is the large one), j<T>.x4join, j<T>.x4join.gz and j<T>.xml.gz 512 MiB each."""
import hashlib
import os
from pathlib import Path
import re
import secrets
import threading

MIB = 1 << 20
SESSION_FILES = {'session-save.xml.gz': 64 * MIB, 'session.json': 1 * MIB, 'play-recipe.json': 1 * MIB}
# j<T>.x4join.gz: the package gzipped (level 1, about 30 MB for 192 MB; x4mods-15's fast join): its identity stays the
# raw package's SHA-256 the relay announced, which the joiner checks after unpacking (the master cannot without it).
JOIN_FILE = re.compile(r'j(\d{1,7})\.(?:x4join|x4join\.gz|xml\.gz)')
JOIN_FILE_MAX = 512 * MIB
SHA256 = re.compile(r'[0-9a-f]{64}')
CHUNK = 1 << 20
RANGE = re.compile(r'bytes=(\d*)-(\d*)')


class UploadError(Exception):
    """An upload that is not kept: (status, error code, why)."""
    def __init__(self, status, code, why):
        super().__init__(why)
        self.status = status
        self.code = code
        self.why = why


def cap_for(name):
    """The size cap of a file name, or None when the name is not one the master stores."""
    if name in SESSION_FILES:
        return SESSION_FILES[name]
    if JOIN_FILE.fullmatch(name):
        return JOIN_FILE_MAX
    return None


def join_tick(name):
    match = JOIN_FILE.fullmatch(name)
    return int(match.group(1)) if match else None


class DataBudget:
    """The bytes of every stored file together. An upload reserves its announced length before the first byte is
    read, so concurrent uploads cannot overrun the budget between them."""
    def __init__(self, limit):
        self.limit = limit
        self.used = 0
        self.lock = threading.Lock()

    def reserve(self, size):
        with self.lock:
            if self.used + size > self.limit:
                return False
            self.used += size
            return True

    def release(self, size):
        with self.lock:
            self.used = max(0, self.used - size)


def read_block(stream, size):
    """A block of the request body; a stalled or reset connection is a short body, not a storage failure."""
    try:
        return stream.read(size)
    except OSError:
        return b''


def receive(stream, length, folder, expect_sha, final_name=None):
    """Read exactly `length` bytes of `stream` into a new file in `folder`, hashing them as they come. The file is
    kept only when the SHA-256 is `expect_sha`; returns its path (final_name, or a fresh name: a download of the
    previous version may still have that one open, and Windows cannot replace an open file)."""
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    part = folder / f'.upload-{secrets.token_hex(8)}.part'
    digest = hashlib.sha256()
    received = 0
    try:
        with part.open('wb') as out:
            while received < length:
                block = read_block(stream, min(CHUNK, length - received))
                if not block:
                    break
                out.write(block)
                digest.update(block)
                received += len(block)
        if received < length:
            raise UploadError(400, 'short_body', f'the upload ended after {received} of {length} bytes')
        if digest.hexdigest() != expect_sha:
            raise UploadError(400, 'bad_sha256', 'the bytes do not match X-X4MP-Sha256')
        target = folder / (final_name or f'{secrets.token_hex(8)}.bin')
        os.replace(part, target)
        return target
    except OSError as error:
        raise UploadError(503, 'storage', f'the file could not be stored ({error.__class__.__name__})') from None
    finally:
        try:
            part.unlink()
        except OSError:
            pass


def parse_range(header, size):
    """The byte range a Range header asks of a file of `size` bytes: (first, last) inclusive, None for the whole file,
    or ValueError when it cannot be served (416). One range only; a list of ranges is served whole."""
    if not header:
        return None
    match = RANGE.fullmatch(header.strip())
    if not match:
        return None
    first_text = match.group(1)
    last_text = match.group(2)
    if not first_text and not last_text:
        return None
    if not first_text:
        suffix = int(last_text)
        if suffix == 0 or size == 0:
            raise ValueError('empty suffix range')
        return max(0, size - suffix), size - 1
    first = int(first_text)
    last = int(last_text) if last_text else size - 1
    if first >= size or last < first:
        raise ValueError('range past the end')
    return first, min(last, size - 1)


def send_range(path, first, last, out):
    """Write bytes first..last (inclusive) of `path` to `out`."""
    with open(path, 'rb') as source:
        source.seek(first)
        left = last - first + 1
        while left > 0:
            block = source.read(min(CHUNK, left))
            if not block:
                break
            out.write(block)
            left -= len(block)

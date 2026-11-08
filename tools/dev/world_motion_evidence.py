"""Strict bounded Linux Nav watch evidence, independent of census hash coverage.

Physical selectors are diagnostics. Equality uses canonical motion and timing
preimages, including the census type/flags/portable-parent trailer.
"""
import hashlib
import re
import struct

from native_world_evidence import DETAIL, OBJECT

SAVED_ID = re.compile(r'[0-7][0-9a-f]{15}')
ARMED = re.compile(r'NATIVE_GATE WORLD_MOTION_ARMED version=1 first=(\d+) last=(\d+) ids=([0-9a-f,]+)\s*$')
ROW = re.compile(
    r'NATIVE_GATE WORLD_MOTION version=1 tick=(\d+) id=([0-9a-f]{16}) '
    r'type=([0-9a-f]{8}) flags=([0-9a-f]{8}) parent=([0-9a-f]{16}) '
    r'read=([01]) write=([01]) cursor=([012]) motion=([0-9a-f]{412}) timing=([0-9a-f]{190})\s*$')
FOOTER = re.compile(r'NATIVE_GATE WORLD_MOTION_STOP count=(\d+) first=(\d+) last=(\d+) samples=(\d+) failures=(\d+)\s*$')
STOP = re.compile(r'NATIVE_GATE STOP slot=p(\d+) completed=(\d+) reason=(\d+) restored=(\d+)(?:\s|$)')


def validate_configuration(identities, window, ticks=None):
    if (not isinstance(identities, (list, tuple)) or not 1 <= len(identities) <= 4
            or any(not isinstance(saved, str) or not SAVED_ID.fullmatch(saved)
                   or int(saved, 16) == 0 for saved in identities) or len(set(identities)) != len(identities)):
        raise ValueError('world motion watch requires 1..4 unique nonzero imported saved IDs')
    if (not isinstance(window, (list, tuple)) or len(window) != 2
            or any(type(tick) is not int for tick in window)
            or not 1 <= window[0] <= window[1] <= 9007199254740991
            or window[1] - window[0] >= 18000 or (ticks is not None and window[1] > ticks)):
        raise ValueError('world motion window requires FIRST..LAST, at most 18000 ticks, entirely within the run')


def configured_watch(config):
    """Read literal generated assignments; duplicate or nonliteral config fails closed."""
    values = []
    for key in ('watch', 'window'):
        entries = re.findall(r'\bX4LAN_CONFIG\.native_gate_world_motion_' + key + r'\s*=([^;\n]*)', config)
        if not entries:
            values.append(None)
            continue
        if len(entries) != 1 or not re.fullmatch(r'\s*\{[^{}]*\}\s*', entries[0]):
            raise ValueError('invalid or duplicate world motion configuration')
        tokens = entries[0].strip()[1:-1].split(',')
        if key == 'watch':
            if any(not re.fullmatch(r'''\s*(['"])[0-9a-f]{16}\1\s*''', token) for token in tokens):
                raise ValueError('nonliteral world motion watch')
            values.append([token.strip()[1:-1] for token in tokens])
        else:
            if any(not re.fullmatch(r'\s*\d+\s*', token) for token in tokens):
                raise ValueError('nonliteral world motion window')
            values.append([int(token) for token in tokens])
    if any(value is not None for value in values):
        validate_configuration(*values)
    return values


class MotionEvidence:
    def __init__(self, slot, identities, window, ticks):
        validate_configuration(identities, window, ticks)
        self.slot, self.identities, self.window = slot, list(identities), list(window)
        self.rows, self.errors = {}, []
        self.armed, self.footers, self.stopped, self.census_checks = 0, 0, False, 0
        self.stops = []

    def error(self, message):
        if len(self.errors) < 64:
            self.errors.append(f'client {self.slot}: {message}')

    def observe(self, line):
        if 'NATIVE_GATE STOP ' in line:
            self.stopped = True
            match = STOP.search(line)
            self.stops.append(tuple(map(int, match.groups())) if match else None)
        if 'NATIVE_GATE WORLD_MOTION' in line:
            if 'NATIVE_GATE WORLD_MOTION_ARMED ' in line:
                match = ARMED.search(line)
                self.armed += 1
                if (not match or self.armed != 1 or self.rows or self.stopped
                        or [int(match[1]), int(match[2])] != self.window
                        or match[3].split(',') != self.identities):
                    self.error('malformed, duplicate, late or inconsistent WORLD_MOTION_ARMED')
            elif 'NATIVE_GATE WORLD_MOTION_STOP ' in line:
                match = FOOTER.search(line)
                self.footers += 1
                expected = [len(self.identities), *self.window,
                            len(self.identities) * (self.window[1] - self.window[0] + 1), 0]
                if (not match or [int(value) for value in match.groups()] != expected
                        or self.footers != 1 or not self.stopped):
                    self.error('malformed, duplicate, early or unsuccessful WORLD_MOTION_STOP')
            else:
                self.accept_row(line)
        # When full census rows are retained, independently verify their bytes or
        # leaf hashes against this observer at the same tick. No inventory scan.
        if 'NATIVE_GATE WORLD_DETAIL ' in line:
            match = DETAIL.search(line)
            if match and match[3] in ('motion', 'timing'):
                watched = self.rows.get((int(match[1]), match[2]))
                if watched:
                    self.census_checks += 1
                    if watched[match[3]] != match[4]:
                        self.error(f'WORLD_DETAIL {match[3]} differs from motion watch at {match[1]} / {match[2]}')
        elif 'NATIVE_GATE WORLD_OBJECT ' in line:
            match = OBJECT.search(line)
            if match:
                watched = self.rows.get((int(match[1]), match[2]))
                if watched:
                    for domain, group in (('motion', 8), ('timing', 9)):
                        self.census_checks += 1
                        digest = hashlib.sha256(bytes.fromhex(watched[domain])).hexdigest()
                        if digest != match[group]:
                            self.error(f'WORLD_OBJECT {domain} leaf differs from motion watch at {match[1]} / {match[2]}')

    def accept_row(self, line):
        match = ROW.search(line)
        if not match:
            self.error('malformed, truncated or failed WORLD_MOTION row')
            return
        tick, saved = int(match[1]), match[2]
        key = (tick, saved)
        if not self.window[0] <= tick <= self.window[1] or saved not in self.identities:
            self.error('unexpected WORLD_MOTION tick or identity')
            return
        if self.armed != 1 or self.stopped or self.footers:
            self.error('WORLD_MOTION outside armed live interval')
        position = len(self.rows)
        expected_key = (self.window[0] + position // len(self.identities), self.identities[position % len(self.identities)])
        if key in self.rows or key != expected_key:
            self.error('duplicate, missing or out-of-order WORLD_MOTION')
        motion, timing = bytes.fromhex(match[9]), bytes.fromhex(match[10])
        trailer = struct.pack('<IIQ', int(match[3], 16), int(match[4], 16), int(match[5], 16))
        read, write, cursor = map(int, (match[6], match[7], match[8]))
        relation = 2 if cursor == 2 else 0 if cursor == read else 1
        if (read + write != 1 or motion[-16:] != trailer or timing[-16:] != trailer
                or struct.unpack_from('<I', motion)[0] != 0x2c51158
                or struct.unpack_from('<I', timing, 12)[0] != relation
                or int(match[4], 16) not in (5, 7)):
            self.error(f'inconsistent canonical WORLD_MOTION at {tick} / {saved}')
        if key not in self.rows:
            self.rows[key] = dict(motion=match[9], timing=match[10])

    def finish(self):
        expected = len(self.identities) * (self.window[1] - self.window[0] + 1)
        if self.armed != 1 or self.footers != 1 or not self.stopped or len(self.rows) != expected:
            self.error(f'incomplete WORLD_MOTION evidence: {len(self.rows)}/{expected} rows, '
                       f'{self.armed} arm receipts, {self.footers} footers, stopped={self.stopped}')
        if (len(self.stops) != 1 or self.stops[0] is None or self.stops[0][0] != self.slot
                or self.stops[0][1] < self.window[1] or self.stops[0][2:] != (1, 1)):
            self.error('missing, duplicate or unsuccessful STOP for motion evidence')


def compare_evidence(clients):
    if not clients:
        raise ValueError('motion evidence requires at least one local client')
    errors = []
    for client in clients:
        client.finish()
        errors.extend(client.errors)
    differences = []
    for peer in clients[1:]:
        reference = clients[0]
        first = next((key for key in sorted(reference.rows.keys() & peer.rows.keys())
                      if reference.rows[key] != peer.rows[key]), None)
        if first:
            domains = [domain for domain in ('motion', 'timing') if reference.rows[first][domain] != peer.rows[first][domain]]
            differences.append(dict(slots=[reference.slot, peer.slot], tick=first[0], saved_id=first[1], domains=domains))
    if differences:
        errors.append(f'WORLD_MOTION differs at tick {min(row["tick"] for row in differences)}')
    return dict(world_motion_rows={client.slot: len(client.rows) for client in clients},
                world_motion_census_checks={client.slot: client.census_checks for client in clients},
                world_motion_first_difference=min((row['tick'] for row in differences), default=None),
                world_motion_compared_pairs=len(clients) - 1,
                world_motion_peer_equal=(not errors if len(clients) > 1 else None),
                world_motion_differences=differences, world_motion_errors=errors, world_motion_ok=not errors)

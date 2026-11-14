"""Explicit VIEW evidence against the production C codec and full raw proofs."""
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import unittest

import linux_player_network_evidence as evidence

ROOT = Path(__file__).resolve().parents[1]


class NetworkViewEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            source = directory / 'records.c'
            source.write_text(r'''
#include "lockstep_stock_record.h"
#include <stdio.h>
static void emit(stock_input_record *record) {
    unsigned char bytes[STOCK_INPUT_MAX_BYTES];
    unsigned length=stock_input_encode(record,bytes,sizeof bytes);
    for(unsigned i=0;i<length;++i)printf("%02x",bytes[i]);
    putchar('\n');
}
int main(void) {
    stock_input_record record={.flags=STOCK_INPUT_ACTIVE|STOCK_INPUT_CURSOR|STOCK_INPUT_VIEW,
        .capture16=38,.cursor={0.125f,-0.25f,0},.view=3,.fov=0.75f,
        .target=UINT64_C(0xfedcba9876543210),.count=1,
        .events={{STOCK_INPUT_STATE,2,1}}};
    emit(&record);
    record.count=0;emit(&record);
    record.flags=STOCK_INPUT_VIEW;record.cursor[0]=record.cursor[1]=0;
    record.count=1;record.events[0]=(stock_input_event){STOCK_INPUT_CONTEXT,8,0};emit(&record);
    return 0;
}
''')
            binary = directory / 'records'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'src/native/engine'), str(source), '-o', str(binary)], check=True)
            cls.active, cls.neutral, cls.inactive = subprocess.check_output([str(binary)], text=True).splitlines()

    def test_production_layout_and_explicit_default_refusal(self):
        self.assertEqual(evidence.decode_record(self.active, 40, 0, allow_cursor=True, allow_view=True), 1)
        self.assertEqual(evidence.decode_record(self.neutral, 40, 0, allow_cursor=True, allow_view=True), 0)
        self.assertEqual(evidence.decode_record(self.inactive, 40, 0, allow_cursor=True,
                                               allow_space=True, allow_view=True), 0)
        raw = bytes.fromhex(self.active)
        self.assertEqual(raw[29], 3)
        self.assertEqual(struct.unpack('<f', raw[30:34])[0], 0.75)
        self.assertEqual(int.from_bytes(raw[34:42], 'little'), 0xfedcba9876543210)
        for kwargs in ({}, {'allow_cursor': True}, {'allow_view': True},
                       {'allow_cursor': True, 'allow_view': 1},
                       {'allow_cursor': True, 'allow_view': None}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                evidence.decode_record(self.active, 40, 0, **kwargs)
        with self.assertRaises(ValueError):
            evidence.decode_record(self.inactive, 40, 0, allow_cursor=True, allow_view=True)
        for fov in (-0.0, -2.0):
            # The C wire representation requires finite, not positive, FOV.
            present = bytearray.fromhex(self.neutral)
            present[30:34] = struct.pack('<f', fov)
            self.assertEqual(evidence.decode_record(present.hex(), 40, 0,
                                                   allow_cursor=True, allow_view=True), 0)
        wrapped = bytearray.fromhex(self.neutral)
        wrapped[2:4] = (0xffff).to_bytes(2, 'little')
        self.assertEqual(evidence.decode_record(wrapped.hex(), 65537, 0, allow_cursor=True, allow_view=True), 0)

    def test_malformed_unused_nonfinite_event_and_truncation_refusals(self):
        original = bytearray.fromhex(self.active)
        variants = []
        for offset, value in ((0, 2), (2, 39), (16, 1), (28, 1), (29, 4), (42, 32)):
            changed = original.copy(); changed[offset] = value; variants.append(changed)
        for flag in (4, 16, 32, 64):
            changed = original.copy(); changed[1] |= flag; variants.append(changed)
        for value in (float('nan'), float('inf'), -float('inf')):
            changed = original.copy(); changed[30:34] = struct.pack('<f', value); variants.append(changed)
        for kind in (0, 2, 3):
            changed = original.copy(); changed[43:45] = ((kind << 14) | 2).to_bytes(2, 'little'); variants.append(changed)
        changed = original.copy(); changed[1] &= ~8; variants.append(changed)
        changed = original.copy(); changed[1] &= ~2; variants.append(changed)
        variants.extend((original[:42], original[:-1], original + b'\0'))
        for changed in variants:
            with self.subTest(raw=changed.hex()), self.assertRaises(ValueError):
                evidence.decode_record(changed.hex(), 40, 0, allow_cursor=True, allow_view=True)
        # Absent VIEW requires positive-zero bits, including FOV and target.
        for offset in (29, 30, 33, 34):
            changed = bytearray(43); changed[0:4] = bytes((3, 3, 38, 0)); changed[offset] = 128
            with self.assertRaises(ValueError):
                evidence.decode_record(changed.hex(), 40, 0, allow_cursor=True, allow_view=True)

    def trace(self):
        from test_linux_t40_evidence import common_trace
        rows, stderr, engine = common_trace()
        def replace(line):
            if not any(marker in line for marker in ('physics=network ', 'KEYBOARD_SLOT1 ', 'LINUX_KEYBOARD_CAPTURE ')):
                return line
            match = re.search(r'record=([0-9a-f]+)', line)
            old = bytes.fromhex(match[1])
            new = bytearray.fromhex(self.neutral)
            new[2:4] = old[2:4]
            return line[:match.start(1)] + new.hex() + line[match.end(1):]
        return tuple([replace(line) for line in stream] for stream in (rows, stderr, engine))

    def test_full_collector_preserves_raw_echo_and_slot_records(self):
        import linux_gate_live as live
        rows, stderr, engine = self.trace()
        p1 = evidence.parse_network_physics_evidence(rows, stderr, engine, 223,
            frame_count=180, slot=1, allow_cursor=True, allow_view=True,
            session_frames=180, startup='v1-t40')
        self.assertTrue(p1['pass'], p1['errors'][:5])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = {name: root / name for name in ('trace', 'engine', 'stdout', 'stderr')}
            paths['trace'].write_text(''.join(rows)); paths['engine'].write_text(''.join(engine))
            paths['stderr'].write_text(''.join(stderr)); paths['stdout'].touch()
            def collect(name, **kwargs):
                arguments = (paths['trace'], root / name, paths['engine'], paths['stdout'], 223)
                return live.collect_network_client(arguments, paths['stderr'], 180, 2,
                    cursor=True, session_frames=180, startup='v1-t40', **kwargs)
            proof = collect('enabled', allow_view=True)
            self.assertTrue(proof['pass'], proof['errors'][:5])
            self.assertEqual(proof['stderr_sha256'], hashlib.sha256(paths['stderr'].read_bytes()).hexdigest())
            self.assertEqual(proof['log_sha256'], hashlib.sha256(paths['trace'].read_bytes()).hexdigest())
            self.assertEqual(proof['slot_records']['2'][0], re.search('record=([0-9a-f]+)', engine[0])[1])
            self.assertFalse(collect('default')['pass'])
            # Actual local echo disagreement must still fail even with valid VIEW.
            changed = list(engine)
            encoded = re.search('record=([0-9a-f]+)', changed[0])[1]
            raw = bytearray.fromhex(encoded); raw[34] ^= 1
            changed[0] = changed[0].replace(encoded, raw.hex())
            paths['engine'].write_text(''.join(changed))
            self.assertFalse(collect('changed-echo', allow_view=True)['pass'])
        for kwargs in ({'allow_cursor': False}, {'startup': None}, {'session_frames': 0}, {'allow_view': 1}):
            options = dict(frame_count=180, slot=2, allow_cursor=True, allow_view=True,
                           startup='v1-t40', session_frames=180)
            options.update(kwargs)
            self.assertFalse(evidence.parse_network_physics_evidence(rows, stderr, engine, 223, **options)['pass'])

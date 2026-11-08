"""Synthetic receipts: no X4, engine executable or native process is used."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
from world_motion_evidence import MotionEvidence, compare_evidence, configured_watch, validate_configuration
from linux_gate_evidence import validate_evidence
from test_linux_gate_evidence import log as census_log

SAVED = '000000000006ec76'


def motion_row(tick, saved=SAVED, swapped=False, changed=False):
    trailer = struct.pack('<IIQ', 0x2b82440, 7, 0x100)
    motion, timing = bytearray(190), bytearray(79)
    struct.pack_into('<I', motion, 0, 0x2c51158)
    struct.pack_into('<I', timing, 12, 0)
    motion[4] = int(changed)
    return (f'NATIVE_GATE WORLD_MOTION version=1 tick={tick} id={saved} '
            f'type=02b82440 flags=00000007 parent=0000000000000100 '
            f'read={int(swapped)} write={int(not swapped)} cursor={int(swapped)} '
            f'motion={(motion + trailer).hex()} timing={(timing + trailer).hex()}')


def motion_log(slot=1, swapped=False, changed=None):
    return [f'NATIVE_GATE WORLD_MOTION_ARMED version=1 first=2 last=3 ids={SAVED}',
            *[motion_row(tick, swapped=swapped, changed=tick == changed) for tick in (2, 3)],
            f'NATIVE_GATE STOP slot=p{slot} completed=120 reason=1 restored=1',
            'NATIVE_GATE WORLD_MOTION_STOP count=1 first=2 last=3 samples=2 failures=0']


def parsed(lines, slot=1):
    evidence = MotionEvidence(slot, [SAVED], [2, 3], 120)
    for line in lines:
        evidence.observe(line)
    return evidence


class MotionEvidenceTests(unittest.TestCase):
    def test_empty_rejected_and_single_local_slot_has_no_peer_verdict(self):
        with self.assertRaises(ValueError):
            compare_evidence([])
        result = compare_evidence([parsed(motion_log())])
        self.assertTrue(result['world_motion_ok'])
        self.assertEqual(result['world_motion_compared_pairs'], 0)
        self.assertIsNone(result['world_motion_peer_equal'])
    def test_physical_selector_swaps_do_not_compare(self):
        result = compare_evidence([parsed(motion_log()), parsed(motion_log(2, swapped=True), 2)])
        self.assertTrue(result['world_motion_ok'], result)
        self.assertEqual(result['world_motion_compared_pairs'], 1)
        self.assertTrue(result['world_motion_peer_equal'])
        self.assertEqual(result['world_motion_rows'], {1: 2, 2: 2})

    def test_exact_first_tick_and_domain(self):
        result = compare_evidence([parsed(motion_log()), parsed(motion_log(2, changed=3), 2)])
        self.assertFalse(result['world_motion_ok'])
        self.assertEqual(result['world_motion_first_difference'], 3)
        self.assertEqual(result['world_motion_differences'][0]['domains'], ['motion'])

    def test_four_identity_cartesian_window_requires_every_row(self):
        identities = [f'{identity:016x}' for identity in range(1, 5)]
        rows = ['NATIVE_GATE WORLD_MOTION_ARMED version=1 first=2 last=3 ids=' + ','.join(identities),
                *[motion_row(tick, saved=saved) for tick in (2, 3) for saved in identities],
                'NATIVE_GATE STOP slot=p1 completed=3 reason=1 restored=1',
                'NATIVE_GATE WORLD_MOTION_STOP count=4 first=2 last=3 samples=8 failures=0']
        for selected, expected in ((rows, True), (rows[:4] + rows[5:], False)):
            evidence = MotionEvidence(1, identities, [2, 3], 3)
            for line in selected:
                evidence.observe(line)
            self.assertEqual(compare_evidence([evidence])['world_motion_ok'], expected)

    def test_missing_duplicate_truncated_out_of_order_and_failed_receipts(self):
        good = motion_log()
        cases = [good[:1] + good[2:], good[:2] + good[1:], good[:1] + [good[1][:-1]] + good[2:],
                 [good[0], good[2], good[1], *good[3:]], good[1:], good[:-1], good + [good[-1]],
                 [good[0], good[1].replace('read=0 write=1', 'read=0 write=0'), *good[2:]],
                 [good[0], good[1].replace('cursor=0', 'cursor=1'), *good[2:]],
                 [good[0], 'NATIVE_GATE WORLD_MOTION_ERROR tick=2 stage=identity_recheck', *good[2:]],
                 [line.replace('failures=0', 'failures=1') for line in good],
                 [line.replace('reason=1', 'reason=2') for line in good],
                 good + [good[-2]]]
        for lines in cases:
            with self.subTest(lines=lines):
                self.assertFalse(compare_evidence([parsed(lines)])['world_motion_ok'])

    def test_metadata_and_domain_bytes_match_retained_census(self):
        lines = motion_log()
        row = lines[1]
        motion = row.split(' motion=')[1].split()[0]
        timing = row.split(' timing=')[1]
        detail = f'NATIVE_GATE WORLD_DETAIL tick=2 id={SAVED} domain=motion bytes={motion}'
        leaf = (f'NATIVE_GATE WORLD_OBJECT tick=2 id={SAVED} type=2b82440 flags=7 parent=0000000000000100 '
                'topology=' + 'a' * 64 + ' health=' + 'b' * 64 +
                ' motion=' + hashlib.sha256(bytes.fromhex(motion)).hexdigest() +
                ' timing=' + hashlib.sha256(bytes.fromhex(timing)).hexdigest())
        for extra, expected in (([detail, leaf], True), ([detail[:-1] + 'f'], False),
                                ([leaf.replace(hashlib.sha256(bytes.fromhex(motion)).hexdigest(), 'c' * 64)], False)):
            result = compare_evidence([parsed(lines[:3] + extra + lines[3:])])
            self.assertEqual(result['world_motion_ok'], expected, result)
            self.assertGreater(result['world_motion_census_checks'][1], 0)

    def test_manifest_wrapper_validates_one_stream_pass_and_preserves_census(self):
        streams = {slot: iter(motion_log(slot)[:-2] + census_log(slot) + motion_log(slot)[-1:]) for slot in (1, 2)}
        result = validate_evidence(streams, 120, 60, motion_watch=[SAVED], motion_window=[2, 3])
        self.assertTrue(result['evidence_ok'], result)
        self.assertEqual(result['world_hash_ticks'], 3)
        self.assertEqual(result['world_motion_rows'], {1: 2, 2: 2})

    def test_strict_configuration_and_full_18000_tick_bound(self):
        validate_configuration([SAVED], [1, 18000], 18000)
        validate_configuration([SAVED], [8000, 12000], 12000)
        for ids, window, ticks in (([], [1, 2], 3), ([SAVED] * 2, [1, 2], 3),
                                   (['8000000000000001'], [1, 2], 3), ([SAVED], [1, 18001], 20000),
                                   ([SAVED], [2, 3], 2), ([SAVED], [True, 3], 3), ([SAVED], None, 3)):
            with self.assertRaises(ValueError):
                validate_configuration(ids, window, ticks)
        config = (f"X4LAN_CONFIG.native_gate_world_motion_watch = {{'{SAVED}'}}\n"
                  'X4LAN_CONFIG.native_gate_world_motion_window = {8000,12000}\n')
        self.assertEqual(configured_watch(config), [[SAVED], [8000, 12000]])
        with self.assertRaises(ValueError):
            configured_watch(config + config)


if __name__ == '__main__':
    unittest.main()

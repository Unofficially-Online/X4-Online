"""tools/dev/census_hybrid.py on synthetic two-client runs: the census filter (membership by parent chain, absent
objects, the gate's outside roots), the outside comparison, membership differences, sim_us before/after and the
bandwidth figures. Offline, no game."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/dev'))
import census_hybrid
from native_world_evidence import DOMAINS, roots

CONFIG = ("X4LAN_CONFIG.native_gate_world_hash_schema = 'WORLD_IMPORTED_1'\nX4LAN_CONFIG.native_gate_world_hash_stride = 60\n"
          "X4LAN_CONFIG.native_gate_tick_hz = 30\nX4LAN_CONFIG.native_gate_hybrid = {owners={2},tick=30,poses=4}\n")
GALAXY, CLUSTER, S1, S2, Z1, Z2, A, B, N, T = range(0x10, 0x1a)
PARENTS = {GALAXY: 0, CLUSTER: GALAXY, S1: CLUSTER, S2: CLUSTER, Z1: S1, Z2: S2, A: Z1, B: Z2, N: Z2, T: Z2}


def record(identity, parent=0, state='a', present=True):
    kind, flags = (0x4000, 1) if present else (0, 0)
    timing = (b'\x02\0\0\0' if present else b'')+struct.pack('<IIQ', kind, flags, parent)
    hashes = tuple(hashlib.sha256(f'{identity}/{state}/{d}'.encode()).digest() for d in DOMAINS[:3])+(hashlib.sha256(timing).digest(),)
    return {'type': kind, 'flags': flags, 'parent': parent, 'hashes': hashes, 'timing': timing[:-16]}


def world(states=None, parents=None, absent=()):
    states, parents = states or {}, {**PARENTS, **(parents or {})}
    return {i: record(i, 0 if i in absent else p, states.get(i, 'a'), i not in absent) for i, p in parents.items()}


def capture(tick, objects):
    lines = ''.join(f'[X4LAN] NATIVE_GATE WORLD_OBJECT tick={tick} id={i:016x} type={r["type"]:x} flags={r["flags"]:x} parent={r["parent"]:016x} '
                    + ' '.join(f'{n}={v.hex()}' for n, v in zip(DOMAINS, r['hashes'])) + f' timing_fields={r["timing"].hex() or "-"}\n'
                    for i, r in sorted(objects.items()))
    present = sum(1 for r in objects.values() if r['flags'])
    state = f'WORLD_IMPORTED_1:{tick}:{len(objects)}:' + ':'.join(roots(objects))
    return lines + (f'[X4LAN] NATIVE_GATE WORLD_HASH tick={tick} objects={len(objects)} present={present} changed={len(objects)} '
                    f'unknown_class=0 unknown_motion=0 unknown_references=0 state={state}\n')


def gate_census(tick, objects, inside, roots_override=None):
    outside = census_hybrid.outside_roots(objects, inside)
    if roots_override: outside = roots_override
    return (f'[X4LAN] NATIVE_GATE HYBRID_CENSUS tick={tick} outside={len(objects)-len(inside)} inside={len(inside)} absent_inside=0 '
            f'members={len(inside)} movers=1 entered=0 left=0 inside_root={"0"*64} state=HYBRID_OUTSIDE_1:{tick}:{len(objects)-len(inside)}:'
            + ':'.join(outside) + '\n')


def frames(sims):
    return ''.join(f'[X4LAN] NATIVE_GATE FRAME_TIMING tick={t} frames=1 wait_us=10 frame_us=20 outside_us=30 sim_us={us} probe_us=5\n'
                   for t, us in sims)


def client_log(owner, captures, sims, status_extra=''):
    text = '[X4LAN] NATIVE_GATE HYBRID_ARMED owners=2 tick=30 poses=4\n'
    for tick, objects, inside in captures:
        text += capture(tick, objects)
        if inside is not None: text += gate_census(tick, objects, inside)
    text += frames(sims)
    text += (f'[X4LAN] NATIVE_GATE HYBRID tick=600 armed=1 active=1 owners=2 local={2 if owner else 1} tick=30 started=30 '
             f'sectors=2:{S2:x} owner={int(owner)} view_index={1 if owner else -1} members=4 movers=1 own_movers={int(owner)} '
             f'live_movers={int(owner)} scan_us=900 frame_us=12 frame_us_max=40 rebuilds=2 entered=0 left=0 eval_inside=5 eval_outside=7 '
             f'built={570 if owner else 0} proposed={570 if owner else 0} squeezed=0 entries_out={570 if owner else 0} bytes_mean=63.0 '
             f'bytes_max=63 full_bytes=63 digest=0 pose_misses=0 records=0/569/0/0 entries_in=569 missing=0 proxies={0 if owner else 569} '
             f'unknown=0 parent_mismatch=0 unread=0 err_mean_m={0 if owner else 12.5} err_max_m={0 if owner else 80.25} census=1 '
             f'failures=0 why=none{status_extra}\n')
    return text


def make_run(root, logs):
    for slot, text in enumerate(logs, 1):
        client = Path(root)/f'client{slot}'
        (client/'installed').mkdir(parents=True)
        (client/'installed/x4_lan_lab-config.lua').write_text(CONFIG)
        (client/'engine-output.log').write_text(text)
    return Path(root)


INSIDE = {S2, Z2, B, N, T}
SIMS1 = [(20, 30000), (25, 31000), (400, 26000), (410, 27000), (430, 26500)]
SIMS2 = [(20, 30500), (25, 30000), (400, 30000), (410, 29000), (430, 31000)]


class CensusHybridTests(unittest.TestCase):
    def run_pair(self, second_objects, second_inside=None, first_inside=None, override=None):
        first = world()
        with tempfile.TemporaryDirectory() as root:
            log1 = client_log(False, [(1, first, None), (60, world(states={N: 'low'}), first_inside or INSIDE)], SIMS1)
            log2 = client_log(True, [(1, first, None), (60, second_objects, second_inside or INSIDE)], SIMS2)
            if override: log2 = log2.replace(override[0], override[1])
            return census_hybrid.analyse(make_run(root, [log1, log2]))

    def test_inside_differences_are_filtered_and_the_outside_stays_identical(self):
        result = self.run_pair(world(states={N: 'high', B: 'high'}))
        self.assertEqual(result['sectors'], [f'{S2:016x}'])
        v = result['verdict']
        self.assertTrue(v['outside_identical'] and v['membership_identical'] and v['classification_ok'])
        self.assertIsNone(v['first_outside_difference'])
        pair = result['pairs']['client2'][0]
        self.assertEqual((pair['tick'], pair['outside'], pair['inside'], pair['differing']['motion']), (60, 5, [5, 5], 0))
        c1, c2 = result['clients']['client1'], result['clients']['client2']
        self.assertEqual((c1['sim_ms_before'], c1['sim_ms_after'], c2['sim_ms_after']), (30.5, 26.5, 30.0))
        self.assertEqual((v['owner_sim_ms'], v['other_sim_ms']), ([30.0], [26.5]))
        self.assertEqual((c2['record_kbps'], c2['full_kbps'], c1['err_max_m'], c1['proxies']), (15.1, 15.1, '80.25', '569'))

    def test_an_outside_difference_is_named_with_its_domain(self):
        result = self.run_pair(world(states={A: 'other'}))
        v = result['verdict']
        self.assertFalse(v['outside_identical'])
        self.assertEqual(v['first_outside_difference'], 60)
        pair = result['pairs']['client2'][0]
        self.assertEqual(pair['differing']['motion'], 1)
        self.assertEqual(pair['samples']['motion'], [f'{A:016x}/4000'])

    def test_a_boundary_crossing_is_a_membership_difference_not_an_outside_one(self):
        # N left S2 on the owner only (flew to Z1): inside on client 1, outside on client 2; excluded from the comparison.
        result = self.run_pair(world(states={N: 'moved'}, parents={N: Z1}), second_inside=INSIDE - {N})
        v = result['verdict']
        self.assertTrue(v['outside_identical'])
        self.assertFalse(v['membership_identical'])
        pair = result['pairs']['client2'][0]
        self.assertEqual((pair['membership_differs'], pair['membership_samples'], pair['outside']), (1, [f'{N:016x}'], 5))

    def test_an_absent_object_keeps_its_previous_membership_and_the_gate_roots_are_checked(self):
        # T destroyed on the owner between the captures: inside by the previous capture's membership.
        result = self.run_pair(world(absent=(T,)))
        self.assertTrue(result['verdict']['classification_ok'] and result['verdict']['outside_identical'])
        # The gate classified differently from the parent chains: reported, not hidden.
        wrong = census_hybrid.outside_roots(world(), set())
        result = self.run_pair(world(), override=(census_hybrid.outside_roots(world(), INSIDE)[0], wrong[0]))
        self.assertEqual(result['clients']['client2']['classification'], 'differs at 60')
        self.assertFalse(result['verdict']['classification_ok'])

    def test_membership_walks_parents_and_keeps_absent_objects_inside(self):
        objects = world(absent=(T,))
        inside = census_hybrid.membership(objects, {S2}, {T})
        self.assertEqual(inside, INSIDE)
        self.assertEqual(census_hybrid.membership(objects, {S2}, set()), INSIDE - {T})
        self.assertEqual(census_hybrid.membership(objects, set(), set()), set())


if __name__ == '__main__':
    unittest.main()

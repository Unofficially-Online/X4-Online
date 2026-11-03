"""tools/dev/census_fp_attack.py (FR29b): two clients' FP_ATTACK_ROWs aligned, the first difference named."""
import contextlib
import importlib.util
import io
from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('census_fp_attack', ROOT/'tools/dev/census_fp_attack.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def scenario_rows():
    """The rows of src/native/engine/test_fp_attack_scenario.h (both gates print them; in= from the Linux test)."""
    text = (ROOT/'src/native/engine/test_fp_attack_scenario.h').read_text()
    body = text[text.index('tfa_rows[TFA_ROWS]={'):text.index('};', text.index('tfa_rows[TFA_ROWS]={'))]
    joined = ''.join(re.findall(r'"([^"]*)"', body))
    rows = ['index=' + r for r in joined.split('index=') if r]
    scopes = ['gas', 'wl', 'wl', 'wl', 'gas', 'gas', 'exec', 'apply']
    out = []
    for r, scope in zip(rows, scopes):
        head, rest = r.split(' thread=', 1)
        out.append(f'{head} in={scope} thread={rest}')
    assert len(out) == 8, len(out)
    return out


def d(x):
    return struct.pack('<d', x).hex()


def log(rows):
    return ''.join(f'[X4LAN] NATIVE_GATE FP_ATTACK_ROW {r}\n' for r in rows)


def run(left, right, *extra):
    with tempfile.TemporaryDirectory() as folder:
        a, b = Path(folder)/'a.log', Path(folder)/'b.log'
        a.write_text(log(left))
        b.write_text(log(right))
        out = io.StringIO()
        argv = sys.argv
        sys.argv = ['census_fp_attack.py', str(a), str(b), *extra]
        try:
            with contextlib.redirect_stdout(out):
                module.main()
        finally:
            sys.argv = argv
        return out.getvalue()


class CensusFpAttackTests(unittest.TestCase):
    def test_equal_runs(self):
        rows = scenario_rows()
        out = run(rows, rows, '--target', '7336a')
        self.assertIn('left: 8 rows, 1 get_attackstrength calls, 1 applies', out)
        self.assertIn('no difference in 1 matched calls', out)
        self.assertIn('applies matched 1, differing 0', out)

    def test_weapon_damage_differs(self):
        rows = scenario_rows()
        right = list(rows)
        # the weapon's hullshield one ulp up in the damage call's output: the first difference is that operation
        old = d(10.5)
        new = struct.pack('<Q', struct.unpack('<Q', bytes.fromhex(old))[0] + 1).hex()
        right[2] = right[2].replace('b1=' + old, 'b1=' + new)
        self.assertNotEqual(right[2], rows[2])
        out = run(rows, right, '--detail')
        self.assertIn('FIRST DIFFERENCE: tick 546 attacker 0000000000096587 -> target 000000000007336a #0', out)
        self.assertIn('OPERATION (equal inputs, different output) at left seq 4 dps in=wl obj=0000000000073371', out)
        self.assertIn('b1.hullshield: left 4025000000000000  right 4025000000000001  10.5 vs 10.500000000000002 (+1 ulp)', out)
        self.assertIn('float 41280000 (10.5)', out)

    def test_draw_input_differs(self):
        rows = scenario_rows()
        right = list(rows)
        # the loop's bound one ulp off: an input of the draw (seq 3), before the damage call
        right[1] = right[1].replace('args=43160000,', 'args=43160001,')
        out = run(rows, right)
        self.assertIn('INPUT at left seq 3 rng in=wl / right seq 3 rng in=wl', out)
        self.assertIn('arg bound: left 43160000  right 43160001  150.0 vs 150.00001525878906 (+1 ulp)', out)

    def test_aggregate_output_and_missing_call(self):
        rows = scenario_rows()
        right = list(rows)
        old = d(5.25)
        new = struct.pack('<Q', struct.unpack('<Q', bytes.fromhex(old))[0] - 1).hex()
        # only get_attackstrength's own result differs (its last add is not in the right log): its output, after everything
        right[6] = right[6].replace('b1=' + old, 'b1=' + new)
        del right[5]
        out = run(rows, right, '--detail')
        self.assertIn('unmatched  left seq 6 add in=gas label=gas.out1  right -', out)
        self.assertIn('OPERATION (equal inputs, different output) at left seq 0 gas in=exec', out)
        self.assertIn('b1.hullshield: left 4015000000000000  right 4014ffffffffffff', out)
        # a call only one side made
        out = run(rows, [r for r in rows if 'kind=apply' in r])
        self.assertIn('only left: tick 546 0000000000096587 -> 000000000007336a #0', out)

    def test_add_fields_never_read(self):
        rows = scenario_rows()
        right = list(rows)
        # the first add's accumulator with Linux's unset stack bytes in f0, f1 and the flags: not a difference, counted
        k = next(i for i, r in enumerate(right) if 'kind=add' in r)
        before = re.search(r' b0=([0-9a-f]{128}) ', right[k]).group(1)
        garbage = before[:80] + '80f24548' + '107d0000' + before[96:112] + '2080' + before[116:]
        right[k] = right[k].replace(' b0=' + before + ' ', ' b0=' + garbage + ' ')
        self.assertNotEqual(right[k], rows[k])
        module.unread_differences[0] = 0
        out = run(rows, right, '--target', '7336a')
        self.assertIn('no difference in 1 matched calls', out)
        self.assertIn('add fields never read (f0, f1, flags) differing, not compared: 3', out)
        # the same bytes in a field the add reads (+0x38) are a difference
        module.unread_differences[0] = 0
        right[k] = rows[k].replace(' b0=' + before + ' ', ' b0=' + before[:96] + '01000000' + before[104:] + ' ')
        out = run(rows, right, '--target', '7336a')
        self.assertIn('INPUT at left', out)
        self.assertIn('b0.f2:', out)

    def test_rounding_midpoint(self):
        tie = struct.unpack('>Q', struct.pack('>d', 290.31939697265625))[0] - (1 << 28)
        self.assertIn('ON A MIDPOINT', module.rounding(f'{tie:016x}'))
        self.assertNotIn('MIDPOINT', module.rounding(struct.pack('>d', 290.3193664550781).hex()))


if __name__ == '__main__':
    unittest.main()

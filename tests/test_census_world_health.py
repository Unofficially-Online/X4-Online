import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('census_world_health', ROOT/'tools/dev/census_world_health.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def d(x):
    return struct.pack('>d', x).hex()


def f(x):
    return struct.pack('>f', x).hex()


def shield(value, changed, maximum=4000.0, delay=2.25, rate=12.5, factor=0.75, generators=2, modified=1, mods=(1.0, 1.0, 0.5)):
    return (d(value)+d(changed)+d(maximum)+d(delay)+f(rate)+f(factor)+f'{generators:016x}'+str(modified)
            + ''.join(f(m) for m in mods))


def row(tick, identity='000000000007336a', value=4000.0, changed=0.0, hull=100.0, time=None, present=True, text=None):
    time = 7.0+tick/60.0 if time is None else time
    if not present:
        return f'[X4LAN] NATIVE_GATE WORLD_HEALTH tick={tick} id={identity} present=0 time={d(time)} hull=- shield=-\n'
    return (f'[X4LAN] NATIVE_GATE WORLD_HEALTH tick={tick} id={identity} present=1 time={d(time)} hull={d(hull)} '
            f'shield={text or shield(value, changed)}\n')


def read(*lines):
    with tempfile.TemporaryDirectory() as folder:
        log = Path(folder)/'engine-output.log'
        log.write_text('[X4LAN] NATIVE_GATE WORLD_HASH tick=1 objects=1\n' + ''.join(lines))
        return module.rows(Path(folder))


class WorldHealthTests(unittest.TestCase):
    def test_shield_text_matches_the_gate_format(self):
        text = shield(2.5, 0.0)
        self.assertEqual(len(text), 121)
        fields = module.decode(d(1.0), d(99.0), text)
        self.assertEqual(module.value(*fields['value']), 2.5)
        self.assertEqual(module.value(*fields['rate']), 12.5)
        self.assertEqual(module.value(*fields['generators']), 2)
        self.assertEqual(module.value(*fields['mod2']), 0.5)
        self.assertEqual(module.decode(d(1.0), d(99.0), 'none')['shield'], ('none', 's'))
        with self.assertRaises(ValueError):
            module.decode(d(1.0), d(99.0), text[:-1])

    def test_ulp_distance_across_signs(self):
        one = d(1.0)
        after = struct.pack('>Q', int(one, 16)+3).hex()
        self.assertEqual(module.ulp(one, after, 'd'), 3)
        self.assertEqual(module.ulp(d(-0.0), d(0.0), 'd'), 0)
        self.assertEqual(module.ulp(f(1.0), struct.pack('>I', int(f(1.0), 16)-2).hex(), 'f'), -2)
        self.assertIsNone(module.ulp('-', one, 'd'))

    def test_first_difference_and_changes(self):
        base = 4000.0-37.5
        left = read(row(569, value=4000.0), row(570, value=base, changed=9.5), row(571, value=base, changed=9.5),
                    row(570, identity='0000000000073371', value=4000.0))
        nudged = struct.unpack('>d', struct.pack('>Q', int(d(base), 16)+1))[0]
        right = read(row(569, value=4000.0), row(570, value=nudged, changed=9.5), row(571, value=nudged, changed=9.5),
                     row(570, identity='0000000000073371', value=4000.0))
        text = module.report(left, right)
        self.assertIn('== 000000000007336a ticks left 3 right 3 common 3 (569..571)', text)
        self.assertIn('first difference at tick 570', text)
        self.assertIn(f'value: left {d(base)} ({base!r}) right {d(nudged)} ({nudged!r}) ulp +1', text)
        self.assertIn('left changes: 1', text)
        self.assertIn('tick 570: value 4000.0 -> 3962.5 (d -37.5); changed 0.0 -> 9.5 (d 9.5)', text)
        self.assertIn('== 0000000000073371 ticks left 1 right 1 common 1 (570..570)\n   equal on every common tick', text)
        only = module.report(left, right, {'0000000000073371'})
        self.assertNotIn('7336a', only)

    def test_presence_and_game_time(self):
        left = read(row(10), row(11, present=False))
        right = read(row(10, time=1.5), row(11))
        text = module.report(left, right)
        self.assertIn('first difference at tick 11', text)
        self.assertIn('present: left 0 right 1', text)
        self.assertIn('left changes: 1\n     tick 11: present 1 -> 0\n   right changes: 0', text)
        self.assertNotIn('hull: left', text)
        self.assertEqual(module.differences(left['000000000007336a'][10], right['000000000007336a'][10]), [])

    def test_refusals(self):
        with self.assertRaises(ValueError):
            read(row(5), row(5))
        with self.assertRaises(ValueError):
            read('[X4LAN] NATIVE_GATE WORLD_HEALTH tick=5 id=7336a present=1\n')


if __name__ == '__main__':
    unittest.main()

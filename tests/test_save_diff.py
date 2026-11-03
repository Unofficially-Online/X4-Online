"""tools/dev/save_diff.py: two saves compared element by element, the signature and header date ignored."""
import gzip
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('save_diff', ROOT/'tools/dev/save_diff.py')
save_diff = importlib.util.module_from_spec(spec)
spec.loader.exec_module(save_diff)

BASE = ('<?xml version="1.0"?><savegame><info><save name="{name}" date="{date}"/><game time="{time}"/></info>'
        '<universe><component class="ship" id="[0x10]"><offset x="{x}" y="2"/><wait until="{until}"/></component>'
        '<component class="station" id="[0x20]">{text}</component></universe>{extra}<signature>{sig}</signature></savegame>')


def save(folder, file, gz=True, **values):
    fields = dict(name='a', date='1', time='7.5', x='1', until='9', text='t', extra='', sig='AAAA')
    fields.update(values)
    data = BASE.format(**fields).encode()
    path = Path(folder)/file
    path.write_bytes(gzip.compress(data) if gz else data)
    return path


class SaveDiff(unittest.TestCase):
    def test_equal_but_for_the_header_and_signature(self):
        with tempfile.TemporaryDirectory() as folder:
            left, right = save(folder, 'l.xml.gz'), save(folder, 'r.xml', gz=False, name='b', date='2', sig='BBBB')
            result = save_diff.compare(left, right)
            self.assertTrue(result['equal'], result)
            self.assertEqual(result['elements'], 9)

    def test_values_are_counted_with_their_path(self):
        with tempfile.TemporaryDirectory() as folder:
            result = save_diff.compare(save(folder, 'l.xml.gz'), save(folder, 'r.xml.gz', x='1.0000001', until='9.4', text='u'))
            self.assertFalse(result['equal'])
            self.assertEqual(result['by_attribute'], {'offset.x': 1, 'wait.until': 1, 'component#text': 1})
            paths = {(s['path'], s['attribute']) for s in result['samples']}
            self.assertIn(('savegame/universe/component[[0x10]]/wait', 'until'), paths)
            self.assertIn(('savegame/universe/component[[0x20]]', '#text'), paths)
            self.assertIsNone(result['structure'])

    def test_structure_stops_the_walk(self):
        with tempfile.TemporaryDirectory() as folder:
            result = save_diff.compare(save(folder, 'l.xml.gz'), save(folder, 'r.xml.gz', extra='<economy/>'))
            self.assertFalse(result['equal'])
            self.assertIn('economy', result['structure']['reason'] + result['structure'].get('other', ''))


if __name__ == '__main__':
    unittest.main()

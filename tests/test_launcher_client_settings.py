"""X4LS_CLIENT_CONFIG (test only): one client's own X4 settings in its disposable profile's config.xml. Offline."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'src'))
from lockstep.launcher import profile   # noqa: E402


class ClientSettingsTests(unittest.TestCase):
    def test_only_the_named_client_gets_its_settings(self):
        spec = '2:lodfactor=1.0,effectdistance=0.9,res_width=1600;3:gfxquality=0'
        self.assertEqual(profile.client_settings(spec, 2), [('lodfactor', '1.0'), ('effectdistance', '0.9'), ('res_width', '1600')])
        self.assertEqual(profile.client_settings(spec, 3), [('gfxquality', '0')])
        self.assertEqual(profile.client_settings(spec, 1), [])
        self.assertEqual(profile.client_settings(None, 2), [])
        self.assertEqual(profile.client_settings('', 2), [])

    def test_bad_settings_are_refused(self):
        for spec in ('2:lodfactor=1<0', '2:LOD=1', '2:lodfactor=', '2:a b=1'):
            with self.assertRaises(SystemExit, msg=spec):
                profile.client_settings(spec, 2)

    def test_an_element_is_replaced_where_it_is_or_added(self):
        base = profile.config_xml(1280, 720)
        text = profile.with_settings(base, [('res_width', '1600'), ('lodfactor', '1.0')])
        self.assertIn('<res_width>1600</res_width><res_height>720</res_height>', text)
        self.assertTrue(text.rstrip().endswith('<lodfactor>1.0</lodfactor></root>'))
        self.assertEqual(text.count('<res_width>'), 1)
        self.assertEqual(profile.with_settings(base, []), base)


if __name__ == '__main__':
    unittest.main()

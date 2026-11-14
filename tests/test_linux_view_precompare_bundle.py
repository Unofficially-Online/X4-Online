import hashlib
from pathlib import Path
import tempfile
import unittest

import linux_view_precompare_bundle as pre


class PrecompareEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.stderr = Path(self.tmp.name) / 'stderr'
        self.close = 'X4 COMPONENT_TRACE v=1 stage=close tick=10 tid=7 sampling=bounded foreign=lower_bound\n'
        self.sample = ('X4 PRECOMPARE v=1 stage=sample index=0 tick=8 ordinal=0 phase=1 '
                       'object=30000 class=3ccad80 runtime=7 parent=20000 ancestor=20000 '
                       'result=10000 valid=1 slot=2 failure=0\n')
        self.site = 'X4 PRECOMPARE v=1 stage=site calls=1 sampled=1 missed=3 capped=0 foreign_lower=0 rows=1\n'

    def evidence(self, contents, active=True):
        self.stderr.write_text(contents)
        sha = hashlib.sha256(self.stderr.read_bytes()).hexdigest()
        ancestry = {'pass': True, 'stderr_sha256': sha, 'bind_tick': 8, 'tid': 7,
                    'bind': {'p1': 0x10000, 'p2': 0x20000}}
        component = {'pass': True, 'stderr_sha256': sha, 'bind_tick': 8, 'tid': 7}
        physics = {'pass': True, 'stderr_sha256': sha}
        return pre.collect(self.stderr, ancestry, component, physics, 10, active), ancestry, component, physics

    def test_positive_and_foreign_lower_bound(self):
        site = self.site.replace('foreign_lower=0', 'foreign_lower=4')
        result, *_ = self.evidence(self.close + self.sample + site)
        self.assertTrue(result['pass'], result['errors'])
        self.assertEqual((result['samples'][0]['slot'], result['samples'][0]['result']), (2, 0x10000))
        self.assertEqual(result['foreign_lower'], 4)
        self.assertFalse(result['ownership_exhaustive'])

    def test_invalid_row_is_retained(self):
        sample = self.sample.replace('class=3ccad80', 'class=0').replace('valid=1 slot=2 failure=0',
                                                                         'valid=0 slot=0 failure=2')
        result, *_ = self.evidence(self.close + sample + self.site)
        self.assertTrue(result['pass'], result['errors'])
        self.assertEqual((result['valid_samples'], result['failed_samples']), (0, 1))
        self.assertEqual(result['samples'][0]['failure'], 2)

    def test_zero_calls_and_no_samples_are_distinct(self):
        for calls, expected in ((0, 'zero_calls'), (7, 'no_samples')):
            line = f'X4 PRECOMPARE v=1 stage=site calls={calls} sampled=0 missed=3 capped=0 foreign_lower=0 rows=0\n'
            result, *_ = self.evidence(self.close + line)
            self.assertTrue(result['pass'], result['errors'])
            self.assertEqual(result['observation'], expected)

    def test_changed_raw_and_upstream_hashes_rejected(self):
        good = self.close + self.sample + self.site
        _, ancestry, component, physics = self.evidence(good)
        self.stderr.write_text(good + 'changed raw\n')
        self.assertFalse(pre.collect(self.stderr, ancestry, component, physics, 10, True)['pass'])
        self.stderr.write_text(good)
        for proof in (ancestry, component, physics):
            proof['stderr_sha256'] = '0' * 64
            self.assertFalse(pre.collect(self.stderr, ancestry, component, physics, 10, True)['pass'])
            proof['stderr_sha256'] = hashlib.sha256(self.stderr.read_bytes()).hexdigest()

    def test_grammar_bounds_closure_and_disabled(self):
        good = self.close + self.sample + self.site
        cases = [good + self.site, good.replace('ordinal=0', 'ordinal=3'),
                 good.replace('slot=2', 'slot=1'), good.replace('result=10000', 'result=10000000000000000'),
                 self.sample + self.close + self.site, self.close + self.sample]
        for case in cases:
            self.assertFalse(self.evidence(case)[0]['pass'], case)
        self.assertFalse(self.evidence(good, False)[0]['pass'])
        self.assertTrue(self.evidence('ordinary stderr\n', False)[0]['pass'])

    def test_dependency_and_environment(self):
        self.assertTrue(pre.enabled({'view_precompare_trace': True}, True, True, True))
        for deps in ((False, True, True), (True, False, True), (True, True, False)):
            with self.assertRaises(ValueError):
                pre.enabled({'view_precompare_trace': True}, *deps)
        with self.assertRaises(ValueError):
            pre.enabled({'view_precompare_trace': 1}, True, True, True)
        self.assertNotIn(pre.ENVIRONMENT, pre.environment({pre.ENVIRONMENT: '1'}, False))
        self.assertEqual(pre.environment({}, True)[pre.ENVIRONMENT], '1')


if __name__ == '__main__':
    unittest.main()

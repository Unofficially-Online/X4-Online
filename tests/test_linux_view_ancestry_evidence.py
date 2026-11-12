import unittest
from tools.dev.linux_view_ancestry_evidence import parse_view_ancestry_evidence

EXPECTED = {'p1': 0x10000, 'p2': 0x20000, 'p1_runtime': 11, 'p2_runtime': 12,
            'p1_saved': 13, 'p2_saved': 14}


def receipts():
    return [
        'X4 VIEW_ANCESTRY v=1 stage=bind tick=5 slot=1 tid=42 p1=10000 p2=20000 p1_runtime=11 p2_runtime=12 p1_saved=13 p2_saved=14 ref_before=4 ref_after=5\n',
        'X4 VIEW_ANCESTRY v=1 stage=sample site=weapon index=0 tick=11 tid=42 self=30000 arg=0 category=2 complete=0 count=2 nodes=30000:3c6a440:90,20000:3d21748:12\n',
        'X4 VIEW_ANCESTRY v=1 stage=site site=weapon started=10 returned=10 examined=1 throttled=9 overflow=0 foreign_lower=1 rows=1\n',
        'X4 VIEW_ANCESTRY v=1 stage=site site=engine started=0 returned=0 examined=0 throttled=0 overflow=0 foreign_lower=0 rows=0\n',
        'X4 VIEW_ANCESTRY v=1 stage=site site=ship started=0 returned=0 examined=0 throttled=0 overflow=0 foreign_lower=0 rows=0\n',
        'X4 VIEW_ANCESTRY v=1 stage=close tick=20 slot=1 ref_before=5 ref_after=4 sampling_closed=1 unpatched=0 foreign=lower_bound\n',
    ]


class ViewAncestryEvidenceTests(unittest.TestCase):
    def parse(self, lines):
        return parse_view_ancestry_evidence(lines, 1, True, 20, EXPECTED)

    def test_bounded_observation_does_not_claim_coverage(self):
        result = self.parse(receipts())
        self.assertTrue(result['pass'], result['errors'])
        self.assertTrue(result['p2_observed']['weapon'])
        self.assertFalse(result['p2_observed']['ship'])
        self.assertFalse(result['coverage_complete'])
        self.assertFalse(result['safe_resume_proven'])
        self.assertTrue(result['p2_mapping_eligible'])
        without_p1 = dict(EXPECTED)
        del without_p1['p1']
        limited = parse_view_ancestry_evidence(receipts(), 1, True, 20, without_p1)
        self.assertTrue(limited['pass'], limited['errors'])
        self.assertFalse(limited['p1_pointer_independently_bound'])
        missing = receipts()
        del missing[1]
        missing[1] = 'X4 VIEW_ANCESTRY v=1 stage=site site=weapon started=0 returned=0 examined=0 throttled=0 overflow=0 foreign_lower=0 rows=0\n'
        missing_result = self.parse(missing)
        self.assertTrue(missing_result['pass'], missing_result['errors'])
        self.assertFalse(missing_result['p2_mapping_eligible'])

    def test_rejects_wrong_identity_accounting_and_category(self):
        for old, new in [('p2_saved=14', 'p2_saved=15'), ('throttled=9', 'throttled=8'),
                         ('category=2', 'category=1'), ('tid=42 self', 'tid=43 self'),
                         ('ref_after=4', 'ref_after=3')]:
            with self.subTest(old=old):
                self.assertFalse(self.parse([row.replace(old, new) for row in receipts()])['pass'])

    def test_rejects_missing_duplicate_reordered_and_truncated(self):
        lines = receipts()
        for changed in (lines[:-1], lines + [lines[-1]], [lines[0], lines[2], lines[1], *lines[3:]],
                        [*lines[:-1], lines[-1].rstrip()]):
            self.assertFalse(self.parse(changed)['pass'])

    def test_disabled_rejects_rows(self):
        self.assertTrue(parse_view_ancestry_evidence([], 1, False, 20, {})['pass'])
        self.assertFalse(parse_view_ancestry_evidence(receipts(), 1, False, 20, {})['pass'])


if __name__ == '__main__':
    unittest.main()

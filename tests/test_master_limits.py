"""The master's token buckets (src/lockstep/master/limits.py), on a test clock."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
from lockstep.master import limits  # noqa: E402


class Clock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


class BucketTests(unittest.TestCase):
    def test_a_burst_passes_then_the_rate_holds(self):
        clock = Clock()
        limiter = limits.RateLimiter(1, 3, 100, 100, clock=clock)
        self.assertEqual([limiter.allow('192.0.2.1') for _ in range(4)], [True, True, True, False])
        clock.now += 1.0
        self.assertTrue(limiter.allow('192.0.2.1'))
        self.assertFalse(limiter.allow('192.0.2.1'))

    def test_every_ip_has_its_own_bucket_and_all_share_the_global_one(self):
        clock = Clock()
        limiter = limits.RateLimiter(1, 2, 1, 3, clock=clock)
        self.assertTrue(limiter.allow('192.0.2.1'))
        self.assertTrue(limiter.allow('192.0.2.1'))
        self.assertFalse(limiter.allow('192.0.2.1'))   # its own bucket is empty
        self.assertTrue(limiter.allow('198.51.100.1'))
        self.assertFalse(limiter.allow('198.51.100.1'))   # the global bucket is empty now
        self.assertFalse(limiter.allow('203.0.113.1'))

    def test_six_probes_a_minute(self):
        clock = Clock()
        probes = limits.per_minute(6, clock=clock)
        self.assertEqual(sum(probes.allow('192.0.2.1') for _ in range(10)), 6)
        clock.now += 10
        self.assertTrue(probes.allow('192.0.2.1'))     # one token per 10 s
        self.assertFalse(probes.allow('192.0.2.1'))

    def test_the_table_of_buckets_stays_bounded(self):
        clock = Clock()
        limiter = limits.RateLimiter(1, 2, 10 ** 9, 10 ** 9, clock=clock, keys_max=100)
        for number in range(1000):
            limiter.allow(f'10.0.{number // 256}.{number % 256}')
        self.assertLessEqual(len(limiter.buckets), 100)

    def test_the_contract_defaults(self):
        defaults = limits.MasterLimits()
        self.assertEqual((defaults.games_max, defaults.relays_max, defaults.games_per_ip), (200, 32, 4))
        self.assertEqual(defaults.data_bytes_max, 8 << 30)
        self.assertEqual(defaults.json_bytes_max, 64 << 10)
        self.assertEqual((defaults.heartbeat_s, defaults.lobby_wait_s, defaults.probes_per_minute), (90, 25, 6))


if __name__ == '__main__':
    unittest.main()

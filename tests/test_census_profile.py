"""census_profile.py on synthetic SAMPLE_PROFILE and FRAME_TIMING lines."""
import io
import json
from contextlib import redirect_stdout
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/dev'))
import census_profile as c


def frame(tick, sim):
    return (f'[X4LAN] NATIVE_GATE FRAME_TIMING tick={tick} frames={tick} wait_us=100 frame_us=4000 outside_us=26000 sum_wait_us=1 '
            f'sum_frame_us=1 sum_outside_us=1 pre_us=1 created_us=0 join_us=0 sum_pre_us=1 reads=0 pace_us=0 sum_pace_us=0 sim_us={sim} probe_us=900')


def stop(samples=1000, step=800):
    return (f'[X4LAN] NATIVE_GATE SAMPLE_PROFILE_STOP armed=1 period_ms=2 modules=90 samples={samples} step={step} walked={samples} '
            f'leaf=700/50/240/10 truncated=3 unwalked=0 faults=0 failed=0 dropped=0 frames=30000 pause_us=25000 pause_max_us=140 stage=stopped')


def rows(physics=400, physics_step=400):
    return [f'[X4LAN] NATIVE_GATE SAMPLE_PROFILE_ROW index=0 kind=mark label=moveworker_run rva=ef4400 count=1000 step=800',
            f'[X4LAN] NATIVE_GATE SAMPLE_PROFILE_ROW index=3 kind=mark label=physics_step rva=ba600 count={physics} step={physics_step}',
            '[X4LAN] NATIVE_GATE SAMPLE_PROFILE_ROW index=21 kind=inner rva=13d4000 site=0 count=300 step=300',
            '[X4LAN] NATIVE_GATE SAMPLE_PROFILE_ROW index=22 kind=inner rva=ef4620 site=1 count=200 step=0']


def write_run(root, name, lines_by_client):
    run = Path(root)/name
    for client, lines in lines_by_client.items():
        (run/client).mkdir(parents=True)
        (run/client/'engine-output.log').write_text('\n'.join(lines)+'\n')
    return run


class CensusProfileTests(unittest.TestCase):
    def test_parse_keeps_the_last_stop_and_the_sim_median(self):
        lines = [frame(1, 99999), frame(3, 20000), frame(4, 24000), frame(5, 22000), stop(10, 5), rows(1, 1)[1], stop(), *rows()]
        p = c.parse(lines)
        self.assertEqual(p['status']['samples'], '1000');self.assertEqual(len(p['rows']), 4)
        self.assertEqual(p['sim_us'], 22000);self.assertEqual(p['timing_ticks'], 3) # tick 1 skipped
        s = c.summary(p)
        self.assertEqual(s['leaf'], dict(x4=700, gate=50, other=240, unknown=10))
        self.assertEqual(s['pause_mean_us'], 25.0);self.assertEqual(s['sim_ms'], 22.0)
        m = s['marks']['physics_step']
        self.assertEqual(m['share'], 0.4);self.assertEqual(m['step_share'], 0.5);self.assertEqual(m['ms'], 11.0)
        self.assertAlmostEqual(m['err'], 0.0155, places=4)
        self.assertEqual(s['inner'][1], dict(rva='ef4620', site=True, count=200, step=0))

    def test_no_stop_no_summary_and_no_step_no_estimate(self):
        self.assertIsNone(c.summary(c.parse([frame(3, 1000)])))
        s = c.summary(c.parse([stop(100, 0), *rows(10, 0)]))
        self.assertIsNone(s['marks']['physics_step']['step_share']);self.assertIsNone(s['marks']['physics_step']['ms'])

    def test_run_directory_compare_and_names(self):
        with tempfile.TemporaryDirectory() as root:
            a = write_run(root, 'two-sectors', {'client1': [frame(3, 30000), stop(), *rows(400, 400)],
                                                'client2': [frame(3, 30000), stop(), *rows(400, 400)]})
            b = write_run(root, 'masked', {'client1': [frame(3, 24000), stop(), *rows(200, 200)]})
            result = c.compare(c.analyse(a), c.analyse(b))
            self.assertEqual(list(result), ['client1'])
            self.assertEqual(result['client1']['sim_delta'], 6.0)
            self.assertEqual(result['client1']['marks']['physics_step'], dict(a=15.0, b=6.0, delta=9.0))
            names = Path(root)/'names.csv'
            names.write_text('rva,va,name\n0xef4400,0x140ef4400,MoveWorker::Run\n0x13d3460,0x1413d3460,JPH::Update\n')
            table = c.names_table(names)
            self.assertEqual(c.name_of(table, 0xef4400), 'MoveWorker::Run')
            self.assertEqual(c.name_of(table, 0xef4620), 'MoveWorker::Run+0x220')
            self.assertEqual(c.name_of(table, 0x100), '');self.assertEqual(c.name_of(None, 0x100), '')
            out = io.StringIO()
            with redirect_stdout(out):
                sys.argv = ['census_profile.py', str(a), '--names', str(names)];c.main()
            self.assertIn('physics_step', out.getvalue());self.assertIn('MoveWorker::Run+0x220', out.getvalue())
            out = io.StringIO()
            with redirect_stdout(out):
                sys.argv = ['census_profile.py', str(a), '--against', str(b), '--json'];c.main()
            self.assertEqual(json.loads(out.getvalue())['client1']['marks']['physics_step']['delta'], 9.0)
            log = a/'client2'/'engine-output.log'
            self.assertEqual(list(c.logs_of(log)), ['engine-output.log'])


if __name__ == '__main__':
    unittest.main()

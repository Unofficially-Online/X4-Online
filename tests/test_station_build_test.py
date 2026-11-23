"""Station construction test (tools/dev/station_build_test.py): its plan, its readers and its report.

Synthetic: the report reads made-up log rows. What the real game does when two players click a station together is
shown by a live run of `station_build_test.py run`, not here."""
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/dev'))
if sys.platform != 'win32':   # its input module (ui_window_input) calls Win32 when it is imported
    raise unittest.SkipTest('Win32 window input: Windows only')
import station_build_test as sbt  # noqa: E402

ROW = 'ABC-123/built=%d/standing=1/planned=1/tasks=0/0/builders=0/manager=false/budget=0/stored=claytronics:%d,energycells:3000,hullparts:3000'
OTHER = 'DEF-456/built=%d/standing=1/planned=1/tasks=0/0/builders=0/manager=false/budget=0/stored=-'


def look(tick, built=0, clay=3000, other=0):
    return '[=ERROR=] 1.00 [X4LAN] NATIVE_GATE UI_REPLAY_SCRIPT tick=%d op=station_look ok=true result=stations=2 %s %s money=49000000\n' % (
        tick, ROW % (built, clay), OTHER % other)


def applied(tick, slot, what):
    return '[=ERROR=] 1.00 [X4LAN] NATIVE_GATE UI_BUILD_APPLIED tick=%d slot=%d what=%s\n' % (tick, slot, what)


def parts(tick, world='aa'):
    return '[X4LAN] NATIVE_GATE DIGEST_PARTS tick=%d hash=1 world=%s rid=5 pilot=6 input=7\n' % (tick, world)


STEPS = [{'player': k, 'step': step, 'ok': True, 'tries': 1, 'rows': {}, 'menu': None} for k in (1, 2) for step in ('map', 'place', 'confirm', 'select')]


class ReportTests(unittest.TestCase):
    def run_report(self, first, second, steps=STEPS):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            (folder / 'relay.log').write_text('NATIVE_GATE tick=900 failure=none done=False\n')
            (folder / 'errors.log').write_text('')
            logs = [folder / 'c1.log', folder / 'c2.log']
            logs[0].write_text(first)
            logs[1].write_text(second)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                failed = sbt.report(logs, folder, steps)
        return failed, out.getvalue()

    def good(self):
        return (applied(100, 1, 'ReserveBuildPlot=77') + applied(200, 2, 'ReserveBuildPlot=78') + look(300) + look(600, 1, 2946, 1) + look(900, 1, 2946, 1) +
                ''.join(parts(t) for t in (300, 600, 900)))

    def test_two_clients_that_agree_pass(self):
        failed, text = self.run_report(self.good(), self.good())
        self.assertEqual(failed, 0, text)
        self.assertIn('station ABC-123: its module stands on both clients from the reading of tick 600', text)
        self.assertIn('the 3 station readings both clients took to tick 900 are the same', text)
        self.assertIn('the 2 build calls ran on both clients', text)

    def test_a_station_built_one_reading_earlier_on_one_client_fails_by_name(self):
        # what run sb2 showed: one client took the building material a portion ahead and stood a reading earlier
        late = applied(100, 1, 'ReserveBuildPlot=77') + applied(200, 2, 'ReserveBuildPlot=78') + look(300) + look(600, 0, 2970, 1) + look(900, 1, 2946, 1) + \
            ''.join(parts(t) for t in (300, 600, 900))
        failed, text = self.run_report(self.good(), late)
        self.assertGreaterEqual(failed, 2, text)
        self.assertIn('built at the reading of tick 600 on client 1 and of tick 900 on client 2', text)
        self.assertIn('1 readings differ, the first at tick 600', text)

    def test_a_click_that_did_not_arrive_and_a_call_on_one_client_only_fail(self):
        steps = [dict(s) for s in STEPS[:6]] + [{'player': 2, 'step': 'confirm', 'ok': False, 'tries': 1, 'rows': {'1': None, '2': None}, 'menu': 'station shown=true changes=true plan=1'}]
        failed, text = self.run_report(self.good(), self.good().replace(applied(200, 2, 'ReserveBuildPlot=78'), applied(200, 2, 'ReserveBuildPlot=0')), steps)
        self.assertGreaterEqual(failed, 2, text)
        self.assertIn('player 2: stopped at "confirm"', text)
        self.assertIn('build calls differ', text)

    def test_a_world_that_parts_fails(self):
        other = self.good().replace(parts(900), parts(900, 'bb'))
        failed, text = self.run_report(self.good(), other)
        self.assertEqual(failed, 1, text)
        self.assertIn('world differs at 1 ticks from tick 900', text)


class PlanTests(unittest.TestCase):
    def test_both_clients_get_the_same_fixtures_and_readings(self):
        plan = sbt.plan(5)
        self.assertEqual(plan[0], plan[1])
        ops = [row[1] for row in plan[0]]
        for op in ('station_kit', 'wallet', 'station_wares', 'builder_warp', 'station_look'):
            self.assertIn(op, ops)
        self.assertTrue(all(row[0] < 5 * 1800 for row in plan[0]))

    def test_a_faction_per_player_gets_each_its_own_fixtures(self):
        one, two = sbt.plan(5, 'factions')
        # the replayed fixtures (a vessel for each faction, the material, the warp) are client 1's alone: in both plans
        # they would run twice; the canonical ones (money, player 2's blueprints) and the readings are in both
        self.assertEqual([row for row in one if row[1] == 'station_kit'], [(30, 'station_kit', 1, True), (32, 'station_kit', 2, True)])
        self.assertFalse([row for row in two if row[1] in ('station_kit', 'station_wares', 'builder_warp')])
        for rows in (one, two):
            self.assertIn((40, 'wallet', 50000000), rows)
            self.assertIn((60, 'station_blueprints', 2), rows)
        self.assertEqual([row for row in one if row[1] == 'station_look'], [row for row in two if row[1] == 'station_look'])
        self.assertEqual(one, sorted(one, key=lambda row: row[0]))

    def test_a_reading_of_a_faction_per_player_names_the_owner_and_each_account(self):
        found = sbt.stations('stations=2 %s %s/owner=x4lan_player2 money=1 money2=2' % (ROW % (1, 2946), ROW.replace('ABC-123', 'XYZ-999') % (0, 3000)))
        self.assertEqual(sorted(found), ['ABC-123', 'XYZ-999'])
        self.assertEqual(found['XYZ-999']['built'], 0)

    def test_a_reading_is_read_per_station(self):
        found = sbt.stations('stations=2 %s %s money=1' % (ROW % (1, 2946), OTHER % 0))
        self.assertEqual(found['ABC-123']['built'], 1)
        self.assertEqual(found['ABC-123']['stored'], 'claytronics:2946,energycells:3000,hullparts:3000')
        self.assertEqual(found['DEF-456']['stored'], '-')

    def test_the_click_places_lie_inside_the_window(self):
        for place in (sbt.PLOTS_ICON, sbt.PROPERTY_ICON, sbt.DOCKS_ICON, sbt.FIRST_MODULE, sbt.PUT, sbt.CONFIRM, sbt.ASSIGN, sbt.PLACE[1], sbt.PLACE[2]):
            self.assertTrue(0 < place[0] < 1 and 0 < place[1] < 1)


if __name__ == '__main__':
    unittest.main()

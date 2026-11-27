"""The showcase video's tools (tools/dev/showcase): the takes' plans as the overlay gets them, the cut's filter graphs
and its tick arithmetic. Offline: no X4, no ffmpeg, no picture."""
import re
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHOWCASE = ROOT / 'tools/dev/showcase'
sys.path.insert(0, str(SHOWCASE))
import cut      # noqa: E402
import plans    # noqa: E402
import take     # noqa: E402

CORE = (ROOT / 'tools/dev/native_gate_ui_replay_core.lua').read_text(encoding='utf-8')
OPS = set(re.findall(r"op ?== ?'([a-z0-9_]+)'", CORE))
FIXTURES = {'slot_near', 'hostile'}      # canonical by their UI event: the same row in every client's plan


class Plans(unittest.TestCase):
    def test_lua_source(self):
        self.assertEqual(take.lua([[150, 'slot_near', 2, 1, 80], [5, 'hostile', 1, 'A b', None, 0.5, True]]),
                         "{{150,'slot_near',2,1,80},{5,'hostile',1,'A b',nil,0.5,true}}")
        with self.assertRaises(AssertionError):
            take.lua("it's")

    def test_every_take(self):
        for name, make in plans.TAKES.items():
            made = make()
            plan = made['plan']
            self.assertEqual(len(plan), 2, name)
            window = made['minutes'] * 60 * 30
            for rows in plan:
                self.assertEqual([row[0] for row in rows], sorted(row[0] for row in rows), name)
                for row in rows:
                    self.assertIn(row[1], OPS, f'{name}: {row}')
                    self.assertTrue(0 < row[0] < window, f'{name}: {row} outside the pilot window')
                    if row[1] in ('input', 'input_local'):
                        self.assertIn(row[2], ('action', 'state', 'range'), row)
                        self.assertTrue(0 < row[3] < 512, row)
            # a fixture runs on every client at the same step, or the worlds part
            shared = [[row for row in rows if row[1] in FIXTURES] for rows in plan]
            self.assertEqual(shared[0], shared[1], name)
            # a client's own view keys never go on the record, a flight key never stays local
            for rows in plan:
                for row in rows:
                    if row[1] == 'input_local':
                        self.assertIn(row[3], (plans.A_MAP, plans.A_COCKPIT_VIEW, plans.A_EXTERNAL_VIEW, plans.A_TARGET_VIEW,
                                               plans.A_TOGGLE_HUD), row)
                    if row[1] == 'input':
                        self.assertIn(row[3], (plans.A_STOP, plans.S_ACCELERATE, plans.S_DECELERATE, plans.S_BOOST,
                                               plans.R_YAW, plans.R_PITCH, plans.R_ROLL, plans.R_THROTTLE), row)

    def test_walk_is_the_station_walk_tests_meet(self):
        sys.path.insert(0, str(ROOT / 'tools/dev'))
        import contextlib
        import io
        from unittest import mock
        import station_walk_test
        out = io.StringIO()
        with contextlib.redirect_stdout(out), mock.patch.object(sys, 'argv', ['station_walk_test.py', 'plan', '--scenario', 'meet']):
            station_walk_test.main()
        printed = out.getvalue().strip()
        # theirs ends with two more read-only foot_where rows per player
        for extra in (",{1500,'foot_where'},{1810,'foot_where'}", ",{1000,'foot_where'},{1270,'foot_where'}"):
            self.assertIn(extra, printed)
            printed = printed.replace(extra, '')
        ours = [[row for row in rows if row[1] != 'input_local'] for rows in plans.walk()['plan']]      # without the hint keys
        self.assertEqual(printed, take.lua(ours))

    def test_hud_toggles_pair_up(self):
        """Every take ends with the HUD as it began (an even count of toggles per player)."""
        for name, make in plans.TAKES.items():
            for rows in make()['plan']:
                toggles = [row for row in rows if row[1] == 'input_local' and row[3] == plans.A_TOGGLE_HUD]
                self.assertEqual(len(toggles) % 2, 0, name)

    def test_battle_rows(self):
        rows = plans.battle(300, plans.BLUE + plans.RED)
        self.assertEqual(sum(row[7] for row in rows if row[4] == 'terran'), sum(row[7] for row in rows if row[4] == 'xenon'))
        self.assertEqual(len({row[3] for row in rows}), len(rows))          # names are what target_named finds
        for row in rows:
            self.assertRegex(row[5], r'^(ter|atf|xen)_(s|m|l|xl)_[a-z]+_0\d_a$')
            self.assertIn(row[2], (1, 2))
            self.assertEqual(row[10], 60)
        melee = plans.battle(850, plans.MELEE)
        self.assertEqual(sum(row[7] for row in melee if row[4] == 'terran'), sum(row[7] for row in melee if row[4] == 'xenon'))

    def test_missions_plan_is_the_release_checks(self):
        sys.path.insert(0, str(ROOT / 'tools/dev'))
        import release_check
        made = plans.missions()
        end = [row for row in made['plan'][0] if row[1] == 'every'][0][3]
        self.assertEqual([row for row in made['plan'][0] if row[1] != 'input_local'], release_check.satellite_plan(end))
        self.assertEqual(made['plan'][0], made['plan'][1])


class Cut(unittest.TestCase):
    TABLE = [(10.0, 100), (11.0, 120), (12.0, 140), (20.0, 300)]

    def test_tick_time(self):
        self.assertEqual(cut.tick_time(self.TABLE, 120), 11.0)
        self.assertAlmostEqual(cut.tick_time(self.TABLE, 130), 11.5)
        self.assertAlmostEqual(cut.tick_time(self.TABLE, 220), 16.0)
        with self.assertRaises(SystemExit):
            cut.tick_time(self.TABLE, 99)

    def test_placed_by_tick_at_game_speed(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / 't.ticks.txt').write_text(''.join(f'{t} {k}\n' for t, k in self.TABLE), encoding='utf-8')
            edit = {'ticks': {'a': 't.ticks.txt'}}
            shot = cut.placed({'kind': 'full', 'take': 'a', 'tick': 140, 'seconds': 2, 'speed': 'game'}, edit, Path(d))
            self.assertEqual(shot['at'], 12.0)
            # 60 ticks (2 s of the game) took 3 s of the take: played 1.5 times as fast
            self.assertAlmostEqual(shot['speed'], 1.5)
            pair = cut.placed({'kind': 'pair', 'take': 'a', 'tick': [100, 120], 'seconds': 1}, edit, Path(d))
            self.assertEqual(pair['at'], [10.0, 11.0])
            card = {'kind': 'card', 'image': 'x.png', 'seconds': 2}
            self.assertIs(cut.placed(card, edit, Path(d)), card)

    def test_filters(self):
        for kind in ('full', 'split', 'duo', 'fit', 'quad', 'blend', 'wipe', 'pair', 'card'):
            graph = cut.shot_filter({'kind': kind, 'seconds': 2, 'lag': -3, 'fade': [0.2, 0.3], 'speed': 1.5}, 1920, 1080, True)
            self.assertTrue(graph.endswith('[v]'), kind)
            self.assertIn('overlay=0:0', graph)
            self.assertIn('fade=t=out:st=1.700', graph)
            self.assertEqual('setpts=PTS/1.50000' in graph, kind != 'card', kind)
            if kind in ('split', 'duo', 'fit', 'quad', 'blend', 'wipe'):
                self.assertIn('crop=1920:1080:1920:0', graph, kind)      # player 2's window
                self.assertIn('trim=start_frame=3', graph, kind)         # player 1's picture three frames later
        with self.assertRaises(SystemExit):
            cut.shot_filter({'kind': 'nope', 'seconds': 1}, 1920, 1080, False)


if __name__ == '__main__':
    unittest.main()

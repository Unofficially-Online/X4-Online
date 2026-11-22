"""Action parity (tools/dev/action_parity.py) and action_tests.py --swap / dockpair.

Synthetic: made-up client logs and plans. Whether player 2's actions are player 1's in the real game is shown by live
runs of `action_tests.py run` and `action_parity.py` on their run folders, not here."""
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/dev'))
import action_parity as ap  # noqa: E402
import action_tests  # noqa: E402

LOOK = 'state=ok ship=%s owner=player pilot=%s docked=false at=%s orders=0 default=%s people=%s hull=50 name=X'


def chain_row(tick, text):
    return '[=ERROR=] 9.0 [X4LAN] NATIVE_GATE UI_REPLAY_SCRIPT tick=%d op=chain ok=true result=%s scope=0' % (tick, text)


def client_log(path, ship, pilot, default, dock_at, slot_lines=(), game=()):
    rows = ['[=ERROR=] 1.0 [X4LAN] NATIVE_GATE DIGEST_PARTS tick=1 world=1']
    rows.append(chain_row(61, 'step=look n=1 at=1 ok=true ' + LOOK % (ship, pilot, '0/0/0', default, '0/2')))
    rows.append(chain_row(62, 'step=dock n=2 at=2 ok=true autopilot'))
    rows.append(chain_row(60 + dock_at, 'step=dock n=2 at=%d ok=true docked after=%d asked=1' % (dock_at, dock_at)))
    rows.append(chain_row(61 + dock_at, 'step=price n=3 at=%d ok=true repair price=11000 | chain_end ok=true steps=3/3' % (dock_at + 1)))
    rows += ['[=ERROR=] 9.0 [X4LAN] NATIVE_GATE ' + x for x in slot_lines]
    rows += ['[General] 12.0 ' + x for x in game]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(rows) + '\n', encoding='utf-8')


class Answers(unittest.TestCase):
    def test_none_against_some_object_is_a_flag(self):
        flags, notes = ap.compare_answers(LOOK % ('444197', '444202', '1/2/3', '-', '0/2'), LOOK % ('142286', '0', '9/9/9', '-', '0/2'))
        self.assertEqual(len(flags), 1)
        self.assertIn('pilot=444202 / 0', flags[0])

    def test_other_ids_and_places_are_not(self):
        flags, notes = ap.compare_answers(LOOK % ('444197', '444202', '1/2/3', '-', '0/2'), LOOK % ('142286', '7', '9/9/9', '-', '0/2'))
        self.assertEqual(flags, [])

    def test_words_and_shapes(self):
        flags, _ = ap.compare_answers(LOOK % ('1', '2', '0/0/0', '-', '0/2'), LOOK % ('1', '2', '0/0/0', 'Recon', '1/2,service:99'))
        self.assertEqual(sorted(f.split('=')[0] for f in flags), ['default', 'people'])
        flags, _ = ap.compare_answers('granted', 'tradecomputerbusy')
        self.assertTrue(flags)

    def test_numbers_are_notes(self):
        flags, notes = ap.compare_answers('repair price=11085', 'repair price=11856')
        self.assertEqual(flags, [])
        self.assertEqual(notes, ['price=11085 / 11856'])

    def test_a_key_on_one_side(self):
        flags, _ = ap.compare_answers('docked after=3 by=ai', 'docked after=3')
        self.assertEqual(flags, ['by only for player 1 (ai)'])


class Compare(unittest.TestCase):
    def run_compare(self, slot_lines=(), game2=(), dock2=300, pilot2='0'):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            client_log(run/'client1/engine-output.log', '444197', '444202', '-', 300, slot_lines)
            client_log(run/'client2/engine-output.log', '142286', pilot2, '-', dock2, slot_lines, game2)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                flags = ap.compare(run)
            return flags, out.getvalue()

    def test_alike_players_give_no_flag(self):
        flags, text = self.run_compare(pilot2='7')
        self.assertEqual(flags, 0, text)

    def test_the_findings(self):
        slot = ['UI_REPLAY_APPLIED tick=10 slot=1 op=71 result=applied what=SignalObject(68658,x4lan_ai_dock)',
                'UI_REPLAY_APPLIED tick=20 slot=2 op=71 result=applied what=SignalObject(68658,x4lan_ai_dock) generated']
        flags, text = self.run_compare(slot, game2=['CXAnarkController::ChangeUI() - duplicated call to ChangeUI.'], dock2=900)
        self.assertIn('FLAG answer   look: pilot=444202 / 0 (none against some)', text)
        self.assertIn('FLAG time     dock: player 2 took 899 ticks, player 1 299', text)
        self.assertIn('x4lan_ai_dock) generated: slot 1 x0, slot 2 x1 (from tick 20)', text)
        self.assertIn('x4lan_ai_dock): slot 1 x1 (from tick 10), slot 2 x0', text)
        self.assertIn('client 1 x0, client 2 x1: CXAnarkController::ChangeUI()', text)
        self.assertEqual(flags, 5, text)

    def test_lines_before_the_session_are_not_compared(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'log'
            path.write_text('[General] 1.0 Game initialisation time 5 seconds\n[=ERROR=] 2.0 [X4LAN] NATIVE_GATE DIGEST_PARTS tick=1\n'
                            '[General] 3.0 ======================================\n[General] 4.0 Error in AI script 12\n', encoding='utf-8')
            self.assertEqual(ap.lines_of(path, False), {'Error in AI script N': 1})


class Swap(unittest.TestCase):
    def args(self, scenario, *more):
        p = SimpleNamespace(swap=True, minutes=None, ai_dock=False)
        parser = __import__('argparse').ArgumentParser()
        scenario.options(parser)
        for k, v in vars(parser.parse_args(list(more))).items():
            setattr(p, k, v)
        return p

    def test_tables_and_starts_change_places_and_back(self):
        a = self.args(action_tests.CrewMove)
        before = dict(action_tests.CrewMove.FROM), dict(action_tests.CrewMove.ABOARD)
        try:
            self.assertEqual(action_tests.swap_targets(action_tests.CrewMove, a), ['FROM', 'TO', 'ABOARD'])
            self.assertEqual(action_tests.CrewMove.FROM[1], before[0][2])
            plan = action_tests.full_plan(action_tests.CrewMove, a)
            chains = [[e for e in client if e[1] == 'chain'][0] for client in plan]
            self.assertEqual((chains[0][0], chains[1][0]), (90, 60))
            self.assertIn(('arrived', 'try', 'aboard=3', 30, 6000, 'ship_look', action_tests.CrewMove.TO[1]), chains[0][3])
        finally:
            action_tests.swap_targets(action_tests.CrewMove, a)
        self.assertEqual((dict(action_tests.CrewMove.FROM), dict(action_tests.CrewMove.ABOARD)), before)

    def test_crew_stations(self):
        a = self.args(action_tests.Crew)
        station, station2 = a.station, a.station2
        try:
            self.assertIn('--station/--station2', action_tests.swap_targets(action_tests.Crew, a))
            self.assertEqual((a.station, a.station2), (station2, station))
        finally:
            action_tests.swap_targets(action_tests.Crew, a)

    def test_dockpair_roles(self):
        a = self.args(action_tests.DockPair)
        names = {k: [s[0] for s in action_tests.DockPair.chain(k, a)] for k in action_tests.PLAYERS}
        self.assertIn('after', names[1])          # player 1 leaves first and flies on
        self.assertNotIn('after', names[2])
        self.assertIn('held', names[2])
        try:
            action_tests.swap_targets(action_tests.DockPair, a)
            self.assertNotIn('after', [s[0] for s in action_tests.DockPair.chain(1, a)])
        finally:
            action_tests.swap_targets(action_tests.DockPair, a)


if __name__ == '__main__':
    unittest.main()

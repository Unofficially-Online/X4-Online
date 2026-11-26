"""tools/dev/release_check.py and its script ops (rc_look, rc_accept, rc_activate, rc_drive in
native_gate_ui_replay_core.lua): the plans, the row readers, the performance comparison, the verdicts and the lock
rules, on synthetic logs and a mocked game (no X4). The checks themselves need X4 (X4LAN_GAMEDATA, a gate DLL)."""
import importlib.util
import io
import json
import os
import sys
import tempfile
import time
import unittest
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('release_check', ROOT / 'tools/dev/release_check.py')
rc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rc)


def write(folder, name, text):
    path = Path(folder) / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='latin-1')
    return path


def timing_log(sim, rframe, eye_per_tick, ticks=range(600, 1300, 60)):
    """A client log with FRAME_TIMING every 60 ticks and a status counter."""
    lines = ['[General] 0.00 start']
    for t in ticks:
        lines.append('[=ERROR=] %.2f [X4LAN] NATIVE_GATE FRAME_TIMING tick=%d frames=%d wait_us=100 frame_us=9000 outside_us=20000 '
                     'pre_us=500 sim_us=%d pace_us=300 probe_us=1500 slot10_us=1800 sim_rest_us=%d rframes=2 rframe_us=%d' %
                     (t / 30, t, t, sim, sim - 1800, rframe))
        lines.append('[X4LAN] NATIVE_GATE PILOT_INPUTS tick=%d eye=seat/%d/%d/0/0.000 scopes=%d' % (t, t * 2, t * eye_per_tick, t * 9))
        lines.append('[X4LAN] NATIVE_GATE MASS_TRAFFIC tick=%d total_wait_us=%d wait_us=40' % (t, t * 1500))
    return '\n'.join(lines) + '\n'


class Rows(unittest.TestCase):
    def test_fields_split_slashed_values(self):
        f = rc.fields('tick=60 eye=seat/52961/456680/0 hud_zone=1/5961/3 mode=sim')
        self.assertEqual(f['eye.1'], '52961')
        self.assertEqual(f['hud_zone.2'], '3')
        self.assertEqual(f['mode'], 'sim')
        self.assertNotIn('eye.0', f)          # 'seat' is no number

    def test_rows_carry_the_last_stamp(self):
        with tempfile.TemporaryDirectory() as d:
            log = write(d, 'c.log', '[=ERROR=] 12.50 [X4LAN] NATIVE_GATE RENDER_FRAMES tick=60 x=1\n[X4LAN] NATIVE_GATE STATE_END tick=60 e=0\n')
            self.assertEqual([(s, k) for s, k, _ in rc.rows(log)], [(12.5, 'RENDER_FRAMES'), (12.5, 'STATE_END')])

    def test_dll_specs(self):
        self.assertEqual(rc.parse_dll('C:/x/build-all10/x4lan_gate_64.dll@3fb2dc25'), (Path('C:/x/build-all10/x4lan_gate_64.dll'), '3fb2dc25'))
        self.assertEqual(rc.parse_dll('C:/x/x4lan_gate_64.dll'), (Path('C:/x/x4lan_gate_64.dll'), None))
        self.assertEqual(rc.parse_dll('C:/a@b/x.dll'), (Path('C:/a@b/x.dll'), None))

    def test_lua_literal(self):
        self.assertEqual(rc.lua([[1, 'rc_look'], [2, 'input', 'state', 13, 1, True, 0.5, "it's"]]),
                         "{{1,'rc_look'},{2,'input','state',13,1,true,0.5,'it\\'s'}}")


class Plans(unittest.TestCase):
    def test_perf_plan_repeats_one_minute_of_flight(self):
        plan = rc.perf_plan(300 + 3 * 1800)
        self.assertEqual([e for e in plan if e[1] == 'travel_key'][:2], [[600, 'travel_key'], [1050, 'travel_key']])
        self.assertEqual(sum(1 for e in plan if e[1] == 'aim_fire' and e[2] == 1), 3)
        every = [e for e in plan if e[1] == 'every']
        self.assertEqual(every[0][2:4], [2, 1500])
        self.assertTrue(all(e[0] < 300 + 3 * 1800 for e in plan))

    def test_plans_only_for_the_played_slots(self):
        self.assertEqual(rc.plans_for(3, ['x'], [2]), [[], ['x'], []])
        self.assertEqual(rc.plans_for(2, ['x']), [['x'], ['x']])

    def test_missions_plan_looks_before_and_after_shift_a(self):
        plan = rc.missions_plan(25000)
        looks = [e[0] for e in plan if e[1] == 'rc_look']
        key = [e[0] for e in plan if e[1] == 'input_local' and e[3] == rc.AUTOPILOT_KEY]
        self.assertTrue(looks[0] < key[0] < looks[1])
        self.assertIn([400, 'rc_mm', 'open'], plan)

    def test_a_solo_client_takes_its_locks_first_profile(self):
        firsts = {'x4-a': 45855, 'x4-b': 45857, 'x4-c': 45850, 'x4-d': 45853}
        for lock, profile in firsts.items():
            for slot in range(1, 9):
                self.assertEqual(rc.profile_of(rc.solo_first_profile(lock, slot), slot), profile)
        self.assertEqual([rc.profile_of(rc.CLIENTS_FIRST, k) for k in (1, 2, 3)], [45855, 45856, 45857])
        self.assertEqual(rc.lock_profiles(['x4-a', 'x4-b', 'x4-c']), {45855, 45856, 45857, 45858, 45850, 45851})

    def test_play_refuses_profiles_outside_the_held_slots(self):
        with self.assertRaises(RuntimeError):         # client 1 at index 0 is 45850: not x4-a's or x4-b's
            rc.play(ROOT, 'o', 's', rc.Lane(), 'x', 0, solo=1, log=lambda s: None)


class Perf(unittest.TestCase):
    def test_profile_means_rates_and_relay_pace(self):
        with tempfile.TemporaryDirectory() as d:
            log = write(d, 'client2/x4ls-c2.log', timing_log(20000, 3000, 70))
            write(d, 'native-inputs.jsonl', ''.join('{"tick":%d,"released_at":%.3f,"rows":{}}\n' % (t, 1000 + t / 25.0) for t in range(0, 1300, 10)))
            p = rc.perf_profile(log, d)
            self.assertAlmostEqual(p['tick_us'], 20500)
            self.assertAlmostEqual(p['frame_us'], 3000)
            self.assertAlmostEqual(p['tps'], 25.0, places=3)
            self.assertAlmostEqual(p['rates']['PILOT_INPUTS.eye.2'], 70)
            self.assertAlmostEqual(p['rates']['MASS_TRAFFIC.total_wait_us'], 1500)      # a cumulative time: us per tick
            self.assertAlmostEqual(p['timing']['MASS_TRAFFIC.wait_us'], 40)
            self.assertAlmostEqual(p['timing']['FRAME_TIMING.slot10_us'], 1800)

    def test_verdict_fails_beyond_ten_percent_and_names_the_differences(self):
        with tempfile.TemporaryDirectory() as d:
            base = rc.perf_profile(write(d, 'b.log', timing_log(20000, 3000, 10)))
            same = rc.perf_profile(write(d, 's.log', timing_log(21000, 3100, 10)))
            slow = rc.perf_profile(write(d, 'c.log', timing_log(24000, 3000, 70)))
            lines, failed = rc.perf_verdict('x', base, same)
            self.assertEqual(failed, 0)
            lines, failed = rc.perf_verdict('x', base, slow)
            self.assertEqual(failed, 1)
            text = '\n'.join(lines)
            self.assertIn('FRAME_TIMING.sim_us', text)
            self.assertIn('PILOT_INPUTS.eye.2', text)          # 10 -> 70 poses per tick
            times, activity = rc.differences(base, slow)
            self.assertIn('FRAME_TIMING.sim_us', [e[0] for e in times[:2]])

    def test_report_groups_runs_by_name(self):
        with tempfile.TemporaryDirectory() as d:
            for name, sim in (('a', 20000), ('b', 20400), ('c', 30000), ('e', 21000)):
                write(d, '%s/client1/engine-output.log' % name, timing_log(sim, 3000, 10))
            self.assertEqual(list(rc.client_logs(Path(d) / 'a')), [1])
            out = io.StringIO()
            with redirect_stdout(out):
                code = rc.main(['report', 'perf', 'candidate=%s/c' % d, 'candidate=%s/e' % d, 'rk9=%s/a' % d, 'rk9=%s/b' % d,
                                'without-own_throttle=%s/a' % d])
            text = out.getvalue()
            self.assertEqual(code, 1, text)                     # (30000+21000)/2 against (20000+20400)/2: 26% slower
            self.assertIn('FAIL  client 1: candidate vs rk9', text)
            self.assertIn('own_throttle', text)                 # the ablation's difference is that module's cost

    def test_module_costs_are_set_against_the_run_to_run_spread(self):
        def p(tick, frame):
            return {'tick_us': tick, 'frame_us': frame, 'timing': {}, 'rates': {}, 'log': 'x', 'samples': 1, 'ticks': 1}
        self.assertIsNone(rc.run_noise({('candidate', 1): [p(100, 10)]}, 1))
        profiles = {('candidate', 1): [p(30000, 1500), p(32000, 1560)], ('baseline', 1): [p(31000, 1550)],
                    ('without-own_throttle', 1): [p(32500, 1540)]}
        self.assertEqual(rc.run_noise(profiles, 1), (2, 2000, 60))
        out = io.StringIO()
        with redirect_stdout(out):
            rc.perf_summary(profiles, [('candidate', '', '', ()), ('baseline', '', '', ()), ('without-own_throttle', '', '', ())], [1])
        self.assertIn('run-to-run spread of one build: 2000 us per tick, 60 us per frame', out.getvalue())
        self.assertIn('own_throttle', out.getvalue())
        self.assertIn('(within the noise)', out.getvalue())

    def test_missing_measures_are_not_failures(self):
        lines, failed = rc.perf_verdict('x', {'timing': {}, 'rates': {}}, {'timing': {}, 'rates': {}})
        self.assertEqual(failed, 0)
        self.assertTrue(all('n/a' in l for l in lines[:3]))


def look(slot, active, listed, all_=None, guidance='none', target='0', ap=0, menus='-'):
    """An rc_look answer: active (id, owner, name), listed [(id, owner, name)], all_ [(id, owner)]."""
    def part(items):
        return '%d[%s]' % (len(items), ','.join('%s/%s' % (i[0], i[1]) for i in items))
    all_ = all_ if all_ is not None else [(i, o) for i, o, _ in listed]
    own = [n for i, o, n in listed if o == str(slot)]
    return ('slot=%d active=%s/%s ap=%d guidance=%s target=%s distance=- step=1/6 listed=%s all=%s own=[%s] menus=%s objective=Fly' %
            (slot, active[0], active[1], ap, guidance, target, part(listed), part(all_), ','.join(own), menus))


def script_log(rows_):
    return ''.join('[X4LAN] UI_REPLAY_SCRIPT tick=%d op=%s ok=true result=%s%s\n' % (t, op, r, ' scope=1' if op != 'rc_accept' else '')
                   for t, op, r in rows_)


class MissionVerdicts(unittest.TestCase):
    def run_verdict(self, logs, offer=None, fresh=False):
        report = rc.Report('t')
        with tempfile.TemporaryDirectory() as d:
            paths = {k: write(d, 'c%d.log' % k, text) for k, text in logs.items()}
            rc.missions_verdict(report, paths, offer, fresh)
            rc.shift_a_verdict(report, paths)
        return report

    def test_own_missions_and_the_accepted_offer(self):
        p2 = script_log([
            (190, 'rc_look', look(2, ('11', '2', 'Cadet'), [('11', '2', 'Cadet')], [('11', '2'), ('7', '1')], guidance='55', target='55')),
            (330, 'rc_look', look(2, ('11', '2', 'Cadet'), [('11', '2', 'Cadet')], [('11', '2'), ('7', '1')], guidance='55', target='55', ap=1)),
            (440, 'rc_mm', 'opened'),
            (500, 'rc_look', look(2, ('11', '2', 'Cadet'), [('11', '2', 'Cadet')], guidance='55', target='55', menus='MapMenu:mission')),
            (1990, 'rc_offers', 'n=12 story=2[2/Defenders_of_Sol,1/Defenders_of_Sol] other=1'),
            (2000, 'rc_accept', 'accepted 90 plot/x terran Defenders_of_Sol'),
            (2300, 'rc_look', look(2, ('91', '2', 'Defenders_of_Sol'), [('11', '2', 'Cadet'), ('91', '2', 'Defenders_of_Sol')],
                                   guidance='60', target='60')),
        ])
        report = self.run_verdict({2: p2}, 'Defenders')
        text = report.text()
        self.assertEqual(report.failed, 0, text)
        self.assertIn('PASS  player 2: story offer "Defenders" accepted', text)
        self.assertIn('PASS  player 2: Shift+A after the continue', text)
        self.assertIn('offers of another player in its list', text)

    def test_the_world_count_follows_the_managers_row(self):
        rows_ = [(190, 'rc_look', look(2, ('11', '2', 'C'), [('11', '2', 'C')], [('11', '2'), ('7', '1')]))]
        state = '[X4LAN] NATIVE_GATE UI_MISSIONS_STATE tick=100 missions=%d offers=40 conversation=1.0 active=0\n'
        self.assertIn('PASS  player 2: world count = the manager', self.run_verdict({2: state % 2 + script_log(rows_)}).text())
        self.assertIn('FAIL  player 2: world count = the manager', self.run_verdict({2: state % 3 + script_log(rows_)}).text())

    def test_another_players_mission_on_this_client_fails(self):
        p1 = script_log([(190, 'rc_look', look(1, ('7', '1', 'Old'), [('7', '1', 'Old'), ('11', '2', 'Cadet')]))])
        report = self.run_verdict({1: p1})
        self.assertIn("FAIL  player 1: lists only its own missions", report.text())

    def test_a_mission_md_gave_but_the_list_hides_fails(self):
        p2 = script_log([(190, 'rc_look', look(2, ('0', '0', '-'), [], [('11', '2')]))])
        report = self.run_verdict({2: p2})
        self.assertIn('FAIL  player 2: lists every mission MD gave it', report.text())
        self.assertIn('FAIL  player 2: active mission is its own', report.text())

    def test_guidance_elsewhere_and_shift_a_that_does_nothing_fail(self):
        p2 = script_log([(190, 'rc_look', look(2, ('11', '2', 'C'), [('11', '2', 'C')], guidance='77', target='55')),
                         (330, 'rc_look', look(2, ('11', '2', 'C'), [('11', '2', 'C')], guidance='77', target='55', ap=0))])
        text = self.run_verdict({2: p2}).text()
        self.assertIn('FAIL  player 2: objective marker = its mission', text)
        self.assertIn('FAIL  player 2: Shift+A after the continue', text)
        self.assertIn('the autopilot is off', text)

    def test_no_guidance_mission_is_no_failure(self):
        p1 = script_log([(190, 'rc_look', look(1, ('7', '1', 'C'), [('7', '1', 'C')], guidance='none', target='55')),
                         (330, 'rc_look', look(1, ('7', '1', 'C'), [('7', '1', 'C')], guidance='none', target='55', ap=1))])
        text = self.run_verdict({1: p1}).text()
        self.assertIn('PASS  player 1: objective marker = its mission', text)
        self.assertIn('PASS  player 1: Shift+A after the continue', text)

    def test_an_offer_never_made_for_a_copy_fails(self):
        p2 = script_log([(190, 'rc_look', look(2, ('11', '2', 'C'), [('11', '2', 'C')]))])
        self.assertIn('FAIL  player 2: story offer "Defenders" accepted', self.run_verdict({2: p2}, 'Defenders').text())


class WeaponPanel(unittest.TestCase):
    def test_every_player_sees_its_active_group(self):
        row = ('[X4LAN] UI_REPLAY_SCRIPT tick=%d op=hud_weapons ok=true result=ship=5 stance=false groups=1/1 p1=%d/1/0/7/0 '
               'p2=%d/1/0/7/0 s1=0/0/0/0/0 scope=1' + chr(10))
        with tempfile.TemporaryDirectory() as d:
            logs = {1: write(d, 'c1.log', row % (180, 1, 1)), 2: write(d, 'c2.log', row % (180, 0, 0))}
            report = rc.Report('t')
            rc.weapons_verdict(report, logs)
            text = report.text()
            self.assertIn('PASS  player 1: weapon panel', text)
            self.assertIn('FAIL  player 2: weapon panel', text)
            self.assertIn('(player 1: 2 of 2)', text)
        self.assertTrue(rc.has_op(ROOT, 'rc_look'))
        self.assertFalse(rc.has_op(ROOT, 'no_such_op'))


class LeadMarkRange(unittest.TestCase):
    def test_in_range_near_out_far_as_player_one(self):
        def rows_(near, far, assist=('2', '2')):
            return ''.join('[X4LAN] UI_REPLAY_SCRIPT tick=%d op=aim_look ok=true result=assist=%s mouse=false target=77 in_range=%s scope=1%s'
                           % (t, a, r, chr(10)) for t, a, r in ((2360, assist[0], near), (2450, assist[1], far)))
        with tempfile.TemporaryDirectory() as d:
            logs = {1: write(d, 'c1.log', rows_('true', 'false')), 2: write(d, 'c2.log', rows_('true', 'true')),
                    3: write(d, 'c3.log', rows_('true', 'false', ('2', '0')))}
            report = rc.Report('t')
            rc.range_verdict(report, logs)
            text = report.text()
            self.assertIn('PASS  player 1: lead mark range', text)
            self.assertIn('FAIL  player 2: lead mark range', text)       # in range at 10 km
            self.assertIn('FAIL  player 3: lead mark range', text)       # another assist state than player 1
        plan = rc.range_plan(2, 3)
        self.assertEqual(plan[0], [2600, 'goto_slot', 3, 1000])
        self.assertEqual(rc.range_plan(3, 3)[0][2], 1)
        self.assertEqual(rc.range_plan(1, 1), [])


class SatelliteVerdicts(unittest.TestCase):
    def test_milestones_speech_and_sync(self):
        with tempfile.TemporaryDirectory() as d:
            c1 = ('[X4LAN] NATIVE_GATE MILESTONE reached name=terran_cadet-M1-slot1 due=3 save=900\n'
                  '[X4LAN] NATIVE_GATE MILESTONE reached name=terran_cadet-M1-slot2 due=3 save=990\n'
                  '[X4LAN] NATIVE_GATE SPEECH_START tick=500 speaker=1a owner=1 scope=1 muted=0 state=1\n'
                  '[X4LAN] NATIVE_GATE SPEECH_START tick=600 speaker=1b owner=2 scope=2 muted=1 state=1\n'
                  '[X4LAN] NATIVE_GATE WORLD_HASH tick=1800 objects=5 state=W:1800:5:aa\n')
            c2 = c1.replace('owner=1 scope=1 muted=0', 'owner=1 scope=1 muted=1').replace('owner=2 scope=2 muted=1', 'owner=2 scope=2 muted=0')
            logs = {1: write(d, 'client1/x4ls-c1.log', c1), 2: write(d, 'client2/x4ls-c2.log', c2)}
            write(d, 'desyncs.log', '')
            report = rc.Report('t')
            rc.satellite_verdict(report, logs, 2, d)
            self.assertEqual(report.failed, 0, report.text())
            # player 2's line heard on client 1 as well, and a third player without M1
            write(d, 'client1/x4ls-c1.log', c1.replace('owner=2 scope=2 muted=1', 'owner=2 scope=2 muted=0'))
            report = rc.Report('t')
            rc.satellite_verdict(report, logs, 3, d)
            text = report.text()
            self.assertIn('FAIL  player 2: its speech on its client only', text)
            self.assertIn('FAIL  player 3: cadet chapter 1 (M1)', text)

    def test_the_run_ends_once_every_milestone_is_in(self):
        with tempfile.TemporaryDirectory() as d:
            row = '[X4LAN] NATIVE_GATE MILESTONE reached name=terran_cadet-M1-slot%d due=1 save=2' + chr(10)
            logs = {1: write(d, 'c1.log', row % 1), 2: write(d, 'c2.log', row % 1)}
            until = rc.milestones_done([1, 2], 2, settle=0)
            self.assertIsNone(until(logs))                       # slot 2's M1 is missing
            with open(logs[1], 'a', encoding='latin-1') as f:
                f.write(row % 2)
            self.assertIsNone(until(logs))                       # on client 1 only
            with open(logs[2], 'a', encoding='latin-1') as f:
                f.write(row % 2)
            self.assertEqual(until(logs), 'every M1 on every client')

    def test_an_earlier_runs_log_is_not_read_and_a_new_log_is_read_from_its_start(self):
        with tempfile.TemporaryDirectory() as d:
            row = '[X4LAN] NATIVE_GATE MILESTONE reached name=terran_cadet-M1-slot%d due=1 save=2' + chr(10)
            old = write(d, 'c1.log', 'x' * 5000 + chr(10) + row % 1)          # the profile's log of an earlier run
            os.utime(old, (time.time() - 3600, time.time() - 3600))
            watch = rc.Watch()
            watch.read({1: old}, since=time.time() - 60)
            self.assertEqual(watch.found, {})                       # not this run's
            watch.read({1: old})
            self.assertIn(1, watch.found['terran_cadet-M1-slot1'])
            write(d, 'c1.log', 'starting' + chr(10))                # X4 started its log anew: shorter than read
            watch.read({1: old})
            self.assertNotIn(1, watch.found['terran_cadet-M1-slot1'])
            with open(old, 'a', encoding='latin-1') as f:
                f.write(row % 1)
            watch.read({1: old})
            self.assertIn(1, watch.found['terran_cadet-M1-slot1'])

    def test_a_split_run_ends_once_each_m1_is_in_on_its_own_client(self):
        with tempfile.TemporaryDirectory() as d:
            row = '[X4LAN] NATIVE_GATE MILESTONE reached name=terran_cadet-M1-slot%d due=1 save=2' + chr(10)
            logs = {1: write(d, 'c1.log', row % 1), 2: write(d, 'c2.log', '')}
            host = write(d, 'host.log', 'tick 900 (game 0.5 min), in sync' + chr(10))
            until = rc.milestones_done([1, 2], 2, settle=0)
            self.assertIsNone(until(logs, host))                 # in sync: wait for every M1 everywhere
            with open(logs[2], 'a', encoding='latin-1') as f:
                f.write(row % 2)
            self.assertIsNone(until(logs, host))
            with open(host, 'a', encoding='utf-8') as f:
                f.write('relay: NATIVE_GATE DESYNC tick=6600 slots=2 first=6600 mismatches=1' + chr(10))
            self.assertEqual(until(logs, host), 'the session split; every M1 on its own client')
            self.assertIsNone(rc.milestones_done([1, 2], 2, settle=0)({1: logs[1], 2: write(d, 'c3.log', '')}, host))

    def test_the_first_tick_whose_digest_parts_differ(self):
        with tempfile.TemporaryDirectory() as d:
            rows_ = '[X4LAN] NATIVE_GATE DIGEST_PARTS tick=%d hash=%s world=aa pilot=%s' + chr(10)
            logs = {1: write(d, 'c1.log', rows_ % (10, 'x1', 'p1') + rows_ % (11, 'x2', 'p2')),
                    2: write(d, 'c2.log', rows_ % (10, 'y1', 'p1') + rows_ % (11, 'y2', 'q2'))}
            self.assertEqual(rc.first_part_split(logs), (11, ['pilot']))
        self.assertEqual(rc.suit_plan(600), [[600, 'input', 'action', 0x115, 0], [700, 'suit_out']])

    def test_the_planned_end_is_no_stop(self):
        with tempfile.TemporaryDirectory() as d:
            end = ('[X4LAN] NATIVE_GATE STOP slot=p1 completed=5441 reason=1 restored=1 wait_game_delta=0' + chr(10) +
                   '[X4LAN] NATIVE_GATE STOP_NOTICE reason=1 the relay ended the session (its tick limit or a mismatch)' + chr(10))
            stop = '[X4LAN] NATIVE_GATE STOP slot=p2 completed=2811 reason=79 restored=1 stage=budget_owner' + chr(10)
            logs = {1: write(d, 'c1.log', end), 2: write(d, 'c2.log', stop)}
            self.assertEqual(len(rc.stops(logs)), 1)
            self.assertIn('reason=79', rc.stops(logs)[0])

    def test_refused_settings_fail_the_run(self):
        with tempfile.TemporaryDirectory() as d:
            line = '[=ERROR=] 4.30 [X4LAN] NATIVE_GATE ERROR CONFIG_REFUSED [string "x"]:268: native_gate_world_record must be a file count 1..16' + chr(10)
            logs = {1: write(d, 'c1.log', line), 2: write(d, 'c2.log', 'nothing' + chr(10))}
            report = rc.Report('t')
            rc.common_checks(report, logs)
            self.assertTrue(report.failed)
            self.assertIn('file count 1..16', report.text())
            armed = '[=ERROR=] 4.37 [X4LAN] NATIVE_GATE ERROR [string "extensions/x4_lan_lab/ui/native_gate_probe.lua"]:8402: runtime ids callers refused mode=set' + chr(10)
            self.assertIn('runtime ids callers refused', rc.config_refusals({3: write(d, 'c3.log', armed)})[0][1])

    def test_a_split_fails(self):
        with tempfile.TemporaryDirectory() as d:
            logs = {1: write(d, 'client1/x4ls-c1.log', '[X4LAN] NATIVE_GATE WORLD_HASH tick=1800 state=W:a\n'),
                    2: write(d, 'client2/x4ls-c2.log', '[X4LAN] NATIVE_GATE WORLD_HASH tick=1800 state=W:b\n')}
            report = rc.Report('t')
            rc.sync_verdict(report, logs, d)
            self.assertIn('FAIL  in sync', report.text())

    def test_throttle_pulse(self):
        with tempfile.TemporaryDirectory() as d:
            steady = ''.join('[X4LAN] NATIVE_GATE OWN_THROTTLE tick=%d s=1 desired=1 smoothed=1 key=0 hud=%d shown=1 targeted=1/1 boost=0/0 '
                             'travel=1/1 actual=1/1 mps=%.6g/%.6g dv=0\n' % (t, t, 2000.0, 2000.0 if t % 2 else 1500.0) for t in range(100, 200))
            report = rc.Report('t')
            rc.throttle_verdict(report, {2: write(d, 'c2.log', steady)})
            self.assertIn('FAIL  player 2: HUD speed steady', report.text())
            fine = steady.replace('/1500/', '/2000/').replace('mps=2000/1500', 'mps=2000/2000')
            report = rc.Report('t')
            rc.throttle_verdict(report, {2: write(d, 'c3.log', fine)})
            self.assertIn('PASS  player 2: HUD speed steady', report.text())


class LockRules(unittest.TestCase):
    def test_one_two_three_and_the_user(self):
        with tempfile.TemporaryDirectory() as d:
            a = rc.Locks('agent-a', 'test', root=d, log=lambda s: None, poll=0)
            b = rc.Locks('agent-b', 'test', root=d, log=lambda s: None, poll=0)
            c = rc.Locks('agent-c', 'test', root=d, log=lambda s: None, poll=0)
            self.assertEqual(a.try_acquire('one')[0], ['x4-b'])
            self.assertEqual(a.index, 6)
            self.assertIn('agent-a', (Path(d) / 'x4-b/owner.txt').read_text())
            self.assertEqual(sorted(b.try_acquire('two')[0]), ['x4-c', 'x4-d'])   # the other pair is free
            self.assertEqual(b.index, 0)
            held, why = c.try_acquire('two')                   # neither pair: queued
            self.assertIsNone(held)
            self.assertEqual((Path(d) / 'want-both.txt').read_text().split(), ['agent-c'])
            self.assertIsNone(a.try_acquire('one')[0])          # a queued run of several clients comes first
            a.done()
            self.assertEqual(sorted(c.try_acquire('two')[0]), ['x4-a', 'x4-b'])
            self.assertEqual(c.index, 4)
            c.done()
            b.done()
            self.assertFalse((Path(d) / 'want-both.txt').exists())
            self.assertEqual(sorted(p.name for p in Path(d).iterdir()), [])
            self.assertEqual(sorted(b.try_acquire('three')[0]), ['x4-a', 'x4-b', 'x4-c'])
            self.assertEqual(b.index, 4)
            b.done()
            (Path(d) / 'USER-PLAYING.txt').write_text('playing')
            held, why = b.try_acquire('three')
            self.assertIsNone(held)
            self.assertIn('USER-PLAYING', why)
            self.assertIsNone(b.try_acquire('one')[0])          # no X4 at all while the user plays (the user's order)
            b.beside_user = a.beside_user = c.beside_user = True  # unless the user allows one --solo X4 beside the game
            self.assertEqual(b.try_acquire('one')[0], ['x4-b'])
            self.assertEqual(a.try_acquire('one')[0], ['x4-a'])
            self.assertIsNone(c.try_acquire('one')[0])          # x4-c and x4-d are closed then
            self.assertIsNone(c.try_acquire('two')[0])
            a.done()
            b.done()
            self.assertEqual(sorted(c.try_acquire('two')[0]), ['x4-a', 'x4-b'])   # the open pair, never c+d
            self.assertIsNone(a.try_acquire('three')[0])
            c.done()


class Pause(unittest.TestCase):
    def test_a_pause_file_holds_new_runs(self):
        with tempfile.TemporaryDirectory() as d:
            pause = Path(d) / 'PAUSE'
            locks = rc.Locks('t', 'test', root=Path(d) / 'locks', log=lambda s: None, pause=pause)
            (Path(d) / 'locks').mkdir()
            pause.write_text('fix agent first', encoding='utf-8')
            held, why = locks.try_acquire('one')
            self.assertIsNone(held)
            self.assertIn('paused', why)
            pause.unlink()
            held, why = locks.try_acquire('one')
            self.assertEqual(held, ['x4-b'])
            locks.done()


class Suites(unittest.TestCase):
    def test_a_suite_running_from_the_tree_holds_the_run(self):
        import subprocess
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(rc.suites_in(d), [])
            suite = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)', 'unittest', 'discover'], cwd=d)
            try:
                for _ in range(50):
                    if rc.suites_in(d):
                        break
                    time.sleep(0.1)
                self.assertEqual(rc.suites_in(d), [suite.pid])
                with self.assertRaises(RuntimeError):
                    rc.wait_for_suites(d, log=lambda s: None, limit=0, poll=0)
            finally:
                suite.kill()
                suite.wait()
            self.assertEqual(rc.suites_in(d), [])
            self.assertEqual(rc.suites_in(ROOT), [])            # this test's own suite does not count


class Disk(unittest.TestCase):
    """A run's raw logs and saves go after its verdict; the verdict, the census records and the crash notes stay."""

    def test_a_run_keeps_its_verdict_and_census_records(self):
        with tempfile.TemporaryDirectory() as d:
            run = Path(d)
            for name in ('client1/engine-output.log', 'client1/x4lan-census-3.txt', 'client1/x4lan-crash.txt',
                         'client1/installed/ui/a.lua', 'saves/milestones/m1.xml.gz', 'native-inputs.jsonl', 'native-barrier.json',
                         'summary.json', 'desyncs.log'):
                (run / name).parent.mkdir(parents=True, exist_ok=True)
                (run / name).write_text('x' * 100, encoding='utf-8')
            self.assertEqual(rc.prune_run(run, log=lambda s: None), 0)              # no verdict kept yet: nothing goes
            rc.save_report(run, rc.Report('t'))
            self.assertGreater(rc.prune_run(run, log=lambda s: None), 0)
            left = sorted(str(p.relative_to(run)).replace(chr(92), '/') for p in run.rglob('*') if p.is_file())
            self.assertEqual(left, ['client1/x4lan-census-3.txt', 'client1/x4lan-crash.txt', 'desyncs.log',
                                    'release-check-report.txt', 'summary.json'])
            self.assertFalse((run / 'saves').exists())

    def test_a_run_waits_for_free_space(self):
        self.assertIsNotNone(rc.disk_free_gb(ROOT))
        self.assertIsNotNone(rc.wait_for_disk(0, log=lambda s: None))


class GraftOps(unittest.TestCase):
    """Every release is driven by this checkout's ops: an older core gets the hook and the block, a core with an older
    block gets this one, the rest of that core stays as it was."""

    def test_old_and_older_cores_get_these_ops(self):
        mine = rc.ops_block((ROOT / rc.CORE).read_text(encoding='utf-8').replace(chr(13) + chr(10), chr(10)))
        self.assertIsNotNone(mine)
        mine_core = (ROOT / rc.CORE).read_text(encoding='utf-8').replace(chr(13) + chr(10), chr(10))
        head = mine_core[:len(mine_core) - len(mine)]
        older = head.replace(rc.OPS_HOOK + chr(10) + rc.NEW_TAIL, rc.OLD_TAIL)         # a core of before the ops
        self.assertEqual(older.count(rc.OLD_TAIL), 1)
        with tempfile.TemporaryDirectory() as d:
            core = Path(d) / rc.CORE
            core.parent.mkdir(parents=True)
            for text in (older, head + '-- ------' + chr(10) + rc.OPS_START + ' an older block' + chr(10) + 'X=1' + chr(10)):
                core.write_bytes(text.replace(chr(10), chr(13) + chr(10)).encode('utf-8'))
                self.assertTrue(rc.graft_ops(d, log=lambda s: None))
                got = core.read_bytes().decode('utf-8')
                self.assertIn(chr(13) + chr(10), got)                                   # its line ends kept
                got = got.replace(chr(13) + chr(10), chr(10))
                self.assertEqual(rc.ops_block(got), mine)
                self.assertEqual(got.count(rc.OPS_HOOK), 1)
                self.assertTrue(rc.graft_ops(d, log=lambda s: None))                    # again: nothing changes
                self.assertEqual(core.read_bytes().decode('utf-8').replace(chr(13) + chr(10), chr(10)), got)


class StaleState(unittest.TestCase):
    def test_a_stopped_runs_state_goes_a_running_ones_stays(self):
        with tempfile.TemporaryDirectory() as d:
            view = Path(d) / 'lanes/c1'
            view.mkdir(parents=True)
            state = Path(d) / '.local/x4ls/session.json'
            state.parent.mkdir(parents=True)
            state.write_text(json.dumps({'views': [str(view)], 'pids': [os.getpid()], 'relay_pid': None}))
            self.assertFalse(rc.clear_stale_state(d, log=lambda s: None))        # this process runs: kept
            self.assertTrue(state.exists())
            state.write_text(json.dumps({'views': [str(view)], 'pids': [], 'relay_pid': None}))
            self.assertTrue(rc.clear_stale_state(d, log=lambda s: None))
            self.assertFalse(state.exists())
            self.assertFalse(view.exists())                                       # an empty view folder goes too
            self.assertTrue(rc.clear_stale_state(d, log=lambda s: None))

    def test_a_stopped_runs_own_relay_is_stopped(self):
        import subprocess
        with tempfile.TemporaryDirectory() as d:
            state = Path(d) / '.local/x4ls/session.json'
            state.parent.mkdir(parents=True)
            relay = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)', str(Path(d) / 'relay-of-this-checkout')])
            try:
                state.write_text(json.dumps({'views': [], 'pids': [], 'relay_pid': relay.pid}))
                self.assertTrue(rc.clear_stale_state(d, log=lambda s: None))     # its command line names this checkout
                self.assertIsNotNone(relay.poll())
                self.assertFalse(state.exists())
            finally:
                if relay.poll() is None:
                    relay.kill()


class LockSlots(unittest.TestCase):
    def test_a_run_limited_to_some_slots(self):
        with tempfile.TemporaryDirectory() as d:
            a = rc.Locks('agent-a', 'test', root=d, log=lambda s: None, poll=0, slots=['x4-a', 'x4-b'])
            (Path(d) / 'x4-a').mkdir()
            (Path(d) / 'x4-b').mkdir()
            held, why = a.try_acquire('one')
            self.assertIsNone(held)                              # x4-c and x4-d are free, but not this run's
            self.assertFalse((Path(d) / 'x4-c').exists())
            held, why = a.try_acquire('two')
            self.assertIsNone(held)
            self.assertFalse((Path(d) / 'x4-d').exists())
            a.done()


class ScriptOps(unittest.TestCase):
    """The ops under LuaJIT with a mocked game: what rc_look reports, which offer rc_accept takes, rc_drive's steps."""

    @classmethod
    def setUpClass(cls):
        try:
            from lupa.luajit21 import LuaRuntime
        except ImportError:
            raise unittest.SkipTest('needs lupa (LuaJIT 2.1)')
        cls.Lua = LuaRuntime

    def setUp(self):
        self.L = self.Lua()
        self.L.execute((ROOT / 'tools/dev/native_gate_ui_replay_core.lua').read_text(encoding='utf-8'))
        self.L.execute(r'''
          calls={}
          missions={{id='11',name='Terran Cadet',owner=2},{id='7',name='Old Job',owner=1},{id='12',name='Patrol',owner=0}}
          offers={{id='90',name='Defenders of Sol',kind='plot/x',faction='terran',actor='a90',owner=1},
                  {id='91',name='Defenders of Sol',kind='plot/x',faction='terran',actor='a91',owner=2}}
          state={active='11',ap=false,suit=false,objective='Fly to the satellite',target='55',distance=4000}
          local function visible() local out={} for _,m in ipairs(missions) do if m.owner==2 or m.owner==0 then out[#out+1]=m end end return out end
          local function details(m) return m.id,m.name,'d',1,'t',m.kind or 'plot','s','sn','f',1,'r',{{step=1,text='x'},{step=2,text=state.objective}},2 end
          env={GetNumMissions=function() return #visible() end,GetMissionDetails=function(i) return details(visible()[i]) end,
            X4LAN_UI_REPLAY_STOCK={GetNumMissions=function() return #missions end,GetMissionDetails=function(i) return details(missions[i]) end},
            GetMissionObjective=function(i) if missions[i].id==state.active then return state.objective end return 'other' end,
            ConvertStringTo64Bit=function(s) return tostring(s) end,ConvertStringToLuaID=function(s) return 'L'..tostring(s) end,
            GetActiveGuidanceMissionComponent=function() return state.target end,
            GetComponentData=function() return 'sector1' end,
            SignalObject=function(...) calls[#calls+1]='signal:'..table.concat({...},',') end,
            OpenMenu=function(name,p) calls[#calls+1]='open:'..name..':'..tostring(p[6]) end}
          C={GetActiveMissionID=function() return state.active..'ULL' end,IsAutoPilotActive=function() return state.ap end,
            GetMissionIDObjectiveTarget=function() return state.target..'ULL' end,GetPlayerOccupiedShipID=function() return '5ULL' end,
            GetObjectPositionInSector=function(id) if tostring(id):match('%d+')=='5' then return {x=0,y=0,z=0} end return {x=0,y=0,z=state.distance} end,
            SetActiveMission=function(id) calls[#calls+1]='active:'..tostring(id);state.active=tostring(id) end,
            LaunchSatellite=function(ship,macro) calls[#calls+1]='launch:'..macro end,
            IsComponentClass=function() return state.suit end,GetPlayerGalaxyID=function() return 'g' end,
            SetSofttarget=function(id) calls[#calls+1]='target:'..tostring(id) end,
            TransportPlayerToTarget=function() calls[#calls+1]='suit_out';state.suit=true end}
          gate={mpp_owner=function(id) for _,m in ipairs(missions) do if m.id==id then return m.owner end end
                                       for _,o in ipairs(offers) do if o.id==id then return o.owner end end return 0 end,
            pilot_stock_inject=function(kind,id,v) calls[#calls+1]=kind..':'..string.format('0x%x',id);return 1 end,
            pilot_stock_local=function(kind,id,v) calls[#calls+1]='local_'..kind..':'..string.format('0x%x',id);return 1 end,
            pilot_aim_script=function(mode) calls[#calls+1]='aim:'..mode;return 1 end}
          tools={env=env,C=C,gate=gate,own_slot=2,missions={offers=function() return offers end},
            menu=function() return nil end,slot_id=function(s) return 'ship'..s end,suit_target=function() return {} end}
          RC=X4LAN_RELEASE_CHECK
        ''')

    def test_look_lists_own_and_all_with_owners(self):
        text = self.L.eval('RC.look(tools)')
        self.assertIn('slot=2 active=11/2 ap=0 guidance=55 target=55 distance=4000 step=2/2', text)
        self.assertIn('listed=2[11/2,12/0] all=3[11/2,7/1,12/0] own=[Terran_Cadet]', text)
        self.assertIn('objective=Fly_to_the_satellite', text)
        self.assertLess(len(text), 420)                    # one X4 log line
        look = rc.parse_look(text)
        self.assertEqual(look['active'], ('11', '2'))
        self.assertEqual(look['all'], [('11', '2'), ('7', '1'), ('12', '0')])
        self.assertEqual(look['own'], ['Terran_Cadet'])
        self.assertEqual(look['distance'], 4000)

    def test_lua_ids_and_long_lists(self):
        self.L.execute("for i=1,30 do missions[#missions+1]={id='ID: '..(100+i),name='Job '..i,owner=0} end")
        text = self.L.eval('RC.look(tools)')
        self.assertIn('listed=32[11/2,12/0,101/0,', text)
        self.assertIn(',+]', text)
        self.assertLess(len(text), 420)
        self.assertEqual(self.L.eval("RC.decimal('ID: 1416125')"), '1416125')
        self.assertEqual(self.L.eval("RC.decimal('258362ULL')"), '258362')

    def test_offers_name_the_story_and_other_players(self):
        self.assertEqual(self.L.eval("RC.offers(tools,'Defenders')"), 'n=2 story=2[1/Defenders_of_Sol,2/Defenders_of_Sol] other=1')
        offers = rc.parse_offers(self.L.eval("RC.offers(tools)"))
        self.assertEqual(offers['story'], [('1', 'Defenders_of_Sol'), ('2', 'Defenders_of_Sol')])

    def test_accept_takes_only_this_players_offer_once(self):
        self.assertTrue(self.L.eval("RC.accept(tools,'Defenders')").startswith('accepted 91 '))
        self.assertEqual(self.L.eval("calls[1]"), 'signal:a91,accept,L91')
        self.assertEqual(self.L.eval("RC.accept(tools,'Defenders')"), 'accepted_before 91')
        self.assertIsNone(self.L.eval("RC.accept(tools,'Nothing')"))

    def test_drive_flies_deploys_and_repairs(self):
        L = self.L
        L.execute("state.active='7'")                                   # another mission is active: make the story's active
        self.assertIn('activate set 11', L.eval("RC.drive(tools,nil,100)"))
        self.assertIn('did=shift_a', L.eval("RC.drive(tools,nil,130)"))
        self.assertIn('local_action:0xb3', list(L.eval('calls').values()))     # Shift+A as this client's own key
        self.assertIn('did=fly', L.eval("RC.drive(tools,nil,160)"))      # no second key before the retry time
        L.execute("state.objective='Deploy a satellite'")
        self.assertIn('did=launch', L.eval("RC.drive(tools,nil,200)"))
        self.assertIn('did=wait_deploy', L.eval("RC.drive(tools,nil,230)"))
        L.execute("state.objective='Repair the satellite';state.distance=300")
        self.assertIn('did=get_up', L.eval("RC.drive(tools,nil,300)"))
        self.assertIn('did=wait_up', L.eval("RC.drive(tools,nil,330)"))
        self.assertIn('did=suit_out true', L.eval("RC.drive(tools,nil,400)"))
        self.assertIn('did=place', L.eval("RC.drive(tools,nil,430)"))
        self.assertIn('did=repairing', L.eval("RC.drive(tools,nil,460)"))
        self.assertIn('did=fire', L.eval("RC.drive(tools,nil,500)"))
        L.execute("state.distance=45")                                  # the suit drifted off
        self.assertIn('did=repairing', L.eval("RC.drive(tools,nil,520)"))    # not before 120 ticks after the placement
        self.assertIn('did=place_again', L.eval("RC.drive(tools,nil,560)"))
        L.execute("state.distance=12")
        L.execute("state.objective='Follow your wingmate'")
        self.assertIn('did=cease_fire', L.eval("RC.drive(tools,nil,530)"))
        self.assertIn('did=chapter_done', L.eval("RC.drive(tools,nil,560)"))
        calls = list(L.eval('calls').values())
        for expected in ('active:11', 'launch:eq_arg_satellite_01_macro', 'action:0x115', 'suit_out', 'target:55', 'aim:1', 'aim:0'):
            self.assertIn(expected, calls)
        self.assertEqual(calls.count('launch:eq_arg_satellite_01_macro'), 1)
        self.assertIn('signal:g,x4lan_goto,L55,12', calls)       # the suit 12 m from the satellite: the laser's reach

    def test_an_objective_the_autopilot_left_out_of_reach_is_reached_by_placement(self):
        L = self.L
        L.execute("state.objective='Check on your wingmate';state.distance=761")
        self.assertIn('did=fly', L.eval("RC.drive(tools,nil,100)"))
        self.assertIn('did=fly', L.eval("RC.drive(tools,nil,200)"))          # not yet RETRY ticks without progress
        self.assertIn('did=place_near', L.eval("RC.drive(tools,nil,340)"))
        self.assertIn('signal:g,x4lan_goto,L55,300', list(L.eval('calls').values()))
        L.execute("state.distance=300")
        self.assertIn('did=fly', L.eval("RC.drive(tools,nil,700)"))          # inside the step's reach: nothing more

    def test_the_left_ship_is_logged_from_the_get_up(self):
        L = self.L
        self.assertEqual(L.eval("RC.ship_state(tools)"), 'ship=none')
        L.execute("state.objective='Repair the satellite';state.distance=300")
        L.eval("RC.drive(tools,nil,300)")                             # the get-up: the ship the player sits in
        L.execute("C.GetNumOrders=function() return 0 end;C.IsFlightAssistActive=function() return true end")
        text = L.eval("RC.ship_state(tools)")
        self.assertTrue(text.startswith('ship=5ULL speed='), text)
        self.assertIn('pos=0.0,0.0,0.0', text)
        self.assertIn('orders=0', text)
        self.assertIn('fa=1 ap=0', text)
        self.assertNotIn(' ', text.split('ai=')[1].split(' orders=')[0])

    def test_the_story_not_an_upkeep_job(self):
        self.L.execute("table.insert(missions,1,{id='5',name='Acquire Captain',owner=2,kind='upkeep'})")
        self.assertEqual(self.L.eval("RC.own_mission(tools).id"), '11')

    def test_mission_manager_opens_the_maps_mission_mode(self):
        self.assertEqual(self.L.eval("RC.mm(tools,'open')"), 'opened')
        self.assertEqual(self.L.eval('calls[1]'), 'open:MapMenu:mission')


if __name__ == '__main__':
    unittest.main()

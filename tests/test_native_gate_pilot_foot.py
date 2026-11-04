"""Slot 1 on foot (lockstep_pilot_foot.h, native_gate_pilot_foot, stop reason 185, default off) and hiring a person who stands
on a station (the conversation family's hire scope around MapMenu.buttonHire); reports/LOCKSTEP-ON-FOOT-20261101.txt.

Covers the probe arming foot after stock piloting (two synthetic clients under LuaJIT, the real probe, a mocked gate) and its
refusals, per-client stock script lists (scripted walking for runs without people), the option checker and the overlay
builder; the hire scope under LuaJIT (slot 1's fee and conversation answer as this family's events in call order, the same
stock calls on every client at the apply; slots 2+ refused as a whole; the probe's, the simulation's and the apply's own calls
untouched), its script ops; and the mirrors (the guard's inventory and table hold GetUp, the fee's wire number in C and Lua,
the stop reason in the registry). The native half is tested in C: src/native/engine/test_lockstep_pilot_foot.h."""
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
sys.path.insert(0, str(ROOT/'tools/dev'))
import test_native_gate_pilot_stock as stock  # noqa: E402
import test_native_gate_ui_missions as missions  # noqa: E402
import test_native_gate_ui_replay as uirep  # noqa: E402
import ui_guard_inventory as inv  # noqa: E402

ENGINE = ROOT/'src/native/engine'
FOOT_H = (ENGINE/'lockstep_pilot_foot.h').read_text(encoding='utf-8')
MISSIONS_H = (ENGINE/'lockstep_ui_missions.h').read_text(encoding='utf-8')
TABLE_H = (ENGINE/'lockstep_ui_guard_table.h').read_text(encoding='utf-8')
REGISTRY = (ROOT/'docs/LOCKSTEP-REGISTRY.txt').read_text(encoding='utf-8')

PROBE_FOOT = missions.PROBE_MISSIONS + r'''
  gate.pilot_foot=function() calls[#calls+1]='foot';if FOOT_REFUSAL then return 0,FOOT_REFUSAL end;return 1 end
  gate.pilot_foot_bodies=function() calls[#calls+1]='bodies';if BODIES_REFUSAL then return 0,BODIES_REFUSAL end;return 1 end
  gate.pilot_foot_slots=function() calls[#calls+1]='foot_slots';if SLOTS_REFUSAL then return 0,SLOTS_REFUSAL end;return 1 end
'''
BODY_H = (ENGINE/'lockstep_pilot_foot_body.h').read_text(encoding='utf-8')
SLOTS_H = (ENGINE/'lockstep_pilot_foot_slots.h').read_text(encoding='utf-8')
PROBE = (ROOT/'tools/dev/native_gate_probe.lua').read_text(encoding='utf-8')


def foot_client(slot, prelude=PROBE_FOOT, **extra):
    cfg = dict(native_gate_pilot_present=True, native_gate_runtime_ids=True, native_gate_ui_replay=['dock', 'events', 'conversation'],
               native_gate_ui_guard='all', native_gate_pilot_foot=True)
    cfg.update(extra)
    return stock.StockClient(slot, slots_all=True, prelude=prelude, **{k: v for k, v in cfg.items()})


class ProbeTests(unittest.TestCase):
    def test_foot_arms_on_every_client_after_the_stock_slots_and_before_the_replay(self):
        clients = [foot_client(1), foot_client(2)]
        stock.run(clients, 12)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            calls = c.values('calls')
            self.assertLess(calls.index('stock:1/3'), calls.index('foot'))
            self.assertEqual(calls.count('foot'), 2)                 # armed before loading, again after the UI reload (identical rerun)
            self.assertTrue(any(f'PILOT_FOOT_ARMED slot={c.slot}' in line for line in c.values('logs')))
            self.assertIn('guard:16383', c.values('ui_calls'))     # the guard the claim needs is armed with the replay

    def test_refusals_name_themselves(self):
        for extra, prelude, why in (
                (dict(native_gate_pilot_foot='yes'), PROBE_FOOT, 'pilot foot refused: native_gate_pilot_foot must be true'),
                (dict(native_gate_ui_guard=None), PROBE_FOOT, 'pilot foot refused: needs native_gate_ui_replay and native_gate_ui_guard'),
                (dict(native_gate_ui_replay=None, native_gate_ui_guard=None), PROBE_FOOT,
                 'pilot foot refused: needs native_gate_ui_replay and native_gate_ui_guard'),
                ({}, PROBE_FOOT + "FOOT_REFUSAL='pins_foot:d7d3b0'", 'pilot foot refused: pins_foot:d7d3b0'),
                ({}, PROBE_FOOT + "FOOT_REFUSAL='foot_claim_taken'", 'pilot foot refused: foot_claim_taken'),
                ({}, PROBE_FOOT + 'gate.pilot_foot=nil', 'pilot foot refused: gate without pilot_foot')):
            with self.subTest(why):
                errors = foot_client(1, prelude=prelude, **extra).errors()
                self.assertTrue(any(why in line for line in errors), errors[-2:])

    def test_bodies_arm_after_foot_on_every_client(self):
        clients = [foot_client(1, native_gate_pilot_foot_bodies=True), foot_client(2, native_gate_pilot_foot_bodies=True)]
        stock.run(clients, 12)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            calls = c.values('calls')
            self.assertLess(calls.index('foot'), calls.index('bodies'))
            self.assertTrue(any(f'PILOT_FOOT_BODIES_ARMED slot={c.slot}' in line for line in c.values('logs')))
        self.assertNotIn('bodies', foot_client(1).values('calls'))

    def test_bodies_refusals_name_themselves(self):
        for extra, prelude, why in (
                (dict(native_gate_pilot_foot_bodies='yes'), PROBE_FOOT, 'pilot foot bodies refused: native_gate_pilot_foot_bodies must be true'),
                (dict(native_gate_pilot_foot_bodies=True), PROBE_FOOT + "BODIES_REFUSAL='bodies_filter'", 'pilot foot bodies refused: bodies_filter'),
                (dict(native_gate_pilot_foot_bodies=True), PROBE_FOOT + 'gate.pilot_foot_bodies=nil',
                 'pilot foot bodies refused: gate without pilot_foot_bodies')):
            with self.subTest(why):
                errors = foot_client(1, prelude=prelude, **extra).errors()
                self.assertTrue(any(why in line for line in errors), errors[-2:])

    def test_foot_slots_arm_after_foot_and_bodies_on_every_client(self):
        clients = [foot_client(k, native_gate_pilot_foot_bodies=True, native_gate_pilot_foot_slots=True) for k in (1, 2)]
        stock.run(clients, 12)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            calls = c.values('calls')
            self.assertLess(calls.index('foot'), calls.index('bodies'))
            self.assertLess(calls.index('bodies'), calls.index('foot_slots'))
            self.assertTrue(any(f'PILOT_FOOT_SLOTS_ARMED slot={c.slot}' in line for line in c.values('logs')))
        alone = [foot_client(k, native_gate_pilot_foot_slots=True) for k in (1, 2)]   # bodies are optional
        stock.run(alone, 12)
        for c in alone:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            self.assertIn('foot_slots', c.values('calls'))
            self.assertNotIn('bodies', c.values('calls'))
        self.assertNotIn('foot_slots', foot_client(1).values('calls'))

    def test_foot_slots_refusals_name_themselves(self):
        for extra, prelude, why in (
                (dict(native_gate_pilot_foot_slots='yes'), PROBE_FOOT, 'pilot foot slots refused: native_gate_pilot_foot_slots must be true'),
                (dict(native_gate_pilot_foot_slots=True), PROBE_FOOT + "SLOTS_REFUSAL='pins_foot_slots:d7f8d0'",
                 'pilot foot slots refused: pins_foot_slots:d7f8d0'),
                (dict(native_gate_pilot_foot_slots=True), PROBE_FOOT + 'gate.pilot_foot_slots=nil', 'pilot foot slots refused: gate without pilot_foot_slots')):
            with self.subTest(why):
                errors = foot_client(1, prelude=prelude, **extra).errors()
                self.assertTrue(any(why in line for line in errors), errors[-2:])

    def test_bodies_and_foot_slots_arm_with_death(self):
        # Everything on together (reports/LOCKSTEP-ON-FOOT-20261101.txt section 13): death arms first, then slot 1 on foot,
        # the bodies and slots 2..N on foot, on every client; no refusal names the death module. (The suit's and the
        # respawn's arming have their own tests in test_native_gate_pilot_stock.py: they need the ships' poses.)
        extra = dict(native_gate_pilot_death=True, native_gate_pilot_foot_bodies=True, native_gate_pilot_foot_slots=True)
        clients = [foot_client(k, **extra) for k in (1, 2)]
        stock.run(clients, 12)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
            calls = c.values('calls')
            self.assertLess(calls.index('death:0/0'), calls.index('foot'))
            self.assertLess(calls.index('foot'), calls.index('bodies'))
            self.assertLess(calls.index('bodies'), calls.index('foot_slots'))
            logs = c.values('logs')
            for armed in ('PILOT_DEATH_ARMED', 'PILOT_FOOT_ARMED', 'PILOT_FOOT_BODIES_ARMED', 'PILOT_FOOT_SLOTS_ARMED'):
                self.assertTrue(any(f'{armed} slot={c.slot}' in line for line in logs), armed)
        self.assertNotIn('not with native_gate_pilot_death', PROBE)

    def test_a_stock_script_per_client_gets_slot_2_up_and_walking(self):
        # Client 2 records slot 2's GET_UP and walking keys natively (lockstep_pilot_foot_slots.h); the probe injects them as
        # it does slot 1's. The live spec's shape: slot 1 up first, slot 2 a little later, here on short ticks.
        walk = [[[7, 'action', 0x115, 0], [9, 'state', 0x1a, 1], [11, 'state', 0x1a, 0]],
                [[8, 'action', 0x115, 0], [10, 'state', 0x1c, 1], [11, 'range', 7, 0.3], [12, 'state', 0x1c, 0]]]
        clients = [stock.StockClient(k, walk, slots_all=True) for k in (1, 2)]
        stock.run(clients, 14)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
        self.assertEqual(clients[0].values('injected'), ['7/action/277/0', '9/state/26/1', '11/state/26/0'])
        self.assertEqual(clients[1].values('injected'), ['8/action/277/0', '10/state/28/1', '11/range/7/0.3', '12/state/28/0'])

    def test_a_stop_logs_every_modules_why_first(self):
        # Live w1foot (stop 185 at tick 37) lost its foot why: the STOP and ABORT paths log gate.pilot_why() at once.
        for anchor in ("cfg.slot,tonumber(s.completed),s.reason,s.restored,s.wait_game_delta,s.max_dt_error))\n",
                       "' tick='..tostring(status.issued))\n"):
            at = PROBE.index(anchor) + len(anchor)
            self.assertIn("if gate.pilot_why then pcall(function() local why=gate.pilot_why();if why then log('PILOT_STOP_WHY '..why) end end) end",
                          PROBE[at:at + 320])

    def test_off_nothing_is_asked(self):
        c = foot_client(1, native_gate_pilot_foot=None)
        stock.run([c], 8)
        self.assertNotIn('foot', c.values('calls'))
        self.assertFalse(any('PILOT_FOOT' in line for line in c.values('logs')))

    def test_a_stock_script_per_client_walks_slot_1_and_leaves_slot_2_alone(self):
        # Client 1: get up, forward, mouselook, stop; client 2: nothing (its walking keys would be refused natively).
        walk = [[[7, 'action', 0x115, 0], [9, 'state', 0x1a, 1], [10, 'range', 7, 0.25], [12, 'state', 0x1a, 0]], False]
        clients = [stock.StockClient(k, walk, slots_all=True) for k in (1, 2)]
        stock.run(clients, 14)
        for c in clients:
            self.assertEqual(c.errors(), [], c.values('logs')[-3:])
        self.assertEqual(clients[0].values('injected'), ['7/action/277/0', '9/state/26/1', '10/range/7/0.25', '12/state/26/0'])
        self.assertEqual(clients[1].values('injected'), [])
        self.assertTrue(any('PILOT_STOCK_ARMED slot=1 capture=1 slots=3 script=list' in line for line in clients[0].values('logs')))
        self.assertTrue(any('PILOT_STOCK_ARMED slot=2 capture=1 slots=3 script=none' in line for line in clients[1].values('logs')))
        # One list for every capturing client stays as before.
        both = [stock.StockClient(k, [[8, 'state', 0x1a, 1]], slots_all=True) for k in (1, 2)]
        stock.run(both, 10)
        self.assertEqual([c.values('injected') for c in both], [['8/state/26/1'], ['8/state/26/1']])


class OptionTests(unittest.TestCase):
    def failures(self, text):
        return [f['rule'] for f in stock.options.probe_rules(text, set(re.findall(r'X4LAN_CONFIG\.(native_gate_\w+)\s*=\s*true\b', text)))]

    def test_rules(self):
        both = dict(native_gate_pilot_stock_slots="'all'", native_gate_pilot_capture_source="{'stock','stock'}",
                    native_gate_pilot_device='{false,false}', native_gate_pilot_pad=None)
        good = uirep.stock_config(**both, native_gate_pilot_foot='true', native_gate_ui_replay="{'dock'}", native_gate_ui_guard="'all'")
        self.assertEqual(self.failures(good), [])
        for rule, extra in (
                ('native_gate_pilot_foot must be true', dict(native_gate_pilot_foot="'yes'", native_gate_ui_replay="{'dock'}", native_gate_ui_guard="'all'")),
                ('native_gate_pilot_foot needs native_gate_pilot_stock', dict(native_gate_pilot_stock=None, native_gate_pilot_foot='true',
                                                                              native_gate_ui_replay="{'dock'}", native_gate_ui_guard="'all'")),
                ('native_gate_pilot_foot needs native_gate_ui_guard', dict(native_gate_pilot_foot='true', native_gate_ui_replay="{'dock'}")),
                ('native_gate_pilot_foot needs native_gate_ui_replay', dict(native_gate_pilot_foot='true', native_gate_ui_guard="'all'"))):
            with self.subTest(rule):
                self.assertIn(rule, self.failures(uirep.stock_config(**{**both, **extra})))
        self.assertEqual(self.failures(uirep.stock_config(**both, native_gate_pilot_foot='false')), [])
        foot = dict(native_gate_pilot_foot='true', native_gate_ui_replay="{'dock'}", native_gate_ui_guard="'all'")
        self.assertEqual(self.failures(uirep.stock_config(**both, **foot, native_gate_pilot_foot_bodies='true')), [])
        # With the death module (section 13): bodies and slots on foot are accepted together with it.
        death = dict(native_gate_pilot_death='true', native_gate_pilot_death_suit='true', native_gate_pilot_respawn='300',
                     native_gate_pilot_ship_state='true')
        self.assertEqual(self.failures(uirep.stock_config(**both, **foot, **death, native_gate_pilot_foot_bodies='true',
                                                          native_gate_pilot_foot_slots='true')), [])
        for rule, extra in (
                ('native_gate_pilot_foot_bodies must be true', dict(**foot, native_gate_pilot_foot_bodies="'yes'")),
                ('native_gate_pilot_foot_bodies needs native_gate_pilot_foot', dict(native_gate_pilot_foot_bodies='true'))):
            with self.subTest(rule):
                self.assertIn(rule, self.failures(uirep.stock_config(**{**both, **extra})))
        no_slots = {**both, 'native_gate_pilot_stock_slots': None}
        self.assertIn('native_gate_pilot_foot_bodies needs native_gate_pilot_stock_slots',
                      self.failures(uirep.stock_config(**no_slots, **foot, native_gate_pilot_foot_bodies='true')))
        self.assertEqual(self.failures(uirep.stock_config(**both, **foot, native_gate_pilot_foot_slots='true')), [])
        self.assertEqual(self.failures(uirep.stock_config(**both, **foot, native_gate_pilot_foot_bodies='true', native_gate_pilot_foot_slots='true')), [])
        for rule, extra in (
                ('native_gate_pilot_foot_slots must be true', dict(**foot, native_gate_pilot_foot_slots="'yes'")),
                ('native_gate_pilot_foot_slots needs native_gate_pilot_foot', dict(native_gate_pilot_foot_slots='true'))):
            with self.subTest(rule):
                self.assertIn(rule, self.failures(uirep.stock_config(**{**both, **extra})))
        self.assertIn('native_gate_pilot_foot_slots needs native_gate_pilot_stock_slots',
                      self.failures(uirep.stock_config(**no_slots, **foot, native_gate_pilot_foot_slots='true')))
        # Stock scripts: one event list per client is accepted.
        self.assertEqual(self.failures(uirep.stock_config(**both, native_gate_pilot_stock_script="{{{7,'action',277,0}},false}")), [])
        self.assertEqual(self.failures(uirep.stock_config(**both, native_gate_pilot_stock_script="{false,{{7,'state',26,1}}}")), [])


class BuilderTests(unittest.TestCase):
    ARGS = ['--players', '2', '--mode', 'coop', '--source', 'stock', '--present']

    def lines(self, *extra):
        args = uirep.players.defaults(uirep.players.build_parser().parse_args(['config', *self.ARGS, *extra]))
        return uirep.players.config_block(args)[0]

    def test_block(self):
        lines = self.lines('--ui-replay', 'dock,events,conversation', '--ui-guard', 'all', '--foot')
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_foot = true', lines)
        self.assertNotIn('X4LAN_CONFIG.native_gate_pilot_foot = true', self.lines('--ui-replay', 'dock', '--ui-guard', 'all'))
        for extra in (('--foot',), ('--ui-replay', 'dock', '--foot')):
            with self.subTest(extra), self.assertRaises(ValueError) as caught:
                self.lines(*extra)
            self.assertIn('--foot needs --source stock, --ui-replay and --ui-guard', str(caught.exception))
        bodies = self.lines('--ui-replay', 'dock,events,conversation', '--ui-guard', 'all', '--foot', '--foot-bodies')
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_foot_bodies = true', bodies)
        self.assertNotIn('X4LAN_CONFIG.native_gate_pilot_foot_bodies = true', lines)
        with self.assertRaises(ValueError) as caught:
            self.lines('--ui-replay', 'dock', '--ui-guard', 'all', '--foot-bodies')
        self.assertIn('--foot-bodies needs --foot', str(caught.exception))
        walk = self.lines('--ui-replay', 'dock,events,conversation', '--ui-guard', 'all', '--foot', '--foot-slots')
        self.assertIn('X4LAN_CONFIG.native_gate_pilot_foot_slots = true', walk)
        self.assertNotIn('X4LAN_CONFIG.native_gate_pilot_foot_bodies = true', walk)
        self.assertNotIn('X4LAN_CONFIG.native_gate_pilot_foot_slots = true', bodies)
        with self.assertRaises(ValueError) as caught:
            self.lines('--ui-replay', 'dock', '--ui-guard', 'all', '--foot-slots')
        self.assertIn('--foot-slots needs --foot', str(caught.exception))
        # Everything with the death module, its suit and respawn (section 13).
        everything = self.lines('--ui-replay', 'dock,events,conversation', '--ui-guard', 'all', '--foot', '--foot-bodies', '--foot-slots',
                                '--death', '--death-suit', '--respawn', '300')
        for line in ('X4LAN_CONFIG.native_gate_pilot_foot_bodies = true', 'X4LAN_CONFIG.native_gate_pilot_foot_slots = true',
                     'X4LAN_CONFIG.native_gate_pilot_death = true'):
            self.assertIn(line, everything)


HIRE = r'''
  local function show(v) return type(v)=='userdata' and 'id:'..tostring(ids[v]) or tostring(v) end
  env.ProceedFromMenu=function(section,param)
    local out={};for i,v in ipairs(param or {}) do out[i]=show(v) end
    stock_calls[#stock_calls+1]='ProceedFromMenu:'..tostring(section)..',{'..table.concat(out,'/')..'}' end
  env.TransferPlayerMoneyTo=function(amount,container) stock_calls[#stock_calls+1]='TransferPlayerMoneyTo:'..tostring(amount)..','..
    (type(container)=='userdata' and 'id:'..tostring(ids[container]) or tostring(container)) end
  -- MapMenu.buttonHire as the stock menu does it in mode 'hire' with an instanced person: pay, then answer the conversation.
  MapMenu={name='MapMenu',mode='hire',contextMenuData={}}
  function MapMenu.buttonHire()
    env.TransferPlayerMoneyTo(1500,make_id('300'))
    env.ProceedFromMenu('g_hire',{make_id('200'),'service',false})
    return 'hired'
  end
  MENUS={MapMenu}
'''


def hire_session(slot):
    lua, g = uirep.client()
    lua.execute(missions.MOCK + HIRE)
    lua.execute(f"missions_session({slot},{missions.MASK},{{menus=function() return MENUS end}})")
    return lua, g


class HireScopeTests(unittest.TestCase):
    def test_slot_1_pays_and_hires_in_call_order_on_every_client(self):
        calls = []
        for _ in (1, 2):
            lua, g = hire_session(1)
            lua.execute("S.frame(40)")                          # the frame wraps the menu's Hire button once it exists
            self.assertTrue(any('UI_MISSIONS_HIRE_SCOPE menu=MapMenu function=buttonHire' in l for l in uirep.values(g.logs)))
            lua.execute("LOCAL=1;S.tick=41;answer=MapMenu.buttonHire()")
            self.assertEqual(g.answer, 'hired')
            self.assertEqual(uirep.values(g.stock_calls), [])    # nothing ran here
            pushed = uirep.values(g.pushed)
            self.assertEqual([p[1] for p in pushed], [0x49, 0x49])
            self.assertEqual([p[2][:2] for p in pushed], ['10', '03'])   # the fee (16), then ProceedFromMenu (3)
            logs = uirep.values(g.logs)
            self.assertTrue(any('what=TransferPlayerMoneyTo via=hire' in l for l in logs))
            self.assertTrue(any('what=ProceedFromMenu via=lua' in l for l in logs))
            # Outside the scope the fee is no longer captured as this family's: the stock global is back.
            lua.execute("LOCAL=1;env.TransferPlayerMoneyTo(5,make_id('300'))")
            self.assertEqual(uirep.values(g.stock_calls), ['TransferPlayerMoneyTo:5,id:300'])
            lua.execute("stock_calls={}")
            due = ';'.join('1:73:' + p[2] for p in pushed)
            lua.execute(f"DUE={{[44]='{due}'}};S.apply(44)")
            calls.append(uirep.values(g.stock_calls))
            self.assertEqual(uirep.values(g.results), ['44:1', '44:1'])
        self.assertEqual(calls[0], calls[1])
        self.assertEqual(calls[0], ['TransferPlayerMoneyTo:1500,id:300', 'ProceedFromMenu:g_hire,{id:200/service/false}'])

    def test_slots_2_and_up_are_refused_as_a_whole(self):
        lua, g = hire_session(2)
        lua.execute("S.frame(40);LOCAL=1;S.tick=41;answer=MapMenu.buttonHire()")
        self.assertIsNone(g.answer)
        self.assertEqual(uirep.values(g.stock_calls), [])     # no fee, no answer: nothing paid without the hire
        self.assertEqual(uirep.values(g.pushed), [])
        self.assertIn('UI_REPLAY_REFUSED what=MapMenu.buttonHire why=slot', uirep.values(g.logs))
        self.assertEqual(uirep.values(g.notices), ['41:Multiplayer: MapMenu.buttonHire works for player 1 only for now - nothing happened.'])
        # A section of slot 2 holding the fee stops.
        lua.execute("DUE={[50]='2:73:10'};S.apply(50)")
        self.assertEqual(g.FAILED, 'ui_lua_op')

    def test_the_probe_the_simulation_and_the_apply_run_the_stock_button(self):
        lua, g = hire_session(2)
        lua.execute("S.frame(40);LOCAL=0;answer=MapMenu.buttonHire()")
        self.assertEqual(g.answer, 'hired')
        self.assertEqual(uirep.values(g.stock_calls), ['TransferPlayerMoneyTo:1500,id:300', 'ProceedFromMenu:g_hire,{id:200/service/false}'])

    def test_wrapped_once_and_off_without_the_family(self):
        lua, g = hire_session(1)
        lua.execute("S.frame(40);first=MapMenu.buttonHire;S.frame(41);same=first==MapMenu.buttonHire")
        self.assertTrue(g.same)
        lua, g = uirep.client()
        lua.execute(missions.MOCK + HIRE + "before=MapMenu.buttonHire;missions_session(1,256+2048,{menus=function() return MENUS end});S.frame(40);"
                    "same=before==MapMenu.buttonHire")
        self.assertTrue(g.same)

    def test_a_fee_that_does_not_encode_is_refused(self):
        lua, g = uirep.client()
        lua.execute(missions.MOCK + HIRE + "function MapMenu.buttonHire() env.TransferPlayerMoneyTo(10,make_id('777'));return 'hired' end")
        lua.execute(f"missions_session(1,{missions.MASK},{{menus=function() return MENUS end}})")
        lua.execute("S.frame(40);LOCAL=1;S.tick=42;answer=MapMenu.buttonHire()")
        self.assertIn('UI_REPLAY_REFUSED what=TransferPlayerMoneyTo why=unmapped', uirep.values(g.logs))
        self.assertEqual(uirep.values(g.pushed), [])
        self.assertEqual(uirep.values(g.stock_calls), [])
        self.assertEqual(g.answer, 'hired')


class ScriptTests(unittest.TestCase):
    def test_hire_talk_and_confirm_call_the_stock_entries_as_a_person_does(self):
        lua, g = hire_session(1)
        lua.execute(r'''
          env.GetNPCs=function(id) assert(tostring(id)=='9ULL');return {make_id('100'),make_id('200')} end
          env.GetNPCBlackboard=function(npc,key) assert(key=='$HiringFee');return ids[npc]=='200' and 1234 or nil end
          env.Menus=MENUS
          local tools={env=env,C=C,gate=gate,saved_id=function(s) return tonumber(s)+0ULL end,slot_id=function(k) return 1000+k end,
            saved_lua=function(s) return make_id(s) end}
          plan=X4LAN_UI_REPLAY_CORE.script({{1,'hire_talk','9'},{2,'hire_confirm',1,'service',false}},40,tools)
          S.frame(40)
          LOCAL=1
          talk=plan[41][1].call()
          -- The hire context's discounts (createHireContext, h1h): read from the person's container as the stock does.
          MapMenu.modeparam={'g_hire',make_id('200'),1}
          env.ConvertIDTo64Bit=function(v) return ids[v] end
          env.GetContextByClass=function(id,class,self) assert(id=='200' and class=='container' and self==true);return 'c300' end
          env.GetComponentData=function(id,key) assert(id=='c300' and key=='hiringdiscounts');return {{amount=10},{amount=5}} end
          confirm=plan[42][1].call()
          factor=MapMenu.hiringdiscounts.totalfactor
          target=MapMenu.contextMenuData.hireObject;role=MapMenu.contextMenuData.hireRole;post=MapMenu.contextMenuData.hireIsPost
          MapMenu.mode='map';ok,missing=pcall(plan[42][1].call)
          env.GetNPCBlackboard=function() return nil end;ok2,nobody=pcall(plan[41][1].call)''')
        self.assertTrue(g.talk.endswith(' fee=1234'), g.talk)
        self.assertEqual((g.confirm, g.target, g.role, g.post), ('hire service', 1001, 'service', False))
        self.assertAlmostEqual(g.factor, 0.85)
        pushed = uirep.values(g.pushed)
        self.assertEqual([p[2][:2] for p in pushed], ['01', '10', '03'])   # StartConversationFromMenu, the fee, ProceedFromMenu
        self.assertFalse(g.ok)
        self.assertIn('no hire menu open', str(g.missing))
        self.assertFalse(g.ok2)
        self.assertIn('nobody to hire on 9', str(g.nobody))


    def test_where_reads_a_saved_objects_position_and_captures_nothing(self):
        lua, g = hire_session(1)
        lua.execute(r'''
          C.GetObjectPositionInSector=function(id) assert(tostring(id)=='112047ULL');return {x=1000.4,y=-5,z=20000,yaw=math.pi/2} end
          local tools={env=env,C=C,gate=gate,saved_id=function(s) return tonumber(s,16)+0ULL end}
          plan=X4LAN_UI_REPLAY_CORE.script({{3,'where','000000000001b5af'}},40,tools)
          LOCAL=1;at=plan[43][1].call()''')
        self.assertEqual(g.at, '1000,-5,20000/90')
        self.assertEqual(uirep.values(g.pushed), [])


class MirrorTests(unittest.TestCase):
    def test_get_up_is_guarded_in_the_flight_group(self):
        self.assertEqual(inv.INVENTORY[('ffi', 'GetUp')][:2], ('ffi', 'flight'))
        self.assertRegex(TABLE_H, r'\{"GetUp",0x1aba30u,UIGUARD_FFI,UIGUARD_G_FLIGHT,0u,')

    def test_the_fee_number_agrees(self):
        native = int(re.search(r'#define UIMIS_C_HIRE_MONEY (\d+)u', MISSIONS_H).group(1))
        lua = int(re.search(r'K\.HIRE_MONEY=(\d+)', uirep.CORE).group(1))
        self.assertEqual(native, lua)
        names = re.search(r'K\.CONVERSATION=\{(.*?)\}', uirep.CORE, re.S).group(1)
        self.assertGreater(native, len(re.findall(r"'(\w+)'", names)))   # above the conversation globals' numbers

    def test_the_stop_reason_and_the_claim_are_registered(self):
        self.assertIn('#define PFOOT_STOP_REASON 185u', FOOT_H)
        self.assertRegex(REGISTRY, r'\n  185 PFOOT_STOP_REASON +lockstep_pilot_foot\.h')
        self.assertIn('foot          GetUp (native_gate_pilot_foot', REGISTRY)
        self.assertRegex(REGISTRY, r'(?s)185 PFOOT_STOP_REASON +lockstep_pilot_foot\.h.*lockstep_pilot_foot_body\.h')
        self.assertIn('native_gate_pilot_foot_bodies', REGISTRY)
        self.assertIn('pfbody_fail', BODY_H)
        self.assertIn('pfoot_failed=1', BODY_H.split('static int pfbody_fail', 1)[1].split('}', 1)[0])   # body whys stop with 185
        # Slots 2..N on foot: its whys are 185's, every one registered.
        self.assertIn('pfoot_failed=1', SLOTS_H.split('static int pfslot_fail', 1)[1].split('}', 1)[0])
        self.assertRegex(REGISTRY, r'(?s)185 PFOOT_STOP_REASON .*lockstep_pilot_foot_slots\.h \(native_gate_pilot_foot_slots')
        whys = set(re.findall(r'pfslot_(?:fail|async)\("(slot_\w+)"\)', SLOTS_H))
        self.assertGreaterEqual(len(whys), 20)
        section = REGISTRY.split('lockstep_pilot_foot_slots.h (native_gate_pilot_foot_slots', 1)[1].split('190 ADAPT_STOP_PLAN', 1)[0]
        self.assertEqual(sorted(w for w in whys if w not in section), [])
        self.assertIn('with native_gate_pilot_foot_slots also of slot k on client k', REGISTRY)

    def test_the_bodies_park_around_every_get_up(self):
        # Live footlook: bodies + slot 1's get-up ended both X4s; every body leaves the tree in the pass of any get-up.
        native = (ENGINE/'lockstep_pilot_native.h').read_text(encoding='utf-8')
        after_replay = native.split('if(!pstk_frame(tick,&why)) return pnat_fail(why);', 1)[1]
        self.assertLess(after_replay.index('pfbody_guard(tick)'), after_replay.index('pslot_replay(tick)'))
        self.assertIn('pfoot_state!=PFOOT_SEATED', BODY_H)
        self.assertIn('pfslot_getup_due(tick)', BODY_H)

    def test_the_double_tap_clock_is_the_sessions_in_the_replay(self):
        # Live df2b: slot 1's first forward press parted the walking bank's 0x38f4e70 (each process's real time) and |W2.1 from
        # its apply tick. The replay runs X4's walking state handler with the time lock held and the session's clock as real time.
        stock = (ENGINE/'lockstep_pilot_stock.h').read_text(encoding='utf-8')
        walk = re.search(r'static void pstk_replay_walk\(.*?\n\}', stock, re.S).group(0)
        self.assertIn('pfoot_replay_clock(kind,n->handler)) pfoot_replay_state(n->handler,id,(unsigned char)value)', walk)
        body = re.search(r'static void pfoot_replay_state\(.*?\n\}', FOOT_H, re.S).group(0)
        self.assertLess(body.index('pnat_fn.time(&now)'), body.index('EnterCriticalSection(lock)'))
        self.assertLess(body.index('EnterCriticalSection(lock)'), body.index('__try'))
        self.assertIn('__finally {memcpy(pnat_base+PFOOT_REAL_TIME,&real,8);if(lock) LeaveCriticalSection(lock);}', body)
        self.assertIn('pfoot_fail_async("foot_clock")', body)
        self.assertIn('#define PFOOT_TIME_LOCK 0x6b60e10u', FOOT_H)
        self.assertIn('#define PFOOT_REAL_TIME 0x6b60e40u', FOOT_H)
        self.assertIn('pfoot_time_lock=(CRITICAL_SECTION*)(image+PFOOT_TIME_LOCK);', FOOT_H)
        self.assertIn('{0xe9f90u,0x39,', FOOT_H)   # the clock d7d3b0 calls is pinned
        self.assertIn('foot_clock', REGISTRY)

    def test_the_walking_ids_are_the_options_menus_first_person_controls(self):
        # gameoptions.lua's 'firstperson' controls (ranges 15 16 13 14, states 26 27 32 33 87 30 31 28 29 34 35 36, action 220):
        # every one of them is a walking id of the module (lockstep_pilot_foot.h pfoot_id).
        body = re.search(r'static unsigned pfoot_id\(unsigned kind,int id\) \{(.*?)\n\}', FOOT_H, re.S).group(1)
        self.assertIn('(id>=0x1a && id<=0x24) || id==0x57', body)
        self.assertIn('id>=0xd && id<=0x10', body)
        self.assertIn('id==0xdc', body)
        for state in (26, 27, 32, 33, 87, 30, 31, 28, 29, 34, 35, 36):
            self.assertTrue(0x1a <= state <= 0x24 or state == 0x57, state)
        for rng in (15, 16, 13, 14):
            self.assertTrue(0xd <= rng <= 0x10, rng)


if __name__ == '__main__':
    unittest.main()

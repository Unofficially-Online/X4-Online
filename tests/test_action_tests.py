"""Action tests (tools/dev/action_tests.py) and the script ops they use (native_gate_ui_replay_core.lua: chain, cargo,
wallet, trade_look).

Synthetic: the Lua runs under LuaJIT with a mocked world, the report reads made-up log rows. What the real game does
with a scenario is shown by a live run of `action_tests.py run`, not here."""
import re
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
sys.path.insert(0, str(ROOT/'tools/dev'))
import test_native_gate_ui_replay as uirep  # noqa: E402
import action_tests  # noqa: E402
import station_walk_test as rig  # noqa: E402

# A world for the chain: the ship docks at the third dock_state asked, cargo is what CARGO holds, the trade list has
# one sell and one buy offer for ore, AddTradeToShipQueue moves the cargo.
WORLD = r'''
  CARGO={};ASKED=0;DOCKED=false;calls={}
  tools={gate=gate,C={},env={},own_slot=2,dock_range=9000,
    slot_id=function(s) return 'ship'..s end,slot_lua=function(s) return 'ship'..s end,
    saved_id=function(s) return 'id'..s end,saved_lua=function(s) return 'lua'..s end,
    dock_state=function() ASKED=ASKED+1;if ASKED>=3 then DOCKED=true end;return {docked=DOCKED,distance=5000} end,
    assigned_dock=function() return nil end,
    foot_where=function() return 'docked='..tostring(DOCKED) end}
  tools.C.RequestDockAt=function() return false end
  tools.C.UndockPlayerShip=function() DOCKED=false;return ffi.new('char[8]','ok') end
  tools.C.GetPlayerID=function() return 7 end
  tools.env.StartAutoPilot=function(x) calls[#calls+1]='autopilot:'..tostring(x) end
  tools.env.GetComponentData=function(ship,key) calls[#calls+1]='data:'..tostring(ship)..':'..key;return CARGO end
  tools.env.GetPlayerMoney=function() return 123 end
  tools.env.GetTradeList=function(station) return {{id='o1',ware='ore',isselloffer=true,price=50,amount=900},{id='o2',ware='ore',isbuyoffer=true,price=40,amount=10},
    {id='o3',ware='ice',isselloffer=true,price=1,amount=1}} end
  tools.env.AddTradeToShipQueue=function(offer,ship,amount,immediate) calls[#calls+1]='trade:'..offer..':'..tostring(ship)..':'..amount..':'..tostring(immediate)
    CARGO.ore=(CARGO.ore or 0)+(offer=='o1' and amount or -amount);if CARGO.ore==0 then CARGO.ore=nil end end
  tools.env.SignalObject=function(who,what,ship) calls[#calls+1]='signal:'..what..':'..tostring(ship) end
  tools.env.AddUITriggeredEvent=function(screen,control,value) calls[#calls+1]=screen..':'..control..':'..tostring(value.credits) end
  function run(plan,ticks)
    local by=X4LAN_UI_REPLAY_CORE.script(plan,0,tools)
    local rows={}
    for t=0,ticks do
      for _,e in ipairs(by[t] or {}) do
        local ok,result=pcall(e.call)
        if not (e.quiet and ok and result==nil) then rows[#rows+1]=t..' '..tostring(e.op)..' '..tostring(ok)..' '..tostring(result) end
      end
    end
    return rows
  end
'''


def values(table):
    return list(table.values()) if table is not None else []


class ChainTests(unittest.TestCase):
    def client(self):
        lua, g = uirep.client()
        lua.execute('ffi=require("ffi")')
        lua.execute(WORLD)
        return lua, g

    def test_steps_run_one_after_the_other_each_when_the_last_has_ended(self):
        lua, g = self.client()
        lua.execute('''rows=run({{5,'chain',400,{
          {'dock','until','^docked','dock_station','aa',300},
          {'rest','wait',3},
          {'look','op','trade_look','aa','ore'},
          {'buy','op','trade_buy','aa','ore',20,2,1},
          {'bought','poll','^amount=20 ',2,40,'cargo','ore'},
          {'undock','op','undock'},
          {'away','poll','docked=false',5,50,'foot_where'},
          {'sell','op','trade_sell','aa','ore',20,2,1},
          {'sold','poll','^amount=0 ',2,40,'cargo','ore'}}}},420)''')
        rows = values(g.rows)
        text = '\n'.join(rows)
        self.assertIn('5 chain true steps=9', rows[0])
        self.assertIn('step=dock n=1 at=1 ok=true autopilot', text)
        self.assertIn('step=dock n=1 at=4 ok=true docked after=3', text)          # the third dock_state: docked
        self.assertIn('step=look n=3 at=9 ok=true offers=2 sell=50/900 buy=40/10 money=123', text)
        self.assertIn('step=bought n=5 at=11 ok=true amount=20 ware=ore wares=1 ore=20 money=123', text)
        self.assertIn('step=sold n=9 ', text)
        self.assertIn('amount=0 ware=ore wares=0 money=123', text)
        self.assertTrue(rows[-1].endswith('chain_end ok=true steps=9/9'), rows[-1])
        calls = values(g.calls)
        self.assertIn('trade:o1:ship2:20:true', calls)                               # the station's sell offer, immediate
        self.assertIn('trade:o2:ship2:20:true', calls)
        self.assertEqual(calls.count('signal:docked_player_trade_added:ship2'), 2)
        self.assertLess(calls.index('trade:o1:ship2:20:true'), calls.index('trade:o2:ship2:20:true'))
        # ...and the report's reader takes the rows apart
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp)/'c.log'
            log.write_text('\n'.join('[X4LAN] NATIVE_GATE UI_REPLAY_SCRIPT tick=%s op=%s ok=%s result=%s scope=0' % tuple(r.split(' ', 3)) for r in rows))
            c = action_tests.parse(log)
        self.assertEqual(c['end'], {'ok': True, 'done': 9, 'of': 9, 'why': None, 'tick': c['end']['tick']})
        self.assertEqual(action_tests.last_answer(c, 'dock', 'docked')['text'], 'docked after=3 asked=0')
        self.assertEqual(rig.field(action_tests.last_answer(c, 'bought')['text'], 'amount'), '20')

    def test_a_dock_approach_that_stalls_detours_to_the_docks_face(self):
        lua, g = self.client()
        world = '''DS={docked=false};GAP=3735;LEFT=5000;TOG=0;AP=0
          tools.dock_state=function() return {docked=DS.docked,distance=2400} end
          tools.C.RequestDockAt=function() return true end
          tools.assigned_dock=function() return 'dock1' end
          tools.dock_gap=function() return GAP end
          tools.dock_neighbour=function() calls[#calls+1]='detour';LEFT=1000;return 'ship9','via=9 via_dock=80 docked=3' end
          tools.detour_left=function() LEFT=LEFT-200;return LEFT end
          tools.env.StartAutoPilot=function(x) calls[#calls+1]='autopilot:'..tostring(x);if x=='ship9' then TOG=TOG+1 end;if x=='dock1' and TOG>0 then DS.docked=true end end
          '''
        lua.execute(world + "rows=run({{0,'dock_station','aa',2000}},2000)")
        text = '\n'.join(values(g.rows))
        self.assertIn('31 dock_station true granted after=31', text)
        self.assertIn('36 dock_station true approach dock=dock1 distance=2400 dock_gap=3735', text)
        # the second approach brought the ship no nearer: the autopilot to the ship docked next to the dock, then the
        # approach from beside it
        self.assertIn('636 dock_station true detour n=1 via=9 via_dock=80 docked=3 distance=2400 dock_gap=3735', text)
        # (beside it: 400 m off was still on the far side of the wharf's module, 20 Nov)
        self.assertNotIn('detour_end n=1 left=200 ', text)
        self.assertIn('641 dock_station true detour_end n=1 left=0 after=5 dock_gap=3735', text)
        self.assertIn('642 dock_station true approach dock=dock1 distance=2400 dock_gap=3735', text)
        self.assertIn('643 dock_station true docked after=643 asked=1', text)
        calls = values(g.calls)
        self.assertEqual([c for c in calls if c.startswith('detour')], ['detour'])
        self.assertLess(calls.index('autopilot:ship9'), len(calls) - 1 - calls[::-1].index('autopilot:dock1'))
        # a detour that stands still for ten seconds ends there: the approach from where it got to
        lua.execute('calls={}\n' + world + "tools.detour_left=function() return 500 end\nrows=run({{0,'dock_station','aa',1300}},1300)")
        text = '\n'.join(values(g.rows))
        self.assertIn('1236 dock_station true detour_end n=1 left=500 after=600 dock_gap=3735', text)
        self.assertIn('1237 dock_station true approach dock=dock1 distance=2400 dock_gap=3735', text)
        # stalled within a kilometre of the pad: no detour (nobody to fly beside that is nearer), the approach again
        lua.execute('calls={}\n' + world + "GAP=294\nrows=run({{0,'dock_station','aa',1300}},1300)")
        text = '\n'.join(values(g.rows))
        self.assertIn('636 dock_station true approach dock=dock1 distance=2400 dock_gap=294', text)
        self.assertNotIn('detour', text)
        # a detour of 0 metres: none, the approach goes on as before (the stranded ship of 17-19 Nov)
        lua.execute('calls={}\n' + world + "rows=run({{0,'dock_station','aa',1300,false,false,0}},1300)")
        text = '\n'.join(values(g.rows))
        self.assertIn('636 dock_station true approach dock=dock1 distance=2400 dock_gap=3735', text)
        self.assertNotIn('detour', text)
        self.assertIn('1300 dock_station true limit stage=granted distance=2400 asked=1 dock_gap=3735', text)

    def test_the_ai_dock_stops_the_autopilot_in_range_and_signals_the_galaxy_until_docked(self):
        # ai_dock_station (core K.ai_dock_fly): the autopilot to the station; within the request's range it is stopped
        # and the galaxy signalled (MD AiDock: X4's dock_player_ship_at); no dock assigned for 600 ticks: asked again
        lua, g = self.client()
        lua.execute('''N=0;DOCK=nil;SIGNALS=0
          tools.dock_state=function() N=N+1;return {docked=N>=1500,distance=N<100 and 20000 or 5000} end
          tools.assigned_dock=function() return DOCK end
          tools.galaxy_lua=function() return 'galaxy' end
          tools.env.StopAutoPilot=function() calls[#calls+1]='stop' end
          tools.env.SignalObject=function(who,what,x) SIGNALS=SIGNALS+1;calls[#calls+1]='signal:'..tostring(who)..':'..what..':'..tostring(x)
            if SIGNALS>=2 then DOCK='dock1' end end
          rows=run({{0,'ai_dock_station','aa',3000}},3000)''')
        text = '\n'.join(values(g.rows))
        self.assertIn('0 ai_dock_station true autopilot', text)
        self.assertIn('120 ai_dock_station true autopilot_stop distance=5000', text)      # in range at the next 30th tick
        self.assertIn('135 ai_dock_station true ai_dock asked=1 distance=5000', text)     # K.AI_DOCK_SETTLE later
        self.assertIn('735 ai_dock_station true ai_dock_again dock=nil distance=5000', text)
        self.assertIn('736 ai_dock_station true ai_dock asked=2 distance=5000', text)
        self.assertIn('1036 ai_dock_station true stage=ai dock=dock1 distance=5000 asked=2', text)
        self.assertIn('1500 ai_dock_station true docked after=1500 asked=2 by=ai', text)
        self.assertNotIn('1501 ai_dock_station', text)
        self.assertEqual(values(g.calls), ['autopilot:luaaa', 'stop', 'signal:galaxy:x4lan_ai_dock:luaaa', 'signal:galaxy:x4lan_ai_dock:luaaa'])
        # the op alone: one signal
        lua.execute("calls={};rows=run({{0,'ai_dock','aa'}},2)")
        self.assertEqual(values(g.rows), ['0 ai_dock true signalled'])
        self.assertEqual(values(g.calls), ['signal:galaxy:x4lan_ai_dock:luaaa'])

    def test_the_guidance_ops_plot_remove_and_read_this_clients_course(self):
        lua, g = self.client()
        lua.execute('''TARGET=nil;AP=false
          ConvertStringTo64Bit=ConvertStringTo64Bit or function(s) return s end
          tools.C.SetGuidance=function(id,offset) calls[#calls+1]='set:'..tostring(id)..':'..offset.x..'/'..offset.yaw;TARGET='st'..tostring(id) end
          tools.C.EndGuidance=function() calls[#calls+1]='end';TARGET=nil end
          tools.C.IsAutoPilotActive=function() return AP end
          tools.env.GetActiveGuidanceMissionComponent=function() return TARGET end
          tools.env.GetComponentData=function(c,key) return key=='idcode' and 'NLN-075' or nil end
          tools.dock_state=function() return {docked=false,distance=12345.6} end
          rows=run({{0,'guidance_look','aa'},{1,'guidance_set','aa'},{2,'guidance_look','aa'},{3,'guidance_end'},{4,'guidance_look'}},5)''')
        rows = values(g.rows)
        self.assertEqual(rows, ['0 guidance_look true guidance=none distance=12345 autopilot=false', '1 guidance_set true set',
                                '2 guidance_look true guidance=NLN-075/stidaa distance=12345 autopilot=false', '3 guidance_end true ffi',
                                '4 guidance_look true guidance=none autopilot=false'])
        self.assertEqual(values(g.calls), ['set:idaa:0/0', 'end'])

    def test_the_ai_dock_option_swaps_every_chains_dock_station_step(self):
        plan = [[(30, 'dock_kit', 1), (60, 'chain', 100, [('dock', 'until', '^docked', 'dock_station', 'aa', 9000, False, False, True, 0),
                                                          ('rest', 'wait', 3), ('dock2', 'until', '^docked', 'dock_station', 'bb')])]]
        self.assertEqual(action_tests.ai_docks(plan), [[(30, 'dock_kit', 1), (60, 'chain', 100, [
            ('dock', 'until', '^docked', 'ai_dock_station', 'aa', 9000), ('rest', 'wait', 3), ('dock2', 'until', '^docked', 'ai_dock_station', 'bb', 3000)])]])
        import contextlib
        import io
        for name in action_tests.SCENARIOS:
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                action_tests.main(['plan', name, '--ai-dock'])
            self.assertNotRegex(out.getvalue(), r"'dock_station'", name)

    def test_a_point_of_one_frame_is_mapped_into_a_rotated_frame(self):
        lua, g = self.client()
        lua.execute('''
          local function rot(v)    -- a rotation: 30 degrees about y, then 50 about x
            local c1,s1,c2,s2=math.cos(math.rad(30)),math.sin(math.rad(30)),math.cos(math.rad(50)),math.sin(math.rad(50))
            local x,y,z=v[1]*c1+v[3]*s1,v[2],-v[1]*s1+v[3]*c1
            return {x,y*c2-z*s2,y*s2+z*c2}
          end
          local a,b,v={17,-212,-224},{-127,-209,2822},{300,-40,-600}
          local out=X4LAN_UI_REPLAY_CORE.frame_map(a,b,rot(a),rot(b),v)
          local want=rot(v)
          MAPPED_ERROR=math.sqrt((out[1]-want[1])^2+(out[2]-want[2])^2+(out[3]-want[3])^2)
        ''')
        self.assertLess(lua.eval('MAPPED_ERROR'), 1e-6)

    def test_a_stalled_approach_near_the_dock_hops_over_by_the_flight_keys(self):
        lua, g = self.client()
        # the dock's frame is the ship's (no rotation); a ship with inertia, stepped once a tick (dock_state is read
        # first in every step): a held strafe key adds 2 m/s a tick, without one the speed falls by a tenth; the
        # forward speed follows the throttle (key ticks counted, 1 m/s each); the approach docks the ship once it has
        # hopped to the dock's far side
        world = '''DS={docked=false};POS={-17,212,224};VEL={0,0,0};HELD={};APS=0;THROTTLE=0
          gate.pilot_stock_inject=function(kind,id,on) HELD[id]=on;calls[#calls+1]='key:'..id..'='..on end
          tools.dock_state=function()
            if HELD[13]==1 then THROTTLE=THROTTLE+1 end;if HELD[14]==1 then THROTTLE=THROTTLE-1 end
            local acc={((HELD[7]==1) and 2 or 0)-((HELD[6]==1) and 2 or 0),((HELD[8]==1) and 2 or 0)-((HELD[9]==1) and 2 or 0)}
            for k=1,2 do if acc[k]~=0 then VEL[k]=VEL[k]+acc[k] else VEL[k]=VEL[k]*0.9 end end
            VEL[3]=VEL[3]+(THROTTLE-VEL[3])*0.2
            for k=1,3 do POS[k]=POS[k]+VEL[k]/30 end
            return {docked=DS.docked,distance=2800}
          end
          tools.C.RequestDockAt=function() return true end
          tools.assigned_dock=function() return 'dock1' end
          tools.dock_gap=function() return 300 end
          tools.dock_rel=function() return string.format('%.0f/%.0f/%.0f centre=0/0/3000',POS[1],POS[2],POS[3]) end
          tools.dock_frames=function()
            local p,c={POS[1],POS[2],POS[3]},{0,0,3000}
            return p,c,{-p[1],-p[2],-p[3]},{c[1]-p[1],c[2]-p[2],c[3]-p[3]}
          end
          tools.env.StopAutoPilot=function() calls[#calls+1]='stop' end
          tools.env.StartAutoPilot=function(x) calls[#calls+1]='autopilot:'..tostring(x);APS=APS+1;if POS[3]<0 then DS.docked=true end end
          '''
        lua.execute(world + "rows=run({{0,'dock_station','aa',5000}},5000)")
        text = '\n'.join(values(g.rows))
        self.assertRegex(text, r'\n\d+ dock_station true hop n=1 check=none dock_seen=17/-212/-224 points=2 to=0/500/-150 dock_gap=300')
        self.assertRegex(text, r'dock_station true hop_point n=1 point=2 ')
        self.assertRegex(text, r'dock_station true hop_end n=1 ')
        self.assertRegex(text, r'dock_station true docked after=\d+ asked=1')
        self.assertNotIn('detour', text)
        self.assertNotIn('hop_stop', text)
        # it arrived without swinging past: never more than 60 m beyond the climb's height
        heights = [float(m.group(1)) for m in re.finditer(r'rel=[-\d]+/(-?\d+)/', text)]
        self.assertTrue(all(h < 560 for h in heights), heights)
        calls = values(g.calls)
        self.assertIn('stop', calls)
        self.assertLess(calls.index('stop'), calls.index('key:8=1'))                 # up first
        self.assertEqual(lua.eval('THROTTLE'), 0)                                     # the throttle back at zero
        self.assertTrue(all(lua.eval('HELD[%d]' % k) in (0, None) for k in (6, 7, 8, 9, 13, 14)))
        # a route given with the op: its points, 'here' the ship's own value; the last one reached within 12 m, then
        # three seconds, then the dock key (here it docks the ship), no approach autopilot after the route
        route = "{{'here',400,'here'},{-50,400,-60}}"
        lua.execute('calls={}\n' + world + "tools.C.QuickDock=function() calls[#calls+1]='quickdock';DS.docked=true;return true end\n"
                    "rows=run({{0,'dock_station','aa',5000,false,false,true,%s}},5000)" % route)
        text = '\n'.join(values(g.rows))
        self.assertRegex(text, r'hop n=1 check=none dock_seen=17/-212/-224 points=2 to=-50/400/-60 ')
        end = re.search(r'\n(\d+) dock_station true hop_end n=1 .*rel=(-?\d+)/(-?\d+)/(-?\d+)', text)
        self.assertLessEqual(abs(int(end.group(2)) + 50) + abs(int(end.group(3)) - 400) + abs(int(end.group(4)) + 60), 21, end.group(0))
        self.assertRegex(text, r'\n\d+ dock_station true quickdock=true dock=dock1 ')
        self.assertRegex(text, r'dock_station true docked after=\d+ asked=1')
        calls = values(g.calls)
        self.assertEqual(calls[calls.index('quickdock'):].count('autopilot:dock1'), 0)
        # ...and when the dock key does not dock it: the approach as before, and no second hop on a route
        lua.execute('calls={}\n' + world + "tools.C.QuickDock=function() calls[#calls+1]='quickdock';return false end\n"
                    "rows=run({{0,'dock_station','aa',5000,false,false,true,%s}},5000)" % route)
        text = '\n'.join(values(g.rows))
        self.assertRegex(text, r'\n\d+ dock_station true settle_end dock=dock1 ')
        self.assertEqual(text.count(' hop n='), 1)
        # no hop: the approach as before
        lua.execute('calls={}\n' + world + "rows=run({{0,'dock_station','aa',1300,false,false,true,0}},1300)")
        text = '\n'.join(values(g.rows))
        self.assertNotIn(' hop', text)
        self.assertIn('1300 dock_station true limit stage=granted distance=2800 asked=1 dock_gap=300', text)

    def test_a_route_from_x4s_landing_pose_waits_for_it_and_holds_where_it_ends(self):
        # dock_station's route of points from where X4's landing check wants the ship ({'approach',dx,dy,dz}: the MD
        # answer of the op dock_approach, tools.dock_approach[own slot]); hold=true: no dock key, no approach after it
        lua, g = self.client()
        world = '''DS={docked=false};POS={64,43,-271};VEL={0,0,0};HELD={};THROTTLE=0
          gate.pilot_stock_inject=function(kind,id,on) HELD[id]=on end
          tools.dock_state=function()
            if HELD[13]==1 then THROTTLE=THROTTLE+1 end;if HELD[14]==1 then THROTTLE=THROTTLE-1 end
            local acc={((HELD[7]==1) and 2 or 0)-((HELD[6]==1) and 2 or 0),((HELD[8]==1) and 2 or 0)-((HELD[9]==1) and 2 or 0)}
            for k=1,2 do if acc[k]~=0 then VEL[k]=VEL[k]+acc[k] else VEL[k]=VEL[k]*0.9 end end
            VEL[3]=VEL[3]+(THROTTLE-VEL[3])*0.2
            for k=1,3 do POS[k]=POS[k]+VEL[k]/30 end
            return {docked=DS.docked,distance=550}
          end
          tools.C.RequestDockAt=function() return true end
          tools.C.QuickDock=function() calls[#calls+1]='quickdock';return true end
          tools.assigned_dock=function() return 'dock1' end
          tools.dock_gap=function() return 280 end
          tools.dock_rel=function() return string.format('%.0f/%.0f/%.0f centre=393/-73/188',POS[1],POS[2],POS[3]) end
          tools.dock_frames=function()
            local p,c={POS[1],POS[2],POS[3]},{393,-73,188}
            return p,c,{-p[1],-p[2],-p[3]},{c[1]-p[1],c[2]-p[2],c[3]-p[3]}
          end
          tools.env.StopAutoPilot=function() calls[#calls+1]='stop' end
          tools.env.StartAutoPilot=function(x) calls[#calls+1]='autopilot:'..tostring(x) end
          tools.dock_approach={}
          '''
        route = "{{'approach',0,30,-200},{'approach',0,0,0},hold=true}"
        # not known yet: the approach goes on and the hop waits
        lua.execute(world + "rows=run({{0,'dock_station','aa',1900,false,false,false,%s}},1900)" % route)
        text = '\n'.join(values(g.rows))
        self.assertIn('dock_station true hop_wait why=no_approach dock_gap=280', text)
        self.assertNotIn(' hop n=', text)
        # known (slot 2's own answer): the route to 34 m over the pad, then the ship stays; no dock key, no approach
        lua.execute('calls={}\n' + world + "tools.dock_approach[2]={pos={4,34,2},rot={0,0,0},here={64,43,-271}}\n"
                    "rows=run({{0,'dock_station','aa',5000,false,false,false,%s}},5000)" % route)
        text = '\n'.join(values(g.rows))
        self.assertRegex(text, r'hop n=1 check=none dock_seen=-64/-43/271 points=2 to=4/34/2 ')
        self.assertRegex(text, r'\n\d+ dock_station true hold dock_gap=280 rel=')
        self.assertRegex(text, r'dock_station true stage=hold after=60 ')
        end = re.search(r'\n(\d+) dock_station true hop_end n=1 .*rel=(-?\d+)/(-?\d+)/(-?\d+)', text)
        self.assertLessEqual(abs(int(end.group(2)) - 4) + abs(int(end.group(3)) - 34) + abs(int(end.group(4)) - 2), 21, end.group(0))
        calls = values(g.calls)
        self.assertNotIn('quickdock', calls)
        self.assertEqual(calls[calls.index('stop'):].count('autopilot:dock1'), 0)
        self.assertEqual(lua.eval('THROTTLE'), 0)
        # {hold=true,after=N} without points: from step N the autopilot is stopped once and never started again
        lua.execute('calls={}\n' + world + "rows=run({{0,'dock_station','aa',3000,false,false,false,{hold=true,after=1500}}},3000)")
        text = '\n'.join(values(g.rows))
        self.assertIn('1500 dock_station true hold after=1500 dock_gap=280', text)
        self.assertNotIn(' hop', text)
        calls = values(g.calls)
        self.assertEqual(calls.count('stop'), 1)
        self.assertEqual(calls[calls.index('stop'):].count('autopilot:dock1'), 0)
        self.assertIn('3000 dock_station true limit stage=hold', text)

    def test_the_landing_pose_and_the_warp_to_the_dock_are_canonical_ui_events_for_any_slot(self):
        # dock_approach / dock_warp (MD DockApproach / DockWarp): test fixtures in every client's plan, for slot 2, 3 or 8
        lua, g = self.client()
        lua.execute('''tools.env.AddUITriggeredEvent=function(screen,control,v)
            calls[#calls+1]=screen..':'..control..':'..tostring(v.ship)..':'..tostring(v.slot)..':'..tostring(v.up)..':'..tostring(v.ahead) end
          by=X4LAN_UI_REPLAY_CORE.script({{0,'dock_approach',2},{1,'dock_warp',3},{2,'dock_warp',8,20,-10}},0,tools)
          CANON={}
          for t=0,2 do for _,e in ipairs(by[t]) do CANON[#CANON+1]=tostring(e.canonical);e.call() end end''')
        self.assertEqual(values(g.calls), ['X4LAN_UiReplay:DockApproach:ship2:2:nil:nil', 'X4LAN_UiReplay:DockWarp:ship3:3:30:0',
                                           'X4LAN_UiReplay:DockWarp:ship8:8:20:-10'])
        self.assertEqual(values(g.CANON), ['true', 'true', 'true'])
        md = (ROOT/'tools/dev/native_gate_ui_replay.xml').read_text(encoding='utf-8')
        for cue, row in (('DockApproach', 'UI_REPLAY_DOCK_APPROACH'), ('DockWarp', 'UI_REPLAY_DOCK_WARP')):
            self.assertIn('<cue name="%s" instantiate="true">' % cue, md)
            self.assertIn("control=\"'%s'\"" % cue, md)
            self.assertIn(row, md)
        self.assertIn('<get_docking_approach_pos dock="$Dock" ship="$Ship" position="$Pos" rotation="$Rot"/>', md)
        self.assertIn("<raise_lua_event name=\"'X4LAN_UiReplay.DockApproach'\"", md)

    def test_a_step_that_never_answers_ends_the_chain_and_says_which(self):
        lua, g = self.client()
        lua.execute('''rows=run({{0,'chain',100,{{'a','op','cargo','ore'},{'never','poll','^amount=5 ',2,10,'cargo','ore'},{'b','op','cargo'}}}},120)''')
        rows = values(g.rows)
        self.assertTrue(rows[-1].endswith('chain_end ok=false steps=1/3 why=never:never'), rows[-1])
        self.assertFalse(any('step=b ' in r for r in rows))
        # ...unless the step is a `try`: then the miss is a row and the chain goes on
        lua.execute('''rows=run({{0,'chain',100,{{'never','try','^amount=5 ',2,10,'cargo','ore'},{'b','op','cargo'}}}},120)''')
        text = '\n'.join(values(g.rows))
        self.assertIn('step=never n=1 at=11 ok=true gave_up', text)
        self.assertIn('step=b n=2 ', text)
        self.assertTrue(values(g.rows)[-1].endswith('chain_end ok=true steps=2/2'), values(g.rows)[-1])
        lua.execute('''rows=run({{0,'chain',6,{{'long','wait',50},{'b','op','cargo'}}}},120)''')
        self.assertTrue(values(g.rows)[-1].endswith('chain_end ok=false steps=0/2 why=long:limit'), values(g.rows)[-1])
        lua.execute('''tools.env.GetComponentData=function() error('boom') end
          rows=run({{0,'chain',20,{{'bad','op','cargo'},{'b','op','cargo'}}}},30)''')
        self.assertIn('step=bad n=1 at=1 ok=false', values(g.rows)[-1])
        self.assertTrue(values(g.rows)[-1].endswith('chain_end ok=false steps=0/2 why=bad:error'), values(g.rows)[-1])

    def test_the_menu_ops_press_the_stock_handlers_and_meet_the_block(self):
        lua, g = self.client()
        lua.execute('''
          pressed={}
          MAP={name='MapMenu',mode='tradecontext',contextMenuMode='trade',contextMenuData={component=300,currentShip=200,immediate=true,orders={}}}
          MAP.orderAmountHelper=function(sell,buy,value) if value<0 then MAP.contextMenuData.orders[sell]=value else MAP.contextMenuData.orders[buy]=value end end
          MAP.buttonConfirmTrade=function() pressed[#pressed+1]='confirm' end
          DOCK={name='DockedMenu',shown=true,buttonTrade=function(exchange) pressed[#pressed+1]='trade:'..tostring(exchange) end,
            buttonUndock=function() return 0 end}          -- as the order block's refusal answers
          tools.menu=function(name) return ({MapMenu=MAP,DockedMenu=DOCK})[name] end
          tools.menu_entry=function(menu,name) return name=='buttonConfirmTrade' and 'blocked' or 'open why=shared' end
          tools.env.ConvertStringToLuaID=function(s) return 'lua'..s end
          rows=run({{0,'menu_entry','MapMenu','buttonConfirmTrade'},{1,'menu_look','DockedMenu'},{2,'menu_call','DockedMenu','buttonTrade',false},
            {3,'menu_look','MapMenu'},{4,'trade_menu_amount','ore',20},{5,'menu_look','MapMenu'},{6,'menu_call','MapMenu','buttonConfirmTrade'},
            {7,'trade_menu_amount','ore',-5},{8,'menu_look','MapMenu'},{9,'menu_call','DockedMenu','buttonUndock'},{10,'menu_call','NoMenu','x'},
            {11,'menu_call','MapMenu','nothing'},{12,'trade_menu_amount','ice',-1}},13)''')
        rows = values(g.rows)
        self.assertEqual(rows[0], '0 menu_entry true blocked entry=MapMenu.buttonConfirmTrade')
        self.assertEqual(rows[1], '1 menu_look true shown=true mode=nil context=nil')
        self.assertEqual(rows[2], '2 menu_call true called')
        self.assertEqual(rows[3], '3 menu_look true shown=false mode=tradecontext context=trade ship=200 immediate=true amounts=-')
        self.assertEqual(rows[5], '5 menu_look true shown=false mode=tradecontext context=trade ship=200 immediate=true amounts=-20')
        self.assertEqual(rows[6], '6 menu_call true called')
        self.assertEqual(rows[8], '8 menu_look true shown=false mode=tradecontext context=trade ship=200 immediate=true amounts=-20,5')
        self.assertEqual(rows[9], '9 menu_call true blocked answer=0')
        self.assertEqual(rows[10:], ['10 menu_call true no_menu', '11 menu_call true no_function', '12 trade_menu_amount true no_buy_offer'])
        self.assertEqual(values(g.pressed), ['trade:false', 'confirm'])
        # the trade menu: by the dock menu's button while that menu is on screen, else opened as the button opens it
        lua.execute('''opened={}
          tools.env.OpenMenu=function(name,param) opened[#opened+1]=name..':'..tostring(param[6])..':'..tostring(param[7][1]) end
          rows=run({{0,'trade_menu_open','aa'}},1);DOCK.shown=false;more=run({{0,'trade_menu_open','aa'}},1)''')
        self.assertEqual(values(g.rows), ['0 trade_menu_open true via=dock_menu'])
        self.assertEqual(values(g.more), ['0 trade_menu_open true via=open_menu dock_menu=false'])
        self.assertEqual(values(g.opened), ['MapMenu:tradecontext:luaaa'])
        self.assertEqual(values(g.pressed)[-1], 'trade:false')
        self.assertEqual(lua.eval("MAP.contextMenuData.orders.o1"), -20)          # the station's sell offer: the ship buys
        self.assertEqual(lua.eval("MAP.contextMenuData.orders.o2"), 5)

    def test_the_top_level_bar_and_back_ops_press_the_helpers_routes(self):
        """interact: a click on the HUD's top level bar is Helper.scrollTopLevelInternal on the open menu with the entry's
        index (when the bar offers it); Back is Helper.closeMenu 'back'; the dock menu's reading names its ship and whether
        that is this client's slot's (own_slot 2)."""
        lua, g = self.client()
        lua.execute('''
          pressed={}
          TOP={name='TopLevelMenu',shown=true}
          MAP={name='MapMenu',shown=true,param2={'DockedMenu',{0,0},{'restore'}},cleanup=function() pressed[#pressed+1]='map_cleanup' end}
          DOCK={name='DockedMenu',shown=true,mode='cockpit',currentplayership=1002ULL}
          tools.menu=function(name) return ({TopLevelMenu=TOP,MapMenu=MAP,DockedMenu=DOCK})[name] end
          gate.pilot_ship=function(s) return 1000+s end
          tools.env.Helper={topLevelMenus={{id='options',menu='OptionsMenu'},{id='docked',menu='DockedMenu',needsdock=true},{id='cockpit',menu='DockedMenu',needsdock=false}},
            checkTopLevelConditions=function(entry) return not entry.needsdock end,
            scrollTopLevelInternal=function(menu,index,offset,noreturn) pressed[#pressed+1]=menu.name..':'..index..':'..tostring(offset)..':'..tostring(noreturn) end,
            closeMenu=function(menu,due) pressed[#pressed+1]='close:'..menu.name..':'..due end}
          rows=run({{0,'toplevel_tab','cockpit'},{1,'toplevel_tab','cockpit','MapMenu',true},{2,'toplevel_tab','docked'},{3,'toplevel_tab','nothing'},
            {4,'menu_look','DockedMenu'},{5,'menu_back','MapMenu'}},6)
          TOP.shown=false;DOCK.currentplayership=1001ULL
          more=run({{0,'toplevel_tab','cockpit'},{1,'menu_look','DockedMenu'}},2)''')
        self.assertEqual(values(g.rows), ['0 toplevel_tab true pressed opens=DockedMenu', '1 toplevel_tab true pressed opens=DockedMenu',
                                          '2 toplevel_tab true not_offered', '3 toplevel_tab true no_entry',
                                          '4 menu_look true shown=true mode=cockpit context=nil ship=1002 own=true',
                                          '5 menu_back true back to=DockedMenu'])
        self.assertEqual(values(g.pressed), ['TopLevelMenu:3:nil:nil', 'MapMenu:3:nil:true', 'close:MapMenu:back', 'map_cleanup'])
        self.assertEqual(values(g.more), ['0 toplevel_tab true not_shown', '1 menu_look true shown=true mode=cockpit context=nil ship=1001 own=false'])
        self.assertTrue(lua.eval('X4LAN_UI_REPLAY_CORE.SCOPED.toplevel_tab and X4LAN_UI_REPLAY_CORE.SCOPED.menu_back'))

    def test_the_ship_ops_make_the_crew_and_order_calls_of_the_menus(self):
        lua, g = self.client()
        lua.execute('''
          ffi.cdef('typedef uint64_t NPCSeed; typedef uint64_t UniverseID; typedef struct { UniverseID entity; UniverseID personcontrollable; NPCSeed personseed; } GenericActor;')
          made={}
          PEOPLE={{seed=11ULL,role='service'},{seed=12ULL,role='marine'}}
          tools.ship_id=function(ref) return 500ULL end
          tools.ship_people=function(ref,role) local out={};for _,x in ipairs(PEOPLE) do if not role or x.role==role then out[#out+1]=x end end;return out end
          tools.ship_look=function(ref) return 'state=ok ship='..tostring(ref) end
          tools.C.RemovePerson=function(ship,seed) made[#made+1]='remove:'..tostring(ship)..':'..tostring(seed) end
          tools.C.AssignHiredActor=function(actor,ship,post,role,check) made[#made+1]='assign:'..tostring(actor.personseed)..':'..tostring(ship)..':'..post..':'..tostring(check);return ffi.new('char[4]','') end
          tools.C.CreateOrder=function(id,def,default) made[#made+1]='order:'..tostring(id)..':'..def..':'..tostring(default);return 3 end
          tools.C.EnableOrder=function(id,idx) made[#made+1]='enable:'..idx end
          tools.C.ResetOrderLoop=function(id) made[#made+1]='resetloop' end
          tools.C.EnablePlannedDefaultOrder=function(id,check) made[#made+1]='planned:'..tostring(check);return true end
          tools.C.GetObjectPositionInSector=function(id) return {x=100,y=0,z=-50} end
          tools.env.SetOrderParam=function(ship,idx,param,_,value) made[#made+1]='param:'..tostring(idx)..':'..param..':'..tostring(value) end
          tools.env.CreateOrder=function(ship,def,params) made[#made+1]='lua:'..tostring(ship)..':'..def;return 4 end
          tools.env.GetComponentData=function(id,key) return key=='assignedpilot' and 'pilot77' or nil end
          tools.env.ConvertIDTo64Bit=function(x) return 77ULL end
          tools.env.ConvertStringTo64Bit=function(s) return s end
          tools.env.ConvertStringToLuaID=function(s) return 'lua'..s end
          tools.env.SignalObject=function(who,what) made[#made+1]='signal:'..tostring(who)..':'..what end
          rows=run({{0,'ship_look','aa'},{1,'crew_fire','aa','marine'},{2,'crew_promote','aa'},{3,'captain_fire','aa'},{4,'explore','aa','s1'},
            {5,'q_patrol','aa','s1'},{6,'behaviour','aa','Patrol','s1'},{7,'move_by','aa','s1',1000,0,500},{8,'ship_arrived','aa',2000},
            {9,'crew_fire','aa','pilot'},{10,'behaviour_confirm','aa'}},11)''')
        rows = values(g.rows)
        self.assertEqual(rows[0], '0 ship_look true state=ok ship=aa')
        self.assertEqual(rows[1], '1 crew_fire true fired=12 role=marine')
        self.assertEqual(rows[2], '2 crew_promote true promoted=11 role=service answer=-')
        self.assertEqual(rows[3], '3 captain_fire true dismissed=77')
        self.assertEqual(rows[5], '5 q_patrol true 3')
        self.assertEqual(rows[6], '6 behaviour true 3')
        self.assertEqual(rows[10], '10 behaviour_confirm true true')
        self.assertEqual(rows[7], '7 move_by true order=4 from=100/0/-50 to=1100/0/450')
        self.assertEqual(rows[8], '8 ship_arrived true near=true distance=1118')
        self.assertEqual(rows[9], '9 crew_fire true nobody')
        made = values(g.made)
        for call in ('remove:500ULL:12ULL', 'assign:11ULL:500ULL:aipilot:false', 'signal:77ULL:npc__control_dismissed', 'lua:luaaa:Explore',
                     'order:idaa:Patrol:false', 'param:3:1:luas1', 'enable:3', 'order:idaa:Patrol:true', 'param:planneddefault:1:luas1', 'resetloop',
                     'planned:false', 'lua:luaaa:MoveWait'):
            self.assertIn(call, made)

    def test_the_fleet_ops_make_the_wing_sale_and_transfer_calls_of_the_menus(self):
        lua, g = self.client()
        lua.execute('''
          ffi.cdef('typedef uint64_t NPCSeed; typedef uint64_t UniverseID; typedef struct { const char* reason; NPCSeed npc; NPCSeed partnernpc; } UICrewExchangeResult;')
          made={}
          OWNER='player'
          tools.ship_id=function(ref) return 'id'..tostring(ref) end
          tools.ship_people=function(ref,role) return {{seed=11ULL,role='service'}} end
          tools.ship_look=function(ref) return OWNER and 'state=ok ship=5 owner='..OWNER..' pilot=7' or 'state=gone' end
          tools.deploy_look=function(ref) return 'deployed=0 holds=satellites:1 ref='..tostring(ref) end
          HELD=11
          tools.deploy_held=function(ref,kind) return HELD end
          tools.C.IsOrderSelectableFor=function(def,id) return true end
          tools.C.ResetOrderLoop=function(id) made[#made+1]='resetloop:'..tostring(id) end
          tools.C.CreateOrder=function(id,def,default) made[#made+1]='order:'..tostring(id)..':'..def..':'..tostring(default);return 2 end
          tools.C.EnableOrder=function(id,idx) made[#made+1]='enable:'..idx end
          tools.C.AdjustOrder=function(id,idx,to) made[#made+1]='adjust:'..idx..':'..to;return true end
          tools.C.RemoveCommander2=function(id) made[#made+1]='uncommand:'..tostring(id);return true end
          tools.C.EnablePlannedDefaultOrder=function(id,check) made[#made+1]='planned:'..tostring(check);return true end
          tools.C.SellPlayerShip=function(ship,yard) made[#made+1]='sell:'..tostring(ship)..':'..tostring(yard) end
          tools.C.CanContainerBuildShip=function(yard,ship) return true end
          tools.C.GetContextByClass=function(ship) return 'idyard' end
          tools.C.PerformCrewExchange2=function(from,to,npcs,n,back,m,c1,c2,swap,check)
            made[#made+1]='exchange:'..tostring(from)..':'..tostring(to)..':'..tostring(npcs[0])..':'..n..':'..m..':'..tostring(check)
            return {reason=ffi.new('char[4]','')} end
          tools.C.DropCargo=function(ship,ware,amount) made[#made+1]='drop:'..tostring(ship)..':'..ware..':'..amount;return true end
          tools.C.LaunchSatellite=function(ship,macro) made[#made+1]='launch:'..tostring(ship)..':'..macro end
          tools.env.SetOrderParam=function(ship,idx,param,_,value) made[#made+1]='param:'..tostring(idx)..':'..param..':'..tostring(value) end
          tools.env.GetComponentData=function(id,...) if select(1,...)=='assignedpilot' then return 'pilot77' end;return true,true end
          tools.env.GetTotalValue=function(ship,sell,yard) return 81234 end
          tools.env.TransferMoneyToPlayer=function(amount,from,why) made[#made+1]='money:'..amount..':'..tostring(from)..':'..tostring(why) end
          tools.env.SetComponentName=function(ship,name) made[#made+1]='name:'..tostring(ship)..':'..name end
          tools.env.AddUITriggeredEvent=function(screen,control,value) made[#made+1]=screen..':'..control..':'..tostring(value.ware)..':'..tostring(value.amount) end
          rows=run({{0,'assign','aa','bb','attack',2},{1,'unassign','aa'},{2,'ship_value','aa','yard'},{3,'ship_owned','aa'},{4,'ship_owned','aa'},
            {5,'crew_move','aa','bb','service',true},{6,'crew_move','aa','bb'},{7,'ship_name',2,'New Name'},{8,'deploy_look',2},{9,'hold_stock',2,'ore',50},
            {10,'launch','satellite','sat_macro'},{11,'drop_cargo','ore',20}},12)
          OWNER='argon';more=run({{0,'ship_owned','aa'}},1);OWNER=nil;gone=run({{0,'ship_owned','aa'}},1)
          counts=run({{0,'deploy_count','Satellites'}},1);HELD=10;for _,row in ipairs(run({{0,'deploy_count','Satellites'},{1,'deploy_count','Mines'}},2)) do counts[#counts+1]=row end''')
        # a ship that holds eleven satellites has launched one when it holds ten (the first asking is the count before)
        self.assertEqual(values(g.counts), ['0 deploy_count true left=11 launched=0 kind=Satellites', '0 deploy_count true left=10 launched=1 kind=Satellites',
                                            '1 deploy_count true left=10 launched=0 kind=Mines'])
        rows = values(g.rows)
        self.assertEqual(rows[0], '0 assign true order=2')
        self.assertEqual(rows[1], '1 unassign true removed=true wait=2 enabled=true')
        self.assertEqual(rows[2], '2 ship_value true price=81234 takes=true sellable=true docked=true')
        self.assertEqual(rows[4], '4 ship_owned true mine=true')
        self.assertEqual(rows[5], '5 crew_move true check=11 role=service reason=-')
        self.assertEqual(rows[6], '6 crew_move true moved=11 role=service reason=-')
        self.assertEqual(rows[7], '7 ship_name true named')
        self.assertEqual(rows[8], '8 deploy_look true deployed=0 holds=satellites:1 ref=2')
        self.assertEqual(values(g.more), ['0 ship_owned true mine=false owner=argon'])
        self.assertEqual(values(g.gone), ['0 ship_owned true mine=false state=gone'])
        made = values(g.made)
        # "Assign as subordinate": the loop's reset, the order, its seven parameters, the enable, the move to the front
        at = made.index('resetloop:idaa')
        self.assertEqual(made[at:at + 11], ['resetloop:idaa', 'order:idaa:AssignCommander:false', 'param:2:1:luabb', 'param:2:2:attack', 'param:2:3:2',
                                            'param:2:4:true', 'param:2:5:true', 'param:2:6:true', 'param:2:8:true', 'enable:2', 'adjust:2:1'])
        # "Remove assignment", the sale (the money first, as the menu), the transfer's question and its confirm
        for call in ('uncommand:idaa', 'order:idaa:Wait:true', 'planned:false', 'exchange:idaa:idbb:11ULL:1:0:true',
                     'exchange:idaa:idbb:11ULL:1:0:false', 'name:ship2:New Name', 'X4LAN_UiReplay:Hold:ore:50', 'launch:ship2:sat_macro', 'drop:ship2:ore:20'):
            self.assertIn(call, made)
        # the sale is the build family's: one commit scope with the money first, as the map's "Sell ships" confirm
        build = (ROOT/'tools/dev/native_gate_ui_build_core.lua').read_text(encoding='utf-8')
        sale = build[build.index("elseif op=='sell' then"):build.index("elseif op=='plot' then")]
        self.assertIn("S.with_scope('script.sell'", sale)
        self.assertLess(sale.index("TransferMoneyToPlayer(price,station,'sellship')"), sale.index('tools.C.SellPlayerShip(ship,station)'))
        self.assertIn("'buttonSellShips'", build[build.index('K.SCOPES={'):build.index('K.CONSTRUCTION=')])
        self.assertTrue(lua.eval('X4LAN_UI_REPLAY_CORE.CANONICAL.hold_stock'))
        self.assertTrue(lua.eval('X4LAN_UI_REPLAY_CORE.CANONICAL.hull_set'))
        cue = (ROOT/'tools/dev/native_gate_ui_replay.xml').read_text(encoding='utf-8')
        self.assertIn('control="\'Hold\'"', cue)
        # an order with its parameters, as the interact menu's order entries make it
        lua.execute('''made={}
          tools.env.CreateOrder=function(ship,def,params,...) made[#made+1]={ship=ship,def=def,params=params,n=select('#',...),last=select(select('#',...),...)};return 5 end
          tools.env.GetComponentData=function() return 'pilot' end
          tools.C.RemoveAllOrders2=function(id) made[#made+1]={cleared=id} end
          tools.C.GetObjectPositionInSector=function(id) return {x=100,y=200,z=300} end
          tools.deployed=function(macro,n) return 'sat'..n end
          rows=run({{0,'order','aa','Follow',{{'target',{'id','bb'}}}},
            {1,'order','aa','CollectDropsInRadius',{{'destination',{'at','s1',2,0,50,0}}},true},
            {2,'order','aa','CollectDeployables',{{'targetlist',{'list',{'deployed','sat_macro',2}}}}},
            {3,'order','aa','Flee',{{'method','boost'},{'donotdrop',true}}},{4,'order','aa','ProtectStation',{{'station',{'pos','s1',1,2,3}}}}},5)
          tools.C.IsOrderSelectableFor=function() return false end;more=run({{0,'order','aa','Follow',{}}},1)
          tools.C.GetObjectPositionInSector=function(id) return id=='idaa' and {x=0,y=0,z=0} or {x=300,y=400,z=0} end
          gap=run({{0,'ship_gap','aa','bb'}},1)''')
        self.assertEqual(values(g.gap), ['0 ship_gap true gap=500'])
        self.assertTrue(lua.eval("('gap=1999'):find('^gap=1?%d?%d?%d$')~=nil and ('gap=2000'):find('^gap=1?%d?%d?%d$')==nil and ('gap=7'):find('^gap=1?%d?%d?%d$')~=nil"))
        self.assertEqual(values(g.rows), ['%d order true order=5' % i for i in range(5)])
        self.assertEqual(values(g.more), ['0 order true not_selectable'])
        self.assertEqual(lua.eval("made[1].ship..':'..made[1].def..':'..made[1].params.target..':'..made[1].n..':'..tostring(made[1].last)"), 'luaaa:Follow:luabb:7:true')
        self.assertEqual(lua.eval("made[2].cleared"), 'idaa')          # CLEAR: the queue's removal first
        self.assertEqual(lua.eval("made[3].params.destination[1]..':'..table.concat(made[3].params.destination[2],',')"), 'luas1:100,250,300')
        self.assertEqual(lua.eval("made[4].params.targetlist[1]"), 'sat2')
        self.assertEqual(lua.eval("made[5].params.method..':'..tostring(made[5].params.donotdrop)"), 'boost:true')
        self.assertEqual(lua.eval("made[6].params.station[1]..':'..table.concat(made[6].params.station[2],',')"), 'luas1:1,2,3')
        # the faction's rules as the menus list them
        lua.execute('''tools.rules=function() return {{kind='blacklists',id='3',name='No_Xenon',default=true},{kind='traderules',id='7',name='Allies',default=false}} end
          rows=run({{0,'rules_has','No_Xenon'},{1,'rules_has','Nobody'}},2)''')
        self.assertEqual(values(g.rows), ['0 rules_has true has=true kind=blacklists id=3', '1 rules_has true has=false'])
        # a fleet ship's hold by its saved ID
        lua.execute('''tools.env.GetComponentData=function(ship,key) return ship=='luaaa' and {ore=7} or {} end
          rows=run({{0,'cargo','ore','aa'},{1,'cargo','ore'}},2)''')
        self.assertEqual(values(g.rows), ['0 cargo true amount=7 ware=ore wares=1 ore=7 money=123', '1 cargo true amount=0 ware=ore wares=0 money=123'])

    def test_the_system_storage_claim_and_mining_ops_make_the_calls_of_the_menus(self):
        lua, g = self.client()
        lua.execute('''
          ffi.cdef('typedef uint64_t UniverseID;')
          made={}
          tools.ship_id=function(ref) return 'id'..tostring(ref) end
          SYSTEMS={id='idaa',weapons={{id='w1',groups=''},{id='w2',groups='p1'}},turrets={{id='t1',mode='defend',armed=true}},
            groups={{context='c1',path='p',group='g1',mode='defend',armed=true,operational=2}},
            drones={{kind='defence',stored=4,modes={'defend','attackenemies'},mode='defend',at=1,armed=false}}}
          tools.systems=function(ref) return SYSTEMS end
          tools.systems_look=function(ref) return 'active=1/1 weapons=2:-,p1 ref='..tostring(ref) end
          tools.pilot_look=function() return 'assist=true activity=none ' end
          tools.settings_look=function() return 'ships_wait=true ' end
          tools.storage_look=function(ref) return 'docked=true outside='..tostring(OUTSIDE)..' ' end
          tools.wreck_look=function(name) return 'state=ok owner=ownerless claimable=true name='..name end
          tools.named_ship=function(name) if name=='Wreck' then return 'luawreck','ownerless','idwreck' end end
          tools.mine_spot=function(sector,ware,ship) return SPOT end
          for _,name in ipairs({'SetAllTurretModes','SetAllTurretsArmed','SetAllDronesArmed','SetTurretGroupMode2','SetTurretGroupArmed','SetWeaponMode',
              'SetDroneMode','SetDroneTypeArmed','SetWeaponGroup','PutShipIntoStorage'}) do
            tools.C[name]=function(...) local parts={name};for i=1,select('#',...) do parts[#parts+1]=tostring((select(i,...))) end;made[#made+1]=table.concat(parts,':') end
          end
          OUTSIDE=true
          tools.C.IsShipAtExternalDock=function(id) return OUTSIDE end
          tools.C.GetContextByClass=function(id,class) return 'idcarrier' end
          ROOM=true;CAN=true;COMING=false
          tools.C.HasContainerFreeInternalShipStorage=function(container,id) return ROOM end
          tools.C.CanPutShipIntoStorage=function(container,id) return CAN end
          tools.C.IsShipBeingRetrieved=function(id) return COMING end
          tools.C.RequestShipFromInternalStorage2=function(id,fast,who) made[#made+1]='fetch:'..tostring(id)..':'..tostring(fast)..':'..tostring(who);return true end
          tools.C.ClaimShip=function(ship,wreck) made[#made+1]='claim:'..tostring(ship)..':'..tostring(wreck);return true end
          tools.C.GetDistanceBetween=function(a,b) return 1500 end
          tools.C.IsOrderSelectableFor=function(def,id) return true end
          tools.C.GetObjectPositionInSector=function(id) return {x=1,y=2,z=3} end
          MARINES=true
          tools.env.GetComponentData=function(id,...)
            local key=select(1,...)
            if key=='isdocked' then return false,MARINES,40000 end
            if key=='canbeclaimed' then return true end
            if key=='cargo' then return CARGO end
            return 'pilot' end
          tools.env.GetWareCapacity=function(ship,ware,flag) return 900 end
          tools.env.CreateOrder=function(ship,def,params) made[#made+1]={ship=ship,def=def,params=params};return 3 end
          tools.env.AddUITriggeredEvent=function(screen,control,value) made[#made+1]=screen..':'..control..':'..tostring(value.ship or value.near)..':'..tostring(value.amount or value.name)..':'..tostring(value.metres) end
          rows=run({{0,'systems_look','aa'},{1,'turrets_mode','attackfighters','aa'},{2,'turrets_mode','defend'},{3,'turrets_armed',false,'aa'},{4,'tgroup_mode','aa',1,'missiledefence'},
            {5,'tgroup_armed','aa',1,false},{6,'turret_set','aa',1,'mining'},{7,'drone_set','aa','defence',2},{8,'drone_armed','aa','defence',true},
            {9,'weapon_group','aa',2,false,3,true},{10,'pilot_look'},{11,'settings_look'},{12,'store','bb'},{13,'storage_look','bb'},{14,'claim','aa','Wreck'},
            {15,'claim','aa','Nothing'},{16,'wreck_look','Wreck'},{17,'marines_stock','aa',2},{18,'abandon','aa','Wreck',1500}},19)
          OUTSIDE=false;inside=run({{0,'store','bb'},{1,'retrieve','bb'}},2)
          MARINES=false;nobody=run({{0,'claim','aa','Wreck'}},1)
          CARGO={ore=5};SPOT={x=10,y=20,z=30,amount=77,d=250000}
          mining=run({{0,'cargo_gain','aa'},{1,'mine_spot','s1','ore','aa'},{2,'mine','aa','ore','s1'},{3,'order','aa','DeployObjectAtPosition',{{'destination',{'spot','s1','ore','aa'}}}}},4)
          CARGO={ore=45,ice=2};SPOT=nil
          later=run({{0,'cargo_gain','aa'},{1,'mine_spot','s1','ore','aa'},{2,'mine','aa','ore','s1'},{3,'order','aa','DeployObjectAtPosition',{{'destination',{'spot','s1','ore','aa'}}}}},4)''')
        rows = values(g.rows)
        self.assertEqual(rows[0], '0 systems_look true active=1/1 weapons=2:-,p1 ref=aa')
        self.assertEqual(rows[4], '4 tgroup_mode true ffi group=g1')
        self.assertEqual(rows[7], '7 drone_set true ffi mode=attackenemies')
        self.assertEqual(rows[12:17], ['12 store true put', '13 storage_look true docked=true outside=true ', '14 claim true claimed=true gap=1500', '15 claim true no_wreck',
                                       '16 wreck_look true state=ok owner=ownerless claimable=true name=Wreck'])
        self.assertEqual(values(g.inside), ['0 store true inside', '1 retrieve true requested=true'])
        # the entry's conditions: greyed out, the op does nothing
        lua.execute('''before=#made;OUTSIDE=true;CAN=false;grey=run({{0,'store','bb'}},1);CAN=true;ROOM=false;for _,r in ipairs(run({{0,'store','bb'}},1)) do grey[#grey+1]=r end
          OUTSIDE=false;COMING=true;for _,r in ipairs(run({{0,'retrieve','bb'}},1)) do grey[#grey+1]=r end;grey_calls=#made-before''')
        self.assertEqual(values(g.grey), ['0 store true greyed cannot', '0 store true greyed no_room', '0 retrieve true greyed on_its_way'])
        self.assertEqual(g.grey_calls, 0)
        self.assertEqual(values(g.nobody), ['0 claim true no_marines'])
        made = [x for x in values(g.made) if isinstance(x, str)]
        for call in ('SetAllTurretModes:idaa:attackfighters', 'SetAllTurretModes:ship2:defend', 'SetAllTurretsArmed:idaa:false', 'SetTurretGroupMode2:idaa:c1:p:g1:missiledefence',
                     'SetTurretGroupArmed:idaa:c1:p:g1:false', 'SetWeaponMode:t1:mining', 'SetDroneMode:idaa:defence:attackenemies', 'SetDroneTypeArmed:idaa:defence:true',
                     'SetWeaponGroup:idaa:w2:false:3:true', 'PutShipIntoStorage:idcarrier:idbb', 'fetch:idbb:false:7', 'claim:idaa:idwreck',
                     'X4LAN_UiReplay:Marines:luaaa:2:nil', 'X4LAN_UiReplay:Abandoned:luaaa:Wreck:1500'):
            self.assertIn(call, made)
        self.assertEqual(values(g.mining)[:3], ['0 cargo_gain true gain=0 total=5', '1 mine_spot true at=10/20/30 yield=77 away=500', '2 mine true order=3 at=10/20/30 yield=77'])
        # the hold's gain counts from the first asking; without a place that yields the ware nothing is ordered, and
        # the deploy order's place is where the ship is
        self.assertEqual(values(g.later)[:3], ['0 cargo_gain true gain=42 total=47', '1 mine_spot true no_spot yields=none', '2 mine true no_spot'])
        self.assertEqual(lua.eval("(function() for _,m in ipairs(made) do if type(m)=='table' and m.def=='MiningPlayer' then return m.ship..':'..m.params.ware..':'..m.params.destination[1]..':'..table.concat(m.params.destination[2],',') end end end)()"),
                         'luaaa:ore:luas1:10,20,30')
        self.assertEqual(lua.eval("(function() local last;for _,m in ipairs(made) do if type(m)=='table' and m.def=='DeployObjectAtPosition' then last=m end end;return table.concat(last.params.destination[2],',') end)()"), '1,2,3')
        for op in ('marines_stock', 'abandon'):
            self.assertTrue(lua.eval('X4LAN_UI_REPLAY_CORE.CANONICAL.%s' % op))
        # repeat orders and a loop order, as the behaviour page and the trade menu's loop Confirm make them
        lua.execute('''made={}
          for _,name in ipairs({'RemoveAllOrders','EnablePlannedDefaultOrder','SetOrderLoop','EnableOrder'}) do
            tools.C[name]=function(...) local parts={name};for i=1,select('#',...) do parts[#parts+1]=tostring((select(i,...))) end;made[#made+1]=table.concat(parts,':');return true end
          end
          tools.C.CreateOrder=function(id,def,default) made[#made+1]='CreateOrder:'..tostring(id)..':'..def..':'..tostring(default);return 3 end
          tools.env.SetOrderParam=function(ship,idx,param,index,value) made[#made+1]='param:'..tostring(ship)..':'..idx..':'..param..':'..tostring(value) end
          tools.env.GetWareData=function(ware,key) return key=='maxprice' and 300 or 100 end
          rows=run({{0,'behaviour_loop','aa'},{1,'trade_loop_order','aa','SingleBuy','st1','ore',20},{2,'trade_loop_order','aa','SingleSell','st2','ore',0,150}},3)''')
        self.assertEqual(values(g.rows), ['0 behaviour_loop true loop=true', '1 trade_loop_order true order=3 price=300', '2 trade_loop_order true order=3 price=150'])
        self.assertEqual(values(g.made)[:4], ['RemoveAllOrders:idaa', 'CreateOrder:idaa:Wait:true', 'EnablePlannedDefaultOrder:idaa:false', 'SetOrderLoop:idaa:0:false'])
        self.assertEqual(values(g.made)[4:10], ['CreateOrder:idaa:SingleBuy:false', 'param:luaaa:3:1:ore', 'param:luaaa:3:4:luast1', 'param:luaaa:3:5:20',
                                                'param:luaaa:3:7:30000', 'EnableOrder:idaa:3'])
        self.assertIn('param:luaaa:3:7:15000', values(g.made))
        self.assertTrue(lua.eval("(function() for _,e in ipairs(X4LAN_UI_REPLAY_CORE.UNBLOCKED.queue) do if e=='MapMenu.buttonConfirmTradeLoop' then return true end end end)()"))
        # a fleet ship's question: the scripted key press is the UI's call with this client's interaction ID
        lua.execute('''made={};QUESTION=0
          gate.md_media_question=function() return QUESTION end
          tools.env.RaisePlayerInteractionEvent=function(id) made[#made+1]='raise:'..tostring(id) end
          rows=run({{0,'question_look'},{1,'question_answer'}},2)
          QUESTION=4711;for _,r in ipairs(run({{0,'question_look'},{1,'question_answer'}},2)) do rows[#rows+1]=r end''')
        self.assertEqual(values(g.rows), ['0 question_look true question=0 ', '1 question_answer true no_question', '0 question_look true question=4711 ',
                                          '1 question_answer true answered id=4711'])
        self.assertEqual(values(g.made), ['raise:4711'])
        # a ware exchange between two ships of the player's: the other ship's buy offer takes what the first gives
        lua.execute('''made={}
          tools.env.GetWareExchangeTradeList=function(ship,other) made[#made+1]='list:'..tostring(ship)..':'..tostring(other)
            return {{id='x1',ware='ore',isbuyoffer=true,amount=500},{id='x2',ware='ore',isselloffer=true,amount=40},{id='x3',ware='ice',isbuyoffer=true,amount=9}} end
          tools.env.AddTradeToShipQueue=function(offer,ship,amount,immediate) made[#made+1]='queue:'..offer..':'..tostring(ship)..':'..amount..':'..tostring(immediate) end
          rows=run({{0,'exchange_look','aa','bb','ore'},{1,'trade_exchange','aa','bb','ore',30},{2,'trade_exchange','aa','bb','ore',-10},{3,'trade_exchange','aa','bb','silicon',5},
            {4,'hold_stock','aa','ore',50}},5)''')
        self.assertEqual(values(g.rows), ['0 exchange_look true offers=2 buy=500 sell=40 all=3 ', '1 trade_exchange true lua', '2 trade_exchange true lua', '3 trade_exchange true no_offer',
                                          '4 hold_stock true lua'])
        made = values(g.made)
        self.assertEqual(made[0], 'list:luaaa:luabb')
        self.assertIn('queue:x1:idaa:30:false', made)
        self.assertIn('queue:x2:idaa:10:false', made)
        self.assertEqual(sum(1 for x in made if x.startswith('queue:')), 2)
        # a launched deployable as the interact menu treats it
        lua.execute('''made={};ACTIVE=true;OWN='luamine'
          tools.deploy_mark=function(macro) return '4711' end
          tools.deploy_own=function(macro) if OWN then return OWN,'id4711' end end
          tools.env.GetComponentData=function(id,key) return ACTIVE end
          tools.C.ActivateObject=function(id,on) made[#made+1]='activate:'..tostring(id)..':'..tostring(on);ACTIVE=on end
          tools.C.SelfDestructComponent=function(id) made[#made+1]='destroy:'..tostring(id);OWN=nil end
          rows=run({{0,'deploy_mark','mine_macro'},{1,'deployable_look','mine_macro'},{2,'deployable_active','mine_macro',true},{3,'deployable_active','mine_macro',false},
            {4,'deployable_look','mine_macro'},{5,'deployable_destroy','mine_macro'},{6,'deployable_look','mine_macro'},{7,'deployable_active','mine_macro',true}},8)''')
        self.assertEqual(values(g.rows), ['0 deploy_mark true marked=4711', '1 deployable_look true state=ok active=true ', '2 deployable_active true already',
                                          '3 deployable_active true ffi', '4 deployable_look true state=ok active=false ', '5 deployable_destroy true ffi',
                                          '6 deployable_look true state=gone ', '7 deployable_active true gone'])
        self.assertEqual(values(g.made), ['activate:id4711:false', 'destroy:id4711'])
        # a wing's group settings and a fleet's name (the faction family's claims), a planned default order's parameter
        lua.execute('''made={}
          tools.wing_look=function(ref,group) return 'role=defence sight=true group='..group..' ' end
          for _,name in ipairs({'SetSubordinateGroupAssignment','SetSubordinateGroupAttackOnSight','SetSubordinateGroupDockAtCommander','SetSubordinateGroupReinforceFleet',
              'SetSubordinateGroupRespondToDistressCalls','SetSubordinateGroupResupplyAtFleet','SetFleetName'}) do
            tools.C[name]=function(...) local parts={name};for i=1,select('#',...) do parts[#parts+1]=tostring((select(i,...))) end;made[#made+1]=table.concat(parts,':') end
          end
          tools.env.SetOrderParam=function(ship,order,param,index,value) made[#made+1]='param:'..tostring(ship)..':'..order..':'..param..':'..tostring(index)..':'..tostring(value) end
          rows=run({{0,'wing_look','aa',2},{1,'wing_set','aa',1,'role','attack'},{2,'wing_set','aa',1,'sight',false},{3,'wing_set','aa',2,'dock',true},
            {4,'wing_set','aa',1,'reinforce',true},{5,'wing_set','aa',1,'distress',false},{6,'wing_set','aa',1,'resupply',true},{7,'fleet_name','aa','Home Fleet'},
            {8,'behaviour_param','aa',1,'ore'},{9,'behaviour_param','aa',2,{'id','s1'}},{10,'behaviour_param','aa',4,0},{11,'wing_set','aa',1,'colour','red'}},12)''')
        self.assertEqual(values(g.rows)[0], '0 wing_look true role=defence sight=true group=2 ')
        self.assertIn('wing_set colour', values(g.rows)[11])          # an unknown setting is the script's mistake
        self.assertEqual([x for x in values(g.made) if isinstance(x, str)], [
            'SetSubordinateGroupAssignment:idaa:1:attack', 'SetSubordinateGroupAttackOnSight:idaa:1:false', 'SetSubordinateGroupDockAtCommander:idaa:2:true',
            'SetSubordinateGroupReinforceFleet:idaa:1:true', 'SetSubordinateGroupRespondToDistressCalls:idaa:1:false', 'SetSubordinateGroupResupplyAtFleet:idaa:1:true',
            'SetFleetName:idaa:Home Fleet', 'param:luaaa:planneddefault:1:nil:ore', 'param:luaaa:planneddefault:2:nil:luas1', 'param:luaaa:planneddefault:4:nil:0'])
        cue = (ROOT/'tools/dev/native_gate_ui_replay.xml').read_text(encoding='utf-8')
        for control in ('Marines', 'Abandoned'):
            self.assertIn('control="\'%s\'"' % control, cue)

    def test_every_scenario_has_a_plan_for_both_clients_with_the_same_fixtures(self):
        import argparse
        lua, g = self.client()
        for name, scenario in action_tests.SCENARIOS.items():
            s = argparse.ArgumentParser()
            scenario.options(s)
            args = s.parse_args([])
            plans = action_tests.full_plan(scenario, args)
            self.assertEqual(len(plans), 2, name)
            fixtures = [[e for e in plan if lua.eval('X4LAN_UI_REPLAY_CORE.CANONICAL[%r]' % e[1])] for plan in plans]
            self.assertEqual(fixtures[0], fixtures[1], name)          # a fixture runs on every client at the same tick
            for plan in plans:
                chains = [e for e in plan if e[1] == 'chain']
                self.assertEqual(len(chains), 1, name)
                names = [step[0] for step in chains[0][3]]
                self.assertEqual(len(names), len(set(names)), '%s: a step name twice' % name)
                self.assertLessEqual(chains[0][0] + chains[0][2], (scenario.minutes * 60 + 420) * 30, name)
                # the plan is one the core takes: every op known, no fixture inside a chain
                lua.execute('X4LAN_UI_REPLAY_CORE.script(%s,0,tools)' % rig.lua(plan))

    def test_regular_readings_are_one_plan_entry(self):
        lua, g = self.client()
        lua.execute('''CARGO={ore=4}
          rows=run({{3,'every',5,14,{{'cargo','ore'},{'foot_where'}}},{8,'cargo'}},20)''')
        self.assertEqual(sorted(values(g.rows)), sorted([
            '3 every true readings=6', '3 cargo true amount=4 ware=ore wares=1 ore=4 money=123', '3 foot_where true docked=false',
            '8 cargo true wares=1 ore=4 money=123', '8 cargo true amount=4 ware=ore wares=1 ore=4 money=123', '8 foot_where true docked=false',
            '13 cargo true amount=4 ware=ore wares=1 ore=4 money=123', '13 foot_where true docked=false']))
        self.assertEqual(action_tests.watch_rows([('cargo', 'ore', 1)], 1), [(300, 'every', 300, 1649, [('cargo', 'ore', 1)])])
        self.assertEqual(action_tests.watch_rows([], 1), [])
        # the longest plan fits a Windows command line with room to spare
        import argparse
        for name, scenario in action_tests.SCENARIOS.items():
            s = argparse.ArgumentParser()
            scenario.options(s)
            self.assertLess(len(rig.lua(action_tests.full_plan(scenario, s.parse_args([])))), 20000, name)

    def test_a_fixture_is_no_step_and_the_wallet_is_a_fixture(self):
        lua, g = self.client()
        with self.assertRaises(Exception):
            lua.execute('''run({{0,'chain',10,{{'kit','op','dock_kit',1}}}},5)''')
        with self.assertRaises(Exception):
            lua.execute('''run({{0,'chain',10,{{'x','sometimes','cargo'}}}},5)''')
        lua.execute('''rows=run({{3,'wallet',50000}},5)''')
        self.assertEqual(values(g.calls)[-1], 'X4LAN_UiReplay:Wallet:50000')
        self.assertTrue(lua.eval('X4LAN_UI_REPLAY_CORE.CANONICAL.wallet'))
        cue = (ROOT/'tools/dev/native_gate_ui_replay.xml').read_text(encoding='utf-8')
        self.assertIn('control="\'Wallet\'"', cue)
        self.assertIn('<reward_player money="(event.param3.$credits)Cr"/>', cue)


STOCK_UI = ROOT.parent/'x4-re/gamelua/08/ui/addons'
MENU_FILES = {'InteractMenu': 'ego_interactmenu/menu_interactmenu.lua', 'MapMenu': 'ego_detailmonitor/menu_map.lua'}


class OpenEntryTests(unittest.TestCase):
    """The stock menu entries a session leaves open to a person (K.UNBLOCKED): read against the stock menus."""

    @unittest.skipUnless(STOCK_UI.is_dir(), 'x4-re stock UI scripts not at hand')
    def test_every_open_order_entry_makes_only_calls_the_replay_captures(self):
        lua, _ = uirep.client()
        entries = [e for family in lua.eval('X4LAN_UI_REPLAY_CORE.UNBLOCKED').values() for e in family.values()]
        # what an order entry may call: reads, and the calls the orders family captures
        allowed = {'IsOrderSelectableFor', 'IsComponentClass', 'GetWareCapacity', 'RemoveAllOrders2', 'CreateOrder'}
        seen = 0
        for entry in entries:
            menu, name = entry.split('.')
            if menu not in MENU_FILES:
                continue          # a hotkey
            text = (STOCK_UI/MENU_FILES[menu]).read_text(encoding='utf-8', errors='replace')
            body = re.search(r'\nfunction menu\.' + name + r'\((.*?)\)\n(.*?)\nend\n', text, re.S)
            self.assertIsNotNone(body, entry)
            if not name.startswith('order'):
                continue          # the queue editor's rows and the trade Confirm: their own families' tests
            calls = set(re.findall(r'\bC\.(\w+)\(', body.group(2))) | set(re.findall(r'(?<![\w.:])(CreateOrder\d?|SetOrderParam|AddTradeToShipQueue|SignalObject)\(', body.group(2)))
            self.assertLessEqual(calls, allowed, entry)
            seen += 1
        self.assertGreaterEqual(seen, 28)
        # the two functions named order... that order nothing are never blocked (the probe's ORDER_LOCAL)
        probe = (ROOT/'tools/dev/native_gate_probe.lua').read_text(encoding='utf-8')
        self.assertIn("local ORDER_LOCAL={MapMenu={orderAmountHelper=true},InteractMenu={orderIconText=true}}", probe)
        for menu, name in (('MapMenu', 'orderAmountHelper'), ('InteractMenu', 'orderIconText')):
            text = (STOCK_UI/MENU_FILES[menu]).read_text(encoding='utf-8', errors='replace')
            body = re.search(r'\nfunction menu\.' + name + r'\((.*?)\)\n(.*?)\nend\n', text, re.S).group(2)
            self.assertEqual(re.findall(r'\bC\.(\w+)\(', body), [], name)          # no engine call at all



def rows_of(steps, slot):
    """Log rows of a finished trade chain."""
    out, n = [], 0
    for name, text in steps:
        n += 1
        out.append('[X4LAN] NATIVE_GATE UI_REPLAY_SCRIPT tick=%d op=chain ok=true result=step=%s n=%d at=%d ok=true %s scope=1' % (100 + n * 10 + slot, name, n, n * 10, text))
    out.append('[X4LAN] NATIVE_GATE UI_REPLAY_SCRIPT tick=%d op=chain ok=true result=chain_end ok=true steps=19/19 scope=1' % (400 + slot))
    return out


class ReportTests(unittest.TestCase):
    STEPS = [('dock_buy', 'docked after=1900 asked=1'), ('at_buy', 'player=1 station=500 docked=true'),
             ('entry_trade', 'open why=shared entry=MapMenu.buttonConfirmTrade'), ('offer_buy', 'offers=1 sell=19.78/22280 money=60000'),
             ('cargo_before', 'amount=0 ware=foodrations wares=0 money=60000'), ('buy', 'lua'),
             ('bought', 'amount=20 ware=foodrations wares=1 foodrations=20 money=59604.4'), ('undock', 'ok'), ('undocked', 'player=1 station=0 docked=false'),
             ('dock_sell', 'docked after=800 asked=1'), ('at_sell', 'player=1 station=600 docked=true'), ('offer_sell', 'offers=1 buy=18.69/2562 money=59208.8'),
             ('sell', 'lua'), ('sold', 'amount=0 ware=foodrations wares=0 money=59582.6')]
    MONEY = ['UI_TRADE_MONEY tick=1 UITRADE_1:1000000,n;0;0', 'UI_TRADE_MONEY tick=21 UITRADE_1:1000000,1000000;0;0', 'UI_TRADE_MONEY tick=45 UITRADE_1:6000000,6000000;0;0',
             'UI_TRADE_MONEY tick=2100 UITRADE_1:5960440,5960440;0;0', 'UI_TRADE_MONEY tick=2130 UITRADE_1:5920880,5920880;0;0',
             'UI_TRADE_MONEY tick=3300 UITRADE_1:5958260,5958260;0;0', 'UI_TRADE_MONEY tick=3330 UITRADE_1:5995640,5995640;0;0']
    APPLIED = ['UI_REPLAY_APPLIED tick=%d slot=%d op=88 result=applied what=AddTradeToShipQueue' % x for x in ((2100, 1), (2130, 2), (3300, 1), (3330, 2))]

    def logs(self, tmp, change=None):
        paths = []
        for k in (1, 2):
            lines = rows_of(self.STEPS, k) + ['[X4LAN] NATIVE_GATE ' + x for x in self.MONEY + self.APPLIED]
            lines += ['[X4LAN] NATIVE_GATE DIGEST_PARTS tick=%d hash=%d world=aa records=bb inputs=cc rid=dd' % (t, t) for t in (1, 2, 3, 1799, 1800)]          # to the first capture of the world
            # the watch: both players' holds read on this client at the same ticks (not in the presentation scope)
            lines += ['[X4LAN] NATIVE_GATE UI_REPLAY_SCRIPT tick=%d op=cargo ok=true result=amount=%d ware=foodrations wares=0 money=60000' % (t, n)
                      for t in (340, 640) for n in (0, 20)]
            if change:
                lines = change(k, lines)
            path = Path(tmp)/('c%d.log' % k)
            path.write_text('\n'.join(lines), encoding='utf-8')
            paths.append(path)
        return paths

    def args(self):
        t = action_tests.Trade
        return SimpleNamespace(buy_station=t.BUY, sell_station=t.SELL, ware=t.WARE, amount=t.AMOUNT, credits=t.CREDITS, by='calls')

    def failed(self, change=None):
        import contextlib
        import io
        with tempfile.TemporaryDirectory() as tmp:
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                n = action_tests.report(action_tests.Trade, self.logs(tmp, change), None, self.args())
            return n, out.getvalue()

    def test_a_clean_trade_run_passes_every_check(self):
        n, text = self.failed()
        self.assertEqual(n, 0, text)
        self.assertIn('the fixture paid 50000 Cr at tick 45; then -395.60 Cr at tick 2100, -395.60 Cr at tick 2130, +373.80 Cr at tick 3300, +373.80 Cr at tick 3330', text)
        self.assertIn('the 4 trade calls ran on both clients at the same ticks', text)

    def test_the_interact_report_reads_each_players_own_menu(self):
        import contextlib
        import io
        own = 'shown=true mode=cockpit context=nil ship=%d own=true'
        steps = {k: [('tab', 'pressed opens=DockedMenu'), ('open', own % (1000 + k)), ('assist_off', 'assist=false'),
                     ('maptab', 'pressed opens=DockedMenu'), ('open2', own % (1000 + k)), ('back', 'back to=DockedMenu'),
                     ('open3', own % (1000 + k)), ('assist_on', 'assist=true'), ('other', 'gave_up')] for k in (1, 2)}

        def run(change=None):
            with tempfile.TemporaryDirectory() as tmp:
                paths = []
                for k in (1, 2):
                    lines = rows_of(change(k, steps[k]) if change else steps[k], k)
                    lines += ['[X4LAN] NATIVE_GATE DIGEST_PARTS tick=%d hash=%d world=aa records=bb inputs=cc rid=dd' % (t, t) for t in (1, 2, 1799, 1800)]
                    lines += ['[X4LAN] NATIVE_GATE UI_DOCKED_MENU kept=0 passed=0 person=1 via=tab'] if k == 2 else []
                    lines += ['[X4LAN] NATIVE_GATE UI_REPLAY_SCRIPT tick=%d op=ship_look ok=true result=ship=%d at=1/2/3' % (t, s) for t in (340, 640) for s in (1, 2)]
                    path = Path(tmp)/('c%d.log' % k)
                    path.write_text('\n'.join(lines), encoding='utf-8')
                    paths.append(path)
                out = io.StringIO()
                with contextlib.redirect_stdout(out):
                    n = action_tests.report(action_tests.Interact, paths, None, SimpleNamespace())
                return n, out.getvalue()
        n, text = run()
        self.assertEqual(n, 0, text)
        self.assertIn('client 2: UI_DOCKED_MENU kept=0 passed=0 person=1 via=tab', text)
        # the menu was slot 1's ship on client 2, or client 1 showed a dock menu while player 2 used its own
        n, text = run(lambda k, s: [(a, b.replace('own=true', 'own=false') if a == 'open2' and k == 2 else b) for a, b in s])
        self.assertEqual(n, 1, text)
        self.assertIn("opened    FAIL player 2: Ship Interactions for its own ship from the map's tabs", text)
        n, text = run(lambda k, s: s[:-1] + [('other', 'shown=true mode=cockpit context=nil ship=1002 own=false')] if k == 1 else s)
        self.assertEqual(n, 1, text)
        self.assertIn("own       FAIL player 1's client kept its dock menu closed while the other player used its own (shown=true", text)
        self.assertIn("own       ok   player 2's client kept its dock menu closed while the other player used its own (watched 1200 ticks)", text)

    def test_what_went_wrong_is_named(self):
        def unsold(k, lines):
            return [x for x in lines if 'step=sold ' not in x] if k == 2 else lines
        n, text = self.failed(unsold)
        self.assertEqual(n, 1, text)
        self.assertIn('sold      FAIL player 2', text)

        def account(k, lines):
            return [x.replace('5958260,5958260', '5958261,5958261') for x in lines] if k == 2 else lines
        n, text = self.failed(account)
        self.assertEqual(n, 1, text)
        self.assertIn('account   FAIL', text)

        def blocked(k, lines):          # the calls trade, but a person's click on Confirm would be refused
            return [x.replace('ok=true open why=shared entry=', 'ok=true blocked entry=') for x in lines]
        n, text = self.failed(blocked)
        self.assertEqual(n, 2, text)
        import contextlib
        import io
        printed = io.StringIO()
        with contextlib.redirect_stdout(printed):
            self.assertEqual(action_tests.main(['plan', 'trade']), 0)
        self.assertIn("'trade_menu_open'", printed.getvalue())          # the default is the menu-driven run
        self.assertIn('offered   FAIL player 1: the trade menu', text)
        self.assertIn('REFUSED to a person in this session (blocked entry=MapMenu.buttonConfirmTrade)', text)

        def refused(k, lines):          # what a client was refused is named (a note: the checks say what it cost)
            return lines + ['[X4LAN] NATIVE_GATE UI_REPLAY_REFUSED what=MapMenu.buttonHire why=slot'] * 2 if k == 2 else lines
        n, text = self.failed(refused)
        self.assertEqual(n, 0, text)
        self.assertIn("refused   note player 2's client was refused: MapMenu.buttonHire (slot) x2", text)

        def reading(k, lines):
            return [x.replace('tick=640 op=cargo ok=true result=amount=20', 'tick=640 op=cargo ok=true result=amount=19') for x in lines] if k == 2 else lines
        n, text = self.failed(reading)
        self.assertEqual(n, 1, text)
        self.assertIn('readings  FAIL 2 readings differ, the first at tick 640', text)

        def world(k, lines):
            return [x.replace('tick=1800 hash=1800 world=aa', 'tick=1800 hash=1800 world=ab') for x in lines] if k == 2 else lines
        n, text = self.failed(world)
        self.assertEqual(n, 1, text)
        self.assertIn('in sync   FAIL world differs at 1 ticks from tick 1800', text)

        def short(k, lines):          # a run that ends before the first capture of the world has compared no world
            return [x for x in lines if 'DIGEST_PARTS tick=1799' not in x and 'DIGEST_PARTS tick=1800' not in x]
        n, text = self.failed(short)
        self.assertEqual(n, 1, text)
        self.assertIn('in sync   FAIL the run ended before the first capture of the whole world (tick 1800)', text)

        def stopped(k, lines):
            return lines[:5] + ['[X4LAN] NATIVE_GATE STOP reason=3'] if k == 1 else lines
        n, text = self.failed(stopped)
        self.assertGreaterEqual(n, 3, text)
        self.assertIn('running   FAIL', text)
        self.assertIn('chain     FAIL player 1: no end row', text)

    def test_the_plan_is_a_chain_per_player_with_the_fixtures_first(self):
        plan = action_tests.Trade.plan(self.args())
        self.assertEqual(len(plan), 2)
        for k, client in enumerate(plan, 1):
            self.assertEqual(client[:3], [(30, 'dock_kit', 1), (30, 'dock_kit', 2), (40, 'wallet', 50000)])
            tick, op, limit, steps = client[3]
            self.assertEqual((tick, op), (60 + 30 * (k - 1), 'chain'))
            names = [s[0] for s in steps]
            self.assertEqual(len(names), len(set(names)))
            self.assertIn(('buy', 'op', 'trade_buy', action_tests.Trade.BUY, 'foodrations', 20, k, 1), steps)
            self.assertIn(('sell', 'op', 'trade_sell', action_tests.Trade.SELL, 'foodrations', 20, k, 1), steps)
        by_menu = self.args()
        by_menu.by = 'menu'
        steps = action_tests.Trade.plan(by_menu)[1][3][3]          # player 2's chain: the stock menus' own handlers
        for step in (('open_buy', 'op', 'trade_menu_open', action_tests.Trade.BUY), ('amount_buy', 'op', 'trade_menu_amount', 'foodrations', 20),
                     ('buy', 'op', 'menu_call', 'MapMenu', 'buttonConfirmTrade'), ('amount_sell', 'op', 'trade_menu_amount', 'foodrations', -20),
                     ('sell', 'op', 'menu_call', 'MapMenu', 'buttonConfirmTrade')):
            self.assertIn(step, steps)
        self.assertFalse(any(s[2] in ('trade_buy', 'trade_sell') for s in steps if len(s) > 2))
        full = action_tests.full_plan(action_tests.Trade, self.args())
        for client in full:          # the watch: the same readings at the same ticks in both plans
            self.assertEqual([x for x in client if x[1] == 'every'], [(300, 'every', 300, 14249, [('cargo', 'foodrations', 1), ('cargo', 'foodrations', 2)])])
        lua, _ = uirep.client()
        self.assertEqual(lua.eval('#' + rig.lua(full)), 2)          # the plan's text is Lua


if __name__ == '__main__':
    unittest.main()

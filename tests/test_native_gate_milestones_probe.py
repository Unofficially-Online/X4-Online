"""The probe's milestone saves and a continued session's restore (native_gate_milestones, native_gate_continue;
tools/dev/native_gate_probe.lua). Synthetic: LuaJIT with a stand-in gate, X4 and MD events; no X4."""
from pathlib import Path
import unittest

from lupa.luajit21 import LuaRuntime, LuaError

SOURCE = (Path(__file__).resolve().parents[1] / 'tools/dev/native_gate_probe.lua').read_text()
DONE = "|1234|" + 'ab' * 32 + "|2500|-"


class MilestoneProbeTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('''
          X4LAN_CONFIG={native_gate_defaults=false,native_gate_test=true,test_profile='45841',slot='p1',
            native_gate_hash='clock',native_gate_snapshot_sha=string.rep('a',64),
            port=12345,token=string.rep('c',32)}
          X4LAN_SHA256={digest=function() return string.rep('b',64) end}
          logs={};loads=0;reports=0;boots=0;is_menu=true;game_time=7
          state={active=1,boot_phase=0,issued=0,report_tick=0,rng='0'}
          local gate={boot=function() boots=boots+1;return 0 end,
            poll=function() return state end,
            load_requested=function() state.boot_phase=1;return 1 end,
            report=function(tick) reports=reports+1;state.report_tick=tick;return 1 end,
            stop=function() state.active=0;return 1 end}
          package.loadlib=function(path) assert(not path:find('fixedtick'));return function() return gate end end
          local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,
            C={GetSaveFolderPath=function() return 'fake/45841/save' end,
              GetCurrentGameTime=function() return game_time end,IsStartmenu=function() return is_menu end,
              ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end}}
          require=function(name) assert(name=='ffi');return ffi end
          DebugError=function(s) logs[#logs+1]=s end
          events={};RegisterEvent=function(name,callback) events[name]=callback end
          SetScript=function(name,fn) update=fn end
          GetSaveList=function() return {{filename='save_009'}} end
          LoadGame=function(slot) assert(slot=='save_009');loads=loads+1 end
          ConvertIDTo64Bit=function(v) return v end
          ConvertStringToLuaID=function(text) return 'ID:'..text end
          -- the stand-in gate's milestone side
          calls={};save_text='idle|0|0|-|0|-';walking={};claims={};scopes={}
          gate.save_arm=function(tick,folder,drop) calls[#calls+1]=table.concat({tick,folder,drop},',');save_text='armed|'..tick..'|0|-|0|-';return 1 end
          gate.save_result=function() return save_text end
          gate.pilot_ship=function(k) return 1000+k,string.format('%016x',k) end
          gate.ui_inventory_player=function(k) return tostring(500+k) end
          gate.pilot_foot_walking=function(k) return walking[k] and 1 or 0 end
          gate.mpp_scope=function(k) scopes[#scopes+1]=k;return 1 end
          gate.mpp_unscope=function(k) return 1 end
          gate.mpp_claim=function(k,id) claims[#claims+1]=k..':'..id;return 1 end
          gate.mpp_status=function() return 'armed=1 owners=3 hash=00000000000000aa claims=3 gone=0' end
          inventory={software_scannerobjectmk3={amount=1},weapon_gen_spacesuit_repairlaser_01_mk1={amount=1}}
          GetPlayerInventory=function() return inventory end
          added={};AddInventory=function(_,ware,n) added[#added+1]=ware..'='..n;inventory[ware]={amount=n} end
        ''')

    def start(self):
        self.lua.execute(SOURCE)
        self.lua.execute('update();update();update()')
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(SOURCE)
        self.lua.execute('update()')

    def to(self, tick):
        """Every tick up to this one, each with its game time (60 Hz: the probe's default tick rate)."""
        self.lua.execute(f'for t=state.issued+1,{tick} do state.issued=t;game_time=7+(t-1)/60;update() end')

    def rows(self, text):
        return [line for line in self.lua.globals().logs.values() if text in line]

    def test_a_milestone_saves_at_a_tick_from_its_game_time_and_says_so(self):
        self.lua.execute("X4LAN_CONFIG.native_gate_milestones={save=true,watch={{'terran_cadet',1,2},{'terran_cadet',2,2}}}")
        self.start()
        self.assertTrue(self.rows('MILESTONE_CLOCK first=1 time=7 hz=60 save=true'))
        # MD: the chapter is over at game time 10; the probe hears it at tick 300 (any tick before T-25 gives the same T)
        self.to(300)
        self.lua.execute("events['X4LAN_Milestones.Reached1'](nil,10)")
        self.to(301)
        self.assertTrue(self.rows('MILESTONE reached name=terran_cadet-M1-slot2 age=10 due=481 save=541 at=301'), self.rows('MILESTONE'))
        self.to(515)
        self.assertEqual(list(self.lua.globals().calls.values()), [])
        self.to(516)   # T-25: the slots, the owners, the arming
        self.assertEqual(list(self.lua.globals().calls.values()), ['541,fake/45841/save,0'])
        slot2 = self.rows('SAVE_SLOT name=terran_cadet-M1-slot2 tick=541 slot=2')
        self.assertEqual(len(slot2), 1)
        self.assertIn('player=502 ship=1002 walking=0 inventory=software_scannerobjectmk3:1,weapon_gen_spacesuit_repairlaser_01_mk1:1', slot2[0])
        self.assertTrue(self.rows('SAVE_SLOT name=terran_cadet-M1-slot2 tick=541 slot=1 player=501 ship=1001 walking=0 inventory=- '))
        self.assertTrue(self.rows('SAVE_OWNERS name=terran_cadet-M1-slot2 tick=541 kept=unavailable'))   # no UI replay here
        self.assertTrue(self.rows('MILESTONE_SAVE armed name=terran_cadet-M1-slot2 tick=541 at=516'))
        self.to(541)
        self.lua.execute(f"save_text='done|541{DONE}'")
        self.to(542)
        self.assertTrue(self.rows('MILESTONE_SAVE saved name=terran_cadet-M1-slot2 tick=541 file=mpsave/s541.xml.gz bytes=1234 sha256=' + 'ab' * 32 +
                                  ' ms=2500 at=542'))
        self.assertFalse(self.rows('ERROR'))

    def test_a_refused_save_is_tried_again_and_a_walking_player_1_postpones_it(self):
        self.lua.execute("X4LAN_CONFIG.native_gate_milestones={save=true,watch={{'terran_cadet',1,1}}}")
        self.start()
        self.lua.execute("events['X4LAN_Milestones.Reached1'](nil,10)")
        self.to(2)
        self.lua.execute('walking[1]=true')
        self.to(516)
        self.assertTrue(self.rows('MILESTONE_SAVE postponed name=terran_cadet-M1-slot1 tick=541 why=walking slots=1 retry=841 walks=1 at=516'))
        self.assertEqual(list(self.lua.globals().calls.values()), [])
        self.lua.execute('walking[1]=false')
        self.to(816)
        self.assertEqual(list(self.lua.globals().calls.values()), ['841,fake/45841/save,0'])
        self.lua.execute("save_text='failed|841|0|-|0|is_saving_possible'")
        self.to(842)   # X4 refused it (a call of the simulation): 300 ticks on
        self.assertTrue(self.rows('ERROR MILESTONE_SAVE_FAILED name=terran_cadet-M1-slot1 tick=841 state=failed why=is_saving_possible retry=1141 tries=1 at=842'))
        self.to(1116)
        self.assertEqual(list(self.lua.globals().calls.values())[-1], '1141,fake/45841/save,0')

    def test_a_periodic_save_near_a_milestone_save_is_left_out(self):
        self.lua.execute("X4LAN_CONFIG.native_gate_milestones={save=true,watch={{'terran_cadet',1,2}}};"
                         "X4LAN_CONFIG.native_gate_save_every=300")
        self.start()
        self.lua.execute("events['X4LAN_Milestones.Reached1'](nil,10)")   # T=541: the periodic 600 is 59 ticks away
        self.to(299)
        self.lua.execute(f"save_text='done|300{DONE}'")
        self.to(541)
        self.lua.execute(f"save_text='done|541{DONE}'")
        self.to(899)
        self.assertEqual([c.split(',')[0] for c in self.lua.globals().calls.values()], ['300', '541', '900'])
        self.assertTrue(self.rows('PERIODIC_SAVE skipped tick=600 for=milestone at=570'))
        self.assertEqual(len(self.rows('PERIODIC_SAVE skipped')), 1)

    def test_without_saves_the_rows_stay_and_a_late_one_is_said(self):
        self.lua.execute("X4LAN_CONFIG.native_gate_milestones={save=false,watch={{'terran_cadet',1,2}}}")
        self.start()
        self.lua.execute("events['X4LAN_Milestones.Reached1'](nil,10)")
        self.to(600)
        self.assertTrue(self.rows('MILESTONE reached name=terran_cadet-M1-slot2 age=10 due=481 save=off'))
        self.assertEqual(list(self.lua.globals().calls.values()), [])
        self.setUp()
        self.lua.execute("X4LAN_CONFIG.native_gate_milestones={save=true,watch={{'terran_cadet',1,2}}}")
        self.start()
        self.to(530)
        self.lua.execute("events['X4LAN_Milestones.Reached1'](nil,10)")   # T-25 = 516 has gone by: no client could agree now
        self.to(531)
        self.assertTrue(self.rows('ERROR MILESTONE_LATE name=terran_cadet-M1-slot2 due=481 tick=541 at=531'))
        self.assertEqual(list(self.lua.globals().calls.values()), [])

    def test_a_periodic_save_is_prepared_as_a_milestone_save(self):
        """Every periodic save of a session gets the slots' rows and the owner snapshot when it arms (x4ls continue --save)."""
        self.lua.execute("X4LAN_CONFIG.native_gate_milestones={save=true,watch={}};X4LAN_CONFIG.native_gate_save_every=300")
        self.start()
        self.to(270)
        self.assertEqual(list(self.lua.globals().calls.values()), ['300,fake/45841/save,0'])
        self.assertEqual(len(self.rows('SAVE_SLOT name=periodic-t300 tick=300 slot=')), 2)
        self.assertTrue(self.rows('SAVE_OWNERS name=periodic-t300 tick=300 kept=unavailable at=270'))
        self.to(271)
        self.assertEqual(len(self.rows('SAVE_SLOT name=periodic-t300')), 2)   # once per save

    def player_session(self, save='true'):
        self.lua.execute(f"X4LAN_CONFIG.native_gate_milestones={{save={save},watch={{}}}};X4LAN_CONFIG.native_gate_ui_lua='block';"
                         "X4LAN_CONFIG.native_gate_ui_lua_names={'SaveGame','LoadGame'};X4LAN_CONFIG.slot='p2'")
        self.lua.execute("""
          raised={};native_saves=0;notices={}
          SaveGame=function() native_saves=native_saves+1 end
          LoadGame=function() native_loads=(native_loads or 0)+1 end
          AddUITriggeredEvent=function(screen,control,param) raised[#raised+1]=screen..'/'..control..'/'..param.slot..'/'..param.file..'/'..param.name end
          local gate=package.loadlib('gate')()
          gate.ui_replay_local=function() return 1 end
          -- the Lua guard arms with the world hash's per-tick step (a play session has one)
          X4LAN_CONFIG.native_gate_world_hash_stride=100000
          gate.world_capture=function(tick) return 'tick='..tick..' state=WORLD_IMPORTED_1:'..tick end
          gate.world_banks=function() return 'test' end
          gate.world_page=function() return 0 end
          gate.world_detail=function() return 0 end
        """)
        self.start()
        # the info line (after the load: a defaults table there would be the defaults module's)
        self.lua.execute("X4LAN_NATIVE_GATE_DEFAULTS={pending={},notice=function(text) notices[#notices+1]=text end}")

    def test_a_persons_save_from_the_menu_is_a_save_of_the_session(self):
        self.player_session()
        self.to(100)
        self.lua.execute("SaveGame('save_003','My save #3')")   # the menu's callbackSave: held by the Lua guard
        g = self.lua.globals()
        self.assertEqual(g.native_saves, 0)
        self.assertEqual(list(g.raised.values()), ['X4LAN_Saves/Request/2/save_003/My_save__3'])
        self.assertTrue(self.rows('PLAYER_SAVE asked from=menu file=save_003 name=My_save__3 slot=2 ok=true at=100'))
        self.lua.execute("SaveGame('save_004','again')")       # one at a time
        self.assertEqual(len(g.raised), 1)
        # every client's MD answers at the apply tick: the request, then its game time
        self.lua.execute("events['X4LAN_Saves.Request'](nil,'2|save_003|My_save__3');events['X4LAN_Saves.RequestTime'](nil,10)")
        self.to(101)
        self.assertTrue(self.rows('PLAYER_SAVE reached name=My_save__3 slot=2 file=save_003 age=10 due=301 save=361 at=101'),
                        self.rows('PLAYER_SAVE'))
        self.to(336)
        self.assertEqual(list(g.calls.values()), ['361,fake/45841/save,0'])
        self.assertEqual(len(self.rows('SAVE_SLOT name=My_save__3-t361 tick=361')), 2)
        self.to(361)
        self.lua.execute("save_text='done|361" + DONE + "'")
        self.to(362)
        self.assertTrue(self.rows('PLAYER_SAVE saved name=My_save__3-t361 tick=361 file=mpsave/s361.xml.gz bytes=1234 sha256=' + 'ab' * 32 +
                                  ' ms=2500 slot=2 save=save_003 at=362'))
        self.assertIn('game saved (My_save__3)', list(g.notices.values()))   # on the client of the person who asked
        self.assertFalse(self.rows('ERROR'))

    def test_another_players_save_is_not_said_here_and_load_stays_off(self):
        self.player_session()
        self.lua.execute("events['X4LAN_Saves.Request'](nil,'1|quicksave|quicksave');events['X4LAN_Saves.RequestTime'](nil,10)")
        self.to(361)
        self.lua.execute("save_text='done|361" + DONE + "'")
        self.to(362)
        g = self.lua.globals()
        self.assertTrue(self.rows('PLAYER_SAVE saved name=quicksave-t361'))
        self.assertNotIn('game saved (quicksave)', list(g.notices.values()))   # player 1 asked: its own screen, not this one
        loads = g.native_loads   # the probe's own load of the session save at the boot
        self.lua.execute("LoadGame('save_003');LoadGame('save_003')")
        self.assertEqual(g.native_loads, loads)
        self.assertEqual(sum('loading a saved game is off' in n for n in g.notices.values()), 1)

    def test_without_saves_a_person_is_told(self):
        self.player_session('false')
        self.to(100)
        self.lua.execute("SaveGame('save_003','x')")
        g = self.lua.globals()
        self.assertEqual((g.native_saves, len(g.raised)), (0, 0))
        self.assertTrue(self.rows('PLAYER_SAVE refused why=off from=menu at=100'))
        self.assertIn('saving is off in this game (a Linux PC or open slots)', list(g.notices.values()))

    def test_bad_config_is_refused_at_load(self):
        for value in ("{save=true}", "{save=1,watch={{'a',1,2}}}", "{save=true,watch={{'a b',1,2}}}", "{save=true,watch={{'a',1,9}}}"):
            self.setUp()
            self.lua.execute('X4LAN_CONFIG.native_gate_milestones=' + value)
            with self.assertRaisesRegex(LuaError, 'native_gate_milestones'):
                self.lua.execute(SOURCE)
        self.setUp()
        self.lua.execute("X4LAN_CONFIG.native_gate_continue={name='x',slots={[2]={inventory={{'a b',1}}}}}")
        with self.assertRaisesRegex(LuaError, 'native_gate_continue'):
            self.lua.execute(SOURCE)

    def test_a_continued_session_claims_the_saved_owners_and_gives_the_inventory(self):
        self.lua.execute("X4LAN_CONFIG.native_gate_continue={name='terran_cadet-M1-slot2',slots={[2]={inventory={{'weapon_x',2}}}}};"
                         "X4LAN_CONFIG.native_gate_pilot_window={40,4000}")
        self.start()
        self.to(2)
        self.assertTrue(self.rows('SAVE_OWNERS dump name=terran_cadet-M1-slot2 no_ui_replay at=2'))
        # what the MD dump sends: the saved table (a cue instance as its text, its runtime ID in hex), then the count
        self.lua.execute("events['X4LAN_Milestones.Owner2'](nil,1615001);events['X4LAN_Milestones.Owner2'](nil,'ID: 1615002');"
                         "events['X4LAN_Milestones.Owner1'](nil,77);events['X4LAN_Milestones.Owner2'](nil,'Ch2_Escort<inst:18a6b2>');"
                         "events['X4LAN_Milestones.OwnersEnd'](nil,4)")
        self.to(31)
        self.assertEqual(list(self.lua.globals().claims.values()), [])
        self.to(32)
        self.assertEqual(list(self.lua.globals().claims.values()), ['2:1615001', '2:1615002', '1:77', '2:1615538'])
        self.assertTrue(self.rows('SAVE_OWNERS restored name=terran_cadet-M1-slot2 heard=4 of=4 claimed=4 refused=0 unreadable=0 at=32 armed=1 owners=3'))
        self.to(48)
        self.assertEqual(list(self.lua.globals().added.values()), ['weapon_x=2'])
        self.assertTrue(self.rows('CONTINUE_INVENTORY name=terran_cadet-M1-slot2 tick=48 slot=2 wares=1 applied=1 now=weapon_x=2'))
        self.assertFalse(self.rows('ERROR'))

    def test_a_cue_without_an_id_is_counted_not_loud(self):
        # what X4 sends for a cue (live, 24 Nov): nil, or its bare name; neither can be claimed
        self.lua.execute("X4LAN_CONFIG.native_gate_continue={name='c1-t3600'}")
        self.start()
        self.lua.execute("events['X4LAN_Milestones.Owner2'](nil,nil);events['X4LAN_Milestones.Owner2'](nil,'SelectMission');"
                         "events['X4LAN_Milestones.Owner2'](nil,1615001);events['X4LAN_Milestones.OwnersEnd'](nil,3)")
        self.to(32)
        self.assertEqual(list(self.lua.globals().claims.values()), ['2:1615001'])
        self.assertTrue(self.rows('SAVE_OWNERS restored name=c1-t3600 heard=3 of=3 claimed=1 refused=0 unreadable=2 at=32'))
        self.assertFalse(self.rows('ERROR'))

    def test_a_continued_session_says_when_owners_are_missing(self):
        self.lua.execute("X4LAN_CONFIG.native_gate_continue={name='terran_cadet-M1-slot2'}")
        self.start()
        self.lua.execute("events['X4LAN_Milestones.Owner2'](nil,1615001);events['X4LAN_Milestones.OwnersEnd'](nil,2)")
        self.to(32)
        self.assertTrue(self.rows('ERROR SAVE_OWNERS_FAILED restored name=terran_cadet-M1-slot2 heard=1 of=2 claimed=1'))


if __name__ == '__main__':
    unittest.main()

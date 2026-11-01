"""Interior nav helpers at the gate's join (lockstep_nav_mesh.h, native_gate_nav_mesh, stop reason 186, default off):
probe arming, the receipt in the declared state ('sync' only), NAV_MESH_ROW drains, status and stop rows, refusals, the
option checker and the registry. The native half (the stubs, the join, the TLS time, the busy deferral, the image facts)
is tested in C: src/native/engine/test_lockstep_nav_mesh.h."""
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ROOT/'tools/dev'))
import test_native_gate_probe as probe  # noqa: E402
import check_gate_options as options  # noqa: E402

HEADER = (ROOT/'src/native/engine/lockstep_nav_mesh.h').read_text(encoding='utf-8')
REGISTRY = (ROOT/'docs/LOCKSTEP-REGISTRY.txt').read_text(encoding='utf-8')
PROBE = (ROOT/'tools/dev/native_gate_probe.lua').read_text(encoding='utf-8')

MOCKS = '''
  local gate=package.loadlib('gate')();order={};frames=0
  gate.nav_mesh=function(mode) order[#order+1]='mesh:'..tostring(mode);return mesh_native or 1 end
  gate.nav_mesh_status=function() return 'mode=sync installed=1 stage=armed' end
  mesh_rows={[0]='index=0 tick=1 kind=generate key=000000000001b5af klass=0 joined=1 time=4030000000000000 us=1200',
             [1]='index=1 tick=1 kind=regenerate key=000000000001b5af klass=0 joined=1 time=4030000000000000 us=300'}
  gate.nav_mesh_row=function(i) local row=mesh_rows[i];mesh_rows[i]=nil;return row end
  gate.nav_mesh_frame=function() frames=frames+1;return 'NMESH_1:2:00000000deadbeef' end
  X4LAN_SHA256.digest=function(data) hashed_data=data;return string.rep('b',64) end
'''


class NavMeshProbeTests(unittest.TestCase):
    setUp = probe.SnapshotProbeTests.setUp

    def run_session(self, mode):
        self.lua.execute("X4LAN_CONFIG.native_gate_nav_mesh='%s'" % mode + MOCKS)
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads, 1)
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(probe.SOURCE);self.lua.execute('update();state.issued=2;update()')
        return list(self.lua.globals().logs.values())

    def test_sync_arms_and_joins_the_receipt_to_the_declared_state(self):
        lines = self.run_session('sync')
        self.assertEqual(list(self.lua.globals().order.values()), ['mesh:sync', 'mesh:sync'])   # again after the UI reload
        self.assertTrue(any(line.endswith('NAV_MESH_ARMED mode=sync') for line in lines))
        self.assertIn('|NMESH_1:2:00000000deadbeef', self.lua.globals().hashed_data)
        self.assertEqual(self.lua.globals().frames, 2)                       # every admitted tick
        self.assertEqual(sum('NAV_MESH_ROW index=0 tick=1 kind=generate' in line for line in lines), 1)
        self.assertEqual(sum('NAV_MESH_ROW index=1 tick=1 kind=regenerate' in line for line in lines), 1)
        self.assertTrue(any('NAV_MESH tick=1 mode=sync installed=1' in line for line in lines))
        self.lua.execute("state.active=0;state.completed=2;setmetatable(state,{__index=function() return 0 end})");self.lua.execute('update()')
        lines = list(self.lua.globals().logs.values())
        self.assertTrue(any('NAV_MESH_STOP mode=sync installed=1' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_observe_logs_rows_and_leaves_the_declared_state_alone(self):
        lines = self.run_session('observe')
        self.assertTrue(any(line.endswith('NAV_MESH_ARMED mode=observe') for line in lines))
        self.assertNotIn('NMESH_1', self.lua.globals().hashed_data)
        self.assertEqual(self.lua.globals().frames, 0)
        self.assertEqual(sum('NAV_MESH_ROW index=0 ' in line for line in lines), 1)

    def test_refusals(self):
        for config in ("X4LAN_CONFIG.native_gate_nav_mesh='deadline'", "X4LAN_CONFIG.native_gate_nav_mesh=true",
                       "X4LAN_CONFIG.native_gate_nav_mesh='sync';mesh_native=0"):
            with self.subTest(config=config):
                self.setUp()
                self.lua.execute(config+MOCKS)
                self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
                self.assertEqual(self.lua.globals().loads, 0)
                self.assertTrue(any('nav mesh refused' in line for line in self.lua.globals().logs.values()), config)


class NavMeshOptionTests(unittest.TestCase):
    BASE = 'X4LAN_CONFIG.native_gate_snapshot_sha = "%s"\n' % ('a'*64)   # a snapshot boot, as every probe-armed option needs

    def failures(self, text):
        return [f['rule'] for f in options.check(text)[1] if 'nav_mesh' in f['rule']]

    def test_modes_need_the_world_identities(self):
        ids = 'X4LAN_CONFIG.native_gate_world_identities = true\n'
        for value in ("'observe'", '"sync"'):
            with self.subTest(value=value):
                self.assertEqual(self.failures(self.BASE+ids+'X4LAN_CONFIG.native_gate_nav_mesh = %s\n' % value), [])
        self.assertEqual(len(self.failures(self.BASE+"X4LAN_CONFIG.native_gate_nav_mesh = 'sync'\n")), 1)
        self.assertEqual(len(self.failures(self.BASE+ids+"X4LAN_CONFIG.native_gate_nav_mesh = 'deadline'\n")), 1)
        self.assertEqual(self.failures(self.BASE+'X4LAN_CONFIG.native_gate_nav_mesh = false\n'), [])


class MirrorTests(unittest.TestCase):
    def test_stop_reason_registered(self):
        self.assertIn('#define NMESH_STOP_REASON 186u', HEADER)
        self.assertRegex(REGISTRY, r'\n  186 NMESH_STOP_REASON +lockstep_nav_mesh\.h')

    def test_receipt_follows_the_runtime_ids_word_in_the_digest(self):
        # The join runs before rid_frame, so the receipt and the next runtime ID describe the same join.
        at_ids = PROBE.index("data=data..'|RUNTIME_IDS_1|'")
        at_mesh = PROBE.index("if cfg.native_gate_nav_mesh=='sync' then data=data..'|'..assert(gate.nav_mesh_frame()")
        self.assertLess(at_ids, at_mesh)
        gate = (ROOT/'src/native/engine/lockstep_gate.c').read_text(encoding='utf-8')
        self.assertLess(gate.index('nmesh_join(stats.completed,NMESH_JOIN_MS)'), gate.index('if(!rid_frame(stats.completed))'))
        self.assertLess(gate.index('noct_join(stats.completed,NOCT_JOIN_MS)'), gate.index('nmesh_join(stats.completed,NMESH_JOIN_MS)'))

    def test_pins_cover_the_loop_and_its_three_sites(self):
        sites = re.findall(r'\{0x([0-9a-f]+)u,NMESH_(GENERATE|REGENERATE|WAIT_IAT),([56])\}', HEADER)
        self.assertEqual(sites, [('108793c', 'GENERATE', '5'), ('1087a76', 'REGENERATE', '5'), ('10878d9', 'WAIT_IAT', '6')])
        self.assertIn('{0x1087810u,0x571u,', HEADER)   # the loop holds all three sites

    def test_builds_close_before_the_load_and_never_time_out_there(self):
        # build162 (footr1/footr2): builds were open until the first join, so the thread slept in X4's build on the busy
        # sets and the first join timed out. Now only the menu (boot phase 0, no tick) lets a take or build run natively;
        # the load's request closes them, a busy ID is deferred at the join, and arming brings an idle thread round.
        self.assertIn('if(boot_mode && !boot_phase && !stats.issued) return 1;', HEADER)
        self.assertIn('nmesh_gate_close_locked(NMESH_GATE_FORCED);', HEADER)
        self.assertIn('if(mode==NMESH_SYNC) ReleaseSemaphore(semaphore,1,NULL);', HEADER)
        self.assertIn('return nmesh_fail("build_busy");', HEADER)
        self.assertIn('nmesh_deferred && nmesh_deferred_seq==nmesh_join_seq', HEADER)


if __name__ == '__main__':
    unittest.main()

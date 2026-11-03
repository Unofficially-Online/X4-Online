"""native_gate_render_fps (lockstep_render_frames.h): probe arming, status rows, the FRAME_TIMING split
and the option checker's prerequisites. The render-only frames themselves are tested in C
(test_lockstep_render_frames.h); nothing here runs X4."""
from pathlib import Path
import importlib.util
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_native_gate_probe as probe

ROOT = Path(__file__).resolve().parents[1]
_SPEC = importlib.util.spec_from_file_location('check_gate_options', ROOT/'tools/dev/check_gate_options.py')
options = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(options)

# What a render-only frame relies on, as the probe and the option checker see it.
PREREQUISITES = '''
  X4LAN_CONFIG.native_gate_tick_hz=30
  X4LAN_CONFIG.native_gate_clients=2
  X4LAN_CONFIG.native_gate_lookahead=1
  X4LAN_CONFIG.native_gate_serial_pathing=true
  X4LAN_CONFIG.native_gate_radar_serial=true
  X4LAN_CONFIG.native_gate_render_rng_split=true
  X4LAN_CONFIG.native_gate_mass_traffic='sync'
  X4LAN_CONFIG.native_gate_region_pass='sync'
'''
MOCKS = '''
  local gate=package.loadlib('gate')();order={}
  local boot=gate.boot
  gate.wire=function(v,n) return 1 end
  gate.lookahead=function(k) return 1 end
  gate.tick_rate=function(hz,pace) order[#order+1]='rate:'..hz;return 1 end
  gate.render_frames=function(fps) order[#order+1]='render:'..fps;return render_refused and 0 or 1 end
  gate.render_frames_status=function() return 'fps=60 eff_fps=60 ready=1 steps='..state.issued end
  gate.boot=function(...) order[#order+1]='boot';return boot(...) end
  gate.render_rng_split=function() return 1 end
  gate.render_rng_split_status=function() return 'mode=split' end
  gate.mass_traffic=function() return 1 end
  gate.mass_traffic_status=function() return 'mode=sync' end
  gate.region_pass=function() return 1 end
  gate.region_pass_status=function() return 'mode=sync' end
'''


class RenderFramesProbeTests(unittest.TestCase):
    setUp = probe.SnapshotProbeTests.setUp

    def start(self, extra):
        self.lua.execute(PREREQUISITES+MOCKS+extra)
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')

    def logs(self):
        return [line for call in self.lua.globals().logs.values() for line in call.split('\n')]

    def test_armed_after_the_tick_rate_before_the_boot(self):
        self.start('X4LAN_CONFIG.native_gate_render_fps=60')
        self.assertEqual(self.lua.globals().loads, 1)
        self.assertEqual(list(self.lua.globals().order.values())[:3], ['rate:30', 'render:60', 'boot'])
        self.assertTrue(any(line.endswith('RENDER_FRAMES_ARMED fps=60') for line in self.logs()))

    def test_a_refused_rate_never_starts_the_gate(self):
        self.start('X4LAN_CONFIG.native_gate_render_fps=60;render_refused=true')
        self.assertEqual(self.lua.globals().loads, 0)
        self.assertNotIn('boot', list(self.lua.globals().order.values()))
        self.assertTrue(any('REFUSED render_fps=60' in line for line in self.logs()))

    def test_missing_prerequisites_fail_before_the_gate_hears_of_it(self):
        for broken in ('X4LAN_CONFIG.native_gate_render_fps=30',                     # not above the tick rate
                       'X4LAN_CONFIG.native_gate_render_fps=60.5',
                       "X4LAN_CONFIG.native_gate_render_fps='60'",
                       'X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_lookahead=0',
                       'X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_serial_pathing=false',
                       'X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_radar_serial=false',
                       'X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_render_rng_split=nil',
                       "X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_mass_traffic='observe'",
                       'X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_region_pass=nil',
                       'X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_runtime_ids=true',
                       'X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_tick_hz=nil'):
            self.setUp()
            self.start(broken)  # the probe's own error handler logs the refusal and stops
            self.assertNotIn('render:60', list(self.lua.globals().order.values()), broken)
            self.assertEqual(self.lua.globals().boots, 0, broken)
            self.assertTrue(any('ERROR' in line and 'native_gate_render_fps needs' in line for line in self.logs()), broken)

    def test_without_the_key_the_gate_never_hears_of_it(self):
        for off in ('', 'X4LAN_CONFIG.native_gate_render_fps=false'):
            self.setUp()
            self.start(off+"\ngate.render_frames=function() error('not configured') end")
            self.assertEqual(self.lua.globals().loads, 1)
            self.assertFalse(any('RENDER_FRAMES' in line for line in self.logs()))


class RenderSplitProbeTests(unittest.TestCase):
    """FRAME_TIMING carries gate.render_split when the gate has it; RENDER_FRAMES rows follow the status stride."""
    setUp = probe.StatusStrideTests.setUp
    run_ticks = probe.StatusStrideTests.run_ticks
    lines = probe.StatusStrideTests.lines
    ticks = staticmethod(probe.StatusStrideTests.ticks)
    MODULES = probe.StatusStrideTests.MODULES

    def test_frame_timing_appends_the_split(self):
        lines = self.run_ticks(3, "gate.render_split=function() return 'slot10_us=4100 sim_rest_us=17900 rframes=1 rframe_us=16000' end")
        timing = [line for line in lines if 'FRAME_TIMING tick=' in line]
        self.assertEqual(len(timing), 3)
        self.assertTrue(all(line.endswith(' slot10_us=4100 sim_rest_us=17900 rframes=1 rframe_us=16000') for line in timing))
        # an older gate without the function: the row is as before
        self.setUp()
        lines = self.run_ticks(2)
        self.assertTrue(all('slot10_us' not in line and line.split('probe_us=')[1].strip().lstrip('-').isdigit()
                            for line in lines if 'FRAME_TIMING tick=' in line))


class RenderFpsOptionTests(unittest.TestCase):
    BASE = ('X4LAN_CONFIG.native_gate_snapshot_sha = "%s"\n' % ('a'*64) +
            'X4LAN_CONFIG.native_gate_tick_hz = 30\nX4LAN_CONFIG.native_gate_lookahead = 1\n'
            'X4LAN_CONFIG.native_gate_serial_pathing = true\nX4LAN_CONFIG.native_gate_radar_diag = true\n'
            'X4LAN_CONFIG.native_gate_radar_serial = true\nX4LAN_CONFIG.native_gate_render_rng_split = true\n'
            "X4LAN_CONFIG.native_gate_mass_traffic = 'sync'\nX4LAN_CONFIG.native_gate_region_pass = true\n")

    def failures(self, text):
        return [f for f in options.check(text)[1] if 'render_fps' in f['rule']]

    def test_rates_and_prerequisites(self):
        for fps in (40, 60, 120, 144, 360):
            self.assertEqual(self.failures(self.BASE+f'X4LAN_CONFIG.native_gate_render_fps = {fps}\n'), [], fps)
        self.assertEqual(self.failures(self.BASE+'X4LAN_CONFIG.native_gate_render_fps = false\n'), [])
        self.assertEqual(self.failures(self.BASE), [])
        for fps in ('30', '20', '361', '60.5', "'60'"):
            self.assertEqual(len(self.failures(self.BASE+f'X4LAN_CONFIG.native_gate_render_fps = {fps}\n')), 1, fps)
        for line in ('native_gate_serial_pathing = true', 'native_gate_radar_serial = true', 'native_gate_render_rng_split = true',
                     "native_gate_mass_traffic = 'sync'", 'native_gate_region_pass = true', 'native_gate_lookahead = 1'):
            text = self.BASE.replace('X4LAN_CONFIG.'+line+'\n', '')+'X4LAN_CONFIG.native_gate_render_fps = 60\n'
            found = self.failures(text)
            self.assertEqual(len(found), 1, line)
            self.assertTrue(any(line.split(' =')[0] in bit for bit in found[0]['bits']), (line, found))
        text = self.BASE+"X4LAN_CONFIG.native_gate_mass_traffic = 'observe'\nX4LAN_CONFIG.native_gate_render_fps = 60\n"
        self.assertEqual(len(self.failures(text.replace("X4LAN_CONFIG.native_gate_mass_traffic = 'sync'\n", ''))), 1)
        ids = self.BASE+'X4LAN_CONFIG.native_gate_runtime_ids = true\nX4LAN_CONFIG.native_gate_render_fps = 60\n'
        self.assertEqual(self.failures(ids)[0]['bits'], ['native_gate_runtime_ids_local'])
        self.assertEqual(self.failures(ids+'X4LAN_CONFIG.native_gate_runtime_ids_local = true\n'), [])
        self.assertEqual(len(self.failures(self.BASE.split('\n', 1)[1]+'X4LAN_CONFIG.native_gate_render_fps = 60\n')), 1)

    def test_the_rule_reads_the_gate_and_probe_values(self):
        source = (ROOT/'src/native/engine/lockstep_render_frames.h').read_text()
        self.assertIn('#define RF_FPS_MAX 360u', source)
        self.assertIn('#define RF_STOP_REASON 182u', source)
        probe_source = probe.SOURCE
        self.assertIn('fps<=360', probe_source)


if __name__ == '__main__':
    unittest.main()

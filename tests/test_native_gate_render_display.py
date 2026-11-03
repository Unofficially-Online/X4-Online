"""native_gate_render_camera / native_gate_render_motion (lockstep_render_display.h): probe arming after the
render-only frames, refusals, status and per-frame rows, and the option checker. The display transforms
themselves are tested in C (test_lockstep_render_display.h); nothing here runs X4."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_native_gate_probe as probe
import test_native_gate_render_frames as frames

ROOT = Path(__file__).resolve().parents[1]
options = frames.options

DISPLAY_MOCKS = '''
  local gate=package.loadlib('gate')()
  gate.render_camera=function(mode) order[#order+1]='camera:'..mode;return camera_refused and 0 or 1 end
  gate.render_motion=function(mode) order[#order+1]='motion:'..mode;return 1 end
  gate.render_display_rows=function() order[#order+1]='rows';return 1 end
  gate.render_display_status=function() return 'camera=extrapolate motion=interpolate armed=1 failures=0 restored=1' end
  display_rows={}
  gate.render_display_row=function(cursor)
    if cursor<#display_rows then return display_rows[cursor+1],cursor+1 end
    return nil
  end
'''


class RenderDisplayProbeTests(unittest.TestCase):
    setUp = probe.SnapshotProbeTests.setUp

    def start(self, extra):
        self.lua.execute(frames.PREREQUISITES+frames.MOCKS+DISPLAY_MOCKS+extra)
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')

    def logs(self):
        return [line for call in self.lua.globals().logs.values() for line in call.split('\n')]

    def order(self):
        return list(self.lua.globals().order.values())

    def test_armed_after_the_render_frames_before_the_boot(self):
        self.start("X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_render_camera=true;"
                   "X4LAN_CONFIG.native_gate_render_motion='interpolate';X4LAN_CONFIG.native_gate_render_display_rows=true")
        self.assertEqual(self.lua.globals().loads, 1)
        self.assertEqual(self.order()[:6], ['rate:30', 'render:60', 'camera:extrapolate', 'motion:interpolate', 'rows', 'boot'])
        self.assertTrue(any(line.endswith('RENDER_CAMERA_ARMED mode=extrapolate') for line in self.logs()))
        self.assertTrue(any(line.endswith('RENDER_MOTION_ARMED mode=interpolate') for line in self.logs()))

    def test_one_stage_alone(self):
        self.start("X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_render_motion='extrapolate'")
        self.assertEqual(self.order()[:4], ['rate:30', 'render:60', 'motion:extrapolate', 'boot'])
        self.assertFalse(any('RENDER_CAMERA' in line for line in self.logs()))

    def test_a_refused_stage_never_starts_the_gate(self):
        self.start('X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_render_camera=true;camera_refused=true')
        self.assertEqual(self.lua.globals().loads, 0)
        self.assertNotIn('boot', self.order())
        self.assertTrue(any('REFUSED render_camera=extrapolate' in line for line in self.logs()))

    def test_bad_values_and_missing_render_frames_fail_before_the_gate_hears_of_it(self):
        for broken in ("X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_render_camera='sideways'",
                       'X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_render_motion=1',
                       'X4LAN_CONFIG.native_gate_render_camera=true',
                       "X4LAN_CONFIG.native_gate_render_motion='interpolate'",
                       'X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_render_display_rows=true'):
            self.setUp()
            self.start(broken)
            self.assertEqual(self.lua.globals().boots, 0, broken)
            self.assertFalse(any(step.startswith(('camera:', 'motion:', 'rows')) for step in self.order()), broken)
            self.assertTrue(any('ERROR' in line and ('native_gate_render_' in line) for line in self.logs()), broken)

    def test_without_the_keys_the_gate_never_hears_of_it(self):
        for off in ('X4LAN_CONFIG.native_gate_render_fps=60',
                    'X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_render_camera=false;X4LAN_CONFIG.native_gate_render_motion=false'):
            self.setUp()
            self.start(off+"\ngate.render_camera=function() error('not configured') end\ngate.render_motion=function() error('not configured') end")
            self.assertEqual(self.lua.globals().loads, 1)
            self.assertFalse(any('RENDER_DISPLAY' in line or 'RENDER_CAMERA' in line for line in self.logs()))


class RenderDisplayRowTests(unittest.TestCase):
    """RENDER_DISPLAY rows follow the status stride; RENDER_DISPLAY_ROW lines drain the gate's ring every tick."""
    setUp = probe.StatusStrideTests.setUp
    run_ticks = probe.StatusStrideTests.run_ticks
    lines = probe.StatusStrideTests.lines
    ticks = staticmethod(probe.StatusStrideTests.ticks)
    MODULES = probe.StatusStrideTests.MODULES

    def arm_then(self, keys, last):
        # The arming is covered above; the keys are set after it (tick 1 arms), so the stride and the rows are seen alone.
        self.run_ticks(1, DISPLAY_MOCKS.replace("local gate=package.loadlib('gate')()", "local gate=package.loadlib('gate')();order={}"))
        self.lua.execute(keys)
        self.lua.execute(f'for tick=2,{last} do state.issued=tick;update() end')
        return self.lines()

    def test_status_and_rows(self):
        lines = self.arm_then("X4LAN_CONFIG.native_gate_render_camera=true;X4LAN_CONFIG.native_gate_render_display_rows=true;"
                              "display_rows={'seq=0 tick=2 kind=render s=0.5','seq=1 tick=3 kind=step s=0'}", 4)
        self.assertEqual(self.ticks(lines, 'RENDER_DISPLAY'), [2, 3, 4])
        rows = [line.split('RENDER_DISPLAY_ROW ')[1] for line in lines if 'RENDER_DISPLAY_ROW ' in line]
        self.assertEqual(rows, ['seq=0 tick=2 kind=render s=0.5', 'seq=1 tick=3 kind=step s=0'])  # each row once
        self.assertTrue(any(' camera=extrapolate motion=interpolate' in line for line in lines if 'RENDER_DISPLAY tick=' in line))
        self.setUp()
        lines = self.arm_then("X4LAN_CONFIG.native_gate_render_motion='extrapolate';display_rows={'seq=0 tick=2 kind=render s=0.5'}", 3)
        self.assertFalse(any('RENDER_DISPLAY_ROW' in line for line in lines))  # rows only with their key
        self.assertEqual(self.ticks(lines, 'RENDER_DISPLAY'), [2, 3])


class RenderDisplayOptionTests(unittest.TestCase):
    BASE = frames.RenderFpsOptionTests.BASE+'X4LAN_CONFIG.native_gate_render_fps = 60\n'

    def failures(self, text):
        return [f for f in options.check(text)[1] if 'render_camera' in f['rule'] or 'render_motion' in f['rule'] or 'render_display' in f['rule']]

    def test_values_and_prerequisites(self):
        for value in ('true', "'extrapolate'", "'interpolate'", '"interpolate"', 'false'):
            for key in ('native_gate_render_camera', 'native_gate_render_motion'):
                self.assertEqual(self.failures(self.BASE+f'X4LAN_CONFIG.{key} = {value}\n'), [], (key, value))
        for value in ("'sideways'", '1', "'Extrapolate'"):
            self.assertEqual(len(self.failures(self.BASE+f'X4LAN_CONFIG.native_gate_render_camera = {value}\n')), 1, value)
        without = self.BASE.replace('X4LAN_CONFIG.native_gate_render_fps = 60\n', '')
        found = self.failures(without+"X4LAN_CONFIG.native_gate_render_motion = 'interpolate'\n")
        self.assertEqual([f['bits'] for f in found], [['native_gate_render_fps']])
        self.assertEqual(len(self.failures(self.BASE+'X4LAN_CONFIG.native_gate_render_display_rows = true\n')), 1)
        self.assertEqual(self.failures(self.BASE+'X4LAN_CONFIG.native_gate_render_camera = true\nX4LAN_CONFIG.native_gate_render_display_rows = true\n'), [])

    def test_the_rule_reads_the_gate_and_probe_values(self):
        source = (ROOT/'src/native/engine/lockstep_render_display.h').read_text()
        self.assertIn('#define RDISP_STOP_REASON 183u', source)
        self.assertIn('"extrapolate"', source)
        self.assertIn('"interpolate"', source)
        lua = (ROOT/'src/native/engine/lockstep_render_display_lua.h').read_text()
        for name in ('render_camera', 'render_motion', 'render_display_rows', 'render_display_status', 'render_display_row'):
            self.assertIn('lua_'+name+'(lua_State', lua)
            self.assertIn('METHOD('+name+')', (ROOT/'src/native/engine/lockstep_gate.c').read_text())
        self.assertIn("mode=='extrapolate' or mode=='interpolate'", probe.SOURCE)


if __name__ == '__main__':
    unittest.main()

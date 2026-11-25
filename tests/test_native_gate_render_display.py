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



SMOOTH_MOCKS = DISPLAY_MOCKS + """
  gate.render_radar=function(mode) order[#order+1]='radar:'..mode;return radar_refused and 0 or 1 end
  gate.render_shots=function(mode) order[#order+1]='shots:'..mode;return 1 end
  gate.render_clock=function(mode) order[#order+1]='clock:'..mode;return 1 end
  gate.render_radar_status=function() return 'mode=rotate armed=1 active=1 stage=live why=- found=1 model=12/0/0 failures=0 camera=1:abc' end
  gate.render_shots_status=function() return 'mode=lead armed=1 active=1 stage=live why=- items=4 swapped=8 failures=0' end
"""


class RenderSmoothProbeTests(unittest.TestCase):
    """native_gate_render_radar / native_gate_render_shots (lockstep_render_radar.h, lockstep_render_shots.h): armed after
    the render stages, before the boot; refusals; the status rows. The stages themselves are tested in C."""
    setUp = probe.SnapshotProbeTests.setUp
    MOTION = "X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_render_motion='interpolate';X4LAN_CONFIG.native_gate_bullet_rng=true;"

    def start(self, extra):
        self.lua.execute(frames.PREREQUISITES+frames.MOCKS+SMOOTH_MOCKS+extra)
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')

    logs = RenderDisplayProbeTests.logs
    order = RenderDisplayProbeTests.order

    def test_armed_after_the_render_stages_before_the_boot(self):
        self.start(self.MOTION+'X4LAN_CONFIG.native_gate_render_radar=true;X4LAN_CONFIG.native_gate_render_shots=true')
        self.assertEqual(self.lua.globals().loads, 1)
        self.assertEqual(self.order()[:5], ['rate:30', 'render:60', 'motion:interpolate', 'radar:rotate', 'shots:lead'])
        self.assertIn('boot', self.order())
        self.assertTrue(any(line.endswith('RENDER_RADAR_ARMED mode=rotate') for line in self.logs()))
        self.assertTrue(any(line.endswith('RENDER_SHOTS_ARMED mode=lead') for line in self.logs()))

    def test_the_other_modes(self):
        self.start(self.MOTION+"X4LAN_CONFIG.native_gate_render_radar='observe';X4LAN_CONFIG.native_gate_render_shots='probe'")
        self.assertEqual(self.order()[3:5], ['radar:observe', 'shots:probe'])
        self.setUp()
        self.start(self.MOTION+"X4LAN_CONFIG.native_gate_render_shots='motion'")
        self.assertEqual(self.order()[3], 'shots:motion')
        self.assertFalse(any(step.startswith('radar:') for step in self.order()))

    def test_the_steady_clock_is_armed_before_the_stages_and_gap_is_the_default(self):
        self.start(self.MOTION+"X4LAN_CONFIG.native_gate_render_clock='steady';X4LAN_CONFIG.native_gate_render_radar=true")
        self.assertEqual(self.order()[:5], ['rate:30', 'render:60', 'motion:interpolate', 'clock:steady', 'radar:rotate'])
        self.assertTrue(any(line.endswith('RENDER_CLOCK_ARMED mode=steady') for line in self.logs()))
        for gap in ("X4LAN_CONFIG.native_gate_render_clock='gap'", ''):
            self.setUp()
            self.start(self.MOTION+gap)
            self.assertFalse(any(step.startswith('clock:') for step in self.order()), gap)
            self.assertIn('boot', self.order())
        for broken in (self.MOTION+"X4LAN_CONFIG.native_gate_render_clock='smooth'", "X4LAN_CONFIG.native_gate_render_clock='steady'"):
            self.setUp()
            self.start(broken)
            self.assertEqual(self.lua.globals().boots, 0, broken)
            self.assertTrue(any('ERROR' in line and 'native_gate_render_clock' in line for line in self.logs()), broken)

    def test_the_radar_alone_needs_no_render_frames(self):
        self.start('X4LAN_CONFIG.native_gate_render_radar=true')
        self.assertEqual(self.lua.globals().loads, 1)
        self.assertEqual(self.order()[:2], ['rate:30', 'radar:rotate'])

    def test_a_refused_stage_never_starts_the_gate(self):
        self.start('X4LAN_CONFIG.native_gate_render_radar=true;radar_refused=true')
        self.assertEqual(self.lua.globals().loads, 0)
        self.assertNotIn('boot', self.order())
        self.assertTrue(any('REFUSED render_radar=rotate' in line for line in self.logs()))

    def test_bad_values_and_missing_prerequisites_fail_before_the_gate_hears_of_it(self):
        for broken in ("X4LAN_CONFIG.native_gate_render_radar='sideways'",
                       'X4LAN_CONFIG.native_gate_render_rng_split=false;X4LAN_CONFIG.native_gate_render_radar=true',
                       self.MOTION+"X4LAN_CONFIG.native_gate_render_shots='fast'",
                       "X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_bullet_rng=true;X4LAN_CONFIG.native_gate_render_shots=true",
                       "X4LAN_CONFIG.native_gate_render_fps=60;X4LAN_CONFIG.native_gate_render_motion='interpolate';X4LAN_CONFIG.native_gate_render_shots=true"):
            self.setUp()
            self.start(broken)
            self.assertEqual(self.lua.globals().boots, 0, broken)
            self.assertFalse(any(step.startswith(('radar:', 'shots:')) for step in self.order()), broken)
            self.assertTrue(any('ERROR' in line and 'native_gate_render_' in line for line in self.logs()), broken)


class RenderSmoothRowTests(unittest.TestCase):
    """RENDER_RADAR and RENDER_SHOTS rows follow the status stride."""
    setUp = probe.StatusStrideTests.setUp
    run_ticks = probe.StatusStrideTests.run_ticks
    lines = probe.StatusStrideTests.lines
    ticks = staticmethod(probe.StatusStrideTests.ticks)
    MODULES = probe.StatusStrideTests.MODULES

    def test_status_rows(self):
        self.run_ticks(1, SMOOTH_MOCKS.replace("local gate=package.loadlib('gate')()", "local gate=package.loadlib('gate')();order={}"))
        self.lua.execute('X4LAN_CONFIG.native_gate_render_radar=true;X4LAN_CONFIG.native_gate_render_shots=true')
        self.lua.execute('for tick=2,4 do state.issued=tick;update() end')
        lines = self.lines()
        self.assertEqual(self.ticks(lines, 'RENDER_RADAR'), [2, 3, 4])
        self.assertEqual(self.ticks(lines, 'RENDER_SHOTS'), [2, 3, 4])
        self.assertTrue(any(' camera=1:abc' in line for line in lines if 'RENDER_RADAR tick=' in line))


class RenderSmoothOptionTests(unittest.TestCase):
    BASE = RenderDisplayOptionTests.BASE

    def failures(self, text):
        return [f for f in options.check(text)[1] if 'render_radar' in f['rule'] or 'render_shots' in f['rule'] or 'render_clock' in f['rule']]

    def test_values_and_prerequisites(self):
        for value in ('true', "'rotate'", "'observe'", 'false'):
            self.assertEqual(self.failures(self.BASE+f'X4LAN_CONFIG.native_gate_render_radar = {value}\n'), [], value)
        for value in ("'sideways'", '1'):
            self.assertEqual(len(self.failures(self.BASE+f'X4LAN_CONFIG.native_gate_render_radar = {value}\n')), 1, value)
        motion = self.BASE+"X4LAN_CONFIG.native_gate_render_motion = 'interpolate'\nX4LAN_CONFIG.native_gate_bullet_rng = true\n"
        for value in ('true', "'lead'", "'motion'", "'probe'", 'false'):
            self.assertEqual(self.failures(motion+f'X4LAN_CONFIG.native_gate_render_shots = {value}\n'), [], value)
        self.assertEqual(len(self.failures(motion+"X4LAN_CONFIG.native_gate_render_shots = 'fast'\n")), 1)
        found = self.failures(self.BASE+'X4LAN_CONFIG.native_gate_bullet_rng = true\nX4LAN_CONFIG.native_gate_render_shots = true\n')
        self.assertEqual([f['bits'] for f in found], [['native_gate_render_motion']])
        found = self.failures(self.BASE+"X4LAN_CONFIG.native_gate_render_motion = 'interpolate'\nX4LAN_CONFIG.native_gate_render_shots = true\n")
        self.assertEqual([f['bits'] for f in found], [['native_gate_bullet_rng']])
        for value in ("'gap'", "'steady'"):
            self.assertEqual(self.failures(self.BASE+f'X4LAN_CONFIG.native_gate_render_clock = {value}\n'), [], value)
        self.assertEqual(len(self.failures(self.BASE+"X4LAN_CONFIG.native_gate_render_clock = 'smooth'\n")), 1)
        without = self.BASE.replace('X4LAN_CONFIG.native_gate_render_fps = 60\n', '')
        found = self.failures(without+"X4LAN_CONFIG.native_gate_render_clock = 'steady'\n")
        self.assertEqual([f['bits'] for f in found], [['native_gate_render_fps']])

    def test_the_rules_read_the_gate_and_probe_values(self):
        radar = (ROOT/'src/native/engine/lockstep_render_radar.h').read_text()
        shots = (ROOT/'src/native/engine/lockstep_render_shots.h').read_text()
        self.assertIn('#define RRADAR_STOP_REASON 230u', radar)
        self.assertIn('#define RSHOT_STOP_REASON 231u', shots)
        for word in ('"rotate"', '"observe"'):
            self.assertIn(word, radar)
        for word in ('"lead"', '"motion"', '"probe"'):
            self.assertIn(word, shots)
        lua = (ROOT/'src/native/engine/lockstep_render_display_lua.h').read_text()
        gate = (ROOT/'src/native/engine/lockstep_gate.c').read_text()
        for name in ('render_radar', 'render_radar_status', 'render_shots', 'render_shots_status', 'render_clock'):
            self.assertIn('lua_'+name+'(lua_State', lua)
            self.assertIn('METHOD('+name+')', gate)
        self.assertIn('rradar_check()) {finish(RRADAR_STOP_REASON)', gate)
        self.assertIn('rshot_check()) {finish(RSHOT_STOP_REASON)', gate)
        self.assertIn("mode=='rotate' or mode=='observe'", probe.SOURCE)
        self.assertIn("mode=='lead' or mode=='motion' or mode=='probe'", probe.SOURCE)
        display = (ROOT/'src/native/engine/lockstep_render_display.h').read_text()
        self.assertIn('"gap"', display)
        self.assertIn('"steady"', display)
        self.assertIn("mode=='steady' and cfg.native_gate_render_fps", probe.SOURCE)


if __name__ == '__main__':
    unittest.main()

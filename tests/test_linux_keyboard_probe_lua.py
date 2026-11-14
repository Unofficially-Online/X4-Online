"""Run the canonical keyboard probe under LuaJIT with engine/gate mocks."""
from pathlib import Path
import unittest

from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'tools/dev/native_gate_linux_keyboard.lua'
CANONICAL = '0000000000000011'
NEUTRAL = '0000000000000022'


class LinuxKeyboardProbeLuaTests(unittest.TestCase):
    def test_factory_admission_requires_explicit_independent_keyboard(self):
        source = (ROOT / 'tools/dev/native_gate_probe.lua').read_text()
        start = source.index('if cfg.native_gate_player_factory_probe then\n')
        end = source.index('\nif cfg.native_gate_player_reference_probe then', start)
        admission = source[start:end]
        for mutation, accepted in (
            ('', True),
            ('keyboard.independent_players=false', False),
            ('keyboard.delay=3', False),
            ('keyboard.lookahead=1', False),
            ('cfg.native_gate_lookahead=1', False),
            ('cfg.native_gate_clients=1', False),
            ('cfg.native_gate_stock_keyboard_replay_probe=true', False),
            ('cfg.native_gate_stock_keyboard_replay_probe=nil', True),
            ('cfg.native_gate_player_reference_probe=true', False),
        ):
            with self.subTest(mutation=mutation):
                lua = self.runtime()
                lua.execute('''
                    cfg.native_gate_player_factory_probe=true
                    cfg.native_gate_serial_pathing=true
                    cfg.native_gate_snapshot_sha='fixture'
                    gate.factory_probe_request=function() end
                    gate.factory_probe_status=function() end
                    keyboard=cfg.native_gate_stock_keyboard_replay_probe
                    keyboard.independent_players=true
                    keyboard.delay=2
                    keyboard.lookahead=0
                ''')
                lua.execute(mutation)
                if accepted:
                    lua.execute(admission)
                else:
                    with self.assertRaisesRegex(Exception, 'player factory diagnostic'):
                        lua.execute(admission)

    def runtime(self, slot='p1', runtime_id=74565):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.globals().initial_slot = slot
        lua.globals().initial_runtime = runtime_id
        lua.execute('''
            ffi = require('ffi')
            runtime = ffi.new('uint64_t', initial_runtime)
            operational = true
            position = {x=1.25,y=-2,z=3,yaw=4,pitch=5,roll=6}
            saved = '0000000000000011'
            raw_status = 'problem=active restored=0'
            request_result = 1
            capture_result = '5301aa'
            requests, stages, logs, sections = {}, {}, {}, {}
            capture_calls, pose_calls, status_calls = 0, 0, 0
            saved_argument = nil
            refused_slot = 0
            cfg = {
                slot=initial_slot, native_gate_clients=2,
                native_gate_stock_keyboard_observer=true,
                native_gate_stock_keyboard_replay_probe={
                    last_capture=18, neutral_ship='0000000000000022'},
                native_gate_lookahead=0,
            }
            gate = {
                platform=function() return 'linux' end,
                saved_identity=function(value) saved_argument=value; return saved end,
                stock_keyboard_replay_status=function()
                    status_calls=status_calls+1
                    return raw_status
                end,
                stock_keyboard_replay_request=function(...)
                    requests[#requests+1]={...}
                    return request_result
                end,
                stock_keyboard_replay_capture=function()
                    capture_calls=capture_calls+1
                    return capture_result
                end,
                stock_keyboard_replay_stage=function(slot, payload)
                    stages[#stages+1]={slot, payload}
                    if slot==refused_slot then return 0 end
                    -- The mock native decoder binds each record to its source.
                    if payload~='source'..slot then return 0 end
                    return 1
                end,
            }
            engine = {C={
                GetPlayerOccupiedShipID=function() return runtime end,
                IsComponentOperational=function(value)
                    assert(value==runtime)
                    return operational
                end,
                GetObjectPositionInSector=function(value)
                    assert(value==runtime)
                    pose_calls=pose_calls+1
                    return position
                end,
            }}
            register=function(name, owner, adapter)
                sections[#sections+1]={name=name, owner=owner, adapter=adapter}
            end
            logger=function(text) logs[#logs+1]=text end
        ''')
        lua.execute(SOURCE.read_text())
        return lua

    def create(self, lua):
        lua.execute('probe=X4LAN_LINUX_KEYBOARD.create(gate,cfg,register,engine,logger)')

    def start(self, lua):
        self.create(lua)
        return lua.eval('probe.frame(1)')

    def test_slot_and_source_contract(self):
        for slot in ('p0', 'p3', 'p11', 'p1 ', 'P1', '', 1):
            with self.subTest(slot=slot):
                lua = self.runtime(slot)
                with self.assertRaises(Exception):
                    self.create(lua)
                self.assertEqual(len(lua.globals().requests), 0)
        for change in (
            'cfg.native_gate_clients=1',
            'cfg.native_gate_stock_keyboard_observer=false',
            'gate.platform=function() return "windows" end',
            'cfg.native_gate_pilot_inputs=true',
            'cfg.native_gate_player_probe=true',
        ):
            with self.subTest(change=change):
                lua = self.runtime()
                lua.execute(change)
                with self.assertRaises(Exception):
                    self.create(lua)
        for slot in ('p1', 'p2'):
            lua = self.runtime(slot)
            self.start(lua)
            request = lua.globals().requests[1]
            self.assertEqual([request[index] for index in range(1, 7)],
                             [int(slot[1]), 18, 2, 0, CANONICAL, NEUTRAL])
            self.assertEqual(lua.globals().saved_argument, '74565')
            section = lua.globals().sections[1]
            self.assertEqual((section.name, section.owner), ('stock', 'linux_canonical_keyboard'))

    def test_independent_flag_is_explicit_and_preserves_canonical_default(self):
        for value in (None, False):
            lua = self.runtime()
            lua.globals().cfg.native_gate_stock_keyboard_replay_probe.independent_players = value
            self.start(lua)
            self.assertEqual(lua.eval('#requests[1]'), 6)
            self.assertEqual(lua.globals().sections[1].owner, 'linux_canonical_keyboard')
        for slot in ('p1', 'p2'):
            lua = self.runtime(slot)
            lua.globals().cfg.native_gate_stock_keyboard_replay_probe.independent_players = True
            self.start(lua)
            self.assertEqual(lua.eval('#requests[1]'), 8)
            self.assertEqual(lua.globals().requests[1][7], 1)
            self.assertEqual(lua.globals().requests[1][8], 0)  # explicit legacy startup version
            self.assertEqual(lua.globals().sections[1].owner, 'linux_independent_keyboard')
            self.assertTrue(any('mode=independent' in row and 'p2=' + NEUTRAL in row
                                for row in lua.globals().logs.values()))
            self.assertEqual(lua.eval('probe.frame(2)'),
                             'LINUX_KEYBOARD_SHIP_1:0000000000000011/1.25/-2/3/4/5/6')
        for value in (1, 0, 'true', 'false'):
            lua = self.runtime()
            lua.globals().cfg.native_gate_stock_keyboard_replay_probe.independent_players = value
            with self.assertRaisesRegex(Exception, 'independent_players must be boolean'):
                self.create(lua)

    def test_numeric_configuration_is_finite_and_integral(self):
        for value in (float('nan'), float('inf'), -float('inf'), -1, 1, 8, 9, 0.5, '0'):
            with self.subTest(lookahead=value):
                lua = self.runtime()
                lua.globals().cfg.native_gate_lookahead = value
                with self.assertRaises(Exception):
                    self.create(lua)
        for value in (float('nan'), float('inf'), -float('inf'), 2, 3.5, '18'):
            with self.subTest(last_capture=value):
                lua = self.runtime()
                lua.globals().cfg.native_gate_stock_keyboard_replay_probe.last_capture = value
                with self.assertRaises(Exception):
                    self.create(lua)
        for lookahead in (0, None):
            lua = self.runtime()
            lua.globals().cfg.native_gate_lookahead = lookahead
            self.start(lua)
            request = lua.globals().requests[1]
            self.assertEqual((request[3], request[4]), (2, 0))

    def test_external_camera_is_explicit_and_runs_once_for_slot_two(self):
        for slot in ('p1', 'p2'):
            for enabled in (None, False, True):
                with self.subTest(slot=slot, enabled=enabled):
                    lua = self.runtime(slot)
                    lua.execute('''
                        cfg.native_gate_stock_keyboard_replay_probe.independent_players=true
                        camera_calls=0
                        gate.player_external_camera_probe=function()
                            camera_calls=camera_calls+1
                            return 1
                        end
                    ''')
                    lua.globals().cfg.native_gate_linux_external_camera_probe = enabled
                    self.start(lua)
                    for tick in range(2, 9):
                        lua.eval(f'probe.frame({tick})')
                    expected_calls = int(slot == 'p2' and enabled is True)
                    self.assertEqual(lua.globals().camera_calls, expected_calls)
                    if expected_calls:
                        with self.assertRaisesRegex(Exception, 'cannot be retried'):
                            lua.eval('probe.frame(6)')
                        self.assertEqual(lua.globals().camera_calls, 1)

    def test_external_camera_refuses_invalid_configuration_and_native_failure(self):
        for change in (
            'cfg.native_gate_stock_keyboard_replay_probe.independent_players=false',
            'cfg.native_gate_stock_keyboard_replay_probe.last_capture=4',
            'gate.player_external_camera_probe=nil',
            'cfg.native_gate_linux_external_camera_probe=1',
            'cfg.native_gate_linux_external_camera_probe="true"',
        ):
            with self.subTest(change=change):
                lua = self.runtime('p2')
                lua.execute('''
                    cfg.native_gate_stock_keyboard_replay_probe.independent_players=true
                    cfg.native_gate_linux_external_camera_probe=true
                    gate.player_external_camera_probe=function() return 1 end
                ''')
                lua.execute(change)
                with self.assertRaisesRegex(Exception, 'external camera probe'):
                    self.create(lua)
        lua = self.runtime('p2')
        lua.execute('''
            cfg.native_gate_stock_keyboard_replay_probe.independent_players=true
            cfg.native_gate_linux_external_camera_probe=true
            camera_calls=0
            gate.player_external_camera_probe=function()
                camera_calls=camera_calls+1
                return 0
            end
        ''')
        self.start(lua)
        with self.assertRaisesRegex(Exception, 'external camera probe refused'):
            lua.eval('probe.frame(6)')
        with self.assertRaisesRegex(Exception, 'cannot be retried'):
            lua.eval('probe.frame(6)')
        self.assertEqual(lua.globals().camera_calls, 1)

    def test_neutral_identity_and_first_frame_refusals(self):
        for neutral in ('', '0' * 16, 'ABCDEF1234567890', '1234', 22):
            lua = self.runtime()
            lua.globals().cfg.native_gate_stock_keyboard_replay_probe.neutral_ship = neutral
            with self.assertRaises(Exception):
                self.create(lua)
        lua = self.runtime()
        lua.globals().saved = NEUTRAL
        self.create(lua)
        with self.assertRaisesRegex(Exception, 'canonical/neutral identity collision'):
            lua.eval('probe.frame(1)')
        self.assertEqual(len(lua.globals().requests), 0)
        lua = self.runtime()
        lua.execute('saved=nil')
        self.create(lua)
        with self.assertRaises(Exception):
            lua.eval('probe.frame(1)')
        self.assertEqual(len(lua.globals().requests), 0)
        lua = self.runtime()
        self.create(lua)
        with self.assertRaisesRegex(Exception, 'start at tick one'):
            lua.eval('probe.frame(2)')
        self.assertEqual(len(lua.globals().requests), 0)

    def test_request_refusal_preserves_native_status(self):
        lua = self.runtime()
        lua.execute("request_result=0; raw_status='problem=request_boundary restored=0'")
        self.create(lua)
        with self.assertRaisesRegex(Exception, 'keyboard request refused: problem=request_boundary restored=0'):
            lua.eval('probe.frame(1)')
        self.assertEqual(len(lua.globals().sections), 0)
        self.assertEqual(lua.globals().pose_calls, 0)
        self.assertFalse(any('ARMED' in line for line in lua.globals().logs.values()))

    def test_capture_window_and_native_capture_refusal(self):
        lua = self.runtime()
        self.start(lua)
        for tick in (1, 2, 19, 20):
            self.assertIsNone(lua.eval(f'sections[1].adapter.proposal({tick})'))
        self.assertEqual(lua.globals().capture_calls, 0)
        for tick in (3, 18):
            self.assertEqual(lua.eval(f'sections[1].adapter.proposal({tick})'), '5301aa')
        self.assertEqual(lua.globals().capture_calls, 2)
        self.assertTrue(any('CAPTURE tick=18 record=5301aa' in line
                            for line in lua.globals().logs.values()))
        lua.execute("capture_result=nil; raw_status='problem=capture_seal'")
        with self.assertRaisesRegex(Exception, 'capture refused: problem=capture_seal'):
            lua.eval('sections[1].adapter.proposal(4)')

    def test_both_source_records_stage_in_order_and_fail_strictly(self):
        lua = self.runtime()
        self.start(lua)
        result = lua.eval("sections[1].adapter.apply(6, {'source1','source2'})")
        self.assertEqual(result, 'linux_keyboard=2')
        self.assertEqual([[row[1], row[2]] for row in lua.globals().stages.values()],
                         [[1, 'source1'], [2, 'source2']])
        for refused_slot in (1, 2):
            lua = self.runtime()
            self.start(lua)
            lua.globals().refused_slot = refused_slot
            with self.assertRaisesRegex(Exception, 'keyboard stage refused'):
                lua.eval("sections[1].adapter.apply(6, {'source1','source2'})")
            self.assertEqual(len(lua.globals().stages), refused_slot)
        lua = self.runtime()
        self.start(lua)
        with self.assertRaisesRegex(Exception, 'keyboard stage refused'):
            lua.eval("sections[1].adapter.apply(6, {'source2','source1'})")

    def test_pose_is_shared_saved_identity_and_complete_position(self):
        rows = []
        for slot, runtime in (('p1', 74565), ('p2', 354185)):
            lua = self.runtime(slot, runtime)
            row = self.start(lua)
            rows.append(row)
            self.assertEqual(row, 'LINUX_KEYBOARD_SHIP_1:0000000000000011/1.25/-2/3/4/5/6')
            self.assertNotIn(str(runtime), row)
            self.assertEqual(lua.globals().pose_calls, 1)
        self.assertEqual(rows[0], rows[1])

    def test_each_pose_field_rejects_nonfinite_or_nonnumeric_values(self):
        for field in ('x', 'y', 'z', 'yaw', 'pitch', 'roll'):
            for value in (float('nan'), float('inf'), -float('inf'), '1', None):
                with self.subTest(field=field, value=value):
                    lua = self.runtime()
                    lua.globals().position[field] = value
                    self.create(lua)
                    with self.assertRaisesRegex(Exception, 'nonfinite canonical keyboard pose: ' + field):
                        lua.eval('probe.frame(1)')
                    self.assertFalse(any('LINUX_KEYBOARD_SHIP ' in line
                                         for line in lua.globals().logs.values()))

    def test_changed_or_unavailable_canonical_ship_fails_before_pose(self):
        for mutation, reason in (
            ("runtime=ffi.new('uint64_t',1234)", 'canonical keyboard ship changed'),
            ('operational=false', 'canonical keyboard ship unavailable'),
        ):
            lua = self.runtime()
            self.start(lua)
            lua.execute(mutation)
            with self.assertRaisesRegex(Exception, reason):
                lua.eval('probe.frame(2)')
            self.assertEqual(lua.globals().pose_calls, 1)
            self.assertEqual(len(lua.globals().requests), 1)
        lua = self.runtime()
        self.start(lua)
        lua.globals().raw_status = 'problem=native_failure restored=0'
        lua.eval('probe.stop()')
        self.assertEqual(lua.globals().logs[len(lua.globals().logs)],
                         'LINUX_KEYBOARD_STOP problem=native_failure restored=0')


if __name__ == '__main__':
    unittest.main()

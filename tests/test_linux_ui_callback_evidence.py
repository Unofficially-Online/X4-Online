import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_gate_live as live
from test_linux_presentation_probe import receipt
import test_stock_keyboard_replay_overlay as keyboard


class UiCallbackEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.payload = ('v=1 owner=42 object=20008 move=5/5/3/0/0/0/0/0/42 '
                        'render=4/4/1/0/0/0/0/0/42')
        self.engine = ['LINUX_VIEW_QUERY_ARMED tick=7 slot=2',
                       'LINUX_UI_CALLBACK_CLOSE tick=183 slot=2',
                       'LINUX_UI_CALLBACK_RESULT tick=184 slot=2 ' + self.payload]
        self.stderr = ['X4 UI CALLBACK RESULT ' + self.payload]
        self.presentation = receipt(2)

    def parse(self, engine=None, stderr=None, presentation=None):
        return live.parse_ui_callback_evidence(self.engine if engine is None else engine,
            self.stderr if stderr is None else stderr,
            self.presentation if presentation is None else presentation, 2, 180)

    def test_admission_and_frozen_config(self):
        options = {'ui_callback_observer': True, 'player_view_query_observer': True,
                   'player_presentation_probe': True}
        self.assertTrue(live.validate_ui_callback_observer(options, 5))
        self.assertFalse(live.validate_ui_callback_observer({}, 0))
        for changed in ({**options, 'ui_callback_observer': 1},
                        {**options, 'player_view_query_observer': False},
                        {**options, 'player_presentation_probe': False}):
            with self.assertRaises(SystemExit):
                live.validate_ui_callback_observer(changed, 5)
        with self.assertRaises(SystemExit):
            live.validate_ui_callback_observer(options, 4)
        line = 'X4LAN_CONFIG.native_gate_linux_ui_callback_observer = true\n'
        live.validate_ui_callback_config(line, True)
        with self.assertRaises(SystemExit):
            live.validate_ui_callback_config(line + line, True)

    def test_overlay_flag_requires_dependencies(self):
        fixture = keyboard.StockKeyboardReplayOverlayTests()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        common = ('--stock-keyboard-observer', '--serial-pathing', '--player-factory-probe',
                  '--stock-keyboard-replay-probe', '--independent-keyboard-frames', '5',
                  '--stock-keyboard-replay-last-capture', '7')
        with self.assertRaises(SystemExit):
            fixture.build(*common, '--ui-callback-observer')
        config = fixture.build(*common, '--player-view-query-observer',
                               '--player-presentation-probe', '--ui-callback-observer') / 'ui/config.lua.append'
        self.assertIn('X4LAN_CONFIG.native_gate_linux_ui_callback_observer = true', config.read_text())

    def test_strict_drained_receipt_and_presentation_binding(self):
        self.assertTrue(self.parse()['pass'])
        for index in range(3):
            lines = self.engine[:index] + self.engine[index + 1:]
            self.assertFalse(self.parse(engine=lines)['pass'])
        self.assertFalse(self.parse(engine=[self.engine[1], self.engine[0], self.engine[2]])['pass'])
        self.assertFalse(self.parse(stderr=self.stderr * 2)['pass'])
        self.assertFalse(self.parse(presentation=[])['pass'])
        self.assertFalse(self.parse(engine=[x.replace('object=20008', 'object=20010') for x in self.engine],
                                    stderr=[x.replace('object=20008', 'object=20010') for x in self.stderr])['pass'])
        for changed in ('move=5/4/3', 'move=5/5/3/1', 'render=0/0/0', 'render=4/4/1/0/1'):
            old = 'move=5/5/3' if changed.startswith('move') else 'render=4/4/1/0/0'
            payload = self.payload.replace(old, changed)
            engine = self.engine[:2] + [self.engine[2].replace(self.payload, payload)]
            self.assertFalse(self.parse(engine=engine, stderr=['X4 UI CALLBACK RESULT ' + payload])['pass'])

    def test_malformed_numbers_and_wrong_arm_are_rejected(self):
        for replacement in ('5//3', '/5/3', '5/5/3/', '9' * 21 + '/5/3'):
            payload = self.payload.replace('5/5/3', replacement)
            engine = self.engine[:2] + [self.engine[2].replace(self.payload, payload)]
            self.assertFalse(self.parse(engine=engine, stderr=['X4 UI CALLBACK RESULT ' + payload])['pass'])
        for arm in ('LINUX_VIEW_QUERY_ARMED tick=8 slot=2',
                    'LINUX_VIEW_QUERY_ARMED tick=7 slot=1'):
            self.assertFalse(self.parse(engine=[arm] + self.engine[1:])['pass'])

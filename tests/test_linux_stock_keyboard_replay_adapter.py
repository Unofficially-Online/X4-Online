"""Engine-free regressions for the optional native keyboard replay adapter."""
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
ENGINE = ROOT / "src/native/engine/linux"


class StockKeyboardReplayAdapterTests(unittest.TestCase):
    def test_adapter_boundaries(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            executable = pathlib.Path(temporary_directory) / "adapter-test"
            subprocess.run(
                ["cc", "-std=c11", "-I", str(ENGINE),
                 str(ENGINE / "test_stock_keyboard_replay_adapter.c"),
                 "-lm", "-o", str(executable)],
                check=True,
            )
            for case in ("independent_vertical", "vertical_bad_readback", "vertical_suppressed",
                         "default_vertical_refused", "independent_strafe", "strafe_suppressed", "strafe_bad_readback", "strafe_unreadable",
                         "default_strafe_refused", "strafe_invalid", "strafe_value", "default", "inflight", "held", "badowner", "initial",
                         "wrongphase", "capture", "stage", "press_release",
                         "bad_readback", "binding_universe", "binding_input_owner",
                         "binding_player", "binding_player_runtime",
                         "binding_player_saved", "binding_controller",
                         "binding_ship", "binding_runtime", "binding_saved",
                         "request_default", "request_independent", "independent_slot1",
                         "independent_slot2", "independent_empty", "independent_empty_p1", "independent_p1_held",
                         "independent_frame6",
                         "independent_frame6_lost_hold", "independent_bad_target", "independent_missing",
                         "independent_boundary", "independent_pins", "independent_target_pins", "independent_input_depth",
                         "independent_wrapper_depth", "independent_wrong_tick",
                         "independent_p2_failure", "independent_p2_stop"):
                with self.subTest(case=case):
                    result = subprocess.run([str(executable), case], check=True, capture_output=True, text=True)
                    if case in ("independent_slot1", "independent_slot2", "independent_empty"):
                        wire = bytearray(43)
                        wire[0], wire[2], wire[42] = 3, 3, 2
                        wire.extend(bytes.fromhex('024001000000024000000000'))
                        self.assertEqual(result.stderr,
                            'X4LAN KEYBOARD_SLOT1 tick=5 capture=3 saved=0000000000000001 '
                            f'record={wire.hex()} held_before=0 held_after=0\n')
                    if case == "independent_empty_p1":
                        wire = bytearray(43)
                        wire[0], wire[2] = 3, 3
                        self.assertEqual(result.stderr,
                            'X4LAN KEYBOARD_SLOT1 tick=5 capture=3 saved=0000000000000001 '
                            f'record={wire.hex()} held_before=0 held_after=0\n')


if __name__ == "__main__":
    unittest.main()

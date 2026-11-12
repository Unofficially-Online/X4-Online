"""Explicit network-only ten-second window; real parsers and synthetic records."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
import linux_gate_live as live
import build_native_gate_overlay as overlay
from linux_player_frames_evidence import collect_recurring_physics_evidence, parse_recurring_physics_evidence
from linux_player_network_evidence import collect_network_physics_evidence, parse_network_physics_evidence
from test_linux_player_frames_evidence import valid_frames, worker_rows
from test_linux_player_network_evidence import record
from test_stock_keyboard_replay_overlay import snapshot_bytes


def neutral_network(frame_count):
    """One complete fixture with strictly advancing Update timestamps and no held keys."""
    rows, tick, input_index = [], 0, 0
    records = [record(tick - 2) for tick in range(5, 5 + frame_count)]
    for line in valid_frames(frame_count):
        if 'physics=frame tick=' in line and 'event=begin' in line:
            tick = int(line.split('tick=')[1].split()[0]); input_index = 0
        if 'input=press ' in line or 'input=release ' in line or 'physics=input ' in line:
            phases = ('before', 'applied') if input_index == 0 else ('updated',)
            input_index += 1
        else:
            rows.append(line.replace('input=state2 worker=', 'input=record worker=').
                        replace('input=state3 worker=', 'input=record worker=').
                        replace('input=none worker=', 'input=record worker='))
            continue
        for phase in phases:
            rows.append(f'X4LAN LIFECYCLE_DISCARD physics=network tick={tick} slot=2 stage={phase} '
                        f'held_before=0 held_after=0 digital=0 record={records[tick-5]}\n')
    stderr = worker_rows(frame_count) + [
        f'X4LAN KEYBOARD_SLOT1 tick={tick} capture={tick-2} saved=0000000000000011 '
        f'record={records[tick-5]} held_before=0 held_after=0\n' for tick in range(5, 5 + frame_count)]
    engine = [f'LINUX_KEYBOARD_CAPTURE tick={tick-2} record={records[tick-5]}\n'
              for tick in range(5, 5 + frame_count)]
    return rows, stderr, engine


class LongNetworkWindowTests(unittest.TestCase):
    def options(self):
        return dict(independent_keyboard_frames=600, stock_keyboard_replay_probe=True,
                    player_factory_probe=True, stock_keyboard_observer=True, serial_pathing=True,
                    player_lifetime_preload=True, independent_keyboard_mode='independent_keyboard',
                    stock_keyboard_replay_last_capture=602)

    def test_network_only_admission_drain_and_mode(self):
        self.assertEqual(live.validate_independent_keyboard(self.options(), 605), 600)
        self.assertTrue(live.validate_network_physics(self.options(), True, 600, 605))
        self.assertEqual(live.physics_window_end(True, 600, 604, 605, network=True), 604)
        self.assertEqual(live.physics_environment_mode(600, True), 'network-frames-discard')
        for count in (181, 600, 601):
            self.assertRaises(SystemExit, live.physics_window_end, True, count, None, 1000)
        self.assertRaises(SystemExit, live.physics_window_end, True, 601, None, 1000, network=True)
        self.assertRaises(SystemExit, live.physics_window_end, True, 600, 603, 605, network=True)
        self.assertRaises(SystemExit, live.physics_window_end, True, 600, None, 604, network=True)
        self.assertRaises(SystemExit, live.validate_independent_keyboard,
                          dict(self.options(), independent_keyboard_frames=601), 1000)
        self.assertRaises(SystemExit, live.validate_network_physics, self.options(), False, 600, 1000)
        self.assertRaises(SystemExit, live.validate_network_physics, self.options(), True, 599, 1000)
        self.assertFalse(parse_recurring_physics_evidence([], [], 223, frame_count=181)['pass'])
        self.assertFalse(parse_network_physics_evidence([], [], [], 223, frame_count=601, slot=2)['pass'])

    def test_one_600_frame_fixture_and_final_held_refusal(self):
        rows, stderr, engine = neutral_network(600)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = {name:root/name for name in ('native','retained','engine','stdout','stderr')}
            paths['native'].write_text(''.join(rows)); paths['stderr'].write_text(''.join(stderr))
            paths['engine'].write_text(''.join(engine)); paths['stdout'].touch()
            self.assertGreater(paths['native'].stat().st_size, 8 * 1024 * 1024)
            result = collect_network_physics_evidence(paths['native'],paths['retained'],paths['engine'],
                        paths['stdout'],223,frame_count=600,stderr=paths['stderr'],slot=2)
            self.assertTrue(result['pass'], result['errors'][:5])
            self.assertEqual([frame['tick'] for frame in result['frames']], list(range(5,605)))
            self.assertEqual(len(result['worker']['events']), 1798)
            self.assertEqual(live.sha(paths['native']), result['log_sha256'])
            self.assertFalse(result['restored'] or result['resumed'] or result['movement_proven'])
        held = record(602, [(2,1)])
        bad_stderr = [line.replace(record(602),held).replace('held_after=0','held_after=1')
                      if 'KEYBOARD_SLOT1 tick=604 ' in line else line for line in stderr]
        result = parse_network_physics_evidence(rows,bad_stderr,engine,223,frame_count=600,slot=2)
        self.assertFalse(result['pass']); self.assertIn('P1 final keys held',result['errors'])
        bad_rows = []
        for line in rows:
            if 'physics=network tick=604 ' in line:
                line = line.replace(record(602), held)
                if 'stage=before ' not in line:
                    line = line.replace('held_after=0', 'held_after=1')
            bad_rows.append(line)
        result = parse_network_physics_evidence(bad_rows,stderr,engine,223,frame_count=600,slot=2)
        self.assertFalse(result['pass']); self.assertTrue(any('P2 final keys held' in e for e in result['errors']))

    def test_network_32_mib_and_scripted_8_mib_retention_bounds(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for mode,limit in (('network',32),('scripted',8)):
                trace=root/f'{mode}.native'; retained=root/f'{mode}.retained'
                with trace.open('wb') as stream:stream.truncate(limit * 1024 * 1024 + 1)
                empty=root/'empty';empty.touch()
                result=collect_recurring_physics_evidence(trace,retained,empty,empty,223,
                            frame_count=2,stderr=empty,input_mode=mode)
                self.assertFalse(result['pass']); self.assertEqual(trace.stat().st_size,retained.stat().st_size)
                self.assertTrue(any(f'exceeds {limit} MiB' in error for error in result['errors']))

    def test_bundle_freezes_600_overlay_sources_and_exact_drain(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);snapshot=root/'save.gz';snapshot.write_bytes(snapshot_bytes());output=root/'bundle'
            original_copy=live.shutil.copy2
            def copy(source,destination,*args,**kwargs):
                if Path(source).name==live.PLAYER_LIFETIME:
                    Path(destination).write_bytes(b'fixture preload');return destination
                return original_copy(source,destination,*args,**kwargs)
            def command(arguments,**kwargs):
                if 'build_native_gate_linux.py' in str(arguments[1]):
                    (output/'.build/x4lan_gate.so').write_bytes(b'fixture gate')
                elif 'build_native_gate_overlay.py' in str(arguments[1]):
                    with mock.patch.object(sys,'argv',arguments[1:]):overlay.main()
                return mock.Mock(stdout='fixture-commit\n' if 'rev-parse' in arguments else '',returncode=0)
            with mock.patch.object(live.subprocess,'run',side_effect=command), \
                    mock.patch.object(live.shutil,'copy2',side_effect=copy),contextlib.redirect_stdout(io.StringIO()):
                live.main(['bundle','--zig','/unused','--snapshot',str(snapshot),'--out',str(output),
                           '--stock-keyboard-replay-probe','--player-factory-probe','--stock-keyboard-observer',
                           '--serial-pathing','--player-lifetime-preload','--player-factory-retain-until','60',
                           '--independent-keyboard-frames','600','--ticks','605'])
            manifest=json.loads((output/'manifest.json').read_text())
            self.assertEqual(manifest['independent_keyboard_frames'],600)
            self.assertEqual(manifest['stock_keyboard_replay_last_capture'],602)
            config=(output/'extension/x4_lan_lab/ui/config.lua').read_text()
            self.assertIn('last_capture=602',config);self.assertIn('independent_players=true',config)
            for name in ('src/native/engine/linux/gate_player_control_recurring.h',
                         'tools/dev/linux_gate_live.py','tools/dev/build_native_gate_overlay.py'):
                relative='source-receipts/'+name
                self.assertEqual((output/relative).read_bytes(),(ROOT/name).read_bytes())
                self.assertEqual(live.sha(output/relative),manifest['files'][relative])
            for name in ('linux_player_frames_evidence.py','linux_player_network_evidence.py'):
                self.assertEqual((output/name).read_bytes(),(ROOT/'tools/dev'/name).read_bytes())
                self.assertEqual(live.sha(output/name),manifest['files'][name])

if __name__=='__main__':unittest.main()

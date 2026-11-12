"""Lifecycle runner protocol/option checks; no X4 process is launched."""
import json
import hashlib
import io
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_gate_live as live

PREFIX = 'X4LAN LIFECYCLE_DISCARD '
STAGES = ['before_take', 'after_take', 'before_release', 'after_release']
PASS = PREFIX + 'result=pass reason=complete exit=223 restored=0 resumed=0'


class LifecycleDiscardTests(unittest.TestCase):
    def input_lines(self):
        lines = self.routes_lines()
        index = next(i for i, line in enumerate(lines) if 'snapshot=taken ' in line)
        lines.insert(index + 1, PREFIX + 'input=press id=2 state=1 keys=1000 digital=0')
        index = next(i for i, line in enumerate(lines) if 'snapshot=updated ' in line)
        lines.insert(index, PREFIX + 'input=release id=2 state=0 keys=0000 digital=-1')
        return [line.replace('update=pass calls=1 input=none', 'update=pass calls=1 input=state2').replace(
            'reason=routed_lifecycle_returned', 'reason=input_lifecycle_returned') for line in lines]

    def test_input_measured_press_release_and_causal_update(self):
        lines = self.input_lines()
        evidence = live.lifecycle_discard_evidence(lines, 223, injected_input=True)
        self.assertTrue(evidence['pass'], evidence['errors'])
        self.assertTrue(evidence['routes'] and evidence['update'] and evidence['isolated'])
        self.assertEqual([row['operation'] for row in evidence['input_samples']], ['press', 'release'])
        self.assertEqual(evidence['updates'][0]['input'], 'state2')
        self.assertFalse(live.lifecycle_discard_evidence(lines, 223, routes=True)['pass'])
        self.assertFalse(live.lifecycle_discard_evidence(self.routes_lines(), 223, injected_input=True)['pass'])
        for before, after in [('input=state2', 'input=none'), ('id=2 state=1', 'id=3 state=1'),
                              ('state=1 keys=1000', 'state=0 keys=1000'),
                              ('keys=1000', 'keys=0100'), ('keys=0000', 'keys=1000'),
                              ('digital=0', 'digital=-1'), ('digital=-1', 'digital=0'),
                              ('digital=-1', 'digital=1'), ('digital=-1', 'digital=nan'),
                              ('digital=0', 'digital=inf'), ('digital=0', 'digital=1e999'),
                              ('reason=input_lifecycle_returned', 'reason=routed_lifecycle_returned')]:
            bad = [line.replace(before, after) for line in lines]
            self.assertFalse(live.lifecycle_discard_evidence(bad, 223, injected_input=True)['pass'])
        press = next(line for line in lines if 'input=press ' in line)
        release = next(line for line in lines if 'input=release ' in line)
        missing = [line for line in lines if line != release]
        misplaced = missing[:]
        index = next(i for i, line in enumerate(misplaced) if ' update=pass ' in line)
        misplaced.insert(index, release)
        for bad in (missing, [line for line in lines if line != press], misplaced,
                    lines[:-1] + [press, lines[-1]], lines[:-1] + [release, lines[-1]]):
            self.assertFalse(live.lifecycle_discard_evidence(bad, 223, injected_input=True)['pass'])

    def test_input_run_option_and_mutual_exclusion(self):
        with mock.patch.object(live, 'run') as run:
            live.main(['run', '--root', '/unused', '--controller-lifecycle-input-discard'])
            self.assertTrue(run.call_args.args[0].controller_lifecycle_input_discard)
        for other in ('--controller-lifecycle-discard', '--controller-lifecycle-isolated-discard',
                      '--controller-lifecycle-update-discard', '--controller-lifecycle-routes-discard'):
            with mock.patch.object(live, 'run') as run, \
                    mock.patch.object(sys, 'stderr', new_callable=io.StringIO), self.assertRaises(SystemExit):
                live.main(['run', '--root', '/unused', '--controller-lifecycle-input-discard', other])
            run.assert_not_called()

    def test_routes_coverage_flags_truth_table_and_phase(self):
        original = self.routes_lines()
        coverage = next(line for line in original if 'routes=coverage ' in line)
        for authority in (0, 1):
            for ship in (0, 1):
                selected = 'agent' if authority or not ship else 'player'
                marker = (PREFIX + f'routes=coverage selected={selected} global_authority={authority} '
                          f'ship_flag={ship} canonical_authority=1 canonical_ship_flag=0 time=0.5')
                lines = [marker if line == coverage else line for line in original]
                result = live.lifecycle_discard_evidence(lines, 223, routes=True)
                self.assertTrue(result['pass'], result['errors'])
                self.assertEqual(result['route_coverage'][0]['selected'], selected)
                wrong = marker.replace('selected=' + selected,
                                       'selected=' + ('player' if selected == 'agent' else 'agent'))
                self.assertFalse(live.lifecycle_discard_evidence(
                    [wrong if line == coverage else line for line in original], 223, routes=True)['pass'])
        for before, after in [('global_authority=0', 'global_authority=2'),
                              ('ship_flag=1', 'ship_flag=-1'),
                              ('canonical_authority=0', 'canonical_authority=2'),
                              ('canonical_ship_flag=1', 'canonical_ship_flag=2'),
                              ('selected=player', 'selected=other'),
                              ('time=0.5', 'time=0'), ('time=0.5', 'time=-1'),
                              ('time=0.5', 'time=nan'), ('time=0.5', 'time=inf'),
                              ('time=0.5', 'time=1e999'), ('time=0.5', 'time=1 extra=1')]:
            bad = [line.replace(before, after) if line == coverage else line for line in original]
            self.assertFalse(live.lifecycle_discard_evidence(bad, 223, routes=True)['pass'])
        without = [line for line in original if line != coverage]
        wrong_phase = without[:]
        index = next(i for i, line in enumerate(wrong_phase) if 'snapshot=canonical_routes ' in line)
        wrong_phase.insert(index + 1, coverage)
        for bad in (without, [coverage] + without, wrong_phase,
                    original[:-1] + [coverage, original[-1]], original + [coverage]):
            self.assertFalse(live.lifecycle_discard_evidence(bad, 223, routes=True)['pass'])

    def routes_lines(self):
        lines = self.update_lines()
        index = next(index for index, line in enumerate(lines) if 'stage=before_release' in line)
        samples = []
        for getter, size in (('transform', 64), ('velocity', 32), ('acceleration', 32)):
            for mode in (0, 1):
                samples.append(PREFIX + f'routes=sample getter={getter} mode={mode} size={size} '
                               + 'expected=' + '00' * size + ' actual=' + '00' * size + ' match=1 canary=1')
        lines[index:index] = [PREFIX + 'stage=before_routes',
                             PREFIX + 'routes=coverage selected=player global_authority=0 ship_flag=1 '
                             'canonical_authority=0 canonical_ship_flag=1 time=0.5',
                             self.snapshot('canonical_routes').replace('zone_count=2', 'zone_count=3'),
                             *samples,
                             self.snapshot('canonical_routes_after').replace('zone_count=2', 'zone_count=3'),
                             PREFIX + 'routes=pass samples=6 bank_unchanged=1 singleton_canonical=1',
                             PREFIX + 'stage=after_routes', self.snapshot('routes_returned')]
        return [line.replace('reason=updated_lifecycle_returned', 'reason=routed_lifecycle_returned')
                for line in lines]

    def test_routes_exact_samples_order_sizes_and_canonical_observations(self):
        lines = self.routes_lines()
        evidence = live.lifecycle_discard_evidence(lines, 223, routes=True)
        self.assertTrue(evidence['pass'], evidence['errors'])
        self.assertTrue(evidence['update'] and evidence['isolated'])
        self.assertEqual(len(evidence['route_samples']), 6)
        self.assertFalse(live.lifecycle_discard_evidence(lines, 223, update=True)['pass'])
        self.assertFalse(live.lifecycle_discard_evidence(self.update_lines(), 223, routes=True)['pass'])
        for before, after in [('getter=transform mode=1', 'getter=transform mode=0'),
                              ('size=64', 'size=32'), ('canary=1', 'canary=0'),
                              ('match=1', 'match=0'), ('actual=00', 'actual=01'),
                              ('actual=00', 'actual='), ('expected=00', 'expected=GG'),
                              ('bank_unchanged=1', 'bank_unchanged=0'),
                              ('singleton_canonical=1', 'singleton_canonical=0')]:
            with self.subTest(before=before, after=after):
                self.assertFalse(live.lifecycle_discard_evidence(
                    [line.replace(before, after) for line in lines], 223, routes=True)['pass'])
        for label in ('canonical_routes', 'canonical_routes_after'):
            for before, after in [('bank=00', 'bank=01'), ('changed_bytes=0', 'changed_bytes=1'),
                                  ('zone_count=3', 'zone_count=2'), ('zone_count=3', 'zone_count=4'),
                                  ('zone=abc', 'zone=def'), ('contexts_nodes=3', 'contexts_nodes=4')]:
                bad = [line.replace(before, after) if f'snapshot={label} ' in line else line for line in lines]
                self.assertFalse(live.lifecycle_discard_evidence(bad, 223, routes=True)['pass'])
        missing = [line for line in lines if 'getter=velocity mode=1' not in line]
        self.assertFalse(live.lifecycle_discard_evidence(missing, 223, routes=True)['pass'])
        sample_indices = [index for index, line in enumerate(lines) if 'routes=sample' in line]
        reordered = lines[:]
        a, b = sample_indices[:2]
        reordered[a], reordered[b] = reordered[b], reordered[a]
        self.assertFalse(live.lifecycle_discard_evidence(reordered, 223, routes=True)['pass'])

    def test_routes_run_option_and_mutual_exclusion(self):
        with mock.patch.object(live, 'run') as run:
            live.main(['run', '--root', '/unused', '--controller-lifecycle-routes-discard'])
            self.assertTrue(run.call_args.args[0].controller_lifecycle_routes_discard)
        for other in ('--controller-lifecycle-discard', '--controller-lifecycle-isolated-discard',
                      '--controller-lifecycle-update-discard'):
            with mock.patch.object(live, 'run') as run, \
                    mock.patch.object(sys, 'stderr', new_callable=io.StringIO), self.assertRaises(SystemExit):
                live.main(['run', '--root', '/unused', '--controller-lifecycle-routes-discard', other])
            run.assert_not_called()

    def update_lines(self):
        lines = self.isolated_lines()
        index = next(index for index, line in enumerate(lines) if 'stage=before_release' in line)
        lines[index:index] = [PREFIX + 'stage=before_update', PREFIX + 'stage=after_update',
                             PREFIX + 'update=pass calls=1 input=none worker=1 zone_matches=1 '
                             'controller_identity=1 time=0.0166666667', self.snapshot('updated')]
        return [line.replace('reason=isolated_lifecycle_returned', 'reason=updated_lifecycle_returned')
                for line in lines]

    def test_update_requires_exact_causal_readback_and_positive_time(self):
        lines = self.update_lines()
        evidence = live.lifecycle_discard_evidence(lines, 223, update=True)
        self.assertTrue(evidence['pass'], evidence['errors'])
        self.assertTrue(evidence['isolated'])
        self.assertEqual(evidence['updates'][0]['input'], 'none')
        self.assertFalse(live.lifecycle_discard_evidence(lines, 223, isolated=True)['pass'])
        self.assertFalse(live.lifecycle_discard_evidence(self.isolated_lines(), 223, update=True)['pass'])
        marker = next(line for line in lines if ' update=pass ' in line)
        for before, after in [('calls=1', 'calls=2'), ('input=none', 'input=synthetic'),
                              ('worker=1', 'worker=0'), ('zone_matches=1', 'zone_matches=0'),
                              ('controller_identity=1', 'controller_identity=0'),
                              ('time=0.0166666667', 'time=0'),
                              ('time=0.0166666667', 'time=-1'),
                              ('time=0.0166666667', 'time=nan'),
                              ('time=0.0166666667', 'time=inf'),
                              ('time=0.0166666667', 'time=1e999'),
                              ('time=0.0166666667', 'time=1 extra=1'),
                              ('reason=updated_lifecycle_returned', 'reason=isolated_lifecycle_returned')]:
            with self.subTest(before=before, after=after):
                self.assertFalse(live.lifecycle_discard_evidence(
                    [line.replace(before, after) for line in lines], 223, update=True)['pass'])
        cases = [[line for line in lines if 'snapshot=updated ' not in line],
                 [line for line in lines if line != marker], lines[:-1] + [marker, lines[-1]],
                 [line for line in lines if line != marker][:-1] + [marker, lines[-1]],
                 [line.replace('stage=before_update', 'stage=after_update') for line in lines]]
        for bad in cases:
            self.assertFalse(live.lifecycle_discard_evidence(bad, 223, update=True)['pass'])

    def test_update_run_option_and_mutual_exclusion(self):
        with mock.patch.object(live, 'run') as run:
            live.main(['run', '--root', '/unused', '--controller-lifecycle-update-discard'])
            self.assertTrue(run.call_args.args[0].controller_lifecycle_update_discard)
        for other in ('--controller-lifecycle-discard', '--controller-lifecycle-isolated-discard'):
            with mock.patch.object(live, 'run') as run, \
                    mock.patch.object(sys, 'stderr', new_callable=io.StringIO), self.assertRaises(SystemExit):
                live.main(['run', '--root', '/unused', '--controller-lifecycle-update-discard', other])
            run.assert_not_called()

    def isolated_lines(self):
        marker = (PREFIX + 'isolation=pass bind_hits=1 destructor_hits=1 contexts_preserved=1 '
                  'zone_restored=1 bank_restored=1 singleton_restored=1 patch_restored=1')
        return self.lines()[:-1] + [marker, self.snapshot('canonical_restored'),
                                   PASS.replace('reason=complete', 'reason=isolated_lifecycle_returned')]

    def test_isolated_causal_events_and_global_snapshots(self):
        lines = self.isolated_lines()
        evidence = live.lifecycle_discard_evidence(lines, 223, isolated=True)
        self.assertTrue(evidence['pass'], evidence['errors'])
        self.assertFalse(evidence['restored'])
        self.assertFalse(evidence['resumed'])
        self.assertFalse(live.lifecycle_discard_evidence(lines, 223)['pass'])
        self.assertFalse(live.lifecycle_discard_evidence(self.lines(), 223, isolated=True)['pass'])
        for bad in (lines[:-3] + [lines[-2], lines[-3], lines[-1]],
                    lines[:-3] + lines[-2:], lines[:-3] + [lines[-3]] + lines[-3:],
                    lines[:2] + [lines[-3]] + lines[2:-3] + lines[-2:],
                    [line.replace('bind_hits=1', 'bind_hits=0') for line in lines],
                    [line.replace('reason=isolated_lifecycle_returned', 'reason=complete') for line in lines]):
            self.assertFalse(live.lifecycle_discard_evidence(bad, 223, isolated=True)['pass'])
        for field, replacement in (('changed_bytes=0', 'changed_bytes=1'),
                                   ('bank=00', 'bank=01'), ('zone=abc', 'zone=def'),
                                   ('zone_count=2', 'zone_count=3'),
                                   ('animation_a=0', 'animation_a=1'),
                                   ('animation_b=0', 'animation_b=1'),
                                   ('contexts_nodes=3', 'contexts_nodes=4'),
                                   ('contexts_payloads=00', 'contexts_payloads=01')):
            bad = [line.replace(field, replacement) if 'snapshot=canonical_restored ' in line
                   else line for line in lines]
            self.assertFalse(live.lifecycle_discard_evidence(bad, 223, isolated=True)['pass'])
        bad = [line.replace('contexts_nodes=3', 'contexts_nodes=4') if 'snapshot=released ' in line
               else line for line in lines]
        self.assertFalse(live.lifecycle_discard_evidence(bad, 223, isolated=True)['pass'])

    def test_isolated_run_option_and_mutual_exclusion(self):
        with mock.patch.object(live, 'run') as run:
            live.main(['run', '--root', '/unused', '--controller-lifecycle-isolated-discard'])
            self.assertTrue(run.call_args.args[0].controller_lifecycle_isolated_discard)
            self.assertFalse(run.call_args.args[0].controller_lifecycle_discard)
        with mock.patch.object(live, 'run') as run, \
                mock.patch.object(sys, 'stderr', new_callable=io.StringIO), self.assertRaises(SystemExit):
            live.main(['run', '--root', '/unused', '--controller-lifecycle-isolated-discard',
                       '--controller-lifecycle-discard'])
        run.assert_not_called()

    def test_dedicated_fd_file_and_independent_fatal_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            trace, retained, engine, output = [Path(directory) / name for name in
                                               ('native', 'retained', 'engine', 'stdout')]
            engine.write_text('engine chatter\n')
            # Old/interleaved stdout records do not become native evidence.
            output.write_text('X4LAN LIFECYCLE_DISCARD stage=entered engine chatter\n')
            collect = lambda: live.collect_lifecycle_discard(trace, retained, engine, output, 223)
            self.assertFalse(collect()['pass'])
            data = ('\n'.join(self.lines()) + '\n').encode()
            trace.write_bytes(data)
            evidence = collect()
            self.assertTrue(evidence['pass'], evidence['errors'])
            self.assertEqual(evidence['native_fd_path'], str(trace))
            self.assertEqual(evidence['log_sha256'], hashlib.sha256(data).hexdigest())
            self.assertEqual(retained.read_bytes(), data)
            for bad in (data + b'X4LAN LIFECYCLE_', data + (PASS + '\n').encode(),
                        data.rstrip(b'\n'), data + b'other chatter\n'):
                trace.write_bytes(bad)
                self.assertFalse(collect()['pass'])
            trace.write_bytes(data)
            for source in (engine, output):
                source.write_text('NATIVE_GATE ERROR lifecycle_fatal\n')
                self.assertFalse(collect()['pass'])
                source.write_text('ordinary chatter\n')
            trace.unlink()
            trace.symlink_to(retained)
            self.assertFalse(collect()['pass'])

    def manifest(self):
        return dict(player_factory_probe=True, player_factory_retain_until=6,
                    stock_keyboard_observer=True, serial_pathing=True, ticks=10)

    def snapshot(self, label):
        return (PREFIX + f'snapshot={label} changed_bytes=0 bank=' + '00' * 996
                + ' zone=abc zone_count=2 animation_a=0 animation_b=0 controller=0 '
                + 'controller_vtable=0 contexts_ok=1 contexts_valid=1 contexts_nodes=3 '
                + 'contexts_mask=7 contexts_payloads=' + '00' * 48)

    def lines(self):
        return [PREFIX + 'stage=entered count=2', self.snapshot('initial'),
                PREFIX + 'stage=before_singleton', PREFIX + 'stage=after_singleton',
                self.snapshot('prepared'), PREFIX + 'stage=before_take',
                PREFIX + 'stage=after_take', self.snapshot('taken'),
                PREFIX + 'stage=before_release', PREFIX + 'stage=after_release',
                self.snapshot('released'), PASS]

    def test_snapshot_fields_and_order(self):
        valid = self.lines()
        snapshots = [line for line in valid if 'snapshot=' in line]
        for lines in ([line for line in valid if 'snapshot=' not in line] + snapshots,
                      valid[:-1] + [snapshots[0], PASS],
                      [line for line in valid if 'snapshot=taken ' not in line]):
            self.assertFalse(live.lifecycle_discard_evidence(lines, 223)['pass'])
        for before, after in [('bank=00', 'bank=GG'), ('bank=00', 'bank='),
                              ('contexts_valid=1', 'contexts_valid=0'),
                              ('contexts_ok=1', 'contexts_ok=0'),
                              ('contexts_payloads=00', 'contexts_payloads=xx')]:
            bad = [line.replace(before, after) for line in valid]
            self.assertFalse(live.lifecycle_discard_evidence(bad, 223)['pass'])
        removed_targets = [line.replace('contexts_ok=1', 'contexts_ok=0').replace(
            'contexts_mask=7', 'contexts_mask=3') for line in valid]
        self.assertTrue(live.lifecycle_discard_evidence(removed_targets, 223)['pass'])

    def test_complete_protocol_only_accepts_exit223(self):
        self.assertTrue(live.lifecycle_discard_evidence(self.lines(), 223)['pass'])
        for code in (None, 0, 1, -9):
            self.assertFalse(live.lifecycle_discard_evidence(self.lines(), code)['pass'])

    def test_missing_duplicate_reordered_and_late_stages(self):
        cases = [self.lines()[1:], self.lines()[:-2] + [PASS], self.lines() + [PASS],
                 self.lines()[:2] + self.lines()[1:], self.lines() + [PREFIX + 'stage=after_take'],
                 self.lines()[:2] + [PREFIX + 'stage=extra'] + self.lines()[2:],
                 [PREFIX + 'stage=' + stage for stage in reversed(STAGES)] + [PASS],
                 [PASS] + self.lines()[:-1]]
        # Dropping the optional early observation is allowed.
        self.assertTrue(live.lifecycle_discard_evidence(cases.pop(0), 223)['pass'])
        for lines in cases:
            with self.subTest(lines=lines):
                self.assertFalse(live.lifecycle_discard_evidence(lines, 223)['pass'])

    def test_fail_or_malformed_final_and_fatal_output(self):
        for final in (PASS.replace('result=pass', 'result=fail'),
                      PASS.replace('restored=0', 'restored=1'), PASS + ' extra=1',
                      'prefix ' + PASS):
            self.assertFalse(live.lifecycle_discard_evidence(self.lines()[:-1] + [final], 223)['pass'])
        self.assertFalse(live.lifecycle_discard_evidence(
            self.lines() + ['X4LAN NATIVE_GATE ERROR lifecycle_bad'], 223)['pass'])

    def test_waits_for_second_local_exit_and_rejects_unexpected_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / 'trace'
            trace.write_text('X4LAN LIFECYCLE_DISCARD result=pass reason=lifecycle_returned exit=223 restored=0 resumed=0\n')
            traces = {1: trace, 2: trace}
            self.assertFalse(live.lifecycle_discard_wait({1: 223, 2: None}, traces))
            self.assertFalse(live.lifecycle_discard_wait({1: None, 2: 223}, traces))
            self.assertTrue(live.lifecycle_discard_wait({1: 223, 2: 223}, traces))
            for codes in ({1: 0, 2: None}, {1: 223, 2: -9}):
                self.assertRaises(RuntimeError, live.lifecycle_discard_wait, codes, traces)

    def test_process_fatal_remains_independent_of_engine_log(self):
        with tempfile.TemporaryDirectory() as directory:
            engine, process = Path(directory) / 'engine', Path(directory) / 'process'
            engine.write_text('[X4LAN] NATIVE_GATE ARMED\n')
            process.write_text('X4LAN NATIVE_GATE ERROR PLAYER_CONTROL_BOUNDARY_FATAL exit=222\n')
            watches = [live.LogWatch(path) for path in (engine, process)]
            for watch in watches:
                watch.poll()
            self.assertIsNone(watches[0].failure)
            self.assertIn('PLAYER_CONTROL_BOUNDARY_FATAL', watches[1].failure)

    def test_effective_manifest_requirements(self):
        live.validate_lifecycle_discard(self.manifest(), 10)
        for key in ('player_factory_probe', 'stock_keyboard_observer', 'serial_pathing'):
            self.assertRaises(SystemExit, live.validate_lifecycle_discard,
                              self.manifest() | {key: False}, 10)
        for retain in (None, 0, 4, 5, 10, 11):
            self.assertRaises(SystemExit, live.validate_lifecycle_discard,
                              self.manifest() | {'player_factory_retain_until': retain}, 10)
        for key in ('player_control_boundary_probe', 'player_control_bank_probe',
                    'player_reference_probe', 'stock_keyboard_replay_probe', 'player_seat_preflight'):
            self.assertRaises(SystemExit, live.validate_lifecycle_discard,
                              self.manifest() | {key: True}, 10)

    def test_run_uses_effective_tick_override_before_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'bundle'
            root.mkdir()
            (root / live.MARKER).touch()
            (root / 'manifest.json').write_text(json.dumps(self.manifest()))
            args = SimpleNamespace(user='fixture', root=root, ticks=6,
                                   controller_lifecycle_discard=True)
            user = SimpleNamespace(pw_dir=directory, pw_uid=os.geteuid())
            with mock.patch('pwd.getpwnam', return_value=user), \
                    mock.patch.object(live.subprocess, 'Popen') as launch, \
                    self.assertRaisesRegex(SystemExit, 'retention'):
                live.run(args)
            launch.assert_not_called()

    def test_flag_is_run_only(self):
        with mock.patch.object(live, 'run') as run:
            live.main(['run', '--root', '/unused', '--controller-lifecycle-discard'])
            self.assertTrue(run.call_args.args[0].controller_lifecycle_discard)
        with mock.patch.object(live, 'bundle') as bundle, \
                mock.patch.object(sys, 'stderr', new_callable=io.StringIO) as errors, \
                self.assertRaises(SystemExit):
            live.main(['bundle', '--zig', '/unused', '--snapshot', '/unused', '--out', '/unused',
                       '--controller-lifecycle-discard'])
        bundle.assert_not_called()
        self.assertIn('unrecognized arguments: --controller-lifecycle-discard', errors.getvalue())


if __name__ == '__main__':
    unittest.main()

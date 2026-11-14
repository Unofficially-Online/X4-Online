import hashlib
import fcntl
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.dev import linux_semaphore_observer_bundle as bundle
from tools.dev import exec_linux_semaphore_observer as launcher


class SemaphoreEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.options = {'semaphore_observer': True, 'player_ui_session_frames': 180,
                        'player_startup_phases': 'v1-t40', 'serial_pathing': True, 'mass_traffic': 'sync',
                        'independent_keyboard_frames': 180, 'player_lifetime_preload': True}
        self.lifecycle = self.directory / 'lifecycle.log'
        self.lifecycle.write_text('X4LAN SEMAPHORE OBSERVER CHECKPOINT tick=219 owner=77 main_parked=1 complete=1\n')
        self.physics = {'pass': True, 'native_fd_path': str(self.lifecycle),
                        'returncode': 223, 'frame_count': 180, 'frames': [{'tick': 219}],
                        'log_sha256': hashlib.sha256(self.lifecycle.read_bytes()).hexdigest()}
        self.path = self.directory / 'semaphore-observer.log'
        self.rows = [
            'X4 SEMAPHORE OBSERVER ACTIVE version=1',
            'X4 SEMAPHORE OBSERVER v=1 phase=enter kind=0 token=1 tid=11 caller=829a8f arg=1 result=0 movement=0 ns=10',
            'X4 SEMAPHORE OBSERVER v=1 phase=return kind=0 token=1 tid=11 caller=829a8f arg=1 result=0 movement=0 ns=20',
            'X4 SEMAPHORE OBSERVER v=1 phase=enter kind=2 token=2 tid=77 caller=1234 arg=0 result=0 movement=1 ns=30',
            'X4 SEMAPHORE OBSERVER v=1 phase=return kind=2 token=2 tid=77 caller=1234 arg=0 result=0 movement=1 ns=40',
            'X4 SEMAPHORE OBSERVER STATUS v=1 tick=219 owner=77 closed=1 active=0 tokens=2 receipts=4 incomplete=0 constructor=2 complete=1',
        ]

    def collect(self):
        self.path.write_text('\n'.join(self.rows) + '\n')
        return bundle.collect(self.path, self.physics, self.options, True)

    def test_paired_ledger_and_limits(self):
        result = self.collect()
        self.assertTrue(result['pass'], result['errors'])
        self.assertEqual(result['movement_waits'], 1)
        self.assertFalse(result['frame_ownership_proved'])
        self.assertFalse(result['bootstrap_proved'])

    def test_binds_only_validated_os_tid_when_available(self):
        self.path.write_text('\n'.join(self.rows) + '\n')
        for proof, accepted in (({'pass': True, 'tid': 77}, True),
                                ({'pass': True, 'tid': 78}, False),
                                ({'pass': False, 'tid': 77}, False),
                                ({'pass': True, 'tid': True}, False)):
            result = bundle.collect(self.path, self.physics, self.options, True, ancestry=proof)
            self.assertEqual(result['pass'], accepted, result['errors'])
        self.options['view_ancestry_observer'] = True
        self.assertFalse(bundle.collect(self.path, self.physics, self.options, True)['pass'])

    def test_missing_status_pairs_constructor_or_quiescence_refuses(self):
        for index, value in ((-1, ''), (-1, self.rows[-1].replace('active=0', 'active=1')),
                             (-1, self.rows[-1].replace('incomplete=0', 'incomplete=1')),
                             (2, self.rows[2].replace('token=1', 'token=9')),
                             (1, self.rows[1].replace('caller=829a8f', 'caller=829a90')),
                             (3, self.rows[3].replace('movement=1', 'movement=0'))):
            with self.subTest(index=index, value=value):
                old = self.rows[index]; self.rows[index] = value
                self.assertFalse(self.collect()['pass'])
                self.rows[index] = old
        self.physics['pass'] = False
        self.assertFalse(self.collect()['pass'])

    def test_360_frame_cutoff_uses_its_actual_final_tick(self):
        self.options.update(player_ui_session_frames=360, independent_keyboard_frames=360)
        self.physics.update(frame_count=360, frames=[{'tick': 399}])
        self.lifecycle.write_text('X4LAN SEMAPHORE OBSERVER CHECKPOINT tick=399 owner=77 main_parked=1 complete=1\n')
        self.physics['log_sha256'] = hashlib.sha256(self.lifecycle.read_bytes()).hexdigest()
        self.rows[-1] = self.rows[-1].replace('tick=219', 'tick=399')
        self.assertTrue(self.collect()['pass'])
        self.rows[-1] = self.rows[-1].replace('tick=399', 'tick=219')
        self.assertFalse(self.collect()['pass'])

    def test_disabled_and_dependencies(self):
        self.assertTrue(bundle.collect(None, None, {}, False)['pass'])
        with self.assertRaises(ValueError):
            bundle.enabled({'semaphore_observer': True})

    def test_v18_pins_provider_and_preserves_v17_delegation(self):
        from test_linux_view_component_policy_bundle import fixture
        with patch.object(bundle.previous, 'validate_bundle', return_value='accepted') as old:
            old_manifest = {'view_query_source_version': 17}
            self.assertEqual(bundle.validate_bundle(self.directory, old_manifest, True), 'accepted')
            self.assertIs(old.call_args.args[1], old_manifest)
            manifest = fixture(self.directory, bundle, 18)
            manifest.update(self.options, semaphore_observer_evidence_version=1)
            provider = self.directory / 'semaphore_observer' / bundle.PROVIDER
            provider.parent.mkdir()
            provider.write_bytes(b'frozen provider')
            key = 'semaphore_observer/' + bundle.PROVIDER
            manifest['files'][key] = hashlib.sha256(provider.read_bytes()).hexdigest()
            manifest.update(semaphore_observer_sha256=manifest['files'][key],
                            semaphore_observer_size=provider.stat().st_size)
            self.assertEqual(bundle.validate_bundle(self.directory, manifest, True), 'accepted')
            self.assertEqual(old.call_args.args[1]['view_query_source_version'], 17)
            provider.write_bytes(b'tampered provider')
            with self.assertRaises(ValueError):
                bundle.validate_bundle(self.directory, manifest, True)

    def test_launcher_opens_exclusive_private_regular_receipt_before_exec(self):
        (self.directory / 'game').mkdir()
        image = self.directory / 'game/X4'; image.write_bytes(b'image')
        provider = self.directory / bundle.PROVIDER; provider.write_bytes(b'provider')
        expected = hashlib.sha256(provider.read_bytes()).hexdigest()
        argv = ['helper', '--case', str(self.directory), '--provider-sha256', expected,
                '--provider-size', str(provider.stat().st_size), '--', str(image), '-skipintro']
        captured = {}
        def fake_exec(executable, args, env):
            fd = int(env['X4MP_SEMAPHORE_OBSERVER_FD'])
            captured.update(executable=executable, args=args, env=env, fd=fd,
                            flags=fcntl.fcntl(fd, fcntl.F_GETFL), inheritable=os.get_inheritable(fd))
            raise RuntimeError('captured')
        original_digest = launcher.digest
        with patch.object(sys, 'argv', argv), patch.object(launcher, 'digest', side_effect=lambda p:
                launcher.X4_SHA if p == image else original_digest(p)), patch.object(launcher.os, 'execve', fake_exec), \
                patch.dict(os.environ, {'X4MP_SEMAPHORE_PRELOAD_CHAIN': '/existing.so'}, clear=True):
            with self.assertRaisesRegex(RuntimeError, 'captured'):
                launcher.main()
        try:
            self.assertEqual(captured['executable'], str(image))
            self.assertEqual(captured['env']['LD_PRELOAD'], str(provider) + ' /existing.so')
            self.assertTrue(captured['flags'] & os.O_APPEND)
            self.assertTrue(captured['inheritable'])
            self.assertEqual((self.directory / 'semaphore-observer.log').stat().st_mode & 0o777, 0o600)
        finally:
            os.close(captured['fd'])

    def test_dispatch_opt_in_environment_and_receipts(self):
        manifest = dict(self.options, semaphore_observer_sha256='a' * 64, semaphore_observer_size=123)
        program = [str(self.directory / 'game/X4')]
        env, _ = bundle.wrap_program(self.directory, manifest, {'X4_DEV_WORKER_DISPATCH': '1'}, program)
        self.assertNotIn('X4_DEV_WORKER_DISPATCH', env)
        manifest['worker_dispatch'] = True
        env, _ = bundle.wrap_program(self.directory, manifest, {}, program)
        self.assertEqual(env['X4_DEV_WORKER_DISPATCH'], '1')
        with self.assertRaises(ValueError):
            bundle.enabled(dict(manifest, semaphore_observer=False))
        with self.assertRaises(ValueError):
            bundle.enabled(dict(manifest, worker_dispatch='1'))
        self.options['worker_dispatch'] = True
        self.physics['frames'] = [{'tick': tick} for tick in range(40, 220)]
        self.assertFalse(self.collect()['pass'])
        self.lifecycle.write_text(self.lifecycle.read_text() +
            'X4LAN WORKER_DISPATCH bootstrap=1 parked=1 count=0 maximum=1\n' +
            ''.join(f'X4LAN WORKER_DISPATCH tick={tick} ticket={tick} consumed=1 post_returned=1\n'
                    for tick in range(37, 220)))
        self.physics['log_sha256'] = hashlib.sha256(self.lifecycle.read_bytes()).hexdigest()
        result = self.collect()
        self.assertTrue(result['pass'], result['errors'])
        self.assertEqual(result['worker_dispatch']['dispatches'], 183)
        self.lifecycle.write_text(self.lifecycle.read_text().replace('ticket=219', 'ticket=218'))
        self.physics['log_sha256'] = hashlib.sha256(self.lifecycle.read_bytes()).hexdigest()
        self.assertFalse(self.collect()['pass'])

    def test_wrapper_never_preloads_the_python_helper(self):
        manifest = dict(self.options, semaphore_observer_sha256='a' * 64, semaphore_observer_size=123)
        env, program = bundle.wrap_program(self.directory, manifest, {'LD_PRELOAD': '/view.so'},
                                           [str(self.directory / 'game/X4'), '-skipintro'])
        self.assertNotIn('LD_PRELOAD', env)
        self.assertEqual(env['X4MP_SEMAPHORE_PRELOAD_CHAIN'], '/view.so')
        self.assertEqual(program[0], '/usr/bin/python3')
        self.assertEqual(program[-2:], [str(self.directory / 'game/X4'), '-skipintro'])
        with self.assertRaises(ValueError):
            bundle.wrap_program(self.directory, manifest, {}, ['/other/X4'])


class SemaphorePostprocessingTests(unittest.TestCase):
    def test_exact_local_slots_accept_full_and_external_guest_proofs(self):
        import linux_gate_live as live
        for local in ([1, 2], [2]):
            with self.subTest(local=local), patch.object(live.semaphore_bundle, 'collect', return_value={'pass': True}):
                result = {'pass': False, 'physics_discard': {'pass': True, 'clients': {}}}
                live.collect_semaphore_observations(result, local, {slot: Path('/case') for slot in local}, {})
                self.assertTrue(result['physics_discard']['pass'])
                self.assertEqual(set(result['semaphore_observer_evidence']), {str(slot) for slot in local})

    def test_failed_missing_or_exception_evidence_invalidates_physics(self):
        import linux_gate_live as live
        for local, cases, evidence in (
                ([1, 2], {1: Path('/a'), 2: Path('/b')}, {'pass': False}),
                ([1, 2], {1: Path('/a'), 2: Path('/b')}, {}),
                ([1, 2], {1: Path('/a')}, {'pass': True}),
                ([2], {1: Path('/a'), 2: Path('/b')}, {'pass': True}),
                ([], {}, {'pass': True}),
                ([2, 2], {2: Path('/b')}, {'pass': True}),
                ([2], {2: Path('/b')}, RuntimeError('receipt unavailable'))):
            with self.subTest(local=local, cases=cases, evidence=evidence):
                result = {'pass': False, 'physics_discard': {'pass': True, 'clients': {}}}
                kwargs = {'side_effect': evidence} if isinstance(evidence, Exception) else {'return_value': evidence}
                with patch.object(live.semaphore_bundle, 'collect', **kwargs):
                    live.collect_semaphore_observations(result, local, cases, {})
                self.assertFalse(result['physics_discard']['pass'])
                self.assertFalse(result['pass'])


class SemaphoreBundleBuildTests(unittest.TestCase):
    def test_mocked_linux_core_default_and_explicit_observer(self):
        import json
        import linux_gate_live as live
        from test_linux_ui_render_lease_evidence import RenderLeaseAdmissionTests
        original_main = live.main
        seen = []

        def build(arguments):
            active = '--player-ui-seat-correction' in arguments
            if active:
                converted, skip = [], False
                for token in arguments:
                    if skip:
                        skip = False
                    elif token in ('--independent-keyboard-frames', '--player-ui-overlay-callbacks', '--ticks'):
                        skip = True
                    elif token not in ('--player-ui-overlay-probe', '--serial-pathing'):
                        converted.append(token)
                arguments = converted + ['--player-ui-session-frames', '180', '--ticks', '220',
                                         '--player-startup-phases', 'v1-t40', '--semaphore-observer']
            arguments += ['--preset', 'linux-core']
            output = Path(arguments[arguments.index('--out') + 1])
            prior_run, prior_copy = live.subprocess.run, live.shutil.copy2
            builds = []

            def command(argv, **kwargs):
                if 'build_linux_semaphore_observer.py' in str(argv[1]):
                    builds.append(argv)
                    target = Path(argv[argv.index('--out') + 1])
                    target.mkdir()
                    (target / bundle.PROVIDER).write_bytes(b'fixture observer')
                    return unittest.mock.Mock(returncode=0)
                return prior_run(argv, **kwargs)

            def copy_file(source, destination, *args, **kwargs):
                if Path(source).name == live.PORTABLE_MATH:
                    Path(destination).write_bytes(b'fixture portable math')
                    return destination
                return prior_copy(source, destination, *args, **kwargs)

            with patch.object(live.subprocess, 'run', side_effect=command), \
                    patch.object(live.shutil, 'copy2', side_effect=copy_file):
                result = original_main(arguments)
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(manifest['view_query_source_version'], 18)
            for name in ('linux_gate_live.py', 'linux_stock_view_fov_observer.py',
                         'linux_player_frames_evidence.py', 'linux_player_network_evidence.py'):
                key = 'source-receipts/tools/dev/' + name
                self.assertEqual(manifest['files'][key], live.sha(output / key))
                self.assertEqual(manifest['files'][name], manifest['files'][key])
            for name in ('tests/test_linux_semaphore_checkpoint_evidence.py',
                         'tests/test_linux_t40_evidence.py', 'tests/test_linux_network_long_window.py'):
                key = 'source-receipts/' + name
                self.assertEqual(manifest['files'][key], live.sha(output / key))
            self.assertIs(manifest['semaphore_observer'], active)
            self.assertEqual(bool(builds), active)
            self.assertEqual(manifest['semaphore_observer_evidence_version'], 1 if active else None)
            self.assertTrue(manifest['serial_pathing'])
            self.assertEqual(manifest['mass_traffic'], 'sync')
            self.assertIs(live.semaphore_bundle.enabled(manifest), active)
            live.semaphore_bundle.validate_bundle(output, manifest, True)
            provider = output / 'semaphore_observer' / bundle.PROVIDER
            self.assertEqual(provider.exists(), active)
            if active:
                self.assertEqual(manifest['semaphore_observer_sha256'], live.sha(provider))
                self.assertEqual(manifest['semaphore_observer_size'], provider.stat().st_size)
                self.assertEqual(manifest['independent_keyboard_frames'], 180)
            seen.append(active)
            return result

        with patch.object(live, 'main', side_effect=build):
            RenderLeaseAdmissionTests().test_mocked_bundle_forwarding_config_helper_and_source_inventory()
        self.assertEqual(set(seen), {False, True})

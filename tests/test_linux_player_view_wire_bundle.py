"""Explicit VIEW route coherence, closure and exact two-peer capture evidence."""
import copy
import hashlib
import json
from pathlib import Path
import re
import struct
import tempfile
import unittest
from unittest.mock import patch

import linux_player_view_wire_bundle as wire
from test_linux_view_component_policy_bundle import fixture


class ViewWireBundleTests(unittest.TestCase):
    def options(self):
        return dict({name: True for name in wire.REQUIRED_OPTIONS}, player_view_wire=True,
                    stock_view_fov_observer=False, player_startup_phases='v1-t40',
                    independent_keyboard_frames=180, player_ui_session_frames=180)

    def test_option_coherence_and_final_environment(self):
        self.assertFalse(wire.enabled({}))
        options = self.options()
        self.assertTrue(wire.enabled(options))
        for name in wire.REQUIRED_OPTIONS:
            with self.subTest(missing=name), self.assertRaises(ValueError):
                wire.enabled(dict(options, **{name: False}))
        for change in ({'stock_view_fov_observer': True}, {'player_view_wire': 1},
                       {'player_ui_session_frames': 179}, {'player_ui_session_frames': 901},
                       {'independent_keyboard_frames': 181}, {'player_startup_phases': None}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                wire.enabled(dict(options, **change))
        ambient = {name: 'ambient' for name in wire.NEW_ENV}
        ambient['KEEP'] = 'yes'
        self.assertEqual(wire.environment(ambient, False, {}), {'KEEP': 'yes'})
        env = dict({name: '1' for name in wire.REQUIRED_ENV},
                   X4_DEV_PLAYER_PHASES='v1-t40', X4_DEV_UI_SESSION_FRAMES='180',
                   X4_DEV_STOCK_VIEW_FOV_OBSERVE='1')
        enabled = wire.environment(env, True, options)
        self.assertTrue(all(enabled[name] == '1' for name in wire.NEW_ENV))
        self.assertNotIn('X4_DEV_STOCK_VIEW_FOV_OBSERVE', enabled)
        for name in (*wire.REQUIRED_ENV, 'X4_DEV_PLAYER_PHASES', 'X4_DEV_UI_SESSION_FRAMES'):
            with self.subTest(env=name), self.assertRaises(ValueError):
                wire.environment(dict(env, **{name: 'wrong'}), True, options)
        import linux_view_host_capture_bundle as host
        self.assertTrue(host.enabled(options))
        old = dict(options, player_view_wire=False, stock_view_fov_observer=True)
        self.assertTrue(host.enabled(old))
        for observer, rewrite in ((False, False), (True, True)):
            with self.assertRaises(ValueError):
                host.enabled(dict(options, stock_view_fov_observer=observer, player_view_wire=rewrite))

    def test_inventory_hash_chain_and_legacy_defaults(self):
        import linux_view_host_capture_bundle as host
        with tempfile.TemporaryDirectory() as directory, patch.object(host, 'validate_bundle', return_value='legacy'):
            root = Path(directory)
            manifest = fixture(root, wire, 16)
            manifest.update(self.options(), player_view_wire_evidence_version=1,
                            player_selection_parity_evidence_version=1)
            self.assertEqual(wire.validate_bundle(root, manifest, True), 'legacy')
            for name in ('src/native/engine/linux/gate_player_stock_view_wire_runtime.h',
                         'src/native/engine/linux/player_view_xs_preload_impl.h',
                         'tools/dev/linux_player_network_evidence.py'):
                path = root / 'source-receipts' / name
                original = path.read_bytes()
                path.write_bytes(original + b'tamper')
                with self.subTest(source=name), self.assertRaises(ValueError):
                    wire.validate_bundle(root, manifest, True)
                path.write_bytes(original)
            helper = root / 'linux_player_view_wire_bundle.py'
            helper.write_bytes(helper.read_bytes() + b'tamper')
            with self.assertRaises(ValueError):
                wire.validate_bundle(root, manifest, True)
            for change in ({'view_query_source_version': 15}, {'player_view_wire_evidence_version': True},
                           {'player_view_wire_evidence_version': None}):
                with self.assertRaises(ValueError):
                    wire.validate_bundle(root, dict(manifest, **change), True)
            with self.assertRaises(ValueError):
                wire.validate_bundle(root, manifest, False)
        with patch.object(wire.previous, 'validate_bundle', return_value='old') as previous:
            self.assertEqual(wire.validate_bundle('/unused', {'view_query_source_version': 15}, False), 'old')
            self.assertEqual(previous.call_args.args[1]['view_query_source_version'], 15)

    def raw_fixture(self, root, cockpit_bits=1):
        from test_linux_t40_evidence import common_trace
        import linux_gate_live as live
        options = self.options()
        budget = wire.budget_from_options(options)
        records, capture_rows = {}, {}
        for slot in (1, 2):
            own, captures = [], {}
            for tick in range(budget['capture'], budget['last_capture'] + 1):
                present = budget['first'] + 6 <= tick <= budget['last']
                record = bytearray(43)
                record[:4] = bytes((3, 11 if present else 3, tick & 255, tick >> 8))
                bits = cockpit_bits if present else 0
                fov = struct.pack('<f', (0.75 if slot == 1 else 1.25) if present else 0)
                target = (0xfedcba9876543210 + slot) if present else 0
                record[29] = bits; record[30:34] = fov; record[34:42] = target.to_bytes(8, 'little')
                own.append(record.hex())
                role = 'HOST' if slot == 1 else 'RENDER'
                captures[tick] = (f'X4 VIEW_{role}_CAPTURE v=1 tick={tick} present={int(present)} '
                    f'bits={bits} fov={int.from_bytes(fov,"little"):08x} target={target:016x}\n')
            records[slot], capture_rows[slot] = own, captures
        clients = {}
        for slot in (1, 2):
            rows, stderr, engine = common_trace()
            def replace(line, selected):
                match = re.search(r'record=([0-9a-f]+)', line)
                if not match:
                    return line
                capture = int.from_bytes(bytes.fromhex(match[1])[2:4], 'little')
                return line[:match.start(1)] + records[selected][capture - budget['capture']] + line[match.end(1):]
            rows = [replace(line, 2) if 'physics=network ' in line else line for line in rows]
            stderr = [replace(line, 1) if 'KEYBOARD_SLOT1 ' in line else line for line in stderr]
            staged = []
            for line in stderr:
                release = re.fullmatch(r'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick=([0-9]+) tail=1 event=release\n', line)
                if release and int(release[1]) in capture_rows[slot]:
                    staged.append(capture_rows[slot][int(release[1])])
                staged.append(line)
            engine = [replace(line, slot) for line in engine]
            profile = root / f'case{slot}/home/.config/EgoSoft/X4/45841'
            profile.mkdir(parents=True)
            trace = profile / 'controller-lifecycle.log'; trace.write_text(''.join(rows))
            engine_path = profile / 'engine-output.log'; engine_path.write_text(''.join(engine))
            stderr_path = root / f'game{slot}-stderr.log'; stderr_path.write_text(''.join(staged))
            stdout = root / f'game{slot}-stdout.log'; stdout.touch()
            proof = live.collect_network_client((trace, root / f'retained{slot}', engine_path, stdout, 223),
                stderr_path, 180, slot, cursor=True, session_frames=180, startup='v1-t40', allow_view=True)
            self.assertTrue(proof['pass'], proof['errors'][:4])
            clients[str(slot)] = proof
        return options, clients

    def test_actual_physics_bound_capture_wire_and_tamper_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            options, clients = self.raw_fixture(root)
            proof = wire.collect(root, clients, options, True)
            self.assertTrue(proof['pass'], proof['errors'])
            self.assertTrue(proof['wire_records_proved'])
            self.assertFalse(proof['xs_calls_proved'])
            import linux_view_host_capture_bundle as host
            import linux_view_render_capture_bundle as guest
            for slot, module in ((1, host), (2, guest)):
                captured = module.collect(root / f'game{slot}-stderr.log', clients[str(slot)], options, slot, True)
                self.assertTrue(captured['pass'], captured['errors'])
                self.assertTrue(captured['wire_enabled'])
            self.assertTrue(wire.collect(root, {}, {}, False)['pass'])
            for mutation in ('bits', 'fov', 'target', 'present', 'epoch'):
                stderr = root / 'game2-stderr.log'
                original = stderr.read_text()
                line = next(line for line in original.splitlines() if 'VIEW_RENDER_CAPTURE' in line and 'tick=50 ' in line)
                changed = {'bits': line.replace('bits=1', 'bits=3'),
                           'fov': line.replace('fov=3fa00000', 'fov=3f800000'),
                           'target': line.replace('fedcba9876543212', 'fedcba9876543213'),
                           'present': line.replace('present=1', 'present=0'),
                           'epoch': line.replace('tick=50 ', 'tick=51 ')}[mutation]
                stderr.write_text(original.replace(line, changed, 1))
                # Even a fresh raw hash cannot excuse bytes differing from accepted wire.
                fresh = copy.deepcopy(clients)
                fresh['2']['stderr_sha256'] = hashlib.sha256(stderr.read_bytes()).hexdigest()
                with self.subTest(mutation=mutation):
                    self.assertFalse(wire.collect(root, fresh, options, True)['pass'])
                stderr.write_text(original)
            wrong = copy.deepcopy(clients)
            wrong['2']['slot_records']['1'][0] = wrong['2']['slot_records']['1'][1]
            self.assertFalse(wire.collect(root, wrong, options, True)['pass'])
            stderr = root / 'game2-stderr.log'
            stderr.write_text(stderr.read_text() + 'raw appended after accepted proof\n')
            self.assertFalse(wire.collect(root, clients, options, True)['pass'])

    def test_coherent_non_cockpit_raw_capture_and_both_peer_streams_are_refused(self):
        # These complete fixtures pass the representation-only physics parser.
        # Both peers, both slots, native rows, original engine exports and VIEW
        # capture rows agree. Only the consumer's cockpit-only limit rejects.
        with tempfile.TemporaryDirectory() as directory:
            for bits in (0, 2, 3):
                root = Path(directory) / f'bits{bits}'
                options, clients = self.raw_fixture(root, cockpit_bits=bits)
                with self.subTest(bits=bits):
                    proof = wire.collect(root, clients, options, True)
                    self.assertFalse(proof['pass'])
                    self.assertFalse(proof['wire_records_proved'])
                    self.assertTrue(proof['cockpit_only'])
                    self.assertFalse(proof['xs_calls_proved'])

    def test_mocked_build_default_and_explicit_wire(self):
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
                    elif token not in ('--player-ui-overlay-probe', '--stock-view-fov-observer'):
                        converted.append(token)
                arguments = converted + ['--player-ui-session-frames', '180', '--ticks', '220',
                    '--player-startup-phases', 'v1-t40', '--player-cursor-active-flight',
                    '--view-host-capture-shadow', '--view-render-capture-shadow',
                    '--player-ui-camera-refill', '--player-ui-projection-fov-scope', '--player-selection-parity',
                    '--view-ancestry-observer', '--view-component-trace', '--view-precompare-trace', '--player-view-wire']
            result = original_main(arguments)
            output = Path(arguments[arguments.index('--out') + 1])
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(manifest['view_query_source_version'], 18)
            self.assertIs(manifest['player_view_wire'], active)
            self.assertEqual(manifest['player_view_wire_evidence_version'], 1 if active else None)
            import linux_semaphore_observer_bundle as performance
            performance.validate_bundle(output, manifest, True)
            self.assertEqual(manifest['files']['linux_view_xs_bundle.py'], live.sha(output / 'linux_view_xs_bundle.py'))
            seen.append(active)
            return result
        with patch.object(live, 'main', side_effect=build):
            RenderLeaseAdmissionTests().test_mocked_bundle_forwarding_config_helper_and_source_inventory()
        self.assertEqual(set(seen), {False, True})

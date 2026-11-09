"""Compare isolated native X4 clients on one Linux host.

Create a fresh fixture with tools/dev/linux_snapshot_capture.py, then build a
bundle containing the gate, minimal lockstep extension, snapshot and relay:

    python3 tools/dev/linux_gate_live.py bundle --zig PATH/zig \
        --snapshot PATH/save_009.xml.gz --out .local/linux-test

The gate's determinism features are opt-in; use the configuration documented in
an experiment report when reproducing it. This harness exercises the Linux
simulation core, not complete Windows/Linux multiplayer or player controls.

On a desktop, run as the Steam user using its existing X11 display:

    python3 tools/dev/linux_gate_live.py run --root .local/linux-test \
        --user STEAM_USER --game PATH/TO/X4 --display :0 --preflight

Remove --preflight to start the run. XAUTHORITY must identify the local display.
The default Vulkan driver is Mesa lavapipe. Supply --vulkan-icd PATH/driver.json
to use a different installed driver. Without --display, each client gets a
private Xvfb display; that mode requires Xvfb and xauth. A root-owned server
harness can still select a different Steam user with --user.

Requirements: pinned Linux X4 9.00 (Steam build 23660954), running Steam,
bubblewrap, Python 3.11+, and enough available RAM (default 6.5 GiB per client).
Another X4 process prevents startup. The bundle must be marked by this builder
and reside beneath the selected user's home directory.

Each client gets private HOME/XDG directories, a windowed profile with autosave
disabled, an owned snapshot, and an extension view containing official DLC and
this test extension. Host files and game files are mounted read-only; device
access, shared memory, Steam IPC and the selected display remain shared with the
desktop. The harness never starts or stops Steam. Its file writes are confined
to owned run directories and a temporary lock mount point.

Exit 0 requires clean stops at the requested tick, restored hooks, complete
scheduled world captures, no capture/identity errors, matching captured hashes,
and the local relay's passing verdict. Unknown object/controller fields remain
explicit coverage limits in run-result.json. For an external relay this host
can validate only its local clients; combine both hosts' evidence separately.
Results and raw logs remain in ROOT/runs/NAME, including failed attempts.
"""
import argparse
import ctypes
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import socket
import subprocess
import sys
import time

from linux_gate_evidence import is_fatal_gate_message, validate_evidence

EXE_SHA = '113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20'
MARKER = '.x4mp-linux-gate-live'
SLOT_SAVE = 'save_009.xml.gz'
PORTABLE_MATH = 'libx4mp_portable_math.so'
PORTABLE_MATH_ACTIVE = re.compile(r'X4MP_PORTABLE_MATH ACTIVE version=1 kat=([0-9a-f]{16}) symbols=28 decimal=1 exact_reciprocal=1185/1185 ')
# A client past its STOP line runs free, so nothing waits long after one (seconds).
FAILED_STOP_SETTLE = 5   # after the last STOP, for its trailing lines (WORLD_IDENTITY_STOP, ...)
FAILED_STOP_PEER = 120   # for the peer's own STOP after a failed one
PEER_STOP = 300          # for the peer's STOP after a clean one
RELAY_VERDICT = 120      # for the relay's verdict after both stopped

# An opt-in simulation test configuration, not Windows player/UI feature parity.
# Keep these options explicit: the generic session defaults include hooks that
# the Linux engine adapter does not yet implement.
LINUX_CORE_OPTIONS = {
    'portable_math': True,
    'seed_rng': True,
    'serial_pathing': True,
    'path_determinism': 3,
    'region_rng': 'key',
    'ai_wait_rng': True,
    'md_resume_rng': True,
    'md_timed_rng': True,
    'boot_hold': True,
    'event_order': True,
    'gravidar': True,
    'gravidar_deadlines': True,
    'gravidar_wide': 'sort',
    'travel_snapshot': True,
    'runtime_ids': True,
    'runtime_ids_local': True,
    'script_rng': 'key',
    'script_rng_mask': 7,
    'script_rng_unmapped': True,
    'find_order': 'sort',
    'loadout_order': 'canonical',
    'attention_all': True,
    'spawn_rng': 'key',
    'weapon_fire_rng': True,
    'engine_refresh_rng': True,
    'spline_join_all': True,
    'actor_rng_all': True,
    'player_timer': True,
    'tempzone_order': True,
    'warp_rng': 'split',
    'warp_exit': True,
    'world_hash_shields': True,
    'mining_camera': True,
    'mass_traffic': 'sync',
    'region_pass': 'sync',
    'pass_stride': 2,
    'created_identities': True,
    'created_load': True,
    'portable_created': True,
    'nav_cadence': 'far',
    'nav_params_init': True,
    'nav_probe_init': True,
}


def apply_bundle_preset(args):
    """Fill unspecified options; explicit diagnostic modes keep their value."""
    if args.preset == 'linux-core':
        for name, value in LINUX_CORE_OPTIONS.items():
            # Ordered batches replace the snapshot strategy; never arm both.
            if name == 'travel_snapshot' and args.travel_determinism:
                continue
            current = getattr(args, name)
            if current is None or current is False:
                setattr(args, name, value)
    # Both modes require serialization even when the CLI flag was omitted.
    if args.gravidar or (args.path_determinism or 0) & 1:
        args.serial_pathing = True
    if getattr(args, 'mixed_platform', False):
        apply_mixed_platform(args)


def mixed_platform_options():
    """tools/build_play_overlay.py's MIXED_PLATFORM_SETS (the Windows host's --mixed-platform) as bundle options:
    imported, not copied, so a Windows host overlay and a Linux bundle carry the same keys and values."""
    import importlib.util
    path = Path(__file__).resolve().parents[1] / 'build_play_overlay.py'
    spec = importlib.util.spec_from_file_location('build_play_overlay_mixed', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    options = {}
    for text in module.MIXED_PLATFORM_SETS:
        key, value = text.split('=', 1)
        if not key.startswith('native_gate_'):
            raise SystemExit(f'--mixed-platform: unexpected key {key}')
        value = value.strip()
        options[key[len('native_gate_'):]] = value.strip("'") if value.startswith("'") else int(value)
    return options


def apply_mixed_platform(args):
    for name, value in mixed_platform_options().items():
        if not hasattr(args, name):
            raise SystemExit(f'--mixed-platform: no bundle option for native_gate_{name}')
        current = getattr(args, name)
        default_ticks = name == 'fp_redirect_ticks' and current == 3
        if current not in (None, False, value) and not default_ticks:
            raise SystemExit(f'--mixed-platform sets native_gate_{name}={value!r}, the command line {current!r}')
        setattr(args, name, value)


def sha(path):
    with open(path, 'rb') as stream:
        h = hashlib.sha256()
        for block in iter(lambda: stream.read(1 << 20), b''):
            h.update(block)
        return h.hexdigest()


def snapshot_seed(path):
    """Use the save's world seed for JobDB initialization on every reload."""
    with gzip.open(path, 'rb') as stream:
        header = stream.read(8192)
    # The fixed-size prefix may end inside an unrelated UTF-8 character.
    match = re.search(rb'<game\s[^>]*\bseed="(\d+)"', header)
    if match is None or int(match.group(1)) > 0xffffffff:
        raise ValueError('snapshot must contain a 32-bit unsigned world seed in its header')
    return int(match.group(1))


# ------------------------------------------------------------------ bundle


def insert_ui_files(text, wanted):
    """Add <file name=.../> entries right after ui/config.lua in ui.xml (lane tool rule)."""
    match = re.search(r'<file\s+name="ui/config\.lua"\s*/>', text)
    if not match:
        raise SystemExit('ui.xml has no ui/config.lua entry')
    line_start = text.rfind('\n', 0, match.start()) + 1
    indent = text[line_start:match.start()]
    indent = indent if not indent.strip() else '    '
    return text[:match.end()] + ''.join(f'\n{indent}<file name="{name}" />' for name in wanted) + text[match.end():]


def apply_overlay(target, overlay):
    """Plain files replace, <path>.append appends, ui.xml.files lists UI files (lane tool rules)."""
    ui_files = []
    for item in sorted(Path(overlay).rglob('*')):
        if not item.is_file():
            continue
        rel = item.relative_to(overlay).as_posix()
        if '/' not in rel and rel.startswith('x4lane-'):
            continue  # overlay metadata (x4lane-manifest.json), not extension content
        if rel == 'ui.xml.files':
            ui_files += [l.strip() for l in item.read_text().splitlines() if l.strip() and not l.startswith('#')]
        elif rel.endswith('.append'):
            dest = target / rel[:-len('.append')]
            old = dest.read_bytes()
            add = item.read_bytes()
            dest.write_bytes(old + (b'' if not old or old.endswith(b'\n') else b'\n') + add + (b'' if add.endswith(b'\n') else b'\n'))
        else:
            dest = target / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, dest)
    if ui_files:
        ui = target / 'ui.xml'
        ui.write_text(insert_ui_files(ui.read_text(), ui_files))


def compose_extension_template(overlay, target, extension_base=None):
    """Build a verified extension while retaining the per-client placeholders.

    The retired replica extension is no longer present in this repository. Use
    the same minimal base and UI registration as the current Windows launcher.
    An explicit base remains available for reproducing historical experiments.
    """
    repo = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(repo / 'src'))
    from lockstep.launcher import extension

    extension.verify_overlay(overlay)
    shutil.copytree(extension_base or extension.BASE, target)
    if extension_base is None:
        (target / 'content.xml').write_text(extension.CONTENT, encoding='utf-8')
        (target / 'ui.xml').write_text(extension.ui_xml(extension.BASE_UI), encoding='utf-8')
        (target / 'ui/config.lua').write_text('X4LAN_CONFIG = {enabled = false}\n', encoding='utf-8')
    apply_overlay(target, overlay)
    if extension_base is None:
        overlay_files = [line.strip() for line in (overlay / 'ui.xml.files').read_text().splitlines()
                         if line.strip() and not line.startswith('#')]
        (target / 'ui.xml').write_text(extension.ui_xml([*extension.BASE_UI, *overlay_files]), encoding='utf-8')


def bundle(args):
    repo = Path(__file__).resolve().parents[2]
    if args.ticks < 1 or args.stride < 1:
        raise SystemExit('--ticks and --stride must be positive')
    if args.status_stride is not None and not 1 <= args.status_stride <= 3600:
        raise SystemExit('--status-stride must be between 1 and 3600')
    if args.created_rows and not args.created_identities:
        raise SystemExit('--created-rows requires --created-identities')
    if args.economy_watch is not None or args.economy_watch_window is not None:
        from economy_watch_config import validate_configuration
        try:
            validate_configuration(args.economy_watch, args.economy_watch_window, args.ticks)
        except ValueError as error:
            raise SystemExit(str(error)) from error
    if args.navmesh_post_observe is not None:
        from navmesh_post_config import validate_window
        try:
            validate_window(args.navmesh_post_observe, args.ticks)
        except ValueError as error:
            raise SystemExit(str(error)) from error
    if args.world_motion_watch is not None or args.world_motion_window is not None:
        from world_motion_evidence import validate_configuration
        try:
            validate_configuration(args.world_motion_watch, args.world_motion_window, args.ticks)
        except ValueError as error:
            raise SystemExit(str(error)) from error
    out = args.out
    if out.exists():
        raise SystemExit(f'{out} exists; use a new directory')
    snapshot_sha = sha(args.snapshot)
    world_seed = snapshot_seed(args.snapshot)
    out.mkdir(parents=True)
    work = out / '.build'
    work.mkdir()
    subprocess.run([sys.executable, str(repo / 'tools/dev/build_native_gate_linux.py'), '--zig', str(args.zig), '--out', str(work)], check=True)
    overlay = work / 'overlay'
    command = [sys.executable, str(repo / 'tools/dev/build_native_gate_overlay.py'), '--gate-so', str(work / 'x4lan_gate.so'),
               '--snapshot', str(args.snapshot), '--mode', 'battle' if args.battle else 'clock', '--world-identities',
               '--world-hash-stride', str(args.stride), '--world-hash-schema', 'WORLD_IMPORTED_2', '--out', str(overlay)]
    command += ['--world-log-mode', args.world_log_mode]
    if args.battle:
        command.append('--snapshot-prebind')
    for name in ('actor_diag', 'strict_state', 'shield_refresh', 'order_state', 'fleet_state', 'flight_rng', 'flight_full',
                 'avoidance_refresh', 'steering_init', 'agent_full', 'spline_diag', 'spline_refresh', 'spline_state', 'params_init', 'strict_physics', 'body_diag', 'params_diag'):
        if getattr(args, name):
            command.append('--' + name.replace('_', '-'))
    if args.world_rolling_slices is not None:
        command += ['--world-rolling-slices', str(args.world_rolling_slices)]
    if args.economy_watch is not None:
        command += ['--economy-watch', args.economy_watch,
                    '--economy-watch-window', *map(str, args.economy_watch_window)]
    if args.navmesh_post_observe is not None:
        command += ['--navmesh-post-observe', *map(str, args.navmesh_post_observe)]
    if args.world_motion_watch is not None:
        command += ['--world-motion-watch', *args.world_motion_watch,
                    '--world-motion-window', *map(str, args.world_motion_window)]
    if args.status_stride is not None:
        command += ['--status-stride', str(args.status_stride)]
    if args.probe_profile:
        command.append('--probe-profile')
    if args.seed_rng:
        command.append('--seed-rng')
    if args.clients > 2:
        command.append('--session-clients')
    if args.path_determinism:
        command += ['--path-determinism', str(args.path_determinism)]
    if args.region_rng:
        command += ['--region-rng', args.region_rng]
    if args.ai_wait_rng:
        command.append('--ai-wait-rng')
    if args.md_resume_rng:
        command.append('--md-resume-rng')
    if args.boot_hold:
        command.append('--boot-hold')
    if args.event_order:
        command.append('--event-order')
    if args.event_rng:
        command += ['--event-rng', args.event_rng]
    if args.event_rng_rows_window:
        command += ['--event-rng-rows-window', *map(str, args.event_rng_rows_window)]
    if args.event_rows is not None:
        command += ['--event-rows', str(args.event_rows)]
    if args.gravidar:
        command.append('--gravidar')
    if args.gravidar_deadlines:
        command.append('--gravidar-deadlines')
    if args.travel_snapshot:
        command.append('--travel-snapshot')
    if args.travel_determinism:
        command.append('--travel-determinism')
    if args.path_transform_observe:
        command += ['--path-transform-observe', *map(str, args.path_transform_observe)]
    if args.runtime_ids:
        command.append('--runtime-ids')
    if args.runtime_id_callers:
        command.append('--runtime-id-callers')
    if args.runtime_ids_local:
        command.append('--runtime-ids-local')
    if args.script_rng:
        command += ['--script-rng', args.script_rng]
    if args.script_rng_mask is not None:
        command += ['--script-rng-mask', str(args.script_rng_mask)]
    if args.script_rng_rows:
        command += ['--script-rng-rows', str(args.script_rng_rows)]
    if args.script_rng_rows_window:
        command += ['--script-rng-rows-window', *map(str, args.script_rng_rows_window)]
    if args.script_rng_natural:
        command.append('--script-rng-natural')
    if args.script_rng_trace:
        command.append('--script-rng-trace')
    if args.script_rng_unmapped:
        command.append('--script-rng-unmapped')
    if args.md_timed_rng:
        command.append('--md-timed-rng')
    if args.find_order:
        command += ['--find-order', args.find_order]
    if args.loadout_order:
        command += ['--loadout-order', args.loadout_order]
    if args.attention_all:
        command.append('--attention-all')
    if args.step_trace:
        command += ['--step-trace', *args.step_trace, '--step-trace-ticks', str(args.step_trace_ticks)]
        if args.step_chain:
            command.append('--step-chain')
    if args.fp_redirect:
        command += ['--fp-redirect', args.fp_redirect, '--fp-redirect-ticks', str(args.fp_redirect_ticks)]
        if args.fp_redirect_solver:
            command += ['--fp-redirect-solver', args.fp_redirect_solver]
        if args.fp_redirect_drones:
            command += ['--fp-redirect-drones', args.fp_redirect_drones]
        if args.fp_redirect_watch:
            command += ['--fp-redirect-watch', *args.fp_redirect_watch]
    if args.coverage:
        command += ['--coverage', str(args.coverage), '--coverage-list', str(args.coverage_list)]
        if args.coverage_from is not None:
            command += ['--coverage-from', str(args.coverage_from)]
    if args.tick_hz is not None:
        command += ['--tick-hz', str(args.tick_hz)]
    if args.loadout_order_rows:
        command += ['--loadout-order-rows', *(str(t) for t in args.loadout_order_rows)]
    if args.gravidar_wide:
        command += ['--gravidar-wide', args.gravidar_wide]
    if args.nav_cadence:
        command += ['--nav-cadence', args.nav_cadence]
    if args.nav_params_init:
        command.append('--nav-params-init')
    if args.nav_key:
        command.append('--nav-key')
    if args.nav_probe_init:
        command.append('--nav-probe-init')
    if args.created_identities:
        command.append('--created-identities')
    if args.created_load:
        command.append('--created-load')
    if args.portable_created:
        command.append('--portable-created')
    if args.spawn_rng:
        command += ['--spawn-rng', args.spawn_rng]
    if args.weapon_fire_rng:
        command.append('--weapon-fire-rng')
    if args.engine_refresh_rng:
        command.append('--engine-refresh-rng')
    if args.build_rng:
        command.append('--build-rng')
    if args.economy_load_rng:
        command.append('--economy-load-rng')
    if args.spline_join_all:
        command.append('--spline-join-all')
    if args.actor_rng_all:
        command.append('--actor-rng-all')
    if args.actor_rng_rows:
        command.append('--actor-rng-rows')
    if args.player_timer:
        command.append('--player-timer')
    if args.tempzone_order:
        command.append('--tempzone-order')
    if args.warp_rng:
        command += ['--warp-rng', args.warp_rng]
    if args.warp_exit:
        command.append('--warp-exit')
    if args.flight_limits:
        command.append('--flight-limits')
    if args.body1_mass:
        command += ['--body1-mass', args.body1_mass]
    if args.body_step_diag:
        command.append('--body-step-diag')
    if args.velocity_diag:
        command.append('--velocity-diag')
    if args.impulse_diag:
        command.append('--impulse-diag')
    if args.ik_diag:
        command.append('--ik-diag')
    if args.ik_step:
        command.append('--ik-step')
    if args.steer_roll:
        command.append('--steer-roll')
    if args.ik_wrap:
        command.append('--ik-wrap')
    if args.shape_refresh:
        command += ['--shape-refresh', args.shape_refresh]
    for name in ('nav_diag', 'nav_rng', 'nav_initial_refresh', 'attention_rng', 'attention_initial_refresh', 'shape_rng', 'shape_initial_refresh', 'radar_rng', 'radar_initial_refresh'):
        if getattr(args, name):
            command.append('--' + name.replace('_', '-'))
    if args.world_hash_shields:
        command.append('--world-hash-shields')
    if args.mining_camera:
        command.append('--mining-camera')
    if args.mass_traffic:
        command += ['--mass-traffic', args.mass_traffic]
    if args.region_pass:
        command += ['--region-pass', args.region_pass]
    if args.pass_stride is not None:
        command += ['--pass-stride', str(args.pass_stride)]
    if args.serial_pathing or (args.path_determinism or 0) & 1:
        command.append('--serial-pathing')
    if args.fork_save:
        command += ['--fork-save', args.fork_save, '--fork-save-budget-ms', str(args.fork_save_budget_ms), '--fork-save-watchdog', str(args.fork_save_watchdog)]
        if args.fork_save_every:
            command += ['--fork-save-every', str(args.fork_save_every)]
        if args.fork_save_slot:
            command += ['--fork-save-slot', str(args.fork_save_slot)]
    for saved in args.world_hash_watch or []:
        command += ['--world-hash-watch', saved]
    if args.world_health_ticks:
        command += ['--world-health-ticks', *(str(x) for x in args.world_health_ticks)]
    if args.fp_attack_capture:
        command += ['--fp-attack-capture', *(str(x) for x in args.fp_attack_capture)]
    for saved in args.fp_attack_watch or []:
        command += ['--fp-attack-watch', saved]
    subprocess.run(command, check=True)
    extension = out / 'extension/x4_lan_lab'
    compose_extension_template(overlay, extension, args.extension_base)
    if args.portable_math:
        subprocess.run([sys.executable, str(repo / 'tools/dev/build_portable_math_preload.py'), '--zig', str(args.zig)], check=True)
        (out / 'portable_math').mkdir()
        shutil.copy2(repo / '.local/portable-math/preload' / PORTABLE_MATH, out / 'portable_math' / PORTABLE_MATH)
    missing = [name for name in ('ui/native_gate_probe.lua', 'ui/sha256.lua') if not (extension / name).is_file()]
    if missing:
        raise SystemExit(f'the composed lockstep extension lacks required files: {missing}')
    config = (extension / 'ui/config.lua').read_text()
    for token in ('@X4LANE_PORT@', '@X4LANE_TOKEN@', '@X4LANE_SLOT@'):
        if token not in config:
            raise SystemExit(f'composed config lacks {token}')
    if snapshot_sha not in config:
        raise SystemExit('composed config does not pin the snapshot')
    if args.created_rows:
        with (extension / 'ui/config.lua').open('a', encoding='utf-8', newline='\n') as handle:
            handle.write('X4LAN_CONFIG.native_gate_created_rows = true\n')
    if args.wire_crypto:
        # The slot's credential depends on the session token: filled in at run time (run --wire-crypto-credentials).
        with (extension / 'ui/config.lua').open('a', newline='\n') as handle:
            handle.write(f'X4LAN_CONFIG.native_gate_wire_crypto = "{WIRE_CRYPTO_TOKEN}"\n')
    (out / 'snapshot').mkdir()
    shutil.copy2(args.snapshot, out / 'snapshot' / SLOT_SAVE)
    shutil.copy2(repo / 'tools/dev/native_gate_relay.py', out / 'native_gate_relay.py')
    shutil.copy2(Path(__file__), out / 'linux_gate_live.py')
    shutil.copy2(Path(__file__).with_name('linux_gate_evidence.py'), out / 'linux_gate_evidence.py')
    for helper in ('world_motion_evidence.py', 'native_world_evidence.py', 'economy_watch_config.py', 'navmesh_post_config.py'):
        shutil.copy2(Path(__file__).with_name(helper), out / helper)
    shutil.rmtree(work)
    commit = subprocess.run(['git', '-C', str(repo), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    files = {p.relative_to(out).as_posix(): sha(p) for p in sorted(out.rglob('*')) if p.is_file()}
    recorded_options = (
        'path_determinism',
        'region_rng',
        'ai_wait_rng',
        'md_resume_rng',
        'boot_hold',
        'event_order',
        'event_rows',
        'event_rng',
        'event_rng_rows_window',
        'gravidar',
        'gravidar_deadlines',
        'travel_snapshot',
        'travel_determinism',
        'path_transform_observe',
        'navmesh_post_observe',
        'world_log_mode',
        'world_rolling_slices',
        'status_stride',
        'probe_profile',
        'runtime_ids',
        'runtime_id_callers',
        'runtime_ids_local',
        'script_rng',
        'script_rng_mask',
        'script_rng_rows',
        'script_rng_rows_window',
        'script_rng_trace',
        'md_timed_rng',
        'find_order',
        'loadout_order',
        'loadout_order_rows',
        'attention_all',
        'nav_key',
        'step_trace',
        'step_trace_ticks',
        'step_chain',
        'fp_redirect',
        'fp_redirect_watch',
        'fp_redirect_ticks',
        'fp_redirect_solver',
        'coverage',
        'coverage_from',
        'gravidar_wide',
        'spawn_rng',
        'weapon_fire_rng',
        'engine_refresh_rng',
        'build_rng',
        'economy_load_rng',
        'spline_join_all',
        'actor_rng_all',
        'actor_rng_rows',
        'player_timer',
        'tempzone_order',
        'warp_rng',
        'warp_exit',
        'flight_limits',
        'body1_mass',
        'body_step_diag',
        'velocity_diag',
        'impulse_diag',
        'ik_diag',
        'ik_step',
        'ik_wrap',
        'world_hash_shields',
        'mining_camera',
        'mass_traffic',
        'region_pass',
        'pass_stride',
        'created_identities',
        'created_load',
        'portable_created',
        'nav_cadence',
        'nav_params_init',
        'nav_probe_init',
        'clients',
        'fp_redirect_drones',
        'world_health_ticks',
        'fp_attack_capture',
        'fp_attack_watch',
        'serial_pathing',
        'script_rng_unmapped',
        'script_rng_natural',
    )
    manifest = {
        'commit': commit,
        'source_dirty': bool(subprocess.run(
            ['git', '-C', str(repo), 'status', '--porcelain'],
            check=True, capture_output=True, text=True).stdout),
        'ticks': args.ticks,
        'stride': args.stride,
        'snapshot_sha256': snapshot_sha,
        'wire_crypto': bool(args.wire_crypto),
        'gate_so_sha256': files['extension/x4_lan_lab/native/x4lan_gate.so'],
        'files': files,
        'portable_math_sha256': files.get(f'portable_math/{PORTABLE_MATH}'),
        'seed_rng': bool(args.seed_rng),
        'world_hash_watch': args.world_hash_watch or [],
        'fork_save': args.fork_save and {
            'request': args.fork_save,
            'every': args.fork_save_every,
            'slot': args.fork_save_slot,
            'budget_ms': args.fork_save_budget_ms,
            'watchdog_s': args.fork_save_watchdog,
        },
    }
    manifest.update({name: getattr(args, name) for name in recorded_options})
    manifest['economy_watch'] = args.economy_watch
    manifest['economy_watch_window'] = args.economy_watch_window
    manifest['world_motion_watch'] = args.world_motion_watch
    manifest['world_motion_window'] = args.world_motion_window
    manifest['created_rows'] = bool(args.created_rows)
    manifest['snapshot_seed'] = world_seed
    manifest['preset'] = args.preset
    manifest['tick_hz'] = args.tick_hz or 60  # the clients' native_gate_tick_hz; the relay runs with the same
    manifest['mixed_platform'] = bool(args.mixed_platform)  # MIXED_PLATFORM_SETS applied (tools/build_play_overlay.py)
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (out / MARKER).write_text('Disposable Linux lockstep test bundle.\n', encoding='utf-8')
    print(f'bundle {out}: {len(files)} files, gate {manifest["gate_so_sha256"][:16]}, snapshot {snapshot_sha[:16]}, commit {commit[:8]}')


# ------------------------------------------------------------------ run


def processes():
    result = {}
    for path in Path('/proc').glob('[0-9]*'):
        try:
            stat = (path / 'stat').read_text().rsplit(')', 1)[1].split()
            result[int(path.name)] = {'parent': int(stat[1]), 'start': int(stat[19]), 'uid': path.stat().st_uid,
                                      'name': (path / 'comm').read_text().strip()}
        except (OSError, ValueError):
            pass
    return result


def x4_executables():
    """(pid, executable) of every running X4. X4 renames its main thread (comm is 'Main()'),
    so only /proc/<pid>/exe identifies it."""
    found = []
    for path in Path('/proc').glob('[0-9]*'):
        try:
            link = os.readlink(path / 'exe')
        except OSError:
            continue
        link = link[:-len(' (deleted)')] if link.endswith(' (deleted)') else link
        if Path(link).name == 'X4':
            found.append((int(path.name), link))
    return sorted(found)


def stop_error(reasons, stopped_at, now, clients=2):
    """The error that ends the run now, or None to keep waiting. reasons and stopped_at map
    the slots seen stopping to their reason (the text of a gate error) and to when they were seen."""
    failed = {slot: reason for slot, reason in reasons.items() if reason != 1}
    if failed:
        settled = len(reasons) == clients and now - max(stopped_at.values()) >= FAILED_STOP_SETTLE
        if settled or now - min(stopped_at[slot] for slot in failed) >= FAILED_STOP_PEER:
            return '; '.join(f'client {slot}: ' + (reason if isinstance(reason, str) else f'NATIVE_GATE STOP reason={reason}')
                             for slot, reason in sorted(failed.items()))
        return None
    if reasons and len(reasons) < clients and now - min(stopped_at.values()) >= PEER_STOP:
        return f'only client(s) {sorted(reasons)} stopped; no peer STOP within {PEER_STOP} s'
    return None


def start_ticks(pid):
    return int(Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[19])



def create_lock_target(path, uid, gid):
    """Claim a new mount point atomically; never follow or take an existing path."""
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    try:
        if os.geteuid() == 0:
            os.fchown(fd, uid, gid)
        return fd, os.fstat(fd)
    except BaseException:
        stat = os.fstat(fd)
        try:
            current = path.lstat()
            if (current.st_dev, current.st_ino) == (stat.st_dev, stat.st_ino):
                path.unlink()
        finally:
            os.close(fd)
        raise


def release_lock_target(path, fd, stat):
    """Remove only the inode created by this run, retaining replacements."""
    try:
        current = path.lstat()
        if (current.st_dev, current.st_ino) != (stat.st_dev, stat.st_ino):
            raise RuntimeError('lock target replaced; left untouched')
        path.unlink()
    except FileNotFoundError:
        raise RuntimeError('lock target disappeared') from None
    finally:
        os.close(fd)


def owned_group_members(group_id, started):
    """Report surviving members of a launch group without signalling unknown PIDs."""
    found = []
    for path in Path('/proc').glob('[0-9]*'):
        try:
            fields = (path / 'stat').read_text().rsplit(')', 1)[1].split()
            if int(fields[2]) == group_id and int(fields[19]) >= started:
                found.append(int(path.name))
        except (OSError, ValueError, IndexError):
            continue
    return sorted(found)


def stop_owned_process(proc, started):
    """Signal only while the group leader still has the recorded identity."""
    def signal_if_owned(sig):
        if proc.poll() is None and start_ticks(proc.pid) == started:
            os.killpg(proc.pid, sig)
            return True
        return False

    try:
        if signal_if_owned(signal.SIGTERM):
            try:
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                if signal_if_owned(signal.SIGKILL):
                    proc.wait(timeout=10)
    except ProcessLookupError:
        pass


def wait_owned_groups(starts, timeout=5):
    """Allow exiting descendants to settle; retain all survivors as failure evidence."""
    deadline = time.monotonic() + timeout
    while True:
        remaining = {pid: owned_group_members(pid, start) for pid, start in starts.items()}
        remaining = {pid: members for pid, members in remaining.items() if members}
        if not remaining or time.monotonic() >= deadline:
            return remaining
        time.sleep(0.1)


def memory_available_gib():
    for line in Path('/proc/meminfo').read_text().splitlines():
        if line.startswith('MemAvailable:'):
            return int(line.split()[1]) / (1024 * 1024)
    return 0.0


def steam_identity(userhome, uid):
    pid = int((userhome / '.steam/steam.pid').read_text().strip())
    row = processes().get(pid)
    if not row or row['uid'] != uid or row['name'] != 'steam':
        raise RuntimeError('the running Steam client of the game user is required')
    return {'pid': pid, 'start': row['start'], 'uid': uid}


def steam_shared_memory(shm, uid):
    """Whether shm holds the Steam client's shared memory for uid (u<uid>-Shm_* owned by uid). The directory itself is
    usually root's with mode 1777, as /dev/shm is (snap: /dev/shm/snap.steam)."""
    try:
        return any(p.name.startswith(f'u{uid}-Shm_') and p.stat().st_uid == uid for p in shm.iterdir())
    except OSError:
        return False


def bounded_tail(path, limit=1 << 20):
    try:
        with open(path, 'rb') as stream:
            stream.seek(0, os.SEEK_END)
            size = stream.tell()
            stream.seek(max(0, size - limit))
            return stream.read().decode(errors='replace')
    except FileNotFoundError:
        return ''


class LogWatch:
    """Reads a growing X4 log incrementally (it can grow by megabytes per second with
    -debug scripts) and remembers the gate's milestone lines once seen."""

    def __init__(self, path):
        self.path, self.offset, self.carry = path, 0, b''
        self.armed = self.stopped = False
        self.stop_reason = self.failure = None

    def poll(self):
        try:
            with open(self.path, 'rb') as stream:
                stream.seek(self.offset)
                data = stream.read(64 << 20)
        except FileNotFoundError:
            return
        self.offset += len(data)
        lines = (self.carry + data).split(b'\n')
        self.carry = lines.pop()
        for line in lines:
            if b'NATIVE_GATE' not in line:
                continue
            text = line.decode(errors='replace')
            if 'NATIVE_GATE ARMED' in text:
                self.armed = True
            if 'NATIVE_GATE STOP ' in text:
                self.stopped = True
                reason = re.search(r'NATIVE_GATE STOP .*?reason=(\d+)', text)
                self.stop_reason = int(reason.group(1)) if reason else -1
            # As the Windows lane runs: the session defaults' loud rows (a degraded counter grew, a default skipped) are
            # recorded, not fatal; every other ERROR and any REFUSED is.
            if self.failure is None and is_fatal_gate_message(text):
                self.failure = text[text.index('NATIVE_GATE'):][:300]


def gate_lines(path):
    """The [X4LAN] NATIVE_GATE lines of a finished log, streamed."""
    with open(path, 'rb') as stream:
        for raw in stream:
            if b'NATIVE_GATE' in raw:
                yield raw.decode(errors='replace').rstrip('\r\n')


def user_command(command, user):
    """Switch users only for a root-owned server harness, never on a desktop."""
    import pwd

    current_user = pwd.getpwuid(os.geteuid()).pw_name
    if user == current_user:
        return list(command)
    if os.geteuid() != 0:
        raise SystemExit(f'cannot run as {user}: this process belongs to {current_user}')
    return ['runuser', '-u', user, '--', *command]


def lavapipe_icd(directory=Path('/usr/share/vulkan/icd.d')):
    """Locate the 64-bit software driver on Debian and Fedora installations."""
    for name in ('lvp_icd.x86_64.json', 'lvp_icd.json'):
        candidate = directory / name
        if candidate.is_file():
            return candidate
    return None


def desktop_display(display):
    """Validate an explicit local X11 display and its existing authentication file."""
    if not re.fullmatch(r':\d+(?:\.\d+)?', display):
        raise SystemExit('--display must name a local X11 display, for example :0')
    authorization = os.environ.get('XAUTHORITY')
    if not authorization or not Path(authorization).is_file():
        raise SystemExit('--display requires an existing XAUTHORITY file in the environment')
    return display, Path(authorization).resolve(strict=True)


def namespace_command(case, game, dlc_binds, lock, env, program, user, shm=None):
    """bwrap view as tools/dev/linux_native_admission_run.py: read-only host, private case,
    game read-only at case/game, extension directory replaced by this case's allowlist.
    shm (run --shm): a directory bound over /dev/shm, where a confined Steam client keeps its
    shared memory (the snap's /dev/shm/snap.steam), so X4 outside the snap reaches it."""
    game = game.resolve(strict=True)
    command = user_command(['bwrap', '--ro-bind', '/', '/', '--proc', '/proc', '--dev-bind', '/dev', '/dev'], user)
    if shm:
        command += ['--bind', str(shm), '/dev/shm']
    command += ['--bind', str(case), str(case), '--ro-bind', str(game), str(case / 'game')]
    for source, view in dlc_binds:
        command += ['--ro-bind', str(source), str(view)]
    command += ['--ro-bind', str(case / 'extensions'), str(case / 'game/extensions'),
                '--ro-bind', str(case / 'extensions'), str(game / 'extensions'), '--bind', str(lock), '/tmp/EGOSOFT_X4.lock',
                '--die-with-parent', '--chdir', str(case / 'game'), '--clearenv']
    for key, value in env.items():
        command += ['--setenv', key, value]
    return command + ['--', *program]


def portable_math_env(case):
    """The preload of one instance: the Linux side of the Windows FP keys. It stops X4 (exit 97) on
    any mismatch; its log line says what is active."""
    return {'LD_PRELOAD': str(case / PORTABLE_MATH), 'X4MP_PORTABLE_DECIMAL': '1', 'X4MP_EXACT_RECIPROCAL': '1',
            'X4MP_PORTABLE_MATH_LOG': str(case / 'portable-math.log')}


def portable_math_state(case):
    """(active, log lines) of one instance's preload. Every process X4 starts inherits LD_PRELOAD
    and appends its own line (dbus-launch is refused with exit 97, not being X4), so only the
    lines of this instance's X4 say whether the patches are active."""
    try:
        lines = (case / 'portable-math.log').read_text(errors='replace').splitlines()
    except OSError:
        return False, []
    own = [line for line in lines if line.endswith(f' exe={case}/game/X4')]
    active = any(PORTABLE_MATH_ACTIVE.search(line) for line in own) and not any('REFUSED' in line for line in own)
    return active, [line[:300] for line in lines[:16]]


WIRE_CRYPTO_TOKEN = '@X4LANE_WIRE_CRYPTO@'
CREDENTIAL = re.compile(r'x4c1:[0-9a-f]{64}:[0-9a-f]{64}')


def case_config(text, port, token, slot, clients, credential=None):
    """A client's config.lua: the session's port, token, slot and size; with wire crypto (bundle --wire-crypto) the slot's
    credential (native_gate_crypto.py credential for this token and slot; a secret, never logged)."""
    wants = WIRE_CRYPTO_TOKEN in text
    if wants and not credential:
        raise RuntimeError(f'the bundle has wire crypto: slot {slot} needs a credential (run --wire-crypto-credentials)')
    if credential and not wants:
        raise RuntimeError('a wire crypto credential, but the bundle has no wire crypto line (bundle --wire-crypto)')
    if credential and not CREDENTIAL.fullmatch(credential):
        raise RuntimeError(f'slot {slot}: not a wire crypto credential (x4c1:<64 hex>:<64 hex>)')
    text = (text.replace('@X4LANE_PORT@', str(port)).replace('@X4LANE_TOKEN@', token)
            .replace('@X4LANE_SLOT@', f'p{slot}').replace('@X4LANE_CLIENTS@', str(clients)))
    return text.replace(WIRE_CRYPTO_TOKEN, credential or '')


def load_credentials(path):
    """--wire-crypto-credentials FILE: JSON {"<slot>": "x4c1:...", ...}, readable by its owner only (it is a secret)."""
    if os.name == 'posix' and path.stat().st_mode & 0o077:
        raise SystemExit(f'{path}: a credential file must not be readable by others (chmod 600)')
    data = json.loads(path.read_text())
    if not isinstance(data, dict) or not data or any(not str(k).isdigit() or not isinstance(v, str) or not CREDENTIAL.fullmatch(v)
                                                     for k, v in data.items()):
        raise SystemExit(f'{path}: expected {{"<slot>": "x4c1:<64 hex>:<64 hex>", ...}}')
    return {int(k): v for k, v in data.items()}


def stage_case(root, run, slot, game, userhome, port, token, clients=2, credential=None, snapshot=True):
    """One private instance: HOME, profile 45841 (autosave off), snapshot, extensions."""
    case = run / f'case{slot}'
    profile = case / 'home/.config/EgoSoft/X4/45841'
    for path in (profile / 'save', case / 'tmp', case / 'cache', case / 'data', case / 'runtime', case / 'game',
                 case / 'extensions', case / 'home/.steam'):
        path.mkdir(parents=True, exist_ok=True)
    (case / 'runtime').chmod(0o700)
    # This effect produced RGB speckles on the tested RADV setup, even without
    # native hooks. Disable it only in this disposable diagnostic profile.
    (profile / 'config.xml').write_text('<?xml version="1.0"?><root><autosave>false</autosave><uisafemode>false</uisafemode>'
                                        '<fullscreen>false</fullscreen><borderless>false</borderless><res_width>800</res_width>'
                                        '<res_height>600</res_height><antialiasing>0</antialiasing><vsync>false</vsync>'
                                        '<chromaticaberration>false</chromaticaberration></root>\n')
    if snapshot:
        shutil.copyfile(root / 'snapshot' / SLOT_SAVE, profile / 'save' / SLOT_SAVE)
    for name in ('root', 'steam.pid', 'sdk32', 'sdk64'):
        source = userhome / '.steam' / name
        if source.exists():
            (case / 'home/.steam' / name).symlink_to(source)
    (case / 'home/.steam/steam').symlink_to('root')
    dlc_binds = []
    for extension in sorted((game / 'extensions').glob('ego_dlc_*')):
        if extension.is_dir():
            view = case / 'dlc-source' / extension.name
            view.mkdir(parents=True)
            dlc_binds.append((extension.resolve(strict=True), view))
            (case / 'extensions' / extension.name).symlink_to(view)
    lab = case / 'extensions/x4_lan_lab'
    shutil.copytree(root / 'extension/x4_lan_lab', lab)
    config = lab / 'ui/config.lua'
    config.write_text(case_config(config.read_text(), port, token, slot, clients, credential))
    if credential:
        config.chmod(0o600)
    ids = sorted(p.name for p in (case / 'extensions').iterdir())
    if any(not (n.startswith('ego_dlc_') or n == 'x4_lan_lab') for n in ids):
        raise RuntimeError('unexpected extension in the private view')
    (profile / 'content.xml').write_text('<?xml version="1.0"?><content>' + ''.join(
        f'<extension id="{n}" enabled="true" />' for n in ids) + '</content>\n')
    if (root / 'portable_math' / PORTABLE_MATH).is_file():
        shutil.copyfile(root / 'portable_math' / PORTABLE_MATH, case / PORTABLE_MATH)
    lock = case / 'x4.lock'
    lock.touch()
    return case, profile, dlc_binds, lock, ids


def launch_order(text, clients):
    """The client numbers in the order they start, from "2,1"; by default 1..clients."""
    if not text:
        return list(range(1, clients + 1))
    order = [int(part) for part in text.split(',')]
    if sorted(order) != list(range(1, clients + 1)):
        raise SystemExit(f'--launch-order must name each of the clients 1..{clients} once: {text}')
    return order


def collect_run_evidence(run_dir, slots, cases, ticks, manifest):
    """Retain both logs and installed config, then validate the complete evidence."""
    result = {}
    logs_by_slot = {}
    for slot, (case, profile, *_rest) in zip(slots, cases):
        client = run_dir / f'client{slot}'
        (client / 'installed').mkdir(parents=True, exist_ok=True)
        log = profile / 'engine-output.log'
        if log.exists():
            shutil.copyfile(log, client / 'engine-output.log')
        shutil.copyfile(case / 'extensions/x4_lan_lab/ui/config.lua', client / 'installed/x4_lan_lab-config.lua')
        if (case / 'portable-math.log').is_file():
            shutil.copyfile(case / 'portable-math.log', client / 'portable-math.log')
        retained_log = client / 'engine-output.log'
        logs_by_slot[slot] = gate_lines(retained_log) if retained_log.exists() else ()
    result.update(validate_evidence(logs_by_slot, ticks, manifest['stride'],
                                    rolling_slices=manifest.get('world_rolling_slices'),
                                    motion_watch=manifest.get('world_motion_watch'),
                                    motion_window=manifest.get('world_motion_window')))
    try:
        result['relay'] = json.loads((run_dir / 'relay.json').read_text())
    except (OSError, ValueError):
        result['relay'] = None
    if manifest.get('portable_math_sha256'):
        states = [portable_math_state(case) for case, *_rest in cases]
        result['portable_math'] = [lines for _, lines in states]
        result['portable_math_active'] = [active for active, _ in states]
    return result


def run(args):
    import pwd
    try:
        user = pwd.getpwnam(args.user)
    except KeyError:
        raise SystemExit(f'REFUSED: no user {args.user} on this host (--user names the Steam user)') from None
    if os.geteuid() not in (0, user.pw_uid):
        raise SystemExit('run as the selected Steam user or as root')
    userhome = Path(user.pw_dir).resolve(strict=True)
    root = args.root.resolve(strict=True)
    if (root == userhome or not root.is_relative_to(userhome)
            or not (root / MARKER).is_file() or root.stat().st_uid not in (0, user.pw_uid)):
        raise SystemExit(f'{root} is not a staged live-test root under {userhome} with {MARKER}')
    manifest = json.loads((root / 'manifest.json').read_text())
    for rel, digest in manifest['files'].items():
        if sha(root / rel) != digest:
            raise SystemExit(f'bundle file changed: {rel}')
    world_seed = snapshot_seed(root / 'snapshot' / SLOT_SAVE)
    if manifest.get('snapshot_seed', world_seed) != world_seed:
        raise SystemExit('bundle seed differs from its snapshot')
    # A desktop host (e.g. snap Steam) keeps the game, the Steam client's .steam and its shared memory elsewhere:
    # --game, --steam-home and --shm name them; the defaults are the VPS dedicated-server layout.
    game_path = args.game or userhome / 'x4/game'
    try:
        game = game_path.resolve(strict=True)
    except OSError:
        raise SystemExit(f'REFUSED: no X4 installation at {game_path}') from None
    steam_home = args.steam_home or userhome
    shm = args.shm.resolve(strict=True) if args.shm else None
    problems = []
    if sha(game / 'X4') != EXE_SHA:
        problems.append('X4 is not the pinned Linux 9.00 executable')
    if x4_executables():
        problems.append('another X4 is running (stop the dedicated server first)')
    display = desktop_display(args.display) if args.display else None
    required_tools = ['bwrap']
    if os.geteuid() != user.pw_uid:
        required_tools.append('runuser')
    if display is None:
        required_tools.extend(('Xvfb', 'xauth'))
    for tool in required_tools:
        if not shutil.which(tool):
            problems.append(f'{tool} missing')
    vulkan_driver = args.vulkan_icd or lavapipe_icd()
    if vulkan_driver is None or not vulkan_driver.is_file():
        problems.append('Vulkan driver missing: install Mesa lavapipe or supply --vulkan-icd')
    else:
        vulkan_driver = vulkan_driver.resolve(strict=True)
    memory = memory_available_gib()
    clients = manifest.get('clients', 2)
    external = args.relay_port is not None
    if external != bool(args.relay_token) or external != bool(args.local_slots):
        raise SystemExit('--relay-port, --relay-token and --local-slots go together')
    if external and (not 1024 <= args.relay_port <= 65535 or not re.fullmatch(r'[0-9a-f]{32}', args.relay_token)):
        raise SystemExit('--relay-port must be 1024..65535 and --relay-token 32 lowercase hex')
    local = sorted({int(part) for part in args.local_slots.split(',')}) if external else list(range(1, clients + 1))
    if not local or any(not 1 <= slot <= clients for slot in local):
        raise SystemExit(f'--local-slots must name slots of 1..{clients}')
    needed = args.min_memory_gib if args.min_memory_gib is not None else 6.5 * len(local)
    if memory < needed:
        problems.append(f'MemAvailable {memory:.1f} GiB < {needed} GiB for {clients} instances')
    try:
        steam = steam_identity(steam_home, user.pw_uid)
    except (OSError, ValueError, RuntimeError) as error:
        problems.append(str(error))
        steam = None
    if shm and not steam_shared_memory(shm, user.pw_uid):
        problems.append(f'--shm {shm} holds no shared memory of the Steam client of {args.user} (u{user.pw_uid}-Shm_*)')
    if problems:
        raise SystemExit('REFUSED: ' + '; '.join(problems))
    layer = args.discard_layer or Path('/opt/x4mp/vklayer')
    discard = layer.is_dir() and not args.no_discard
    if args.discard_layer and not discard and not args.no_discard:
        raise SystemExit(f'REFUSED: --discard-layer {layer} is not a directory')
    if args.preflight:
        print('PREFLIGHT OK ' + json.dumps({'game': str(game), 'memory_available_gib': round(memory, 1), 'steam': steam,
                                            'steam_home': str(steam_home), 'shm': shm and str(shm), 'discard_layer': discard and str(layer),
                                            'bundle_commit': manifest['commit'],
                                            'portable_math': bool(manifest.get('portable_math_sha256')), 'seed_rng': manifest.get('seed_rng'),
                                            'gate_so_sha256': manifest['gate_so_sha256']}))
        return
    ticks = manifest['ticks'] if args.ticks is None else args.ticks
    if manifest.get('navmesh_post_observe') is not None:
        from navmesh_post_config import validate_window
        validate_window(manifest['navmesh_post_observe'], ticks)
    if manifest.get('economy_watch') is not None or manifest.get('economy_watch_window') is not None:
        from economy_watch_config import validate_configuration
        validate_configuration(manifest.get('economy_watch'), manifest.get('economy_watch_window'), ticks)
    if manifest.get('world_motion_watch') is not None or manifest.get('world_motion_window') is not None:
        from world_motion_evidence import validate_configuration
        validate_configuration(manifest.get('world_motion_watch'), manifest.get('world_motion_window'), ticks)
    if ticks < 1 or manifest['stride'] < 1:
        raise SystemExit('--ticks and the bundle stride must be positive')
    name = args.name or time.strftime('live-%Y%m%d-%H%M%S')
    if not re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9_.-]*', name):
        raise SystemExit('--name must be a single directory name containing letters, digits, dots, underscores or hyphens')
    run_dir = root / 'runs' / name
    run_dir.mkdir(parents=True, exist_ok=False)
    if external:
        port, token = args.relay_port, args.relay_token
    else:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reserve:
            reserve.bind(('127.0.0.1', 0))
            port = reserve.getsockname()[1]
        token = secrets.token_hex(16)
    order = [slot for slot in launch_order(args.launch_order, clients) if slot in local]
    credentials = {}
    if args.wire_crypto_credentials:
        if not external:
            raise SystemExit('--wire-crypto-credentials needs an external relay with --wire-crypto (--relay-port)')
        credentials = load_credentials(args.wire_crypto_credentials)
        if any(slot not in credentials for slot in local):
            raise SystemExit(f'--wire-crypto-credentials lacks a credential for slots {[s for s in local if s not in credentials]}')
    by_slot = {slot: stage_case(root, run_dir, slot, game, steam_home, port, token, clients, credentials.get(slot)) for slot in local}
    cases = [by_slot[slot] for slot in local]
    for path in [run_dir, *run_dir.rglob('*')]:
        if os.geteuid() == 0 and not path.is_symlink():
            os.chown(path, user.pw_uid, user.pw_gid)
    result = {'name': name, 'ticks': ticks, 'port': port, 'memory_available_gib': round(memory, 1), 'discard_layer': discard,
              'manifest_commit': manifest['commit'], 'gate_so_sha256': manifest['gate_so_sha256'], 'steam_before': steam,
              'portable_math_sha256': manifest.get('portable_math_sha256'), 'seed_rng': manifest.get('seed_rng'), 'launch_order': order,
              'external_relay': external, 'local_slots': local, 'snapshot_seed': world_seed}
    procs, starts, logs = [], {}, []
    cancelled = []
    # bwrap binds each private lock over this path, and cannot create it in the read-only view.
    lock_target = Path('/tmp/EGOSOFT_X4.lock')
    try:
        lock_fd, lock_stat = create_lock_target(lock_target, user.pw_uid, user.pw_gid)
    except OSError as error:
        raise SystemExit(f'REFUSED: cannot exclusively create lock target {lock_target}: {error}') from error
    for number in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(number, lambda *_: cancelled.append(1))

    def launch(label, command, env=None, as_user=False):
        log = (run_dir / f'{label}.log').open('wb')
        logs.append(log)
        extra = {'user': user.pw_uid, 'group': user.pw_gid, 'extra_groups': []} if as_user and os.geteuid() == 0 else {}
        parent = os.getpid()

        def parent_death():
            if ctypes.CDLL(None).prctl(1, signal.SIGKILL) != 0 or os.getppid() != parent:
                os._exit(125)
        proc = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
                                env=env, preexec_fn=parent_death, **extra)
        starts[proc.pid] = start_ticks(proc.pid)
        procs.append(proc)
        return proc
    try:
        relay = None
        if not external:
            relay = launch('relay', [sys.executable, str(root / 'native_gate_relay.py'), '--port', str(port), '--token', token,
                                     '--ticks', str(ticks), '--report', str(run_dir / 'relay.json'), '--relative-real', '--observe',
                                     '--tick-hz', str(manifest.get('tick_hz', 60)),
                                     '--clients', str(clients)], as_user=True)
            deadline = time.monotonic() + 10
            while 'NATIVE_GATE RELAY READY' not in bounded_tail(run_dir / 'relay.log'):
                if relay.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('relay did not start')
                time.sleep(0.1)
        games_by_slot = {}
        for slot in order:
            case, profile, dlc_binds, lock, ids = by_slot[slot]
            if display:
                display_name, auth = display
            else:
                display_number = next(n for n in range(101, 140)
                                      if not Path(f'/tmp/.X{n}-lock').exists() and not Path(f'/tmp/.X11-unix/X{n}').exists())
                display_name = f':{display_number}'
                auth = case / 'xauthority'
                auth.touch(mode=0o600)
                if os.geteuid() == 0:
                    os.chown(auth, user.pw_uid, user.pw_gid)
                subprocess.run(user_command(['xauth', '-f', str(auth), 'add', display_name, '.', secrets.token_hex(16)], args.user),
                               check=True, stdout=subprocess.DEVNULL)
                xvfb = launch(f'xvfb{slot}', ['Xvfb', display_name, '-screen', '0', '800x600x24', '-nolisten', 'tcp', '-noreset', '-auth', str(auth)],
                              as_user=True)
                deadline = time.monotonic() + 10
                while not Path(f'/tmp/.X11-unix/X{display_number}').exists():
                    if xvfb.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError('private Xvfb failed')
                    time.sleep(0.1)
            env = {'PATH': '/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin', 'HOME': str(case / 'home'),
                   'USER': args.user, 'LOGNAME': args.user, 'DISPLAY': display_name, 'XAUTHORITY': str(auth),
                   'XDG_CONFIG_HOME': str(case / 'home/.config'), 'XDG_CACHE_HOME': str(case / 'cache'),
                   'XDG_DATA_HOME': str(case / 'data'), 'XDG_RUNTIME_DIR': str(case / 'runtime'), 'TMPDIR': str(case / 'tmp'),
                   'SteamAppId': '392160', 'SteamGameId': '392160', 'LD_LIBRARY_PATH': str(case / 'game/lib'),
                   'VK_ICD_FILENAMES': str(vulkan_driver), 'VK_DRIVER_FILES': str(vulkan_driver),
                   'SDL_AUDIODRIVER': 'dummy', 'ALSOFT_DRIVERS': 'null'}
            if discard:
                env.update(VK_LAYER_PATH=str(layer), VK_INSTANCE_LAYERS='VK_LAYER_X4MP_discard', X4MP_VK_DISCARD='1')
            if manifest.get('portable_math_sha256'):
                env.update(portable_math_env(case))
            program = [str(case / 'game/X4'), '-personalfolderid', '45841', '-skipintro', '-debug', 'scripts',
                       '-logfile', 'engine-output.log', '-nocputhrottle', '-noworkshopsync', f'-seed:{world_seed}']
            games_by_slot[slot] = (launch(f'game{slot}', namespace_command(case, game, dlc_binds, lock, env, program, args.user, shm)), profile)
        games = [games_by_slot[slot] for slot in local]
        result['started'] = time.time()
        deadline = time.monotonic() + args.timeout
        watches = [LogWatch(profile / 'engine-output.log') for _, profile in games]
        stopped_at = {}
        while True:
            if cancelled:
                raise RuntimeError('cancelled')
            if time.monotonic() > deadline:
                raise RuntimeError('timeout (loading or relay); see the client logs')
            if steam_identity(steam_home, user.pw_uid) != steam:
                raise RuntimeError('Steam client identity changed')
            for slot, watch, (proc, _) in zip(local, watches, games):
                watch.poll()
                if proc.poll() is not None and not watch.stopped and not watch.failure:
                    raise RuntimeError(f'client {slot}: X4 exited before its STOP line')
                if watch.stopped or watch.failure:
                    stopped_at.setdefault(slot, time.monotonic())
            index = {slot: i for i, slot in enumerate(local)}
            error = stop_error({slot: watches[index[slot]].failure or watches[index[slot]].stop_reason for slot in stopped_at},
                               stopped_at, time.monotonic(), len(local))
            if error:
                raise RuntimeError(error)
            if all(w.stopped for w in watches) and external:
                break  # the relay belongs to the session's host side; its report stays there
            if all(w.stopped for w in watches):
                try:
                    verdict = json.loads((run_dir / 'relay.json').read_text()).get('verdict')
                except (OSError, ValueError):
                    verdict = None
                if relay.poll() is not None or (verdict and verdict != 'incomplete'):
                    break
                if time.monotonic() - max(stopped_at.values()) >= RELAY_VERDICT:
                    raise RuntimeError(f'both clients stopped, no relay verdict within {RELAY_VERDICT} s')
            time.sleep(1)
        result['armed'] = [w.armed for w in watches]
    except Exception as error:
        result['error'] = str(error)
    finally:
        for proc in reversed(procs):
            try:
                stop_owned_process(proc, starts[proc.pid])
            except (OSError, subprocess.TimeoutExpired) as error:
                result.setdefault('cleanup_errors', []).append(str(error))
        result['owned_processes_remaining'] = wait_owned_groups(starts)
        if result['owned_processes_remaining']:
            result.setdefault('cleanup_errors', []).append('owned process groups still have members')
        for log in logs:
            log.close()
        try:
            release_lock_target(lock_target, lock_fd, lock_stat)
        except (OSError, RuntimeError) as error:
            result.setdefault('cleanup_errors', []).append(str(error))
        result['steam_after'] = None
        try:
            result['steam_after'] = steam_identity(steam_home, user.pw_uid)
        except (OSError, ValueError, RuntimeError) as error:
            result['steam_after'] = {'error': str(error)}
        result['steam_unchanged'] = result['steam_after'] == steam
        # Do not signal processes identified only by executable path or an unverified PID.
        result['x4_left'] = [pid for pid, _ in x4_executables()]
    # Even failed postprocessing must leave an explicit result next to the raw logs.
    result['pass'] = False
    try:
        result.update(collect_run_evidence(run_dir, local, cases, ticks, manifest))
        result['pass'] = (
            not result.get('error')
            and not result.get('cleanup_errors')
            and result['steam_unchanged']
            and not result['x4_left']
            and result['evidence_ok']
            and (external or (result['relay'] and result['relay'].get('verdict') == 'pass'))
            and all(result.get('portable_math_active', [True]))
        )
    except Exception as error:
        result['postprocessing_error'] = str(error)
    (run_dir / 'run-result.json').write_text(json.dumps(result, indent=2, default=str) + '\n')
    print('LINUX_GATE_LIVE ' + json.dumps({k: result.get(k) for k in ('name', 'pass', 'error', 'postprocessing_error', 'stops', 'world_identity', 'world_capture_problems', 'portable_math_active', 'world_hash_ticks',
                                                                    'world_hash_first_difference', 'evidence_errors', 'steam_unchanged', 'x4_left')}))
    print(f'results: {run_dir}')
    raise SystemExit(0 if result['pass'] else 1)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    b = sub.add_parser('bundle', help='build the gate, overlay and extension; collect the owned snapshot')
    b.add_argument('--zig', type=Path, required=True)
    b.add_argument('--snapshot', type=Path, required=True, help='owned native snapshot save_009.xml.gz (the census snapshot)')
    b.add_argument('--out', type=Path, required=True)
    b.add_argument('--preset', choices=('linux-core',),
                   help='opt-in Linux simulation test configuration; omit for manual hook experiments')
    b.add_argument('--world-log-mode', choices=('full', 'hashes'), default='full',
                   help='hashes suppresses object detail logs while keeping every full world capture')
    b.add_argument('--world-rolling-slices', type=int, metavar='N',
                   help='experimental per-tick object slices; requires hashes-only logging, changes census coverage timing')
    b.add_argument('--economy-watch', metavar='SAVED_ID', help='read-only station economy deadline evidence')
    b.add_argument('--economy-watch-window', type=int, nargs=2, metavar=('FIRST', 'LAST'),
                   help='inclusive economy watch window, at most 36000 ticks')
    b.add_argument('--navmesh-post-observe', type=int, nargs=2, metavar=('FIRST', 'LAST'),
                   help='observe Linux navmesh post attempts during startup and this inclusive live tick window')
    b.add_argument('--world-motion-watch', nargs='+', metavar='SAVED_ID',
                   help='default-off per-tick canonical Nav evidence for up to four imported saved IDs')
    b.add_argument('--world-motion-window', type=int, nargs=2, metavar=('FIRST', 'LAST'),
                   help='explicit inclusive motion evidence window, at most 18000 ticks')
    b.add_argument('--status-stride', type=int, metavar='N',
                   help='thin diagnostic status logs every N ticks (1..3600)')
    b.add_argument('--probe-profile', action='store_true',
                   help='record probe timings for scan/output performance comparisons')
    b.add_argument('--extension-base', type=Path, help='explicit historical extension base (default: current minimal lockstep base)')
    b.add_argument('--portable-math', action='store_true', help='preload libx4mp_portable_math.so with its decimal and exact_reciprocal patches')
    b.add_argument('--seed-rng', action='store_true', help='key the MoveWorker RNG word each frame (boot option 2)')
    b.add_argument('--wire-crypto', action='store_true',
                   help='wire crypto (reports/LOCKSTEP-WIRE-CRYPTO-20261101.txt): the config carries native_gate_wire_crypto, each '
                        "slot's credential filled in at run time (run --wire-crypto-credentials; needs a relay with --wire-crypto)")
    b.add_argument('--serial-pathing', action='store_true', help='join the pathing pass before the frame goes on (boot option 1)')
    b.add_argument('--md-resume-rng', action='store_true', help='load-time MD cue delays on per-cue streams')
    b.add_argument('--runtime-ids', action='store_true', help='the same runtime IDs on every client (set mode)')
    b.add_argument('--runtime-id-callers', action='store_true', help='the table of who allocates runtime IDs (needs --runtime-ids)')
    b.add_argument('--runtime-ids-local', action='store_true', help='what only the view asks for takes runtime IDs from its own lists (needs --runtime-ids and --created-identities)')
    b.add_argument('--script-rng', choices=('observe', 'key'), help='AI scripts and MD cues on per-script streams')
    b.add_argument('--script-rng-mask', type=int, choices=range(1, 8), metavar='1..7', help='1 AI scripts, 2 MD cues, 4 also boot phase 2 (default 3)')
    b.add_argument('--script-rng-rows', type=int, metavar='TICKS', help='SCRIPT_RNG_ROW lines for the first TICKS ticks')
    b.add_argument('--script-rng-rows-window', type=int, nargs=2, metavar=('FIRST', 'LAST'), help='SCRIPT_RNG_ROW lines of ticks FIRST..LAST only')
    b.add_argument('--script-rng-trace', action='store_true', help='script RNG rows of nested scopes too, with PCs and range bounds')
    b.add_argument('--script-rng-natural', action='store_true', help='rows of outermost unkeyed scopes too')
    b.add_argument('--script-rng-unmapped', action='store_true', help='AI owners without a portable ID are keyed by their rank')
    b.add_argument('--md-timed-rng', action='store_true', help='direct MD cue checks on the per-cue streams')
    b.add_argument('--find-order', choices=('observe', 'sort'), help='find_* candidate lists on saved IDs')
    b.add_argument('--loadout-order', choices=('observe', 'canonical'), help="what a station wants to restock does not depend on the macros' heap addresses")
    b.add_argument('--step-trace', nargs='+', metavar='SAVED_ID', help="the Linear flight model's step, entry and exit, for up to four saved IDs (diagnostic)")
    b.add_argument('--step-trace-ticks', type=int, default=3, help='ticks 1..N of the step trace (default 3); N above 600: the 60 ticks ending at N')
    b.add_argument('--step-chain', action='store_true', help="FR8's step chain rows with the step trace (native_gate_step_chain)")
    b.add_argument('--fp-redirect', choices=('shadow', 'writer', 'redirect'), help='the flight parameter refresh recomputed by strict code next to the native one (Option B slice 1; writer and redirect write the writer\'s +0x0..+0xc; the spline length hook is Windows-only)')
    b.add_argument('--fp-redirect-watch', nargs='+', metavar='SAVED_ID', help='FP_REDIRECT_ROWs for up to four saved IDs')
    b.add_argument('--fp-redirect-ticks', type=int, default=3, help='ticks 1..N of the FP_REDIRECT_ROWs (default 3)')
    b.add_argument('--fp-redirect-solver', choices=('shadow', 'redirect'), help='FR27: the GetSafePos solver on one portable implementation (needs --fp-redirect)')
    b.add_argument('--fp-redirect-drones', choices=('shadow', 'redirect'), help='FR30: the drone factors of get_attackstrength in Windows\' order (needs --fp-redirect)')
    b.add_argument('--coverage', type=int, metavar='TICKS', help='which listed functions run in ticks 1..TICKS (diagnostic)')
    b.add_argument('--coverage-list', type=Path, help='the Linux function list for --coverage (fp_inventory.py --coverage-lists)')
    b.add_argument('--coverage-from', type=int, metavar='TICK', help='--coverage marks the list at tick TICK instead of tick 1')
    b.add_argument('--mixed-platform', action='store_true',
                   help="a mixed Windows/Linux session's settings, tools/build_play_overlay.py MIXED_PLATFORM_SETS (fp_redirect, its "
                        "ticks, solver and drones), imported so both sides match")
    b.add_argument('--tick-hz', type=int, choices=(60, 40, 30, 20),
                   help="the clients' native_gate_tick_hz and the relay's --tick-hz (default 60; the session defaults and W35 use 30)")
    b.add_argument('--attention-all', action='store_true', help="every portable object's attention timer keyed by (tick, ID), its first due at the first admitted call")
    b.add_argument('--loadout-order-rows', type=int, nargs=2, metavar=('FROM', 'TO'), help='LOADOUT_ROWs in this tick window (at most 600 ticks)')
    b.add_argument('--path-transform-observe', type=int, nargs=2, metavar=('FIRST', 'LAST'),
                   help='compare ordered batch native and live path transforms (experimental)')
    b.add_argument('--nav-cadence', choices=('far', 'observe'), help='nav refresh intervals no longer depend on the camera distance')
    b.add_argument('--nav-params-init', action='store_true', help='parameter deadlines reset at the first call')
    b.add_argument('--nav-key', action='store_true', help="the nav refresh's draws keyed per ship (tick, portable ID, ordinal, time)")
    b.add_argument('--nav-probe-init', action='store_true', help='obstacle-probe deadlines reset at the first call')
    b.add_argument('--created-identities', action='store_true', help='portable IDs and census rows for components made after the load')
    b.add_argument('--created-rows', action='store_true', help='retain individual creation records for diagnosis (requires --created-identities)')
    b.add_argument('--created-load', action='store_true', help='also for those the load makes after the import')
    b.add_argument('--portable-created', action='store_true', help='ordering and keying modules use created IDs')
    b.add_argument('--gravidar-wide', choices=('sort',), help='the Gravidar\'s zone groups in portable order for the pass (needs --gravidar)')
    b.add_argument('--spawn-rng', choices=('observe', 'key'), help='ship loadouts on the generator\'s explicit state')
    b.add_argument('--weapon-fire-rng', action='store_true', help='every weapon shot on its own keyed stream (native_gate_weapon_fire_rng)')
    b.add_argument('--engine-refresh-rng', action='store_true', help="a ship's next engine-parameter refresh from a keyed stream (native_gate_engine_refresh_rng)")
    b.add_argument('--actor-rng-all', action='store_true', help="every portable ship's Move and Agent update on keyed streams (native_gate_actor_rng_all)")
    b.add_argument('--player-timer', action='store_true', help="the player's fleet rebuild timer from the first admitted frame (native_gate_player_timer)")
    b.add_argument('--warp-rng', choices=('split', 'observe'), help="warps on a keyed word or counted natively; with --step-trace the listed ships' warp watch rows (native_gate_warp_rng)")
    b.add_argument('--tempzone-order', action='store_true',
                   help='order tempzone requests after snapshot-tracked or inline pathing batches finish')
    b.add_argument('--warp-exit', action='store_true', help='the gate exit sum in the Windows order (native_gate_warp_exit)')
    b.add_argument('--body1-mass', choices=('fallback',), help="a ship's second body gets the empty compound's mass properties at creation (native_gate_body1_mass)")
    b.add_argument('--body-step-diag', action='store_true', help='BODY_STEP rows: the fixture bodies before and after the Jolt step (native_gate_body_step_diag)')
    b.add_argument('--nav-diag', action='store_true', help="NAV rows: the battle fixture's navigation upkeep, Windows' nav module (native_gate_nav_diag)")
    b.add_argument('--nav-rng', action='store_true', help="the fixture's upkeep draws from actor_seed('NAV_UPDT', ...), as Windows (native_gate_nav_rng; NAV rows too)")
    b.add_argument('--nav-initial-refresh', action='store_true', help="the fixture's parameter deadline at time - dt at its first upkeep, as Windows (native_gate_nav_initial_refresh)")
    b.add_argument('--attention-rng', action='store_true', help="the fixture's attention timer draws from actor_seed('ATTENTIO', ...), as Windows (native_gate_attention_rng; ATTENTION rows too)")
    b.add_argument('--attention-initial-refresh', action='store_true', help="the fixture's attention due at time - dt at its first call, as Windows (native_gate_attention_initial_refresh)")
    b.add_argument('--radar-rng', action='store_true', help="the fixture's radar units on actor_seed('RADARUNI', ...), as Windows (native_gate_radar_rng; needs --gravidar)")
    b.add_argument('--radar-initial-refresh', action='store_true', help="the fixture's radar unit times re-based at the prepare, as Windows (native_gate_radar_initial_refresh)")
    b.add_argument('--shape-rng', action='store_true', help="the fixture's shape refresh on actor_seed('SHAPE_UP', ...), as Windows (native_gate_shape_rng; SHAPE rows too)")
    b.add_argument('--shape-initial-refresh', action='store_true', help="the fixture's shape due times at time - dt at the first call per holder, as Windows (native_gate_shape_initial_refresh)")
    b.add_argument('--shape-refresh', choices=('view',), help="shape refresh intervals from the player's ship on a keyed draw, due times aligned at the first call per object (native_gate_shape_refresh)")
    b.add_argument('--ik-wrap', action='store_true', help="a skeleton joint's wrapped target as (t2 + value) - pi, the Windows order (native_gate_ik_wrap)")
    b.add_argument('--steer-roll', action='store_true', help="the steering roll alignment as Windows computes it (native_gate_steer_roll)")
    b.add_argument('--ik-step', action='store_true', help="the joints' fixed-step divisions by 0.02 as Windows' multiplications by 50 (native_gate_ik_step)")
    b.add_argument('--ik-diag', action='store_true', help='IKJOINT rows: the fixture ships\' and weapons\' skeleton joints (native_gate_ik_diag)')
    b.add_argument('--impulse-diag', action='store_true', help='IMPULSE rows: the impulses on fixture bodies (native_gate_impulse_diag)')
    b.add_argument('--velocity-diag', action='store_true', help="VELOCITY rows: the fixture controllers' SetVelocity calls, as Windows (native_gate_velocity_diag)")
    b.add_argument('--flight-limits', action='store_true', help="the engine parameters' limit words with Windows' hoisted reciprocal (native_gate_flight_limits)")
    b.add_argument('--world-hash-shields', action='store_true', help="census shields in canonical form (native_gate_world_hash_shields = "
                   "'canonical', as the Windows default stack hashes them)")
    b.add_argument('--mining-camera', action='store_true', help="the region object search's camera test from the player's ship "
                   "(native_gate_mining_camera = 'view')")
    b.add_argument('--battle', action='store_true', help="the battle fixture: native_gate_hash = 'battle' with the snapshot's "
                   'twelve (--mode battle --snapshot-prebind; needs --serial-pathing, --path-determinism 1 or 3 and world identities)')
    for name, why in (('actor-diag', "the fixture actors' raw native state row (gate.actor)"),
                      ('strict-state', 'the raw hull, shield and navigation banks in the digest (needs --actor-diag)'),
                      ('shield-refresh', 'GetShieldPercent at the game time before the shield is read (needs --strict-state)'),
                      ('order-state', "the fixture's order queues in the digest"), ('fleet-state', 'their fleet membership in the digest'),
                      ('flight-rng', "each fixture steering update on its keyed word (gate_flight.h)"),
                      ('flight-full', 'full steering updates for the fixture (no cheap updates)'),
                      ('avoidance-refresh', "the fixture's avoidance geometry rebuilt every steering update (needs --flight-full)"),
                      ('steering-init', "the steering constructor's two angles keyed per fixture model (needs --flight-rng)"),
                      ('agent-full', "full pathing-Agent updates for the fixture (native_gate_agent_full)"),
                      ('spline-diag', "the spline hook's fixture path and spline state (gate_spline_join.h)"),
                      ('spline-refresh', "the fixture's spline obstacles refreshed, full updates (needs --spline-diag, --agent-full)"),
                      ('spline-state', "the fixture's spline state in the digest (needs --spline-diag)"),
                      ('params-init', "each fixture ship's flight parameters built once at its first sample (native_gate_params_init)"),
                      ('strict-physics', "the fixture ships' two physics bodies in the digest, Windows' STATE_ROW (needs --strict-state)"),
                      ('body-diag', "BODY rows: the fixture ships' raw physics body state each traced tick (diagnostic)"),
                      ('params-diag', "PARAMS rows: every flight-parameter builder call on a fixture ship (diagnostic)")):
        b.add_argument('--' + name, action='store_true', help=why)
    b.add_argument('--actor-rng-rows', action='store_true', help='one ACTOR_RNG_ROW per keyed scope that drew (diagnostic)')
    b.add_argument('--mass-traffic', choices=('sync', 'observe'), help='the mass traffic pass joined at the gate tick (native_gate_mass_traffic)')
    b.add_argument('--region-pass', choices=('sync', 'observe'), help='the region manager pass joined at the gate tick (native_gate_region_pass)')
    b.add_argument('--pass-stride', type=int, help='the synced passes only every Nth frame (native_gate_pass_stride)')
    b.add_argument('--spline-join-all', action='store_true', help='every spline update waits until its route request is published (native_gate_spline_join_all; needs serial pathing)')
    travel = b.add_mutually_exclusive_group()
    travel.add_argument('--travel-snapshot', action='store_true',
                        help='wingmen read a per-pass snapshot of the leader travel flag')
    travel.add_argument('--travel-determinism', action='store_true',
                        help='experimental: execute pathing batches in order; replaces the preset snapshot strategy')
    b.add_argument('--gravidar-deadlines', action='store_true', help='factor-pass deadlines rebased at the first gated Move, low-attention passes keyed')
    b.add_argument('--gravidar', action='store_true', help='the gated Gravidar pass, keyed units, time sync, sector order (implies serial pathing)')
    b.add_argument('--event-rows', type=int, metavar='TICKS', help='one EVENT_ROW per due event in the boot passes and the first TICKS gated ticks (up to 20000; about 50 MB per client for 6000 ticks)')
    b.add_argument('--event-order', action='store_true', help='due events delivered in (time, saved ID) source order')
    b.add_argument('--economy-load-rng', action='store_true', help='key economy draws during snapshot loading')
    b.add_argument('--build-rng', action='store_true', help='key build-storage update scheduling by identity and game time')
    b.add_argument('--event-rng', choices=('observe', 'key'), help='scope RNG around event deliveries (needs --event-order)')
    b.add_argument('--event-rng-rows-window', type=int, nargs=2, metavar=('FIRST', 'LAST'), help='record all event deliveries in this tick window')
    b.add_argument('--boot-hold', action='store_true', help='the first AIDirector::Move after the load runs in tick 1')
    b.add_argument('--ai-wait-rng', action='store_true', help='load-time AI Wait durations on per-wait streams')
    b.add_argument('--region-rng', choices=('key', 'observe'), help='hazard region updates on per-region streams')
    b.add_argument('--path-determinism', type=int, choices=(1, 2, 3), help='nav refresh stamped per frame (bit 1, implies --serial-pathing) and no 1 ms pathing budget (bit 2)')
    b.add_argument('--world-hash-watch', nargs='*', metavar='SAVED_ID', help='log the hashed bytes of these saved IDs (16 hex digits, up to 64)')
    b.add_argument('--fork-save', metavar='TICK[:NAME]', help='non-blocking save by fork (D14): at TICK the client forks and its child writes '
                   'save/mpjoin/NAME.xml.gz in its profile while the session goes on (reports/LOCKSTEP-NONBLOCKING-SAVE-20261101.txt)')
    b.add_argument('--fork-save-every', type=int, help='repeat every N ticks under the same name (repeated-fork runs)')
    b.add_argument('--fork-save-slot', type=int, help='only this slot forks (default every client)')
    b.add_argument('--fork-save-budget-ms', type=int, default=50, help='how long the fork tick may wait for the quiet point')
    b.add_argument('--fork-save-watchdog', type=int, default=600, help="seconds before the child's own alarm ends it")
    b.add_argument('--fp-attack-capture', type=int, nargs=2, metavar=('FIRST', 'LAST'), help='FR29b: FP_ATTACK_ROWs of the attack computation in ticks FIRST..LAST (needs --fp-redirect)')
    b.add_argument('--fp-attack-watch', nargs='*', metavar='SAVED_ID', help='FR29b: only calls on these saved IDs (16 hex digits, up to 32)')
    b.add_argument('--world-health-ticks', type=int, nargs=2, metavar=('FIRST', 'LAST'), help='FR28: WORLD_HEALTH rows (stored hull and shield) of the watches on every tick FIRST..LAST (needs --world-hash-watch)')
    b.add_argument('--clients', type=int, default=2, choices=range(2, 9), metavar='2..8', help='instances on the host; above 2 they use the protocol 3 wire')
    b.add_argument('--ticks', type=int, default=600)
    b.add_argument('--stride', type=int, default=60, help='world hash capture every N ticks (and tick 1)')
    r = sub.add_parser('run', help='run on Linux as the Steam user, or select one as root')
    r.add_argument('--root', type=Path, required=True)
    r.add_argument('--user', default='x4server')
    r.add_argument('--display', help='use this existing local X11 display instead of private Xvfb instances, e.g. :0')
    r.add_argument('--vulkan-icd', type=Path, help='Vulkan driver JSON; default: detected 64-bit Mesa lavapipe ICD')
    r.add_argument('--ticks', type=int)
    r.add_argument('--name')
    r.add_argument('--timeout', type=int, default=3600, help='seconds for load and the whole run')
    r.add_argument('--min-memory-gib', type=float, help='default 6.5 per instance')
    r.add_argument('--no-discard', action='store_true', help='render with lavapipe instead of the discard layer')
    r.add_argument('--discard-layer', type=Path, help='the X4MP discard layer directory (default /opt/x4mp/vklayer)')
    r.add_argument('--game', type=Path, help='the X4 installation (default ~USER/x4/game, the VPS layout), e.g. a Steam library\'s common/X4 Foundations')
    r.add_argument('--steam-home', type=Path, help='the directory whose .steam belongs to the running Steam client (default ~USER; snap Steam: ~USER/snap/steam/common)')
    r.add_argument('--shm', type=Path, help="bind this directory over /dev/shm for X4 (a confined Steam client's shared memory, e.g. /dev/shm/snap.steam)")
    r.add_argument('--preflight', action='store_true', help='only check the host requirements and the bundle; start nothing')
    r.add_argument('--relay-port', type=int, help='mixed session: use the relay already listening on 127.0.0.1:PORT of this host '
                   '(e.g. one serving Windows clients too) instead of starting one; needs --relay-token and --local-slots')
    r.add_argument('--relay-token', help='the token of that relay (32 hex)')
    r.add_argument('--local-slots', metavar='2', help='mixed session: the slots this host runs (the others join from elsewhere)')
    r.add_argument('--wire-crypto-credentials', type=Path, metavar='FILE',
                   help='a bundle built with --wire-crypto: JSON {"<slot>": "x4c1:...", ...} (native_gate_crypto.py credential for '
                        'this --relay-token), mode 600; with an external relay only (--relay-port)')
    r.add_argument('--launch-order', metavar='2,1', help='start clients in this order for comparison; repeated differences still need diagnosis and do not alone prove slot-specific behaviour')
    args = p.parse_args(argv)
    if args.command == 'bundle':
        apply_bundle_preset(args)
        bundle(args)
    else:
        run(args)


if __name__ == '__main__':
    main()

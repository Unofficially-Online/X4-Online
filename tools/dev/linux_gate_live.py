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
import ipaddress
import json
import math
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import socket
import stat
import subprocess
import sys
import time

import linux_view_ancestry_bundle as ancestry_bundle
import linux_ui_camera_refill_bundle as camera_refill_bundle
import linux_stock_view_fov_observer as stock_view_observer
import linux_player_cursor_bundle as cursor_bundle
import linux_view_component_trace_bundle as component_trace_bundle
import linux_player_space_bundle as space_bundle
import linux_view_render_capture_bundle as guest_capture_bundle
import linux_view_host_capture_bundle as input_observation_bundle
import linux_view_projection_scope_bundle as projection_scope_bundle
import linux_selection_parity_bundle as selection_parity_bundle
import linux_player_view_wire_bundle as view_wire_bundle
import linux_view_performance_bundle as view_performance_bundle
import linux_semaphore_observer_bundle as semaphore_bundle
import linux_player_fire_observation as fire_observation
import linux_cursor_input_trace as mouse_trace
import linux_view_precompare_bundle as precompare_trace
import linux_shared_identity_bundle as shared_identity_bundle
from linux_gate_evidence import is_fatal_gate_message, validate_evidence
from linux_player_physics_evidence import collect_physics_evidence
try:
    from linux_player_frames_evidence import collect_recurring_physics_evidence
except ModuleNotFoundError as error:
    if error.name != 'linux_player_frames_evidence':
        raise
    collect_recurring_physics_evidence = None

try:
    from linux_player_network_evidence import collect_network_physics_evidence, compare_throttle_peers
except ModuleNotFoundError as error:
    if error.name != 'linux_player_network_evidence':
        raise
    collect_network_physics_evidence = None
    compare_throttle_peers = None

try:
    from linux_player_camera_evidence import collect_camera_evidence
except ModuleNotFoundError as error:
    if error.name != 'linux_player_camera_evidence':
        raise
    collect_camera_evidence = None

from linux_ui_session_budget import (
    budget_from_options, budget_from_overlay, expected_callbacks, resolve_session_options,
)

from linux_player_startup_phases import phases_from_options, startup_phases

EXE_SHA = '113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20'
MARKER = '.x4mp-linux-gate-live'
SLOT_SAVE = 'save_009.xml.gz'
PORTABLE_MATH = 'libx4mp_portable_math.so'
PLAYER_LIFETIME = 'libx4mp_player_lifetime.so'
PLAYER_VIEW_QUERY = 'libx4mp_player_view_query.so'
VIEW_QUERY_SOURCES = (
    'src/native/engine/linux/player_ui_session_budget.h',
    'src/native/engine/linux/player_startup_phases.h',
    'src/native/engine/linux/player_view_query_preload.c',
    'src/native/engine/linux/player_view_query_preload.h',
    'src/native/engine/linux/player_live_pose_preload.h',
    'src/native/engine/linux/player_live_pose_preload_impl.h',
    'src/native/engine/linux/player_live_pose_preload_pin.h',
    'src/native/engine/linux/player_ui_live_correction.h',
    'src/native/engine/linux/player_ui_seat_transform.h',
    'src/native/engine/linux/player_ui_escape_guard.cpp',
    'src/native/engine/linux/player_ui_escape_guard.h',
    'src/native/engine/linux/player_view_query_preload_pin.h',
    'src/native/engine/linux/gate_player_view_query.h',
    'src/native/engine/lockstep_sha256.h',
    'tools/dev/build_player_view_query_preload.py',
)
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


def mixed_platform_literal(text):
    """Decode the shared preset's limited Lua literals; never evaluate code."""
    value = text.strip()
    if value in ('true', 'false'):
        return value == 'true'
    if re.fullmatch(r'-?(?:0|[1-9][0-9]*)', value):
        return int(value)
    if re.fullmatch(r"'[^'\\\x00-\x1f\x7f]*'", value):
        return value[1:-1]
    raise SystemExit(f'--mixed-platform: unsupported preset literal {text!r}')


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
        options[key[len('native_gate_'):]] = mixed_platform_literal(value)
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


def validate_factory_retention(retain_until, factory_probe, ticks):
    """Require the owned P2 reference to be released before session STOP."""
    if retain_until is None:
        return
    if not factory_probe:
        raise SystemExit('--player-factory-retain-until requires --player-factory-probe')
    if type(retain_until) is not int or not 4 <= retain_until <= 36000 or retain_until >= ticks:
        raise SystemExit('--player-factory-retain-until must be 4..36000 and less than --ticks')


def validate_seat_preflight(factory_probe, retain_until, observer, serial_pathing, ticks):
    """Keep the diagnostic player owned through the tick-four inspection."""
    if not (factory_probe and observer and serial_pathing):
        raise SystemExit('--player-seat-preflight requires the factory probe, observer and serial pathing')
    if type(retain_until) is not int or not 5 <= retain_until <= 36000 or retain_until >= ticks:
        raise SystemExit('--player-seat-preflight requires a factory release tick in 5..36000 before STOP')


def validate_control_bank_probe(observer, serial_pathing, ticks, replay=False, reference_probe=False):
    """Keep the tick-five canonical bank roundtrip inside its isolated session."""
    if not observer or not serial_pathing or type(ticks) is not int or ticks < 6:
        raise SystemExit('--player-control-bank-probe requires observer, serial pathing and at least six ticks')
    if replay or reference_probe:
        raise SystemExit('run the control-bank probe separately from keyboard replay and reference probes')


def validate_control_boundary_probe(observer, serial_pathing, ticks, conflicts):
    """Measure one main/worker rendezvous without exchanging player state."""
    if not observer or not serial_pathing or type(ticks) is not int or ticks < 6:
        raise SystemExit('--player-control-boundary-probe requires observer, serial pathing and at least six ticks')
    if any(conflicts):
        raise SystemExit('run the control-boundary probe separately from player mutation experiments')


def validate_independent_keyboard(options, ticks):
    frames = options.get('independent_keyboard_frames', 0)
    session = budget_from_options(options)
    maximum = session['frames'] if session else 600
    if session and frames != session['frames']:
        raise SystemExit('session and independent frame budgets differ')
    if type(frames) is not int or (frames != 0 and not 2 <= frames <= maximum):
        raise SystemExit('--independent-keyboard-frames must be 0 or 2..600')
    if not frames:
        return 0
    required = ('stock_keyboard_replay_probe', 'player_factory_probe',
                'stock_keyboard_observer', 'serial_pathing')
    if (not all(options.get(key) for key in required) or
            not (options.get('player_lifetime_preload') or options.get('player_lifetime_sha256')) or
            options.get('clients', 2) != 2 or
            ticks <= (session['recurring_end'] if session else 4 + frames)):
        raise SystemExit('independent keyboard frames require replay, factory, observer, serial pathing, lifetime preload, two clients and ticks > 4+frames')
    if any(options.get(key) for key in ('player_control_boundary_probe', 'player_control_bank_probe',
                                      'player_control_bank_census', 'player_reference_probe', 'player_seat_preflight')):
        raise SystemExit('independent keyboard frames conflict with other player experiments')
    return frames


def validate_lifetime_receipts(receipts):
    return receipts == ['X4 PLAYER LIFETIME PRELOAD ACTIVE version=5']


def lifetime_preload_sources(repo):
    """Sources required to reproduce and inspect the frozen lifetime preload."""
    engine = repo / 'src/native/engine/linux'
    sources = [engine / name for name in (
        'player_lifetime_preload.c', 'player_lifetime_preload_emit.h',
        'player_lifetime_preload_pin.h', 'player_lifetime_preload_wrappers.h',
    )]
    sources += [repo / 'src/native/engine/lockstep_sha256.h',
                repo / 'tools/dev/build_player_lifetime_preload.py']
    if not all(source.is_file() for source in sources):
        raise SystemExit('lifetime preload requires all frozen source files')
    return sources


def validate_view_query_observer(options, frames):
    enabled = options.get('player_view_query_observer', False)
    if type(enabled) is not bool:
        raise SystemExit('player_view_query_observer must be a boolean')
    if enabled and (frames < 4 or not (options.get('player_lifetime_preload') or
                                      options.get('player_lifetime_sha256'))):
        raise SystemExit('view query observer requires at least 4 independent frames and lifetime preload')
    return enabled


UI_OVERLAY_SOURCES = (
    'src/native/engine/linux/player_ui_session_budget.h',
    'src/native/engine/linux/player_startup_phases.h',
    'src/native/engine/linux/gate_player_ui_overlay_probe.h',
    'src/native/engine/linux/player_ui_published_pose.h',
    'src/native/engine/linux/player_ui_seat_transform.h',
    'src/native/engine/linux/player_ui_overlay_lifecycle.h',
    'src/native/engine/linux/player_ui_viewstate.h',
    'src/native/engine/linux/player_ui_seat_pose.h',
    'tools/dev/linux_ui_seat_pose_evidence.py',
    'src/native/engine/linux/player_ui_escape_guard.cpp',
    'src/native/engine/linux/player_ui_escape_guard.h',
    'tools/dev/build_player_view_query_preload.py',
    'src/native/engine/linux/player_presentation_overlay.h',
    'src/native/engine/linux/player_view_query_preload.c',
    'src/native/engine/linux/player_view_query_preload.h',
    'src/native/engine/linux/player_view_query_preload_pin.h',
    'src/native/engine/linux/gate_linux.c', 'tools/dev/native_gate_probe.lua',
)


PLAN_KICK_SOURCES = (
    'src/native/engine/linux/gate_player_ui_plan_kick.h',
    'src/native/engine/linux/player_ui_plan_kick.h',
    'tools/dev/linux_ui_plan_kick_evidence.py',
)


def validate_plan_kick(options):
    enabled = options.get('player_ui_plan_kick_probe', False)
    if type(enabled) is not bool:
        raise SystemExit('player_ui_plan_kick_probe must be a boolean')
    if enabled and (options.get('player_ui_overlay_probe') is not True or
                    type(options.get('player_ui_overlay_callbacks')) is not int or
                    options['player_ui_overlay_callbacks'] != expected_callbacks(options)):
        raise SystemExit('plan-kick requires explicit UI overlay160')
    return enabled


def plan_kick_sources(repo):
    sources = [repo / name for name in PLAN_KICK_SOURCES]
    if not all(path.is_file() for path in sources):
        raise SystemExit('plan-kick requires all frozen source files')
    return sources


def validate_plan_kick_config(config, enabled):
    values = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_linux_ui_plan_kick_probe\s*=\s*(.*?)\s*$', config)
    if values != (['true'] if enabled else []):
        raise SystemExit('plan-kick config does not match frozen option')


def plan_kick_environment(enabled, slot):
    return {'X4_DEV_UI_PLAN_KICK': '1'} if enabled and slot == 2 else {}


SEAT_QUERY_SOURCES = (
    'src/native/engine/linux/gate_player_ui_seat_query.h',
    'src/native/engine/linux/player_ui_seat_query.h',
    'tools/dev/linux_ui_seat_query_evidence.py',
)


def validate_seat_query(options):
    enabled = options.get('player_ui_seat_query_probe', False)
    if type(enabled) is not bool:
        raise SystemExit('player_ui_seat_query_probe must be a boolean')
    if enabled and (options.get('player_ui_overlay_probe') is not True or
                    type(options.get('player_ui_overlay_callbacks')) is not int or
                    options['player_ui_overlay_callbacks'] != expected_callbacks(options)):
        raise SystemExit('seat-query requires explicit UI overlay160')
    return enabled


def seat_query_sources(repo):
    sources = [repo / name for name in SEAT_QUERY_SOURCES]
    if not all(path.is_file() for path in sources):
        raise SystemExit('seat-query requires all frozen source files')
    return sources


def validate_seat_query_config(config, enabled):
    values = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_linux_ui_seat_query_probe\s*=\s*(.*?)\s*$', config)
    if values != (['true'] if enabled else []):
        raise SystemExit('seat-query config does not match frozen option')


def seat_query_environment(enabled, slot):
    return {'X4_DEV_UI_SEAT_QUERY': '1'} if enabled and slot == 2 else {}


LIVE_POSE_SOURCES = (
    'src/native/engine/linux/gate_player_live_pose_observer.h',
    'src/native/engine/linux/player_live_pose_preload.h',
    'src/native/engine/linux/player_live_pose_preload_impl.h',
    'src/native/engine/linux/player_live_pose_preload_pin.h',
    'tools/dev/linux_ui_live_pose_observer_evidence.py',
    'tools/dev/linux_ui_pose_scope_evidence.py',
)


def validate_live_pose_observer(options):
    enabled = options.get('player_ui_live_pose_observer', False)
    if type(enabled) is not bool:
        raise SystemExit('player_ui_live_pose_observer must be a boolean')
    if enabled and (options.get('player_ui_seat_query_probe') is not True or
                    options.get('player_ui_overlay_probe') is not True or
                    type(options.get('player_ui_overlay_callbacks')) is not int or
                    options['player_ui_overlay_callbacks'] != expected_callbacks(options)):
        raise SystemExit('live-pose observer requires explicit seat query and UI overlay160')
    return enabled


def validate_live_pose_evidence_contract(options):
    enabled = validate_live_pose_observer(options)
    version = options.get('player_ui_live_pose_observer_evidence_version')
    if (enabled and (type(version) is not int or version != 2)) or (not enabled and version is not None):
        raise SystemExit('live-pose evidence contract must be frozen as version 2 when enabled')
    return version


SEAT_CORRECTION_SOURCES = (
    'src/native/engine/linux/gate_player_ui_seat_correction.h',
    'src/native/engine/linux/gate_player_ui_overlay_probe.h',
    'src/native/engine/linux/gate_player_ui_seat_query.h',
    'src/native/engine/linux/player_ui_live_correction.h',
    'src/native/engine/linux/gate_player_live_pose_observer.h',
    'src/native/engine/linux/player_live_pose_preload.h',
    'src/native/engine/linux/player_live_pose_preload_impl.h',
    'src/native/engine/linux/player_live_pose_preload_pin.h',
    'src/native/engine/linux/player_ui_published_pose.h',
    'src/native/engine/linux/player_ui_seat_transform.h',
    'tools/dev/linux_ui_seat_correction_evidence.py',
)


def validate_seat_correction(options, *, frozen=False):
    enabled = options.get('player_ui_seat_correction', False)
    if type(enabled) is not bool:
        raise SystemExit('player_ui_seat_correction must be a boolean')
    if enabled and not validate_live_pose_observer(options):
        raise SystemExit('seat correction requires explicit live observer, seat query and UI overlay160')
    if frozen:
        version = options.get('player_ui_seat_correction_evidence_version')
        if (enabled and (type(version) is not int or version != 1)) or (not enabled and version is not None):
            raise SystemExit('seat correction requires frozen evidence version 1 when enabled')
    return enabled


def seat_correction_sources(repo):
    sources = [repo / name for name in SEAT_CORRECTION_SOURCES]
    if not all(path.is_file() for path in sources):
        raise SystemExit('seat correction requires all frozen source files')
    return sources


def validate_seat_correction_config(config, enabled):
    values = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_linux_ui_seat_correction\s*=\s*(.*?)\s*$', config)
    if values != (['true'] if enabled else []):
        raise SystemExit('seat correction config does not match frozen option')


def seat_correction_environment(enabled, slot):
    return {'X4_DEV_UI_SEAT_CORRECTION': '1'} if enabled and slot == 2 else {}


RENDER_LEASE_SOURCES = (
    'src/native/engine/linux/gate_player_ui_overlay_probe.h',
    'src/native/engine/linux/gate_player_ui_seat_correction.h',
    'src/native/engine/linux/gate_player_live_pose_observer.h',
    'src/native/engine/linux/player_live_pose_preload_impl.h',
    'src/native/engine/linux/player_view_query_preload.c',
    'src/native/engine/linux/player_view_query_preload.h',
    'src/native/engine/linux/player_view_query_preload_pin.h',
    'tools/dev/linux_ui_render_lease_evidence.py',
)


SESSION_SOURCES = (
    'src/native/engine/linux/gate_player_startup_phases.h',
    'src/native/engine/linux/player_startup_phases.h',
    'tools/dev/linux_player_startup_phases.py',
    'src/native/engine/linux/player_ui_session_budget.h',
    'src/native/engine/linux/gate_player_control_recurring.h',
    'src/native/engine/linux/gate_stock_keyboard_replay.h',
    'tools/dev/linux_ui_session_budget.py',
    'tools/dev/linux_ui_session_evidence.py',
)


def session_sources(repo):
    sources = [repo/name for name in SESSION_SOURCES]
    if not all(path.is_file() for path in sources):
        raise SystemExit('session mode requires all frozen budget sources')
    return sources


def enforce_session_disk_budget(run_dir, limit):
    used = sum(path.stat().st_size for path in run_dir.rglob('*')
               if path.is_file() and not path.is_symlink())
    if used > limit:
        raise RuntimeError('session disk budget exceeded; owned processes stopped')


def validate_session_config(config, frames, startup=None):
    assignments = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_linux_ui_session_frames\s*=\s*(.*?)\s*$', config)
    if assignments != ([str(frames)] if frames else []):
        raise SystemExit('session config does not match frozen frame budget')
    values = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_linux_startup_phases\s*=\s*(.*?)\s*$', config)
    if values != ([repr(startup)] if startup else []):
        raise SystemExit('startup phase config does not match its frozen option')


def render_lease_sources(repo):
    sources = [repo/name for name in RENDER_LEASE_SOURCES]
    if not all(path.is_file() for path in sources):
        raise SystemExit('render lease requires all frozen source files')
    return sources


def validate_render_lease(options, *, frozen=False):
    enabled = options.get('player_ui_render_lease', False)
    if type(enabled) is not bool:
        raise SystemExit('player_ui_render_lease must be a boolean')
    if enabled and not validate_seat_correction(options, frozen=frozen):
        raise SystemExit('render lease requires explicit seat correction and its full overlay160 chain')
    if frozen:
        version = options.get('player_ui_render_lease_evidence_version')
        if (enabled and (type(version) is not int or version != 1)) or (not enabled and version is not None):
            raise SystemExit('render lease requires frozen evidence version 1 when enabled')
    return enabled


def validate_render_lease_config(config, enabled):
    values = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_linux_ui_render_lease\s*=\s*(.*?)\s*$', config)
    if values != (['true'] if enabled else []):
        raise SystemExit('render lease config does not match frozen option')


def render_lease_environment(enabled, slot):
    return {'X4_DEV_UI_RENDER_LEASE': '1'} if enabled and slot == 2 else {}


def live_pose_observer_sources(repo):
    sources = [repo / name for name in LIVE_POSE_SOURCES]
    if not all(path.is_file() for path in sources):
        raise SystemExit('live-pose observer requires all frozen source files')
    return sources


def validate_live_pose_observer_config(config, enabled):
    values = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_linux_ui_live_pose_observer\s*=\s*(.*?)\s*$', config)
    if values != (['true'] if enabled else []):
        raise SystemExit('live-pose observer config does not match frozen option')


def live_pose_observer_environment(enabled, slot):
    return {'X4_DEV_UI_LIVE_POSE_OBSERVER': '1'} if enabled and slot == 2 else {}


def validate_ui_overlay(options, frames):
    validate_render_lease(options)
    validate_seat_correction(options)
    validate_plan_kick(options)
    validate_seat_query(options)
    validate_live_pose_observer(options)
    enabled = options.get('player_ui_overlay_probe', False)
    if type(enabled) is not bool:
        raise SystemExit('player_ui_overlay_probe must be a boolean')
    callbacks = options.get('player_ui_overlay_callbacks', 1)
    session = budget_from_options(options)
    allowed = (session['callbacks'],) if session else (1, 8, 160)
    if type(callbacks) is not int or callbacks not in allowed or (callbacks != 1 and not enabled):
        raise SystemExit('UI overlay callbacks must be 1, 8 or 160; multiple callbacks require the explicit probe')
    if callbacks != 1 and frames < callbacks + 5:
        raise SystemExit('UI overlay callback count requires independent frames >= count+5')
    required = ('player_presentation_fork_probe', 'player_presentation_probe',
                'player_view_query_observer', 'ui_callback_observer')
    if enabled and (frames < 6 or not all(options.get(key) is True for key in required) or
                    not (options.get('player_lifetime_preload') or options.get('player_lifetime_sha256'))):
        raise SystemExit('UI overlay requires independent >=6 frames, fork, presentation, view-query, callbacks and lifetime')
    return enabled


def ui_overlay_sources(repo):
    sources = [repo / name for name in UI_OVERLAY_SOURCES]
    if not all(source.is_file() for source in sources):
        raise SystemExit('UI overlay requires all frozen source files')
    return sources


def validate_ui_overlay_config(config, enabled, callbacks=1):
    values = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_linux_ui_overlay_probe\s*=\s*(.*?)\s*$', config)
    if values != (['true'] if enabled else []):
        raise SystemExit('UI overlay config does not match frozen option')
    counts = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_linux_ui_overlay_callbacks\s*=\s*(.*?)\s*$', config)
    if counts != ([str(callbacks)] if enabled and callbacks != 1 else []):
        raise SystemExit('UI overlay callback config does not match frozen option')


def ui_overlay_environment(enabled, slot, callbacks=1, session_frames=0, startup=None):
    if session_frames:
        from linux_ui_session_budget import session_budget
        budget = session_budget(session_frames, startup)
        if callbacks != budget['callbacks']:
            raise ValueError('session environment callback budget mismatch')
        environment = {'X4_DEV_UI_SESSION_FRAMES': str(budget['frames'])}
        if startup:
            environment['X4_DEV_PLAYER_PHASES'] = startup
        return environment
    key = {1: 'X4_DEV_UI_OVERLAY', 8: 'X4_DEV_UI_OVERLAY_EIGHT', 160: 'X4_DEV_UI_OVERLAY_160'}[callbacks]
    return {key: '1'} if enabled and slot == 2 else {}


def parse_ui_overlay_evidence(engine_lines, stderr_lines, trace_lines, slot, enabled, fork, callbacks=1, session_frames=0, startup=None):
    result = {'pass': False, 'errors': [], 'enabled': enabled and slot == 2,
              'readiness_proven': False, 'exclusion_proven': False, 'safe_resume_proven': False}
    errors = result['errors']
    allowed_callbacks = (1,8,160)
    phase = startup_phases(startup)
    first_tick = phase['presentation_first']
    if session_frames:
        from linux_ui_session_budget import session_budget
        try:
            allowed_callbacks = (session_budget(session_frames, startup)['callbacks'],)
            result['session_frames'] = session_frames
            result['startup_phases'] = startup
        except ValueError as error:
            errors.append(str(error))
    if (type(slot) is not int or slot not in (1, 2) or type(enabled) is not bool or
            type(callbacks) is not int or callbacks not in allowed_callbacks or (callbacks != 1 and not enabled)):
        errors.append('invalid UI overlay slot or activation boolean')
        return result
    markers = [(i, line) for i, line in enumerate(engine_lines) if 'UI_OVERLAY' in line]
    native = [line for line in stderr_lines if 'X4 UI OVERLAY' in line]
    result.update(raw_engine_receipts=[line for _, line in markers], raw_native_receipts=native)
    if not enabled or slot == 1:
        if markers or native:
            errors.append('unexpected disabled or client1 UI overlay evidence')
        result['pass'] = not errors
        return result
    if (slot != 2 or len(markers) != 2 or len(native) != (callbacks + 1 if callbacks != 1 else 1) or
            any(line.count('UI_OVERLAY') != 1 for _, line in markers) or
            any(len(line) > 2048 or not line.endswith('\n') for line in [*result['raw_engine_receipts'], *native])):
        errors.append('missing, duplicate, truncated or oversized UI overlay receipts')
        return result
    prefix = r'.*?\[X4LAN\] NATIVE_GATE '
    if not re.fullmatch(prefix + rf'LINUX_UI_OVERLAY_ARMED tick={phase["presentation_arm"]} slot=2\n', markers[0][1]):
        errors.append('invalid UI overlay arm')
    number, pointer = r'(0|[1-9][0-9]{0,19})', r'([1-9a-f][0-9a-f]{0,15})'
    measurements = ('updates', 'applies', 'restores', 'before', 'overlay', 'paused', 'resumed', 'restored')
    names = (*measurements, 'p1', 'p2', 'room', 'object')
    body = (' '.join(key+'='+number for key in measurements) + ' ' +
            ' '.join(key+'='+pointer for key in ('p1', 'p2', 'room', 'object')) + r' canonical_restored=1')
    if callbacks == 1:
        payload = r'v=2 status=pass tick=9 ' + body
        match = re.fullmatch(prefix + r'LINUX_UI_OVERLAY_RESULT tick=10 slot=2 ('+payload+r')\n', markers[1][1])
        if not match:
            errors.append('invalid UI overlay result grammar or restoration state')
            return result
        if native != ['X4 UI OVERLAY RESULT '+match[1]+'\n']:
            errors.append('native and engine UI overlay receipts differ')
        observations = [match.groups()[1:]]
    else:
        summary = f'v=3 status=pass first={first_tick} last={first_tick+callbacks-1} count={callbacks}'
        if (not re.fullmatch(prefix + f'LINUX_UI_OVERLAY_RESULT tick={first_tick+callbacks} slot=2 '+summary+r'\n', markers[1][1]) or
                native[-1] != 'X4 UI OVERLAY RESULT '+summary+'\n'):
            errors.append('invalid bounded UI overlay summary')
        observations = []
        for ordinal, line in enumerate(native[:-1], 1):
            frame = (f'X4 UI OVERLAY FRAME v=3 tick={first_tick+ordinal-1} ordinal={ordinal} count={callbacks} ' + body + r'\n')
            match = re.fullmatch(frame, line)
            if not match:
                errors.append('missing, reordered or malformed UI overlay frame')
            else:
                observations.append(match.groups())
    from linux_player_presentation_evidence import validate_presentation_rows
    presentation = validate_presentation_rows(trace_lines, slot, tick=phase['presentation_probe'])
    proof = fork.get('measurements', {})
    proof_valid = (fork.get('pass') is True and
                   all(key in proof for key in ('p1_ship', 'p2_ship', 'p1', 'p2', 'room')))
    if not proof_valid:
        errors.append('UI overlay lacks an accepted child presentation fork proof')
    measured_frames = []
    for values in observations:
        measured = {key: int(value, 16 if index >= 8 else 10)
                    for index, (key, value) in enumerate(zip(names, values))}
        measured_frames.append(measured)
        if any(value > 0xffffffffffffffff for value in measured.values()):
            errors.append('UI overlay field outside native uint64 range')
        if not (1 <= measured['updates'] <= 8 and
                measured['applies'] == measured['restores'] == measured['updates'] + 1):
            errors.append('UI overlay updates or apply/restore counters invalid')
        if proof_valid and (any(measured[key] != proof['p1_ship'] for key in ('before', 'paused', 'restored')) or
                any(measured[key] != proof['p2_ship'] for key in ('overlay', 'resumed')) or
                any(measured[key] != proof[key] for key in ('p1', 'p2', 'room'))):
            errors.append('UI overlay P1/P2 pause/resume/restore or player identity mismatch')
        if not presentation['pass'] or measured['object'] != presentation.get('ui_plan', {}).get('presenter', -9) + 8:
            errors.append('UI overlay object differs from authenticated presenter+8')
    if callbacks == 1 and measured_frames:
        result['measurements'] = measured_frames[0]
    else:
        result['frames'] = measured_frames
    fork_rows = fork.get('raw_engine_receipts', [])
    prior = [i for i, line in enumerate(engine_lines) if len(fork_rows) == 2 and line == fork_rows[1]]
    if len(prior) != 1 or not prior[0] < markers[0][0] < markers[1][0]:
        errors.append('child fork result, UI arm and result order invalid')
    fork_native = fork.get('raw_native_receipts', [])
    native_prior = [i for i, line in enumerate(stderr_lines)
                    if len(fork_native) == 1 and line == fork_native[0]]
    first_ui = next((i for i, line in enumerate(stderr_lines) if 'X4 UI OVERLAY' in line), -1)
    if len(native_prior) != 1 or not native_prior[0] < first_ui:
        errors.append('native child fork result must occur exactly once before UI evidence')
    result['callback_tick'] = first_tick
    result['callback_count'] = callbacks
    result['limitations'] = ['bounded native-guarded Move callbacks only; no persistent UI, reader exclusion or gameplay acceptance']
    result['pass'] = not errors
    return result


def collect_ui_overlay_evidence(profile, stderr, slot, enabled, fork, callbacks=1, session_frames=0, startup=None):
    paths = {'engine_sha256': profile / 'engine-output.log', 'stderr_sha256': stderr}
    if enabled and slot == 2:
        paths['native_fd_sha256'] = profile / 'controller-lifecycle.log'
    lines, hashes, errors = {}, {}, []
    for key, path in paths.items():
        try:
            lines[key] = path.read_text(errors='strict').splitlines(keepends=True)
            hashes[key] = sha(path)
        except (OSError, UnicodeError) as error:
            lines[key] = []
            errors.append('UI overlay file unavailable: ' + str(error))
    result = parse_ui_overlay_evidence(lines['engine_sha256'], lines['stderr_sha256'],
                                     lines.get('native_fd_sha256', []), slot, enabled, fork, callbacks, session_frames, startup)
    result.update(hashes)
    if errors:
        result['errors'].extend(errors)
        result['pass'] = False
    if enabled and slot == 2:
        for key, label in (('engine_sha256', 'engine'), ('native_fd_sha256', 'lifecycle'),
                           ('stderr_sha256', 'stderr')):
            if key not in hashes or fork.get(key) != hashes[key]:
                result['errors'].append(f'UI overlay and fork proofs refer to different or missing {label} bytes')
                result['pass'] = False
    return result


def validate_presentation_fork(options, frames):
    enabled = options.get('player_presentation_fork_probe', False)
    if type(enabled) is not bool:
        raise SystemExit('player_presentation_fork_probe must be a boolean')
    if enabled and (frames < 5 or options.get('player_presentation_probe') is not True or
                    not (options.get('player_lifetime_preload') or options.get('player_lifetime_sha256')) or
                    options.get('fork_save')):
        raise SystemExit('presentation fork requires independent >=5 frames, presentation and lifetime; excludes fork-save')
    return enabled


def presentation_fork_sources(repo):
    sources = [repo / 'src/native/engine/linux' / name for name in
               ('player_presentation_fork_probe.h', 'player_presentation_overlay.h',
                'gate_player_presentation_fork.h', 'gate_fork_save.h', 'gate_linux.c')]
    if not all(path.is_file() for path in sources):
        raise SystemExit('presentation fork requires all frozen native source files')
    return sources


def validate_presentation_fork_config(config, enabled):
    values = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_linux_presentation_fork_probe\s*=\s*(.*?)\s*$', config)
    if values != (['true'] if enabled else []):
        raise SystemExit('presentation fork config does not match frozen option')


def presentation_fork_environment(enabled):
    return {'X4_DEV_PRESENTATION_FORK': '1'} if enabled else {}


def collect_preload_evidence(result, local, games, run_dir, manifest, *, view_query_enabled,
                             ui_callback_enabled, lifetime_preload, fork_enabled, recurring, physics_frames,
                             ui_overlay_enabled=False):
    """Retain independent fork diagnostics before preserving the first observer failure."""
    fork_errors = []
    if fork_enabled or 'player_presentation_fork_probe' in manifest:
        for slot, (_, profile) in zip(local, games):
            stderr = run_dir / f'game{slot}-stderr.log' if recurring else run_dir / f'game{slot}-stdout.log'
            fork = collect_presentation_fork_evidence(profile, stderr, slot, fork_enabled, manifest.get('player_startup_phases'))
            if fork_enabled:
                result.setdefault('presentation_fork_evidence', {})[str(slot)] = fork
            if not fork['pass']:
                fork_errors.append(f'client {slot}: invalid presentation fork evidence: {fork["errors"]}')
    overlay_errors = []
    if ui_overlay_enabled or 'player_ui_overlay_probe' in manifest:
        for slot, (_, profile) in zip(local, games):
            stderr = run_dir / f'game{slot}-stderr.log' if recurring else run_dir / f'game{slot}-stdout.log'
            fork = result.get('presentation_fork_evidence', {}).get(str(slot), {})
            evidence = collect_ui_overlay_evidence(profile, stderr, slot, ui_overlay_enabled, fork,
                                                   manifest.get('player_ui_overlay_callbacks', 1), manifest.get('player_ui_session_frames', 0),
                                                   manifest.get('player_startup_phases'))
            if ui_overlay_enabled:
                result.setdefault('ui_overlay_evidence', {})[str(slot)] = evidence
            if not evidence['pass']:
                overlay_errors.append(f'client {slot}: invalid UI overlay evidence: {evidence["errors"]}')
    plan_kick_enabled = validate_plan_kick(manifest)
    if plan_kick_enabled or 'player_ui_plan_kick_probe' in manifest:
        from linux_ui_plan_kick_evidence import collect_plan_kick_evidence
        for slot, (_, profile) in zip(local, games):
            stderr = run_dir / f'game{slot}-stderr.log' if recurring else run_dir / f'game{slot}-stdout.log'
            overlay = result.get('ui_overlay_evidence', {}).get(str(slot), {})
            evidence = collect_plan_kick_evidence(profile, stderr, slot, plan_kick_enabled, overlay)
            if plan_kick_enabled:
                result.setdefault('ui_plan_kick_evidence', {})[str(slot)] = evidence
            if not evidence['pass']:
                overlay_errors.append(f'client {slot}: invalid plan-kick evidence: {evidence["errors"]}')
    seat_query_enabled = validate_seat_query(manifest)
    if seat_query_enabled or 'player_ui_seat_query_probe' in manifest:
        from linux_ui_seat_query_evidence import collect_ui_seat_query_evidence
        for slot, (_, profile) in zip(local, games):
            stderr = run_dir / f'game{slot}-stderr.log' if recurring else run_dir / f'game{slot}-stdout.log'
            overlay = result.get('ui_overlay_evidence', {}).get(str(slot), {})
            fork = result.get('presentation_fork_evidence', {}).get(str(slot), {})
            evidence = collect_ui_seat_query_evidence(profile, stderr, slot, seat_query_enabled, overlay, fork)
            if seat_query_enabled:
                result.setdefault('ui_seat_query_evidence', {})[str(slot)] = evidence
            if not evidence['pass']:
                overlay_errors.append(f'client {slot}: invalid seat-query evidence: {evidence["errors"]}')
    live_pose_enabled = validate_live_pose_observer(manifest)
    camera_refill_enabled = camera_refill_bundle.validate_options(manifest)
    if live_pose_enabled or 'player_ui_live_pose_observer' in manifest:
        validate_live_pose_evidence_contract(manifest)
        from linux_ui_live_pose_observer_evidence import collect_ui_live_pose_observer_evidence
        from linux_ui_pose_scope_evidence import collect_ui_pose_scope_evidence
        for slot, (_, profile) in zip(local, games):
            stderr = run_dir / f'game{slot}-stderr.log' if recurring else run_dir / f'game{slot}-stdout.log'
            overlay = result.get('ui_overlay_evidence', {}).get(str(slot), {})
            fork = result.get('presentation_fork_evidence', {}).get(str(slot), {})
            query = result.get('ui_seat_query_evidence', {}).get(str(slot), {})
            render_enabled = validate_render_lease(manifest, frozen=True)
            render = None
            if render_enabled or 'player_ui_render_lease' in manifest:
                from linux_ui_render_lease_evidence import collect_ui_render_lease_evidence
                callback = {}
                if render_enabled and slot == 2:
                    try:
                        callback = parse_ui_callback_evidence(
                            (profile/'engine-output.log').read_text(errors='strict').splitlines(),
                            stderr.read_text(errors='strict').splitlines(),
                            (profile/'controller-lifecycle.log').read_text(errors='strict').splitlines(keepends=True),
                            slot, physics_frames, manifest.get('player_ui_session_frames',0),
                            manifest.get('player_startup_phases'))
                        callback.update(engine_sha256=sha(profile/'engine-output.log'),
                                        stderr_sha256=sha(stderr),
                                        native_fd_sha256=sha(profile/'controller-lifecycle.log'))
                    except (OSError, UnicodeError):
                        callback = {'pass': False, 'errors': ['render callback inputs unreadable']}
                render_evidence = collect_ui_render_lease_evidence(
                    profile, stderr, slot, render_enabled, overlay, fork, query, callback, camera_refill_enabled)
                if render_enabled:
                    result.setdefault('ui_render_lease_evidence', {})[str(slot)] = render_evidence
                    if slot == 2:
                        render = render_evidence
                        if projection_scope_bundle.enabled(manifest):
                            projection = projection_scope_bundle.collect_order(stderr, manifest, slot)
                            if projection['pass']:
                                render['projection_fov_count'] = projection['receipt_count'] - 1
                            else:
                                overlay_errors.append(f'client {slot}: invalid projection FOV scope: {projection["errors"]}')
                if not render_evidence['pass']:
                    overlay_errors.append(f'client {slot}: invalid render lease evidence: {render_evidence["errors"]}')
            evidence = collect_ui_live_pose_observer_evidence(profile, stderr, slot, live_pose_enabled,
                                                               overlay, fork, query, render)
            if live_pose_enabled:
                result.setdefault('ui_live_pose_observer_evidence', {})[str(slot)] = evidence
            if not evidence['pass']:
                overlay_errors.append(f'client {slot}: invalid live-pose observation: {evidence["errors"]}')
            scope = collect_ui_pose_scope_evidence(profile, stderr, slot, live_pose_enabled, evidence, overlay, render)
            if live_pose_enabled:
                result.setdefault('ui_pose_scope_evidence', {})[str(slot)] = scope
            if not scope['pass']:
                overlay_errors.append(f'client {slot}: invalid pose-scope observation: {scope["errors"]}')
            from linux_ui_seat_correction_evidence import collect_ui_seat_correction_evidence
            correction_enabled = validate_seat_correction(manifest, frozen=True)
            correction = collect_ui_seat_correction_evidence(
                profile, stderr, slot, correction_enabled, overlay, fork, query, evidence, scope, render)
            if correction_enabled:
                result.setdefault('ui_seat_correction_evidence', {})[str(slot)] = correction
            if not correction['pass']:
                overlay_errors.append(f'client {slot}: invalid seat correction evidence: {correction["errors"]}')
            if camera_refill_enabled or 'player_ui_camera_refill' in manifest:
                from linux_ui_camera_refill_evidence import collect_camera_refill_evidence
                refill = collect_camera_refill_evidence(
                    profile, stderr, slot, camera_refill_enabled, overlay, fork, query, render or {})
                result.setdefault('ui_camera_refill_evidence', {})[str(slot)] = refill
                if not refill['pass']:
                    overlay_errors.append(f'client {slot}: invalid camera refill evidence: {refill["errors"]}')
    if 'ui_callback_observer' in manifest and not ui_callback_enabled:
        for slot, (_, profile) in zip(local, games):
            engine_text = (profile / 'engine-output.log').read_text(errors='strict')
            stderr_text = (run_dir / f'game{slot}-stderr.log').read_text(errors='strict')
            if 'LINUX_UI_CALLBACK_' in engine_text or 'X4 UI CALLBACK RESULT' in stderr_text:
                raise ValueError(f'client {slot}: unexpected disabled UI callback evidence')
    if view_query_enabled:
        result['player_view_query_evidence'] = {}
        result['player_view_query_receipts'] = {}
        view_errors = []
        for slot, (_, profile) in zip(local, games):
            try:
                with (run_dir / f'game{slot}-stderr.log').open(errors='strict') as stream:
                    receipts = [line.rstrip('\n') for line in stream if 'X4 VIEW QUERY PRELOAD' in line]
            except (OSError, UnicodeError):
                receipts = []
            result['player_view_query_receipts'][str(slot)] = receipts
            if not validate_view_query_receipts(receipts):
                view_errors.append(f'client {slot}: missing, repeated or refused view query observer activation')
            evidence = collect_view_query_evidence(profile / 'engine-output.log', slot, physics_frames, manifest.get('player_ui_session_frames',0),
                            manifest.get('player_startup_phases'))
            result['player_view_query_evidence'][str(slot)] = evidence
            if ui_callback_enabled:
                try:
                    engine_lines = (profile / 'engine-output.log').read_text(errors='strict').splitlines()
                    stderr_lines = (run_dir / f'game{slot}-stderr.log').read_text(errors='strict').splitlines()
                    presentation_lines = (profile / 'controller-lifecycle.log').read_text(errors='strict').splitlines(keepends=True)
                    callback = parse_ui_callback_evidence(engine_lines, stderr_lines, presentation_lines, slot, physics_frames, manifest.get('player_ui_session_frames',0),
                            manifest.get('player_startup_phases'))
                except (OSError, UnicodeError):
                    callback = {'pass': False, 'errors': ['callback evidence files unreadable']}
                result.setdefault('ui_callback_evidence', {})[str(slot)] = callback
                if not callback['pass']:
                    view_errors.append(f'client {slot}: invalid UI callback evidence: {callback["errors"]}')
            if not evidence['pass']:
                view_errors.append(f'client {slot}: invalid view query evidence: {evidence["errors"]}')
        if view_errors:
            raise ValueError('; '.join(view_errors))
    if lifetime_preload:
        result['player_lifetime_receipts'] = {}
        for slot in local:
            stderr = run_dir / f'game{slot}-stderr.log'
            with stderr.open(errors='strict') as stream:
                receipts = [line.rstrip('\n') for line in stream if 'X4 PLAYER LIFETIME PRELOAD' in line]
            result['player_lifetime_receipts'][str(slot)] = receipts
            if not validate_lifetime_receipts(receipts):
                raise ValueError(f'client {slot}: missing, repeated or refused lifetime preload activation')
    if fork_errors or overlay_errors:
        raise ValueError('; '.join(fork_errors + overlay_errors))


def collect_presentation_fork_evidence(profile, stderr, slot, enabled, startup=None):
    # Read streams independently: an absent/truncated trace must not hide stderr refusal.
    paths = {'engine_sha256': profile / 'engine-output.log', 'stderr_sha256': stderr}
    if enabled:
        paths['native_fd_sha256'] = profile / 'controller-lifecycle.log'
    lines, hashes, errors = {}, {}, []
    for key, path in paths.items():
        try:
            lines[key] = path.read_text(errors='strict').splitlines(keepends=True)
            hashes[key] = sha(path)
        except (OSError, UnicodeError) as error:
            lines[key] = []
            errors.append('presentation fork file unavailable: ' + str(error))
    engine_lines, stderr_lines = lines['engine_sha256'], lines['stderr_sha256']
    native = [line for line in stderr_lines if 'X4 PRESENTATION FORK' in line]
    markers = [line for line in engine_lines if 'LINUX_PRESENTATION_FORK' in line]
    if errors:
        return {'pass': False, 'errors': errors, 'raw_native_receipts': native,
                'raw_engine_receipts': markers, **hashes}
    if not enabled:
        unexpected = native or any('PRESENTATION_FORK' in line for line in engine_lines)
        return {'pass': not unexpected, 'enabled': False,
                'errors': ['unexpected presentation fork evidence while disabled'] if unexpected else []}
    result = parse_presentation_fork_evidence(engine_lines, stderr_lines, lines['native_fd_sha256'], slot, startup)
    result.update(hashes)
    return result


def parse_presentation_fork_evidence(engine_lines, stderr_lines, trace_lines, slot, startup=None):
    """Bind the child's measured roundtrip to the existing parent identities."""
    result = {'pass': False, 'errors': [], 'readiness_proven': False, 'safe_resume_proven': False}
    errors = result['errors']
    phase = startup_phases(startup)
    if type(slot) is not int or slot not in (1, 2):
        errors.append('invalid presentation fork slot')
        return result
    markers = [(i, line) for i, line in enumerate(engine_lines) if 'LINUX_PRESENTATION_FORK' in line]
    result['raw_engine_receipts'] = [line for _, line in markers]
    raw = [line for line in stderr_lines if 'X4 PRESENTATION FORK' in line]
    result['raw_native_receipts'] = raw
    if (len(markers) != 2 or any(len(line) > 4096 or line.count('LINUX_PRESENTATION_FORK') != 1 or not line.endswith('\n') for _, line in markers) or
            len(raw) != 1 or len(raw[0]) > 4096 or not raw[0].endswith('\n')):
        errors.append('missing, duplicate, truncated or oversized presentation fork receipts')
        return result
    prefix = r'.*?\[X4LAN\] NATIVE_GATE '
    if not re.fullmatch(prefix + rf'LINUX_PRESENTATION_FORK_ARMED tick={phase["presentation_probe"]} slot={slot}\n', markers[0][1]):
        errors.append('invalid presentation fork arm')
    pointer, number = r'([1-9a-f][0-9a-f]{0,15})', r'(0|[1-9][0-9]{0,19})'
    payload = (r'v=1 status=pass parent_unchanged=1 child_reaped=1 lease_held=0 '
               + ' '.join(name + '=' + pointer for name in ('p1', 'p2', 'room')) + ' '
               + ' '.join(name + '=' + number for name in ('p1_ship', 'p2_ship', 'before', 'overlay', 'restored'))
               + ' live=' + pointer + ' cached=' + pointer + ' refs=' + number + ',' + number + ',' + number
               + ' marker=' + number + ',' + number + r' release_attempts=1')
    match = re.fullmatch(prefix + rf'LINUX_PRESENTATION_FORK_RESULT tick={phase["presentation_arm"]} slot={slot} (' + payload + r')\n', markers[1][1])
    if not match:
        errors.append('invalid presentation fork result grammar or success state')
        return result
    receipt = match.group(1)
    if raw != ['X4 PRESENTATION FORK RESULT ' + receipt + '\n']:
        errors.append('native and engine presentation fork receipts differ')
    names = ('p1', 'p2', 'room', 'p1_ship', 'p2_ship', 'before', 'overlay', 'restored',
             'live', 'cached', 'refs_before', 'refs_acquired', 'refs_after', 'marker_before', 'marker_after')
    values = dict(zip(names, match.groups()[1:]))
    parsed = {key: int(value, 16 if key in ('p1', 'p2', 'room', 'live', 'cached') else 10)
              for key, value in values.items()}
    result['measurements'] = parsed
    if any(value > 0xffffffffffffffff for value in parsed.values()):
        errors.append('presentation fork field outside native uint64 range')
    from linux_player_presentation_evidence import validate_presentation_rows
    presentation = validate_presentation_rows(trace_lines, slot, tick=phase['presentation_probe'])
    if not presentation['pass']:
        errors.append('missing or invalid authenticated presentation identities')
    else:
        first, second = presentation['observations']
        if (parsed['p1'] != first['player'] or parsed['p2'] != second['player'] or
                parsed['room'] != second['parent'] or parsed['p1_ship'] != presentation['occupied_runtime']):
            errors.append('presentation fork player, room or P1 ship identity mismatch')
    runtimes = set()
    for line in trace_lines:
        if 'physics=body ' not in line or ' index=0 ' not in line:
            continue
        runtime = re.findall(r' runtime=([^ \n]*)', line)
        roles = re.findall(r' role=([^ \n]*)', line)
        slots = re.findall(r' slot=([^ \n]*)', line)
        if (not line.startswith('X4LAN LIFECYCLE_DISCARD physics=body ') or len(runtime) != 1 or
                not re.fullmatch(r'[0-9]{1,20}', runtime[0]) or
                (roles and roles != ['p2']) or (slots and slots != [str(slot)])):
            errors.append('malformed retained P2 body runtime, role or slot')
            continue
        value = int(runtime[0])
        if value > 0xffffffffffffffff:
            errors.append('retained P2 body runtime outside uint64 range')
            continue
        runtimes.add(value)
    if runtimes != {parsed['p2_ship']}:
        errors.append('presentation fork P2 ship differs from retained body evidence')
    if (not parsed['p1_ship'] or not parsed['p2_ship'] or parsed['p1_ship'] == parsed['p2_ship'] or
            parsed['before'] != parsed['p1_ship'] or parsed['restored'] != parsed['p1_ship'] or
            parsed['overlay'] != parsed['p2_ship'] or parsed['live'] != parsed['room'] or parsed['cached'] != parsed['room']):
        errors.append('presentation fork P1/P2/P1 roundtrip or room mismatch')
    if (not 1 < parsed['refs_before'] < 0xffffffff or parsed['refs_acquired'] != parsed['refs_before'] + 1 or
            parsed['refs_after'] != parsed['refs_before'] or parsed['marker_before'] != 2 or parsed['marker_after'] != 2):
        errors.append('presentation fork reference or marker imbalance')
    prior = [i for i, line in enumerate(engine_lines)
             if re.fullmatch(prefix + rf'LINUX_PRESENTATION_PROBE tick={phase["presentation_probe"]} slot={slot}\n', line)]
    if len(prior) != 1 or not prior[0] < markers[0][0] < markers[1][0]:
        errors.append('presentation probe, fork arm and result ordering invalid')
    result['limitations'] = ['child-only measured getter roundtrip; no UI handler, reader exclusion or gameplay acceptance']
    result['pass'] = not errors
    return result


def validate_ui_callback_observer(options, frames):
    enabled = options.get('ui_callback_observer', False)
    if type(enabled) is not bool:
        raise SystemExit('ui_callback_observer must be a boolean')
    if enabled and (frames < 5 or options.get('player_view_query_observer') is not True or
                    options.get('player_presentation_probe') is not True):
        raise SystemExit('UI callback observer requires view-query, presentation, and at least 5 independent frames')
    return enabled


def validate_ui_callback_config(config, enabled):
    values = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_linux_ui_callback_observer\s*=\s*(.*?)\s*$', config)
    if (enabled and values != ['true']) or (not enabled and values not in ([], ['false'])):
        raise SystemExit('UI callback config does not match its frozen option')


def parse_ui_callback_evidence(engine_lines, stderr_lines, presentation_lines, slot, frames, session_frames=0, startup=None):
    """Validate one drained callback epoch; this does not establish exclusion."""
    errors = []
    phase = startup_phases(startup)
    final_tick = phase['apply'] + frames - 1
    maximum = session_frames or 600
    if session_frames:
        try:
            budget = budget_from_options({'player_ui_session_frames': session_frames, 'player_startup_phases': startup})
            if budget['frames'] != frames:
                raise ValueError('session callback budget mismatch')
        except ValueError as error:
            return {'pass': False, 'errors': [str(error)]}
    if type(slot) is not int or slot not in (1, 2) or type(frames) is not int or not 5 <= frames <= maximum:
        return {'pass': False, 'errors': ['invalid callback evidence parameters']}
    prefix = 'LINUX_UI_CALLBACK_'
    rows = [line[line.index(prefix):].rstrip('\n') for line in engine_lines if prefix in line]
    close = f'LINUX_UI_CALLBACK_CLOSE tick={final_tick - 1} slot={slot}'
    counters = r'[0-9]{1,20}(?:/[0-9]{1,20}){8}'
    pattern = (rf'LINUX_UI_CALLBACK_RESULT tick={final_tick} slot={slot} '
               r'(v=1 owner=([1-9][0-9]{0,9}) object=([1-9a-f][0-9a-f]{0,15}) '
               rf'move=({counters}) render=({counters}))')
    match = re.fullmatch(pattern, rows[1]) if len(rows) == 2 else None
    if len(rows) != 2 or rows[0] != close or not match:
        errors.append('missing, duplicated, malformed or reordered close/result')
        return {'pass': False, 'errors': errors}
    payload, owner, obj, move, render = match.groups()
    owner = int(owner)
    if owner > 0xffffffff or int(obj, 16) > 0xffffffffffffffff:
        errors.append('invalid callback identity')
    parsed = []
    for text in (move, render):
        fields = text.split('/')
        if len(fields) != 9 or any(not re.fullmatch(r'0|[1-9][0-9]*', x) for x in fields):
            errors.append('malformed callback counters')
            continue
        values = [int(x) for x in fields]
        if any(x > 0xffffffffffffffff for x in values) or not values[0] or values[0] != values[1] or any(values[3:8]) or values[8] != owner:
            errors.append('unbalanced, foreign, nested, changed or empty callback scope')
        parsed.append(values)
    from linux_player_presentation_evidence import validate_presentation_rows
    presentation = validate_presentation_rows(presentation_lines, slot, tick=phase['presentation_probe'])
    if not presentation['pass'] or int(obj, 16) != presentation.get('ui_plan', {}).get('presenter', -9) + 8:
        errors.append('callback object does not match authenticated presentation owner')
    arm = [index for index, line in enumerate(engine_lines) if 'LINUX_VIEW_QUERY_ARMED' in line]
    arm_rows = [engine_lines[index].split('LINUX_VIEW_QUERY_ARMED', 1)[1].rstrip('\n') for index in arm]
    if arm_rows != [f' tick={phase["presentation_probe"]} slot={slot}']:
        errors.append('callback arm tick or slot invalid')
    close_index = [index for index, line in enumerate(engine_lines) if 'LINUX_UI_CALLBACK_CLOSE' in line]
    result_index = [index for index, line in enumerate(engine_lines) if 'LINUX_UI_CALLBACK_RESULT' in line]
    if len(arm) != 1 or len(close_index) != 1 or len(result_index) != 1 or not arm[0] < close_index[0] < result_index[0]:
        errors.append('callback arm, close and result order invalid')
    raw = [line.rstrip('\n') for line in stderr_lines if 'X4 UI CALLBACK RESULT' in line]
    if raw != ['X4 UI CALLBACK RESULT ' + payload]:
        errors.append('missing, duplicate or mismatched deferred preload receipt')
    return {'pass': not errors, 'errors': errors, 'owner': owner, 'object': obj,
            'move': parsed[0] if len(parsed) == 2 else None,
            'render': parsed[1] if len(parsed) == 2 else None,
            'coverage': 'callbacks entered after arm and before close request',
            'readiness_proven': False}


def view_query_sources(repo):
    sources = [repo / name for name in VIEW_QUERY_SOURCES]
    if not all(source.is_file() for source in sources):
        raise SystemExit('view query observer requires all frozen source files')
    return sources


def validate_view_query_bundle(root, manifest, network_physics):
    enabled = validate_view_query_observer(manifest, manifest.get('independent_keyboard_frames', 0))
    library = 'player_view_query/' + PLAYER_VIEW_QUERY
    files = manifest.get('files', {})
    if not isinstance(files, dict):
        raise SystemExit('view query observer file inventory must be a mapping')
    digest = manifest.get('player_view_query_sha256')
    size = manifest.get('player_view_query_size')
    if not enabled:
        allowed_receipts = {'source-receipts/' + source for source in VIEW_QUERY_SOURCES}
        if size is not None or digest is not None or (root / library).exists():
            raise SystemExit('disabled view query observer has frozen observer artifacts')
        for name, expected in files.items():
            if 'player_view_query_preload' not in name:
                continue
            if name not in allowed_receipts:
                raise SystemExit('disabled view query observer has frozen observer artifacts')
            path = root / name
            if (not isinstance(expected, str) or re.fullmatch('[0-9a-f]{64}', expected) is None or
                    not path.is_file() or path.is_symlink() or sha(path) != expected):
                raise SystemExit('disabled view query observer has invalid source receipt: ' + name)
        return False
    if not network_physics:
        raise SystemExit('view query observer requires disposable independent network physics')
    if type(size) is not int or size <= 0 or not (root / library).is_file() or (root / library).stat().st_size != size:
        raise SystemExit('view query observer library size is missing or inconsistent')
    if not isinstance(digest, str) or re.fullmatch('[0-9a-f]{64}', digest) is None or files.get(library) != digest:
        raise SystemExit('view query observer library hash is missing or inconsistent')
    for name in [library, *('source-receipts/' + source for source in VIEW_QUERY_SOURCES)]:
        expected = files.get(name)
        path = root / name
        if (not isinstance(expected, str) or re.fullmatch('[0-9a-f]{64}', expected) is None or
                not path.is_file() or path.is_symlink() or sha(path) != expected):
            raise SystemExit('view query observer requires intact library and source receipts: ' + name)
    return True


def validate_view_query_config(config, enabled):
    assignments = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_linux_view_query_observer\s*=\s*(.*?)\s*$', config)
    if assignments != (['true'] if enabled else []):
        raise SystemExit('view query observer config does not match the frozen option')


def view_query_environment(env, case, digest, size):
    # Keep the existing lifetime/math order and append the observer for this owned X4 only.
    return dict(env, LD_PRELOAD=' '.join(filter(None, (env.get('LD_PRELOAD'), str(case / PLAYER_VIEW_QUERY)))),
                X4MP_VIEW_QUERY_OBSERVER='1', X4MP_VIEW_QUERY_PRELOAD_SHA256=digest,
                X4MP_VIEW_QUERY_PRELOAD_SIZE=str(size))


def validate_view_query_receipts(receipts):
    return receipts == ['X4 VIEW QUERY PRELOAD ACTIVE version=1']


def parse_view_query_evidence(lines, slot, frames, session_frames=0, startup=None):
    """Validate the frozen observer report, without treating queries as readiness."""
    errors, rows = [], []
    result = {'pass': False, 'errors': errors, 'rows': rows, 'raw_markers': [],
              'observation_complete': False, 'readiness_proven': False,
              'row_scope': 'outside_marked_frame'}
    phase = startup_phases(startup)
    final_tick = phase['apply'] + frames - 1
    maximum = session_frames or 600
    if session_frames:
        try:
            budget = budget_from_options({'player_ui_session_frames': session_frames, 'player_startup_phases': startup})
            if budget['frames'] != frames:
                raise ValueError('session view-query budget mismatch')
        except ValueError as error:
            errors.append(str(error))
            return result
    if type(slot) is not int or slot not in (1, 2) or type(frames) is not int or not 4 <= frames <= maximum:
        errors.append('invalid view query slot or frame window')
        return result
    markers = [line for line in lines if 'LINUX_VIEW_QUERY' in line]
    result['raw_markers'] = markers
    prefix = r'.*?\[X4LAN\] NATIVE_GATE '
    armed = re.compile(prefix + rf'LINUX_VIEW_QUERY_ARMED tick={phase["presentation_probe"]} slot=' + str(slot) + r'\n')
    report = re.compile(prefix + r'LINUX_VIEW_QUERY_RESULT tick=' + str(final_tick) +
        r' slot=' + str(slot) + r' v=1 finished=1 ui_calls=([0-9]{1,20}) worker_calls=([0-9]{1,20}) '
        r'other_calls=([0-9]{1,20}) probe_calls=([0-9]{1,20}) overflow=([0-9]{1,20}) rows=([0-9]{1,2}) data=([^\n]*)\n')
    if (len(markers) != 2 or any(len(line) > 16640 or line.count('LINUX_VIEW_QUERY') != 1 for line in markers) or
            not armed.fullmatch(markers[0])):
        errors.append('missing, repeated, malformed or misordered view query markers')
        return result
    match = report.fullmatch(markers[1])
    if not match:
        errors.append('invalid view query final report, tick, version or slot')
        return result
    counts = dict(zip(('ui_calls', 'worker_calls', 'other_calls', 'probe_calls', 'overflow', 'row_count'),
                      map(int, match.groups()[:6])))
    result.update(counts)
    if any(value > (1 << 64) - 1 for value in counts.values()) or counts['row_count'] > 64:
        errors.append('view query counts exceed native bounds')
        return result
    data = match.group(7)
    entries = data.split(';') if data else []
    if len(entries) != counts['row_count']:
        errors.append('view query row count mismatch')
        return result
    grammar = re.compile(r'([0-9a-f]{1,16})/([0-9]{1,20})/([0-9]{1,20})/([0-9]{1,20})/([0-9]{1,20})/([01])/0')
    seen, previous_first = set(), phase['presentation_probe']
    for entry in entries:
        match = grammar.fullmatch(entry)
        if not match:
            errors.append('malformed view query row')
            continue
        caller = int(match.group(1), 16)
        value, first, last, count, in_image = map(int, match.groups()[1:])
        key = (caller, value, in_image)
        if (not caller or max(value, first, last, count) > (1 << 64) - 1 or not count or
                not phase['presentation_probe'] <= first <= last <= final_tick or first < previous_first or key in seen or
                in_image != int(0x656000 <= caller < 0x2c730a9)):
            errors.append('invalid view query row identity, range, count or order')
        seen.add(key)
        previous_first = first
        rows.append(dict(caller=match.group(1), result=value, first_tick=first, last_tick=last,
                         count=count, in_image=in_image, probe=0))
    retained = sum(row['count'] for row in rows)
    ordinary = counts['ui_calls'] - counts['probe_calls']
    if (ordinary < 0 or retained + counts['overflow'] > ordinary or
            (counts['overflow'] and counts['row_count'] != 64)):
        errors.append('inconsistent view query counter totals or overflow')
    result['unrepresented_ui_calls'] = ordinary - retained - counts['overflow']
    result['observation_complete'] = not errors and not counts['overflow'] and result['unrepresented_ui_calls'] == 0
    result['limitations'] = ['outside_marked_frame includes unmarked menu/default-notice/pause work; not stock-UI-only',
                              'query observation does not establish player view or readiness']
    if counts['overflow']:
        result['limitations'].append('native row capacity exceeded; callers were omitted')
    if result['unrepresented_ui_calls']:
        result['limitations'].append('UI counters include calls without retained rows')
    result['pass'] = not errors
    return result


def collect_view_query_evidence(engine, slot, frames, session_frames=0, startup=None):
    # Read linewise and keep only the two bounded receipts, never the world dump.
    try:
        markers = []
        with Path(engine).open(encoding='utf-8', errors='strict') as stream:
            for line in stream:
                if 'LINUX_VIEW_QUERY' in line:
                    if len(line) > 16640 or len(markers) == 2:
                        return {'pass': False, 'errors': ['oversized or repeated view query receipt'],
                                'rows': [], 'observation_complete': False, 'readiness_proven': False}
                    markers.append(line)
        result = parse_view_query_evidence(markers, slot, frames, session_frames, startup)
        result['engine_sha256'] = sha(Path(engine))
        return result
    except (OSError, UnicodeError) as error:
        return {'pass': False, 'errors': ['view query engine log unavailable: ' + str(error)],
                'rows': [], 'observation_complete': False, 'readiness_proven': False}


def recurring_diagnostic_sources(repo):
    engine = repo / 'src/native/engine/linux'
    sources = [engine / name for name in (
        'gate_player_recurring_receipt.h', 'gate_player_recurring_pause.h')]
    if not all(source.is_file() for source in sources):
        raise SystemExit('recurring diagnostics require both frozen receipt and pause headers')
    return sources


def freeze_source_receipts(repo, out, sources):
    for source in sources:
        destination = out / 'source-receipts' / source.relative_to(repo)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def validate_rolling_leaf_ticks(value, rolling_slices, ticks):
    if value is None:
        return
    if (not isinstance(value, (list, tuple)) or len(value) != 2 or
        any(type(tick) is not int for tick in value) or
        type(rolling_slices) is not int or not 1 <= rolling_slices <= 18000 or
        type(ticks) is not int or not 1 <= value[0] < value[1] <= min(18000, ticks)):
        raise SystemExit('--world-rolling-leaf-ticks requires rolling census and two ordered ticks within the run')


def validate_rolling_raw_ids(ids, leaf_ticks):
    if ids is None:
        return
    if (leaf_ticks is None or not isinstance(ids, (list, tuple)) or not 1 <= len(ids) <= 8 or
        any(not isinstance(value, str) or re.fullmatch('[0-7][0-9a-f]{15}', value) is None or
            value == '0000000000000000' for value in ids) or len(set(ids)) != len(ids)):
        raise SystemExit('--world-rolling-raw-ids requires leaf ticks and 1..8 unique nonzero saved IDs as 16 lowercase hex digits')


def validate_throttle_probe(options, independent_frames):
    enabled = options.get('player_throttle_probe', False)
    warm = options.get('player_throttle_warm_probe', False)
    if type(enabled) is not bool or type(warm) is not bool:
        raise SystemExit('throttle probe options must be booleans')
    if enabled and warm:
        raise SystemExit('throttle probe modes are mutually exclusive')
    if (enabled or warm) and (independent_frames < (3 if warm else 2) or
                              options.get('player_external_camera_probe')):
        raise SystemExit('throttle probe requires independent keyboard frames and excludes the camera probe')
    return enabled or warm


def throttle_probe_sources(repo):
    source = repo / 'src/native/engine/linux/gate_player_throttle_discard.h'
    if not source.is_file():
        raise SystemExit('throttle probe requires gate_player_throttle_discard.h')
    return [source]


def throttle_probe_environment(manifest, network_physics):
    enabled = validate_throttle_probe(manifest, manifest.get('independent_keyboard_frames', 0))
    if enabled and not network_physics:
        raise SystemExit('throttle probe requires disposable independent network physics')
    warm = manifest.get('player_throttle_warm_probe', False)
    expected_start = 6 if warm else 5 if enabled else None
    expected_mode = 'state13-warm-discard' if warm else 'state13-discard' if enabled else None
    if ((warm and ('player_throttle_start_tick' not in manifest or
                   'player_throttle_environment' not in manifest)) or
            (expected_start is not None and type(manifest.get('player_throttle_start_tick', expected_start)) is not int) or
            manifest.get('player_throttle_start_tick', expected_start) != expected_start or
            manifest.get('player_throttle_environment', expected_mode) != expected_mode):
        raise SystemExit('throttle probe frozen schedule or environment mismatch')
    return {'X4_DEV_P2_THROTTLE': expected_mode} if enabled else {}


def validate_presentation_probe(options, independent_frames):
    enabled = options.get('player_presentation_probe', False)
    if type(enabled) is not bool:
        raise SystemExit('player_presentation_probe must be a boolean')
    if enabled and independent_frames < 4:
        raise SystemExit('--player-presentation-probe requires at least 4 independent keyboard frames')
    return enabled


def validate_presentation_config(config, enabled):
    assignments = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_linux_presentation_probe\s*=\s*(.*?)\s*$', config)
    if (enabled and assignments != ['true']) or (not enabled and assignments not in ([], ['false'])):
        raise SystemExit('composed presentation config does not match the frozen option')


def presentation_probe_sources(repo):
    sources = [repo / 'src/native/engine/linux' / name for name in
               ('gate_player_presentation_probe.h', 'player_presentation_probe.h')]
    if not all(source.is_file() for source in sources):
        raise SystemExit('presentation probe requires both native presentation headers')
    return sources


def validate_external_camera(options, independent_frames):
    enabled = options.get('player_external_camera_probe', False)
    if type(enabled) is not bool:
        raise SystemExit('player_external_camera_probe must be a boolean')
    if enabled and independent_frames < 3:
        raise SystemExit('--player-external-camera-probe requires at least 3 independent keyboard frames')
    return enabled


def validate_external_camera_config(config, enabled):
    assignments = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_linux_external_camera_probe\s*=\s*(.*?)\s*$', config)
    if (enabled and assignments != ['true']) or (not enabled and assignments not in ([], ['false'])):
        raise SystemExit('composed external camera config does not match the frozen option')


def validate_network_physics(manifest, physics, frames, ticks):
    network_frames = validate_independent_keyboard(manifest, ticks)
    validate_external_camera(manifest, network_frames)
    validate_presentation_probe(manifest, network_frames)
    validate_presentation_fork(manifest, network_frames)
    validate_ui_overlay(manifest, network_frames)
    validate_view_query_observer(manifest, network_frames)
    validate_throttle_probe(manifest, network_frames)
    if not network_frames and manifest.get('independent_keyboard_mode') is not None:
        raise SystemExit('independent keyboard mode requires a nonzero frozen frame count')
    if network_frames and (not physics or frames != network_frames or
            manifest.get('independent_keyboard_mode') != 'independent_keyboard' or
            manifest.get('stock_keyboard_replay_last_capture') !=
            (budget_from_options(manifest) or {}).get('last_capture', network_frames + 2)):
        raise SystemExit('independent keyboard bundle requires matching --controller-physics-discard --controller-physics-frames and frozen capture window')
    return bool(network_frames)


def physics_environment_mode(frames, network=False):
    return 'network-frames-discard' if network else ('bounded-frames-discard' if frames > 1 else 'single-frame-discard')


def collect_network_client(arguments, stderr, frames, slot, camera=False, throttle=False, throttle_start_tick=5, presentation=False, session_frames=0, startup=None, cursor=False, space=False, *, allow_view=False, allow_semaphore_checkpoint=False):
    kwargs = {'allow_camera': True} if camera else {}
    if allow_semaphore_checkpoint is not False:
        kwargs['allow_semaphore_checkpoint'] = allow_semaphore_checkpoint
    if allow_view is not False:
        kwargs['allow_view'] = allow_view
    if cursor:
        kwargs['allow_cursor'] = True
    if space:
        kwargs['allow_space'] = True
    if session_frames:
        kwargs['session_frames'] = session_frames
    if startup:
        kwargs['startup'] = startup
    if presentation:
        kwargs['allow_presentation'] = True
    if throttle:
        kwargs['allow_throttle'] = True
        kwargs['throttle_start_tick'] = throttle_start_tick
    evidence = collect_network_physics_evidence(*arguments, frame_count=frames, stderr=stderr, slot=slot, **kwargs)
    evidence.update(mode='independent_keyboard', stderr_path=str(stderr))
    evidence.setdefault('validation_pending', False)
    if not stderr.is_file() or evidence.get('stderr_sha256') != sha(stderr):
        evidence['errors'].append('worker stderr changed after network evidence collection')
        evidence['pass'] = False
    return evidence


def add_camera_evidence(physics_result, run_dir, profiles):
    camera_result = collect_camera_evidence(run_dir, profiles)
    physics_result['camera_probe_result'] = camera_result
    physics_result['pass'] = physics_result['pass'] and camera_result['pass']


def ancestry_expected_identity(client, stderr):
    """Use independently validated network, presentation and body receipts."""
    from linux_player_network_evidence import SLOT1
    if client.get('pass') is not True:
        raise ValueError('ancestry requires accepted network physics evidence')
    presentation = client.get('presentation') or {}
    frames = client.get('frames') or []
    if presentation.get('pass') is not True or not frames or not frames[0].get('bodies'):
        raise ValueError('ancestry requires accepted presentation and retained-body evidence')
    saved = {int(match[3], 16) for line in stderr.read_text().splitlines(keepends=True)
             if (match := SLOT1.fullmatch(line))}
    if len(saved) != 1 or not next(iter(saved)):
        raise ValueError('missing independent P1 saved identity')
    body = frames[0]['bodies'][0]
    return {'p2': body['ship'], 'p1_runtime': presentation['occupied_runtime'],
            'p2_runtime': body['runtime'], 'p1_saved': next(iter(saved)),
            'p2_saved': 0x175310}


def validate_external_guest(enabled, external, local, clients, network_physics, manifest,
                            target_snapshot, protocol, relay_host, arm_at, target_commit):
    """Admit a single local guest only for the frozen two-slot experiment.

    The supplied target values are operator declarations, not remote attestations.
    """
    if not enabled:
        if target_snapshot is not None or protocol is not None or target_commit is not None:
            raise SystemExit('external guest target pins require --external-player-guest')
        return None
    if not (external and local == [2] and clients == 2 and network_physics and
            relay_host is not None and arm_at is not None and
            manifest.get('shared_identity_t40') is True and manifest.get('source_dirty') is False):
        raise SystemExit('external guest requires relay host and arm time, slot 2, two-player network physics, clean source and T40 identity')
    if protocol != 3 or not isinstance(target_snapshot, str) or not re.fullmatch(r'[0-9a-f]{64}', target_snapshot):
        raise SystemExit('external guest requires protocol 3 and a lowercase target snapshot SHA256')
    if not isinstance(target_commit, str) or not re.fullmatch(r'[0-9a-f]{40}', target_commit) or target_commit != manifest.get('commit'):
        raise SystemExit('external guest requires matching declared source commit')
    if target_snapshot != manifest.get('snapshot_sha256'):
        raise SystemExit('external guest target snapshot differs from frozen bundle')
    return {'role': 'guest', 'local_slot': 2, 'logical_players': 2, 'protocol': 3,
            'target_snapshot_sha256': target_snapshot, 'target_pins_are_declarations': True,
            'local_executable_sha256': EXE_SHA, 'local_gate_sha256': manifest['gate_so_sha256'],
            'local_bundle_commit': manifest['commit'], 'declared_target_commit': target_commit, 'remote_evidence': 'pending',
            'cross_peer_equality': None, 'world_equality': None}


def external_guest_local_physics(clients, frames):
    """Check only the independently parsed guest; no remote comparison is implied."""
    guest = clients.get('2') if set(clients) == {'2'} else None
    if not isinstance(guest, dict) or guest.get('slot') != 2 or guest.get('pass') is not True:
        return False
    if guest.get('errors') or guest.get('validation_pending'):
        return False
    records = guest.get('slot_records')
    return (isinstance(records, dict) and set(records) == {'1', '2'} and
            all(isinstance(rows, list) and len(rows) == frames and
                all(isinstance(row, str) and re.fullmatch(r'(?:[0-9a-f]{2})+', row)
                    for row in rows) for rows in records.values()))


def network_peer_errors(clients, frames, throttle=False):
    """A local parser pass also needs both peers to agree on both slot streams."""
    if set(clients) != {'1', '2'}:
        return ['network evidence requires both local clients']
    for slot, evidence in clients.items():
        records = evidence.get('slot_records')
        if (evidence.get('pass') is not True or evidence.get('errors') or
                evidence.get('validation_pending') or evidence.get('slot') != int(slot) or
                not isinstance(records, dict) or set(records) != {'1', '2'}):
            return ['network client evidence failed, incomplete or pending']
        if any(not isinstance(rows, list) or len(rows) != frames or
               any(not isinstance(row, str) or not re.fullmatch(r'(?:[0-9a-f]{2})+', row) for row in rows)
               for rows in records.values()):
            return ['network slot records are missing or incomplete']
    if clients['1']['slot_records'] != clients['2']['slot_records']:
        return ['network applied slot records differ between peers']
    if throttle:
        if compare_throttle_peers is None:
            return ['strict throttle peer validator unavailable']
        return compare_throttle_peers(clients['1'], clients['2'])
    return []


def validate_lifecycle_discard(manifest, ticks):
    if manifest.get('independent_keyboard_frames'):
        validate_independent_keyboard(manifest, ticks)
    retain = manifest.get('player_factory_retain_until')
    if not (manifest.get('player_factory_probe') and manifest.get('stock_keyboard_observer')
            and manifest.get('serial_pathing') and isinstance(retain, int) and 5 < retain < ticks):
        raise SystemExit('--controller-lifecycle-discard requires bundled factory probe, retention >5 and <ticks, observer and serial pathing')
    conflicts = ('player_control_boundary_probe', 'player_control_bank_probe',
                 'player_reference_probe', 'stock_keyboard_replay_probe', 'player_seat_preflight')
    if any(manifest.get(key) for key in conflicts
           if key != 'stock_keyboard_replay_probe' or not manifest.get('independent_keyboard_frames')):
        raise SystemExit('--controller-lifecycle-discard conflicts with boundary/bank/reference/replay/seat probes')


def validate_recurring_readiness(manifest, ticks, end):
    if type(end) is not int or not 5 <= end <= 1000 or ticks <= end:
        raise SystemExit('--controller-recurring-end must be 5..1000 and less than effective --ticks')
    if not (manifest.get('stock_keyboard_observer') and manifest.get('serial_pathing')):
        raise SystemExit('--controller-recurring-readiness requires bundled observer and serial pathing')
    if manifest.get('player_control_boundary_probe') or manifest.get('player_control_bank_probe'):
        raise SystemExit('recurring readiness conflicts with control-boundary and control-bank probes')


def physics_window_end(enabled, frame_count, requested_end, ticks, *, network=False, session_frames=0, startup=None):
    if session_frames:
        from linux_ui_session_budget import session_budget
        if not network or session_budget(session_frames, startup)['frames'] != frame_count:
            raise SystemExit('session physics budget mismatch')
    maximum = session_frames or (600 if network else 180)
    if type(frame_count) is not int or not 1 <= frame_count <= maximum:
        raise SystemExit(f'--controller-physics-frames must be 1..{maximum} for this mode')
    if not enabled:
        if frame_count != 1:
            raise SystemExit('--controller-physics-frames requires --controller-physics-discard')
        return requested_end
    expected = startup_phases(startup)['apply'] + frame_count - 1
    if requested_end is not None and requested_end != expected:
        raise SystemExit('--controller-recurring-end differs from the selected startup/frame budget')
    if ticks <= expected:
        raise SystemExit('physics frames require effective --ticks >= end+1')
    return expected


def collect_physics_client(arguments, stderr, frame_count):
    if frame_count > 1:
        if collect_recurring_physics_evidence is None:
            raise RuntimeError('repeated physics evidence collector is unavailable')
        evidence = collect_recurring_physics_evidence(*arguments, frame_count=frame_count, stderr=stderr)
    else:
        evidence = collect_physics_evidence(*arguments)
        with stderr.open(errors='replace') as stream:
            messages = [line for line in stream if 'PLAYER_CONTROL_RECURRING' in line]
        if messages != ['X4LAN PLAYER_CONTROL_RECURRING_WORKER tick=5 tail=1 event=enter\n']:
            evidence['errors'].append('physics requires one worker entry without release or main completion')
        evidence.update(stderr_path=str(stderr), stderr_sha256=sha(stderr))
        evidence['pass'] = not evidence['errors']
    evidence['frame_count'] = frame_count
    return evidence


def recurring_readiness_evidence(lines, end):
    ticks, errors = [], []
    pattern = (r'X4LAN PLAYER_CONTROL_RECURRING tick=([1-9]\d*) completed=1 readiness_only=1 '
               r'bank_writes=0 context_writes=0 reference_calls=0 other_threads_excluded=0')
    for line in lines:
        if is_fatal_gate_message(line) or 'PLAYER_CONTROL_RECURRING_FATAL' in line:
            errors.append('fatal: ' + line.strip()[:300])
        if 'PLAYER_CONTROL_RECURRING' not in line or 'PLAYER_CONTROL_RECURRING_FATAL' in line:
            continue
        match = re.fullmatch(pattern, line.rstrip('\n'))
        if not match or not line.endswith('\n'):
            errors.append('malformed recurring readiness record')
        else:
            ticks.append(int(match.group(1)))
    if ticks != list(range(5, end + 1)):
        errors.append('require exactly ordered unique ticks 5..end')
    return {'pass': not errors, 'ticks': ticks, 'end': end, 'errors': errors,
            'readiness_only': True, 'other_threads_excluded': False}


def recurring_tail_readiness_evidence(lines, end):
    events, ticks, errors = [], [], []
    worker_pattern = (r'X4LAN PLAYER_CONTROL_RECURRING_WORKER tick=([1-9]\d*) '
                      r'tail=1 event=(enter|release)\n')
    main_pattern = (r'X4LAN PLAYER_CONTROL_RECURRING tick=([1-9]\d*) completed=1 readiness_only=1 '
                    r'bank_writes=0 context_writes=0 reference_calls=0 other_threads_excluded=0 tail=1\n')
    for line in lines:
        if is_fatal_gate_message(line) or 'PLAYER_CONTROL_RECURRING_FATAL' in line:
            errors.append('fatal: ' + line.strip()[:300])
        if 'PLAYER_CONTROL_RECURRING' not in line or 'PLAYER_CONTROL_RECURRING_FATAL' in line:
            continue
        worker = re.fullmatch(worker_pattern, line)
        main = re.fullmatch(main_pattern, line)
        if worker:
            events.append((worker.group(2), int(worker.group(1))))
        elif main:
            tick = int(main.group(1))
            ticks.append(tick)
            events.append(('complete', tick))
        elif line == 'X4LAN PLAYER_CONTROL_RECURRING tail=1 hook_restored=1\n':
            events.append(('restored', end + 1))
        else:
            errors.append('malformed recurring tail readiness record')
    expected = [(event, tick) for tick in range(5, end + 1)
                for event in ('enter', 'release', 'complete')] + [('restored', end + 1)]
    if events != expected:
        errors.append('require enter/release/completion per tick 5..end, then one hook restoration')
    return {'pass': not errors, 'ticks': ticks, 'end': end, 'events': events, 'errors': errors,
            'tail': True, 'hook_restored': events == expected,
            'readiness_only': True, 'other_threads_excluded': False}


def lifecycle_route_sample(text):
    match = re.fullmatch(
        r'X4LAN LIFECYCLE_DISCARD routes=sample getter=(transform|velocity|acceleration) '
        r'mode=([01]) size=(64|32) expected=([0-9a-f]+) actual=([0-9a-f]+) match=1 canary=1', text)
    if not match:
        return None
    getter, mode, size, expected, actual = match.groups()
    required_size = 64 if getter == 'transform' else 32
    if int(size) != required_size or len(expected) != required_size * 2 or expected != actual:
        return None
    return dict(getter=getter, mode=int(mode), size=required_size, expected=expected, actual=actual)


def lifecycle_route_coverage(text):
    """Canonical ship_flag still describes the held P2 ship under P1 globals."""
    match = re.fullmatch(
        r'X4LAN LIFECYCLE_DISCARD routes=coverage selected=(agent|player) '
        r'global_authority=([01]) ship_flag=([01]) canonical_authority=([01]) '
        r'canonical_ship_flag=([01]) time=([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)', text)
    if not match:
        return None
    selected, authority, ship, canonical_authority, canonical_ship, time_text = match.groups()
    time_value = float(time_text)
    expected = 'agent' if authority == '1' or ship == '0' else 'player'
    if selected != expected or not math.isfinite(time_value) or time_value <= 0:
        return None
    return dict(selected=selected, global_authority=int(authority), ship_flag=int(ship),
                canonical_authority=int(canonical_authority), canonical_ship_flag=int(canonical_ship),
                time=time_text)


def lifecycle_input_sample(text):
    match = re.fullmatch(
        r'X4LAN LIFECYCLE_DISCARD input=(press|release) id=2 state=([01]) keys=([01]{4}) '
        r'digital=([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)', text)
    if not match:
        return None
    operation, state, keys, digital_text = match.groups()
    wanted = ('1', '1000', 0) if operation == 'press' else ('0', '0000', -1)
    digital = float(digital_text)
    if (state, keys) != wanted[:2] or not math.isfinite(digital) or digital != wanted[2]:
        return None
    return dict(operation=operation, id=2, state=int(state), keys=keys, digital=digital_text)


def lifecycle_discard_evidence(lines, returncode, strict=False, isolated=False, update=False, routes=False,
                               injected_input=False):
    routes = routes or injected_input
    update = update or routes
    isolated = isolated or update
    core = ['before_take', 'after_take', 'before_release', 'after_release']
    expected = ['initial', 'prepared', 'before_take', 'after_take', 'taken']
    if injected_input:
        expected += ['input_press']
    if update:
        core += ['before_update', 'after_update']
        expected += ['before_update', 'after_update', 'update']
        if injected_input:
            expected += ['input_release']
        expected += ['updated']
    if routes:
        core += ['before_routes', 'after_routes']
        expected += ['before_routes', 'route_coverage', 'canonical_routes']
        expected += [f'route:{getter}:{mode}' for getter in ('transform', 'velocity', 'acceleration')
                     for mode in (0, 1)]
        expected += ['canonical_routes_after', 'routes', 'after_routes', 'routes_returned']
    expected += ['before_release', 'after_release', 'released']
    if isolated:
        expected += ['isolation', 'canonical_restored']
    stages, events, snapshots, finals, errors, updates = [], [], [], [], [], []
    route_samples = []
    route_coverage = []
    input_samples = []
    snapshot_pattern = (
        r'X4LAN LIFECYCLE_DISCARD snapshot=(initial|prepared|taken|updated|released|canonical_restored|canonical_routes|canonical_routes_after|routes_returned) '
        r'changed_bytes=(\d+) bank=([0-9a-f]{1992}) zone=([0-9a-f]+) zone_count=(\d+) '
        r'animation_a=([0-9a-f]+) animation_b=([0-9a-f]+) controller=([0-9a-f]+) '
        r'controller_vtable=([0-9a-f]+) contexts_ok=([01]) contexts_valid=1 '
        r'contexts_nodes=(\d+) contexts_mask=([0-7]) contexts_payloads=([0-9a-f]{96})')
    for line in lines:
        if is_fatal_gate_message(line):
            errors.append('fatal gate message: ' + line.strip()[:300])
        if 'X4LAN LIFECYCLE_DISCARD' not in line:
            if strict:
                errors.append('unexpected dedicated trace line')
            continue
        text = line.strip()
        stage = re.fullmatch(r'X4LAN LIFECYCLE_DISCARD stage=(\w+)(?: .*)?', text)
        snapshot = re.fullmatch(snapshot_pattern, text)
        final = re.fullmatch(r'X4LAN LIFECYCLE_DISCARD result=(pass|fail) reason=(\w+) exit=223 restored=0 resumed=0', text)
        isolation = re.fullmatch(
            r'X4LAN LIFECYCLE_DISCARD isolation=pass bind_hits=1 destructor_hits=1 '
            r'contexts_preserved=1 zone_restored=1 bank_restored=1 '
            r'singleton_restored=1 patch_restored=1', text)
        expected_input = 'state2' if injected_input else 'none'
        update_marker = re.fullmatch(
            r'X4LAN LIFECYCLE_DISCARD update=pass calls=1 input=' + expected_input + r' worker=1 '
            r'zone_matches=1 controller_identity=1 time=([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)', text)
        route_sample = lifecycle_route_sample(text) if routes else None
        coverage = lifecycle_route_coverage(text) if routes else None
        input_sample = lifecycle_input_sample(text) if injected_input else None
        if finals:
            errors.append('lifecycle marker after final result')
        if stage:
            name = stage.group(1)
            if name in core:
                stages.append(name)
                events.append(name)
            elif name not in ('entered', 'before_singleton', 'after_singleton') or stages:
                errors.append('unexpected extra stage')
        elif snapshot:
            fields = dict(zip(('label', 'changed_bytes', 'bank', 'zone', 'zone_count',
                               'animation_a', 'animation_b', 'controller', 'controller_vtable',
                               'contexts_ok', 'contexts_nodes', 'contexts_mask', 'contexts_payloads'),
                              snapshot.groups()))
            fields['contexts_valid'] = 1
            events.append(fields['label'])
            snapshots.append(fields)
            if int(fields['changed_bytes']) > 996:
                errors.append('changed_bytes exceeds bank length')
            if int(fields['contexts_ok']) != int(fields['contexts_mask'] == '7'):
                errors.append('contexts_ok inconsistent with contexts_mask')
        elif final:
            finals.append({'result': final.group(1), 'reason': final.group(2)})
            if events != expected:
                errors.append('final result before complete ordered unique events')
            expected_reason = ('input_lifecycle_returned' if injected_input else
                               'routed_lifecycle_returned' if routes else
                               'updated_lifecycle_returned' if update else 'isolated_lifecycle_returned')
            if isolated and final.group(2) != expected_reason:
                errors.append('isolated final reason mismatch')
        elif isolation and isolated:
            events.append('isolation')
        elif update_marker and update:
            events.append('update')
            time_value = float(update_marker.group(1))
            updates.append({'calls': 1, 'input': expected_input, 'time': update_marker.group(1)})
            if not math.isfinite(time_value) or time_value <= 0:
                errors.append('update time must be finite and positive')
        elif route_sample:
            route_samples.append(route_sample)
            events.append(f'route:{route_sample["getter"]}:{route_sample["mode"]}')
        elif coverage:
            route_coverage.append(coverage)
            events.append('route_coverage')
        elif input_sample:
            input_samples.append(input_sample)
            events.append('input_' + input_sample['operation'])
        elif routes and text == ('X4LAN LIFECYCLE_DISCARD routes=pass samples=6 '
                                 'bank_unchanged=1 singleton_canonical=1'):
            events.append('routes')
        else:
            errors.append('malformed lifecycle marker')
    if events != expected:
        errors.append('missing, duplicate or reordered snapshots/stages')
    if len(finals) != 1 or finals[0]['result'] != 'pass':
        errors.append('require exactly one final pass marker')
    if isolated and events == expected:
        by_label = {snapshot['label']: snapshot for snapshot in snapshots}
        initial, restored, released = (by_label[label] for label in
                                       ('initial', 'canonical_restored', 'released'))
        context_fields = ('contexts_valid', 'contexts_ok', 'contexts_nodes',
                          'contexts_mask', 'contexts_payloads')
        restored_fields = ('bank', 'zone', 'zone_count', 'animation_a', 'animation_b',
                           *context_fields)
        if restored['changed_bytes'] != '0' or any(initial[key] != restored[key] for key in restored_fields):
            errors.append('canonical snapshot does not match initial globals')
        if any(initial[key] != released[key] for key in context_fields):
            errors.append('released contexts differ from initial contexts')
        if routes:
            route_fields = ('bank', 'zone', 'animation_a', 'animation_b', *context_fields)
            for label in ('canonical_routes', 'canonical_routes_after'):
                observed = by_label[label]
                if (observed['changed_bytes'] != '0'
                        or any(initial[key] != observed[key] for key in route_fields)
                        or int(observed['zone_count']) != int(initial['zone_count']) + 1):
                    errors.append(f'{label} does not preserve measured canonical globals and held zone')
    if returncode != 223:
        errors.append(f'expected process exit 223, got {returncode}')
    return {'pass': not errors, 'returncode': returncode, 'stages': stages,
            'events': events, 'snapshots': snapshots, 'finals': finals, 'errors': errors,
            'isolated': isolated, 'update': update, 'updates': updates,
            'routes': routes, 'route_samples': route_samples,
            'route_coverage': route_coverage,
            'injected_input': injected_input, 'input_samples': input_samples,
            'restored': False, 'resumed': False}


def collect_lifecycle_discard(trace, retained, engine, output, returncode, isolated=False, update=False, routes=False,
                              injected_input=False):
    """Only the private native FD is evidence; independently check both fatal logs."""
    evidence = lifecycle_discard_evidence([], returncode, isolated=isolated, update=update, routes=routes,
                                         injected_input=injected_input)
    evidence['native_fd_path'] = str(trace)
    evidence['retained_log'] = str(retained)
    try:
        with os.fdopen(os.open(trace, os.O_RDONLY | os.O_NOFOLLOW), 'rb') as stream:
            if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                raise OSError('native lifecycle trace is not a regular file')
            data = stream.read(1 << 20)
            if stream.read(1):
                raise OSError('native lifecycle trace exceeds 1 MiB')
        retained.write_bytes(data)
        evidence = lifecycle_discard_evidence(data.decode('utf-8').splitlines(keepends=True),
                                              returncode, strict=True, isolated=isolated, update=update, routes=routes,
                                              injected_input=injected_input)
        evidence.update(native_fd_path=str(trace), retained_log=str(retained),
                        log_sha256=hashlib.sha256(data).hexdigest())
        if not data.endswith(b'\n'):
            evidence['errors'].append('incomplete final native trace line')
    except (OSError, UnicodeError) as error:
        evidence['errors'].append(f'native lifecycle trace unavailable/invalid: {error}')
    for source in (engine, output):
        try:
            with source.open(errors='replace') as stream:
                evidence['errors'].extend(f'{source.name}: {line.strip()[:300]}'
                                          for line in stream if is_fatal_gate_message(line))
        except OSError as error:
            evidence['errors'].append(f'fatal log unavailable: {error}')
    evidence['pass'] = not evidence['errors']
    return evidence


DISCARD_TAIL_BYTES = 4096


def lifecycle_discard_terminal(path, *, network=False):
    """Inspect only an exited child's bounded tail; this never validates a run."""
    limit = (32 if network else 8) * 1024 * 1024
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(fd, 'rb') as stream:
            before = os.fstat(stream.fileno())
            if not stat.S_ISREG(before.st_mode):
                raise ValueError('trace is not a regular file')
            if not 0 < before.st_size <= limit:
                raise ValueError(f'trace is empty or exceeds {limit // (1024 * 1024)} MiB')
            offset = max(0, before.st_size - DISCARD_TAIL_BYTES)
            stream.seek(offset)
            tail = stream.read(DISCARD_TAIL_BYTES)
            after = os.fstat(stream.fileno())
            if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                raise ValueError('trace changed after child exit')
        if offset:
            _, separator, tail = tail.partition(b'\n')
            if not separator:
                raise ValueError('terminal receipt exceeds tail bound')
        lines = tail.splitlines(keepends=True)
        terminal = re.fullmatch(
            rb'X4LAN LIFECYCLE_DISCARD result=(pass|fail) reason=([A-Za-z0-9_]{1,128}) '
            rb'exit=223 restored=0 resumed=0\n', lines[-1] if lines else b'')
        if not terminal or sum(b'X4LAN LIFECYCLE_DISCARD result=' in line for line in lines) != 1:
            raise ValueError('missing, malformed, truncated or repeated terminal receipt in trace tail')
        return terminal[1].decode('ascii'), terminal[2].decode('ascii')
    except (OSError, ValueError) as error:
        raise RuntimeError(f'lifecycle discard terminal evidence unavailable: {error}') from error


def collect_cursor_observations(result, local, games, run_dir, enabled, manifest):
    """Bind cursor receipts to fully validated physics before compaction."""
    try:
        observations = {str(slot): cursor_bundle.collect(
            run_dir / f'game{slot}-stderr.log', profile / 'controller-lifecycle.log', enabled,
            result.get('physics_discard', {}).get('clients', {}).get(str(slot), {}), manifest)
            for slot, (_, profile) in zip(local, games)}
        result['player_cursor_active_flight_evidence'] = observations
        if ((enabled and set(observations) != {'1', '2'}) or
                any(not proof['pass'] for proof in observations.values())):
            result['pass'] = False
            result.get('physics_discard', {})['pass'] = False
    except Exception as error:
        result['player_cursor_active_flight_error'] = str(error)
        result['pass'] = False
        result.get('physics_discard', {})['pass'] = False


def collect_stock_view_observations(result, local, games, run_dir, enabled, *, rewrite=False):
    """Run after full physics validation, before successful proof compaction."""
    try:
        observations = {}
        for slot, (_, profile) in zip(local, games):
            observations[str(slot)] = stock_view_observer.collect(
                run_dir / f'game{slot}-stderr.log', profile / 'controller-lifecycle.log', enabled,
                result.get('physics_discard', {}).get('clients', {}).get(str(slot), {}), rewrite=rewrite)
        result['stock_view_fov_observer_evidence'] = observations
        if (((enabled or rewrite) and set(observations) != {'1', '2'}) or
                any(not proof['pass'] for proof in observations.values())):
            result['pass'] = False
            result.get('physics_discard', {})['pass'] = False
    except Exception as error:
        result['stock_view_fov_observer_error'] = str(error)
        result['pass'] = False
        result.get('physics_discard', {})['pass'] = False


def lifecycle_discard_wait(returncodes, traces, *, network=False):
    """A failure exits promptly; a success waits for its peer and full validation."""
    for slot, code in returncodes.items():
        if code is not None and code != 223:
            raise RuntimeError(f'client {slot}: lifecycle discard expected exit 223, got {code}')
    for slot, code in returncodes.items():
        if code == 223:
            if slot not in traces:
                raise RuntimeError(f'client {slot}: lifecycle discard trace path missing')
            try:
                verdict, reason = lifecycle_discard_terminal(traces[slot], network=network)
            except RuntimeError as error:
                raise RuntimeError(f'client {slot}: {error}') from error
            if verdict == 'fail':
                raise RuntimeError(f'client {slot}: lifecycle discard reported failure: {reason}')
    return bool(returncodes) and all(code == 223 for code in returncodes.values())


def collect_fire_observations(result, local, games, run_dir, active, manifest):
    """Keep raw receipts bound to expanded, accepted per-client physics evidence."""
    bounded = result.get('physics_discard', {})
    try:
        proofs = {str(slot): fire_observation.collect(
            run_dir / f'game{slot}-stderr.log',
            profile / 'controller-lifecycle.log', active,
            bounded.get('clients', {}).get(str(slot), {}), manifest)
            for slot, (_, profile) in zip(local, games)}
        result['player_fire_observation_evidence'] = proofs
        if ((active and set(proofs) != {'1', '2'}) or
                any(not proof['pass'] for proof in proofs.values())):
            result['pass'] = False
            bounded['pass'] = False
    except Exception as error:
        result['player_fire_observation_error'] = str(error)
        result['pass'] = False
        bounded['pass'] = False


def collect_semaphore_observations(result, local, cases_by_slot, manifest):
    """A required observer must cover every local client before physics passes."""
    bounded = result.get('physics_discard', {})
    try:
        expected = {str(slot) for slot in local}
        if (not expected or len(expected) != len(local) or
                {str(slot) for slot in cases_by_slot} != expected):
            raise ValueError('semaphore observer requires exactly the expected local slots')
        clients = bounded.get('clients', {})
        proofs = {str(slot): semaphore_bundle.collect(
            cases_by_slot[slot] / 'semaphore-observer.log', clients.get(str(slot)), manifest, True,
            ancestry=bounded.get('view_ancestry', {}).get(str(slot)))
            for slot in local}
        result['semaphore_observer_evidence'] = proofs
        if set(proofs) != expected or any(proof.get('pass') is not True for proof in proofs.values()):
            result['pass'] = False
            bounded['pass'] = False
    except Exception as error:
        result['semaphore_observer_error'] = str(error)
        result['pass'] = False
        bounded['pass'] = False


def bundle(args):
    try:
        args = argparse.Namespace(**resolve_session_options(vars(args), cli=True))
        view_wire_enabled = view_wire_bundle.enabled(vars(args))
        semaphore_observer_enabled = semaphore_bundle.enabled(vars(args))
        camera_refill_enabled = camera_refill_bundle.validate_options(vars(args))
        stock_view_enabled = stock_view_observer.enabled(vars(args))
        render_capture_enabled = guest_capture_bundle.enabled(vars(args))
        host_capture_enabled = input_observation_bundle.enabled(vars(args))
        projection_scope_enabled = projection_scope_bundle.enabled(vars(args))
        selection_parity_enabled = selection_parity_bundle.enabled(vars(args))
        cursor_enabled = cursor_bundle.enabled(vars(args))
        space_enabled = space_bundle.enabled(vars(args))
        shared_identity_enabled = shared_identity_bundle.validate_options(vars(args))
    except ValueError as error:
        raise SystemExit(str(error)) from error
    repo = Path(__file__).resolve().parents[2]
    caller_stride = args.runtime_id_caller_stride
    if caller_stride is not None and (not 1 <= caller_stride <= 3600 or
            not args.runtime_id_callers or not args.runtime_ids):
        raise SystemExit('--runtime-id-caller-stride requires --runtime-id-callers, --runtime-ids and 1..3600')
    independent_frames = validate_independent_keyboard(vars(args), args.ticks)
    validate_rolling_leaf_ticks(args.world_rolling_leaf_ticks, args.world_rolling_slices, args.ticks)
    validate_rolling_raw_ids(args.world_rolling_raw_ids, args.world_rolling_leaf_ticks)
    validate_external_camera(vars(args), independent_frames)
    presentation_enabled = validate_presentation_probe(vars(args), independent_frames)
    view_query_enabled = validate_view_query_observer(vars(args), independent_frames)
    ancestry_enabled = ancestry_bundle.validate_options(vars(args), bool(independent_frames))
    component_trace_enabled = component_trace_bundle.enabled(vars(args), ancestry_enabled)
    mouse_trace_enabled = mouse_trace.enabled(vars(args))
    precompare_enabled = precompare_trace.enabled(vars(args), component_trace_enabled, ancestry_enabled, bool(independent_frames))
    fire_observation_enabled = fire_observation.enabled(vars(args))
    if ancestry_enabled and not presentation_enabled:
        raise SystemExit('VIEW ancestry requires presentation identity evidence')
    ui_overlay_enabled = validate_ui_overlay(vars(args), independent_frames)
    overlay_sources = ui_overlay_sources(repo) if ui_overlay_enabled else []
    if args.player_ui_session_frames:
        overlay_sources.extend(session_sources(repo))
    plan_kick_enabled = validate_plan_kick(vars(args))
    if plan_kick_enabled:
        overlay_sources.extend(plan_kick_sources(repo))
    seat_query_enabled = validate_seat_query(vars(args))
    if seat_query_enabled:
        overlay_sources.extend(seat_query_sources(repo))
    render_lease_enabled = validate_render_lease(vars(args))
    if render_lease_enabled:
        overlay_sources.extend(render_lease_sources(repo))
    seat_correction_enabled = validate_seat_correction(vars(args))
    if seat_correction_enabled:
        overlay_sources.extend(seat_correction_sources(repo))
    live_pose_enabled = validate_live_pose_observer(vars(args))
    if live_pose_enabled:
        overlay_sources.extend(live_pose_observer_sources(repo))
    fork_enabled = validate_presentation_fork(vars(args), independent_frames)
    fork_sources = presentation_fork_sources(repo) if fork_enabled else []
    ui_callback_enabled = validate_ui_callback_observer(vars(args), independent_frames)
    view_sources = view_query_sources(repo) if view_query_enabled else []
    throttle_enabled = validate_throttle_probe(vars(args), independent_frames)
    throttle_sources = throttle_probe_sources(repo) if throttle_enabled else []
    presentation_sources = presentation_probe_sources(repo) if presentation_enabled else []
    if args.player_external_camera_probe:
        camera_sources = ('gate_player_camera_probe.h', 'player_camera_probe.h',
                          'gate_player_camera_id_scope.h', 'gate_player_camera_context_anchor.h',
                          'input_context_census.h')
        if not all((repo / 'src/native/engine/linux' / name).is_file() for name in camera_sources):
            raise SystemExit('external camera probe requires all native camera headers')
    if args.player_control_boundary_probe:
        validate_control_boundary_probe(args.stock_keyboard_observer, args.serial_pathing, args.ticks,
            (args.player_control_bank_probe, args.player_factory_probe, args.player_reference_probe,
             args.stock_keyboard_replay_probe, args.player_seat_preflight))
    if args.player_control_bank_probe:
        validate_control_bank_probe(args.stock_keyboard_observer, args.serial_pathing, args.ticks,
                                    args.stock_keyboard_replay_probe, args.player_reference_probe)
    if args.player_seat_preflight:
        validate_seat_preflight(args.player_factory_probe, args.player_factory_retain_until,
                                args.stock_keyboard_observer, args.serial_pathing, args.ticks)
    if args.player_control_bank_census:
        if not args.stock_keyboard_observer or not args.serial_pathing or args.ticks < 4:
            raise SystemExit('--player-control-bank-census requires observer, serial pathing and at least four ticks')
        if args.stock_keyboard_replay_probe:
            raise SystemExit('run the control-bank census separately from the keyboard replay probe')
    validate_factory_retention(args.player_factory_retain_until, args.player_factory_probe, args.ticks)
    if args.player_factory_probe and (not args.stock_keyboard_observer or not args.serial_pathing or args.ticks < 3):
        raise SystemExit('--player-factory-probe requires the observer, serial pathing and at least three ticks')
    if args.player_factory_probe and (args.player_reference_probe or (args.stock_keyboard_replay_probe and not independent_frames)):
        raise SystemExit('run the player factory probe separately from other player experiments')
    if args.player_reference_probe and (not args.stock_keyboard_observer or not args.serial_pathing or args.ticks < 3):
        raise SystemExit('--player-reference-probe requires the observer, serial pathing and at least three ticks')
    if args.player_reference_probe and args.stock_keyboard_replay_probe:
        raise SystemExit('run the player reference and keyboard replay probes separately')
    replay_last_capture = None
    if args.stock_keyboard_replay_probe:
        if not args.stock_keyboard_observer or args.clients != 2:
            raise SystemExit('--stock-keyboard-replay-probe requires --stock-keyboard-observer and --clients 2')
        # Linux overlays are bare and expose no lookahead option: unset means 0
        # in the probe. Leave lookahead + 2 ticks to drain delayed captures.
        lookahead = 0
        replay_last_capture = ((budget_from_options(vars(args)) or {}).get('last_capture', independent_frames + 2)
                               if independent_frames else args.ticks - (lookahead + 2))
        if replay_last_capture < 3:
            raise SystemExit('--stock-keyboard-replay-probe requires at least 5 ticks')
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
    if args.world_rolling_leaf_ticks is not None:
        command += ['--world-rolling-leaf-ticks', *map(str, args.world_rolling_leaf_ticks)]
    if args.world_rolling_raw_ids is not None:
        command += ['--world-rolling-raw-ids', *args.world_rolling_raw_ids]
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
    if args.frame_timing:
        command.append('--frame-timing')
    if args.stock_keyboard_observer:
        command.append('--stock-keyboard-observer')
    if args.player_control_bank_census:
        command.append('--player-control-bank-census')
    if args.player_control_bank_probe:
        command.append('--player-control-bank-probe')
    if args.player_control_boundary_probe:
        command.append('--player-control-boundary-probe')
    if args.player_seat_preflight:
        command.append('--player-seat-preflight')
    if args.player_factory_probe:
        command.append('--player-factory-probe')
    if args.player_factory_retain_until is not None:
        command.extend(('--player-factory-retain-until', str(args.player_factory_retain_until)))
    if args.player_reference_probe:
        command.append('--player-reference-probe')
    if args.seed_rng:
        command.append('--seed-rng')
    if args.stock_keyboard_replay_probe:
        command += ['--stock-keyboard-replay-probe',
                    '--stock-keyboard-replay-last-capture', str(replay_last_capture)]
    if independent_frames:
        command += ['--independent-keyboard-frames', str(independent_frames)]
    if view_query_enabled:
        command.append('--player-view-query-observer')
    if args.player_ui_session_frames:
        command += ['--player-ui-session-frames', str(args.player_ui_session_frames)]
    elif ui_overlay_enabled:
        command.append('--player-ui-overlay-probe')
        if args.player_ui_overlay_callbacks != 1:
            command += ['--player-ui-overlay-callbacks', str(args.player_ui_overlay_callbacks)]
    if args.player_startup_phases:
        command += ['--player-startup-phases', args.player_startup_phases]
    if plan_kick_enabled:
        command.append('--player-ui-plan-kick-probe')
    if seat_query_enabled:
        command.append('--player-ui-seat-query-probe')
    if live_pose_enabled:
        command.append('--player-ui-live-pose-observer')
    if seat_correction_enabled:
        command.append('--player-ui-seat-correction')
    if camera_refill_enabled:
        command.append('--player-ui-camera-refill')
    if render_lease_enabled:
        command.append('--player-ui-render-lease')
    if fork_enabled:
        command.append('--player-presentation-fork-probe')
    if ui_callback_enabled:
        command.append('--ui-callback-observer')
    if args.player_external_camera_probe:
        command.append('--player-external-camera-probe')
    if presentation_enabled:
        command.append('--player-presentation-probe')
    if args.clients > 2 or args.stock_keyboard_replay_probe:
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
    if caller_stride is not None:
        command += ['--runtime-id-caller-stride', str(caller_stride)]
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
    if args.avoid_angle:
        command.append('--avoid-angle')
    if args.pursuit_speed:
        command.append('--pursuit-speed')
    if args.probes_diag:
        command.append('--probes-diag')
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
    overlay, shared_identity_metadata = shared_identity_bundle.prepare_overlay(
        repo, overlay, work / 'shared-identity-overlay', vars(args))
    extension = out / 'extension/x4_lan_lab'
    compose_extension_template(overlay, extension, args.extension_base)
    if view_query_enabled:
        subprocess.run([sys.executable, str(repo / 'tools/dev/build_player_view_query_preload.py'),
                        '--zig', str(args.zig)], check=True)
        (out / 'player_view_query').mkdir()
        shutil.copy2(repo / '.local/player-view-query' / PLAYER_VIEW_QUERY,
                     out / 'player_view_query' / PLAYER_VIEW_QUERY)
    if args.portable_math:
        subprocess.run([sys.executable, str(repo / 'tools/dev/build_portable_math_preload.py'), '--zig', str(args.zig)], check=True)
        (out / 'portable_math').mkdir()
        shutil.copy2(repo / '.local/portable-math/preload' / PORTABLE_MATH, out / 'portable_math' / PORTABLE_MATH)
    if args.player_lifetime_preload:
        validate_lifecycle_discard(vars(args), args.ticks)
        subprocess.run([sys.executable, str(repo / 'tools/dev/build_player_lifetime_preload.py'),
                        '--zig', str(args.zig)], check=True)
        (out / 'player_lifetime').mkdir()
        shutil.copy2(repo / '.local/player-lifetime' / PLAYER_LIFETIME, out / 'player_lifetime' / PLAYER_LIFETIME)
    if semaphore_observer_enabled:
        subprocess.run([sys.executable, str(repo / 'tools/dev/build_linux_semaphore_observer.py'),
                        '--zig', str(args.zig), '--out', str(out / 'semaphore_observer')], check=True)
    missing = [name for name in ('ui/native_gate_probe.lua', 'ui/sha256.lua') if not (extension / name).is_file()]
    if missing:
        raise SystemExit(f'the composed lockstep extension lacks required files: {missing}')
    config = (extension / 'ui/config.lua').read_text()
    validate_view_query_config(config, view_query_enabled)
    validate_ui_callback_config(config, ui_callback_enabled)
    validate_presentation_fork_config(config, fork_enabled)
    validate_ui_overlay_config(config, ui_overlay_enabled, args.player_ui_overlay_callbacks)
    validate_plan_kick_config(config, plan_kick_enabled)
    validate_seat_query_config(config, seat_query_enabled)
    validate_live_pose_observer_config(config, live_pose_enabled)
    validate_seat_correction_config(config, seat_correction_enabled)
    validate_render_lease_config(config, render_lease_enabled)
    camera_refill_bundle.validate_config(config, camera_refill_enabled)
    validate_session_config(config, args.player_ui_session_frames, args.player_startup_phases)
    leaf_assignment = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_world_rolling_leaf_ticks\s*=\s*(.*?)\s*$', config)
    expected_leaf = [] if args.world_rolling_leaf_ticks is None else [
        '{%d,%d}' % tuple(args.world_rolling_leaf_ticks)]
    if leaf_assignment != expected_leaf:
        raise SystemExit('composed rolling leaf config does not match the frozen option')
    raw_assignment = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_world_rolling_raw_ids\s*=\s*(.*?)\s*$', config)
    expected_raw = [] if args.world_rolling_raw_ids is None else [
        '{' + ','.join('"' + value + '"' for value in args.world_rolling_raw_ids) + '}']
    if raw_assignment != expected_raw:
        raise SystemExit('composed rolling raw config does not match the frozen option')
    validate_external_camera_config(config, args.player_external_camera_probe)
    validate_presentation_config(config, presentation_enabled)
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
    shutil.copy2(Path(__file__).with_name('linux_ui_seat_pose_evidence.py'), out / 'linux_ui_seat_pose_evidence.py')
    shutil.copy2(Path(__file__).with_name('linux_ui_plan_kick_evidence.py'), out / 'linux_ui_plan_kick_evidence.py')
    shutil.copy2(Path(__file__).with_name('linux_ui_seat_query_evidence.py'), out / 'linux_ui_seat_query_evidence.py')
    shutil.copy2(Path(__file__).with_name('linux_ui_live_pose_observer_evidence.py'), out / 'linux_ui_live_pose_observer_evidence.py')
    shutil.copy2(Path(__file__).with_name('linux_ui_pose_scope_evidence.py'), out / 'linux_ui_pose_scope_evidence.py')
    shutil.copy2(Path(__file__).with_name('linux_ui_seat_correction_evidence.py'), out / 'linux_ui_seat_correction_evidence.py')
    shutil.copy2(Path(__file__).with_name('linux_ui_render_lease_evidence.py'), out / 'linux_ui_render_lease_evidence.py')
    shutil.copy2(Path(__file__).with_name('linux_player_startup_phases.py'), out / 'linux_player_startup_phases.py')
    shutil.copy2(Path(__file__).with_name('linux_ui_session_budget.py'), out / 'linux_ui_session_budget.py')
    shutil.copy2(Path(__file__).with_name('linux_ui_session_evidence.py'), out / 'linux_ui_session_evidence.py')
    shutil.copy2(Path(__file__).with_name('linux_gate_evidence.py'), out / 'linux_gate_evidence.py')
    shutil.copy2(Path(__file__).with_name('linux_player_physics_evidence.py'), out / 'linux_player_physics_evidence.py')
    frames_helper = Path(__file__).with_name('linux_player_frames_evidence.py')
    if frames_helper.is_file():
        shutil.copy2(frames_helper, out / frames_helper.name)
        context_helper = Path(__file__).with_name('linux_player_context_evidence.py')
        if not context_helper.is_file():
            raise SystemExit('recurring physics evidence requires its context policy collector')
        shutil.copy2(context_helper, out / context_helper.name)
        presentation_helper = Path(__file__).with_name('linux_player_presentation_evidence.py')
        if not presentation_helper.is_file():
            raise SystemExit('recurring physics evidence requires its presentation collector')
        shutil.copy2(presentation_helper, out / presentation_helper.name)
    for helper in ('world_motion_evidence.py', 'native_world_evidence.py', 'economy_watch_config.py', 'navmesh_post_config.py'):
        shutil.copy2(Path(__file__).with_name(helper), out / helper)
    camera_helper = Path(__file__).with_name('linux_player_camera_evidence.py')
    if camera_helper.is_file():
        shutil.copy2(camera_helper, out / camera_helper.name)
    elif args.player_external_camera_probe:
        raise SystemExit('external camera probe requires its strict camera evidence collector')
    network_helper = Path(__file__).with_name('linux_player_network_evidence.py')
    if network_helper.is_file():
        shutil.copy2(network_helper, out / network_helper.name)
    elif independent_frames:
        raise SystemExit('independent keyboard physics requires the network evidence collector')
    if independent_frames or args.player_lifetime_preload or (args.stock_keyboard_observer and args.serial_pathing):
        # Freeze the actual helper/header sources alongside the compiled module.
        source_paths = []
        if args.stock_keyboard_observer and args.serial_pathing:
            source_paths.extend(recurring_diagnostic_sources(repo))
            for name in ('player_context_policy.h', 'gate_player_context_policy_adapter.h',
                         'input_context_census.h', 'gate_player_camera_context_anchor.h'):
                source = repo / 'src/native/engine/linux' / name
                if not source.is_file():
                    raise SystemExit('recurring context policy requires ' + name)
                source_paths.append(source)
        if args.player_lifetime_preload:
            source_paths.extend(lifetime_preload_sources(repo))
        if independent_frames:
            source_paths.extend((repo / 'src/native/engine/linux').glob('gate_player_*.h'))
        network_header = repo / 'src/native/engine/linux/gate_player_physics_network.h'
        if independent_frames and not network_header.is_file():
            raise SystemExit('independent keyboard physics requires gate_player_physics_network.h')
        if args.player_external_camera_probe:
            camera_header = repo / 'src/native/engine/linux/gate_player_camera_probe.h'
            camera_core = repo / 'src/native/engine/linux/player_camera_probe.h'
            camera_scope = repo / 'src/native/engine/linux/gate_player_camera_id_scope.h'
            camera_census = repo / 'src/native/engine/linux/input_context_census.h'
            camera_anchor = repo / 'src/native/engine/linux/gate_player_camera_context_anchor.h'
            if not all(path.is_file() for path in (camera_header, camera_core, camera_scope, camera_census, camera_anchor)):
                raise SystemExit('external camera probe requires all native camera headers')
            source_paths.extend((camera_core, camera_census, camera_anchor))
        source_paths.extend(view_sources)
        if ui_callback_enabled:
            source_paths.append(repo / 'src/native/engine/linux/gate_linux.c')
        source_paths.extend(throttle_sources)
        source_paths.extend(presentation_sources)
        source_paths.extend(fork_sources)
        source_paths.extend(overlay_sources)
        source_paths += [repo / 'tools/dev' / name for name in
                         ('linux_gate_live.py', 'build_native_gate_overlay.py', 'native_gate_probe.lua',
                          'native_gate_linux_keyboard.lua')]
        freeze_source_receipts(repo, out, source_paths)
    if args.world_rolling_leaf_ticks is not None:
        leaf_sources = (repo / 'src/native/engine/linux/gate_world.h', repo / 'src/native/engine/linux/gate_linux.c',
                        repo / 'src/native/engine/linux/gate_world_digest_cache.h',
                        repo / 'src/native/engine/linux/gate_world_classes.h',
                        repo / 'src/native/engine/linux/gate_world_created.h',
                        repo / 'src/native/engine/lockstep_world_nav.h', repo / 'src/native/engine/lockstep_sha256.h',
                        repo / 'tools/dev/native_gate_probe.lua', repo / 'tools/dev/build_native_gate_overlay.py',
                        repo / 'tools/dev/linux_gate_live.py')
        freeze_source_receipts(repo, out, leaf_sources)
    if shared_identity_enabled:
        freeze_source_receipts(repo, out, shared_identity_bundle.sources(repo))
    for helper in shared_identity_bundle.BUNDLE_HELPERS:
        shutil.copy2(repo / 'tools/dev' / helper, out / helper)
    freeze_source_receipts(repo, out, cursor_bundle.source_paths(repo))
    shutil.copy2(repo / 'tools/dev/linux_player_cursor_bundle.py', out / 'linux_player_cursor_bundle.py')
    freeze_source_receipts(repo, out, semaphore_bundle.source_paths(repo))
    for helper in semaphore_bundle.HELPERS:
        shutil.copy2(repo / "tools/dev" / helper, out / helper)
    for helper in space_bundle.HELPERS:
        shutil.copy2(repo / "tools/dev" / helper, out / helper)
    freeze_source_receipts(repo, out, component_trace_bundle.source_paths(repo))
    for helper in component_trace_bundle.HELPERS:
        shutil.copy2(repo / 'tools/dev' / helper, out / helper)
    freeze_source_receipts(repo, out, stock_view_observer.source_paths(repo))
    shutil.copy2(repo / 'tools/dev/linux_stock_view_fov_observer.py', out / 'linux_stock_view_fov_observer.py')
    freeze_source_receipts(repo, out, camera_refill_bundle.source_paths(repo))
    for helper in camera_refill_bundle.HELPERS:
        shutil.copy2(repo / 'tools/dev' / helper, out / helper)
    freeze_source_receipts(repo, out, ancestry_bundle.source_paths(repo))
    for helper in ('linux_view_ancestry_bundle.py', 'linux_view_ancestry_evidence.py'):
        shutil.copy2(repo / 'tools/dev' / helper, out / helper)
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
        'world_rolling_leaf_ticks', 'world_rolling_raw_ids',
        'status_stride',
        'probe_profile',
        'frame_timing',
        'player_control_bank_census',
        'player_control_bank_probe',
        'player_control_boundary_probe',
        'player_seat_preflight',
        'stock_keyboard_observer',
        'stock_keyboard_replay_probe',
        'player_reference_probe', 'player_factory_probe', 'player_factory_retain_until',
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
        'player_lifetime_sha256': files.get(f'player_lifetime/{PLAYER_LIFETIME}'),
        'player_view_query_observer': view_query_enabled,
        'view_query_source_version': semaphore_bundle.SOURCE_VERSION,
        'semaphore_observer': semaphore_observer_enabled,
        'semaphore_observer_evidence_version': 1 if semaphore_observer_enabled else None,
        'semaphore_observer_sha256': files.get('semaphore_observer/' + semaphore_bundle.PROVIDER),
        'semaphore_observer_size': (out / 'semaphore_observer' / semaphore_bundle.PROVIDER).stat().st_size if semaphore_observer_enabled else None,
        'player_view_wire': view_wire_enabled,
        'player_view_wire_evidence_version': 1 if view_wire_enabled else None,
        'player_selection_parity': selection_parity_enabled,
        'player_selection_parity_evidence_version': 1 if selection_parity_enabled else None,
        'player_fire_observation': fire_observation_enabled,
        'player_fire_observation_evidence_version': 2 if fire_observation_enabled else None,
        'cursor_input_trace': mouse_trace_enabled,
        'cursor_input_trace_evidence_version': 1 if mouse_trace_enabled else None,
        'view_precompare_trace': precompare_enabled,
        'view_precompare_trace_evidence_version': 1 if precompare_enabled else None,
        'player_space_context': space_enabled,
        'player_space_context_evidence_version': 1 if space_enabled else None,
        'player_cursor_active_flight': cursor_enabled,
        'player_cursor_active_flight_evidence_version': 1 if cursor_enabled else None,
        'view_render_capture_shadow': render_capture_enabled,
        'view_host_capture_shadow': host_capture_enabled,
        'player_ui_projection_fov_scope': projection_scope_enabled,
        'stock_view_fov_observer': stock_view_enabled,
        'stock_view_fov_observer_evidence_version': 1 if stock_view_enabled else None,
        'view_ancestry_observer': ancestry_enabled,
        'player_ui_camera_refill': camera_refill_enabled,
        'player_ui_camera_refill_evidence_version': 1 if camera_refill_enabled else None,
        'view_component_trace': component_trace_enabled,
        'view_component_trace_evidence_version': 1 if component_trace_enabled else None,
        'ui_callback_observer': ui_callback_enabled,
        'player_presentation_fork_probe': fork_enabled,
        'player_ui_plan_kick_probe': plan_kick_enabled,
        'player_ui_seat_query_probe': seat_query_enabled,
        'player_ui_live_pose_observer': live_pose_enabled,
        'player_ui_seat_correction': seat_correction_enabled,
        'player_ui_render_lease': render_lease_enabled,
        'player_ui_session_frames': args.player_ui_session_frames,
        'player_startup_phases': args.player_startup_phases,
        'session_disk_limit': (budget_from_options(vars(args)) or {}).get('disk_limit'),
        'player_ui_render_lease_evidence_version': 1 if render_lease_enabled else None,
        'player_ui_seat_correction_evidence_version': 1 if seat_correction_enabled else None,
        'player_ui_live_pose_observer_evidence_version': 2 if live_pose_enabled else None,
        'player_ui_overlay_probe': ui_overlay_enabled,
        'player_ui_overlay_callbacks': args.player_ui_overlay_callbacks,
        'player_view_query_sha256': files.get(f'player_view_query/{PLAYER_VIEW_QUERY}'),
        'player_view_query_size': (out / 'player_view_query' / PLAYER_VIEW_QUERY).stat().st_size if view_query_enabled else None,
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
    if caller_stride is not None:
        manifest['runtime_id_caller_stride'] = caller_stride
    manifest['economy_watch'] = args.economy_watch
    manifest['economy_watch_window'] = args.economy_watch_window
    manifest['world_motion_watch'] = args.world_motion_watch
    manifest['world_motion_window'] = args.world_motion_window
    manifest['stock_keyboard_replay_last_capture'] = replay_last_capture
    manifest['independent_keyboard_frames'] = independent_frames
    manifest['player_external_camera_probe'] = bool(args.player_external_camera_probe)
    manifest['player_presentation_probe'] = presentation_enabled
    manifest['player_throttle_probe'] = bool(args.player_throttle_probe)
    manifest['player_throttle_warm_probe'] = bool(args.player_throttle_warm_probe)
    manifest['player_throttle_start_tick'] = (6 if args.player_throttle_warm_probe else
                                               5 if throttle_enabled else None)
    manifest['player_throttle_environment'] = (
        'state13-warm-discard' if args.player_throttle_warm_probe else
        'state13-discard' if throttle_enabled else None)
    manifest['independent_keyboard_mode'] = 'independent_keyboard' if independent_frames else None
    manifest['created_rows'] = bool(args.created_rows)
    manifest['snapshot_seed'] = world_seed
    manifest['preset'] = args.preset
    manifest['tick_hz'] = args.tick_hz or 60  # the clients' native_gate_tick_hz; the relay runs with the same
    manifest['mixed_platform'] = bool(args.mixed_platform)  # MIXED_PLATFORM_SETS applied (tools/build_play_overlay.py)
    manifest.update(shared_identity_metadata)
    shared_identity_bundle.validate_bundle(out, manifest)
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
            if b'PLAYER_CONTROL_RECURRING_FATAL' in line and self.failure is None:
                self.failure = line.decode(errors='replace')[:300]
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


def case_config(text, port, token, slot, clients, credential=None, relay_host=None, arm_at=None):
    """A client's config.lua: the session's port, token, slot and size; with wire crypto (bundle --wire-crypto) the slot's
    credential (native_gate_crypto.py credential for this token and slot; a secret, never logged)."""
    relay_host, arm_at = remote_session_options(relay_host, arm_at, external=True)
    wants = WIRE_CRYPTO_TOKEN in text
    if wants and not credential:
        raise RuntimeError(f'the bundle has wire crypto: slot {slot} needs a credential (run --wire-crypto-credentials)')
    if credential and not wants:
        raise RuntimeError('a wire crypto credential, but the bundle has no wire crypto line (bundle --wire-crypto)')
    if credential and not CREDENTIAL.fullmatch(credential):
        raise RuntimeError(f'slot {slot}: not a wire crypto credential (x4c1:<64 hex>:<64 hex>)')
    text = (text.replace('@X4LANE_PORT@', str(port)).replace('@X4LANE_TOKEN@', token)
            .replace('@X4LANE_SLOT@', f'p{slot}').replace('@X4LANE_CLIENTS@', str(clients)))
    text = text.replace(WIRE_CRYPTO_TOKEN, credential or '')
    if relay_host is not None or arm_at is not None:
        text += '\n'
        if relay_host is not None:
            text += f"X4LAN_CONFIG.native_gate_relay_host = '{relay_host}'\n"
        if arm_at is not None:
            text += f'X4LAN_CONFIG.native_gate_arm_at = {arm_at}\n'
    return text


def remote_session_options(host, arm_at, external, now=None):
    """Validate remote settings before inserting them into private Lua config."""
    if (host is not None or arm_at is not None) and not external:
        raise ValueError('--relay-host and --arm-at require an external relay')
    if host is not None:
        if not isinstance(host, str):
            raise ValueError('--relay-host must be a dotted IPv4 address')
        try:
            host = str(ipaddress.IPv4Address(host))
        except ipaddress.AddressValueError:
            raise ValueError('--relay-host must be a dotted IPv4 address') from None
    if arm_at is not None:
        if type(arm_at) is not int or arm_at <= 0 or arm_at <= (time.time() if now is None else now):
            raise ValueError('--arm-at must be a future integer UTC second')
    return host, arm_at


def load_credentials(path):
    """--wire-crypto-credentials FILE: JSON {"<slot>": "x4c1:...", ...}, readable by its owner only (it is a secret)."""
    if os.name == 'posix' and path.stat().st_mode & 0o077:
        raise SystemExit(f'{path}: a credential file must not be readable by others (chmod 600)')
    data = json.loads(path.read_text())
    if not isinstance(data, dict) or not data or any(not str(k).isdigit() or not isinstance(v, str) or not CREDENTIAL.fullmatch(v)
                                                     for k, v in data.items()):
        raise SystemExit(f'{path}: expected {{"<slot>": "x4c1:<64 hex>:<64 hex>", ...}}')
    return {int(k): v for k, v in data.items()}


def stage_case(root, run, slot, game, userhome, port, token, clients=2, credential=None, snapshot=True,
               relay_host=None, arm_at=None):
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
    config.write_text(case_config(config.read_text(), port, token, slot, clients, credential, relay_host, arm_at))
    if credential:
        config.chmod(0o600)
    ids = sorted(p.name for p in (case / 'extensions').iterdir())
    if any(not (n.startswith('ego_dlc_') or n == 'x4_lan_lab') for n in ids):
        raise RuntimeError('unexpected extension in the private view')
    (profile / 'content.xml').write_text('<?xml version="1.0"?><content>' + ''.join(
        f'<extension id="{n}" enabled="true" />' for n in ids) + '</content>\n')
    if (root / 'player_view_query' / PLAYER_VIEW_QUERY).is_file():
        shutil.copyfile(root / 'player_view_query' / PLAYER_VIEW_QUERY, case / PLAYER_VIEW_QUERY)
    if (root / 'player_lifetime' / PLAYER_LIFETIME).is_file():
        shutil.copyfile(root / 'player_lifetime' / PLAYER_LIFETIME, case / PLAYER_LIFETIME)
    if (root / 'semaphore_observer' / semaphore_bundle.PROVIDER).is_file():
        shutil.copyfile(root / 'semaphore_observer' / semaphore_bundle.PROVIDER, case / semaphore_bundle.PROVIDER)
        shutil.copyfile(root / 'exec_linux_semaphore_observer.py', case / 'exec_linux_semaphore_observer.py')
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


def retain_gate_log(source, retained, extracted, writers_closed):
    """Extract the gate stream from the engine log after owned writers exit.

    The process stdout log is a separate stream and may end mid-line during
    teardown. Refuse to publish or validate a potentially incomplete snapshot.
    """
    if not writers_closed:
        raise RuntimeError('cannot retain engine log while owned writers may still run')
    if not source.is_file():
        raise RuntimeError(f'engine log missing: {source}')
    shutil.copyfile(source, retained)
    with extracted.open('w', encoding='utf-8') as output:
        for line in gate_lines(retained):
            marker = line.find('[X4LAN] NATIVE_GATE ')
            if marker >= 0:
                output.write(line[marker:] + '\n')


def completed_client_evidence(engine_log, process_output):
    """Keep engine records and fatal messages written directly to stderr.

    X4 can repeat ordinary engine messages on stdout. Only fatal records from
    that separate retained stream are added, so duplicated progress cannot be
    mistaken for a second native capture. A fatal always invalidates the run.
    """
    yield from gate_lines(engine_log)
    if process_output.is_file():
        for line in gate_lines(process_output):
            if is_fatal_gate_message(line):
                yield line


def collect_run_evidence(run_dir, slots, cases, ticks, manifest, writers_closed):
    """Retain both logs and installed config, then validate the complete evidence."""
    result = {}
    logs_by_slot = {}
    for slot, (case, profile, *_rest) in zip(slots, cases):
        client = run_dir / f'client{slot}'
        (client / 'installed').mkdir(parents=True, exist_ok=True)
        log = profile / 'engine-output.log'
        retained_log = client / 'engine-output.log'
        retain_gate_log(log, retained_log, run_dir / f'game{slot}.log', writers_closed)
        shutil.copyfile(case / 'extensions/x4_lan_lab/ui/config.lua', client / 'installed/x4_lan_lab-config.lua')
        if (case / 'portable-math.log').is_file():
            shutil.copyfile(case / 'portable-math.log', client / 'portable-math.log')
        process_output = run_dir / f'game{slot}-stdout.log'
        needs_process_output = (manifest.get('player_control_bank_probe') or
                                manifest.get('player_control_boundary_probe'))
        if needs_process_output and not process_output.is_file():
            probe_name = 'control-bank' if manifest.get('player_control_bank_probe') else 'control-boundary'
            raise RuntimeError(f'{probe_name} process output missing: {process_output}')
        logs_by_slot[slot] = completed_client_evidence(retained_log, process_output)
    result.update(validate_evidence(logs_by_slot, ticks, manifest['stride'],
                                    rolling_slices=manifest.get('world_rolling_slices'),
                                    motion_watch=manifest.get('world_motion_watch'),
                                    motion_window=manifest.get('world_motion_window'),
                                    player_control_bank_probe=manifest.get('player_control_bank_probe', False),
                                    player_control_boundary_probe=manifest.get('player_control_boundary_probe', False)))
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
    try:
        shared_identity_bundle.validate_bundle(root, manifest)
    except (ValueError, OSError) as error:
        raise SystemExit('shared identity bundle refused: ' + str(error)) from error
    ticks = manifest['ticks'] if args.ticks is None else args.ticks
    validate_rolling_leaf_ticks(manifest.get('world_rolling_leaf_ticks'), manifest.get('world_rolling_slices'), ticks)
    validate_rolling_raw_ids(manifest.get('world_rolling_raw_ids'), manifest.get('world_rolling_leaf_ticks'))
    input_lifecycle = getattr(args, 'controller_lifecycle_input_discard', False)
    physics = getattr(args, 'controller_physics_discard', False)
    physics_frames = getattr(args, 'controller_physics_frames', 1)
    network_physics = validate_network_physics(manifest, physics, physics_frames, ticks)
    session = budget_from_options(manifest)
    if session:
        resolved = resolve_session_options(manifest)
        if any(manifest.get(key) != resolved[key] for key in
               ('independent_keyboard_frames','player_ui_overlay_probe','player_ui_overlay_callbacks')):
            raise SystemExit('noncanonical frozen session budget')
        if manifest.get('session_disk_limit') != session['disk_limit']:
            raise SystemExit('session disk limit differs from frozen budget')
        for source in SESSION_SOURCES:
            receipt = 'source-receipts/'+source
            if receipt not in manifest.get('files', {}) or not (root/receipt).is_file():
                raise SystemExit('session mode requires all frozen budget receipts')
        if shutil.disk_usage(root).free < session['disk_limit']+(512 << 20):
            raise SystemExit('session mode requires its disk budget plus 512 MiB free reserve')
    view_query_enabled = validate_view_query_bundle(root, manifest, network_physics)
    ancestry_enabled = semaphore_bundle.validate_bundle(root, manifest, network_physics)
    semaphore_observer_enabled = semaphore_bundle.enabled(manifest)
    view_wire_enabled = view_wire_bundle.enabled(manifest)
    fire_observation_enabled = fire_observation.enabled(manifest)
    space_enabled = space_bundle.enabled(manifest)
    component_trace_enabled = component_trace_bundle.enabled(manifest, ancestry_enabled)
    mouse_trace_enabled = mouse_trace.enabled(manifest)
    precompare_enabled = precompare_trace.enabled(manifest, component_trace_enabled, ancestry_enabled, network_physics)
    cursor_enabled = cursor_bundle.enabled(manifest)
    stock_view_enabled = stock_view_observer.enabled(manifest)
    render_capture_enabled = guest_capture_bundle.enabled(manifest)
    host_capture_enabled = input_observation_bundle.enabled(manifest)
    projection_scope_enabled = projection_scope_bundle.enabled(manifest)
    selection_parity_enabled = selection_parity_bundle.enabled(manifest)
    camera_refill_enabled = camera_refill_bundle.validate_options(manifest)
    camera_refill_bundle.validate_config(
        (root / 'extension/x4_lan_lab/ui/config.lua').read_text(), camera_refill_enabled)
    if ancestry_enabled and manifest.get('player_presentation_probe') is not True:
        raise SystemExit('VIEW ancestry requires presentation identity evidence')
    ui_overlay_enabled = validate_ui_overlay(manifest, manifest.get('independent_keyboard_frames', 0))
    if ui_overlay_enabled:
        if not network_physics:
            raise SystemExit('UI overlay requires disposable independent network physics')
        for source in UI_OVERLAY_SOURCES:
            receipt = 'source-receipts/' + source
            if receipt not in manifest.get('files', {}) or not (root / receipt).is_file():
                raise SystemExit('UI overlay requires its frozen source receipts')
    plan_kick_enabled = validate_plan_kick(manifest)
    if plan_kick_enabled:
        for source in PLAN_KICK_SOURCES:
            receipt = 'source-receipts/' + source
            if receipt not in manifest.get('files', {}) or not (root / receipt).is_file():
                raise SystemExit('plan-kick requires its frozen source receipts')
        if ('linux_ui_plan_kick_evidence.py' not in manifest.get('files', {}) or
                not (root / 'linux_ui_plan_kick_evidence.py').is_file()):
            raise SystemExit('plan-kick requires its frozen evidence collector')
    seat_query_enabled = validate_seat_query(manifest)
    if seat_query_enabled:
        for source in SEAT_QUERY_SOURCES:
            receipt = 'source-receipts/' + source
            if receipt not in manifest.get('files', {}) or not (root / receipt).is_file():
                raise SystemExit('seat-query requires its frozen source receipts')
        if ('linux_ui_seat_query_evidence.py' not in manifest.get('files', {}) or
                not (root / 'linux_ui_seat_query_evidence.py').is_file()):
            raise SystemExit('seat-query requires its frozen evidence collector')
    live_pose_enabled = validate_live_pose_observer(manifest)
    validate_live_pose_evidence_contract(manifest)
    seat_correction_enabled = validate_seat_correction(manifest, frozen=True)
    render_lease_enabled = validate_render_lease(manifest, frozen=True)
    if render_lease_enabled:
        for source in RENDER_LEASE_SOURCES:
            receipt = 'source-receipts/'+source
            if receipt not in manifest.get('files', {}) or not (root/receipt).is_file():
                raise SystemExit('render lease requires all frozen source receipts')
        helper = 'linux_ui_render_lease_evidence.py'
        if helper not in manifest.get('files', {}) or not (root/helper).is_file():
            raise SystemExit('render lease requires its frozen evidence collector')
    if seat_correction_enabled:
        for source in SEAT_CORRECTION_SOURCES:
            receipt = 'source-receipts/' + source
            if receipt not in manifest.get('files', {}) or not (root / receipt).is_file():
                raise SystemExit('seat correction requires all frozen source receipts')
        helper = 'linux_ui_seat_correction_evidence.py'
        if helper not in manifest.get('files', {}) or not (root / helper).is_file():
            raise SystemExit('seat correction requires its frozen collector')
    if live_pose_enabled:
        for source in LIVE_POSE_SOURCES:
            receipt = 'source-receipts/' + source
            if receipt not in manifest.get('files', {}) or not (root / receipt).is_file():
                raise SystemExit('live-pose observer requires its frozen source receipts')
        for helper in ('linux_ui_live_pose_observer_evidence.py', 'linux_ui_pose_scope_evidence.py'):
            if helper not in manifest.get('files', {}) or not (root / helper).is_file():
                raise SystemExit('live-pose observer requires both frozen evidence collectors')
    fork_enabled = validate_presentation_fork(manifest, manifest.get('independent_keyboard_frames', 0))
    if fork_enabled:
        if not network_physics:
            raise SystemExit('presentation fork requires disposable independent network physics')
        for name in ('player_presentation_fork_probe.h', 'player_presentation_overlay.h',
                     'gate_player_presentation_fork.h', 'gate_fork_save.h', 'gate_linux.c'):
            receipt = 'source-receipts/src/native/engine/linux/' + name
            if receipt not in manifest.get('files', {}) or not (root / receipt).is_file():
                raise SystemExit('presentation fork requires its frozen source receipts')
    ui_callback_enabled = validate_ui_callback_observer(manifest, manifest.get('independent_keyboard_frames', 0))
    if ui_callback_enabled and not network_physics:
        raise SystemExit('UI callback observer requires disposable network physics')
    if ui_callback_enabled and 'source-receipts/src/native/engine/linux/gate_linux.c' not in manifest.get('files', {}):
        raise SystemExit('UI callback observer requires sealed gate source')
    throttle_environment = throttle_probe_environment(manifest, network_physics)
    throttle_receipt = 'source-receipts/src/native/engine/linux/gate_player_throttle_discard.h'
    if throttle_environment and (throttle_receipt not in manifest.get('files', {}) or
                                 not (root / throttle_receipt).is_file()):
        raise SystemExit('throttle probe requires its frozen native header receipt')
    presentation_enabled = manifest.get('player_presentation_probe', False)
    camera_enabled = manifest.get('player_external_camera_probe', False)
    if camera_enabled and (collect_camera_evidence is None or not (root / 'linux_player_camera_evidence.py').is_file()):
        raise SystemExit('external camera probe requires its bundled strict camera evidence collector')
    lifetime_preload = physics or getattr(args, 'player_lifetime_preload', False)
    recurring_tail = getattr(args, 'controller_recurring_tail_readiness', False) or physics
    recurring = getattr(args, 'controller_recurring_readiness', False) or recurring_tail
    recurring_end = getattr(args, 'controller_recurring_end', None)
    recurring_end = physics_window_end(physics, physics_frames, recurring_end, ticks, network=network_physics, session_frames=manifest.get('player_ui_session_frames',0), startup=manifest.get('player_startup_phases'))
    if recurring_end is not None and not recurring:
        raise SystemExit('--controller-recurring-end requires a recurring readiness mode')
    recurring_end = 30 if recurring_end is None else recurring_end
    if lifetime_preload and not manifest.get('player_lifetime_sha256'):
        raise SystemExit('lifetime preload requires a bundle with --player-lifetime-preload')
    if lifetime_preload and not recurring_tail:
        raise SystemExit('lifetime preload requires tail readiness or disposable physics mode')
    if physics_frames > 1 and not network_physics and (collect_recurring_physics_evidence is None or
            'linux_player_frames_evidence.py' not in manifest.get('files', {}) or
            not (root / 'linux_player_frames_evidence.py').is_file()):
        raise SystemExit('repeated physics requires the bundled linux_player_frames_evidence.py collector')
    if network_physics and (collect_network_physics_evidence is None or
            'linux_player_network_evidence.py' not in manifest.get('files', {}) or
            not (root / 'linux_player_network_evidence.py').is_file()):
        raise SystemExit('independent keyboard physics requires its bundled strict network collector')
    routes_lifecycle = getattr(args, 'controller_lifecycle_routes_discard', False) or input_lifecycle
    update_lifecycle = getattr(args, 'controller_lifecycle_update_discard', False) or routes_lifecycle
    selected_modes = [getattr(args, field, False) for field in
                      ('controller_lifecycle_discard', 'controller_lifecycle_isolated_discard',
                       'controller_lifecycle_update_discard', 'controller_lifecycle_routes_discard',
                       'controller_lifecycle_input_discard', 'controller_recurring_readiness',
                       'controller_recurring_tail_readiness', 'controller_physics_discard')]
    isolated_lifecycle = getattr(args, 'controller_lifecycle_isolated_discard', False) or update_lifecycle
    if sum(selected_modes) > 1:
        raise SystemExit('choose one controller experiment mode')
    lifecycle = getattr(args, 'controller_lifecycle_discard', False) or isolated_lifecycle or physics
    if lifecycle:
        validate_lifecycle_discard(manifest, ticks)
    if recurring:
        validate_recurring_readiness(manifest, ticks, recurring_end)
    if manifest.get('player_control_boundary_probe'):
        validate_control_boundary_probe(manifest.get('stock_keyboard_observer'),
            manifest.get('serial_pathing'), ticks,
            (manifest.get(key) for key in ('player_control_bank_probe', 'player_factory_probe',
             'player_reference_probe', 'stock_keyboard_replay_probe', 'player_seat_preflight')))
    if manifest.get('player_control_bank_probe'):
        validate_control_bank_probe(manifest.get('stock_keyboard_observer'),
                                    manifest.get('serial_pathing'), ticks,
                                    manifest.get('stock_keyboard_replay_probe'),
                                    manifest.get('player_reference_probe'))
    if manifest.get('player_seat_preflight'):
        validate_seat_preflight(manifest.get('player_factory_probe'),
                                manifest.get('player_factory_retain_until'),
                                manifest.get('stock_keyboard_observer'),
                                manifest.get('serial_pathing'), ticks)
    validate_factory_retention(manifest.get('player_factory_retain_until'),
                               manifest.get('player_factory_probe'), ticks)
    if manifest.get('player_control_bank_census') and ticks < 4:
        raise SystemExit('--player-control-bank-census requires at least four ticks')
    if presentation_enabled:
        for name in ('gate_player_presentation_probe.h', 'player_presentation_probe.h'):
            receipt = 'source-receipts/src/native/engine/linux/' + name
            if receipt not in manifest.get('files', {}) or not (root / receipt).is_file():
                raise SystemExit('presentation probe requires both frozen native header receipts')
    for rel, digest in manifest['files'].items():
        if sha(root / rel) != digest:
            raise SystemExit(f'bundle file changed: {rel}')
    validate_presentation_config((root / 'extension/x4_lan_lab/ui/config.lua').read_text(encoding='utf-8'),
                                 presentation_enabled)
    validate_view_query_config((root / 'extension/x4_lan_lab/ui/config.lua').read_text(), view_query_enabled)
    validate_ui_callback_config((root / 'extension/x4_lan_lab/ui/config.lua').read_text(), ui_callback_enabled)
    validate_presentation_fork_config((root / 'extension/x4_lan_lab/ui/config.lua').read_text(), fork_enabled)
    validate_ui_overlay_config((root / 'extension/x4_lan_lab/ui/config.lua').read_text(), ui_overlay_enabled,
                               manifest.get('player_ui_overlay_callbacks', 1))
    validate_plan_kick_config((root / 'extension/x4_lan_lab/ui/config.lua').read_text(), plan_kick_enabled)
    validate_seat_query_config((root / 'extension/x4_lan_lab/ui/config.lua').read_text(), seat_query_enabled)
    validate_live_pose_observer_config((root / 'extension/x4_lan_lab/ui/config.lua').read_text(), live_pose_enabled)
    validate_seat_correction_config((root / 'extension/x4_lan_lab/ui/config.lua').read_text(), seat_correction_enabled)
    validate_render_lease_config((root / 'extension/x4_lan_lab/ui/config.lua').read_text(), render_lease_enabled)
    validate_session_config((root / 'extension/x4_lan_lab/ui/config.lua').read_text(),
                            manifest.get('player_ui_session_frames', 0), manifest.get('player_startup_phases'))
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
    try:
        relay_host, arm_at = remote_session_options(args.relay_host, args.arm_at, external)
    except ValueError as error:
        raise SystemExit(f'REFUSED: {error}') from None
    declared_relay_host = relay_host
    if external and relay_host is None:
        relay_host = '127.0.0.1'
    if external and (not 1024 <= args.relay_port <= 65535 or not re.fullmatch(r'[0-9a-f]{32}', args.relay_token)):
        raise SystemExit('--relay-port must be 1024..65535 and --relay-token 32 lowercase hex')
    local = sorted({int(part) for part in args.local_slots.split(',')}) if external else list(range(1, clients + 1))
    if not local or any(not 1 <= slot <= clients for slot in local):
        raise SystemExit(f'--local-slots must name slots of 1..{clients}')
    guest = validate_external_guest(getattr(args, 'external_player_guest', False), external, local, clients,
                                    network_physics, manifest, getattr(args, 'target_snapshot_sha256', None),
                                    getattr(args, 'target_protocol', None), declared_relay_host, arm_at,
                                    getattr(args, 'target_source_commit', None))
    if guest is not None:
        config = (root / 'extension/x4_lan_lab/ui/config.lua').read_text(encoding='utf-8')
        assignments = re.findall(r'(?m)^\s*X4LAN_CONFIG\.native_gate_clients\s*=\s*(.*?)\s*$', config)
        if assignments not in (['2'], ['@X4LANE_CLIENTS@']) or sha(root / 'snapshot' / SLOT_SAVE) != guest['target_snapshot_sha256']:
            raise SystemExit('external guest requires frozen two-client protocol 3 config and exact snapshot')
    if lifecycle and len(local) != 2 and guest is None:
        raise SystemExit('--controller-lifecycle-discard requires two local clients')
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
                                            'gate_so_sha256': manifest['gate_so_sha256'], 'external_guest': guest}))
        return
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
    by_slot = {slot: stage_case(root, run_dir, slot, game, steam_home, port, token, clients, credentials.get(slot),
                                relay_host=relay_host, arm_at=arm_at) for slot in local}
    cases = [by_slot[slot] for slot in local]
    for path in [run_dir, *run_dir.rglob('*')]:
        if os.geteuid() == 0 and not path.is_symlink():
            os.chown(path, user.pw_uid, user.pw_gid)
    result = {'name': name, 'ticks': ticks, 'port': port, 'memory_available_gib': round(memory, 1), 'discard_layer': discard,
              'manifest_commit': manifest['commit'], 'gate_so_sha256': manifest['gate_so_sha256'], 'steam_before': steam,
              'player_view_wire': view_wire_enabled,
              'player_view_wire_evidence_version': manifest.get('player_view_wire_evidence_version'),
              'player_lifetime_sha256': manifest.get('player_lifetime_sha256'),
              'player_lifetime_enabled': lifetime_preload,
              'player_presentation_fork_probe': fork_enabled,
              'player_ui_plan_kick_probe': plan_kick_enabled,
              'player_ui_seat_query_probe': seat_query_enabled,
              'player_ui_live_pose_observer': live_pose_enabled,
              'player_ui_seat_correction': seat_correction_enabled,
              'player_ui_render_lease': render_lease_enabled,
              'player_ui_camera_refill': camera_refill_enabled,
              'player_ui_camera_refill_evidence_version': manifest.get('player_ui_camera_refill_evidence_version'),
              'player_ui_session_frames': manifest.get('player_ui_session_frames',0),
              'shared_identity_t40': manifest.get('shared_identity_t40', False),
              'shared_identity_plan_sha256': manifest.get('shared_identity_plan_sha256'),
              'player_startup_phases': manifest.get('player_startup_phases'),
              'session_budget': session,
              'session_disk_limit': manifest.get('session_disk_limit'),
              'player_ui_render_lease_evidence_version': manifest.get('player_ui_render_lease_evidence_version'),
              'player_ui_seat_correction_evidence_version': manifest.get('player_ui_seat_correction_evidence_version'),
              'player_ui_live_pose_observer_evidence_version': manifest.get('player_ui_live_pose_observer_evidence_version'),
              'player_ui_overlay_probe': ui_overlay_enabled,
              'player_ui_overlay_callbacks': manifest.get('player_ui_overlay_callbacks', 1),
              'player_view_query_observer': view_query_enabled,
              'player_view_query_sha256': manifest.get('player_view_query_sha256'),
              'player_throttle_probe': bool(manifest.get('player_throttle_probe', False)),
              'player_throttle_warm_probe': bool(manifest.get('player_throttle_warm_probe', False)),
              'portable_math_sha256': manifest.get('portable_math_sha256'), 'seed_rng': manifest.get('seed_rng'), 'launch_order': order,
              'external_relay': external, 'relay_host': relay_host if external else None, 'arm_at': arm_at,
              'local_slots': local, 'snapshot_seed': world_seed, 'external_guest': guest}
    if lifecycle:
        result['physics_discard' if physics else 'lifecycle_discard'] = {'pass': False, 'clients': {}, 'restored': False,
                                       'resumed': False, 'tickrunaccepted': False}
        if physics:
            result['physics_discard']['frame_count'] = physics_frames
            if network_physics:
                result['physics_discard'].update(mode='independent_keyboard', validation_pending=True)
    if recurring and not physics:
        result['recurring_readiness'] = {'pass': False, 'end': recurring_end, 'tail': recurring_tail, 'clients': {},
                                         'readiness_only': True, 'other_threads_excluded': False}
    procs, starts, logs, games = [], {}, [], []
    cases_by_slot = {}
    cancelled = []
    # bwrap binds each private lock over this path, and cannot create it in the read-only view.
    lock_target = Path('/tmp/EGOSOFT_X4.lock')
    try:
        lock_fd, lock_stat = create_lock_target(lock_target, user.pw_uid, user.pw_gid)
    except OSError as error:
        raise SystemExit(f'REFUSED: cannot exclusively create lock target {lock_target}: {error}') from error
    for number in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(number, lambda *_: cancelled.append(1))

    def launch(label, command, env=None, as_user=False, stderr_path=None):
        log = (run_dir / f'{label}.log').open('wb')
        logs.append(log)
        error_log = subprocess.STDOUT
        if stderr_path is not None:
            error_log = stderr_path.open('wb')
            logs.append(error_log)
        extra = {'user': user.pw_uid, 'group': user.pw_gid, 'extra_groups': []} if as_user and os.geteuid() == 0 else {}
        parent = os.getpid()

        def parent_death():
            if ctypes.CDLL(None).prctl(1, signal.SIGKILL) != 0 or os.getppid() != parent:
                os._exit(125)
        proc = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=error_log, start_new_session=True,
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
            if lifetime_preload:
                env['LD_PRELOAD'] = ' '.join(filter(None, (str(case / PLAYER_LIFETIME), env.get('LD_PRELOAD'))))
                env['X4MP_PLAYER_LIFETIME_PRELOAD'] = '1'
            if view_query_enabled:
                env = view_query_environment(env, case, manifest['player_view_query_sha256'],
                                             manifest['player_view_query_size'])
                if ui_callback_enabled:
                    env['X4MP_UI_CALLBACK_OBSERVER'] = '1'
            env = stock_view_observer.environment(env, stock_view_enabled)
            env = guest_capture_bundle.environment(env, render_capture_enabled, slot)
            env = input_observation_bundle.environment(env, host_capture_enabled, slot)
            env = projection_scope_bundle.environment(env, projection_scope_enabled, slot)
            env = selection_parity_bundle.environment(env, selection_parity_enabled, slot, manifest)
            env = cursor_bundle.environment(env, cursor_enabled)
            env = space_bundle.environment(env, space_enabled)
            env = ancestry_bundle.environment(env, ancestry_enabled)
            env = component_trace_bundle.environment(env, component_trace_enabled)
            env = mouse_trace.environment(env, mouse_trace_enabled)
            env = precompare_trace.environment(env, precompare_enabled)
            env = fire_observation.environment(env, fire_observation_enabled)
            env = camera_refill_bundle.environment(env, camera_refill_enabled, slot)
            env.update(presentation_fork_environment(fork_enabled))
            env.update(ui_overlay_environment(ui_overlay_enabled, slot, manifest.get('player_ui_overlay_callbacks', 1), manifest.get('player_ui_session_frames', 0),
                                                   manifest.get('player_startup_phases')))
            env.update(plan_kick_environment(plan_kick_enabled, slot))
            env.update(seat_query_environment(seat_query_enabled, slot))
            env.update(live_pose_observer_environment(live_pose_enabled, slot))
            env.update(seat_correction_environment(seat_correction_enabled, slot))
            env.update(render_lease_environment(render_lease_enabled, slot))
            if physics:
                env['X4_DEV_PLAYER_PHYSICS'] = physics_environment_mode(physics_frames, network_physics)
                env['X4_DEV_PLAYER_LIFECYCLE_LOG'] = str(profile / 'controller-lifecycle.log')
                env.update(throttle_environment)
            elif lifecycle:
                env['X4_DEV_PLAYER_LIFECYCLE'] = ('take-release-input-discard' if input_lifecycle else
                    'take-release-routes-discard' if routes_lifecycle else
                    'take-release-update-discard' if update_lifecycle else
                    'take-release-isolated-discard' if isolated_lifecycle else 'take-release-discard')
                env['X4_DEV_PLAYER_LIFECYCLE_LOG'] = str(profile / 'controller-lifecycle.log')
            if recurring:
                env['X4LAN_PLAYER_CONTROL_RECURRING'] = '1'
                env['X4LAN_PLAYER_CONTROL_RECURRING_END'] = str(recurring_end)
                if recurring_tail:
                    env['X4LAN_PLAYER_CONTROL_RECURRING_TAIL'] = '1'
            env = view_wire_bundle.environment(env, view_wire_enabled, manifest)
            program = [str(case / 'game/X4'), '-personalfolderid', '45841', '-skipintro', '-debug', 'scripts',
                       '-logfile', 'engine-output.log', '-nocputhrottle', '-noworkshopsync', f'-seed:{world_seed}']
            if semaphore_observer_enabled:
                if not Path('/usr/bin/python3').is_file():
                    raise RuntimeError('semaphore observer requires the system Python launcher')
                env, program = semaphore_bundle.wrap_program(case, manifest, env, program)
            cases_by_slot[slot] = case
            games_by_slot[slot] = (launch(f'game{slot}-stdout', namespace_command(case, game, dlc_binds, lock, env, program, args.user, shm),
                stderr_path=run_dir / f'game{slot}-stderr.log' if recurring else None), profile)
        games = [games_by_slot[slot] for slot in local]
        result['started'] = time.time()
        deadline = time.monotonic() + args.timeout
        watches = [LogWatch(profile / 'engine-output.log') for _, profile in games]
        process_watches = [LogWatch(run_dir / f'game{slot}-stdout.log') for slot in local]
        error_watches = [LogWatch(run_dir / f'game{slot}-stderr.log') if recurring else None for slot in local]
        stopped_at = {}
        while True:
            if cancelled:
                raise RuntimeError('cancelled')
            if time.monotonic() > deadline:
                raise RuntimeError('timeout (loading or relay); see the client logs')
            if steam_identity(steam_home, user.pw_uid) != steam:
                raise RuntimeError('Steam client identity changed')
            if session:
                enforce_session_disk_budget(run_dir, session['disk_limit'])
            for slot, watch, process_watch, error_watch, (proc, _) in zip(local, watches, process_watches, error_watches, games):
                watch.poll()
                process_watch.poll()
                if error_watch is not None:
                    error_watch.poll()
                if recurring and (watch.failure or process_watch.failure or error_watch.failure):
                    raise RuntimeError(f'client {slot}: {watch.failure or process_watch.failure or error_watch.failure}')
                if lifecycle:
                    if watch.failure or process_watch.failure:
                        raise RuntimeError(f'client {slot}: {watch.failure or process_watch.failure}')
                    continue
                if proc.poll() is not None and not watch.stopped and not watch.failure:
                    raise RuntimeError(f'client {slot}: X4 exited before its STOP line')
                if watch.stopped or watch.failure:
                    stopped_at.setdefault(slot, time.monotonic())
            if lifecycle:
                if lifecycle_discard_wait(
                        {slot: proc.poll() for slot, (proc, _) in zip(local, games)},
                        {slot: profile / 'controller-lifecycle.log' for slot, (_, profile) in zip(local, games)},
                        network=network_physics):
                    break
                time.sleep(0.1)
                continue
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
        collect_preload_evidence(result, local, games, run_dir, manifest,
                                 view_query_enabled=view_query_enabled, ui_callback_enabled=ui_callback_enabled,
                                 lifetime_preload=lifetime_preload, fork_enabled=fork_enabled,
                                 recurring=recurring, physics_frames=physics_frames,
                                 ui_overlay_enabled=ui_overlay_enabled)
        if not network_physics:
            from linux_view_component_trace_evidence import collect_component_trace
            disabled_trace = {str(slot): collect_component_trace(
                run_dir / f'game{slot}-stderr.log', None, None, None, False) for slot in local}
            result['view_component_trace'] = disabled_trace
            if any(not proof['pass'] for proof in disabled_trace.values()):
                raise ValueError('unexpected or unreadable disabled component trace')
        if lifecycle:
            clients_evidence = {}
            for slot, (proc, profile) in zip(local, games):
                arguments = (profile / 'controller-lifecycle.log',
                             run_dir / f'client{slot}-controller-lifecycle.log',
                             profile / 'engine-output.log', run_dir / f'game{slot}-stdout.log', proc.returncode)
                if physics:
                    stderr = run_dir / f'game{slot}-stderr.log'
                    if network_physics:
                        clients_evidence[str(slot)] = collect_network_client(
                            arguments, stderr, physics_frames, slot,
                            camera=camera_enabled, throttle=bool(throttle_environment),
                            throttle_start_tick=manifest.get('player_throttle_start_tick') or 5,
                            presentation=presentation_enabled, session_frames=manifest.get('player_ui_session_frames',0), startup=manifest.get('player_startup_phases'), cursor=cursor_enabled, space=space_enabled, allow_view=view_wire_enabled,
                            allow_semaphore_checkpoint=semaphore_observer_enabled)
                    else:
                        clients_evidence[str(slot)] = collect_physics_client(arguments, stderr, physics_frames)
                else:
                    clients_evidence[str(slot)] = collect_lifecycle_discard(
                        *arguments, isolated=isolated_lifecycle, update=update_lifecycle,
                        routes=routes_lifecycle, injected_input=input_lifecycle)
            result['physics_discard' if physics else 'lifecycle_discard'] = {
                'pass': len(local) == 2 and set(clients_evidence) == {str(slot) for slot in local}
                        and all(item['pass'] for item in clients_evidence.values())
                        and not result.get('error') and not result.get('cleanup_errors')
                        and not result['owned_processes_remaining']
                        and result['steam_unchanged'] and not result['x4_left'],
                'clients': clients_evidence, 'restored': False, 'resumed': False,
                'tickrunaccepted': False}
            if physics:
                result['physics_discard']['frame_count'] = physics_frames
                if network_physics:
                    result['physics_discard'].update(mode='independent_keyboard', validation_pending=False)
                    if guest is not None:
                        result['physics_discard']['local_physics_pass'] = external_guest_local_physics(
                            clients_evidence, physics_frames)
                        result['physics_discard']['validation_pending'] = True
                    peer_errors = network_peer_errors(clients_evidence, physics_frames,
                                                      throttle=bool(throttle_environment))
                    result['physics_discard']['peer_errors'] = peer_errors
                    result['physics_discard']['pass'] = result['physics_discard']['pass'] and not peer_errors
                    if camera_enabled:
                        profiles = {str(slot): profile for slot, (_, profile) in zip(local, games)}
                        add_camera_evidence(result['physics_discard'], run_dir, profiles)
                    from linux_player_startup_phases import startup_phases
                    phase = startup_phases(manifest.get('player_startup_phases'))
                    last_tick = phase['apply'] + physics_frames - 1
                    ancestry_results = {}
                    if ancestry_enabled:
                        for slot, (_, profile) in zip(local, games):
                            stderr = run_dir / f'game{slot}-stderr.log'
                            try:
                                expected = ancestry_expected_identity(clients_evidence[str(slot)], stderr)
                            except (ValueError, OSError, KeyError, TypeError):
                                expected = {}
                            proof = ancestry_bundle.collect(profile, stderr, slot, True, last_tick, expected)
                            if proof.get('bind_tick') != phase['take']:
                                proof.setdefault('errors', []).append('ancestry bind did not occur at Take')
                                proof['pass'] = False
                            ancestry_results[str(slot)] = proof
                        result['physics_discard']['view_ancestry'] = ancestry_results
                        result['physics_discard']['pass'] = (result['physics_discard']['pass'] and
                            set(ancestry_results) == {'1', '2'} and
                            all(proof['pass'] for proof in ancestry_results.values()))
                    from linux_view_component_trace_evidence import collect_component_trace
                    trace_results = {str(slot): collect_component_trace(
                        run_dir / f'game{slot}-stderr.log', ancestry_results.get(str(slot)),
                        clients_evidence.get(str(slot)), last_tick, component_trace_enabled)
                        for slot, _ in zip(local, games)}
                    result['physics_discard']['view_component_trace'] = trace_results
                    result['physics_discard']['pass'] = (result['physics_discard']['pass'] and
                        set(trace_results) == {'1', '2'} and all(proof['pass'] for proof in trace_results.values()))
        else:
            result.update(collect_run_evidence(run_dir, local, cases, ticks, manifest,
                                           writers_closed=not result.get('cleanup_errors') and not result['owned_processes_remaining']))
        result['pass'] = not lifecycle and (
            not result.get('error')
            and not result.get('cleanup_errors')
            and result['steam_unchanged']
            and not result['x4_left']
            and result['evidence_ok']
            and (external or (result['relay'] and result['relay'].get('verdict') == 'pass'))
            and all(result.get('portable_math_active', [True]))
        )
        if recurring and not physics:
            clients_evidence = {}
            for slot in local:
                path = run_dir / f'game{slot}-stderr.log'
                with path.open(errors='replace') as stream:
                    parser = recurring_tail_readiness_evidence if recurring_tail else recurring_readiness_evidence
                    evidence = parser(stream, recurring_end)
                evidence.update(stderr_path=str(path), stderr_sha256=sha(path))
                for source in (run_dir / f'game{slot}-stdout.log', by_slot[slot][1] / 'engine-output.log'):
                    with source.open(errors='replace') as stream:
                        fatal = [line.strip()[:300] for line in stream if is_fatal_gate_message(line)
                                 or 'PLAYER_CONTROL_RECURRING_FATAL' in line]
                    evidence['errors'].extend(fatal)
                evidence['pass'] = not evidence['errors']
                clients_evidence[str(slot)] = evidence
            result['recurring_readiness'] = {
                'pass': result['pass'] and set(clients_evidence) == {str(slot) for slot in local}
                        and all(client['pass'] for client in clients_evidence.values()),
                'full_tick_evidence_pass': result['pass'], 'clients': clients_evidence,
                'end': recurring_end, 'tail': recurring_tail,
                'readiness_only': True, 'other_threads_excluded': False}
            result['pass'] = result['pass'] and result['recurring_readiness']['pass']
    except Exception as error:
        result['postprocessing_error'] = str(error)
    try:
        clients = result.get('physics_discard', {}).get('clients', {})
        proofs = {str(slot): guest_capture_bundle.collect(
            run_dir / f'game{slot}-stderr.log', clients.get(str(slot)), manifest, slot, render_capture_enabled)
            for slot, _ in zip(local, games)}
        result['view_render_capture_shadow_evidence'] = proofs
        if ((render_capture_enabled and set(proofs) != {'1', '2'}) or
                any(not proof['pass'] for proof in proofs.values())):
            result['pass'] = False
            result.get('physics_discard', {})['pass'] = False
    except Exception as error:
        result['view_render_capture_shadow_error'] = str(error)
        result['pass'] = False
    try:
        clients = result.get('physics_discard', {}).get('clients', {})
        proofs = {str(slot): input_observation_bundle.collect(
            run_dir / f'game{slot}-stderr.log', clients.get(str(slot)), manifest, slot, host_capture_enabled)
            for slot, _ in zip(local, games)}
        result['view_host_capture_shadow_evidence'] = proofs
        if ((host_capture_enabled and set(proofs) != {'1', '2'}) or
                any(not proof['pass'] for proof in proofs.values())):
            result['pass'] = False
            result.get('physics_discard', {})['pass'] = False
    except Exception as error:
        result['view_host_capture_shadow_error'] = str(error)
        result['pass'] = False
    try:
        clients = result.get('physics_discard', {}).get('clients', {})
        proofs = {str(slot): projection_scope_bundle.collect(
            run_dir / f'game{slot}-stderr.log', clients.get(str(slot)), manifest, slot, projection_scope_enabled)
            for slot, _ in zip(local, games)}
        result['projection_fov_scope_evidence'] = proofs
        if ((projection_scope_enabled and set(proofs) != {'1', '2'}) or
                any(not proof['pass'] for proof in proofs.values())):
            result['pass'] = False
            result.get('physics_discard', {})['pass'] = False
    except Exception as error:
        result['projection_fov_scope_error'] = str(error)
        result['pass'] = False
    try:
        clients = result.get('physics_discard', {}).get('clients', {})
        proofs = {str(slot): selection_parity_bundle.collect(
            run_dir / f'game{slot}-stderr.log', clients.get(str(slot)), manifest, slot, selection_parity_enabled)
            for slot, _ in zip(local, games)}
        result['selection_parity_evidence'] = proofs
        if ((selection_parity_enabled and set(proofs) != {'1', '2'}) or
                any(not proof['pass'] for proof in proofs.values())):
            result['pass'] = False
            result.get('physics_discard', {})['pass'] = False
    except Exception as error:
        result['selection_parity_error'] = str(error)
        result['pass'] = False
    try:
        wire = view_wire_bundle.collect(run_dir, result.get('physics_discard', {}).get('clients', {}),
                                        manifest, view_wire_enabled)
        result['player_view_wire_evidence'] = wire
        if not wire['pass']:
            result['pass'] = False
            result.get('physics_discard', {})['pass'] = False
    except Exception as error:
        result['player_view_wire_error'] = str(error)
        result['pass'] = False
        result.get('physics_discard', {})['pass'] = False
    if semaphore_observer_enabled:
        collect_semaphore_observations(result, local, cases_by_slot, manifest)
    if 'stock_view_fov_observer' in manifest:
        collect_stock_view_observations(result, local, games, run_dir, stock_view_enabled,
                                        rewrite=view_wire_enabled)
    if 'player_cursor_active_flight' in manifest:
        collect_cursor_observations(result, local, games, run_dir, cursor_enabled, manifest)
    if 'player_space_context' in manifest:
        try:
            proofs = {str(slot): space_bundle.collect(
                run_dir / f'game{slot}-stderr.log', space_enabled,
                result.get('physics_discard', {}).get('clients', {}).get(str(slot), {}),
                result.get('player_cursor_active_flight_evidence', {}).get(str(slot), {}))
                for slot, (_, profile) in zip(local, games)}
            result['player_space_context_evidence'] = proofs
            if (space_enabled and set(proofs) != {'1', '2'}) or any(not proof['pass'] for proof in proofs.values()):
                result['pass'] = False
                result.get('physics_discard', {})['pass'] = False
        except Exception as error:
            result['player_space_context_error'] = str(error)
            result['pass'] = False
            result.get('physics_discard', {})['pass'] = False
    # Diagnostics remain observations, with separate hash-bound acceptance.
    # A failed required observer invalidates the bounded experiment, never its raw logs.
    if 'cursor_input_trace' in manifest:
        try:
            bounded = result.get('physics_discard', {})
            proofs = {str(slot): mouse_trace.collect(
                run_dir / f'game{slot}-stderr.log', mouse_trace_enabled,
                bounded.get('clients', {}).get(str(slot), {}),
                result.get('player_cursor_active_flight_evidence', {}).get(str(slot), {}), manifest)
                for slot, _ in zip(local, games)}
            result['cursor_input_trace_evidence'] = proofs
            if (mouse_trace_enabled and set(proofs) != {'1', '2'}) or any(not proof['pass'] for proof in proofs.values()):
                result['pass'] = False
                bounded['pass'] = False
        except Exception as error:
            result['cursor_input_trace_error'] = str(error)
            result['pass'] = False
            result.get('physics_discard', {})['pass'] = False
    if 'view_precompare_trace' in manifest:
        try:
            bounded = result.get('physics_discard', {})
            end_tick = session['recurring_end'] if session else 0
            proofs = {str(slot): precompare_trace.collect(
                run_dir / f'game{slot}-stderr.log',
                bounded.get('view_ancestry', {}).get(str(slot)),
                bounded.get('view_component_trace', {}).get(str(slot)),
                bounded.get('clients', {}).get(str(slot)), end_tick, precompare_enabled)
                for slot, _ in zip(local, games)}
            result['view_precompare_trace_evidence'] = proofs
            if (precompare_enabled and set(proofs) != {'1', '2'}) or any(not proof['pass'] for proof in proofs.values()):
                result['pass'] = False
                bounded['pass'] = False
        except Exception as error:
            result['view_precompare_trace_error'] = str(error)
            result['pass'] = False
            result.get('physics_discard', {})['pass'] = False
    if 'player_fire_observation' in manifest:
        collect_fire_observations(result, local, games, run_dir, fire_observation_enabled, manifest)
    if session:
        try:
            from linux_ui_session_evidence import compact_validated_result, rolling_world_summary
            roots = {str(slot): rolling_world_summary(profile/'engine-output.log', session['recurring_end'])
                     for slot, (_, profile) in zip(local, games)}
            result['session_world_roots'] = roots
            matched = (set(roots) == {'1','2'} and not any(proof['errors'] for proof in roots.values()) and
                       roots['1']['sha256'] == roots['2']['sha256'])
            result['session_raw_world_roots_match'] = None if guest is not None else matched
            if not matched:
                result.get('physics_discard', {})['pass'] = False
                result.get('physics_discard', {}).setdefault('peer_errors', []).append('session rolling-root evidence missing or different')
            if guest is None:
                compact_validated_result(result)
        except Exception as error:
            result['session_evidence_error'] = str(error)
            result.get('physics_discard', {})['pass'] = False
    (run_dir / 'run-result.json').write_text(json.dumps(result, indent=2, default=str) + '\n')
    print('LINUX_GATE_LIVE ' + json.dumps({k: result.get(k) for k in ('name', 'pass', 'lifecycle_discard', 'physics_discard', 'recurring_readiness', 'error', 'postprocessing_error', 'stops', 'world_identity', 'world_capture_problems', 'portable_math_active', 'world_hash_ticks',
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
    b.add_argument('--world-rolling-raw-ids', nargs='+', metavar='SAVED_HEX',
                   help='default-off raw canonical bytes for 1..8 unique 16-digit lowercase saved IDs; requires leaf ticks')
    b.add_argument('--world-rolling-leaf-ticks', type=int, nargs=2, metavar=('FIRST', 'SECOND'),
                   help='diagnostic per-object domain hashes at two rolling ticks')
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
    b.add_argument('--frame-timing', action='store_true',
                   help='record existing native frame, wait and probe timing counters')
    b.add_argument('--stock-keyboard-observer', action='store_true',
                   help='observe selected native keyboard handlers; leaves local controls unchanged')
    b.add_argument('--player-control-bank-census', action='store_true',
                   help='read-only Linux control-bank census; requires observer, serial pathing and at least four ticks')
    b.add_argument('--player-seat-preflight', action='store_true',
                   help='read-only retained-player seat inspection at tick four; no seating or controls')
    b.add_argument('--player-control-bank-probe', action='store_true',
                   help='reserved control-bank roundtrip diagnostic; native activation is currently withheld')
    b.add_argument('--player-control-boundary-probe', action='store_true',
                   help='readiness-only tick-five main/worker rendezvous; does not exchange player state')
    b.add_argument('--player-factory-retain-until', type=int, metavar='TICK',
                   help='retain the diagnostic P2 result until this tick; requires the factory probe')
    b.add_argument('--ui-callback-observer', action='store_true',
                   help='bounded UI callback scope observation; requires view-query and presentation probes')
    shared_identity_bundle.add_arguments(b)
    b.add_argument('--view-ancestry-observer', action='store_true',
                   help='bounded owner-thread VIEW ancestry sampling; requires presentation and view-query observers')
    b.add_argument('--player-fire-observation', action='store_true',
                   help='observe the first primary-group P2 gun once in a finite T40 physics session; no firing mutation')
    b.add_argument('--cursor-input-trace', action='store_true',
                   help='observe native mouse gates in the bounded cursor experiment')
    b.add_argument('--view-precompare-trace', action='store_true',
                   help='observe engine ownership before the native player comparison')
    b.add_argument('--view-component-trace', action='store_true',
                   help='bounded rotating component identity samples; requires --view-ancestry-observer')
    b.add_argument('--player-view-query-observer', action='store_true',
                   help='default-off pre-main view query observer; requires independent >=4 frames and lifetime preload')
    b.add_argument('--player-lifetime-preload', action='store_true',
                   help='bundle default-off pre-main lifetime guard for disposable physics experiments only')
    b.add_argument('--semaphore-observer', action='store_true',
                   help='bounded target semaphore diagnostics for a finite T40 session; no ownership proof')
    b.add_argument('--player-factory-probe', action='store_true',
                   help='isolated Linux construct/restore/dispose diagnostic; not playable controls')
    b.add_argument('--player-reference-probe', action='store_true',
                   help='default-off read-only P1 reference inspection; requires observer and serial pathing')
    b.add_argument('--stock-keyboard-replay-probe', action='store_true',
                   help='default-off two-client single-ship keyboard proof; requires observer, not independent player controls')
    throttle_modes = b.add_mutually_exclusive_group()
    throttle_modes.add_argument('--player-throttle-probe', action='store_true',
                   help='default-off disposable STATE13 P2 throttle diagnostic; requires independent keyboard physics')
    throttle_modes.add_argument('--player-throttle-warm-probe', action='store_true',
                   help='disposable STATE13 probe after a neutral Update; requires at least 3 independent frames')
    b.add_argument('--player-startup-phases', choices=('v1-t40',),
                   help='explicit common startup tuple; requires finite session mode')
    b.add_argument('--player-ui-session-frames', type=int, default=0, metavar='N',
                   help='explicit finite disposable session, 180..900 frames; implies UI budget and excludes legacy overlay flags')
    b.add_argument('--player-cursor-active-flight', action='store_true',
                   help='default-off finite T40 active-flight cursor replay only; excludes context, VIEW, AIM and FIRE')
    b.add_argument('--player-space-context', action='store_true',
                   help='default-off T40 SPACE context replay; requires independent cursor mode')
    b.add_argument('--player-view-wire', action='store_true',
                   help='default-off finite T40 VIEW wire; requires the complete capture and consumer chain')
    b.add_argument('--player-selection-parity', action='store_true',
                   help='default-off read-only Player/UI selection comparison at authenticated VIEW captures')
    b.add_argument('--player-ui-projection-fov-scope', action='store_true',
                   help='enable the bounded guest-only projection FOV presentation scope')
    b.add_argument('--view-host-capture-shadow', action='store_true',
                   help='default-off host-only VIEW capture shadow; never admits VIEW wire records')
    b.add_argument('--view-render-capture-shadow', action='store_true',
                   help='default-off guest-only VIEW capture shadow; never admits VIEW wire records')
    b.add_argument('--stock-view-fov-observer', action='store_true',
                   help='default-off stock coefficient topology observer; requires T40 finite independent window')
    b.add_argument('--player-ui-camera-refill', action='store_true',
                   help='default-off guest stock camera refill lease; requires full render/correction chain')
    b.add_argument('--player-ui-render-lease', action='store_true',
                   help='default-off bounded guest GameRender lease; requires explicit seat correction and overlay160')
    b.add_argument('--player-ui-seat-correction', action='store_true',
                   help='default-off coupled Move seat correction; requires explicit live observer, seat query and overlay160')
    b.add_argument('--player-ui-live-pose-observer', action='store_true',
                   help='guest-only bounded native pose-entry observation; requires explicit seat query and overlay160')
    b.add_argument('--player-ui-seat-query-probe', action='store_true',
                   help='guest parent-worker stock seat-query diagnostic; requires explicit overlay160')
    b.add_argument('--player-ui-plan-kick-probe', action='store_true',
                   help='guest-only disposable plan handler/RNG diagnostic; requires explicit overlay160')
    b.add_argument('--player-ui-overlay-callbacks', type=int, choices=(1, 8, 160), default=1,
                   help='bounded UI overlay callback count; 8/160 require the explicit probe and >=count+5 frames')
    b.add_argument('--player-ui-overlay-probe', action='store_true',
                   help='default-off client2 bounded UI overlay (one callback by default); requires fork/view-query/callbacks and >=6 independent frames')
    b.add_argument('--player-presentation-fork-probe', action='store_true',
                   help='default-off disposable child presentation getter probe; requires independent >=5 frames and presentation')
    b.add_argument('--player-presentation-probe', action='store_true',
                   help='default-off read-only presentation identity probe; requires >=4 independent frames')
    b.add_argument('--player-external-camera-probe', action='store_true',
                   help='default-off disposable external-camera diagnostic; requires >=3 independent keyboard frames')
    b.add_argument('--independent-keyboard-frames', type=int, default=0, metavar='N',
                   help='explicit disposable two-player keyboard physics window, 2..600; requires all probe flags')
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
    b.add_argument('--runtime-id-caller-stride', type=int, metavar='N',
                   help='emit full runtime ID caller tables every N ticks (1..3600; needs --runtime-id-callers)')
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
    b.add_argument('--probes-diag', action='store_true', help='PROBES rows: the fixture ships obstacle probe vectors (native_gate_probes_diag)')
    b.add_argument('--pursuit-speed', action='store_true', help="the pursuit behaviour's speed factor as Windows computes it (native_gate_pursuit_speed)")
    b.add_argument('--avoid-angle', action='store_true', help="the obstacle avoidance's angle scaling as Windows computes it (native_gate_avoid_angle)")
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
    lifecycle_modes = r.add_mutually_exclusive_group()
    lifecycle_modes.add_argument('--controller-lifecycle-discard', action='store_true',
                   help='RUN-only disposable take/release experiment; expects exit 223, never accepts a tick run')
    lifecycle_modes.add_argument('--controller-lifecycle-isolated-discard', action='store_true',
                   help='RUN-only isolated take/release globals experiment; still discards the process with exit 223')
    lifecycle_modes.add_argument('--controller-lifecycle-update-discard', action='store_true',
                   help='RUN-only one native controller Update without synthetic input; isolated disposable experiment')
    lifecycle_modes.add_argument('--controller-lifecycle-routes-discard', action='store_true',
                   help='RUN-only bounded getter routes after one isolated controller Update; disposable state probe')
    lifecycle_modes.add_argument('--controller-lifecycle-input-discard', action='store_true',
                   help='RUN-only native STATE press/Update/release with bounded getter routes; disposable input probe')
    lifecycle_modes.add_argument('--controller-recurring-readiness', action='store_true',
                   help='RUN-only main/worker readiness observations at ticks 5..end; full STOP evidence required')
    lifecycle_modes.add_argument('--controller-recurring-tail-readiness', action='store_true',
                   help='RUN-only worker-tail readiness at ticks 5..end and hook restoration at end+1; full STOP required')
    lifecycle_modes.add_argument('--controller-physics-discard', action='store_true',
                   help='RUN-only bounded retained P2 physics passes; abort on callbacks and discard process at worker tail')
    r.add_argument('--controller-physics-frames', type=int, default=1, metavar='N',
                   help='RUN-only bounded physics passes (1..180 scripted, 1..600 independent network; default 1); nondefault requires physics discard')
    r.add_argument('--player-lifetime-preload', action='store_true',
                   help='activate the bundled lifetime guard without binding objects during tail readiness')
    r.add_argument('--controller-recurring-end', type=int, metavar='TICK',
                   help='last readiness tick (5..1000, default 30); requires readiness mode and ticks > end')
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
    r.add_argument('--relay-port', type=int, help='use an external relay instead of starting one; '
                   'needs --relay-token and --local-slots')
    r.add_argument('--relay-token', help='the token of that relay (32 hex)')
    r.add_argument('--relay-host', help='external relay dotted IPv4 address (default 127.0.0.1)')
    r.add_argument('--arm-at', type=int, metavar='UTC_SECOND', help='external session: agreed future UTC second to load')
    r.add_argument('--external-player-guest', action='store_true', help='explicit single-local slot 2 physics guest; remote evidence remains pending')
    r.add_argument('--target-snapshot-sha256', help='declared remote session snapshot SHA256')
    r.add_argument('--target-protocol', type=int, choices=(3,), help='declared remote session protocol')
    r.add_argument('--target-source-commit', help='declared remote session source commit; must match this bundle')
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

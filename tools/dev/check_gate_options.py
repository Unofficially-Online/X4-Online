"""Check a native gate config's boot option bits against lua_boot's dependency rules.

The gate refuses a start with code 10 when an option bit lacks a prerequisite
bit. This recomputes the bit mask exactly as native_gate_probe.lua does and
evaluates every rule in lockstep_gate.c's lua_boot offline, so an overlay can
be checked before two X4 instances are launched. Options the probe arms after
the boot (not boot bits) are checked against the probe's own prerequisites.

A session config (native_gate_test with slot, port and token) is checked with its session defaults applied
(tools/dev/native_gate_defaults.lua through native_gate_defaults.py): an empty session config is the full default
stack; native_gate_defaults = false checks the bare config.
"""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_gate_defaults as defaults   # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
NAMED = {'REQUEST_OPTION': 1 << 52, 'SOLVER_OPTION': 1 << 50, 'REGISTRY_MD_BIT': 1 << 51}


def option_bits():
    probe = (ROOT/'tools/dev/native_gate_probe.lua').read_text()
    block = probe[probe.index('local options=(cfg.native_gate_serial_pathing'):probe.index('if diagnostic then assert(options==')]
    return [(k, int(v)) for k, v in re.findall(r'cfg\.(native_gate_\w+) and (\d+)', block)]


def rules():
    source = (ROOT/'src/native/engine/lockstep_gate.c').read_text()
    start = source.index('if(!boot_option_mask_valid(options) ||')
    end = source.index('{p_lua_pushnumber(L,result);return 1;}', start)
    lines = [line.strip().rstrip('|').strip() for line in source[start+3:end].split('\n')][1:]
    result = []
    for line in lines:
        expr = line.replace('(uint64_t)options', 'o').replace('ULL', '')
        expr = expr.replace('&&', ' and ').replace('||', ' or ').replace('!(', 'not (')
        for name, value in NAMED.items(): expr = expr.replace(name, str(value))
        expr = expr.rstrip(')').rstrip()
        while expr.count('(') > expr.count(')'): expr += ')'
        result.append((line, expr))
    return result


# Options armed by native_gate_probe.lua after the boot: accepted values and the
# boot bits whose Move wrapper they count in (any one of them).
PROBE_OPTIONS = {
    'native_gate_bullet_cadence': (('true', 'every', 'observe'), ('native_gate_bullet_rng', 'native_gate_bullet_diag'), 'bullet'),
    'native_gate_nav_cadence': (('true', 'far', 'every', 'observe'),
                                ('native_gate_nav_rng', 'native_gate_nav_diag', 'native_gate_nav_initial_refresh'), 'nav'),
    'native_gate_view_points': (('true', 'players', 'observe'), (), None),
    'native_gate_mass_traffic': (('true', 'sync', 'observe'), (), None),
    'native_gate_spawn_rng': (('key', 'observe'), (), None),
    'native_gate_region_pass': (('true', 'sync', 'observe'), (), None),
    'native_gate_view_rows': (('true',), (), None),
    'native_gate_view_triggers': (('block', 'observe'), (), None),
    'native_gate_view_points_source': (('player', 'ship'), (), None),
    'native_gate_view_points_mask': (tuple(str(i) for i in range(16)), (), None),
    'native_gate_region_camera': (('view',), (), None),
    'native_gate_region_fields': (('view',), (), None),
    'native_gate_mass_traffic_camera': (('view',), (), None),
    'native_gate_effect_camera': (('view',), (), None),
    'native_gate_highway_camera': (('view',), (), None),
    'native_gate_physics_scenes': (('viewpoints',), (), None),
    'native_gate_camera_context': (('view',), (), None),
    'native_gate_shape_refresh': (('view',), (), None),
    'native_gate_mining_camera': (('view',), (), None),
    'native_gate_xs_camera': (('view',), (), None),
    'native_gate_camera_probe': (('true',), (), None),
    'native_gate_highway_speed': (('key',), (), None),
    'native_gate_sound_rng': (('split',), (), None),
    'native_gate_view_scenes': (('true',), (), None),
    'native_gate_field_fill': (('view', 'block'), (), None),
    'native_gate_traffic_views': (('block', 'observe', 'view'), (), None),
    'native_gate_traffic_rows': (('true',), (), None),
    'native_gate_speak_timer': (('true',), (), None),
    'native_gate_speech_clock': (('true',), (), None),
    'native_gate_cutscene_ready': (('true',), (), None),
    'native_gate_viewpoint_policy': (('sector',), (), None),
    'native_gate_effect_rows': (('true',), (), None),
    'native_gate_motion_watch_orders': (('true',), (), None),
    'native_gate_motion_watch_digest': (('true',), (), None),
    'native_gate_rng_rows': (('true',), (), None),
    'native_gate_motion_watch_doors': (('true',), (), None),
    'native_gate_wreck_watch': (('true',), (), None),
    'native_gate_motion_watch_splines': (('true',), ('native_gate_spline_diag', 'native_gate_spline_refresh', 'native_gate_spline_join'), 'spline'),
    'native_gate_gravidar_digest': (('true',), (), None),
    'native_gate_nav_probe_init': (('true',), ('native_gate_nav_rng', 'native_gate_nav_diag', 'native_gate_nav_initial_refresh'), 'nav'),
    'native_gate_ship_sound_rng': (('true', 'split', 'observe'), (), None),
    'native_gate_player_timer': (('true',), (), None),
    'native_gate_nav_params_init': (('true',), ('native_gate_nav_rng', 'native_gate_nav_diag', 'native_gate_nav_initial_refresh'), 'nav'),
    'native_gate_nav_player': (('true',), ('native_gate_nav_rng', 'native_gate_nav_diag', 'native_gate_nav_initial_refresh'), 'nav'),
    'native_gate_attention_all': (('true',), ('native_gate_attention_rng', 'native_gate_attention_initial_refresh'), 'attention'),
}


# Probe options that need every one of the listed options (boot bits or keys).
PROBE_REQUIRES_ALL = {
    'native_gate_cutscene_ready': (('true',), ('native_gate_serial_pathing',)),
    'native_gate_actor_rng_all': (('true',), ('native_gate_world_identities', 'native_gate_agent_rng', 'native_gate_ship_rng')),
    'native_gate_created_load': (('true',), ('native_gate_world_identities', 'native_gate_created_identities')),
    'native_gate_gravidar_deadlines': (('true',), ('native_gate_world_identities', 'native_gate_gravidar_time_sync')),
    'native_gate_weapon_fire_rng': (('true',), ('native_gate_world_identities',)),
    'native_gate_warp_rng': (('true', 'split', 'observe'), ('native_gate_world_identities',)),
    'native_gate_transition_rows': (('true',), ('native_gate_world_identities',)),
    'native_gate_tempzone_order': (('true', 'order', 'observe'), ('native_gate_world_identities', 'native_gate_serial_pathing')),
    'native_gate_path_transforms': (('sim', 'observe'), ('native_gate_serial_pathing',)),
    'native_gate_body_pose': (('sim', 'observe'), ()),
    'native_gate_gravidar_posts': (('true',), ('native_gate_gravidar_key_all',)),
    'native_gate_gravidar_wide': (('true', 'wide', 'sort'), ()),
    'native_gate_shape_order': (('true', 'wait', 'sync'), ()),
    'native_gate_engine_refresh_rng': (('true',), ('native_gate_world_identities',)),
    'native_gate_portable_math': (('true',), ()),
    'native_gate_exact_reciprocal': (('true',), ()),
    'native_gate_fp_redirect_solver': (('shadow', 'redirect'), ()),  # and native_gate_fp_redirect (a mode, below)
    'native_gate_fp_redirect_drones': (('shadow', 'redirect'), ()),  # FR30; and native_gate_fp_redirect (below)
    'native_gate_fp_env': (('default', 'observe'), ()),
    'native_gate_decimal': (('true',), ()),
    'native_gate_tick_hz': (('60', '40', '30', '20'), ()),
    'native_gate_tick_pace': (('true',), ('native_gate_tick_hz',)),
    'native_gate_fast_reads': (('true',), ()),
    'native_gate_play_mode': (('true',), ()),
    'native_gate_build_rows': (('true',), ('native_gate_world_identities',)),
    'native_gate_economy_load_rng': (('true',), ('native_gate_world_identities',)),
    'native_gate_path_budget': (('25', '50', '100', '150', '200', '300', '500'), ('native_gate_serial_pathing',)),
    'native_gate_pass_stride': (('1', '2', '3', '4'), ()),
    'native_gate_event_rng': (('observe', 'key'), ('native_gate_world_identities', 'native_gate_event_order')),
    'native_gate_build_rng': (('true',), ('native_gate_world_identities',)),
    'native_gate_ui_cargo': (('block', 'observe'), ('native_gate_world_identities',)),
    'native_gate_supply_rows': (('true',), ('native_gate_world_identities',)),
    'native_gate_ui_lazy': (('block', 'observe', 'key'), ('native_gate_world_identities',)),
    'native_gate_ui_exports': (('observe', 'block'), ()),
    'native_gate_loadout_order': (('observe', 'canonical'), ()),
    'native_gate_order_canon': (('observe', 'canonical'), ()),
    'native_gate_pick_order': (('observe', 'canonical'), ()),
    'native_gate_macro_arena': (('true',), ()),
    'native_gate_nav_octree': (('observe', 'sync', 'deadline', 'agreed'), ('native_gate_world_identities',)),
    'native_gate_nav_octree_snapshot': (('true',), ('native_gate_world_identities',)),
    'native_gate_nav_mesh': (('observe', 'sync'), ('native_gate_world_identities',)),   # lockstep_nav_mesh.h, stop reason 186
    'native_gate_character_play': (('observe', 'sync'), ()),   # lockstep_character_play.h, stop reason 188
    'native_gate_body_order': (('sort', 'observe'),
                               ('native_gate_world_identities', 'native_gate_created_identities', 'native_gate_portable_created')),
    'native_gate_virtual_players': (('observe', 'construct'), ('native_gate_serial_pathing',)),
    'native_gate_spline_join_all': (('true',), ('native_gate_spline_join',)),
}


PREDICT_MODES = ('true', "'observe'", '"observe"', "'present'", '"present"', 'false')


def predict_rules(config_text, enabled):
    """native_gate_pilot_predict: a mode or a per-client list of modes; needs the
    piloting prototype, and 'present' needs the render RNG split's slot-10 wrapper."""
    failures = []
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_pilot_predict\s*=\s*([^\n;]+)', config_text):
        value = value.split('--')[0].strip()
        modes = [m.strip() for m in value[1:-1].split(',')] if value.startswith('{') and value.endswith('}') else [value]
        if value in ('false', 'nil'): continue
        if not modes or any(m not in PREDICT_MODES + ('nil',) for m in modes):
            failures.append(dict(rule="native_gate_pilot_predict must be true, 'observe', 'present' or a per-client list of them",
                                 bits=[value]))
            continue
        if 'native_gate_pilot_inputs' not in enabled:
            failures.append(dict(rule='native_gate_pilot_predict needs native_gate_pilot_inputs', bits=['native_gate_pilot_inputs']))
        if any('present' in m for m in modes) and 'native_gate_render_rng_split' not in enabled:
            failures.append(dict(rule="native_gate_pilot_predict 'present' needs native_gate_render_rng_split = true",
                                 bits=['native_gate_render_rng_split']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
            failures.append(dict(rule='native_gate_pilot_predict needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    return failures


HIT_POLICIES = ('true', "'shooter'", '"shooter"', "'victim'", '"victim"', "'first'", '"first"')


def hit_claim_rules(config_text, enabled):
    """native_gate_hit_claims: a player-versus-player policy (true = shooter); needs the
    piloting prototype on the wire, camera-independent bullet tests and world identities."""
    failures = []
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_hit_claims\s*=\s*([^\n;]+)', config_text):
        value = value.split('--')[0].strip()
        if value in ('false', 'nil'): continue
        if value not in HIT_POLICIES:
            failures.append(dict(rule="native_gate_hit_claims must be true, 'shooter', 'victim' or 'first'", bits=[value]))
            continue
        for name in ('native_gate_pilot_inputs', 'native_gate_world_identities'):
            if name not in enabled:
                failures.append(dict(rule=f'native_gate_hit_claims needs {name}', bits=[name]))
        if not re.search(r"X4LAN_CONFIG\.native_gate_pilot_source\s*=\s*['\"]wire['\"]", config_text):
            failures.append(dict(rule="native_gate_hit_claims needs native_gate_pilot_source = 'wire'", bits=['native_gate_pilot_source']))
        if not re.search(r"X4LAN_CONFIG\.native_gate_bullet_cadence\s*=\s*(true|['\"]every['\"])", config_text):
            failures.append(dict(rule="native_gate_hit_claims needs native_gate_bullet_cadence = 'every'", bits=['native_gate_bullet_cadence']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
            failures.append(dict(rule='native_gate_hit_claims needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    return failures


FIRE_MODES = ('true', "'record'", '"record"', "'script'", '"script"')


def pilot_fire_rules(config_text, enabled):
    """native_gate_pilot_fire: 'record' (true) or 'script'; needs the piloting prototype,
    the all-ship RNG keys (launch draws) and the 'every' bullet cadence (creation-frame test)."""
    failures = []
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_pilot_fire\s*=\s*([^\n;]+)', config_text):
        value = value.split('--')[0].strip()
        if value in ('false', 'nil'): continue
        if value not in FIRE_MODES:
            failures.append(dict(rule="native_gate_pilot_fire must be true, 'record' or 'script'", bits=[value]))
            continue
        for name in ('native_gate_pilot_inputs', 'native_gate_actor_rng_all', 'native_gate_world_identities'):
            if name not in enabled:
                failures.append(dict(rule=f'native_gate_pilot_fire needs {name}', bits=[name]))
        slots = lua_value(config_text, 'native_gate_pilot_fire_slots')
        if slots is not None and slots != 'nil' and value not in ("'script'", '"script"'):
            failures.append(dict(rule="native_gate_pilot_fire_slots needs native_gate_pilot_fire = 'script'", bits=[value]))
        if not re.search(r"X4LAN_CONFIG\.native_gate_bullet_cadence\s*=\s*(true|['\"]every['\"])", config_text):
            failures.append(dict(rule="native_gate_pilot_fire needs native_gate_bullet_cadence = 'every'", bits=['native_gate_bullet_cadence']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
            failures.append(dict(rule='native_gate_pilot_fire needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    return failures


CAPTURE_SOURCES = ("'native'", '"native"', "'device'", '"device"', "'stock'", '"stock"', 'false', 'nil')


def capture_source_rules(config_text, enabled):
    """native_gate_pilot_capture_source: 'native' (X4's own controls, stop reason 113)
    or 'device', or a per-client list; 'native' needs the piloting prototype with
    local suppression and the capture (or the wire source)."""
    failures = []
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_pilot_capture_source\s*=\s*([^\n;]+)', config_text):
        value = value.split('--')[0].strip()
        modes = [m.strip() for m in value[1:-1].split(',')] if value.startswith('{') and value.endswith('}') else [value]
        if value in ('false', 'nil'): continue
        if not modes or any(m not in CAPTURE_SOURCES for m in modes):
            failures.append(dict(rule="native_gate_pilot_capture_source must be 'native', 'device', 'stock' or a per-client list of them",
                                 bits=[value]))
            continue
        if any('stock' in m for m in modes) and 'native_gate_pilot_stock' not in enabled:
            failures.append(dict(rule="native_gate_pilot_capture_source 'stock' needs native_gate_pilot_stock = true",
                                 bits=['native_gate_pilot_stock']))
        if not any('native' in m for m in modes): continue
        for name in ('native_gate_pilot_inputs', 'native_gate_pilot_suppress_local'):
            if name not in enabled:
                failures.append(dict(rule=f"native_gate_pilot_capture_source 'native' needs {name}", bits=[name]))
        wire = re.search(r"X4LAN_CONFIG\.native_gate_pilot_source\s*=\s*['\"]wire['\"]", config_text)
        if not wire and 'native_gate_pilot_capture' not in enabled:
            failures.append(dict(rule="native_gate_pilot_capture_source 'native' needs native_gate_pilot_capture or the wire source",
                                 bits=['native_gate_pilot_capture']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
            failures.append(dict(rule='native_gate_pilot_capture_source needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    return failures


def lua_value(config_text, option):
    m = re.search(r'X4LAN_CONFIG\.' + option + r'\s*=\s*([^\n;]+)', config_text)
    return m[1].split('--')[0].strip() if m else None


def lua_modes(value):
    if value is None:
        return []
    return [m.strip().strip('\'"') for m in value[1:-1].split(',')] if value.startswith('{') and value.endswith('}') else [value.strip('\'"')]


def stock_slots(config_text):
    """The stock slots (1-based) of native_gate_pilot_stock_slots: slot 1 alone when absent,
    every pilot slot for 'all', else the listed slots; None when the value is invalid."""
    value = lua_value(config_text, 'native_gate_pilot_stock_slots')
    count = len(lua_modes(lua_value(config_text, 'native_gate_pilot_slots')))
    if value is None or value == 'nil':
        return [1]
    if value.strip('\'"') == 'all':
        return list(range(1, count+1))
    items = lua_modes(value)
    if not value.startswith('{') or not items or not all(i.isdigit() for i in items):
        return None
    slots = [int(i) for i in items]
    if 1 not in slots or len(set(slots)) != len(slots) or any(not 1 <= s <= count for s in slots):
        return None
    return sorted(slots)


def pilot_stock_rules(config_text, enabled):
    """native_gate_pilot_stock (lockstep_pilot_stock.h, stop reason 155): the stock slots fly
    through X4's own player control path on every client: slot 1 always,
    native_gate_pilot_stock_slots = 'all' or a list adds slots 2..N (lockstep_pilot_slots.h,
    stop reason 161). Needs the wire piloting prototype with local suppression and the
    snapshot; a stock slot's client captures (capture source 'stock', no device), every other
    client samples its own slot ('device' with a device); a stock slot has no pilot record, so
    its client's prediction stays off, pilot fire reads the stock record's trigger (hit claims
    are keyed by the slot ships, not by records, and may run with it)."""
    value = lua_value(config_text, 'native_gate_pilot_stock')
    if value is None or value in ('false', 'nil'):
        return []
    if value != 'true':
        return [dict(rule='native_gate_pilot_stock must be true', bits=[value])]
    failures = []
    for name in ('native_gate_pilot_inputs', 'native_gate_pilot_suppress_local'):
        if name not in enabled:
            failures.append(dict(rule=f'native_gate_pilot_stock needs {name}', bits=[name]))
    if not re.search(r"X4LAN_CONFIG\.native_gate_pilot_source\s*=\s*['\"]wire['\"]", config_text):
        failures.append(dict(rule="native_gate_pilot_stock needs native_gate_pilot_source = 'wire'", bits=['native_gate_pilot_source']))
    if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
        failures.append(dict(rule='native_gate_pilot_stock needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    slots = stock_slots(config_text)
    if slots is None:
        failures.append(dict(rule="native_gate_pilot_stock_slots must be 'all' or a list of pilot slots holding 1",
                             bits=['native_gate_pilot_stock_slots']))
        slots = [1]
    sources = lua_modes(lua_value(config_text, 'native_gate_pilot_capture_source'))
    devices = lua_modes(lua_value(config_text, 'native_gate_pilot_device'))
    wanted = ['stock' if i+1 in slots else 'device' for i in range(len(sources))]
    if len(sources) < 2 or len(sources) < max(slots) or sources != wanted:
        failures.append(dict(rule="native_gate_pilot_stock needs native_gate_pilot_capture_source 'stock' exactly on the stock slots' "
                                  "clients and 'device' on the others (e.g. {'stock','device'})", bits=['native_gate_pilot_capture_source']))
    elif len(devices) < len(sources) or any((d in ('false', 'nil')) != (m == 'stock') for d, m in zip(devices, sources)):
        failures.append(dict(rule='native_gate_pilot_stock needs native_gate_pilot_device = false on the stock slots\' clients and a device '
                                  'on the others (e.g. {false,<device>})', bits=['native_gate_pilot_device']))
    predict = lua_modes(lua_value(config_text, 'native_gate_pilot_predict'))
    if any(i+1 in slots and p not in ('false', 'nil') for i, p in enumerate(predict)):
        failures.append(dict(rule='native_gate_pilot_stock excludes prediction on the stock slots\' clients', bits=['native_gate_pilot_predict']))
    script = lua_value(config_text, 'native_gate_pilot_stock_script')
    names = tuple(q + name + q for q in '\'"' for name in ('demo', 'demo2', 'demo3', 'demo4', 'aim'))
    if script is not None and script != 'nil':
        if script.startswith('{{{') or (script.startswith('{') and '{' in script[1:] and not script.startswith('{{')):
            pass   # one event list per client ({{{tick,kind,id,value},...},false}): the probe checks each event
        elif script.startswith('{') and not script.startswith('{{'):
            if any(entry not in names+('false', 'nil') for entry in [e.strip() for e in script.strip('{}').split(',') if e.strip()]):
                failures.append(dict(rule="native_gate_pilot_stock_script per client must be {'demo'|'demo2'|'demo3'|'demo4'|'aim'|false,...}", bits=[script]))
        elif script not in names and not script.startswith('{{'):
            failures.append(dict(rule="native_gate_pilot_stock_script must be 'demo', 'demo2'..'demo4', 'aim', a list of {tick,kind,id,value} or one per client",
                                 bits=[script]))
    return failures


def pilot_present_rules(config_text, enabled):
    """native_gate_pilot_present (lockstep_pilot_present.h, stop reason 162): needs stock slots and
    the render split (slot 10 runs in the presentation scope); the camera is X4's own on the
    client's slot, so the camera follow and anchor stay off."""
    value = lua_value(config_text, 'native_gate_pilot_present')
    if value is None or value in ('false', 'nil'):
        return []
    if value not in ('true', "'camera'", '"camera"'):
        return [dict(rule='native_gate_pilot_present must be true or camera', bits=[value])]
    failures = []
    if 'native_gate_pilot_stock' not in enabled or lua_value(config_text, 'native_gate_pilot_stock_slots') in (None, 'nil'):
        failures.append(dict(rule='native_gate_pilot_present needs native_gate_pilot_stock_slots', bits=['native_gate_pilot_stock_slots']))
    if 'native_gate_render_rng_split' not in enabled:
        failures.append(dict(rule='native_gate_pilot_present needs native_gate_render_rng_split', bits=['native_gate_render_rng_split']))
    for name in ('native_gate_pilot_camera_follow', 'native_gate_camera_anchor'):
        other = lua_value(config_text, name)
        if other is not None and other not in ('false', 'nil'):
            failures.append(dict(rule=f'native_gate_pilot_present excludes {name}', bits=[name]))
    return failures


OWN_PREDICT_MODES = ('true', 'observe', 'present', 'swap', 'false', 'nil')


def own_predict_rules(config_text, enabled):
    """native_gate_own_predict (lockstep_own_predict.h, stop reason 184): a mode or a per-client list of
    modes on the stock slots' clients (each predicts from its own captured stock records); 'present' draws
    in slot 10 through the render split ('swap' too: the same swap with the simulated pose). The tuning keys need
    it and stay in range."""
    failures = []
    value = lua_value(config_text, 'native_gate_own_predict')
    keys = {'native_gate_own_predict_others': None, 'native_gate_own_predict_tau': (0, 2000),
            'native_gate_own_predict_horizon': (1, 30), 'native_gate_own_predict_rows': (0, 3600)}
    if value is None or value == 'nil':
        for name in keys:
            if lua_value(config_text, name) not in (None, 'nil'):
                failures.append(dict(rule=f'{name} needs native_gate_own_predict', bits=['native_gate_own_predict']))
        return failures
    modes = lua_modes(value)
    if not modes or any(m not in OWN_PREDICT_MODES for m in modes):
        return [dict(rule="native_gate_own_predict must be true, 'observe', 'present', 'swap' or a per-client list of them", bits=[value])]
    active = [i+1 for i, m in enumerate(modes) if m not in ('false', 'nil')]
    if not active:
        return failures
    if 'native_gate_pilot_stock' not in enabled:
        failures.append(dict(rule='native_gate_own_predict needs native_gate_pilot_stock', bits=['native_gate_pilot_stock']))
    else:
        slots = stock_slots(config_text) or [1]
        sources = lua_modes(lua_value(config_text, 'native_gate_pilot_capture_source'))
        single = not value.startswith('{')
        clients = range(1, len(sources)+1) if single else active
        if any(c not in slots or (c <= len(sources) and sources[c-1] != 'stock') for c in clients):
            failures.append(dict(rule="native_gate_own_predict needs capture source 'stock' on every predicting client (a stock slot)",
                                 bits=['native_gate_pilot_capture_source', 'native_gate_pilot_stock_slots']))
    if ('present' in modes or 'swap' in modes) and 'native_gate_render_rng_split' not in enabled:
        failures.append(dict(rule="native_gate_own_predict 'present' needs native_gate_render_rng_split = true",
                             bits=['native_gate_render_rng_split']))
    for name, bounds in keys.items():
        option = lua_value(config_text, name)
        if option in (None, 'nil'):
            continue
        if bounds is None:
            if option not in ('true', 'false'):
                failures.append(dict(rule=f'{name} must be true or false', bits=[option]))
            continue
        try:
            number = float(option)
        except ValueError:
            number = None
        if number is None or number != int(number) or not bounds[0] <= number <= bounds[1]:
            failures.append(dict(rule=f'{name} must be a whole number {bounds[0]}..{bounds[1]}', bits=[option]))
    return failures


def pilot_death_rules(config_text, enabled):
    """native_gate_pilot_death (lockstep_pilot_death.h, stop reason 163): a stock session; the slot's ship
    comes back only with native_gate_pilot_respawn, a delay in whole ticks."""
    death = lua_value(config_text, 'native_gate_pilot_death')
    respawn = lua_value(config_text, 'native_gate_pilot_respawn')
    suit = lua_value(config_text, 'native_gate_pilot_death_suit')
    failures = []
    if suit is not None and suit not in ('false', 'nil'):
        if suit != 'true':
            failures.append(dict(rule='native_gate_pilot_death_suit must be true', bits=[suit]))
        if death != 'true':
            failures.append(dict(rule='native_gate_pilot_death_suit needs native_gate_pilot_death', bits=['native_gate_pilot_death']))
        if lua_value(config_text, 'native_gate_pilot_present') in (None, 'false', 'nil'):
            failures.append(dict(rule='native_gate_pilot_death_suit needs native_gate_pilot_present', bits=['native_gate_pilot_present']))
        if 'native_gate_pilot_ship_state' not in enabled:
            failures.append(dict(rule='native_gate_pilot_death_suit needs native_gate_pilot_ship_state', bits=['native_gate_pilot_ship_state']))
    if death is not None and death not in ('false', 'nil', 'true'):
        failures.append(dict(rule='native_gate_pilot_death must be true', bits=[death]))
    if death == 'true' and 'native_gate_pilot_stock' not in enabled:
        failures.append(dict(rule='native_gate_pilot_death needs native_gate_pilot_stock', bits=['native_gate_pilot_stock']))
    if respawn is not None and respawn not in ('false', 'nil'):
        if death != 'true':
            failures.append(dict(rule='native_gate_pilot_respawn needs native_gate_pilot_death', bits=['native_gate_pilot_death']))
        if not respawn.isdigit() or not 1 <= int(respawn) <= 8388607:   # PILOT_TICK_MAX, lockstep_pilot_inputs.h
            failures.append(dict(rule='native_gate_pilot_respawn must be a delay of 1..8388607 ticks', bits=[respawn]))
    return failures


def pilot_foot_rules(config_text, enabled):
    """native_gate_pilot_foot (lockstep_pilot_foot.h, stop reason 185): slot 1 gets up and walks through the stock
    record; a stock session with the UI replay and the UI guard (the docked menu's GetUp is claimed in the guard)."""
    foot = lua_value(config_text, 'native_gate_pilot_foot')
    failures = []
    if foot is None or foot in ('false', 'nil'):
        return failures
    if foot != 'true':
        failures.append(dict(rule='native_gate_pilot_foot must be true', bits=[foot]))
    if 'native_gate_pilot_stock' not in enabled:
        failures.append(dict(rule='native_gate_pilot_foot needs native_gate_pilot_stock', bits=['native_gate_pilot_stock']))
    for name in ('native_gate_ui_replay', 'native_gate_ui_guard'):
        if lua_value(config_text, name) in (None, 'false', 'nil'):
            failures.append(dict(rule=f'native_gate_pilot_foot needs {name}', bits=[name]))
    return failures


def pilot_foot_bodies_rules(config_text, enabled):
    """native_gate_pilot_foot_bodies (lockstep_pilot_foot_body.h, stop reason 185): slots 2..N's players in their seats'
    rooms; needs slot 1 on foot and the stock slots. With the death module its kill guard takes a body off before X4's
    kill reaches it (lockstep_pilot_foot_body.h)."""
    bodies = lua_value(config_text, 'native_gate_pilot_foot_bodies')
    failures = []
    if bodies is None or bodies in ('false', 'nil'):
        return failures
    if bodies != 'true':
        failures.append(dict(rule='native_gate_pilot_foot_bodies must be true', bits=[bodies]))
    if lua_value(config_text, 'native_gate_pilot_foot') != 'true':
        failures.append(dict(rule='native_gate_pilot_foot_bodies needs native_gate_pilot_foot', bits=['native_gate_pilot_foot']))
    if lua_value(config_text, 'native_gate_pilot_stock_slots') in (None, 'false', 'nil'):
        failures.append(dict(rule='native_gate_pilot_foot_bodies needs native_gate_pilot_stock_slots', bits=['native_gate_pilot_stock_slots']))
    return failures


def pilot_foot_slots_rules(config_text, enabled):
    """native_gate_pilot_foot_slots (lockstep_pilot_foot_slots.h, stop reason 185): slots 2..N get up and walk through
    their stock records in their scopes; needs slot 1 on foot and the stock slots. Bodies are optional (they leave the
    tree while anyone is off its seat); so is the death module (a walking slot's ship lost is no death)."""
    walk = lua_value(config_text, 'native_gate_pilot_foot_slots')
    failures = []
    if walk is None or walk in ('false', 'nil'):
        return failures
    if walk != 'true':
        failures.append(dict(rule='native_gate_pilot_foot_slots must be true', bits=[walk]))
    if lua_value(config_text, 'native_gate_pilot_foot') != 'true':
        failures.append(dict(rule='native_gate_pilot_foot_slots needs native_gate_pilot_foot', bits=['native_gate_pilot_foot']))
    if lua_value(config_text, 'native_gate_pilot_stock_slots') in (None, 'false', 'nil'):
        failures.append(dict(rule='native_gate_pilot_foot_slots needs native_gate_pilot_stock_slots', bits=['native_gate_pilot_stock_slots']))
    return failures


UI_REPLAY_FAMILIES = ('travel', 'target', 'orders', 'autopilot', 'dock', 'weapons', 'missiles', 'queue', 'events', 'flight', 'trade', 'missions', 'build',
                      'assets', 'conversation', 'faction')
UI_GUARD_GROUPS = ('flight', 'orders', 'fleet', 'trade', 'faction', 'assets', 'build', 'conflict', 'missions', 'events', 'conversation',
                   'session', 'cheat', 'editor')   # lockstep_ui_guard.h, in its order


def ui_replay_rules(config_text, enabled):
    """native_gate_ui_replay (lockstep_ui_replay.h, stop reason 164): true or a list of families (UI_REPLAY_FAMILIES). Every
    client captures its own slot, so every slot is a stock slot; client k's UI must be slot k's (the presentation);
    components travel as runtime IDs, so those must be the same on every client."""
    value = lua_value(config_text, 'native_gate_ui_replay')
    failures = []
    # The UI guard (lockstep_ui_guard.h): 'all' or a list of its groups; it rides the replay's local phases and pass.
    guard = lua_value(config_text, 'native_gate_ui_guard')
    if guard not in (None, 'nil', 'false'):
        groups = lua_modes(guard) if guard.startswith('{') else None
        if guard.strip("'\"") != 'all' and (not groups or any(g not in UI_GUARD_GROUPS for g in groups) or len(set(groups)) != len(groups)):
            failures.append(dict(rule='native_gate_ui_guard must be all or a list of ' + ', '.join(UI_GUARD_GROUPS), bits=[guard]))
        if value in (None, 'false', 'nil'):
            failures.append(dict(rule='native_gate_ui_guard needs native_gate_ui_replay', bits=['native_gate_ui_replay']))
    for option in ('native_gate_ui_replay_script', 'native_gate_ui_replay_fleet', 'native_gate_ui_replay_min_hull', 'native_gate_ui_trade_fund'):
        if lua_value(config_text, option) not in (None, 'nil', 'false') and value in (None, 'false', 'nil'):
            failures.append(dict(rule=f'{option} needs native_gate_ui_replay', bits=['native_gate_ui_replay']))
    # Test protection of the slot ships (native_gate_ui_replay.xml, MinHull): a whole hull percentage.
    hull = lua_value(config_text, 'native_gate_ui_replay_min_hull')
    if hull not in (None, 'nil') and not (hull.isdigit() and 1 <= int(hull) <= 100):
        failures.append(dict(rule='native_gate_ui_replay_min_hull must be a whole percentage 1..100', bits=[hull]))
    # Test options of the build families (native_gate_ui_build_core.lua): a plan per client, whole credits for the fund.
    names_now = lua_modes(value) if value and value.startswith('{') else []
    for option in ('native_gate_ui_build_script', 'native_gate_ui_build_fund'):
        if lua_value(config_text, option) not in (None, 'nil', 'false') and not ('build' in names_now or 'assets' in names_now):
            failures.append(dict(rule=f'{option} needs native_gate_ui_replay build or assets', bits=['native_gate_ui_replay']))
    fund = lua_value(config_text, 'native_gate_ui_build_fund')
    if fund not in (None, 'nil') and not (fund.isdigit() and 1 <= int(fund) <= 10**9):
        failures.append(dict(rule='native_gate_ui_build_fund must be whole credits 1..1e9', bits=[fund]))
    # The trade family's test fixture (lockstep_ui_trade.h): credits for every slot faction that is not `player`, once.
    fund = lua_value(config_text, 'native_gate_ui_trade_fund')
    if fund not in (None, 'nil'):
        if not (fund.isdigit() and 1 <= int(fund) <= 1000000000):
            failures.append(dict(rule='native_gate_ui_trade_fund must be whole credits 1..1000000000', bits=[fund]))
        if value not in (None, 'false', 'nil') and (value == 'true' or 'trade' not in (lua_modes(value) or [])):
            failures.append(dict(rule='native_gate_ui_trade_fund needs the trade family', bits=[value]))
    if value is None or value in ('false', 'nil'):
        return failures
    if value != 'true':
        names = lua_modes(value)
        if not value.startswith('{') or not names or any(n not in UI_REPLAY_FAMILIES for n in names) or len(set(names)) != len(names):
            return failures + [dict(rule='native_gate_ui_replay must be true or a list of ' + ', '.join(UI_REPLAY_FAMILIES), bits=[value])]
        # Missiles fly at the slot's soft target (the target family) from the launchers pilot fire leases (stop 164 otherwise).
        if 'missiles' in names and 'target' not in names:
            failures.append(dict(rule='native_gate_ui_replay missiles needs the target family', bits=[value]))
        if 'missiles' in names and lua_value(config_text, 'native_gate_pilot_fire') in (None, 'false', 'nil'):
            failures.append(dict(rule='native_gate_ui_replay missiles needs native_gate_pilot_fire', bits=['native_gate_pilot_fire']))
        # The build and assets families (lockstep_ui_build.h) claim the UI guard's hooks.
        if ('build' in names or 'assets' in names) and guard in (None, 'nil', 'false'):
            failures.append(dict(rule='native_gate_ui_replay build/assets need native_gate_ui_guard', bits=['native_gate_ui_guard']))
        # The faction family (lockstep_ui_faction.h) claims the UI guard's hooks too.
        if 'faction' in names and guard in (None, 'nil', 'false'):
            failures.append(dict(rule='native_gate_ui_replay faction needs native_gate_ui_guard', bits=['native_gate_ui_guard']))
        # Missions and conversations (lockstep_ui_missions.h): the guard's hook captures them, slots 2+ are its refusals.
        guard_text = (guard or '').strip('\'"')
        guard_groups = set(UI_GUARD_GROUPS) if guard_text == 'all' else set(lua_modes(guard) or ()) if guard and guard.startswith('{') else set()
        for family in ('missions', 'conversation'):
            if family in names and family not in guard_groups:
                failures.append(dict(rule=f'native_gate_ui_replay {family} needs native_gate_ui_guard with the {family} group', bits=['native_gate_ui_guard']))
    if lua_value(config_text, 'native_gate_pilot_stock') != 'true' or (lua_value(config_text, 'native_gate_pilot_stock_slots') or '').strip('\'"') != 'all':
        failures.append(dict(rule="native_gate_ui_replay needs native_gate_pilot_stock_slots = 'all'", bits=['native_gate_pilot_stock_slots']))
    if lua_value(config_text, 'native_gate_pilot_present') in (None, 'false', 'nil'):
        failures.append(dict(rule='native_gate_ui_replay needs native_gate_pilot_present', bits=['native_gate_pilot_present']))
    rid = lua_value(config_text, 'native_gate_runtime_ids')
    if rid is None or rid.strip('\'"') in ('false', 'nil', 'observe'):
        failures.append(dict(rule='native_gate_ui_replay needs native_gate_runtime_ids in set mode', bits=['native_gate_runtime_ids']))
    if 'native_gate_world_identities' not in enabled:
        failures.append(dict(rule='native_gate_ui_replay needs native_gate_world_identities', bits=['native_gate_world_identities']))
    return failures


def player_settings_rules(config_text, enabled):
    """native_gate_player_settings_per_slot (lockstep_player_settings.h, stop reason 164): true or absent; it rides the UI
    replay's faction family. native_gate_player_settings_spawn = {tick, 'macro'}: the spawn check, with the key."""
    value = lua_value(config_text, 'native_gate_player_settings_per_slot')
    spawn = lua_value(config_text, 'native_gate_player_settings_spawn')
    failures = []
    on = value not in (None, 'false', 'nil')
    if on:
        if value != 'true':
            failures.append(dict(rule='native_gate_player_settings_per_slot must be true', bits=[value]))
        replay = lua_value(config_text, 'native_gate_ui_replay') or ''
        if 'faction' not in (lua_modes(replay) if replay.startswith('{') else []):
            failures.append(dict(rule='native_gate_player_settings_per_slot needs native_gate_ui_replay faction', bits=['native_gate_ui_replay']))
    if spawn not in (None, 'false', 'nil'):
        if not on:
            failures.append(dict(rule='native_gate_player_settings_spawn needs native_gate_player_settings_per_slot',
                                 bits=['native_gate_player_settings_per_slot']))
        m = re.fullmatch(r"\{\s*([0-9]{1,6})\s*,\s*(['\"])([A-Za-z0-9_]+)\2\s*\}", spawn)
        if not m or int(m[1]) < 1:
            failures.append(dict(rule="native_gate_player_settings_spawn must be {tick, 'macro'}", bits=[spawn]))
    return failures


def crash_note_rules(config_text, enabled):
    """native_gate_crash_note (lockstep_crash_note.h): true or absent."""
    value = lua_value(config_text, 'native_gate_crash_note')
    if value is not None and value not in ('true', 'false', 'nil'):
        return [dict(rule='native_gate_crash_note must be true', bits=[value])]
    return []


def render_fps_rules(config_text, enabled):
    """native_gate_render_fps (lockstep_render_frames.h): an integer above the tick rate (at most 360) and
    every module a render-only frame relies on; the gate also caps the rate at X4's own frame-rate target."""
    value = lua_value(config_text, 'native_gate_render_fps')
    if value is None or value in ('false', 'nil'):
        return []
    failures = []
    hz = lua_value(config_text, 'native_gate_tick_hz')
    if not re.fullmatch(r'[0-9]{1,3}', value) or not hz or not re.fullmatch(r'[0-9]{1,3}', hz) or not int(hz) < int(value) <= 360:
        failures.append(dict(rule='native_gate_render_fps must be an integer above native_gate_tick_hz, at most 360', bits=[value, hz]))
    missing = [name for name in ('native_gate_serial_pathing', 'native_gate_radar_serial', 'native_gate_render_rng_split') if name not in enabled]
    for name in ('native_gate_mass_traffic', 'native_gate_region_pass'):
        if (lua_value(config_text, name) or '').strip('\'"') not in ('true', 'sync'):
            missing.append(name + " = 'sync'")
    lookahead = lua_value(config_text, 'native_gate_lookahead')
    if not lookahead or not re.fullmatch(r'[1-8]', lookahead):
        missing.append('native_gate_lookahead')
    if 'native_gate_runtime_ids' in enabled and 'native_gate_runtime_ids_local' not in enabled:
        missing.append('native_gate_runtime_ids_local')
    if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
        missing.append('native_gate_snapshot_sha')
    if missing:
        failures.append(dict(rule='native_gate_render_fps needs serial pathing, radar serial, the render RNG split, '
                                  'mass traffic and region pass sync, the lookahead wire, the snapshot boot '
                                  'and, with runtime IDs, their local lists', bits=missing))
    return failures


def render_display_rules(config_text, enabled):
    """native_gate_render_camera / native_gate_render_motion (lockstep_render_display.h): true, 'extrapolate' or
    'interpolate', each on top of native_gate_render_fps; native_gate_render_display_rows needs one of them."""
    failures = []
    fps = lua_value(config_text, 'native_gate_render_fps')
    armed = []
    for name in ('native_gate_render_camera', 'native_gate_render_motion'):
        value = lua_value(config_text, name)
        if value is None or value in ('false', 'nil'):
            continue
        armed.append(name)
        if value not in ('true', "'extrapolate'", '"extrapolate"', "'interpolate'", '"interpolate"'):
            failures.append(dict(rule=f"{name} must be true, 'extrapolate' or 'interpolate'", bits=[value]))
        if fps is None or fps in ('false', 'nil'):
            failures.append(dict(rule=f'{name} needs native_gate_render_fps', bits=['native_gate_render_fps']))
    rows = lua_value(config_text, 'native_gate_render_display_rows')
    if rows is not None and rows not in ('false', 'nil'):
        if rows != 'true':
            failures.append(dict(rule='native_gate_render_display_rows must be true', bits=[rows]))
        if not armed:
            failures.append(dict(rule='native_gate_render_display_rows needs native_gate_render_camera or native_gate_render_motion',
                                 bits=['native_gate_render_camera', 'native_gate_render_motion']))
    return failures


HYBRID_KEYS = re.compile(r'(owners|tick|poses)\s*=\s*(\{[^{}]*\}|[^,{}]+)')


def hybrid_rules(config_text, enabled):
    """native_gate_hybrid = {owners={k,...}, tick=T, poses=P} (lockstep_hybrid.h, stop reason 187): owner slots 1..4
    among the pilot slots, each once, T >= 2, P 0..5 (default 4); view points 'players', pilot slots and the protocol 3
    input wire (the 'hybrid' input section). The census filter needs native_gate_world_hash_stride (not required)."""
    value = lua_value(config_text, 'native_gate_hybrid')
    if value is None or value in ('nil', 'false'):
        return []
    found = HYBRID_KEYS.findall(value) if value.startswith('{') and value.endswith('}') else []
    rest = HYBRID_KEYS.sub('', value[1:-1]).replace(',', '').strip() if found else value
    values = {k: v.strip() for k, v in found}
    owners = [o.strip() for o in values.get('owners', '{}')[1:-1].split(',') if o.strip()] if 'owners' in values else []
    tick, poses = values.get('tick', ''), values.get('poses', '4')
    if (not found or rest or len(values) != len(found) or not owners or not all(o.isdigit() for o in owners) or
            len(set(owners)) != len(owners) or any(not 1 <= int(o) <= 4 for o in owners) or
            not re.fullmatch(r'[0-9]{1,7}', tick) or int(tick) < 2 or not re.fullmatch(r'[0-5]', poses)):
        return [dict(rule='native_gate_hybrid must be {owners={k,...},tick=T,poses=P}: owner slots 1..4 once each, T >= 2, P 0..5',
                     bits=[value])]
    failures = []
    if (lua_value(config_text, 'native_gate_view_points') or '').strip('\'"') not in ('players', 'true'):
        failures.append(dict(rule="native_gate_hybrid needs native_gate_view_points = 'players'", bits=['native_gate_view_points']))
    pilots = len(lua_modes(lua_value(config_text, 'native_gate_pilot_slots')))
    if not pilots or any(int(o) > pilots for o in owners):
        failures.append(dict(rule='native_gate_hybrid owners must be pilot slots (native_gate_pilot_slots)', bits=['native_gate_pilot_slots']))
    if not re.search(r'X4LAN_CONFIG\.native_gate_clients\s*=\s*(?:[2-8]\b|@X4LANE_CLIENTS@)', config_text):
        failures.append(dict(rule='native_gate_hybrid needs the protocol 3 input wire (native_gate_clients)', bits=['native_gate_clients']))
    return failures


def probe_rules(config_text, enabled):
    """Prerequisites of options native_gate_probe.lua arms after the boot."""
    failures = []
    for rule in (predict_rules, hit_claim_rules, pilot_fire_rules, capture_source_rules, pilot_stock_rules, pilot_present_rules,
                 pilot_death_rules, pilot_foot_rules, pilot_foot_bodies_rules, pilot_foot_slots_rules, ui_replay_rules,
                 player_settings_rules, crash_note_rules, render_fps_rules, render_display_rules, own_predict_rules, hybrid_rules):
        failures += rule(config_text, enabled)
    if lua_value(config_text, 'native_gate_pilot_stock_script') is not None and 'native_gate_pilot_stock' not in enabled:
        failures.append(dict(rule='native_gate_pilot_stock_script needs native_gate_pilot_stock', bits=['native_gate_pilot_stock']))
    # Virtual players step 3 spike: the construction tick belongs to 'construct' and is an admitted tick 2..18000.
    due = lua_value(config_text, 'native_gate_virtual_players_construct_tick')
    if due is not None and due != 'nil':
        if (lua_value(config_text, 'native_gate_virtual_players') or '').strip('\'"') != 'construct':
            failures.append(dict(rule="native_gate_virtual_players_construct_tick needs native_gate_virtual_players = 'construct'",
                                 bits=['native_gate_virtual_players']))
        if not re.fullmatch(r'[0-9]{1,5}', due) or not 2 <= int(due) <= 18000:
            failures.append(dict(rule='native_gate_virtual_players_construct_tick must be an admitted tick 2..18000', bits=[due]))
    for option, (values, required) in PROBE_REQUIRES_ALL.items():
        for value in re.findall(r'X4LAN_CONFIG\.' + option + r'\s*=\s*([^\s;]+)', config_text):
            value = value.strip('\'"')
            if value in ('false', 'nil'): continue
            if value not in values:
                failures.append(dict(rule=f'{option} must be {", ".join(values)}', bits=[value]))
            missing = [name for name in required if name not in enabled]
            if missing:
                failures.append(dict(rule=f'{option} needs {", ".join(required)}', bits=missing))
            if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
                failures.append(dict(rule=f'{option} needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    for option, (values, modules, wrapper) in PROBE_OPTIONS.items():
        for value in re.findall(r'X4LAN_CONFIG\.' + option + r'\s*=\s*([^\s;]+)', config_text):
            value = value.strip('\'"')
            if value == 'false': continue
            if value not in values:
                failures.append(dict(rule=f'{option} must be {", ".join(values)}', bits=[value]))
            if modules and not enabled & set(modules):
                failures.append(dict(rule=f'{option} counts in the {wrapper} Move wrapper', bits=[' or '.join(modules)]))
            if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
                failures.append(dict(rule=f'{option} needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    if re.search(r'X4LAN_CONFIG\.native_gate_nav_probe_init\s*=\s*true\b', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_nav_cadence\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_nav_probe_init needs native_gate_nav_cadence', bits=['native_gate_nav_cadence']))
    if re.search(r'X4LAN_CONFIG\.native_gate_nav_params_init\s*=\s*true\b', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_nav_cadence\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_nav_params_init needs native_gate_nav_cadence', bits=['native_gate_nav_cadence']))
    if re.search(r'X4LAN_CONFIG\.native_gate_nav_octree\s*=\s*[\'"]agreed[\'"]', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_clients\s*=\s*(?:[2-8]\b|@X4LANE_CLIENTS@)', config_text):
        failures.append(dict(rule="native_gate_nav_octree = 'agreed' needs the protocol 3 input wire (native_gate_clients)",
                             bits=['native_gate_clients']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_nav_octree_sites\s*=\s*([^\s;]+)', config_text):
        value = value.strip('\'"')
        if value in ('false', 'nil'): continue
        if value != 'true':
            failures.append(dict(rule='native_gate_nav_octree_sites must be true', bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_nav_octree\s*=\s*[\'"](deadline|agreed)[\'"]', config_text):
            failures.append(dict(rule="native_gate_nav_octree_sites needs native_gate_nav_octree = 'deadline' or 'agreed'",
                                 bits=['native_gate_nav_octree']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_nav_octree_delay\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'[1-9][0-9]{0,3}', value) or int(value) > 3600:
            failures.append(dict(rule='native_gate_nav_octree_delay must be a tick count 1..3600', bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_nav_octree\s*=\s*[\'"]deadline[\'"]', config_text):
            failures.append(dict(rule="native_gate_nav_octree_delay needs native_gate_nav_octree = 'deadline'", bits=['native_gate_nav_octree']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_command_order\s*=\s*([^\s;]+)', config_text):
        value = value.strip('\'"')
        if value in ('false', 'nil'): continue
        if value not in ('observe', 'sort'):
            failures.append(dict(rule='native_gate_command_order must be observe or sort', bits=[value]))
        if 'native_gate_serial_pathing' not in enabled:
            failures.append(dict(rule='native_gate_command_order needs native_gate_serial_pathing', bits=['native_gate_serial_pathing']))
        if value == 'sort' and 'native_gate_travel_snapshot' not in enabled:
            failures.append(dict(rule='native_gate_command_order sort needs native_gate_travel_snapshot (the late-splice join)',
                                 bits=['native_gate_travel_snapshot']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
            failures.append(dict(rule='native_gate_command_order needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_tempzone_order\s*=\s*([^\s;]+)', config_text):
        if value.strip('\'"') in ('true', 'order') and 'native_gate_travel_snapshot' not in enabled:
            failures.append(dict(rule='native_gate_tempzone_order order needs native_gate_travel_snapshot (the late-splice join)',
                                 bits=['native_gate_travel_snapshot']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_gravidar_wide\s*=\s*([^\s;]+)', config_text):
        missing = [name for name in ('native_gate_radar_serial', 'native_gate_world_identities') if name not in enabled]
        if value.strip('\'"') == 'sort' and missing:
            failures.append(dict(rule='native_gate_gravidar_wide sort needs native_gate_radar_serial, native_gate_world_identities '
                                      '(the gated Gravidar publish, zone keys)', bits=missing))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_draw_watch\s*=\s*([^\n;]+)', config_text):
        value = value.split('--')[0].strip()
        if value in ('false', 'nil'): continue
        ids = re.findall(r"['\"]([0-9a-f]{16})['\"]", value)
        if not (value.startswith('{') and value.endswith('}')) or not 1 <= len(ids) <= 4 or \
           len(ids) != len([v for v in value[1:-1].split(',') if v.strip()]):
            failures.append(dict(rule='native_gate_draw_watch must be a list of one to four 16-digit saved IDs', bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_actor_rng_all\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_draw_watch needs native_gate_actor_rng_all', bits=['native_gate_actor_rng_all']))
    if re.search(r'X4LAN_CONFIG\.native_gate_draw_watch_frame\s*=\s*(?!false\b|nil\b)\S', config_text):
        if not re.search(r'X4LAN_CONFIG\.native_gate_draw_watch_frame\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_draw_watch_frame must be true', bits=['native_gate_draw_watch_frame']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_seed_rng\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_draw_watch_frame needs native_gate_seed_rng', bits=['native_gate_seed_rng']))
        if re.search(r'X4LAN_CONFIG\.native_gate_draw_watch\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_draw_watch_frame excludes native_gate_draw_watch', bits=['native_gate_draw_watch']))
    if re.search(r'X4LAN_CONFIG\.native_gate_actor_rng_rows\s*=\s*true\b', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_actor_rng_all\s*=\s*true\b', config_text):
        failures.append(dict(rule='native_gate_actor_rng_rows needs native_gate_actor_rng_all', bits=['native_gate_actor_rng_all']))
    if re.search(r'X4LAN_CONFIG\.native_gate_command_order_rows\s*=\s*true\b', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_command_order\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_command_order_rows needs native_gate_command_order', bits=['native_gate_command_order']))
    if re.search(r'X4LAN_CONFIG\.native_gate_nav_player\s*=\s*true\b', config_text):
        if not re.search(r'X4LAN_CONFIG\.native_gate_nav_cadence\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_nav_player needs native_gate_nav_cadence', bits=['native_gate_nav_cadence']))
        if re.search(r'X4LAN_CONFIG\.native_gate_player_observer\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_nav_player excludes native_gate_player_observer (one owner of PlayerController slot 33)',
                                 bits=['native_gate_player_observer']))
    if re.search(r'X4LAN_CONFIG\.native_gate_viewpoint_policy\s*=\s*(?!false\b|nil\b)\S', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_(?:mass_traffic|highway)_camera\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_viewpoint_policy needs native_gate_mass_traffic_camera or native_gate_highway_camera',
                             bits=['native_gate_mass_traffic_camera', 'native_gate_highway_camera']))
    if re.search(r'X4LAN_CONFIG\.native_gate_camera_anchor\s*=\s*(?!false\b|nil\b)\S', config_text):
        if not re.search(r'X4LAN_CONFIG\.native_gate_view_points\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_camera_anchor needs native_gate_view_points', bits=['native_gate_view_points']))
        if re.search(r'X4LAN_CONFIG\.native_gate_camera_offset\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_camera_anchor excludes native_gate_camera_offset', bits=['native_gate_camera_offset']))
        if re.search(r'X4LAN_CONFIG\.native_gate_pilot_camera_follow\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_camera_anchor excludes native_gate_pilot_camera_follow', bits=['native_gate_pilot_camera_follow']))
    if re.search(r'X4LAN_CONFIG\.native_gate_view_rows\s*=\s*true\b', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_view_points\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_view_rows needs native_gate_view_points', bits=['native_gate_view_points']))
    for option in ('native_gate_region_fields', 'native_gate_mass_traffic_camera', 'native_gate_effect_camera', 'native_gate_effect_rows',
                   'native_gate_highway_camera', 'native_gate_physics_scenes', 'native_gate_camera_context',
                   'native_gate_viewpoint_policy', 'native_gate_shape_refresh', 'native_gate_mining_camera', 'native_gate_xs_camera',
                   'native_gate_camera_probe', 'native_gate_highway_speed', 'native_gate_sound_rng', 'native_gate_view_scenes', 'native_gate_field_fill',
                   'native_gate_traffic_views', 'native_gate_traffic_rows'):
        if re.search(r'X4LAN_CONFIG\.' + option + r'\s*=\s*(?!false\b|nil\b)\S', config_text) and \
           not re.search(r'X4LAN_CONFIG\.native_gate_view_points\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule=f'{option} needs native_gate_view_points', bits=['native_gate_view_points']))
    for option in ('native_gate_region_fields', 'native_gate_effect_camera', 'native_gate_effect_rows', 'native_gate_highway_camera',
                   'native_gate_physics_scenes', 'native_gate_camera_context', 'native_gate_shape_refresh', 'native_gate_mining_camera',
                   'native_gate_xs_camera', 'native_gate_camera_probe', 'native_gate_highway_speed', 'native_gate_sound_rng', 'native_gate_view_scenes',
                   'native_gate_field_fill', 'native_gate_traffic_views', 'native_gate_traffic_rows'):
        if re.search(r'X4LAN_CONFIG\.' + option + r'\s*=\s*(?!false\b|nil\b)\S', config_text) and \
           not re.search(r'X4LAN_CONFIG\.native_gate_(?:mass_traffic|region_pass)\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule=f'{option} needs native_gate_mass_traffic or native_gate_region_pass',
                                 bits=['native_gate_mass_traffic', 'native_gate_region_pass']))
    if re.search(r'X4LAN_CONFIG\.native_gate_mass_traffic_camera\s*=\s*(?!false\b|nil\b)\S', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_mass_traffic\s*=\s*(?:true\b|[\'"]sync[\'"])', config_text):
        failures.append(dict(rule="native_gate_mass_traffic_camera needs native_gate_mass_traffic = 'sync'", bits=['native_gate_mass_traffic']))
    if re.search(r'X4LAN_CONFIG\.native_gate_pass_stride\s*=\s*(?!false\b|nil\b)\S', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_(?:mass_traffic|region_pass)\s*=\s*(?:true\b|[\'"]sync[\'"])', config_text):
        failures.append(dict(rule="native_gate_pass_stride needs native_gate_mass_traffic or native_gate_region_pass = 'sync'",
                             bits=['native_gate_mass_traffic', 'native_gate_region_pass']))
    if re.search(r'X4LAN_CONFIG\.native_gate_region_camera\s*=\s*(?!false\b|nil\b)\S', config_text):
        if not re.search(r'X4LAN_CONFIG\.native_gate_view_points\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_region_camera needs native_gate_view_points', bits=['native_gate_view_points']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_region_pass\s*=\s*(?:true\b|[\'"]sync[\'"])', config_text):
            failures.append(dict(rule="native_gate_region_camera needs native_gate_region_pass = 'sync'", bits=['native_gate_region_pass']))
    if re.search(r'X4LAN_CONFIG\.native_gate_view_points_source\s*=\s*(?!false\b|nil\b)\S', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_view_points\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_view_points_source needs native_gate_view_points', bits=['native_gate_view_points']))
    if re.search(r'X4LAN_CONFIG\.native_gate_view_points_mask\s*=\s*(?!false\b|nil\b)\S', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_view_points\s*=\s*(?:true\b|[\'"]players[\'"])', config_text):
        failures.append(dict(rule="native_gate_view_points_mask needs native_gate_view_points = 'players'", bits=['native_gate_view_points']))
    if re.search(r'X4LAN_CONFIG\.native_gate_view_triggers\s*=\s*(?!false\b|nil\b)\S', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_view_points\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_view_triggers needs native_gate_view_points', bits=['native_gate_view_points']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_sample_profile\s*=\s*([^\s;]+)', config_text):
        if value in ('nil', 'false'): continue
        if not re.fullmatch(r'[1-9][0-9]?', value) or int(value) > 50:
            failures.append(dict(rule='native_gate_sample_profile must be a sample period in ms, 1..50', bits=[value]))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_physics_trace\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'0|[1-9][0-9]{0,6}', value) or int(value) > 1000000:
            failures.append(dict(rule='native_gate_physics_trace must be a tick count 0..1000000', bits=[value]))
        if 'native_gate_world_identities' not in enabled:
            failures.append(dict(rule='native_gate_physics_trace needs native_gate_world_identities', bits=['native_gate_world_identities']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
            failures.append(dict(rule='native_gate_physics_trace needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_physics_trace_children\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'[1-9][0-9]{0,3}', value) or int(value) > 3600:
            failures.append(dict(rule='native_gate_physics_trace_children must be a tick count 1..3600', bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_physics_trace\s*=\s*(?!nil\b)[0-9]', config_text):
            failures.append(dict(rule='native_gate_physics_trace_children needs native_gate_physics_trace', bits=['native_gate_physics_trace']))
    if re.search(r'X4LAN_CONFIG\.native_gate_view_scenes\s*=\s*(?!false\b|nil\b)\S', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_physics_scenes\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_view_scenes needs native_gate_physics_scenes', bits=['native_gate_physics_scenes']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_world_hash_shields\s*=\s*([^\s;]+)', config_text):
        if value in ('nil', 'false'): continue
        if value not in ("'canonical'", '"canonical"'):
            failures.append(dict(rule="native_gate_world_hash_shields must be 'canonical'", bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_world_hash_stride\s*=\s*[0-9]', config_text):
            failures.append(dict(rule='native_gate_world_hash_shields needs native_gate_world_hash_stride', bits=['native_gate_world_hash_stride']))
    # Hot join census (reports/LOCKSTEP-HOTJOIN-20261101.txt 1.6): one more world capture at the join's T+1 on the session and
    # the joiner (native_gate_probe.lua join_client.capture_at).
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_world_hash_join\s*=\s*([^\s;]+)', config_text):
        if value in ('nil', 'false'): continue
        if value != 'true':
            failures.append(dict(rule='native_gate_world_hash_join must be true', bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_world_hash_stride\s*=\s*[0-9]', config_text):
            failures.append(dict(rule='native_gate_world_hash_join needs native_gate_world_hash_stride', bits=['native_gate_world_hash_stride']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_join\s*=\s*(?!false|nil)\S', config_text):
            failures.append(dict(rule='native_gate_world_hash_join needs native_gate_join', bits=['native_gate_join']))
    # The nav-only hash between census captures (lockstep_world_hash.h world_nav_hash).
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_world_nav_stride\s*=\s*([^\s;]+)', config_text):
        if value in ('nil', 'false'): continue
        census = re.search(r'X4LAN_CONFIG\.native_gate_world_hash_stride\s*=\s*([0-9]+)', config_text)
        if not value.isdigit() or int(value) < 1:
            failures.append(dict(rule='native_gate_world_nav_stride must be a tick count', bits=[value]))
        elif not census or int(value) >= int(census[1]):
            failures.append(dict(rule='native_gate_world_nav_stride needs native_gate_world_hash_stride above it', bits=['native_gate_world_hash_stride']))
        if not re.search(r"X4LAN_CONFIG\.native_gate_world_hash_schema\s*=\s*['\"]WORLD_IMPORTED_2['\"]", config_text):
            failures.append(dict(rule='native_gate_world_nav_stride needs native_gate_world_hash_schema WORLD_IMPORTED_2', bits=['native_gate_world_hash_schema']))
    # FR28: per-tick WORLD_HEALTH rows of the world-hash watches, ticks first..last (read-only).
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_world_health_ticks\s*=\s*([^\n;]+)', config_text):
        value = value.strip()
        if value in ('nil', 'false'): continue
        window = re.fullmatch(r'\{\s*([0-9]+)\s*,\s*([0-9]+)\s*\}', value)
        if not window or not 1 <= int(window[1]) <= int(window[2]):
            failures.append(dict(rule='native_gate_world_health_ticks must be {first,last} ticks, 1 <= first <= last', bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_world_hash_stride\s*=\s*[0-9]', config_text):
            failures.append(dict(rule='native_gate_world_health_ticks needs native_gate_world_hash_stride', bits=['native_gate_world_hash_stride']))
        if not re.search(r"X4LAN_CONFIG\.native_gate_world_hash_watch\s*=\s*\{\s*['\"][0-9a-f]{16}['\"]", config_text):
            failures.append(dict(rule='native_gate_world_health_ticks needs native_gate_world_hash_watch', bits=['native_gate_world_hash_watch']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_viewpoint_standins\s*=\s*([^\s;]+)', config_text):
        if value in ('nil', 'false'): continue
        if value not in ('2', '3', '4'):
            failures.append(dict(rule='native_gate_viewpoint_standins must be 2, 3 or 4', bits=[value]))
        if not re.search(r"X4LAN_CONFIG\.native_gate_viewpoint_policy\s*=\s*['\"]sector['\"]", config_text):
            failures.append(dict(rule='native_gate_viewpoint_standins needs native_gate_viewpoint_policy', bits=['native_gate_viewpoint_policy']))
    if re.search(r'X4LAN_CONFIG\.native_gate_field_fill\s*=\s*(?!false\b|nil\b)\S', config_text) and \
       not re.search(r'X4LAN_CONFIG\.native_gate_region_fields\s*=\s*(?!false\b|nil\b)\S', config_text):
        failures.append(dict(rule='native_gate_field_fill needs native_gate_region_fields', bits=['native_gate_region_fields']))
    if re.search(r'X4LAN_CONFIG\.native_gate_highway_speed\s*=\s*(?!false\b|nil\b)\S', config_text) and 'native_gate_world_identities' not in enabled:
        failures.append(dict(rule='native_gate_highway_speed needs native_gate_world_identities', bits=['native_gate_world_identities']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_runtime_ids\s*=\s*([^\s;]+)', config_text):
        if value in ('nil', 'false'): continue
        number = re.fullmatch(r'[1-9][0-9]{0,5}', value)
        if value != 'true' and value.strip('\'"') != 'observe' and not (number and int(value) <= 524288):
            failures.append(dict(rule="native_gate_runtime_ids must be a slot 1..524288, true or 'observe'", bits=[value]))
        if 'native_gate_world_identities' not in enabled:
            failures.append(dict(rule='native_gate_runtime_ids needs native_gate_world_identities', bits=['native_gate_world_identities']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_snapshot_sha\s*=', config_text):
            failures.append(dict(rule='native_gate_runtime_ids needs the snapshot boot', bits=['native_gate_snapshot_sha']))
    if re.search(r'X4LAN_CONFIG\.native_gate_runtime_ids_barrier\s*=\s*(?!false\b|nil\b)\S', config_text):
        if not re.search(r'X4LAN_CONFIG\.native_gate_runtime_ids_barrier\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_runtime_ids_barrier must be true', bits=['native_gate_runtime_ids_barrier']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_runtime_ids\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_runtime_ids_barrier needs native_gate_runtime_ids', bits=['native_gate_runtime_ids']))
    if re.search(r'X4LAN_CONFIG\.native_gate_runtime_ids_local\s*=\s*(?!false\b|nil\b)\S', config_text):
        if not re.search(r'X4LAN_CONFIG\.native_gate_runtime_ids_local\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_runtime_ids_local must be true', bits=['native_gate_runtime_ids_local']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_runtime_ids\s*=\s*(?!false\b|nil\b|[\'"]observe[\'"])\S', config_text):
            failures.append(dict(rule='native_gate_runtime_ids_local needs native_gate_runtime_ids set to a slot', bits=['native_gate_runtime_ids']))
        if 'native_gate_created_identities' not in enabled:
            failures.append(dict(rule='native_gate_runtime_ids_local needs native_gate_created_identities', bits=['native_gate_created_identities']))
    # A client's guidance (lockstep_guidance_local.h): the local lists, the script RNG's MD frames (mask bit 2), the MD listener frames.
    if re.search(r'X4LAN_CONFIG\.native_gate_guidance_local\s*=\s*(?!false\b|nil\b)\S', config_text):
        if 'native_gate_guidance_local' not in enabled:
            failures.append(dict(rule='native_gate_guidance_local must be true', bits=['native_gate_guidance_local']))
        if 'native_gate_runtime_ids_local' not in enabled:
            failures.append(dict(rule='native_gate_guidance_local needs native_gate_runtime_ids_local', bits=['native_gate_runtime_ids_local']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_script_rng\s*=\s*[\'"](?:key|observe)[\'"]', config_text):
            failures.append(dict(rule='native_gate_guidance_local needs native_gate_script_rng', bits=['native_gate_script_rng']))
        masks = re.findall(r'X4LAN_CONFIG\.native_gate_script_rng_mask\s*=\s*([0-9]+)\b', config_text)
        if not (int(masks[-1]) if masks else 3) & 2:
            failures.append(dict(rule='native_gate_guidance_local needs the MD bit (2) of native_gate_script_rng_mask', bits=['native_gate_script_rng_mask']))
        if 'native_gate_md_timed_rng' not in enabled:
            failures.append(dict(rule='native_gate_guidance_local needs native_gate_md_timed_rng', bits=['native_gate_md_timed_rng']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_highway_rows\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'[1-9][0-9]{0,3}', value) or int(value) > 3600:
            failures.append(dict(rule='native_gate_highway_rows must be a tick count 1..3600', bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_view_points\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_highway_rows needs native_gate_view_points', bits=['native_gate_view_points']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_(?:mass_traffic|region_pass)\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_highway_rows needs native_gate_mass_traffic or native_gate_region_pass',
                                 bits=['native_gate_mass_traffic', 'native_gate_region_pass']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_highway_rows_from\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'[1-9][0-9]{0,6}', value) or int(value) > 1000000:
            failures.append(dict(rule='native_gate_highway_rows_from must be a tick 1..1000000', bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_highway_rows\s*=\s*(?!nil\b)[0-9]', config_text):
            failures.append(dict(rule='native_gate_highway_rows_from needs native_gate_highway_rows', bits=['native_gate_highway_rows']))
    if re.search(r'X4LAN_CONFIG\.native_gate_physics_trace_saved\s*=\s*(?!false\b|nil\b)\S', config_text):
        if not re.search(r'X4LAN_CONFIG\.native_gate_physics_trace_saved\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_physics_trace_saved must be true', bits=['native_gate_physics_trace_saved']))
        if not re.search(r'X4LAN_CONFIG\.native_gate_physics_trace\s*=\s*[1-9]', config_text):
            failures.append(dict(rule='native_gate_physics_trace_saved needs native_gate_physics_trace >= 1', bits=['native_gate_physics_trace']))
    # Diagnostic rows only: body and contact rows of the physics trace from a tick on.
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_physics_trace_from\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        rows = re.search(r'X4LAN_CONFIG\.native_gate_physics_trace\s*=\s*([0-9]+)\b', config_text)
        if not re.fullmatch(r'[1-9][0-9]{0,6}', value):
            failures.append(dict(rule='native_gate_physics_trace_from must be a tick 1..1000000', bits=[value]))
        elif not rows or int(rows.group(1)) < int(value):
            failures.append(dict(rule='native_gate_physics_trace_from needs native_gate_physics_trace >= from', bits=['native_gate_physics_trace']))
    # Diagnostic rows only: the wreck watch reads the motion watch's list.
    if (re.search(r'X4LAN_CONFIG\.native_gate_wreck_watch\s*=\s*true\b', config_text) and
            not re.search(r'X4LAN_CONFIG\.native_gate_motion_watch\s*=\s*\{', config_text)):
        failures.append(dict(rule='native_gate_wreck_watch needs native_gate_motion_watch', bits=['native_gate_motion_watch']))
    # Diagnostic rows only: script RNG rows of a tick window.
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_script_rng_rows_window\s*=\s*([^\n;]+)', config_text):
        value = value.strip()
        if value == 'nil': continue
        match = re.fullmatch(r'\{\s*([1-9][0-9]*)\s*,\s*([1-9][0-9]*)\s*\}', value)
        if not match or int(match.group(2)) < int(match.group(1)) or int(match.group(2)) - int(match.group(1)) > 600:
            failures.append(dict(rule='native_gate_script_rng_rows_window must be {first, last} ticks at most 600 apart', bits=[value]))
        elif not re.search(r'X4LAN_CONFIG\.native_gate_script_rng_rows\s*=\s*true\b', config_text):
            failures.append(dict(rule='native_gate_script_rng_rows_window needs native_gate_script_rng_rows = true', bits=['native_gate_script_rng_rows']))
    # Diagnostic rows only: who allocates runtime IDs, every N ticks.
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_runtime_ids_caller_stride\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'[1-9][0-9]{0,3}', value) or int(value) > 3600:
            failures.append(dict(rule='native_gate_runtime_ids_caller_stride must be a tick count 1..3600', bits=[value]))
        elif not re.search(r'X4LAN_CONFIG\.native_gate_runtime_ids\s*=\s*(?!false\b|nil\b)\S', config_text):
            failures.append(dict(rule='native_gate_runtime_ids_caller_stride needs native_gate_runtime_ids', bits=['native_gate_runtime_ids']))
    # Log thinning only (held ticks are written before a stop or failure); no prerequisite.
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_status_stride\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'[1-9][0-9]{0,3}', value) or int(value) > 3600:
            failures.append(dict(rule='native_gate_status_stride must be a tick count 1..3600', bits=[value]))
    # Diagnostic timing only (one PROBE_PROFILE line per 60 ticks); no prerequisite.
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_probe_profile\s*=\s*([^\s;]+)', config_text):
        if value not in ('true', 'false', 'nil'):
            failures.append(dict(rule='native_gate_probe_profile accepts only true', bits=[value]))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_gravidar_digest_rows\s*=\s*([^\s;]+)', config_text):
        if value == 'nil': continue
        if not re.fullmatch(r'[1-9][0-9]{0,6}', value) or int(value) > 1000000:
            failures.append(dict(rule='native_gate_gravidar_digest_rows must be a tick count 1..1000000', bits=[value]))
        if 'native_gate_gravidar_digest' not in enabled:
            failures.append(dict(rule='native_gate_gravidar_digest_rows needs native_gate_gravidar_digest', bits=['native_gate_gravidar_digest']))
    return failures


def check(config_text):
    """(boot option bits, failures) of a config; a session config with its defaults applied."""
    options, failures, _ = check_report(config_text)
    return options, failures


def check_report(config_text):
    """(boot option bits, failures, defaults report or None): the rules run on the effective config
    (native_gate_defaults.effective); a refused combination of the defaults is a failure too."""
    config_text, report = defaults.effective(config_text)
    options, failures = check_effective(config_text)
    for why in (report or {}).get('errors', []):
        failures.append(dict(rule='defaults: ' + why, bits=['native_gate_defaults']))
    return options, failures, report


def check_effective(config_text):
    bits = option_bits()
    enabled = set(re.findall(r'X4LAN_CONFIG\.(native_gate_\w+)\s*=\s*true\b', config_text))
    options = sum(value for name, value in bits if name in enabled)
    names = {value: name for name, value in bits}
    failures = []
    for line, expr in rules():
        if eval(expr, {'o': options}):
            mentioned = [names.get(int(v), v) for v in re.findall(r'&(\d+)', line.replace('ULL', ''))]
            failures.append(dict(rule=line, bits=mentioned))
    failures.extend(probe_rules(config_text, enabled))
    # FR27 / FR30: the solver and the drone factors arm inside the fp redirect (a mode string, not a true flag)
    for key in ('native_gate_fp_redirect_solver', 'native_gate_fp_redirect_drones'):
        armed = re.search(r"X4LAN_CONFIG\." + key + r"\s*=\s*['\"]", config_text)
        if armed and not re.search(r"X4LAN_CONFIG\.native_gate_fp_redirect\s*=\s*['\"]", config_text):
            failures.append(dict(rule=key + ' needs native_gate_fp_redirect', bits=['native_gate_fp_redirect']))
    # FR29b: the attack capture, ticks first..last (read-only), armed after the fp redirect, keyed by world identities
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_fp_attack_capture\s*=\s*([^\n;]+)', config_text):
        value = value.strip()
        if value in ('nil', 'false'): continue
        window = re.fullmatch(r'\{\s*([0-9]+)\s*,\s*([0-9]+)\s*\}', value)
        if not window or not 1 <= int(window[1]) <= int(window[2]):
            failures.append(dict(rule='native_gate_fp_attack_capture must be {first,last} ticks, 1 <= first <= last', bits=[value]))
        if not re.search(r"X4LAN_CONFIG\.native_gate_fp_redirect\s*=\s*['\"]", config_text):
            failures.append(dict(rule='native_gate_fp_attack_capture needs native_gate_fp_redirect', bits=['native_gate_fp_redirect']))
        if 'native_gate_world_identities' not in enabled:
            failures.append(dict(rule='native_gate_fp_attack_capture needs native_gate_world_identities', bits=['native_gate_world_identities']))
    for value in re.findall(r'X4LAN_CONFIG\.native_gate_fp_attack_watch\s*=\s*([^\n;]+)', config_text):
        value = value.strip()
        if value in ('nil', 'false'): continue
        ids = re.findall(r"['\"]([^'\"]*)['\"]", value)
        if not re.fullmatch(r"\{[^{}]*\}", value) or len(ids) > 32 or len(set(ids)) != len(ids) or \
           any(not re.fullmatch('[0-9a-f]{16}', i) or int(i, 16) == 0 for i in ids):
            failures.append(dict(rule='native_gate_fp_attack_watch must be up to 32 unique saved IDs (16 lowercase hex digits)', bits=[value]))
        if not re.search(r'X4LAN_CONFIG\.native_gate_fp_attack_capture\s*=\s*\{', config_text):
            failures.append(dict(rule='native_gate_fp_attack_watch needs native_gate_fp_attack_capture', bits=['native_gate_fp_attack_capture']))
    return options, failures


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('config', type=Path, help='config.lua or config.lua.append')
    args = p.parse_args()
    options, failures, report = check_report(args.config.read_text())
    print(f'options={options}')
    if report and report.get('bare'):
        print('DEFAULTS_OFF native_gate_defaults = false: the bare config is checked')
    elif report and report.get('session'):
        print(f"DEFAULTS applied={len(report['applied'])} explicit={len(report['explicit'])} off={len(report['off'])} skipped={len(report['skipped'])}")
        for key, why, _ in report['skipped']:
            print('DEFAULT_SKIPPED', key, '->', why)
    for f in failures: print('REFUSED by rule:', f['rule'], '->', ', '.join(map(str, f['bits'])))
    sys.exit(1 if failures else 0)


if __name__ == '__main__': main()

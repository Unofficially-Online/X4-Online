"""Session defaults (tools/dev/native_gate_defaults.lua and its Python mirror; reports/LOCKSTEP-DEFAULTS-20261101.txt):
the table, the apply rules in Lua and Python alike, the option checker on an empty session config, the overlay builders,
and the probe's loud paths (refusals, skipped defaults, degraded counters, stops) with their info lines."""
import contextlib
import importlib.util
import io
import re
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from lupa.luajit21 import LuaRuntime, LuaError

ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT/'tools/dev'
LUA = (DEV/'native_gate_defaults.lua').read_text(encoding='utf-8')
PROBE = (DEV/'native_gate_probe.lua').read_text(encoding='utf-8')
sys.path.insert(0, str(DEV))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT/path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


defaults = load('native_gate_defaults_test', 'tools/dev/native_gate_defaults.py')
options = load('check_gate_options_defaults', 'tools/dev/check_gate_options.py')
players = load('native_gate_players_defaults', 'tools/dev/native_gate_players.py')

PLAN = ('{["mode"]="coop",["occupied"]="player",["reputation"]="mirror",["slots"]={{["faction"]="player",["ship"]="00000000000ae79c"},'
        '{["faction"]="player",["ship"]="000000000007a8fe"}},["starts"]={{["object"]="000000000007a8fe",["sector"]="0000000000000729",'
        '["x"]=0,["y"]=100000,["yaw"]=180,["z"]=2000}},["tick"]=20,["version"]=1}')
# The live-test spec: only the session (slot, port, token, clients) and the identity plan.
SESSION = f'''X4LAN_CONFIG.enabled = false
X4LAN_CONFIG.native_gate_test = true
X4LAN_CONFIG.test_profile = "45841"
X4LAN_CONFIG.host = "127.0.0.1"
X4LAN_CONFIG.port = @X4LANE_PORT@
X4LAN_CONFIG.token = "@X4LANE_TOKEN@"
X4LAN_CONFIG.slot = "@X4LANE_SLOT@"
X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@
X4LAN_CONFIG.native_gate_player_identity_plan = {PLAN}
'''
# The people config of 1 November (.local/census/overlay-Play42P/ui/config.lua.append; the watch list shortened).
PLAY42P = '''X4LAN_CONFIG.enabled = false
X4LAN_CONFIG.native_gate_test = true
X4LAN_CONFIG.native_gate_hash = "battle"
X4LAN_CONFIG.native_gate_start_time = 5
X4LAN_CONFIG.test_profile = "45841"
X4LAN_CONFIG.host = "127.0.0.1"
X4LAN_CONFIG.port = @X4LANE_PORT@
X4LAN_CONFIG.token = "@X4LANE_TOKEN@"
X4LAN_CONFIG.slot = "@X4LANE_SLOT@"
X4LAN_CONFIG.native_gate_order_state = true
X4LAN_CONFIG.native_gate_fleet_state = true
X4LAN_CONFIG.native_gate_rng_schema = 2
X4LAN_CONFIG.native_gate_snapshot_sha = "7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8"
X4LAN_CONFIG.native_gate_snapshot_fixture = {'0000000000176a13','0000000000176a74','0000000000176ad5','0000000000176b36','0000000000176b97','0000000000176bf8','0000000000176c59','0000000000176c77','0000000000176c95','0000000000176cb3','0000000000176cd1','0000000000176cef'}
X4LAN_CONFIG.native_gate_serial_pathing = true
X4LAN_CONFIG.native_gate_seed_rng = true
X4LAN_CONFIG.native_gate_interrupt_diag = true
X4LAN_CONFIG.native_gate_nested_actions = true
X4LAN_CONFIG.native_gate_world_identities = true
X4LAN_CONFIG.native_gate_world_hash_schema = 'WORLD_IMPORTED_2'
X4LAN_CONFIG.native_gate_world_timing_fields = true
X4LAN_CONFIG.native_gate_flight_rng = true
X4LAN_CONFIG.native_gate_avoidance_refresh = true
X4LAN_CONFIG.native_gate_steering_init = true
X4LAN_CONFIG.native_gate_steering_schema = 2
X4LAN_CONFIG.native_gate_radar_diag = true
X4LAN_CONFIG.native_gate_radar_serial = true
X4LAN_CONFIG.native_gate_radar_rng = true
X4LAN_CONFIG.native_gate_radar_initial_refresh = true
X4LAN_CONFIG.native_gate_spline_refresh = true
X4LAN_CONFIG.native_gate_spline_state = true
X4LAN_CONFIG.native_gate_spline_join = true
X4LAN_CONFIG.native_gate_agent_rng = true
X4LAN_CONFIG.native_gate_ship_rng = true
X4LAN_CONFIG.native_gate_params_init = true
X4LAN_CONFIG.native_gate_weapon_rng = true
X4LAN_CONFIG.native_gate_nav_rng = true
X4LAN_CONFIG.native_gate_nav_initial_refresh = true
X4LAN_CONFIG.native_gate_strict_state = true
X4LAN_CONFIG.native_gate_shield_refresh = true
X4LAN_CONFIG.native_gate_strict_physics = true
X4LAN_CONFIG.native_gate_attention_rng = true
X4LAN_CONFIG.native_gate_attention_initial_refresh = true
X4LAN_CONFIG.native_gate_death_rng = true
X4LAN_CONFIG.native_gate_bullet_rng = true
X4LAN_CONFIG.native_gate_shape_rng = true
X4LAN_CONFIG.native_gate_shape_initial_refresh = true
X4LAN_CONFIG.native_gate_path_determinism = 3
X4LAN_CONFIG.native_gate_gravidar_key_all = true
X4LAN_CONFIG.native_gate_event_order = true
X4LAN_CONFIG.native_gate_gravidar_time_sync = true
X4LAN_CONFIG.native_gate_script_rng = "key"
X4LAN_CONFIG.native_gate_ai_schedule = true
X4LAN_CONFIG.native_gate_script_rng_mask = 7
X4LAN_CONFIG.native_gate_gravidar_load_pass = true
X4LAN_CONFIG.native_gate_md_timed_rng = true
X4LAN_CONFIG.native_gate_boot_hold = true
X4LAN_CONFIG.native_gate_gravidar_sector_order = true
X4LAN_CONFIG.native_gate_md_resume_rng = true
X4LAN_CONFIG.native_gate_ai_wait_rng = true
X4LAN_CONFIG.native_gate_spline_diag = true
X4LAN_CONFIG.native_gate_agent_full = true
X4LAN_CONFIG.native_gate_flight_full = true
X4LAN_CONFIG.native_gate_ai_diag = true
X4LAN_CONFIG.native_gate_travel_snapshot = true
X4LAN_CONFIG.native_gate_frame_timing = true
X4LAN_CONFIG.native_gate_find_order = 'sort'
X4LAN_CONFIG.native_gate_region_rng = 'key'
X4LAN_CONFIG.native_gate_world_hash_watch = {'0000000000097720'}  -- 64 IDs in Play42P
X4LAN_CONFIG.native_gate_bullet_diag = true
X4LAN_CONFIG.native_gate_hit_claims = 'victim'
X4LAN_CONFIG.native_gate_spawn_rng = 'key'
X4LAN_CONFIG.native_gate_created_identities = true
X4LAN_CONFIG.native_gate_portable_created = true
X4LAN_CONFIG.native_gate_created_load = true
X4LAN_CONFIG.native_gate_script_rng_unmapped = 'key'
X4LAN_CONFIG.native_gate_nav_probe_init = true
X4LAN_CONFIG.native_gate_mass_traffic = 'sync'
X4LAN_CONFIG.native_gate_region_pass = 'sync'
X4LAN_CONFIG.native_gate_speech_game_time = true
X4LAN_CONFIG.native_gate_nav_player = true
X4LAN_CONFIG.native_gate_nav_params_init = true
X4LAN_CONFIG.native_gate_attention_all = true
X4LAN_CONFIG.native_gate_gravidar_deadlines = true
X4LAN_CONFIG.native_gate_weapon_fire_rng = true
X4LAN_CONFIG.native_gate_region_camera = 'view'
X4LAN_CONFIG.native_gate_region_fields = 'view'
X4LAN_CONFIG.native_gate_mass_traffic_camera = 'view'
X4LAN_CONFIG.native_gate_effect_camera = 'view'
X4LAN_CONFIG.native_gate_highway_camera = 'view'
X4LAN_CONFIG.native_gate_engine_refresh_rng = true
X4LAN_CONFIG.native_gate_player_timer = true
X4LAN_CONFIG.native_gate_ship_sound_rng = 'split'
X4LAN_CONFIG.native_gate_view_points_source = 'ship'
X4LAN_CONFIG.native_gate_warp_rng = true
X4LAN_CONFIG.native_gate_physics_scenes = 'viewpoints'
X4LAN_CONFIG.native_gate_status_stride = 60
X4LAN_CONFIG.native_gate_tempzone_order = 'order'
X4LAN_CONFIG.native_gate_lookahead = 1
X4LAN_CONFIG.native_gate_probe_profile = true
X4LAN_CONFIG.native_gate_camera_context = 'view'
X4LAN_CONFIG.native_gate_nav_octree = 'agreed'
X4LAN_CONFIG.native_gate_nav_octree_snapshot = true
X4LAN_CONFIG.native_gate_viewpoint_policy = 'sector'
X4LAN_CONFIG.native_gate_event_rng = 'key'
X4LAN_CONFIG.native_gate_build_rng = true
X4LAN_CONFIG.native_gate_gravidar_wide = 'sort'
X4LAN_CONFIG.native_gate_loadout_order = 'canonical'
X4LAN_CONFIG.native_gate_order_canon = 'canonical'
X4LAN_CONFIG.native_gate_highway_speed = 'key'
X4LAN_CONFIG.native_gate_portable_math = true
X4LAN_CONFIG.native_gate_exact_reciprocal = true
X4LAN_CONFIG.native_gate_decimal = true
X4LAN_CONFIG.native_gate_fp_env = 'default'
X4LAN_CONFIG.native_gate_shape_order = 'sync'
X4LAN_CONFIG.native_gate_pick_order = 'canonical'
X4LAN_CONFIG.native_gate_macro_arena = true
X4LAN_CONFIG.native_gate_address_order = 150
X4LAN_CONFIG.native_gate_runtime_ids = true
X4LAN_CONFIG.native_gate_runtime_ids_barrier = true
X4LAN_CONFIG.native_gate_runtime_ids_local = true
X4LAN_CONFIG.native_gate_shape_refresh = 'view'
X4LAN_CONFIG.native_gate_sound_rng = 'split'
X4LAN_CONFIG.native_gate_tick_hz = 30
X4LAN_CONFIG.native_gate_path_budget = 100
X4LAN_CONFIG.native_gate_pass_stride = 2
X4LAN_CONFIG.native_gate_fast_reads = true
X4LAN_CONFIG.native_gate_runtime_ids_caller_stride = 10
X4LAN_CONFIG.native_gate_build_rows = true
X4LAN_CONFIG.native_gate_economy_load_rng = true
X4LAN_CONFIG.native_gate_field_fill = 'view'
X4LAN_CONFIG.native_gate_mining_camera = 'view'
X4LAN_CONFIG.native_gate_xs_camera = 'view'
X4LAN_CONFIG.native_gate_traffic_views = 'view'
X4LAN_CONFIG.native_gate_cutscene_ready = true
X4LAN_CONFIG.native_gate_speak_timer = true
X4LAN_CONFIG.native_gate_world_hash_shields = 'canonical'
X4LAN_CONFIG.native_gate_view_scenes = true
X4LAN_CONFIG.native_gate_viewpoint_standins = 8
X4LAN_CONFIG.native_gate_view_ui = 'block'
X4LAN_CONFIG.native_gate_view_triggers = 'block'
X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@
X4LAN_CONFIG.native_gate_player_identity = true
X4LAN_CONFIG.native_gate_player_identity_plan = {["mode"]="coop",["occupied"]="player",["reputation"]="mirror",["slots"]={{["faction"]="player",["ship"]="00000000000ae79c"},{["faction"]="player",["ship"]="000000000007a8fe"}},["starts"]={{["object"]="000000000007a8fe",["sector"]="0000000000000729",["x"]=0,["y"]=100000,["yaw"]=180,["z"]=2000}},["tick"]=20,["version"]=1}
X4LAN_CONFIG.native_gate_pilot_inputs = true
X4LAN_CONFIG.native_gate_pilot_slots = {'00000000000ae79c','000000000007a8fe'}
X4LAN_CONFIG.native_gate_pilot_source = 'wire'
X4LAN_CONFIG.native_gate_pilot_delay = 3
X4LAN_CONFIG.native_gate_pilot_window = {40,36000}
X4LAN_CONFIG.native_gate_pilot_suppress_local = true
X4LAN_CONFIG.native_gate_pilot_stock = true
X4LAN_CONFIG.native_gate_pilot_stock_slots = 'all'
X4LAN_CONFIG.native_gate_pilot_capture_source = {'stock','stock'}
X4LAN_CONFIG.native_gate_pilot_device = {false,false}
X4LAN_CONFIG.native_gate_pilot_stock_script = {'demo','demo2'}
X4LAN_CONFIG.native_gate_pilot_present = true
X4LAN_CONFIG.native_gate_camera_anchor = nil  -- a play base's chase camera: X4's own camera instead
X4LAN_CONFIG.native_gate_pilot_capture = true
X4LAN_CONFIG.native_gate_pilot_ship_state = true
X4LAN_CONFIG.native_gate_pilot_block_orders = true
X4LAN_CONFIG.native_gate_render_rng_split = true
X4LAN_CONFIG.native_gate_bullet_cadence = 'every'
X4LAN_CONFIG.native_gate_nav_cadence = 'far'
X4LAN_CONFIG.native_gate_actor_diag = true
X4LAN_CONFIG.native_gate_weapon_diag = true
X4LAN_CONFIG.native_gate_view_points = 'players'
X4LAN_CONFIG.native_gate_actor_rng_all = true
X4LAN_CONFIG.native_gate_world_hash_stride = 1800
X4LAN_CONFIG.native_gate_trace_stride = 60
X4LAN_CONFIG.native_gate_spline_join_all = true
X4LAN_CONFIG.native_gate_path_transforms = 'sim'
X4LAN_CONFIG.native_gate_ui_replay = {'travel','target','orders','autopilot','dock','weapons','queue','missiles'}
X4LAN_CONFIG.native_gate_guidance_local = true
X4LAN_CONFIG.native_gate_body_pose = 'sim'
X4LAN_CONFIG.native_gate_pilot_fire = 'record'
X4LAN_CONFIG.native_gate_pilot_death = true
X4LAN_CONFIG.native_gate_pilot_death_suit = true
X4LAN_CONFIG.native_gate_pilot_respawn = 300
X4LAN_CONFIG.native_gate_crash_note = true'''


def lua_runtime(config):
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute("logs={};info={};DebugError=function(s) logs[#logs+1]=s end")
    lua.execute("local C={ShowInfoLine=function(text,kind) info[#info+1]=text end,IsStartmenu=function() return menu==true end}\n"
                "local ffi={cdef=function() end,C=C};require=function(name) assert(name=='ffi');return ffi end")
    lua.execute('X4LAN_CONFIG=' + config)
    lua.execute(LUA)
    return lua


def lua_session(extra=''):
    return lua_runtime("{native_gate_test=true,slot='p1',port=1234,token=string.rep('c',32),native_gate_clients=2,native_gate_player_identity_plan="
                       "{mode='coop',occupied='player',reputation='mirror',slots={{faction='player',ship='00000000000ae79c'},"
                       "{faction='player',ship='000000000007a8fe'}},tick=20,version=1}" + extra + '}')


def report(lua):
    return lua.eval('X4LAN_CONFIG._native_gate_defaults')


def items(table):
    return [table[i] for i in range(1, len(table)+1)] if table is not None else []


class TableTests(unittest.TestCase):
    def test_the_mirror_reads_the_lua_table(self):
        lua = lua_runtime('nil')
        D = lua.globals().X4LAN_NATIVE_GATE_DEFAULTS
        self.assertEqual([D.ON[i][1] for i in range(1, len(D.ON)+1)], defaults.ON_KEYS)
        self.assertEqual([D.DIAGNOSTIC[i][1] for i in range(1, len(D.DIAGNOSTIC)+1)], defaults.DIAGNOSTIC_KEYS)
        self.assertEqual([D.EXCLUDED[i][1] for i in range(1, len(D.EXCLUDED)+1)], defaults.EXCLUDED_KEYS)
        self.assertEqual([D.TUNING[i][1] for i in range(1, len(D.TUNING)+1)], defaults.TUNING_KEYS)
        self.assertEqual(items(D.FAMILIES), defaults.TABLE['families'])
        for i, (key, value, needs, why) in enumerate(defaults.TABLE['on'], 1):
            self.assertEqual(D.ON[i][3], ' '.join(needs), key)
            if not value.startswith("'@"):
                self.assertEqual(D.canon(D.ON[i][2]), lua.eval('X4LAN_NATIVE_GATE_DEFAULTS.canon(' + value + ')'), key)

    def test_every_key_is_in_one_list_and_prerequisites_come_first(self):
        lists = [defaults.ON_KEYS, defaults.DIAGNOSTIC_KEYS, defaults.EXCLUDED_KEYS, defaults.TUNING_KEYS, defaults.TABLE['session']]
        every = [k for keys in lists for k in keys]
        self.assertEqual(len(every), len(set(every)), [k for k in every if every.count(k) > 1])
        for i, (key, _, needs, _) in enumerate(defaults.TABLE['on']):
            for token in needs:
                name = re.match(r'!?([a-z0-9_]+)', token.lstrip('@'))[1]
                if token.startswith('@') or token.startswith('!'):
                    continue
                if name in defaults.ON_KEYS:
                    self.assertLess(defaults.ON_KEYS.index(name), i, f'{key} needs {name} after it')

    def test_every_probe_key_is_classified(self):
        files = ('native_gate_crypto', 'native_gate_inputs_core', 'native_gate_relay', 'native_gate_tour', 'native_gate_ui_build_core',
                 'native_gate_ui_replay_core', 'native_gate_join_core', 'native_gate_ffi_relay_core', 'native_gate_ui_inventory_core')   # file names the probe mentions, not keys
        keys = set(re.findall(r'native_gate_[a-z0-9_]+', PROBE)) - set(files)
        known = set(defaults.ON_KEYS) | set(defaults.DIAGNOSTIC_KEYS) | set(defaults.EXCLUDED_KEYS) | set(defaults.TUNING_KEYS) | set(defaults.TABLE['session'])
        self.assertEqual(sorted(keys - known), [])
        self.assertEqual(sorted(known - keys), ['native_gate_world_timing_fields'])   # Play42P sets it, nothing reads it

    def test_diagnostics_stay_off(self):
        for key in ('native_gate_frame_timing', 'native_gate_probe_profile', 'native_gate_sample_profile', 'native_gate_world_hash_watch',
                    'native_gate_bullet_diag', 'native_gate_view_points_mask', 'native_gate_hybrid', 'native_gate_physics_trace', 'native_gate_pilot_capture'):
            self.assertIn(key, defaults.DIAGNOSTIC_KEYS)
        values, rep = defaults.apply(defaults.parse(SESSION))
        self.assertFalse(set(rep['applied']) & set(defaults.DIAGNOSTIC_KEYS))

    def test_the_people_config_is_covered(self):
        """Every key of Play42P is a default with Play42P's value, a diagnostic, a fixture, a tuning key or the session."""
        play = defaults.parse(PLAY42P)
        values, rep = defaults.apply(defaults.parse(SESSION))
        on = dict((k, v) for k, v, _, _ in defaults.TABLE['on'])
        differ = {}
        for key, value in play.items():
            if not key.startswith('native_gate_'):
                continue
            if key in on:
                if not defaults.same(values[key], value):
                    differ[key] = (values[key], value)
            else:
                self.assertTrue(key in defaults.DIAGNOSTIC_KEYS + defaults.EXCLUDED_KEYS + defaults.TUNING_KEYS + defaults.TABLE['session'], key)
        # The stack is Play42P's plus every UI family; the controls stay open 40 minutes (Play42P's run ended at 36000).
        self.assertEqual(sorted(differ), ['native_gate_pilot_window', 'native_gate_ui_replay'])
        self.assertEqual(set(defaults.modes(play['native_gate_ui_replay'])) - set(defaults.modes(values['native_gate_ui_replay'])), set())
        # What the defaults add to Play42P: the newer features.
        added = sorted(set(rep['applied']) - set(play))
        self.assertEqual(added, sorted(['native_gate_ui_guard', 'native_gate_pilot_foot', 'native_gate_player_settings_per_slot', 'native_gate_render_fps',
                                        'native_gate_render_camera', 'native_gate_render_motion', 'native_gate_own_predict',
                                        'native_gate_own_predict_rows', 'native_gate_player_identity', 'native_gate_pilot_foot_bodies',
                                        'native_gate_pilot_foot_slots', 'native_gate_nav_mesh', 'native_gate_character_play', 'native_gate_speech_clock',
                                        'native_gate_ui_lazy', 'native_gate_missions_per_player', 'native_gate_missions_per_player_ai_frames', 'native_gate_pilot_slots_post', 'native_gate_view_cameras', 'native_gate_own_predict_hud', 'native_gate_hud_ship_refresh', 'native_gate_missions_per_player_offers', 'native_gate_runtime_ids_ui_elements', 'native_gate_pilot_aim', 'native_gate_pilot_aim_assist', 'native_gate_join_start_money', 'native_gate_speech_local', 'native_gate_travel_determinism']) if 'native_gate_player_identity' not in play else
                         sorted(['native_gate_ui_guard', 'native_gate_pilot_foot', 'native_gate_player_settings_per_slot', 'native_gate_render_fps',
                                 'native_gate_render_camera', 'native_gate_render_motion', 'native_gate_own_predict', 'native_gate_own_predict_rows',
                                 'native_gate_pilot_foot_bodies', 'native_gate_pilot_foot_slots', 'native_gate_nav_mesh', 'native_gate_character_play', 'native_gate_speech_clock',
                                 'native_gate_ui_lazy', 'native_gate_missions_per_player', 'native_gate_missions_per_player_ai_frames', 'native_gate_pilot_slots_post', 'native_gate_view_cameras', 'native_gate_own_predict_hud', 'native_gate_hud_ship_refresh', 'native_gate_missions_per_player_offers', 'native_gate_runtime_ids_ui_elements', 'native_gate_pilot_aim', 'native_gate_pilot_aim_assist', 'native_gate_join_start_money', 'native_gate_speech_local', 'native_gate_travel_determinism']))


class ApplyTests(unittest.TestCase):
    def test_inert_without_a_session(self):
        lua = lua_runtime('{enabled=false,slot="p1",token=""}')
        self.assertIsNone(report(lua))
        self.assertEqual(items(lua.globals().logs), [])
        self.assertEqual(defaults.apply(defaults.parse('X4LAN_CONFIG.enabled = false\n'))[1], None)
        # The config every client starts from stays inert: x4ls writes it (src/lockstep/launcher/extension.py) as this
        # line, then the overlay's session keys.
        launcher = (ROOT/'src/lockstep/launcher/extension.py').read_text(encoding='utf-8')
        source = re.search(r"'(X4LAN_CONFIG = \{enabled = false\})\\n'", launcher)[1] + '\n'   # extension.py's config text
        lua = lua_runtime('nil')
        lua.execute(source)
        self.assertIsNone(lua.eval('X4LAN_NATIVE_GATE_DEFAULTS.apply(X4LAN_CONFIG)'))
        self.assertIsNone(lua.eval('X4LAN_CONFIG.native_gate_world_identities'))

    def test_native_gate_test_without_a_session_is_refused(self):
        lua = lua_runtime('{native_gate_test=true,slot="p1",port=1,token=""}')
        self.assertEqual(len(report(lua).errors), 1)
        with self.assertRaisesRegex(LuaError, 'without a lockstep session'):
            lua.execute('X4LAN_NATIVE_GATE_DEFAULTS.announce(X4LAN_CONFIG._native_gate_defaults)')
        self.assertTrue(any('ERROR DEFAULTS_REFUSED' in line for line in items(lua.globals().logs)))
        self.assertTrue(any('did not start' in line for line in items(lua.globals().info)))

    def test_the_bare_gate(self):
        lua = lua_runtime("{native_gate_test=true,native_gate_defaults=false,slot='p1',port=1234,token=string.rep('c',32)}")
        self.assertTrue(report(lua).bare)
        self.assertIsNone(lua.eval('X4LAN_CONFIG.native_gate_world_identities'))
        lua.execute('X4LAN_NATIVE_GATE_DEFAULTS.announce(X4LAN_CONFIG._native_gate_defaults)')
        self.assertTrue(any('DEFAULTS_OFF native_gate_defaults=false' in line for line in items(lua.globals().logs)))

    def test_a_session_gets_the_whole_stack_once(self):
        lua = lua_session()
        R = report(lua)
        self.assertEqual(len(R.applied), len(defaults.ON_KEYS) - 4)
        self.assertEqual(sorted(s[1] for s in items(R.skipped)), sorted(['native_gate_lookahead_max', 'native_gate_wire_crypto', 'native_gate_peer_window',
                                                                          'native_gate_peer_rendezvous_port']))
        cfg = lua.globals().X4LAN_CONFIG
        self.assertEqual(items(cfg.native_gate_pilot_slots), ['00000000000ae79c', '000000000007a8fe'])
        self.assertEqual(items(cfg.native_gate_pilot_capture_source), ['stock', 'stock'])
        self.assertEqual(items(cfg.native_gate_pilot_device), [False, False])
        self.assertEqual(items(cfg.native_gate_pilot_window), [40, 72000])
        self.assertEqual(items(cfg.native_gate_ui_replay), defaults.TABLE['families'])
        self.assertEqual((cfg.native_gate_lookahead, cfg.native_gate_pilot_delay, cfg.native_gate_render_fps, cfg.native_gate_own_predict),
                         (1, 3, 60, 'present'))
        # Death and on foot arm together (deathfoot merged): death, bodies and slots are all on.
        self.assertTrue(cfg.native_gate_pilot_death)
        self.assertTrue(cfg.native_gate_pilot_foot_bodies)
        self.assertTrue(cfg.native_gate_pilot_foot_slots)
        # The loading file applied it; the probe applies it again and gets the same report.
        self.assertEqual(lua.eval('X4LAN_NATIVE_GATE_DEFAULTS.apply(X4LAN_CONFIG)==X4LAN_CONFIG._native_gate_defaults'), True)
        lua.execute('X4LAN_NATIVE_GATE_DEFAULTS.announce(X4LAN_CONFIG._native_gate_defaults)')
        logs = items(lua.globals().logs)
        self.assertEqual(sum('ERROR DEFAULT_SKIPPED' in line for line in logs), 4)
        self.assertEqual(len(items(lua.globals().info)), 1)
        self.assertIn('4 default features are off', items(lua.globals().info)[0])

    def test_explicit_values_win(self):
        lua = lua_session(",native_gate_render_fps=false,native_gate_pilot_death='off',native_gate_status_stride=1,native_gate_ui_guard=false")
        cfg, R = lua.globals().X4LAN_CONFIG, report(lua)
        self.assertIsNone(cfg.native_gate_render_fps)
        self.assertIsNone(cfg.native_gate_render_camera)   # needs render_fps
        self.assertIsNone(cfg.native_gate_pilot_death)
        self.assertIsNone(cfg.native_gate_pilot_respawn)
        self.assertEqual(cfg.native_gate_status_stride, 1)
        self.assertEqual(sorted(items(R.offlist)), ['native_gate_pilot_death', 'native_gate_render_fps', 'native_gate_ui_guard'])
        self.assertIn('native_gate_status_stride', items(R.explicit))
        # Without the guard its families and the foot (and with it the bodies and slots) are dropped loudly.
        self.assertNotIn('build', items(cfg.native_gate_ui_replay))
        skipped = {s[1]: s[2] for s in items(R.skipped)}
        self.assertEqual(skipped['native_gate_pilot_foot_slots'], 'needs native_gate_pilot_foot (off)')
        self.assertEqual(skipped['native_gate_ui_replay:build'], 'needs native_gate_ui_guard (off)')
        self.assertEqual(skipped['native_gate_pilot_foot'], 'needs native_gate_ui_guard (off)')
        self.assertEqual(skipped['native_gate_player_settings_per_slot'], 'needs native_gate_ui_replay with faction')

    def test_death_and_foot_arm_together(self):
        lua = lua_session(',native_gate_pilot_foot_bodies=true')
        cfg, R = lua.globals().X4LAN_CONFIG, report(lua)
        skipped = {s[1]: s[2] for s in items(R.skipped)}
        for key in ('native_gate_pilot_death', 'native_gate_pilot_respawn', 'native_gate_pilot_foot_slots'):
            self.assertNotIn(key, skipped)
        self.assertTrue(cfg.native_gate_pilot_death and cfg.native_gate_pilot_respawn and cfg.native_gate_pilot_foot_slots)

    def test_adaptive_and_its_exclusions(self):
        lua = lua_session(",native_gate_lookahead='adaptive'")
        cfg, R = lua.globals().X4LAN_CONFIG, report(lua)
        # Render frames run under protocol 5 as well (their gap over the adaptive permits): on, with camera and motion.
        self.assertEqual((cfg.native_gate_lookahead_max, cfg.native_gate_pilot_delay, cfg.native_gate_render_fps, cfg.native_gate_render_camera),
                         (4, None, 60, 'extrapolate'))
        skipped = {s[1]: s[2] for s in items(R.skipped)}
        self.assertNotIn('native_gate_render_fps', skipped)
        for key in ('native_gate_pilot_delay', 'native_gate_wire_crypto', 'native_gate_peer_window'):
            self.assertIn('protocol 5', skipped[key], key)
        self.assertEqual(items(R.errors), [])

    def test_refused_explicit_combinations(self):
        for extra, why in ((",native_gate_lookahead='adaptive',native_gate_wire_crypto='x4c1:a:b'", 'protocol 5'),
                           (",native_gate_wire_crypto='x4c1:a:b',native_gate_peer_window=1", 'refuses --peer-window with --wire-crypto')):
            lua = lua_session(extra)
            self.assertIn(why, items(report(lua).errors)[0])
            with self.assertRaises(LuaError):
                lua.execute('X4LAN_NATIVE_GATE_DEFAULTS.announce(X4LAN_CONFIG._native_gate_defaults)')

    def test_without_the_protocol_3_wire_or_the_plan_the_players_stack_is_skipped_loudly(self):
        lua = lua_runtime("{native_gate_test=true,slot='p1',port=1234,token=string.rep('c',32)}")
        skipped = {s[1]: s[2] for s in items(report(lua).skipped)}
        self.assertEqual(skipped['native_gate_player_identity'], 'needs native_gate_player_identity_plan')
        self.assertEqual(skipped['native_gate_lookahead'], 'needs the protocol 3 input wire (native_gate_clients)')
        self.assertIn('native_gate_ui_replay', skipped)
        self.assertTrue(lua.eval('X4LAN_CONFIG.native_gate_world_identities'))

    def test_python_applies_what_lua_applies(self):
        for extra_lua, extra_text in (('', ''), (",native_gate_lookahead='adaptive'", "X4LAN_CONFIG.native_gate_lookahead = 'adaptive'\n"),
                                      (',native_gate_pilot_fire=false,native_gate_ui_guard={"flight","missions"}',
                                       "X4LAN_CONFIG.native_gate_pilot_fire = false\nX4LAN_CONFIG.native_gate_ui_guard = {'flight','missions'}\n"),
                                      (',native_gate_pilot_foot_slots=true', 'X4LAN_CONFIG.native_gate_pilot_foot_slots = true\n')):
            with self.subTest(extra=extra_text):
                lua = lua_session(extra_lua)
                values, rep = defaults.apply(defaults.parse(SESSION.replace('@X4LANE_CLIENTS@', '2') + extra_text))
                self.assertEqual(items(report(lua).applied), rep['applied'])
                self.assertEqual([s[1] for s in items(report(lua).skipped)], [s[0] for s in rep['skipped']])
                D = lua.globals().X4LAN_NATIVE_GATE_DEFAULTS
                for key in rep['applied']:
                    self.assertEqual(D.canon(lua.globals().X4LAN_CONFIG[key]), lua.eval('X4LAN_NATIVE_GATE_DEFAULTS.canon(' + values[key] + ')'), key)


class LoudTests(unittest.TestCase):
    def test_a_growing_degraded_counter_is_an_error_row_in_the_log_only(self):
        lua = lua_runtime('nil')
        lua.execute('W=X4LAN_NATIVE_GATE_DEFAULTS.watcher()')
        line = lua.globals().W.line
        self.assertEqual(len(line('ENGINE_RNG', 60, 'installed=1 keyed=5 unmapped=0 fallback=0 failures=0')), 0)
        self.assertEqual(len(line('ENGINE_RNG', 120, 'installed=1 keyed=9 unmapped=2 fallback=0 failures=0')), 1)
        self.assertEqual(len(line('ENGINE_RNG', 180, 'installed=1 keyed=9 unmapped=2 fallback=0 failures=0')), 0)   # no growth
        self.assertEqual(len(line('ENGINE_RNG', 240, 'installed=1 keyed=9 unmapped=3 fallback=1 rid_failures=1')), 3)
        logs = items(lua.globals().logs)
        self.assertTrue(any('ERROR DEGRADED status=ENGINE_RNG unmapped=2 (+2) tick=120' in l for l in logs))
        self.assertTrue(any('rid_failures=1' in l for l in logs))
        self.assertEqual(items(lua.globals().info), [])   # no in-game line (user, 3 Nov): the rows are in the log
        # The UI families' composite and the guard (which shows its own info line).
        line('UI_TRADE_STATUS', 120, 'ui_trade=armed refused=0 lua=4/3/1/0 view=x')
        line('UI_GUARD_STATUS', 120, 'ui_guard=armed refused=2 lost=0')
        logs = items(lua.globals().logs)
        self.assertTrue(any('UI_TRADE_STATUS lua_skipped=1' in l for l in logs))
        self.assertTrue(any('UI_GUARD_STATUS refused=2' in l for l in logs))
        self.assertFalse(any('ui guard' in i for i in items(lua.globals().info)))

    def test_a_stop_names_its_reason(self):
        lua = lua_runtime('nil')
        lua.execute("X4LAN_NATIVE_GATE_DEFAULTS.stop_notice(163,'slot=2 ship_lost')")
        self.assertTrue(any('ERROR SESSION_STOPPED reason=163 (pilot death) why=slot=2 ship_lost' in l for l in items(lua.globals().logs)))
        self.assertIn('pilot death', items(lua.globals().info)[0])
        lua.execute('X4LAN_NATIVE_GATE_DEFAULTS.stop_notice(1)')
        self.assertFalse(any('reason=1 (' in l and 'ERROR' in l for l in items(lua.globals().logs)))

    def test_the_stop_reasons_are_the_registrys(self):
        registry = (ROOT/'docs/LOCKSTEP-REGISTRY.txt').read_text(encoding='utf-8')
        codes = {int(c) for c in re.findall(r'^  (1\d\d) [A-Z_]+', registry, re.M)}
        lua = lua_runtime('nil')
        named = {int(k) for k in lua.globals().X4LAN_NATIVE_GATE_DEFAULTS.STOP_REASONS.keys()}
        self.assertEqual(sorted(codes - named), [])

    def test_notices_at_the_start_menu_come_again_in_game(self):
        lua = lua_runtime('nil')
        lua.execute("menu=true;X4LAN_NATIVE_GATE_DEFAULTS.notice('first')")
        self.assertEqual(len(items(lua.globals().info)), 1)
        lua.execute('X4LAN_NATIVE_GATE_DEFAULTS.flush_notices(false)')
        self.assertEqual(len(items(lua.globals().info)), 1)
        lua.execute('X4LAN_NATIVE_GATE_DEFAULTS.flush_notices(true);X4LAN_NATIVE_GATE_DEFAULTS.flush_notices(true)')
        self.assertEqual(items(lua.globals().info), ['Multiplayer: first', 'Multiplayer: first'])
        lua.execute("menu=false;X4LAN_NATIVE_GATE_DEFAULTS.notice('second')")
        self.assertEqual(lua.eval('#X4LAN_NATIVE_GATE_DEFAULTS.pending'), 0)


# The probe on a gate mock that grants every arming (function()return 1 end) and refuses the boot (code 10), with the
# session config of the live-test spec and the overlay's UI files in their order.
PROBE_HARNESS = '''
  hashed={};logs={};info={};boots=0;is_menu=true
  state={active=1,boot_phase=0,issued=0,report_tick=0,rng='0'}
  X4LAN_SHA256={digest=function(data) hashed[#hashed+1]=data;return string.rep('b',64) end}
  gate=setmetatable({boot=function() boots=boots+1;return BOOT or 10 end,poll=function() return state end,sha256=false,
    stop=function() state.active=0;return 1 end,load_requested=function() state.boot_phase=1;return 1 end,
    world_capture_nav_v2=function(tick) return 'tick='..tick..' state=WORLD_IMPORTED_2:'..tick end},
    {__index=function(t,k) return function() return 1,'ok' end end})
  package.loadlib=function(path) assert(not path:find('fixedtick'));return function() return gate end end
  local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,new=function() return {} end,
    C={GetSaveFolderPath=function() return 'fake/45841/save' end,GetCurrentGameTime=function() return 7 end,
      IsStartmenu=function() return is_menu end,ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end,
      ShowInfoLine=function(text) info[#info+1]=text end}}
  require=function(name) assert(name=='ffi');return ffi end
  DebugError=function(s) logs[#logs+1]=s end
  RegisterEvent=function() end
  SetScript=function(name,fn) update=fn end
  GetSaveList=function() return {{filename='save_009'}} end
  LoadGame=function() end
'''
SESSION_LUA = ("{native_gate_test=true,test_profile='45841',slot='p1',port=12345,token=string.rep('c',32),native_gate_clients=2,"
               "native_gate_player_identity_plan={mode='coop',occupied='player',reputation='mirror',slots={{faction='player',ship='00000000000ae79c'},"
               "{faction='player',ship='000000000007a8fe'}},tick=20,version=1}%s}")
UI_FILES = ('native_gate_defaults.lua', 'native_gate_inputs_core.lua', 'native_gate_player_identity_core.lua', 'native_gate_player_identity.lua',
            'native_gate_ui_replay_core.lua', 'native_gate_ui_build_core.lua', 'native_gate_ui_inventory_core.lua')


class ProbeTests(unittest.TestCase):
    def probe(self, extra='', files=UI_FILES):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute('X4LAN_CONFIG=' + SESSION_LUA % extra)
        lua.execute(PROBE_HARNESS)
        for name in files:
            lua.execute((DEV/name).read_text(encoding='utf-8'))
        return lua

    def test_the_live_spec_session_arms_the_whole_stack_up_to_the_boot(self):
        lua = self.probe()
        lua.execute(PROBE)
        lua.execute('update()')
        logs = items(lua.globals().logs)
        self.assertFalse(any('CONFIG_REFUSED' in l for l in logs), logs[-3:])
        self.assertTrue(any(l.endswith('DEFAULTS version=1 applied=184 explicit=0 off=0 skipped=4') for l in logs))
        for row in ('RENDER_FRAMES_ARMED fps=60', 'RENDER_CAMERA_ARMED mode=extrapolate', 'TICK_RATE_ARMED hz=30', 'PLAYER_IDENTITY BOUND slot=p1 mode=coop slots=2'):
            self.assertTrue(any(row in l for l in logs), row)
        # The stack joins the boot digest; the refused boot is loud: an ERROR row and an info line with the reason.
        boot = [h for h in items(lua.globals().hashed) if h.startswith('SNAPSHOT_BYTES_1|')]
        self.assertEqual(len(boot), 1)
        self.assertIn('|DEFAULTS_1|' + 'b'*64, boot[0])
        self.assertTrue(logs[-1].endswith('ERROR REFUSED code=10 stage=1'), logs[-1])
        self.assertTrue(any('refused code=10 stage=1 (start refused)' in i for i in items(lua.globals().info)))

    def test_without_the_defaults_module_the_probe_refuses(self):
        lua = self.probe(files=())
        with self.assertRaisesRegex(LuaError, 'native gate defaults missing'):
            lua.execute(PROBE)
        self.assertEqual(lua.globals().boots, 0)
        self.assertTrue(any('ERROR DEFAULTS_MISSING' in l for l in items(lua.globals().logs)))
        self.assertTrue(any('did not start' in i for i in items(lua.globals().info)))
        # The bare gate says so and runs without the module: nothing is defaulted (here not even the hash scope).
        lua = self.probe(',native_gate_defaults=false', files=())
        with self.assertRaisesRegex(LuaError, 'invalid gate hash scope'):
            lua.execute(PROBE)
        self.assertTrue(any('DEFAULTS_OFF native_gate_defaults=false' in l for l in items(lua.globals().logs)))

    def test_a_config_the_probe_refuses_is_an_info_line(self):
        lua = self.probe(",native_gate_hash='other'")
        with self.assertRaisesRegex(LuaError, 'invalid gate hash scope'):
            lua.execute(PROBE)
        self.assertTrue(any('ERROR CONFIG_REFUSED' in l and 'invalid gate hash scope' in l for l in items(lua.globals().logs)))
        self.assertTrue(any('did not start - invalid gate hash scope' in i for i in items(lua.globals().info)))

    def test_refused_defaults_never_boot(self):
        lua = self.probe(",native_gate_lookahead='adaptive',native_gate_wire_crypto='x4c1:a:b'")
        with self.assertRaisesRegex(LuaError, 'defaults refused'):
            lua.execute(PROBE)
        self.assertEqual(lua.globals().boots, 0)

    def test_a_frame_error_stops_with_an_info_line(self):
        # The bare gate (a minimal mock): a schema the gate does not report fails the first admitted frame.
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute("X4LAN_CONFIG={native_gate_defaults=false,native_gate_test=true,test_profile='45841',slot='p1',native_gate_hash='clock',"
                    "native_gate_snapshot_sha=string.rep('a',64),port=12345,token=string.rep('c',32),native_gate_rng_schema=2}")
        lua.execute(PROBE_HARNESS.replace("{__index=function(t,k) return function() return 1,'ok' end end}", '{}'))
        lua.execute((DEV/'native_gate_defaults.lua').read_text(encoding='utf-8'))
        lua.execute(PROBE)
        lua.execute('BOOT=0;update();state.boot_phase=3;state.issued=1;is_menu=false;update()')
        logs = items(lua.globals().logs)
        self.assertTrue(any('ERROR' in l and 'native RNG schema mismatch' in l for l in logs), logs[-4:])
        self.assertTrue(any('the session stopped - native RNG schema mismatch' in i for i in items(lua.globals().info)))


class CheckerTests(unittest.TestCase):
    def test_an_empty_session_config_validates_to_the_full_stack(self):
        options_bits, failures, rep = options.check_report(SESSION)
        self.assertEqual(failures, [])
        self.assertEqual(len(rep['applied']), len(defaults.ON_KEYS) - 4)
        text, _ = defaults.effective(SESSION)
        for key in rep['applied']:
            self.assertRegex(text, r'X4LAN_CONFIG\.' + key + r' = ')
        self.assertGreater(options_bits, 0)

    def test_explicit_off_keys_still_validate(self):
        for key in ('native_gate_world_identities', 'native_gate_pilot_stock', 'native_gate_ui_guard', 'native_gate_render_rng_split',
                    'native_gate_view_points', 'native_gate_serial_pathing'):
            with self.subTest(key=key):
                self.assertEqual(options.check(SESSION + f'X4LAN_CONFIG.{key} = false\n')[1], [])
        failures = options.check(SESSION + "X4LAN_CONFIG.native_gate_lookahead = 'adaptive'\nX4LAN_CONFIG.native_gate_wire_crypto = 'x4c1:a:b'\n")[1]
        self.assertTrue(any(f['rule'].startswith('defaults: ') for f in failures))
        # Bodies and the death module both set explicitly: compatible since the deathfoot merge.
        self.assertEqual(options.check(SESSION + 'X4LAN_CONFIG.native_gate_pilot_foot_bodies = true' + chr(10) + 'X4LAN_CONFIG.native_gate_pilot_death = true' + chr(10))[1], [])
        self.assertEqual(options.check(SESSION + 'X4LAN_CONFIG.native_gate_pilot_foot_bodies = true' + chr(10))[1], [])

    def test_a_bare_or_non_session_config_is_checked_as_written(self):
        bare = SESSION + 'X4LAN_CONFIG.native_gate_defaults = false\n'
        self.assertEqual(defaults.effective(bare)[0], bare)
        self.assertEqual(options.check(bare)[0], 0)
        fragment = 'X4LAN_CONFIG.native_gate_world_identities = true\n'
        self.assertEqual(defaults.effective(fragment)[0], fragment)


class BuilderTests(unittest.TestCase):
    BASE = '''X4LAN_CONFIG.enabled = false
X4LAN_CONFIG.native_gate_test = true
X4LAN_CONFIG.native_gate_hash = "battle"
X4LAN_CONFIG.test_profile = "45841"
X4LAN_CONFIG.host = "127.0.0.1"
X4LAN_CONFIG.port = @X4LANE_PORT@
X4LAN_CONFIG.token = "@X4LANE_TOKEN@"
X4LAN_CONFIG.slot = "@X4LANE_SLOT@"
X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@
'''

    def overlay(self, root, name, *extra):
        base = root/'base'
        if not base.exists():
            for d in ('ui', 'native', 'md'):
                (base/d).mkdir(parents=True)
            (base/'ui/config.lua.append').write_text(self.BASE)
            (base/'ui/native_gate_probe.lua').write_text('-- probe\n')
            (base/'native/x4lan_gate_64.dll').write_bytes(b'dll')
            (base/'ui.xml.files').write_text('ui/native_gate_probe.lua\n')
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            players.main(['overlay', '--base', str(base), '--out', str(root/name), '--probe', str(DEV/'native_gate_probe.lua'),
                          '--players', '2', '--mode', 'coop', '--source', 'stock', *extra])
        return root/name, out.getvalue()

    def test_defaults_only_writes_the_session_and_the_plan(self):
        with tempfile.TemporaryDirectory() as d:
            out, notes = self.overlay(Path(d), 'a', '--defaults-only')
            text = (out/'ui/config.lua.append').read_text()
            own = [line for line in text.splitlines() if line.startswith('X4LAN_CONFIG.') and line not in self.BASE.splitlines()]
            self.assertEqual([line.split(' = ')[0] for line in own], ['X4LAN_CONFIG.native_gate_player_identity_plan'])
            files = (out/'ui.xml.files').read_text().split()
            self.assertEqual(files[0], 'ui/native_gate_defaults.lua')
            self.assertEqual(files[-1], 'ui/native_gate_probe.lua')
            for name in defaults.UI_FILES + defaults.MD_FILES:
                self.assertTrue((out/name).is_file(), name)
            self.assertEqual(options.check(text)[1], [])
            self.assertIn('SESSION DEFAULTS: 3 lines written', notes)
            # The flags the defaults set are refused with --defaults-only.
            with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
                self.overlay(Path(d), 'b', '--defaults-only', '--death')

    def test_a_built_run_writes_its_overrides_and_turns_off_what_it_leaves_out(self):
        with tempfile.TemporaryDirectory() as d:
            out, notes = self.overlay(Path(d), 'c', '--present', '--ui-replay', '--ui-guard', '--fire', 'record', '--death')
            text = (out/'ui/config.lua.append').read_text()
            values = defaults.parse(text)
            for key in ('native_gate_pilot_stock', 'native_gate_pilot_slots', 'native_gate_pilot_present', 'native_gate_pilot_fire', 'native_gate_actor_diag'):
                self.assertNotIn(key, values, key)    # equal to the defaults: left out
            self.assertEqual(values['native_gate_ui_replay'], 'true')
            self.assertEqual(values['native_gate_world_hash_stride'], '18000')
            for key in ('native_gate_pilot_foot', 'native_gate_pilot_respawn', 'native_gate_hit_claims', 'native_gate_own_predict'):
                self.assertEqual(values[key], 'false', key)
            self.assertEqual(options.check(text)[1], [])
            self.assertIn('off in this run: ', notes)

    def test_the_base_builder_ships_the_module_and_says_when_it_is_bare(self):
        builder = load('build_native_gate_overlay_defaults', 'tools/dev/build_native_gate_overlay.py')
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            dll = root/'gate.dll'
            dll.write_bytes(b'dll')
            fixed = root/'fixed.dll'
            fixed.write_bytes(b'dll')
            out = root/'clock'
            argv = ['build', '--gate-dll', str(dll), '--fixedtick-dll', str(fixed), '--mode', 'clock', '--out', str(out)]
            with mock.patch('sys.argv', argv), contextlib.redirect_stdout(io.StringIO()) as printed:
                builder.main()
            self.assertIn('DEFAULTS_OFF', printed.getvalue())
            self.assertEqual((out/'ui.xml.files').read_text().split()[0], 'ui/native_gate_defaults.lua')
            self.assertTrue((out/'ui/config.lua.append').read_text().startswith('X4LAN_CONFIG.native_gate_defaults = false  -- a fixed-tick boot'))


if __name__ == '__main__':
    unittest.main()

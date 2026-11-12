-- The production Linux gate module through LuaJIT's package.loadlib, outside X4:
-- the Lua 5.1 ABI resolves from the running LuaJIT, the `gate` table holds the
-- core methods and none of the unported module methods, and every start refuses
-- at the file pin without touching memory.  luajit test_gate_lua.lua x4lan_gate.so
local path=assert(arg[1],'module path required')
local open=assert(package.loadlib(path,'luaopen_x4lan_gate'))
local gate=assert(open(),'module did not resolve the Lua ABI')
local core={'sha256','start','boot','load_requested','report','poll','stop','frame_timing','qpc_us','tick_rate','wire','propose',
 'world_motion_config','world_motion_capture','world_motion_row','world_motion_status',
 'world_rolling_leaves_config','world_rolling_leaves_row','world_rolling_raw_add','world_rolling_raw_row',
 'economy_watch_config','economy_watch_capture','economy_watch_status',
 'economy_load_rng','economy_load_rng_status','economy_load_rng_row',
 'stock_keyboard_observer_request','stock_keyboard_observer_status','stock_keyboard_observer_drain',
 'stock_keyboard_identity','stock_keyboard_replay_request','stock_keyboard_replay_capture',
 'player_reference_request','player_reference_status','factory_probe_request','factory_probe_status',
 'player_external_camera_probe','player_presentation_probe',
 'player_view_query_observe','player_view_query_phase','player_view_query_finish',
 'player_ui_callback_close','player_ui_callback_result',
 'player_presentation_fork_arm','player_presentation_fork_result',
 'player_ui_overlay_arm','player_ui_overlay_result','player_ui_overlay_abort',
 'pilot_present_pause','pilot_present_resume',
 'player_seat_preflight_request','player_seat_preflight',
 'player_control_bank_census_request','player_control_bank_census',
 'player_control_bank_probe_request','player_control_bank_probe_status',
 'player_control_boundary_request','player_control_boundary_status',
 'stock_keyboard_replay_stage','stock_keyboard_replay_status',
 'navmesh_post_observe','navmesh_post_observe_status','navmesh_post_observe_row',
 'relay_host','lookahead','lookahead_status','peer_links','peer_status','wire_crypto','wire_crypto_status','platform','verify_stage',
 'world_watch','world_capture','world_capture_nav_v2','world_capture_rolling','world_page','world_detail','world_banks','saved_identity','world_health','world_hash_shields','world_hash_shields_status','flight_faults','flight','avoidance','agent','ship','spline','body','actor','params','snapshot_fixture','snapshot_actor','snapshot_enroll','snapshot_status',
 'path_determinism','path_determinism_status','path_budget','path_budget_status','region_rng','region_rng_status','region_rng_row',
 'ai_wait_rng','ai_wait_rng_status','ai_wait_rng_row','read_cost',
 'md_resume_rng','md_resume_rng_status','md_resume_rng_row','boot_hold','boot_hold_status',
 'event_order','event_order_status','event_rows','event_order_row','event_rng','event_rng_status','event_rng_row','build_rng','build_rng_status','build_rng_row',
 'gravidar_key','gravidar_time','gravidar_key_status','gravidar','ai_schedule','ai_schedule_status','ai_schedule_fallbacks',
 'gravidar_deadlines','gravidar_deadlines_status','travel_snapshot','travel_snapshot_status',
 'travel_determinism','travel_determinism_status',
 'path_transform_observe','path_transform_observe_status','path_transform_observe_row',
 'runtime_ids','runtime_ids_status','runtime_ids_word','runtime_ids_map_row','runtime_ids_caller_row','runtime_ids_callers',
 'script_rng','script_rng_status','script_rng_row','script_rng_own_entries','md_timed_rng','md_timed_rng_status','find_order','find_order_status','find_order_row','loadout_order','loadout_order_status','loadout_order_row','nav_key','nav','attention','shape','attention_all','attention_all_status','step_trace','step_trace_arm','step_trace_status','step_trace_row','step_chain','fp_redirect','fp_redirect_watch','fp_redirect_status','fp_redirect_row','fp_redirect_solver','coverage','coverage_frame','coverage_status','coverage_row','nav_cadence','nav_cadence_status','nav_cadence_row','nav_cadence_watch','nav_probe_init','nav_params_init','created_identities','portable_created','created_load','created_identities_status','created_row','created_identity','created_runtime','gravidar_wide','gravidar_wide_status','spawn_rng','spawn_rng_status','spawn_rng_row','fork_save','fork_save_status','fp_redirect_drones','fp_attack','fp_attack_watch','fp_attack_status','fp_attack_row',
 'weapon_fire_rng','weapon_fire_rng_status','engine_refresh_rng','engine_refresh_rng_status','spline_join_all','spline_join_all_status',
 'mass_traffic','mass_traffic_status','region_pass','region_pass_status','pass_stride','actor_rng_all','actor_rng_all_status','actor_rng_rows','actor_rng_row',
 'player_timer','player_timer_status','tempzone_order','tempzone_order_status','tempzone_order_row','warp_rng','warp_rng_status','warp_rng_row',
 'warp_exit','warp_exit_status','flight_limits','flight_limits_status','body1_mass','body1_mass_status','body1_mass_census','body_step_diag','body_step_status','body_step','velocity','velocity_status','impulse_diag','impulse_status','impulse','ik_joints','ik_status','ik_step','ik_step_status','nav_probes','steer_roll','steer_roll_status','avoid_angle','avoid_angle_status','pursuit_speed','pursuit_speed_status','ik_wrap','ik_wrap_status','region_camera','region_camera_frame','region_camera_status','runtime_ids_local','runtime_ids_probe'}
local count=0
for _,name in ipairs(core) do assert(type(gate[name])=='function','missing gate.'..name) end
for _ in pairs(gate) do count=count+1 end
assert(count==#core,'unexpected gate methods: '..count)
for _,name in ipairs({'world_trace','thread_rng','boot_rng_anchor','pilot_inputs'}) do
 assert(gate[name]==nil,'unported method present: '..name)
end
assert(gate.platform()=='linux')
assert(gate.player_presentation_probe()==0,'presentation must refuse without an admitted experiment')
assert(gate.world_rolling_leaves_config(1,2)==0 and gate.world_rolling_leaves_row(0)==nil and
 gate.world_rolling_raw_add('0000000000000001')==0 and gate.world_rolling_raw_row(0)==nil,
 'rolling world diagnostics must refuse outside a verified session')
assert(gate.player_view_query_phase(0)==1, 'unarmed view-query phase must be a no-op')
assert(gate.player_view_query_observe()==0 and gate.player_view_query_finish()==nil,
 'view query must refuse outside an admitted worker')
assert(gate.player_ui_callback_close()==0 and gate.player_ui_callback_result()==nil,
 'UI callback observer must refuse outside an admitted worker')
assert(gate.player_presentation_fork_arm()==0 and gate.player_presentation_fork_result()==nil,
 'presentation fork must refuse outside an admitted worker')
assert(gate.player_ui_overlay_arm()==0 and gate.player_ui_overlay_result()==nil and
 gate.pilot_present_pause()==0 and gate.pilot_present_resume()==0,
 'UI overlay must refuse outside an admitted worker')
assert(gate.player_external_camera_probe()==0,
 'camera diagnostic must refuse outside an admitted worker')
assert(gate.travel_determinism()==0,'ordered jobs must refuse outside a verified session')
assert(gate.build_rng()==0,'build RNG must refuse outside a verified session')
assert(gate.stock_keyboard_observer_request()==0,'keyboard observer must refuse outside an admitted worker')
assert(gate.stock_keyboard_observer_status()==nil and gate.stock_keyboard_observer_drain()==nil,
 'keyboard observer reads must refuse outside an admitted worker')
assert(gate.stock_keyboard_identity()==nil and gate.stock_keyboard_replay_request()==0 and
 gate.stock_keyboard_replay_capture()==nil and gate.stock_keyboard_replay_stage()==0 and
 gate.stock_keyboard_replay_status()==nil,'keyboard replay methods must refuse outside a verified session')
assert(gate.player_reference_request()==0 and gate.player_reference_status()==nil,
 'player reference probe must refuse outside a verified session')
assert(gate.player_control_bank_probe_request()==0 and gate.player_control_bank_probe_status()==nil,
 'control bank roundtrip must refuse outside a verified worker boundary')
assert(gate.player_control_boundary_request()==0 and gate.player_control_boundary_status()==nil,
 'control boundary diagnostic must refuse outside a verified worker boundary')
assert(gate.event_rng('key')==0 and gate.event_rng('observe', 1, 2)==0 and gate.event_rng('invalid')==0,
 'event RNG must refuse outside a verified session')
assert(gate.world_motion_config('000000000006ec76',1,2)==0,'motion observer must refuse outside a verified live session')
-- Canonical shields arm only on a running gate (its image verified), and only in canonical mode.
assert(gate.world_hash_shields('canonical')==0 and gate.world_hash_shields('view')==0 and gate.world_hash_shields_status()=='mode=off canonical=0')
assert(gate.sha256('')=='e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855')
assert(gate.sha256('abc')=='ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
assert(gate.sha256(string.rep('a',1000000))=='cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0')
assert(gate.sha256('a\0b')~=gate.sha256('a'),'embedded zero bytes must count')
assert(gate.poll()==nil and gate.frame_timing()==nil)
assert(gate.wire(3,4)==1 and gate.wire(3,9)==0 and gate.wire(2,3)==0 and gate.wire(1.5,2)==0)
assert(gate.lookahead(2)==1 and gate.lookahead(9)==0 and gate.lookahead(-1)==0)
assert(gate.wire(2,2)==1 and gate.lookahead(1)==0,'protocol 4 needs wire 3')
assert(gate.relay_host('10.1.2.3')==1 and gate.relay_host('256.1.1.1')==0 and gate.relay_host('1.2.3')==0)
assert(gate.relay_host('127.0.0.1')==1)
local token,digest=string.rep('0',32),string.rep('a',64)
-- Not the pinned X4 build: refused at the file pin; the gate stays unused.
assert(gate.boot(1,47000,token,5000,digest,0)==11 and gate.verify_stage()=='file')
assert(gate.start(1,47000,token,5000,digest,0)==11)
assert(gate.boot(1,47000,'short',5000,digest,0)==10)
assert(gate.boot(1,80,token,5000,digest,0)==10)
assert(gate.boot(1,47000,token,5000,digest,32)==105,'unported option bits refuse before anything else') -- 32: ai_diag
assert(gate.boot(1,47000,token,5000,digest,0.5)==10)
-- World identities (2^46) are a ported option: refused only at the file pin here.
assert(gate.boot(1,47000,token,5000,digest,70368744177664)==11)
assert(gate.boot(1,47000,token,5000,digest,70368744177664+2)==11) -- with the frame seed
assert(gate.boot(1,47000,token,5000,digest,70368744177664+3)==11) -- with serial pathing
assert(gate.boot(1,47000,token,5000,digest,70368744177664+32)==105) -- 32: ai_diag (4, flight_diag, is ported)
local summary,why=gate.world_capture(1)
assert(summary==nil and why:match('stage=') and gate.world_page(0)==nil and gate.world_banks()==nil)
local rolling, rolling_why=gate.world_capture_rolling(1,1)
assert(rolling==nil and rolling_why:match('stage='))
assert(gate.poll()==nil and gate.stop()==0 and gate.load_requested()==0)
-- Non-blocking save by fork: refused outside an admitted session; its status is readable once stopped.
assert(gate.fork_save(600,'join_600','/tmp/x4save')==0)
assert(gate.fork_save_status():match('^state=off tick=0 '))
assert(gate.report(0,digest)==0 and gate.propose(0,'')==0)
assert(gate.wire(3,3)==1,'still configurable: never started')
assert(type(gate.qpc_us())=='number' and gate.qpc_us()>0)
assert(gate.lookahead_status():match('^lookahead=0 '))
-- Wire crypto (opt-in): needs protocol 3 and a well-formed credential; nothing is armed until then.
assert(gate.wire(2,2)==1 and gate.wire_crypto('x4c1:'..string.rep('1',64)..':'..string.rep('2',64))==0,'wire crypto needs wire 3')
assert(gate.wire(3,2)==1 and gate.wire_crypto('x4c1:short')==0 and gate.wire_crypto(nil)==0)
assert(gate.wire_crypto_status():match('^armed=0 '))
assert(gate.wire_crypto('x4c1:'..string.rep('1',64)..':'..string.rep('2',64))==1 and gate.wire_crypto_status():match('^armed=1 established=0 keyed=0 '))
-- Armed under protocol 2 (a later gate.wire(2,2)): start/boot refuse with 10 before any pin is read.
assert(gate.wire(2,2)==1 and gate.boot(1,47000,token,5000,digest,0)==10 and gate.start(1,47000,token,5000,digest,0)==10)
assert(gate.wire(3,2)==1 and gate.boot(1,47000,token,5000,digest,0)==11,'armed with protocol 3: the file pin decides')
-- A second load returns the same module state (X4 reloads its UI).
local again=assert(package.loadlib(path,'luaopen_x4lan_gate'))()
assert(again.wire(2,2)==1 and again.poll()==nil)
print('LUA BRIDGE PASS methods='..count)

assert(gate.factory_probe_request()==0 and gate.factory_probe_status()==nil,
 'player factory probe must refuse outside a verified session')
assert(gate.player_seat_preflight_request()==0 and gate.player_seat_preflight()==nil,
 'seat preflight must refuse outside a verified session')

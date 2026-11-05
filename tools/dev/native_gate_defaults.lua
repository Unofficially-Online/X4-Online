-- Lockstep session defaults (user decision, 1 November 2026: "everything should be on by default, fail loudly not
-- silently"; reports/LOCKSTEP-DEFAULTS-20261101.txt). Inert without a lockstep session: nothing happens unless
-- X4LAN_CONFIG.native_gate_test is set. Once a session is configured (native_gate_test, slot pN, port, token), every
-- feature and sync fix below is ON unless the session config sets its key to false (or 'off'); a key the config sets
-- keeps the config's value. Diagnostics and measurement keys (D.DIAGNOSTIC) and the keys of D.EXCLUDED stay off.
-- native_gate_defaults = false runs the bare gate (every key as configured, absent = off) and says so in the log.
-- Every default that cannot apply (a prerequisite off or missing, a conflict) is LOUD: an ERROR row with the reason and
-- an info line in game; explicit combinations the relay or the gate would refuse stop before the boot (D.apply errors).
-- Loaded first by every session overlay (ui.xml.files), so the adapters that read X4LAN_CONFIG while loading see the
-- same values as the probe; the probe applies it again (idempotent: the report is kept in cfg._native_gate_defaults).
-- Mirror for the Python tools: tools/dev/native_gate_defaults.py parses this file (one entry per line, below).
X4LAN_NATIVE_GATE_DEFAULTS=nil
local D={VERSION=1}
-- D.ON, in arming order (a key's prerequisites come before it): {'key',VALUE,'needs','why'}, one entry per line.
-- needs, space-separated: 'key' (on), 'key=value', 'key~item' (a list holding item), '!key' (off: a conflict), '@wire3'
-- (protocol 3 input wire: native_gate_clients), '@plan' (native_gate_player_identity_plan), '@fixed' (not protocol 5).
-- VALUE '@name' is derived from the session (D.DERIVE); '@credential' and '@configured' have no default (on only when
-- the session sets them: their absence is reported).
D.ON={
 {'native_gate_hash','battle','','session world: the battle scope of snapshot 005 (12 fixture actors in the declared state)'},
 {'native_gate_snapshot_sha','7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8','','session world: the owned pristine 7.3 snapshot 005, booted without the fixed-tick DLL'},
 {'native_gate_snapshot_fixture',{'0000000000176a13','0000000000176a74','0000000000176ad5','0000000000176b36','0000000000176b97','0000000000176bf8','0000000000176c59','0000000000176c77','0000000000176c95','0000000000176cb3','0000000000176cd1','0000000000176cef'},'native_gate_snapshot_sha native_gate_hash=battle','session world: the saved fixture actors of snapshot 005, enrolled by saved ID'},
 {'native_gate_world_identities',true,'','saved-ID map: every keyed fix and the identity plan resolve objects through it'},
 {'native_gate_serial_pathing',true,'','pathing pass serialized at the gate join'},
 {'native_gate_seed_rng',true,'','per-tick seeded thread RNG words'},
 {'native_gate_flight_rng',true,'','flight model draws keyed'},
 {'native_gate_flight_full',true,'','every flight controller covered (avoidance refresh needs it)'},
 {'native_gate_avoidance_refresh',true,'native_gate_flight_full','avoidance refresh keyed'},
 {'native_gate_steering_init',true,'native_gate_flight_rng','steering initialization keyed'},
 {'native_gate_steering_schema',2,'','steering lifetime in the declared state'},
 {'native_gate_ai_diag',true,'','installs the AI hooks the radar needs (ai_rng is excluded by the keyed script RNG); AI rows only at the trace stride'},
 {'native_gate_radar_diag',true,'native_gate_ai_diag','installs the radar hooks radar_serial needs; rows only when drained'},
 {'native_gate_radar_serial',true,'native_gate_radar_diag native_gate_serial_pathing','radar passes serialized'},
 {'native_gate_radar_rng',true,'native_gate_radar_serial','radar draws keyed'},
 {'native_gate_radar_initial_refresh',true,'native_gate_radar_rng','radar first refresh keyed'},
 {'native_gate_agent_rng',true,'','agent draws keyed'},
 {'native_gate_agent_full',true,'','every agent covered (spline refresh and join need it)'},
 {'native_gate_spline_diag',true,'','installs the spline hooks spline_refresh and spline_join need; rows only at the trace stride'},
 {'native_gate_spline_refresh',true,'native_gate_spline_diag native_gate_agent_full','spline refresh keyed'},
 {'native_gate_spline_join',true,'native_gate_spline_diag native_gate_agent_full native_gate_serial_pathing','spline builds joined at the gate'},
 {'native_gate_spline_join_all',true,'native_gate_spline_join','every spline build joined'},
 {'native_gate_ship_rng',true,'','ship draws keyed'},
 {'native_gate_params_init',true,'','flight parameters initialized at a fixed point'},
 {'native_gate_weapon_diag',true,'','registers the fixture guns with the weapon module: their draws are keyed by actor and slot (lockstep_weapon.h); rows only at the trace stride'},
 {'native_gate_weapon_rng',true,'','weapon draws keyed'},
 {'native_gate_nav_rng',true,'','navigation draws keyed'},
 {'native_gate_nav_initial_refresh',true,'','navigation first refresh keyed'},
 {'native_gate_attention_rng',true,'','attention draws keyed'},
 {'native_gate_attention_initial_refresh',true,'','attention first refresh keyed'},
 {'native_gate_death_rng',true,'','death draws keyed'},
 {'native_gate_bullet_rng',true,'','bullet draws keyed'},
 {'native_gate_shape_rng',true,'','physics shape draws keyed'},
 {'native_gate_shape_initial_refresh',true,'','physics shape first refresh keyed'},
 {'native_gate_interrupt_diag',true,'','installs the interrupt hooks the Windows script RNG keys on (no own entries); rows are trace rows'},
 {'native_gate_nested_actions',true,'','nested MD/AI actions observed: the Windows script RNG keys on them'},
 {'native_gate_actor_diag',true,'','the actors raw native state (hull bits, navigation, physics) joins the declared digest'},
 {'native_gate_strict_state',true,'native_gate_actor_diag','strict health and navigation state in the digest'},
 {'native_gate_shield_refresh',true,'native_gate_actor_diag','shield refresh before the state read'},
 {'native_gate_strict_physics',true,'native_gate_actor_diag','physics bodies and attention in the digest'},
 {'native_gate_spline_state',true,'native_gate_strict_physics','spline state in the digest'},
 {'native_gate_order_state',true,'','order queues in the digest (ui/native_gate_orders.lua)'},
 {'native_gate_fleet_state',true,'','fleet membership in the digest (ui/native_gate_fleet.lua)'},
 {'native_gate_rng_schema',2,'','the gate RNG schema is checked every tick (a mismatch stops the session)'},
 {'native_gate_world_hash_stride',1800,'','census: the world digest joins the report every 1800 ticks (a split stops at the relay)'},
 {'native_gate_world_hash_schema','WORLD_IMPORTED_2','native_gate_world_hash_stride','census with the enriched navigation fields'},
 {'native_gate_world_hash_shields','canonical','native_gate_world_hash_stride','census shields in canonical form'},
 {'native_gate_path_determinism',3,'native_gate_serial_pathing','navigation stamps and pathing budget fixed'},
 {'native_gate_travel_snapshot',true,'native_gate_serial_pathing native_gate_path_determinism','wingmen read a per-pass travel snapshot'},
 {'native_gate_path_budget',100,'native_gate_serial_pathing','pathing round robin by count, not by clock'},
 {'native_gate_path_transforms','sim','native_gate_serial_pathing','pathing reads simulation transforms, not view caches'},
 {'native_gate_body_pose','sim','','new physics bodies take simulation poses'},
 {'native_gate_gravidar_key_all',true,'','Gravidar units keyed'},
 {'native_gate_gravidar_time_sync',true,'','Gravidar times on game time'},
 {'native_gate_gravidar_load_pass',true,'','Gravidar load pass fixed'},
 {'native_gate_gravidar_sector_order',true,'','Gravidar sectors in a fixed order'},
 {'native_gate_gravidar_deadlines',true,'native_gate_world_identities native_gate_gravidar_time_sync','Gravidar deadlines keyed'},
 {'native_gate_gravidar_wide','sort','native_gate_radar_serial native_gate_world_identities','stations radar in a deterministic zone order'},
 {'native_gate_event_order',true,'','due events in a fixed order'},
 {'native_gate_event_rng','key','native_gate_world_identities native_gate_event_order','event listeners draws keyed'},
 {'native_gate_script_rng','key','native_gate_world_identities native_gate_interrupt_diag native_gate_nested_actions','MD and AI script draws keyed'},
 {'native_gate_script_rng_mask',7,'native_gate_script_rng','script RNG scopes: AI, MD, boot'},
 {'native_gate_script_rng_unmapped','key','native_gate_script_rng','AI owners without a portable ID keyed by marker and rank'},
 {'native_gate_ai_schedule',true,'','AI scripts scheduled in a fixed order'},
 {'native_gate_md_timed_rng',true,'','MD timed cues keyed'},
 {'native_gate_md_resume_rng',true,'','MD resumed cues keyed'},
 {'native_gate_ai_wait_rng',true,'native_gate_world_identities','load-time AI waits keyed'},
 {'native_gate_boot_hold',true,'','the universe is held until the first gated frame'},
 {'native_gate_find_order','sort','native_gate_world_identities','find candidate lists in saved-ID order'},
 {'native_gate_region_rng','key','native_gate_world_identities','region passes keyed'},
 {'native_gate_spawn_rng','key','','ship loadout generation keyed'},
 {'native_gate_created_identities',true,'native_gate_world_identities','created objects get portable IDs'},
 {'native_gate_portable_created',true,'native_gate_created_identities','created objects keyed by their creation frame'},
 {'native_gate_created_load',true,'native_gate_world_identities native_gate_created_identities','load-created objects keyed'},
 {'native_gate_speech_game_time',true,'','NPC speech on game time'},
 {'native_gate_weapon_fire_rng',true,'native_gate_world_identities','weapon fire draws keyed'},
 {'native_gate_engine_refresh_rng',true,'native_gate_world_identities','engine parameter refreshes keyed'},
 {'native_gate_player_timer',true,'','player timers on game time'},
 {'native_gate_ship_sound_rng','split','','ship sounds on their own RNG word'},
 {'native_gate_warp_rng',true,'native_gate_world_identities','warp draws keyed'},
 {'native_gate_tempzone_order','order','native_gate_world_identities native_gate_serial_pathing native_gate_travel_snapshot','temporary zones in a fixed order'},
 {'native_gate_build_rng',true,'native_gate_world_identities','build storage reschedules keyed'},
 {'native_gate_economy_load_rng',true,'native_gate_world_identities','economy schedule keyed at the load'},
 {'native_gate_loadout_order','canonical','','restock wishes in macro-id order'},
 {'native_gate_order_canon','canonical','','generated loadouts and script tables in id order'},
 {'native_gate_pick_order','canonical','','weighted library picks in file order'},
 {'native_gate_macro_arena',true,'','macros in id hash order'},
 {'native_gate_shape_order','sync','','collision files loaded before the part is built'},
 {'native_gate_character_play','sync','','a body animation start loads its character and motion first and draws on a keyed word (stop 188): the DF2 wharf patrol split without it'},
 {'native_gate_portable_math',true,'','CRT math entries on the portable library'},
 {'native_gate_exact_reciprocal',true,'','RCPSS/RSQRTSS exactly rounded'},
 {'native_gate_decimal',true,'','saved values read by the portable decimal reader'},
 {'native_gate_fp_env','default','','MXCSR 0x1f80 on every X4 thread'},
 {'native_gate_cutscene_ready',true,'native_gate_serial_pathing','cutscene readiness at the join'},
 {'native_gate_speak_timer',true,'','speak timers on game time'},
 {'native_gate_fast_reads',true,'','small reads by a guarded copy (same values, faster)'},
 {'native_gate_tick_hz',30,'','30 simulation steps per second (the relay runs --tick-hz 30)'},
 {'native_gate_runtime_ids',true,'native_gate_world_identities','runtime IDs allocated alike on every client'},
 {'native_gate_runtime_ids_barrier',true,'native_gate_runtime_ids','the runtime ID word joins the digest'},
 {'native_gate_runtime_ids_local',true,'native_gate_runtime_ids native_gate_created_identities','client-local objects take local runtime IDs'},
 {'native_gate_render_rng_split',true,'','the render runs on its own RNG word'},
 {'native_gate_bullet_cadence','every','native_gate_bullet_rng','every bullet tested every frame (camera-independent)'},
 {'native_gate_nav_cadence','far','native_gate_nav_rng','navigation refresh cadence camera-independent'},
 {'native_gate_nav_probe_init',true,'native_gate_nav_cadence','obstacle probe deadlines fixed'},
 {'native_gate_nav_params_init',true,'native_gate_nav_cadence','parameter refresh deadlines fixed'},
 {'native_gate_nav_player',true,'native_gate_nav_cadence','the player ship in the navigation cadence'},
 {'native_gate_attention_all',true,'native_gate_attention_rng','attention high on every client where any player is'},
 {'native_gate_actor_rng_all',true,'native_gate_world_identities native_gate_agent_rng native_gate_ship_rng','every portable ship keyed'},
 {'native_gate_view_points','players','','the players ships are the view points on every client'},
 {'native_gate_view_points_source','ship','native_gate_view_points','view points at the ships'},
 {'native_gate_view_triggers','block','native_gate_view_points','camera triggers blocked in the simulation'},
 {'native_gate_view_ui','block','native_gate_view_points native_gate_runtime_ids_local','UI view queries blocked in the simulation'},
 {'native_gate_mass_traffic','sync','','mass traffic joined at the gate'},
 {'native_gate_region_pass','sync','','region passes joined at the gate'},
 {'native_gate_pass_stride',2,'native_gate_mass_traffic=sync','traffic and region passes every 2 ticks'},
 {'native_gate_region_camera','view','native_gate_view_points native_gate_region_pass=sync','region camera from the view points'},
 {'native_gate_region_fields','view','native_gate_view_points','region fields from the view points'},
 {'native_gate_field_fill','view','native_gate_region_fields','field fill from the view points'},
 {'native_gate_mass_traffic_camera','view','native_gate_view_points native_gate_mass_traffic=sync','mass traffic camera from the view points'},
 {'native_gate_effect_camera','view','native_gate_view_points','effects from the view points'},
 {'native_gate_highway_camera','view','native_gate_view_points','highways from the view points'},
 {'native_gate_highway_speed','key','native_gate_view_points native_gate_world_identities','highway speeds keyed'},
 {'native_gate_physics_scenes','viewpoints','native_gate_view_points','physics scenes per view point'},
 {'native_gate_view_scenes',true,'native_gate_physics_scenes','a physics scene per sector'},
 {'native_gate_camera_context','view','native_gate_view_points','camera context from the view points'},
 {'native_gate_shape_refresh','view','native_gate_view_points','shape refresh from the view points'},
 {'native_gate_mining_camera','view','native_gate_view_points','mining from the view points'},
 {'native_gate_xs_camera','view','native_gate_view_points','XS ships from the view points'},
 {'native_gate_traffic_views','view','native_gate_view_points','traffic views from the view points'},
 {'native_gate_sound_rng','split','native_gate_view_points','sounds on their own RNG word'},
 {'native_gate_viewpoint_policy','sector','native_gate_mass_traffic_camera','view point stand-ins per sector'},
 {'native_gate_viewpoint_standins',4,'native_gate_viewpoint_policy','four covered sectors'},
 {'native_gate_nav_octree','agreed','native_gate_world_identities @wire3','navigation octrees shown when every client built them'},
 {'native_gate_nav_octree_snapshot',true,'native_gate_nav_octree','octrees built from the requesting join'},
 {'native_gate_status_stride',60,'','log thinning: every tick sampled and hashed, rows written every 60 ticks and before any stop'},
 {'native_gate_trace_stride',60,'','trace rows every 60 ticks instead of every tick'},
 {'native_gate_crash_note',true,'','fail loudly: an unhandled exception leaves a CRASH line (x4lan-crash.txt)'},
 {'native_gate_lookahead',1,'@wire3','protocol 4: a permit window of 1 tick (render frames, own-ship prediction and wire crypto need a fixed window)'},
 {'native_gate_lookahead_max',4,'native_gate_lookahead=adaptive','adaptive input delay (protocol 5): on only when the session sets native_gate_lookahead=adaptive (relay --adaptive)'},
 {'native_gate_wire_crypto','@credential','@wire3 @fixed','encrypted and signed relay path: on when the session has a credential (x4c1:...) and the relay --wire-crypto'},
 {'native_gate_peer_window','@configured','@wire3 @fixed !native_gate_wire_crypto','direct player links: on only when configured on every PC and the relay (--peer-window m); players see each other addresses'},
 {'native_gate_peer_rendezvous_port','@configured','native_gate_peer_window','NAT traversal for the peer links: on only when configured (relay --rendezvous-port)'},
 {'native_gate_player_identity',true,'@plan native_gate_world_identities native_gate_hash=battle native_gate_snapshot_sha','slot factions, ship ownership and starts from the identity plan'},
 {'native_gate_pilot_slots','@plan_ships','native_gate_player_identity','one slot ship per player, from the identity plan'},
 {'native_gate_pilot_inputs',true,'native_gate_pilot_slots @wire3 native_gate_serial_pathing native_gate_world_identities','lockstep piloting'},
 {'native_gate_pilot_source','wire','native_gate_pilot_inputs','each client captures its own slot and proposes it on the wire'},
 {'native_gate_pilot_delay',3,'native_gate_pilot_inputs @fixed','pilot records apply 3 ticks after capture (lookahead + 2)'},
 {'native_gate_pilot_window','@plan_window','native_gate_pilot_inputs','controls from the tick after the identity setup to tick 72000 (40 minutes)'},
 {'native_gate_pilot_suppress_local',true,'native_gate_pilot_inputs','local controls parked: input only through the records'},
 {'native_gate_pilot_stock',true,'native_gate_pilot_inputs native_gate_pilot_suppress_local native_gate_pilot_source=wire','every slot flies through X4s own player control path'},
 {'native_gate_pilot_stock_slots','all','native_gate_pilot_stock','every slot is a stock slot'},
 {'native_gate_pilot_capture_source','@plan_stock','native_gate_pilot_stock','every client captures its own slot (stock)'},
 {'native_gate_pilot_device','@plan_nodevice','native_gate_pilot_stock','stock capture: no device sampler'},
 {'native_gate_pilot_present',true,'native_gate_pilot_stock native_gate_pilot_stock_slots native_gate_render_rng_split','client k presents slot k (camera, HUD, UI)'},
 {'native_gate_pilot_ship_state',true,'native_gate_pilot_inputs','slot ships state in the receipt'},
 {'native_gate_pilot_block_orders',true,'native_gate_pilot_inputs','stock menus that would act on one client are blocked; the UI replay reopens what it shares'},
 {'native_gate_pilot_fire','record','native_gate_pilot_inputs native_gate_actor_rng_all native_gate_world_identities native_gate_bullet_cadence=every','slot ships fire from the record trigger (record)'},
 {'native_gate_hit_claims','victim','native_gate_pilot_inputs native_gate_world_identities native_gate_pilot_source=wire native_gate_bullet_cadence=every','player-versus-player hits decided by the victim slot'},
 {'native_gate_pilot_death',true,'native_gate_pilot_stock','a slot ship death does not end the session'},
 {'native_gate_pilot_respawn',300,'native_gate_pilot_death','a new slot ship after 300 ticks'},
 {'native_gate_pilot_death_suit',true,'native_gate_pilot_death native_gate_pilot_present native_gate_pilot_ship_state','a dead slot 2..N waits in its own spacesuit'},
 {'native_gate_ui_replay','@families','native_gate_pilot_stock_slots=all native_gate_pilot_present native_gate_runtime_ids native_gate_world_identities','stock UI actions as events of the slot: every family'},
 {'native_gate_ui_guard','all','native_gate_ui_replay','every other world-changing stock UI entry refused, with an info line'},
 {'native_gate_pilot_foot',true,'native_gate_pilot_stock native_gate_ui_replay native_gate_ui_guard','slot 1 gets up and walks on every client'},
 {'native_gate_nav_mesh','sync','native_gate_pilot_foot native_gate_world_identities','interior nav helpers built at an agreed join (stop 186): with on foot, the wharf patrol split without it (footlook5 in sync with it)'},
 {'native_gate_pilot_foot_bodies',true,'native_gate_pilot_foot native_gate_pilot_stock_slots','slots 2..N in their seats rooms'},
 {'native_gate_pilot_foot_slots',true,'native_gate_pilot_foot native_gate_pilot_stock_slots','slots 2..N get up and walk'},
 {'native_gate_player_settings_per_slot',true,'native_gate_ui_replay~faction','each slot its own faction settings'},
 {'native_gate_guidance_local',true,'native_gate_pilot_present native_gate_runtime_ids_local native_gate_script_rng native_gate_md_timed_rng','a client own guidance takes local runtime IDs'},
 {'native_gate_render_fps',60,'native_gate_lookahead native_gate_tick_hz native_gate_snapshot_sha native_gate_serial_pathing native_gate_radar_serial native_gate_render_rng_split native_gate_mass_traffic=sync native_gate_region_pass=sync native_gate_runtime_ids_local','render-only frames between the steps, up to 60 per second'},
 {'native_gate_render_camera','extrapolate','native_gate_render_fps','the camera moves in the render-only frames'},
 {'native_gate_render_motion','extrapolate','native_gate_render_fps','moving objects drawn smoothed in the render-only frames'},
 {'native_gate_own_predict','present','native_gate_pilot_stock native_gate_render_rng_split','the own ship answers the controls at once (display only)'},
 {'native_gate_own_predict_rows',0,'native_gate_own_predict','no per-step OWN_PREDICT row (diagnostic); OWN_PREDICT_STATUS every 600 ticks stays'},
}
-- Why a '!' conflict won (added to the skip reason).
D.CONFLICTS={
 native_gate_wire_crypto='the relay refuses --peer-window with --wire-crypto (the sealed peer hello is not joined yet)',
}
-- The UI replay families (lockstep_pilot_inputs.h UIREP_FAMILY_*, docs/LOCKSTEP-REGISTRY.txt), in bit order, with what
-- each needs beyond the replay itself: a key that must be on, or a guard group.
D.FAMILIES={'travel','target','orders','autopilot','dock','weapons','missiles','queue','events','flight','trade','missions','build',
 'assets','conversation','faction'}
D.FAMILY_NEEDS={missiles='native_gate_pilot_fire',missions='guard:missions',conversation='guard:conversation',build='guard',assets='guard',faction='guard'}
-- Diagnostics and measurement: off by default (they only log, capture, trace, profile, watch or measure). {'key','why'}.
D.DIAGNOSTIC={
 {'native_gate_frame_timing','FRAME_TIMING per frame (measurement)'},
 {'native_gate_probe_profile','PROBE_PROFILE timing (measurement)'},
 {'native_gate_sample_profile','MoveWorker stack sampler (measurement)'},
 {'native_gate_world_hash_watch','WORLD rows of watched objects'},
 {'native_gate_world_health_ticks','WORLD_HEALTH rows'},
 {'native_gate_world_hash_join','hot join census: one more world capture at the join T+1'},
 {'native_gate_world_trace_ticks','world trace'},
 {'native_gate_world_trace_early','world trace manifest'},
 {'native_gate_world_queue_trace','world queue trace'},
 {'native_gate_world_wait_manifest','world wait trace'},
 {'native_gate_world_wait_rng_manifest','world wait RNG experiment'},
 {'native_gate_world_action_manifest','world action trace'},
 {'native_gate_world_values','world typed output trace'},
 {'native_gate_world_position','world position trace'},
 {'native_gate_world_safe_position','world safe position trace'},
 {'native_gate_world_find_rng_manifest','world FindComponent RNG trace'},
 {'native_gate_world_position_rng_manifest','world position RNG trace'},
 {'native_gate_bullet_diag','BULLET rows (the bullet keys are bullet_rng)'},
 {'native_gate_death_diag','DEATH rows'},
 {'native_gate_mass_diag','MASS rows'},
 {'native_gate_flight_diag','FLIGHT and AVOIDANCE rows'},
 {'native_gate_agent_diag','AGENT rows'},
 {'native_gate_ship_diag','SHIP rows'},
 {'native_gate_params_diag','PARAMS rows'},
 {'native_gate_nav_diag','NAV rows'},
 {'native_gate_velocity_diag','VELOCITY rows'},
 {'native_gate_shape_diag','SHAPE rows'},
 {'native_gate_attention_diag','ATTENTION rows'},
 {'native_gate_damage_diag','damage trace'},
 {'native_gate_contact_diag','CONTACT rows'},
 {'native_gate_contact_order','CONTACT_ORDER rows'},
 {'native_gate_contact_properties','CONTACT_PROPERTIES rows'},
 {'native_gate_body_diag','BODY rows'},
 {'native_gate_boost_diag','boost samples'},
 {'native_gate_travel_diag','travel samples'},
 {'native_gate_address_order','ADDRESS_ORDER rows (read only)'},
 {'native_gate_build_rows','who-gets-built rows (only watches)'},
 {'native_gate_supply_rows','SUPPLY_POST rows'},
 {'native_gate_supply_wants','SUPPLY_WANTS rows'},
 {'native_gate_effect_rows','EFFECT rows'},
 {'native_gate_traffic_rows','traffic rows'},
 {'native_gate_view_rows','view point rows'},
 {'native_gate_viewpoint_policy_rows','view point policy rows'},
 {'native_gate_highway_rows','highway rows'},
 {'native_gate_highway_rows_from','highway rows start'},
 {'native_gate_highway_trace','highway fixture pose trace'},
 {'native_gate_rng_rows','RNG rows'},
 {'native_gate_actor_rng_rows','actor RNG rows'},
 {'native_gate_command_order_rows','command order rows'},
 {'native_gate_created_rows','created identity rows'},
 {'native_gate_created_load_stacks','load-created object stacks'},
 {'native_gate_join_evq_dump','hot join pending event rows'},
 {'native_gate_join_watch','hot join watched object rows'},
 {'native_gate_join_watch_span','hot join watched object rows after T+1 .. T+n'},
 {'native_gate_join_hold_boot_events','hot join boot event passes held'},
 {'native_gate_join_precise','hot join save at full precision'},
 {'native_gate_join_in_window','hot join save inside the pilot window, measured (diagnostic)'},
 {'native_gate_pilot_dump_tick','pilot slot ship, controller, agent and solver bytes at this tick (diagnostic)'},
 {'native_gate_join_passes_admit','hot joiner: the synced passes, stand-ins and travel snapshot of T at its admission (diagnostic)'},
 {'native_gate_join_ids','hot joiner saved objects at the session runtime IDs'},
 {'native_gate_join_buildp','hot join build processor busy rows (diagnostic)'},
 {'native_gate_join_view_skip','hot join view-bank writes a joiner leaves out (diagnostic)'},
 {'native_gate_join_viewl','hot join: a joiner runs the live and player view handlers where its flags differ (VIEWL)'},
 {'native_gate_join_pend','hot join: a joiner takes the session pending view cache flags (PEND)'},
 {'native_gate_join_viewf_bits','hot join VIEWF flag bits a joiner takes from the session (diagnostic; 0 off)'},
 {'native_gate_join_viewf_write','hot join VIEWF writes on a joiner (diagnostic; off: no flip after STOP)'},
 {'native_gate_join_shipyards','hot join shipyard capacity rows at these ticks (diagnostic)'},
 {'native_gate_join_refresh_builds','hot join: every station build state refreshed at T+1 on every client'},
 {'native_gate_join_shipyards_mask','hot join shipyard rows: which reads run (diagnostic)'},
 {'native_gate_event_rows','event rows'},
 {'native_gate_event_rows_from','event rows from this tick (the window of a hot joiner)'},
 {'native_gate_event_rng_rows','event RNG rows'},
 {'native_gate_script_rng_rows','script RNG rows'},
 {'native_gate_script_rng_rows_ticks','script RNG rows window'},
 {'native_gate_script_rng_rows_window','script RNG rows window'},
 {'native_gate_script_rng_natural','natural script RNG rows'},
 {'native_gate_script_rng_trace','script RNG trace rows (nested scopes, PCs, range bounds)'},
 {'native_gate_runtime_ids_rows','runtime ID rows'},
 {'native_gate_runtime_ids_callers','runtime ID caller rows'},
 {'native_gate_runtime_ids_caller_stride','RUNTIME_IDS_CALLER rows every N ticks'},
 {'native_gate_loadout_order_rows','LOADOUT rows'},
 {'native_gate_order_canon_rows','ORDER_CANON rows'},
 {'native_gate_pick_order_rows','PICK_ORDER rows'},
 {'native_gate_ui_lazy_rows','UI lazy rows'},
 {'native_gate_transition_rows','transition rows'},
 {'native_gate_render_display_rows','render display rows'},
 {'native_gate_gravidar_watch','Gravidar contact watch'},
 {'native_gate_gravidar_digest','Gravidar digest rows (read only)'},
 {'native_gate_gravidar_digest_rows','Gravidar digest rows'},
 {'native_gate_gravidar_posts','Gravidar post rows (read only)'},
 {'native_gate_motion_watch','motion watch'},
 {'native_gate_motion_watch_digest','motion watch digest'},
 {'native_gate_motion_watch_doors','motion watch doors'},
 {'native_gate_motion_watch_orders','motion watch orders'},
 {'native_gate_motion_watch_splines','motion watch splines'},
 {'native_gate_motion_watch_zones','motion watch zones'},
 {'native_gate_wreck_watch','wreck watch'},
 {'native_gate_draw_watch','draw watch'},
 {'native_gate_draw_watch_frame','draw watch frame'},
 {'native_gate_nav_cadence_watch','navigation cadence watch rows'},
 {'native_gate_nav_octree_sites','navigation octree invalidation sites (read only)'},
 {'native_gate_physics_trace','physics trace'},
 {'native_gate_physics_trace_children','physics trace children'},
 {'native_gate_physics_trace_children_from','physics trace children start'},
 {'native_gate_physics_trace_from','physics trace start'},
 {'native_gate_physics_trace_saved','physics trace saved objects'},
 {'native_gate_step_trace','step trace'},
 {'native_gate_step_trace_ticks','step trace ticks'},
 {'native_gate_step_chain','step chain trace'},
 {'native_gate_trace_window','trace window'},
 {'native_gate_coverage','code coverage'},
 {'native_gate_thread_rng_census','thread RNG census'},
 {'native_gate_camera_probe','camera probe rows'},
 {'native_gate_view_points_mask','masks view points for measurement'},
 {'native_gate_fp_attack_capture','FP attack capture'},
 {'native_gate_fp_attack_watch','FP attack watch'},
 {'native_gate_fp_redirect_watch','FP redirect watch'},
 {'native_gate_fp_redirect_ticks','FP redirect rows'},
 {'native_gate_registry_md_trace','registry MD trace'},
 {'native_gate_creation_trace_schema','creation trace'},
 {'native_gate_job_rng','job RNG rows (observe)'},
 {'native_gate_pilot_capture','PILOT_STOCK_CAPTURE row per captured record (the record is captured without it)'},
 {'native_gate_pilot_observe_native','native channel observation rows'},
 {'native_gate_player_observer','player controller observer'},
 {'native_gate_player_request_observer','player request observer'},
 {'native_gate_player_solver_observer','player solver observer'},
 {'native_gate_player_transition_observer','player transition observer'},
 {'native_gate_player_transition_queue_evidence','player transition queue evidence'},
 {'native_gate_player_transition_prebind_receipt','player transition prebind receipt'},
 {'native_gate_npc_diag','NPC rows'},
 {'native_gate_npc_lifecycle','NPC action rows'},
 {'native_gate_npc_pilot','NPC pilot watch'},
 {'native_gate_npc_trigger','NPC trigger rows'},
 {'native_gate_npc_finish','NPC finish rows'},
 {'native_gate_npc_provenance','NPC provenance rows'},
 {'native_gate_hybrid','hybrid sectors: not yet correct (no handover; slice H1 under test)'},
}
-- Neither a feature default nor a diagnostic: fixtures, experiments, alternatives and cross-platform keys. {'key','why'}.
D.EXCLUDED={
 {'native_gate_missions','census scenario fixture (MD events at fixed ticks)'},
 {'native_gate_highway','census scenario fixture'},
 {'native_gate_highway_super','census scenario fixture'},
 {'native_gate_highway_target','census scenario fixture'},
 {'native_gate_player_tour','census fixture: warps the player every 60 ticks'},
 {'native_gate_big_war','stress fixture'},
 {'native_gate_big_spawn','stress fixture'},
 {'native_gate_bigbattle','stress fixture'},
 {'native_gate_battle_engage','false keeps the fixture battle calm (sandbox runs)'},
 {'native_gate_command_test','two-player fixture (protocol 2)'},
 {'native_gate_management_test','two-player fixture (protocol 2)'},
 {'native_gate_construction_test','two-player fixture (protocol 2)'},
 {'native_gate_player_probe','two-player fixture (protocol 2)'},
 {'native_gate_docking_state','two-player fixture (protocol 2)'},
 {'native_gate_docking_shape_rng','two-player fixture (protocol 2)'},
 {'native_gate_docking_shape_refresh','two-player fixture (protocol 2)'},
 {'native_gate_creation_rng_schema','two-player fixture (protocol 2)'},
 {'native_gate_registry_creation_schema','two-player fixture (protocol 2)'},
 {'native_gate_pilot_stock_script','scripted controls for runs without people'},
 {'native_gate_pilot_local_script','scripted local keys for runs without people'},
 {'native_gate_ui_replay_script','scripted UI plan for runs without people'},
 {'native_gate_ui_build_script','scripted build plan for runs without people'},
 {'native_gate_ui_replay_fleet','test fleet handed to the player at the setup tick'},
 {'native_gate_ui_replay_min_hull','test protection of the slot ships'},
 {'native_gate_ui_trade_fund','test credits for the slot factions'},
 {'native_gate_ui_build_fund','test credits for the build families'},
 {'native_gate_player_settings_spawn','player settings spawn check (test)'},
 {'native_gate_hit_claims_script','Lua test carrier of hit claims'},
 {'native_gate_pilot_fire_slots','scripted fire schedule (test)'},
 {'native_gate_camera_perturb','parity-leak harness'},
 {'native_gate_camera_offset','two-player camera experiment'},
 {'native_gate_camera_anchor','chase camera: X4s own camera presents the slot instead (pilot_present)'},
 {'native_gate_pilot_camera_follow','watch-own-ship camera of wire runs: pilot_present replaces it'},
 {'native_gate_pilot_predict','record prediction of wire/script runs: stock capture excludes it, own_predict replaces it'},
 {'native_gate_virtual_players','virtual players spikes (observe/construct)'},
 {'native_gate_player_orders','scripted player orders (wire runs; the UI replay orders family carries orders)'},
 {'native_gate_fork_save','non-blocking save (Linux gate only)'},
 {'native_gate_join','hot join (lockstep_join.h): not live-tested yet (J1-J3, reports/LOCKSTEP-HOTJOIN-20261101.txt); set by the join tooling'},
 {'native_gate_play_mode','perf mode without observer rows and world pages: the census rows the live comparison reads go too'},
 {'native_gate_ui_cargo','fix not in the people stack (Play42P); not validated with it'},
 {'native_gate_ui_lazy','tick-187 census key (observe), not in the people stack'},
 {'native_gate_body_order','physics body order: did not remove its class, not in the people stack'},
 {'native_gate_command_order','command order census key, not in the people stack'},
 {'native_gate_fp_redirect','Option B (mixed Windows/Linux): moves Windows to Linux order; the people stack does not run on the Linux gate yet'},
 {'native_gate_fp_redirect_solver','Option B (mixed Windows/Linux), needs native_gate_fp_redirect'},
 {'native_gate_fp_redirect_drones','Option B (mixed Windows/Linux), needs native_gate_fp_redirect'},
 {'native_gate_npc_rng','NPC RNG experiment (excluded by the keyed script RNG)'},
 {'native_gate_npc_deadline','NPC deadline experiment'},
 {'native_gate_ai_rng','excluded by the keyed script RNG'},
 {'native_gate_interrupt_rng','excluded by the keyed script RNG'},
 {'native_gate_boot_rng_anchor','boot RNG anchor experiment'},
 {'native_gate_travel_determinism','alternative to travel_snapshot (mutually exclusive)'},
 {'native_gate_nav_key','Linux gate only'},
 {'native_gate_warp_exit','Linux gate only: the gate exit sum in the Windows order (Windows is that order); a Linux default after a 0 W vs L census'},
 {'native_gate_timer_resolution','timer experiment'},
 {'native_gate_tick_pace','unset = paced (false unpaces)'},
 {'native_gate_world_timing_fields','no reader in the probe (obsolete key)'},
 {'native_gate_creation_rng_manifest','two-player construction fixture'},
 {'native_gate_diagnostic_contract','get-up diagnostic fixture contract'},
 {'native_gate_player_origin_token','get-up fixture'},
 {'native_gate_player_transition_last_tick','get-up diagnostic fixture'},
 {'native_gate_player_transition_manifest','get-up diagnostic fixture'},
 {'native_gate_registry_origin_schema','registry origin trace (diagnostic fixture)'},
 {'native_gate_registry_origin_identity_schema','registry origin trace (diagnostic fixture)'},
 {'native_gate_npc_pilot_origin','NPC pilot rows (diagnostic)'},
 {'native_gate_npc_provenance_origin','NPC provenance rows (diagnostic)'},
 {'native_gate_shape_lifetimes','shape lifetime rows (diagnostic)'},
 {'native_gate_player_orders_capture','orders UI capture: excluded by the order block of piloting (one owner of the order menus)'},
}
-- Settings of a feature with the probe's own value when absent (no session default needed). {'key','why'}.
D.TUNING={
 {'native_gate_own_predict_horizon','own-ship prediction cap in ticks (probe: 10)'},
 {'native_gate_own_predict_tau','own-ship correction time constant (probe: 150 ms)'},
 {'native_gate_own_predict_others','own-ship prediction of the other stock slots (probe: on)'},
 {'native_gate_pilot_pad','device sampler pad (wire runs)'},
 {'native_gate_pilot_invert','device sampler axes (wire runs)'},
 {'native_gate_pilot_native_requests','native capture channels (capture source native)'},
 {'native_gate_pilot_predict_horizon','record prediction horizon (pilot_predict)'},
 {'native_gate_pilot_predict_tau','record prediction smoothing (pilot_predict)'},
 {'native_gate_pilot_predict_raw','record prediction raw rows (pilot_predict)'},
 {'native_gate_nav_octree_delay','nav octree deadline mode delay (probe: 30)'},
 {'native_gate_player_orders_delay','player orders delay (probe: 3)'},
 {'native_gate_boot_rng_anchor_mask','boot RNG anchor experiment mask'},
 {'native_gate_virtual_players_construct_tick','virtual players construct tick (probe: 20)'},
 {'native_gate_start_time','fixed-tick boot start (not the snapshot boot)'},
}
-- The keys a session config supplies itself (no default).
D.SESSION={'native_gate_test','native_gate_clients','native_gate_player_identity_plan','native_gate_relay_host','native_gate_arm_timeout_ms',
 'native_gate_input_priority','native_gate_defaults'}
-- Stop reasons the probe names in its info line (docs/LOCKSTEP-REGISTRY.txt, section 4; tests check them).
D.STOP_REASONS={[1]='the relay stopped the session',[2]='not the main thread or loading at start',[3]='load or state mismatch',
 [4]='dt or report tick error',[5]='clock drift',[6]='window closed',[7]='relay timeout',[8]='socket error',[10]='start refused',
 [11]='image verify',[26]='boot timeout',[56]='local proposal differs',[100]='mass traffic',[101]='pilot prediction',[102]='lookahead ahead',
 [103]='lookahead conflict',[104]='peer mismatch',[105]='Linux gate: unported option',[106]='wire crypto: invalid datagram',
 [107]='wire crypto: handshake',[108]='peer equivocation',[110]='spawn RNG',[111]='hit claims',[112]='pilot fire',[113]='pilot native',
 [120]='speech time',[121]='Gravidar deadlines',[130]='runtime IDs',[131]='command order',[132]='weapon fire',[133]='physics trace',
 [134]='body order',[135]='shape order',[136]='engine RNG',[137]='nav octree',[138]='camera anchor',[139]='speak timer',[140]='ship sound',
 [141]='draw watch',[142]='player timer',[143]='warp RNG',[144]='tempzone order',[145]='transition rows',[146]='warp exit',[150]='Gravidar wide',
 [151]='event RNG',[152]='build RNG',[153]='UI cargo',[154]='supply rows',[155]='pilot stock',[156]='portable math',[157]='exact reciprocal',
 [158]='fp environment',[159]='decimal reader',[160]='virtual players',[161]='pilot slots',[162]='pilot present',[163]='pilot death',
 [164]='UI replay',[165]='region camera',[166]='region camera (mining)',[167]='region camera (xs)',[168]='region camera (probe)',
 [169]='region camera (highway rows)',[170]='UI lazy',[171]='supply wants',[172]='loadout order',[173]='order canon',[174]='pick order',
 [175]='macro arena',[176]='path budget',[177]='economy load',[178]='cutscene ready',[179]='fp redirect',[180]='path transforms',
 [181]='body pose',[182]='render frames',[183]='render display',[184]='own-ship prediction',[185]='on foot',[186]='nav mesh',
 [187]='hybrid sectors',[188]='character play',[190]='adaptive plan',[191]='adaptive source',[192]='adaptive quiet',[193]='adaptive dropped',[194]='adaptive pilot'}

local function on(v) return v~=nil and v~=false end
local function copy(v)
 if type(v)~='table' then return v end
 local out={}
 for k,x in pairs(v) do out[k]=copy(x) end
 return out
end
-- The plan's slot ships, or nil and why.
local function plan_ships(cfg)
 local plan=cfg.native_gate_player_identity_plan
 if type(plan)~='table' or type(plan.slots)~='table' or #plan.slots<1 then return nil,'needs native_gate_player_identity_plan with slots' end
 local ships={}
 for k,slot in ipairs(plan.slots) do
  local ship=type(slot)=='table' and slot.ship
  if type(ship)~='string' or not ship:match('^[0-9a-f]+$') or #ship~=16 then return nil,'plan slot '..k..' has no saved ship (created ships cannot be piloted yet)' end
  ships[k]=ship
 end
 return ships
end
D.DERIVE={}
D.DERIVE['@plan_ships']=function(cfg) return plan_ships(cfg) end
D.DERIVE['@plan_stock']=function(cfg)
 local ships,why=plan_ships(cfg);if not ships then return nil,why end
 local out={};for k=1,#ships do out[k]='stock' end;return out
end
D.DERIVE['@plan_nodevice']=function(cfg)
 local ships,why=plan_ships(cfg);if not ships then return nil,why end
 local out={};for k=1,#ships do out[k]=false end;return out
end
-- The window opens after the identity is folded in (setup tick + K.FOLD_LAG 10 + 5, players.py) and not before 40.
D.DERIVE['@plan_window']=function(cfg)
 local plan=cfg.native_gate_player_identity_plan
 local tick=type(plan)=='table' and type(plan.tick)=='number' and plan.tick or 20
 return {math.max(40,tick+16),72000}
end
-- Every family whose prerequisites hold; a dropped family is reported (R.skipped) by the caller through `drop`.
D.DERIVE['@families']=function(cfg,R,drop)
 local guard=cfg.native_gate_ui_guard
 local guard_off=R.off.native_gate_ui_guard
 local groups={}
 if type(guard)=='table' then for _,g in ipairs(guard) do groups[g]=true end end
 local out={}
 for _,name in ipairs(D.FAMILIES) do
  local need,why=D.FAMILY_NEEDS[name]
  if need=='native_gate_pilot_fire' and not on(cfg.native_gate_pilot_fire) then why='needs native_gate_pilot_fire (off)'
  elseif need and need:sub(1,5)=='guard' then
   if guard_off then why='needs native_gate_ui_guard (off)'
   elseif type(guard)=='table' and need~='guard' and not groups[need:sub(7)] then why='needs native_gate_ui_guard with the '..need:sub(7)..' group'
   end
  end
  if why then drop('native_gate_ui_replay:'..name,why) else out[#out+1]=name end
 end
 return out
end

-- A session: native_gate_test with a slot pN, a port and a token.
function D.session(cfg)
 return type(cfg)=='table' and on(cfg.native_gate_test) and type(cfg.slot)=='string' and cfg.slot:match('^p%d+$')~=nil and
  cfg.port~=nil and cfg.port~='' and type(cfg.token)=='string' and cfg.token~=''
end
local CONDITIONS={
 ['@wire3']={function(cfg) return type(cfg.native_gate_clients)=='number' end,'needs the protocol 3 input wire (native_gate_clients)'},
 ['@plan']={function(cfg) return type(cfg.native_gate_player_identity_plan)=='table' end,'needs native_gate_player_identity_plan'},
 ['@fixed']={function(cfg) return cfg.native_gate_lookahead~='adaptive' end,'protocol 5 (native_gate_lookahead=adaptive) excludes it'},
}
-- nil when every token of needs holds, else the first reason.
function D.unmet(cfg,needs)
 for token in needs:gmatch('%S+') do
  local c=CONDITIONS[token]
  if c then
   if not c[1](cfg) then return c[2] end
  elseif token:sub(1,1)=='!' then
   local key=token:sub(2)
   if on(cfg[key]) then return 'excluded by '..key..' (on)'..(D.CONFLICTS[key] and ': '..D.CONFLICTS[key] or '') end
  else
   local key,op,want=token:match('^([%w_]+)([=~]?)(.*)$')
   local v=cfg[key]
   if not on(v) then return 'needs '..key..' (off)' end
   if op=='=' and tostring(v)~=want then return 'needs '..key..'='..want..' (is '..tostring(v)..')' end
   if op=='~' then
    local found=false
    if type(v)=='table' then for _,x in ipairs(v) do found=found or x==want end end
    if not found then return 'needs '..key..' with '..want end
   end
  end
 end
 return nil
end
-- Canonical text of a value (the stack digest): tables in index then key order.
local function canon(v)
 if type(v)~='table' then return type(v)=='string' and string.format('%q',v) or tostring(v) end
 local parts,keys={},{}
 for i=1,#v do parts[#parts+1]=canon(v[i]) end
 for k in pairs(v) do if type(k)~='number' or k<1 or k>#v or k%1~=0 then keys[#keys+1]=tostring(k) end end
 table.sort(keys)
 for _,k in ipairs(keys) do parts[#parts+1]=k..'='..canon(v[k]) end
 return '{'..table.concat(parts,',')..'}'
end
D.canon=canon
-- Applies the defaults to cfg once. Returns the report (nil without native_gate_test):
--   R.bare (native_gate_defaults=false), R.applied/R.explicit/R.offlist/R.skipped ({key,why}), R.errors (refusals), R.stack.
function D.apply(cfg)
 if type(cfg)~='table' or not on(cfg.native_gate_test) then return nil end
 if type(cfg._native_gate_defaults)=='table' then return cfg._native_gate_defaults end
 local R={version=D.VERSION,applied={},explicit={},offlist={},off={},skipped={},errors={}}
 cfg._native_gate_defaults=R
 if cfg.native_gate_defaults==false then R.bare=true;return R end
 if cfg.native_gate_defaults~=nil and cfg.native_gate_defaults~=true and cfg.native_gate_defaults~=D.VERSION then
  R.errors[#R.errors+1]='native_gate_defaults must be false (the bare gate), true or '..D.VERSION
  return R
 end
 if not D.session(cfg) then
  R.errors[#R.errors+1]='native_gate_test without a lockstep session: slot pN, port and token are required'
  return R
 end
 -- Explicit off: false or 'off' leaves the key absent (absent = off for every gate key).
 for _,e in ipairs(D.ON) do
  local v=cfg[e[1]]
  if v==false or v=='off' then cfg[e[1]]=nil;R.off[e[1]]=true;R.offlist[#R.offlist+1]=e[1] end
 end
 local function drop(key,why,feature) R.skipped[#R.skipped+1]={key,why,feature} end
 for _,e in ipairs(D.ON) do
  local key,value,needs=e[1],e[2],e[3]
  if R.off[key] then
   -- the session turned it off
  elseif cfg[key]~=nil then
   R.explicit[#R.explicit+1]=key
  else
   local why=D.unmet(cfg,needs)
   if not why and type(value)=='string' and value:sub(1,1)=='@' then
    if value=='@credential' then why='no credential in the session config (native_gate_wire_crypto = x4c1:...; relay --wire-crypto): the relay path is plain UDP with the shared token'
    elseif value=='@configured' then why='not configured (an opt-in of every PC and the relay)'
    else
     local derive=assert(D.DERIVE[value],'unknown derived default '..value)
     value,why=derive(cfg,R,drop)
    end
   end
   if why then drop(key,why,e[4])
   elseif value~=nil then cfg[key]=copy(value);R.applied[#R.applied+1]=key end
  end
 end
 -- Explicit combinations the relay or the gate refuses: stop before the boot.
 if on(cfg.native_gate_wire_crypto) and cfg.native_gate_lookahead=='adaptive' then
  R.errors[#R.errors+1]='native_gate_wire_crypto covers protocols 3 and 4; native_gate_lookahead=adaptive is protocol 5 (the relay refuses both)'
 end
 if on(cfg.native_gate_wire_crypto) and on(cfg.native_gate_peer_window) then
  R.errors[#R.errors+1]='native_gate_peer_window with native_gate_wire_crypto: '..D.CONFLICTS.native_gate_wire_crypto
 end
 -- The stack every client must run alike: its digest joins the boot data (a client with another stack stops at tick 0).
 local parts={'DEFAULTS_STACK_'..D.VERSION}
 for _,e in ipairs(D.ON) do
  local v=cfg[e[1]]
  if e[1]=='native_gate_wire_crypto' then v=on(v) and 'set' or nil end   -- a per-slot secret: only whether it is on
  parts[#parts+1]=e[1]..'='..(v==nil and 'off' or canon(v))
 end
 R.stack=table.concat(parts,';')
 return R
end

-- Loud output. D.log(text): one NATIVE_GATE row; D.notice(text): the in-game info line (X4's ShowInfoLine, the UI guard's
-- path), shown at once and again from the probe's frame once X4 is out of the start menu (D.flush_notices).
D.pending={}
function D.log(text) DebugError('[X4LAN] NATIVE_GATE '..text) end
local function show(text)
 local ok,why=pcall(function()
  local ffi=require('ffi')
  pcall(ffi.cdef,'void ShowInfoLine(const char*,uint32_t);')
  ffi.C.ShowInfoLine(text,4)
 end)
 return ok,why
end
local function in_menu()
 local ok,menu=pcall(function() return require('ffi').C.IsStartmenu() end)
 return not ok or menu==true
end
function D.notice(text)
 text='Multiplayer: '..text
 local ok,why=show(text)
 if not ok then D.log('NOTICE_UNSHOWN why='..tostring(why)) end
 if not ok or in_menu() then D.pending[#D.pending+1]=text end
 return ok
end
-- From the probe's onUpdate: pending notices again once a game is on screen (a start-menu info line is not drawn).
function D.flush_notices(in_game)
 if #D.pending==0 or not in_game then return end
 local pending=D.pending;D.pending={}
 for _,text in ipairs(pending) do show(text) end
end
-- An ERROR row and an info line: the shared form of every refusal, skipped default and degraded path.
function D.loud(kind,text,notice)
 D.log('ERROR '..kind..' '..text)
 if notice~=false then D.notice(notice or (kind:lower():gsub('_',' ')..': '..text)) end
end
-- The report as rows: what is on, what the session set or turned off, every skip (loud), refusals (raised).
function D.announce(R)
 if not R then return end
 if R.bare then
  D.log('DEFAULTS_OFF native_gate_defaults=false: the bare gate, every feature key as configured (absent = off)')
  return
 end
 if #R.errors>0 then
  for _,why in ipairs(R.errors) do D.loud('DEFAULTS_REFUSED',why,false) end
  D.notice('the session did not start - '..R.errors[1])
  error('defaults refused: '..table.concat(R.errors,'; '),0)
 end
 D.log('DEFAULTS version='..R.version..' applied='..#R.applied..' explicit='..#R.explicit..' off='..#R.offlist..' skipped='..#R.skipped)
 local function rows(head,list)
  local line={}
  for _,key in ipairs(list) do
   line[#line+1]=(key:gsub('^native_gate_',''))
   if #table.concat(line,',')>1500 then D.log(head..' keys='..table.concat(line,','));line={} end
  end
  if #line>0 then D.log(head..' keys='..table.concat(line,',')) end
 end
 rows('DEFAULTS_APPLIED',R.applied)
 if #R.explicit>0 then rows('DEFAULTS_EXPLICIT',R.explicit) end
 if #R.offlist>0 then rows('DEFAULTS_EXPLICIT_OFF',R.offlist) end
 local names={}
 for _,s in ipairs(R.skipped) do
  D.loud('DEFAULT_SKIPPED','key='..s[1]..' why='..s[2]..(s[3] and ' feature='..s[3] or ''),false)
  names[#names+1]=(s[1]:gsub('^native_gate_',''))
 end
 if #names>0 then
  D.notice(#names..' default feature'..(#names==1 and ' is' or 's are')..' off - '..table.concat(names,', ',1,math.min(#names,4))..
   (#names>4 and ', ...' or '')..' (see the log: DEFAULT_SKIPPED)')
 end
end

-- Degraded paths in module statuses: a counter of WATCH that grows is an ERROR row (every growth), in the log only. The in-game
-- info line it also showed is gone (user, 3 Nov): the counters seen in every session (build rng fallback, find order unmapped,
-- created identities macro_unknown) are deterministic paths the census confirms equal, and a player cannot act on them.
D.WATCH={failures=true,unmapped=true,fallback=true,unknown=true,unsupported=true,refused=true,swap_refused=true,lost=true,torn=true}
D.WATCH_SUFFIX={'_failures','_unmapped','_refused','_unknown'}
-- 'lua=a/b/c/d' composites of the UI families (native_gate_ui_replay_core.lua): the names of their parts.
D.WATCH_LUA={UI_TRADE_STATUS={'captured','applied','skipped','refused'},UI_FACTION_STATUS={'applied','skipped','parts','dropped','refused'}}
D.WATCH_LUA_ON={skipped=true,dropped=true,refused=true}
local function watched(name)
 if D.WATCH[name] then return true end
 for _,suffix in ipairs(D.WATCH_SUFFIX) do if name:sub(-#suffix)==suffix then return true end end
 return false
end
function D.watcher()
 local W={last={}}
 -- status: the row's name (e.g. 'RUNTIME_IDS'), tick, text (key=value pairs); returns the loud events.
 function W.line(status,tick,text)
  local events={}
  local function counter(name,value)
   local id=status..' '..name
   local before=W.last[id]
   W.last[id]=value
   if value>(before or 0) then
    local row='status='..status..' '..name..'='..value..' (+'..(value-(before or 0))..') tick='..tostring(tick)
    D.loud('DEGRADED',row,false)
    events[#events+1]=row
   end
  end
  for name,value in tostring(text):gmatch('([%a_][%w_]*)=(%d+)') do
   if watched(name) then counter(name,tonumber(value)) end
  end
  local parts=D.WATCH_LUA[status]
  local lua=parts and tostring(text):match(' lua=([%d/]+)')
  if lua then
   local i=0
   for value in lua:gmatch('%d+') do
    i=i+1
    if parts[i] and D.WATCH_LUA_ON[parts[i]] then counter('lua_'..parts[i],tonumber(value)) end
   end
  end
  return events
 end
 return W
end
-- The info line of a stop: the reason by name (D.STOP_REASONS) and the module's why when it has one.
function D.stop_notice(reason,why)
 reason=tonumber(reason) or 0
 local name=D.STOP_REASONS[reason] or 'reason '..reason
 if reason<=1 then
  D.log('STOP_NOTICE reason=1 the relay ended the session (its tick limit or a mismatch: see the relay report)')
  D.notice('the session ended (the relay stopped it)')
 else
  D.loud('SESSION_STOPPED','reason='..reason..' ('..name..')'..(why and ' why='..why or ''),'the session stopped - '..name..(why and ' ('..why..')' or '')..' - see the log')
 end
end

X4LAN_NATIVE_GATE_DEFAULTS=D
-- Applied while loading (before the adapters that read X4LAN_CONFIG); the probe announces the report.
if X4LAN_CONFIG and on(X4LAN_CONFIG.native_gate_test) then D.apply(X4LAN_CONFIG) end

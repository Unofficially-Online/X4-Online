#ifndef X4_GATE_PLAYER_PHYSICS_REPLAY_H
#define X4_GATE_PLAYER_PHYSICS_REPLAY_H

/* Default-unused scripted continuation of the disposable physics experiment.
 * The caller authenticates an entered worker/main-parked generation and keeps
 * all controller, ship, zone and private-table storage alive until _exit.
 * Each call restores the P1 bank seen at this scope's own entry. This does not
 * exclude other native readers or make shared-state exchange safe for play. */
static uint64_t player_physics_replay_last = FWRR_FIRST_GENERATION;
static unsigned player_physics_replay_busy;

static unsigned player_physics_replay_input(uint64_t generation) {
    if (generation >= 5 && generation <= 14) return 2;
    if (generation >= 25 && generation <= 34) return 3;
    return 0;
}

static float player_physics_replay_read_input(unsigned char keys[4]) {
    unsigned char flight, enabled, ignored;
    uint64_t autopilot;
    float digital;
    lifecycle_discard_require(player_recurring_physics_entered() && player_physics_identity_for(1) &&
        gate_tls_worker(gate_tls_self()) && !atomic_load(&stock_observer_input_depth) &&
        !atomic_load(&sko_session.wrapper_depth), "physics_replay_input_identity");
    lifecycle_discard_read((uintptr_t)LX(0x3d8daa8), &flight, 1);
    lifecycle_discard_read((uintptr_t)LX(0x3d8da60), &enabled, 1);
    lifecycle_discard_read((uintptr_t)LX(0x3e5b29a), &ignored, 1);
    lifecycle_discard_read((uintptr_t)LX(0x3d8da58), &autopilot, sizeof autopilot);
    lifecycle_discard_read((uintptr_t)LX(0x3d8da68), keys, 4);
    lifecycle_discard_read((uintptr_t)LX(0x3d8d9ac), &digital, sizeof digital);
    lifecycle_discard_require(flight == 1 && enabled == 1 && !ignored && !autopilot && isfinite(digital),
                              "physics_replay_input_suppressed");
    return digital;
}

static void player_physics_replay_log_input(uint64_t generation, unsigned id, unsigned state,
                                            const unsigned char keys[4], float digital) {
    char message[256];
    snprintf(message, sizeof message,
        "X4LAN LIFECYCLE_DISCARD physics=input tick=%llu id=%u state=%u keys=%u%u%u%u digital=%.9g\n",
        (unsigned long long)generation, id, state, keys[0], keys[1], keys[2], keys[3], (double)digital);
    lifecycle_discard_write(message);
}

/* Existing scripted diagnostic; network mode supplies its own ordered record. */
static void player_physics_scripted_input(void *context) {
    uint64_t generation = *(const uint64_t *)context;
    unsigned id = player_physics_replay_input(generation);
    unsigned char keys[4], expected[4] = {0, 0, 0, 0};
    float digital = player_physics_replay_read_input(keys);
    lifecycle_discard_require(!memcmp(keys, expected, sizeof keys), "physics_replay_keys_not_neutral");
    if (id) {
        ((void (*)(int, unsigned char))LX(0x166c6f0))((int)id, 1);
        expected[id - 2] = 1;
        digital = player_physics_replay_read_input(keys);
        lifecycle_discard_require(!memcmp(keys, expected, sizeof keys), "physics_replay_press_readback");
    }
    player_physics_replay_log_input(generation, id, id != 0, keys, digital);
    /* 0=neutral, 1=STATE2 and 2=STATE3. The shared logger must distinguish
     * both nonzero values before this continuation is wired. */
    lifecycle_discard_update_once(id ? (int)id - 1 : 0);
    digital = player_physics_replay_read_input(keys);
    lifecycle_discard_require(!memcmp(keys, expected, sizeof keys), "physics_replay_update_keys");
    if (id) {
        ((void (*)(int, unsigned char))LX(0x166c6f0))((int)id, 0);
        expected[id - 2] = 0;
        digital = player_physics_replay_read_input(keys);
        lifecycle_discard_require(!memcmp(keys, expected, sizeof keys), "physics_replay_release_readback");
    }
    player_physics_replay_log_input(generation, id, 0, keys, digital);
}

/* Called once for generation >5 only after the recurring adapter has entered
 * that exact generation. The previous generation must have completed its one
 * admitted slot-40 call. Any failed step discards the process. */
static void player_physics_replay_begin_with_input(uint64_t generation,
                                                   void (*update_input)(void *), void *context) {
    lifecycle_discard_require(player_recurring_physics_entered() && generation >= player_startup()->apply &&
        generation == stats.issued && player_recurring_tail_token.generation == generation &&
        generation == player_physics_replay_last + 1 && update_input &&
        atomic_load_explicit(&player_physics_ready, memory_order_acquire) == 1 &&
        player_physics_identity() && !player_physics_replay_busy && !player_physics_upkeep_busy &&
        player_physics_upkeep_calls == 1 && lifecycle_input_pins(), "physics_replay_admission");
    player_physics_replay_busy = 1;
    unsigned char canonical_bank[PCB_VALUE_BYTES];
    input_context_census_sample before = {0}, after = {0};
    const input_context_census_operations operations = {NULL, player_seat_preflight_read};
    player_context_adapter_outside(generation, PLAYER_CONTEXT_FRAME_BEGIN);
    player_context_adapter_scope_begin(generation);
    lifecycle_routes_read_bank(canonical_bank);
    lifecycle_discard_snapshot("physics_update_before", 0);
    player_physics_require_bank(canonical_bank);
    (void)lifecycle_discard_context_collect(&operations, &before);
    lifecycle_discard_require(before.tree_valid, "physics_replay_contexts_before");

    lifecycle_routes_assign_player(factory_probe_state.player2);
    lifecycle_routes_write_bank(player_physics_bank);
    lifecycle_discard_require(player_routes_enter(&player_physics_routes.routes) &&
        player_physics_identity_for(1) &&
        (uintptr_t)((void *(*)(void))LX(0x1dd0270))() == player_physics_zone,
        "physics_replay_scope_enter");

    update_input(context);
    lifecycle_discard_require(player_recurring_physics_entered() && player_physics_identity_for(1) &&
        (uintptr_t)((void *(*)(void))LX(0x1dd0270))() == player_physics_zone,
        "physics_replay_scope_return");
    player_route_snapshot snapshot = {0};
    lifecycle_discard_require(route_compare_snapshot(&player_physics_routes, &snapshot),
                              "physics_replay_route_snapshot");
    lifecycle_routes_read_bank(player_physics_bank);
    lifecycle_discard_require(player_routes_leave(&player_physics_routes.routes, snapshot),
                              "physics_replay_routes_leave");
    lifecycle_routes_write_bank(canonical_bank);
    lifecycle_routes_assign_player(factory_probe_player1);
    (void)lifecycle_discard_context_collect(&operations, &after);
    lifecycle_discard_require(player_physics_identity() && after.tree_valid &&
        before.node_count == after.node_count && before.target_present == after.target_present &&
        !memcmp(before.payloads, after.payloads, sizeof before.payloads), "physics_replay_contexts_changed");
    player_context_adapter_scope_end(generation);
    player_physics_require_bank(canonical_bank);
    lifecycle_discard_snapshot("physics_update_after", 0);
    player_physics_require_bank(canonical_bank);
    player_physics_upkeep_calls = 0;
    player_context_adapter_scope_begin(generation);
    player_physics_motion("before");
    player_physics_observe_body("before");
    player_context_adapter_scope_end(generation);
    player_physics_require_bank(canonical_bank);
    char message[160];
    snprintf(message, sizeof message,
        "X4LAN LIFECYCLE_DISCARD physics=ready generation=%llu main_parked=1\n",
        (unsigned long long)generation);
    lifecycle_discard_write(message);
    player_physics_replay_last = generation;
    player_physics_replay_busy = 0;
}
static void player_physics_replay_begin(uint64_t generation) {
    player_physics_replay_begin_with_input(generation, player_physics_scripted_input, &generation);
}
#endif

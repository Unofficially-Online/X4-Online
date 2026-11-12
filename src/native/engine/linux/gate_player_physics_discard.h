#ifndef X4_GATE_PLAYER_PHYSICS_DISCARD_H
#define X4_GATE_PLAYER_PHYSICS_DISCARD_H

#include "gate_player_body_observation.h"
#include "gate_player_world_class.h"

/* Default-unused one-worker-pass diagnostic. The caller alone authenticates a
 * recurring generation 5 with main parked and worker entered before begin,
 * and calls finish at the native MoveWorker tail before semDone. This probe
 * keeps all native objects, references, table and RX stubs resident, and never
 * returns after finish or a forbidden callback. Other native readers are not
 * excluded. Neither these getter samples nor reaching the tail proves motion. */
static route_compare_experiment player_physics_routes;
static unsigned char player_physics_bank[PCB_VALUE_BYTES];
static _Atomic unsigned player_physics_ready;
static uint64_t player_physics_worker;
static uintptr_t player_physics_zone;
static unsigned player_physics_upkeep_busy;
static unsigned player_physics_upkeep_calls;
static int player_recurring_physics_entered(void);
static inline void player_recurring_diagnostic_failure(void);
static void player_physics_upkeep(void *controller, double now);

/* A foreign thread may call these while Take is still constructing the
 * controller. Log only arguments and the caller identity: the owner's mutable
 * lifecycle fields cannot safely describe that intermediate state. */
static _Noreturn void player_physics_lifetime_failure(const char *reason, void *object) {
    char message[256];
    snprintf(message, sizeof(message),
        "X4LAN LIFECYCLE_DISCARD physics=lifetime reason=%s thread=%llu object=%llx\n",
        reason, (unsigned long long)GetCurrentThreadId(),
        (unsigned long long)(uintptr_t)object);
    lifecycle_discard_write(message);
    lifecycle_discard_exit(0, reason);
}

static void player_physics_ship_replaced(void *ship) {
    player_physics_lifetime_failure("physics_ship_replaced", ship);
}

static void player_physics_controller_destroyed(void *controller) {
    player_physics_lifetime_failure("physics_controller_destroyed", controller);
}

static void player_physics_prepare_take(uintptr_t ship) {
    plp_information information = {0};
    plp_prepare_fn prepare = player_lifetime_prepare_provider(&information);
    lifecycle_discard_require(prepare &&
        prepare((void *)ship, player_physics_ship_replaced, player_physics_controller_destroyed),
        "physics_lifetime_prepare");
}

static void player_physics_complete_take(uintptr_t controller) {
    plp_information information = {0};
    plp_complete_fn complete = player_lifetime_complete_provider(&information);
    lifecycle_discard_require(complete && complete((void *)controller), "physics_lifetime_complete");
}

#include "gate_player_callback_diagnostic.h"

static void player_physics_slot40(void *controller, double now) {
    player_physics_upkeep(controller, now);
}

/* World census runs on the main thread at a later tick, outside the P2
 * worker scope. The retained experiment owns this storage until process exit. */
static uintptr_t player_physics_world_class(void *ship, void *controller, uintptr_t observed) {
    if (atomic_load_explicit(&player_physics_ready, memory_order_acquire) != 1) return 0;
    player_controller_routes *routes = &player_physics_routes.routes;
    player_target_hold *hold = &player_target_hold_resident;
    uintptr_t stock[81], private_words[81], ship_controller = 0, controller_ship = 0, actual = 0;
    uintptr_t stock_table = (uintptr_t)LX(SEAT_PREFLIGHT_CONTROLLER_VTABLE);
    if (!ship || !controller || (uintptr_t)ship > UINTPTR_MAX - 0x200 ||
        (uintptr_t)controller > UINTPTR_MAX - 0x20 ||
        !read_local((unsigned char *)ship + 0x200, &ship_controller, sizeof ship_controller) ||
        !read_local((unsigned char *)controller + 0x20, &controller_ship, sizeof controller_ship) ||
        !read_local(controller, &actual, sizeof actual) || actual != observed ||
        !read_local((void *)(stock_table - 16), stock, sizeof stock) ||
        !read_local(routes->table, private_words, sizeof private_words)) return 0;
    player_world_class_check c = {
        .ship = (uintptr_t)ship, .controller = (uintptr_t)controller,
        .held_ship = lifecycle_discard_ship, .owned_controller = lifecycle_discard_controller,
        .route_ship = (uintptr_t)routes->held_ship, .route_controller = (uintptr_t)routes->controller,
        .ship_controller = ship_controller, .controller_ship = controller_ship,
        .observed_table = observed, .private_table = (uintptr_t)&routes->table[2],
        .stock_table = (uintptr_t)routes->stock_table, .expected_stock_table = stock_table,
        .ready = atomic_load_explicit(&player_physics_ready, memory_order_acquire),
        .installed = player_physics_routes.installed == 1 && player_physics_routes.controller == controller &&
                     player_physics_routes.held_ship == ship && player_physics_routes.stock_table == (void **)stock_table,
        .left = player_physics_routes.left == 1,
        .active = atomic_load_explicit(&player_routes_active, memory_order_acquire) == routes,
        .held = hold->identity.target == (uintptr_t)ship && hold->target_owned == 1 && hold->room_owned == 1 &&
                !hold->busy && !hold->uncertain && !hold->pending && player_target_hold_owned_slots(),
        .failures = atomic_load_explicit(&routes->access_failures, memory_order_acquire),
        .stock_pin = hash_ok(stock, sizeof stock, "76d84a63343e4506f05d71421f4f45e731a17574e9dade58b212dbd92dcd1bc3"),
        .stock_words = stock, .private_words = private_words,
        .replacements = {(uintptr_t)player_routes_transform, (uintptr_t)player_routes_velocity,
            (uintptr_t)player_routes_acceleration, (uintptr_t)player_routes_throttle,
            (uintptr_t)player_routes_no_highway, (uintptr_t)player_routes_assist,
            (uintptr_t)player_routes_agent_mode, (uintptr_t)player_physics_slot40,
            (uintptr_t)player_physics_slot41, (uintptr_t)player_physics_slot42,
            (uintptr_t)player_physics_slot43}
    };
    return player_world_class_matches(&c) ? stock_table : 0;
}

/* The original target-hold check requires the pre-take factory boundary, which
 * deliberately no longer holds here. Validate its retained ownership and
 * object identities without running that pre-take predicate. */
static int player_physics_identity_for(unsigned scoped) {
    player_target_hold *hold = &player_target_hold_resident;
    uint32_t target_count = 0, room_count = 0;
    if (!player_target_hold_read(hold->identity.target, PLAYER_TARGET_HOLD_COUNT_OFFSET, &target_count, 4) ||
        !player_target_hold_read(hold->identity.room, PLAYER_TARGET_HOLD_COUNT_OFFSET, &room_count, 4))
        return 0;
    return target_count > 1 && room_count > 1 &&
        player_physics_worker == (uint64_t)GetCurrentThreadId() &&
        factory_probe_singleton(scoped ? factory_probe_state.player2 : factory_probe_player1) &&
        hold->target_owned == 1 && hold->room_owned == 1 && !hold->busy &&
        !hold->uncertain && !hold->pending && player_target_hold_owned_slots() &&
        hold->identity.target == lifecycle_discard_ship &&
        lifecycle_discard_pointer(hold->identity.target + PLAYER_TARGET_HOLD_REFERENCE_OFFSET) ==
            hold->identity.target_reference_vtable &&
        lifecycle_discard_pointer(hold->identity.room + PLAYER_TARGET_HOLD_REFERENCE_OFFSET) ==
            hold->identity.room_reference_vtable &&
        lifecycle_discard_pointer(hold->identity.room + PLAYER_TARGET_HOLD_PARENT_OFFSET) ==
            lifecycle_discard_ship &&
        lifecycle_discard_pointer(lifecycle_discard_ship + 0x200) == lifecycle_discard_controller &&
        lifecycle_discard_pointer(lifecycle_discard_controller) == (uintptr_t)&player_physics_routes.routes.table[2] &&
        lifecycle_discard_pointer(lifecycle_discard_controller + 0x20) == lifecycle_discard_ship &&
        lifecycle_discard_pointer((uintptr_t)LX(PLAYER_ZONE_GLOBAL_VA)) == player_physics_zone &&
        player_physics_routes.installed && player_physics_routes.left &&
        player_routes_scope == (scoped ? &player_physics_routes.routes : NULL) &&
        atomic_load_explicit(&player_routes_active, memory_order_acquire) == &player_physics_routes.routes &&
        !atomic_load_explicit(&player_physics_routes.routes.access_failures, memory_order_acquire);
}

static int player_physics_identity(void) {
    return player_physics_identity_for(0);
}

#include "gate_player_context_policy_adapter.h"

/* Native simulation can advance P1's bank between callbacks. Compare against
 * this scope's entry state, not the bank saved before the whole worker pass. */
static void player_physics_require_bank(const unsigned char *expected) {
    unsigned char observed[PCB_VALUE_BYTES];
    lifecycle_routes_read_bank(observed);
    lifecycle_discard_require(player_physics_identity() &&
        !memcmp(observed, expected, sizeof(observed)), "physics_scope_canonical_state_changed");
}

/* One native upkeep call in the already admitted same-zone worker pass. The
 * resident P1/P2 references remain held; no reference or controller is freed.
 * This restores measured globals only. Native P2 events and controller-owned
 * references are retained, and unrelated background readers are not excluded. */
static void player_physics_upkeep(void *controller, double now) {
    static const lx_site upkeep_pin = {
        "player-upkeep", 0x1672370, 7095,
        "8d672ea7553c9bb4939fb173d8c86494f8eeb92068aa6dd14984e357d9d0c8d9"
    };
    static const lx_site zone_pin = {"current-player-zone", 0x1dd0270, 85,
        "77e7ef91167073d9dc9d54f5fd9eca2eca0c69f66ab840e6917b23999180b59f"};
    /* Preserve the original short-circuit order: -1 means not evaluated. */
    int entered = player_recurring_physics_entered();
    int ready = -1, same_controller = -1, identity = -1, idle = -1;
    int first_call = -1, finite_time = -1, upkeep_pinned = -1, zone_pinned = -1;
    int admitted = entered &&
        (ready = atomic_load_explicit(&player_physics_ready, memory_order_acquire) == 1) &&
        (same_controller = (uintptr_t)controller == lifecycle_discard_controller) &&
        (identity = player_physics_identity()) && (idle = !player_physics_upkeep_busy) &&
        (first_call = !player_physics_upkeep_calls) && (finite_time = !!isfinite(now)) &&
        (upkeep_pinned = site_ok(&upkeep_pin)) && (zone_pinned = site_ok(&zone_pin));
    if (!admitted) {
        char message[384];
        snprintf(message, sizeof message,
            "X4LAN CONTEXT_UPKEEP_FAILURE tick=%llu thread=%llu controller=%llx expected=%llx "
            "entered=%d ready=%d controller_match=%d identity=%d idle=%d first_call=%d "
            "finite_time=%d upkeep_pinned=%d zone_pinned=%d\n",
            (unsigned long long)stats.issued, (unsigned long long)GetCurrentThreadId(),
            (unsigned long long)(uintptr_t)controller, (unsigned long long)lifecycle_discard_controller,
            entered, ready, same_controller, identity, idle, first_call, finite_time, upkeep_pinned, zone_pinned);
        lifecycle_discard_write(message);
        player_recurring_diagnostic_failure();
        if (entered && ready == 1 && same_controller == 1 && identity == 1 && idle == 1) {
            static input_context_census_entries failure_entries;
            input_context_census_sample failure_context;
            const input_context_census_operations operations = {NULL, player_seat_preflight_read};
            int collected = input_context_census_collect_entries(&operations, sko_session.owner + 0x10,
                                                                  &failure_context, &failure_entries);
            player_context_diagnostic("upkeep", stats.issued, (uint64_t)GetCurrentThreadId(),
                failure_context.manager, collected, failure_context.complete, failure_context.tree_valid,
                failure_context.node_count, failure_context.target_present, failure_context.reason,
                failure_entries.payloads, failure_entries.count);
        }
    }
    lifecycle_discard_require(admitted, "physics_upkeep_admission");
    player_physics_upkeep_busy = 1;
    player_context_adapter_scope_begin(stats.issued);
    unsigned char canonical_bank[PCB_VALUE_BYTES];
    lifecycle_routes_read_bank(canonical_bank);
    lifecycle_discard_snapshot("physics_upkeep_before", 0);
    player_physics_require_bank(canonical_bank);
    input_context_census_sample before, after;
    const input_context_census_operations operations = {NULL, player_seat_preflight_read};
    lifecycle_discard_require(lifecycle_discard_context_collect(&operations, &before) &&
        before.tree_valid, "physics_upkeep_contexts_before");
    lifecycle_routes_assign_player(factory_probe_state.player2);
    lifecycle_routes_write_bank(player_physics_bank);
    lifecycle_discard_require(player_routes_enter(&player_physics_routes.routes) &&
        player_physics_identity_for(1) &&
        (uintptr_t)((void *(*)(void))LX(0x1dd0270))() == player_physics_zone,
        "physics_upkeep_scope_enter");
    char message[256];
    snprintf(message, sizeof(message),
        "X4LAN LIFECYCLE_DISCARD physics=upkeep stage=enter thread=%llu controller=%llx time=%.17g\n",
        (unsigned long long)GetCurrentThreadId(), (unsigned long long)(uintptr_t)controller, now);
    lifecycle_discard_write(message);
    ((void (*)(void *, double))LX(upkeep_pin.va))(controller, now);
    lifecycle_discard_require(player_recurring_physics_entered() && player_physics_identity_for(1) &&
        (uintptr_t)((void *(*)(void))LX(0x1dd0270))() == player_physics_zone,
        "physics_upkeep_scope_return");
    player_route_snapshot snapshot = {0};
    lifecycle_discard_require(route_compare_snapshot(&player_physics_routes, &snapshot),
        "physics_upkeep_snapshot");
    lifecycle_routes_read_bank(player_physics_bank);
    lifecycle_discard_require(player_routes_leave(&player_physics_routes.routes, snapshot),
        "physics_upkeep_routes_leave");
    lifecycle_routes_write_bank(canonical_bank);
    lifecycle_routes_assign_player(factory_probe_player1);
    lifecycle_discard_require(player_physics_identity() &&
        lifecycle_discard_context_collect(&operations, &after) &&
        after.tree_valid && before.node_count == after.node_count &&
        before.target_present == after.target_present &&
        !memcmp(before.payloads, after.payloads, sizeof(before.payloads)),
        "physics_upkeep_contexts_changed");
    player_context_adapter_scope_end(stats.issued);
    player_physics_require_bank(canonical_bank);
    lifecycle_discard_snapshot("physics_upkeep_after", 0);
    ++player_physics_upkeep_calls;
    player_physics_upkeep_busy = 0;
    snprintf(message, sizeof(message),
        "X4LAN LIFECYCLE_DISCARD physics=upkeep stage=leave thread=%llu controller=%llx time=%.17g\n",
        (unsigned long long)GetCurrentThreadId(), (unsigned long long)(uintptr_t)controller, now);
    lifecycle_discard_write(message);
}

static void player_physics_motion(const char *stage) {
    route_compare_experiment *experiment = &player_physics_routes;
    unsigned char *tls = gate_tls_self();
    double now = 0;
    lifecycle_discard_require(player_physics_identity() && gate_tls_worker(tls), "physics_motion_identity");
    unsigned char canonical_bank[PCB_VALUE_BYTES];
    lifecycle_routes_read_bank(canonical_bank);
    lifecycle_discard_read((uintptr_t)gate_tls_time(tls), &now, sizeof(now));
    lifecycle_discard_require(isfinite(now), "physics_motion_time");
    void **table = &experiment->routes.table[2];
    static const char *names[] = {"transform", "velocity", "acceleration"};
    static const char digits[] = "0123456789abcdef";
    for (unsigned index = 0; index < 3; ++index) {
        route_compare_output output;
        route_compare_initialize_output(&output, index, 1);
        ((player_route_motion)table[route_compare_slots[index]])(
            experiment->controller, output.data, 1, now);
        for (unsigned i = 0; i < sizeof(output.prefix); ++i)
            lifecycle_discard_require(output.prefix[i] == ROUTE_COMPARE_CANARY &&
                                      output.suffix[i] == ROUTE_COMPARE_CANARY, "physics_motion_canary");
        size_t size = route_compare_output_sizes[index];
        for (size_t i = size; i < sizeof(output.data); ++i)
            lifecycle_discard_require(output.data[i] == ROUTE_COMPARE_CANARY, "physics_motion_canary");
        char hex[ROUTE_COMPARE_BUFFER_SIZE * 2 + 1];
        for (size_t i = 0; i < size; ++i) {
            hex[i * 2] = digits[output.data[i] >> 4];
            hex[i * 2 + 1] = digits[output.data[i] & 15];
        }
        hex[size * 2] = 0;
        char message[512];
        snprintf(message, sizeof(message),
            "X4LAN LIFECYCLE_DISCARD physics=sample stage=%s getter=%s mode=1 size=%zu time=%.17g raw=%s\n",
            stage, names[index], size, now, hex);
        lifecycle_discard_write(message);
    }
    lifecycle_discard_require(player_physics_identity(), "physics_motion_access");
    player_physics_require_bank(canonical_bank);
}

/* Read actual Jolt state as well as controller getter outputs. These samples
 * do not assert that another physics thread is quiescent. Retain every link
 * needed to reject a body or scene change when comparing the two samples. */
static void player_physics_observe_body(const char *stage) {
    player_body_observation sample = {0};
    lifecycle_discard_require(player_body_observe(player_seat_preflight_read, NULL,
        lifecycle_discard_ship, &sample), "physics_body_unreadable");
    for (unsigned index = 0; index < PLAYER_BODY_RECORD_COUNT; ++index) {
        const player_body_record *record = &sample.records[index];
        const player_body_links *links = &record->links;
        char message[2048];
        snprintf(message, sizeof(message),
            "X4LAN LIFECYCLE_DISCARD physics=body stage=%s index=%u ship=%llx vtable=%llx "
            "parent=%llx runtime=%llu present=%u record=%llx xbody=%llx scene=%llx system=%llx "
            "interface=%llx manager=%llx array=%llx body=%llx motion=%llx count=%llu id=%u "
            "motion_present=%u position=%.9g,%.9g,%.9g rotation=%.9g,%.9g,%.9g,%.9g "
            "linear=%.9g,%.9g,%.9g angular=%.9g,%.9g,%.9g\n",
            stage, index, (unsigned long long)sample.ship, (unsigned long long)sample.ship_vtable,
            (unsigned long long)sample.ship_parent, (unsigned long long)sample.ship_runtime_id,
            links->present, (unsigned long long)links->record, (unsigned long long)links->xbody,
            (unsigned long long)links->scene, (unsigned long long)links->system,
            (unsigned long long)links->interface, (unsigned long long)links->manager,
            (unsigned long long)links->body_array, (unsigned long long)links->body,
            (unsigned long long)links->motion, (unsigned long long)links->body_count, links->body_id,
            record->motion_present, record->position[0], record->position[1], record->position[2],
            record->rotation[0], record->rotation[1], record->rotation[2], record->rotation[3],
            record->linear_velocity[0], record->linear_velocity[1], record->linear_velocity[2],
            record->angular_velocity[0], record->angular_velocity[1], record->angular_velocity[2]);
        lifecycle_discard_write(message);
    }
}

/* Only an already authenticated generation-5 entered worker calls this. */
static void player_physics_begin_with_input(unsigned framed, void (*update_input)(void *), void *context) {
    player_physics_worker = (uint64_t)GetCurrentThreadId();
    lifecycle_discard_begin(20, 1, framed ? 2 : 1);
    if (update_input) {
        update_input(context);
    } else {
        lifecycle_discard_input_press();
        lifecycle_discard_update_once(1);
        lifecycle_discard_input_release();
    }
    lifecycle_discard_snapshot("physics_updated", 0);
    player_physics_zone = player_zone_resident.anchor;
    double now = 0;
    lifecycle_discard_read((uintptr_t)gate_tls_time(gate_tls_self()), &now, sizeof(now));
    player_route_callbacks callbacks = {(void *)player_physics_slot40, (void *)player_physics_slot41,
                                        (void *)player_physics_slot42, (void *)player_physics_slot43};
    lifecycle_discard_require(player_physics_zone && isfinite(now) &&
        route_compare_prepare_with_callbacks(&player_physics_routes, (void *)lifecycle_discard_controller,
                                             (void *)lifecycle_discard_ship, now, &callbacks),
        "physics_routes_prepare");
    lifecycle_discard_require(route_compare_capture_scoped(&player_physics_routes) &&
                              route_compare_leave(&player_physics_routes), "physics_routes_capture");
    lifecycle_routes_read_bank(player_physics_bank);
    lifecycle_discard_require(pz_restore(&player_zone_resident.ledger, &lifecycle_isolation_zone_key),
                              "physics_zone_restore");
    lifecycle_routes_write_bank(lifecycle_discard_initial_bank);
    lifecycle_routes_assign_player(factory_probe_player1);
    lifecycle_routes_require_canonical(player_physics_zone);
    lifecycle_discard_require(player_physics_identity(), "physics_ready_identity");
    if (framed) {
        player_context_adapter_init();
    }
    lifecycle_discard_snapshot("physics_canonical", 0);
    player_context_adapter_scope_begin(stats.issued);
    player_physics_motion("before");
    player_physics_observe_body("before");
    player_context_adapter_scope_end(stats.issued);
    lifecycle_discard_write("X4LAN LIFECYCLE_DISCARD physics=ready generation=5 main_parked=1\n");
    atomic_store_explicit(&lxw_controller_normalize, player_physics_world_class, memory_order_release);
    atomic_store_explicit(&player_physics_ready, 1, memory_order_release);
}

static void player_physics_begin_mode(unsigned framed) {
    player_physics_begin_with_input(framed, NULL, NULL);
}

static void player_physics_discard_begin(void) {
    player_physics_begin_mode(0);
}

static void player_physics_sample_end(void) {
    lifecycle_discard_require(atomic_load_explicit(&player_physics_ready, memory_order_acquire) == 1 &&
        player_physics_identity() && !player_physics_upkeep_busy && player_physics_upkeep_calls == 1,
        "physics_tail_identity");
    player_context_adapter_outside(stats.issued, PLAYER_CONTEXT_FRAME_END);
    player_context_adapter_scope_begin(stats.issued);
    lifecycle_discard_snapshot("physics_end", 0);
    player_physics_observe_body("after");
    player_physics_motion("after");
    player_context_adapter_scope_end(stats.issued);
    lifecycle_discard_snapshot("physics_sampled", 0);
}

static _Noreturn void player_physics_discard_finish(void) {
    player_physics_sample_end();
    lifecycle_discard_exit(1, "physics_frame_returned");
}

static void player_physics_replay_begin(uint64_t generation);

static void player_physics_frame_record(uint64_t generation, const char *event) {
    char message[160];
    snprintf(message, sizeof(message),
        "X4LAN LIFECYCLE_DISCARD physics=frame tick=%llu event=%s\n",
        (unsigned long long)generation, event);
    lifecycle_discard_write(message);
}

static void player_physics_frames_begin(uint64_t generation) {
    if (generation == 5) {
        player_physics_begin_mode(1);
    } else {
        player_physics_frame_record(generation, "begin");
        player_physics_replay_begin(generation);
    }
}

static void player_physics_frames_finish(uint64_t generation, unsigned final) {
    player_physics_sample_end();
    player_physics_frame_record(generation, "end");
    if (final) {
        lifecycle_discard_exit(1, "physics_frames_returned");
    }
}
#endif

#ifndef X4_GATE_PLAYER_PRESENTATION_PROBE_H
#define X4_GATE_PLAYER_PRESENTATION_PROBE_H

#include "gate_player_startup_phases.h"

/* Explicit one-shot tick-7 UI diagnostic; include after physics, keyboard and
 * camera headers. It never publishes P2. Native cached lookup temporarily owns
 * one reference; object holds and admitted phase remain the caller's duty. */
#include "player_presentation_probe.h"

static _Atomic unsigned presentation_probe_attempted;

static int presentation_native_pins(void) {
    static const lx_site pins[] = {
        {"presentation-occupied", 0xa44e40, 0x53, "c4ae051dfa320a46f8049b7e35f5f686c7f4455e98f33ce52b13c2cbe0c134d3"},
        {"presentation-ship", 0x169af50, 0x8a, "46a9d0b2850828840e5fef14431aacf27f815e6956225caa57e9ca3fdaa27f67"},
        {"presentation-controlled", 0x1699f20, 0x6b, "875a39c3fadc0608768592821830bf99016fb500cdc452f788f7e69fa2091196"},
        {"presentation-live", 0x189d8f0, 0x37, "7c5c18f7f341ce22b68eefdc5479b7e8c7e4d55fa0d259ed3e708edd44b951db"},
        {"presentation-cached", 0x189f8d0, 0xae, "6b1cdc06228630f355c25b7b29a879b992fedb9d418f4b9d449376b669469a1e"},
        {"presentation-release", 0xa14540, 0x25, "7368cef95d1efcc8cda06dfd53df92ca026f1da024481b82f3c7a86b8db75587"},
        {"ui-firstperson-getter", 0xa3ccd0, 0x22, "1eef28cd12fc4e335cae6b5338198f4835731336dc49c6c4b4819d860eb84119"},
        {"ui-cockpit-handler", 0xa78130, 0x66, "fb9fb4bdf42e7d81a9dc202cff6e005eb4472a468ec2e82ddddb22fd0aad62a4"},
        {"ui-firstperson-handler", 0xa781a0, 0x66, "3f837e21bca5a488efbdf35a86016437dc12ad836ca66de89eca8bc9c4f41955"},
        {"ui-plan-select", 0xa78010, 0x11e, "a96f6a6c9d631df528d4c5393996bf737886f2e8aee72e10d509db8223e3b10a"},
        {"ui-plan-apply", 0xa2cb90, 0x11c, "a61d665e2e1c62b9e0323674002827efe5165a8ef46383f5d4b45349e6f12912"},
        {"ui-plan-dispatch", 0xa25770, 0xc3, "992b66044eddd4d4b58d0305ec47219b23591a48e47cd0c26937639866d3890d"},
        {"ui-plan-change", 0xa2b0a0, 0x1458, "b481f1d5b87ccbda681ad8553582ed11b1394e9366036e0e738220737dc6bfb1"}
    };
    unsigned char digest[32];
    char hex[65];
    if (!gate_file_sha256(gate_env.exe, LX_FILE_SIZE, digest)) return 0;
    hex32(digest, hex);
    if (strcmp(hex, PLAYER_BODY_IMAGE_SHA) || !player_seat_preflight_pins(NULL)) return 0;
    for (unsigned i = 0; i < sizeof pins / sizeof pins[0]; ++i) {
        if (site_ok(&pins[i])) continue;
        if (i == 0 && view_query_occupied_original_pin()) continue;
        return 0;
    }
    return 1;
}

/* These are identity observations only; the handlers remain uncalled. The
 * verified worker boundary does not exclude unrelated UI or renderer readers. */
typedef struct {
    uintptr_t manager, presenter, presenter_vtable, controller, controller_vtable;
    unsigned char first_person;
} presentation_ui_plan;

static int presentation_ui_read(uintptr_t object, size_t offset, void *output, size_t size) {
    const uintptr_t max_user = UINT64_C(0x00007fffffffffff);
    return object >= 0x10000 && object <= max_user && !(object & 7) &&
        offset <= max_user - object && size <= max_user - (object + offset) &&
        read_local((void *)(object + offset), output, size);
}

static int presentation_ui_plan_collect(presentation_ui_plan *sample) {
    uintptr_t cockpit = 0, firstperson = 0, plan_change = 0;
    memset(sample, 0, sizeof *sample);
    return read_local(LX(0x3db6e48), &sample->manager, sizeof sample->manager) &&
        presentation_ui_read(sample->manager, 8, &sample->presenter, sizeof sample->presenter) &&
        presentation_ui_read(sample->presenter, 0, &sample->presenter_vtable, sizeof sample->presenter_vtable) &&
        sample->presenter_vtable == (uintptr_t)LX(0x3ab4600) &&
        presentation_ui_read(sample->presenter, 0x18, &sample->controller, sizeof sample->controller) &&
        presentation_ui_read(sample->presenter, 0x6c, &sample->first_person, sizeof sample->first_person) &&
        sample->first_person <= 1 &&
        presentation_ui_read(sample->controller, 0, &sample->controller_vtable, sizeof sample->controller_vtable) &&
        sample->controller_vtable == (uintptr_t)LX(0x3ab4060) &&
        read_local(LX(0x3ab4610), &cockpit, sizeof cockpit) && cockpit == (uintptr_t)LX(0xa78130) &&
        read_local(LX(0x3ab4618), &firstperson, sizeof firstperson) && firstperson == (uintptr_t)LX(0xa781a0) &&
        read_local(LX(0x3ab4070), &plan_change, sizeof plan_change) && plan_change == (uintptr_t)LX(0xa2b0a0);
}

static int presentation_native_read(void *unused, uintptr_t at, void *out, size_t size) {
    (void)unused;
    return read_local((void *)at, out, size);
}
static uintptr_t presentation_native_live(void *unused, uintptr_t player) {
    (void)unused;
    return (uintptr_t)((void *(*)(void *, int, unsigned char))LX(0x189d8f0))((void *)player, 0x19, 0);
}
static uintptr_t presentation_native_cached(void *unused, uintptr_t player) {
    (void)unused;
    void *answer = NULL;
    ((void **(*)(void **, void *, int, unsigned char))LX(0x189f8d0))(&answer, (void *)player, 0x19, 0);
    return (uintptr_t)answer;
}
static void presentation_native_release(void *unused, uintptr_t object) {
    (void)unused;
    ((void (*)(void *))LX(0xa14540))((void *)(object + 0x38));
}
static int presentation_native_room_ok(void *unused, uintptr_t room) {
    (void)unused;
    uintptr_t table = 0;
    uint32_t gate = 1;
    if (!read_local((void *)room, &table, 8) || room > UINTPTR_MAX - 0x68 ||
        !read_local((void *)(room + 0x68), &gate, 4)) return 0;
    return gate == 0 && (table == (uintptr_t)LX(SEAT_PREFLIGHT_CONTROL_ROOM_VTABLE) ||
                         table == (uintptr_t)LX(SEAT_PREFLIGHT_COCKPIT_VTABLE));
}

static void presentation_native_row(const char *name, uint32_t bank, const presentation_probe_row *row) {
    char line[640];
    int n = snprintf(line, sizeof line,
        "X4LAN PRESENTATION_PROBE v=1 stage=%s tick=%llu slot=%u player=%llx parent=%llx vector=%llx,%llx,%llx position=%u bank=%u cache_parent=%llx live_called=%u live=%llx cached_called=%u cached=%llx omitted=%u refs=%u,%u,%u marker=%u,%u\n",
        name, (unsigned long long)player_startup()->presentation_probe, stats.slot, (unsigned long long)row->player, (unsigned long long)row->parent,
        (unsigned long long)row->vector_begin, (unsigned long long)row->vector_end,
        (unsigned long long)row->vector_capacity, row->position, bank,
        (unsigned long long)row->cache_parent, row->live_called, (unsigned long long)row->live,
        row->cached_called, (unsigned long long)row->cached, row->cached_omitted,
        row->refs_before, row->refs_acquired, row->refs_after, row->marker_before, row->marker_after);
    lifecycle_discard_require(n > 0 && (size_t)n < sizeof line, "presentation_format");
    lifecycle_discard_write(line);
}

static int lua_player_presentation_probe(lua_State *state) {
    const char *reason = "admission";
    int success = 0;
    if (atomic_exchange_explicit(&presentation_probe_attempted, 1, memory_order_acq_rel)) {
        reason = "reentry";
        goto finish;
    }
    if (!accept_ui() || !stats.active || (uint64_t)stats.issued != player_startup()->presentation_probe || boot_phase != 3 ||
        (stats.slot != 1 && stats.slot != 2) || wire_clients != 2 || !keyboard_probe_requested ||
        keyboard_probe_queue.config.independent_players != 1 || player_recurring_physics != 3 ||
        atomic_load_explicit(&player_physics_ready, memory_order_acquire) != 1 ||
        !stock_observer_worker_boundary() || !gate_tls_worker(gate_tls_self()) ||
        player_routes_scope || player_network_busy || player_physics_replay_busy ||
        player_physics_upkeep_busy || keyboard_probe_inflight || atomic_load(&stock_observer_input_depth) ||
        atomic_load(&sko_session.wrapper_depth) || !player_physics_identity() ||
        !factory_probe_state.player2_owned || factory_probe_state.player2_uncertain ||
        !sko_session.owner || sko_session.owner > UINTPTR_MAX - 0x10) goto finish;
    reason = "pins";
    if (!presentation_native_pins()) goto finish;
    uint32_t bank = 0;
    uintptr_t p1room = 0;
    unsigned char *tls = gate_tls_self();
    if (!read_local(LX(0x3e77430), &bank, 4) || bank > 1 ||
        !read_local((void *)(factory_probe_player1 + 0x70), &p1room, 8)) {
        reason = "bank_or_parent";
        goto finish;
    }
    (void)tls; /* worker admission selects the 3e77430 bank. */
    unsigned char before_bank[PCB_VALUE_BYTES], after_bank[PCB_VALUE_BYTES];
    input_context_census_sample before_context = {0}, after_context = {0};
    static input_context_census_entries before_entries, after_entries;
    const input_context_census_operations census_ops = {NULL, player_seat_preflight_read};
    lifecycle_routes_read_bank(before_bank);
    reason = "context_before";
    if (!input_context_census_collect_entries(&census_ops, sko_session.owner + 0x10,
                                               &before_context, &before_entries)) goto finish;
    const presentation_probe_ops ops = {NULL, presentation_native_read, presentation_native_live,
        presentation_native_cached, presentation_native_release, presentation_native_room_ok};
    presentation_probe_row p1 = {0}, p2 = {0};
    reason = "p1";
    int p1_ok = presentation_probe_sample(&ops, factory_probe_player1, bank, p1room, &p1);
    presentation_native_row("p1", bank, &p1);
    if (!p1_ok) goto finish;
    reason = "p2";
    int p2_ok = presentation_probe_sample(&ops, factory_probe_state.player2, bank,
                                           player_target_hold_resident.identity.room, &p2);
    presentation_native_row("p2", bank, &p2);
    if (!p2_ok) goto finish;
    presentation_ui_plan plan_before = {0}, plan_after = {0};
    reason = "ui_plan_identity";
    if (!presentation_ui_plan_collect(&plan_before)) goto finish;
    uint64_t occupied = ((uint64_t (*)(void))LX(0xa44e40))();
    lifecycle_routes_read_bank(after_bank);
    if (!presentation_ui_plan_collect(&plan_after) ||
        memcmp(&plan_before, &plan_after, sizeof plan_before) ||
        !player_physics_identity() || memcmp(before_bank, after_bank, sizeof before_bank) ||
        !input_context_census_collect_entries(&census_ops, sko_session.owner + 0x10,
                                               &after_context, &after_entries) ||
        before_entries.count != after_entries.count ||
        memcmp(before_entries.payloads, after_entries.payloads, before_entries.count * 16u)) {
        reason = "identity_bank_or_context_after";
        goto finish;
    }
    char message[320];
    snprintf(message, sizeof message,
        "X4LAN PRESENTATION_PROBE v=1 stage=ui_plan tick=%llu slot=%u manager=%llx presenter=%llx presenter_vtable=%llx controller=%llx controller_vtable=%llx flag=%u authenticated=1\n",
        (unsigned long long)player_startup()->presentation_probe, stats.slot, (unsigned long long)plan_before.manager, (unsigned long long)plan_before.presenter,
        (unsigned long long)plan_before.presenter_vtable, (unsigned long long)plan_before.controller,
        (unsigned long long)plan_before.controller_vtable, (unsigned)plan_before.first_person);
    lifecycle_discard_write(message);
    snprintf(message, sizeof message, "X4LAN PRESENTATION_PROBE v=1 stage=occupied tick=%llu slot=%u runtime=%llu observational=1\n", (unsigned long long)player_startup()->presentation_probe, stats.slot, (unsigned long long)occupied);
    lifecycle_discard_write(message);
    success = 1;
    reason = "ok";
finish:
    {
        char message[192];
        snprintf(message, sizeof message, "X4LAN PRESENTATION_PROBE v=1 stage=result tick=%llu slot=%u success=%d reason=%s\n", (unsigned long long)player_startup()->presentation_probe, stats.slot, success, reason);
        if (lifecycle_discard_log >= 0) {
            lifecycle_discard_write(message);
        } else {
            (void)write(STDERR_FILENO, message, strlen(message));
        }
    }
    p_lua_pushnumber(state, success);
    return 1;
}
#endif

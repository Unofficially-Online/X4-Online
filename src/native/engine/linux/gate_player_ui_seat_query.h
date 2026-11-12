#ifndef X4_GATE_PLAYER_UI_SEAT_QUERY_H
#define X4_GATE_PLAYER_UI_SEAT_QUERY_H

#include "player_ui_seat_query.h"

/* One bounded parent-worker experiment; failures terminate the disposable run. */
static int ui_seat_query_enabled;
static unsigned ui_seat_query_attempted;
static input_context_census_entries ui_seat_query_context_entries;


typedef struct {
    uintptr_t actor, actor_table, actor_parent, actor_controller, actor_controller_table;
    uintptr_t ship, ship_table, ship_reference_table, ship_room;
    uintptr_t room, room_table, room_reference_table, room_parent;
    uint64_t ship_runtime, room_runtime;
    uint32_t position, ship_count, ship_marker, ship_class_gate, room_count, room_marker, room_class_gate;
} ui_seat_query_identity;

/* Only published after both stock calls and both P1 releases have succeeded. */
static ui_seat_query_identity ui_seat_query_identities[2];
static _Alignas(16) float ui_seat_query_poses[2][16];
static int ui_seat_query_complete;

typedef struct {
    unsigned count;
    char hash[65];
} ui_seat_query_context;

static int ui_seat_query_environment(uint64_t *rng) {
    unsigned char *tls = gate_tls_self();
    uint32_t bank = 2;
    return tls && gate_tls_worker(tls) && gate_tls_rng(tls) == rng &&
        player_recurring_physics_entered() && player_physics_identity() &&
        presentation_fork_read(NULL, (uintptr_t)LX(0x3e77430), &bank, 4) &&
        bank == ui_overlay_expected_bank(&ui_overlay_probe) &&
        presentation_fork_word((uintptr_t)LX(FACTORY_PROBE_UNIVERSE_VA), presentation_fork.universe);
}

static int ui_seat_query_identity_read(unsigned index, ui_seat_query_identity *identity) {
    const presentation_fork_request *request = &presentation_fork.request;
    memset(identity, 0, sizeof *identity);
    identity->actor = index ? request->player2 : request->player1;
    identity->ship = index ? presentation_fork.p2_ship : presentation_fork.p1_ship;
    identity->room = index ? request->room : presentation_fork.p1_room;
    if (!identity->actor || !identity->ship || !identity->room ||
        identity->ship == identity->room) return 0;
    if (!player_target_hold_read(identity->actor, 0, &identity->actor_table, 8) ||
        !player_target_hold_read(identity->actor, 0x70, &identity->actor_parent, 8) ||
        !player_target_hold_read(identity->actor, 0x200, &identity->actor_controller, 8) ||
        !player_target_hold_read(identity->actor, 0x6aa8, &identity->position, 4) ||
        !player_target_hold_read(identity->ship, 0, &identity->ship_table, 8) ||
        !player_target_hold_read(identity->ship, 8, &identity->ship_runtime, 8) ||
        !player_target_hold_read(identity->ship, 0x38, &identity->ship_reference_table, 8) ||
        !player_target_hold_read(identity->ship, 0x40, &identity->ship_count, 4) ||
        !player_target_hold_read(identity->ship, 0x44, &identity->ship_marker, 4) ||
        !player_target_hold_read(identity->ship, 0x68, &identity->ship_class_gate, 4) ||
        !player_target_hold_read(identity->ship, 0x408, &identity->ship_room, 8) ||
        !player_target_hold_read(identity->room, 0, &identity->room_table, 8) ||
        !player_target_hold_read(identity->room, 8, &identity->room_runtime, 8) ||
        !player_target_hold_read(identity->room, 0x38, &identity->room_reference_table, 8) ||
        !player_target_hold_read(identity->room, 0x40, &identity->room_count, 4) ||
        !player_target_hold_read(identity->room, 0x44, &identity->room_marker, 4) ||
        !player_target_hold_read(identity->room, 0x68, &identity->room_class_gate, 4) ||
        !player_target_hold_read(identity->room, 0x70, &identity->room_parent, 8)) return 0;
    if (identity->actor_controller &&
        !player_target_hold_read(identity->actor_controller, 0, &identity->actor_controller_table, 8)) return 0;
    return identity->actor_table == (uintptr_t)LX(SEAT_PREFLIGHT_PLAYER_VTABLE) &&
        identity->actor_parent == identity->room &&
        identity->ship_table == (uintptr_t)LX(SEAT_PREFLIGHT_SHIP_VTABLE) &&
        identity->ship_reference_table == (uintptr_t)LX(PLAYER_TARGET_HOLD_SHIP_REFERENCE_VTABLE) &&
        identity->room_table == (uintptr_t)LX(SEAT_PREFLIGHT_COCKPIT_VTABLE) &&
        identity->room_reference_table == (uintptr_t)LX(PLAYER_TARGET_HOLD_COCKPIT_REFERENCE_VTABLE) &&
        identity->ship_runtime == (index ? request->expected_p2_ship : request->expected_p1_ship) &&
        identity->room_runtime && identity->ship_room == identity->room &&
        identity->room_parent == identity->ship && identity->ship_marker == 2 && identity->room_marker == 2 &&
        identity->ship_class_gate == 0 && identity->room_class_gate == 0 &&
        identity->ship_count > 0 && identity->ship_count < INT32_MAX - 1 &&
        identity->room_count > 0 && identity->room_count < INT32_MAX - 1;
}

static int ui_seat_query_context_read(ui_seat_query_context *result) {
    input_context_census_sample sample = {0};
    const input_context_census_operations operations = {NULL, player_seat_preflight_read};
    unsigned char digest[32];
    if (!sko_session.owner || sko_session.owner > UINTPTR_MAX - 0x10 ||
        !input_context_census_collect_entries(&operations, sko_session.owner + 0x10, &sample,
                                              &ui_seat_query_context_entries) ||
        !sample.tree_valid || sample.node_count != ui_seat_query_context_entries.count) return 0;
    result->count = ui_seat_query_context_entries.count;
    lockstep_sha256(ui_seat_query_context_entries.payloads, result->count * 16u, digest);
    hex32(digest, result->hash);
    return 1;
}

/* Only authenticated P1 holds are changed here; existing P2 ownership remains
 * untouched. Failure leaves an uncertain obligation and terminates the run. */
static int ui_seat_query_p1_reference(void *context, int room, int acquire) {
    uint64_t *rng = context;
    ui_seat_query_identity before, checked, after;
    if (!ui_seat_query_environment(rng) || !ui_seat_query_identity_read(0, &before) ||
        !ui_seat_query_identity_read(0, &checked) || memcmp(&before, &checked, sizeof before))
        ui_overlay_die("seat_reference_before");
    uintptr_t object = room ? before.room : before.ship;
    uint32_t current_count = room ? before.room_count : before.ship_count;
    if (!acquire && current_count <= 1) ui_overlay_die("seat_reference_last");
    if (acquire) ((void (*)(void *))LX(PLAYER_TARGET_HOLD_ACQUIRE_VA))((void *)object);
    else ((void (*)(void *))LX(PLAYER_TARGET_HOLD_RELEASE_VA))((void *)(object + 0x38));
    if (!ui_seat_query_environment(rng) || !ui_seat_query_identity_read(0, &after))
        ui_overlay_die("seat_reference_after");
    uint32_t *count = room ? &before.room_count : &before.ship_count;
    if (acquire) ++*count; else --*count;
    if (memcmp(&before, &after, sizeof before)) ui_overlay_die("seat_reference_changed");
    return 1;
}

static int ui_seat_query_one(void *context, unsigned index) {
    uint64_t *rng = context;
    ui_seat_query_identity before, after;
    ui_seat_query_context context_before, context_after;
    if (!ui_seat_query_environment(rng) || !ui_seat_query_identity_read(index, &before) ||
        !ui_seat_query_context_read(&context_before) || !ui_seat_query_environment(rng))
        ui_overlay_die("seat_query_before");
    uint64_t rng_before = *rng;
    _Alignas(16) float output[16] = {0};
    int32_t position = (int32_t)before.position;
    unsigned char returned = ((unsigned char (*)(void *, float *, void *, const int32_t *))LX(0x131e9a0))(
        (void *)before.ship, output, (void *)before.room, &position);
    if (!ui_seat_query_environment(rng)) ui_overlay_die("seat_query_tls_after");
    uint64_t rng_after = *rng;
    if (!ui_seat_query_identity_read(index, &after) || memcmp(&before, &after, sizeof before) ||
        !ui_seat_query_context_read(&context_after) || !ui_seat_query_environment(rng))
        ui_overlay_die("seat_query_after");
    *rng = rng_before;
    if (*rng != rng_before) ui_overlay_die("seat_query_rng_restore");
    player_ui_seat_query_result result;
    player_ui_seat_query_classify(output, &result);
    char raw[129], words[129];
    for (unsigned i = 0; i < 16; ++i) {
        snprintf(raw + 8 * i, 9, "%08x", result.raw[i]);
        snprintf(words + 8 * i, 9, "%08x", result.normalized[i]);
    }
    dprintf(2, "X4 UI SEAT_QUERY v=1 tick=9 role=%s return=%u actor=%llx ship=%llx room=%llx "
        "position=%u bank=%u rng_before=%016llx rng_after=%016llx rng_restored=%016llx "
        "contexts_before=%u hash_before=%s contexts_after=%u hash_after=%s finite=%d rigid=%d raw=%s words=%s\n",
        index ? "p2" : "p1", (unsigned)returned, (unsigned long long)before.actor,
        (unsigned long long)before.ship, (unsigned long long)before.room, before.position,
        ui_overlay_expected_bank(&ui_overlay_probe), (unsigned long long)rng_before, (unsigned long long)rng_after,
        (unsigned long long)*rng, context_before.count, context_before.hash, context_after.count,
        context_after.hash, result.finite, result.rigid, raw, words);
    if (returned != 1) ui_overlay_die("seat_query_native_return");
    ui_seat_query_identities[index] = before;
    memcpy(ui_seat_query_poses[index], output, sizeof output);
    return 1;
}

static void ui_seat_query_once(void) {
    static const lx_site seat = {"ui-seat-query", 0x131e9a0, 0x171,
        "aeb628df89532b5a6cedfe9bd2d54572ca82b52f31b51af2eda4e0ae011ce84b"};
    if (!ui_seat_query_enabled) return;
    unsigned char *tls = gate_tls_self();
    uint64_t *rng = tls ? gate_tls_rng(tls) : NULL;
    if (ui_seat_query_attempted++ || stats.issued != 9 || ui_overlay_multi_count != 160 ||
        !rng || !ui_seat_query_environment(rng) || !player_target_hold_pins() || !site_ok(&seat))
        ui_overlay_die("seat_query_admission");
    const player_ui_seat_query_ops operations = {rng, ui_seat_query_p1_reference, ui_seat_query_one};
    if (!player_ui_seat_query_run(&operations)) ui_overlay_die("seat_query_sequence");
    ui_seat_query_complete = 1;
    dprintf(2, "X4 UI SEAT_QUERY v=1 tick=9 stage=complete calls=2 p1_holds_released=1\n");
}
#endif

#ifndef X4_GATE_PLAYER_SELECTION_PARITY_H
#define X4_GATE_PLAYER_SELECTION_PARITY_H

/* Optional evidence at an already authenticated local capture. This never
 * chooses the capture target and never calls GetPlayerTarget/GetSofttarget. */
#include "player_selection_parity.h"

typedef struct {
    player_stock_view_capture_identity identity;
    uintptr_t players[2];
    uint64_t runtimes[2];
} selection_parity_native;

static uint64_t selection_parity_last_generation;

static int selection_parity_native_auth(void *context) {
    const selection_parity_native *native = context;
    if (!native || !keyboard_probe_worker() || !player_recurring_physics_entered() ||
        player_routes_scope || native->identity.generation != stats.issued ||
        native->identity.local_slot != stats.slot ||
        !player_physics_identity() || !factory_probe_state.player2_owned || factory_probe_state.player2_uncertain ||
        native->players[0] != factory_probe_player1 || native->players[1] != factory_probe_state.player2 ||
        native->runtimes[0] != factory_probe_identity.player_runtime ||
        native->runtimes[1] != factory_probe_player2_runtime ||
        !keyboard_probe_binding.controller || keyboard_probe_binding.controller != factory_probe_identity.controller ||
        !keyboard_probe_binding.ship || keyboard_probe_binding.ship != factory_probe_identity.ship) return 0;
    if (stats.slot == 2) {
        if (!stock_view_ui_lease_auth(NULL, &native->identity)) return 0;
    } else if (stats.slot == 1) {
        if (!stock_view_host_auth(NULL, &native->identity)) return 0;
    } else return 0;
    uintptr_t ship = 0, controller = 0;
    if (keyboard_probe_binding.controller > UINTPTR_MAX - 0x28 ||
        keyboard_probe_binding.ship > UINTPTR_MAX - 0x208 ||
        !read_local((void *)(keyboard_probe_binding.controller + 0x20), &ship, 8) ||
        ship != keyboard_probe_binding.ship ||
        !read_local((void *)(ship + 0x200), &controller, 8) || controller != keyboard_probe_binding.controller) return 0;
    player_stock_view_control_ops registry = {
        .read = stock_view_control_native_read, .registry_global = (uintptr_t)LX(0x3daa860u)
    };
    for (unsigned index = 0; index < 2; ++index) {
        uintptr_t table = 0;
        if (!native->players[index] || !native->runtimes[index] ||
            !read_local((void *)native->players[index], &table, 8) || table != (uintptr_t)LX(0x3b5f7a0u) ||
            !stock_view_control_registry(&registry, native->players[index], native->runtimes[index])) return 0;
    }
    return 1;
}

static int selection_parity_native_read(void *context, uintptr_t address, void *out, size_t size) {
    (void)context;
    return read_local((void *)address, out, size);
}

static int selection_parity_native_map(void *context, uint64_t runtime,
                                       uint64_t *saved, uint64_t *portable) {
    (void)context;
    if (!world_identity_sealed || !world_identities || !runtime || !saved || !portable) return 0;
    *saved = world_identity_lookup(runtime);
    *portable = world_identity_portable(runtime);
    return 1;
}

static int selection_parity_native_pins(void) {
    static const lx_site sites[] = {
        {"selection-player-reference", 0x1dd0040, 51,
         "09ccbc4ae147249804838115a870ec1052d7d2f586dab4fc79e30bdf32f004d0"},
        {"selection-reference-layout", 0x14bcf00, 179,
         "6263c530e31d9f895e0b2e036f2131cc42269b2400a58498df4aaabbad1beb7a"},
        {"selection-ui-softtarget", 0xa45060, 281,
         "e9732073f43bd1ca97f5316e64fb3eae300143026dd74cf5456d1230d384a06a"}
    };
    for (unsigned index = 0; index < sizeof sites / sizeof sites[0]; ++index) {
        if (!site_ok(&sites[index])) return 0;
    }
    return 1;
}

static int selection_parity_write(uint64_t generation, unsigned slot, const char *source,
                                  const selection_parity_value *value) {
    char line[512];
    const char *mapping = value->status == SELECTION_PARITY_ABSENT ? "none" :
        !value->mapping_available ? "unavailable" :
        value->saved ? "saved" : value->portable ? "created" : "unmapped";
    int length = snprintf(line, sizeof line,
        "X4 SELECTION_PARITY v=1 tick=%llu slot=%u source=%s status=%s at=%llx raw=%016llx "
        "object=%llx runtime=%016llx mapping=%s saved=%016llx portable=%016llx\n",
        (unsigned long long)generation, slot, source, selection_parity_status_name(value->status),
        (unsigned long long)value->source, (unsigned long long)value->raw,
        (unsigned long long)value->object, (unsigned long long)value->runtime, mapping,
        (unsigned long long)value->saved, (unsigned long long)value->portable);
    return length > 0 && (size_t)length < sizeof line && write(2, line, (size_t)length) == length;
}

static int player_selection_parity_capture_inner(uint64_t generation, unsigned slot) {
    const char *setting = getenv("X4_DEV_SELECTION_PARITY");
    if (!setting) return 1;
    if (strcmp(setting, "1") || !player_startup()->version || !player_session_budget.frames ||
        !generation || generation <= selection_parity_last_generation || slot != stats.slot ||
        generation < keyboard_probe_queue.config.first_capture ||
        generation > keyboard_probe_queue.config.last_capture ||
        !selection_parity_native_pins()) return 0;
    selection_parity_native native = {
        .identity = {generation, keyboard_probe_binding.universe,
            slot == 2 ? presentation_fork.request.player2 : keyboard_probe_binding.player,
            slot == 2 ? lifecycle_discard_controller : keyboard_probe_binding.controller, slot},
        .players = {factory_probe_player1, factory_probe_state.player2},
        .runtimes = {factory_probe_identity.player_runtime, factory_probe_player2_runtime}
    };
    selection_parity_ops ops = {
        &native, selection_parity_native_auth, selection_parity_native_read, selection_parity_native_map,
        native.players[0], native.players[1], (uintptr_t)LX(0x3daa860u), (uintptr_t)LX(0x3dbafd0u)
    };
    selection_parity_snapshot snapshot;
    if (!player_selection_parity_collect(&ops, &snapshot) || !selection_parity_native_pins()) return 0;
    char line[384];
    int length = snprintf(line, sizeof line,
        "X4 SELECTION_PARITY v=1 stage=identity tick=%llu slot=%u tid=%llu local=%llx controller=%llx "
        "p1=%llx p1_runtime=%016llx p2=%llx p2_runtime=%016llx\n",
        (unsigned long long)generation, slot, (unsigned long long)GetCurrentThreadId(),
        (unsigned long long)native.identity.player, (unsigned long long)native.identity.controller,
        (unsigned long long)native.players[0], (unsigned long long)native.runtimes[0],
        (unsigned long long)native.players[1], (unsigned long long)native.runtimes[1]);
    if (length <= 0 || (size_t)length >= sizeof line || write(2, line, (size_t)length) != length ||
        !selection_parity_write(generation, slot, "p1_player", &snapshot.player1) ||
        !selection_parity_write(generation, slot, "p2_player", &snapshot.player2) ||
        !selection_parity_write(generation, slot, "local_ui", &snapshot.ui)) return 0;
    selection_parity_last_generation = generation;
    return 1;
}

static int player_selection_parity_capture(uint64_t generation, unsigned slot) {
    int saved_errno = errno;
    int result = player_selection_parity_capture_inner(generation, slot);
    errno = saved_errno;
    return result;
}
#endif

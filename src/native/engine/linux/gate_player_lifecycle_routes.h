#ifndef X4_GATE_PLAYER_LIFECYCLE_ROUTES_H
#define X4_GATE_PLAYER_LIFECYCLE_ROUTES_H

/* Compare native P2 motion getters with their private routes while the P1
 * globals are restored. This experiment remains process-discard-only: the
 * joined boundary does not exclude every background or rendering reader.
 * Include after the lifecycle snapshot and isolated zone helpers. */
#include "gate_player_route_compare.h"

static route_compare_experiment lifecycle_routes_comparison;
static unsigned char lifecycle_routes_player_bank[PCB_VALUE_BYTES];

static void lifecycle_routes_read_bank(unsigned char *bank) {
    for (unsigned index = 0; index < PCB_REGION_COUNT; ++index) {
        const pcb_region *region = &pcb_regions[index];
        lifecycle_discard_read((uintptr_t)LX(region->va), bank + region->offset, region->length);
    }
}

static void lifecycle_routes_write_bank(const unsigned char *bank) {
    for (unsigned index = 0; index < PCB_REGION_COUNT; ++index) {
        const pcb_region *region = &pcb_regions[index];
        memcpy(LX(region->va), bank + region->offset, region->length);
    }
}

static void lifecycle_routes_assign_player(uintptr_t player) {
    void **destination = (void **)(factory_probe_universe + 0x238);
    void **assigned = ((void **(*)(void **, void *))LX(FACTORY_PROBE_ASSIGN_VA))(
        destination, (void *)player);
    lifecycle_discard_require(assigned == destination && factory_probe_singleton(player),
                              "routes_singleton_assign");
}

static void lifecycle_routes_require_canonical(uintptr_t zone) {
    unsigned char observed[PCB_VALUE_BYTES];
    lifecycle_routes_read_bank(observed);
    lifecycle_discard_require(!memcmp(observed, lifecycle_discard_initial_bank, sizeof(observed)) &&
        factory_probe_singleton(factory_probe_player1) &&
        lifecycle_discard_pointer((uintptr_t)LX(PLAYER_ZONE_GLOBAL_VA)) == zone,
        "routes_canonical_state_changed");
}

static void lifecycle_routes_log_samples(void) {
    static const char *names[] = {"transform", "velocity", "acceleration"};
    static const char digits[] = "0123456789abcdef";
    route_compare_experiment *comparison = &lifecycle_routes_comparison;
    for (unsigned getter = 0; getter < 3; ++getter) {
        size_t size = route_compare_output_sizes[getter];
        for (unsigned mode = 0; mode < 2; ++mode) {
            char expected[ROUTE_COMPARE_BUFFER_SIZE * 2 + 1];
            char actual[ROUTE_COMPARE_BUFFER_SIZE * 2 + 1];
            for (size_t byte = 0; byte < size; ++byte) {
                unsigned char scoped = comparison->scoped[getter][mode].data[byte];
                unsigned char offscope = comparison->offscope[getter][mode].data[byte];
                expected[byte * 2] = digits[scoped >> 4];
                expected[byte * 2 + 1] = digits[scoped & 15];
                actual[byte * 2] = digits[offscope >> 4];
                actual[byte * 2 + 1] = digits[offscope & 15];
            }
            expected[size * 2] = actual[size * 2] = 0;
            char message[768];
            snprintf(message, sizeof(message),
                "X4LAN LIFECYCLE_DISCARD routes=sample getter=%s mode=%u size=%zu "
                "expected=%s actual=%s match=%u canary=1\n", names[getter], mode, size,
                expected, actual, !(comparison->raw_mismatch_mask & (1u << (getter * 2 + mode))));
            lifecycle_discard_write(message);
        }
    }
}

static void lifecycle_discard_compare_routes(void) {
    lifecycle_discard_stage("before_routes");
    uintptr_t zone = player_zone_resident.anchor;
    lifecycle_discard_require(zone && cre_image_pinned() && factory_probe_joined_boundary() &&
        factory_probe_singleton(factory_probe_state.player2) &&
        lifecycle_discard_pointer(lifecycle_discard_ship + 0x200) == lifecycle_discard_controller &&
        lifecycle_discard_pointer((uintptr_t)LX(PLAYER_ZONE_GLOBAL_VA)) == zone,
        "routes_admission");
    double now = 0;
    lifecycle_discard_read((uintptr_t)gate_tls_time(gate_tls_self()), &now, sizeof(now));
    route_compare_experiment *comparison = &lifecycle_routes_comparison;
    lifecycle_discard_require(route_compare_prepare(comparison, (void *)lifecycle_discard_controller,
        (void *)lifecycle_discard_ship, now), "routes_prepare");
    lifecycle_discard_require(route_compare_capture_scoped(comparison), "routes_scoped_capture");
    lifecycle_discard_require(route_compare_leave(comparison), "routes_leave");
    /* Stock getters may change caches; preserve the state after the last one.
     * These bytes are still opaque experimental state, not an ownership model. */
    lifecycle_routes_read_bank(lifecycle_routes_player_bank);
    lifecycle_discard_require(pz_restore(&player_zone_resident.ledger, &lifecycle_isolation_zone_key),
                              "routes_zone_leave");
    lifecycle_routes_write_bank(lifecycle_discard_initial_bank);
    /* Retain the manual P1 hold and the factory-owned P2 hold across both
     * singleton assignments. Final lifecycle cleanup drops the P1 hold once. */
    lifecycle_routes_assign_player(factory_probe_player1);
    lifecycle_routes_require_canonical(zone);
    player_route_snapshot canonical_answers = {0};
    unsigned char canonical_ship_flag = 0;
    lifecycle_discard_require(route_compare_snapshot(comparison, &canonical_answers) &&
        route_compare_ship_flag(comparison->controller, comparison->held_ship, &canonical_ship_flag),
        "routes_canonical_answers");
    char coverage[512];
    snprintf(coverage, sizeof(coverage),
        "X4LAN LIFECYCLE_DISCARD routes=coverage selected=%s global_authority=%u ship_flag=%u "
        "canonical_authority=%u canonical_ship_flag=%u time=%.17g\n",
        comparison->snapshot.global_authority || !comparison->scoped_ship_after ? "agent" : "player",
        comparison->snapshot.global_authority, comparison->scoped_ship_after,
        canonical_answers.global_authority, canonical_ship_flag, now);
    lifecycle_discard_write(coverage);
    lifecycle_routes_require_canonical(zone);
    lifecycle_discard_snapshot("canonical_routes", 0);
    lifecycle_discard_require(route_compare_offscope(comparison), "routes_offscope_capture");
    lifecycle_routes_log_samples();
    lifecycle_discard_snapshot("canonical_routes_after", 0);
    lifecycle_routes_require_canonical(zone);
    lifecycle_discard_require(!comparison->raw_mismatch_mask && !comparison->scalar_mismatch_mask &&
        !atomic_load_explicit(&comparison->routes.access_failures, memory_order_relaxed),
        "routes_output_mismatch");
    lifecycle_discard_write("X4LAN LIFECYCLE_DISCARD routes=pass samples=6 "
                            "bank_unchanged=1 singleton_canonical=1\n");

    lifecycle_discard_require(player_zone_resident.slot_owned == 1 &&
        (uintptr_t)player_zone_resident.slot_reference == zone &&
        pz_enter(&player_zone_resident.ledger, &lifecycle_isolation_zone_key) == PCB_ZONE_ACTIVE,
        "routes_zone_reenter");
    lifecycle_routes_assign_player(factory_probe_state.player2);
    lifecycle_routes_write_bank(lifecycle_routes_player_bank);
    unsigned char restored_bank[PCB_VALUE_BYTES];
    lifecycle_routes_read_bank(restored_bank);
    lifecycle_discard_require(lifecycle_discard_pointer(lifecycle_discard_ship + 0x200) ==
        lifecycle_discard_controller && lifecycle_discard_pointer((uintptr_t)LX(PLAYER_ZONE_GLOBAL_VA)) == zone &&
        route_compare_detach_for_discard(comparison), "routes_return_or_detach");
    lifecycle_discard_require(!memcmp(restored_bank, lifecycle_routes_player_bank, sizeof(restored_bank)) &&
        factory_probe_singleton(factory_probe_state.player2) &&
        lifecycle_discard_pointer(lifecycle_discard_controller + 0x20) == lifecycle_discard_ship &&
        !atomic_load_explicit(&comparison->routes.access_failures, memory_order_relaxed),
        "routes_return_readback");
    lifecycle_discard_stage("after_routes");
    lifecycle_discard_snapshot("routes_returned", 0);
}
#endif

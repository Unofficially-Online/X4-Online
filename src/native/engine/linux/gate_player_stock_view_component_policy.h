#ifndef X4_GATE_PLAYER_STOCK_VIEW_COMPONENT_POLICY_H
#define X4_GATE_PLAYER_STOCK_VIEW_COMPONENT_POLICY_H

/* Include after gate_player_stock_view_fov.h. The coordinator must bind only
 * after the initial transaction commits: ship identities are then immutable,
 * and the provider's release/acquire binding publishes them to foreign threads.
 * This header does not bind or activate a hook. */
#include "player_stock_view_component_policy.h"
#include "player_stock_view_control_proof.h"

static int stock_view_component_gate_read(void *context, uintptr_t address, void *out, unsigned size) {
    (void)context;
    return read_local((void *)address, out, size);
}

static int stock_view_component_registry_read(void *context, uintptr_t address, void *out, size_t size) {
    (void)context;
    return read_local((void *)address, out, size);
}

static int stock_view_component_gate_foreign(void *context, uintptr_t component,
                                             uint64_t component_runtime, uintptr_t parent) {
    (void)context;
    struct { uintptr_t class_pointer; uint64_t runtime; } ship, after;
    if (!parent || parent > UINTPTR_MAX - sizeof ship ||
        !read_local((void *)parent, &ship, sizeof ship) ||
        ship.class_pointer != (uintptr_t)LX(SEAT_PREFLIGHT_SHIP_VTABLE) || !ship.runtime) return 0;
    const player_stock_view_control_ops registry = {
        .read = stock_view_component_registry_read,
        .registry_global = (uintptr_t)LX(0x3daa860),
    };
    return stock_view_control_registry(&registry, component, component_runtime) &&
        stock_view_control_registry(&registry, parent, ship.runtime) &&
        read_local((void *)parent, &after, sizeof after) &&
        ship.class_pointer == after.class_pointer && ship.runtime == after.runtime;
}

static int stock_view_component_gate_authenticate(void *context, unsigned expected_slot, uint64_t *generation) {
    (void)context;
    unsigned actual_slot = 0;
    if (!generation || expected_slot < 1 || expected_slot > 2 ||
        !stock_view_gate_owner(stock_view_gate_controllers[expected_slot - 1], &actual_slot) ||
        factory_probe_identity.ship != stock_view_gate_ships[0] ||
        lifecycle_discard_ship != stock_view_gate_ships[1] || actual_slot != expected_slot) return 0;
    *generation = stats.issued;
    return 1;
}

static player_stock_view_component_context stock_view_component_gate_context(void) {
    const player_stock_view_component_context context = {
        .read = stock_view_component_gate_read,
        .authenticate = stock_view_component_gate_authenticate,
        .foreign_identity = stock_view_component_gate_foreign,
        .ships = {stock_view_gate_ships[0], stock_view_gate_ships[1]},
        .classes = {(uintptr_t)LX(0x3c6a440), (uintptr_t)LX(0x3ccad80)},
        .state = &stock_view_gate_state,
    };
    return context;
}

static int stock_view_component_gate_policy(unsigned site, uintptr_t component,
                                             unsigned char original, unsigned char *result) {
    player_stock_view_component_context context = stock_view_component_gate_context();
    return player_stock_view_component_route(&context, site, component, original, result);
}

static int stock_view_precompare_gate_policy(uintptr_t component, uintptr_t ancestor,
                                              uintptr_t original, uintptr_t *result) {
    player_stock_view_component_context context = stock_view_component_gate_context();
    return player_stock_view_precompare_route(&context, component, ancestor, original, result);
}
#endif

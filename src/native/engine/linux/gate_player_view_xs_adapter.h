#ifndef X4_GATE_PLAYER_VIEW_XS_ADAPTER_H
#define X4_GATE_PLAYER_VIEW_XS_ADAPTER_H
/* Included after gate_mining_camera.h. Each native transform uses the same
 * authenticated worker tick and an exact, registry-validated sector path. */
#include "player_view_xs_policy.h"

typedef struct {
    uintptr_t object;
    uintptr_t vtable;
    uintptr_t parent;
    uint64_t runtime;
} player_view_xs_path_node;
typedef struct {
    player_view_xs_path_node nodes[17];
    unsigned count;
    uintptr_t sector;
} player_view_xs_path;

static int player_view_xs_gate_registry(uintptr_t object, uint64_t runtime) {
    const player_stock_view_control_ops registry = {
        .read = stock_view_component_registry_read,
        .registry_global = (uintptr_t)LX(0x3daa860),
    };
    return stock_view_control_registry(&registry, object, runtime);
}
static int player_view_xs_gate_node(uintptr_t object, player_view_xs_path_node *node) {
    if (!object || object > UINTPTR_MAX - 0x78 || !node ||
        !read_local((void *)object, &node->vtable, 8) ||
        !read_local((void *)(object + 8), &node->runtime, 8) ||
        !read_local((void *)(object + 0x70), &node->parent, 8) || !node->runtime ||
        !player_view_xs_gate_registry(object, node->runtime))
        return 0;
    unsigned known = 0;
    for (unsigned i = 0; i < LXW_CLASS_COUNT; ++i) {
        if (!lxw_classes[i].family && !lxw_classes[i].top &&
            node->vtable == (uintptr_t)LX(lxw_classes[i].linux_vtable)) {
            known = 1;
            break;
        }
    }
    if (!known)
        return 0;
    node->object = object;
    return 1;
}
static int player_view_xs_gate_path(uintptr_t object, player_view_xs_path *path) {
    if (!path)
        return 0;
    memset(path, 0, sizeof *path);
    while (object && path->count < 17) {
        for (unsigned i = 0; i < path->count; ++i)
            if (path->nodes[i].object == object)
                return 0;
        player_view_xs_path_node *node = &path->nodes[path->count];
        if (!player_view_xs_gate_node(object, node))
            return 0;
        ++path->count;
        if (node->vtable == (uintptr_t)LX(0x3d1d850)) {
            path->sector = object;
            return 1;
        }
        object = node->parent;
    }
    return 0;
}
static int player_view_xs_gate_path_same(const player_view_xs_path *path) {
    for (unsigned i = 0; i < path->count; ++i) {
        player_view_xs_path_node current = {0};
        if (!player_view_xs_gate_node(path->nodes[i].object, &current) ||
            memcmp(&current, &path->nodes[i], sizeof current))
            return 0;
    }
    return 1;
}
static int player_view_xs_gate_position(uintptr_t object, uintptr_t *sector, float position[3]) {
    player_view_xs_path path;
    if (!sector || !position || !player_view_xs_gate_path(object, &path))
        return 0;
    unsigned char *tls = gate_tls_self();
    double now = 0, after = 0;
    if (!tls || !gate_tls_worker(tls) || !gate_tls_time(tls) ||
        !read_local(gate_tls_time(tls), &now, sizeof now) || !isfinite(now))
        return 0;
    float matrix[16] __attribute__((aligned(16)));
    lxmc_gtr((void *)object, matrix, now, (void *)path.sector);
    if (gate_tls_self() != tls || !read_local(gate_tls_time(tls), &after, sizeof after) ||
        memcmp(&now, &after, sizeof now) || !player_view_xs_gate_path_same(&path) ||
        !isfinite(matrix[0]) || !isfinite(matrix[1]) || !isfinite(matrix[2]))
        return 0;
    *sector = path.sector;
    memcpy(position, matrix, 3 * sizeof(float));
    return 1;
}
static uint64_t player_view_xs_gate_tid(void *unused) {
    (void)unused;
    return (uint64_t)syscall(SYS_gettid);
}
static int player_view_xs_gate_snapshot(void *unused, uint64_t generation, unsigned positions,
                                        player_view_xs_snapshot *out) {
    (void)unused;
    if (!out || generation != stats.issued ||
        generation != player_recurring_tail_token.generation || !world_identity_sealed ||
        !world_identities)
        return 0;
    player_view_xs_snapshot result = {.generation = generation};
    for (unsigned i = 0; i < 2; ++i) {
        unsigned slot = 0;
        player_stock_view view;
        uintptr_t ship = stock_view_gate_ships[i];
        player_view_xs_path_node identity = {0}, after = {0};
        if (!stock_view_gate_owner(stock_view_gate_controllers[i], &slot) || slot != i + 1 ||
            factory_probe_identity.ship != stock_view_gate_ships[0] ||
            lifecycle_discard_ship != stock_view_gate_ships[1] ||
            !player_stock_view_read(&stock_view_gate_state, generation, slot,
                                    stock_view_gate_state.saved[i], &view) ||
            !player_view_xs_gate_node(ship, &identity) ||
            identity.vtable != (uintptr_t)LX(SEAT_PREFLIGHT_SHIP_VTABLE) ||
            world_identity_portable(identity.runtime) != stock_view_gate_state.saved[i])
            return 0;
        result.slots[i].ship = ship;
        result.slots[i].target = view.target;
        result.slots[i].external = !view.cockpit;
        if (positions && result.slots[i].external &&
            !player_view_xs_gate_position(ship, &result.slots[i].sector, result.slots[i].position))
            return 0;
        if (!player_view_xs_gate_node(ship, &after) || memcmp(&identity, &after, sizeof identity) ||
            world_identity_portable(after.runtime) != stock_view_gate_state.saved[i] ||
            !stock_view_gate_owner(stock_view_gate_controllers[i], &slot) || slot != i + 1)
            return 0;
    }
    if (generation != stats.issued || generation != player_recurring_tail_token.generation)
        return 0;
    *out = result;
    return 1;
}
static int player_view_xs_gate_candidate(void *unused, uintptr_t object, unsigned position,
                                         player_view_xs_candidate *out) {
    (void)unused;
    if (!out)
        return -1;
    player_view_xs_path_node before = {0}, after = {0};
    if (!player_view_xs_gate_node(object, &before))
        return -1;
    if (before.vtable != (uintptr_t)LX(0x3d282a8))
        return 0;
    uint64_t portable = world_identity_portable(before.runtime);
    player_view_xs_candidate candidate = {.portable = portable};
    if (portable <= 1 ||
        (position &&
         !player_view_xs_gate_position(object, &candidate.sector, candidate.position)) ||
        !player_view_xs_gate_node(object, &after) || memcmp(&before, &after, sizeof before) ||
        world_identity_portable(after.runtime) != portable)
        return -1;
    *out = candidate;
    return 1;
}
static player_view_xs_ops player_view_xs_gate_ops(void) {
    return (player_view_xs_ops){.enabled = 1,
                                .owner_tid = stock_view_gate_tid,
                                .thread_id = player_view_xs_gate_tid,
                                .snapshot = player_view_xs_gate_snapshot,
                                .candidate = player_view_xs_gate_candidate};
}
/* The provider bind publishes stock_view_gate_tid before any policy runs.
 * Only its owner may inspect the tail token or mutable coordinator state.
 * No token means this owner call is outside the admitted simulation tail.
 * A partial or stale token claims simulation and must fail closed. Unknown
 * foreign threads remain refused until a presentation scope is proven. */
static int player_view_xs_gate_scope(void) {
    if ((uint64_t)syscall(SYS_gettid) != stock_view_gate_tid)
        return -1;
    fwrr_token token = player_recurring_tail_token;
    if (!token.owner && !token.generation && !token.worker_thread)
        return 0;
    return player_recurring_physics_entered() ? 1 : -1;
}
/* Target result: 1 uses *out, 2 calls the stock getter, 0 refuses.
 * The active XS case must not invoke the mutating local target resolver. */
static int player_view_xs_gate_target(uintptr_t ship, uintptr_t unused, uintptr_t *out) {
    (void)unused;
    int scope = player_view_xs_gate_scope();
    if (!out || scope < 0)
        return 0;
    if (!scope)
        return 2;
    player_view_xs_candidate candidate = {0};
    int kind = player_view_xs_gate_candidate(NULL, ship, 0, &candidate);
    if (kind < 0 || kind > 1)
        return 0;
    if (!kind)
        return 2;
    player_view_xs_ops ops = player_view_xs_gate_ops();
    return player_view_xs_route_target(&ops, stats.issued, ship, 0, out);
}
static int player_view_xs_gate_external(uint64_t original, uint64_t *out) {
    int scope = player_view_xs_gate_scope();
    if (!out || scope < 0)
        return 0;
    if (!scope) {
        *out = original;
        return 1;
    }
    player_view_xs_ops ops = player_view_xs_gate_ops();
    return player_view_xs_route_external(&ops, stats.issued, original, out);
}
static int player_view_xs_gate_distance(uintptr_t ship, uintptr_t camera, unsigned flags,
                                        float original, float *out) {
    int scope = player_view_xs_gate_scope();
    if (!out || scope < 0)
        return 0;
    if (!scope) {
        *out = original;
        return 1;
    }
    if (!camera || flags != 1)
        return 0;
    player_view_xs_ops ops = player_view_xs_gate_ops();
    return player_view_xs_route_distance(&ops, stats.issued, ship, original, out);
}
#endif

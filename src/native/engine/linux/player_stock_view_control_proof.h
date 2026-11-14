#ifndef X4_PLAYER_STOCK_VIEW_CONTROL_PROOF_H
#define X4_PLAYER_STOCK_VIEW_CONTROL_PROOF_H
/* Read-only proof for the pinned Linux Player::controlled path, limited to
 * the observed P1/P2 UI overlay graph. No registry resolver or virtual method
 * is called. Repeated reads detect drift, not ABA or lifetime exclusion. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STOCK_VIEW_CONTROL_VECTOR_MAX 8192u

typedef struct {
    void *context;
    int (*authenticate)(void *);
    int (*read)(void *, uintptr_t, void *, size_t);
    uintptr_t registry_global;
    uintptr_t player1, player2, room, ship;
    uint64_t player1_runtime, player2_runtime, room_runtime, ship_runtime;
    uintptr_t player_vtable, cockpit_vtable, control_room_vtable, ship_vtable;
} player_stock_view_control_ops;
typedef struct { uintptr_t begin, end, capacity; } player_stock_view_control_vector;
typedef struct {
    uintptr_t vtable, parent;
    uint64_t runtime;
    player_stock_view_control_vector vector;
} player_stock_view_control_player;

static inline int stock_view_control_read(const player_stock_view_control_ops *ops, uintptr_t base,
                                           size_t offset, void *out, size_t size) {
    if (!base || offset > UINTPTR_MAX - base || size > UINTPTR_MAX - (base + offset)) return 0;
    return ops->read(ops->context, base + offset, out, size) == 1;
}
/* 0xa45060 / 0x14bcf00 registry layout, but without the latter's invalidation
 * write: validate live flags, 16-bit generation and object/runtime readback. */
static inline int stock_view_control_registry(const player_stock_view_control_ops *ops,
                                               uintptr_t object, uint64_t runtime) {
    uint32_t low = (uint32_t)runtime & 0x1ffffffu;
    if (!object || !runtime || !low) return 0;
    uint32_t index = low - 1u, block_index = index >> 20, slot = (index & 0xfffffu) % 3u;
    uint32_t group_index = (index & 0xfffffu) / 3u;
    uintptr_t registry = 0, registry_after = 0, block = 0, block_after = 0, entry = 0;
    unsigned char group[32], after[32]; uint64_t object_id = 0, object_id_after = 0;
    if (!stock_view_control_read(ops, ops->registry_global, 0, &registry, 8) || !registry ||
        !stock_view_control_read(ops, registry, 0x60u + (size_t)block_index * 8u, &block, 8) || !block ||
        !stock_view_control_read(ops, block, (size_t)group_index * 32u, group, sizeof group)) return 0;
    memcpy(&entry, group + slot * 8u, 8);
    uint32_t flags = 0, generations = 0;
    memcpy(&flags, group + 28, 4);
    memcpy(&generations, group + 24u + (slot / 2u) * 4u, 4);
    unsigned nibble = (flags >> (16u + slot * 4u)) & 0xfu;
    unsigned generation = (generations >> ((slot & 1u) * 16u)) & 0xffffu;
    if ((nibble & 5u) != 5u || generation != ((runtime >> 25) & 0xffffu) || entry != object ||
        !stock_view_control_read(ops, object, 8, &object_id, 8) || object_id != runtime ||
        !stock_view_control_read(ops, block, (size_t)group_index * 32u, after, sizeof after) ||
        !stock_view_control_read(ops, registry, 0x60u + (size_t)block_index * 8u, &block_after, 8) ||
        !stock_view_control_read(ops, ops->registry_global, 0, &registry_after, 8) ||
        !stock_view_control_read(ops, object, 8, &object_id_after, 8)) return 0;
    return registry == registry_after && block == block_after && object_id == object_id_after &&
        !memcmp(group, after, sizeof group);
}
static inline int stock_view_control_player_read(const player_stock_view_control_ops *ops, uintptr_t player,
                                                  player_stock_view_control_player *out) {
    return stock_view_control_read(ops, player, 0, &out->vtable, 8) &&
        stock_view_control_read(ops, player, 8, &out->runtime, 8) &&
        stock_view_control_read(ops, player, 0x70, &out->parent, 8) &&
        stock_view_control_read(ops, player, 0x308, &out->vector, sizeof out->vector);
}
static inline int player_stock_view_control_equivalent(const player_stock_view_control_ops *ops) {
    if (!ops || !ops->authenticate || !ops->read || !ops->registry_global ||
        !ops->player1 || !ops->player2 || ops->player1 == ops->player2 || !ops->room || !ops->ship ||
        !ops->player1_runtime || !ops->player2_runtime || !ops->room_runtime || !ops->ship_runtime ||
        !ops->player_vtable || !ops->cockpit_vtable || !ops->control_room_vtable || !ops->ship_vtable ||
        ops->authenticate(ops->context) != 1) return 0;
    player_stock_view_control_player a = {0}, b = {0}, a_after = {0}, b_after = {0};
    uintptr_t room_class = 0, room_parent = 0, ship_class = 0, room_class_after = 0, room_parent_after = 0, ship_class_after = 0;
    uint32_t room_gate = 1, ship_gate = 1, room_gate_after = 1, ship_gate_after = 1;
    if (!stock_view_control_player_read(ops, ops->player1, &a) ||
        !stock_view_control_player_read(ops, ops->player2, &b) ||
        a.vtable != ops->player_vtable || b.vtable != ops->player_vtable ||
        a.runtime != ops->player1_runtime || b.runtime != ops->player2_runtime ||
        a.parent != ops->room || b.parent != ops->room || memcmp(&a.vector, &b.vector, sizeof a.vector) ||
        !a.vector.begin || (a.vector.begin | a.vector.end | a.vector.capacity) & 7u ||
        a.vector.begin >= a.vector.end || a.vector.end > a.vector.capacity ||
        a.vector.end - a.vector.begin > STOCK_VIEW_CONTROL_VECTOR_MAX ||
        !stock_view_control_read(ops, ops->room, 0, &room_class, 8) ||
        (room_class != ops->cockpit_vtable && room_class != ops->control_room_vtable) ||
        !stock_view_control_read(ops, ops->room, 0x68, &room_gate, 4) || room_gate ||
        !stock_view_control_read(ops, ops->room, 0x70, &room_parent, 8) || room_parent != ops->ship ||
        !stock_view_control_read(ops, ops->ship, 0, &ship_class, 8) || ship_class != ops->ship_vtable ||
        !stock_view_control_read(ops, ops->ship, 0x68, &ship_gate, 4) || ship_gate) return 0;
    uintptr_t first = 0, first_at = 0;
    for (uintptr_t at = a.vector.begin; at < a.vector.end; at += 8) {
        uintptr_t value = 0, again = 0;
        if (!stock_view_control_read(ops, at, 0, &value, 8) ||
            !stock_view_control_read(ops, at, 0, &again, 8) || value != again) return 0;
        if (value) { first = value; first_at = at; break; }
    }
    if (first != ops->ship ||
        !stock_view_control_registry(ops, ops->player1, ops->player1_runtime) ||
        !stock_view_control_registry(ops, ops->player2, ops->player2_runtime) ||
        !stock_view_control_registry(ops, ops->room, ops->room_runtime) ||
        !stock_view_control_registry(ops, ops->ship, ops->ship_runtime) ||
        !stock_view_control_player_read(ops, ops->player1, &a_after) ||
        !stock_view_control_player_read(ops, ops->player2, &b_after) ||
        memcmp(&a, &a_after, sizeof a) || memcmp(&b, &b_after, sizeof b) ||
        !stock_view_control_read(ops, ops->room, 0, &room_class_after, 8) || room_class != room_class_after ||
        !stock_view_control_read(ops, ops->room, 0x68, &room_gate_after, 4) || room_gate != room_gate_after ||
        !stock_view_control_read(ops, ops->room, 0x70, &room_parent_after, 8) || room_parent != room_parent_after ||
        !stock_view_control_read(ops, ops->ship, 0, &ship_class_after, 8) || ship_class != ship_class_after ||
        !stock_view_control_read(ops, ops->ship, 0x68, &ship_gate_after, 4) || ship_gate != ship_gate_after ||
        ops->authenticate(ops->context) != 1) return 0;
    /* Recheck the entire decisive prefix after registry and identity reads. */
    for (uintptr_t at = a.vector.begin; at <= first_at; at += 8) {
        uintptr_t value = 0;
        if (!stock_view_control_read(ops, at, 0, &value, 8) ||
            value != (at == first_at ? ops->ship : 0)) return 0;
    }
    return ops->authenticate(ops->context) == 1;
}
#endif

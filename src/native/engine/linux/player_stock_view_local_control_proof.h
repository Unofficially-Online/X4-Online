#ifndef X4_PLAYER_STOCK_VIEW_LOCAL_CONTROL_PROOF_H
#define X4_PLAYER_STOCK_VIEW_LOCAL_CONTROL_PROOF_H
#include "player_stock_view_control_proof.h"

/* Single-player version of the pinned Player controlled-object equivalence.
 * It does not call invalidating smart-reference resolvers or virtual methods.
 * The caller authenticates current P1 presentation and vtable/code pins. */
static inline int player_stock_view_local_control_equivalent(const player_stock_view_control_ops *ops) {
    if (!ops || !ops->authenticate || !ops->read || !ops->registry_global || !ops->player1 ||
        !ops->room || !ops->ship || !ops->player1_runtime || !ops->room_runtime || !ops->ship_runtime ||
        !ops->player_vtable || !ops->cockpit_vtable || !ops->control_room_vtable || !ops->ship_vtable ||
        ops->authenticate(ops->context) != 1) {
        return 0;
    }
    player_stock_view_control_player player = {0};
    player_stock_view_control_player after = {0};
    uintptr_t room_class = 0;
    uintptr_t room_parent = 0;
    uintptr_t ship_class = 0;
    uint32_t room_gate = 1;
    uint32_t ship_gate = 1;
    if (!stock_view_control_player_read(ops, ops->player1, &player) ||
        player.vtable != ops->player_vtable || player.runtime != ops->player1_runtime || player.parent != ops->room ||
        !player.vector.begin || (player.vector.begin | player.vector.end | player.vector.capacity) & 7u ||
        player.vector.begin >= player.vector.end || player.vector.end > player.vector.capacity ||
        player.vector.end - player.vector.begin > STOCK_VIEW_CONTROL_VECTOR_MAX ||
        !stock_view_control_read(ops, ops->room, 0, &room_class, 8) ||
        (room_class != ops->cockpit_vtable && room_class != ops->control_room_vtable) ||
        !stock_view_control_read(ops, ops->room, 0x68, &room_gate, 4) || room_gate ||
        !stock_view_control_read(ops, ops->room, 0x70, &room_parent, 8) || room_parent != ops->ship ||
        !stock_view_control_read(ops, ops->ship, 0, &ship_class, 8) || ship_class != ops->ship_vtable ||
        !stock_view_control_read(ops, ops->ship, 0x68, &ship_gate, 4) || ship_gate) {
        return 0;
    }
    uintptr_t first_at = 0;
    for (uintptr_t at = player.vector.begin; at < player.vector.end; at += 8) {
        uintptr_t value = 0;
        if (!stock_view_control_read(ops, at, 0, &value, 8)) return 0;
        if (value) {
            if (value != ops->ship) return 0;
            first_at = at;
            break;
        }
    }
    if (!first_at || !stock_view_control_registry(ops, ops->player1, ops->player1_runtime) ||
        !stock_view_control_registry(ops, ops->room, ops->room_runtime) ||
        !stock_view_control_registry(ops, ops->ship, ops->ship_runtime) ||
        !stock_view_control_player_read(ops, ops->player1, &after) || memcmp(&player, &after, sizeof player)) {
        return 0;
    }
    uintptr_t value = 0;
    uint32_t gate = 1;
    if (!stock_view_control_read(ops, ops->room, 0, &value, 8) || value != room_class ||
        !stock_view_control_read(ops, ops->room, 0x68, &gate, 4) || gate != room_gate ||
        !stock_view_control_read(ops, ops->room, 0x70, &value, 8) || value != room_parent ||
        !stock_view_control_read(ops, ops->ship, 0, &value, 8) || value != ship_class ||
        !stock_view_control_read(ops, ops->ship, 0x68, &gate, 4) || gate != ship_gate) {
        return 0;
    }
    for (uintptr_t at = player.vector.begin; at <= first_at; at += 8) {
        if (!stock_view_control_read(ops, at, 0, &value, 8) || value != (at == first_at ? ops->ship : 0)) {
            return 0;
        }
    }
    return ops->authenticate(ops->context) == 1;
}
#endif

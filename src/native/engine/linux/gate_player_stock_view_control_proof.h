#ifndef X4_GATE_PLAYER_STOCK_VIEW_CONTROL_PROOF_H
#define X4_GATE_PLAYER_STOCK_VIEW_CONTROL_PROOF_H
#include "player_stock_view_control_proof.h"
#include "gate_player_stock_view_ui_selection.h"
#include "player_stock_view_capture_handoff.h"

/* Concrete read-only control proof for the already authenticated guest UI
 * Move lease. This does not provide lease coverage outside that callback. */
typedef struct { const player_stock_view_capture_identity *identity; } stock_view_control_native;
static inline int stock_view_control_native_auth(void *context) {
    const stock_view_control_native *native = context;
    return native && stock_view_ui_lease_auth(NULL, native->identity);
}
static inline int stock_view_control_native_read(void *context, uintptr_t at, void *out, size_t size) {
    (void)context; return read_local((void *)at, out, size);
}
static inline int stock_view_control_native_pins(void) {
    static const lx_site sites[] = {
        {"control-player-method", 0x136c820, 68, "38cb2d7e1fe206c4a214526bcfd0d4274ce8237d350f9711334de76d950a578c"},
        {"control-cockpit-class", 0xa39930, 48, "8bf9e521822333a1f5b82954f8081829f44d80b19b641fd7e96a19ed85b416f9"},
        {"control-room-class", 0xa398e0, 48, "2074c11d6aa602fe94e95bf83d1f851227d00088bcefb4850d328bc7041dc429"},
        {"control-ship-class", 0xa389f0, 48, "6cc846478894fc1636f1416467c973ff24c09d48b3207c8f8b4fa37d6bfd6e42"}
    };
    for (unsigned i = 0; i < sizeof sites / sizeof sites[0]; ++i) if (!site_ok(&sites[i])) return 0;
    static const struct { uintptr_t slot, expected; } slots[] = {
        {0x3b5f7a0u + 0x1ce0u, 0x136c820u}, {0x3ca5fc0u + 0x11f0u, 0xa39930u},
        {0x3cb4e88u + 0x11f0u, 0xa398e0u}, {0x3d21748u + 0x11f0u, 0xa389f0u}
    };
    for (unsigned i = 0; i < sizeof slots / sizeof slots[0]; ++i) {
        uintptr_t value = 0;
        if (!read_local(LX(slots[i].slot), &value, 8) || value != (uintptr_t)LX(slots[i].expected)) return 0;
    }
    uint32_t cockpit = 1, room = 1, ship = 0;
    return read_local(LX(0x2c81ce0u + 28u), &cockpit, 4) && cockpit == 0 &&
        read_local(LX(0x2c81d00u + 28u), &room, 4) && room == 0 &&
        read_local(LX(0x2c822a0u + 28u), &ship, 4) && ship == 0x401;
}
static inline int stock_view_control_native_proof(void *unused, const player_stock_view_capture_identity *identity) {
    (void)unused;
    stock_view_control_native native = {identity};
    const player_target_hold_snapshot *hold = &player_target_hold_resident.identity;
    if (!stock_view_control_native_auth(&native) || !stock_view_control_native_pins()) return 0;
    player_stock_view_control_ops ops = {
        .context = &native, .authenticate = stock_view_control_native_auth, .read = stock_view_control_native_read,
        .registry_global = (uintptr_t)LX(0x3daa860u),
        .player1 = presentation_fork.request.player1, .player2 = presentation_fork.request.player2,
        .room = presentation_fork.request.room, .ship = player_target_hold_resident.target_reference,
        .player1_runtime = factory_probe_identity.player_runtime, .player2_runtime = factory_probe_player2_runtime,
        .room_runtime = hold->room_runtime, .ship_runtime = hold->target_runtime,
        .player_vtable = (uintptr_t)LX(0x3b5f7a0u), .cockpit_vtable = (uintptr_t)LX(0x3ca5fc0u),
        .control_room_vtable = (uintptr_t)LX(0x3cb4e88u), .ship_vtable = (uintptr_t)LX(0x3d21748u)
    };
    return player_stock_view_control_equivalent(&ops) && stock_view_control_native_pins() &&
        stock_view_control_native_auth(&native);
}
static inline int player_stock_view_ui_verified_capture(const player_stock_view_capture_identity *identity,
                                                          player_stock_view_capture_sample *out) {
    const player_stock_view_ui_control_proof proof = {NULL, stock_view_control_native_proof};
    return player_stock_view_ui_capture_native(identity, &proof, out);
}
static inline int player_stock_view_ui_verified_capture_observed(const player_stock_view_capture_identity *identity,
        uintptr_t camera, float fov, player_stock_view_capture_sample *out) {
    const player_stock_view_ui_control_proof proof = {NULL, stock_view_control_native_proof};
    return player_stock_view_ui_capture_observed(identity, &proof, camera, fov, out);
}
static inline int stock_view_guest_capture_observed(void *unused,
        const player_stock_view_capture_identity *identity, uintptr_t camera, float fov,
        player_stock_view_capture_sample *out) {
    (void)unused;
    return player_stock_view_ui_verified_capture_observed(identity, camera, fov, out);
}
/* Call only within the authenticated guest UI lease; the capture and its
 * completed FOV value are copied to the mailbox before that lease ends. */
static inline int player_stock_view_guest_stage(player_stock_view_mailbox *mailbox,
        player_view_render_fov_sample *render, const player_stock_view_capture_identity *identity) {
    uintptr_t camera = 0;
    if (!identity || !stock_view_ui_lease_auth(NULL, identity) ||
        identity->universe > UINTPTR_MAX - 0x3f0 ||
        !read_local((void *)(identity->universe + 0x3e8), &camera, sizeof camera) || !camera) return 0;
    return player_stock_view_capture_handoff(mailbox, render, identity, camera,
                                            stock_view_guest_capture_observed, NULL);
}
#endif

#ifndef X4_GATE_PLAYER_STOCK_VIEW_HOST_CAPTURE_H
#define X4_GATE_PLAYER_STOCK_VIEW_HOST_CAPTURE_H
#include "player_stock_view_local_control_proof.h"
#include "gate_player_stock_view_control_proof.h"

/* P1 is locally presented only on slot 1. The sealed keyboard binding is a
 * comparison anchor; all identity fields are freshly read in this epoch. */
typedef struct {
    player_stock_view_capture_identity identity;
    player_stock_view_ui_selection selection;
    uintptr_t camera;
    float fov;
} player_stock_view_host_capture;

static inline int stock_view_host_auth(void *context, const player_stock_view_capture_identity *identity) {
    (void)context;
    uintptr_t player = 0;
    uintptr_t ship = 0;
    uintptr_t controller = 0;
    return identity && stats.slot == 1 && identity->local_slot == 1 && keyboard_probe_worker() &&
        stock_observer_worker_boundary() && !player_routes_scope && player_recurring_physics_entered() &&
        identity->generation == stats.issued && identity->generation == player_recurring_tail_token.generation &&
        identity->universe && identity->universe == keyboard_probe_binding.universe &&
        identity->universe <= UINTPTR_MAX - 0x240 && identity->player && identity->player <= UINTPTR_MAX - 0x78 &&
        identity->player == keyboard_probe_binding.player && identity->player == factory_probe_player1 &&
        identity->controller == keyboard_probe_binding.controller && identity->controller &&
        identity->controller <= UINTPTR_MAX - 0x28 && keyboard_probe_binding.ship &&
        keyboard_probe_binding.ship <= UINTPTR_MAX - 0x208 &&
        read_local((void *)(identity->universe + 0x238), &player, 8) && player == identity->player &&
        read_local((void *)(identity->controller + 0x20), &ship, 8) && ship == keyboard_probe_binding.ship &&
        read_local((void *)(ship + 0x200), &controller, 8) && controller == identity->controller;
}

static inline int stock_view_host_control_auth(void *context) {
    const player_stock_view_host_capture *capture = context;
    return capture && stock_view_host_auth(NULL, &capture->identity);
}

static inline int stock_view_host_control_proof(player_stock_view_host_capture *capture) {
    uintptr_t room = 0;
    uint64_t room_runtime = 0;
    if (!stock_view_host_control_auth(capture) || !stock_view_control_native_pins() ||
        !read_local((void *)(capture->identity.player + 0x70), &room, 8) || !room || room > UINTPTR_MAX - 16 ||
        !read_local((void *)(room + 8), &room_runtime, 8) || !room_runtime) {
        return 0;
    }
    player_stock_view_control_ops ops = {
        .context = capture, .authenticate = stock_view_host_control_auth, .read = stock_view_control_native_read,
        .registry_global = (uintptr_t)LX(0x3daa860u), .player1 = capture->identity.player,
        .room = room, .ship = keyboard_probe_binding.ship,
        .player1_runtime = keyboard_probe_binding.player_runtime, .room_runtime = room_runtime,
        .ship_runtime = keyboard_probe_binding.runtime, .player_vtable = (uintptr_t)LX(0x3b5f7a0u),
        .cockpit_vtable = (uintptr_t)LX(0x3ca5fc0u), .control_room_vtable = (uintptr_t)LX(0x3cb4e88u),
        .ship_vtable = (uintptr_t)LX(0x3d21748u)
    };
    return player_stock_view_local_control_equivalent(&ops) && stock_view_control_native_pins() &&
        stock_view_host_control_auth(capture);
}

static inline int stock_view_host_floating(void *context, unsigned *out) {
    const player_stock_view_host_capture *capture = context;
    uintptr_t camera = 0;
    uintptr_t actor = 0;
    uint32_t mode = 0;
    if (!capture || !out || !read_local((void *)(capture->identity.universe + 0x3e8), &camera, 8)) return 0;
    if (!camera) {
        *out = 0;
        return 1;
    }
    if (camera > UINTPTR_MAX - 0x884 || !read_local((void *)(camera + 0x780), &actor, 8) ||
        !read_local((void *)(camera + 0x880), &mode, 4)) {
        return 0;
    }
    *out = actor && mode && actor != keyboard_probe_binding.ship;
    return 1;
}

static inline int stock_view_host_fov(void *context, uintptr_t camera, float *out) {
    const player_stock_view_host_capture *capture = context;
    if (!capture || !out || capture->camera != camera || !isfinite(capture->fov)) return 0;
    *out = capture->fov;
    return 1;
}

static inline int stock_view_host_target(void *context, const player_stock_view_capture_identity *identity,
                                         uint64_t *runtime) {
    player_stock_view_host_capture *capture = context;
    return capture && player_stock_view_ui_target(&capture->selection, identity, runtime);
}

static inline int player_stock_view_host_capture_observed(void *unused,
        const player_stock_view_capture_identity *identity, uintptr_t camera, float fov,
        player_stock_view_capture_sample *out) {
    (void)unused;
    static const lx_site pins[] = {
        {"ui-softtarget", 0xa45060, 281, "e9732073f43bd1ca97f5316e64fb3eae300143026dd74cf5456d1230d384a06a"},
        {"ui-controlled-current", 0x1dd1940, 51, "870ae58c2b4ea03a44f9a17ff0589a0885f8e0932d1ca6bce3ba3d60d3dcf594"},
        {"ui-controlled-player", 0x1699f20, 0x6b, "875a39c3fadc0608768592821830bf99016fb500cdc452f788f7e69fa2091196"},
        {"ui-parent-walk", 0x189d8f0, 0x37, "7c5c18f7f341ce22b68eefdc5479b7e8c7e4d55fa0d259ed3e708edd44b951db"},
        {"ui-floating", 0x1d6b1c0, 99, "28662e2439a706b8794ec0c9dbe9b2b31aef33695461446cfbba6466a58967f6"}
    };
    if (!identity || !out || !camera || !isfinite(fov)) return 0;
    for (unsigned index = 0; index < sizeof pins / sizeof pins[0]; ++index) {
        if (!site_ok(&pins[index])) return 0;
    }
    player_stock_view_host_capture capture = {.identity = *identity, .camera = camera, .fov = fov};
    player_stock_view_ui_ops ui = {&capture, stock_view_host_auth, stock_view_ui_native_read,
                                  stock_view_ui_native_get, (uintptr_t)LX(0x3dbafd0)};
    if (!stock_view_host_control_proof(&capture) || !player_stock_view_ui_select(&capture.selection, &ui, identity)) {
        return 0;
    }
    player_stock_view_capture_ops ops = {&capture, stock_view_host_auth, stock_view_ui_native_read,
        stock_view_host_floating, stock_view_host_fov, stock_view_host_target, stock_view_ui_native_portable};
    player_stock_view_capture_sample sample;
    if (!player_stock_view_capture_sample_native(&ops, identity, &sample) || !stock_view_host_control_proof(&capture) ||
        !player_stock_view_ui_revalidate(&capture.selection) || !stock_view_host_auth(NULL, identity)) {
        return 0;
    }
    if (!player_selection_parity_capture(identity->generation, identity->local_slot)) return 0;
    *out = sample;
    return 1;
}
#endif

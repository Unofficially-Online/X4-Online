#ifndef X4_GATE_PLAYER_STOCK_VIEW_UI_SELECTION_H
#define X4_GATE_PLAYER_STOCK_VIEW_UI_SELECTION_H
#include "player_stock_view_ui_selection.h"
static int player_selection_parity_capture(uint64_t generation, unsigned slot);

/* Bounded guest UI-lease capture only. This does not extend the lease,
 * publish wire records, or prove that every capture tick has a UI callback.
 * Include after the presentation/overlay gate definitions. */
typedef struct {
    player_stock_view_ui_selection selection;
    player_stock_view_capture_identity identity;
    uintptr_t observed_camera;
    float observed_fov;
    unsigned observed;
} player_stock_view_ui_native;

static inline int stock_view_ui_lease_auth(void *context, const player_stock_view_capture_identity *id) {
    (void)context;
    const presentation_fork_request *request = &presentation_fork.request;
    native_ui_overlay_probe *probe = &ui_overlay_probe;
    return id && stats.slot == 2 && id->local_slot == 2 && id->generation == stats.issued &&
        id->generation == player_recurring_tail_token.generation && player_recurring_physics_entered() &&
        syscall(SYS_gettid) == (long)probe->lifecycle.owner_tid && probe->object &&
        probe->lifecycle.object == probe->object && !ui_game_render_active && !ui_camera_refill_active &&
        __atomic_load_n(&probe->lifecycle.state, __ATOMIC_ACQUIRE) == PLAYER_UI_OVERLAY_ACTIVE &&
        id->universe == presentation_fork.universe && id->player == request->player2 &&
        id->controller == lifecycle_discard_controller &&
        player_physics_worker == (uint64_t)GetCurrentThreadId() &&
        (!ui_correction_enabled || ui_overlay_bank_matches(probe, ui_overlay_expected_bank(probe))) &&
        presentation_fork_owner(&presentation_fork, request->player1, request->player2, request->room, request->bank);
}
static inline int stock_view_ui_native_read(void *context, uintptr_t at, void *out, size_t size) {
    (void)context; return read_local((void *)at, out, size);
}
static inline int stock_view_ui_native_get(void *context, player_stock_view_softtarget *out) {
    (void)context;
    uintptr_t registry = 0;
    if (!out || !read_local(LX(0x3daa860), &registry, sizeof registry) || !registry) return 0;
    /* Struct-by-value expresses the pinned hidden-sret SysV ABI. */
    *out = ((player_stock_view_softtarget (*)(void))LX(0xa45060))();
    return 1;
}
static inline int stock_view_ui_native_target(void *context, const player_stock_view_capture_identity *id,
                                              uint64_t *runtime) {
    player_stock_view_ui_native *native = context;
    return native && player_stock_view_ui_target(&native->selection, id, runtime);
}
static inline int stock_view_ui_native_floating(void *context, unsigned *out) {
    const player_stock_view_ui_native *native = context;
    uintptr_t camera = 0, actor = 0;
    uint32_t mode = 0;
    if (!native || !out || !native->identity.universe ||
        native->identity.universe > UINTPTR_MAX - 0x3f0 ||
        !read_local((void *)(native->identity.universe + 0x3e8), &camera, 8)) return 0;
    if (!camera) { *out = 0; return 1; }
    if (camera > UINTPTR_MAX - 0x884 || !read_local((void *)(camera + 0x780), &actor, 8) ||
        !read_local((void *)(camera + 0x880), &mode, 4) || !presentation_fork.p2_ship) return 0;
    /* Proof callback establishes that the native controlled getter returns
     * this P2 ship. The pinned 0x1d6b1c0 then compares camera actor with it. */
    *out = actor && mode && actor != presentation_fork.p2_ship;
    return 1;
}
static inline int stock_view_ui_native_fov(void *context, uintptr_t camera, float *out) {
    const player_stock_view_ui_native *native = context;
    if (!native || !camera || !out) return 0;
    if (native->observed) {
        if (native->observed_camera != camera || !isfinite(native->observed_fov)) return 0;
        *out = native->observed_fov; return 1;
    }
    *out = ((float (*)(void *))LX(0x1926cc0))((void *)camera); return 1;
}
static inline int stock_view_ui_native_portable(void *context, uint64_t runtime, uint64_t *out) {
    (void)context;
    if (!out || !runtime || !world_identity_sealed || !world_identities) return 0;
    *out = world_identity_portable(runtime); return 1;
}
/* The owner must prove local controlled-object equivalence with read-only
 * evidence. No extra native controlled-object getter is called here: its
 * dynamic virtual targets have not been shown side-effect free. */
typedef struct {
    void *context;
    int (*authenticate)(void *, const player_stock_view_capture_identity *);
} player_stock_view_ui_control_proof;
static inline int stock_view_ui_capture_native(const player_stock_view_capture_identity *identity,
        const player_stock_view_ui_control_proof *proof, uintptr_t observed_camera, float observed_fov,
        unsigned observed, player_stock_view_capture_sample *out) {
    static const lx_site pins[] = {
        {"ui-softtarget", 0xa45060, 281, "e9732073f43bd1ca97f5316e64fb3eae300143026dd74cf5456d1230d384a06a"},
        {"ui-controlled-current", 0x1dd1940, 51, "870ae58c2b4ea03a44f9a17ff0589a0885f8e0932d1ca6bce3ba3d60d3dcf594"},
        {"ui-controlled-player", 0x1699f20, 0x6b, "875a39c3fadc0608768592821830bf99016fb500cdc452f788f7e69fa2091196"},
        {"ui-parent-walk", 0x189d8f0, 0x37, "7c5c18f7f341ce22b68eefdc5479b7e8c7e4d55fa0d259ed3e708edd44b951db"},
        {"ui-floating", 0x1d6b1c0, 99, "28662e2439a706b8794ec0c9dbe9b2b31aef33695461446cfbba6466a58967f6"},
        {"ui-fov", 0x1926cc0, 0x1c5, "529f00a038c89f27d56d14e5804ee101db07e8ccb8b33492372c6d46d98ab3cc"}
    };
    if (!identity || !out || !proof || !proof->authenticate) return 0;
    for (unsigned i = 0; i < sizeof pins / sizeof pins[0]; ++i) if (!site_ok(&pins[i])) return 0;
    if (observed && (!observed_camera || !isfinite(observed_fov))) return 0;
    player_stock_view_ui_native native = {.identity = *identity, .observed_camera = observed_camera,
        .observed_fov = observed_fov, .observed = observed};
    player_stock_view_ui_ops ui = {&native, stock_view_ui_lease_auth, stock_view_ui_native_read,
                                  stock_view_ui_native_get, (uintptr_t)LX(0x3dbafd0)};
    if (!stock_view_ui_lease_auth(&native, identity) || proof->authenticate(proof->context, identity) != 1 ||
        !player_stock_view_ui_select(&native.selection, &ui, identity)) return 0;
    player_stock_view_capture_ops ops = {&native, stock_view_ui_lease_auth, stock_view_ui_native_read,
        stock_view_ui_native_floating, stock_view_ui_native_fov, stock_view_ui_native_target,
        stock_view_ui_native_portable};
    player_stock_view_capture_sample next;
    if (!player_stock_view_capture_sample_native(&ops, identity, &next) ||
        proof->authenticate(proof->context, identity) != 1 || !stock_view_ui_lease_auth(&native, identity) ||
        !player_stock_view_ui_revalidate(&native.selection)) return 0;
    if (observed && !player_selection_parity_capture(identity->generation, identity->local_slot)) return 0;
    *out = next;
    return 1;
}
/* No extra native FOV call: the caller supplies a previously authenticated
 * completed renderer value for this camera and bounded generation. */
static inline int player_stock_view_ui_capture_observed(const player_stock_view_capture_identity *identity,
        const player_stock_view_ui_control_proof *proof, uintptr_t camera, float fov,
        player_stock_view_capture_sample *out) {
    return stock_view_ui_capture_native(identity, proof, camera, fov, 1, out);
}
static inline int player_stock_view_ui_capture_native(const player_stock_view_capture_identity *identity,
        const player_stock_view_ui_control_proof *proof, player_stock_view_capture_sample *out) {
    return stock_view_ui_capture_native(identity, proof, 0, 0, 0, out);
}
#endif

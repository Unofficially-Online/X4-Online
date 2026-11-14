#ifndef X4_GATE_PLAYER_STOCK_VIEW_CAPTURE_H
#define X4_GATE_PLAYER_STOCK_VIEW_CAPTURE_H
#include "player_stock_view_capture.h"

/* The capture coordinator must supply a reviewed local-selection provider.
 * It proves that this slot's player, camera and target belong to the local
 * capture epoch, including on the guest client. It must prove the singleton-
 * based floating getter represents that local slot and pin its target read.
 * Until such a provider exists, native VIEW capture is unavailable. */
typedef struct {
    void *context;
    int (*authenticate_local)(void *, const player_stock_view_capture_identity *);
    int (*target_runtime)(void *, const player_stock_view_capture_identity *, uint64_t *);
} player_stock_view_local_selection;

typedef struct {
    const player_stock_view_local_selection *selection;
} player_stock_view_native_capture;

static inline int stock_view_capture_native_auth(void *context, const player_stock_view_capture_identity *identity) {
    const player_stock_view_native_capture *native = context;
    return native && native->selection && native->selection->authenticate_local &&
        keyboard_probe_requested && keyboard_probe_worker() && stock_observer_worker_boundary() &&
        !player_routes_scope && identity->generation == stats.issued &&
        identity->generation >= keyboard_probe_queue.config.first_capture &&
        identity->generation <= keyboard_probe_queue.config.last_capture &&
        identity->local_slot == stats.slot && keyboard_probe_identity() &&
        identity->universe == keyboard_probe_binding.universe &&
        native->selection->authenticate_local(native->selection->context, identity) == 1;
}
static inline int stock_view_capture_native_read(void *context, uintptr_t at, void *out, size_t size) {
    (void)context; return read_local((void *)at, out, size);
}
static inline int stock_view_capture_native_floating(void *context, unsigned *out) {
    (void)context; if (!out) return 0;
    *out = ((unsigned char (*)(void))LX(0x1d6b1c0))(); return 1;
}
static inline int stock_view_capture_native_fov(void *context, uintptr_t camera, float *out) {
    (void)context; if (!camera || !out) return 0;
    *out = ((float (*)(void *))LX(0x1926cc0))((void *)camera); return 1;
}
static inline int stock_view_capture_native_target(void *context, const player_stock_view_capture_identity *identity,
                                                    uint64_t *runtime) {
    const player_stock_view_native_capture *native = context;
    return native && native->selection && native->selection->target_runtime &&
        native->selection->target_runtime(native->selection->context, identity, runtime) == 1;
}
static inline int stock_view_capture_native_portable(void *context, uint64_t runtime, uint64_t *out) {
    (void)context;
    if (!out || !runtime || !world_identity_sealed || !world_identities) return 0;
    *out = world_identity_portable(runtime); return 1;
}
static inline int player_stock_view_capture_native(const player_stock_view_local_selection *selection,
        const player_stock_view_capture_identity *identity, player_stock_view_capture_sample *out) {
    static const lx_site pins[] = {
        {"capture-floating", 0x1d6b1c0, 99, "28662e2439a706b8794ec0c9dbe9b2b31aef33695461446cfbba6466a58967f6"},
        {"capture-fov", 0x1926cc0, 0x1c5, "529f00a038c89f27d56d14e5804ee101db07e8ccb8b33492372c6d46d98ab3cc"}
    };
    if (!selection || !selection->authenticate_local || !selection->target_runtime) return 0;
    for (unsigned i = 0; i < sizeof pins / sizeof pins[0]; ++i) if (!site_ok(&pins[i])) return 0;
    player_stock_view_native_capture native = {selection};
    const player_stock_view_capture_ops ops = {&native, stock_view_capture_native_auth,
        stock_view_capture_native_read, stock_view_capture_native_floating, stock_view_capture_native_fov,
        stock_view_capture_native_target, stock_view_capture_native_portable};
    return player_stock_view_capture_sample_native(&ops, identity, out);
}
#endif

#ifndef X4_PLAYER_STOCK_VIEW_CAPTURE_H
#define X4_PLAYER_STOCK_VIEW_CAPTURE_H

/* Dormant Linux X4 9.00 capture primitive. The caller must authenticate a
 * local capture epoch, the native getters and the soft-target reader. In
 * particular, the camera's actor pointer is not the player's soft target.
 * A successful target read of zero means no selection; an unavailable reader
 * is failure. Nothing here enables a wire flag or mutates native state. */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t generation;
    uintptr_t universe, player, controller;
    unsigned local_slot;
} player_stock_view_capture_identity;

typedef struct {
    void *context;
    int (*authenticate)(void *, const player_stock_view_capture_identity *);
    int (*read)(void *, uintptr_t, void *, size_t);
    int (*floating)(void *, unsigned *);
    int (*fov)(void *, uintptr_t, float *);
    /* Must authenticate the actual local player's selection, not the camera
     * actor. A failed or unsupported read must return 0, never fabricate 0. */
    int (*target_runtime)(void *, const player_stock_view_capture_identity *, uint64_t *);
    /* Return 1 only when the identity map is available and authoritative;
     * write zero for a known unmapped nonzero runtime. */
    int (*portable)(void *, uint64_t, uint64_t *);
} player_stock_view_capture_ops;

typedef struct {
    unsigned char bits;
    float fov;
    uint64_t target;
} player_stock_view_capture_sample;

static inline int player_stock_view_capture_read(const player_stock_view_capture_ops *ops,
                                                  uintptr_t address, size_t offset, void *out, size_t size) {
    if (!address || offset > UINTPTR_MAX - address || size > UINTPTR_MAX - (address + offset)) return 0;
    return ops->read(ops->context, address + offset, out, size) == 1;
}

/* One FOV getter only when a camera is present and its mode is cockpit; one
 * floating getter on every successful candidate. Callback results and all
 * identity fields are staged locally and published only after final checks.
 * Repeated reads detect drift but do not establish a lock or prevent ABA. */
static inline int player_stock_view_capture_sample_native(const player_stock_view_capture_ops *ops,
        const player_stock_view_capture_identity *identity, player_stock_view_capture_sample *out) {
    if (!ops || !identity || !out || !ops->authenticate || !ops->read || !ops->floating ||
        !ops->fov || !ops->target_runtime || !ops->portable || !identity->generation ||
        !identity->universe || !identity->player || !identity->controller ||
        identity->local_slot < 1 || identity->local_slot > 2 ||
        ops->authenticate(ops->context, identity) != 1) return 0;
    uintptr_t camera = 0, camera_after = 0, actor = 0, actor_after = 0;
    uint32_t mode = 0, mode_after = 0;
    uint64_t runtime = 0, runtime_after = 0, portable = 0;
    unsigned floating = 0;
    float fov = 1.0f;
    if (!player_stock_view_capture_read(ops, identity->universe, 0x3e8, &camera, sizeof camera)) return 0;
    if (camera && (!player_stock_view_capture_read(ops, camera, 0x880, &mode, sizeof mode) ||
                   !player_stock_view_capture_read(ops, camera, 0x780, &actor, sizeof actor))) return 0;
    if (camera && !mode) {
        if (ops->fov(ops->context, camera, &fov) != 1 || !isfinite(fov) ||
            ops->authenticate(ops->context, identity) != 1) return 0;
    }
    if (ops->floating(ops->context, &floating) != 1 || floating > 1 ||
        ops->authenticate(ops->context, identity) != 1 ||
        ops->target_runtime(ops->context, identity, &runtime) != 1 ||
        ops->authenticate(ops->context, identity) != 1) return 0;
    if (runtime && (ops->portable(ops->context, runtime, &portable) != 1 ||
                    ops->authenticate(ops->context, identity) != 1)) return 0;
    if (!player_stock_view_capture_read(ops, identity->universe, 0x3e8, &camera_after, sizeof camera_after) ||
        camera != camera_after ||
        (camera && (!player_stock_view_capture_read(ops, camera, 0x880, &mode_after, sizeof mode_after) ||
                    !player_stock_view_capture_read(ops, camera, 0x780, &actor_after, sizeof actor_after) ||
                    mode != mode_after || actor != actor_after)) ||
        ops->target_runtime(ops->context, identity, &runtime_after) != 1 || runtime != runtime_after ||
        ops->authenticate(ops->context, identity) != 1) return 0;
    player_stock_view_capture_sample result = {
        .bits = (unsigned char)((!camera || !mode ? 1u : 0u) | (floating ? 2u : 0u)),
        .fov = fov, .target = runtime ? (portable ? portable : 1u) : 0u
    };
    *out = result;
    return 1;
}
#endif

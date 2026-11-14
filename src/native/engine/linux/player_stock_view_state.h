#ifndef X4_PLAYER_STOCK_VIEW_STATE_H
#define X4_PLAYER_STOCK_VIEW_STATE_H

/* Stock-v3 VIEW staging for a separately authenticated two-player pass.
 * This helper neither admits a complete record nor publishes engine hooks.
 * The owner must serialize staging and readers, authenticate both ship slots,
 * and stop readers before replacing a generation. A missing VIEW field uses
 * the Windows neutral view; unrelated unsupported flags still require their
 * own consumers and must not be silently discarded by the caller.
 */
#include "lockstep_stock_record.h"

#define PLAYER_STOCK_VIEW_SLOTS 2u

typedef struct {
    unsigned cockpit;
    unsigned floating;
    float fov;
    uint64_t target;
    unsigned present;
} player_stock_view;

typedef struct {
    uint64_t generation;
    uint64_t saved[PLAYER_STOCK_VIEW_SLOTS];
    player_stock_view slots[PLAYER_STOCK_VIEW_SLOTS];
    unsigned initialized;
} player_stock_view_state;

/* The native sampler must acquire cockpit/floating, FOV and portable target
 * from the same authorized capture scope. Failure leaves the record intact. */
typedef int (*player_stock_view_sample_fn)(void *context, unsigned char *bits,
                                           float *fov, uint64_t *target);

static inline int player_stock_view_capture(stock_input_record *record,
                                            player_stock_view_sample_fn sample,
                                            void *context) {
    if (!record || !sample || (record->flags & STOCK_INPUT_VIEW)) {
        return 0;
    }
    unsigned char bits = 0;
    float fov = 0.0f;
    uint64_t target = 0;
    if (!sample(context, &bits, &fov, &target) || bits > 3 || !isfinite(fov)) {
        return 0;
    }
    record->flags |= STOCK_INPUT_VIEW;
    record->view = bits;
    record->fov = fov;
    record->target = target;
    return 1;
}

/* Decode both complete wire records before changing the staged generation.
 * capture16 is the Windows delay-two capture epoch, not the apply epoch.
 * A caller must separately authorize every event and every present flag. */
static inline int player_stock_view_stage(player_stock_view_state *state,
                                          uint64_t generation,
                                          const uint64_t saved[2],
                                          const unsigned char *wire1, size_t length1,
                                          const unsigned char *wire2, size_t length2) {
    if (!state || !saved || !saved[0] || !saved[1] || saved[0] == saved[1] ||
        generation < 2 || (state->initialized &&
        (state->generation == UINT64_MAX || generation != state->generation + 1 ||
         saved[0] != state->saved[0] || saved[1] != state->saved[1]))) {
        return 0;
    }
    stock_input_record record[2];
    if (!stock_input_decode(wire1, length1, &record[0]) ||
        !stock_input_decode(wire2, length2, &record[1]) ||
        record[0].capture16 != (uint16_t)(generation - 2) ||
        record[1].capture16 != (uint16_t)(generation - 2)) {
        return 0;
    }
    player_stock_view_state next = {0};
    next.generation = generation;
    next.saved[0] = saved[0];
    next.saved[1] = saved[1];
    next.initialized = 1;
    for (unsigned index = 0; index < PLAYER_STOCK_VIEW_SLOTS; ++index) {
        player_stock_view *view = &next.slots[index];
        view->present = !!(record[index].flags & STOCK_INPUT_VIEW);
        if (view->present) {
            view->cockpit = record[index].view & 1u;
            view->floating = (record[index].view >> 1) & 1u;
            view->fov = record[index].fov;
            view->target = record[index].target;
        } else {
            view->cockpit = 1;
            view->fov = 1.0f;
        }
    }
    *state = next;
    return 1;
}

/* Explicit slot, ship identity and generation are required: unknown readers
 * must keep stock behavior; stale or ambiguous owned readers must fail closed. */
static inline int player_stock_view_read(const player_stock_view_state *state,
                                         uint64_t generation, unsigned slot,
                                         uint64_t saved, player_stock_view *out) {
    if (!state || !out || !state->initialized || slot < 1 || slot > 2 ||
        generation != state->generation || !saved || state->saved[slot - 1] != saved) {
        return 0;
    }
    *out = state->slots[slot - 1];
    return 1;
}

/* Map the exact controller passed as RBX in the pinned coefficient function.
 * Caller owns both controllers and must prove their lifetimes for the call.
 * Non-player controllers keep their stock result; a known controller with a
 * stale generation is refused rather than being assigned to another slot. */
static inline int player_stock_view_controller_mode(
    const player_stock_view_state *state, uint64_t generation,
    const uintptr_t controllers[2], uintptr_t controller, int native_mode,
    int *mode, unsigned *slot) {
    if (!state || !controllers || !mode || !slot || !controller ||
        !controllers[0] || !controllers[1] || controllers[0] == controllers[1]) {
        return 0;
    }
    *slot = 0;
    if (controller != controllers[0] && controller != controllers[1]) {
        *mode = native_mode;
        return 1;
    }
    unsigned selected = controller == controllers[0] ? 1u : 2u;
    player_stock_view view;
    if (!player_stock_view_read(state, generation, selected, state->saved[selected - 1], &view)) {
        return 0;
    }
    *slot = selected;
    *mode = view.cockpit ? 0 : 1;
    return 1;
}

/* On an admitted call to 0x1926cc0 the real function runs exactly once, even
 * when its value will be replaced. The caller must use the corresponding
 * per-controller mode decision at both native mode tests before this call. */
typedef float (*player_stock_view_fov_native_fn)(void *camera);
static inline int player_stock_view_controller_fov(
    const player_stock_view_state *state, uint64_t generation,
    const uintptr_t controllers[2], uintptr_t controller, void *camera,
    player_stock_view_fov_native_fn native, float *out) {
    if (!native || !out) return 0;
    int mode;
    unsigned slot;
    if (!player_stock_view_controller_mode(state, generation, controllers,
                                           controller, 0, &mode, &slot)) return 0;
    float original = native(camera);
    if (!slot) {
        *out = original;
        return 1;
    }
    player_stock_view view;
    if (!player_stock_view_read(state, generation, slot, state->saved[slot - 1], &view)) return 0;
    *out = view.cockpit ? view.fov : 1.0f;
    return 1;
}

/* Windows pview_fov_stub: external view selects unity, cockpit uses the
 * replayed factor. This is only the coefficient, not a native detour. */
static inline float player_stock_view_fov_factor(const player_stock_view *view) {
    return view->cockpit ? view->fov : 1.0f;
}
#endif

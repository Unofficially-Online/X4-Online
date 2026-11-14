#ifndef X4_LINUX_PLAYER_CURSOR_CONVERGENCE_H
#define X4_LINUX_PLAYER_CURSOR_CONVERGENCE_H

/* Cursor data used at the pinned Linux PlayerController Update convergence
 * (0x166d3ac, image 113f73d7...). This helper is dormant: it does not install
 * a hook, sample focus, widen queue admission, or authenticate engine objects.
 * The owner must supply both fully validated slot records before the first
 * effect, keep them immutable for the tick, and call only from the owner
 * worker with native controller identities authenticated for that same tick.
 * The live vector is the 12-byte bank field at 0x3d8d9b8. */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "../lockstep_stock_record.h"

typedef struct {
    uintptr_t canonical_controller;
    uintptr_t guest_controller;
    uint64_t tick;
    float replay[2][3];
    float captured[3];
    unsigned staged;
    unsigned seen;
    unsigned expected;
    unsigned captured_valid;
    unsigned captured_focused;
    uint64_t applied[2];
    unsigned failed;
} player_cursor_convergence;

static inline int player_cursor_record_vector(const stock_input_record *record, float output[3]) {
    if (!record || !output) return 0;
    for (unsigned index = 0; index < 3; ++index) {
        if (!isfinite(record->cursor[index]) || fabsf(record->cursor[index]) > 16.0f ||
            (!(record->flags & STOCK_INPUT_CURSOR) && stock_input_float_bits(record->cursor[index]) != 0))
            return 0;
    }
    if (record->flags & STOCK_INPUT_CURSOR) memcpy(output, record->cursor, sizeof(record->cursor));
    else memset(output, 0, 3 * sizeof(float));
    return 1;
}

/* Stage both rows before either native controller can consume a cursor value.
 * Once a tick has effects, an invalid or duplicate transition poisons this
 * disposable scope; callers must not retry after failure. */
static inline int player_cursor_convergence_stage(player_cursor_convergence *state, uint64_t tick,
                                                   uintptr_t canonical, uintptr_t guest,
                                                   const stock_input_record *first,
                                                   const stock_input_record *second) {
    float first_vector[3], second_vector[3];
    if (!state || state->failed) return 0;
    if (!tick || tick < 2 || !canonical || !guest || canonical == guest ||
        (state->staged && (state->seen != state->expected || tick != state->tick + 1 ||
                           state->canonical_controller != canonical || state->guest_controller != guest)) ||
        !first || !second || first->capture16 != (uint16_t)(tick - 2) ||
        second->capture16 != (uint16_t)(tick - 2) ||
        !player_cursor_record_vector(first, first_vector) ||
        !player_cursor_record_vector(second, second_vector)) {
        state->failed = 1;
        return 0;
    }
    state->canonical_controller = canonical;
    state->guest_controller = guest;
    state->tick = tick;
    memcpy(state->replay[0], first_vector, sizeof first_vector);
    memcpy(state->replay[1], second_vector, sizeof second_vector);
    state->seen = 0;
    state->expected = 3;
    state->captured_valid = 0;
    state->staged = 1;
    return 1;
}

/* Before the first delayed apply, only the canonical native Update runs. Its
 * calculated local sample is captured while replay is neutral; no P2 Update is
 * invented. This is useful for the T40 capture ticks 38 and 39. */
static inline int player_cursor_convergence_capture_only(player_cursor_convergence *state, uint64_t tick,
                                                          uintptr_t canonical, uintptr_t guest) {
    if (!state || state->failed) return 0;
    if (!tick || !canonical || !guest || canonical == guest ||
        (state->staged && (state->seen != state->expected || tick != state->tick + 1 ||
                           state->canonical_controller != canonical || state->guest_controller != guest))) {
        state->failed = 1;
        return 0;
    }
    state->canonical_controller = canonical;
    state->guest_controller = guest;
    state->tick = tick;
    memset(state->replay, 0, sizeof state->replay);
    state->seen = 0;
    state->expected = 1;
    state->captured_valid = 0;
    state->staged = 1;
    return 1;
}

/* The canonical controller computes device input on either client's machine.
 * Preserve it before writing replay; an authenticated unfocused snapshot yields
 * positive zero, matching the Windows focus rule. Unknown focus is rejected. */
static inline int player_cursor_convergence_apply_local(player_cursor_convergence *state, uint64_t tick,
                                                         uintptr_t controller, int focused, int local_active,
                                                         float native_vector[3]) {
    if (!state || state->failed) return 0;
    if (!state->staged || tick != state->tick || !native_vector ||
        (focused != 0 && focused != 1) || (local_active != 0 && local_active != 1)) {
        state->failed = 1;
        return 0;
    }
    unsigned slot = controller == state->canonical_controller ? 0 :
                    controller == state->guest_controller ? 1 : 2;
    if (slot > 1 || !(state->expected & (1u << slot)) || (state->seen & (1u << slot))) {
        state->failed = 1;
        return 0;
    }
    for (unsigned index = 0; index < 3; ++index) {
        if (!isfinite(native_vector[index]) || fabsf(native_vector[index]) > 16.0f) {
            state->failed = 1;
            return 0;
        }
    }
    if (slot == 0) {
        if (focused && local_active) memcpy(state->captured, native_vector, sizeof state->captured);
        else memset(state->captured, 0, sizeof state->captured);
        state->captured_valid = 1;
        state->captured_focused = (unsigned)focused;
    }
    memcpy(native_vector, state->replay[slot], sizeof state->replay[slot]);
    if (memcmp(native_vector, state->replay[slot], sizeof state->replay[slot])) {
        state->failed = 1;
        return 0;
    }
    ++state->applied[slot];
    state->seen |= 1u << slot;
    return 1;
}

/* Compatibility entry: existing callers still require active local flight.
 * SPACE dispatch supplies the separate local state, even when delayed P1 input
 * remains active for a few frames after this client's menu opens. */
static inline int player_cursor_convergence_apply(player_cursor_convergence *state, uint64_t tick,
                                                   uintptr_t controller, int focused,
                                                   float native_vector[3]) {
    return player_cursor_convergence_apply_local(state, tick, controller, focused, 1, native_vector);
}

/* The wire flag is not enabled here: the actual input adapter may use this
 * sample only after its focus and convergence boundary have been authenticated. */
static inline int player_cursor_convergence_capture(const player_cursor_convergence *state,
                                                     uint64_t tick, float output[3]) {
    if (!state || state->failed || !state->captured_valid || tick != state->tick || !output) return 0;
    memcpy(output, state->captured, sizeof state->captured);
    return 1;
}
#endif

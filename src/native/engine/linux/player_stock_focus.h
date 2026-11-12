#ifndef X4_PLAYER_STOCK_FOCUS_H
#define X4_PLAYER_STOCK_FOCUS_H

#include <stdint.h>

/* SDL window queries belong to the main thread. Workers consume one atomic,
 * generation-tagged result and never call SDL themselves. Zero means invalid;
 * a valid sample with no focused game window is a distinct, neutral result. */
typedef struct {
    uint64_t published;
    uint64_t last_generation; /* Main-thread writer only. */
    uintptr_t window;
    uint32_t window_id;
    unsigned failed;
} player_stock_focus_state;

static int player_stock_focus_refuse(player_stock_focus_state *state) {
    __atomic_store_n(&state->failed, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&state->published, 0, __ATOMIC_RELEASE);
    return 0;
}

/* The caller has authenticated the SDL API and read the game's window fields
 * on the main thread. Recreating that window requires a fresh test session. */
static int player_stock_focus_publish(player_stock_focus_state *state,
                                      uint64_t generation, int on_main_thread,
                                      uintptr_t window, uint32_t stored_id,
                                      uint32_t observed_id, uintptr_t focused_window) {
    if (__atomic_load_n(&state->failed, __ATOMIC_ACQUIRE) || !on_main_thread ||
        !generation || generation > (UINT64_MAX >> 1) ||
        generation != state->last_generation + 1 || !window || !stored_id ||
        observed_id != stored_id ||
        (state->window && (state->window != window || state->window_id != stored_id))) {
        return player_stock_focus_refuse(state);
    }
    state->window = window;
    state->window_id = stored_id;
    state->last_generation = generation;
    __atomic_store_n(&state->published,
                     (generation << 1) | (focused_window == window), __ATOMIC_RELEASE);
    return 1;
}

static int player_stock_focus_state_read(const player_stock_focus_state *state,
                                        uint64_t generation, unsigned *focused) {
    if (!focused || !generation || generation > (UINT64_MAX >> 1) ||
        __atomic_load_n(&state->failed, __ATOMIC_ACQUIRE)) return 0;
    uint64_t sample = __atomic_load_n(&state->published, __ATOMIC_ACQUIRE);
    if ((sample >> 1) != generation) return 0;
    *focused = (unsigned)(sample & 1);
    return 1;
}

#endif

#ifndef X4_GATE_PLAYER_CURSOR_INPUT_TRACE_H
#define X4_GATE_PLAYER_CURSOR_INPUT_TRACE_H

#include "player_cursor_input_trace.h"

/* Observation at P1's pinned cursor convergence, before delayed cursor replay.
 * This reads device gates without invoking getters or changing input state. */
static cursor_input_trace player_cursor_input_trace;

static int player_cursor_input_read(void *context, uintptr_t address,
                                    void *output, size_t length) {
    (void)context;
    return gate_read_local((const void *)address, output, length);
}

static int player_cursor_input_authenticate(void *context, uint64_t generation,
                                            uintptr_t expected_input) {
    (void)context;
    uintptr_t input = 0, universe = 0, player = 0, ship = 0;
    return player_cursor_ready && keyboard_probe_worker() &&
        player_recurring_physics_entered() && !player_routes_scope &&
        generation == stats.issued && generation == player_recurring_tail_token.generation &&
        expected_input == sko_session.owner &&
        gate_read_local(LX(0x3daaaf8), &input, sizeof input) && input == expected_input &&
        gate_read_local(LX(0x3db6948), &universe, sizeof universe) && universe &&
        universe <= UINTPTR_MAX - 0x240 &&
        gate_read_local((void *)(universe + 0x238), &player, sizeof player) &&
        player == factory_probe_player1 &&
        keyboard_probe_binding.controller &&
        keyboard_probe_binding.controller <= UINTPTR_MAX - 0x28 &&
        gate_read_local((void *)(keyboard_probe_binding.controller + 0x20), &ship, sizeof ship) &&
        ship == keyboard_probe_binding.ship;
}

static int player_cursor_input_emit(void *context, uint64_t generation, unsigned index,
                                    const cursor_input_trace_sample *sample) {
    (void)context;
    int saved_errno = errno;
    int written = dprintf(2,
        "X4 INPUT_MOUSE v=1 tick=%llu index=%u worker=%lu input=%llx device=%llx "
        "mode33=%u steer36=%u transient37=%u direct38=%u block39=%u gate42=%u override_a010=%u "
        "context8=%u ignored=%u window_gate_present=%u window_gate=%llx window_gate_value=%u "
        "window_owner=%llx window_context_present=%u window_context=%llx window_modal=%u mutation=0\n",
        (unsigned long long)generation, index, (unsigned long)GetCurrentThreadId(),
        (unsigned long long)sample->input, (unsigned long long)sample->mouse_device,
        sample->mode33, sample->steer36, sample->transient37, sample->direct38,
        sample->block39, sample->gate42, sample->override_a010, sample->context8, sample->ignored,
        sample->window_gate_present, (unsigned long long)sample->window_gate, sample->window_gate_value,
        (unsigned long long)sample->window_owner, sample->window_context_present,
        (unsigned long long)sample->window_context, sample->window_modal);
    errno = saved_errno;
    return written > 0;
}

static int player_cursor_input_request(unsigned cursor_enabled) {
    int option = cursor_input_trace_option(getenv(CURSOR_INPUT_TRACE_ENV));
    if (option < 0 || (option && !cursor_enabled)) return 0;
    return cursor_input_trace_init(&player_cursor_input_trace, option);
}

static int player_cursor_input_observe(uint64_t generation) {
    if (!player_cursor_input_trace.enabled) return 1;
    if (!sko_session.owner || sko_session.owner > UINTPTR_MAX - 0xcf1) return 0;
    const cursor_input_trace_api api = {
        .read = player_cursor_input_read,
        .authenticate = player_cursor_input_authenticate,
        .emit = player_cursor_input_emit,
        .input_global = (uintptr_t)LX(0x3daaaf8),
        .context8_global = sko_session.owner + 0xcf0,
        .ignored_global = (uintptr_t)LX(0x3e5b29a),
        .window_gate_global = (uintptr_t)LX(0x3daaaf0),
        .window_owner_global = (uintptr_t)LX(0x3daad60),
    };
    return cursor_input_trace_capture(&player_cursor_input_trace, &api, generation, sko_session.owner);
}

#endif

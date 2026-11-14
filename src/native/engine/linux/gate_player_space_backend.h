#ifndef X4_GATE_PLAYER_SPACE_BACKEND_H
#define X4_GATE_PLAYER_SPACE_BACKEND_H
#include "gate_player_space_replay.h"

/* Applied state belongs to the native bank, not the queue, whose committed
 * state is still the pre-record value while slot callbacks are running. */
static player_space_replay_state player_space_coordinator;
static unsigned player_space_applied[2] = {1, 1};
static int player_space_collect_identity(stock_keyboard_identity_sample *sample, unsigned local, unsigned delayed) {
    const stock_keyboard_identity_ops operations = {stock_keyboard_identity_pins, stock_keyboard_identity_read,
        stock_keyboard_identity_address, stock_keyboard_identity_getter, world_identity_lookup, sko_session.owner};
    return stock_keyboard_identity_collect_states(sample, &operations, 1, local, delayed);
}
static int player_space_mode(void) {
    return player_space_coordinator.requested;
}
static unsigned player_space_slot_state(unsigned slot) {
    return slot >= 1 && slot <= 2 ? player_space_applied[slot - 1] : 2;
}
static int player_space_local_state(unsigned *active) {
    return keyboard_replay_space_local_state(&keyboard_probe_queue, active);
}
static int player_space_range(void *unused_context, uintptr_t address, size_t size, unsigned writable) {
    (void)unused_context;
    if (!size || address > UINTPTR_MAX - size) return 0;
    uintptr_t end = address + size - 1;
    for (uintptr_t at = address;;) {
        char permissions[5];
        if (!gate_maps_find(at, permissions, NULL, 0) || permissions[0] != 'r' ||
            (writable && permissions[1] != 'w')) return 0;
        if (at == end) break;
        uintptr_t next = (at | (uintptr_t)4095) + 1;
        at = next > at && next < end ? next : end;
    }
    return 1;
}
static int player_space_read(void *unused_context, uintptr_t address, void *output, size_t size) {
    (void)unused_context;
    return read_local((void *)address, output, size);
}
static player_space_vector_swap player_space_cas(void *unused_context, uintptr_t address, uint64_t expected, uint64_t replacement) {
    (void)unused_context;
    if ((address & 7) || !player_space_range(NULL, address, 8, 1)) return PLAYER_SPACE_VECTOR_SWAP_REJECTED;
    return __atomic_compare_exchange_n((uint64_t *)address, &expected, replacement, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
        ? PLAYER_SPACE_VECTOR_SWAP_APPLIED : PLAYER_SPACE_VECTOR_SWAP_REJECTED;
}
static int player_space_owner(void *unused_context, uintptr_t *owner) {
    (void)unused_context;
    return read_local(LX(0x3daaaf8), owner, sizeof *owner) && *owner && *owner == sko_session.owner;
}
static int player_space_postjoin(void *unused_context, uint64_t generation, uintptr_t owner) {
    (void)unused_context;
    unsigned char flight = 0;
    return factory_probe_main_boundary() && generation == stats.issued && owner == sko_session.owner &&
        atomic_load(&sko_session.active) && !stock_observer_thread_failure && !stock_observer_failed &&
        factory_probe_player1 && lifecycle_discard_controller && player_space_native_pins() &&
        read_local(LX(0x3d8daa8), &flight, 1) && flight == 1;
}
static int player_space_phase(void *unused_context, uint64_t *generation, player_space_replay_phase *phase) {
    (void)unused_context;
    *generation = stats.issued;
    if (!keyboard_probe_worker() || atomic_load(&stock_observer_input_depth) != 1 ||
        !keyboard_probe_binding.player || !keyboard_probe_binding.controller ||
        keyboard_probe_queue.tick != stats.issued || strcmp(keyboard_probe_problem, "active")) {
        *phase = PLAYER_SPACE_PHASE_INVALID; return 1;
    }
    *phase = keyboard_probe_queue.phase == KEYBOARD_REPLAY_CAPTURE ? PLAYER_SPACE_PHASE_CAPTURE : PLAYER_SPACE_PHASE_INVALID;
    return 1;
}
static int player_space_local_op(void *unused_context, unsigned *active) {
    (void)unused_context;
    return player_space_local_state(active);
}
static int player_space_slot_op(void *unused_context, unsigned slot, unsigned *active) {
    (void)unused_context;
    if (!active || slot < 1 || slot > 2) return 0;
    *active = player_space_slot_state(slot);
    return 1;
}
static int player_space_cf0(void *unused_context, uintptr_t owner, unsigned *active) {
    (void)unused_context;
    unsigned char value = 2;
    if (!read_local((void *)(owner + 0xcf0), &value, 1) || value > 1) return 0;
    *active = value;
    return 1;
}
static int player_space_local_op_apply(void *unused_context, uint64_t generation, unsigned active) {
    (void)unused_context;
    return player_space_native_local(generation, active);
}
static int player_space_queue_edge(void *unused_context, uint64_t generation, unsigned active) {
    (void)unused_context;
    unsigned before = 2;
    if (generation != stats.issued ||
        keyboard_replay_consume(&keyboard_probe_queue, STOCK_INPUT_CONTEXT, 8, active) != KEYBOARD_REPLAY_OK ||
        !player_space_cf0(NULL, sko_session.owner, &before) || before == active) return 0;
    int saved_errno = errno;
    int written = dprintf(2, "X4LAN SPACE v=1 stage=local tick=%llu worker=%lu owner=%llx controller=%llx active=%u cf0_before=%u\n",
        (unsigned long long)generation, (unsigned long)GetCurrentThreadId(),
        (unsigned long long)sko_session.owner, (unsigned long long)keyboard_probe_binding.controller,
        active, before);
    errno = saved_errno;
    return written > 0;
}
static int player_space_refresh_op(void *unused_context, uint64_t generation) {
    (void)unused_context;
    return player_space_native_refresh(generation);
}
static void player_space_original(void *unused_context, unsigned context, unsigned char active) {
    (void)unused_context;
    ((void (*)(unsigned, unsigned char))LX(0x165d950))(context, active);
}
static void player_space_fatal(void *unused_context, const char *reason) {
    (void)unused_context;
    player_recurring_fatal(reason);
}
static const player_space_replay_ops player_space_ops = {
    .vector = {NULL, player_space_range, player_space_read, player_space_cas},
    .owner = player_space_owner, .postjoin = player_space_postjoin, .callback_phase = player_space_phase,
    .local_state = player_space_local_op, .slot_state = player_space_slot_op, .cf0 = player_space_cf0,
    .local = player_space_local_op_apply, .queue_edge = player_space_queue_edge, .refresh = player_space_refresh_op,
    .original = player_space_original, .fatal = player_space_fatal
};
static void player_space_callback(unsigned context, unsigned char active) {
    player_space_replay_callback(&player_space_coordinator, &player_space_ops, context, active);
}
static int player_space_request(unsigned independent) {
    return player_space_replay_request(&player_space_coordinator, getenv("X4_DEV_PLAYER_SPACE"), independent,
                                       (unsigned)player_cursor_mode(), player_startup()->version, player_startup()->capture);
}
static int player_space_admit(uint64_t generation) {
    return player_space_replay_mainboundary(&player_space_coordinator, &player_space_ops, generation,
                                            (uint64_t)(uintptr_t)LX(0x165d950), (uint64_t)(uintptr_t)player_space_callback);
}
static int player_space_apply(unsigned slot, uint64_t generation, unsigned active) {
    unsigned char observed = 2;
    if (!player_space_mode() || slot < 1 || slot > 2 || active > 1 || active == player_space_applied[slot - 1] ||
        !player_space_native_replay(slot, generation, active) ||
        !read_local(LX(0x3d8daa8), &observed, 1) || observed != active) return 0;
    uintptr_t controller = slot == 1 ? keyboard_probe_binding.controller : lifecycle_discard_controller;
    int saved_errno = errno;
    int written = dprintf(2, "X4LAN SPACE v=1 stage=replay tick=%llu worker=%lu slot=%u controller=%llx active=%u flight=%u\n",
        (unsigned long long)generation, (unsigned long)GetCurrentThreadId(), slot,
        (unsigned long long)controller, active, (unsigned)observed);
    errno = saved_errno;
    if (written <= 0) return 0;
    player_space_applied[slot - 1] = active;
    return 1;
}
#endif

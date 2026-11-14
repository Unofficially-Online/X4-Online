#ifndef X4_GATE_PLAYER_CURSOR_REPLAY_H
#define X4_GATE_PLAYER_CURSOR_REPLAY_H
#include "player_cursor_preload_pin.h"
#include "player_view_pin_map_batch.h"
#include "player_cursor_convergence.h"
#include <errno.h>
#include <stdio.h>

/* This active-flight-only experiment is deliberately narrower than Windows
 * SPACE handoff, VIEW, AIM and FIRE. No unvalidated wire flag is admitted. */
static player_cursor_preload_api player_cursor_api;
static player_cursor_convergence player_cursor_state;
static int player_cursor_ready, player_cursor_targets_published;
static uint64_t player_cursor_capture_receipts;
static int player_cursor_input_request(unsigned cursor_enabled);
static int player_cursor_input_observe(uint64_t generation);

static int player_cursor_update_original_pin(void) {
    const char *sha = NULL;
    uint64_t size = 0;
    player_view_query_api view;
    player_cursor_preload_api cursor;
    const char *enabled = getenv("X4_DEV_PLAYER_CURSOR");
    if (!enabled || strcmp(enabled, "1") || player_view_active_map_batch) return 0;
    player_view_map_batch mappings;
    if (!player_view_map_begin(&mappings)) return 0;
    player_view_active_map_batch = &mappings;
    int valid = view_query_expected_library(&sha, &size) &&
        player_view_query_provider(sha, size, &view) && player_cursor_preload_provider(&view, &cursor);
    player_view_active_map_batch = NULL;
    return player_view_map_end(&mappings) && valid;
}
static int player_cursor_mode(void) { return player_cursor_ready; }

/* Invoked only from the pinned pre-main bridge on the admitted worker. The
 * extra checks do not call stock getters or touch unrelated controllers. */
static int player_cursor_on_convergence(uintptr_t controller) {
    if (!player_cursor_ready || !keyboard_probe_requested ||
        stats.issued < keyboard_probe_queue.config.first_capture ||
        stats.issued > player_recurring_end) return 1;
    uintptr_t canonical = keyboard_probe_binding.controller;
    uintptr_t guest = lifecycle_discard_controller;
    if (controller != canonical && controller != guest) return 1;
    unsigned focus = 0;
    uintptr_t player = 0, controller_ship = 0;
    unsigned char flight = 0;
    unsigned slot = controller == canonical ? 1 : 2, local = 1;
    if (player_space_mode() && !player_space_local_state(&local)) return 0;
    if (!keyboard_probe_worker() || !player_recurring_physics_entered() ||
        !gate_tls_worker(gate_tls_self()) || stats.issued != player_recurring_tail_token.generation ||
        !canonical || !guest || canonical == guest ||
        !player_stock_focus_read(stats.issued, &focus) ||
        !gate_read_local(LX(0x3db6948), &player, sizeof player) || !player ||
        !gate_read_local((void *)(player + 0x238), &player, sizeof player) ||
        !gate_read_local((void *)(controller + 0x20), &controller_ship, sizeof controller_ship) ||
        !gate_read_local(LX(0x3d8daa8), &flight, sizeof flight) || flight != player_space_slot_state(slot)) return 0;
    if (controller == canonical) {
        if (player_routes_scope || player != factory_probe_player1 || controller_ship != keyboard_probe_binding.ship)
            return 0;
        if (!player_cursor_input_observe(stats.issued)) return 0;
    } else if (player_routes_scope != &player_physics_routes.routes || player != factory_probe_state.player2 ||
               controller_ship != lifecycle_discard_ship || !player_network_busy) return 0;
    return player_cursor_convergence_apply_local(&player_cursor_state, stats.issued, controller, (int)focus,
                                                  local, (float *)LX(0x3d8d9b8));
}
static int player_cursor_request(unsigned independent) {
    const char *enabled = getenv("X4_DEV_PLAYER_CURSOR");
    if (!player_cursor_input_request(enabled != NULL)) return 0;
    if (!enabled) return 1;
    const char *sha = NULL;
    uint64_t size = 0;
    player_view_query_api view;
    long tid = syscall(SYS_gettid);
    unsigned focus = 0;
    if (strcmp(enabled, "1") || !independent || !player_startup()->version ||
        !player_stock_focus_wanted() || !player_stock_focus_read(stats.issued, &focus) ||
        tid <= 0 || (unsigned long)tid > UINT32_MAX || !view_query_expected_library(&sha, &size) ||
        !player_view_query_provider(sha, size, &view) ||
        !player_cursor_preload_provider(&view, &player_cursor_api) ||
        !player_cursor_api.bind((uint32_t)tid, player_cursor_on_convergence)) return 0;
    player_cursor_ready = 1;
    return 1;
}
static int player_cursor_capture_only(uint64_t generation) {
    if (!player_cursor_ready) return 1;
    if (!keyboard_probe_binding.controller || !lifecycle_discard_controller || !player_physics_identity()) return 0;
    if (!player_cursor_targets_published) {
        unsigned focused = 0;
        unsigned char flight = 0;
        long owner = syscall(SYS_gettid);
        if (owner <= 0 || (unsigned long)owner > UINT32_MAX ||
            !player_stock_focus_read(generation, &focused) ||
            !gate_read_local(LX(0x3d8daa8), &flight, sizeof flight) || flight != 1 ||
            !player_cursor_api.targets(keyboard_probe_binding.controller, lifecycle_discard_controller)) return 0;
        int saved_errno = errno;
        int written = dprintf(2, "X4LAN CURSOR v=1 stage=targets tick=%llu owner=%lu worker=%lu canonical=%llx guest=%llx focused=%u flight=%u\n",
            (unsigned long long)generation, (unsigned long)owner, (unsigned long)GetCurrentThreadId(),
            (unsigned long long)keyboard_probe_binding.controller, (unsigned long long)lifecycle_discard_controller,
            focused, (unsigned)flight);
        errno = saved_errno;
        if (written <= 0) return 0;
        player_cursor_targets_published = 1;
    }
    return player_cursor_convergence_capture_only(&player_cursor_state, generation,
                                                keyboard_probe_binding.controller, lifecycle_discard_controller);
}
static int player_cursor_prepare_transaction(uint64_t generation, const uint64_t saved[2],
                                             const stock_input_record records[2],
                                             player_cursor_convergence *next) {
    if (!next) return 0;
    *next = player_cursor_state;
    if (!player_cursor_ready) return 1;
    if (saved[0] != keyboard_probe_queue.config.saved_ship_ids[0] ||
        saved[1] != keyboard_probe_queue.config.saved_ship_ids[1] || !player_physics_identity()) return 0;
    /* Stage in a copy: a rejected pair cannot alter the currently published
     * epoch, even if another input adapter validates the same pair. */
    return player_cursor_convergence_stage(next, generation, keyboard_probe_binding.controller,
                                           lifecycle_discard_controller, &records[0], &records[1]);
}
static int player_cursor_capture_sample(uint64_t generation, float output[3]) {
    if (!player_cursor_ready || player_cursor_state.seen != player_cursor_state.expected ||
        !player_cursor_convergence_capture(&player_cursor_state, generation, output)) return 0;
    uint32_t device[3], first[3], second[3];
    memcpy(device, output, sizeof device);
    memcpy(first, player_cursor_state.replay[0], sizeof first);
    memcpy(second, player_cursor_state.replay[1], sizeof second);
    int saved_errno = errno;
    int written = dprintf(2, "X4LAN CURSOR v=1 stage=capture tick=%llu focused=%u seen=%u device=%08x,%08x,%08x "
               "slot1=%08x,%08x,%08x slot2=%08x,%08x,%08x\n",
            (unsigned long long)generation, player_cursor_state.captured_focused, player_cursor_state.seen,
            device[0], device[1], device[2], first[0], first[1], first[2], second[0], second[1], second[2]);
    errno = saved_errno;
    if (written <= 0) return 0;
    ++player_cursor_capture_receipts;
    return 1;
}
static int player_cursor_tail(uint64_t generation, unsigned final) {
    if (!player_cursor_ready || generation < keyboard_probe_queue.config.first_capture) return 1;
    if (player_cursor_state.failed || !player_cursor_state.staged || player_cursor_state.tick != generation ||
        player_cursor_state.seen != player_cursor_state.expected) return 0;
    if (!final) return 1;
    uint64_t expected_captures = keyboard_probe_queue.config.last_capture -
                                 keyboard_probe_queue.config.first_capture + 1;
    uint64_t expected_first = generation - keyboard_probe_queue.config.first_capture + 1;
    uint64_t expected_second = generation - player_startup()->apply + 1;
    if (player_cursor_capture_receipts != expected_captures || player_cursor_state.applied[0] != expected_first ||
        player_cursor_state.applied[1] != expected_second) return 0;
    int saved_errno = errno;
    int written = dprintf(2, "X4LAN CURSOR v=1 stage=close tick=%llu captured=%llu slot1=%llu slot2=%llu complete=1\n",
            (unsigned long long)generation, (unsigned long long)player_cursor_capture_receipts,
            (unsigned long long)player_cursor_state.applied[0], (unsigned long long)player_cursor_state.applied[1]);
    errno = saved_errno;
    return written > 0;
}
#endif

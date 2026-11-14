#ifndef X4_GATE_PLAYER_VIEW_HOST_CAPTURE_RUNTIME_H
#define X4_GATE_PLAYER_VIEW_HOST_CAPTURE_RUNTIME_H
#include <errno.h>
#include <stdio.h>
#include "gate_player_stock_view_host_capture.h"
#include "player_stock_view_capture_handoff.h"
#include "player_stock_view_fov_preload_pin.h"

/* Separate, default-off P1 shadow. The caller must invoke start at keyboard
 * request and seal at the worker capture boundary; no VIEW wire is admitted. */
static player_view_render_fov_sample host_view_fov = {.mutex = PTHREAD_MUTEX_INITIALIZER};
static player_stock_view_mailbox host_view_mailbox;
static _Atomic unsigned host_view_enabled;
static _Atomic uint64_t host_view_owner_tid;
static player_stock_view_capture_sample host_view_sealed;
static unsigned host_view_sealed_present;

static void player_view_host_fov_observe(uintptr_t controller, uintptr_t camera, float value) {
    if (!atomic_load_explicit(&host_view_enabled, memory_order_acquire) ||
        (uint64_t)syscall(SYS_gettid) != atomic_load_explicit(&host_view_owner_tid, memory_order_relaxed)) {
        return;
    }
    if (stats.slot != 1 || !camera || !isfinite(value) || !keyboard_probe_worker() ||
        player_routes_scope || !player_recurring_physics_entered() ||
        stats.issued != player_recurring_tail_token.generation ||
        controller != keyboard_probe_binding.controller || !keyboard_probe_binding.universe ||
        keyboard_probe_binding.universe > UINTPTR_MAX - 0x3f0 || !keyboard_probe_binding.player ||
        keyboard_probe_binding.player != factory_probe_player1 || !keyboard_probe_binding.ship) {
        return;
    }
    uintptr_t current_camera = 0;
    uintptr_t player = 0;
    uintptr_t ship = 0;
    uintptr_t ship_controller = 0;
    if (!controller || controller > UINTPTR_MAX - 0x28 || keyboard_probe_binding.ship > UINTPTR_MAX - 0x208 ||
        !read_local((void *)(keyboard_probe_binding.universe + 0x3e8), &current_camera, 8) || current_camera != camera ||
        !read_local((void *)(keyboard_probe_binding.universe + 0x238), &player, 8) || player != keyboard_probe_binding.player ||
        !read_local((void *)(controller + 0x20), &ship, 8) || ship != keyboard_probe_binding.ship ||
        !read_local((void *)(ship + 0x200), &ship_controller, 8) || ship_controller != controller) {
        return;
    }
    (void)player_view_render_fov_publish(&host_view_fov, stats.issued, camera, controller, value);
}

static int player_view_host_capture_start(void) {
    const char *enabled = getenv("X4_DEV_HOST_VIEW_CAPTURE");
    if (!enabled) return 1;
    const char *observer = getenv("X4_DEV_STOCK_VIEW_FOV_OBSERVE");
    const char *replay = getenv("X4_DEV_STOCK_VIEW_FOV");
    /* The provider observes the original native value before applying replay.
     * Exactly one provider mode is required; capture must never read back the
     * delayed replay value and feed that value into the next wire record. */
    if ((!observer && !replay) || (observer && replay) ||
        (observer && strcmp(observer, "1")) || (replay && strcmp(replay, "1"))) return 0;
    if (strcmp(enabled, "1") || stats.slot != 1 || !player_startup()->version || !player_session_budget.frames ||
        !player_cursor_mode() || keyboard_probe_queue.config.delay != 2 || player_session_budget.presentation_count < 8) {
        return 0;
    }
    uint64_t first = player_startup()->presentation_first + 6;
    uint64_t last = player_startup()->presentation_first + player_session_budget.presentation_count - 1;
    if (last > keyboard_probe_queue.config.last_capture) last = keyboard_probe_queue.config.last_capture;
    if (!player_stock_view_mailbox_configure(&host_view_mailbox, keyboard_probe_queue.config.first_capture,
        keyboard_probe_queue.config.last_capture, first, last, 1, 2)) {
        return 0;
    }
    const char *sha = NULL;
    uint64_t size = 0;
    player_view_query_api sealed;
    int (*bind)(player_stock_view_fov_observer) = NULL;
    if (!view_query_expected_library(&sha, &size) || !player_view_query_provider(sha, size, &sealed) ||
        !player_stock_view_fov_observer_provider(&sealed, &bind)) {
        return 0;
    }
    long tid = syscall(SYS_gettid);
    if (tid <= 0 || (unsigned long)tid > UINT32_MAX || keyboard_probe_requested || keyboard_probe_closed ||
        !stats.active || boot_phase != 3 || GetCurrentThreadId() != stock_observer_worker_thread ||
        ui_thread != stock_observer_worker_thread || !gate_tls_worker(gate_tls_self()) || stock_observer_thread_failure) {
        return 0;
    }
    atomic_store_explicit(&host_view_owner_tid, (uint64_t)tid, memory_order_relaxed);
    atomic_store_explicit(&host_view_enabled, 1, memory_order_release);
    if (!bind(player_view_host_fov_observe)) {
        atomic_store_explicit(&host_view_enabled, 0, memory_order_release);
        return 0;
    }
    return 1;
}

static int player_view_host_capture_seal(uint64_t generation) {
    if (!atomic_load_explicit(&host_view_enabled, memory_order_acquire)) return 1;
    if (generation >= host_view_mailbox.first && generation <= host_view_mailbox.last) {
        uintptr_t camera = 0;
        player_stock_view_capture_identity identity = {
            generation, keyboard_probe_binding.universe, keyboard_probe_binding.player,
            keyboard_probe_binding.controller, 1
        };
        if (!identity.universe || identity.universe > UINTPTR_MAX - 0x3f0 ||
            !read_local((void *)(identity.universe + 0x3e8), &camera, 8) || !camera ||
            !player_stock_view_capture_handoff(&host_view_mailbox, &host_view_fov, &identity, camera,
                                               player_stock_view_host_capture_observed, NULL)) {
            return 0;
        }
    }
    if (!player_stock_view_mailbox_seal(&host_view_mailbox, generation, 1,
                                       &host_view_sealed_present, &host_view_sealed)) {
        return 0;
    }
    uint32_t fov_bits = 0;
    memcpy(&fov_bits, &host_view_sealed.fov, sizeof fov_bits);
    int saved_errno = errno;
    int written = dprintf(2, "X4 VIEW_HOST_CAPTURE v=1 tick=%llu present=%u bits=%u fov=%08x target=%016llx\n",
        (unsigned long long)generation, host_view_sealed_present, host_view_sealed.bits, fov_bits,
        (unsigned long long)host_view_sealed.target);
    errno = saved_errno;
    return written > 0;
}
#endif

#ifndef X4_GATE_PLAYER_VIEW_RENDER_CAPTURE_H
#define X4_GATE_PLAYER_VIEW_RENDER_CAPTURE_H
/* Diagnostic-only guest handoff. It consumes and validates the captured view
 * at seal, but does not add VIEW to wire records or enable replay policies. */
#include <errno.h>
#include <stdio.h>
#include "gate_player_stock_view_control_proof.h"
#include "player_view_render_fov_observer_pin.h"
static player_view_render_fov_sample render_view_sample = {.mutex = PTHREAD_MUTEX_INITIALIZER};
static player_stock_view_mailbox render_view_mailbox;
static _Atomic unsigned render_view_enabled;
static _Atomic uint64_t render_view_owner_tid;
static player_stock_view_capture_sample render_view_sealed;
static unsigned render_view_sealed_present;
/* Failure-only diagnostics: foreign render threads touch atomic counters only. */
static _Atomic uint64_t render_view_callbacks, render_view_owner_callbacks;
static uint64_t render_view_eligible_callbacks, render_view_published_callbacks;
static const char *render_view_last_rejection = "no_owner_callback";

static int player_view_render_capture_failure(const char *stage, uint64_t generation,
        uintptr_t camera, uintptr_t controller) {
    int saved_errno = errno;
    unsigned initialized = 0, camera_matches = 0, controller_matches = 0;
    uint64_t sample_generation = 0;
    if (!pthread_mutex_lock(&render_view_sample.mutex)) {
        initialized = render_view_sample.initialized;
        sample_generation = render_view_sample.generation;
        camera_matches = render_view_sample.camera == camera;
        controller_matches = render_view_sample.controller == controller;
        (void)pthread_mutex_unlock(&render_view_sample.mutex);
    }
    (void)dprintf(2, "X4 VIEW_RENDER_FAILURE v=1 stage=%s tick=%llu last_rejection=%s calls=%llu owner_calls=%llu eligible=%llu published=%llu initialized=%u sample_tick=%llu camera_match=%u controller_match=%u\n",
        stage, (unsigned long long)generation, render_view_last_rejection,
        (unsigned long long)atomic_load_explicit(&render_view_callbacks, memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&render_view_owner_callbacks, memory_order_relaxed),
        (unsigned long long)render_view_eligible_callbacks,
        (unsigned long long)render_view_published_callbacks, initialized,
        (unsigned long long)sample_generation, camera_matches, controller_matches);
    errno = saved_errno;
    return 0;
}

static void player_view_render_capture_observe(uintptr_t camera, float value) {
    atomic_fetch_add_explicit(&render_view_callbacks, 1, memory_order_relaxed);
    if (!atomic_load_explicit(&render_view_enabled, memory_order_acquire) ||
        (uint64_t)syscall(SYS_gettid) != atomic_load_explicit(&render_view_owner_tid, memory_order_relaxed)) return;
    atomic_fetch_add_explicit(&render_view_owner_callbacks, 1, memory_order_relaxed);
    const player_stock_view_capture_identity identity = {stats.issued, presentation_fork.universe,
        presentation_fork.request.player2, lifecycle_discard_controller, 2};
    const char *rejected = NULL;
    if (!camera || !isfinite(value)) rejected = "camera_value";
    else if (stats.slot != 2) rejected = "role";
    /* Stock projection queries also run during Move. Accept only the same
     * authenticated local lease used by capture, never a canonical P1 query
     * between the guest presentation scopes. */
    else if (ui_projection_scope_active && !stock_view_ui_lease_auth(NULL, &identity))
        rejected = "projection_scope_identity";
    else if (!ui_projection_scope_active && !ui_game_render_active && !stock_view_ui_lease_auth(NULL, &identity))
        rejected = "outside_local_presentation";
    else if (ui_camera_refill_active) rejected = "camera_refill";
    else if (player_routes_scope) rejected = "replay_scope";
    else if (syscall(SYS_gettid) != (long)ui_overlay_probe.lifecycle.owner_tid) rejected = "lease_owner";
    else if (__atomic_load_n(&ui_overlay_probe.lifecycle.state, __ATOMIC_ACQUIRE) != PLAYER_UI_OVERLAY_ACTIVE)
        rejected = "lease_state";
    else if (!player_recurring_physics_entered()) rejected = "physics_scope";
    else if (stats.issued != player_recurring_tail_token.generation) rejected = "generation";
    if (rejected) { render_view_last_rejection = rejected; return; }
    ++render_view_eligible_callbacks;
    const presentation_fork_request *request = &presentation_fork.request;
    uintptr_t actual_camera = 0, player = 0;
    if (!presentation_fork.universe || presentation_fork.universe > UINTPTR_MAX - 0x3f0 ||
        !read_local((void *)(presentation_fork.universe + 0x3e8), &actual_camera, 8) || actual_camera != camera ||
        !read_local((void *)(presentation_fork.universe + 0x238), &player, 8) || player != request->player1 ||
        !lifecycle_discard_controller ||
        !presentation_fork_owner(&presentation_fork, request->player1, request->player2, request->room, request->bank)) {
        render_view_last_rejection = "presentation_identity"; return;
    }
    if (player_view_render_fov_publish(&render_view_sample, stats.issued, camera, lifecycle_discard_controller, value))
        { ++render_view_published_callbacks; render_view_last_rejection = "published"; }
    else render_view_last_rejection = "publication";
}
static int player_view_render_capture_start(void) {
    const char *enabled = getenv("X4_DEV_RENDER_FOV_CAPTURE");
    if (!enabled) return 1;
    if (strcmp(enabled, "1")) return 0;
    /* The P1 presentation provider has not been authenticated yet. */
    if (stats.slot != 2 || !player_startup()->version || !player_session_budget.frames ||
        !player_cursor_mode() || keyboard_probe_queue.config.delay != 2 ||
        !getenv("X4_DEV_RENDER_FOV_OBSERVE") || strcmp(getenv("X4_DEV_RENDER_FOV_OBSERVE"), "1") ||
        !getenv("X4_DEV_UI_RENDER_LEASE") || strcmp(getenv("X4_DEV_UI_RENDER_LEASE"), "1") ||
        player_session_budget.presentation_count < 8) return 0;
    uint64_t first = player_startup()->presentation_first + 6;
    uint64_t last = player_startup()->presentation_first + player_session_budget.presentation_count - 1;
    if (last > keyboard_probe_queue.config.last_capture) last = keyboard_probe_queue.config.last_capture;
    if (!player_stock_view_mailbox_configure(&render_view_mailbox, keyboard_probe_queue.config.first_capture,
        keyboard_probe_queue.config.last_capture, first, last, 2, 2)) return 0;
    const char *sha = NULL; uint64_t size = 0;
    player_view_query_api sealed; player_view_render_fov_api observer;
    if (!view_query_expected_library(&sha, &size) || !player_view_query_provider(sha, size, &sealed) ||
        !player_view_render_fov_provider(&sealed, &observer)) return 0;
    long owner_tid = syscall(SYS_gettid);
    /* Request is not published yet: authenticate the same worker predicates
     * directly instead of calling keyboard_probe_worker() prematurely. */
    if (owner_tid <= 0 || (unsigned long)owner_tid > UINT32_MAX || keyboard_probe_requested ||
        keyboard_probe_closed || !stats.active || boot_phase != 3 ||
        GetCurrentThreadId() != stock_observer_worker_thread || ui_thread != stock_observer_worker_thread ||
        !gate_tls_worker(gate_tls_self()) || stock_observer_thread_failure) return 0;
    atomic_store_explicit(&render_view_owner_tid, (uint64_t)owner_tid, memory_order_relaxed);
    atomic_store_explicit(&render_view_enabled, 1, memory_order_release);
    if (!observer.bind(player_view_render_capture_observe)) {
        atomic_store_explicit(&render_view_enabled, 0, memory_order_release); return 0;
    }
    return 1;
}
static int player_view_render_capture_ui(void) {
    if (!atomic_load_explicit(&render_view_enabled, memory_order_acquire)) return 1;
    uint64_t generation = stats.issued;
    if (generation < render_view_mailbox.first || generation > render_view_mailbox.last) return 1;
    player_stock_view_capture_identity id = {generation, presentation_fork.universe,
        presentation_fork.request.player2, lifecycle_discard_controller, 2};
    uintptr_t camera = 0;
    float fov = 0;
    player_stock_view_capture_sample value;
    if (!stock_view_ui_lease_auth(NULL, &id))
        return player_view_render_capture_failure("lease", generation, camera, id.controller);
    if (id.universe > UINTPTR_MAX - 0x3f0 ||
        !read_local((void *)(id.universe + 0x3e8), &camera, sizeof camera) || !camera)
        return player_view_render_capture_failure("camera", generation, camera, id.controller);
    if (!player_view_render_fov_read(&render_view_sample, generation, camera, id.controller, &fov))
        return player_view_render_capture_failure("sample", generation, camera, id.controller);
    if (!player_stock_view_ui_verified_capture_observed(&id, camera, fov, &value))
        return player_view_render_capture_failure("capture", generation, camera, id.controller);
    if (!player_stock_view_mailbox_stage(&render_view_mailbox, generation, id.local_slot, &value))
        return player_view_render_capture_failure("mailbox", generation, camera, id.controller);
    return 1;
}
static int player_view_render_capture_seal(uint64_t generation) {
    if (!atomic_load_explicit(&render_view_enabled, memory_order_acquire)) return 1;
    if (!player_stock_view_mailbox_seal(&render_view_mailbox, generation, 2,
                                         &render_view_sealed_present, &render_view_sealed)) return 0;
    uint32_t fov_bits = 0;
    memcpy(&fov_bits, &render_view_sealed.fov, sizeof fov_bits);
    int saved_errno = errno;
    int written = dprintf(2, "X4 VIEW_RENDER_CAPTURE v=1 tick=%llu present=%u bits=%u fov=%08x target=%016llx\n",
        (unsigned long long)generation, render_view_sealed_present, render_view_sealed.bits,
        fov_bits, (unsigned long long)render_view_sealed.target);
    errno = saved_errno;
    return written > 0;
}
#endif

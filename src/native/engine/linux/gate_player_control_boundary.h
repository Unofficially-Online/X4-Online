#ifndef X4_GATE_PLAYER_CONTROL_BOUNDARY_H
#define X4_GATE_PLAYER_CONTROL_BOUNDARY_H

/* One-shot scheduling diagnostic, not permission to exchange player state.
 * Main arms before FrameTick and parks after FrameTick has returned. The
 * worker arrives after its existing path join. Neither side reads or writes
 * player banks, context trees or component references during this rendezvous.
 * Renderer/background work is NOT claimed to be suspended by this barrier.
 */
#include "frame_worker_rendezvous.h"
#include <unistd.h>

#define PLAYER_CONTROL_BOUNDARY_TIMEOUT_MS 5000u

static frame_worker_rendezvous player_control_rendezvous = FRAME_WORKER_RENDEZVOUS_INIT;
static atomic_uint player_control_boundary_requested;

static _Noreturn void player_control_boundary_fatal(const char *reason) {
    char message[512];
    int length = snprintf(message, sizeof(message),
        "X4LAN NATIVE_GATE ERROR PLAYER_CONTROL_BOUNDARY_FATAL exit=222 reason=%s state=%u failure=%u\n",
        reason, atomic_load_explicit(&player_control_rendezvous.state, memory_order_acquire),
        atomic_load_explicit(&player_control_rendezvous.failure, memory_order_acquire));
    if (length > 0 && (size_t)length < sizeof(message)) {
        ssize_t written = write(STDERR_FILENO, message, (size_t)length);
        (void)written;
    }
    _exit(222);
}

static void player_control_boundary_wait(unsigned long long started, int worker) {
    if (GetTickCount64() - started >= PLAYER_CONTROL_BOUNDARY_TIMEOUT_MS) {
        (void)fwr_cancel(&player_control_rendezvous, FWR_TICK, GetCurrentThreadId(), worker, 1);
        player_control_boundary_fatal(worker ? "worker_timeout" : "main_timeout");
    }
    const struct timespec pause = {0, 1000000};
    nanosleep(&pause, NULL);
}

/* The definitions follow the network and sealed-preload adapters. */
static void player_view_ancestry_before(uint64_t generation);
static void player_view_ancestry_after(uint64_t generation);
static void player_view_ancestry_close(uint64_t generation);
#include "gate_player_control_recurring.h"

static int player_control_boundary_before_frame(void) {
    int recurring = player_recurring_before_frame();
    if (recurring) return recurring;
    if (player_recurring_selected()) return 0;
    if ((!atomic_load_explicit(&player_control_boundary_requested, memory_order_acquire) &&
         !player_lifecycle_discard_requested()) ||
        !stats.active || stats.issued != FWR_TICK) {
        return 0;
    }
    if (GetCurrentThreadId() != main_thread || boot_phase != 3 || !path_installed ||
        !stock_observer_worker_thread || stock_observer_worker_thread != ui_thread ||
        fwr_arm(&player_control_rendezvous, stats.issued, main_thread,
                stock_observer_worker_thread) != FWR_ARMED_OK) {
        player_control_boundary_fatal("arm");
    }
    return 1;
}

/* Called only after the original FrameTick has returned on the main thread. */
static void player_control_boundary_after_frame(int armed_by_this_call) {
    if (!armed_by_this_call) {
        return;
    }
    if (armed_by_this_call == 2) {
        player_recurring_after_frame();
        return;
    }
    if (fwr_main_park(&player_control_rendezvous, FWR_TICK,
                      GetCurrentThreadId()) != FWR_ARRIVAL_RECORDED) {
        player_control_boundary_fatal("main_arrival");
    }
    unsigned long long started = GetTickCount64();
    for (;;) {
        fwr_result result = fwr_main_poll(&player_control_rendezvous, FWR_TICK,
                                         GetCurrentThreadId());
        if (result == FWR_MAIN_RETURN_ALLOWED) {
            return;
        }
        if (result != FWR_WAIT) {
            player_control_boundary_fatal("main_poll");
        }
        player_control_boundary_wait(started, 0);
    }
}

static void player_control_boundary_after_join(void) {
    if (player_recurring_after_join()) return;
    /* Main publishes the one-shot generation before native FrameTick releases
     * semSimStart. Acquire it before inspecting the already-published worker
     * boundary. Completed later passes do not reenter this one-shot scope.
     */
    unsigned flags = atomic_load_explicit(&player_control_rendezvous.state, memory_order_acquire);
    if (!(flags & FWR_ARMED) || (flags & FWR_RELEASED)) {
        return;
    }
    if (!factory_probe_joined_boundary() ||
        fwr_worker_arrive(&player_control_rendezvous, FWR_TICK,
                          GetCurrentThreadId()) != FWR_ARRIVAL_RECORDED) {
        player_control_boundary_fatal("worker_arrival");
    }
    unsigned long long started = GetTickCount64();
    fwr_entered_token token = {0};
    for (;;) {
        fwr_result result = fwr_worker_try_enter(&player_control_rendezvous, FWR_TICK,
                                                GetCurrentThreadId(), &token);
        if (result == FWR_WORKER_ENTERED) {
            break;
        }
        if (result != FWR_WAIT) {
            player_control_boundary_fatal("worker_enter");
        }
        player_control_boundary_wait(started, 1);
    }
    if (player_lifecycle_discard_requested()) {
        player_lifecycle_discard_run(atomic_load_explicit(&player_control_rendezvous.state,
                                                       memory_order_acquire));
    }
    /* Deliberately empty by default: this experiment establishes rendezvous reachability.
     * In particular it does not invoke the control-bank roundtrip helper.
     */
    if (!factory_probe_joined_boundary() ||
        fwr_worker_release(&player_control_rendezvous, FWR_TICK, GetCurrentThreadId(),
                           &token) != FWR_RELEASE_PUBLISHED) {
        player_control_boundary_fatal("worker_release");
    }
}

static int lua_player_control_boundary_request(lua_State *state) {
    if (player_recurring_selected()) player_recurring_fatal("one_shot_conflict");
    unsigned expected = 0;
    int accepted = accept_ui() && stats.active && stats.issued == 2 &&
        stock_observer_worker_boundary() &&
        atomic_compare_exchange_strong_explicit(&player_control_boundary_requested,
            &expected, 1, memory_order_release, memory_order_relaxed);
    p_lua_pushnumber(state, accepted);
    return 1;
}

static int lua_player_control_boundary_status(lua_State *state) {
    if (!stock_observer_worker_boundary()) {
        p_lua_pushnil(state);
        return 1;
    }
    unsigned flags = atomic_load_explicit(&player_control_rendezvous.state, memory_order_acquire);
    unsigned failure = atomic_load_explicit(&player_control_rendezvous.failure, memory_order_acquire);
    unsigned complete = (flags & (FWR_RELEASED | FWR_MAIN_AUTHORIZED)) ==
        (FWR_RELEASED | FWR_MAIN_AUTHORIZED) && !(flags & FWR_POISONED);
    char message[512];
    int length = snprintf(message, sizeof(message),
        "requested=%u captured_tick=%u main_parked=%u worker_entered=%u worker_released=%u "
        "main_return_allowed=%u complete=%u failure=%u state=%u "
        "readiness_only=1 bank_writes=0 context_writes=0 reference_calls=0 other_threads_excluded=0",
        atomic_load_explicit(&player_control_boundary_requested, memory_order_acquire),
        flags & FWR_ARMED ? 5u : 0u, !!(flags & FWR_MAIN_PARKED), !!(flags & FWR_ENTERED),
        !!(flags & FWR_RELEASED), !!(flags & FWR_MAIN_AUTHORIZED), complete, failure, flags);
    if (length < 0 || (size_t)length >= sizeof(message)) {
        p_lua_pushnil(state);
    } else {
        p_lua_pushlstring(state, message, (size_t)length);
    }
    return 1;
}
#endif

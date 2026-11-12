#ifndef X4LAN_FRAME_WORKER_RENDEZVOUS_H
#define X4LAN_FRAME_WORKER_RENDEZVOUS_H

/* UNUSED, ENGINE-FREE EXPERIMENT HELPER.
 *
 * One instance, one exact tick-five rendezvous, no reset/rearm or fallback.
 * Arm BEFORE FrameTick. Main calls main_park only AFTER FrameTick returns;
 * worker calls worker_arrive only AFTER the path join. The helper cannot prove
 * either engine location: callers own those hook-placement obligations.
 * Caller must supply actual, stable thread IDs in one common identity domain.
 * The registered main/worker IDs must be nonzero and distinct.
 *
 * Calls never sleep or wait for a peer. WAIT means remain parked and poll within
 * the CALLER'S bounded deadline. FAILED (including cancel/timeout) means remain
 * parked until caller's fatal exit; it NEVER permits ordinary continuation.
 * Only MAIN_RETURN_ALLOWED authorizes main to leave its park. Worker release
 * merely publishes permission; it does NOT assert main has actually returned.
 * This helper grants no bank/tree/zone writes or engine access whatsoever.
 */
#include <stdatomic.h>
#include <stdint.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "rendezvous requires lock-free unsigned atomics");

#define FWR_TICK UINT64_C(5)
#define FWR_INITIALIZING (1u << 0)
#define FWR_ARMED (1u << 1)
#define FWR_MAIN_PARKED (1u << 2)
#define FWR_WORKER_ARRIVED (1u << 3)
#define FWR_ENTERED (1u << 4)
#define FWR_RELEASED (1u << 5)
#define FWR_MAIN_AUTHORIZED (1u << 6)
#define FWR_POISONED (1u << 7)

typedef enum {
    FWR_FAILED = -1,
    FWR_WAIT = 0,
    FWR_ARRIVAL_RECORDED = 1,
    FWR_WORKER_ENTERED = 2,
    FWR_RELEASE_PUBLISHED = 3,
    FWR_MAIN_RETURN_ALLOWED = 4,
    FWR_ARMED_OK = 5
} fwr_result;

typedef enum {
    FWR_NO_FAILURE = 0, FWR_BAD_ARM, FWR_BAD_TICK, FWR_BAD_THREAD,
    FWR_BAD_ORDER, FWR_REENTRY, FWR_BAD_TOKEN, FWR_CANCELLED, FWR_TIMEOUT,
    FWR_CONTENTION
} fwr_failure;

typedef struct {
    atomic_uint state;
    atomic_uint failure;
    uint64_t main_thread;
    uint64_t worker_thread;
} frame_worker_rendezvous;

/* Zero initialization is mandatory before the instance is shared. */
#define FRAME_WORKER_RENDEZVOUS_INIT { ATOMIC_VAR_INIT(0), ATOMIC_VAR_INIT(0), 0, 0 }

typedef struct {
    const frame_worker_rendezvous *owner;
    uint64_t tick;
    uint64_t worker_thread;
} fwr_entered_token;

static inline fwr_result fwr_poison(frame_worker_rendezvous *r, fwr_failure why) {
    unsigned none = FWR_NO_FAILURE;
    (void)atomic_compare_exchange_strong_explicit(&r->failure, &none, (unsigned)why,
                                                 memory_order_release, memory_order_relaxed);
    (void)atomic_fetch_or_explicit(&r->state, FWR_POISONED, memory_order_acq_rel);
    return FWR_FAILED;
}

static inline fwr_result fwr_arm(frame_worker_rendezvous *r, uint64_t tick,
                                 uint64_t main_thread, uint64_t worker_thread) {
    unsigned empty = 0;
    if (!atomic_compare_exchange_strong_explicit(&r->state, &empty, FWR_INITIALIZING,
                                                 memory_order_acq_rel, memory_order_acquire)) {
        return fwr_poison(r, FWR_BAD_ARM);
    }
    if (tick != FWR_TICK || !main_thread || !worker_thread || main_thread == worker_thread) {
        return fwr_poison(r, FWR_BAD_ARM);
    }
    r->main_thread = main_thread;
    r->worker_thread = worker_thread;
    unsigned initializing = FWR_INITIALIZING;
    if (!atomic_compare_exchange_strong_explicit(&r->state, &initializing, FWR_ARMED,
                                                 memory_order_acq_rel, memory_order_acquire)) {
        return fwr_poison(r, FWR_BAD_ARM);
    }
    return FWR_ARMED_OK;
}

/* Acquire arm publication before reading the immutable thread registration. */
static inline int fwr_check(frame_worker_rendezvous *r, unsigned state, uint64_t tick,
                            uint64_t thread, int worker) {
    if (state & FWR_POISONED) {
        return 0;
    }
    if (!(state & FWR_ARMED)) {
        (void)fwr_poison(r, FWR_BAD_ORDER);
        return 0;
    }
    if (tick != FWR_TICK) {
        (void)fwr_poison(r, FWR_BAD_TICK);
        return 0;
    }
    if (thread != (worker ? r->worker_thread : r->main_thread)) {
        (void)fwr_poison(r, FWR_BAD_THREAD);
        return 0;
    }
    return 1;
}

static inline fwr_result fwr_arrive(frame_worker_rendezvous *r, uint64_t tick,
                                    uint64_t thread, int worker) {
    unsigned bit = worker ? FWR_WORKER_ARRIVED : FWR_MAIN_PARKED;
    for (unsigned retry = 0; retry < 16; ++retry) {
        unsigned state = atomic_load_explicit(&r->state, memory_order_acquire);
        if (!fwr_check(r, state, tick, thread, worker)) {
            return FWR_FAILED;
        }
        if (state & bit) {
            return fwr_poison(r, FWR_REENTRY);
        }
        if (state & (FWR_ENTERED | FWR_RELEASED | FWR_MAIN_AUTHORIZED)) {
            return fwr_poison(r, FWR_BAD_ORDER);
        }
        if (atomic_compare_exchange_strong_explicit(&r->state, &state, state | bit,
                                                     memory_order_acq_rel, memory_order_acquire)) {
            return FWR_ARRIVAL_RECORDED;
        }
    }
    return fwr_poison(r, FWR_CONTENTION);
}

static inline fwr_result fwr_main_park(frame_worker_rendezvous *r, uint64_t tick, uint64_t thread) {
    return fwr_arrive(r, tick, thread, 0);
}
static inline fwr_result fwr_worker_arrive(frame_worker_rendezvous *r, uint64_t tick, uint64_t thread) {
    return fwr_arrive(r, tick, thread, 1);
}

static inline fwr_result fwr_worker_try_enter(frame_worker_rendezvous *r, uint64_t tick,
                                             uint64_t thread, fwr_entered_token *token) {
    if (!token) {
        return fwr_poison(r, FWR_BAD_TOKEN);
    }
    for (unsigned retry = 0; retry < 16; ++retry) {
        unsigned state = atomic_load_explicit(&r->state, memory_order_acquire);
        if (!fwr_check(r, state, tick, thread, 1)) {
            return FWR_FAILED;
        }
        if (!(state & FWR_WORKER_ARRIVED)) {
            return fwr_poison(r, FWR_BAD_ORDER);
        }
        if (state & (FWR_ENTERED | FWR_RELEASED | FWR_MAIN_AUTHORIZED)) {
            return fwr_poison(r, FWR_REENTRY);
        }
        if (!(state & FWR_MAIN_PARKED)) {
            return FWR_WAIT;
        }
        if (atomic_compare_exchange_strong_explicit(&r->state, &state, state | FWR_ENTERED,
                                                     memory_order_acq_rel, memory_order_acquire)) {
            *token = (fwr_entered_token){r, tick, thread};
            return FWR_WORKER_ENTERED;
        }
    }
    return fwr_poison(r, FWR_CONTENTION);
}

static inline fwr_result fwr_worker_release(frame_worker_rendezvous *r, uint64_t tick,
                                           uint64_t thread, const fwr_entered_token *token) {
    unsigned state = atomic_load_explicit(&r->state, memory_order_acquire);
    if (!fwr_check(r, state, tick, thread, 1)) {
        return FWR_FAILED;
    }
    if (!token || token->owner != r || token->tick != tick || token->worker_thread != thread) {
        return fwr_poison(r, FWR_BAD_TOKEN);
    }
    if (!(state & FWR_ENTERED) || !(state & FWR_MAIN_PARKED) ||
        (state & (FWR_RELEASED | FWR_MAIN_AUTHORIZED))) {
        return fwr_poison(r, FWR_BAD_ORDER);
    }
    if (!atomic_compare_exchange_strong_explicit(&r->state, &state, state | FWR_RELEASED,
                                                 memory_order_acq_rel, memory_order_acquire)) {
        return fwr_poison(r, FWR_BAD_ORDER);
    }
    return FWR_RELEASE_PUBLISHED;
}

static inline fwr_result fwr_main_poll(frame_worker_rendezvous *r, uint64_t tick, uint64_t thread) {
    unsigned state = atomic_load_explicit(&r->state, memory_order_acquire);
    if (!fwr_check(r, state, tick, thread, 0)) {
        return FWR_FAILED;
    }
    if (!(state & FWR_MAIN_PARKED)) {
        return fwr_poison(r, FWR_BAD_ORDER);
    }
    if (state & FWR_MAIN_AUTHORIZED) {
        return fwr_poison(r, FWR_REENTRY);
    }
    if (!(state & FWR_RELEASED)) {
        return FWR_WAIT;
    }
    if (!atomic_compare_exchange_strong_explicit(&r->state, &state, state | FWR_MAIN_AUTHORIZED,
                                                 memory_order_acq_rel, memory_order_acquire)) {
        return fwr_poison(r, FWR_BAD_ORDER);
    }
    return FWR_MAIN_RETURN_ALLOWED;
}

static inline fwr_result fwr_cancel(frame_worker_rendezvous *r, uint64_t tick, uint64_t thread,
                                   int worker, int timed_out) {
    unsigned state = atomic_load_explicit(&r->state, memory_order_acquire);
    if (!fwr_check(r, state, tick, thread, worker)) {
        return FWR_FAILED;
    }
    return fwr_poison(r, timed_out ? FWR_TIMEOUT : FWR_CANCELLED);
}

#endif

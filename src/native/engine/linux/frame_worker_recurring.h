#ifndef X4_FRAME_WORKER_RECURRING_H
#define X4_FRAME_WORKER_RECURRING_H

/* Engine-free, default-unused protocol. It cannot prove hook locations,
 * object lifetime, exclusion of other readers/writers, or liveness of the
 * main/worker schedule. The caller handles bounded waiting and terminates on
 * any failure; never continue simulation after cancellation or poison.
 *
 * Initialize zeroed resident storage once, before sharing it. Registrations are
 * immutable after initialization. The packed atomic contains a generation and
 * all transition bits; no mutable generation is read outside that atomic.
 * Arm generation 5 before the corresponding FrameTick, park main after its
 * return, and arrive worker after the caller's joined boundary. A generation
 * completes only after worker release and main authorization; only then may
 * the registered main thread arm the exact next generation. */
#include <stdatomic.h>
#include <stdint.h>

_Static_assert(ATOMIC_LONG_LOCK_FREE == 2 && ATOMIC_LLONG_LOCK_FREE == 2,
               "64-bit atomic must be lock-free");
#define FWRR_FIRST_GENERATION UINT64_C(5)
#define FWRR_MAX_GENERATION (UINT64_MAX >> 8)
#define FWRR_ARMED UINT64_C(0x01)
#define FWRR_MAIN_PARKED UINT64_C(0x02)
#define FWRR_WORKER_ARRIVED UINT64_C(0x04)
#define FWRR_ENTERED UINT64_C(0x08)
#define FWRR_RELEASED UINT64_C(0x10)
#define FWRR_MAIN_AUTHORIZED UINT64_C(0x20)
#define FWRR_POISONED UINT64_C(0x40)
#define FWRR_INITIALIZING UINT64_C(0x80)
#define FWRR_FLAGS UINT64_C(0xff)

typedef enum {
    FWRR_FAILED = -1, FWRR_WAIT = 0, FWRR_OK = 1,
    FWRR_WORKER_ENTERED = 2, FWRR_MAIN_RETURN_ALLOWED = 3
} fwrr_result;
typedef enum {
    FWRR_NO_FAILURE, FWRR_BAD_REGISTRATION, FWRR_BAD_GENERATION,
    FWRR_BAD_THREAD, FWRR_BAD_ORDER, FWRR_REENTRY, FWRR_BAD_TOKEN,
    FWRR_CANCELLED, FWRR_TIMEOUT
} fwrr_failure;
typedef struct {
    _Atomic uint64_t state;
    _Atomic unsigned failure;
    uint64_t main_thread;
    uint64_t worker_thread;
} frame_worker_recurring;
#define FRAME_WORKER_RECURRING_INIT { ATOMIC_VAR_INIT(0), ATOMIC_VAR_INIT(0), 0, 0 }
typedef struct {
    const frame_worker_recurring *owner;
    uint64_t generation;
    uint64_t worker_thread;
} fwrr_token;

static inline uint64_t fwrr_generation(uint64_t state) { return state >> 8; }
static inline uint64_t fwrr_pack(uint64_t generation, uint64_t flags) {
    return (generation << 8) | flags;
}
static inline fwrr_result fwrr_fail(frame_worker_recurring *r, fwrr_failure why) {
    if (!r) return FWRR_FAILED;
    /* Poison is the transition point. A successful concurrent authorization
     * that happened earlier cannot be revoked. */
    (void)atomic_fetch_or_explicit(&r->state, FWRR_POISONED, memory_order_acq_rel);
    unsigned expected = FWRR_NO_FAILURE;
    (void)atomic_compare_exchange_strong_explicit(&r->failure, &expected, (unsigned)why,
                                                   memory_order_release, memory_order_relaxed);
    return FWRR_FAILED;
}
static inline int fwrr_initialize(frame_worker_recurring *r, uint64_t main_thread,
                                  uint64_t worker_thread) {
    if (!r || !main_thread || !worker_thread || main_thread == worker_thread) return 0;
    uint64_t empty = 0;
    if (!atomic_compare_exchange_strong_explicit(&r->state, &empty, FWRR_INITIALIZING,
                                                  memory_order_acq_rel, memory_order_acquire)) return 0;
    r->main_thread = main_thread;
    r->worker_thread = worker_thread;
    /* Generation 4 is an internal completed predecessor, not an armed tick. */
    uint64_t initializing = FWRR_INITIALIZING;
    return atomic_compare_exchange_strong_explicit(
        &r->state, &initializing, fwrr_pack(4, FWRR_RELEASED | FWRR_MAIN_AUTHORIZED),
        memory_order_release, memory_order_acquire);
}
static inline int fwrr_check(frame_worker_recurring *r, uint64_t state, uint64_t generation,
                             uint64_t thread, int worker) {
    if (state & FWRR_POISONED) return 0;
    if (!(state & FWRR_ARMED) || (state & FWRR_INITIALIZING)) {
        (void)fwrr_fail(r, FWRR_BAD_ORDER); return 0;
    }
    if (generation < FWRR_FIRST_GENERATION || generation > FWRR_MAX_GENERATION ||
        fwrr_generation(state) != generation) {
        (void)fwrr_fail(r, FWRR_BAD_GENERATION); return 0;
    }
    if (thread != (worker ? r->worker_thread : r->main_thread)) {
        (void)fwrr_fail(r, FWRR_BAD_THREAD); return 0;
    }
    return 1;
}
static inline fwrr_result fwrr_arm(frame_worker_recurring *r, uint64_t generation, uint64_t thread) {
    if (!r) return FWRR_FAILED;
    uint64_t state = atomic_load_explicit(&r->state, memory_order_acquire);
    for (;;) {
        if (state & FWRR_POISONED) return FWRR_FAILED;
        if (!state || (state & FWRR_INITIALIZING)) return fwrr_fail(r, FWRR_BAD_ORDER);
        if (thread != r->main_thread) return fwrr_fail(r, FWRR_BAD_THREAD);
        uint64_t previous = fwrr_generation(state);
        if (generation < FWRR_FIRST_GENERATION || generation > FWRR_MAX_GENERATION ||
            previous >= FWRR_MAX_GENERATION || generation != previous + 1)
            return fwrr_fail(r, FWRR_BAD_GENERATION);
        if ((state & (FWRR_RELEASED | FWRR_MAIN_AUTHORIZED)) !=
            (FWRR_RELEASED | FWRR_MAIN_AUTHORIZED)) return fwrr_fail(r, FWRR_BAD_ORDER);
        uint64_t next = fwrr_pack(generation, FWRR_ARMED);
        if (atomic_compare_exchange_strong_explicit(&r->state, &state, next,
                                                     memory_order_acq_rel, memory_order_acquire)) return FWRR_OK;
    }
}
static inline fwrr_result fwrr_arrive(frame_worker_recurring *r, uint64_t generation,
                                      uint64_t thread, int worker) {
    if (!r) return FWRR_FAILED;
    uint64_t bit = worker ? FWRR_WORKER_ARRIVED : FWRR_MAIN_PARKED;
    uint64_t state = atomic_load_explicit(&r->state, memory_order_acquire);
    for (;;) {
        if (!fwrr_check(r, state, generation, thread, worker)) return FWRR_FAILED;
        if (state & bit) return fwrr_fail(r, FWRR_REENTRY);
        if (state & (FWRR_ENTERED | FWRR_RELEASED | FWRR_MAIN_AUTHORIZED))
            return fwrr_fail(r, FWRR_BAD_ORDER);
        if (atomic_compare_exchange_strong_explicit(&r->state, &state, state | bit,
                                                     memory_order_acq_rel, memory_order_acquire)) return FWRR_OK;
    }
}
static inline fwrr_result fwrr_main_park(frame_worker_recurring *r, uint64_t generation, uint64_t thread) {
    return fwrr_arrive(r, generation, thread, 0);
}
static inline fwrr_result fwrr_worker_arrive(frame_worker_recurring *r, uint64_t generation, uint64_t thread) {
    return fwrr_arrive(r, generation, thread, 1);
}
static inline fwrr_result fwrr_worker_try_enter(frame_worker_recurring *r, uint64_t generation,
                                                uint64_t thread, fwrr_token *token) {
    if (!r) return FWRR_FAILED;
    if (!token) return fwrr_fail(r, FWRR_BAD_TOKEN);
    uint64_t state = atomic_load_explicit(&r->state, memory_order_acquire);
    for (;;) {
        if (!fwrr_check(r, state, generation, thread, 1)) return FWRR_FAILED;
        if (!(state & FWRR_WORKER_ARRIVED)) return fwrr_fail(r, FWRR_BAD_ORDER);
        if (state & (FWRR_ENTERED | FWRR_RELEASED | FWRR_MAIN_AUTHORIZED))
            return fwrr_fail(r, FWRR_REENTRY);
        if (!(state & FWRR_MAIN_PARKED)) return FWRR_WAIT;
        if (atomic_compare_exchange_strong_explicit(&r->state, &state, state | FWRR_ENTERED,
                                                     memory_order_acq_rel, memory_order_acquire)) {
            *token = (fwrr_token){r, generation, thread};
            return FWRR_WORKER_ENTERED;
        }
    }
}
static inline fwrr_result fwrr_worker_release(frame_worker_recurring *r, uint64_t generation,
                                               uint64_t thread, const fwrr_token *token) {
    if (!r) return FWRR_FAILED;
    uint64_t state = atomic_load_explicit(&r->state, memory_order_acquire);
    for (;;) {
        if (!fwrr_check(r, state, generation, thread, 1)) return FWRR_FAILED;
        if (!token || token->owner != r || token->generation != generation || token->worker_thread != thread)
            return fwrr_fail(r, FWRR_BAD_TOKEN);
        if (!(state & FWRR_ENTERED) || !(state & FWRR_MAIN_PARKED) ||
            (state & (FWRR_RELEASED | FWRR_MAIN_AUTHORIZED))) return fwrr_fail(r, FWRR_BAD_ORDER);
        if (atomic_compare_exchange_strong_explicit(&r->state, &state, state | FWRR_RELEASED,
                                                     memory_order_acq_rel, memory_order_acquire)) return FWRR_OK;
    }
}
static inline fwrr_result fwrr_main_poll(frame_worker_recurring *r, uint64_t generation, uint64_t thread) {
    if (!r) return FWRR_FAILED;
    uint64_t state = atomic_load_explicit(&r->state, memory_order_acquire);
    for (;;) {
        if (!fwrr_check(r, state, generation, thread, 0)) return FWRR_FAILED;
        if (!(state & FWRR_MAIN_PARKED)) return fwrr_fail(r, FWRR_BAD_ORDER);
        if (state & FWRR_MAIN_AUTHORIZED) return fwrr_fail(r, FWRR_REENTRY);
        if (!(state & FWRR_RELEASED)) return FWRR_WAIT;
        if (atomic_compare_exchange_strong_explicit(&r->state, &state, state | FWRR_MAIN_AUTHORIZED,
                                                     memory_order_acq_rel, memory_order_acquire))
            return FWRR_MAIN_RETURN_ALLOWED;
    }
}
static inline fwrr_result fwrr_cancel(frame_worker_recurring *r, uint64_t generation,
                                      uint64_t thread, int worker, int timed_out) {
    if (!r) return FWRR_FAILED;
    uint64_t state = atomic_load_explicit(&r->state, memory_order_acquire);
    for (;;) {
        if (!fwrr_check(r, state, generation, thread, worker)) return FWRR_FAILED;
        if (state & FWRR_MAIN_AUTHORIZED) return fwrr_fail(r, FWRR_BAD_ORDER);
        if (atomic_compare_exchange_strong_explicit(&r->state, &state, state | FWRR_POISONED,
                                                     memory_order_acq_rel, memory_order_acquire)) {
            unsigned expected = FWRR_NO_FAILURE;
            (void)atomic_compare_exchange_strong_explicit(
                &r->failure, &expected, timed_out ? FWRR_TIMEOUT : FWRR_CANCELLED,
                memory_order_release, memory_order_relaxed);
            return FWRR_FAILED;
        }
    }
}
#endif

#ifndef X4_PLAYER_WORKER_DISPATCH_CONTRACT_H
#define X4_PLAYER_WORKER_DISPATCH_CONTRACT_H

/* Pure, default-unused supplement to FWRR. This checks the order of events
 * supplied by an adapter; it does not authenticate native call sites, exclude
 * other semaphore users, or establish an empty startup semaphore. In particular,
 * do not use it as a native admission until those facts have been established.
 * The adapter must keep the contract resident and serialize initialization. */
#include "frame_worker_recurring.h"
#include <stddef.h>

#define PWDC_POST_STARTED UINT64_C(0x01)
#define PWDC_POST_RETURNED UINT64_C(0x02)
#define PWDC_WAIT_CONSUMED UINT64_C(0x04)
#define PWDC_JOINED UINT64_C(0x08)
#define PWDC_FAILED UINT64_C(0x10)
#define PWDC_FLAGS UINT64_C(0xff)

typedef struct {
    _Atomic uint64_t state;
    frame_worker_recurring *rendezvous;
} player_worker_dispatch_contract;

#define PLAYER_WORKER_DISPATCH_CONTRACT_INIT { ATOMIC_VAR_INIT(0), NULL }

static inline int pwdc_initialize(player_worker_dispatch_contract *contract,
                                  frame_worker_recurring *rendezvous) {
    if (!contract || !rendezvous || contract->rendezvous ||
        atomic_load_explicit(&contract->state, memory_order_acquire)) return 0;
    contract->rendezvous = rendezvous;
    return 1;
}

static inline int pwdc_fail(player_worker_dispatch_contract *contract) {
    if (contract) {
        atomic_fetch_or_explicit(&contract->state, PWDC_FAILED, memory_order_acq_rel);
        if (contract->rendezvous) fwrr_fail(contract->rendezvous, FWRR_BAD_ORDER);
    }
    return 0;
}

static inline int pwdc_frame_pending(const player_worker_dispatch_contract *contract,
                                     uint64_t generation) {
    uint64_t frame = atomic_load_explicit(&contract->rendezvous->state, memory_order_acquire);
    uint64_t flags = frame & FWRR_FLAGS;
    return fwrr_generation(frame) == generation &&
        (flags == FWRR_ARMED || flags == (FWRR_ARMED | FWRR_MAIN_PARKED));
}

/* Main calls immediately before the exact admitted FrameTick post. Mark before
 * forwarding: the worker may return from its wait before that call returns. */
static inline int pwdc_post_begin(player_worker_dispatch_contract *contract,
                                  uint64_t generation, uint64_t thread) {
    if (!contract || !contract->rendezvous) return pwdc_fail(contract);
    frame_worker_recurring *r = contract->rendezvous;
    uint64_t frame = atomic_load_explicit(&r->state, memory_order_acquire);
    uint64_t previous = atomic_load_explicit(&contract->state, memory_order_acquire);
    if (thread != r->main_thread || generation > FWRR_MAX_GENERATION ||
        generation < FWRR_FIRST_GENERATION || fwrr_generation(frame) != generation ||
        (frame & FWRR_FLAGS) != FWRR_ARMED || (previous & PWDC_FAILED))
        return pwdc_fail(contract);
    if (previous && ((previous & PWDC_FLAGS) !=
        (PWDC_POST_STARTED | PWDC_POST_RETURNED | PWDC_WAIT_CONSUMED | PWDC_JOINED) ||
        fwrr_generation(previous) + 1 != generation)) return pwdc_fail(contract);
    uint64_t next = fwrr_pack(generation, PWDC_POST_STARTED);
    if (!atomic_compare_exchange_strong_explicit(&contract->state, &previous, next,
                                                  memory_order_acq_rel, memory_order_acquire))
        return pwdc_fail(contract);
    return 1;
}

/* Call only after forwarding the native post once. This records return, not a
 * claim about an undocumented native return code or semaphore ownership. */
static inline int pwdc_post_return(player_worker_dispatch_contract *contract,
                                   uint64_t generation, uint64_t thread) {
    if (!contract || !contract->rendezvous || thread != contract->rendezvous->main_thread ||
        !pwdc_frame_pending(contract, generation))
        return pwdc_fail(contract);
    uint64_t state = atomic_load_explicit(&contract->state, memory_order_acquire);
    for (;;) {
        if (fwrr_generation(state) != generation || !(state & PWDC_POST_STARTED) ||
            (state & (PWDC_POST_RETURNED | PWDC_JOINED | PWDC_FAILED))) return pwdc_fail(contract);
        if (atomic_compare_exchange_weak_explicit(&contract->state, &state, state | PWDC_POST_RETURNED,
                                                   memory_order_acq_rel, memory_order_acquire)) return 1;
    }
}

/* The existing movement wait wrapper calls this only after the original wait
 * returns for the pinned semaphore. No interpretation of the raw return value
 * is made here; the adapter must separately authenticate its ABI and meaning. */
static inline int pwdc_wait_return(player_worker_dispatch_contract *contract,
                                   uint64_t generation, uint64_t thread) {
    if (!contract || !contract->rendezvous || thread != contract->rendezvous->worker_thread ||
        !pwdc_frame_pending(contract, generation))
        return pwdc_fail(contract);
    uint64_t state = atomic_load_explicit(&contract->state, memory_order_acquire);
    for (;;) {
        if (fwrr_generation(state) != generation || !(state & PWDC_POST_STARTED) ||
            (state & (PWDC_WAIT_CONSUMED | PWDC_JOINED | PWDC_FAILED))) return pwdc_fail(contract);
        if (atomic_compare_exchange_weak_explicit(&contract->state, &state, state | PWDC_WAIT_CONSUMED,
                                                   memory_order_acq_rel, memory_order_acquire)) return 1;
    }
}

/* At the existing post-join hook: 0 means post has not returned yet and the
 * caller may wait with a bound; -1 is fatal; 1 consumes the generation once. */
static inline int pwdc_join(player_worker_dispatch_contract *contract,
                            uint64_t generation, uint64_t thread) {
    if (!contract || !contract->rendezvous || thread != contract->rendezvous->worker_thread ||
        !pwdc_frame_pending(contract, generation)) {
        pwdc_fail(contract); return -1;
    }
    uint64_t state = atomic_load_explicit(&contract->state, memory_order_acquire);
    for (;;) {
        if (fwrr_generation(state) != generation ||
            (state & (PWDC_POST_STARTED | PWDC_WAIT_CONSUMED)) !=
                (PWDC_POST_STARTED | PWDC_WAIT_CONSUMED) || (state & (PWDC_JOINED | PWDC_FAILED))) {
            pwdc_fail(contract); return -1;
        }
        if (!(state & PWDC_POST_RETURNED)) return 0;
        if (atomic_compare_exchange_weak_explicit(&contract->state, &state, state | PWDC_JOINED,
                                                   memory_order_acq_rel, memory_order_acquire)) return 1;
    }
}

/* Admitted scope only: alternate wait, unexpected post or shutdown means the
 * dispatch is ambiguous. The process must stop; no retry or native resume. */
static inline int pwdc_unexpected_activity(player_worker_dispatch_contract *contract) {
    return pwdc_fail(contract);
}
#endif

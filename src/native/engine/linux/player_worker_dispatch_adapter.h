#ifndef X4_PLAYER_WORKER_DISPATCH_ADAPTER_H
#define X4_PLAYER_WORKER_DISPATCH_ADAPTER_H
/* Resident handoff; caller authenticates native sites and never resumes after failure. */
#include "player_worker_dispatch_contract.h"
enum { PWDA_OFF, PWDA_REQUESTED, PWDA_STARTUP_POST, PWDA_DRAIN, PWDA_PARKED,
       PWDA_ARMED, PWDA_POSTING, PWDA_POSTED, PWDA_CONSUMED, PWDA_JOINED, PWDA_TAIL, PWDA_FAILED };
typedef struct {
    _Atomic unsigned phase;
    _Atomic uint64_t generation;
    uint64_t main_thread, worker_thread, first_generation;
    player_worker_dispatch_contract contract;
    void *context;
    int (*snapshot)(void *, unsigned *, unsigned *);
    uint64_t (*clock_ms)(void *);
    void (*pause)(void *);
    unsigned timeout_ms;
} player_worker_dispatch_adapter;
#define PLAYER_WORKER_DISPATCH_ADAPTER_INIT {0}
static inline int pwda_fail(player_worker_dispatch_adapter *adapter) {
    atomic_store_explicit(&adapter->phase, PWDA_FAILED, memory_order_release);
    return pwdc_fail(&adapter->contract);
}
static inline unsigned pwda_phase(player_worker_dispatch_adapter *adapter) {
    return atomic_load_explicit(&adapter->phase, memory_order_acquire);
}
static inline int pwda_transition(player_worker_dispatch_adapter *adapter, unsigned from, unsigned to) {
    return atomic_compare_exchange_strong_explicit(&adapter->phase, &from, to,
        memory_order_acq_rel, memory_order_acquire) ? 1 : pwda_fail(adapter);
}
static inline int pwda_wait_phase(player_worker_dispatch_adapter *adapter, unsigned phase) {
    uint64_t start = adapter->clock_ms(adapter->context);
    while (pwda_phase(adapter) == phase) {
        if (adapter->clock_ms(adapter->context) - start >= adapter->timeout_ms)
            return pwda_fail(adapter);
        adapter->pause(adapter->context);
    }
    return pwda_phase(adapter) != PWDA_FAILED;
}
static inline int pwda_empty(player_worker_dispatch_adapter *adapter, unsigned *count) {
    unsigned maximum = 0;
    if (!adapter->snapshot(adapter->context, count, &maximum) || maximum != 1 || *count > 1)
        return pwda_fail(adapter);
    return 1;
}
static inline int pwda_request(player_worker_dispatch_adapter *adapter, uint64_t main_thread, uint64_t worker_thread,
                               uint64_t first_generation, frame_worker_recurring *rendezvous) {
    if (!adapter || !main_thread || !worker_thread || main_thread == worker_thread || first_generation < 2 || !adapter->snapshot ||
        !adapter->clock_ms || !adapter->pause || !adapter->timeout_ms || pwda_phase(adapter) != PWDA_OFF ||
        !pwdc_initialize(&adapter->contract, rendezvous)) return adapter ? pwda_fail(adapter) : 0;
    adapter->main_thread = main_thread;
    adapter->worker_thread = worker_thread;
    adapter->first_generation = first_generation;
    atomic_store_explicit(&adapter->phase, PWDA_REQUESTED, memory_order_release);
    return 1;
}
static inline int pwda_post_begin(player_worker_dispatch_adapter *adapter, uint64_t thread) {
    if (thread != adapter->main_thread)
        return pwda_fail(adapter);
    if (pwda_phase(adapter) == PWDA_REQUESTED)
        return pwda_transition(adapter, PWDA_REQUESTED, PWDA_STARTUP_POST);
    if (pwda_phase(adapter) != PWDA_ARMED || !pwdc_post_begin(&adapter->contract,
        atomic_load_explicit(&adapter->generation, memory_order_acquire), thread)) return pwda_fail(adapter);
    return pwda_transition(adapter, PWDA_ARMED, PWDA_POSTING);
}
static inline int pwda_post_return(player_worker_dispatch_adapter *adapter, uint64_t thread) {
    if (thread != adapter->main_thread)
        return pwda_fail(adapter);
    if (pwda_phase(adapter) == PWDA_STARTUP_POST)
        return pwda_transition(adapter, PWDA_STARTUP_POST, PWDA_DRAIN);
    if (pwda_phase(adapter) != PWDA_POSTING || !pwdc_post_return(&adapter->contract,
        atomic_load_explicit(&adapter->generation, memory_order_acquire), thread)) return pwda_fail(adapter);
    return pwda_transition(adapter, PWDA_POSTING, PWDA_POSTED);
}
/* A zero ticket forwards an old startup wait unchanged. Positive ticket is
 * assigned at this exact invocation, before entering its native wait. */
static inline int pwda_pre_wait(player_worker_dispatch_adapter *adapter, uint64_t thread, uint64_t *ticket) {
    if (thread != adapter->worker_thread || *ticket)
        return pwda_fail(adapter);
    unsigned phase = pwda_phase(adapter), count;
    if (phase == PWDA_REQUESTED)
        return 1;
    if (phase == PWDA_STARTUP_POST) {
        if (!pwda_wait_phase(adapter, PWDA_STARTUP_POST))
            return 0;
        phase = pwda_phase(adapter);
    }
    if (phase == PWDA_TAIL) {
        if (!pwda_empty(adapter, &count) || count || !pwda_transition(adapter, PWDA_TAIL, PWDA_PARKED))
            return pwda_fail(adapter);
    } else if (phase == PWDA_DRAIN) {
        if (!pwda_empty(adapter, &count))
            return 0;
        if (count)
            return 1;
        if (!pwda_transition(adapter, PWDA_DRAIN, PWDA_PARKED))
            return 0;
    } else {
        return pwda_fail(adapter);
    }
    uint64_t start = adapter->clock_ms(adapter->context);
    for (;;) {
        phase = pwda_phase(adapter);
        if (phase == PWDA_POSTED) break;
        if (phase != PWDA_PARKED && phase != PWDA_ARMED && phase != PWDA_POSTING)
            return pwda_fail(adapter);
        if (adapter->clock_ms(adapter->context) - start >= adapter->timeout_ms)
            return pwda_fail(adapter);
        adapter->pause(adapter->context);
    }
    *ticket = atomic_load_explicit(&adapter->generation, memory_order_acquire);
    return *ticket != 0 ? 1 : pwda_fail(adapter);
}
static inline int pwda_arm(player_worker_dispatch_adapter *adapter, uint64_t generation, uint64_t thread) {
    if (thread != adapter->main_thread)
        return pwda_fail(adapter);
    uint64_t start = adapter->clock_ms(adapter->context);
    while (pwda_phase(adapter) != PWDA_PARKED) {
        if (pwda_phase(adapter) == PWDA_FAILED || adapter->clock_ms(adapter->context) - start >= adapter->timeout_ms)
            return pwda_fail(adapter);
        adapter->pause(adapter->context);
    }
    uint64_t previous = atomic_load_explicit(&adapter->generation, memory_order_acquire);
    unsigned count;
    if (generation != (previous ? previous + 1 : adapter->first_generation) ||
        !pwda_empty(adapter, &count) || count) return pwda_fail(adapter);
    atomic_store_explicit(&adapter->generation, generation, memory_order_release);
    /* FWRR is armed by caller while worker_thread remains PARKED. */
    return 1;
}
static inline int pwda_publish_arm(player_worker_dispatch_adapter *adapter) {
    return pwda_transition(adapter, PWDA_PARKED, PWDA_ARMED);
}
static inline int pwda_wait_return(player_worker_dispatch_adapter *adapter, uint64_t thread, uint64_t ticket) {
    if (!ticket) {
        unsigned phase = pwda_phase(adapter);
        return (thread == adapter->worker_thread && phase >= PWDA_REQUESTED && phase <= PWDA_DRAIN)
            ? 1 : pwda_fail(adapter);
    }
    if (ticket != atomic_load_explicit(&adapter->generation, memory_order_acquire) ||
        !pwdc_wait_return(&adapter->contract, ticket, thread)) return pwda_fail(adapter);
    return pwda_transition(adapter, PWDA_POSTED, PWDA_CONSUMED);
}
static inline int pwda_join(player_worker_dispatch_adapter *adapter, uint64_t thread, uint64_t ticket) {
    if (!ticket || ticket != atomic_load_explicit(&adapter->generation, memory_order_acquire) ||
        pwdc_join(&adapter->contract, ticket, thread) != 1) return pwda_fail(adapter);
    return pwda_transition(adapter, PWDA_CONSUMED, PWDA_JOINED);
}
static inline int pwda_tail(player_worker_dispatch_adapter *adapter, uint64_t thread, uint64_t *ticket) {
    if (thread != adapter->worker_thread || !*ticket ||
        *ticket != atomic_load_explicit(&adapter->generation, memory_order_acquire) ||
        !pwda_transition(adapter, PWDA_JOINED, PWDA_TAIL)) return pwda_fail(adapter);
    *ticket = 0;
    return 1;
}
#endif

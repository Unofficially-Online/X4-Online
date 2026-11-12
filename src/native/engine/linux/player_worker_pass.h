#ifndef X4_PLAYER_WORKER_PASS_H
#define X4_PLAYER_WORKER_PASS_H

#include <stdatomic.h>
#include <stdint.h>

/* Policy only: the caller authenticates the native hook sites, semaphore,
 * owner threads, zero permits under the native mutex, and exclusion of other
 * producers/consumers. This does not establish engine reader exclusion. A wait
 * marker is published before the native wait and therefore does not prove the
 * worker has entered pthread_cond_wait. No native semaphore is touched here. */
typedef enum {
    PWP_IDLE,
    PWP_WAITING,
    PWP_WAIT_PUBLISHING,
    PWP_ARM_PUBLISHING,
    PWP_ARMED,
    PWP_POSTING,
    PWP_POSTED,
    PWP_EARLY_WAKE,
    PWP_AWOKEN,
    PWP_JOINED,
    PWP_TAIL,
    PWP_AUTHORIZED,
    PWP_FAILED,
} pwp_phase;

typedef struct {
    _Atomic unsigned phase;
    _Atomic uint64_t ordinal;
    _Atomic uint64_t generation;
    uint64_t main_thread;
    uint64_t worker_thread;
} player_worker_pass;

typedef struct {
    const player_worker_pass *owner;
    uint64_t ordinal;
    uint64_t generation;
    uint64_t worker_thread;
} pwp_token;

static inline int pwp_fail(player_worker_pass *pass) {
    atomic_store_explicit(&pass->phase, PWP_FAILED, memory_order_release);
    return 0;
}

static inline int pwp_init(player_worker_pass *pass, uint64_t main_thread,
                           uint64_t worker_thread, uint64_t initial_generation) {
    if (!pass || !main_thread || !worker_thread || main_thread == worker_thread) {
        return 0;
    }
    atomic_init(&pass->phase, PWP_IDLE);
    atomic_init(&pass->ordinal, 0);
    atomic_init(&pass->generation, initial_generation);
    pass->main_thread = main_thread;
    pass->worker_thread = worker_thread;
    return 1;
}

static inline int pwp_transition(player_worker_pass *pass, unsigned expected, unsigned next) {
    if (!atomic_compare_exchange_strong_explicit(&pass->phase, &expected, next,
            memory_order_acq_rel, memory_order_acquire)) {
        return pwp_fail(pass);
    }
    return 1;
}

/* The ordinal is worker-owned and strictly consecutive, never a main tick. */
static inline int pwp_wait(player_worker_pass *pass, uint64_t thread_id, uint64_t ordinal) {
    if (!pass || thread_id != pass->worker_thread || !ordinal ||
        ordinal != atomic_load_explicit(&pass->ordinal, memory_order_acquire) + 1) {
        return pass ? pwp_fail(pass) : 0;
    }
    unsigned state = atomic_load_explicit(&pass->phase, memory_order_acquire);
    if (state != PWP_IDLE && state != PWP_AUTHORIZED) {
        return pwp_fail(pass);
    }
    if (!pwp_transition(pass, state, PWP_WAIT_PUBLISHING)) {
        return 0;
    }
    atomic_store_explicit(&pass->ordinal, ordinal, memory_order_relaxed);
    return pwp_transition(pass, PWP_WAIT_PUBLISHING, PWP_WAITING);
}

/* zero_permits and exclusive_native_path are certified by the integration,
 * not guessed from the wait marker. */
static inline int pwp_arm(player_worker_pass *pass, uint64_t thread_id, uint64_t generation,
                          uint64_t ordinal, int zero_permits, int exclusive_native_path) {
    if (!pass || thread_id != pass->main_thread || !generation || !ordinal || !zero_permits ||
        !exclusive_native_path || ordinal != atomic_load_explicit(&pass->ordinal, memory_order_acquire) ||
        generation != atomic_load_explicit(&pass->generation, memory_order_acquire) + 1) {
        return pass ? pwp_fail(pass) : 0;
    }
    if (!pwp_transition(pass, PWP_WAITING, PWP_ARM_PUBLISHING)) {
        return 0;
    }
    atomic_store_explicit(&pass->generation, generation, memory_order_relaxed);
    return pwp_transition(pass, PWP_ARM_PUBLISHING, PWP_ARMED);
}

static inline int pwp_post_begin(player_worker_pass *pass, uint64_t thread_id, uint64_t generation,
                                 uint64_t ordinal, int native_site_and_args_valid) {
    if (!pass || thread_id != pass->main_thread || !native_site_and_args_valid ||
        generation != atomic_load_explicit(&pass->generation, memory_order_acquire) ||
        ordinal != atomic_load_explicit(&pass->ordinal, memory_order_acquire)) {
        return pass ? pwp_fail(pass) : 0;
    }
    return pwp_transition(pass, PWP_ARMED, PWP_POSTING);
}

/* May race the worker's return from the native wait. */
static inline int pwp_post_done(player_worker_pass *pass, uint64_t thread_id, int native_post_ok) {
    if (!pass || thread_id != pass->main_thread || !native_post_ok) {
        return pass ? pwp_fail(pass) : 0;
    }
    unsigned state = PWP_POSTING;
    if (atomic_compare_exchange_strong_explicit(&pass->phase, &state, PWP_POSTED,
            memory_order_acq_rel, memory_order_acquire)) {
        return 1;
    }
    if (state == PWP_EARLY_WAKE) {
        return pwp_transition(pass, PWP_EARLY_WAKE, PWP_AWOKEN);
    }
    return pwp_fail(pass);
}

/* Returns 1 when post has completed, 2 when caller must await post_done. */
static inline int pwp_wake(player_worker_pass *pass, uint64_t thread_id, uint64_t ordinal,
                           pwp_token *token) {
    if (!pass || !token || thread_id != pass->worker_thread ||
        ordinal != atomic_load_explicit(&pass->ordinal, memory_order_acquire)) {
        return pass ? pwp_fail(pass) : 0;
    }
    unsigned state = atomic_load_explicit(&pass->phase, memory_order_acquire);
    if (state != PWP_POSTED && state != PWP_POSTING) {
        return pwp_fail(pass);
    }
    unsigned desired = state == PWP_POSTED ? PWP_AWOKEN : PWP_EARLY_WAKE;
    if (!atomic_compare_exchange_strong_explicit(&pass->phase, &state, desired,
            memory_order_acq_rel, memory_order_acquire)) {
        if (state != PWP_POSTED || !pwp_transition(pass, PWP_POSTED, PWP_AWOKEN)) {
            return pwp_fail(pass);
        }
        desired = PWP_AWOKEN;
    }
    *token = (pwp_token){pass, ordinal,
        atomic_load_explicit(&pass->generation, memory_order_acquire), thread_id};
    return desired == PWP_AWOKEN ? 1 : 2;
}

static inline int pwp_token_valid(player_worker_pass *pass, uint64_t thread_id, const pwp_token *token) {
    return pass && token && token->owner == pass && thread_id == pass->worker_thread &&
        token->worker_thread == thread_id &&
        token->ordinal == atomic_load_explicit(&pass->ordinal, memory_order_acquire) &&
        token->generation == atomic_load_explicit(&pass->generation, memory_order_acquire);
}

static inline int pwp_join(player_worker_pass *pass, uint64_t thread_id, const pwp_token *token) {
    if (!pwp_token_valid(pass, thread_id, token)) {
        return pass ? pwp_fail(pass) : 0;
    }
    return pwp_transition(pass, PWP_AWOKEN, PWP_JOINED);
}

static inline int pwp_tail(player_worker_pass *pass, uint64_t thread_id, const pwp_token *token) {
    if (!pwp_token_valid(pass, thread_id, token)) {
        return pass ? pwp_fail(pass) : 0;
    }
    return pwp_transition(pass, PWP_JOINED, PWP_TAIL);
}

static inline int pwp_authorize(player_worker_pass *pass, uint64_t thread_id, uint64_t generation) {
    if (!pass || thread_id != pass->main_thread ||
        generation != atomic_load_explicit(&pass->generation, memory_order_acquire)) {
        return pass ? pwp_fail(pass) : 0;
    }
    return pwp_transition(pass, PWP_TAIL, PWP_AUTHORIZED);
}
#endif

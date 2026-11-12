#include "frame_worker_recurring.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>

#define ROUNDS UINT64_C(4000)
#define MAIN_THREAD UINT64_C(101)
#define WORKER_THREAD UINT64_C(202)

typedef struct {
    frame_worker_recurring r;
    _Atomic uint64_t armed, worker_arrived;
    uint64_t main_payload, worker_payload;
} fixture;
static void wait_for(_Atomic uint64_t *value, uint64_t wanted) {
    while (atomic_load_explicit(value, memory_order_acquire) < wanted) sched_yield();
}
static void *run_worker(void *opaque) {
    fixture *f = opaque;
    for (uint64_t generation = 5; generation < 5 + ROUNDS; ++generation) {
        wait_for(&f->armed, generation);
        if (generation & 1) {
            /* Schedule main first without an acquire operation: the worker's
             * successful protocol CAS must publish the main payload. */
            uint64_t observed;
            do {
                observed = atomic_load_explicit(&f->r.state, memory_order_relaxed);
                if (fwrr_generation(observed) != generation || !(observed & FWRR_MAIN_PARKED))
                    sched_yield();
            } while (fwrr_generation(observed) != generation || !(observed & FWRR_MAIN_PARKED));
        }
        assert(fwrr_worker_arrive(&f->r, generation, WORKER_THREAD) == FWRR_OK);
        fwrr_token token = {0};
        if (!(generation & 1))
            assert(fwrr_worker_try_enter(&f->r, generation, WORKER_THREAD, &token) == FWRR_WAIT);
        atomic_store_explicit(&f->worker_arrived, generation, memory_order_release);
        fwrr_result result;
        do {
            result = fwrr_worker_try_enter(&f->r, generation, WORKER_THREAD, &token);
            if (result == FWRR_WAIT) sched_yield();
        } while (result == FWRR_WAIT);
        assert(result == FWRR_WORKER_ENTERED);
        assert(token.owner == &f->r && token.generation == generation && token.worker_thread == WORKER_THREAD);
        assert(f->main_payload == generation);
        f->worker_payload = generation;
        assert(fwrr_worker_release(&f->r, generation, WORKER_THREAD, &token) == FWRR_OK);
    }
    return NULL;
}
static void real_threads(void) {
    fixture f = {.r = FRAME_WORKER_RECURRING_INIT};
    assert(fwrr_initialize(&f.r, MAIN_THREAD, WORKER_THREAD));
    assert(!fwrr_initialize(&f.r, MAIN_THREAD, WORKER_THREAD));
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, run_worker, &f));
    for (uint64_t generation = 5; generation < 5 + ROUNDS; ++generation) {
        assert(fwrr_arm(&f.r, generation, MAIN_THREAD) == FWRR_OK);
        atomic_store_explicit(&f.armed, generation, memory_order_release);
        if (!(generation & 1)) wait_for(&f.worker_arrived, generation);
        /* Write after all external coordination. The worker may read this only
         * after acquiring the park transition through the protocol. */
        f.main_payload = generation;
        assert(fwrr_main_park(&f.r, generation, MAIN_THREAD) == FWRR_OK);
        fwrr_result result;
        do {
            result = fwrr_main_poll(&f.r, generation, MAIN_THREAD);
            if (result == FWRR_WAIT) sched_yield();
        } while (result == FWRR_WAIT);
        assert(result == FWRR_MAIN_RETURN_ALLOWED);
        assert(f.worker_payload == generation);
    }
    assert(!pthread_join(worker, NULL));
    uint64_t state = atomic_load(&f.r.state);
    assert(fwrr_generation(state) == 4 + ROUNDS);
    assert((state & (FWRR_RELEASED | FWRR_MAIN_AUTHORIZED)) == (FWRR_RELEASED | FWRR_MAIN_AUTHORIZED));
    assert(!atomic_load(&f.r.failure));
}
static void init(frame_worker_recurring *r) {
    atomic_init(&r->state, 0); atomic_init(&r->failure, 0);
    assert(fwrr_initialize(r, MAIN_THREAD, WORKER_THREAD));
}
static fwrr_token enter(frame_worker_recurring *r, uint64_t generation) {
    fwrr_token token = {0};
    assert(fwrr_main_park(r, generation, MAIN_THREAD) == FWRR_OK);
    assert(fwrr_worker_arrive(r, generation, WORKER_THREAD) == FWRR_OK);
    assert(fwrr_worker_try_enter(r, generation, WORKER_THREAD, &token) == FWRR_WORKER_ENTERED);
    return token;
}
static void failures(void) {
    frame_worker_recurring r = FRAME_WORKER_RECURRING_INIT;
    assert(!fwrr_initialize(&r, 0, WORKER_THREAD));
    assert(!fwrr_initialize(&r, 7, 7));
    init(&r); assert(fwrr_arm(&r, 6, MAIN_THREAD) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_BAD_GENERATION);
    init(&r); assert(fwrr_arm(&r, 5, WORKER_THREAD) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_BAD_THREAD);
    init(&r); assert(fwrr_arm(&r, 5, MAIN_THREAD) == FWRR_OK);
    assert(fwrr_arm(&r, 6, MAIN_THREAD) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_BAD_ORDER);
    init(&r); assert(fwrr_arm(&r, 5, MAIN_THREAD) == FWRR_OK);
    assert(fwrr_worker_arrive(&r, 4, WORKER_THREAD) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_BAD_GENERATION);
    init(&r); assert(fwrr_arm(&r, 5, MAIN_THREAD) == FWRR_OK);
    assert(fwrr_worker_arrive(&r, 6, WORKER_THREAD) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_BAD_GENERATION);
    init(&r); assert(fwrr_arm(&r, 5, MAIN_THREAD) == FWRR_OK);
    assert(fwrr_worker_arrive(&r, 5, MAIN_THREAD) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_BAD_THREAD);
    init(&r); assert(fwrr_arm(&r, 5, MAIN_THREAD) == FWRR_OK);
    assert(fwrr_main_park(&r, 5, MAIN_THREAD) == FWRR_OK);
    assert(fwrr_main_poll(&r, 5, MAIN_THREAD) == FWRR_WAIT);
    assert(fwrr_main_park(&r, 5, MAIN_THREAD) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_REENTRY);
    init(&r); assert(fwrr_arm(&r, 5, MAIN_THREAD) == FWRR_OK);
    fwrr_token duplicate = enter(&r, 5);
    assert(fwrr_worker_try_enter(&r, 5, WORKER_THREAD, &duplicate) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_REENTRY);
    init(&r); assert(fwrr_arm(&r, 5, MAIN_THREAD) == FWRR_OK);
    fwrr_token token = enter(&r, 5), stale = token;
    assert(fwrr_worker_release(&r, 5, WORKER_THREAD, &token) == FWRR_OK);
    assert(fwrr_main_poll(&r, 5, MAIN_THREAD) == FWRR_MAIN_RETURN_ALLOWED);
    assert(fwrr_arm(&r, 6, MAIN_THREAD) == FWRR_OK);
    token = enter(&r, 6);
    assert(fwrr_worker_release(&r, 6, WORKER_THREAD, &stale) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_BAD_TOKEN);
    init(&r); assert(fwrr_arm(&r, 5, MAIN_THREAD) == FWRR_OK);
    token = enter(&r, 5);
    assert(fwrr_worker_release(&r, 5, WORKER_THREAD, &token) == FWRR_OK);
    assert(fwrr_arm(&r, 6, MAIN_THREAD) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_BAD_ORDER);
    init(&r); assert(fwrr_arm(&r, 5, MAIN_THREAD) == FWRR_OK);
    token = enter(&r, 5);
    assert(fwrr_worker_release(&r, 5, WORKER_THREAD, &token) == FWRR_OK);
    assert(fwrr_cancel(&r, 5, MAIN_THREAD, 0, 0) == FWRR_FAILED);
    assert(fwrr_main_poll(&r, 5, MAIN_THREAD) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_CANCELLED);
    init(&r); assert(fwrr_arm(&r, 5, MAIN_THREAD) == FWRR_OK);
    token = enter(&r, 5); token.owner = NULL;
    assert(fwrr_worker_release(&r, 5, WORKER_THREAD, &token) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_BAD_TOKEN);
    init(&r); assert(fwrr_arm(&r, 5, MAIN_THREAD) == FWRR_OK);
    token = enter(&r, 5); token.worker_thread = MAIN_THREAD;
    assert(fwrr_worker_release(&r, 5, WORKER_THREAD, &token) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_BAD_TOKEN);
    init(&r); assert(fwrr_arm(&r, 5, MAIN_THREAD) == FWRR_OK);
    token = enter(&r, 5);
    assert(fwrr_cancel(&r, 5, MAIN_THREAD, 0, 0) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_CANCELLED);
    assert(fwrr_worker_release(&r, 5, WORKER_THREAD, &token) == FWRR_FAILED);
    assert(fwrr_main_poll(&r, 5, MAIN_THREAD) == FWRR_FAILED);
    assert(fwrr_arm(&r, 6, MAIN_THREAD) == FWRR_FAILED);
    init(&r); assert(fwrr_arm(&r, 5, MAIN_THREAD) == FWRR_OK);
    assert(fwrr_cancel(&r, 5, WORKER_THREAD, 1, 1) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_TIMEOUT);
    init(&r); assert(fwrr_arm(&r, 5, MAIN_THREAD) == FWRR_OK);
    token = enter(&r, 5);
    assert(fwrr_worker_release(&r, 5, WORKER_THREAD, &token) == FWRR_OK);
    assert(fwrr_worker_release(&r, 5, WORKER_THREAD, &token) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_BAD_ORDER);
    init(&r);
    atomic_store(&r.state, fwrr_pack(FWRR_MAX_GENERATION, FWRR_RELEASED | FWRR_MAIN_AUTHORIZED));
    assert(fwrr_arm(&r, FWRR_MAX_GENERATION, MAIN_THREAD) == FWRR_FAILED);
    assert(atomic_load(&r.failure) == FWRR_BAD_GENERATION);
}
int main(void) { failures(); real_threads(); puts("recurring main/worker rendezvous: ok"); }

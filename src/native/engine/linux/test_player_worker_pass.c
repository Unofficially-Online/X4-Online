#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include "player_worker_pass.h"

enum { MAIN = 11, WORKER = 22 };
static void prepare(player_worker_pass *pass) {
    assert(pwp_init(pass, MAIN, WORKER, 4));
    assert(pwp_wait(pass, WORKER, 1));
    assert(pwp_arm(pass, MAIN, 5, 1, 1, 1));
    assert(pwp_post_begin(pass, MAIN, 5, 1, 1));
}

static void test_order(int early) {
    player_worker_pass pass;
    pwp_token token;
    prepare(&pass);
    if (early) {
        assert(pwp_wake(&pass, WORKER, 1, &token) == 2);
        assert(pwp_post_done(&pass, MAIN, 1));
    } else {
        assert(pwp_post_done(&pass, MAIN, 1));
        assert(pwp_wake(&pass, WORKER, 1, &token) == 1);
    }
    assert(pwp_join(&pass, WORKER, &token));
    assert(pwp_tail(&pass, WORKER, &token));
    assert(pwp_authorize(&pass, MAIN, 5));
    assert(pwp_wait(&pass, WORKER, 2));
    assert(pwp_arm(&pass, MAIN, 6, 2, 1, 1));
    assert(!pwp_join(&pass, WORKER, &token));
    assert(atomic_load(&pass.phase) == PWP_FAILED);
}

static void test_refusals(void) {
    player_worker_pass pass;
    pwp_token token;
    assert(pwp_init(&pass, MAIN, WORKER, 4));
    assert(pwp_wait(&pass, WORKER, 1));
    assert(!pwp_arm(&pass, MAIN, 5, 1, 0, 1));
    assert(atomic_load(&pass.phase) == PWP_FAILED);
    assert(pwp_init(&pass, MAIN, WORKER, 4));
    assert(pwp_wait(&pass, WORKER, 1));
    assert(!pwp_arm(&pass, MAIN, 5, 1, 1, 0));
    assert(pwp_init(&pass, MAIN, WORKER, 4));
    assert(pwp_wait(&pass, WORKER, 1));
    assert(!pwp_wake(&pass, WORKER, 1, &token));
    prepare(&pass);
    assert(!pwp_post_begin(&pass, MAIN, 5, 1, 1));
    prepare(&pass);
    assert(!pwp_wake(&pass, MAIN, 1, &token));
    prepare(&pass);
    assert(!pwp_post_done(&pass, MAIN, 0));
    prepare(&pass);
    assert(pwp_post_done(&pass, MAIN, 1));
    assert(pwp_wake(&pass, WORKER, 1, &token) == 1);
    assert(!pwp_wake(&pass, WORKER, 1, &token));
    prepare(&pass);
    assert(pwp_post_done(&pass, MAIN, 1));
    assert(pwp_wake(&pass, WORKER, 1, &token) == 1);
    assert(!pwp_tail(&pass, WORKER, &token));
    prepare(&pass);
    assert(!pwp_authorize(&pass, MAIN, 5));
    prepare(&pass);
    assert(!pwp_wake(&pass, WORKER, 2, &token));
    prepare(&pass);
    assert(pwp_post_done(&pass, MAIN, 1));
    assert(pwp_wake(&pass, WORKER, 1, &token) == 1);
    player_worker_pass foreign;
    prepare(&foreign);
    assert(pwp_post_done(&foreign, MAIN, 1));
    pwp_token foreign_token;
    assert(pwp_wake(&foreign, WORKER, 1, &foreign_token) == 1);
    assert(!pwp_join(&pass, WORKER, &foreign_token));
}

/* Interleave the exact reservation, competing refusal, and publication used
 * by wait/arm. No scheduler timing is needed to reach these states. */
static void test_failure_during_publication(void) {
    player_worker_pass pass;
    assert(pwp_init(&pass, MAIN, WORKER, 4));
    assert(pwp_transition(&pass, PWP_IDLE, PWP_WAIT_PUBLISHING));
    atomic_store_explicit(&pass.ordinal, 1, memory_order_relaxed);
    assert(!pwp_arm(&pass, MAIN, 5, 1, 1, 1));
    assert(atomic_load(&pass.phase) == PWP_FAILED);
    assert(!pwp_transition(&pass, PWP_WAIT_PUBLISHING, PWP_WAITING));
    assert(atomic_load(&pass.phase) == PWP_FAILED);

    assert(pwp_init(&pass, MAIN, WORKER, 4));
    assert(pwp_wait(&pass, WORKER, 1));
    assert(pwp_transition(&pass, PWP_WAITING, PWP_ARM_PUBLISHING));
    atomic_store_explicit(&pass.generation, 5, memory_order_relaxed);
    assert(!pwp_post_begin(&pass, MAIN, 5, 1, 1));
    assert(atomic_load(&pass.phase) == PWP_FAILED);
    assert(!pwp_transition(&pass, PWP_ARM_PUBLISHING, PWP_ARMED));
    assert(atomic_load(&pass.phase) == PWP_FAILED);
}

typedef struct {
    player_worker_pass *pass;
    pwp_token token;
    int result;
} wake_args;
static void *racing_wake(void *opaque) {
    wake_args *args = opaque;
    args->result = pwp_wake(args->pass, WORKER, 1, &args->token);
    return NULL;
}

static void test_concurrent_post_and_wake(void) {
    for (unsigned i = 0; i < 1000; ++i) {
        player_worker_pass pass;
        prepare(&pass);
        wake_args args = { .pass = &pass };
        pthread_t worker;
        assert(!pthread_create(&worker, NULL, racing_wake, &args));
        assert(pwp_post_done(&pass, MAIN, 1));
        assert(!pthread_join(worker, NULL));
        assert(args.result == 1 || args.result == 2);
        assert(pwp_join(&pass, WORKER, &args.token));
        assert(pwp_tail(&pass, WORKER, &args.token));
        assert(pwp_authorize(&pass, MAIN, 5));
    }
}

int main(void) {
    test_order(0);
    test_order(1);
    test_refusals();
    test_failure_during_publication();
    test_concurrent_post_and_wake();
    puts("worker pass policy: ok");
}

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include "player_worker_dispatch_contract.h"

static frame_worker_recurring rendezvous;
static player_worker_dispatch_contract dispatch;
static unsigned native_posts, native_waits;
static void *const semaphore = (void *)(uintptr_t)0x1234;

static void reset(void) {
    rendezvous = (frame_worker_recurring)FRAME_WORKER_RECURRING_INIT;
    dispatch = (player_worker_dispatch_contract)PLAYER_WORKER_DISPATCH_CONTRACT_INIT;
    native_posts = native_waits = 0;
    assert(fwrr_initialize_at(&rendezvous, 11, 22, 40));
    assert(pwdc_initialize(&dispatch, &rendezvous));
    assert(fwrr_arm(&rendezvous, 40, 11) == FWRR_OK);
}

static uint64_t native_wait(void *argument) {
    assert(argument == semaphore);
    ++native_waits;
    errno = EBUSY;
    return UINT64_C(0xfedcba9876543210);
}

/* These test wrappers illustrate forwarding; production must compose with the
 * already installed native adapters and authenticate exact argument/call site. */
static uint64_t wrapped_wait(void *argument, uint64_t generation, uint64_t thread) {
    uint64_t result = native_wait(argument);
    int native_errno = errno;
    assert(pwdc_wait_return(&dispatch, generation, thread));
    errno = native_errno;
    return result;
}

static uint64_t native_post(void *argument, unsigned count, int wake_before_return) {
    assert(argument == semaphore && count == 1);
    assert(errno == EDOM);
    ++native_posts;
    if (wake_before_return) {
        errno = E2BIG;
        assert(wrapped_wait(argument, 40, 22) == UINT64_C(0xfedcba9876543210));
        assert(errno == EBUSY);
        assert(pwdc_join(&dispatch, 40, 22) == 0);
    }
    errno = ENOSPC;
    return UINT64_C(0x123456789abcdef0);
}

static uint64_t wrapped_post(void *argument, unsigned count, int wake_before_return) {
    int incoming_errno = errno;
    assert(pwdc_post_begin(&dispatch, 40, 11));
    errno = incoming_errno;
    uint64_t result = native_post(argument, count, wake_before_return);
    int native_errno = errno;
    assert(pwdc_post_return(&dispatch, 40, 11));
    errno = native_errno;
    return result;
}

static void complete_fwrr(void) {
    fwrr_token token = {0};
    assert(pwdc_join(&dispatch, 40, 22) == 1);
    assert(fwrr_worker_arrive(&rendezvous, 40, 22) == FWRR_OK);
    assert(fwrr_main_park(&rendezvous, 40, 11) == FWRR_OK);
    assert(fwrr_worker_try_enter(&rendezvous, 40, 22, &token) == FWRR_WORKER_ENTERED);
    assert(fwrr_worker_release(&rendezvous, 40, 22, &token) == FWRR_OK);
    assert(fwrr_main_poll(&rendezvous, 40, 11) == FWRR_MAIN_RETURN_ALLOWED);
}

int main(void) {
    reset();
    errno = EDOM;
    assert(wrapped_post(semaphore, 1, 1) == UINT64_C(0x123456789abcdef0));
    assert(errno == ENOSPC && native_posts == 1 && native_waits == 1);
    complete_fwrr();
    assert(fwrr_arm(&rendezvous, 41, 11) == FWRR_OK);
    assert(pwdc_post_begin(&dispatch, 41, 11));
    assert(pwdc_post_return(&dispatch, 41, 11));
    assert(pwdc_wait_return(&dispatch, 41, 22));
    assert(pwdc_join(&dispatch, 41, 22) == 1);

    reset(); assert(!pwdc_wait_return(&dispatch, 40, 22));
    reset(); assert(!pwdc_post_begin(&dispatch, 40, 22));
    reset(); assert(!pwdc_post_begin(&dispatch, 41, 11));
    reset(); assert(pwdc_post_begin(&dispatch, 40, 11)); assert(!pwdc_post_begin(&dispatch, 40, 11));
    reset(); assert(pwdc_post_begin(&dispatch, 40, 11)); assert(!pwdc_wait_return(&dispatch, 39, 22));
    reset(); assert(pwdc_post_begin(&dispatch, 40, 11)); assert(!pwdc_wait_return(&dispatch, 40, 11));
    reset(); assert(pwdc_post_begin(&dispatch, 40, 11)); assert(pwdc_wait_return(&dispatch, 40, 22));
    assert(!pwdc_wait_return(&dispatch, 40, 22));
    reset(); assert(pwdc_post_begin(&dispatch, 40, 11)); assert(pwdc_post_return(&dispatch, 40, 11));
    assert(!pwdc_post_return(&dispatch, 40, 11));
    reset(); assert(pwdc_post_begin(&dispatch, 40, 11)); assert(!pwdc_unexpected_activity(&dispatch));
    assert(!pwdc_wait_return(&dispatch, 40, 22));
    reset(); assert(pwdc_post_begin(&dispatch, 40, 11));
    assert(fwrr_fail(&rendezvous, FWRR_TIMEOUT) == FWRR_FAILED);
    assert(!pwdc_wait_return(&dispatch, 40, 22));
    reset(); assert(pwdc_join(&dispatch, 40, 22) == -1);
    reset(); errno = EDOM; assert(wrapped_post(semaphore, 1, 0)); assert(wrapped_wait(semaphore, 40, 22));
    assert(pwdc_join(&dispatch, 40, 22) == 1); assert(pwdc_join(&dispatch, 40, 22) == -1);
    return 0;
}

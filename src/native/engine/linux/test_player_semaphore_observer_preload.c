#define _GNU_SOURCE
#include <assert.h>
#include "player_semaphore_observer_preload.c"
static unsigned calls[4], dispatch_events;
static void resident_callback(unsigned kind, unsigned returning, uintptr_t caller,
                              uint64_t argument, uint64_t result, unsigned movement) {
    (void)kind; (void)returning; (void)caller; (void)argument; (void)result; (void)movement;
    ++dispatch_events;
    errno = EILSEQ; /* wrapper must restore native/incoming errno across callbacks */
}
static uint64_t fake_constructor(void *sem, unsigned initial, unsigned maximum) {
    assert(sem == TARGET_SEMAPHORE && initial == 0 && maximum == 1 && errno == EDOM);
    ++calls[0]; errno = EBUSY; return UINT64_C(0x1111222233334444);
}
static uint64_t fake_post(void *sem, unsigned count) {
    assert((sem == TARGET_SEMAPHORE || sem == (void *)42) && count == 1 && errno == EDOM);
    ++calls[1]; errno = ENOSPC; return UINT64_C(0x5555666677778888);
}
static uint64_t fake_wait(void *sem) {
    assert(sem == TARGET_SEMAPHORE && errno == EDOM);
    ++calls[2]; errno = EAGAIN; return UINT64_C(0x9999aaaabbbbcccc);
}
static uint64_t fake_timed(void *sem, unsigned timeout) {
    assert(sem == TARGET_SEMAPHORE && timeout == 7 && errno == EDOM);
    ++calls[3]; errno = ERANGE; return UINT64_C(0xddddeeeeffff0000);
}
int main(void) {
    int pipefd[2]; assert(!pipe(pipefd)); receipt_fd = pipefd[1];
    originals[0] = (void *)fake_constructor; originals[1] = (void *)fake_post;
    originals[2] = (void *)fake_wait; originals[3] = (void *)fake_timed;
    errno = EDOM; assert(x4mp_semaphore_observer_constructor(TARGET_SEMAPHORE, 0, 1) == UINT64_C(0x1111222233334444)); assert(errno == EBUSY);
    errno = EDOM; assert(x4mp_semaphore_observer_post(TARGET_SEMAPHORE, 1) == UINT64_C(0x5555666677778888)); assert(errno == ENOSPC);
    assert(x4mp_semaphore_observer_movement_scope(1));
    errno = EDOM; assert(x4mp_semaphore_observer_wait(TARGET_SEMAPHORE) == UINT64_C(0x9999aaaabbbbcccc)); assert(errno == EAGAIN);
    assert(x4mp_semaphore_observer_movement_scope(0));
    errno = EDOM; assert(x4mp_semaphore_observer_timed_wait(TARGET_SEMAPHORE, 7) == UINT64_C(0xddddeeeeffff0000)); assert(errno == ERANGE);
    errno = EDOM; assert(x4mp_semaphore_observer_post((void *)42, 1) == UINT64_C(0x5555666677778888)); assert(errno == ENOSPC);
    assert(calls[0] == 1 && calls[1] == 2 && calls[2] == 1 && calls[3] == 1);
    assert(!x4mp_semaphore_observer_incomplete() && atomic_load(&receipt_count) == 8);
    player_semaphore_observer_status status = {0};
    assert(x4mp_semaphore_observer_status(&status) && status.tokens == 4 && status.receipts == 8 &&
           !status.incomplete && status.constructor_state == 2);
    errno = EDOM; assert(x4mp_semaphore_observer_checkpoint(219)); assert(errno == EDOM);
    assert(atomic_load(&observation_state) == OBSERVATION_CLOSED);
    assert(x4mp_semaphore_dispatch_register(resident_callback));
    assert(!x4mp_semaphore_dispatch_register(resident_callback));
    errno = EDOM; assert(x4mp_semaphore_observer_post(TARGET_SEMAPHORE, 1) == UINT64_C(0x5555666677778888));
    assert(dispatch_events == 2);
    assert(errno == ENOSPC && calls[1] == 3 && atomic_load(&receipt_count) == 8);
    atomic_store(&observation_state, 0);
    atomic_store(&receipt_count, MAX_RECEIPTS);
    errno = EDOM; assert(x4mp_semaphore_observer_post(TARGET_SEMAPHORE, 1) == UINT64_C(0x5555666677778888));
    assert(errno == ENOSPC && calls[1] == 4 && x4mp_semaphore_observer_incomplete());
    assert(dispatch_events == 4);
    atomic_store(&observation_state, 1);
    atomic_store(&incomplete, 0);
    atomic_store(&receipt_count, 8);
    assert(!x4mp_semaphore_observer_checkpoint(220));
    assert(x4mp_semaphore_observer_incomplete());
    close(pipefd[1]); char output[4096]; ssize_t size = read(pipefd[0], output, sizeof output - 1);
    assert(size > 0); output[size] = 0;
    assert(strstr(output, "INCOMPLETE reason=capacity"));
    assert(strstr(output, "closed=1 active=0 tokens=4 receipts=8 incomplete=0 constructor=2 complete=1"));
    assert(strstr(output, "closed=1 active=1 tokens=5 receipts=8 incomplete=1"));
    assert(strstr(output, "movement=1"));
    for (unsigned i = 0; i < 4; ++i) {
        char needle[32]; snprintf(needle, sizeof needle, "phase=enter kind=%u", i); assert(strstr(output, needle));
        snprintf(needle, sizeof needle, "phase=return kind=%u", i); assert(strstr(output, needle));
    }
    close(pipefd[0]);
    return 0;
}

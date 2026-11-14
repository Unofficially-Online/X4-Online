#define WORKER_WAIT_OBSERVER_TEST
#include "linux_worker_wait_composed_observer.c"
#include <assert.h>
static unsigned calls;
static void *seen;
static unsigned seen_count;
static uint64_t fake_wait(void *semaphore) {
    assert(semaphore == (void *)NATIVE_SEMAPHORE && errno == EDOM);
    errno = ERANGE; return 0x55;
}
static uint64_t fake_post(void *semaphore, unsigned count) {
    calls++; seen = semaphore; seen_count = count; assert(errno == EDOM);
    errno = ERANGE; return 123;
}
int main(void) {
    post_original = fake_post;
    enabled = 0; errno = EDOM;
    assert(observe_post((void *)NATIVE_SEMAPHORE, 1) == 123);
    assert(calls == 1 && seen == (void *)NATIVE_SEMAPHORE && seen_count == 1 && errno == ERANGE);
    unsigned char first[16] = {0}, second[16] = {0}, patched[16];
    first[13] = 0xe8; first[14] = 0x2e; first[15] = 0xea;
    second[0] = 0x9f; second[1] = 0xff; second[2] = 0xe9;
    assert(cross_call_patch(first, second, 0x20251ad, 0x40003be0, patched));
    assert(patched[0] != second[0] && patched[1] != second[1]);
    assert(!memcmp(patched + 2, second + 2, 14));
    assert(!cross_call_patch(first, second, 0x20251ad, 0x40003be1, patched));
    assert(!cross_call_patch(first, second, 0x20251ad, UINT64_C(0x100003be0), patched));
    begin_ns = 1; end_ns = UINT64_MAX; enabled = 1;
    int fds[2]; assert(pipe(fds) == 0); assert(dup2(fds[1], 2) >= 0); close(fds[1]);
    errno = EDOM; assert(observe_post((void *)NATIVE_SEMAPHORE, 1) == 123);
    assert(calls == 2 && errno == ERANGE);
    wait_original = fake_wait; errno = EDOM;
    assert(alternate_wait((void *)NATIVE_SEMAPHORE) == 0x55 && errno == ERANGE);
    errno = EDOM; assert(observe_frame_early((void *)NATIVE_SEMAPHORE, 1) == 123 && errno == ERANGE);
    errno = EDOM; assert(observe_frame_normal((void *)NATIVE_SEMAPHORE, 1) == 123 && errno == ERANGE);
    errno = EDOM; assert(observe_frame_state((void *)NATIVE_SEMAPHORE, 1) == 123 && errno == ERANGE);
    errno = EAGAIN;
    unsigned long token = x4_worker_wait_trace_enter((void *)NATIVE_SEMAPHORE);
    assert(token && errno == EAGAIN);
    errno = EBUSY; end_ns = 1; x4_worker_wait_trace_return((void *)NATIVE_SEMAPHORE, token, 0x1234);
    assert(errno == EBUSY);
    char output[2048]; ssize_t size = read(fds[0], output, sizeof output - 1);
    assert(size > 0); output[size] = 0;
    assert(strstr(output, "site=post_shutdown phase=enter semaphore_match=1"));
    assert(strstr(output, "site=post_shutdown phase=returned semaphore_match=1 raw_return=000000000000007b"));
    assert(strstr(output, "site=alternate phase=returned semaphore_match=1 raw_return=0000000000000055"));
    assert(strstr(output, "site=post_frame_early phase=returned semaphore_match=1"));
    assert(strstr(output, "site=post_frame_normal phase=returned semaphore_match=1"));
    assert(strstr(output, "site=post_frame_state phase=returned semaphore_match=1"));
    assert(strstr(output, "site=movement phase=enter semaphore_match=1"));
    assert(strstr(output, "site=movement phase=returned semaphore_match=1 raw_return=0000000000001234"));
    end_ns = UINT64_MAX;
    atomic_store(&events, 512);
    assert(!x4_worker_wait_trace_enter((void *)NATIVE_SEMAPHORE));
    char overflow[128]; size = read(fds[0], overflow, sizeof overflow - 1);
    assert(size > 0); overflow[size] = 0;
    assert(!strcmp(overflow, "X4 WORKER_WAIT OVERFLOW v=2\n"));
    return 0;
}

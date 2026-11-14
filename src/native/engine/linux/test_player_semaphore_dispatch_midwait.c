#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include "player_semaphore_observer_preload.c"
static pthread_mutex_t wait_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wait_condition = PTHREAD_COND_INITIALIZER;
static unsigned entered, released, callback_entries, callback_returns;
static uint64_t native_wait(void *semaphore) {
    assert(semaphore == TARGET_SEMAPHORE && errno == EDOM);
    assert(!pthread_mutex_lock(&wait_mutex));
    entered = 1;
    assert(!pthread_cond_broadcast(&wait_condition));
    while (!released) assert(!pthread_cond_wait(&wait_condition, &wait_mutex));
    assert(!pthread_mutex_unlock(&wait_mutex));
    errno = EAGAIN;
    return UINT64_C(0x9999aaaabbbbcccc);
}
static void callback(unsigned kind, unsigned returning, uintptr_t caller,
                     uint64_t argument, uint64_t result, unsigned movement) {
    assert(kind == 2 && caller && !argument && movement == 1);
    if (returning) {
        ++callback_returns;
        assert(result == UINT64_C(0x9999aaaabbbbcccc));
    } else ++callback_entries;
    errno = EILSEQ;
}
static void *worker(void *unused) {
    (void)unused;
    assert(x4mp_semaphore_observer_movement_scope(1));
    errno = EDOM;
    assert(x4mp_semaphore_observer_wait(TARGET_SEMAPHORE) == UINT64_C(0x9999aaaabbbbcccc));
    assert(errno == EAGAIN);
    assert(x4mp_semaphore_observer_movement_scope(0));
    return NULL;
}
int main(void) {
    receipt_fd = open("/dev/null", O_WRONLY);
    assert(receipt_fd >= 0);
    atomic_store(&constructor_state, 2);
    originals[2] = (void *)native_wait;
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, worker, NULL));
    assert(!pthread_mutex_lock(&wait_mutex));
    while (!entered) assert(!pthread_cond_wait(&wait_condition, &wait_mutex));
    assert(x4mp_semaphore_dispatch_register(callback));
    /* Closing the ledger cannot disable the resident callback. */
    atomic_fetch_or(&observation_state, OBSERVATION_CLOSED);
    released = 1;
    assert(!pthread_cond_broadcast(&wait_condition));
    assert(!pthread_mutex_unlock(&wait_mutex));
    assert(!pthread_join(thread, NULL));
    assert(!callback_entries && callback_returns == 1);
    assert(atomic_load(&observation_state) == OBSERVATION_CLOSED);
    close(receipt_fd);
    puts("provider registration during native wait: synthetic return-only callback ok");
}

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gate_player_recurring_receipt.h"
typedef struct { const char *name; uintptr_t va; size_t size; const char *sha; } lx_site;
static struct { int active; } stats = {1};
static int boot_phase = 3, pins_good = 1, read_good = 1, worker_good = 1;
static uint64_t stock_observer_worker_thread = 7;
static pthread_mutex_t pause_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t counter_a, counter_b;
static uint64_t GetCurrentThreadId(void) { return 7; }
static void *gate_tls_self(void) { return &stats; }
static int gate_tls_worker(void *tls) { return tls && worker_good; }
static int site_ok(const lx_site *site) { return pins_good && site->size && strlen(site->sha) == 64; }
static void *address(uintptr_t va) {
    if (va == 0x3db8288) return &pause_mutex;
    if (va == 0x3db82e4) return &counter_a;
    if (va == 0x3db82e8) return &counter_b;
    assert(0);
    return NULL;
}
#define LX(va) address(va)
static int read_local(const void *address_value, void *output, size_t size) {
    if (!read_good) return 0;
    memcpy(output, address_value, size);
    return 1;
}
#include "gate_player_recurring_pause.h"

static _Atomic int locked, release_lock;
static void *hold_mutex(void *unused) {
    (void)unused;
    assert(!pthread_mutex_lock(&pause_mutex));
    atomic_store(&locked, 1);
    while (!atomic_load(&release_lock)) sched_yield();
    assert(!pthread_mutex_unlock(&pause_mutex));
    return NULL;
}
int main(void) {
    player_recurring_receipt receipt = {0};
    player_recurring_pause_sample(&receipt);
    assert(receipt.pause_available == 1 && receipt.paused == 0 && !receipt.pause_error);
    counter_b = 1;
    receipt = (player_recurring_receipt){0};
    player_recurring_pause_sample(&receipt);
    assert(receipt.pause_available == 1 && receipt.paused == 1 && !receipt.pause_error);
    pthread_t other;
    assert(!pthread_create(&other, NULL, hold_mutex, NULL));
    while (!atomic_load(&locked)) sched_yield();
    receipt = (player_recurring_receipt){0};
    player_recurring_pause_sample(&receipt);
    assert(!receipt.pause_available && receipt.paused == -1 && receipt.pause_error == EBUSY);
    atomic_store(&release_lock, 1);
    assert(!pthread_join(other, NULL));
    read_good = 0;
    receipt = (player_recurring_receipt){0};
    player_recurring_pause_sample(&receipt);
    assert(!receipt.pause_available && receipt.paused == -1 && !pthread_mutex_trylock(&pause_mutex));
    assert(!pthread_mutex_unlock(&pause_mutex));
    pins_good = 0;
    receipt = (player_recurring_receipt){0};
    player_recurring_pause_sample(&receipt);
    assert(!receipt.pause_available && receipt.paused == -1);
    puts("pause receipt uses nonblocking mutex and reports unavailable PASS");
}

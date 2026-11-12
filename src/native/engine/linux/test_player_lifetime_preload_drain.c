#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include "player_lifetime_preload.c"

static _Atomic unsigned native_entered;
static _Atomic unsigned native_release;
static unsigned char *stub;
static int ship;

static uintptr_t blocked_native(void *object, uintptr_t marker, int value) {
    assert(object == &ship && marker == 0x1234 && value == 7);
    atomic_store_explicit(&native_entered, 1, memory_order_release);
    while (!atomic_load_explicit(&native_release, memory_order_acquire)) {
        sched_yield();
    }
    return marker + (unsigned)value;
}

static void *enter_before_publication(void *unused) {
    (void)unused;
    uintptr_t value = ((uintptr_t (*)(void *, uintptr_t, int))stub)(&ship, 0x1234, 7);
    assert(value == 0x123b);
    return NULL;
}

static void *release_after_publication(void *unused) {
    (void)unused;
    while (!atomic_load_explicit(&binding_slot, memory_order_seq_cst)) {
        sched_yield();
    }
    assert(atomic_load_explicit(&plp_entry_count, memory_order_seq_cst) == 1);
    assert(atomic_load_explicit(&registration, memory_order_acquire) == 1);
    atomic_store_explicit(&native_release, 1, memory_order_release);
    return NULL;
}

static void fatal(void *object) {
    (void)object;
    _exit(81);
}

int main(void) {
    unsigned char *pages = mmap(NULL, PLP_PAGE * 2, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(pages != MAP_FAILED);
    memcpy(pages, &plp_original[0], 8);
    const unsigned char tail[] = {0x5d, 0x41, 0x5c, 0x41, 0x5d, 0xff, 0x25, 0, 0, 0, 0};
    memcpy(pages + 8, tail, sizeof tail);
    uintptr_t callback = (uintptr_t)blocked_native;
    memcpy(pages + 8 + sizeof tail, &callback, sizeof callback);
    memset(pages + PLP_PAGE, 0xcc, PLP_PAGE);
    plp_emit(pages + PLP_PAGE, (uintptr_t)&binding_slot, (uintptr_t)&plp_entry_count,
             0, (uintptr_t)pages, 0, (uintptr_t)plp_call_original0);
    plp_original_target0 = pages + PLP_PAGE + PLP_TRAMPOLINE_OFFSET;
    assert(!mprotect(pages, PLP_PAGE * 2, PROT_READ | PROT_EXEC));
    stub = pages + PLP_PAGE;
    information.installed = 1;
    pthread_t entrant, release;
    assert(!pthread_create(&entrant, NULL, enter_before_publication, NULL));
    while (!atomic_load_explicit(&native_entered, memory_order_acquire)) {
        sched_yield();
    }
    assert(atomic_load_explicit(&plp_entry_count, memory_order_seq_cst) == 1);
    assert(!pthread_create(&release, NULL, release_after_publication, NULL));
    assert(x4mp_player_lifetime_prepare(&ship, fatal, fatal));
    assert(!pthread_join(entrant, NULL));
    assert(!pthread_join(release, NULL));
    assert(atomic_load_explicit(&plp_entry_count, memory_order_seq_cst) == 0);
    assert(atomic_load_explicit(&registration, memory_order_acquire) == 2);
    puts("pre-publication native entry drained before preparation returns PASS");
}

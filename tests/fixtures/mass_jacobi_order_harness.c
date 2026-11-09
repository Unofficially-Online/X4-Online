/* Production transaction with simulated Win32 responses. No game instructions,
 * OS thread suspension, image files, or executable pages are used. */
#include <assert.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t DWORD;
typedef int32_t LONG;
typedef size_t SIZE_T;
typedef uintptr_t HANDLE;
typedef struct {
    DWORD ContextFlags;
    uintptr_t Rip;
} CONTEXT;
typedef struct {
    volatile LONG held;
} SRWLOCK;
typedef struct {
    void *BaseAddress;
    size_t RegionSize;
    DWORD State;
    DWORD Protect;
} MEMORY_BASIC_INFORMATION;
#define SRWLOCK_INIT {0}
#define CONTEXT_CONTROL 1u
#define PAGE_EXECUTE_READ 0x20u
#define PAGE_EXECUTE_READWRITE 0x40u
#define MEM_COMMIT 0x1000u

static unsigned char *image;
static struct {
    int active;
    uint64_t issued;
} stats;
static DWORD ui_thread = 7;
static int boot_mode;
static int boot_phase;
static unsigned pmath_frozen_count;
static HANDLE pmath_frozen[2] = {0, 1};
static int frozen;
static int freeze_refused;
static int context_refused;
static int hash_refused;
static int release_pending;
static int release_stuck;
static int thaw_refused;
static int resume_lock_owner;
static uintptr_t paused_rip;
static unsigned protection_calls;
static unsigned write_calls;
static unsigned flush_calls;
static unsigned freeze_calls;
static unsigned fail_protection_call;
static unsigned partial_write_call;
static unsigned fail_write_call;
static unsigned fail_flush_call;
static unsigned foreign_write_call;
static unsigned fail_flush_from;
static int stop_at_pins;
static int stop_at_write;
static int stop_at_thaw;
static int stop_at_publish;
static DWORD protection = PAGE_EXECUTE_READ;
static pthread_mutex_t observer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t observer_condition = PTHREAD_COND_INITIALIZER;
static pthread_t observer_thread;
static _Thread_local int is_observer;
static int observer_at_thaw;
static int observer_at_pins;
static int observer_active;
static int observer_waiting;
static int owner_released;
static int observer_saw_transient;
static int observer_saw_idle;
static int observer_check;
static int observer_owned;

static void after_exchange(volatile LONG *target, LONG value);
static void after_release(SRWLOCK *lock);
static void *observe_gate_after_thaw(void *unused);
static void start_gate_observer(void);
static void request_test_stop(void);
static void resume_test_lock_owner(void);
static LONG InterlockedCompareExchange(volatile LONG *at, LONG desired, LONG expected) {
    return __sync_val_compare_and_swap(at, expected, desired);
}
static LONG InterlockedExchange(volatile LONG *at, LONG desired) {
    const LONG previous = __sync_lock_test_and_set(at, desired);
    after_exchange(at, desired);
    return previous;
}
static LONG InterlockedIncrement(volatile LONG *at) {
    return __sync_add_and_fetch(at, 1);
}
static int TryAcquireSRWLockExclusive(SRWLOCK *lock) {
    const int acquired = __sync_bool_compare_and_swap(&lock->held, 0, 1);
    if (!acquired && is_observer) {
        assert(!pthread_mutex_lock(&observer_mutex));
        observer_waiting = 1;
        assert(!pthread_cond_broadcast(&observer_condition));
        assert(!pthread_mutex_unlock(&observer_mutex));
    }
    return acquired;
}
static void ReleaseSRWLockExclusive(SRWLOCK *lock) {
    assert(lock->held);
    __sync_lock_release(&lock->held);
    after_release(lock);
}
static DWORD GetCurrentThreadId(void) {
    return 7;
}
static HANDLE GetCurrentProcess(void) {
    return 1;
}
static int pmath_cleanup_clear(void) {
    return !release_pending;
}
static int pmath_retry_pending(void) {
    if (frozen || release_stuck) return 0;
    release_pending = 0;
    if (resume_lock_owner) resume_test_lock_owner();
    return 1;
}
static int pmath_freeze(void) {
    assert(!frozen);
    ++freeze_calls;
    if (freeze_refused || release_pending) return 0;
    frozen = 1;
    return 1;
}
static int pmath_thaw(void) {
    assert(frozen);
    frozen = 0;
    if (thaw_refused) release_pending = 1;
    if (stop_at_thaw) {
        stop_at_thaw = 0;
        request_test_stop();
    }
    if (observer_at_thaw) {
        observer_at_thaw = 0;
        start_gate_observer();
    }
    return !release_pending;
}
static void Sleep(unsigned duration) {
    assert(duration == 1);
    if (is_observer) {
        assert(!pthread_mutex_lock(&observer_mutex));
        while (!owner_released) assert(!pthread_cond_wait(&observer_condition, &observer_mutex));
        assert(!pthread_mutex_unlock(&observer_mutex));
    }
}
static int GetThreadContext(HANDLE handle, CONTEXT *context) {
    (void)handle;
    assert(frozen);
    context->Rip = paused_rip;
    return !context_refused;
}
static size_t VirtualQuery(const void *at, MEMORY_BASIC_INFORMATION *memory, size_t size) {
    memory->BaseAddress = (void *)((uintptr_t)at & ~(uintptr_t)4095);
    memory->RegionSize = 4096;
    memory->State = MEM_COMMIT;
    memory->Protect = protection;
    return size;
}
static int VirtualProtect(void *at, size_t size, DWORD desired, DWORD *previous) {
    (void)at;
    (void)size;
    assert(frozen);
    ++protection_calls;
    if (protection_calls == fail_protection_call) return 0;
    *previous = protection;
    protection = desired;
    return 1;
}
static int WriteProcessMemory(HANDLE process, void *to, const void *from, size_t size, SIZE_T *written) {
    (void)process;
    assert(frozen && protection == PAGE_EXECUTE_READWRITE);
    ++write_calls;
    if (write_calls == fail_write_call) {
        *written = 0;
        return 0;
    }
    *written = write_calls == partial_write_call ? size / 2 : size;
    memcpy(to, from, *written);
    if (write_calls == foreign_write_call) ((unsigned char *)to)[0] ^= 0x80;
    if (stop_at_write) {
        stop_at_write = 0;
        request_test_stop();
    }
    return *written == size;
}
static int FlushInstructionCache(HANDLE process, const void *at, size_t size) {
    (void)process;
    (void)at;
    (void)size;
    assert(frozen);
    ++flush_calls;
    return flush_calls != fail_flush_call && (!fail_flush_from || flush_calls < fail_flush_from);
}
static int read_local(const void *, void *, size_t);
static int hash_ok(const void *, unsigned, const char *);
#include "lockstep_mass_jacobi_order.h"

static void *observe_gate_after_thaw(void *unused) {
    (void)unused;
    is_observer = 1;
    observer_saw_transient = mass_jacobi_dirty && !mass_jacobi_installed;
    observer_saw_idle = !mass_jacobi_installed && !mass_jacobi_dirty && !mass_jacobi_rx_pending &&
        !mass_jacobi_cache_pending && !mass_jacobi_cleanup_uncertain;
    observer_check = mass_jacobi_check();
    observer_owned = mass_jacobi_owned();
    return NULL;
}
static void start_gate_observer(void) {
    observer_active = 1;
    assert(!pthread_create(&observer_thread, NULL, observe_gate_after_thaw, NULL));
    assert(!pthread_mutex_lock(&observer_mutex));
    while (!observer_waiting) assert(!pthread_cond_wait(&observer_condition, &observer_mutex));
    assert(!pthread_mutex_unlock(&observer_mutex));
}
static void after_release(SRWLOCK *lock) {
    if (lock == &mass_jacobi_lifecycle_lock && observer_active && !is_observer) {
        assert(!pthread_mutex_lock(&observer_mutex));
        owner_released = 1;
        assert(!pthread_cond_broadcast(&observer_condition));
        assert(!pthread_mutex_unlock(&observer_mutex));
    }
}
static void request_test_stop(void) {
    mass_jacobi_restore();
}
static void resume_test_lock_owner(void) {
    resume_lock_owner = 0;
    ReleaseSRWLockExclusive(&mass_jacobi_lifecycle_lock);
}
static void after_exchange(volatile LONG *target, LONG value) {
    if (target == &mass_jacobi_installed && value == 1 && stop_at_publish) {
        stop_at_publish = 0;
        request_test_stop();
    }
}
static int read_local(const void *from, void *to, size_t size) {
    memcpy(to, from, size);
    mass_jacobi_unpatch_copy(from, to, size);
    return 1;
}
static int hash_ok(const void *at, unsigned size, const char *sha) {
    assert(!frozen && at == image + MASS_JACOBI_FUNCTION_RVA && size == MASS_JACOBI_FUNCTION_SIZE);
    assert(!strcmp(sha, MASS_JACOBI_FUNCTION_SHA));
    if (observer_at_pins) {
        observer_at_pins = 0;
        start_gate_observer();
    }
    if (stop_at_pins) {
        stop_at_pins = 0;
        request_test_stop();
    }
    return !hash_refused;
}
static void reset(void) {
    assert(!frozen && !mass_jacobi_lifecycle_lock.held);
    memcpy(image + MASS_JACOBI_BLOCK_RVA, mass_jacobi_original, MASS_JACOBI_BLOCK_SIZE);
    memcpy(mass_jacobi_owned_bytes, mass_jacobi_original + MASS_JACOBI_PATCH_OFFSET, MASS_JACOBI_PATCH_SIZE);
    stats.active = 1;
    stats.issued = 0;
    ui_thread = 7;
    boot_mode = 1;
    boot_phase = 0;
    mass_jacobi_installed = 0;
    mass_jacobi_restored = 0;
    mass_jacobi_dirty = 0;
    mass_jacobi_rx_pending = 0;
    mass_jacobi_cache_pending = 0;
    mass_jacobi_stop_requested = 0;
    mass_jacobi_cleanup_uncertain = 0;
    mass_jacobi_known = 1;
    mass_jacobi_attempts = 0;
    mass_jacobi_busy = 0;
    mass_jacobi_failures = 0;
    mass_jacobi_stage = "idle";
    protection_calls = 0;
    write_calls = 0;
    flush_calls = 0;
    freeze_calls = 0;
    fail_protection_call = 0;
    partial_write_call = 0;
    fail_write_call = 0;
    fail_flush_call = 0;
    foreign_write_call = 0;
    fail_flush_from = 0;
    freeze_refused = 0;
    context_refused = 0;
    hash_refused = 0;
    release_pending = 0;
    release_stuck = 0;
    thaw_refused = 0;
    resume_lock_owner = 0;
    stop_at_pins = 0;
    stop_at_write = 0;
    stop_at_thaw = 0;
    stop_at_publish = 0;
    pmath_frozen_count = 0;
    paused_rip = 0;
    protection = PAGE_EXECUTE_READ;
    observer_at_thaw = 0;
    observer_at_pins = 0;
    observer_active = 0;
    observer_waiting = 0;
    owner_released = 0;
    observer_saw_transient = 0;
    observer_saw_idle = 0;
    observer_check = 0;
    observer_owned = 0;
}
static void test_normal_and_foreign(void) {
    reset();
    assert(mass_jacobi_arm() && mass_jacobi_state() == 1 && mass_jacobi_owned() && mass_jacobi_check());
    assert(!frozen && protection == PAGE_EXECUTE_READ && mass_jacobi_pins());
    const unsigned writes = write_calls;
    stats.issued = 10;
    boot_phase = 3;
    assert(mass_jacobi_arm() && write_calls == writes);
    for (unsigned i = 0; i < MASS_JACOBI_BLOCK_SIZE; i++) {
        image[MASS_JACOBI_BLOCK_RVA + i] ^= 0x80;
        assert(!mass_jacobi_owned() && !mass_jacobi_check());
        mass_jacobi_restore();
        assert(mass_jacobi_installed && !mass_jacobi_restored && write_calls == writes);
        assert(!strcmp(mass_jacobi_stage, "restore_foreign"));
        image[MASS_JACOBI_BLOCK_RVA + i] ^= 0x80;
        mass_jacobi_failures = 0;
    }
    mass_jacobi_restore();
    assert(mass_jacobi_restored && mass_jacobi_clean() && mass_jacobi_state() == 0);
    assert(!frozen && protection == PAGE_EXECUTE_READ);
    char status[400];
    assert(mass_jacobi_status(status, sizeof(status)) && strstr(status, "restored=1 dirty=0 rx_pending=0"));
    assert(!mass_jacobi_status(status, 5));
}
static void test_all_window_boundaries(void) {
    const uintptr_t start = (uintptr_t)(image + MASS_JACOBI_PATCH_RVA);
    for (unsigned offset = 0; offset <= MASS_JACOBI_PATCH_SIZE; offset++) {
        reset();
        pmath_frozen_count = 1;
        paused_rip = start + offset;
        if (offset == 0 || offset == MASS_JACOBI_PATCH_SIZE) {
            assert(mass_jacobi_arm());
            mass_jacobi_restore();
            assert(mass_jacobi_restored);
        } else {
            assert(!mass_jacobi_arm() && !write_calls && mass_jacobi_busy == 200);
            assert(mass_jacobi_state() == 0 && !strcmp(mass_jacobi_stage, "busy"));
        }
        assert(!frozen);
    }
    reset();
    pmath_frozen_count = 1;
    context_refused = 1;
    assert(!mass_jacobi_arm() && !write_calls && !frozen && !strcmp(mass_jacobi_stage, "context"));
    reset();
    freeze_refused = 1;
    assert(!mass_jacobi_arm() && !write_calls && !strcmp(mass_jacobi_stage, "freeze"));
}
static void test_partial_write_and_recovery(void) {
    reset();
    partial_write_call = 1;
    assert(!mass_jacobi_arm() && !mass_jacobi_dirty && !mass_jacobi_rx_pending && mass_jacobi_state() == 0);
    assert(write_calls == 2 && !strcmp(mass_jacobi_stage, "write_partial") && !frozen);
    reset();
    partial_write_call = 1;
    fail_write_call = 2;
    assert(!mass_jacobi_arm() && mass_jacobi_dirty && mass_jacobi_known && !mass_jacobi_rx_pending);
    assert(!strcmp(mass_jacobi_stage, "rollback_failed") && !mass_jacobi_owned());
    fail_write_call = 0;
    mass_jacobi_restore();
    assert(mass_jacobi_restored && mass_jacobi_state() == 0 && !mass_jacobi_dirty && !frozen);
    reset();
    assert(mass_jacobi_arm());
    partial_write_call = 2;
    mass_jacobi_restore();
    assert(mass_jacobi_installed && !mass_jacobi_restored && mass_jacobi_state() == 1);
    partial_write_call = 0;
    mass_jacobi_restore();
    assert(mass_jacobi_restored && mass_jacobi_state() == 0);
    reset();
    foreign_write_call = 1;
    assert(!mass_jacobi_arm() && mass_jacobi_dirty && !mass_jacobi_known && !strcmp(mass_jacobi_stage, "write_foreign"));
    const unsigned char foreign = image[MASS_JACOBI_PATCH_RVA];
    mass_jacobi_restore();
    assert(write_calls == 1 && image[MASS_JACOBI_PATCH_RVA] == foreign && !mass_jacobi_restored);
    assert(!strcmp(mass_jacobi_stage, "restore_foreign"));
}
static void test_protection_and_flush_failures(void) {
    reset();
    protection = PAGE_EXECUTE_READWRITE;
    assert(!mass_jacobi_arm() && !write_calls && !protection_calls);
    reset();
    fail_protection_call = 1;
    assert(!mass_jacobi_arm() && !write_calls && !mass_jacobi_dirty && !mass_jacobi_rx_pending);
    reset();
    fail_protection_call = 2;
    assert(!mass_jacobi_arm() && mass_jacobi_dirty && mass_jacobi_rx_pending && !mass_jacobi_installed);
    assert(!strcmp(mass_jacobi_stage, "protect_restore") && !mass_jacobi_owned());
    assert(mass_jacobi_saved_protection == PAGE_EXECUTE_READ && protection == PAGE_EXECUTE_READWRITE);
    fail_protection_call = 0;
    mass_jacobi_restore();
    assert(mass_jacobi_restored && mass_jacobi_clean() && protection == PAGE_EXECUTE_READ);
    reset();
    fail_flush_call = 1;
    assert(!mass_jacobi_arm() && mass_jacobi_state() == 0 && mass_jacobi_clean());
    assert(write_calls == 2 && !strcmp(mass_jacobi_stage, "flush"));
    reset();
    fail_flush_from = 1;
    assert(!mass_jacobi_arm() && mass_jacobi_state() == 0 && !mass_jacobi_dirty && mass_jacobi_cache_pending);
    assert(!mass_jacobi_owned() && !strcmp(mass_jacobi_stage, "rollback_failed"));
    fail_flush_from = 0;
    mass_jacobi_restore();
    assert(mass_jacobi_restored && mass_jacobi_clean());
    reset();
    assert(mass_jacobi_arm());
    fail_protection_call = 4;
    mass_jacobi_restore();
    assert(mass_jacobi_installed && mass_jacobi_rx_pending && !mass_jacobi_dirty);
    fail_protection_call = 0;
    mass_jacobi_restore();
    assert(mass_jacobi_restored && mass_jacobi_clean());
}
static void test_stop_races_and_retained_release(void) {
    reset();
    mass_jacobi_restore();
    assert(mass_jacobi_stopping() && !mass_jacobi_arm() && !write_calls);
    for (unsigned point = 0; point < 4; point++) {
        reset();
        stop_at_pins = point == 0;
        stop_at_write = point == 1;
        stop_at_thaw = point == 2;
        stop_at_publish = point == 3;
        assert(!mass_jacobi_arm());
        assert(mass_jacobi_stopping() && mass_jacobi_clean() && mass_jacobi_state() == 0);
        assert(!mass_jacobi_lifecycle_lock.held && !frozen && !mass_jacobi_arm());
    }
    reset();
    thaw_refused = 1;
    release_stuck = 1;
    assert(!mass_jacobi_arm() && mass_jacobi_dirty && mass_jacobi_cleanup_uncertain && !mass_jacobi_installed);
    mass_jacobi_restore();
    assert(!mass_jacobi_clean() && !mass_jacobi_restored && !strcmp(mass_jacobi_stage, "cleanup_pending"));
    thaw_refused = 0;
    release_stuck = 0;
    mass_jacobi_restore();
    assert(mass_jacobi_restored && mass_jacobi_clean() && mass_jacobi_state() == 0);
    reset();
    mass_jacobi_lifecycle_lock.held = 1;
    mass_jacobi_restore();
    assert(mass_jacobi_cleanup_uncertain && !mass_jacobi_clean() && !strcmp(mass_jacobi_stage, "lifecycle_busy"));
    ReleaseSRWLockExclusive(&mass_jacobi_lifecycle_lock);
    mass_jacobi_restore();
    assert(mass_jacobi_clean());
    reset();
    mass_jacobi_lifecycle_lock.held = 1;
    release_pending = 1;
    resume_lock_owner = 1;
    mass_jacobi_restore();
    assert(mass_jacobi_clean() && !mass_jacobi_lifecycle_lock.held);
}
static void test_reader_resumed_before_publication(void) {
    reset();
    observer_at_pins = 1;
    assert(mass_jacobi_arm());
    assert(!pthread_join(observer_thread, NULL));
    assert(observer_saw_idle && observer_check && observer_owned);
    assert(!mass_jacobi_failures && !mass_jacobi_cleanup_uncertain);
    mass_jacobi_restore();
    assert(mass_jacobi_restored && mass_jacobi_clean());
    reset();
    observer_at_thaw = 1;
    assert(mass_jacobi_arm());
    assert(!pthread_join(observer_thread, NULL));
    assert(observer_saw_transient && observer_check && observer_owned);
    assert(!mass_jacobi_failures && !mass_jacobi_cleanup_uncertain);
    assert(mass_jacobi_installed && mass_jacobi_state() == 1);
    mass_jacobi_restore();
    assert(mass_jacobi_restored && mass_jacobi_clean());
    reset();
    observer_at_thaw = 1;
    thaw_refused = 1;
    release_stuck = 1;
    assert(!mass_jacobi_arm());
    assert(!pthread_join(observer_thread, NULL));
    assert(observer_saw_transient && !observer_check && !observer_owned);
    assert(mass_jacobi_cleanup_uncertain && mass_jacobi_dirty && !mass_jacobi_installed);
    thaw_refused = 0;
    release_stuck = 0;
    mass_jacobi_restore();
    assert(mass_jacobi_restored && mass_jacobi_clean());
}
static void test_arm_refusals(void) {
    reset();
    stats.active = 0;
    assert(!mass_jacobi_arm());
    stats.active = 1;
    ui_thread = 8;
    assert(!mass_jacobi_arm());
    ui_thread = 7;
    stats.issued = 1;
    assert(!mass_jacobi_arm());
    stats.issued = 0;
    boot_mode = 0;
    assert(!mass_jacobi_arm());
    boot_mode = 1;
    boot_phase = 1;
    assert(!mass_jacobi_arm());
    boot_phase = 0;
    hash_refused = 1;
    assert(!mass_jacobi_arm() && !write_calls);
    reset();
    image[MASS_JACOBI_PATCH_RVA] ^= 1;
    assert(!mass_jacobi_arm() && !write_calls);
}
int main(void) {
    image = calloc(1, MASS_JACOBI_FUNCTION_RVA + MASS_JACOBI_FUNCTION_SIZE);
    assert(image);
    test_normal_and_foreign();
    test_all_window_boundaries();
    test_partial_write_and_recovery();
    test_protection_and_flush_failures();
    test_stop_races_and_retained_release();
    test_reader_resumed_before_publication();
    test_arm_refusals();
    free(image);
    puts("mass Jacobi order production transaction failure/lifecycle tests PASS");
    return 0;
}

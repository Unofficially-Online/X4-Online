#define _GNU_SOURCE
/* Private, bounded observation only. This module does not authorize a frame.
 * The launcher owns an already-open receipt fd and authenticates the process. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "../lockstep_sha256.h"
#include "player_semaphore_original_view.h"

#define EXPORT __attribute__((visibility("default")))
#define TARGET_SEMAPHORE ((void *)(uintptr_t)0x3e59e20)
#define MAX_RECEIPTS 8192u
#define OBSERVATION_CLOSED (UINT64_C(1) << 63)
#define X4_FILE_SIZE 60459280u
static const char x4_sha[] = "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20";
static player_semaphore_observer_info observer_info;
__attribute__((visibility("protected"))) void *x4mp_semaphore_observer_originals[4];
#define originals x4mp_semaphore_observer_originals
static int receipt_fd = -1;
static _Atomic uint64_t next_token;
static _Atomic uint64_t observation_state;
static _Atomic unsigned receipt_count, incomplete, constructor_state;
static _Thread_local unsigned movement_wait_scope;
static const size_t function_lengths[4] = {0x150, 0xe0, 0xd0, 0x170};
static const char *const function_hashes[4] = {
    "67068087dfa9e4c1de02561561448ca0ffd47327a46108c2015ce5fea7f30ab2",
    "853e6d16983bd5a42c26efaf075b1a47efaf4cb82ecb4feb32c1ee183b716078",
    "651efec3dd008483223ea0d44dacd25c7a30e6273062954f1c1d621f66a09c52",
    "f6f84544f89af7bfbf5a0cc43f51289a1bcc60ef92d0191682f247344ecec069"
};

static void refuse(void) {
    static const char message[] = "X4 SEMAPHORE OBSERVER REFUSED\n";
    (void)write(STDERR_FILENO, message, sizeof message - 1);
    _exit(223);
}
static int image_matches(void) {
    int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    struct stat status;
    if (fd < 0 || fstat(fd, &status) || status.st_size != X4_FILE_SIZE) {
        if (fd >= 0) close(fd);
        return 0;
    }
    lockstep_sha256_ctx hash;
    lockstep_sha256_init(&hash);
    unsigned char buffer[65536], digest[32];
    ssize_t count;
    while ((count = read(fd, buffer, sizeof buffer)) > 0)
        lockstep_sha256_update(&hash, buffer, (size_t)count);
    close(fd);
    if (count < 0) return 0;
    lockstep_sha256_final(&hash, digest);
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i)
        if (x4_sha[2*i] != hex[digest[i] >> 4] || x4_sha[2*i+1] != hex[digest[i] & 15]) return 0;
    return 1;
}
static int function_matches(unsigned index) {
    unsigned char digest[32];
    static const char hex[] = "0123456789abcdef";
    lockstep_sha256((const void *)player_semaphore_site_addresses[index], function_lengths[index], digest);
    for (unsigned i = 0; i < 32; ++i)
        if (function_hashes[index][2*i] != hex[digest[i] >> 4] ||
            function_hashes[index][2*i+1] != hex[digest[i] & 15]) return 0;
    return 1;
}
static int single_thread(void) {
    DIR *directory = opendir("/proc/self/task");
    if (!directory) return 0;
    unsigned count = 0;
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(directory))) if (entry->d_name[0] != '.') ++count;
    int error = errno;
    if (closedir(directory)) error = 1;
    return count == 1 && !error;
}
static void emit(const char *phase, unsigned kind, uint64_t token, uintptr_t caller,
                 uint64_t argument, uint64_t result) {
    unsigned index = atomic_fetch_add_explicit(&receipt_count, 1, memory_order_relaxed);
    if (index >= MAX_RECEIPTS) {
        if (!atomic_exchange(&incomplete, 1)) {
            static const char overflow[] = "X4 SEMAPHORE OBSERVER INCOMPLETE reason=capacity\n";
            (void)write(receipt_fd, overflow, sizeof overflow - 1);
        }
        return;
    }
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) { atomic_store(&incomplete, 1); return; }
    char row[256];
    int length = snprintf(row, sizeof row,
        "X4 SEMAPHORE OBSERVER v=1 phase=%s kind=%u token=%llu tid=%ld caller=%llx arg=%llu result=%llu movement=%u ns=%llu\n",
        phase, kind, (unsigned long long)token, (long)syscall(SYS_gettid),
        (unsigned long long)caller, (unsigned long long)argument, (unsigned long long)result,
        kind == 2 ? movement_wait_scope : 0,
        (unsigned long long)now.tv_sec * 1000000000ull + (unsigned long long)now.tv_nsec);
    if (length <= 0 || (size_t)length >= sizeof row || write(receipt_fd, row, (size_t)length) != length)
        atomic_store(&incomplete, 1);
}
static uint64_t enter(unsigned kind, void *sem, uintptr_t caller, uint64_t argument) {
    if (sem != TARGET_SEMAPHORE) return 0;
    uint64_t state = atomic_load_explicit(&observation_state, memory_order_acquire);
    for (;;) {
        if (state & OBSERVATION_CLOSED) return 0;
        if (state == OBSERVATION_CLOSED - 1) { atomic_store(&incomplete, 1); return 0; }
        if (atomic_compare_exchange_weak_explicit(&observation_state, &state, state + 1,
                                                   memory_order_acq_rel, memory_order_acquire)) break;
    }
    uint64_t token = atomic_fetch_add_explicit(&next_token, 1, memory_order_relaxed) + 1;
    if (!token) { atomic_store(&incomplete, 1); atomic_fetch_sub(&observation_state, 1); return 0; }
    emit("enter", kind, token, caller, argument, 0);
    return token;
}
static void leave(unsigned kind, uint64_t token, uintptr_t caller, uint64_t argument, uint64_t result) {
    if (token) {
        emit("return", kind, token, caller, argument, result);
        atomic_fetch_sub_explicit(&observation_state, 1, memory_order_release);
    }
}

EXPORT uint64_t x4mp_semaphore_observer_constructor(void *sem, unsigned initial, unsigned maximum) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    int incoming_errno = errno;
    uint64_t token = enter(0, sem, caller, ((uint64_t)initial << 32) | maximum);
    if (token && atomic_exchange_explicit(&constructor_state, 1, memory_order_acq_rel))
        atomic_store(&incomplete, 1);
    errno = incoming_errno;
    uint64_t result = ((uint64_t (*)(void *, unsigned, unsigned))originals[0])(sem, initial, maximum);
    int native_errno = errno;
    if (token) atomic_store_explicit(&constructor_state, 2, memory_order_release);
    leave(0, token, caller, ((uint64_t)initial << 32) | maximum, result);
    errno = native_errno;
    return result;
}
#define OBSERVER_WRAPPER(NAME, INDEX, SIGNATURE, ARGUMENTS, EXTRA) \
EXPORT uint64_t NAME SIGNATURE { \
    uintptr_t caller = (uintptr_t)__builtin_return_address(0); \
    int incoming_errno = errno; \
    uint64_t token = enter(INDEX, sem, caller, EXTRA); \
    if (token && atomic_load_explicit(&constructor_state, memory_order_acquire) != 2) atomic_store(&incomplete, 1); \
    errno = incoming_errno; \
    uint64_t result = ((uint64_t (*) SIGNATURE)originals[INDEX]) ARGUMENTS; \
    int native_errno = errno; \
    leave(INDEX, token, caller, EXTRA, result); \
    errno = native_errno; \
    return result; \
}
OBSERVER_WRAPPER(x4mp_semaphore_observer_post, 1, (void *sem, unsigned count), (sem, count), count)
OBSERVER_WRAPPER(x4mp_semaphore_observer_wait, 2, (void *sem), (sem), 0)
OBSERVER_WRAPPER(x4mp_semaphore_observer_timed_wait, 3, (void *sem, unsigned timeout), (sem, timeout), timeout)
#undef OBSERVER_WRAPPER

EXPORT unsigned x4mp_semaphore_observer_info(player_semaphore_observer_info *out) {
    if (out) *out = observer_info;
    return PLAYER_SEM_OBSERVER_VERSION;
}
/* The existing lmt callsite sets this TLS marker around its one original wait.
 * It is evidence about that adapter invocation, never a frame ticket. */
EXPORT int x4mp_semaphore_observer_movement_scope(unsigned entered) {
    if (entered > 1 || movement_wait_scope == entered) {
        atomic_store(&incomplete, 1);
        return 0;
    }
    movement_wait_scope = entered;
    return 1;
}
EXPORT unsigned x4mp_semaphore_observer_incomplete(void) {
    return atomic_load(&incomplete) || atomic_load(&receipt_count) > MAX_RECEIPTS;
}
EXPORT unsigned x4mp_semaphore_observer_status(player_semaphore_observer_status *out) {
    if (!out) return 0;
    *out = (player_semaphore_observer_status){
        atomic_load(&next_token), atomic_load(&receipt_count),
        x4mp_semaphore_observer_incomplete(), atomic_load(&constructor_state)
    };
    return 1;
}
/* Close the observation interval and drain only already-entered wrappers.
 * Caller must separately authenticate its main/worker boundary. New native
 * calls continue normally after closure but are outside the observed interval. */
EXPORT int x4mp_semaphore_observer_checkpoint(uint64_t generation) {
    int incoming_errno = errno;
    uint64_t old = atomic_fetch_or_explicit(&observation_state, OBSERVATION_CLOSED, memory_order_acq_rel);
    if (!generation || (old & OBSERVATION_CLOSED)) { atomic_store(&incomplete, 1); errno = incoming_errno; return 0; }
    struct timespec start, now, pause = {0, 1000000};
    int clock_ok = !clock_gettime(CLOCK_MONOTONIC, &start);
    uint64_t active = 0;
    do {
        active = atomic_load_explicit(&observation_state, memory_order_acquire) & ~OBSERVATION_CLOSED;
        if (!active) break;
        if (!clock_ok || clock_gettime(CLOCK_MONOTONIC, &now) ||
            now.tv_sec > start.tv_sec + 1 ||
            (now.tv_sec == start.tv_sec + 1 && now.tv_nsec >= start.tv_nsec)) break;
        nanosleep(&pause, NULL);
    } while (1);
    if (active) atomic_store(&incomplete, 1);
    player_semaphore_observer_status status;
    (void)x4mp_semaphore_observer_status(&status);
    unsigned complete = !active && !status.incomplete && status.constructor_state == 2 &&
        status.tokens <= UINT64_MAX / 2 && status.receipts == status.tokens * 2;
    char row[256];
    int length = snprintf(row, sizeof row,
        "X4 SEMAPHORE OBSERVER STATUS v=1 tick=%llu owner=%ld closed=1 active=%llu tokens=%llu receipts=%u incomplete=%u constructor=%u complete=%u\n",
        (unsigned long long)generation, (long)syscall(SYS_gettid), (unsigned long long)active, (unsigned long long)status.tokens,
        status.receipts, status.incomplete, status.constructor_state, complete);
    if (length <= 0 || (size_t)length >= sizeof row || write(receipt_fd, row, (size_t)length) != length) {
        atomic_store(&incomplete, 1); complete = 0;
    }
    errno = incoming_errno;
    return complete;
}

static unsigned char *allocate_near(void) {
    for (uintptr_t address = 0x10000; address < 0x3f0000; address += PLAYER_SEM_PAGE_SIZE) {
        void *page = mmap((void *)address, PLAYER_SEM_PAGE_SIZE, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (page != MAP_FAILED) return page;
    }
    return NULL;
}
static void install(void) {
    uintptr_t wrappers[4] = {
        (uintptr_t)x4mp_semaphore_observer_constructor, (uintptr_t)x4mp_semaphore_observer_post,
        (uintptr_t)x4mp_semaphore_observer_wait, (uintptr_t)x4mp_semaphore_observer_timed_wait
    };
    for (unsigned i = 0; i < 4; ++i) {
        uintptr_t site = player_semaphore_site_addresses[i];
        if (memcmp((void *)site, player_semaphore_original[i], 9) || !function_matches(i)) refuse();
        unsigned char *page = allocate_near();
        if (!page) refuse();
        memset(page, 0xcc, PLAYER_SEM_PAGE_SIZE);
        player_semaphore_abs_jump(page, wrappers[i]);
        unsigned relocated = player_semaphore_relocated[i];
        memcpy(page + PLAYER_SEM_TRAMPOLINE_OFFSET, player_semaphore_original[i], relocated);
        player_semaphore_abs_jump(page + PLAYER_SEM_TRAMPOLINE_OFFSET + relocated, site + relocated);
        if (mprotect(page, PLAYER_SEM_PAGE_SIZE, PROT_READ | PROT_EXEC)) refuse();
        observer_info.sites[i] = (player_semaphore_observer_site){(uintptr_t)page, wrappers[i], (uintptr_t)&originals[i]};
        originals[i] = page + PLAYER_SEM_TRAMPOLINE_OFFSET;
    }
    if (!single_thread()) refuse();
    for (unsigned i = 0; i < 4; ++i) {
        uintptr_t site = player_semaphore_site_addresses[i], page = observer_info.sites[i].page;
        int64_t distance = (int64_t)page - (int64_t)(site + 5);
        if (distance < INT32_MIN || distance > INT32_MAX) refuse();
        unsigned char patch[8] = {0xe9,0,0,0,0,0x90,0x90,0x90};
        int32_t relative = (int32_t)distance;
        memcpy(patch + 1, &relative, sizeof relative);
        uint64_t expected, desired;
        memcpy(&expected, player_semaphore_original[i], 8);
        memcpy(&desired, patch, 8);
        void *base = (void *)(site & ~(uintptr_t)(PLAYER_SEM_PAGE_SIZE - 1));
        if (mprotect(base, PLAYER_SEM_PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC) ||
            !__atomic_compare_exchange_n((uint64_t *)site, &expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            refuse();
        __builtin___clear_cache((char *)site, (char *)site + 8);
        if (mprotect(base, PLAYER_SEM_PAGE_SIZE, PROT_READ | PROT_EXEC)) refuse();
    }
    observer_info.version = PLAYER_SEM_OBSERVER_VERSION;
    observer_info.installed = 1;
}
__attribute__((constructor)) static void observer_start(void) {
    const char *setting = getenv("X4MP_SEMAPHORE_OBSERVER");
    if (!setting) return;
    const char *fd_text = getenv("X4MP_SEMAPHORE_OBSERVER_FD");
    if (strcmp(setting, "1") || !fd_text || !*fd_text || !image_matches() || !single_thread() ||
        sysconf(_SC_PAGESIZE) != PLAYER_SEM_PAGE_SIZE) refuse();
    unsigned fd = 0;
    for (const char *p = fd_text; *p; ++p) {
        if (*p < '0' || *p > '9' || fd > (100000u - (unsigned)(*p - '0')) / 10) refuse();
        fd = fd * 10 + (unsigned)(*p - '0');
    }
    struct stat receipt;
    int flags = fcntl((int)fd, F_GETFL);
    if (fd < 3 || flags < 0 || fstat((int)fd, &receipt) || !S_ISREG(receipt.st_mode) ||
        !(flags & O_APPEND) || (flags & O_ACCMODE) == O_RDONLY) refuse();
    receipt_fd = (int)fd;
    install();
    static const char active[] = "X4 SEMAPHORE OBSERVER ACTIVE version=1\n";
    if (write(receipt_fd, active, sizeof active - 1) != (ssize_t)(sizeof active - 1)) refuse();
}

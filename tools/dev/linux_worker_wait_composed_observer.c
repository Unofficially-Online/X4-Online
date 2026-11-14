/* Opt-in private process-lifetime observation of the two pinned native waits.
 * This diagnostic observes entry/return only; it never changes a semaphore or
 * claims that a wait belongs to a particular frame. No late uninstall. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define NATIVE_WAIT ((void (*)(void *))(uintptr_t)0x1a23cc0u)
#define NATIVE_SEMAPHORE ((uintptr_t)0x3e59e20u)
static _Atomic unsigned events;
static uint64_t begin_ns, end_ns;
static int enabled;

static uint64_t current_ns(void) {
    struct timespec t;
    return clock_gettime(CLOCK_MONOTONIC, &t) ? 0 :
        (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static int in_window(uint64_t now, uint64_t first, uint64_t last) {
    return now >= first && now <= last;
}
/* Reserve a complete pair on entry; return is recorded even after the window. */
static unsigned long begin_pair(const char *site, void *semaphore) {
    uint64_t now = current_ns();
    if (!enabled || !in_window(now, begin_ns, end_ns)) return 0;
    unsigned pair = atomic_fetch_add(&events, 1);
    if (pair == 512) {
        static const char message[] = "X4 WORKER_WAIT OVERFLOW v=2\n";
        (void)write(STDERR_FILENO, message, sizeof message - 1);
    }
    if (pair >= 512) return 0;
    unsigned long token = (unsigned long)pair + 1;
    dprintf(STDERR_FILENO,
        "X4 WORKER_WAIT v=2 token=%lu mono_ns=%llu tid=%ld site=%s phase=enter semaphore_match=%u\n",
        token, (unsigned long long)now, syscall(SYS_gettid), site,
        (unsigned)((uintptr_t)semaphore == NATIVE_SEMAPHORE));
    return token;
}
static void end_pair(const char *site, void *semaphore, unsigned long token, uint64_t result) {
    if (!token) return;
    dprintf(STDERR_FILENO,
        "X4 WORKER_WAIT v=2 token=%lu mono_ns=%llu tid=%ld site=%s phase=returned semaphore_match=%u raw_return=%016llx\n",
        token, (unsigned long long)current_ns(), syscall(SYS_gettid), site,
        (unsigned)((uintptr_t)semaphore == NATIVE_SEMAPHORE), (unsigned long long)result);
}
static uint64_t (*wait_original)(void *) = (void *)(uintptr_t)0x1a23cc0u;
static uint64_t alternate_wait(void *semaphore) {
    int incoming = errno;
    unsigned long token = begin_pair("alternate", semaphore);
    errno = incoming;
    uint64_t result = wait_original(semaphore);
    int outgoing = errno;
    end_pair("alternate", semaphore, token, result);
    errno = outgoing;
    return result;
}
__attribute__((visibility("default"))) unsigned long x4_worker_wait_trace_enter(void *semaphore) {
    int saved = errno;
    unsigned long token = begin_pair("movement", semaphore);
    errno = saved;
    return token;
}
__attribute__((visibility("default"))) void x4_worker_wait_trace_return(void *semaphore, unsigned long token, uint64_t result) {
    int saved = errno;
    end_pair("movement", semaphore, token, result);
    errno = saved;
}
static uint64_t (*post_original)(void *, unsigned) = (void *)(uintptr_t)0x1a23be0u;
static uint64_t observe_post_site(const char *site, void *semaphore, unsigned count) {
    int incoming = errno;
    unsigned long token = begin_pair(site, semaphore);
    errno = incoming;
    uint64_t result = post_original(semaphore, count);
    int outgoing = errno;
    end_pair(site, semaphore, token, result);
    errno = outgoing;
    return result;
}

static uint64_t observe_post(void *s, unsigned n) { return observe_post_site("post_shutdown", s, n); }
static uint64_t observe_frame_early(void *s, unsigned n) { return observe_post_site("post_frame_early", s, n); }
static uint64_t observe_frame_normal(void *s, unsigned n) { return observe_post_site("post_frame_normal", s, n); }
static uint64_t observe_frame_state(void *s, unsigned n) { return observe_post_site("post_frame_state", s, n); }

/* Calls at byte 13 cross a 16-byte boundary. Their first two displacement
 * bytes must be unchanged so the trailing two can be swapped atomically. */
static int cross_call_patch(const unsigned char first[16], const unsigned char second[16],
                            uintptr_t call_address, uintptr_t target, unsigned char patched[16]) {
    if (first[13] != 0xe8) return 0;
    int64_t delta = (int64_t)target - (int64_t)(call_address + 5);
    if (delta < INT32_MIN || delta > INT32_MAX) return 0;
    int32_t displacement = (int32_t)delta;
    unsigned char bytes[4]; memcpy(bytes, &displacement, sizeof bytes);
    if (bytes[0] != first[14] || bytes[1] != first[15]) return 0;
    memcpy(patched, second, 16);
    patched[0] = bytes[2]; patched[1] = bytes[3];
    return 1;
}

/* Produce a rel32 call with all unrelated bytes unchanged. */
static int call_patch(const unsigned char original[16], unsigned offset, uintptr_t call_address,
                      uintptr_t target, unsigned char patched[16]) {
    if (offset > 11 || original[offset] != 0xe8) return 0;
    int64_t delta = (int64_t)target - (int64_t)(call_address + 5);
    if (delta < INT32_MIN || delta > INT32_MAX) return 0;
    memcpy(patched, original, 16);
    int32_t displacement = (int32_t)delta;
    memcpy(patched + offset + 1, &displacement, sizeof displacement);
    return 1;
}
#ifndef WORKER_WAIT_OBSERVER_TEST
#include <fcntl.h>
#include <dirent.h>
#include "lockstep_sha256.h"
static int number(const char *text, uint64_t *out) {
    if (!text || !*text) return 0;
    uint64_t n = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9' || n > (UINT64_MAX - (*p - '0')) / 10) return 0;
        n = n * 10 + (*p - '0');
    }
    *out = n;
    return 1;
}
static int start_time(uint64_t *out) {
    char buffer[4096];
    int fd = open("/proc/self/stat", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    ssize_t length = read(fd, buffer, sizeof buffer - 1);
    close(fd);
    if (length <= 0 || length == (ssize_t)sizeof buffer - 1) return 0;
    buffer[length] = 0;
    char *field = strrchr(buffer, ')');
    if (!field || field[1] != ' ') return 0;
    field += 2;
    for (unsigned index = 3; index < 22; ++index) {
        field = strchr(field, ' ');
        if (!field) return 0;
        ++field;
    }
    char *end = strchr(field, ' ');
    if (!end) return 0;
    *end = 0;
    return number(field, out);
}
static int image_matches(void) {
    int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    lockstep_sha256_ctx context;
    lockstep_sha256_init(&context);
    unsigned char buffer[65536], digest[32];
    ssize_t length;
    while ((length = read(fd, buffer, sizeof buffer)) > 0)
        lockstep_sha256_update(&context, buffer, (size_t)length);
    close(fd);
    if (length < 0) return 0;
    lockstep_sha256_final(&context, digest);
    static const unsigned char expected[32] = {
        0x11,0x3f,0x73,0xd7,0x45,0x70,0x08,0x4f,0x6c,0xe1,0x8a,0x65,0x9b,0x36,0x50,0x09,
        0xec,0xcf,0x57,0x9f,0xb6,0x6d,0xba,0xbb,0x5d,0xad,0x2b,0x17,0x18,0x04,0x5f,0x20};
    return !memcmp(digest, expected, sizeof digest);
}
static int code_matches(uintptr_t site, const unsigned char expected[16]) {
    return !memcmp((const void *)site, expected, 16);
}
static void emit_jump(unsigned char *stub, void *target) {
    stub[0] = 0xff;
    stub[1] = 0x25;
    memset(stub + 2, 0, 4);
    memcpy(stub + 6, &target, 8);
}
static int wait_provider_matches(void) {
    static const unsigned char expected[32] = {
        0x65,0x1e,0xfe,0xc3,0xdd,0x00,0x84,0x83,0x22,0x3e,0xa0,0xd4,0x4d,0xac,0xd2,0x5c,
        0x7a,0x30,0xe6,0x27,0x30,0x62,0x95,0x4f,0x1c,0x1d,0x62,0x1f,0x66,0xa0,0x9c,0x52};
    unsigned char actual[32];
    lockstep_sha256((const void *)(uintptr_t)0x1a23cc0u, 208, actual);
    return !memcmp(actual, expected, sizeof actual);
}
static int post_provider_matches(void) {
    static const unsigned char expected[32] = {
        0xb4,0x84,0xa3,0xc6,0x1e,0xe5,0xfe,0xa3,0x01,0xcf,0x78,0x43,0x2f,0x53,0x26,0xc3,
        0xe7,0x75,0x6f,0xb2,0x70,0x32,0xf6,0x39,0x51,0x73,0x38,0x79,0xfb,0xee,0x4e,0x44};
    unsigned char actual[32];
    lockstep_sha256((const void *)(uintptr_t)0x1a23be0u, 0xb0, actual);
    return !memcmp(actual, expected, sizeof actual);
}
static int only_this_thread(void) {
    DIR *directory = opendir("/proc/self/task");
    if (!directory) return 0;
    unsigned count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9') ++count;
    }
    closedir(directory);
    return count == 1;
}
static int swap_site(uintptr_t address, const unsigned char before[16],
                     const unsigned char after[16]) {
    unsigned __int128 expected, replacement;
    memcpy(&expected, before, 16);
    memcpy(&replacement, after, 16);
    void *page = (void *)(address & ~(uintptr_t)4095);
    if (mprotect(page, 4096, PROT_READ | PROT_WRITE | PROT_EXEC)) return 0;
    unsigned __int128 seen = __sync_val_compare_and_swap((unsigned __int128 *)address,
                                                         expected, replacement);
    if (mprotect(page, 4096, PROT_READ | PROT_EXEC)) return 0;
    return seen == expected;
}
static int install(void) {
    static const unsigned char alternate[16] = {
        0x3c,0xf4,0xf0,0x02,0xe8,0xd7,0x92,0xad,0x00,0x48,0x8b,0x05,0x90,0x01,0xe6,0x02};
    static const unsigned char post[16] = {
        0xe8,0x8b,0x20,0xa5,0x00,0x48,0x8b,0x05,0x2c,0x80,0xe4,0x02,0x48,0x8b,0x78,0x18};
    static const unsigned char post_arguments[16] = {
        0xc6,0x40,0x28,0x00,0xbe,0x01,0x00,0x00,0x00,0x48,0x8d,0x3d,0xd0,0x82,0xe8,0x02};
    static const unsigned char early_first[16] = {0x1f,0xbe,0x01,0x00,0x00,0x00,0x48,0x8d,0x3d,0x73,0x4c,0xe3,0x01,0xe8,0x2e,0xea};
    static const unsigned char early_second[16] = {0x9f,0xff,0xe9,0x3c,0xff,0xff,0xff,0x66,0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00};
    static const unsigned char normal_first[16] = {0x20,0x48,0x39,0xd0,0x0f,0x85,0x66,0x02,0x00,0x00,0xbe,0x01,0x00,0x00,0x00,0x48};
    static const unsigned char normal[16] = {0x8d,0x3d,0x2a,0x4c,0xe3,0x01,0xe8,0xe5,0xe9,0x9f,0xff,0xe8,0xa0,0xd8,0x1d,0xff};
    static const unsigned char state_first[16] = {0xd0,0xbe,0x01,0x00,0x00,0x00,0x48,0x8d,0x3d,0x13,0x4a,0xe3,0x01,0xe8,0xce,0xe7};
    static const unsigned char state_second[16] = {0x9f,0xff,0xe9,0xd5,0xfc,0xff,0xff,0x66,0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00};
    const uintptr_t alternate_address = 0xf4a9e0u;
    const uintptr_t post_address = 0xfd1b50u;
    unsigned int eax, ebx, ecx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx) || !(ecx & bit_CMPXCHG16B) ||
        sysconf(_SC_PAGESIZE) != 4096 || !only_this_thread() || !wait_provider_matches() || !post_provider_matches() ||
        !code_matches(alternate_address, alternate) || !code_matches(post_address, post) || !code_matches(0xfd1b40u, post_arguments) ||
        !code_matches(0x20251a0u, early_first) || !code_matches(0x20251b0u, early_second) ||
        !code_matches(0x20251e0u, normal_first) || !code_matches(0x20251f0u, normal) ||
        !code_matches(0x2025400u, state_first) || !code_matches(0x2025410u, state_second)) return 0;
    unsigned char *page = mmap(NULL, 0x40000, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (page == MAP_FAILED) return 0;
    emit_jump(page, (void *)alternate_wait);
    emit_jump(page + 32, (void *)observe_post);
    uintptr_t first_stub_address = ((uintptr_t)page + 0xffffu) & ~(uintptr_t)0xffffu;
    first_stub_address += 0x3be0u;
    if (first_stub_address + 0x20000u + 16u > (uintptr_t)page + 0x40000u) return 0;
    unsigned char *early_stub = (unsigned char *)first_stub_address;
    unsigned char *normal_stub = early_stub + 0x10000u;
    unsigned char *state_stub = early_stub + 0x20000u;
    emit_jump(early_stub, (void *)observe_frame_early);
    emit_jump(normal_stub, (void *)observe_frame_normal);
    emit_jump(state_stub, (void *)observe_frame_state);
    unsigned char alternate_patch[16], post_patch[16], early_patch[16], normal_patch[16], state_patch[16];
    if (!call_patch(alternate, 4, alternate_address + 4, (uintptr_t)page, alternate_patch) ||
        !call_patch(post, 0, post_address, (uintptr_t)(page + 32), post_patch) ||
        !cross_call_patch(early_first, early_second, 0x20251adu, (uintptr_t)early_stub, early_patch) ||
        !call_patch(normal, 6, 0x20251f6u, (uintptr_t)normal_stub, normal_patch) ||
        !cross_call_patch(state_first, state_second, 0x202540du, (uintptr_t)state_stub, state_patch) ||
        mprotect(page, 0x40000, PROT_READ | PROT_EXEC)) return 0;
    /* Any partial installation fails the opt-in process immediately. */
    if (!swap_site(alternate_address, alternate, alternate_patch) ||
        !swap_site(post_address, post, post_patch) ||
        !swap_site(0x20251b0u, early_second, early_patch) ||
        !swap_site(0x20251f0u, normal, normal_patch) ||
        !swap_site(0x2025410u, state_second, state_patch)) return 0;
    return only_this_thread();
}
__attribute__((constructor)) static void initialize(void) {
    int saved_errno = errno;
    const char *option = getenv("X4_DEV_WORKER_WAIT_OBSERVE");
    if (!option) {
        errno = saved_errno;
        return;
    }
    uint64_t pid, start, actual_start, first, last;
    uint64_t now = current_ns();
    if (strcmp(option, "1") || !number(getenv("X4_PRIVATE_WORKER_PID"), &pid) || pid != (uint64_t)getpid() ||
        !number(getenv("X4_PRIVATE_WORKER_START"), &start) || !start_time(&actual_start) || start != actual_start ||
        !number(getenv("X4_PRIVATE_WORKER_BEGIN_NS"), &first) || !number(getenv("X4_PRIVATE_WORKER_END_NS"), &last) ||
        !first || last <= first || last - first > UINT64_C(480000000000) || now > last || !image_matches()) {
        static const char message[] = "X4 WORKER_WAIT REFUSED v=1\n";
        (void)write(STDERR_FILENO, message, sizeof message - 1);
        _exit(224);
    }
    begin_ns = first;
    end_ns = last;
    enabled = 1;
    if (!install()) {
        static const char message[] = "X4 WORKER_WAIT REFUSED v=1\n";
        (void)write(STDERR_FILENO, message, sizeof message - 1);
        _exit(224);
    }
    dprintf(2, "X4 WORKER_WAIT_COMPOSED ACTIVE v=2 pid=%llu start=%llu begin_ns=%llu end_ns=%llu max_pairs=512\n",
            (unsigned long long)pid, (unsigned long long)start,
            (unsigned long long)first, (unsigned long long)last);
    errno = saved_errno;
}
#endif

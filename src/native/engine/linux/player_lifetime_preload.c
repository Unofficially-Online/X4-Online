#define _GNU_SOURCE
/* Opt-in disposable diagnostic; see player_lifetime_preload_emit.h. The two
 * entry detours are installed before main only after a single-thread check.
 * Registered objects, callbacks, binding and stubs must live until _exit. */
#include <dirent.h>
#include <errno.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <sched.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "../lockstep_sha256.h"
#include "player_lifetime_preload_emit.h"
#define EXPORT __attribute__((visibility("default")))
#define PLP_FILE_SIZE 60459280u
static const char image_sha[] = "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20";
static const char *fde_sha[2] = {"1192e8098cfaf5f739be66ef0d032da4bf25c0f0464be38a003b3cdd4e4f50c8",
                                  "0c89cab7cb46b07e7b029854f2d6d49d71ceea7934aee8a0ab5b1ac351080ab2"};
static const size_t fde_size[2] = {668,1268};
static _Atomic(const plp_binding *) binding_slot;
static _Atomic unsigned registration;
static _Atomic uint32_t construction_replacements;
__attribute__((visibility("hidden"))) _Atomic uint32_t plp_entry_count;
__attribute__((visibility("hidden"))) void *plp_original_target0;
__attribute__((visibility("hidden"))) void *plp_original_target1;
#include "player_lifetime_preload_wrappers.h"
static plp_binding construction_binding;
static plp_binding final_binding;
static plp_information information;
static void refuse(void) {
    static const char message[] = "X4 PLAYER LIFETIME PRELOAD REFUSED\n";
    (void)write(2, message, sizeof message - 1);
    _exit(223);
}
static int digest_matches(const unsigned char digest[32], const char *expected) {
    static const char hex[] = "0123456789abcdef";
    for (unsigned index = 0; index < 32; ++index) {
        if (hex[digest[index] >> 4] != expected[2 * index] ||
            hex[digest[index] & 15] != expected[2 * index + 1]) {
            return 0;
        }
    }
    return 1;
}
static int image_matches(void) {
    int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    struct stat status;
    if (fd < 0 || fstat(fd, &status) || status.st_size != PLP_FILE_SIZE) {
        if (fd >= 0) {
            close(fd);
        }
        return 0;
    }
    lockstep_sha256_ctx context;
    lockstep_sha256_init(&context);
    unsigned char buffer[65536], digest[32];
    ssize_t count;
    while ((count = read(fd, buffer, sizeof buffer)) > 0) {
        lockstep_sha256_update(&context, buffer, (size_t)count);
    }
    close(fd);
    if (count < 0) {
        return 0;
    }
    lockstep_sha256_final(&context, digest);
    return digest_matches(digest, image_sha);
}
static int single_thread(void) {
    DIR *directory = opendir("/proc/self/task");
    if (!directory) {
        return 0;
    }
    unsigned count = 0;
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] != '.') {
            ++count;
        }
    }
    int read_error = errno;
    int close_error = closedir(directory);
    return count == 1 && !read_error && !close_error;
}
static int is_rx(uintptr_t address) {
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        return 0;
    }
    char line[1024], permissions[5];
    unsigned long start, end;
    int found = 0;
    while (fgets(line, sizeof line, maps)) {
        if (sscanf(line, "%lx-%lx %4s", &start, &end, permissions) == 3 &&
            address >= start && address < end) {
            found = !strcmp(permissions, "r-xp");
            break;
        }
    }
    fclose(maps);
    return found;
}
static unsigned char *allocate_near(void) {
    for (uintptr_t address = 0x10000; address < 0x3f0000; address += PLP_PAGE) {
        void *page = mmap((void *)address, PLP_PAGE, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (page != MAP_FAILED) {
            return page;
        }
    }
    return NULL;
}
EXPORT unsigned x4mp_player_lifetime_info(plp_information *out) {
    if (out) {
        *out = information;
    }
    return PLP_VERSION;
}
/* Called only outside a native hook. Timeout is fatal to the experiment: the
 * binding remains published, so no caller may retry or revert it. */
static int player_lifetime_drain_entries(void) {
    struct timespec start, now;
    if (clock_gettime(CLOCK_MONOTONIC, &start)) {
        return 0;
    }
    while (atomic_load_explicit(&plp_entry_count, memory_order_seq_cst)) {
        if (clock_gettime(CLOCK_MONOTONIC, &now) || now.tv_sec > start.tv_sec + 1 ||
            (now.tv_sec == start.tv_sec + 1 && now.tv_nsec >= start.tv_nsec)) {
            return 0;
        }
        sched_yield();
    }
    return 1;
}

/* Prepare publishes a resident immutable construction binding. Only the
 * calling thread may replace the held ship until complete publishes the final
 * immutable binding. The caller must terminate if either operation fails. */
EXPORT int x4mp_player_lifetime_prepare(void *ship, void (*ship_fatal)(void *),
                                        void (*controller_fatal)(void *)) {
    unsigned expected = 0;
    long thread = syscall(SYS_gettid);
    if (!information.installed || !ship || !ship_fatal || !controller_fatal || thread <= 0 ||
        (unsigned long)thread > UINT32_MAX ||
        !atomic_compare_exchange_strong(&registration, &expected, 1)) {
        return 0;
    }
    construction_binding = (plp_binding){
        .ship = ship,
        .controller = NULL,
        .ship_fatal = ship_fatal,
        .controller_fatal = controller_fatal,
        .owner_thread = (uint32_t)thread,
        .construction = 1,
        .replacement_counter = &construction_replacements,
    };
    atomic_store_explicit(&binding_slot, &construction_binding, memory_order_seq_cst);
    if (!player_lifetime_drain_entries()) {
        return 0;
    }
    atomic_store_explicit(&registration, 2, memory_order_release);
    return 1;
}

EXPORT int x4mp_player_lifetime_complete(void *controller) {
    if (!controller || atomic_load_explicit(&registration, memory_order_acquire) != 2 ||
        (uint32_t)syscall(SYS_gettid) != construction_binding.owner_thread ||
        atomic_load_explicit(&construction_replacements, memory_order_acquire) != 1) {
        return 0;
    }
    void *ship = NULL;
    memcpy(&ship, (unsigned char *)controller + 0x20, sizeof ship);
    if (ship != construction_binding.ship) {
        return 0;
    }
    unsigned expected = 2;
    if (!atomic_compare_exchange_strong(&registration, &expected, 3)) {
        return 0;
    }
    final_binding = construction_binding;
    final_binding.controller = controller;
    final_binding.construction = 0;
    atomic_store_explicit(&binding_slot, &final_binding, memory_order_release);
    atomic_store_explicit(&registration, 4, memory_order_release);
    return 1;
}

/* Compatibility for existing diagnostic callers. This does not protect the
 * interval before registration; use prepare/complete around native Take. */
EXPORT int x4mp_player_lifetime_register(void *ship, void *controller,
                                         void (*ship_fatal)(void *), void (*controller_fatal)(void *)) {
    unsigned expected = 0;
    if (!information.installed || !ship || !controller || !ship_fatal || !controller_fatal ||
        !atomic_compare_exchange_strong(&registration, &expected, 3)) {
        return 0;
    }
    final_binding = (plp_binding){
        .ship = ship,
        .controller = controller,
        .ship_fatal = ship_fatal,
        .controller_fatal = controller_fatal,
    };
    atomic_store_explicit(&binding_slot, &final_binding, memory_order_release);
    atomic_store_explicit(&registration, 4, memory_order_release);
    return 1;
}
__attribute__((constructor)) static void player_lifetime_init(void) {
    const char *enabled = getenv("X4MP_PLAYER_LIFETIME_PRELOAD");
    if (!enabled || !*enabled || !strcmp(enabled, "0")) {
        return;
    }
    if (strcmp(enabled, "1") || !image_matches() || sysconf(_SC_PAGESIZE) != PLP_PAGE)
        refuse();
    /* Full mapped function hashes make the following byte relocation valid. */
    for (unsigned index = 0; index < 2; ++index) {
        unsigned char digest[32];
        uintptr_t site = plp_sites[index];
        if (!is_rx(site) || !is_rx(site + fde_size[index] - 1) ||
            memcmp((const void *)site, &plp_original[index], 8)) {
            refuse();
        }
        lockstep_sha256((const void *)site, fde_size[index], digest);
        if (!digest_matches(digest, fde_sha[index])) {
            refuse();
        }
        unsigned char *page = allocate_near();
        if (!page) {
            refuse();
        }
        memset(page, 0xcc, PLP_PAGE);
        uintptr_t wrapper = (uintptr_t)(index ? plp_call_original1 : plp_call_original0);
        plp_emit(page, (uintptr_t)&binding_slot, (uintptr_t)&plp_entry_count, index, site, PLP_VTABLE, wrapper);
        if (index) {
            plp_original_target1 = page + PLP_TRAMPOLINE_OFFSET;
        } else {
            plp_original_target0 = page + PLP_TRAMPOLINE_OFFSET;
        }
        information.wrappers[index] = (const void *)wrapper;
        information.original_slots[index] = index ? (const void *)&plp_original_target1 :
                                                    (const void *)&plp_original_target0;
        if (mprotect(page, PLP_PAGE, PROT_READ | PROT_EXEC) || !is_rx((uintptr_t)page)) {
            refuse();
        }
        information.pages[index] = page;
    }
    information.version = PLP_VERSION;
    information.binding_slot = &binding_slot;
    information.entry_count = &plp_entry_count;
    if (!single_thread()) {
        refuse();
    }
    for (unsigned index = 0; index < 2; ++index) {
        uintptr_t site = plp_sites[index];
        int64_t delta = (int64_t)(uintptr_t)information.pages[index] - (int64_t)(site + 5);
        if (delta < INT32_MIN || delta > INT32_MAX) {
            refuse();
        }
        unsigned char patch[8] = {0xe9,0,0,0,0,0x90,0x90,0x90};
        int32_t rel = (int32_t)delta;
        memcpy(patch + 1, &rel, 4);
        uintptr_t base = site & ~(uintptr_t)(PLP_PAGE - 1);
        if (mprotect((void *)base, PLP_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC)) {
            refuse();
        }
        uint64_t desired, expected = plp_original[index];
        memcpy(&desired, patch, 8);
        if (!__atomic_compare_exchange_n((uint64_t *)site, &expected, desired, 0,
                                         __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            refuse();
        }
        __builtin___clear_cache((char *)site, (char *)site + 8);
        if (mprotect((void *)base, PLP_PAGE, PROT_READ | PROT_EXEC) || !is_rx(site)) {
            refuse();
        }
    }
    information.installed = 1;
    static const char active[] = "X4 PLAYER LIFETIME PRELOAD ACTIVE version=5\n";
    (void)write(STDERR_FILENO, active, sizeof active - 1);
}

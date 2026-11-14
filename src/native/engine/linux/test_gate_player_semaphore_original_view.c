#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>
#include "../lockstep_sha256.h"
#include "player_semaphore_original_view.h"
static unsigned char entries[4][512];
static unsigned maps_opens;
static const char *maps_contents;
static void (*mid_transaction_attack)(void);
static FILE *counted_fopen(const char *path, const char *mode) {
    if (!strcmp(path, "/proc/self/maps")) {
        ++maps_opens;
        if (maps_contents) return fmemopen((void *)maps_contents, strlen(maps_contents), "r");
    }
    return fopen(path, mode);
}
static unsigned char *fake_lx(uintptr_t address) {
    for (unsigned i = 0; i < 4; ++i) if (address == player_semaphore_site_addresses[i]) return entries[i];
    return NULL;
}
#define LX(address) fake_lx(address)
static int gate_read_local(const void *address, void *out, size_t size) {
    unsigned char *fake = fake_lx((uintptr_t)address);
    if (fake) { memcpy(out, fake, size); return 1; }
    struct iovec local = {out, size}, remote = {(void *)address, size};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)size;
}
static int gate_maps_find(uintptr_t address, char permissions[5], char *path, size_t capacity) {
    FILE *file = counted_fopen("/proc/self/maps", "r"); if (!file) return 0;
    char line[PATH_MAX + 128]; int found = 0;
    while (fgets(line, sizeof line, file)) {
        unsigned long start, end; char perms[5], pathname[PATH_MAX] = "";
        int fields = sscanf(line, "%lx-%lx %4s %*s %*s %*s %4095[^\n]", &start, &end, perms, pathname);
        if (fields >= 3 && address >= start && address < end) {
            memcpy(permissions, perms, 5);
            if (path && capacity) { snprintf(path, capacity, "%s", pathname); }
            found = 1; break;
        }
    }
    fclose(file); return found;
}
static void hex32(const unsigned char digest[32], char output[65]) {
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) { output[i*2] = hex[digest[i] >> 4]; output[i*2+1] = hex[digest[i] & 15]; }
    output[64] = 0;
}
static int hash_ok(const void *data, size_t size, const char *expected) {
    static const size_t lengths[4] = {0x150, 0xe0, 0xd0, 0x170};
    static const char *const hashes[4] = {
        "67068087dfa9e4c1de02561561448ca0ffd47327a46108c2015ce5fea7f30ab2",
        "853e6d16983bd5a42c26efaf075b1a47efaf4cb82ecb4feb32c1ee183b716078",
        "651efec3dd008483223ea0d44dacd25c7a30e6273062954f1c1d621f66a09c52",
        "f6f84544f89af7bfbf5a0cc43f51289a1bcc60ef92d0191682f247344ecec069"
    };
    for (unsigned i = 0; i < 4; ++i) if (size == lengths[i] && !strcmp(expected, hashes[i])) {
        const unsigned char *bytes = data;
        if (memcmp(bytes, player_semaphore_original[i], 9)) return 0;
        for (size_t j = 9; j < size; ++j) if (bytes[j] != 0x72) return 0;
        return 1;
    }
    unsigned char digest[32]; char actual[65]; lockstep_sha256(data, size, digest); hex32(digest, actual);
    if (size == 32 && mid_transaction_attack) { void (*attack)(void) = mid_transaction_attack; mid_transaction_attack = NULL; attack(); }
    return !strcmp(actual, expected);
}
#define fopen counted_fopen
#include "gate_player_semaphore_original_view.h"
#undef fopen

static void test_commit_mapping_order(void) {
    player_semaphore_maps_transaction view;
    char permissions[5];
    maps_contents = "1000-2000 r-xp 0 00:00 0\n3000-4000 r--p 0 00:00 0\n";
    assert(player_semaphore_maps_begin(&view));
    assert(player_semaphore_maps_find(&view, 0x1001, permissions, NULL, 0));
    assert(player_semaphore_maps_commit(&view));
    /* The used mapping is unchanged; an unrelated overlapping or regressed
     * row still makes the fresh maps input inconsistent and must fail. */
    maps_contents = "1000-2000 r-xp 0 00:00 0\n1500-2500 r--p 0 00:00 0\n";
    assert(!player_semaphore_maps_commit(&view));
    maps_contents = "1000-2000 r-xp 0 00:00 0\n500-900 r--p 0 00:00 0\n";
    assert(!player_semaphore_maps_commit(&view));
    free(view.rows);
    maps_contents = NULL;
}

static void (*drift_slot)(void);
static void (*configure_provider)(const player_semaphore_observer_info *);
static player_semaphore_observer_info attack_info;
static unsigned char *attack_page, *attack_wrapper;
static void attack_slot(void) { drift_slot(); }
static void attack_metadata(void) { player_semaphore_observer_info bad = attack_info; bad.installed = 0; configure_provider(&bad); }
static void attack_permissions(void) { assert(!mprotect(attack_page, 4096, PROT_READ | PROT_WRITE | PROT_EXEC)); }
static void attack_unmap(void) { assert(!munmap(attack_page, 4096)); }
static void attack_entry(void) { entries[2][0] ^= 1; }
static void attack_tail(void) { entries[0][20] ^= 1; }
static void attack_provider(void) {
    void *page = (void *)((uintptr_t)attack_wrapper & ~(uintptr_t)4095);
    assert(!mprotect(page, 4096, PROT_READ | PROT_WRITE | PROT_EXEC));
    *attack_wrapper ^= 1;
    assert(!mprotect(page, 4096, PROT_READ | PROT_EXEC));
}
static int legacy_hash(void *unused, const void *bytes, size_t length, const char *sha) {
    (void)unused; return hash_ok(bytes, length, sha);
}
/* Exact previous orchestration, retained only to measure maps opens against the
 * replacement on the same provider and detours. */
static int legacy_pin(uintptr_t site, size_t length, const char *sha) {
    void *symbol = dlsym(RTLD_DEFAULT, "x4mp_semaphore_observer_info"); Dl_info owner = {0};
    if (!player_semaphore_pinned_provider(symbol, &owner)) return 0;
    player_semaphore_observer_info info = {0};
    if (((unsigned (*)(player_semaphore_observer_info *))symbol)(&info) != 1) return 0;
    player_semaphore_pin_context context = {(uintptr_t)owner.dli_fbase, NULL};
    if (!player_semaphore_observer_validate(&info, player_semaphore_read, player_semaphore_owned, &context)) return 0;
    static const size_t sizes[4] = {0x150,0xe0,0xd0,0x170};
    static const char *const hashes[4] = {
        "67068087dfa9e4c1de02561561448ca0ffd47327a46108c2015ce5fea7f30ab2",
        "853e6d16983bd5a42c26efaf075b1a47efaf4cb82ecb4feb32c1ee183b716078",
        "651efec3dd008483223ea0d44dacd25c7a30e6273062954f1c1d621f66a09c52",
        "f6f84544f89af7bfbf5a0cc43f51289a1bcc60ef92d0191682f247344ecec069"};
    for (unsigned i=0; i<4; ++i)
        if (!player_semaphore_original_range(&info, player_semaphore_site_addresses[i], sizes[i], hashes[i],
            player_semaphore_read, player_semaphore_owned, legacy_hash, &context)) return 0;
    return player_semaphore_original_range(&info, site, length, sha, player_semaphore_read, player_semaphore_owned, legacy_hash, &context);
}
int main(int argc, char **argv) {
    assert(argc == 2 || argc == 3);
    test_commit_mapping_order();
    void *provider = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL); assert(provider);
    if (argc == 3) {
        Dl_info owner = {0};
        assert(player_semaphore_pinned_provider(dlsym(provider, "x4mp_semaphore_observer_info"), &owner));
        return 0;
    }
    player_semaphore_observer_info info = {.version = 1, .installed = 1};
    const char *names[4] = {"x4mp_semaphore_observer_constructor", "x4mp_semaphore_observer_post", "x4mp_semaphore_observer_wait", "x4mp_semaphore_observer_timed_wait"};
    for (unsigned i = 0; i < 4; ++i) {
        uintptr_t site = player_semaphore_site_addresses[i];
        memset(entries[i], 0x72, sizeof entries[i]); memcpy(entries[i], player_semaphore_original[i], 9);
        unsigned char *page = mmap((void *)(uintptr_t)(0x3000000 + i * 4096), 4096, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        assert(page != MAP_FAILED);
        info.sites[i] = (player_semaphore_observer_site){(uintptr_t)page, (uintptr_t)dlsym(provider, names[i]), 0};
        assert(info.sites[i].wrapper);
        memset(page, 0xcc, 4096); player_semaphore_abs_jump(page, info.sites[i].wrapper);
        unsigned length = player_semaphore_relocated[i];
        memcpy(page + PLAYER_SEM_TRAMPOLINE_OFFSET, player_semaphore_original[i], length);
        player_semaphore_abs_jump(page + PLAYER_SEM_TRAMPOLINE_OFFSET + length, site + length);
        assert(!mprotect(page, 4096, PROT_READ | PROT_EXEC));
    }
    void (*configure)(const player_semaphore_observer_info *) = dlsym(provider, "test_semaphore_configure"); assert(configure); configure(&info);
    player_semaphore_mapping parsed;
    assert(!player_semaphore_maps_row("malformed\n", &parsed));
    assert(!player_semaphore_maps_row("2000-1000 r-xp 0 00:00 0\n", &parsed));
    assert(player_semaphore_maps_row("1000-2000 r-xp 0 00:00 0\n", &parsed) && parsed.anonymous);
    assert(player_semaphore_maps_row("1000-2000 r-xp 0 00:00 0 /foreign\n", &parsed) && !parsed.anonymous);
    unsigned char original_hash[32]; char sha[65]; lockstep_sha256(entries[1], 32, original_hash); hex32(original_hash, sha);
    assert(player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha));
    setenv("X4MP_SEMAPHORE_OBSERVER", "1", 1);
    assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha));
    for (unsigned i = 0; i < 4; ++i) {
        int32_t relative = (int32_t)(info.sites[i].page - (player_semaphore_site_addresses[i] + 5));
        entries[i][0] = 0xe9; memcpy(entries[i] + 1, &relative, 4); memset(entries[i] + 5, 0x90, 3);
    }
    maps_opens = 0;
    assert(legacy_pin(player_semaphore_site_addresses[1], 32, sha));
    unsigned legacy_opens = maps_opens;
    maps_opens = 0;
    assert(player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha));
    assert(maps_opens == 2 && legacy_opens >= 98);
    printf("semaphore maps reads: legacy=%u transaction=%u\n", legacy_opens, maps_opens);
    /* Inject changes after the requested range hash; every cached view must
     * fail its final authentication or consistency check. */
    drift_slot = dlsym(provider, "test_semaphore_drift_slot"); assert(drift_slot);
    configure_provider = configure; attack_info = info;
    attack_page = (unsigned char *)info.sites[0].page;
    attack_wrapper = (unsigned char *)info.sites[1].wrapper;
    mid_transaction_attack = attack_metadata;
    assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha)); configure(&info);
    mid_transaction_attack = attack_slot;
    assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha)); configure(&info);
    mid_transaction_attack = attack_permissions;
    assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha));
    assert(!mprotect(attack_page, 4096, PROT_READ | PROT_EXEC));
    mid_transaction_attack = attack_entry;
    assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha)); entries[2][0] ^= 1;
    mid_transaction_attack = attack_tail;
    assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha)); entries[0][20] ^= 1;
    mid_transaction_attack = attack_provider;
    assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha)); attack_provider();
    unsigned char page_copy[4096]; memcpy(page_copy, attack_page, sizeof page_copy);
    mid_transaction_attack = attack_unmap;
    assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha));
    int foreign = memfd_create("foreign-semaphore-page", MFD_CLOEXEC); assert(foreign >= 0);
    assert(!ftruncate(foreign, 4096));
    assert(mmap(attack_page, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED_NOREPLACE, foreign, 0) == attack_page);
    memcpy(attack_page, page_copy, sizeof page_copy);
    assert(!mprotect(attack_page, 4096, PROT_READ | PROT_EXEC));
    assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha));
    assert(!munmap(attack_page, 4096)); close(foreign);
    assert(mmap(attack_page, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == attack_page);
    memcpy(attack_page, page_copy, sizeof page_copy);
    assert(!mprotect(attack_page, 4096, PROT_READ | PROT_EXEC));
    assert(player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha));
    /* Provider-supplied foreign wrapper and read-only original slots fail. */
    player_semaphore_observer_info foreign_info = info; foreign_info.sites[1].wrapper = (uintptr_t)main;
    configure(&foreign_info); assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha)); configure(&info);
    uintptr_t slot_address = (uintptr_t)dlsym(provider, "x4mp_semaphore_observer_originals");
    void *slot_page = (void *)(slot_address & ~(uintptr_t)4095);
    assert(!mprotect(slot_page, 4096, PROT_READ));
    assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha));
    assert(!mprotect(slot_page, 4096, PROT_READ | PROT_WRITE));
    void (*drift)(void) = dlsym(provider, "test_semaphore_drift_slot"); assert(drift); drift();
    assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha)); configure(&info);
    unsigned char *wrapper = (unsigned char *)info.sites[1].wrapper;
    void *wrapper_page = (void *)((uintptr_t)wrapper & ~(uintptr_t)4095);
    assert(!mprotect(wrapper_page, 4096, PROT_READ | PROT_WRITE | PROT_EXEC));
    *wrapper ^= 1;
    assert(!mprotect(wrapper_page, 4096, PROT_READ | PROT_EXEC));
    assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha));
    assert(!mprotect(wrapper_page, 4096, PROT_READ | PROT_WRITE | PROT_EXEC));
    *wrapper ^= 1;
    assert(!mprotect(wrapper_page, 4096, PROT_READ | PROT_EXEC));
    entries[2][0] ^= 1; assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha)); entries[2][0] ^= 1;
    entries[0][20] ^= 1; assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha)); entries[0][20] ^= 1;
    entries[1][10] ^= 1; assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha)); entries[1][10] ^= 1;
    setenv("X4MP_SEMAPHORE_OBSERVER_SHA256", "wrong", 1); assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha));
    return 0;
}

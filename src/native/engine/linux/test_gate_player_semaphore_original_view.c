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
#include <unistd.h>
#include "../lockstep_sha256.h"
#include "player_semaphore_original_view.h"
static unsigned char entries[4][512];
static unsigned char *fake_lx(uintptr_t address) {
    for (unsigned i = 0; i < 4; ++i) if (address == player_semaphore_site_addresses[i]) return entries[i];
    return NULL;
}
#define LX(address) fake_lx(address)
static int gate_read_local(const void *address, void *out, size_t size) {
    unsigned char *fake = fake_lx((uintptr_t)address);
    memcpy(out, fake ? fake : address, size); return 1;
}
static int gate_maps_find(uintptr_t address, char permissions[5], char *path, size_t capacity) {
    FILE *file = fopen("/proc/self/maps", "r"); if (!file) return 0;
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
    unsigned char digest[32]; char actual[65]; lockstep_sha256(data, size, digest); hex32(digest, actual); return !strcmp(actual, expected);
}
#include "gate_player_semaphore_original_view.h"
int main(int argc, char **argv) {
    assert(argc == 2 || argc == 3);
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
    unsigned char original_hash[32]; char sha[65]; lockstep_sha256(entries[1], 32, original_hash); hex32(original_hash, sha);
    assert(player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha));
    setenv("X4MP_SEMAPHORE_OBSERVER", "1", 1);
    assert(!player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha));
    for (unsigned i = 0; i < 4; ++i) {
        int32_t relative = (int32_t)(info.sites[i].page - (player_semaphore_site_addresses[i] + 5));
        entries[i][0] = 0xe9; memcpy(entries[i] + 1, &relative, 4); memset(entries[i] + 5, 0x90, 3);
    }
    assert(player_semaphore_original_pin(player_semaphore_site_addresses[1], 32, sha));
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

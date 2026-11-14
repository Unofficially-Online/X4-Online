#ifndef X4_PLAYER_SEMAPHORE_ORIGINAL_VIEW_H
#define X4_PLAYER_SEMAPHORE_ORIGINAL_VIEW_H

/* Engine-free description of an observer's four provider-entry detours.
 * Ownership, module-file pins and wrapper-code pins are the caller's job;
 * provider-supplied pointers or hashes alone do not establish ownership. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>

#define PLAYER_SEM_OBSERVER_VERSION 1u
#define PLAYER_SEM_PAGE_SIZE 4096u
#define PLAYER_SEM_TRAMPOLINE_OFFSET 256u
#define PLAYER_SEM_OBSERVER_SITES 4u

typedef struct {
    uintptr_t page;
    uintptr_t wrapper;
    uintptr_t original_slot;
} player_semaphore_observer_site;

typedef struct {
    unsigned version;
    unsigned installed;
    player_semaphore_observer_site sites[PLAYER_SEM_OBSERVER_SITES];
} player_semaphore_observer_info;

/* A snapshot is meaningful as a closed ledger only after the caller has
 * independently established quiescence. Atomic counters alone do not do so. */
typedef struct {
    uint64_t tokens;
    unsigned receipts, incomplete, constructor_state;
} player_semaphore_observer_status;

typedef int (*player_semaphore_read_fn)(void *context, uintptr_t address, void *out, size_t size);
typedef int (*player_semaphore_owner_fn)(void *context, uintptr_t page, uintptr_t wrapper, uintptr_t original_slot);
typedef int (*player_semaphore_hash_fn)(void *context, const void *bytes, size_t size, const char *sha);

static const uintptr_t player_semaphore_site_addresses[PLAYER_SEM_OBSERVER_SITES] = {
    UINT64_C(0x1a23a90), UINT64_C(0x1a23be0), UINT64_C(0x1a23cc0), UINT64_C(0x1a23d90)
};
static const unsigned char player_semaphore_original[PLAYER_SEM_OBSERVER_SITES][9] = {
    {0x41,0x55,0x41,0x89,0xf5,0x31,0xf6,0x41,0x54},
    {0x41,0x55,0x49,0x89,0xfd,0x41,0x54,0x55,0x89},
    {0x55,0x53,0x48,0x89,0xfb,0x48,0x83,0xec,0x28},
    {0x41,0x54,0x55,0x89,0xf5,0x53,0x48,0x89,0xfb}
};
static const unsigned player_semaphore_relocated[PLAYER_SEM_OBSERVER_SITES] = {9,8,9,9};

static inline void player_semaphore_abs_jump(unsigned char *destination, uintptr_t target) {
    static const unsigned char jump[6] = {0xff,0x25,0,0,0,0};
    memcpy(destination, jump, sizeof jump);
    memcpy(destination + sizeof jump, &target, sizeof target);
}

/* Rebuild and compare each whole RX page and every live entry. The two jumps
 * are deterministic: entry -> wrapper; trampoline -> original + whole length. */
static inline int player_semaphore_observer_validate(const player_semaphore_observer_info *info,
        player_semaphore_read_fn read_memory, player_semaphore_owner_fn owner, void *context) {
    if (!info || info->version != PLAYER_SEM_OBSERVER_VERSION || info->installed != 1 ||
        !read_memory || !owner) return 0;
    for (unsigned i = 0; i < PLAYER_SEM_OBSERVER_SITES; ++i) {
        uintptr_t site = player_semaphore_site_addresses[i], page = info->sites[i].page;
        uintptr_t wrapper = info->sites[i].wrapper, original_slot = info->sites[i].original_slot;
        if (!page || page > INT64_MAX || (page & (PLAYER_SEM_PAGE_SIZE - 1)) || !wrapper || !original_slot ||
            !owner(context, page, wrapper, original_slot)) return 0;
        uintptr_t original = 0;
        if (!read_memory(context, original_slot, &original, sizeof original) ||
            original != page + PLAYER_SEM_TRAMPOLINE_OFFSET) return 0;
        for (unsigned j = 0; j < i; ++j)
            if (page == info->sites[j].page) return 0;
        int64_t relative64 = (int64_t)page - (int64_t)(site + 5);
        if (relative64 < INT32_MIN || relative64 > INT32_MAX) return 0;
        unsigned char entry[8] = {0xe9,0,0,0,0,0x90,0x90,0x90}, actual_entry[8];
        int32_t relative = (int32_t)relative64;
        memcpy(entry + 1, &relative, sizeof relative);
        if (!read_memory(context, site, actual_entry, sizeof actual_entry) ||
            memcmp(entry, actual_entry, sizeof entry)) return 0;
        unsigned char expected[PLAYER_SEM_PAGE_SIZE], actual[PLAYER_SEM_PAGE_SIZE];
        memset(expected, 0xcc, sizeof expected);
        player_semaphore_abs_jump(expected, wrapper);
        unsigned length = player_semaphore_relocated[i];
        memcpy(expected + PLAYER_SEM_TRAMPOLINE_OFFSET, player_semaphore_original[i], length);
        player_semaphore_abs_jump(expected + PLAYER_SEM_TRAMPOLINE_OFFSET + length, site + length);
        if (!read_memory(context, page, actual, sizeof actual) || memcmp(expected, actual, sizeof actual)) return 0;
    }
    return 1;
}

/* Caller first authenticates the provider and every wrapper against independent
 * pins, then validates all four detours. The original range hash remains the
 * authoritative pin; only the authenticated overwritten bytes are restored. */
static inline int player_semaphore_original_range(const player_semaphore_observer_info *info,
        uintptr_t site, size_t length, const char *sha, player_semaphore_read_fn read_memory,
        player_semaphore_owner_fn owner, player_semaphore_hash_fn hash, void *context) {
    if (!sha || !hash || !read_memory || length < 9 || length > 512) return 0;
    unsigned index;
    for (index = 0; index < PLAYER_SEM_OBSERVER_SITES; ++index)
        if (player_semaphore_site_addresses[index] == site) break;
    if (index == PLAYER_SEM_OBSERVER_SITES) return 0;
    unsigned char bytes[512];
    if (!read_memory(context, site, bytes, length)) return 0;
    if (hash(context, bytes, length, sha)) return 1;
    if (!player_semaphore_observer_validate(info, read_memory, owner, context)) return 0;
    memcpy(bytes, player_semaphore_original[index], 8);
    return hash(context, bytes, length, sha);
}
#endif

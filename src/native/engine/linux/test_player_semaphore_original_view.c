#include <assert.h>
#include <stdlib.h>
#include "player_semaphore_original_view.h"

static unsigned char entries[4][512], pages[4][PLAYER_SEM_PAGE_SIZE];
static uintptr_t slots[4];
static player_semaphore_observer_info info;
static int ownership = 1;
static int mock_read(void *context, uintptr_t address, void *output, size_t length) {
    (void)context;
    for (unsigned i = 0; i < 4; ++i) {
        if (address == info.sites[i].original_slot && length == sizeof slots[i]) {
            memcpy(output, &slots[i], length); return 1;
        }
        if (address == player_semaphore_site_addresses[i] && length <= sizeof entries[i]) {
            memcpy(output, entries[i], length); return 1;
        }
        if (address == info.sites[i].page && length == sizeof pages[i]) {
            memcpy(output, pages[i], length); return 1;
        }
    }
    return 0;
}
static int mock_owner(void *context, uintptr_t page, uintptr_t wrapper, uintptr_t original_slot) {
    (void)context; (void)page; (void)wrapper; (void)original_slot; return ownership;
}
static int mock_hash(void *context, const void *bytes, size_t length, const char *sha) {
    (void)context;
    if (strcmp(sha, "expected") || length != 32) return 0;
    const unsigned char *p = bytes;
    if (memcmp(p, player_semaphore_original[1], 9)) return 0;
    for (unsigned i = 9; i < 32; ++i) if (p[i] != 0x72) return 0;
    return 1;
}
static void setup(void) {
    memset(&info, 0, sizeof info);
    info.version = PLAYER_SEM_OBSERVER_VERSION;
    info.installed = 1;
    ownership = 1;
    for (unsigned i = 0; i < 4; ++i) {
        memset(entries[i], 0x72, sizeof entries[i]);
        memcpy(entries[i], player_semaphore_original[i], 9);
        info.sites[i].page = (player_semaphore_site_addresses[i] & ~(uintptr_t)4095) + 0x10000 + i * 4096;
        info.sites[i].wrapper = 0x40000000 + i * 256;
        info.sites[i].original_slot = (uintptr_t)&slots[i];
        slots[i] = info.sites[i].page + PLAYER_SEM_TRAMPOLINE_OFFSET;
        memset(pages[i], 0xcc, sizeof pages[i]);
        player_semaphore_abs_jump(pages[i], info.sites[i].wrapper);
        unsigned length = player_semaphore_relocated[i];
        memcpy(pages[i] + PLAYER_SEM_TRAMPOLINE_OFFSET, player_semaphore_original[i], length);
        player_semaphore_abs_jump(pages[i] + PLAYER_SEM_TRAMPOLINE_OFFSET + length,
                                  player_semaphore_site_addresses[i] + length);
        int32_t relative = (int32_t)(info.sites[i].page - (player_semaphore_site_addresses[i] + 5));
        entries[i][0] = 0xe9;
        memcpy(entries[i] + 1, &relative, 4);
        memset(entries[i] + 5, 0x90, 3);
    }
}
static int valid(void) { return player_semaphore_observer_validate(&info, mock_read, mock_owner, 0); }
static int range(void) {
    return player_semaphore_original_range(&info, player_semaphore_site_addresses[1], 32,
                                            "expected", mock_read, mock_owner, mock_hash, 0);
}
int main(void) {
    setup(); assert(valid() && range());
    ownership = 0; assert(!valid() && !range());
    setup(); info.version++; assert(!valid());
    setup(); info.installed = 0; assert(!valid());
    setup(); info.sites[1].page = info.sites[0].page; assert(!valid());
    setup(); entries[2][2] ^= 1; assert(!valid() && !range());
    setup(); pages[0][30] ^= 1; assert(!valid());
    setup(); pages[1][PLAYER_SEM_TRAMPOLINE_OFFSET + 8] ^= 1; assert(!valid());
    setup(); entries[1][9] ^= 1; assert(!range());
    setup(); info.sites[0].wrapper++; assert(!valid());
    setup(); slots[0]++; assert(!valid() && !range());
    setup(); memcpy(entries[1], player_semaphore_original[1], 9);
    assert(player_semaphore_original_range(NULL, player_semaphore_site_addresses[1], 32,
                                            "expected", mock_read, NULL, mock_hash, NULL));
    return 0;
}

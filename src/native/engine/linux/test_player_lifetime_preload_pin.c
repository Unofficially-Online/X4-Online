#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "player_lifetime_preload_emit.h"
static unsigned char *memory;
static plp_information fake_information;
static void *fake_lx(uintptr_t va) {
    if (va == PLP_REPLACE) return memory;
    if (va == PLP_DESTRUCT) return memory + PLP_PAGE;
    if (va == PLP_VTABLE) return memory + PLP_PAGE * 4;
    abort();
}
#define LX(va) fake_lx(va)
static int gate_read_local(const void *source, void *output, size_t length) {
    memcpy(output, source, length);
    return 1;
}
static int gate_maps_find(uintptr_t address, char permissions[5], char *path, size_t capacity) {
    if (address == (uintptr_t)fake_information.wrappers[0] ||
        address == (uintptr_t)fake_information.wrappers[1] || address == (uintptr_t)memory || address == (uintptr_t)(memory + PLP_PAGE) ||
        address == (uintptr_t)(memory + PLP_PAGE * 2) || address == (uintptr_t)(memory + PLP_PAGE * 3))
        strcpy(permissions, "r-xp");
    else
        strcpy(permissions, "rw-p");
    if (capacity) path[0] = 0;
    return 1;
}
static int hash_ok(const void *bytes, size_t length, const char *expected) {
    return !memcmp(bytes, expected, length);
}
#include "player_lifetime_preload_pin.h"
int main(int argc, char **argv) {
    assert(argc == 2);
    memory = mmap(NULL, PLP_PAGE * 5, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(memory != MAP_FAILED);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    assert(library);
    void (*setup)(void *, void *) = dlsym(library, "fake_lifetime_setup");
    assert(setup);
    setup(memory + PLP_PAGE * 2, memory + PLP_PAGE * 3);
    plp_information information;
    unsigned (*info)(plp_information *) = dlsym(library, "x4mp_player_lifetime_info");
    assert(info(&information) == PLP_VERSION);
    fake_information = information;
    unsigned char expected[2][64];
    for (unsigned index = 0; index < 2; ++index) {
        memset(expected[index], 0xa5, sizeof expected[index]);
        memcpy(expected[index], &plp_original[index], 8);
        unsigned char *site = memory + PLP_PAGE * index;
        unsigned char *page = memory + PLP_PAGE * (index + 2);
        memcpy(site, expected[index], sizeof expected[index]);
        memset(page, 0xcc, PLP_PAGE);
        plp_emit(page, (uintptr_t)information.binding_slot, (uintptr_t)information.entry_count, index, (uintptr_t)site,
                 (uintptr_t)LX(PLP_VTABLE), (uintptr_t)information.wrappers[index]);
        int32_t distance = (int32_t)(page - (site + 5));
        const unsigned char jump[8] = {0xe9,0,0,0,0,0x90,0x90,0x90};
        memcpy(site, jump, 8);
        memcpy(site + 1, &distance, 4);
    }
    assert(player_lifetime_prepare_provider(&information));
    assert(player_lifetime_complete_provider(&information));
    void **slot = (void **)information.original_slots[0];
    void *expected_target = *slot;
    *slot = (unsigned char *)expected_target + 1;
    assert(!player_lifetime_prepare_provider(&information));
    *slot = expected_target;
    unsigned char *wrapper = (unsigned char *)information.wrappers[0];
    uintptr_t wrapper_page = (uintptr_t)wrapper & ~(uintptr_t)(PLP_PAGE - 1);
    assert(!mprotect((void *)wrapper_page, PLP_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
    wrapper[0] ^= 1;
    assert(!player_lifetime_prepare_provider(&information));
    wrapper[0] ^= 1;
    assert(!mprotect((void *)wrapper_page, PLP_PAGE, PROT_READ | PROT_EXEC));
    assert(player_lifetime_original_pin(PLP_REPLACE, 64, (char *)expected[0]));
    assert(player_lifetime_original_pin(PLP_DESTRUCT, 64, (char *)expected[1]));
    memory[PLP_PAGE * 2 + 4000] ^= 1;
    assert(!player_lifetime_original_pin(PLP_REPLACE, 64, (char *)expected[0]));
    memory[PLP_PAGE * 2 + 4000] ^= 1;
    memory[PLP_PAGE + 5] ^= 1;
    assert(!player_lifetime_original_pin(PLP_REPLACE, 64, (char *)expected[0]));
    memory[PLP_PAGE + 5] ^= 1;
    memory[16] ^= 1;
    assert(!player_lifetime_original_pin(PLP_REPLACE, 64, (char *)expected[0]));
    puts("lifetime preload provider and original-view pin PASS");
}

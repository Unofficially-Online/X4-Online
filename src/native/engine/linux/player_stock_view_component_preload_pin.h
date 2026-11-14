#ifndef X4_PLAYER_STOCK_VIEW_COMPONENT_PRELOAD_PIN_H
#define X4_PLAYER_STOCK_VIEW_COMPONENT_PRELOAD_PIN_H
#include "player_stock_view_component_preload.h"
#include "player_view_ancestry_preload_pin.h"
typedef struct {
    int (*bind)(player_stock_view_component_policy);
    player_stock_view_component_info info;
} player_stock_view_component_api;
static int player_stock_view_component_provider(const player_view_query_api *sealed,
                                                player_stock_view_component_api *out) {
    if (!sealed || !sealed->finish || !out) return 0;
    memset(out, 0, sizeof *out);
    Dl_info module;
    if (!dladdr((void *)sealed->finish, &module) || !module.dli_fbase) return 0;
    void *info_fn = dlsym(RTLD_DEFAULT, "x4mp_stock_view_component_info");
    void *bind_fn = dlsym(RTLD_DEFAULT, "x4mp_stock_view_component_bind");
    if (!player_view_pin_same_module((uintptr_t)info_fn, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)bind_fn, module.dli_fbase, "r-xp")) return 0;
    player_stock_view_component_info info = {0};
    if (((int (*)(player_stock_view_component_info *))info_fn)(&info) != 1 ||
        (info.version != 1 && info.version != 2) || info.installed != 1 || !info.page || (info.page & 4095) ||
        !player_view_pin_permissions(info.page, 4096, "r-xp", 1)) return 0;
    if (info.version == 2) {
        player_view_ancestry_api ancestry;
        if (!player_view_ancestry_provider(sealed, &ancestry) || info.page != ancestry.information.stubs[0]) return 0;
        for (unsigned i = 0; i < 2; ++i) {
            if (info.bridge[i] != ancestry.information.dispatchers[i] ||
                info.original_slot[i] != ancestry.information.original_slots[i] ||
                info.frame[i] != ancestry.information.frames[i] ||
                info.original[i] != ancestry.information.original_words[i] ||
                info.patched[i] != ancestry.information.patched_words[i] ||
                !player_view_pin_same_module(info.dispatch[i], module.dli_fbase, "r-xp")) return 0;
        }
        void *library = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_NOLOAD);
        if (!library) return 0;
        player_view_pin_find_fde_fn find = (player_view_pin_find_fde_fn)dlsym(library, "_Unwind_Find_FDE");
        int ok = find != NULL;
        for (unsigned i = 0; ok && i < 2; ++i) {
            player_view_pin_unwind_bases bases = {0};
            void *fde = find((void *)info.bridge[i], &bases);
            Dl_info owner;
            ok = fde && bases.function == (void *)info.bridge[i] && dladdr(fde, &owner) &&
                 owner.dli_fbase == module.dli_fbase && player_view_pin_permissions((uintptr_t)fde, 4, "r--p", 0);
        }
        dlclose(library);
        if (!ok) return 0;
        out->bind = (int (*)(player_stock_view_component_policy))bind_fn;
        out->info = info;
        return 1;
    }
    static const uintptr_t functions[2] = {0x11132d0, 0x177f900};
    static const uintptr_t blocks[2] = {0x1113338, 0x177f9c0};
    static const unsigned lengths[2] = {133, 224}, opcode_offsets[2] = {1, 0};
    static const uintptr_t targets[2] = {0x1d6b1c0, 0x1e07020};
    static const uint64_t originals[2] = {UINT64_C(0x835b00c57e82e818), UINT64_C(0x75c0840068765be8)};
    static const char *hashes[2] = {
        "48dd904179d64a8b5f75a87b5b989a3f7c215342c71ac5132f07b7b86b8d3b58",
        "d46a2e354ebd8f76037356afe53a778117f45401b2dfd3c64727ae42e8dfd0ef"};
    unsigned char page[4096], expected_page[4096]; memset(expected_page, 0xcc, sizeof expected_page);
    for (unsigned i = 0; i < 2; ++i) {
        if (info.original[i] != originals[i] ||
            !player_view_pin_same_module(info.bridge[i], module.dli_fbase, "r-xp") ||
            !player_view_pin_same_module(info.dispatch[i], module.dli_fbase, "r-xp") ||
            !player_view_pin_same_module(info.original_slot[i], module.dli_fbase, "rw-p") ||
            !player_view_pin_same_module(info.frame[i], module.dli_fbase, "rw-p") ||
            !player_view_pin_permissions((uintptr_t)LX(functions[i]), lengths[i], "r-xp", 0)) return 0;
        uintptr_t target = 0;
        if (!gate_read_local((void *)info.original_slot[i], &target, sizeof target) ||
            target != (uintptr_t)LX(targets[i])) return 0;
        unsigned char body[224];
        if (!gate_read_local(LX(functions[i]), body, lengths[i])) return 0;
        size_t offset = blocks[i] - functions[i];
        int64_t distance = (int64_t)(info.page + i * 16) -
                           (int64_t)((uintptr_t)LX(blocks[i]) + opcode_offsets[i] + 5);
        if (distance < INT32_MIN || distance > INT32_MAX) return 0;
        unsigned char patch[8]; memcpy(patch, &originals[i], 8); patch[opcode_offsets[i]] = 0xe8;
        int32_t relative = (int32_t)distance; memcpy(patch + opcode_offsets[i] + 1, &relative, 4);
        uint64_t patched; memcpy(&patched, patch, 8);
        if (info.patched[i] != patched || memcmp(body + offset, patch, 8)) return 0;
        memcpy(body + offset, &originals[i], 8);
        if (!hash_ok(body, lengths[i], hashes[i])) return 0;
        unsigned char *stub = expected_page + i * 16;
        stub[0] = 0xff; stub[1] = 0x25; memset(stub + 2, 0, 4); memcpy(stub + 6, &info.bridge[i], 8);
        unsigned char expected[48] = {16,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,8,0x90,1,0,0}, actual[48];
        uint32_t length = 20, cie = 24; uint64_t start = info.page + i * 16, extent = 14;
        memcpy(expected + 20, &length, 4); memcpy(expected + 24, &cie, 4);
        memcpy(expected + 28, &start, 8); memcpy(expected + 36, &extent, 8);
        if (!gate_read_local((void *)info.frame[i], actual, sizeof actual) || memcmp(actual, expected, sizeof actual)) return 0;
    }
    if (!gate_read_local((void *)info.page, page, sizeof page) || memcmp(page, expected_page, sizeof page)) return 0;
    static const lx_site extra[] = {
        {"stock-view-missile", 0x1113360, 76, "5798f27c36fee251792a259c2e5b74ba1e647907ea96fb177bcccb73b8535fdc"},
        {"stock-view-floating", 0x1d6b1c0, 99, "28662e2439a706b8794ec0c9dbe9b2b31aef33695461446cfbba6466a58967f6"},
        {"stock-view-external", 0x1e07020, 51, "7fbd4e7bdb604524025c4a46cc791ae7137b7c1f19270213781e35e5b7890f51"}
    };
    for (unsigned i = 0; i < 3; ++i) if (!site_ok(&extra[i])) return 0;
    void *lib = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) return 0;
    player_view_pin_find_fde_fn find = (player_view_pin_find_fde_fn)dlsym(lib, "_Unwind_Find_FDE");
    int ok = find != NULL;
    for (unsigned i = 0; ok && i < 2; ++i) {
        player_view_pin_unwind_bases bases = {0};
        ok = find((void *)(info.page + i * 16), &bases) == (void *)(info.frame[i] + 20) &&
             bases.function == (void *)(info.page + i * 16);
        memset(&bases, 0, sizeof bases);
        void *fde = ok ? find((void *)info.bridge[i], &bases) : NULL;
        Dl_info owner;
        ok = fde && bases.function == (void *)info.bridge[i] && dladdr(fde, &owner) &&
             owner.dli_fbase == module.dli_fbase && player_view_pin_permissions((uintptr_t)fde, 4, "r--p", 0);
    }
    dlclose(lib);
    if (!ok) return 0;
    out->bind = (int (*)(player_stock_view_component_policy))bind_fn;
    out->info = info;
    return 1;
}
#endif

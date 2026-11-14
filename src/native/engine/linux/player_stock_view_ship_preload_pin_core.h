#ifndef X4_PLAYER_STOCK_VIEW_SHIP_PRELOAD_PIN_CORE_H
#define X4_PLAYER_STOCK_VIEW_SHIP_PRELOAD_PIN_CORE_H
#include "player_stock_view_ship_preload.h"
#include "player_stock_view_fov_cfi.h"
#include "player_view_ancestry_preload.h"
typedef struct {
    int (*bind)(player_stock_view_ship_policy);
    player_stock_view_ship_info info;
} player_stock_view_ship_api;
static int player_stock_view_ship_provider_core(const player_view_query_api *sealed,
                                                player_stock_view_ship_api *out,
                                                const view_ancestry_info *ancestry) {
    if (!sealed || !sealed->finish || !out) return 0;
    memset(out, 0, sizeof *out);
    Dl_info module;
    if (!dladdr((void *)sealed->finish, &module) || !module.dli_fbase) return 0;
    void *info_fn = dlsym(RTLD_DEFAULT, "x4mp_stock_view_ship_info");
    void *bind_fn = dlsym(RTLD_DEFAULT, "x4mp_stock_view_ship_bind");
    if (!player_view_pin_same_module((uintptr_t)info_fn, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)bind_fn, module.dli_fbase, "r-xp")) return 0;
    player_stock_view_ship_info info = {0};
    const uint64_t original = UINT64_C(0xc985000008808a8b);
    if (((int (*)(player_stock_view_ship_info *))info_fn)(&info) != 1 ||
        info.version != 1 || info.installed != 1 || info.original != original ||
        !info.page || (info.page & 4095) || !player_view_pin_permissions(info.page, 4096, "r-xp", 1) ||
        !player_view_pin_same_module(info.bridge, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module(info.dispatch, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module(info.frame, module.dli_fbase, "rw-p")) return 0;
    unsigned char code[456], page[4096], expected_page[4096];
    if (!player_view_pin_permissions((uintptr_t)LX(0x129bad0), sizeof code, "r-xp", 0) ||
        !gate_read_local(LX(0x129bad0), code, sizeof code)) return 0;
    const size_t offset = 0x129bb21 - 0x129bad0;
    int64_t distance = (int64_t)info.page - (int64_t)((uintptr_t)LX(0x129bb21) + 5);
    if (distance < INT32_MIN || distance > INT32_MAX) return 0;
    unsigned char patch[8]; memcpy(patch, &original, 8); patch[0] = 0xe9; patch[5] = 0x90;
    int32_t relative = (int32_t)distance; memcpy(patch + 1, &relative, 4);
    uint64_t patched; memcpy(&patched, patch, 8);
    if (info.patched != patched || memcmp(code + offset, patch, 8)) return 0;
    memcpy(code + offset, &original, 8);
    if (ancestry) {
        const uint64_t entry_original = UINT64_C(0xfd89485554415541);
        if (ancestry->blocks[2] != (uintptr_t)LX(0x129bad0) || ancestry->offsets[2] ||
            ancestry->original_words[2] != entry_original ||
            memcmp(code, &ancestry->patched_words[2], 8)) return 0;
        memcpy(code, &entry_original, 8);
    }
    if (!hash_ok(code, sizeof code, "cff3c5caae429ad58f93402f0d0b754c82d467823d8298c3641b1459a1605e50")) return 0;
    memset(expected_page, 0xcc, sizeof expected_page); expected_page[0] = 0xff; expected_page[1] = 0x25;
    memset(expected_page + 2, 0, 4); memcpy(expected_page + 6, &info.bridge, 8);
    if (!gate_read_local((void *)info.page, page, sizeof page) || memcmp(page, expected_page, sizeof page)) return 0;
    unsigned char expected_frame[PLAYER_STOCK_VIEW_FOV_FRAME_SIZE], actual_frame[PLAYER_STOCK_VIEW_FOV_FRAME_SIZE];
    player_stock_view_ship_frame(expected_frame, info.page);
    if (!gate_read_local((void *)info.frame, actual_frame, sizeof actual_frame) ||
        memcmp(actual_frame, expected_frame, sizeof actual_frame)) return 0;
    void *lib = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) return 0;
    player_view_pin_find_fde_fn find = (player_view_pin_find_fde_fn)dlsym(lib, "_Unwind_Find_FDE");
    player_view_pin_unwind_bases bases = {0};
    int ok = find && find((void *)info.page, &bases) == (void *)(info.frame + 28) && bases.function == (void *)info.page;
    memset(&bases, 0, sizeof bases);
    void *fde = ok ? find((void *)info.bridge, &bases) : NULL;
    Dl_info owner;
    ok = fde && bases.function == (void *)info.bridge && dladdr(fde, &owner) &&
         owner.dli_fbase == module.dli_fbase && player_view_pin_permissions((uintptr_t)fde, 4, "r--p", 0);
    dlclose(lib);
    if (!ok) return 0;
    out->bind = (int (*)(player_stock_view_ship_policy))bind_fn;
    out->info = info;
    return 1;
}
#endif

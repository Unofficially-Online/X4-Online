#ifndef X4_PLAYER_STOCK_VIEW_FOV_PRELOAD_PIN_H
#define X4_PLAYER_STOCK_VIEW_FOV_PRELOAD_PIN_H
#include "player_stock_view_fov_preload.h"
#include "player_stock_view_fov_cfi.h"

typedef struct {
    int (*bind)(player_stock_view_mode_policy, player_stock_view_fov_policy);
    player_stock_view_fov_info info;
} player_stock_view_fov_api;

/* Require the already authenticated sealed view-query DSO. This accepts only
 * its exact owned sites and resident RX stubs, after reconstructing the full
 * original coefficient function. It does not authorize callback binding. */
static int player_stock_view_fov_provider(const player_view_query_api *sealed,
                                          player_stock_view_fov_api *out) {
    if (!sealed || !sealed->finish || !out) return 0;
    memset(out, 0, sizeof *out);
    Dl_info module;
    if (!dladdr((void *)sealed->finish, &module) || !module.dli_fbase) return 0;
    void *info_fn = dlsym(RTLD_DEFAULT, "x4mp_stock_view_fov_info");
    void *bind_fn = dlsym(RTLD_DEFAULT, "x4mp_stock_view_fov_bind");
    if (!player_view_pin_same_module((uintptr_t)info_fn, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)bind_fn, module.dli_fbase, "r-xp")) return 0;
    player_stock_view_fov_info info = {0};
    if (((int (*)(player_stock_view_fov_info *))info_fn)(&info) != 1 ||
        info.version != 2 || info.installed != 1 || !info.page || (info.page & 4095) ||
        !player_view_pin_permissions(info.page, 4096, "r-xp", 1) ||
        !player_view_pin_same_module(info.mode_dispatch, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module(info.fov_dispatch, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module(info.original_fov_slot, module.dli_fbase, "rw-p") ||
        !player_view_pin_same_module(info.call_frame, module.dli_fbase, "rw-p")) return 0;
    static const uintptr_t sites[3] = {0x1666d2a, 0x1666e87, 0x1666d58};
    static const uint64_t words[3] = {UINT64_C(0x0ff300000880978b),
        UINT64_C(0x0ff300000880878b), UINT64_C(0x100ff3002bff63e8)};
    unsigned char code[2773], page[4096], expected_page[4096];
    if (!player_view_pin_permissions((uintptr_t)LX(0x16666b0), sizeof code, "r-xp", 0) ||
        !gate_read_local(LX(0x16666b0), code, sizeof code)) return 0;
    memset(expected_page, 0xcc, sizeof expected_page);
    for (unsigned index = 0; index < 3; ++index) {
        uintptr_t bridge = index == 2 ? info.fov_bridge : info.mode_bridge[index];
        if (info.original[index] != words[index] ||
            !player_view_pin_same_module(bridge, module.dli_fbase, "r-xp")) return 0;
        size_t offset = sites[index] - 0x16666b0;
        int64_t distance = (int64_t)(info.page + index * 16) - (int64_t)((uintptr_t)LX(sites[index]) + 5);
        if (distance < INT32_MIN || distance > INT32_MAX) return 0;
        unsigned char patch[8];
        memcpy(patch, &words[index], 8);
        patch[0] = index == 2 ? 0xe8 : 0xe9;
        int32_t relative = (int32_t)distance;
        memcpy(patch + 1, &relative, 4);
        if (index != 2) patch[5] = 0x90;
        uint64_t patched;
        memcpy(&patched, patch, 8);
        if (info.patched[index] != patched || memcmp(code + offset, patch, 8)) return 0;
        memcpy(code + offset, &words[index], 8);
        unsigned char *stub = expected_page + index * 16;
        stub[0] = 0xff; stub[1] = 0x25;
        memset(stub + 2, 0, 4); memcpy(stub + 6, &bridge, 8);
    }
    if (!hash_ok(code, sizeof code, "844509812707345ef5b8de3dfb05becfe3eedec23c188b70001546f0db7ae538") ||
        !gate_read_local((void *)info.page, page, sizeof page) || memcmp(page, expected_page, sizeof page)) return 0;
    uintptr_t original = 0;
    if (!gate_read_local((void *)info.original_fov_slot, &original, sizeof original) ||
        original != (uintptr_t)LX(0x1926cc0)) return 0;
    static const lx_site original_fov = {"stock-view-fov", 0x1926cc0, 0x1c5,
        "529f00a038c89f27d56d14e5804ee101db07e8ccb8b33492372c6d46d98ab3cc"};
    if (!site_ok(&original_fov)) return 0;
    for (unsigned index = 0; index < 2; ++index) {
        unsigned char expected[PLAYER_STOCK_VIEW_FOV_FRAME_SIZE], actual[PLAYER_STOCK_VIEW_FOV_FRAME_SIZE];
        if (!player_view_pin_same_module(info.mode_frame[index], module.dli_fbase, "rw-p")) return 0;
        player_stock_view_fov_frame(expected, info.page + index * 16);
        if (!gate_read_local((void *)info.mode_frame[index], actual, sizeof actual) ||
            memcmp(expected, actual, sizeof actual)) return 0;
    }
    unsigned char expected_call[48] = {16,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,8,0x90,1,0,0};
    uint32_t length = 20, cie = 24;
    uint64_t start = info.page + 32, extent = 14;
    memcpy(expected_call + 20, &length, 4); memcpy(expected_call + 24, &cie, 4);
    memcpy(expected_call + 28, &start, 8); memcpy(expected_call + 36, &extent, 8);
    unsigned char actual_call[48];
    if (!gate_read_local((void *)info.call_frame, actual_call, sizeof actual_call) ||
        memcmp(actual_call, expected_call, sizeof actual_call)) return 0;
    void *lib = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) return 0;
    player_view_pin_find_fde_fn find = (player_view_pin_find_fde_fn)dlsym(lib, "_Unwind_Find_FDE");
    int ok = find != NULL;
    for (unsigned index = 0; ok && index < 3; ++index) {
        player_view_pin_unwind_bases bases = {0};
        uintptr_t frame = index == 2 ? info.call_frame + 20 : info.mode_frame[index] + 28;
        ok = find((void *)(info.page + index * 16), &bases) == (void *)frame &&
             bases.function == (void *)(info.page + index * 16);
        memset(&bases, 0, sizeof bases);
        uintptr_t bridge = index == 2 ? info.fov_bridge : info.mode_bridge[index];
        void *fde = ok ? find((void *)bridge, &bases) : NULL;
        Dl_info owner;
        ok = fde && bases.function == (void *)bridge && dladdr(fde, &owner) &&
             owner.dli_fbase == module.dli_fbase && player_view_pin_permissions((uintptr_t)fde, 4, "r--p", 0);
    }
    dlclose(lib);
    if (!ok) return 0;
    out->bind = (int (*)(player_stock_view_mode_policy, player_stock_view_fov_policy))bind_fn;
    out->info = info;
    return 1;
}
/* Observer is additive: its callback receives the exact camera and original
 * float already returned by the installed stock call. */
static inline int player_stock_view_fov_observer_provider(const player_view_query_api *sealed,
        int (**bind)(player_stock_view_fov_observer)) {
    player_stock_view_fov_api verified;
    if (!bind || !player_stock_view_fov_provider(sealed, &verified)) return 0;
    Dl_info module;
    void *function = dlsym(RTLD_DEFAULT, "x4mp_stock_view_fov_observe_bind");
    if (!dladdr((void *)sealed->finish, &module) || !module.dli_fbase ||
        !player_view_pin_same_module((uintptr_t)function, module.dli_fbase, "r-xp")) return 0;
    *bind = (int (*)(player_stock_view_fov_observer))function;
    return 1;
}
#endif

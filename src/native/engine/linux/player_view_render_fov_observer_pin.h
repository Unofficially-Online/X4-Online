#ifndef X4_PLAYER_VIEW_RENDER_FOV_OBSERVER_PIN_H
#define X4_PLAYER_VIEW_RENDER_FOV_OBSERVER_PIN_H
#include "player_view_render_fov_observer.h"
typedef struct {
    int (*bind)(player_view_render_fov_observer);
    player_view_render_fov_info info;
} player_view_render_fov_api;
/* Authenticate the complete function after reconstructing its original call,
 * the exact trampoline, original target and same-image exported API. */
static int player_view_render_fov_provider(const player_view_query_api *sealed, player_view_render_fov_api *out) {
    if (!sealed || !sealed->finish || !out) return 0;
    memset(out, 0, sizeof *out);
    Dl_info module;
    if (!dladdr((void *)sealed->finish, &module) || !module.dli_fbase) return 0;
    void *info_fn = dlsym(RTLD_DEFAULT, "x4mp_view_render_fov_info");
    void *bind_fn = dlsym(RTLD_DEFAULT, "x4mp_view_render_fov_bind");
    if (!player_view_pin_same_module((uintptr_t)info_fn, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)bind_fn, module.dli_fbase, "r-xp")) return 0;
    player_view_render_fov_info info = {0};
    if (((int (*)(player_view_render_fov_info *))info_fn)(&info) != 1 ||
        info.version != 1 || info.installed != 1 || !info.page || (info.page & 4095) ||
        !player_view_pin_permissions(info.page, 4096, "r-xp", 1) ||
        !player_view_pin_same_module(info.dispatch, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module(info.original_slot, module.dli_fbase, "rw-p") ||
        !player_view_pin_same_module(info.frame, module.dli_fbase, "rw-p")) return 0;
    const uint64_t original_word = UINT64_C(0x100ff3fffffcf1e8);
    if (info.original != original_word) return 0;
    int64_t delta = (int64_t)info.page - (int64_t)((uintptr_t)LX(0x1926fca) + 5);
    if (delta < INT32_MIN || delta > INT32_MAX) return 0;
    uint64_t patched = original_word; int32_t displacement = (int32_t)delta;
    memcpy((unsigned char *)&patched + 1, &displacement, 4);
    if (info.patched != patched) return 0;
    unsigned char code[0xa1], page[4096], expected_page[4096];
    if (!player_view_pin_permissions((uintptr_t)LX(0x1926f80), sizeof code, "r-xp", 0) ||
        !gate_read_local(LX(0x1926f80), code, sizeof code) ||
        memcmp(code + 0x4a, &patched, 8)) return 0;
    memcpy(code + 0x4a, &original_word, 8);
    if (!hash_ok(code, sizeof code, "1da4645c6eed9fabfdb4f9a9eef4676e8cbf6efaf1dc28abf510eea77a95ef88")) return 0;
    memset(expected_page, 0xcc, sizeof expected_page);
    expected_page[0] = 0xff; expected_page[1] = 0x25;
    memset(expected_page + 2, 0, 4); memcpy(expected_page + 6, &info.dispatch, 8);
    if (!gate_read_local((void *)info.page, page, sizeof page) || memcmp(page, expected_page, sizeof page)) return 0;
    uintptr_t target = 0;
    static const lx_site original = {"render-fov-original", 0x1926cc0, 0x1c5,
        "529f00a038c89f27d56d14e5804ee101db07e8ccb8b33492372c6d46d98ab3cc"};
    if (!gate_read_local((void *)info.original_slot, &target, sizeof target) ||
        target != (uintptr_t)LX(0x1926cc0) || !site_ok(&original)) return 0;
    unsigned char expected_frame[48] = {16,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,8,0x90,1,0,0};
    uint32_t length = 20, cie = 24; uint64_t extent = 14, start = info.page;
    memcpy(expected_frame + 20, &length, 4); memcpy(expected_frame + 24, &cie, 4);
    memcpy(expected_frame + 28, &start, 8); memcpy(expected_frame + 36, &extent, 8);
    unsigned char actual_frame[48];
    if (!gate_read_local((void *)info.frame, actual_frame, sizeof actual_frame) ||
        memcmp(actual_frame, expected_frame, sizeof actual_frame)) return 0;
    void *lib = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) return 0;
    player_view_pin_find_fde_fn find = (player_view_pin_find_fde_fn)dlsym(lib, "_Unwind_Find_FDE");
    player_view_pin_unwind_bases bases = {0};
    int ok = find && find((void *)info.page, &bases) == (void *)(info.frame + 20) && bases.function == (void *)info.page;
    memset(&bases, 0, sizeof bases);
    void *fde = ok ? find((void *)info.dispatch, &bases) : NULL; Dl_info owner;
    ok = ok && fde && bases.function == (void *)info.dispatch && dladdr(fde, &owner) &&
        owner.dli_fbase == module.dli_fbase && player_view_pin_permissions((uintptr_t)fde, 4, "r--p", 0);
    dlclose(lib);
    if (!ok) return 0;
    out->bind = (int (*)(player_view_render_fov_observer))bind_fn; out->info = info; return 1;
}
#endif

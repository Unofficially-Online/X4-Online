#ifndef X4_PLAYER_PROJECTION_FOV_SCOPE_PRELOAD_PIN_H
#define X4_PLAYER_PROJECTION_FOV_SCOPE_PRELOAD_PIN_H
#include "player_projection_fov_scope_preload.h"
#include "player_view_render_fov_observer_pin.h"

typedef struct {
    int (*bind)(uint32_t, uintptr_t, player_projection_fov_scope_callback, player_projection_fov_scope_callback);
    int (*activate)(void);
    int (*disarm)(void);
    player_projection_fov_scope_info info;
} player_projection_fov_scope_api;

/* The refill provider and this provider share this exact caller window. Both
 * reconstruct the same original bytes; neither accepts an unknown patch. */
static int player_projection_fov_scope_provider(const player_view_query_api *sealed,
                                                  player_projection_fov_scope_api *out) {
    if (!sealed || !sealed->finish || !out) return 0;
    memset(out, 0, sizeof *out);
    Dl_info module;
    if (!dladdr((void *)sealed->finish, &module) || !module.dli_fbase) return 0;
    void *info_fn = dlsym(RTLD_DEFAULT, "x4mp_projection_fov_scope_info");
    void *bind_fn = dlsym(RTLD_DEFAULT, "x4mp_projection_fov_scope_bind");
    void *activate_fn = dlsym(RTLD_DEFAULT, "x4mp_projection_fov_scope_activate");
    void *disarm_fn = dlsym(RTLD_DEFAULT, "x4mp_projection_fov_scope_disarm");
    if (!player_view_pin_same_module((uintptr_t)info_fn, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)bind_fn, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)activate_fn, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)disarm_fn, module.dli_fbase, "r-xp")) return 0;
    player_projection_fov_scope_info info = {0};
    const uint64_t original_word = UINT64_C(0xc8280f0093b604e8);
    if (((int (*)(player_projection_fov_scope_info *))info_fn)(&info) != 1 ||
        info.version != 1 || info.installed != 1 || info.site != (uintptr_t)LX(0xfeb977) ||
        info.original_word != original_word || !info.stub || (info.stub & 4095) ||
        info.stub_size != 14 || info.frame_size != 48 || info.page_size != 4096 ||
        !player_view_pin_permissions(info.stub, 4096, "r-xp", 1) ||
        !player_view_pin_same_module(info.dispatcher, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module(info.original_slot, module.dli_fbase, "rw-p") ||
        !player_view_pin_same_module(info.frame, module.dli_fbase, "rw-p")) return 0;
    int64_t distance = (int64_t)info.stub - (int64_t)(info.site + 5);
    if (distance < INT32_MIN || distance > INT32_MAX) return 0;
    uint64_t patched = original_word;
    int32_t relative = (int32_t)distance;
    memcpy((unsigned char *)&patched + 1, &relative, 4);
    if (info.patched_word != patched) return 0;
    unsigned char loop[0x82], page[4096], expected_page[4096];
    if (!player_view_pin_permissions((uintptr_t)LX(0xfeb940), sizeof loop, "r-xp", 0) ||
        !gate_read_local(LX(0xfeb940), loop, sizeof loop) || memcmp(loop + 0x37, &patched, 8)) return 0;
    memcpy(loop + 0x37, &original_word, 8);
    if (!hash_ok(loop, sizeof loop, "e5b77561f311eab417410c549f180c3e0219392ae444abd39c60339dfd7e653a")) return 0;
    memset(expected_page, 0xcc, sizeof expected_page);
    expected_page[0] = 0xff;
    expected_page[1] = 0x25;
    memset(expected_page + 2, 0, 4);
    memcpy(expected_page + 6, &info.dispatcher, 8);
    if (!gate_read_local((void *)info.stub, page, sizeof page) || memcmp(page, expected_page, sizeof page)) return 0;
    uintptr_t original_target = 0;
    player_view_render_fov_api renderer;
    if (!gate_read_local((void *)info.original_slot, &original_target, 8) ||
        original_target != (uintptr_t)LX(0x1926f80) || !player_view_render_fov_provider(sealed, &renderer)) return 0;
    unsigned char expected_frame[48] = {16,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,8,0x90,1,0,0};
    uint32_t length = 20, cie = 24;
    uint64_t start = info.stub, extent = 14;
    memcpy(expected_frame + 20, &length, 4);
    memcpy(expected_frame + 24, &cie, 4);
    memcpy(expected_frame + 28, &start, 8);
    memcpy(expected_frame + 36, &extent, 8);
    unsigned char frame[48];
    if (!gate_read_local((void *)info.frame, frame, sizeof frame) || memcmp(frame, expected_frame, sizeof frame)) return 0;
    void *library = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_NOLOAD);
    if (!library) return 0;
    player_view_pin_find_fde_fn find = (player_view_pin_find_fde_fn)dlsym(library, "_Unwind_Find_FDE");
    player_view_pin_unwind_bases bases = {0};
    int ok = find && find((void *)info.stub, &bases) == (void *)(info.frame + 20) && bases.function == (void *)info.stub;
    memset(&bases, 0, sizeof bases);
    void *fde = ok ? find((void *)info.dispatcher, &bases) : NULL;
    Dl_info owner;
    ok = ok && fde && bases.function == (void *)info.dispatcher && dladdr(fde, &owner) &&
        owner.dli_fbase == module.dli_fbase && player_view_pin_permissions((uintptr_t)fde, 4, "r--p", 0);
    dlclose(library);
    if (!ok) return 0;
    out->bind = (int (*)(uint32_t, uintptr_t, player_projection_fov_scope_callback,
                         player_projection_fov_scope_callback))bind_fn;
    out->activate = (int (*)(void))activate_fn;
    out->disarm = (int (*)(void))disarm_fn;
    out->info = info;
    return 1;
}
#endif

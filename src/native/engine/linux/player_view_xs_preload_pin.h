#ifndef X4_PLAYER_VIEW_XS_PRELOAD_PIN_H
#define X4_PLAYER_VIEW_XS_PRELOAD_PIN_H
#include "player_view_xs_preload.h"

typedef struct {
    int (*bind)(player_view_xs_target_policy, player_view_xs_external_policy,
                player_view_xs_distance_policy);
    int (*bound)(void);
    player_view_xs_preload_info info;
} player_view_xs_preload_api;

/* Composite preflight: authenticate the three patched calls against the full
 * original caller hash, immutable stub page, exact original slots and FDEs. */
static int player_view_xs_preload_provider(const player_view_query_api *sealed,
                                           player_view_xs_preload_api *out) {
    if (!sealed || !sealed->finish || !out)
        return 0;
    memset(out, 0, sizeof *out);
    Dl_info module;
    if (!dladdr((void *)sealed->finish, &module) || !module.dli_fbase)
        return 0;
    void *get_info = dlsym(RTLD_DEFAULT, "x4mp_view_xs_info");
    void *bind = dlsym(RTLD_DEFAULT, "x4mp_view_xs_bind");
    void *bound = dlsym(RTLD_DEFAULT, "x4mp_view_xs_bound");
    if (!player_view_pin_same_module((uintptr_t)get_info, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)bind, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)bound, module.dli_fbase, "r-xp"))
        return 0;
    player_view_xs_preload_info info = {0};
    if (((int (*)(player_view_xs_preload_info *))get_info)(&info) != 1 || info.version != 1 ||
        info.installed != 1 || !info.page || (info.page & 4095) ||
        !player_view_pin_permissions(info.page, 4096, "r-xp", 1))
        return 0;
    static const uintptr_t sites[3] = {0x129a6be, 0x129a75c, 0x129a779};
    static const uintptr_t targets[3] = {0x1c90380, 0x1e07020, 0x1c98690};
    static const unsigned char originals[3][5] = {{0xe8, 0xbd, 0x5c, 0x9f, 0x00},
                                                  {0xe8, 0xbf, 0xc8, 0xb6, 0x00},
                                                  {0xe8, 0x12, 0xdf, 0x9f, 0x00}};
    unsigned char caller_bytes[0x111], page[4096], expected_page[4096];
    if (!player_view_pin_permissions((uintptr_t)LX(0x129a680), sizeof caller_bytes, "r-xp", 0) ||
        !gate_read_local(LX(0x129a680), caller_bytes, sizeof caller_bytes))
        return 0;
    memset(expected_page, 0xcc, sizeof expected_page);
    void *library = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_NOLOAD);
    if (!library)
        return 0;
    player_view_pin_find_fde_fn find =
        (player_view_pin_find_fde_fn)dlsym(library, "_Unwind_Find_FDE");
    int ok = find != NULL;
    for (unsigned i = 0; ok && i < 3; ++i) {
        if (memcmp(info.original[i], originals[i], 5) ||
            !player_view_pin_same_module(info.bridge[i], module.dli_fbase, "r-xp") ||
            !player_view_pin_same_module(info.original_slot[i], module.dli_fbase, "rw-p") ||
            !player_view_pin_same_module(info.frame[i], module.dli_fbase, "rw-p")) {
            ok = 0;
            break;
        }
        int64_t delta = (int64_t)(info.page + i * 16) - (int64_t)((uintptr_t)LX(sites[i]) + 5);
        if (delta < INT32_MIN || delta > INT32_MAX) {
            ok = 0;
            break;
        }
        unsigned char expected_call[5] = {0xe8};
        int32_t relative = (int32_t)delta;
        memcpy(expected_call + 1, &relative, 4);
        size_t offset = sites[i] - 0x129a680;
        if (memcmp(info.patched[i], expected_call, 5) ||
            memcmp(caller_bytes + offset, expected_call, 5)) {
            ok = 0;
            break;
        }
        memcpy(caller_bytes + offset, originals[i], 5);
        unsigned char *stub = expected_page + i * 16;
        stub[0] = 0xff;
        stub[1] = 0x25;
        memset(stub + 2, 0, 4);
        memcpy(stub + 6, &info.bridge[i], 8);
        uintptr_t original = 0;
        if (!gate_read_local((void *)info.original_slot[i], &original, 8) ||
            original != (uintptr_t)LX(targets[i])) {
            ok = 0;
            break;
        }
        unsigned char expected_frame[48] = {16, 0,    0,  0,    0, 0, 0,    0, 1, 0,
                                            1,  0x78, 16, 0x0c, 7, 8, 0x90, 1, 0, 0};
        uint32_t length = 20, cie = 24;
        uint64_t start = info.page + i * 16, extent = 14;
        memcpy(expected_frame + 20, &length, 4);
        memcpy(expected_frame + 24, &cie, 4);
        memcpy(expected_frame + 28, &start, 8);
        memcpy(expected_frame + 36, &extent, 8);
        unsigned char frame[48];
        player_view_pin_unwind_bases bases = {0};
        if (!gate_read_local((void *)info.frame[i], frame, sizeof frame) ||
            memcmp(frame, expected_frame, sizeof frame) ||
            find((void *)start, &bases) != (void *)(info.frame[i] + 20) ||
            bases.function != (void *)start) {
            ok = 0;
            break;
        }
        memset(&bases, 0, sizeof bases);
        void *fde = find((void *)info.bridge[i], &bases);
        Dl_info owner;
        if (!fde || bases.function != (void *)info.bridge[i] || !dladdr(fde, &owner) ||
            owner.dli_fbase != module.dli_fbase ||
            !player_view_pin_permissions((uintptr_t)fde, 4, "r--p", 0))
            ok = 0;
    }
    dlclose(library);
    if (!ok ||
        !hash_ok(caller_bytes, sizeof caller_bytes,
                 "e491a0bd3d0a73760d66ada4215e20b07b4af0dc6b894d232dfba2472bb9142b") ||
        !gate_read_local((void *)info.page, page, sizeof page) ||
        memcmp(page, expected_page, sizeof page))
        return 0;
    static const lx_site callees[] = {
        {"xs-target", 0x1c90380, 0x51,
         "47a88f24f0a75dc1801d682abe6c5d83b1a6c52f2d125428bb1d8e17b83b4c00"},
        {"xs-external", 0x1e07020, 0x33,
         "7fbd4e7bdb604524025c4a46cc791ae7137b7c1f19270213781e35e5b7890f51"},
        {"xs-camera", 0x1dccd30, 0x34,
         "6398d63d2afe8dbdfcc384682ea75da888787d96b67df91fba9519d42bec94d0"},
        {"xs-distance", 0x1c98690, 0x1f8,
         "598e8e3b893cd530a4c413082a2bcaee43c3bd0ec3e728b4afddb2ec97eaba4b"},
    };
    for (unsigned i = 0; i < sizeof callees / sizeof callees[0]; ++i)
        if (!site_ok(&callees[i]))
            return 0;
    uintptr_t slot = 0;
    uint32_t threshold = 0;
    if (!gate_read_local(LX(0x3d29e98), &slot, 8) || slot != (uintptr_t)LX(0x129a680) ||
        !gate_read_local(LX(0x2d25b20), &threshold, 4) || threshold != 0x4a742400)
        return 0;
    out->bind = (int (*)(player_view_xs_target_policy, player_view_xs_external_policy,
                         player_view_xs_distance_policy))bind;
    out->bound = (int (*)(void))bound;
    out->info = info;
    return 1;
}
#endif

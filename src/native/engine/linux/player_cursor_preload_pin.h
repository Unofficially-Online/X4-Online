#ifndef X4_PLAYER_CURSOR_PRELOAD_PIN_H
#define X4_PLAYER_CURSOR_PRELOAD_PIN_H
/* Include after the sealed view-query provider and before using the cursor
 * adapter. This validates the exact unaligned owned patch and reconstructs
 * only those seven bytes in a private copy of the entire Update. */
#include "player_cursor_preload.h"

typedef struct {
    int (*bind)(uint32_t owner, int (*callback)(uintptr_t controller));
    int (*targets)(uintptr_t canonical, uintptr_t guest);
    player_cursor_preload_info info;
} player_cursor_preload_api;

static int player_cursor_relative_target(uintptr_t base, size_t end, int32_t displacement, uintptr_t target) {
    if (base > (uintptr_t)INT64_MAX - end) return 0;
    int64_t origin = (int64_t)(base + end);
    if (displacement < 0 && origin < -(int64_t)displacement) return 0;
    if (displacement > 0 && origin > INT64_MAX - displacement) return 0;
    return (uintptr_t)(origin + displacement) == target;
}

static int player_cursor_preload_provider(const player_view_query_api *sealed,
                                          player_cursor_preload_api *out) {
    Dl_info provider = {0};
    if (!sealed || !out || !sealed->finish || !dladdr((void *)sealed->finish, &provider) || !provider.dli_fbase)
        return 0;
    memset(out, 0, sizeof *out);
    void *info_fn = dlsym(RTLD_DEFAULT, "x4mp_cursor_info");
    void *bind_fn = dlsym(RTLD_DEFAULT, "x4mp_cursor_bind");
    void *targets_fn = dlsym(RTLD_DEFAULT, "x4mp_cursor_targets");
    if (!player_view_pin_same_module((uintptr_t)info_fn, provider.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)bind_fn, provider.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)targets_fn, provider.dli_fbase, "r-xp")) return 0;
    player_cursor_preload_info info = {0};
    static const unsigned char original[7] = {0x80, 0x3d, 0xc7, 0x06, 0x72, 0x02, 0x00};
    if (((int (*)(player_cursor_preload_info *))info_fn)(&info) != 1 ||
        info.version != 1 || info.installed != 1 || info.site != (uintptr_t)LX(0x166d3ac) ||
        memcmp(info.original, original, sizeof original) || !info.stub || (info.stub & 4095) ||
        info.stub_size != 14 || info.bridge_size != 348 || info.frame_size != 64 || info.page_size != 4096 ||
        !player_view_pin_permissions(info.site, 7, "r-xp", 0) ||
        !player_view_pin_permissions(info.stub, 4096, "r-xp", 1) ||
        !player_view_pin_same_module(info.bridge, provider.dli_fbase, "r-xp") ||
        !player_view_pin_permissions(info.bridge, info.bridge_size, "r-xp", 0) ||
        !player_view_pin_same_module(info.dispatcher, provider.dli_fbase, "r-xp") ||
        !player_view_pin_same_module(info.resume_slot, provider.dli_fbase, "rw-p") ||
        !player_view_pin_same_module(info.frame, provider.dli_fbase, "rw-p") ||
        !player_view_pin_permissions(info.resume_slot, sizeof(uintptr_t), "rw-p", 0) ||
        !player_view_pin_permissions(info.frame, 64, "rw-p", 0)) return 0;
    /* Two PC-relative fields are relocated by the dynamic linker. Validate
     * their exact targets, then hash all 348 instruction bytes with only those
     * fields normalized to the assembler's zero placeholders. */
    unsigned char bridge_code[348];
    if (!gate_read_local((void *)info.bridge, bridge_code, sizeof bridge_code) ||
        bridge_code[0xa1] != 0xe8 || bridge_code[0x156] != 0xff || bridge_code[0x157] != 0x25) return 0;
    int32_t dispatch_relative, resume_relative;
    memcpy(&dispatch_relative, bridge_code + 0xa2, 4);
    memcpy(&resume_relative, bridge_code + 0x158, 4);
    if (!player_cursor_relative_target(info.bridge, 0xa6, dispatch_relative, info.dispatcher) ||
        !player_cursor_relative_target(info.bridge, 0x15c, resume_relative, info.resume_slot)) return 0;
    memset(bridge_code + 0xa2, 0, 4);
    memset(bridge_code + 0x158, 0, 4);
    if (!hash_ok(bridge_code, sizeof bridge_code,
                 "6db2f939d7e991c8361c861998295205543cbd36e9e8761ca55cc60df3014b4f")) return 0;
    uintptr_t resume = 0;
    if (!gate_read_local((void *)info.resume_slot, &resume, sizeof resume) || resume != (uintptr_t)LX(0x166d3b3))
        return 0;
    unsigned char page[4096], expected_page[4096], patch[7] = {0xe9,0,0,0,0,0x90,0x90};
    memset(expected_page, 0xcc, sizeof expected_page);
    expected_page[0] = 0xff; expected_page[1] = 0x25;
    memset(expected_page + 2, 0, 4);
    memcpy(expected_page + 6, &info.bridge, sizeof info.bridge);
    if (!gate_read_local((void *)info.stub, page, sizeof page) || memcmp(page, expected_page, sizeof page) ||
        info.stub > INT64_MAX || info.site > INT64_MAX - 5) return 0;
    int64_t distance = (int64_t)info.stub - (int64_t)(info.site + 5);
    if (distance < INT32_MIN || distance > INT32_MAX) return 0;
    int32_t displacement = (int32_t)distance;
    memcpy(patch + 1, &displacement, sizeof displacement);
    if (memcmp(patch, info.patch, sizeof patch)) return 0;
    unsigned char original_update[15106];
    if (!gate_read_local(LX(0x166d020), original_update, sizeof original_update) ||
        memcmp(original_update + (0x166d3ac - 0x166d020), patch, sizeof patch)) return 0;
    memcpy(original_update + (0x166d3ac - 0x166d020), original, sizeof original);
    if (!hash_ok(original_update, sizeof original_update, "f5fb3e3afe84b9c4f1ecd7c27088d49e6cf6e84971c2d8af3c05bdba15e4143f"))
        return 0;
    static const unsigned char cie[32] = {
        28,0,0,0, 0,0,0,0, 1,0,1,0x78,16,
        0x0c,7,0xc0,0x05, 0x90,1, 0x8f,2, 0x8e,3, 0x8d,4,
        0x8c,5, 0x86,6, 0x83,7, 0
    };
    unsigned char frame[64], expected_frame[64] = {0};
    uint32_t length = 20, cie_offset = 36;
    uint64_t address = info.stub, range = 14;
    memcpy(expected_frame, cie, sizeof cie);
    memcpy(expected_frame + 32, &length, 4); memcpy(expected_frame + 36, &cie_offset, 4);
    memcpy(expected_frame + 40, &address, 8); memcpy(expected_frame + 48, &range, 8);
    if (!gate_read_local((void *)info.frame, frame, sizeof frame) || memcmp(frame, expected_frame, sizeof frame))
        return 0;
    void *library = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_NOLOAD);
    if (!library) return 0;
    player_view_pin_find_fde_fn find = (player_view_pin_find_fde_fn)dlsym(library, "_Unwind_Find_FDE");
    player_view_pin_unwind_bases bases = {0};
    int valid = find && find((void *)info.stub, &bases) == (void *)(info.frame + 32) && bases.function == (void *)info.stub;
    memset(&bases, 0, sizeof bases);
    void *bridge_fde = valid ? find((void *)info.bridge, &bases) : NULL;
    Dl_info fde_owner = {0};
    valid = bridge_fde && bases.function == (void *)info.bridge && dladdr(bridge_fde, &fde_owner) &&
            fde_owner.dli_fbase == provider.dli_fbase &&
            player_view_pin_permissions((uintptr_t)bridge_fde, 4, "r--p", 0);
    dlclose(library);
    if (!valid) return 0;
    out->bind = (int (*)(uint32_t, int (*)(uintptr_t)))bind_fn;
    out->targets = (int (*)(uintptr_t, uintptr_t))targets_fn;
    out->info = info;
    return 1;
}
#endif

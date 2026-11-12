#ifndef X4_PLAYER_CAMERA_REFILL_PRELOAD_PIN_H
#define X4_PLAYER_CAMERA_REFILL_PRELOAD_PIN_H
#include "player_camera_refill_preload.h"

typedef struct {
    int (*bind)(uint32_t, uintptr_t, player_camera_refill_callback, player_camera_refill_callback);
    int (*activate)(void);
    int (*disarm)(void);
    player_camera_refill_info info;
} player_camera_refill_api;

/* Authenticate all owned bytes, original body and registered unwind data against
 * the already authenticated view-query preload before publishing callbacks. */
static int player_camera_refill_provider(const player_view_query_api *sealed, player_camera_refill_api *out) {
    Dl_info module;
    if (!sealed || !out || !sealed->finish || !dladdr((void *)sealed->finish, &module) || !module.dli_fbase) return 0;
    memset(out, 0, sizeof *out);
    void *info_fn = dlsym(RTLD_DEFAULT, "x4mp_camera_refill_info");
    void *bind_fn = dlsym(RTLD_DEFAULT, "x4mp_camera_refill_bind");
    void *activate_fn = dlsym(RTLD_DEFAULT, "x4mp_camera_refill_activate");
    void *disarm_fn = dlsym(RTLD_DEFAULT, "x4mp_camera_refill_disarm");
    if (!player_view_pin_same_module((uintptr_t)info_fn, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)bind_fn, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)activate_fn, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)disarm_fn, module.dli_fbase, "r-xp")) return 0;
    player_camera_refill_info info = {0};
    if (((int (*)(player_camera_refill_info *))info_fn)(&info) != 1 || info.version != 1 || info.installed != 1 ||
        info.entry != (uintptr_t)LX(0x192a310) || info.original_word != UINT64_C(0x8949c0ef0f665741) ||
        !info.stub || (info.stub & 4095) || info.stub_size != 14 || info.trampoline_size != 19 ||
        info.frame_size != 48 || info.page_size != 4096 ||
        !player_view_pin_permissions(info.entry, 0x588, "r-xp", 0) ||
        !player_view_pin_permissions(info.stub, 4096, "r-xp", 1) ||
        !player_view_pin_same_module(info.trampoline, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module(info.dispatcher, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module(info.original_slot, module.dli_fbase, "rw-p") ||
        !player_view_pin_same_module(info.frame, module.dli_fbase, "rw-p") ||
        !player_view_pin_permissions(info.trampoline, 19, "r-xp", 0) ||
        !player_view_pin_permissions(info.original_slot, sizeof(uintptr_t), "rw-p", 0) ||
        !player_view_pin_permissions(info.frame, 48, "rw-p", 0)) return 0;
    static const unsigned char wanted_trampoline[19] = {0x41,0x57,0x66,0x0f,0xef,0xc0,0x49,0xbb,
        0x16,0xa3,0x92,0x01,0,0,0,0,0x41,0xff,0xe3};
    unsigned char trampoline[19], page[4096], expected_page[4096], original[0x588];
    uintptr_t target = 0;
    if (!gate_read_local((void *)info.trampoline, trampoline, sizeof trampoline) ||
        memcmp(trampoline, wanted_trampoline, sizeof trampoline) ||
        !gate_read_local((void *)info.original_slot, &target, sizeof target) || target != info.trampoline) return 0;
    memset(expected_page, 0xcc, sizeof expected_page); expected_page[0]=0xff; expected_page[1]=0x25;
    memset(expected_page+2, 0, 4); memcpy(expected_page+6, &info.dispatcher, 8);
    if (!gate_read_local((void *)info.stub, page, sizeof page) || memcmp(page, expected_page, sizeof page) ||
        info.stub > INT64_MAX || info.entry > INT64_MAX - 5) return 0;
    int64_t distance = (int64_t)info.stub - (int64_t)(info.entry + 5);
    if (distance < INT32_MIN || distance > INT32_MAX) return 0;
    unsigned char patch[8] = {0xe9,0,0,0,0,0x90,0x49,0x89};
    int32_t displacement = (int32_t)distance; memcpy(patch+1, &displacement, 4);
    uint64_t patched = 0; memcpy(&patched, patch, 8);
    if (info.patched_word != patched || !gate_read_local((void *)info.entry, original, sizeof original) ||
        memcmp(original, patch, sizeof patch)) return 0;
    uint64_t word = UINT64_C(0x8949c0ef0f665741); memcpy(original, &word, 8);
    if (!hash_ok(original, sizeof original, "acb114cd09879defbadd3a2dde0901975b3bbdc6ef371335c47120b820ea3430")) return 0;
    static const lx_site loop = {"camera-refill-loop", 0xfeb940, 0x82,
        "e5b77561f311eab417410c549f180c3e0219392ae444abd39c60339dfd7e653a"};
    if (!site_ok(&loop)) return 0;
    unsigned char expected_frame[48] = {16,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,8,0x90,1,0,0};
    uint32_t length=20, cie=24; uint64_t start=info.stub, range=14;
    memcpy(expected_frame+20,&length,4); memcpy(expected_frame+24,&cie,4);
    memcpy(expected_frame+28,&start,8); memcpy(expected_frame+36,&range,8);
    unsigned char frame[48];
    if (!gate_read_local((void *)info.frame, frame, sizeof frame) || memcmp(frame, expected_frame, sizeof frame)) return 0;
    void *lib = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) return 0;
    player_view_pin_find_fde_fn find = (player_view_pin_find_fde_fn)dlsym(lib, "_Unwind_Find_FDE");
    player_view_pin_unwind_bases bases = {0};
    int ok = find && find((void *)info.stub, &bases) == (void *)(info.frame+20) && bases.function == (void *)info.stub;
    memset(&bases, 0, sizeof bases);
    void *fde = ok ? find((void *)info.trampoline, &bases) : NULL;
    Dl_info fde_module;
    ok = fde && bases.function == (void *)info.trampoline && dladdr(fde, &fde_module) &&
         fde_module.dli_fbase == module.dli_fbase && player_view_pin_permissions((uintptr_t)fde, 4, "r--p", 0);
    dlclose(lib);
    if (!ok) return 0;
    out->bind = (int (*)(uint32_t, uintptr_t, player_camera_refill_callback, player_camera_refill_callback))bind_fn;
    out->activate = (int (*)(void))activate_fn;
    out->disarm = (int (*)(void))disarm_fn;
    out->info = info;
    return 1;
}
#endif

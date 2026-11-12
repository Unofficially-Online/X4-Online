#ifndef X4_PLAYER_LIVE_POSE_PRELOAD_PIN_H
#define X4_PLAYER_LIVE_POSE_PRELOAD_PIN_H

/* Include after player_view_query_preload_pin.h. The caller must first obtain
 * its sealed view-query provider; this check changes no live bytes. */
#include "player_live_pose_preload.h"

#define PLAYER_LIVE_POSE_PIN_ENTRY UINT64_C(0x1628f20)
#define PLAYER_LIVE_POSE_PIN_WORD UINT64_C(0x4855a8678d4c5441)
#define PLAYER_LIVE_POSE_PIN_SHA "fbf95e0c5dc034ca657533ec711a24ad63bf4936937d06f224ac04af839aa21e"

typedef struct {
    int (*bind)(uint32_t, uintptr_t);
    int (*tick)(uint64_t);
    int (*finish)(player_live_pose_snapshot *);
    int (*scope_begin)(unsigned);
    int (*scope)(unsigned);
    int (*scope_result)(player_live_pose_scope_snapshot *);
    player_live_pose_info information;
} player_live_pose_api;

static int player_live_pose_provider(const player_view_query_api *sealed_provider,
                                     player_live_pose_api *output) {
    if (!sealed_provider || !output) return 0;
    memset(output, 0, sizeof *output);
    Dl_info module;
    if (!sealed_provider->finish || !dladdr((void *)sealed_provider->finish, &module) ||
        !module.dli_fbase) return 0;
    unsigned char image_digest[32];
    char image_hex[65];
    if (!gate_file_sha256(gate_env.exe, PLAYER_VIEW_PIN_IMAGE_SIZE, image_digest)) return 0;
    hex32(image_digest, image_hex);
    if (strcmp(image_hex, PLAYER_VIEW_PIN_IMAGE_SHA)) return 0;
    void *get_info = dlsym(RTLD_DEFAULT, "x4mp_live_pose_info");
    void *bind = dlsym(RTLD_DEFAULT, "x4mp_live_pose_bind");
    void *tick = dlsym(RTLD_DEFAULT, "x4mp_live_pose_tick");
    void *finish = dlsym(RTLD_DEFAULT, "x4mp_live_pose_finish");
    void *scope_begin = dlsym(RTLD_DEFAULT, "x4mp_live_pose_scope_begin");
    void *scope = dlsym(RTLD_DEFAULT, "x4mp_live_pose_scope");
    void *scope_result = dlsym(RTLD_DEFAULT, "x4mp_live_pose_scope_result");
    if (!player_view_pin_same_module((uintptr_t)get_info, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)bind, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)tick, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)finish, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)scope_begin, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)scope, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)scope_result, module.dli_fbase, "r-xp")) return 0;
    player_live_pose_info info = {0};
    if (((unsigned (*)(player_live_pose_info *))get_info)(&info) != PLAYER_LIVE_POSE_VERSION ||
        info.version != PLAYER_LIVE_POSE_VERSION || info.installed != 1 ||
        info.entry != (uintptr_t)LX(PLAYER_LIVE_POSE_PIN_ENTRY) ||
        info.original_word != PLAYER_LIVE_POSE_PIN_WORD || info.page_size != 4096 ||
        info.stub_size != 14 || info.trampoline_size != 20 || info.entry_frame_size != 48 ||
        !info.stub || (info.stub & 4095) ||
        !player_view_pin_permissions(info.entry, 0x96, "r-xp", 0) ||
        !player_view_pin_permissions(info.stub, 4096, "r-xp", 1) ||
        !player_view_pin_same_module(info.trampoline, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module(info.dispatcher, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module(info.original_slot, module.dli_fbase, "rw-p") ||
        !player_view_pin_same_module(info.entry_frame, module.dli_fbase, "rw-p") ||
        !player_view_pin_permissions(info.trampoline, 20, "r-xp", 0) ||
        !player_view_pin_permissions(info.original_slot, sizeof(uintptr_t), "rw-p", 0) ||
        !player_view_pin_permissions(info.entry_frame, 48, "rw-p", 0)) return 0;
    static const unsigned char expected_trampoline[20] = {
        0x41,0x54,0x4c,0x8d,0x67,0xa8,0x55,0x49,0xbb,0x27,
        0x8f,0x62,0x01,0,0,0,0,0x41,0xff,0xe3
    };
    uintptr_t target = 0;
    unsigned char trampoline[20], page[4096], expected_page[4096];
    if (!gate_read_local((void *)info.original_slot, &target, sizeof target) || target != info.trampoline ||
        !gate_read_local((void *)info.trampoline, trampoline, sizeof trampoline) ||
        memcmp(trampoline, expected_trampoline, sizeof trampoline)) return 0;
    memset(expected_page, 0xcc, sizeof expected_page);
    expected_page[0] = 0xff;
    expected_page[1] = 0x25;
    memset(expected_page + 2, 0, 4);
    memcpy(expected_page + 6, &info.dispatcher, 8);
    if (!gate_read_local((void *)info.stub, page, sizeof page) ||
        memcmp(page, expected_page, sizeof page) || info.stub > INT64_MAX || info.entry > INT64_MAX - 5) return 0;
    int64_t relative = (int64_t)info.stub - (int64_t)(info.entry + 5);
    if (relative < INT32_MIN || relative > INT32_MAX) return 0;
    unsigned char patch[8] = {0xe9,0,0,0,0,0x90,0x90,0x48};
    int32_t displacement = (int32_t)relative;
    memcpy(patch + 1, &displacement, 4);
    uint64_t patch_word = 0;
    memcpy(&patch_word, patch, sizeof patch_word);
    unsigned char original[0x96];
    if (info.patched_word != patch_word || !gate_read_local((void *)info.entry, original, sizeof original) ||
        memcmp(original, patch, sizeof patch)) return 0;
    uint64_t original_word = PLAYER_LIVE_POSE_PIN_WORD;
    memcpy(original, &original_word, sizeof original_word);
    if (!hash_ok(original, sizeof original, PLAYER_LIVE_POSE_PIN_SHA)) return 0;
    unsigned char expected_frame[48] = {16,0,0,0, 0,0,0,0, 1,0,1,0x78,16, 0x0c,7,8,0x90,1,0,0};
    uint32_t fde_length = 20, cie_offset = 24;
    uint64_t stub = info.stub, range = 14;
    memcpy(expected_frame + 20, &fde_length, 4);
    memcpy(expected_frame + 24, &cie_offset, 4);
    memcpy(expected_frame + 28, &stub, 8);
    memcpy(expected_frame + 36, &range, 8);
    unsigned char frame[48];
    if (!gate_read_local((void *)info.entry_frame, frame, sizeof frame) ||
        memcmp(frame, expected_frame, sizeof frame)) return 0;
    void *unwind_library = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_NOLOAD);
    if (!unwind_library) return 0;
    player_view_pin_find_fde_fn find_fde =
        (player_view_pin_find_fde_fn)dlsym(unwind_library, "_Unwind_Find_FDE");
    player_view_pin_unwind_bases bases = {0};
    int unwind_ok = find_fde && find_fde((void *)info.stub, &bases) == (void *)(info.entry_frame + 20) &&
        bases.function == (void *)info.stub;
    memset(&bases, 0, sizeof bases);
    void *trampoline_fde = unwind_ok ? find_fde((void *)info.trampoline, &bases) : NULL;
    Dl_info fde_owner;
    unwind_ok = trampoline_fde && bases.function == (void *)info.trampoline &&
        dladdr(trampoline_fde, &fde_owner) && fde_owner.dli_fbase == module.dli_fbase &&
        player_view_pin_permissions((uintptr_t)trampoline_fde, 4, "r--p", 0);
    dlclose(unwind_library);
    if (!unwind_ok) return 0;
    output->bind = (int (*)(uint32_t, uintptr_t))bind;
    output->tick = (int (*)(uint64_t))tick;
    output->finish = (int (*)(player_live_pose_snapshot *))finish;
    output->scope_begin = (int (*)(unsigned))scope_begin;
    output->scope = (int (*)(unsigned))scope;
    output->scope_result = (int (*)(player_live_pose_scope_snapshot *))scope_result;
    output->information = info;
    return 1;
}
typedef struct {
    int (*configure)(uint32_t, const float *, uintptr_t, uintptr_t);
    int (*result)(player_live_correction_snapshot *);
} player_live_correction_api;

/* Only symbols in the already authenticated resident provider may mutate an
 * owned output in the bounded correction experiment. */
static inline int player_live_correction_provider(const player_live_pose_api *provider,
                                                   player_live_correction_api *out) {
    Dl_info module;
    if (!provider || !out || !provider->finish ||
        !dladdr((void *)provider->finish, &module) || !module.dli_fbase) return 0;
    void *configure = dlsym(RTLD_DEFAULT, "x4mp_live_correction_configure");
    void *result = dlsym(RTLD_DEFAULT, "x4mp_live_correction_result");
    if (!player_view_pin_same_module((uintptr_t)configure, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)result, module.dli_fbase, "r-xp")) return 0;
    out->configure = (int (*)(uint32_t, const float *, uintptr_t, uintptr_t))configure;
    out->result = (int (*)(player_live_correction_snapshot *))result;
    return 1;
}

#endif

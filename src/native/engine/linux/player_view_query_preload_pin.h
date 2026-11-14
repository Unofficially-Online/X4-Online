#ifndef X4_PLAYER_VIEW_QUERY_PRELOAD_PIN_H
#define X4_PLAYER_VIEW_QUERY_PRELOAD_PIN_H

/* Include after LX, gate_read_local, gate_maps_find and gate_file_sha256.
 * Only an authenticated process-resident preload may normalize the copied
 * occupied getter. No live bytes are changed by this header. */
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "player_view_query_preload.h"
#include "player_view_pin_map_batch.h"

static int player_view_pin_find_mapping(uintptr_t address, char permissions[5], char *path, size_t capacity) {
    if (player_view_active_map_batch)
        return player_view_map_find(player_view_active_map_batch, address, permissions, path, capacity);
    return gate_maps_find(address, permissions, path, capacity);
}

#define PLAYER_VIEW_PIN_ENTRY UINT64_C(0xa44e40)
#define PLAYER_VIEW_PIN_ORIGINAL UINT64_C(0x4803371b01158b48)
#define PLAYER_VIEW_PIN_IMAGE_SIZE 60459280u
#define PLAYER_VIEW_PIN_IMAGE_SHA "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20"
#define PLAYER_VIEW_PIN_FUNCTION_SHA "c4ae051dfa320a46f8049b7e35f5f686c7f4455e98f33ce52b13c2cbe0c134d3"

typedef int (*player_view_query_configure_fn)(uint32_t, uint32_t);
typedef int (*player_view_query_phase_fn)(uint64_t, unsigned);
typedef int (*player_view_query_snapshot_fn)(player_view_query_snapshot *);
typedef struct {
    player_view_query_configure_fn configure;
    player_view_query_phase_fn phase;
    player_view_query_snapshot_fn snapshot;
    player_view_query_snapshot_fn finish;
    player_view_query_info information;
} player_view_query_api;

typedef struct { void *text; void *data; void *function; } player_view_pin_unwind_bases;
typedef void *(*player_view_pin_find_fde_fn)(const void *, player_view_pin_unwind_bases *);

static int player_view_pin_digest_string(const char *sha) {
    if (!sha || strlen(sha) != 64) return 0;
    for (unsigned i = 0; i < 64; ++i) {
        if (!((sha[i] >= '0' && sha[i] <= '9') || (sha[i] >= 'a' && sha[i] <= 'f'))) return 0;
    }
    return 1;
}

static int player_view_pin_permissions(uintptr_t address, size_t size, const char *wanted, int anonymous) {
    if (!address || !size || size - 1 > UINTPTR_MAX - address) return 0;
    char first[5], last[5], first_path[PATH_MAX], last_path[PATH_MAX];
    if (!player_view_pin_find_mapping(address, first, first_path, sizeof first_path) ||
        !player_view_pin_find_mapping(address + size - 1, last, last_path, sizeof last_path) ||
        strcmp(first, wanted) || strcmp(last, wanted) || strcmp(first_path, last_path)) return 0;
    return !anonymous || !first_path[0];
}

static int player_view_pin_same_module(uintptr_t address, void *base, const char *permissions) {
    Dl_info owner;
    return address && dladdr((void *)address, &owner) && owner.dli_fbase == base &&
        player_view_pin_permissions(address, 1, permissions, 0);
}

static int player_view_query_provider(const char *expected_sha256, uint64_t expected_size,
                                      player_view_query_api *output) {
    if (!output) return 0;
    memset(output, 0, sizeof *output);
    if (!player_view_pin_digest_string(expected_sha256) || !expected_size) return 0;
    void *get_info = dlsym(RTLD_DEFAULT, "x4mp_view_query_info");
    void *configure = dlsym(RTLD_DEFAULT, "x4mp_view_query_configure");
    void *phase = dlsym(RTLD_DEFAULT, "x4mp_view_query_phase");
    void *snapshot = dlsym(RTLD_DEFAULT, "x4mp_view_query_snapshot");
    void *finish = dlsym(RTLD_DEFAULT, "x4mp_view_query_finish");
    Dl_info module;
    if (!get_info || !dladdr(get_info, &module) || !module.dli_fname || !module.dli_fbase) return 0;
    const char *name = strrchr(module.dli_fname, '/');
    name = name ? name + 1 : module.dli_fname;
    if (strcmp(name, "libx4mp_player_view_query.so") ||
        !player_view_pin_same_module((uintptr_t)get_info, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)configure, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)phase, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)snapshot, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)finish, module.dli_fbase, "r-xp")) return 0;
    unsigned char digest[32];
    char hex[65];
    if (!gate_file_sha256(module.dli_fname, expected_size, digest)) return 0;
    hex32(digest, hex);
    if (strcmp(hex, expected_sha256)) return 0;

    player_view_query_info info = {0};
    if (((unsigned (*)(player_view_query_info *))get_info)(&info) != PLAYER_VIEW_QUERY_VERSION ||
        info.version != PLAYER_VIEW_QUERY_VERSION || info.installed != 1 ||
        info.entry != (uintptr_t)LX(PLAYER_VIEW_PIN_ENTRY) || info.original_word != PLAYER_VIEW_PIN_ORIGINAL ||
        info.page_size != 4096 || info.entry_stub_size != 14 || info.original_trampoline_size != 26 ||
        info.entry_frame_size != 48 || !info.entry_stub || (info.entry_stub & 4095) ||
        !player_view_pin_permissions(info.entry, 0x53, "r-xp", 0) ||
        !player_view_pin_permissions(info.entry_stub, 4096, "r-xp", 1) ||
        !player_view_pin_same_module(info.original_trampoline, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module(info.dispatcher, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module(info.original_slot, module.dli_fbase, "rw-p") ||
        !player_view_pin_same_module(info.entry_frame, module.dli_fbase, "rw-p") ||
        !player_view_pin_permissions(info.original_trampoline, 26, "r-xp", 0) ||
        !player_view_pin_permissions(info.original_slot, sizeof(uintptr_t), "rw-p", 0) ||
        !player_view_pin_permissions(info.entry_frame, 48, "rw-p", 0)) return 0;

    uintptr_t original_target = 0;
    static const unsigned char trampoline[26] = {
        0x48,0xba,0x48,0x69,0xdb,0x03,0,0,0,0, 0x48,0x8b,0x12,
        0x49,0xbb,0x47,0x4e,0xa4,0,0,0,0,0, 0x41,0xff,0xe3
    };
    unsigned char actual_trampoline[26];
    if (!gate_read_local((void *)info.original_slot, &original_target, sizeof original_target) ||
        original_target != info.original_trampoline ||
        !gate_read_local((void *)info.original_trampoline, actual_trampoline, sizeof actual_trampoline) ||
        memcmp(actual_trampoline, trampoline, sizeof trampoline)) return 0;
    unsigned char expected_page[4096], actual_page[4096];
    memset(expected_page, 0xcc, sizeof expected_page);
    expected_page[0] = 0xff; expected_page[1] = 0x25;
    memset(expected_page + 2, 0, 4);
    memcpy(expected_page + 6, &info.dispatcher, 8);
    if (!gate_read_local((void *)info.entry_stub, actual_page, sizeof actual_page) ||
        memcmp(expected_page, actual_page, sizeof expected_page)) return 0;
    if (info.entry_stub > INT64_MAX || info.entry > INT64_MAX - 5) return 0;
    int64_t relative = (int64_t)info.entry_stub - (int64_t)(info.entry + 5);
    if (relative < INT32_MIN || relative > INT32_MAX) return 0;
    unsigned char expected_patch[8] = {0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x48};
    int32_t displacement = (int32_t)relative;
    memcpy(expected_patch + 1, &displacement, 4);
    uint64_t patched = 0, actual_word = 0;
    memcpy(&patched, expected_patch, 8);
    if (info.patched_word != patched || !gate_read_local((void *)info.entry, &actual_word, 8) ||
        actual_word != patched) return 0;

    unsigned char frame[48] = {16,0,0,0, 0,0,0,0, 1,0,1,0x78,16, 0x0c,7,8,0x90,1,0,0};
    uint32_t fde_length = 20, cie_offset = 24;
    uint64_t stub = info.entry_stub, range = 14;
    memcpy(frame + 20, &fde_length, 4);
    memcpy(frame + 24, &cie_offset, 4);
    memcpy(frame + 28, &stub, 8);
    memcpy(frame + 36, &range, 8);
    unsigned char actual_frame[48];
    if (!gate_read_local((void *)info.entry_frame, actual_frame, sizeof actual_frame) ||
        memcmp(actual_frame, frame, sizeof frame)) return 0;
    void *unwind_library = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_NOLOAD);
    if (!unwind_library) return 0;
    player_view_pin_find_fde_fn find_fde =
        (player_view_pin_find_fde_fn)dlsym(unwind_library, "_Unwind_Find_FDE");
    player_view_pin_unwind_bases bases = {0};
    int unwind_ok = find_fde &&
        find_fde((void *)info.entry_stub, &bases) == (void *)(info.entry_frame + 20) &&
        bases.function == (void *)info.entry_stub;
    memset(&bases, 0, sizeof bases);
    void *trampoline_fde = unwind_ok ? find_fde((void *)info.original_trampoline, &bases) : NULL;
    Dl_info fde_owner;
    unwind_ok = trampoline_fde && bases.function == (void *)info.original_trampoline &&
        dladdr(trampoline_fde, &fde_owner) && fde_owner.dli_fbase == module.dli_fbase &&
        player_view_pin_permissions((uintptr_t)trampoline_fde, 4, "r--p", 0);
    dlclose(unwind_library);
    if (!unwind_ok) return 0;
    output->configure = (player_view_query_configure_fn)configure;
    output->phase = (player_view_query_phase_fn)phase;
    output->snapshot = (player_view_query_snapshot_fn)snapshot;
    output->finish = (player_view_query_snapshot_fn)finish;
    output->information = info;
    return 1;
}

typedef struct {
    unsigned (*info)(player_ui_callback_snapshot *);
    int (*snapshot)(player_ui_callback_snapshot *);
    int (*finish)(void);
} player_ui_callback_api;

/* Require the already sealed provider and both exact installed slot values. */
static int player_ui_callback_provider(const player_view_query_api *provider,
                                       player_ui_callback_api *out) {
    if (!provider || !out) return 0;
    memset(out, 0, sizeof *out);
    Dl_info module;
    if (!dladdr((void *)provider->finish, &module) || !module.dli_fbase) return 0;
    void *info = dlsym(RTLD_DEFAULT, "x4mp_view_query_ui_info");
    void *snapshot = dlsym(RTLD_DEFAULT, "x4mp_view_query_ui_callbacks");
    void *finish = dlsym(RTLD_DEFAULT, "x4mp_view_query_ui_finish");
    if (!player_view_pin_same_module((uintptr_t)info, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)snapshot, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)finish, module.dli_fbase, "r-xp")) return 0;
    player_ui_callback_snapshot pins = {0};
    if (((unsigned (*)(player_ui_callback_snapshot *))info)(&pins) != 1 || pins.installed != 1) return 0;
    const uintptr_t slots[] = { (uintptr_t)LX(0x3ab4988u), (uintptr_t)LX(0x3ab4990u) };
    const uintptr_t originals[] = { (uintptr_t)LX(0xa77e90u), (uintptr_t)LX(0xa60520u) };
    for (unsigned i = 0; i < 2; ++i) {
        uintptr_t actual = 0, target = 0;
        if (pins.slots[i] != slots[i] || pins.original[i] != originals[i] ||
            !player_view_pin_permissions(slots[i], sizeof actual, "r--p", 0) ||
            !player_view_pin_same_module(pins.replacement[i], module.dli_fbase, "r-xp") ||
            !player_view_pin_same_module(pins.original_slot[i], module.dli_fbase, "rw-p") ||
            !gate_read_local((void *)pins.original_slot[i], &target, sizeof target) || target != originals[i] ||
            !gate_read_local((void *)slots[i], &actual, sizeof actual) || actual != pins.replacement[i]) return 0;
    }
    out->info = (unsigned (*)(player_ui_callback_snapshot *))info;
    out->snapshot = (int (*)(player_ui_callback_snapshot *))snapshot;
    out->finish = (int (*)(void))finish;
    return 1;
}

typedef int (*player_ui_overlay_bind_fn)(uint32_t, uintptr_t, player_ui_overlay_callback,
                                         player_ui_overlay_callback);
static inline player_ui_overlay_bind_fn player_ui_overlay_bind_provider(const player_view_query_api *provider) {
    Dl_info module;
    if (!provider || !dladdr((void *)provider->finish, &module) || !module.dli_fbase) return NULL;
    void *bind = dlsym(RTLD_DEFAULT, "x4mp_view_query_ui_bind");
    if (!player_view_pin_same_module((uintptr_t)bind, module.dli_fbase, "r-xp")) return NULL;
    return (player_ui_overlay_bind_fn)bind;
}

typedef player_ui_overlay_bind_fn player_ui_overlay_bind_eight_fn;
typedef unsigned (*player_ui_overlay_progress_fn)(void);
static inline int player_ui_overlay_eight_provider(const player_view_query_api *provider,
                                                     player_ui_overlay_bind_eight_fn *bind,
                                                     player_ui_overlay_progress_fn *progress) {
    Dl_info module;
    if (!provider || !bind || !progress || !dladdr((void *)provider->finish, &module) || !module.dli_fbase)
        return 0;
    void *bind_symbol = dlsym(RTLD_DEFAULT, "x4mp_view_query_ui_bind_eight");
    void *progress_symbol = dlsym(RTLD_DEFAULT, "x4mp_view_query_ui_overlay_progress");
    if (!player_view_pin_same_module((uintptr_t)bind_symbol, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)progress_symbol, module.dli_fbase, "r-xp")) return 0;
    *bind = (player_ui_overlay_bind_eight_fn)bind_symbol;
    *progress = (player_ui_overlay_progress_fn)progress_symbol;
    return 1;
}

typedef int (*player_ui_overlay_bind_count_fn)(uint32_t, uintptr_t, player_ui_overlay_callback,
                                               player_ui_overlay_callback, unsigned);
static inline int player_ui_overlay_count_provider(const player_view_query_api *provider,
                                                    player_ui_overlay_bind_count_fn *bind,
                                                    player_ui_overlay_progress_fn *progress) {
    Dl_info module;
    if (!provider || !bind || !progress || !dladdr((void *)provider->finish, &module) || !module.dli_fbase)
        return 0;
    void *bind_symbol = dlsym(RTLD_DEFAULT, "x4mp_view_query_ui_bind_count");
    void *progress_symbol = dlsym(RTLD_DEFAULT, "x4mp_view_query_ui_overlay_progress");
    if (!player_view_pin_same_module((uintptr_t)bind_symbol, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)progress_symbol, module.dli_fbase, "r-xp")) return 0;
    *bind = (player_ui_overlay_bind_count_fn)bind_symbol;
    *progress = (player_ui_overlay_progress_fn)progress_symbol;
    return 1;
}

typedef int (*player_game_render_bind_fn)(uint32_t, player_ui_overlay_callback,
                                          player_ui_overlay_callback);
static inline int player_game_render_provider(const player_view_query_api *provider,
                                               player_game_render_bind_fn *bind,
                                               int (**activate)(void),
                                               player_ui_overlay_progress_fn *progress) {
    Dl_info module;
    player_game_render_info info = {0};
    if (!provider || !bind || !activate || !progress || !dladdr((void *)provider->finish, &module) || !module.dli_fbase)
        return 0;
    void *info_symbol = dlsym(RTLD_DEFAULT, "x4mp_view_query_game_render_info");
    void *bind_symbol = dlsym(RTLD_DEFAULT, "x4mp_view_query_game_render_bind");
    void *progress_symbol = dlsym(RTLD_DEFAULT, "x4mp_view_query_game_render_progress");
    void *activate_symbol = dlsym(RTLD_DEFAULT, "x4mp_view_query_game_render_activate");
    if (!player_view_pin_same_module((uintptr_t)info_symbol, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)bind_symbol, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)progress_symbol, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)activate_symbol, module.dli_fbase, "r-xp") ||
        ((unsigned (*)(player_game_render_info *))info_symbol)(&info) != 1 || info.installed != 1 ||
        info.slot != (uintptr_t)LX(0x3ab1280u) || info.original != 0xfb0180u ||
        !player_view_pin_permissions(info.slot, sizeof(uintptr_t), "r--p", 0) ||
        !player_view_pin_same_module(info.replacement, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module(info.original_slot, module.dli_fbase, "rw-p")) return 0;
    uintptr_t actual = 0, target = 0;
    if (!gate_read_local((void *)info.slot, &actual, sizeof actual) || actual != info.replacement ||
        !gate_read_local((void *)info.original_slot, &target, sizeof target) || target != info.original ||
        !player_view_pin_permissions((uintptr_t)LX(0xfb0180u), 0x70, "r-xp", 0) ||
        !hash_ok((const void *)LX(0xfb0180u), 0x70,
                 "fa4b6b5d88d285cb39eb2160f8382cb54b1b2b4e2578a5d9613c8fc0a4c9b59b")) return 0;
    *bind = (player_game_render_bind_fn)bind_symbol;
    *activate = (int (*)(void))activate_symbol;
    *progress = (player_ui_overlay_progress_fn)progress_symbol;
    return 1;
}

static int player_view_query_occupied_pin(const char *expected_sha256, uint64_t expected_size) {
    if (!player_view_pin_digest_string(expected_sha256) || !expected_size) return 0;
    unsigned char digest[32];
    char hex[65];
    if (!gate_file_sha256(gate_env.exe, PLAYER_VIEW_PIN_IMAGE_SIZE, digest)) return 0;
    hex32(digest, hex);
    if (strcmp(hex, PLAYER_VIEW_PIN_IMAGE_SHA)) return 0;
    unsigned char copy[0x53];
    if (!gate_read_local(LX(PLAYER_VIEW_PIN_ENTRY), copy, sizeof copy)) return 0;
    if (hash_ok(copy, sizeof copy, PLAYER_VIEW_PIN_FUNCTION_SHA)) return 1;
    player_view_query_api api;
    if (!player_view_query_provider(expected_sha256, expected_size, &api)) return 0;
    uint64_t captured_word;
    memcpy(&captured_word, copy, 8);
    if (captured_word != api.information.patched_word) return 0;
    uint64_t original = PLAYER_VIEW_PIN_ORIGINAL;
    memcpy(copy, &original, 8);
    return hash_ok(copy, sizeof copy, PLAYER_VIEW_PIN_FUNCTION_SHA);
}
#endif

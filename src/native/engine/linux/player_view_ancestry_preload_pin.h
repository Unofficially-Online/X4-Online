#ifndef X4_PLAYER_VIEW_ANCESTRY_PRELOAD_PIN_H
#define X4_PLAYER_VIEW_ANCESTRY_PRELOAD_PIN_H

/* Requires an already sealed player_view_query provider. No byte mutation. */
#include "player_view_ancestry_preload.h"
#include <stdlib.h>
#include "player_stock_view_ship_preload_pin_core.h"

typedef struct {
    int (*bind)(uint32_t, uintptr_t, uintptr_t);
    int (*tick)(uint64_t);
    int (*finish)(view_ancestry_snapshot *);
    view_ancestry_info information;
} player_view_ancestry_api;

static int player_view_ancestry_provider(const player_view_query_api *provider,
                                         player_view_ancestry_api *out) {
    static const uintptr_t blocks[] = {0x1113338u, 0x177f9c0u, 0x129bad0u};
    static const uintptr_t entries[] = {0x11132d0u, 0x177f900u, 0x129bad0u};
    static const size_t sizes[] = {133, 224, 456};
    static const unsigned offsets[] = {1, 0, 0};
    static const uint64_t words[] = {UINT64_C(0x835b00c57e82e818),
        UINT64_C(0x75c0840068765be8), UINT64_C(0xfd89485554415541)};
    static const char *hashes[] = {
        "48dd904179d64a8b5f75a87b5b989a3f7c215342c71ac5132f07b7b86b8d3b58",
        "d46a2e354ebd8f76037356afe53a778117f45401b2dfd3c64727ae42e8dfd0ef",
        "cff3c5caae429ad58f93402f0d0b754c82d467823d8298c3641b1459a1605e50"};
    static const uintptr_t targets[] = {0x1d6b1c0u, 0x1e07020u};
    static const unsigned char trampoline[] = {0x41,0x55,0x41,0x54,0x55,0x49,0xbb,
        0xd5,0xba,0x29,0x01,0,0,0,0,0x41,0xff,0xe3};
    Dl_info module;
    if (!provider || !out || !provider->finish ||
        !dladdr((void *)provider->finish, &module) || !module.dli_fbase) return 0;
    memset(out, 0, sizeof *out);
    void *info_fn = dlsym(RTLD_DEFAULT, "x4mp_view_ancestry_info");
    void *bind = dlsym(RTLD_DEFAULT, "x4mp_view_ancestry_bind");
    void *tick = dlsym(RTLD_DEFAULT, "x4mp_view_ancestry_tick");
    void *finish = dlsym(RTLD_DEFAULT, "x4mp_view_ancestry_finish");
    void *methods[] = {info_fn, bind, tick, finish};
    for (unsigned i = 0; i < 4; ++i)
        if (!player_view_pin_same_module((uintptr_t)methods[i], module.dli_fbase, "r-xp")) return 0;
    view_ancestry_info info = {0};
    if (!((int (*)(view_ancestry_info *))info_fn)(&info) ||
        info.version != VIEW_ANCESTRY_VERSION || info.installed != 1 ||
        info.stub_size != 14 || info.frame_size != 48 || info.page_size != 4096 ||
        info.ship_trampoline_size != sizeof trampoline || !info.stubs[0] || (info.stubs[0] & 4095) ||
        !player_view_pin_permissions(info.stubs[0], 4096, "r-xp", 1) ||
        !player_view_pin_same_module(info.ship_trampoline, module.dli_fbase, "r-xp") ||
        !player_view_pin_permissions(info.ship_trampoline, sizeof trampoline, "r-xp", 0)) return 0;
    unsigned char got_trampoline[sizeof trampoline];
    if (!gate_read_local((void *)info.ship_trampoline, got_trampoline, sizeof got_trampoline) ||
        memcmp(got_trampoline, trampoline, sizeof trampoline)) return 0;
    unsigned char expected_page[4096], page[4096];
    memset(expected_page, 0xcc, sizeof expected_page);
    const char *pre = getenv("X4_DEV_VIEW_PRECOMPARE_TRACE");
    if ((pre && strcmp(pre, "1")) || info.pre_enabled != (pre != NULL)) return 0;
    if (pre) {
        const uint64_t original_word = UINT64_C(0x4800658819e80c24);
        uintptr_t original_target = 0;
        if (info.pre_block != (uintptr_t)LX(0x177f9a0u) || info.pre_original_word != original_word ||
            info.pre_original_target != (uintptr_t)LX(0x1dd81c0u) ||
            info.pre_stub != info.stubs[0] + VIEW_ANCESTRY_SITES * 16u ||
            !player_view_pin_same_module(info.pre_dispatch, module.dli_fbase, "r-xp") ||
            !player_view_pin_same_module(info.pre_frame, module.dli_fbase, "rw-p") ||
            !player_view_pin_permissions(info.pre_frame, 48, "rw-p", 0) ||
            !player_view_pin_same_module(info.pre_original_slot, module.dli_fbase, "rw-p") ||
            !gate_read_local((void *)info.pre_original_slot, &original_target, sizeof original_target) ||
            original_target != info.pre_original_target) return 0;
        int64_t delta = (int64_t)info.pre_stub - (int64_t)(info.pre_block + 7);
        if (delta < INT32_MIN || delta > INT32_MAX) return 0;
        unsigned char patch[8]; memcpy(patch, &original_word, 8); patch[2] = 0xe8;
        int32_t relative = (int32_t)delta; memcpy(patch + 3, &relative, 4);
        uint64_t expected; memcpy(&expected, patch, 8);
        if (info.pre_patched_word != expected) return 0;
        unsigned char *stub = expected_page + VIEW_ANCESTRY_SITES * 16u;
        stub[0] = 0xff; stub[1] = 0x25; memset(stub + 2, 0, 4);
        memcpy(stub + 6, &info.pre_dispatch, 8);
        unsigned char frame[48], expected_frame[48] = {16,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,8,0x90,1,0,0};
        uint32_t len = 20, cie = 24; uint64_t start = info.pre_stub, range = 14;
        memcpy(expected_frame + 20, &len, 4); memcpy(expected_frame + 24, &cie, 4);
        memcpy(expected_frame + 28, &start, 8); memcpy(expected_frame + 36, &range, 8);
        if (!gate_read_local((void *)info.pre_frame, frame, 48) || memcmp(frame, expected_frame, 48)) return 0;
        unsigned char getter[85];
        if (!player_view_pin_permissions((uintptr_t)LX(0x1dd81c0u), sizeof getter, "r-xp", 0) ||
            !gate_read_local(LX(0x1dd81c0u), getter, sizeof getter) ||
            !hash_ok(getter, sizeof getter, "a0437b5716ca6a2143d11a0d25f75fb3ca04342de8588818d339ca512e75c4a6")) return 0;
    }
    for (unsigned i = 0; i < VIEW_ANCESTRY_SITES; ++i) {
        if (info.blocks[i] != (uintptr_t)LX(blocks[i]) || info.offsets[i] != offsets[i] ||
            info.original_words[i] != words[i] || info.stubs[i] != info.stubs[0] + i * 16 ||
            !player_view_pin_same_module(info.dispatchers[i], module.dli_fbase, "r-xp") ||
            !player_view_pin_same_module(info.frames[i], module.dli_fbase, "rw-p") ||
            !player_view_pin_permissions(info.frames[i], 48, "rw-p", 0) ||
            !player_view_pin_permissions((uintptr_t)LX(entries[i]), sizes[i], "r-xp", 0)) return 0;
        unsigned char *stub = expected_page + i * 16;
        stub[0] = 0xff; stub[1] = 0x25;
        memset(stub + 2, 0, 4);
        memcpy(stub + 6, &info.dispatchers[i], 8);
        if (info.stubs[i] > INT64_MAX || info.blocks[i] > (uintptr_t)INT64_MAX - offsets[i] - 5) return 0;
        int64_t delta = (int64_t)info.stubs[i] - (int64_t)(info.blocks[i] + offsets[i] + 5);
        if (delta < INT32_MIN || delta > INT32_MAX) return 0;
        unsigned char patch[8]; memcpy(patch, &words[i], sizeof patch);
        patch[offsets[i]] = i == 2 ? 0xe9 : 0xe8;
        int32_t relative = (int32_t)delta;
        memcpy(patch + offsets[i] + 1, &relative, sizeof relative);
        uint64_t word = 0; memcpy(&word, patch, sizeof word);
        unsigned char original[456];
        if (info.patched_words[i] != word ||
            !gate_read_local((void *)(uintptr_t)LX(entries[i]), original, sizes[i]) ||
            memcmp(original + blocks[i] - entries[i], patch, sizeof patch)) return 0;
        if (i == 1 && pre) {
            size_t offset = 0x177f9a0u - 0x177f900u;
            if (memcmp(original + offset, &info.pre_patched_word, 8)) return 0;
            memcpy(original + offset, &info.pre_original_word, 8);
        }
        if (i == 2) {
            const size_t ship_offset = 0x129bb21u - 0x129bad0u;
            const uint64_t ship_original = UINT64_C(0xc985000008808a8b);
            if (memcmp(original + ship_offset, &ship_original, 8)) {
                player_stock_view_ship_api ship;
                if (!player_stock_view_ship_provider_core(provider, &ship, &info) ||
                    memcmp(original + ship_offset, &ship.info.patched, 8)) return 0;
                memcpy(original + ship_offset, &ship_original, 8);
            }
        }
        memcpy(original + blocks[i] - entries[i], &words[i], sizeof words[i]);
        if (!hash_ok(original, sizes[i], hashes[i])) return 0;
        unsigned char expected_frame[48] = {16,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,8,0x90,1,0,0};
        uint32_t len = 20, cie = 24; uint64_t start = info.stubs[i], range = 14;
        memcpy(expected_frame + 20, &len, 4); memcpy(expected_frame + 24, &cie, 4);
        memcpy(expected_frame + 28, &start, 8); memcpy(expected_frame + 36, &range, 8);
        unsigned char frame[48];
        if (!gate_read_local((void *)info.frames[i], frame, sizeof frame) ||
            memcmp(frame, expected_frame, sizeof frame)) return 0;
    }
    static const struct { uintptr_t address; size_t length; const char *sha; } other_pins[] = {
        {0x1113360u, 76, "5798f27c36fee251792a259c2e5b74ba1e647907ea96fb177bcccb73b8535fdc"},
        {0x1d6b1c0u, 99, "28662e2439a706b8794ec0c9dbe9b2b31aef33695461446cfbba6466a58967f6"},
        {0x1e07020u, 51, "7fbd4e7bdb604524025c4a46cc791ae7137b7c1f19270213781e35e5b7890f51"},
    };
    for (unsigned i = 0; i < sizeof other_pins / sizeof other_pins[0]; ++i) {
        unsigned char original[99];
        if (!player_view_pin_permissions((uintptr_t)LX(other_pins[i].address), other_pins[i].length, "r-xp", 0) ||
            !gate_read_local(LX(other_pins[i].address), original, other_pins[i].length) ||
            !hash_ok(original, other_pins[i].length, other_pins[i].sha)) return 0;
    }
    if (!gate_read_local((void *)info.stubs[0], page, sizeof page) || memcmp(page, expected_page, sizeof page)) return 0;
    for (unsigned i = 0; i < 2; ++i) {
        uintptr_t target = 0;
        if (info.original_targets[i] != (uintptr_t)LX(targets[i]) ||
            !player_view_pin_same_module(info.original_slots[i], module.dli_fbase, "rw-p") ||
            !player_view_pin_permissions(info.original_slots[i], sizeof target, "rw-p", 0) ||
            !gate_read_local((void *)info.original_slots[i], &target, sizeof target) || target != info.original_targets[i]) return 0;
    }
    void *library = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_NOLOAD);
    if (!library) return 0;
    player_view_pin_find_fde_fn find = (player_view_pin_find_fde_fn)dlsym(library, "_Unwind_Find_FDE");
    int ok = find != NULL;
    for (unsigned i = 0; ok && i < VIEW_ANCESTRY_SITES; ++i) {
        player_view_pin_unwind_bases bases = {0};
        ok = find((void *)info.stubs[i], &bases) == (void *)(info.frames[i] + 20) &&
             bases.function == (void *)info.stubs[i];
    }
    if (ok && pre) {
        player_view_pin_unwind_bases pre_bases = {0};
        ok = find((void *)info.pre_stub, &pre_bases) == (void *)(info.pre_frame + 20) &&
             pre_bases.function == (void *)info.pre_stub;
        memset(&pre_bases, 0, sizeof pre_bases);
        void *bridge_fde = ok ? find((void *)info.pre_dispatch, &pre_bases) : NULL;
        Dl_info bridge_module;
        ok = bridge_fde && pre_bases.function == (void *)info.pre_dispatch &&
             dladdr(bridge_fde, &bridge_module) && bridge_module.dli_fbase == module.dli_fbase &&
             player_view_pin_permissions((uintptr_t)bridge_fde, 4, "r--p", 0);
    }
    player_view_pin_unwind_bases bases = {0};
    void *fde = ok ? find((void *)info.ship_trampoline, &bases) : NULL;
    Dl_info fde_module;
    ok = fde && bases.function == (void *)info.ship_trampoline && dladdr(fde, &fde_module) &&
         fde_module.dli_fbase == module.dli_fbase && player_view_pin_permissions((uintptr_t)fde, 4, "r--p", 0);
    dlclose(library);
    if (!ok) return 0;
    out->bind = (int (*)(uint32_t, uintptr_t, uintptr_t))bind;
    out->tick = (int (*)(uint64_t))tick;
    out->finish = (int (*)(view_ancestry_snapshot *))finish;
    out->information = info;
    return 1;
}
#endif

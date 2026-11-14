#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "player_view_ancestry_preload.h"
#include "player_view_query_preload.h"
#include "player_stock_view_ship_preload.h"
#include "player_stock_view_fov_cfi.h"
#include "player_stock_view_component_preload.h"
#include "player_stock_view_precompare_preload.h"
static _Alignas(4096) unsigned char page[4096];
static _Alignas(4096) unsigned char ship_page[4096];
static unsigned char ship_frame[PLAYER_STOCK_VIEW_FOV_FRAME_SIZE];
static player_stock_view_ship_info ship_info;
static player_stock_view_component_info component_info;
static unsigned char sites[3][456], original[3][456], others[3][99], frames[3][48], trampoline[18], fde[8];
static uintptr_t slots[2], pre_slot;
static unsigned char pre_frame[48], pre_getter[85];
static view_ancestry_info info;
static int wrong_module, wrong_permissions, wrong_unwind, missing_tick, wrong_hash, wrong_pre_module;
typedef struct { int (*finish)(player_view_query_snapshot *); } player_view_query_api;
typedef struct { const char *name; uintptr_t address; unsigned size; const char *hash; } lx_site;
static int site_ok(const lx_site *site) { return site != NULL; }
typedef struct { void *text, *data, *function; } player_view_pin_unwind_bases;
typedef void *(*player_view_pin_find_fde_fn)(const void *, player_view_pin_unwind_bases *);
static int view_finish(player_view_query_snapshot *out) { (void)out; return 1; }
static int get_info(view_ancestry_info *out) { *out = info; return 1; }
static int bind_api(uint32_t tid, uintptr_t first, uintptr_t second) { (void)tid; (void)first; (void)second; return 1; }
static int tick_api(uint64_t tick) { (void)tick; return 1; }
static int finish_api(view_ancestry_snapshot *out) { (void)out; return 1; }
static int get_ship_info(player_stock_view_ship_info *out) { *out = ship_info; return 1; }
static int bind_ship(player_stock_view_ship_policy policy) { (void)policy; return 1; }
static int get_component_info(player_stock_view_component_info *out) { *out = component_info; return 1; }
static int bind_precompare(player_stock_view_precompare_policy unused) { (void)unused; return 1; }
static int bind_component(player_stock_view_component_policy policy) { (void)policy; return 1; }
static void ship_bridge(void) {}
static void dispatch(void) {}
static void target1(void) {}
static void target2(void) {}
static uintptr_t address(uintptr_t value) {
    switch (value) {
    case 0x11132d0u: return (uintptr_t)sites[0];
    case 0x1113338u: return (uintptr_t)(sites[0] + 104);
    case 0x177f900u: return (uintptr_t)sites[1];
    case 0x177f9a0u: return (uintptr_t)(sites[1] + 160);
    case 0x1dd81c0u: return (uintptr_t)pre_getter;
    case 0x177f9c0u: return (uintptr_t)(sites[1] + 192);
    case 0x129bad0u: return (uintptr_t)sites[2];
    case 0x129bb21u: return (uintptr_t)(sites[2] + 0x51);
    case 0x1113360u: return (uintptr_t)others[0];
    case 0x1d6b1c0u: return (uintptr_t)target1;
    case 0x1e07020u: return (uintptr_t)target2;
    default: return 0;
    }
}
#define LX(value) ((void *)address(value))
static int gate_read_local(const void *src, void *dst, size_t size) {
    if (src == (void *)target1) { memcpy(dst, others[1], size); return 1; }
    if (src == (void *)target2) { memcpy(dst, others[2], size); return 1; }
    memcpy(dst, src, size); return 1;
}
static int hash_ok(const void *bytes, size_t length, const char *hash) {
    (void)hash;
    if (wrong_hash) return 0;
    const size_t sizes[] = {133, 224, 456};
    for (unsigned i = 0; i < 3; ++i) if (sizes[i] == length) return !memcmp(bytes, original[i], length);
    return length == 76 || length == 99 || length == 51 || length == 85;
}
static int player_view_pin_permissions(uintptr_t p, size_t size, const char *wanted, int anonymous) {
    (void)p; (void)size; (void)wanted; (void)anonymous; return !wrong_permissions;
}
static int player_view_pin_same_module(uintptr_t p, void *base, const char *wanted) {
    (void)base; (void)wanted; return p && !(wrong_module && p == (uintptr_t)bind_api) &&
        !(wrong_pre_module && p == (uintptr_t)bind_precompare);
}
static int fake_dladdr(const void *pointer, Dl_info *out) {
    (void)pointer; memset(out, 0, sizeof *out); out->dli_fbase = (void *)1; return 1;
}
static void *fake_find_fde(const void *pointer, player_view_pin_unwind_bases *bases) {
    if (wrong_unwind) return NULL;
    for (unsigned i = 0; i < 3; ++i) if (pointer == page + 16 * i) {
        bases->function = page + 16 * i; return frames[i] + 20;
    }
    if (pointer == page + 48) { bases->function = page + 48; return pre_frame + 20; }
    if (pointer == trampoline) { bases->function = trampoline; return fde; }
    if (pointer == ship_page) { bases->function = ship_page; return ship_frame + 28; }
    if (pointer == (void *)ship_bridge) { bases->function = (void *)ship_bridge; return fde; }
    if (pointer == (void *)dispatch) { bases->function = (void *)dispatch; return fde; }
    return NULL;
}
static void *fake_dlsym(void *handle, const char *name) {
    (void)handle;
    if (!strcmp(name, "x4mp_stock_view_precompare_bind")) return (void *)bind_precompare;
    if (!strcmp(name, "x4mp_view_ancestry_info")) return (void *)get_info;
    if (!strcmp(name, "x4mp_view_ancestry_bind")) return (void *)bind_api;
    if (!strcmp(name, "x4mp_view_ancestry_tick")) return missing_tick ? NULL : (void *)tick_api;
    if (!strcmp(name, "x4mp_view_ancestry_finish")) return (void *)finish_api;
    if (!strcmp(name, "x4mp_stock_view_ship_info")) return (void *)get_ship_info;
    if (!strcmp(name, "x4mp_stock_view_ship_bind")) return (void *)bind_ship;
    if (!strcmp(name, "x4mp_stock_view_component_info")) return (void *)get_component_info;
    if (!strcmp(name, "x4mp_stock_view_component_bind")) return (void *)bind_component;
    if (!strcmp(name, "_Unwind_Find_FDE")) return (void *)fake_find_fde;
    return NULL;
}
static void *fake_dlopen(const char *name, int flags) { (void)name; (void)flags; return (void *)1; }
static int fake_dlclose(void *handle) { (void)handle; return 0; }
#define dladdr fake_dladdr
#define dlsym fake_dlsym
#define dlopen fake_dlopen
#define dlclose fake_dlclose
#include "player_view_ancestry_preload_pin.h"
#include "player_stock_view_ship_preload_pin.h"
#include "player_stock_view_component_preload_pin.h"
#include "player_stock_view_precompare_preload_pin.h"
int main(void) {
    const uint64_t ship_mode = UINT64_C(0xc985000008808a8b);
    memcpy(original[2] + 0x51, &ship_mode, 8);
    memcpy(sites[2] + 0x51, &ship_mode, 8);
    const uint64_t words[] = {UINT64_C(0x835b00c57e82e818),UINT64_C(0x75c0840068765be8),UINT64_C(0xfd89485554415541)};
    const unsigned offsets[] = {1, 0, 0}, positions[] = {104, 192, 0};
    const unsigned char code[] = {0x41,0x55,0x41,0x54,0x55,0x49,0xbb,0xd5,0xba,0x29,0x01,0,0,0,0,0x41,0xff,0xe3};
    memcpy(trampoline, code, sizeof code); memset(page, 0xcc, sizeof page);
    info.version = 1; info.installed = 1; info.stub_size = 14; info.frame_size = 48; info.page_size = 4096;
    info.ship_trampoline = (uintptr_t)trampoline; info.ship_trampoline_size = sizeof trampoline;
    for (unsigned i = 0; i < 3; ++i) {
        memcpy(original[i] + positions[i], &words[i], 8);
        info.blocks[i] = (uintptr_t)(sites[i] + positions[i]); info.offsets[i] = offsets[i];
        info.original_words[i] = words[i]; info.stubs[i] = (uintptr_t)(page + 16 * i);
        info.dispatchers[i] = (uintptr_t)dispatch; info.frames[i] = (uintptr_t)frames[i];
        unsigned char *stub = page + 16 * i; stub[0] = 0xff; stub[1] = 0x25; memset(stub + 2, 0, 4);
        memcpy(stub + 6, &info.dispatchers[i], 8);
        int64_t delta = (int64_t)info.stubs[i] - (int64_t)(info.blocks[i] + offsets[i] + 5);
        assert(delta >= INT32_MIN && delta <= INT32_MAX);
        unsigned char patch[8]; memcpy(patch, &words[i], 8); patch[offsets[i]] = i == 2 ? 0xe9 : 0xe8;
        int32_t relative = (int32_t)delta; memcpy(patch + offsets[i] + 1, &relative, 4);
        memcpy(&info.patched_words[i], patch, 8); memcpy(sites[i] + positions[i], patch, 8);
        unsigned char header[20] = {16,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,8,0x90,1,0,0};
        memcpy(frames[i], header, 20); uint32_t length = 20, cie = 24; uint64_t range = 14;
        memcpy(frames[i] + 20, &length, 4); memcpy(frames[i] + 24, &cie, 4);
        memcpy(frames[i] + 28, &info.stubs[i], 8); memcpy(frames[i] + 36, &range, 8);
    }
    slots[0] = (uintptr_t)target1; slots[1] = (uintptr_t)target2;
    for (unsigned i = 0; i < 2; ++i) { info.original_targets[i] = slots[i]; info.original_slots[i] = (uintptr_t)&slots[i]; }
    player_view_query_api sealed = {view_finish}; player_view_ancestry_api out;
    assert(player_view_ancestry_provider(&sealed, &out));
    ship_info = (player_stock_view_ship_info){1, 1, (uintptr_t)ship_page, (uintptr_t)ship_bridge,
                                              (uintptr_t)dispatch, (uintptr_t)ship_frame, ship_mode, 0};
    memset(ship_page, 0xcc, sizeof ship_page); ship_page[0] = 0xff; ship_page[1] = 0x25;
    memset(ship_page + 2, 0, 4); memcpy(ship_page + 6, &ship_info.bridge, 8);
    player_stock_view_ship_frame(ship_frame, (uintptr_t)ship_page);
    int64_t delta = (int64_t)ship_page - (int64_t)(sites[2] + 0x51 + 5);
    assert(delta >= INT32_MIN && delta <= INT32_MAX);
    unsigned char ship_patch[8]; memcpy(ship_patch, &ship_mode, 8); ship_patch[0] = 0xe9; ship_patch[5] = 0x90;
    int32_t relative = (int32_t)delta; memcpy(ship_patch + 1, &relative, 4);
    memcpy(&ship_info.patched, ship_patch, 8); memcpy(sites[2] + 0x51, ship_patch, 8);
    assert(!setenv("X4_DEV_VIEW_PRECOMPARE_TRACE", "1", 1));
    info.pre_enabled = 1; info.pre_block = (uintptr_t)(sites[1] + 160);
    info.pre_stub = (uintptr_t)(page + 48); info.pre_dispatch = (uintptr_t)dispatch;
    info.pre_frame = (uintptr_t)pre_frame; info.pre_original_target = (uintptr_t)pre_getter;
    pre_slot = info.pre_original_target; info.pre_original_slot = (uintptr_t)&pre_slot;
    info.pre_original_word = UINT64_C(0x4800658819e80c24);
    memcpy(original[1] + 160, &info.pre_original_word, 8);
    unsigned char pre_patch[8]; memcpy(pre_patch, &info.pre_original_word, 8); pre_patch[2] = 0xe8;
    int64_t pre_delta = (int64_t)info.pre_stub - (int64_t)(info.pre_block + 7);
    assert(pre_delta >= INT32_MIN && pre_delta <= INT32_MAX);
    int32_t pre_relative = (int32_t)pre_delta; memcpy(pre_patch + 3, &pre_relative, 4);
    memcpy(&info.pre_patched_word, pre_patch, 8); memcpy(sites[1] + 160, pre_patch, 8);
    page[48] = 0xff; page[49] = 0x25; memset(page + 50, 0, 4); memcpy(page + 54, &info.pre_dispatch, 8);
    memcpy(pre_frame, frames[0], 48); memcpy(pre_frame + 28, &info.pre_stub, 8);
    assert(player_view_ancestry_provider(&sealed, &out));
    player_stock_view_precompare_api pre_api;
    assert(player_stock_view_precompare_provider(&sealed, &pre_api));
    assert(pre_api.bind == bind_precompare);
    wrong_pre_module = 1; assert(!player_stock_view_precompare_provider(&sealed, &pre_api)); wrong_pre_module = 0;
    sites[1][160] ^= 1;
    assert(!player_stock_view_precompare_provider(&sealed, &pre_api));
    sites[1][160] ^= 1;
    sites[1][160] ^= 1; assert(!player_view_ancestry_provider(&sealed, &out)); sites[1][160] ^= 1;
    pre_slot = 0; assert(!player_view_ancestry_provider(&sealed, &out)); pre_slot = info.pre_original_target;
    pre_frame[10] ^= 1; assert(!player_view_ancestry_provider(&sealed, &out)); pre_frame[10] ^= 1;
    assert(player_view_ancestry_provider(&sealed, &out));
    unsetenv("X4_DEV_VIEW_PRECOMPARE_TRACE"); info.pre_enabled = 0;
    memcpy(sites[1] + 160, &info.pre_original_word, 8); memset(page + 48, 0xcc, 16);
    player_stock_view_ship_api ship_out;
    assert(player_view_ancestry_provider(&sealed, &out));
    assert(player_stock_view_ship_provider(&sealed, &ship_out));
    component_info.version = 2; component_info.installed = 1; component_info.page = (uintptr_t)page;
    for (unsigned i = 0; i < 2; ++i) {
        component_info.bridge[i] = info.dispatchers[i]; component_info.dispatch[i] = (uintptr_t)dispatch;
        component_info.original_slot[i] = info.original_slots[i]; component_info.frame[i] = info.frames[i];
        component_info.original[i] = info.original_words[i]; component_info.patched[i] = info.patched_words[i];
    }
    player_stock_view_component_api component_out;
    assert(player_stock_view_component_provider(&sealed, &component_out));
    component_info.patched[0] ^= 1; assert(!player_stock_view_component_provider(&sealed, &component_out));
    component_info.patched[0] ^= 1;
    ship_page[20] ^= 1; assert(!player_view_ancestry_provider(&sealed, &out));
    assert(!player_stock_view_component_provider(&sealed, &component_out));
    assert(!player_stock_view_ship_provider(&sealed, &ship_out)); ship_page[20] ^= 1;
    sites[2][0x51] ^= 1; assert(!player_view_ancestry_provider(&sealed, &out)); sites[2][0x51] ^= 1;
    ship_frame[10] ^= 1; assert(!player_stock_view_ship_provider(&sealed, &ship_out)); ship_frame[10] ^= 1;
    memcpy(sites[2] + 0x51, &ship_mode, 8);
    missing_tick = 1; assert(!player_view_ancestry_provider(&sealed, &out)); missing_tick = 0;
    wrong_module = 1; assert(!player_view_ancestry_provider(&sealed, &out)); wrong_module = 0;
    wrong_permissions = 1; assert(!player_view_ancestry_provider(&sealed, &out)); wrong_permissions = 0;
    wrong_hash = 1; assert(!player_view_ancestry_provider(&sealed, &out)); wrong_hash = 0;
    wrong_unwind = 1; assert(!player_view_ancestry_provider(&sealed, &out)); wrong_unwind = 0;
    info.version = 2; assert(!player_view_ancestry_provider(&sealed, &out)); info.version = 1;
    page[40] ^= 1; assert(!player_view_ancestry_provider(&sealed, &out)); page[40] ^= 1;
    sites[1][10] ^= 1; assert(!player_view_ancestry_provider(&sealed, &out)); sites[1][10] ^= 1;
    slots[0] = 0; assert(!player_view_ancestry_provider(&sealed, &out)); slots[0] = (uintptr_t)target1;
    frames[0][12] ^= 1; assert(!player_view_ancestry_provider(&sealed, &out)); frames[0][12] ^= 1;
    trampoline[0] ^= 1; assert(!player_view_ancestry_provider(&sealed, &out)); trampoline[0] ^= 1;
    assert(player_view_ancestry_provider(&sealed, &out));
    puts("view ancestry provider pin PASS");
}

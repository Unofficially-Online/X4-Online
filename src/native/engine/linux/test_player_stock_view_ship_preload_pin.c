#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "player_stock_view_ship_preload.h"
#include "player_view_query_preload.h"
static _Alignas(4096) unsigned char page[4096];
static unsigned char code[456], original[456], frame[56], fde[8];
static player_stock_view_ship_info info;
static int wrong_module, wrong_permissions, wrong_unwind, wrong_hash;
typedef struct { int (*finish)(player_view_query_snapshot *); } player_view_query_api;
typedef struct { void *text, *data, *function; } player_view_pin_unwind_bases;
typedef void *(*player_view_pin_find_fde_fn)(const void *, player_view_pin_unwind_bases *);
static int view_finish(player_view_query_snapshot *out) { (void)out; return 1; }
static int get_info(player_stock_view_ship_info *out) { *out = info; return 1; }
static int bind_api(player_stock_view_ship_policy policy) { (void)policy; return 1; }
static void bridge(void) {} static void dispatch(void) {}
static uintptr_t address(uintptr_t value) {
    return value >= 0x129bad0 && value < 0x129bc98 ? (uintptr_t)code + value - 0x129bad0 : 0;
}
#define LX(value) ((void *)address(value))
static int gate_read_local(const void *src, void *dst, size_t size) { memcpy(dst, src, size); return 1; }
static int hash_ok(const void *bytes, size_t length, const char *hash) {
    (void)hash; return !wrong_hash && length == sizeof original && !memcmp(bytes, original, length);
}
static int player_view_pin_permissions(uintptr_t p, size_t size, const char *wanted, int anonymous) {
    (void)p; (void)size; (void)wanted; (void)anonymous; return !wrong_permissions;
}
static int player_view_pin_same_module(uintptr_t p, void *base, const char *wanted) {
    (void)base; (void)wanted; return p && !(wrong_module && p == (uintptr_t)bind_api);
}
static int fake_dladdr(const void *pointer, Dl_info *out) {
    (void)pointer; memset(out, 0, sizeof *out); out->dli_fbase = (void *)1; return 1;
}
static void *fake_find_fde(const void *pointer, player_view_pin_unwind_bases *bases) {
    if (wrong_unwind) return NULL;
    if (pointer == page) { bases->function = page; return frame + 28; }
    if (pointer == (void *)bridge) { bases->function = (void *)bridge; return fde; }
    return NULL;
}
static void *fake_dlsym(void *handle, const char *name) {
    (void)handle;
    if (!strcmp(name, "x4mp_stock_view_ship_info")) return (void *)get_info;
    if (!strcmp(name, "x4mp_stock_view_ship_bind")) return (void *)bind_api;
    if (!strcmp(name, "_Unwind_Find_FDE")) return (void *)fake_find_fde;
    return NULL;
}
static void *fake_dlopen(const char *name, int flags) { (void)name; (void)flags; return (void *)1; }
static int fake_dlclose(void *handle) { (void)handle; return 0; }
#define dladdr fake_dladdr
#define dlsym fake_dlsym
#define dlopen fake_dlopen
#define dlclose fake_dlclose
#include "player_stock_view_ship_preload_pin.h"
int main(void) {
    const uint64_t entry = UINT64_C(0xfd89485554415541);
    memcpy(original, &entry, 8);
    memcpy(code, &entry, 8);
    const size_t offset = 0x129bb21 - 0x129bad0;
    const uint64_t word = UINT64_C(0xc985000008808a8b);
    memcpy(original + offset, &word, 8);
    info = (player_stock_view_ship_info){1, 1, (uintptr_t)page, (uintptr_t)bridge,
                                        (uintptr_t)dispatch, (uintptr_t)frame, word, 0};
    memset(page, 0xcc, sizeof page); page[0] = 0xff; page[1] = 0x25;
    memset(page + 2, 0, 4); memcpy(page + 6, &info.bridge, 8);
    int64_t delta = (int64_t)page - (int64_t)(code + offset + 5);
    assert(delta >= INT32_MIN && delta <= INT32_MAX);
    unsigned char patch[8]; memcpy(patch, &word, 8); patch[0] = 0xe9; patch[5] = 0x90;
    int32_t relative = (int32_t)delta; memcpy(patch + 1, &relative, 4);
    memcpy(&info.patched, patch, 8); memcpy(code + offset, patch, 8);
    player_stock_view_ship_frame(frame, (uintptr_t)page);
    player_view_query_api sealed = {view_finish}; player_stock_view_ship_api out;
    assert(player_stock_view_ship_provider(&sealed, &out));
    wrong_module = 1; assert(!player_stock_view_ship_provider(&sealed, &out)); wrong_module = 0;
    wrong_permissions = 1; assert(!player_stock_view_ship_provider(&sealed, &out)); wrong_permissions = 0;
    wrong_hash = 1; assert(!player_stock_view_ship_provider(&sealed, &out)); wrong_hash = 0;
    wrong_unwind = 1; assert(!player_stock_view_ship_provider(&sealed, &out)); wrong_unwind = 0;
    page[100] ^= 1; assert(!player_stock_view_ship_provider(&sealed, &out)); page[100] ^= 1;
    code[2] ^= 1; assert(!player_stock_view_ship_provider(&sealed, &out)); code[2] ^= 1;
    frame[15] ^= 1; assert(!player_stock_view_ship_provider(&sealed, &out)); frame[15] ^= 1;
    assert(player_stock_view_ship_provider(&sealed, &out));
    puts("stock VIEW ship provider pin PASS");
}

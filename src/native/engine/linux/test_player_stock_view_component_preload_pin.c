#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "player_stock_view_component_preload.h"
#include "player_view_query_preload.h"
static _Alignas(4096) unsigned char page[4096];
static unsigned char code[2][224], original[2][224], frame[2][48], fde[8];
static uintptr_t slots[2];
static player_stock_view_component_info info;
static int wrong_module, wrong_permissions, wrong_unwind, wrong_hash;
typedef struct { int (*finish)(player_view_query_snapshot *); } player_view_query_api;
typedef struct { void *text, *data, *function; } player_view_pin_unwind_bases;
typedef void *(*player_view_pin_find_fde_fn)(const void *, player_view_pin_unwind_bases *);
typedef struct { const char *name; uintptr_t address; size_t size; const char *hash; } lx_site;
static int view_finish(player_view_query_snapshot *out) { (void)out; return 1; }
static int get_info(player_stock_view_component_info *out) { *out = info; return 1; }
static int bind_api(player_stock_view_component_policy policy) { (void)policy; return 1; }
static void bridge1(void) {} static void bridge2(void) {} static void dispatch(void) {}
static void target1(void) {} static void target2(void) {}
static uintptr_t address(uintptr_t value) {
    if (value >= 0x11132d0 && value < 0x11132d0 + 133) return (uintptr_t)code[0] + value - 0x11132d0;
    if (value >= 0x177f900 && value < 0x177f900 + 224) return (uintptr_t)code[1] + value - 0x177f900;
    if (value == 0x1d6b1c0) return (uintptr_t)target1;
    if (value == 0x1e07020) return (uintptr_t)target2;
    return 0;
}
#define LX(value) ((void *)address(value))
static int gate_read_local(const void *src, void *dst, size_t size) { memcpy(dst, src, size); return 1; }
static int hash_ok(const void *bytes, size_t length, const char *hash) {
    (void)hash; return !wrong_hash && ((length == 133 && !memcmp(bytes, original[0], length)) ||
        (length == 224 && !memcmp(bytes, original[1], length)));
}
static int site_ok(const lx_site *site) { (void)site; return !wrong_hash; }
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
    for (unsigned i = 0; i < 2; ++i) {
        if (pointer == page + 16 * i) { bases->function = page + 16 * i; return frame[i] + 20; }
        if (pointer == (void *)info.bridge[i]) { bases->function = (void *)info.bridge[i]; return fde; }
    }
    return NULL;
}
static void *fake_dlsym(void *handle, const char *name) {
    (void)handle;
    if (!strcmp(name, "x4mp_stock_view_component_info")) return (void *)get_info;
    if (!strcmp(name, "x4mp_stock_view_component_bind")) return (void *)bind_api;
    if (!strcmp(name, "_Unwind_Find_FDE")) return (void *)fake_find_fde;
    return NULL;
}
static void *fake_dlopen(const char *name, int flags) { (void)name; (void)flags; return (void *)1; }
static int fake_dlclose(void *handle) { (void)handle; return 0; }
#define dladdr fake_dladdr
#define dlsym fake_dlsym
#define dlopen fake_dlopen
#define dlclose fake_dlclose
#include "player_stock_view_component_preload_pin.h"
int main(void) {
    const uint64_t words[] = {UINT64_C(0x835b00c57e82e818), UINT64_C(0x75c0840068765be8)};
    const unsigned positions[] = {104, 192}, opcode_offsets[] = {1, 0};
    uintptr_t bridges[] = {(uintptr_t)bridge1, (uintptr_t)bridge2};
    slots[0] = (uintptr_t)target1; slots[1] = (uintptr_t)target2;
    memset(page, 0xcc, sizeof page); info.version = 1; info.installed = 1; info.page = (uintptr_t)page;
    for (unsigned i = 0; i < 2; ++i) {
        memcpy(original[i] + positions[i], &words[i], 8); info.original[i] = words[i];
        info.bridge[i] = bridges[i]; info.dispatch[i] = (uintptr_t)dispatch;
        info.original_slot[i] = (uintptr_t)&slots[i]; info.frame[i] = (uintptr_t)frame[i];
        int64_t delta = (int64_t)(page + 16 * i) - (int64_t)(code[i] + positions[i] + opcode_offsets[i] + 5);
        assert(delta >= INT32_MIN && delta <= INT32_MAX);
        unsigned char patch[8]; memcpy(patch, &words[i], 8); patch[opcode_offsets[i]] = 0xe8;
        int32_t relative = (int32_t)delta; memcpy(patch + opcode_offsets[i] + 1, &relative, 4);
        memcpy(&info.patched[i], patch, 8); memcpy(code[i] + positions[i], patch, 8);
        unsigned char *stub = page + 16 * i; stub[0] = 0xff; stub[1] = 0x25;
        memset(stub + 2, 0, 4); memcpy(stub + 6, &bridges[i], 8);
        unsigned char header[20] = {16,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,8,0x90,1,0,0};
        memcpy(frame[i], header, 20); uint32_t length = 20, cie = 24; uint64_t start = (uintptr_t)stub, range = 14;
        memcpy(frame[i] + 20, &length, 4); memcpy(frame[i] + 24, &cie, 4);
        memcpy(frame[i] + 28, &start, 8); memcpy(frame[i] + 36, &range, 8);
    }
    player_view_query_api sealed = {view_finish}; player_stock_view_component_api out;
    assert(player_stock_view_component_provider(&sealed, &out));
    wrong_module = 1; assert(!player_stock_view_component_provider(&sealed, &out)); wrong_module = 0;
    wrong_permissions = 1; assert(!player_stock_view_component_provider(&sealed, &out)); wrong_permissions = 0;
    wrong_hash = 1; assert(!player_stock_view_component_provider(&sealed, &out)); wrong_hash = 0;
    wrong_unwind = 1; assert(!player_stock_view_component_provider(&sealed, &out)); wrong_unwind = 0;
    page[80] ^= 1; assert(!player_stock_view_component_provider(&sealed, &out)); page[80] ^= 1;
    code[1][2] ^= 1; assert(!player_stock_view_component_provider(&sealed, &out)); code[1][2] ^= 1;
    slots[0] = 0; assert(!player_stock_view_component_provider(&sealed, &out)); slots[0] = (uintptr_t)target1;
    frame[0][12] ^= 1; assert(!player_stock_view_component_provider(&sealed, &out)); frame[0][12] ^= 1;
    assert(player_stock_view_component_provider(&sealed, &out));
    puts("stock VIEW component provider pin PASS");
}

#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "player_stock_view_fov_preload.h"
#include "player_view_query_preload.h"
static _Alignas(4096) unsigned char page[4096];
static unsigned char code[2773], original[2773], mode_frames[2][56], call_frame[48], fde[8];
static player_stock_view_fov_info info;
static uintptr_t original_slot;
static int wrong_module, wrong_permissions, wrong_unwind, wrong_hash;
typedef struct { int (*finish)(player_view_query_snapshot *); } player_view_query_api;
typedef struct { void *text, *data, *function; } player_view_pin_unwind_bases;
typedef void *(*player_view_pin_find_fde_fn)(const void *, player_view_pin_unwind_bases *);
typedef struct { const char *name; uintptr_t address; size_t size; const char *hash; } lx_site;
static int view_finish(player_view_query_snapshot *out) { (void)out; return 1; }
static int get_info(player_stock_view_fov_info *out) { *out = info; return 1; }
static int bind_api(player_stock_view_mode_policy mode, player_stock_view_fov_policy fov) { (void)mode; (void)fov; return 1; }
static int bind_observer(player_stock_view_fov_observer cb) { (void)cb; return 1; }
static void bridge1(void) {} static void bridge2(void) {} static void bridge3(void) {}
static void dispatch1(void) {} static void dispatch2(void) {} static void target(void) {}
static uintptr_t address(uintptr_t value) {
    if (value >= 0x16666b0 && value < 0x1667185) return (uintptr_t)code + value - 0x16666b0;
    if (value == 0x1926cc0) return (uintptr_t)target;
    return 0;
}
#define LX(value) ((void *)address(value))
static int gate_read_local(const void *src, void *dst, size_t size) { memcpy(dst, src, size); return 1; }
static int hash_ok(const void *bytes, size_t length, const char *hash) {
    (void)hash; return !wrong_hash && length == sizeof original && !memcmp(bytes, original, length);
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
    for (unsigned i = 0; i < 3; ++i) {
        if (pointer == page + 16 * i) {
            bases->function = page + 16 * i;
            return i == 2 ? call_frame + 20 : mode_frames[i] + 28;
        }
        uintptr_t bridge = i == 2 ? info.fov_bridge : info.mode_bridge[i];
        if (pointer == (void *)bridge) { bases->function = (void *)bridge; return fde; }
    }
    return NULL;
}
static void *fake_dlsym(void *handle, const char *name) {
    (void)handle;
    if (!strcmp(name, "x4mp_stock_view_fov_observe_bind")) return (void *)bind_observer;
    if (!strcmp(name, "x4mp_stock_view_fov_info")) return (void *)get_info;
    if (!strcmp(name, "x4mp_stock_view_fov_bind")) return (void *)bind_api;
    if (!strcmp(name, "_Unwind_Find_FDE")) return (void *)fake_find_fde;
    return NULL;
}
static void *fake_dlopen(const char *name, int flags) { (void)name; (void)flags; return (void *)1; }
static int fake_dlclose(void *handle) { (void)handle; return 0; }
#define dladdr fake_dladdr
#define dlsym fake_dlsym
#define dlopen fake_dlopen
#define dlclose fake_dlclose
#include "player_stock_view_fov_preload_pin.h"
int main(void) {
    const uintptr_t sites[] = {0x1666d2a, 0x1666e87, 0x1666d58};
    const uint64_t words[] = {UINT64_C(0x0ff300000880978b), UINT64_C(0x0ff300000880878b), UINT64_C(0x100ff3002bff63e8)};
    uintptr_t bridges[] = {(uintptr_t)bridge1, (uintptr_t)bridge2, (uintptr_t)bridge3};
    memset(page, 0xcc, sizeof page);
    info.version = 2; info.installed = 1; info.page = (uintptr_t)page;
    info.mode_bridge[0] = bridges[0]; info.mode_bridge[1] = bridges[1]; info.fov_bridge = bridges[2];
    info.mode_dispatch = (uintptr_t)dispatch1; info.fov_dispatch = (uintptr_t)dispatch2;
    info.original_fov_slot = (uintptr_t)&original_slot; original_slot = (uintptr_t)target;
    info.call_frame = (uintptr_t)call_frame;
    for (unsigned i = 0; i < 3; ++i) {
        size_t offset = sites[i] - 0x16666b0;
        memcpy(original + offset, &words[i], 8); info.original[i] = words[i];
        unsigned char patch[8]; memcpy(patch, &words[i], 8); patch[0] = i == 2 ? 0xe8 : 0xe9;
        int64_t delta = (int64_t)(page + 16 * i) - (int64_t)(code + offset + 5);
        assert(delta >= INT32_MIN && delta <= INT32_MAX);
        int32_t relative = (int32_t)delta; memcpy(patch + 1, &relative, 4); if (i != 2) patch[5] = 0x90;
        memcpy(&info.patched[i], patch, 8); memcpy(code + offset, patch, 8);
        unsigned char *stub = page + 16 * i; stub[0] = 0xff; stub[1] = 0x25;
        memset(stub + 2, 0, 4); memcpy(stub + 6, &bridges[i], 8);
        if (i < 2) { info.mode_frame[i] = (uintptr_t)mode_frames[i]; player_stock_view_fov_frame(mode_frames[i], (uintptr_t)stub); }
    }
    unsigned char header[20] = {16,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,8,0x90,1,0,0};
    memcpy(call_frame, header, 20); uint32_t length = 20, cie = 24; uint64_t start = (uintptr_t)(page + 32), range = 14;
    memcpy(call_frame + 20, &length, 4); memcpy(call_frame + 24, &cie, 4);
    memcpy(call_frame + 28, &start, 8); memcpy(call_frame + 36, &range, 8);
    player_view_query_api sealed = {view_finish}; player_stock_view_fov_api out;
    assert(player_stock_view_fov_provider(&sealed, &out));
    int (*observer_bind)(player_stock_view_fov_observer) = NULL;
    assert(player_stock_view_fov_observer_provider(&sealed, &observer_bind) && observer_bind == bind_observer);
    wrong_module = 1; assert(!player_stock_view_fov_provider(&sealed, &out)); wrong_module = 0;
    wrong_permissions = 1; assert(!player_stock_view_fov_provider(&sealed, &out)); wrong_permissions = 0;
    wrong_hash = 1; assert(!player_stock_view_fov_provider(&sealed, &out)); wrong_hash = 0;
    wrong_unwind = 1; assert(!player_stock_view_fov_provider(&sealed, &out)); wrong_unwind = 0;
    info.version = 1; assert(!player_stock_view_fov_provider(&sealed, &out)); info.version = 2;
    page[45] ^= 1; assert(!player_stock_view_fov_provider(&sealed, &out)); page[45] ^= 1;
    code[10] ^= 1; assert(!player_stock_view_fov_provider(&sealed, &out)); code[10] ^= 1;
    original_slot = 0; assert(!player_stock_view_fov_provider(&sealed, &out)); original_slot = (uintptr_t)target;
    mode_frames[0][12] ^= 1; assert(!player_stock_view_fov_provider(&sealed, &out)); mode_frames[0][12] ^= 1;
    assert(player_stock_view_fov_provider(&sealed, &out));
    puts("stock VIEW FOV provider pin PASS");
}

#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "player_view_query_preload.h"
#include "player_cursor_preload.h"
static _Alignas(4096) unsigned char page[4096];
static unsigned char update[15106], expected_update[15106], frame[64], fde[8];
static unsigned char bridge_code[348], expected_bridge[348];
static uintptr_t resume_slot = 0x166d3b3;
static player_cursor_preload_info info;
static int wrong_module, wrong_permission, wrong_hash, wrong_unwind;
#define LX(va) ((void *)((va) == 0x166d020 ? (uintptr_t)update : (va) == 0x166d3ac ? (uintptr_t)(update + 0x38c) : (va)))
typedef struct { int (*finish)(player_view_query_snapshot *); } player_view_query_api;
typedef struct { void *text, *data, *function; } player_view_pin_unwind_bases;
typedef void *(*player_view_pin_find_fde_fn)(const void *, player_view_pin_unwind_bases *);
static int view_finish(player_view_query_snapshot *out) { (void)out; return 1; }
static int get_info(player_cursor_preload_info *out) { *out = info; return 1; }
static int bind_cursor(uint32_t tid, int (*callback)(uintptr_t)) { (void)tid; (void)callback; return 1; }
static int bind_targets(uintptr_t canonical, uintptr_t guest) { (void)canonical; (void)guest; return 1; }
static void dispatch(void) {}
static int hash_ok(const void *bytes, size_t length, const char *hash) {
    if (wrong_hash || !hash) return 0;
    if (length == sizeof expected_update) return !memcmp(bytes, expected_update, length);
    if (length == sizeof expected_bridge) return !memcmp(bytes, expected_bridge, length);
    return 0;
}
static int gate_read_local(const void *src, void *dst, size_t length) { memcpy(dst, src, length); return 1; }
static int player_view_pin_permissions(uintptr_t address, size_t length, const char *wanted, int anonymous) {
    (void)address; (void)length; (void)wanted; (void)anonymous; return !wrong_permission;
}
static int player_view_pin_same_module(uintptr_t address, void *base, const char *wanted) {
    (void)base; (void)wanted; return address && !(wrong_module && address == (uintptr_t)bind_cursor);
}
static int fake_dladdr(const void *address, Dl_info *out) {
    (void)address; memset(out, 0, sizeof *out); out->dli_fbase = (void *)1; return 1;
}
static void *fake_find_fde(const void *address, player_view_pin_unwind_bases *bases) {
    if (wrong_unwind) return NULL;
    if (address == page) { bases->function = page; return frame + 32; }
    if (address == bridge_code) { bases->function = bridge_code; return fde; }
    return NULL;
}
static void *fake_dlsym(void *handle, const char *name) {
    (void)handle;
    if (!strcmp(name, "x4mp_cursor_info")) return (void *)get_info;
    if (!strcmp(name, "x4mp_cursor_bind")) return (void *)bind_cursor;
    if (!strcmp(name, "x4mp_cursor_targets")) return (void *)bind_targets;
    if (!strcmp(name, "_Unwind_Find_FDE")) return (void *)fake_find_fde;
    return NULL;
}
static void *fake_dlopen(const char *name, int flags) { (void)name; (void)flags; return (void *)1; }
static int fake_dlclose(void *handle) { (void)handle; return 0; }
#define dladdr fake_dladdr
#define dlsym fake_dlsym
#define dlopen fake_dlopen
#define dlclose fake_dlclose
#include "player_cursor_preload_pin.h"
int main(void) {
    const unsigned char original[7] = {0x80,0x3d,0xc7,0x06,0x72,0x02,0};
    memcpy(expected_update + 0x38c, original, 7);
    memcpy(update, expected_update, sizeof update);
    memset(page, 0xcc, sizeof page); page[0] = 0xff; page[1] = 0x25; memset(page + 2, 0, 4);
    uintptr_t bridge_address = (uintptr_t)bridge_code;
    memcpy(page + 6, &bridge_address, 8);
    expected_bridge[0xa1] = 0xe8; expected_bridge[0x156] = 0xff; expected_bridge[0x157] = 0x25;
    memcpy(bridge_code, expected_bridge, sizeof bridge_code);
    intptr_t dispatch_distance = (intptr_t)dispatch - (intptr_t)(bridge_code + 0xa6);
    intptr_t resume_distance = (intptr_t)&resume_slot - (intptr_t)(bridge_code + 0x15c);
    assert(dispatch_distance >= INT32_MIN && dispatch_distance <= INT32_MAX &&
           resume_distance >= INT32_MIN && resume_distance <= INT32_MAX);
    int32_t dispatch_rel = (int32_t)dispatch_distance, resume_rel = (int32_t)resume_distance;
    memcpy(bridge_code + 0xa2, &dispatch_rel, 4); memcpy(bridge_code + 0x158, &resume_rel, 4);
    const unsigned char cie[32] = {28,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,0xc0,5,0x90,1,0x8f,2,0x8e,3,0x8d,4,0x8c,5,0x86,6,0x83,7,0};
    memcpy(frame, cie, sizeof cie);
    uint32_t length = 20, offset = 36;
    uint64_t address = (uintptr_t)page, range = 14;
    memcpy(frame + 32, &length, 4); memcpy(frame + 36, &offset, 4);
    memcpy(frame + 40, &address, 8); memcpy(frame + 48, &range, 8);
    intptr_t distance = (intptr_t)page - (intptr_t)(update + 0x38c + 5);
    assert(distance >= INT32_MIN && distance <= INT32_MAX);
    unsigned char patch[7] = {0xe9,0,0,0,0,0x90,0x90};
    int32_t relative = (int32_t)distance;
    memcpy(patch + 1, &relative, 4); memcpy(update + 0x38c, patch, 7);
    info = (player_cursor_preload_info){.version=1,.installed=1,.site=(uintptr_t)(update+0x38c),
        .stub=(uintptr_t)page,.bridge=bridge_address,.dispatcher=(uintptr_t)dispatch,
        .resume_slot=(uintptr_t)&resume_slot,.frame=(uintptr_t)frame,.stub_size=14,.bridge_size=348,.frame_size=64,.page_size=4096};
    memcpy(info.original, original, 7); memcpy(info.patch, patch, 7);
    player_view_query_api sealed = {.finish=view_finish};
    player_cursor_preload_api result;
    assert(player_cursor_preload_provider(&sealed, &result) && result.bind == bind_cursor);
    update[5] ^= 1; assert(!player_cursor_preload_provider(&sealed, &result)); update[5] ^= 1;
    update[0x38c] ^= 1; assert(!player_cursor_preload_provider(&sealed, &result)); update[0x38c] ^= 1;
    page[100] ^= 1; assert(!player_cursor_preload_provider(&sealed, &result)); page[100] ^= 1;
    bridge_code[20] ^= 1; assert(!player_cursor_preload_provider(&sealed, &result)); bridge_code[20] ^= 1;
    bridge_code[0xa2] ^= 1; assert(!player_cursor_preload_provider(&sealed, &result)); bridge_code[0xa2] ^= 1;
    frame[5] ^= 1; assert(!player_cursor_preload_provider(&sealed, &result)); frame[5] ^= 1;
    resume_slot++; assert(!player_cursor_preload_provider(&sealed, &result)); resume_slot--;
    wrong_module=1; assert(!player_cursor_preload_provider(&sealed, &result)); wrong_module=0;
    wrong_permission=1; assert(!player_cursor_preload_provider(&sealed, &result)); wrong_permission=0;
    wrong_hash=1; assert(!player_cursor_preload_provider(&sealed, &result)); wrong_hash=0;
    wrong_unwind=1; assert(!player_cursor_preload_provider(&sealed, &result)); wrong_unwind=0;
    assert(player_cursor_preload_provider(&sealed, &result));
    puts("cursor provider pin PASS");
    return 0;
}

#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "player_view_query_preload.h"
#include "player_live_pose_preload.h"

static _Alignas(4096) unsigned char page[4096];
static unsigned char site[0x96], expected_site[0x96], trampoline[20], frame[48], fde[8];
static uintptr_t original_slot;
static player_live_pose_info info;
static int wrong_module, wrong_permission, wrong_hash, wrong_unwind;
static struct { const char *exe; } gate_env = {"synthetic"};
#define PLAYER_VIEW_PIN_IMAGE_SIZE 60459280u
#define PLAYER_VIEW_PIN_IMAGE_SHA "image"
#define LX(va) ((void *)((va) == UINT64_C(0x1628f20) ? (uintptr_t)site : 0))
typedef struct { int (*finish)(player_view_query_snapshot *); } player_view_query_api;
typedef struct { void *text, *data, *function; } player_view_pin_unwind_bases;
typedef void *(*player_view_pin_find_fde_fn)(const void *, player_view_pin_unwind_bases *);
static int view_finish(player_view_query_snapshot *out) { (void)out; return 1; }
static unsigned get_info(player_live_pose_info *out) { *out = info; return PLAYER_LIVE_POSE_VERSION; }
static int bind_pose(uint32_t tid, uintptr_t target) { (void)tid; (void)target; return 1; }
static int tick_pose(uint64_t tick) { (void)tick; return 1; }
static int finish_pose(player_live_pose_snapshot *out) { (void)out; return 1; }
static int scope_pose(unsigned state) { (void)state; return 1; }
static int scope_result(player_live_pose_scope_snapshot *out) { (void)out; return 1; }
static int configure_correction(uint32_t tid, const float *delta, uintptr_t low, uintptr_t high) {
    (void)tid; (void)delta; (void)low; (void)high; return 1;
}
static int correction_result(player_live_correction_snapshot *out) { (void)out; return 1; }
static int missing_correction;
static int missing_scope;
static void dispatch(void) {}
static int gate_file_sha256(const char *path, size_t size, unsigned char *digest) {
    (void)path; (void)size; memset(digest, 0, 32); return !wrong_hash;
}
static void hex32(const unsigned char *digest, char *out) { (void)digest; strcpy(out, "image"); }
static int hash_ok(const void *bytes, size_t length, const char *hash) {
    return !wrong_hash && length == sizeof expected_site && hash && !memcmp(bytes, expected_site, length);
}
static int gate_read_local(const void *src, void *dst, size_t size) { memcpy(dst, src, size); return 1; }
static int player_view_pin_permissions(uintptr_t address, size_t size, const char *wanted, int anonymous) {
    (void)address; (void)size; (void)wanted; (void)anonymous; return !wrong_permission;
}
static int player_view_pin_same_module(uintptr_t address, void *base, const char *wanted) {
    (void)base; (void)wanted; return address && !(wrong_module && address == (uintptr_t)bind_pose);
}
static int fake_dladdr(const void *address, Dl_info *out) {
    (void)address; memset(out, 0, sizeof *out); out->dli_fbase = (void *)1; return 1;
}
static void *fake_find_fde(const void *address, player_view_pin_unwind_bases *bases) {
    if (wrong_unwind) return NULL;
    if (address == page) { bases->function = page; return frame + 20; }
    if (address == trampoline) { bases->function = trampoline; return fde; }
    return NULL;
}
static void *fake_dlsym(void *handle, const char *name) {
    (void)handle;
    if (!strcmp(name, "x4mp_live_pose_info")) return (void *)get_info;
    if (!strcmp(name, "x4mp_live_pose_bind")) return (void *)bind_pose;
    if (!strcmp(name, "x4mp_live_pose_tick")) return (void *)tick_pose;
    if (!strcmp(name, "x4mp_live_pose_finish")) return (void *)finish_pose;
    if (!strcmp(name, "x4mp_live_pose_scope_begin")) return (void *)scope_pose;
    if (!strcmp(name, "x4mp_live_pose_scope")) return missing_scope ? NULL : (void *)scope_pose;
    if (!strcmp(name, "x4mp_live_pose_scope_result")) return (void *)scope_result;
    if (!strcmp(name, "x4mp_live_correction_configure")) return (void *)configure_correction;
    if (!strcmp(name, "x4mp_live_correction_result")) return missing_correction ? NULL : (void *)correction_result;
    if (!strcmp(name, "_Unwind_Find_FDE")) return (void *)fake_find_fde;
    return NULL;
}
static void *fake_dlopen(const char *name, int flags) { (void)name; (void)flags; return (void *)1; }
static int fake_dlclose(void *handle) { (void)handle; return 0; }
#define dladdr fake_dladdr
#define dlsym fake_dlsym
#define dlopen fake_dlopen
#define dlclose fake_dlclose
#include "player_live_pose_preload_pin.h"

int main(void) {
    static const unsigned char code[20] = {0x41,0x54,0x4c,0x8d,0x67,0xa8,0x55,0x49,0xbb,0x27,
                                           0x8f,0x62,0x01,0,0,0,0,0x41,0xff,0xe3};
    memcpy(trampoline, code, sizeof code);
    uint64_t original = PLAYER_LIVE_POSE_PIN_WORD;
    memcpy(expected_site, &original, 8);
    memset(page, 0xcc, sizeof page);
    page[0] = 0xff; page[1] = 0x25; memset(page + 2, 0, 4);
    uintptr_t dispatcher = (uintptr_t)dispatch;
    memcpy(page + 6, &dispatcher, 8);
    unsigned char header[20] = {16,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,8,0x90,1,0,0};
    memcpy(frame, header, sizeof header);
    uint32_t length = 20, offset = 24;
    uint64_t stub = (uintptr_t)page, range = 14;
    memcpy(frame + 20, &length, 4); memcpy(frame + 24, &offset, 4);
    memcpy(frame + 28, &stub, 8); memcpy(frame + 36, &range, 8);
    unsigned char patch[8] = {0xe9,0,0,0,0,0x90,0x90,0x48};
    int64_t difference = (int64_t)(uintptr_t)page - (int64_t)((uintptr_t)site + 5);
    assert(difference >= INT32_MIN && difference <= INT32_MAX);
    int32_t relative = (int32_t)difference;
    memcpy(patch + 1, &relative, 4); memcpy(site, patch, 8);
    original_slot = (uintptr_t)trampoline;
    info = (player_live_pose_info){.version=1, .installed=1, .entry=(uintptr_t)site,
        .stub=(uintptr_t)page, .trampoline=(uintptr_t)trampoline, .dispatcher=dispatcher,
        .original_slot=(uintptr_t)&original_slot, .entry_frame=(uintptr_t)frame,
        .original_word=original, .stub_size=14, .trampoline_size=20, .entry_frame_size=48, .page_size=4096};
    memcpy(&info.patched_word, patch, 8);
    player_view_query_api provider = {view_finish};
    player_live_pose_api result;
    assert(player_live_pose_provider(&provider, &result));
    missing_scope=1; assert(!player_live_pose_provider(&provider, &result)); missing_scope=0;
    wrong_module=1; assert(!player_live_pose_provider(&provider, &result)); wrong_module=0;
    wrong_permission=1; assert(!player_live_pose_provider(&provider, &result)); wrong_permission=0;
    wrong_hash=1; assert(!player_live_pose_provider(&provider, &result)); wrong_hash=0;
    wrong_unwind=1; assert(!player_live_pose_provider(&provider, &result)); wrong_unwind=0;
    info.version=2; assert(!player_live_pose_provider(&provider, &result)); info.version=1;
    page[40]^=1; assert(!player_live_pose_provider(&provider, &result)); page[40]^=1;
    trampoline[0]^=1; assert(!player_live_pose_provider(&provider, &result)); trampoline[0]^=1;
    site[8]^=1; assert(!player_live_pose_provider(&provider, &result)); site[8]^=1;
    original_slot=0; assert(!player_live_pose_provider(&provider, &result)); original_slot=(uintptr_t)trampoline;
    frame[12]^=1; assert(!player_live_pose_provider(&provider, &result)); frame[12]^=1;
    assert(player_live_pose_provider(&provider, &result));
    player_live_correction_api correction;
    assert(player_live_correction_provider(&result, &correction));
    missing_correction=1; assert(!player_live_correction_provider(&result, &correction));
    puts("live pose provider pin PASS");
}

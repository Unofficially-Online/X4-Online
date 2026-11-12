#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "player_view_query_preload.h"

static _Alignas(4096) unsigned char fake_page[4096];
static unsigned char fake_site[0x53], fake_original[0x53], fake_trampoline[26], fake_frame[48], fake_fde[16];
static uintptr_t original_slot;
static uintptr_t fake_ui_slots[2], fake_ui_originals[2];
static int bad_ui_permissions, bad_ui_module, bad_ui_version;
static int missing_eight_symbol, foreign_eight_symbol, missing_count_symbol;
static void fixture_ui_move(void) {}
static void fixture_ui_render(void) {}
static unsigned fixture_ui_info(player_ui_callback_snapshot *out) {
    memset(out, 0, sizeof *out);
    out->installed = 1;
    out->slots[0] = (uintptr_t)&fake_ui_slots[0];
    out->slots[1] = (uintptr_t)&fake_ui_slots[1];
    out->original[0] = 0xa77e90;
    out->original[1] = 0xa60520;
    out->original_slot[0] = (uintptr_t)&fake_ui_originals[0];
    out->original_slot[1] = (uintptr_t)&fake_ui_originals[1];
    out->replacement[0] = (uintptr_t)fixture_ui_move;
    out->replacement[1] = (uintptr_t)fixture_ui_render;
    return bad_ui_version ? 2 : 1;
}
static int fixture_ui_snapshot(player_ui_callback_snapshot *out) { (void)out; return 1; }
static int fixture_ui_finish(void) { return 1; }
static int fixture_eight_bind(uint32_t tid, uintptr_t object, player_ui_overlay_callback enter,
                              player_ui_overlay_callback leave) {
    (void)tid; (void)object; (void)enter; (void)leave; return 1;
}
static int fixture_count_bind(uint32_t tid, uintptr_t object, player_ui_overlay_callback enter,
                              player_ui_overlay_callback leave, unsigned count) {
    (void)tid; (void)object; (void)enter; (void)leave; return count == 160;
}
static unsigned fixture_overlay_progress(void) { return 1; }
static player_view_query_info information;
static int bad_permissions, bad_module, bad_digest, bad_unwind;
static struct { const char *exe; } gate_env = {"game"};
static const char sha[] = "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20";
static unsigned fixture_info(player_view_query_info *out) { *out=information;return PLAYER_VIEW_QUERY_VERSION; }
static int fixture_configure(uint32_t a,uint32_t b) { (void)a;(void)b;return 1; }
static int fixture_phase(uint64_t a,unsigned b) { (void)a;(void)b;return 1; }
static int fixture_snapshot(player_view_query_snapshot *out) { (void)out;return 1; }
static int fixture_finish(player_view_query_snapshot *out) { (void)out;return 1; }
static void fixture_dispatch(void) {}
static void *fake_unwind(const void *pc,void *raw) {
    void **bases=raw;
    if (bad_unwind) return NULL;
    if (pc==(void *)fake_page) { bases[2]=fake_page;return fake_frame+20; }
    if (pc==(void *)fake_trampoline) { bases[2]=fake_trampoline;return fake_fde; }
    return NULL;
}
static void *fake_dlopen(const char *name,int flags) { (void)name;(void)flags;return (void *)1; }
static int fake_dlclose(void *handle) { (void)handle;return 0; }
static void *fake_dlsym(void *handle,const char *name) {
    (void)handle;
    if (!strcmp(name,"x4mp_view_query_info")) return (void *)fixture_info;
    if (!strcmp(name,"x4mp_view_query_configure")) return (void *)fixture_configure;
    if (!strcmp(name,"x4mp_view_query_phase")) return (void *)fixture_phase;
    if (!strcmp(name,"x4mp_view_query_snapshot")) return (void *)fixture_snapshot;
    if (!strcmp(name,"x4mp_view_query_finish")) return (void *)fixture_finish;
    if (!strcmp(name,"x4mp_view_query_ui_info")) return (void *)fixture_ui_info;
    if (!strcmp(name,"x4mp_view_query_ui_callbacks")) return (void *)fixture_ui_snapshot;
    if (!strcmp(name,"x4mp_view_query_ui_finish")) return (void *)fixture_ui_finish;
    if (!strcmp(name,"x4mp_view_query_ui_bind_eight")) return missing_eight_symbol ? NULL : (void *)fixture_eight_bind;
    if (!strcmp(name,"x4mp_view_query_ui_bind_count")) return missing_count_symbol ? NULL : (void *)fixture_count_bind;
    if (!strcmp(name,"x4mp_view_query_ui_overlay_progress")) return (void *)fixture_overlay_progress;
    if (!strcmp(name,"_Unwind_Find_FDE")) return (void *)fake_unwind;
    return NULL;
}
static int fake_dladdr(const void *address,Dl_info *out) {
    (void)address;memset(out,0,sizeof *out);out->dli_fname="/tmp/libx4mp_player_view_query.so";
    out->dli_fbase=(bad_module && address==(void *)fixture_phase) ||
        (bad_ui_module && address==(void *)fixture_ui_render) ||
        (foreign_eight_symbol && address==(void *)fixture_overlay_progress) ? (void *)2 : (void *)1;
    return 1;
}
static int gate_maps_find(uintptr_t address,char permissions[5],char *path,size_t size) {
    const char *access="r-xp", *file="/tmp/libx4mp_player_view_query.so";
    if (address >= (uintptr_t)fake_page && address < (uintptr_t)fake_page + sizeof fake_page) file="";
    if ((address >= (uintptr_t)fake_frame && address < (uintptr_t)fake_frame+sizeof fake_frame) ||
        (address >= (uintptr_t)&original_slot && address < (uintptr_t)&original_slot+sizeof original_slot)) access="rw-p";
    if (address >= (uintptr_t)fake_fde && address < (uintptr_t)fake_fde+sizeof fake_fde) access="r--p";
    if (address >= (uintptr_t)fake_ui_originals && address < (uintptr_t)(fake_ui_originals + 2)) access="rw-p";
    if (address >= (uintptr_t)fake_ui_slots && address < (uintptr_t)(fake_ui_slots + 2))
        access = bad_ui_permissions ? "rw-p" : "r--p";
    if (bad_permissions && address==(uintptr_t)fake_page) access="rwxp";
    strcpy(permissions,access);if(path && size) snprintf(path,size,"%s",file);return 1;
}
static int gate_read_local(const void *source,void *dest,size_t size) { memcpy(dest,source,size);return 1; }
static int gate_file_sha256(const char *path,unsigned long long size,unsigned char digest[32]) {
    (void)path;(void)size;memset(digest,0,32);return !bad_digest;
}
static void hex32(const unsigned char digest[32],char *out) { (void)digest;strcpy(out,sha); }
static int hash_ok(const void *data,size_t size,const char *hash) {
    (void)hash;return size==sizeof fake_original && !memcmp(data,fake_original,size);
}
#define LX(a) ((void *)(uintptr_t)((a)==0xa44e40 ? (uintptr_t)fake_site : \
    (a)==0x3ab4988 ? (uintptr_t)&fake_ui_slots[0] : \
    (a)==0x3ab4990 ? (uintptr_t)&fake_ui_slots[1] : (a)))
#define dlsym fake_dlsym
#define dladdr fake_dladdr
#define dlopen fake_dlopen
#define dlclose fake_dlclose
#include "player_view_query_preload_pin.h"

static void setup(void) {
    static const unsigned char original_trampoline[26]={
        0x48,0xba,0x48,0x69,0xdb,0x03,0,0,0,0,0x48,0x8b,0x12,
        0x49,0xbb,0x47,0x4e,0xa4,0,0,0,0,0,0x41,0xff,0xe3};
    memset(fake_original,0x42,sizeof fake_original);
    uint64_t original=PLAYER_VIEW_PIN_ORIGINAL;
    memcpy(fake_original,&original,8);memcpy(fake_site,fake_original,sizeof fake_site);
    memcpy(fake_trampoline,original_trampoline,sizeof fake_trampoline);
    memset(fake_page,0xcc,sizeof fake_page);fake_page[0]=0xff;fake_page[1]=0x25;memset(fake_page+2,0,4);
    uintptr_t dispatch=(uintptr_t)fixture_dispatch;memcpy(fake_page+6,&dispatch,8);
    memset(fake_frame,0,sizeof fake_frame);
    const unsigned char cie[20]={16,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,8,0x90,1,0,0};
    memcpy(fake_frame,cie,sizeof cie);
    uint32_t length=20,offset=24;uint64_t page=(uintptr_t)fake_page,range=14;
    memcpy(fake_frame+20,&length,4);memcpy(fake_frame+24,&offset,4);
    memcpy(fake_frame+28,&page,8);memcpy(fake_frame+36,&range,8);
    original_slot=(uintptr_t)fake_trampoline;
    int64_t distance=(int64_t)(uintptr_t)fake_page-(int64_t)((uintptr_t)fake_site+5);
    assert(distance>=INT32_MIN && distance<=INT32_MAX);
    unsigned char patch[8]={0xe9,0,0,0,0,0x90,0x90,0x48};int32_t rel=(int32_t)distance;
    memcpy(patch+1,&rel,4);memcpy(fake_site,patch,8);
    uint64_t patch_word;memcpy(&patch_word,patch,8);
    information=(player_view_query_info){.version=1,.installed=1,.entry=(uintptr_t)fake_site,
        .entry_stub=(uintptr_t)fake_page,.original_trampoline=(uintptr_t)fake_trampoline,
        .dispatcher=dispatch,.original_slot=(uintptr_t)&original_slot,.entry_frame=(uintptr_t)fake_frame,
        .original_word=original,.patched_word=patch_word,.entry_stub_size=14,
        .original_trampoline_size=26,.entry_frame_size=48,.page_size=4096};
    bad_permissions=bad_module=bad_digest=bad_unwind=0;
    bad_ui_permissions=bad_ui_module=bad_ui_version=0;
    fake_ui_originals[0]=0xa77e90; fake_ui_originals[1]=0xa60520;
    fake_ui_slots[0]=(uintptr_t)fixture_ui_move;
    fake_ui_slots[1]=(uintptr_t)fixture_ui_render;
}
int main(void) {
    player_view_query_api api;
    setup();assert(player_view_query_provider(sha,123,&api));
    assert(api.configure && api.phase && api.snapshot && api.finish);
    player_ui_callback_api ui_api;
    assert(player_ui_callback_provider(&api, &ui_api));
    player_ui_overlay_bind_eight_fn eight_bind;
    player_ui_overlay_progress_fn progress;
    assert(player_ui_overlay_eight_provider(&api, &eight_bind, &progress));
    assert(eight_bind == fixture_eight_bind && progress == fixture_overlay_progress);
    missing_eight_symbol = 1;
    assert(!player_ui_overlay_eight_provider(&api, &eight_bind, &progress));
    missing_eight_symbol = 0;
    foreign_eight_symbol = 1;
    assert(!player_ui_overlay_eight_provider(&api, &eight_bind, &progress));
    foreign_eight_symbol = 0;
    player_ui_overlay_bind_count_fn count_bind;
    assert(player_ui_overlay_count_provider(&api, &count_bind, &progress));
    assert(count_bind == fixture_count_bind && progress == fixture_overlay_progress);
    missing_count_symbol = 1;
    assert(!player_ui_overlay_count_provider(&api, &count_bind, &progress));
    missing_count_symbol = 0;
    foreign_eight_symbol = 1;
    assert(!player_ui_overlay_count_provider(&api, &count_bind, &progress));
    foreign_eight_symbol = 0;
    fake_ui_slots[1] ^= 1; assert(!player_ui_callback_provider(&api, &ui_api));
    fake_ui_slots[1] ^= 1;
    bad_ui_permissions=1; assert(!player_ui_callback_provider(&api, &ui_api));
    bad_ui_permissions=0;
    bad_ui_module=1; assert(!player_ui_callback_provider(&api, &ui_api));
    bad_ui_module=0;
    fake_ui_originals[0]^=1; assert(!player_ui_callback_provider(&api, &ui_api));
    fake_ui_originals[0]^=1;
    bad_ui_version=1; assert(!player_ui_callback_provider(&api, &ui_api));
    bad_ui_version=0;
    assert(player_view_query_occupied_pin(sha,123));
    fake_site[15]^=1;assert(!player_view_query_occupied_pin(sha,123));
    setup();fake_site[0]^=1;assert(!player_view_query_occupied_pin(sha,123));
    setup();fake_page[4095]^=1;assert(!player_view_query_provider(sha,123,&api));
    setup();fake_trampoline[12]^=1;assert(!player_view_query_provider(sha,123,&api));
    setup();original_slot=0;assert(!player_view_query_provider(sha,123,&api));
    setup();fake_frame[13]^=1;assert(!player_view_query_provider(sha,123,&api));
    setup();bad_unwind=1;assert(!player_view_query_provider(sha,123,&api));
    setup();bad_permissions=1;assert(!player_view_query_provider(sha,123,&api));
    setup();bad_module=1;assert(!player_view_query_provider(sha,123,&api));
    setup();bad_digest=1;assert(!player_view_query_provider(sha,123,&api));
    setup();assert(!player_view_query_provider("bad",123,&api));
    setup();memcpy(fake_site,fake_original,sizeof fake_site);assert(player_view_query_occupied_pin(sha,123));
    assert(!player_view_query_occupied_pin(NULL,123));
    puts("view query original pin and provider authentication PASS");
}

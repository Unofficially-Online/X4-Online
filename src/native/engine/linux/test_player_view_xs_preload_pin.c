#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "player_view_xs_preload.h"
static _Alignas(4096) unsigned char page[4096];
static unsigned char code[0x111], original[0x111], frame[3][48], fde[8];
static player_view_xs_preload_info info;
static uintptr_t originals[3], slot;
static uint32_t threshold = 0x4a742400;
static int bad_hash, bad_module, bad_unwind;
typedef struct { int (*finish)(void *); } player_view_query_api;
typedef struct { void *text, *data, *function; } player_view_pin_unwind_bases;
typedef void *(*player_view_pin_find_fde_fn)(const void *, player_view_pin_unwind_bases *);
typedef struct { const char *name; uintptr_t address; size_t size; const char *hash; } lx_site;
static int finish(void *p) { (void)p; return 1; }
static int get_info(player_view_xs_preload_info *out) { *out = info; return 1; }
static int bound_api(void) { return 1; }
static int bind_api(player_view_xs_target_policy a, player_view_xs_external_policy b, player_view_xs_distance_policy c) {
    (void)a; (void)b; (void)c; return 1;
}
static void bridge0(void) {} static void bridge1(void) {} static void bridge2(void) {}
static void target0(void) {} static void target1(void) {} static void target2(void) {}
static uintptr_t address(uintptr_t v) {
    if (v >= 0x129a680 && v < 0x129a791) return (uintptr_t)code + v - 0x129a680;
    if (v == 0x1c90380) return (uintptr_t)target0;
    if (v == 0x1e07020) return (uintptr_t)target1;
    if (v == 0x1c98690) return (uintptr_t)target2;
    if (v == 0x3d29e98) return (uintptr_t)&slot;
    if (v == 0x2d25b20) return (uintptr_t)&threshold;
    return 0;
}
#define LX(v) ((void *)address(v))
static int gate_read_local(const void *s, void *d, size_t n) { memcpy(d,s,n); return 1; }
static int hash_ok(const void *b, size_t n, const char *h) { (void)h; return !bad_hash && n == sizeof original && !memcmp(b,original,n); }
static int site_ok(const lx_site *site) { (void)site; return !bad_hash; }
static int player_view_pin_permissions(uintptr_t p,size_t n,const char *w,int a) { (void)p;(void)n;(void)w;(void)a;return 1; }
static int player_view_pin_same_module(uintptr_t p,void *b,const char *w) { (void)b;(void)w; return p && !(bad_module && p==(uintptr_t)bind_api); }
static int fake_dladdr(const void *p,Dl_info *d) { (void)p; memset(d,0,sizeof *d);d->dli_fbase=(void *)1;return 1; }
static void *find_fde(const void *p,player_view_pin_unwind_bases *b) {
    if (bad_unwind) return NULL;
    for (unsigned i = 0; i < 3; ++i) {
        if (p == page + i*16) { b->function = page + i*16; return frame[i] + 20; }
        if (p == (void *)info.bridge[i]) { b->function = (void *)info.bridge[i]; return fde; }
    }
    return NULL;
}
static void *fake_dlsym(void *h,const char *n) { (void)h;
    if (!strcmp(n,"x4mp_view_xs_info")) return (void *)get_info;
    if (!strcmp(n,"x4mp_view_xs_bind")) return (void *)bind_api;
    if (!strcmp(n,"x4mp_view_xs_bound")) return (void *)bound_api;
    if (!strcmp(n,"_Unwind_Find_FDE")) return (void *)find_fde;
    return NULL;
}
static void *fake_dlopen(const char *n,int f) {(void)n;(void)f;return (void *)1;}
static int fake_dlclose(void *h) {(void)h;return 0;}
#define dladdr fake_dladdr
#define dlsym fake_dlsym
#define dlopen fake_dlopen
#define dlclose fake_dlclose
#include "player_view_xs_preload_pin.h"
int main(void) {
    static const uintptr_t sites[] = {0x129a6be,0x129a75c,0x129a779};
    static const unsigned char bytes[3][5] = {{0xe8,0xbd,0x5c,0x9f,0},{0xe8,0xbf,0xc8,0xb6,0},{0xe8,0x12,0xdf,0x9f,0}};
    info.version=1; info.installed=1; info.page=(uintptr_t)page;
    info.bridge[0]=(uintptr_t)bridge0;info.bridge[1]=(uintptr_t)bridge1;info.bridge[2]=(uintptr_t)bridge2;
    originals[0]=(uintptr_t)target0; originals[1]=(uintptr_t)target1; originals[2]=(uintptr_t)target2;
    memset(page,0xcc,sizeof page);
    for (unsigned i=0;i<3;++i) {
        size_t offset=sites[i]-0x129a680;memcpy(original+offset,bytes[i],5);memcpy(info.original[i],bytes[i],5);
        int64_t delta=(int64_t)(uintptr_t)(page+i*16)-(int64_t)(address(sites[i])+5);assert(delta>=INT32_MIN && delta<=INT32_MAX);
        int32_t rel=(int32_t)delta;info.patched[i][0]=0xe8;memcpy(info.patched[i]+1,&rel,4);
        info.original_slot[i]=(uintptr_t)&originals[i];info.frame[i]=(uintptr_t)frame[i];
        unsigned char *stub=page+i*16;stub[0]=0xff;stub[1]=0x25;memset(stub+2,0,4);memcpy(stub+6,&info.bridge[i],8);
        unsigned char head[20]={16,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,8,0x90,1,0,0};memcpy(frame[i],head,20);
        uint32_t len=20,cie=24;uint64_t start=(uintptr_t)(page+i*16),extent=14;
        memcpy(frame[i]+20,&len,4);memcpy(frame[i]+24,&cie,4);memcpy(frame[i]+28,&start,8);memcpy(frame[i]+36,&extent,8);
    }
    memcpy(code,original,sizeof code);for (unsigned i=0;i<3;++i) memcpy(code+sites[i]-0x129a680,info.patched[i],5);
    slot=(uintptr_t)code;player_view_query_api sealed={finish};player_view_xs_preload_api out;
    assert(player_view_xs_preload_provider(&sealed,&out));
    bad_hash=1;assert(!player_view_xs_preload_provider(&sealed,&out));bad_hash=0;
    bad_module=1;assert(!player_view_xs_preload_provider(&sealed,&out));bad_module=0;
    bad_unwind=1;assert(!player_view_xs_preload_provider(&sealed,&out));bad_unwind=0;
    code[0x3e]^=1;assert(!player_view_xs_preload_provider(&sealed,&out));code[0x3e]^=1;
    page[100]^=1;assert(!player_view_xs_preload_provider(&sealed,&out));page[100]^=1;
    originals[1]=0;assert(!player_view_xs_preload_provider(&sealed,&out));originals[1]=(uintptr_t)target1;
    slot=0;assert(!player_view_xs_preload_provider(&sealed,&out));
    return 0;
}

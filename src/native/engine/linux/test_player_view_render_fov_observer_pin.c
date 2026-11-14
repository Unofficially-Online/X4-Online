#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "player_view_render_fov_observer.h"
static _Alignas(4096) unsigned char page[4096];
static unsigned char code[0xa1], original[0xa1], frame[48], fde[8];
static player_view_render_fov_info info;
static uintptr_t original_slot;
static int bad_hash, bad_module, bad_unwind;
typedef struct { int (*finish)(void *); } player_view_query_api;
typedef struct { void *text, *data, *function; } player_view_pin_unwind_bases;
typedef void *(*player_view_pin_find_fde_fn)(const void *, player_view_pin_unwind_bases *);
typedef struct { const char *name; uintptr_t address; size_t size; const char *hash; } lx_site;
static int finish(void *p) { (void)p; return 1; }
static int get_info(player_view_render_fov_info *out) { *out=info; return 1; }
static int bind_api(player_view_render_fov_observer cb) { (void)cb; return 1; }
static void dispatch(void) {} static void target(void) {}
static uintptr_t address(uintptr_t v) {
    if (v >= 0x1926f80 && v < 0x1927021) return (uintptr_t)code + v - 0x1926f80;
    if (v == 0x1926cc0) return (uintptr_t)target;
    return 0;
}
#define LX(v) ((void *)address(v))
static int gate_read_local(const void *s, void *d, size_t n) { memcpy(d,s,n); return 1; }
static int hash_ok(const void *b, size_t n, const char *h) { (void)h; return !bad_hash && n == sizeof original && !memcmp(b, original,n); }
static int site_ok(const lx_site *s) { (void)s; return !bad_hash; }
static int player_view_pin_permissions(uintptr_t p,size_t n,const char *w,int a) { (void)p;(void)n;(void)w;(void)a;return 1; }
static int player_view_pin_same_module(uintptr_t p,void *b,const char *w) { (void)b;(void)w; return p && !(bad_module && p==(uintptr_t)bind_api); }
static int fake_dladdr(const void *p,Dl_info *d) { (void)p; memset(d,0,sizeof *d);d->dli_fbase=(void *)1;return 1; }
static void *find_fde(const void *p,player_view_pin_unwind_bases *b) {
    if (bad_unwind) return NULL;
    if (p==page) {b->function=page; return frame+20;}
    if (p==(void *)dispatch) {b->function=(void *)dispatch;return fde;}
    return NULL;
}
static void *fake_dlsym(void *h,const char *n) { (void)h;
    if (!strcmp(n,"x4mp_view_render_fov_info")) return (void *)get_info;
    if (!strcmp(n,"x4mp_view_render_fov_bind")) return (void *)bind_api;
    if (!strcmp(n,"_Unwind_Find_FDE")) return (void *)find_fde;
    return NULL;
}
static void *fake_dlopen(const char *n,int f) {(void)n;(void)f;return (void *)1;}
static int fake_dlclose(void *h) {(void)h;return 0;}
#define dladdr fake_dladdr
#define dlsym fake_dlsym
#define dlopen fake_dlopen
#define dlclose fake_dlclose
#include "player_view_render_fov_observer_pin.h"
int main(void) {
    const uint64_t word=UINT64_C(0x100ff3fffffcf1e8); memcpy(original+0x4a,&word,8);
    info=(player_view_render_fov_info){1,1,(uintptr_t)page,(uintptr_t)dispatch,(uintptr_t)&original_slot,(uintptr_t)frame,word,word};
    int64_t delta=(int64_t)page-(int64_t)(code+0x4a+5); assert(delta>=INT32_MIN && delta<=INT32_MAX);
    int32_t rel=(int32_t)delta; memcpy((unsigned char *)&info.patched+1,&rel,4);
    memcpy(code,original,sizeof code);memcpy(code+0x4a,&info.patched,8);original_slot=(uintptr_t)target;
    memset(page,0xcc,sizeof page);page[0]=0xff;page[1]=0x25;memset(page+2,0,4);memcpy(page+6,&info.dispatch,8);
    unsigned char head[20]={16,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,8,0x90,1,0,0};memcpy(frame,head,20);
    uint32_t len=20,cie=24;uint64_t start=(uintptr_t)page,extent=14;
    memcpy(frame+20,&len,4);memcpy(frame+24,&cie,4);memcpy(frame+28,&start,8);memcpy(frame+36,&extent,8);
    player_view_query_api sealed={finish};player_view_render_fov_api out;
    assert(player_view_render_fov_provider(&sealed,&out));
    bad_hash=1;assert(!player_view_render_fov_provider(&sealed,&out));bad_hash=0;
    bad_module=1;assert(!player_view_render_fov_provider(&sealed,&out));bad_module=0;
    bad_unwind=1;assert(!player_view_render_fov_provider(&sealed,&out));bad_unwind=0;
    code[0x4a]^=1;assert(!player_view_render_fov_provider(&sealed,&out));code[0x4a]^=1;
    page[100]^=1;assert(!player_view_render_fov_provider(&sealed,&out));page[100]^=1;
    original_slot=0;assert(!player_view_render_fov_provider(&sealed,&out));
    return 0;
}

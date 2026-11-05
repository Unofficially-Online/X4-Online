/* Warps on a keyed word on the Linux X4 9.00 executable (native_gate_warp_rng = 'split' or 'observe'), and the warp
 * watch: the Linux twin of lockstep_warp_rng.h, whose ordinals, keyed dispatch, rows, watch rows and status are
 * shared. Included by gate_linux.c only, after gate_step_trace.h and gate_tempzone_order.h (path_thread_name).
 *
 * Linux sites. U::WarpEvent's vtable 0x3b83440 (typeinfo 0x3d56338) holds Dispatch 0x16b3e30 in slot 3 (Itanium's two
 * destructors: Windows slot 2), the entry 0x3b83458 in RELRO, swapped like the player timer's slot. Dispatch unpacks
 * the event as on Windows (+0x18 target space, +0x20 transform pointer, +0x28 record, +0x30/+0x31 flags) and calls the
 * listener's slot +0xc0 (MSVC slot 23 + 1): U::Object 0x14620b0 (Windows 0x6cf760; ShipSmall's 0x12b0a80 jumps to it).
 * Its System V arguments are Windows' in order: rdi event, rsi listener, rdx arg, xmm0 time (kept across the flag
 * test, passed on). OnWarpEvent with a record whose first byte is set asks the safe-position search 0x1155360 (Windows
 * 0x835b80) at 0x14623ae: radius record+4, else the size (slot +0x14f8, +0x20) * 1.1, the thread's game time, record+1
 * and record+0x38, and record+0x30 as the search's optional state: with it the two draws are FloatState on that state
 * (0x1a48690), without it on the thread's word (0x1a491c0 at 0x11557f8 and 0x1155809), always both. Pins (lx_sites
 * warp_*): Dispatch, OnWarpEvent's record test and search call, the search's state test and both draw pairs.
 * Not ported: the constructor's poster attribution (Windows thunk 2976; rows here carry site 0, thread none, posted 0).
 * Arming, the per-tick check (stop reason 143) and the restore are this file's. */
#define WR_OWN_PLATFORM
#define WR_TLS_NOW(tls,out) do {double *wr_time_=(tls)?gate_tls_time((unsigned char*)(tls)):NULL;if(wr_time_) memcpy((out),wr_time_,8);} while(0)
#define __try if(1)
#define __finally
#define AbnormalTermination() 0
#include "lockstep_warp_rng.h"
#undef __try
#undef __finally
#undef AbnormalTermination
static const lx_site *lxwr_site(const char *name) {
    for(unsigned i=0;i<gate_site_count;i++) if(!strcmp(gate_sites[i].name,name)) return &gate_sites[i];
    return NULL;
}
static int wr_pins(void) {
    return site_ok(lxwr_site("warp_dispatch")) && site_ok(lxwr_site("warp_place")) && site_ok(lxwr_site("warp_direction")) &&
        site_ok(lxwr_site("warp_draws"));
}
static __attribute__((noinline)) void wr_dispatch(void *event,void *listener,void *arg,double time) {
    unsigned char *tls=gate_tls_self();
    wr_run(wr_original_override?wr_original_override:(wr_fn)(void*)LX(LX_WR_DISPATCH_VA),tls?gate_tls_rng(tls):NULL,tls,event,listener,arg,time);
}
static int wr_owned(void) {
    void *now;memcpy(&now,LX(LX_WR_ENTRY_VA),8);return !wr_installed || now==(void*)wr_dispatch;
}
static int lxwr_swap(void *expected,void *value) {
    return gate_data_swap64(LX(LX_WR_ENTRY_VA),(uint64_t)(uintptr_t)expected,(uint64_t)(uintptr_t)value);
}
/* ---- the frame capture: the gate poster 0x1614ff0's WarpEvent construction (call at 0x1615867, rdx = r14 = rbp-0x180,
 * the product A*B of A [rbp-0x200] and B [rbp-0x1c0]) through a stub; pins warp_frame_product and warp_frame_call
 * (the call's displacement read as the original's). A mismatch only leaves the frames out (frame_pin=0). */
#define LXWR_FRAME_BLOCK (LX_WR_FRAME_CALL_VA&~15u)
typedef void *(*lxwr_ctor_fn)(void *,void *,const void *,void *,unsigned,unsigned);
static lxwr_ctor_fn lxwr_ctor_override; /* tests only; NULL calls 0x16bbe40 */
static unsigned char *lxwr_page;
static int lxwr_frame_installed;
static void *lxwr_frame_ctor(void *event,void *space,const void *transform,void *record,unsigned a,unsigned b) {
    wr_frame_note(event,(const unsigned char*)transform);
    return (lxwr_ctor_override?lxwr_ctor_override:(lxwr_ctor_fn)(void*)LX(LX_WR_CTOR_VA))(event,space,transform,record,a,b);
}
static int lxwr_frame_block(const void *target,unsigned __int128 *out) {
    unsigned at=LX_WR_FRAME_CALL_VA+1-LXWR_FRAME_BLOCK;if(at>12u) return 0;
    unsigned char b[16];memcpy(b,LX(LXWR_FRAME_BLOCK),16);
    int32_t d=(int32_t)((intptr_t)target-(intptr_t)(LX(LX_WR_FRAME_CALL_VA)+5));memcpy(b+at,&d,4);memcpy(out,b,16);return 1;
}
/* 0 original, 1 ours, -1 anything else. */
static int lxwr_frame_state(void) {
    unsigned char op;int32_t d;
    if(!gate_read_local(LX(LX_WR_FRAME_CALL_VA),&op,1) || op!=0xe8 || !gate_read_local(LX(LX_WR_FRAME_CALL_VA)+1,&d,4)) return -1;
    unsigned char *to=LX(LX_WR_FRAME_CALL_VA)+5+d;
    return to==LX(LX_WR_CTOR_VA)?0:lxwr_page && to==lxwr_page?1:-1;
}
static int lxwr_frame_pins(void) {
    if(!site_ok(lxwr_site("warp_frame_product"))) return 0;
    const lx_site *s=lxwr_site("warp_frame_call");unsigned char copy[0x80];
    if(!s || s->length>sizeof(copy) || !gate_read_local(LX(s->va),copy,s->length) || lxwr_frame_state()<0) return 0;
    int32_t d=(int32_t)((intptr_t)LX(LX_WR_CTOR_VA)-(intptr_t)(LX(LX_WR_FRAME_CALL_VA)+5));memcpy(copy+(LX_WR_FRAME_CALL_VA-s->va)+1,&d,4);
    return hash_ok(copy,s->length,s->sha256);
}
static int lxwr_frame_swap(const void *from,const void *to) {
    unsigned __int128 expected,desired;
    return lxwr_frame_block(from,&expected) && lxwr_frame_block(to,&desired) && gate_code_swap128(LX(LXWR_FRAME_BLOCK),expected,desired);
}
static int lxwr_frame_install(void) {
    if(lxwr_frame_installed) return 1;
    if(!lxwr_frame_pins() || lxwr_frame_state()!=0) return 0;
    if(!lxwr_page) lxwr_page=gate_near_page(LX(LX_WR_FRAME_CALL_VA));
    if(!lxwr_page) return 0;
    intptr_t d=(intptr_t)lxwr_page-(intptr_t)(LX(LX_WR_FRAME_CALL_VA)+5);if(d<INT32_MIN || d>INT32_MAX) return 0;
    if(mprotect(lxwr_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxwr_page,0xcc,GATE_PAGE);void *t=(void*)lxwr_frame_ctor;lxwr_page[0]=0xff;lxwr_page[1]=0x25;memset(lxwr_page+2,0,4);memcpy(lxwr_page+6,&t,8);
    if(mprotect(lxwr_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    if(!lxwr_frame_swap(LX(LX_WR_CTOR_VA),lxwr_page)) return 0;
    lxwr_frame_installed=1;return 1;
}
static int lxwr_frame_owned(void) {
    if(!lxwr_frame_installed) return 1;
    void *t;memcpy(&t,lxwr_page+6,8);
    return lxwr_frame_state()==1 && lxwr_page[0]==0xff && lxwr_page[1]==0x25 && t==(void*)lxwr_frame_ctor && lxwr_frame_pins();
}
static void lxwr_frame_restore(void) {
    if(!lxwr_frame_installed) return;
    int state=lxwr_frame_state();
    if((state==1 && lxwr_frame_swap(lxwr_page,LX(LX_WR_CTOR_VA))) || state==0) lxwr_frame_installed=0;
}
/* Before the first admitted frame, on the MoveWorker; needs the world identities. */
static int wr_arm(int mode) {
    if(wr_installed) return stats.active && mode==wr_mode; /* Identical rerun after a loading-UI reload. */
    if((mode!=WR_SPLIT && mode!=WR_OBSERVE) || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed) {wr_stage="needs";return 0;}
    if(!wr_pins()) {wr_stage="pins";return 0;}
    void *entry;memcpy(&entry,LX(LX_WR_ENTRY_VA),8);
    if(entry!=(void*)LX(LX_WR_DISPATCH_VA)) {wr_stage="entry";return 0;}
    wr_calls=wr_failures=wr_rows=wr_constructed=wr_watches=0;wr_admitted=wr_keyed=wr_unmapped=wr_foreign=wr_outside=wr_full=wr_drew=wr_draws=wr_unknown=0;
    wr_attributed=0;wr_tick=UINT64_MAX;memset(wr_slots,0,sizeof(wr_slots));memset(wr_posters,0,sizeof(wr_posters));
    wr_ctor_installed=0;wr_ctor_override=NULL;wr_ctor_original_word=wr_ctor_patched_word=0; /* no constructor hook here */
    wr_framed=0;memset(wr_frames,0,sizeof(wr_frames));
    wr_mode=mode;InterlockedExchange(&wr_active,1);
    if(!lxwr_swap(LX(LX_WR_DISPATCH_VA),(void*)wr_dispatch)) {InterlockedExchange(&wr_active,0);wr_stage="entry";return 0;}
    wr_frame_ok=lxwr_frame_install(); /* the frame capture is diagnostic: its absence does not refuse the warp keying */
    wr_installed=1;wr_restored=0;wr_stage="armed";return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 143. */
static int wr_check(void) {
    if(InterlockedCompareExchange64(&wr_failures,0,0)) {wr_stage="exception";return 0;}
    const char *why=!wr_owned()?"entry_changed":!wr_pins()?"block_changed":!lxwr_frame_owned()?"frame_changed":NULL;
    if(why) {wr_stage=why;InterlockedIncrement64(&wr_failures);return 0;}
    return 1;
}
static void wr_restore(void) {
    InterlockedExchange(&wr_active,0);
    lxwr_frame_restore();wr_frame_ok=0;
    if(!wr_installed) return;
    if(!lxwr_swap((void*)wr_dispatch,LX(LX_WR_DISPATCH_VA))) return; /* a foreign value stays */
    wr_installed=0;wr_restored=1;
}
static int lua_warp_rng(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==5 && !memcmp(mode,"split",5)?WR_SPLIT:mode && length==7 && !memcmp(mode,"observe",7)?WR_OBSERVE:WR_OFF;
    p_lua_pushnumber(L,m!=WR_OFF && wr_arm(m));return 1;
}
static int lua_warp_rng_status(lua_State *L) {
    char out[640];int n=accept_ui()?wr_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_warp_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[1280];int n=0; /* a watch row with its frame: about 840 characters */
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=wr_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}

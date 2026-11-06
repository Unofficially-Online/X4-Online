/* The parallel pathing pass's tempzone requests granted in a portable order on the Linux X4 9.00 executable
 * (native_gate_tempzone_order = 'order' or true): the Linux twin of lockstep_tempzone_order.h, whose park, candidate
 * notes, grant, unpark, rows and status are shared. Included by gate_linux.c only, after gate_path.h and
 * gate_travel_snapshot.h. The mechanism and the Windows evidence are in that header.
 *
 * lx9/lx11b (Linux, 2 clients, the defaults' determinism set without this key): from 1440 client 2 alone differs, three
 * Xenon fighters in two sectors (06dbd2 in cluster_21; 07eb70/07eb7c, wingmen of 07eb5f, in cluster_414), motion only.
 * Between 1410 and 1440 the three flew (about 3 km/s, travel) into new zones while 07eb5f stayed in its own; topology
 * and timing agree (the zones are made off the MoveWorker, parent 0 on both), and each zone-relative position is one
 * step apart (client 2's read bank is client 1's write bank: 49.5 m at 2967 m/s, 54.6 m at 3247 m/s, 1/60 s), the
 * full-G1o class: the zone's origin is another ship's position. The census every 3 ticks (lx10) hides it, a launch order
 * of 2,1 (lx11b) keeps it on client 2: a race of the pass's jobs for the one process-wide slot, whose outcome follows
 * each client's timing.
 *
 * Linux sites. GCC inlined the zone lookup 0x785b90 into the query, so FUN_140785db0 is 0x12981b0 (Itanium: rdi the
 * result's reference, rsi the sector, rdx the position, ecx create; the sector's zones in the thread's view,
 * +0xf0 + index*0x80). It has five callers: Agent_UpdatePathing 0x1444c70 (create=1 at 0x14490be, create=0 at
 * 0x1449609), the zone switch 0x142efa0 (create=1 0x142fa2e, create=0 0x142fb34) and 0x12eb620 (0x12ec389), which the
 * region manager's Run 0x12ef310 reaches (Windows 0xee7290). The slot: flag 0x3eb9730 (dword), zone 0x3eb96f0, the
 * XLib::Mutex 0x3eb9700. Each request block tests the flag, locks, tests it again and asks with create=1; a new zone
 * (not active, 0x1cb3f40) is stored with a reference (0x1fa4030: +0x44 1 -> 2, +0x40 + 1, Windows' layout) and the flag
 * set; with the flag set it asks with create=0. RunPass 0x13e35e0 tests the flag at 0x13e3d78 and, at
 * 0x13e4143..0x13e41a9, clears the slot (0x1dc8de0, Windows 0x96940) once its zone is active in the reading view: the
 * zone's +0x1e6 bit 1 clear and its view word +0x11c + index*0x80 with 0x10000 set and 0x40000 clear, the index
 * 0x12021e0's (the published 0x3e7742c unless the thread's view flag fs:-0x2620 is set): Windows' fields, so the
 * shared tz_active_view reads them with the published index.
 *
 * Hooks: the three calls whose rel32 lies in one aligned 16 bytes (both create=0 requests and the region caller) go
 * through a jmp stub on a private page near the image to lxtz_find, CAS of the 16 bytes, so the shared code sees each
 * call with its return address. The two create=1 calls' rel32 cross a 16-byte boundary and are not patched: with the
 * slot parked no job reaches them, and a job that did would leave its zone in the parked slot, which the unpark finds
 * (slot_changed, stop reason 144). So only 'order' is ported ('observe' logs the create=1 calls); creates and
 * unblocked stay 0 here. The grant runs on the MoveWorker as on Windows: TLS game time (fs:-0x2728) 0, as on a job
 * (the job thunk 0x13c45c0 and the batch job 0x13e32f0 never sync it), and the keyed word (fs:-0x2720), both restored.
 * Pins (lx_sites tempzone_*): the query, the active test, the view index, the reset, the reference copy, the request
 * blocks, the slot stores, RunPass's test and clear and the region call, with our displacements read as the originals;
 * checked every gate tick (stop reason 144), restored by CAS on stop. */
#define TZ_OWN_PLATFORM
#define TZ_FLAG (LX_TZ_FLAG_VA-LX_IMAGE_BASE)
#define TZ_ZONE (LX_TZ_ZONE_VA-LX_IMAGE_BASE)
#define TZ_LOCK (LX_TZ_LOCK_VA-LX_IMAGE_BASE)
#define TZ_VIEW (LX_TZ_VIEW_VA-LX_IMAGE_BASE)
#define TZ_PATH_CREATE (LX_TZ_PATH_CREATE_RET_VA-LX_IMAGE_BASE)
#define TZ_PATH_FIND (LX_TZ_PATH_FIND_CALL_VA+5u-LX_IMAGE_BASE)
#define TZ_SWITCH_CREATE (LX_TZ_SWITCH_CREATE_RET_VA-LX_IMAGE_BASE)
#define TZ_SWITCH_FIND (LX_TZ_SWITCH_FIND_CALL_VA+5u-LX_IMAGE_BASE)
/* gate_path.h's path_pass_open, as Windows' path_thread_class reads it (warp and tempzone attribution only). */
enum {PATH_THREAD_NONE,PATH_THREAD_MOVEWORKER,PATH_THREAD_MAIN,PATH_THREAD_PASS,PATH_THREAD_OTHER};
static unsigned path_thread_class(void) {
    DWORD id=GetCurrentThreadId();
    if(id==ui_thread) return PATH_THREAD_MOVEWORKER;
    if(id==main_thread) return PATH_THREAD_MAIN;
    return InterlockedCompareExchange(&path_pass_open,0,0)?PATH_THREAD_PASS:PATH_THREAD_OTHER;
}
static const char *path_thread_name(unsigned c) {
    static const char *const names[]={"none","moveworker","main","pass","other"};
    return c<sizeof(names)/sizeof(*names)?names[c]:"?";
}
/* The engine's XLib::Mutex Lock/Unlock for the slot; a test sets stand-ins. */
static void (*lxtz_lock)(void *),(*lxtz_unlock)(void *);
static void lxtz_mutex(void *m,int lock) {
    void (*f)(void *)=lock?(lxtz_lock?lxtz_lock:(void(*)(void*))(void*)LX(LX_MUTEX_LOCK_VA)):(lxtz_unlock?lxtz_unlock:(void(*)(void*))(void*)LX(LX_MUTEX_UNLOCK_VA));
    f(m);
}
#ifndef InterlockedIncrement /* tz_addref's +0x40 (gate_actor_all.h has the same) */
#define InterlockedIncrement(p) __atomic_add_fetch((p),1,__ATOMIC_SEQ_CST)
#endif
#define CRITICAL_SECTION void
#define EnterCriticalSection(mutex) lxtz_mutex((mutex),1)
#define LeaveCriticalSection(mutex) lxtz_mutex((mutex),0)
#define GetLastError() ((DWORD)errno)
#define SetLastError(e) (errno=(int)(e))
#define __try if(1)
#define __finally
#include "lockstep_tempzone_order.h"
#undef CRITICAL_SECTION
#undef EnterCriticalSection
#undef LeaveCriticalSection
#undef GetLastError
#undef SetLastError
#undef __try
#undef __finally
/* ---- the native calls ---- */
typedef int64_t *(*lxtz_find_fn)(int64_t *,void *,const void *,unsigned);
static lxtz_find_fn lxtz_find_target; /* tests only; NULL calls 0x12981b0 */
/* The query hands the position to the zone builder 0x1143620, which loads it with movaps (0x11436f5): it must be 16-byte
 * aligned, as the request blocks' stack vectors are. A grant passes a candidate's copy (tz_candidate_t.position, 8 mod
 * 16 at odd indices; lx12a: the MoveWorker's #GP at 0x11436f5 at tick 1810 on the client whose winning request was
 * noted second), so every call goes through an aligned copy of the 16 bytes. */
static int64_t *lxtz_native(void *sector,int64_t *out,const void *position,unsigned char create) {
    float aligned[4] __attribute__((aligned(16)));memcpy(aligned,position,sizeof(aligned));
    return (lxtz_find_target?lxtz_find_target:(lxtz_find_fn)(void*)LX(LX_TZ_FIND_VA))(out,sector,aligned,create);
}
static tz_find_fn tz_original(void) {return tz_original_override?tz_original_override:lxtz_native;}
static void tz_release_ref(int64_t *ref) {
    if(tz_release_override) tz_release_override(ref);else ((tz_release_fn)(void*)LX(LX_TZ_RESET_VA))(ref);
}
static uint32_t tz_site(uintptr_t ret) {
    return ret>=(uintptr_t)image && ret<(uintptr_t)LX(LXW_IMAGE_END)?(uint32_t)(ret-(uintptr_t)image):0xffffffffu;
}
/* The job context for the grant: TLS game time 0, the keyed word; leave restores both. tls is the thread pointer. */
static void tz_context_enter(tz_context_t *s,unsigned char *tls,uint64_t seed) {
    const uint64_t zero=0;s->tls=tls;s->seed=s->exit=seed;s->time=s->word=0;
    double *time=tls?gate_tls_time(tls):NULL;uint64_t *word=tls?gate_tls_rng(tls):NULL;
    if(!time || !word) {s->tls=NULL;return;}
    memcpy(&s->time,time,8);memcpy(&s->word,word,8);memcpy(time,&zero,8);memcpy(word,&seed,8);
}
static void tz_context_leave(tz_context_t *s) {
    if(!s->tls) return;
    double *time=gate_tls_time(s->tls);uint64_t *word=gate_tls_rng(s->tls);
    memcpy(&s->exit,word,8);memcpy(time,&s->time,8);memcpy(word,&s->word,8);
}
/* Entered by jmp from the stub: the return address read is the native caller's. Itanium order. */
static __attribute__((noinline)) int64_t *lxtz_find(int64_t *out,void *sector,const void *position,unsigned create) {
    return tz_find_run(tz_original(),(uintptr_t)__builtin_return_address(0),sector,out,position,(unsigned char)create);
}
/* ---- the three redirected calls ---- */
#define LXTZ_SITES 3u
typedef struct {const char *name;uint32_t call_va,block_va;} lxtz_site_t;
static const lxtz_site_t lxtz_sites[LXTZ_SITES]={
    {"path_find",LX_TZ_PATH_FIND_CALL_VA,LX_TZ_PATH_FIND_CALL_VA&~15u},
    {"switch_find",LX_TZ_SWITCH_FIND_CALL_VA,LX_TZ_SWITCH_FIND_CALL_VA&~15u},
    {"other",LX_TZ_OTHER_CALL_VA,LX_TZ_OTHER_CALL_VA&~15u}};
static unsigned char *lxtz_page;
static unsigned char lxtz_site_done[LXTZ_SITES];
static int lxtz_patched;
/* The aligned 16 bytes holding site i's rel32, with the rel32 leading to target. */
static int lxtz_block(unsigned i,const void *target,unsigned __int128 *out) {
    const lxtz_site_t *s=&lxtz_sites[i];unsigned at=s->call_va+1-s->block_va;
    if(at>12u) return 0;
    unsigned char b[16];memcpy(b,LX(s->block_va),16);
    int32_t d=(int32_t)((intptr_t)target-(intptr_t)(LX(s->call_va)+5));memcpy(b+at,&d,4);memcpy(out,b,16);
    return 1;
}
/* 0 original, 1 ours, -1 anything else. */
static int lxtz_site_state(unsigned i) {
    const lxtz_site_t *s=&lxtz_sites[i];unsigned char op;int32_t d;
    if(!gate_read_local(LX(s->call_va),&op,1) || op!=0xe8 || !gate_read_local(LX(s->call_va)+1,&d,4)) return -1;
    unsigned char *to=LX(s->call_va)+5+d;
    if(to==LX(LX_TZ_FIND_VA)) return 0;
    if(lxtz_page && to==lxtz_page) return 1;
    return -1;
}
static int lxtz_swap(unsigned i,const void *from,const void *to) {
    unsigned __int128 expected,desired;
    return lxtz_block(i,from,&expected) && lxtz_block(i,to,&desired) && gate_code_swap128(LX(lxtz_sites[i].block_va),expected,desired);
}
/* jmp [rip+0]; abs64 to lxtz_find, one stub for the three sites. */
static void lxtz_write_stub(unsigned char *p) {void *t=(void*)lxtz_find;p[0]=0xff;p[1]=0x25;memset(p+2,0,4);memcpy(p+6,&t,8);}
static int lxtz_stub_ok(void) {
    if(!lxtz_page) return 0;
    void *t;memcpy(&t,lxtz_page+6,8);return lxtz_page[0]==0xff && lxtz_page[1]==0x25 && t==(void*)lxtz_find;
}
static const lx_site *lxtz_site_named(const char *name) {
    for(unsigned i=0;i<gate_site_count;i++) if(!strcmp(gate_sites[i].name,name)) return &gate_sites[i];
    return NULL;
}
/* The pins, each with a redirected call's displacement read as the original's. */
static int lxtz_pins(void) {
    static const char *const names[]={"tempzone_find","tempzone_active","tempzone_view","tempzone_reset","tempzone_addref",
        "tempzone_path_create","tempzone_path_store","tempzone_path_find","tempzone_switch_create","tempzone_switch_find",
        "tempzone_switch_store","tempzone_runpass_test","tempzone_runpass_clear","tempzone_region_call"};
    for(unsigned k=0;k<sizeof(names)/sizeof(*names);k++) {
        const lx_site *s=lxtz_site_named(names[k]);unsigned char copy[0x400];
        if(!s || s->length>sizeof(copy) || !gate_read_local(LX(s->va),copy,s->length)) return 0;
        for(unsigned i=0;i<LXTZ_SITES;i++) {
            uint32_t c=lxtz_sites[i].call_va;if(c+5<=s->va || c>=s->va+s->length) continue;
            if(c<s->va || c+5>s->va+s->length || lxtz_site_state(i)<0) return 0;
            int32_t d=(int32_t)((intptr_t)LX(LX_TZ_FIND_VA)-(intptr_t)(LX(c)+5));memcpy(copy+(c-s->va)+1,&d,4);
        }
        if(!hash_ok(copy,s->length,s->sha256)) return 0;
    }
    return 1;
}
static int tz_owned(void) {
    if(!lxtz_patched) return 1;
    for(unsigned i=0;i<LXTZ_SITES;i++) if(lxtz_site_state(i)!=1) return 0;
    return lxtz_stub_ok();
}
/* Before the first admitted frame, on the MoveWorker; needs the pathing join (options&1) and the world identities. */
static int tz_arm(int mode,unsigned options) {
    if(tz_installed) return stats.active && mode==tz_mode; /* Identical rerun after a loading-UI reload. */
    if(mode==TZ_OBSERVE) {tz_stage="observe_unported";return 0;}
    if(mode!=TZ_ORDER || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread || !(options&1)) return 0;
    if(!world_identity_installed) {tz_stage="needs";return 0;}
    if(!lxtz_pins()) {tz_stage="pins";return 0;}
    for(unsigned i=0;i<LXTZ_SITES;i++) if(lxtz_site_state(i)!=0) {tz_stage="site";return 0;}
    if(!lxtz_page) lxtz_page=gate_near_page(LX(LX_TZ_PATH_FIND_CALL_VA));
    if(!lxtz_page) {tz_stage="page";return 0;}
    for(unsigned i=0;i<LXTZ_SITES;i++) {
        intptr_t d=(intptr_t)lxtz_page-(intptr_t)(LX(lxtz_sites[i].call_va)+5);
        if(d<INT32_MIN || d>INT32_MAX) {tz_stage="page";return 0;}
    }
    if(mprotect(lxtz_page,GATE_PAGE,PROT_READ|PROT_WRITE)) {tz_stage="page";return 0;}
    memset(lxtz_page,0xcc,GATE_PAGE);lxtz_write_stub(lxtz_page);
    if(mprotect(lxtz_page,GATE_PAGE,PROT_READ|PROT_EXEC)) {tz_stage="page";return 0;}
    tz_noted=tz_rows=tz_failures=tz_finds=tz_misses=tz_creates=tz_unblocked=tz_other_creates=tz_other_made=tz_other_in_pass=tz_outside=0;
    tz_passes=tz_blocks=tz_opens=tz_cleared=tz_granted=tz_stored=tz_found=tz_waits=tz_overflow=tz_requests=tz_max=tz_drew=tz_unmapped=tz_gone=0;
    tz_blocked=tz_open=0;
    tz_mode=mode;InterlockedExchange(&tz_active,1);
    for(unsigned i=0;i<LXTZ_SITES;i++) {
        if(!lxtz_swap(i,LX(LX_TZ_FIND_VA),lxtz_page)) {
            for(unsigned j=0;j<i;j++) if(lxtz_site_done[j] && lxtz_swap(j,lxtz_page,LX(LX_TZ_FIND_VA))) lxtz_site_done[j]=0;
            InterlockedExchange(&tz_active,0);tz_stage="entry";return 0;
        }
        lxtz_site_done[i]=1;
    }
    lxtz_patched=1;tz_installed=1;tz_restored=0;tz_stage="armed";return 1;
}
static int tz_after_join(void) {return tz_after_join_with(gate_tls_self());}
/* Main-thread tick check; any failure stops the gate with reason 144. */
static int tz_check(void) {
    if(InterlockedCompareExchange64(&tz_failures,0,0)) return 0;
    const char *why=!tz_owned()?"entry_changed":!lxtz_pins()?"block_changed":NULL;
    if(why) {tz_stage=why;InterlockedIncrement64(&tz_failures);return 0;}
    return 1;
}
static void tz_restore(void) {
    InterlockedExchange(&tz_active,0);
    tz_unpark(); /* a slot changed meanwhile stays as it is */
    if(!tz_installed) return;
    if(lxtz_patched) {
        int all=1;
        for(unsigned i=0;i<LXTZ_SITES;i++) if(lxtz_site_done[i]) {
            int state=lxtz_site_state(i);
            if((state==1 && lxtz_swap(i,lxtz_page,LX(LX_TZ_FIND_VA))) || state==0) lxtz_site_done[i]=0;else all=0;
        }
        if(!all) return; /* a foreign value stays */
        lxtz_patched=0;
    }
    tz_installed=0;tz_restored=1;
}
/* gate_path.h's release site: the slot parked for the pass (1), and after the join the slot back and at most one grant. */
static int lxtz_before_release(void) {
    if(stats.active && !tz_before_release()) {InterlockedIncrement64(&tz_failures);return 0;}
    return 1;
}
static int lxtz_after_join(void) {
    if(stats.active && !tz_after_join()) {InterlockedIncrement64(&tz_failures);return 0;}
    return 1;
}
static int lua_tempzone_order(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==5 && !memcmp(mode,"order",5)?TZ_ORDER:mode && length==7 && !memcmp(mode,"observe",7)?TZ_OBSERVE:TZ_OFF;
    p_lua_pushnumber(L,m!=TZ_OFF && tz_arm(m,path_options));return 1;
}
static int lua_tempzone_order_status(lua_State *L) {
    char out[896];int n=accept_ui()?tz_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_tempzone_order_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[384];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=tz_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}

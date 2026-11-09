/* native_gate_impulse_diag (diagnostic, default off, outside the digest): the impulses X4 gives a fixture ship's
 * physics bodies inside a tick window, with the body as Jolt sees it before and its velocities after, as IMPULSE rows
 * in the Linux gate's text (linux/gate_impulse.h), so the two compare row for row.
 *
 * Why: at the battle's tick 28 (W36j against lx50a/lx51a) a fixture ship's body0 gets two impulses at a point before
 * the Jolt step, each followed by the record copy-back into the navigation controller (Windows 0xbf27d, Linux
 * 0xee81f3). The first leaves Windows and Linux bit-identical; the second does not (actor 2 angular y, actor 4 linear
 * z). Linux's rows reproduce its own result from their inputs; these show whether Windows gets another impulse or
 * point, or applies the same ones differently.
 *
 * Site: Jolt's Body::AddImpulse(Vec3Arg impulse, RVec3Arg point), inlined at 0x1405910 (rcx Body*, rdx &impulse,
 * r8 &point; Linux BodyInterface::AddImpulse 0x1a7fc90), pinned over 32 bytes. Its first five bytes become a jmp rel32
 * to a stub on a page near the image (lockstep_safe_pos_order.h's scheme, as lockstep_nav_refresh_watch.h), with the
 * other threads frozen and none inside those bytes; the trampoline runs the two relocated instructions (mov rax,rsp;
 * sub rsp,0xd8) and jumps to +10, and its unwind entry (the allocation) is registered and never revoked. The view
 * keeps the pin. A call on the UI thread inside the window, on a body whose record (Body +0x50) is one of a fixture
 * ship's two records (+0x260 body0, +0x268 body1), records the impulse, the point, the body's pose, its
 * MotionProperties' first 0x60 bytes and its velocities after; every other call just runs the original. Checked
 * every gate tick (stop reason 207), restored by compare-exchange at stop; a foreign value is never overwritten. */
#define WAI_STOP_REASON 207u
#define WAI_ENTRY 0x1405910u
#define WAI_RESUME 0x140591au
#define WAI_SIZE 32u
#define WAI_SHA "e48d48afa25d92771f799f5ba42e86cec20a55502ab80549276cc879b645a946"
#define WAI_PAGE_ANCHOR 0x1400000u
#define WAI_STUB 0x00u
#define WAI_TRAMP 0x20u
#define WAI_TRAMP_SIZE 24u    /* the 10 relocated bytes and a 14-byte jump */
#define WAI_UNWIND 0x40u      /* UNWIND_INFO of the trampoline */
#define WAI_CODE 0x60u
#define WAI_MAX 8u
#define WAI_ACTORS 12u
static const unsigned char wai_original[5]={0x48,0x8b,0xc4,0x48,0x81};
static const unsigned char wai_relocated[10]={0x48,0x8b,0xc4,0x48,0x81,0xec,0xd8,0x00,0x00,0x00}; /* mov rax,rsp; sub rsp,0xd8 */
/* version 1, prolog 10 bytes, 2 codes: at 10 UWOP_ALLOC_LARGE (op 1, info 0) of 27 slots (0xd8 / 8) */
static const unsigned char wai_unwind_info[8]={1,10,2,0,10,0x01,27,0};
typedef void (*wai_fn)(void *body,const void *impulse,const void *point);
typedef struct {uint64_t seq,source,caller;unsigned which;uint32_t impulse[4],point[4],pose[8],motion[24],after[8];} wai_record;
typedef struct {uint64_t tick;unsigned count;wai_record records[WAI_MAX];} wai_trace;
static wai_trace wai_traces[WAI_ACTORS];
static int wai_installed,wai_restored,wai_unwind_added;
static unsigned char *wai_page;
static volatile LONG wai_any;
static uint64_t wai_from,wai_to;
static volatile LONG64 wai_calls,wai_recorded,wai_failures,wai_seq;
static const char *wai_stage="idle";
static wai_fn wai_original_override; /* tests */
static RUNTIME_FUNCTION wai_unwind_function;
static int wai_record_of(void *body,unsigned *actor,unsigned *which) {
    void *record=NULL;
    if(!body || !sample_read((unsigned char*)body+0x50,&record,8) || !record) return 0;
    for(unsigned a=1;a<=WAI_ACTORS;a++) {
        unsigned char *object=actor_components[a-1];uint64_t id=0;void *records[2]={0};
        if(actor_role(a)!=ACTOR_FIXTURE_SHIP || !object || !sample_read(object+8,&id,8) || id!=actor_ids[a-1] ||
           !sample_read(object+0x260,records,16)) continue;
        for(unsigned w=0;w<2;w++) if(records[w]==record) {*actor=a;*which=w;return 1;}
    }
    return 0;
}
static __attribute__((noinline)) void wai_hook(void *body,const void *impulse,const void *point) {
    wai_fn original=wai_original_override?wai_original_override:(wai_fn)(void*)(wai_page+WAI_TRAMP);
    InterlockedIncrement64(&wai_calls);
    unsigned actor=0,which=0;unsigned char *motion=NULL;wai_record *r=NULL;
    const uint64_t tick=stats.issued;
    if(stats.active && wai_to && tick>=wai_from && tick<=wai_to && GetCurrentThreadId()==ui_thread && wai_record_of(body,&actor,&which)) {
        wai_trace *trace=&wai_traces[actor-1];
        if(trace->tick!=tick) {trace->tick=tick;trace->count=0;}
        if(trace->count<WAI_MAX && sample_read((unsigned char*)body+0x48,&motion,8) && motion) {
            r=&trace->records[trace->count++];memset(r,0,sizeof(*r));
            r->seq=(uint64_t)InterlockedIncrement64(&wai_seq);r->which=which;
            const unsigned char *ret=__builtin_return_address(0);r->source=(uint64_t)(uintptr_t)(ret-image);
            void *frames[6]={0};const USHORT got=RtlCaptureStackBackTrace(0,6,frames,NULL); /* the caller's caller, when the walk finds it */
            for(USHORT k=0;k+1<got;k++) if(frames[k]==(void*)ret) {r->caller=(uint64_t)(uintptr_t)((unsigned char*)frames[k+1]-image);break;}
            if(!sample_read(impulse,r->impulse,16) || !sample_read(point,r->point,16) || !sample_read(body,r->pose,32) ||
               !sample_read(motion,r->motion,0x60)) InterlockedIncrement64(&wai_failures);
        } else InterlockedIncrement64(&wai_failures);
    }
    original(body,impulse,point);
    if(r) {if(!sample_read(motion,r->after,32)) InterlockedIncrement64(&wai_failures);else InterlockedIncrement64(&wai_recorded);}
}
/* ---- the page and the entry (lockstep_safe_pos_order.h's scheme, as lockstep_nav_refresh_watch.h) ---- */
static void wai_code(unsigned char out[WAI_CODE]) {
    memset(out,0xcc,WAI_CODE);
    spo_jump_to(out+WAI_STUB,(const void*)wai_hook);
    memcpy(out+WAI_TRAMP,wai_relocated,10);spo_jump_to(out+WAI_TRAMP+10,(const void*)(image+WAI_RESUME));
    memcpy(out+WAI_UNWIND,wai_unwind_info,sizeof(wai_unwind_info));
}
static int wai_reach(const unsigned char *page) {
    const intptr_t d=(intptr_t)(page+WAI_STUB)-(intptr_t)(image+WAI_ENTRY+5);
    return d>INT32_MIN+0x2000 && d<INT32_MAX-0x2000;
}
static unsigned char *wai_alloc_page(void) {
    const uintptr_t at=(uintptr_t)(image+WAI_PAGE_ANCHOR)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            const uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            if(wai_reach(p)) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int wai_write_page(void) {
    DWORD old,ignored;
    if(!VirtualProtect(wai_page,0x1000,PAGE_READWRITE,&old)) return 0;
    memset(wai_page,0xcc,0x1000);wai_code(wai_page);
    if(!VirtualProtect(wai_page,0x1000,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),wai_page,0x1000);return 1;
}
static int wai_page_ok(void) {unsigned char want[WAI_CODE];if(!wai_page) return 0;wai_code(want);return !memcmp(wai_page,want,WAI_CODE);}
/* The trampoline's unwind entry: registered once per page and never revoked (the page is never freed once armed). */
static int wai_unwind_register(void) {
    if(wai_unwind_added) return 1;
    wai_unwind_function.BeginAddress=WAI_TRAMP;wai_unwind_function.EndAddress=WAI_TRAMP+WAI_TRAMP_SIZE;wai_unwind_function.UnwindData=WAI_UNWIND;
    if(!RtlAddFunctionTable(&wai_unwind_function,1,(DWORD64)(uintptr_t)wai_page)) return 0;
    wai_unwind_added=1;
    DWORD64 base=0;return RtlLookupFunctionEntry((DWORD64)(uintptr_t)(wai_page+WAI_TRAMP+12),&base,NULL)==&wai_unwind_function && base==(DWORD64)(uintptr_t)wai_page;
}
static void wai_patch_bytes(unsigned char out[5]) {
    const int32_t rel=(int32_t)((intptr_t)(wai_page+WAI_STUB)-(intptr_t)(image+WAI_ENTRY+5));out[0]=0xe9;memcpy(out+1,&rel,4);
}
static int wai_state(void) {
    unsigned char now[5],ours[5];memcpy(now,(const void*)(image+WAI_ENTRY),5);
    if(!memcmp(now,wai_original,5)) return 0;
    if(wai_page && wai_reach(wai_page)) {wai_patch_bytes(ours);if(!memcmp(now,ours,5)) return 1;}
    return -1;
}
static int wai_pins(void) {
    static unsigned char body[WAI_SIZE];
    return read_local(image+WAI_ENTRY,body,WAI_SIZE) && hash_ok(body,WAI_SIZE,WAI_SHA) && !memcmp(body,wai_relocated,10);
}
static int wai_overlaps(const void *source,size_t bytes) {
    if(!*(volatile LONG*)&wai_any || !image || !source || !bytes) return 0;
    const uintptr_t a=(uintptr_t)source,lo=(uintptr_t)(image+WAI_ENTRY),hi=lo+5;
    return a<hi && a+bytes>lo;
}
static void wai_unpatch_copy(const void *source,void *copy,size_t bytes) {
    if(!wai_overlaps(source,bytes)) return;
    const uintptr_t a=(uintptr_t)source,lo=(uintptr_t)(image+WAI_ENTRY);
    for(unsigned i=0;i<5;i++) if(lo+i>=a && lo+i<a+bytes) ((unsigned char*)copy)[lo+i-a]=wai_original[i];
}
static int wai_window_clear(void) {
    const uintptr_t entry=(uintptr_t)(image+WAI_ENTRY);
    for(unsigned i=0;i<pmath_frozen_count;i++) {
        CONTEXT c;memset(&c,0,sizeof(c));c.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(pmath_frozen[i],&c)) return 0;
        if(c.Rip>entry && c.Rip<entry+5) return 0;
    }
    return 1;
}
static int wai_switch(int install) {
    unsigned char ours[5];wai_patch_bytes(ours);
    if(install) InterlockedExchange(&wai_any,1);
    for(unsigned attempt=0;attempt<200;attempt++) {
        if(!pmath_freeze()) break;
        if(!wai_window_clear()) {pmath_thaw();Sleep(1);continue;}
        const int ok=view_cas_bytes(WAI_ENTRY,install?wai_original:ours,install?ours:wai_original,5);
        pmath_thaw();
        if(ok!=install) InterlockedExchange(&wai_any,0);
        return ok;
    }
    if(install) InterlockedExchange(&wai_any,0);
    return 0;
}
/* gate.impulse_diag(from,to): UI thread, before the first admitted frame. Armed again with the same window (the
 * probe's arming block runs again on a loading-UI reload) it answers 1 and changes nothing; another window is refused. */
static int wai_arm(double from,double to) {
    if(!isfinite(from) || !isfinite(to) || floor(from)!=from || floor(to)!=to || from<1 || to<from || to>1e9) {if(!wai_installed) wai_stage="window";return 0;}
    if(wai_installed) return stats.active && (uint64_t)from==wai_from && (uint64_t)to==wai_to;
    if(!stats.active || stats.issued || (boot_mode && boot_phase) || GetCurrentThreadId()!=ui_thread) {wai_stage="state";return 0;}
    if(!wai_pins()) {wai_stage="pins";return 0;}
    if(wai_state()!=0) {wai_stage="entry";return 0;}
    if(!wai_page) wai_page=wai_alloc_page();
    if(!wai_page || !wai_write_page() || !wai_page_ok()) {wai_stage="page";return 0;}
    if(!wai_unwind_register()) {wai_stage="unwind";return 0;}
    wai_from=(uint64_t)from;wai_to=(uint64_t)to;memset(wai_traces,0,sizeof(wai_traces));
    wai_calls=wai_recorded=wai_failures=0;wai_restored=0;
    if(!wai_switch(1)) {wai_stage="busy";return 0;}
    wai_installed=1;wai_stage="armed";return 1;
}
static int wai_owned(void) {return !wai_installed || (wai_state()==1 && wai_page_ok());}
static int wai_check(void) {
    if(InterlockedCompareExchange64(&wai_failures,0,0)) {wai_stage="failures";return 0;}
    if(!wai_owned()) {wai_stage="site_changed";InterlockedIncrement64(&wai_failures);return 0;}
    return 1;
}
static void wai_restore(void) {
    if(!wai_installed) return;
    if(wai_state()!=1) {wai_stage="restore_foreign";return;}
    if(wai_switch(0)) {wai_installed=0;wai_restored=1;wai_stage="restored";}
    else wai_stage="restore_busy";
}
static int wai_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity,"tick=%llu installed=%d restored=%d calls=%lld recorded=%lld failures=%lld state=%d stage=%s window=%llu:%llu",
        (unsigned long long)stats.issued,wai_installed,wai_restored,(long long)wai_calls,(long long)wai_recorded,(long long)wai_failures,
        wai_state(),wai_stage,(unsigned long long)wai_from,(unsigned long long)wai_to);
    return n>0 && (size_t)n<capacity?n:0;
}
/* The Linux gate's text, then caller= (the caller's caller, Windows only, 0 when the walk did not find it). */
static int wai_text(unsigned actor,unsigned ordinal,char *out,size_t cap) {
    if(actor<1 || actor>WAI_ACTORS || cap<900) return 0;
    const wai_trace *trace=&wai_traces[actor-1];const unsigned count=trace->tick==stats.issued?trace->count:0;
    if(ordinal<1 || ordinal>count) return 0;
    const wai_record *r=&trace->records[ordinal-1];
    int n=snprintf(out,cap,"which=%u tick=%llu seq=%llu source=%llx impulse=",r->which,(unsigned long long)trace->tick,
        (unsigned long long)r->seq,(unsigned long long)r->source);
    n+=hex_words(out+n,cap-(size_t)n,r->impulse,4);
    n+=snprintf(out+n,cap-(size_t)n," point=");n+=hex_words(out+n,cap-(size_t)n,r->point,4);
    n+=snprintf(out+n,cap-(size_t)n," pose=");n+=hex_words(out+n,cap-(size_t)n,r->pose,8);
    n+=snprintf(out+n,cap-(size_t)n," motion=");n+=hex_words(out+n,cap-(size_t)n,r->motion,24);
    n+=snprintf(out+n,cap-(size_t)n," after=");n+=hex_words(out+n,cap-(size_t)n,r->after,8);
    n+=snprintf(out+n,cap-(size_t)n," caller=%llx",(unsigned long long)r->caller);
    return n>0 && (size_t)n<cap?n:0;
}

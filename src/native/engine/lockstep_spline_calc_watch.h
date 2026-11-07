/* SPLINE_CALC (diagnostic, DEFAULT OFF: native_gate_spline_calc_watch = {from,to}): when the spline route calculator runs
 * relative to the gated frames.
 *
 * J1 j1-353 .. j1-355: ShipLarge 0x6f1e7's one route request at 3722 carries the same request on both sides (the motion
 * watch's req=: the request transform, parameters and flags) and yields control points a few ulps apart on the joiner (pts=),
 * the J1 split at 3780. The calculator thread (0x1099650, one FIFO worker) runs 0xec0cb0(request): it resolves the request's
 * weak reference +0x18 (0xebdcc0) and looks up the component of the runtime ID at +0x28 in the component map (0xced70), so it
 * reads live world state whenever the worker reaches the job: after the request (a pathing update of one frame) and before the
 * update that consumes it (the next frame's, joined by native_gate_spline_join_all). Where in that interval it runs follows
 * thread scheduling: paced alike on the session's clients, not on a joiner that replays the journal in bursts.
 *
 * Entry hook (count and record only, the original runs unchanged): the first seven bytes, mov rax,rsp; mov [rax+8],rbx (no rip
 * operand), become jmp rel32 to a stub on our page; the trampoline replays them and jumps to 0xec0cb7. A call while
 * stats.issued lies in [from,to] writes a row: the tick and whether the main thread was inside the gated frame
 * (gate_step's original FrameTick) at the entry and after the original returned, the microseconds since that frame began, the
 * thread, the request's index (+0x70) and the component of its +0x28 runtime ID as a portable key. Rows go to the sink (the
 * crash notes) at each gate tick. Pins: the entry's 256 bytes through the view. Installed and restored with every other thread
 * suspended (portable math's freeze) and none in the 7-byte window; armed on the UI thread before the first admitted frame; any
 * change at a gate tick stops the gate with reason 199. */
#define SCW_STOP_REASON 199u
#define SCW_ENTRY 0xec0cb0u
#define SCW_RESUME 0xec0cb7u
#define SCW_SIZE 256u
#define SCW_SHA "814c2328b986198ce9699f0dba5ad127a2be2813789eb7cd1e0cbcfc86d5f8bc"
#define SCW_INDEX 0x70u
#define SCW_RUNTIME 0x28u
#define SCW_PAGE_ANCHOR 0xec0000u
#define SCW_STUB 0x00u
#define SCW_TRAMP 0x20u
#define SCW_CODE 0x40u
#define SCW_CAP ((size_t)8<<20)
static const unsigned char scw_original[7]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x08};
typedef uint64_t (*scw_fn)(void *,void *,void *,void *);
static int scw_installed,scw_restored;
static unsigned char *scw_page;
static volatile LONG scw_any;
static uint64_t scw_from,scw_to;
static void (*scw_sink)(const char *,int);
static SRWLOCK scw_lock=SRWLOCK_INIT;
static char *scw_buffer;static size_t scw_used,scw_capacity;
static volatile LONG64 scw_calls,scw_in_frame_calls,scw_rows,scw_dropped,scw_failures;
static const char *scw_stage="idle";
/* Set by gate_step around the gated frame's original FrameTick (lockstep_gate.c): inside it, and the frame's start (QPC). */
static volatile LONG scw_frame_open;
static volatile LONG64 scw_frame_qpc;
static LONG64 scw_qpc_hz;
/* SPLINE_PHASE (opt-in, native_gate_spline_phase; every client): the calculator runs at fixed points of the frame only. Its
 * entry waits (the hook, before the original) until the main thread opens the phase; path_release opens it twice a frame, before
 * it releases the pathing pass (the requests made since the last join) and after it joins it (those the pass's spline updates
 * made), and waits there until the calculator's queue is empty: 0x1099650 pops a request under the queue's lock 0x3d74910, runs
 * 0xec0cb0, publishes it (+0xa8, under 0x3d74938) and only then takes it off the count +0x3d74980. Nothing else moves the world
 * while the main thread waits there, so every client computes a route from the same state, and the consuming update (the next
 * pass, joined by native_gate_spline_join_all) finds it published. A held calculator past SCW_PHASE_WAIT_MS runs anyway and
 * counts a failure; a drain past SCW_DRAIN_MS stops the gate (199). Status phase=on:drains:jobs:held:timeouts:max_us. */
#define SCW_QUEUE_LOCK 0x3d74910u
#define SCW_QUEUE_COUNT 0x3d74980u
#define SCW_PHASE_WAIT_MS 10000u
#define SCW_DRAIN_MS 5000u
static int scw_phase_on;
static HANDLE scw_phase_event;
static DWORD scw_drain_ms=SCW_DRAIN_MS,scw_hold_ms=SCW_PHASE_WAIT_MS;   /* tests: shorter */
static volatile LONG64 scw_phase_drains,scw_phase_jobs,scw_phase_held,scw_phase_timeouts,scw_phase_max_us;
static CRITICAL_SECTION *scw_queue_lock;static const uint64_t *scw_queue_at;   /* set at the install (tests: their own) */
static int scw_queue_count(uint64_t *count) {
    if(!scw_queue_lock || !scw_queue_at) return 0;
    EnterCriticalSection(scw_queue_lock);
    const int ok=read_local(scw_queue_at,count,8);
    LeaveCriticalSection(scw_queue_lock);
    return ok;
}
/* The main thread at a phase point: let the calculator run until its queue is empty. 0: unreadable or timed out (stop). */
static int scw_phase_drain(void) {
    if(!scw_phase_on || !scw_phase_event) return 1;
    uint64_t count=0;
    if(!scw_queue_count(&count)) {scw_stage="phase_queue";InterlockedIncrement64(&scw_failures);return 0;}
    if(!count) return 1;
    LARGE_INTEGER t0,t1;QueryPerformanceCounter(&t0);
    const LONG64 before=InterlockedCompareExchange64(&scw_calls,0,0);
    InterlockedIncrement64(&scw_phase_drains);SetEvent(scw_phase_event);
    const DWORD start=GetTickCount();
    for(;;) {
        if(!scw_queue_count(&count)) {ResetEvent(scw_phase_event);scw_stage="phase_queue";InterlockedIncrement64(&scw_failures);return 0;}
        if(!count) break;
        if(GetTickCount()-start>scw_drain_ms) {ResetEvent(scw_phase_event);scw_stage="phase_drain_timeout";InterlockedIncrement64(&scw_failures);return 0;}
        SwitchToThread();
    }
    ResetEvent(scw_phase_event);
    QueryPerformanceCounter(&t1);
    InterlockedAdd64(&scw_phase_jobs,InterlockedCompareExchange64(&scw_calls,0,0)-before);
    const LONG64 us=scw_qpc_hz?(t1.QuadPart-t0.QuadPart)*1000000/scw_qpc_hz:0;
    if(us>scw_phase_max_us) scw_phase_max_us=us;
    return 1;
}
/* The calculator's entry while the phase is on: wait for it to open (bounded). */
static void scw_phase_hold(void) {
    if(!scw_phase_on || !scw_phase_event || WaitForSingleObject(scw_phase_event,0)==WAIT_OBJECT_0) return;
    InterlockedIncrement64(&scw_phase_held);
    if(WaitForSingleObject(scw_phase_event,scw_hold_ms)!=WAIT_OBJECT_0) {
        InterlockedIncrement64(&scw_phase_timeouts);InterlockedIncrement64(&scw_failures);scw_stage="phase_hold_timeout";
    }
}
static long long scw_us_since_frame(void) {
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    const LONG64 start=InterlockedCompareExchange64(&scw_frame_qpc,0,0);
    if(!scw_qpc_hz || !start) return -1;
    return (long long)((now.QuadPart-start)*1000000/scw_qpc_hz);
}
static void scw_append(const char *row,int n) {
    AcquireSRWLockExclusive(&scw_lock);
    if(scw_used+(size_t)n>scw_capacity && scw_capacity<SCW_CAP) {
        size_t want=scw_capacity?scw_capacity*2:(size_t)1<<20;while(want<scw_used+(size_t)n) want*=2;
        char *grown=(char*)realloc(scw_buffer,want);if(grown) {scw_buffer=grown;scw_capacity=want;}
    }
    if(scw_used+(size_t)n<=scw_capacity) {memcpy(scw_buffer+scw_used,row,(size_t)n);scw_used+=(size_t)n;InterlockedIncrement64(&scw_rows);}
    else InterlockedIncrement64(&scw_dropped);
    ReleaseSRWLockExclusive(&scw_lock);
}
static void scw_flush(void) {
    if(!scw_sink) return;
    AcquireSRWLockExclusive(&scw_lock);
    if(scw_used) {scw_sink(scw_buffer,(int)scw_used);scw_used=0;}
    ReleaseSRWLockExclusive(&scw_lock);
}
static int scw_row(char *out,size_t capacity,uint64_t tick_in,int open_in,long long us_in,uint64_t tick_out,int open_out,long long us_out,
                   unsigned long thread,uint32_t index,uint64_t key) {
    const int n=snprintf(out,capacity,"[X4LAN] NATIVE_GATE SPLINE_CALC tick=%llu frame=%d us=%lld tick_out=%llu frame_out=%d us_out=%lld thread=%lu index=%u key=%016llx\r\n",
        (unsigned long long)tick_in,open_in,us_in,(unsigned long long)tick_out,open_out,us_out,thread,index,(unsigned long long)key);
    return n>0 && (size_t)n<capacity?n:0;
}
static __attribute__((noinline)) uint64_t scw_hook(void *request,void *a2,void *a3,void *a4) {
    scw_phase_hold();   /* SPLINE_PHASE: only inside a phase */
    InterlockedIncrement64(&scw_calls);
    const uint64_t tick_in=stats.issued;const int open_in=InterlockedCompareExchange(&scw_frame_open,0,0)!=0;
    const long long us_in=scw_us_since_frame();
    if(open_in) InterlockedIncrement64(&scw_in_frame_calls);
    uint32_t index=0;uint64_t runtime=0;
    if(request) {memcpy(&index,(const unsigned char*)request+SCW_INDEX,4);memcpy(&runtime,(const unsigned char*)request+SCW_RUNTIME,8);}   /* the original reads them next */
    const uint64_t result=((scw_fn)(void*)(scw_page+SCW_TRAMP))(request,a2,a3,a4);
    const uint64_t tick_out=stats.issued;const int open_out=InterlockedCompareExchange(&scw_frame_open,0,0)!=0;
    const long long us_out=scw_us_since_frame();
    if(scw_to && tick_in>=scw_from && tick_in<=scw_to) {
        char row[320];const uint64_t key=runtime?world_identity_portable(runtime):0;
        const int n=scw_row(row,sizeof(row),tick_in,open_in,us_in,tick_out,open_out,us_out,(unsigned long)GetCurrentThreadId(),index,key);
        if(n) scw_append(row,n);else InterlockedIncrement64(&scw_dropped);
    }
    return result;
}
/* ---- the page and the entry (lockstep_safe_pos_order.h's scheme) ---- */
static void scw_code(unsigned char out[SCW_CODE]) {
    memset(out,0xcc,SCW_CODE);
    spo_jump_to(out+SCW_STUB,(const void*)scw_hook);
    memcpy(out+SCW_TRAMP,scw_original,7);spo_jump_to(out+SCW_TRAMP+7,(const void*)(image+SCW_RESUME));
}
static int scw_reach(const unsigned char *page) {
    const intptr_t d=(intptr_t)(page+SCW_STUB)-(intptr_t)(image+SCW_ENTRY+5);
    return d>INT32_MIN+0x2000 && d<INT32_MAX-0x2000;
}
static unsigned char *scw_alloc_page(void) {
    const uintptr_t at=(uintptr_t)(image+SCW_PAGE_ANCHOR)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            const uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            if(scw_reach(p)) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int scw_write_page(void) {
    DWORD old,ignored;
    if(!VirtualProtect(scw_page,0x1000,PAGE_READWRITE,&old)) return 0;
    memset(scw_page,0xcc,0x1000);scw_code(scw_page);
    if(!VirtualProtect(scw_page,0x1000,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),scw_page,0x1000);return 1;
}
static int scw_page_ok(void) {unsigned char want[SCW_CODE];if(!scw_page) return 0;scw_code(want);return !memcmp(scw_page,want,SCW_CODE);}
/* The entry's seven bytes while installed: jmp rel32 to the stub, two int3. */
static void scw_patch_bytes(unsigned char out[7]) {
    const int32_t rel=(int32_t)((intptr_t)(scw_page+SCW_STUB)-(intptr_t)(image+SCW_ENTRY+5));out[0]=0xe9;memcpy(out+1,&rel,4);out[5]=0xcc;out[6]=0xcc;
}
static int scw_state(void) {
    unsigned char now[7],ours[7];memcpy(now,(const void*)(image+SCW_ENTRY),7);
    if(!memcmp(now,scw_original,7)) return 0;
    if(scw_page && scw_reach(scw_page)) {scw_patch_bytes(ours);if(!memcmp(now,ours,7)) return 1;}
    return -1;
}
/* The calculator loop 0x1099650 as the phase reads it: the queue lock it pops under (0x1099722 lea rcx,[0x3d74910]), its call of
 * 0xec0cb0 (0x109976c), the publication (0x1099792 xchg [rbx+0xa8],eax) and only then the count's decrement (0x10997b3 sub
 * qword [0x3d74980],1). */
static int scw_loop_pins(void) {
    unsigned char a[7],b[5],c[6],e[8];int32_t d1,d2,d3;
    if(!read_local(image+0x1099722,a,7) || !read_local(image+0x109976c,b,5) || !read_local(image+0x1099792,c,6) || !read_local(image+0x10997b3,e,8)) return 0;
    memcpy(&d1,a+3,4);memcpy(&d2,b+1,4);memcpy(&d3,e+3,4);
    static const unsigned char lea[3]={0x48,0x8d,0x0d},xchg[6]={0x87,0x83,0xa8,0x00,0x00,0x00},sub[3]={0x48,0x83,0x2d};
    return !memcmp(a,lea,3) && 0x1099729u+(uint32_t)d1==SCW_QUEUE_LOCK && b[0]==0xe8 && 0x1099771u+(uint32_t)d2==SCW_ENTRY &&
           !memcmp(c,xchg,6) && !memcmp(e,sub,3) && e[7]==1 && 0x10997bbu+(uint32_t)d3==SCW_QUEUE_COUNT;
}
static int scw_pins(void) {
    static unsigned char body[SCW_SIZE];
    return read_local(image+SCW_ENTRY,body,SCW_SIZE) && hash_ok(body,SCW_SIZE,SCW_SHA) && !memcmp(body,scw_original,7) && scw_loop_pins();
}
static int scw_overlaps(const void *source,size_t bytes) {
    if(!*(volatile LONG*)&scw_any || !image || !source || !bytes) return 0;
    const uintptr_t a=(uintptr_t)source,lo=(uintptr_t)(image+SCW_ENTRY),hi=lo+7;
    return a<hi && a+bytes>lo;
}
static void scw_unpatch_copy(const void *source,void *copy,size_t bytes) {
    if(!scw_overlaps(source,bytes)) return;
    const uintptr_t a=(uintptr_t)source,lo=(uintptr_t)(image+SCW_ENTRY);
    for(unsigned i=0;i<7;i++) if(lo+i>=a && lo+i<a+bytes) ((unsigned char*)copy)[lo+i-a]=scw_original[i];
}
static int scw_window_clear(void) {
    const uintptr_t entry=(uintptr_t)(image+SCW_ENTRY);
    for(unsigned i=0;i<pmath_frozen_count;i++) {
        CONTEXT c;memset(&c,0,sizeof(c));c.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(pmath_frozen[i],&c)) return 0;
        if(c.Rip>entry && c.Rip<entry+7) return 0;
    }
    return 1;
}
static int scw_switch(int install) {
    unsigned char ours[7];scw_patch_bytes(ours);
    if(install) InterlockedExchange(&scw_any,1);
    for(unsigned attempt=0;attempt<200;attempt++) {
        if(!pmath_freeze()) break;
        if(!scw_window_clear()) {pmath_thaw();Sleep(1);continue;}
        const int ok=view_cas_bytes(SCW_ENTRY,install?scw_original:ours,install?ours:scw_original,7);
        pmath_thaw();
        if(ok!=install) InterlockedExchange(&scw_any,0);
        return ok;
    }
    if(install) InterlockedExchange(&scw_any,0);
    return 0;
}
static int scw_owned(void);   /* below */
/* The entry hook, shared by the rows and the phase: UI thread, before the first admitted frame. */
static int scw_install(void) {
    if(scw_installed) return scw_owned();
    if(!stats.active || stats.issued || (boot_mode && boot_phase) || GetCurrentThreadId()!=ui_thread) {scw_stage="state";return 0;}
    if(!scw_pins()) {scw_stage="pins";return 0;}
    if(scw_state()!=0) {scw_stage="entry";return 0;}
    {LARGE_INTEGER hz;QueryPerformanceFrequency(&hz);scw_qpc_hz=hz.QuadPart;}
    scw_queue_lock=(CRITICAL_SECTION*)(image+SCW_QUEUE_LOCK);scw_queue_at=(const uint64_t*)(image+SCW_QUEUE_COUNT);
    if(!scw_page) scw_page=scw_alloc_page();
    if(!scw_page || !scw_write_page() || !scw_page_ok()) {scw_stage="page";return 0;}
    scw_calls=scw_in_frame_calls=scw_rows=scw_dropped=scw_failures=0;scw_restored=0;
    if(!scw_switch(1)) {scw_stage="busy";return 0;}
    scw_installed=1;scw_stage="armed";return 1;
}
/* gate.spline_calc_watch(from,to): the rows' window; the probe's setup runs again after the load (a reloaded UI repeats the
 * configuration), so the same window is accepted again. */
static int scw_arm(double from,double to) {
    if(!isfinite(from) || !isfinite(to) || floor(from)!=from || floor(to)!=to || from<1 || to<from || to>1e9) {scw_stage="window";return 0;}
    if(scw_to) return (uint64_t)from==scw_from && (uint64_t)to==scw_to && scw_owned();
    if(!scw_install()) return 0;
    scw_from=(uint64_t)from;scw_to=(uint64_t)to;return 1;
}
/* gate.spline_phase(1): the calculator at the frame's phase points only (repeatable, as the setup's repeat needs). */
static int scw_phase_arm(void) {
    if(scw_phase_on) return scw_owned();
    if(!scw_phase_event) scw_phase_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!scw_phase_event) {scw_stage="phase_event";return 0;}
    if(!scw_install()) return 0;
    scw_phase_drains=scw_phase_jobs=scw_phase_held=scw_phase_timeouts=scw_phase_max_us=0;
    scw_phase_on=1;return 1;
}
static int scw_owned(void) {return !scw_installed || (scw_state()==1 && scw_page_ok());}
static int scw_check(void) {
    scw_flush();
    if(InterlockedCompareExchange64(&scw_failures,0,0)) return 0;
    if(!scw_owned()) {scw_stage="site_changed";InterlockedIncrement64(&scw_failures);return 0;}
    return 1;
}
static void scw_restore(void) {
    if(scw_phase_on) {scw_phase_on=0;if(scw_phase_event) SetEvent(scw_phase_event);}   /* a held calculator runs on */
    if(!scw_installed) return;
    if(scw_state()!=1) {scw_stage="restore_foreign";return;}
    if(scw_switch(0)) {scw_installed=0;scw_restored=1;scw_stage="restored";scw_flush();}
    else scw_stage="restore_busy";
}
static int scw_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity,"installed=%d restored=%d stage=%s window=%llu:%llu calls=%lld in_frame=%lld rows=%lld dropped=%lld failures=%lld"
        " phase=%d:%lld:%lld:%lld:%lld:%lld",
        scw_installed,scw_restored,scw_stage,(unsigned long long)scw_from,(unsigned long long)scw_to,(long long)scw_calls,(long long)scw_in_frame_calls,
        (long long)scw_rows,(long long)scw_dropped,(long long)scw_failures,scw_phase_on,(long long)scw_phase_drains,(long long)scw_phase_jobs,
        (long long)scw_phase_held,(long long)scw_phase_timeouts,(long long)scw_phase_max_us);
    return n>0 && (size_t)n<capacity?n:0;
}

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
#define SCW_XFORM_STUB 0x40u   /* + 0x20 per site */
#define SCW_STEER_STUB 0x80u   /* STEER_PHASE (below): the steering path job's entry */
#define SCW_STEER_TRAMP 0xa0u
#define SCW_CODE 0xc0u
#define SCW_CAP ((size_t)8<<20)
/* Both entries (the route request's 0xec0cb0 and the steering path job's 0xecc6f0) begin with these seven bytes. */
static const unsigned char scw_original[7]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x08};
typedef uint64_t (*scw_fn)(void *,void *,void *,void *);
typedef void (*scw_steer_fn)(void *);
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
/* SPLINE_PHASE (native_gate_spline_phase, a session default since 18 Nov; every client): the calculator runs at fixed points of
 * the frame only. Its
 * entry waits (the hook, before the original) until the main thread opens the phase; path_release opens it twice a frame, before
 * it releases the pathing pass (the requests made since the last join) and after it joins it (those the pass's spline updates
 * made), and waits there until the calculator's queue is empty: 0x1099650 pops a request under the queue's lock 0x3d74910, runs
 * 0xec0cb0, publishes it (+0xa8, under 0x3d74938) and only then takes it off the count +0x3d74980. Nothing else moves the world
 * while the main thread waits there, so every client computes a route from the same state, and the consuming update (the next
 * pass, joined by native_gate_spline_join_all) finds it published. A held calculator past SCW_PHASE_WAIT_MS runs anyway and
 * is counted (timeouts): the queue is emptied twice a frame, so a hold that long means that no frame ran for that long (a
 * session waiting for a stalled client) and the job reads the world the next phase would have shown it. Until 18 Nov it also
 * counted a failure, which as a default would have stopped every waiting client of such a session. A drain past SCW_DRAIN_MS
 * stops the gate (199). Status phase=on:drains:jobs:held:timeouts:max_us.
 * Why a default (18 Nov, two clients on one PC, the station scenario `transport`, ticks 1700..3200): without the phase 22 of
 * 54 jobs began 24..32.5 ms after their gated frame at a tick period of 33.3 ms, one batch 1.7 ms before the next tick. With
 * the worker of client 2 held back 10 ms (native_gate_spline_calc_delay, below) 6 of 54 jobs ran in the next tick on that
 * client and read other transforms than client 1's (3 to 5 of 51, ships under way); held back 40 ms, 22 ran a tick later and
 * 21 read other transforms. With the phase and the same 40 ms: every job in the same tick with the same transforms on both
 * clients. A busy PC is that delay. (Neither delayed run parted in 3,500 ticks: the routes came out alike. The J1 split at
 * 3780 is what such an input does when they do not.)
 * STEER_PHASE (19 Nov, with the phase): the same thread serves a second queue, the steering model's async path jobs. The step
 * 0xec7200 makes one when a ship that wants to dock has no path pending (+0x388) and its path is empty or its retry time
 * +0x378 has passed: it fills a 0x60-byte job and hands it to 0xecc400, which pushes it on the deque 0x3d749f0.. under the
 * lock 0x3d74998, counts it at 0x3d74a08 and wakes the calculator. Once the route queue is empty, 0x1099650 takes the front job
 * under that lock, runs 0xecc6f0 (it sweeps a query sphere through the world with 0x841500/0x841620 and searches the nav
 * graph with 0x108cc00, at three radii if it must), publishes it (+0x58 = 1, 0x109985b xchg) and only then takes it off the
 * count (0x109986b). Each later step of the ship tests +0x58; the first that finds it set copies the result into the path
 * +0x360 and clears +0x388. The route hold stopped the worker before route requests only, so a job ran whenever the worker
 * had nothing else: during the next frame's pass on one client, after it on the other. Laptop hunt kg5 (refit, 19 Nov): a
 * police ship in zone 72b, retrying its dock at wharf 1b5af, made a job every five or six ticks with the same inputs on both
 * clients; at tick 10556 its result came four ticks later on client 1 and five on client 2, and from then the retries ran a
 * tick apart (another timer draw each), the dock rows parted at 10560 and the world at 10800. Now the job's entry waits
 * for the phase like a route request's, and a drain runs until both queues are empty: a job made in a pass is computed after
 * that pass's join and is published at the next step on every client. 0xecc540(1) (OnlineManager::NotifyGameReset through
 * 0xeac1f0) waits until the steering queue is empty, as 0xeb67c0(1) does for the routes: a reset while the worker is held
 * waits up to the hold's bound, as the routes do. Status steer=jobs:held.
 * The Linux gate's twin of the phase and the delay is linux/gate_spline_phase.h (GCC inlined the job into the worker, so there
 * the job waits after its Unlock call, and the route at its function's entry). */
#define SCW_QUEUE_LOCK 0x3d74910u
#define SCW_QUEUE_COUNT 0x3d74980u
#define SCW_STEER_ENTRY 0xecc6f0u
#define SCW_STEER_RESUME 0xecc6f7u
#define SCW_STEER_SHA "04b4281ce2924529b63dd2a65012ab45dfcbc4c5a9b843994bcb2eb86484c713"
#define SCW_STEER_LOCK 0x3d74998u
#define SCW_STEER_COUNT 0x3d74a08u
#define SCW_PHASE_WAIT_MS 10000u
#define SCW_DRAIN_MS 5000u
static int scw_phase_on;
static HANDLE scw_phase_event;
static DWORD scw_drain_ms=SCW_DRAIN_MS,scw_hold_ms=SCW_PHASE_WAIT_MS;   /* tests: shorter */
static volatile LONG64 scw_phase_drains,scw_phase_jobs,scw_phase_held,scw_phase_timeouts,scw_phase_max_us;
static volatile LONG64 scw_steer_calls,scw_steer_held;
/* Set at the install (tests: their own): the route queue's lock and count, the steering queue's lock and count. */
static CRITICAL_SECTION *scw_queue_lock,*scw_steer_lock;static const uint64_t *scw_queue_at,*scw_steer_at;
static int scw_queue_count(uint64_t *count) {
    if(!scw_queue_lock || !scw_queue_at || !scw_steer_lock || !scw_steer_at) return 0;
    uint64_t routes=0,jobs=0;
    EnterCriticalSection(scw_queue_lock);
    int ok=read_local(scw_queue_at,&routes,8);
    LeaveCriticalSection(scw_queue_lock);
    EnterCriticalSection(scw_steer_lock);
    ok=ok && read_local(scw_steer_at,&jobs,8);
    LeaveCriticalSection(scw_steer_lock);
    *count=routes+jobs;
    return ok;
}
/* The main thread at a phase point: let the calculator run until its queue is empty. 0: unreadable or timed out (stop). */
static int scw_phase_drain(void) {
    if(!scw_phase_on || !scw_phase_event) return 1;
    uint64_t count=0;
    if(!scw_queue_count(&count)) {scw_stage="phase_queue";InterlockedIncrement64(&scw_failures);return 0;}
    if(!count) return 1;
    LARGE_INTEGER t0,t1;QueryPerformanceCounter(&t0);
    const LONG64 before=InterlockedCompareExchange64(&scw_calls,0,0)+InterlockedCompareExchange64(&scw_steer_calls,0,0);
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
    InterlockedAdd64(&scw_phase_jobs,InterlockedCompareExchange64(&scw_calls,0,0)+InterlockedCompareExchange64(&scw_steer_calls,0,0)-before);
    const LONG64 us=scw_qpc_hz?(t1.QuadPart-t0.QuadPart)*1000000/scw_qpc_hz:0;
    if(us>scw_phase_max_us) scw_phase_max_us=us;
    return 1;
}
/* The calculator's entry while the phase is on: wait for it to open (bounded). */
static void scw_phase_hold(void) {
    if(!scw_phase_on || !scw_phase_event || WaitForSingleObject(scw_phase_event,0)==WAIT_OBJECT_0) return;
    InterlockedIncrement64(&scw_phase_held);
    if(WaitForSingleObject(scw_phase_event,scw_hold_ms)!=WAIT_OBJECT_0) {
        InterlockedIncrement64(&scw_phase_timeouts);   /* no frame for that long: it runs, counted */
    }
}
/* SPLINE_CALC_DELAY (test only, native_gate_spline_calc_delay = {slot, ms}; the client of that slot alone): the calculator's
 * worker sleeps ms at the entry of every job, before the phase's hold, as a worker does that a busy PC keeps from its
 * processor. 18 Nov, a two-client station run on a quiet PC: 22 of 54 jobs began 24..32.5 ms after their gated frame, at a
 * tick period of 33.3 ms, and on both clients inside the same tick; a few milliseconds more and such a job runs in the next
 * tick on one client alone. The option makes that happen at will: without the phase the delayed client's routes read a later
 * world, with the phase the main thread waits for the delayed worker and nothing changes. */
#define SCW_DELAY_MAX_MS 200u
static volatile LONG scw_delay_ms;
static volatile LONG64 scw_delayed;
static void scw_delay_wait(void) {
    const LONG ms=InterlockedCompareExchange(&scw_delay_ms,0,0);
    if(ms>0) {InterlockedIncrement64(&scw_delayed);Sleep((DWORD)ms);}
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
/* SPLINE_XFORM (with the window): j1-360 found the request's own inputs equal at 3722 (req=, inp=, obs=, the component vector
 * cmp= in the same order) and the route's two inner nodes 1..4 ulps apart. The calculator takes its components' transforms
 * relative to the request's space through 0x3dde10(component, out, space, 0) at 0xec13db and 0xec1ebd: while the window is
 * armed both calls (E8 rel32) go through our page, the original runs, and a call while stats.issued lies in [from,to] writes a
 * row: the tick, the site, the request being calculated (the component of its +0x28 as a portable key, its index), the
 * component and the space as portable keys and the 0x40-byte result raw. Patched with every other thread suspended (a rel32
 * may straddle two 16-byte blocks), owned and restored with the entry.
 * j1-361: all 405 transforms of 3715..3730 equal. The calculator's obstacle volume (0xec0800) is the component's bounds
 * (vfunc 0x14b0) inflated by the request's margin, for class 0x62 the box at +0xd20: for a station (its vfunc 0x14b0 the
 * getter 0x7eb460, lea rax,[rcx+0xc90]) the row adds b= the struct at +0xc90 and the box at +0xd20, 0x30 bytes each raw
 * (read only, no call); '-' for other classes. */
#define SCW_XFORM_FN 0x3dde10u
#define SCW_BOUNDS_GETTER 0x7eb460u
#define SCW_XFORM_SITES 2u
static const unsigned scw_xform_sites[SCW_XFORM_SITES]={0xec13dbu,0xec1ebdu};
typedef uint64_t (*scw_xform_fn)(void *,void *,void *,uint64_t,uint64_t,uint64_t);
static int scw_xform_on;
static volatile LONG64 scw_xform_rows;
static uint64_t scw_cur_key;static uint32_t scw_cur_index;   /* the request the calculator runs (one worker) */
static int scw_xform_row(char *out,size_t capacity,uint64_t tick,unsigned site,uint64_t request,uint32_t index,uint64_t key,uint64_t space,
                         const unsigned char m[0x40],const unsigned char *bounds) {
    char hex[0x81],bh[2*0x60+1];static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<0x40;i++) {hex[2*i]=digits[m[i]>>4];hex[2*i+1]=digits[m[i]&15];}
    hex[0x80]=0;
    if(bounds) {for(unsigned i=0;i<0x60;i++) {bh[2*i]=digits[bounds[i]>>4];bh[2*i+1]=digits[bounds[i]&15];} bh[0xc0]=0;}
    else {bh[0]='-';bh[1]=0;}
    const int n=snprintf(out,capacity,"[X4LAN] NATIVE_GATE SPLINE_XFORM tick=%llu site=%u request=%016llx index=%u key=%016llx space=%016llx m=%s b=%s\r\n",
        (unsigned long long)tick,site,(unsigned long long)request,index,(unsigned long long)key,(unsigned long long)space,hex,bh);
    return n>0 && (size_t)n<capacity?n:0;
}
static void scw_xform_note(unsigned site,void *component,const void *out,void *space) {
    const uint64_t tick=stats.issued;
    if(!scw_to || tick<scw_from || tick>scw_to) return;
    uint64_t id=0,sid=0;unsigned char m[0x40];
    if(!component || !read_local((const unsigned char*)component+8,&id,8)) id=0;
    if(!space || !read_local((const unsigned char*)space+8,&sid,8)) sid=0;
    if(!out || !read_local(out,m,0x40)) memset(m,0,0x40);
    unsigned char b[0x60];int hb=0;void *vt=NULL;uintptr_t fn=0;
    if(component && read_local(component,&vt,8) && vt && read_local((const unsigned char*)vt+0x14b0,&fn,8) && fn==(uintptr_t)(image+SCW_BOUNDS_GETTER) &&
       read_local((const unsigned char*)component+0xc90,b,0x30) && read_local((const unsigned char*)component+0xd20,b+0x30,0x30)) hb=1;
    char row[640];
    const int n=scw_xform_row(row,sizeof(row),tick,site,scw_cur_key,scw_cur_index,id?world_identity_portable(id):0,sid?world_identity_portable(sid):0,m,
                              hb?b:NULL);
    if(n) {scw_append(row,n);InterlockedIncrement64(&scw_xform_rows);} else InterlockedIncrement64(&scw_dropped);
}
#define SCW_XFORM_HOOK(i) static __attribute__((noinline)) uint64_t scw_xform_hook##i(void *c,void *out,void *space,uint64_t a4,uint64_t a5,uint64_t a6) { \
    const uint64_t r=((scw_xform_fn)(void*)(image+SCW_XFORM_FN))(c,out,space,a4,a5,a6);scw_xform_note(i,c,out,space);return r;}
SCW_XFORM_HOOK(0) SCW_XFORM_HOOK(1)
static const void *scw_xform_hook(unsigned i) {return i?(const void*)scw_xform_hook1:(const void*)scw_xform_hook0;}
static __attribute__((noinline)) uint64_t scw_hook(void *request,void *a2,void *a3,void *a4) {
    scw_delay_wait();   /* SPLINE_CALC_DELAY (test only): a starved worker */
    scw_phase_hold();   /* SPLINE_PHASE: only inside a phase */
    InterlockedIncrement64(&scw_calls);
    const uint64_t tick_in=stats.issued;const int open_in=InterlockedCompareExchange(&scw_frame_open,0,0)!=0;
    const long long us_in=scw_us_since_frame();
    if(open_in) InterlockedIncrement64(&scw_in_frame_calls);
    uint32_t index=0;uint64_t runtime=0;
    if(request) {memcpy(&index,(const unsigned char*)request+SCW_INDEX,4);memcpy(&runtime,(const unsigned char*)request+SCW_RUNTIME,8);}   /* the original reads them next */
    if(scw_xform_on && scw_to && tick_in>=scw_from && tick_in<=scw_to) {scw_cur_key=runtime?world_identity_portable(runtime):0;scw_cur_index=index;}
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
/* STEER_PHASE: the steering path job's entry (one worker; the job's own code runs unchanged). */
static __attribute__((noinline)) void scw_steer_hook(void *job) {
    scw_delay_wait();   /* SPLINE_CALC_DELAY (test only): a starved worker */
    if(scw_phase_on && scw_phase_event && WaitForSingleObject(scw_phase_event,0)!=WAIT_OBJECT_0) InterlockedIncrement64(&scw_steer_held);
    scw_phase_hold();
    InterlockedIncrement64(&scw_steer_calls);
    ((scw_steer_fn)(void*)(scw_page+SCW_STEER_TRAMP))(job);
}
/* ---- the page and the entries (lockstep_safe_pos_order.h's scheme) ---- */
static void scw_code(unsigned char out[SCW_CODE]) {
    memset(out,0xcc,SCW_CODE);
    spo_jump_to(out+SCW_STUB,(const void*)scw_hook);
    memcpy(out+SCW_TRAMP,scw_original,7);spo_jump_to(out+SCW_TRAMP+7,(const void*)(image+SCW_RESUME));
    for(unsigned i=0;i<SCW_XFORM_SITES;i++) spo_jump_to(out+SCW_XFORM_STUB+0x20u*i,scw_xform_hook(i));
    spo_jump_to(out+SCW_STEER_STUB,(const void*)scw_steer_hook);
    memcpy(out+SCW_STEER_TRAMP,scw_original,7);spo_jump_to(out+SCW_STEER_TRAMP+7,(const void*)(image+SCW_STEER_RESUME));
}
static int scw_reach_one(const unsigned char *page,unsigned stub,unsigned entry) {
    const intptr_t d=(intptr_t)(page+stub)-(intptr_t)(image+entry+5);
    return d>INT32_MIN+0x2000 && d<INT32_MAX-0x2000;
}
static int scw_reach(const unsigned char *page) {
    return scw_reach_one(page,SCW_STUB,SCW_ENTRY) && scw_reach_one(page,SCW_STEER_STUB,SCW_STEER_ENTRY);
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
/* An entry's seven bytes while installed: jmp rel32 to its stub, two int3. */
#define SCW_ENTRIES 2u
static const unsigned scw_entries[SCW_ENTRIES]={SCW_ENTRY,SCW_STEER_ENTRY},scw_stubs[SCW_ENTRIES]={SCW_STUB,SCW_STEER_STUB};
static void scw_patch_bytes_at(unsigned i,unsigned char out[7]) {
    const int32_t rel=(int32_t)((intptr_t)(scw_page+scw_stubs[i])-(intptr_t)(image+scw_entries[i]+5));out[0]=0xe9;memcpy(out+1,&rel,4);out[5]=0xcc;out[6]=0xcc;
}
/* One entry: 0 the original, 1 ours, -1 anything else. */
static int scw_entry_state(unsigned i) {
    unsigned char now[7],ours[7];memcpy(now,(const void*)(image+scw_entries[i]),7);
    if(!memcmp(now,scw_original,7)) return 0;
    if(scw_page && scw_reach(scw_page)) {scw_patch_bytes_at(i,ours);if(!memcmp(now,ours,7)) return 1;}
    return -1;
}
/* Both entries: 0 both original, 1 both ours, -1 anything else (one of each too). */
static int scw_state(void) {
    const int a=scw_entry_state(0),b=scw_entry_state(1);
    return a==b?a:-1;
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
/* STEER_PHASE: the same loop's steering part: the lock it takes the front job under (0x1099807 lea rcx,[0x3d74998]), its call
 * of 0xecc6f0 (0x1099851), the publication (0x109985b xchg [rbx+0x58],eax) and only then the count's decrement (0x109986b sub
 * qword [0x3d74a08],1). */
static int scw_steer_loop_pins(void) {
    unsigned char a[7],b[5],c[3],e[8];int32_t d1,d2,d3;
    if(!read_local(image+0x1099807,a,7) || !read_local(image+0x1099851,b,5) || !read_local(image+0x109985b,c,3) || !read_local(image+0x109986b,e,8)) return 0;
    memcpy(&d1,a+3,4);memcpy(&d2,b+1,4);memcpy(&d3,e+3,4);
    static const unsigned char lea[3]={0x48,0x8d,0x0d},xchg[3]={0x87,0x43,0x58},sub[3]={0x48,0x83,0x2d};
    return !memcmp(a,lea,3) && 0x109980eu+(uint32_t)d1==SCW_STEER_LOCK && b[0]==0xe8 && 0x1099856u+(uint32_t)d2==SCW_STEER_ENTRY &&
           !memcmp(c,xchg,3) && !memcmp(e,sub,3) && e[7]==1 && 0x1099873u+(uint32_t)d3==SCW_STEER_COUNT;
}
static int scw_pins(void) {
    static unsigned char body[SCW_SIZE];
    if(!read_local(image+SCW_ENTRY,body,SCW_SIZE) || !hash_ok(body,SCW_SIZE,SCW_SHA) || memcmp(body,scw_original,7) || !scw_loop_pins()) return 0;
    return read_local(image+SCW_STEER_ENTRY,body,SCW_SIZE) && hash_ok(body,SCW_SIZE,SCW_STEER_SHA) && !memcmp(body,scw_original,7) &&
           scw_steer_loop_pins();
}
/* The transform sites' rel32: the original (to 0x3dde10) and ours (to the site's stub). */
static int32_t scw_xform_rel(unsigned i,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+scw_xform_sites[i]+5));}
static int scw_xform_state(unsigned i) {
    unsigned char now[5];memcpy(now,(const void*)(image+scw_xform_sites[i]),5);
    if(now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==scw_xform_rel(i,image+SCW_XFORM_FN)) return 0;
    if(scw_page && d==scw_xform_rel(i,scw_page+SCW_XFORM_STUB+0x20u*i)) return 1;
    return -1;
}
static int scw_xform_pins(void) {
    for(unsigned i=0;i<SCW_XFORM_SITES;i++) {
        unsigned char now[5];int32_t d;
        if(!read_local(image+scw_xform_sites[i],now,5) || now[0]!=0xe8) return 0;
        memcpy(&d,now+1,4);if(d!=scw_xform_rel(i,image+SCW_XFORM_FN)) return 0;
    }
    return 1;
}
static int scw_overlaps(const void *source,size_t bytes) {
    if(!*(volatile LONG*)&scw_any || !image || !source || !bytes) return 0;
    const uintptr_t a=(uintptr_t)source;
    for(unsigned k=0;k<SCW_ENTRIES;k++) {const uintptr_t lo=(uintptr_t)(image+scw_entries[k]),hi=lo+7;if(a<hi && a+bytes>lo) return 1;}
    if(scw_xform_on) for(unsigned i=0;i<SCW_XFORM_SITES;i++) {
        const uintptr_t s=(uintptr_t)(image+scw_xform_sites[i]+1);
        if(a<s+4 && a+bytes>s) return 1;
    }
    return 0;
}
static void scw_unpatch_copy(const void *source,void *copy,size_t bytes) {
    if(!scw_overlaps(source,bytes)) return;
    const uintptr_t a=(uintptr_t)source;
    for(unsigned k=0;k<SCW_ENTRIES;k++) {
        const uintptr_t lo=(uintptr_t)(image+scw_entries[k]);
        for(unsigned i=0;i<7;i++) if(lo+i>=a && lo+i<a+bytes) ((unsigned char*)copy)[lo+i-a]=scw_original[i];
    }
    if(scw_xform_on) for(unsigned i=0;i<SCW_XFORM_SITES;i++) {
        const uintptr_t s=(uintptr_t)(image+scw_xform_sites[i]+1);const int32_t d=scw_xform_rel(i,image+SCW_XFORM_FN);unsigned char o[4];memcpy(o,&d,4);
        for(unsigned k=0;k<4;k++) if(s+k>=a && s+k<a+bytes) ((unsigned char*)copy)[s+k-a]=o[k];
    }
}
/* Both sites to ours (install) or back, every other thread suspended; all or none. */
static int scw_xform_switch(int install) {
    for(unsigned attempt=0;attempt<200;attempt++) {
        if(!pmath_freeze()) break;
        unsigned done=0;int ok=1;
        for(unsigned i=0;i<SCW_XFORM_SITES && ok;i++) {
            const int32_t orig=scw_xform_rel(i,image+SCW_XFORM_FN),ours=scw_xform_rel(i,scw_page+SCW_XFORM_STUB+0x20u*i);
            unsigned char e[4],v[4];memcpy(e,install?&orig:&ours,4);memcpy(v,install?&ours:&orig,4);
            if(view_cas_bytes(scw_xform_sites[i]+1,e,v,4)) done|=1u<<i;else ok=0;
        }
        if(!ok) for(unsigned i=0;i<SCW_XFORM_SITES;i++) if(done&(1u<<i)) {   /* undo */
            const int32_t orig=scw_xform_rel(i,image+SCW_XFORM_FN),ours=scw_xform_rel(i,scw_page+SCW_XFORM_STUB+0x20u*i);
            unsigned char e[4],v[4];memcpy(e,install?&ours:&orig,4);memcpy(v,install?&orig:&ours,4);view_cas_bytes(scw_xform_sites[i]+1,e,v,4);
        }
        pmath_thaw();
        return ok;
    }
    return 0;
}
static int scw_window_clear(void) {
    for(unsigned i=0;i<pmath_frozen_count;i++) {
        CONTEXT c;memset(&c,0,sizeof(c));c.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(pmath_frozen[i],&c)) return 0;
        for(unsigned k=0;k<SCW_ENTRIES;k++) {const uintptr_t entry=(uintptr_t)(image+scw_entries[k]);if(c.Rip>entry && c.Rip<entry+7) return 0;}
    }
    return 1;
}
/* Both entries to ours (install) or back, every other thread suspended; all or none. */
static int scw_switch(int install) {
    if(install) InterlockedExchange(&scw_any,1);
    for(unsigned attempt=0;attempt<200;attempt++) {
        if(!pmath_freeze()) break;
        if(!scw_window_clear()) {pmath_thaw();Sleep(1);continue;}
        unsigned done=0;int ok=1;
        for(unsigned k=0;k<SCW_ENTRIES && ok;k++) {
            unsigned char ours[7];scw_patch_bytes_at(k,ours);
            if(view_cas_bytes(scw_entries[k],install?scw_original:ours,install?ours:scw_original,7)) done|=1u<<k;else ok=0;
        }
        if(!ok) for(unsigned k=0;k<SCW_ENTRIES;k++) if(done&(1u<<k)) {   /* undo */
            unsigned char ours[7];scw_patch_bytes_at(k,ours);
            view_cas_bytes(scw_entries[k],install?ours:scw_original,install?scw_original:ours,7);
        }
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
    scw_steer_lock=(CRITICAL_SECTION*)(image+SCW_STEER_LOCK);scw_steer_at=(const uint64_t*)(image+SCW_STEER_COUNT);
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
    if(!scw_xform_pins()) {scw_stage="xform_pins";return 0;}
    for(unsigned i=0;i<SCW_XFORM_SITES;i++) if(scw_xform_state(i)!=0) {scw_stage="xform_site";return 0;}
    if(!scw_install()) return 0;
    scw_xform_on=1;   /* before the switch: the view hides the sites from the first byte written */
    if(!scw_xform_switch(1)) {scw_xform_on=0;scw_stage="xform_busy";return 0;}
    scw_xform_rows=0;scw_from=(uint64_t)from;scw_to=(uint64_t)to;return 1;
}
/* gate.spline_phase(1): the calculator at the frame's phase points only (repeatable, as the setup's repeat needs). */
static int scw_phase_arm(void) {
    if(scw_phase_on) return scw_owned();
    if(!scw_phase_event) scw_phase_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!scw_phase_event) {scw_stage="phase_event";return 0;}
    if(!scw_install()) return 0;
    scw_phase_drains=scw_phase_jobs=scw_phase_held=scw_phase_timeouts=scw_phase_max_us=0;scw_steer_calls=scw_steer_held=0;
    scw_phase_on=1;return 1;
}
static int scw_owned(void) {
    if(!scw_installed) return 1;
    if(scw_state()!=1 || !scw_page_ok()) return 0;
    for(unsigned i=0;scw_xform_on && i<SCW_XFORM_SITES;i++) if(scw_xform_state(i)!=1) return 0;
    return 1;
}
/* gate.spline_calc_delay(ms): test only; 1..SCW_DELAY_MAX_MS, repeatable with the same value (the setup's repeat). */
static int scw_delay_arm(double ms) {
    if(!isfinite(ms) || floor(ms)!=ms || ms<1 || ms>SCW_DELAY_MAX_MS) {scw_stage="delay";return 0;}
    if(scw_delay_ms) return (LONG)ms==scw_delay_ms && scw_owned();
    if(!scw_install()) return 0;
    scw_delayed=0;InterlockedExchange(&scw_delay_ms,(LONG)ms);return 1;
}
static int scw_check(void) {
    scw_flush();
    if(InterlockedCompareExchange64(&scw_failures,0,0)) return 0;
    if(!scw_owned()) {scw_stage="site_changed";InterlockedIncrement64(&scw_failures);return 0;}
    return 1;
}
static void scw_restore(void) {
    if(scw_phase_on) {scw_phase_on=0;if(scw_phase_event) SetEvent(scw_phase_event);}   /* a held calculator runs on */
    InterlockedExchange(&scw_delay_ms,0);
    if(!scw_installed) return;
    if(scw_xform_on) {
        int ours=1;for(unsigned i=0;i<SCW_XFORM_SITES;i++) if(scw_xform_state(i)!=1) ours=0;
        if(!ours) {scw_stage="restore_foreign";return;}
        if(!scw_xform_switch(0)) {scw_stage="restore_busy";return;}
        scw_xform_on=0;
    }
    if(scw_state()!=1) {scw_stage="restore_foreign";return;}
    if(scw_switch(0)) {scw_installed=0;scw_restored=1;scw_stage="restored";scw_flush();}
    else scw_stage="restore_busy";
}
static int scw_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity,"installed=%d restored=%d stage=%s window=%llu:%llu calls=%lld in_frame=%lld rows=%lld dropped=%lld failures=%lld"
        " phase=%d:%lld:%lld:%lld:%lld:%lld xform=%d:%lld delay=%ld:%lld steer=%lld:%lld",
        scw_installed,scw_restored,scw_stage,(unsigned long long)scw_from,(unsigned long long)scw_to,(long long)scw_calls,(long long)scw_in_frame_calls,
        (long long)scw_rows,(long long)scw_dropped,(long long)scw_failures,scw_phase_on,(long long)scw_phase_drains,(long long)scw_phase_jobs,
        (long long)scw_phase_held,(long long)scw_phase_timeouts,(long long)scw_phase_max_us,scw_xform_on,(long long)scw_xform_rows,
        (long)scw_delay_ms,(long long)scw_delayed,(long long)scw_steer_calls,(long long)scw_steer_held);
    return n>0 && (size_t)n<capacity?n:0;
}

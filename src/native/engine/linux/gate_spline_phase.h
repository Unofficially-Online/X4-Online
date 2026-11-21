/* native_gate_spline_phase on the Linux X4 9.00 executable: the Linux twin of lockstep_spline_calc_watch.h's SPLINE_PHASE
 * and STEER_PHASE (the mechanism and the evidence are there: the 18 Nov station runs, the laptop hunts kg5 and kh of 19 Nov)
 * and of its test-only SPLINE_CALC_DELAY. Included by gate_linux.c only.
 *
 * Linux facts. AI::NavSplineCalculator (vtable 0x3b28b38, Run = slot 2 0x10b0b30, Windows 0x1099650; gate_spline_join.h)
 * serves two libstdc++ deques of pointers. Routes: start.cur 0x3e769f0 against finish.cur 0x3e76a10, under the pthread
 * mutex 0x3e769a8 (Windows: the critical section 0x3d74910 and the count 0x3d74980). The worker reads the front under
 * that mutex, calls 0x1374d90(request) at 0x10b0bdf (Windows 0xec0cb0; its one caller), publishes it (xchg
 * [request+0xb8],1 at 0x10b0c07, under the XLib::Mutex 0x3e76960) and only then takes it off (start.cur += 8 at 0x10b0c50,
 * or the node step at 0x10b0e18). Steering path jobs: start.cur 0x3e76ad0 against finish.cur 0x3e76af0, under the
 * XLib::Mutex 0x3e76a80 (Lock 0x1a23650 and Unlock 0x1a236a0 take its pthread mutex at +8; Windows 0x3d74998 and the
 * count 0x3d74a08). GCC inlined the job (Windows 0xecc6f0) into the worker: the front is read under the mutex, the
 * Unlock at 0x10b0cc1, the job (the sweep at 0x10b0cd2, the nav search 0x13d4d90, at three radii if it must), its
 * publication (xchg [job+0x58],1 at 0x10b0d54) and only then the take-off under the mutex (0x10b0d89, or the node step
 * at 0x10b1059). As on Windows, a deque is empty only once its last item is published.
 *
 * So a route's hold sits at 0x1374d90's entry: its first 8 bytes (push rbp; mov rbp,rsp; push r15; push r14: whole
 * instructions, nothing RIP-relative) become jmp rel32 to a stub on a page of ours near the image, in one aligned 8-byte
 * compare-exchange; the trampoline replays them and jumps to 0x1374d98. The call at 0x10b0bdf stays X4's: it lies in
 * spline_worker_run, which gate_spline_join.h hashes at every tick. A job's hold sits after its Unlock: the call at
 * 0x10b0cc1 (rel32, one compare-exchange of the 16-byte line that holds it) goes to a stub that unlocks as X4 does and
 * then waits for the phase, as Windows' hook waits at 0xecc6f0's entry, after LeaveCriticalSection. The phase points are
 * gate_path.h's lxp_path_release, as Windows' path_release: before the pass's release and after its join, the drain runs
 * until both deques are empty (each read under its mutex).
 *
 * Fail closed: spline_worker_run (the route part of the loop), spline_phase_steer (the job part, read with X4's call at
 * 0x10b0cc1) and spline_phase_route (0x1374d90's head, read with X4's 8 bytes), and the loop's references to both
 * deques, both mutexes and both publications decoded; a build that differs keeps X4's code. Armed on the MoveWorker
 * before the first admitted frame, both sites or neither; the per-tick check (stop reason 199, as Windows) and the
 * restore are this file's (the page stays: a thread may be inside it). Status as Windows' (the watch's fields 0). */
#define LXSP_STOP_REASON 199u
#define LXSP_ROUTE_VA 0x1374d90u
#define LXSP_ROUTE_RESUME_VA 0x1374d98u
#define LXSP_STEER_CALL_VA 0x10b0cc1u
#define LXSP_UNLOCK_VA 0x1a236a0u
#define LXSP_ROUTE_MUTEX_VA 0x3e769a8u
#define LXSP_ROUTE_BEGIN_VA 0x3e769f0u
#define LXSP_ROUTE_END_VA 0x3e76a10u
#define LXSP_STEER_MUTEX_VA 0x3e76a80u   /* XLib::Mutex; its pthread mutex at +8 */
#define LXSP_STEER_BEGIN_VA 0x3e76ad0u
#define LXSP_STEER_END_VA 0x3e76af0u
#define LXSP_PHASE_WAIT_MS 10000u
#define LXSP_DRAIN_MS 5000u
#define LXSP_DELAY_MAX_MS 200u
#define LXSP_ROUTE_STUB 0x00u
#define LXSP_ROUTE_TRAMP 0x20u
#define LXSP_STEER_STUB 0x40u
#define LXSP_CODE 0x60u
static const unsigned char lxsp_route_head[8]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56};
typedef uint64_t (*lxsp_route_fn)(void *,void *,void *,void *,void *,void *);
typedef void (*lxsp_unlock_fn)(void *);
static unsigned char *lxsp_page;
static int lxsp_installed,lxsp_restored,lxsp_on;
static const char *lxsp_stage="idle";
static volatile int64_t lxsp_calls,lxsp_failures,lxsp_drains,lxsp_jobs,lxsp_held,lxsp_timeouts,lxsp_max_us,lxsp_steer_calls,lxsp_steer_held,lxsp_delayed;
static volatile int lxsp_delay_ms;
static unsigned lxsp_drain_ms=LXSP_DRAIN_MS,lxsp_hold_ms=LXSP_PHASE_WAIT_MS;   /* tests: shorter */
static lxsp_unlock_fn lxsp_unlock;   /* tests: a stand-in for X4's Unlock */
/* Set at the install (tests: their own): each deque's mutex and its start.cur and finish.cur. */
static pthread_mutex_t *lxsp_route_lock,*lxsp_steer_lock;
static void *const *lxsp_route_begin,*const *lxsp_route_end,*const *lxsp_steer_begin,*const *lxsp_steer_end;
/* The phase: a manual-reset event on the monotonic clock, open while the main thread drains. */
static pthread_mutex_t lxsp_event_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t lxsp_event_cond;
static int lxsp_event_ready,lxsp_event_open;
static int lxsp_event_init(void) {
    if(lxsp_event_ready) return 1;
    pthread_condattr_t a;
    if(pthread_condattr_init(&a)) return 0;
    const int ok=!pthread_condattr_setclock(&a,CLOCK_MONOTONIC) && !pthread_cond_init(&lxsp_event_cond,&a);
    pthread_condattr_destroy(&a);
    return lxsp_event_ready=ok;
}
static int lxsp_is_open(void) {return __atomic_load_n(&lxsp_event_open,__ATOMIC_SEQ_CST);}
static void lxsp_set(int open) {
    if(!lxsp_event_ready) return;
    pthread_mutex_lock(&lxsp_event_lock);
    __atomic_store_n(&lxsp_event_open,open,__ATOMIC_SEQ_CST);
    if(open) pthread_cond_broadcast(&lxsp_event_cond);
    pthread_mutex_unlock(&lxsp_event_lock);
}
static int lxsp_phase_on(void) {return __atomic_load_n(&lxsp_on,__ATOMIC_SEQ_CST);}
/* A job's entry while the phase is on: wait for it to open (bounded, as Windows: a hold that long means no frame ran
 * for that long; the job then runs, counted). */
static void lxsp_hold(void) {
    if(!lxsp_phase_on() || !lxsp_event_ready || lxsp_is_open()) return;
    InterlockedIncrement64(&lxsp_held);
    struct timespec until;clock_gettime(CLOCK_MONOTONIC,&until);
    until.tv_sec+=(time_t)(lxsp_hold_ms/1000u);until.tv_nsec+=(long)(lxsp_hold_ms%1000u)*1000000L;
    if(until.tv_nsec>=1000000000L) {until.tv_sec++;until.tv_nsec-=1000000000L;}
    pthread_mutex_lock(&lxsp_event_lock);
    int r=0;
    while(!lxsp_event_open && r!=ETIMEDOUT) r=pthread_cond_timedwait(&lxsp_event_cond,&lxsp_event_lock,&until);
    const int open=lxsp_event_open;
    pthread_mutex_unlock(&lxsp_event_lock);
    if(!open) InterlockedIncrement64(&lxsp_timeouts);
}
/* SPLINE_CALC_DELAY (test only): the worker sleeps ms at the start of every job, before the hold. */
static void lxsp_delay_wait(void) {
    const int ms=__atomic_load_n(&lxsp_delay_ms,__ATOMIC_SEQ_CST);
    if(ms>0) {InterlockedIncrement64(&lxsp_delayed);usleep((useconds_t)ms*1000u);}
}
/* 1 and count = the deques that hold an item (0..2), each read under its mutex; 0 unknown or unlockable. */
static int lxsp_queue_count(uint64_t *count) {
    if(!lxsp_route_lock || !lxsp_route_begin || !lxsp_route_end || !lxsp_steer_lock || !lxsp_steer_begin || !lxsp_steer_end) return 0;
    void *rb,*re,*sb,*se;
    if(pthread_mutex_lock(lxsp_route_lock)) return 0;
    rb=__atomic_load_n((void**)lxsp_route_begin,__ATOMIC_SEQ_CST);re=__atomic_load_n((void**)lxsp_route_end,__ATOMIC_SEQ_CST);
    pthread_mutex_unlock(lxsp_route_lock);
    if(pthread_mutex_lock(lxsp_steer_lock)) return 0;
    sb=__atomic_load_n((void**)lxsp_steer_begin,__ATOMIC_SEQ_CST);se=__atomic_load_n((void**)lxsp_steer_end,__ATOMIC_SEQ_CST);
    pthread_mutex_unlock(lxsp_steer_lock);
    *count=(uint64_t)(rb!=re)+(uint64_t)(sb!=se);return 1;
}
static int64_t lxsp_done(void) {return __atomic_load_n(&lxsp_calls,__ATOMIC_SEQ_CST)+__atomic_load_n(&lxsp_steer_calls,__ATOMIC_SEQ_CST);}
/* The main thread at a phase point: let the calculator run until both deques are empty. 0: unreadable or timed out
 * (the gate stops, 199). */
static int lxsp_drain(void) {
    if(!lxsp_phase_on() || !lxsp_event_ready) return 1;
    uint64_t count=0;
    if(!lxsp_queue_count(&count)) {lxsp_stage="phase_queue";InterlockedIncrement64(&lxsp_failures);return 0;}
    if(!count) return 1;
    LARGE_INTEGER t0,t1;QueryPerformanceCounter(&t0);
    const int64_t before=lxsp_done();
    InterlockedIncrement64(&lxsp_drains);lxsp_set(1);
    const ULONGLONG start=GetTickCount64();
    for(;;) {
        if(!lxsp_queue_count(&count)) {lxsp_set(0);lxsp_stage="phase_queue";InterlockedIncrement64(&lxsp_failures);return 0;}
        if(!count) break;
        if(GetTickCount64()-start>lxsp_drain_ms) {lxsp_set(0);lxsp_stage="phase_drain_timeout";InterlockedIncrement64(&lxsp_failures);return 0;}
        sched_yield();
    }
    lxsp_set(0);
    QueryPerformanceCounter(&t1);   /* microseconds (gate_platform.h) */
    InterlockedAdd64(&lxsp_jobs,lxsp_done()-before);
    const int64_t us=t1.QuadPart-t0.QuadPart;
    if(us>lxsp_max_us) lxsp_max_us=us;
    return 1;
}
/* ---- the hooks (one worker) ---- */
static __attribute__((noinline)) uint64_t lxsp_route_hook(void *a,void *b,void *c,void *d,void *e,void *f) {
    lxsp_delay_wait();   /* SPLINE_CALC_DELAY (test only): a starved worker */
    lxsp_hold();         /* SPLINE_PHASE: only inside a phase */
    InterlockedIncrement64(&lxsp_calls);
    return ((lxsp_route_fn)(void*)(lxsp_page+LXSP_ROUTE_TRAMP))(a,b,c,d,e,f);
}
/* STEER_PHASE: the job's Unlock, then the hold; the inlined job runs on in X4's code. */
static __attribute__((noinline)) void lxsp_steer_unlock(void *mutex) {
    (lxsp_unlock?lxsp_unlock:(lxsp_unlock_fn)(void*)LX(LXSP_UNLOCK_VA))(mutex);
    lxsp_delay_wait();
    if(lxsp_phase_on() && lxsp_event_ready && !lxsp_is_open()) InterlockedIncrement64(&lxsp_steer_held);
    lxsp_hold();
    InterlockedIncrement64(&lxsp_steer_calls);
}
/* ---- the page and the two sites ---- */
static void lxsp_code(unsigned char out[LXSP_CODE]) {
    memset(out,0xcc,LXSP_CODE);
    lxw_jump_write(out+LXSP_ROUTE_STUB,(const void*)lxsp_route_hook);
    memcpy(out+LXSP_ROUTE_TRAMP,lxsp_route_head,8);lxw_jump_write(out+LXSP_ROUTE_TRAMP+8,(const void*)LX(LXSP_ROUTE_RESUME_VA));
    lxw_jump_write(out+LXSP_STEER_STUB,(const void*)lxsp_steer_unlock);
}
static int lxsp_page_ok(void) {unsigned char want[LXSP_CODE];if(!lxsp_page) return 0;lxsp_code(want);return !memcmp(lxsp_page,want,LXSP_CODE);}
static int lxsp_rel(int32_t *out,const unsigned char *from_end,const void *target) {
    const intptr_t d=(intptr_t)target-(intptr_t)from_end;
    if(d!=(intptr_t)(int32_t)d) return 0;
    *out=(int32_t)d;return 1;
}
/* The route entry's 8 bytes: X4's, or ours (jmp rel32 to the stub, three int3). */
static int lxsp_route_word(int ours,unsigned char out[8]) {
    if(!ours) {memcpy(out,lxsp_route_head,8);return 1;}
    int32_t r;
    if(!lxsp_page || !lxsp_rel(&r,LX(LXSP_ROUTE_VA)+5,lxsp_page+LXSP_ROUTE_STUB)) return 0;
    out[0]=0xe9;memcpy(out+1,&r,4);out[5]=out[6]=out[7]=0xcc;return 1;
}
/* The job's Unlock call: X4's (to 0x1a236a0) or ours (to the stub). */
static int lxsp_steer_call(int ours,unsigned char out[5]) {
    int32_t r;out[0]=0xe8;
    if(ours && !lxsp_page) return 0;
    if(!lxsp_rel(&r,LX(LXSP_STEER_CALL_VA)+5,ours?(const void*)(lxsp_page+LXSP_STEER_STUB):(const void*)LX(LXSP_UNLOCK_VA))) return 0;
    memcpy(out+1,&r,4);return 1;
}
static int lxsp_route_state(void) {   /* 0 X4's, 1 ours, -1 other */
    unsigned char now[8],want[8];
    if(!gate_read_local(LX(LXSP_ROUTE_VA),now,8)) return -1;
    if(!memcmp(now,lxsp_route_head,8)) return 0;
    if(lxsp_route_word(1,want) && !memcmp(now,want,8)) return 1;
    return -1;
}
static int lxsp_steer_state(void) {   /* 0 X4's, 1 ours, -1 other */
    unsigned char now[5],want[5];
    if(!gate_read_local(LX(LXSP_STEER_CALL_VA),now,5)) return -1;
    if(lxsp_steer_call(0,want) && !memcmp(now,want,5)) return 0;
    if(lxsp_steer_call(1,want) && !memcmp(now,want,5)) return 1;
    return -1;
}
/* The target of a RIP-relative operand: the instruction at va, length bytes, its disp32 at +at. */
static uint32_t lxsp_rip(uint32_t va,unsigned length,unsigned at) {
    int32_t d;memcpy(&d,LX(va+at),4);return va+length+(uint32_t)d;
}
/* The loop as the phase reads it: both deques' ends, both mutexes, the route call, both publications and both take-offs. */
static int lxsp_loop_pins(void) {
    static const unsigned char publish_route[7]={0x41,0x87,0x86,0xb8,0x00,0x00,0x00},publish_job[3]={0x87,0x45,0x58};
    return lxsp_rip(0x10b0b95u,7,3)==LXSP_ROUTE_MUTEX_VA && lxsp_rip(0x10b0b77u,7,3)==LXSP_ROUTE_BEGIN_VA &&
           lxsp_rip(0x10b0b70u,7,3)==LXSP_ROUTE_END_VA && LX(0x10b0bdfu)[0]==0xe8 && lxsp_rip(0x10b0bdfu,5,1)==LXSP_ROUTE_VA &&
           !memcmp(LX(0x10b0c07u),publish_route,7) && lxsp_rip(0x10b0c50u,7,3)==LXSP_ROUTE_BEGIN_VA &&
           lxsp_rip(0x10b0c90u,7,3)==LXSP_STEER_END_VA && lxsp_rip(0x10b0c97u,7,3)==LXSP_STEER_BEGIN_VA &&
           lxsp_rip(0x10b0cb7u,7,3)==LXSP_STEER_MUTEX_VA && !memcmp(LX(0x10b0d54u),publish_job,3) &&
           lxsp_rip(0x10b0d57u,7,3)==LXSP_STEER_MUTEX_VA && lxsp_rip(0x10b0d89u,7,3)==LXSP_STEER_BEGIN_VA;
}
static int lxsp_pins(void) {
    const lx_site *w=site_named("spline_worker_run"),*r=site_named("spline_phase_route"),*s=site_named("spline_phase_steer");
    unsigned char copy[0x140];
    if(!w || !r || !s || r->va!=LXSP_ROUTE_VA || r->length<8 || r->length>sizeof(copy) || s->length>sizeof(copy) ||
       s->va>LXSP_STEER_CALL_VA || LXSP_STEER_CALL_VA+5>s->va+s->length || !site_ok(w)) return 0;
    if(lxsp_route_state()<0 || !gate_read_local(LX(r->va),copy,r->length)) return 0;
    memcpy(copy,lxsp_route_head,8);
    if(!hash_ok(copy,r->length,r->sha256)) return 0;
    if(lxsp_steer_state()<0 || !gate_read_local(LX(s->va),copy,s->length) || !lxsp_steer_call(0,copy+(LXSP_STEER_CALL_VA-s->va))) return 0;
    return hash_ok(copy,s->length,s->sha256) && lxsp_loop_pins();
}
static int lxsp_route_swap(int install) {
    unsigned char from[8],to[8];uint64_t f,t;
    if(!lxsp_route_word(!install,from) || !lxsp_route_word(install,to)) return 0;
    memcpy(&f,from,8);memcpy(&t,to,8);
    return gate_code_swap64(LX(LXSP_ROUTE_VA),f,t);
}
/* The rel32 of the job's call, in one compare-exchange of the 16-byte line that holds it. */
static int lxsp_steer_swap(int install) {
    unsigned char from[5],to[5];
    if(!lxsp_steer_call(!install,from) || !lxsp_steer_call(install,to)) return 0;
    unsigned char *rel=LX(LXSP_STEER_CALL_VA)+1,*line=(unsigned char*)((uintptr_t)rel&~(uintptr_t)15);const unsigned at=(unsigned)(rel-line);
    unsigned __int128 current,desired;
    if(at+4>16 || LX(LXSP_STEER_CALL_VA)[0]!=0xe8) return 0;
    memcpy(&current,line,16);
    if(memcmp((unsigned char*)&current+at,from+1,4)) return 0;
    desired=current;memcpy((unsigned char*)&desired+at,to+1,4);
    return gate_code_swap128(line,current,desired);
}
static int lxsp_owned(void) {return !lxsp_installed || (lxsp_route_state()==1 && lxsp_steer_state()==1 && lxsp_page_ok());}
/* Both sites, shared by the phase and the delay: the MoveWorker, before the first admitted frame. */
static int lxsp_install(void) {
    if(lxsp_installed) return lxsp_owned();
    if(!stats.active || stats.issued || (boot_mode && boot_phase) || GetCurrentThreadId()!=ui_thread) {lxsp_stage="state";return 0;}
    if(lxsp_route_state()!=0 || lxsp_steer_state()!=0) {lxsp_stage="entry";return 0;}
    if(!lxsp_pins()) {lxsp_stage="pins";return 0;}
    if(!lxsp_page) lxsp_page=gate_near_page(LX(LXSP_ROUTE_VA));
    if(!lxsp_page || mprotect(lxsp_page,GATE_PAGE,PROT_READ|PROT_WRITE)) {lxsp_stage="page";return 0;}
    memset(lxsp_page,0xcc,GATE_PAGE);lxsp_code(lxsp_page);
    if(mprotect(lxsp_page,GATE_PAGE,PROT_READ|PROT_EXEC) || !lxsp_page_ok()) {lxsp_stage="page";return 0;}
    {unsigned char w[8],c[5];if(!lxsp_route_word(1,w) || !lxsp_steer_call(1,c)) {lxsp_stage="reach";return 0;}}
    lxsp_route_lock=(pthread_mutex_t*)(void*)LX(LXSP_ROUTE_MUTEX_VA);lxsp_steer_lock=(pthread_mutex_t*)(void*)(LX(LXSP_STEER_MUTEX_VA)+8);
    lxsp_route_begin=(void*const*)(void*)LX(LXSP_ROUTE_BEGIN_VA);lxsp_route_end=(void*const*)(void*)LX(LXSP_ROUTE_END_VA);
    lxsp_steer_begin=(void*const*)(void*)LX(LXSP_STEER_BEGIN_VA);lxsp_steer_end=(void*const*)(void*)LX(LXSP_STEER_END_VA);
    lxsp_calls=lxsp_failures=0;lxsp_restored=0;
    if(!lxsp_route_swap(1)) {lxsp_stage="busy";return 0;}
    if(!lxsp_steer_swap(1)) {lxsp_route_swap(0);lxsp_stage="busy";return 0;}   /* both or neither */
    lxsp_installed=1;lxsp_stage="armed";return 1;
}
/* gate.spline_phase(1): the calculator at the frame's phase points only (repeatable, as the setup's repeat needs). */
static int lxsp_phase_arm(void) {
    if(lxsp_phase_on()) return lxsp_owned();
    if(!lxsp_event_init()) {lxsp_stage="phase_event";return 0;}
    if(!lxsp_install()) return 0;
    lxsp_drains=lxsp_jobs=lxsp_held=lxsp_timeouts=lxsp_max_us=0;lxsp_steer_calls=lxsp_steer_held=0;
    lxsp_set(0);__atomic_store_n(&lxsp_on,1,__ATOMIC_SEQ_CST);return 1;
}
/* gate.spline_calc_delay(ms): test only; 1..LXSP_DELAY_MAX_MS, repeatable with the same value. */
static int lxsp_delay_arm(double ms) {
    if(!isfinite(ms) || floor(ms)!=ms || ms<1 || ms>LXSP_DELAY_MAX_MS) {lxsp_stage="delay";return 0;}
    const int now=__atomic_load_n(&lxsp_delay_ms,__ATOMIC_SEQ_CST);
    if(now) return (int)ms==now && lxsp_owned();
    if(!lxsp_install()) return 0;
    lxsp_delayed=0;__atomic_store_n(&lxsp_delay_ms,(int)ms,__ATOMIC_SEQ_CST);return 1;
}
/* The main thread's tick check; 0 stops the gate with reason 199. */
static int lxsp_check(void) {
    if(__atomic_load_n(&lxsp_failures,__ATOMIC_SEQ_CST)) return 0;
    if(!lxsp_owned()) {lxsp_stage="site_changed";InterlockedIncrement64(&lxsp_failures);return 0;}
    if(!lxsp_pins()) {lxsp_stage="block_changed";InterlockedIncrement64(&lxsp_failures);return 0;}
    return 1;
}
static void lxsp_restore(void) {
    if(lxsp_phase_on()) {__atomic_store_n(&lxsp_on,0,__ATOMIC_SEQ_CST);lxsp_set(1);}   /* a held calculator runs on */
    __atomic_store_n(&lxsp_delay_ms,0,__ATOMIC_SEQ_CST);
    if(!lxsp_installed) return;
    if(lxsp_route_state()!=1 || lxsp_steer_state()!=1) {lxsp_stage="restore_foreign";return;}
    if(!lxsp_steer_swap(0) || !lxsp_route_swap(0)) {lxsp_stage="restore_busy";return;}
    lxsp_installed=0;lxsp_restored=1;lxsp_stage="restored";   /* the page stays: the worker may be inside it */
}
static int lxsp_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity,"installed=%d restored=%d stage=%s window=0:0 calls=%lld in_frame=0 rows=0 dropped=0 failures=%lld"
        " phase=%d:%lld:%lld:%lld:%lld:%lld xform=0:0 delay=%d:%lld steer=%lld:%lld",
        lxsp_installed,lxsp_restored,lxsp_stage,(long long)lxsp_calls,(long long)lxsp_failures,lxsp_phase_on(),(long long)lxsp_drains,
        (long long)lxsp_jobs,(long long)lxsp_held,(long long)lxsp_timeouts,(long long)lxsp_max_us,lxsp_delay_ms,(long long)lxsp_delayed,
        (long long)lxsp_steer_calls,(long long)lxsp_steer_held);
    return n>0 && (size_t)n<capacity?n:0;
}
static int lua_spline_phase(lua_State *L) {p_lua_pushnumber(L,p_lua_tonumber(L,1)==1.0 && lxsp_phase_arm());return 1;}
static int lua_spline_calc_delay(lua_State *L) {p_lua_pushnumber(L,lxsp_delay_arm(p_lua_tonumber(L,1)));return 1;}
static int lua_spline_calc_watch_status(lua_State *L) {
    char out[400];const int n=accept_ui()?lxsp_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}

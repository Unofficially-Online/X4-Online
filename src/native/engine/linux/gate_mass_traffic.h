/* The mass traffic and region manager passes joined at the gate tick on the Linux X4 9.00 executable: the Linux side
 * of lockstep_mass_traffic.h's 'sync' and 'observe' (native_gate_mass_traffic, native_gate_region_pass) and of
 * native_gate_pass_stride. Included by gate_linux.c only. The mechanism and the Windows evidence are in that header:
 * MoveWorker kicks both passes at the end of every frame and neither is joined natively, so the frame in which a pass's
 * results (traffic, asteroids around the camera, with draws on the pass thread's word) are used depends on thread timing.
 *
 * Linux sites. MoveWorker::Run 0x1212490 releases g_semSimDone at 0x121276a, then kicks mass traffic (task in 0x3e59f60:
 * when its dwords +0x18 and +0x1c are clear, under the XLib::Mutex +0x70 it clears the parked byte +0xa0 and signals
 * +0xd8 when the thread is parked) and the region manager (task in 0x3e50148: 0x12d1680(task, 0), which, when the task
 * has work, +0x118 or +0x11c, clears the parked byte +0x168 under the XLib::Mutex +0x138 and signals +0x1a0), then
 * returns to the frame start, whose wait on g_semSimStart 0x3e59e20 is the call at 0x12125b5 (0x1a23cc0). Each pass
 * thread, when its pass is done, sets its parked byte under its mutex and waits: traffic Run 0x11f94d0 calls
 * 0x1a23f70 (XLib cond, XLib mutex) at 0x11f9735, region Run 0x12ef310 calls pthread_cond_wait at 0x12ef3bf. So a pass
 * kicked at the end of frame N is done once its task's parked byte is set again (Windows: the ready byte).
 *
 * With this, the three calls point (rel32, CAS of the aligned 16 bytes) at jmp stubs on a private page near the image:
 * the frame-start wait is counted (the gate tick then knows the MoveWorker has passed its kicks), and each pass
 * thread's wait, once it returns (a kick), gives the thread the seed the gate set for that pass. 'sync': the gate tick
 * (main thread, before FrameTick) waits for the MoveWorker at its frame-start wait, then for each synced pass to be
 * parked (read under its mutex), and sets the next pass's seed, lockstep_seed(domain, completed tick), Windows' seed;
 * the pass thread copies it into its own word when it wakes, so no other thread's TLS is written. 'observe' keeps the
 * timing and counts how often a pass is still running at the gate tick. Stride N: on the ticks off the stride the gate
 * clears the parked bytes while the threads wait (the kick then has no thread to wake) and sets them back at the next
 * join, as Windows holds its ready bytes. A timeout (the worker 5 s, traffic 5 s, region 120 s) stops the gate (reason
 * 100, as on Windows). Not ported: Zone::Move's TryEnter count of g_MassTrafficLock (Windows' 'skipped' check). */
#include <errno.h>
#define LMT_DOMAIN 0x58344c4d41535354ULL        /* "X4LMASST", MT_DOMAIN */
#define LMT_REGION_DOMAIN 0x58344c5245475053ULL /* "X4LREGPS", MT_REGION_DOMAIN */
#define LMT_STOP_REASON 100u
#define LMT_STRIDE_MAX 16u
#define LMT_WORKER_TIMEOUT_MS 5000u
#define LMT_SITES 3u
enum {LMT_OFF,LMT_SYNC,LMT_OBSERVE};
enum {LMT_SITE_WORKER,LMT_SITE_TRAFFIC,LMT_SITE_REGION};
typedef struct {const char *name;uint32_t call_va,target_va,block_va;} lmt_site;
static const lmt_site lmt_sites[LMT_SITES]={
    {"worker",LX_MT_WORKER_WAIT_CALL_VA,LX_SEM_WAIT_VA,LX_MT_WORKER_WAIT_BLOCK_VA},
    {"traffic",LX_MT_TRAFFIC_WAIT_CALL_VA,LX_CONDVAR_WAIT_VA,LX_MT_TRAFFIC_WAIT_BLOCK_VA},
    {"region",LX_MT_REGION_WAIT_CALL_VA,LX_PTHREAD_COND_WAIT_PLT_VA,LX_MT_REGION_WAIT_BLOCK_VA}};
typedef struct {
    const char *name;uint32_t global,lock,parked;unsigned timeout_ms;uint64_t domain;
    int mode,held;volatile int seed_pending;uint64_t seed;
    uint64_t joins,busy,absent,seeds,applied,wait_us,max_wait_us,total_wait_us;
} lmt_pass;
static lmt_pass lmt_traffic={.name="traffic",.global=LX_FS_MASS_VA,.lock=LX_MT_TRAFFIC_LOCK,.parked=LX_MT_TRAFFIC_PARKED,.timeout_ms=5000u,.domain=LMT_DOMAIN};
static lmt_pass lmt_region={.name="region",.global=LX_FS_REGION_VA,.lock=LX_MT_REGION_LOCK,.parked=LX_MT_REGION_PARKED,.timeout_ms=120000u,.domain=LMT_REGION_DOMAIN};
static int lmt_installed,lmt_restored,lmt_patched,lmt_trace_enabled;
static volatile int lmt_active;
static unsigned char *lmt_page;
static unsigned char lmt_site_done[LMT_SITES];
static volatile int64_t lmt_failures,lmt_worker_waits,lmt_worker_wakes;
static const char *lmt_stage="idle";
static unsigned lmt_stride=1;
static uint64_t lmt_stride_skips;
/* The engine's XLib::Mutex Lock/Unlock; a test sets stand-ins. */
static void (*lmt_lock)(void *),(*lmt_unlock)(void *);
static int lmt_fail(const char *why) {lmt_stage=why;InterlockedIncrement64(&lmt_failures);return 0;}
static unsigned char *lmt_task(const lmt_pass *p) {unsigned char *t=NULL;return gate_read_local(LX(p->global),&t,8)?t:NULL;}
static void lmt_mutex(void *m,int lock) {
    void (*f)(void *)=lock?(lmt_lock?lmt_lock:(void(*)(void*))(void*)LX(LX_MUTEX_LOCK_VA)):(lmt_unlock?lmt_unlock:(void(*)(void*))(void*)LX(LX_MUTEX_UNLOCK_VA));
    f(m);
}
static int lmt_parked(const lmt_pass *p,unsigned char *task) {
    lmt_mutex(task+p->lock,1);unsigned char v=task[p->parked];lmt_mutex(task+p->lock,0);return v!=0;
}
static void lmt_parked_set(const lmt_pass *p,unsigned char *task,unsigned char v) {lmt_mutex(task+p->lock,1);task[p->parked]=v;lmt_mutex(task+p->lock,0);}
static int lmt_worker_parked(void) {return InterlockedCompareExchange64(&lmt_worker_waits,0,0)>InterlockedCompareExchange64(&lmt_worker_wakes,0,0);}
/* ---- the three redirected calls ---- */
typedef uint64_t (*lmt_sem_wait_fn)(void *);
typedef uint64_t (*lmt_cond_wait_fn)(void *,void *);
/* Optional private diagnostic uses the existing owned hook; absent by default. */
extern unsigned long x4_worker_wait_trace_enter(void *) __attribute__((weak, visibility("default")));
extern void x4_worker_wait_trace_return(void *, unsigned long, uint64_t) __attribute__((weak, visibility("default")));
extern int x4mp_semaphore_observer_movement_scope(unsigned) __attribute__((weak, visibility("default")));
extern int x4mp_semaphore_observer_checkpoint(uint64_t) __attribute__((weak, visibility("default")));
static unsigned lmt_semaphore_observer_enabled;
static uint64_t lmt_worker_wait(void *sem) {
    int incoming_errno = errno;
    unsigned long observation = 0;
    if (lmt_trace_enabled)
        observation = x4_worker_wait_trace_enter(sem);
    errno = incoming_errno;
    int counted=sem==(void*)LX(LX_FS_SIM_START_VA);
    if(counted) InterlockedIncrement64(&lmt_worker_waits);
    if (counted && lmt_semaphore_observer_enabled) (void)x4mp_semaphore_observer_movement_scope(1);
    errno = incoming_errno;
    uint64_t r=((lmt_sem_wait_fn)(void*)LX(LX_SEM_WAIT_VA))(sem);
    int outgoing_errno = errno;
    if (counted && lmt_semaphore_observer_enabled) (void)x4mp_semaphore_observer_movement_scope(0);
    if(counted) InterlockedIncrement64(&lmt_worker_wakes);
    if (observation) x4_worker_wait_trace_return(sem, observation, r);
    errno = outgoing_errno;
    return r;
}
/* On the pass thread, its wait just returned (a kick): the seed the gate set for this pass, into its own word. */
static void lmt_woken(lmt_pass *p) {
    if(!__atomic_exchange_n(&p->seed_pending,0,__ATOMIC_ACQ_REL)) return;
    uint64_t *word=gate_tls_rng(gate_tls_self());
    if(word) {*word=p->seed;++p->applied;}
}
static uint64_t lmt_traffic_wait(void *cond,void *mutex) {
    uint64_t r=((lmt_cond_wait_fn)(void*)LX(LX_CONDVAR_WAIT_VA))(cond,mutex);lmt_woken(&lmt_traffic);return r;
}
static uint64_t lmt_region_wait(void *cond,void *mutex) {
    uint64_t r=((lmt_cond_wait_fn)(void*)LX(LX_PTHREAD_COND_WAIT_PLT_VA))(cond,mutex);lmt_woken(&lmt_region);return r;
}
static void *lmt_entry(unsigned i) {
    return i==LMT_SITE_WORKER?(void*)lmt_worker_wait:i==LMT_SITE_TRAFFIC?(void*)lmt_traffic_wait:(void*)lmt_region_wait;
}
/* The aligned 16 bytes holding site i's rel32, with the rel32 leading to target. */
static int lmt_block(unsigned i,const void *target,unsigned __int128 *out) {
    const lmt_site *s=&lmt_sites[i];unsigned at=s->call_va+1-s->block_va;
    if(at>12u) return 0;
    unsigned char b[16];memcpy(b,LX(s->block_va),16);
    int32_t d=(int32_t)((intptr_t)target-(intptr_t)(LX(s->call_va)+5));memcpy(b+at,&d,4);memcpy(out,b,16);
    return 1;
}
/* 0 original, 1 ours, -1 anything else. */
static int lmt_site_state(unsigned i) {
    const lmt_site *s=&lmt_sites[i];unsigned char op;int32_t d;
    if(!gate_read_local(LX(s->call_va),&op,1) || op!=0xe8 || !gate_read_local(LX(s->call_va)+1,&d,4)) return -1;
    unsigned char *to=LX(s->call_va)+5+d;
    if(to==LX(s->target_va)) return 0;
    if(lmt_page && to==lmt_page+0x20u*i) return 1;
    return -1;
}
static int lmt_swap(unsigned i,const void *from,const void *to) {
    unsigned __int128 expected,desired;
    return lmt_block(i,from,&expected) && lmt_block(i,to,&desired) && gate_code_swap128(LX(lmt_sites[i].block_va),expected,desired);
}
/* jmp [rip+0]; abs64, one per site, 0x20 apart. */
static void lmt_write_stubs(unsigned char *p) {
    for(unsigned i=0;i<LMT_SITES;i++) {unsigned char *s=p+0x20u*i;void *t=lmt_entry(i);s[0]=0xff;s[1]=0x25;memset(s+2,0,4);memcpy(s+6,&t,8);}
}
static int lmt_stubs_ok(void) {
    if(!lmt_page) return 0;
    for(unsigned i=0;i<LMT_SITES;i++) {const unsigned char *s=lmt_page+0x20u*i;void *t;memcpy(&t,s+6,8);if(s[0]!=0xff || s[1]!=0x25 || t!=lmt_entry(i)) return 0;}
    return 1;
}
static const lx_site *lmt_site_named(const char *name) {
    for(unsigned i=0;i<gate_site_count;i++) if(!strcmp(gate_sites[i].name,name)) return &gate_sites[i];
    return NULL;
}
/* Pins: the kick and park code (the sites' own bytes read as the originals) and the three call targets. */
static int lmt_pins(void) {
    static const char *const names[]={"fs_move_worker_frame_end","fs_mass_park","fs_region_park","mt_region_kick","fs_sem_wait","fs_condvar_wait"};
    for(unsigned k=0;k<sizeof(names)/sizeof(*names);k++) {
        const lx_site *s=lmt_site_named(names[k]);unsigned char copy[0x200];
        /* The optional observer owns this provider's entry. Authenticate its
         * full original view through the shared verifier, including all four
         * provider hooks; the local callsite reconstruction below is unrelated. */
        if (s && s->va == LX_SEM_WAIT_VA) {
            if (!site_ok(s)) return 0;
            continue;
        }
        if(!s || s->length>sizeof(copy) || !gate_read_local(LX(s->va),copy,s->length)) return 0;
        for(unsigned i=0;i<LMT_SITES;i++) {
            uint32_t c=lmt_sites[i].call_va;if(c+5<=s->va || c>=s->va+s->length) continue;
            if(c<s->va || c+5>s->va+s->length || lmt_site_state(i)<0) return 0;
            int32_t d=(int32_t)((intptr_t)LX(lmt_sites[i].target_va)-(intptr_t)(LX(c)+5));memcpy(copy+(c-s->va)+1,&d,4);
        }
        if(!player_tail_original_view(s->va,copy,s->length,player_recurring_tail_callback) || !hash_ok(copy,s->length,s->sha256)) return 0;
    }
    return 1;
}
static int lmt_install(void) {
    if(lmt_installed) return 1;
    const char *semaphore_observer = getenv("X4MP_SEMAPHORE_OBSERVER");
    lmt_semaphore_observer_enabled = semaphore_observer && !strcmp(semaphore_observer, "1");
    if (semaphore_observer) {
        Dl_info observer_owner = {0}, checkpoint_owner = {0}, information_owner = {0};
        void *information = dlsym(RTLD_DEFAULT, "x4mp_semaphore_observer_info");
        if (!lmt_semaphore_observer_enabled || !x4mp_semaphore_observer_movement_scope ||
            !x4mp_semaphore_observer_checkpoint ||
            !player_semaphore_pinned_provider((void *)x4mp_semaphore_observer_movement_scope, &observer_owner) ||
            !dladdr((void *)x4mp_semaphore_observer_checkpoint, &checkpoint_owner) ||
            !information || !dladdr(information, &information_owner) ||
            observer_owner.dli_fbase != checkpoint_owner.dli_fbase ||
            observer_owner.dli_fbase != information_owner.dli_fbase) {
            lmt_stage="semaphore_observer";return 0;
        }
    }
    const char *trace = getenv("X4_DEV_WORKER_WAIT_OBSERVE");
    lmt_trace_enabled = 0;
    if (trace) {
        if (strcmp(trace, "1") || !x4_worker_wait_trace_enter || !x4_worker_wait_trace_return) {
            lmt_stage="trace";return 0;
        }
        lmt_trace_enabled = 1;
    }
    if(!lmt_pins()) {lmt_stage="pins";return 0;}
    for(unsigned i=0;i<LMT_SITES;i++) if(lmt_site_state(i)!=0) {lmt_stage="site";return 0;}
    if(!lmt_page) lmt_page=gate_near_page(LX(LX_MT_TRAFFIC_WAIT_CALL_VA));
    if(!lmt_page) {lmt_stage="page";return 0;}
    for(unsigned i=0;i<LMT_SITES;i++) {
        intptr_t d=(intptr_t)(lmt_page+0x20u*i)-(intptr_t)(LX(lmt_sites[i].call_va)+5);
        if(d<INT32_MIN || d>INT32_MAX) {lmt_stage="page";return 0;}
    }
    if(mprotect(lmt_page,GATE_PAGE,PROT_READ|PROT_WRITE)) {lmt_stage="page";return 0;}
    memset(lmt_page,0xcc,GATE_PAGE);lmt_write_stubs(lmt_page);
    if(mprotect(lmt_page,GATE_PAGE,PROT_READ|PROT_EXEC)) {lmt_stage="page";return 0;}
    lmt_failures=lmt_worker_waits=lmt_worker_wakes=0;lmt_restored=0;
    for(unsigned i=0;i<LMT_SITES;i++) {
        if(!lmt_swap(i,LX(lmt_sites[i].target_va),lmt_page+0x20u*i)) {
            for(unsigned j=0;j<i;j++) if(lmt_site_done[j] && lmt_swap(j,lmt_page+0x20u*j,LX(lmt_sites[j].target_va))) lmt_site_done[j]=0;
            lmt_stage="cas";return 0;
        }
        lmt_site_done[i]=1;
    }
    __atomic_store_n(&lmt_active,1,__ATOMIC_SEQ_CST);
    lmt_patched=1;lmt_installed=1;lmt_stage="armed";return 1;
}
static void lmt_pass_reset(lmt_pass *p) {
    p->mode=LMT_OFF;p->held=0;p->seed_pending=0;p->seed=0;
    p->joins=p->busy=p->absent=p->seeds=p->applied=p->wait_us=p->max_wait_us=p->total_wait_us=0;
}
/* Before the first admitted frame, on the MoveWorker; a loading-UI reload repeats the same mode. */
static int lmt_arm_pass(lmt_pass *p,int mode) {
    if(p->mode!=LMT_OFF) return stats.active && mode==p->mode;
    if((mode!=LMT_SYNC && mode!=LMT_OBSERVE) || !stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!lmt_install()) return 0;
    lmt_pass_reset(p);p->mode=mode;
    if(mode==LMT_SYNC) {p->seed=lockstep_seed(p->domain,0,0,0,0,0);__atomic_store_n(&p->seed_pending,1,__ATOMIC_RELEASE);}
    return 1;
}
static int lmt_stride_arm(unsigned n) {
    int synced=lmt_traffic.mode==LMT_SYNC || lmt_region.mode==LMT_SYNC;
    if(lmt_stride!=1 || stats.issued) return n==lmt_stride && synced;
    if(!n || n>LMT_STRIDE_MAX || !synced || !stats.active || GetCurrentThreadId()!=ui_thread) return 0;
    lmt_stride=n;return 1;
}
/* One pass at the gate tick (the MoveWorker parked when worker != 0). */
static int lmt_join_pass(lmt_pass *p,uint64_t tick,int worker) {
    if(p->mode==LMT_OFF) return 1;
    unsigned char *task=lmt_task(p);
    if(!task) {++p->absent;return 1;}
    if(p->mode==LMT_OBSERVE) {if(worker && !lmt_parked(p,task)) ++p->busy;return 1;}
    uint64_t started=GetTickCount64();struct timespec t0,t1;clock_gettime(CLOCK_MONOTONIC,&t0);int busy=0;
    while(!lmt_parked(p,task)) {
        busy=1;
        if(GetTickCount64()-started>=p->timeout_ms) return lmt_fail(p==&lmt_region?"region_pass":"pass");
        if(GetTickCount64()-started>=2) {struct timespec s={0,1000000};nanosleep(&s,NULL);} else sched_yield();
    }
    clock_gettime(CLOCK_MONOTONIC,&t1);
    p->wait_us=(uint64_t)((t1.tv_sec-t0.tv_sec)*1000000+(t1.tv_nsec-t0.tv_nsec)/1000);
    p->total_wait_us+=p->wait_us;if(p->wait_us>p->max_wait_us) p->max_wait_us=p->wait_us;
    p->busy+=busy;++p->joins;
    p->seed=lockstep_seed(p->domain,tick,0,0,0,0);__atomic_store_n(&p->seed_pending,1,__ATOMIC_RELEASE);++p->seeds;
    return 1;
}
static int lmt_unhold(lmt_pass *p) {
    unsigned char *task;if(!p->held || !(task=lmt_task(p))) return 0;
    lmt_parked_set(p,task,1);p->held=0;return 1;
}
static void lmt_hold(lmt_pass *p) {
    unsigned char *task;if(p->mode==LMT_SYNC && (task=lmt_task(p))) {lmt_parked_set(p,task,0);p->held=1;}
}
/* Gate tick, main thread (the frame just run is complete, before FrameTick). 0 fails closed. */
static int lmt_join(uint64_t tick) {
    if(!__atomic_load_n(&lmt_active,__ATOMIC_SEQ_CST)) return 1;
    int need=(lmt_traffic.mode==LMT_SYNC && lmt_task(&lmt_traffic)) || (lmt_region.mode==LMT_SYNC && lmt_task(&lmt_region));
    uint64_t started=GetTickCount64();
    while(need && !lmt_worker_parked()) {
        if(GetTickCount64()-started>=LMT_WORKER_TIMEOUT_MS) return lmt_fail("worker");
        sched_yield();
    }
    int worker=lmt_worker_parked();
    if(lmt_unhold(&lmt_traffic)+lmt_unhold(&lmt_region)) ++lmt_stride_skips;
    if(!lmt_join_pass(&lmt_traffic,tick,worker) || !lmt_join_pass(&lmt_region,tick,worker)) return 0;
    return 1;
}
/* Main thread, after quiet-point observers and before the next FrameTick.
 * A stride-held parked byte is zero even though its task is asleep. Setting it
 * during join would make the subsequent fork checks mistake a skipped pass for
 * a running thread. Keep the real parked state until those checks finish. */
static int lmt_prepare_next(uint64_t tick) {
    if (!__atomic_load_n(&lmt_active, __ATOMIC_SEQ_CST) ||
        lmt_stride <= 1 || !( (tick + 1) % lmt_stride)) return 1;
    int needed = (lmt_traffic.mode == LMT_SYNC && lmt_task(&lmt_traffic)) ||
                 (lmt_region.mode == LMT_SYNC && lmt_task(&lmt_region));
    if (needed && !lmt_worker_parked()) return lmt_fail("stride_worker_not_parked");
    if (lmt_worker_parked()) {
        lmt_hold(&lmt_traffic);
        lmt_hold(&lmt_region);
    }
    return 1;
}
static int lmt_owned(void) {
    if(!lmt_patched) return 1;
    for(unsigned i=0;i<LMT_SITES;i++) if(lmt_site_state(i)!=1) return 0;
    return lmt_stubs_ok();
}
/* Main-thread tick check; any failure stops the gate with reason 100. */
static int lmt_check(void) {
    if(InterlockedCompareExchange64(&lmt_failures,0,0)) return 0;
    if(!lmt_owned()) return lmt_fail("site_changed");
    if(!lmt_pins()) return lmt_fail("block_changed");
    return 1;
}
static void lmt_restore(void) {
    __atomic_store_n(&lmt_active,0,__ATOMIC_SEQ_CST);
    lmt_unhold(&lmt_traffic);lmt_unhold(&lmt_region);
    if(!lmt_installed) return;
    if(lmt_patched) {
        int all=1;
        for(unsigned i=0;i<LMT_SITES;i++) if(lmt_site_done[i]) {
            int state=lmt_site_state(i);
            if((state==1 && lmt_swap(i,lmt_page+0x20u*i,LX(lmt_sites[i].target_va))) || state==0) lmt_site_done[i]=0;else all=0;
        }
        if(!all) return;
        lmt_patched=0;
    }
    lmt_restored=1;lmt_installed=0;
}
static const char *lmt_mode_name(int mode) {return mode==LMT_SYNC?"sync":mode==LMT_OBSERVE?"observe":"off";}
static int lmt_pass_status(const lmt_pass *p,char *out,size_t capacity) {
    int n=snprintf(out,capacity,"mode=%s armed=%d installed=%d frame=%llu joins=%llu busy=%llu absent=%llu wait_us=%llu max_wait_us=%llu "
        "total_wait_us=%llu seeds=%llu applied=%llu worker=%lld/%lld restored=%d stage=%s failures=%lld stride=%u stride_skips=%llu",
        lmt_mode_name(p->mode),lmt_active,lmt_installed,(unsigned long long)stats.issued,(unsigned long long)p->joins,(unsigned long long)p->busy,
        (unsigned long long)p->absent,(unsigned long long)p->wait_us,(unsigned long long)p->max_wait_us,(unsigned long long)p->total_wait_us,
        (unsigned long long)p->seeds,(unsigned long long)p->applied,(long long)lmt_worker_waits,(long long)lmt_worker_wakes,lmt_restored,lmt_stage,
        (long long)lmt_failures,lmt_stride,(unsigned long long)lmt_stride_skips);
    return n>0 && (size_t)n<capacity?n:0;
}
static int lmt_mode_arg(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    return mode && length==4 && !memcmp(mode,"sync",4)?LMT_SYNC:mode && length==7 && !memcmp(mode,"observe",7)?LMT_OBSERVE:LMT_OFF;
}
static int lua_mass_traffic(lua_State *L) {int m=lmt_mode_arg(L);p_lua_pushnumber(L,m!=LMT_OFF && lmt_arm_pass(&lmt_traffic,m));return 1;}
static int lua_region_pass(lua_State *L) {int m=lmt_mode_arg(L);p_lua_pushnumber(L,m!=LMT_OFF && lmt_arm_pass(&lmt_region,m));return 1;}
static int lua_pass_stride(lua_State *L) {
    double n=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(n) && n>=1 && n<=LMT_STRIDE_MAX && floor(n)==n && lmt_stride_arm((unsigned)n));return 1;
}
static int lua_mass_traffic_status(lua_State *L) {
    char out[1024];int n=accept_ui()?lmt_pass_status(&lmt_traffic,out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_region_pass_status(lua_State *L) {
    char out[1024];int n=accept_ui()?lmt_pass_status(&lmt_region,out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}

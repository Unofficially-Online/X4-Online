/* Spline routes consumed in the first update after their request on every client, on the Linux X4 9.00
 * executable: the Linux side of lockstep_spline.h's native_gate_spline_join_all (cause B of the Windows long
 * runs, see there). Included by gate_linux.c only. The fixture trace and obstacle refresh of that header need
 * the battle scope and are not ported; this is the join for every spline ship of the clock-mode world.
 *
 * Linux sites. The spline model's update (Windows 0xeb7230, slot +0x70 of 0x2c4f480) is 0x1363de0, reached only
 * through slot +0x78 of the model's vtable 0x3b5d6b8: (model rdi, slot rsi, dt xmm0, time xmm1, cheap dl,
 * player cl), Windows' arguments in System V order. Its pending route requests are a libstdc++ std::deque of
 * request pointers at +0x468 (Windows' MSVC deque at +0x428): the update reads start.cur +0x478 against
 * finish.cur +0x498 and takes the front as *start.cur, then reads the request's status +0xb8 (Windows +0xa8)
 * and its flag bytes +0xbc.. under the XLib::Mutex 0x3e76960 (Windows' critical section 0x3d74938), taken with
 * 0x1a23650 and released with 0x1a236a0; status 0 means not yet published, and the update then flies its
 * provisional segment. The calculator worker (vtable 0x3b28b38, Run 0x10b0b30 = Windows 0x1099650) is made by
 * 0x2001180 and kept in 0x3e5b850; Run sets its running byte +0xb8 (Windows +0x58), computes a request
 * (0x1374d90 = Windows 0xec0cb0) and publishes it with xchg [request+0xb8],1 under the same mutex.
 *
 * With this, the update slot points at lsj_update: in admitted frames, once spline_join_all is armed, it waits
 * (bounded, SPLINE_JOIN_ALL_TIMEOUT ms) until the front request is published, then runs the native update. The
 * wait copies nothing, cancels nothing and runs no job, as on Windows. A timeout, an unknown status, an
 * unreadable deque or a worker that is not running counts a failure, which stops the gate (reason 58, as on
 * Windows). Needs serial pathing (the pathing owner controls the deque). Boot option 2^35
 * (native_gate_spline_join) installs the slot swap; gate.spline_join_all() arms the wait. */
#define LSJ_TIMEOUT 2000u /* SPLINE_JOIN_ALL_TIMEOUT */
#define LSJ_STOP_REASON 58u
typedef void (*lsj_update_fn)(void *,void *,float,double,unsigned char,unsigned char);
typedef void (*lsj_mutex_fn)(void *);
static int lsj_installed,lsj_restored,lsj_all;
static volatile int64_t lsj_calls,lsj_joins,lsj_waits,lsj_timeouts,lsj_failures;
static const char *lsj_stage="idle";
/* The engine's XLib::Mutex Lock/Unlock (0x1a23650/0x1a236a0); a test sets stand-ins. */
static lsj_mutex_fn lsj_lock,lsj_unlock;
typedef struct {uint64_t count;uint32_t status;void *front;} lsj_front_t;
/* The front request and its status, read as the native update reads it (count: 0 empty, 1 not empty). */
static int lsj_front(void *model,lsj_front_t *f) {
    void *cur=NULL,*finish=NULL;
    f->count=0;f->status=0;f->front=NULL;
    if(!read_local((unsigned char*)model+LX_SPLINE_DEQUE_CUR,&cur,8) || !read_local((unsigned char*)model+LX_SPLINE_DEQUE_FINISH,&finish,8))
        return 0;
    if(cur==finish) return 1;
    f->count=1;
    if(!cur || !read_local(cur,&f->front,8) || !f->front) return 0;
    void *mutex=LX(LX_SPLINE_RESULT_MUTEX_VA);
    (lsj_lock?lsj_lock:(lsj_mutex_fn)(void*)LX(LX_MUTEX_LOCK_VA))(mutex);
    int ok=read_local((unsigned char*)f->front+LX_SPLINE_REQUEST_STATUS,&f->status,4);
    (lsj_unlock?lsj_unlock:(lsj_mutex_fn)(void*)LX(LX_MUTEX_UNLOCK_VA))(mutex);
    return ok;
}
static int lsj_worker_running(void) {
    void *worker=NULL,*vtable=NULL;unsigned char running=0;
    return read_local(LX(LX_SPLINE_WORKER_VA),&worker,8) && worker && read_local(worker,&vtable,8) &&
           vtable==LX(LX_SPLINE_WORKER_VTABLE) && read_local((unsigned char*)worker+LX_SPLINE_WORKER_RUNNING,&running,1) && running==1;
}
static void lsj_sleep_ms(void) {struct timespec t={0,1000000};nanosleep(&t,NULL);}
/* Windows spline_join_core: 1 when nothing is pending or the front is published, 0 on a failure. */
static int lsj_join(void *model,unsigned timeout,int *waited) {
    lsj_front_t f;*waited=0;
    if(!lsj_front(model,&f)) return 0;
    if(!f.count) return 1;
    InterlockedIncrement64(&lsj_joins);
    if(f.status>1) return 0;
    if(f.status) return 1;
    if(!lsj_worker_running()) return 0;
    *waited=1;InterlockedIncrement64(&lsj_waits);
    uint64_t started=GetTickCount64();
    while(!f.status) {
        if(GetTickCount64()-started>=timeout) {InterlockedIncrement64(&lsj_timeouts);return 0;}
        lsj_sleep_ms();
        if(!lsj_front(model,&f) || !f.count || f.status>1) return 0;
    }
    return 1;
}
/* The battle fixture's spline models (Windows spline_observe, boot options 2^33 spline_diag and 2^34 spline_refresh):
 * the model's Agent +0x8 (Windows +0x10) is a fixture's (gate_battle.h) and holds it (+0xf30): with refresh the
 * obstacle publication time +0x148 (Windows +0x108; the update's avoidance query 0x13693a3 takes it, with the vector
 * +0x130, Windows +0xf0) goes stale and the update is a full one. The spline state (Windows +0xd0, four floats) is
 * +0x110 (the update's float stores, as Windows' at +0xd0). The model fields from +0xd0 on sit 0x40 above Windows'. */
#define LSJ_MODEL_AGENT 0x8u
#define LSJ_MODEL_STATE 0x110u
#define LSJ_MODEL_AVOIDANCE_TIME 0x148u
static unsigned lxb_fixture_agent(void *self); /* gate_battle.h */
static int lsj_diag,lsj_refresh;
static volatile int64_t lsj_fixture_calls,lsj_refreshes;
static lsj_update_fn lsj_original_override; /* tests */
static int lsj_state(void *model,void *agent,uint32_t values[4]) {
    void *vtable=NULL,*owner=NULL;
    if(!read_local(model,&vtable,8) || vtable!=(void*)LX(LX_SPLINE_VTABLE) || !read_local((unsigned char*)model+LSJ_MODEL_AGENT,&owner,8) ||
       owner!=agent || !read_local((unsigned char*)model+LSJ_MODEL_STATE,values,16)) return 0;
    for(unsigned i=0;i<4;i++) {float f;memcpy(&f,values+i,4);if(!isfinite(f)) return 0;}
    return 1;
}
static void lsj_update(void *self,void *slot,float dt,double time,unsigned char cheap,unsigned char player) {
    lsj_update_fn original=lsj_original_override?lsj_original_override:(lsj_update_fn)(void*)LX(LX_SPLINE_UPDATE_VA);
    const int gated=stats.active && boot_phase==3;
    if(lsj_all && gated) {
        int waited=0;
        if(!lsj_join(self,LSJ_TIMEOUT,&waited)) InterlockedIncrement64(&lsj_failures);
    }
    InterlockedIncrement64(&lsj_calls);
    void *agent=NULL;
    if(gated && lsj_diag && read_local((unsigned char*)self+LSJ_MODEL_AGENT,&agent,8) && lxb_fixture_agent(agent)) {
        void *model=NULL;
        if(!read_local((unsigned char*)agent+0xf30u,&model,8) || model!=self) InterlockedIncrement64(&lsj_failures);
        else {
            InterlockedIncrement64(&lsj_fixture_calls);
            if(lsj_refresh) {const double stale=-1.0;memcpy((unsigned char*)self+LSJ_MODEL_AVOIDANCE_TIME,&stale,8);InterlockedIncrement64(&lsj_refreshes);}
            original(self,slot,dt,time,lsj_refresh?0:cheap,player);return;
        }
    }
    original(self,slot,dt,time,cheap,player);
}
static const lx_site *lsj_site(const char *name) {
    for(unsigned i=0;i<gate_site_count;i++) if(!strcmp(gate_sites[i].name,name)) return &gate_sites[i];
    return NULL;
}
static int lsj_pins(void) {
    void *slot=NULL,*run=NULL;
    return site_ok(lsj_site("spline_update_head")) && site_ok(lsj_site("spline_worker_run")) &&
           site_ok(lsj_site("spline_worker_make")) && site_ok(lsj_site("x4_mutex_lock")) && site_ok(lsj_site("x4_mutex_unlock")) &&
           gate_read_local(LX(LX_SPLINE_WORKER_VTABLE+0x10u),&run,8) && run==(void*)(uintptr_t)LX_SPLINE_WORKER_RUN_VA &&
           gate_read_local(LX(LX_SPLINE_SLOT_VA),&slot,8) && (slot==LX(LX_SPLINE_UPDATE_VA) || slot==(void*)lsj_update);
}
static void *lsj_slot(void) {void *value=NULL;return gate_read_local(LX(LX_SPLINE_SLOT_VA),&value,8)?value:NULL;}
/* Boot options 2^33 (spline_diag), 2^35 (spline_join), at boot (before the load): the slot swap; needs serial pathing. */
static int lsj_install(void) {
    if(lsj_installed) return 1;
    if(!(boot_options&1)) {lsj_stage="needs";return 0;}
    lsj_diag=(boot_options&8589934592ull)!=0;lsj_refresh=(boot_options&17179869184ull)!=0;
    if(lsj_refresh && (!lsj_diag || !(boot_options&512ull))) {lsj_stage="needs";return 0;} /* Windows: refresh needs diag and agent_full */
    if(!lsj_pins() || lsj_slot()!=LX(LX_SPLINE_UPDATE_VA)) {lsj_stage="pins";return 0;}
    lsj_calls=lsj_joins=lsj_waits=lsj_timeouts=lsj_failures=0;lsj_restored=0;lsj_all=0;
    if(!gate_data_swap64(LX(LX_SPLINE_SLOT_VA),(uint64_t)(uintptr_t)LX(LX_SPLINE_UPDATE_VA),(uint64_t)(uintptr_t)lsj_update)) {lsj_stage="cas";return 0;}
    lsj_installed=1;lsj_stage="installed";return 1;
}
/* Before the first admitted frame, on the MoveWorker: the wait for every spline update. */
static int lsj_arm_all(void) {
    if(lsj_all) return 1;
    if(!lsj_installed || !stats.active || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    lsj_all=1;lsj_stage="armed";return 1;
}
/* Main-thread tick check; a failure stops the gate with reason 58. */
static int lsj_check(void) {
    if(InterlockedCompareExchange64(&lsj_failures,0,0)) {lsj_stage="failed";return 0;}
    if(lsj_slot()!=(void*)lsj_update || !lsj_pins()) {lsj_stage="slot_changed";InterlockedIncrement64(&lsj_failures);return 0;}
    return 1;
}
static void lsj_restore(void) {
    if(!lsj_installed) return;
    if(gate_data_swap64(LX(LX_SPLINE_SLOT_VA),(uint64_t)(uintptr_t)lsj_update,(uint64_t)(uintptr_t)LX(LX_SPLINE_UPDATE_VA))) {lsj_installed=0;lsj_restored=1;}
}
static int lsj_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d all=%d frame=%llu calls=%lld requests=%lld waits=%lld timeouts=%lld failures=%lld restored=%d stage=%s",
        lsj_installed,lsj_all,(unsigned long long)stats.issued,(long long)lsj_calls,(long long)lsj_joins,(long long)lsj_waits,
        (long long)lsj_timeouts,(long long)lsj_failures,lsj_restored,lsj_stage);
    return n>0 && (size_t)n<capacity?n:0;
}
static int lua_spline_join_all(lua_State *L) {p_lua_pushnumber(L,lsj_arm_all());return 1;}
/* native_gate_spline_diag's trace row (logged, not hashed). */
static int lua_spline(lua_State *L) {
    double actor=p_lua_tonumber(L,1);char out[160];
    /* As Windows: still readable after the restore (the probe's final SPLINE rows come after the stop). */
    if(!accept_ui() || (!lsj_installed && !lsj_restored) || !isfinite(actor) || actor<1 || actor>12 || floor(actor)!=actor) {p_lua_pushnil(L);return 1;}
    int n=snprintf(out,sizeof(out),"fixture_calls=%lld refreshes=%lld refresh=%d",(long long)lsj_fixture_calls,(long long)lsj_refreshes,lsj_refresh);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_spline_join_all_status(lua_State *L) {
    char out[384];int n=accept_ui()?lsj_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}

/* Hot join, the joiner's pass of T (diagnostic native_gate_join_passes_admit, default off; reports/LOCKSTEP-JOIN-IN-WINDOW-
 * 20261104.txt section 15). The session's frame T+1 starts from what the gate tick after frame T joined: the synced passes
 * kicked at the end of frame T (lockstep_mass_traffic.h: mass traffic and the region manager, seeded with key(T-1), joined
 * with key(T) seeded for the next), run with the stand-in cameras of T (lockstep_region_camera.h), and its pathing pass reads
 * each leader's +0xf7b from the copy taken before it (lockstep_travel_snapshot.h, the read moved after the first joined pass).
 * A joiner admits T+1 straight after its load: its last loading frame's passes ran with the local camera and unseeded and are
 * not joined (they run into T+1), its view points latch at T+1 for the first time (with the first latch's attention refresh,
 * which the session made long before), its stand-ins and sites follow after T+2, and its first pathing pass reads the live
 * +0xf7b (j1-141..143: REGION_PASS, MASS_TRAFFIC and TRAVEL_SNAPSHOT apart at 634; actor 5 parts at 651).
 *
 * jpass_admit(T), lockstep_join.h's join_admit_passes: join_admit calls it on the main thread once the session's T is
 * published, the MoveWorker parked at its frame start (ui_thread is the MoveWorker: nothing of a frame runs meanwhile):
 *  1. the loading frame's synced passes waited out (ready bytes), each seeded for the pass of T with key(T-1), as the
 *     session's join after T-1 seeded it;
 *  2. the slot ships resolved as the pilot pass resolves them, the view points latched (data only: no attention refresh, so
 *     the latch at T+1 finds them unchanged, as the session's does), the stand-ins written (a stand-in camera is attached to
 *     its view point object, so the same objects give the same cameras; no view scenes, field fill or shape latch from this
 *     thread) and the region camera's pending sites installed;
 *  3. the travel snapshot taken and its read moved to the padding (the pathing thread is idle);
 *  4. traffic and region kicked as MoveWorker::Run kicks them (0xef4777, 0xef47d4: the task's conditions, its lock, the ready
 *     byte, SetEvent on a parked pass thread), on the pass stride's ticks only, then joined as the gate tick after T joins
 *     them (mt_join: the draws counted, key(T) seeded for T+1, the stride's hold for T+1).
 * A module not armed is skipped. 0 refuses the admission (JOIN_WHY_CAPTURE "passes"); jpass_why names the step. */
static int jpass_armed,jpass_done;
static const char *jpass_why="-";
static uint64_t jpass_wait_us,jpass_kicks,jpass_steps;
static int jpass_fail(const char *why) {jpass_why=why;return 0;}
/* A synced pass's ready byte set (its last pass done), bounded. */
static int jpass_wait_ready(mt_pass *p) {
    unsigned char *task=mt_task(p);
    if(p->mode!=MT_SYNC || !task) return 1;
    const ULONGLONG started=GetTickCount64();
    while(!mt_ready(p,task)) {
        if(GetTickCount64()-started>=p->timeout_ms) return 0;
        Sleep(1);
    }
    return 1;
}
/* MoveWorker::Run's kicks, from this thread with the MoveWorker parked: the same conditions, lock, ready byte and wake. */
static void jpass_kick_traffic(void) {
    unsigned char *task=mt_task(&mt_traffic);int32_t a=0,b=0,c=0;
    if(mt_traffic.mode!=MT_SYNC || !task || !read_local(task+0x18,&a,4) || !read_local(task+0x1c,&b,4) || !read_local(task+0x20,&c,4)) return;
    if((a || b) && !c) return;                     /* 0xef4782..0xef4794 */
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(task+MT_TASK_LOCK);
    EnterCriticalSection(lock);
    if(task[MT_READY]) {
        task[MT_READY]=0;
        int32_t parked=0;HANDLE wake=NULL;
        if(read_local(task+0xa0,&parked,4) && parked && read_local(task+0xa8,&wake,8) && wake) SetEvent(wake);
        ++jpass_kicks;
    }
    LeaveCriticalSection(lock);
}
static void jpass_kick_region(void) {
    unsigned char *task=mt_task(&mt_region);int32_t a=0,b=0,busy=0;
    if(mt_region.mode!=MT_SYNC || !task || !read_local(task+0xf8,&a,4) || !read_local(task+0xfc,&b,4)) return;
    if(!a && !b) return;                           /* 0xef47db..0xef47ef: no work */
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(task+MT_REGION_LOCK);
    EnterCriticalSection(lock);
    if(read_local(task+0x144,&busy,4) && !busy && task[MT_REGION_READY]) {
        task[MT_REGION_READY]=0;
        int32_t parked=0;HANDLE wake=NULL;
        if(read_local(task+0x158,&parked,4) && parked && read_local(task+0x160,&wake,8) && wake) SetEvent(wake);
        ++jpass_kicks;
    }
    LeaveCriticalSection(lock);
}
static int jpass_admit(uint64_t t) {
    LARGE_INTEGER t0,t1,frequency;QueryPerformanceCounter(&t0);QueryPerformanceFrequency(&frequency);
    if(!jpass_armed || jpass_done) return jpass_fail("not_armed");
    if(!t || stats.issued!=t || GetCurrentThreadId()!=main_thread) return jpass_fail("guard");
    const int mt_synced=InterlockedCompareExchange(&mt_active,0,0) && (mt_traffic.mode==MT_SYNC || mt_region.mode==MT_SYNC);
    /* 1 */
    if(mt_synced) {
        const ULONGLONG started=GetTickCount64();
        while(!mt_worker_parked()) {
            if(GetTickCount64()-started>=MT_TIMEOUT_MS) return jpass_fail("worker");
            Sleep(1);
        }
        if(!jpass_wait_ready(&mt_traffic) || !jpass_wait_ready(&mt_region)) return jpass_fail("loading_pass");
        if((mt_traffic.mode==MT_SYNC && mt_task(&mt_traffic) && !mt_rng_point(&mt_traffic,t-1)) ||
           (mt_region.mode==MT_SYNC && mt_task(&mt_region) && !mt_rng_point(&mt_region,t-1))) return jpass_fail("seed");
    }
    ++jpass_steps;
    /* 2 */
    if(pilot_armed && pilot_slot_count) {
        unsigned char *controller=pilot_player_controller(),*object=NULL;
        if(controller && !read_local(controller+0x28,&object,8)) object=NULL;
        void *map=*(void**)(image+0x6cf1500);
        for(unsigned i=0;i<pilot_slot_count;i++) pilot_resolve(&pilot_slots[i],(pilot_find_fn)(image+0xced70),map,object);
    }
    if(view_installed && view_lock_ready) {
        int ok=0;EnterCriticalSection(&view_lock);
        __try {ok=view_frame_latch(t,0);} __finally {LeaveCriticalSection(&view_lock);}
        if(!ok) return jpass_fail("view_latch");
    }
    if(rc_installed) {
        if(!rc_frame_body(t,0)) return jpass_fail("stand_in");
        if(!rc_join_install(t) || (rc_armed_groups&~rc_installed_groups)) return jpass_fail("rc_install");
    }
    ++jpass_steps;
    /* 3 */
    if(InterlockedCompareExchange(&tsnap_active,0,0) && !tsnap_read_installed) {
        unsigned char *manager=*(unsigned char**)(image+0x6cf21c8);
        if(!tsnap_take(manager)) return jpass_fail("travel_snapshot");
        if(!tsnap_swap8(TSNAP_READ_WORD,tsnap_read_original,tsnap_read_patched)) return jpass_fail("travel_read");
        tsnap_read_installed=1;tsnap_idle=1;tsnap_released=0;tsnap_stage="joined";
    }
    ++jpass_steps;
    /* 4 */
    if(mt_synced) {
        if(mt_stride<=1 || !(t%mt_stride)) {jpass_kick_traffic();jpass_kick_region();}
        if(!mt_join(t,MT_TIMEOUT_MS)) return jpass_fail("join");
    }
    ++jpass_steps;
    QueryPerformanceCounter(&t1);
    jpass_wait_us=frequency.QuadPart?(uint64_t)((t1.QuadPart-t0.QuadPart)*1000000/frequency.QuadPart):0;
    jpass_done=1;jpass_why="done";return 1;
}
/* join_admit_passes as the joiner armed it: the extra admission writes (lockstep_join_pctl.h), then the pass of T. */
static void (*jadmit_extra)(void);
static int jadmit_dispatch(uint64_t t) {
    if(jadmit_extra) jadmit_extra();
    return !jpass_armed || jpass_admit(t);
}
/* gate.join_passes_admit(1): a joiner, before its admission (the probe, with native_gate_join_passes_admit). */
static int jpass_arm(void) {
    if(stats.issued || jpass_done) return 0;
    jpass_armed=1;join_admit_passes=jadmit_dispatch;return 1;
}
static int jpass_status(char *out,size_t capacity) {
    return snprintf(out,capacity," passes_admit=%d/%d why=%s steps=%llu kicks=%llu us=%llu",jpass_armed,jpass_done,jpass_why,
                    (unsigned long long)jpass_steps,(unsigned long long)jpass_kicks,(unsigned long long)jpass_wait_us);
}

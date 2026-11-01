/* native_gate_nav_mesh: interior nav helper builds wait for the gate's join in 'sync' (lockstep_nav_mesh.h). */
/* A cache with the five sizes the thread loop tests (four priority sets +0x248.., the rebuild list +0x328), its run flag
 * (+0x60) and a builder thread with the native loop's shape: wait on the semaphore (through the module's wait, as the
 * patched call at 0x10878d9 does), take the first ID, build it outside the lock, erase it after, and take the next one
 * without waiting while any is left. The fake builds Sleep while their ID is busy, as X4's builds do; with the debounce
 * on, a request marks its ID busy until the second frame update after it, as X4's requests do. */
static __attribute__((aligned(16))) unsigned char test_nmesh_cache[0x400];
static volatile LONG test_nmesh_stop;
static struct {uint64_t id;unsigned kind;} test_nmesh_queue[64];static unsigned test_nmesh_head,test_nmesh_tail;
static CRITICAL_SECTION test_nmesh_queue_lock,test_nmesh_mark_lock;
static HANDLE test_nmesh_wake;
static volatile LONG test_nmesh_busy_id;       /* this ID is in the busy sets */
static volatile LONG test_nmesh_debounce;      /* requests mark their ID busy for two frame updates */
static volatile LONG test_nmesh_unpatched;     /* the thread waits in X4's own call (before the patch, or not come round yet) */
static struct {uint64_t id;LONG left;} test_nmesh_marks[64];static unsigned test_nmesh_mark_count;
static volatile LONG test_nmesh_slow;          /* a build that never ends while set */
static uint64_t test_nmesh_built[64];static double test_nmesh_built_time[64];static volatile LONG test_nmesh_built_count;
static __attribute__((aligned(16))) unsigned char test_nmesh_tls_block[0x400];   /* the builder thread's TLS block */
static unsigned char *test_nmesh_tls(void) {return test_nmesh_tls_block;}
static void test_nmesh_counts(void) {
    uint64_t sets=0,rebuilds=0;
    for(unsigned i=test_nmesh_head;i!=test_nmesh_tail;i++) {if(test_nmesh_queue[i%64].kind) ++rebuilds;else ++sets;}
    memset(test_nmesh_cache+0x248,0,0x100);memcpy(test_nmesh_cache+0x248+0x40*1,&sets,8);memcpy(test_nmesh_cache+0x328,&rebuilds,8);
}
static void test_nmesh_request(uint64_t id,unsigned kind) {
    if(InterlockedCompareExchange(&test_nmesh_debounce,0,0)) {
        EnterCriticalSection(&test_nmesh_mark_lock);
        assert(test_nmesh_mark_count<64);test_nmesh_marks[test_nmesh_mark_count].id=id;test_nmesh_marks[test_nmesh_mark_count++].left=2;
        LeaveCriticalSection(&test_nmesh_mark_lock);
    }
    EnterCriticalSection(&test_nmesh_queue_lock);
    test_nmesh_queue[test_nmesh_tail%64].id=id;test_nmesh_queue[test_nmesh_tail++%64].kind=kind;test_nmesh_counts();
    LeaveCriticalSection(&test_nmesh_queue_lock);
    ReleaseSemaphore(test_nmesh_wake,1,NULL);
}
/* The cache's frame update (once per UniverseFrameUpdate): every mark one update older. */
static void test_nmesh_frame_update(void) {
    EnterCriticalSection(&test_nmesh_mark_lock);
    for(unsigned i=0;i<test_nmesh_mark_count;i++) if(test_nmesh_marks[i].left>0) --test_nmesh_marks[i].left;
    LeaveCriticalSection(&test_nmesh_mark_lock);
}
static void test_nmesh_record(uint64_t id) {
    double t;memcpy(&t,test_nmesh_tls_block+NMESH_TLS_TIME,8);
    LONG at=InterlockedIncrement(&test_nmesh_built_count)-1;test_nmesh_built[at%64]=id;test_nmesh_built_time[at%64]=t;
}
static unsigned char test_nmesh_fake_busy(void *cache,uint64_t id) {
    assert(cache==test_nmesh_cache);
    if((LONG)id==InterlockedCompareExchange(&test_nmesh_busy_id,0,0)) return 1;
    unsigned char busy=0;
    EnterCriticalSection(&test_nmesh_mark_lock);
    for(unsigned i=0;i<test_nmesh_mark_count;i++) if(test_nmesh_marks[i].id==id && test_nmesh_marks[i].left>0) busy=1;
    LeaveCriticalSection(&test_nmesh_mark_lock);
    return busy;
}
/* X4's builds Sleep(10) while their ID is busy (0x108bc10). */
static void test_nmesh_busy_wait(void *cache,uint64_t id) {
    while(test_nmesh_fake_busy(cache,id) && !InterlockedCompareExchange(&test_nmesh_stop,0,0)) Sleep(1);
}
static uint64_t test_nmesh_fake_generate(void *cache,uint64_t id) {
    assert(cache==test_nmesh_cache);test_nmesh_busy_wait(cache,id);
    while(InterlockedCompareExchange(&test_nmesh_slow,0,0) && !InterlockedCompareExchange(&test_nmesh_stop,0,0)) Sleep(1);
    Sleep(2);test_nmesh_record(id);return 1;
}
static uint64_t test_nmesh_fake_regenerate(void *cache,uint64_t id,void *params) {
    assert(cache==test_nmesh_cache && params==(void *)test_nmesh_cache);test_nmesh_busy_wait(cache,id);Sleep(1);test_nmesh_record(id|0x8000);return 1;
}
static DWORD WINAPI test_nmesh_builder(LPVOID unused) {
    (void)unused;
    while(!InterlockedCompareExchange(&test_nmesh_stop,0,0)) {
        DWORD woke=InterlockedCompareExchange(&test_nmesh_unpatched,0,0)?WaitForSingleObject(test_nmesh_wake,5):
            nmesh_wait_on(test_nmesh_cache,test_nmesh_wake,5);
        if(woke!=WAIT_OBJECT_0) continue;
        for(;;) {
            EnterCriticalSection(&test_nmesh_queue_lock);
            int any=test_nmesh_head!=test_nmesh_tail;uint64_t id=any?test_nmesh_queue[test_nmesh_head%64].id:0;
            unsigned kind=any?test_nmesh_queue[test_nmesh_head%64].kind:0;
            LeaveCriticalSection(&test_nmesh_queue_lock);
            if(!any || InterlockedCompareExchange(&test_nmesh_stop,0,0)) break;
            if(kind) nmesh_run(NMESH_K_REGENERATE,test_nmesh_cache,id,test_nmesh_cache,(void *)test_nmesh_fake_regenerate,test_nmesh_fake_busy);
            else nmesh_run(NMESH_K_GENERATE,test_nmesh_cache,id,NULL,(void *)test_nmesh_fake_generate,test_nmesh_fake_busy);
            EnterCriticalSection(&test_nmesh_queue_lock);++test_nmesh_head;test_nmesh_counts();LeaveCriticalSection(&test_nmesh_queue_lock);
        }
    }
    return 0;
}
static void test_nmesh_settle(void) {for(int i=0;i<1000 && (test_nmesh_head!=test_nmesh_tail || nmesh_busy_now || nmesh_held);i++) Sleep(2);Sleep(10);}
/* An already gated thread with the builds open, as the steady-state tests start. */
static void test_nmesh_reset(void) {
    EnterCriticalSection(&nmesh_lock);nmesh_reset_counters();nmesh_gate=NMESH_GATE_ARRIVED;LeaveCriticalSection(&nmesh_lock);
    test_nmesh_built_count=0;nmesh_stage="armed";
}
/* What nmesh_arm does after the sites are ours, in the menu (boot phase 0, no tick): the first state, one release. */
static void test_nmesh_arm_sync(void) {
    boot_mode=1;boot_phase=0;stats.issued=0;
    EnterCriticalSection(&nmesh_lock);nmesh_reset_counters();nmesh_begin_locked(NMESH_SYNC);LeaveCriticalSection(&nmesh_lock);
    test_nmesh_built_count=0;nmesh_stage="armed";
    ReleaseSemaphore(test_nmesh_wake,1,NULL);
}
static uint64_t test_nmesh_ms_since(LARGE_INTEGER t0) {
    LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return (uint64_t)((t.QuadPart-t0.QuadPart)*1000/f.QuadPart);
}
static void test_nav_mesh(void) {
    uint64_t saved_issued=stats.issued;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;DWORD saved_ui=ui_thread;
    stats.active=1;boot_phase=3;stats.issued=20;ui_thread=GetCurrentThreadId();
    InitializeCriticalSection(&test_nmesh_queue_lock);InitializeCriticalSection(&test_nmesh_mark_lock);
    test_nmesh_wake=CreateSemaphoreW(NULL,0,1024,NULL);assert(test_nmesh_wake);
    memset(test_nmesh_cache,0,sizeof(test_nmesh_cache));test_nmesh_cache[NMESH_RUN_FLAG]=1;test_nmesh_head=test_nmesh_tail=0;test_nmesh_counts();
    if(!nmesh_lock_ready) {InitializeCriticalSection(&nmesh_lock);nmesh_lock_ready=1;}
    if(!nmesh_open) nmesh_open=CreateEventW(NULL,TRUE,TRUE,NULL);
    assert(nmesh_open);nmesh_installed=1;nmesh_tls=test_nmesh_tls;test_nmesh_reset();
    double native_time=-7.5;memcpy(test_nmesh_tls_block+NMESH_TLS_TIME,&native_time,8);
    HANDLE builder=CreateThread(NULL,0,test_nmesh_builder,NULL,0,NULL);assert(builder);
    /* Observe: builds run as they come, at the builder's own TLS time; rows carry the tick read on the builder thread. */
    nmesh_mode=NMESH_OBSERVE;nmesh_closed=0;
    test_nmesh_request(0x101,0);test_nmesh_request(0x102,1);test_nmesh_settle();
    assert(test_nmesh_built_count==2 && nmesh_generated==1 && nmesh_regenerated==1 && nmesh_free_builds==2 && !nmesh_join_builds && !nmesh_held);
    assert(test_nmesh_built_time[0]==native_time && test_nmesh_built[1]==(0x102|0x8000) && !nmesh_gated && !nmesh_pre_gate);
    assert(nmesh_join_with(test_nmesh_cache,20,1.0,1000) && !nmesh_joins);   /* observe never joins */
    char text[256];assert(nmesh_row_text(0,text,sizeof(text)) && strstr(text,"tick=20 kind=generate key=0000000000000000 klass=2 joined=0 time=c01e000000000000 us="));
    assert(nmesh_row_text(1,text,sizeof(text)) && strstr(text," kind=regenerate ") && !nmesh_row_text(2,text,sizeof(text)));
    assert(!nmesh_frame_text(text,sizeof(text)));   /* no receipt outside 'sync' */
    /* Sync: the first join closes the builds; a frame's requests wait for the next join (the thread takes nothing: it waits
     * at the take gate) and are all built there, at the join's game time, which goes back afterwards. */
    nmesh_mode=NMESH_SYNC;test_nmesh_reset();
    assert(nmesh_join_with(test_nmesh_cache,20,10.0,1000) && nmesh_joins==1 && nmesh_closed && nmesh_frame_text(text,sizeof(text)) &&
           !strcmp(text,"NMESH_1:0:cbf29ce484222325"));
    stats.issued=21;test_nmesh_request(0x201,0);test_nmesh_request(0x202,0);test_nmesh_request(0x203,1);
    Sleep(40);assert(test_nmesh_built_count==0 && !nmesh_held && nmesh_gated>=1 && !nmesh_waited);   /* woke, took nothing */
    stats.issued=22;assert(nmesh_join_with(test_nmesh_cache,21,10.5,2000));
    assert(test_nmesh_built_count==3 && nmesh_join_builds==3 && !nmesh_free_builds && nmesh_joins==2 && nmesh_closed && !nmesh_held &&
           nmesh_last_pending==3 && nmesh_tls_set==3 && nmesh_receipt_builds==3 && nmesh_unkeyed==3);
    for(int i=0;i<3;i++) assert(test_nmesh_built_time[i]==10.5);
    double back;memcpy(&back,test_nmesh_tls_block+NMESH_TLS_TIME,8);assert(back==native_time);
    assert(nmesh_row_text(2,text,sizeof(text)) && strstr(text,"tick=21 ") && strstr(text," joined=1 time=4025000000000000 "));
    char first[64];assert(nmesh_frame_text(first,sizeof(first)) && !strncmp(first,"NMESH_1:3:",10) && strcmp(first,"NMESH_1:0:cbf29ce484222325"));
    Sleep(20);assert(test_nmesh_built_count==3);
    /* The receipt does not depend on the order the thread took the IDs in: the same set in the same join, another order. */
    test_nmesh_reset();assert(nmesh_join_with(test_nmesh_cache,20,10.0,1000));
    test_nmesh_request(0x203,1);test_nmesh_request(0x202,0);test_nmesh_request(0x201,0);
    assert(nmesh_join_with(test_nmesh_cache,21,10.5,2000) && test_nmesh_built_count==3 && nmesh_frame_text(text,sizeof(text)) && !strcmp(text,first));
    /* Another join for the same set is another receipt. */
    test_nmesh_reset();assert(nmesh_join_with(test_nmesh_cache,20,10.0,1000));
    test_nmesh_request(0x201,0);test_nmesh_request(0x202,0);test_nmesh_request(0x203,1);
    assert(nmesh_join_with(test_nmesh_cache,22,10.5,2000) && nmesh_frame_text(text,sizeof(text)) && strcmp(text,first));
    /* A held ID in the busy sets is not built at that join and holds the IDs behind it; nothing builds inside the frame;
     * the next join (sets cleared) builds them all. */
    test_nmesh_reset();assert(nmesh_join_with(test_nmesh_cache,30,11.0,1000));
    InterlockedExchange(&test_nmesh_busy_id,0x301);test_nmesh_request(0x301,0);test_nmesh_request(0x302,0);Sleep(20);
    assert(nmesh_join_with(test_nmesh_cache,31,11.5,2000) && nmesh_held_deferred==1 && nmesh_deferrals==1 && !test_nmesh_built_count && nmesh_closed);
    assert(test_nmesh_tail-test_nmesh_head==2 && nmesh_held && nmesh_held_id==0x301);
    InterlockedExchange(&test_nmesh_busy_id,0);Sleep(20);assert(!test_nmesh_built_count);   /* closed: nothing inside a frame */
    assert(nmesh_join_with(test_nmesh_cache,32,12.0,2000) && test_nmesh_built_count==2 && !nmesh_held && test_nmesh_head==test_nmesh_tail);
    assert(test_nmesh_built[0]==0x301 && test_nmesh_built_time[0]==12.0 && nmesh_receipt_builds==2);
    /* A build that does not end within the join's time fails closed and leaves the builds open. */
    InterlockedExchange(&test_nmesh_slow,1);test_nmesh_request(0x401,0);Sleep(10);
    assert(!nmesh_join_with(test_nmesh_cache,33,12.5,30) && !strcmp(nmesh_stage,"join_timeout") && nmesh_failures==1 && !nmesh_closed);
    assert(!nmesh_join_with(test_nmesh_cache,34,13.0,30));   /* failed: no further joins */
    InterlockedExchange(&test_nmesh_slow,0);test_nmesh_settle();assert(test_nmesh_built_count==3 && !nmesh_free_builds);   /* it began in the join */
    char status[1024];assert(nmesh_status(status,sizeof(status)) && strstr(status,"mode=sync installed=1 ") && strstr(status," join_builds=3 free_builds=0 ") &&
                             strstr(status," stage=join_timeout failures=1"));
    /* A deferred build whose join fails does not wait for a join that never comes: with the builds open it runs natively. */
    nmesh_failures=0;nmesh_stage="armed";assert(nmesh_join_with(test_nmesh_cache,40,15.0,2000) && nmesh_closed);
    InterlockedExchange(&test_nmesh_busy_id,0x601);InterlockedExchange(&test_nmesh_slow,0);test_nmesh_request(0x601,0);Sleep(20);
    EnterCriticalSection(&nmesh_lock);nmesh_joining=1;nmesh_closed=0;SetEvent(nmesh_open);LeaveCriticalSection(&nmesh_lock);   /* a join opens */
    for(int i=0;i<200 && !nmesh_deferred;i++) Sleep(1);
    assert(nmesh_deferred && test_nmesh_built_count==3);
    InterlockedExchange(&test_nmesh_busy_id,0);nmesh_open_builds();test_nmesh_settle();   /* the join failed: open for good */
    assert(test_nmesh_built_count==4 && test_nmesh_built[3]==0x601 && !nmesh_held && !nmesh_deferred);
    /* Unreadable sizes fail closed. */
    nmesh_failures=0;nmesh_stage="armed";assert(!nmesh_join_with(NULL,35,13.5,100) && !strcmp(nmesh_stage,"counts"));
    /* Opening the builds (stop, restore) lets a waiting take go on. */
    nmesh_failures=0;nmesh_stage="armed";assert(nmesh_join_with(test_nmesh_cache,36,14.0,2000) && nmesh_closed);
    test_nmesh_request(0x501,0);Sleep(20);assert(test_nmesh_built_count==4 && !nmesh_held && nmesh_gated>=1);
    nmesh_open_builds();test_nmesh_settle();assert(test_nmesh_built_count==5 && !nmesh_closed);
    /* ---- The boot (build162, footr1/footr2: stopped here with join_timeout, joins=0 held=1 last_pending=13) ----
     * Armed in the menu: one release brings the thread round to its wait and the gate closes there. The snapshot load
     * asks for 13 helpers before any join; X4 marks each requested ID busy until the second frame update after its
     * request and the boot runs one. Nothing is taken or built before the first join; that join defers the ID it takes
     * at once (no timeout); tick 1's update lets the load's IDs go and join 1 builds all of them at its game time; an
     * ID asked for in tick 1 follows two joins later. */
    test_nmesh_arm_sync();
    for(int i=0;i<500 && nmesh_gate==NMESH_GATE_WAIT;i++) Sleep(1);
    assert(nmesh_gate==NMESH_GATE_ARRIVED && nmesh_closed && !nmesh_pre_gate && !nmesh_joins);
    boot_phase=1;InterlockedExchange(&test_nmesh_debounce,1);   /* the load: requests, no join yet */
    for(unsigned i=0;i<12;i++) test_nmesh_request(0x700+i,0);
    test_nmesh_request(0x7a0,1);boot_phase=2;Sleep(40);
    assert(!test_nmesh_built_count && !nmesh_held && !nmesh_busy_now && nmesh_gated>=1 && !nmesh_waited && !nmesh_generated && !nmesh_regenerated &&
           !nmesh_free_builds && !nmesh_pre_gate && nmesh_pending(test_nmesh_cache)==13 && nmesh_closed);
    test_nmesh_frame_update();boot_phase=3;   /* the boot's one UniverseFrameUpdate; the first gated frame follows */
    LARGE_INTEGER t0;QueryPerformanceCounter(&t0);
    assert(nmesh_join_with(test_nmesh_cache,0,5.0,NMESH_JOIN_MS) && test_nmesh_ms_since(t0)<2000);
    assert(nmesh_joins==1 && !test_nmesh_built_count && nmesh_held && nmesh_held_id==0x700 && nmesh_deferrals==1 && nmesh_held_deferred==1 &&
           nmesh_last_pending==13 && nmesh_max_pending==13 && nmesh_closed && !nmesh_failures);
    assert(nmesh_frame_text(text,sizeof(text)) && !strcmp(text,"NMESH_1:0:cbf29ce484222325"));
    stats.issued=1;Sleep(20);assert(!test_nmesh_built_count);   /* tick 1's frame: nothing builds */
    test_nmesh_frame_update();test_nmesh_request(0x7f0,0);       /* tick 1's update; one more request in that tick */
    assert(nmesh_join_with(test_nmesh_cache,1,5.25,NMESH_JOIN_MS));
    assert(test_nmesh_built_count==13 && nmesh_join_builds==13 && nmesh_generated==12 && nmesh_regenerated==1 && !nmesh_free_builds &&
           nmesh_held && nmesh_held_id==0x7f0 && nmesh_held_deferred==2 && nmesh_receipt_builds==13 && nmesh_tls_set==13 && nmesh_closed);
    for(int i=0;i<13;i++) assert(test_nmesh_built_time[i]==5.25);
    assert(test_nmesh_built[0]==0x700 && test_nmesh_built[12]==(0x7a0|0x8000));
    assert(nmesh_row_text(0,text,sizeof(text)) && strstr(text,"tick=1 kind=generate ") && strstr(text," joined=1 time=4015000000000000 "));
    char boot[64];assert(nmesh_frame_text(boot,sizeof(boot)) && !strncmp(boot,"NMESH_1:13:",11));
    /* Joins 2 and 3 back to back: the deferral of join 2 counts only there; join 3 tries again even if the thread has not
     * seen join 2 close yet. */
    stats.issued=2;test_nmesh_frame_update();assert(nmesh_join_with(test_nmesh_cache,2,5.5,NMESH_JOIN_MS) && test_nmesh_built_count==13 && nmesh_held);
    stats.issued=3;test_nmesh_frame_update();assert(nmesh_join_with(test_nmesh_cache,3,5.75,NMESH_JOIN_MS));
    assert(test_nmesh_built_count==14 && test_nmesh_built[13]==0x7f0 && test_nmesh_built_time[13]==5.75 && !nmesh_held && nmesh_receipt_builds==14);
    assert(nmesh_status(status,sizeof(status)) && strstr(status," free_builds=0 ") && strstr(status," gate=arrived ") && strstr(status," pre_gate=0 ") &&
           strstr(status," receipt=14/") && strstr(status," failures=0"));
    /* Another client whose thread woke later took another ID first: the same joins build the same set (receipt). */
    char other[64];
    test_nmesh_arm_sync();for(int i=0;i<500 && nmesh_gate==NMESH_GATE_WAIT;i++) Sleep(1);
    boot_phase=2;for(unsigned i=0;i<12;i++) test_nmesh_request(0x70b-i,0);
    test_nmesh_request(0x7a0,1);test_nmesh_frame_update();boot_phase=3;
    assert(nmesh_join_with(test_nmesh_cache,0,5.0,NMESH_JOIN_MS) && !test_nmesh_built_count && nmesh_held_id==0x70b);
    stats.issued=1;test_nmesh_frame_update();test_nmesh_request(0x7f0,0);
    assert(nmesh_join_with(test_nmesh_cache,1,5.25,NMESH_JOIN_MS) && test_nmesh_built_count==13);
    assert(nmesh_frame_text(other,sizeof(other)) && !strcmp(other,boot));
    stats.issued=2;test_nmesh_frame_update();assert(nmesh_join_with(test_nmesh_cache,2,5.5,NMESH_JOIN_MS));
    stats.issued=3;test_nmesh_frame_update();assert(nmesh_join_with(test_nmesh_cache,3,5.75,NMESH_JOIN_MS) && test_nmesh_built_count==14 && !nmesh_held);
    InterlockedExchange(&test_nmesh_debounce,0);
    /* The transition: what the load asked for and is no longer busy is all built at the first join. */
    test_nmesh_arm_sync();for(int i=0;i<500 && nmesh_gate==NMESH_GATE_WAIT;i++) Sleep(1);
    boot_phase=2;for(unsigned i=0;i<4;i++) test_nmesh_request(0xb01+i,0);
    Sleep(20);assert(!test_nmesh_built_count && !nmesh_held);
    boot_phase=3;assert(nmesh_join_with(test_nmesh_cache,0,7.0,NMESH_JOIN_MS));
    assert(test_nmesh_built_count==4 && nmesh_join_builds==4 && !nmesh_held && nmesh_closed && nmesh_last_pending==4 && nmesh_receipt_builds==4);
    for(int i=0;i<4;i++) assert(test_nmesh_built_time[i]==7.0);
    /* A thread that does not come round before the load is requested (busy in a native build of the menu's world):
     * that build runs natively (pre_gate); the request closes the gate (forced) and the next ID waits for a join. */
    InterlockedExchange(&test_nmesh_unpatched,1);Sleep(20);   /* the thread sits in X4's own wait */
    test_nmesh_arm_sync();Sleep(20);assert(nmesh_gate==NMESH_GATE_WAIT && !nmesh_closed);
    InterlockedExchange(&test_nmesh_slow,1);test_nmesh_request(0xc01,0);
    for(int i=0;i<500 && !nmesh_pre_gate;i++) Sleep(1);
    assert(nmesh_pre_gate==1 && !test_nmesh_built_count);
    InterlockedExchange(&test_nmesh_unpatched,0);boot_phase=1;test_nmesh_request(0xc02,0);   /* the load is requested */
    InterlockedExchange(&test_nmesh_slow,0);
    for(int i=0;i<500 && !nmesh_held;i++) Sleep(1);
    Sleep(20);assert(test_nmesh_built_count==1 && test_nmesh_built[0]==0xc01 && nmesh_held && nmesh_held_id==0xc02 && nmesh_waited==1 &&
                     nmesh_gate==NMESH_GATE_FORCED && nmesh_closed && !nmesh_free_builds && !nmesh_generated);
    boot_phase=3;assert(nmesh_join_with(test_nmesh_cache,0,8.0,NMESH_JOIN_MS) && test_nmesh_built_count==2 && test_nmesh_built_time[1]==8.0 &&
                        nmesh_join_builds==1 && !nmesh_free_builds && !nmesh_held);
    assert(nmesh_status(status,sizeof(status)) && strstr(status," gate=forced ") && strstr(status," pre_gate=1 "));
    /* A build sleeping on the busy sets inside a join (it entered X4's build outside a join, as every build did before the
     * first join in build162) stops that join at once, left open, instead of after the join's 30 s. */
    test_nmesh_reset();nmesh_closed=0;stats.issued=50;InterlockedExchange(&test_nmesh_debounce,1);
    test_nmesh_request(0xd01,0);
    for(int i=0;i<500 && !nmesh_busy_now;i++) Sleep(1);
    assert(nmesh_busy_now && nmesh_held && !test_nmesh_built_count);
    QueryPerformanceCounter(&t0);
    assert(!nmesh_join_with(test_nmesh_cache,50,9.0,NMESH_JOIN_MS) && test_nmesh_ms_since(t0)<2000 && !strcmp(nmesh_stage,"build_busy") && !nmesh_closed);
    test_nmesh_frame_update();test_nmesh_frame_update();test_nmesh_settle();
    assert(test_nmesh_built_count==1 && nmesh_free_builds==1 && !nmesh_join_builds);
    InterlockedExchange(&test_nmesh_debounce,0);
    /* The cache's shutdown (run flag cleared) lets a take waiting at the gate, and a held build, go: X4 waits for the
     * thread. The module fails (shutdown), so a gate still running would stop. */
    test_nmesh_arm_sync();for(int i=0;i<500 && nmesh_gate==NMESH_GATE_WAIT;i++) Sleep(1);
    boot_phase=3;stats.issued=60;assert(nmesh_join_with(test_nmesh_cache,60,10.0,NMESH_JOIN_MS) && nmesh_closed);
    InterlockedExchange(&test_nmesh_busy_id,0xe01);test_nmesh_request(0xe01,0);
    assert(nmesh_join_with(test_nmesh_cache,61,10.25,NMESH_JOIN_MS) && nmesh_held && nmesh_held_id==0xe01 && nmesh_closed);   /* held, deferred */
    InterlockedExchange(&test_nmesh_busy_id,0);test_nmesh_request(0xe02,0);
    test_nmesh_cache[NMESH_RUN_FLAG]=0;test_nmesh_settle();
    assert(test_nmesh_built_count==2 && nmesh_free_builds==2 && nmesh_released==1 && !strcmp(nmesh_stage,"shutdown") && nmesh_failures==1 && !nmesh_closed);
    assert(!nmesh_join_with(test_nmesh_cache,62,10.5,NMESH_JOIN_MS));
    test_nmesh_arm_sync();for(int i=0;i<500 && nmesh_gate==NMESH_GATE_WAIT;i++) Sleep(1);
    assert(nmesh_gate==NMESH_GATE_ARRIVED);   /* the flag is still clear: the take gate lets the next wake go */
    test_nmesh_request(0xe03,0);test_nmesh_settle();
    assert(test_nmesh_built_count==1 && nmesh_released==1 && nmesh_gated==0 && !strcmp(nmesh_stage,"shutdown"));
    test_nmesh_cache[NMESH_RUN_FLAG]=1;
    nmesh_open_builds();InterlockedExchange(&test_nmesh_stop,1);WaitForSingleObject(builder,2000);CloseHandle(builder);
    CloseHandle(test_nmesh_wake);DeleteCriticalSection(&test_nmesh_queue_lock);DeleteCriticalSection(&test_nmesh_mark_lock);
    /* Arming refusals on the zero image: no call at the sites, then calls but the blocks do not match. */
    nmesh_installed=0;nmesh_mode=NMESH_OFF;nmesh_tls=nmesh_tls_native;test_nmesh_reset();nmesh_stage="idle";
    boot_mode=1;boot_phase=0;stats.issued=0;
    assert(!nmesh_arm(NMESH_SYNC) && !strcmp(nmesh_stage,"site") && !nmesh_installed && !nmesh_arm(NMESH_OFF) && !nmesh_arm(NMESH_SYNC+1));
    for(unsigned i=0;i<NMESH_SITES;i++) {nmesh_site_original(i,image+nmesh_sites[i].rva);assert(nmesh_site_state(i)==0);}
    image[nmesh_sites[1].rva]=0xe9;assert(nmesh_site_state(1)<0);image[nmesh_sites[1].rva]=0xe8;   /* a call stays a call */
    image[nmesh_sites[NMESH_WAIT_SITE].rva+1]=0x25;assert(nmesh_site_state(NMESH_WAIT_SITE)<0);   /* an import call stays a call */
    image[nmesh_sites[NMESH_WAIT_SITE].rva+1]=0x15;assert(nmesh_site_state(NMESH_WAIT_SITE)==0);
    assert(!nmesh_arm(NMESH_SYNC) && !strcmp(nmesh_stage,"pins") && nmesh_mode==NMESH_OFF);   /* the zero image's blocks do not match */
    for(unsigned i=0;i<NMESH_SITES;i++) memset(image+nmesh_sites[i].rva,0,nmesh_sites[i].len);
    stats.issued=saved_issued;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;ui_thread=saved_ui;nmesh_stage="idle";
    printf("nav mesh observe/sync join/TLS time/order-free receipt/busy deferral/timeout/open/boot drain/forced gate/build_busy/shutdown/arming tests PASS\n");
}
/* The facts on the pinned image: the cache thread's loop and its three calls, the sizes it tests, the singleton, the
 * request and its debounce, the frame update and where it runs, the build's time and post, the events' dispatch, the
 * NPC slots event, the run flag and the shutdown. */
static void test_nav_mesh_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    for(unsigned i=0;i<NMESH_SITES;i++) assert(nmesh_site_state(i)==0);
    assert(nmesh_pins_ok());
    #define TNM_RIP(at,len) ((unsigned)((at)+(len)+*(int32_t*)(image+(at)+(len)-4)))
    /* The loop (r13 = cache+8): waits on the semaphore cache+0x58 through WaitForSingleObject's import (the site), tests a
     * set's size at +0x18 of its block, builds with rcx = cache. */
    assert(!memcmp(image+0x10878d0,"\xba\xff\xff\xff\xff\x49\x8b\x4d\x50\xff\x15",11) && TNM_RIP(0x10878d9,6)==NMESH_WAIT_IAT);
    assert(!memcmp(image+0x1087909,"\x49\x83\x7e\x18\x00",5));
    assert(!memcmp(image+0x1087930,"\x49\x8d\x4d\xf8\x49\x8b\x56\x10\x48\x8b\x52\x20",12));
    assert(!memcmp(image+0x1087a6a,"\x49\x8d\x4d\xf8\x4c\x8d\x44\x24\x40\x48\x8b\xd3",12));
    assert(!memcmp(image+0x1087adb,"\x49\x8b\x85\x20\x03\x00\x00",7) && !memcmp(image+0x1087af0,"\x49\x8d\x85\x40\x02\x00\x00",7));
    /* The run flag cache+0x60: set when the thread starts, tested at the loop's end, cleared by the cache's shutdown (the
     * singleton), which then releases the semaphore and waits for the thread. */
    assert(!memcmp(image+0x10878b3,"\x41\xc6\x45\x58\x01",5) && !memcmp(image+0x1087d25,"\x41\x80\x7d\x58\x00",5));
    assert(TNM_RIP(0x1226dcb,7)==NMESH_CACHE && !memcmp(image+0x1226dd7,"\x80\x7b\x60\x00",4) && !memcmp(image+0x1226ddd,"\xc6\x43\x60\x00",4));
    /* The singleton the cache's constructor stores and the request reads; the request is 0x1087df0 by tail jump. */
    assert(TNM_RIP(0x1233c25,7)==NMESH_CACHE && TNM_RIP(0x75b414,7)==NMESH_CACHE && image[0x75b425]==0xe9 && TNM_RIP(0x75b425,5)==0x1087df0u);
    /* Both requests release the semaphore cache+0x58 (ReleaseSemaphore's import) and mark the ID busy in +0x170 under +0x148. */
    assert(!memcmp(image+0x1088010,"\x41\x8d\x50\x01\x49\x8b\x4f\x58\xff\x15",10) && TNM_RIP(0x1088018,6)==0x1bc14d8u);
    assert(!memcmp(image+0x75b586,"\x41\x8d\x50\x01\x48\x8b\x4e\x58\xff\x15",10) && TNM_RIP(0x75b58e,6)==0x1bc14d8u);
    assert(!memcmp(image+0x1087fa5,"\x49\x8d\x9f\x48\x01\x00\x00",7) && !memcmp(image+0x1087fbb,"\x49\x8d\xb7\x70\x01\x00\x00",7));
    assert(!memcmp(image+0x75b51f,"\x48\x8d\x9e\x48\x01\x00\x00",7) && !memcmp(image+0x75b538,"\x48\x8d\xbe\x70\x01\x00\x00",7));
    /* The frame update (primary vtable slot 1) clears +0x1b0 and swaps it with +0x170; 0xef41e0 calls every updater's
     * slot 1 only on the Movement worker (TLS+0x314, set by its thread 0xef4400), once per UniverseFrameUpdate. */
    assert(*(uint64_t*)(image+0x2c69010+8)==0x140000000ull+0x1087d90u);
    assert(!memcmp(image+0x1087daf,"\x48\x8d\x8e\xb0\x01\x00\x00",7) && TNM_RIP(0x1087db6,5)==0x11d720u);
    assert(!memcmp(image+0x1087dbb,"\x48\x8d\x8e\x70\x01\x00\x00",7) && TNM_RIP(0x1087dc9,5)==0x79e1f0u);
    assert(!memcmp(image+0xef41ea,"\xba\x14\x03\x00\x00",5) && !memcmp(image+0xef41fb,"\x83\x3c\x0a\x00",4) && !memcmp(image+0xef423b,"\xff\x50\x08",3));
    assert(!memcmp(image+0xef4496,"\xb8\x14\x03\x00\x00\x42\xc7\x04\x30\x01\x00\x00\x00",13));
    assert(image[0x9fe5a4]==0xe8 && TNM_RIP(0x9fe5a4,5)==0xef41e0u);
    /* GeneratePropsEvent dispatches to slot 521 (0x1048); Positional slot 521 asks for the helper. */
    assert(!memcmp(image+0x34e0b0,"\x48\x8b\x02\x48\x8b\xca\x0f\x28\xd3\x49\x8b\xd0\x48\xff\xa0\x48\x10\x00\x00",19));
    assert(image[0x753e68]==0xe8 && TNM_RIP(0x753e68,5)==0x75b3d0u);
    /* The builds: busy sets test 0x108ca70 with Sleep(10) under the build lock; the record's time is the thread's TLS +0x300. */
    assert(image[0x108bbfe]==0xe8 && TNM_RIP(0x108bbfe,5)==NMESH_BUSY && !memcmp(image+0x108bc10,"\xb9\x0a\x00\x00\x00\xff\x15",7));
    assert(image[0x108ae27]==0xe8 && TNM_RIP(0x108ae27,5)==NMESH_BUSY && !memcmp(image+0x108ae30,"\xb9\x0a\x00\x00\x00\xff\x15",7));
    assert(!memcmp(image+0x108c5fa,"\xb8\x00\x03\x00\x00\x48\x8b\x04\x18\x48\x89\x46\x18",13));
    assert(image[0x108c628]==0xe8 && TNM_RIP(0x108c628,5)==0x108c800u);
    /* Publish: NavHelperGeneratedEvent posted with the thread's TLS time through PostEventDeferred; the rebuild likewise. */
    assert(TNM_RIP(0x108c97f,7)==0x2baf4c8u && !memcmp(image+0x108c989,"\xb9\x00\x03\x00\x00\xf2\x0f\x10\x14",9) && TNM_RIP(0x108c999,5)==0x99a720u);
    assert(TNM_RIP(0x108b63e,7)==0x2baf570u && !memcmp(image+0x108b64d,"\xb9\x00\x03\x00\x00",5) && TNM_RIP(0x108b65f,5)==0x99a720u);
    /* Dispatch: slot 454 (0xe30), and NavContext's slot 454 posts NPCSlotsValidatedEvent (vtable 0x2ba6bf0). */
    assert(!memcmp(image+0x9754c0,"\x48\x8b\x02\x48\x8b\xca\x0f\x28\xd3\x49\x8b\xd0\x48\xff\xa0\x30\x0e\x00\x00",19));
    assert(TNM_RIP(0x6b85c0,7)==0x2ba6bf0u);
    /* The singleton's thread is the cache's secondary vtable slot 1. */
    assert(*(uint64_t*)(image+0x2c68ff8+8)==0x140000000ull+0x1087810u);
    #undef TNM_RIP
    VirtualFree(image,0,MEM_RELEASE);image=prior;
    printf("nav mesh image: sites, pins, loop, wait, run flag, shutdown, singleton, request, debounce, frame update, busy wait, record time, posts, dispatch, NPC slots event PASS\n");
}

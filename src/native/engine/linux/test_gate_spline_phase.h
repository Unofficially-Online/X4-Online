/* The route calculator at the frame's phase points only (gate_spline_phase.h): the drain against deques of our own (a
 * calculator thread that empties them once the phase opens, the routes first, then the steering path jobs, as
 * 0x10b0b30 does), the hold, the job's Unlock before its hold, the test delay; on the real executable the pins (tamper
 * refused), both sites armed (only their bytes moved), the page's stubs and trampoline, the job's stub run with a
 * stand-in Unlock, a foreign entry failing the tick check, and the byte-exact restore. X4's code is not executed here.
 * Included by test_gate_linux.c. */
static pthread_mutex_t tlsp_route_cs=PTHREAD_MUTEX_INITIALIZER,tlsp_steer_cs=PTHREAD_MUTEX_INITIALIZER,tlsp_job_cs=PTHREAD_MUTEX_INITIALIZER;
static int tlsp_cells[4];
static void *tlsp_rb,*tlsp_re,*tlsp_sb,*tlsp_se;
static void tlsp_fill(int routes,int jobs) {
    tlsp_rb=&tlsp_cells[0];tlsp_re=routes?(void*)&tlsp_cells[1]:(void*)&tlsp_cells[0];
    tlsp_sb=&tlsp_cells[2];tlsp_se=jobs?(void*)&tlsp_cells[3]:(void*)&tlsp_cells[2];
}
static void tlsp_point(int steer_known) {
    lxsp_route_lock=&tlsp_route_cs;lxsp_route_begin=&tlsp_rb;lxsp_route_end=&tlsp_re;
    lxsp_steer_lock=steer_known?&tlsp_steer_cs:NULL;lxsp_steer_begin=steer_known?&tlsp_sb:NULL;lxsp_steer_end=steer_known?&tlsp_se:NULL;
}
static void *tlsp_calculator(void *p) {
    (void)p;
    for(const ULONGLONG t0=GetTickCount64();!lxsp_is_open();) {if(GetTickCount64()-t0>2000) return (void*)1;usleep(200);}
    usleep(5000);pthread_mutex_lock(&tlsp_route_cs);tlsp_rb=tlsp_re;pthread_mutex_unlock(&tlsp_route_cs);
    usleep(5000);pthread_mutex_lock(&tlsp_steer_cs);tlsp_sb=tlsp_se;pthread_mutex_unlock(&tlsp_steer_cs);
    return NULL;
}
static void *tlsp_opener(void *p) {(void)p;usleep(20000);lxsp_set(1);return NULL;}
static void tlsp_unlock(void *m) {pthread_mutex_unlock((pthread_mutex_t*)m);}
/* A job as the worker reaches it: its mutex held, then the Unlock call (re-pointed: unlock, then the hold). */
static void *tlsp_job(void *p) {
    (void)p;pthread_mutex_lock(&tlsp_job_cs);lxsp_steer_unlock(&tlsp_job_cs);return NULL;
}
static void *tlsp_job_stub(void *p) {
    pthread_mutex_lock(&tlsp_job_cs);((void (*)(void*))p)(&tlsp_job_cs);return NULL;
}
static void tlsp_reset(void) {
    lxsp_calls=lxsp_failures=lxsp_drains=lxsp_jobs=lxsp_held=lxsp_timeouts=lxsp_max_us=lxsp_steer_calls=lxsp_steer_held=lxsp_delayed=0;
    lxsp_stage="idle";
}
static void test_spline_phase_linux(void) {
    pthread_t t;void *r;
    assert(lxsp_event_init());tlsp_reset();
    tlsp_point(0);__atomic_store_n(&lxsp_on,1,__ATOMIC_SEQ_CST);tlsp_fill(0,0);
    assert(!lxsp_drain() && !strcmp(lxsp_stage,"phase_queue") && lxsp_failures==1);   /* the steering deque unknown: refused */
    lxsp_failures=0;lxsp_stage="idle";tlsp_point(1);
    __atomic_store_n(&lxsp_on,0,__ATOMIC_SEQ_CST);tlsp_fill(1,0);assert(lxsp_drain() && !lxsp_drains);   /* off: nothing */
    __atomic_store_n(&lxsp_on,1,__ATOMIC_SEQ_CST);tlsp_fill(0,0);assert(lxsp_drain() && !lxsp_drains);  /* empty: not opened */
    tlsp_fill(1,1);assert(!pthread_create(&t,NULL,tlsp_calculator,NULL));
    assert(lxsp_drain() && lxsp_drains==1 && tlsp_rb==tlsp_re && tlsp_sb==tlsp_se && !lxsp_is_open());   /* both drained, closed */
    assert(!pthread_join(t,&r) && !r);
    tlsp_fill(0,1);assert(!pthread_create(&t,NULL,tlsp_calculator,NULL));   /* STEER_PHASE: a job alone opens it */
    assert(lxsp_drain() && lxsp_drains==2 && tlsp_sb==tlsp_se && !lxsp_is_open());assert(!pthread_join(t,&r) && !r);
    lxsp_drain_ms=30;tlsp_fill(1,0);assert(!lxsp_drain() && !strcmp(lxsp_stage,"phase_drain_timeout") && lxsp_failures==1);   /* nobody empties it */
    assert(!lxsp_is_open());lxsp_failures=0;
    tlsp_fill(0,1);assert(!lxsp_drain() && !strcmp(lxsp_stage,"phase_drain_timeout") && lxsp_failures==1);   /* nor a job */
    assert(!lxsp_is_open());lxsp_failures=0;tlsp_fill(0,0);lxsp_drain_ms=LXSP_DRAIN_MS;
    /* the hold: open passes at once; closed waits for the phase; closed past its bound is counted and runs */
    lxsp_set(1);lxsp_hold();assert(!lxsp_held);lxsp_set(0);
    assert(!pthread_create(&t,NULL,tlsp_opener,NULL));
    {const ULONGLONG t0=GetTickCount64();lxsp_hold();assert(lxsp_held==1 && !lxsp_timeouts && GetTickCount64()-t0>=10);}
    pthread_join(t,NULL);lxsp_set(0);
    lxsp_hold_ms=10;lxsp_hold();assert(lxsp_held==2 && lxsp_timeouts==1 && !lxsp_failures);lxsp_hold_ms=LXSP_PHASE_WAIT_MS;
    /* the job's Unlock comes first: while the job waits for the phase its mutex is free (the drain reads the deque) */
    lxsp_unlock=tlsp_unlock;assert(!pthread_create(&t,NULL,tlsp_job,NULL));
    for(const ULONGLONG t0=GetTickCount64();lxsp_steer_held<1 && GetTickCount64()-t0<2000;) usleep(200);
    usleep(2000);assert(lxsp_steer_held==1 && !lxsp_steer_calls && !pthread_mutex_trylock(&tlsp_job_cs));
    pthread_mutex_unlock(&tlsp_job_cs);lxsp_set(1);pthread_join(t,NULL);assert(lxsp_steer_calls==1 && lxsp_held==3);lxsp_set(0);
    {char out[400];assert(lxsp_status(out,sizeof(out))>0 && strstr(out," phase=1:4:") && strstr(out," steer=1:1") && strstr(out," xform=0:0"));}
    /* the test delay: refused out of range, nothing while off, one sleep and one count per job while on, off at the restore */
    assert(!lxsp_delay_arm(0) && !lxsp_delay_arm(1.5) && !lxsp_delay_arm(LXSP_DELAY_MAX_MS+1) && !strcmp(lxsp_stage,"delay") && !lxsp_delay_ms);
    {   ULONGLONG t0=GetTickCount64();lxsp_delay_wait();assert(!lxsp_delayed && GetTickCount64()-t0<15);
        lxsp_delay_ms=30;t0=GetTickCount64();lxsp_delay_wait();assert(lxsp_delayed==1 && GetTickCount64()-t0>=25);
        char out[400];assert(lxsp_status(out,sizeof(out))>0 && strstr(out," delay=30:1"));
    }
    lxsp_restore();assert(!lxsp_phase_on() && lxsp_is_open() && !lxsp_delay_ms);   /* not installed: the hold released */
    lxsp_unlock=NULL;lxsp_route_lock=lxsp_steer_lock=NULL;lxsp_route_begin=lxsp_route_end=lxsp_steer_begin=lxsp_steer_end=NULL;
    lxsp_set(0);tlsp_reset();
    printf("spline phase: the steering deque required, off, empty, both deques drained by the calculator and closed again, a job alone, "
           "the drain bound for either, the hold (open, opened, bound), the job's Unlock before its hold, the delay, the restore's release PASS\n");
}
/* A code byte flipped for a check, the page made writable first (an armed site leaves it read-execute). */
static void tlsp_flip(uint32_t va) {
    assert(!mprotect((void*)((uintptr_t)LX(va)&~(uintptr_t)(GATE_PAGE-1)),GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));*LX(va)^=1;
}
static void test_spline_phase_elf(void) {
    assert(!memcmp(LX(LXSP_ROUTE_VA),lxsp_route_head,8) && lxsp_route_state()==0 && lxsp_steer_state()==0);
    assert(lxsp_loop_pins() && lxsp_pins());
    /* the job's Unlock takes the steering mutex: lea rdi,[0x3e76a80] right before the call */
    assert(!memcmp(LX(0x10b0cb7u),"\x48\x8d\x3d",3) && LXSP_STEER_CALL_VA==0x10b0cb7u+7u+3u);
    tlsp_flip(0x10b0d00u);assert(!lxsp_pins());tlsp_flip(0x10b0d00u);   /* the job */
    tlsp_flip(0x1374db0u);assert(!lxsp_pins());tlsp_flip(0x1374db0u);   /* the route head past the 8 bytes */
    tlsp_flip(0x10b0c00u);assert(!lxsp_pins());tlsp_flip(0x10b0c00u);   /* the route part of the loop (spline_worker_run) */
    tlsp_flip(0x10b0d55u);assert(!lxsp_loop_pins() && !lxsp_pins());tlsp_flip(0x10b0d55u);assert(lxsp_pins());   /* the job's publication */
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();tlsp_reset();
    stats.issued=4;assert(!lxsp_phase_arm() && !strcmp(lxsp_stage,"state") && !lxsp_installed);stats.issued=0;
    unsigned char head[16],line[48];memcpy(head,LX(LXSP_ROUTE_VA),16);memcpy(line,LX(0x10b0cb0u),48);
    assert(lxsp_phase_arm() && lxsp_installed && lxsp_phase_on() && lxsp_phase_arm() && !strcmp(lxsp_stage,"armed"));
    assert(lxsp_route_state()==1 && lxsp_steer_state()==1 && lxsp_page_ok() && lxsp_pins() && lxsp_check());
    {   /* the entry: jmp rel32 to the stub and three int3; the call: its rel32 to the job's stub; nothing else moved */
        int32_t r;unsigned char *e=LX(LXSP_ROUTE_VA),*s=LX(LXSP_STEER_CALL_VA);
        memcpy(&r,e+1,4);assert(e[0]==0xe9 && e+5+r==lxsp_page+LXSP_ROUTE_STUB && !memcmp(e+5,"\xcc\xcc\xcc",3) && !memcmp(e+8,head+8,8));
        memcpy(&r,s+1,4);assert(s[0]==0xe8 && s+5+r==lxsp_page+LXSP_STEER_STUB);
        assert(!memcmp(LX(0x10b0cb0u),line,0x12) && !memcmp(LX(0x10b0cc6u),line+0x16,48-0x16));
    }
    {   /* the page: stub -> the route hook; the trampoline replays the 8 bytes and goes on at 0x1374d98; stub -> the job's Unlock */
        const unsigned char *p=lxsp_page;uint64_t w;
        assert(!memcmp(p,"\xff\x25\x00\x00\x00\x00",6));memcpy(&w,p+6,8);assert(w==(uint64_t)(uintptr_t)lxsp_route_hook);
        assert(!memcmp(p+LXSP_ROUTE_TRAMP,lxsp_route_head,8) && !memcmp(p+LXSP_ROUTE_TRAMP+8,"\xff\x25\x00\x00\x00\x00",6));
        memcpy(&w,p+LXSP_ROUTE_TRAMP+14,8);assert(w==(uint64_t)(uintptr_t)LX(LXSP_ROUTE_RESUME_VA));
        assert(!memcmp(p+LXSP_STEER_STUB,"\xff\x25\x00\x00\x00\x00",6));memcpy(&w,p+LXSP_STEER_STUB+6,8);assert(w==(uint64_t)(uintptr_t)lxsp_steer_unlock);
    }
    /* X4's deques in the image are empty: a drain reads both under X4's mutexes and opens nothing */
    assert(lxsp_route_lock==(void*)LX(LXSP_ROUTE_MUTEX_VA) && (unsigned char*)lxsp_steer_lock==LX(LXSP_STEER_MUTEX_VA)+8);
    assert(lxsp_drain() && !lxsp_drains && !lxsp_failures);
    {   /* the job's stub run with a stand-in Unlock: unlocked, then held until the phase opens */
        pthread_t t;lxsp_unlock=tlsp_unlock;
        assert(!pthread_create(&t,NULL,tlsp_job_stub,(void*)(lxsp_page+LXSP_STEER_STUB)));
        for(const ULONGLONG t0=GetTickCount64();lxsp_steer_held<1 && GetTickCount64()-t0<2000;) usleep(200);
        assert(lxsp_steer_held==1 && !lxsp_steer_calls && !pthread_mutex_trylock(&tlsp_job_cs));pthread_mutex_unlock(&tlsp_job_cs);
        lxsp_set(1);pthread_join(t,NULL);lxsp_set(0);assert(lxsp_steer_calls==1);lxsp_unlock=NULL;
    }
    {char out[400];assert(lxsp_status(out,sizeof(out))>0 && strstr(out,"installed=1 restored=0 stage=armed") && strstr(out," phase=1:0:0:1:0:") && strstr(out," steer=1:1"));}
    /* a foreign entry fails the tick check; ours back, the check passes again */
    tlsp_flip(LXSP_ROUTE_VA+6u);assert(!lxsp_check() && !strcmp(lxsp_stage,"site_changed"));tlsp_flip(LXSP_ROUTE_VA+6u);
    lxsp_failures=0;lxsp_stage="armed";assert(lxsp_check());
    lxsp_restore();
    assert(!lxsp_installed && lxsp_restored && !lxsp_phase_on() && !strcmp(lxsp_stage,"restored") && lxsp_route_state()==0 && lxsp_steer_state()==0);
    assert(!memcmp(LX(LXSP_ROUTE_VA),head,16) && !memcmp(LX(0x10b0cb0u),line,48) && lxsp_pins());
    munmap(lxsp_page,GATE_PAGE);lxsp_page=NULL;lxsp_restored=0;lxsp_set(0);tlsp_reset();stats.active=0;
    lxsp_route_lock=lxsp_steer_lock=NULL;lxsp_route_begin=lxsp_route_end=lxsp_steer_begin=lxsp_steer_end=NULL;
    printf("spline phase ELF: pins (tamper refused), the route entry and the job's Unlock armed (only their bytes moved), the stubs and "
           "the trampoline, X4's empty deques drained, the job's stub (unlock, then hold), a foreign entry stops, restored byte-exact PASS\n");
}

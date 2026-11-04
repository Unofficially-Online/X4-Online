/* Spline routes joined (gate_spline_join.h): the join against a stand-in model (a one-request libstdc++ deque,
 * a request published by another thread, a stand-in worker and mutex), and on the real executable the pins,
 * the slot swap and its restore. Included by test_gate_linux.c. */
static unsigned tsj_locks,tsj_unlocks;
static void tsj_lock(void *m) {(void)m;++tsj_locks;}
static void tsj_unlock(void *m) {(void)m;++tsj_unlocks;}
static unsigned char tsj_request[0x100];
static void *tsj_publish(void *arg) {
    (void)arg;struct timespec t={0,20000000};nanosleep(&t,NULL);
    uint32_t one=1;memcpy(tsj_request+LX_SPLINE_REQUEST_STATUS,&one,4);return NULL;
}
static void tsj_status(uint32_t s) {memcpy(tsj_request+LX_SPLINE_REQUEST_STATUS,&s,4);}
static void test_spline_join_linux(void) {
    reset_session();stats.active=1;boot_mode=1;boot_phase=3;stats.issued=7;ui_thread=GetCurrentThreadId();
    lsj_lock=tsj_lock;lsj_unlock=tsj_unlock;lsj_joins=lsj_waits=lsj_timeouts=lsj_failures=0;
    static unsigned char model[0x500],worker[0xd0];static void *deque[4];
    void *cur=&deque[0],*finish=&deque[0];deque[0]=tsj_request;
    memcpy(model+LX_SPLINE_DEQUE_CUR,&cur,8);memcpy(model+LX_SPLINE_DEQUE_FINISH,&finish,8);
    int waited=-1;
    /* Nothing pending: no request, no lock. */
    assert(lsj_join(model,30,&waited)==1 && !waited && !lsj_joins && !tsj_locks);
    finish=&deque[1];memcpy(model+LX_SPLINE_DEQUE_FINISH,&finish,8);
    /* Published already: one request, read under the mutex, no wait. */
    tsj_status(1);assert(lsj_join(model,30,&waited)==1 && !waited && lsj_joins==1 && tsj_locks==1 && tsj_unlocks==1);
    /* A status the calculator never writes: a failure. */
    tsj_status(2);assert(lsj_join(model,30,&waited)==0);
    /* Pending, but no running worker to publish it: a failure, without waiting. */
    uintptr_t page=(uintptr_t)LX(LX_SPLINE_WORKER_VA)&~(uintptr_t)(GATE_PAGE-1);
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));
    put64(LX(LX_SPLINE_WORKER_VA),0);tsj_status(0);assert(lsj_join(model,30,&waited)==0 && !waited);
    void *vt=LX(LX_SPLINE_WORKER_VTABLE);memcpy(worker,&vt,8);worker[LX_SPLINE_WORKER_RUNNING]=0;
    put64(LX(LX_SPLINE_WORKER_VA),(uint64_t)(uintptr_t)worker);assert(lsj_join(model,30,&waited)==0 && !waited);
    /* Pending with the worker running: the join waits until another thread publishes it. */
    worker[LX_SPLINE_WORKER_RUNNING]=1;tsj_status(0);
    pthread_t publisher;assert(!pthread_create(&publisher,NULL,tsj_publish,NULL));
    assert(lsj_join(model,2000,&waited)==1 && waited==1 && lsj_waits==1 && !lsj_timeouts);
    pthread_join(publisher,NULL);
    /* Never published: the bounded wait times out (a failure). */
    tsj_status(0);uint64_t started=GetTickCount64();
    assert(lsj_join(model,30,&waited)==0 && waited==1 && lsj_timeouts==1 && GetTickCount64()-started>=30);
    char out[384];assert(lsj_status(out,sizeof(out))>0 && strstr(out," waits=2 timeouts=1 "));
    lsj_lock=lsj_unlock=NULL;put64(LX(LX_SPLINE_WORKER_VA),0);
    printf("spline join PASS\n");
}
/* The real executable: pins, the slot as found, the swap to lsj_update and back. The file's slot holds the update's
 * link address (LX(va)==va in the process); this test's copy of the image is elsewhere, so it is relocated first. */
static void test_spline_join_elf(void) {
    void *slot=NULL;memcpy(&slot,LX(LX_SPLINE_SLOT_VA),8);assert(slot==(void*)(uintptr_t)LX_SPLINE_UPDATE_VA);
    uintptr_t page=(uintptr_t)LX(LX_SPLINE_SLOT_VA)&~(uintptr_t)(GATE_PAGE-1);
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));put64(LX(LX_SPLINE_SLOT_VA),(uint64_t)(uintptr_t)LX(LX_SPLINE_UPDATE_VA));
    assert(lsj_pins() && lsj_slot()==LX(LX_SPLINE_UPDATE_VA));
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    boot_options=0;assert(!lsj_install() && !strcmp(lsj_stage,"needs"));
    boot_options=1;assert(lsj_install() && lsj_installed && lsj_slot()==(void*)lsj_update && lsj_check());
    assert(lsj_arm_all() && lsj_all);
    lsj_restore();assert(!lsj_installed && lsj_restored && lsj_slot()==LX(LX_SPLINE_UPDATE_VA));
    lsj_all=0;boot_options=0;
    printf("spline join ELF PASS\n");
}

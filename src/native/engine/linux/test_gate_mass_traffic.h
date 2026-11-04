/* Mass traffic and region passes joined (gate_mass_traffic.h): the join against stand-in tasks (a pass finished by
 * another thread, a timeout, observe, the stride's hold and release), the seed a woken pass thread takes, and on the
 * real executable the pins, the three redirected calls and their restore. Included by test_gate_linux.c. */
static void tmt_lock(void *m) {(void)m;}
static unsigned char tmt_traffic_task[0x200],tmt_region_task[0x200];
static void *tmt_finish(void *arg) {
    struct timespec t={0,20000000};nanosleep(&t,NULL);((unsigned char*)arg)[LX_MT_TRAFFIC_PARKED]=1;return NULL;
}
static void tmt_tasks(int present) {
    uintptr_t page=(uintptr_t)LX(LX_FS_MASS_VA)&~(uintptr_t)(GATE_PAGE-1);
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));
    page=(uintptr_t)LX(LX_FS_REGION_VA)&~(uintptr_t)(GATE_PAGE-1);
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));
    put64(LX(LX_FS_MASS_VA),present?(uint64_t)(uintptr_t)tmt_traffic_task:0);put64(LX(LX_FS_REGION_VA),present?(uint64_t)(uintptr_t)tmt_region_task:0);
}
static void test_mass_traffic_linux(void) {
    reset_session();stats.active=1;boot_mode=1;boot_phase=3;stats.issued=11;ui_thread=GetCurrentThreadId();
    lmt_lock=lmt_unlock=tmt_lock;lmt_pass_reset(&lmt_traffic);lmt_pass_reset(&lmt_region);lmt_stride=1;lmt_stride_skips=0;
    lmt_traffic.mode=LMT_SYNC;lmt_region.mode=LMT_SYNC;lmt_active=1;lmt_failures=0;
    lmt_worker_waits=1;lmt_worker_wakes=0; /* the MoveWorker is at its frame-start wait */
    tmt_tasks(1);tmt_traffic_task[LX_MT_TRAFFIC_PARKED]=1;tmt_region_task[LX_MT_REGION_PARKED]=1;
    /* Both passes done: joined at once, each pass's next seed is Windows' (domain, completed tick). */
    assert(lmt_join(11) && lmt_traffic.joins==1 && lmt_region.joins==1 && !lmt_traffic.busy);
    assert(lmt_traffic.seed==lockstep_seed(LMT_DOMAIN,11,0,0,0,0) && lmt_region.seed==lockstep_seed(LMT_REGION_DOMAIN,11,0,0,0,0));
    assert(lmt_traffic.seed_pending && lmt_region.seed_pending);
    /* The woken pass thread takes its seed once, into its own word. */
    uint64_t *word=gate_tls_rng(gate_tls_self());*word=0x77;
    lmt_woken(&lmt_traffic);assert(*word==lockstep_seed(LMT_DOMAIN,11,0,0,0,0) && lmt_traffic.applied==1 && !lmt_traffic.seed_pending);
    *word=0x78;lmt_woken(&lmt_traffic);assert(*word==0x78 && lmt_traffic.applied==1);
    /* A pass still running at the gate tick: the join waits until another thread parks it. */
    tmt_traffic_task[LX_MT_TRAFFIC_PARKED]=0;
    pthread_t finisher;assert(!pthread_create(&finisher,NULL,tmt_finish,tmt_traffic_task));
    assert(lmt_join(12) && lmt_traffic.joins==2 && lmt_traffic.busy==1 && lmt_traffic.wait_us>=10000);
    pthread_join(finisher,NULL);
    /* Observe: no wait, a running pass is only counted. */
    lmt_traffic.mode=LMT_OBSERVE;tmt_traffic_task[LX_MT_TRAFFIC_PARKED]=0;
    assert(lmt_join(13) && lmt_traffic.busy==2 && lmt_traffic.joins==2);
    lmt_traffic.mode=LMT_SYNC;tmt_traffic_task[LX_MT_TRAFFIC_PARKED]=1;
    /* Stride 2: after tick 14 (tick 15 is off the stride) both parked bytes are held at 0, the next join gives them back. */
    lmt_stride=2;assert(lmt_join(14) && !tmt_traffic_task[LX_MT_TRAFFIC_PARKED] && !tmt_region_task[LX_MT_REGION_PARKED]);
    assert(lmt_traffic.held && lmt_region.held);
    assert(lmt_join(15) && tmt_traffic_task[LX_MT_TRAFFIC_PARKED]==1 && lmt_stride_skips==1 && !lmt_traffic.held);
    lmt_stride=1;
    /* A pass that never parks fails closed. */
    tmt_region_task[LX_MT_REGION_PARKED]=0;unsigned saved=lmt_region.timeout_ms;lmt_region.timeout_ms=30;
    assert(!lmt_join(16) && lmt_failures==1 && !strcmp(lmt_stage,"region_pass"));
    lmt_region.timeout_ms=saved;lmt_failures=0;
    /* An absent task (a world without traffic) is skipped and counted. */
    tmt_tasks(0);assert(lmt_join(17) && lmt_traffic.absent==1 && lmt_region.absent==1);
    char out[1024];assert(lmt_pass_status(&lmt_traffic,out,sizeof(out))>0 && strstr(out,"mode=sync") && strstr(out," applied=1 "));
    lmt_active=0;lmt_traffic.mode=lmt_region.mode=LMT_OFF;lmt_lock=lmt_unlock=NULL;lmt_worker_waits=lmt_worker_wakes=0;
    printf("mass traffic and region pass PASS\n");
}
/* The real executable: pins, the three calls as found, redirected to the stubs and back. */
static void test_mass_traffic_elf(void) {
    unsigned char before[LMT_SITES][16];
    for(unsigned i=0;i<LMT_SITES;i++) {assert(lmt_site_state(i)==0);memcpy(before[i],LX(lmt_sites[i].block_va),16);}
    assert(lmt_pins());
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();lmt_page=NULL;
    lmt_pass_reset(&lmt_traffic);lmt_pass_reset(&lmt_region);
    assert(lmt_arm_pass(&lmt_traffic,LMT_SYNC) && lmt_installed && lmt_arm_pass(&lmt_region,LMT_SYNC));
    assert(lmt_traffic.seed_pending && lmt_traffic.seed==lockstep_seed(LMT_DOMAIN,0,0,0,0,0)); /* the first gated pass is seeded too */
    for(unsigned i=0;i<LMT_SITES;i++) assert(lmt_site_state(i)==1);
    assert(lmt_stubs_ok() && lmt_owned() && lmt_check() && lmt_stride_arm(2) && lmt_stride==2);
    lmt_restore();assert(!lmt_installed && lmt_restored);
    for(unsigned i=0;i<LMT_SITES;i++) assert(lmt_site_state(i)==0 && !memcmp(before[i],LX(lmt_sites[i].block_va),16));
    lmt_traffic.mode=lmt_region.mode=LMT_OFF;lmt_stride=1;
    printf("mass traffic and region pass ELF PASS\n");
}

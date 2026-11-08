/* Synthetic and copied-ELF tests: no engine callback is executed. */
static unsigned navmesh_test_calls, navmesh_test_bad_errno;
static void navmesh_test_native(void *source, void *event, double due) {
    (void)source;(void)event;
    if(errno!=EDOM || due!=12.5) __atomic_add_fetch(&navmesh_test_bad_errno,1,__ATOMIC_RELAXED);
    __atomic_add_fetch(&navmesh_test_calls,1,__ATOMIC_RELAXED);
    errno=ERANGE;
}
typedef struct {uint64_t vtable,runtime;} navmesh_test_source;
static navmesh_test_source navmesh_test_object;
static uint64_t navmesh_test_event;
static void *navmesh_test_thread(void *arg) {
    (void)arg;
    for(unsigned i=0;i<50;i++) {errno=EDOM;navmesh_post_observer(&navmesh_test_object,&navmesh_test_event,12.5);assert(errno==ERANGE);}
    return NULL;
}
static void *navmesh_test_stop_thread(void *arg) {
    (void)arg;
    navmesh_post_restore();
    return NULL;
}
static void test_navmesh_post(void) {
    memset(navmesh_post_rows,0,sizeof(navmesh_post_rows));
    navmesh_test_object=(navmesh_test_source){(uintptr_t)image+0x50,0x1234};
    navmesh_test_event=(uintptr_t)LX(NAVMESH_POST_EVENT_VTABLE);
    navmesh_post_first=1;navmesh_post_last=5;navmesh_post_native=navmesh_test_native;
    navmesh_post_count=navmesh_post_attempts=navmesh_post_published=navmesh_post_unreadable=navmesh_post_overflow=0;
    navmesh_test_calls=navmesh_test_bad_errno=0;navmesh_post_publish(2,0);
    __atomic_store_n(&navmesh_post_guard,0,__ATOMIC_RELEASE);
    uint64_t read_count_before = gate_read_count;
    pthread_t workers[4];
    for(unsigned i=0;i<4;i++) assert(!pthread_create(&workers[i],NULL,navmesh_test_thread,NULL));
    for(unsigned i=0;i<4;i++) assert(!pthread_join(workers[i],NULL));
    assert(navmesh_test_calls==200 && !navmesh_test_bad_errno && navmesh_post_count==200);
    assert(gate_read_count == read_count_before);
    for(unsigned i=0;i<200;i++) {
        navmesh_post_row *row=&navmesh_post_rows[i];
        assert(__atomic_load_n(&row->ready,__ATOMIC_ACQUIRE) && row->readable && row->phase==2 && row->epoch==0);
        assert(row->runtime==0x1234 && row->event==(uintptr_t)&navmesh_test_event);
    }
    /* Phase 3 before the first permit is bootstrap context, not tick 1. */
    navmesh_post_publish(3, 0);
    errno=EDOM;navmesh_post_observer(&navmesh_test_object,&navmesh_test_event,12.5);
    assert(navmesh_post_count==201 && navmesh_post_rows[200].phase==3 && navmesh_post_rows[200].epoch==0);
    navmesh_test_event = 0;
    errno=EDOM;navmesh_post_observer(&navmesh_test_object,&navmesh_test_event,12.5);
    assert(navmesh_post_count==202 && navmesh_post_unreadable==1 && errno==ERANGE);
    navmesh_test_event=(uintptr_t)LX(NAVMESH_POST_EVENT_VTABLE);
    navmesh_post_publish(3,6);errno=EDOM;navmesh_post_observer(&navmesh_test_object,&navmesh_test_event,12.5);
    assert(navmesh_test_calls==203 && navmesh_post_count==202 && navmesh_post_attempts==203 && errno==ERANGE);
    navmesh_post_publish(3,3);navmesh_post_count=NAVMESH_POST_CAPACITY;
    errno=EDOM;navmesh_post_observer(&navmesh_test_object,&navmesh_test_event,12.5);
    assert(navmesh_post_overflow==1 && navmesh_post_count==NAVMESH_POST_CAPACITY+1 && errno==ERANGE);
    navmesh_post_count=202;
    /* An admitted capture must finish before stop can report a drain. A late
     * entry still forwards to native but does not touch the sealed rows. */
    __atomic_store_n(&navmesh_post_guard, 1, __ATOMIC_RELEASE);
    pthread_t stopper;
    assert(!pthread_create(&stopper, NULL, navmesh_test_stop_thread, NULL));
    while (!(__atomic_load_n(&navmesh_post_guard, __ATOMIC_ACQUIRE) & NAVMESH_POST_CLOSED))
        sched_yield();
    errno=EDOM;navmesh_post_observer(&navmesh_test_object,&navmesh_test_event,12.5);
    assert(navmesh_post_count==202 && navmesh_test_calls==205 && errno==ERANGE);
    navmesh_post_leave();
    assert(!pthread_join(stopper, NULL));
    assert(navmesh_post_restored && !navmesh_post_cleanup_uncertain);
    memset(navmesh_post_rows,0,sizeof(navmesh_post_rows));
    navmesh_post_count=navmesh_post_attempts=navmesh_post_published=navmesh_post_unreadable=navmesh_post_overflow=0;
    printf("navmesh post synthetic threads, filtering, overflow and forwarding PASS\n");
}
static void navmesh_test_flip_byte(uint32_t address) {
    unsigned char *byte = LX(address);
    void *page = (void *)((uintptr_t)byte & ~(uintptr_t)(GATE_PAGE - 1));
    assert(!mprotect(page, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
    *byte ^= 1;
    assert(!mprotect(page, GATE_PAGE, PROT_READ | PROT_EXEC));
}
static void navmesh_test_rejected_pin(uint32_t address, const char *stage) {
    navmesh_test_flip_byte(address);
    assert(!navmesh_post_arm(1, 60));
    assert(!strcmp(navmesh_post_stage, stage));
    assert(!navmesh_post_installed && !navmesh_post_ever);
    navmesh_test_flip_byte(address);
}
static void test_navmesh_post_elf(void) {
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;ui_thread=GetCurrentThreadId();
    unsigned char original[16],patched[16];memcpy(original,LX(NAVMESH_POST_SITE),16);
    /* Each independent pin detects arbitrary modifications in its own span.
     * The exact original image remains the only accepted variant here. */
    navmesh_test_rejected_pin(NAVMESH_POST_SITE, "pin_site");
    navmesh_test_rejected_pin(0x10a6ef0u, "pin_body");
    navmesh_test_rejected_pin(NAVMESH_POST_NATIVE + 0x20u, "pin_native");
    navmesh_test_rejected_pin(0x10a5e40u, "pin_worker");
    assert(navmesh_post_pins() && navmesh_post_arm(1,60));
    assert(navmesh_post_owned() && navmesh_post_check() && navmesh_post_arm(1,60));
    assert(!navmesh_post_arm(1,61));
    memcpy(patched,LX(NAVMESH_POST_SITE),16);
    assert(patched[0]==0xe8 && !memcmp(patched+5,original+5,11));
    assert(!mprotect(navmesh_post_page, GATE_PAGE, PROT_READ | PROT_WRITE));
    navmesh_post_page[0] ^= 1;
    assert(!mprotect(navmesh_post_page, GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(!navmesh_post_owned());
    navmesh_post_restore();
    assert(navmesh_post_installed && !navmesh_post_restored && navmesh_post_cleanup_uncertain);
    assert(!mprotect(navmesh_post_page, GATE_PAGE, PROT_READ | PROT_WRITE));
    navmesh_post_page[0] ^= 1;
    assert(!mprotect(navmesh_post_page, GATE_PAGE, PROT_READ | PROT_EXEC));
    navmesh_post_restore();
    assert(!navmesh_post_installed && !navmesh_post_restored && navmesh_post_cleanup_uncertain);
    navmesh_post_cleanup_uncertain=0; /* Only the copied test image was touched. */
    navmesh_post_restored=1;
    assert(!memcmp(original,LX(NAVMESH_POST_SITE),16));
    assert(!navmesh_post_arm(1,60));
    /* Isolated copied ELF has no thread executing its site. */
    navmesh_post_ever=0;navmesh_post_test_swap_failure=1;
    assert(!navmesh_post_arm(1,60) && navmesh_post_installed && navmesh_post_cleanup_uncertain);
    assert(!navmesh_post_arm(1,60));
    navmesh_post_restore();assert(!navmesh_post_installed && !navmesh_post_restored && navmesh_post_cleanup_uncertain);
    navmesh_post_ever=0;navmesh_post_cleanup_uncertain=0;navmesh_post_test_swap_failure=2;
    assert(!navmesh_post_arm(1,60) && !navmesh_post_installed && navmesh_post_cleanup_uncertain);
    navmesh_post_restore();assert(!navmesh_post_restored && navmesh_post_cleanup_uncertain);
    navmesh_post_cleanup_uncertain=0;
    printf("navmesh post copied ELF pins, ownership and restoration PASS\n");
}

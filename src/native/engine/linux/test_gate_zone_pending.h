/* The frame's pending physics work in every frame (gate_zone_pending.h): the wrapper against a real mutex (free, busy
 * for another caller, busy and freed within the wait, busy past it, another thread, no session), and on the real
 * executable the function's head (the stack the stub reads), the pins, the three sites re-pointed at the stub, the stub
 * run with a planted frame (the caller read at [rsp+0x50]) and the byte-exact restore. Included by test_gate_linux.c. */
static pthread_mutex_t tzp_lock=PTHREAD_MUTEX_INITIALIZER;
static volatile int tzp_hold_ms,tzp_held,tzp_other_result;
static void *tzp_holder(void *arg) {
    (void)arg;pthread_mutex_lock(&tzp_lock);__atomic_store_n(&tzp_held,1,__ATOMIC_SEQ_CST);
    usleep((useconds_t)tzp_hold_ms*1000u);pthread_mutex_unlock(&tzp_lock);return NULL;
}
static void tzp_hold(pthread_t *t,int ms) {
    tzp_hold_ms=ms;__atomic_store_n(&tzp_held,0,__ATOMIC_SEQ_CST);
    assert(!pthread_create(t,NULL,tzp_holder,NULL));
    while(!__atomic_load_n(&tzp_held,__ATOMIC_SEQ_CST)) usleep(100);
}
static void *tzp_other(void *arg) {
    (void)arg;tzp_other_result=lxzp_enter(&tzp_lock,LX(LXZP_FRAME_RETURN_VA));
    if(!tzp_other_result) pthread_mutex_unlock(&tzp_lock);
    return NULL;
}
static void tzp_reset(void) {lxzp_calls=lxzp_waited=lxzp_lost=lxzp_nested=lxzp_other=0;}
static void test_zone_pending_linux(void) {
    reset_session();stats.active=1;ui_thread=GetCurrentThreadId();tzp_reset();
    const unsigned char *frame=LX(LXZP_FRAME_RETURN_VA),*elsewhere=LX(0x116390cu);
    pthread_t t;
    /* Free: taken at once, whoever asks. */
    assert(lxzp_enter(&tzp_lock,frame)==0);pthread_mutex_unlock(&tzp_lock);
    assert(lxzp_enter(&tzp_lock,elsewhere)==0);pthread_mutex_unlock(&tzp_lock);
    assert(lxzp_calls==2 && !lxzp_waited && !lxzp_nested && !lxzp_other);
    /* Busy for a call that is not the frame's: X4's answer at once. */
    tzp_hold(&t,200);
    {const ULONGLONG start=GetTickCount64();assert(lxzp_enter(&tzp_lock,elsewhere)==EBUSY);assert(GetTickCount64()-start<100);}
    assert(lxzp_nested==1 && !lxzp_waited);
    /* Busy for the frame's call and freed within the wait: taken. */
    assert(lxzp_enter(&tzp_lock,frame)==0);pthread_mutex_unlock(&tzp_lock);pthread_join(t,NULL);
    assert(lxzp_waited==1 && !lxzp_lost);
    /* Busy past the wait: X4's answer after LXZP_WAIT_MS. */
    tzp_hold(&t,700);
    {const ULONGLONG start=GetTickCount64();assert(lxzp_enter(&tzp_lock,frame)==EBUSY);
     const ULONGLONG took=GetTickCount64()-start;assert(took>=LXZP_WAIT_MS && took<LXZP_WAIT_MS+300);}
    assert(lxzp_waited==2 && lxzp_lost==1);
    /* Another thread asks as X4 does, even with the frame's caller. */
    {pthread_t o;assert(!pthread_create(&o,NULL,tzp_other,NULL));pthread_join(o,NULL);assert(tzp_other_result==EBUSY && lxzp_other==1);}
    pthread_join(t,NULL);
    /* No session: as X4 does. */
    stats.active=0;tzp_hold(&t,200);assert(lxzp_enter(&tzp_lock,frame)==EBUSY && lxzp_other==2);pthread_join(t,NULL);
    assert(lxzp_waited==2 && lxzp_lost==1 && lxzp_nested==1 && lxzp_calls==7);
    tzp_reset();
    printf("zone pending: free, busy elsewhere, busy and freed in the wait, busy past it, other thread, no session PASS\n");
}
/* The stub entered as from a site of 0x1161bb0: [rsp+0x50] after the call holds the caller of 0x1161bb0. */
static int tzp_run_stub(const void *stub,pthread_mutex_t *m,const void *caller) {
    int r;
    __asm__ volatile(
        "mov %%rsp,%%r12\n\t"
        "sub $128,%%rsp\n\t"        /* past the red zone */
        "and $-16,%%rsp\n\t"
        "sub $0x50,%%rsp\n\t"       /* aligned at the call, as at the sites */
        "mov %[c],0x48(%%rsp)\n\t"
        "mov %[m],%%rdi\n\t"
        "call *%[s]\n\t"
        "mov %%r12,%%rsp\n\t"
        :"=a"(r):[s]"r"(stub),[m]"r"(m),[c]"r"(caller)
        :"r12","rdi","rsi","rdx","rcx","r8","r9","r10","r11","memory","cc",
         "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15");
    return r;
}
static void test_zone_pending_elf(void) {
    /* The head: push r15; lea rdi,[lock]; push r14..rbx (six pushes); sub rsp,0x18; call trylock. So at each site the
     * caller's return is 0x48 above the stack pointer, 0x50 in the stub. */
    static const unsigned char head[0x1a]={0x41,0x57,0x48,0x8d,0x3d,0x8f,0x76,0xd1,0x02,0x41,0x56,0x41,0x55,0x41,0x54,0x55,0x53,
                                           0x48,0x83,0xec,0x18,0xe8,0xc6,0x63,0x4f,0xff};
    assert(!memcmp(LX(LXZP_FN_VA),head,sizeof(head)));
    for(unsigned i=0;i<LXZP_SITES;i++) assert(lxzp_state(i)==0);
    {int32_t r;memcpy(&r,LX(LXZP_FRAME_CALL_VA)+1,4);assert(LX(LXZP_FRAME_CALL_VA)[0]==0xe8 && LX(LXZP_FRAME_CALL_VA)+5+r==LX(LXZP_FN_VA));}
    assert(lxzp_pins());
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();tzp_reset();
    memset(lxzp_installed,0,sizeof(lxzp_installed));lxzp_restored=0;lxzp_why="-";
    unsigned char lines[LXZP_SITES][32];
    for(unsigned i=0;i<LXZP_SITES;i++) memcpy(lines[i],LX(lxzp_sites[i])-8,32);
    assert(lxzp_arm()==1 && lxzp_installed_count()==(int)LXZP_SITES && lxzp_arm()==1);
    assert(lxzp_check() && lxzp_pins() && lxzp_page_ok());
    for(unsigned i=0;i<LXZP_SITES;i++) {
        int32_t r;unsigned char *s=LX(lxzp_sites[i]);memcpy(&r,s+1,4);
        assert(lxzp_state(i)==1 && s[0]==0xe8 && s+5+r==lxzp_page+LXZP_STUB);
        assert(!memcmp(LX(lxzp_sites[i])-8,lines[i],9) && !memcmp(s+5,lines[i]+13,19)); /* only the rel32 moved */
    }
    {const unsigned char *p=lxzp_page+LXZP_STUB;uint64_t w;
     assert(!memcmp(p,"\x48\x8b\x74\x24\x50\xff\x25\x00\x00\x00\x00",11));memcpy(&w,p+11,8);assert(w==(uint64_t)(uintptr_t)lxzp_enter);}
    /* The stub run: the caller it hands on decides the wait. */
    pthread_t t;
    assert(tzp_run_stub(lxzp_page+LXZP_STUB,&tzp_lock,LX(LXZP_FRAME_RETURN_VA))==0);pthread_mutex_unlock(&tzp_lock);
    tzp_hold(&t,200);
    assert(tzp_run_stub(lxzp_page+LXZP_STUB,&tzp_lock,LX(0x116390cu))==EBUSY && lxzp_nested==1 && !lxzp_waited);
    assert(tzp_run_stub(lxzp_page+LXZP_STUB,&tzp_lock,LX(LXZP_FRAME_RETURN_VA))==0 && lxzp_waited==1 && !lxzp_lost);
    pthread_mutex_unlock(&tzp_lock);pthread_join(t,NULL);
    {char out[200];assert(lxzp_status(out,sizeof(out))>0 && strstr(out,"installed=3/3 why=- calls=3 waited=1 lost=0 nested=1 other=0"));}
    lxzp_restore();
    assert(!lxzp_installed_count() && lxzp_restored && lxzp_pins());
    for(unsigned i=0;i<LXZP_SITES;i++) assert(lxzp_state(i)==0 && !memcmp(LX(lxzp_sites[i])-8,lines[i],32));
    munmap(lxzp_page,GATE_PAGE);lxzp_page=NULL;lxzp_restored=0;lxzp_why="-";tzp_reset();stats.active=0;
    printf("zone pending ELF: the head and pins, three sites to the stub, the stub's caller at [rsp+0x50], restored PASS\n");
}

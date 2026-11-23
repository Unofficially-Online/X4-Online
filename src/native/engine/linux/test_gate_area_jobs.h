/* The area jobs on the Linux gate (gate_area_jobs.h). ELF: the pins and the three calls into QueueJob. Dispatch: a site's
 * job runs at once with a job thread's TLS (time 0, view 0, the keyed word) and the thread's own TLS back; any other
 * job, or one asked without TLS, is queued as X4 queues it. */
static void test_area_jobs_elf(void) {
    for(unsigned i=0;i<LXAJ_SITES;i++) {
        assert(lxaj_pins(&lxaj_sites[i]) && lxaj_state(&lxaj_sites[i])==0);
        assert(((uintptr_t)LX(lxaj_sites[i].call)&15u)+5u<=16u);   /* the rel32 in one 16-byte line */
    }
    puts("area jobs (Linux ELF): pins, the three QueueJob calls, each in one 16-byte line PASS");
}
static int tlaj_queued;
static int tlaj_queue(void *m,void *cb,void *payload,unsigned cat,unsigned prio) {(void)m;(void)cb;(void)payload;(void)cat;(void)prio;++tlaj_queued;return 7;}
static unsigned char *tlaj_tls;static int tlaj_ran;static uint64_t tlaj_seen_rng,tlaj_seen_time;static uint32_t tlaj_seen_view;
static void tlaj_job(void *payload,void *unused) {
    (void)payload;(void)unused;++tlaj_ran;
    memcpy(&tlaj_seen_time,tlaj_tls+GATE_TLS_LINUX_TIME,8);memcpy(&tlaj_seen_rng,tlaj_tls+GATE_TLS_LINUX_RNG,8);memcpy(&tlaj_seen_view,tlaj_tls+GATE_TLS_LINUX_VIEW,4);
}
static void test_area_jobs_dispatch(void) {
    static unsigned char tls[0x3000];unsigned char *t=tls+0x2800;   /* fs-relative fields sit below the TLS pointer */
    tlaj_tls=t;const uint64_t time=0x4040000000000000ull,rng=0x1234,prev=0x77;const uint32_t view=1;
    memcpy(t+GATE_TLS_LINUX_TIME,&time,8);memcpy(t+GATE_TLS_LINUX_RNG,&rng,8);memcpy(t+GATE_TLS_LINUX_PREVIOUS_TIME,&prev,8);memcpy(t+GATE_TLS_LINUX_VIEW,&view,4);
    int saved[LXAJ_SITES];for(unsigned i=0;i<LXAJ_SITES;i++) {saved[i]=lxaj_sites[i].installed;lxaj_sites[i].installed=1;lxaj_sites[i].inlined=0;}
    lxaj_job_override=tlaj_job;lxaj_forwarded=0;tlaj_queued=tlaj_ran=0;
    static unsigned char object[16];void *payload[5]={object,LX(lxaj_sites[0].body),0,0,0};
    /* the area site's job: run at once, keyed word, time and view 0, the thread's TLS back */
    assert(lxaj_queue_impl(tlaj_queue,t,NULL,LX(lxaj_sites[0].thunk),payload,1,2)==0 && tlaj_ran==1 && !tlaj_queued && lxaj_sites[0].inlined==1);
    assert(tlaj_seen_time==0 && tlaj_seen_view==0 && tlaj_seen_rng==lockstep_seed(LXAJ_DOMAIN,stats.issued,1,0,0,0));
    {uint64_t a,b,c;uint32_t d;memcpy(&a,t+GATE_TLS_LINUX_TIME,8);memcpy(&b,t+GATE_TLS_LINUX_RNG,8);memcpy(&c,t+GATE_TLS_LINUX_PREVIOUS_TIME,8);memcpy(&d,t+GATE_TLS_LINUX_VIEW,4);
     assert(a==time && b==rng && c==prev && d==view);}
    /* the second job of the tick takes the next ordinal */
    payload[1]=LX(lxaj_sites[2].body);
    assert(lxaj_queue_impl(tlaj_queue,t,NULL,LX(lxaj_sites[2].thunk),payload,1,0)==0 && lxaj_sites[2].inlined==1 && tlaj_seen_rng==lockstep_seed(LXAJ_DOMAIN,stats.issued,2,0,0,0));
    /* another site's body under this callback, no TLS, an empty payload: queued as X4 queues them */
    payload[1]=LX(lxaj_sites[1].body);
    assert(lxaj_queue_impl(tlaj_queue,t,NULL,LX(lxaj_sites[0].thunk),payload,1,0)==7);
    payload[1]=LX(lxaj_sites[0].body);
    assert(lxaj_queue_impl(tlaj_queue,NULL,NULL,LX(lxaj_sites[0].thunk),payload,1,0)==7);
    payload[0]=NULL;assert(lxaj_queue_impl(tlaj_queue,t,NULL,LX(lxaj_sites[0].thunk),payload,1,0)==7);
    assert(tlaj_queued==3 && lxaj_forwarded==3 && tlaj_ran==2);
    {char out[192];assert(lxaj_status(out,sizeof(out)) && strstr(out,"inlined=1/0/1 forwarded=3"));}
    for(unsigned i=0;i<LXAJ_SITES;i++) {lxaj_sites[i].installed=saved[i];lxaj_sites[i].inlined=0;}
    lxaj_job_override=NULL;lxaj_forwarded=0;lxaj_tick=UINT64_MAX;lxaj_ordinal=0;
    puts("area jobs (Linux): a site's job at once with a job thread's TLS and the keyed word, others queued PASS");
}

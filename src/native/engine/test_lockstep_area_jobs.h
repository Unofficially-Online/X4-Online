/* Jobs of the simulation run at their request (lockstep_area_jobs.h): which jobs are taken, the job thread's TLS
 * context around them and its restoration, the per-tick keys, and what is forwarded. No native code runs. */
static unsigned char *test_aj_tls;
static uint64_t test_aj_seen_time,test_aj_seen_previous,test_aj_seen_rng;
static uint32_t test_aj_seen_view;
static void *test_aj_seen_payload;
static unsigned test_aj_jobs,test_aj_queued;
static void test_aj_job(void *payload,void *scratch) {
    assert(!scratch);++test_aj_jobs;test_aj_seen_payload=payload;
    memcpy(&test_aj_seen_time,test_aj_tls+0x300,8);memcpy(&test_aj_seen_previous,test_aj_tls+0x328,8);
    memcpy(&test_aj_seen_view,test_aj_tls+0x314,4);memcpy(&test_aj_seen_rng,test_aj_tls+0x318,8);
}
static int test_aj_queue(void *mgr,void *cb,void *payload,unsigned cat,unsigned prio) {
    (void)mgr;(void)cb;(void)payload;assert(cat==1 && prio==0);++test_aj_queued;return 7;
}
static void test_area_jobs(void) {
    static unsigned char object[0x20],tls[0x400];void *payload[5];
    const uint64_t time=0x401c772015d867c4ULL,previous=0x401c660f04c756b3ULL,rng=0x1234567890abcdefULL;const uint32_t view=1;
    const uint64_t saved_issued=stats.issued;
    test_aj_tls=tls;memset(tls,0,sizeof(tls));
    memcpy(tls+0x300,&time,8);memcpy(tls+0x328,&previous,8);memcpy(tls+0x314,&view,4);memcpy(tls+0x318,&rng,8);
    assert(AREAJ_SITES==3 && !areaj_installed_count());
    for(unsigned i=0;i<AREAJ_SITES;i++) {areaj_sites[i].installed=1;areaj_sites[i].inlined=0;}
    areaj_job_test=test_aj_job;areaj_tick=UINT64_MAX;areaj_forwarded=areaj_failed=0;test_aj_jobs=test_aj_queued=0;stats.issued=5;
    #define AJ_CALL(cb) areaj_queue_impl(test_aj_queue,tls,NULL,(void*)(cb),payload,1,0)
    /* Each site's job (its thunk as the callback, its body in the payload) runs at once with a job thread's context:
     * no synced game time, the published view, a random stream keyed by the tick and the job's place in it. */
    for(unsigned i=0;i<AREAJ_SITES;i++) {
        payload[0]=object;payload[1]=image+areaj_sites[i].body;payload[2]=payload[3]=payload[4]=NULL;
        assert(AJ_CALL(image+areaj_sites[i].thunk)==0 && test_aj_jobs==i+1 && !test_aj_queued && test_aj_seen_payload==payload);
        assert(!test_aj_seen_time && !test_aj_seen_previous && !test_aj_seen_view &&
               test_aj_seen_rng==lockstep_seed(AREAJ_DOMAIN,5,i+1,0,0,0));
        assert(!memcmp(tls+0x300,&time,8) && !memcmp(tls+0x328,&previous,8) && !memcmp(tls+0x314,&view,4) && !memcmp(tls+0x318,&rng,8));
        assert(areaj_sites[i].inlined==1);
    }
    /* The next tick starts its keys again. */
    stats.issued=6;assert(AJ_CALL(image+areaj_sites[2].thunk)==0 && test_aj_seen_rng==lockstep_seed(AREAJ_DOMAIN,6,1,0,0,0));
    assert(areaj_sites[2].inlined==2 && test_aj_jobs==4 && !areaj_forwarded && !areaj_failed);
    /* Everything else goes to X4's queue unchanged: another callback, one site's thunk with another site's body, no
     * object, a site that is not in, a thread without the game's context. */
    assert(AJ_CALL(test_aj_job)==7 && test_aj_queued==1);
    payload[1]=image+areaj_sites[0].body;assert(AJ_CALL(image+areaj_sites[2].thunk)==7 && test_aj_queued==2);
    payload[1]=image+areaj_sites[2].body;payload[0]=NULL;assert(AJ_CALL(image+areaj_sites[2].thunk)==7 && test_aj_queued==3);
    payload[0]=object;areaj_sites[2].installed=0;assert(AJ_CALL(image+areaj_sites[2].thunk)==7 && test_aj_queued==4);
    areaj_sites[2].installed=1;
    assert(areaj_queue_impl(test_aj_queue,NULL,NULL,image+areaj_sites[2].thunk,payload,1,0)==7 && test_aj_queued==5);
    assert(test_aj_jobs==4 && areaj_forwarded==5 && !areaj_failed);
    char out[200];assert(areaj_status(out,sizeof(out))>0 && !strcmp(out,"installed=3/3 why=- inlined=1/1/2 forwarded=5 failed=0"));
    #undef AJ_CALL
    for(unsigned i=0;i<AREAJ_SITES;i++) {areaj_sites[i].installed=0;areaj_sites[i].inlined=0;}
    areaj_job_test=NULL;areaj_tick=UINT64_MAX;areaj_forwarded=areaj_failed=0;stats.issued=saved_issued;
}

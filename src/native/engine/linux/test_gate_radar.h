/* The battle fixture's radar units (gate_gravidar.h): a bound actor's unit in a gated Gravidar frame applies on its
 * "RADARUNI" word by call ordinal and gets the thread's word back, ahead of key_all; another unit keeps key_all's path;
 * with the initial refresh armed an unregistered unit is a failure and runs natively. Included by test_gate_linux.c. */
static unsigned char trd_unit[0x100],trd_other[0x100],trd_ship[0x300];
static uint64_t trd_seen;
static int trd_calls;
static void trd_native(void *unit,void *queue,void *result) {(void)unit;(void)queue;(void)result;++trd_calls;uint64_t *w=gate_tls_rng(gate_tls_self());trd_seen=*w;*w+=7;}
static void test_radar_fixture_linux(void) {
    static lxb_actor saved[LXB_ACTORS];const unsigned saved_bound=lxb_bound;memcpy(saved,lxb_actors,sizeof(saved));memset(lxb_actors,0,sizeof(lxb_actors));
    reset_session();stats.active=1;boot_phase=3;stats.issued=9;ui_thread=GetCurrentThreadId();
    const uint64_t runtime=0x9003,other=0x9999;
    memcpy(trd_ship+8,&runtime,8);memcpy(trd_unit+8,&runtime,8);memcpy(trd_other+8,&other,8);
    lxb_bound=LXB_ACTORS;lxb_actors[2].object=trd_ship;lxb_actors[2].runtime=runtime;
    assert(lxb_fixture_runtime(runtime)==3 && !lxb_fixture_runtime(other) && lxg_fixture_unit(trd_unit)==3);
    lxg_native_override=trd_native;memset(lxg_fixture_tick,0,sizeof(lxg_fixture_tick));memset(lxg_initialized,0,sizeof(lxg_initialized));
    lxg_seeded=1;lxg_initial=0;__atomic_store_n(&gravidar_in_frame,1,__ATOMIC_SEQ_CST);__atomic_store_n(&gravidar_key_all,0,__ATOMIC_SEQ_CST);
    uint64_t *word=gate_tls_rng(gate_tls_self());*word=0x4444;const int64_t failures=gravidar_failures;
    gravidar_unit(trd_unit,NULL,NULL);
    assert(trd_calls==1 && trd_seen==lockstep_seed(0x5241444152554e49ULL,9,3,0,1,0) && *word==0x4444 && lxg_fixture_calls[2]==1);
    gravidar_unit(trd_unit,NULL,NULL);assert(trd_seen==lockstep_seed(0x5241444152554e49ULL,9,3,0,2,0) && *word==0x4444);
    gravidar_unit(trd_other,NULL,NULL);assert(trd_calls==3 && trd_seen==0x4444); /* not a fixture unit: key_all off, native */
    lxg_initial=1;gravidar_unit(trd_unit,NULL,NULL);assert(gravidar_failures==failures+1 && trd_calls==4);  /* not registered */
    lxg_initialized[2]=trd_unit;gravidar_unit(trd_unit,NULL,NULL);assert(gravidar_failures==failures+1 && trd_seen==lockstep_seed(0x5241444152554e49ULL,9,3,0,3,0));
    __atomic_store_n(&gravidar_in_frame,0,__ATOMIC_SEQ_CST);*word=0x5555;gravidar_unit(trd_unit,NULL,NULL);assert(trd_seen==0x5555); /* outside a gated frame */
    gravidar_failures=failures;lxg_seeded=lxg_initial=0;lxg_native_override=NULL;memset(lxg_initialized,0,sizeof(lxg_initialized));trd_calls=0;
    memcpy(lxb_actors,saved,sizeof(saved));lxb_bound=saved_bound;reset_session();
    printf("radar fixture: RADARUNI words by call ordinal ahead of key_all, the word back, an unregistered unit a failure PASS\n");
}

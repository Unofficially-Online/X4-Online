/* The tempzone order (gate_tempzone_order.h): the request wrapper (sites, thread classes, misses noted, found zones,
 * other code's creates, errno kept), the parked slot around a joined pass (RunPass's clear on the published view, park,
 * unpark, a slot changed meanwhile, an unjoined park), the grant (smallest request by sector key and position whatever
 * the job order, the set digest, TLS game time 0 and a keyed word during the call and both restored, the slot store with
 * its reference), a pending zone and an overflowing or vanished request deferring the grant, a grant finding an active
 * zone, arming refusals ('observe' is not ported), status and row bounds; on the real executable the five calls, the
 * slot's references, RunPass's clear, the helpers, tamper refusals and the arm, check and restore of the three
 * redirected calls. Included by test_gate_linux.c. */
typedef struct {unsigned char bytes[0x300];} ttz_zone_t;
static ttz_zone_t ttz_made,ttz_existing;
static int64_t ttz_result_create,ttz_result_find;
static unsigned ttz_calls,ttz_releases,ttz_draws,ttz_locks,ttz_unlocks;
static void *ttz_sector_seen;static uint32_t ttz_position_seen[4];static unsigned char ttz_create_seen;
static unsigned char *ttz_tls;static double ttz_time_seen;static uint64_t ttz_word_seen;
static void ttz_lock(void *m) {assert(m==LX(LX_TZ_LOCK_VA));++ttz_locks;}
static void ttz_unlock(void *m) {assert(m==LX(LX_TZ_LOCK_VA));++ttz_unlocks;}
static uint64_t ttz_step(uint64_t s) {s=s*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return (s>>30)|(s<<34);}
static int64_t *ttz_original(void *sector,int64_t *out,const void *position,unsigned char create) {
    ++ttz_calls;ttz_sector_seen=sector;memcpy(ttz_position_seen,position,16);ttz_create_seen=create;
    if(ttz_tls && create) { /* only the grant runs in the MoveWorker context */
        memcpy(&ttz_time_seen,gate_tls_time(ttz_tls),8);memcpy(&ttz_word_seen,gate_tls_rng(ttz_tls),8);uint64_t w=ttz_word_seen;
        for(unsigned i=0;i<ttz_draws;i++) w=ttz_step(w);
        memcpy(gate_tls_rng(ttz_tls),&w,8);
    }
    *out=create?ttz_result_create:ttz_result_find;
    if(*out) InterlockedIncrement((volatile LONG*)((unsigned char*)*out+0x40));
    errno=1234;return out;
}
static void ttz_release(int64_t *ref) {++ttz_releases;if(*ref) __atomic_sub_fetch((volatile LONG*)((unsigned char*)*ref+0x40),1,__ATOMIC_SEQ_CST);*ref=0;}
static LONG ttz_count(const ttz_zone_t *z) {LONG c;memcpy(&c,z->bytes+0x40,4);return c;}
static void ttz_zone(ttz_zone_t *z,uint64_t runtime,int active) {
    memset(z,0,sizeof(*z));memcpy(z->bytes+8,&runtime,8);LONG one=1;memcpy(z->bytes+0x44,&one,4);
    for(unsigned i=0;i<4;i++) {uint32_t v=active?0x10000:0;memcpy(z->bytes+0x11c+i*0x80,&v,4);}
}
static void ttz_reset(void) {
    tz_mode=TZ_OFF;tz_installed=tz_restored=tz_blocked=tz_open=0;tz_active=0;tz_saved_zone=0;tz_saved_flag=0;
    tz_noted=tz_rows=tz_failures=tz_finds=tz_misses=tz_creates=tz_unblocked=tz_other_creates=tz_other_made=tz_other_in_pass=tz_outside=0;
    tz_passes=tz_blocks=tz_opens=tz_cleared=tz_granted=tz_stored=tz_found=tz_waits=tz_overflow=tz_requests=tz_max=tz_drew=tz_unmapped=tz_gone=0;
    memset(tz_candidates,0,sizeof(tz_candidates));tz_last_ret=0;tz_original_override=NULL;tz_release_override=NULL;tz_stage="idle";
}
static volatile LONG *ttz_flag(void) {return (volatile LONG*)LX(LX_TZ_FLAG_VA);}
static volatile int64_t *ttz_slot(void) {return (volatile int64_t*)LX(LX_TZ_ZONE_VA);}
static void ttz_view(uint32_t index) {memcpy(LX(LX_TZ_VIEW_VA),&index,4);}
static void ttz_writable(uint32_t va) {
    uintptr_t page=(uintptr_t)LX(va)&~(uintptr_t)(GATE_PAGE-1);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));
}
/* One request call of a job (the thread classes come from ui_thread/main_thread/path_pass_open). */
static void ttz_request(uint32_t site,void *sector,const float position[4],unsigned char create) {
    int64_t out=0;assert(tz_find_run(ttz_original,(uintptr_t)image+site,sector,&out,position,create)==&out);
}
static unsigned ttz_target_calls;static uintptr_t ttz_target_position;static float ttz_target_seen[4];
static void *ttz_target_sector;static int64_t *ttz_target_out;static unsigned ttz_target_create;
static int64_t *ttz_target(int64_t *out,void *sector,const void *position,unsigned create) {
    ++ttz_target_calls;ttz_target_position=(uintptr_t)position;memcpy(ttz_target_seen,position,16);
    ttz_target_sector=sector;ttz_target_out=out;ttz_target_create=create;*out=0;return out;
}
static void ttz_aligned_check(void) {
    lxtz_find_target=ttz_target;assert(tz_original()==lxtz_native || tz_original_override);
    /* The table's 40-byte entries: of two neighbours, one position is 8 mod 16. */
    unsigned odd=((uintptr_t)tz_candidates[0].position&15u)?0u:1u;
    assert(sizeof(tz_candidate_t)%16u==8u && ((uintptr_t)tz_candidates[odd].position&15u)==8u);
    const float p[4]={7,-8,9,0};memcpy(tz_candidates[odd].position,p,16);
    static unsigned char sector[0x40];int64_t made=-1;
    assert(lxtz_native(sector,&made,tz_candidates[odd].position,1)==&made && ttz_target_calls==1);
    assert(!(ttz_target_position&15u) && !memcmp(ttz_target_seen,p,16) && ttz_target_sector==sector && ttz_target_out==&made &&
           ttz_target_create==1 && made==0);
    /* An already aligned caller's vector also arrives aligned and unchanged; create=0 passes through. */
    float q[4] __attribute__((aligned(16)))={1,2,3,4};
    assert(lxtz_native(sector,&made,q,0)==&made && ttz_target_calls==2 && !(ttz_target_position&15u) && !memcmp(ttz_target_seen,q,16) && !ttz_target_create);
    lxtz_find_target=NULL;ttz_target_calls=0;memset(tz_candidates,0,sizeof(tz_candidates));
}
static void ttz_pass(unsigned char *sectors[3],const float positions[3][4],const unsigned order[3]) {
    assert(tz_before_release());
    for(unsigned i=0;i<3;i++) ttz_request(order[i]&1?TZ_SWITCH_FIND:TZ_PATH_FIND,sectors[order[i]],positions[order[i]],0);
}
static void test_tempzone_order_linux(void) {
    reset_session();
    DWORD self=GetCurrentThreadId();uint64_t other_site=LX_TZ_OTHER_CALL_VA+5u-LX_IMAGE_BASE;
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);
    world_identity_count=0;world_identity_sealed=0;
    /* Sector keys run opposite to their runtime IDs: the order is the portable one. */
    assert(world_identity_record(0x5c0001,0x30) && world_identity_record(0x5c0002,0x20) && world_identity_record(0x5c0003,0x10));
    ttz_writable(LX_TZ_FLAG_VA);ttz_writable(LX_TZ_ZONE_VA);ttz_writable(LX_TZ_LOCK_VA);ttz_writable(LX_TZ_VIEW_VA);
    *ttz_flag()=0;*ttz_slot()=0;ttz_view(1);
    ttz_reset();tz_release_override=ttz_release;tz_original_override=ttz_original;lxtz_lock=ttz_lock;lxtz_unlock=ttz_unlock;
    static unsigned char sector_a[0x40],sector_b[0x40],sector_c[0x40],stray[0x40];
    uint64_t id;id=0x5c0001;memcpy(sector_a+8,&id,8);id=0x5c0002;memcpy(sector_b+8,&id,8);id=0x5c0003;memcpy(sector_c+8,&id,8);
    id=0x5c0999;memcpy(stray+8,&id,8);
    unsigned char *sectors[3]={sector_a,sector_b,sector_c};
    const float positions[3][4]={{100,0,60000,0},{-5,7,1,0},{3,-90000,2,0}};
    float nearby[4]={1,1,1,0};
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=1989;
    /* Inactive: the original only. */
    ui_thread=self+1;main_thread=self+2;path_pass_open=1;ttz_result_find=0;
    ttz_request(TZ_PATH_FIND,sector_a,nearby,0);assert(ttz_calls==1 && !tz_finds && !tz_noted && !tz_last_ret);
    /* Order: a job's create=0 miss is noted (errno as the original left it), a found zone is not. */
    tz_mode=TZ_ORDER;tz_installed=1;tz_active=1;
    errno=77;ttz_request(TZ_PATH_FIND,sector_a,nearby,0);assert(errno==1234);
    assert(tz_finds==1 && tz_misses==1 && tz_noted==1 && tz_candidates[0].sector==sector_a && tz_candidates[0].runtime==0x5c0001 &&
           tz_candidates[0].site==TZ_PATH_FIND && !memcmp(tz_candidates[0].position,nearby,16) && tz_last_ret==(uintptr_t)image+TZ_PATH_FIND);
    ttz_zone(&ttz_existing,0x77001,1);ttz_result_find=(int64_t)&ttz_existing;
    ttz_request(TZ_SWITCH_FIND,sector_b,nearby,0);assert(tz_finds==2 && tz_misses==1 && tz_noted==1);
    /* The region caller: its create=1 counted as other, its lookups not; a made zone also logged. */
    ttz_result_find=0;ttz_request((uint32_t)other_site,sector_b,nearby,1);ttz_request((uint32_t)other_site,sector_b,nearby,0);
    assert(tz_other_creates==1 && tz_finds==2);
    path_pass_open=0;ttz_zone(&ttz_made,0x99001,0);ttz_result_create=(int64_t)&ttz_made;LONG64 other_rows=tz_rows;
    ttz_request((uint32_t)other_site,sector_c,nearby,1);assert(tz_other_creates==2 && tz_other_made==1 && !tz_other_in_pass && tz_rows==other_rows+1);
    {char other[384];snprintf(other,sizeof(other),"kind=other site=%llx thread=other sector=s:0000000000000010 pos=3f800000,3f800000,3f800000 "
        "zone=0000000000099001 active=0 n=0 set=0000000000000000 draws=0",(unsigned long long)other_site);
     char row[384];assert(tz_row((uint64_t)other_rows,row,sizeof(row))>0 && !strcmp(strstr(row,"kind="),other));}
    path_pass_open=1;ttz_request((uint32_t)other_site,sector_c,nearby,1);assert(tz_other_made==2 && tz_other_in_pass==1);
    ttz_result_create=0;tz_rows=other_rows;tz_other_creates=1;tz_other_made=tz_other_in_pass=0;
    path_pass_open=0;ttz_request(TZ_PATH_FIND,sector_b,nearby,0);assert(tz_outside==1 && tz_noted==1); /* a job outside a joined pass */
    path_pass_open=1;ui_thread=self;ttz_request(TZ_PATH_FIND,sector_b,nearby,0);assert(tz_outside==2); /* the MoveWorker */
    ui_thread=self+1;
    /* A pass: free slot -> parked; three misses in some job order -> the grant of the smallest (sector key 0x10 = sector_c). */
    ttz_zone(&ttz_made,0x88001,0);ttz_result_create=(int64_t)&ttz_made;ttz_tls=gate_tls_self();ttz_draws=3;
    double now=1989.5;memcpy(gate_tls_time(ttz_tls),&now,8);uint64_t word=0xabcdef;memcpy(gate_tls_rng(ttz_tls),&word,8);
    const unsigned order1[3]={0,1,2},order2[3]={2,0,1};
    unsigned locks=ttz_locks;
    ttz_pass(sectors,positions,order1);
    assert(tz_blocked && tz_open && *ttz_flag()==1 && !*ttz_slot() && tz_noted==3 && tz_passes==1 && tz_blocks==1 && tz_opens==1);
    assert(ttz_locks==locks+1 && ttz_unlocks==ttz_locks);
    unsigned calls=ttz_calls;
    assert(tz_after_join_with(ttz_tls) && !tz_blocked && ttz_calls==calls+1 && ttz_create_seen==1 && ttz_sector_seen==sector_c &&
           !memcmp(ttz_position_seen,positions[2],16) && ttz_unlocks==ttz_locks);
    uint64_t seed=lockstep_seed(TZ_DOMAIN,1989,0x10,((uint64_t)0x40400000u<<32)|0xc7afc800u,0x40000000u,WORLD_KEY_SAVED);
    assert(ttz_time_seen==0.0 && ttz_word_seen==seed);
    {double t;uint64_t w;memcpy(&t,gate_tls_time(ttz_tls),8);memcpy(&w,gate_tls_rng(ttz_tls),8);assert(t==1989.5 && w==0xabcdef);}
    {LONG state;memcpy(&state,ttz_made.bytes+0x44,4);assert(*ttz_slot()==(int64_t)&ttz_made && *ttz_flag()==1 && ttz_count(&ttz_made)==1 && state==2);}
    assert(tz_granted==1 && tz_stored==1 && tz_drew==1 && tz_rows==1 && tz_requests==3 && tz_max==3);
    char row[384];assert(tz_row(0,row,sizeof(row))>0);
    uint64_t set=0;
    for(unsigned i=0;i<3;i++) {
        uint32_t bits[3];memcpy(bits,positions[i],12);uint64_t key=0x30-0x10*i;
        set+=lockstep_mix(lockstep_seed(TZ_DOMAIN,key,((uint64_t)bits[0]<<32)|bits[1],bits[2],i&1?TZ_SWITCH_FIND:TZ_PATH_FIND,0));
    }
    {char expect[384];snprintf(expect,sizeof(expect),"index=0 tick=1989 kind=grant site=%x thread=moveworker sector=s:0000000000000010 "
        "pos=40400000,c7afc800,40000000 zone=0000000000088001 active=0 n=3 set=%016llx draws=3",TZ_PATH_FIND,(unsigned long long)set);
     assert(!strcmp(row,expect));}
    /* Pending (not active): the next pass is parked and not open; its requests wait; the slot comes back. */
    stats.issued=1990;ttz_pass(sectors,positions,order2);
    assert(tz_blocked && !tz_open && *ttz_flag()==1 && !*ttz_slot() && tz_saved_zone==(uint64_t)&ttz_made);
    calls=ttz_calls;assert(tz_after_join_with(ttz_tls) && ttz_calls==calls && *ttz_slot()==(int64_t)&ttz_made && *ttz_flag()==1);
    assert(tz_waits==1 && tz_row(1,row,sizeof(row))>0 && strstr(row," tick=1990 kind=wait ") && strstr(row," zone=0000000000000000 active=0 n=3 set="));
    {char expect[32];snprintf(expect,sizeof(expect)," set=%016llx ",(unsigned long long)set);assert(strstr(row,expect));} /* the job order is not in it */
    /* Active in the published view: the next pass clears it as RunPass would (reference released), then it is open. */
    stats.issued=1991;for(unsigned i=0;i<4;i++) {uint32_t v=i==1?0x10000:0;memcpy(ttz_made.bytes+0x11c+i*0x80,&v,4);}
    unsigned releases=ttz_releases;assert(tz_before_release() && tz_cleared==1 && tz_open && ttz_releases==releases+1 && !ttz_count(&ttz_made));
    assert(*ttz_flag()==1 && !*ttz_slot() && !tz_saved_flag && !tz_saved_zone);
    assert(tz_after_join_with(ttz_tls) && !*ttz_flag() && !*ttz_slot());
    /* The active test: the published index, the zone's +0x1e6 bit 1 and the view word's 0x40000. */
    ttz_view(2);assert(!tz_active_view(ttz_made.bytes));ttz_view(1);assert(tz_active_view(ttz_made.bytes));
    {uint32_t f=0x20000;memcpy(ttz_made.bytes+0x1e4,&f,4);assert(!tz_active_view(ttz_made.bytes));f=0;memcpy(ttz_made.bytes+0x1e4,&f,4);}
    ttz_made.bytes[0x11c+0x80+2]|=4;assert(!tz_active_view(ttz_made.bytes));
    ttz_view(7);assert(!tz_active_view(ttz_existing.bytes));ttz_view(1);
    /* A different job order gives the same grant and set. */
    ttz_zone(&ttz_made,0x88002,0);stats.issued=1992;ttz_pass(sectors,positions,order2);LONG64 rows=tz_rows;
    assert(tz_after_join_with(ttz_tls) && ttz_sector_seen==sector_c && tz_rows==rows+1 && tz_row((uint64_t)rows,row,sizeof(row))>0);
    {char expect[64];snprintf(expect,sizeof(expect)," n=3 set=%016llx draws=3",(unsigned long long)set);assert(strstr(row,expect) && strstr(row," kind=grant "));}
    /* The grant finds an active zone: counted, released, nothing stored. */
    *ttz_slot()=0;*ttz_flag()=0;ttz_zone(&ttz_existing,0x77002,1);ttz_result_create=(int64_t)&ttz_existing;
    stats.issued=1993;ttz_pass(sectors,positions,order1);assert(tz_after_join_with(ttz_tls) && tz_found==1 && !*ttz_slot() && !*ttz_flag());
    assert(!ttz_count(&ttz_existing) && tz_row((uint64_t)(tz_rows-1),row,sizeof(row))>0 && strstr(row," zone=0000000000077002 active=1 "));
    /* No request: no row; a vanished sector (its ID changed) and an overflowing pass wait. */
    ttz_result_create=(int64_t)&ttz_made;rows=tz_rows;assert(tz_before_release() && tz_after_join_with(ttz_tls) && tz_rows==rows);
    assert(tz_before_release());ttz_request(TZ_PATH_FIND,sector_a,nearby,0);id=0x5c0042;memcpy(sector_a+8,&id,8);
    calls=ttz_calls;assert(tz_after_join_with(ttz_tls) && ttz_calls==calls && tz_gone==1);id=0x5c0001;memcpy(sector_a+8,&id,8);
    assert(tz_before_release());for(unsigned i=0;i<TZ_CANDIDATES+1;i++) ttz_request(TZ_PATH_FIND,sector_b,nearby,0);
    calls=ttz_calls;assert(tz_noted==TZ_CANDIDATES+1 && tz_after_join_with(ttz_tls) && ttz_calls==calls && tz_overflow==1 && !*ttz_flag());
    assert(tz_row((uint64_t)(tz_rows-1),row,sizeof(row))>0 && strstr(row," kind=wait ") && strstr(row," n=257 "));
    /* An unmapped sector keys by its runtime ID (counted); no TLS leaves the word alone. */
    assert(tz_before_release());ttz_request(TZ_PATH_FIND,stray,nearby,0);unsigned long long unmapped=tz_unmapped;
    assert(tz_after_join_with(NULL) && tz_unmapped==unmapped+1 && ttz_sector_seen==stray && *ttz_slot()==(int64_t)&ttz_made);
    assert(tz_row((uint64_t)(tz_rows-1),row,sizeof(row))>0 && strstr(row," sector=-:00000000005c0999 ") && strstr(row," draws=0"));
    *ttz_slot()=0;*ttz_flag()=0;ttz_zone(&ttz_made,0x88003,0);
    /* A slot changed during the parked pass (a job's create=1 would leave its zone there): fail; an unjoined park refuses the next release. */
    assert(tz_before_release());*ttz_slot()=(int64_t)&ttz_made;
    assert(!tz_after_join_with(ttz_tls) && !strcmp(tz_stage,"slot_changed") && !tz_blocked && *ttz_slot()==(int64_t)&ttz_made);
    *ttz_slot()=0;*ttz_flag()=0;
    assert(tz_before_release() && !tz_before_release() && !strcmp(tz_stage,"unjoined"));
    /* lxtz_before_release counts the failure for the stop. */
    LONG64 failures=tz_failures;assert(!lxtz_before_release() && tz_failures==failures+1);
    tz_restore();assert(!tz_blocked && !*ttz_flag() && !*ttz_slot() && !tz_active);
    /* Status and rows at the widest values; eviction. */
    char status[896];
    tz_mode=TZ_ORDER;tz_noted=tz_rows=tz_failures=tz_finds=tz_misses=tz_creates=tz_unblocked=tz_other_creates=tz_other_made=tz_other_in_pass=tz_outside=INT64_MAX;
    tz_passes=tz_blocks=tz_opens=tz_cleared=tz_granted=tz_stored=tz_found=tz_waits=tz_overflow=tz_requests=tz_max=tz_drew=tz_unmapped=tz_gone=UINT64_MAX;
    tz_restored=1;tz_stage="grant_exception";int n=tz_status(status,sizeof(status));
    assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE TEMPZONE_ORDER_STOP \n")<896 && strstr(status,"mode=order "));
    tz_rows=1;tz_ring[0]=(tz_row_t){UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,{UINT32_MAX,UINT32_MAX,UINT32_MAX},UINT32_MAX,UINT32_MAX,UINT32_MAX,
        TZ_ROW_CREATE,PATH_THREAD_MOVEWORKER,WORLD_KEY_CREATED,255};
    n=tz_row(0,row,sizeof(row));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE TEMPZONE_ROW \n")<384);
    tz_rows=TZ_RING+2;assert(tz_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1") && !tz_row(TZ_RING+2,row,sizeof(row)));
    /* The native adapter hands the query a 16-byte aligned copy of the position, whatever the caller's alignment
     * (lx12a: a grant from candidate 1, position at 8 mod 16, faulted on the zone builder's movaps). */
    ttz_aligned_check();
    /* Arming: 'order' only, before the first admitted frame, on the MoveWorker, the pathing join, the identities; pins (the synthetic image refuses). */
    ttz_reset();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=self;world_identity_installed=1;
    assert(!tz_arm(TZ_OFF,1) && !tz_arm(9,1) && !tz_arm(TZ_ORDER,2));
    assert(!tz_arm(TZ_OBSERVE,1) && !strcmp(tz_stage,"observe_unported"));
    stats.issued=1;assert(!tz_arm(TZ_ORDER,1));stats.issued=0;
    ui_thread=self+1;assert(!tz_arm(TZ_ORDER,1));ui_thread=self;
    boot_phase=1;assert(!tz_arm(TZ_ORDER,1));boot_phase=0;
    world_identity_installed=0;assert(!tz_arm(TZ_ORDER,1) && !strcmp(tz_stage,"needs"));world_identity_installed=1;
    assert(!tz_arm(TZ_ORDER,1) && !strcmp(tz_stage,"pins") && !tz_installed && !tz_active);
    ttz_reset();ttz_tls=NULL;memset(tz_ring,0,sizeof(tz_ring));lxtz_lock=lxtz_unlock=NULL;
    *ttz_flag()=0;*ttz_slot()=0;ttz_view(0);
    world_identity_installed=0;world_identity_count=0;free(world_identities);world_identities=NULL;path_pass_open=0;
    printf("tempzone order wrapper/park/grant/order-independence/pending/clear/overflow/arming/status PASS\n");
}
/* RIP-relative operand at va (instruction length len, disp32 in its last four bytes) points at target. */
static int ttz_rip(uint32_t va,unsigned len,uint32_t target) {
    int32_t d;memcpy(&d,LX(va+len-4),4);return (uint32_t)((int64_t)va+len+d)==target;
}
static int ttz_call(uint32_t va,uint32_t target) {
    int32_t d;memcpy(&d,LX(va+1),4);return *LX(va)==0xe8 && (uint32_t)((int64_t)va+5+d)==target;
}
/* The real executable: the request calls, the slot, RunPass's clear, the helpers, tamper refusals, arm/check/restore. */
static void test_tempzone_order_elf(void) {
    assert(lxtz_pins());
    /* The five calls of the query, the request calls with create in ecx (mov ecx,1 / xor ecx,ecx just before). */
    assert(ttz_call(0x14490be,LX_TZ_FIND_VA) && !memcmp(LX(0x14490b6),"\xb9\x01\x00\x00\x00",5) && LX_TZ_PATH_CREATE_RET_VA==0x14490be + 5);
    assert(ttz_call(LX_TZ_PATH_FIND_CALL_VA,LX_TZ_FIND_VA) && !memcmp(LX(0x1449601),"\x31\xc9",2));
    assert(ttz_call(0x142fa2e,LX_TZ_FIND_VA) && !memcmp(LX(0x142fa19),"\xb9\x01\x00\x00\x00",5) && LX_TZ_SWITCH_CREATE_RET_VA==0x142fa2e + 5);
    assert(ttz_call(LX_TZ_SWITCH_FIND_CALL_VA,LX_TZ_FIND_VA) && !memcmp(LX(0x142fb32),"\x31\xc9",2));
    assert(ttz_call(LX_TZ_OTHER_CALL_VA,LX_TZ_FIND_VA));
    /* The create=1 calls' rel32 cross a 16-byte line (not patched); the three others lie in one. */
    assert(((0x14490be + 1)&15u)>12u && ((0x142fa2e + 1)&15u)>12u);
    for(unsigned i=0;i<LXTZ_SITES;i++) assert(lxtz_sites[i].call_va+1-lxtz_sites[i].block_va<=12u);
    /* The slot: the flag test, the lock, the store and the flag set in both request blocks; RunPass's test and clear. */
    assert(ttz_rip(0x144907d,6,LX_TZ_FLAG_VA) && ttz_rip(0x144908b,7,LX_TZ_LOCK_VA) && ttz_rip(0x144909a,6,LX_TZ_FLAG_VA));
    assert(ttz_rip(0x1449597,7,LX_TZ_ZONE_VA) && ttz_rip(0x14495b1,6,LX_TZ_FLAG_VA));
    assert(ttz_rip(0x142f9e7,6,LX_TZ_FLAG_VA) && ttz_rip(0x142f9f5,7,LX_TZ_LOCK_VA) && ttz_rip(0x142fc4a,7,LX_TZ_ZONE_VA) && ttz_rip(0x142fc64,6,LX_TZ_FLAG_VA));
    assert(ttz_rip(0x13e3d78,6,LX_TZ_FLAG_VA) && ttz_rip(0x13e4143,7,LX_TZ_LOCK_VA) && ttz_rip(0x13e4152,7,LX_TZ_ZONE_VA) && ttz_rip(0x13e419a,7,LX_TZ_FLAG_VA));
    assert(ttz_call(0x13e414d,LX_MUTEX_LOCK_VA) && ttz_call(0x13e41a4,LX_MUTEX_UNLOCK_VA) && ttz_call(0x144909c-0x7,LX_MUTEX_LOCK_VA));
    /* The clear's test is Windows': +0x1e6 bit 1, the view word +0x11c + index*0x80, 0x40000 and 0x10000; the index from 0x12021e0. */
    assert(!memcmp(LX(0x13e415e),"\x41\xf6\x85\xe6\x01\x00\x00\x02",8) && ttz_call(0x13e4168,0x12021e0) && ttz_call(0x13e4195,LX_TZ_RESET_VA));
    assert(!memcmp(LX(0x13e4173),"\x41\x8b\x84\x05\x1c\x01\x00\x00",8));
    assert(ttz_rip(0x12021e8,6,LX_TZ_VIEW_VA));
    assert(ttz_call(0x1449114,LX_TZ_ACTIVE_VA) && ttz_call(0x1449584,0x1fa4030) && ttz_call(0x142fa59,LX_TZ_ACTIVE_VA));
    /* The reference copy and reset touch +0x44 and +0x40 (Windows' layout, tz_addref). */
    assert(!memcmp(LX(0x1fa4042),"\xf0\x0f\xb1\x56\x44",5) && !memcmp(LX(0x1fa4047),"\xf0\x83\x46\x40\x01",5));
    assert(!memcmp(LX(0x1dc8df3),"\xf0\x83\x6a\x40\x01",5));
    /* Tamper refusals. */
    unsigned char *at=LX(LX_TZ_FIND_VA+0x40);uintptr_t page=(uintptr_t)at&~(uintptr_t)(GATE_PAGE-1);
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));*at^=1;assert(!lxtz_pins());*at^=1;assert(lxtz_pins());
    at=LX(0x144907d+0x80);page=(uintptr_t)at&~(uintptr_t)(GATE_PAGE-1);
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));*at^=1;assert(!lxtz_pins());*at^=1;
    at=LX(0x13e4143+0x10);page=(uintptr_t)at&~(uintptr_t)(GATE_PAGE-1);
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));*at^=1;assert(!lxtz_pins());*at^=1;assert(lxtz_pins());
    /* Arm: the three calls through the stub; check; a changed block stops; restore puts every byte back. */
    unsigned char before[LXTZ_SITES][16];
    for(unsigned i=0;i<LXTZ_SITES;i++) {assert(lxtz_site_state(i)==0);memcpy(before[i],LX(lxtz_sites[i].block_va),16);}
    ttz_reset();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();world_identity_installed=1;
    assert(!tz_arm(TZ_ORDER,0) && tz_arm(TZ_ORDER,1) && tz_installed && tz_active && tz_owned() && tz_check());
    assert(tz_arm(TZ_ORDER,1) && !tz_arm(TZ_OBSERVE,1));
    for(unsigned i=0;i<LXTZ_SITES;i++) {
        int32_t d;memcpy(&d,LX(lxtz_sites[i].call_va)+1,4);assert(lxtz_site_state(i)==1 && LX(lxtz_sites[i].call_va)+5+d==lxtz_page);
    }
    void *dispatch;memcpy(&dispatch,lxtz_page+6,8);assert(lxtz_page[0]==0xff && lxtz_page[1]==0x25 && dispatch==(void*)lxtz_find);
    at=LX(0x142fb14+0x4);page=(uintptr_t)at&~(uintptr_t)(GATE_PAGE-1);
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));*at^=1;
    assert(!tz_check() && !strcmp(tz_stage,"block_changed"));*at^=1;tz_failures=0;assert(tz_check());
    tz_restore();assert(tz_restored && !tz_installed && !tz_active);
    for(unsigned i=0;i<LXTZ_SITES;i++) assert(lxtz_site_state(i)==0 && !memcmp(before[i],LX(lxtz_sites[i].block_va),16));
    assert(lxtz_pins());
    ttz_reset();world_identity_installed=0;
    printf("tempzone order ELF PASS\n");
}

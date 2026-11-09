/* Physics shape refresh (gate_shape.h): a fixture actor's two records through the wrapper (the due time at time - dt
 * at the first call per holder, the word from "SHAPE_UP" by actor, record and ordinal and back, a holder without an ID
 * a failure), shape_refresh's alignment (the first call per owner key at the thread's time, an unmapped owner left
 * alone) and its interval (native before the latch, 5 s outside the point's sector, the clamp and the keyed draw, the
 * word given back, a degenerate shape); on the real executable the pin, the entry and the je patched and restored.
 * Included by test_gate_linux.c after test_gate_mining_camera.h (its fake components). */
static unsigned char tsh_ship[0x300],tsh_record[3][0x48],tsh_holder[3][0x40],tsh_body[0x10],tsh_shape[0x10];
static double tsh_seen_due;
static uint64_t tsh_seen_word;
static int tsh_calls;
static void tsh_original(void *record,unsigned char flag) {
    (void)flag;++tsh_calls;unsigned char *holder=NULL;memcpy(&holder,(unsigned char*)record+0x20,8);
    uint64_t *word=gate_tls_rng(gate_tls_self());
    memcpy(&tsh_seen_due,holder+0x28,8);tsh_seen_word=*word;*word+=1; /* one draw */
    const double due=99.0;memcpy(holder+0x28,&due,8);
}
static float tsh_range;
static uint64_t tsh_word;
static int tsh_draws;
static float tsh_float(float range) {uint64_t *word=gate_tls_rng(gate_tls_self());tsh_range=range;tsh_word=*word;*word^=1;++tsh_draws;return 0.0625f;}
static float tsh_box[8];
static void tsh_bounds(float *out,void *shape) {(void)shape;memcpy(out,tsh_box,32);}
static void *tsh_shape_vtable[8];
static void tsh_record_set(unsigned i,void *owner,uint64_t holder_id,double due) {
    void *p=owner;memset(tsh_record[i],0,sizeof(tsh_record[i]));memset(tsh_holder[i],0,sizeof(tsh_holder[i]));
    memcpy(tsh_record[i],&p,8);p=tsh_holder[i];memcpy(tsh_record[i]+0x20,&p,8);
    memcpy(tsh_holder[i]+8,&holder_id,8);memcpy(tsh_holder[i]+0x28,&due,8);
}
static void test_shape_linux(void) {
    static lxb_actor saved[LXB_ACTORS];static unsigned saved_slots[LXB_ACTORS];const unsigned saved_bound=lxb_bound;
    memcpy(saved,lxb_actors,sizeof(saved));memcpy(saved_slots,lxb_body_slot,sizeof(saved_slots));
    memset(lxb_actors,0,sizeof(lxb_actors));memset(lxb_body_slot,0,sizeof(lxb_body_slot));
    reset_session();stats.active=1;boot_phase=3;stats.issued=1;ui_thread=GetCurrentThreadId();
    unsigned char *base=gate_tls_self();uint64_t *word=gate_tls_rng(base);const double now=7.15,dt=gate_tick_dt();memcpy(gate_tls_time(base),&now,8);
    /* Actor 5's ship with its two records at +0x258/+0x260. */
    const uint64_t runtime=0x9005;void *p;memset(tsh_ship,0,sizeof(tsh_ship));memcpy(tsh_ship+8,&runtime,8);
    lxb_bound=LXB_ACTORS;lxb_actors[4].object=tsh_ship;lxb_actors[4].runtime=runtime;
    for(unsigned w=0;w<2;w++) {tsh_record_set(w,tsh_ship,0x700+w,12.0);p=tsh_record[w];memcpy(tsh_ship+0x258+w*8,&p,8);}
    unsigned which=9;assert(lxb_fixture_record(tsh_record[1],&which)==5 && which==1 && lxb_fixture_record(tsh_record[0],&which)==5 && !which);
    memset(lxsh_samples,0,sizeof(lxsh_samples));memset(lxsh_initialized,0,sizeof(lxsh_initialized));lxsh_calls=lxsh_inits=0;lxsh_failures=0;
    lxsh_fixture=lxsh_seeded=lxsh_initial=1;lxsh_rc_armed=0;lxsh_original_override=tsh_original;tsh_calls=0;
    *word=0x1111;lxsh_update(tsh_record[0],0);
    assert(tsh_calls==1 && tsh_seen_due==now-dt && tsh_seen_word==lockstep_seed(LXSH_DOMAIN,1,5,0,1,0) && *word==0x1111);
    const lxsh_sample *s0=&lxsh_samples[4][0],*s1=&lxsh_samples[4][1];
    assert(s0->calls==1 && s0->initialized==1 && s0->init_total==1 && s0->last_init==1 && lxsh_inits==1 && s0->due_after==0x4058c00000000000ull);
    lxsh_update(tsh_record[1],1);
    assert(tsh_seen_word==lockstep_seed(LXSH_DOMAIN,1,5,1,1,0) && s1->initialized==1 && s1->flag==1 && lxsh_inits==2);
    lxsh_update(tsh_record[0],0); /* the same holder again: no re-base, the second ordinal */
    assert(tsh_seen_due==99.0 && tsh_seen_word==lockstep_seed(LXSH_DOMAIN,1,5,0,2,0) && s0->calls==2 && !s0->initialized && lxsh_inits==2);
    char text[512];assert(lxsh_text(5,0,text,sizeof(text))>0 && !strncmp(text,"tick=1 calls=2 time=401c99999999999a flag=0 ",44) && !lxsh_text(5,2,text,sizeof(text)));
    {char totals[1024];assert(lxsh_init_totals(totals,sizeof(totals))>0 && strstr(totals,",5/0/1,5/1/1,"));}
    {const uint64_t zero=0;memcpy(tsh_holder[0]+8,&zero,8);lxsh_update(tsh_record[0],0);assert(lxsh_failures==1 && tsh_calls==4);lxsh_failures=0;}
    /* Outside an admitted frame the fixture path stays out. */
    boot_phase=2;*word=0x2222;lxsh_update(tsh_record[1],0);assert(tsh_seen_word==0x2222 && s1->calls==1);boot_phase=3;
    /* shape_refresh's alignment: the first call per owner key (saved), every caller; unmapped owners left alone. */
    world_identities=world_identities?world_identities:calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);
    world_identity_installed=1;world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x9006,0x306) && world_identity_record(0x9007,0x307));
    static tmc_object_t cluster,sector,other,owner,point,stray;
    tmc_vtable[0x11e8/8]=(void*)tmc_is_class;lxmc_gtr_override=tmc_gtr;
    tmc_init(&cluster,0xf,NULL,0,0,0);tmc_init(&sector,0x58,&cluster,0,0,0);tmc_init(&other,0x58,&cluster,0,0,0);
    tmc_init(&owner,0x49,&sector,1000,0,0);owner.id=0x9006;tmc_init(&point,0x49,&sector,1090,0,0);tmc_init(&stray,0x49,&sector,0,0,0);stray.id=0x9999;
    tsh_record_set(2,&owner,0x777,12.0);
    memset((void*)lxsh_keys,0,sizeof(lxsh_keys));lxsh_aligned=lxsh_unmapped=0;lxsh_rc_failures=0;
    lxsh_rc_armed=1;__atomic_store_n(&lxsh_count,-1,__ATOMIC_SEQ_CST);
    lxsh_update(tsh_record[2],0);assert(tsh_seen_due==12.0 && !lxsh_aligned); /* before the first latch */
    tsh_record_set(2,&owner,0x777,12.0);__atomic_store_n(&lxsh_count,1,__ATOMIC_SEQ_CST);
    lxsh_update(tsh_record[2],0);assert(tsh_seen_due==now && lxsh_aligned==1);
    lxsh_update(tsh_record[2],0);assert(tsh_seen_due==99.0 && lxsh_aligned==1);
    tsh_record_set(2,&stray,0x778,12.0);lxsh_update(tsh_record[2],0);assert(tsh_seen_due==12.0 && lxsh_unmapped==1);
    /* The interval: native before the latch; outside the point's sector 5 s without a draw; the clamp, the keyed draw. */
    p=tsh_body;memcpy(tsh_holder[2]+0x10,&p,8);p=tsh_shape;memcpy(tsh_body+8,&p,8);p=tsh_shape_vtable;memcpy(tsh_shape,&p,8);
    tsh_shape_vtable[0x20/8]=(void*)tsh_bounds;lxsh_float_override=tsh_float;
    {const float box[8]={-3,-4,0,0, 3,4,0,0};memcpy(tsh_box,box,32);} /* diagonal 10 */
    double out=-1;lxsh_point=&point;lxsh_sector=&sector;
    __atomic_store_n(&lxsh_count,-1,__ATOMIC_SEQ_CST);assert(!lxsh_interval(tsh_holder[2],&owner,&out));
    __atomic_store_n(&lxsh_count,1,__ATOMIC_SEQ_CST);tsh_draws=0;*word=0x3333;
    assert(lxsh_interval(tsh_holder[2],&owner,&out) && out==5.0-0.0625 && tsh_range==0.1f); /* (90 - 10) / 10 = 8 -> 5 */
    {unsigned klass;assert(tsh_word==lockstep_seed(LXSH_RC_DOMAIN,1,world_identity_key(0x9006,&klass),0,0,0) && *word==0x3333 && tsh_draws==1);}
    point.at[0]=1025;assert(lxsh_interval(tsh_holder[2],&owner,&out) && out==1.5-0.0625);  /* (25 - 10) / 10 */
    point.at[0]=1012;assert(lxsh_interval(tsh_holder[2],&owner,&out) && out==0.5-0.0625);  /* 0.2 -> 0.5 */
    lxsh_sector=&other;tsh_draws=0;assert(lxsh_interval(tsh_holder[2],&owner,&out) && out==5.0 && !tsh_draws);
    lxsh_sector=&sector;memset(tsh_box,0,sizeof(tsh_box));assert(lxsh_interval(tsh_holder[2],&owner,&out) && out==0.5 && !tsh_draws);
    assert(!lxsh_rc_failures);
    {char status[512];assert(lxsh_rc_status(status,sizeof(status))>0 && strstr(status," aligned=1 unmapped=1 "));}
    lxsh_rc_armed=lxsh_fixture=lxsh_seeded=lxsh_initial=0;__atomic_store_n(&lxsh_count,-1,__ATOMIC_SEQ_CST);lxsh_point=lxsh_sector=NULL;
    lxsh_original_override=NULL;lxsh_float_override=NULL;lxmc_gtr_override=NULL;world_identity_installed=0;world_identity_count=0;
    memcpy(lxb_actors,saved,sizeof(saved));memcpy(lxb_body_slot,saved_slots,sizeof(saved_slots));lxb_bound=saved_bound;reset_session();
    printf("shape: fixture records re-based once per holder on SHAPE_UP words, shape_refresh aligned per owner key, Windows' interval rule on a keyed draw PASS\n");
}
static void test_shape_elf(void) {
    unsigned char entry[16],je[16];memcpy(entry,LX(LXSH_UPDATE_VA),16);memcpy(je,(unsigned char*)((uintptr_t)LX(LXSH_JE_VA)&~(uintptr_t)15),16);
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    lxsh_installed=lxsh_restored=lxsh_entry_on=lxsh_je_on=lxsh_fixture=lxsh_rc_armed=0;
    assert(lxsh_pins() && lxsh_entry_state()==0 && lxsh_je_state()==0);
    assert(lxsh_install(1,1) && lxsh_fixture && lxsh_entry_state()==1 && lxsh_je_state()==0 && lxsh_owned() && !lxsh_check());
    assert(!memcmp(LX(LXSH_UPDATE_VA)+5,entry+5,11));
    assert(!memcmp(lxsh_page+LXSH_TRAMP,"\x48\x8b\x47\x20\x48\x85\xc0\x75\x01\xc3\xff\x25",12));
    {void *to;memcpy(&to,lxsh_page+LXSH_TRAMP+16,8);assert(to==(void*)LX(LXSH_RESUME_VA));memcpy(&to,lxsh_page+LXSH_HOOK+6,8);assert(to==(void*)lxsh_update);}
    assert(lxsh_rc_arm() && lxsh_rc_armed && lxsh_je_state()==1 && lxsh_owned() && lxsh_pins() && !lxsh_check());
    {void *to;memcpy(&to,lxsh_page+LXSH_STUB+15,8);assert(to==(void*)lxsh_interval);
     memcpy(&to,lxsh_page+LXSH_STUB+38+6,8);assert(to==(void*)LX(LXSH_DUE_VA));memcpy(&to,lxsh_page+LXSH_STUB+52+6,8);assert(to==(void*)LX(LXSH_DIST_VA));}
    lxsh_restore();
    assert(lxsh_restored && !lxsh_installed && !lxsh_rc_armed && !memcmp(LX(LXSH_UPDATE_VA),entry,16) &&
           !memcmp((unsigned char*)((uintptr_t)LX(LXSH_JE_VA)&~(uintptr_t)15),je,16) && lxsh_pins());
    lxsh_restored=0;stats.active=0;boot_mode=0;
    printf("shape ELF: Body_UpdateShape pinned, the entry to the wrapper and the je to the interval stub, both restored PASS\n");
}

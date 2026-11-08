/* The battle fixture's steering on Linux (gate_flight.h): the random float against golden values of the shared
 * algorithm; the update of a registered fixture on its keyed word (restored after), cheap off with full, the avoidance
 * time stale with refresh, an unregistered model native, a stale Agent retired; the keyed initial angles once per model
 * lifetime; the constructor stub run from a planted site (every caller-saved register kept, the 15 bytes replayed,
 * back at the site's end). On the real executable: the update slot, the site's bytes and the instructions the layout
 * relies on. Included by test_gate_linux.c. */
static uint64_t tfl_seen_rng;static unsigned char tfl_seen_cheap;static unsigned tfl_original_calls;
static void tfl_original(void *self,void *slot,float dt,double time,unsigned char cheap,unsigned char player) {
    (void)self;(void)slot;(void)dt;(void)time;(void)player;
    tfl_seen_rng=*lxf_test_rng;tfl_seen_cheap=cheap;++tfl_original_calls;*lxf_test_rng^=0xffu; /* the native draws */
}
static void *tfl_observed;
static void tfl_observe(unsigned char *model) {tfl_observed=model;}
static void test_flight_linux(void) {
    /* The random float: golden values of ror(s*0x5851f42d4c957f2d+0x14057b7ef767814f,30), (float)(u32)s*2^-32*limit. */
    uint64_t state=0x0123456789abcdefULL;const uint32_t limits[2]={0x40c90fdbu,0x40490fdbu};float limit;
    memcpy(&limit,&limits[0],4);float a=lxf_random(&state,limit);uint32_t bits;memcpy(&bits,&a,4);
    assert(state==0xd77d1548b38cb48cULL && bits==0x408d0499u);
    memcpy(&limit,&limits[1],4);a=lxf_random(&state,limit);memcpy(&bits,&a,4);
    assert(state==0xe8c2c7af9d32f5cfULL && bits==0x3ff6ed7fu);
    memcpy(&limit,&limits[1],4);uint64_t before=state;limit=-limit;assert(lxf_random(&state,limit)==0.0f && state==before);
    /* A fixture: ship (runtime 0x5001), its Agent (+0xf10 the ship, +0xf30 the model), the steering model (+0x8 the Agent). */
    static unsigned char ship[0x40],agent[0x1000],model[0x400],other_model[0x400];
    memset(ship,0,sizeof(ship));memset(agent,0,sizeof(agent));memset(model,0,sizeof(model));memset(other_model,0,sizeof(other_model));
    const uint64_t runtime=0x5001;memcpy(ship+8,&runtime,8);
    void *p=ship;memcpy(agent+LXW_AGENT_OWNER,&p,8);p=model;memcpy(agent+LXF_AGENT_MODEL,&p,8);
    p=agent;memcpy(model+LXF_MODEL_AGENT,&p,8);memcpy(other_model+LXF_MODEL_AGENT,&p,8);
    p=LX(LXF_STEER_VTABLE);memcpy(model,&p,8);memcpy(other_model,&p,8);
    memset(lxf_samples,0,sizeof(lxf_samples));
    assert(!lxf_register(3,ship,runtime,agent)); /* not installed */
    lxf_installed=1;lxf_seeded=1;lxf_full=1;lxf_refresh=1;lxf_initial=1;lxf_inits=0;lxf_registration_inits=0;
    assert(lxf_register(3,ship,runtime,agent) && lxf_samples[2].model==model && lxf_registration_inits==1);
    /* The keyed initial angles: actor_seed("STEERINI", 0, 3, generation 1), the constructor's two limits. */
    uint64_t seed=lockstep_seed(0x5354454552494e49ULL,0,3,1,0,0);float want[2];
    memcpy(&limit,&limits[0],4);want[0]=lxf_random(&seed,limit);memcpy(&limit,&limits[1],4);want[1]=lxf_random(&seed,limit);
    assert(!memcmp(model+LXF_MODEL_AZIMUTH,&want[0],4) && !memcmp(model+LXF_MODEL_POLAR,&want[1],4) && lxf_samples[2].init_generation==1);
    assert(lxf_register(3,ship,runtime,agent) && lxf_samples[2].init_generation==1); /* the same lifetime: no second draw */
    /* The update: keyed on the fixture's word, restored after; cheap off (full), the avoidance time stale (refresh). */
    uint64_t word=0x1111222233334444ULL;lxf_test_rng=&word;lxf_original_override=tfl_original;
    stats.active=1;boot_phase=3;stats.issued=77;tfl_original_calls=0;
    lxf_update(model,agent,0.033f,10.0,1,0);
    assert(tfl_original_calls==1 && tfl_seen_rng==lxf_seed(77,3,1) && tfl_seen_cheap==0 && word==0x1111222233334444ULL);
    double stale;memcpy(&stale,model+LXF_MODEL_AVOIDANCE_TIME,8);assert(stale==-1.0);
    lxf_update(model,agent,0.033f,10.0,1,0);assert(tfl_seen_rng==lxf_seed(77,3,2)); /* the call ordinal */
    stats.issued=78;lxf_update(model,agent,0.033f,10.0,1,0);assert(tfl_seen_rng==lxf_seed(78,3,1));
    /* Another model of the same Agent is not the fixture's: native word, cheap as given. */
    uint64_t native=word;tfl_seen_rng=0;lxf_update(other_model,agent,0.033f,10.0,1,0);
    assert(tfl_seen_rng==native && tfl_seen_cheap==1 && word==(native^0xffu)); /* the native draws stay */
    /* Not seeded: the native word. Outside the session: native. */
    native=word;lxf_seeded=0;lxf_update(model,agent,0.033f,10.0,1,0);assert(tfl_seen_rng==native);lxf_seeded=1;
    boot_phase=2;lxf_update(model,agent,0.033f,10.0,1,0);assert(tfl_seen_cheap==1);boot_phase=3;
    /* The constructor: a new model lifetime of the fixture's Agent gets its own keyed angles (generation 2). */
    lxf_construct_observe(other_model);
    seed=lockstep_seed(0x5354454552494e49ULL,0,3,2,0,0);
    memcpy(&limit,&limits[0],4);want[0]=lxf_random(&seed,limit);memcpy(&limit,&limits[1],4);want[1]=lxf_random(&seed,limit);
    assert(lxf_samples[2].model==other_model && lxf_samples[2].init_generation==2 && !memcmp(other_model+LXF_MODEL_AZIMUTH,&want[0],4) &&
           !memcmp(other_model+LXF_MODEL_POLAR,&want[1],4));
    /* A stale Agent (the ship's ID changed): retired, the update native. */
    const uint64_t moved=0x5002;memcpy(ship+8,&moved,8);
    lxf_update(other_model,agent,0.033f,10.0,1,0);assert(lxf_samples[2].model==NULL && tfl_seen_cheap==1);
    memcpy(ship+8,&runtime,8);
    lxf_test_rng=NULL;lxf_original_override=NULL;lxf_installed=lxf_seeded=lxf_full=lxf_refresh=lxf_initial=0;stats.active=0;boot_phase=0;stats.issued=0;
    memset(lxf_samples,0,sizeof(lxf_samples));
    /* The constructor stub, run from a planted site: push rbx; mov rbx,rdi; movabs rax,K; call site; pop rbx; ret. The site
     * is the stub's jmp and the 10 left bytes; its end (site+15) a ret. */
    unsigned char *page=mmap(NULL,2*GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(page!=MAP_FAILED);
    unsigned char *stub=page+GATE_PAGE,*site=page+0x80;memset(page,0xcc,2*GATE_PAGE);
    static const unsigned char tramp_head[]={0x53,0x48,0x89,0xfb,0x48,0xb8};
    const uint64_t k=0x7a7a5b5b3c3c1d1dULL;unsigned n=0;
    memcpy(page+n,tramp_head,sizeof(tramp_head));n+=sizeof(tramp_head);memcpy(page+n,&k,8);n+=8;
    page[n]=0xe8;int32_t rel=(int32_t)(site-(page+n+5));memcpy(page+n+1,&rel,4);n+=5;page[n++]=0x5b;page[n++]=0xc3;
    lxf_stub_bytes(stub,(void*)tfl_observe,site+15);
    site[0]=0xe9;rel=(int32_t)(stub-(site+5));memcpy(site+1,&rel,4);memcpy(site+5,lxf_ctor_original+5,10);site[15]=0xc3;
    static unsigned char built[0x400] __attribute__((aligned(16)));memset(built,0x5a,sizeof(built));tfl_observed=NULL;
    uint64_t (*run)(void *)=(uint64_t (*)(void *))(void*)page;
    assert(run(built)==k && tfl_observed==built);
    uint64_t cleared;memcpy(&cleared,built+0x360,8);assert(cleared==0 && built[0x368]==0x5a);
    munmap(page,2*GATE_PAGE);
    printf("flight (keyed fixture update, initial angles, constructor stub) PASS\n");fflush(stdout);
}
/* The real executable: the slot holds the update, the head and the set-model are start pins, the site is native, the
 * constructor's draws and stores, the avoidance query's time argument, the Agent's model store. */
static void test_flight_elf(void) {
    assert(rd64(LX(LXF_UPDATE_SLOT_VA))==LXF_UPDATE_VA); /* the file's slot holds the update's VA */
    assert(site_ok(site_named("flight_update")) && site_ok(site_named("agent_set_model")));
    assert(!memcmp(LX(LXF_CTOR_SITE_VA),lxf_ctor_original,15));
    static const struct {uint32_t va;unsigned n;const char *hex;} insn[]={
        {0x14332a0,5,"e81b5f6100"},{0x14332a5,8,"f30f11832c030000"},{0x14332b5,5,"e8065f6100"},{0x14332c1,8,"f30f118330030000"},
        {0x13ea422,4,"498d5578"},{0x1433034,7,"49899f300f0000"}};
    for(unsigned i=0;i<sizeof(insn)/sizeof(*insn);i++) {
        char text[40]={0};for(unsigned j=0;j<insn[i].n;j++) snprintf(text+2*j,3,"%02x",LX(insn[i].va)[j]);
        assert(!strcmp(text,insn[i].hex));
    }
    int32_t rel;memcpy(&rel,LX(0x14332a1),4);assert(0x14332a5u+(uint32_t)rel==0x1a491c0u); /* both draws call the random float */
    memcpy(&rel,LX(0x14332b6),4);assert(0x14332bau+(uint32_t)rel==0x1a491c0u);
}

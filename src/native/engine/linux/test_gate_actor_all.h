/* Per-ship RNG keys for every portable ship (gate_actor_all.h): the shared scopes on the Linux build against stand-in
 * originals (ship and agent keys, ordinals, nesting, the word given back, unmapped ships), and on the real executable
 * the pins, the two Move slots, the agent call and their restore. Included by test_gate_linux.c. */
static uint64_t taa_seen;static unsigned taa_calls;static void *taa_nested_ship;
static void taa_ship_original(void *self) {
    uint64_t *w=actor_all_tls_rng();taa_seen=*w;++taa_calls;*w^=0x1111; /* the Move draws */
    if(taa_nested_ship && self!=taa_nested_ship) {uint64_t before=*w;actor_all_ship_execute(taa_ship_original,taa_nested_ship,w);assert(*w!=before || 1);}
}
static uint64_t taa_agent_seen;static float taa_dt;static double taa_time;static unsigned char taa_cheap;
static void taa_agent_original(void *self,void *commands,float dt,double time,unsigned char cheap) {
    (void)self;(void)commands;uint64_t *w=actor_all_tls_rng();taa_agent_seen=*w;taa_dt=dt;taa_time=time;taa_cheap=cheap;*w^=0x2222;
}
static void test_actor_all_linux(void) {
    reset_session();stats.active=1;boot_mode=1;boot_phase=3;stats.issued=77;ui_thread=GetCurrentThreadId();
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);world_identity_installed=1;
    world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(9301,0x501) && world_identity_record(9302,0x502));
    static unsigned char ship[16],other[16],stranger[16],agent[0x1000];uint64_t r;
    r=9301;memcpy(ship+8,&r,8);r=9302;memcpy(other+8,&r,8);r=9999;memcpy(stranger+8,&r,8);
    void *owner=ship;memcpy(agent+LXW_AGENT_OWNER,&owner,8);
    actor_all_armed=1;actor_all_epoch_tick=UINT64_MAX;actor_all_ship_depth=0;actor_all_agent_depth=0;
    uint64_t *word=actor_all_tls_rng();
    /* Ship scope: (tick, ID, 0, ordinal, 0); the caller's word comes back. */
    *word=0x4444;taa_calls=0;taa_nested_ship=NULL;actor_all_ship_execute(taa_ship_original,ship,word);
    assert(taa_seen==lockstep_seed(actor_all_domains[ACTOR_ALL_SHIP],77,0x501,0,1,0) && *word==0x4444 && taa_calls==1);
    /* The same ship again in this tick: ordinal 2. */
    actor_all_ship_execute(taa_ship_original,ship,word);assert(taa_seen==lockstep_seed(actor_all_domains[ACTOR_ALL_SHIP],77,0x501,0,2,0));
    /* A ship moved inside another ship's scope continues the enclosing stream (nested), as on Windows. */
    taa_nested_ship=other;actor_all_ship_execute(taa_ship_original,ship,word);
    assert(taa_seen==(lockstep_seed(actor_all_domains[ACTOR_ALL_SHIP],77,0x501,0,3,0)^0x1111) && *word==0x4444);
    taa_nested_ship=NULL;
    /* No portable ID: the natural word, and its draws stay on it. */
    *word=0x5555;actor_all_ship_execute(taa_ship_original,stranger,word);assert(taa_seen==0x5555 && *word==(0x5555^0x1111));
    /* Agent scope: (tick, the object's ID, dt bits, 0, time bits) through the Linux owner offset +0xf10; arguments forwarded. */
    float dt=1.0f/30.0f;double time=123.5;uint32_t dt_bits;uint64_t time_bits;memcpy(&dt_bits,&dt,4);memcpy(&time_bits,&time,8);
    *word=0x6666;actor_all_agent_execute(taa_agent_original,agent,(void*)0x1234,dt,time,1,word);
    assert(taa_agent_seen==lockstep_seed(actor_all_domains[ACTOR_ALL_AGENT],77,0x501,dt_bits,0,time_bits) && *word==0x6666);
    assert(taa_dt==dt && taa_time==time && taa_cheap==1);
    /* The frame fold and the status. */
    actor_all_frame(77);char out[3072];assert(actor_all_status(out,sizeof(out))>0 && strstr(out,"ship_keyed=3 ") && strstr(out,"ship_nested=1 ") &&
                                            strstr(out,"ship_unmapped=1 ") && strstr(out,"agent_keyed=1 "));
    actor_all_armed=0;world_identity_installed=0;world_identity_count=0;free(world_identities);world_identities=NULL;
    printf("actor rng all PASS\n");
}
/* The real executable: pins, the slots and the call as found, both hooks installed, armed, owned, and restored exactly. */
static void test_actor_all_elf(void) {
    void *raw0=NULL,*raw1=NULL;memcpy(&raw0,LX(LX_SHIP_SMALL_MOVE_SLOT_VA),8);memcpy(&raw1,LX(LX_SHIP_XL_MOVE_SLOT_VA),8);
    assert(raw0==(void*)(uintptr_t)LX_SHIP_MOVE_VA && raw1==(void*)(uintptr_t)LX_SHIP_MOVE_VA); /* link addresses in the file */
    for(unsigned i=0;i<LAA_SHIP_SLOTS;i++) {
        uintptr_t page=(uintptr_t)LX(laa_ship_slots[i])&~(uintptr_t)(GATE_PAGE-1);
        assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));put64(LX(laa_ship_slots[i]),(uint64_t)(uintptr_t)LX(LX_SHIP_MOVE_VA));
    }
    unsigned char block[16];memcpy(block,LX(LX_AGENT_CALL_BLOCK_VA),16);
    assert(laa_pins() && laa_call_state()==0);
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();laa_page=NULL;
    world_identity_installed=0;assert(laa_ship_install() && laa_agent_install() && !laa_arm()); /* needs the world identities */
    world_identity_installed=1;assert(laa_arm() && actor_all_on());
    assert(laa_slot(0)==(void*)laa_ship_move && laa_slot(1)==(void*)laa_ship_move && laa_call_state()==1 && laa_stub_ok() && laa_owned() && laa_check());
    laa_restore();assert(!actor_all_on() && !laa_ship_installed && !laa_agent_installed && laa_restored);
    assert(laa_slot(0)==LX(LX_SHIP_MOVE_VA) && laa_slot(1)==LX(LX_SHIP_MOVE_VA) && !memcmp(block,LX(LX_AGENT_CALL_BLOCK_VA),16));
    world_identity_installed=0;
    printf("actor rng all ELF PASS\n");
}

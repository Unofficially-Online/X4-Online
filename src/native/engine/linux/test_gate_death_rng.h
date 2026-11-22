/* A ship's death draws keyed (gate_death_rng.h): the listener runs the original on the seeded word and puts the
 * caller's back; outside an admitted frame, from another source or for an object without a portable key it runs
 * natively (synthetic). On the real executable: the pin, the four slots to the forwarder and back. Included by
 * test_gate_linux.c. */
static uint64_t tlxd_seen;
static unsigned tlxd_calls;
static void tlxd_original(void *self,void *source,unsigned reason,void *killer,double time) {
    (void)self;(void)source;(void)reason;(void)killer;(void)time;
    uint64_t *w=gate_tls_rng(gate_tls_self());tlxd_seen=*w;*w+=6;++tlxd_calls;
}
static void test_death_rng_linux(void) {
    uint64_t *word=gate_tls_rng(gate_tls_self());assert(word);
    const uint64_t keep=*word;
    world_identities=world_identities?world_identities:calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);
    world_identity_installed=1;world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x5101,0x9fa51));world_identity_sealed=1;
    static unsigned char ship[16],stray[16];const uint64_t id=0x5101,other=0x5199;memcpy(ship+8,&id,8);memcpy(stray+8,&other,8);
    lxd_override=tlxd_original;lxd_keyed=lxd_unkeyed=lxd_calls=0;tlxd_calls=0;
    stats.active=1;boot_phase=3;ui_thread=GetCurrentThreadId();stats.issued=75;
    *word=0x1234;lxd_listener(ship,ship,3,NULL,10.0);
    assert(tlxd_seen==death_all_seed(75,0x9fa51,3) && *word==0x1234 && lxd_keyed==1 && tlxd_calls==1);
    /* the seed follows tick, key and reason only, never the caller's word */
    *word=0x9999;lxd_listener(ship,ship,3,NULL,11.0);assert(tlxd_seen==death_all_seed(75,0x9fa51,3) && *word==0x9999);
    stats.issued=76;lxd_listener(ship,ship,3,NULL,11.0);assert(tlxd_seen==death_all_seed(76,0x9fa51,3) && tlxd_seen!=death_all_seed(75,0x9fa51,3));
    /* native: no portable key, another source, outside the admitted frames, a bad time */
    *word=0x4444;lxd_listener(stray,stray,3,NULL,1.0);assert(tlxd_seen==0x4444 && *word==0x444a);
    *word=0x4444;lxd_listener(ship,stray,3,NULL,1.0);assert(tlxd_seen==0x4444);
    boot_phase=2;*word=0x4444;lxd_listener(ship,ship,3,NULL,1.0);assert(tlxd_seen==0x4444);boot_phase=3;
    *word=0x4444;lxd_listener(ship,ship,3,NULL,NAN);assert(tlxd_seen==0x4444);
    assert(lxd_keyed==3 && lxd_unkeyed==4 && lxd_calls==7 && tlxd_calls==7);
    lxd_override=NULL;lxd_keyed=lxd_unkeyed=lxd_calls=0;stats.active=0;stats.issued=0;boot_phase=0;
    world_identity_installed=0;world_identity_count=0;world_identity_sealed=0;
    memset(world_identities,0,WORLD_IDENTITY_CAPACITY*sizeof(*world_identities));*word=keep;
    puts("death rng (Linux): the listener on a word from tick, portable key and reason, the caller's word put back; native otherwise PASS");
}
static void test_death_rng_elf(void) {
    for(unsigned i=0;i<sizeof(lxd_pin_names)/sizeof(*lxd_pin_names);i++) assert(site_ok(site_named(lxd_pin_names[i])));
    /* the file's VAs; loaded at another base the slots are relocated for the round, then put back */
    for(unsigned i=0;i<LXD_SLOTS;i++) assert(rd64(LX(lxd_slots[i]))==LXD_LISTENER_VA);
    for(unsigned i=0;i<LXD_SLOTS;i++) {
        unsigned char *page=(unsigned char*)((uintptr_t)LX(lxd_slots[i])&~(uintptr_t)(GATE_PAGE-1));
        assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_WRITE));const void *f=LX(LXD_LISTENER_VA);memcpy(LX(lxd_slots[i]),&f,8);
    }
    assert(lxd_state()==0);
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();lxd_installed=lxd_restored=0;lxd_swapped=0;
    assert(lxd_arm() && lxd_installed && lxd_state()==1 && lxd_check() && lxd_swapped==0xfu);
    assert(lxd_arm()); /* an identical rerun (the probe's arming block runs twice) */
    {char status[256];assert(lxd_status(status,sizeof(status)) && strstr(status,"all=1 slots=f "));}
    lxd_restore();assert(!lxd_installed && lxd_restored && lxd_state()==0 && lxd_pins() && !lxd_swapped);
    for(unsigned i=0;i<LXD_SLOTS;i++) {
        unsigned char *page=(unsigned char*)((uintptr_t)LX(lxd_slots[i])&~(uintptr_t)(GATE_PAGE-1));
        assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_WRITE));const uint64_t va=LXD_LISTENER_VA;memcpy(LX(lxd_slots[i]),&va,8);
    }
    lxd_restored=0;lxd_stage="idle";stats.active=0;
    puts("death rng ELF: the listener pinned, its four ship slots to the forwarder and back PASS");
}

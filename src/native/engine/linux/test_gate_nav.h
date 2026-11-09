/* The battle fixture's navigation upkeep (gate_nav.h, Windows' nav module): a bound actor's controller through the hook
 * (its word seeded from "NAV_UPDT" with the tick's call ordinal and given back, the parameter deadline at time - dt at
 * its first call only, the NAV row in Windows' text), another controller and an unkeyed (diag only) call untouched; on
 * the real executable the two vtable slots shared with the cadence: either installs first, the last to restore swaps
 * them back, a foreign slot refuses the install. Included by test_gate_linux.c. */
static unsigned char tnv_object[0x300],tnv_controller[0x100],tnv_other[0x100];
static uint64_t tnv_word,tnv_seen_word;
static double tnv_seen_deadline;
static int tnv_native_calls;
static void tnv_native(void *self,double time) {
    ++tnv_native_calls;tnv_seen_word=tnv_word;memcpy(&tnv_seen_deadline,(unsigned char*)self+0xd0,8);
    tnv_word=tnv_word*6364136223846793005ULL+1442695040888963407ULL; /* one draw */
    const double due=time+1.5;memcpy((unsigned char*)self+0xd0,&due,8);
}
static void test_nav_fixture_linux(void) {
    static lxb_actor saved_actors[LXB_ACTORS];const unsigned saved_bound=lxb_bound;
    memcpy(saved_actors,lxb_actors,sizeof(saved_actors));memset(lxb_actors,0,sizeof(lxb_actors));
    reset_session();stats.active=1;boot_phase=3;stats.issued=40;ui_thread=GetCurrentThreadId();
    const uint64_t runtime=0x9002;void *p=tnv_controller;
    lxb_bound=LXB_ACTORS;lxb_actors[1].object=tnv_object;lxb_actors[1].runtime=runtime;
    memcpy(tnv_object+8,&runtime,8);memcpy(tnv_object+LXW_OBJ_CONTROLLER,&p,8);
    assert(lxb_fixture_controller(tnv_controller)==2 && !lxb_fixture_controller(tnv_other));
    memset(lxn_samples,0,sizeof(lxn_samples));memset(lxn_initialized,0,sizeof(lxn_initialized));lxn_calls=lxn_inits=0;lxn_failures=0;
    lxn_installed=1;lxn_seeded=1;lxn_initial_refresh=1;lxn_rng_override=&tnv_word;lxv_native_override=tnv_native;
    const double old=7.25,dt=gate_tick_dt();memcpy(tnv_controller+0xd0,&old,8);memcpy(tnv_other+0xd0,&old,8);
    /* First call: the deadline re-based, the keyed word, the native word back after. */
    tnv_word=0x1234;lxv_move(tnv_controller,10.0);
    const uint64_t seed1=lockstep_seed(LXN_DOMAIN,40,2,0,1,0);
    assert(tnv_native_calls==1 && tnv_seen_word==seed1 && tnv_word==0x1234 && tnv_seen_deadline==10.0-dt);
    const lxn_sample *s=&lxn_samples[1];
    assert(s->tick==40 && s->calls==1 && s->initialized==1 && lxn_inits==1 && lxn_calls==1 && !lxn_failures);
    {double v;memcpy(&v,&s->deadline_before,8);assert(v==old);memcpy(&v,&s->deadline_after,8);assert(v==11.5);}
    assert(s->rng_before==seed1 && s->rng_after==seed1*6364136223846793005ULL+1442695040888963407ULL);
    /* Second call in the tick: ordinal 2, no re-base. */
    lxv_move(tnv_controller,10.0);
    assert(tnv_seen_word==lockstep_seed(LXN_DOMAIN,40,2,0,2,0) && tnv_seen_deadline==11.5 && s->calls==2 && !s->initialized && lxn_inits==1 && tnv_word==0x1234);
    /* Another controller: the native word, no row. */
    lxv_move(tnv_other,10.0);
    assert(tnv_native_calls==3 && tnv_seen_word==0x1234 && tnv_word!=0x1234 && lxn_calls==2);
    /* The next tick restarts the ordinal; the row is Windows' text. */
    tnv_word=0x1234;stats.issued=41;lxv_move(tnv_controller,10.0+dt);
    assert(tnv_seen_word==lockstep_seed(LXN_DOMAIN,41,2,0,1,0) && s->tick==41 && s->calls==1);
    char text[512];const int n=lxn_text(2,text,sizeof(text));
    assert(n>0 && !strncmp(text,"tick=41 calls=1 time=",21) && strstr(text," initialized=0 rng_before=") && !lxn_text(13,text,sizeof(text)));
    /* Diag only: the call records with the word it finds. */
    lxn_seeded=0;tnv_word=0x99;lxv_move(tnv_controller,10.0+dt);
    assert(tnv_seen_word==0x99 && s->calls==2 && s->rng_before==0x99 && tnv_word!=0x99);
    /* Outside an admitted frame, or not installed: straight to the native. */
    const uint64_t calls=lxn_calls;boot_phase=2;lxv_move(tnv_controller,10.0);boot_phase=3;
    lxn_installed=0;lxv_move(tnv_controller,10.0);
    assert(lxn_calls==calls && tnv_native_calls==7);
    /* A time that is not finite is a failure (stop 42), the call still runs. */
    lxn_installed=1;lxv_move(tnv_controller,NAN);assert(lxn_failures==1 && tnv_native_calls==8);
    lxn_installed=0;lxn_seeded=lxn_initial_refresh=0;lxn_rng_override=NULL;lxv_native_override=NULL;lxn_failures=0;tnv_native_calls=0;
    memcpy(lxb_actors,saved_actors,sizeof(saved_actors));lxb_bound=saved_bound;reset_session();
    printf("nav fixture: NAV_UPDT word per call ordinal and back, first-call deadline time - dt, other controllers native, Windows' NAV row PASS\n");
}
static void tnv_reset(void) {
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    nav_cadence_installed=nav_cadence_restored=nav_cadence_patched=0;nav_cadence_mode=NAV_CADENCE_OFF;nav_cadence_stage="idle";
    lxn_installed=lxn_restored=0;
}
/* A slot's page writable again (a swap leaves it read only), then the value. */
static void tnv_slot_put(uint32_t va,uint64_t value) {
    unsigned char *slot=LX(va);uintptr_t page=(uintptr_t)slot&~(uintptr_t)(GATE_PAGE-1);
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));put64(slot,value);
}
static void test_nav_fixture_elf(void) {
    for(unsigned i=0;i<2;i++) { /* the slots hold link addresses in the file */
        const uint32_t va=i?LX_NAV_MOVE_SLOT_B_VA:LX_NAV_MOVE_SLOT_A_VA;
        assert(rd64(LX(va))==LX_NAV_MOVE_VA || rd64(LX(va))==(uint64_t)(uintptr_t)LX(LX_NAV_MOVE_VA));
        tnv_slot_put(va,(uint64_t)(uintptr_t)LX(LX_NAV_MOVE_VA));
    }
    unsigned char sites[4][6];for(unsigned i=0;i<4;i++) memcpy(sites[i],LX(lxv_sites[i].va),lxv_sites[i].size);
    /* The fixture path first, then the cadence; the cadence restores first. */
    tnv_reset();
    assert(lxn_install(1,1) && lxn_installed && lxv_slots_ours() && lxv_slots_holders==LXV_FIXTURE && lxn_owned() && !lxn_install(1,1));
    assert(nav_cadence_arm(NAV_CADENCE_FAR) && nav_cadence_patched && lxv_slots_holders==(LXV_FIXTURE|LXV_CADENCE) && nav_cadence_owned());
    nav_cadence_restore();
    assert(nav_cadence_restored && !nav_cadence_installed && lxv_slots_ours() && lxn_owned() && lxv_slots_holders==LXV_FIXTURE);
    for(unsigned i=0;i<4;i++) assert(!memcmp(LX(lxv_sites[i].va),sites[i],lxv_sites[i].size));
    lxn_restore();assert(lxn_restored && !lxn_installed && !lxv_slots_installed && rd64(LX(LX_NAV_MOVE_SLOT_A_VA))==(uint64_t)(uintptr_t)LX(LX_NAV_MOVE_VA) &&
                         rd64(LX(LX_NAV_MOVE_SLOT_B_VA))==(uint64_t)(uintptr_t)LX(LX_NAV_MOVE_VA));
    /* The cadence first, then the fixture path; the fixture path restores first. */
    tnv_reset();
    assert(nav_cadence_arm(NAV_CADENCE_FAR) && lxn_install(0,0) && lxv_slots_holders==(LXV_FIXTURE|LXV_CADENCE));
    lxn_restore();assert(lxn_restored && lxv_slots_ours() && nav_cadence_owned());
    nav_cadence_restore();assert(nav_cadence_restored && !lxv_slots_installed && rd64(LX(LX_NAV_MOVE_SLOT_A_VA))==(uint64_t)(uintptr_t)LX(LX_NAV_MOVE_VA));
    /* A foreign slot refuses the install and stays as found. */
    tnv_reset();tnv_slot_put(LX_NAV_MOVE_SLOT_B_VA,0x1234);
    assert(!lxn_install(1,1) && !lxn_installed && !lxv_slots_installed && rd64(LX(LX_NAV_MOVE_SLOT_A_VA))==(uint64_t)(uintptr_t)LX(LX_NAV_MOVE_VA));
    tnv_slot_put(LX_NAV_MOVE_SLOT_B_VA,(uint64_t)(uintptr_t)LX(LX_NAV_MOVE_VA));
    tnv_reset();stats.active=0;boot_mode=0;
    printf("nav fixture ELF: the vtable slots shared with the cadence (either first, the last restore swaps them back), a foreign slot refused PASS\n");
}

/* The fixture controllers' SetVelocity calls (gate_velocity.h): a planted fixture controller through the wrapper (the
 * input, the current pathing slot before and after, the tick; another controller passes through untraced; the row in
 * Windows' text); on the real executable the pin, both vtable slots swapped and restored. Included by test_gate_linux.c. */
static unsigned char tvd_object[0x300],tvd_controller[0x40],tvd_other[0x40],tvd_slots[0x1a0*2] __attribute__((aligned(16)));
static int tvd_native_calls;
static void tvd_native(void *self,const void *value) { /* the native call writes the current slot (index 1 here) */
    tvd_native_calls++;if(self==tvd_controller) memcpy(tvd_slots+0x1a0+0x40,value,32);
}
static void test_velocity_linux(void) {
    static lxb_actor saved_actors[LXB_ACTORS];int saved_bound=lxb_bound;uint32_t saved_index;
    memcpy(saved_actors,lxb_actors,sizeof(saved_actors));memcpy(&saved_index,LX(LXVD_INDEX_VA),4);
    memset(lxb_actors,0,sizeof(lxb_actors));memset(lxvd_traces,0,sizeof(lxvd_traces));lxvd_calls=lxvd_other_thread=0;lxvd_failures=0;
    reset_session();stats.active=1;stats.issued=28;ui_thread=GetCurrentThreadId();
    lxb_bound=LXB_ACTORS;lxb_actors[2].object=tvd_object;
    void *p=tvd_controller;memcpy(tvd_object+LXW_OBJ_CONTROLLER,&p,8);p=tvd_slots;memcpy(tvd_controller+LXVD_SLOTS,&p,8);
    {unsigned char *page=(unsigned char*)((uintptr_t)LX(LXVD_INDEX_VA)&~(uintptr_t)(GATE_PAGE-1));  /* an earlier swap may leave it read-only */
     assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_WRITE));}
    const uint32_t index=1;memcpy(LX(LXVD_INDEX_VA),&index,4);
    uint32_t before[8],input[8];for(unsigned i=0;i<8;i++) {before[i]=0x3e000000u+i;input[i]=0xbf000000u+i;}
    memcpy(tvd_slots+0x1a0+0x40,before,32);memset(tvd_slots+0x40,0x5a,32); /* slot 0 is not read */
    lxvd_original_override=tvd_native;
    lxvd_move(tvd_controller,input);
    lxvd_move(tvd_other,input);                                                     /* not a fixture controller */
    assert(tvd_native_calls==2 && lxvd_calls==1 && lxvd_traces[2].tick==28 && lxvd_traces[2].count==1 && !lxvd_traces[0].count);
    const lxvd_record *r=&lxvd_traces[2].records[0];
    assert(!memcmp(r->input,input,32) && !memcmp(r->before,before,32) && !memcmp(r->after,input,32) && r->source);
    assert(lxvd_failures<=1);                                                      /* the game time: none without X4's TLS */
    stats.issued=29;lxvd_move(tvd_controller,input);assert(lxvd_traces[2].tick==29 && lxvd_traces[2].count==1); /* a new tick */
    lxvd_original_override=NULL;tvd_native_calls=0;
    memcpy(lxb_actors,saved_actors,sizeof(saved_actors));lxb_bound=saved_bound;memcpy(LX(LXVD_INDEX_VA),&saved_index,4);
    memset(lxvd_traces,0,sizeof(lxvd_traces));lxvd_calls=lxvd_other_thread=0;lxvd_failures=0;stats.active=0;
    printf("velocity diag wrapper PASS\n");
}
static void test_velocity_elf(void) {
    assert(rd64(LX(LXVD_SLOT_A_VA))==LXVD_MOVE_VA && rd64(LX(LXVD_SLOT_B_VA))==LXVD_MOVE_VA); /* the file's slots hold its VA */
    assert(site_ok(site_named("nav_set_velocity")));
    /* Loaded at the executable's base the slots hold the function's address: relocate them for the round, then back. */
    const uint32_t slots[2]={LXVD_SLOT_A_VA,LXVD_SLOT_B_VA};
    for(unsigned i=0;i<2;i++) {
        unsigned char *page=(unsigned char*)((uintptr_t)LX(slots[i])&~(uintptr_t)(GATE_PAGE-1));
        assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_WRITE));const void *f=LX(LXVD_MOVE_VA);memcpy(LX(slots[i]),&f,8);
    }
    lxvd_installed=lxvd_restored=0;
    assert(lxvd_install() && lxvd_installed && lxvd_owned() &&
           rd64(LX(LXVD_SLOT_A_VA))==(uint64_t)(uintptr_t)lxvd_move && rd64(LX(LXVD_SLOT_B_VA))==(uint64_t)(uintptr_t)lxvd_move);
    lxvd_restore();
    assert(!lxvd_installed && lxvd_restored && rd64(LX(LXVD_SLOT_A_VA))==(uint64_t)(uintptr_t)LX(LXVD_MOVE_VA) &&
           rd64(LX(LXVD_SLOT_B_VA))==(uint64_t)(uintptr_t)LX(LXVD_MOVE_VA));
    lxvd_restored=0;
    for(unsigned i=0;i<2;i++) {
        unsigned char *page=(unsigned char*)((uintptr_t)LX(slots[i])&~(uintptr_t)(GATE_PAGE-1));
        assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_WRITE));const uint64_t va=LXVD_MOVE_VA;memcpy(LX(slots[i]),&va,8);
    }
    printf("velocity diag ELF PASS\n");
}

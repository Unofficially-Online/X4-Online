/* The player's fleet rebuild timer (gate_player_timer.h): the rebase at the first admitted call against a stand-in Move
 * and the thread's game time, and on the real executable the pins, the slot swap and its restore. Included by
 * test_gate_linux.c. */
static unsigned tpt_calls;static double tpt_seen_due;
static void tpt_original(void *self) {
    ++tpt_calls;memcpy(&tpt_seen_due,(unsigned char*)self+PT_DUE,8);
    double now;memcpy(&now,gate_tls_time(gate_tls_self()),8);
    if(now>=tpt_seen_due) {double next=now+60.0+5.0;memcpy((unsigned char*)self+PT_DUE,&next,8);} /* the check fires and re-arms */
}
static void test_player_timer_linux(void) {
    reset_session();stats.active=1;boot_mode=1;boot_phase=3;stats.issued=5;ui_thread=GetCurrentThreadId();
    static unsigned char player[0x8a600];double loaded=33.7,t=100.0;memcpy(player+PT_DUE,&loaded,8);
    memcpy(gate_tls_time(gate_tls_self()),&t,8);
    pt_installed=1;pt_calls=0;pt_admitted=pt_rebased=pt_first=pt_fires=pt_last_fire=pt_unreadable=pt_full=0;
    memset(pt_objects,0,sizeof(pt_objects));pt_object_count=0;pt_original_override=tpt_original;tpt_calls=0;
    /* First admitted call: whatever the load left (33.7, due already) becomes the frame's time + 15 s, so no check fires now. */
    pt_move(player);double due;memcpy(&due,player+PT_DUE,8);
    assert(tpt_calls==1 && tpt_seen_due==115.0 && due==115.0 && pt_rebased==1 && pt_first==5 && !pt_fires);
    /* Later calls pass straight through; the check fires once its time comes, and is counted. */
    t=114.0;memcpy(gate_tls_time(gate_tls_self()),&t,8);stats.issued=6;pt_move(player);assert(!pt_fires && pt_rebased==1);
    t=115.0;memcpy(gate_tls_time(gate_tls_self()),&t,8);stats.issued=7;pt_move(player);
    memcpy(&due,player+PT_DUE,8);assert(pt_fires==1 && pt_last_fire==7 && due==180.0);
    /* Outside an admitted frame: native, not admitted. */
    boot_phase=2;pt_move(player);assert(pt_admitted==3 && tpt_calls==4);boot_phase=3;
    char out[768];assert(pt_status(out,sizeof(out))>0 && strstr(out," rebased=1 ") && strstr(out," fires=1 "));
    pt_installed=0;pt_original_override=NULL;
    printf("player timer PASS\n");
}
/* The real executable: pins, the slot as found, swapped to pt_move and back. */
static void test_player_timer_elf(void) {
    void *raw=NULL;memcpy(&raw,LX(LX_PLAYER_MOVE_SLOT_VA),8);assert(raw==(void*)(uintptr_t)LX_PLAYER_MOVE_VA);
    uintptr_t page=(uintptr_t)LX(LX_PLAYER_MOVE_SLOT_VA)&~(uintptr_t)(GATE_PAGE-1);
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));put64(LX(LX_PLAYER_MOVE_SLOT_VA),(uint64_t)(uintptr_t)LX(LX_PLAYER_MOVE_VA));
    assert(pt_pins());
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();pt_installed=0;
    assert(pt_arm() && pt_installed && pt_owned() && pt_check());
    void *now=NULL;memcpy(&now,LX(LX_PLAYER_MOVE_SLOT_VA),8);assert(now==(void*)pt_move);
    pt_restore();assert(!pt_installed && pt_restored);memcpy(&now,LX(LX_PLAYER_MOVE_SLOT_VA),8);assert(now==LX(LX_PLAYER_MOVE_VA));
    printf("player timer ELF PASS\n");
}

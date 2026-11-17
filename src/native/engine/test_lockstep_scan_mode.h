/* lockstep_scan_mode.h on synthetic memory: the key's verdict, the activity for slots 2..N, the adapter's start and
 * stop, the end when the slot leaves the ship the mode was started in; the deep scan's key, its step per tick (on
 * target, the grace off target, the abort, the finish, a target that left the world). */
#define TEST_SCM_PLAYER 0x8a700u
static unsigned char test_scm_p1[TEST_SCM_PLAYER],test_scm_p2[TEST_SCM_PLAYER],test_scm_ship[0x800],test_scm_universe[0x400],test_scm_target[0x100];
static unsigned test_scm_sets;static void *test_scm_set_player;
static unsigned test_scm_can_answer,test_scm_aborts,test_scm_finishes;static void *test_scm_finish_target;static int test_scm_finish_level;
static void test_scm_set_activity(void *player,int activity) {
    ++test_scm_sets;test_scm_set_player=player;memcpy((unsigned char*)player+SCM_ACTIVITY,&activity,4);
}
static int test_scm_activity(const unsigned char *player) {int a=0;memcpy(&a,player+SCM_ACTIVITY,4);return a;}
static unsigned char test_scm_can(void *player,void *target,unsigned char force) {
    void *scanned=target;memcpy(&scanned,test_scm_p2+SCM_TARGET,8);   /* asked as for a start: the player scans nothing */
    assert(player==(void*)test_scm_p2 && target==(void*)test_scm_target && !force && !scanned);return (unsigned char)test_scm_can_answer;
}
static void test_scm_abort(void *player) {++test_scm_aborts;memset((unsigned char*)player+SCM_TARGET,0,8);}
static void test_scm_finish(void *player,void *target,int level) {
    ++test_scm_finishes;test_scm_finish_target=target;test_scm_finish_level=level;memset((unsigned char*)player+SCM_TARGET,0,8);
}
static void test_scm_begin(void) {   /* what X4's start leaves in the player */
    void *t=test_scm_target;const int level=2;const double none=-1.0;const float zero=0.0f;
    memcpy(test_scm_p2+SCM_TARGET,&t,8);memcpy(test_scm_p2+SCM_LEVEL,&level,4);memcpy(test_scm_p2+SCM_GRACE,&none,8);memcpy(test_scm_p2+SCM_PROGRESS,&zero,4);
}
static float test_scm_progress(void) {float p=0;memcpy(&p,test_scm_p2+SCM_PROGRESS,4);return p;}
static double test_scm_grace(void) {double g=0;memcpy(&g,test_scm_p2+SCM_GRACE,8);return g;}
static void test_scan_mode(void) {
    const uint64_t issued_was=stats.issued;const DWORD ui_was=ui_thread;const uint32_t active_was=stats.active;
    unsigned char *base_was=pnat_base;const int pnat_was=pnat_armed,mode_was=pnat_mode,uirep_was=uirep_armed,pins_was=scm_pins_checked;
    const unsigned families_was=uirep_families,pilots_was=pilot_slot_count;const pslot_slot slot_was=pslots[2];const uirep_fns fn_was=uirep_fn;
    unsigned char universe_was[8];
    pnat_base=image;memcpy(universe_was,image+PNAT_UNIVERSE,8);
    memset(test_scm_p1,0,sizeof(test_scm_p1));memset(test_scm_p2,0,sizeof(test_scm_p2));memset(test_scm_universe,0,sizeof(test_scm_universe));
    memset(test_scm_target,0,sizeof(test_scm_target));
    {void *p=test_scm_universe;memcpy(image+PNAT_UNIVERSE,&p,8);p=test_scm_p1;memcpy(test_scm_universe+0x230,&p,8);}
    memset(&pslots[2],0,sizeof(pslots[2]));pslots[2].player=test_scm_p2;pslots[2].ship=test_scm_ship;pslots[2].runtime=0x1234;pslots[2].seated=1;
    pilot_slot_count=2;ui_thread=GetCurrentThreadId();stats.active=1;stats.issued=0;
    uirep_fn.set_activity=test_scm_set_activity;test_scm_sets=0;scm_armed=0;scm_scan_on=0;scm_pins_checked=1;
    /* Not armed: the key is refused for everybody, the activity for slots 2..N, the adapter does nothing. */
    pnat_armed=1;pnat_mode=PNAT_MODE_STOCK;uirep_armed=1;uirep_families=UIREP_FAMILY_TRAVEL;
    assert(pstk_verdict(PSTK_ACTION,0x130)==PSTK_REFUSE && pstk_verdict_slot(PSTK_ACTION,0x130,2)==PSTK_REFUSE);
    assert(!uirep_activity_valid(2,3,0) && !uirep_activity_valid(2,3,1) && uirep_activity_valid(1,3,0));
    uirep_adapter_start(&pslots[2],3);assert(!test_scm_sets && !test_scm_activity(test_scm_p2) && scm_frame_slot(2,1.0));
    /* Arming needs the stock slots, the UI replay with travel, the first tick ahead, the pin. Without the flight
     * family the mode alone is armed: X4's scan block stays as it is. */
    pnat_armed=0;assert(!scm_arm() && !strcmp(scm_why,"needs_stock_and_travel"));pnat_armed=1;
    uirep_families=0;assert(!scm_arm() && !strcmp(scm_why,"needs_stock_and_travel"));uirep_families=UIREP_FAMILY_TRAVEL;
    stats.issued=1;assert(!scm_arm() && !strcmp(scm_why,"late_or_thread"));stats.issued=0;
    scm_pins_checked=-1;assert(!scm_arm() && !strcmp(scm_why,"pins"));scm_pins_checked=1;
    assert(scm_arm() && scm_armed && scm_arm() && !scm_scan_on && !scm_patched && !strcmp(scm_scan_why,"needs_flight"));
    /* Armed: the key rides the record for every slot; the long-range scan and SETA of slots 2..N stay as they were. */
    assert(pstk_verdict(PSTK_ACTION,0x130)==PSTK_REPLAY && pstk_verdict_slot(PSTK_ACTION,0x130,2)==PSTK_REPLAY);
    assert(pstk_verdict(PSTK_ACTION,0x147)==PSTK_REFUSE && pstk_verdict_slot(PSTK_ACTION,0xe1,2)==PSTK_REFUSE);
    assert(uirep_activity_valid(2,3,0) && uirep_activity_valid(2,3,1) && !uirep_activity_valid(2,4,0) && !uirep_activity_valid(2,2,0));
    /* Start: the activity on the slot's player, the ship noted; a second start changes nothing. */
    uirep_adapter_start(&pslots[2],3);
    assert(test_scm_sets==1 && test_scm_set_player==(void*)test_scm_p2 && test_scm_activity(test_scm_p2)==3 && scm_starts==1 && scm_ship[2]==0x1234);
    assert(scm_mode_on(2) && !scm_mode_on(1) && !test_scm_activity(test_scm_p1));
    uirep_adapter_start(&pslots[2],3);assert(test_scm_sets==1 && scm_starts==1);
    /* The slot's frames keep it while the slot sits in that ship; the stop ends it. */
    assert(scm_frame_slot(2,1.0) && test_scm_sets==1 && scm_frame_slot(1,1.0) && !scm_left);
    uirep_adapter_stop(&pslots[2],3);assert(test_scm_sets==2 && !test_scm_activity(test_scm_p2) && scm_stops==1 && !scm_ship[2]);
    /* From another mode: that one is ended first. */
    {int travel=1;memcpy(test_scm_p2+SCM_ACTIVITY,&travel,4);}
    uirep_adapter_start(&pslots[2],3);assert(test_scm_sets==4 && test_scm_activity(test_scm_p2)==3 && scm_starts==2);
    /* Another ship (the first one died, the slot sits in a suit): the mode ends at the slot's next frame. */
    pslots[2].runtime=0x5678;assert(scm_frame_slot(2,1.0) && test_scm_sets==5 && !test_scm_activity(test_scm_p2) && scm_left==1 && !scm_ship[2]);
    /* On foot: no start; a slot that stands up loses the mode. */
    pslots[2].walking=1;uirep_adapter_start(&pslots[2],3);assert(test_scm_sets==5 && scm_unavailable==1 && !test_scm_activity(test_scm_p2));
    pslots[2].walking=0;uirep_adapter_start(&pslots[2],3);assert(test_scm_sets==6 && scm_ship[2]==0x5678);
    pslots[2].walking=1;assert(scm_frame_slot(2,1.0) && test_scm_sets==7 && scm_left==2);pslots[2].walking=0;
    /* A joiner meets the mode on without a noted ship: the slot's ship of that frame is taken. */
    {int scan=3;memcpy(test_scm_p2+SCM_ACTIVITY,&scan,4);}
    assert(!scm_ship[2] && scm_frame_slot(2,1.0) && scm_ship[2]==0x5678 && test_scm_sets==7 && scm_frame_slot(2,1.0) && scm_left==2);
    /* The deep scan. Its step does nothing while X4's block is on; its key is this client's own once the block is off
     * and the flight family is there. */
    test_scm_begin();test_scm_target[SCM_ALIVE]=1;test_scm_can_answer=1;test_scm_aborts=test_scm_finishes=0;
    assert(scm_scan_step(test_scm_p2,10.0) && !scm_scan_ticks && test_scm_progress()==0.0f);
    scm_scan_on=1;scm_fn.can=test_scm_can;scm_fn.abort=test_scm_abort;scm_fn.finish=test_scm_finish;
    assert(pstk_verdict(PSTK_ACTION,0x147)==PSTK_REFUSE);uirep_families|=UIREP_FAMILY_FLIGHT;
    assert(pstk_verdict(PSTK_ACTION,0x147)==PSTK_LOCAL && pstk_verdict_slot(PSTK_ACTION,0x147,2)==PSTK_LOCAL);
    /* On target: half a tick's time per tick, the finish with the target and the level after two seconds of ticks;
     * the slot's frame does the step. */
    {unsigned ticks=0;const unsigned want=(unsigned)(2.0/gate_tick_dt()+0.5);
     while(!test_scm_finishes && ticks<want+2) {assert(scm_frame_slot(2,10.0+ticks*gate_tick_dt()));++ticks;if(!test_scm_finishes) assert(test_scm_progress()>0.0f && test_scm_progress()<1.0f);}
     assert(test_scm_finishes==1 && ticks>=want && ticks<=want+1 && test_scm_finish_target==(void*)test_scm_target && test_scm_finish_level==2);
     assert(scm_scan_finished==1 && scm_scan_ticks==ticks && !test_scm_aborts);
     assert(scm_frame_slot(2,20.0) && scm_scan_ticks==ticks);}   /* no target: no step */
    /* Off target: two seconds of grace from the first tick off; back on target in time clears it. */
    test_scm_begin();test_scm_can_answer=0;
    assert(scm_scan_step(test_scm_p2,20.0) && test_scm_grace()==22.0 && test_scm_progress()==0.0f && !test_scm_aborts);
    assert(scm_scan_step(test_scm_p2,21.9) && test_scm_grace()==22.0 && !test_scm_aborts);
    test_scm_can_answer=1;assert(scm_scan_step(test_scm_p2,21.95) && test_scm_grace()==-1.0 && test_scm_progress()>0.0f);
    /* Off target past the grace: X4's abort. */
    test_scm_can_answer=0;assert(scm_scan_step(test_scm_p2,30.0) && test_scm_grace()==32.0);
    assert(scm_scan_step(test_scm_p2,32.0) && !test_scm_aborts && scm_scan_step(test_scm_p2,32.1) && test_scm_aborts==1 && scm_scan_aborted==1);
    /* The target left the world: the abort at once, the can-scan test not asked. */
    test_scm_begin();test_scm_target[SCM_ALIVE]=0;test_scm_can_answer=1;
    assert(scm_scan_step(test_scm_p2,40.0) && test_scm_aborts==2 && scm_scan_lost==1 && test_scm_finishes==1);
    {char status[384];assert(scm_status(status,sizeof(status))>0 && strstr(status,"armed=1 slots=1:0/0/0.00,2:1/0/0.00 starts=3 stops=1 left=2 unavailable=1 why=- scan=1/") &&
        strstr(status,"/1/1/1 scan_why=needs_flight"));}
    scm_scan_on=0;scm_fn=(scm_fns){0};assert(pstk_verdict(PSTK_ACTION,0x147)==PSTK_REFUSE);
    scm_stop();assert(!scm_armed && !scm_ship[2] && pstk_verdict(PSTK_ACTION,0x130)==PSTK_REFUSE && !uirep_activity_valid(2,3,0));
    scm_why="-";scm_scan_why="-";scm_pins_checked=pins_was;scm_starts=scm_stops=scm_unavailable=scm_left=0;
    scm_scan_ticks=scm_scan_finished=scm_scan_aborted=scm_scan_lost=0;
    uirep_fn=fn_was;pslots[2]=slot_was;pilot_slot_count=pilots_was;uirep_families=families_was;uirep_armed=uirep_was;pnat_armed=pnat_was;pnat_mode=mode_was;
    memcpy(image+PNAT_UNIVERSE,universe_was,8);
    pnat_base=base_was;stats.issued=issued_was;ui_thread=ui_was;stats.active=active_was;
    printf("scan mode: the key for every slot, the activity for slots 2..N, start, stop, the end on leaving the ship; "
           "deep scan: its key, the step on and off target, grace, abort, finish, a lost target PASS\n");
}
static void test_scan_mode_native_image(const char *path) {
    unsigned char *prior=image;const int pins_was=scm_pins_checked,scan_pins_was=scm_scan_pins_checked;
    image=test_pmath_map(path);scm_pins_checked=0;scm_scan_pins_checked=0;
    assert(scm_pins() && scm_scan_pins());
    {const unsigned char *at=image+SCM_KEY_CASE;   /* the key's case: activity 3 -> stop, else start */
     int32_t rel=0;
     assert(at[0x26]==0x83 && at[0x28]==0xb4 && at[0x29]==0x6a && at[0x2c]==0x03);            /* cmp dword [rcx+0x6ab4],3 */
     memcpy(&rel,at+0x97+1,4);assert(at[0x97]==0xe9 && 0xd86deau+5u+(uint32_t)rel==0x9fd330u);  /* jmp StopActivity */
     at=image+0xd86f5eu;memcpy(&rel,at+1,4);assert(at[0]==0xe9 && 0xd86f5eu+5u+(uint32_t)rel==0x9fd120u);   /* jmp StartActivity */
     /* the scan block's two branches, each inside one 16-byte block, and where their jumps land */
     assert(!memcmp(image+SCM_SKIP_TESTS,scm_tests_was,sizeof(scm_tests_was)) && !memcmp(image+SCM_SKIP_BODY,scm_body_was,sizeof(scm_body_was)));
     assert(SCM_SKIP_TESTS+2u+scm_tests_off[1]==0x712c8cu && (SCM_SKIP_TESTS&~15u)==((SCM_SKIP_TESTS+1u)&~15u));
     memcpy(&rel,scm_body_was+2,4);assert(SCM_SKIP_BODY+6u+(uint32_t)rel==0x712f15u);
     memcpy(&rel,scm_body_off+1,4);assert(SCM_SKIP_BODY+5u+(uint32_t)rel==0x712f15u && (SCM_SKIP_BODY&~15u)==((SCM_SKIP_BODY+5u)&~15u));
     /* the block's own calls of the abort and the finish, and the start's call of the can-scan test */
     at=image+0x712c87u;memcpy(&rel,at+1,4);assert(at[0]==0xe8 && 0x712c87u+5u+(uint32_t)rel==SCM_ABORT);
     at=image+0x712f0eu;memcpy(&rel,at+1,4);assert(at[0]==0xe8 && 0x712f0eu+5u+(uint32_t)rel==SCM_FINISH);
     at=image+0x735015u;memcpy(&rel,at+1,4);assert(at[0]==0xe8 && 0x735015u+5u+(uint32_t)rel==SCM_CAN);
     /* the fields the start writes: level, state, grace, progress, then the target through 0x734d70 */
     at=image+0x735032u;assert(at[0]==0x89 && at[1]==0x86 && !memcmp(at+2,"\x7c\xa6\x08\x00",4));
     at=image+0x73504du;assert(at[0]==0x48 && at[1]==0x89 && at[2]==0x86 && !memcmp(at+3,"\x80\xa6\x08\x00",4));
     at=image+0x735057u;assert(at[0]==0x89 && at[1]==0xae && !memcmp(at+2,"\x78\xa6\x08\x00",4));}
    image=prior;scm_pins_checked=pins_was;scm_scan_pins_checked=scan_pins_was;
    printf("pinned native image scan mode verified (the key's case of the mode handler, its start and stop; the scan block, its two branches, can-scan, abort, finish, the start's fields)\n");
}

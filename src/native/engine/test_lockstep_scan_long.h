/* lockstep_scan_long.h on synthetic memory: the mode, the charge's start, its power by the tick, the send, the losses. */
#define TEST_SCL_PLAYER 0x8a700u
static unsigned char test_scl_p1[TEST_SCL_PLAYER],test_scl_p2[TEST_SCL_PLAYER],test_scl_universe[0x400],test_scl_settings[0x4e00];
static unsigned test_scl_sent;static void *test_scl_sent_player;static float test_scl_sent_power;static int test_scl_can_answer;
static unsigned char test_scl_can(void *player,void *arg) {(void)player;assert(arg==(void*)test_scl_settings);return (unsigned char)test_scl_can_answer;}
static void test_scl_send(void *player) {
    ++test_scl_sent;test_scl_sent_player=player;memcpy(&test_scl_sent_power,(unsigned char*)player+SCL_POWER,4);((unsigned char*)player)[SCL_CHARGING]=0;
}
static void test_scl_hold(unsigned k,uint64_t tick,int on) {stats.issued=tick;pstk_fire_tick[k-1]=tick;pstk_fire2_on[k-1]=(unsigned char)on;}
static void test_scan_long(void) {
    const uint64_t issued_was=stats.issued;const DWORD ui_was=ui_thread;const uint32_t active_was=stats.active;
    unsigned char *base_was=pnat_base;const int pnat_was=pnat_armed,mode_was=pnat_mode,uirep_was=uirep_armed,pins_was=scl_pins_checked;
    const unsigned families_was=uirep_families,pilots_was=pilot_slot_count;unsigned char *p2_was=pslots[2].player;
    unsigned char universe_was[8],settings_was[8],arg_was[8],start_was[8],duration_was[4];
    uint64_t tick_was[2]={pstk_fire_tick[0],pstk_fire_tick[1]};unsigned char fire_was[2]={pstk_fire2_on[0],pstk_fire2_on[1]};
    pnat_base=image;
    memcpy(universe_was,image+PNAT_UNIVERSE,8);memcpy(settings_was,image+SCL_SETTINGS,8);memcpy(arg_was,image+SCL_CAN_ARG,8);
    memcpy(start_was,image+SCL_START,8);memcpy(duration_was,image+SCL_DURATION,4);
    memset(test_scl_p1,0,sizeof(test_scl_p1));memset(test_scl_p2,0,sizeof(test_scl_p2));memset(test_scl_universe,0,sizeof(test_scl_universe));
    memset(test_scl_settings,0,sizeof(test_scl_settings));
    {void *p=test_scl_universe;memcpy(image+PNAT_UNIVERSE,&p,8);p=test_scl_p1;memcpy(test_scl_universe+0x230,&p,8);
     p=test_scl_settings;memcpy(image+SCL_SETTINGS,&p,8);memcpy(image+SCL_CAN_ARG,&p,8);
     float low=2.0f,high=3.0f;memcpy(test_scl_settings+0x4d34,&low,4);memcpy(test_scl_settings+0x4d38,&high,4);
     double none=-1.0;memcpy(image+SCL_START,&none,8);
     p=test_scl_p1;memcpy(test_scl_p1+SCL_RESOURCE,&p,8);memcpy(test_scl_p2+SCL_RESOURCE,&p,8);}
    pslots[2].player=test_scl_p2;pilot_slot_count=2;
    ui_thread=GetCurrentThreadId();stats.active=1;stats.issued=0;
    scl_armed=0;scl_pins_checked=1;test_scl_sent=0;test_scl_can_answer=1;
    /* Not armed: nothing. Arming needs the stock slots, the UI replay with travel, the first tick ahead, the pins. */
    {int activity=SCL_SCAN;memcpy(test_scl_p2+SCL_ACTIVITY,&activity,4);}
    test_scl_hold(2,5,1);assert(scl_frame_slot(2,10.0) && !scl_starts && !scl_mode_on(2) && !scl_may_start(test_scl_p2));stats.issued=0;
    pnat_armed=0;assert(!scl_arm() && !strcmp(scl_why,"needs_stock_and_travel"));
    pnat_armed=1;pnat_mode=PNAT_MODE_STOCK;uirep_armed=1;uirep_families=UIREP_FAMILY_TRAVEL|UIREP_FAMILY_MISSILES|UIREP_FAMILY_TARGET;
    stats.issued=1;assert(!scl_arm() && !strcmp(scl_why,"late_or_thread"));stats.issued=0;
    scl_pins_checked=-1;assert(!scl_arm() && !strcmp(scl_why,"pins"));scl_pins_checked=1;
    assert(scl_arm() && scl_armed && scl_arm());scl_fn.can=test_scl_can;scl_fn.send=test_scl_send;
    /* The mode for slots 2..N: the toggle is replayed, the activity is valid, X4's test decides. */
    assert(pstk_verdict_slot(PSTK_ACTION,0x131,2)==PSTK_REPLAY && pstk_verdict_slot(PSTK_ACTION,0xe1,2)==PSTK_REFUSE);
    assert(uirep_activity_valid(2,2,0) && uirep_activity_valid(2,2,1) && !uirep_activity_valid(2,3,0) && !uirep_activity_valid(2,4,0));
    assert(scl_mode_on(2) && !scl_mode_on(1) && scl_may_start(test_scl_p2));
    /* Slot 2: the trigger down in the mode starts the charge; the power follows the tick; the release sends it. */
    test_scl_hold(2,10,0);assert(scl_frame_slot(2,100.0) && !scl_starts);
    test_scl_hold(2,11,1);assert(scl_frame_slot(2,100.0) && scl_starts==1 && scl_slots[1].charging && test_scl_p2[SCL_CHARGING]==1);
    {const float d=scl_slots[1].duration;float power=9.0f;double start=0;
     assert(d>=2.0f && d<=3.0f && d==scl_duration(2,11) && d!=scl_duration(2,12) && d!=scl_duration(3,11));
     memcpy(&start,image+SCL_START,8);assert(start==-1.0);   /* X4's globals are slot 1's alone */
     test_scl_hold(2,12,1);assert(scl_frame_slot(2,100.0+d/2));memcpy(&power,test_scl_p2+SCL_POWER,4);assert(power>0.2499f && power<0.2501f && !test_scl_sent);
     test_scl_hold(2,13,1);assert(scl_frame_slot(2,100.0+d+0.1));memcpy(&power,test_scl_p2+SCL_POWER,4);assert(power==1.0f);
     test_scl_hold(2,14,0);assert(scl_frame_slot(2,100.0+d+0.15) && test_scl_sent==1 && test_scl_sent_player==(void*)test_scl_p2 && test_scl_sent_power==1.0f);
     assert(scl_sends==1 && !scl_slots[1].charging && !test_scl_p2[SCL_CHARGING] && scl_last_power==1.0f);
     /* Held too long: the charge is lost, the release sends nothing worth it (power 0), as X4 does. */
     test_scl_hold(2,20,1);assert(scl_frame_slot(2,200.0) && scl_starts==2);
     {const float d2=scl_slots[1].duration;
      test_scl_hold(2,21,1);assert(scl_frame_slot(2,200.0+d2+0.3) && scl_lost==1);memcpy(&power,test_scl_p2+SCL_POWER,4);assert(power==0.0f);
      test_scl_hold(2,22,0);assert(scl_frame_slot(2,200.0+d2+0.4) && test_scl_sent==2 && test_scl_sent_power==0.0f);}}
    /* The mode left while charging: nothing is sent. No scanner resource, X4's test says no: no charge. */
    test_scl_hold(2,30,1);assert(scl_frame_slot(2,300.0) && scl_starts==3);
    {int none=0,scan=SCL_SCAN;memcpy(test_scl_p2+SCL_ACTIVITY,&none,4);
     test_scl_hold(2,31,1);assert(scl_frame_slot(2,300.5) && scl_cancelled==1 && !scl_slots[1].charging && !test_scl_p2[SCL_CHARGING] && test_scl_sent==2);
     test_scl_hold(2,32,0);assert(scl_frame_slot(2,301.0) && test_scl_sent==2);
     /* the trigger held from outside the mode into it: no press, no charge */
     test_scl_hold(2,33,1);assert(scl_frame_slot(2,302.0));memcpy(test_scl_p2+SCL_ACTIVITY,&scan,4);
     test_scl_hold(2,34,1);assert(scl_frame_slot(2,302.1) && scl_starts==3);test_scl_hold(2,35,0);assert(scl_frame_slot(2,302.2) && test_scl_sent==2);}
    {void *none=NULL,*some=test_scl_p1;memcpy(test_scl_p2+SCL_RESOURCE,&none,8);
     test_scl_hold(2,40,1);assert(scl_frame_slot(2,400.0) && scl_no_resource==1 && scl_starts==3);test_scl_hold(2,41,0);assert(scl_frame_slot(2,400.1));
     memcpy(test_scl_p2+SCL_RESOURCE,&some,8);test_scl_can_answer=0;
     test_scl_hold(2,42,1);assert(scl_frame_slot(2,401.0) && scl_refused==1 && scl_starts==3);test_scl_hold(2,43,0);assert(scl_frame_slot(2,401.1));test_scl_can_answer=1;}
    /* A trigger of another tick is not held (a slot without a record this tick). */
    stats.issued=50;pstk_fire_tick[1]=49;pstk_fire2_on[1]=1;assert(scl_frame_slot(2,500.0) && scl_starts==3);
    /* Slot 1: the same press writes X4's two globals (its own update computes the power); the release sends. */
    {int scan=SCL_SCAN;double start=0;float duration=0,power=0.36f;memcpy(test_scl_p1+SCL_ACTIVITY,&scan,4);
     test_scl_hold(1,60,1);assert(scl_frame_slot(1,600.0) && scl_starts==4 && test_scl_p1[SCL_CHARGING]==1);
     memcpy(&start,image+SCL_START,8);memcpy(&duration,image+SCL_DURATION,4);assert(start==600.0 && duration==scl_slots[0].duration && duration>=2.0f);
     memcpy(test_scl_p1+SCL_POWER,&power,4);   /* what X4's update wrote */
     test_scl_hold(1,61,1);assert(scl_frame_slot(1,601.0));memcpy(&power,test_scl_p1+SCL_POWER,4);assert(power==0.36f);   /* not ours to write */
     test_scl_hold(1,62,0);assert(scl_frame_slot(1,601.2) && test_scl_sent==3 && test_scl_sent_player==(void*)test_scl_p1 && test_scl_sent_power==0.36f);
     /* leaving the mode: the global start goes back to none */
     test_scl_hold(1,63,1);assert(scl_frame_slot(1,610.0) && scl_starts==5);
     {int none=0;memcpy(test_scl_p1+SCL_ACTIVITY,&none,4);test_scl_hold(1,64,1);assert(scl_frame_slot(1,610.5) && scl_cancelled==2);
      memcpy(&start,image+SCL_START,8);assert(start==-1.0);}}
    {char status[384];assert(scl_status(status,sizeof(status))>0 && strstr(status,"armed=1 slots=1:0") && strstr(status,"starts=5 sends=3 lost=1 cancelled=2 refused=1 no_resource=1 "));}
    scl_stop();assert(!scl_armed && pstk_verdict_slot(PSTK_ACTION,0x131,2)==PSTK_REFUSE && !uirep_activity_valid(2,2,0));
    scl_fn.can=NULL;scl_fn.send=NULL;scl_why="-";scl_pins_checked=pins_was;
    pstk_fire_tick[0]=tick_was[0];pstk_fire_tick[1]=tick_was[1];pstk_fire2_on[0]=fire_was[0];pstk_fire2_on[1]=fire_was[1];
    pslots[2].player=p2_was;pilot_slot_count=pilots_was;uirep_families=families_was;uirep_armed=uirep_was;pnat_armed=pnat_was;pnat_mode=mode_was;
    memcpy(image+PNAT_UNIVERSE,universe_was,8);memcpy(image+SCL_SETTINGS,settings_was,8);memcpy(image+SCL_CAN_ARG,arg_was,8);
    memcpy(image+SCL_START,start_was,8);memcpy(image+SCL_DURATION,duration_was,4);
    pnat_base=base_was;stats.issued=issued_was;ui_thread=ui_was;stats.active=active_was;
    printf("scan long: the mode for slots 2..N, charge by the trigger, power by the tick, send, lost and cancelled charges PASS\n");
}
/* The real image: the functions read are the ones called, the constant is X4's. */
static void test_scan_long_native_image(const char *path) {
    unsigned char *prior=image;const int pins_was=scl_pins_checked;
    image=test_pmath_map(path);scl_pins_checked=0;
    assert(scl_pins());
    {const unsigned char *at=image+0x71c8de;   /* state 0x19's branch: the test, activity 2, then start or send */
     int32_t rel=0;memcpy(&rel,at+0x72+1,4);assert(at[0x72]==0xe8 && 0x71c950u+5u+(uint32_t)rel==0x731a50u);
     memcpy(&rel,at+0x79+1,4);assert(at[0x79]==0xe8 && 0x71c957u+5u+(uint32_t)rel==SCL_SEND);}
    image=prior;scl_pins_checked=pins_was;
    printf("pinned native image scan long verified (can-scan test, charge start, per-frame power, send, the 0.2 s constant)\n");
}

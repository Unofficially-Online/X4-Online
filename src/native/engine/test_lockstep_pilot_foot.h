/* Slot 1 on foot (lockstep_pilot_foot.h, lockstep_pilot_foot_guard.h) on the fake image region of the stock tests:
 * the verdicts and the record, the FIRST_PERSON edge (local input side, recorded, replayed through X4's callback with
 * this client's +0x9063 kept), mouselook split between the camera here and the walking handler in the replay, the pick's
 * click refused, the docked menu's GetUp claimed into the record (slot 1) or refused (slots 2..N), the walking pass
 * without a PlayerController, the flight check's grace, adoption when slot 1 sits down again, the receipt's walking bank,
 * the schedule's install/intact/uninstall and every fail-closed path with stop reason 185. */
static unsigned char test_pfoot_on_original;
/* The context vector of a client with a loaded world: X4's GameClient::Init entry and the loaded player's FPC entry. */
static void *test_pfoot_vector[8];
static void test_pfoot_state(int id,unsigned char active) {   /* d7d3b0: one bank byte per id */
    test_pstk_log('w',id,active,NULL);pnat_base[PFOOT_BANK+0x40u+(unsigned)(id&0xf)]=active;
}
static void test_pfoot_range(int id,float value) {            /* d7d5c0 */
    test_pstk_log('y',id,value,NULL);memcpy(pnat_base+PFOOT_BANK+4u*(unsigned)(id&7),&value,4);
}
static void **test_pfoot_no_ship(void *player,void **out) {(void)player;*out=NULL;return out;}   /* 55aeb0 off the seat */
static void test_pfoot_action(int id) {test_pstk_log('u',id,0,NULL);pnat_base[PFOOT_BANK+0x39]^=1;}   /* d7d690: FP_RUN toggles */
static void test_pfoot_camera(int id,float value) {test_pstk_log('c',id,value,NULL);}                  /* 122d970: presentation */
/* d7d6c0 as X4 has it: the bank reset, the active flag, the player's flag, the input object's +0x9063. */
/* d81d20 as X4 has it: the FPC's contexts requested when asked and the FP share is zero, else withdrawn. */
static unsigned test_pfoot_fp_refreshes;
static unsigned test_pfoot_ui_calls[2];static unsigned char *test_pfoot_ui_flag;
static unsigned test_pfoot_getup_calls[2];static unsigned char test_pfoot_getup_node[0x30];
static void test_pfoot_getup_request(void) {   /* 730f30's effect on the set: one node (0x47, requester 6) */
    unsigned char *set=pfoot_input()+PFOOT_CONTEXT_SET,*node=test_pfoot_getup_node;uint64_t one=1;uint32_t value[2]={PFOOT_GETUP_CONTEXT,6u};
    ++test_pfoot_getup_calls[1];memset(node,0,sizeof(test_pfoot_getup_node));memcpy(node,&set,8);memcpy(node+PFOOT_CONTEXT_VALUE,value,8);
    memcpy(set+8,&node,8);memcpy(set+0x28,&one,8);
}
static void test_pfoot_getup_withdraw(void *at,int context,int requester) {
    unsigned char *set=pfoot_input()+PFOOT_CONTEXT_SET;
    assert(at==pfoot_input()+0x10 && context==(int)PFOOT_GETUP_CONTEXT && requester==6);++test_pfoot_getup_calls[0];memset(set,0,0x30);
}
static void *test_pfoot_canon_controller;   /* what X4's lookup of its own player's PlayerController answers now (client 1's K1) */
static void *test_pfoot_controller_of(void *key) {(void)key;return test_pfoot_canon_controller;}
static void test_pfoot_ui_off(void) {++test_pfoot_ui_calls[0];*test_pfoot_ui_flag=0;}
static void test_pfoot_ui_on(void) {++test_pfoot_ui_calls[1];*test_pfoot_ui_flag=1;}
static void test_pfoot_fp_refresh(char present) {
    ++test_pfoot_fp_refreshes;
    uint32_t share=*(volatile uint32_t*)(pnat_base+PFOOT_COUNTER);
    pnat_base[PFOOT_FP_REQUESTED]=(unsigned char)(present && !share);
}
static void test_pfoot_context_original(int context,unsigned char active) {
    test_pstk_log('F',context,active,NULL);
    if(context!=(int)PFOOT_CONTEXT) return;
    memset(pnat_base+PFOOT_BANK,0,PFOOT_BANK_SIZE);pnat_base[PFOOT_ACTIVE]=active;
    test_pnat_player[PFOOT_PLAYER_FLAG]=active;test_pnat_input[PFOOT_INPUT_FLAG]=active;
    test_pfoot_on_original=active;
}
static void test_pfoot_record(uint64_t tick) {   /* capture this tick on slot 1's client and stage it */
    unsigned char rec[PSTK_BYTES_MAX];unsigned length=0;const char *why="";
    stats.issued=tick;assert(pstk_capture(tick,rec,&length,&why) && pstk_stage(1,rec,length,tick));
}
static int test_pfoot_pass(uint64_t tick,unsigned char *controller) {
    stats.issued=tick;
    return pnat_pass(tick,controller,test_pnat_refresh,(uint32_t*)(pnat_base+PNAT_COUNTER),pnat_base+PNAT_CONTEXTS,pnat_base+PNAT_FLIGHT,pnat_authority_stock);
}
static void test_pilot_foot(void) {
    pstk_focus_forced=1;
    unsigned char *saved_image_base=pnat_base;pnat_fns saved_fns=pnat_fn;pstk_fns saved_pstk=pstk_fn;
    uint64_t saved_issued=stats.issued;unsigned saved_slot=stats.slot;
    const void *saved_ptrs[PSTK_HANDLERS];memcpy(saved_ptrs,pstk_handler_ptrs,sizeof(saved_ptrs));
    const uiguard_entry *saved_tab=uiguard_tab;unsigned saved_count=uiguard_count;int saved_guard=uiguard_armed;unsigned saved_groups=uiguard_groups;
    unsigned char *region=VirtualAlloc(NULL,TEST_PNAT_REGION,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(region);pnat_base=region;
    pnat_fn=(pnat_fns){test_pnat_refresh,test_pnat_controller_of,test_pnat_option,test_pnat_steer_mode,test_pnat_steer_direct,
        test_pnat_steer_reset,test_pnat_throttle,test_pnat_match_reset,test_pnat_state,test_pnat_range,test_pnat_space_original,
        test_pstk_cursor_original,test_pnat_time};
    pstk_fn=(pstk_fns){test_pstk_handoff,test_pstk_release,test_pstk_started,test_pstk_stopped,test_pstk_curve,test_pstk_authority,
        test_pstk_aim_original,test_pstk_transform,test_pstk_ship,test_pstk_ray,test_pstk_zone_of};
    {void *aim=(void*)test_pstk_aim_original;memcpy(region+PSTK_SHOOT_VTABLE+8*PSTK_AIM_SLOT,&aim,8);}
    pview_fns saved_view=pview_fn;pview_fn=(pview_fns){test_pview_fov,test_pview_floating,test_pview_portable};
    for(unsigned i=0;i<PVIEW_SITES;i++) memcpy(region+pview_sites[i].rva,pview_sites[i].original,pview_sites[i].length);
    test_pview_floating_value=0;
    DWORD saved_ui_thread=ui_thread;ui_thread=GetCurrentThreadId();
    pstk_handler_ptrs[0]=(void*)test_pstk_action;pstk_handler_ptrs[1]=(void*)test_pstk_state;pstk_handler_ptrs[2]=(void*)test_pstk_range;
    pstk_handler_ptrs[3]=(void*)test_pstk_action;pstk_handler_ptrs[4]=(void*)test_pstk_state;
    unsigned char *input=test_pnat_input,*controller=test_pnat_controller,*map=input+0x63e0,*states=map+0x40,*ranges=map+0x80;
    memset(test_pnat_input,0,sizeof(test_pnat_input));memset(test_pnat_universe,0,sizeof(test_pnat_universe));memset(test_pnat_player,0,sizeof(test_pnat_player));
    void *p=input;memcpy(region+PNAT_INPUT,&p,8);p=test_pnat_universe;memcpy(region+PNAT_UNIVERSE,&p,8);memcpy(region+PNAT_OPTIONS,&p,8);
    p=test_pnat_player;memcpy(test_pnat_universe+0x230,&p,8);
    static unsigned char config[0x300];memset(config,0,sizeof(config));p=config;memcpy(region+PNAT_CONFIG,&p,8);
    test_pnat_vtable[49]=(void*)test_pnat_assist;p=test_pnat_vtable;memcpy(controller,&p,8);
    p=test_pnat_ship;memcpy(controller+0x28,&p,8);test_pnat_ship[0xd1]=1;
    test_pnat_cursor_vtable[1]=(void*)test_pnat_cursor_slot1;test_pnat_cursor_vtable[2]=(void*)test_pnat_cursor_slot2;
    p=test_pnat_cursor_vtable;memcpy(test_pnat_cursor_object,&p,8);p=test_pnat_cursor_object;memcpy(input+0x8fe0,&p,8);
    /* The context vector: SPACE's d88710 and FIRST_PERSON's d7d6c0 twice (GameClient::Init's and the loaded player's
     * FirstPersonController's, d7c530: live w1foot stopped at take+1 when the install wanted exactly one). */
    memset(test_pfoot_vector,0,sizeof(test_pfoot_vector));
    test_pfoot_vector[0]=(void*)test_pnat_handler_action;test_pfoot_vector[1]=region+PNAT_SPACE_RVA;test_pfoot_vector[2]=(void*)test_pnat_handler_state;
    test_pfoot_vector[3]=(void*)test_pfoot_context_original;test_pfoot_vector[4]=(void*)test_pfoot_context_original;
    p=test_pfoot_vector;memcpy(input+0x10,&p,8);p=test_pfoot_vector+5;memcpy(input+0x18,&p,8);
    for(unsigned i=0;i<3;i++) {p=region+pnat_vf_originals[i];memcpy(region+PNAT_VTABLE+8*pnat_vf_slots[i],&p,8);}
    for(unsigned i=0;i<PNAT_PATCH_COUNT;i++) test_pnat_bytes(pnat_patches[i].rva,pnat_patches[i].expected);
    for(unsigned kind=0;kind<3;kind++) {pnat_node *h=(pnat_node*)(map+0x40*kind);memset(h,0,0x40);h->parent=h;}
    test_pnat_node_count=0;
    /* The multimaps as 9ed1f0 and 71b6c0 leave them (one id each kind is enough), and the camera's mouselook handler. */
    for(int id=0x1a;id<=0x24;id++) test_pnat_insert(states,id,(void*)test_pfoot_state);
    test_pnat_insert(states,0x57,(void*)test_pfoot_state);test_pnat_insert(states,0x78,(void*)test_pstk_state);
    test_pnat_insert(ranges,0xf,(void*)test_pfoot_range);test_pnat_insert(ranges,0x10,(void*)test_pfoot_range);
    test_pnat_insert(ranges,7,(void*)test_pfoot_range);test_pnat_insert(ranges,7,(void*)test_pfoot_camera);
    test_pnat_insert(map,0xdc,(void*)test_pfoot_action);test_pnat_insert(map,0x115,(void*)test_pstk_action);
    test_pnat_insert(states,0x11,(void*)test_pstk_state);
    {void *camera=test_pview_camera;memset(test_pview_camera,0,sizeof(test_pview_camera));memcpy(test_pnat_universe+0x3d0,&camera,8);}
    pfoot_context_original=test_pfoot_context_original;
    pfoot_handler_ptrs[0]=(void*)test_pfoot_state;pfoot_handler_ptrs[1]=(void*)test_pfoot_range;pfoot_handler_ptrs[2]=(void*)test_pfoot_action;
    uiguard_tab=uiguard_table;uiguard_count=UIGUARD_ENTRIES;uiguard_unclaim(NULL);
    /* Verdicts before the arming: X4's get up refused, walking local (as before this module). */
    assert(!pfoot_armed && pstk_verdict(PSTK_ACTION,0x115)==PSTK_REFUSE && pstk_verdict(PSTK_STATE,0x1a)==PSTK_LOCAL &&
           pstk_verdict(PSTK_RANGE,7)==PSTK_LOCAL && pstk_verdict(PSTK_STATE,0x78)==PSTK_LOCAL);
    {   pstk_record r,d;unsigned char b[PSTK_BYTES_MAX];memset(&r,0,sizeof(r));r.count=1;r.events[0]=(pstk_event){PSTK_SPACE,PFOOT_CONTEXT,1};
        unsigned n=pstk_encode(&r,b);assert(!pstk_decode(b,n,&d));}   /* no FIRST_PERSON edge without the module */
    /* Arming: after stock piloting only; the GetUp entry must exist and be free. */
    pilot_armed=1;pilot_suppress=1;pilot_source=PILOT_SOURCE_WIRE;pilot_slot_count=2;pilot_delay=3;pilot_device_armed=0;
    pnat_armed=0;pnat_mode=0;stats.slot=1;
    assert(!pfoot_arm_using() && !strcmp(pstk_arm_why,"foot_without_stock"));
    assert(pnat_arm_stock_using(1,1));
    {   unsigned index=UINT_MAX;for(unsigned i=0;i<uiguard_count;i++) if(!strcmp(uiguard_tab[i].name,"GetUp")) index=i;
        assert(index!=UINT_MAX && uiguard_tab[index].kind==UIGUARD_FFI && uiguard_tab[index].group==UIGUARD_G_FLIGHT && !uiguard_tab[index].covered);
        assert(uiguard_claim(index,uimis_claim));
        assert(!pfoot_arm_using() && !strcmp(pstk_arm_why,"foot_claim_taken") && !pfoot_armed);
        uiguard_unclaim(uimis_claim);
        uiguard_count=index;   /* a table without GetUp (the entries before it) */
        assert(!pfoot_arm_using() && !strcmp(pstk_arm_why,"foot_guard_entry"));
        uiguard_tab=uiguard_table;uiguard_count=UIGUARD_ENTRIES;
        pfoot_context_original=NULL;assert(!pfoot_arm_using() && !strcmp(pstk_arm_why,"foot_functions"));pfoot_context_original=test_pfoot_context_original;
        assert(pfoot_arm_using() && pfoot_armed && pfoot_getup_index==index && pfoot_claim_ready() && pfoot_state==PFOOT_SEATED);
        assert(pfoot_arm_using());}   /* identical rerun */
    /* Verdicts armed: slot 1's walking ids, GET_UP and mouselook are replay ids, the pick's click refused; slots 2..N refused. */
    for(int id=0x1a;id<=0x24;id++) assert(pstk_verdict(PSTK_STATE,id)==PSTK_REPLAY && pstk_verdict_slot(PSTK_STATE,id,2)==PSTK_REFUSE);
    assert(pstk_verdict(PSTK_STATE,0x57)==PSTK_REPLAY && pstk_verdict(PSTK_ACTION,0xdc)==PSTK_REPLAY && pstk_verdict(PSTK_ACTION,0x115)==PSTK_REPLAY);
    for(int id=0xd;id<=0x10;id++) assert(pstk_verdict(PSTK_RANGE,id)==PSTK_REPLAY && pstk_verdict_slot(PSTK_RANGE,id,2)==PSTK_REFUSE);
    assert(pstk_verdict(PSTK_RANGE,7)==PSTK_REPLAY && pstk_verdict(PSTK_RANGE,8)==PSTK_REPLAY && pstk_verdict_slot(PSTK_RANGE,8,2)==PSTK_REFUSE);
    assert(pstk_verdict(PSTK_STATE,0x78)==PSTK_REFUSE && pstk_verdict_slot(PSTK_ACTION,0x115,2)==PSTK_REFUSE);
    assert(pstk_verdict(PSTK_STATE,0x11)==PSTK_REPLAY && pstk_verdict_slot(PSTK_STATE,0x11,2)==PSTK_REPLAY &&   /* flight unchanged */
           pstk_verdict(PSTK_STATE,0x17)==PSTK_LOCAL && pstk_verdict(PSTK_RANGE,9)==PSTK_LOCAL);              /* the interact menu: UI */
    {   pstk_record r,d;unsigned char b[PSTK_BYTES_MAX];memset(&r,0,sizeof(r));r.count=2;
        r.events[0]=(pstk_event){PSTK_SPACE,PFOOT_CONTEXT,1};r.events[1]=(pstk_event){PSTK_STATE,0x1a,1};
        unsigned n=pstk_encode(&r,b);assert(pstk_decode(b,n,&d) && pstk_slot_record_ok(&d,1) && !pstk_slot_record_ok(&d,2));
        r.events[0]=(pstk_event){PSTK_SPACE,0xe,1};n=pstk_encode(&r,b);assert(!pstk_decode(b,n,&d));   /* another context */
        r.count=1;r.events[0]=(pstk_event){PSTK_SPACE,8,1};n=pstk_encode(&r,b);assert(pstk_decode(b,n,&d) && pstk_slot_record_ok(&d,2));}
    /* Schedule: start 10 (take 7, live 8), stop 60, delay 3. The swap needs the guard (the docked menu's GetUp). */
    pilot_start=10;pilot_stop=60;pilot_controls_owned=0;pnat_failed=0;pnat_fail_stage=NULL;pilot_failures=0;pnat_installed=0;pnat_restored=1;
    memset(pilot_slots,0,sizeof(pilot_slots));pilot_slots[0].player=1;
    *(uint32_t*)(region+PNAT_COUNTER)=0;region[PNAT_CONTEXTS]=1;region[PNAT_FLIGHT]=0;
    uiguard_armed=1;uiguard_groups=UIGUARD_ALL_GROUPS;
    /* Armed, before the window, controls parked at take: nothing of this module runs, no stop (w1foot: 185 at tick 37). */
    assert(test_pfoot_pass(5,controller) && test_pfoot_pass(6,controller) && !pfoot_installed && !pnat_failed && !pfoot_failed);
    assert(test_pfoot_pass(7,controller) && !strcmp(pnat_stage,"parked") && !pfoot_installed && test_pfoot_vector[3]==(void*)test_pfoot_context_original);
    {char why[768];assert(pilot_why_text(why,sizeof(why)) && strstr(why,"reason=91 ") && strstr(why," native=parked ") && strstr(why," foot_why=- "));}
    /* take+1: both of X4's entries are this module's now. */
    assert(test_pfoot_pass(8,controller) && pnat_is_live() && pfoot_installed && !pfoot_restored && pfoot_entries==2 && !pfoot_entries_late &&
           test_pfoot_vector[3]==(void*)pfoot_context && test_pfoot_vector[4]==(void*)pfoot_context && test_pfoot_vector[1]==(void*)pnat_space &&
           !strcmp(pfoot_stage,"live") && pfoot_local_fp==0);
    /* Capture on slot 1's client (tick 8): X4's FIRST_PERSON edge here runs the input side only and is recorded; walking
     * ids are recorded (nothing runs here), mouselook runs the camera here and is recorded, the pick's click is refused. */
    stats.issued=8;test_pstk_call_count=0;input[PFOOT_INPUT_FLAG]=0;
    pfoot_context(8,1);assert(!test_pstk_call_count && !pstk_build.count);   /* SPACE is d88710's */
    pfoot_context((int)PFOOT_CONTEXT,1);pfoot_context((int)PFOOT_CONTEXT,1);   /* X4 calls both entries: one edge */
    assert(!test_pstk_count('F') && input[PFOOT_INPUT_FLAG]==1 && pfoot_local_fp==1 && pstk_build.count==1 && pfoot_edges_local==1 && pfoot_edges_recorded==1);
    pnat_dispatch_state(map,0x1a,1);pnat_dispatch_range(map,0xf,0.75f);pnat_dispatch_action(map,0xdc);
    assert(!test_pstk_call_count && pstk_build.count==4);
    pnat_dispatch_range(map,7,0.5f);
    assert(test_pstk_count('c')==1 && !test_pstk_count('y') && pstk_build.count==5 && pfoot_split_local==1 && pfoot_split_recorded==1);
    {LONG64 refused=pnat_refused;pnat_dispatch_state(map,0x78,1);assert(pnat_refused==refused+1 && pfoot_refused_pick==1 && !test_pstk_count('s'));}
    /* The docked menu's GetUp in a local phase: the GET_UP key in the record, the menu gets true; X4's GetUp does not run. */
    {uint64_t ret=7;assert(pfoot_claim_decide(pfoot_getup_index,NULL,&ret)==1 && ret==1 && pstk_build.count==6 && pfoot_getups_claimed==1);
     ret=7;assert(!pfoot_claim_decide(pfoot_getup_index+1,NULL,&ret) && !ret);}   /* not its entry */
    {   unsigned char rec[PSTK_BYTES_MAX];unsigned length=0;const char *why="";pstk_record r;
        assert(pstk_capture(8,rec,&length,&why) && pstk_decode(rec,length,&r) && r.count==6);
        assert(r.events[0].kind==PSTK_SPACE && r.events[0].id==PFOOT_CONTEXT && r.events[0].value==1 && r.events[1].id==0x1a && r.events[2].kind==PSTK_RANGE &&
               r.events[3].id==0xdc && r.events[4].kind==PSTK_RANGE && r.events[4].id==7 && r.events[5].kind==PSTK_ACTION && r.events[5].id==0x115);
        assert(pstk_stage(1,rec,length,8));}
    for(uint64_t t=9;t<=12;t++) test_pfoot_record(t);
    assert(test_pfoot_pass(9,controller) && test_pfoot_pass(10,controller));
    /* Tick 11: every client runs X4's FIRST_PERSON callback (this client's +0x9063 kept), the walking handlers, the walking
     * handler alone for mouselook, and GET_UP through the player's handler. */
    input[PFOOT_INPUT_FLAG]=0;test_pstk_call_count=0;pnat_base[PFOOT_BANK+0x2a]=0x33;
    assert(test_pfoot_pass(11,controller));
    assert(test_pstk_calls[0].what=='F' && test_pstk_calls[0].id==(int)PFOOT_CONTEXT && test_pfoot_on_original==1 && !input[PFOOT_INPUT_FLAG] &&
           test_pnat_player[PFOOT_PLAYER_FLAG]==1 && region[PFOOT_ACTIVE]==1 && pfoot_on && pfoot_edges_replayed==1 && !pnat_base[PFOOT_BANK+0x2a]);
    assert(test_pstk_calls[1].what=='w' && test_pstk_calls[1].id==0x1a && test_pstk_calls[2].what=='y' && test_pstk_calls[2].id==0xf &&
           test_pstk_calls[3].what=='u' && test_pstk_calls[4].what=='y' && test_pstk_calls[4].id==7 && test_pstk_calls[5].what=='a' &&
           test_pstk_calls[5].id==0x115 && test_pstk_call_count==6 && !test_pstk_count('c'));
    assert(pfoot_state==PFOOT_LEAVING && pfoot_walking() && pfoot_getups_replayed==1 && pfoot_split_replayed==1);
    char text[1024];
    {   assert(pstk_frame_text(text,sizeof(text)));char *w=strstr(text,"|W1.1:");assert(w && strlen(w)==6+16 && strcmp(w+6,"0000000000000000"));
        uint64_t h=pfoot_bank_hash();pnat_base[PFOOT_BANK+0x4b]^=1;assert(pfoot_bank_hash()!=h);pnat_base[PFOOT_BANK+0x4b]^=1;
        pnat_base[PFOOT_COUNTER]^=1;assert(pfoot_bank_hash()==h);pnat_base[PFOOT_COUNTER]^=1;   /* the counter is not hashed */
        test_pnat_player[PFOOT_PLAYER_FLAG]^=1;assert(pfoot_bank_hash()!=h);test_pnat_player[PFOOT_PLAYER_FLAG]^=1;}
    /* Tick 12: X4 took the player out of the seat: no PlayerController, walking. Flight control may still be the
     * seated state until the replayed SPACE edge: tolerated for PFOOT_GRACE ticks, then it must match. */
    region[PNAT_FLIGHT]=1;
    assert(test_pfoot_pass(12,NULL) && pfoot_state==PFOOT_WALKING && pfoot_walking() && pfoot_walk_passes==1 && !strcmp(pfoot_stage,"walking"));
    assert(!pnat_controller);   /* X4 deleted the seat's controller with the stand-up: not held past this pass (live footlook4) */
    region[PNAT_FLIGHT]=0;
    /* Off the seat the player has no ship to aim from: a PlayerShootController's aim is X4's own (counted), not a stop. */
    {   pstk_ship_fn ship=pstk_fn.ship;pstk_fn.ship=test_pfoot_no_ship;float out[4];unsigned calls=test_pstk_aim_calls;
        assert(test_pstk_weapon_aim(region,out)==1 && test_pstk_aim_calls==calls+1 && pfoot_aim_off_seat==1 && !pnat_fail_stage);
        pstk_fn.ship=ship;}
    for(uint64_t t=13;t<=40;t++) {test_pfoot_record(t-3+3);}
    for(uint64_t t=13;t<=36;t++) assert(test_pfoot_pass(t,NULL));
    assert(pfoot_walk_passes==25 && pnat_is_live());
    region[PNAT_FLIGHT]=1;assert(test_pfoot_pass(37,NULL)==0 && !strcmp(pnat_stage,"foot_flight_drift") && pfoot_failed && pilot_stop_reason()==PFOOT_STOP_REASON);
    region[PNAT_FLIGHT]=0;pnat_failed=0;pfoot_failed=0;pnat_fail_stage=NULL;
    /* A PlayerController again on slot 1's ship: sat down, adopted (seated checks from then on); on another ship: stop. */
    {   static __attribute__((aligned(16))) unsigned char other[0x100],other_ship[0x100];void *q=test_pnat_ship;
        memcpy(other,controller,0x30);memcpy(other+0x28,&q,8);
        assert(test_pfoot_pass(37,other) && pfoot_state==PFOOT_SEATED && pfoot_adopted==1 && pnat_controller==other);
        pnat_controller=controller;pfoot_state=PFOOT_WALKING;q=other_ship;memcpy(other+0x28,&q,8);
        assert(!test_pfoot_pass(38,other) && !strcmp(pnat_stage,"foot_controller") && pilot_stop_reason()==PFOOT_STOP_REASON);
        pnat_failed=0;pfoot_failed=0;pnat_fail_stage=NULL;pfoot_state=PFOOT_WALKING;}
    /* A GET_UP that X4 did not take (the player stays seated): leaving ends after PFOOT_LEAVE_TICKS. */
    {   pfoot_state=PFOOT_LEAVING;pfoot_leave_tick=1;LONG64 kept=pfoot_kept_seated;
        assert(pfoot_check(10,controller,&pnat_controller,pnat_ship,0,0)==0 && pfoot_state==PFOOT_LEAVING);
        assert(pfoot_check(1+PFOOT_LEAVE_TICKS+1,controller,&pnat_controller,pnat_ship,0,0)==0 && pfoot_state==PFOOT_SEATED && pfoot_kept_seated==kept+1);
        pfoot_state=PFOOT_LEAVING;unsigned char *was=pnat_controller;
        assert(pfoot_check(5,test_pnat_ship,&pnat_controller,pnat_ship,0,0)<0 && !strcmp(pfoot_why,"foot_controller") && pnat_controller==was);
        pfoot_failed=0;pfoot_state=PFOOT_WALKING;pfoot_stage="walking";}
    /* While walking: mouselook stays split; the context swap must stay intact; the guard must stay armed. */
    test_pstk_call_count=0;pfoot_local_fp=0;
    stats.issued=39;pnat_dispatch_range(map,7,-0.25f);assert(test_pstk_count('c')==1 && !test_pstk_count('y') && pfoot_split_local==2);   /* pfoot_on */
    memset(&pstk_build,0,sizeof(pstk_build));
    /* An FPC constructed during the session pushes one more entry: swapped at the next pass, counted. */
    test_pfoot_vector[5]=(void*)test_pfoot_context_original;p=test_pfoot_vector+6;memcpy(input+0x18,&p,8);
    assert(test_pfoot_pass(39,NULL) && test_pfoot_vector[5]==(void*)pfoot_context && pfoot_entries==3 && pfoot_entries_late==1);
    /* None of this module's entries left (the vector rebuilt without them): the hook is lost. */
    for(unsigned i=3;i<6;i++) test_pfoot_vector[i]=(void*)test_pnat_handler_state;
    assert(!test_pfoot_pass(39,NULL) && !strcmp(pnat_stage,"foot_hook_lost") && pilot_stop_reason()==PFOOT_STOP_REASON);
    {char why[768];assert(pilot_why_text(why,sizeof(why)) && strstr(why,"reason=185 ") && strstr(why," foot_why=foot_hook_lost ") &&
                          strstr(why," native=foot_hook_lost ") && strstr(why," foot_entries=3/1"));}
    for(unsigned i=3;i<6;i++) test_pfoot_vector[i]=(void*)pfoot_context;
    pnat_failed=0;pfoot_failed=0;pnat_fail_stage=NULL;pfoot_why="-";
    uiguard_armed=0;assert(!test_pfoot_pass(39,NULL) && !strcmp(pnat_stage,"foot_needs_guard"));uiguard_armed=1;pnat_failed=0;pfoot_failed=0;pnat_fail_stage=NULL;
    /* Seated and nobody walking: mouselook is the camera's and the walking handler's here, as before the module. */
    {int on=pfoot_on;pfoot_on=0;pfoot_local_fp=0;test_pstk_call_count=0;pnat_dispatch_range(map,7,0.1f);
     assert(test_pstk_count('c')==1 && test_pstk_count('y')==1 && !pstk_build.count);pfoot_on=on;}
    /* A client of slot 2: its walking keys and the GetUp button are refused (the guard's refusal with its flight group). */
    {   int capturing=pstk_capturing;unsigned slot=pstk_capture_slot;pstk_capture_slot=2;LONG64 refused=pnat_refused;uint64_t ret=7;
        test_pstk_call_count=0;pnat_dispatch_state(map,0x1a,1);pnat_dispatch_action(map,0x115);
        assert(pnat_refused==refused+2 && !test_pstk_call_count && !pstk_build.count);
        pfoot_state=PFOOT_SEATED;
        assert(!pfoot_claim_decide(pfoot_getup_index,NULL,&ret) && !ret && pfoot_getups_refused==1);   /* the guard refuses (flight armed) */
        uiguard_groups=0;assert(pfoot_claim_decide(pfoot_getup_index,NULL,&ret)==1 && !ret && pfoot_getups_refused==2);   /* refused here */
        uiguard_groups=UIGUARD_ALL_GROUPS;pstk_capture_slot=slot;pstk_capturing=capturing;pfoot_state=PFOOT_WALKING;}
    /* With native_gate_pilot_foot_slots (lockstep_pilot_foot_slots.h): client 2's walking key and its Get Up button are
     * slot 2's, in its stock record, while slot 2 sits in its ship; up already, the button is refused as before. */
    {   int capturing=pstk_capturing;unsigned slot=pstk_capture_slot;void *saved_controller=pslots[2].controller;int saved_seated=pslots[2].seated;
        pstk_capturing=1;pstk_capture_slot=2;pslots[2].seated=1;pslots[2].controller=(void*)test_pnat_controller;pilot_slots[1].dead=0;
        pfslot_armed=1;pfslots[2].walking=0;memset(&pstk_build,0,sizeof(pstk_build));uint64_t ret=7;LONG64 claimed=pfslot_getups_claimed;
        test_pstk_call_count=0;pnat_dispatch_state(map,0x1a,1);
        assert(pstk_build.count==1 && pstk_build.events[0].kind==PSTK_STATE && pstk_build.events[0].id==0x1a && !test_pstk_call_count);
        assert(pfoot_claim_decide(pfoot_getup_index,NULL,&ret)==1 && ret==1 && pstk_build.count==2 && pstk_build.events[1].kind==PSTK_ACTION &&
               pstk_build.events[1].id==PFOOT_GET_UP && pfslot_getups_claimed==claimed+1);
        pfslots[2].walking=1;ret=7;assert(!pfoot_claim_decide(pfoot_getup_index,NULL,&ret) && !ret && pstk_build.count==2);
        pfslots[2].walking=0;pfslot_armed=0;memset(&pstk_build,0,sizeof(pstk_build));
        pstk_capture_slot=slot;pstk_capturing=capturing;pslots[2].controller=saved_controller;pslots[2].seated=saved_seated;}
    /* K1: a client of slot 2 while slot 1 walks. X4 withdrew this client's flight contexts (the canonical player has no
     * controller): that SPACE off edge and the on edge answering it are not slot 2's; menus still are; every pass puts
     * this client's contexts back to what slot 2 wants (flight on, walking off); client 1 stays X4's own. */
    {   int capturing=pstk_capturing;unsigned slot=pstk_capture_slot;void *saved_controller=pslots[2].controller;int saved_seated=pslots[2].seated;
        pstk_capturing=1;pstk_capture_slot=2;pslots[2].seated=1;pslots[2].controller=(void*)test_pnat_controller;pilot_slots[1].dead=0;
        pfoot_fp_refresh=test_pfoot_fp_refresh;region[PFOOT_CONTROLS_ENABLED]=1;*(uint32_t*)(region+PNAT_COUNTER)=0;*(uint32_t*)(region+PFOOT_COUNTER)=0;
        memset(&pstk_build,0,sizeof(pstk_build));pfoot_k1_held=0;LONG64 ignored=pfoot_k1_ignored,fixed=pfoot_k1_fixed;unsigned refreshes=test_pnat_refreshes;
        region[PNAT_CONTEXTS]=0;pstk_space(0);   /* 716e20 -> 70e0f0 -> d9cc30(0) on this client */
        assert(!pstk_build.count && pfoot_k1_held && pfoot_k1_ignored==ignored+1 && test_pnat_refreshes==refreshes+1 && region[PNAT_CONTEXTS]==1);
        pstk_space(1);assert(!pstk_build.count && !pfoot_k1_held && pfoot_k1_ignored==ignored+2 && pstk_local_active==1);
        *(uint32_t*)(region+PNAT_COUNTER)=1;pstk_space(0);   /* a menu on this client: slot 2's own edge */
        assert(pstk_build.count==1 && pstk_build.events[0].kind==PSTK_SPACE && pstk_build.events[0].id==8 && !pstk_build.events[0].value && !pfoot_k1_held);
        *(uint32_t*)(region+PNAT_COUNTER)=0;pstk_space(1);assert(pstk_build.count==2 && pstk_build.events[1].value==1);
        /* The enabled byte clear, no menu: with slot 1 seated a lock on this client, slot 2's own edge; with slot 1 off its
         * seat X4's stand-up of slot 1 cleared it (70e48b) and a refresh withdrew the contexts: not slot 2's, the pass
         * sets the byte again before X4's refresh reads it, and the on edge that answers is not recorded either. */
        {   const int state=pfoot_state;const LONG64 en=pfoot_k1_enabled,ig=pfoot_k1_ignored,fx=pfoot_k1_fixed;
            pfoot_state=PFOOT_SEATED;region[PFOOT_CONTROLS_ENABLED]=0;pstk_space(0);assert(pstk_build.count==3 && !pfoot_k1_held);
            region[PFOOT_CONTROLS_ENABLED]=1;pstk_space(1);assert(pstk_build.count==4 && pstk_build.events[3].value==1);
            pfoot_state=PFOOT_WALKING;region[PFOOT_CONTROLS_ENABLED]=0;pstk_space(0);
            assert(pstk_build.count==4 && pfoot_k1_held==1 && pfoot_k1_ignored==ig+1 && !region[PFOOT_CONTROLS_ENABLED]);
            pfoot_k1_reconcile();assert(region[PFOOT_CONTROLS_ENABLED]==1 && pfoot_k1_enabled==en+1 && pfoot_k1_held==1);
            pstk_space(1);assert(pstk_build.count==4 && !pfoot_k1_held && pfoot_k1_ignored==ig+2);
            /* ...and without any edge (the usual order: the contexts came back at once, then the byte went): set again. */
            region[PFOOT_CONTROLS_ENABLED]=0;pfoot_k1_reconcile();assert(region[PFOOT_CONTROLS_ENABLED]==1 && pfoot_k1_enabled==en+2);
            pfoot_state=PFOOT_SEATED;region[PFOOT_CONTROLS_ENABLED]=0;pfoot_k1_reconcile();assert(!region[PFOOT_CONTROLS_ENABLED]);   /* a lock stays */
            region[PFOOT_CONTROLS_ENABLED]=1;
            /* Slot 1 sits down while slot 2 walks: the on edge is not slot 2's, nor the off edge the pass's refresh gives. */
            pslots[2].controller=NULL;pstk_space(1);assert(pstk_build.count==4 && pfoot_k1_held==-1);
            pfoot_k1_reconcile();assert(!region[PFOOT_CONTROLS_ENABLED] && pfoot_k1_held==-1);
            pstk_space(0);assert(pstk_build.count==4 && !pfoot_k1_held);
            /* An open edge that slot 2 itself made true meanwhile is recorded by the pass: it stood up after a foreign off. */
            pslots[2].controller=(void*)test_pnat_controller;region[PFOOT_CONTROLS_ENABLED]=1;pfoot_state=PFOOT_WALKING;
            pstk_space(0);assert(pstk_build.count==4 && pfoot_k1_held==1);
            pslots[2].controller=NULL;pfoot_k1_reconcile();
            assert(pstk_build.count==5 && pstk_build.events[4].kind==PSTK_SPACE && !pstk_build.events[4].value && !pfoot_k1_held);
            pslots[2].controller=(void*)test_pnat_controller;pfoot_state=state;region[PFOOT_CONTROLS_ENABLED]=1;pstk_local_active=1;
            memset(&pstk_build,0,sizeof(pstk_build));pstk_build.count=3;InterlockedExchange64(&pfoot_k1_enabled,en);InterlockedExchange64(&pfoot_k1_ignored,ig);
            InterlockedExchange64(&pfoot_k1_fixed,fx);
        }
        /* The pass: flight contexts back, walking contexts away (70e0f0's d81d20(1) left them requested). */
        region[PNAT_CONTEXTS]=0;region[PFOOT_FP_REQUESTED]=1;unsigned fp=test_pfoot_fp_refreshes;
        pfoot_k1_reconcile();assert(region[PNAT_CONTEXTS]==1 && !region[PFOOT_FP_REQUESTED] && pfoot_k1_fixed==fixed+1 && test_pfoot_fp_refreshes==fp+1);
        pfoot_k1_reconcile();assert(pfoot_k1_fixed==fixed+1);   /* nothing to put back */
        {uint64_t h=pfoot_bank_hash();region[PFOOT_FP_REQUESTED]^=1;assert(pfoot_bank_hash()==h);region[PFOOT_FP_REQUESTED]^=1;}   /* local, not hashed */
        /* A dead slot 2 keeps its contexts as before (its keys are dropped; the respawn's edge relies on them). */
        pslots[2].controller=NULL;pilot_slots[1].dead=1;assert(pfoot_own_seated());pilot_slots[1].dead=0;assert(!pfoot_own_seated());
        region[PNAT_CONTEXTS]=1;pfoot_k1_reconcile();assert(!region[PNAT_CONTEXTS]);   /* not seated, not dead: none */
        pslots[2].controller=(void*)test_pnat_controller;
        /* The get-up key's context (0x47, requester 6) follows this client's own slot's seat: asked for while it has
         * one and the set lacks it (X4 withdrew it at slot 1's stand-up), withdrawn while it has none; agreeing,
         * unpinned or an unreadable set: nothing. */
        {   unsigned char *set=pfoot_input()+PFOOT_CONTEXT_SET,saved[0x30];void (*const rq)(void)=pfoot_getup_request;
            void (*const wd)(void*,int,int)=pfoot_context_withdraw;const LONG64 asked=pfoot_k1_getup_asked,gone=pfoot_k1_getup_withdrawn,bad=pfoot_k1_getup_unreadable;
            memcpy(saved,set,sizeof(saved));memset(set,0,0x30);test_pfoot_getup_calls[0]=test_pfoot_getup_calls[1]=0;
            pfoot_getup_request=test_pfoot_getup_request;pfoot_context_withdraw=test_pfoot_getup_withdraw;
            assert(vpl_slot_seated(2) && !pfoot_context_held(PFOOT_GETUP_CONTEXT,6u));
            pfoot_k1_getup();assert(test_pfoot_getup_calls[1]==1 && pfoot_context_held(PFOOT_GETUP_CONTEXT,6u)==1 && pfoot_k1_getup_asked==asked+1);
            assert(!pfoot_context_held(PFOOT_GETUP_CONTEXT,5u) && !pfoot_context_held(0x46u,6u));
            pfoot_k1_getup();assert(test_pfoot_getup_calls[1]==1 && !test_pfoot_getup_calls[0]);                     /* agrees */
            pslots[2].seated=0;pfoot_k1_getup();assert(test_pfoot_getup_calls[0]==1 && !pfoot_context_held(PFOOT_GETUP_CONTEXT,6u) && pfoot_k1_getup_withdrawn==gone+1);
            pfoot_k1_getup();assert(test_pfoot_getup_calls[0]==1);pslots[2].seated=1;
            {uint64_t many=PFOOT_CONTEXT_WALK+1u;memcpy(set+0x28,&many,8);pfoot_k1_getup();   /* not the shape this reads */
             assert(test_pfoot_getup_calls[1]==1 && pfoot_k1_getup_unreadable==bad+1);memset(set,0,0x30);}
            pfoot_getup_request=NULL;pfoot_k1_getup();assert(test_pfoot_getup_calls[1]==1);                          /* not pinned: X4's */
            {char out[4096];assert(pfoot_status(out,sizeof(out))>0 && strstr(out," k1_gu="));}
            pfoot_getup_request=rq;pfoot_context_withdraw=wd;memcpy(set,saved,sizeof(saved));
            InterlockedExchange64(&pfoot_k1_getup_asked,asked);InterlockedExchange64(&pfoot_k1_getup_withdrawn,gone);InterlockedExchange64(&pfoot_k1_getup_unreadable,bad);
        }
        /* The UI's half: the controller's first-person flag follows this client's own slot through X4's own two
         * functions; agreeing: nothing; no controller or no functions: nothing. */
        {   static _Alignas(16) unsigned char root[0x400],controller[0x80];static unsigned char *holder;const int farmed=pfslot_armed;
            const unsigned char walking=(unsigned char)pfslots[2].walking;void (*const f0)(void)=pfoot_fp_ui[0],(*const f1)(void)=pfoot_fp_ui[1];
            unsigned char *p=root,*before=NULL;memcpy(&before,region+PFOOT_UI_ROOT,8);const LONG64 ui=pfoot_k1_ui_fixed;
            memset(root,0,sizeof(root));memset(controller,0,sizeof(controller));holder=controller;
            memcpy(region+PFOOT_UI_ROOT,&p,8);p=(unsigned char *)&holder;memcpy(root+PFOOT_UI_HOLDER,&p,8);
            test_pfoot_ui_flag=controller+PFOOT_UI_FP_FLAG;test_pfoot_ui_calls[0]=test_pfoot_ui_calls[1]=0;
            pfoot_fp_ui[0]=test_pfoot_ui_off;pfoot_fp_ui[1]=test_pfoot_ui_on;pfslot_armed=1;pfslots[2].walking=1;
            pfoot_k1_ui();assert(test_pfoot_ui_calls[1]==1 && !test_pfoot_ui_calls[0] && controller[PFOOT_UI_FP_FLAG]==1 && pfoot_k1_ui_fixed==ui+1);
            pfoot_k1_ui();assert(test_pfoot_ui_calls[1]==1);                                    /* agrees */
            pfslots[2].walking=0;pfoot_k1_ui();assert(test_pfoot_ui_calls[0]==1 && !controller[PFOOT_UI_FP_FLAG]);   /* sat down, or slot 1's walk left it on */
            controller[PFOOT_UI_FP_FLAG]=1;holder=NULL;pfoot_k1_ui();assert(test_pfoot_ui_calls[0]==1);   /* no controller */
            holder=controller;pfoot_fp_ui[0]=NULL;pfoot_k1_ui();assert(test_pfoot_ui_calls[0]==1);      /* not pinned: X4's */
            pfoot_fp_ui[0]=f0;pfoot_fp_ui[1]=f1;pfslot_armed=farmed;pfslots[2].walking=walking;memcpy(region+PFOOT_UI_ROOT,&before,8);
            InterlockedExchange64(&pfoot_k1_ui_fixed,ui);
        }
        /* Client 1: X4's own answers, every edge its slot's; no reconcile. */
        pstk_capture_slot=1;memset(&pstk_build,0,sizeof(pstk_build));fixed=pfoot_k1_fixed;
        pstk_space(0);assert(pstk_build.count==1 && !pfoot_k1_held);region[PNAT_CONTEXTS]=0;pfoot_k1_reconcile();assert(!region[PNAT_CONTEXTS] && pfoot_k1_fixed==fixed);
        /* Client 1 once slots 2..N can walk: slot 2's stand-up and sit-down run X4's functions in slot 2's scope on this
         * client too. Their SPACE edges are not slot 1's (player 1 kept losing its flight control for as long as player 2
         * walked); the pass sets the enabled byte again and X4's refresh gives the contexts back, that edge unrecorded
         * as well. Slot 1's own edges, outside any scope, are recorded as before. */
        {   const int farmed=pfslot_armed;unsigned char *const pc=pnat_controller;const LONG64 en=pfoot_k1_enabled,ig=pfoot_k1_ignored,fx=pfoot_k1_fixed;
            const LONG64 tn=pfoot_k1_turned;const unsigned scope=pstk_scope_slot;void *(*const lookup)(void *)=pnat_fn.controller;
            pfslot_armed=1;pnat_controller=(unsigned char*)test_pnat_controller;memset(&pstk_build,0,sizeof(pstk_build));region[PFOOT_CONTROLS_ENABLED]=1;
            pnat_fn.controller=test_pfoot_controller_of;test_pfoot_canon_controller=test_pnat_controller;*(uint32_t*)(region+PNAT_COUNTER)=0;
            pstk_scope_slot=2;pstk_space(0);pstk_scope_slot=0;assert(!pstk_build.count && pfoot_k1_held==1 && pfoot_k1_ignored==ig+1);
            region[PFOOT_CONTROLS_ENABLED]=0;region[PNAT_CONTEXTS]=0;pfoot_k1_reconcile();
            assert(region[PFOOT_CONTROLS_ENABLED]==1 && pfoot_k1_enabled==en+1 && pfoot_k1_turned==tn);
            pstk_space(1);assert(!pstk_build.count && !pfoot_k1_held);
            /* X4 tells of the change at its next input poll, outside the scope: slot 1 holds its controls, they are enabled,
             * no menu: not its own; its byte still said "requested" (slot 2's scope had its own), turned for X4's refresh. */
            region[PNAT_CONTEXTS]=1;pstk_space(0);assert(!pstk_build.count && pfoot_k1_held==1);
            region[PNAT_CONTEXTS]=1;{const unsigned refreshes=test_pnat_refreshes;pfoot_k1_reconcile();
             assert(pfoot_k1_turned==tn+1 && test_pnat_refreshes==refreshes+1 && pfoot_k1_held==1);}
            pstk_space(1);assert(!pstk_build.count && !pfoot_k1_held);
            *(uint32_t*)(region+PNAT_COUNTER)=1;pstk_space(0);assert(pstk_build.count==1);                       /* a menu on this client: its own */
            *(uint32_t*)(region+PNAT_COUNTER)=0;pstk_space(1);assert(pstk_build.count==2 && !pfoot_k1_held);
            /* Slot 2 sits down while slot 1 walks (X4's player has no PlayerController): the on edge is not slot 1's, the
             * pass clears the byte the seat set and withdraws; slot 1's own stand-up (its controller gone) is recorded. */
            pnat_controller=NULL;test_pfoot_canon_controller=NULL;pstk_space(1);assert(pstk_build.count==2 && pfoot_k1_held==-1);
            pfoot_k1_reconcile();assert(!region[PFOOT_CONTROLS_ENABLED]);pstk_space(0);assert(pstk_build.count==2 && !pfoot_k1_held);
            region[PFOOT_CONTROLS_ENABLED]=0;pstk_space(0);assert(pstk_build.count==3 && !pstk_build.events[2].value && !pfoot_k1_held);
            {char out[4096];assert(pfoot_status(out,sizeof(out))>0 && strstr(out," k1_en="));}
            pfslot_armed=farmed;pnat_controller=pc;pstk_scope_slot=scope;region[PFOOT_CONTROLS_ENABLED]=1;memset(&pstk_build,0,sizeof(pstk_build));
            pnat_fn.controller=lookup;InterlockedExchange64(&pfoot_k1_turned,tn);
            InterlockedExchange64(&pfoot_k1_enabled,en);InterlockedExchange64(&pfoot_k1_ignored,ig);InterlockedExchange64(&pfoot_k1_fixed,fx);
        }
        memset(&pstk_build,0,sizeof(pstk_build));pstk_capturing=capturing;pstk_capture_slot=slot;pslots[2].controller=saved_controller;pslots[2].seated=saved_seated;
        region[PNAT_CONTEXTS]=1;pfoot_fp_refresh=NULL;}
    /* Already up: the button does not post a second GetUpEvent. */
    {uint64_t ret=7;assert(!pfoot_claim_decide(pfoot_getup_index,NULL,&ret) && !ret && pfoot_getups_claimed==1);}
    {   static char row[8192];assert(pnat_status(row,sizeof(row)) && strstr(row," foot=") && strstr(row," foot_state=2 foot_on=1 ") && strstr(row," getup=1/1 ") && strstr(row," pick_refused=1 ") &&
                                       strstr(row," foot_entries=3/1 k1=2/2"));}
    /* stop+1: the vector entry is X4's again. */
    pilot_stop=40;
    assert(test_pfoot_pass(41,NULL) && !pfoot_installed && pfoot_restored && test_pfoot_vector[3]==(void*)test_pfoot_context_original &&
           test_pfoot_vector[4]==(void*)test_pfoot_context_original && test_pfoot_vector[5]==(void*)test_pfoot_context_original);
    /* After the window X4's callback runs untouched. */
    test_pstk_call_count=0;pfoot_context((int)PFOOT_CONTEXT,0);assert(test_pstk_count('F')==1);
    pnat_armed=0;pnat_mode=0;pstk_capturing=0;pstk_slot=0;pilot_stock_slots=0;pstk_slot_mask=0;pstk_capture_slot=0;pnat_stage="off";
    pnat_failed=0;pnat_fail_stage=NULL;pfoot_armed=0;pfoot_reset_state();pfoot_stage="off";pfoot_installed=0;pfoot_restored=1;
    pfoot_entries=0;pfoot_entries_late=0;
    uiguard_unclaim(NULL);pfoot_getup_index=UINT_MAX;pfoot_context_original=NULL;memset(pfoot_handler_ptrs,0,sizeof(pfoot_handler_ptrs));
    /* K3: the look step's site on a stand-in: patched once, intact, restored; other bytes there refuse; no site, no patch.
     * While it is in, the capture asks this process's device state (none of it set here: a value is held back). */
    {   static unsigned char site[16];uint64_t v=PFOOT_LOOK_ORIGINAL,now=0;
        memcpy(site+3,&v,8);pfoot_look_site=site+3;
        assert(pfoot_look_record() && pfoot_look_install() && pfoot_look_patched && pfoot_look_intact() &&
               (memcpy(&now,site+3,8),now==PFOOT_LOOK_PATCHED) && pfoot_look_install());
        {const LONG64 held=pfoot_look_held;assert(!pfoot_look_device() && !pfoot_look_record() && pfoot_look_held==held+1);InterlockedExchange64(&pfoot_look_held,held);}
        site[4]^=1;assert(!pfoot_look_intact() && !pfoot_look_uninstall() && pfoot_look_patched);site[4]^=1;
        assert(pfoot_look_uninstall() && !pfoot_look_patched);memcpy(&now,site+3,8);assert(now==PFOOT_LOOK_ORIGINAL && pfoot_look_record());
        site[3]^=1;assert(!pfoot_look_install() && !pfoot_look_patched);
        pfoot_look_site=NULL;assert(pfoot_look_install() && !pfoot_look_patched && pfoot_look_intact() && pfoot_look_uninstall());
    }
    uiguard_tab=saved_tab;uiguard_count=saved_count;uiguard_armed=saved_guard;uiguard_groups=saved_groups;
    pilot_armed=0;pilot_suppress=0;pilot_source=0;pilot_slot_count=0;pilot_delay=0;pilot_controls_owned=0;pilot_failures=0;
    pilot_start=pilot_stop=0;pilot_stage="off";memset(pilot_slots,0,sizeof(pilot_slots));stats.issued=saved_issued;stats.slot=saved_slot;
    memcpy(pstk_handler_ptrs,saved_ptrs,sizeof(saved_ptrs));pstk_fn=saved_pstk;ui_thread=saved_ui_thread;
    pstk_aim_tick=0;pstk_aim_valid_replay=0;pstk_aim_installed=0;InterlockedExchange(&pstk_aim_on,0);
    pview_fn=saved_view;if(pview_page) {VirtualFree(pview_page,0,MEM_RELEASE);pview_page=NULL;}pview_installed=0;
    for(unsigned i=0;i<PILOT_SLOTS;i++) InterlockedExchange64(&pview_state[i],0);
    memset(test_pnat_universe+0x3d0,0,8);
    pnat_base=saved_image_base;pnat_fn=saved_fns;VirtualFree(region,0,MEM_RELEASE);
    printf("pilot foot: walking ids/GET_UP/mouselook split in the record, FIRST_PERSON edge, GetUp claim, walking pass, grace, adoption, receipt, fail-closed tests PASS\n");
}
/* The walking diagnostic (pfoot_diag): the bank as hashed, the FPC blocks, the parent, the client-local values. */
static void test_pilot_foot_diag(void) {
    unsigned char *saved_base=pnat_base;int saved_armed=pfoot_armed;unsigned saved_state=pfoot_state;int saved_on=pfoot_on;
    unsigned char *region=VirtualAlloc(NULL,TEST_PNAT_REGION,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(region);pnat_base=region;
    static __attribute__((aligned(16))) unsigned char universe[0x400],player[0x400],fpc[0x300],parent[0x80],camera[0x900],input[0x9100],
        tree[3][0x30],manager[0xc0];
    memset(universe,0,sizeof(universe));memset(player,0,sizeof(player));memset(fpc,0,sizeof(fpc));memset(parent,0,sizeof(parent));
    memset(camera,0,sizeof(camera));memset(input,0,sizeof(input));memset(tree,0,sizeof(tree));memset(manager,0,sizeof(manager));
    unsigned char *p;
    p=universe;memcpy(region+0x3d198f8u,&p,8);p=player;memcpy(universe+0x230,&p,8);p=camera;memcpy(universe+0x3d0,&p,8);
    p=fpc;memcpy(player+0x200,&p,8);p=parent;memcpy(player+0x70,&p,8);
    {void *vt=region+PFOOT_FPC_VTABLE;memcpy(fpc,&vt,8);}
    fpc[0x20]=0xab;fpc[0x88]=0x11;fpc[0x100]=0x22;fpc[0x19f]=0x33;fpc[0x2e0]=1;
    p=input;memcpy(region+0x6cf1560u,&p,8);input[0x9053]=1;
    /* The sensitivity map: header, root = FP_PITCH 0xe with FP_YAW 0xd on its left. */
    unsigned char *header=input+0x6380;p=tree[0];memcpy(header+8,&p,8);
    {int k=0xe;float v=2.5f;memcpy(tree[0]+0x1c,&k,4);memcpy(tree[0]+0x20,&v,4);}
    p=tree[1];memcpy(tree[0]+8,&p,8);
    {int k=0xd;float v=0.75f;memcpy(tree[1]+0x1c,&k,4);memcpy(tree[1]+0x20,&v,4);}
    {float v=0;assert(pfoot_sensitivity(input,0xd,&v) && v==0.75f && pfoot_sensitivity(input,0xe,&v) && v==2.5f && !pfoot_sensitivity(input,0xf,&v));}
    region[PFOOT_BANK]=0x5a;region[PFOOT_FP_REQUESTED]=1;region[PFOOT_COUNTER]=7;   /* this client's own: printed as zero */
    {uint32_t one=1;memcpy(camera+0x868,&one,4);memcpy(region+0x38f4ee0u,&one,4);}
    char out[4096];int n=pfoot_diag(out,sizeof(out));assert(n>0 && (size_t)n==strlen(out));
    {const char *b=strstr(out," foot_bank=5a");assert(b);b+=11;unsigned hex=0;while((b[hex]>='0' && b[hex]<='9') || (b[hex]>='a' && b[hex]<='f')) ++hex;assert(hex==2*PFOOT_BANK_SIZE);
     assert(b[2*(PFOOT_FP_REQUESTED-PFOOT_BANK)]=='0' && b[2*(PFOOT_FP_REQUESTED-PFOOT_BANK)+1]=='0' && b[2*(PFOOT_COUNTER-PFOOT_BANK)+1]=='0');}
    {const char *f=strstr(out," foot_fpc=ab");assert(f);f+=10;assert(f[0x80]=='/' && f[0x81+0x10]=='1' && f[0x81+0x11]=='1' && f[0x81+0xc0]=='/' &&
     f[0x81+0xc1]=='2' && f[0x81+0xc1+0x13e]=='3' && !strncmp(f+0x81+0xc1+0x140,"/01 ",4));}
    assert(strstr(out," foot_parent=0000000000000000/2 foot_locals=ext:1/cut:0/dev:1,0,0,0/sens:0.75,2.5/view:1"));
    /* Another controller class: no blocks; no sensitivities set. */
    {void *vt=region+0x1234;memcpy(fpc,&vt,8);}memset(header+8,0,8);
    assert(pfoot_diag(out,sizeof(out))>0 && strstr(out," foot_fpc=- ") && strstr(out,"/sens:-,-/"));
    /* In the status only while slot 1 is off its seat or its context is on. */
    pfoot_armed=1;pfoot_state=PFOOT_SEATED;pfoot_on=0;
    assert(pfoot_status(out,sizeof(out))>0 && !strstr(out," foot_bank="));
    pfoot_state=PFOOT_WALKING;assert(pfoot_status(out,sizeof(out))>0 && strstr(out," foot_bank=") && strstr(out," foot_locals="));
    assert(!pfoot_diag(out,512));   /* a short buffer prints nothing */
    pfoot_armed=saved_armed;pfoot_state=saved_state;pfoot_on=saved_on;pnat_base=saved_base;VirtualFree(region,0,MEM_RELEASE);
    printf("pilot foot diagnostic: bank as hashed, FPC blocks, parent, client-local values, sensitivity map, gating tests PASS\n");
}
/* The run's double-tap clock (pfoot_replay_state; report 14). d7d3b0's case 0x1a as X4 has it (d7d48f..d7d4e4): MD's input
 * lock, the forward byte, and on a press e9f90's real time (read under the time lock) into 0x38f4e70, the run flag 0x38f4e79
 * set when the press before is less than 0.4 s older. Two clients are one region run twice with their own real time: the
 * boot's at tick 0 (df2b's relay rows: 5.679268966666667 and 5.507744466666668, game 7.083) plus the fixed dt per tick,
 * accumulated as FrameTick does (tick 3903 gives df2b's 135.77926896666153 and 135.60774446666156 exactly). */
static CRITICAL_SECTION test_pfoot_time_cs;
static double test_pfoot_session;
static int test_pfoot_session_ok=1,test_pfoot_lock_depth,test_pfoot_forward_calls,test_pfoot_forward_throw;
static double test_pfoot_seen;
static int test_pfoot_time(double *now) {*now=test_pfoot_session;return test_pfoot_session_ok;}
static void test_pfoot_forward(int id,unsigned char active) {
    assert(id==0x1a);++test_pfoot_forward_calls;
    if(pnat_base[0x38f4dc8u]) return;                          /* MD's input lock */
    pnat_base[0x38f4e8bu]=active;
    if(!active) return;
    double now,last;
    EnterCriticalSection(&test_pfoot_time_cs);                 /* e9f90 */
    memcpy(&now,pnat_base+PFOOT_REAL_TIME,8);test_pfoot_lock_depth=(int)test_pfoot_time_cs.RecursionCount;
    LeaveCriticalSection(&test_pfoot_time_cs);
    test_pfoot_seen=now;
    memcpy(&last,pnat_base+PFOOT_TAP_TIME,8);memcpy(pnat_base+PFOOT_TAP_TIME,&now,8);
    if(last+0.4>now) pnat_base[0x38f4e79u]=1;
    if(test_pfoot_forward_throw) RaiseException(0xe0464f54,0,0,NULL);
}
/* One client: the bank reset (d7d6c0's half), presses of slot 1's forward key replayed at ticks a and b (released between);
 * the walking bank hash, e70 and the run flag after. */
typedef struct {uint64_t hash;double tap;unsigned char run;} test_pfoot_walk;
static test_pfoot_walk test_pfoot_client(unsigned char *states,double real0,uint64_t a,uint64_t b) {
    const double dt=1.0/30.0;double real=real0,game=7.083;
    memset(pnat_base+PFOOT_BANK,0,PFOOT_BANK_SIZE);{double minus=-1.0;memcpy(pnat_base+PFOOT_TAP_TIME,&minus,8);}pnat_base[PFOOT_ACTIVE]=1;
    for(uint64_t t=1;t<=b;t++) {
        real+=dt;game+=dt;
        if(t!=a && t!=b && t!=a+1) continue;
        memcpy(pnat_base+PFOOT_REAL_TIME,&real,8);test_pfoot_session=game;
        pstk_replay_walk(states,PSTK_STATE,0x1a,t==a+1?0u:1u);
        double back;memcpy(&back,pnat_base+PFOOT_REAL_TIME,8);assert(back==real);   /* this process's real time is back */
    }
    test_pfoot_walk w;w.hash=pfoot_bank_hash();memcpy(&w.tap,pnat_base+PFOOT_TAP_TIME,8);w.run=pnat_base[0x38f4e79u];
    return w;
}
static void test_pilot_foot_clock(void) {
    unsigned char *saved_base=pnat_base;pnat_fns saved_fns=pnat_fn;int saved_armed=pfoot_armed;
    const void *saved_ptrs[3];memcpy(saved_ptrs,pfoot_handler_ptrs,sizeof(saved_ptrs));
    CRITICAL_SECTION *saved_lock=pfoot_time_lock;unsigned saved_nodes=test_pnat_node_count;
    LONG64 saved_failures=pilot_failures,saved_async=pnat_async_failures;
    unsigned char *region=VirtualAlloc(NULL,TEST_PNAT_REGION,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(region);pnat_base=region;
    pnat_fn.time=test_pfoot_time;InitializeCriticalSection(&test_pfoot_time_cs);
    static __attribute__((aligned(16))) unsigned char states[0x40];
    {pnat_node *h=(pnat_node*)states;memset(h,0,0x40);h->parent=h;}
    test_pnat_insert(states,0x1a,(void*)test_pfoot_forward);   /* a fresh node; the count is put back at the end */
    pfoot_handler_ptrs[0]=(void*)test_pfoot_forward;
    const double c1=5.679268966666667,c2=5.507744466666668;
    /* Before (the module's replay without the session clock: X4's real time): df2b. One press at 3903: 0x38f4e70 is each
     * client's real time, the bank hash parts (|W2.1 from 3903). Presses at 7498 and 7510 (0.4 s apart): the run flag on
     * client 2 only. */
    pfoot_armed=0;pfoot_time_lock=NULL;
    {   test_pfoot_walk a=test_pfoot_client(states,c1,3903,3903),b=test_pfoot_client(states,c2,3903,3903);
        assert(a.tap==135.77926896666153 && b.tap==135.60774446666156 && a.hash!=b.hash && !a.run && !b.run);
        a=test_pfoot_client(states,c1,7498,7510);b=test_pfoot_client(states,c2,7498,7510);
        assert(!a.run && b.run && a.hash!=b.hash);}
    /* With the session clock: the same bank on both, e70 the game time of the press; the run flag from the session's 0.4 s. */
    pfoot_armed=1;pfoot_time_lock=&test_pfoot_time_cs;
    {   LONG64 calls=pfoot_clock_calls;
        test_pfoot_walk a=test_pfoot_client(states,c1,3903,3903),b=test_pfoot_client(states,c2,3903,3903);
        assert(a.tap==b.tap && a.tap==test_pfoot_session && a.hash==b.hash && !a.run && !b.run);
        assert(pfoot_clock_calls==calls+2 && test_pfoot_lock_depth==2);   /* the lock held around the call; e9f90 enters again */
        a=test_pfoot_client(states,c1,7498,7510);b=test_pfoot_client(states,c2,7498,7510);
        assert(a.hash==b.hash && a.run==b.run && !a.run);
        a=test_pfoot_client(states,c1,7498,7507);b=test_pfoot_client(states,c2,7498,7507);   /* 0.3 s: a run on both */
        assert(a.hash==b.hash && a.run && b.run);}
    /* Only X4's walking state handler: another handler of the same id gets X4's real time; another kind is not taken. */
    {   assert(pfoot_replay_clock(PSTK_STATE,(void*)test_pfoot_forward) && !pfoot_replay_clock(PSTK_RANGE,(void*)test_pfoot_forward) &&
               !pfoot_replay_clock(PSTK_STATE,(void*)test_pfoot_state) && !pfoot_replay_clock(PSTK_STATE,NULL));
        pfoot_armed=0;assert(!pfoot_replay_clock(PSTK_STATE,(void*)test_pfoot_forward));pfoot_armed=1;}
    /* A handler that faults: the real time is back and the lock free before the exception leaves the call. */
    {   double real=42.5;memcpy(region+PFOOT_REAL_TIME,&real,8);test_pfoot_session=99.0;test_pfoot_forward_throw=1;int caught=0;
        __try {pstk_replay_walk(states,PSTK_STATE,0x1a,1u);}
        __except(GetExceptionCode()==0xe0464f54?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
        test_pfoot_forward_throw=0;double back;memcpy(&back,region+PFOOT_REAL_TIME,8);
        assert(caught && back==42.5 && test_pfoot_seen==99.0 && !test_pfoot_time_cs.OwningThread && test_pfoot_time_cs.RecursionCount==0);}
    /* No session clock: nothing runs, the module stops (185, foot_clock). */
    {   int calls=test_pfoot_forward_calls;test_pfoot_session_ok=0;pnat_fail_stage=NULL;pnat_failed=0;pfoot_failed=0;
        pstk_replay_walk(states,PSTK_STATE,0x1a,1u);
        assert(test_pfoot_forward_calls==calls && pfoot_failed && !strcmp(pfoot_why,"foot_clock") && pnat_failed && !strcmp(pnat_fail_stage,"foot_clock"));
        test_pfoot_session_ok=1;pfoot_failed=0;pfoot_why="-";pfoot_stage="off";pnat_failed=0;pnat_fail_stage=NULL;}
    /* The status names the calls. */
    {char out[4096];assert(pfoot_status(out,sizeof(out))>0 && strstr(out," foot_clock="));}
    pilot_failures=saved_failures;pnat_async_failures=saved_async;
    DeleteCriticalSection(&test_pfoot_time_cs);
    test_pnat_node_count=saved_nodes;pfoot_time_lock=saved_lock;memcpy(pfoot_handler_ptrs,saved_ptrs,sizeof(saved_ptrs));
    pfoot_armed=saved_armed;pnat_fn=saved_fns;pnat_base=saved_base;VirtualFree(region,0,MEM_RELEASE);
    printf("pilot foot clock: the double-tap's real time is the session's in the replay (df2b's split and a one-sided run before), lock held, fault, no clock tests PASS\n");
}
/* The facts on the pinned image: the registrations and the vector push of 9ed1f0, the walking bank the handlers write,
 * the context callback, the GetUp export and 71bac0's GET_UP case posting the same event, the frame's FPC update. */
static void test_pilot_foot_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.Signature==IMAGE_NT_SIGNATURE && nt.FileHeader.NumberOfSections<=32);
    assert(fread(sections,sizeof(*sections),nt.FileHeader.NumberOfSections,f)==nt.FileHeader.NumberOfSections);
    unsigned char *mapped=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(mapped);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) if(sections[i].SizeOfRawData) {
        assert(!fseek(f,sections[i].PointerToRawData,SEEK_SET));
        unsigned size=sections[i].SizeOfRawData<sections[i].Misc.VirtualSize?sections[i].SizeOfRawData:sections[i].Misc.VirtualSize;
        assert(fread(mapped+sections[i].VirtualAddress,1,size,f)==size);
    }
    fclose(f);
    unsigned char *saved_image=image;image=mapped;pfoot_pins_checked=0;
    assert(pfoot_pins());
    /* K3: the look step's site in the image is what the patch expects, and the jump lands on the FPC's own test */
    {uint64_t at=0;memcpy(&at,image+PFOOT_LOOK_SITE,8);assert(at==PFOOT_LOOK_ORIGINAL && !memcmp(image+0xd7f340u,"\x80\xbb\xe0\x02\x00\x00\x00",7) &&
      PFOOT_LOOK_SITE+2u+0x51u==0xd7f340u);}
    /* lockstep_ui_reload_hold.h: the frame update's test of the UI reload flag is what the hold expects (je 9fe771 over
     * the call of the reload ab1c40, the flag's clear after it), its pin holds. */
    {uint64_t at=0;int32_t rel=0;memcpy(&at,image+UIRH_JUMP,8);memcpy(&rel,image+UIRH_JUMP+3,4);
     assert(at==UIRH_ORIGINAL && UIRH_JUMP+7u+(uint32_t)rel==0xab1c40u && hash_ok(image+UIRH_PIN,UIRH_PIN_SIZE,UIRH_PIN_SHA));
     memcpy(&rel,image+0x9fe75cu,4);assert(image[0x9fe75a]==0x80 && image[0x9fe75b]==0x3d && 0x9fe761u+(uint32_t)rel==UIRH_FLAG);}
    /* RIP-relative targets read in the code the pins hold. */
    #define TPF_RIP(at,len) ((unsigned)((at)+(len)+*(int32_t*)(mapped+(at)+(len)-4)))
    assert(mapped[0x9ed389]==0x48 && mapped[0x9ed38a]==0x8d && TPF_RIP(0x9ed389,7)==PFOOT_CONTEXT_RVA);          /* lea rax,d7d6c0: the vector push */
    assert(mapped[0x9ed395]==0x48 && TPF_RIP(0x9ed395,7)==0x6cf1560u && mapped[0x9ed3a0]==0x48 && mapped[0x9ed3a3]==0x10);   /* the input object's +0x10 */
    assert(mapped[0x9ed2e4-0x0f]==0x4c && TPF_RIP(0x9ed2d5,7)==PFOOT_STATE_HANDLER && *(uint32_t*)(mapped+0x9ed2dc+1)==0x23u);   /* FP_WALK -> d7d3b0 */
    assert(TPF_RIP(0x9ed339,7)==PFOOT_RANGE_HANDLER && *(uint32_t*)(mapped+0x9ed340+1)==0xfu);   /* FP_WALK range -> d7d5c0 */
    assert(mapped[0xd7d6c4]==0x83 && mapped[0xd7d6c5]==0xf9 && mapped[0xd7d6c6]==0x0d);          /* cmp ecx,0xd */
    assert(TPF_RIP(0xd7d6e4,6)==PFOOT_ACTIVE);                                                   /* mov [0x38f4e7d],dl */
    assert(TPF_RIP(0xd7d3ed,6)==0x38f4e84u && TPF_RIP(0xd7d49c,6)==0x38f4e8bu);                  /* the bank */
    assert(TPF_RIP(0xd7d4cb,8)==0x38f4e70u);
    /* The double-tap clock (report 14): case 0x1a (jump table d7d540/d7d574) is d7d48f; a press calls e9f90 (d7d4aa), which
     * reads the real time 0x6b60e40 between EnterCriticalSection and LeaveCriticalSection of 0x6b60e10; the old press time
     * plus 0.4 (0x2cbe7c0) against the new one sets 0x38f4e79. */
    assert(*(uint32_t*)(mapped+0xd7d540+4*mapped[0xd7d574])==0xd7d48fu && mapped[0xd7d4aa]==0xe8 &&
           (unsigned)(0xd7d4af+*(int32_t*)(mapped+0xd7d4ab))==0xe9f90u);
    assert(TPF_RIP(0xd7d4af,8)==PFOOT_TAP_TIME && TPF_RIP(0xd7d4c3,8)==0x2cbe7c0u && *(double*)(mapped+0x2cbe7c0)==0.4 && TPF_RIP(0xd7d4da,6)==0x38f4e79u);
    assert(TPF_RIP(0xe9f99,7)==PFOOT_TIME_LOCK && TPF_RIP(0xe9fa6,8)==PFOOT_REAL_TIME && TPF_RIP(0xe9fae,7)==PFOOT_TIME_LOCK &&
           TPF_RIP(0xe9fa0,6)==0x1bc1408u && TPF_RIP(0xe9fb5,6)==0x1bc1410u);
    assert(*(double*)(mapped+0x2cbf098)==-1.0 && TPF_RIP(0xd7cded,8)==PFOOT_TAP_TIME && TPF_RIP(0xd7d6dc,8)==PFOOT_TAP_TIME);   /* resets: -1.0 */
    assert(*(uint32_t*)(mapped+0xd7d784+2)==PFOOT_PLAYER_FLAG && *(uint32_t*)(mapped+0xd7d7d0+2)==PFOOT_INPUT_FLAG);   /* +0x6a0d, +0x9063 */
    assert(mapped[0x9fe5f7]==0xe8 && (unsigned)(0x9fe5fc+*(int32_t*)(mapped+0x9fe5f8))==0x9fc670u);
    assert(mapped[0x9fe604]==0xe8 && (unsigned)(0x9fe609+*(int32_t*)(mapped+0x9fe605))==0xd7f8d0u);
    assert(mapped[0x9fe2b4]==0x74 && mapped[0x9fe2d7]==0xe8 && (unsigned)(0x9fe2dc+*(int32_t*)(mapped+0x9fe2d8))==0xd95180u);
    /* GetUp and the GET_UP case: the same GetUpEvent vtable (0x2a85278), the same post 99a3f0. */
    assert(TPF_RIP(0x1ababe,7)==0x2a85278u && TPF_RIP(0x71bdf6,7)==0x2a85278u);
    assert((unsigned)(0x1abb0c+*(int32_t*)(mapped+0x1abb08))==0x99a3f0u && (unsigned)(0x71be44+*(int32_t*)(mapped+0x71be40))==0x99a3f0u);
    /* K1: d81d20 requests FIRST_PERSON (0xd) for the FPC requester (5) and marks 0x38f4e7c; d81ce0 clears it; d9cc30
     * withdraws SPACE (8) for the PlayerController requester (6, lea r8d,[rdx-2]); d7c530 (every FPC) pushes d7d6c0. */
    assert(*(uint32_t*)(mapped+0xd81d59+4)==PFOOT_CONTEXT && *(uint32_t*)(mapped+0xd81d61+5)==5u);
    assert(mapped[0xd81e5a]==0xc6 && mapped[0xd81e5a+6]==1 && (unsigned)(0xd81e5a+7+*(int32_t*)(mapped+0xd81e5a+2))==PFOOT_FP_REQUESTED);
    assert(mapped[0xd81d11]==0xc6 && mapped[0xd81d11+6]==0 && (unsigned)(0xd81d11+7+*(int32_t*)(mapped+0xd81d11+2))==PFOOT_FP_REQUESTED);
    assert(mapped[0xd9cc6a]==0xba && *(uint32_t*)(mapped+0xd9cc6b)==8u && mapped[0xd9cc7d]==0x44 && mapped[0xd9cc7e]==0x8d && mapped[0xd9cc7f]==0x42 && mapped[0xd9cc80]==0xfe);
    assert(mapped[0xd7c7ed]==0x48 && mapped[0xd7c7ee]==0x8d && TPF_RIP(0xd7c7ed,7)==PFOOT_CONTEXT_RVA);
    /* Bodies (lockstep_pilot_foot_body.h): 3d8420 pinned, 716670's lea/call pair, the filter object's vtable. */
    pfbody_pins_checked=0;assert(pfbody_pins());pfbody_pins_checked=0;
    assert(*(uint64_t*)(mapped+PFBODY_FILTER)==0x140000000ull+PFBODY_FILTER_VTABLE);
    /* What X4's parent change does to a Player besides the tree (the bodies' header; on-foot report 13g-13i). 3d8420 without a
     * target: 3d8240(component, 0x15), which skips its own Kill and calls the component's vfunc 0x1380, for a Player 702f40
     * -> 553660 -> 748790 -> 3d7010. There, no new parent (3d724c/3d7253 -> 3d7830), an old one (rbx), alive (+0xd3, 3d7847)
     * and unheld (+0xd4, 3d7851): Kill (vtable +0x1548) with 13, 'removed' (3d787f, 3d78af, 3d78b7). 702f40 makes the
     * Player a camera only when +0x4e8 is NULL (703051/703058 -> 71a560 at 70308e), which sets Universe+0x3d0 (71a744) and,
     * through 97df60, +0x3c8 (97df82). ~PlayerController (d84730) drops a head controller only from the camera at +0x3d0
     * (d849e3 -> d89840, d89855): with P_k's camera there, slot 1's own camera kept one bound to its freed controller (df2). */
    #define TPB_CALL(at) (mapped[at]==0xe8?(unsigned)((at)+5u+*(int32_t*)(mapped+(at)+1)):0u)
    #define TPB_IS(at,...) (!memcmp(mapped+(at),(const unsigned char[]){__VA_ARGS__},sizeof((const unsigned char[]){__VA_ARGS__})))
    assert(TPB_IS(0x3d8489,0xba,0x15,0,0,0) && TPB_CALL(0x3d848e)==0x3d8240u && TPB_IS(0x3d8252,0x83,0xfa,0x15) &&
           TPB_IS(0x3d8324,0xff,0x90,0x80,0x13,0,0) && *(uint64_t*)(mapped+VPS_PLAYER_VTABLE+0x1380)==0x140000000ull+0x702f40u);
    assert(TPB_CALL(0x70312a)==0x553660u && TPB_CALL(0x553785)==0x748790u && TPB_CALL(0x74896b)==0x3d7010u);
    assert(TPB_IS(0x3d724c,0x4d,0x8b,0x4e,0x70) && TPB_IS(0x3d7253,0x0f,0x84) && (unsigned)(0x3d7259u+*(int32_t*)(mapped+0x3d7255))==0x3d7830u);
    assert(TPB_IS(0x3d7842,0x48,0x85,0xdb) && TPB_IS(0x3d7847,0x41,0x80,0xbe,PFBODY_LIFECYCLE,0,0,0,0) &&
           TPB_IS(0x3d7851,0x41,0x83,0xbe,PFBODY_HOLD,0,0,0,0) && TPB_IS(0x3d787f,0x48,0x8b,0xb0,0x48,0x15,0,0) &&
           TPB_IS(0x3d78af,0xba,0x0d,0,0,0) && TPB_IS(0x3d78b7,0xff,0xd6));
    assert(TPB_IS(0x703051,0x48,0x39,0xbb,PFBODY_CAMERA&0xff,PFBODY_CAMERA>>8,0,0) && TPB_IS(0x703058,0x75,0x39) && TPB_CALL(0x70308e)==0x71a560u);
    assert(TPB_IS(0x71a744,0x48,0x89,0xbe,0xd0,0x03,0,0) && TPB_CALL(0x71a736)==0x97df60u && TPB_IS(0x97df82,0x48,0x89,0x8f,PFBODY_CAMERAS&0xff,PFBODY_CAMERAS>>8,0,0));
    assert(TPB_CALL(0xd849e3)==0xd89840u && TPB_IS(0xd89855,0x48,0x8b,0xb8,0xd0,0x03,0,0));
    #undef TPB_IS
    #undef TPB_CALL
    /* Slots 2..N on foot (lockstep_pilot_foot_slots.h): its pins; the GetUpEvent's handler (706cc0: 716d80, the conversation
     * manager idle, 716e20(player, 1, 0)); 716e20's placement (8d3f60, 455f90, 771430, class 0x4d, 7176f0 or 717c30) and
     * the control position none; 70eee0 (9fc670 first, d7c230, vfunc829 with replace 1); the first update at +0x80 and
     * its context ask d81d20 returning at once when 0x38f4e7c is set and 0x38f4e80 clear; the seat flag +0x2e0 from
     * d7d2e0, which compares the control position with 0x6d113d0; 717c30 asking 716d80 and the FPC (5a3da0). */
    #define TPF_CALL(at) (mapped[at]==0xe8?(unsigned)((at)+5u+*(int32_t*)(mapped+(at)+1)):0u)
    #define TPF_RIP8(at) ((unsigned)((at)+7u+*(int32_t*)(mapped+(at)+2)))
    pfslot_pins_checked=0;assert(pfslot_pins());pfslot_pins_checked=0;
    assert(TPF_RIP(0x706cd6,7)==PFSLOT_CONVERSATION && mapped[0x706cdd]==0x83 && *(uint32_t*)(mapped+0x706cdd+2)==0x1a0 && mapped[0x706cdd+6]==1);
    assert(TPF_CALL(0x706ccd)==PFSLOT_MAY_STAND && TPF_CALL(0x706cee)==0x716e20u && mapped[0x706ce9]==0xb2 && mapped[0x706cea]==1);
    assert(TPF_CALL(0x7172e1)==PFSLOT_CONN_TYPE && TPF_CALL(0x717303)==PFSLOT_SHIP_CONN && TPF_CALL(0x717322)==PFSLOT_DOCK_CONN &&
           TPF_CALL(0x7173a3)==PFSLOT_PLACE && TPF_CALL(0x717391)==0x7176f0u);
    assert(*(uint32_t*)(mapped+0x717344+1)==PFSLOT_TARGET_CLASS && *(uint32_t*)(mapped+0x71734c+2)==0x11e0u);
    assert(TPF_RIP(0x7173a8,6)==PFSLOT_POSITION_NONE && *(uint32_t*)(mapped+0x7173ae+3)==PFSLOT_POSITION && TPF_CALL(0x7170da)==PFSLOT_FPC_MAKE);
    assert(TPF_CALL(0x70eee9)==0x9fc670u && TPF_CALL(0x70ef79)==0xd7c230u && *(uint32_t*)(mapped+0x70ef58+3)==0x19e8u && mapped[0x70ef84]==0x41 &&
           mapped[0x70ef85]==0xb0 && mapped[0x70ef86]==1);
    assert(mapped[0xd81ed2]==0x80 && *(uint32_t*)(mapped+0xd81ed2+2)==PFSLOT_FPC_READY && TPF_CALL(0xd81eef)==0xd7cdb0u && TPF_CALL(0xd7ce74)==0xd81d20u);
    assert(TPF_RIP8(0xd81d2c)==PFOOT_COUNTER && TPF_RIP8(0xd81d39)==PFOOT_FP_REQUESTED && mapped[0xd81d40]==0x0f && mapped[0xd81d41]==0x85 &&
           (unsigned)(0xd81d46u+*(int32_t*)(mapped+0xd81d42))==0xd81eb9u && mapped[0xd81ebd]==0xc3);
    assert(mapped[0xd7cebc]==0x88 && *(uint32_t*)(mapped+0xd7cebc+2)==PFSLOT_FPC_SEAT && TPF_CALL(0xd7ceb4)==0xd7d2e0u);
    assert(TPF_RIP(0xd7d346,6)==PFSLOT_POSITION_NONE && *(uint32_t*)(mapped+0xd7d34c+2)==PFSLOT_POSITION);
    assert(TPF_CALL(0x717cb3)==PFSLOT_MAY_STAND && TPF_CALL(0x717ccb)==0x5a3da0u);
    #undef TPF_CALL
    #undef TPF_RIP8
    #undef TPF_RIP
    pfoot_pins_checked=0;image=saved_image;VirtualFree(mapped,0,MEM_RELEASE);
    printf("pilot foot image: pins, registrations, vector pushes (init and every FPC), walking bank, FIRST_PERSON callback, GetUp = GET_UP key, FPC update, K1 context requests, body connect pins, slot k's stand-up facts PASS\n");
}

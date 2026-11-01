/* Death and on foot together (lockstep_pilot_death.h with lockstep_pilot_foot_slots.h, lockstep_pilot_foot_body.h and
 * lockstep_pilot_foot.h; reports/LOCKSTEP-ON-FOOT-20261101.txt section 13), on the slot fixtures of test_pilot_slots
 * (client 2, slots 1 and 2, the foot fixtures of test_lockstep_pilot_foot_slots.h):
 *   - both foot modules arm with the death module (its suit and respawn);
 *   - the bodies' kill guard through X4's own call shape (a 6-byte call [rax+0x1408] become a 5-byte call to the near
 *     page): another component gets X4's answer; slot 2's seat ship asked where no scope can open: the body stays and is
 *     spared when the walk reaches it; asked where one can: the body off the room in slot 2's scope, X4's answer;
 *   - slot 2 dies seated (the kill already took its body off), waits in its suit without a body and cannot get up;
 *   - slot 1 gets up while slot 2 is in its suit: slot 2 updated from the pass, slot 1's deleted controller never read;
 *     slot 2 respawns while slot 1 walks; its body comes back PFBODY_SETTLE ticks after slot 1 sat down;
 *   - slot 2 then gets up (the body parked first) and walks; its empty docked ship dies: the frame's walking update skips
 *     it, the pass makes it a loss and no death (no suit, deaths unchanged, the walk ended, the seat's pointers back,
 *     stranded); the respawn seats it in a new ship and it gets up again from the FPC it kept;
 *   - slot 1 walking loses its ship: no death, it keeps walking; X4 killing slot 1 on foot: stop 163 player_killed;
 *   - slot 1 leaving (X4's stand-up pending) when its ship dies with it aboard: a death, the GET_UP void. */
static unsigned test_pdf_spare_calls;
static unsigned char test_pdf_spare(void *self) {(void)self;++test_pdf_spare_calls;return 0;}
static void test_pdf_frame(uint64_t tick,unsigned count,unsigned id) {
    stats.issued=tick;memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));pstk_slot_frames[1].tick=tick;pstk_slot_frames[1].r.count=count;
    pstk_slot_frames[1].r.events[0]=(pstk_event){PSTK_ACTION,id,0};
}
static int test_pdf_events(const char *want) {const char *e=pdeath_take_events();return want?e && !strcmp(e,want):!e;}
static void test_pilot_death_foot_block(unsigned char *region,unsigned char *input,unsigned char *p1,unsigned char *p2) {
    void *agent=test_pslot_agent2,*room=test_pslot_room;unsigned char *controller=test_pnat_controller;
    pfslot_fns saved_fn=pfslot_fn;uint64_t start=pilot_start,stop=pilot_stop,saved_issued=stats.issued;
    int capturing=pstk_capturing;unsigned capture_slot=pstk_capture_slot;
    unsigned char room_parent[8],ship_parent[8];memcpy(room_parent,test_pslot_room+0x70,8);memcpy(ship_parent,test_pslot_ship2+0x70,8);
    void *ship_is=test_pslot_ship_vt[PSLOT_IS_CLASS],*ship_spare=test_pslot_ship_vt[PFBODY_KILL_SLOT],*ship_kill=test_pslot_ship_vt[PDEATH_KILL_SLOT];
    void **pvt=(void**)(region+VPS_PLAYER_VTABLE);void *player_spare=pvt[PFBODY_KILL_SLOT];
    pslot_slot *s=&pslots[2];pfslot_slot *f=&pfslots[2];pdeath_slot *d1=&pdeath_slots[0],*d2=&pdeath_slots[1];
    double hull1=*(double*)(test_pnat_ship+PDEATH_HULL),hull2=*(double*)(test_pslot_ship2+PDEATH_HULL);
    LONG64 walk_passes=pfoot_walk_passes,adopted=pfoot_adopted,kept_seated=pfoot_kept_seated;const char *foot_stage=pfoot_stage;   /* slot 1's foot test counts from here */
    /* Client 2, the window 10..200 (take 7); slot 2 seated at 6 in its ship's cockpit room, docked (room -> ship -> dock). */
    test_pslot_reset();assert(pslot_arm_using(3));pslots[2].player=p2;pslot_p1=p1;memset(p2+0x70,0,8);memset(p2+0x200,0,8);
    memcpy(test_pslot_ship2+0x200,&agent,8);memcpy(test_pslot_ship2+PSLOT_ROOM,&room,8);test_pslot_parent=test_pslot_room;stats.issued=6;
    pilot_start=10;pilot_stop=200;pnat_input=input;pstk_capturing=1;pstk_capture_slot=2;pstk_local_active=0;pnat_controller=controller;pnat_ship=test_pnat_ship;
    pstk_flight=0;pfire_armed=0;pres_slot=0;test_pslot_p1_stood=0;
    memset(pilot_slots,0,sizeof(pilot_slots));
    pilot_slots[0].resolved=1;pilot_slots[0].object=test_pnat_ship;pilot_slots[0].player=1;pilot_slots[0].runtime=0x1001;pilot_slots[0].seen=1;
    pilot_slots[0].found=test_pnat_ship;
    pilot_slots[1].resolved=1;pilot_slots[1].object=test_pslot_ship2;pilot_slots[1].runtime=0x2001;pilot_slots[1].seen=1;pilot_slots[1].found=test_pslot_ship2;
    *(double*)(test_pnat_ship+PDEATH_HULL)=100.0;*(double*)(test_pslot_ship2+PDEATH_HULL)=100.0;test_pnat_ship[PDEATH_LIFECYCLE]=0;test_pslot_ship2[PDEATH_LIFECYCLE]=0;
    *(uint64_t*)(test_pnat_ship+8)=0x1001;*(uint64_t*)(test_pslot_ship2+8)=0x2001;
    test_pdeath_ids[0]=0x1001;test_pdeath_ids[1]=0x2001;test_pdeath_ids[2]=0;
    test_pdeath_objects[0]=test_pnat_ship;test_pdeath_objects[1]=test_pslot_ship2;test_pdeath_objects[2]=test_pslot_ship2;
    pdeath_find=(pilot_find_fn)test_pdeath_find;pdeath_map=test_pdeath_ids;pdeath_player=test_pnat_ship;
    {   int32_t call=(int32_t)((intptr_t)(region+PDEATH_GAME_OVER)-(intptr_t)(region+PDEATH_GAME_OVER_CALL+5));
        region[PDEATH_GAME_OVER_CALL]=0xe8;memcpy(region+PDEATH_GAME_OVER_CALL+1,&call,4);}
    test_pfslot_fpc_vt[3]=(void*)test_pfslot_fpc_is;test_pfslot_fpc_vt[20]=(void*)test_pfslot_fpc_next;
    {void *vt=test_pfslot_fpc_vt;memset(test_pfslot_fpc1,0,sizeof(test_pfslot_fpc1));memcpy(test_pfslot_fpc1,&vt,8);test_pfslot_fpc1[PFSLOT_FPC_READY]=1;}
    test_pslot_ship_vt[PSLOT_IS_CLASS]=(void*)test_pfslot_ship_is;
    {void *vt=test_pfslot_dock_vt,*dock=test_pfslot_dock,*ship=test_pslot_ship2;test_pfslot_dock_vt[PSLOT_IS_CLASS]=(void*)test_pfslot_dock_is;
     memset(test_pfslot_dock,0,sizeof(test_pfslot_dock));memcpy(test_pfslot_dock,&vt,8);memcpy(test_pslot_ship2+0x70,&dock,8);memcpy(test_pslot_room+0x70,&ship,8);}
    {void *vt=test_pfslot_target_vt;test_pfslot_target_vt[0x11e0/8]=(void*)test_pfslot_target_is;memset(test_pfslot_target,0,sizeof(test_pfslot_target));memcpy(test_pfslot_target,&vt,8);}
    {void *c=test_pfslot_conversation;memset(test_pfslot_conversation,0,sizeof(test_pfslot_conversation));*(int*)(test_pfslot_conversation+0x1a0)=1;
     memcpy(region+PFSLOT_CONVERSATION,&c,8);}
    *(int*)(region+PFSLOT_POSITION_NONE)=0x55;
    {int32_t rel=(int32_t)(PFSLOT_FPC_UPDATE-(PFSLOT_CALL_SITE+5u));region[PFSLOT_CALL_SITE]=0xe8;memcpy(region+PFSLOT_CALL_SITE+1,&rel,4);}
    memcpy(region+PFBODY_KILL_SITE,pfbody_kill_bytes,sizeof(pfbody_kill_bytes));
    pfslot_fn=(pfslot_fns){test_pfslot_may_stand,test_pfslot_make_fpc,test_pfslot_update_fn,test_pfslot_pose,test_pfslot_place,test_pfslot_conn_type,
        test_pfslot_ship_conn,test_pfslot_dock_conn};
    test_pfslot_may=1;test_pfslot_ship_found=0;memset(test_pfslot_fpc,0,sizeof(test_pfslot_fpc));
    test_pslot_ship_vt[PFBODY_KILL_SLOT]=(void*)test_pdf_spare;pvt[PFBODY_KILL_SLOT]=(void*)test_pdf_spare;test_pslot_ship_vt[PDEATH_KILL_SLOT]=(void*)test_pdeath_kill;
    test_pdeath_kills=0;
    /* Arming: the death module with its suit and a respawn 20 ticks after a death, then slot 1 on foot, slots 2..N on foot and
     * the bodies, as the probe arms them; neither foot module refuses the death module any more. */
    pdeath_armed=0;pdeath_failed=0;pdeath_why="-";test_pnat_eject_option=1;assert(pdeath_arm_using(20,1) && pdeath_armed && pdeath_suit);
    pfoot_armed=1;pfoot_failed=0;pfoot_why="-";pfoot_state=PFOOT_SEATED;pfoot_transition_tick=0;
    assert(pfslot_arm_using() && pfslot_armed);
    pfbody_connect=test_pfbody_connect;pfbody_filter=test_pfbody_filter_object;test_pfbody_mode=0;test_pfbody_calls=0;
    assert(pfbody_arm_using() && pfbody_armed);
    /* take-1 (6): the seat, the slots' call site (and their near page), the kill guard and the body; take (7): the walking
     * update's call site; the death module's guard from its first pass. */
    assert(pslot_seat(2) && s->seated && s->controller==test_pslot_c2);
    if(!pslot_hook_installed) assert(pslot_hook_install());
    assert(pfbody_pass(6,1) && pfbody_kill_installed && pfbody_mask==2 && *(void**)(p2+0x70)==room && !strcmp(pfbody_stage,"connected"));
    assert(pfslot_pass(7) && pfslot_hook_installed && pdeath_pass(7,controller) && pdeath_guard_installed);
    /* The kill guard as X4 reaches it: a thunk with X4's shape around the call (rax the vtable from [rcx], a 5-byte call to the
     * near page's stub, one byte after it that must be skipped, rsp 8 mod 16 at the stub as in KillRecursive). */
    unsigned char *thunk=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(thunk);
    {   static const unsigned char head[]={0x48,0x83,0xec,0x08,0x48,0x8b,0x01,0x49,0xbb},tail[]={0x41,0xff,0xd3,0xcc,0x48,0x83,0xc4,0x08,0xc3};
        void *page_stub=pslot_page+PFBODY_KILL_STUB;
        memcpy(thunk,head,sizeof(head));memcpy(thunk+sizeof(head),&page_stub,8);memcpy(thunk+sizeof(head)+8,tail,sizeof(tail));
        FlushInstructionCache(GetCurrentProcess(),thunk,64);}
    unsigned char (*asked)(void*)=(unsigned char(*)(void*))thunk;
    {   /* Any other component (slot 1's player): X4's own answer, nothing else. */
        test_pdf_spare_calls=0;assert(asked(p1)==0 && test_pdf_spare_calls==1 && pfbody_mask==2);
        /* Slot 2's ship asked while no scope can open (here: a presentation scope): the body stays; reached itself, it is spared
         * (1, X4's own answer not asked); the next pass's death path takes it off (late). */
        InterlockedExchange(&pres_open,1);
        assert(asked(test_pslot_ship2)==0 && test_pdf_spare_calls==2 && pfbody_mask==2 && !pfbody_kill_off);
        assert(asked(p2)==1 && test_pdf_spare_calls==2 && pfbody_kill_spared==1);
        InterlockedExchange(&pres_open,0);
        /* Where a scope can open: the body off the room in slot 2's scope first, then X4's answer for the ship. */
        test_pfbody_calls=0;
        assert(asked(test_pslot_ship2)==0 && test_pdf_spare_calls==3 && !pfbody_mask && pfbody_kill_off==1 && test_pfbody_calls==1 && !test_pfbody_target &&
               test_pfbody_singleton==p2 && *(void**)(p2+0x70)==room && test_pslot_singleton()==p1 && !pstk_scope_slot);
        assert(asked(p2)==0 && test_pdf_spare_calls==4);   /* no body in a room any more: X4's answer */
        assert(pfbody_kill_query(test_pslot_room)==0 && pfbody_kill_query(NULL)==0);}
    /* 20: slot 2 dies seated. KillRecursive went through (the room and ship dying: +0xd3), the body already off: the frame's
     * update skips the ship, the death pass retires the seat, the suit is due at +5. */
    test_pslot_ship2[PDEATH_LIFECYCLE]=1;*(double*)(test_pslot_ship2+PDEATH_HULL)=0.0;
    test_pslot_call_count=0;pslot_update_slots(100.0);assert(!test_pslot_count('U'));
    assert(pdeath_pass(20,controller) && pdeath_dead==2 && pilot_slots[1].dead && !s->seated && pslot_is_retired(test_pslot_c2) && !*(void**)(p2+0x70) &&
           d2->deaths==1 && !d2->lost && !d2->losses && d2->suit_tick==25 && d2->create_tick==40 && d2->seat_tick==55);
    assert(test_pdf_events("die:2,suit:2") && pfbody_guard(20) && !pfbody_mask && pfbody_pass(20,1));
    /* 25: into its suit (MD's answer); no body for a dead slot, however long everyone sits. */
    test_pslot_ship2[PDEATH_LIFECYCLE]=0;*(double*)(test_pslot_ship2+PDEATH_HULL)=100.0;*(uint64_t*)(test_pslot_ship2+8)=0x2003;test_pdeath_ids[2]=0x2003;
    memcpy(test_pslot_ship2+0x200,&agent,8);memset(s->retired,0,sizeof(s->retired));
    assert(pdeath_suit_ship(2,0x2003) && pdeath_pass(25,controller) && s->seated && d2->in_suit && pilot_slots[1].dead && test_pdf_events("insuit:2"));
    for(uint64_t t=26;t<=26+PFBODY_SETTLE+2;t++) assert(pfbody_guard(t));
    assert(!pfbody_mask && !pfbody_returns && !pfbody_seat_ok(2));
    /* Its GET_UP in the suit: the claim refuses a dead slot, its record is consumed and ignored. */
    {   LONG64 getups=f->getups;test_pdf_frame(30,1,PFOOT_GET_UP);
        assert(!pfslot_claim_ok(2) && pslot_replay(30) && f->getups==getups && !f->walking && !s->walking);}
    /* 31..32: slot 1 gets up while slot 2 waits in its suit: X4 stood it up (no PlayerController), slot 1 walking. X4 skips
     * d95180: slot 2 (in its suit) updates from the pass; the scope never reads slot 1's deleted controller. */
    pfoot_state=PFOOT_LEAVING;pfoot_leave_tick=31;test_pslot_p1_stood=1;
    assert(pdeath_pass(32,NULL) && !(pdeath_dead&1u) && !d1->lost && pfoot_check(32,NULL,&pnat_controller,pnat_ship,0,0)==1 &&
           pfoot_state==PFOOT_WALKING && !pnat_controller);
    {   test_pslot_call_count=0;pslot_update_tick=30;LONG64 gone=pslot_canonical_gone;
        assert(pslot_check(32) && test_pslot_count('U')==1 && test_pslot_calls[0].object==test_pslot_c2 && test_pslot_calls[0].singleton==p2 &&
               pslot_canonical_gone==gone && test_pslot_singleton()==p1);
        assert(pfbody_guard(32) && !pfbody_mask);}
    /* 40, 55: slot 2 respawns while slot 1 walks: out of its suit (killed as X4 kills a player's old suit), into the new ship. */
    assert(pdeath_pass(40,NULL) && d2->state==PDEATH_CREATING && test_pdf_events("create:2") && pdeath_ship(2,0x2004));
    *(uint64_t*)(test_pslot_ship2+8)=0x2004;test_pdeath_ids[1]=0x2004;test_pslot_call_count=0;
    assert(pdeath_pass(55,NULL) && !pdeath_dead && !pilot_slots[1].dead && s->seated && !d2->in_suit && s->runtime==0x2004 && d2->respawns==1 &&
           test_pdeath_kills==1 && test_pslot_count('X')==1 && test_pslot_count('T')==1 && *(void**)(p2+0x70)==room && test_pdf_events("live:2"));
    memset(&pstk_build,0,sizeof(pstk_build));
    /* Its body waits while slot 1 walks; slot 1 sits down (adopted) at 56; the body back PFBODY_SETTLE ticks later. */
    assert(pfbody_guard(55) && !pfbody_mask);
    test_pslot_p1_stood=0;assert(pfoot_check(56,controller,&pnat_controller,pnat_ship,0,0)==0 && pfoot_state==PFOOT_SEATED && pnat_controller==controller);
    for(uint64_t t=57;t<57+PFBODY_SETTLE;t++) assert(pfbody_guard(t) && !pfbody_mask);
    test_pfbody_calls=0;
    assert(pfbody_guard(57+PFBODY_SETTLE) && pfbody_mask==2 && pfbody_returns==1 && test_pfbody_target==room && *(void**)(p2+0x70)==room && pfbody_pass(90,1));
    /* 90: die seated, respawned, now it gets up: the body parked before the replay, then X4's stand-up path in its scope. */
    test_pdf_frame(90,1,PFOOT_GET_UP);
    assert(pfbody_guard(90) && !pfbody_mask && pfbody_parked);
    assert(pslot_replay(90));
    assert(f->walking && s->walking && s->seated && !s->controller &&
           f->fpc==test_pfslot_fpc && f->stood==1 && f->placed==1 && *(void**)(p2+0x70)==room && p2[PFOOT_PLAYER_FLAG]==1);
    /* 95: its empty docked ship dies while it walks. This frame's walking update skips it (the frame is the dying room). */
    *(double*)(test_pslot_ship2+PDEATH_HULL)=0.0;test_pslot_ship2[PDEATH_LIFECYCLE]=1;
    {   void (*hooked)(void*)=(void(*)(void*))(region+PFSLOT_CALL_SITE+5+*(int32_t*)(region+PFSLOT_CALL_SITE+1));
        LONG64 updates=f->updates;test_pfslot_updates=0;
        hooked(test_pfslot_fpc1);assert(test_pfslot_updates==1 && f->updates==updates && pfslot_dying_skips==1);
        assert(asked(test_pslot_ship2)==0 && !pfbody_mask);}   /* the guard: no body in a room while anyone walks */
    /* The pass: not aboard, so no death: lost, no suit, the walk ended (seat flag 1, first person off, its bank cleared),
     * the seat's pointers back, stranded (records ignored) until the respawn at +20. */
    f->bank[0x3d]=1;
    assert(pdeath_pass(95,controller) && test_pdf_events("lost:2") && d2->lost && d2->losses==1 && d2->deaths==1 && !d2->suit_tick && !d2->in_suit &&
           pilot_slots[1].dead && pdeath_dead==2 && d2->create_tick==115 && d2->seat_tick==130);
    assert(!f->walking && f->lost==1 && !s->walking && !s->seated && s->released && !*(void**)(p2+0x70) && test_pfslot_fpc[PFSLOT_FPC_SEAT]==1 &&
           !p2[PFOOT_PLAYER_FLAG] && !f->bank[0x3d] && !pfslot_own_walking() && pfoot_own_seated());
    assert(pfslot_pass(96) && !pfslot_failed && pfbody_guard(96) && pfbody_pass(96,1));
    {char row[512];assert(pdeath_status(row,sizeof(row)) && strstr(row," d2=1/1/1/95/lost") && strstr(row," losses=1"));}
    {char row[1024];assert(pfslot_status(row,sizeof(row)) && strstr(row," dying_skips=1") && strstr(row," fs2=0/1/0/1/0/0/0/1/0/1"));}
    {   LONG64 getups=f->getups;test_pdf_frame(100,1,PFOOT_GET_UP);assert(pslot_replay(100) && f->getups==getups && !pfslot_claim_ok(2));}
    /* 115, 130: the respawn seats it in a new ship (the stranded mark gone), and it gets up again from the FPC it kept. */
    assert(pdeath_pass(115,controller) && test_pdf_events("create:2") && pdeath_ship(2,0x2005));
    *(uint64_t*)(test_pslot_ship2+8)=0x2005;test_pdeath_ids[1]=0x2005;*(double*)(test_pslot_ship2+PDEATH_HULL)=100.0;test_pslot_ship2[PDEATH_LIFECYCLE]=0;
    assert(pdeath_pass(130,controller) && !pdeath_dead && !d2->lost && d2->respawns==2 && s->seated && s->controller==test_pslot_c2 && test_pdf_events("live:2"));
    memset(&pstk_build,0,sizeof(pstk_build));
    test_pdf_frame(140,1,PFOOT_GET_UP);
    assert(pslot_replay(140) && f->walking && f->stood==2 && f->fpc==test_pfslot_fpc && test_pfslot_fpc[PFSLOT_FPC_SEAT]==0);
    /* 150: slot 1 walking, its ship gone: no death, no ship asked for; it keeps walking (its records still apply). */
    pfoot_state=PFOOT_WALKING;pfoot_transition_tick=149;test_pslot_p1_stood=1;pnat_controller=NULL;pilot_slots[0].found=NULL;pilot_slots[0].resolved=0;
    assert(pdeath_pass(150,NULL) && test_pdf_events("lost:1") && d1->lost && d1->losses==1 && !d1->deaths && !pilot_slots[0].dead && !(pdeath_dead&1u) &&
           !pnat_ship && pfoot_state==PFOOT_WALKING);
    assert(pdeath_pass(151,NULL) && test_pdf_events(NULL) && d1->losses==1 && pfoot_check(151,NULL,&pnat_controller,pnat_ship,0,0)==1);
    {char row[512];assert(pdeath_status(row,sizeof(row)) && strstr(row," d1=0/0/0/0/lost") && strstr(row," losses=2"));}
    /* X4 killing its player on foot (the walk led it into what died): its game over never runs; the session stops (163). */
    {   void (*guard)(void)=(void(*)(void))(pslot_page+PDEATH_STUB);guard();
        assert(pdeath_game_overs==1 && pdeath_failed && pnat_failed && pilot_stop_reason()==PDEATH_STOP_REASON && !pdeath_pass(152,NULL) &&
               !strcmp(pdeath_why,"player_killed"));
        pdeath_failed=0;pdeath_why="-";pdeath_stage="live";pdeath_game_overs=0;pnat_failed=0;pnat_fail_stage=NULL;pilot_failures=0;}
    /* 160: slot 1 leaving (a GET_UP replayed, X4's stand-up still pending) when its ship dies with it aboard: the eject's suit
     * holds a controller, so a death; the GET_UP is void. */
    {   static unsigned char suit_controller[0x100];
        d1->lost=0;pilot_slots[0].found=test_pnat_ship;pilot_slots[0].resolved=1;*(double*)(test_pnat_ship+PDEATH_HULL)=0.0;
        pfoot_state=PFOOT_LEAVING;pfoot_leave_tick=159;test_pslot_p1_stood=0;pnat_controller=controller;pnat_ship=test_pnat_ship;LONG64 kept=pfoot_kept_seated;
        assert(pdeath_pass(160,suit_controller) && test_pdf_events("die:1") && pilot_slots[0].dead && (pdeath_dead&1u) && d1->deaths==1 &&
               pfoot_state==PFOOT_SEATED && pfoot_kept_seated==kept+1 && !pnat_controller);}
    /* The kill guard's counters, in the bodies' status row. */
    {char row[512];assert(pfbody_status(row,sizeof(row)) && strstr(row," returns=1 kill=1/1/1/0"));}
    /* stop+1: the walking update's call site, the bodies (the kill guard after the last body), the walking seat, the slots. */
    assert(pfslot_pass(201) && !pfslot_hook_installed && pfbody_pass(201,0) && !pfbody_kill_installed && !memcmp(region+PFBODY_KILL_SITE,pfbody_kill_bytes,8));
    assert(pslot_release(2) && !s->seated && !*(void**)(p2+0x70) && pslot_hook_uninstall());
    pdeath_restore();assert(!pdeath_guard_installed);
    VirtualFree(thunk,0,MEM_RELEASE);
    /* Leave nothing behind for the blocks below. */
    pdeath_armed=0;pdeath_failed=0;pdeath_dead=0;pdeath_why="-";pdeath_suit=0;pdeath_find=NULL;pdeath_map=NULL;pdeath_player=NULL;memset(pdeath_slots,0,sizeof(pdeath_slots));
    pfslot_armed=0;pfslot_failed=0;pfslot_hook_installed=0;pfslot_stage="off";pfslot_why="-";pfslot_hook_calls=pfslot_late=pfslot_getups_claimed=pfslot_dying_skips=0;
    memset(pfslots,0,sizeof(pfslots));pfslot_fn=saved_fn;
    pfbody_armed=0;pfbody_failed=0;pfbody_parked=0;pfbody_seated_since=0;pfbody_mask=0;pfbody_stage="off";pfbody_why="-";memset(pfbody_back,0,sizeof(pfbody_back));
    pfbody_connects=pfbody_disconnects=pfbody_checks=pfbody_parks=pfbody_unparks=pfbody_returns=0;pfbody_kill_off=pfbody_kill_spared=pfbody_kill_late=0;
    pfbody_connect=NULL;pfbody_filter=NULL;
    pfoot_armed=0;pfoot_failed=0;pfoot_why="-";pfoot_state=PFOOT_SEATED;pfoot_transition_tick=0;test_pslot_p1_stood=0;
    pfoot_walk_passes=walk_passes;pfoot_adopted=adopted;pfoot_kept_seated=kept_seated;pfoot_stage=foot_stage;
    test_pslot_ship_vt[PSLOT_IS_CLASS]=ship_is;test_pslot_ship_vt[PFBODY_KILL_SLOT]=ship_spare;test_pslot_ship_vt[PDEATH_KILL_SLOT]=ship_kill;
    pvt[PFBODY_KILL_SLOT]=player_spare;
    memcpy(test_pslot_room+0x70,room_parent,8);memcpy(test_pslot_ship2+0x70,ship_parent,8);
    memset(region+PFSLOT_CONVERSATION,0,8);memset(region+PFOOT_BANK,0,PFOOT_BANK_SIZE);
    memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));memset(p2+0x200,0,8);memset(p2+0x70,0,8);memset(&pstk_build,0,sizeof(pstk_build));
    memset(pilot_slots,0,sizeof(pilot_slots));
    pilot_slots[0].resolved=1;pilot_slots[0].object=test_pnat_ship;pilot_slots[0].player=1;
    pilot_slots[1].resolved=1;pilot_slots[1].object=test_pslot_ship2;pilot_slots[1].player=0;
    *(uint64_t*)(test_pslot_ship2+8)=0;*(uint64_t*)(test_pnat_ship+8)=0;*(double*)(test_pnat_ship+PDEATH_HULL)=hull1;*(double*)(test_pslot_ship2+PDEATH_HULL)=hull2;
    test_pslot_ship2[PDEATH_LIFECYCLE]=0;memcpy(test_pslot_ship2+0x200,&agent,8);memset(test_pslot_c2,0,sizeof(test_pslot_c2));
    pnat_controller=controller;pnat_ship=test_pnat_ship;pstk_flight=0;
    pilot_start=start;pilot_stop=stop;stats.issued=saved_issued;pstk_capturing=capturing;pstk_capture_slot=capture_slot;
    printf("pilot death and foot: both arm together; kill guard (body off at the seat's kill, spared where no scope opens); slot 2 dies seated, "
           "suit without body, slot 1 up meanwhile, respawn, body back, gets up; empty docked ship dies while walking (lost, no death, "
           "stranded, respawn, up again); slot 1 walking keeps walking without its ship; killed on foot 163; leaving and ejected: GET_UP void PASS\n");
}

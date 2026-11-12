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
/* The suit's weapons (pvehk_equip): X4's installer, group and active-group calls as far as the module sees them. */
static unsigned test_pdf_installs,test_pdf_grouped[2],test_pdf_activated[2],test_pdf_ended;static unsigned char test_pdf_weapon[4][16],test_pdf_macro_block[0x10c0];
static void test_pdf_install(void *ship,void **weapon,void *description) {
    void *macro=NULL,*vt=NULL;uint64_t cap=0;
    memcpy(&vt,description,8);memcpy(&cap,(unsigned char*)description+0x28,8);memcpy(&macro,(unsigned char*)description+0x40,8);
    assert(ship==test_pslot_ship2 && vt==(void*)(pnat_base+PVEHK_DESCRIPTION_VTABLE) && cap==4 && test_pdf_installs<4);
    *weapon=macro==(void*)0x3333?NULL:test_pdf_weapon[test_pdf_installs];   /* 0x3333: a macro the suit has no place for */
    ++test_pdf_installs;
}
static void test_pdf_group(void *ship,void *weapon,unsigned group,unsigned char primary) {(void)ship;(void)weapon;test_pdf_grouped[primary?1:0]+=1u+16u*group;}
static void test_pdf_activate(void *ship,unsigned group,unsigned char primary) {(void)ship;test_pdf_activated[primary?1:0]=group+1u;}
static unsigned char *test_pdf_macro_data(void *macro) {(void)macro;return test_pdf_macro_block;}
static void test_pdf_description_end(void *description) {(void)description;++test_pdf_ended;}
/* The block's state before it, put back at its end. */
typedef struct {
    pfslot_fns fn;uint64_t start,stop,issued;int capturing;unsigned capture_slot;
    unsigned char room_parent[8],ship_parent[8];void *ship_is,*ship_spare,*ship_kill,*player_spare;
    double hull1,hull2;LONG64 walk_passes,adopted,kept_seated;const char *foot_stage;
} test_pdf_saved;
/* Client 2, the window 10..200 (take 7), everything armed as the probe arms it; slot 2 seated at take-1 with its body in
 * its cockpit room, the walking update's call site and the death module's guard from take. */
static void test_pdf_begin(unsigned char *region,unsigned char *input,unsigned char *p1,unsigned char *p2,test_pdf_saved *v) {
    void *agent=test_pslot_agent2,*room=test_pslot_room;unsigned char *controller=test_pnat_controller;
    void **pvt=(void**)(region+VPS_PLAYER_VTABLE);pslot_slot *s=&pslots[2];
    v->fn=pfslot_fn;v->start=pilot_start;v->stop=pilot_stop;v->issued=stats.issued;v->capturing=pstk_capturing;v->capture_slot=pstk_capture_slot;
    memcpy(v->room_parent,test_pslot_room+0x70,8);memcpy(v->ship_parent,test_pslot_ship2+0x70,8);
    v->ship_is=test_pslot_ship_vt[PSLOT_IS_CLASS];v->ship_spare=test_pslot_ship_vt[PFBODY_KILL_SLOT];v->ship_kill=test_pslot_ship_vt[PDEATH_KILL_SLOT];
    v->player_spare=pvt[PFBODY_KILL_SLOT];
    v->hull1=*(double*)(test_pnat_ship+PDEATH_HULL);v->hull2=*(double*)(test_pslot_ship2+PDEATH_HULL);
    v->walk_passes=pfoot_walk_passes;v->adopted=pfoot_adopted;v->kept_seated=pfoot_kept_seated;v->foot_stage=pfoot_stage;   /* slot 1's foot test counts from here */
    test_pfbody_cameras_note();test_pfbody_removed=0;memset(p2+PFBODY_CAMERA,0,8);memset(p2+PFBODY_HOLD,0,4);p2[PFBODY_LIFECYCLE]=0;
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
    test_pfbody_hold_seen=0;
    assert(pfbody_pass(6,1) && pfbody_kill_installed && pfbody_mask==2 && *(void**)(p2+0x70)==room && !strcmp(pfbody_stage,"connected"));
    /* Connected held and with the stand-in camera: P_2 has no camera of its own and this client's cameras stayed (live df2:
     * every client's camera was P_2's from take-1, and slot 1's stand-up left its own camera's head controller dangling). */
    assert(test_pfbody_hold_seen==1 && !*(int32_t*)(p2+PFBODY_HOLD) && test_pfbody_cameras_kept() && !p2[PFBODY_LIFECYCLE]);
    assert(pfslot_pass(7) && pfslot_hook_installed && pdeath_pass(7,controller) && pdeath_guard_installed);
}
static void test_pdf_end(unsigned char *region,unsigned char *p2,test_pdf_saved *v) {
    void *agent=test_pslot_agent2;unsigned char *controller=test_pnat_controller;void **pvt=(void**)(region+VPS_PLAYER_VTABLE);
    if(pdeath_guard_installed) pdeath_restore();
    assert(!pdeath_guard_installed);
    pdeath_armed=0;pdeath_failed=0;pdeath_dead=0;pdeath_why="-";pdeath_suit=0;pdeath_find=NULL;pdeath_map=NULL;pdeath_player=NULL;memset(pdeath_slots,0,sizeof(pdeath_slots));
    pfslot_armed=0;pfslot_failed=0;pfslot_hook_installed=0;pfslot_stage="off";pfslot_why="-";pfslot_hook_calls=pfslot_late=pfslot_getups_claimed=pfslot_dying_skips=0;
    memset(pfslots,0,sizeof(pfslots));pfslot_fn=v->fn;
    pfbody_armed=0;pfbody_failed=0;pfbody_parked=0;pfbody_seated_since=0;pfbody_mask=0;pfbody_stage="off";pfbody_why="-";memset(pfbody_back,0,sizeof(pfbody_back));
    pfbody_connects=pfbody_disconnects=pfbody_checks=pfbody_parks=pfbody_unparks=pfbody_returns=0;pfbody_kill_off=pfbody_kill_spared=pfbody_kill_late=0;
    pfbody_connect=NULL;pfbody_filter=NULL;
    pfoot_armed=0;pfoot_failed=0;pfoot_why="-";pfoot_state=PFOOT_SEATED;pfoot_transition_tick=0;test_pslot_p1_stood=0;
    pfoot_walk_passes=v->walk_passes;pfoot_adopted=v->adopted;pfoot_kept_seated=v->kept_seated;pfoot_stage=v->foot_stage;
    test_pslot_ship_vt[PSLOT_IS_CLASS]=v->ship_is;test_pslot_ship_vt[PFBODY_KILL_SLOT]=v->ship_spare;test_pslot_ship_vt[PDEATH_KILL_SLOT]=v->ship_kill;
    pvt[PFBODY_KILL_SLOT]=v->player_spare;
    memcpy(test_pslot_room+0x70,v->room_parent,8);memcpy(test_pslot_ship2+0x70,v->ship_parent,8);
    memset(region+PFSLOT_CONVERSATION,0,8);memset(region+PFOOT_BANK,0,PFOOT_BANK_SIZE);
    memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));memset(p2+0x200,0,8);memset(p2+0x70,0,8);memset(&pstk_build,0,sizeof(pstk_build));
    memset(pilot_slots,0,sizeof(pilot_slots));
    pilot_slots[0].resolved=1;pilot_slots[0].object=test_pnat_ship;pilot_slots[0].player=1;
    pilot_slots[1].resolved=1;pilot_slots[1].object=test_pslot_ship2;pilot_slots[1].player=0;
    *(uint64_t*)(test_pslot_ship2+8)=0;*(uint64_t*)(test_pnat_ship+8)=0;*(double*)(test_pnat_ship+PDEATH_HULL)=v->hull1;*(double*)(test_pslot_ship2+PDEATH_HULL)=v->hull2;
    test_pslot_ship2[PDEATH_LIFECYCLE]=0;memcpy(test_pslot_ship2+0x200,&agent,8);memset(test_pslot_c2,0,sizeof(test_pslot_c2));
    pnat_controller=controller;pnat_ship=test_pnat_ship;pstk_flight=0;
    pilot_start=v->start;pilot_stop=v->stop;stats.issued=v->issued;pstk_capturing=v->capturing;pstk_capture_slot=v->capture_slot;
    pfbody_held=0;test_pfbody_cameras_back();test_pfbody_removed=0;memset(p2+PFBODY_HOLD,0,4);p2[PFBODY_LIFECYCLE]=0;
}

static void test_pilot_death_foot_block(unsigned char *region,unsigned char *input,unsigned char *p1,unsigned char *p2) {
    test_pdf_saved v;test_pdf_begin(region,input,p1,p2,&v);
    void *agent=test_pslot_agent2,*room=test_pslot_room;unsigned char *controller=test_pnat_controller;
    pslot_slot *s=&pslots[2];pfslot_slot *f=&pfslots[2];pdeath_slot *d1=&pdeath_slots[0],*d2=&pdeath_slots[1];
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
    /* 141..146: walking, slot 2 leaves through its ship's airlock: asked at the apply (out), MD's suit, and LAG ticks later
     * the walk ends, the seat's pointers go back and it sits in the suit, alive, its runtime ID the suit's. */
    {   const unsigned kills=test_pdeath_kills;void *const parent_was=*(void**)(test_pslot_ship2+0x70);
        assert(pvehk_may(2) && pvehk_ask(2,141) && !pvehk_ask(2,141) && d2->out==1 && d2->suit_tick==141+PDEATH_SUIT_LAG && test_pdf_events("out:2"));
        assert(pdeath_pass(142,controller) && d2->out==1 && f->walking && test_pdf_events(NULL));
        *(uint64_t*)(test_pslot_ship2+8)=0x2006;test_pdeath_ids[2]=0x2006;
        assert(pdeath_suit_ship(2,0x2006) && pdeath_pass(141+PDEATH_SUIT_LAG,controller) && d2->out==2 && d2->home==0x2005 && !f->walking && !s->walking &&
               s->seated && s->controller==test_pslot_c2 && s->runtime==0x2006 && !pilot_slots[1].dead && !pdeath_dead && !d2->lost && test_pdf_events("suited:2"));
        {char row[1024];assert(pdeath_status(row,sizeof(row)) && strstr(row,"/out2") && strstr(row," airlock=1/0"));}
        /* 147: flying its suit, not at home: nothing; its GET_UP and its dock keys ask for the way home and never reach
         * X4 (no range yet, then too far: far). 148: the suit under its home ship: out of the suit (killed as X4 kills its
         * player's old suit), seated in the ship it left. */
        *(void**)(test_pslot_ship2+0x70)=NULL;
        assert(pdeath_pass(147,controller) && d2->out==2 && test_pdf_events(NULL));
        assert(pvehk_suited(2) && pvehk_home(2)==0x2005 && !pvehk_home(1) && !pvehk_home_key(2,0x7b) && !pvehk_home_key(1,PFOOT_GET_UP));
        {   LONG64 getups=f->getups;test_pdf_frame(147,1,PFOOT_GET_UP);
            assert(pslot_replay(147) && f->getups==getups && d2->ask==1 && pdeath_pass(147,controller) && !d2->ask && d2->out==2 && test_pdf_events("far:2"));
            assert(pvehk_range(2,2500.0) && pvehk_home_key(2,0xaf) && pdeath_pass(147,controller) && d2->out==2 && test_pdf_events("far:2"));
            assert(pvehk_range(2,-1.0) && pvehk_home_key(2,0xd3) && pdeath_pass(147,controller) && d2->out==2 && test_pdf_events("far:2"));
            memset(&pstk_build,0,sizeof(pstk_build));}
        *(void**)(test_pslot_ship2+0x70)=test_pslot_ship2;*(uint64_t*)(test_pslot_ship2+8)=0x2005;
        /* Its body sat in the suit's cockpit room (a suit has one; connected once everyone sits): off the room before the
         * suit is killed, or X4's kill of the suit walks into P_2 (live 12 Nov: both clients never left that tick). */
        {   const LONG64 late=pfbody_kill_late,offs=pfbody_disconnects;const unsigned removed=test_pfbody_removed;
            pfbody_mask|=2u;test_pfbody_calls=0;
            assert(pdeath_pass(148,controller) && !d2->out && !d2->home && s->seated && s->runtime==0x2005 && test_pdeath_kills==kills+1 &&
                   !pilot_slots[1].dead && test_pdf_events("home:2"));
            assert(!(pfbody_mask&2u) && test_pfbody_calls==1 && !test_pfbody_target && pfbody_kill_late==late+1 && pfbody_disconnects==offs+1 &&
                   test_pfbody_removed==removed && !test_pslot_p2[PFBODY_LIFECYCLE]);
            InterlockedExchange64(&pfbody_kill_late,late);InterlockedExchange64(&pfbody_disconnects,offs);}
        {char row[1024];assert(pdeath_status(row,sizeof(row)) && !strstr(row,"/out") && strstr(row," airlock=1/1/3") && !pvehk_suited(2) && !pvehk_range(2,10.0));}
        *(void**)(test_pslot_ship2+0x70)=parent_was;test_pdeath_ids[2]=0;memset(&pstk_build,0,sizeof(pstk_build));
        /* again, and home by the key within range (the suit not docked anywhere) */
        test_pdf_frame(149,1,PFOOT_GET_UP);assert(pslot_replay(149) && f->walking);
        /* walking, its dock key is the way out (the airlock without its console); GET_UP is not; a second key is consumed */
        assert(!pvehk_home_key(2,PFOOT_GET_UP) && !pvehk_home_key(2,0x7b) && !d2->out);
        test_pdf_frame(149,1,0xd3);assert(pslot_replay(149) && d2->out==1 && d2->suit_tick==149+PDEATH_SUIT_LAG && f->walking && test_pdf_events("out:2"));
        assert(pvehk_home_key(2,0xaf) && d2->out==1 && test_pdf_events(NULL));
        *(uint64_t*)(test_pslot_ship2+8)=0x2007;test_pdeath_ids[2]=0x2007;
        /* this suit gets the weapons slot 1's player keeps for a suit (P_2 keeps none): two primary groups (one macro finds
         * no place), no secondary; the active primary group 1, no active secondary */
        {   static void *g0[2]={(void*)0x1111,(void*)0x3333},*g1[1]={(void*)0x2222};static void **table[2][3];
            unsigned char saved[0x38],macro_was[8];void *t=table,*te=table+2,*one=(void*)1;const int active=1,none=-1;const unsigned two=2,single=1;
            const pvehk_functions fn_was=pvehk_fn;const uint64_t weapons=pvehk_weapons;
            memcpy(saved,p1+PVEHK_WEAPONS_PRIMARY,sizeof(saved));memcpy(macro_was,test_pslot_ship2+0x60,8);
            table[0][0]=g0;table[0][1]=g0+2;table[1][0]=g1;table[1][1]=g1+1;
            memset(p1+PVEHK_WEAPONS_PRIMARY,0,sizeof(saved));memcpy(p1+PVEHK_WEAPONS_PRIMARY,&t,8);memcpy(p1+PVEHK_WEAPONS_PRIMARY+8,&te,8);
            memcpy(p1+PVEHK_ACTIVE_PRIMARY,&active,4);memcpy(p1+PVEHK_ACTIVE_SECONDARY,&none,4);memcpy(test_pslot_ship2+0x60,&one,8);
            memcpy(test_pdf_macro_block+0x10b8,&two,4);memcpy(test_pdf_macro_block+0x10bc,&single,4);
            pvehk_fn=(pvehk_functions){test_pdf_install,test_pdf_group,test_pdf_activate,test_pdf_macro_data,test_pdf_description_end};
            test_pdf_installs=test_pdf_ended=0;memset(test_pdf_grouped,0,sizeof(test_pdf_grouped));memset(test_pdf_activated,0,sizeof(test_pdf_activated));
            assert(pdeath_suit_ship(2,0x2007) && pdeath_pass(149+PDEATH_SUIT_LAG,controller) && d2->out==2 && d2->home==0x2005 && test_pdf_events("suited:2"));
            /* a suit has no room: P_2 gets its place for MD from the ship at the seat; the seat's end takes it off */
            {   const LONG64 marks=pfbody_marks;const pfbody_mark_fn mark_was=pfbody_mark;void (*const unplace_was)(unsigned char *)=pslot_unplace;
                const int roomless=s->roomless;const unsigned char in_was=test_pslot_ship2[PFBODY_PLACED],p_was=p2[PFBODY_PLACED];
                pfbody_mark=test_pfbody_mark;pslot_unplace=pfbody_unplace;p2[PFBODY_PLACED]=0;
                s->roomless=0;test_pslot_ship2[PFBODY_PLACED]=1;pfbody_place_seat(2);assert(!p2[PFBODY_PLACED] && pfbody_marks==marks);
                s->roomless=1;test_pslot_ship2[PFBODY_PLACED]=0;pfbody_place_seat(2);assert(!p2[PFBODY_PLACED] && pfbody_marks==marks);
                test_pslot_ship2[PFBODY_PLACED]=1;pfbody_place_seat(2);pfbody_place_seat(1);assert(p2[PFBODY_PLACED]==1 && pfbody_marks==marks+1);
                pfbody_unplace(p2);assert(!p2[PFBODY_PLACED]);
                s->roomless=roomless;test_pslot_ship2[PFBODY_PLACED]=in_was;p2[PFBODY_PLACED]=p_was;pfbody_mark=mark_was;pslot_unplace=unplace_was;
                InterlockedExchange64(&pfbody_marks,marks);InterlockedDecrement64(&pfbody_unmarks);
            }
            assert(test_pdf_installs==3 && test_pdf_ended==3 && test_pdf_grouped[1]==18 && !test_pdf_grouped[0] && test_pdf_activated[1]==2 &&
                   !test_pdf_activated[0] && pvehk_weapons==weapons+2 && !pvehk_equip_faults);
            pvehk_fn=fn_was;memcpy(p1+PVEHK_WEAPONS_PRIMARY,saved,sizeof(saved));memcpy(test_pslot_ship2+0x60,macro_was,8);
        }
        *(void**)(test_pslot_ship2+0x70)=NULL;*(uint64_t*)(test_pslot_ship2+8)=0x2005;
        assert(pvehk_range(2,120.0) && pvehk_home_key(2,PFOOT_GET_UP) && pdeath_pass(149+PDEATH_SUIT_LAG+1,controller) && !d2->out && s->seated &&
               s->runtime==0x2005 && test_pdeath_kills==kills+2 && test_pdf_events("home:2"));
        *(void**)(test_pslot_ship2+0x70)=parent_was;test_pdeath_ids[2]=0;memset(&pstk_build,0,sizeof(pstk_build));
        /* an asked exit whose suit never came: nothing happens */
        test_pdf_frame(159,1,PFOOT_GET_UP);assert(pslot_replay(159) && f->walking);
        assert(pvehk_ask(2,160) && test_pdf_events("out:2") && pdeath_pass(160+PDEATH_SUIT_LAG,controller) && !d2->out && f->walking && test_pdf_events("noout:2"));
    }
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
    test_pdf_end(region,p2,&v);
    printf("pilot death and foot: both arm together; kill guard (body off at the seat's kill, spared where no scope opens); slot 2 dies seated, "
           "suit without body, slot 1 up meanwhile, respawn, body back, gets up; empty docked ship dies while walking (lost, no death, "
           "stranded, respawn, up again); slot 1 walking keeps walking without its ship; killed on foot 163; leaving and ejected: GET_UP void PASS\n");
}
/* stop+1 with death, suit, respawn, foot, foot slots and bodies all armed (the session defaults), through the whole pass
 * (pnat_pass: the bodies off, the walking call site back, the seats released), X4's event phase after it and the next
 * pass. Live d1s (build171): nobody died, nobody got up, and both clients stopped in this teardown with 163 player_killed:
 * the body leaving its room was a Player with no parent and no hold, which X4's parent change kills 'removed' (3d7010),
 * and its KilledEvent reached the game over (the guard). Then the same with slot 1 and slot 2 walking at the stop, slot 1
 * getting up first with slot 2's body in its room (live df2's order: the park takes the body off, held). */
static int test_pdf_pass(uint64_t tick,unsigned char *controller) {
    stats.issued=tick;
    return pnat_pass(tick,controller,test_pnat_refresh,(uint32_t*)(pnat_base+PNAT_COUNTER),pnat_base+PNAT_CONTEXTS,pnat_base+PNAT_FLIGHT,pnat_authority_stock);
}
/* After stop+1: X4's event phase, then stop+2. No KillEvent for P_2, no game over, no failure; nothing left in the tree. */
static void test_pdf_stopped_clean(unsigned char *p2,unsigned char *controller) {
    assert(!test_pfbody_removed && !p2[PFBODY_LIFECYCLE] && !*(int32_t*)(p2+PFBODY_HOLD) && test_pfbody_cameras_kept());
    test_pfbody_x4_events();
    assert(test_pdf_pass(pilot_stop+2,controller) && !pnat_failed && !pnat_fail_stage && !pdeath_failed && !pfoot_failed && !pdeath_game_overs &&
           !strcmp(pdeath_why,"-") && !strcmp(pfbody_why,"-"));
    assert(!pfbody_mask && !pfbody_kill_installed && !pfslot_hook_installed && !pslot_hook_installed && !pslots[2].seated && !*(void**)(p2+0x70));
}
static void test_pilot_death_foot_stop(unsigned char *region,unsigned char *input,unsigned char *p1,unsigned char *p2) {
    unsigned char *controller=test_pnat_controller;void *room=test_pslot_room;pslot_slot *s=&pslots[2];pfslot_slot *f=&pfslots[2];
    test_pdf_saved v;
    /* A: everyone seated at the stop, slot 2's body in its cockpit room since take-1. */
    test_pdf_begin(region,input,p1,p2,&v);
    pnat_failed=0;pnat_fail_stage=NULL;pilot_failures=0;pilot_controls_owned=0;pdeath_game_overs=0;
    for(uint64_t t=8;t<=200;t+=48) assert(pfbody_guard(t) && pfbody_pass(t,1) && pdeath_pass(t,controller) && pfslot_pass(t) && pfbody_mask==2);
    test_pfbody_calls=0;test_pfbody_hold_seen=0;
    assert(test_pdf_pass(201,controller) && !pnat_failed);
    assert(!strcmp(pfbody_stage,"released") && !pfbody_mask && test_pfbody_calls==1 && !test_pfbody_target && test_pfbody_hold_seen==1 &&
           !strcmp(pfslot_stage,"released") && !strcmp(pslot_stage,"released") && !s->seated && pdeath_guard_installed);
    test_pdf_stopped_clean(p2,controller);
    /* What d1s ran into, X4's own rule: the same body taken off its room unheld is killed 'removed', X4's event phase
     * reaches the game over, the guard turns it into stop 163 player_killed. A real kill of a slot's player still stops so. */
    memcpy(p2+0x70,&room,8);test_pfbody_connect(p2,test_pfbody_filter_object,NULL,NULL);
    assert(test_pfbody_removed==1 && p2[PFBODY_LIFECYCLE]==1);
    test_pfbody_x4_events();
    assert(pdeath_game_overs==1 && pdeath_failed && !strcmp(pdeath_why,"player_killed") && pnat_failed && pilot_stop_reason()==PDEATH_STOP_REASON);
    pnat_failed=0;pnat_fail_stage=NULL;pilot_failures=0;pilot_controls_owned=0;
    test_pdf_end(region,p2,&v);
    /* B: slot 1 gets up while slot 2's body sits in its room (df2's order), then slot 2 gets up; both walk at the stop. */
    test_pdf_begin(region,input,p1,p2,&v);
    pnat_failed=0;pnat_fail_stage=NULL;pilot_failures=0;pilot_controls_owned=0;pdeath_game_overs=0;
    /* 31: slot 1's GET_UP replayed: every body off before X4 stands anyone up, held, no camera moved. */
    pfoot_state=PFOOT_LEAVING;pfoot_leave_tick=31;test_pfbody_calls=0;test_pfbody_hold_seen=0;
    assert(pfbody_guard(31) && !pfbody_mask && pfbody_parked && test_pfbody_calls==1 && test_pfbody_hold_seen==1 && *(void**)(p2+0x70)==room);
    assert(!test_pfbody_removed && !p2[PFBODY_LIFECYCLE] && test_pfbody_cameras_kept());
    test_pfbody_x4_events();assert(!pdeath_game_overs && !pnat_failed);
    /* 32: X4 stood slot 1 up (no PlayerController): walking. */
    test_pslot_p1_stood=1;
    assert(pdeath_pass(32,NULL) && pfoot_check(32,NULL,&pnat_controller,pnat_ship,0,0)==1 && pfoot_state==PFOOT_WALKING && !pnat_controller);
    assert(pfbody_guard(32) && !pfbody_mask);
    /* 40: slot 2 gets up and walks (no body to park). */
    test_pdf_frame(40,1,PFOOT_GET_UP);
    assert(pfbody_guard(40) && !pfbody_mask && pslot_replay(40) && f->walking && s->walking && s->seated && *(void**)(p2+0x70)==room);
    memset(&pstk_build,0,sizeof(pstk_build));
    /* stop+1 with both walking: no body to take off, the walking call site back, slot 2's seat pointers back. */
    test_pfbody_calls=0;
    assert(test_pdf_pass(201,NULL) && !pnat_failed && !test_pfbody_calls && !strcmp(pfbody_stage,"released") && !strcmp(pfslot_stage,"released") &&
           !strcmp(pslot_stage,"released") && !f->walking && !s->walking && !s->seated);
    test_pdf_stopped_clean(p2,NULL);
    pfoot_state=PFOOT_SEATED;test_pslot_p1_stood=0;pnat_controller=controller;
    test_pdf_end(region,p2,&v);
    printf("pilot death and foot at the stop: everything armed, stop+1 through the whole pass takes the body off held (no 'removed' kill, "
           "no game over, no camera of its own), the next pass clean; unheld it is d1s's 163 player_killed; slot 1 up with slot 2's body "
           "in its room (parked held), both walking at the stop PASS\n");
}

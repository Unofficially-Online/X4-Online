/* Slots 2..N on foot (lockstep_pilot_foot_slots.h) and the bodies' park around a get-up (lockstep_pilot_foot_body.h), on
 * the slot fixtures of test_pilot_slots (client 2, slot 2's seat in its scope): arming and every refusal, slot 2's
 * verdicts and Get Up claim, the stand-up consumed in slot 2's scope (the conversation manager and 716d80 asked, the
 * seat's controller released, the FPC made, its first update with this client's FIRST_PERSON flags held, X4's
 * placement target, the control position none, the seat flag, the bank start), slot 2's own walking bank swapped by the
 * scope, its walking key into that bank only, the update after X4's at 9fe604 in the scope, the walking checks, the
 * receipt, K1's own walking, the eye's walk mode, the sit-down, stop+1, the fail-closed paths; and the crash fix: every
 * body off the tree in the pass that replays slot 1's or slot 2's get-up, back after everyone sat for PFBODY_SETTLE. */
static __attribute__((aligned(16))) unsigned char test_pfslot_fpc[0x3c0],test_pfslot_fpc1[0x3c0],test_pfslot_dock[0x100],test_pfslot_target[0x100],
    test_pfslot_conversation[0x200],test_pfslot_deleted[0x400];
static void *test_pfslot_fpc_vt[32],*test_pfslot_dock_vt[600],*test_pfslot_target_vt[600];
static unsigned char test_pfslot_may=1;
static unsigned test_pfslot_requests,test_pfslot_places,test_pfslot_poses,test_pfslot_updates,test_pfslot_ship_found;
static unsigned char *test_pfslot_update_singleton[4];
static unsigned char test_pfslot_update_key[4];
static unsigned char test_pfslot_fpc_is(void *self,int cls) {(void)self;return cls==PFSLOT_FPC_CLASS;}
static void *test_pfslot_fpc_next(void *self) {(void)self;return NULL;}
static unsigned char test_pfslot_ship_is(void *self,int cls) {assert(self==test_pslot_ship2);return cls==PFSLOT_SHIP_CLASS;}
static unsigned char test_pfslot_dock_is(void *self,int cls) {assert(self==test_pfslot_dock);return cls==PFSLOT_DOCK_CLASS;}
static unsigned char test_pfslot_target_is(void *self,int cls) {assert(self==test_pfslot_target);return cls==PFSLOT_TARGET_CLASS;}
static unsigned char test_pfslot_may_stand(void *player) {assert(player==test_pslot_p2 && test_pslot_singleton()==test_pslot_p2 && pstk_scope_slot==2);return test_pfslot_may;}
/* 70eee0 as X4's: an FPC for the singleton unless its chain has one (Positional::vfunc829 replaces the controller). */
static void test_pfslot_make_fpc(void *player) {
    assert(player==test_pslot_p2 && test_pslot_singleton()==test_pslot_p2 && pstk_scope_slot==2);
    void *now=NULL;memcpy(&now,test_pslot_p2+0x200,8);
    if(now) return;
    void *vt=test_pfslot_fpc_vt,*fpc=test_pfslot_fpc;memset(test_pfslot_fpc,0,sizeof(test_pfslot_fpc));memcpy(test_pfslot_fpc,&vt,8);
    memcpy(test_pslot_p2+0x200,&fpc,8);
}
/* d81ec0: the first update (d7cdb0) once, then the pose from the FPC's own fields. d7cdb0 resets the live bank, asks
 * d81d20(1) (a request unless 0x38f4e7c says requested and no share) and sets the seat flag from the control position. */
static void test_pfslot_pose(void *self,float *out,double time,unsigned char overwrite) {
    unsigned char *fpc=self;assert(time==100.0 && overwrite==1);++test_pfslot_poses;
    if(!fpc[PFSLOT_FPC_READY]) {
        fpc[PFSLOT_FPC_READY]=1;pnat_base[0x38f4e8b]=0;
        if(!(pnat_base[PFOOT_FP_REQUESTED] && !*(uint32_t*)(pnat_base+PFOOT_COUNTER))) {++test_pfslot_requests;pnat_base[PFOOT_FP_REQUESTED]=1;}
        fpc[PFSLOT_FPC_SEAT]=*(int*)(test_pslot_singleton()+PFSLOT_POSITION)!=*(int*)(pnat_base+PFSLOT_POSITION_NONE);
    }
    memset(out,0,64);memcpy(out,fpc+0x100,12);out[4]=out[9]=out[14]=1.0f;
}
/* 717c30: the FPC posed at the target's connection (a pose query first, as X4's runs d81ec0 through the vtable). */
static void test_pfslot_place(void *player,void *target,unsigned char flag) {
    assert(player==test_pslot_p2 && target==test_pfslot_target && flag==1 && test_pslot_singleton()==test_pslot_p2);
    float pose[16];test_pfslot_pose(test_pfslot_fpc,pose,100.0,1);
    float x=5.0f;memcpy(test_pfslot_fpc+0x100,&x,4);++test_pfslot_places;
}
static int test_pfslot_conn_type(int position) {assert(position==0x77);return 3;}
static unsigned char test_pfslot_ship_conn(void *ship,void *out,int type) {
    assert(ship==test_pslot_ship2 && type==3);
    if(!test_pfslot_ship_found) return 0;
    void *pair[2]={test_pfslot_target,(void*)1};memcpy(out,pair,16);return 1;
}
static unsigned char test_pfslot_dock_conn(void *dock,void *out,int type) {
    assert(dock==test_pfslot_dock && type==3);void *pair[2]={test_pfslot_target,(void*)1};memcpy(out,pair,16);return 1;
}
/* d7f8d0: moves the FPC by the forward key of the live bank (0x38f4e8b), as its singleton sees it. */
static void test_pfslot_update_fn(void *fpc) {
    if(test_pfslot_updates<4) {test_pfslot_update_singleton[test_pfslot_updates]=test_pslot_singleton();test_pfslot_update_key[test_pfslot_updates]=pnat_base[0x38f4e8b];}
    ++test_pfslot_updates;
    float x;memcpy(&x,(unsigned char*)fpc+0x100,4);x+=pnat_base[0x38f4e8b];memcpy((unsigned char*)fpc+0x100,&x,4);
}
static void test_pfslot_forward(int id,unsigned char active) {assert(id==0x1a);pnat_base[0x38f4e8b]=active;}   /* d7d3b0 */
static void test_pilot_foot_slots_block(unsigned char *region,unsigned char *input,unsigned char *p1,unsigned char *p2) {
    void *agent=test_pslot_agent2,*room=test_pslot_room,*null=NULL;
    pfslot_fns saved_fn=pfslot_fn;uint64_t start=pilot_start,stop=pilot_stop,saved_issued=stats.issued;
    int capturing=pstk_capturing;unsigned capture_slot=pstk_capture_slot;
    unsigned char room_parent[8],ship_parent[8];memcpy(room_parent,test_pslot_room+0x70,8);memcpy(ship_parent,test_pslot_ship2+0x70,8);
    void *ship_is=test_pslot_ship_vt[PSLOT_IS_CLASS];
    test_pslot_reset();assert(pslot_arm_using(3));pslots[2].player=p2;pslot_p1=p1;memset(p2+0x70,0,8);memset(p2+0x200,0,8);
    memcpy(test_pslot_ship2+0x200,&agent,8);memcpy(test_pslot_ship2+PSLOT_ROOM,&room,8);test_pslot_parent=test_pslot_room;stats.issued=6;
    pilot_start=10;pilot_stop=40;pilot_slots[1].dead=0;pilot_slots[0].dead=0;
    assert(pslot_seat(2) && pslots[2].seated && pslots[2].controller==test_pslot_c2);
    if(!pslot_hook_installed) assert(pslot_hook_install());
    pslot_slot *s=&pslots[2];pfslot_slot *f=&pfslots[2];
    /* Fixtures: the FPC class, the docked ancestry (room -> ship -> dock), the placement target, the conversation idle,
     * the control position none, the FPC update's call site. */
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
    /* Arming: after slot 1's foot and the stock slots, X4's functions there (with the death module too: its block below). */
    pfslot_fn=(pfslot_fns){test_pfslot_may_stand,test_pfslot_make_fpc,test_pfslot_update_fn,test_pfslot_pose,test_pfslot_place,test_pfslot_conn_type,
        test_pfslot_ship_conn,test_pfslot_dock_conn};
    pfoot_armed=0;assert(!pfslot_arm_using() && !strcmp(pstk_arm_why,"foot_slots_without_foot"));pfoot_armed=1;
    pfslot_fn.place=NULL;assert(!pfslot_arm_using() && !strcmp(pstk_arm_why,"foot_slots_functions"));pfslot_fn.place=test_pfslot_place;
    {unsigned mask=pslot_mask;pslot_mask=0;assert(!pfslot_arm_using() && !strcmp(pstk_arm_why,"foot_slots_without_slots"));pslot_mask=mask;}
    /* Without the module slot 2's walking keys and GET_UP are refused; with it they are slot 2's replay ids. */
    assert(pstk_verdict_slot(PSTK_STATE,0x1a,2)==PSTK_REFUSE && pstk_verdict_slot(PSTK_ACTION,0x115,2)==PSTK_REFUSE && !pfslot_claim_ok(2));
    assert(pfslot_arm_using() && pfslot_armed && !strcmp(pfslot_stage,"armed") && pfslot_arm_using());
    assert(pstk_verdict_slot(PSTK_STATE,0x1a,2)==PSTK_REPLAY && pstk_verdict_slot(PSTK_ACTION,0x115,2)==PSTK_REPLAY &&
           pstk_verdict_slot(PSTK_RANGE,0xf,2)==PSTK_REPLAY && pstk_verdict_slot(PSTK_RANGE,7,2)==PSTK_REPLAY && pstk_verdict_slot(PSTK_STATE,0x78,2)==PSTK_REFUSE);
    assert(pfslot_claim_ok(2) && !pfslot_claim_ok(1) && !pfslot_claim_ok(3));
    /* The scope swaps slot 2's walking bank; this client's FIRST_PERSON flags (0x38f4e7c, 0x38f4e80) stay its own. */
    region[0x38f4e8b]=0x21;region[PFOOT_FP_REQUESTED]=0;*(uint32_t*)(region+PFOOT_COUNTER)=0;f->bank[0x4b]=0x42;f->bank[0x3c]=0x99;
    assert(pslot_enter(2) && region[0x38f4e8b]==0x42 && !region[PFOOT_FP_REQUESTED] && f->bank[0x4b]==0x21);
    region[0x38f4e8b]=0x43;assert(pslot_leave(2) && region[0x38f4e8b]==0x21 && f->bank[0x4b]==0x43 && f->bank[0x3c]==0x99);
    /* A canonical bank byte changed inside the scope (X4 writing slot 1's bank there) fails the leave. */
    assert(pslot_enter(2));f->bank[0x4b]^=1;assert(!pslot_leave(2));f->bank[0x4b]^=1;region[0x38f4e8b]=0;
    memset(f->bank,0,sizeof(f->bank));
    /* The pass from take installs the call at 9fe604 on the slots' near page; nobody walks yet. */
    assert(pfslot_pass(6) && !pfslot_hook_installed && pfslot_pass(7) && pfslot_hook_installed && !strcmp(pfslot_stage,"live"));
    {int32_t at;memcpy(&at,region+PFSLOT_CALL_SITE+1,4);assert(region[PFSLOT_CALL_SITE]==0xe8 && region+PFSLOT_CALL_SITE+5+at==pslot_page+PFSLOT_STUB);}
    pnat_input=input;pstk_capturing=1;pstk_capture_slot=2;
    /* Live footlook4 (the death module off, as foot_slots and bodies require): slot 1's GET_UP applied, X4 deleted its
     * PlayerController (716e20 -> 70e0f0) and 9fc550 answers none, but the pass has not seen it yet: the latched pointer
     * is the deleted one (its memory reused: vtable NULL). Slot 2's replay and X4's update of slot 2 enter its scope: the
     * deleted controller is never called (before the fix: pslot_enter -> pslot_global_part -> [NULL+0x188], 0xC0000005 at
     * tick 2406), the canonical predicate is 0. The first pass that sees slot 1 walking forgets the pointer. */
    {   unsigned char *latched=pnat_controller;int foot_state=pfoot_state;uint64_t transition=pfoot_transition_tick;
        LONG64 walk_passes=pfoot_walk_passes;const char *foot_stage=pfoot_stage;
        uint64_t tick=11;stats.issued=tick;memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));pstk_slot_frames[1].tick=tick;
        uint64_t autopilot=*(uint64_t*)(region+PNAT_AUTOPILOT);unsigned char flight=region[PNAT_FLIGHT];
        *(uint64_t*)(region+PNAT_AUTOPILOT)=0;region[PNAT_FLIGHT]=0;   /* as live: no autopilot, flight control off: the getter is reached */
        memset(test_pfslot_deleted,0,sizeof(test_pfslot_deleted));pnat_controller=test_pfslot_deleted;test_pslot_p1_stood=1;assert(!pdeath_armed);
        LONG64 gone=pslot_canonical_gone,replays=s->replays;pslot_g1=1;
        assert(pslot_replay(tick) && s->replays==replays+1 && !pslot_g1 && pslot_canonical_gone==gone+1 && test_pslot_singleton()==p1 && !pstk_scope_slot);
        test_pslot_call_count=0;pslot_update_tick=tick;pslot_update_slots(100.0);
        assert(test_pslot_count('U')==1 && pslot_canonical_gone==gone+2 && !pslot_g1 && test_pslot_singleton()==p1);
        {char row[1024];assert(pslot_status(row,sizeof(row)) && strstr(row," canonical_gone="));}
        /* The pass sees no PlayerController after slot 1's GET_UP: walking, and the deleted controller is gone for good. */
        pfoot_state=PFOOT_LEAVING;pfoot_leave_tick=tick-1;
        assert(pfoot_check(tick+1,NULL,&pnat_controller,pnat_ship,0,0)==1 && pfoot_state==PFOOT_WALKING && !pnat_controller);
        assert(pslot_enter(2) && pslot_canonical_gone==gone+2 && !pslot_g1 && pslot_leave(2));
        /* X4's live controller again (it sat down): read as before, nothing counted. */
        test_pslot_p1_stood=0;pnat_controller=test_pnat_controller;
        assert(pslot_enter(2) && pslot_canonical_gone==gone+2 && pslot_leave(2));
        pnat_controller=latched;pfoot_state=foot_state;pfoot_transition_tick=transition;pfoot_walk_passes=walk_passes;pfoot_stage=foot_stage;
        *(uint64_t*)(region+PNAT_AUTOPILOT)=autopilot;region[PNAT_FLIGHT]=flight;memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));}
    /* GET_UP refused by the conversation manager, then by 716d80: kept seated, nothing changed. */
    {   uint64_t tick=12;stats.issued=tick;memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));
        pstk_slot_frames[1].tick=tick;pstk_slot_frames[1].r.count=1;pstk_slot_frames[1].r.events[0]=(pstk_event){PSTK_ACTION,PFOOT_GET_UP,0};
        *(int*)(test_pfslot_conversation+0x1a0)=0;assert(pslot_replay(tick) && f->kept==1 && !f->walking && s->controller==test_pslot_c2);
        *(int*)(test_pfslot_conversation+0x1a0)=1;test_pfslot_may=0;assert(pslot_replay(tick) && f->kept==2 && !f->walking && s->controller==test_pslot_c2);
        test_pfslot_may=1;}
    /* The bodies' guard sees slot 2's GET_UP of this tick (lockstep_pilot_foot_body.h parks before the replay). */
    assert(pfslot_getup_due(12) && !pfslot_getup_due(13));
    /* 13: slot 2 gets up in its scope: the seat's controller released (stock vtable back, X4's release), the FPC made, its first
     * update and X4's placement with this client's FIRST_PERSON flags held (no context request), the control position none,
     * the seat flag 0, P_2's first person flag, slot 2's bank started; the canonical bank and flags as they were. Slot 1
     * stood up in the same tick with the death module off (its deleted controller still latched): never read. */
    {   uint64_t tick=13;stats.issued=tick;test_pslot_call_count=0;test_pfslot_requests=0;test_pfslot_places=0;test_pfslot_ship_found=0;
        pstk_slot_frames[1].tick=tick;region[0x38f4e70]=0x5c;region[PFOOT_ACTIVE]=0;
        unsigned char *latched=pnat_controller;LONG64 gone=pslot_canonical_gone;
        memset(test_pfslot_deleted,0,sizeof(test_pfslot_deleted));pnat_controller=test_pfslot_deleted;test_pslot_p1_stood=1;
        assert(pslot_replay(tick) && f->walking && s->walking && s->seated && !s->controller && f->fpc==test_pfslot_fpc && f->stood==1 && f->placed==1);
        assert(pslot_canonical_gone==gone+1 && !pslot_g1);pnat_controller=latched;test_pslot_p1_stood=0;
        assert(test_pslot_count('X')==1 && *(void**)test_pslot_c2==(void*)(region+PSLOT_VTABLE) && *(void**)(test_pslot_ship2+0x200)==agent);
        assert(!test_pfslot_requests && test_pfslot_places==1 && test_pfslot_fpc[PFSLOT_FPC_READY]==1 && !test_pfslot_fpc[PFSLOT_FPC_SEAT]);
        assert(*(int*)(p2+PFSLOT_POSITION)==0x55 && p2[PFOOT_PLAYER_FLAG]==1 && !region[PFOOT_FP_REQUESTED] && !*(uint32_t*)(region+PFOOT_COUNTER));
        {double e70;memcpy(&e70,f->bank+0x30,8);assert(e70==-1.0 && f->bank[0x3d]==1 && !f->bank[0x4b]);}
        assert(region[0x38f4e70]==0x5c && !region[PFOOT_ACTIVE] && test_pslot_singleton()==p1 && !pstk_scope_slot && *(void**)(p2+0x70)==room);
        float x;memcpy(&x,test_pfslot_fpc+0x100,4);assert(x==5.0f);}
    /* The dock's connection was the target (the ship had none): X4's order, 455f90 first. A second GET_UP is the sit-down. */
    assert(pfslot_own_walking() && pfoot_own_walking() && !pfoot_own_seated() && !pfslot_claim_ok(2) && pfslot_any_walking());
    /* K1 on the walking slot's own client: X4's first-person count holds one for the canonical player's seat, which is not
     * this client's; anything more (a menu, a conversation) still withdraws; a standing canonical player's count is others'. */
    {   void (*refresh)(char)=pfoot_fp_refresh;const int armed=pfoot_armed;const unsigned char contexts=region[0x2f4b829u];
        int none,seat,was;memcpy(&none,region+PFSLOT_POSITION_NONE,4);memcpy(&was,p1+PFSLOT_POSITION,4);seat=none+1;
        const LONG64 unseated=pfoot_k1_unseated,k1_fixed=pfoot_k1_fixed,k1_ignored=pfoot_k1_ignored;
        pfoot_fp_refresh=test_pfoot_fp_refresh;pfoot_armed=1;memcpy(p1+PFSLOT_POSITION,&seat,4);assert(pfslot_canon_seated());
        *(uint32_t*)(region+PFOOT_COUNTER)=1;region[PFOOT_FP_REQUESTED]=0;
        pfoot_k1_reconcile();assert(region[PFOOT_FP_REQUESTED]==1 && *(uint32_t*)(region+PFOOT_COUNTER)==1 && pfoot_k1_unseated==unseated+1);
        *(uint32_t*)(region+PFOOT_COUNTER)=2;pfoot_k1_reconcile();assert(!region[PFOOT_FP_REQUESTED] && *(uint32_t*)(region+PFOOT_COUNTER)==2);
        memcpy(p1+PFSLOT_POSITION,&none,4);assert(!pfslot_canon_seated());
        *(uint32_t*)(region+PFOOT_COUNTER)=1;pfoot_k1_reconcile();assert(!region[PFOOT_FP_REQUESTED] && pfoot_k1_unseated==unseated+2);
        *(uint32_t*)(region+PFOOT_COUNTER)=0;pfoot_k1_reconcile();assert(region[PFOOT_FP_REQUESTED]==1 && pfoot_k1_unseated==unseated+2);
        {char out[4096];assert(pfoot_status(out,sizeof(out))>0 && strstr(out," k1_fp=1/0/"));}
        region[PFOOT_FP_REQUESTED]=0;region[0x2f4b829u]=contexts;memcpy(p1+PFSLOT_POSITION,&was,4);pfoot_armed=armed;pfoot_fp_refresh=refresh;
        InterlockedExchange64(&pfoot_k1_fixed,k1_fixed);InterlockedExchange64(&pfoot_k1_ignored,k1_ignored);   /* the foot test's own counts */
    }
    /* Slot 2's walking key into slot 2's bank only (d7d3b0 in its scope); X4's update at 9fe604 for the singleton's FPC,
     * then slot 2's in its scope (singleton P_2, its bank). */
    {   pnat_node *states=(pnat_node*)(input+0x63e0+0x40);test_pnat_insert((unsigned char*)states,0x1a,(void*)test_pfslot_forward);
        uint64_t tick=14;stats.issued=tick;pstk_slot_frames[1].tick=tick;pstk_slot_frames[1].r.events[0]=(pstk_event){PSTK_STATE,0x1a,1};
        assert(pslot_replay(tick) && f->bank[0x4b]==1 && !region[0x38f4e8b] && f->walking);
        test_pfslot_updates=0;LONG64 calls=pfslot_hook_calls;
        void (*hooked)(void*)=(void(*)(void*))(region+PFSLOT_CALL_SITE+5+*(int32_t*)(region+PFSLOT_CALL_SITE+1));
        hooked(test_pfslot_fpc1);
        assert(test_pfslot_updates==2 && test_pfslot_update_singleton[0]==p1 && !test_pfslot_update_key[0] && test_pfslot_update_singleton[1]==p2 &&
               test_pfslot_update_key[1]==1 && pfslot_hook_calls==calls+1 && pfslot_update_tick==14 && f->updates==1 && test_pslot_singleton()==p1);
        float x;memcpy(&x,test_pfslot_fpc+0x100,4);assert(x==6.0f);}
    /* The walking slot: no d95180 for it, its controller checks skipped; its FPC and parent checked by the pass. */
    {   test_pslot_call_count=0;pslot_update_tick=14;pslot_update_slots(100.0);assert(!test_pslot_count('U'));
        assert(pslot_check(15) && pfslot_pass(15) && !strcmp(pfslot_stage,"walking") && !pfslot_late);
        assert(pfslot_pass(17) && pfslot_late==1 && f->updates==2);   /* a frame without X4's call: the walkers here */
        pfslot_update_tick=16;}
    /* The receipt: the same state the same text; slot 2's bank or pose moved, another hash. */
    {   char a[128],b[128];pfslot_frame();assert(pfslot_frame_text(a,sizeof(a)) && !strncmp(a,"|W2:1:",6) && strlen(a)==6+16);
        pfslot_frame();assert(pfslot_frame_text(b,sizeof(b)) && !strcmp(a,b));
        f->bank[0x4b]^=1;pfslot_frame();assert(pfslot_frame_text(b,sizeof(b)) && strcmp(a,b));f->bank[0x4b]^=1;
        float x=9.0f,old;memcpy(&old,test_pfslot_fpc+0x100,4);memcpy(test_pfslot_fpc+0x100,&x,4);pfslot_frame();assert(pfslot_frame_text(b,sizeof(b)) && strcmp(a,b));
        memcpy(test_pfslot_fpc+0x100,&old,4);pfslot_frame();}
    /* The eye on client 2 (lockstep_pilot_eye.h): the presented pose is P_2's FPC pose. */
    {   unsigned saved_pres_slot=pres_slot;peye_seat_fn saved_seat=peye_seat;peye_pose_fn saved_pose=peye_fpc_pose;void **saved_stock=peye_fpc_stock;
        unsigned char *saved_ship1=pilot_slots[0].object;static void *stock_vt[64];static unsigned char ctl[0x100];
        pres_slot=2;peye_seat=(peye_seat_fn)(void*)test_pfslot_ship_conn;peye_fpc_pose=test_pfslot_pose;peye_fpc_stock=stock_vt+1;
        pilot_slots[0].object=test_pnat_ship;{void *r=test_pslot_room;memcpy(p1+PRES_PARENT,&r,8);}
        assert(peye_update(s) && !strcmp(peye_stage,"walk") && peye_walk_fpc==test_pfslot_fpc);
        float out[16],x;peye_ctl=ctl;peye_live=1;InterlockedExchange(&pres_open,1);memcpy(&x,test_pfslot_fpc+0x100,4);
        peye_pose(ctl,out,100.0,1);assert(out[0]==x && x>=6.0f && peye_poses>=1);
        InterlockedExchange(&pres_open,0);peye_live=0;peye_ctl=NULL;peye_reset();
        pres_slot=saved_pres_slot;peye_seat=saved_seat;peye_fpc_pose=saved_pose;peye_fpc_stock=saved_stock;pilot_slots[0].object=saved_ship1;}
    /* Fail closed: the FPC no longer P_2's movement controller; P_2 moved off its room. */
    {   memset(p2+0x200,0,8);assert(!pfslot_pass(18) && !strcmp(pfslot_why,"slot_fpc_lost") && pfoot_failed && !strcmp(pfoot_why,"slot_fpc_lost"));
        {void *fpc=test_pfslot_fpc;memcpy(p2+0x200,&fpc,8);}pfslot_failed=0;pfoot_failed=0;pfslot_why="-";pfoot_why="-";
        {void *other=test_pfbody_elsewhere;memcpy(p2+0x70,&other,8);}assert(!pfslot_check(2) && !strcmp(pfslot_why,"slot_moved"));
        memcpy(p2+0x70,&room,8);pfslot_failed=0;pfoot_failed=0;pfslot_why="-";pfoot_why="-";pfslot_stage="walking";}
    /* Slot 2's GET_UP while walking sits it down: X4's take in its scope, its PlayerController under the vtable copy again,
     * the control position back, the seat flag 1, its bank inactive. */
    {   uint64_t tick=20;stats.issued=tick;test_pslot_call_count=0;pstk_slot_frames[1].tick=tick;
        pstk_slot_frames[1].r.events[0]=(pstk_event){PSTK_ACTION,PFOOT_GET_UP,0};
        assert(pslot_replay(tick) && !f->walking && !s->walking && f->sat==1 && s->controller==test_pslot_c2 && *(void***)test_pslot_c2==&s->vt.slots[0]);
        assert(test_pslot_count('T')==1 && *(int*)(p2+PFSLOT_POSITION)==0x77 && test_pfslot_fpc[PFSLOT_FPC_SEAT]==1 && !f->bank[0x3d] && !p2[PFOOT_PLAYER_FLAG]);
        assert(!pfslot_own_walking() && pfoot_own_seated() && pfslot_claim_ok(2) && !pfslot_any_walking());
        {char a[64];pfslot_frame();assert(pfslot_frame_text(a,sizeof(a)) && !strcmp(a,"|W2:0:0000000000000000"));}
        /* Up again: the FPC is there already (70eee0 keeps it); the ship's own stand-up connection this time. */
        tick=21;stats.issued=tick;pstk_slot_frames[1].tick=tick;test_pfslot_ship_found=1;
        assert(pslot_replay(tick) && f->walking && f->stood==2 && f->placed==2 && f->fpc==test_pfslot_fpc && !s->controller);}
    /* The status row names each slot: walking/getups/kept/stood/sat/ignored/updates/placed/unplaced. */
    {   char row[1024];assert(pfslot_status(row,sizeof(row)) && strstr(row," foot_slots=walking foot_slots_why=- foot_slots_hook=1/") && strstr(row," fs2=1/5/2/2/1/0/"));}
    /* stop+1: the call site back before the seats go; the walking seat leaves with its pointers only. */
    {   assert(pfslot_pass(41) && !pfslot_hook_installed && region[PFSLOT_CALL_SITE]==0xe8 && !strcmp(pfslot_stage,"released") && !f->walking);
        {int32_t rel;memcpy(&rel,region+PFSLOT_CALL_SITE+1,4);assert(PFSLOT_CALL_SITE+5u+(unsigned)rel==PFSLOT_FPC_UPDATE);}
        assert(pslot_release(2) && !s->seated && !s->walking && !*(void**)(p2+0x70) && test_pslot_singleton()==p1);}
    /* Bodies (the crash fix): connected while everyone sits; slot 1's GET_UP replayed (leaving) parks them in that pass,
     * before X4 stands it up; they come back PFBODY_SETTLE ticks after it sat down; slot 2's GET_UP of the tick parks too. */
    {   memset(p2+0x70,0,8);memset(p2+0x200,0,8);stats.issued=6;pilot_stop=100;
        assert(pslot_seat(2));pfbody_connect=test_pfbody_connect;pfbody_filter=test_pfbody_filter_object;test_pfbody_mode=0;test_pfbody_calls=0;
        memcpy(region+PFBODY_KILL_SITE,pfbody_kill_bytes,sizeof(pfbody_kill_bytes));
        assert(pfbody_arm_using() && pfbody_kill_install() && pfbody_connect_slot(2) && pfbody_mask==2 && *(void**)(p2+0x70)==room);
        /* A join save at T (lockstep_pilot_join.h, W3): armed from the join core's save_at on every client, ahead of this
         * tick; T's pass takes the body off as for a get-up (X4's save after step T holds one player), it comes back
         * PFBODY_SETTLE ticks of everyone seated later; another tick moves nothing. The state names slot 2's ranges. */
        {   const int armed=pilot_armed;pilot_armed=1;pfoot_state=PFOOT_SEATED;memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));
            assert(!pjoin_park_arm(6) && pjoin_park_arm(12) && pjoin_park_arm(12) && !pjoin_park_arm(13) && pjoin_park_tick==12);
            assert(pfbody_guard(11) && pfbody_mask==2 && !pfbody_parked);
            assert(pfbody_guard(12) && !pfbody_mask && pfbody_parked && pjoin_parks==1 && !pjoin_park_tick && pjoin_last_park==12 &&
                   *(void**)(p2+0x70)==room && test_pslot_singleton()==p1 && !pstk_scope_slot);
            assert(pfbody_guard(13) && pfbody_parked && pfbody_seated_since==13 && pfbody_guard(13+PFBODY_SETTLE-1) && pfbody_parked);
            assert(pfbody_guard(13+PFBODY_SETTLE) && !pfbody_parked && pfbody_mask==2 && test_pfbody_target==room);
            {char row[160];assert(pjoin_status_text(row,sizeof(row))>0 && strstr(row," join_park=0/12 parks=1 bodies=2 parked=0"));}
            {char st[512];assert(pjoin_state(st,sizeof(st))>0 && !strncmp(st,"v1/m",4) && strstr(st,"|2:0-0,0-0,0-0"));}
            pjoin_parks=0;pjoin_last_park=0;pfbody_parks=0;pfbody_unparks=0;pfbody_seated_since=0;pilot_armed=armed;}
        pfoot_state=PFOOT_SEATED;memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));
        assert(pfbody_guard(20) && pfbody_mask==2 && !pfbody_parked);
        pfoot_state=PFOOT_LEAVING;   /* slot 1's GET_UP was replayed in this pass */
        assert(pfbody_guard(21) && !pfbody_mask && pfbody_parked && pfbody_parks==1 && !test_pfbody_target && *(void**)(p2+0x70)==room &&
               !strcmp(pfbody_stage,"parked") && test_pslot_singleton()==p1 && !pstk_scope_slot);
        pfoot_state=PFOOT_WALKING;assert(pfbody_guard(22) && pfbody_parks==1 && pfbody_pass(23,1) && !strcmp(pfbody_stage,"parked"));
        pfoot_state=PFOOT_SEATED;   /* adopted: it sat down again */
        assert(pfbody_guard(24) && pfbody_parked && pfbody_seated_since==24 && pfbody_guard(24+PFBODY_SETTLE-1) && pfbody_parked);
        assert(pfbody_guard(24+PFBODY_SETTLE) && !pfbody_parked && pfbody_mask==2 && pfbody_unparks==1 && test_pfbody_target==room && *(void**)(p2+0x70)==room);
        {char row[256];assert(pfbody_status(row,sizeof(row)) && strstr(row," parks=1/1"));}
        /* Slot 2's GET_UP staged for this tick: parked before its replay reaches the scope. */
        uint64_t tick=30;pstk_slot_frames[1].tick=tick;pstk_slot_frames[1].r.count=1;pstk_slot_frames[1].r.events[0]=(pstk_event){PSTK_ACTION,PFOOT_GET_UP,0};
        assert(pfbody_guard(tick) && !pfbody_mask && pfbody_parked && pfbody_parks==2);
        /* Outside the window nothing moves. */
        assert(pfbody_guard(pilot_stop+1) && pfbody_parked);
        assert(pfbody_disconnect_slot(2) && pslot_release(2) && pfbody_kill_uninstall() && !memcmp(region+PFBODY_KILL_SITE,pfbody_kill_bytes,8));
        pfbody_armed=0;pfbody_failed=0;pfbody_parked=0;pfbody_seated_since=0;pfbody_mask=0;pfbody_stage="off";pfbody_why="-";
        pfbody_connects=pfbody_disconnects=pfbody_checks=pfbody_parks=pfbody_unparks=0;pfbody_connect=NULL;pfbody_filter=NULL;}
    /* Reset everything this block set. */
    assert(pslot_hook_uninstall());
    memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));memset(p2+0x200,0,8);memset(p2+0x70,0,8);
    pfslot_armed=0;pfslot_failed=0;pfslot_hook_installed=0;pfslot_stage="off";pfslot_why="-";pfslot_hook_calls=pfslot_late=pfslot_getups_claimed=0;
    memset(pfslots,0,sizeof(pfslots));pfslot_fn=saved_fn;pfoot_armed=0;pfoot_failed=0;pfoot_why="-";pfoot_state=PFOOT_SEATED;
    test_pslot_ship_vt[PSLOT_IS_CLASS]=ship_is;memcpy(test_pslot_room+0x70,room_parent,8);memcpy(test_pslot_ship2+0x70,ship_parent,8);
    memset(region+PFSLOT_CONVERSATION,0,8);memset(region+PFOOT_BANK,0,PFOOT_BANK_SIZE);(void)null;
    pilot_start=start;pilot_stop=stop;stats.issued=saved_issued;pstk_capturing=capturing;pstk_capture_slot=capture_slot;
    printf("pilot foot slots: slot 2 up in its scope (release, FPC, placement, bank), its walk and update, receipt, eye, sit-down, bodies parked around get-ups "
           "and for a join save PASS\n");
}

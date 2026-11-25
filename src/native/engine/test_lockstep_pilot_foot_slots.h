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
static void *test_pfslot_fpc_vt[40],*test_pfslot_dock_vt[600],*test_pfslot_target_vt[600];
static unsigned char test_pfslot_may=1;
static unsigned test_pfslot_requests,test_pfslot_places,test_pfslot_poses,test_pfslot_updates,test_pfslot_ship_found;
static unsigned char *test_pfslot_update_singleton[4];
static unsigned char test_pfslot_update_key[4];
static unsigned char test_pfslot_fpc_is(void *self,int cls) {(void)self;return cls==PFSLOT_FPC_CLASS;}
/* A docked ship's seat: X4's answer "the ship is docked", and X4 ending a slot's controller itself (0xd84730). */
static int test_pfslot_docked;
static int test_pfslot_ship_docked(const unsigned char *ship) {(void)ship;return test_pfslot_docked;}
static unsigned test_pslot_ctl_end_calls,test_pslot_ctl_end_slot;
static void test_pslot_ctl_end(void *controller) {(void)controller;++test_pslot_ctl_end_calls;test_pslot_ctl_end_slot=pstk_scope_slot;}
static void *test_pfslot_fpc_next(void *self) {(void)self;return NULL;}
static void *test_pfslot_rebase_args[3],*test_pfslot_rebase_singleton;static unsigned test_pfslot_rebases;
static void test_pfslot_rebase(void *controller,void *from,void *to) {
    test_pfslot_rebase_args[0]=controller;test_pfslot_rebase_args[1]=from;test_pfslot_rebase_args[2]=to;
    test_pfslot_rebase_singleton=test_pslot_singleton();++test_pfslot_rebases;
}
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
/* 7176f0: the same for a connection whose record carries the get-up tag; 455f20 / 7713c0: the record; cb170: its tag. */
static unsigned test_pfslot_tagged_places;static int test_pfslot_record_tagged;
static unsigned char test_pfslot_record[0x140],test_pfslot_record_name[8];
static void test_pfslot_place_tagged(void *player,void *target,void *name,unsigned char flag) {
    assert(player==test_pslot_p2 && target==test_pfslot_target && name==test_pfslot_record_name && flag==1 && test_pslot_singleton()==test_pslot_p2);
    float pose[16];test_pfslot_pose(test_pfslot_fpc,pose,100.0,1);
    float x=7.0f;memcpy(test_pfslot_fpc+0x100,&x,4);++test_pfslot_tagged_places;
}
static void *test_pfslot_ship_record(void *ship,int type) {
    assert(ship==test_pslot_ship2 && type==3);void *name=test_pfslot_record_name;memcpy(test_pfslot_record+PFSLOT_RECORD_NAME,&name,8);return test_pfslot_record;
}
static void *test_pfslot_dock_record(void *dock,int type) {(void)dock;assert(type==3);return NULL;}   /* a connection without a record: 717c30 */
static unsigned char test_pfslot_tag_has(void *tags,const int *tag) {
    assert(tags==test_pfslot_record+PFSLOT_RECORD_TAGS && *tag==0x4242);return test_pfslot_record_tagged!=0;
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
/* The pilot chair's X4 side (pfslot_chair): SMem::Allocate from a pool, the event's constructor as X4's (its vtable, the
 * trigger +0x18, the name +0x20, the instant bytes +0x40/+0x41), PostEvent as rows (the scope and the singleton at the post),
 * the character's properties per macro (the player's macro: no chair config; one with chairconfig_paranid, a long name;
 * one with a short name the trigger table does not know) and the trigger table's lookup. */
typedef struct {void *source,*event,*name,*singleton;int trigger;unsigned char instant[2],immediate;unsigned scope;double time;} test_pchair_row;
static test_pchair_row test_pchair_rows[16];
static unsigned test_pchair_count,test_pchair_allocs;
static int test_pchair_no_memory;
static __attribute__((aligned(16))) unsigned char test_pchair_pool[16][PCHAIR_EVENT_SIZE],test_pchair_props[3][0x400],test_pchair_config[2][0x340],
    test_pchair_macros[2][0x80];
static void *test_pchair_allocate(size_t size,uintptr_t a,uintptr_t b,uintptr_t c,unsigned alignment) {
    assert(size==PCHAIR_EVENT_SIZE && !a && !b && !c && alignment==0x10);
    if(test_pchair_no_memory) return NULL;
    return test_pchair_pool[test_pchair_allocs++%16];
}
static void *test_pchair_event(void *event,int trigger,void *name,unsigned char instant,unsigned char again) {
    unsigned char *e=event;void *vt=pnat_base+PCHAIR_EVENT_VTABLE;
    memset(e,0,PCHAIR_EVENT_SIZE);memcpy(e,&vt,8);memcpy(e+0x18,&trigger,4);memcpy(e+0x20,&name,8);e[0x40]=instant;e[0x41]=again;
    return event;
}
static void test_pchair_post(void *source,void *event,double time,unsigned char immediate) {
    assert(test_pchair_count<16);
    const unsigned char *e=event;test_pchair_row *r=&test_pchair_rows[test_pchair_count++];
    r->source=source;r->event=event;memcpy(&r->trigger,e+0x18,4);memcpy(&r->name,e+0x20,8);r->instant[0]=e[0x40];r->instant[1]=e[0x41];
    r->immediate=immediate;r->time=time;r->scope=pstk_scope_slot;r->singleton=test_pslot_singleton();
}
static unsigned char *test_pchair_character(void *macro) {
    if(macro==test_pchair_macros[0]) return test_pchair_props[1];
    if(macro==test_pchair_macros[1]) return test_pchair_props[2];
    return test_pchair_props[0];
}
static int test_pchair_trigger_id(uintptr_t length,const void *view) {
    const char *text=NULL;uint64_t n=0;memcpy(&text,view,8);memcpy(&n,(const unsigned char*)view+8,8);
    assert(length==n && text);
    return n==19 && !memcmp(text,"chairconfig_paranid",19)?0x12:0;
}
static void test_pchair_fixtures(void) {
    static const char paranid[]="chairconfig_paranid";
    memset(test_pchair_props,0,sizeof(test_pchair_props));memset(test_pchair_config,0,sizeof(test_pchair_config));
    void *config=test_pchair_config[0];const char *text=paranid;uint64_t n=19,capacity=31;   /* a long name: its text elsewhere */
    memcpy(test_pchair_props[1]+PCHAIR_CONFIG,&config,8);
    memcpy(test_pchair_config[0]+PCHAIR_CONFIG_NAME,&text,8);memcpy(test_pchair_config[0]+PCHAIR_CONFIG_NAME+0x10,&n,8);
    memcpy(test_pchair_config[0]+PCHAIR_CONFIG_NAME+0x18,&capacity,8);
    config=test_pchair_config[1];n=9;capacity=15;                                                /* a short one: in its buffer */
    memcpy(test_pchair_props[2]+PCHAIR_CONFIG,&config,8);memcpy(test_pchair_config[1]+PCHAIR_CONFIG_NAME,"chair_odd",10);
    memcpy(test_pchair_config[1]+PCHAIR_CONFIG_NAME+0x10,&n,8);memcpy(test_pchair_config[1]+PCHAIR_CONFIG_NAME+0x18,&capacity,8);
    test_pchair_count=test_pchair_allocs=0;test_pchair_no_memory=0;
}
static const pchair_fns test_pchair_fns={test_pchair_allocate,test_pchair_event,test_pchair_post,test_pchair_character,test_pchair_trigger_id};
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
    {void *vt=test_pfslot_target_vt;test_pfslot_target_vt[0x11e0/8]=(void*)test_pfslot_target_is;memset(test_pfslot_target,0,sizeof(test_pfslot_target));memcpy(test_pfslot_target,&vt,8);
     test_pfslot_target_vt[PCHAIR_TARGET_SLOT/8]=(void*)test_pfslot_target_is;*(uint64_t*)(test_pfslot_target+8)=0x4d00ULL;}   /* 7173f0's test; its ID */
    {void *c=test_pfslot_conversation;memset(test_pfslot_conversation,0,sizeof(test_pfslot_conversation));*(int*)(test_pfslot_conversation+0x1a0)=1;
     memcpy(region+PFSLOT_CONVERSATION,&c,8);}
    *(int*)(region+PFSLOT_POSITION_NONE)=0x55;
    {int32_t rel=(int32_t)(PFSLOT_FPC_UPDATE-(PFSLOT_CALL_SITE+5u));region[PFSLOT_CALL_SITE]=0xe8;memcpy(region+PFSLOT_CALL_SITE+1,&rel,4);}
    /* Arming: after slot 1's foot and the stock slots, X4's functions there (with the death module too: its block below). */
    pfslot_fn=(pfslot_fns){test_pfslot_may_stand,test_pfslot_make_fpc,test_pfslot_update_fn,test_pfslot_pose,test_pfslot_place,test_pfslot_conn_type,
        test_pfslot_ship_conn,test_pfslot_dock_conn,test_pfslot_place_tagged,test_pfslot_ship_record,test_pfslot_dock_record,test_pfslot_tag_has};
    *(int*)(region+PFSLOT_GETUP_TAG)=0x4242;
    pfoot_armed=0;assert(!pfslot_arm_using() && !strcmp(pstk_arm_why,"foot_slots_without_foot"));pfoot_armed=1;
    pfslot_fn.place=NULL;assert(!pfslot_arm_using() && !strcmp(pstk_arm_why,"foot_slots_functions"));pfslot_fn.place=test_pfslot_place;
    {unsigned mask=pslot_mask;pslot_mask=0;assert(!pfslot_arm_using() && !strcmp(pstk_arm_why,"foot_slots_without_slots"));pslot_mask=mask;}
    /* Without the module slot 2's walking keys and GET_UP are refused; with it they are slot 2's replay ids. */
    assert(pstk_verdict_slot(PSTK_STATE,0x1a,2)==PSTK_REFUSE && pstk_verdict_slot(PSTK_ACTION,0x115,2)==PSTK_REFUSE && !pfslot_claim_ok(2));
    /* The pilot chair (native_gate_pilot_chair) needs the foot slots and X4's five functions. */
    pchair_fns saved_chair=pchair_fn;test_pchair_fixtures();pchair_fn=test_pchair_fns;pchair_armed=0;
    assert(!pchair_arm_using() && !strcmp(pstk_arm_why,"chair_without_foot_slots"));
    assert(pfslot_arm_using() && pfslot_armed && !strcmp(pfslot_stage,"armed") && pfslot_arm_using());
    pchair_fn.event=NULL;assert(!pchair_arm_using() && !strcmp(pstk_arm_why,"chair_functions"));pchair_fn.event=test_pchair_event;
    assert(pchair_arm_using() && pchair_armed && !strcmp(pchair_stage,"live") && pchair_arm_using() && pchair_digest==PCHAIR_DOMAIN);
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
    /* A room change X4 asks for a walking slot's player (lockstep_pilot_foot_body.h): its upward pointer to the new room,
     * X4's rebase on its controller with the two rooms in the slot's scope, and the slot's room follows in the checks.
     * With an offset, into a dying room or one out of the world: held. A room that is killed sends the pointer home.
     * The sit-down below starts from the other room. */
    {   static unsigned char other[0x100];
        pfbody_rebase_fn saved=pfbody_rebase;pfbody_rebase=test_pfslot_rebase;test_pfslot_fpc_vt[PFBODY_FPC_REBASE_SLOT]=(void*)test_pfslot_rebase;
        memset(other,0,sizeof(other));other[PFBODY_PLACED]=1;test_pfslot_rebases=0;
        pvehk_events_on=0;   /* the notices ask X4's class tests of real components (the tests take posts by pvehk_event_test) */
        LONG64 held=pfbody_rooms_held,moved=pfbody_rooms_moved,home=pfbody_rooms_home;
        pfbody_room(p2,p2,1.0,other,other,0,0);assert(pfbody_rooms_held==held+1 && *(void**)(p2+0x70)==room && !s->walk_room);
        other[PFBODY_LIFECYCLE]=1;pfbody_room(p2,p2,1.0,other,NULL,0,0);other[PFBODY_LIFECYCLE]=0;
        other[PFBODY_PLACED]=0;pfbody_room(p2,p2,1.0,other,NULL,0,0);other[PFBODY_PLACED]=1;
        assert(pfbody_rooms_held==held+3 && *(void**)(p2+0x70)==room && !s->walk_room && !test_pfslot_rebases);
        pfbody_room(p2,p2,1.0,room,NULL,0,0);assert(pfbody_rooms_held==held+3 && pfbody_rooms_moved==moved && !test_pfslot_rebases);   /* there already */
        pfbody_room(p2,p2,1.0,other,NULL,0,0);
        assert(*(void**)(p2+0x70)==(void*)other && s->walk_room==other && pslot_at(s)==other && pfbody_rooms_moved==moved+1 && test_pfslot_rebases==1 &&
               test_pfslot_rebase_args[0]==(void*)test_pfslot_fpc && test_pfslot_rebase_args[1]==(void*)room && test_pfslot_rebase_args[2]==(void*)other &&
               test_pfslot_rebase_singleton==p2 && test_pslot_singleton()==p1 && !pstk_scope_slot && pfbody_rooms_held==held+3);
        assert(pfslot_check(2) && !pfslot_failed && !pslot_failed);
        {char row[512];assert(pfbody_status(row,sizeof(row)) && strstr(row," room=") && strstr(row,"/1/0/0 attn="));}
        (void)pfbody_kill_query(other);   /* X4 kills that room */
        assert(*(void**)(p2+0x70)==room && !s->walk_room && pfbody_rooms_home==home+1 && test_pfslot_rebases==1);
        pfbody_room(p2,p2,1.0,other,NULL,0,0);assert(s->walk_room==other && test_pfslot_rebases==2);
        const unsigned char was_placed=((unsigned char*)room)[PFBODY_PLACED];((unsigned char*)room)[PFBODY_PLACED]=1;   /* the cockpit is in the world */
        pfbody_room(p2,p2,1.0,room,NULL,0,0);   /* back into its seat's room by walking */
        ((unsigned char*)room)[PFBODY_PLACED]=was_placed;
        assert(*(void**)(p2+0x70)==room && !s->walk_room && test_pfslot_rebases==3 && test_pfslot_rebase_args[1]==(void*)other && test_pfslot_rebase_args[2]==(void*)room);
        pfbody_room(p2,p2,1.0,other,NULL,0,0);assert(s->walk_room==other && test_pfslot_rebases==4);
        pfbody_rebase=saved?saved:test_pfslot_rebase;
    }
    /* Slot 2's GET_UP while walking sits it down: X4's take in its scope, its PlayerController under the vtable copy again,
     * the control position back, the seat flag 1, its bank inactive. From another room: under its seat's room first. */
    {   uint64_t tick=20;stats.issued=tick;test_pslot_call_count=0;pstk_slot_frames[1].tick=tick;
        pstk_slot_frames[1].r.events[0]=(pstk_event){PSTK_ACTION,PFOOT_GET_UP,0};
        pfslot_ship_docked_test=test_pfslot_ship_docked;test_pfslot_docked=0;   /* the ship flies: X4's answer in the fixture */
        assert(pslot_replay(tick) && !f->walking && !s->walking && f->sat==1 && s->controller==test_pslot_c2 && *(void***)test_pslot_c2==&s->vt.slots[0]);
        assert(*(void**)(p2+0x70)==room && !s->walk_room && test_pfslot_rebases==5 && test_pfslot_rebase_args[2]==(void*)room);
        assert(test_pslot_count('T')==1 && *(int*)(p2+PFSLOT_POSITION)==0x77 && test_pfslot_fpc[PFSLOT_FPC_SEAT]==1 && !f->bank[0x3d] && !p2[PFOOT_PLAYER_FLAG]);
        assert(!pfslot_own_walking() && pfoot_own_seated() && pfslot_claim_ok(2) && !pfslot_any_walking());
        /* The chair: the dock's connection without a record (717a90's case): nothing posted, counted untagged. */
        assert(!test_pchair_count && pchairs[2].untagged==1 && !pchairs[2].posted && pchair_digest==PCHAIR_DOMAIN);
        {char a[64];pfslot_frame();assert(pfslot_frame_text(a,sizeof(a)) && !strcmp(a,"|W2:0:0000000000000000"));}
        /* Up again: the FPC is there already (70eee0 keeps it); the ship's own stand-up connection this time, and its
         * record carries the get-up tag: 716e20's choice, the tagged placement with the record's name (never 717c30). */
        tick=21;stats.issued=tick;pstk_slot_frames[1].tick=tick;test_pfslot_ship_found=1;test_pfslot_record_tagged=1;
        {const unsigned places=test_pfslot_places;test_pfslot_tagged_places=0;
         assert(pslot_replay(tick) && f->walking && f->stood==2 && f->placed==2 && f->tagged==1 && f->fpc==test_pfslot_fpc && !s->controller);
         float x;memcpy(&x,test_pfslot_fpc+0x100,4);assert(test_pfslot_tagged_places==1 && test_pfslot_places==places && x==7.0f);}
        assert(!test_pchair_count);   /* the get-up's deactivate_chair is X4's own post in 7176f0 */
        test_pfslot_record_tagged=0;}
    /* The status row names each slot: walking/getups/kept/stood/sat/ignored/updates/placed/unplaced. */
    {   char row[1024];assert(pfslot_status(row,sizeof(row)) && strstr(row," foot_slots=walking foot_slots_why=- foot_slots_hook=1/") && strstr(row," fs2=1/5/2/2/1/0/"));}
    /* A docked ship's seat (16 Nov). Slot 2 walks; its ship is docked: the sit-down takes no controller, the seat is
     * 'docked'; the get-up from it releases nothing; the ship free again: the take, in its scope; and X4 ending C_2
     * itself (a docking: the destructor's entry hook) runs in slot 2's scope and leaves the seat docked. */
    {   const LONG64 sat=f->sat,stood=f->stood;const unsigned takes=test_pslot_count('T'),releases=test_pslot_count('R');
        uint64_t tick=22;stats.issued=tick;pstk_slot_frames[1].tick=tick;pstk_slot_frames[1].r.events[0]=(pstk_event){PSTK_ACTION,PFOOT_GET_UP,0};
        pfslot_ship_docked_test=test_pfslot_ship_docked;test_pfslot_docked=1;
        test_pfslot_record_tagged=1;test_pchair_count=0;test_pchair_allocs=0;   /* the ship's own seat connection, tagged */
        assert(pslot_replay(tick) && !f->walking && !s->walking && s->docked && s->seated && !s->controller && f->sat==sat+1 && pfslot_dock_sits==1);
        assert(test_pslot_count('T')==takes && *(int*)(p2+PFSLOT_POSITION)==0x77 && test_pfslot_fpc[PFSLOT_FPC_SEAT]==1 && pfslot_claim_ok(2));
        /* The chair at the docked sit-down: 7173f0's two events (the player's chair config, here the default 0x11, then
         * activate_chair 6) with the record's name, played (instant 0), posted not immediate at the game time to the target,
         * in slot 2's scope (its player the singleton). */
        assert(test_pchair_count==2 && pchairs[2].posted==1 && !pchairs[2].instant && pchairs[2].config==PCHAIR_CONFIG_DEFAULT && pchair_digest!=PCHAIR_DOMAIN);
        for(unsigned i=0;i<2;i++) {
            const test_pchair_row *r=&test_pchair_rows[i];
            assert(r->source==test_pfslot_target && r->event==test_pchair_pool[i] && r->trigger==(i?PCHAIR_ACTIVATE:PCHAIR_CONFIG_DEFAULT) &&
                   r->name==test_pfslot_record_name && !r->instant[0] && !r->instant[1] && !r->immediate && r->time==100.0 && r->scope==2 &&
                   r->singleton==p2 && *(void**)r->event==(void*)(region+PCHAIR_EVENT_VTABLE));
        }
        assert(pfslot_undocked() && s->docked && !s->controller && !pfslot_dock_takes);          /* still docked: nothing */
        {char row[1024];assert(pfslot_status(row,sizeof(row)) && strstr(row," docked=2/0/1/0"));}
        tick=23;stats.issued=tick;pstk_slot_frames[1].tick=tick;                                 /* up from the docked seat */
        assert(pslot_replay(tick) && f->walking && s->walking && !s->docked && !s->controller && f->stood==stood+1 && pfslot_dock_getups==1);
        assert(test_pslot_count('R')==releases);
        tick=24;stats.issued=tick;pstk_slot_frames[1].tick=tick;                                 /* and down again */
        assert(pslot_replay(tick) && !f->walking && s->docked && !s->controller);
        assert(test_pchair_count==4 && pchairs[2].posted==2 && test_pchair_rows[2].trigger==PCHAIR_CONFIG_DEFAULT && test_pchair_rows[3].trigger==PCHAIR_ACTIVATE);
        test_pfslot_docked=0;                                                                    /* the ship is free: its controls */
        assert(pfslot_undocked() && !s->docked && s->controller==test_pslot_c2 && *(void***)test_pslot_c2==&s->vt.slots[0] &&
               pfslot_dock_takes==1 && test_pslot_count('T')==takes+1 && test_pslot_singleton()==p1);
        /* X4 ends C_2 itself: in slot 2's scope, the seat docked, the canonical state back. */
        pslot_ctl_end_original=test_pslot_ctl_end;test_pslot_ctl_end_calls=0;test_pslot_ctl_end_slot=0;
        pslot_ctl_end(test_pslot_c2);memset(s->ship+0x200,0,8);
        assert(test_pslot_ctl_end_calls==1 && test_pslot_ctl_end_slot==2 && !s->controller && s->docked && !s->dock_checked && s->seated &&
               pslot_ctl_ends_scoped==1 && !pslot_ctl_ends_plain && !pstk_scope_slot && test_pslot_singleton()==p1);
        {unsigned char other[16]={0};pslot_ctl_end(other);assert(test_pslot_ctl_end_calls==2 && test_pslot_ctl_end_slot==0 && pslot_ctl_ends_scoped==1);}   /* not a slot's: X4's call */
        pslot_ctl_end_original=NULL;s->dock_checked=1;
        test_pfslot_docked=1;tick=25;stats.issued=tick;pstk_slot_frames[1].tick=tick;            /* as the tests below expect it: walking */
        assert(pslot_replay(tick) && f->walking && s->walking && !s->docked);
        test_pfslot_docked=0;
        assert(test_pchair_count==4 && pchairs[2].posted==2);   /* the take after the undocking and the stand-up post nothing */
    }
    /* stop+1: the call site back before the seats go; the walking seat leaves with its pointers only. */
    {   assert(pfslot_pass(41) && !pfslot_hook_installed && region[PFSLOT_CALL_SITE]==0xe8 && !strcmp(pfslot_stage,"released") && !f->walking);
        {int32_t rel;memcpy(&rel,region+PFSLOT_CALL_SITE+1,4);assert(PFSLOT_CALL_SITE+5u+(unsigned)rel==PFSLOT_FPC_UPDATE);}
        assert(pslot_release(2) && !s->seated && !s->walking && !*(void**)(p2+0x70) && test_pslot_singleton()==p1);}
    /* Bodies (the crash fix): connected while everyone sits; slot 1's GET_UP replayed (leaving) parks them in that pass,
     * before X4 stands it up; they come back PFBODY_SETTLE ticks after it sat down; slot 2's GET_UP of the tick parks too. */
    {   memset(p2+0x70,0,8);memset(p2+0x200,0,8);stats.issued=6;pilot_stop=100;
        /* The chair at a seat: slot 2's first seat of the session (its earlier ones were before the chair was armed) is
         * instant, as X4's start seat; the status names the posts. */
        test_pchair_count=0;
        assert(pslot_seat(2) && test_pchair_count==2 && test_pchair_rows[0].instant[0]==1 && test_pchair_rows[1].instant[1]==1 &&
               test_pchair_rows[0].trigger==PCHAIR_CONFIG_DEFAULT && test_pchair_rows[1].trigger==PCHAIR_ACTIVATE && test_pchair_rows[0].scope==2 &&
               test_pchair_rows[1].source==test_pfslot_target && pchairs[2].posted==3 && pchairs[2].instant==1 && pchairs[2].seats==1);
        {char row[2048];assert(pfslot_status(row,sizeof(row)) && strstr(row," chair=live/3/1/0/") && strstr(row," chair_why=- chair2=3/1/0/1/11"));}
        pfbody_connect=test_pfbody_connect;pfbody_filter=test_pfbody_filter_object;test_pfbody_mode=0;test_pfbody_calls=0;
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
    pchair_armed=0;pchair_failed=0;pchair_hold=0;pchair_stage="off";pchair_why="-";memset(pchairs,0,sizeof(pchairs));pchair_fn=saved_chair;
    test_pfslot_record_tagged=0;
    memset(pfslots,0,sizeof(pfslots));pfslot_fn=saved_fn;pfoot_armed=0;pfoot_failed=0;pfoot_why="-";pfoot_state=PFOOT_SEATED;
    test_pslot_ship_vt[PSLOT_IS_CLASS]=ship_is;memcpy(test_pslot_room+0x70,room_parent,8);memcpy(test_pslot_ship2+0x70,ship_parent,8);
    memset(region+PFSLOT_CONVERSATION,0,8);memset(region+PFOOT_BANK,0,PFOOT_BANK_SIZE);(void)null;
    pilot_start=start;pilot_stop=stop;stats.issued=saved_issued;pstk_capturing=capturing;pstk_capture_slot=capture_slot;
    printf("pilot foot slots: slot 2 up in its scope (release, FPC, placement, bank), its walk and update, receipt, eye, sit-down, its chair (untagged, "
           "docked sit-downs played, first seat instant), bodies parked around get-ups and for a join save PASS\n");
}
/* The pilot chair for slots 2 and 3 (pfslot_chair), on the three-slot fixtures of test_pilot_slots (client 3): two players
 * in two ships, each chair activated in its own slot's scope with its own character's chair config, the first seats
 * instant; one ship cannot seat two players (the second seat is refused before anything is posted); slot 3's later seat
 * and its docked and flying sit-downs played; a record without the tag, without a name, a target of another class:
 * nothing; outside the slot's scope and an allocation that fails: stop 226; a joiner's rebuild posts nothing and its next
 * seat is the session's; the digest is the same for the same steps and holds no address. */
static __attribute__((aligned(16))) unsigned char test_pchair_target[2][0x40],test_pchair_record[2][0x140],test_pchair_name[2][8],test_pchair_fpc[0x3c0];
static void *test_pchair_target_vt[600];
static int test_pchair_tagged[2],test_pchair_other_class;
static unsigned char test_pchair_ship_is(void *self,int cls) {(void)self;return cls==PFSLOT_SHIP_CLASS;}
static unsigned char test_pchair_target_is(void *self,int cls) {
    assert(self==test_pchair_target[0] || self==test_pchair_target[1]);return cls==PFSLOT_TARGET_CLASS && !test_pchair_other_class;
}
/* The three-slot controllers' class test and chain (test_pslot_c_is knows slot 2's fixture controller only). */
static unsigned char test_pchair_c_is(void *self,int cls) {assert(self==test_pslot3_c[0] || self==test_pslot3_c[1]);return cls==PSLOT_CONTROLLER_CLASS;}
static void *test_pchair_c_next(void *self) {(void)self;return NULL;}
static int test_pchair_ship_index(const void *ship) {for(int i=0;i<2;i++) if(ship==test_pslot3_ship[i]) return i;assert(0);return -1;}
static int test_pchair_conn_type(int position) {assert(position==0x77);return 3;}
static unsigned char test_pchair_ship_conn(void *ship,void *out,int type) {
    const int i=test_pchair_ship_index(ship);void *pair[2]={test_pchair_target[i],(void*)(uintptr_t)(0x10+i)};
    assert(type==3);memcpy(out,pair,16);return 1;
}
static void *test_pchair_ship_record(void *ship,int type) {assert(type==3);return test_pchair_record[test_pchair_ship_index(ship)];}
static unsigned char test_pchair_dock_conn(void *dock,void *out,int type) {(void)dock;(void)out;(void)type;assert(0);return 0;}
static void *test_pchair_dock_record(void *dock,int type) {(void)dock;(void)type;assert(0);return NULL;}
static unsigned char test_pchair_tag_has(void *tags,const int *tag) {
    assert(*tag==0x4242);
    for(int i=0;i<2;i++) if(tags==test_pchair_record[i]+PFSLOT_RECORD_TAGS) return (unsigned char)test_pchair_tagged[i];
    assert(0);return 0;
}
/* Fresh arming (a client's or a joiner's), both slots seated again at take-1 with every step as before: the digest. */
static uint64_t test_pchair_session(uint64_t take,unsigned later3) {
    pchair_armed=0;assert(pchair_arm_using() && pchair_digest==PCHAIR_DOMAIN);
    pchairs[3].seats=later3;   /* slot 3 sat before: its seat is a later one */
    for(unsigned k=2;k<=3;k++) if(pslots[k].seated) assert(pslot_release(k));
    stats.issued=take-1;test_pchair_count=0;
    assert(pslot_seat(2) && pslot_seat(3) && test_pchair_count==4 && test_pchair_rows[2].instant[0]==!later3);
    return pchair_digest;
}
static void test_pilot_chair_slots(unsigned char *region,unsigned char *p1) {
    const pfslot_fns saved_fn=pfslot_fn;const pchair_fns saved_chair=pchair_fn;const uint64_t saved_issued=stats.issued;const unsigned saved_slot=stats.slot;
    int (*saved_docked)(const unsigned char*)=pfslot_ship_docked_test;const unsigned saved_mode=pnat_mode;
    test_pslot_reset();test_pslot3_made=0;test_pslot3_row_count=0;pview_slot_player=NULL;
    void **cvt=(void**)(region+PSLOT_VTABLE);cvt[49]=(void*)test_pslot3_assist;cvt[50]=(void*)test_pslot3_mode;
    void *const saved_c_is=cvt[3],*const saved_c_next=cvt[20];cvt[3]=(void*)test_pchair_c_is;cvt[20]=(void*)test_pchair_c_next;
    vps_create=test_pslot3_factory;pnat_fn.controller=test_pslot3_controller_of;
    pslot_fn.take=test_pslot3_take;pslot_fn.release=test_pslot3_release;pslot_fn.update=test_pslot3_update;
    test_pslot3_ship_vt[PSLOT_TAKE]=(void*)test_pslot3_take;test_pslot3_ship_vt[PSLOT_RELEASE]=(void*)test_pslot3_release;
    test_pslot3_ship_vt[PSLOT_IS_CLASS]=(void*)test_pchair_ship_is;test_pslot3_room_vt[PSLOT_IS_CLASS]=(void*)test_pslot3_is_room;
    test_pchair_target_vt[PCHAIR_TARGET_SLOT/8]=(void*)test_pchair_target_is;
    for(unsigned i=0;i<2;i++) {
        void *svt=test_pslot3_ship_vt,*rvt=test_pslot3_room_vt,*room=test_pslot3_room[i],*ship=test_pslot3_ship[i],*tvt=test_pchair_target_vt,*name=test_pchair_name[i];
        memset(test_pslot3_ship[i],0,0x500);memcpy(test_pslot3_ship[i],&svt,8);memcpy(test_pslot3_ship[i]+PSLOT_ROOM,&room,8);test_pslot3_ship[i][0xd1]=1;
        memset(test_pslot3_room[i],0,0x400);memcpy(test_pslot3_room[i],&rvt,8);memcpy(test_pslot3_room[i]+0x70,&ship,8);   /* the cockpit in its ship */
        memset(test_pchair_target[i],0,0x40);memcpy(test_pchair_target[i],&tvt,8);*(uint64_t*)(test_pchair_target[i]+8)=0x5100u+i;   /* its ID */
        memset(test_pchair_record[i],0,0x140);memcpy(test_pchair_record[i]+PFSLOT_RECORD_NAME,&name,8);test_pchair_tagged[i]=1;
        pilot_slots[i+1].resolved=1;pilot_slots[i+1].object=test_pslot3_ship[i];pilot_slots[i+1].player=0;pilot_slots[i+1].dead=0;
    }
    pfslot_fn=(pfslot_fns){NULL,NULL,NULL,NULL,NULL,test_pchair_conn_type,test_pchair_ship_conn,test_pchair_dock_conn,NULL,test_pchair_ship_record,
        test_pchair_dock_record,test_pchair_tag_has};
    *(int*)(region+PFSLOT_GETUP_TAG)=0x4242;
    test_pchair_fixtures();pchair_fn=test_pchair_fns;pilot_slot_count=3;stats.slot=3;
    const uint64_t take=pilot_start-PILOT_SUPPRESS_LEAD;
    assert(pslot_arm_using(7) && pslot_mask==6 && pslot_construct_all(take) && test_pslot3_made==2);
    {void *m=test_pchair_macros[0];memcpy(test_pslot3_p[1]+PCHAIR_MACRO,&m,8);}   /* slot 3's player: another character (chairconfig_paranid) */
    pfslot_armed=1;pchair_armed=0;assert(pchair_arm_using() && pchair_armed);
    /* take-1: both seats, each chair in its own slot's scope with its own player the singleton, its own ship's target, its
     * own character's config, instant (the first seats: X4's start seat); X4's order: the config, then activate_chair. */
    stats.issued=take-1;
    assert(pslot_seat_all(take) && pslots[2].seated && pslots[3].seated && test_pslot_singleton()==p1 && !pstk_scope_slot && test_pchair_count==4);
    for(unsigned i=0;i<4;i++) {
        const test_pchair_row *r=&test_pchair_rows[i];const unsigned j=i/2;
        assert(r->source==test_pchair_target[j] && r->scope==j+2 && r->singleton==test_pslot3_p[j] && r->name==test_pchair_name[j] &&
               r->trigger==(i&1?PCHAIR_ACTIVATE:j?0x12:PCHAIR_CONFIG_DEFAULT) && r->instant[0]==1 && r->instant[1]==1 && !r->immediate &&
               r->time==100.0 && r->event==test_pchair_pool[i]);
    }
    assert(pchairs[2].posted==1 && pchairs[3].posted==1 && pchairs[2].instant==1 && pchairs[3].instant==1 && pchairs[2].config==0x11 && pchairs[3].config==0x12);
    {char row[4096];assert(pfslot_status(row,sizeof(row)) && strstr(row," chair=live/2/0/0/") && strstr(row," chair2=1/0/0/1/11 chair3=1/0/0/1/12"));}
    /* One ship seats one player: slot 3 in slot 2's ship is refused (it has slot 2's PlayerController) before any post. */
    {   pslot_slot *x=&pslots[3];
        assert(pslot_release(3) && !x->seated);
        test_pchair_count=0;
        assert(!pslot_seat_ship(3,test_pslot3_ship[0]) && !strcmp(pslot_why,"ship_controlled") && !test_pchair_count && pchairs[3].posted==1 &&
               pchairs[3].seats==1 && !x->seated);
        pslot_failed=0;pslot_why="-";pslot_stage="seated";
        /* Its own ship again (a later seat, as X4's teleport into a seat: the suit, the way home, a respawn): played. */
        assert(pslot_seat(3) && x->seated && test_pchair_count==2 && !test_pchair_rows[0].instant[0] && !test_pchair_rows[1].instant[1] &&
               test_pchair_rows[0].trigger==0x12 && test_pchair_rows[1].trigger==PCHAIR_ACTIVATE && test_pchair_rows[0].scope==3 &&
               test_pchair_rows[1].source==test_pchair_target[1] && pchairs[3].posted==2 && pchairs[3].instant==1 && pchairs[3].seats==2);}
    /* Slot 3 sits down (its GET_UP while walking, in its scope): in a docked ship, then flying; both played to its own ship's
     * target. Before each, the state its stand-up leaves: no controller (released), walking, its FPC. */
    {   pslot_slot *x=&pslots[3];pfslot_slot *f=&pfslots[3];
        assert(pslot_enter(3));
        assert(InterlockedCompareExchangePointer((void*volatile*)x->controller,pnat_base+PSLOT_VTABLE,&x->vt.slots[0])==(void*)&x->vt.slots[0]);
        test_pslot3_release(x->ship);x->controller=NULL;
        assert(pslot_leave(3));
        memset(test_pchair_fpc,0,sizeof(test_pchair_fpc));x->walking=1;f->walking=1;f->fpc=test_pchair_fpc;
        pfslot_ship_docked_test=test_pfslot_ship_docked;test_pfslot_docked=1;test_pchair_count=0;
        assert(pslot_enter(3) && pfslot_sit_down(3) && pslot_leave(3));
        assert(x->docked && !x->walking && !f->walking && !x->controller && test_pchair_count==2 && test_pchair_rows[0].source==test_pchair_target[1] &&
               test_pchair_rows[0].scope==3 && test_pchair_rows[0].trigger==0x12 && !test_pchair_rows[0].instant[0] && pchairs[3].posted==3);
        x->docked=0;x->walking=1;f->walking=1;test_pfslot_docked=0;test_pchair_count=0;
        assert(pslot_enter(3) && pfslot_sit_down(3) && pslot_leave(3));
        assert(x->controller==test_pslot3_c[1] && !x->walking && test_pchair_count==2 && test_pchair_rows[1].trigger==PCHAIR_ACTIVATE &&
               !test_pchair_rows[1].instant[1] && test_pchair_rows[1].singleton==test_pslot3_p[1] && pchairs[3].posted==4 && f->sat==2);
        pfslot_ship_docked_test=saved_docked;f->fpc=NULL;}
    /* No tagged record, no name, a target of another class: nothing (X4's 717a90, 7173f0's own tests). */
    {   const LONG64 untagged=pchairs[2].untagged;void *name=NULL;test_pchair_count=0;
        assert(pslot_enter(2));
        test_pchair_tagged[0]=0;assert(pfslot_chair(2,0));test_pchair_tagged[0]=1;
        memcpy(test_pchair_record[0]+PFSLOT_RECORD_NAME,&name,8);assert(pfslot_chair(2,0));
        name=test_pchair_name[0];memcpy(test_pchair_record[0]+PFSLOT_RECORD_NAME,&name,8);
        test_pchair_other_class=1;assert(pfslot_chair(2,0));test_pchair_other_class=0;
        assert(pslot_leave(2) && !test_pchair_count && pchairs[2].untagged==untagged+3 && pchairs[2].posted==1 && !pchair_failed);}
    /* A character whose config name the trigger table does not know (a short name, in its own buffer): the default. */
    {   void *m=test_pchair_macros[1];memcpy(test_pslot3_p[0]+PCHAIR_MACRO,&m,8);test_pchair_count=0;
        assert(pslot_enter(2) && pfslot_chair(2,0) && pslot_leave(2) && test_pchair_count==2 && test_pchair_rows[0].trigger==PCHAIR_CONFIG_DEFAULT);
        m=test_pslot_macro;memcpy(test_pslot3_p[0]+PCHAIR_MACRO,&m,8);}
    /* Fail closed (stop 226): outside the slot's scope, and an allocation that fails (nothing posted). */
    pnat_mode=PNAT_MODE_STOCK;
    {   test_pchair_count=0;
        assert(!pfslot_chair(3,0) && pchair_failed && !strcmp(pchair_why,"chair_scope") && pnat_failed && pilot_stop_reason()==PCHAIR_STOP_REASON);
        pchair_failed=0;pchair_why="-";pchair_stage="live";pnat_failed=0;pnat_fail_stage=NULL;
        test_pchair_no_memory=1;
        assert(pslot_enter(3) && !pfslot_chair(3,0) && pslot_leave(3) && pchair_failed && !strcmp(pchair_why,"chair_allocate") && !strcmp(pchair_stage,"failed") &&
               pilot_stop_reason()==PCHAIR_STOP_REASON && !test_pchair_count);
        {char row[4096];assert(pfslot_status(row,sizeof(row)) && strstr(row," chair=failed/") && strstr(row," chair_why=chair_allocate"));}
        test_pchair_no_memory=0;pchair_failed=0;pchair_why="-";pchair_stage="live";pnat_failed=0;pnat_fail_stage=NULL;}
    pnat_mode=saved_mode;
    /* A joiner: its save holds slot 2's chair, its rebuild of the seat posts nothing and counts as the slot's seat; its next
     * seat (a respawn's) is played as the session's second seat is. */
    {   pchair_armed=0;assert(pchair_arm_using() && !pchairs[2].posted && !pchairs[2].seats);
        assert(pslot_release(2));test_pchair_count=0;
        pchair_hold=1;assert(pslot_seat(2));pchair_hold=0;   /* as lockstep_pilot_join.h's pjoin_rebuild_slot */
        assert(!test_pchair_count && pchairs[2].skipped==1 && pchairs[2].seats==1 && !pchairs[2].posted && pchair_digest==PCHAIR_DOMAIN);
        assert(pslot_release(2) && pslot_seat(2) && test_pchair_count==2 && !test_pchair_rows[0].instant[0] && pchairs[2].posted==1 && !pchairs[2].instant);
        {char row[4096];assert(pfslot_status(row,sizeof(row)) && strstr(row," chair=live/1/0/1/") && strstr(row," chair2=1/0/1/0/11 chair3=0/0/0/0/0"));}}
    /* The digest: the same steps, the same digest (another event pool, the same value); a later seat of slot 3 another. */
    {   const uint64_t a=test_pchair_session(take,0);test_pchair_allocs=7;
        const uint64_t b=test_pchair_session(take,0),c=test_pchair_session(take,1);
        assert(a==b && a!=c && a!=PCHAIR_DOMAIN);}
    assert(pslot_release_all() && !pslots[2].seated && !pslots[3].seated);
    pchair_armed=0;pchair_failed=0;pchair_hold=0;memset(pchairs,0,sizeof(pchairs));pchair_stage="off";pchair_why="-";pchair_fn=saved_chair;
    pfslot_armed=0;memset(pfslots,0,sizeof(pfslots));pfslot_fn=saved_fn;pfslot_ship_docked_test=saved_docked;
    test_pslot3_ship_vt[PSLOT_IS_CLASS]=NULL;for(unsigned i=0;i<2;i++) memset(test_pslot3_room[i]+0x70,0,8);
    memset(&pilot_slots[2],0,sizeof(pilot_slots[2]));*(int*)(region+PFSLOT_GETUP_TAG)=0;cvt[3]=saved_c_is;cvt[20]=saved_c_next;
    vps_create=test_pslot_factory;pnat_fn.controller=test_pslot_controller_of;stats.issued=saved_issued;stats.slot=saved_slot;
    printf("pilot chair: slots 2 and 3 in two ships, each chair in its own scope with its own config, first seats instant, one ship one player, "
           "later seats and docked and flying sit-downs played, untagged/nameless/other class nothing, stop 226 outside the scope and without memory, "
           "a joiner's rebuild held, the digest PASS\n");
}

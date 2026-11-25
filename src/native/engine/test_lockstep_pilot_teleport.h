/* lockstep_pilot_teleport.h, synthetic (X4's handler, its 55d2a0, the camera setter, the room's own answer and the bodies'
 * 3d8420 are stood in for; the move itself is checked live: the live-check recipe in the commit). On the three-slot fixtures
 * of test_pilot_slots (client 3): slots 2 and 3 walk out of two fighters docked under a war transport; X4 moves each to the
 * transport's bridge in its own scope (no ConnectTo, no camera, the room notices and the teleport event for MD), both in
 * one tick; slot 1 and every case that is not a walking slot's go to X4 or are held; the fail-safe names X4's connect and
 * its camera (stop 222, also from a failed leave), the leave's check is named, and the restore gives this client its camera
 * back and takes P_k off with X4's call, or keeps it where X4 put it; the pins on X4 9.00 in test_pilot_teleport_image. */
static __attribute__((aligned(16))) unsigned char test_ptele_bridge[0x400],test_ptele_carrier[0x400],test_ptele_dock[0x400],
    test_ptele_fpc[2][0x3c0],test_ptele_kids[0x20],test_ptele_camera[0x40],test_ptele_own_camera[0x40];
static void *test_ptele_bridge_vt[600],*test_ptele_carrier_vt[600],*test_ptele_dock_vt[600];
static void *test_ptele_bucket[4],*test_ptele_listed[2];
static unsigned char test_ptele_bridge_is(void *self,int cls) {(void)self;return cls==0x54 || cls==0x19 || cls==0x10;}
static unsigned char test_ptele_carrier_is(void *self,int cls) {(void)self;return cls==0x49 || cls==0x70 || cls==0x75 || cls==0x6f;}
static unsigned char test_ptele_dock_is(void *self,int cls) {(void)self;return cls==0x54;}
static unsigned char test_ptele_ship_is(void *self,int cls) {(void)self;return cls==0x49 || cls==0x70 || cls==0x75;}
typedef struct {void *self,*target,*room,*place,*singleton,*parent;double time;unsigned char a,b,c;unsigned scope;} test_ptele_call;
static test_ptele_call test_ptele_x4_calls[8],test_ptele_room_calls[8];
static unsigned test_ptele_x4_count,test_ptele_room_count;
static void test_ptele_x4(void *self,void *target,double time,void *room,void *place,unsigned char control,unsigned char posted,unsigned char bare) {
    assert(test_ptele_x4_count<8);void *parent=NULL;memcpy(&parent,(unsigned char*)self+0x70,8);
    test_ptele_x4_calls[test_ptele_x4_count++]=(test_ptele_call){self,target,room,place,test_pslot_singleton(),parent,time,control,posted,bare,pstk_scope_slot};
}
static void test_ptele_room(void *self,void *target,double time,void *room,void *extra,unsigned char a,unsigned char b) {
    assert(test_ptele_room_count<8);void *parent=NULL;memcpy(&parent,(unsigned char*)self+0x70,8);
    test_ptele_room_calls[test_ptele_room_count++]=(test_ptele_call){self,target,room,extra,test_pslot_singleton(),parent,time,a,b,0,pstk_scope_slot};
}
typedef struct {void *player;unsigned vtable;uint64_t first,second;unsigned char flag;unsigned scope;} test_ptele_post;
static test_ptele_post test_ptele_posts[16];
static unsigned test_ptele_post_count;
static void test_ptele_event(void *player,unsigned vtable,uint64_t first,uint64_t second) {
    assert(test_ptele_post_count<16);
    test_ptele_posts[test_ptele_post_count++]=(test_ptele_post){player,vtable,first,second,pvehk_event_test_flag,pstk_scope_slot};
    pvehk_event_test_flag=0xee;
}
static unsigned test_ptele_set_cameras;
static void test_ptele_set_camera(void *camera) {++test_ptele_set_cameras;memcpy(test_pnat_universe+PFBODY_CAMERAS,&camera,8);}
static unsigned char test_ptele_room_answer=1;
static unsigned char test_ptele_room_ok(void *room) {(void)room;return test_ptele_room_answer;}
/* X4 holds `child` in `parent`'s child table (one bucket), or nothing when child is NULL. */
static void test_ptele_list(unsigned char *parent,void *child) {
    void *kids=test_ptele_kids,*b=test_ptele_bucket,*e=(unsigned char*)test_ptele_bucket+0x20;
    memset(test_ptele_kids,0,sizeof(test_ptele_kids));memset(test_ptele_bucket,0,sizeof(test_ptele_bucket));
    test_ptele_listed[0]=child;test_ptele_bucket[0]=&test_ptele_listed[0];test_ptele_bucket[1]=child?(void*)&test_ptele_listed[1]:(void*)&test_ptele_listed[0];
    memcpy(test_ptele_kids+8,&b,8);memcpy(test_ptele_kids+0x10,&e,8);memcpy(parent+PSLOT_KIDS_TABLE,&kids,8);
}
/* The bodies' 3d8420 for the restore's take-off: off the parent and out of the child table, held for the call. */
static unsigned test_ptele_takeoffs;
static unsigned char *test_ptele_takeoff_singleton;
static void test_ptele_connect(void *component,const void *filter,void *target,void *extra) {
    assert(!target && !extra && filter==test_pfbody_filter_object && *(int32_t*)((unsigned char*)component+PFBODY_HOLD)>=1);
    ++test_ptele_takeoffs;test_ptele_takeoff_singleton=test_pslot_singleton();
    memset((unsigned char*)component+0x70,0,8);test_ptele_bucket[1]=test_ptele_bucket[0];
}
static void test_ptele_dispatch(void *self,void *target,void *room,void *place,unsigned char control,unsigned char posted,unsigned char bare) {
    void *entry=NULL;memcpy(&entry,image+PTELE_ENTRY,8);   /* as TeleportEvent::Dispatch calls the listener's slot 76 */
    ((ptele_fn)entry)(self,target,100.0,room,place,control,posted,bare);
}
/* The board's fixtures: the ship each slot left (stored in the war transport, as the fighter of rk9-suit), its cockpit, the
 * registry's answer, the suit's kill, 70eee0 / d81ec0 for P_2 and P_3, X4's entry as the death module calls it. */
static __attribute__((aligned(16))) unsigned char test_ptele_home[2][0x500],test_ptele_home_room[2][0x400];
static void *test_ptele_fpc_vt[40];
static uint64_t test_ptele_ids[5];static unsigned char *test_ptele_objects[5];
static void *test_ptele_find(void *map,uint64_t runtime,unsigned flags) {
    assert(map==test_ptele_ids && flags==4);
    for(unsigned i=0;i<5;i++) if(test_ptele_ids[i]==runtime) return test_ptele_objects[i];
    return NULL;
}
/* A ship just claimed (the claim chain): its own cockpit; the slot's faction owns it once claimed; docked here. */
static __attribute__((aligned(16))) unsigned char test_ptele_claim[0x500],test_ptele_claim_room[0x400];
static void *test_ptele_claim_vt[1100];
static int test_ptele_claimed;
static int test_ptele_owner(unsigned char *ship,unsigned char *seat) {(void)seat;return ship!=test_ptele_claim || test_ptele_claimed;}
static int test_ptele_claim_docked(const unsigned char *ship) {return ship==test_ptele_claim;}
static unsigned test_ptele_kills;static void *test_ptele_killed[4];
static void test_ptele_kill(void *component,unsigned reason,void *killer,double now) {
    assert(reason==PDEATH_KILL_REMOVED && !killer && now==100.0 && test_ptele_kills<4);test_ptele_killed[test_ptele_kills++]=component;
}
static unsigned char test_ptele_fpc_is(void *self,int cls) {(void)self;return cls==PFSLOT_FPC_CLASS;}
static void *test_ptele_fpc_next(void *self) {(void)self;return NULL;}
static void test_ptele_make_fpc(void *player) {
    const int i=test_pslot3_of(test_pslot_singleton());
    assert(i>=0 && player==test_pslot3_p[i] && pstk_scope_slot==(unsigned)i+2);
    void *now=NULL;memcpy(&now,(unsigned char*)player+0x200,8);
    if(now) return;
    void *vt=test_ptele_fpc_vt,*fpc=test_ptele_fpc[i];memset(test_ptele_fpc[i],0,sizeof(test_ptele_fpc[i]));memcpy(test_ptele_fpc[i],&vt,8);
    memcpy((unsigned char*)player+0x200,&fpc,8);
}
static void test_ptele_pose(void *self,float *out,double time,unsigned char overwrite) {
    assert(time==100.0 && overwrite==1);((unsigned char*)self)[PFSLOT_FPC_READY]=1;memset(out,0,64);out[4]=out[9]=out[14]=1.0f;
}
static unsigned test_ptele_entered;static unsigned test_ptele_entered_scope;static void *test_ptele_entered_singleton;
static void test_ptele_enter(void *player,void *suit,void *room,void *fourth,unsigned char fifth) {
    (void)player;(void)suit;(void)room;(void)fourth;(void)fifth;
    ++test_ptele_entered;test_ptele_entered_scope=pstk_scope_slot;test_ptele_entered_singleton=test_pslot_singleton();
}
/* The undock key's X4 calls: 715330's answer, TeleportTo and the notice as the module calls them. */
static int test_ptele_may_answer=1;static unsigned test_ptele_mays,test_ptele_notices;
static struct {void *player,*target;unsigned char a,b,c,d,last;double time;void *room,*offset;unsigned scope;void *singleton;} test_ptele_tps[4];
static unsigned test_ptele_tp_count;
static int test_ptele_may(void *player,void *target,unsigned char control,unsigned char one) {
    (void)player;(void)target;assert(!control && one==1);++test_ptele_mays;return test_ptele_may_answer;
}
static unsigned char test_ptele_teleport(void *player,void *target,unsigned char a,unsigned char b,unsigned char c,unsigned char d,double time,void *room,
                                         void *offset,unsigned char last) {
    assert(test_ptele_tp_count<4);
    test_ptele_tps[test_ptele_tp_count++]=(__typeof__(test_ptele_tps[0])){player,target,a,b,c,d,last,time,room,offset,pstk_scope_slot,test_pslot_singleton()};
    return 1;
}
static void test_ptele_notice(void *owner,unsigned action,unsigned kind,unsigned what,void *data) {
    assert(action==0xd3u && kind==8u && what==0x4du && data==(void*)(pnat_base+PTELE_NOTICE_DATA));(void)owner;++test_ptele_notices;
}
/* X4's answer at the window's first seat (rk10): slot 2's fighter docked (a continue of a save made at a pad), slot 3's not. */
static int test_ptele_first_docked=1;
static int test_ptele_seat_docked(const unsigned char *ship) {return test_ptele_first_docked && ship==test_pslot3_ship[0];}
static void test_pilot_teleport_slots(unsigned char *region,unsigned char *p1) {
    const uint64_t saved_issued=stats.issued,saved_start=pilot_start,saved_stop=pilot_stop;const unsigned saved_slot=stats.slot,saved_mode=pnat_mode,saved_mask=pfbody_mask;
    const int saved_bodies=pfbody_armed,saved_events=pvehk_events_on,saved_death=pdeath_armed;
    const pfbody_connect_fn saved_connect=pfbody_connect;const void *const saved_filter=pfbody_filter;
    unsigned char *saved_cams[2];memcpy(saved_cams,test_pnat_universe+PFBODY_CAMERAS,16);
    unsigned char *saved_p1_camera=NULL;memcpy(&saved_p1_camera,p1+PFBODY_CAMERA,8);
    test_pslot_reset();test_pslot3_made=0;test_pslot3_row_count=0;pview_slot_player=NULL;pdeath_armed=0;pfbody_mask=0;
    void **cvt=(void**)(region+PSLOT_VTABLE);cvt[49]=(void*)test_pslot3_assist;cvt[50]=(void*)test_pslot3_mode;
    void *const saved_c_is=cvt[3],*const saved_c_next=cvt[20];cvt[3]=(void*)test_pchair_c_is;cvt[20]=(void*)test_pchair_c_next;
    vps_create=test_pslot3_factory;pnat_fn.controller=test_pslot3_controller_of;
    pslot_fn.take=test_pslot3_take;pslot_fn.release=test_pslot3_release;pslot_fn.update=test_pslot3_update;
    test_pslot3_ship_vt[PSLOT_TAKE]=(void*)test_pslot3_take;test_pslot3_ship_vt[PSLOT_RELEASE]=(void*)test_pslot3_release;
    test_pslot3_ship_vt[PSLOT_IS_CLASS]=(void*)test_ptele_ship_is;test_pslot3_room_vt[PSLOT_IS_CLASS]=(void*)test_pslot3_is_room;
    for(unsigned i=0;i<2;i++) {
        void *svt=test_pslot3_ship_vt,*rvt=test_pslot3_room_vt,*room=test_pslot3_room[i],*ship=test_pslot3_ship[i];
        memset(test_pslot3_ship[i],0,0x500);memcpy(test_pslot3_ship[i],&svt,8);memcpy(test_pslot3_ship[i]+PSLOT_ROOM,&room,8);test_pslot3_ship[i][0xd1]=1;
        memset(test_pslot3_room[i],0,0x400);memcpy(test_pslot3_room[i],&rvt,8);memcpy(test_pslot3_room[i]+0x70,&ship,8);
        pilot_slots[i+1].resolved=1;pilot_slots[i+1].object=test_pslot3_ship[i];pilot_slots[i+1].player=0;pilot_slots[i+1].dead=0;
    }
    pilot_slot_count=3;stats.slot=3;pnat_mode=PNAT_MODE_STOCK;
    const uint64_t take=pilot_start-PILOT_SUPPRESS_LEAD;pilot_stop=take+100;
    assert(pslot_arm_using(7) && pslot_mask==6 && pslot_construct_all(take) && test_pslot3_made==2);
    pfslot_armed=1;stats.issued=take-1;
    /* The window's first seat (take-1) of a ship that is docked (rk10: a continue of rk9's save, player 2's fighter on an
     * NPC transport's pad; X4 makes no controller for its take, 161 take_controller on both clients): the docked seat, no
     * take, C_k's vtable ready; slot 3's ship in space: taken as before. Once X4 says slot 2's ship is out, the pass takes
     * its controls under that vtable. */
    {   int (*const docked_was)(const unsigned char*)=pfslot_ship_docked_test;const LONG64 docked_seats=pslot_docked_seats;
        pfslot_ship_docked_test=test_ptele_seat_docked;test_ptele_first_docked=1;
        assert(pslot_seat_all(take) && pslots[2].seated && pslots[3].seated && test_pslot_singleton()==p1 && !pstk_scope_slot);
        assert(pslots[2].docked && pslots[2].dock_checked && !pslots[2].controller && !*(void**)(test_pslot3_ship[0]+0x200) &&
               pslots[2].vt.slots[0]==pslot_thunks[0] && pslot_docked_seats==docked_seats+1);
        assert(!pslots[3].docked && pslots[3].controller==test_pslot3_c[1]);
        assert(pfslot_undocked() && pslots[2].docked);   /* still docked: nothing */
        test_ptele_first_docked=0;
        assert(pfslot_undocked() && !pslots[2].docked && pslots[2].controller==test_pslot3_c[0] &&
               *(void**)test_pslot3_c[0]==(void*)&pslots[2].vt.slots[0] && test_pslot_singleton()==p1 && !pstk_scope_slot);
        {char row[4096];assert(pfslot_status(row,sizeof(row)) && strstr(row," docked_seats="));}
        pfslot_ship_docked_test=docked_was;
    }
    unsigned char *const P2=test_pslot3_p[0],*const P3=test_pslot3_p[1],*const bridge=test_ptele_bridge;
    /* The war transport (ID 0x3ca0) and its bridge (a cockpit, in the world, ID 0x22fcc2); each fighter under the transport's
     * dock room (0x4400); the fighters' IDs and their cockpits'. */
    {   void *vt=test_ptele_bridge_vt,*carrier=test_ptele_carrier,*dock=test_ptele_dock;
        memset(test_ptele_bridge,0,sizeof(test_ptele_bridge));memset(test_ptele_carrier,0,sizeof(test_ptele_carrier));memset(test_ptele_dock,0,sizeof(test_ptele_dock));
        test_ptele_bridge_vt[PSLOT_IS_CLASS]=(void*)test_ptele_bridge_is;memcpy(bridge,&vt,8);*(uint64_t*)(bridge+8)=0x22fcc2u;bridge[PFBODY_PLACED]=1;
        memcpy(bridge+0x70,&carrier,8);
        vt=test_ptele_carrier_vt;test_ptele_carrier_vt[PSLOT_IS_CLASS]=(void*)test_ptele_carrier_is;memcpy(test_ptele_carrier,&vt,8);
        *(uint64_t*)(test_ptele_carrier+8)=0x3ca0u;
        vt=test_ptele_dock_vt;test_ptele_dock_vt[PSLOT_IS_CLASS]=(void*)test_ptele_dock_is;memcpy(test_ptele_dock,&vt,8);
        *(uint64_t*)(test_ptele_dock+8)=0x4400u;test_ptele_dock[PFBODY_PLACED]=1;memcpy(test_ptele_dock+0x70,&carrier,8);
        for(unsigned i=0;i<2;i++) {
            memcpy(test_pslot3_ship[i]+0x70,&dock,8);*(uint64_t*)(test_pslot3_ship[i]+8)=0x3d85bu+i;*(uint64_t*)(test_pslot3_room[i]+8)=0x3d862u+i;
        }}
    /* Both slots stood up (the seat's controller released in its scope, as the stand-up does) and walk, parked. */
    for(unsigned k=2;k<=3;k++) {
        pslot_slot *x=&pslots[k];pfslot_slot *f=&pfslots[k];
        assert(pslot_enter(k));
        assert(InterlockedCompareExchangePointer((void*volatile*)x->controller,pnat_base+PSLOT_VTABLE,&x->vt.slots[0])==(void*)&x->vt.slots[0]);
        test_pslot3_release(x->ship);x->controller=NULL;
        assert(pslot_leave(k));
        memset(test_ptele_fpc[k-2],0,sizeof(test_ptele_fpc[k-2]));x->walking=1;f->walking=1;f->fpc=test_ptele_fpc[k-2];
    }
    /* Arming: after the foot slots and the bodies, X4's functions there, the entry X4's own. */
    ptele_original=test_ptele_x4;ptele_room_ok_test=test_ptele_room_ok;ptele_room_handler=NULL;ptele_set_camera=NULL;
    {void *h=image+PTELE_HANDLER;memcpy(image+PTELE_ENTRY,&h,8);}
    pfslot_armed=0;assert(!ptele_arm_using() && !strcmp(pstk_arm_why,"teleport_without_foot_slots"));pfslot_armed=1;
    pfbody_armed=0;assert(!ptele_arm_using() && !strcmp(pstk_arm_why,"teleport_without_bodies"));pfbody_armed=1;
    assert(!ptele_arm_using() && !strcmp(pstk_arm_why,"teleport_functions") && !ptele_armed);
    ptele_room_handler=test_ptele_room;ptele_set_camera=test_ptele_set_camera;
    assert(ptele_arm_using() && ptele_armed && ptele_installed && ptele_arm_using() && pslot_moved_note==ptele_moved && ptele_fade_site);
    {void *entry=NULL;memcpy(&entry,image+PTELE_ENTRY,8);assert(entry==(void*)ptele_handler);}
    assert(ptele_pass(take) && !strcmp(ptele_stage,"live"));
    pvehk_events_on=1;pvehk_event_test=test_ptele_event;
    __attribute__((aligned(16))) float place[16]={12.5f,1.0f,-40.0f,0, 1,0,0,0, 0,1,0,0, 0,0,1,0},place3[16]={13.5f,1.0f,-40.0f,0, 1,0,0,0, 0,1,0,0, 0,0,1,0};
    stats.issued=take+5;
    /* Slot 2, delivered outside any scope: moved in its own scope (P_2 the singleton), its upward pointer and walk_room the
     * bridge, X4's 55d2a0 with the event's place (1, 1); no X4 handler, no camera, this client's cameras as they were;
     * MD: the object and the room deferred, then the teleport at once with X4's flag, all in scope 2. */
    {   test_ptele_x4_count=test_ptele_room_count=test_ptele_post_count=0;(void)ptele_take_rows();
        test_ptele_dispatch(P2,P2,bridge,place,0,1,0);
        pslot_slot *x=&pslots[2];const test_ptele_call *c=&test_ptele_room_calls[0];
        assert(*(void**)(P2+0x70)==bridge && x->walk_room==bridge && pslot_at(x)==bridge && !test_ptele_x4_count && test_ptele_room_count==1);
        assert(c->self==P2 && c->target==P2 && c->room==bridge && c->place==place && c->a==1 && c->b==1 && c->scope==2 && c->singleton==P2 &&
               c->parent==bridge && c->time==100.0);
        assert(!*(void**)(P2+PFBODY_CAMERA) && !memcmp(test_pnat_universe+PFBODY_CAMERAS,saved_cams,16) && !pstk_scope_slot && test_pslot_singleton()==p1);
        assert(test_ptele_post_count==3);
        assert(test_ptele_posts[0].vtable==PVEHK_EVENT_OBJECT && test_ptele_posts[0].first==0x3d85bu && test_ptele_posts[0].second==0x3ca0u && test_ptele_posts[0].player==P2);
        assert(test_ptele_posts[1].vtable==PVEHK_EVENT_ROOM && test_ptele_posts[1].first==0x4400u && test_ptele_posts[1].second==0x22fcc2u);
        assert(test_ptele_posts[2].vtable==PVEHK_EVENT_TELEPORT && test_ptele_posts[2].first==0x4400u && test_ptele_posts[2].second==0x22fcc2u &&
               test_ptele_posts[2].flag==1 && test_ptele_posts[2].scope==2 && test_ptele_posts[2].player==P2);
        assert(ptele_applied==1 && ptele_slots[2].applied==1 && ptele_slots[2].room==0x22fcc2u && ptele_digest!=PTELE_DOMAIN);
        const char *rows=ptele_take_rows();
        assert(rows && strstr(rows,"slot=2 tick=") && strstr(rows," room=22fcc2 from=4400 at=12.500,1.000,-40.000 flags=0/1/0 applied") && !ptele_take_rows());
        assert(pslot_enter(2) && pslot_leave(2));   /* the scope's exit check takes the bridge as slot 2's room */
    }
    /* Slot 3 in the same tick, delivered inside its own scope (the event order's delivery): nothing nested, the scope still
     * open after it; slot 2 stays where it is. Both walk on the bridge. */
    {   test_ptele_room_count=test_ptele_post_count=0;const uint64_t digest=ptele_digest;
        assert(pslot_enter(3));
        test_ptele_dispatch(P3,P3,bridge,place3,0,1,0);
        assert(pstk_scope_slot==3 && test_pslot_singleton()==P3 && pslot_leave(3));
        assert(*(void**)(P3+0x70)==bridge && pslots[3].walk_room==bridge && test_ptele_room_count==1 && test_ptele_room_calls[0].scope==3 &&
               test_ptele_room_calls[0].singleton==P3 && test_ptele_room_calls[0].place==place3 && !test_ptele_x4_count);
        assert(*(void**)(P2+0x70)==bridge && pslots[2].walk_room==bridge && ptele_applied==2 && ptele_slots[3].applied==1 && ptele_digest!=digest);
        assert(test_ptele_post_count==3 && test_ptele_posts[0].player==P3 && test_ptele_posts[0].first==0x3d85cu && test_ptele_posts[2].scope==3);
        const char *rows=ptele_take_rows();assert(rows && strstr(rows,"slot=3 ") && strstr(rows," at=13.500,1.000,-40.000 ") && strstr(rows," applied"));
    }
    /* Slot 2's event while slot 3's scope is open: held (X4 asks again next frame), nothing moved. */
    {   test_ptele_room_count=0;
        assert(pslot_enter(3));test_ptele_dispatch(P2,P2,test_ptele_dock,place,0,1,0);assert(pslot_leave(3));
        assert(*(void**)(P2+0x70)==bridge && !test_ptele_room_count && !test_ptele_x4_count && ptele_held==1 && ptele_slots[2].held==1);
        const char *rows=ptele_take_rows();assert(rows && strstr(rows,"slot=2 ") && strstr(rows," held:scope"));
    }
    /* Slot 1 (X4's own player) and a target that is not the listener: X4's handler, as it was called. */
    {   test_ptele_dispatch(p1,p1,bridge,place,0,1,0);
        assert(test_ptele_x4_count==1 && test_ptele_x4_calls[0].self==p1 && test_ptele_x4_calls[0].room==bridge && test_ptele_x4_calls[0].place==place &&
               test_ptele_x4_calls[0].b==1 && !test_ptele_x4_calls[0].scope && test_ptele_x4_calls[0].time==100.0);
        test_ptele_dispatch(P2,p1,bridge,place,0,1,0);
        assert(test_ptele_x4_count==2 && test_ptele_x4_calls[1].self==P2 && test_ptele_x4_calls[1].target==p1 && ptele_passed==2 && !test_ptele_room_count);
        const char *rows=ptele_take_rows();assert(rows && strstr(rows,"slot=1 ") && strstr(rows," x4") && !strstr(rows,"slot=2"));
    }
    /* A room X4's handler refuses (flagged, or its own answer): X4's handler (its failure event), in slot 2's scope. */
    {   test_ptele_room_answer=0;test_ptele_dispatch(P2,P2,test_ptele_dock,place,0,1,0);test_ptele_room_answer=1;
        test_ptele_dock[PTELE_ROOM_FLAG]=1;test_ptele_dispatch(P2,P2,test_ptele_dock,place,0,1,0);test_ptele_dock[PTELE_ROOM_FLAG]=0;
        test_ptele_dispatch(P2,P2,NULL,place,0,1,0);   /* the dispatch found no room */
        assert(test_ptele_x4_count==5 && test_ptele_x4_calls[2].scope==2 && test_ptele_x4_calls[2].singleton==P2 && ptele_refused==3 &&
               *(void**)(P2+0x70)==bridge && !test_ptele_room_count && !pstk_scope_slot);
        const char *rows=ptele_take_rows();assert(rows && strstr(rows," refused"));
    }
    /* Held, never X4's handler on P_2: into control, without a place, a seated slot, its body in the tree, dead, its parent
     * not the seat's pointer. */
    {   const LONG64 held=ptele_held;
        test_ptele_dispatch(P2,P2,test_ptele_dock,place,1,1,0);
        test_ptele_dispatch(P2,P2,test_ptele_dock,place,0,1,1);
        test_ptele_dispatch(P2,P2,test_ptele_dock,NULL,0,1,0);
        pslots[2].walking=0;test_ptele_dispatch(P2,P2,test_ptele_dock,place,0,1,0);pslots[2].walking=1;
        pfbody_mask=2;test_ptele_dispatch(P2,P2,test_ptele_dock,place,0,1,0);pfbody_mask=0;
        pilot_slots[1].dead=1;test_ptele_dispatch(P2,P2,test_ptele_dock,place,0,1,0);pilot_slots[1].dead=0;
        {void *other=test_ptele_carrier;memcpy(P2+0x70,&other,8);test_ptele_dispatch(P2,P2,test_ptele_dock,place,0,1,0);other=bridge;memcpy(P2+0x70,&other,8);}
        test_ptele_dock[PFBODY_PLACED]=0;test_ptele_dispatch(P2,P2,test_ptele_dock,place,0,1,0);test_ptele_dock[PFBODY_PLACED]=1;
        assert(ptele_held==held+8 && test_ptele_x4_count==5 && !test_ptele_room_count && *(void**)(P2+0x70)==bridge && pslots[2].walk_room==bridge);
        const char *rows=ptele_take_rows();
        static const char *const why[]={"held:control","held:no_place","held:seated","held:body","held:dead","held:moved","held:room"};
        for(unsigned i=0;i<sizeof(why)/sizeof(*why);i++) assert(rows && strstr(rows,why[i]));
        /* a full ring of rows drops and says so */
        for(unsigned i=0;i<PTELE_ROWS+2;i++) test_ptele_dispatch(P2,P2,test_ptele_dock,place,1,1,0);
        rows=ptele_take_rows();assert(rows && strstr(rows,"dropped=2") && !ptele_take_rows());
    }
    {char row[4096];assert(ptele_status(row,sizeof(row)) && strstr(row," teleport=live/1/2/") && strstr(row," teleport_why=- teleport_slot=0 x4=0/0/0") &&
                           strstr(row," tp2=1/") && strstr(row," tp3=1/0/22fcc2"));}
    /* The way back: GET_UP on the bridge sits slot 2 down in its fighter, which the carrier holds (its bay's lift took it
     * into storage: no dock connection, no docking controller): the docked seat, its controls not taken (live 24 Nov: 185
     * slot_take_controller). A ship with no container above it is not inside one. Then up again and onto the bridge. */
    {   pslot_slot *x=&pslots[2];const LONG64 inside=pfslot_dock_inside,sits=pfslot_dock_sits;
        int (*const docked_test)(const unsigned char*)=pfslot_ship_docked_test;pfslot_ship_docked_test=NULL;   /* X4's answer, not a test's */
        assert(pfslot_ship_inside(x->ship) && !pfslot_ship_inside(test_ptele_carrier));
        test_ptele_post_count=0;
        assert(pslot_enter(2) && pfslot_sit_down(2) && pslot_leave(2));
        assert(!x->walking && !pfslots[2].walking && x->docked && !x->controller && *(void**)(P2+0x70)==x->room && !x->walk_room &&
               pfslot_dock_inside==inside+1 && pfslot_dock_sits==sits+1 && !*(void**)(x->ship+0x200) && test_pslot_singleton()==p1);
        {char row[4096];assert(pfslot_status(row,sizeof(row)) && strstr(row," docked=2/") && strstr(row," dock_inside="));}
        pfslot_ship_docked_test=docked_test;
        x->docked=0;x->walking=1;pfslots[2].walking=1;test_ptele_dispatch(P2,P2,bridge,place,0,1,0);
        assert(*(void**)(P2+0x70)==bridge && x->walk_room==bridge);(void)ptele_take_rows();
    }
    /* The board (crash rk9-suit, 24 Nov). Slots 2 and 3 in their suits (here the fighters play the suits: seated, at the
     * controls), each suit docked at the war transport, an NPC's ship; the ship each left is a fighter stored in that
     * transport. X4's entry (0x710390) runs for each in its own scope; its event names the bridge with X4's place: noted in
     * the dispatch, nothing moved. The death module's pass boards each: the suit's seat released, the suit killed, seated
     * WITHOUT a take in the ship it left (never the controls of it, nor of the transport), on foot on the bridge at X4's place
     * (its own scope); MD: object and room (deferred), the teleport at once, the suit's controls left. */
    {   const pfslot_fns fn_was=pfslot_fn;void *const kill_was=test_pslot3_ship_vt[PDEATH_KILL_SLOT];
        const pvehk_enter_fn enter_was=pvehk_enter_original;
        void *fvt=test_ptele_fpc_vt;(void)fvt;test_ptele_fpc_vt[3]=(void*)test_ptele_fpc_is;test_ptele_fpc_vt[20]=(void*)test_ptele_fpc_next;
        pfslot_fn.make_fpc=test_ptele_make_fpc;pfslot_fn.pose=test_ptele_pose;
        test_pslot3_ship_vt[PDEATH_KILL_SLOT]=(void*)test_ptele_kill;test_ptele_kills=0;
        for(unsigned i=0;i<2;i++) {   /* the ships they left: under the transport's dock room, each with its cockpit */
            void *svt=test_pslot3_ship_vt,*rvt=test_pslot3_room_vt,*room=test_ptele_home_room[i],*ship=test_ptele_home[i],*dock=test_ptele_dock;
            memset(test_ptele_home[i],0,0x500);memcpy(test_ptele_home[i],&svt,8);memcpy(test_ptele_home[i]+PSLOT_ROOM,&room,8);test_ptele_home[i][PFBODY_PLACED]=1;
            memcpy(test_ptele_home[i]+0x70,&dock,8);*(uint64_t*)(test_ptele_home[i]+8)=0x7001u+i;
            memset(test_ptele_home_room[i],0,0x400);memcpy(test_ptele_home_room[i],&rvt,8);memcpy(test_ptele_home_room[i]+0x70,&ship,8);
            *(uint64_t*)(test_ptele_home_room[i]+8)=0x7011u+i;
            test_ptele_ids[i]=0x7001u+i;test_ptele_objects[i]=test_ptele_home[i];
            test_ptele_ids[2+i]=0x3d85bu+i;test_ptele_objects[2+i]=test_pslot3_ship[i];
        }
        pdeath_find=(pilot_find_fn)test_ptele_find;pdeath_map=test_ptele_ids;pdeath_player=NULL;pdeath_armed=1;pfire_armed=0;
        for(unsigned k=2;k<=3;k++) {   /* into the suit: off the bridge, seated at the suit's controls */
            pslot_slot *x=&pslots[k];pfslot_slot *f=&pfslots[k];pdeath_slot *d=&pdeath_slots[k-1];
            assert(pslot_release(k) && !x->seated);f->walking=0;
            pilot_slots[k-1].resolved=1;pilot_slots[k-1].object=test_pslot3_ship[k-2];pilot_slots[k-1].runtime=0x3d85bu+(k-2);pilot_slots[k-1].player=0;
            assert(pslot_seat(k) && x->seated && x->controller==test_pslot3_c[k-2] && *(void**)(x->player+0x70)==test_pslot3_room[k-2]);
            memset(d,0,sizeof(*d));d->out=2;d->home=0x7001u+(k-2);d->suit=0x3d85bu+(k-2);
        }
        assert(ptele_board_on() && pvehk_home_ship(2)==test_ptele_home[0] && pvehk_home_ship(3)==test_ptele_home[1] && !pvehk_home_ship(1));
        /* X4's entry for P_2 from outside any scope (the pass's poll), for P_3 from inside its own (its Move): X4's own, in the
         * slot's scope; X4's own player's passes as it is. The ship it left gone: not run. */
        {   test_ptele_entered=0;pvehk_enter_original=test_ptele_enter;
            pvehk_enter(P2,test_pslot3_ship[0],NULL,NULL,0);
            assert(test_ptele_entered==1 && test_ptele_entered_scope==2 && test_ptele_entered_singleton==P2 && !pstk_scope_slot && pvehk_enters_x4>=1);
            assert(pslot_enter(3));pvehk_enter(P3,test_pslot3_ship[1],NULL,NULL,0);
            assert(test_ptele_entered==2 && test_ptele_entered_scope==3 && pstk_scope_slot==3 && pslot_leave(3));
            pvehk_enter(p1,NULL,NULL,NULL,0);assert(test_ptele_entered==3 && !test_ptele_entered_scope);
            const LONG64 none=pvehk_board_no_home;test_ptele_home[0][PDEATH_LIFECYCLE]=1;
            pvehk_enter(P2,test_pslot3_ship[0],NULL,NULL,0);assert(test_ptele_entered==3 && pvehk_board_no_home==none+1);
            test_ptele_dispatch(P2,P2,bridge,place,0,1,0);assert(!ptele_slots[2].board);   /* held: no_home */
            test_ptele_home[0][PDEATH_LIFECYCLE]=0;pvehk_enter_original=enter_was;
            const char *rows=ptele_take_rows();assert(rows && strstr(rows,"held:no_home"));
        }
        /* A ship X4 lets the suit into that is another player's (here slot 3's suit, for slot 2): no way in, the suit stays
         * (another player's ship is refused, as the claim's seat refuses it); X4's entry is not run. */
        {   const LONG64 others=pvehk_board_others;const unsigned entered=test_ptele_entered;
            pvehk_enter_original=test_ptele_enter;
            pslot_slot *x3=&pslots[3];unsigned char *const kept_ship=x3->ship;
            x3->ship=test_ptele_home[1];   /* slot 3 seated in the ship slot 2's suit would go into */
            pvehk_enter(P2,test_pslot3_ship[0],test_ptele_home_room[1],NULL,0);
            assert(test_ptele_entered==entered && pvehk_board_others==others+1);
            x3->ship=kept_ship;pvehk_enter_original=enter_was;
        }
        /* The events: noted, nothing moved, nothing of X4's handler. */
        test_ptele_room_count=test_ptele_x4_count=test_ptele_post_count=0;
        test_ptele_dispatch(P2,P2,bridge,place,0,1,0);
        assert(pslot_enter(3));test_ptele_dispatch(P3,P3,bridge,place3,0,1,0);assert(pslot_leave(3));
        assert(ptele_slots[2].board && ptele_slots[3].board && ptele_board_noted==2 && !test_ptele_room_count && !test_ptele_x4_count && !test_ptele_post_count);
        assert(*(void**)(P2+0x70)==test_pslot3_room[0] && *(void**)(P3+0x70)==test_pslot3_room[1] && pslots[2].controller && pslots[3].controller);
        {const char *rows=ptele_take_rows();assert(rows && strstr(rows,"slot=2 ") && strstr(rows,"slot=3 ") && strstr(rows," board") && !strstr(rows,"applied"));}
        /* Held, the suit's: into control, without a place. */
        test_ptele_dispatch(P2,P2,bridge,place,1,1,0);test_ptele_dispatch(P2,P2,bridge,place,0,1,1);
        {const char *rows=ptele_take_rows();assert(rows && strstr(rows,"held:control") && strstr(rows,"held:no_place"));}
        /* The pass: slot 2, then slot 3. */
        for(unsigned k=2;k<=3;k++) {
            pslot_slot *x=&pslots[k];pfslot_slot *f=&pfslots[k];pdeath_slot *d=&pdeath_slots[k-1];
            unsigned char *const P=test_pslot3_p[k-2],*const home=test_ptele_home[k-2];const float *const at=k==2?place:place3;
            test_ptele_room_count=test_ptele_post_count=0;const unsigned kills=test_ptele_kills;const LONG64 done=ptele_board_done;
            d->arrive=1;   /* an old-way arrival is no way home here */
            assert(pvehk_pass(k,take+5) && !pstk_scope_slot && test_pslot_singleton()==p1);
            assert(x->seated && x->ship==home && !x->controller && !x->docked && x->walking && f->walking && x->walk_room==bridge && pslot_at(x)==bridge &&
                   *(void**)(P+0x70)==bridge && x->room==test_ptele_home_room[k-2] && pilot_slots[k-1].runtime==0x7001u+(k-2) && pilot_slots[k-1].object==home);
            assert(!*(void**)(home+0x200) && !*(void**)(test_ptele_carrier+0x200) && !*(void**)(test_pslot3_ship[k-2]+0x200));   /* no controls taken */
            assert(test_ptele_kills==kills+1 && test_ptele_killed[kills]==test_pslot3_ship[k-2] && ptele_board_done==done+1 && !ptele_slots[k].board);
            assert(!d->out && !d->home && !d->suit && !d->arrive && f->fpc==test_ptele_fpc[k-2] && !f->fpc[PFSLOT_FPC_SEAT] && P[PFOOT_PLAYER_FLAG]==1 &&
                   *(int*)(P+PFSLOT_POSITION)==*(int*)(pnat_base+PFSLOT_POSITION_NONE));
            const test_ptele_call *c=&test_ptele_room_calls[0];
            assert(test_ptele_room_count==1 && c->self==P && c->room==bridge && c->scope==k && c->singleton==P && c->parent==bridge && c->a==1 && c->b==1 &&
                   !memcmp(c->place,at,64));
            assert(test_ptele_post_count==4 && test_ptele_posts[0].vtable==PVEHK_EVENT_OBJECT && test_ptele_posts[0].first==0x3d85bu+(k-2) &&
                   test_ptele_posts[0].second==0x3ca0u && test_ptele_posts[1].vtable==PVEHK_EVENT_ROOM && test_ptele_posts[1].first==0x3d862u+(k-2) &&
                   test_ptele_posts[1].second==0x22fcc2u && test_ptele_posts[2].vtable==PVEHK_EVENT_TELEPORT && test_ptele_posts[2].first==0x3d862u+(k-2) &&
                   test_ptele_posts[2].second==0x22fcc2u && test_ptele_posts[2].flag==1 && !test_ptele_posts[2].scope &&   /* from the pass: the post opens the scope */
                   test_ptele_posts[3].vtable==PVEHK_EVENT_CONTROL_OFF && test_ptele_posts[3].first==0x3d85bu+(k-2) && test_ptele_posts[3].player==P);
            assert(pslot_enter(k) && pslot_leave(k));   /* the scope's exit check takes the bridge as its room */
        }
        {const char *e=pdeath_take_events();assert(e && strstr(e,"board:2") && strstr(e,"board:3"));}
        {char row[4096];assert(ptele_status(row,sizeof(row)) && strstr(row," board=2/2/0"));}
        {char row[1024];assert(pdeath_status(row,sizeof(row)) && strstr(row," enter=") );}
        /* The claim chain's seat: slot 2 on foot in a ship it just claimed (X4 put it there as above), the ship it left
         * elsewhere. Unclaimed, an NPC's: nothing (no seat for it there). Claimed, its faction's: the seat changes in the
         * pass outside every scope: the walk ended, seated in the claimed ship (docked: the docked seat, no take), the slot's
         * ship that one now. Slot 3 then in that ship: refused (slot 2's ship). */
        {   void *cvt=test_ptele_claim_vt,*crvt=test_ptele_bridge_vt,*croom=test_ptele_claim_room,*cship=test_ptele_claim;
            int (*const docked_was)(const unsigned char*)=pfslot_ship_docked_test;
            memset(test_ptele_claim,0,sizeof(test_ptele_claim));memset(test_ptele_claim_room,0,sizeof(test_ptele_claim_room));
            test_ptele_claim_vt[PSLOT_IS_CLASS]=(void*)test_ptele_carrier_is;test_ptele_claim_vt[PSLOT_TAKE]=(void*)test_pslot3_take;
            test_ptele_claim_vt[PSLOT_RELEASE]=(void*)test_pslot3_release;
            memcpy(test_ptele_claim,&cvt,8);*(uint64_t*)(test_ptele_claim+8)=0x9001u;memcpy(test_ptele_claim+PSLOT_ROOM,&croom,8);test_ptele_claim[PFBODY_PLACED]=1;
            memcpy(test_ptele_claim_room,&crvt,8);*(uint64_t*)(test_ptele_claim_room+8)=0x9011u;memcpy(test_ptele_claim_room+0x70,&cship,8);
            test_ptele_claim_room[PFBODY_PLACED]=1;
            test_ptele_ids[4]=0x9001u;test_ptele_objects[4]=test_ptele_claim;
            pfslot_owner_test=test_ptele_owner;pfslot_ship_docked_test=test_ptele_claim_docked;test_ptele_claimed=0;
            for(unsigned k=2;k<=3;k++) {*(void**)(pslots[k].player+0x70)=test_ptele_claim_room;pslots[k].walk_room=test_ptele_claim_room;}
            const LONG64 foreign=pfslot_foreign,reseats=pfslot_reseats;
            assert(pfslot_ship_here(2)==test_ptele_claim && !pfslot_may_sit(2,test_ptele_claim));
            assert(pslot_enter(2) && pfslot_sit_down(2) && pslot_leave(2) && pslots[2].walking && !pfslots[2].reseat && pfslot_foreign==foreign+1);
            test_ptele_claimed=1;
            assert(pslot_enter(2) && pfslot_sit_down(2) && pslot_leave(2) && pslots[2].walking && pfslots[2].reseat==test_ptele_claim);
            assert(pfslot_reseat(2) && !pfslots[2].reseat && pfslot_reseats==reseats+1);
            pslot_slot *x=&pslots[2];
            assert(x->seated && x->ship==test_ptele_claim && x->docked && !x->controller && !x->walking && !pfslots[2].walking && !x->walk_room &&
                   *(void**)(P2+0x70)==test_ptele_claim_room && pilot_slots[1].runtime==0x9001u && pilot_slots[1].object==test_ptele_claim &&
                   !*(void**)(test_ptele_claim+0x200) && test_pslot_singleton()==p1 && !pstk_scope_slot);
            assert(pfslot_ship_here(3)==test_ptele_claim && !pfslot_may_sit(3,test_ptele_claim));   /* slot 2's ship now */
            assert(pslot_enter(3) && pfslot_sit_down(3) && pslot_leave(3) && pslots[3].walking && !pfslots[3].reseat && pfslot_foreign==foreign+2);
            {char row[4096];assert(pfslot_status(row,sizeof(row)) && strstr(row," reseat="));}
            /* back for the blocks below: slot 2 on foot on the bridge again (its seat the fighter again below), slot 3 too */
            assert(pslot_release(2) && !pslots[2].seated);
            pilot_slots[1].resolved=1;pilot_slots[1].object=test_ptele_home[0];pilot_slots[1].runtime=0x7001u;pilot_slots[1].renewed=1;
            pslot_seat_untaken=1;assert(pslot_seat(2) && pslots[2].docked);pslot_seat_untaken=0;
            assert(pslot_enter(2) && pfslot_stand(2,100.0,0,NULL) && pslot_leave(2));
            pslots[2].docked=0;
            for(unsigned k=2;k<=3;k++) {*(void**)(pslots[k].player+0x70)=bridge;pslots[k].walk_room=bridge;}
            pfslot_owner_test=NULL;pfslot_ship_docked_test=docked_was;test_ptele_ids[4]=0;test_ptele_objects[4]=NULL;
            pfslot_reseats=reseats;pfslot_foreign=foreign;
        }
        /* Nothing due: the pass does nothing more (X4 asks again where it still wants to). */
        assert(pvehk_pass(2,take+6) && pslots[2].walking && pslots[2].ship==test_ptele_home[0]);
        /* GET_UP on the bridge: slot 2 sits down in the ship it left, stored in the transport: the docked seat, no take. */
        {   int (*const docked_test)(const unsigned char*)=pfslot_ship_docked_test;pfslot_ship_docked_test=NULL;
            assert(pslot_enter(2) && pfslot_sit_down(2) && pslot_leave(2));
            assert(pslots[2].docked && !pslots[2].controller && !pslots[2].walking && *(void**)(P2+0x70)==test_ptele_home_room[0] && !*(void**)(test_ptele_home[0]+0x200));
            pfslot_ship_docked_test=docked_test;
        }
        /* Back to the fixture's state for the blocks below: both seated in their fighters again, walking on the bridge. */
        for(unsigned k=2;k<=3;k++) {
            pslot_slot *x=&pslots[k];pfslot_slot *f=&pfslots[k];
            assert(pslot_release(k) && !x->seated);f->walking=0;memset(x->player+0x200,0,8);
            pilot_slots[k-1].resolved=1;pilot_slots[k-1].object=test_pslot3_ship[k-2];pilot_slots[k-1].runtime=0;pilot_slots[k-1].renewed=0;
            assert(pslot_seat(k) && x->controller==test_pslot3_c[k-2]);
            assert(pslot_enter(k));
            assert(InterlockedCompareExchangePointer((void*volatile*)x->controller,pnat_base+PSLOT_VTABLE,&x->vt.slots[0])==(void*)&x->vt.slots[0]);
            test_pslot3_release(x->ship);x->controller=NULL;
            assert(pslot_leave(k));
            memset(test_ptele_fpc[k-2],0,sizeof(test_ptele_fpc[k-2]));x->walking=1;f->walking=1;f->fpc=test_ptele_fpc[k-2];
            *(void**)(x->player+0x70)=bridge;x->walk_room=bridge;
        }
        /* Shift+D (UNDOCK) on the bridge: each slot's own fighter is docked under the transport it stands in, X4 may take it
         * there (715330): X4's TeleportTo into the fighter, in the slot's scope, on foot (0, 1, 0, 1, no room, no offset);
         * the notice on this client's own slot's UI only (client 3). DOCK_ACTION on foot: nothing. A fighter elsewhere, or
         * X4's no: nothing moves; a running conversation: nothing. No suit any more. */
        {   const ptele_teleport_fn tp_was=ptele_teleport_to;const ptele_may_fn may_was=ptele_may;const ptele_notice_fn notice_was=ptele_notice;
            const unsigned own_was=pstk_capture_slot;
            ptele_teleport_to=test_ptele_teleport;ptele_may=test_ptele_may;ptele_notice=test_ptele_notice;pstk_capture_slot=3;
            test_ptele_tp_count=test_ptele_mays=test_ptele_notices=0;test_ptele_may_answer=1;pdeath_armed=1;
            assert(pvehk_home_key(2,0xd3) && pvehk_home_key(3,0xd3));   /* from the pass (no scope): opened for each */
            assert(test_ptele_tp_count==2 && test_ptele_mays==2 && test_ptele_notices==1 && !pstk_scope_slot && test_pslot_singleton()==p1);
            for(unsigned i=0;i<2;i++)
                assert(test_ptele_tps[i].player==test_pslot3_p[i] && test_ptele_tps[i].target==test_pslot3_ship[i] && !test_ptele_tps[i].a &&
                       test_ptele_tps[i].b==1 && !test_ptele_tps[i].c && test_ptele_tps[i].d==1 && !test_ptele_tps[i].room && !test_ptele_tps[i].offset &&
                       !test_ptele_tps[i].last && test_ptele_tps[i].scope==i+2 && test_ptele_tps[i].singleton==test_pslot3_p[i] && test_ptele_tps[i].time==100.0);
            assert(pslot_enter(2) && pvehk_home_key(2,0xd3) && pstk_scope_slot==2 && pslot_leave(2) && test_ptele_tp_count==3 && test_ptele_tps[2].scope==2);
            assert(pvehk_home_key(2,0xaf) && test_ptele_tp_count==3 && !pdeath_slots[1].out);   /* DOCK_ACTION on foot: nothing, no suit */
            test_ptele_may_answer=2;assert(pvehk_home_key(2,0xd3) && test_ptele_tp_count==3 && ptele_keys_refused==1);test_ptele_may_answer=1;
            {void *elsewhere=test_ptele_carrier;memcpy(test_pslot3_ship[0]+0x70,&elsewhere,8);}   /* under the transport, not in its dock room: still under it */
            assert(pvehk_home_key(2,0xd3) && test_ptele_tp_count==4);
            memset(test_pslot3_ship[0]+0x70,0,8);assert(pvehk_home_key(2,0xd3) && test_ptele_tp_count==4 && ptele_keys_none==1);   /* in space */
            {void *dock=test_ptele_dock;memcpy(test_pslot3_ship[0]+0x70,&dock,8);}
            {   static unsigned char talk[0x200];void *c=talk;memset(talk,0,sizeof(talk));*(int*)(talk+0x1a0)=2;memcpy(pnat_base+PFSLOT_CONVERSATION,&c,8);
                assert(pvehk_home_key(3,0xd3) && test_ptele_tp_count==4 && ptele_keys_busy==1);memset(pnat_base+PFSLOT_CONVERSATION,0,8);}
            pslots[2].walking=0;assert(!pvehk_home_key(2,0xd3) && test_ptele_tp_count==4);pslots[2].walking=1;   /* seated: X4's own handler */
            {char row[4096];assert(ptele_status(row,sizeof(row)) && strstr(row," undock=7/4/1/1/1"));}
            ptele_teleport_to=tp_was;ptele_may=may_was;ptele_notice=notice_was;pstk_capture_slot=own_was;
            ptele_keys=ptele_keys_moved=ptele_keys_none=ptele_keys_refused=ptele_keys_busy=0;
        }
        memset(pdeath_slots,0,sizeof(pdeath_slots));pdeath_armed=0;pdeath_find=NULL;pdeath_map=NULL;
        pfslot_fn=fn_was;test_pslot3_ship_vt[PDEATH_KILL_SLOT]=kill_was;
        ptele_board_noted=ptele_board_done=ptele_board_gone=0;(void)ptele_take_rows();
    }
    /* The fail-safe in the pass: X4 gave P_2 a camera (and made it this client's), or holds P_2 in the bridge's child table:
     * stop 222 with the why and the slot. */
    {   void *camera=test_ptele_camera;memcpy(P2+PFBODY_CAMERA,&camera,8);
        assert(!ptele_pass(take+6) && ptele_failed && !strcmp(ptele_why,"slot_camera") && ptele_failed_slot==2 && pnat_failed && pilot_stop_reason()==222u);
        memset(P2+PFBODY_CAMERA,0,8);ptele_failed=0;ptele_why="-";ptele_failed_slot=0;ptele_stage="live";pnat_failed=0;pnat_fail_stage=NULL;
        assert(ptele_pass(take+6));
        test_ptele_list(bridge,P2);
        assert(!ptele_pass(take+7) && !strcmp(ptele_why,"slot_connected") && ptele_failed_slot==2 && pilot_stop_reason()==222u);
        ptele_failed=0;ptele_why="-";ptele_failed_slot=0;ptele_stage="live";pnat_failed=0;pnat_fail_stage=NULL;
        pfbody_mask=2;assert(ptele_pass(take+7));pfbody_mask=0;   /* a body of ours in the tree is no sign */
        test_ptele_list(bridge,NULL);
    }
    /* The leave's check is named; a failed parent check where X4 holds P_2 stops with 222 at once, before drive_leave. */
    {   test_ptele_list(bridge,P2);pslots[2].walk_room=NULL;   /* the seat's pointer is the cockpit, X4 put P_2 on the bridge */
        assert(pslot_enter(2) && !pslot_leave(2) && !strcmp(pslot_leave_last,"parent") && pslot_leave_slot==2 &&
               pslot_leave_parent_id==0x22fcc2u && pslot_leave_at_id==0x3d862u && !pstk_scope_slot && test_pslot_singleton()==p1);
        assert(ptele_failed && !strcmp(ptele_why,"slot_connected") && !strcmp(pnat_fail_stage,"slot_connected") && pilot_stop_reason()==222u);
        pslot_async("drive_leave");assert(!strcmp(pslot_why,"drive_leave:parent"));
        pfslot_async("slot_update_leave");assert(!strcmp(pfslot_why,"slot_update_leave:parent") && pilot_stop_reason()==222u);
        pfslot_async("slot_update_leave");assert(!strcmp(pfslot_why,"slot_update_leave:parent"));   /* the same text again */
        {char row[8192];assert(pslot_status(row,sizeof(row)) && strstr(row," leave=") && strstr(row,"/parent/2/22fcc2/3d862 unseat_kept=0"));}
        {char row[1024];assert(pilot_why_text(row,sizeof(row)) && strstr(row," teleport=failed teleport_why=slot_connected teleport_slot=2") &&
                               strstr(row," leave=parent") && strstr(row,"slots_why=drive_leave:parent"));}
        assert(pslot_enter(2));pslot_slot *x=&pslots[2];x->walk_room=bridge;assert(pslot_leave(2) && !pslot_leave_last);   /* passes: none named */
        pslot_why="-";pslot_failed=0;pslot_stage="seated";pfslot_failed=0;pfslot_why="-";pfslot_stage="walking";pfoot_failed=0;pfoot_why="-";
        ptele_failed=0;ptele_why="-";ptele_failed_slot=0;ptele_stage="live";pnat_failed=0;pnat_fail_stage=NULL;
    }
    /* The restore of an abnormal stop. Slot 2: X4 connected it to the bridge and made it a camera that is this client's: the
     * cameras back to this client's player's (X4's setter, then +0x3d0), P_2 off the bridge by X4's call in its scope and
     * held, the seat's pointer back; its release then ends the seat as usual. Slot 3: X4 holds it, but it cannot be taken off
     * (no call): kept where X4 put it, and its release does not write +0x70 = NULL. */
    {   void *camera=test_ptele_camera,*own=test_ptele_own_camera;memcpy(P2+PFBODY_CAMERA,&camera,8);memcpy(p1+PFBODY_CAMERA,&own,8);
        memcpy(test_pnat_universe+PFBODY_CAMERAS,&camera,8);memcpy(test_pnat_universe+PFBODY_CAMERAS+8,&camera,8);
        test_ptele_list(bridge,P2);pfbody_connect=test_ptele_connect;pfbody_filter=test_pfbody_filter_object;test_ptele_takeoffs=0;test_ptele_set_cameras=0;
        const int32_t hold=*(int32_t*)(P2+PFBODY_HOLD);
        ptele_restore();
        void *now[2];memcpy(now,test_pnat_universe+PFBODY_CAMERAS,16);
        assert(now[0]==own && now[1]==own && test_ptele_set_cameras==1 && ptele_cameras_back==1);
        assert(test_ptele_takeoffs==1 && test_ptele_takeoff_singleton==P2 && ptele_taken_off==1 && *(int32_t*)(P2+PFBODY_HOLD)==hold &&
               *(void**)(P2+0x70)==bridge && !pslot_listed(P2) && !pstk_scope_slot && test_pslot_singleton()==p1 && !ptele_installed);
        {void *entry=NULL;memcpy(&entry,image+PTELE_ENTRY,8);assert(entry==(void*)(image+PTELE_HANDLER));}
        const LONG64 kept=pslot_unseat_kept;
        assert(pslot_release(2) && !pslots[2].seated && !*(void**)(P2+0x70) && pslot_unseat_kept==kept);
        test_ptele_list(bridge,P3);pfbody_connect=NULL;
        ptele_restore();
        assert(ptele_kept==1 && test_ptele_takeoffs==1 && *(void**)(P3+0x70)==bridge && pslot_listed(P3));
        assert(pslot_release(3) && !pslots[3].seated && *(void**)(P3+0x70)==bridge && pslot_unseat_kept==kept+1);
        {char row[4096];assert(ptele_status(row,sizeof(row)) && strstr(row," x4=1/1/1"));}
        memset(P2+PFBODY_CAMERA,0,8);memset(P3+0x70,0,8);test_ptele_list(bridge,NULL);
    }
    /* stop+1: the entry X4's again (a no-op after the restore here), released. */
    assert(ptele_pass(pilot_stop+1) && !strcmp(ptele_stage,"released"));
    assert(pslot_release_all() && !pslots[2].seated && !pslots[3].seated);
    /* Reset everything this block set. */
    ptele_armed=0;ptele_installed=0;ptele_failed=0;ptele_why="-";ptele_stage="off";ptele_failed_slot=0;pslot_moved_note=NULL;ptele_fade_site=0;
    ptele_original=NULL;ptele_room_ok_test=NULL;ptele_room_handler=NULL;ptele_set_camera=NULL;memset(image+PTELE_ENTRY,0,8);
    ptele_applied=ptele_held=ptele_refused=ptele_passed=ptele_cameras_back=ptele_taken_off=ptele_kept=0;memset(ptele_slots,0,sizeof(ptele_slots));
    pvehk_event_test=NULL;pvehk_events_on=saved_events;pfbody_armed=saved_bodies;pfbody_connect=saved_connect;pfbody_filter=saved_filter;pfbody_mask=saved_mask;
    pdeath_armed=saved_death;pfslot_armed=0;memset(pfslots,0,sizeof(pfslots));pslot_leave_last=NULL;
    memcpy(test_pnat_universe+PFBODY_CAMERAS,saved_cams,16);memcpy(p1+PFBODY_CAMERA,&saved_p1_camera,8);
    test_pslot3_ship_vt[PSLOT_IS_CLASS]=NULL;
    for(unsigned i=0;i<2;i++) {memset(test_pslot3_room[i]+0x70,0,8);memset(test_pslot3_ship[i]+0x70,0,8);}
    memset(&pilot_slots[2],0,sizeof(pilot_slots[2]));cvt[3]=saved_c_is;cvt[20]=saved_c_next;
    vps_create=test_pslot_factory;pnat_fn.controller=test_pslot_controller_of;stats.issued=saved_issued;stats.slot=saved_slot;pnat_mode=saved_mode;
    pilot_start=saved_start;pilot_stop=saved_stop;
    printf("pilot teleport: slots 2 and 3 moved to the carrier's bridge in their own scopes in one tick (no ConnectTo, no camera, MD's "
           "notices), slot 1 and the rest X4's or held, stop 222 for X4's connect and camera (also from a failed leave), leave checks named, "
           "the restore's cameras and take-off, kept where X4 holds it; the board: both suits docked at the transport, X4's entry in each "
           "slot's scope, its event noted, then on foot on the bridge at X4's place, seated without a take in the stored fighter each left, "
           "the sit-down there the docked seat, another player's ship refused, a claimed ship's seat taken over in the pass; "
           "Shift+D on foot into each slot's own docked fighter by X4's TeleportTo PASS\n");
}
/* X4 9.00: the pins, the two vtable entries, and what the header says the code does around them. */
static void test_pilot_teleport_image(const char *path) {
    unsigned char *const mapped=test_pmath_map(path),*const saved=image;
    image=mapped;ptele_pins_checked=0;
    assert(ptele_pins());
    #define TPT_CALL(at) ((unsigned)((at)+5u+*(const int32_t*)(mapped+(at)+1)))
    #define TPT_RIP(at,len) ((unsigned)((at)+(len)+*(const int32_t*)(mapped+(at)+(len)-4)))
    /* The decision: rbx is zero from 0x711952 (xor ebx,ebx); the room and the offset are rbx, the fourth argument r8b 0. */
    assert(mapped[0x711952]==0x33 && mapped[0x711953]==0xdb);
    assert(!memcmp(mapped+0x7136b9,"\x48\x89\x5c\x24\x40\x48\x89\x5c\x24\x38",10) && !memcmp(mapped+0x7136d6,"\x45\x33\xc0",3));
    assert(mapped[0x713672]==0xe8 && TPT_CALL(0x713672)==0x74cd30u && mapped[0x7136df]==0xe8 && TPT_CALL(0x7136df)==0x7154b0u);
    /* ...7143c0's fourth argument is that byte, and its draw is in the branch the byte skips. */
    assert(!memcmp(mapped+0x7154fa,"\x41\x0f\xb6\xd8",4) && !memcmp(mapped+0x715b4b,"\x44\x0f\xb6\xcb",4) && TPT_CALL(0x715b5a)==0x7143c0u);
    assert(!memcmp(mapped+0x714463,"\x84\xdb\x0f\x84",4) && 0x71446bu+*(const int32_t*)(mapped+0x714467)==0x714587u && TPT_CALL(0x714508)==0x4512f0u &&
           0x714508u>0x71446bu && 0x714508u<0x714587u);
    /* The target's two tags (transporter, location_cockpit) and the volume's (bit 40): their names where X4 registers them. */
    assert(TPT_RIP(0x714591,6)==0x395cf34u && TPT_RIP(0x71459a,6)==0x395ccf4u);
    assert(TPT_RIP(0x8da924,6)==0x395cf34u && TPT_RIP(0x8da8d9,7)==0x29fcaa0u && !strcmp((const char*)mapped+0x29fcaa0u,"transporter"));
    assert(TPT_RIP(0x8d8404,6)==0x395ccf4u && TPT_RIP(0x8d83b9,7)==0x2b8ab78u && !strcmp((const char*)mapped+0x2b8ab78u,"location_cockpit"));
    assert(TPT_RIP(0x8daa2c,6)==0x395cf44u && TPT_RIP(0x8da9e1,7)==0x2b8b0d0u && !strcmp((const char*)mapped+0x2b8b0d0u,"trigger_invalid_character_position"));
    assert(TPT_RIP(0x88a998,6)==0x395cf44u && *(const uint64_t*)(mapped+0x88a9c5)==0x10000000000ull && *(const uint64_t*)(mapped+0x74cd76)==0x10000000000ull);
    /* The event's dispatch calls slot 76 (+0x260) with the flags +0x60..+0x62 and the place at +0x20. */
    assert(!memcmp(mapped+0x9b1fc8,"\x4c\x8b\xb0\x60\x02\x00\x00",7) && !memcmp(mapped+0x9b2008,"\x48\x8d\x47\x20",4) && !memcmp(mapped+0x9b2027,"\x41\xff\xd6",3));
    /* The fade: 0xfa7330 from 0x713c3f, its handle kept at [0x6cf1578]+0x1d0. */
    assert(TPT_CALL(0x713c3f)==PVEHK_FADE && TPT_RIP(0x713c11,7)==0x6cf1578u && !memcmp(mapped+0x713c44,"\x48\x89\x87\xd0\x01\x00\x00",7));
    /* UNDOCK on foot (71b190): the player faction's ships (g_PlayerFaction 0x38f4bd0), 715330 == 1, the notice (0xd3, 8, 0x4d),
     * TeleportTo(player, ship, 0, 1, 0, 1, now, NULL, NULL, 0). */
    assert(TPT_RIP(0x71b34c,7)==0x38f4bd0u && TPT_CALL(0x71b580)==PTELE_MAY_TELEPORT && !memcmp(mapped+0x71b585,"\x83\xf8\x01",3));
    assert(TPT_RIP(0x71b58e,7)==PTELE_NOTICE_DATA && TPT_RIP(0x71b5a9,7)==PTELE_NOTICE_OWNER && TPT_CALL(0x71b5b0)==PTELE_NOTICE &&
           !memcmp(mapped+0x71b59a,"\x41\xb9\x4d\x00\x00\x00\x45\x8d\x41\xbb\xba\xd3\x00\x00\x00",15));
    assert(!memcmp(mapped+0x71b5d4,"\xc6\x44\x24\x48\x00",5) && !memcmp(mapped+0x71b5f3,"\xc6\x44\x24\x28\x01\xc6\x44\x24\x20\x00\x41\xb1\x01\x45\x33\xc0",16) &&
           TPT_CALL(0x71b609)==PTELE_TELEPORT_TO);
    #undef TPT_CALL
    #undef TPT_RIP
    /* Another build: a changed byte of the decision refuses the arming. */
    {mapped[0x7136d6]^=1;ptele_pins_checked=0;assert(!ptele_pins() && pin_failed_rva==0x713600u);mapped[0x7136d6]^=1;}
    ptele_pins_checked=0;image=saved;
    printf("pinned native image pilot teleport verified (vfunc76 0x7052f0 at 0x2b32c08, the dispatch 0x9b1f80, TeleportTo's room and offset "
           "none, no draw, the bridge transporter's tags, bit 40 = trigger_invalid_character_position, the fade 0x713c3f, UNDOCK on foot 0x71b190)\n");
}

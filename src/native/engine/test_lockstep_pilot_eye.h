/* The presented eye (lockstep_pilot_eye.h): X4's composition, D from two seats, the tick's update with
 * its refusals, both poses of the canonical player in the scope and every byte back after it; the
 * fighter of see2 against the save's scout. The image test reads the facts the module relies on. */
static __attribute__((aligned(16))) unsigned char test_peye_p1[0x7000],test_peye_p2[0x7000],test_peye_ship1[0x500],test_peye_room1[0x100],
    test_peye_ship2[0x500],test_peye_room2[0x100],test_peye_fpc[0x100],test_peye_other_ctl[0x40],test_peye_station[0x100];
static __attribute__((aligned(16))) float test_peye_seat1[16],test_peye_seat2[16],test_peye_local[16],test_peye_published[16],test_peye_engine_block[16];
static void *test_peye_stock[1+PEYE_FPC_SLOTS],*test_peye_other_vt[2];
static unsigned test_peye_seats,test_peye_poses,test_peye_slot5;
static int test_peye_seat_found=1,test_peye_seat_fault;
static void *test_peye_room2_expected=test_peye_room2;
static double test_peye_time;
static uint64_t test_peye_rng;
static uint64_t *test_peye_rng_word(void) {return &test_peye_rng;}
static void test_peye_pose_set(float m[16],float x,float y,float z,float yaw) {
    float c=cosf(yaw),s=sinf(yaw);memset(m,0,16*sizeof(float));
    m[0]=x;m[1]=y;m[2]=z;m[4]=c;m[6]=-s;m[9]=1.0f;m[12]=s;m[14]=c;
}
static int test_peye_close(const float *a,const float *b,unsigned n,float eps) {
    for(unsigned i=0;i<n;i++) if(!(fabsf(a[i]-b[i])<=eps)) return 0;
    return 1;
}
/* X4's 0x75d110: the seat of the ship in the room its player is parented to. */
static unsigned char test_peye_seat(void *ship,float *out,void *room,const int *position) {
    ++test_peye_seats;++test_peye_rng;
    assert(*position==0x77);
    if(test_peye_seat_fault) {volatile int *nothing=NULL;*nothing=1;}
    if(ship==test_peye_ship1) {assert(room==test_peye_room1);memcpy(out,test_peye_seat1,64);}
    else {assert(ship==test_peye_ship2 && room==test_peye_room2_expected);memcpy(out,test_peye_seat2,64);}
    return (unsigned char)test_peye_seat_found;
}
/* FirstPersonController slot 6 as X4's: the local pose, or the pose given in its frame. */
static void test_peye_fpc_pose(void *self,float *out,double time,unsigned char overwrite) {
    assert(self==test_peye_fpc);++test_peye_poses;test_peye_time=time;
    if(time<0) {if(overwrite) peye_identity(out);return;}
    if(overwrite) memcpy(out,test_peye_local,64);
    else {float r[16];peye_compose(out,test_peye_local,r);memcpy(out,r,64);}
}
static void test_peye_slot5_fn(void *self) {assert(self==test_peye_fpc);++test_peye_slot5;}
static void test_peye_call6_at(float *out,unsigned char overwrite,double time) {
    void **vt=*(void***)test_peye_fpc;((void(*)(void*,float*,double,unsigned char))vt[PEYE_POSE_SLOT])(test_peye_fpc,out,time,overwrite);
}
static void test_peye_call6(float *out,unsigned char overwrite) {test_peye_call6_at(out,overwrite,2.5);}
static void test_pilot_eye(void) {
    peye_seat_fn saved_seat=peye_seat;peye_pose_fn saved_pose=peye_fpc_pose;void **saved_stock=peye_fpc_stock;int saved_ready=peye_vt_ready;
    unsigned char *saved_p1=pslot_p1;pilot_slot saved_slot0=pilot_slots[0];uint64_t *(*saved_rng)(void)=vps_rng_word;LONG64 saved_moved=pres_rng_moved;
    /* X4's composition: the child's pose in the parent's frame (row vectors), w lanes as its SSE code. */
    {   float child[16],parent[16],out[16];test_peye_pose_set(child,1,2,3,0);test_peye_pose_set(parent,10,0,0,1.57079632679f);
        peye_compose(child,parent,out);
        const float want[16]={10+3,2,-1,0, 0,0,-1,0, 0,1,0,0, 1,0,0,0};
        assert(test_peye_close(out,want,16,1e-5f));}
    /* D: the same seat is identity and moves nothing; D carries slot 1's seat onto slot k's, whatever the rotations. */
    {   float s1[16],sk[16],d[16],moved[16];test_peye_pose_set(s1,0,-0.0383f,-0.1493f,0);
        assert(!peye_delta_from(s1,s1,d));{float id[16];peye_identity(id);assert(!memcmp(d,id,64));}
        test_peye_pose_set(s1,0.3f,-0.2f,0.5f,0.7f);test_peye_pose_set(sk,-1.1f,0.4f,2.0f,-0.4f);
        assert(peye_delta_from(s1,sk,d));peye_compose(s1,d,moved);assert(test_peye_close(moved,sk,16,1e-5f));}
    /* see2: the save's scout 0ae79c (UI anchor - cockpit of ship_arg_s_scout_01) against the fighter 176ad5
     * (ship_arg_s_fighter_01): the eye 0.39 m forward and 3 cm down in the fighter's room. */
    {   float scout[16],fighter[16],d[16];
        test_peye_pose_set(scout,0.0f,-0.3505759f+0.3123242f,0.7214615f-0.8707749f,0);
        test_peye_pose_set(fighter,0.005998066f-1.40667e-05f,-0.5951678f+0.5245181f,7.036417f-6.798061f,0);
        assert(fabsf(scout[1]+0.03825f)<1e-4f && fabsf(scout[2]+0.1493f)<1e-4f);   /* the save's player offset */
        assert(peye_delta_from(scout,fighter,d) && fabsf(d[0]-0.006f)<1e-3f && fabsf(d[1]+0.0324f)<1e-3f && fabsf(d[2]-0.3877f)<1e-3f);}
    /* A seat that is not rigid is not one. */
    {float s[16];test_peye_pose_set(s,0,0,0,0);assert(peye_rigid(s));s[4]=2.0f;assert(!peye_rigid(s));s[4]=NAN;assert(!peye_rigid(s));}
    /* The fixtures: slot 1's player in its scout's cockpit room, slot 2 seated in a fighter's room. */
    memset(test_peye_p1,0,sizeof(test_peye_p1));memset(test_peye_p2,0,sizeof(test_peye_p2));
    {   unsigned char *room1=test_peye_room1,*room2=test_peye_room2,*fpc=test_peye_fpc;
        memcpy(test_peye_p1+PRES_PARENT,&room1,8);memcpy(test_peye_ship1+PSLOT_ROOM,&room1,8);memcpy(test_peye_ship2+PSLOT_ROOM,&room2,8);
        memcpy(test_peye_p1+PEYE_MOVEMENT,&fpc,8);}
    *(int*)(test_peye_p1+PEYE_POSITION)=0x77;*(int*)(test_peye_p2+PEYE_POSITION)=0x77;
    for(unsigned i=0;i<PEYE_FPC_SLOTS;i++) test_peye_stock[1+i]=(void*)test_pslot_c_other;
    test_peye_stock[0]=(void*)0xc01;test_peye_stock[1+PEYE_POSE_SLOT]=(void*)test_peye_fpc_pose;test_peye_stock[1+5]=(void*)test_peye_slot5_fn;
    {void *vt=&test_peye_stock[1];memcpy(test_peye_fpc,&vt,8);}
    test_peye_pose_set(test_peye_seat1,0.0f,-0.0383f,-0.1493f,0);
    test_peye_pose_set(test_peye_seat2,0.006f,-0.0707f,0.2384f,0);
    memcpy(test_peye_local,test_peye_seat1,64);             /* X4 holds the eye at slot 1's seat */
    memcpy(test_peye_published,test_peye_seat1,64);
    pslot_p1=test_peye_p1;memset(&pilot_slots[0],0,sizeof(pilot_slots[0]));pilot_slots[0].object=test_peye_ship1;
    pslot_slot s;memset(&s,0,sizeof(s));s.seated=1;s.ship=test_peye_ship2;s.room=test_peye_room2;s.player=test_peye_p2;
    vps_rng_word=test_peye_rng_word;test_peye_rng=0x55;pres_rng_moved=0;test_peye_seats=test_peye_poses=test_peye_slot5=0;
    peye_seat=NULL;peye_fpc_pose=NULL;peye_fpc_stock=NULL;peye_vt_ready=0;peye_reset();
    /* Without X4's functions: off, nothing taken. */
    assert(!peye_update(&s) && !strcmp(peye_stage,"off:functions") && !test_peye_seats);
    peye_seat=test_peye_seat;peye_fpc_pose=test_peye_fpc_pose;peye_fpc_stock=&test_peye_stock[1];
    /* Different seats: on, D taken outside any scope with the simulation's RNG word put back. */
    assert(peye_update(&s) && !strcmp(peye_stage,"on") && peye_on && test_peye_seats==2 && test_peye_rng==0x55 && pres_rng_moved==1);
    /* The vtable copy: the column object, X4's slots, slot 6 the eye's. */
    assert(peye_vt_ready && peye_vt[0]==(void*)0xc01 && peye_vt[1+PEYE_POSE_SLOT]==(void*)peye_pose && peye_vt[1+5]==(void*)test_peye_slot5_fn);
    assert(fabsf(peye_delta[2]-0.3877f)<1e-3f && peye_key[0]==test_peye_ship1 && peye_key[1]==test_peye_room1 && peye_key[2]==test_peye_ship2);
    /* The scope: live and published poses of the canonical player at slot 2's seat; nothing else of it moves. */
    static unsigned char before[sizeof(test_peye_p1)],fpc_before[sizeof(test_peye_fpc)];
    unsigned char *view=test_peye_p1+PRES_VIEW_CACHE+PRES_VIEW_STRIDE,*published=(unsigned char*)test_peye_published;
    memcpy(view+PEYE_VIEW_BLOCK,&published,8);
    memcpy(before,test_peye_p1,sizeof(before));memcpy(fpc_before,test_peye_fpc,sizeof(fpc_before));
    peye_enter(test_peye_p1,view,1,&s,test_peye_room1);InterlockedExchange(&pres_open,1);
    assert(peye_live && peye_published && *(void**)test_peye_fpc==(void*)&peye_vt[1]);
    {   float *block=NULL;memcpy(&block,view+PEYE_VIEW_BLOCK,8);
        assert(block==peye_block[1] && test_peye_close(block,test_peye_seat2,16,1e-5f));}
    {   float out[16],in[16],want[16];
        test_peye_call6(out,1);assert(test_peye_close(out,test_peye_seat2,16,1e-5f));            /* the eye X4 gives a player of slot 2's ship */
        test_peye_pose_set(in,0.1f,0.2f,0.3f,0.5f);memcpy(out,in,64);test_peye_call6(out,0);     /* a pose in the player's frame */
        peye_compose(in,test_peye_seat2,want);assert(test_peye_close(out,want,16,1e-5f));
        ((void(*)(void*))(*(void***)test_peye_fpc)[5])(test_peye_fpc);assert(test_peye_slot5==1);   /* other slots are X4's */
        test_peye_call6(out,1);
        /* A time before the start (X4: identity) is X4's own answer. */
        test_peye_call6_at(out,1,-1.0);assert(test_peye_time==-1.0);{float id[16];peye_identity(id);assert(!memcmp(out,id,64));}}
    assert(peye_poses==3);
    InterlockedExchange(&pres_open,0);
    {float out[16];test_peye_call6(out,1);assert(test_peye_close(out,test_peye_seat1,16,0) && peye_poses==3);}   /* outside: X4's own */
    InterlockedExchange(&pres_open,1);peye_leave();InterlockedExchange(&pres_open,0);
    assert(!memcmp(before,test_peye_p1,sizeof(before)) && !memcmp(fpc_before,test_peye_fpc,sizeof(fpc_before)) && !peye_lost && peye_scopes==1);
    /* A block the engine replaced in the scope stays the engine's (lost); a NULL block is identity with D. */
    {   unsigned char *engine=(unsigned char*)test_peye_engine_block,*none=NULL;
        peye_enter(test_peye_p1,view,1,&s,test_peye_room1);memcpy(view+PEYE_VIEW_BLOCK,&engine,8);peye_leave();
        assert(peye_lost==1 && !memcmp(view+PEYE_VIEW_BLOCK,&engine,8) && *(void**)test_peye_fpc==(void*)&test_peye_stock[1]);
        memcpy(view+PEYE_VIEW_BLOCK,&none,8);peye_enter(test_peye_p1,view,0,&s,test_peye_room1);
        float *block=NULL;memcpy(&block,view+PEYE_VIEW_BLOCK,8);float want[16];peye_identity(want);peye_compose(want,peye_delta,want);
        assert(block==peye_block[0] && test_peye_close(block,want,16,1e-6f));
        peye_leave();assert(!*(void**)(view+PEYE_VIEW_BLOCK));
        memcpy(view+PEYE_VIEW_BLOCK,&published,8);}
    /* Another movement controller: not wrapped, named, and the published pose left alone too (camera and HUD agree). */
    {   unsigned char *other=test_peye_other_ctl,*fpc=test_peye_fpc;void *vt=image+0x1234;memcpy(other,&vt,8);memcpy(test_peye_p1+PEYE_MOVEMENT,&other,8);
        peye_enter(test_peye_p1,view,1,&s,test_peye_room1);assert(!peye_live && !peye_published && peye_other==1 && peye_other_vtable==0x1234);
        assert(!memcmp(view+PEYE_VIEW_BLOCK,&published,8));
        peye_leave();assert(*(void**)other==vt);memcpy(test_peye_p1+PEYE_MOVEMENT,&fpc,8);}
    /* The scope of another seat than the one D was taken for: nothing. */
    {   peye_enter(test_peye_p1,view,1,&s,test_peye_room2);assert(!peye_live && !peye_published);peye_leave();}
    {   char row[256];assert(peye_status(row,sizeof(row)) && !strcmp(row," eye=on/3/3/1/0.006,-0.032,0.388 eye_controller=1234:1"));}
    /* The same model (scouts): same, D identity, a scope changes no byte. */
    memcpy(test_peye_seat2,test_peye_seat1,64);
    assert(!peye_update(&s) && !strcmp(peye_stage,"same") && !peye_on);
    peye_enter(test_peye_p1,view,1,&s,test_peye_room1);assert(!peye_live && !peye_published);peye_leave();
    assert(!memcmp(before,test_peye_p1,sizeof(before)) && !memcmp(fpc_before,test_peye_fpc,sizeof(fpc_before)));
    test_peye_pose_set(test_peye_seat2,0.006f,-0.0707f,0.2384f,0);
    /* Slot 1 on foot (lockstep_pilot_foot.h): its player stands in a station room. Slot 2 is presented at its own seat, X4's
     * w lanes, whatever slot 1's walking pose; one seat call (slot 2's), the RNG word put back; foot off: skipped as before. */
    {   unsigned char *station=test_peye_station,*room1=test_peye_room1;int saved_foot=pfoot_armed;
        memcpy(test_peye_p1+PRES_PARENT,&station,8);pfoot_armed=1;test_peye_seats=0;test_peye_rng=0x55;
        assert(peye_update(&s) && !strcmp(peye_stage,"seat") && peye_on && peye_seat_mode && test_peye_seats==1 && test_peye_rng==0x55 &&
               peye_key[1]==test_peye_station && peye_key[2]==test_peye_ship2 && peye_key[3]==test_peye_room2);
        {float id[16];peye_identity(id);assert(!memcmp(peye_delta,id,64));}
        float walking[16],saved_local[16];memcpy(saved_local,test_peye_local,64);
        test_peye_pose_set(walking,12.0f,1.7f,-30.0f,2.0f);walking[3]=0.25f;memcpy(test_peye_local,walking,64);
        peye_enter(test_peye_p1,view,1,&s,test_peye_station);InterlockedExchange(&pres_open,1);assert(peye_live && peye_published);
        {   float out[16],want[16];memcpy(want,test_peye_seat2,64);want[3]=0.25f;
            test_peye_call6(out,1);assert(test_peye_close(out,want,16,1e-6f));
            float *block=NULL;memcpy(&block,view+PEYE_VIEW_BLOCK,8);memcpy(want,test_peye_seat2,64);   /* the published pose's w lanes */
            for(unsigned i=0;i<4;i++) want[4*i+3]=test_peye_published[4*i+3];
            assert(block==peye_block[1] && test_peye_close(block,want,16,1e-6f));}
        InterlockedExchange(&pres_open,0);
        {float out[16];test_peye_call6(out,1);assert(test_peye_close(out,walking,16,0));}   /* outside: X4's own walking pose */
        InterlockedExchange(&pres_open,1);peye_leave();InterlockedExchange(&pres_open,0);
        assert(!peye_live && !peye_published && *(void**)test_peye_fpc==(void*)&test_peye_stock[1]);
        test_peye_seat_found=0;assert(!peye_update(&s) && !strcmp(peye_stage,"skip:no_seat") && !peye_seat_mode);test_peye_seat_found=1;
        pfoot_armed=0;assert(!peye_update(&s) && !strcmp(peye_stage,"skip:slot1_seat") && !peye_seat_mode);
        pfoot_armed=saved_foot;memcpy(test_peye_local,saved_local,64);memcpy(test_peye_p1+PRES_PARENT,&room1,8);
        assert(peye_update(&s) && !strcmp(peye_stage,"on") && !peye_seat_mode);}
    /* Refusals: slot 1's player not in its ship's room, no seat connection, not rigid, a fault in X4's call. */
    {   unsigned char *elsewhere=test_peye_room2;memcpy(test_peye_p1+PRES_PARENT,&elsewhere,8);
        assert(!peye_update(&s) && !strcmp(peye_stage,"skip:slot1_seat"));
        unsigned char *room1=test_peye_room1;memcpy(test_peye_p1+PRES_PARENT,&room1,8);}
    test_peye_seat_found=0;assert(!peye_update(&s) && !strcmp(peye_stage,"skip:no_seat"));test_peye_seat_found=1;
    test_peye_seat2[4]=3.0f;assert(!peye_update(&s) && !strcmp(peye_stage,"skip:not_rigid"));test_peye_seat2[4]=1.0f;
    test_peye_rng=0x55;test_peye_seat_fault=1;assert(!peye_update(&s) && !strcmp(peye_stage,"skip:fault") && test_peye_rng==0x55);test_peye_seat_fault=0;
    {float id[16];peye_identity(id);assert(!memcmp(peye_delta,id,64));}
    s.seated=0;assert(!peye_update(&s) && !strcmp(peye_stage,"skip:parent"));s.seated=1;
    assert(peye_update(&s) && peye_on);
    /* A roomless ship: the seat relative to the ship itself, the scope's parent then. */
    {   pslot_slot r=s;r.room=test_peye_ship2;r.roomless=1;test_peye_room2_expected=test_peye_ship2;test_peye_seats=0;
        assert(peye_update(&r) && peye_on && test_peye_seats==2 && peye_key[3]==test_peye_ship2);
        peye_enter(test_peye_p1,view,1,&r,test_peye_room1);assert(peye_live && peye_published);peye_leave();
        test_peye_room2_expected=test_peye_room2;}
    peye_reset();peye_seat=saved_seat;peye_fpc_pose=saved_pose;peye_fpc_stock=saved_stock;peye_vt_ready=saved_ready;
    pslot_p1=saved_p1;pilot_slots[0]=saved_slot0;vps_rng_word=saved_rng;pres_rng_moved=saved_moved;
    printf("pilot eye: slot k's seat from X4's own seat transform, live and published in the scope, every byte back\n");
}
/* In the slots flow (test_lockstep_pilot_slots.h, after its first UI frame): the eye through pres_enter and
 * pres_leave, the probe's pause and the replay's resume inside X4's UI entry; then every byte back. */
static unsigned test_peye_flow_seen;
static unsigned char test_peye_flow_seat(void *ship,float *out,void *room,const int *position) {
    (void)position;++test_peye_seats;
    if(ship==test_peye_ship1) {assert(room==(void*)test_pres_room1);memcpy(out,test_peye_seat1,64);}
    else {assert(ship==test_pslot_ship2 && room==test_pslot_room);memcpy(out,test_peye_seat2,64);}
    return 1;
}
static void test_peye_flow_ui(void) {
    float out[16],*block=NULL;
    test_peye_call6(out,1);assert(test_peye_close(out,test_peye_seat2,16,1e-5f));
    memcpy(&block,test_pnat_player+PRES_VIEW_CACHE+PRES_VIEW_STRIDE+PEYE_VIEW_BLOCK,8);
    assert(block==peye_block[1] && test_peye_close(block,test_peye_seat2,16,1e-5f));
    ++test_peye_flow_seen;
}
static void test_pilot_eye_flow(pslot_slot *s2,void (*ui_entry)(void*),uint64_t canonical) {
    unsigned char *p1=test_pnat_player,*view=p1+PRES_VIEW_CACHE+PRES_VIEW_STRIDE,*kept_ctl=NULL,*kept_block=NULL;
    pilot_slot kept_slot0=pilot_slots[0];
    memcpy(&kept_ctl,p1+PEYE_MOVEMENT,8);memcpy(&kept_block,view+PEYE_VIEW_BLOCK,8);
    for(unsigned i=0;i<PEYE_FPC_SLOTS;i++) test_peye_stock[1+i]=(void*)test_pslot_c_other;
    test_peye_stock[0]=(void*)0xc01;test_peye_stock[1+PEYE_POSE_SLOT]=(void*)test_peye_fpc_pose;
    {   void *vt=&test_peye_stock[1];unsigned char *fpc=test_peye_fpc,*published=(unsigned char*)test_peye_published,*room1=test_pres_room1;
        memcpy(test_peye_fpc,&vt,8);memcpy(p1+PEYE_MOVEMENT,&fpc,8);memcpy(view+PEYE_VIEW_BLOCK,&published,8);
        memcpy(test_peye_ship1+PSLOT_ROOM,&room1,8);}
    test_peye_pose_set(test_peye_seat1,0.0f,-0.0383f,-0.1493f,0);test_peye_pose_set(test_peye_seat2,0.006f,-0.0707f,0.2384f,0);
    memcpy(test_peye_local,test_peye_seat1,64);memcpy(test_peye_published,test_peye_seat1,64);
    pilot_slots[0].object=test_peye_ship1;
    peye_seat=test_peye_flow_seat;peye_fpc_pose=test_peye_fpc_pose;peye_fpc_stock=&test_peye_stock[1];peye_vt_ready=0;peye_reset();
    /* The pass's update: slot 1 in its room, D from both seats. */
    assert(peye_update(s2) && !strcmp(peye_stage,"on"));
    static unsigned char before[0x7000],fpc_before[sizeof(test_peye_fpc)];
    memcpy(before,p1,sizeof(before));memcpy(fpc_before,test_peye_fpc,sizeof(fpc_before));
    test_peye_flow_seen=0;test_pres_ui_hook=test_peye_flow_ui;ui_entry(test_pres_self);test_pres_ui_hook=NULL;
    assert(test_peye_flow_seen==1 && !pres_open && peye_scopes==3 && !peye_lost && !peye_live && !peye_published);
    assert(!memcmp(before,p1,sizeof(before)) && !memcmp(fpc_before,test_peye_fpc,sizeof(fpc_before)) && pslot_hash()==canonical);
    {char row[4096];assert(pres_status(row,sizeof(row)) && strstr(row," eye=on/3/1/0/0.006,-0.032,0.388 present_restored="));}
    /* Outside the scope X4's own pose. */
    {float out[16];test_peye_call6(out,1);assert(test_peye_close(out,test_peye_seat1,16,0));}
    memcpy(p1+PEYE_MOVEMENT,&kept_ctl,8);memcpy(view+PEYE_VIEW_BLOCK,&kept_block,8);pilot_slots[0]=kept_slot0;
    peye_reset();peye_seat=NULL;peye_fpc_pose=NULL;peye_fpc_stock=NULL;peye_vt_ready=0;
}
/* The facts on the pinned image: the seat transform asks the UI anchor filter, the player's controller class
 * and its slot 6, 716670 connects the player at the room's player slot. */
static void test_pilot_eye_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    unsigned count=nt.FileHeader.NumberOfSections;
    assert(fread(sections,sizeof(*sections),count,f)==count);
    static unsigned char bytes[0x500];
    assert(test_pnat_file_bytes(f,sections,count,PEYE_SEAT,PEYE_SEAT_SIZE,bytes) && hash_ok(bytes,PEYE_SEAT_SIZE,"5dda1ded31999c6c7984952e2fef76d8d458d3571186e2e78740e8a3f3180c57"));
    assert(test_pnat_file_bytes(f,sections,count,PEYE_FPC_POSE,PEYE_FPC_POSE_SIZE,bytes) && hash_ok(bytes,PEYE_FPC_POSE_SIZE,"b1214c17c25e0ce8060da52b5d4a69159dfd89d74975f6bffd3092ec5f77ae8c"));
    /* A static filter object's class, from its vtable's column object. */
    #define TEST_PEYE_CLASS(vt_va,name) do { uint64_t col_va=0;unsigned td=0;char text[64]; \
        assert(test_pnat_file_bytes(f,sections,count,(unsigned)((vt_va)-0x140000000ULL)-8,8,bytes));memcpy(&col_va,bytes,8); \
        assert(test_pnat_file_bytes(f,sections,count,(unsigned)(col_va-0x140000000ULL)+12,4,bytes));memcpy(&td,bytes,4); \
        assert(test_pnat_file_bytes(f,sections,count,td+16,sizeof(text),(unsigned char*)text));text[sizeof(text)-1]=0; \
        assert(!strcmp(text,name)); } while(0)
    /* 0x75d150: lea rdx,[the IsUIAnchor filter]; ecx = the control position; 0x884260 joins its group's filter. */
    {   int32_t rel;uint64_t vt;assert(test_pnat_file_bytes(f,sections,count,0x75d150,7,bytes) && bytes[0]==0x48 && bytes[1]==0x8d && bytes[2]==0x15);
        memcpy(&rel,bytes+3,4);unsigned filter=0x75d157u+(unsigned)rel;assert(filter==0x2f60618u);
        assert(test_pnat_file_bytes(f,sections,count,filter,8,bytes));memcpy(&vt,bytes,8);
        TEST_PEYE_CLASS(vt,".?AVTemplateConnectionFilterByIsUIAnchor@U@@");}
    /* 716670 connects the player through 0x3d8420 at the room's player slot. */
    {   int32_t rel;uint64_t vt;assert(test_pnat_file_bytes(f,sections,count,0x71673b,15,bytes) && bytes[0]==0x48 && bytes[1]==0x8d && bytes[2]==0x15 && bytes[10]==0xe8);
        memcpy(&rel,bytes+3,4);unsigned filter=0x716742u+(unsigned)rel;assert(filter==0x2f60e70u);
        memcpy(&rel,bytes+11,4);assert(0x71674au+(unsigned)rel==0x3d8420u);
        assert(test_pnat_file_bytes(f,sections,count,filter,8,bytes));memcpy(&vt,bytes,8);
        TEST_PEYE_CLASS(vt,".?AVTemplateConnectionFilterByIsPlayerSlot@U@@");}
    /* The player's controller class and its 49 slots, slot 6 the pose. */
    TEST_PEYE_CLASS(0x140000000ULL+PEYE_FPC_VTABLE,".?AVFirstPersonController@U@@");
    assert(test_pnat_file_bytes(f,sections,count,PEYE_FPC_VTABLE,8*(PEYE_FPC_SLOTS+1),bytes));
    {   uint64_t v;memcpy(&v,bytes+8*PEYE_POSE_SLOT,8);assert(v==0x140000000ULL+PEYE_FPC_POSE);
        for(unsigned i=0;i<PEYE_FPC_SLOTS;i++) {memcpy(&v,bytes+8*i,8);assert(v>0x140001000ULL && v<0x141c00000ULL);}
        memcpy(&v,bytes+8*PEYE_FPC_SLOTS,8);assert(v<0x140001000ULL || v>=0x141c00000ULL);}
    /* Positional::vfunc648 asks the movement controller at +0x200, slot 6 (+0x30). */
    assert(test_pnat_file_bytes(f,sections,count,0x7461d0,7,bytes) && !memcmp(bytes,"\x48\x8b\x89\x00\x02\x00\x00",7));
    assert(test_pnat_file_bytes(f,sections,count,0x746201,6,bytes) && !memcmp(bytes,"\x48\x8b\x01\xff\x50\x30",6));
    #undef TEST_PEYE_CLASS
    fclose(f);
    printf("pinned native image pilot eye verified (seat transform, UI anchor and player slot filters, FirstPersonController slot 6)\n");
}

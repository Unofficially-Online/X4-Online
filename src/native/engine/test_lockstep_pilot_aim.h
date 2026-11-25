/* lockstep_pilot_aim.h on synthetic memory (owned controllers, made-up transforms, stand-ins for X4's functions): the
 * replayed aim's rules. Whether X4's guns then turn is the live run's question (reports/LOCKSTEP-UI-MISSIONS-20261101.txt). */
static unsigned test_paim_original_calls;
static unsigned char test_paim_manual_value;
static _Alignas(16) unsigned char test_paim_ship[64],test_paim_space[64];
static unsigned char test_paim_original(void *controller,float *first,float *second,void *space,void *weapon,void *transform,float a7,float a8,
                                        unsigned char a9,unsigned char a10,unsigned char a11,unsigned char a12,unsigned char a13) {
    (void)controller;(void)first;(void)second;(void)space;(void)weapon;(void)transform;(void)a7;(void)a8;(void)a9;(void)a10;(void)a11;(void)a12;(void)a13;
    ++test_paim_original_calls;return 0;
}
static unsigned char test_paim_manual(void *self) {assert(!self);return test_paim_manual_value;}
/* The ship in the asked frame: at (10,20,30), its x along y, its y along z, its z along x. */
static void test_paim_transform(void *from,float *t,void *to,uint64_t flag) {
    assert(from==(void*)test_paim_ship && to==(void*)test_paim_space && !flag);
    memset(t,0,64);t[0]=10.0f;t[1]=20.0f;t[2]=30.0f;t[5]=1.0f;t[10]=1.0f;t[12]=1.0f;
}
#define TEST_PAIM_CALL(c) paim_aim(c,first,second,test_paim_space,NULL,NULL,0.0f,0.0f,0,0,0,0,0)
/* The assist: X4's aim function asks the controller's vtable slot 17 for the target, as 7e72b0 does. */
static void *test_paim_seen_target;
static void **test_paim_no_target(void *controller,void **out,void *weapon) {(void)controller;(void)weapon;*out=NULL;return out;}
static unsigned char test_paim_not_manual(void *controller) {(void)controller;return 0;}
/* What X4's aim saw in the lock's call: a10 (the evasion flag). */
static int test_paim_seen_a10=-1;
static unsigned char test_paim_original_asks(void *controller,float *first,float *second,void *space,void *weapon,void *transform,float a7,float a8,
                                             unsigned char a9,unsigned char a10,unsigned char a11,unsigned char a12,unsigned char a13) {
    (void)first;(void)second;(void)space;(void)transform;(void)a7;(void)a8;(void)a9;(void)a11;(void)a12;(void)a13;
    void *target=(void*)1;paim_target_get(controller,&target,weapon);test_paim_seen_target=target;
    test_paim_seen_a10=a10;
    ++test_paim_original_calls;return target!=NULL;
}
static float test_paim_one(void *controller) {(void)controller;return 1.0f;}
static float test_paim_range(void *gun) {(void)gun;return 2500.0f;}
static unsigned test_paim_own;
static unsigned test_paim_own_slot(void) {return test_paim_own;}
/* X4's dot predicate as a stand-in: it asks the gun's controller slot 16 from its own call site, as 81bb50 does. */
static int test_paim_dot_seen=-1;
static unsigned char test_paim_dot_predicate(void *weapon) {
    unsigned char *controller=NULL;memcpy(&controller,(unsigned char*)weapon+PFIRE_CONTROLLER,8);
    test_paim_dot_seen=controller?paim_player_answer(controller,(uintptr_t)(image+PAIM_DOT_ASK_RETURN)):0;
    return (unsigned char)test_paim_dot_seen;
}
/* A world for the cone, the lock and the track: objects at positions in the asked frame (and in the zone, the same
 * frame here), axes as the identity or turned (x along y, y along z, z along x). */
typedef struct {const void *object;float pos[3];int turned;} test_paim_pose;
static test_paim_pose test_paim_poses[16];
static unsigned test_paim_pose_count;
static _Alignas(16) unsigned char test_paim_zone[64];
static void test_paim_place(const void *object,float x,float y,float z,int turned) {
    unsigned i=0;
    while(i<test_paim_pose_count && test_paim_poses[i].object!=object) i++;
    assert(i<sizeof(test_paim_poses)/sizeof(*test_paim_poses));
    if(i==test_paim_pose_count) test_paim_pose_count++;
    test_paim_poses[i].object=object;test_paim_poses[i].pos[0]=x;test_paim_poses[i].pos[1]=y;test_paim_poses[i].pos[2]=z;test_paim_poses[i].turned=turned;
}
static void test_paim_world(void *from,float *t,void *to,uint64_t flag) {
    assert((to==(void*)test_paim_space || to==(void*)test_paim_zone) && !flag);
    for(unsigned i=0;i<test_paim_pose_count;i++) if(test_paim_poses[i].object==from) {
        memset(t,0,64);memcpy(t,test_paim_poses[i].pos,12);
        if(test_paim_poses[i].turned) {t[5]=1.0f;t[10]=1.0f;t[12]=1.0f;} else {t[4]=1.0f;t[9]=1.0f;t[14]=1.0f;}
        return;
    }
    assert(!"an object without a pose");
}
/* Targets: a vtable whose +0x1bf0 says "large", a macro whose class data hold the aim assist flag and whose aim targets
 * list is set or empty. */
static void *test_paim_vtable[0x1c00/8];
static unsigned char test_paim_large_value;
static unsigned char test_paim_large(void *self) {(void)self;return test_paim_large_value;}
static _Alignas(16) unsigned char test_paim_macro[16],test_paim_class_data[0x320],test_paim_macro_data[0x768];
static unsigned char *test_paim_class_of(void *macro) {assert(macro==(void*)test_paim_macro);return test_paim_class_data;}
static unsigned char *test_paim_macro_of(void *macro) {assert(macro==(void*)test_paim_macro);return test_paim_macro_data;}
static const float test_paim_centre[4]={0.0f,0.0f,0.0f,0.0f};
static void *test_paim_point_gun;
static const float *test_paim_point(void *target,void *gun) {(void)target;test_paim_point_gun=gun;return test_paim_centre;}
static float test_paim_distance2;
static int test_paim_distance_mode;
static float test_paim_distance(void *target,void *gun,void *zero,double now,int mode) {
    (void)target;(void)gun;assert(!zero && now==12.5);test_paim_distance_mode=mode;return test_paim_distance2;
}
static int test_paim_now(double *now) {*now=12.5;return 1;}
static _Alignas(16) unsigned char test_paim_config[0x2a0];
static void test_paim_target_init(unsigned char *object) {
    memset(object,0,0x80);void *vtable=test_paim_vtable,*macro=test_paim_macro;memcpy(object,&vtable,8);memcpy(object+0x60,&macro,8);
}
/* The slots' targets, by slot. */
static unsigned char *test_paim_targets[PILOT_SLOTS+1];
static unsigned char *test_paim_target_of(unsigned slot) {assert(slot>=1 && slot<=PILOT_SLOTS);return test_paim_targets[slot];}
static unsigned char *test_paim_zone_of(unsigned char *player) {(void)player;return test_paim_zone;}
static int test_paim_near(float a,float b,float tolerance) {return fabsf(a-b)<=tolerance;}
_Static_assert(INPUT_STOCK_VERSION==PSTK_VERSION,"the input container's stock version is the stock record's");
static void test_pilot_aim(void) {
    static unsigned char c1[0x100],c2[0x100],c3[0x100];
    pstk_fns saved_pstk=pstk_fn;pfire_slot saved_fire[PILOT_SLOTS];memcpy(saved_fire,pfire_slots,sizeof(saved_fire));
    pilot_slot saved_slots[4]={pilot_slots[0],pilot_slots[1],pilot_slots[2],pilot_slots[3]};const unsigned saved_count=pilot_slot_count,saved_pres_slot=pres_slot;
    pstk_slot_frame saved_frames[3]={pstk_slot_frames[1],pstk_slot_frames[2],pstk_slot_frames[3]};const uint64_t saved_issued=stats.issued,saved_aim_tick=pstk_aim_tick;
    const DWORD saved_thread=ui_thread;const int saved_armed=paim_armed,saved_valid=pstk_aim_valid_replay,saved_focus=pstk_focus_forced;
    const int saved_paused=pres_paused;const LONG saved_open=pres_open;
    float saved_replay[3];memcpy(saved_replay,pstk_aim_replay,12);const unsigned char saved_steer=pstk_aim_steer_replay;
    const pstk_aim_fn saved_original=paim_original;const paim_fns saved_fn=paim_fn;
    unsigned char *saved_players[5]={NULL,pslots[1].player,pslots[2].player,pslots[3].player,pslots[4].player};
    unsigned char *saved_base=pnat_base;unsigned char saved_config_word[8],saved_mode_words[8];
    int (*saved_now)(double *)=pfire_now;const unsigned saved_delay=pilot_delay;
    const int saved_opred_armed=opred_armed,saved_opred_mode=opred_mode,saved_opred_others=opred_others;
    opred_slot *saved_opred=malloc(3*sizeof(opred_slot));assert(saved_opred);memcpy(saved_opred,&opred_slots[1],3*sizeof(opred_slot));
    const float point[3]={1.0f,2.0f,3.0f};float first[4],second[4];
    pstk_fn.transform=test_paim_transform;paim_original=test_paim_original;paim_manual_override=test_paim_manual;test_paim_original_calls=0;
    memset(pfire_slots,0,sizeof(pfire_slots));pilot_slot_count=2;pilot_slots[0].object=test_paim_ship;pilot_slots[1].object=test_paim_ship;
    pfire_slots[0].leased=1;pfire_slots[0].count=1;pfire_slots[0].guns[0].controller=c1;
    pfire_slots[1].leased=1;pfire_slots[1].count=2;pfire_slots[1].guns[0].controller=c1+8;pfire_slots[1].guns[1].controller=c2;
    pfire_slots[0].guns[0].object=c3+0x10;pfire_slots[1].guns[0].object=c3+0x20;pfire_slots[1].guns[1].object=c3+0x30;   /* the guns: any address */
    ui_thread=GetCurrentThreadId();stats.issued=50;paim_armed=1;
    pstk_slot_frame *f=&pstk_slot_frames[1];memset(f,0,sizeof(*f));
    f->tick=50;f->r.flags=PSTK_FLAG_AIM;memcpy(f->r.aim,point,12);f->r.steer=1u|PAIM_STEER_MANUAL;
    /* Slot 2's record of this tick with a manual aim: the point in the asked frame, in both outputs. */
    {   const LONG64 aimed=paim_aimed;memset(first,0x55,16);memset(second,0x55,16);
        assert(TEST_PAIM_CALL(c2)==1 && first[0]==13.0f && first[1]==21.0f && first[2]==32.0f && first[3]==0.0f && !memcmp(first,second,16));
        assert(paim_aimed==aimed+1 && !test_paim_original_calls);}
    /* No manual aim, another tick's record, a record without an aim, no ship: X4's own answer (at rest), outputs untouched. */
    {   const LONG64 rest=paim_rest;memset(first,0x55,16);
        f->r.steer=1;assert(!TEST_PAIM_CALL(c2) && test_paim_original_calls==1);f->r.steer=PAIM_STEER_MANUAL;
        f->tick=49;assert(!TEST_PAIM_CALL(c2));f->tick=50;
        f->r.flags=0;assert(!TEST_PAIM_CALL(c2));f->r.flags=PSTK_FLAG_AIM;
        pilot_slots[1].object=NULL;assert(!TEST_PAIM_CALL(c2));pilot_slots[1].object=test_paim_ship;
        assert(!paim_aim(c2,first,second,NULL,NULL,NULL,0.0f,0.0f,0,0,0,0,0));
        unsigned char same[16];memset(same,0x55,16);
        assert(paim_rest==rest+5 && test_paim_original_calls==5 && !memcmp(first,same,16));
        assert(TEST_PAIM_CALL(c2)==1 && first[0]==13.0f);}   /* the manual bit alone, without mouse steering's */
    /* Slot 1: the stock record's replayed aim. */
    {   pstk_aim_tick=50;pstk_aim_valid_replay=1;memcpy(pstk_aim_replay,point,12);pstk_aim_steer_replay=1u|PAIM_STEER_MANUAL;memset(first,0,16);
        assert(TEST_PAIM_CALL(c1)==1 && first[0]==13.0f && first[2]==32.0f);
        pstk_aim_steer_replay=1;assert(!TEST_PAIM_CALL(c1));pstk_aim_steer_replay=1u|PAIM_STEER_MANUAL;
        pstk_aim_valid_replay=0;assert(!TEST_PAIM_CALL(c1));pstk_aim_valid_replay=1;
        pstk_aim_tick=51;assert(!TEST_PAIM_CALL(c1));pstk_aim_tick=50;}
    /* A Mindless controller that is no lease of ours (the AI's <shoot>), one the simulation replaced, a slot whose lease
     * ended, another thread, not armed: X4's own answer. */
    {   const LONG64 other=paim_other,foreign=paim_foreign;const unsigned calls=test_paim_original_calls;
        assert(!TEST_PAIM_CALL(c3) && paim_other==other+1);
        pfire_slots[1].guns[1].foreign=1;assert(!TEST_PAIM_CALL(c2) && paim_other==other+2);pfire_slots[1].guns[1].foreign=0;
        pfire_slots[1].leased=0;assert(!TEST_PAIM_CALL(c2) && paim_other==other+3);pfire_slots[1].leased=1;
        ui_thread=saved_thread+1;assert(!TEST_PAIM_CALL(c2) && paim_foreign==foreign+1);ui_thread=GetCurrentThreadId();
        paim_armed=0;assert(!TEST_PAIM_CALL(c2) && paim_other==other+4);paim_armed=1;
        assert(test_paim_original_calls==calls+5 && TEST_PAIM_CALL(c2)==1);}
    /* Slot 15, "manual aim?": yes for a leased controller whose slot's record of this tick has the manual bit. */
    {   const paim_manual_fn saved_manual=paim_manual_original;const LONG64 answers=paim_manual_answers;
        paim_manual_original=test_paim_not_manual;
        f->r.steer=1u|PAIM_STEER_MANUAL;assert(paim_manual_get(c2)==1 && paim_manual_get(c1)==1 && paim_manual_answers==answers+2);
        f->r.steer=1;assert(!paim_manual_get(c2));f->r.steer=PAIM_STEER_ASSIST;assert(!paim_manual_get(c2));f->r.steer=PAIM_STEER_MANUAL;
        f->tick=49;assert(!paim_manual_get(c2));f->tick=50;
        assert(!paim_manual_get(c3));                                                            /* no lease of ours */
        ui_thread=saved_thread+1;assert(!paim_manual_get(c2));ui_thread=GetCurrentThreadId();    /* another thread */
        paim_armed=0;assert(!paim_manual_get(c2));paim_armed=1;
        assert(paim_manual_get(c2)==1 && paim_manual_answers==answers+3);
        paim_manual_original=saved_manual;f->r.steer=1u|PAIM_STEER_MANUAL;}
    /* From here: a world of placed objects, X4's functions of the cone and the lock as stand-ins, the config (cone
     * cos 10 degrees, minimum distance 50 m) where X4 keeps it. */
    static _Alignas(16) unsigned char ship2[0x80],ship3[0x80],ship4[0x80],player2[0x80],player3[0x80],player4[0x80],target_x[0x80],target_y[0x80],
        c2a[0x100],c3a[0x100],c4a[0x100],c4b[0x100],gun2[0x80],gun3[0x80],gun4a[0x80],gun4b[0x80];
    const paim_target_fn saved_target=paim_target_original,saved_via=paim_via_original;const paim_manual_fn saved_manual=paim_manual_original,saved_lock=paim_lock_original;
    const paim_manual_fn saved_player=paim_player_original,saved_dot=paim_dot_original;
    const paim_float_fn saved_reach=paim_reach_original,saved_distance=paim_distance_original;
    const int saved_assist=paim_assist_on;
    pnat_base=image;memcpy(saved_config_word,image+PAIM_RANGE_SCALE,8);memcpy(saved_mode_words,image+PAIM_ASSIST_OTHER,8);
    {void *config=test_paim_config;memcpy(image+PAIM_RANGE_SCALE,&config,8);}
    {const float cone=cosf(10.0f*3.14159265f/180.0f),minimum=50.0f;memcpy(test_paim_config+PAIM_CONE,&cone,4);memcpy(test_paim_config+PAIM_MIN_DISTANCE,&minimum,4);}
    test_paim_vtable[PAIM_LARGE_SLOT/8]=(void*)test_paim_large;test_paim_large_value=0;test_paim_class_data[PAIM_CLASS_FLAG]=1;
    memset(test_paim_macro_data,0,sizeof(test_paim_macro_data));
    paim_fn.point=test_paim_point;paim_fn.class_of=test_paim_class_of;paim_fn.macro_of=test_paim_macro_of;paim_fn.distance=test_paim_distance;
    pfire_now=test_paim_now;pstk_fn.transform=test_paim_world;pstk_fn.zone=test_paim_zone_of;test_paim_pose_count=0;
    paim_original=test_paim_original_asks;paim_target_original=test_paim_no_target;paim_via_original=test_paim_no_target;
    paim_manual_original=test_paim_not_manual;paim_lock_original=test_paim_not_manual;paim_target_override=test_paim_target_of;
    paim_player_original=test_paim_not_manual;paim_reach_original=test_paim_one;paim_distance_original=test_paim_one;
    paim_assist_override=0;paim_assist_on=1;paim_range_override=1;
    test_paim_target_init(target_x);test_paim_target_init(target_y);test_paim_target_init(ship2);
    /* Slots 2, 3 and 4, each with its own ship, player P_k, target and record. Slot 2: target X 1 km ahead, its point
     * 2.9 degrees off it. Slot 3, 5 km aside: target Y 2 km ahead of it, its point 39.8 degrees off from P_3 (on the line
     * through Y from the origin, where slot 2's player sits: X4's cone is P_k's). Slot 4: slot 2's ship as its target
     * 5 km below, its point 1.1 degrees off, two guns. */
    memset(pfire_slots,0,sizeof(pfire_slots));pilot_slot_count=4;
    pilot_slots[0].object=test_paim_ship;pilot_slots[1].object=ship2;pilot_slots[2].object=ship3;pilot_slots[3].object=ship4;
    pslots[2].player=player2;pslots[3].player=player3;pslots[4].player=player4;
    test_paim_targets[2]=target_x;test_paim_targets[3]=target_y;test_paim_targets[4]=ship2;
    test_paim_place(ship2,0,0,0,0);test_paim_place(player2,0,0,0,0);test_paim_place(target_x,0,0,1000,0);
    test_paim_place(ship3,5000,0,0,0);test_paim_place(player3,5000,0,0,0);test_paim_place(target_y,5000,0,2000,0);
    test_paim_place(ship4,0,5000,0,0);test_paim_place(player4,0,5000,0,0);
    pfire_slots[1].leased=1;pfire_slots[1].count=1;pfire_slots[1].guns[0].controller=c2a;pfire_slots[1].guns[0].object=gun2;
    pfire_slots[2].leased=1;pfire_slots[2].count=1;pfire_slots[2].guns[0].controller=c3a;pfire_slots[2].guns[0].object=gun3;
    pfire_slots[3].leased=1;pfire_slots[3].count=2;pfire_slots[3].guns[0].controller=c4a;pfire_slots[3].guns[0].object=gun4a;
    pfire_slots[3].guns[1].controller=c4b;pfire_slots[3].guns[1].object=gun4b;
    const unsigned char both=1u|PAIM_STEER_MANUAL|PAIM_STEER_ASSIST;
    pstk_slot_frame *f2=&pstk_slot_frames[1],*f3=&pstk_slot_frames[2],*f4=&pstk_slot_frames[3];
    memset(f2,0,sizeof(*f2));memset(f3,0,sizeof(*f3));memset(f4,0,sizeof(*f4));
    f2->tick=f3->tick=f4->tick=50;f2->r.flags=f3->r.flags=f4->r.flags=PSTK_FLAG_AIM;f2->r.steer=f3->r.steer=f4->r.steer=both;
    f2->r.aim[0]=50.0f;f2->r.aim[2]=1000.0f;f3->r.aim[0]=2500.0f;f3->r.aim[2]=3000.0f;f4->r.aim[1]=-5000.0f;f4->r.aim[2]=100.0f;
#define TEST_PAIM_GUN(c,g) paim_aim(c,first,second,test_paim_space,g,NULL,0.0f,0.0f,0,1,0,0,1)
    void *out=NULL;const uintptr_t turn=(uintptr_t)(image+PAIM_TURN_RETURN);
    /* Several slots aiming in one tick, interleaved: each with its own player's cone, target and lock. */
    {   const LONG64 hits=paim_cone_hits,misses=paim_cone_misses,assisted=paim_assisted,answers=paim_target_answers;const unsigned calls=test_paim_original_calls;
        const uint64_t points3=paim_slot_points[2],cones2=paim_slot_cones[1],cones4=paim_slot_cones[3],locks2=paim_slot_locks[1],locks4=paim_slot_locks[3];
        memset(first,0x55,16);test_paim_seen_target=(void*)1;
        assert(TEST_PAIM_GUN(c3a,gun3)==1 && first[0]==7500.0f && first[1]==0.0f && first[2]==3000.0f && !memcmp(first,second,16));
        assert(test_paim_original_calls==calls && test_paim_seen_target==(void*)1 && paim_cone_misses==misses+1);   /* slot 3: outside P_3's cone */
        assert(paim_last_cosine<0.77f && paim_last_cosine>0.76f);
        out=NULL;assert(paim_via_answer(c3a,&out,NULL,turn)==&out && out==(void*)target_y);   /* its turn takes its target's velocity */
        memset(first,0x55,16);
        test_paim_seen_a10=-1;
        assert(TEST_PAIM_GUN(c2a,gun2)==1 && test_paim_seen_target==(void*)target_x && test_paim_original_calls==calls+1);   /* slot 2: X4's aim on X */
        assert(test_paim_seen_a10==0);   /* X4's player lead: no evasion (Weapon::Move gave 1) */
        assert(test_paim_point_gun==(void*)gun2 && paim_last_cosine>0.998f && first[0]!=50.0f);   /* the lead is X4's: outputs not the point */
        out=NULL;assert(paim_via_answer(c2a,&out,NULL,turn)==&out && out==(void*)target_x);
        out=(void*)1;assert(paim_via_answer(c2a,&out,NULL,turn)==&out && !out);              /* once */
        assert(TEST_PAIM_GUN(c4a,gun4a)==1 && test_paim_seen_target==(void*)ship2);            /* slot 4: X4's aim on slot 2's ship */
        assert(TEST_PAIM_GUN(c4b,gun4b)==1 && test_paim_seen_target==(void*)ship2 && test_paim_point_gun==(void*)gun4b);
        out=NULL;assert(paim_via_answer(c4b,&out,NULL,turn)==&out && out==(void*)ship2);
        assert(TEST_PAIM_GUN(c2a,gun2)==1 && test_paim_seen_target==(void*)target_x);
        assert(paim_cone_hits==hits+4 && paim_assisted==assisted+4 && paim_target_answers==answers+4 && test_paim_original_calls==calls+4);
        assert(paim_slot_points[2]==points3+1 && paim_slot_cones[1]==cones2+2 && paim_slot_cones[3]==cones4+2 && paim_slot_locks[1]==locks2+2 &&
               paim_slot_locks[3]==locks4+2);
        assert(!paim_target_for && !paim_target);
        /* Slot 18 at any other call: X4's answer (the range gate 813f17, another controller, another thread). */
        out=(void*)1;assert(paim_via_answer(c2a,&out,NULL,(uintptr_t)(image+0x813f17u))==&out && !out);
        assert(TEST_PAIM_GUN(c2a,gun2)==1);
        out=(void*)1;assert(paim_via_answer(c4a,&out,NULL,turn)==&out && !out);
        ui_thread=saved_thread+1;out=(void*)1;assert(paim_via_answer(c2a,&out,NULL,turn)==&out && !out);ui_thread=GetCurrentThreadId();
        /* Slot 34 (the HUD's icon): locked at this tick or the one before, per gun. */
        assert(paim_locked_get(c2a)==1 && paim_locked_get(c4a)==1 && paim_locked_get(c4b)==1 && !paim_locked_get(c3a) && !paim_locked_get(c1));
        stats.issued=51;assert(paim_locked_get(c2a)==1);stats.issued=52;assert(!paim_locked_get(c2a));stats.issued=50;
        paim_armed=0;assert(!paim_locked_get(c2a));paim_armed=1;}
    /* A player without X4's aim assist (its option off, or mode 2 with mouse steering): the record has no assist bit and
     * the point stays the point, inside the cone too; the other slots keep theirs. */
    {   const LONG64 hits=paim_cone_hits;const unsigned calls=test_paim_original_calls;
        f4->r.steer=1u|PAIM_STEER_MANUAL;
        assert(TEST_PAIM_GUN(c4a,gun4a)==1 && first[0]==0.0f && first[1]==0.0f && first[2]==100.0f && test_paim_original_calls==calls);
        assert(!paim_locked_get(c4a) && paim_cone_hits==hits);
        assert(TEST_PAIM_GUN(c2a,gun2)==1 && test_paim_seen_target==(void*)target_x && paim_cone_hits==hits+1);
        f4->r.steer=both;}
    /* X4's class flag (7ad890 +0x318) off: no aim assist on that target; manual aim keeps its point, the assist alone rests. */
    {   const LONG64 refused=paim_class_refused,rest=paim_rest;const unsigned calls=test_paim_original_calls;
        test_paim_class_data[PAIM_CLASS_FLAG]=0;
        assert(TEST_PAIM_GUN(c2a,gun2)==1 && first[0]==50.0f && first[2]==1000.0f && test_paim_original_calls==calls && paim_class_refused==refused+1);
        f2->r.steer=PAIM_STEER_ASSIST;test_paim_seen_target=(void*)1;
        assert(!TEST_PAIM_GUN(c2a,gun2) && !test_paim_seen_target && paim_rest==rest+1);
        f2->r.steer=both;test_paim_class_data[PAIM_CLASS_FLAG]=1;}
    /* The lock's minimum distance: a target whose class says "large" and that has no aim targets locks only from 50 m
     * (X4's squared distance, mode 2); closer: X4's false, outputs untouched, no lock for the HUD. */
    {   const LONG64 refused=paim_lock_refused;const unsigned calls=test_paim_original_calls;
        test_paim_large_value=1;test_paim_distance2=40.0f*40.0f;memset(first,0x55,16);unsigned char same[16];memset(same,0x55,16);
        assert(!TEST_PAIM_GUN(c2a,gun2) && !memcmp(first,same,16) && test_paim_original_calls==calls && paim_lock_refused==refused+1);
        assert(test_paim_distance_mode==2 && !paim_locked_get(c2a));
        out=(void*)1;assert(paim_via_answer(c2a,&out,NULL,turn)==&out && !out);             /* no turn after a false aim */
        test_paim_distance2=60.0f*60.0f;assert(TEST_PAIM_GUN(c2a,gun2)==1 && test_paim_original_calls==calls+1 && paim_locked_get(c2a)==1);
        test_paim_distance2=40.0f*40.0f;void *aims=(void*)1;memcpy(test_paim_macro_data+PAIM_AIM_TARGETS,&aims,8);  /* with aim targets */
        assert(TEST_PAIM_GUN(c2a,gun2)==1 && test_paim_original_calls==calls+2);
        aims=NULL;memcpy(test_paim_macro_data+PAIM_AIM_TARGETS,&aims,8);
        test_paim_distance2=NAN;assert(!TEST_PAIM_GUN(c2a,gun2) && paim_lock_refused==refused+2);
        test_paim_large_value=0;assert(TEST_PAIM_GUN(c2a,gun2)==1 && test_paim_original_calls==calls+3);   /* not "large" */
        assert(paim_lock_refused==refused+2);}
    /* The aim assist without manual aim (no mouse steering): the lock on a target in the gun's range, no cone; slot 15 says
     * yes only then. Slot 3's target is 39.8 degrees off its point, and it is locked all the same. */
    {   const LONG64 out_of_range=paim_out_of_range;const unsigned calls=test_paim_original_calls;
        f3->r.steer=PAIM_STEER_ASSIST;
        assert(paim_manual_get(c3a)==1 && TEST_PAIM_GUN(c3a,gun3)==1 && test_paim_seen_target==(void*)target_y && test_paim_original_calls==calls+1);
        assert(paim_locked_get(c3a)==1);
        paim_range_override=-1;assert(!paim_manual_get(c3a) && !TEST_PAIM_GUN(c3a,gun3) && paim_out_of_range==out_of_range+2);
        assert(!paim_locked_get(c3a));
        /* With manual aim the range is not asked (X4's manual aim skips the gate): slot 2's cone locks out of range too. */
        assert(TEST_PAIM_GUN(c2a,gun2)==1 && test_paim_seen_target==(void*)target_x && paim_out_of_range==out_of_range+2);
        paim_range_override=1;f3->r.steer=both;}
    /* No target, the slot's own ship as its target, the assist not armed, no config: the point. */
    {   const unsigned calls=test_paim_original_calls;const LONG64 unread=paim_config_unread;
        test_paim_targets[2]=NULL;assert(TEST_PAIM_GUN(c2a,gun2)==1 && first[0]==50.0f);
        out=(void*)1;assert(paim_via_answer(c2a,&out,NULL,turn)==&out && !out);              /* no target, no velocity */
        test_paim_targets[2]=ship2;assert(TEST_PAIM_GUN(c2a,gun2)==1 && first[0]==50.0f);test_paim_targets[2]=target_x;
        paim_assist_on=0;assert(TEST_PAIM_GUN(c2a,gun2)==1 && first[0]==50.0f);paim_assist_on=1;
        {void *none=NULL;memcpy(image+PAIM_RANGE_SCALE,&none,8);}
        assert(TEST_PAIM_GUN(c2a,gun2)==1 && first[0]==50.0f && paim_config_unread==unread+1);
        {void *config=test_paim_config;memcpy(image+PAIM_RANGE_SCALE,&config,8);}
        assert(test_paim_original_calls==calls);}
    /* X4's cone at its edge (cos 10 degrees): 9.9 degrees in, 10.1 out; a NaN point: -1, out. */
    {   const float in=1000.0f*tanf(9.9f*3.14159265f/180.0f),beyond=1000.0f*tanf(10.1f*3.14159265f/180.0f);float cosine=0.0f;
        const float qi[3]={in,0.0f,1000.0f},qo[3]={beyond,0.0f,1000.0f},qn[3]={NAN,0.0f,1000.0f},cone=cosf(10.0f*3.14159265f/180.0f);
        assert(paim_cone(2,qi,target_x,gun2,test_paim_space,cone,&cosine) && cosine<1.0f);
        assert(!paim_cone(2,qo,target_x,gun2,test_paim_space,cone,&cosine));
        assert(!paim_cone(2,qn,target_x,gun2,test_paim_space,cone,&cosine) && cosine==-1.0f);
        const float qs[3]={0.0f,0.0f,1000.0f};assert(paim_cone(2,qs,target_x,gun2,test_paim_space,cone,&cosine) && cosine==1.0f);
        pslots[2].player=NULL;{const LONG64 unread=paim_cone_unread;assert(!paim_cone(2,qs,target_x,gun2,test_paim_space,cone,&cosine) && paim_cone_unread==unread+1);}
        pslots[2].player=player2;}
    /* The capture: X4's aim assist decision as the player's own (mode 1; 2 only without mouse steering; 3: the other word). */
    {   assert(paim_assist_rule(1,0,1) && paim_assist_rule(1,0,0) && !paim_assist_rule(2,0,1) && paim_assist_rule(2,0,0) && !paim_assist_rule(0,1,0));
        assert(paim_assist_rule(3,1,1) && !paim_assist_rule(3,2,1) && paim_assist_rule(3,2,0) && !paim_assist_rule(3,0,0));
        uint32_t words[2]={1u,3u};memcpy(image+PAIM_ASSIST_OTHER,words,8);   /* the image's own: 3 at 0x2f4b9c4, 1 at 0x2f4b9c0 */
        assert(paim_assist_wanted(1) && paim_assist_wanted(0));
        words[0]=2u;memcpy(image+PAIM_ASSIST_OTHER,words,8);assert(!paim_assist_wanted(1) && paim_assist_wanted(0));
        words[0]=0u;memcpy(image+PAIM_ASSIST_OTHER,words,8);assert(!paim_assist_wanted(0));
        memcpy(image+PAIM_ASSIST_OTHER,saved_mode_words,8);}
    /* The track (the capturing client of slot 3, manual aim): target Y crosses at 30 m per tick, the delay is 3 ticks; a
     * point on Y turns by Y's motion over the delay as seen from the ship (2.58 degrees at 2 km). */
    {   const LONG64 tracked=paim_tracked,wide=paim_track_far,fresh=paim_track_fresh;float aim[3]={0.0f,0.0f,100000.0f};
        pilot_delay=3;opred_armed=0;
        stats.issued=60;test_paim_place(target_y,5000-30,0,2000,0);paim_track(3,aim);
        assert(aim[0]==0.0f && aim[2]==100000.0f && paim_track_fresh==fresh+1);   /* the first tick: no motion known yet */
        stats.issued=61;test_paim_place(target_y,5000,0,2000,0);paim_track(3,aim);
        assert(paim_tracked==tracked+1 && test_paim_near(aim[0]/aim[2],90.0f/2000.0f,1e-5f) && aim[1]==0.0f);
        assert(test_paim_near(sqrtf(aim[0]*aim[0]+aim[2]*aim[2]),100000.0f,1.0f));
        /* outside the cone (21.8 degrees off): as it was */
        stats.issued=62;test_paim_place(target_y,5000+30,0,2000,0);aim[0]=40000.0f;aim[1]=0.0f;aim[2]=100000.0f;paim_track(3,aim);
        assert(aim[0]==40000.0f && paim_track_far==wide+1);
        /* another tick missed, another target: fresh again */
        stats.issued=64;aim[0]=0.0f;paim_track(3,aim);assert(aim[0]==0.0f && paim_track_fresh==fresh+2);
        /* drawn ahead by the own-ship prediction (own ship and, with the other slots predicted, a slot ship target): a
         * target drawn where it will be needs no turn */
        test_paim_targets[3]=ship2;test_paim_place(ship2,5000,0,3000,0);stats.issued=65;paim_track(3,aim);
        opred_armed=1;opred_mode=OPRED_PRESENT;opred_others=1;opred_slots[1].enabled=1;opred_slots[1].h_eff=3.0;opred_slots[2].enabled=1;opred_slots[2].h_eff=3.0;
        test_paim_place(ship2,5000+30,0,3000,0);stats.issued=66;paim_track(3,aim);
        assert(test_paim_near(aim[0],0.0f,1e-3f) && test_paim_near(aim[2],100000.0f,1e-3f));
        /* others not predicted: drawn at the capture tick (60 m aside), turned to where it will be (150 m aside) */
        opred_others=0;test_paim_place(ship2,5000+60,0,3000,0);stats.issued=67;paim_track(3,aim);
        {const float to=150.0f/3000.0f,from=60.0f/3000.0f;assert(test_paim_near(aim[0]/aim[2],(to-from)/(1.0f+to*from),1e-5f));}
        /* a turned ship (its z along the zone's x): the turn comes back into the ship's own frame */
        opred_armed=0;test_paim_targets[3]=target_y;test_paim_place(ship3,0,0,0,1);test_paim_place(player3,0,0,0,1);
        test_paim_place(target_y,2000,-30,0,0);stats.issued=70;aim[0]=0.0f;aim[1]=0.0f;aim[2]=100000.0f;paim_track(3,aim);
        test_paim_place(target_y,2000,0,0,0);stats.issued=71;paim_track(3,aim);
        assert(test_paim_near(aim[0]/aim[2],90.0f/2000.0f,1e-5f) && test_paim_near(aim[1],0.0f,1e-2f));
        test_paim_place(ship3,5000,0,0,0);test_paim_place(player3,5000,0,0,0);test_paim_place(target_y,5000,0,2000,0);
        test_paim_targets[3]=target_y;test_paim_place(ship2,0,0,0,0);}
    /* The rotation itself: 45 degrees about y; a vector on the axis stays; a turn past 90 degrees is refused. */
    {   const float a[3]={0.0f,0.0f,1.0f},b[3]={1.0f,0.0f,1.0f},v[3]={0.0f,0.0f,10.0f},axis[3]={0.0f,1.0f,0.0f},back[3]={0.0f,0.0f,-1.0f};float r[3];
        paim_turn_by(a,b,v,r);assert(test_paim_near(r[0],7.0710678f,1e-4f) && test_paim_near(r[1],0.0f,1e-6f) && test_paim_near(r[2],7.0710678f,1e-4f));
        paim_turn_by(a,b,axis,r);assert(test_paim_near(r[0],0.0f,1e-6f) && test_paim_near(r[1],1.0f,1e-6f) && test_paim_near(r[2],0.0f,1e-6f));
        paim_turn_by(a,back,v,r);assert(!memcmp(r,v,12));paim_turn_by(a,a,v,r);assert(!memcmp(r,v,12));}
    /* The dots (presentation): X4's predicate for this client's own leased gun, asked by the render, with slot 16 saying
     * yes inside it; the dot's distances (the target's from the gun, the range); slot 16 at the HUD's lead marker; every
     * other ask X4's own. Client 2 (its own slot 2, target X), then client 3 (slot 3, target Y). */
    {   static _Alignas(16) unsigned char w2[0x400],w3[0x400];
        unsigned char *const saved_w2=pfire_slots[1].guns[0].object,*const saved_w3=pfire_slots[2].guns[0].object;
        pfire_slots[1].guns[0].object=w2;pfire_slots[2].guns[0].object=w3;
        {void *c=c2a;memcpy(w2+PFIRE_CONTROLLER,&c,8);c=c3a;memcpy(w3+PFIRE_CONTROLLER,&c,8);}
        paim_own_slot_override=test_paim_own_slot;paim_gun_range_override=test_paim_range;paim_dot_original=test_paim_dot_predicate;
        const uintptr_t render=(uintptr_t)(image+PAIM_DOT_RENDER),render2=(uintptr_t)(image+PAIM_DOT_RENDER2),move=(uintptr_t)(image+0x814772u);
        const uintptr_t at_target=(uintptr_t)(image+PAIM_DOT_TARGET_RETURN),at_colour=(uintptr_t)(image+PAIM_DOT_COLOUR_RETURN),
            at_reach=(uintptr_t)(image+PAIM_DOT_REACH_RETURN),lead=(uintptr_t)(image+PAIM_HUD_LEAD_RETURN),ask=(uintptr_t)(image+PAIM_DOT_ASK_RETURN);
        const LONG64 asks=paim_dot_asks,shown=paim_dot_shown,elsewhere=paim_dot_elsewhere,leads=paim_hud_leads,distances=paim_dot_distances;
        test_paim_own=2;
        assert(paim_dot_answer(w2,render)==1 && test_paim_dot_seen==1 && !paim_dot_for);          /* its own gun: X4's yes */
        assert(paim_dot_answer(w2,render2)==1 && paim_dot_asks==asks+2 && paim_dot_shown==shown+2);
        assert(!paim_dot_answer(w2,move) && test_paim_dot_seen==0);                               /* Weapon::Move: X4's own */
        assert(!paim_dot_answer(w3,render) && test_paim_dot_seen==0);                             /* slot 3's gun: no dot */
        ui_thread=saved_thread+1;assert(!paim_dot_answer(w2,render) && paim_dot_elsewhere==elsewhere+1);ui_thread=GetCurrentThreadId();
        paim_armed=0;assert(!paim_dot_answer(w2,render));paim_armed=1;
        assert(paim_dot_asks==asks+2);
        /* slot 16: no outside the predicate's call; yes at the HUD's lead marker for a leased gun; no for the AI's */
        assert(!paim_player_answer(c2a,ask) && !paim_player_answer(c2a,(uintptr_t)(image+0x813519u)));
        assert(paim_player_answer(c2a,lead)==1 && paim_player_answer(c3a,lead)==1 && !paim_player_answer(c3,lead) && paim_hud_leads==leads+2);
        ui_thread=saved_thread+1;assert(!paim_player_answer(c2a,lead));ui_thread=GetCurrentThreadId();
        /* the distances: slot 13 the target's (X4's squared distance, mode 1, from the gun), slot 11 the range */
        test_paim_distance2=1000.0f*1000.0f;test_paim_distance_mode=0;
        assert(paim_distance_answer(c2a,1,at_target)==1000.0f && test_paim_distance_mode==1);
        assert(paim_distance_answer(c2a,1,at_colour)==1000.0f && paim_distance_answer(c2a,0,at_reach)==2500.0f);
        assert(paim_dot_distances==distances+3);
        assert(paim_distance_answer(c2a,1,at_reach)==1.0f && paim_distance_answer(c2a,0,at_target)==1.0f);   /* not its site */
        assert(paim_distance_answer(c3a,1,at_target)==1.0f && paim_distance_answer(c3,1,at_target)==1.0f);   /* not its own gun */
        assert(paim_distance_answer(c2a,1,(uintptr_t)(image+0x816000u))==1.0f && paim_dot_distances==distances+3);
        test_paim_targets[2]=NULL;assert(paim_distance_answer(c2a,1,at_target)==PAIM_DOT_FAR);test_paim_targets[2]=target_x;   /* no target */
        test_paim_distance2=NAN;assert(paim_distance_answer(c2a,1,at_target)==PAIM_DOT_FAR);
        /* client 3: its own gun has the dot, at its target's distance; slot 2's has none */
        test_paim_own=3;test_paim_distance2=2000.0f*2000.0f;
        assert(paim_dot_answer(w3,render)==1 && test_paim_dot_seen==1 && !paim_dot_answer(w2,render));
        assert(paim_distance_answer(c3a,1,at_target)==2000.0f && paim_distance_answer(c2a,1,at_target)==1.0f);
        pfire_slots[1].guns[0].object=saved_w2;pfire_slots[2].guns[0].object=saved_w3;
        paim_own_slot_override=NULL;paim_gun_range_override=NULL;test_paim_distance2=0.0f;}
#undef TEST_PAIM_GUN
    paim_player_original=saved_player;paim_dot_original=saved_dot;
    paim_reach_original=saved_reach;paim_distance_original=saved_distance;
    paim_original=test_paim_original;paim_target_original=saved_target;paim_via_original=saved_via;paim_target_override=NULL;paim_assist_on=saved_assist;
    paim_manual_original=saved_manual;paim_lock_original=saved_lock;paim_range_override=0;paim_assist_override=0;paim_fn=saved_fn;pfire_now=saved_now;
    memcpy(image+PAIM_RANGE_SCALE,saved_config_word,8);pnat_base=saved_base;pilot_delay=saved_delay;
    opred_armed=saved_opred_armed;opred_mode=saved_opred_mode;opred_others=saved_opred_others;memcpy(&opred_slots[1],saved_opred,3*sizeof(opred_slot));free(saved_opred);
    pslots[2].player=saved_players[2];pslots[3].player=saved_players[3];pslots[4].player=saved_players[4];pslots[1].player=saved_players[1];
    memset(paim_locks,0,sizeof(paim_locks));memset(&paim_track_last,0,sizeof(paim_track_last));paim_turn_for=NULL;paim_turn_target=NULL;
    pstk_fn.transform=test_paim_transform;memset(pfire_slots,0,sizeof(pfire_slots));pilot_slot_count=2;
    pilot_slots[0].object=test_paim_ship;pilot_slots[1].object=test_paim_ship;
    pfire_slots[0].leased=1;pfire_slots[0].count=1;pfire_slots[0].guns[0].controller=c1;pfire_slots[0].guns[0].object=c3+0x10;
    pfire_slots[1].leased=1;pfire_slots[1].count=2;pfire_slots[1].guns[0].controller=c1+8;pfire_slots[1].guns[1].controller=c2;
    pfire_slots[1].guns[0].object=c3+0x20;pfire_slots[1].guns[1].object=c3+0x30;
    f=&pstk_slot_frames[1];memset(f,0,sizeof(*f));f->tick=50;f->r.flags=PSTK_FLAG_AIM;memcpy(f->r.aim,point,12);f->r.steer=1u|PAIM_STEER_MANUAL;
    /* The record: a slot's aim is accepted only while this is armed; the steering byte carries the manual and assist bits,
     * both at once too. */
    {   pstk_record r;memset(&r,0,sizeof(r));r.flags=PSTK_FLAG_AIM;r.steer=1u|PAIM_STEER_MANUAL;memcpy(r.aim,point,12);r.capture16=7;
        unsigned char bytes[PSTK_BYTES_MAX];pstk_record back;const unsigned n=pstk_encode(&r,bytes);
        assert(n && bytes[0]==PSTK_VERSION && pstk_decode(bytes,n,&back) && back.steer==(1u|PAIM_STEER_MANUAL) && back.aim[2]==3.0f);
        bytes[28]=8;assert(!pstk_decode(bytes,n,&back));bytes[28]=PAIM_STEER_ASSIST;assert(pstk_decode(bytes,n,&back));
        bytes[28]=1u|PAIM_STEER_MANUAL|PAIM_STEER_ASSIST;assert(pstk_decode(bytes,n,&back) && back.steer==7u);
        bytes[0]=3;assert(!pstk_decode(bytes,n,&back));bytes[0]=PSTK_VERSION;   /* a record of the kit before: refused */
        assert(pstk_slot_record_ok(&r,1) && pstk_slot_record_ok(&r,2));
        paim_armed=0;assert(pstk_slot_record_ok(&r,1) && !pstk_slot_record_ok(&r,2));paim_armed=1;}
    /* The capture: no aim without the focus; slots 2..N only inside their own presentation scope. */
    {   float aim[3];unsigned char steer=0;const LONG64 unfocused=pstk_aim_unfocused,no_scope=paim_no_scope;
        pstk_focus_forced=-1;assert(paim_take(2,aim,&steer)==-1 && pstk_aim_unfocused==unfocused+1);pstk_focus_forced=1;
        pres_open=0;pres_paused=0;pres_slot=2;assert(!paim_take(2,aim,&steer) && paim_no_scope==no_scope+1);   /* no scope to open */
        pres_slot=3;pres_paused=1;assert(!paim_take(4,aim,&steer) && paim_no_scope==no_scope+2 && pres_paused==1); /* another slot's */
        paim_armed=0;assert(!paim_take(2,aim,&steer) && paim_no_scope==no_scope+2);paim_armed=1;
        /* A scripted aim (runs without people): the point as a manual aim, whatever the focus and the scope; mode 1 with the
         * assist bit as the player's option gives it with mouse steering, 3 never, 2 the assist alone. */
        paim_script_on=1;paim_script_point[0]=4.0f;paim_script_point[1]=5.0f;paim_script_point[2]=6.0f;pstk_focus_forced=-1;steer=0;paim_assist_override=-1;
        assert(paim_take(2,aim,&steer)==1 && aim[0]==4.0f && aim[2]==6.0f && steer==(1u|PAIM_STEER_MANUAL) && paim_no_scope==no_scope+2);
        paim_assist_override=1;steer=0;assert(paim_take(4,aim,&steer)==1 && steer==(1u|PAIM_STEER_MANUAL|PAIM_STEER_ASSIST));
        paim_script_on=3;steer=0;assert(paim_take(3,aim,&steer)==1 && steer==(1u|PAIM_STEER_MANUAL));
        paim_script_on=2;steer=0;assert(paim_take(2,aim,&steer)==1 && steer==PAIM_STEER_ASSIST);
        paim_script_on=0;pstk_focus_forced=1;paim_assist_override=0;}
    /* Arming: only with the record's fire lease, on the UI thread. */
    {   const int fire=pfire_armed,mode=pfire_mode;paim_armed=0;
        pfire_armed=0;assert(!paim_arm(0) && !strcmp(paim_why,"fire_record"));
        pfire_armed=1;pfire_mode=PFIRE_SCRIPT;assert(!paim_arm(1) && !strcmp(paim_why,"fire_record"));
        pfire_mode=PFIRE_RECORD;ui_thread=saved_thread+1;assert(!paim_arm(0) && !strcmp(paim_why,"thread"));
        paim_armed=1;paim_assist_on=1;assert(paim_arm(1) && !paim_arm(0));paim_armed=0;paim_assist_on=0;   /* armed: the same form only */
        pfire_armed=fire;pfire_mode=mode;paim_why="-";}
    char text[1024];paim_armed=1;
    assert(paim_status(text,sizeof(text))>0 && strstr(text,"armed=1") && strstr(text,"no_scope=") && strstr(text,"installed=000000000") && strstr(text," dot=") && strstr(text," lead=") && strstr(text," own=") &&
           strstr(text," cone=") && strstr(text," turn=") && strstr(text," lock=") && strstr(text," track=") && strstr(text," s2=") && strstr(text," s4="));
    pstk_fn=saved_pstk;memcpy(pfire_slots,saved_fire,sizeof(saved_fire));
    for(unsigned i=0;i<4;i++) pilot_slots[i]=saved_slots[i];
    pilot_slot_count=saved_count;pres_slot=saved_pres_slot;for(unsigned i=0;i<3;i++) pstk_slot_frames[i+1]=saved_frames[i];
    stats.issued=saved_issued;pstk_aim_tick=saved_aim_tick;
    ui_thread=saved_thread;paim_armed=saved_armed;pstk_aim_valid_replay=saved_valid;pstk_focus_forced=saved_focus;pres_paused=saved_paused;
    pres_open=saved_open;memcpy(pstk_aim_replay,saved_replay,12);pstk_aim_steer_replay=saved_steer;paim_original=saved_original;
    paim_manual_override=NULL;
    memset(paim_slot_points,0,sizeof(paim_slot_points));memset(paim_slot_cones,0,sizeof(paim_slot_cones));memset(paim_slot_locks,0,sizeof(paim_slot_locks));
    puts("pilot aim: manual aim, X4's cone/class flag/minimum distance, slots 2, 3 and 4 aiming at once, slot 18 turn, slot 34 lock, "
         "the player lead (a10), the dots of clients 2 and 3, slot 16 at the HUD's lead, capture bits, track: synthetic tests PASS");
}
/* The pinned image: the functions' bytes, the Mindless vtable's five entries, Weapon::Move's gimbal-turn call of slot 18
 * (its return address is what slot 18 answers at) and GetAimAssistState's call of slot 34. */
static void test_pilot_aim_native_image(const char *path) {
    HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    assert(file!=INVALID_HANDLE_VALUE);
    HANDLE mapping=CreateFileMappingA(file,NULL,PAGE_READONLY|SEC_IMAGE_NO_EXECUTE,0,0,NULL);
    assert(mapping);
    const unsigned char *view=MapViewOfFile(mapping,FILE_MAP_READ,0,0,0);
    assert(view);
    for(unsigned i=0;i<sizeof(paim_pins_list)/sizeof(*paim_pins_list);i++)
        assert(hash_ok(view+paim_pins_list[i].rva,paim_pins_list[i].size,paim_pins_list[i].sha));
    uint64_t entry=0,base=0;const IMAGE_DOS_HEADER *dos=(const void*)view;const IMAGE_NT_HEADERS64 *nt=(const void*)(view+dos->e_lfanew);
    base=nt->OptionalHeader.ImageBase;memcpy(&entry,view+PFIRE_MINDLESS_VTABLE+PAIM_VTABLE_SLOT,8);
    /* mapped as an image without relocation applied at its preferred base or at the view: the entry is one of the two */
    assert(entry==base+PAIM_MINDLESS_AIM || entry==(uint64_t)(uintptr_t)view+PAIM_MINDLESS_AIM);
    for(unsigned i=0;i<PAIM_ENTRIES;i++) {
        memcpy(&entry,view+paim_entries[i].vtable+paim_entries[i].slot,8);
        assert(entry==base+paim_entries[i].target || entry==(uint64_t)(uintptr_t)view+paim_entries[i].target);
    }
    /* the call sites whose return addresses the dots and the HUD's lead answer at */
    static const unsigned char slot16_call[6]={0xff,0x90,0x80,0x00,0x00,0x00},dot_call[6]={0xff,0x90,0x88,0x20,0x00,0x00},
        slot13_call[3]={0xff,0x50,0x68},slot11_call[3]={0xff,0x50,0x58};
    assert(!memcmp(view+PAIM_DOT_ASK_RETURN-6u,slot16_call,6) && !memcmp(view+PAIM_HUD_LEAD_RETURN-6u,slot16_call,6));
    assert(!memcmp(view+PAIM_DOT_RENDER-6u,dot_call,6) && !memcmp(view+PAIM_DOT_RENDER2-6u,dot_call,6));
    assert(!memcmp(view+PAIM_DOT_TARGET_RETURN-3u,slot13_call,3) && !memcmp(view+PAIM_DOT_COLOUR_RETURN-3u,slot13_call,3));
    assert(!memcmp(view+PAIM_DOT_REACH_RETURN-3u,slot11_call,3));
    /* X4's player controller says yes in slot 16 (what the dots and the HUD's lead get from ours) */
    memcpy(&entry,view+PFIRE_PLAYER_VTABLE+PAIM_PLAYER_SLOT,8);assert(entry==base+PAIM_YES || entry==(uint64_t)(uintptr_t)view+PAIM_YES);
    memcpy(&entry,view+PFIRE_MINDLESS_VTABLE+0x90u,8);   /* slot 18: slot 17's answer */
    assert(entry==base+PAIM_TARGET_VIA || entry==(uint64_t)(uintptr_t)view+PAIM_TARGET_VIA);
    static const unsigned char via_call[6]={0xff,0x90,0x90,0x00,0x00,0x00},lock_call[6]={0xff,0x90,0x10,0x01,0x00,0x00};
    assert(!memcmp(view+PAIM_TURN_RETURN-6u,via_call,6));   /* 8141ef: call [rax+0x90], returning to 8141f5 */
    assert(!memcmp(view+0xb02552u,lock_call,6));            /* GetAimAssistState: call [rax+0x110] */
    float threshold=0.0f;memcpy(&threshold,view+0x2cbe5d4u,4);assert(threshold==1.0f);   /* 7e89ee: the clamp at 1 */
    memcpy(&threshold,view+0x2cbf01cu,4);assert(threshold==-1.0f);                        /* 7e8a02: ...and at -1 */
    memcpy(&threshold,view+0x22f1330u,4);assert(threshold==PAIM_EPSILON);                 /* 7e8953: the epsilon */
    UnmapViewOfFile(view);CloseHandle(mapping);CloseHandle(file);
    puts("pilot aim: pinned image (Mindless aim, manual aim predicate, player aim, cone and lock helpers, vtable slots 11/13/15/16/17/18/21/34, "
         "U::Weapon's dot predicate, the gimbal turn's slot 18 call, the HUD's slot 34 call, the dots' and the HUD lead's calls) PASS");
}

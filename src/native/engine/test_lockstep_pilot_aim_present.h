/* lockstep_pilot_aim_present.h on synthetic memory: this client's own guns at its latest aim (slots 1, 2 and 3 as this
 * client's own), X4's rules on the sample quietly, the IK on a stand-in for X4's forward kinematics that reads its joints
 * through the value slot, the table's gating, and the icon's slot 34. Whether X4's guns are drawn that way is the live
 * run's question. Uses the world of test_lockstep_pilot_aim.h (its stand-ins and poses). */
static unsigned test_paimp_own;
static unsigned test_paimp_own_slot(void) {return test_paimp_own;}
/* A gun: two rotations (type 5 yaw about y, type 4 pitch about x), each within +-10 degrees, the muzzle 2 m ahead of the
 * pivot. Its joints are 0x680-byte blocks; the stand-in for 81cb80 reads their values through the value slot, as X4's
 * e20370 does. */
#define TEST_PAIMP_LIMIT 0.17453292f
static _Alignas(16) unsigned char test_paimp_joints[3][2][0x680];
static unsigned char *test_paimp_gun_objects[3];
static int test_paimp_collect(paimp_gun *g) {
    for(unsigned s=0;s<3;s++) if(g->gun==test_paimp_gun_objects[s]) {
        g->count=2;
        for(unsigned k=0;k<2;k++) {
            paimp_joint *j=&g->j[k];memset(j,0,sizeof(*j));j->at=test_paimp_joints[s][k];memcpy(&j->sim,j->at+0x08,4);
            j->type=k?4:5;j->lo=-TEST_PAIMP_LIMIT;j->hi=TEST_PAIMP_LIMIT;j->value=j->sim;
        }
        return 1;
    }
    return 0;
}
static float test_paimp_original_value(void *self) {float v=0.0f;memcpy(&v,(unsigned char*)self-PAIMP_VALUE_AT+0x08,4);return v;}
static float *test_paimp_muzzle(void *gun,float *out) {
    unsigned s=0;
    while(s<3 && test_paimp_gun_objects[s]!=gun) s++;
    assert(s<3);
    const float yaw=paimp_value_answer(test_paimp_joints[s][0]+PAIMP_VALUE_AT),pitch=paimp_value_answer(test_paimp_joints[s][1]+PAIMP_VALUE_AT);
    const float cy=cosf(yaw),sy=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch);
    /* rows: x = (cy, 0, -sy), y = (sy sp, cp, cy sp), z = (sy cp, -sp, cy cp); the muzzle 2 m along z */
    memset(out,0,64);
    out[4]=cy;out[6]=-sy;out[8]=sy*sp;out[9]=cp;out[10]=cy*sp;out[12]=sy*cp;out[13]=-sp;out[14]=cy*cp;
    out[0]=2.0f*out[12];out[1]=2.0f*out[13];out[2]=2.0f*out[14];
    return out;
}
static float test_paimp_lead_point[3];
static unsigned test_paimp_lead_calls;
static float *test_paimp_lead(void *gun,float *out,void *target,void *space) {
    (void)gun;(void)target;(void)space;++test_paimp_lead_calls;memcpy(out,test_paimp_lead_point,12);out[3]=0.0f;return out;
}
static void test_paimp_set_joints(unsigned s,float yaw,float pitch) {memcpy(test_paimp_joints[s][0]+0x08,&yaw,4);memcpy(test_paimp_joints[s][1]+0x08,&pitch,4);}
static void test_pilot_aim_present(void) {
    /* the world: slot k's ship, gun and player at (k*5000,0,0), unturned; its target 1 km ahead (slot 1: X; 2: Y; 3: X too) */
    static _Alignas(16) unsigned char ships[3][0x80],players[3][0x80],guns[3][0x400],targets[2][0x80],controllers[3][0x100],universe[0x300];
    static unsigned char other_controller[0x100];
    const pstk_fns saved_pstk=pstk_fn;const paim_fns saved_fn=paim_fn;pfire_slot saved_fire[PILOT_SLOTS];memcpy(saved_fire,pfire_slots,sizeof(saved_fire));
    pilot_slot saved_slots[3]={pilot_slots[0],pilot_slots[1],pilot_slots[2]};const unsigned saved_count=pilot_slot_count;
    unsigned char *saved_players[3]={pslots[1].player,pslots[2].player,pslots[3].player},*saved_base=pnat_base;
    const DWORD saved_thread=ui_thread;const int saved_armed=paim_armed,saved_assist=paim_assist_on;const uint64_t saved_issued=stats.issued;
    unsigned char saved_universe_word[8],saved_config_word[8];
    int (*saved_now)(double *)=pfire_now;
    const paim_manual_fn saved_manual=paim_manual_override,saved_player=paim_player_original,saved_lock=paim_lock_original;
    pnat_base=image;memcpy(saved_universe_word,image+PNAT_UNIVERSE,8);memcpy(saved_config_word,image+PAIM_RANGE_SCALE,8);
    {void *config=test_paim_config;memcpy(image+PAIM_RANGE_SCALE,&config,8);}
    {const float cone=cosf(10.0f*3.14159265f/180.0f),minimum=50.0f;memcpy(test_paim_config+PAIM_CONE,&cone,4);memcpy(test_paim_config+PAIM_MIN_DISTANCE,&minimum,4);}
    {void *u=universe,*p1=players[0];memset(universe,0,sizeof(universe));memcpy(universe+0x230,&p1,8);memcpy(image+PNAT_UNIVERSE,&u,8);}
    test_paim_vtable[PAIM_LARGE_SLOT/8]=(void*)test_paim_large;test_paim_large_value=0;test_paim_class_data[PAIM_CLASS_FLAG]=1;
    memset(test_paim_macro_data,0,sizeof(test_paim_macro_data));
    paim_fn.point=test_paim_point;paim_fn.class_of=test_paim_class_of;paim_fn.macro_of=test_paim_macro_of;paim_fn.distance=test_paim_distance;
    pfire_now=test_paim_now;pstk_fn.transform=test_paim_world;pstk_fn.zone=test_paim_zone_of;test_paim_pose_count=0;
    paim_target_override=test_paim_target_of;paim_range_override=1;paim_assist_on=1;paim_player_original=test_paim_not_manual;
    paim_lock_original=test_paim_not_manual;
    memset(pfire_slots,0,sizeof(pfire_slots));pilot_slot_count=3;
    for(unsigned s=0;s<3;s++) {
        pilot_slots[s].object=ships[s];if(s) pslots[s+1].player=players[s];
        test_paim_place(ships[s],(float)(s*5000),0,0,0);test_paim_place(players[s],(float)(s*5000),0,0,0);test_paim_place(guns[s],(float)(s*5000),0,0,0);
        pfire_slots[s].leased=1;pfire_slots[s].count=1;pfire_slots[s].guns[0].controller=controllers[s];pfire_slots[s].guns[0].object=guns[s];
        {void *c=controllers[s];memcpy(guns[s]+PFIRE_CONTROLLER,&c,8);}
        test_paimp_gun_objects[s]=guns[s];test_paimp_set_joints(s,0.0f,0.0f);
    }
    test_paim_target_init(targets[0]);test_paim_target_init(targets[1]);
    test_paim_place(targets[0],0,0,1000,0);test_paim_place(targets[1],5000,0,1000,0);
    test_paim_targets[1]=targets[0];test_paim_targets[2]=targets[1];test_paim_targets[3]=NULL;   /* slot 3: no target */
    ui_thread=GetCurrentThreadId();stats.issued=100;paim_armed=1;
    paimp_own_override=test_paimp_own_slot;paim_own_slot_override=test_paimp_own_slot;paimp_collect_override=test_paimp_collect;
    paimp_fn.muzzle=test_paimp_muzzle;paimp_fn.lead=test_paimp_lead;paimp_value_original=test_paimp_original_value;
    paimp_armed=1;paimp_active=0;paimp_sample.valid=0;
    const unsigned char both=1u|PAIM_STEER_MANUAL|PAIM_STEER_ASSIST,manual=1u|PAIM_STEER_MANUAL;
    /* The sample: this client's own slot only, as sampled; too old (more than two ticks) is not shown. */
    {   const float ahead[3]={0.0f,0.0f,1000.0f};
        test_paimp_own=2;paimp_note_sample(3,ahead,both);assert(!paimp_sample.valid);
        paimp_note_sample(2,ahead,both);assert(paimp_sample.valid && paimp_sample.tick==100 && paimp_sample.steer==both && paimp_fresh());
        stats.issued=102;assert(paimp_fresh());stats.issued=103;assert(!paimp_fresh());stats.issued=100;
        paimp_armed=0;paimp_sample.valid=0;paimp_note_sample(2,ahead,both);assert(!paimp_sample.valid);paimp_armed=1;}
    /* X4's rules on the sample now, quietly, for slots 1, 2 and 3 as this client's own. */
    {   const LONG64 hits=paim_cone_hits,refused=paim_class_refused,assisted=paim_assisted;const float cosine=paim_last_cosine;
        float point[3];int locked=0;
        /* slot 2, the point 2.9 degrees off its target 1 km ahead: locked, X4's lead for the gun */
        test_paimp_own=2;{const float p[3]={50.0f,0.0f,1000.0f};paimp_note_sample(2,p,both);}
        test_paimp_lead_point[0]=5010.0f;test_paimp_lead_point[1]=3.0f;test_paimp_lead_point[2]=1000.0f;test_paimp_lead_calls=0;
        assert(paimp_aim_now(2,guns[1],test_paim_zone,point,&locked)==1 && locked && point[0]==5010.0f && point[1]==3.0f && test_paimp_lead_calls==1);
        assert(paimp_aim_now(2,guns[1],test_paim_zone,NULL,&locked)==1 && locked && test_paimp_lead_calls==1);   /* the icon: no lead asked */
        /* manual only: the point itself */
        {const float p[3]={50.0f,0.0f,1000.0f};paimp_note_sample(2,p,manual);}
        assert(paimp_aim_now(2,guns[1],test_paim_zone,point,&locked)==1 && !locked && point[0]==5050.0f && point[2]==1000.0f);
        /* slot 1 (P_1 is X4's own player): 39 degrees off its target, outside the cone: the point */
        test_paimp_own=1;{const float p[3]={800.0f,0.0f,1000.0f};paimp_note_sample(1,p,both);}
        assert(paimp_aim_now(1,guns[0],test_paim_zone,point,&locked)==1 && !locked && point[0]==800.0f);
        {const float p[3]={10.0f,0.0f,1000.0f};paimp_note_sample(1,p,both);}
        assert(paimp_aim_now(1,guns[0],test_paim_zone,point,&locked)==1 && locked && point[0]==5010.0f);   /* inside: the lead */
        /* the class without the aim assist flag: the point, and the counter is the simulation's (quiet) */
        test_paim_class_data[PAIM_CLASS_FLAG]=0;assert(paimp_aim_now(1,guns[0],test_paim_zone,point,&locked)==1 && !locked && point[0]==10.0f);
        test_paim_class_data[PAIM_CLASS_FLAG]=1;
        /* the aim assist alone (no mouse steering): the lock on a target in range, at rest out of range */
        paimp_note_sample(1,point,PAIM_STEER_ASSIST);
        assert(paimp_aim_now(1,guns[0],test_paim_zone,point,&locked)==1 && locked && point[0]==5010.0f);
        paim_range_override=-1;assert(!paimp_aim_now(1,guns[0],test_paim_zone,point,&locked) && !locked);paim_range_override=1;
        /* slot 3 has no target: its manual point, never a lock */
        test_paimp_own=3;{const float p[3]={0.0f,20.0f,1000.0f};paimp_note_sample(3,p,both);}
        assert(paimp_aim_now(3,guns[2],test_paim_zone,point,&locked)==1 && !locked && point[0]==10000.0f && point[1]==20.0f);
        /* no aim bits, a stale sample: at rest */
        paimp_note_sample(3,point,1u);assert(!paimp_aim_now(3,guns[2],test_paim_zone,point,&locked));
        paimp_note_sample(3,point,both);stats.issued=110;assert(!paimp_aim_now(3,guns[2],test_paim_zone,point,&locked));stats.issued=100;
        assert(paim_cone_hits==hits && paim_class_refused==refused && paim_assisted==assisted && paim_last_cosine==cosine && !paim_quiet);}
    /* The IK on X4's forward kinematics (the stand-in reads the joints through the value slot): the muzzle points at the aim,
     * each rotation inside its limit; behind the gun: not solved, the simulation's values. */
    {   paimp_gun g;memset(&g,0,sizeof(g));g.gun=guns[1];test_paimp_set_joints(1,0.05f,-0.02f);assert(test_paimp_collect(&g));
        paimp_gun_count=1;paimp_guns[0]=g;paimp_active=1;
        const float aim[3]={1000.0f*tanf(0.08f),-1000.0f*tanf(0.03f),1000.0f};float m[16],r[2];
        assert(paimp_ik(&paimp_guns[0],aim));
        assert(paimp_fk(&paimp_guns[0],(float[]){paimp_guns[0].j[0].value,paimp_guns[0].j[1].value},m) && paimp_residual(m,aim,r));
        assert(sqrtf(r[0]*r[0]+r[1]*r[1])<PAIMP_CONVERGED && paimp_guns[0].j[0].value>0.07f && paimp_guns[0].j[0].value<0.09f);
        const uint64_t clamped=paimp_clamped;
        const float wide[3]={1000.0f,0.0f,1000.0f};   /* 45 degrees aside: held at the limit */
        assert(paimp_ik(&paimp_guns[0],wide) && paimp_guns[0].j[0].value==TEST_PAIMP_LIMIT && paimp_clamped==clamped+1);
        const float behind[3]={0.0f,0.0f,-1000.0f};
        assert(!paimp_ik(&paimp_guns[0],behind) && paimp_guns[0].j[0].value==0.05f && paimp_guns[0].j[1].value==-0.02f);
        paimp_active=0;paimp_gun_count=0;test_paimp_set_joints(1,0.0f,0.0f);}
    /* The value slot: the table's value for its joints while it answers on the frame thread; X4's at any other read. */
    {   paimp_gun_count=1;memset(&paimp_guns[0],0,sizeof(paimp_guns[0]));paimp_guns[0].gun=guns[0];paimp_guns[0].count=1;
        paimp_guns[0].j[0].at=test_paimp_joints[0][0];paimp_guns[0].j[0].value=0.125f;test_paimp_set_joints(0,0.5f,0.0f);
        void *self=test_paimp_joints[0][0]+PAIMP_VALUE_AT,*other=test_paimp_joints[0][1]+PAIMP_VALUE_AT;
        paimp_active=0;assert(paimp_value_answer(self)==0.5f);
        paimp_active=1;assert(paimp_value_answer(self)==0.125f && paimp_value_answer(other)==0.0f);
        ui_thread=saved_thread+1;assert(paimp_value_answer(self)==0.5f);ui_thread=GetCurrentThreadId();
        paimp_active=0;paimp_gun_count=0;test_paimp_set_joints(0,0.0f,0.0f);}
    /* Slot 10 for clients 1, 2 and 3: the own gun's joints answered at its aim while the table is up, none after it; another
     * slot's gun never; a stale sample: nothing. */
    for(unsigned own=1;own<=3;own++) {
        const uint64_t shown=paimp_shown;
        test_paimp_own=own;const float p[3]={(own==3?-1.0f:1.0f)*1000.0f*tanf(0.06f),0.0f,1000.0f};paimp_note_sample(own,p,manual);
        const int entered=paimp_enter();
        assert(entered && paimp_active && paimp_gun_count==1 && paimp_guns[0].gun==guns[own-1] && paimp_guns[0].shown && paimp_shown==shown+1);
        const float yaw=paimp_value_answer(test_paimp_joints[own-1][0]+PAIMP_VALUE_AT);
        assert(fabsf(yaw-(own==3?-0.06f:0.06f))<2e-3f);
        for(unsigned s=1;s<=3;s++) if(s!=own) assert(paimp_value_answer(test_paimp_joints[s-1][0]+PAIMP_VALUE_AT)==0.0f);
        paimp_leave(entered);
        assert(!paimp_active && paimp_value_answer(test_paimp_joints[own-1][0]+PAIMP_VALUE_AT)==0.0f);
        stats.issued=110;assert(!paimp_enter() && !paimp_active);stats.issued=100;
    }
    {paimp_armed=0;test_paimp_own=2;const float p[3]={0.0f,0.0f,1000.0f};paimp_note_sample(2,p,manual);assert(!paimp_enter());paimp_armed=1;}
    /* The icon (slot 34 at GetAimAssistState): this client's own gun by its local lock, before the simulation's; X4's answer
     * from another call site, for another slot's gun, without a fresh sample. */
    {   const uintptr_t state=(uintptr_t)(image+PAIMP_STATE_RETURN);int answered=0;
        test_paimp_own=2;{const float p[3]={50.0f,0.0f,1000.0f};paimp_note_sample(2,p,both);}
        assert(paimp_locked_answer(controllers[1],state,&answered)==1 && answered);
        {const float p[3]={800.0f,0.0f,1000.0f};paimp_note_sample(2,p,both);}
        assert(!paimp_locked_answer(controllers[1],state,&answered) && answered);           /* outside the cone: dim, at once */
        assert(paim_locked_answer(controllers[1],state)==0);                                /* through slot 34: the local answer */
        {const float p[3]={50.0f,0.0f,1000.0f};paimp_note_sample(2,p,both);}
        assert(paim_locked_answer(controllers[1],state)==1);
        assert(!paimp_locked_answer(controllers[1],(uintptr_t)(image+0x816000u),&answered) && !answered);
        assert(!paimp_locked_answer(controllers[0],state,&answered) && !answered);          /* slot 1's gun on client 2 */
        assert(!paimp_locked_answer(other_controller,state,&answered) && !answered);
        stats.issued=110;assert(!paimp_locked_answer(controllers[1],state,&answered) && !answered);stats.issued=100;
        test_paimp_own=3;{const float p[3]={0.0f,0.0f,1000.0f};paimp_note_sample(3,p,both);}
        assert(!paimp_locked_answer(controllers[2],state,&answered) && answered);           /* client 3: no target, no lock */
        test_paimp_own=1;{const float p[3]={10.0f,0.0f,1000.0f};paimp_note_sample(1,p,both);}
        assert(paimp_locked_answer(controllers[0],state,&answered)==1 && answered);}        /* client 1 */
    char text[768];assert(paimp_status(text,sizeof(text))>0 && strstr(text,"armed=1") && strstr(text," shown=") && strstr(text," icons="));
    paimp_armed=0;paimp_active=0;paimp_gun_count=0;paimp_sample.valid=0;paimp_own_override=NULL;paim_own_slot_override=NULL;
    paimp_collect_override=NULL;memset(&paimp_fn,0,sizeof(paimp_fn));paimp_value_original=NULL;
    paim_target_override=NULL;paim_range_override=0;paim_assist_on=saved_assist;paim_player_original=saved_player;paim_lock_original=saved_lock;
    paim_manual_override=saved_manual;paim_fn=saved_fn;pstk_fn=saved_pstk;pfire_now=saved_now;
    memcpy(image+PNAT_UNIVERSE,saved_universe_word,8);memcpy(image+PAIM_RANGE_SCALE,saved_config_word,8);pnat_base=saved_base;
    memcpy(pfire_slots,saved_fire,sizeof(saved_fire));for(unsigned i=0;i<3;i++) pilot_slots[i]=saved_slots[i];pilot_slot_count=saved_count;
    pslots[1].player=saved_players[0];pslots[2].player=saved_players[1];pslots[3].player=saved_players[2];
    ui_thread=saved_thread;paim_armed=saved_armed;stats.issued=saved_issued;test_paim_pose_count=0;
    puts("pilot aim present: the sample of slots 1, 2 and 3, X4's rules on it quietly (lock, point, assist alone), the IK in its limits, "
         "the value slot's gating, slot 10's table, the icon's slot 34: synthetic tests PASS");
}
/* The pinned image: the functions the presentation relies on, the value slot's entry and the vbtable's offset, the dot's
 * call of the muzzle frame, the HUD's call of the lead. */
static void test_pilot_aim_present_native_image(const char *path) {
    HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    assert(file!=INVALID_HANDLE_VALUE);
    HANDLE mapping=CreateFileMappingA(file,NULL,PAGE_READONLY|SEC_IMAGE_NO_EXECUTE,0,0,NULL);
    assert(mapping);
    const unsigned char *view=MapViewOfFile(mapping,FILE_MAP_READ,0,0,0);
    assert(view);
    for(unsigned i=0;i<sizeof(paimp_pins_list)/sizeof(*paimp_pins_list);i++)
        assert(hash_ok(view+paimp_pins_list[i].rva,paimp_pins_list[i].size,paimp_pins_list[i].sha));
    const IMAGE_DOS_HEADER *dos=(const void*)view;const IMAGE_NT_HEADERS64 *nt=(const void*)(view+dos->e_lfanew);
    const uint64_t base=nt->OptionalHeader.ImageBase;uint64_t entry=0;int32_t offset=0,rel=0;
    memcpy(&entry,view+PAIMP_VALUE_VTABLE+PAIMP_VALUE_SLOT,8);
    assert(entry==base+PAIMP_VALUE_THUNK || entry==(uint64_t)(uintptr_t)view+PAIMP_VALUE_THUNK);
    memcpy(&offset,view+PAIMP_VBT_OFFSET,4);assert(offset==(int32_t)PAIMP_VALUE_AT);
    assert(view[0x81d7aau]==0xe8);memcpy(&rel,view+0x81d7abu,4);assert(0x81d7afu+rel==PAIMP_MUZZLE);   /* 81d700: the dot's muzzle frame */
    assert(view[0x4de5c5u]==0xe8);memcpy(&rel,view+0x4de5c6u,4);assert(0x4de5cau+rel==PAIMP_LEAD);     /* the HUD's lead (4de590) */
    static const unsigned char state_call[6]={0xff,0x90,0x10,0x01,0x00,0x00};
    assert(!memcmp(view+PAIMP_STATE_RETURN-6u,state_call,6));
    UnmapViewOfFile(view);CloseHandle(mapping);CloseHandle(file);
    puts("pilot aim present: pinned image (the value slot and its thunk, the bone transform, the weapon frame, the muzzle frame, the HUD's "
         "lead and the icon's slot 34 call) PASS");
}

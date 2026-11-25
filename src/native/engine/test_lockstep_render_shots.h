/* Bullets drawn moving between the steps (lockstep_render_shots.h), on test_lockstep_render_display.h's fake engine through
 * the real render RNG split wrapper: a fake component registry (Find's layout), bullets noted by the Move wrapper's hook, the
 * step sample (live, a U::Bullet, in the camera's zone, first sample, motion, snaps, re-identified, noted twice), extrapolate
 * and interpolate (clamped at the first sample), an own shot of the ship slot 3 presents leaving its drawn guns, a bullet
 * gone or stale before the draw, a foreign pointer failing closed, an exception unwinding, nesting, the probe (one bullet 5 m
 * aside through rdisp_override), arming, status; with X4.exe the pins and Find's layout. */
enum {TRS_OWN,TRS_OTHER,TRS_NEW,TRS_FAR,TRS_COUNT};
static __declspec(align(16)) unsigned char test_rs_map[0x200],test_rs_block[0x400],test_rs_bullets[TRS_COUNT][0x300],test_rs_poses[TRS_COUNT][RDISP_BLOCK_BYTES];
static __declspec(align(16)) unsigned char test_rs_weapon[0x100],test_rs_turret[0x100],test_rs_gun[0x100],test_rs_foreign_block[RDISP_BLOCK_BYTES];
static unsigned char *test_rs_seen[TRS_COUNT];
static float test_rs_seen_pose[TRS_COUNT][16];
static int test_rs_foreign;
static const double test_rs_base[TRS_COUNT][3]={{200,10,60},{-300,0,900},{150,-20,40},{0,0,0}};
static const double test_rs_v[TRS_COUNT][3]={{0,0,120},{60,0,-80},{10,90,0},{0,0,100}};
static uint64_t test_rs_id(unsigned i) {return (3ull<<25)|(uint64_t)(1u+i);}
static unsigned char *test_rs_vc(unsigned i) {return test_rs_bullets[i]+RDISP_VC+TRD_BANK*RDISP_VC_STRIDE;}
/* Find's layout: block 0 at +0x58, 32-byte buckets of three (pointers, 16-bit generations at +0x18, nibbles at bit 16+4e). */
static void test_rs_live(unsigned i,int live) {
    const unsigned slot=i,k=slot/3u,e=slot%3u;unsigned char *bucket=test_rs_block+32u*k;uint32_t flags;uint16_t generation=3;
    unsigned char *object=test_rs_bullets[i];memcpy(bucket+8u*e,&object,8);memcpy(bucket+0x18+2u*e,&generation,2);
    memcpy(&flags,bucket+0x1c,4);flags&=~(15u<<(16u+4u*e));if(live) flags|=5u<<(16u+4u*e);memcpy(bucket+0x1c,&flags,4);
}
static void test_rs_pose(unsigned i,double n,rdisp_xf *x) {
    test_rd_xf(x,1,0.2+0.1*i,test_rs_base[i][0]+n*test_rs_v[i][0],test_rs_base[i][1]+n*test_rs_v[i][1],test_rs_base[i][2]+n*test_rs_v[i][2]);
}
static void test_rs_engine(void) {
    memset(test_rs_map,0,sizeof(test_rs_map));memset(test_rs_block,0,sizeof(test_rs_block));memset(test_rs_bullets,0,sizeof(test_rs_bullets));
    memset(test_rs_poses,0,sizeof(test_rs_poses));memset(test_rs_weapon,0,sizeof(test_rs_weapon));memset(test_rs_turret,0,sizeof(test_rs_turret));
    memset(test_rs_gun,0,sizeof(test_rs_gun));
    *(unsigned char**)(image+RSHOT_REGISTRY)=test_rs_map;*(unsigned char**)(test_rs_map+RSHOT_REGISTRY_BLOCKS)=test_rs_block;
    /* The own shot's weapon sits in a turret of the ship slot 3 flies; the other's in another ship. */
    *(unsigned char**)(test_rs_weapon+RSHOT_PARENT)=test_rs_turret;*(unsigned char**)(test_rs_turret+RSHOT_PARENT)=test_rd_objects[TRD_SHIP];
    *(unsigned char**)(test_rs_gun+RSHOT_PARENT)=test_rd_objects[TRD_OTHER];
    for(unsigned i=0;i<TRS_COUNT;i++) {
        unsigned char *b=test_rs_bullets[i];*(unsigned char**)b=image+RSHOT_VTABLE;*(uint64_t*)(b+RDISP_ID)=test_rs_id(i);
        *(unsigned char**)test_rs_vc(i)=i==TRS_FAR?test_rd_objects[TRD_SECTOR]:test_rd_objects[TRD_ZONE];
        *(unsigned char**)(test_rs_vc(i)+RDISP_VC_BLOCK)=test_rs_poses[i];
        *(unsigned char**)(b+RSHOT_SHOOTER)=i==TRS_OTHER?test_rs_gun:test_rs_weapon;
        memset(test_rs_poses[i]+0x40,0x6b,RDISP_BLOCK_BYTES-0x40);
        test_rs_live(i,1);
    }
    pilot_slot_count=3;memset(pilot_slots,0,sizeof(pilot_slots));pilot_slots[2].object=test_rd_objects[TRD_SHIP];
}
static void test_rs_world(double n) {
    for(unsigned i=0;i<TRS_COUNT;i++) {rdisp_xf x;float f[16];test_rs_pose(i,n,&x);test_rd_floats(&x,f);memcpy(test_rs_poses[i],f,64);}
}
static void test_rs_original(void *self) {
    for(unsigned i=0;i<TRS_COUNT;i++) {
        test_rs_seen[i]=*(unsigned char**)(test_rs_vc(i)+RDISP_VC_BLOCK);
        assert(!((uintptr_t)test_rs_seen[i]&15));memcpy(test_rs_seen_pose[i],test_rs_seen[i],64);
        if(test_rs_seen[i]!=test_rs_foreign_block) assert(!memcmp(test_rs_seen[i]+0x40,test_rs_poses[i]+0x40,RDISP_BLOCK_BYTES-0x40));
    }
    if(test_rs_foreign) *(unsigned char**)(test_rs_vc(TRS_OTHER)+RDISP_VC_BLOCK)=test_rs_foreign_block;
    test_rd_original(self);
}
static void test_rs_slot10(int render) {
    volatile uint64_t word=1;
    InterlockedExchange(&rf_frame_open,render);
    render_rng_execute(test_rs_original,NULL,&word,NULL);
    InterlockedExchange(&rf_frame_open,0);
}
static int test_rs_slot10_throwing(void) {
    int caught=0;volatile uint64_t word=1;InterlockedExchange(&rf_frame_open,1);
    __try {render_rng_execute(test_rs_original,NULL,&word,NULL);}
    __except(EXCEPTION_EXECUTE_HANDLER) {caught=1;}
    InterlockedExchange(&rf_frame_open,0);return caught;
}
/* A step: the world at n, slot 3's Moves (the wrapper's notes), slot 10. mask: the bullets that move in it. */
static void test_rs_step(uint64_t tick,double n,unsigned mask) {
    stats.issued=tick;test_rd_world(n);test_rs_world(n);
    for(unsigned i=0;i<TRS_COUNT;i++) if(mask&(1u<<i)) rshot_note(test_rs_bullets[i]);
    test_rs_slot10(0);
}
static void test_rs_render(double s) {test_rd_timing(s);test_rs_slot10(1);}
static int test_rs_engine_blocks(void) {
    for(unsigned i=0;i<TRS_COUNT;i++) if(*(unsigned char**)(test_rs_vc(i)+RDISP_VC_BLOCK)!=test_rs_poses[i]) return 0;
    return 1;
}
static void test_rs_reset(int mode) {
    rshot_mode=mode;rshot_armed=1;rshot_pending=0;rshot_active=1;rshot_failures=0;rshot_stage="armed";rshot_why="-";rshot_open=0;rshot_restored=1;
    rshot_reset();rshot_note_count=0;rshot_gen=0;rshot_swap_count=0;rshot_window=0;
    rshot_noted=rshot_notes_capped=rshot_samples=rshot_sample_skip=rshot_items_max=rshot_capped=rshot_dead=rshot_not_bullet=0;
    rshot_elsewhere=rshot_no_block=rshot_first=rshot_moved=rshot_snaps=rshot_reparented=rshot_frames=rshot_swapped=0;
    rshot_restores=rshot_stale=rshot_gone=rshot_clamped=rshot_own=rshot_probe=rshot_probe_refused=rshot_full=rshot_foreign=rshot_nested=0;
    rshot_lead_last=rshot_lead_max=rshot_disp_max=0;test_rs_foreign=0;
}
/* The own ship drawn ahead by the predictor of slot 3: its simulated pose moved by ahead (as opred_present_enter leaves it). */
static void test_rs_present(double ahead_x) {
    float f[16];memcpy(f,test_rd_blocks[TRD_SHIP],64);f[0]+=(float)ahead_x;
    memcpy(opred_blocks[2],test_rd_blocks[TRD_SHIP],RDISP_BLOCK_BYTES);memcpy(opred_blocks[2],f,64);
    unsigned char *volatile *at=(unsigned char *volatile *)(test_rd_vc(TRD_SHIP)+RDISP_VC_BLOCK);
    *at=(unsigned char*)opred_blocks[2];
    opred_open=1;opred_open_at[2]=at;opred_open_saved[2]=test_rd_blocks[TRD_SHIP];opred_open_object[2]=test_rd_objects[TRD_SHIP];
}
static int test_rs_drawn_at(unsigned i,double n,const double shift[3],double tolerance) {
    rdisp_xf x;test_rs_pose(i,n,&x);if(shift) for(unsigned j=0;j<3;j++) x.t[j]+=shift[j];
    return test_rd_near(test_rs_seen_pose[i],&x,tolerance);
}
static void test_render_shots(void) {
    unsigned saved_hz=gate_tick_hz,saved_fps=rf_fps,saved_count=pilot_slot_count;int saved_mode=render_rng_mode;uint64_t saved_issued=stats.issued;
    unsigned char *saved_registry=*(unsigned char**)(image+RSHOT_REGISTRY);pilot_slot saved_slots[PILOT_SLOTS];memcpy(saved_slots,pilot_slots,sizeof(saved_slots));
    gate_tick_hz=30;rf_fps=60;stats.active=1;render_rng_mode=RENDER_RNG_SPLIT;rf_ready_flag=1;rdisp_is_a_override=test_rd_is_a;
    test_rd_engine();test_rd_reset(RDISP_OFF,RDISP_EXTRAPOLATE);test_rs_engine();test_rs_reset(RSHOT_LEAD);
    const unsigned all=(1u<<TRS_OWN)|(1u<<TRS_OTHER)|(1u<<TRS_FAR);
    /* Two steps (extrapolate: the step frames as simulated); the new bullet appears at the second, one is noted twice. */
    test_rs_step(10,1,all);assert(rshot_samples==1 && rshot_count[rshot_gen]==2 && rshot_first==2 && rshot_elsewhere==1 && !rshot_frames);
    rshot_note(test_rs_bullets[TRS_OWN]);
    test_rs_step(11,2,all|(1u<<TRS_NEW));
    assert(rshot_count[rshot_gen]==3 && rshot_moved==2 && rshot_first==3 && !rshot_frames && test_rs_engine_blocks() && !rshot_note_count);
    /* A render-only frame half a step on: the two moving bullets drawn advanced, the new one as simulated, the far one alone. */
    test_rs_render(0.5);
    assert(rshot_frames==1 && rshot_swapped==2 && rshot_restores==2 && test_rs_engine_blocks() && rshot_check());
    assert(test_rs_drawn_at(TRS_OWN,2.5,NULL,1e-5) && test_rs_drawn_at(TRS_OTHER,2.5,NULL,1e-5));
    assert(test_rs_seen[TRS_NEW]==test_rs_poses[TRS_NEW] && test_rs_seen[TRS_FAR]==test_rs_poses[TRS_FAR] && fabs(rshot_disp_max-60)<1e-2);
    /* The own shot of the ship slot 3 presents (drawn 12 m ahead of its simulated pose): it leaves the drawn guns, the shift
     * fading with its age; the other ship's bullet does not move with it. */
    test_rs_present(12);test_rs_render(0.5);
    {
        const double s=0.5,lead=12.0-s*30.0,age=(1.0+s)/30.0,shift[3]={exp(-age/RSHOT_TAU)*lead,0,0};
        assert(rshot_own==2 && test_rs_drawn_at(TRS_OWN,2.5,shift,1e-5) && test_rs_drawn_at(TRS_OTHER,2.5,NULL,1e-5));
        const double fresh[3]={exp(-s/30.0/RSHOT_TAU)*lead,0,0};   /* the new own bullet: its first step, at its first sample */
        assert(test_rs_drawn_at(TRS_NEW,2,fresh,1e-5) && !rshot_clamped && fabs(rshot_lead_max-fabs(fresh[0]))<1e-3);
    }
    test_rd_follow_unswap();assert(test_rs_engine_blocks() && rshot_check());
    /* 'motion': no lead. */
    rshot_mode=RSHOT_MOTION;test_rs_present(12);test_rs_render(0.5);
    assert(test_rs_drawn_at(TRS_OWN,2.5,NULL,1e-5) && test_rs_seen[TRS_NEW]==test_rs_poses[TRS_NEW] && rshot_own==2);
    test_rd_follow_unswap();rshot_mode=RSHOT_LEAD;
    /* Gone (its registry slot no longer live) and stale (its pose written after the sample): left alone. */
    test_rs_live(TRS_OWN,0);test_rs_poses[TRS_OTHER][0]+=1.0f;
    uint64_t gone=rshot_gone,stale=rshot_stale;test_rs_render(0.5);
    assert(rshot_gone==gone+1 && rshot_stale==stale+1 && test_rs_engine_blocks() && test_rs_seen[TRS_OWN]==test_rs_poses[TRS_OWN]);
    test_rs_live(TRS_OWN,1);test_rs_poses[TRS_OTHER][0]-=1.0f;
    /* An exception in slot 10: the wrapper's __finally puts every pointer back. */
    test_rd_throw=1;assert(test_rs_slot10_throwing() && test_rs_engine_blocks() && !rshot_open && rshot_check());test_rd_throw=0;
    render_rng_failures=0;render_rng_stage="idle";
    /* Fail closed: a foreign pointer where ours was stays and stops the gate (231). */
    test_rs_foreign=1;test_rs_render(0.5);test_rs_foreign=0;
    assert(!rshot_check() && !strcmp(rshot_why,"block_foreign") && rshot_foreign==1 && *(unsigned char**)(test_rs_vc(TRS_OTHER)+RDISP_VC_BLOCK)==test_rs_foreign_block);
    *(unsigned char**)(test_rs_vc(TRS_OTHER)+RDISP_VC_BLOCK)=test_rs_poses[TRS_OTHER];rshot_failures=0;rshot_why="-";rshot_stage="live";
    /* Nested. */
    rshot_window=1;rshot_open=1;assert(!rshot_enter() && rshot_nested==1 && !rshot_check());rshot_open=0;rshot_failures=0;rshot_why="-";
    /* At the sample: a dead bullet, a component that is not a bullet, a teleport (snap), a re-identified bullet. */
    test_rs_step(12,3,all|(1u<<TRS_NEW));
    test_rs_live(TRS_FAR,0);*(unsigned char**)test_rs_bullets[TRS_NEW]=image+0x1234;
    stats.issued=13;test_rd_world(4);test_rs_world(4);
    float tf[16];memcpy(tf,test_rs_poses[TRS_OTHER],64);tf[0]+=5000.0f;memcpy(test_rs_poses[TRS_OTHER],tf,64);
    *(uint64_t*)(test_rs_bullets[TRS_OWN]+RDISP_ID)=test_rs_id(TRS_OWN)+(1ull<<25);
    {   /* the new generation is the live one in the registry */
        uint16_t generation=4;memcpy(test_rs_block+0x18+2u*TRS_OWN,&generation,2);
    }
    for(unsigned i=0;i<TRS_COUNT;i++) rshot_note(test_rs_bullets[i]);
    uint64_t snaps=rshot_snaps,reid=rshot_reparented,dead=rshot_dead,odd=rshot_not_bullet;test_rs_slot10(0);
    assert(rshot_snaps==snaps+1 && rshot_reparented==reid+1 && rshot_dead==dead+1 && rshot_not_bullet==odd+1 && rshot_count[rshot_gen]==2);
    test_rs_render(0.5);assert(test_rs_engine_blocks() && test_rs_seen[TRS_OTHER]==test_rs_poses[TRS_OTHER] && test_rs_seen[TRS_OWN]==test_rs_poses[TRS_OWN]);
    test_rs_engine();
    /* Stage 3's sample missing in a step (not ready): no sample, nothing drawn moved until two samples again. */
    rf_ready_flag=0;test_rs_step(14,5,all);rf_ready_flag=1;assert(rshot_sample_skip==1 && !rshot_valid);
    uint64_t frames=rshot_frames;test_rs_render(0.5);assert(rshot_frames==frames);
    /* Interpolate: the step frame draws the previous step, a render-only frame half way; a bullet's first step is held at its
     * first sample (clamped) where a lead would draw it earlier. */
    test_rd_engine();test_rd_reset(RDISP_OFF,RDISP_INTERPOLATE);test_rs_engine();test_rs_reset(RSHOT_LEAD);
    test_rs_step(20,1,all);test_rs_step(21,2,all);
    assert(rshot_frames==1 && test_rs_drawn_at(TRS_OWN,1,NULL,1e-5) && test_rs_drawn_at(TRS_OTHER,1,NULL,1e-5) && test_rs_engine_blocks());
    test_rs_render(0.5);assert(test_rs_drawn_at(TRS_OWN,1.5,NULL,1e-5) && test_rs_drawn_at(TRS_OTHER,1.5,NULL,1e-5));
    test_rs_step(22,3,all|(1u<<TRS_NEW));test_rs_present(12);test_rs_render(0.5);
    {
        const double s=-0.5,lead=12.0-s*30.0,shift[3]={lead,0,0};   /* first sample: age 0, the whole lead */
        assert(test_rs_drawn_at(TRS_NEW,3,shift,1e-5) && rshot_clamped==1 && test_rs_drawn_at(TRS_OTHER,2.5,NULL,1e-5));
    }
    test_rd_follow_unswap();assert(test_rs_engine_blocks() && rshot_check());
    /* The probe: in a render-only frame one live bullet of the zone 5 m aside along its x axis, through rdisp_override. */
    rshot_mode=RSHOT_PROBE;uint64_t overridden=rdisp_overridden;frames=rshot_frames;test_rs_render(0.5);
    {
        float f[16];memcpy(f,test_rs_poses[TRS_OWN],64);rdisp_xf x;rdisp_xf_from(f,&x);for(unsigned j=0;j<3;j++) x.t[j]+=RSHOT_PROBE_M*x.r[j];
        assert(rshot_probe==1 && rdisp_overridden==overridden+1 && rshot_frames==frames && test_rd_near(test_rs_seen_pose[TRS_OWN],&x,1e-5));
        assert(test_rs_seen[TRS_OTHER]==test_rs_poses[TRS_OTHER] && test_rs_engine_blocks() && rdisp_check() && rshot_check());
    }
    test_rs_step(23,4,all);assert(rshot_probe==1);   /* never in a step frame */
    rshot_mode=RSHOT_LEAD;
    /* Status. */
    char text[1600];
    assert(rshot_status(text,sizeof(text)) && strstr(text,"mode=lead armed=1 active=1 stage=live") && strstr(text," failures=0 restored=1 ") &&
           strstr(text," probe=1 ") && strlen(text)<1400);
    assert(!rshot_status(text,64));
    /* Arming: refused without stage 3; pending before the start, active from the first tick when the pins hold and the bullet
     * Move slot holds the gate's wrapper; afterwards the same value only. */
    int saved_pins=rshot_pins_checked,saved_rdisp=rdisp_pins_checked,saved_installed=bullet_installed;
    unsigned char *saved_entry=*(unsigned char**)(image+bullet_entries[1]);
    rshot_armed=0;rshot_active=0;rshot_mode=RSHOT_OFF;
    assert(rshot_mode_of("lead",4)==RSHOT_LEAD && rshot_mode_of("motion",6)==RSHOT_MOTION && rshot_mode_of("probe",5)==RSHOT_PROBE && !rshot_mode_of("true",4));
    int motion=rdisp_motion_mode;rdisp_motion_mode=RDISP_OFF;assert(!rshot_arm(RSHOT_LEAD,0) && !strcmp(rshot_stage,"refused"));rdisp_motion_mode=motion;
    rshot_pins_checked=1;rdisp_pins_checked=1;bullet_installed=0;
    assert(rshot_arm(RSHOT_LEAD,0) && rshot_pending && !rshot_active);
    assert(!rshot_check() && !strcmp(rshot_why,"bullet_move") && !rshot_active);
    rshot_failures=0;rshot_why="-";assert(rshot_arm(RSHOT_MOTION,0));
    bullet_installed=2;*(unsigned char**)(image+bullet_entries[1])=(unsigned char*)(void*)bullet_move;
    assert(rshot_check() && rshot_active && !strcmp(rshot_stage,"armed") && rshot_arm(RSHOT_MOTION,1) && !rshot_arm(RSHOT_LEAD,1));
    rshot_restore();assert(!rshot_active && rshot_restored && !strcmp(rshot_stage,"stopped"));
    rshot_pins_checked=-1;assert(!rshot_arm(RSHOT_LEAD,0));
    rshot_pins_checked=saved_pins;rdisp_pins_checked=saved_rdisp;bullet_installed=saved_installed;*(unsigned char**)(image+bullet_entries[1])=saved_entry;
    /* Not armed: the wrapper's hook notes nothing. */
    rshot_armed=0;rshot_note_count=0;rshot_note(test_rs_bullets[0]);assert(!rshot_note_count);
    rshot_active=0;rshot_mode=RSHOT_OFF;rshot_stage="off";rshot_why="-";rshot_failures=0;rshot_reset();
    test_rd_follow_unswap();test_rd_follow_counters();opred_stage="off";
    test_rd_reset(RDISP_OFF,RDISP_OFF);rdisp_armed=0;rdisp_active=0;rdisp_stage="off";rdisp_is_a_override=NULL;
    rf_ready_flag=0;rf_gap_first=rf_interval=rf_estimate=rf_frame_start=0;
    *(unsigned char**)(image+RDISP_UNIVERSE)=NULL;*(uint32_t*)(image+RDISP_VIEW_INDEX)=0;*(unsigned char**)(image+RSHOT_REGISTRY)=saved_registry;
    memcpy(pilot_slots,saved_slots,sizeof(saved_slots));pilot_slot_count=saved_count;
    gate_tick_hz=saved_hz;rf_fps=saved_fps;render_rng_mode=saved_mode;stats.issued=saved_issued;stats.active=0;
    render_rng_calls=render_rng_steps=render_rng_unknown=render_rng_ids=0;render_rng_frame=UINT64_MAX;rf_slot10_r_us=rf_slot10_step_us=rf_slot10_r_calls=0;
    puts("render shots: bullets noted by the Move wrapper, sampled live in the camera's zone and drawn at the world's display "
         "time in slot 10, put back by compare-and-swap (extrapolate, interpolate, clamped at the first sample, slot 3's own "
         "shots leaving the drawn guns, motion only, gone, stale, dead, not a bullet, snap, re-identified, noted twice, stage 3 "
         "missing, foreign fails closed, exception unwind, nesting, the 5 m probe through rdisp_override, arming, status) PASS");
}
static void test_render_shots_native_image(const char *path) {
    unsigned char *prior=image;int saved=rshot_pins_checked;
    image=test_pmath_map(path);rshot_pins_checked=0;assert(rshot_pins());
    /* Find: the slot in the low 25 bits, 32 blocks of 2^20 at +0x58, buckets of three (0xaaaaaaab), the nibbles at +0x1c, the
     * generations at +0x18. */
    assert(!memcmp(image+0xced87,"\x25\xff\xff\xff\x01",5) && !memcmp(image+0xced99,"\xc1\xe8\x14",3) && !memcmp(image+0xceda1,"\x48\x8b\x4c\xc1\x58",5));
    assert(!memcmp(image+0xcedb2,"\xb8\xab\xaa\xaa\xaa",5) && !memcmp(image+0xcedd8,"\x44\x8b\x42\x1c",4) && !memcmp(image+0xcedfc,"\x8b\x54\x82\x18",4));
    /* The U::Bullet vtable: Move at +0x1510 (the wrapper's entry), the render queue at +0x15a8; the render walk from it. */
    assert(*(unsigned char**)(image+RSHOT_VTABLE+0x1510u)==image+BULLET_MOVE && image+bullet_entries[1]==image+RSHOT_VTABLE+0x1510u);
    assert(image[0x751896]==0xe8 && 0x75189bu+*(int32_t*)(image+0x751897)==0x3de630u);
    image[0xced88]^=1;rshot_pins_checked=0;assert(!rshot_pins());image[0xced88]^=1;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;rshot_pins_checked=saved;
    puts("render shots on X4.exe: the bullet Move, Find and the render queue pinned with Find's layout and the bullet's vtable slots");
}

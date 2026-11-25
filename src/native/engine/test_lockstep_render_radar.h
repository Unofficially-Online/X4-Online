/* The HUD radar turns with the drawn own ship (lockstep_render_radar.h), on test_lockstep_render_display.h's fake engine,
 * through the real render RNG split wrapper: a radar camera in the universe's list (its controller, holomap and behavior
 * with X4's vtables, the fixed offset, the focus's ID, the centre) whose bank the refill made from the simulated heading;
 * the own ship drawn turned by the predictor of slot 3 and of slot 8 (client k presents slot k), by stage 3, both models
 * (heading, full), observe, the eye of a render-only frame, every refusal (no focus, no zone, model miss), a re-hung camera,
 * foreign writes counted, an exception unwinding, nesting, arming, status; with X4.exe the pins and their layouts. */
static __declspec(align(16)) unsigned char test_rad_camera[0x900],test_rad_controller[0x100],test_rad_holomap[0x340],test_rad_behavior[0x40];
static rdisp_xf test_rad_o;
static double test_rad_centre[3];
static float test_rad_seen[5][16];
static unsigned test_rad_count,test_rad_calls;
static int test_rad_foreign;
static unsigned char *test_rad_table(void) {return test_rad_camera+RDISP_TABLE+TRD_BANK*RDISP_TABLE_STRIDE;}
static float *test_rad_entry(unsigned i) {return (float*)(test_rad_table()+RDISP_TABLE_ENTRIES+i*RDISP_ENTRY+RDISP_ENTRY_POSE);}
/* X4's camera for a frame F at the centre: rows O.r F, translation O.t F + centre. */
static void test_rad_model(const double f[9],rdisp_xf *out) {
    predict_mul(test_rad_o.r,f,out->r);
    for(unsigned j=0;j<3;j++) out->t[j]=test_rad_o.t[0]*f[j]+test_rad_o.t[1]*f[3+j]+test_rad_o.t[2]*f[6+j]+test_rad_centre[j];
}
static void test_rad_pose(rdisp_xf *x,double wx,double wy,double wz,double tx,double ty,double tz) {
    double w[3]={wx,wy,wz};predict_exp(w,x->r);x->t[0]=tx;x->t[1]=ty;x->t[2]=tz;
}
/* The bank as the refill writes it from the camera in the zone mz: anchored in the zone (root, zone, sector) or, with ship,
 * in that ship (root, ship, zone, sector). */
static void test_rad_bank(const rdisp_xf *mz,const rdisp_xf *ship) {
    unsigned char *table=test_rad_table();float f[16];rdisp_xf m[5],inv;
    unsigned char *keys[5];
    if(ship) {
        assert(rdisp_inverse(ship,&inv));rdisp_compose(mz,&inv,&m[1]);m[2]=*mz;rdisp_compose(mz,&test_rd_z,&m[3]);rdisp_compose(&m[3],&test_rd_q,&m[0]);
        keys[0]=NULL;keys[1]=test_rd_objects[TRD_SHIP];keys[2]=test_rd_objects[TRD_ZONE];keys[3]=test_rd_objects[TRD_SECTOR];test_rad_count=4;
    } else {
        m[1]=*mz;rdisp_compose(mz,&test_rd_z,&m[2]);rdisp_compose(&m[2],&test_rd_q,&m[0]);
        keys[0]=NULL;keys[1]=test_rd_objects[TRD_ZONE];keys[2]=test_rd_objects[TRD_SECTOR];test_rad_count=3;
    }
    memset(table,0,RDISP_TABLE_STRIDE);
    *(unsigned char**)table=keys[1];*(uint64_t*)(table+RDISP_TABLE_CAPACITY)=RDISP_TABLE_INLINE;*(uint64_t*)(table+RDISP_TABLE_COUNT)=test_rad_count;
    for(unsigned i=0;i<test_rad_count;i++) {memcpy(table+RDISP_TABLE_ENTRIES+i*RDISP_ENTRY,&keys[i],8);test_rd_floats(&m[i],f);memcpy(test_rad_entry(i),f,64);}
}
/* The radar: camera+0x20 the controller (+0x90 the holomap, +0xa0 the offset), the holomap (+0x2c0 the behavior, +0x2e0 the
 * focus, +0x320 the centre), X4's vtables; second in the universe's list after the player camera. */
static void test_rad_radar(uint64_t focus) {
    memset(test_rad_camera,0,sizeof(test_rad_camera));memset(test_rad_controller,0,sizeof(test_rad_controller));
    memset(test_rad_holomap,0,sizeof(test_rad_holomap));memset(test_rad_behavior,0,sizeof(test_rad_behavior));
    *(unsigned char**)test_rad_controller=image+RRADAR_CONTROLLER_VT;*(unsigned char**)(test_rad_controller+RRADAR_HOLOMAP)=test_rad_holomap;
    float of[16];test_rd_floats(&test_rad_o,of);memcpy(test_rad_controller+RRADAR_OFFSET,of,64);
    *(unsigned char**)test_rad_holomap=image+RRADAR_HOLOMAP_VT;*(unsigned char**)(test_rad_holomap+RRADAR_BEHAVIOR)=test_rad_behavior;
    *(uint64_t*)(test_rad_holomap+RRADAR_FOCUS)=focus;
    float c[4]={(float)test_rad_centre[0],(float)test_rad_centre[1],(float)test_rad_centre[2],0};memcpy(test_rad_holomap+RRADAR_CENTRE,c,16);
    *(unsigned char**)test_rad_behavior=image+RRADAR_BEHAVIOR_VT;
    *(unsigned char**)(test_rad_camera+RRADAR_CONTROLLER)=test_rad_controller;
    memset(test_rad_camera+RDISP_EYE,0x44,RDISP_EYE_BYTES);
    *(unsigned char**)(test_rd_universe+RDISP_CAMERA_LIST)=test_rd_camera;*(unsigned char**)(test_rd_universe+RDISP_CAMERA_LIST+8)=test_rad_camera;
    *(uint64_t*)(test_rd_universe+RDISP_CAMERA_LIST_COUNT)=2;
}
static void test_rad_original(void *self) {
    ++test_rad_calls;
    for(unsigned i=0;i<test_rad_count;i++) memcpy(test_rad_seen[i],test_rad_entry(i),64);
    memset(test_rad_camera+RDISP_EYE,0xcd,RDISP_EYE_BYTES);   /* the radar's eye, from its bank */
    if(test_rad_foreign) test_rad_entry(1)[0]+=1.0f;
    test_rd_original(self);
}
static void test_rad_slot10(int render) {
    volatile uint64_t word=1;
    InterlockedExchange(&rf_frame_open,render);
    render_rng_execute(test_rad_original,NULL,&word,NULL);
    InterlockedExchange(&rf_frame_open,0);
}
static int test_rad_slot10_throwing(void) {
    int caught=0;volatile uint64_t word=1;InterlockedExchange(&rf_frame_open,1);
    __try {render_rng_execute(test_rad_original,NULL,&word,NULL);}
    __except(EXCEPTION_EXECUTE_HANDLER) {caught=1;}
    InterlockedExchange(&rf_frame_open,0);return caught;
}
/* Slot j+1's predictor draws the ship at the pose (as opred_present_enter leaves it). */
static void test_rad_present(unsigned j,const rdisp_xf *drawn) {
    float pf[16];test_rd_floats(drawn,pf);
    memcpy(opred_blocks[j],test_rd_blocks[TRD_SHIP],RDISP_BLOCK_BYTES);memcpy(opred_blocks[j],pf,64);
    unsigned char *volatile *at=(unsigned char *volatile *)(test_rd_vc(TRD_SHIP)+RDISP_VC_BLOCK);
    *at=(unsigned char*)opred_blocks[j];
    opred_open=1;opred_open_at[j]=at;opred_open_saved[j]=test_rd_blocks[TRD_SHIP];opred_open_object[j]=test_rd_objects[TRD_SHIP];
}
static void test_rad_unpresent(void) {
    *(unsigned char**)(test_rd_vc(TRD_SHIP)+RDISP_VC_BLOCK)=test_rd_blocks[TRD_SHIP];
    opred_open=0;memset(opred_open_at,0,sizeof(opred_open_at));memset(opred_open_object,0,sizeof(opred_open_object));
}
static void test_rad_counters(void) {
    rradar_calls=rradar_idle=rradar_found=rradar_no_focus=rradar_table=rradar_frame_miss=rradar_level=rradar_identity=0;
    rradar_yaw=rradar_full_model=rradar_miss=rradar_rehung=rradar_written=rradar_restores=rradar_eye=rradar_foreign=0;
    rradar_cached=rradar_full=rradar_nested=0;rradar_residual_last=rradar_residual_max=rradar_centre_err_max=rradar_d_last=rradar_d_max=0;
    memset(&rradar_d,0,sizeof(rradar_d));memset(&rradar_seen,0,sizeof(rradar_seen));
}
static void test_rad_reset(int mode) {
    rradar_mode=mode;rradar_armed=1;rradar_pending=0;rradar_active=1;rradar_failures=0;rradar_stage="armed";rradar_why="-";
    rradar_open=0;rradar_camera_count=0;rradar_restored=1;test_rad_counters();test_rad_calls=0;test_rad_foreign=0;
}
/* The ship's simulated pose (its engine block) and the bank the refill makes from it (yaw: Y(psi_S); full: R_S). */
static void test_rad_sim(const rdisp_xf *s,int full,const rdisp_xf *anchor_ship) {
    float f[16];test_rd_floats(s,f);memcpy(test_rd_blocks[TRD_SHIP],f,64);
    double frame[9];rdisp_xf mz;
    if(full) memcpy(frame,s->r,sizeof(frame));else assert(rradar_heading(s,frame));
    test_rad_model(frame,&mz);test_rad_bank(&mz,anchor_ship?s:NULL);
}
static int test_rad_bank_is(unsigned char before[5][64]) {
    for(unsigned i=0;i<test_rad_count;i++) if(memcmp(test_rad_entry(i),before[i],64)) return 0;
    return 1;
}
static void test_rad_keep(unsigned char before[5][64]) {for(unsigned i=0;i<test_rad_count;i++) memcpy(before[i],test_rad_entry(i),64);}
/* What the fake slot 10 saw against X4's camera for the drawn pose (frame F_D at the same centre). */
static int test_rad_saw_model(const double fd[9],int anchored_in_ship,const rdisp_xf *drawn) {
    rdisp_xf mz,sector,root;test_rad_model(fd,&mz);rdisp_compose(&mz,&test_rd_z,&sector);rdisp_compose(&sector,&test_rd_q,&root);
    const unsigned z=anchored_in_ship?2:1;
    if(!test_rd_near(test_rad_seen[z],&mz,1e-4) || !test_rd_near(test_rad_seen[z+1],&sector,1e-4) || !test_rd_near(test_rad_seen[0],&root,1e-4)) return 0;
    if(anchored_in_ship) {   /* the camera in the drawn ship: its pose in the zone through the drawn ship is M_Z' */
        rdisp_xf in_ship,through;float f[16];memcpy(f,test_rad_seen[1],64);rdisp_xf_from(f,&in_ship);rdisp_compose(&in_ship,drawn,&through);
        if(!test_rd_near(test_rad_seen[2],&through,1e-4)) return 0;
    }
    return 1;
}
static void test_render_radar(void) {
    unsigned saved_hz=gate_tick_hz,saved_fps=rf_fps;int saved_mode=render_rng_mode;uint64_t saved_issued=stats.issued;
    gate_tick_hz=30;rf_fps=0;stats.active=1;render_rng_mode=RENDER_RNG_SPLIT;rf_ready_flag=1;rdisp_is_a_override=test_rd_is_a;
    test_rd_engine();test_rd_reset(RDISP_OFF,RDISP_OFF);rdisp_armed=0;rdisp_active=0;test_rd_follow_counters();test_rd_cut_counters();
    stats.issued=50;test_rd_world(2);
    test_rd_xf(&test_rad_o,0,-0.6,0,800,-1200);
    rdisp_xf s,d;test_rad_pose(&s,0.1,0.3,0,100,0,50);test_rad_pose(&d,0.12,0.42,0.02,108,1,52);
    test_rad_centre[0]=100;test_rad_centre[1]=0;test_rad_centre[2]=50;
    test_rad_radar(0x1000+TRD_SHIP);test_rad_sim(&s,0,NULL);test_rad_reset(RRADAR_ROTATE);
    unsigned char before[5][64];test_rad_keep(before);
    double ys[9],yd[9];assert(rradar_heading(&s,ys) && rradar_heading(&d,yd));
    /* Nothing drawn displaced: nothing to do. */
    test_rad_slot10(0);assert(test_rad_calls==1 && rradar_idle==1 && rradar_found==1 && rradar_seen.index==1 && rradar_seen.count==3 && !rradar_seen.ship && !rradar_written && test_rad_bank_is(before));
    /* Slot 3's predictor draws the ship turned (a step frame): the radar's bank is X4's camera for the drawn heading while
     * slot 10 runs, and the refill's afterwards, bit for bit; the eye stays what slot 10 made of it. */
    test_rad_present(2,&d);test_rad_slot10(0);
    assert(rradar_found==2 && rradar_yaw==1 && rradar_written==1 && rradar_restores==1 && !rradar_eye && !rradar_miss);
    assert(test_rad_saw_model(yd,0,NULL) && test_rad_bank_is(before) && rradar_residual_max<1e-5 && rradar_centre_err_max<1e-3);
    assert(fabs(rradar_d_last-predict_angle(ys,yd))<1e-5 && rradar_d_last>0.1);
    unsigned char eye[RDISP_EYE_BYTES];memset(eye,0xcd,sizeof(eye));assert(!memcmp(test_rad_camera+RDISP_EYE,eye,sizeof(eye)));
    assert(rradar_seen.index==1 && rradar_seen.camera==test_rad_camera && rradar_seen.zone_at==1 && rradar_seen.ship==test_rd_objects[TRD_SHIP]);
    assert(rradar_check() && !rradar_open && test_rad_calls==2);
    /* A render-only frame: the eye slot 10 recomputed goes back too. */
    memset(test_rad_camera+RDISP_EYE,0x44,RDISP_EYE_BYTES);memset(eye,0x44,sizeof(eye));
    test_rd_timing(0.5);test_rad_slot10(1);
    assert(test_rad_saw_model(yd,0,NULL) && test_rad_bank_is(before) && !memcmp(test_rad_camera+RDISP_EYE,eye,sizeof(eye)) && rradar_eye==1);
    /* Slot 8 presents (client 8): its own ship's drawn heading, the same way. */
    test_rad_unpresent();test_rad_present(7,&d);test_rad_slot10(0);
    assert(rradar_written==3 && rradar_restores==3 && test_rad_saw_model(yd,0,NULL) && test_rad_bank_is(before));
    /* observe: every check and the turn, no write. */
    test_rad_reset(RRADAR_OBSERVE);test_rad_slot10(0);
    assert(rradar_yaw==1 && rradar_d.n==1 && !rradar_written && !memcmp(test_rad_seen[1],before[1],64) && test_rad_bank_is(before));
    test_rad_reset(RRADAR_ROTATE);
    /* The zone's entry is not the model (another camera, a branch not modelled): nothing written. */
    {
        rdisp_xf odd,mz;float f[16];memcpy(f,test_rad_entry(1),64);rdisp_xf_from(f,&mz);test_rad_pose(&odd,0.05,0,0,0,0,0);rdisp_compose(&odd,&mz,&mz);
        test_rd_floats(&mz,f);memcpy(test_rad_entry(1),f,64);test_rad_keep(before);
        test_rad_slot10(0);assert(rradar_miss==1 && !rradar_written && rradar_residual_last>RRADAR_MODEL_TOL && test_rad_bank_is(before));
        assert(!memcmp(test_rad_seen[1],before[1],64));
    }
    /* The full model (the behavior's other axis: the ship's whole rotation): the bank of R_S, drawn R_D. */
    test_rad_reset(RRADAR_ROTATE);test_rad_sim(&s,1,NULL);test_rad_keep(before);test_rad_slot10(0);
    assert(rradar_full_model==1 && !rradar_yaw && rradar_written==1 && test_rad_saw_model(d.r,0,NULL) && test_rad_bank_is(before));
    /* A ship looking straight up: no heading (level), the full model still holds. */
    {
        rdisp_xf up,up_d;test_rad_pose(&up,-1.5707963267948966,0,0,100,0,50);test_rad_pose(&up_d,-1.5707963267948966+0.05,0.02,0,101,0,50);
        test_rad_unpresent();test_rad_present(2,&up_d);test_rad_reset(RRADAR_ROTATE);test_rad_sim(&up,1,NULL);test_rad_keep(before);
        test_rad_slot10(0);assert(rradar_level==1 && rradar_full_model==1 && test_rad_saw_model(up_d.r,0,NULL) && test_rad_bank_is(before));
        test_rad_unpresent();test_rad_present(2,&d);
    }
    /* Refusals: the focus is another ship (its ship drawn as simulated); the zone not in the camera's chain. */
    test_rad_reset(RRADAR_ROTATE);test_rad_sim(&s,0,NULL);test_rad_keep(before);
    *(uint64_t*)(test_rad_holomap+RRADAR_FOCUS)=0x1000+TRD_OTHER;test_rad_slot10(0);
    assert(rradar_found==1 && rradar_no_focus==1 && !rradar_written && test_rad_bank_is(before));
    *(uint64_t*)(test_rad_holomap+RRADAR_FOCUS)=0x1000+TRD_SHIP;
    {unsigned char *station=test_rd_objects[TRD_STATION];memcpy(test_rad_table()+RDISP_TABLE_ENTRIES+RDISP_ENTRY,&station,8);}*(unsigned char**)test_rad_table()=test_rd_objects[TRD_STATION];
    test_rad_slot10(0);assert(rradar_frame_miss==1 && !rradar_written);
    test_rad_sim(&s,0,NULL);test_rad_keep(before);
    /* Not a radar (another controller's vtable): never looked at. */
    *(unsigned char**)test_rad_controller=image+0x1234;test_rad_slot10(0);assert(rradar_found==2 && !rradar_written);
    *(unsigned char**)test_rad_controller=image+RRADAR_CONTROLLER_VT;
    /* A camera anchored in the own ship, re-hung by rdisp_others first: the zone's entry still ends at X4's camera for the
     * drawn heading, the camera's entry in the ship consistent with it through the drawn ship; both leaves restore. */
    test_rad_reset(RRADAR_ROTATE);test_rd_cut_counters();test_rad_sim(&s,0,&s);test_rad_keep(before);test_rad_slot10(0);
    assert(rradar_rehung==1 && rradar_written==1 && rdisp_oth_written==1 && rdisp_oth_restored==1 && !rdisp_oth_foreign);
    assert(rradar_seen.zone_at==2 && test_rad_saw_model(yd,1,&d) && test_rad_bank_is(before));
    test_rad_sim(&s,0,NULL);test_rad_keep(before);
    /* A foreign write into the bank while slot 10 runs: it stays, counted, the gate goes on (X4's own bank). */
    test_rad_reset(RRADAR_ROTATE);test_rad_foreign=1;test_rad_slot10(0);test_rad_foreign=0;
    assert(rradar_foreign==1 && !rradar_restores && memcmp(test_rad_entry(1),before[1],64) && rradar_check() && !rradar_failures);
    test_rad_sim(&s,0,NULL);test_rad_keep(before);
    /* An exception in slot 10: the wrapper's __finally puts the bank back. */
    test_rd_throw=1;assert(test_rad_slot10_throwing());test_rd_throw=0;
    assert(test_rad_bank_is(before) && rradar_restores==1 && !rradar_open);render_rng_failures=0;render_rng_stage="idle";
    /* Status: it names the camera. */
    char text[1600];
    assert(rradar_status(text,sizeof(text)) && strstr(text,"mode=rotate armed=1 active=1 stage=live") && strstr(text," model_last=yaw") &&
           strstr(text," camera=1:") && strstr(text," count=3 chain=0,0 ") && strstr(text," failures=0 restored=1 ") && strlen(text)<1400);
    assert(!rradar_status(text,64));
    /* Stage 3 draws the ship (no predictor): extrapolate, the ship turning 0.05 rad a step; a render-only frame half a step on. */
    test_rad_unpresent();test_rd_reset(RDISP_OFF,RDISP_EXTRAPOLATE);rf_fps=60;test_rad_reset(RRADAR_ROTATE);
    for(unsigned n=1;n<=2;n++) {
        stats.issued=60+n;test_rd_world(n);rdisp_xf sn;test_rad_pose(&sn,0,0.3+0.05*n,0,100+30*n,0,50);
        test_rad_centre[0]=sn.t[0];test_rad_centre[2]=sn.t[2];test_rad_radar(0x1000+TRD_SHIP);test_rad_sim(&sn,0,NULL);test_rad_slot10(0);
    }
    assert(!rradar_written && rradar_idle==2);   /* extrapolate: the step frames are drawn as simulated */
    test_rad_keep(before);test_rd_timing(0.5);test_rad_slot10(1);
    {
        rdisp_xf half;test_rad_pose(&half,0,0.3+0.125,0,0,0,0);double yh[9];assert(rradar_heading(&half,yh));
        assert(rdisp_swapped>=1 && rradar_yaw==1 && rradar_written==1 && rradar_eye==1 && test_rad_bank_is(before));
        assert(test_rad_saw_model(yh,0,NULL) && fabs(rradar_d_last-0.025)<1e-6);
    }
    /* Nested: an open call fails closed (230). */
    rradar_open=1;assert(!rradar_enter() && rradar_nested==1 && !rradar_check() && !strcmp(rradar_why,"nested"));
    rradar_open=0;rradar_failures=0;rradar_why="-";rradar_stage="armed";
    /* Arming: pending before the start (no engine read), active from the first tick when the pins hold; afterwards the same
     * value only; pins that do not hold stop (230, why=pins). */
    int saved_pins=rradar_pins_checked,saved_rdisp=rdisp_pins_checked;
    rradar_armed=0;rradar_active=0;rradar_mode=RRADAR_OFF;
    assert(rradar_mode_of("rotate",6)==RRADAR_ROTATE && rradar_mode_of("observe",7)==RRADAR_OBSERVE && !rradar_mode_of("true",4) && !rradar_mode_of(NULL,0));
    assert(!rradar_arm(RRADAR_OFF,0) && !rradar_armed);
    rradar_pins_checked=1;rdisp_pins_checked=1;
    assert(rradar_arm(RRADAR_ROTATE,0) && rradar_pending && !rradar_active && !strcmp(rradar_stage,"pending"));
    assert(rradar_check() && rradar_active && !strcmp(rradar_stage,"armed"));
    assert(rradar_arm(RRADAR_ROTATE,1) && !rradar_arm(RRADAR_OBSERVE,1));
    rradar_restore();assert(!rradar_active && rradar_restored && !strcmp(rradar_stage,"stopped"));
    rradar_pins_checked=-1;assert(!rradar_arm(RRADAR_ROTATE,0) && !strcmp(rradar_stage,"refused"));
    rradar_pins_checked=0;rdisp_pins_checked=-1;rradar_failures=0;rradar_why="-";assert(rradar_arm(RRADAR_ROTATE,0));
    assert(!rradar_check() && !rradar_active && !strcmp(rradar_why,"pins"));
    rradar_pins_checked=saved_pins;rdisp_pins_checked=saved_rdisp;
    rradar_armed=0;rradar_active=0;rradar_mode=RRADAR_OFF;rradar_stage="off";rradar_why="-";rradar_failures=0;test_rad_counters();
    test_rd_follow_unswap();test_rd_follow_counters();opred_stage="off";test_rd_cut_counters();
    test_rd_reset(RDISP_OFF,RDISP_OFF);rdisp_armed=0;rdisp_active=0;rdisp_stage="off";
    memset(test_rd_universe,0,sizeof(test_rd_universe));
    rdisp_is_a_override=NULL;rf_ready_flag=0;rf_gap_first=rf_interval=rf_estimate=rf_frame_start=0;
    *(unsigned char**)(image+RDISP_UNIVERSE)=NULL;*(uint32_t*)(image+RDISP_VIEW_INDEX)=0;
    gate_tick_hz=saved_hz;rf_fps=saved_fps;render_rng_mode=saved_mode;stats.issued=saved_issued;stats.active=0;
    render_rng_calls=render_rng_steps=render_rng_unknown=render_rng_ids=0;render_rng_frame=UINT64_MAX;rf_slot10_r_us=rf_slot10_step_us=rf_slot10_r_calls=0;
    puts("render radar: the radar camera of the universe's list turned to the drawn heading in slot 10 and put back bit for bit "
         "(predictor of slot 3 and slot 8, stage 3, heading and full models, a level ship, observe, the render-only eye, no focus, "
         "no zone, model miss, not a radar, re-hung by rdisp_others, foreign counted, exception unwind, nesting, arming, status) PASS");
}
static void test_render_radar_native_image(const char *path) {
    unsigned char *prior=image;int saved=rradar_pins_checked;
    image=test_pmath_map(path);rradar_pins_checked=0;assert(rradar_pins());
    /* The controller's transform: the holomap +0x90, its behavior +0x2c0 and focus +0x2e0, Find, the reference (behavior +0x68),
     * the centre +0x320, the axis test (behavior +0xc0), atan2 and sincos of the heading, the offset +0xa0..+0xd0. */
    assert(!memcmp(image+0xda1fa6,"\x48\x8b\x81\x90\x00\x00\x00",7) && !memcmp(image+0xda1fad,"\x48\x8b\x88\xc0\x02\x00\x00",7));
    assert(!memcmp(image+0xda1fd5,"\x48\x8b\x90\xe0\x02\x00\x00",7) && image[0xda1ff2]==0xe8 && 0xda1ff7u+*(int32_t*)(image+0xda1ff3)==0xced70u);
    assert(!memcmp(image+0xda2028,"\x41\xff\x51\x68",4) && !memcmp(image+0xda2092,"\x0f\x10\x80\x20\x03\x00\x00",7));
    assert(!memcmp(image+0xda20ad,"\xff\x90\xc0\x00\x00\x00",6) && 0xda20eau+*(int32_t*)(image+0xda20e6)==0x1a1a398u && 0xda20fcu+*(int32_t*)(image+0xda20f8)==0x1750210u);
    assert(!memcmp(image+0xda21b6,"\x0f\x10\x8b\xa0\x00\x00\x00",7) && !memcmp(image+0xda21d1,"\x0f\x10\xa3\xd0\x00\x00\x00",7));
    /* The axis X4's default branch keeps: (0, -1, 0). */
    {float axis[4];memcpy(axis,image+0x2cc0d40,16);assert(axis[0]==0.0f && axis[1]==-1.0f && axis[2]==0.0f);}
    image[0xda1fae]^=1;rradar_pins_checked=0;assert(!rradar_pins());image[0xda1fae]^=1;
    unsigned char *slot=*(unsigned char**)(image+RRADAR_CONTROLLER_VT+49u*8u);*(unsigned char**)(image+RRADAR_CONTROLLER_VT+49u*8u)=image+0x1234;
    rradar_pins_checked=0;assert(!rradar_pins());*(unsigned char**)(image+RRADAR_CONTROLLER_VT+49u*8u)=slot;rradar_pins_checked=0;assert(rradar_pins());
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;rradar_pins_checked=saved;
    puts("render radar on X4.exe: the controller's transform, GetOffset, the behavior's reference and axis test pinned with their layouts and vtable slots");
}

/* Display transforms between two steps (lockstep_render_display.h): pose arithmetic, the display offset,
 * arming, the step's sample and the slot 10 swaps on fake engine memory through the real render RNG split
 * wrapper (extrapolate and interpolate, the camera bank following the camera and a displaced own ship, a
 * predictor's own swap and the override, stale, snapped and re-identified objects, foreign writes failing
 * closed, an exception unwinding, nesting), status and rows; with X4.exe the pins and the layouts they hold. */
enum {TRD_SHIP,TRD_ZONE,TRD_SECTOR,TRD_OTHER,TRD_STATION,TRD_ELSEWHERE,TRD_COUNT};
#define TRD_BANK 1u
static __declspec(align(16)) unsigned char test_rd_universe[0x400],test_rd_camera[0x900],test_rd_objects[TRD_COUNT][0x200];
static __declspec(align(16)) unsigned char test_rd_blocks[TRD_COUNT][RDISP_BLOCK_BYTES],test_rd_predicted[RDISP_BLOCK_BYTES];
static unsigned char *test_rd_children[8];
static rdisp_xf test_rd_c,test_rd_s,test_rd_z,test_rd_q,test_rd_o;
/* What the fake slot 10 saw. */
static unsigned char *test_rd_seen_block[TRD_COUNT];
static float test_rd_seen_pose[TRD_COUNT][16],test_rd_seen_entries[4][16];
static int test_rd_foreign_block,test_rd_foreign_entry,test_rd_throw;
static unsigned test_rd_calls;
static unsigned char *test_rd_vc(unsigned which) {return test_rd_objects[which]+RDISP_VC+TRD_BANK*RDISP_VC_STRIDE;}
static int test_rd_is_a(void *object,int cls) {return cls==RDISP_ZONE_CLASS && object==test_rd_objects[TRD_ZONE];}
static void test_rd_rot(int axis,double angle,double r[9]) {double w[3]={0,0,0};w[axis]=angle;predict_exp(w,r);}
static void test_rd_xf(rdisp_xf *x,int axis,double angle,double tx,double ty,double tz) {test_rd_rot(axis,angle,x->r);x->t[0]=tx;x->t[1]=ty;x->t[2]=tz;}
static void test_rd_floats(const rdisp_xf *x,float f[16]) {for(unsigned i=0;i<4;i++) f[4*i+3]=7.0f+(float)i;rdisp_xf_to(x,f);}
static int test_rd_near(const float *f,const rdisp_xf *x,double tolerance) {
    float g[16];test_rd_floats(x,g);
    for(unsigned i=0;i<16;i++) if(fabs((double)f[i]-(double)g[i])>tolerance*(1.0+fabs((double)g[i]))) return 0;
    return 1;
}
static float *test_rd_entry(unsigned i) {return (float*)(test_rd_camera+RDISP_TABLE+TRD_BANK*RDISP_TABLE_STRIDE+RDISP_TABLE_ENTRIES+i*RDISP_ENTRY+RDISP_ENTRY_POSE);}
/* The world at step n: the own ship (the camera's anchor) and another ship move, a station stands, the camera turns in the ship. */
static void test_rd_poses(double n,rdisp_xf *c,rdisp_xf *s,rdisp_xf *o) {
    test_rd_xf(c,1,0.02*n,0,1.5,3);test_rd_xf(s,2,0.01*n,100+30*n,0,50);test_rd_xf(o,1,0.05*n,500+20*n,10*n,0);
}
static void test_rd_world(double n) {
    test_rd_poses(n,&test_rd_c,&test_rd_s,&test_rd_o);
    test_rd_xf(&test_rd_z,0,0.3,1000,2000,3000);
    test_rd_xf(&test_rd_q,2,0,1e5,0,0);
    float f[16];
    test_rd_floats(&test_rd_s,f);memcpy(test_rd_blocks[TRD_SHIP],f,64);
    test_rd_floats(&test_rd_o,f);memcpy(test_rd_blocks[TRD_OTHER],f,64);
    test_rd_floats(&test_rd_z,f);memcpy(test_rd_blocks[TRD_ZONE],f,64);
    /* The view bank as the refill writes it: root, anchor, its parents. */
    unsigned char *table=test_rd_camera+RDISP_TABLE+TRD_BANK*RDISP_TABLE_STRIDE;
    rdisp_xf m[4];m[1]=test_rd_c;rdisp_compose(&m[1],&test_rd_s,&m[2]);rdisp_compose(&m[2],&test_rd_z,&m[3]);rdisp_compose(&m[3],&test_rd_q,&m[0]);
    unsigned char *keys[4]={NULL,test_rd_objects[TRD_SHIP],test_rd_objects[TRD_ZONE],test_rd_objects[TRD_SECTOR]};
    for(unsigned i=0;i<4;i++) {memcpy(table+RDISP_TABLE_ENTRIES+i*RDISP_ENTRY,&keys[i],8);test_rd_floats(&m[i],f);memcpy(test_rd_entry(i),f,64);}
}
static void test_rd_engine(void) {
    memset(test_rd_universe,0,sizeof(test_rd_universe));memset(test_rd_camera,0,sizeof(test_rd_camera));memset(test_rd_objects,0,sizeof(test_rd_objects));
    memset(test_rd_blocks,0,sizeof(test_rd_blocks));
    *(unsigned char**)(image+RDISP_UNIVERSE)=test_rd_universe;*(unsigned char**)(test_rd_universe+RDISP_CAMERA)=test_rd_camera;
    *(uint32_t*)(image+RDISP_VIEW_INDEX)=TRD_BANK;
    unsigned char *table=test_rd_camera+RDISP_TABLE+TRD_BANK*RDISP_TABLE_STRIDE;
    *(unsigned char**)table=test_rd_objects[TRD_SHIP];*(uint64_t*)(table+RDISP_TABLE_CAPACITY)=RDISP_TABLE_INLINE;*(uint64_t*)(table+RDISP_TABLE_COUNT)=4;
    unsigned char *parents[TRD_COUNT]={test_rd_objects[TRD_ZONE],test_rd_objects[TRD_SECTOR],NULL,test_rd_objects[TRD_ZONE],test_rd_objects[TRD_ZONE],test_rd_objects[TRD_SECTOR]};
    for(unsigned i=0;i<TRD_COUNT;i++) {
        *(uint64_t*)(test_rd_objects[i]+RDISP_ID)=0x1000+i;
        *(unsigned char**)test_rd_vc(i)=parents[i];*(unsigned char**)(test_rd_vc(i)+RDISP_VC_BLOCK)=test_rd_blocks[i];
    }
    float f[16];rdisp_xf station;test_rd_xf(&station,0,0.7,-400,0,90);test_rd_floats(&station,f);memcpy(test_rd_blocks[TRD_STATION],f,64);
    memcpy(test_rd_blocks[TRD_ELSEWHERE],f,64);memcpy(test_rd_blocks[TRD_SECTOR],f,64);
    for(unsigned i=0;i<TRD_COUNT;i++) memset(test_rd_blocks[i]+0x40,0x5a,RDISP_BLOCK_BYTES-0x40); /* scale and velocity: copied as they are */
    unsigned order[4]={TRD_SHIP,TRD_OTHER,TRD_STATION,TRD_ELSEWHERE};
    for(unsigned i=0;i<4;i++) test_rd_children[i]=test_rd_objects[order[i]];
    unsigned char **children=(unsigned char**)(test_rd_vc(TRD_ZONE)+RDISP_VC_CHILDREN);children[0]=(unsigned char*)test_rd_children;children[1]=(unsigned char*)(test_rd_children+4);
    memset(test_rd_camera+RDISP_EYE,0x33,RDISP_EYE_BYTES);
}
static void test_rd_original(void *self) {
    (void)self;++test_rd_calls;
    for(unsigned i=0;i<TRD_COUNT;i++) {
        test_rd_seen_block[i]=*(unsigned char**)(test_rd_vc(i)+RDISP_VC_BLOCK);
        assert(!((uintptr_t)test_rd_seen_block[i]&15));memcpy(test_rd_seen_pose[i],test_rd_seen_block[i],64);
        assert(!memcmp(test_rd_seen_block[i]+0x40,test_rd_blocks[i]+0x40,RDISP_BLOCK_BYTES-0x40));
    }
    for(unsigned i=0;i<4;i++) memcpy(test_rd_seen_entries[i],test_rd_entry(i),64);
    memset(test_rd_camera+RDISP_EYE,0xab,RDISP_EYE_BYTES);   /* the render's eye, from the bank */
    if(test_rd_foreign_block) *(unsigned char**)(test_rd_vc(TRD_OTHER)+RDISP_VC_BLOCK)=test_rd_predicted;
    if(test_rd_foreign_entry) test_rd_entry(2)[0]+=1.0f;
    if(test_rd_throw) RaiseException(0xe0525232,0,0,NULL);
}
/* Slot 10 that touches no fake object (the native-image boot point). */
static void test_rd_slot10_only(void *self) {(void)self;++test_rd_calls;}
static void test_rd_slot10(int render) {
    volatile uint64_t word=1;
    InterlockedExchange(&rf_frame_open,render);
    render_rng_execute(test_rd_original,NULL,&word,NULL);
    InterlockedExchange(&rf_frame_open,0);
}
static int test_rd_slot10_throwing(void) {
    int caught=0;volatile uint64_t word=1;InterlockedExchange(&rf_frame_open,1);
    __try {render_rng_execute(test_rd_original,NULL,&word,NULL);}
    __except(EXCEPTION_EXECUTE_HANDLER) {caught=1;}
    InterlockedExchange(&rf_frame_open,0);return caught;
}
static void test_rd_step(uint64_t tick,double n) {stats.issued=tick;test_rd_world(n);test_rd_slot10(0);}
/* A render-only frame at display offset s (time since the step's presentation, in steps). */
static void test_rd_timing(double s) {
    int64_t step=rf_freq()/30;rf_interval=step/2;rf_estimate=step/4;rf_gap_first=1000000;
    rf_frame_start=rf_gap_first+(int64_t)(s*(double)step)-rf_interval;
}
static void test_rd_render(double s) {test_rd_timing(s);test_rd_slot10(1);}
static int test_rd_engine_blocks(void) {
    for(unsigned i=0;i<TRD_COUNT;i++) if(*(unsigned char**)(test_rd_vc(i)+RDISP_VC_BLOCK)!=test_rd_blocks[i]) return 0;
    return 1;
}
static void test_rd_reset(int camera,int motion) {
    rdisp_camera_mode=camera;rdisp_motion_mode=motion;rdisp_armed=1;rdisp_pending=0;rdisp_active=1;rdisp_failures=0;rdisp_stage="armed";rdisp_why="-";
    rdisp_open=0;rdisp_window=0;rdisp_swap_count=0;rdisp_cam_open_count=0;rdisp_override_count=0;rdisp_owns=NULL;rdisp_restored=1;
    rdisp_cam_reset();rdisp_items_reset();rdisp_gen=0;
    rdisp_samples=rdisp_swapped=rdisp_restores=rdisp_owned=rdisp_overridden=rdisp_override_refused=rdisp_stale=rdisp_snaps=rdisp_reparented=0;
    rdisp_cam_written_n=rdisp_cam_restored=rdisp_cam_identity=rdisp_cam_stale=rdisp_chain=rdisp_eye_restored=rdisp_foreign=rdisp_nested=0;
    rdisp_frames=rdisp_step_frames=rdisp_render_frames=0;rdisp_row_seq=0;
    memset(&rdisp_err_m,0,sizeof(rdisp_err_m));memset(&rdisp_err_rad,0,sizeof(rdisp_err_rad));
    test_rd_foreign_block=test_rd_foreign_entry=test_rd_throw=0;test_rd_calls=0;
}
static void test_rd_math(void) {
    rdisp_xf a,b,c,inv,id,back;test_rd_xf(&a,2,0.4,1,2,3);test_rd_xf(&b,0,-0.7,-5,6,0.5);
    assert(rdisp_inverse(&a,&inv));rdisp_compose(&a,&inv,&id);assert(rdisp_is_identity(&id));
    rdisp_compose(&a,&b,&c);rdisp_inverse(&b,&inv);rdisp_compose(&c,&inv,&back);
    for(unsigned i=0;i<9;i++) assert(fabs(back.r[i]-a.r[i])<1e-12);
    for(unsigned i=0;i<3;i++) assert(fabs(back.t[i]-a.t[i])<1e-12);
    rdisp_xf singular;memset(&singular,0,sizeof(singular));assert(!rdisp_inverse(&singular,&inv));
    /* Constant motion: a -> b -> c, the motion of (a, b) reproduces a at -1, b at 0 and c at 1. */
    rdisp_xf p0,p1,p2,out;double dp[3],w[3];
    test_rd_xf(&p0,1,0.10,10,0,0);test_rd_xf(&p1,1,0.15,12,1,0);test_rd_xf(&p2,1,0.20,14,2,0);
    rdisp_motion_of(&p0,&p1,dp,w);assert(fabs(w[1]-0.05)<1e-12 && fabs(dp[0]-2)<1e-12);
    rdisp_advance(&p1,dp,w,-1,&out);for(unsigned i=0;i<9;i++) assert(fabs(out.r[i]-p0.r[i])<1e-12);assert(fabs(out.t[0]-10)<1e-12);
    rdisp_advance(&p1,dp,w,1,&out);for(unsigned i=0;i<9;i++) assert(fabs(out.r[i]-p2.r[i])<1e-12);assert(fabs(out.t[1]-2)<1e-12);
    rdisp_stat st,sr;memset(&st,0,sizeof(st));memset(&sr,0,sizeof(sr));rdisp_error(&p0,dp,w,&p2,&st,&sr);
    assert(st.n==1 && fabs(st.max-sqrt(4.0+1.0))<1e-9 && sr.n==1 && fabs(sr.max-0.05)<1e-9); /* p2 is two steps on, not one */
    memset(&sr,0,sizeof(sr));memset(&st,0,sizeof(st));rdisp_error(&p1,dp,w,&p2,&st,&sr);assert(st.max<1e-9 && sr.max<1e-6 && st.bucket[0]==1);
    float f[16]={0};f[7]=NAN;assert(rdisp_finite(f)); /* a w lane */f[1]=INFINITY;assert(!rdisp_finite(f));
    assert(rdisp_mode_of("extrapolate",11)==RDISP_EXTRAPOLATE && rdisp_mode_of("interpolate",11)==RDISP_INTERPOLATE);
    assert(!rdisp_mode_of("interpolat",10) && !rdisp_mode_of(NULL,0) && !rdisp_mode_of("true",4));
}
static void test_rd_offset(void) {
    unsigned saved_hz=gate_tick_hz;gate_tick_hz=30;
    int64_t step=rf_freq()/30;rf_gap_first=5000;rf_interval=step/2;rf_estimate=step/4;
    rf_frame_start=rf_gap_first;assert(fabs(rdisp_time_offset()-0.5)<1e-5);          /* presented one interval after the step */
    rf_estimate=step*3/4;assert(fabs(rdisp_time_offset()-0.75)<1e-5);                 /* slower frames: the measured period */
    rf_frame_start=rf_gap_first+step;assert(rdisp_time_offset()==1.0);                 /* late: held at the next step */
    rf_frame_start=rf_gap_first-2*step;assert(rdisp_time_offset()==0.0);
    gate_tick_hz=0;assert(rdisp_time_offset()==0.0);
    gate_tick_hz=saved_hz;rf_gap_first=rf_interval=rf_estimate=rf_frame_start=0;
}
static void test_rd_arm(void) {
    unsigned saved_fps=rf_fps;int saved_pins=rdisp_pins_checked;
    rdisp_armed=0;rdisp_camera_mode=rdisp_motion_mode=RDISP_OFF;
    rf_fps=0;rdisp_pins_checked=1;assert(!rdisp_arm(1,RDISP_EXTRAPOLATE,0) && !rdisp_armed && !strcmp(rdisp_stage,"refused"));
    rf_fps=60;rdisp_pins_checked=-1;assert(!rdisp_arm(1,RDISP_EXTRAPOLATE,0) && !rdisp_armed);   /* known not to hold */
    rdisp_pins_checked=1;assert(!rdisp_arm(1,RDISP_OFF,0) && !rdisp_arm(0,7,0) && !rdisp_armed);
    /* Armed pending: active only from the gate's first tick (rdisp_check, the pins). */
    assert(rdisp_arm(1,RDISP_EXTRAPOLATE,0) && rdisp_armed && rdisp_camera_mode==RDISP_EXTRAPOLATE && !rdisp_motion_mode);
    assert(rdisp_pending && !rdisp_active && !strcmp(rdisp_stage,"pending"));
    assert(rdisp_arm(0,RDISP_INTERPOLATE,0) && rdisp_motion_mode==RDISP_INTERPOLATE && rdisp_pending && !rdisp_active);
    assert(rdisp_check() && !rdisp_pending && rdisp_active && !strcmp(rdisp_stage,"armed") && rdisp_check());
    /* After the start only the same value. */
    assert(rdisp_arm(0,RDISP_INTERPOLATE,1) && !rdisp_arm(0,RDISP_EXTRAPOLATE,1) && rdisp_motion_mode==RDISP_INTERPOLATE && rdisp_active);
    rdisp_armed=0;rdisp_active=0;rdisp_camera_mode=rdisp_motion_mode=RDISP_OFF;rdisp_stage="off";rf_fps=saved_fps;rdisp_pins_checked=saved_pins;
}
/* The live crash of R3/R4 (2026-11-01, 0xC0000005 right after RENDER_FRAMES_ARMED): the probe arms gate.render_camera and
 * gate.render_motion in its arming pass, after gate.render_frames and before gate.boot, so X4.exe is not verified and image is
 * NULL; the arm hashed its pins at image+0x979ef0.. there. Through the Lua binding with that state (it must not read engine
 * memory), then the boot point after the start: the first tick (pins), loading frames and a first step without a camera,
 * a camera with an empty bank, until the camera and the bank exist. */
static const char *test_rd_lua_mode;
static double test_rd_lua_result;
static const char *test_rd_lua_string(lua_State *L,int index,size_t *length) {
    (void)L;assert(index==1 && length);*length=test_rd_lua_mode?strlen(test_rd_lua_mode):0;return test_rd_lua_mode;
}
static void test_rd_lua_push(lua_State *L,double value) {(void)L;test_rd_lua_result=value;}
static int test_rd_lua_arm(int camera,const char *mode) {
    int faulted=0;test_rd_lua_mode=mode;test_rd_lua_result=-1;
    __try {if(camera) lua_render_camera(NULL);else lua_render_motion(NULL);}
    __except(EXCEPTION_EXECUTE_HANDLER) {faulted=1;}
    assert(!faulted);return (int)test_rd_lua_result;
}
static void test_rd_boot_point(void) {
    unsigned char *verified=image;unsigned saved_hz=gate_tick_hz,saved_fps=rf_fps;int saved_pins=rdisp_pins_checked,saved_used=used,saved_mode=render_rng_mode;
    uint64_t saved_issued=stats.issued;uint32_t saved_active=stats.active;
    const char *(*old_string)(lua_State *,int,size_t *)=p_lua_tolstring;void (*old_push)(lua_State *,double)=p_lua_pushnumber;
    p_lua_tolstring=test_rd_lua_string;p_lua_pushnumber=test_rd_lua_push;
    test_rd_reset(RDISP_OFF,RDISP_OFF);rdisp_armed=0;rdisp_active=0;rdisp_stage="off";rdisp_sample_fail=0;
    /* The arming pass: render frames armed, the gate not started, X4.exe not verified. */
    image=NULL;used=0;stats.active=0;stats.issued=0;gate_tick_hz=30;rf_fps=60;rdisp_pins_checked=0;
    assert(test_rd_lua_arm(1,"extrapolate")==1 && test_rd_lua_arm(0,"extrapolate")==1);
    assert(rdisp_armed && rdisp_pending && !rdisp_active && !rdisp_pins_checked && !strcmp(rdisp_stage,"pending"));
    assert(test_rd_lua_arm(1,"sideways")==0 && test_rd_lua_arm(0,NULL)==0 && rdisp_camera_mode==RDISP_EXTRAPOLATE);
    /* The start verified X4.exe. Its first tick checks the pins: bytes that are not X4.exe 9.00's stop the gate (183, why=pins). */
    image=verified;used=1;stats.active=1;render_rng_mode=RENDER_RNG_SPLIT;rdisp_is_a_override=test_rd_is_a;
    assert(!rdisp_check() && !rdisp_active && !rdisp_pending && !strcmp(rdisp_why,"pins") && !strcmp(rdisp_stage,"failed"));
    assert(rdisp_pins_checked==-1 && test_rd_lua_arm(1,"extrapolate")==1);   /* after the start: the same value only */
    used=0;assert(test_rd_lua_arm(1,"extrapolate")==0 && !strcmp(rdisp_stage,"refused"));used=1;
    /* With the pinned bytes (the native-image test hashes the real ones): active from the first tick. */
    rdisp_failures=0;rdisp_why="-";rdisp_pins_checked=1;used=0;
    assert(test_rd_lua_arm(1,"extrapolate")==1 && test_rd_lua_arm(0,"extrapolate")==1 && !rdisp_active);used=1;
    test_rd_engine();
    *(unsigned char**)(image+RDISP_UNIVERSE)=NULL;               /* no universe camera yet */
    assert(rdisp_check() && rdisp_active && !strcmp(rdisp_stage,"armed"));
    /* Loading frames before tick 0: slot 10 runs, the boundary's preconditions do not hold yet. */
    rf_ready_flag=0;test_rd_calls=0;
    test_rd_slot10(0);test_rd_render(0.5);
    assert(test_rd_calls==2 && !rdisp_samples && !rdisp_frames && !rdisp_cam.valid && test_rd_engine_blocks() && rdisp_check());
    /* A first step without a camera, then a camera with an empty bank: nothing sampled, nothing written, no failure. */
    rf_ready_flag=1;stats.issued=1;test_rd_slot10(0);test_rd_render(0.5);
    *(unsigned char**)(image+RDISP_UNIVERSE)=test_rd_universe;*(unsigned char**)(test_rd_universe+RDISP_CAMERA)=NULL;
    stats.issued=2;test_rd_slot10(0);test_rd_render(0.5);
    *(unsigned char**)(test_rd_universe+RDISP_CAMERA)=test_rd_camera;
    unsigned char *table=test_rd_camera+RDISP_TABLE+TRD_BANK*RDISP_TABLE_STRIDE;unsigned char saved_table[RDISP_TABLE_STRIDE];
    memcpy(saved_table,table,sizeof(saved_table));memset(table,0,RDISP_TABLE_STRIDE);*(uint64_t*)(table+RDISP_TABLE_CAPACITY)=RDISP_TABLE_INLINE;
    stats.issued=3;test_rd_slot10(0);test_rd_render(0.5);
    assert(rdisp_samples==3 && rdisp_sample_fail==3 && !rdisp_cam.valid && !rdisp_items_valid && !rdisp_frames && !rdisp_cam_written_n);
    assert(test_rd_engine_blocks() && rdisp_check() && !rdisp_open && test_rd_calls==8);
    /* The camera and its bank exist: two steps, then a render-only frame draws the camera and the objects moved on. */
    memcpy(table,saved_table,sizeof(saved_table));
    test_rd_step(4,1);test_rd_step(5,2);test_rd_render(0.5);
    assert(rdisp_samples==5 && rdisp_render_frames==1 && rdisp_cam_written_n==1 && rdisp_cam_restored==1 && test_rd_engine_blocks() && rdisp_check());
    rdisp_restore();assert(!rdisp_active && rdisp_restored);
    rdisp_armed=0;rdisp_active=0;rdisp_camera_mode=rdisp_motion_mode=RDISP_OFF;rdisp_stage="off";rdisp_why="-";rdisp_failures=0;rdisp_is_a_override=NULL;
    rdisp_cam_reset();rdisp_items_reset();rf_ready_flag=0;rf_gap_first=rf_interval=rf_estimate=rf_frame_start=0;rdisp_sample_fail=0;
    *(unsigned char**)(image+RDISP_UNIVERSE)=NULL;*(uint32_t*)(image+RDISP_VIEW_INDEX)=0;
    p_lua_tolstring=old_string;p_lua_pushnumber=old_push;
    gate_tick_hz=saved_hz;rf_fps=saved_fps;rdisp_pins_checked=saved_pins;used=saved_used;render_rng_mode=saved_mode;
    stats.issued=saved_issued;stats.active=saved_active;
    render_rng_calls=render_rng_steps=render_rng_unknown=render_rng_ids=0;render_rng_frame=UINT64_MAX;rf_slot10_r_us=rf_slot10_step_us=rf_slot10_r_calls=0;
}
static int test_rd_owns(const void *object) {return object==test_rd_objects[TRD_SHIP];}
static void test_rd_flow(void) {
    unsigned saved_hz=gate_tick_hz,saved_fps=rf_fps;int saved_mode=render_rng_mode;uint64_t saved_issued=stats.issued;
    gate_tick_hz=30;rf_fps=60;stats.active=1;render_rng_mode=RENDER_RNG_SPLIT;rf_ready_flag=1;rdisp_is_a_override=test_rd_is_a;
    test_rd_engine();test_rd_reset(RDISP_EXTRAPOLATE,RDISP_EXTRAPOLATE);
    /* Two steps: the first is history only, the second has the motion; extrapolate leaves step frames as simulated. */
    test_rd_step(10,1);assert(rdisp_samples==1 && rdisp_item_count[rdisp_gen]==3 && rdisp_zone==test_rd_objects[TRD_ZONE] && !rdisp_frames);
    test_rd_step(11,2);assert(rdisp_samples==2 && !rdisp_frames && test_rd_engine_blocks() && test_rd_calls==2);
    assert(test_rd_seen_block[TRD_OTHER]==test_rd_blocks[TRD_OTHER] && rdisp_items[rdisp_gen][1].moved && !rdisp_items[rdisp_gen][2].moved);
    /* A render-only frame half a step on: the other ship and the own ship drawn advanced, the station as it is;
     * the camera turned on in the ship, the bank above the ship following its drawn pose. */
    unsigned char before[4][64];for(unsigned i=0;i<4;i++) memcpy(before[i],test_rd_entry(i),64);
    unsigned char eye[RDISP_EYE_BYTES];memcpy(eye,test_rd_camera+RDISP_EYE,sizeof(eye));
    test_rd_render(0.5);
    assert(test_rd_calls==3 && rdisp_frames==1 && rdisp_render_frames==1 && fabs(rdisp_s_last-0.5)<1e-5);
    assert(test_rd_seen_block[TRD_OTHER]!=test_rd_blocks[TRD_OTHER] && test_rd_seen_block[TRD_SHIP]!=test_rd_blocks[TRD_SHIP]);
    assert(test_rd_seen_block[TRD_STATION]==test_rd_blocks[TRD_STATION] && test_rd_seen_block[TRD_ELSEWHERE]==test_rd_blocks[TRD_ELSEWHERE]);
    rdisp_xf o,s,c,m2,m3,m0;test_rd_poses(2.5,&c,&s,&o);
    assert(test_rd_near(test_rd_seen_pose[TRD_OTHER],&o,1e-5) && test_rd_near(test_rd_seen_pose[TRD_SHIP],&s,1e-5));
    rdisp_compose(&c,&s,&m2);rdisp_compose(&m2,&test_rd_z,&m3);rdisp_compose(&m3,&test_rd_q,&m0);
    assert(test_rd_near(test_rd_seen_entries[1],&c,1e-5) && test_rd_near(test_rd_seen_entries[2],&m2,1e-5));
    assert(test_rd_near(test_rd_seen_entries[3],&m3,1e-5) && test_rd_near(test_rd_seen_entries[0],&m0,1e-5));
    /* Everything back: the engine's pointers, the bank bit for bit, the eye slot 10 recomputed. */
    assert(test_rd_engine_blocks() && rdisp_swapped==2 && rdisp_restores==2 && rdisp_cam_written_n==1 && rdisp_cam_restored==1 && rdisp_chain==1);
    for(unsigned i=0;i<4;i++) assert(!memcmp(test_rd_entry(i),before[i],64));
    assert(!memcmp(test_rd_camera+RDISP_EYE,eye,sizeof(eye)) && rdisp_eye_restored==1 && rdisp_check() && !rdisp_open && !rdisp_swap_count);
    /* The next step: the extrapolation error of the constant-motion world is measured (zero here, rows exact). */
    test_rd_step(12,3);assert(rdisp_err_m.n==2 && rdisp_err_m.max<1e-3 && rdisp_cam_err_m.n==1 && rdisp_cam_err_rad.max<1e-5);
    /* Rows and status. */
    char text[2048];uint64_t next=0;rdisp_rows_on=1;test_rd_render(0.25);
    assert(rdisp_row_text(0,text,sizeof(text),&next) && next==1 && strstr(text,"seq=0 tick=12 kind=render") && strstr(text," s=0.2500 ") &&
           strstr(text," moved=2 swapped=2 stale=0 table=1"));
    assert(!rdisp_row_text(1,text,sizeof(text),&next));
    assert(rdisp_status(text,sizeof(text)) && strstr(text,"camera=extrapolate motion=extrapolate armed=1") && strstr(text," failures=0 restored=1") &&
           strstr(text," swapped=4 restores=4 ") && strstr(text," zone=1 items=3 ") && strlen(text)<1900);
    assert(!rdisp_status(text,64));
    /* A predictor's own swap of the own ship (inside predict_present_enter): stage 3 leaves it, the bank follows it. */
    uint64_t owned=rdisp_owned;
    stats.issued=12;test_rd_timing(0.5);InterlockedExchange(&rf_frame_open,1);rdisp_s_time=0;
    rdisp_sample();assert(rdisp_window && fabs(rdisp_display_offset()-0.5)<1e-5);
    rdisp_xf predicted;test_rd_xf(&predicted,2,0.05,250,4,50);float pf[16];memcpy(test_rd_predicted,test_rd_blocks[TRD_SHIP],RDISP_BLOCK_BYTES);
    test_rd_floats(&predicted,pf);memcpy(test_rd_predicted,pf,64);
    *(unsigned char**)(test_rd_vc(TRD_SHIP)+RDISP_VC_BLOCK)=test_rd_predicted;
    rdisp_owns=test_rd_owns;
    int entered=rdisp_enter();assert(entered && rdisp_owned==owned+1 && *(unsigned char**)(test_rd_vc(TRD_SHIP)+RDISP_VC_BLOCK)==test_rd_predicted);
    test_rd_poses(3.5,&c,&s,&o);rdisp_compose(&c,&predicted,&m2);
    assert(test_rd_near(test_rd_entry(1),&c,1e-5) && test_rd_near(test_rd_entry(2),&m2,1e-5));
    rdisp_leave(entered);InterlockedExchange(&rf_frame_open,0);
    *(unsigned char**)(test_rd_vc(TRD_SHIP)+RDISP_VC_BLOCK)=test_rd_blocks[TRD_SHIP];rdisp_owns=NULL;assert(test_rd_engine_blocks() && rdisp_check());
    /* The override: the layer swaps a given pose in; refused outside the window. */
    float packed[12];for(unsigned i=0;i<4;i++) memcpy(packed+3*i,pf+4*i,12); /* translation, x, y, z: 3 floats each */
    assert(!rdisp_override(test_rd_objects[TRD_STATION],packed));
    InterlockedExchange(&rf_frame_open,1);rdisp_sample();
    assert(rdisp_override(test_rd_objects[TRD_STATION],packed) && rdisp_override(test_rd_objects[TRD_STATION],packed) && rdisp_override_count==1);
    float bad[12]={0};bad[4]=NAN;assert(!rdisp_override(test_rd_objects[TRD_OTHER],bad));
    entered=rdisp_enter();unsigned char *shown=*(unsigned char**)(test_rd_vc(TRD_STATION)+RDISP_VC_BLOCK);
    assert(entered && shown!=test_rd_blocks[TRD_STATION] && test_rd_near((float*)shown,&predicted,1e-6) && rdisp_overridden==1);
    rdisp_leave(entered);InterlockedExchange(&rf_frame_open,0);assert(test_rd_engine_blocks() && rdisp_check());
    /* A block written after the sample is left alone (stale). */
    test_rd_blocks[TRD_OTHER][0]^=1;uint64_t stale=rdisp_stale;test_rd_render(0.5);
    assert(rdisp_stale==stale+1 && test_rd_seen_block[TRD_OTHER]==test_rd_blocks[TRD_OTHER]);test_rd_blocks[TRD_OTHER][0]^=1;
    /* An exception in slot 10: the wrapper's __finally puts everything back. */
    test_rd_throw=1;assert(test_rd_slot10_throwing() && test_rd_engine_blocks() && !rdisp_open && rdisp_check());test_rd_throw=0;
    render_rng_failures=0;render_rng_stage="idle";
    /* A teleport (beyond 1000 m a step) and a re-identified object are drawn as simulated. */
    test_rd_step(13,4);stats.issued=14;test_rd_world(5);
    float tf[16];memcpy(tf,test_rd_blocks[TRD_OTHER],64);tf[0]+=5000.0f;memcpy(test_rd_blocks[TRD_OTHER],tf,64);
    *(uint64_t*)(test_rd_objects[TRD_STATION]+RDISP_ID)=0x9999;
    uint64_t snaps=rdisp_snaps,reid=rdisp_reparented;test_rd_slot10(0);assert(rdisp_snaps==snaps+1 && rdisp_reparented==reid+1);
    test_rd_render(0.5);assert(test_rd_seen_block[TRD_OTHER]==test_rd_blocks[TRD_OTHER] && test_rd_seen_block[TRD_SHIP]!=test_rd_blocks[TRD_SHIP]);
    *(uint64_t*)(test_rd_objects[TRD_STATION]+RDISP_ID)=0x1000+TRD_STATION;
    /* Fail closed: a foreign pointer where ours was, a foreign write into the bank; the foreign value stays. */
    test_rd_step(15,6);test_rd_step(16,7);
    test_rd_foreign_block=1;test_rd_render(0.5);test_rd_foreign_block=0;
    assert(!rdisp_check() && !strcmp(rdisp_why,"block_foreign") && rdisp_foreign==1 &&
           *(unsigned char**)(test_rd_vc(TRD_OTHER)+RDISP_VC_BLOCK)==test_rd_predicted);
    *(unsigned char**)(test_rd_vc(TRD_OTHER)+RDISP_VC_BLOCK)=test_rd_blocks[TRD_OTHER];rdisp_failures=0;rdisp_why="-";rdisp_stage="live";
    test_rd_foreign_entry=1;memcpy(before[2],test_rd_entry(2),64);test_rd_render(0.5);test_rd_foreign_entry=0;
    assert(!rdisp_check() && !strcmp(rdisp_why,"camera_foreign") && memcmp(test_rd_entry(2),before[2],64) && test_rd_engine_blocks());
    rdisp_failures=0;rdisp_why="-";
    /* Nested entry. */
    rdisp_window=1;rdisp_open=1;assert(!rdisp_enter() && rdisp_nested==1 && !rdisp_check());rdisp_open=0;rdisp_failures=0;rdisp_why="-";
    /* Interpolate: the step frame draws the previous step, render-only frames half way; the camera likewise. */
    test_rd_engine();test_rd_reset(RDISP_INTERPOLATE,RDISP_INTERPOLATE);
    test_rd_step(20,1);test_rd_step(21,2);
    test_rd_poses(1,&c,&s,&o);
    assert(rdisp_step_frames==1 && test_rd_near(test_rd_seen_pose[TRD_OTHER],&o,1e-5) && test_rd_near(test_rd_seen_pose[TRD_SHIP],&s,1e-5));
    assert(test_rd_near(test_rd_seen_entries[1],&c,1e-5) && test_rd_engine_blocks());
    test_rd_render(0.5);test_rd_poses(1.5,&c,&s,&o);
    assert(fabs(rdisp_s_last+0.5)<1e-5 && test_rd_near(test_rd_seen_pose[TRD_OTHER],&o,1e-5) && test_rd_near(test_rd_seen_entries[1],&c,1e-5));
    assert(test_rd_engine_blocks() && rdisp_check());
    /* Camera only: no object is swapped, the bank turns with the camera alone. */
    test_rd_engine();test_rd_reset(RDISP_EXTRAPOLATE,RDISP_OFF);
    test_rd_step(30,1);test_rd_step(31,2);test_rd_render(0.5);
    assert(test_rd_seen_block[TRD_OTHER]==test_rd_blocks[TRD_OTHER] && test_rd_seen_block[TRD_SHIP]==test_rd_blocks[TRD_SHIP] && !rdisp_swapped);
    test_rd_poses(2.5,&c,&s,&o);rdisp_compose(&c,&test_rd_s,&m2);
    assert(test_rd_near(test_rd_seen_entries[1],&c,1e-5) && test_rd_near(test_rd_seen_entries[2],&m2,1e-5) && !rdisp_chain);
    /* Not ready (the last boundary's preconditions), off, or stopped: nothing at all. */
    rf_ready_flag=0;test_rd_step(32,3);assert(!rdisp_cam.valid);rf_ready_flag=1;
    uint64_t frames=rdisp_frames;test_rd_render(0.5);assert(rdisp_frames==frames);
    rdisp_restore();assert(!rdisp_active && rdisp_restored && !strcmp(rdisp_stage,"stopped"));
    test_rd_step(33,4);test_rd_step(34,5);test_rd_render(0.5);assert(rdisp_frames==frames && test_rd_engine_blocks());
    rdisp_armed=0;rdisp_active=0;rdisp_camera_mode=rdisp_motion_mode=RDISP_OFF;rdisp_stage="off";rdisp_rows_on=0;rdisp_is_a_override=NULL;
    rdisp_cam_reset();rdisp_items_reset();rf_ready_flag=0;rf_gap_first=rf_interval=rf_estimate=rf_frame_start=0;
    *(unsigned char**)(image+RDISP_UNIVERSE)=NULL;*(uint32_t*)(image+RDISP_VIEW_INDEX)=0;
    gate_tick_hz=saved_hz;rf_fps=saved_fps;render_rng_mode=saved_mode;stats.issued=saved_issued;stats.active=0;
    render_rng_calls=render_rng_steps=render_rng_unknown=render_rng_ids=0;render_rng_frame=UINT64_MAX;rf_slot10_r_us=rf_slot10_step_us=rf_slot10_r_calls=0;
}
static void test_render_display(void) {
    test_rd_math();test_rd_offset();test_rd_arm();test_rd_boot_point();test_rd_flow();
    puts("render display: pose arithmetic, display offset, arming (through the Lua binding before the start, image NULL: no engine "
         "read; pins on the first tick), the boot point (no camera, empty bank), extrapolate and interpolate on fake engine memory "
         "through the render wrapper (objects swapped and restored, the camera bank following camera and own ship, predictor swap and "
         "override, stale, snap, re-identified, foreign writes fail closed, exception unwind, nesting) PASS");
}
static void test_render_display_native_image(const char *path) {
    unsigned char *prior=image;int saved=rdisp_pins_checked;
    image=test_pmath_map(path);rdisp_pins_checked=0;assert(rdisp_pins());
    int32_t d;
    /* The bank reader: the MoveWorker's index, entries at +0x40 of camera+bank*0x2b0, capacity and count, 0x50 a step, rows from +0x10. */
    memcpy(&d,image+0x979f1c,4);assert(0x979f20u+(uint32_t)d==RDISP_VIEW_INDEX);
    assert(!memcmp(image+0x979f20,"\x48\x69\xd8\xb0\x02\x00\x00",7) && !memcmp(image+0x979f2a,"\x4c\x8d\x4b\x40",4));
    assert(*(uint32_t*)(image+0x979f31)==RDISP_TABLE_CAPACITY-RDISP_TABLE_ENTRIES && image[0x979f35]==RDISP_TABLE_INLINE);
    assert(*(uint32_t*)(image+0x979f4f)==RDISP_TABLE_COUNT-RDISP_TABLE_ENTRIES && image[0x979f6b]==RDISP_ENTRY && image[0x979f74]==RDISP_TABLE);
    /* The refill: the table base camera+0x30+index*0x2b0, the anchor from camera+0x770 stored at the table's start. */
    assert(!memcmp(image+0x979d0e,"\x48\x69\xd0\xb0\x02\x00\x00\x48\x83\xc2\x30",11) && *(uint32_t*)(image+0x979a9d)==0x770);
    /* The eye: camera+0x890..0x8cf. */
    assert(*(uint32_t*)(image+0x983640)==RDISP_EYE && *(uint32_t*)(image+0x983674)==RDISP_EYE+0x30);
    /* PropagateCacheFlags: the children vector at vc+0x10, the 0x70-byte, 16-aligned pose block at vc+0x28. */
    assert(!memcmp(image+0x3d4794,"\x48\x8d\x53\x10",4) && !memcmp(image+0x3d4842,"\x8d\x4e\x70",3) && *(uint32_t*)(image+0x3d483e)==0x10);
    /* The render walk: the block from vc+0x28, aligned mulps from it. */
    assert(!memcmp(image+0x3de8af,"\x48\x8b\x46\x28",4) && !memcmp(image+0x3de8cd,"\x0f\x59\x40\x20",4));
    image[0x9799f5]^=1;rdisp_pins_checked=0;assert(!rdisp_pins());image[0x9799f5]^=1;
    /* The arm path against these bytes: armed pending before the start (no read), the first tick hashes the pins and
     * activates; then the boot point as the file holds it (g_Universe and the bank index zero: no camera yet) through the
     * render wrapper, a step frame and a render-only frame: sample_fail, nothing written. A tampered pin stops (why=pins). */
    unsigned saved_hz=gate_tick_hz,saved_fps=rf_fps;int saved_used=used,saved_mode=render_rng_mode;uint64_t saved_issued=stats.issued;
    gate_tick_hz=30;rf_fps=60;used=0;rdisp_pins_checked=0;
    rdisp_armed=0;rdisp_active=0;rdisp_camera_mode=rdisp_motion_mode=RDISP_OFF;rdisp_failures=0;rdisp_why="-";
    assert(rdisp_arm(1,RDISP_EXTRAPOLATE,0) && rdisp_arm(0,RDISP_EXTRAPOLATE,0) && rdisp_pending && !rdisp_active && !rdisp_pins_checked);
    used=1;stats.active=1;stats.issued=1;render_rng_mode=RENDER_RNG_SPLIT;rf_ready_flag=1;
    assert(rdisp_check() && rdisp_pins_checked==1 && rdisp_active && !strcmp(rdisp_stage,"armed"));
    assert(*(uint64_t*)(image+RDISP_UNIVERSE)==0 && *(uint32_t*)(image+RDISP_VIEW_INDEX)==0);
    uint64_t samples=rdisp_samples,fails=rdisp_sample_fail,frames=rdisp_frames;unsigned calls=test_rd_calls;
    {volatile uint64_t word=1;render_rng_execute(test_rd_slot10_only,NULL,&word,NULL);}
    test_rd_timing(0.5);{volatile uint64_t word=1;InterlockedExchange(&rf_frame_open,1);render_rng_execute(test_rd_slot10_only,NULL,&word,NULL);InterlockedExchange(&rf_frame_open,0);}
    assert(test_rd_calls==calls+2 && rdisp_samples==samples+1 && rdisp_sample_fail==fails+1 && rdisp_frames==frames && !rdisp_cam.valid && !rdisp_open && rdisp_check());
    rdisp_restore();
    used=0;rdisp_pins_checked=0;image[0x9835a5]^=1;
    assert(rdisp_arm(1,RDISP_EXTRAPOLATE,0));used=1;
    assert(!rdisp_check() && !rdisp_active && !strcmp(rdisp_why,"pins") && rdisp_pins_checked==-1);
    image[0x9835a5]^=1;rdisp_restore();
    rdisp_armed=0;rdisp_active=0;rdisp_camera_mode=rdisp_motion_mode=RDISP_OFF;rdisp_stage="off";rdisp_why="-";rdisp_failures=0;
    rdisp_cam_reset();rdisp_items_reset();rf_ready_flag=0;rf_gap_first=rf_interval=rf_estimate=rf_frame_start=0;
    gate_tick_hz=saved_hz;rf_fps=saved_fps;used=saved_used;render_rng_mode=saved_mode;stats.issued=saved_issued;stats.active=0;
    render_rng_calls=render_rng_steps=render_rng_unknown=render_rng_ids=0;render_rng_frame=UINT64_MAX;rf_slot10_r_us=rf_slot10_step_us=rf_slot10_r_calls=0;
    render_rng_frame_calls=render_rng_frame_steps=render_rng_frame_ids=render_rng_entry=render_rng_exit=0;render_rng_stage="idle";
    rdisp_samples=rdisp_sample_fail=rdisp_sample_us=rdisp_sample_us_sum=rdisp_sample_us_max=0;test_rd_calls=0;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;rdisp_pins_checked=saved;
    puts("render display on X4.exe: the view-bank reader and refill, the eye, PropagateCacheFlags and the render walk pinned with their layouts; "
         "armed pending before the start, pins and activation on the first tick, the boot point without a camera");
}

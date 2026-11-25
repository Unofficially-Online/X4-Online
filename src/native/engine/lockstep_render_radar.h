/* The HUD radar turns with the drawn own ship, DEFAULT ON (Windows gate, stop reason 230). Display only.
 *   native_gate_render_radar = true ('rotate') | 'observe' (every check and the status, no write)
 * Found 24 Nov (player 2: the radar stuttered while the ship turned, as if it updated too rarely; read in
 * player 2's log and X4.exe 9.00). The radar is drawn every frame in slot 10 (GameClient::vfunc2 0x122bfc0 renders every
 * camera of the universe's list +0x2c0, count +0x3c0) by a camera of that list whose movement controller (camera+0x20) is a
 * U::FixedOffsetHoloMapCameraMovementController (vtable 0x2c2fb80): +0x90 its U::HoloMap (0x2c2cee0), whose +0x2c0 is a
 * U::RadarBehavior (0x2c2eca0) and +0x2e0 the focus's runtime ID (the ship the player sits in). Its transform (vfunc49
 * 0xda1f50, read in the binary): Find 0xced70 for the focus, the behavior's reference (vfunc13 0xdf9f10, the focus's zone),
 * the focus's transform in it at the game time (0x3db8a0), its translation replaced by the map centre holomap+0x320 (x, y, z
 * floats); unless the behavior's axis is not (0, -1, 0) (vfunc24 0xdfa040) only the heading is kept: psi = atan2(-z.x, z.z)
 * of the z row (0x1a1a398), the frame Y(psi) with the rows (cos, 0, sin), (0, 1, 0), (-sin, 0, cos) (sincos 0x1750210 of
 * -psi); the camera is the fixed offset O (controller+0xa0: translation, x, y, z rows) in that frame: rows O.r Y, translation
 * O.t Y + centre. The camera's view bank (camera+0x30+A*0x2b0, as lockstep_render_display.h reads the player camera's) is
 * refilled from it in slot 3 only (0x9799f0 -> Camera::GetOffset 0x97a300 -> the controller). So the radar turned with the
 * SIMULATED heading, in steps (30 Hz, 1 to 4 frames each), and about 3 steps behind the drawn ship the own-ship predictor
 * presents (lead mean 0.4 deg, max 6 deg), while the cockpit turned with the drawn ship in every frame. Its blips already
 * see the gate's swaps (the radar projection reads the view caches at render time).
 *
 * So, in every slot 10 call that draws a ship displaced (the predictor's swap, a stage 3 swap or an override), after
 * rdisp_others_enter (every swap is in place), for each radar camera of the list whose focus is such a ship (runtime IDs
 * equal; any slot's: client k's own ship is the ship of the slot it presents): the bank entry of the ship's cached parent
 * (its zone) M_Z must hold the model: its rotation rows within RRADAR_MODEL_TOL of O.r Y(psi_S) for the simulated (engine)
 * pose S (the yaw model) or of O.r R_S (the full model, X4's other branch); otherwise nothing is written (model=../miss). With
 * F the model's frame (Y(psi) or R) of S and of the drawn pose D, dR = F_S^T F_D (a turn about the zone's vertical in the yaw
 * model), c = t_Z - O.t F_S the centre the entry was made with: M_Z' = (R_Z dR, (t_Z - c) dR + c), which is what X4 makes from
 * D; D_c = M_Z' N_Z^-1 the camera's own change (N_Z the entry as it is now: M_Z, or G M_Z where rdisp_others re-hung this
 * camera in this call; the model is checked on its saved entries), and every entry X of the bank becomes D_c X: the zone's
 * entry ends at M_Z', the rest of the chain consistent with it. The written entries go back after the
 * original when each still holds what was written (else counted as foreign=, the entry left as it is: X4's own bank, as
 * rdisp_others); in a render-only frame the eye camera+0x890 that slot 10 recomputed goes back too. Nothing else is written:
 * not the controller, the holomap or the behavior. No simulation work reads the bank inside slot 10 (lockstep_render_display.h).
 * Status RENDER_RADAR (gate.render_radar_status): it names the camera (list index, pointers, the anchor's vtable, the chain's
 * vtables, where the zone sits) and counts the model's verdicts, its residual, the centre against holomap+0x320 and the turn
 * per frame (d_mrad), so 'observe' measures exactly what 'rotate' would write.
 * Pins (the gate's first tick; a mismatch stops the gate, 230 why=pins): rdisp's, the transform 0xda1f50, GetOffset 0x97a300,
 * vfunc13 0xdf9f10 and vfunc24 0xdfa040 (.pdata extents), and the vtable slots that lead to them. Nested entry: 230. */
#define RRADAR_STOP_REASON 230u
#define RRADAR_CONTROLLER 0x20u          /* camera+0x20: its movement controller */
#define RRADAR_CONTROLLER_VT 0x2c2fb80u  /* U::FixedOffsetHoloMapCameraMovementController */
#define RRADAR_HOLOMAP 0x90u             /* controller+0x90 */
#define RRADAR_OFFSET 0xa0u              /* controller+0xa0: O (translation, x, y, z rows; 16 floats) */
#define RRADAR_HOLOMAP_VT 0x2c2cee0u     /* U::HoloMap */
#define RRADAR_BEHAVIOR 0x2c0u           /* holomap+0x2c0 */
#define RRADAR_BEHAVIOR_VT 0x2c2eca0u    /* U::RadarBehavior */
#define RRADAR_FOCUS 0x2e0u              /* holomap+0x2e0: the focus's runtime ID */
#define RRADAR_CENTRE 0x320u             /* holomap+0x320: the map centre (x, y, z, w floats) */
#define RRADAR_CAMERAS 2u
#define RRADAR_CHAIN_SHOWN 6u
#define RRADAR_MODEL_TOL 2e-3
#define RRADAR_IMAGE_SIZE 0x6fa9000u
enum {RRADAR_OFF,RRADAR_ROTATE,RRADAR_OBSERVE};
enum {RRADAR_MODEL_NONE,RRADAR_MODEL_YAW,RRADAR_MODEL_FULL};
typedef struct {
    unsigned char *camera,*entries;unsigned count;int eye;
    unsigned char saved[RDISP_CHAIN_MAX][64],written[RDISP_CHAIN_MAX][64],eye_bytes[RDISP_EYE_BYTES];
} rradar_camera;
static int rradar_mode,rradar_armed,rradar_pending,rradar_pins_checked,rradar_restored=1;
static volatile LONG rradar_active;
static volatile LONG64 rradar_failures;
static volatile int rradar_open;
static volatile DWORD rradar_open_thread;
static const char *rradar_stage="off",*rradar_why="-";
static rradar_camera rradar_cameras[RRADAR_CAMERAS];
static unsigned rradar_camera_count;
/* The last radar camera seen (the status names it). */
static struct {
    unsigned index,count,zone_at,chain_n;int model;
    unsigned char *camera,*anchor,*holomap,*zone,*ship;uint32_t anchor_vt,chain_vt[RRADAR_CHAIN_SHOWN];uint64_t focus;
} rradar_seen;
static uint64_t rradar_calls,rradar_idle,rradar_found,rradar_no_focus,rradar_table,rradar_frame_miss,rradar_level,rradar_identity;
static uint64_t rradar_yaw,rradar_full_model,rradar_miss,rradar_rehung,rradar_written,rradar_restores,rradar_eye,rradar_foreign;
static uint64_t rradar_cached,rradar_full,rradar_nested;
static double rradar_residual_last,rradar_residual_max,rradar_centre_err_max,rradar_d_last,rradar_d_max;
static rdisp_stat rradar_d;
static const struct {unsigned rva,size;const char *sha;} rradar_pin_table[]={
    {0xda1f50u,0x458u,"a5a4e7394af3fbacec95c3273e9e8c93a0e3f2937362a50a79edb4e2280c4057"},   /* the controller's transform */
    {0x97a300u,0x3dcu,"f494c5a2d068a5be0bdf2897a6def0ad99b378c609919e47a7fdc8452374950d"},   /* Camera::GetOffset */
    {0xdf9f10u,0x106u,"2238f53ae3c3c181eb5ac455e289b3048a13f90e68dc9cf29c53bc5fcfb1f614"},   /* RadarBehavior vfunc13: the reference */
    {0xdfa040u,0x33u,"7a9592c97fd7bca17e1cd828cdaea79b590849611202d1f979cf7123097921e4"}};   /* RadarBehavior vfunc24: the axis test */
/* Vtable slots (relocated pointers: compared with image + the target). */
static const struct {unsigned slot,target;} rradar_slot_table[]={
    {RRADAR_CONTROLLER_VT+49u*8u,0xda1f50u},{RRADAR_BEHAVIOR_VT+13u*8u,0xdf9f10u},{RRADAR_BEHAVIOR_VT+24u*8u,0xdfa040u},
    {RRADAR_HOLOMAP_VT+674u*8u,0xe18f30u}};
static int rradar_pins(void) {
    if(!rradar_pins_checked) {
        int ok=image!=NULL;
        for(unsigned i=0;ok && i<sizeof(rradar_pin_table)/sizeof(*rradar_pin_table);i++) ok=hash_ok(image+rradar_pin_table[i].rva,rradar_pin_table[i].size,rradar_pin_table[i].sha);
        for(unsigned i=0;ok && i<sizeof(rradar_slot_table)/sizeof(*rradar_slot_table);i++) {
            unsigned char *at=NULL;ok=sample_read(image+rradar_slot_table[i].slot,&at,8) && at==image+rradar_slot_table[i].target;
        }
        rradar_pins_checked=ok?1:-1;
    }
    return rradar_pins_checked==1;
}
static int rradar_fail(const char *why) {
    if(!strcmp(rradar_why,"-")) rradar_why=why;
    rradar_stage="failed";InterlockedIncrement64(&rradar_failures);return 0;
}
/* An object's vtable as an image offset (0 when it is not in the image). */
static uint32_t rradar_vt(const unsigned char *object) {
    uintptr_t vt=0;
    if(!object || !image || !sample_read(object,&vt,8) || vt<(uintptr_t)image || vt-(uintptr_t)image>=RRADAR_IMAGE_SIZE) return 0;
    return (uint32_t)(vt-(uintptr_t)image);
}
/* The controller's frame of a pose's heading, Y(psi) (rows as above); 0 when the z row is (nearly) vertical. */
static int rradar_heading(const rdisp_xf *x,double y[9]) {
    double fx=x->r[6],fz=x->r[8],n=sqrt(fx*fx+fz*fz);
    if(!(n>1e-3)) return 0;
    fx/=n;fz/=n;
    y[0]=fz;y[1]=0;y[2]=-fx;y[3]=0;y[4]=1;y[5]=0;y[6]=fx;y[7]=0;y[8]=fz;
    return 1;
}
static void rradar_tmul(const double a[9],const double b[9],double out[9]) { /* a^T b */
    double t[9];
    for(unsigned i=0;i<3;i++) for(unsigned j=0;j<3;j++) t[3*i+j]=a[i]*b[j]+a[3+i]*b[3+j]+a[6+i]*b[6+j];
    memcpy(out,t,sizeof(t));
}
static double rradar_diff(const double a[9],const double b[9]) {
    double m=0;for(unsigned i=0;i<9;i++) {double d=fabs(a[i]-b[i]);if(!(d<=m)) m=d;}
    return m;
}
/* The ship drawn displaced in this call whose runtime ID is id: its object, its drawn pose and the engine's (the simulated)
 * pose block; NULL when no such ship is displaced (the predictor's swaps of any slot, then stage 3's and the overrides). */
static unsigned char *rradar_displaced(uint64_t id,unsigned bank,float drawn[16],float sim[16]) {
    for(unsigned j=0;opred_open && j<PILOT_SLOTS;j++) {
        unsigned char *object=opred_open_object[j],*block=NULL;uint64_t got=0;
        if(!opred_open_at[j] || !object || !sample_read(object+RDISP_ID,&got,8) || got!=id) continue;
        if(!sample_read((const void*)opred_open_at[j],&block,8) || block!=(unsigned char*)opred_blocks[j] || !sample_read(opred_open_saved[j],sim,64)) return NULL;
        memcpy(drawn,opred_blocks[j],64);return rdisp_finite(drawn) && rdisp_finite(sim)?object:NULL;
    }
    for(unsigned i=0;rdisp_open && bank==rdisp_bank && i<rdisp_swap_count;i++) {
        unsigned char *object=rdisp_swaps[i].slot-RDISP_VC_BLOCK-RDISP_VC-bank*RDISP_VC_STRIDE,*block=NULL;uint64_t got=0;
        if(!sample_read(object+RDISP_ID,&got,8) || got!=id) continue;
        if(!sample_read(rdisp_swaps[i].slot,&block,8) || block!=rdisp_swaps[i].mine || !sample_read(rdisp_swaps[i].engine,sim,64)) return NULL;
        memcpy(drawn,rdisp_swaps[i].mine,64);return rdisp_finite(drawn) && rdisp_finite(sim)?object:NULL;
    }
    return NULL;
}
/* Slot 10, after rdisp_others_enter, before the original: 1 when a radar camera's bank is written. */
static int rradar_enter(void) {
    rradar_camera_count=0;
    if(!rradar_armed || !InterlockedCompareExchange(&rradar_active,0,0) || !stats.active || !image) return 0;
    if(rradar_open) {++rradar_nested;rradar_fail("nested");return 0;}
    ++rradar_calls;
    const int displaced=(rdisp_open && rdisp_swap_count) || opred_open;
    if(!displaced) ++rradar_idle;   /* nothing is drawn displaced in this call: the camera is only named */
    unsigned bank=2;unsigned char *universe=NULL,*player=NULL;uint64_t cameras=0;
    if(!sample_read(image+RDISP_VIEW_INDEX,&bank,4) || bank>1 || !sample_read(image+RDISP_UNIVERSE,&universe,8) || !universe ||
       !sample_read(universe+RDISP_CAMERA,&player,8) || !sample_read(universe+RDISP_CAMERA_LIST_COUNT,&cameras,8) || cameras>RDISP_CAMERA_LIST_MAX) {
        ++rradar_table;return 0;
    }
    const int render=InterlockedCompareExchange(&rf_frame_open,0,0)!=0;
    for(uint64_t c=0;c<cameras;c++) {
        unsigned char *camera=NULL,*controller=NULL,*holomap=NULL,*behavior=NULL;uint64_t focus=0;
        if(!sample_read(universe+RDISP_CAMERA_LIST+8*c,&camera,8) || !camera || camera==player) continue;
        if(!sample_read(camera+RRADAR_CONTROLLER,&controller,8) || rradar_vt(controller)!=RRADAR_CONTROLLER_VT ||
           !sample_read(controller+RRADAR_HOLOMAP,&holomap,8) || rradar_vt(holomap)!=RRADAR_HOLOMAP_VT ||
           !sample_read(holomap+RRADAR_BEHAVIOR,&behavior,8) || rradar_vt(behavior)!=RRADAR_BEHAVIOR_VT ||
           !sample_read(holomap+RRADAR_FOCUS,&focus,8) || !focus) continue;
        ++rradar_found;
        rradar_seen.index=(unsigned)c;rradar_seen.camera=camera;rradar_seen.holomap=holomap;rradar_seen.focus=focus;
        unsigned char *zone=NULL,*entries=NULL,*anchor=NULL;uint64_t count=0;
        unsigned char bytes[RDISP_CHAIN_MAX*RDISP_ENTRY],orig[RDISP_CHAIN_MAX*RDISP_ENTRY];
        if(!rdisp_table_of(camera,bank,&entries,&count,&anchor) || !sample_read(entries,bytes,(size_t)count*RDISP_ENTRY) ||
           rdisp_entry_key(bytes,0) || rdisp_entry_key(bytes,1)!=anchor) {++rradar_table;continue;}
        rradar_seen.anchor=anchor;rradar_seen.anchor_vt=rradar_vt(anchor);rradar_seen.count=(unsigned)count;
        rradar_seen.chain_n=0;
        for(unsigned i=1;i<count && rradar_seen.chain_n<RRADAR_CHAIN_SHOWN;i++) rradar_seen.chain_vt[rradar_seen.chain_n++]=rradar_vt(rdisp_entry_key(bytes,i));
        if(!displaced) continue;
        float drawn_f[16],sim_f[16],f[16];unsigned char *ship=rradar_displaced(focus,bank,drawn_f,sim_f);
        rradar_seen.ship=ship;
        if(!ship) {++rradar_no_focus;continue;}   /* its ship is drawn as simulated in this call */
        if(!sample_read(rdisp_vc(ship,bank),&zone,8) || !zone) {++rradar_table;continue;}
        rradar_seen.zone=zone;
        /* The bank as the refill left it: where rdisp_others re-hung this camera in this call, its saved entries. */
        memcpy(orig,bytes,(size_t)count*RDISP_ENTRY);
        for(unsigned n=0;n<rdisp_other_open;n++) if(rdisp_others[n].camera==camera && rdisp_others[n].entries==entries) {
            const rdisp_other *o=&rdisp_others[n];++rradar_rehung;
            for(unsigned i=0;i<o->count && i<count;i++) if(!i || i>o->first) memcpy(orig+i*RDISP_ENTRY+RDISP_ENTRY_POSE,o->saved[i],64);
        }
        unsigned z=0;int finite=1;
        for(unsigned i=1;i<count && !z;i++) if(rdisp_entry_key(orig,i)==zone) z=i;
        rradar_seen.zone_at=z;
        if(!z) {++rradar_frame_miss;continue;}    /* the ship's zone is not in the camera's chain: no frame to turn in */
        for(unsigned i=0;i<count && finite;i++) {rdisp_entry_pose(bytes,i,f);finite=rdisp_finite(f);rdisp_entry_pose(orig,i,f);finite=finite && rdisp_finite(f);}
        float of[16];
        if(!finite || !sample_read(controller+RRADAR_OFFSET,of,64) || !rdisp_finite(of)) {++rradar_table;continue;}
        rdisp_xf o,mz,s,d;rdisp_xf_from(of,&o);rdisp_entry_pose(orig,z,f);rdisp_xf_from(f,&mz);rdisp_xf_from(sim_f,&s);rdisp_xf_from(drawn_f,&d);
        /* The model: which frame X4 made this entry with (yaw first, as X4's default branch). */
        double ys[9],yd[9],pred[9],fs[9],fd[9],residual=1e30;int model=RRADAR_MODEL_NONE;
        if(rradar_heading(&s,ys) && rradar_heading(&d,yd)) {
            predict_mul(o.r,ys,pred);residual=rradar_diff(pred,mz.r);
            if(residual<=RRADAR_MODEL_TOL) {model=RRADAR_MODEL_YAW;memcpy(fs,ys,sizeof(fs));memcpy(fd,yd,sizeof(fd));}
        } else ++rradar_level;
        if(!model) {
            double full;predict_mul(o.r,s.r,pred);full=rradar_diff(pred,mz.r);
            if(full<residual) residual=full;
            if(full<=RRADAR_MODEL_TOL) {model=RRADAR_MODEL_FULL;memcpy(fs,s.r,sizeof(fs));memcpy(fd,d.r,sizeof(fd));}
        }
        rradar_seen.model=model;rradar_residual_last=residual;if(residual<1e29 && residual>rradar_residual_max) rradar_residual_max=residual;
        if(!model) {++rradar_miss;continue;}
        if(model==RRADAR_MODEL_YAW) ++rradar_yaw;else ++rradar_full_model;
        /* The centre the entry was made with, against X4's (diagnostic). */
        double centre[3],dr[9],e;float xc[4];
        for(unsigned j=0;j<3;j++) centre[j]=mz.t[j]-(o.t[0]*fs[j]+o.t[1]*fs[3+j]+o.t[2]*fs[6+j]);
        if(sample_read(holomap+RRADAR_CENTRE,xc,16) && isfinite(xc[0]) && isfinite(xc[1]) && isfinite(xc[2])) {
            double v[3]={centre[0]-xc[0],centre[1]-xc[1],centre[2]-xc[2]};e=rdisp_norm3(v);if(e>rradar_centre_err_max) rradar_centre_err_max=e;
        }
        rradar_tmul(fs,fd,dr);
        const double turn=predict_angle(fs,fd);
        rdisp_stat_add(&rradar_d,turn,NULL);rradar_d_last=turn;if(turn>rradar_d_max) rradar_d_max=turn;
        if(!(turn>1e-9)) {++rradar_identity;continue;}
        if(rradar_mode!=RRADAR_ROTATE) continue;   /* observe: measured, nothing written */
        /* M_Z' from the refill's entry; the change from the entry as it is now (rdisp_others may have re-hung it: the zone's
         * entry then ends at M_Z' all the same, and everything above and below it stays consistent with it). */
        rdisp_xf mz_new,mz_now,mz_inv,change;predict_mul(mz.r,dr,mz_new.r);
        for(unsigned j=0;j<3;j++) mz_new.t[j]=(mz.t[0]-centre[0])*dr[j]+(mz.t[1]-centre[1])*dr[3+j]+(mz.t[2]-centre[2])*dr[6+j]+centre[j];
        rdisp_entry_pose(bytes,z,f);rdisp_xf_from(f,&mz_now);
        if(!rdisp_inverse(&mz_now,&mz_inv)) {++rradar_table;continue;}
        rdisp_compose(&mz_new,&mz_inv,&change);
        if(rradar_camera_count>=RRADAR_CAMERAS) {++rradar_full;break;}
        rradar_camera *w=&rradar_cameras[rradar_camera_count];
        w->camera=camera;w->entries=entries;w->count=(unsigned)count;
        for(unsigned i=0;i<count;i++) {
            rdisp_xf m,out;rdisp_entry_pose(bytes,i,f);rdisp_xf_from(f,&m);rdisp_compose(&change,&m,&out);
            memcpy(w->saved[i],f,64);rdisp_xf_to(&out,f);memcpy(w->written[i],f,64);
        }
        if(!render) {uint64_t blocks=0;if(sample_read(camera+RDISP_CAMERA_BLOCKS,&blocks,8) && blocks) ++rradar_cached;}
        w->eye=render && sample_read(camera+RDISP_EYE,w->eye_bytes,RDISP_EYE_BYTES);
        for(unsigned i=0;i<count;i++) memcpy(entries+i*RDISP_ENTRY+RDISP_ENTRY_POSE,w->written[i],64);
        ++rradar_camera_count;++rradar_written;
    }
    if(!rradar_camera_count) return 0;
    rradar_open_thread=GetCurrentThreadId();rradar_open=1;rradar_stage="live";
    return 1;
}
/* After the original (also on unwind; the first of the leaves): the banks back, in a render-only frame the eyes too. */
static void rradar_leave(int entered) {
    if(!entered) return;
    for(unsigned n=rradar_camera_count;n-->0;) {
        rradar_camera *w=&rradar_cameras[n];int foreign=0;
        for(unsigned i=0;i<w->count;i++) {
            unsigned char *pose=w->entries+i*RDISP_ENTRY+RDISP_ENTRY_POSE;
            if(memcmp(pose,w->written[i],64)) {foreign=1;continue;}
            memcpy(pose,w->saved[i],64);
        }
        if(foreign) ++rradar_foreign;else ++rradar_restores;
        if(w->eye) {memcpy(w->camera+RDISP_EYE,w->eye_bytes,RDISP_EYE_BYTES);++rradar_eye;}
    }
    rradar_camera_count=0;rradar_open=0;
}
/* ---- arming, the gate's checks, status ---- */
static int rradar_mode_of(const char *text,size_t length) {
    if(!text) return RRADAR_OFF;
    if(length==6 && !memcmp(text,"rotate",6)) return RRADAR_ROTATE;
    if(length==7 && !memcmp(text,"observe",7)) return RRADAR_OBSERVE;
    return RRADAR_OFF;
}
static const char *rradar_mode_name(int mode) {return mode==RRADAR_ROTATE?"rotate":mode==RRADAR_OBSERVE?"observe":"off";}
/* gate.render_radar(mode): before the start; afterwards the same value only. Reads no engine memory (image may be NULL in
 * the probe's arming pass); the gate's first tick checks the pins and activates it (rradar_check). */
static int rradar_arm(int mode,int started) {
    if(mode!=RRADAR_ROTATE && mode!=RRADAR_OBSERVE) return 0;
    if(started) return rradar_armed && rradar_mode==mode;
    if(rradar_pins_checked<0) {rradar_stage="refused";return 0;}
    rradar_mode=mode;rradar_armed=1;rradar_pending=1;rradar_restored=1;rradar_stage="pending";rradar_why="-";rradar_failures=0;
    InterlockedExchange(&rradar_active,0);return 1;
}
static int rradar_check(void) {
    if(!rradar_armed) return 1;
    if(rradar_pending) {
        rradar_pending=0;
        if(!image || !rdisp_pins() || !rradar_pins()) return rradar_fail("pins");
        rradar_stage="armed";InterlockedExchange(&rradar_active,1);
    }
    return !InterlockedCompareExchange64(&rradar_failures,0,0);
}
/* finish(): nothing new from here; a slot 10 call in flight puts its banks back in its own __finally. */
static void rradar_restore(void) {
    InterlockedExchange(&rradar_active,0);rradar_pending=0;
    if(!rradar_armed) return;
    for(unsigned i=0;i<200 && rradar_open && GetCurrentThreadId()!=rradar_open_thread;i++) Sleep(1);
    rradar_restored=!rradar_open && !rradar_camera_count;
    if(strcmp(rradar_stage,"failed")) rradar_stage="stopped";
}
static int rradar_status(char *out,size_t capacity) {
    char chain[RRADAR_CHAIN_SHOWN*10+2];size_t at=0;chain[0]=0;
    for(unsigned i=0;i<rradar_seen.chain_n && at+10<sizeof(chain);i++) at+=(size_t)snprintf(chain+at,sizeof(chain)-at,"%s%x",i?",":"",rradar_seen.chain_vt[i]);
    if(!at) {chain[0]='-';chain[1]=0;}
    int n=snprintf(out,capacity,
        "mode=%s armed=%d active=%ld stage=%s why=%s calls=%llu idle=%llu found=%llu no_focus=%llu table=%llu frame_miss=%llu level=%llu "
        "model=%llu/%llu/%llu residual=%.3g/%.3g centre_err=%.3g identity=%llu rehung=%llu written=%llu restores=%llu eye=%llu foreign=%llu "
        "cached=%llu full=%llu nested=%llu d_mrad=%.3f/%.3f/%.3f d_n=%llu failures=%lld restored=%d "
        "camera=%u:%llx anchor=%llx:%x holomap=%llx focus=%llx ship=%llx zone=%llx@%u count=%u chain=%s model_last=%s",
        rradar_mode_name(rradar_mode),rradar_armed,rradar_active,rradar_stage,rradar_why,(unsigned long long)rradar_calls,(unsigned long long)rradar_idle,
        (unsigned long long)rradar_found,(unsigned long long)rradar_no_focus,(unsigned long long)rradar_table,(unsigned long long)rradar_frame_miss,
        (unsigned long long)rradar_level,(unsigned long long)rradar_yaw,(unsigned long long)rradar_full_model,(unsigned long long)rradar_miss,
        rradar_residual_last<1e29?rradar_residual_last:-1.0,rradar_residual_max,rradar_centre_err_max,(unsigned long long)rradar_identity,
        (unsigned long long)rradar_rehung,(unsigned long long)rradar_written,(unsigned long long)rradar_restores,(unsigned long long)rradar_eye,
        (unsigned long long)rradar_foreign,(unsigned long long)rradar_cached,(unsigned long long)rradar_full,(unsigned long long)rradar_nested,
        rradar_d_last*1000.0,rdisp_mean(&rradar_d)*1000.0,rradar_d_max*1000.0,(unsigned long long)rradar_d.n,(long long)rradar_failures,rradar_restored,
        rradar_seen.index,(unsigned long long)(uintptr_t)rradar_seen.camera,(unsigned long long)(uintptr_t)rradar_seen.anchor,rradar_seen.anchor_vt,
        (unsigned long long)(uintptr_t)rradar_seen.holomap,(unsigned long long)rradar_seen.focus,(unsigned long long)(uintptr_t)rradar_seen.ship,
        (unsigned long long)(uintptr_t)rradar_seen.zone,rradar_seen.zone_at,rradar_seen.count,chain,
        rradar_seen.model==RRADAR_MODEL_YAW?"yaw":rradar_seen.model==RRADAR_MODEL_FULL?"full":"none");
    return n>0 && (size_t)n<capacity?n:0;
}

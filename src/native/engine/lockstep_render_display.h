/* Display transforms between two steps, stages 2 and 3 of reports/LOCKSTEP-RENDER-FRAMES-20261101.txt,
 * DEFAULT OFF (Windows gate, stop reason 183). Both need native_gate_render_fps (lockstep_render_frames.h).
 *   native_gate_render_camera = 'extrapolate' | 'interpolate' (true = 'extrapolate')
 *       stage 2: the player camera (cockpit mouse look and head look, the chase camera; of the slot this
 *       client presents) moves in the render-only frames;
 *   native_gate_render_motion = 'extrapolate' | 'interpolate' (true = 'extrapolate')
 *       stage 3: the moving objects of the camera's zone move in the render-only frames.
 * Display only. All of it runs on the MoveWorker inside VBI slot 10 (the render RNG split's wrapper,
 * render_rng_execute) and is undone before slot 10 returns, also on an exception (the wrapper's __finally).
 * No simulation slot, pass or job runs while it is in place: in a render-only frame only slot 10 runs
 * (stage 1); in a step frame the pathing pass is joined before the slots, the mass traffic and region
 * passes start after them, and slot 11 comes after slot 10. Nothing is ever written back.
 *
 * What the renderer reads (X4.exe 9.00, read in the binary, pinned below):
 *   - The view cache of a component for bank A (object+0xe0+A*0x80, A = *0x38f4ee0 on the MoveWorker):
 *     +0x00 the cached parent, +0x10/+0x18 the children (begin/end of a vector of component pointers,
 *     PropagateCacheFlags 0x3d46b0 -> CopyToViewCache 0x997be0, each entry holds a reference), +0x28 the
 *     pose block: a 0x70-byte, 16-byte aligned allocation (0x3d4845), +0x00 the translation, +0x10/+0x20/
 *     +0x30 the rotation rows (the local axes in the parent's frame), +0x40 the scale. The render walk
 *     0x3de630 composes every model-view from these blocks (mulps from the block: aligned) and falls back
 *     to the component's own transform only when the pointer is NULL.
 *   - The player camera's view bank for bank A (camera = *(g_Universe 0x3d198f8)+0x3d0, table =
 *     camera+0x30+A*0x2b0): +0x00 the anchor (camera+0x770, referenced), entries of 0x50 bytes {key,
 *     pad, translation row, three rotation rows} inline at +0x10 (capacity +0x290, inline while 8,
 *     otherwise +0x10 holds the heap pointer), count +0x2a0. The refill 0x9799f0 (slot 3, write index,
 *     made the read index by the swap) writes entry 0 for the root (key 0), then the anchor and each live
 *     parent (+0x70) up the tree, each the camera's pose in that component's frame (GetOffset 0x97a300).
 *     The reader 0x979ef0 composes a pose with the entry of the asked component, or with the anchor's
 *     entry and then the view caches (0x3dde10) for a component that has none (the cockpit below the
 *     ship). The render's eye 0x9835a0 is the entry of the scene's zone, stored inverted at camera+0x890
 *     (0x40 bytes) for the frame.
 * So the camera relative to the cockpit, and every object relative to the cockpit through the pose
 * blocks, decide the picture; the zone-level entries decide the eye, culling and LOD.
 *
 * Timing (the display offset s, in steps, of what a slot 10 call draws). A step's picture is built in
 * its slot 10 and presented by the next FrameTick: the start of the first render-only frame of the gap
 * (t_first). A render-only frame starting at t is presented by the FrameTick after it, at about
 * t + max(frame interval, measured frame period). So s = (t + max(interval, estimate) - t_first) / T,
 * T = 1/native_gate_tick_hz, clamped to 0..1 (1 = the next step's presentation). The step frame is s 0.
 *   extrapolate  a render-only frame draws pose(N) advanced by s along the motion N-1 -> N (no delay;
 *                the step frame itself is the simulation's; overshoot on a change, gone at the next step);
 *   interpolate  every frame, the step frame included, draws pose(N-1) advanced by s (s - 1 relative
 *                to N): never off the simulated path between two steps, everything one step late.
 * Motion: position linear, rotation along the shortest arc (R(s) = Exp(s w) R_N, w = Log(R_N R_{N-1}^T),
 * the local rotation vector, lockstep_pilot_predict.h's helpers). A step of more than 1000 m or 0.5 rad
 * (a teleport, a highway or zone change, a respawn) is drawn as simulated (snaps=).
 *
 * Stage 3, objects. In each step frame (slot 10 entry, before any presentation hook writes): the zone =
 * the first view-bank entry from the anchor up that is of class 0x6d (IsOfClass, vtable +0x11e8, the zone
 * the camera's render takes), its children for bank A (at most RDISP_ITEMS, capped=), each with its
 * runtime ID (+8), its pose block pointer and the block's first 64 bytes; matched with the previous step
 * by pointer and ID (same cached parent). In a frame that draws an offset, every child that moved gets a
 * private block: the engine's 0x70 bytes copied, the pose written (w lanes kept), and the view cache's
 * +0x28 pointer swapped to it by compare-and-swap; after slot 10 the engine's pointer goes back by
 * compare-and-swap. A block whose pointer or pose is no longer the sampled one is left alone (stale=);
 * one a predictor presents itself is left alone (owned=, below). The children of a moved object (its
 * turrets, cockpit, docked ships) follow through their parent.
 * Stage 2, camera. In each step frame the view bank (keys, poses) and, for each chain member, its pose
 * block pointer and pose. C = the camera's pose in the anchor's frame (entry 1); its motion N-1 -> N
 * (same anchor, else reset) gives C' at the frame's offset and D = C' C^-1, a change in the camera's own
 * frame. Every entry X becomes D M_X: the camera moves, the world does not. When a chain member K (the
 * own ship) is drawn displaced (stage 3, an override or a predictor's swap: its block pointer or pose
 * differs from the sample), the entries above it follow it: with P its parent's entry, M_P' = D M_K S'_K
 * (S'_K its displayed pose) and every entry from P up (and the root) becomes G M_X, G = M_P' M_P^-1. The
 * written entries go back after slot 10 when they still hold what was written (otherwise foreign=, a
 * failure); in a render-only frame the eye camera+0x890 that slot 10 recomputed from them goes back too, so
 * outside slot 10 everything is as stage 1 leaves it. The table is written whenever stage 2 is on (D) or
 * stage 3 is on and a chain member is displaced (G), in the frames that draw an offset.
 * Not covered (visible, not desync sources): the HUD (slot 6 is not run in a render-only frame: brackets and
 * the crosshair stay at the step's projection), particles and effects that are not components, objects
 * outside the camera's zone, the view-block cache of a step frame filled before slot 10 (interpolate
 * mode; cam_cached=), new input (still applied at the next step: the camera shows the camera's own motion
 * between the two last steps, not a mouse movement made since; a non-consuming read of X4's pending input
 * queue is the open RE item of report section 6).
 *
 * Hook interface for a display predictor (e.g. lockstep_own_predict.h), all on the MoveWorker in slot 10:
 *   rdisp_display_offset()   the offset s the world is drawn at in this slot 10 call, in steps relative
 *                            to the admitted step (step frame 0, interpolate -1..0, otherwise 0..1); 0
 *                            outside slot 10 or without render-only frames. Assign it to a predictor's
 *                            own pointer (opred_display_offset = rdisp_display_offset) at arm.
 *   rdisp_owns               a predictor's int owns(const void *component): 1 = it swaps that component's
 *                            pose itself in this call; stage 3 leaves it alone (owned=). Its swap must
 *                            happen in predict_present_enter, which runs after rdisp_sample and before
 *                            rdisp_enter: the camera bank then follows its displayed pose.
 *   rdisp_override(c, pose)  or let this layer do it: from rdisp_sample until rdisp_enter (i.e. inside
 *                            predict_present_enter), up to RDISP_OVERRIDES components per call, a pose
 *                            (12 floats: translation, x, y, z rows, in the parent's frame) to draw
 *                            instead; swapped and restored with stage 3's mechanism, the camera bank
 *                            follows. Needs either key armed and active (from the gate's first tick);
 *                            returns 1 when accepted.
 * Fail closed: a pointer or entry that does not hold what was written when it goes back, a nested entry,
 * stop the gate (183, why=). Pins (checked on the gate's first tick, not at arm: before the start X4.exe is
 * not verified and image is NULL): the bank reader 0x979ef0, the refill 0x9799f0, the eye 0x9835a0, the render
 * walk 0x3de630 and PropagateCacheFlags 0x3d46b0 (.pdata extents); a mismatch stops the gate (183, why=pins).
 * finish() stops new entries; an open call restores in its own __finally.
 * Status RENDER_DISPLAY (gate.render_display_status) and rows RENDER_DISPLAY_ROW per drawn frame
 * (gate.render_display_row, native_gate_render_display_rows = true). */
#define RDISP_STOP_REASON 183u
#define RDISP_VIEW_INDEX 0x38f4ee0u      /* the MoveWorker's read bank (0x979ef0, 0x3de630) */
#define RDISP_UNIVERSE 0x3d198f8u
#define RDISP_CAMERA 0x3d0u
#define RDISP_TABLE 0x30u
#define RDISP_TABLE_STRIDE 0x2b0u
#define RDISP_TABLE_ENTRIES 0x10u
#define RDISP_TABLE_CAPACITY 0x290u
#define RDISP_TABLE_COUNT 0x2a0u
#define RDISP_TABLE_INLINE 8u
#define RDISP_ENTRY 0x50u
#define RDISP_ENTRY_POSE 0x10u
#define RDISP_EYE 0x890u
#define RDISP_EYE_BYTES 0x40u
#define RDISP_CAMERA_BLOCKS 0x8e8u       /* the camera's view-block cache in use (cleared by 0x97a160) */
#define RDISP_VC 0xe0u
#define RDISP_VC_STRIDE 0x80u
#define RDISP_VC_CHILDREN 0x10u
#define RDISP_VC_BLOCK 0x28u
#define RDISP_BLOCK_BYTES 0x70u
#define RDISP_ID 0x08u
#define RDISP_CLASS_SLOT 0x11e8u
#define RDISP_ZONE_CLASS 0x6d
#define RDISP_ITEMS 4096u
#define RDISP_HASH_BITS 13u
#define RDISP_HASH (1u<<RDISP_HASH_BITS)
#define RDISP_CHAIN_MAX 24u
#define RDISP_OVERRIDES 4u
#define RDISP_SNAP_POSITION 1000.0       /* m per step */
#define RDISP_SNAP_ANGLE 0.5             /* rad per step */
#define RDISP_ROWS 1024u
#define RDISP_BUCKETS 8u
enum {RDISP_OFF,RDISP_EXTRAPOLATE,RDISP_INTERPOLATE};
typedef struct {double r[9],t[3];} rdisp_xf;   /* p_parent = p_local R + t; R's rows are the local axes */
typedef struct {
    unsigned char *object,*block,*parent;uint64_t id;
    float pose[16];                             /* the block's first 64 bytes at the step */
    double dp[3],w[3];                          /* motion N-1 -> N: translation, local rotation vector */
    unsigned char motion,moved,snap;
} rdisp_item;
typedef struct {unsigned char *slot,*engine,*mine;} rdisp_swap;
typedef struct {void *object;float pose[12];} rdisp_override_entry;
typedef struct {uint64_t n;double sum,max;uint64_t bucket[RDISP_BUCKETS];} rdisp_stat;
typedef struct {uint64_t seq,tick,wall_ms;double s,cam_rad,cam_m,disp_m;unsigned kind,moved,swapped,stale,table;} rdisp_row;
static const double rdisp_pos_edges[RDISP_BUCKETS-1]={0.001,0.01,0.03,0.1,0.3,1.0,3.0};
static int rdisp_camera_mode,rdisp_motion_mode,rdisp_armed,rdisp_pending,rdisp_rows_on,rdisp_pins_checked,rdisp_restored=1;
static volatile LONG rdisp_active;
static volatile LONG64 rdisp_failures;
static const char *rdisp_stage="off",*rdisp_why="-";
/* Tests only: replaces IsOfClass. */
static int (*rdisp_is_a_override)(void *object,int cls);
/* The hook interface (above). */
static int (*rdisp_owns)(const void *object);
/* ---- the call being drawn (MoveWorker, slot 10) ---- */
static int rdisp_window,rdisp_render_frame;
static volatile int rdisp_open;
static volatile DWORD rdisp_open_thread;
static double rdisp_s_time,rdisp_s_world,rdisp_s_camera;
static unsigned rdisp_bank;
/* ---- stage 3 ---- */
static rdisp_item rdisp_items[2][RDISP_ITEMS];
static uint32_t rdisp_hash[2][RDISP_HASH];
static unsigned rdisp_item_count[2],rdisp_gen;
static int rdisp_items_valid;
static unsigned char *rdisp_zone;
static __declspec(align(64)) unsigned char rdisp_blocks[RDISP_ITEMS+RDISP_OVERRIDES][RDISP_BLOCK_BYTES];
static rdisp_swap rdisp_swaps[RDISP_ITEMS+RDISP_OVERRIDES];
static unsigned rdisp_swap_count;
static rdisp_override_entry rdisp_overrides[RDISP_OVERRIDES];
static unsigned rdisp_override_count;
/* ---- stage 2 ---- */
static struct {
    int valid;unsigned char *camera,*table,*entries,*anchor;uint64_t count;
    unsigned char bytes[RDISP_CHAIN_MAX*RDISP_ENTRY];        /* the entries at the step */
    unsigned char *chain_block[RDISP_CHAIN_MAX],*chain_parent[RDISP_CHAIN_MAX];
    float chain_pose[RDISP_CHAIN_MAX][16];int chain_ok[RDISP_CHAIN_MAX];
    rdisp_xf anchor_pose;                                       /* C: the camera in the anchor's frame */
} rdisp_cam;
static rdisp_xf rdisp_cam_last;static unsigned char *rdisp_cam_last_anchor;static int rdisp_cam_last_valid;
static double rdisp_cam_dp[3],rdisp_cam_w[3];static int rdisp_cam_motion,rdisp_cam_last_motion;
static unsigned char rdisp_cam_saved[RDISP_CHAIN_MAX][64],rdisp_cam_written[RDISP_CHAIN_MAX][64],rdisp_eye_saved[RDISP_EYE_BYTES];
static unsigned rdisp_cam_open_count;static int rdisp_eye_open;
/* ---- accounting ---- */
static uint64_t rdisp_samples,rdisp_sample_fail,rdisp_sample_us,rdisp_sample_us_sum,rdisp_sample_us_max,rdisp_frames,rdisp_step_frames,rdisp_render_frames;
static uint64_t rdisp_apply_us,rdisp_apply_us_sum,rdisp_apply_us_max,rdisp_items_max,rdisp_capped,rdisp_zone_missing,rdisp_moved_last;
static uint64_t rdisp_swapped,rdisp_restores,rdisp_owned,rdisp_overridden,rdisp_override_refused,rdisp_stale,rdisp_snaps,rdisp_reparented;
static uint64_t rdisp_cam_written_n,rdisp_cam_restored,rdisp_cam_identity,rdisp_cam_stale,rdisp_cam_snaps,rdisp_cam_resets,rdisp_cam_cached;
static uint64_t rdisp_chain,rdisp_chain_parent,rdisp_eye_restored,rdisp_foreign,rdisp_nested,rdisp_last_tick;
static double rdisp_s_last,rdisp_s_max,rdisp_disp_m,rdisp_disp_rad,rdisp_cam_rad_max,rdisp_cam_m_max;
static rdisp_stat rdisp_err_m,rdisp_err_rad,rdisp_cam_err_m,rdisp_cam_err_rad;
static rdisp_row rdisp_rows[RDISP_ROWS];
static uint64_t rdisp_row_seq;
static const struct {unsigned rva,size;const char *sha;} rdisp_pin_table[]={
    {0x979ef0u,0x264u,"81de8348fb8fc02d3c34c7a8a35b225a0c29b0a1b2ac668597f57da80ecd88ca"},   /* the view-bank reader */
    {0x9799f0u,0x2e6u,"5f15d5c3f7ab81f3f044910eade398bf4aec1a6acd019490797f985bcb1fd46f"},   /* the view-bank refill */
    {0x9835a0u,0x453u,"c8435e8772e0ad16ef9267fd6e3f0b42566153798dc90f13580cdcaecf98b483"},   /* the render's eye */
    {0x3de630u,0x7e5u,"c76f237791494750478ffd518026d62295ce3177c1d958b85782b8cd9b08220f"},   /* the render walk's pose blocks */
    {0x3d46b0u,0x3ccu,"84f77bb26750e1f20ccb53d4bc8863a9aa18119da21e6d1648e4659fdedc7784"}};  /* PropagateCacheFlags */
static int rdisp_pins(void) {
    if(!rdisp_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<sizeof(rdisp_pin_table)/sizeof(*rdisp_pin_table);i++) ok=hash_ok(image+rdisp_pin_table[i].rva,rdisp_pin_table[i].size,rdisp_pin_table[i].sha);
        rdisp_pins_checked=ok?1:-1;
    }
    return rdisp_pins_checked==1;
}
static int rdisp_fail(const char *why) {
    if(!strcmp(rdisp_why,"-")) rdisp_why=why;
    rdisp_stage="failed";InterlockedIncrement64(&rdisp_failures);return 0;
}
/* ---- pose arithmetic (double; rows convention as lockstep_pilot_predict.h) ---- */
static int rdisp_finite(const float f[16]) {
    for(unsigned i=0;i<16;i++) if((i&3)!=3 && !isfinite(f[i])) return 0;
    return 1;
}
static void rdisp_xf_from(const float f[16],rdisp_xf *x) {
    for(unsigned i=0;i<3;i++) {x->t[i]=f[i];for(unsigned j=0;j<3;j++) x->r[3*i+j]=f[4*(i+1)+j];}
}
static void rdisp_xf_to(const rdisp_xf *x,float f[16]) { /* the w lanes stay */
    for(unsigned i=0;i<3;i++) {f[i]=(float)x->t[i];for(unsigned j=0;j<3;j++) f[4*(i+1)+j]=(float)x->r[3*i+j];}
}
static void rdisp_identity(rdisp_xf *x) {
    memset(x,0,sizeof(*x));x->r[0]=x->r[4]=x->r[8]=1.0;
}
/* a, then b (a in b's frame): p_b = (p R_a + t_a) R_b + t_b. */
static void rdisp_compose(const rdisp_xf *a,const rdisp_xf *b,rdisp_xf *out) {
    rdisp_xf c;predict_mul(a->r,b->r,c.r);
    for(unsigned j=0;j<3;j++) c.t[j]=a->t[0]*b->r[j]+a->t[1]*b->r[3+j]+a->t[2]*b->r[6+j]+b->t[j];
    *out=c;
}
static int rdisp_inverse(const rdisp_xf *a,rdisp_xf *out) {
    const double *m=a->r;
    double c[9]={m[4]*m[8]-m[5]*m[7],m[2]*m[7]-m[1]*m[8],m[1]*m[5]-m[2]*m[4],
                 m[5]*m[6]-m[3]*m[8],m[0]*m[8]-m[2]*m[6],m[2]*m[3]-m[0]*m[5],
                 m[3]*m[7]-m[4]*m[6],m[1]*m[6]-m[0]*m[7],m[0]*m[4]-m[1]*m[3]};
    double det=m[0]*c[0]+m[1]*c[3]+m[2]*c[6];
    if(!(fabs(det)>1e-12)) return 0;
    rdisp_xf v;
    for(unsigned i=0;i<9;i++) v.r[i]=c[i]/det;
    for(unsigned j=0;j<3;j++) v.t[j]=-(a->t[0]*v.r[j]+a->t[1]*v.r[3+j]+a->t[2]*v.r[6+j]);
    *out=v;return 1;
}
/* The motion from a (step N-1) to b (step N): the translation and the local rotation vector (R_b = Exp(w) R_a). */
static void rdisp_motion_of(const rdisp_xf *a,const rdisp_xf *b,double dp[3],double w[3]) {
    double e[9];
    for(unsigned i=0;i<3;i++) dp[i]=b->t[i]-a->t[i];
    predict_mul_transposed(b->r,a->r,e);predict_log(e,w);
}
/* b advanced by s steps along that motion (s = -1: a, 0: b, 1: one more step). */
static void rdisp_advance(const rdisp_xf *b,const double dp[3],const double w[3],double s,rdisp_xf *out) {
    double ws[3]={s*w[0],s*w[1],s*w[2]},e[9];rdisp_xf c;
    predict_exp(ws,e);predict_mul(e,b->r,c.r);
    for(unsigned i=0;i<3;i++) c.t[i]=b->t[i]+s*dp[i];
    *out=c;
}
static double rdisp_norm3(const double v[3]) {return sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);}
static int rdisp_is_identity(const rdisp_xf *x) {
    static const double id[9]={1,0,0,0,1,0,0,0,1};
    for(unsigned i=0;i<9;i++) if(fabs(x->r[i]-id[i])>1e-9) return 0;
    return rdisp_norm3(x->t)<=1e-6;
}
static void rdisp_stat_add(rdisp_stat *s,double v,const double *edges) {
    if(!(v>=0)) v=1e30;
    ++s->n;s->sum+=v;if(v>s->max) s->max=v;
    if(edges) {unsigned b=0;while(b<RDISP_BUCKETS-1 && v>=edges[b]) ++b;++s->bucket[b];}
}
/* Prediction error of the constant-motion step: what extrapolating (prev motion, s = 1) gives against
 * the pose the simulation then had. */
static void rdisp_error(const rdisp_xf *prev,const double dp[3],const double w[3],const rdisp_xf *now,rdisp_stat *m,rdisp_stat *rad) {
    rdisp_xf guess;rdisp_advance(prev,dp,w,1.0,&guess);
    double d[3]={now->t[0]-guess.t[0],now->t[1]-guess.t[1],now->t[2]-guess.t[2]};
    rdisp_stat_add(m,rdisp_norm3(d),rdisp_pos_edges);rdisp_stat_add(rad,predict_angle(now->r,guess.r),NULL);
}
/* ---- reading the engine ---- */
static int rdisp_is_zone(unsigned char *object) {
    if(rdisp_is_a_override) return rdisp_is_a_override(object,RDISP_ZONE_CLASS);
    void **vtable=NULL;unsigned char (*is_a)(void *,int)=NULL;
    if(!sample_read(object,&vtable,8) || !vtable || !sample_read((unsigned char*)vtable+RDISP_CLASS_SLOT,&is_a,8) || !is_a) return 0;
    return is_a(object,RDISP_ZONE_CLASS)!=0;
}
static unsigned char *rdisp_vc(unsigned char *object,unsigned bank) {return object+RDISP_VC+bank*RDISP_VC_STRIDE;}
/* The player camera's view bank for the bank: table, entries, count, anchor. */
static int rdisp_table(unsigned bank,unsigned char **camera,unsigned char **table,unsigned char **entries,uint64_t *count,unsigned char **anchor) {
    unsigned char *universe=NULL;uint64_t capacity=0;
    if(!sample_read(image+RDISP_UNIVERSE,&universe,8) || !universe || !sample_read(universe+RDISP_CAMERA,camera,8) || !*camera) return 0;
    *table=*camera+RDISP_TABLE+bank*RDISP_TABLE_STRIDE;
    if(!sample_read(*table+RDISP_TABLE_CAPACITY,&capacity,8) || !sample_read(*table+RDISP_TABLE_COUNT,count,8) ||
       !sample_read(*table,anchor,8)) return 0;
    if(capacity==RDISP_TABLE_INLINE) *entries=*table+RDISP_TABLE_ENTRIES;
    else if(!sample_read(*table+RDISP_TABLE_ENTRIES,entries,8)) return 0;
    return *entries && *anchor && *count>=2 && *count<=RDISP_CHAIN_MAX && *count<=capacity;
}
static unsigned char *rdisp_entry_key(const unsigned char *bytes,unsigned i) {
    unsigned char *key;memcpy(&key,bytes+i*RDISP_ENTRY,8);return key;
}
static void rdisp_entry_pose(const unsigned char *bytes,unsigned i,float out[16]) {memcpy(out,bytes+i*RDISP_ENTRY+RDISP_ENTRY_POSE,64);}
/* ---- the step's sample (step frame, slot 10 entry) ---- */
static void rdisp_cam_reset(void) {rdisp_cam.valid=0;rdisp_cam_last_valid=0;rdisp_cam_motion=0;rdisp_cam_last_motion=0;}
static void rdisp_items_reset(void) {rdisp_items_valid=0;rdisp_item_count[0]=rdisp_item_count[1]=0;}
static int rdisp_sample_camera(unsigned bank) {
    rdisp_cam.valid=0;
    unsigned char *camera,*table,*entries,*anchor;uint64_t count;
    if(!rdisp_table(bank,&camera,&table,&entries,&count,&anchor) || !sample_read(entries,rdisp_cam.bytes,(size_t)count*RDISP_ENTRY)) return 0;
    if(rdisp_entry_key(rdisp_cam.bytes,0) || rdisp_entry_key(rdisp_cam.bytes,1)!=anchor) return 0;
    for(unsigned i=0;i<count;i++) {float f[16];rdisp_entry_pose(rdisp_cam.bytes,i,f);if(!rdisp_finite(f)) return 0;}
    rdisp_cam.camera=camera;rdisp_cam.table=table;rdisp_cam.entries=entries;rdisp_cam.anchor=anchor;rdisp_cam.count=count;
    for(unsigned i=1;i<count;i++) {
        unsigned char *key=rdisp_entry_key(rdisp_cam.bytes,i),*vc=key?rdisp_vc(key,bank):NULL;
        rdisp_cam.chain_ok[i]=vc && sample_read(vc,&rdisp_cam.chain_parent[i],8) && sample_read(vc+RDISP_VC_BLOCK,&rdisp_cam.chain_block[i],8) &&
            rdisp_cam.chain_block[i] && sample_read(rdisp_cam.chain_block[i],rdisp_cam.chain_pose[i],64);
    }
    float c[16];rdisp_entry_pose(rdisp_cam.bytes,1,c);rdisp_xf_from(c,&rdisp_cam.anchor_pose);
    /* The camera's own motion between the last two steps (in the anchor's frame). */
    int had=rdisp_cam_motion;double dp[3],w[3];memcpy(dp,rdisp_cam_dp,sizeof(dp));memcpy(w,rdisp_cam_w,sizeof(w));
    rdisp_cam_motion=0;
    if(rdisp_cam_last_valid && rdisp_cam_last_anchor==anchor) {
        if(had) rdisp_error(&rdisp_cam_last,dp,w,&rdisp_cam.anchor_pose,&rdisp_cam_err_m,&rdisp_cam_err_rad);
        rdisp_motion_of(&rdisp_cam_last,&rdisp_cam.anchor_pose,rdisp_cam_dp,rdisp_cam_w);
        if(rdisp_norm3(rdisp_cam_dp)>RDISP_SNAP_POSITION || rdisp_norm3(rdisp_cam_w)>RDISP_SNAP_ANGLE) ++rdisp_cam_snaps;
        else rdisp_cam_motion=1;
    } else if(rdisp_cam_last_valid) ++rdisp_cam_resets;
    rdisp_cam_last=rdisp_cam.anchor_pose;rdisp_cam_last_anchor=anchor;rdisp_cam_last_valid=1;
    rdisp_cam.valid=1;return 1;
}
static uint32_t rdisp_slot_of(const void *p) {
    uint64_t x=((uint64_t)(uintptr_t)p>>4)*0x9e3779b97f4a7c15ULL;return (uint32_t)(x>>(64-RDISP_HASH_BITS));
}
static int rdisp_lookup(unsigned gen,const void *object) {
    uint32_t h=rdisp_slot_of(object);
    for(unsigned probe=0;probe<RDISP_HASH;probe++,h=(h+1)&(RDISP_HASH-1)) {
        uint32_t v=rdisp_hash[gen][h];
        if(!v) return -1;
        if(rdisp_items[gen][v-1].object==object) return (int)(v-1);
    }
    return -1;
}
static void rdisp_insert(unsigned gen,unsigned index) {
    uint32_t h=rdisp_slot_of(rdisp_items[gen][index].object);
    while(rdisp_hash[gen][h]) h=(h+1)&(RDISP_HASH-1);
    rdisp_hash[gen][h]=index+1;
}
static int rdisp_sample_items(unsigned bank) {
    unsigned next=rdisp_gen^1,n=0;rdisp_item *items=rdisp_items[next];
    memset(rdisp_hash[next],0,sizeof(rdisp_hash[next]));rdisp_item_count[next]=0;rdisp_zone=NULL;
    for(unsigned i=1;rdisp_cam.valid && i<rdisp_cam.count && !rdisp_zone;i++) {
        unsigned char *key=rdisp_entry_key(rdisp_cam.bytes,i);
        if(key && rdisp_is_zone(key)) rdisp_zone=key;
    }
    unsigned char *begin=NULL,*end=NULL;
    if(!rdisp_zone || !sample_read(rdisp_vc(rdisp_zone,bank)+RDISP_VC_CHILDREN,&begin,8) || !sample_read(rdisp_vc(rdisp_zone,bank)+RDISP_VC_CHILDREN+8,&end,8) ||
       end<begin || (end-begin)%8) {++rdisp_zone_missing;rdisp_items_valid=0;rdisp_gen=next;return 0;}
    uint64_t children=(uint64_t)(end-begin)/8;
    if(children>RDISP_ITEMS) {++rdisp_capped;children=RDISP_ITEMS;}
    for(uint64_t c=0;c<children;c++) {
        rdisp_item *it=&items[n];unsigned char *object=NULL,*vc;
        if(!sample_read(begin+8*c,&object,8) || !object) continue;
        vc=rdisp_vc(object,bank);
        if(!sample_read(vc,&it->parent,8) || it->parent!=rdisp_zone || !sample_read(vc+RDISP_VC_BLOCK,&it->block,8) || !it->block ||
           ((uintptr_t)it->block&15) || !sample_read(it->block,it->pose,64) || !rdisp_finite(it->pose) || !sample_read(object+RDISP_ID,&it->id,8)) continue;
        it->object=object;it->motion=it->moved=it->snap=0;
        /* The previous step's pose of the same object (same ID, same parent): the motion N-1 -> N. */
        int k=rdisp_items_valid?rdisp_lookup(rdisp_gen,object):-1;
        if(k>=0) {
            const rdisp_item *p=&rdisp_items[rdisp_gen][k];
            int changed=memcmp(p->pose,it->pose,64)!=0;
            if(p->id!=it->id || p->parent!=it->parent) ++rdisp_reparented;
            else if(changed || (p->motion && !p->snap)) {
                rdisp_xf a,b;rdisp_xf_from(p->pose,&a);rdisp_xf_from(it->pose,&b);
                /* What extrapolating the previous motion one step would have drawn, against this step. */
                if(p->motion && !p->snap) rdisp_error(&a,p->dp,p->w,&b,&rdisp_err_m,&rdisp_err_rad);
                if(changed) {
                    rdisp_motion_of(&a,&b,it->dp,it->w);it->motion=1;
                    if(rdisp_norm3(it->dp)>RDISP_SNAP_POSITION || rdisp_norm3(it->w)>RDISP_SNAP_ANGLE) {it->snap=1;++rdisp_snaps;}
                    else it->moved=1;
                }
            }
        }
        rdisp_insert(next,n);++n;
    }
    rdisp_item_count[next]=n;rdisp_gen=next;rdisp_items_valid=1;
    if(n>rdisp_items_max) rdisp_items_max=n;
    return 1;
}
/* ---- the frame's display offset ---- */
static double rdisp_time_offset(void) {
    int64_t step=gate_tick_hz?rf_freq()/(int64_t)gate_tick_hz:0,ahead=rf_interval>rf_estimate?rf_interval:rf_estimate;
    if(step<=0) return 0;
    double s=(double)(rf_frame_start+ahead-rf_gap_first)/(double)step;
    return s<0?0:s>1?1:s;
}
__attribute__((unused)) static double rdisp_display_offset(void) { /* the hook interface */return rdisp_window || rdisp_open?rdisp_s_world:0.0;}
/* Slot 10 entry (MoveWorker, before predict_present_enter): the offset, and in a step frame the step's sample. */
static void rdisp_sample(void) {
    rdisp_window=0;rdisp_override_count=0;rdisp_s_time=rdisp_s_world=rdisp_s_camera=0;
    if(!stats.active || !rf_fps) return;
    rdisp_render_frame=InterlockedCompareExchange(&rf_frame_open,0,0)!=0;
    if(rdisp_render_frame) rdisp_s_time=rdisp_time_offset();
    rdisp_s_world=rdisp_motion_mode==RDISP_INTERPOLATE?rdisp_s_time-1.0:rdisp_s_time;
    rdisp_s_camera=rdisp_camera_mode==RDISP_INTERPOLATE?rdisp_s_time-1.0:rdisp_s_time;
    rdisp_window=1;
    if(!rdisp_armed || !InterlockedCompareExchange(&rdisp_active,0,0) || rdisp_render_frame) return;
    unsigned bank=2;
    if(!InterlockedCompareExchange(&rf_ready_flag,0,0) || !sample_read(image+RDISP_VIEW_INDEX,&bank,4) || bank>1) {
        rdisp_cam_reset();rdisp_items_reset();return;
    }
    LARGE_INTEGER t0,t1;QueryPerformanceCounter(&t0);
    rdisp_bank=bank;++rdisp_samples;
    if(!rdisp_sample_camera(bank)) {++rdisp_sample_fail;rdisp_cam_reset();rdisp_items_reset();}
    else if(rdisp_motion_mode) rdisp_sample_items(bank);
    QueryPerformanceCounter(&t1);
    rdisp_sample_us=rf_us(t1.QuadPart-t0.QuadPart);rdisp_sample_us_sum+=rdisp_sample_us;if(rdisp_sample_us>rdisp_sample_us_max) rdisp_sample_us_max=rdisp_sample_us;
    rdisp_last_tick=stats.issued;
}
/* The hook interface: a pose drawn instead for one component in this slot 10 call (see above). */
__attribute__((unused)) static int rdisp_override(void *object,const float pose[12]) { /* the hook interface */
    if(!rdisp_armed || !InterlockedCompareExchange(&rdisp_active,0,0) || !rdisp_window || !object || !pose || rdisp_override_count>=RDISP_OVERRIDES) {
        ++rdisp_override_refused;return 0;
    }
    for(unsigned i=0;i<12;i++) if(!isfinite(pose[i])) {++rdisp_override_refused;return 0;}
    for(unsigned i=0;i<rdisp_override_count;i++) if(rdisp_overrides[i].object==object) {memcpy(rdisp_overrides[i].pose,pose,sizeof(rdisp_overrides[i].pose));return 1;}
    rdisp_overrides[rdisp_override_count].object=object;memcpy(rdisp_overrides[rdisp_override_count].pose,pose,sizeof(rdisp_overrides[0].pose));
    ++rdisp_override_count;return 1;
}
/* ---- slot 10: the swaps ---- */
static int rdisp_is_overridden(const void *object) {
    for(unsigned i=0;i<rdisp_override_count;i++) if(rdisp_overrides[i].object==object) return 1;
    return 0;
}
/* A private copy of the engine's block with this pose, swapped into the view cache's +0x28. */
static int rdisp_swap_in(unsigned char *vc,unsigned char *engine,const float pose[16]) {
    if(rdisp_swap_count>=sizeof(rdisp_swaps)/sizeof(*rdisp_swaps)) return 0;
    unsigned char *mine=rdisp_blocks[rdisp_swap_count];
    if(!sample_read(engine,mine,RDISP_BLOCK_BYTES)) return 0;
    for(unsigned i=0;i<4;i++) memcpy(mine+16*i,pose+4*i,12);
    if(InterlockedCompareExchangePointer((void*volatile*)(vc+RDISP_VC_BLOCK),mine,engine)!=engine) return 0;
    rdisp_swaps[rdisp_swap_count].slot=vc+RDISP_VC_BLOCK;rdisp_swaps[rdisp_swap_count].engine=engine;rdisp_swaps[rdisp_swap_count].mine=mine;
    ++rdisp_swap_count;return 1;
}
static void rdisp_apply_items(double s,unsigned *moved,unsigned *swapped,unsigned *stale,double *disp) {
    const rdisp_item *items=rdisp_items[rdisp_gen];unsigned count=rdisp_item_count[rdisp_gen];
    for(unsigned i=0;i<count;i++) {
        const rdisp_item *it=&items[i];
        if(!it->moved) continue;
        ++*moved;
        if(rdisp_is_overridden(it->object) || (rdisp_owns && rdisp_owns(it->object))) {++rdisp_owned;continue;}
        unsigned char *vc=rdisp_vc(it->object,rdisp_bank),*block=NULL;float now[16];
        if(!sample_read(vc+RDISP_VC_BLOCK,&block,8) || block!=it->block || !sample_read(block,now,64) || memcmp(now,it->pose,64)) {++*stale;++rdisp_stale;continue;}
        rdisp_xf b,d;float shown[16];rdisp_xf_from(it->pose,&b);rdisp_advance(&b,it->dp,it->w,s,&d);
        memcpy(shown,it->pose,64);rdisp_xf_to(&d,shown);
        double moved_m=fabs(s)*rdisp_norm3(it->dp),moved_rad=fabs(s)*rdisp_norm3(it->w);
        if(moved_m>*disp) *disp=moved_m;
        if(moved_m>rdisp_disp_m) rdisp_disp_m=moved_m;
        if(moved_rad>rdisp_disp_rad) rdisp_disp_rad=moved_rad;
        if(rdisp_swap_in(vc,block,shown)) {++*swapped;++rdisp_swapped;} else {++*stale;++rdisp_stale;}
    }
}
static void rdisp_apply_overrides(unsigned *swapped) {
    for(unsigned i=0;i<rdisp_override_count;i++) {
        unsigned char *vc=rdisp_vc(rdisp_overrides[i].object,rdisp_bank),*block=NULL;float shown[16];
        if(!sample_read(vc+RDISP_VC_BLOCK,&block,8) || !block || ((uintptr_t)block&15) || !sample_read(block,shown,64)) {++rdisp_override_refused;continue;}
        for(unsigned r=0;r<4;r++) memcpy(shown+4*r,rdisp_overrides[i].pose+3*r,12);
        if(rdisp_swap_in(vc,block,shown)) {++*swapped;++rdisp_overridden;} else ++rdisp_override_refused;
    }
}
/* ---- slot 10: the camera's view bank ---- */
static int rdisp_cam_apply(double s,int camera_moves,double *cam_rad,double *cam_m) {
    if(!rdisp_cam.valid) return 0;
    unsigned char *camera,*table,*entries,*anchor;uint64_t count;unsigned char now[RDISP_CHAIN_MAX*RDISP_ENTRY];
    if(!rdisp_table(rdisp_bank,&camera,&table,&entries,&count,&anchor) || camera!=rdisp_cam.camera || table!=rdisp_cam.table ||
       entries!=rdisp_cam.entries || anchor!=rdisp_cam.anchor || count!=rdisp_cam.count || !sample_read(entries,now,(size_t)count*RDISP_ENTRY) ||
       memcmp(now,rdisp_cam.bytes,(size_t)count*RDISP_ENTRY)) {++rdisp_cam_stale;return 0;}
    /* D: the camera's own change in its frame. */
    rdisp_xf d,c1,inverse;rdisp_identity(&d);
    if(camera_moves && rdisp_cam_motion && s!=0) {
        rdisp_advance(&rdisp_cam.anchor_pose,rdisp_cam_dp,rdisp_cam_w,s,&c1);
        if(!rdisp_inverse(&rdisp_cam.anchor_pose,&inverse)) {++rdisp_cam_stale;return 0;}
        rdisp_compose(&c1,&inverse,&d);
        *cam_rad=fabs(s)*rdisp_norm3(rdisp_cam_w);*cam_m=fabs(s)*rdisp_norm3(rdisp_cam_dp);
        if(*cam_rad>rdisp_cam_rad_max) rdisp_cam_rad_max=*cam_rad;
        if(*cam_m>rdisp_cam_m_max) rdisp_cam_m_max=*cam_m;
    }
    /* The first chain member drawn displaced (stage 3, an override, a predictor's swap): the entries above follow it. */
    rdisp_xf g=d;unsigned k=0;
    for(unsigned i=1;i+1<count && !k;i++) {
        if(!rdisp_cam.chain_ok[i]) continue;
        unsigned char *key=rdisp_entry_key(rdisp_cam.bytes,i),*block=NULL;float pose[16];
        if(!sample_read(rdisp_vc(key,rdisp_bank)+RDISP_VC_BLOCK,&block,8) || !block || !sample_read(block,pose,64)) continue;
        if(block==rdisp_cam.chain_block[i] && !memcmp(pose,rdisp_cam.chain_pose[i],64)) continue;
        if(rdisp_cam.chain_parent[i]!=rdisp_entry_key(rdisp_cam.bytes,i+1) || !rdisp_finite(pose)) {++rdisp_chain_parent;continue;}
        rdisp_xf mk,mp,shown,mp_new,mp_inv;float f[16];
        rdisp_entry_pose(rdisp_cam.bytes,i,f);rdisp_xf_from(f,&mk);rdisp_entry_pose(rdisp_cam.bytes,i+1,f);rdisp_xf_from(f,&mp);
        rdisp_xf_from(pose,&shown);
        if(!rdisp_inverse(&mp,&mp_inv)) {++rdisp_chain_parent;continue;}
        rdisp_compose(&d,&mk,&mp_new);rdisp_compose(&mp_new,&shown,&mp_new);rdisp_compose(&mp_new,&mp_inv,&g);
        k=i;++rdisp_chain;
    }
    if(!k && rdisp_is_identity(&d)) {++rdisp_cam_identity;return 0;}
    /* The step frame of interpolate mode: X4 may have built view blocks from the bank before slot 10. */
    if(!rdisp_render_frame) {uint64_t blocks=0;if(sample_read(camera+RDISP_CAMERA_BLOCKS,&blocks,8) && blocks) ++rdisp_cam_cached;}
    rdisp_cam_open_count=0;rdisp_eye_open=0;
    if(rdisp_render_frame && sample_read(camera+RDISP_EYE,rdisp_eye_saved,RDISP_EYE_BYTES)) rdisp_eye_open=1;
    for(unsigned i=0;i<count;i++) {
        rdisp_xf m,shown;float f[16];rdisp_entry_pose(rdisp_cam.bytes,i,f);rdisp_xf_from(f,&m);
        rdisp_compose(i>=1 && i<=k?&d:&g,&m,&shown);
        memcpy(rdisp_cam_saved[i],f,64);rdisp_xf_to(&shown,f);memcpy(rdisp_cam_written[i],f,64);
    }
    for(unsigned i=0;i<count;i++) memcpy(entries+i*RDISP_ENTRY+RDISP_ENTRY_POSE,rdisp_cam_written[i],64);
    rdisp_cam_open_count=(unsigned)count;++rdisp_cam_written_n;
    return 1;
}
static void rdisp_cam_leave(void) {
    if(!rdisp_cam_open_count) return;
    int foreign=0;
    for(unsigned i=0;i<rdisp_cam_open_count;i++) {
        unsigned char *pose=rdisp_cam.entries+i*RDISP_ENTRY+RDISP_ENTRY_POSE;
        if(memcmp(pose,rdisp_cam_written[i],64)) {foreign=1;continue;}
        memcpy(pose,rdisp_cam_saved[i],64);
    }
    if(foreign) {++rdisp_foreign;rdisp_fail("camera_foreign");} else ++rdisp_cam_restored;
    /* The eye slot 10 computed from the written bank: back to the step's (outside slot 10 nothing differs from stage 1). */
    if(rdisp_eye_open) {memcpy(rdisp_cam.camera+RDISP_EYE,rdisp_eye_saved,RDISP_EYE_BYTES);++rdisp_eye_restored;}
    rdisp_cam_open_count=0;rdisp_eye_open=0;
}
static void rdisp_swaps_leave(void) {
    for(unsigned i=rdisp_swap_count;i-->0;) {
        rdisp_swap *w=&rdisp_swaps[i];
        if(InterlockedCompareExchangePointer((void*volatile*)w->slot,w->engine,w->mine)!=w->mine) {++rdisp_foreign;rdisp_fail("block_foreign");}
        else ++rdisp_restores;
    }
    rdisp_swap_count=0;
}
static void rdisp_row_add(unsigned moved,unsigned swapped,unsigned stale,unsigned table,double s,double cam_rad,double cam_m,double disp) {
    if(!rdisp_rows_on) return;
    rdisp_row *r=&rdisp_rows[rdisp_row_seq%RDISP_ROWS];FILETIME ft;GetSystemTimePreciseAsFileTime(&ft);
    uint64_t t=((uint64_t)ft.dwHighDateTime<<32|ft.dwLowDateTime)/10000u;
    r->seq=rdisp_row_seq;r->tick=stats.issued;r->wall_ms=t>11644473600000ull?t-11644473600000ull:0;r->kind=rdisp_render_frame;
    r->s=s;r->cam_rad=cam_rad;r->cam_m=cam_m;r->disp_m=disp;r->moved=moved;r->swapped=swapped;r->stale=stale;r->table=table;
    ++rdisp_row_seq;
}
/* Slot 10 (MoveWorker, after predict_present_enter, before the original): returns 1 when anything is in place. */
static int rdisp_enter(void) {
    int window=rdisp_window;rdisp_window=0;
    if(!window || !rdisp_armed || !InterlockedCompareExchange(&rdisp_active,0,0) || !stats.active) return 0;
    if(rdisp_open) {++rdisp_nested;rdisp_fail("nested");return 0;}
    /* What this call draws: extrapolate leaves the step frame as simulated; interpolate draws it one step back. */
    int items=rdisp_motion_mode && rdisp_items_valid && rdisp_s_world!=0;
    int camera=rdisp_camera_mode && rdisp_s_camera!=0;
    if(!items && !camera && !rdisp_override_count) return 0;
    LARGE_INTEGER t0,t1;QueryPerformanceCounter(&t0);
    unsigned moved=0,swapped=0,stale=0,table=0;double disp=0,cam_rad=0,cam_m=0;
    rdisp_swap_count=0;rdisp_cam_open_count=0;rdisp_eye_open=0;rdisp_open_thread=GetCurrentThreadId();rdisp_open=1;
    if(items) rdisp_apply_items(rdisp_s_world,&moved,&swapped,&stale,&disp);
    rdisp_apply_overrides(&swapped);
    rdisp_moved_last=moved;
    /* The bank follows the camera (stage 2) and a displaced chain member (stage 3, an override or a predictor's own swap). */
    if(camera || items || rdisp_override_count) table=(unsigned)rdisp_cam_apply(rdisp_s_camera,camera,&cam_rad,&cam_m);
    if(!rdisp_swap_count && !rdisp_cam_open_count) {rdisp_open=0;return 0;}   /* nothing moved: nothing to undo */
    ++rdisp_frames;if(rdisp_render_frame) ++rdisp_render_frames;else ++rdisp_step_frames;
    double s=items?rdisp_s_world:rdisp_s_camera;
    rdisp_s_last=s;if(fabs(s)>rdisp_s_max) rdisp_s_max=fabs(s);
    rdisp_row_add(moved,swapped,stale,table,s,cam_rad,cam_m,disp);
    QueryPerformanceCounter(&t1);rdisp_apply_us=rf_us(t1.QuadPart-t0.QuadPart);
    rdisp_stage="live";
    return 1;
}
/* After the original (also on unwind; before predict_present_leave): everything back. */
static void rdisp_leave(int entered) {
    if(!entered) return;
    LARGE_INTEGER t0,t1;QueryPerformanceCounter(&t0);
    rdisp_cam_leave();rdisp_swaps_leave();rdisp_open=0;
    QueryPerformanceCounter(&t1);rdisp_apply_us+=rf_us(t1.QuadPart-t0.QuadPart);
    rdisp_apply_us_sum+=rdisp_apply_us;if(rdisp_apply_us>rdisp_apply_us_max) rdisp_apply_us_max=rdisp_apply_us;
}
/* ---- arming, the gate's checks, status ---- */
static int rdisp_mode_of(const char *text,size_t length) {
    if(!text) return RDISP_OFF;
    if(length==11 && !memcmp(text,"extrapolate",11)) return RDISP_EXTRAPOLATE;
    if(length==11 && !memcmp(text,"interpolate",11)) return RDISP_INTERPOLATE;
    return RDISP_OFF;
}
/* gate.render_camera(mode) / gate.render_motion(mode): after gate.render_frames, before the start; afterwards the same value only.
 * Reads no engine memory: the probe arms these in its arming pass, before gate.boot, when the gate has not verified X4.exe yet
 * and image is still NULL (reading the pins here faulted, 0xC0000005, live runs R3/R4 2026-11-01). The arm is recorded as
 * pending; the gate's first tick checks the pins and activates it (rdisp_check). Until then nothing is sampled or swapped. */
static int rdisp_arm(int camera,int mode,int started) {
    int *slot=camera?&rdisp_camera_mode:&rdisp_motion_mode;
    if(mode!=RDISP_EXTRAPOLATE && mode!=RDISP_INTERPOLATE) return 0;
    if(started) return *slot==mode;
    if(!rf_fps || rdisp_pins_checked<0) {rdisp_stage="refused";return 0;}
    *slot=mode;rdisp_armed=1;rdisp_pending=1;rdisp_restored=1;rdisp_stage="pending";rdisp_why="-";rdisp_failures=0;
    rdisp_cam_reset();rdisp_items_reset();
    InterlockedExchange(&rdisp_active,0);return 1;
}
/* Main-thread gate tick (the gate started: X4.exe verified, image set). The first one activates a pending arm when the pins
 * hold (else why=pins); a failure stops the gate (183). */
static int rdisp_check(void) {
    if(!rdisp_armed) return 1;
    if(rdisp_pending) {
        rdisp_pending=0;
        if(!image || !rdisp_pins()) return rdisp_fail("pins");
        rdisp_stage="armed";InterlockedExchange(&rdisp_active,1);
    }
    return !InterlockedCompareExchange64(&rdisp_failures,0,0);
}
/* finish(): nothing new from here; a slot 10 call in flight puts its own swaps back in its __finally. */
static void rdisp_restore(void) {
    InterlockedExchange(&rdisp_active,0);rdisp_pending=0;
    if(!rdisp_armed) return;
    for(unsigned i=0;i<200 && rdisp_open && GetCurrentThreadId()!=rdisp_open_thread;i++) Sleep(1);
    rdisp_restored=!rdisp_open && !rdisp_swap_count && !rdisp_cam_open_count;
    if(strcmp(rdisp_stage,"failed")) rdisp_stage="stopped";
}
static const char *rdisp_mode_name(int mode) {return mode==RDISP_EXTRAPOLATE?"extrapolate":mode==RDISP_INTERPOLATE?"interpolate":"off";}
static double rdisp_mean(const rdisp_stat *s) {return s->n?s->sum/(double)s->n:0.0;}
static int rdisp_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,
        "camera=%s motion=%s armed=%d active=%ld stage=%s why=%s tick=%llu s_last=%.3f s_max=%.3f samples=%llu sample_fail=%llu "
        "sample_us=%llu/%llu/%llu frames=%llu step_frames=%llu render_frames=%llu apply_us=%llu/%llu/%llu zone=%d items=%u items_max=%llu capped=%llu "
        "zone_missing=%llu moved=%llu swapped=%llu restores=%llu owned=%llu overrides=%llu override_refused=%llu stale=%llu snaps=%llu reparented=%llu "
        "disp_m=%.4g disp_rad=%.4g err_m=%.4g/%.4g err_rad=%.4g/%.4g err_n=%llu err_hist=%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu "
        "cam_written=%llu cam_restored=%llu eye_restored=%llu cam_identity=%llu cam_stale=%llu cam_snaps=%llu cam_resets=%llu cam_cached=%llu "
        "chain=%llu chain_parent=%llu cam_rad=%.4g cam_m=%.4g cam_err_m=%.4g/%.4g cam_err_rad=%.4g/%.4g cam_err_n=%llu "
        "foreign=%llu nested=%llu failures=%lld restored=%d",
        rdisp_mode_name(rdisp_camera_mode),rdisp_mode_name(rdisp_motion_mode),rdisp_armed,rdisp_active,rdisp_stage,rdisp_why,
        (unsigned long long)rdisp_last_tick,rdisp_s_last,rdisp_s_max,(unsigned long long)rdisp_samples,(unsigned long long)rdisp_sample_fail,
        (unsigned long long)rdisp_sample_us,(unsigned long long)(rdisp_samples?rdisp_sample_us_sum/rdisp_samples:0),(unsigned long long)rdisp_sample_us_max,
        (unsigned long long)rdisp_frames,(unsigned long long)rdisp_step_frames,(unsigned long long)rdisp_render_frames,
        (unsigned long long)rdisp_apply_us,(unsigned long long)(rdisp_frames?rdisp_apply_us_sum/rdisp_frames:0),(unsigned long long)rdisp_apply_us_max,
        rdisp_zone!=NULL,rdisp_item_count[rdisp_gen],(unsigned long long)rdisp_items_max,(unsigned long long)rdisp_capped,
        (unsigned long long)rdisp_zone_missing,(unsigned long long)rdisp_moved_last,(unsigned long long)rdisp_swapped,(unsigned long long)rdisp_restores,
        (unsigned long long)rdisp_owned,(unsigned long long)rdisp_overridden,(unsigned long long)rdisp_override_refused,(unsigned long long)rdisp_stale,
        (unsigned long long)rdisp_snaps,(unsigned long long)rdisp_reparented,rdisp_disp_m,rdisp_disp_rad,
        rdisp_mean(&rdisp_err_m),rdisp_err_m.max,rdisp_mean(&rdisp_err_rad),rdisp_err_rad.max,(unsigned long long)rdisp_err_m.n,
        (unsigned long long)rdisp_err_m.bucket[0],(unsigned long long)rdisp_err_m.bucket[1],(unsigned long long)rdisp_err_m.bucket[2],
        (unsigned long long)rdisp_err_m.bucket[3],(unsigned long long)rdisp_err_m.bucket[4],(unsigned long long)rdisp_err_m.bucket[5],
        (unsigned long long)rdisp_err_m.bucket[6],(unsigned long long)rdisp_err_m.bucket[7],
        (unsigned long long)rdisp_cam_written_n,(unsigned long long)rdisp_cam_restored,(unsigned long long)rdisp_eye_restored,
        (unsigned long long)rdisp_cam_identity,(unsigned long long)rdisp_cam_stale,(unsigned long long)rdisp_cam_snaps,(unsigned long long)rdisp_cam_resets,
        (unsigned long long)rdisp_cam_cached,(unsigned long long)rdisp_chain,(unsigned long long)rdisp_chain_parent,rdisp_cam_rad_max,rdisp_cam_m_max,
        rdisp_mean(&rdisp_cam_err_m),rdisp_cam_err_m.max,rdisp_mean(&rdisp_cam_err_rad),rdisp_cam_err_rad.max,(unsigned long long)rdisp_cam_err_m.n,
        (unsigned long long)rdisp_foreign,(unsigned long long)rdisp_nested,(long long)rdisp_failures,rdisp_restored);
    return n>0 && (size_t)n<capacity?n:0;
}
/* Row seq (or the oldest still kept); *next = the cursor after it; 0 when there is none yet. */
static int rdisp_row_text(uint64_t seq,char *out,size_t capacity,uint64_t *next) {
    if(seq>=rdisp_row_seq) return 0;
    if(rdisp_row_seq-seq>RDISP_ROWS) seq=rdisp_row_seq-RDISP_ROWS;
    const rdisp_row *r=&rdisp_rows[seq%RDISP_ROWS];
    int n=snprintf(out,capacity,"seq=%llu tick=%llu kind=%s wall_ms=%llu s=%.4f cam_mrad=%.3f cam_mm=%.1f moved=%u swapped=%u stale=%u table=%u disp_mm=%.1f",
        (unsigned long long)r->seq,(unsigned long long)r->tick,r->kind?"render":"step",(unsigned long long)r->wall_ms,r->s,r->cam_rad*1000.0,r->cam_m*1000.0,
        r->moved,r->swapped,r->stale,r->table,r->disp_m*1000.0);
    *next=seq+1;
    return n>0 && (size_t)n<capacity?n:0;
}

/* The join with the own-ship prediction, at its arm. Without it each draws the own ship on its own: the prediction at
 * offset 0 while the camera and the world are drawn at s (one step is 8 m at 240 m/s), stage 3 counting its swap as stale=
 * (live p2e: owned=0 stale=2068; with the join p2f: owned=2162 stale=0, census 0). */
static int opred_arm_joined(int mode,int others,unsigned tau_ms,unsigned cap,const char **why) {
    if(!opred_arm(mode,others,tau_ms,cap,why)) return 0;
    opred_display_offset=rdisp_display_offset;rdisp_owns=opred_owns;
    return 1;
}

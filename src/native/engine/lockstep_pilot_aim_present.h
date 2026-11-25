/* This client's own guns presented without the input delay (native_gate_pilot_aim_present, a default; needs
 * native_gate_pilot_aim). User report (rk9, 24 Nov, player 2): the drawn aim lagged behind the cursor; asked for the
 * same presentation without the input delay that the own ship's movement has.
 *
 * The record of tick c is applied at c + pilot_delay on every client (lockstep_pilot_aim.h): the gimbals, the shots and
 * the hits follow the cursor three ticks late, and the dots (X4's per-gun aim indicator, which shows where each gun points)
 * and the aim assist icon with them. The own-ship prediction (lockstep_own_predict.h) draws the own ship from the inputs
 * not yet replayed; this does the same for the own guns, on this client only, for presentation only:
 *   the sample   the capture's latest aim of this client's own slot (paim_take: the pick or the cursor ray, as a point in
 *                the ship's frame, the steering byte), kept before the track turns it for the delay (paimp_note_sample);
 *   the aim      X4's rules on that sample now (the same as paim_aim's: the manual point; with the assist bit and the
 *                slot's target the cone around P_k and the lock; the assist alone on a target in range), and in the lock
 *                X4's lead for that gun (815330, the HUD's lead marker's own function, with the player lead: slot 16's
 *                yes there, lockstep_pilot_aim.h). Asked quietly (paim_quiet): the simulation's counters stay its own;
 *   the joints   the gun's skeleton joints (+0x208 -> +0x48: the gimbal, lockstep_ik_diag.h) that point its muzzle frame
 *                (81cb80, the frame the dot is drawn from) at that aim: a damped Gauss-Newton on X4's own forward
 *                kinematics (81cb80 itself, with the trial values answered by the value slot below), each rotation
 *                kept inside its limits (the joint's DOF entry {type, min, max}: the definition's chains *(gun+0x58)+0x1c8,
 *                their joint definitions +0x150, as e20370 lays the joints out); translations keep their values;
 *   the view     U::IKValue's value slot (vtable 0x2c450d0 +0x28, the read e20370 makes for every bone transform)
 *                answers those values for those joints while slot 10 draws (render_rng_execute: paimp_enter before the
 *                original, paimp_leave after it, on the frame thread) and X4's own value at every other read. Slot 10
 *                does not simulate: the dots (81d700 -> 81cb80), the gun models and the muzzle effects drawn in it take
 *                the presented aim; Weapon::Move, the shots and the hits read the joints outside it and keep the replayed
 *                aim, the same on every client. Nothing is written: the simulation's joints are never touched;
 *   the icon     slot 34 asked by GetAimAssistState (0xb02450, the HUD's aim assist icon, slot 6) for one of this
 *                client's own guns: X4's lock rule on the sample now (paimp_locked_answer).
 * The gimbal is presented where X4's IK would put it for the aim, without the turn: X4's own gimbals turn at a limited rate
 * (0xe1f840), which the presentation skips. A gun whose joints are not of that shape (more than PAIMP_JOINTS, an unreadable
 * definition), whose solve does not converge, or without a fresh sample is drawn as simulated (counted).
 * Status: gate.pilot_aim_present_status. Off: gate.pilot_aim_present(0) (native_gate_pilot_aim_present=false). */
#define PAIMP_VALUE_VTABLE 0x2c450d0u   /* U::IKValue, at +0x678 of each 0x680-byte joint (its vbtable 0x2c450c0, +4: 0x678) */
#define PAIMP_VALUE_SLOT 0x28u          /* its value: the thunk e83818 -> 474600, the joint's +0x08 */
#define PAIMP_VALUE_THUNK 0xe83818u
#define PAIMP_VALUE_AT 0x678u
#define PAIMP_VBT_OFFSET 0x2c450c4u     /* the vbtable's offset of the IKValue */
#define PAIMP_MUZZLE 0x81cb80u          /* (gun, out): the muzzle frame in the gun's frame (81d700's: the dot along its z row) */
#define PAIMP_LEAD 0x815330u            /* (gun, out, target, space): X4's lead of the gun on the target (the HUD's marker) */
#define PAIMP_STATE_RETURN 0xb02558u    /* GetAimAssistState after its slot 34 call at 0xb02552 */
#define PAIMP_DEF 0x58u                 /* the component's definition (e20370: [owner]+0x58) */
#define PAIMP_CHAINS 0x1c8u             /* ... its chains: a vector of {begin, end, cap} of joint definitions */
#define PAIMP_DOFS 0x150u               /* a joint definition's DOF vector: 12-byte entries {type 1..6, min, max} */
#define PAIMP_NO_CHAINS 0x394d340u      /* e20370's empty defaults */
#define PAIMP_NO_DOFS 0x394d2c8u
#define PAIMP_JOINTS 4u
#define PAIMP_SAMPLE_AGE 2u             /* ticks: an older sample is not shown */
#define PAIMP_ITERATIONS 6u
#define PAIMP_STEP 1e-3f                /* rad: the Jacobian's difference */
#define PAIMP_DAMPING 1e-6f
#define PAIMP_CONVERGED 1e-4f           /* rad: the barrel's angle off the aim, when not clamped */
static const struct {unsigned rva,size;const char *sha;} paimp_pins_list[]={
    {PAIMP_VALUE_THUNK,19u,"4e175422d7c691534881fd802fcde97119283ed28b358e8bf262a074eb2bfff7"},
    {0x474600u,6u,"aa402fd3d245c68d45de2119127b5f5df6575b7b25673c2d2e9bb18009abc60a"},     /* the value: movss xmm0,[rcx-0x18]; ret */
    {0xe20370u,488u,"e145daf89feee6d906f69c7a06ae6009ce603f855651d04e319f0fba29e7302e"},   /* a bone's transform from its joints */
    {0xe22b70u,1364u,"cc2d26f173592d0f711af3ee816481a61a01b3123c2035866cc8686c6e5a3812"},  /* a bone's frame */
    {0x81c960u,541u,"ccbc84dfc72cf3becc51931243418e6822e749bb84eb505fe14872a567e10e1f"},   /* U::Weapon's frame slot 0x1ec8 */
    {PAIMP_MUZZLE,294u,"28f69f6182127e4d0183f20c1d8020dd8cb82d09e3aa7e039719a5da527826a3"}};
typedef struct {unsigned char *at;float value,sim,lo,hi;int type;} paimp_joint;
typedef struct {unsigned char *gun;unsigned count;int shown,locked;paimp_joint j[PAIMP_JOINTS];} paimp_gun;
typedef struct {
    float *(*muzzle)(void *,float *);                 /* 81cb80 */
    float *(*lead)(void *,float *,void *,void *);     /* 815330 */
} paimp_fns;
static paimp_fns paimp_fn;
static paimp_gun paimp_guns[PFIRE_GUNS];
static unsigned paimp_gun_count;
static volatile int paimp_active;      /* the value slot answers the table (frame thread, inside slot 10) */
static int paimp_armed,paimp_installed,paimp_pins_checked;
static const char *paimp_why="-";
typedef float (*paimp_value_fn)(void *);
static paimp_value_fn paimp_value_original;
static struct {uint64_t tick;float point[3];unsigned char steer;int valid;} paimp_sample;
static uint64_t paimp_frames,paimp_guns_seen,paimp_shown,paimp_locks,paimp_points,paimp_rest,paimp_stale,paimp_shape,paimp_unsolved,
    paimp_clamped,paimp_values,paimp_icons,paimp_icon_locks,paimp_samples;
static float paimp_last_error=-1.0f;
/* The first own gun of the last frame drawn: its presented and simulated joint values, its lock (the trace rows). */
static struct {uint64_t tick;unsigned count;int locked;float shown[PAIMP_JOINTS],sim[PAIMP_JOINTS];} paimp_last;
static char paimp_dofs_seen[160]="-";   /* the first own gun's joints as found: type:min:max */
static unsigned (*paimp_own_override)(void);   /* tests */

static int paimp_pins(void) {
    if(!paimp_pins_checked) {
        paimp_pins_checked=1;
        int32_t offset=0;void *entry=NULL;
        for(unsigned i=0;i<sizeof(paimp_pins_list)/sizeof(*paimp_pins_list);i++)
            if(!hash_ok(image+paimp_pins_list[i].rva,paimp_pins_list[i].size,paimp_pins_list[i].sha)) paimp_pins_checked=-1;
        if(!read_local(image+PAIMP_VBT_OFFSET,&offset,4) || offset!=(int32_t)PAIMP_VALUE_AT) paimp_pins_checked=-1;
        if(!read_local(pnat_base+PAIMP_VALUE_VTABLE+PAIMP_VALUE_SLOT,&entry,8) || entry!=(void*)(image+PAIMP_VALUE_THUNK)) paimp_pins_checked=-1;
    }
    return paimp_pins_checked>0;
}
static unsigned paimp_own(void) {return paimp_own_override?paimp_own_override():paim_own_slot();}
/* The capture (paim_take) of this client's own slot: its latest aim, as sampled. */
static void paimp_note_sample(unsigned slot,const float aim[3],unsigned char steer) {
    if(!paimp_armed || slot!=paimp_own()) return;
    memcpy(paimp_sample.point,aim,12);paimp_sample.steer=steer;paimp_sample.tick=stats.issued;paimp_sample.valid=1;++paimp_samples;
}
static int paimp_fresh(void) {
    return paimp_sample.valid && paimp_sample.tick<=stats.issued && stats.issued-paimp_sample.tick<=PAIMP_SAMPLE_AGE;
}
/* X4's lead of a gun on a target in space (815330): 1 with a finite, nonzero point. */
static int paimp_lead(unsigned char *gun,unsigned char *target,void *space,float out[3]) {
    _Alignas(16) float point[4]={0.0f,0.0f,0.0f,0.0f};
    if(!paimp_fn.lead || !paimp_fn.lead(gun,point,target,space)) return 0;
    if(!isfinite(point[0]) || !isfinite(point[1]) || !isfinite(point[2]) || (point[0]==0.0f && point[1]==0.0f && point[2]==0.0f)) return 0;
    memcpy(out,point,12);return 1;
}
/* X4's aim rules (paim_aim's) on this client's sample now, for one of its own guns, in space: 1 aiming (*locked, and the
 * point when point is given), 0 at rest or no fresh sample. Quiet: no simulation counter moves. */
static int paimp_aim_now(unsigned slot,unsigned char *gun,void *space,float point[3],int *locked) {
    unsigned char *ship=slot>=1 && slot<=PILOT_SLOTS?pilot_slots[slot-1].object:NULL;float t[16],q[4]={0.0f,0.0f,0.0f,0.0f};
    *locked=0;
    if(!ship || !gun || !space || !pstk_fn.transform || !paimp_fresh()) return 0;
    const unsigned char steer=paimp_sample.steer;
    if(!(steer&(PAIM_STEER_MANUAL|PAIM_STEER_ASSIST))) return 0;
    int aiming=0;
    paim_quiet=1;
    if(steer&PAIM_STEER_MANUAL) {
        pstk_identity(t);pstk_fn.transform(ship,t,space,0);pstk_apply(t,paimp_sample.point,q);
        if(isfinite(q[0]) && isfinite(q[1]) && isfinite(q[2])) {
            aiming=1;
            if(point) memcpy(point,q,12);
            unsigned char *target=steer&PAIM_STEER_ASSIST?paim_assist_target(slot,gun,0):NULL;float cone=0.0f,min_distance=0.0f,cosine=0.0f;
            if(target && paim_config(&cone,&min_distance) && paim_cone(slot,q,target,gun,space,cone,&cosine) && paim_lock_ok(target,gun,min_distance))
                *locked=!point || paimp_lead(gun,target,space,point);
        }
    } else {   /* the aim assist alone: the lock on a target in the gun's range */
        unsigned char *target=paim_assist_target(slot,gun,1);float cone=0.0f,min_distance=0.0f;
        if(target && paim_config(&cone,&min_distance) && paim_lock_ok(target,gun,min_distance) && (!point || paimp_lead(gun,target,space,point)))
            aiming=*locked=1;
    }
    paim_quiet=0;
    return aiming;
}
/* The space the presentation works in: the zone of the slot's player. */
static void *paimp_space(unsigned slot) {
    const unsigned char *player=uirep_slot_player(slot);
    return player && pstk_fn.zone?pstk_fn.zone((unsigned char*)player):NULL;
}
/* Slot 34 at GetAimAssistState for one of this client's own guns: X4's lock rule on the sample now. */
static unsigned char paimp_locked_answer(void *controller,uintptr_t from,int *answered) {
    unsigned char *gun=NULL;int locked=0;
    *answered=0;
    if(!paimp_armed || from!=(uintptr_t)(image+PAIMP_STATE_RETURN) || GetCurrentThreadId()!=ui_thread || !(gun=paim_own_gun(controller))) return 0;
    const unsigned slot=paimp_own();void *space=paimp_space(slot);
    if(!space || !paimp_fresh()) {++paimp_stale;return 0;}   /* no sample: X4's answer from the simulation's lock */
    paimp_aim_now(slot,gun,space,NULL,&locked);
    *answered=1;++paimp_icons;
    if(locked) ++paimp_icon_locks;
    return (unsigned char)locked;
}
/* The value slot: this gun's presented joint while the table answers (frame thread, inside slot 10), else X4's. */
static float paimp_value_answer(void *self) {
    if(paimp_active && GetCurrentThreadId()==ui_thread)
        for(unsigned g=0;g<paimp_gun_count;g++)
            for(unsigned k=0;k<paimp_guns[g].count;k++)
                if(paimp_guns[g].j[k].at+PAIMP_VALUE_AT==(unsigned char*)self) {++paimp_values;return paimp_guns[g].j[k].value;}
    return paimp_value_original(self);
}
static float paimp_value_get(void *self) {return paimp_value_answer(self);}
/* A joint's DOF entry as e20370 lays them out: groups (skeleton +0x10) one per chain, a chain's joint definitions in order,
 * each with its DOF entries, one joint per entry. 1 found. */
static int paimp_dof(unsigned char *gun,unsigned group,unsigned index,int *type,float *lo,float *hi) {
    unsigned char *def=NULL,*chains=NULL;uintptr_t cv[2]={0,0},jv[2]={0,0};unsigned acc=0;
    if(!read_local(gun+PAIMP_DEF,&def,8) || !def || !read_local(def+PAIMP_CHAINS,&chains,8)) return 0;
    if(!chains) chains=image+PAIMP_NO_CHAINS;
    if(!read_local(chains,cv,16) || cv[1]<cv[0] || (cv[1]-cv[0])%0x18u || group>=(cv[1]-cv[0])/0x18u) return 0;
    if(!read_local((void*)(cv[0]+group*0x18u),jv,16) || jv[1]<jv[0] || (jv[1]-jv[0])%8u || (jv[1]-jv[0])/8u>64u) return 0;
    for(uintptr_t at=jv[0];at<jv[1];at+=8u) {
        unsigned char *joint=NULL,*dofs=NULL;uintptr_t dv[2]={0,0};
        if(!read_local((void*)at,&joint,8) || !joint || !read_local(joint+PAIMP_DOFS,&dofs,8)) return 0;
        if(!dofs) dofs=image+PAIMP_NO_DOFS;
        if(!read_local(dofs,dv,16) || dv[1]<dv[0] || (dv[1]-dv[0])%12u || (dv[1]-dv[0])/12u>6u) return 0;
        const unsigned n=(unsigned)((dv[1]-dv[0])/12u);
        if(index<acc+n) {
            struct {int32_t type;float lo,hi;} e;
            if(!read_local((void*)(dv[0]+(index-acc)*12u),&e,12)) return 0;
            *type=e.type;*lo=e.lo;*hi=e.hi;return 1;
        }
        acc+=n;
    }
    return 0;
}
typedef struct {paimp_gun *g;int bad;} paimp_collect_ctx;
static void paimp_collect_one(void *ctx,uint16_t group,uint16_t joint,unsigned char *at) {
    paimp_collect_ctx *c=(paimp_collect_ctx*)ctx;paimp_gun *g=c->g;paimp_joint *j;
    if(c->bad) return;
    if(g->count>=PAIMP_JOINTS) {c->bad=1;return;}
    j=&g->j[g->count];memset(j,0,sizeof(*j));j->at=at;
    if(!read_local(at+0x08,&j->sim,4) || !isfinite(j->sim) || !paimp_dof(g->gun,group,joint,&j->type,&j->lo,&j->hi)) {c->bad=1;return;}
    if(!isfinite(j->lo) || !isfinite(j->hi) || j->lo>j->hi) {j->lo=-3.14159265f;j->hi=3.14159265f;}
    j->value=j->sim;++g->count;
}
static int (*paimp_collect_override)(paimp_gun *);   /* tests */
static int paimp_collect(paimp_gun *g) {
    if(paimp_collect_override) return paimp_collect_override(g);
    paimp_collect_ctx c={g,0};g->count=0;
    const int walked=join_ikv_walk(g->gun,paimp_collect_one,&c);
    return walked==1 && !c.bad && g->count;
}
static int paimp_rotation(const paimp_joint *j) {return j->type>=4 && j->type<=6;}
/* The muzzle frame with the trial values (the value slot answers them while paimp_active). */
static int paimp_fk(paimp_gun *g,const float *v,float m[16]) {
    for(unsigned k=0;k<g->count;k++) g->j[k].value=v[k];
    memset(m,0,64);
    if(!paimp_fn.muzzle || !paimp_fn.muzzle(g->gun,m)) return 0;
    for(unsigned i=0;i<16;i++) if(!isfinite(m[i])) return 0;
    return 1;
}
/* The barrel's error against the aim (gun frame): the muzzle's x and y rows on the unit direction to the aim; 0 off behind. */
static int paimp_residual(const float m[16],const float aim[3],float r[2]) {
    float d[3]={aim[0]-m[0],aim[1]-m[1],aim[2]-m[2]};
    const float n2=paim_dot(d,d);
    if(!(n2>1e-6f)) return 0;
    const float inv=1.0f/sqrtf(n2);
    for(unsigned i=0;i<3;i++) d[i]*=inv;
    r[0]=paim_dot(m+4,d);r[1]=paim_dot(m+8,d);
    return paim_dot(m+12,d)>0.0f;
}
/* Small symmetric positive system (n <= PAIMP_JOINTS), Gaussian elimination: 1 solved. */
static int paimp_solve(float a[PAIMP_JOINTS][PAIMP_JOINTS],float b[PAIMP_JOINTS],unsigned n) {
    for(unsigned c=0;c<n;c++) {
        unsigned p=c;
        for(unsigned r=c+1;r<n;r++) if(fabsf(a[r][c])>fabsf(a[p][c])) p=r;
        if(!(fabsf(a[p][c])>1e-12f)) return 0;
        if(p!=c) {for(unsigned k=0;k<n;k++) {float x=a[c][k];a[c][k]=a[p][k];a[p][k]=x;} float x=b[c];b[c]=b[p];b[p]=x;}
        for(unsigned r=c+1;r<n;r++) {const float f=a[r][c]/a[c][c];for(unsigned k=c;k<n;k++) a[r][k]-=f*a[c][k];b[r]-=f*b[c];}
    }
    for(unsigned c=n;c-->0;) {float x=b[c];for(unsigned k=c+1;k<n;k++) x-=a[c][k]*b[k];b[c]=x/a[c][c];}
    return 1;
}
/* The joints that point the muzzle at the aim (gun frame), inside their limits: damped Gauss-Newton on X4's own forward
 * kinematics. 1 with the values in g (converged, or held at a limit), 0 not solved (the values back at the simulation's). */
static int paimp_ik(paimp_gun *g,const float aim[3]) {
    float v[PAIMP_JOINTS],m[16],r[2],best=-1.0f;unsigned n=g->count;int clamped=0;
    for(unsigned k=0;k<n;k++) v[k]=g->j[k].sim;
    for(unsigned it=0;it<PAIMP_ITERATIONS;it++) {
        if(!paimp_fk(g,v,m) || !paimp_residual(m,aim,r)) break;
        best=sqrtf(r[0]*r[0]+r[1]*r[1]);
        if(best<PAIMP_CONVERGED) break;
        float jac[2][PAIMP_JOINTS],a[PAIMP_JOINTS][PAIMP_JOINTS],b[PAIMP_JOINTS];
        for(unsigned k=0;k<n;k++) {
            float w[PAIMP_JOINTS],mk[16],rk[2]={r[0],r[1]};
            memcpy(w,v,sizeof(w));
            if(paimp_rotation(&g->j[k])) {w[k]+=PAIMP_STEP;if(!paimp_fk(g,w,mk) || !paimp_residual(mk,aim,rk)) {rk[0]=r[0];rk[1]=r[1];}}
            jac[0][k]=(rk[0]-r[0])/PAIMP_STEP;jac[1][k]=(rk[1]-r[1])/PAIMP_STEP;
        }
        for(unsigned p=0;p<n;p++) {
            b[p]=-(jac[0][p]*r[0]+jac[1][p]*r[1]);
            for(unsigned q=0;q<n;q++) a[p][q]=jac[0][p]*jac[0][q]+jac[1][p]*jac[1][q]+(p==q?PAIMP_DAMPING:0.0f);
        }
        if(!paimp_solve(a,b,n)) break;
        clamped=0;
        for(unsigned k=0;k<n;k++) {
            if(!paimp_rotation(&g->j[k])) continue;
            float x=v[k]+b[k];
            if(x<g->j[k].lo) {x=g->j[k].lo;clamped=1;}
            if(x>g->j[k].hi) {x=g->j[k].hi;clamped=1;}
            v[k]=x;
        }
    }
    if(paimp_fk(g,v,m) && paimp_residual(m,aim,r)) best=sqrtf(r[0]*r[0]+r[1]*r[1]);
    else best=-1.0f;
    paimp_last_error=best;
    if(!(best>=0.0f) || (!clamped && best>PAIMP_CONVERGED*10.0f)) {
        for(unsigned k=0;k<n;k++) g->j[k].value=g->j[k].sim;
        return 0;
    }
    if(clamped) ++paimp_clamped;
    for(unsigned k=0;k<n;k++) g->j[k].value=v[k];
    return 1;
}
/* The aim (space) into the gun's frame: the transpose of its axes (orthonormal) on the offset. */
static int paimp_to_gun(unsigned char *gun,void *space,const float in[3],float out[3]) {
    float t[16];
    pstk_identity(t);pstk_fn.transform(gun,t,space,0);
    const float d[3]={in[0]-t[0],in[1]-t[1],in[2]-t[2]};
    out[0]=paim_dot(d,t+4);out[1]=paim_dot(d,t+8);out[2]=paim_dot(d,t+12);
    return isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]);
}
static void paimp_note_dofs(const paimp_gun *g) {
    int n=0;
    if(paimp_dofs_seen[0]!='-') return;
    for(unsigned k=0;k<g->count && n>=0 && (size_t)n<sizeof(paimp_dofs_seen);k++)
        n+=snprintf(paimp_dofs_seen+n,sizeof(paimp_dofs_seen)-(size_t)n,"%s%d:%.4f:%.4f",k?",":"",g->j[k].type,(double)g->j[k].lo,(double)g->j[k].hi);
}
/* Slot 10, before the original (frame thread): this client's own guns at the aim of its sample now. 1 when the table is up. */
static int paimp_enter(void) {
    if(!paimp_armed || !paim_armed || GetCurrentThreadId()!=ui_thread || paimp_active) return 0;
    const unsigned slot=paimp_own();void *space=NULL;
    paimp_gun_count=0;++paimp_frames;
    if(slot<1 || slot>PILOT_SLOTS || !pfire_slots[slot-1].leased) return 0;
    if(!paimp_fresh() || !(space=paimp_space(slot))) {++paimp_stale;return 0;}
    const pfire_slot *f=&pfire_slots[slot-1];
    paimp_active=1;
    for(unsigned i=0;i<f->count && i<PFIRE_GUNS;i++) {
        const pfire_gun *pg=&f->guns[i];
        if(pg->foreign || !pg->object) continue;
        paimp_gun *g=&paimp_guns[paimp_gun_count];memset(g,0,sizeof(*g));g->gun=pg->object;
        ++paimp_guns_seen;
        if(!paimp_collect(g)) {++paimp_shape;continue;}
        paimp_note_dofs(g);
        float aim[3],local[3];int locked=0;
        ++paimp_gun_count;   /* in the table from here: its values answer the solve's trial reads */
        if(!paimp_aim_now(slot,g->gun,space,aim,&locked)) {g->count=0;++paimp_rest;continue;}   /* at rest: as simulated */
        g->locked=locked;
        if(!paimp_to_gun(g->gun,space,aim,local) || !paimp_ik(g,local)) {g->count=0;++paimp_unsolved;continue;}
        g->shown=1;++paimp_shown;
        if(locked) ++paimp_locks;
        else ++paimp_points;
        if(paimp_gun_count==1) {
            paimp_last.tick=stats.issued;paimp_last.count=g->count;paimp_last.locked=locked;
            for(unsigned k=0;k<g->count;k++) {paimp_last.shown[k]=g->j[k].value;paimp_last.sim[k]=g->j[k].sim;}
        }
    }
    if(!paimp_gun_count) {paimp_active=0;return 0;}
    return 1;
}
static void paimp_leave(int entered) {
    if(entered) {paimp_active=0;paimp_gun_count=0;}
}
static void paimp_uninstall(void) {
    if(paimp_installed && pnat_swap8(pnat_base+PAIMP_VALUE_VTABLE+PAIMP_VALUE_SLOT,(void*)paimp_value_get,(void*)(image+PAIMP_VALUE_THUNK),1))
        paimp_installed=0;
}
static int paimp_arm(int on) {
    if(!on) {paimp_uninstall();paimp_armed=0;paimp_active=0;paimp_why="off";return 1;}
    if(paimp_armed) return 1;
    if(GetCurrentThreadId()!=ui_thread) {paimp_why="thread";return 0;}
    if(!paim_armed) {paimp_why="pilot_aim";return 0;}
    if(!paimp_pins()) {paimp_why="pins";return 0;}
    paimp_value_original=(paimp_value_fn)(void*)(image+PAIMP_VALUE_THUNK);
    paimp_fn.muzzle=(float*(*)(void*,float*))(void*)(image+PAIMP_MUZZLE);
    paimp_fn.lead=(float*(*)(void*,float*,void*,void*))(void*)(image+PAIMP_LEAD);
    if(!pnat_swap8(pnat_base+PAIMP_VALUE_VTABLE+PAIMP_VALUE_SLOT,(void*)(image+PAIMP_VALUE_THUNK),(void*)paimp_value_get,1)) {
        paimp_why="value_slot";return 0;
    }
    paimp_installed=1;paimp_sample.valid=0;paimp_armed=1;paimp_why="armed";return 1;
}
static void paimp_restore(void) {
    paimp_uninstall();
    paimp_armed=0;paimp_active=0;paimp_gun_count=0;paimp_sample.valid=0;
}
static int paimp_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"armed=%d why=%s installed=%d own=%u frames=%llu samples=%llu guns=%llu shown=%llu locks=%llu points=%llu "
        "rest=%llu stale=%llu shape=%llu unsolved=%llu clamped=%llu values=%llu icons=%llu/%llu error=%.6f dofs=%s",paimp_armed,paimp_why,
        paimp_installed,paimp_own(),(unsigned long long)paimp_frames,(unsigned long long)paimp_samples,(unsigned long long)paimp_guns_seen,
        (unsigned long long)paimp_shown,(unsigned long long)paimp_locks,(unsigned long long)paimp_points,(unsigned long long)paimp_rest,
        (unsigned long long)paimp_stale,(unsigned long long)paimp_shape,(unsigned long long)paimp_unsolved,(unsigned long long)paimp_clamped,
        (unsigned long long)paimp_values,(unsigned long long)paimp_icon_locks,(unsigned long long)paimp_icons,(double)paimp_last_error,
        paimp_dofs_seen);
    /* last=<tick>:<lock>:<shown>/<simulated>,... (radians) */
    if(n>0 && (size_t)n<cap) n+=snprintf(out+n,cap-(size_t)n," last=%llu:%d",(unsigned long long)paimp_last.tick,paimp_last.locked);
    for(unsigned k=0;n>0 && (size_t)n<cap && k<paimp_last.count;k++)
        n+=snprintf(out+n,cap-(size_t)n,"%c%.4f/%.4f",k?',':':',(double)paimp_last.shown[k],(double)paimp_last.sim[k]);
    return n;
}
/* gate.pilot_aim_present(1|0): this client's own guns presented at its latest aim, or not. */
static int lua_pilot_aim_present(lua_State *L) {
    const int ok=accept_ui() && paimp_arm(p_lua_tonumber(L,1)==1.0);
    p_lua_pushnumber(L,ok);p_lua_pushlstring(L,paimp_why,strlen(paimp_why));return 2;
}
static int lua_pilot_aim_present_status(lua_State *L) {
    char out[768];int n=paimp_status(out,sizeof(out));
    if(n>(int)sizeof(out)-1) n=(int)sizeof(out)-1;
    p_lua_pushlstring(L,out,(size_t)(n>0?n:0));return 1;
}

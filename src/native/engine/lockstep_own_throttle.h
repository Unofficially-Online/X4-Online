/* The own ship's throttle, boost and travel drive at once (native_gate_own_throttle, a default with native_gate_own_predict =
 * 'present'). Display only, this client's own slot only, every slot (1..N); nothing here writes engine memory, calls engine
 * code or draws RNG, and the simulation keeps the replayed records.
 *
 * What lagged. Every control reaches the simulation pilot_delay ticks after its capture (lockstep_pilot_stock.h). The own
 * ship is drawn where the simulation will have it (lockstep_own_predict.h), but the speed bar asks the simulation: Lua's
 * GetPlayerSpeed (0xb0c0a0; ui/core/lua 'crosshair handling.lua' updateSpeedbar, the HUD of the presentation scope on a
 * client k >= 2) answers, from the bank and the ship as tick T left them,
 *   actual     the forward speed as a fraction (NavigationController::vfunc12 0xea6730: of the max forward speed, 1..2 the
 *              boost range, 2..3 the travel range, the reverse speed below 0),
 *   targeted   PlayerController::vfunc13 0xd8c650: 2 while the travel drive charges or runs (the drive's +0x340, a pending
 *              ship event 0x1fa), 1 while boosting, 0 after a full stop, else the desired throttle 0x2f4b760, which
 *              vfunc33 sets to the setpoint 0x2f4b75c under flight assist,
 *   m/s        the forward speed (0x6dd1d0), boosting (0x54ce90 on the drive), travel (as targeted's),
 * so the arrow, the number and the boost/travel colours came a delay behind the key, the speed a delay behind the drawn
 * ship. The drawn motion itself: own_predict's command model learns the smoothed throttle 0x2f4b764 from the held keys (a
 * held accelerate key is a ramp it fits), but a wheel step, a stop, full forward or a throttle axis move changes the
 * setpoint, which is no channel of that model, so the drawn ship answered those a delay late as well.
 *
 * What this does, from the simulation's S(T) and the own records not yet applied (this client's capture ring, and the
 * record this frame's dispatch is still building, so a key shows in the frame it is pressed):
 *   setpoint   stepped through every pending record with X4's rules [F: the ramp 0xd88fe0, the handlers 0xd870b0 /
 *              0xd87f70 / 0xd86c80, the key step in 0xd95180 at 0xd954e7]: the stop action sets 0, full forward 1 while
 *              held and 0 at its release, a boost start 1, the throttle axis an absolute value, the wheel and the held
 *              accelerate / decelerate keys a step; a step that crosses zero stops at 0 and holds there 0.5 s while it
 *              keeps its direction (0x2f4b774 / 0x2f4b778); clamped to the bank's limits 0x2f4b768 / 76c. The step sizes
 *              are fitted from the authoritative ticks (recursive least squares): a key's step is k (r + (1-r) |s|) with
 *              r linear in the setpoint at its press (0x2f4b780), i.e. linear in [1, |m|, |s|, |m||s|] per direction; the
 *              wheel's linear in [1, |s|, s^2] per unit; the axis s = a x + b. A fit is used after four samples while its
 *              one-step error stays below a tenth of the step itself; an input no fit covers leaves the setpoint where it
 *              is (counted: partial), which is what the screen showed before.
 *   targeted   2 / 1 / the predicted setpoint at the end of T+d, as vfunc13 answers for the shown travel / boost / setpoint;
 *              the simulation's own answer where it is none of those (flight assist off with a strafe, match speed,
 *              autopilot, highway). vfunc33 sets desired from the setpoint between path_release's handlers and d95180's key
 *              step (rk9 live t1: in a key ramp desired was a step behind; learnt per session from the ramps, othr_order),
 *              so the simulation's own bar shows that setpoint a tick later still.
 *   boost, travel  each a level: the simulation's at T, then the intents of the window in order: a boost press and release
 *              (with the boost toggle option 0x38f52d7 a press toggles), the stop action ends a boost; the travel key
 *              0x12f toggles. The window holds the pending records and the applied ones of the last G ticks: an applied
 *              intent stays shown until the simulation shows it (confirmed) or G ticks pass without (refused: a jammed
 *              drive, no boost energy). G = the slowest confirmation seen plus 2, 3..12 ticks (the travel key goes through
 *              md/modes.xml). After a refusal the channel shows no switch-on of its own until the simulation's state
 *              changes or 150 ticks pass, so a refused key shows for d + G ticks once and not again.
 *   speed      actual and m/s move by own_predict's displayed forward speed less the simulated one, in the direction the
 *              predicted setpoint moves or, at a steady one, the simulated ship accelerates (0 while it draws the simulated
 *              pose: travel speeds, after a reset; below 0.5 m/s the finite differences' noise; at rest its drift), the fraction by
 *              the slope of its segment fitted from the simulation's own answers: the number is the speed of the ship on
 *              the screen.
 *   motion     own_predict's command path (opred_future, own slot) takes the predicted smoothed throttle: the predicted
 *              setpoint smoothed at the bank's rates 0x31be67c / 680 per second (vfunc33 at 0xd98251), when every
 *              throttle input of its horizon is covered and this path's one-step error is no worse than its own model's
 *              for that channel. A wheel step or a stop then moves the drawn ship at once like a held key.
 * Game time: the hold and the rates are seconds; a tick's game time is measured in the probe's frame (the thread's time,
 * TLS +0x300, as lockstep_hit_native.h reads it), the gate's tick length until two frames have been seen.
 * The HUD's call: GetPlayerSpeed's entry jumps to othr_speed_hook (a near page: the prologue's 6 bytes and back), which
 * runs it and replaces its first five answers on the Lua stack. The probe's own global would not do: the HUD's Anark
 * scripts never called it (rk9 live t1, calls=0). Installed at the arm on the UI thread, out at the session's end.
 * Not covered: the boost energy bar (GetBoostEnergyPercentage, FFI), the activity colours (an event, GetPlayerActivity),
 * the charge sound, tint and messages of md/modes.xml, engine sounds and effects, the camera shake: those follow the
 * simulation. Where the presentation answers another slot (client k outside its scope: the probe's own frame) the stock
 * values pass unchanged (counted: outside). */
#define OTHR_SETPOINT 0x2f4b75cu
#define OTHR_DESIRED 0x2f4b760u
#define OTHR_SMOOTHED 0x2f4b764u
#define OTHR_MIN 0x2f4b768u
#define OTHR_MAX 0x2f4b76cu
#define OTHR_STEP_LAST 0x2f4b774u
#define OTHR_HOLD_UNTIL 0x2f4b778u   /* a double: game time */
#define OTHR_RAMP_BASE 0x2f4b780u
#define OTHR_MATCH 0x2f4b786u
#define OTHR_LINEAR_Z 0x2f4b790u
#define OTHR_FULL_STOP 0x2f4b7d0u
#define OTHR_AUTOPILOT 0x2f4b7d8u
#define OTHR_FLIGHT 0x2f4b828u
#define OTHR_HIGHWAY 0x6b661d0u
#define OTHR_RATE_UP 0x31be67cu
#define OTHR_RATE_DOWN 0x31be680u
#define OTHR_BOOST_TOGGLE 0x38f52d7u
#define OTHR_ID_STOP 0x7bu
#define OTHR_ID_TRAVEL 0x12fu
#define OTHR_ID_ACCELERATE 0xdu
#define OTHR_ID_DECELERATE 0xeu
#define OTHR_ID_FULL_FORWARD 0xfu
#define OTHR_ID_FULL_REVERSE 0x10u
#define OTHR_ID_BOOST 0x11u
#define OTHR_ID_INVERT 0x5au
#define OTHR_ID_MATCH 0x5bu
#define OTHR_ID_AXIS 4u
#define OTHR_ID_WHEEL 0xau
#define OTHR_ZERO 1e-4
#define OTHR_ZERO_HOLD 0.5           /* s [F: 0x2cbe7e8, the ramp's hold at zero] */
#define OTHR_DIM 4u
#define OTHR_READY 4u
#define OTHR_LAMBDA 0.995
#define OTHR_P0 1e6                 /* the step sizes are exact functions of the features: next to no prior */
#define OTHR_P_TRACE 1e9
#define OTHR_FAST 0.3                /* the fits' one-step error and step size: a short memory */
#define OTHR_RING 64u
#define OTHR_WINDOW 48u              /* intents looked at: the grace plus the delay */
#define OTHR_GRACE_MIN 3u
#define OTHR_GRACE_MAX 12u
#define OTHR_REFUSE_HOLD 150u
#define OTHR_PATH_MAX 30u            /* = OPRED_HORIZON_MAX */
#define OTHR_DV_MIN 0.5              /* m/s */
#define OTHR_ACCEL_MIN 0.2           /* m/s per tick: the simulated ship speeds up or slows down */
#define OTHR_DV_SPEED_MAX 800.0      /* m/s: the lead's speed only below travel speeds (own_predict draws none above) */
#define OTHR_RESP_REACHED 0.005      /* of the max speed: the response has reached the setpoint's speed */
#define OTHR_RESP_GRACE 15u          /* ticks after the last pending input before a response that does not speed up ends */
#define OTHR_RESP_MAX 900u           /* ticks (30 s) a response lasts at most after its last pending input */
enum {OTHR_FIT_UP,OTHR_FIT_DOWN,OTHR_FIT_WHEEL,OTHR_FIT_AXIS,OTHR_FITS};
static const char *const othr_fit_names[OTHR_FITS]={"up","down","wheel","axis"};
static const unsigned othr_fit_dims[OTHR_FITS]={4,4,3,2};
enum {OTHR_BOOST,OTHR_TRAVEL,OTHR_CHANNELS};
static const char *const othr_channel_names[OTHR_CHANNELS]={"boost","travel"};
enum {OTHR_ABSTAIN_READ,OTHR_ABSTAIN_FLIGHT,OTHR_ABSTAIN_AUTOPILOT,OTHR_ABSTAIN_MATCH,OTHR_ABSTAIN_HIGHWAY,OTHR_ABSTAIN_ASSIST,OTHR_ABSTAINS};
static const char *const othr_abstain_names[OTHR_ABSTAINS]={"read","flight","autopilot","match","highway","assist"};
_Static_assert(OTHR_PATH_MAX==OPRED_HORIZON_MAX,"the motion path covers own_predict's horizon");
typedef struct {double w[OTHR_DIM],P[OTHR_DIM*OTHR_DIM],err,base;uint64_t samples,used;} othr_fit;
/* The bank fields this reads (slot 1 the canonical bank, slots 2..N their stores: opred_bank_read). */
typedef struct {
    float s,desired,smoothed,lo,hi,step,m,linear_z,up,down;double hold_until;uint64_t autopilot;
    unsigned char flight,match,stop[2],highway;
} othr_bank;
/* One run of the setpoint through the pending records. */
typedef struct {
    double s,lo,hi,m,step,hold,smoothed,up,down,desired;
    unsigned char acc,dec,full_forward,boost_on,toggle;
    unsigned partial;
} othr_run;
/* What the HUD's own call saw at a tick (the simulation's answers). */
typedef struct {uint64_t tick;float actual,targeted;double mps;unsigned char boost,travel,valid;} othr_obs;
typedef struct {uint64_t tick;unsigned char state,valid;} othr_level;
static int othr_armed,othr_motion;
static unsigned othr_slot,othr_rows;
static const char *othr_why="-";
static othr_fit othr_fits[OTHR_FITS];
static opred_held othr_held;static uint64_t othr_held_tick;
static othr_bank othr_prev;static uint64_t othr_prev_tick;static int othr_prev_valid;
static othr_obs othr_obs_ring[OTHR_RING];
static othr_level othr_levels[OTHR_CHANNELS][OTHR_RING];   /* the simulation's boost / travel by tick (forward-filled) */
static uint64_t othr_refused_at[OTHR_CHANNELS];static unsigned char othr_refused_state[OTHR_CHANNELS];
static unsigned othr_grace=6,othr_lag_max;
static double othr_slope[4];static unsigned char othr_slope_known[4];   /* fraction per m/s: reverse, 0..1, 1..2, 2..3 */
/* The response to an own throttle change: opened at a tick whose pending input moves the predicted setpoint (target = the
 * setpoint it predicts, last = that tick), it lasts while the simulated ship goes to the target's speed; the speed number
 * leads only inside it (rel16 release check, player 2 at the end of a travel stop: the ship slowed down by itself with
 * the throttle steady at 1, own_predict drew it 25 m/s slower and on below the normal top speed, the number read 137 at
 * 162 m/s for 9 ticks; player 1 within 2%). */
static struct {uint64_t last;double target;int active;} othr_resp;
static uint64_t othr_resp_opened,othr_resp_led;
static double othr_smooth_err,othr_motion_err;static uint64_t othr_smooth_samples,othr_motion_samples;
/* vfunc33 (desired = the setpoint, then the smoothing) runs between the handlers of path_release and the key step of
 * d95180, or after both: seen in the key ramps (desired one key step behind the setpoint, or level with it). */
static uint64_t othr_order[2];
static uint64_t othr_seen_tick;   /* the HUD tick the frame last learnt the slope and the confirmations from */
static int othr_order_b(void) {return othr_order[1]>othr_order[0];}
/* The HUD's GetPlayerSpeed, natively: its entry jumps to othr_speed_hook, which runs it through a trampoline on the near
 * page and replaces its first five answers on the Lua stack (the Anark HUD scripts do not see a global of the probe's
 * Lua state: rk9 live run t1, calls=0 with the probe's own wrapper). */
#define OTHR_SPEED_RVA 0xb0c0a0u
#define OTHR_SPEED_PROLOGUE 6u             /* push rbx (REX); push rbp; push rsi; push r15: no rip-relative bytes */
#define OTHR_PAGE 0x1000u
#define OTHR_STUB 0x20u
static const unsigned char othr_speed_bytes[OTHR_SPEED_PROLOGUE]={0x40,0x53,0x55,0x56,0x41,0x57};
static unsigned char *othr_page,othr_patched[5];
static int othr_hooked,othr_hook_restored=1;
static int (*othr_speed_original)(lua_State *);
static int (*othr_lua_gettop)(lua_State *);
static int (*othr_lua_toboolean)(lua_State *,int);
static void (*othr_lua_replace)(lua_State *,int);
static void (*othr_lua_pushboolean)(lua_State *,int);
static uint64_t othr_hook_calls,othr_hook_short,othr_hook_thread;
static double othr_now_override;   /* tests */
static double othr_dt,othr_last_now;   /* the game time one admitted tick advances, from othr_now in the probe's frame */
static uint64_t othr_calls,othr_adjusted,othr_outside,othr_stale,othr_frames,othr_partial,othr_abstains[OTHR_ABSTAINS],
    othr_confirmed[OTHR_CHANNELS],othr_refused[OTHR_CHANNELS],othr_unseen[OTHR_CHANNELS],
    othr_motion_paths,othr_motion_refused,othr_rows_written;
/* The last call's answer, for the row. */
static struct {uint64_t tick;double s_sim,s_shown,t_sim,t_shown,actual_sim,actual_shown,mps_sim,mps_shown,dv;int b_sim,b_shown,tr_sim,tr_shown,valid;
    unsigned calls;double mps_lo,mps_hi,shown_lo,shown_hi;int resp;double resp_target;} othr_last;
/* The code this reads the meaning of (SHA-256 of the 9.00 bytes). */
static const struct {unsigned rva,size;const char *sha;} othr_pin_table[]={
    {0xb0c0a0u,0x38eu,"61b187dcbf063f8453093a013fb57e1a927f95f21e83a8531e8f37088bab2646"},   /* GetPlayerSpeed: its eight answers */
    {0xd8c650u,0x116u,"19362c7be9178685085334818e82a44c390bfeb02b1780a80770897a254fa30d"},   /* vfunc13: targeted */
    {0xd88fe0u,0x55bu,"d30e434ac0bf2d4c61a5a06581240bf18a7560759775b206c42d690d611d8355"},   /* the ramp, the hold at zero */
    {0xd98251u,0xc5u,"2bb54aeeb4c0e252007784343379134caa6e63c38195b146b9a15d879d6b0b77"},    /* vfunc33: the smoothing */
    {0xd872c0u,0x21cu,"068423e11242eb2eba525e54d29e6e55babf25021d44dc3a2caea6053a4787f4"},   /* accelerate / decelerate */
    {0xd874dcu,0x107u,"82ffe006c99a5af220ff2a47a9397284bab0fa98f18423eb60d6bc6c0cbf911c"},   /* full forward */
    {0xd877a7u,0xc0u,"1a5c6f39cfc86f635e006a0a5bb09240197c515049d7b378c1852f83e6b9dfa3"},    /* boost: hold or toggle, the setpoint 1 */
    {0xd88469u,0x75u,"c914d1350cd1e210ec0b503df24959e8ae45cf1c8ead4badbe77d8161121efa7"},    /* wheel */
    {0xd88314u,0x155u,"0e923c9ab81e5f2c2eb590e9fe6fecc3f6fa73ce4d2060b7133f7ae0c84f4cad"},   /* throttle axis */
    {0xd954e7u,0xf2u,"d9453b582a300ffd3fa992ae6c95555b895689d3df2284f7e0073501c2eebdb4"},    /* the key step */
    {0xea6730u,0x31au,"08866f0485770090f2ba957feb0f01c9066e60997a6ab832c54386dd338648bd"},   /* the actual speed fraction */
    {0x6dd1d0u,0x180u,"e80f1ea6d0bbe6540955185acb02e219e33180a9fde46efac88ade18e767aecc"}};  /* the forward speed */
#define OTHR_PINS (sizeof(othr_pin_table)/sizeof(*othr_pin_table))
static int othr_pins_checked;   /* 0 unchecked, 1 verified, -1 refused */
static int othr_pins(void) {
    if(!othr_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<OTHR_PINS;i++) ok=hash_ok(image+othr_pin_table[i].rva,othr_pin_table[i].size,othr_pin_table[i].sha);
        othr_pins_checked=ok?1:-1;
    }
    return othr_pins_checked==1;
}
static double othr_now(void) {
    if(othr_now_override) return othr_now_override;
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);double now=0;
    if(!slots || !slots[0] || !read_local((unsigned char*)slots[0]+0x300,&now,8) || !isfinite(now)) return 0;
    return now;
}
static double othr_clamp(double x,double lo,double hi) {return x<lo?lo:x>hi?hi:x;}
static double othr_tick_dt(void) {return othr_dt>0?othr_dt:PREDICT_DT;}
static int othr_sign(double x) {return x>0?1:x<0?-1:0;}

/* ---- the fits ---- */
static void othr_fit_reset(othr_fit *f,unsigned n) {
    memset(f,0,sizeof(*f));
    for(unsigned i=0;i<n;i++) f->P[i*OTHR_DIM+i]=OTHR_P0;
}
static double othr_fit_eval(const othr_fit *f,const double x[OTHR_DIM]) {return opred_dot(f->w,x,OTHR_DIM);}
static int othr_fit_ready(const othr_fit *f) {return f->samples>=OTHR_READY && f->err<=0.01*f->base;}
/* One sample: y against x (the unused dimensions zero), hold = what holding the setpoint would have missed by (the
 * baseline of readiness, squared below). */
static void othr_fit_learn(othr_fit *f,unsigned n,const double x[OTHR_DIM],double y,double hold) {
    if(!isfinite(y) || !isfinite(hold)) return;
    for(unsigned i=0;i<n;i++) if(!isfinite(x[i])) return;
    const double e=y-othr_fit_eval(f,x);
    f->err+=OTHR_FAST*(e*e-f->err);f->base+=OTHR_FAST*(hold*hold-f->base);
    double Px[OTHR_DIM],k[OTHR_DIM],trace=0,den;
    for(unsigned i=0;i<n;i++) trace+=f->P[i*OTHR_DIM+i];
    const double lambda=trace>OTHR_P_TRACE?1.0:OTHR_LAMBDA;
    den=lambda;
    for(unsigned i=0;i<n;i++) {double s=0;for(unsigned j=0;j<n;j++) s+=f->P[i*OTHR_DIM+j]*x[j];Px[i]=s;den+=x[i]*s;}
    if(!isfinite(den) || den<=1e-12) return;
    for(unsigned i=0;i<n;i++) k[i]=Px[i]/den;
    for(unsigned i=0;i<n;i++) for(unsigned j=i;j<n;j++) {
        double v=(f->P[i*OTHR_DIM+j]-k[i]*Px[j])/lambda;
        f->P[i*OTHR_DIM+j]=f->P[j*OTHR_DIM+i]=v;
    }
    for(unsigned i=0;i<n;i++) f->w[i]+=k[i]*e;
    ++f->samples;
}
static void othr_key_x(double m,double s,double x[OTHR_DIM]) {x[0]=1.0;x[1]=fabs(m);x[2]=fabs(s);x[3]=fabs(m)*fabs(s);}
static void othr_wheel_x(double s,double x[OTHR_DIM]) {x[0]=1.0;x[1]=fabs(s);x[2]=s*s;x[3]=0.0;}
static void othr_axis_x(double v,double x[OTHR_DIM]) {x[0]=v;x[1]=1.0;x[2]=x[3]=0.0;}

/* ---- the bank ---- */
static int othr_bank_get(unsigned slot,othr_bank *b) {
    memset(b,0,sizeof(*b));
    return opred_bank_read(slot,OTHR_SETPOINT,&b->s,4) && opred_bank_read(slot,OTHR_DESIRED,&b->desired,4) &&
        opred_bank_read(slot,OTHR_SMOOTHED,&b->smoothed,4) && opred_bank_read(slot,OTHR_MIN,&b->lo,4) && opred_bank_read(slot,OTHR_MAX,&b->hi,4) &&
        opred_bank_read(slot,OTHR_STEP_LAST,&b->step,4) && opred_bank_read(slot,OTHR_HOLD_UNTIL,&b->hold_until,8) &&
        opred_bank_read(slot,OTHR_RAMP_BASE,&b->m,4) && opred_bank_read(slot,OTHR_LINEAR_Z,&b->linear_z,4) &&
        opred_bank_read(slot,OTHR_RATE_UP,&b->up,4) && opred_bank_read(slot,OTHR_RATE_DOWN,&b->down,4) &&
        opred_bank_read(slot,OTHR_AUTOPILOT,&b->autopilot,8) && opred_bank_read(slot,OTHR_FLIGHT,&b->flight,1) &&
        opred_bank_read(slot,OTHR_MATCH,&b->match,1) && opred_bank_read(slot,OTHR_FULL_STOP,b->stop,2) &&
        opred_bank_read(slot,OTHR_HIGHWAY,&b->highway,1) &&
        isfinite(b->s) && isfinite(b->desired) && isfinite(b->smoothed) && isfinite(b->lo) && isfinite(b->hi) && b->lo<=b->hi &&
        isfinite(b->step) && isfinite(b->m) && isfinite(b->up) && isfinite(b->down);
}
/* Why the setpoint does not drive the targeted speed and the throttle now (OTHR_ABSTAINS: it does). */
static unsigned othr_abstain(const othr_bank *b) {
    if(!b->flight) return OTHR_ABSTAIN_FLIGHT;
    if(b->autopilot) return OTHR_ABSTAIN_AUTOPILOT;
    if(b->match) return OTHR_ABSTAIN_MATCH;
    if(b->highway) return OTHR_ABSTAIN_HIGHWAY;
    /* vfunc33 without flight assist: the strafe's z (0x2f4b790) is the desired throttle. Otherwise desired is the setpoint,
     * or one key step behind it (vfunc33 before d95180's key step). */
    if(fabs((double)b->linear_z)>=OTHR_ZERO && fabs((double)b->desired-(double)b->linear_z)<1e-6 && fabs((double)b->desired-(double)b->s)>=OTHR_ZERO)
        return OTHR_ABSTAIN_ASSIST;
    return OTHR_ABSTAINS;
}

/* ---- records ---- */
static int othr_state_index(unsigned id) {return opred_index(pstk_replay_states,OPRED_STATES,id);}
static int othr_held_state(const opred_held *h,unsigned id) {int i=othr_state_index(id);return i>=0 && h->state[i];}
/* The held controls after the records applied up to tick (a copy: the stored fold stays at its own tick). */
static void othr_held_at(unsigned slot,uint64_t tick,opred_held *out) {
    opred_held h=othr_held;uint64_t from=othr_held_tick;
    if(!from || tick<from || tick-from>PSTK_RING/2) {memset(&h,0,sizeof(h));from=tick?tick-1:0;}
    for(uint64_t t=from+1;t<=tick;t++) {pstk_record r;if(opred_record_applied(slot,t,&r)) opred_fold(&h,&r,NULL,NULL);}
    *out=h;
}
/* The record that applies at t (> now) for the own slot; for the newest tick, the one this frame's dispatch is building
 * when the capture has not run yet (slot 6 before the probe's frame). */
static int othr_record_pending(unsigned slot,uint64_t t,uint64_t now,pstk_record *r) {
    if(opred_record_pending(slot,1,t,r)) return 1;
    if(t==now+pilot_delay && stats.issued==now && pstk_capturing && pstk_capture_slot==slot && pstk_build.count) {
        *r=pstk_build;return 1;
    }
    return 0;
}
static int othr_record(unsigned slot,uint64_t t,uint64_t now,pstk_record *r) {
    return t<=now?opred_record_applied(slot,t,r):othr_record_pending(slot,t,now,r);
}

/* ---- the setpoint, X4's rules with the fitted step sizes ---- */
/* 0xd88fe0 for one step: the hold at zero (while the direction stays), the crossing, the clamp. */
static void othr_ramp(othr_run *r,double delta,int hold_zero) {
    const int dir=othr_sign(delta),last=othr_sign(r->step);
    if(dir!=last) r->hold=0.0;
    else if(r->hold>1e-9) {r->step=delta;return;}
    double s=r->s;
    if(hold_zero && fabs(s)>=OTHR_ZERO && othr_sign(s)!=othr_sign(s+delta)) {s=0.0;r->hold=OTHR_ZERO_HOLD;}
    else s+=delta;
    s=othr_clamp(s,r->lo,r->hi);
    if(fabs(s)<OTHR_ZERO) s=othr_clamp(0.0,r->lo,r->hi);
    r->s=s;r->step=delta;
}
/* The events of one record in order (the handlers in path_release). */
static void othr_events(othr_run *r,const pstk_record *rec) {
    double x[OTHR_DIM];
    for(unsigned i=0;rec && i<rec->count;i++) {
        const pstk_event *e=&rec->events[i];float f;memcpy(&f,&e->value,4);
        if(e->kind==PSTK_ACTION && e->id==OTHR_ID_STOP) {r->s=othr_clamp(0.0,r->lo,r->hi);r->boost_on=0;}
        else if(e->kind==PSTK_STATE) {
            const unsigned char on=e->value!=0;
            if(e->id==OTHR_ID_ACCELERATE || e->id==OTHR_ID_DECELERATE) {
                if(on) r->m=r->s;    /* 0xd872e5: the ramp base at the press */
                else r->hold=0.0;    /* 0xd8732d: the release ends a hold (the throttle axis at rest) */
                if(e->id==OTHR_ID_ACCELERATE) r->acc=on;else r->dec=on;
            } else if(e->id==OTHR_ID_FULL_FORWARD) {
                if(on && !r->full_forward) {r->s=othr_clamp(1.0,r->lo,r->hi);r->full_forward=1;}
                else if(!on && r->full_forward) {r->s=othr_clamp(0.0,r->lo,r->hi);r->full_forward=0;}
            } else if(e->id==OTHR_ID_BOOST) {
                const unsigned char want=r->toggle?(on?!r->boost_on:r->boost_on):on;
                if(want && !r->boost_on) r->s=othr_clamp(1.0,r->lo,r->hi);   /* 0xd878f7 */
                r->boost_on=want;
            } else if(e->id==OTHR_ID_FULL_REVERSE || e->id==OTHR_ID_INVERT || e->id==OTHR_ID_MATCH) ++r->partial;
        } else if(e->kind==PSTK_RANGE && e->id==OTHR_ID_WHEEL && isfinite(f) && fabs(f)>=OTHR_ZERO) {
            if(othr_fit_ready(&othr_fits[OTHR_FIT_WHEEL])) {
                othr_wheel_x(r->s,x);r->m=r->s;othr_ramp(r,(double)f*othr_fit_eval(&othr_fits[OTHR_FIT_WHEEL],x),1);
            } else ++r->partial;
        } else if(e->kind==PSTK_RANGE && e->id==OTHR_ID_AXIS && isfinite(f)) {
            if(othr_fit_ready(&othr_fits[OTHR_FIT_AXIS])) {othr_axis_x(f,x);r->s=othr_clamp(othr_fit_eval(&othr_fits[OTHR_FIT_AXIS],x),r->lo,r->hi);}
            else ++r->partial;
        }
    }
}
/* The tick's key step (d95180 at 0xd954e7). */
static void othr_key(othr_run *r) {
    double x[OTHR_DIM];
    const int key=(int)r->acc-(int)r->dec;
    if(key) {
        const othr_fit *fit=&othr_fits[key>0?OTHR_FIT_UP:OTHR_FIT_DOWN];
        if(othr_fit_ready(fit)) {othr_key_x(r->m,r->s,x);othr_ramp(r,(double)key*othr_fit_eval(fit,x),1);}
        else ++r->partial;
    }
}
/* vfunc33's smoothing towards the desired throttle at the rates per second (0xd98251). */
static double othr_smoothed(double smoothed,double desired,double up,double down,double dt) {
    const double d=desired-smoothed;
    if(fabs(d)<OTHR_ZERO) return desired;
    return smoothed+(d>0?(d<up*dt?d:up*dt):(d>-down*dt?d:-down*dt));
}
/* One tick: the hold's clock, the handlers, vfunc33 (before the key step or after it), the key step. */
static void othr_tick(othr_run *r,const pstk_record *rec,double dt,int order_b) {
    if(r->hold>0.0) r->hold=r->hold>dt?r->hold-dt:0.0;
    othr_events(r,rec);
    if(order_b) {r->desired=r->s;r->smoothed=othr_smoothed(r->smoothed,r->desired,r->up,r->down,dt);othr_key(r);}
    else {othr_key(r);r->desired=r->s;r->smoothed=othr_smoothed(r->smoothed,r->desired,r->up,r->down,dt);}
}
static int othr_toggle(void) {unsigned char on=0;return pnat_base && read_local(pnat_base+OTHR_BOOST_TOGGLE,&on,1) && on;}
static void othr_run_start(othr_run *r,const othr_bank *b,const opred_held *h,double now) {
    memset(r,0,sizeof(*r));
    r->s=b->s;r->lo=b->lo;r->hi=b->hi;r->m=b->m;r->step=b->step;r->smoothed=b->smoothed;r->up=b->up;r->down=b->down;r->desired=b->desired;
    r->hold=now>0 && b->hold_until>now?b->hold_until-now:0.0;
    r->acc=(unsigned char)othr_held_state(h,OTHR_ID_ACCELERATE);r->dec=(unsigned char)othr_held_state(h,OTHR_ID_DECELERATE);
    r->full_forward=(unsigned char)othr_held_state(h,OTHR_ID_FULL_FORWARD);r->boost_on=(unsigned char)othr_held_state(h,OTHR_ID_BOOST);
    r->toggle=(unsigned char)othr_toggle();
}
/* The setpoint at the end of T+n through the pending records (the targeted speed it makes), and the smoothed throttle at T+1..T+n
 * (path, n <= OTHR_PATH_MAX). OTHR_ABSTAINS when the setpoint drives the targeted speed now, else the abstain cause (the
 * path is then not made). */
static unsigned othr_predict(unsigned slot,uint64_t T,unsigned n,othr_bank *b,double *s_out,double *path,unsigned *partial) {
    *partial=0;
    if(!othr_bank_get(slot,b)) return OTHR_ABSTAIN_READ;
    *s_out=b->s;
    const unsigned why=othr_abstain(b);
    if(why!=OTHR_ABSTAINS) return why;
    opred_held h;othr_held_at(slot,T,&h);
    othr_run r;othr_run_start(&r,b,&h,othr_now());
    const double dt=othr_tick_dt();const int order_b=othr_order_b();
    for(unsigned k=0;k<n && k<OTHR_PATH_MAX;k++) {
        pstk_record rec;const int have=othr_record_pending(slot,T+1+k,T,&rec);
        othr_tick(&r,have?&rec:NULL,dt,order_b);
        if(path) path[k]=r.smoothed;
    }
    *s_out=r.s;*partial=r.partial;   /* the setpoint at the end of T+n: vfunc13's answer then, or (vfunc33 before the key step) a tick later */
    return OTHR_ABSTAINS;
}

/* ---- learning, once per tick (the probe's frame, after own_predict's) ---- */
/* Does the record touch the setpoint other than by a held key? */
static int othr_record_events(const pstk_record *r,int *stop,int *wheel,float *wheel_value,int *axis,float *axis_value,int *edges) {
    *stop=*wheel=*axis=*edges=0;*wheel_value=*axis_value=0.0f;
    for(unsigned i=0;r && i<r->count;i++) {
        const pstk_event *e=&r->events[i];float f;memcpy(&f,&e->value,4);
        if(e->kind==PSTK_ACTION && e->id==OTHR_ID_STOP) ++*stop;
        else if(e->kind==PSTK_RANGE && e->id==OTHR_ID_WHEEL && isfinite(f) && fabs(f)>=OTHR_ZERO) {++*wheel;*wheel_value=f;}
        else if(e->kind==PSTK_RANGE && e->id==OTHR_ID_AXIS && isfinite(f)) {++*axis;*axis_value=f;}
        else if(e->kind==PSTK_STATE && (e->id==OTHR_ID_ACCELERATE || e->id==OTHR_ID_DECELERATE || e->id==OTHR_ID_FULL_FORWARD ||
                e->id==OTHR_ID_FULL_REVERSE || e->id==OTHR_ID_BOOST || e->id==OTHR_ID_INVERT || e->id==OTHR_ID_MATCH)) ++*edges;
    }
    return *stop || *wheel || *axis || *edges;
}
static int othr_inside(double s,const othr_bank *b) {return s>(double)b->lo+OTHR_ZERO && s<(double)b->hi-OTHR_ZERO;}
static void othr_learn(unsigned slot,uint64_t T,const othr_bank *b,const opred_held *before,const opred_held *after) {
    if(!othr_prev_valid || othr_prev_tick+1!=T) return;
    const othr_bank *p=&othr_prev;
    if(othr_abstain(p)!=OTHR_ABSTAINS || othr_abstain(b)!=OTHR_ABSTAINS) return;
    pstk_record r;const int have=opred_record_applied(slot,T,&r);
    int stop,wheel,axis,edges;float wv,av;othr_record_events(have?&r:NULL,&stop,&wheel,&wv,&axis,&av,&edges);
    const double s0=p->s,s1=b->s,delta=s1-s0,dt=othr_tick_dt();double x[OTHR_DIM];
    const int key0=othr_held_state(before,OTHR_ID_ACCELERATE)-othr_held_state(before,OTHR_ID_DECELERATE),
        key1=othr_held_state(after,OTHR_ID_ACCELERATE)-othr_held_state(after,OTHR_ID_DECELERATE),
        full=othr_held_state(before,OTHR_ID_FULL_FORWARD) || othr_held_state(after,OTHR_ID_FULL_FORWARD);
    const int clean=fabs(s0)>=OTHR_ZERO && fabs(s1)>=OTHR_ZERO && othr_sign(s0)==othr_sign(s1) && othr_inside(s0,p) && othr_inside(s1,b) &&
        !(b->hold_until>0 && p->hold_until==b->hold_until && delta==0.0);
    /* A held key, no edge and no other input this tick. */
    if(key0 && key0==key1 && !full && !stop && !wheel && !axis && !edges && clean) {
        othr_key_x(p->m,s0,x);othr_fit_learn(&othr_fits[key0>0?OTHR_FIT_UP:OTHR_FIT_DOWN],4,x,delta/(double)key0,delta);
    }
    /* One wheel step, no key. */
    if(wheel==1 && !key0 && !key1 && !full && !stop && !axis && !edges && clean) {
        othr_wheel_x(s0,x);othr_fit_learn(&othr_fits[OTHR_FIT_WHEEL],3,x,delta/(double)wv,delta);
    }
    /* The axis, alone. */
    if(axis==1 && !key0 && !key1 && !full && !stop && !wheel && !edges && othr_inside(s1,b)) {
        othr_axis_x(av,x);othr_fit_learn(&othr_fits[OTHR_FIT_AXIS],2,x,s1,delta);
    }
    /* vfunc33's place: in a key ramp desired is level with the setpoint (after the key step) or a step behind (before). */
    if(key0 && key0==key1 && !stop && !wheel && !axis && !edges && fabs(delta)>1e-6) {
        if(fabs((double)b->desired-s1)<1e-7) ++othr_order[0];
        else if(fabs((double)b->desired-s0)<1e-7) ++othr_order[1];
    }
    /* The smoothing rule against the bank, and the end-to-end step of the motion path (the HUD's run from T-1) against
     * own_predict's model error for that channel. */
    {   const double e=(double)b->smoothed-othr_smoothed(p->smoothed,b->desired,b->up,b->down,dt);
        othr_smooth_err+=OPRED_SLOW*(e*e-othr_smooth_err);++othr_smooth_samples;
    }
    {   const double now=othr_now();othr_run q;othr_run_start(&q,p,before,now>0?now-dt:0.0);
        othr_tick(&q,have?&r:NULL,dt,othr_order_b());
        const double e=(double)b->smoothed-q.smoothed;
        othr_motion_err+=OPRED_SLOW*(e*e-othr_motion_err);++othr_motion_samples;
    }
}
/* The fraction's slope per segment, from the HUD's answer of this tick and own_predict's simulated forward speed. */
static unsigned othr_segment(double f) {return f<0?0u:f<1?1u:f<2?2u:3u;}
static int othr_forward_sim(unsigned slot,uint64_t tick,double *v) {
    if(slot<1 || slot>PILOT_SLOTS) return 0;
    const opred_slot *p=&opred_slots[slot-1];const opred_entry *e=&p->ring[tick%OPRED_RING];
    if(!p->enabled || e->tick!=tick || !e->valid || !e->rates_valid || !isfinite(e->rates[2])) return 0;
    *v=e->rates[2];return 1;
}
static void othr_learn_slope(unsigned slot,uint64_t T) {
    const othr_obs *o=&othr_obs_ring[T%OTHR_RING],*q=&othr_obs_ring[(T-1)%OTHR_RING];double v,w;
    if(!o->valid || o->tick!=T || !othr_forward_sim(slot,T,&v)) return;
    const unsigned seg=othr_segment(o->actual);double slope=NAN;
    if(seg==1 && v>5.0 && o->actual>0.02 && o->actual<0.98) slope=o->actual/v;
    else if(seg==0 && v<-5.0) slope=o->actual/v;
    else if(q->valid && q->tick==T-1 && othr_segment(q->actual)==seg && othr_forward_sim(slot,T-1,&w) && fabs(v-w)>1.0) slope=(o->actual-q->actual)/(v-w);
    if(!isfinite(slope) || slope<=0) return;
    othr_slope[seg]=othr_slope_known[seg]?othr_slope[seg]+0.2*(slope-othr_slope[seg]):slope;othr_slope_known[seg]=1;
}
/* The simulation's boost / travel by tick: the HUD's answers, forward-filled over ticks it did not ask. */
static void othr_level_note(unsigned ch,uint64_t tick,unsigned char state) {
    othr_level *l=&othr_levels[ch][tick%OTHR_RING];l->tick=tick;l->state=state;l->valid=1;
}
static int othr_level_at(unsigned ch,uint64_t tick,unsigned char *state) {
    for(unsigned back=0;back<OTHR_RING/2 && back<=tick;back++) {
        const othr_level *l=&othr_levels[ch][(tick-back)%OTHR_RING];
        if(l->valid && l->tick==tick-back) {*state=l->state;return 1;}
    }
    return 0;
}

/* ---- boost and travel: the simulation's level, then the window's intents ---- */
/* The intent a record carries for a channel, folded in order: returns 1 and the level after it. */
static int othr_intent(unsigned ch,const pstk_record *r,int toggle,unsigned char before,unsigned char *after) {
    int any=0;unsigned char level=before;
    for(unsigned i=0;r && i<r->count;i++) {
        const pstk_event *e=&r->events[i];
        if(ch==OTHR_BOOST) {
            if(e->kind==PSTK_STATE && e->id==OTHR_ID_BOOST) {
                const unsigned char on=e->value!=0;
                if(toggle) {if(on) {level=!level;any=1;}}
                else {level=on;any=1;}
            } else if(e->kind==PSTK_ACTION && e->id==OTHR_ID_STOP) {level=0;any=1;}
        } else if(e->kind==PSTK_ACTION && e->id==OTHR_ID_TRAVEL) {level=!level;any=1;}
    }
    *after=level;return any;
}
/* The shown level of a channel at T (the HUD's call); the simulation's when no intent stands. */
static unsigned char othr_channel(unsigned ch,unsigned slot,uint64_t T,unsigned char sim,int toggle,int *intent) {
    *intent=0;
    if(othr_refused_at[ch] && (sim!=othr_refused_state[ch] || T>othr_refused_at[ch]+OTHR_REFUSE_HOLD)) othr_refused_at[ch]=0;
    const uint64_t first=T>othr_grace?T-othr_grace+1:1;unsigned char shown=sim,prev=sim;int open=0,primed=0;
    for(uint64_t t=first;t<=T+pilot_delay;t++) {
        pstk_record r;unsigned char after;
        if(!othr_record(slot,t,T,&r) || !othr_intent(ch,&r,toggle,prev,&after)) continue;
        if(!primed) {   /* the level before the window's first intent: the simulation's then (or now, for a pending one) */
            unsigned char then=sim;
            if(t<=T && t>1) othr_level_at(ch,t-1,&then);
            prev=then;primed=1;othr_intent(ch,&r,toggle,prev,&after);
        }
        if(after==prev) continue;
        if(after && othr_refused_at[ch]) continue;   /* after a refusal no switch-on of its own */
        if(t<=T) {
            /* Applied: confirmed once the simulation showed it at a tick since, refused after the grace without. */
            int seen=0;unsigned asked=0;
            for(uint64_t u=t;u<=T;u++) {
                const othr_level *l=&othr_levels[ch][u%OTHR_RING];
                if(l->valid && l->tick==u) {++asked;if(l->state==after) {seen=1;break;}}
            }
            if(seen) {prev=after;open=0;shown=sim;continue;}
            if(T-t+1>=othr_grace) {
                if(othr_refused_at[ch]==T) continue;   /* the HUD asked twice this tick */
                if(asked*2>=othr_grace) {++othr_refused[ch];othr_refused_at[ch]=T;othr_refused_state[ch]=sim;}
                else ++othr_unseen[ch];
                continue;   /* the level stays what it was */
            }
        }
        prev=after;shown=after;open=1;
    }
    *intent=open;
    return open?shown:sim;
}
/* Confirmations, once per tick: how long the simulation took to show an applied intent (the grace follows it). */
static void othr_confirm(unsigned ch,unsigned slot,uint64_t T) {
    unsigned char now,before;
    if(!othr_level_at(ch,T,&now) || !othr_level_at(ch,T-1,&before) || now==before) return;
    for(unsigned back=0;back<OTHR_GRACE_MAX && back<T;back++) {
        pstk_record r;unsigned char after;
        if(!opred_record_applied(slot,T-back,&r) || !othr_intent(ch,&r,ch==OTHR_BOOST && othr_toggle(),before,&after) || after!=now) continue;
        ++othr_confirmed[ch];
        if(back>othr_lag_max) othr_lag_max=back;
        unsigned g=othr_lag_max+2;othr_grace=g<OTHR_GRACE_MIN?OTHR_GRACE_MIN:g>OTHR_GRACE_MAX?OTHR_GRACE_MAX:g;
        return;
    }
}

/* ---- the motion: own_predict's command path for the smoothed throttle (opred_throttle_path) ---- */
#define OTHR_MOTION_READY 30u
/* Why the path was not given (status motion_why=): not armed or another slot, too few samples, abstained, partial, non-finite. */
static uint64_t othr_motion_why[5];
static unsigned othr_motion_path(unsigned slot,uint64_t tick,unsigned h,double *path) {
    if(!othr_armed || !othr_motion || slot!=othr_slot || !h || h>OTHR_PATH_MAX) {++othr_motion_why[0];return 0;}
    if(othr_motion_samples<OTHR_MOTION_READY) {++othr_motion_why[1];return 0;}
    const opred_slot *p=&opred_slots[slot-1];
    if(opred_command_ready(&p->cmd,3) && othr_motion_err>p->cmd.err_model[3] && othr_motion_err>1e-9) {++othr_motion_refused;return 0;}
    othr_bank b;double s=0;unsigned partial=0;
    if(othr_predict(slot,tick,h,&b,&s,path,&partial)!=OTHR_ABSTAINS) {++othr_motion_why[2];return 0;}
    if(partial) {++othr_motion_why[3];return 0;}
    for(unsigned k=0;k<h;k++) if(!isfinite(path[k])) {++othr_motion_why[4];return 0;}
    ++othr_motion_paths;return h;
}

/* ---- the HUD's call (slot 6, the presentation of the own slot) ---- */
/* own_predict's displayed forward speed less the simulated one (the step it shows, or the one before when this tick's has
 * not been made yet: the HUD asked before the probe's frame). */
static int othr_display_dv(unsigned slot,uint64_t T,double *dv,double *accel) {
    if(slot<1 || slot>PILOT_SLOTS) return 0;
    const opred_slot *p=&opred_slots[slot-1];
    if(!p->enabled || !p->ready || !p->have_end || (p->display_tick!=T && p->display_tick+1!=T)) return 0;
    const opred_entry *e=&p->ring[p->display_tick%OPRED_RING],*before=&p->ring[(p->display_tick-1)%OPRED_RING];
    if(e->tick!=p->display_tick || !e->rates_valid || !isfinite(p->end_rates[2]) || !isfinite(e->rates[2])) return 0;
    *dv=p->end_rates[2]-e->rates[2];
    *accel=before->tick+1==e->tick && before->rates_valid && isfinite(before->rates[2])?e->rates[2]-before->rates[2]:0.0;
    return 1;
}
/* in: GetPlayerSpeed's actual, targeted, m/s, boosting, travel; out: what the HUD shows. 1 when the own slot answered. */
static int othr_speed(const double in[5],double out[5]) {
    memcpy(out,in,5*sizeof(double));++othr_calls;
    if(!othr_armed || !opred_armed || !stats.active || join_catching_up()) return 0;
    const unsigned k=othr_slot;
    if(pstk_scope_slot || (k==1?pres_open!=0:(!pres_open || pres_slot!=k))) {++othr_outside;return 0;}
    const uint64_t T=pstk_frame_tick;
    if(!T || T+1<stats.issued || T>stats.issued) {++othr_stale;return 0;}
    for(unsigned i=0;i<5;i++) if(!isfinite(in[i])) return 0;
    const unsigned char b_sim=in[3]!=0.0,tr_sim=in[4]!=0.0;
    othr_obs *o=&othr_obs_ring[T%OTHR_RING];
    o->tick=T;o->actual=(float)in[0];o->targeted=(float)in[1];o->mps=in[2];o->boost=b_sim;o->travel=tr_sim;o->valid=1;
    othr_level_note(OTHR_BOOST,T,b_sim);othr_level_note(OTHR_TRAVEL,T,tr_sim);
    othr_bank b;double s_pred=0.0;unsigned partial=0;
    const unsigned why=othr_predict(k,T,pilot_delay,&b,&s_pred,NULL,&partial);
    if(why<OTHR_ABSTAINS) ++othr_abstains[why];
    if(partial) ++othr_partial;
    int bi=0,ti=0;
    const unsigned char b_shown=othr_channel(OTHR_BOOST,k,T,b_sim,othr_toggle(),&bi),tr_shown=othr_channel(OTHR_TRAVEL,k,T,tr_sim,0,&ti);
    double targeted=in[1];
    if(tr_shown) targeted=2.0;
    else if(b_shown) targeted=1.0;
    else if(why==OTHR_ABSTAINS) targeted=s_pred;
    else if((tr_sim || b_sim) && why!=OTHR_ABSTAIN_READ) targeted=b.desired;   /* shown off: vfunc13's answer without it */
    out[1]=othr_clamp(targeted,-1.0,3.0);out[3]=b_shown;out[4]=tr_shown;
    /* The speed of the drawn ship only where it makes an own throttle change immediate: inside the response (below), from
     * half a metre per second (the finite differences' noise); elsewhere the number is the simulation's, as player 1's
     * (own_predict's response model drifts at rest, rk9 live t3: -1.6 m/s for a ship standing still; it draws a ship that
     * slows down by itself after a travel stop on below the top speed, rel16). Never with the travel drive charging or
     * running (simulated or shown) and never from travel speeds (rk15, the user in travel: the speed "pulsing"; this module
     * had changed no answer in that cruise, adjusted=338 from 7800 to 8400). The m/s answer moves by whole numbers. */
    double dv=0.0,accel=0.0;
    const double trend=why==OTHR_ABSTAINS?s_pred-(double)b.s:0.0;
    const int have_dv=!tr_sim && !tr_shown && fabs(in[2])<OTHR_DV_SPEED_MAX && othr_display_dv(k,T,&dv,&accel);
    /* The response: only an own pending input opens it; boost, travel, a setpoint this module does not predict (the
     * autopilot, matching speed, ...), a setpoint that moved otherwise (a refused input), the target's speed reached, no
     * speeding up towards it OTHR_RESP_GRACE ticks after the last input, or OTHR_RESP_MAX ticks end it. */
    const double gap=othr_resp.target-in[0];
    const int toward=fabs(gap)>=OTHR_RESP_REACHED?(gap>0?1:-1):0;
    if(why!=OTHR_ABSTAINS || tr_sim || tr_shown || b_sim || b_shown) othr_resp.active=0;
    else if(fabs(trend)>=OTHR_ZERO) {
        if(!othr_resp.active) ++othr_resp_opened;
        othr_resp.active=1;othr_resp.last=T;othr_resp.target=s_pred;
    } else if(othr_resp.active && (fabs((double)b.s-othr_resp.target)>=OTHR_ZERO || !toward || T>othr_resp.last+OTHR_RESP_MAX ||
        (T>othr_resp.last+OTHR_RESP_GRACE && accel*toward<OTHR_ACCEL_MIN))) othr_resp.active=0;
    /* Inside it, the drawn ship's speed towards the target's and never past it, within the throttle's range (the actual
     * answer -1..1, where the learnt slope converts): the number reaches the simulation's as the ship reaches the target. */
    const double gap_now=othr_resp.target-in[0];
    const int direction=othr_resp.active && fabs(gap_now)>=OTHR_RESP_REACHED?(gap_now>0?1:-1):0;
    const unsigned seg=in[0]<0?0u:1u;
    if(have_dv && direction && in[0]>=-1.0 && in[0]<=1.0 && othr_slope_known[seg] && othr_slope[seg]>0 && fabs(dv)>=OTHR_DV_MIN &&
       (dv>0?1:-1)==direction) {
        const double room=gap_now/othr_slope[seg];   /* m/s to the target's speed */
        if(direction>0 && dv>room) dv=room;
        if(direction<0 && dv<room) dv=room;
        out[0]=othr_clamp(in[0]+dv*othr_slope[seg],-1.0,1.0);
        out[2]=in[2]+nearbyint(dv);
        ++othr_resp_led;
    }
    int changed=0;
    for(unsigned i=0;i<5;i++) if(out[i]!=in[i]) changed=1;
    if(changed) ++othr_adjusted;
    if(othr_last.tick!=T) {othr_last.calls=0;othr_last.mps_lo=othr_last.mps_hi=in[2];othr_last.shown_lo=othr_last.shown_hi=out[2];}
    ++othr_last.calls;
    if(in[2]<othr_last.mps_lo) othr_last.mps_lo=in[2];
    if(in[2]>othr_last.mps_hi) othr_last.mps_hi=in[2];
    if(out[2]<othr_last.shown_lo) othr_last.shown_lo=out[2];
    if(out[2]>othr_last.shown_hi) othr_last.shown_hi=out[2];
    othr_last.tick=T;othr_last.s_sim=b.s;othr_last.s_shown=s_pred;othr_last.t_sim=in[1];othr_last.t_shown=out[1];
    othr_last.actual_sim=in[0];othr_last.actual_shown=out[0];othr_last.mps_sim=in[2];othr_last.mps_shown=out[2];othr_last.dv=dv;
    othr_last.b_sim=b_sim;othr_last.b_shown=b_shown;othr_last.tr_sim=tr_sim;othr_last.tr_shown=tr_shown;othr_last.valid=changed||bi||ti;
    othr_last.resp=othr_resp.active;othr_last.resp_target=othr_resp.target;
    return 1;
}

/* ---- the probe's frame, once per admitted tick after own_predict's ---- */
/* The bank as tick T left it, the held key, and what the HUD's latest call showed against the simulation's answer (hud= its
 * tick: T when the HUD ran before the probe's frame, T-1 when after it, as live in slot 6), with that tick's calls and the
 * range of the simulated and shown m/s over them (calls=, range=). Every N ticks (native_gate_own_throttle_rows = N). */
static int othr_row(const othr_bank *b,int key,uint64_t T,char *out,size_t capacity) {
    int n=snprintf(out,capacity,"tick=%llu s=%.4g desired=%.4g smoothed=%.4g key=%d",(unsigned long long)T,b->s,b->desired,b->smoothed,key);
    if(n>0 && (size_t)n<capacity && othr_last.tick && othr_last.tick+1>=T)
        n+=snprintf(out+n,capacity-(size_t)n," hud=%llu shown=%.4g targeted=%.4g/%.4g boost=%d/%d travel=%d/%d actual=%.4g/%.4g mps=%.6g/%.6g dv=%.3g"
            " calls=%u range=%.6g..%.6g/%.6g..%.6g response=%d/%.4g",
            (unsigned long long)othr_last.tick,othr_last.s_shown,othr_last.t_sim,othr_last.t_shown,othr_last.b_sim,othr_last.b_shown,othr_last.tr_sim,
            othr_last.tr_shown,othr_last.actual_sim,othr_last.actual_shown,othr_last.mps_sim,othr_last.mps_shown,othr_last.dv,othr_last.calls,
            othr_last.mps_lo,othr_last.mps_hi,othr_last.shown_lo,othr_last.shown_hi,othr_last.resp,othr_last.resp_target);
    else if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," hud=-");
    return n>0 && (size_t)n<capacity?n:0;
}
/* Learn from tick T (the bank after it and the record it applied); returns a row's length when one is due (rows), 0 when
 * none, -1 refused. */
static int othr_frame(uint64_t T,char *row,size_t capacity,const char **why) {
    *why="-";
    if(!othr_armed) {*why="not_armed";return -1;}
    if(!T || (othr_frames && T<=othr_prev_tick)) {*why="tick";return -1;}
    const unsigned k=othr_slot;
    const double now=othr_now();
    if(now>0 && othr_last_now>0 && now>othr_last_now && othr_frames && T==othr_prev_tick+1) othr_dt=now-othr_last_now;
    othr_last_now=now;
    opred_held before,after;othr_held_at(k,T-1,&before);othr_held_at(k,T,&after);
    othr_bank b;const othr_bank last=othr_prev;const int had=othr_prev_valid && othr_prev_tick+1==T;
    const int readable=othr_bank_get(k,&b);
    if(readable && pilot_slot_present(k,T)) {
        othr_learn(k,T,&b,&before,&after);
        othr_prev=b;othr_prev_valid=1;
    } else othr_prev_valid=0;
    othr_prev_tick=T;othr_held=after;othr_held_tick=T;
    const uint64_t h=othr_last.tick;   /* the HUD's latest call: T before this frame, T-1 after it */
    if(h && h>othr_seen_tick && h+1>=T) {
        othr_learn_slope(k,h);
        for(unsigned ch=0;ch<OTHR_CHANNELS;ch++) othr_confirm(ch,k,h);
        othr_seen_tick=h;
    }
    ++othr_frames;
    /* A row while something moves: the HUD shown what the simulation does not yet, a key held, the bank's throttle changed. */
    const int key=othr_held_state(&after,OTHR_ID_ACCELERATE)-othr_held_state(&after,OTHR_ID_DECELERATE);
    const int moved=readable && (!had || b.s!=last.s || b.desired!=last.desired || b.smoothed!=last.smoothed);
    int n=0;
    (void)moved;
    if(othr_rows && T%othr_rows==0 && readable && (n=othr_row(&b,key,T,row,capacity))>0)
        ++othr_rows_written;
    return n;
}

/* ---- GetPlayerSpeed's entry (the HUD's own call, any Lua state) ---- */
/* LuaJIT calls it as a lua_CFunction on the UI thread; it pushes eight answers (or none without a ship). */
static int othr_speed_hook(lua_State *L) {
    const int n=othr_speed_original(L);
    ++othr_hook_calls;
    if(n!=8 || !othr_lua_gettop || !othr_lua_toboolean || !othr_lua_replace || !othr_lua_pushboolean) {++othr_hook_short;return n;}
    if(GetCurrentThreadId()!=ui_thread) {++othr_hook_thread;return n;}
    const int top=othr_lua_gettop(L);
    if(top<8) {++othr_hook_short;return n;}
    const int at=top-7;   /* actual, targeted, m/s, boosting, travel, then three of match speed */
    double in[5],out[5];
    in[0]=p_lua_tonumber(L,at);in[1]=p_lua_tonumber(L,at+1);in[2]=p_lua_tonumber(L,at+2);
    in[3]=othr_lua_toboolean(L,at+3)!=0;in[4]=othr_lua_toboolean(L,at+4)!=0;
    if(!othr_speed(in,out)) return n;
    for(int i=0;i<3;i++) if(out[i]!=in[i]) {p_lua_pushnumber(L,out[i]);othr_lua_replace(L,at+i);}
    for(int i=3;i<5;i++) if(out[i]!=in[i]) {othr_lua_pushboolean(L,out[i]!=0.0);othr_lua_replace(L,at+i);}
    return n;
}
static unsigned char *othr_alloc_page(void) {
    uintptr_t at=(uintptr_t)(pnat_base+OTHR_SPEED_RVA)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            unsigned char *p=VirtualAlloc((void*)(sign<0?at-distance:at+distance),OTHR_PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            const intptr_t a=(intptr_t)p-(intptr_t)(pnat_base+OTHR_SPEED_RVA);
            if(a>INT32_MIN+0x10000 && a<INT32_MAX-0x10000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
/* The page: the trampoline (the prologue, then back to the entry's seventh byte) and at OTHR_STUB the jump the entry takes. */
static void othr_write_page(unsigned char *p) {
    memset(p,0xcc,0x40);
    memcpy(p,othr_speed_bytes,OTHR_SPEED_PROLOGUE);pview_abs_jump(p+OTHR_SPEED_PROLOGUE,pnat_base+OTHR_SPEED_RVA+OTHR_SPEED_PROLOGUE);
    pview_abs_jump(p+OTHR_STUB,(void*)othr_speed_hook);
}
/* Installed at the arm (UI thread: nothing else runs GetPlayerSpeed), the session's end takes it out. */
static int othr_hook_install(const char **why) {
    *why="-";
    if(othr_hooked) return 1;
    if(!othr_lua_gettop) {
        HMODULE runtime=GetModuleHandleW(L"lua51_64.dll");
        if(runtime) {
            othr_lua_gettop=(void*)GetProcAddress(runtime,"lua_gettop");othr_lua_toboolean=(void*)GetProcAddress(runtime,"lua_toboolean");
            othr_lua_replace=(void*)GetProcAddress(runtime,"lua_replace");othr_lua_pushboolean=(void*)GetProcAddress(runtime,"lua_pushboolean");
        }
    }
    if(!othr_lua_gettop || !othr_lua_toboolean || !othr_lua_replace || !othr_lua_pushboolean) {*why="lua_api";return 0;}
    if(!othr_pins() || memcmp(pnat_base+OTHR_SPEED_RVA,othr_speed_bytes,OTHR_SPEED_PROLOGUE)) {*why="entry";return 0;}
    if(!othr_page && !(othr_page=othr_alloc_page())) {*why="page";return 0;}
    DWORD old,ignored;
    if(!VirtualProtect(othr_page,OTHR_PAGE,PAGE_READWRITE,&old)) {*why="page";return 0;}
    othr_write_page(othr_page);
    if(!VirtualProtect(othr_page,OTHR_PAGE,PAGE_EXECUTE_READ,&ignored)) {*why="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),othr_page,OTHR_PAGE);
    othr_speed_original=(int(*)(lua_State*))othr_page;
    const int32_t rel=(int32_t)((intptr_t)(othr_page+OTHR_STUB)-(intptr_t)(pnat_base+OTHR_SPEED_RVA+5));
    othr_patched[0]=0xe9;memcpy(othr_patched+1,&rel,4);
    if(!pview_cas(OTHR_SPEED_RVA,othr_speed_bytes,othr_patched,5)) {*why="patch";return 0;}
    othr_hooked=1;othr_hook_restored=0;return 1;
}
static void othr_hook_uninstall(void) {
    if(!othr_hooked) return;
    if(pview_cas(OTHR_SPEED_RVA,othr_patched,othr_speed_bytes,5)) {othr_hooked=0;othr_hook_restored=1;}
    else othr_hook_restored=!memcmp(pnat_base+OTHR_SPEED_RVA,othr_speed_bytes,5);   /* someone else's bytes: leave them */
}

/* ---- arming, status ---- */
static void othr_clear(void) {
    for(unsigned i=0;i<OTHR_FITS;i++) othr_fit_reset(&othr_fits[i],othr_fit_dims[i]);
    memset(&othr_held,0,sizeof(othr_held));othr_held_tick=0;memset(&othr_prev,0,sizeof(othr_prev));othr_prev_tick=0;othr_prev_valid=0;
    memset(othr_obs_ring,0,sizeof(othr_obs_ring));memset(othr_levels,0,sizeof(othr_levels));
    memset(othr_refused_at,0,sizeof(othr_refused_at));memset(othr_refused_state,0,sizeof(othr_refused_state));
    othr_grace=6;othr_lag_max=0;othr_dt=othr_last_now=0;memset(othr_slope,0,sizeof(othr_slope));memset(othr_slope_known,0,sizeof(othr_slope_known));
    othr_smooth_err=0;othr_motion_err=0;othr_seen_tick=0;othr_smooth_samples=othr_motion_samples=0;memset(othr_order,0,sizeof(othr_order));
    othr_hook_calls=othr_hook_short=othr_hook_thread=0;memset(othr_motion_why,0,sizeof(othr_motion_why));
    othr_calls=othr_adjusted=othr_outside=othr_stale=othr_frames=othr_partial=0;memset(othr_abstains,0,sizeof(othr_abstains));
    memset(othr_confirmed,0,sizeof(othr_confirmed));memset(othr_refused,0,sizeof(othr_refused));
    memset(othr_unseen,0,sizeof(othr_unseen));othr_motion_paths=othr_motion_refused=othr_rows_written=0;memset(&othr_last,0,sizeof(othr_last));
    memset(&othr_resp,0,sizeof(othr_resp));othr_resp_opened=othr_resp_led=0;
}
/* rows: a row every N ticks while the screen shows something the simulation does not yet (0 none); motion: own_predict's
 * command path takes the predicted throttle. After gate.own_predict('present', ...); an identical rerun succeeds. */
static int othr_arm(unsigned rows,int motion,const char **why) {
    *why="-";
    if(othr_armed) {
        if(rows==othr_rows && (motion!=0)==othr_motion && stats.active) return 1;
        *why="armed_differently";return 0;
    }
    if(!opred_armed || opred_mode!=OPRED_PRESENT) {*why="own_predict_present";return 0;}
    if(!stats.active || stats.slot<1 || stats.slot>pilot_slot_count || stats.slot>PILOT_SLOTS ||
       !opred_slots[stats.slot-1].own || !opred_slots[stats.slot-1].enabled) {*why="own_slot";return 0;}
    if(!othr_pins()) {*why="pins";return 0;}
    othr_clear();
    othr_slot=stats.slot;othr_rows=rows;othr_motion=motion!=0;
    opred_throttle_path=othr_motion?othr_motion_path:NULL;
    othr_armed=1;othr_why="armed";return 1;
}
static void othr_restore(void) {
    othr_hook_uninstall();
    if(opred_throttle_path==othr_motion_path) opred_throttle_path=NULL;
    othr_armed=0;othr_why="stopped";
}
static int othr_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%d why=%s slot=%u rows=%u motion=%d calls=%llu adjusted=%llu outside=%llu stale=%llu frames=%llu partial=%llu "
        "grace=%u lag=%u dt=%.4g response=%llu/%llu",othr_armed,othr_why,othr_slot,othr_rows,othr_motion,(unsigned long long)othr_calls,
        (unsigned long long)othr_adjusted,(unsigned long long)othr_outside,(unsigned long long)othr_stale,(unsigned long long)othr_frames,
        (unsigned long long)othr_partial,othr_grace,othr_lag_max,othr_tick_dt(),(unsigned long long)othr_resp_opened,(unsigned long long)othr_resp_led);
    for(unsigned i=0;n>0 && (size_t)n<capacity && i<OTHR_FITS;i++) {
        const othr_fit *f=&othr_fits[i];
        int more=snprintf(out+n,capacity-(size_t)n," %s=%llu/%d/%.3g/%.3g",othr_fit_names[i],(unsigned long long)f->samples,othr_fit_ready(f),
            f->err,f->base);
        if(more<0) return 0;
        n+=more;
    }
    for(unsigned c=0;n>0 && (size_t)n<capacity && c<OTHR_CHANNELS;c++) {
        int more=snprintf(out+n,capacity-(size_t)n," %s=%llu/%llu/%llu",othr_channel_names[c],(unsigned long long)othr_confirmed[c],
            (unsigned long long)othr_refused[c],(unsigned long long)othr_unseen[c]);
        if(more<0) return 0;
        n+=more;
    }
    if(n>0 && (size_t)n<capacity) {
        int more=snprintf(out+n,capacity-(size_t)n," abstain=");
        if(more<0) return 0;
        n+=more;
    }
    for(unsigned a=0;n>0 && (size_t)n<capacity && a<OTHR_ABSTAINS;a++) {
        int more=snprintf(out+n,capacity-(size_t)n,"%s%s:%llu",a?",":"",othr_abstain_names[a],(unsigned long long)othr_abstains[a]);
        if(more<0) return 0;
        n+=more;
    }
    if(n>0 && (size_t)n<capacity) {
        int more=snprintf(out+n,capacity-(size_t)n," smooth=%llu/%.3g order=%llu/%llu motion=%llu/%llu/%.3g/%llu motion_why=%llu,%llu,%llu,%llu,%llu"
            " slope=%.4g,%.4g,%.4g,%.4g hook=%d/%d/%llu/%llu/%llu",
            (unsigned long long)othr_smooth_samples,othr_smooth_err,(unsigned long long)othr_order[0],(unsigned long long)othr_order[1],
            (unsigned long long)othr_motion_paths,(unsigned long long)othr_motion_refused,othr_motion_err,(unsigned long long)othr_motion_samples,
            (unsigned long long)othr_motion_why[0],(unsigned long long)othr_motion_why[1],(unsigned long long)othr_motion_why[2],
            (unsigned long long)othr_motion_why[3],(unsigned long long)othr_motion_why[4],othr_slope[0],othr_slope[1],othr_slope[2],
            othr_slope[3],othr_hooked,othr_hook_restored,(unsigned long long)othr_hook_calls,(unsigned long long)othr_hook_short,
            (unsigned long long)othr_hook_thread);
        if(more<0) return 0;
        n+=more;
    }
    return n>0 && (size_t)n<capacity?n:0;
}

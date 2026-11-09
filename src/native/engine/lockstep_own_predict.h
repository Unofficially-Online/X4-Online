/* Own-ship and slot-ship display prediction for stock piloting, DEFAULT OFF, stop reason 184
 * (native_gate_own_predict; reports/LOCKSTEP-OWN-SHIP-PREDICTION-20261101.txt). Display only:
 * nothing here writes simulation state, calls engine code or draws RNG. Its one engine write is the
 * scoped pointer swap of a slot ship's published view-cache pose block inside VBI slot 10 (present
 * mode), put back before slot 10 returns. The census cannot see it by construction.
 *
 * Lockstep applies a record captured at tick c at c+d on every client (d = native_gate_pilot_delay).
 * The simulated ship S(t) is the latency-free ship N shifted by d: N(T) = S(T+d). This module shows
 * a slot ship at an estimate of S(T+h_eff) (h_eff <= h = min(d, the horizon cap)) in the frame of
 * tick T: the own ship answers the controls at once, other players' ships are drawn where their
 * owners see them; the world stays at T.
 *
 * Read on the MoveWorker in slot 6, after this tick's stock capture (guarded reads only):
 *   pose    the ship's published view-cache pose block [A] (object+0xe0+A*0x80: +0 the cached parent,
 *           +0x28 the local PosRot, row 0 the translation; A = *0x38f4ee0 on this thread): what slot
 *           10 draws (lockstep_pilot_predict.h section 3 of its report);
 *   u       the slot's processed control channels after this tick's update: input bank 0x2f4b750
 *           yaw/pitch/roll, 0x2f4b764 smoothed throttle, 0x2f4b788 x/y/z, flight control 0x2f4b828
 *           (reports/LOCKSTEP-VIRTUAL-PLAYERS-RE-20261029.txt section 2); slot 1 from the canonical
 *           bank, slots 2..N from their stores (lockstep_pilot_slots.h);
 *   inputs  the stock records (lockstep_pilot_stock.h): the one applied at T from the staging ring,
 *           the pending ones applying at T+1..T+h from this client's capture ring (own slot: all of
 *           them) or from the staging ring (other slots: those that have arrived).
 * Model, fitted online per slot from authoritative ticks only (never from a prediction):
 *   command   u(t) = W [u(t-1), phi(t), 1], phi = the record's cursor outputs, the held ranges and
 *             the held states folded from the event stream (the wheel is an impulse); multi-output
 *             recursive least squares (forgetting 0.999, trace bound): static maps, ramps and
 *             integrators (the throttle) are all of this form;
 *   response  per body axis (linear x y z, angular x y z in the previous pose's frame, as
 *             lockstep_pilot_predict.h): v(t) = a v(t-1) + b.u(t) + c; a residual beyond 4 sigma that
 *             leaves over half of the step's change unexplained while the controls held still (a hit,
 *             a ram, an explosion) is not fitted, unless refusals get frequent (then the model is the
 *             wrong one); a change at 0.8x the envelope of changes (rise and fall apart) or more is
 *             the acceleration limit: not fitted (a saturated step fitted as linear drags the whole fit),
 *             and a predicted change is clamped to the envelope.
 *   A channel or axis uses its model after 30 samples and only while its one-step error stays below
 *   0.9x the error of holding the last value (command) or the velocity (axis); otherwise it holds:
 *   the prediction is never knowingly worse than dead reckoning.
 * Prediction, every tick from the newest authoritative S(T) (a model error never accumulates beyond
 * one horizon): pending records -> u(T+1..T+h) -> v -> pose. A missing record (another player's, not
 * yet arrived) holds the previous one. Flight control off: constant velocity.
 * Correction policy (display D = P(T+h_eff) with a correction offset C):
 *   smooth  C takes the jump between the previous display path continued to this tick and the new
 *           prediction and decays with tau (native_gate_own_predict_tau, default 150 ms): a
 *           misprediction (another player's late input, a hit, a ram) is corrected over about tau;
 *           a new own input causes no jump (the continuation runs on the newest command); the part
 *           of a jump within 5 cm plus 10 % of the step's motion (2 mrad plus 10 % of its rotation) is
 *           shown at once, so the steady update of an accelerating ship does not turn into a lag;
 *   snap    C beyond 20 m plus one step of the ship's own motion, or 0.35 rad, is dropped (counted);
 *   reset   a discontinuity clears the history and the lead (D = S, the lead grows back over 2h
 *           steps): the ship or its parent changed (respawn, docking, undocking, a zone or highway
 *           change), a step 50 m off the ship's own velocity (teleport), an unreadable pose, a dead
 *           slot; counted by cause;
 *   lead    h_eff moves towards its target by half a step per step: 0 above 1200 m/s (travel drive,
 *           highways; back below 800 m/s) and while the predicted lead exceeds 300 m or 0.6 rad,
 *           else h. The cockpit camera hangs under the ship and follows D, which never jumps except
 *           at a snap or a reset.
 * Measured (logged, never hashed): for each prediction made at T, when T+h is simulated, the position
 * and angle error of the model, of constant velocity, of no prediction (S(T)) and of the displayed
 * pose (when h_eff was h); every tick the lead |D - S(T)|; snaps and resets by cause.
 * Present mode (render RNG split's slot-10 wrapper, called from predict_present_enter): in slot 10 on
 * the MoveWorker, each predicted ship's entry [A]+0x28 points to a private 16-byte aligned copy of
 * its pose block (w lanes and the following words kept) holding D; the engine's pointer goes back
 * before slot 10 returns, also on an exception (the wrapper's __finally). The entry must still hold
 * the sampled S(T) (or, in a render-only frame, the previous step's) under the sampled parent, else
 * that ship is drawn simulated (counted). A nested entry or a failed restore stops the gate (184).
 * Swap mode: the same swap with the block's own pose (nothing drawn differently): a census difference with
 * present but not with swap is the drawn pose reaching the simulation, one with swap the swap itself.
 * Render-frames join (lockstep_render_frames.h): opred_display_offset, when set, returns the display
 * time of the current slot 10 relative to this step, in steps (-1..1, 0 = the step); the displayed
 * pose is advanced by that much at its last rates and the correction decays by it. opred_owns(object)
 * is 1 while slot 10 draws that object at a predicted pose (an interpolator leaves it alone);
 * opred_display_pose(slot, offset, out) gives a slot's displayed pose at any offset.
 * The camera's view bank follows the own ship's displayed pose in every slot 10 call (lockstep_render_display.h,
 * rdisp_follow_enter): without it the characters in the own cockpit, the eye, culling and LOD stayed at the simulated
 * camera (client 2's own pilot drawn from inside its head; report section 13).
 * Not covered (report): HUD brackets and lead marks (simulated, slot 6), world-space effects; projectiles stay simulated. */
#define OPRED_STOP_REASON 184u
#define OPRED_HORIZON_MAX 30u
#define OPRED_HORIZON_CAP 10u
#define OPRED_TAU_MS 150u
#define OPRED_TAU_MAX_MS 2000u
#define OPRED_RING 64u
#define OPRED_CURSOR 3u
#define OPRED_RANGES 7u
#define OPRED_STATES 26u
#define OPRED_FEATURES (OPRED_CURSOR+OPRED_RANGES+OPRED_STATES)
#define OPRED_CHANNELS 7u
#define OPRED_CMD_DIM (OPRED_CHANNELS+OPRED_FEATURES+1u)
#define OPRED_AXES 6u
#define OPRED_AXIS_DIM (OPRED_CHANNELS+2u)
#define OPRED_READY 30u
#define OPRED_BETTER 0.9
#define OPRED_SLOW 0.002            /* the readiness comparison: long memory, maneuvers dominate it */
#define OPRED_FAST 0.05             /* the outlier scale */
#define OPRED_LAMBDA 0.999
#define OPRED_P0 1000.0
#define OPRED_P_TRACE 1e7           /* no forgetting above it: no wind-up without excitation */
#define OPRED_OUTLIER 16.0          /* squared residual over 16x the mean square: 4 sigma */
#define OPRED_UNEXPLAINED 0.25      /* ... and over half of the step's change unexplained */
#define OPRED_REFUSING 0.12         /* refusing more often than this (about three steps in a row): the model is wrong, not the samples */
#define OPRED_ENVELOPE_DECAY 0.9999
#define OPRED_ENVELOPE_MARGIN 1.0
#define OPRED_ENVELOPE_GROWTH 1.5
#define OPRED_SATURATED 0.8         /* a change this close to the envelope is an acceleration limit, not fitted */
#define OPRED_SNAP_POSITION 20.0    /* m, plus one step of the ship's motion */
#define OPRED_SNAP_ANGLE 0.35       /* rad */
#define OPRED_BAND_POSITION 0.05    /* m, plus 10 % of a step's motion: a smaller jump is shown at once, not smoothed */
#define OPRED_BAND_ANGLE 2e-3       /* rad, plus 10 % of a step's rotation */
#define OPRED_BAND_FRACTION 0.1
#define OPRED_TELEPORT 50.0         /* m off the previous step's velocity */
#define OPRED_SPEED_SUSPEND 1200.0  /* m/s */
#define OPRED_SPEED_RESUME 800.0
#define OPRED_LEAD_POSITION 300.0   /* m */
#define OPRED_LEAD_ANGLE 0.6        /* rad */
#define OPRED_RAMP 0.5              /* horizon change per step */
#define OPRED_SAME_POSITION 0.01    /* m: slot 10 finds the sampled block */
#define OPRED_SAME_ROTATION 1e-4
#define OPRED_BLOCK_BYTES 0x60u     /* PosRot and the two words after it */
#define OPRED_BLOCK_MIN 0x40u
#define OPRED_BANK_ROT 0x2f4b750u
#define OPRED_BANK_THROTTLE 0x2f4b764u
#define OPRED_BANK_LINEAR 0x2f4b788u
#define OPRED_BANK_FLIGHT 0x2f4b828u
#define OPRED_BUCKETS 8u
enum {OPRED_OFF,OPRED_OBSERVE,OPRED_PRESENT,OPRED_SWAP}; /* swap: present's pointer swap with the block's own pose (a census separation run) */
enum {OPRED_RESET_READ,OPRED_RESET_PARENT,OPRED_RESET_JUMP,OPRED_RESET_SHIP,OPRED_RESET_DEAD,OPRED_RESETS};
static const char *const opred_reset_names[OPRED_RESETS]={"read","parent","jump","ship","dead"};
_Static_assert(sizeof(pstk_replay_ranges)/sizeof(*pstk_replay_ranges)==OPRED_RANGES,"stock replay ranges");
_Static_assert(sizeof(pstk_replay_states)/sizeof(*pstk_replay_states)==OPRED_STATES,"stock replay states");
typedef struct {float range[OPRED_RANGES];unsigned char state[OPRED_STATES];float cursor[OPRED_CURSOR];} opred_held;
typedef struct {
    double w[OPRED_CHANNELS][OPRED_CMD_DIM],P[OPRED_CMD_DIM*OPRED_CMD_DIM];
    double err_model[OPRED_CHANNELS],err_hold[OPRED_CHANNELS],lo[OPRED_CHANNELS],hi[OPRED_CHANNELS];
    uint64_t samples;unsigned seen;
} opred_command;
typedef struct {
    double w[OPRED_AXIS_DIM],P[OPRED_AXIS_DIM*OPRED_AXIS_DIM];
    double err_model,err_hold,err_fast,envelope[2],refusing; /* envelope: largest rise [0] and fall [1] per step */
    uint64_t samples,refused,saturated;
} opred_axis;
typedef struct {
    uint64_t tick,made;
    unsigned valid,rates_valid,u_valid,flight,predicted,display_valid;
    predict_pose sim;double rates[OPRED_AXES],u[OPRED_CHANNELS];
    predict_pose made_pose[PREDICT_KINDS];
} opred_entry;
typedef struct {uint64_t n;double sum,sq,max;uint64_t bucket[OPRED_BUCKETS];} opred_stat;
typedef struct {
    unsigned enabled,own,suspended,have_end,ready,checked_now;
    unsigned char *object,*parent;
    opred_command cmd;opred_axis axis[OPRED_AXES];
    opred_held held;uint64_t held_tick;double phi[OPRED_FEATURES];
    opred_entry ring[OPRED_RING];
    predict_offset correction;predict_pose end,display,sim;double end_rates[OPRED_AXES],h_eff;
    float shown[12],sim_floats[12],prev_floats[12];unsigned prev_valid;uint64_t display_tick;
    opred_stat pos[PREDICT_KINDS],ang[PREDICT_KINDS],lead_pos,lead_ang;
    double last_pos,last_ang,lead_now_pos,lead_now_ang;
    uint64_t samples,predictions,checked,resets[OPRED_RESETS],snaps,bounded,suspends,missing,missing_now,actions,edges,nonfinite;
} opred_slot;
static const double opred_pos_edges[OPRED_BUCKETS-1]={0.01,0.03,0.1,0.3,1.0,3.0,10.0};
static opred_slot opred_slots[PILOT_SLOTS];
static int opred_mode,opred_armed,opred_others;
static unsigned opred_cap,opred_tau_ms,opred_horizon;
static uint64_t opred_frames,opred_last_tick;
static const char *opred_stage="off";
static volatile LONG64 opred_failures;
/* The render-frames join: the display time of this slot 10 relative to the step, in steps. */
static double (*opred_display_offset)(void);
/* Slot 10: the entries swapped in this call (MoveWorker only). */
static __attribute__((aligned(16))) float opred_blocks[PILOT_SLOTS][32];
static unsigned char *volatile *opred_open_at[PILOT_SLOTS];
static unsigned char *opred_open_saved[PILOT_SLOTS],*opred_open_object[PILOT_SLOTS];
static int opred_open;
static uint64_t opred_present_catchup;   /* present calls skipped while this client is a joiner catching up */
static uint64_t opred_present_calls,opred_present_applied,opred_present_restored,opred_present_lost,opred_present_refused,
    opred_present_stale,opred_present_previous,opred_present_thread;
/* The camera's view bank follows the presented own ship (lockstep_render_display.h, rdisp_follow_enter): bank written and
 * put back, eye put back (render-only frames), skipped because stage 2/3 wrote the bank, swapped ship not in the camera's
 * chain, parent differs, no table, view blocks cached before slot 10. */
static uint64_t opred_follow_written,opred_follow_restored,opred_follow_eye,opred_follow_rdisp,opred_follow_outside,
    opred_follow_parent,opred_follow_table,opred_follow_cached;
/* The camera bank's layout pins (lockstep_render_display.h): present and swap follow with the bank. */
static int rdisp_pins(void);

/* ---- features: the held controls of a slot, folded from its stock records ---- */
static int opred_index(const uint16_t *list,unsigned count,unsigned id) {
    for(unsigned i=0;i<count;i++) if(list[i]==id) return (int)i;
    return -1;
}
/* One record's events in order: ranges and states are held, the wheel (range 0xa) is an impulse of
 * its own tick, actions and SPACE edges are counted (not modelled). A record without the cursor
 * flag has zero cursor outputs (d95180 then reads zero). */
static void opred_fold(opred_held *h,const pstk_record *r,uint64_t *actions,uint64_t *edges) {
    int wheel=opred_index(pstk_replay_ranges,OPRED_RANGES,0xa);
    if(wheel>=0) h->range[wheel]=0.0f;
    for(unsigned i=0;i<r->count;i++) {
        const pstk_event *e=&r->events[i];int at;
        if(e->kind==PSTK_RANGE && (at=opred_index(pstk_replay_ranges,OPRED_RANGES,e->id))>=0) {
            float f;memcpy(&f,&e->value,4);if(isfinite(f)) h->range[at]=f;
        } else if(e->kind==PSTK_STATE && (at=opred_index(pstk_replay_states,OPRED_STATES,e->id))>=0) h->state[at]=e->value?1:0;
        else if(e->kind==PSTK_ACTION) {if(actions) ++*actions;}
        else if(e->kind==PSTK_SPACE) {if(edges) ++*edges;}
    }
    for(unsigned i=0;i<OPRED_CURSOR;i++) h->cursor[i]=(r->flags&PSTK_FLAG_CURSOR) && isfinite(r->cursor[i])?r->cursor[i]:0.0f;
}
static void opred_features(const opred_held *h,double phi[OPRED_FEATURES]) {
    for(unsigned i=0;i<OPRED_CURSOR;i++) phi[i]=h->cursor[i];
    for(unsigned i=0;i<OPRED_RANGES;i++) phi[OPRED_CURSOR+i]=h->range[i];
    for(unsigned i=0;i<OPRED_STATES;i++) phi[OPRED_CURSOR+OPRED_RANGES+i]=h->state[i];
}
/* The record a slot applies at tick t, from the staging ring (consumed entries keep their bytes). */
static int opred_record_applied(unsigned slot,uint64_t t,pstk_record *r) {
    if(slot<1 || slot>PILOT_SLOTS) return 0;
    const pstk_staged *e=&pstk_ring[slot-1][t%PSTK_RING];
    return e->tick==t && e->length && pstk_decode(e->bytes,e->length,r);
}
/* A record that applies at t > now: the own slot's from this client's capture ring, another slot's
 * once it has arrived (staged and not yet consumed). */
static int opred_record_pending(unsigned slot,int own,uint64_t t,pstk_record *r) {
    if(own) {
        if(t<pilot_delay) return 0;
        const pstk_local *l=&pstk_locals[(t-pilot_delay)%PSTK_RING];
        return l->used && l->capture==t-pilot_delay && l->apply==t && pstk_decode(l->bytes,l->length,r);
    }
    if(slot<1 || slot>PILOT_SLOTS) return 0;
    const pstk_staged *e=&pstk_ring[slot-1][t%PSTK_RING];
    return e->present && e->tick==t && pstk_decode(e->bytes,e->length,r);
}

/* ---- recursive least squares (P symmetric, row major) ---- */
static void opred_rls_init(double *P,unsigned n) {
    memset(P,0,(size_t)n*n*sizeof(double));
    for(unsigned i=0;i<n;i++) P[i*n+i]=OPRED_P0;
}
/* Gain k = P x / (lambda + x'Px), then P = (P - k x'P) / lambda; no forgetting while the trace is
 * above its bound. 0 for a degenerate or non-finite update (P unchanged). */
static int opred_rls_gain(double *P,unsigned n,const double *x,double *k) {
    double Px[OPRED_CMD_DIM],trace=0,den;
    for(unsigned i=0;i<n;i++) trace+=P[i*n+i];
    double lambda=trace>OPRED_P_TRACE?1.0:OPRED_LAMBDA;
    den=lambda;
    for(unsigned i=0;i<n;i++) {double s=0;for(unsigned j=0;j<n;j++) s+=P[i*n+j]*x[j];Px[i]=s;den+=x[i]*s;}
    if(!isfinite(den) || den<=1e-12) return 0;
    for(unsigned i=0;i<n;i++) k[i]=Px[i]/den;
    for(unsigned i=0;i<n;i++) for(unsigned j=i;j<n;j++) {
        double v=(P[i*n+j]-k[i]*Px[j])/lambda;
        P[i*n+j]=P[j*n+i]=v;
    }
    return 1;
}
static double opred_dot(const double *a,const double *b,unsigned n) {double s=0;for(unsigned i=0;i<n;i++) s+=a[i]*b[i];return s;}

/* ---- command model ---- */
static void opred_command_reset(opred_command *m) {
    memset(m,0,sizeof(*m));opred_rls_init(m->P,OPRED_CMD_DIM);
    for(unsigned c=0;c<OPRED_CHANNELS;c++) m->w[c][c]=1.0; /* the prior: hold */
}
static void opred_command_input(const double u_prev[OPRED_CHANNELS],const double phi[OPRED_FEATURES],double x[OPRED_CMD_DIM]) {
    memcpy(x,u_prev,OPRED_CHANNELS*sizeof(double));memcpy(x+OPRED_CHANNELS,phi,OPRED_FEATURES*sizeof(double));x[OPRED_CMD_DIM-1]=1.0;
}
static int opred_command_ready(const opred_command *m,unsigned c) {
    return m->samples>=OPRED_READY && m->err_model[c]<OPRED_BETTER*m->err_hold[c];
}
/* One authoritative tick: u_prev and phi -> u_now. */
static void opred_command_learn(opred_command *m,const double u_prev[OPRED_CHANNELS],const double phi[OPRED_FEATURES],const double u_now[OPRED_CHANNELS]) {
    double x[OPRED_CMD_DIM],k[OPRED_CMD_DIM],e[OPRED_CHANNELS];
    opred_command_input(u_prev,phi,x);
    for(unsigned c=0;c<OPRED_CHANNELS;c++) {
        if(!isfinite(u_now[c]) || !isfinite(u_prev[c])) return;
        e[c]=u_now[c]-opred_dot(m->w[c],x,OPRED_CMD_DIM);
        double hold=u_now[c]-u_prev[c];
        m->err_model[c]+=OPRED_SLOW*(e[c]*e[c]-m->err_model[c]);m->err_hold[c]+=OPRED_SLOW*(hold*hold-m->err_hold[c]);
        if(!m->seen || u_now[c]<m->lo[c]) m->lo[c]=u_now[c];
        if(!m->seen || u_now[c]>m->hi[c]) m->hi[c]=u_now[c];
    }
    m->seen=1;
    if(!opred_rls_gain(m->P,OPRED_CMD_DIM,x,k)) return;
    for(unsigned c=0;c<OPRED_CHANNELS;c++) for(unsigned i=0;i<OPRED_CMD_DIM;i++) m->w[c][i]+=k[i]*e[c];
    ++m->samples;
}
/* One predicted step: a ready channel from its model (clamped to the range seen, with a tenth of
 * it as margin), any other held. */
static void opred_command_step(const opred_command *m,const double u_prev[OPRED_CHANNELS],const double phi[OPRED_FEATURES],double u[OPRED_CHANNELS]) {
    double x[OPRED_CMD_DIM];opred_command_input(u_prev,phi,x);
    for(unsigned c=0;c<OPRED_CHANNELS;c++) {
        double v=u_prev[c];
        if(opred_command_ready(m,c)) {
            double margin=0.1*(m->hi[c]-m->lo[c]),y=opred_dot(m->w[c],x,OPRED_CMD_DIM);
            if(isfinite(y)) v=y<m->lo[c]-margin?m->lo[c]-margin:y>m->hi[c]+margin?m->hi[c]+margin:y;
        }
        u[c]=v;
    }
}

/* ---- response model ---- */
static double opred_noise(unsigned axis) {return axis<3?PREDICT_LINEAR_NOISE:PREDICT_ANGULAR_NOISE;}
static void opred_axis_reset(opred_axis *a) {memset(a,0,sizeof(*a));opred_rls_init(a->P,OPRED_AXIS_DIM);a->w[0]=1.0;} /* prior: constant velocity */
static void opred_axis_input(double v_prev,const double u[OPRED_CHANNELS],double x[OPRED_AXIS_DIM]) {
    x[0]=v_prev;memcpy(x+1,u,OPRED_CHANNELS*sizeof(double));x[OPRED_AXIS_DIM-1]=1.0;
}
static int opred_axis_ready(const opred_axis *a) {return a->samples>=OPRED_READY && a->err_model<OPRED_BETTER*a->err_hold;}
/* The model's next velocity as a prediction uses it: a in [0,1], the change within the envelope. */
static double opred_axis_predict(const opred_axis *a,double v,const double u[OPRED_CHANNELS]) {
    double w[OPRED_AXIS_DIM],x[OPRED_AXIS_DIM];memcpy(w,a->w,sizeof(w));
    if(w[0]<0) w[0]=0;
    if(w[0]>1) w[0]=1;
    opred_axis_input(v,u,x);
    double next=opred_dot(w,x,OPRED_AXIS_DIM),up=a->envelope[0]>0?a->envelope[0]:a->envelope[1],down=a->envelope[1]>0?a->envelope[1]:a->envelope[0];
    if(!isfinite(next)) return next;
    if(up>0 && next>v+OPRED_ENVELOPE_MARGIN*up) next=v+OPRED_ENVELOPE_MARGIN*up;
    if(down>0 && next<v-OPRED_ENVELOPE_MARGIN*down) next=v-OPRED_ENVELOPE_MARGIN*down;
    return next;
}
static void opred_axis_learn(opred_axis *a,unsigned axis,double v_prev,const double u_prev[OPRED_CHANNELS],const double u[OPRED_CHANNELS],double v_now) {
    double x[OPRED_AXIS_DIM],k[OPRED_AXIS_DIM];
    if(!isfinite(v_prev) || !isfinite(v_now)) {++a->refused;return;}
    opred_axis_input(v_prev,u,x);
    /* e: the linear fit's residual; p: the error of the prediction as made (clamped), which decides readiness. */
    double e=v_now-opred_dot(a->w,x,OPRED_AXIS_DIM),p=v_now-opred_axis_predict(a,v_prev,u),hold=v_now-v_prev,noise=opred_noise(axis);
    if(!isfinite(e) || !isfinite(p)) {++a->refused;return;}
    /* An impulse the controls did not cause (once the model is in use): the controls held still this step,
     * the prediction error is far beyond the recent ones and leaves most of the change unexplained. A model
     * that keeps refusing (about three steps in a row) is the wrong one: it fits again. */
    int changed=0;
    for(unsigned c=0;c<OPRED_CHANNELS;c++) if(fabs(u[c]-u_prev[c])>1e-6) changed=1;
    double scale=a->err_fast>a->err_model?a->err_fast:a->err_model;
    if(scale<noise*noise) scale=noise*noise;
    int outlier=opred_axis_ready(a) && !changed && a->refusing<OPRED_REFUSING && p*p>OPRED_OUTLIER*scale && p*p>OPRED_UNEXPLAINED*hold*hold;
    a->refusing+=OPRED_FAST*((outlier?1.0:0.0)-a->refusing);
    if(outlier) {++a->refused;return;}
    a->err_model+=OPRED_SLOW*(p*p-a->err_model);a->err_hold+=OPRED_SLOW*(hold*hold-a->err_hold);a->err_fast+=OPRED_FAST*(p*p-a->err_fast);
    /* A change at the envelope is the acceleration limit: the linear fit leaves it to the clamp. */
    double *side=&a->envelope[hold<0];
    int established=a->samples>=OPRED_READY && *side>noise,saturated=*side>noise && fabs(hold)>=OPRED_SATURATED**side;
    /* The envelope follows the changes seen; once established it grows by half at most per step (one
     * impulse cannot open it). */
    double grown=established?OPRED_ENVELOPE_GROWTH**side:fabs(hold);
    a->envelope[0]*=OPRED_ENVELOPE_DECAY;a->envelope[1]*=OPRED_ENVELOPE_DECAY;
    if(fabs(hold)>*side) *side=fabs(hold)<grown?fabs(hold):grown;
    if(saturated) {++a->saturated;return;}
    if(!opred_rls_gain(a->P,OPRED_AXIS_DIM,x,k)) return;
    for(unsigned i=0;i<OPRED_AXIS_DIM;i++) a->w[i]+=k[i]*e;
    ++a->samples;
}
/* One step of every axis: the model where ready (a in [0,1], the change within the envelope), else
 * constant velocity; model = 0 forces constant velocity (flight control off). */
static void opred_axes_step(const opred_axis *axes,int model,double v[OPRED_AXES],const double u[OPRED_CHANNELS],uint64_t *nonfinite) {
    for(unsigned i=0;i<OPRED_AXES;i++) {
        if(!model || !opred_axis_ready(&axes[i])) continue;
        double next=opred_axis_predict(&axes[i],v[i],u);
        if(!isfinite(next)) {if(nonfinite) ++*nonfinite;continue;}
        v[i]=next;
    }
}
/* steps (>= 0, fractional) from (pose, v): step k applies u[k] (the last one repeats); a partial
 * step moves a fraction of the velocity change and of dt. */
static void opred_run(const opred_slot *p,int model,const predict_pose *base,const double v0[OPRED_AXES],const double (*u)[OPRED_CHANNELS],
                      unsigned count,double steps,predict_pose *out,double v_out[OPRED_AXES],uint64_t *nonfinite) {
    double v[OPRED_AXES];*out=*base;memcpy(v,v0,sizeof(v));
    for(unsigned k=0;steps>1e-9 && k<OPRED_HORIZON_MAX+2;k++) {
        double f=steps>=1.0?1.0:steps,next[OPRED_AXES];memcpy(next,v,sizeof(next));
        if(count) opred_axes_step(p->axis,model,next,u[k<count?k:count-1],nonfinite);
        for(unsigned i=0;i<OPRED_AXES;i++) v[i]+=f*(next[i]-v[i]);
        predict_advance(out,v,f*PREDICT_DT);
        steps-=f;
    }
    if(v_out) memcpy(v_out,v,sizeof(v));
}
/* The commands of the next h steps (u[k] applies at tick+1+k) and how many records were missing. */
static unsigned opred_future(opred_slot *p,unsigned slot,uint64_t tick,unsigned h,const double u_now[OPRED_CHANNELS],double (*u)[OPRED_CHANNELS]) {
    opred_held held=p->held;double phi[OPRED_FEATURES],prev[OPRED_CHANNELS];unsigned missing=0;
    memcpy(prev,u_now,sizeof(prev));
    for(unsigned k=0;k<h;k++) {
        pstk_record r;
        if(opred_record_pending(slot,p->own,tick+1+k,&r)) opred_fold(&held,&r,NULL,NULL);
        else ++missing;   /* held: the same controls, the same cursor */
        opred_features(&held,phi);
        opred_command_step(&p->cmd,prev,phi,u[k]);
        memcpy(prev,u[k],sizeof(prev));
    }
    return missing;
}

/* ---- display smoothing ---- */
/* What of a jump exceeds its band (a smaller jump is shown at once: no lag from a small steady misprediction). */
static void opred_shrink(double v[3],double band) {
    double n=sqrt(predict_dot3(v,v)),k=n>band?(n-band)/n:0.0;
    for(unsigned i=0;i<3;i++) v[i]*=k;
}
/* C = lambda (C + the jump's excess over the bands); dropped beyond the snap bounds (1 = snapped). */
static int opred_smooth(predict_offset *c,const predict_pose *continuation,const predict_pose *next,double lambda,double snap_position,
                        double band_position,double band_angle) {
    double e[9],w[3],d[3];
    predict_mul_transposed(continuation->r,next->r,e);predict_log(e,w);
    for(unsigned i=0;i<3;i++) d[i]=continuation->p[i]-next->p[i];
    opred_shrink(d,band_position);opred_shrink(w,band_angle);
    for(unsigned i=0;i<3;i++) {c->p[i]=lambda*(c->p[i]+d[i]);c->r[i]=lambda*(c->r[i]+w[i]);}
    if(!(sqrt(predict_dot3(c->p,c->p))<=snap_position) || !(sqrt(predict_dot3(c->r,c->r))<=OPRED_SNAP_ANGLE)) {memset(c,0,sizeof(*c));return 1;}
    return 0;
}
static void opred_stat_add(opred_stat *s,double value,const double *edges) {
    if(!isfinite(value)) return;
    unsigned b=0;
    if(edges) while(b<OPRED_BUCKETS-1 && value>=edges[b]) ++b;
    ++s->n;s->sum+=value;s->sq+=value*value;if(value>s->max) s->max=value;++s->bucket[b];
}

/* ---- engine reads (MoveWorker, slot 6) ---- */
/* The store offset of an image address in a slot's store (lockstep_pilot_slots.h order), or -1. */
static int opred_store_offset(unsigned address,unsigned size) {
    unsigned n=0;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(r->base!=VPL_BASE_IMAGE || !(PSLOT_CLASSES>>r->cls&1u)) continue;
        if(address>=r->offset && address+size<=r->offset+r->size) return (int)(n+address-r->offset);
        n+=r->size;
    }
    return -1;
}
static int opred_bank_read(unsigned slot,unsigned address,void *out,unsigned size) {
    if(slot==1) return pnat_base && read_local(pnat_base+address,out,size);
    if(slot<2 || slot>PILOT_SLOTS || !pslots[slot].seated) return 0;
    int at=opred_store_offset(address,size);
    if(at<0 || (unsigned)at+size>VPL_BLOCK_MAX) return 0;
    memcpy(out,pslots[slot].store+at,size);return 1;
}
/* The slot's processed channels after this tick's update and its flight control. */
static int opred_controls(unsigned slot,double u[OPRED_CHANNELS],unsigned *flight) {
    float rot[3],throttle,linear[3];unsigned char on=0;
    if(!opred_bank_read(slot,OPRED_BANK_ROT,rot,sizeof(rot)) || !opred_bank_read(slot,OPRED_BANK_THROTTLE,&throttle,4) ||
       !opred_bank_read(slot,OPRED_BANK_LINEAR,linear,sizeof(linear)) || !opred_bank_read(slot,OPRED_BANK_FLIGHT,&on,1)) return 0;
    u[0]=rot[0];u[1]=rot[1];u[2]=rot[2];u[3]=throttle;u[4]=linear[0];u[5]=linear[1];u[6]=linear[2];
    for(unsigned c=0;c<OPRED_CHANNELS;c++) if(!isfinite(u[c])) return 0;
    *flight=on!=0;return 1;
}
/* The published pose block [A] of a component: its parent (cached = live), the block's address and
 * its first 16 floats. */
static int opred_view(unsigned char *object,unsigned char **parent,unsigned char *volatile **at,unsigned char **block,float pose[16]) {
    unsigned index=2;unsigned char *cached=NULL,*live=NULL,*b=NULL;
    if(!object || !read_local(image+PREDICT_VIEW_INDEX,&index,4) || index>1) return 0;
    unsigned char *vc=object+PREDICT_VIEW_CACHE+index*PREDICT_VIEW_STRIDE;
    if(!read_local(vc,&cached,8) || !read_local(object+0x70,&live,8) || !cached || cached!=live ||
       !read_local(vc+PREDICT_VIEW_BLOCK,&b,8) || !b || !read_local(b,pose,16*sizeof(float))) return 0;
    *parent=live;*block=b;if(at) *at=(unsigned char *volatile*)(vc+PREDICT_VIEW_BLOCK);
    return 1;
}
static void opred_floats(const predict_pose *pose,float out[12]) {predict_floats(pose,out);}
static int opred_same(const float block[16],const float xyz[12]) {
    double deviation[2];predict_view_deviation(block,xyz,deviation);
    return deviation[0]<=OPRED_SAME_POSITION && deviation[1]<=OPRED_SAME_ROTATION;
}

/* ---- the per-tick step of one slot ---- */
static void opred_reset(opred_slot *p,unsigned cause) {
    memset(p->ring,0,sizeof(p->ring));memset(&p->correction,0,sizeof(p->correction));
    p->have_end=0;p->h_eff=0;p->ready=0;p->prev_valid=0;p->suspended=0;
    if(cause<OPRED_RESETS) ++p->resets[cause];
}
/* Fold the records applied since the last tick into the held controls (a gap over the ring
 * restarts them). */
static void opred_fold_applied(opred_slot *p,unsigned slot,uint64_t tick) {
    if(!p->held_tick || tick<=p->held_tick || tick-p->held_tick>PSTK_RING/2) {memset(&p->held,0,sizeof(p->held));p->held_tick=tick-1;}
    for(uint64_t t=p->held_tick+1;t<=tick;t++) {
        pstk_record r;
        if(opred_record_applied(slot,t,&r)) opred_fold(&p->held,&r,&p->actions,&p->edges);
        else {int wheel=opred_index(pstk_replay_ranges,OPRED_RANGES,0xa);if(wheel>=0) p->held.range[wheel]=0.0f;}
    }
    p->held_tick=tick;opred_features(&p->held,p->phi);
}
static void opred_check(opred_slot *p,const opred_entry *now) {
    p->checked_now=0;
    if(!now->predicted || now->made+opred_horizon!=now->tick) return;
    for(unsigned k=0;k<PREDICT_KINDS;k++) {
        if(k==PREDICT_DISPLAY && !now->display_valid) continue;
        double dp=predict_distance(&now->made_pose[k],&now->sim),da=predict_angle(now->made_pose[k].r,now->sim.r);
        opred_stat_add(&p->pos[k],dp,opred_pos_edges);opred_stat_add(&p->ang[k],da,NULL);
        if(k==PREDICT_MODEL) {p->last_pos=dp;p->last_ang=da;}
    }
    ++p->checked;p->checked_now=1;
}
static void opred_slot_frame(unsigned index,uint64_t tick) {
    opred_slot *p=&opred_slots[index];const pilot_slot *s=&pilot_slots[index];const unsigned slot=index+1;
    p->ready=0;p->checked_now=0;p->missing_now=0;
    opred_fold_applied(p,slot,tick);
    opred_entry *before=&p->ring[(tick-1)%OPRED_RING];
    predict_pose pose;unsigned char *parent=NULL,*block=NULL;float raw[16];int cause=-1,sampled=0;
    if(s->dead || !s->resolved || !s->object) cause=OPRED_RESET_DEAD;
    else if(p->object && p->object!=s->object) cause=OPRED_RESET_SHIP;
    if(cause!=OPRED_RESET_DEAD) {
        sampled=opred_view(s->object,&parent,NULL,&block,raw) && predict_pose_from_bank(raw,&pose);
        if(!sampled) cause=OPRED_RESET_READ;
        else if(cause<0 && p->parent && parent!=p->parent) cause=OPRED_RESET_PARENT;
        else if(cause<0 && before->tick==tick-1 && before->rates_valid) {
            predict_pose expected=before->sim;predict_advance(&expected,before->rates,PREDICT_DT);
            if(predict_distance(&expected,&pose)>OPRED_TELEPORT) cause=OPRED_RESET_JUMP;
        }
    }
    if(cause>=0 && (cause!=OPRED_RESET_DEAD || p->object)) opred_reset(p,(unsigned)cause);
    p->object=cause==OPRED_RESET_DEAD?NULL:s->object;
    if(!sampled) {p->parent=NULL;return;}
    p->parent=parent;
    opred_entry *now=&p->ring[tick%OPRED_RING];
    int have_before=before->tick==tick-1 && before->valid;
    if(now->tick!=tick) {memset(now,0,sizeof(*now));now->tick=tick;} /* else: the prediction made for this tick stays */
    now->valid=1;now->sim=pose;now->rates_valid=0;++p->samples;
    now->u_valid=opred_controls(slot,now->u,&now->flight);
    if(have_before) {predict_rates(&before->sim,&pose,PREDICT_DT,now->rates);now->rates_valid=1;}
    /* Learn from this authoritative tick (flight control on at both ends). */
    int driven=have_before && before->u_valid && now->u_valid && before->flight && now->flight;
    if(driven) opred_command_learn(&p->cmd,before->u,p->phi,now->u);
    if(driven && before->rates_valid && now->rates_valid)
        for(unsigned i=0;i<OPRED_AXES;i++) opred_axis_learn(&p->axis[i],i,before->rates[i],before->u,now->u,now->rates[i]);
    opred_check(p,now);
    p->sim=pose;opred_floats(&pose,p->sim_floats);
    if(!now->rates_valid) { /* no velocity yet: shown as simulated */
        p->display=pose;memcpy(p->shown,p->sim_floats,sizeof(p->shown));memset(p->end_rates,0,sizeof(p->end_rates));
        p->end=pose;p->have_end=0;p->h_eff=0;memset(&p->correction,0,sizeof(p->correction)); /* the next tick starts its path afresh */
        p->display_tick=tick;p->ready=1;p->lead_now_pos=p->lead_now_ang=0;
        return;
    }
    /* Predict: the full horizon (measured) and the shown one (h_eff). */
    const unsigned h=opred_horizon;double u[OPRED_HORIZON_MAX][OPRED_CHANNELS],u_now[OPRED_CHANNELS];
    int model=now->u_valid && now->flight;
    if(now->u_valid) memcpy(u_now,now->u,sizeof(u_now));else memset(u_now,0,sizeof(u_now));
    p->missing_now=opred_future(p,slot,tick,h,u_now,u);p->missing+=p->missing_now;
    predict_pose full,dead,shown,cont;double rates_end[OPRED_AXES],speed=sqrt(predict_dot3(now->rates,now->rates));
    opred_run(p,model,&pose,now->rates,(const double (*)[OPRED_CHANNELS])u,h,(double)h,&full,NULL,&p->nonfinite);
    opred_run(p,0,&pose,now->rates,NULL,0,(double)h,&dead,NULL,NULL);
    ++p->predictions;
    /* The lead's target: none at travel speeds (hysteresis) or beyond the lead bounds. */
    if(!p->suspended && speed>OPRED_SPEED_SUSPEND) {p->suspended=1;++p->suspends;}
    else if(p->suspended && speed<OPRED_SPEED_RESUME) p->suspended=0;
    int bounded=predict_distance(&full,&pose)>OPRED_LEAD_POSITION || predict_angle(full.r,pose.r)>OPRED_LEAD_ANGLE;
    if(bounded) ++p->bounded;
    double target=p->suspended || bounded?0.0:(double)h,previous=p->h_eff,h_eff=previous;
    if(h_eff<target) {h_eff+=OPRED_RAMP;if(h_eff>target) h_eff=target;}
    else if(h_eff>target) {h_eff-=OPRED_RAMP;if(h_eff<target) h_eff=target;}
    opred_run(p,model,&pose,now->rates,(const double (*)[OPRED_CHANNELS])u,h,h_eff,&shown,rates_end,&p->nonfinite);
    /* The previous display path continued to this tick's end time, on the newest command. */
    if(p->have_end) {
        double steps=1.0+h_eff-previous;if(steps<0) steps=0;
        const double (*last)[OPRED_CHANNELS]=h_eff>0?(const double (*)[OPRED_CHANNELS])&u[(unsigned)ceil(h_eff)-1]:(const double (*)[OPRED_CHANNELS])&u_now;
        opred_run(p,model,&p->end,p->end_rates,last,1,steps,&cont,NULL,&p->nonfinite);
        double lambda=opred_tau_ms?exp(-1000.0*PREDICT_DT/(double)opred_tau_ms):0.0;
        double spin=sqrt(predict_dot3(now->rates+3,now->rates+3));
        if(opred_smooth(&p->correction,&cont,&shown,lambda,OPRED_SNAP_POSITION+speed*PREDICT_DT,
                        OPRED_BAND_POSITION+OPRED_BAND_FRACTION*speed*PREDICT_DT,OPRED_BAND_ANGLE+OPRED_BAND_FRACTION*spin*PREDICT_DT)) ++p->snaps;
    } else memset(&p->correction,0,sizeof(p->correction));
    predict_pose display;predict_offset_apply(&shown,&p->correction,&display);
    p->end=shown;memcpy(p->end_rates,rates_end,sizeof(rates_end));p->h_eff=h_eff;p->have_end=1;
    p->display=display;opred_floats(&display,p->shown);p->display_tick=tick;p->ready=1;
    p->lead_now_pos=predict_distance(&display,&pose);p->lead_now_ang=predict_angle(display.r,pose.r);
    opred_stat_add(&p->lead_pos,p->lead_now_pos,NULL);opred_stat_add(&p->lead_ang,p->lead_now_ang,NULL);
    /* Store the predictions for tick+h. */
    opred_entry *target_entry=&p->ring[(tick+h)%OPRED_RING];
    memset(target_entry,0,sizeof(*target_entry));target_entry->tick=tick+h;target_entry->predicted=1;target_entry->made=tick;
    target_entry->made_pose[PREDICT_MODEL]=full;target_entry->made_pose[PREDICT_DEAD]=dead;target_entry->made_pose[PREDICT_NONE]=pose;
    target_entry->made_pose[PREDICT_DISPLAY]=display;target_entry->display_valid=fabs(h_eff-(double)h)<1e-9;
}
/* The displayed pose of a predicted slot at a display offset (steps from this tick, -1..1): the
 * shown pose moved at its last rates, the correction decayed by the offset. 0 without one. */
static int opred_display_pose_of(unsigned slot,double offset,float out[12],uint64_t tick) {
    if(!opred_armed || slot<1 || slot>pilot_slot_count) return 0;
    const opred_slot *p=&opred_slots[slot-1];
    if(!p->enabled || !p->ready || p->display_tick!=tick) return 0;
    if(!isfinite(offset) || offset==0.0) {memcpy(out,p->shown,sizeof(p->shown));return 1;}
    if(offset<-1.0) offset=-1.0;
    if(offset>1.0) offset=1.0;
    predict_pose moved=p->end,display;predict_offset c=p->correction;
    predict_advance(&moved,p->end_rates,offset*PREDICT_DT);
    double lambda=opred_tau_ms?exp(-1000.0*offset*PREDICT_DT/(double)opred_tau_ms):(offset>0?0.0:1.0);
    for(unsigned i=0;i<3;i++) {c.p[i]*=lambda;c.r[i]*=lambda;}
    predict_offset_apply(&moved,&c,&display);predict_floats(&display,out);
    return 1;
}
static int opred_display_pose(unsigned slot,double offset,float out[12]) {return opred_display_pose_of(slot,offset,out,stats.issued);}

/* ---- slot 10 (present mode; MoveWorker, inside the render RNG split's wrapper) ---- */
static __attribute__((unused)) int opred_owns(const void *object) {
    for(unsigned i=0;opred_open && i<PILOT_SLOTS;i++) if(opred_open_at[i] && opred_open_object[i]==object) return 1;
    return 0;
}
/* hud = 0: slot 10's draw. hud = 1: the UI's projections in slot 6 (lockstep_own_predict_hud.h): the same swap at offset
 * 0, and before this tick's opred_frame has run (the start of slot 6) the pose is the previous tick's display advanced by
 * one step at its last rates (provisional: the block then holds this tick's simulated pose, which the predictor has not
 * sampled yet, so there is nothing to compare it with; the pointer swap and its restore are checked as ever). */
static uint64_t opred_hud_exact,opred_hud_provisional,opred_hud_stale,opred_hud_refused;
static int opred_present_enter_as(int hud) {
    if(!opred_armed || (opred_mode!=OPRED_PRESENT && opred_mode!=OPRED_SWAP) || !stats.active) return 0;
    if(join_catching_up()) {++opred_present_catchup;return 0;}   /* a joiner before its handover draws the plain steps */
    if(!hud) ++opred_present_calls;
    if(opred_open) {InterlockedIncrement64(&opred_failures);opred_stage=hud?"hud_nested":"present_nested";return 0;}
    if(GetCurrentThreadId()!=ui_thread) {++opred_present_thread;return 0;}
    double offset=hud?0.0:opred_display_offset?opred_display_offset():0.0;
    unsigned opened=0;
    for(unsigned i=0;i<pilot_slot_count && i<PILOT_SLOTS;i++) {
        opred_slot *p=&opred_slots[i];const pilot_slot *s=&pilot_slots[i];
        opred_open_at[i]=NULL;
        if(!p->enabled || !p->ready || !p->object) continue;
        const int provisional=hud && stats.issued && p->display_tick+1==stats.issued;
        if((p->display_tick!=stats.issued && !provisional) || s->object!=p->object) {++*(hud?&opred_hud_stale:&opred_present_stale);continue;}
        unsigned char *parent=NULL,*block=NULL,*volatile *at=NULL;float now[16];
        if(!opred_view(p->object,&parent,&at,&block,now) || parent!=p->parent) {++*(hud?&opred_hud_refused:&opred_present_refused);continue;}
        if(!provisional && !opred_same(now,p->sim_floats)) {
            if(hud || !p->prev_valid || !opred_same(now,p->prev_floats)) {++*(hud?&opred_hud_refused:&opred_present_refused);continue;}
            ++opred_present_previous; /* a render-only frame on the other bank */
        }
        float *mine=opred_blocks[i],shown[12];
        if(!read_local(block,mine,OPRED_BLOCK_BYTES)) {
            if(!read_local(block,mine,OPRED_BLOCK_MIN)) {++opred_present_refused;continue;}
            memset(mine+OPRED_BLOCK_MIN/4,0,sizeof(opred_blocks[i])-OPRED_BLOCK_MIN);
        }
        if(opred_mode==OPRED_SWAP) for(unsigned r=0;r<4;r++) memcpy(shown+3*r,now+4*r,3*sizeof(float)); /* the block's own pose */
        else if(provisional?!opred_display_pose_of(i+1,1.0,shown,stats.issued-1):!opred_display_pose(i+1,offset,shown)) {
            ++*(hud?&opred_hud_stale:&opred_present_stale);continue;
        }
        for(unsigned r=0;r<4;r++) memcpy(mine+4*r,shown+3*r,3*sizeof(float)); /* w lanes stay the engine's */
        int swapped=0;
        __try {swapped=InterlockedCompareExchangePointer((void*volatile*)at,(void*)mine,(void*)block)==(void*)block;}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {swapped=0;}
        if(!swapped) {++opred_present_refused;continue;}
        opred_open_at[i]=at;opred_open_saved[i]=block;opred_open_object[i]=p->object;++opened;
        if(!hud) ++opred_present_applied;else if(provisional) ++opred_hud_provisional;else ++opred_hud_exact;
    }
    opred_open=opened!=0;
    return opred_open;
}
static int opred_present_enter(void) {return opred_present_enter_as(0);}
/* After the original (also on unwind): every swapped entry back unless the engine replaced it. */
static void opred_present_leave(int entered) {
    if(!entered) return;
    for(unsigned i=PILOT_SLOTS;i-->0;) {
        unsigned char *volatile *at=opred_open_at[i];
        if(!at) continue;
        int back=-1;
        __try {back=InterlockedCompareExchangePointer((void*volatile*)at,(void*)opred_open_saved[i],(void*)opred_blocks[i])==(void*)opred_blocks[i];}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {back=-1;}
        if(back==1) ++opred_present_restored;
        else if(back==0) ++opred_present_lost;   /* the engine's own pointer stands */
        else {InterlockedIncrement64(&opred_failures);opred_stage="present_restore";}
        opred_open_at[i]=NULL;
    }
    opred_open=0;
}
/* Main-thread gate tick: a present failure stops the gate (184). */
static int opred_check_live(void) {
    return !opred_armed || (opred_mode!=OPRED_PRESENT && opred_mode!=OPRED_SWAP) || !InterlockedCompareExchange64(&opred_failures,0,0);
}

/* ---- arming, the per-tick frame, rows ---- */
static int opred_arm(int mode,int others,unsigned tau_ms,unsigned cap,const char **why) {
    *why="-";
    if(opred_armed) {
        if(mode==opred_mode && others==opred_others && tau_ms==opred_tau_ms && cap==opred_cap && stats.active) return 1;
        *why="armed_differently";return 0;
    }
    if(mode!=OPRED_OBSERVE && mode!=OPRED_PRESENT && mode!=OPRED_SWAP) {*why="mode";return 0;}
    if(tau_ms>OPRED_TAU_MAX_MS || cap<1 || cap>OPRED_HORIZON_MAX || (others!=0 && others!=1)) {*why="range";return 0;}
    if(!stats.active || stats.issued) {*why="after_first_tick";return 0;}
    if(!pilot_armed || pilot_source!=PILOT_SOURCE_WIRE || !pilot_delay) {*why="needs_wire_pilot";return 0;}
    if(!pnat_armed || pnat_mode!=PNAT_MODE_STOCK || !pstk_capturing) {*why="needs_stock_capture";return 0;}
    if(stats.slot<1 || stats.slot>pilot_slot_count || stats.slot>PILOT_SLOTS || pstk_capture_slot!=stats.slot ||
       !(pstk_slot_mask>>(stats.slot-1)&1u)) {*why="own_slot_not_stock";return 0;}
    if(predict_armed) {*why="pilot_predict_armed";return 0;}
    if(mode!=OPRED_OBSERVE && (!render_rng_installed || render_rng_mode!=RENDER_RNG_SPLIT)) {*why="present_without_render_split";return 0;}
    if(mode!=OPRED_OBSERVE && (!predict_pins() || !rdisp_pins())) {*why="present_pins";return 0;}
    memset(opred_slots,0,sizeof(opred_slots));
    for(unsigned i=0;i<pilot_slot_count && i<PILOT_SLOTS;i++) {
        opred_slot *p=&opred_slots[i];
        p->own=i+1==stats.slot;
        p->enabled=(pstk_slot_mask>>i&1u) && (p->own || others);
        opred_command_reset(&p->cmd);
        for(unsigned a=0;a<OPRED_AXES;a++) opred_axis_reset(&p->axis[a]);
    }
    opred_mode=mode;opred_others=others;opred_tau_ms=tau_ms;opred_cap=cap;
    opred_horizon=pilot_delay<cap?pilot_delay:cap;
    if(opred_horizon>OPRED_HORIZON_MAX) opred_horizon=OPRED_HORIZON_MAX;
    opred_frames=opred_last_tick=0;opred_failures=0;opred_open=0;memset(opred_open_at,0,sizeof(opred_open_at));
    opred_present_calls=opred_present_applied=opred_present_restored=opred_present_lost=opred_present_refused=0;
    opred_present_stale=opred_present_previous=opred_present_thread=0;
    opred_follow_written=opred_follow_restored=opred_follow_eye=opred_follow_rdisp=opred_follow_outside=0;
    opred_follow_parent=opred_follow_table=opred_follow_cached=0;
    opred_armed=1;opred_stage="armed";return 1;
}
/* An adaptive plan replaced the delay armed at the menu (kmax+2): the horizon follows, or the ship would be
 * drawn up to the cap ahead whatever the real delay is. Arrives with tick 0's permits, before any prediction. */
static void opred_delay_adopted(void) {
    if(!opred_armed) return;
    unsigned h=pilot_delay<opred_cap?pilot_delay:opred_cap;
    opred_horizon=h>OPRED_HORIZON_MAX?OPRED_HORIZON_MAX:h;
}
/* Once per admitted tick on the MoveWorker (slot 6), after this tick's stock capture. */
static int opred_frame(uint64_t tick,const char **why) {
    *why="-";
    if(!opred_armed) {*why="not_armed";return 0;}
    if(!tick || tick<=opred_last_tick || pstk_frame_tick!=tick) {*why="tick";return 0;}
    if(opred_open) {InterlockedIncrement64(&opred_failures);opred_stage="frame_in_render";*why="open";return 0;}
    for(unsigned i=0;i<pilot_slot_count && i<PILOT_SLOTS;i++) {
        opred_slot *p=&opred_slots[i];
        /* The previous tick's shown block, for a render-only frame on the other bank. */
        if(p->ready) {memcpy(p->prev_floats,p->sim_floats,sizeof(p->prev_floats));p->prev_valid=1;} else p->prev_valid=0;
        /* Absent (hot join): nobody steers it before its handover, so nothing to predict or present; it starts afresh at H. */
        if(p->enabled && !pilot_slot_present(i+1,tick)) {
            if(p->object) opred_reset(p,OPRED_RESETS);   /* not counted: no cause of the prediction's own */
            p->ready=0;p->object=NULL;p->parent=NULL;continue;
        }
        if(p->enabled) opred_slot_frame(i,tick);
    }
    opred_last_tick=tick;++opred_frames;opred_stage="live";return 1;
}
static double opred_mean(const opred_stat *s) {return s->n?s->sum/(double)s->n:0.0;}
static double opred_rms(const opred_stat *s) {return s->n?sqrt(s->sq/(double)s->n):0.0;}
static unsigned opred_ready_mask(const opred_slot *p) { /* bits 0..6 command channels, 8..13 axes */
    unsigned m=0;
    for(unsigned c=0;c<OPRED_CHANNELS;c++) if(opred_command_ready(&p->cmd,c)) m|=1u<<c;
    for(unsigned a=0;a<OPRED_AXES;a++) if(opred_axis_ready(&p->axis[a])) m|=1u<<(8+a);
    return m;
}
/* Per tick: sK=h_eff/model error landed this tick (m, rad; - when none)/lead now (m, rad)/snaps/missing records. */
static int opred_row(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"tick=%llu h=%u",(unsigned long long)opred_last_tick,opred_horizon);
    for(unsigned i=0;n>0 && (size_t)n<capacity && i<pilot_slot_count && i<PILOT_SLOTS;i++) {
        const opred_slot *p=&opred_slots[i];int more;
        if(!p->enabled) continue;
        if(!p->ready) more=snprintf(out+n,capacity-(size_t)n," s%u=-",i+1);
        else if(p->checked_now) more=snprintf(out+n,capacity-(size_t)n," s%u=%.2g/%.3g/%.3g/%.3g/%.3g/%llu/%llu",i+1,p->h_eff,p->last_pos,p->last_ang,
            p->lead_now_pos,p->lead_now_ang,(unsigned long long)p->snaps,(unsigned long long)p->missing_now);
        else more=snprintf(out+n,capacity-(size_t)n," s%u=%.2g/-/-/%.3g/%.3g/%llu/%llu",i+1,p->h_eff,p->lead_now_pos,p->lead_now_ang,
            (unsigned long long)p->snaps,(unsigned long long)p->missing_now);
        if(more<0) return 0;
        n+=more;
    }
    return n>0 && (size_t)n<capacity?n:0;
}
static int opred_status(char *out,size_t capacity) {
    static const char *kinds[PREDICT_KINDS]={"model","dead","none","display"};
    int n=snprintf(out,capacity,"mode=%s armed=%d horizon=%u cap=%u delay=%u tau_ms=%u others=%d frames=%llu tick=%llu stage=%s failures=%lld "
        "present_calls=%llu present_applied=%llu present_restored=%llu present_lost=%llu present_refused=%llu present_stale=%llu "
        "present_previous=%llu present_thread=%llu follow=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu",
        opred_mode==OPRED_OBSERVE?"observe":opred_mode==OPRED_PRESENT?"present":opred_mode==OPRED_SWAP?"swap":"off",opred_armed,opred_horizon,opred_cap,pilot_delay,opred_tau_ms,
        opred_others,(unsigned long long)opred_frames,(unsigned long long)opred_last_tick,opred_stage,(long long)opred_failures,
        (unsigned long long)opred_present_calls,(unsigned long long)opred_present_applied,(unsigned long long)opred_present_restored,
        (unsigned long long)opred_present_lost,(unsigned long long)opred_present_refused,(unsigned long long)opred_present_stale,
        (unsigned long long)opred_present_previous,(unsigned long long)opred_present_thread,
        (unsigned long long)opred_follow_written,(unsigned long long)opred_follow_restored,(unsigned long long)opred_follow_eye,
        (unsigned long long)opred_follow_rdisp,(unsigned long long)opred_follow_outside,(unsigned long long)opred_follow_parent,
        (unsigned long long)opred_follow_table,(unsigned long long)opred_follow_cached);
    if(n>0 && (size_t)n<capacity && opred_present_catchup) n+=snprintf(out+n,capacity-(size_t)n," present_catchup=%llu",(unsigned long long)opred_present_catchup);
    for(unsigned i=0;n>0 && (size_t)n<capacity && i<pilot_slot_count && i<PILOT_SLOTS;i++) {
        const opred_slot *p=&opred_slots[i];int more;
        if(!p->enabled) continue;
        more=snprintf(out+n,capacity-(size_t)n," s%u: own=%u samples=%llu predictions=%llu checked=%llu h_eff=%.2g snaps=%llu bounded=%llu suspends=%llu "
            "missing=%llu actions=%llu edges=%llu nonfinite=%llu resets=",i+1,p->own,(unsigned long long)p->samples,(unsigned long long)p->predictions,
            (unsigned long long)p->checked,p->h_eff,(unsigned long long)p->snaps,(unsigned long long)p->bounded,(unsigned long long)p->suspends,
            (unsigned long long)p->missing,(unsigned long long)p->actions,(unsigned long long)p->edges,(unsigned long long)p->nonfinite);
        if(more<0) return 0;
        n+=more;
        for(unsigned r=0;n>0 && (size_t)n<capacity && r<OPRED_RESETS;r++) {
            more=snprintf(out+n,capacity-(size_t)n,"%s%s:%llu",r?",":"",opred_reset_names[r],(unsigned long long)p->resets[r]);
            if(more<0) return 0;
            n+=more;
        }
        if(n>0 && (size_t)n<capacity) {
            more=snprintf(out+n,capacity-(size_t)n," ready=%04x command=%llu axes=%llu,%llu,%llu,%llu,%llu,%llu refused=%llu,%llu,%llu,%llu,%llu,%llu",
                opred_ready_mask(p),(unsigned long long)p->cmd.samples,(unsigned long long)p->axis[0].samples,(unsigned long long)p->axis[1].samples,
                (unsigned long long)p->axis[2].samples,(unsigned long long)p->axis[3].samples,(unsigned long long)p->axis[4].samples,
                (unsigned long long)p->axis[5].samples,(unsigned long long)p->axis[0].refused,(unsigned long long)p->axis[1].refused,
                (unsigned long long)p->axis[2].refused,(unsigned long long)p->axis[3].refused,(unsigned long long)p->axis[4].refused,
                (unsigned long long)p->axis[5].refused);
            if(more<0) return 0;
            n+=more;
        }
        for(unsigned k=0;n>0 && (size_t)n<capacity && k<PREDICT_KINDS;k++) {
            more=snprintf(out+n,capacity-(size_t)n," %s=%.4g/%.4g/%.4g,%.4g/%.4g/%.4g",kinds[k],opred_mean(&p->pos[k]),opred_rms(&p->pos[k]),p->pos[k].max,
                opred_mean(&p->ang[k]),opred_rms(&p->ang[k]),p->ang[k].max);
            if(more<0) return 0;
            n+=more;
        }
        if(n>0 && (size_t)n<capacity) {
            more=snprintf(out+n,capacity-(size_t)n," lead=%.4g/%.4g/%.4g,%.4g/%.4g/%.4g hist=",opred_mean(&p->lead_pos),opred_rms(&p->lead_pos),
                p->lead_pos.max,opred_mean(&p->lead_ang),opred_rms(&p->lead_ang),p->lead_ang.max);
            if(more<0) return 0;
            n+=more;
        }
        for(unsigned b=0;n>0 && (size_t)n<capacity && b<OPRED_BUCKETS;b++) {
            more=snprintf(out+n,capacity-(size_t)n,"%s%llu",b?",":"",(unsigned long long)p->pos[PREDICT_MODEL].bucket[b]);
            if(more<0) return 0;
            n+=more;
        }
    }
    return n>0 && (size_t)n<capacity?n:0;
}

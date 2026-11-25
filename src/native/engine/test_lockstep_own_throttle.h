/* The own ship's throttle, boost and travel drive at once (lockstep_own_throttle.h) on fake engine memory, for the own slot
 * 1, 2 and 3: the bank in the canonical image (slot 1) or in the slot's store (2, 3), the records through the real rings
 * (applied ones in the staging ring, pending ones in the capture ring, this frame's in pstk_build), the HUD's call with a
 * truth's GetPlayerSpeed answers. The truth is the test's own: X4's throttle rules as read from the 9.00 code (the key step
 * k (r + (1-r)|s|) with r from the ramp base, the wheel, full forward, stop, a boost that sets the setpoint to 1 and fails
 * without energy, the hold at zero for 0.5 s, the smoothing at its rates), a travel drive that answers its key a tick later
 * (md/modes.xml) unless jammed. Checked: after the fits have learnt, the targeted speed the HUD shows at T is the truth's at
 * T+d (press, release, wheel, stop, full forward, through zero); a key shows in the frame it is pressed (before the
 * capture: pstk_build); boost and travel at once, confirmed, refused (shown d+G ticks at most, then not again until the
 * simulation changes); the speed moved by own_predict's displayed forward speed; the motion path own_predict's command
 * model takes; the stock answers outside the own slot's presentation; arming refusals; worst-case widths. */
#define TTHR_DELAY 3u
#define TTHR_TICKS 760u
static const double TTHR_K=0.9,TTHR_C1=0.3,TTHR_C2=0.5,TTHR_KW=0.08,TTHR_UP=0.8,TTHR_DOWN=1.2,TTHR_LO=-0.5,TTHR_HI=1.0;
static struct {
    double s,desired,smoothed,m,step,hold_until,v,time;unsigned char acc,dec,ff,boost,travel,energy,jammed,travel_due;uint64_t travel_at;
} tthr;
static unsigned tthr_slot;
static double tthr_shown_targeted[TTHR_TICKS+8],tthr_truth_targeted[TTHR_TICKS+8],tthr_truth_s[TTHR_TICKS+8];
static unsigned char tthr_shown_boost[TTHR_TICKS+8],tthr_shown_travel[TTHR_TICKS+8],tthr_partial_at[TTHR_TICKS+8];
static int tthr_sign(double x) {return x>0?1:x<0?-1:0;}
static double tthr_clamp(double x) {return x<TTHR_LO?TTHR_LO:x>TTHR_HI?TTHR_HI:x;}
static void tthr_ramp(double delta) {
    if(tthr_sign(delta)!=tthr_sign(tthr.step)) tthr.hold_until=0;
    else if(tthr.hold_until>tthr.time+1e-9) {tthr.step=delta;return;}
    double s=tthr.s;
    if(fabs(s)>=1e-4 && tthr_sign(s)!=tthr_sign(s+delta)) {s=0;tthr.hold_until=tthr.time+0.5;}
    else s+=delta;
    s=tthr_clamp(s);if(fabs(s)<1e-4) s=0;
    tthr.s=s;tthr.step=delta;
}
/* One tick of the truth: the record applied at T in order, the MD answer, the key step, the smoothing, the ship. */
static void tthr_apply(uint64_t T,const pstk_record *r) {
    tthr.time=(double)T/30.0;
    for(unsigned i=0;r && i<r->count;i++) {
        const pstk_event *e=&r->events[i];float f;memcpy(&f,&e->value,4);const unsigned char on=e->value!=0;
        if(e->kind==PSTK_ACTION && e->id==0x7b) {tthr.s=0;tthr.boost=0;}
        else if(e->kind==PSTK_ACTION && e->id==0x12f) {if(!tthr.jammed) {tthr.travel_due=1;tthr.travel_at=T+1;}}
        else if(e->kind==PSTK_STATE && (e->id==0xd || e->id==0xe)) {
            if(on) tthr.m=tthr.s;else tthr.hold_until=0;
            if(e->id==0xd) tthr.acc=on;else tthr.dec=on;
        } else if(e->kind==PSTK_STATE && e->id==0xf) {
            if(on && !tthr.ff) {tthr.s=tthr_clamp(1.0);tthr.ff=1;}
            else if(!on && tthr.ff) {tthr.s=tthr_clamp(0.0);tthr.ff=0;}
        } else if(e->kind==PSTK_STATE && e->id==0x11) {
            if(on && !tthr.boost) {tthr.s=tthr_clamp(1.0);tthr.boost=tthr.energy;}
            else if(!on) tthr.boost=0;
        } else if(e->kind==PSTK_RANGE && e->id==0xa && fabs(f)>=1e-4) {
            const double a=fabs(tthr.s),rr=TTHR_C1+TTHR_C2*a;tthr.m=tthr.s;tthr_ramp((double)f*TTHR_KW*(rr+(1-rr)*a));
        }
    }
    if(tthr.travel_due && T>=tthr.travel_at) {tthr.travel=!tthr.travel;tthr.travel_due=0;}
    /* vfunc33 before d95180's key step: desired is the setpoint after the handlers, the smoothing follows it */
    tthr.desired=tthr.s;
    const double d=tthr.desired-tthr.smoothed;
    if(fabs(d)<1e-4) tthr.smoothed=tthr.desired;
    else tthr.smoothed+=d>0?(d<TTHR_UP/30.0?d:TTHR_UP/30.0):(d>-TTHR_DOWN/30.0?d:-TTHR_DOWN/30.0);
    const int key=(int)tthr.acc-(int)tthr.dec;
    if(key) {const double rr=TTHR_C1+TTHR_C2*fabs(tthr.m);tthr_ramp((double)key*TTHR_K/30.0*(rr+(1-rr)*fabs(tthr.s)));}
    tthr.v+=0.1*(300.0*tthr.smoothed*(tthr.boost?1.6:1.0)*(tthr.travel?12.0:1.0)-tthr.v);
}
static double tthr_targeted(void) {return tthr.travel?2.0:tthr.boost?1.0:tthr.desired;}   /* the drive answers once MD ran */
static void tthr_put(unsigned addr,const void *v,unsigned n) {
    if(tthr_slot==1) memcpy(pnat_base+addr,v,n);else memcpy(pslots[tthr_slot].store+test_pslot_store_offset(addr),v,n);
}
static void tthr_bank(void) {
    float s=(float)tthr.s,ds=(float)tthr.desired,sm=(float)tthr.smoothed,lo=(float)TTHR_LO,hi=(float)TTHR_HI,step=(float)tthr.step,m=(float)tthr.m,
        up=(float)TTHR_UP,down=(float)TTHR_DOWN,z=0.0f;unsigned char on=1,off[2]={0,0};uint64_t none=0;
    tthr_put(OTHR_SETPOINT,&s,4);tthr_put(OTHR_DESIRED,&ds,4);tthr_put(OTHR_SMOOTHED,&sm,4);tthr_put(OTHR_MIN,&lo,4);tthr_put(OTHR_MAX,&hi,4);
    tthr_put(OTHR_STEP_LAST,&step,4);tthr_put(OTHR_HOLD_UNTIL,&tthr.hold_until,8);tthr_put(OTHR_RAMP_BASE,&m,4);tthr_put(OTHR_LINEAR_Z,&z,4);
    tthr_put(OTHR_RATE_UP,&up,4);tthr_put(OTHR_RATE_DOWN,&down,4);tthr_put(OTHR_AUTOPILOT,&none,8);tthr_put(OTHR_FLIGHT,&on,1);
    tthr_put(OTHR_MATCH,off,1);tthr_put(OTHR_FULL_STOP,off,2);tthr_put(OTHR_HIGHWAY,off,1);
}
/* The person's input captured at c. */
static void tthr_script(uint64_t c,pstk_record *r) {
    memset(r,0,sizeof(*r));r->capture16=(uint16_t)c;
    struct {uint64_t at;unsigned kind,id;float value;} plan[64];unsigned n=0;
#define TTHR_AT(t,k,i,v) do {plan[n].at=(t);plan[n].kind=(k);plan[n].id=(i);plan[n].value=(v);++n;} while(0)
    /* learning: accelerate from 0 and again from a ramp base above it, decelerate twice, the wheel both ways */
    TTHR_AT(10,PSTK_STATE,0xd,1);TTHR_AT(30,PSTK_STATE,0xd,0);TTHR_AT(40,PSTK_STATE,0xd,1);TTHR_AT(60,PSTK_STATE,0xd,0);
    TTHR_AT(70,PSTK_STATE,0xe,1);TTHR_AT(82,PSTK_STATE,0xe,0);TTHR_AT(90,PSTK_STATE,0xe,1);TTHR_AT(100,PSTK_STATE,0xe,0);
    TTHR_AT(185,PSTK_STATE,0xd,1);TTHR_AT(200,PSTK_STATE,0xd,0);   /* the press checked in its own frame */
    TTHR_AT(240,PSTK_ACTION,0x7b,0);
    TTHR_AT(250,PSTK_STATE,0xd,1);TTHR_AT(262,PSTK_STATE,0xd,0);
    TTHR_AT(270,PSTK_STATE,0xe,1);TTHR_AT(320,PSTK_STATE,0xe,0);   /* through zero: the hold, then reverse */
    TTHR_AT(330,PSTK_STATE,0xf,1);TTHR_AT(340,PSTK_STATE,0xf,0);
    TTHR_AT(400,PSTK_STATE,0x11,1);TTHR_AT(420,PSTK_STATE,0x11,0);  /* boost */
    TTHR_AT(450,PSTK_STATE,0x11,1);TTHR_AT(462,PSTK_STATE,0x11,0);  /* no energy: refused */
    TTHR_AT(470,PSTK_STATE,0x11,1);TTHR_AT(476,PSTK_STATE,0x11,0);  /* again: not shown */
    TTHR_AT(520,PSTK_ACTION,0x12f,0);TTHR_AT(600,PSTK_ACTION,0x12f,0);   /* travel on, off */
    TTHR_AT(650,PSTK_ACTION,0x12f,0);   /* jammed */
    pstk_event *e;
    for(unsigned i=0;i<n;i++) {
        if(plan[i].at!=c) continue;
        e=&r->events[r->count++];e->kind=(unsigned char)plan[i].kind;e->id=(uint16_t)plan[i].id;
        if(plan[i].kind==PSTK_RANGE) memcpy(&e->value,&plan[i].value,4);else e->value=plan[i].value!=0.0f;
    }
    /* the wheel: up every other tick 110..150, down 152..176, one step at 280 is inside the decelerate (not learnt) */
    if((c>=110 && c<=150 && c%2==0) || (c>=152 && c<=176 && c%2==0) || c==210 || c==214) {
        float w=(c>=152 && c<=176)?-1.0f:1.0f;if(c==214) w=-1.0f;
        e=&r->events[r->count++];e->kind=PSTK_RANGE;e->id=0xa;memcpy(&e->value,&w,4);
    }
#undef TTHR_AT
}
static void tthr_setup(unsigned slot) {
    memset(&tthr,0,sizeof(tthr));tthr.energy=1;tthr_slot=slot;
    memset(pstk_ring,0,sizeof(pstk_ring));memset(pstk_locals,0,sizeof(pstk_locals));memset(&pstk_build,0,sizeof(pstk_build));
    memset(opred_slots,0,sizeof(opred_slots));memset(pilot_slots,0,sizeof(pilot_slots));
    for(unsigned k=2;k<=3;k++) {memset(&pslots[k],0,sizeof(pslots[k]));pslots[k].seated=1;}
    pilot_slot_count=3;pilot_source=PILOT_SOURCE_WIRE;pilot_delay=TTHR_DELAY;pilot_armed=1;pilot_start=4;pilot_stop=100000;
    pnat_armed=1;pnat_mode=PNAT_MODE_STOCK;pstk_capturing=1;pstk_capture_slot=slot;pstk_slot_mask=7;pstk_frame_tick=0;pstk_scope_slot=0;
    pnat_base=image;image[OTHR_BOOST_TOGGLE]=0;
    stats.slot=slot;stats.active=1;stats.issued=0;
    opred_armed=1;opred_mode=OPRED_PRESENT;opred_slots[slot-1].own=1;opred_slots[slot-1].enabled=1;
    pres_open=slot>=2;pres_slot=slot>=2?slot:0;
    othr_armed=0;othr_pins_checked=1;opred_throttle_path=NULL;
    const char *why;assert(othr_arm(1,1,&why) && othr_slot==slot && opred_throttle_path==othr_motion_path);
    assert(othr_arm(1,1,&why));assert(!othr_arm(2,1,&why) && !strcmp(why,"armed_differently"));
    tthr_bank();
}
static void tthr_teardown(void) {
    othr_restore();assert(!othr_armed && !opred_throttle_path);othr_now_override=0;othr_pins_checked=0;
    memset(opred_slots,0,sizeof(opred_slots));opred_armed=0;opred_mode=OPRED_OFF;
    memset(pilot_slots,0,sizeof(pilot_slots));pilot_slot_count=pilot_source=pilot_delay=0;pilot_armed=0;pilot_start=pilot_stop=0;
    pnat_armed=0;pnat_mode=0;pstk_capturing=0;pstk_capture_slot=0;pstk_slot_mask=0;pstk_frame_tick=0;
    memset(pstk_ring,0,sizeof(pstk_ring));memset(pstk_locals,0,sizeof(pstk_locals));memset(&pstk_build,0,sizeof(pstk_build));
    for(unsigned k=2;k<=3;k++) memset(&pslots[k],0,sizeof(pslots[k]));
    pres_open=0;pres_slot=0;stats.slot=1;stats.issued=0;
}
/* The HUD's call with the truth's answers; returns the shown ones. */
static void tthr_hud(uint64_t T,double out[5]) {
    const double in[5]={tthr.v/300.0>3.0?3.0:tthr.v/300.0,tthr_targeted(),trunc(tthr.v),tthr.boost,tthr.travel};
    const uint64_t partial=othr_partial;
    assert(othr_speed(in,out));
    tthr_partial_at[T]=othr_partial!=partial;
    const uint64_t calls=othr_calls,outside=othr_outside;double again[5];
    pres_open=!pres_open;assert(!othr_speed(in,again) && !memcmp(in,again,sizeof(again)) && othr_outside==outside+1);  /* not its scope */
    pres_open=!pres_open;pstk_scope_slot=2;assert(!othr_speed(in,again) && !memcmp(in,again,sizeof(again)));pstk_scope_slot=0;
    assert(othr_calls==calls+2);
    tthr_shown_targeted[T]=out[1];tthr_truth_targeted[T]=in[1];tthr_shown_boost[T]=out[3]!=0;tthr_shown_travel[T]=out[4]!=0;
}
static void tthr_tick(uint64_t T,int hud_before_capture,int hud_after_frame) {
    /* path_release: the record captured at T-d applies */
    pstk_record r;int have=0;
    if(T>TTHR_DELAY) {pstk_staged *e=&pstk_ring[tthr_slot-1][T%PSTK_RING];assert(e->tick==T && pstk_decode(e->bytes,e->length,&r));e->present=0;have=1;}
    if(T==440) tthr.energy=0;
    if(T==500) tthr.energy=1;
    if(T==640) tthr.jammed=1;
    tthr_apply(T,have?&r:NULL);tthr_bank();tthr_truth_s[T]=tthr.s;
    othr_now_override=tthr.time;
    stats.issued=T;pstk_frame_tick=T;
    /* slot 4: this frame's dispatch */
    pstk_record c;tthr_script(T,&c);pstk_build=c;pstk_build.count=c.count;
    double out[5];
    if(hud_before_capture) tthr_hud(T,out);
    /* the probe's capture */
    unsigned char bytes[PSTK_BYTES_MAX];const unsigned n=pstk_encode(&c,bytes);
    pstk_local *l=&pstk_locals[T%PSTK_RING];memset(l,0,sizeof(*l));l->capture=T;l->apply=T+TTHR_DELAY;l->length=n;l->used=1;memcpy(l->bytes,bytes,n);
    pstk_staged *e=&pstk_ring[tthr_slot-1][(T+TTHR_DELAY)%PSTK_RING];memset(e,0,sizeof(*e));e->tick=T+TTHR_DELAY;e->length=n;memcpy(e->bytes,bytes,n);e->present=1;
    memset(&pstk_build,0,sizeof(pstk_build));
    if(!hud_before_capture && !hud_after_frame) tthr_hud(T,out);
    /* own_predict's step for this tick (its simulated forward speed; the display a step ahead) */
    opred_slot *p=&opred_slots[tthr_slot-1];opred_entry *oe=&p->ring[T%OPRED_RING];memset(oe,0,sizeof(*oe));
    oe->tick=T;oe->valid=1;oe->rates_valid=1;oe->rates[2]=tthr.v;
    /* the frame */
    char row[512];const char *why;const int rows=othr_frame(T,row,sizeof(row),&why);
    assert(rows>=0);
    if(rows>0) assert(!strncmp(row,"tick=",5) && strstr(row," desired=") && (strstr(row," targeted=") || strstr(row," hud=-")));
    if(hud_after_frame) tthr_hud(T,out);   /* live: slot 6 runs the probe's frame first */
}
static void tthr_hook(unsigned slot);
static void test_othr_slot(unsigned slot) {
    tthr_setup(slot);
    memset(tthr_shown_targeted,0,sizeof(tthr_shown_targeted));memset(tthr_truth_targeted,0,sizeof(tthr_truth_targeted));memset(tthr_truth_s,0,sizeof(tthr_truth_s));
    /* The HUD before the capture (a key of this frame from pstk_build), after it, and after the probe's frame (the live order). */
    for(uint64_t T=1;T<=TTHR_TICKS;T++) tthr_tick(T,T%7!=0 && T%2==0,T%2==1);
    /* The fits learnt from the authoritative ticks alone. */
    assert(othr_fit_ready(&othr_fits[OTHR_FIT_UP]) && othr_fit_ready(&othr_fits[OTHR_FIT_DOWN]) && othr_fit_ready(&othr_fits[OTHR_FIT_WHEEL]));
    assert(!othr_fits[OTHR_FIT_AXIS].samples && othr_order[1]>othr_order[0] && othr_rows_written>100);
    /* Press, release, wheel, stop, accelerate, through zero (hold), full forward: what the HUD showed at T is the truth's
     * setpoint at the end of T+d (its own bar shows that as desired at T+d after a handler, at T+d+1 after a key step: its
     * vfunc33 runs before the key step). */
    double worst=0;uint64_t at=0,checked=0;
    for(uint64_t T=180;T<=360;T++) {
        const double d=fabs(tthr_shown_targeted[T]-tthr_truth_s[T+TTHR_DELAY]);
        if(d>worst) {worst=d;at=T;}
        ++checked;
    }
    if(worst>2e-3) {
        char status[2048];othr_status(status,sizeof(status));
        printf("own throttle slot %u: shown %.5f at %llu (partial %d), truth %.5f at %llu; %s\n",slot,tthr_shown_targeted[at],(unsigned long long)at,
            tthr_partial_at[at],tthr_truth_s[at+TTHR_DELAY],(unsigned long long)(at+TTHR_DELAY),status);
        for(uint64_t T=at>8?at-8:0;T<=at+4;T++) printf("  T=%llu shown=%.5f truth(T+d)=%.5f partial=%d\n",(unsigned long long)T,tthr_shown_targeted[T],
            tthr_truth_s[T+TTHR_DELAY],tthr_partial_at[T]);
    }
    assert(worst<=2e-3 && checked==181);
    /* The press of 185 showed in its own frame: the simulation had not moved yet. */
    assert(tthr_truth_targeted[185]==tthr_truth_targeted[184] && tthr_shown_targeted[185]>tthr_truth_targeted[185]+1e-4);
    /* Without this module the screen is the truth d ticks late: the stop at 240 shows 0 at once, the simulation still moves. */
    assert(tthr_shown_targeted[240]==0.0 && tthr_truth_targeted[240]>0.1);
    /* Boost at once (400..419 captured, the simulation from 403), targeted 1, off at the release. */
    assert(!tthr_shown_boost[399] && tthr_shown_boost[400] && tthr_shown_targeted[400]==1.0 && tthr_truth_targeted[400]!=1.0);
    for(uint64_t T=400;T<420;T++) assert(tthr_shown_boost[T]);
    assert(!tthr_shown_boost[420] && !tthr_shown_boost[424]);
    /* No energy at 450: shown at once, then refused within the grace; the press at 470 not shown again. */
    assert(tthr_shown_boost[450] && othr_refused[OTHR_BOOST]>=1);
    unsigned shown=0;for(uint64_t T=450;T<470;T++) shown+=tthr_shown_boost[T];
    for(uint64_t T=450;T<450+TTHR_DELAY;T++) assert(tthr_shown_boost[T]);
    assert(shown>=TTHR_DELAY && shown<=TTHR_DELAY+OTHR_GRACE_MAX);
    for(uint64_t T=470;T<480;T++) assert(!tthr_shown_boost[T]);
    /* Travel at once (520), targeted 2, kept until the simulation answers (MD a tick later), off at once at 600. */
    assert(!tthr_shown_travel[519] && tthr_shown_travel[520] && tthr_shown_targeted[520]==2.0 && tthr_truth_targeted[520]!=2.0);
    for(uint64_t T=520;T<600;T++) assert(tthr_shown_travel[T]);
    assert(!tthr_shown_travel[600] && !tthr_shown_travel[610] && othr_confirmed[OTHR_TRAVEL]>=2 && othr_lag_max>=1);
    /* Jammed at 650: shown at once, refused within the grace. */
    shown=0;for(uint64_t T=650;T<700;T++) shown+=tthr_shown_travel[T];
    assert(tthr_shown_travel[650] && othr_refused[OTHR_TRAVEL]==1 && shown>=TTHR_DELAY && shown<=TTHR_DELAY+OTHR_GRACE_MAX);
    for(uint64_t T=700;T<=TTHR_TICKS;T++) assert(!tthr_shown_travel[T] && !tthr_shown_boost[T]);
    /* The motion path own_predict's command model takes: the predicted smoothed throttle, here through a wheel step. */
    {   const uint64_t T=TTHR_TICKS;opred_slot *p=&opred_slots[slot-1];
        pstk_record w;memset(&w,0,sizeof(w));w.capture16=(uint16_t)(T+1-TTHR_DELAY);float one=-1.0f;w.count=1;w.events[0].kind=PSTK_RANGE;w.events[0].id=0xa;
        memcpy(&w.events[0].value,&one,4);unsigned char bytes[PSTK_BYTES_MAX];const unsigned n=pstk_encode(&w,bytes);
        pstk_local *l=&pstk_locals[(T+1-TTHR_DELAY)%PSTK_RING];memset(l,0,sizeof(*l));l->capture=T+1-TTHR_DELAY;l->apply=T+1;l->length=n;l->used=1;memcpy(l->bytes,bytes,n);
        double path[OPRED_HORIZON_MAX],u[OPRED_HORIZON_MAX][OPRED_CHANNELS],u_now[OPRED_CHANNELS]={0,0,0,tthr.smoothed,0,0,0};
        assert(othr_motion_samples>=OTHR_MOTION_READY && othr_motion_path(slot,T,TTHR_DELAY,path)==TTHR_DELAY && path[0]<tthr.smoothed);
        opred_command_reset(&p->cmd);opred_future(p,slot,T,TTHR_DELAY,u_now,u);
        for(unsigned k=0;k<TTHR_DELAY;k++) assert(u[k][3]==path[k]);
        othr_motion=0;assert(!othr_motion_path(slot,T,TTHR_DELAY,path));othr_motion=1;
        assert(!othr_motion_path(slot%3+1,T,TTHR_DELAY,path));   /* another slot */
    }
    /* The speed: own_predict draws the ship 6 m/s slower than the simulation's forward speed while the wheel step above
     * lowers the throttle (pending at T+1): the number follows; faster, against the throttle, or at a steady one it does not. */
    {   const uint64_t T=TTHR_TICKS;opred_slot *p=&opred_slots[slot-1];p->ready=1;p->have_end=1;p->display_tick=T;
        opred_entry *oe=&p->ring[T%OPRED_RING];oe->rates[2]=200.0;p->end_rates[2]=194.0;
        tthr.v=200.0;tthr.boost=tthr.travel=tthr.travel_due=0;double out[5];
        const double in[5]={200.0/300.0,tthr.desired,200.0,0,0};
        assert(othr_slope_known[1] && fabs(othr_slope[1]-1.0/300.0)<1e-6);
        assert(othr_speed(in,out) && out[2]==194.0 && fabs(out[0]-194.0/300.0)<1e-6 && out[1]<in[1]);
        opred_entry *before=&p->ring[(T-1)%OPRED_RING];before->tick=T-1;before->valid=before->rates_valid=1;before->rates[2]=200.0;
        p->display_tick=T-1;assert(othr_speed(in,out) && out[2]==194.0);   /* the HUD before this tick's own_predict step */
        p->display_tick=T-2;assert(othr_speed(in,out) && out[2]==200.0);
        p->display_tick=T;p->end_rates[2]=199.7;assert(othr_speed(in,out) && out[2]==200.0 && out[0]==in[0]);   /* noise */
        p->end_rates[2]=206.6;assert(othr_speed(in,out) && out[2]==200.0 && out[0]==in[0]);   /* against the throttle */
        p->end_rates[2]=193.4;assert(othr_speed(in,out) && out[2]==193.0);
        memset(&pstk_locals[(T+1-TTHR_DELAY)%PSTK_RING],0,sizeof(pstk_local));   /* no input pending: a steady throttle */
        assert(othr_speed(in,out) && out[2]==200.0 && out[0]==in[0]);                /* ...at a steady speed: none */
        before->rates[2]=197.0;assert(othr_speed(in,out) && out[2]==200.0);           /* speeding up, drawn slower: none */
        p->end_rates[2]=206.4;assert(othr_speed(in,out) && out[2]==206.0);            /* speeding up, drawn faster */
    }
    tthr_hook(slot);
    char status[2048];assert(othr_status(status,sizeof(status))>0 && strstr(status,"armed=1") && strstr(status," up=") && strstr(status," travel="));
    tthr_teardown();
}
/* The hook on a fake Lua stack (index 1..top), and its install on the fake image. */
static double tthr_stack[24];static int tthr_top;static double tthr_answers[8];static int tthr_answer_count=8;
static int tthr_gettop(lua_State *L) {(void)L;return tthr_top;}
static double tthr_tonumber(lua_State *L,int i) {(void)L;return tthr_stack[i];}
static int tthr_toboolean(lua_State *L,int i) {(void)L;return tthr_stack[i]!=0;}
static void tthr_pushnumber(lua_State *L,double v) {(void)L;tthr_stack[++tthr_top]=v;}
static void tthr_pushboolean(lua_State *L,int v) {(void)L;tthr_stack[++tthr_top]=v?1:0;}
static void tthr_replace(lua_State *L,int i) {(void)L;tthr_stack[i]=tthr_stack[tthr_top--];}
static int tthr_original(lua_State *L) {(void)L;for(int i=0;i<tthr_answer_count;i++) tthr_stack[++tthr_top]=tthr_answers[i];return tthr_answer_count;}
static void tthr_hook(unsigned slot) {
    double (*number)(lua_State*,int)=p_lua_tonumber;void (*push)(lua_State*,double)=p_lua_pushnumber;
    p_lua_tonumber=tthr_tonumber;p_lua_pushnumber=tthr_pushnumber;
    othr_lua_gettop=tthr_gettop;othr_lua_toboolean=tthr_toboolean;othr_lua_replace=tthr_replace;othr_lua_pushboolean=tthr_pushboolean;
    othr_speed_original=tthr_original;const DWORD ui=ui_thread;ui_thread=GetCurrentThreadId();
    /* The same answers through the hook and through othr_speed; a Lua value below the eight stays. */
    const double in[5]={0.5,0.25,150.0,0,0};double out[5];
    for(int i=0;i<5;i++) tthr_answers[i]=in[i];
    tthr_answers[5]=1;tthr_answers[6]=0.75;tthr_answers[7]=0;
    assert(othr_speed(in,out));
    tthr_top=1;tthr_stack[1]=42;const uint64_t calls=othr_hook_calls;
    assert(othr_speed_hook((lua_State*)&tthr_top)==8 && tthr_top==9 && tthr_stack[1]==42 && othr_hook_calls==calls+1);
    for(int i=0;i<5;i++) assert(tthr_stack[2+i]==(i>=3?(out[i]!=0):out[i]));
    assert(tthr_stack[7]==1 && tthr_stack[8]==0.75 && tthr_stack[9]==0);
    tthr_answer_count=0;tthr_top=0;const uint64_t shorts=othr_hook_short;
    assert(othr_speed_hook((lua_State*)&tthr_top)==0 && tthr_top==0 && othr_hook_short==shorts+1);   /* no ship: nothing pushed */
    tthr_answer_count=8;ui_thread=GetCurrentThreadId()+1;tthr_top=0;
    assert(othr_speed_hook((lua_State*)&tthr_top)==8 && othr_hook_thread==1 && tthr_stack[2]==in[1]);   /* another thread: stock */
    othr_hook_thread=0;ui_thread=GetCurrentThreadId();
    /* The install on the fake image: the entry jumps to the page, the page's trampoline runs the prologue and goes back. */
    if(slot==1) {
        unsigned char saved[16];memcpy(saved,image+OTHR_SPEED_RVA,16);memcpy(image+OTHR_SPEED_RVA,othr_speed_bytes,OTHR_SPEED_PROLOGUE);
        const char *why;assert(othr_hook_install(&why) && othr_hooked && !othr_hook_restored && othr_page);
        int32_t rel;memcpy(&rel,image+OTHR_SPEED_RVA+1,4);
        assert(image[OTHR_SPEED_RVA]==0xe9 && image+OTHR_SPEED_RVA+5+rel==othr_page+OTHR_STUB && image[OTHR_SPEED_RVA+5]==0x57);
        void *back,*to;memcpy(&back,othr_page+OTHR_SPEED_PROLOGUE+6,8);memcpy(&to,othr_page+OTHR_STUB+6,8);
        assert(!memcmp(othr_page,othr_speed_bytes,OTHR_SPEED_PROLOGUE) && othr_page[OTHR_SPEED_PROLOGUE]==0xff && othr_page[OTHR_SPEED_PROLOGUE+1]==0x25 &&
               back==(void*)(image+OTHR_SPEED_RVA+OTHR_SPEED_PROLOGUE) && to==(void*)othr_speed_hook && othr_speed_original==(void*)othr_page);
        assert(othr_hook_install(&why));   /* the rerun */
        othr_hook_uninstall();assert(!othr_hooked && othr_hook_restored && !memcmp(image+OTHR_SPEED_RVA,othr_speed_bytes,OTHR_SPEED_PROLOGUE));
        image[OTHR_SPEED_RVA]=0x90;assert(!othr_hook_install(&why) && !strcmp(why,"entry") && !othr_hooked);   /* another build */
        memcpy(image+OTHR_SPEED_RVA,saved,16);
    }
    othr_speed_original=NULL;ui_thread=ui;p_lua_tonumber=number;p_lua_pushnumber=push;
}
static void test_othr_arming(void) {
    const char *why;othr_armed=0;othr_pins_checked=1;
    opred_armed=0;stats.active=1;stats.slot=1;pilot_slot_count=2;
    assert(!othr_arm(0,1,&why) && !strcmp(why,"own_predict_present"));
    opred_armed=1;opred_mode=OPRED_OBSERVE;assert(!othr_arm(0,1,&why) && !strcmp(why,"own_predict_present"));
    opred_mode=OPRED_PRESENT;memset(opred_slots,0,sizeof(opred_slots));assert(!othr_arm(0,1,&why) && !strcmp(why,"own_slot"));
    opred_slots[0].own=opred_slots[0].enabled=1;stats.slot=3;assert(!othr_arm(0,1,&why) && !strcmp(why,"own_slot"));
    stats.slot=1;othr_pins_checked=-1;assert(!othr_arm(0,1,&why) && !strcmp(why,"pins"));
    othr_pins_checked=1;assert(othr_arm(0,0,&why) && !opred_throttle_path);   /* motion off: no join */
    double in[5]={0.5,0.5,150,0,0},out[5];
    stats.active=0;assert(!othr_speed(in,out) && !memcmp(in,out,sizeof(out)));stats.active=1;
    othr_restore();assert(!othr_speed(in,out) && !memcmp(in,out,sizeof(out)));
    char row[512];assert(othr_frame(5,row,sizeof(row),&why)<0 && !strcmp(why,"not_armed"));
    /* Worst-case widths: every counter at its maximum. */
    othr_armed=1;othr_why="armed_differently";othr_slot=8;othr_rows=3600;othr_motion=1;
    othr_calls=othr_adjusted=othr_outside=othr_stale=othr_frames=othr_partial=UINT64_MAX;othr_grace=OTHR_GRACE_MAX;othr_lag_max=UINT32_MAX;
    for(unsigned i=0;i<OTHR_FITS;i++) {othr_fits[i].samples=UINT64_MAX;othr_fits[i].err=othr_fits[i].base=-1.234567e-300;}
    for(unsigned c=0;c<OTHR_CHANNELS;c++) othr_confirmed[c]=othr_refused[c]=othr_unseen[c]=UINT64_MAX;
    for(unsigned a=0;a<OTHR_ABSTAINS;a++) othr_abstains[a]=UINT64_MAX;
    othr_smooth_samples=othr_motion_paths=othr_motion_refused=othr_motion_samples=UINT64_MAX;
    for(unsigned i=0;i<5;i++) othr_motion_why[i]=UINT64_MAX;othr_smooth_err=othr_motion_err=-1.234567e-300;
    othr_order[0]=othr_order[1]=othr_hook_calls=othr_hook_short=othr_hook_thread=UINT64_MAX;othr_hooked=othr_hook_restored=-1;
    for(unsigned s=0;s<4;s++) othr_slope[s]=-1.234567e-300;
    char status[2048];const int n=othr_status(status,sizeof(status));assert(n>0);
    othr_last.valid=1;othr_last.tick=othr_prev_tick=UINT64_MAX-1;othr_last.s_sim=othr_last.s_shown=othr_last.t_sim=othr_last.t_shown=-1.2345e300;
    othr_last.actual_sim=othr_last.actual_shown=othr_last.mps_sim=othr_last.mps_shown=othr_last.dv=-1.2345e300;
    othr_last.b_sim=othr_last.b_shown=othr_last.tr_sim=othr_last.tr_shown=INT_MIN;
    othr_bank wide;memset(&wide,0,sizeof(wide));wide.s=wide.desired=wide.smoothed=-1.2345e30f;
    const int m=othr_row(&wide,-1,UINT64_MAX-1,row,sizeof(row));assert(m>0 && strstr(row," hud="));
    printf("own throttle worst-case status %d bytes (2048), row %d bytes (512)\n",n,m);
    othr_clear();othr_armed=0;othr_why="-";othr_slot=othr_rows=0;othr_motion=0;othr_pins_checked=0;othr_prev_tick=0;othr_hooked=0;othr_hook_restored=1;
    memset(opred_slots,0,sizeof(opred_slots));opred_armed=0;opred_mode=OPRED_OFF;pilot_slot_count=0;stats.active=0;
}
/* The pins against the actual X4.exe. */
static void test_own_throttle_native_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    assert(fread(sections,sizeof(*sections),nt.FileHeader.NumberOfSections,f)==nt.FileHeader.NumberOfSections);
    static unsigned char bytes[0x800];
    for(unsigned i=0;i<OTHR_PINS;i++) {
        unsigned offset=0,rva=othr_pin_table[i].rva,size=othr_pin_table[i].size;
        assert(size<=sizeof(bytes));
        for(unsigned j=0;j<nt.FileHeader.NumberOfSections;j++)
            if(rva>=sections[j].VirtualAddress && rva+size<=sections[j].VirtualAddress+sections[j].SizeOfRawData)
                offset=sections[j].PointerToRawData+rva-sections[j].VirtualAddress;
        assert(offset && !fseek(f,(long)offset,SEEK_SET) && fread(bytes,1,size,f)==size && hash_ok(bytes,size,othr_pin_table[i].sha));
        bytes[size/2]^=1;assert(!hash_ok(bytes,size,othr_pin_table[i].sha));   /* tampered */
    }
    fclose(f);
    printf("own throttle pins against X4.exe PASS\n");
}
static void test_own_throttle(void) {
    test_othr_arming();
    for(unsigned slot=1;slot<=3;slot++) test_othr_slot(slot);
    printf("own throttle setpoint rules and fits/same-frame key/boost and travel intents (confirmed, refused)/speed/motion path/outside the scope, slots 1 2 3/arming/widths tests PASS\n");
}

/* Own-ship display prediction (lockstep_own_predict.h): features from stock records, the command and
 * response models, observe mode end to end on fake engine memory with two stock slots (the own one
 * with every pending record, the other one's records arriving two ticks late), the correction policy
 * (smooth decay, snap, reset causes, the lead ramp at travel speed, impulses not fitted), present mode
 * through the real render_rng_execute, arming refusals and worst-case widths. The truth ship is
 * independent of the code under test: its own control fold, a key ramp, a throttle integrator and an
 * acceleration-limited response the linear model cannot represent exactly. */
#define TOPR_DELAY 3u
#define TOPR_ARRIVAL 2u   /* slot 2's records reach this client two ticks after their capture */
typedef struct {float yaw,pitch,cursor[3];unsigned char roll_left,roll_right,acc,dec,strafe_left,strafe_right;} topr_controls;
static struct {
    topr_controls k[2];double u[2][OPRED_CHANNELS],v[2][6],kick[2][6],push[2];predict_pose pose[2];
    unsigned char staged[2][PSTK_RING][PSTK_BYTES_MAX];unsigned staged_length[2][PSTK_RING];
    unsigned render_overwrite,render_throw,render_calls,render_seen[2],check_writes;float render_floats[2][32];int render_owned[2];
} topr;
static unsigned char topr_ship[2][0x300],topr_respawn[0x300],topr_zone[2][16];
static __attribute__((aligned(16))) float topr_block[2][2][32];   /* ship, bank */
static __attribute__((aligned(16))) float topr_foreign[32];
static double topr_clamp(double x,double lo,double hi) {return x<lo?lo:x>hi?hi:x;}
/* The truth's own fold of the stock events (not opred_fold). */
static void topr_apply(topr_controls *k,const pstk_record *r) {
    for(unsigned i=0;i<r->count;i++) {
        const pstk_event *e=&r->events[i];float f;memcpy(&f,&e->value,4);
        if(e->kind==PSTK_RANGE && e->id==1) k->yaw=f;
        else if(e->kind==PSTK_RANGE && e->id==2) k->pitch=f;
        else if(e->kind==PSTK_STATE) {
            unsigned char on=e->value!=0;
            if(e->id==0x70) k->roll_left=on;
            else if(e->id==0x71) k->roll_right=on;
            else if(e->id==0xd) k->acc=on;
            else if(e->id==0xe) k->dec=on;
            else if(e->id==3) k->strafe_left=on;
            else if(e->id==2) k->strafe_right=on;
        }
    }
    for(unsigned i=0;i<3;i++) k->cursor[i]=(r->flags&PSTK_FLAG_CURSOR)?r->cursor[i]:0.0f;
}
/* The processed channels: mouse and stick add up (clamped), a roll key ramp, a throttle integrator, strafe. */
static void topr_commands(unsigned s) {
    const topr_controls *k=&topr.k[s];double *u=topr.u[s];
    u[0]=topr_clamp((double)k->cursor[0]+(double)k->yaw,-1,1);
    u[1]=topr_clamp((double)k->cursor[1]+(double)k->pitch,-1,1);
    u[2]+=0.25*((double)k->roll_right-(double)k->roll_left-u[2]);
    u[3]=topr_clamp(u[3]+0.03*((double)k->acc-(double)k->dec),0,1);
    u[4]=(double)k->strafe_right-(double)k->strafe_left;u[5]=u[6]=0;
}
/* The ship: per axis a first-order response, acceleration limited. */
static void topr_respond(unsigned s) {
    static const double gain[6]={0.2,0.2,0.15,0.3,0.3,0.25},acc[6]={1.0,1.0,3.0,0.1,0.08,0.15};
    const double *u=topr.u[s];double target[6]={50*u[4],0,300*u[3],1.2*u[1],1.0*u[0],2.0*u[2]},*v=topr.v[s];
    for(unsigned i=0;i<6;i++) {v[i]+=topr_clamp(gain[i]*(target[i]-v[i]),-acc[i],acc[i])+topr.kick[s][i];topr.kick[s][i]=0;}
    v[2]+=topr.push[s];
    predict_advance(&topr.pose[s],v,PREDICT_DT);
}
/* Scripted input of slot s+1 captured at c: mouse steering every tick, edges on keys and the stick. */
static void topr_script(unsigned s,uint64_t c,pstk_record *r) {
    memset(r,0,sizeof(*r));r->flags=PSTK_FLAG_CURSOR;r->capture16=(uint16_t)c;
    uint64_t t=s?c+37:c;
    r->cursor[0]=(float)(t>=120 && t<1100?0.6*sin((double)t*2.0*3.14159265358979/180.0):0.0);
    r->cursor[1]=(float)(t>=300 && t<1000?((t/50)%2?0.4:-0.4):0.0);
    static const struct {uint64_t at;unsigned kind,id;float value;} plan[]={
        {40,PSTK_STATE,0xd,1},{100,PSTK_STATE,0xd,0},{200,PSTK_STATE,0x71,1},{230,PSTK_STATE,0x71,0},{400,PSTK_STATE,0xe,1},
        {430,PSTK_STATE,0xe,0},{500,PSTK_STATE,0x70,1},{520,PSTK_STATE,0x70,0},{600,PSTK_STATE,2,1},{640,PSTK_STATE,2,0},
        {700,PSTK_RANGE,1,0.5f},{760,PSTK_RANGE,1,0.0f},{800,PSTK_STATE,0xd,1},{830,PSTK_STATE,0xd,0},{900,PSTK_RANGE,2,-0.3f},
        {960,PSTK_RANGE,2,0.0f}};
    for(unsigned i=0;i<sizeof(plan)/sizeof(*plan);i++) {
        if(plan[i].at!=t) continue;
        pstk_event *e=&r->events[r->count++];e->kind=(unsigned char)plan[i].kind;e->id=(uint16_t)plan[i].id;
        if(plan[i].kind==PSTK_RANGE) memcpy(&e->value,&plan[i].value,4);else e->value=plan[i].value!=0.0f;
    }
}
static void topr_write_bank(unsigned s) {
    float rot[3]={(float)topr.u[s][0],(float)topr.u[s][1],(float)topr.u[s][2]},throttle=(float)topr.u[s][3],
        linear[3]={(float)topr.u[s][4],(float)topr.u[s][5],(float)topr.u[s][6]};unsigned char on=1;
    if(!s) {
        memcpy(pnat_base+OPRED_BANK_ROT,rot,12);memcpy(pnat_base+OPRED_BANK_THROTTLE,&throttle,4);
        memcpy(pnat_base+OPRED_BANK_LINEAR,linear,12);pnat_base[OPRED_BANK_FLIGHT]=on;
    } else {
        unsigned char *store=pslots[2].store;
        memcpy(store+test_pslot_store_offset(OPRED_BANK_ROT),rot,12);memcpy(store+test_pslot_store_offset(OPRED_BANK_THROTTLE),&throttle,4);
        memcpy(store+test_pslot_store_offset(OPRED_BANK_LINEAR),linear,12);store[test_pslot_store_offset(OPRED_BANK_FLIGHT)]=on;
    }
}
/* Both banks of the published pose block hold the simulated pose; the words after the PosRot are markers. */
static void topr_write_block(unsigned s) {
    for(unsigned b=0;b<2;b++) {
        float *f=topr_block[s][b];
        for(unsigned i=0;i<3;i++) {f[i]=(float)topr.pose[s].p[i];for(unsigned j=0;j<3;j++) f[4*(i+1)+j]=(float)topr.pose[s].r[3*i+j];}
        f[3]=1.0f;f[7]=f[11]=f[15]=0.0f;
        for(unsigned i=16;i<24;i++) f[i]=100.0f+(float)i+(float)s;   /* scale, velocity: the engine's */
    }
}
static void topr_view(unsigned char *ship,unsigned char *zone,float *b0,float *b1) {
    memcpy(ship+0x70,&zone,8);
    for(unsigned b=0;b<2;b++) {
        unsigned char *vc=ship+PREDICT_VIEW_CACHE+b*PREDICT_VIEW_STRIDE;float *block=b?b1:b0;
        memcpy(vc,&zone,8);memcpy(vc+PREDICT_VIEW_BLOCK,&block,8);
    }
}
/* One admitted tick as the engine runs it: arrivals, path_release (apply and consume), slot 3 (the
 * truth responds, publishes the bank and the pose block), slot 6 (capture, then the predictor). */
static void topr_tick(uint64_t T) {
    if(T>TOPR_ARRIVAL) { /* slot 2's record captured at T-2 arrives */
        uint64_t c=T-TOPR_ARRIVAL;pstk_staged *e=&pstk_ring[1][(c+TOPR_DELAY)%PSTK_RING];
        memset(e,0,sizeof(*e));e->tick=c+TOPR_DELAY;e->length=topr.staged_length[1][c%PSTK_RING];
        memcpy(e->bytes,topr.staged[1][c%PSTK_RING],e->length);e->present=1;
    }
    stats.issued=T;pstk_frame_tick=T;
    for(unsigned s=0;s<2;s++) {
        if(T>TOPR_DELAY) {
            pstk_staged *e=&pstk_ring[s][T%PSTK_RING];pstk_record r;
            assert(e->present && e->tick==T && pstk_decode(e->bytes,e->length,&r) && r.capture16==(uint16_t)(T-TOPR_DELAY));
            topr_apply(&topr.k[s],&r);e->present=0;
        }
        topr_commands(s);topr_respond(s);topr_write_bank(s);topr_write_block(s);
    }
    for(unsigned s=0;s<2;s++) { /* capture at T */
        pstk_record r;unsigned char bytes[PSTK_BYTES_MAX];topr_script(s,T,&r);unsigned n=pstk_encode(&r,bytes);
        if(!s) {
            pstk_local *l=&pstk_locals[T%PSTK_RING];memset(l,0,sizeof(*l));l->capture=T;l->apply=T+TOPR_DELAY;l->length=n;l->used=1;memcpy(l->bytes,bytes,n);
            pstk_staged *e=&pstk_ring[0][(T+TOPR_DELAY)%PSTK_RING];memset(e,0,sizeof(*e));e->tick=T+TOPR_DELAY;e->length=n;memcpy(e->bytes,bytes,n);e->present=1;
        } else {memcpy(topr.staged[1][T%PSTK_RING],bytes,n);topr.staged_length[1][T%PSTK_RING]=n;}
    }
    /* The predictor only reads engine memory in slot 6: ships, pose blocks and the bank stay as they are. */
    static unsigned char ships[sizeof(topr_ship)],respawn[sizeof(topr_respawn)],blocks[sizeof(topr_block)],bank[0xe0];
    if(topr.check_writes) {
        memcpy(ships,topr_ship,sizeof(ships));memcpy(respawn,topr_respawn,sizeof(respawn));memcpy(blocks,topr_block,sizeof(blocks));
        memcpy(bank,pnat_base+OPRED_BANK_ROT,sizeof(bank));
    }
    const char *why;assert(opred_frame(T,&why));
    if(topr.check_writes) assert(!memcmp(ships,topr_ship,sizeof(ships)) && !memcmp(respawn,topr_respawn,sizeof(respawn)) &&
                                 !memcmp(blocks,topr_block,sizeof(blocks)) && !memcmp(bank,pnat_base+OPRED_BANK_ROT,sizeof(bank)));
}
/* Along the ship's forward axis (row 3 of a 16-float block): how far a is ahead of b. */
static double topr_ahead(const float *a,const float *b,const float *block) {
    double d=0;for(unsigned i=0;i<3;i++) d+=((double)a[i]-(double)b[i])*(double)block[12+i];
    return d;
}
/* Slot 10: the original reads what each ship's entry [0] points at. */
static void topr_render(void *self) {
    assert(self==(void*)&topr);++topr.render_calls;
    for(unsigned s=0;s<2;s++) {
        float *at=NULL;memcpy(&at,topr_ship[s]+PREDICT_VIEW_CACHE+PREDICT_VIEW_BLOCK,8);
        topr.render_seen[s]=at==opred_blocks[s];memcpy(topr.render_floats[s],at,sizeof(topr.render_floats[s]));
        topr.render_owned[s]=opred_owns(topr_ship[s]);
    }
    if(topr.render_overwrite) {float *f=topr_foreign;memcpy(topr_ship[0]+PREDICT_VIEW_CACHE+PREDICT_VIEW_BLOCK,&f,8);}
    if(topr.render_throw) RaiseException(0xe0525233,0,0,NULL);
}
static int topr_slot10(void) {
    uint64_t word=0x99;int caught=0;
    __try {render_rng_execute(topr_render,&topr,&word,NULL);}
    __except(EXCEPTION_EXECUTE_HANDLER) {caught=1;}
    assert(word==0x99);return caught;
}
static double topr_display_error(unsigned s) { /* |shown - the pose the truth will have h ticks on| is checked by the module itself */
    const opred_slot *p=&opred_slots[s];return p->pos[PREDICT_DISPLAY].n?opred_mean(&p->pos[PREDICT_DISPLAY]):0.0;
}
static void topr_setup(int mode,int others) {
    memset(&topr,0,sizeof(topr));memset(topr_ship,0,sizeof(topr_ship));memset(topr_respawn,0,sizeof(topr_respawn));
    for(unsigned s=0;s<2;s++) {
        topr_view(topr_ship[s],topr_zone[0],topr_block[s][0],topr_block[s][1]);
        predict_pose start={{1000.0+400.0*s,-500.0,20000.0},{1,0,0,0,1,0,0,0,1}};topr.pose[s]=start;topr_write_block(s);
    }
    memset(pstk_ring,0,sizeof(pstk_ring));memset(pstk_locals,0,sizeof(pstk_locals));
    memset(pilot_slots,0,sizeof(pilot_slots));
    for(unsigned s=0;s<2;s++) {pilot_slots[s].object=topr_ship[s];pilot_slots[s].resolved=1;pilot_slots[s].saved=0xa001+s;}
    pilot_slot_count=2;pilot_source=PILOT_SOURCE_WIRE;pilot_delay=TOPR_DELAY;pilot_armed=1;pilot_start=4;pilot_stop=100000;
    pnat_armed=1;pnat_mode=PNAT_MODE_STOCK;pstk_capturing=1;pstk_capture_slot=1;pstk_slot_mask=3;pstk_frame_tick=0;
    memset(&pslots[2],0,sizeof(pslots[2]));pslots[2].seated=1;pnat_base=image;
    *(unsigned*)(image+PREDICT_VIEW_INDEX)=0;
    stats.slot=1;stats.active=1;stats.issued=0;
    memset(opred_slots,0,sizeof(opred_slots));opred_armed=0;
    const char *why;assert(opred_arm(mode,others,150,10,&why) && opred_horizon==TOPR_DELAY && opred_slots[0].own && opred_slots[0].enabled);
    assert(opred_slots[1].enabled==(unsigned)others && !opred_slots[1].own);
}
static void topr_teardown(void) {
    memset(opred_slots,0,sizeof(opred_slots));opred_armed=0;opred_mode=OPRED_OFF;opred_others=0;opred_cap=opred_tau_ms=opred_horizon=0;
    opred_frames=opred_last_tick=0;opred_failures=0;opred_stage="off";opred_open=0;opred_display_offset=NULL;
    opred_present_calls=opred_present_applied=opred_present_restored=opred_present_lost=opred_present_refused=0;
    opred_present_stale=opred_present_previous=opred_present_thread=0;
    memset(pilot_slots,0,sizeof(pilot_slots));pilot_slot_count=pilot_source=pilot_delay=0;pilot_armed=0;pilot_start=pilot_stop=0;
    pnat_armed=0;pnat_mode=0;pstk_capturing=0;pstk_capture_slot=0;pstk_slot_mask=0;pstk_frame_tick=0;
    memset(pstk_ring,0,sizeof(pstk_ring));memset(pstk_locals,0,sizeof(pstk_locals));memset(&pslots[2],0,sizeof(pslots[2]));
}
static void test_opred_features(void) {
    /* Store offsets: the slots' own walk. */
    static const unsigned addresses[]={OPRED_BANK_ROT,OPRED_BANK_THROTTLE,OPRED_BANK_LINEAR,OPRED_BANK_FLIGHT};
    for(unsigned i=0;i<4;i++) assert(opred_store_offset(addresses[i],1)==(int)test_pslot_store_offset(addresses[i]));
    assert(opred_store_offset(0x1000,4)==-1 && opred_store_offset(0x2f4b76c,8)==-1); /* not a region / crosses its end */
    /* Fold: ranges and states held, the wheel an impulse, the cursor per record, actions and SPACE counted. */
    opred_held h;memset(&h,0,sizeof(h));pstk_record r;memset(&r,0,sizeof(r));uint64_t actions=0,edges=0;double phi[OPRED_FEATURES];
    float half=0.5f,wheel=2.0f;r.flags=PSTK_FLAG_CURSOR;r.cursor[0]=0.25f;r.cursor[2]=-1.0f;r.count=5;
    r.events[0]=(pstk_event){PSTK_RANGE,2,0};memcpy(&r.events[0].value,&half,4);
    r.events[1]=(pstk_event){PSTK_STATE,0x71,1};r.events[2]=(pstk_event){PSTK_RANGE,0xa,0};memcpy(&r.events[2].value,&wheel,4);
    r.events[3]=(pstk_event){PSTK_ACTION,0x7b,0};r.events[4]=(pstk_event){PSTK_SPACE,8,1};
    opred_fold(&h,&r,&actions,&edges);opred_features(&h,phi);
    int pitch=opred_index(pstk_replay_ranges,OPRED_RANGES,2),wheel_at=opred_index(pstk_replay_ranges,OPRED_RANGES,0xa),roll=opred_index(pstk_replay_states,OPRED_STATES,0x71);
    assert(pitch>=0 && wheel_at>=0 && roll>=0 && actions==1 && edges==1);
    assert(phi[0]==0.25 && phi[1]==0 && phi[2]==-1.0 && phi[OPRED_CURSOR+pitch]==0.5 && phi[OPRED_CURSOR+wheel_at]==2.0 && phi[OPRED_CURSOR+OPRED_RANGES+roll]==1.0);
    memset(&r,0,sizeof(r));opred_fold(&h,&r,NULL,NULL);opred_features(&h,phi);   /* the next record: no events, no cursor */
    assert(phi[0]==0 && phi[2]==0 && phi[OPRED_CURSOR+pitch]==0.5 && phi[OPRED_CURSOR+wheel_at]==0 && phi[OPRED_CURSOR+OPRED_RANGES+roll]==1.0);
    float nan=NAN;r.count=1;r.events[0]=(pstk_event){PSTK_RANGE,2,0};memcpy(&r.events[0].value,&nan,4);
    opred_fold(&h,&r,NULL,NULL);assert(h.range[pitch]==0.5f); /* a non-finite value is not taken */
    /* Records: the applied one stays readable once consumed; a pending own one comes from the capture ring. */
    memset(pstk_ring,0,sizeof(pstk_ring));memset(pstk_locals,0,sizeof(pstk_locals));pilot_delay=3;
    pstk_record w,got;topr_script(0,40,&w);unsigned char bytes[PSTK_BYTES_MAX];unsigned n=pstk_encode(&w,bytes);
    pstk_staged *e=&pstk_ring[0][43%PSTK_RING];e->tick=43;e->length=n;memcpy(e->bytes,bytes,n);e->present=0;
    assert(opred_record_applied(1,43,&got) && got.count==1 && !opred_record_applied(1,44,&got) && !opred_record_pending(1,0,43,&got));
    e->present=1;assert(opred_record_pending(1,0,43,&got) && !opred_record_pending(2,0,43,&got));
    pstk_local *l=&pstk_locals[40%PSTK_RING];l->capture=40;l->apply=43;l->used=1;l->length=n;memcpy(l->bytes,bytes,n);
    assert(opred_record_pending(1,1,43,&got) && !opred_record_pending(1,1,44,&got));
    l->apply=44;assert(!opred_record_pending(1,1,43,&got));
    memset(pstk_ring,0,sizeof(pstk_ring));memset(pstk_locals,0,sizeof(pstk_locals));pilot_delay=0;
}
static void test_opred_models(void) {
    /* Command model: a static map, a lag and an integrator; ready only when better than holding. */
    opred_command m;opred_command_reset(&m);double u[OPRED_CHANNELS]={0},prev[OPRED_CHANNELS],phi[OPRED_FEATURES],next[OPRED_CHANNELS];
    for(unsigned c=0;c<OPRED_CHANNELS;c++) assert(!opred_command_ready(&m,c));
    uint32_t seed=7;
    for(unsigned t=0;t<3000;t++) {
        memset(phi,0,sizeof(phi));seed=seed*1664525u+1013904223u;
        phi[0]=sin(t*0.07);phi[OPRED_CURSOR]=(seed>>28)&1?0.5:0.0;phi[OPRED_CURSOR+OPRED_RANGES]=(t/40)%3==1;phi[OPRED_CURSOR+OPRED_RANGES+1]=(t/40)%3==2;
        memcpy(prev,u,sizeof(prev));
        u[0]=phi[0]+phi[OPRED_CURSOR];u[1]=0.8*u[1]+0.2*phi[OPRED_CURSOR+OPRED_RANGES];
        u[3]=topr_clamp(u[3]+0.02*(phi[OPRED_CURSOR+OPRED_RANGES]-phi[OPRED_CURSOR+OPRED_RANGES+1]),-5,5);
        opred_command_learn(&m,prev,phi,u);
    }
    assert(opred_command_ready(&m,0) && opred_command_ready(&m,1) && opred_command_ready(&m,3) && !opred_command_ready(&m,2));
    memset(phi,0,sizeof(phi));phi[0]=0.3;phi[OPRED_CURSOR]=0.5;phi[OPRED_CURSOR+OPRED_RANGES]=1;
    double from[OPRED_CHANNELS]={0.1,0.4,7,0.5,0,0,0};opred_command_step(&m,from,phi,next);
    assert(fabs(next[0]-0.8)<1e-3 && fabs(next[1]-0.52)<1e-3 && next[2]==7 && fabs(next[3]-0.52)<1e-3);
    /* Response model: a first-order response is fitted; holding wins on an axis it cannot explain. */
    opred_axis a;opred_axis_reset(&a);double v=0,uu[OPRED_CHANNELS]={0},up[OPRED_CHANNELS]={0};
    for(unsigned t=0;t<2000;t++) {
        memcpy(up,uu,sizeof(up));uu[0]=(t/60)%2?1.0:-0.5;double before=v;v+=0.3*(2.0*uu[0]-v);
        opred_axis_learn(&a,3,before,up,uu,v);
    }
    assert(opred_axis_ready(&a) && a.refused<=5 && a.saturated>0 && fabs(a.w[0]-0.7)<1e-4 && fabs(a.w[1]-0.6)<1e-4 && a.envelope[0]>0.4 && a.envelope[1]>0.4);
    const opred_axis fitted=a;double vs[OPRED_AXES]={0,0,0,0.5,0,0};uu[0]=1.0;opred_axis axes[OPRED_AXES];
    for(unsigned i=0;i<OPRED_AXES;i++) opred_axis_reset(&axes[i]);
    axes[3]=a;opred_axes_step(axes,1,vs,uu,NULL);assert(fabs(vs[3]-(0.7*0.5+0.6))<1e-4);
    vs[3]=0.5;opred_axes_step(axes,0,vs,uu,NULL);assert(vs[3]==0.5);   /* flight control off: constant velocity */
    /* An impulse is refused once the model is in use; its fit stays. */
    double w0=a.w[0];uint64_t refused=a.refused;opred_axis_learn(&a,3,1.0,uu,uu,9.0);assert(a.refused==refused+1 && a.w[0]==w0);
    double envelope=a.envelope[0];assert(envelope<1.0);
    opred_axis_learn(&a,3,1.0,uu,uu,1.75);assert(a.refused==refused+2 && a.w[0]==w0);   /* most of the change unexplained */
    up[0]=-0.5;opred_axis_learn(&a,3,1.0,up,uu,1.6);assert(a.refused==refused+2 && a.w[0]!=w0); /* the controls changed: fitted */
    up[0]=-0.5;opred_axis_learn(&a,3,1.0,up,uu,40.0);assert(a.envelope[0]<=1.5*envelope+1e-12); /* an impulse does not open the envelope */
    /* The envelope bounds a predicted change. */
    a=fitted;a.envelope[0]=a.envelope[1]=0.01;axes[3]=a;vs[3]=0;uu[0]=1.0;opred_axes_step(axes,1,vs,uu,NULL);assert(fabs(vs[3]-0.01)<1e-12);
    /* Noise only: the model is never ready (no better than constant velocity). */
    opred_axis_reset(&a);seed=3;
    for(unsigned t=0;t<2000;t++) {seed=seed*1664525u+1013904223u;double noisy=((seed>>8)&0xffff)/65536.0-0.5;opred_axis_learn(&a,0,0,uu,uu,noisy);}
    assert(!opred_axis_ready(&a));
    /* RLS: a degenerate update leaves P alone. */
    double P[4]={1,0,0,1},x[2]={NAN,0},k[2];assert(!opred_rls_gain(P,2,x,k) && P[0]==1);
    /* Smoothing: decay, then a snap beyond the bounds. */
    predict_offset c;memset(&c,0,sizeof(c));predict_pose p0={{0,0,0},{1,0,0,0,1,0,0,0,1}},p1=p0;p1.p[0]=4;
    double lambda=exp(-1000.0*PREDICT_DT/150.0);
    assert(!opred_smooth(&c,&p1,&p0,lambda,20,0,0) && fabs(c.p[0]-4*lambda)<1e-12);
    for(unsigned i=0;i<10;i++) assert(!opred_smooth(&c,&p0,&p0,lambda,20,0,0));
    assert(fabs(c.p[0]-4*pow(lambda,11))<1e-12);
    p1.p[0]=25;assert(opred_smooth(&c,&p1,&p0,lambda,20,0,0) && c.p[0]==0);
    p1.p[0]=25;assert(!opred_smooth(&c,&p1,&p0,lambda,40,0,0));memset(&c,0,sizeof(c));
    p1.p[0]=0;predict_exp((const double[3]){0,0.5,0},p1.r);assert(opred_smooth(&c,&p1,&p0,1.0,20,0,0));
    /* The band: a jump inside it is shown at once, only the excess is smoothed. */
    memset(&c,0,sizeof(c));p1=p0;p1.p[1]=0.15;assert(!opred_smooth(&c,&p1,&p0,lambda,20,0.2,0) && c.p[1]==0);
    p1.p[1]=0.5;assert(!opred_smooth(&c,&p1,&p0,lambda,20,0.2,0) && fabs(c.p[1]-0.3*lambda)<1e-12);
    memset(&c,0,sizeof(c));p1=p0;predict_exp((const double[3]){0.002,0,0},p1.r);assert(!opred_smooth(&c,&p1,&p0,lambda,20,0,0.003) && c.r[0]==0);
}
/* Observe on fake engine memory: both slots, then the correction policy and the reset causes. */
static void test_opred_observe(void) {
    unsigned saved_view=*(unsigned*)(image+PREDICT_VIEW_INDEX),saved_slot=stats.slot;uint64_t saved_issued=stats.issued;int saved_active=stats.active;
    unsigned char saved_bank[0xe0];memcpy(saved_bank,image+0x2f4b750,sizeof(saved_bank));unsigned char *saved_pnat=pnat_base;
    topr_setup(OPRED_OBSERVE,1);
    topr.check_writes=1; /* observe never writes engine memory (checked around every frame) */
    for(uint64_t T=1;T<=1200;T++) topr_tick(T);
    const opred_slot *own=&opred_slots[0],*other=&opred_slots[1];
    printf("own predict slot1 angle model %.4g dead %.4g model %.4g/%.4g dead %.4g/%.4g none %.4g display %.4g lead %.4g/%.4g ready=%04x checked=%llu missing=%llu\n",
        opred_mean(&own->ang[PREDICT_MODEL]),opred_mean(&own->ang[PREDICT_DEAD]),opred_mean(&own->pos[PREDICT_MODEL]),own->pos[PREDICT_MODEL].max,opred_mean(&own->pos[PREDICT_DEAD]),own->pos[PREDICT_DEAD].max,
        opred_mean(&own->pos[PREDICT_NONE]),topr_display_error(0),opred_mean(&own->lead_pos),own->lead_pos.max,opred_ready_mask(own),
        (unsigned long long)own->checked,(unsigned long long)own->missing);
    printf("own predict slot2 model %.4g/%.4g dead %.4g/%.4g none %.4g display %.4g angle model %.4g dead %.4g ready=%04x missing=%llu\n",
        opred_mean(&other->pos[PREDICT_MODEL]),other->pos[PREDICT_MODEL].max,opred_mean(&other->pos[PREDICT_DEAD]),other->pos[PREDICT_DEAD].max,
        opred_mean(&other->pos[PREDICT_NONE]),topr_display_error(1),opred_mean(&other->ang[PREDICT_MODEL]),opred_mean(&other->ang[PREDICT_DEAD]),
        opred_ready_mask(other),(unsigned long long)other->missing);
    fflush(stdout);
    /* The own ship: every pending record known, the model well ahead of dead reckoning and of no prediction. */
    assert(own->checked>=1190 && own->missing<=2 && own->samples==1200 && !own->snaps); /* two records before the first capture */
    for(unsigned r=0;r<OPRED_RESETS;r++) assert(!own->resets[r]);
    assert(opred_mean(&own->pos[PREDICT_MODEL])*4<opred_mean(&own->pos[PREDICT_DEAD]));
    assert(opred_mean(&own->ang[PREDICT_MODEL])*2.5<opred_mean(&own->ang[PREDICT_DEAD]));
    assert(own->pos[PREDICT_NONE].max>0.8*300.0*TOPR_DELAY*PREDICT_DT && own->pos[PREDICT_MODEL].max<1.0 && own->ang[PREDICT_MODEL].max<0.02);
    assert(own->pos[PREDICT_DISPLAY].n>1000 && topr_display_error(0)<opred_mean(&own->pos[PREDICT_DEAD]));
    assert((opred_ready_mask(own)&0x0b)==0x0b && (opred_ready_mask(own)&(1u<<(8+2))) && (opred_ready_mask(own)&(1u<<(8+4))));
    assert(fabs(own->h_eff-3.0)<1e-12 && own->lead_pos.max<40.0);
    /* The other ship: two of three records missing every tick, still better than dead reckoning. */
    assert(other->missing>=2*1100 && other->checked>=1190);
    assert(opred_mean(&other->pos[PREDICT_MODEL])<opred_mean(&other->pos[PREDICT_DEAD]));
    assert(opred_mean(&other->ang[PREDICT_MODEL])<opred_mean(&other->ang[PREDICT_DEAD]));
    char text[8192];
    assert(opred_row(text,sizeof(text)) && !strncmp(text,"tick=1200 h=3 s1=3/",19) && strstr(text," s2=3/"));
    assert(opred_status(text,sizeof(text)) && strstr(text,"mode=observe armed=1 horizon=3 cap=10 delay=3 tau_ms=150 others=1") &&
           strstr(text," s1: own=1 samples=1200") && strstr(text,"resets=read:0,parent:0,jump:0,ship:0,dead:0") && strstr(text," lead="));
    /* A ram: a lateral kick the controls did not cause. Not fitted, no snap; the shown pose converges. */
    uint64_t refused=own->axis[0].refused,snaps=own->snaps;topr.kick[0][0]=1.5/(TOPR_DELAY*PREDICT_DT);topr_tick(1201); /* the path moves 1.5 m */
    double first=sqrt(predict_dot3(own->correction.p,own->correction.p));topr_tick(1202);topr_tick(1203);
    double later=sqrt(predict_dot3(own->correction.p,own->correction.p));
    assert(own->axis[0].refused==refused+1 && own->snaps==snaps && first>0.3 && later<first);
    for(uint64_t T=1204;T<1240;T++) topr_tick(T);
    assert(sqrt(predict_dot3(own->correction.p,own->correction.p))<0.05*first);
    /* An explosion: a jump of the path beyond 20 m plus a step's motion snaps (counted), the ship is not reset. */
    topr.kick[0][1]=50.0/(TOPR_DELAY*PREDICT_DT);topr_tick(1240); /* the path moves 50 m */
    assert(own->snaps==snaps+1 && !own->resets[OPRED_RESET_JUMP]);
    for(uint64_t T=1241;T<1250;T++) topr_tick(T);
    assert(own->snaps==snaps+1); /* the drift is predicted from then on */
    topr.kick[0][1]=-topr.v[0][1];for(uint64_t T=1250;T<1260;T++) topr_tick(T);
    /* Travel speed: the lead ramps down by half a step per step, and back below the resume speed. */
    topr.push[0]=40.0;uint64_t T=1260;
    while(opred_slots[0].h_eff>0 && T<1400) {double h=own->h_eff;topr_tick(T++);assert(own->h_eff==h || fabs(h-own->h_eff-OPRED_RAMP)<1e-12 || own->h_eff==0);}
    assert(own->suspends==1 && own->suspended && own->h_eff==0 && !own->resets[OPRED_RESET_JUMP]);
    topr_tick(T++);assert(fabs(own->lead_now_pos-sqrt(predict_dot3(own->correction.p,own->correction.p)))<1e-3); /* shown = S(T) plus what is left of C */
    topr.push[0]=-60.0;
    while(sqrt(predict_dot3(topr.v[0],topr.v[0]))>200.0 && T<1500) topr_tick(T++);
    topr.push[0]=0;for(unsigned i=0;i<8;i++) topr_tick(T++);
    assert(!own->suspended && own->h_eff==3.0);
    /* Reset causes: a teleport, a parent change (docking, a zone change), an unreadable pose, a new ship, a dead slot. */
    topr.pose[0].p[0]+=5000.0;topr_tick(T++);assert(own->resets[OPRED_RESET_JUMP]==1 && own->h_eff==0 && !own->have_end);
    for(unsigned i=0;i<6;i++) topr_tick(T++);
    assert(own->h_eff==3.0 && own->resets[OPRED_RESET_JUMP]==1);
    topr_view(topr_ship[0],topr_zone[1],topr_block[0][0],topr_block[0][1]);topr_tick(T++);assert(own->resets[OPRED_RESET_PARENT]==1);
    void *none=NULL;memcpy(topr_ship[0]+PREDICT_VIEW_CACHE+PREDICT_VIEW_BLOCK,&none,8);topr_tick(T++);
    assert(own->resets[OPRED_RESET_READ]==1 && !own->ready);
    topr_view(topr_ship[0],topr_zone[1],topr_block[0][0],topr_block[0][1]);topr_tick(T++);assert(own->ready && own->resets[OPRED_RESET_PARENT]==1);
    topr_view(topr_respawn,topr_zone[1],topr_block[0][0],topr_block[0][1]);pilot_slots[0].object=topr_respawn;topr_tick(T++);
    assert(own->resets[OPRED_RESET_SHIP]==1 && own->object==topr_respawn);
    pilot_slots[0].dead=1;pilot_slots[0].object=NULL;pilot_slots[0].resolved=0;topr_tick(T++);topr_tick(T++);
    assert(own->resets[OPRED_RESET_DEAD]==1 && !own->ready && !own->object);
    assert(opred_row(text,sizeof(text)) && strstr(text," s1=-"));
    /* Ticks only advance; a frame outside its tick is refused (never a stop). */
    const char *why;assert(!opred_frame(T-1,&why) && !strcmp(why,"tick"));
    pstk_frame_tick=T+5;assert(!opred_frame(T,&why) && !strcmp(why,"tick"));
    assert(opred_check_live()); /* observe never stops the gate */
    topr_teardown();
    pnat_base=saved_pnat;memcpy(image+0x2f4b750,saved_bank,sizeof(saved_bank));*(unsigned*)(image+PREDICT_VIEW_INDEX)=saved_view;
    stats.slot=saved_slot;stats.issued=saved_issued;stats.active=saved_active;
}
/* Present: the slot-10 pointer swap through the render RNG split's real wrapper. */
static double topr_offset_value;
static double topr_offset(void) {return topr_offset_value;}
static void test_opred_present(void) {
    unsigned saved_view=*(unsigned*)(image+PREDICT_VIEW_INDEX),saved_slot=stats.slot;uint64_t saved_issued=stats.issued;int saved_active=stats.active;
    int saved_mode=render_rng_mode,saved_installed=render_rng_installed;DWORD saved_ui=ui_thread;
    unsigned char saved_bank[0xe0];memcpy(saved_bank,image+0x2f4b750,sizeof(saved_bank));unsigned char *saved_pnat=pnat_base;
    /* Refusals first: no split, observe split, pins; then armed. */
    render_rng_installed=0;predict_pins_checked=1;
    {   const char *why;stats.active=1;stats.issued=0;stats.slot=1;pilot_slot_count=2;pilot_source=PILOT_SOURCE_WIRE;pilot_delay=3;pilot_armed=1;
        pnat_armed=1;pnat_mode=PNAT_MODE_STOCK;pstk_capturing=1;pstk_capture_slot=1;pstk_slot_mask=3;
        assert(!opred_arm(OPRED_PRESENT,1,150,10,&why) && !strcmp(why,"present_without_render_split"));
        render_rng_installed=1;render_rng_mode=RENDER_RNG_OBSERVE;assert(!opred_arm(OPRED_PRESENT,1,150,10,&why));
        render_rng_mode=RENDER_RNG_SPLIT;predict_pins_checked=0;assert(!opred_arm(OPRED_PRESENT,1,150,10,&why) && !strcmp(why,"present_pins"));
        predict_pins_checked=1;
    }
    render_rng_state=1;render_rng_frame=UINT64_MAX;ui_thread=GetCurrentThreadId();
    topr_setup(OPRED_PRESENT,1);
    uint64_t T=1;unsigned shown=0;
    for(;T<=300;T++) {
        topr_tick(T);
        float sim[2][32];memcpy(sim[0],topr_block[0][0],sizeof(sim[0]));memcpy(sim[1],topr_block[1][0],sizeof(sim[1])); /* bank 0 */
        uint64_t applied=opred_present_applied;
        assert(!topr_slot10());
        for(unsigned s=0;s<2;s++) {
            float *at=NULL;memcpy(&at,topr_ship[s]+PREDICT_VIEW_CACHE+PREDICT_VIEW_BLOCK,8);
            assert(at==topr_block[s][0] && !memcmp(topr_block[s][0],sim[s],sizeof(sim[s])) && !opred_owns(topr_ship[s])); /* back after slot 10 */
            if(topr.render_seen[s]) {
                const float *f=topr.render_floats[s];
                for(unsigned r=0;r<4;r++) for(unsigned j=0;j<3;j++) assert(f[4*r+j]==opred_slots[s].shown[3*r+j]);
                assert(f[3]==1.0f && f[16]==116.0f+(float)s && f[23]==123.0f+(float)s && topr.render_owned[s]); /* the engine's words kept */
                if(!s && T>=200) assert(topr_ahead(f,sim[0],sim[0])>2.0*200.0*PREDICT_DT); /* flying forward: the own ship is drawn ahead */
            } else assert(!memcmp(topr.render_floats[s],sim[s],sizeof(sim[s])));
        }
        if(opred_present_applied>applied) ++shown;
    }
    assert(shown>=298 && opred_present_restored==opred_present_applied && !opred_present_lost && !opred_failures && opred_check_live());
    assert(opred_present_applied>=2*297);
    /* A render-only frame: the same tick again, the block still S(T): drawn again; with a display offset the
     * shown pose moves on by that fraction of a step. */
    uint64_t applied=opred_present_applied;topr_offset_value=0.5;opred_display_offset=topr_offset;
    assert(!topr_slot10() && opred_present_applied==applied+2);
    float expected[12];assert(opred_display_pose(1,0.5,expected));
    for(unsigned r=0;r<4;r++) for(unsigned j=0;j<3;j++) assert(topr.render_floats[0][4*r+j]==expected[3*r+j]);
    {float now[16]={0};for(unsigned i=0;i<12;i++) now[i+i/3]=opred_slots[0].shown[i];
     float later[16]={0};for(unsigned i=0;i<12;i++) later[i+i/3]=expected[i];
     assert(topr_ahead(later,now,now)>0.4*300.0*PREDICT_DT);}  /* half a step at about 300 m/s */
    opred_display_offset=NULL;
    /* The other bank holds the previous step (render-only frame after the publish): accepted, counted. */
    uint64_t previous=opred_present_previous;
    for(unsigned i=0;i<12;i++) topr_block[0][0][i+i/3]=opred_slots[0].prev_floats[i];
    assert(!topr_slot10() && opred_present_previous==previous+1);
    /* The engine replaces the entry during slot 10: its pointer stands. */
    topr_tick(T++);topr.render_overwrite=1;assert(!topr_slot10());topr.render_overwrite=0;
    float *at=NULL;memcpy(&at,topr_ship[0]+PREDICT_VIEW_CACHE+PREDICT_VIEW_BLOCK,8);
    assert(opred_present_lost==1 && at==topr_foreign && opred_check_live());
    topr_view(topr_ship[0],topr_zone[0],topr_block[0][0],topr_block[0][1]);
    /* An exception in slot 10: both entries back. */
    topr_tick(T++);uint64_t restored=opred_present_restored;topr.render_throw=1;assert(topr_slot10());topr.render_throw=0;
    assert(opred_present_restored==restored+2 && !opred_open);
    memcpy(&at,topr_ship[0]+PREDICT_VIEW_CACHE+PREDICT_VIEW_BLOCK,8);assert(at==topr_block[0][0]);
    render_rng_failures=0;render_rng_stage="idle";
    /* The block changed after slot 6 (not S(T), not the previous step): that ship is drawn simulated. */
    topr_tick(T++);topr_block[0][0][0]+=1.0f;uint64_t refused=opred_present_refused;
    assert(!topr_slot10() && opred_present_refused==refused+1 && !topr.render_seen[0] && topr.render_seen[1]);
    /* Another thread, a stale tick: nothing swapped. */
    topr_tick(T++);ui_thread=saved_ui==GetCurrentThreadId()?saved_ui+1:saved_ui;
    uint64_t other=opred_present_thread;assert(!topr_slot10() && opred_present_thread==other+1 && !topr.render_seen[0]);
    ui_thread=GetCurrentThreadId();stats.issued=T+3;uint64_t stale=opred_present_stale;
    assert(!topr_slot10() && opred_present_stale==stale+2);stats.issued=T-1;
    /* Swap mode: the same swap, the block's own pose drawn (the census separation run). */
    stats.issued=T-1;opred_mode=OPRED_SWAP;topr_tick(T++);
    {   float sim0[32];memcpy(sim0,topr_block[0][0],sizeof(sim0));uint64_t before=opred_present_applied;
        assert(!topr_slot10() && opred_present_applied==before+2 && topr.render_seen[0] && topr.render_seen[1]);
        assert(!memcmp(topr.render_floats[0],sim0,sizeof(sim0)) && opred_check_live());
        char text[8192];assert(opred_status(text,sizeof(text)) && !strncmp(text,"mode=swap ",10));
    }
    opred_mode=OPRED_PRESENT;
    /* A nested entry fails: the gate stops with 184 and nothing presents after it. */
    opred_open=1;assert(!opred_present_enter() && !opred_check_live());opred_open=0;
    uint32_t saved_reason=stats.reason;stats.restored=1;render_rng_installed=0;finish(OPRED_STOP_REASON);
    assert(stats.reason==OPRED_STOP_REASON && !stats.active && !opred_present_enter());
    stats.reason=saved_reason;
    topr_teardown();
    render_rng_mode=saved_mode;render_rng_installed=saved_installed;render_rng_calls=render_rng_steps=render_rng_unknown=render_rng_ids=0;
    render_rng_frame=UINT64_MAX;render_rng_stage="idle";predict_pins_checked=0;ui_thread=saved_ui;
    pnat_base=saved_pnat;memcpy(image+0x2f4b750,saved_bank,sizeof(saved_bank));*(unsigned*)(image+PREDICT_VIEW_INDEX)=saved_view;
    stats.slot=saved_slot;stats.issued=saved_issued;stats.active=saved_active;
}
static void test_opred_arming(void) {
    unsigned saved_slot=stats.slot;uint64_t saved_issued=stats.issued;int saved_active=stats.active;const char *why;
    topr_setup(OPRED_OBSERVE,1);
    assert(opred_arm(OPRED_OBSERVE,1,150,10,&why));                   /* the identical rerun after the UI reload */
    assert(!opred_arm(OPRED_OBSERVE,0,150,10,&why) && !strcmp(why,"armed_differently"));
    topr_teardown();
    stats.active=1;stats.slot=1;
    #define TOPR_REFUSE(setup,expected) do {topr_setup(OPRED_OBSERVE,1);opred_armed=0;setup;assert(!opred_arm(OPRED_OBSERVE,1,150,10,&why) && !strcmp(why,expected));topr_teardown();} while(0)
    TOPR_REFUSE(stats.issued=5,"after_first_tick");
    TOPR_REFUSE(pilot_source=PILOT_SOURCE_SCRIPT,"needs_wire_pilot");
    TOPR_REFUSE(pnat_mode=PNAT_MODE_NATIVE,"needs_stock_capture");
    TOPR_REFUSE(pstk_capturing=0,"needs_stock_capture");
    TOPR_REFUSE(pstk_capture_slot=2,"own_slot_not_stock");
    TOPR_REFUSE(pstk_slot_mask=2,"own_slot_not_stock");
    TOPR_REFUSE(predict_armed=1,"pilot_predict_armed");predict_armed=0;
    #undef TOPR_REFUSE
    topr_setup(OPRED_OBSERVE,1);opred_armed=0;
    assert(!opred_arm(OPRED_OFF,1,150,10,&why) && !strcmp(why,"mode"));
    assert(!opred_arm(OPRED_OBSERVE,1,OPRED_TAU_MAX_MS+1,10,&why) && !strcmp(why,"range"));
    assert(!opred_arm(OPRED_OBSERVE,1,150,0,&why) && !opred_arm(OPRED_OBSERVE,1,150,31,&why) && !opred_arm(OPRED_OBSERVE,2,150,10,&why));
    /* The cap shortens the horizon below the delay; others off predicts the own slot only. */
    pilot_delay=12;assert(opred_arm(OPRED_OBSERVE,0,0,10,&why) && opred_horizon==10 && opred_slots[0].enabled && !opred_slots[1].enabled);
    /* The arm the Lua surface uses joins the render frames: display offset and ownership both set. */
    assert(opred_display_offset==NULL && rdisp_owns==NULL);
    assert(opred_arm_joined(OPRED_OBSERVE,0,0,10,&why) && opred_display_offset==rdisp_display_offset && rdisp_owns==opred_owns);
    opred_display_offset=NULL;rdisp_owns=NULL;
    /* An adaptive plan's delay replaces the armed one: the horizon follows it, under the same cap. */
    pilot_delay=6;opred_delay_adopted();assert(opred_horizon==6);
    pilot_delay=12;opred_delay_adopted();assert(opred_horizon==10);
    topr_teardown();
    pilot_delay=6;opred_delay_adopted();assert(opred_horizon==0); /* not armed: nothing to follow */
    pilot_delay=0;
    stats.slot=saved_slot;stats.issued=saved_issued;stats.active=saved_active;
}
/* Worst-width status and row: four slots, every counter and statistic extreme. */
static void test_opred_widths(void) {
    unsigned saved_count=pilot_slot_count;char status[8192],row[512];
    pilot_slot_count=PILOT_SLOTS;opred_armed=1;opred_mode=OPRED_PRESENT;opred_horizon=opred_cap=OPRED_HORIZON_MAX;opred_tau_ms=OPRED_TAU_MAX_MS;
    pilot_delay=PILOT_DELAY_MAX;opred_others=1;opred_stage="present_restore";opred_frames=opred_last_tick=UINT64_MAX;opred_failures=INT64_MIN;
    opred_present_calls=opred_present_applied=opred_present_restored=opred_present_lost=opred_present_refused=UINT64_MAX;
    opred_present_stale=opred_present_previous=opred_present_thread=UINT64_MAX;
    for(unsigned i=0;i<PILOT_SLOTS;i++) {
        opred_slot *p=&opred_slots[i];memset(p,0xff,sizeof(*p));p->enabled=p->ready=p->checked_now=1;
        p->h_eff=-1.2345678e-300;p->last_pos=p->last_ang=p->lead_now_pos=p->lead_now_ang=-1.2345678e-300;
        for(unsigned k=0;k<PREDICT_KINDS;k++) {p->pos[k].n=p->ang[k].n=1;p->pos[k].sum=p->ang[k].sum=-1.2345678e300;
            p->pos[k].sq=p->ang[k].sq=1.2345678e300;p->pos[k].max=p->ang[k].max=-1.2345678e-300;}
        p->lead_pos=p->pos[0];p->lead_ang=p->ang[0];
        for(unsigned a=0;a<OPRED_AXES;a++) {p->axis[a].samples=UINT64_MAX;p->axis[a].err_model=0;p->axis[a].err_hold=1;}
        p->cmd.samples=UINT64_MAX;for(unsigned c=0;c<OPRED_CHANNELS;c++) {p->cmd.err_model[c]=0;p->cmd.err_hold[c]=1;}
    }
    int n=opred_status(status,sizeof(status)),m=opred_row(row,sizeof(row));
    assert(n>0 && n<(int)sizeof(status) && m>0 && m<(int)sizeof(row) && strstr(status,"ready=3f7f"));
    printf("own predict worst-case status %d bytes, row %d bytes\n",n,m);
    memset(opred_slots,0,sizeof(opred_slots));opred_armed=0;opred_mode=OPRED_OFF;opred_horizon=opred_cap=opred_tau_ms=0;opred_others=0;
    opred_stage="off";opred_frames=opred_last_tick=0;opred_failures=0;pilot_delay=0;
    opred_present_calls=opred_present_applied=opred_present_restored=opred_present_lost=opred_present_refused=0;
    opred_present_stale=opred_present_previous=opred_present_thread=0;pilot_slot_count=saved_count;
}
static void test_own_predict(void) {
    test_opred_features();test_opred_models();test_opred_observe();test_opred_present();test_opred_arming();test_opred_widths();
    printf("own predict features/command and response models/observe end to end/correction policy/reset causes/present swap/arming/widths tests PASS\n");
}

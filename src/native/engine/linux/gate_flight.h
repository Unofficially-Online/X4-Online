/* The battle fixture's steering on the Linux X4 9.00 executable: the Linux twin of lockstep_flight.h's keyed fixture path
 * (boot options 8 native_gate_flight_rng, 16 native_gate_flight_full, 2^31 native_gate_avoidance_refresh, 2^32
 * native_gate_steering_init; 4 native_gate_flight_diag installs the hook too). Included by gate_linux.c only.
 *
 * Linux sites. U::Path::SteeringFlightControlModel (vtable 0x3b64a68) updates through slot 15 (+0x78, Windows slot 14
 * +0x70: GCC's second destructor), 0x13e9d50 (Windows 0xec7200), with (model rdi, slot rsi, dt xmm0, time xmm1, cheap
 * dl, player cl), Windows' arguments in System V order. Its model fields sit 0x10 lower than Windows': the Agent at
 * +0x8 (+0x10), the avoidance time +0x78 (+0x88, checked at the update's avoidance query 0x13ea422), the two
 * constructor angles +0x32c/+0x330 (+0x33c/+0x340). The Agent holds its model at +0xf30 (+0xf10) and its ship at
 * +0xf10 (+0xef0). GCC inlines the constructor (Windows 0xec40c0) into the Agent's set-model 0x1432b80; its two draws
 * (0x1a491c0 at 0x14332a0 and 0x14332b5, limits 2pi and pi) store at 0x14332a5/0x14332c1. The observe hook is a jmp at
 * 0x14332c9 (pxor xmm0,xmm0; mov qword [rbx+0x360],0: 15 bytes, one 16-byte line, nothing jumps inside) to a stub that
 * keeps every caller-saved register (rax is live), the flags and the FPU/SSE state, calls lxf_construct_observe(rbx),
 * replays the 15 bytes and returns to 0x14332d8.
 * The random float: Linux 0x1a491c0 steps the thread word (fs:-0x2720), Windows 0x14ddc90 a given one; both compute
 * ror(state*0x5851f42d4c957f2d+0x14057b7ef767814f,30) and (float)(u32)state*2^-32*limit (the same constant 0x2f800000),
 * so lxf_random gives Windows' bits for the keyed initial angles.
 * The fixture's keys are Windows': flight_seed(tick, actor, call ordinal) per update and actor_seed("STEERINI", 0,
 * actor, generation+1) for the angles; a fixture actor's identity is its number (actor_rng_identity, schema 2).
 * Registration (lxf_register) comes from the battle fixture (actor, ship, runtime ID, Agent). Not ported: the
 * diagnostic blocks (FLIGHT rows, avoidance copies), the FP redirect solver scope and the world trace. */
#define LXF_STEER_VTABLE 0x3b64a68u   /* the model's address point */
#define LXF_UPDATE_SLOT_VA (LXF_STEER_VTABLE+0x78u)
#define LXF_UPDATE_VA 0x13e9d50u
#define LXF_SET_MODEL_VA 0x1432b80u
#define LXF_CTOR_SITE_VA 0x14332c9u
#define LXF_CTOR_RETURN_VA 0x14332d8u
#define LXF_MODEL_AGENT 0x8u
#define LXF_MODEL_AVOIDANCE_TIME 0x78u
#define LXF_MODEL_AZIMUTH 0x32cu
#define LXF_MODEL_POLAR 0x330u
#define LXF_AGENT_MODEL 0xf30u
#define LXF_ACTORS 12u
#define LXF_STUB 160u
typedef void (*lxf_update_fn)(void *,void *,float,double,unsigned char,unsigned char);
/* Windows' FLIGHT row blocks (lockstep_flight.h flight_sample), captured on the fixture path for comparison: the update's
 * dt and time, the slot before and after (24 words, then 0x1a0 bytes of input), the model (0x3b0 bytes: Linux fields
 * sit 0x10 lower, so Windows' 0x3a0 block ends at +0x390 here; the vtable and the Agent pointer zeroed as Windows
 * zeroes its pointer words), the Agent's limits and parameters at +0x340 (0x60 and 0x1a0 bytes, Windows' offsets: the
 * Agent's buffers +0/+0x1a0/+0x340 sit before the first shifted field +0x700) and the thread's game time. */
#define LXF_MODEL_BYTES 0x3b0u
#define LXF_AGENT_LIMITS 0x340u
typedef struct {
    void *model,*agent,*initialized_model,*object;
    uint64_t runtime,tick,calls,init_generation,rng_before,rng_after;
    uint64_t cheap,applied_cheap,time_bits,tls_time;
    uint32_t dt,last_cheap,last_player,before[24],after[24];
    uint32_t model_before[LXF_MODEL_BYTES/4],agent_limits[0x60/4],slot_before[0x1a0/4],params_before[0x1a0/4];
} lxf_sample;
static lxf_sample lxf_samples[LXF_ACTORS];
static int lxf_installed,lxf_restored,lxf_seeded,lxf_full,lxf_refresh,lxf_initial,lxf_ctor_installed;
static uint64_t lxf_calls,lxf_inits,lxf_refreshes,lxf_ctor_calls,lxf_retired,lxf_failures,lxf_registration_inits;
static unsigned char *lxf_page;
static lxf_update_fn lxf_original_override; /* tests */
static uint64_t *lxf_test_rng;              /* tests: the thread word */
static const unsigned char lxf_ctor_original[15]={0x66,0x0f,0xef,0xc0,0x48,0xc7,0x83,0x60,0x03,0x00,0x00,0x00,0x00,0x00,0x00};

static float lxf_random(uint64_t *state,float limit) {
    if(!(limit>=0.0f)) return 0.0f; /* the native error path (never with the constructor's constant limits) */
    uint64_t s=*state*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;s=(s>>30)|(s<<34);*state=s;
    const uint32_t scale_bits=0x2f800000u;float scale;memcpy(&scale,&scale_bits,4);
    float x=(float)(int64_t)(uint32_t)s;x*=scale;x*=limit;return x;
}
static uint64_t lxf_seed(uint64_t tick,unsigned actor,uint64_t ordinal) {return lockstep_seed(0x464c49474854524eULL,tick,actor,0,ordinal,0);}
/* Still this actor's Agent: its ship and the ship's runtime ID. */
static int lxf_identity(unsigned i,const unsigned char *agent) {
    unsigned char *component=NULL;uint64_t id=0;
    return gate_read_local(agent+LXW_AGENT_OWNER,&component,8) && component && component==lxf_samples[i].object &&
        gate_read_local(component+8,&id,8) && id==lxf_samples[i].runtime;
}
static void lxf_retire(unsigned i) {lxf_samples[i].agent=lxf_samples[i].model=NULL;++lxf_retired;}
/* Windows flight_init_model: the constructor's two draws on a keyed word, once per model lifetime. */
static void lxf_init_model(lxf_sample *s,unsigned actor,unsigned char *model) {
    if(s->initialized_model==model) return;
    uint64_t rng=lockstep_seed(0x5354454552494e49ULL,0,actor,s->init_generation+1,0,0);
    const uint32_t bits[2]={0x40c90fdbu,0x40490fdbu};float limits[2];memcpy(limits,bits,8);
    const float azimuth=lxf_random(&rng,limits[0]),polar=lxf_random(&rng,limits[1]);
    memcpy(model+LXF_MODEL_AZIMUTH,&azimuth,4);memcpy(model+LXF_MODEL_POLAR,&polar,4);
    s->initialized_model=model;++s->init_generation;++lxf_inits;
}
/* From the stub at the constructor's end (rbx, the new model): a fixture's Agent gets a new model lifetime. */
static void lxf_construct_observe(unsigned char *model) {
    if(!stats.active || boot_phase!=3 || !lxf_initial || !model) return;
    void *agent=NULL,*vtable=NULL;
    if(!gate_read_local(model+LXF_MODEL_AGENT,&agent,8) || !agent) return;
    for(unsigned i=0;i<LXF_ACTORS;i++) if(lxf_samples[i].agent==agent) {
        if(!lxf_identity(i,agent)) {lxf_retire(i);continue;}
        if(!gate_read_local(model,&vtable,8) || vtable!=(void*)LX(LXF_STEER_VTABLE)) {++lxf_failures;return;}
        lxf_samples[i].initialized_model=NULL;lxf_samples[i].model=model;
        lxf_init_model(&lxf_samples[i],i+1,model);++lxf_ctor_calls;break;
    }
}
static lxf_sample *lxf_sample_for(void *self,unsigned *actor) {
    void *owner=NULL;
    if(!gate_read_local((unsigned char*)self+LXF_MODEL_AGENT,&owner,8)) return NULL;
    for(unsigned i=0;i<LXF_ACTORS;i++) if(lxf_samples[i].model==self && lxf_samples[i].agent==owner) {
        if(!lxf_identity(i,owner)) {lxf_retire(i);return NULL;}
        *actor=i+1;return &lxf_samples[i];
    }
    return NULL;
}
/* The vtable slot's function: a fixture's update on its keyed word (restored after), every other model natively. */
static void lxf_update(void *self,void *slot,float dt,double time,unsigned char cheap,unsigned char player) {
    lxf_update_fn original=lxf_original_override?lxf_original_override:(lxf_update_fn)(void*)LX(LXF_UPDATE_VA);
    unsigned actor=0;lxf_sample *s=stats.active && boot_phase==3?lxf_sample_for(self,&actor):NULL;
    if(!s) {original(self,slot,dt,time,cheap,player);return;}
    if(s->tick!=stats.issued) {s->tick=stats.issued;s->calls=s->cheap=s->applied_cheap=0;}
    ++s->calls;s->cheap+=cheap!=0;s->applied_cheap+=!lxf_full && cheap!=0;
    /* The diagnostic blocks (read only; a failed read leaves zeros). */
    memcpy(&s->dt,&dt,4);memcpy(&s->time_bits,&time,8);s->last_cheap=cheap;s->last_player=player;
    if(!gate_read_local(slot,s->before,sizeof(s->before))) memset(s->before,0,sizeof(s->before));
    if(!gate_read_local(slot,s->slot_before,sizeof(s->slot_before))) memset(s->slot_before,0,sizeof(s->slot_before));
    if(!gate_read_local(self,s->model_before,sizeof(s->model_before))) memset(s->model_before,0,sizeof(s->model_before));
    memset(s->model_before,0,16);memset((unsigned char*)s->model_before+0x60,0,0x10); /* vtable, Agent; the behaviour pointers (Windows +0x70) */
    if(!s->agent || !gate_read_local((unsigned char*)s->agent+LXF_AGENT_LIMITS,s->agent_limits,sizeof(s->agent_limits)) ||
       !gate_read_local((unsigned char*)s->agent+LXF_AGENT_LIMITS,s->params_before,sizeof(s->params_before))) {
        memset(s->agent_limits,0,sizeof(s->agent_limits));memset(s->params_before,0,sizeof(s->params_before));
    }
    {unsigned char *base=gate_tls_self();const double *t=base?gate_tls_time(base):NULL;s->tls_time=0;if(t) memcpy(&s->tls_time,t,8);}
    uint64_t *rng=lxf_test_rng?lxf_test_rng:gate_tls_rng(gate_tls_self()),saved=rng?*rng:0;
    const int keyed=lxf_seeded && rng!=NULL;
    if(keyed) *rng=lxf_seed(stats.issued,actor,s->calls);
    s->rng_before=rng?*rng:0;
    if(lxf_refresh) {const double stale=-1.0;memcpy((unsigned char*)self+LXF_MODEL_AVOIDANCE_TIME,&stale,8);++lxf_refreshes;}
    original(self,slot,dt,time,lxf_full?0:cheap,player);
    if(!gate_read_local(slot,s->after,sizeof(s->after))) memset(s->after,0,sizeof(s->after));
    s->rng_after=rng?*rng:0;
    if(keyed) *rng=saved;
    ++lxf_calls;
}
/* Windows flight_register_with: the actor's Agent and, if it is a steering model, the model (keyed initial angles). */
static int lxf_register_with(unsigned actor,void *object,uint64_t runtime,unsigned char *agent,int init) {
    if(!lxf_installed || actor<1 || actor>LXF_ACTORS || !object || !agent) return 0;
    lxf_sample *s=&lxf_samples[actor-1];unsigned char *model=NULL;void *vtable=NULL,*owner=NULL;
    s->object=object;s->runtime=runtime;
    if(!gate_read_local(agent+LXF_AGENT_MODEL,&model,8) || !model || !gate_read_local(model,&vtable,8) ||
       !gate_read_local(model+LXF_MODEL_AGENT,&owner,8) || owner!=agent) return 0;
    s->agent=agent;
    if(vtable!=(void*)LX(LXF_STEER_VTABLE)) {s->model=NULL;return 0;}
    s->model=model;
    if(lxf_initial && !init) s->initialized_model=model;
    else if(lxf_initial) {uint64_t before=s->init_generation;lxf_init_model(s,actor,model);if(s->init_generation!=before) ++lxf_registration_inits;}
    return 1;
}
/* The battle fixture's preparation calls it (not ported yet: until then only the tests do). */
static __attribute__((unused)) int lxf_register(unsigned actor,void *object,uint64_t runtime,unsigned char *agent) {
    return lxf_register_with(actor,object,runtime,agent,1);
}
/* lea rsp,[rsp-0x80]; push rax,rcx,rdx,rsi,rdi,r8..r11; pushfq; push rbp; mov rbp,rsp; and rsp,-16; sub rsp,0x200;
 * fxsave64 [rsp]; mov rdi,rbx; movabs rax,fn; call rax; fxrstor64 [rsp]; mov rsp,rbp; pop rbp; popfq; pop r11..r8,
 * rdi..rax; lea rsp,[rsp+0x80]; the 15 replayed bytes; jmp [rip] back. */
static void lxf_stub_bytes(unsigned char out[LXF_STUB],const void *fn,const void *back) {
    static const unsigned char head[]={0x48,0x8d,0x64,0x24,0x80, 0x50,0x51,0x52,0x56,0x57, 0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53, 0x9c,
        0x55, 0x48,0x89,0xe5, 0x48,0x83,0xe4,0xf0, 0x48,0x81,0xec,0x00,0x02,0x00,0x00, 0x48,0x0f,0xae,0x04,0x24, 0x48,0x89,0xdf, 0x48,0xb8};
    static const unsigned char tail[]={0xff,0xd0, 0x48,0x0f,0xae,0x0c,0x24, 0x48,0x89,0xec, 0x5d, 0x9d,
        0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58, 0x5f,0x5e,0x5a,0x59,0x58, 0x48,0x8d,0xa4,0x24,0x80,0x00,0x00,0x00};
    static const unsigned char jump[]={0xff,0x25,0,0,0,0};
    _Static_assert(sizeof(head)+8+sizeof(tail)+sizeof(lxf_ctor_original)+sizeof(jump)+8<=LXF_STUB,"stub size");
    unsigned n=0;memset(out,0xcc,LXF_STUB);
    memcpy(out+n,head,sizeof(head));n+=sizeof(head);memcpy(out+n,&fn,8);n+=8;
    memcpy(out+n,tail,sizeof(tail));n+=sizeof(tail);
    memcpy(out+n,lxf_ctor_original,sizeof(lxf_ctor_original));n+=sizeof(lxf_ctor_original);
    memcpy(out+n,jump,sizeof(jump));n+=sizeof(jump);memcpy(out+n,&back,8);
}
static void lxf_entry(unsigned char out[5]) {
    int32_t d=(int32_t)((intptr_t)lxf_page-(intptr_t)(LX(LXF_CTOR_SITE_VA)+5));out[0]=0xe9;memcpy(out+1,&d,4);
}
/* 0 native, 1 our jmp, -1 anything else. */
static int lxf_ctor_state(void) {
    unsigned char b[15],want[5];
    if(!gate_read_local(LX(LXF_CTOR_SITE_VA),b,15)) return -1;
    if(!memcmp(b,lxf_ctor_original,15)) return 0;
    if(lxf_page && !memcmp(b+5,lxf_ctor_original+5,10)) {lxf_entry(want);if(!memcmp(b,want,5)) return 1;}
    return -1;
}
static int lxf_ctor_swap(const unsigned char from[5],const unsigned char to[5]) {
    unsigned char *line=LX(LXF_CTOR_SITE_VA&~15u);const unsigned at=LXF_CTOR_SITE_VA&15u;
    unsigned char a[16],b[16];unsigned __int128 expected,desired;
    if(at+5>16) return 0;
    memcpy(a,line,16);memcpy(b,a,16);memcpy(a+at,from,5);memcpy(b+at,to,5);
    memcpy(&expected,a,16);memcpy(&desired,b,16);
    return gate_code_swap128(line,expected,desired);
}
static int lxf_ctor_install(void) {
    if(lxf_ctor_state()!=0) return 0;
    if(!lxf_page) lxf_page=gate_near_page(LX(LXF_CTOR_SITE_VA));
    if(!lxf_page) return 0;
    intptr_t d=(intptr_t)lxf_page-(intptr_t)(LX(LXF_CTOR_SITE_VA)+5);if(d<INT32_MIN || d>INT32_MAX) return 0;
    if(mprotect(lxf_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxf_page,0xcc,GATE_PAGE);lxf_stub_bytes(lxf_page,(void*)lxf_construct_observe,LX(LXF_CTOR_RETURN_VA));
    if(mprotect(lxf_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    unsigned char to[5];lxf_entry(to);
    if(!lxf_ctor_swap(lxf_ctor_original,to)) return 0;
    lxf_ctor_installed=1;return 1;
}
static int lxf_slot_swap(void *expected,void *value) {
    return gate_data_swap64(LX(LXF_UPDATE_SLOT_VA),(uint64_t)(uintptr_t)expected,(uint64_t)(uintptr_t)value);
}
/* Boot: the update head and the set-model are start pins (gate_sites.h); the slot holds the native update. */
static int lxf_install(int seed,int full,int refresh,int initial) {
    if(lxf_installed) return 1;
    if((refresh && !full) || (initial && !seed) || rd64(LX(LXF_UPDATE_SLOT_VA))!=(uint64_t)(uintptr_t)LX(LXF_UPDATE_VA)) return 0;
    if(!lxf_slot_swap(LX(LXF_UPDATE_VA),(void*)lxf_update)) return 0;
    lxf_seeded=seed;lxf_full=full;lxf_refresh=refresh;lxf_initial=initial;lxf_installed=1;lxf_restored=0;
    if(initial && !lxf_ctor_install()) return 0; /* the stop restores the slot */
    return 1;
}
static int lxf_owned(void) {
    if(lxf_installed && rd64(LX(LXF_UPDATE_SLOT_VA))!=(uint64_t)(uintptr_t)lxf_update) return 0;
    if(lxf_ctor_installed && lxf_ctor_state()!=1) return 0;
    return 1;
}
static void lxf_restore(void) {
    if(lxf_ctor_installed) {
        unsigned char from[5];lxf_entry(from);
        int state=lxf_ctor_state();
        if(state==1 && lxf_ctor_swap(from,lxf_ctor_original)) lxf_ctor_installed=0;
        else if(state==0) lxf_ctor_installed=0;
    }
    if(lxf_installed && lxf_slot_swap((void*)lxf_update,LX(LXF_UPDATE_VA))) lxf_installed=0;
    lxf_restored=!lxf_installed && !lxf_ctor_installed; /* the stub page stays: a thread may still be inside it */
}
static int lxf_status(char *out,size_t cap) {
    unsigned registered=0;for(unsigned i=0;i<LXF_ACTORS;i++) registered+=lxf_samples[i].model!=NULL;
    int n=snprintf(out,cap,"installed=%d restored=%d seeded=%d full=%d refresh=%d initial=%d ctor=%d calls=%llu refreshes=%llu inits=%llu "
        "ctor_calls=%llu registration_inits=%llu retired=%llu registered=%u failures=%llu",lxf_installed,lxf_restored,lxf_seeded,lxf_full,
        lxf_refresh,lxf_initial,lxf_ctor_installed,(unsigned long long)lxf_calls,(unsigned long long)lxf_refreshes,(unsigned long long)lxf_inits,
        (unsigned long long)lxf_ctor_calls,(unsigned long long)lxf_registration_inits,(unsigned long long)lxf_retired,registered,
        (unsigned long long)lxf_failures);
    return n>0 && (size_t)n<cap?n:0;
}

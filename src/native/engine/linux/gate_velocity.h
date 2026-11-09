/* native_gate_velocity_diag on the Linux gate (boot option 524288, diagnostic, default off): Windows'
 * lockstep_velocity.h, the fixture ships' NavigationController::SetVelocity calls with their input and the pathing
 * slot's velocities before and after, as VELOCITY rows in Windows' text. Included by gate_linux.c after gate_battle.h.
 *
 * Why: the battle's tick-28 split against Windows is in the velocities X4 puts on both physics bodies before the Jolt
 * step (gate_body_step.h: the step leaves them as they are). Those come from the ship's GetLocalVelocity, the
 * controller's pathing slot (+0x40 linear, +0x50 angular), which SetVelocity writes; its conversion (the zone's
 * velocity subtracted, the transposed rotation) is the same arithmetic in the same order on both executables, so the
 * rows show whether the input or the zone's reference splits.
 *
 * Linux sites: U::NavigationController::SetVelocity is 0x141fe50 (Windows vfunc8 0xea5790), held by slot 15 of the
 * class's two vtables 0x3b697e8 and 0x3b94bb0 (Windows numbering + 7, as vfunc33 in slot 40, gate_nav.h). The slots
 * are pointer swaps. The pathing slots: controller +0x28 (Windows +0x30), stride 0x1a0, the current index at
 * 0x3e4fea8 (Windows 0x38f4ee4). The ship's controller is at +0x200 on both. Stop reasons as Windows: 43 the install
 * at boot, 44 a failure or a slot no longer ours. */
#define LXVD_MOVE_VA 0x141fe50u
#define LXVD_SLOT_A_VA (0x3b697e8u+15u*8u)
#define LXVD_SLOT_B_VA (0x3b94bb0u+15u*8u)
#define LXVD_INDEX_VA 0x3e4fea8u
#define LXVD_SLOTS 0x28u
#define LXVD_MAX 32u
typedef void (*lxvd_fn)(void *,const void *);
typedef struct {uint64_t source,time,seq;uint32_t input[8],before[8],after[8],body[2][8];int bodies;} lxvd_record;
typedef struct {uint64_t tick;unsigned count;lxvd_record records[LXVD_MAX];} lxvd_trace;
static lxvd_trace lxvd_traces[LXB_ACTORS];
static int lxvd_installed,lxvd_restored;
static uint64_t lxvd_calls,lxvd_other_thread;
static volatile int64_t lxvd_failures;
static lxvd_fn lxvd_original_override; /* tests */
static int lxvd_state(void *controller,uint32_t state[8]) {
    unsigned char *slots=NULL;uint32_t index=0;
    return gate_read_local(LX(LXVD_INDEX_VA),&index,4) && index<2 && gate_read_local((unsigned char*)controller+LXVD_SLOTS,&slots,8) && slots &&
           gate_read_local(slots+index*0x1a0u+0x40u,state,32);
}
static void lxvd_move(void *self,const void *value) {
    lxvd_fn original=lxvd_original_override?lxvd_original_override:(lxvd_fn)(void*)LX(LXVD_MOVE_VA);unsigned actor=0;
    if(stats.active && lxb_bound==LXB_ACTORS) for(unsigned i=0;i<LXB_ACTORS;i++) {
        void *controller=NULL;
        if(lxb_actors[i].object && gate_read_local((unsigned char*)lxb_actors[i].object+LXW_OBJ_CONTROLLER,&controller,8) && controller==self) {actor=i+1;break;}
    }
    if(actor && GetCurrentThreadId()!=ui_thread) {lxvd_other_thread++;actor=0;}
    if(!actor) {original(self,value);return;}
    lxvd_trace *trace=&lxvd_traces[actor-1];
    if(trace->tick!=stats.issued) {trace->tick=stats.issued;trace->count=0;}
    if(trace->count>=LXVD_MAX) {__atomic_add_fetch(&lxvd_failures,1,__ATOMIC_SEQ_CST);original(self,value);return;}
    lxvd_record *r=&trace->records[trace->count++];memset(r,0,sizeof(*r));
    const unsigned char *caller=__builtin_return_address(0);
    r->source=(uint64_t)(uintptr_t)(caller-image)+LX_IMAGE_BASE; /* the caller's Linux VA */
    unsigned char *tls=gate_tls_self();const double *now=tls?gate_tls_time(tls):NULL;
    if(!now || !gate_read_local(now,&r->time,8)) __atomic_add_fetch(&lxvd_failures,1,__ATOMIC_SEQ_CST);
    if(!gate_read_local(value,r->input,32) || !lxvd_state(self,r->before)) __atomic_add_fetch(&lxvd_failures,1,__ATOMIC_SEQ_CST);
    /* Linux only, after Windows' fields: the order among the step captures, and both bodies' velocities now. */
    r->seq=__atomic_add_fetch(&lx_phys_seq,1,__ATOMIC_SEQ_CST);
    for(unsigned w=0;w<2;w++) {unsigned char *motion=NULL;if(lxbs_find(actor,w,&motion) && gate_read_local(motion,r->body[w],32)) r->bodies|=1<<w;}
    original(self,value);
    if(!lxvd_state(self,r->after)) __atomic_add_fetch(&lxvd_failures,1,__ATOMIC_SEQ_CST);
    lxvd_calls++;
}
static int lxvd_slots_swap(void *expected,void *value) {
    int a=gate_data_swap64(LX(LXVD_SLOT_A_VA),(uint64_t)(uintptr_t)expected,(uint64_t)(uintptr_t)value);
    int b=gate_data_swap64(LX(LXVD_SLOT_B_VA),(uint64_t)(uintptr_t)expected,(uint64_t)(uintptr_t)value);
    return a && b;
}
static int lxvd_owned(void) {
    return !lxvd_installed || (rd64(LX(LXVD_SLOT_A_VA))==(uint64_t)(uintptr_t)lxvd_move && rd64(LX(LXVD_SLOT_B_VA))==(uint64_t)(uintptr_t)lxvd_move);
}
/* At boot, before the snapshot load (Windows installs it in the boot call too). */
static int lxvd_install(void) {
    const lx_site *s=site_named("nav_set_velocity");
    if(lxvd_installed) return 1;
    if(!s || s->va!=LXVD_MOVE_VA || !site_ok(s)) return 0;
    if(rd64(LX(LXVD_SLOT_A_VA))!=(uint64_t)(uintptr_t)LX(LXVD_MOVE_VA) || rd64(LX(LXVD_SLOT_B_VA))!=(uint64_t)(uintptr_t)LX(LXVD_MOVE_VA)) return 0;
    memset(lxvd_traces,0,sizeof(lxvd_traces));lxvd_calls=lxvd_other_thread=0;lxvd_failures=0;
    if(!lxvd_slots_swap(LX(LXVD_MOVE_VA),(void*)lxvd_move)) {lxvd_slots_swap((void*)lxvd_move,LX(LXVD_MOVE_VA));return 0;}
    lxvd_installed=1;lxvd_restored=0;return 1;
}
static void lxvd_restore(void) {
    if(!lxvd_installed) return;
    if(lxvd_slots_swap((void*)lxvd_move,LX(LXVD_MOVE_VA))) {lxvd_installed=0;lxvd_restored=1;}
}
/* gate.velocity(actor, ordinal): Windows' text; ordinal 0 the count of this tick's calls. */
static int lua_velocity(lua_State *L) {
    double actor=p_lua_tonumber(L,1),ordinal=p_lua_tonumber(L,2);
    if(!lxvd_installed || GetCurrentThreadId()!=ui_thread || !isfinite(actor) || actor<1 || actor>LXB_ACTORS || floor(actor)!=actor ||
       !isfinite(ordinal) || ordinal<0 || ordinal>LXVD_MAX || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    lxvd_trace *trace=&lxvd_traces[(unsigned)actor-1];unsigned count=trace->tick==stats.issued?trace->count:0;
    if(ordinal==0) {p_lua_pushnumber(L,count);return 1;}
    if(ordinal>count) {p_lua_pushnil(L);return 1;}
    lxvd_record *r=&trace->records[(unsigned)ordinal-1];char out[768];
    int n=snprintf(out,sizeof(out),"tick=%llu source=%llx time=%016llx input=",(unsigned long long)trace->tick,
        (unsigned long long)r->source,(unsigned long long)r->time);
    n+=lxb_hex_words(out+n,sizeof(out)-(size_t)n,r->input,8);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," before=");n+=lxb_hex_words(out+n,sizeof(out)-(size_t)n,r->before,8);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," after=");n+=lxb_hex_words(out+n,sizeof(out)-(size_t)n,r->after,8);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," seq=%llu",(unsigned long long)r->seq);
    for(unsigned w=0;w<2;w++) if(r->bodies&(1<<w)) {n+=snprintf(out+n,sizeof(out)-(size_t)n," body%u=",w);n+=lxb_hex_words(out+n,sizeof(out)-(size_t)n,r->body[w],8);}
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_velocity_status(lua_State *L) {
    char out[256];int n=snprintf(out,sizeof(out),"calls=%llu restored=%d failures=%lld installed=%d other_thread=%llu owned=%d",
        (unsigned long long)lxvd_calls,lxvd_restored,(long long)lxvd_failures,lxvd_installed,(unsigned long long)lxvd_other_thread,lxvd_owned());
    if(!accept_ui() || n<=0 || (size_t)n>=sizeof(out)) {p_lua_pushnil(L);return 1;}
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}

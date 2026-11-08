/* The battle fixture on the Linux X4 9.00 executable (native_gate_hash = 'battle' with native_gate_snapshot_fixture):
 * the Linux twin of lockstep_snapshot_fixture.h's enrollment and of lua_actor's state row (lockstep_gate.c), whose
 * text the probe hashes into the per-tick digest. Included by gate_linux.c only, after gate_flight.h.
 *
 * The twelve fixture ships of snapshot 005 are named by saved ID (gate.snapshot_fixture, before the load). At the boot
 * admission (the world identities sealed) each is bound: the one runtime ID the import gave its saved ID, the object the
 * component find returns for it (0xa68df0 on the map 0x3daa860), its own runtime ID at +0x8, the class U::ShipSmall
 * (0x3d21748; Windows 0x2b7feb8), the fixture's macro (U::Macro name, a libstdc++ string), a NavigationController
 * (0x3b697e8) at +0x200 that it owns (+0x20; Windows +0x28), its slots (+0x28; +0x30) at Agent+0xaf0 (the Agent at
 * +0x38; +0x40), the Agent's ship at +0xf10 (+0xef0). At tick 1, in the path release (Windows path_release), each
 * steering model registers with gate_flight.h (keyed initial angles), a spline model is left to its own hook.
 * gate.snapshot_enroll(i, runtime) from the MD events completes it.
 * gate.actor(runtime, i, refresh): Windows' row for a NavigationController ship: attention (+0x1ec), hull (+0x278;
 * +0x280), the bank index, current and previous (the read and the other bank, 24 words each, stride 0x1a0), the
 * controller's cache (+0x58, 16 words; +0x60), hull_bits, the stored shield (gate_world.h's text, as Windows'), the
 * steering lifetime when steering_init is on, and the navigation refresh (+0xc0/+0xc8). With refresh=1
 * (native_gate_shield_refresh) GetShieldPercent 0x17d7b70 (Windows 0x51f5b0: the presence check, the group's factored
 * maximum against 1e-7, the holder's read 0x1293330 at the time, value*100/max) runs at the UI thread's game time
 * before the shield is read again, as Windows' shield_refresh_native. Not ported yet: the spline state (spline_diag), the Agent and pilot fields
 * (agent_rng), the radar registration and the parameter initialization at tick 1 (radar_*, params_init), dock
 * controllers, dynamic ships, the hot joiner's bind. Stop 74 for a refused bind or preparation, as on Windows. */
#define LXB_ACTORS 12u
#define LXB_SHIP_VTABLE 0x3d21748u   /* U::ShipSmall */
#define LXB_NAV_VTABLE 0x3b697e8u    /* U::NavigationController */
#define LXB_LINEAR_VTABLE 0x3b624e0u /* U::Path::LinearFlightControlModel */
#define LXB_OBJ_HULL 0x278u
#define LXB_NAV_CACHE 0x58u
#define LXB_BANK_STRIDE 0x1a0u
typedef void *(*lxb_find_fn)(void *,uint64_t,unsigned);
typedef struct {uint64_t saved,runtime;void *object;unsigned char *agent;unsigned imports,enrolled;const char *flight;} lxb_actor;
static lxb_actor lxb_actors[LXB_ACTORS];
static unsigned lxb_configured,lxb_bound,lxb_prepared,lxb_enrolled;
static uint64_t lxb_bound_tick,lxb_prepared_tick;
static volatile int64_t lxb_failures;
static const char *lxb_stage="none";
static unsigned lxb_stage_actor;
static lxb_find_fn lxb_find_override; /* tests */
static void *lxb_map_override;
typedef float (*lxb_percent_fn)(void *,double);
static lxb_percent_fn lxb_percent_override; /* tests */
static const double *lxb_time_override;     /* tests: the UI thread's game time */
static uint64_t lxb_shield_refresh_calls,lxb_shield_refresh_failures;
static int lxb_pin_skip; /* tests on the synthetic image */
static int lxb_shield_pinned(void) {const lx_site *s=site_named("shield_percent");return s && site_ok(s);}
/* Windows shield_refresh_native: GetShieldPercent (0x17d7b70) at the UI thread's game time materializes the lazy
 * recharge (value = max, time 0 once full) before the state is read. A start pin; the percent must be in range. */
static int lxb_shield_refresh(void *object) {
    const double *t=lxb_time_override?lxb_time_override:gate_tls_time(gate_tls_self());double time=0;
    if((!lxb_pin_skip && !lxb_shield_pinned()) || !t) return 0;
    memcpy(&time,t,8);if(!isfinite(time) || time<0) return 0;
    const float percent=(lxb_percent_override?lxb_percent_override:(lxb_percent_fn)(void*)LX(LX_SHIELD_PERCENT_VA))(object,time);
    ++lxb_shield_refresh_calls;
    return isfinite(percent) && percent>=0 && percent<=100.0f;
}

static int lxb_manifest(const char *text,size_t length) {
    uint64_t ids[LXB_ACTORS];
    if(!text || length!=12*16+11) return 0;
    for(unsigned i=0;i<LXB_ACTORS;i++) {
        uint64_t id=0;
        for(unsigned j=0;j<16;j++) {
            char c=text[i*17+j];unsigned d=c>='0'&&c<='9'?(unsigned)(c-'0'):c>='a'&&c<='f'?(unsigned)(c-'a'+10):16u;
            if(d>15) return 0;id=(id<<4)|d;
        }
        if(!id || (i<11 && text[i*17+16]!=',')) return 0;
        for(unsigned j=0;j<i;j++) if(ids[j]==id) return 0;
        ids[i]=id;
    }
    if(lxb_configured) {for(unsigned i=0;i<LXB_ACTORS;i++) if(lxb_actors[i].saved!=ids[i]) return 0;return 1;} /* a UI reload */
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !world_identity_installed || !path_installed || !(path_options&1)) return 0;
    memset(lxb_actors,0,sizeof(lxb_actors));
    for(unsigned i=0;i<LXB_ACTORS;i++) lxb_actors[i].saved=ids[i];
    lxb_configured=1;return 1;
}
/* The sealed identity table: each fixture saved ID's runtime IDs (Windows captures them at the import; one each). */
static void lxb_capture_imports(void) {
    for(unsigned i=0;i<LXB_ACTORS;i++) {lxb_actors[i].imports=0;lxb_actors[i].runtime=0;}
    if(!world_identities) return;
    for(uint64_t e=0;e<WORLD_IDENTITY_CAPACITY;e++) {
        const world_identity_entry *w=&world_identities[e];if(!w->runtime) continue;
        for(unsigned i=0;i<LXB_ACTORS;i++) if(w->saved==lxb_actors[i].saved) {++lxb_actors[i].imports;lxb_actors[i].runtime=w->runtime;}
    }
}
static int lxb_macro_is(const unsigned char *object,const char *expected) {
    char name[96];return ai_sched_macro(object,name) && !strcmp(name,expected);
}
/* The actor's object, controller and Agent, as Windows snapshot_native_actor checks them. */
static int lxb_native_actor(unsigned index,lxb_find_fn find,void *map) {
    lxb_actor *a=&lxb_actors[index];unsigned char *object=NULL,*controller=NULL,*slots=NULL,*agent=NULL;
    void *vtable=NULL,*owner=NULL,*agent_owner=NULL;uint64_t id=0;
    const char *expected=index<6?"ship_arg_s_fighter_01_a_macro":"ship_xen_s_fighter_01_a_macro";
    lxb_stage_actor=index+1;
    if(!find || !map) {lxb_stage="map";return 0;}
    if(a->imports!=1 || !a->runtime) {lxb_stage="imports";return 0;}
    object=find(map,a->runtime,4);
    if(!object || (a->object && a->object!=object)) {lxb_stage="find";return 0;}
    if(world_identity_lookup(a->runtime)!=a->saved || !gate_read_local(object+8,&id,8) || id!=a->runtime) {lxb_stage="identity";return 0;}
    if(!gate_read_local(object,&vtable,8) || vtable!=(void*)LX(LXB_SHIP_VTABLE)) {lxb_stage="class";return 0;}
    if(!lxb_macro_is(object,expected)) {lxb_stage="macro";return 0;}
    if(!gate_read_local(object+LXW_OBJ_CONTROLLER,&controller,8) || !controller) {lxb_stage="controller";return 0;}
    if(!gate_read_local(controller,&vtable,8) || vtable!=(void*)LX(LXB_NAV_VTABLE)) {lxb_stage="vtable";return 0;}
    if(!gate_read_local(controller+LXW_NAV_OWNER,&owner,8) || owner!=object) {lxb_stage="owner";return 0;}
    if(!gate_read_local(controller+LXW_NAV_SLOTS,&slots,8) || !slots || (uintptr_t)slots<LXW_AGENT_SLOTS) {lxb_stage="slots";return 0;}
    if(!gate_read_local(controller+LXW_NAV_AGENT,&agent,8) || agent!=slots-LXW_AGENT_SLOTS) {lxb_stage="agent";return 0;}
    if(!gate_read_local(agent+LXW_AGENT_OWNER,&agent_owner,8) || agent_owner!=object) {lxb_stage="agent_owner";return 0;}
    a->object=object;a->agent=agent;return 1;
}
static lxb_find_fn lxb_find(void) {return lxb_find_override?lxb_find_override:(lxb_find_fn)(void*)LX(LX_FIND_VA);}
static void *lxb_map(void) {
    if(lxb_map_override) return lxb_map_override;
    void *map=NULL;return gate_read_local(LX(LX_COMPONENT_MAP_VA),&map,8)?map:NULL;
}
/* At the boot admission (main thread, tick 0): all twelve or none. */
static int lxb_bind(void) {
    if(!lxb_configured) return 1;
    lxb_stage="bind_guard";
    if(lxb_bound || lxb_failures || stats.issued || boot_phase!=3) return 0;
    lxb_capture_imports();
    lxb_find_fn find=lxb_find();void *map=lxb_map();
    for(unsigned i=0;i<LXB_ACTORS;i++) {
        lxb_actors[i].object=NULL;lxb_actors[i].agent=NULL;
        if(!lxb_native_actor(i,find,map)) return 0;
        for(unsigned j=0;j<i;j++) if(lxb_actors[i].object==lxb_actors[j].object || lxb_actors[i].runtime==lxb_actors[j].runtime) {lxb_stage="duplicate";return 0;}
    }
    lxb_bound=LXB_ACTORS;lxb_bound_tick=stats.issued;lxb_stage="bound";lxb_stage_actor=0;return 1;
}
/* Tick 1, in the path release on the UI thread: the steering models register (gate_flight.h). */
static int lxb_prepare(void) {
    if(!lxb_configured || lxb_prepared) return 1;
    lxb_stage="prepare_guard";
    if(lxb_bound!=LXB_ACTORS || lxb_failures || stats.issued!=1 || GetCurrentThreadId()!=ui_thread) return 0;
    lxb_find_fn find=lxb_find();void *map=lxb_map();
    lxb_stage="prepare_identity";
    for(unsigned i=0;i<LXB_ACTORS;i++) if(!lxb_native_actor(i,find,map)) return 0;
    for(unsigned i=0;i<LXB_ACTORS;i++) {
        lxb_actor *a=&lxb_actors[i];unsigned char *model=NULL;void *vtable=NULL;
        lxb_stage_actor=i+1;lxb_stage="prepare_steering";a->flight="disabled";
        if(!lxf_installed) continue;
        if(!gate_read_local(a->agent+LXF_AGENT_MODEL,&model,8)) return 0;
        if(!model) {a->flight="none";continue;}
        if(!gate_read_local(model,&vtable,8)) return 0;
        if(vtable==(void*)LX(LX_SPLINE_VTABLE)) {a->flight="spline";continue;} /* its own hook covers it */
        if(vtable!=(void*)LX(LXF_STEER_VTABLE) || !lxf_register(i+1,a->object,a->runtime,a->agent)) return 0;
        a->flight="steering";
    }
    lxb_prepared=LXB_ACTORS;lxb_prepared_tick=stats.issued;lxb_stage="complete";lxb_stage_actor=0;return 1;
}
static int lxb_enroll(unsigned actor,uint64_t runtime) {
    if(actor<1 || actor>LXB_ACTORS || lxb_bound!=LXB_ACTORS || lxb_prepared!=LXB_ACTORS || lxb_failures) return 0;
    lxb_actor *a=&lxb_actors[actor-1];uint64_t current=0;
    if(runtime!=a->runtime || !gate_read_local((unsigned char*)a->object+8,&current,8) || current!=runtime ||
       world_identity_lookup(runtime)!=a->saved) return 0;
    if(!a->enrolled) {a->enrolled=1;++lxb_enrolled;}
    return 1;
}
static int lxb_hex_words(char *out,size_t capacity,const uint32_t *words,unsigned count) {
    static const char digits[]="0123456789abcdef";
    if(capacity<(size_t)count*8u+1u) {if(capacity) out[0]=0;return 0;}
    for(unsigned i=0;i<count;i++) for(unsigned j=0;j<8;j++) out[i*8u+j]=digits[(words[i]>>(28u-4u*j))&15u];
    out[count*8u]=0;return (int)(count*8u);
}
/* The steering lifetime as Windows prints it: generation and the two angles of a registered model, else its kind. */
static int lxb_steering(unsigned actor,const unsigned char *agent,char *out,size_t cap) {
    const lxf_sample *s=&lxf_samples[actor-1];uint32_t angles[2];
    if(s->model && gate_read_local((unsigned char*)s->model+LXF_MODEL_AZIMUTH,angles,8))
        return snprintf(out,cap,";steering=%llu:%08x:%08x",(unsigned long long)s->init_generation,angles[0],angles[1]);
    void *model=NULL,*vtable=NULL;
    if(!gate_read_local(agent+LXF_AGENT_MODEL,&model,8)) return -1;
    if(!model) return snprintf(out,cap,";steering=none:%llu",(unsigned long long)s->init_generation);
    if(!gate_read_local(model,&vtable,8)) return -1;
    if(vtable==(void*)LX(LX_SPLINE_VTABLE)) return snprintf(out,cap,";steering=spline:%llu",(unsigned long long)s->init_generation);
    if(vtable==(void*)LX(LXB_LINEAR_VTABLE)) return snprintf(out,cap,";steering=linear:%llu",(unsigned long long)s->init_generation);
    return snprintf(out,cap,";steering=unsupported:%llx",(unsigned long long)((unsigned char*)vtable-image));
}
static int lxb_unavailable(lua_State *L,const char *stage) {
    char message[96];int n=snprintf(message,sizeof(message),"stage=%s detail_rva=0",stage);
    p_lua_pushnil(L);p_lua_pushlstring(L,message,(size_t)n);return 2;
}
static int lua_actor(lua_State *L) {
    double number=p_lua_tonumber(L,1),actor=p_lua_tonumber(L,2),refresh=p_lua_tonumber(L,3);
    if(!stats.active || GetCurrentThreadId()!=ui_thread || !isfinite(number) || number<1 || number>2199023255551.0 || floor(number)!=number)
        return lxb_unavailable(L,"guard");
    void *map=lxb_map();unsigned char *object=map?lxb_find()(map,(uint64_t)number,4):NULL,*controller=NULL,*slots=NULL;
    unsigned char object_data[0x288],ctrl_data[0xd0];
    if(!object || !gate_read_local(object,object_data,sizeof(object_data))) return lxb_unavailable(L,"object_read");
    memcpy(&controller,object_data+LXW_OBJ_CONTROLLER,8);
    if(!controller || !gate_read_local(controller,ctrl_data,sizeof(ctrl_data))) return lxb_unavailable(L,"controller_read");
    if(lxb_configured && (!isfinite(actor) || actor<1 || actor>LXB_ACTORS || floor(actor)!=actor ||
       !lxb_actors[(unsigned)actor-1].enrolled || lxb_actors[(unsigned)actor-1].object!=object ||
       lxb_actors[(unsigned)actor-1].runtime!=(uint64_t)number)) return lxb_unavailable(L,"snapshot_enrollment");
    void *vtable;memcpy(&vtable,ctrl_data,8);
    if(vtable!=(void*)LX(LXB_NAV_VTABLE)) return lxb_unavailable(L,"controller_type");
    memcpy(&slots,ctrl_data+LXW_NAV_SLOTS,8);
    if(!slots) return lxb_unavailable(L,"state_read");
    const unsigned index_actor=isfinite(actor) && actor>=1 && actor<=LXB_ACTORS && floor(actor)==actor?(unsigned)actor:0;
    if(index_actor && lxf_installed) (void)lxf_register(index_actor,object,(uint64_t)number,slots-LXW_AGENT_SLOTS);
    unsigned index=2;
    if(!gate_read_local(LX(LX_BANK_READ_VA),&index,4) || index>1) return lxb_unavailable(L,"state_index");
    uint32_t state[2][24],cache[16];double hull;unsigned attention;
    for(unsigned i=0;i<2;i++) if(!gate_read_local(slots+i*LXB_BANK_STRIDE,state[i],sizeof(state[i]))) return lxb_unavailable(L,"state_read");
    memcpy(cache,ctrl_data+LXB_NAV_CACHE,sizeof(cache));memcpy(&hull,object_data+LXB_OBJ_HULL,8);memcpy(&attention,object_data+LXW_OBJ_ATTENTION,4);
    char out[2048];int n=snprintf(out,sizeof(out),"attention=%u;hull=%.17g;index=%u;current=",attention,hull,index);
    n+=lxb_hex_words(out+n,sizeof(out)-(size_t)n,state[index],24);
    n+=snprintf(out+n,sizeof(out)-(size_t)n,";previous=");
    n+=lxb_hex_words(out+n,sizeof(out)-(size_t)n,state[1-index],24);
    n+=snprintf(out+n,sizeof(out)-(size_t)n,";cache=");
    n+=lxb_hex_words(out+n,sizeof(out)-(size_t)n,cache,16);
    uint64_t hull_bits;memcpy(&hull_bits,&hull,8);char shield[192];
    int shield_ok=lxw_shield_state(object,shield,NULL);
    if(refresh!=0) {
        if(refresh!=1 || !boot_mode || boot_phase!=3 || !index_actor || !shield_ok || !lxb_shield_refresh(object) ||
           !(shield_ok=lxw_shield_state(object,shield,NULL))) {++lxb_shield_refresh_failures;return lxb_unavailable(L,"shield_refresh");}
    }
    n+=snprintf(out+n,sizeof(out)-(size_t)n,";hull_bits=%016llx;shield=%s",(unsigned long long)hull_bits,shield_ok?shield:"unavailable");
    if(lxf_initial && index_actor) {
        int m=lxb_steering(index_actor,slots-LXW_AGENT_SLOTS,out+n,sizeof(out)-(size_t)n);
        if(m<0) return lxb_unavailable(L,"steering_model_read");
        n+=m;
    }
    uint64_t refresh_time;unsigned refresh_index;
    memcpy(&refresh_time,ctrl_data+LXW_NAV_TIME,8);memcpy(&refresh_index,ctrl_data+LXW_NAV_TIME+8,4);
    n+=snprintf(out+n,sizeof(out)-(size_t)n,";nav_refresh=%016llx:%u",(unsigned long long)refresh_time,refresh_index);
    if(n<=0 || (size_t)n>=sizeof(out)) return lxb_unavailable(L,"overflow");
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_snapshot_fixture(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);
    p_lua_pushnumber(L,lxb_manifest(text,length));return 1;
}
static int lua_snapshot_actor(lua_State *L) {
    double number=p_lua_tonumber(L,1);
    if(!accept_ui() || lxb_bound!=LXB_ACTORS || !isfinite(number) || number<1 || number>LXB_ACTORS || floor(number)!=number) {p_lua_pushnil(L);return 1;}
    const lxb_actor *a=&lxb_actors[(unsigned)number-1];char out[256];
    int n=snprintf(out,sizeof(out),"id=%u saved=%016llx runtime=%016llx flight=%s bound_tick=%llu prepared_tick=%llu",(unsigned)number,
        (unsigned long long)a->saved,(unsigned long long)a->runtime,a->flight?a->flight:"disabled",(unsigned long long)lxb_bound_tick,
        (unsigned long long)lxb_prepared_tick);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_snapshot_enroll(lua_State *L) {
    double actor=p_lua_tonumber(L,1);size_t length=0;const char *text=p_lua_tolstring(L,2,&length);uint64_t runtime=0;
    int ok=accept_ui() && stats.active && isfinite(actor) && actor>=1 && actor<=LXB_ACTORS && floor(actor)==actor && text && length && length<=20;
    for(size_t i=0;ok && i<length;i++) {
        unsigned digit=(unsigned)(text[i]-'0');
        if(digit>9 || runtime>(UINT64_MAX-digit)/10) ok=0;else runtime=runtime*10+digit;
    }
    ok=ok && lxb_enroll((unsigned)actor,runtime);
    if(!ok) __atomic_add_fetch(&lxb_failures,1,__ATOMIC_SEQ_CST);
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_snapshot_status(lua_State *L) {
    char out[256];int n=accept_ui()?snprintf(out,sizeof(out),"configured=%u bound=%u prepared=%u enrolled=%u bound_tick=%llu prepared_tick=%llu "
        "failures=%lld stage=%s actor=%u",lxb_configured,lxb_bound,lxb_prepared,lxb_enrolled,(unsigned long long)lxb_bound_tick,
        (unsigned long long)lxb_prepared_tick,(long long)lxb_failures,lxb_stage,lxb_stage_actor):0;
    if(n>0 && (size_t)n<sizeof(out)) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}

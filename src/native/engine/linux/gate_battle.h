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
/* The flight parameters built once per fixture ship at its first gate.actor (native_gate_params_init, boot bit 4096;
 * Windows params_once in lua_actor). The builder 0x14378c0 (Windows 0xea1e80, pinned whole: params_build) is the one
 * NavigationController::OnUpdateEngineParameters (0x14382f0) refreshes the slot with: controller, the Agent (+0x38;
 * Windows +0x40) plus the write selector (LX_BANK_WRITE_VA) times 0x1a0, base 0 (the call at 0x1438408, inside
 * erng_listener_tail). Without it a ship flies its first ticks on the parameters it was saved with. Stop 36 when an
 * actor refuses (Windows' stop), 35 when the pin does. */
#define LXB_PARAMS_BUILD_VA 0x14378c0u
typedef void (*lxb_params_fn)(void *controller,void *destination,unsigned char base);
static int lxb_params_enabled;
static unsigned lxb_params_done[LXB_ACTORS],lxb_params_calls;
static lxb_params_fn lxb_params_override; /* tests */
/* native_gate_params_diag (boot option 32768, Windows lockstep_params.h): every builder call on a fixture ship's
 * controller in an admitted frame on the UI thread, with its caller, the bank it fills (the Agent's +0/+0x1a0/+0x340),
 * base, the thread's time and RNG word before and after, the 24 words and the four flag bytes at +0x190 it wrote. The
 * builder's first 16 bytes (push r15 .. push rbx: six pushes and two register moves, nothing RIP-relative) become
 * jmp [rip] -> lxpt_observe and a 2-byte nop in one aligned compare-exchange; the original runs through a trampoline
 * (those 16 bytes, then jmp [rip] -> 0x14378d0). The params_build pin hashes the entry as original while it is ours. */
#define LXPT_MAX 32u
typedef struct {uint64_t source,time,rng_before,rng_after;int destination;unsigned base,view;uint32_t values[24];unsigned char flags[4];} lxpt_record;
typedef struct {uint64_t tick;unsigned count;lxpt_record records[LXPT_MAX];} lxpt_trace;
static lxpt_trace lxpt_traces[LXB_ACTORS];
static unsigned char lxpt_original[16],*lxpt_page;
static int lxpt_installed,lxpt_restored;
static uint64_t lxpt_calls,lxpt_failures;
static int lxb_params_pinned(void) {
    const lx_site *s=site_named("params_build");
    if(!s) return 0;
    if(!lxpt_installed) return site_ok(s);
    unsigned char copy[0xa30];
    if(s->length!=sizeof(copy) || !gate_read_local(LX(s->va),copy,sizeof(copy))) return 0;
    memcpy(copy,lxpt_original,16);return hash_ok(copy,s->length,s->sha256);
}
static int lxb_params_once(unsigned actor,unsigned char *controller,const unsigned char *slots) {
    if(actor<1 || actor>LXB_ACTORS || !controller || !slots) return 0;
    if(lxb_params_done[actor-1]) return 1;
    unsigned char *agent=NULL;unsigned index=2;
    if(!gate_read_local(controller+LXW_NAV_AGENT,&agent,8) || !agent || agent!=slots-LXW_AGENT_SLOTS ||
       agent!=lxb_actors[actor-1].agent) return 0;
    if(!gate_read_local(LX(LX_BANK_WRITE_VA),&index,4) || index>1) return 0;
    lxb_params_fn build=lxb_params_override?lxb_params_override:(lxb_params_fn)(void*)LX(LXB_PARAMS_BUILD_VA);
    build(controller,agent+index*LXB_BANK_STRIDE,0);
    lxb_params_done[actor-1]=1;++lxb_params_calls;return 1;
}
static lxb_params_fn lxpt_original_override; /* tests */
/* Which bound actor owns this navigation controller (at its ship's +0x200, the ship's runtime ID unchanged), else 0:
 * Windows' test in nav_move and params_trace (actor_components[i]+0x200). */
static unsigned lxb_fixture_controller(const void *controller) {
    if(lxb_bound!=LXB_ACTORS || !controller) return 0;
    for(unsigned i=0;i<LXB_ACTORS;i++) {
        void *own=NULL;uint64_t id=0;
        if(lxb_actors[i].object && gate_read_local((unsigned char*)lxb_actors[i].object+LXW_OBJ_CONTROLLER,&own,8) && own==controller &&
           gate_read_local((unsigned char*)lxb_actors[i].object+8,&id,8) && id==lxb_actors[i].runtime) return i+1;
    }
    return 0;
}
static void lxpt_observe(void *controller,void *destination,unsigned char base) {
    lxb_params_fn original=lxpt_original_override?lxpt_original_override:(lxb_params_fn)(void*)lxpt_page;
    const unsigned actor=stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread?lxb_fixture_controller(controller):0;
    if(!actor) {original(controller,destination,base);return;}
    lxpt_trace *trace=&lxpt_traces[actor-1];
    if(trace->tick!=stats.issued) {trace->tick=stats.issued;trace->count=0;}
    if(trace->count>=LXPT_MAX) {++lxpt_failures;original(controller,destination,base);return;}
    lxpt_record *r=&trace->records[trace->count++];memset(r,0,sizeof(*r));
    const unsigned char *caller=__builtin_return_address(0);
    r->source=(uint64_t)(uintptr_t)(caller-image)+LX_IMAGE_BASE;r->base=base;r->destination=-1;
    unsigned char *agent=NULL;
    if(gate_read_local((unsigned char*)controller+LXW_NAV_AGENT,&agent,8) && agent)
        for(int k=0;k<3;k++) if((unsigned char*)destination==agent+(size_t)k*LXB_BANK_STRIDE) r->destination=k;
    unsigned char *tls=gate_tls_self();uint64_t *rng=tls?gate_tls_rng(tls):NULL;const double *now=tls?gate_tls_time(tls):NULL;
    if(rng) r->rng_before=*rng; else ++lxpt_failures;
    if(now) memcpy(&r->time,now,8);
    original(controller,destination,base);
    r->rng_after=rng?*rng:0;
    if(!gate_read_local(destination,r->values,sizeof(r->values)) || !gate_read_local((unsigned char*)destination+0x190,r->flags,4)) ++lxpt_failures;
    ++lxpt_calls;
}
static int lxpt_swap(const unsigned char from[16],const unsigned char to[16]) {
    unsigned __int128 expected,desired;memcpy(&expected,from,16);memcpy(&desired,to,16);
    return gate_code_swap128(LX(LXB_PARAMS_BUILD_VA),expected,desired);
}
static void lxpt_entry(unsigned char out[16]) {
    const void *to=(const void*)lxpt_observe;out[0]=0xff;out[1]=0x25;memset(out+2,0,4);memcpy(out+6,&to,8);out[14]=0x66;out[15]=0x90;
}
static int lxpt_install(void) {
    static const unsigned char prologue[16]={0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x41,0x89,0xd4,0x55,0x48,0x89,0xfd,0x53};
    if(lxpt_installed) return 1;
    if(!lxb_params_pinned() || !gate_read_local(LX(LXB_PARAMS_BUILD_VA),lxpt_original,16) || memcmp(lxpt_original,prologue,16)) return 0;
    if(!lxpt_page) lxpt_page=gate_near_page(LX(LXB_PARAMS_BUILD_VA));
    if(!lxpt_page || mprotect(lxpt_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxpt_page,0xcc,GATE_PAGE);memcpy(lxpt_page,lxpt_original,16);
    {const void *back=LX(LXB_PARAMS_BUILD_VA+16u);lxpt_page[16]=0xff;lxpt_page[17]=0x25;memset(lxpt_page+18,0,4);memcpy(lxpt_page+22,&back,8);}
    if(mprotect(lxpt_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    unsigned char to[16];lxpt_entry(to);
    if(!lxpt_swap(lxpt_original,to)) return 0;
    lxpt_installed=1;lxpt_restored=0;return 1;
}
static void lxpt_restore(void) {
    if(!lxpt_installed) return;
    unsigned char ours[16];lxpt_entry(ours);
    if(lxpt_swap(ours,lxpt_original)) {lxpt_installed=0;lxpt_restored=1;}
}
/* gate.params(actor, 0) the calls this tick, gate.params(actor, k) the k-th: Windows' PARAMS row text. */
static int lua_params(lua_State *L) {
    double actor=p_lua_tonumber(L,1),ordinal=p_lua_tonumber(L,2);
    if((!lxpt_installed && !lxpt_restored) || GetCurrentThreadId()!=ui_thread || !isfinite(actor) || actor<1 || actor>LXB_ACTORS ||
       floor(actor)!=actor || !isfinite(ordinal) || ordinal<0 || ordinal>LXPT_MAX || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    const lxpt_trace *trace=&lxpt_traces[(unsigned)actor-1];unsigned count=trace->tick==stats.issued?trace->count:0;
    if(ordinal==0) {p_lua_pushnumber(L,count);return 1;}
    if(ordinal>count) {p_lua_pushnil(L);return 1;}
    const lxpt_record *r=&trace->records[(unsigned)ordinal-1];char out[768];
    int n=snprintf(out,sizeof(out),"tick=%llu source=%llx destination=%d base=%u time=%016llx view=%u rng_before=%016llx rng_after=%016llx flags=%02x%02x%02x%02x values=",
        (unsigned long long)trace->tick,(unsigned long long)r->source,r->destination,r->base,(unsigned long long)r->time,r->view,
        (unsigned long long)r->rng_before,(unsigned long long)r->rng_after,r->flags[0],r->flags[1],r->flags[2],r->flags[3]);
    for(unsigned i=0;i<24 && n>0 && (size_t)n<sizeof(out);i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",r->values[i]);
    if(n<=0 || (size_t)n>=sizeof(out)) {p_lua_pushnil(L);return 1;}
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
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
/* gate_actor_all.h's fixture paths: which bound actor this ship or Agent is (its ship's runtime ID unchanged), else 0. */
static unsigned lxb_fixture_ship(void *self) {
    if(lxb_bound!=LXB_ACTORS || !self) return 0;
    for(unsigned i=0;i<LXB_ACTORS;i++) if(lxb_actors[i].object==self) {
        uint64_t id=0;return gate_read_local((unsigned char*)self+8,&id,8) && id==lxb_actors[i].runtime?i+1:0;
    }
    return 0;
}
static unsigned lxb_fixture_agent(void *self) {
    if(lxb_bound!=LXB_ACTORS || !self) return 0;
    for(unsigned i=0;i<LXB_ACTORS;i++) if(lxb_actors[i].agent==self) {
        void *ship=NULL;uint64_t id=0;
        return gate_read_local((unsigned char*)self+LXW_AGENT_OWNER,&ship,8) && ship==lxb_actors[i].object &&
               gate_read_local((unsigned char*)ship+8,&id,8) && id==lxb_actors[i].runtime?i+1:0;
    }
    return 0;
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
    if(index_actor && lxb_params_enabled && !lxb_params_done[index_actor-1] &&
       (!lxb_configured || !lxb_params_once(index_actor,controller,slots))) {
        finish(36);return lxb_unavailable(L,"parameter_initialization");
    }
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
    if(lsj_diag) { /* Windows: with the spline hook installed (spline_diag), the model's spline state or none */
        unsigned char *agent=slots-LXW_AGENT_SLOTS;void *model=NULL,*model_vtable=NULL;uint32_t values[4];
        if(!gate_read_local(agent+LXF_AGENT_MODEL,&model,8)) return lxb_unavailable(L,"spline_model_read");
        if(model && !gate_read_local(model,&model_vtable,8)) return lxb_unavailable(L,"spline_type_read");
        if(model && model_vtable==(void*)LX(LX_SPLINE_VTABLE)) {
            if(!lsj_state(model,agent,values)) return lxb_unavailable(L,"spline_state");
            n+=snprintf(out+n,sizeof(out)-(size_t)n,";spline_state=%08x%08x%08x%08x",values[0],values[1],values[2],values[3]);
        } else n+=snprintf(out+n,sizeof(out)-(size_t)n,";spline_state=none");
    }
    uint64_t refresh_time;unsigned refresh_index;
    memcpy(&refresh_time,ctrl_data+LXW_NAV_TIME,8);memcpy(&refresh_index,ctrl_data+LXW_NAV_TIME+8,4);
    n+=snprintf(out+n,sizeof(out)-(size_t)n,";nav_refresh=%016llx:%u",(unsigned long long)refresh_time,refresh_index);
    if(n<=0 || (size_t)n>=sizeof(out)) return lxb_unavailable(L,"overflow");
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
/* gate.body(actor, which): Windows' body_state (lockstep_body.h) for a bound fixture ship's two physics records, the
 * declared state of native_gate_strict_physics. The X4 record is as on Windows (owner +0, the X4 body +0x10 with its Jolt
 * BodyID at +8, the scene +0x18, the shape holder +0x20 with its due time at +0x28, flags +0x38, pre +0x40, delta +0x60:
 * the shape-changed path 0xeeac0a reads +0x10/+0x18/+0x39/+0x3a alike). The scene's PhysicsSystem is at +0x20; Jolt's
 * no-lock BodyInterface starts with its lock interface pointer at PhysicsSystem+0x140 (Windows +0x1b8; JPH::
 * PhysicsSystem::Init 0x1b1cc80 stores +0x120 there), whose +8 is the BodyManager (embedded at +0x10): its bodies
 * (count +0, array +0x10), a Body's pose +0, motion properties +0x48, user data +0x50 (the record), +0x60 metadata
 * and its BodyID +0x70 are Jolt's own layout. The ship's two record slots are Windows' +0x260/+0x268, fields there sit
 * 8 lower on Linux from the hull on (+0x280 -> +0x278): the first slot whose record names this ship as its owner is
 * taken, +0x258 before +0x260, once per ship, and every read is checked as on Windows. */
#define LXB_BODY_SLOT_LOW 0x258u
#define LXB_PHYS_LOCK_IFACE 0x140u
static unsigned lxb_body_slot[LXB_ACTORS]; /* the first record slot, once found */
static int lxb_record_owned(unsigned char *object,unsigned offset) {
    unsigned char *record=NULL;void *owner=NULL;
    return gate_read_local(object+offset,&record,8) && record && gate_read_local(record,&owner,8) && owner==object;
}
/* gate_shape.h's fixture path: which bound actor this body record belongs to (its owner the actor's ship, the ship's
 * runtime ID unchanged, the record in the first (which 0) or second (1) body slot), else 0. */
static unsigned lxb_fixture_record(void *record,unsigned *which) {
    void *owner=NULL;
    if(lxb_bound!=LXB_ACTORS || !record || !gate_read_local(record,&owner,8) || !owner) return 0;
    const unsigned actor=lxb_fixture_ship(owner);
    if(!actor) return 0;
    unsigned slot=lxb_body_slot[actor-1];
    for(unsigned offset=LXB_BODY_SLOT_LOW;!slot && offset<=LXB_BODY_SLOT_LOW+8u;offset+=8u) if(lxb_record_owned(owner,offset)) slot=offset;
    if(!slot) return 0;
    for(unsigned w=0;w<2;w++) {void *r=NULL;if(gate_read_local((unsigned char*)owner+slot+w*8u,&r,8) && r==record) {*which=w;return actor;}}
    return 0;
}
static int lxb_body_state(unsigned actor,unsigned char *object,unsigned which,char *out,size_t cap) {
    if(actor<1 || actor>LXB_ACTORS || which>1 || cap<1024) return 0;
    unsigned slot=lxb_body_slot[actor-1];
    if(!slot) {
        for(unsigned offset=LXB_BODY_SLOT_LOW;offset<=LXB_BODY_SLOT_LOW+8u && !slot;offset+=8u) if(lxb_record_owned(object,offset)) slot=offset;
        if(!slot) return 0;
        lxb_body_slot[actor-1]=slot;
    }
    unsigned char *record=NULL,*xbody,*scene,*system,*iface,*manager,*bodies,*body,*motion,*holder;
    void *owner,*userdata;uint32_t id,actual_id,pose[8],mp[32],pre[8],delta[8],meta[8],flags;uint64_t count,due=0;
    if(!gate_read_local(object+slot+which*8u,&record,8)) return 0;
    if(!record) {snprintf(out,cap,"none");return 1;}
    if(!gate_read_local(record,&owner,8) || owner!=object ||
       !gate_read_local(record+0x10,&xbody,8) || !xbody || !gate_read_local(xbody+8,&id,4) || id==0xffffffffu ||
       !gate_read_local(record+0x18,&scene,8) || !scene || !gate_read_local(scene+0x20,&system,8) || !system ||
       !gate_read_local(system+LXB_PHYS_LOCK_IFACE,&iface,8) || !iface || !gate_read_local(iface+8,&manager,8) || !manager ||
       !gate_read_local(manager,&count,8) || (id&0x7fffffu)>=count || count>0x800000u ||
       !gate_read_local(manager+0x10,&bodies,8) || !bodies || !gate_read_local(bodies+(id&0x7fffffu)*8u,&body,8) || !body ||
       ((uintptr_t)body&1u) || !gate_read_local(body+0x70,&actual_id,4) || actual_id!=id ||
       !gate_read_local(body+0x50,&userdata,8) || userdata!=record ||
       !gate_read_local(body+0x48,&motion,8) || !motion || !gate_read_local(body,pose,sizeof(pose)) ||
       !gate_read_local(motion,mp,sizeof(mp)) || !gate_read_local(body+0x60,meta,sizeof(meta)) ||
       !gate_read_local(record+0x40,pre,sizeof(pre)) || !gate_read_local(record+0x60,delta,sizeof(delta)) ||
       !gate_read_local(record+0x38,&flags,4) || !gate_read_local(record+0x20,&holder,8) ||
       (holder && !gate_read_local(holder+0x28,&due,8))) return 0;
    int n=snprintf(out,cap,"flags=%08x shape_due=%016llx body_id=%08x pose=",flags,(unsigned long long)due,id);
    n+=lxb_hex_words(out+n,cap-(size_t)n,pose,8);
    n+=snprintf(out+n,cap-(size_t)n," motion=");n+=lxb_hex_words(out+n,cap-(size_t)n,mp,32);
    n+=snprintf(out+n,cap-(size_t)n," pre=");n+=lxb_hex_words(out+n,cap-(size_t)n,pre,8);
    n+=snprintf(out+n,cap-(size_t)n," delta=");n+=lxb_hex_words(out+n,cap-(size_t)n,delta,8);
    meta[4]=0; /* the BodyID is process-local; its identity was checked above (Windows does the same) */
    n+=snprintf(out+n,cap-(size_t)n," meta=");n+=lxb_hex_words(out+n,cap-(size_t)n,meta,8);
    /* Diagnostic, not in the digest (physics_bits reads pose, motion, pre, delta): the body's Jolt shape type and
     * subtype (Body+0x40 -> Shape+0x18/+0x19, Jolt's own layout). */
    {unsigned char *shape=NULL,*inner=NULL,ts[2]={0xff,0xff},is[2]={0xff,0xff};
     if(gate_read_local(body+0x40,&shape,8) && shape) (void)gate_read_local(shape+0x18,ts,2);
     /* a decorated shape (type 2) keeps its inner shape at +0x20 (Jolt DecoratedShape::mInnerShape) */
     if(shape && ts[0]==2 && gate_read_local(shape+0x20,&inner,8) && inner) (void)gate_read_local(inner+0x18,is,2);
     /* a compound inner shape (type 1): its sub-shape count (Jolt CompoundShape::mSubShapes size, +0x50) */
     uint64_t subs=UINT64_MAX;if(inner && is[0]==1) (void)gate_read_local(inner+0x50,&subs,8);
     n+=snprintf(out+n,cap-(size_t)n," shape=%02x%02x/%02x%02x:%lld",ts[0],ts[1],is[0],is[1],subs==UINT64_MAX?-1ll:(long long)subs);}
    return n>0 && (size_t)n<cap;
}
static int lua_body(lua_State *L) {
    double actor=p_lua_tonumber(L,1),which=p_lua_tonumber(L,2);char out[1024];
    if(!stats.active || GetCurrentThreadId()!=ui_thread || lxb_bound!=LXB_ACTORS || !isfinite(actor) || actor<1 || actor>LXB_ACTORS ||
       floor(actor)!=actor || !isfinite(which) || which<0 || which>1 || floor(which)!=which) {p_lua_pushnil(L);return 1;}
    const lxb_actor *a=&lxb_actors[(unsigned)actor-1];uint64_t id=0;
    if(!a->object || !gate_read_local((unsigned char*)a->object+8,&id,8) || id!=a->runtime ||
       !lxb_body_state((unsigned)actor,a->object,(unsigned)which,out,sizeof(out))) {p_lua_pushnil(L);return 1;}
    p_lua_pushlstring(L,out,strlen(out));return 1;
}
/* gate.body1_mass_census(): lockstep_body_mass.h over the BodyManager behind the first bound ship's first record, as
 * Windows (all bodies; read only), the verify for native_gate_body1_mass. */
static int lua_body1_mass_census(lua_State *L) {
    unsigned char *manager=NULL;body_mass_census_t census;char out[256];
    if(!stats.active || GetCurrentThreadId()!=ui_thread || lxb_bound!=LXB_ACTORS) {p_lua_pushnil(L);return 1;}
    for(unsigned actor=1;actor<=LXB_ACTORS && !manager;actor++) {
        const lxb_actor *a=&lxb_actors[actor-1];unsigned char *record=NULL;uint64_t id=0;unsigned slot=lxb_body_slot[actor-1];
        /* the first record's slot, as lxb_body_state finds it (the census may run before gate.body) */
        for(unsigned offset=LXB_BODY_SLOT_LOW;!slot && a->object && offset<=LXB_BODY_SLOT_LOW+8u;offset+=8u) if(lxb_record_owned(a->object,offset)) slot=offset;
        if(!a->object || !slot || !gate_read_local((unsigned char*)a->object+8,&id,8) || id!=a->runtime ||
           !gate_read_local((unsigned char*)a->object+slot,&record,8) || !record) continue;
        manager=body_mass_manager(gate_read_local,a->object,record,LXB_PHYS_LOCK_IFACE);
    }
    if(!manager || !body_mass_census(gate_read_local,manager,&census)) {p_lua_pushnil(L);return 1;}
    int n=body_mass_census_text(&census,out,sizeof(out));
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

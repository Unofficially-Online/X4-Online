/* Explicit, bounded snapshot fixture enrollment. Imported IDs select objects;
 * completed native type/macro/owner relationships are checked before binding.
 * This is not a whole-world identity or state normalization mechanism. */
typedef void *(*snapshot_find_fn)(void *,uint64_t,unsigned);
typedef struct {
    uint64_t saved,runtime;void *object;
    unsigned imports,enrolled;nav_sample first_nav;
} snapshot_fixture_actor;
static snapshot_fixture_actor snapshot_actors[12];
static SRWLOCK snapshot_lock=SRWLOCK_INIT;
static unsigned snapshot_configured,snapshot_bound,snapshot_prepared,snapshot_enrolled;
static uint64_t snapshot_bound_tick,snapshot_prepared_tick;
static world_bank_phase snapshot_bound_banks,snapshot_prepared_banks;
static volatile LONG64 snapshot_failures;
static const char *snapshot_prepare_stage="none";
static uint64_t snapshot_attempt_tick,snapshot_attempt_time;
static unsigned snapshot_attempt_actor,snapshot_attempt_ui,snapshot_attempt_main,snapshot_attempt_view;
static unsigned snapshot_attempt_model;
static const char *snapshot_flight_kinds[12];

static int snapshot_manifest(const char *text,size_t length) {
    uint64_t ids[12];
    if(!text || length!=12*16+11) return 0;
    for(unsigned i=0;i<12;i++) {
        uint64_t id=0;
        for(unsigned j=0;j<16;j++) {
            char c=text[i*17+j];unsigned d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:16;
            if(d>15) return 0;id=(id<<4)|d;
        }
        if(!id || (i<11 && text[i*17+16]!=',')) return 0;
        for(unsigned j=0;j<i;j++) if(ids[j]==id) return 0;
        ids[i]=id;
    }
    if(snapshot_configured) {
        for(unsigned i=0;i<12;i++) if(snapshot_actors[i].saved!=ids[i]) return 0;
        return 1; /* Exact UI-reload retry; no state or engine API calls. */
    }
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !world_identity_installed || !path_installed || !(path_options&1)) return 0;
    for(unsigned i=0;i<12;i++) snapshot_actors[i].saved=ids[i];
    snapshot_configured=1;return 1;
}
static void snapshot_import_capture(void *object,uint64_t runtime,uint64_t saved) {
    if(!snapshot_configured) return;
    for(unsigned i=0;i<12;i++) if(snapshot_actors[i].saved==saved) {
        AcquireSRWLockExclusive(&snapshot_lock);
        snapshot_fixture_actor *a=&snapshot_actors[i];
        if(a->imports || !object || !runtime || snapshot_bound) InterlockedIncrement64(&snapshot_failures);
        else {a->object=object;a->runtime=runtime;a->imports=1;}
        ReleaseSRWLockExclusive(&snapshot_lock);return;
    }
}
/* A hot joiner (lockstep_join.h): the fixture actors bound without an Agent (wrecks at T) and how many. */
static unsigned char snapshot_dead[12];
static unsigned snapshot_joiner_dead;
/* What the gate binds for a fixture actor, its VIEW: 'nav' a NavigationController with its Agent (bound with the Agent);
 * 'drift' a U::LinearDecelerationController, the controller X4's death listener gives a destroyed ship (its wreck's drift,
 * lockstep_wreck_watch.h) and that stays on the wreck after the drift ended (j633: 23 of its 25 wreck ships carry
 * <movement class="lineardeceleration">, decelerations long past included); 'bare' no controller (j633's other two wrecks,
 * killed by a low-attention attack, have no movement); 'other' any other
 * controller, or a NavigationController that fails a check; 'lost' the bound object is not found as the fixture's ship any
 * more. A session's capture at T writes its twelve views into the join package (join_gate_rec fixture); a joiner's bind
 * classifies its loaded twelve the same way and binds only when every view equals the session's: nav with the Agent,
 * drift and bare without one (the session's Agent of a dead actor updates no more). J1 j1-9: actor 8 (176c77) died at
 * tick 508 (game time 24.0163; the session's probe rows say 8/dead from 540, its last ACTOR row is at 480) and keeps its
 * drift controller (the save: start 24.016, 5 s), and the joiner refused it at bind_joiner_controller because the rule
 * then was 'a wreck has no controller'. */
enum {SNAPSHOT_VIEW_UNSET,SNAPSHOT_VIEW_NAV,SNAPSHOT_VIEW_DRIFT,SNAPSHOT_VIEW_BARE,SNAPSHOT_VIEW_OTHER,SNAPSHOT_VIEW_LOST,SNAPSHOT_VIEWS};
static const char *const snapshot_view_names[SNAPSHOT_VIEWS]={"unset","nav","drift","bare","other","lost"};
static const char *snapshot_view_name(unsigned view) {return view<SNAPSHOT_VIEWS?snapshot_view_names[view]:"invalid";}
#define SNAPSHOT_NAV_VTABLE 0x2c51158u    /* U::NavigationController */
#define SNAPSHOT_DRIFT_VTABLE 0x2c3f5f0u  /* U::LinearDecelerationController (WRECK_LDC_VTABLE) */
static unsigned char snapshot_bound_views[12];  /* each actor's view at its bind (a session: nav; a joiner: the session's) */
static unsigned snapshot_join_view(unsigned index); /* lockstep_join.h: the session's view in a joiner's package, else 0 */
/* The last check that refused an actor and the values it read (SNAPSHOT_BOUNDARY check=). */
static char snapshot_check[224]="-";
static const char *snapshot_object_check="-";
/* The macro's name (std::string at +0x20, length +0x30, capacity +0x38) is the fixture's ship macro. */
static int snapshot_macro_is(unsigned char *macro,const char *expected) {
    void *vtable,*getter;uint64_t length,capacity;unsigned char *name=macro+0x20;char text[64];
    if(!macro || !read_local(macro,&vtable,8) || vtable!=image+0x2ba5430 ||
       !read_local((unsigned char*)vtable+18*8,&getter,8) || getter!=image+0xefb10 ||
       !read_local(macro+0x30,&length,8) || length!=strlen(expected) || length>=sizeof(text) ||
       !read_local(macro+0x38,&capacity,8) || capacity<length) return 0;
    if(capacity>=16 && (!read_local(name,&name,8) || !name)) return 0;
    return read_local(name,text,(size_t)length+1) && !memcmp(text,expected,(size_t)length+1);
}
/* The actor's object: imported once, found by its runtime ID, keyed by its saved ID, a ship of the fixture's macro. The
 * first check that refused: snapshot_object_check (map, imports, find, identity, class, macro). */
static int snapshot_object_actor(unsigned index,snapshot_find_fn find,void *map) {
    snapshot_fixture_actor *a=&snapshot_actors[index];unsigned char *object=a->object,*macro=NULL;void *vtable;uint64_t id;
    const char *expected=index<6?"ship_arg_s_fighter_01_a_macro":"ship_xen_s_fighter_01_a_macro",*check=NULL;
    if(!find || !map) check="map";
    else if(a->imports!=1 || !a->runtime || !object) check="imports";
    else if(find(map,a->runtime,4)!=object) check="find";
    else if(world_identity_lookup(a->runtime)!=a->saved || !read_local(object+8,&id,8) || id!=a->runtime) check="identity";
    else if(!read_local(object,&vtable,8) || vtable!=image+0x2b7feb8) check="class";
    else if(!read_local(object+0x60,&macro,8) || !snapshot_macro_is(macro,expected)) check="macro";
    snapshot_object_check=check?check:"ok";return !check;
}
/* The object and its navigation controller, Agent and parameters. A refusal names its check and the values it read in
 * snapshot_check: object:<sub> (snapshot_object_actor), controller_read, controller_null, vtable (ctrl_vt the controller's
 * class, as an image offset), owner, slots, parameters, agent_owner. */
static int snapshot_native_actor(unsigned index,snapshot_find_fn find,void *map,unsigned char **slots_out) {
    snapshot_fixture_actor *a=&snapshot_actors[index];unsigned char *object=a->object,*controller=NULL,*slots=NULL,*parameters=NULL;
    void *vtable=NULL,*owner=NULL,*agent_owner=NULL;const char *check=NULL;
    if(!snapshot_object_actor(index,find,map)) check="object";
    else if(!read_local(object+0x200,&controller,8)) check="controller_read";
    else if(!controller) check="controller_null";
    else if(!read_local(controller,&vtable,8)) check="vtable_read";
    else if(vtable!=image+SNAPSHOT_NAV_VTABLE) check="vtable";
    else if(!read_local(controller+0x28,&owner,8) || owner!=object) check="owner";
    else if(!read_local(controller+0x30,&slots,8) || !slots || (uintptr_t)slots<0xad0) check="slots";
    else if(!read_local(controller+0x40,&parameters,8) || parameters!=slots-0xad0) check="parameters";
    else if(!read_local(parameters+0xef0,&agent_owner,8) || agent_owner!=object) check="agent_owner";
    uintptr_t vt=(uintptr_t)vtable,base=(uintptr_t)image;
    snprintf(snapshot_check,sizeof(snapshot_check),"check=%s%s%s ctrl=%s ctrl_vt=%llx owner=%s slots=%s parameters=%s agent_owner=%s",
             check?check:"ok",check && !strcmp(check,"object")?":":"",check && !strcmp(check,"object")?snapshot_object_check:"",
             controller?"set":"null",vt>=base && vt-base<0x6fa9000?(unsigned long long)(vt-base):0ULL,
             !owner?"-":owner==object?"object":"other",!slots?"-":(uintptr_t)slots<0xad0?"low":"set",
             !parameters?"-":slots && parameters==slots-0xad0?"agent":"other",!agent_owner?"-":agent_owner==object?"object":"other");
    if(check) return 0;
    *slots_out=slots;return 1;
}
/* The actor's view (above); *slots_out the Agent's slots for nav, else NULL. snapshot_check names the navigation check that
 * refused any other view. */
static unsigned snapshot_actor_view(unsigned index,snapshot_find_fn find,void *map,unsigned char **slots_out) {
    unsigned char *controller=NULL;void *vtable=NULL;*slots_out=NULL;
    if(snapshot_native_actor(index,find,map,slots_out)) return SNAPSHOT_VIEW_NAV;
    if(strncmp(snapshot_check,"check=object",12)==0) return SNAPSHOT_VIEW_LOST;
    if(!read_local((unsigned char*)snapshot_actors[index].object+0x200,&controller,8)) return SNAPSHOT_VIEW_OTHER;
    if(!controller) return SNAPSHOT_VIEW_BARE;
    return read_local(controller,&vtable,8) && vtable==image+SNAPSHOT_DRIFT_VTABLE?SNAPSHOT_VIEW_DRIFT:SNAPSHOT_VIEW_OTHER;
}
/* A session at its join capture (lockstep_join.h, main thread after step T): the twelve views, all 0 without a bound fixture. */
static int snapshot_session_views(unsigned char out[12],snapshot_find_fn find,void *map) {
    memset(out,0,12);
    if(!snapshot_configured || snapshot_bound!=12 || !map) return 0;
    for(unsigned i=0;i<12;i++) {unsigned char *slots;out[i]=(unsigned char)snapshot_actor_view(i,find,map,&slots);}
    return 1;
}
/* At the boot admission: tick 0 of the snapshot, or a hot joiner's session tick T (join_tick_base; its import keyed every
 * object of the join save by the session's portable ID, so the fixture's saved IDs find the same twelve: X4 renumbers what it
 * saves, the import maps it back). A joiner binds each actor as the session's gate saw it at T (its view from the package)
 * and refuses at bind_joiner_view when its loaded actor's view differs, or is neither nav, drift nor bare; the stage, actor,
 * both views and the navigation check with its values show in SNAPSHOT_BOUNDARY (J1 j1-8: bound=0 failures=1 named nothing;
 * j1-9: bind_joiner_controller actor=8 named no check). */
static int snapshot_bind(snapshot_find_fn find,void *map) {
    if(!snapshot_configured) return 1;
    uint64_t base=join_tick_base();
    snapshot_prepare_stage="bind_guard";snapshot_attempt_tick=stats.issued;snapshot_attempt_actor=0;
    if(snapshot_bound || snapshot_failures || stats.issued!=base || boot_phase!=3 || !map) return 0;
    if(!world_banks_read(&snapshot_bound_banks)) return 0;
    unsigned char *slots[12],views[12];unsigned dead=0;
    for(unsigned i=0;i<12;i++) {
        snapshot_attempt_actor=i+1;snapshot_prepare_stage="bind_registered";
        if(actor_components[i] || actor_ids[i]) return 0;
        if(!base) {
            snapshot_prepare_stage="bind_actor";
            if(!snapshot_native_actor(i,find,map,&slots[i])) return 0;
            views[i]=SNAPSHOT_VIEW_NAV;
        } else {
            snapshot_prepare_stage="bind_joiner_view";
            unsigned want=snapshot_join_view(i),have=snapshot_actor_view(i,find,map,&slots[i]);
            if(have!=want || (have!=SNAPSHOT_VIEW_NAV && have!=SNAPSHOT_VIEW_DRIFT && have!=SNAPSHOT_VIEW_BARE)) {
                char check[sizeof(snapshot_check)];memcpy(check,snapshot_check,sizeof(check));
                snprintf(snapshot_check,sizeof(snapshot_check),"session=%s joiner=%s %s",snapshot_view_name(want),snapshot_view_name(have),check);
                return 0;
            }
            views[i]=(unsigned char)have;
            if(have!=SNAPSHOT_VIEW_NAV) {slots[i]=NULL;++dead;}
        }
        snapshot_prepare_stage="bind_duplicate";
        for(unsigned j=0;j<i;j++) if(snapshot_actors[i].object==snapshot_actors[j].object || snapshot_actors[i].runtime==snapshot_actors[j].runtime) return 0;
    }
    /* Only our registration tables change on the main thread. Engine-mutating
     * preparations wait for the first simulation/UI path-release boundary. */
    for(unsigned i=0;i<12;i++) {
        actor_components[i]=snapshot_actors[i].object;actor_ids[i]=snapshot_actors[i].runtime;snapshot_dead[i]=!slots[i];
        snapshot_bound_views[i]=views[i];
        if(!slots[i]) continue;
        if(agent_installed) agent_samples[i].agent=slots[i]-0xad0;
        if(flight_installed) flight_samples[i].agent=slots[i]-0xad0;
    }
    snprintf(snapshot_check,sizeof(snapshot_check),"-");
    snapshot_joiner_dead=dead;snapshot_bound=12;snapshot_bound_tick=stats.issued;snapshot_prepare_stage="bound";snapshot_attempt_actor=0;return 1;
}
/* init=0 (a hot joiner): register the model without the session-start draw (flight_register_with). */
static int snapshot_flight_register_with(unsigned actor,unsigned char *slots,int init) {
    if(actor<1 || actor>12 || !slots) return 0;
    unsigned i=actor-1;void *model,*vtable,*owner;unsigned char *agent=slots-0xad0;
    if(!read_local(agent+0xf10,&model,8)) return 0;
    snapshot_attempt_model=0;
    if(!model) {flight_samples[i].model=NULL;snapshot_flight_kinds[i]="none";return 1;}
    if(!read_local(model,&vtable,8) || !read_local((unsigned char*)model+0x10,&owner,8) || owner!=agent) return 0;
    if((unsigned char*)vtable>=image && (unsigned char*)vtable<image+0x6fa9000) snapshot_attempt_model=(unsigned)((unsigned char*)vtable-image);
    if(vtable==image+SPLINE_ENTRY-0x70) {
        uint32_t state[4];if(!spline_state(model,agent,state)) return 0;
        /* The snapshot can restore a spline model. It is already covered by
         * the spline hook; a later steering construction uses its existing
         * constructor hook. Do not replace or initialize another model type. */
        flight_samples[i].model=NULL;snapshot_flight_kinds[i]="spline";return 1;
    }
    if(vtable!=image+FLIGHT_ENTRY-0x70) return 0;
    snapshot_flight_kinds[i]="steering";return flight_register_with(actor,slots,init);
}
static int snapshot_flight_register(unsigned actor,unsigned char *slots) {return snapshot_flight_register_with(actor,slots,1);}
/* A hot joiner at its first tick T+1 (the session's tick 1 is long past). The session prepared the fixture at tick 1 and
 * enrolled it from the MD events of RestoreActors (ticks 1..3), and every client stops (74) unless all twelve are enrolled
 * by tick 4, so at T all twelve were; those events do not repeat on a joiner (the cue ran before T, the probe raises it at
 * tick 1 only). So the joiner registers its live actors with the modules (radar unit, navigation parameters, steering
 * model) WITHOUT the session-start writes (the radar unit times, the parameter initialization and the steering
 * constructor draw were made at the session's tick 1; the joiner's actors carry what the save restored) and enrolls all
 * twelve; a wreck (view drift or bare: bound without an Agent) only by its object. */
static int snapshot_joiner_prepare(snapshot_find_fn find,void *map) {
    unsigned char *slots[12];
    snapshot_prepare_stage="joiner_identity";
    for(unsigned i=0;i<12;i++) {
        snapshot_attempt_actor=i+1;slots[i]=NULL;
        if(snapshot_dead[i] ? !snapshot_object_actor(i,find,map) : !snapshot_native_actor(i,find,map,&slots[i])) return 0;
    }
    snapshot_prepare_stage="joiner_radar_wait";snapshot_attempt_actor=0;
    if(gravidar_initial && !gravidar_wait(*(void**)(image+0x6cf1650),1000)) return 0;
    for(unsigned i=0;i<12;i++) {
        if(!slots[i]) continue;
        snapshot_attempt_actor=i+1;snapshot_prepare_stage="joiner_radar_registration";
        if(gravidar_initial && !gravidar_register_with(i+1,1)) return 0;
        snapshot_prepare_stage="joiner_parameters";
        if(params_enabled && (!agent_identity(i,slots[i]-0xad0) || !params_keep(i+1))) return 0;
        snapshot_prepare_stage="joiner_steering";
        if(flight_installed && !snapshot_flight_register_with(i+1,slots[i],0)) return 0;
    }
    for(unsigned i=0;i<12;i++) if(!snapshot_actors[i].enrolled) {snapshot_actors[i].enrolled=1;++snapshot_enrolled;}
    snprintf(snapshot_check,sizeof(snapshot_check),"-");
    snapshot_prepared=12;snapshot_prepared_tick=stats.issued;snapshot_prepare_stage="joiner_complete";snapshot_attempt_actor=0;return 1;
}
static int snapshot_prepare(void) {
    if(!snapshot_configured || snapshot_prepared) return 1;
    uint64_t base=join_tick_base();   /* a hot joiner prepares at its first tick T+1 */
    snapshot_prepare_stage="guard";snapshot_attempt_tick=stats.issued;
    snapshot_attempt_ui=GetCurrentThreadId()==ui_thread;snapshot_attempt_main=GetCurrentThreadId()==main_thread;
    if(snapshot_bound!=12 || snapshot_failures || stats.issued!=base+1 || GetCurrentThreadId()!=ui_thread) return 0;
    if(base) return snapshot_joiner_prepare((snapshot_find_fn)(image+0xced70),*(void**)(image+0x6cf1500));
    if(!world_banks_read(&snapshot_prepared_banks)) return 0;
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);double time;
    snapshot_prepare_stage="tls";
    if(tls && tls[0]) {snapshot_attempt_view=*((unsigned char*)tls[0]+0x314);read_local((unsigned char*)tls[0]+0x300,&snapshot_attempt_time,8);}
    if(!tls || !tls[0] || *((unsigned char*)tls[0]+0x314)!=1 ||
       !read_local((unsigned char*)tls[0]+0x300,&time,8) || !isfinite(time)) return 0;
    void *map=*(void**)(image+0x6cf1500);snapshot_find_fn find=(snapshot_find_fn)(image+0xced70);
    unsigned char *slots[12];
    snapshot_prepare_stage="identity";
    for(unsigned i=0;i<12;i++) {snapshot_attempt_actor=i+1;if(!snapshot_native_actor(i,find,map,&slots[i])) return 0;}
    snapshot_prepare_stage="radar_wait";snapshot_attempt_actor=0;
    if(gravidar_initial && !gravidar_wait(*(void**)(image+0x6cf1650),1000)) return 0;
    for(unsigned i=0;i<12;i++) {
        snapshot_attempt_actor=i+1;snapshot_prepare_stage="radar_registration";
        if(gravidar_initial && !gravidar_register(i+1)) return 0;
        snapshot_prepare_stage="parameters";
        if(params_enabled) {
            void *controller;unsigned write_index=*(unsigned*)(image+0x2f4b6f4);
            if(write_index>1 || !read_local((unsigned char*)actor_components[i]+0x200,&controller,8) ||
               !agent_identity(i,slots[i]-0xad0) || !params_once((params_fn)(image+0xea1e80),i+1,controller,slots[i]-0xad0+write_index*0x1a0)) return 0;
        }
        snapshot_prepare_stage="steering";
        if(flight_installed && !snapshot_flight_register(i+1,slots[i])) return 0;
    }
    snprintf(snapshot_check,sizeof(snapshot_check),"-");
    snapshot_prepared=12;snapshot_prepared_tick=stats.issued;snapshot_prepare_stage="complete";snapshot_attempt_actor=0;return 1;
}
static void snapshot_nav_observe(unsigned actor,const void *sample) {
    if(snapshot_configured && actor>=1 && actor<=12 && !snapshot_actors[actor-1].first_nav.calls)
        snapshot_actors[actor-1].first_nav=*(const nav_sample*)sample;
}
static int snapshot_enroll(unsigned actor,uint64_t runtime) {
    if(actor<1 || actor>12 || snapshot_bound!=12 || snapshot_prepared!=12 || snapshot_failures) return 0;
    snapshot_fixture_actor *a=&snapshot_actors[actor-1];uint64_t current;
    if(runtime!=a->runtime || actor_components[actor-1]!=a->object || actor_ids[actor-1]!=runtime ||
       !read_local((unsigned char*)a->object+8,&current,8) || current!=runtime || world_identity_lookup(runtime)!=a->saved) return 0;
    if(!a->enrolled) {a->enrolled=1;++snapshot_enrolled;}
    return 1;
}

/* The battle fixture on Linux (gate_battle.h): the manifest and its guards, the bind of twelve planted fixture ships
 * at the admission (their identity, class, macro, controller, owner, slots and Agent; each check refuses), the tick-1
 * preparation registering their steering models with gate_flight.h (keyed initial angles), the enrollment, and
 * gate.actor's row in Windows' format (attention, hull, the bank index, current and previous banks, cache, hull bits,
 * shield, steering lifetime, navigation refresh). Included by test_gate_linux.c. */
static double tbt_args[4];static char tbt_out[4096];static int tbt_nil;
static double tbt_number(lua_State *L,int index) {(void)L;return index>=1 && index<=3?tbt_args[index]:0;}
static void tbt_text(lua_State *L,const char *s,size_t n) {(void)L;assert(n<sizeof(tbt_out));memcpy(tbt_out,s,n);tbt_out[n]=0;}
static void tbt_nilf(lua_State *L) {(void)L;tbt_nil=1;tbt_out[0]=0;}
static double tbt_result;static void tbt_numres(lua_State *L,double n) {(void)L;tbt_result=n;}
static const char *tbt_text_arg;static const char *tbt_tolstring(lua_State *L,int index,size_t *length) {
    (void)L;(void)index;if(length) *length=tbt_text_arg?strlen(tbt_text_arg):0;return tbt_text_arg;
}
static uint64_t *tbt_word,tbt_seen;static unsigned char tbt_cheap;
static void tbt_ship_native(void *self) {(void)self;tbt_seen=*tbt_word;*tbt_word^=0x77u;}
static void tbt_agent_native(void *self,void *commands,float dt,double time,unsigned char cheap) {
    (void)self;(void)commands;(void)dt;(void)time;tbt_seen=*tbt_word;tbt_cheap=cheap;*tbt_word^=0x77u;
}
static void tbt_spline_native(void *self,void *slot,float dt,double time,unsigned char cheap,unsigned char player) {
    (void)self;(void)slot;(void)dt;(void)time;(void)player;tbt_cheap=cheap;
}
static void *tbt_params_controller,*tbt_params_destination;static unsigned tbt_params_base,tbt_params_calls;
static void tbt_params(void *controller,void *destination,unsigned char base) {
    tbt_params_controller=controller;tbt_params_destination=destination;tbt_params_base=base;++tbt_params_calls;
}
static float tbt_percent_value;static double tbt_percent_time;static void *tbt_percent_object;
static float tbt_percent(void *object,double time) {tbt_percent_object=object;tbt_percent_time=time;return tbt_percent_value;}
typedef struct {
    unsigned char object[0x300],controller[0x100],agent[0x1000],model[0x400],macro[0x40];
    char name[40];
} tbt_ship;
static tbt_ship tbt_ships[12];
static void *tbt_find_result(void *map,uint64_t runtime,unsigned kind) {
    (void)map;if(kind!=4) return NULL;
    for(unsigned i=0;i<12;i++) {uint64_t id;memcpy(&id,tbt_ships[i].object+8,8);if(id==runtime) return tbt_ships[i].object;}
    return NULL;
}
static void tbt_plant(unsigned i) {
    tbt_ship *s=&tbt_ships[i];memset(s,0,sizeof(*s));
    void *p=LX(LXB_SHIP_VTABLE);memcpy(s->object,&p,8);
    const uint64_t runtime=0x9000+i;memcpy(s->object+8,&runtime,8);
    p=s->macro;memcpy(s->object+0x60,&p,8);
    p=LX(LX_MACRO_VTABLE);memcpy(s->macro,&p,8);
    snprintf(s->name,sizeof(s->name),"%s",i<6?"ship_arg_s_fighter_01_a_macro":"ship_xen_s_fighter_01_a_macro");
    p=s->name;memcpy(s->macro+0x20,&p,8);const uint64_t length=strlen(s->name);memcpy(s->macro+0x28,&length,8);
    p=s->controller;memcpy(s->object+LXW_OBJ_CONTROLLER,&p,8);
    const unsigned attention=7;memcpy(s->object+LXW_OBJ_ATTENTION,&attention,4);
    const double hull=1234.5+i;memcpy(s->object+LXB_OBJ_HULL,&hull,8);
    p=LX(LXB_NAV_VTABLE);memcpy(s->controller,&p,8);
    p=s->object;memcpy(s->controller+LXW_NAV_OWNER,&p,8);
    p=s->agent+LXW_AGENT_SLOTS;memcpy(s->controller+LXW_NAV_SLOTS,&p,8);
    p=s->agent;memcpy(s->controller+LXW_NAV_AGENT,&p,8);
    const uint64_t refresh_time=0x4041000000000000ULL;const unsigned refresh_index=3;
    memcpy(s->controller+LXW_NAV_TIME,&refresh_time,8);memcpy(s->controller+LXW_NAV_TIME+8,&refresh_index,4);
    for(unsigned w=0;w<24;w++) {const uint32_t a=0x10000000u+w,b=0x20000000u+w;memcpy(s->agent+LXW_AGENT_SLOTS+4*w,&a,4);memcpy(s->agent+LXW_AGENT_SLOTS+LXB_BANK_STRIDE+4*w,&b,4);}
    p=s->object;memcpy(s->agent+LXW_AGENT_OWNER,&p,8);
    p=s->model;memcpy(s->agent+LXF_AGENT_MODEL,&p,8);
    p=LX(LXF_STEER_VTABLE);memcpy(s->model,&p,8);p=s->agent;memcpy(s->model+LXF_MODEL_AGENT,&p,8);
}
static const char *tbt_manifest(void) {
    static char text[12*17+1];
    for(unsigned i=0;i<12;i++) snprintf(text+i*17,18,"%016llx%s",(unsigned long long)(0x176a13+i*0x61),i<11?",":"");
    return text;
}
static void test_battle_linux(void) {
    p_lua_tonumber=tbt_number;p_lua_pushlstring=tbt_text;p_lua_pushnil=tbt_nilf;p_lua_pushnumber=tbt_numres;p_lua_tolstring=tbt_tolstring;
    ui_thread=GetCurrentThreadId();
    /* The macro getter in its slot, the bank read index 0, an identity table with the twelve. Earlier slot swaps may
     * have left the vtable's page read-only, as the loader would. */
    for(unsigned k=0;k<2;k++) {
        const uintptr_t page=(uintptr_t)LX(k?LX_BANK_READ_VA:LX_MACRO_VTABLE+19u*8u)&~(uintptr_t)(GATE_PAGE-1);
        assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));
    }
    uint64_t getter_slot_saved;memcpy(&getter_slot_saved,LX(LX_MACRO_VTABLE+19u*8u),8);
    const uint64_t getter=(uint64_t)(uintptr_t)LX(LX_MACRO_NAME_GETTER_VA);memcpy(LX(LX_MACRO_VTABLE+19u*8u),&getter,8);
    uint32_t bank_saved;memcpy(&bank_saved,LX(LX_BANK_READ_VA),4);const uint32_t zero=0;memcpy(LX(LX_BANK_READ_VA),&zero,4);
    world_identity_entry *table_saved=world_identities;int sealed_saved=world_identity_sealed,installed_saved=world_identity_installed;
    int64_t count_saved=world_identity_count;
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);world_identity_count=0;
    world_identity_sealed=0;
    for(unsigned i=0;i<12;i++) {tbt_plant(i);assert(world_identity_record(0x9000+i,0x176a13+i*0x61));}
    world_identity_sealed=1;world_identity_installed=1;
    lxb_find_override=tbt_find_result;static unsigned char map_dummy;lxb_map_override=&map_dummy;
    memset(lxb_actors,0,sizeof(lxb_actors));lxb_configured=lxb_bound=lxb_prepared=lxb_enrolled=0;lxb_failures=0;
    /* The manifest: twelve lowercase IDs before the load, refused afterwards, a UI reload repeats it. */
    const int path_saved=path_installed;const unsigned options_saved=path_options;path_installed=1;path_options=1;
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;
    tbt_text_arg="0000000000176a13";assert(!lxb_manifest(tbt_text_arg,strlen(tbt_text_arg)));
    const char *manifest=tbt_manifest();
    assert(lxb_manifest(manifest,strlen(manifest)) && lxb_configured && lxb_actors[11].saved==0x176a13+11*0x61);
    assert(lxb_manifest(manifest,strlen(manifest))); /* a reload */
    /* The bind: every check refuses on its own, then all twelve. */
    boot_phase=3;
    void *keep;memcpy(&keep,tbt_ships[4].object,8);void *other=LX(LXB_NAV_VTABLE);memcpy(tbt_ships[4].object,&other,8);
    assert(!lxb_bind() && !strcmp(lxb_stage,"class") && lxb_stage_actor==5);memcpy(tbt_ships[4].object,&keep,8);
    const char kept=tbt_ships[8].name[9];tbt_ships[8].name[9]='X';assert(!lxb_bind() && !strcmp(lxb_stage,"macro"));tbt_ships[8].name[9]=kept;
    memcpy(&keep,tbt_ships[2].controller+LXW_NAV_OWNER,8);other=tbt_ships[3].object;memcpy(tbt_ships[2].controller+LXW_NAV_OWNER,&other,8);
    assert(!lxb_bind() && !strcmp(lxb_stage,"owner"));memcpy(tbt_ships[2].controller+LXW_NAV_OWNER,&keep,8);
    memcpy(&keep,tbt_ships[6].agent+LXW_AGENT_OWNER,8);memcpy(tbt_ships[6].agent+LXW_AGENT_OWNER,&other,8);
    assert(!lxb_bind() && !strcmp(lxb_stage,"agent_owner"));memcpy(tbt_ships[6].agent+LXW_AGENT_OWNER,&keep,8);
    stats.issued=1;assert(!lxb_bind() && !strcmp(lxb_stage,"bind_guard"));stats.issued=0;
    assert(lxb_bind() && lxb_bound==12 && lxb_actors[3].object==tbt_ships[3].object && lxb_actors[3].agent==tbt_ships[3].agent);
    /* Tick 1: the steering models register with gate_flight.h and get their keyed angles. */
    memset(lxf_samples,0,sizeof(lxf_samples));lxf_installed=1;lxf_initial=1;lxf_seeded=1;
    assert(!lxb_prepare()); /* not tick 1 yet */
    stats.issued=1;assert(lxb_prepare() && lxb_prepared==12 && !strcmp(lxb_actors[0].flight,"steering") && lxf_samples[5].model==tbt_ships[5].model);
    uint64_t seed=lockstep_seed(0x5354454552494e49ULL,0,6,1,0,0);const uint32_t limit_bits=0x40c90fdbu;float limit,want;
    memcpy(&limit,&limit_bits,4);want=lxf_random(&seed,limit);assert(!memcmp(tbt_ships[5].model+LXF_MODEL_AZIMUTH,&want,4));
    /* The enrollment: the actor's own runtime ID only. */
    assert(!lxb_enroll(1,0x9001) && lxb_enroll(1,0x9000) && lxb_enrolled==1);
    tbt_text_arg="36865";tbt_args[1]=2;assert((lua_snapshot_enroll(NULL),tbt_result==1) && lxb_enrolled==2); /* 0x9001 */
    /* gate.actor: an enrolled actor's row in Windows' format; one not enrolled refuses. */
    tbt_args[1]=0x9000;tbt_args[2]=1;tbt_args[3]=0;tbt_nil=0;assert(lua_actor(NULL)==1 && !tbt_nil);
    char expect[160];snprintf(expect,sizeof(expect),"attention=7;hull=1234.5;index=0;current=100000001000000110000002");
    assert(!strncmp(tbt_out,expect,strlen(expect)));
    assert(strstr(tbt_out,";previous=200000002000000120000002") && strstr(tbt_out,";hull_bits=40934a0000000000;shield=") &&
           strstr(tbt_out,";steering=1:") && strstr(tbt_out,";nav_refresh=4041000000000000:3"));
    /* with canonical shields the row reads the shield at the thread's game time (a ship without a group: none either way) */
    {const double at=5.0;shield_canonical=1;lxb_time_override=&at;tbt_nil=0;assert(lua_actor(NULL)==1 && !tbt_nil && strstr(tbt_out,";shield=none"));
     lxb_time_override=NULL;shield_canonical=0;}
    tbt_args[1]=0x9002;tbt_args[2]=3;tbt_nil=0;assert(lua_actor(NULL)==2 && tbt_nil); /* not enrolled */
    /* The refresh: GetShieldPercent at the UI thread's game time, its percent in range (the pin: test_battle_elf). */
    const double now=123.25;lxb_time_override=&now;lxb_percent_override=tbt_percent;tbt_percent_value=50.0f;tbt_percent_time=0;lxb_pin_skip=1;
    assert(lxb_shield_refresh(tbt_ships[0].object) && tbt_percent_time==123.25 && tbt_percent_object==tbt_ships[0].object);
    tbt_percent_value=150.0f;assert(!lxb_shield_refresh(tbt_ships[0].object));
    tbt_percent_value=-1.0f;assert(!lxb_shield_refresh(tbt_ships[0].object));
    const double never=-1.0;lxb_time_override=&never;tbt_percent_value=50.0f;assert(!lxb_shield_refresh(tbt_ships[0].object));
    /* gate.actor with the refresh: a ship without a shield prints none (as Windows) and still refreshes (its percent 0). */
    lxb_time_override=&now;tbt_percent_value=0.0f;tbt_percent_object=NULL;
    tbt_args[1]=0x9000;tbt_args[2]=1;tbt_args[3]=1;tbt_nil=0;assert(lua_actor(NULL)==1 && !tbt_nil && tbt_percent_object==tbt_ships[0].object && strstr(tbt_out,";shield="));
    boot_phase=2;tbt_nil=0;assert(lua_actor(NULL)==2 && tbt_nil);boot_phase=3; /* only in the session */
    lxb_time_override=NULL;lxb_percent_override=NULL;lxb_pin_skip=0;
    /* gate_actor_all.h's fixture paths: a bound actor's ship Move and Agent update on Windows' keyed streams (actor
     * number, call ordinal), the word restored, full Agent updates; another ship or Agent is no fixture. */
    uint64_t fixture_word=0x5151515151515151ULL;laa_fixture_rng_override=&fixture_word;
    laa_ship_original_override=tbt_ship_native;laa_agent_original_override=tbt_agent_native;laa_ship_seeded=laa_agent_seeded=1;laa_agent_full=1;
    memset(laa_fixture_ship_calls,0,sizeof(laa_fixture_ship_calls));memset(laa_fixture_agent_calls,0,sizeof(laa_fixture_agent_calls));
    stats.issued=40;assert(lxb_fixture_ship(tbt_ships[4].object)==5 && lxb_fixture_agent(tbt_ships[4].agent)==5 && !lxb_fixture_ship(tbt_ships[4].agent));
    tbt_word=&fixture_word;laa_ship_move(tbt_ships[4].object);
    assert(tbt_seen==lockstep_seed(0x534849505f4d4f56ULL,40,5,0,1,0) && fixture_word==0x5151515151515151ULL);
    laa_ship_move(tbt_ships[4].object);assert(tbt_seen==lockstep_seed(0x534849505f4d4f56ULL,40,5,0,2,0));
    laa_agent_update(tbt_ships[4].agent,NULL,0.033f,10.0,1);
    assert(tbt_seen==lockstep_seed(0x4147454e545f524eULL,40,5,0,1,0) && tbt_cheap==0 && fixture_word==0x5151515151515151ULL);
    laa_agent_seeded=0;laa_agent_full=0;laa_agent_update(tbt_ships[4].agent,NULL,0.033f,10.0,1);
    assert(tbt_seen==0x5151515151515151ULL && tbt_cheap==1); /* diag only: native word, cheap as given */
    const uint64_t moved_id=0x9999;memcpy(tbt_ships[4].object+8,&moved_id,8);assert(!lxb_fixture_ship(tbt_ships[4].object) && !lxb_fixture_agent(tbt_ships[4].agent));
    const uint64_t back_id=0x9004;memcpy(tbt_ships[4].object+8,&back_id,8);
    laa_fixture_rng_override=NULL;laa_ship_original_override=NULL;laa_agent_original_override=NULL;laa_ship_seeded=laa_agent_seeded=laa_agent_full=0;tbt_word=NULL;
    /* gate_spline_join.h's fixture path: a fixture Agent's spline model refreshes (+0x148 stale) and updates fully; its
     * state (+0x110) joins gate.actor's row; another model of no fixture stays native. */
    static unsigned char spline[0x600] __attribute__((aligned(16)));memset(spline,0,sizeof(spline));
    void *sp=LX(LX_SPLINE_VTABLE);memcpy(spline,&sp,8);sp=tbt_ships[2].agent;memcpy(spline+LSJ_MODEL_AGENT,&sp,8);
    const float state_values[4]={1.5f,-2.25f,0.125f,8.0f};memcpy(spline+LSJ_MODEL_STATE,state_values,16);
    void *steering_model;memcpy(&steering_model,tbt_ships[2].agent+LXF_AGENT_MODEL,8);sp=spline;memcpy(tbt_ships[2].agent+LXF_AGENT_MODEL,&sp,8);
    lsj_diag=1;lsj_refresh=1;lsj_original_override=tbt_spline_native;const int64_t calls_before=lsj_fixture_calls;
    lsj_update(spline,NULL,0.033f,10.0,1,0);
    double stale;memcpy(&stale,spline+LSJ_MODEL_AVOIDANCE_TIME,8);assert(stale==-1.0 && tbt_cheap==0 && lsj_fixture_calls==calls_before+1);
    uint32_t got[4];assert(lsj_state(spline,tbt_ships[2].agent,got) && !memcmp(got,state_values,16) && !lsj_state(spline,tbt_ships[3].agent,got));
    tbt_args[1]=0x9002;tbt_args[2]=3;tbt_args[3]=0;lxb_actors[2].enrolled=1;tbt_nil=0;assert(lua_actor(NULL)==1 && !tbt_nil);
    assert(strstr(tbt_out,";spline_state=3fc00000c01000003e00000041000000;nav_refresh="));
    memcpy(spline+LSJ_MODEL_AGENT,&tbt_ships[11].object,8); /* no fixture's Agent: native */
    memset(spline+LSJ_MODEL_AVOIDANCE_TIME,0,8);lsj_update(spline,NULL,0.033f,10.0,1,0);memcpy(&stale,spline+LSJ_MODEL_AVOIDANCE_TIME,8);
    assert(stale==0 && tbt_cheap==1);
    memcpy(tbt_ships[2].agent+LXF_AGENT_MODEL,&steering_model,8);lxb_actors[2].enrolled=0;
    lsj_diag=lsj_refresh=0;lsj_original_override=NULL;
    /* params_init: a bound ship's flight parameters built once, at its first gate.actor, into its Agent's write bank
     * (LX_BANK_WRITE_VA's index times 0x1a0) with base 0; an Agent other than the bound one, or a selector out of
     * range, refuses. */
    {const uintptr_t page=(uintptr_t)LX(LX_BANK_WRITE_VA)&~(uintptr_t)(GATE_PAGE-1);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));}
    uint32_t write_saved;memcpy(&write_saved,LX(LX_BANK_WRITE_VA),4);const uint32_t one=1,two=2;memcpy(LX(LX_BANK_WRITE_VA),&one,4);
    lxb_params_override=tbt_params;memset(lxb_params_done,0,sizeof(lxb_params_done));lxb_params_calls=0;lxb_params_enabled=1;tbt_params_calls=0;
    tbt_args[1]=0x9000;tbt_args[2]=1;tbt_args[3]=0;tbt_nil=0;assert(lua_actor(NULL)==1 && !tbt_nil);
    assert(tbt_params_calls==1 && lxb_params_calls==1 && tbt_params_controller==tbt_ships[0].controller &&
           tbt_params_destination==tbt_ships[0].agent+LXB_BANK_STRIDE && tbt_params_base==0);
    assert(lua_actor(NULL)==1 && !tbt_nil && tbt_params_calls==1); /* once */
    const unsigned char *slots1=tbt_ships[1].agent+LXW_AGENT_SLOTS;void *agent_kept;memcpy(&agent_kept,tbt_ships[1].controller+LXW_NAV_AGENT,8);
    void *wrong=tbt_ships[2].agent;memcpy(tbt_ships[1].controller+LXW_NAV_AGENT,&wrong,8);
    assert(!lxb_params_once(2,tbt_ships[1].controller,slots1));memcpy(tbt_ships[1].controller+LXW_NAV_AGENT,&agent_kept,8);
    memcpy(LX(LX_BANK_WRITE_VA),&two,4);assert(!lxb_params_once(2,tbt_ships[1].controller,slots1));
    memcpy(LX(LX_BANK_WRITE_VA),&zero,4);assert(lxb_params_once(2,tbt_ships[1].controller,slots1) && tbt_params_destination==tbt_ships[1].agent);
    assert(!lxb_params_once(0,tbt_ships[1].controller,slots1) && !lxb_params_once(13,tbt_ships[1].controller,slots1) && tbt_params_calls==2);
    memcpy(LX(LX_BANK_WRITE_VA),&write_saved,4);
    lxb_params_override=NULL;lxb_params_enabled=0;memset(lxb_params_done,0,sizeof(lxb_params_done));lxb_params_calls=0;
    /* params_diag: a fixture controller's builder call recorded (its bank, the words it wrote), another passed through;
     * gate.params gives the count and Windows' row text. */
    {lxpt_original_override=tbt_params;lxpt_installed=1;memset(lxpt_traces,0,sizeof(lxpt_traces));lxpt_calls=lxpt_failures=0;tbt_params_calls=0;
     const uint32_t word=0x41200000u;memcpy(tbt_ships[0].agent+LXB_BANK_STRIDE,&word,4);
     stats.issued=33;lxpt_observe(tbt_ships[0].controller,tbt_ships[0].agent+LXB_BANK_STRIDE,0);
     assert(tbt_params_calls==1 && lxpt_calls==1 && lxpt_traces[0].tick==33 && lxpt_traces[0].count==1 &&
            lxpt_traces[0].records[0].destination==1 && lxpt_traces[0].records[0].values[0]==word);
     lxpt_observe(tbt_ships[1].object,tbt_ships[1].agent,0);assert(tbt_params_calls==2 && lxpt_calls==1); /* not a controller */
     tbt_args[1]=1;tbt_args[2]=0;assert(lua_params(NULL)==1 && tbt_result==1);
     tbt_args[2]=1;tbt_nil=0;assert(lua_params(NULL)==1 && !tbt_nil && strstr(tbt_out,"tick=33 ") && strstr(tbt_out," destination=1 base=0 ") &&
                                     strstr(tbt_out," values=41200000"));
     stats.issued=34;tbt_args[2]=0;assert(lua_params(NULL)==1 && tbt_result==0); /* another tick: none yet */
     lxpt_original_override=NULL;lxpt_installed=0;memset(lxpt_traces,0,sizeof(lxpt_traces));lxpt_calls=0;}
    /* gate.body: the ship's first owned record slot (+0x258), the record's X4 body and BodyID, its scene's PhysicsSystem
     * (+0x20), the no-lock lock interface (+0x140) and its BodyManager (+8), the Jolt body; Windows' text; an empty
     * second slot is none; a body whose user data is another record refuses. */
    {
        static unsigned char rec[0x80],xb[0x10],scn[0x30],sys[0x200],lock[0x10],mgr[0x18],motion_words[0x80],holder_b[0x30];
        static unsigned char jbody[0x80] __attribute__((aligned(16)));static void *body_slots[4];
        memset(rec,0,sizeof(rec));memset(jbody,0,sizeof(jbody));memset(body_slots,0,sizeof(body_slots));
        void *p=tbt_ships[0].object;memcpy(rec,&p,8);
        p=xb;memcpy(rec+0x10,&p,8);const uint32_t bid=0x01000002u;memcpy(xb+8,&bid,4);
        p=scn;memcpy(rec+0x18,&p,8);p=holder_b;memcpy(rec+0x20,&p,8);const double due=12.5;memcpy(holder_b+0x28,&due,8);
        const uint32_t record_flags=0x11u;memcpy(rec+0x38,&record_flags,4);
        for(unsigned w=0;w<8;w++) {const uint32_t a=0x100u+w,b=0x200u+w,c=0x300u+w,d=0x400u+w;memcpy(rec+0x40+4*w,&a,4);memcpy(rec+0x60+4*w,&b,4);
                                   memcpy(jbody+4*w,&c,4);memcpy(jbody+0x60+4*w,&d,4);}
        for(unsigned w=0;w<32;w++) {const uint32_t m=0x500u+w;memcpy(motion_words+4*w,&m,4);}
        p=sys;memcpy(scn+0x20,&p,8);p=lock;memcpy(sys+LXB_PHYS_LOCK_IFACE,&p,8);p=mgr;memcpy(lock+8,&p,8);
        const uint64_t body_count=4;memcpy(mgr,&body_count,8);p=body_slots;memcpy(mgr+0x10,&p,8);body_slots[2]=jbody;
        p=motion_words;memcpy(jbody+0x48,&p,8);p=rec;memcpy(jbody+0x50,&p,8);memcpy(jbody+0x70,&bid,4);
        p=rec;memcpy(tbt_ships[0].object+LXB_BODY_SLOT_LOW,&p,8);
        memset(lxb_body_slot,0,sizeof(lxb_body_slot));
        tbt_args[1]=1;tbt_args[2]=0;tbt_nil=0;assert(lua_body(NULL)==1 && !tbt_nil && lxb_body_slot[0]==LXB_BODY_SLOT_LOW);
        assert(!strncmp(tbt_out,"flags=00000011 shape_due=4029000000000000 body_id=01000002 pose=0000030000000301",80));
        assert(strstr(tbt_out," motion=0000050000000501") && strstr(tbt_out," pre=0000010000000101") && strstr(tbt_out," delta=0000020000000201") &&
               strstr(tbt_out," meta=00000400000004010000040200000403000000000000040500000406") && strstr(tbt_out," shape=ffff/ffff:-1"));
        {static unsigned char fake_shape[0x20];memset(fake_shape,0,sizeof(fake_shape));fake_shape[0x18]=1;fake_shape[0x19]=8;
         void *fs=fake_shape;memcpy(jbody+0x40,&fs,8);tbt_nil=0;assert(lua_body(NULL)==1 && !tbt_nil && strstr(tbt_out," shape=0108/ffff:-1"));
         memset(jbody+0x40,0,8);}
        tbt_args[2]=1;tbt_nil=0;assert(lua_body(NULL)==1 && !tbt_nil && !strcmp(tbt_out,"none"));
        void *other=xb;memcpy(jbody+0x50,&other,8);tbt_args[2]=0;tbt_nil=0;assert(lua_body(NULL)==1 && tbt_nil);
        tbt_args[1]=13;tbt_nil=0;assert(lua_body(NULL)==1 && tbt_nil);
        memset(tbt_ships[0].object+LXB_BODY_SLOT_LOW,0,8);memset(lxb_body_slot,0,sizeof(lxb_body_slot));
    }
    /* Restore. */
    lxf_installed=lxf_initial=lxf_seeded=0;memset(lxf_samples,0,sizeof(lxf_samples));
    memset(lxb_actors,0,sizeof(lxb_actors));lxb_configured=lxb_bound=lxb_prepared=lxb_enrolled=0;lxb_failures=0;
    lxb_find_override=NULL;lxb_map_override=NULL;
    free(world_identities);world_identities=table_saved;world_identity_sealed=sealed_saved;world_identity_installed=installed_saved;
    world_identity_count=count_saved;path_installed=path_saved;path_options=options_saved;
    memcpy(LX(LX_MACRO_VTABLE+19u*8u),&getter_slot_saved,8);memcpy(LX(LX_BANK_READ_VA),&bank_saved,4);
    stats.active=0;boot_mode=0;boot_phase=0;stats.issued=0;
    printf("battle fixture (manifest, bind, prepare, enroll, actor row, shield refresh) PASS\n");fflush(stdout);
}
/* The real executable: GetShieldPercent's pin, its holder (group+0x58) and its call of the read. */
static void test_battle_elf(void) {
    assert(lxb_shield_pinned() && lxb_params_pinned());
    /* The listener refreshes the slot with the builder: controller, [controller+0x38] + selector*0x1a0, base 0. */
    static const unsigned char refresh[]={0x8b,0x05,0x5e,0x5e,0x95,0x02,0x48,0x8d,0x14,0x40,0x48,0x8d,0x34,0x90,0x31,0xd2,
                                          0x48,0xc1,0xe6,0x05,0x48,0x03,0x77,0x38,0xe8};
    assert(!memcmp(LX(0x14383f0),refresh,sizeof(refresh)));
    int32_t to;memcpy(&to,LX(0x1438409),4);assert(0x143840du+(uint32_t)to==LXB_PARAMS_BUILD_VA);
    assert(0x14383f6u+0x2955e5eu==LX_BANK_WRITE_VA && LXW_NAV_AGENT==0x38u);
    /* params_diag's entry: installed (jmp [rip] -> the observer, nop2; the pin still holds with the entry as found), restored. */
    {unsigned char before[16];memcpy(before,LX(LXB_PARAMS_BUILD_VA),16);
     assert(lxpt_install() && lxpt_installed && LX(LXB_PARAMS_BUILD_VA)[0]==0xff && LX(LXB_PARAMS_BUILD_VA)[1]==0x25 &&
            LX(LXB_PARAMS_BUILD_VA)[14]==0x66 && LX(LXB_PARAMS_BUILD_VA)[15]==0x90 && lxb_params_pinned() &&
            !memcmp(lxpt_page,before,16) && lxpt_page[16]==0xff && lxpt_page[17]==0x25);
     lxpt_restore();assert(!lxpt_installed && lxpt_restored && !memcmp(LX(LXB_PARAMS_BUILD_VA),before,16) && lxb_params_pinned());
     munmap(lxpt_page,GATE_PAGE);lxpt_page=NULL;lxpt_restored=0;}
    static const unsigned char add[]={0x48,0x83,0xc7,0x58},call=0xe8;
    assert(!memcmp(LX(0x17d7bdb),add,4) && *LX(0x17d7be4)==call);
    int32_t rel;memcpy(&rel,LX(0x17d7be5),4);assert(0x17d7be9u+(uint32_t)rel==0x1293330u); /* the read (gate_world.h's shield_recharge) */
}

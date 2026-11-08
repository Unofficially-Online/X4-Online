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
    assert(lxb_shield_pinned());
    static const unsigned char add[]={0x48,0x83,0xc7,0x58},call=0xe8;
    assert(!memcmp(LX(0x17d7bdb),add,4) && *LX(0x17d7be4)==call);
    int32_t rel;memcpy(&rel,LX(0x17d7be5),4);assert(0x17d7be9u+(uint32_t)rel==0x1293330u); /* the read (gate_world.h's shield_recharge) */
}

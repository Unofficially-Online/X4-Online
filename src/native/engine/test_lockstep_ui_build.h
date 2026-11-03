/* The build and assets families (lockstep_ui_build.h) on a private executable region: a stand-in for every claimed
 * function behind the guard's real hook and shared routine, called the way LuaJIT calls them (struct arguments by
 * pointer, floats in xmm1 and on the stack, a struct returned through the hidden pointer). Captured in a local phase
 * (nothing runs, the predicted answers), the open batch and its flush into parts, the order with another family's
 * event, the wire check, reassembly on two "clients" (the same calls with the same contents, the same receipt), the
 * in-batch references (token, plot, condition, the plot size read at the apply), the slot rules at capture and at the
 * apply, refusals (too long, queue full, unmapped, outside a local phase, a query), torn and malformed batches, gone and
 * mismatched references. The image test: every claim is an FFI entry of the generated guard table, the pins. */
#include <stdarg.h>
static unsigned char *tub_region;
static __attribute__((aligned(16))) unsigned char tub_components[8][0x100];
static unsigned char tub_map_marker[16];
static uint64_t tub_salt;
static void *tub_find(void *map,uint64_t id,unsigned flags) {
    assert(flags==4 && map==(void*)tub_map_marker);
    return id>=0x7001 && id<=0x7006?tub_components[id-0x7000]:NULL;
}
static uint64_t tub_portable(uint64_t runtime) {return runtime>=0x7001 && runtime<=0x7006?(0xabc000+runtime)^tub_salt:0;}
/* What a stand-in saw: its claim, the check-only flag, a canonical text of every argument's contents. */
static char tub_seen[UIBUILD_CLAIMS][8192];
static unsigned tub_calls[UIBUILD_CLAIMS],tub_queries[UIBUILD_CLAIMS];
static uint64_t tub_bt6_answer=0x4242,tub_plot_answer=0x7003,tub_extend_answer=1;
static float tub_level;static unsigned tub_level_calls;
static void tub_cat(char *out,size_t cap,const char *fmt,...) {
    size_t n=strlen(out);va_list v;va_start(v,fmt);vsnprintf(out+n,cap-n,fmt,v);va_end(v);
}
static const char *tub_s(const char *s) {return s?s:"(null)";}
static void tub_describe(const uibuild_claim *c,const uint64_t a[UIBUILD_ARGS],char *out,size_t cap) {
    out[0]=0;
    for(unsigned i=0;c->spec[i];i++) {
        uint64_t v=a[i];
        switch(c->spec[i]) {
        case 'h':case 'n':break;
        case 'k':tub_cat(out,cap,"k%u;",(unsigned)(v&0xff));break;
        case 'I':case 'i':case 'B':case 't':case 'u':tub_cat(out,cap,"%llx;",(unsigned long long)v);break;
        case 'b':tub_cat(out,cap,"b%u;",(unsigned)(v&0xff));break;
        case 's':case 'z':tub_cat(out,cap,"'%s';",tub_s((const char*)(uintptr_t)v));break;
        case 'F':tub_cat(out,cap,"F%08x;",(unsigned)(uint32_t)v);break;
        case 'P':{const uint32_t *p=(const uint32_t*)(uintptr_t)v;tub_cat(out,cap,"P");for(int k=0;k<6;k++) tub_cat(out,cap,"%08x,",p[k]);tub_cat(out,cap,";");break;}
        case 'C':{const uint32_t *p=(const uint32_t*)(uintptr_t)v;tub_cat(out,cap,"C%08x,%08x,%08x;",p[0],p[1],p[2]);break;}
        case 'L': {
            const uibuild_loadout2 *l=(const uibuild_loadout2*)(uintptr_t)v;
            const uibuild_macro2 *m[4]={l->weapons,l->turrets,l->shields,l->engines};uint32_t mn[4]={l->nweapons,l->nturrets,l->nshields,l->nengines};
            for(int k=0;k<4;k++) for(uint32_t e=0;e<mn[k];e++) tub_cat(out,cap,"m%d:%s/%s/%zu/%u/%s/%s,",k,tub_s(m[k][e].macro),tub_s(m[k][e].type),m[k][e].slot,
                m[k][e].optional,tub_s(m[k][e].setting.ammo),tub_s(m[k][e].setting.mode));
            const uibuild_group2 *g[2]={l->turretgroups,l->shieldgroups};uint32_t gn[2]={l->nturretgroups,l->nshieldgroups};
            for(int k=0;k<2;k++) for(uint32_t e=0;e<gn[k];e++) tub_cat(out,cap,"g%d:%s/%s/%s/%u/%u/%s/%s,",k,tub_s(g[k][e].macro),tub_s(g[k][e].path),tub_s(g[k][e].group),
                g[k][e].count,g[k][e].optional,tub_s(g[k][e].setting.ammo),tub_s(g[k][e].setting.mode));
            for(uint32_t e=0;e<l->nammo;e++) tub_cat(out,cap,"a:%s/%u/%u,",tub_s(l->ammo[e].macro),l->ammo[e].amount,l->ammo[e].optional);
            for(uint32_t e=0;e<l->nunits;e++) tub_cat(out,cap,"u:%s/%u/%u,",tub_s(l->units[e].macro),l->units[e].amount,l->units[e].optional);
            for(uint32_t e=0;e<l->nsoftware;e++) tub_cat(out,cap,"s:%s,",tub_s(l->software[e].ware));
            tub_cat(out,cap,"t:%s/%u,",tub_s(l->thruster.macro),l->thruster.optional);
            for(uint32_t e=0;e<l->ncrew;e++) tub_cat(out,cap,"c:%s/%u/%u,",tub_s(l->crew[e].role),l->crew[e].count,l->crew[e].optional);
            tub_cat(out,cap,"x%u;",l->hascrewexperience);
            break;
        }
        case 'T': {
            const uibuild_xferinfo2 *t=(const uibuild_xferinfo2*)(uintptr_t)v;
            const uibuild_xfer2 *x[3]={t->removed,t->added,t->transferred};uint32_t xn[3]={t->nremoved,t->nadded,t->ntransferred};
            for(int k=0;k<3;k++) for(uint32_t e=0;e<xn[k];e++) tub_cat(out,cap,"T%d:%s/%llx/%u/%lld,",k,tub_s(x[k][e].role),(unsigned long long)x[k][e].seed,x[k][e].amount,(long long)x[k][e].price);
            tub_cat(out,cap,";");break;
        }
        case 'X': {
            const uibuild_task6info *x=(const uibuild_task6info*)(uintptr_t)v;
            if(!x) {tub_cat(out,cap,"X-;");break;}
            for(uint32_t e=0;e<x->nblacklists;e++) tub_cat(out,cap,"bl%d/%s,",x->blacklists[e].id,tub_s(x->blacklists[e].type));
            for(uint32_t e=0;e<x->nfightrules;e++) tub_cat(out,cap,"fr%d/%s,",x->fightrules[e].id,tub_s(x->fightrules[e].type));
            tub_cat(out,cap,"p%s;",tub_s(x->paint));break;
        }
        case 'G':{const uibuild_actor *g=(const uibuild_actor*)(uintptr_t)v;tub_cat(out,cap,"G%llx/%llx/%llx;",(unsigned long long)g->entity,(unsigned long long)g->personcontrollable,(unsigned long long)g->seed);break;}
        case 'N':{const uint64_t *s=(const uint64_t*)(uintptr_t)v;uint32_t n=(uint32_t)a[i+1];tub_cat(out,cap,"N%u:",n);for(uint32_t e=0;e<n;e++) tub_cat(out,cap,"%llx,",(unsigned long long)s[e]);tub_cat(out,cap,";");break;}
        case 'R':{const uibuild_xfer *x=(const uibuild_xfer*)(uintptr_t)v;uint32_t n=(uint32_t)a[i+1];tub_cat(out,cap,"R%u:",n);
            for(uint32_t e=0;e<n;e++) tub_cat(out,cap,"%s/%llx/%u,",tub_s(x[e].role),(unsigned long long)x[e].seed,x[e].amount);tub_cat(out,cap,";");break;}
        default:break;
        }
    }
}
static const char tub_ok_reason[]="",tub_no_reason[]="NotPossible";
static uint64_t tub_record(unsigned claim,uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7,uint64_t a8,uint64_t a9,uint64_t a10,uint64_t a11) {
    const uibuild_claim *c=&uibuild_claims[claim];uint64_t a[UIBUILD_ARGS]={a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11};
    const char *k=strchr(c->spec,'k');unsigned query=k && (a[k-c->spec]&0xff);
    if(query) ++tub_queries[claim];else ++tub_calls[claim];
    if(!query) tub_describe(c,a,tub_seen[claim],sizeof(tub_seen[claim]));
    if(!strcmp(c->name,"AddBuildTask6")) return tub_bt6_answer;
    if(!strcmp(c->name,"ReserveBuildPlot")) return tub_plot_answer;
    if(!strcmp(c->name,"ExtendBuildPlot")) return tub_extend_answer;
    if(c->ret==UIBUILD_RET_REASON) return (uint64_t)(uintptr_t)(query?tub_no_reason:tub_ok_reason);
    if(c->ret==UIBUILD_RET_STRUCT_REASON) {const char *r=query?tub_no_reason:tub_ok_reason;memcpy((void*)(uintptr_t)a0,&r,8);return a0;}
    return c->ret==UIBUILD_RET_VOID?0xdeadbeefu:0x101;   /* a bool in al */
}
#define TUB(n) static uint64_t tub_fn_##n(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7,uint64_t a8,uint64_t a9,uint64_t a10,uint64_t a11) \
    {return tub_record(n,a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11);}
TUB(0) TUB(1) TUB(2) TUB(3) TUB(4) TUB(5) TUB(6) TUB(8) TUB(9) TUB(10) TUB(11) TUB(12) TUB(13) TUB(14) TUB(15) TUB(16) TUB(17)
TUB(18) TUB(19) TUB(20) TUB(21) TUB(22) TUB(23) TUB(24) TUB(25) TUB(26) TUB(27) TUB(28) TUB(29) TUB(30)
static void tub_float(uint64_t station,float v) {assert(station==0x7001);tub_level=v;++tub_level_calls;++tub_calls[7];}
static void *const tub_fns[]={(void*)tub_fn_0,(void*)tub_fn_1,(void*)tub_fn_2,(void*)tub_fn_3,(void*)tub_fn_4,(void*)tub_fn_5,(void*)tub_fn_6,(void*)tub_float,
    (void*)tub_fn_8,(void*)tub_fn_9,(void*)tub_fn_10,(void*)tub_fn_11,(void*)tub_fn_12,(void*)tub_fn_13,(void*)tub_fn_14,(void*)tub_fn_15,(void*)tub_fn_16,
    (void*)tub_fn_17,(void*)tub_fn_18,(void*)tub_fn_19,(void*)tub_fn_20,(void*)tub_fn_21,(void*)tub_fn_22,(void*)tub_fn_23,(void*)tub_fn_24,(void*)tub_fn_25,
    (void*)tub_fn_26,(void*)tub_fn_27,(void*)tub_fn_28,(void*)tub_fn_29,(void*)tub_fn_30};
_Static_assert(sizeof(tub_fns)/sizeof(*tub_fns)==UIBUILD_CLAIMS,"a stand-in per claim");
/* The plot's size and centre at the apply (GetBuildPlotSize, GetBuildPlotCenterOffset). */
static float tub_plot_size[3]={7000,8000,9000},tub_plot_center[3]={1,2,3};
static void *tub_get_size(void *out,uint64_t station) {assert(station==0x7003);memcpy(out,tub_plot_size,12);return out;}
static void *tub_get_center(void *out,uint64_t station) {assert(station==0x7003);memcpy(out,tub_plot_center,12);return out;}
/* The guard table of the stand-ins: entry i at 0x1000 + 0x40*i: mov [rsp+8],rbx; mov rax,stand-in; jmp rax. One more
 * entry that no family claims (the guard refuses it as before). */
#define TUB_ENTRIES (UIBUILD_CLAIMS+1)
static uiguard_entry tub_table[TUB_ENTRIES];
static unsigned tub_rva(unsigned i) {return 0x1000u+0x40u*i;}
static void tub_table_build(void) {
    static const unsigned char pro[5]={0x48,0x89,0x5c,0x24,0x08};
    for(unsigned i=0;i<TUB_ENTRIES;i++) {
        unsigned char *p=tub_region+tub_rva(i);memcpy(p,pro,5);
        void *target=i<UIBUILD_CLAIMS?tub_fns[i]:(void*)tub_fn_0;
        p[5]=0x48;p[6]=0xb8;memcpy(p+7,&target,8);p[15]=0xff;p[16]=0xe0;
        uiguard_entry *e=&tub_table[i];memset(e,0,sizeof(*e));
        e->name=i<UIBUILD_CLAIMS?uibuild_claims[i].name:"Unclaimed";e->rva=tub_rva(i);e->kind=UIGUARD_FFI;
        e->group=i<UIBUILD_CLAIMS && uibuild_claims[i].family==UIREP_FAMILY_ASSETS?UIGUARD_G_ASSETS:UIGUARD_G_BUILD;
        e->checkonly=-1;e->prefix=5;memcpy(e->bytes,pro,5);e->pin=5;e->sha="-";
        if(i<UIBUILD_CLAIMS) {
            const char *k=strchr(uibuild_claims[i].spec,'k');if(k) e->checkonly=(signed char)(k-uibuild_claims[i].spec);
            if(uibuild_claims[i].ret==UIBUILD_RET_STRUCT_REASON) {e->ret=UIGUARD_RET_STRUCT;e->size=24;e->strings=1;}
            if(uibuild_claims[i].ret==UIBUILD_RET_REASON) e->ret=UIGUARD_RET_STRING;
        }
    }
}
static unsigned tub_claim(const char *name) {for(unsigned i=0;i<UIBUILD_CLAIMS;i++) if(!strcmp(uibuild_claims[i].name,name)) return i;assert(0);return 0;}
static void *tub_entry(const char *name) {return tub_region+tub_rva(tub_claim(name));}
typedef uint64_t (*tub_bt6_fn)(uint64_t,uint64_t,const char*,const uibuild_loadout2*,int64_t,const uibuild_xferinfo2*,unsigned char,const char*,const uibuild_task6info*);
typedef void (*tub_money_fn)(uint64_t,int64_t);
typedef uint64_t (*tub_reserve_fn)(uint64_t,const char*,const char*,const float*,float,float,float);
typedef void (*tub_pay_fn)(uint64_t,const float*,const float*);
typedef unsigned char (*tub_extend_fn)(uint64_t,const float*,const float*,unsigned char);
typedef void (*tub_level_fn)(uint64_t,float);
typedef struct {const char *reason;uint64_t person,partner;} tub_exchange_result;
typedef tub_exchange_result (*tub_exchange_fn)(uint64_t,uint64_t,const uint64_t*,uint32_t,const uint64_t*,uint32_t,uint64_t,uint64_t,unsigned char,unsigned char);
typedef const char *(*tub_assign_fn)(const uibuild_actor*,uint64_t,const char*,const char*,unsigned char);
typedef void (*tub_reassign_fn)(uint64_t,const uibuild_xfer*,uint32_t);
typedef unsigned char (*tub_take_fn)(uint64_t,unsigned char,unsigned char);
typedef unsigned char (*tub_mod_fn)(uint64_t,const char*);
typedef unsigned char (*tub_paint_fn)(uint64_t,const char*,unsigned char);
typedef void (*tub_dismantle_fn)(uint64_t);
/* The parts this client queued, in order (the wire's), then the queue is empty. */
typedef struct {unsigned op,length;unsigned char data[UIREP_EVENT_MAX];} tub_event;
static tub_event tub_wire[UIREP_QUEUE];static unsigned tub_wire_count;
static void tub_take_queue(void) {
    tub_wire_count=0;
    while(uirep_queue_count) {
        const uirep_event *e=&uirep_queue[uirep_queue_head%UIREP_QUEUE];
        tub_wire[tub_wire_count].op=e->op;tub_wire[tub_wire_count].length=e->length;memcpy(tub_wire[tub_wire_count].data,e->data,e->length);++tub_wire_count;
        uirep_queue_head=(uirep_queue_head+1)%UIREP_QUEUE;--uirep_queue_count;
    }
}
/* One client's apply of the wire's parts of slot `slot`: every record stepped, Lua records answered ok; the run's rows. */
static char tub_rows[4096];
static int tub_apply(unsigned slot,uint64_t tick,unsigned *lua_ids,unsigned *lua_count) {
    tub_rows[0]=0;if(lua_count) *lua_count=0;
    InterlockedExchange(&uirep_replaying,1);uirep_ui_depth=0;
    int result=1;
    for(unsigned i=0;i<tub_wire_count && result>0;i++) {
        if(tub_wire[i].op!=UIBUILD_OP_PART) continue;
        const char *why=NULL;int r=uibuild_part(slot,tub_wire[i].data,tub_wire[i].length,&why);
        if(r<0) {tub_cat(tub_rows,sizeof(tub_rows),"part:%s|",why);result=r;break;}
        if(r==0) continue;
        for(;;) {
            char what[128];unsigned lua=0,length=0;const unsigned char *payload=NULL;const char *w2=NULL;
            int s=uibuild_step(slot,tick,what,sizeof(what),&lua,&payload,&length,&w2,NULL);
            if(s<0) {tub_cat(tub_rows,sizeof(tub_rows),"stop:%s|",w2);result=-3;break;}
            if(s==0) {tub_cat(tub_rows,sizeof(tub_rows),"done|");break;}
            if(s==2) {char hex[2*UIBUILD_BATCH_MAX+1];pilot_hex(payload,length,hex);tub_cat(tub_rows,sizeof(tub_rows),"lua%u:%s|",lua,hex);if(lua_ids) lua_ids[(*lua_count)++]=lua;assert(uibuild_lua_done(slot,1));continue;}
            tub_cat(tub_rows,sizeof(tub_rows),"%s|",what);
        }
    }
    InterlockedExchange(&uirep_replaying,0);
    return result;
}
static void tub_reset_counts(void) {memset(tub_calls,0,sizeof(tub_calls));memset(tub_queries,0,sizeof(tub_queries));memset(tub_seen,0,sizeof(tub_seen));tub_level_calls=0;}
static void tub_capture_phase(int on) {uirep_ui_depth=on?1:0;}
/* Another family's claim (the dispatch keeps one family per entry). */
static int test_ub_other_claim(unsigned index,const uint64_t *frame,uint64_t *ret) {(void)index;(void)frame;(void)ret;return 0;}
static void test_ui_build(void) {
    unsigned char *saved_base=pnat_base,*saved_image=image;DWORD saved_ui=ui_thread;uint64_t saved_issued=stats.issued,saved_stop=pilot_stop,saved_start=pstk_capture_start;
    int saved_capturing=pstk_capturing;unsigned saved_capture_slot=pstk_capture_slot,saved_families=uirep_families;uirep_fns saved_fn=uirep_fn;
    unsigned char *region=VirtualAlloc(NULL,TEST_PNAT_REGION,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(region);
    tub_region=region;pnat_base=image=region;ui_thread=GetCurrentThreadId();memset(region,0xcc,0x4000);
    {void *map=tub_map_marker;memcpy(region+UIREP_MAP,&map,8);}
    uirep_fn=(uirep_fns){.find=tub_find,.portable=tub_portable};
    tub_table_build();
    uiguard_tab=tub_table;uiguard_count=TUB_ENTRIES;uiguard_region=NULL;uiguard_installed=0;uiguard_armed=0;memset(uiguard_on,0,sizeof(uiguard_on));
    uiguard_unclaim(NULL);uibuild_armed=0;
    uirep_armed=1;uirep_failed=0;uirep_stopped=0;uirep_replaying=0;uirep_queue_head=uirep_queue_count=0;uirep_refusal_head=uirep_refusal_tail=0;
    pstk_capturing=1;pstk_capture_start=1;stats.issued=10;pilot_stop=100000;pstk_capture_slot=1;rid_probe_frame=0;pres_open=0;render_rng_in_slot=0;uirep_dispatch_depth=0;
    /* Arming: what it needs. */
    uirep_families=UIREP_FAMILY_TRAVEL;assert(!uibuild_arm_using(0) && !strcmp(uibuild_arm_why,"needs_family"));
    uirep_families=UIBUILD_FAMILIES;assert(!uibuild_arm_using(0) && !strcmp(uibuild_arm_why,"needs_ui_guard"));
    uiguard_groups=0;assert(uiguard_arm_using(1u<<UIGUARD_G_FLIGHT));   /* the guard's groups do not select build or assets */
    tub_table[3].name="NotPayBuildPlotSize";assert(!uibuild_arm_using(0) && !strcmp(uibuild_arm_why,"claim:PayBuildPlotSize"));tub_table[3].name=uibuild_claims[3].name;
    /* Another family's claim on one of the entries refuses the arming (and leaves nothing claimed); then the real one. */
    assert(uiguard_claim(uibuild_guard_of[0],test_ub_other_claim) && !uibuild_arm_using(0) && !strncmp(uibuild_arm_why,"claim_taken:",12));
    for(unsigned i=0;i<UIGUARD_MAX;i++) assert(uiguard_claim_by[i]!=uibuild_claim_decide);
    uiguard_unclaim(test_ub_other_claim);
    /* An entry an armed family covers with its own hook is never claimed (two patches at one entry). */
    tub_table[uibuild_guard_of[0]].covered=UIREP_FAMILY_BUILD;
    assert(!uibuild_arm_using(0) && !strncmp(uibuild_arm_why,"claim_covered:",14));
    tub_table[uibuild_guard_of[0]].covered=0;
    assert(uibuild_arm_using(0) && uibuild_armed && uiguard_claim_by[uibuild_guard_of[0]]==uibuild_claim_decide);
    assert(uibuild_arm_using(0) && !uibuild_arm_using(1) && !strcmp(uibuild_arm_why,"rerun"));
    uibuild_fn.entry=NULL;uibuild_fn.plot_size=tub_get_size;uibuild_fn.plot_center=tub_get_center;
    /* The guard installs every claimed entry although its groups do not hold them; the unclaimed one stays as it was. */
    assert(uiguard_install() && uiguard_intact());uiguard_live=1;
    for(unsigned i=0;i<UIBUILD_CLAIMS;i++) assert(uiguard_on[i] && region[tub_rva(i)]==0xe9);
    assert(!uiguard_on[UIBUILD_CLAIMS] && region[tub_rva(UIBUILD_CLAIMS)]==0x48);
    /* Outside a local phase X4's function runs at once; nothing is captured. */
    tub_reset_counts();tub_capture_phase(0);
    assert(((tub_mod_fn)tub_entry("InstallShipMod"))(0x7002,"modx")==1 && tub_calls[tub_claim("InstallShipMod")]==1 && !uibuild_open_records);
    /* ---- a ship purchase as menu_ship_configuration.lua's buttonConfirm makes it: money (a Lua record), the build task, its
     * transferred money with the token the menu got back. ---- */
    static uibuild_macro2 weapons[24];static char macros[24][40];
    for(unsigned i=0;i<24;i++) {snprintf(macros[i],sizeof(macros[i]),"weapon_gen_s_laser_%02u_mk1_macro",i%6);
        weapons[i]=(uibuild_macro2){macros[i],"weapon",i,(unsigned char)(i&1),{i%3?"ammo_x_macro":NULL,"default"}};}
    uibuild_group2 turrets[2]={{"turret_a_macro","..","group_1",2,0,{NULL,"attackenemies"}},{"turret_b_macro","..","group_2",4,1,{"",""}}};
    uibuild_ammo ammo[1]={{"missile_a_macro",8,0}},units[1]={{"ship_gen_xs_cargodrone_empty_01_a_macro",2,1}};
    uibuild_software software[2]={{"software_dockmk2"},{"software_scannerlongrangemk1"}};uibuild_crewdata crew[1]={{"service",3,0}};
    uibuild_loadout2 loadout={0};loadout.weapons=weapons;loadout.nweapons=24;loadout.turretgroups=turrets;loadout.nturretgroups=2;loadout.ammo=ammo;loadout.nammo=1;
    loadout.units=units;loadout.nunits=1;loadout.software=software;loadout.nsoftware=2;loadout.thruster=(uibuild_vmacro){"thruster_gen_s_allround_01_mk1_macro",0};
    loadout.ncrew=1;loadout.crew=crew;loadout.hascrewexperience=1;
    uibuild_xfer2 added[2]={{"service",0x1111222233334444ULL,1,500},{"marine",0x5555ULL,2,-7}};
    uibuild_xferinfo2 xfer={0};xfer.added=added;xfer.nadded=2;
    uibuild_typeid blacklists[1]={{3,"sectortravel"}},fightrules[2]={{-1,"attack"},{7,"defend"}};
    uibuild_task6info info={blacklists,1,fightrules,2,NULL};
    /* What the stand-in sees when the menu's call runs as it is: the reference for the apply. */
    static char expected[8192];tub_reset_counts();
    ((tub_bt6_fn)tub_fns[0])(0x7001,0,"ship_arg_s_scout_01_a_macro",&loadout,120000,&xfer,0,"Probe One",&info);
    memcpy(expected,tub_seen[0],sizeof(expected));assert(strstr(expected,"m0:weapon_gen_s_laser_05_mk1_macro/weapon/23/1/ammo_x_macro/default"));
    tub_reset_counts();tub_capture_phase(1);
    uint64_t token=((tub_bt6_fn)tub_entry("AddBuildTask6"))(0x7001,0,"ship_arg_s_scout_01_a_macro",&loadout,120000,&xfer,0,"Probe One",&info);
    assert((token&0xffffffffff000000ULL)==UIBUILD_TOKEN && !tub_calls[0] && uibuild_open_records==1);
    {const char *why="";static const unsigned char money[3]={1,2,3};
     /* a Lua record lands behind it; the money call came first in the menu, the test's order is the record's order */
     assert(uibuild_add_lua(1,money,3,0,&why) && uibuild_open_records==2);}
    ((tub_money_fn)tub_entry("SetBuildTaskTransferredMoney"))(token,120000);
    assert(uibuild_open_records==3 && !tub_calls[1]);
    /* An unknown token is refused (nothing queued). */
    ((tub_money_fn)tub_entry("SetBuildTaskTransferredMoney"))(token+1,5);assert(uibuild_open_records==3);
    {char rows[256];assert(uibuild_refusals_text(rows,sizeof(rows)) && strstr(rows,":SetBuildTaskTransferredMoney:token"));}
    unsigned strings=uibuild_open_strings;assert(strings>=20 && strings<32);   /* 24 weapons, six macros: each once */
    /* Another family's event closes the batch first: the parts, then that event. */
    {unsigned char other[2]={9,9};tub_capture_phase(0);assert(uirep_push(UIREP_OP_LUA_CALL,other,2));}
    assert(!uibuild_open_records && uirep_queue_count>=3);
    tub_take_queue();
    {unsigned parts=0;for(unsigned i=0;i<tub_wire_count;i++) if(tub_wire[i].op==UIBUILD_OP_PART) {assert(i==parts);++parts;}
     assert(parts>=2 && tub_wire[tub_wire_count-1].op==UIREP_OP_LUA_CALL);
     /* the wire's section check accepts the parts, refuses a part without the families */
     const char *why=NULL;assert(uirep_event_ok(1,UIBUILD_OP_PART,tub_wire[0].data,tub_wire[0].length,&why));
     unsigned f=uirep_families;uirep_families=UIREP_FAMILY_TRAVEL;assert(!uirep_event_ok(1,UIBUILD_OP_PART,tub_wire[0].data,tub_wire[0].length,&why) && !strcmp(why,"ui_build"));uirep_families=f;
     unsigned char bad[4]={0,3,3,0};assert(!uibuild_event_ok(1,UIBUILD_OP_PART,bad,4,&why));}
    /* Client A and client B apply the same wire: the stand-ins see the menu's contents, the token is X4's build task ID. */
    uint64_t hashes[2];char rows_a[4096];
    for(int client=0;client<2;client++) {
        tub_reset_counts();memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));uibuild_frame_tick=0;
        unsigned lua[8],lua_count=0;assert(tub_apply(1,500,lua,&lua_count)==1);
        assert(tub_calls[0]==1 && !strcmp(tub_seen[0],expected) && tub_calls[1]==1 && lua_count==1 && lua[0]==1);
        assert(!strcmp(tub_seen[1],"4242;1d4c0;"));
        assert(strstr(tub_rows,"AddBuildTask6=16962|lua1:010203|SetBuildTaskTransferredMoney|done|"));
        char frame[96];assert(uibuild_frame_text(500,frame,sizeof(frame)) && strstr(frame,"UIBUILD_1:500:1:"));
        hashes[client]=uibuild_frame_hash;if(!client) memcpy(rows_a,tub_rows,sizeof(rows_a));else assert(!strcmp(rows_a,tub_rows));
    }
    assert(hashes[0]==hashes[1] && hashes[0]);
    /* X4 answers 0 at the apply: the transferred money is skipped, the same everywhere; the receipt differs from the above. */
    tub_bt6_answer=0;tub_reset_counts();memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));
    assert(tub_apply(1,501,NULL,NULL)==1 && tub_calls[0]==1 && !tub_calls[1] && strstr(tub_rows,"SetBuildTaskTransferredMoney skipped=depends"));
    assert(uibuild_frame_hash!=hashes[0]);tub_bt6_answer=0x4242;
    /* ---- a plot: ReserveBuildPlot (floats on the stack) answers 0, the PayBuildPlotSize(0) behind it takes the new plot ---- */
    tub_reset_counts();tub_capture_phase(1);
    float location[6]={1.5f,-2.25f,1e6f,0.5f,0,-0.125f},size[3]={4000,4000,4000},center[3]={10,20,30};
    static char expected_reserve[8192];
    assert(((tub_reserve_fn)tub_entry("ReserveBuildPlot"))(0x7004,"player","factory",location,4000.0f,5000.5f,-6000.25f)==0);
    ((tub_pay_fn)tub_entry("PayBuildPlotSize"))(0,size,center);
    /* ExtendBuildPlot answers true; the PayBuildPlotSize right behind it is conditional and reads the size at the apply. */
    assert(((tub_extend_fn)tub_entry("ExtendBuildPlot"))(0x7003,size,center,1)==1);
    ((tub_pay_fn)tub_entry("PayBuildPlotSize"))(0x7003,center,center);
    ((tub_level_fn)tub_entry("SetStationModuleLoadoutLevel"))(0x7001,0.625f);
    assert(uibuild_open_records==5 && !tub_calls[2] && !tub_calls[3] && !tub_level_calls);
    tub_capture_phase(0);
    ((tub_reserve_fn)tub_fns[2])(0x7004,"player","factory",location,4000.0f,5000.5f,-6000.25f);memcpy(expected_reserve,tub_seen[2],sizeof(expected_reserve));
    {float f[3]={4000.0f,5000.5f,-6000.25f};uint32_t b[3];memcpy(b,f,12);char want[64];snprintf(want,sizeof(want),"F%08x;F%08x;F%08x;",b[0],b[1],b[2]);
     assert(strstr(expected_reserve,want) && strstr(expected_reserve,"7004;'player';'factory';P3fc00000,c0100000,"));}
    tub_reset_counts();assert(uibuild_flush("test"));tub_take_queue();memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));
    assert(tub_apply(1,502,NULL,NULL)==1);
    assert(!strcmp(tub_seen[2],expected_reserve) && tub_calls[3]==2 && tub_level_calls==1 && tub_level==0.625f);
    {char want[160];uint32_t s0,s1,s2;memcpy(&s0,&tub_plot_size[0],4);memcpy(&s1,&tub_plot_size[1],4);memcpy(&s2,&tub_plot_size[2],4);
     snprintf(want,sizeof(want),"7003;C%08x,%08x,%08x;",s0,s1,s2);assert(!strncmp(tub_seen[3],want,strlen(want)));}   /* the second: sizes as X4 has them now */
    assert(strstr(tub_rows,"ReserveBuildPlot=28675|PayBuildPlotSize|ExtendBuildPlot=1|PayBuildPlotSize|SetStationModuleLoadoutLevel|done|"));
    /* The same batch where the extension fails at the apply: the payment behind it is skipped. */
    tub_extend_answer=0;tub_reset_counts();memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));
    assert(tub_apply(1,503,NULL,NULL)==1 && tub_calls[3]==1 && strstr(tub_rows,"ExtendBuildPlot=0|PayBuildPlotSize skipped=condition"));tub_extend_answer=1;
    /* ReserveBuildPlot answers 0 at the apply: its bound payment is skipped. */
    tub_plot_answer=0;tub_reset_counts();memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));
    assert(tub_apply(1,504,NULL,NULL)==1 && strstr(tub_rows,"ReserveBuildPlot=0|PayBuildPlotSize skipped=depends"));tub_plot_answer=0x7003;
    /* ---- crew: the exchange answers with X4's own answer to the same call as a query, the apply runs it for real ---- */
    tub_reset_counts();tub_capture_phase(1);
    uint64_t left[3]={0xa1,0xa2,0xa3},right[1]={0xb1};
    tub_exchange_result res=((tub_exchange_fn)tub_entry("PerformCrewExchange2"))(0x7001,0x7002,left,3,right,1,0xa1,0,1,0);
    assert(res.reason==tub_no_reason && tub_queries[tub_claim("PerformCrewExchange2")]==1 && !tub_calls[tub_claim("PerformCrewExchange2")]);
    uibuild_actor actor={0,0x7002,0xcafe};
    const char *reason=((tub_assign_fn)tub_entry("AssignHiredActor"))(&actor,0x7001,"aipilot",NULL,0);
    assert(reason==tub_no_reason && tub_queries[tub_claim("AssignHiredActor")]==1);
    /* a query passes as it is (the menus ask to enable a button) */
    assert(((tub_take_fn)tub_entry("TakeShipFromInternalStorage"))(0x7002,0,1)==1 && tub_queries[tub_claim("TakeShipFromInternalStorage")]==1);
    uibuild_xfer reassigned[2]={{"marine",0x51,1},{NULL,0x52,2}};
    ((tub_reassign_fn)tub_entry("ReassignPeople"))(0x7001,reassigned,2);
    assert(((tub_take_fn)tub_entry("TakeShipFromInternalStorage"))(0x7002,1,0)==1 && tub_queries[tub_claim("TakeShipFromInternalStorage")]==2);
    assert(uibuild_open_records==4);
    tub_capture_phase(0);
    static char expected_ex[8192],expected_as[8192],expected_re[8192];
    {tub_exchange_result r2=((tub_exchange_fn)tub_fns[tub_claim("PerformCrewExchange2")])(0x7001,0x7002,left,3,right,1,0xa1,0,1,0);(void)r2;}
    memcpy(expected_ex,tub_seen[tub_claim("PerformCrewExchange2")],sizeof(expected_ex));
    ((tub_assign_fn)tub_fns[tub_claim("AssignHiredActor")])(&actor,0x7001,"aipilot",NULL,0);memcpy(expected_as,tub_seen[tub_claim("AssignHiredActor")],sizeof(expected_as));
    ((tub_reassign_fn)tub_fns[tub_claim("ReassignPeople")])(0x7001,reassigned,2);memcpy(expected_re,tub_seen[tub_claim("ReassignPeople")],sizeof(expected_re));
    assert(strstr(expected_ex,"N3:a1,a2,a3,;N1:b1,;a1;0;b1;k0;") && strstr(expected_as,"G0/7002/cafe;7001;'aipilot';'(null)';k0;") && strstr(expected_re,"R2:marine/51/1,(null)/52/2,;"));
    tub_reset_counts();assert(uibuild_flush("test"));tub_take_queue();memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));
    assert(tub_apply(1,505,NULL,NULL)==1);
    assert(!strcmp(tub_seen[tub_claim("PerformCrewExchange2")],expected_ex) && !strcmp(tub_seen[tub_claim("AssignHiredActor")],expected_as) &&
        !strcmp(tub_seen[tub_claim("ReassignPeople")],expected_re));
    assert(strstr(tub_rows,"PerformCrewExchange2=ok|AssignHiredActor=ok|ReassignPeople|TakeShipFromInternalStorage=1|done|"));
    /* ---- slot rules: mods from the inventory, slot 1 only; factions mode, slot 1 only ---- */
    pstk_capture_slot=2;tub_reset_counts();tub_capture_phase(1);
    assert(((tub_mod_fn)tub_entry("InstallWeaponMod"))(0x7002,"mod_weapon_damage_01_mk1")==0 && !uibuild_open_records);
    ((tub_dismantle_fn)tub_entry("DismantleShipMod"))(0x7002);assert(!uibuild_open_records);
    assert(((tub_paint_fn)tub_entry("InstallPaintMod"))(0x7002,"paintmod_0001",1)==0 && !uibuild_open_records);
    assert(((tub_paint_fn)tub_entry("InstallPaintMod"))(0x7002,"paintmod_0001",0)==1 && uibuild_open_records==1);
    {char rows[512];assert(uibuild_refusals_text(rows,sizeof(rows)) && strstr(rows,":InstallWeaponMod:inventory") && strstr(rows,":DismantleShipMod:inventory") &&
        strstr(rows,":InstallPaintMod:inventory"));}
    assert(!tub_calls[tub_claim("InstallWeaponMod")] && !tub_calls[tub_claim("InstallPaintMod")]);
    tub_capture_phase(0);assert(uibuild_flush("test"));tub_take_queue();
    /* slot 2's default paint runs everywhere; a slot 1 batch with an inventory mod applied as slot 2's is skipped */
    memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));tub_reset_counts();assert(tub_apply(2,506,NULL,NULL)==1 && tub_calls[tub_claim("InstallPaintMod")]==1);
    pstk_capture_slot=1;tub_capture_phase(1);((tub_mod_fn)tub_entry("InstallShipMod"))(0x7002,"modx");tub_capture_phase(0);
    assert(uibuild_flush("test"));tub_take_queue();memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));tub_reset_counts();
    assert(tub_apply(2,507,NULL,NULL)==1 && !tub_calls[tub_claim("InstallShipMod")] && strstr(tub_rows,"InstallShipMod skipped=inventory"));
    memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));tub_reset_counts();assert(tub_apply(1,508,NULL,NULL)==1 && tub_calls[tub_claim("InstallShipMod")]==1);
    uibuild_factions=1;pstk_capture_slot=2;tub_capture_phase(1);
    ((tub_dismantle_fn)tub_entry("RemoveBuildStorage"))(0x7001);assert(!uibuild_open_records);
    {const char *why="";static const unsigned char name[2]={4,5};assert(!uibuild_add_lua(3,name,2,0,&why) && !strcmp(why,"factions"));}
    pstk_capture_slot=1;((tub_dismantle_fn)tub_entry("RemoveBuildStorage"))(0x7001);assert(uibuild_open_records==1);
    tub_capture_phase(0);assert(uibuild_flush("test"));tub_take_queue();
    memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));tub_reset_counts();assert(tub_apply(2,509,NULL,NULL)==1 && strstr(tub_rows,"RemoveBuildStorage skipped=factions"));
    uibuild_factions=0;
    /* ---- refusals: unmapped components, a batch too long, a full queue ---- */
    tub_capture_phase(1);
    ((tub_dismantle_fn)tub_entry("RemoveBuildStorage"))(0x9999);assert(!uibuild_open_records);
    {char rows[256];assert(uibuild_refusals_text(rows,sizeof(rows)) && strstr(rows,":RemoveBuildStorage:unmapped"));}
    static uibuild_macro2 many[255];static char long_macros[255][64];
    for(unsigned i=0;i<255;i++) {snprintf(long_macros[i],sizeof(long_macros[i]),"macro_%03u_with_a_long_name_that_is_unique_xx",i);many[i]=(uibuild_macro2){long_macros[i],"engine",i,0,{NULL,NULL}};}
    uibuild_loadout2 huge={0};huge.engines=many;huge.nengines=255;
    assert(((tub_bt6_fn)tub_entry("AddBuildTask6"))(0x7001,0,"m",&huge,1,&xfer,0,NULL,NULL)==0 && !uibuild_open_records);
    {char rows[256];assert(uibuild_refusals_text(rows,sizeof(rows)) && strstr(rows,":AddBuildTask6:"));}
    ((tub_dismantle_fn)tub_entry("RemoveBuildStorage"))(0x7001);
    {unsigned saved_count=uirep_queue_count;uirep_queue_count=UIREP_QUEUE;   /* the replay's queue is full */
     tub_capture_phase(0);assert(!uibuild_flush("test") && !uibuild_open_records);uirep_queue_count=saved_count;
     char rows[256];assert(uibuild_refusals_text(rows,sizeof(rows)) && strstr(rows,":batch:queue_full"));}
    /* ---- the apply: a torn batch is dropped everywhere, a malformed one stops; gone and mismatched references ---- */
    tub_capture_phase(1);((tub_bt6_fn)tub_entry("AddBuildTask6"))(0x7001,0,"ship_arg_s_scout_01_a_macro",&loadout,120000,&xfer,0,"Probe One",&info);
    tub_capture_phase(0);assert(uibuild_flush("test"));tub_take_queue();assert(tub_wire_count>=2);
    {memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));const char *why=NULL;
     assert(uibuild_part(1,tub_wire[1].data,tub_wire[1].length,&why)==-1 && !strcmp(why,"torn"));   /* a middle part without its start */
     why=NULL;assert(uibuild_part(1,tub_wire[0].data,tub_wire[0].length,&why)==0);
     tub_event e=tub_wire[0];e.data[0]^=1;e.data[1]=1;why=NULL;assert(uibuild_part(1,e.data,e.length,&why)==-1);   /* another batch's sequence */
     memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));e=tub_wire[0];e.data[UIBUILD_PART_HEAD]=9;why=NULL;   /* version 9 */
     for(unsigned i=0;i<tub_wire_count;i++) {int r=uibuild_part(1,i?tub_wire[i].data:e.data,i?tub_wire[i].length:e.length,&why);if(i+1<tub_wire_count) assert(r==0);else assert(r==-2 && !strcmp(why,"ui_build_batch"));}}
    tub_salt=1;memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));assert(tub_apply(1,510,NULL,NULL)==-3 && strstr(tub_rows,"stop:ui_ref_mismatch"));tub_salt=0;
    {unsigned char saved[0x100];memcpy(saved,tub_components[1],0x100);   /* the container is gone: skipped, the same everywhere */
     uirep_fn.find=NULL;memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));tub_reset_counts();
     assert(tub_apply(1,511,NULL,NULL)==1 && !tub_calls[0] && strstr(tub_rows,"AddBuildTask6 skipped=gone"));uirep_fn.find=tub_find;memcpy(tub_components[1],saved,0x100);}
    /* A float the menu passes in a register position is read from the saved xmm (frame - 0x40 + 16*i). */
    {uint64_t frame_block[24]={0};float v=3.5f;memcpy((unsigned char*)&frame_block[8]-0x40+16,&v,4);uint32_t bits=uibuild_xmm(&frame_block[8],1),want=0;memcpy(&want,&v,4);assert(bits==want);}
    /* ---- after the window everything is X4's again ---- */
    uirep_stopped=1;tub_reset_counts();tub_capture_phase(1);
    ((tub_dismantle_fn)tub_entry("RemoveBuildStorage"))(0x7001);assert(tub_calls[tub_claim("RemoveBuildStorage")]==1 && !uibuild_open_records);
    tub_capture_phase(0);uirep_stopped=0;
    {char status[512];assert(uibuild_status(status,sizeof(status)) && strstr(status,"ui_build=armed families=3000"));}
    uiguard_uninstall();assert(uiguard_restored);
    if(uiguard_region) {VirtualFree(uiguard_region,0,MEM_RELEASE);uiguard_region=NULL;}
    uiguard_unclaim(NULL);memset(uibuild_claim_at,0,sizeof(uibuild_claim_at));
    uibuild_armed=0;uiguard_armed=0;uiguard_live=0;uiguard_installed=0;uiguard_groups=0;uiguard_tab=uiguard_table;uiguard_count=UIGUARD_ENTRIES;
    uirep_armed=0;uirep_families=saved_families;uirep_fn=saved_fn;uirep_queue_head=uirep_queue_count=0;uibuild_fn.plot_size=uibuild_fn.plot_center=NULL;
    pnat_base=saved_base;image=saved_image;ui_thread=saved_ui;stats.issued=saved_issued;pilot_stop=saved_stop;pstk_capture_start=saved_start;
    pstk_capturing=saved_capturing;pstk_capture_slot=saved_capture_slot;
    VirtualFree(region,0,MEM_RELEASE);
    printf("UI build families verified on stand-ins: %u claims through the guard's hook, batches, parts, two clients\n",(unsigned)UIBUILD_CLAIMS);
}
/* The pinned image: every claim is an FFI entry of the generated guard table, in the group the families expect; the pins. */
static void test_ui_build_native_image(const char *path) {
    unsigned char *prior=image;image=test_pmath_map(path);
    for(unsigned c=0;c<UIBUILD_CLAIMS;c++) {
        unsigned found=0;
        for(unsigned i=0;i<UIGUARD_ENTRIES;i++) if(uiguard_table[i].kind==UIGUARD_FFI && !strcmp(uiguard_table[i].name,uibuild_claims[c].name)) {
            ++found;const uiguard_entry *e=&uiguard_table[i];
            const char *k=strchr(uibuild_claims[c].spec,'k');
            assert(k?e->checkonly==(int)(k-uibuild_claims[c].spec):e->checkonly<0);
            assert((uibuild_claims[c].ret==UIBUILD_RET_STRUCT_REASON)==(e->ret==UIGUARD_RET_STRUCT));
            assert(!e->covered);   /* no other replay family captures below these */
        }
        assert(found==1);
    }
    assert(uibuild_pins());
    image[UIBUILD_PLOT_SIZE+0x20]^=1;assert(!uibuild_pins() && pin_failed_rva==UIBUILD_PLOT_SIZE);image[UIBUILD_PLOT_SIZE+0x20]^=1;assert(uibuild_pins());
    VirtualFree(image,0,MEM_RELEASE);image=prior;
    printf("UI build families verified on the pinned image: %u claims, pins\n",(unsigned)UIBUILD_CLAIMS);
}

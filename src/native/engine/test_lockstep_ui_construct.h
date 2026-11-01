/* Station construction (lockstep_ui_construct.h) on stand-ins, behind the guard's real hook and the build family's batches
 * (test_lockstep_ui_build.h's harness): the station configuration's confirm captured as one record carrying the whole plan
 * (names, predecessor indexes, exact transform bits, flags, a module loadout through X4's conversion, known IDs kept, new
 * ones 0), refusals at the capture (no construction map, a name that does not find the same object, an order, an owner),
 * the apply on two "clients" through the mirrored commit (build context, parent walk, references taken and given back,
 * X4's constructor seeing the same source sequence, validity and delete, the answer), base/plot/owner/macro skips, factions
 * mode (slot 2 in its faction scope with the trade family, refused without it; the plot's faction ID; scoped money), a
 * cancelled construction, a scripted plan, and the construction ID counter kept canonical. The image test: the pins. */
static __attribute__((aligned(16))) unsigned char tco_obj[8][0x200];   /* runtime 0x9001..0x9008 */
static unsigned char tco_container[8],tco_parent[8];
static void *tco_owner_of[8],*tco_planned[8];
static unsigned char tco_faction[2][0x400];
static void *tco_vtable[720],*tco_iface[1];
static unsigned tco_destroyed,tco_deleted;
static unsigned char tco_db_marker[16];
static unsigned tco_index(const void *o) {return (unsigned)(((const unsigned char*)o-tco_obj[0])/0x200);}
static unsigned char tco_is_class(void *o,unsigned k) {return k==0x62 && tco_container[tco_index(o)];}
static unsigned char tco_is_derived(void *o,unsigned k) {return k==0x6f && tco_parent[tco_index(o)];}
static void *tco_planned_of(void *o) {return tco_planned[tco_index(o)];}
static void tco_destroy(void *c) {(void)c;++tco_destroyed;}
static void *tco_owner(void *o) {return o>=(void*)tco_obj[0] && o<(void*)tco_obj[8]?tco_owner_of[tco_index(o)]:NULL;}
static void *tco_find(void *map,uint64_t id,unsigned flags) {(void)map;assert(flags==4);return id>=0x9001 && id<=0x9008?tco_obj[id-0x9001]:NULL;}
static uint64_t tco_portable(uint64_t runtime) {return runtime>=0x9001 && runtime<=0x9008?0xdef000+runtime:0;}
static void tco_string(unsigned char *at,const char *s) {
    size_t n=strlen(s);uint64_t cap=n<16?15:n;memset(at,0,0x20);
    if(n<16) memcpy(at,s,n);else {const char *p=s;memcpy(at,&p,8);}
    memcpy(at+0x10,&n,8);memcpy(at+0x18,&cap,8);
}
/* Macros with their definitions and connection tables (sorted by the name hash, as X4 keeps them). */
static const char *const tco_macro_names[3]={"prod_gen_refinedmetals_macro","dockarea_x","struct_y_macro"};
static const char *const tco_conn_names[3][3]={{"connectionsnap001","connectionsnap002","Snap_A"},{"connectionsnap001","connectionsnap002","Snap_A"},
    {"connectionsnap001","connectionsnap002","Snap_A"}};
static __attribute__((aligned(16))) unsigned char tco_macro[3][0x60],tco_def[3][0x200],tco_conn[3][3][0x160];
static int tco_macro_wrong;   /* the database answers another macro for macro 2 */
static void *tco_macro_find(void *db,const void *view,uint64_t zero) {
    assert(db==(void*)tco_db_marker && !zero);
    const struct {const char *data;uint64_t length;} *v=view;
    for(unsigned i=0;i<3;i++) if(strlen(tco_macro_names[i])==v->length && !memcmp(tco_macro_names[i],v->data,v->length)) return tco_macro_wrong && i==2?tco_macro[0]:tco_macro[i];
    return NULL;
}
static void *tco_conn_of(unsigned m,const char *name) {
    for(unsigned k=0;k<3;k++) if(!strcmp(tco_conn_names[m][k],name)) {
        uint64_t h=uicons_name_hash(name,(unsigned)strlen(name));
        for(unsigned j=0;j<3;j++) {uint64_t at;memcpy(&at,tco_conn[m][j]+8,8);if(at==h) return tco_conn[m][j];}
    }
    return NULL;
}
static void tco_macros(void) {
    for(unsigned m=0;m<3;m++) {
        memset(tco_macro[m],0,sizeof(tco_macro[m]));tco_string(tco_macro[m]+UICONS_MACRO_NAME,tco_macro_names[m]);
        void *def=tco_def[m];memcpy(tco_macro[m]+UICONS_MACRO_DEF,&def,8);
        uint64_t h[3];unsigned order[3]={0,1,2};
        for(unsigned k=0;k<3;k++) h[k]=uicons_name_hash(tco_conn_names[m][k],(unsigned)strlen(tco_conn_names[m][k]));
        for(unsigned a=0;a<3;a++) for(unsigned b=a+1;b<3;b++) if(h[order[b]]<h[order[a]]) {unsigned t=order[a];order[a]=order[b];order[b]=t;}
        for(unsigned j=0;j<3;j++) {memset(tco_conn[m][j],0,0x160);memcpy(tco_conn[m][j]+8,&h[order[j]],8);tco_string(tco_conn[m][j]+0x10,tco_conn_names[m][order[j]]);}
        void *begin=tco_conn[m][0],*end=tco_conn[m][0]+3*0x160;memcpy(tco_def[m]+UICONS_DEF_CONNECTIONS,&begin,8);memcpy(tco_def[m]+UICONS_DEF_CONNECTIONS+8,&end,8);
    }
}
/* Sequences: the construction map's (the editor) and the station's planned ones. */
typedef struct {unsigned char seq[0x38];void *ptrs[8];__attribute__((aligned(16))) unsigned char e[4][UICONS_ENTRY_SIZE];} tco_sequence;
static tco_sequence tco_editor,tco_planned_a,tco_planned_after;
static void tco_entry(tco_sequence *s,unsigned i,uint64_t id,unsigned macro,void *conn,int pred,void *pred_conn,const float t[16],unsigned flags,int upgrade) {
    unsigned char *e=s->e[i];memset(e,0,UICONS_ENTRY_SIZE);void *m=tco_macro[macro],*p=pred>=0?(void*)s->e[pred]:NULL;
    memcpy(e,&id,8);memcpy(e+8,&m,8);memcpy(e+0x10,&conn,8);memcpy(e+0x18,&p,8);memcpy(e+0x20,&pred_conn,8);memcpy(e+0x30,t,64);
    e[0x208]=flags&1;e[0x209]=(flags>>1)&1;e[0x20a]=(flags>>2)&1;e[0x70]=(unsigned char)upgrade;s->ptrs[i]=e;
}
static void tco_close(tco_sequence *s,unsigned n,uint32_t bookmark) {
    memset(s->seq,0,sizeof(s->seq));void *b=s->ptrs,*e=s->ptrs+n;memcpy(s->seq,&b,8);memcpy(s->seq+8,&e,8);memcpy(s->seq+16,&e,8);memcpy(s->seq+0x30,&bookmark,4);
}
static const float tco_identity[16]={100,0,-50,0, 1,0,0,0, 0,1,0,0, 0,0,1,0};
static float tco_turned[16]={0,250.5f,1e-40f,0, 0,0,-1,0, 0,1,0,0, 1,0,0,0};   /* a denormal: exact bits travel */
static void tco_world(void) {
    tco_macros();
    tco_entry(&tco_editor,0,0x500,0,NULL,-1,NULL,tco_identity,1,0);
    tco_entry(&tco_editor,1,0x9999,1,tco_conn_of(1,"connectionsnap001"),0,tco_conn_of(0,"connectionsnap002"),tco_turned,4,1);
    tco_entry(&tco_editor,2,0x501,2,tco_conn_of(2,"Snap_A"),1,tco_conn_of(1,"connectionsnap002"),tco_identity,2,0);
    tco_close(&tco_editor,3,1);
    tco_entry(&tco_planned_a,0,0x500,0,NULL,-1,NULL,tco_identity,1,0);
    tco_entry(&tco_planned_a,1,0x501,2,tco_conn_of(2,"Snap_A"),0,tco_conn_of(0,"connectionsnap001"),tco_identity,0,0);
    tco_close(&tco_planned_a,2,0);
    tco_entry(&tco_planned_after,0,0x500,0,NULL,-1,NULL,tco_identity,1,0);tco_close(&tco_planned_after,1,0);
}
/* The holomap and its behaviour. */
static __attribute__((aligned(16))) unsigned char tco_holo[0x400],tco_holo_other[0x400],tco_behavior[0x400],tco_behavior_other[0x400];
static void *tco_behavior_vt[8];
static unsigned char tco_kind(void *b,unsigned k) {return b==(void*)tco_behavior && k==2;}
static void *tco_holomap(uint64_t id) {return id==0x7777?(void*)tco_holo:id==0x7778?(void*)tco_holo_other:NULL;}
/* The loadout export (entry 1 only), the upgrade plan functions. */
static void tco_counts(uicons_counts1 *c,uint64_t holomap,size_t idx,uint64_t def,uint64_t module) {
    assert(holomap==0x7777 && !def && !module);memset(c,0,sizeof(*c));if(idx==1) {c->nturrets=2;c->nshieldgroups=1;}
}
static void tco_loadout(uicons_loadout1 *l,uint64_t holomap,size_t idx,uint64_t def,uint64_t module) {
    assert(holomap==0x7777 && idx==1 && !def && !module && l->nturrets==2 && l->nshieldgroups==1);
    l->turrets[0]=(uicons_macro1){"turret_a_macro","turret",0,0};l->turrets[1]=(uicons_macro1){"turret_b_macro","turret",3,1};
    l->shieldgroups[0]=(uicons_group1){"shield_c_macro","..","group_2",4,0};
}
static unsigned char tco_upgrade_empty(const void *plan) {return !((const unsigned char*)plan)[0];}
static unsigned tco_inits,tco_frees;
static void *tco_upgrade_init(void *plan) {memset(plan,0,0x198);memcpy(plan,"init",5);++tco_inits;return plan;}
static void tco_upgrade_free(void *plan) {assert(!memcmp(plan,"init",4) || !memcmp(plan,"L:",2));++tco_frees;}
static void tco_upgrade_from(void *plan,void *macro,const uicons_loadout1 *l) {
    char *out=plan;unsigned n=0;
    n+=snprintf(out+n,0x190-n,"L:%u:",(unsigned)(((unsigned char*)macro-tco_macro[0])/0x60));
    for(uint32_t i=0;i<l->nturrets;i++) n+=snprintf(out+n,0x190-n,"t%s/%s/%zu/%u,",l->turrets[i].macro,l->turrets[i].type,l->turrets[i].slot,l->turrets[i].optional);
    for(uint32_t i=0;i<l->nshieldgroups;i++) n+=snprintf(out+n,0x190-n,"g%s/%s/%s/%u/%u,",l->shieldgroups[i].macro,l->shieldgroups[i].path,l->shieldgroups[i].group,
        l->shieldgroups[i].count,l->shieldgroups[i].optional);
    n+=snprintf(out+n,0x190-n,"n%u%u%u%u%u%u%u",l->nweapons,l->nshields,l->nengines,l->nturretgroups,l->nammo,l->nunits,l->nsoftware);
}
/* The build context and X4's commit. */
static void **tco_context(void *container,void **out) {(void)container;*out=tco_obj[2];return out;}
static __attribute__((aligned(16))) unsigned char tco_task[UICONS_BUILD_TASK_SIZE];
static void *tco_task_vt[2];
static void tco_task_delete(void *task,unsigned flag) {assert(task==(void*)tco_task && flag==1);++tco_deleted;}
static void *tco_allocate(size_t size,uint64_t a,uint64_t b,uint64_t c,uint32_t alignment) {
    (void)a;(void)b;(void)c;assert(size==UICONS_BUILD_TASK_SIZE && alignment==0x10);memset(tco_task,0,sizeof(tco_task));void *vt=tco_task_vt;memcpy(tco_task,&vt,8);return tco_task;
}
static char tco_seen[4096];static void *tco_seen_faction,*tco_seen_now,*tco_seen_container,*tco_seen_parent;static unsigned tco_tasks;
static int tco_invalid;
static const char *tco_name_at(const unsigned char *s,char *buf) {unsigned n=0;if(!s) return "-";return uicons_std_string(s,buf,&n)?buf:"?";}
static void *tco_build_task(void *task,void **container,void **parent,void *faction,void *sequence,uint32_t bookmark,uint64_t zero) {
    assert(task==(void*)tco_task && !bookmark && !zero);++tco_tasks;
    tco_seen_container=*container;tco_seen_parent=*parent;tco_seen_faction=faction;tco_seen_now=*uitrade_player_faction();
    /* X4's copy: every source entry's fields; an ID 0 is drawn from the counter (under its lock) */
    size_t n=0;void **e=uicons_entries(sequence,&n);tco_seen[0]=0;
    uint32_t current=0;memcpy(&current,(unsigned char*)sequence+0x30,4);
    void **bb=NULL,**be=NULL;memcpy(&bb,(unsigned char*)sequence+0x18,8);memcpy(&be,(unsigned char*)sequence+0x20,8);
    tub_cat(tco_seen,sizeof(tco_seen),"n=%zu cur=%u bm=%zu|",n,current,(size_t)(be-bb));
    for(size_t i=0;i<n;i++) {
        unsigned char *s=e[i];uint64_t id;void *m,*c,*p,*pc;char b1[64],b2[64],b3[64];memcpy(&id,s,8);memcpy(&m,s+8,8);memcpy(&c,s+0x10,8);memcpy(&p,s+0x18,8);memcpy(&pc,s+0x20,8);
        if(!id) {EnterCriticalSection(uicons_cid_lock());id=++*uicons_cid_counter();LeaveCriticalSection(uicons_cid_lock());}
        tub_cat(tco_seen,sizeof(tco_seen),"%zu:id=%llx,m=%s,c=%s,p=%u,pc=%s,t=",i,(unsigned long long)id,tco_name_at(m?(unsigned char*)m+UICONS_MACRO_NAME:NULL,b1),
            tco_name_at(c?(unsigned char*)c+0x10:NULL,b2),uicons_index_of(e,n,p),tco_name_at(pc?(unsigned char*)pc+0x10:NULL,b3));
        const uint32_t *t=(const uint32_t*)(s+0x30);for(unsigned k=0;k<16;k++) tub_cat(tco_seen,sizeof(tco_seen),"%x.",t[k]);
        tub_cat(tco_seen,sizeof(tco_seen),",f=%u%u%u,u=%s|",s[0x208],s[0x209],s[0x20a],(const char*)(s+0x70));
    }
    if(!tco_invalid) {uint32_t a=1;uint64_t b=0xb7;memcpy(tco_task+0x10,&a,4);memcpy(tco_task+0x18,&b,8);}
    tco_planned[0]=tco_planned[1]=tco_planned_after.seq;   /* the station plans the new sequence */
    return task;
}
static float tco_plot[6]={8000,8000,8000,0,0,0};
static void *tco_plot_size(void *out,uint64_t station) {assert(station==0x9001 || station==0x9002);memcpy(out,tco_plot,12);return out;}
static void *tco_plot_center(void *out,uint64_t station) {assert(station==0x9001 || station==0x9002);memcpy(out,tco_plot+3,12);return out;}
static void tco_objects(void) {
    memset(tco_obj,0,sizeof(tco_obj));void *vt=tco_vtable,*iface=tco_iface;
    for(unsigned i=0;i<8;i++) {LONG count=5,state=2;memcpy(tco_obj[i],&vt,8);memcpy(tco_obj[i]+0x38,&iface,8);memcpy(tco_obj[i]+0x40,&count,4);memcpy(tco_obj[i]+0x44,&state,4);}
    /* 0x9001 station A (player), 0x9002 station B (x4lan_player2), 0x9003 build context -> 0x9004 -> 0x9005 (the build
     * parent), 0x9006 slot 1's ship (player), 0x9007 slot 2's ship (x4lan_player2) */
    memset(tco_container,0,sizeof(tco_container));memset(tco_parent,0,sizeof(tco_parent));tco_container[0]=tco_container[1]=1;tco_parent[4]=1;
    void *p3=tco_obj[3],*p4=tco_obj[4];memcpy(tco_obj[2]+0x70,&p3,8);memcpy(tco_obj[3]+0x70,&p4,8);
    for(unsigned i=0;i<8;i++) tco_owner_of[i]=tco_faction[0];
    tco_owner_of[1]=tco_owner_of[6]=tco_faction[1];
    tco_planned[0]=tco_planned_a.seq;tco_planned[1]=NULL;
}
static LONG tco_count(unsigned i) {LONG v;memcpy(&v,tco_obj[i]+0x40,4);return v;}
typedef void (*tco_commit_fn)(uint64_t,uint64_t);
static int tco_apply(unsigned slot,uint64_t tick) {
    memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));uibuild_frame_tick=0;tco_objects();tco_inits=tco_frees=0;
    int r=tub_apply(slot,tick,NULL,NULL);
    return r;
}
static void test_ui_construct(void) {
    unsigned char *saved_base=pnat_base,*saved_image=image;DWORD saved_ui=ui_thread;uint64_t saved_issued=stats.issued,saved_stop=pilot_stop,saved_start=pstk_capture_start;
    int saved_capturing=pstk_capturing;unsigned saved_capture_slot=pstk_capture_slot,saved_families=uirep_families,saved_count=pilot_slot_count;uirep_fns saved_fn=uirep_fn;
    pilot_slot saved_pilot[2];memcpy(saved_pilot,pilot_slots,sizeof(saved_pilot));
    unsigned char *region=VirtualAlloc(NULL,TEST_PNAT_REGION,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(region);
    tub_region=region;pnat_base=image=region;ui_thread=GetCurrentThreadId();memset(region,0xcc,0x4000);
    {void *map=tub_map_marker,*db=tco_db_marker,*player=tco_faction[0];memcpy(region+UIREP_MAP,&map,8);memcpy(region+UICONS_MACRO_DB,&db,8);memcpy(region+UITRADE_PLAYER_FACTION,&player,8);}
    InitializeCriticalSection(uicons_cid_lock());{uint64_t c=1000;memcpy(region+UICONS_CID_COUNTER,&c,8);}
    for(unsigned i=0;i<720;i++) tco_vtable[i]=NULL;
    tco_vtable[UICONS_IS_CLASS_SLOT]=(void*)tco_is_class;tco_vtable[UICONS_IS_DERIVED_SLOT]=(void*)tco_is_derived;tco_vtable[UICONS_PLANNED_SLOT]=(void*)tco_planned_of;
    tco_iface[0]=(void*)tco_destroy;tco_task_vt[0]=(void*)tco_task_delete;
    tco_behavior_vt[UICONS_BEHAVIOR_KIND]=(void*)tco_kind;
    {void *vt=tco_behavior_vt,*b=tco_behavior,*o=tco_behavior_other,*s=tco_editor.seq;
     memcpy(tco_behavior,&vt,8);memcpy(tco_behavior_other,&vt,8);memcpy(tco_holo+UICONS_BEHAVIOR,&b,8);memcpy(tco_holo_other+UICONS_BEHAVIOR,&o,8);
     memcpy(tco_behavior+UICONS_BEHAVIOR_SEQUENCE,&s,8);memcpy(tco_behavior_other+UICONS_BEHAVIOR_SEQUENCE,&s,8);}
    tco_world();tco_objects();
    uirep_fn=(uirep_fns){.find=tco_find,.portable=tco_portable};
    uitrade_owner_fn=tco_owner;
    memset(pilot_slots,0,sizeof(saved_pilot));pilot_slots[0].runtime=0x9006;pilot_slots[1].runtime=0x9007;pilot_slot_count=2;
    tub_table_build();
    uiguard_tab=tub_table;uiguard_count=TUB_ENTRIES;uiguard_region=NULL;uiguard_installed=0;uiguard_armed=0;memset(uiguard_on,0,sizeof(uiguard_on));
    uiguard_unclaim(NULL);uibuild_armed=0;
    uirep_armed=1;uirep_failed=0;uirep_stopped=0;uirep_replaying=0;uirep_queue_head=uirep_queue_count=0;uirep_refusal_head=uirep_refusal_tail=0;
    pstk_capturing=1;pstk_capture_start=1;stats.issued=10;pilot_stop=100000;pstk_capture_slot=1;rid_probe_frame=0;pres_open=0;render_rng_in_slot=0;uirep_dispatch_depth=0;
    uicons_fn=(uicons_fns){.holomap=tco_holomap,.macro=tco_macro_find,.counts=tco_counts,.loadout=tco_loadout,.upgrade_empty=tco_upgrade_empty,
        .upgrade_init=tco_upgrade_init,.upgrade_free=tco_upgrade_free,.upgrade_from=tco_upgrade_from,.build_context=tco_context,.allocate=tco_allocate,
        .build_task=tco_build_task};
    uicons_ready=1;
    uirep_families=UIBUILD_FAMILIES;uiguard_groups=0;assert(uiguard_arm_using(1u<<UIGUARD_G_FLIGHT));
    assert(uibuild_arm_using(0) && uicons_cid_on);
    uibuild_fn.entry=NULL;uibuild_fn.plot_size=tco_plot_size;uibuild_fn.plot_center=tco_plot_center;
    assert(uiguard_install() && uiguard_intact());uiguard_live=1;
    unsigned plan_claim=tub_claim(UIBUILD_PLAN_CLAIM),cancel_claim=tub_claim("CancelConstruction");
    tco_commit_fn commit=(tco_commit_fn)tub_entry(UIBUILD_PLAN_CLAIM);
    /* ---- the names: X4's connection lookup (lower-case hash, lower bound) finds each element, nothing else ---- */
    for(unsigned m=0;m<3;m++) for(unsigned k=0;k<3;k++) {
        const char *n=tco_conn_names[m][k];assert(uicons_connection(tco_macro[m],n,(unsigned)strlen(n))==tco_conn_of(m,n));
    }
    assert(uicons_connection(tco_macro[0],"snap_a",6)==tco_conn_of(0,"Snap_A") && !uicons_connection(tco_macro[0],"connectionsnap009",17) && !uicons_connection(tco_macro[0],"",0));
    /* ---- capture: the station configuration's confirm in a local phase; nothing runs, one record carries the plan ---- */
    tub_reset_counts();tub_capture_phase(1);
    commit(0x9001,0x7777);
    assert(!tub_calls[plan_claim] && uibuild_open_records==1 && uicons_captured==1 && !tco_tasks);
    tub_capture_phase(0);assert(uibuild_flush("test"));tub_take_queue();assert(tub_wire_count>=1);
    /* ---- the apply on two clients: X4's commit sees the same source sequence; the answer is the planned fingerprint ---- */
    static char seen_a[4096],rows_a[4096];uint64_t hash_a=0;
    for(int client=0;client<2;client++) {
        tco_tasks=0;tco_deleted=0;tco_destroyed=0;{uint64_t c=1000;memcpy(region+UICONS_CID_COUNTER,&c,8);}
        assert(tco_apply(1,600)==1 && tco_tasks==1);
        assert(tco_seen_container==(void*)tco_obj[0] && tco_seen_parent==(void*)tco_obj[4] && tco_seen_faction==(void*)tco_faction[0] && tco_seen_now==(void*)tco_faction[0]);
        for(unsigned i=0;i<8;i++) assert(tco_count(i)==5);   /* every reference taken is given back */
        assert(!tco_destroyed && !tco_deleted && tco_inits==3 && tco_frees==3);
        assert(strstr(tub_rows,"SetConstructionSequenceFromConstructionMap task=1 plan="));
        if(!client) {memcpy(seen_a,tco_seen,sizeof(seen_a));memcpy(rows_a,tub_rows,sizeof(rows_a));hash_a=uibuild_frame_hash;}
        else assert(!strcmp(seen_a,tco_seen) && !strcmp(rows_a,tub_rows) && hash_a==uibuild_frame_hash);
    }
    {char want[512];uint32_t d;float denormal=1e-40f;memcpy(&d,&denormal,4);
     assert(strstr(seen_a,"n=3 cur=1 bm=1|"));
     assert(strstr(seen_a,"0:id=500,m=prod_gen_refinedmetals_macro,c=-,p=65535,pc=-,t=42c80000.0.c2480000.0.3f800000.0.0.0.0.3f800000.0.0.0.0.3f800000.0.,f=100,u=init|"));
     snprintf(want,sizeof(want),"1:id=3e9,m=dockarea_x,c=connectionsnap001,p=0,pc=connectionsnap002,t=0.437a8000.%x.0.0.0.bf800000.0.0.3f800000.0.0.3f800000.0.0.0.,f=001,"
         "u=L:1:tturret_a_macro/turret/0/0,tturret_b_macro/turret/3/1,gshield_c_macro/../group_2/4/0,n0000000|",d);
     assert(strstr(seen_a,want));   /* a new entry: ID 0 carried, drawn at the apply (1001); the denormal's bits exact */
     assert(strstr(seen_a,"2:id=501,m=struct_y_macro,c=Snap_A,p=1,pc=connectionsnap002,"));}
    /* ---- skips at the apply, the same everywhere: the base changed, the plot changed, X4 kept no task ---- */
    tco_tasks=0;tco_planned_a.e[1][0x208]^=1;tco_apply(1,601);tco_planned_a.e[1][0x208]^=1;
    assert(strstr(tub_rows,"SetConstructionSequenceFromConstructionMap skipped=plan_changed") && !tco_tasks);
    tco_plot[0]=9000;tco_apply(1,602);tco_plot[0]=8000;assert(strstr(tub_rows,"skipped=plot_changed"));
    tco_owner_of[0]=tco_faction[1];memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));tco_inits=0;
    tco_planned[0]=tco_planned_a.seq;assert(tub_apply(1,603,NULL,NULL)==1 && strstr(tub_rows,"skipped=owner"));
    tco_invalid=1;tco_tasks=tco_deleted=0;assert(tco_apply(1,604)==1 && tco_tasks==1 && tco_deleted==1 && strstr(tub_rows,"task=0 plan=0000000000000000"));tco_invalid=0;
    tco_macro_wrong=1;tco_tasks=0;tco_apply(1,605);tco_macro_wrong=0;assert(!tco_tasks && strstr(tub_rows,"skipped=macro"));
    /* ---- refusals at the capture: no construction map, a name that finds another object, an order, an owner ---- */
    tco_objects();tub_capture_phase(1);
    {char rows[512];
     commit(0x9001,0x7778);assert(!uibuild_open_records);
     tco_macro_wrong=1;commit(0x9001,0x7777);tco_macro_wrong=0;assert(!uibuild_open_records);
     void *wrong=tco_conn_of(2,"connectionsnap001");memcpy(tco_editor.e[2]+0x10,&wrong,8);
     {unsigned char saved[0x20];memcpy(saved,tco_conn_of(2,"connectionsnap001")+0x10,0x20);tco_string((unsigned char*)wrong+0x10,"Snap_A");
      commit(0x9001,0x7777);memcpy((unsigned char*)wrong+0x10,saved,0x20);}
     wrong=tco_conn_of(2,"Snap_A");memcpy(tco_editor.e[2]+0x10,&wrong,8);assert(!uibuild_open_records);
     void *later=tco_editor.e[2];memcpy(tco_editor.e[1]+0x18,&later,8);commit(0x9001,0x7777);
     void *first=tco_editor.e[0];memcpy(tco_editor.e[1]+0x18,&first,8);assert(!uibuild_open_records);
     commit(0x9002,0x7777);assert(!uibuild_open_records);   /* slot 1 and x4lan_player2's station */
     assert(uibuild_refusals_text(rows,sizeof(rows)));
     assert(strstr(rows,UIBUILD_PLAN_CLAIM":holomap") && strstr(rows,UIBUILD_PLAN_CLAIM":macro") && strstr(rows,UIBUILD_PLAN_CLAIM":connection") &&
         strstr(rows,UIBUILD_PLAN_CLAIM":order") && strstr(rows,UIBUILD_PLAN_CLAIM":owner"));
     assert(uicons_refused==5 && !strcmp(uicons_last_why,"owner"));}
    /* ---- a cancelled construction: captured, answered true, X4's function with the same task ID at the apply ---- */
    tub_reset_counts();
    assert(((unsigned char(*)(uint64_t,uint64_t))tub_entry("CancelConstruction"))(0x9001,0x1234567890ULL)==1 && !tub_calls[cancel_claim]);
    tub_capture_phase(0);assert(uibuild_flush("test"));tub_take_queue();
    assert(tco_apply(1,606)==1 && tub_calls[cancel_claim]==1 && !strcmp(tub_seen[cancel_claim],"9001;1234567890;") && strstr(tub_rows,"CancelConstruction=1|done|"));
    /* ---- factions mode, slot 2: refused without the trade family's scope; with it, the slot's own station in its scope ---- */
    uibuild_factions=1;pstk_capture_slot=2;tub_capture_phase(1);
    commit(0x9002,0x7777);assert(!uibuild_open_records);
    uirep_families|=UIREP_FAMILY_TRADE;assert(uitrade_wanted());
    commit(0x9001,0x7777);assert(!uibuild_open_records);   /* the player's station is not slot 2's */
    commit(0x9002,0x7777);assert(uibuild_open_records==1);
    {const char *why="";static const unsigned char money[3]={1,2,3};
     assert(!uibuild_add_lua(3,money,3,1,&why) && !strcmp(why,"factions"));   /* only money is scoped */
     assert(!uibuild_add_lua(1,money,3,0,&why) && !strcmp(why,"factions"));
     assert(uibuild_add_lua(1,money,3,1,&why) && uibuild_open_records==2);}
    /* the plot's faction: "player" is slot 2's own ID; another faction's is refused */
    assert(uibuild_faction_ids("player,x4lan_player2") && !strcmp(uibuild_faction_id[1],"x4lan_player2") && !uibuild_faction_ids("player,bad-id"));
    assert(uibuild_faction_ids("player,x4lan_player2"));
    {float location[6]={0};
     assert(((tub_reserve_fn)tub_entry("ReserveBuildPlot"))(0x9002,"player","factory",location,1000.0f,1000.0f,1000.0f)==0 && uibuild_open_records==3);
     ((tub_reserve_fn)tub_entry("ReserveBuildPlot"))(0x9002,"argon","factory",location,1000.0f,1000.0f,1000.0f);assert(uibuild_open_records==3);}
    tub_capture_phase(0);assert(uibuild_flush("test"));tub_take_queue();
    tco_objects();tco_planned[1]=NULL;tco_tasks=0;tub_reset_counts();memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));
    {InterlockedExchange(&uirep_replaying,1);uirep_ui_depth=0;int lua_scope=-1,parts_whole=0;
     for(unsigned i=0;i<tub_wire_count;i++) {
        if(tub_wire[i].op!=UIBUILD_OP_PART) continue;
        const char *why=NULL;int r=uibuild_part(2,tub_wire[i].data,tub_wire[i].length,&why);assert(r>=0);
        if(!r) continue;
        parts_whole=1;
        for(;;) {
            char what[128];unsigned lua=0,length=0;const unsigned char *payload=NULL;const char *w2=NULL;int scope=0;
            int s=uibuild_step(2,607,what,sizeof(what),&lua,&payload,&length,&w2,&scope);
            assert(s>=0);if(!s) break;
            if(s==2) {lua_scope=scope;assert(lua==1 && uibuild_lua_done(2,1));continue;}
            if(strstr(what,UIBUILD_PLAN_CLAIM)) assert(strstr(what,"task=1"));
        }
     }
     InterlockedExchange(&uirep_replaying,0);assert(parts_whole && lua_scope==1);}
    assert(tco_tasks==1 && tco_seen_faction==(void*)tco_faction[1] && tco_seen_now==(void*)tco_faction[1]);   /* inside slot 2's scope */
    {void *now=NULL;memcpy(&now,region+UITRADE_PLAYER_FACTION,8);assert(now==(void*)tco_faction[0] && !uitrade_scope_open);}   /* and out of it */
    assert(strstr(tub_seen[tub_claim("ReserveBuildPlot")],"9002;'x4lan_player2';'factory';"));
    uirep_families=UIBUILD_FAMILIES;uibuild_factions=0;pstk_capture_slot=1;
    /* ---- a scripted plan: the same record without a construction map (identity rotation, IDs drawn at the apply) ---- */
    tco_objects();tub_capture_phase(1);
    {const char *why="";
     assert(!uicons_script_plan(0x9001,"nosuch_macro,-,-,-,0,0,0",&why) && !strcmp(why,"macro"));
     assert(!uicons_script_plan(0x9001,"dockarea_x,connectionsnap001,0,-,0,0,0",&why) && !strcmp(why,"order"));
     assert(uicons_script_plan(0x9001,"prod_gen_refinedmetals_macro,-,-,-,0,0,0;dockarea_x,Snap_A,0,connectionsnap002,0,0,2500",&why));}
    tub_capture_phase(0);assert(uibuild_flush("test"));tub_take_queue();
    {uint64_t c=2000;memcpy(region+UICONS_CID_COUNTER,&c,8);}tco_tasks=0;
    assert(tco_apply(1,608)==1 && tco_tasks==1);
    assert(strstr(tco_seen,"0:id=7d1,m=prod_gen_refinedmetals_macro,c=-,p=65535,pc=-,t=0.0.0.0.3f800000.0.0.0.0.3f800000.0.0.0.0.3f800000.0.,f=000,u=init|"));
    assert(strstr(tco_seen,"1:id=7d2,m=dockarea_x,c=Snap_A,p=0,pc=connectionsnap002,t=0.0.451c4000.0."));
    /* ---- the construction ID counter: local draws go back at the probe frame's start and the UI move's end ---- */
    {volatile uint64_t *ctr=uicons_cid_counter();*ctr=5000;uicons_cid_rewinds=uicons_cid_rewound=0;
     uicons_cid_ui(1);*ctr+=3;uicons_cid_probe(1);assert(*ctr==5000);            /* the editor drew 3 before the probe's frame */
     *ctr+=2;uicons_cid_probe(0);*ctr+=4;uicons_cid_ui(0);assert(*ctr==5002);    /* the apply's 2 stay, the next 4 go back */
     assert(uicons_cid_rewinds==2 && uicons_cid_rewound==7);
     *ctr+=1;assert(*ctr==5003);uicons_cid_probe(1);assert(*ctr==5003);           /* outside a UI move nothing is touched */
     char frame[128];assert(uibuild_frame_text(700,frame,sizeof(frame)) && strstr(frame,":C138b"));
     char status[512];assert(uibuild_status(status,sizeof(status)) && strstr(status,"cid=kept/2/7"));
     uicons_cid_on=0;assert(uibuild_frame_text(700,frame,sizeof(frame)) && !strstr(frame,":C"));uicons_cid_on=1;}
    /* ---- cleanup ---- */
    uiguard_uninstall();assert(uiguard_restored);
    if(uiguard_region) {VirtualFree(uiguard_region,0,MEM_RELEASE);uiguard_region=NULL;}
    uiguard_unclaim(NULL);memset(uibuild_claim_at,0,sizeof(uibuild_claim_at));
    DeleteCriticalSection(uicons_cid_lock());
    uibuild_armed=0;uiguard_armed=0;uiguard_live=0;uiguard_installed=0;uiguard_groups=0;uiguard_tab=uiguard_table;uiguard_count=UIGUARD_ENTRIES;
    uicons_ready=0;uicons_cid_on=0;memset(&uicons_fn,0,sizeof(uicons_fn));uitrade_owner_fn=NULL;memset(uibuild_faction_id,0,sizeof(uibuild_faction_id));
    uirep_armed=0;uirep_families=saved_families;uirep_fn=saved_fn;uirep_queue_head=uirep_queue_count=0;uibuild_fn.plot_size=uibuild_fn.plot_center=NULL;
    memcpy(pilot_slots,saved_pilot,sizeof(saved_pilot));pilot_slot_count=saved_count;
    pnat_base=saved_base;image=saved_image;ui_thread=saved_ui;stats.issued=saved_issued;pilot_stop=saved_stop;pstk_capture_start=saved_start;
    pstk_capturing=saved_capturing;pstk_capture_slot=saved_capture_slot;
    VirtualFree(region,0,MEM_RELEASE);
    printf("UI construct verified on stand-ins: the plan record, two clients through the mirrored commit, skips, refusals, factions scope, cancel, script, ID counter\n");
}
/* The pinned image: every function the plan commit calls or mirrors, the Station vtable slot, a tampered byte refusing. */
static void test_ui_construct_native_image(const char *path) {
    unsigned char *prior=image;image=test_pmath_map(path);
    assert(uicons_pins());
    for(unsigned i=0;i<UICONS_PINS;i++) {
        unsigned rva=uicons_pin_table[i].rva;image[rva+uicons_pin_table[i].size-1]^=1;assert(!uicons_pins() && pin_failed_rva==rva);image[rva+uicons_pin_table[i].size-1]^=1;
    }
    {unsigned char saved[8];memcpy(saved,image+UICONS_STATION_VTABLE+8u*UICONS_PLANNED_SLOT,8);memset(image+UICONS_STATION_VTABLE+8u*UICONS_PLANNED_SLOT,0,8);
     assert(!uicons_pins() && pin_failed_rva==UICONS_STATION_VTABLE);memcpy(image+UICONS_STATION_VTABLE+8u*UICONS_PLANNED_SLOT,saved,8);}
    {unsigned char saved[8];memcpy(saved,image+UICONS_STATION_VTABLE+8u*UICONS_OWNER_SLOT,8);memset(image+UICONS_STATION_VTABLE+8u*UICONS_OWNER_SLOT,0,8);
     assert(!uicons_pins() && pin_failed_rva==UICONS_STATION_VTABLE);memcpy(image+UICONS_STATION_VTABLE+8u*UICONS_OWNER_SLOT,saved,8);}
    assert(uicons_pins());
    VirtualFree(image,0,MEM_RELEASE);image=prior;
    printf("UI construct verified on the pinned image: %u pins, the Station vtable's planned-sequence and owner slots\n",(unsigned)UICONS_PINS);
}

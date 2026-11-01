/* The faction family (lockstep_ui_faction.h) on a private executable region: a stand-in for every claimed function behind the
 * guard's real hook and shared routine, called the way LuaJIT calls them (structs by pointer, floats in xmm0/xmm1), and
 * stand-in rule managers of X4's layout (counter, tree, rules owned by g_PlayerFaction at the create) and player entity.
 * The arming and its refusals, capture in a local phase (nothing runs, a token for a create, the parts), the slot rules at
 * capture, the wire check, reassembly and the apply on two "clients" in factions mode (the slot's faction scope, the token
 * becoming the same real ID, "player" becoming the slot's faction) and in coop, the authority (not_owner, gone, token,
 * depends, a faction that is not the slot's, the Lua half's verdict), torn and malformed records, faults, the receipt and the
 * pass. The image test: every claim an FFI export of its name in the generated guard table, no family covers it, the pins. */
#include <stdarg.h>
#define TUF_ENTRIES (UIFAC_CLAIMS+1)
static unsigned char *tuf_region;
static __attribute__((aligned(16))) unsigned char tuf_components[6][0x400];
static unsigned char tuf_map_marker[16];
static uint64_t tuf_salt;
static unsigned char tuf_fac[2][0x400];                /* faction 0 `player`, faction 1 x4lan_player2 */
static unsigned char *tuf_object(uint64_t id) {return id>=0x7001 && id<=0x7005?tuf_components[id-0x7000]:NULL;}
static void *tuf_find(void *map,uint64_t id,unsigned flags) {assert(flags==4 && map==(void*)tuf_map_marker);return tuf_object(id);}
static uint64_t tuf_portable(uint64_t id) {return tuf_object(id)?(0xfac000+id)^tuf_salt:0;}
static void *tuf_owner(void *object) {void *f=NULL;memcpy(&f,(unsigned char*)object+0x348,8);return f;}
static void tuf_own(uint64_t id,unsigned faction) {void *f=tuf_fac[faction];memcpy(tuf_object(id)+0x348,&f,8);}
static void *tuf_now(void) {void *f=NULL;memcpy(&f,tuf_region+UITRADE_PLAYER_FACTION,8);return f;}
static void tuf_set_now(void *f) {memcpy(tuf_region+UITRADE_PLAYER_FACTION,&f,8);}
/* Stand-in rule managers in X4's layout: a chain of nodes in ID order (each the right child of the one before). */
#define TUF_NODES 64u
typedef struct {unsigned char *parent,*left,*right;uint64_t color;int32_t key;int32_t pad;unsigned char *value;} tuf_node;
typedef struct {int32_t counter,pad;uint64_t unused;unsigned char *h_parent,*root,*h_right;uint64_t h_color;unsigned char *leftmost;uint64_t size;} tuf_manager;
_Static_assert(__builtin_offsetof(tuf_node,key)==UIFAC_N_KEY && __builtin_offsetof(tuf_node,value)==UIFAC_N_VALUE && __builtin_offsetof(tuf_node,right)==UIFAC_N_RIGHT,"node");
_Static_assert(__builtin_offsetof(tuf_manager,root)==UIFAC_M_ROOT && __builtin_offsetof(tuf_manager,leftmost)==UIFAC_M_LEFTMOST && __builtin_offsetof(tuf_manager,size)==UIFAC_M_SIZE,"manager");
static tuf_manager tuf_managers[UIFAC_KINDS];
static tuf_node tuf_nodes[UIFAC_KINDS][TUF_NODES];
static void *tuf_rules[UIFAC_KINDS][TUF_NODES];        /* a rule: +0 its owner */
static unsigned tuf_node_count[UIFAC_KINDS];
static void tuf_reset_managers(int32_t start) {
    memset(tuf_managers,0,sizeof(tuf_managers));memset(tuf_nodes,0,sizeof(tuf_nodes));memset(tuf_rules,0,sizeof(tuf_rules));memset(tuf_node_count,0,sizeof(tuf_node_count));
    for(unsigned k=1;k<UIFAC_KINDS;k++) {tuf_managers[k].counter=start;tuf_manager *m=&tuf_managers[k];memcpy(tuf_region+uifac_manager_rva[k],&m,8);}
}
static int32_t tuf_insert(unsigned kind,void *owner) {
    tuf_manager *m=&tuf_managers[kind];unsigned i=tuf_node_count[kind]++;assert(i<TUF_NODES);
    tuf_node *n=&tuf_nodes[kind][i];unsigned char *header=(unsigned char*)m+UIFAC_M_HEADER;
    int32_t id=++m->counter;tuf_rules[kind][i]=owner;
    n->key=id;n->value=(unsigned char*)&tuf_rules[kind][i];
    if(!i) {n->parent=header;m->root=(unsigned char*)n;m->leftmost=(unsigned char*)n;}
    else {n->parent=(unsigned char*)&tuf_nodes[kind][i-1];tuf_nodes[kind][i-1].right=(unsigned char*)n;}
    ++m->size;return id;
}
/* The player entity (X4's own player) with the settings' offsets. */
static unsigned char tuf_player[0x8a7e0];
static unsigned char tuf_game[0x400];
/* What a stand-in saw: claim, g_PlayerFaction during the call, every argument's contents. */
static char tuf_seen[UIFAC_CLAIMS][4096];
static unsigned tuf_calls[UIFAC_CLAIMS];
static void *tuf_seen_faction[UIFAC_CLAIMS];
static int32_t tuf_create_answer=-2;                    /* -2: as X4 (++counter), else this */
static int tuf_fault;
static void tuf_cat(char *out,size_t cap,const char *fmt,...) {size_t n=strlen(out);va_list v;va_start(v,fmt);vsnprintf(out+n,cap-n,fmt,v);va_end(v);}
static const char *tuf_s(const char *s) {return s?s:"(null)";}
static unsigned tuf_claim(const char *name) {for(unsigned i=0;i<UIFAC_CLAIMS;i++) if(!strcmp(uifac_claims[i].name,name)) return i;assert(0);return 0;}
static uint64_t tuf_record(unsigned claim,uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5) {
    if(tuf_fault) {volatile int *nothing=NULL;*nothing=1;}
    const uifac_claim *c=&uifac_claims[claim];uint64_t a[UIFAC_ARGS]={a0,a1,a2,a3,a4,a5};char *out=tuf_seen[claim];size_t cap=sizeof(tuf_seen[claim]);
    ++tuf_calls[claim];tuf_seen_faction[claim]=tuf_now();out[0]=0;
    for(unsigned i=0;c->spec[i];i++) {
        uint64_t v=a[i];
        switch(c->spec[i]) {
        case 'I':tuf_cat(out,cap,"%llx;",(unsigned long long)v);break;
        case 'r':tuf_cat(out,cap,"r%d;",(int)(int32_t)v);break;
        case 's':case 'F':tuf_cat(out,cap,"'%s';",tuf_s((const char*)(uintptr_t)v));break;
        case 'b':tuf_cat(out,cap,"b%u;",(unsigned)(v&0xff));break;
        case 'B': {const uifac_blacklist *b=(const uifac_blacklist*)(uintptr_t)v;
            tuf_cat(out,cap,"B%d/%s/%s/%u/",(int)b->id,tuf_s(b->type),tuf_s(b->name),b->usemacrowhitelist);
            for(uint32_t e=0;e<b->nummacros;e++) tuf_cat(out,cap,"m:%s,",b->macros[e]);
            tuf_cat(out,cap,"/%u/",b->usefactionwhitelist);for(uint32_t e=0;e<b->numfactions;e++) tuf_cat(out,cap,"f:%s,",b->factions[e]);
            tuf_cat(out,cap,"/%s/%u;",tuf_s(b->relation),b->hazardous);break;}
        case 'G': {const uifac_fightrule *g=(const uifac_fightrule*)(uintptr_t)v;tuf_cat(out,cap,"G%d/%s/",(int)g->id,tuf_s(g->name));
            for(uint32_t e=0;e<g->numfactions;e++) tuf_cat(out,cap,"%s=%s:%s,",g->factions[e].faction,tuf_s(g->factions[e].civilian),tuf_s(g->factions[e].military));
            tuf_cat(out,cap,";");break;}
        case 'T': {const uifac_traderule *t=(const uifac_traderule*)(uintptr_t)v;tuf_cat(out,cap,"T%d/%s/",(int)t->id,tuf_s(t->name));
            for(uint32_t e=0;e<t->numfactions;e++) tuf_cat(out,cap,"%s,",t->factions[e]);tuf_cat(out,cap,"/%u;",t->iswhitelist);break;}
        case 'L': {const uifac_logo *l=(const uifac_logo*)(uintptr_t)v;tuf_cat(out,cap,"L%s/%s/%u;",tuf_s(l->file),tuf_s(l->icon),l->ispersonal);break;}
        case 'O': {const uifac_supply *s=(const uifac_supply*)(uintptr_t)v;uint32_t n=(uint32_t)a[i+1];tuf_cat(out,cap,"O%u:",n);
            for(uint32_t e=0;e<n;e++) tuf_cat(out,cap,"%s=%d,",tuf_s(s[e].macro),s[e].amount);tuf_cat(out,cap,";");break;}
        default:break;
        }
    }
    if(c->pred==UIFAC_PRED_TOKEN) return tuf_create_answer!=-2?(uint64_t)(uint32_t)tuf_create_answer:(uint64_t)(uint32_t)tuf_insert(c->kind,tuf_now());
    if(!strcmp(c->name,"RemoveBlacklist") || !strcmp(c->name,"RemoveFightRule") || !strcmp(c->name,"RemoveTradeRule")) {
        for(unsigned i=0;i<tuf_node_count[c->kind];i++) if(tuf_nodes[c->kind][i].key==(int32_t)a0) tuf_nodes[c->kind][i].value=NULL;
    }
    if(!strcmp(c->name,"SetPlayerShipsWaitForPlayer")) tuf_player[UIFAC_E_SHIPS_WAIT]=(unsigned char)(a0&0xff);
    return c->ret==UIFAC_RET_BOOL?0x101:0xdeadbeefu;
}
#define TUF(n) static uint64_t tuf_fn_##n(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5) {return tuf_record(n,a0,a1,a2,a3,a4,a5);}
TUF(0) TUF(1) TUF(2) TUF(3) TUF(4) TUF(5) TUF(6) TUF(7) TUF(8) TUF(9) TUF(10) TUF(11) TUF(12) TUF(13) TUF(14) TUF(15) TUF(16)
TUF(17) TUF(18) TUF(19) TUF(20) TUF(21) TUF(22) TUF(23) TUF(24) TUF(25) TUF(26) TUF(27) TUF(28) TUF(29) TUF(30) TUF(31) TUF(32)
static void *const tuf_fns[]={(void*)tuf_fn_0,(void*)tuf_fn_1,(void*)tuf_fn_2,(void*)tuf_fn_3,(void*)tuf_fn_4,(void*)tuf_fn_5,(void*)tuf_fn_6,(void*)tuf_fn_7,
    (void*)tuf_fn_8,(void*)tuf_fn_9,(void*)tuf_fn_10,(void*)tuf_fn_11,(void*)tuf_fn_12,(void*)tuf_fn_13,(void*)tuf_fn_14,(void*)tuf_fn_15,(void*)tuf_fn_16,
    (void*)tuf_fn_17,(void*)tuf_fn_18,(void*)tuf_fn_19,(void*)tuf_fn_20,(void*)tuf_fn_21,(void*)tuf_fn_22,(void*)tuf_fn_23,(void*)tuf_fn_24,(void*)tuf_fn_25,
    (void*)tuf_fn_26,(void*)tuf_fn_27,(void*)tuf_fn_28,(void*)tuf_fn_29,(void*)tuf_fn_30,(void*)tuf_fn_31,(void*)tuf_fn_32};
_Static_assert(sizeof(tuf_fns)/sizeof(*tuf_fns)==UIFAC_CLAIMS,"a stand-in per claim");
static float tuf_level,tuf_resupply;static uint64_t tuf_resupply_of;
static void tuf_level_fn(float v) {unsigned c=tuf_claim("SetPlayerGlobalLoadoutLevel");++tuf_calls[c];tuf_level=v;memcpy(tuf_player+UIFAC_E_LOADOUT,&v,4);}
static void tuf_resupply_fn(uint64_t id,float v) {unsigned c=tuf_claim("SetDefensibleLoadoutLevel");++tuf_calls[c];tuf_resupply_of=id;tuf_resupply=v;tuf_seen_faction[c]=tuf_now();}
static uiguard_entry tuf_table[TUF_ENTRIES];
static unsigned tuf_rva(unsigned i) {return 0x1000u+0x40u*i;}
static void tuf_table_build(void) {
    static const unsigned char pro[5]={0x48,0x89,0x5c,0x24,0x08};
    for(unsigned i=0;i<TUF_ENTRIES;i++) {
        unsigned char *p=tuf_region+tuf_rva(i);memcpy(p,pro,5);
        void *target=i==UIFAC_CLAIMS?(void*)tuf_fn_0:uifac_claims[i].spec[0]=='f'?(void*)tuf_level_fn:uifac_claims[i].spec[1]=='f'?(void*)tuf_resupply_fn:tuf_fns[i];
        p[5]=0x48;p[6]=0xb8;memcpy(p+7,&target,8);p[15]=0xff;p[16]=0xe0;
        uiguard_entry *e=&tuf_table[i];memset(e,0,sizeof(*e));
        e->name=i<UIFAC_CLAIMS?uifac_claims[i].name:"Unclaimed";e->rva=tuf_rva(i);e->kind=UIGUARD_FFI;e->group=UIGUARD_G_FACTION;
        e->checkonly=-1;e->prefix=5;memcpy(e->bytes,pro,5);e->pin=5;e->sha="-";
    }
}
static void *tuf_entry(const char *name) {return tuf_region+tuf_rva(tuf_claim(name));}
typedef uint64_t (*tuf_ptr_fn)(const void*);
typedef void (*tuf_rule_fn)(int32_t);
typedef void (*tuf_bdefault_fn)(int32_t,const char*,const char*,unsigned char);
typedef void (*tuf_ctl_fn)(uint64_t,int32_t,const char*,unsigned char);
typedef void (*tuf_str2_fn)(const char*,const char*);
typedef void (*tuf_bool_fn)(unsigned char);
typedef void (*tuf_float_fn)(float);
typedef void (*tuf_resupply_t)(uint64_t,float);
typedef unsigned char (*tuf_response_fn)(const char*,unsigned char,const char*,const char*,const char*);
typedef void (*tuf_supply_fn)(uint64_t,const uifac_supply*,uint32_t);
/* The wire this client queued, in order; then the queue is empty. */
typedef struct {unsigned op,length;unsigned char data[UIREP_EVENT_MAX];} tuf_event;
static tuf_event tuf_wire[UIREP_QUEUE];static unsigned tuf_wire_count;
static void tuf_take_queue(void) {
    tuf_wire_count=0;
    while(uirep_queue_count) {
        const uirep_event *e=&uirep_queue[uirep_queue_head%UIREP_QUEUE];
        tuf_wire[tuf_wire_count].op=e->op;tuf_wire[tuf_wire_count].length=e->length;memcpy(tuf_wire[tuf_wire_count].data,e->data,e->length);++tuf_wire_count;
        uirep_queue_head=(uirep_queue_head+1)%UIREP_QUEUE;--uirep_queue_count;
    }
}
/* One client applies the wire as slot `slot` (self: the slot faction's ID the Lua half reads; refuse: every record's Lua
 * verdict): the rows, -2/-1 when a part stopped or dropped, -3 when a run stopped. */
static char tuf_rows[4096];
static int tuf_apply(unsigned slot,const char *self,const char *refuse) {
    tuf_rows[0]=0;int result=1;
    InterlockedExchange(&uirep_replaying,1);uirep_ui_depth=0;
    for(unsigned i=0;i<tuf_wire_count && result>0;i++) {
        if(tuf_wire[i].op!=UIFAC_OP_PART) continue;
        const char *why=NULL;int r=uifac_part(slot,tuf_wire[i].data,tuf_wire[i].length,&why);
        if(r<0) {tuf_cat(tuf_rows,sizeof(tuf_rows),"part:%s|",why);result=r;break;}
        if(!r) continue;
        const char *name=NULL;uint64_t subject=0;int s=uifac_ready_subject(slot,&name,&subject);
        if(s<0) {tuf_cat(tuf_rows,sizeof(tuf_rows),"subject_mismatch|");result=-3;break;}
        char what[160];int ran=uifac_run(slot,self,refuse,what,sizeof(what));
        if(ran<0) {tuf_cat(tuf_rows,sizeof(tuf_rows),"stop:%s|",what);result=-3;break;}
        tuf_cat(tuf_rows,sizeof(tuf_rows),"%s|",what);
    }
    InterlockedExchange(&uirep_replaying,0);
    return result;
}
static void tuf_reset_counts(void) {memset(tuf_calls,0,sizeof(tuf_calls));memset(tuf_seen,0,sizeof(tuf_seen));memset(tuf_seen_faction,0,sizeof(tuf_seen_faction));}
static void tuf_client_reset(void) {memset(uifac_rx_slot,0,sizeof(uifac_rx_slot));memset(uifac_map,0,sizeof(uifac_map));uifac_map_count=uifac_map_next=0;uifac_hash=0;}
static int test_uf_other_claim(unsigned index,const uint64_t *frame,uint64_t *ret) {(void)index;(void)frame;(void)ret;return 0;}
static void test_ui_faction(void) {
    unsigned char *saved_base=pnat_base,*saved_image=image;DWORD saved_ui=ui_thread;uint64_t saved_issued=stats.issued,saved_stop=pilot_stop,saved_start=pstk_capture_start;
    int saved_capturing=pstk_capturing,saved_armed=pnat_armed,saved_mode=pnat_mode,saved_slots=pslot_armed,saved_present=pres_armed;
    unsigned saved_capture_slot=pstk_capture_slot,saved_count=pilot_slot_count,saved_mask=pstk_slot_mask;uirep_fns saved_fn=uirep_fn;
    const uiguard_entry *saved_tab=uiguard_tab;unsigned saved_tab_count=uiguard_count;pilot_slot saved_pilot[2];memcpy(saved_pilot,pilot_slots,sizeof(saved_pilot));
    unsigned char *region=VirtualAlloc(NULL,TEST_PNAT_REGION,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(region);
    tuf_region=region;pnat_base=image=region;ui_thread=GetCurrentThreadId();memset(region,0xcc,0x4000);test_uirep_reset();
    {void *map=tuf_map_marker;memcpy(region+UIREP_MAP,&map,8);}
    uirep_fn=(uirep_fns){.find=tuf_find,.portable=tuf_portable};
    uitrade_owner_fn=tuf_owner;uitrade_scope_open=0;
    /* The world: slot 1's ship 0x7001 and station 0x7004 the player's; slot 2's ship 0x7002, station 0x7003 and its module 0x7005
     * x4lan_player2's (factions mode); g_PlayerFaction the player faction; X4's player with its settings. */
    tuf_own(0x7001,0);tuf_own(0x7002,1);tuf_own(0x7003,1);tuf_own(0x7004,0);tuf_own(0x7005,1);tuf_set_now(tuf_fac[0]);
    memset(pilot_slots,0,sizeof(saved_pilot));pilot_slots[0].runtime=0x7001;pilot_slots[1].runtime=0x7002;
    {unsigned char *game=tuf_game,*player=tuf_player;memcpy(region+UIFAC_GAME,&game,8);memcpy(tuf_game+UIFAC_GAME_PLAYER,&player,8);}
    memset(tuf_player,0,sizeof(tuf_player));tuf_player[UIFAC_E_SHIPS_WAIT]=1;tuf_player[UIFAC_E_TAXI_WAITS]=1;tuf_player[UIFAC_E_LOOP_RESERVE]=1;
    tuf_reset_managers(40);tuf_table_build();
    uiguard_tab=tuf_table;uiguard_count=TUF_ENTRIES;uiguard_region=NULL;uiguard_installed=0;uiguard_armed=0;memset(uiguard_on,0,sizeof(uiguard_on));
    uiguard_unclaim(NULL);uifac_armed=0;
    pnat_armed=1;pnat_mode=PNAT_MODE_STOCK;pstk_capturing=1;pilot_slot_count=2;pstk_slot_mask=3;pslot_armed=1;pres_armed=1;
    pstk_capture_start=1;stats.issued=10;pilot_stop=100000;pstk_capture_slot=2;rid_probe_frame=0;pres_open=0;render_rng_in_slot=0;uirep_dispatch_depth=0;
    /* Arming: what it needs, a claim the table lacks, another family's entry, an entry an armed family covers. */
    assert(uirep_arm_using(UIREP_FAMILY_TRAVEL));assert(!uifac_arm_using() && !strcmp(uifac_arm_why,"needs_family"));uirep_armed=0;
    assert(uirep_arm_using(UIREP_FAMILY_FACTION|UIREP_FAMILY_TRAVEL) && (UIREP_FAMILIES&UIREP_FAMILY_FACTION));
    assert(!uifac_ready() && !strcmp(uifac_why,"ui_faction_not_armed"));
    assert(!uifac_arm_using() && !strcmp(uifac_arm_why,"needs_ui_guard"));
    uiguard_groups=0;assert(uiguard_arm_using(1u<<UIGUARD_G_FLIGHT));   /* the guard's groups need not hold the entries */
    tuf_table[3].name="NotSetPlayerBlacklistDefault";assert(!uifac_arm_using() && !strcmp(uifac_arm_why,"claim:SetPlayerBlacklistDefault"));tuf_table[3].name=uifac_claims[3].name;
    assert(uiguard_claim(5,test_uf_other_claim) && !uifac_arm_using() && !strncmp(uifac_arm_why,"claim_taken:",12));
    for(unsigned i=0;i<UIGUARD_MAX;i++) assert(uiguard_claim_by[i]!=uifac_claim_decide);
    uiguard_unclaim(test_uf_other_claim);
    tuf_table[7].covered=UIREP_FAMILY_TRAVEL;assert(!uifac_arm_using() && !strncmp(uifac_arm_why,"claim_covered:",14));tuf_table[7].covered=0;
    assert(uifac_arm_using() && uifac_armed && uifac_ready() && uiguard_claim_by[uifac_guard_of[0]]==uifac_claim_decide);
    assert(uifac_arm_using());   /* a rerun answers 1 */
    /* The guard installs every claimed entry; the unclaimed one stays as it was. */
    assert(uiguard_install() && uiguard_intact());uiguard_live=1;
    for(unsigned i=0;i<UIFAC_CLAIMS;i++) assert(uiguard_on[i] && region[tuf_rva(i)]==0xe9);
    assert(!uiguard_on[UIFAC_CLAIMS] && region[tuf_rva(UIFAC_CLAIMS)]==0x48);
    /* Outside a local phase X4's function runs at once, nothing is captured. */
    tuf_reset_counts();uirep_ui_depth=0;
    ((tuf_str2_fn)tuf_entry("SetFactionDefaultWeaponMode"))("player","attack");
    assert(tuf_calls[tuf_claim("SetFactionDefaultWeaponMode")]==1 && !uirep_queue_count);
    /* ---- client 2 (slot 2), factions mode: the player information menu's blacklist confirm, a weapon mode, a ship's rule ---- */
    const char *macros[2]={"cluster_01_sector001_macro","cluster_02_sector001_macro"},*factions[3]={"player","argon","x4lan_player2"};
    uifac_blacklist bl={0,"sectortravel","Keep out",1,2,(const char**)macros,1,3,(const char**)factions,"enemy",1};
    tuf_reset_counts();uirep_ui_depth=1;
    int32_t token=(int32_t)((tuf_ptr_fn)tuf_entry("CreateBlacklist2"))(&bl);
    assert(uifac_is_token(token) && ((uint32_t)token>>16&0xff)==2 && !tuf_calls[0] && uirep_queue_count==1 && uifac_captured==1);
    ((tuf_bdefault_fn)tuf_entry("SetPlayerBlacklistDefault"))(token,"sectortravel","civilian",1);
    ((tuf_str2_fn)tuf_entry("SetFactionDefaultWeaponMode"))("player","attack");
    ((tuf_ctl_fn)tuf_entry("SetControllableBlacklist"))(0x7002,token,"sectortravel",1);
    assert(uirep_queue_count==4 && !tuf_calls[tuf_claim("SetPlayerBlacklistDefault")]);
    /* The player entity's settings are refused for a slot of another faction: nothing queued, a row. */
    ((tuf_bool_fn)tuf_entry("SetPlayerShipsWaitForPlayer"))(0);assert(uirep_queue_count==4 && !tuf_calls[tuf_claim("SetPlayerShipsWaitForPlayer")]);
    {char rows[256];assert(uifac_refusals_text(rows,sizeof(rows)) && strstr(rows,":SetPlayerShipsWaitForPlayer:player_entity"));}
    /* A trade rule with many factions: several parts; a float in xmm1; supply overrides; a component the map does not know. */
    static char names[200][24];static const char *many[200];for(unsigned i=0;i<200;i++) {snprintf(names[i],sizeof(names[i]),"faction_%03u",i);many[i]=names[i];}
    many[0]="player";
    uifac_traderule tr={0,"Long list",200,(const char**)many,1};
    int32_t ttoken=(int32_t)((tuf_ptr_fn)tuf_entry("CreateTradeRule"))(&tr);
    assert(uifac_is_token(ttoken) && ttoken!=token && uirep_queue_count>=4+10);
    ((tuf_resupply_t)tuf_entry("SetDefensibleLoadoutLevel"))(0x7003,0.375f);
    uifac_supply supply[2]={{"ship_gen_s_fightingdrone_01_a_macro",4},{NULL,-1}};
    ((tuf_supply_fn)tuf_entry("UpdateSupplyOverrides"))(0x7003,supply,2);
    unsigned queued=uirep_queue_count;
    ((tuf_ctl_fn)tuf_entry("SetControllableBlacklist"))(0x7999,token,"sectortravel",1);assert(uirep_queue_count==queued);   /* unmapped */
    ((tuf_ptr_fn)tuf_entry("CreateBlacklist2"))(NULL);assert(uirep_queue_count==queued);                                   /* unreadable */
    {char rows[512];assert(uifac_refusals_text(rows,sizeof(rows)) && strstr(rows,"SetControllableBlacklist:unmapped") && strstr(rows,"CreateBlacklist2:memory"));}
    for(unsigned i=0;i<UIFAC_CLAIMS;i++) assert(!tuf_calls[i]);
    uirep_ui_depth=0;tuf_take_queue();
    /* The wire's section check: the parts with the family, refused without it. */
    {const char *why=NULL;assert(uirep_event_ok(2,UIFAC_OP_PART,tuf_wire[0].data,tuf_wire[0].length,&why));
     unsigned f=uirep_families;uirep_families=UIREP_FAMILY_TRAVEL;assert(!uirep_event_ok(2,UIFAC_OP_PART,tuf_wire[0].data,tuf_wire[0].length,&why) && !strcmp(why,"ui_faction"));uirep_families=f;
     unsigned char bad[4]={0,3,3,0};assert(!uifac_event_ok(2,UIFAC_OP_PART,bad,4,&why));assert(!strcmp(uirep_op_name(UIFAC_OP_PART),"faction_part"));}
    /* Two clients apply the same wire: the same calls, the same real IDs, in slot 2's faction scope; "player" is x4lan_player2. */
    char rows_a[4096];char frames[2][512];
    for(int client=0;client<2;client++) {
        tuf_reset_counts();tuf_client_reset();tuf_reset_managers(40);tuf_insert(UIFAC_K_BLACKLIST,tuf_fac[0]);   /* the player's rule 41 */
        assert(tuf_apply(2,"x4lan_player2",NULL)==1);
        assert(tuf_calls[0]==1 && tuf_seen_faction[0]==tuf_fac[1] && tuf_now()==tuf_fac[0]);
        assert(!strcmp(tuf_seen[0],"B0/sectortravel/Keep out/1/m:cluster_01_sector001_macro,m:cluster_02_sector001_macro,/1/f:x4lan_player2,f:argon,f:x4lan_player2,/enemy/1;"));
        assert(!strcmp(tuf_seen[tuf_claim("SetPlayerBlacklistDefault")],"r42;'sectortravel';'civilian';b1;"));
        assert(!strcmp(tuf_seen[tuf_claim("SetFactionDefaultWeaponMode")],"'x4lan_player2';'attack';"));
        assert(!strcmp(tuf_seen[tuf_claim("SetControllableBlacklist")],"7002;r42;'sectortravel';b1;"));
        assert(strstr(tuf_seen[tuf_claim("CreateTradeRule")],"T0/Long list/x4lan_player2,faction_001,") && strstr(tuf_seen[tuf_claim("CreateTradeRule")],"faction_199,/1;"));
        assert(tuf_resupply_of==0x7003 && tuf_resupply==0.375f && tuf_seen_faction[tuf_claim("SetDefensibleLoadoutLevel")]==tuf_fac[1]);
        assert(!strcmp(tuf_seen[tuf_claim("UpdateSupplyOverrides")],"7003;O2:ship_gen_s_fightingdrone_01_a_macro=4,(null)=-1,;"));
        assert(strstr(tuf_rows,"CreateBlacklist2 scope=42|SetPlayerBlacklistDefault scope|SetFactionDefaultWeaponMode scope|SetControllableBlacklist scope|CreateTradeRule scope=41|"));
        assert(uifac_frame_text(frames[client],sizeof(frames[client]),1));
        assert(strstr(frames[client],"UIFAC_1:b42.2,f40.0,t41.1;1.0.0,1.0.1;w111.00000000;h"));
        if(!client) memcpy(rows_a,tuf_rows,sizeof(rows_a));else assert(!strcmp(rows_a,tuf_rows));
    }
    assert(!strcmp(frames[0],frames[1]));
    /* ---- authority at the apply, the same on every client ---- */
    {   /* the player's rule 41 removed by slot 2 (factions): not_owner; the same by slot 1: runs */
        tuf_reset_counts();tuf_client_reset();uirep_ui_depth=1;
        ((tuf_rule_fn)tuf_entry("RemoveBlacklist"))(41);((tuf_rule_fn)tuf_entry("RemoveBlacklist"))(77);   /* 77: no such rule */
        ((tuf_rule_fn)tuf_entry("RemoveBlacklist"))((int32_t)(UIFAC_TOKEN|(2u<<16)|0x777u));            /* a token nobody created */
        ((tuf_str2_fn)tuf_entry("SetFactionBuildMethod"))("argon","default");                              /* another faction */
        uirep_ui_depth=0;tuf_take_queue();
        assert(tuf_apply(2,"x4lan_player2",NULL)==1 && !tuf_calls[tuf_claim("RemoveBlacklist")]);
        assert(strstr(tuf_rows,"RemoveBlacklist skipped=not_owner|RemoveBlacklist skipped=gone|RemoveBlacklist skipped=token|SetFactionBuildMethod skipped=not_owner|"));
        assert(tuf_apply(1,"player",NULL)==1 && tuf_calls[tuf_claim("RemoveBlacklist")]==1 && !strstr(tuf_rows,"RemoveBlacklist=") && strstr(tuf_rows,"RemoveBlacklist|"));
        assert(strstr(tuf_rows,"RemoveBlacklist skipped=token|"));   /* slot 2's token is no rule of slot 1's either */
    }
    /* A create X4 answers with 0: what names its token is skipped (depends), the same everywhere. */
    tuf_reset_counts();tuf_client_reset();uirep_ui_depth=1;
    {uifac_fightrule fr={0,"Defend",0,NULL};int32_t ft=(int32_t)((tuf_ptr_fn)tuf_entry("CreateFightRule"))(&fr);
     ((tuf_ctl_fn)tuf_entry("SetControllableFightRule"))(0x7002,ft,"attack",1);
     ((tuf_ctl_fn)tuf_entry("SetControllableFightRule"))(0x7002,-1,"attack",0);}
    uirep_ui_depth=0;tuf_take_queue();
    tuf_create_answer=0;assert(tuf_apply(2,"x4lan_player2",NULL)==1);tuf_create_answer=-2;
    assert(strstr(tuf_rows,"CreateFightRule scope=0|SetControllableFightRule skipped=depends|SetControllableFightRule scope|"));
    assert(!strcmp(tuf_seen[tuf_claim("SetControllableFightRule")],"7002;r-1;'attack';b0;"));
    /* An update and a removal naming the token of a create made in an earlier click: the real ID, the slot's faction's rule. */
    tuf_reset_counts();tuf_client_reset();uirep_ui_depth=1;
    {uifac_blacklist b2={0,"sectoractivity","Two",0,0,NULL,0,0,NULL,NULL,0};int32_t t2=(int32_t)((tuf_ptr_fn)tuf_entry("CreateBlacklist2"))(&b2);
     b2.id=(uint32_t)t2;b2.name="Two renamed";((tuf_ptr_fn)tuf_entry("UpdateBlacklist2"))(&b2);((tuf_rule_fn)tuf_entry("RemoveBlacklist"))(t2);}
    uirep_ui_depth=0;tuf_take_queue();tuf_reset_managers(40);
    assert(tuf_apply(2,"x4lan_player2",NULL)==1 && strstr(tuf_rows,"CreateBlacklist2 scope=41|UpdateBlacklist2 scope|RemoveBlacklist scope|"));
    assert(!strcmp(tuf_seen[tuf_claim("UpdateBlacklist2")],"B41/sectoractivity/Two renamed/0//0//(null)/0;") && !strcmp(tuf_seen[tuf_claim("RemoveBlacklist")],"r41;"));
    /* The Lua half's verdict, a subject gone, a subject whose identity differs, the two faction readings that differ. */
    tuf_reset_counts();tuf_client_reset();uirep_ui_depth=1;((tuf_ctl_fn)tuf_entry("SetControllableBlacklist"))(0x7004,-1,"sectortravel",0);uirep_ui_depth=0;tuf_take_queue();
    assert(tuf_apply(2,"x4lan_player2","not_owner")==1 && strstr(tuf_rows,"SetControllableBlacklist skipped=not_owner|") && !tuf_calls[tuf_claim("SetControllableBlacklist")]);
    tuf_client_reset();assert(tuf_apply(2,"",NULL)==1 && strstr(tuf_rows,"skipped=no_faction"));
    tuf_client_reset();assert(tuf_apply(2,"player",NULL)==-3 && strstr(tuf_rows,"stop:ui_faction_view"));
    tuf_client_reset();tuf_salt=1;assert(tuf_apply(2,"x4lan_player2",NULL)==-3 && strstr(tuf_rows,"subject_mismatch"));tuf_salt=0;
    /* Gone: the record names a runtime ID the map does not know (any more), the same everywhere. */
    tuf_client_reset();tuf_wire[0].data[UIFAC_PART_HEAD+1+3]^=0x80;
    assert(tuf_apply(2,"x4lan_player2",NULL)==1 && strstr(tuf_rows,"SetControllableBlacklist skipped=gone|"));
    /* ---- coop: slot 2's ship and station are the player's; the entity's settings for every slot ---- */
    tuf_own(0x7002,0);tuf_own(0x7003,0);
    tuf_reset_counts();tuf_client_reset();uirep_ui_depth=1;
    ((tuf_bool_fn)tuf_entry("SetPlayerShipsWaitForPlayer"))(0);
    ((tuf_float_fn)tuf_entry("SetPlayerGlobalLoadoutLevel"))(0.75f);
    ((tuf_str2_fn)tuf_entry("SetFactionDefaultWeaponMode"))("player","defend");
    assert(((tuf_response_fn)tuf_entry("SetDefaultResponseToSignalForFaction2"))("flee",0,"attacked","player","")==1);
    {uifac_logo logo={"logo.dds",NULL,1};((tuf_ptr_fn)tuf_entry("SetPlayerLogo"))(&logo);}
    uirep_ui_depth=0;tuf_take_queue();assert(tuf_wire_count==5);
    tuf_reset_managers(40);assert(tuf_apply(2,"player",NULL)==1);
    assert(tuf_player[UIFAC_E_SHIPS_WAIT]==0 && tuf_level==0.75f && !strcmp(tuf_seen[tuf_claim("SetFactionDefaultWeaponMode")],"'player';'defend';"));
    assert(tuf_seen_faction[tuf_claim("SetFactionDefaultWeaponMode")]==tuf_fac[0] && !strcmp(tuf_seen[tuf_claim("SetPlayerLogo")],"Llogo.dds/(null)/1;"));
    assert(strstr(tuf_rows,"SetPlayerShipsWaitForPlayer|SetPlayerGlobalLoadoutLevel|SetFactionDefaultWeaponMode|SetDefaultResponseToSignalForFaction2=1|SetPlayerLogo|"));
    {char frame[512];assert(uifac_frame_text(frame,sizeof(frame),0) && strstr(frame,";w011.3f400000") && !strstr(frame,";h"));}
    /* Factions mode again: a client that sends an entity setting of slot 2 anyway is skipped everywhere. */
    tuf_own(0x7002,1);tuf_own(0x7003,1);tuf_client_reset();tuf_reset_counts();
    assert(tuf_apply(2,"x4lan_player2",NULL)==1 && strstr(tuf_rows,"SetPlayerShipsWaitForPlayer skipped=player_entity|") && !tuf_calls[tuf_claim("SetPlayerShipsWaitForPlayer")]);
    /* ---- the faction view (factions mode, client 2's own menus): X4's player faction is slot 2's for a wrapped menu function ---- */
    uirep_ui_depth=0;assert(uifac_view_enter()==-1 && tuf_now()==tuf_fac[0]);                     /* not a local phase */
    uirep_ui_depth=1;InterlockedExchange(&uirep_replaying,1);assert(uifac_view_enter()==-1);InterlockedExchange(&uirep_replaying,0);   /* an apply */
    assert(uifac_view_enter()==1 && tuf_now()==tuf_fac[1] && uifac_player_faction()==tuf_fac[0]);
    assert(uifac_view_enter()==2 && uifac_views_nested==1);                                         /* nested: the outer one closes it */
    {unsigned queued=uirep_queue_count;tuf_reset_counts();
     ((tuf_bool_fn)tuf_entry("SetPlayerShipsWaitForPlayer"))(1);                                  /* still slot 2's entity rule, not the view's */
     assert(uirep_queue_count==queued && !tuf_calls[tuf_claim("SetPlayerShipsWaitForPlayer")]);
     char rows[256];assert(uifac_refusals_text(rows,sizeof(rows)) && strstr(rows,"SetPlayerShipsWaitForPlayer:player_entity"));}
    {char what[64];uifac_rx_slot[1].state=UIFAC_RX_READY;uifac_rx_slot[1].claim=tuf_claim("SetPlayerFactionName");
     assert(uifac_run(2,"x4lan_player2",NULL,what,sizeof(what))==-1 && !strcmp(what,"ui_faction_view_open"));}   /* an apply never inside it */
    assert(uifac_view_leave() && tuf_now()==tuf_fac[0] && !uifac_view_open);
    assert(uifac_view_enter()==1);tuf_set_now(tuf_fac[1]+8);assert(!uifac_view_leave() && tuf_now()==tuf_fac[0]);   /* changed inside: put back, reported */
    assert(uifac_view_enter()==1 && !uifac_pass(7) && !strcmp(uifac_why,"ui_faction_view_open") && tuf_now()==tuf_fac[0] && !uifac_view_open);uifac_why="-";
    tuf_own(0x7002,0);assert(uifac_view_enter()==0 && tuf_now()==tuf_fac[0]);tuf_own(0x7002,1);  /* coop: never needed */
    pilot_slots[1].runtime=0;assert(uifac_view_enter()==-1);pilot_slots[1].runtime=0x7002;       /* no ship */
    uirep_ui_depth=0;
    /* ---- torn, malformed, overlap, a fault, a scope left open ---- */
    tuf_reset_counts();tuf_client_reset();uirep_ui_depth=1;((tuf_ptr_fn)tuf_entry("CreateTradeRule"))(&tr);uirep_ui_depth=0;tuf_take_queue();
    assert(tuf_wire_count>=2);
    {tuf_event keep=tuf_wire[1];tuf_wire[1]=tuf_wire[tuf_wire_count-1];assert(tuf_apply(2,"x4lan_player2",NULL)==-1 && strstr(tuf_rows,"part:torn"));tuf_wire[1]=keep;}
    tuf_client_reset();{unsigned char keep=tuf_wire[0].data[UIFAC_PART_HEAD];tuf_wire[0].data[UIFAC_PART_HEAD]=UIFAC_CLAIMS;
     assert(tuf_apply(2,"x4lan_player2",NULL)==-2 && strstr(tuf_rows,"part:ui_faction_record"));tuf_wire[0].data[UIFAC_PART_HEAD]=keep;}
    tuf_client_reset();{unsigned char keep=tuf_wire[0].data[UIFAC_PART_HEAD+3];tuf_wire[0].data[UIFAC_PART_HEAD+3]=1;   /* the token names slot 1 */
     assert(tuf_apply(2,"x4lan_player2",NULL)==-2);tuf_wire[0].data[UIFAC_PART_HEAD+3]=keep;}
    tuf_client_reset();{unsigned char one[1+4+UIFAC_PART_HEAD]={0,0,1,(unsigned char)tuf_claim("RemoveTradeRule"),5,0,0,0};const char *why=NULL;
     assert(uifac_part(2,one,8,&why)==1 && uifac_part(2,one,8,&why)==-2 && !strcmp(why,"ui_faction_overlap"));}
    tuf_client_reset();uirep_ui_depth=1;((tuf_str2_fn)tuf_entry("SetFactionDefaultWeaponMode"))("player","attack");uirep_ui_depth=0;tuf_take_queue();
    tuf_fault=1;assert(tuf_apply(2,"x4lan_player2",NULL)==-3 && strstr(tuf_rows,"stop:ui_fault") && !uitrade_scope_open && tuf_now()==tuf_fac[0]);tuf_fault=0;
    InterlockedExchange(&uirep_replaying,1);assert(uitrade_scope_enter(2)==1);InterlockedExchange(&uirep_replaying,0);
    assert(!uifac_pass(5) && !strcmp(uifac_why,"ui_faction_scope_open") && !uitrade_scope_open && tuf_now()==tuf_fac[0]);uifac_why="-";
    assert(uifac_pass(6));
    {char text[512];assert(uifac_status(text,sizeof(text)) && strstr(text,"ui_faction=armed") && strstr(text,"claims=33"));}
    /* The probe's own frame and an apply pass to X4's function; a query-free claim never declines in a local phase. */
    tuf_reset_counts();uirep_ui_depth=1;InterlockedExchange(&rid_probe_frame,1);((tuf_bool_fn)tuf_entry("SetPlayerTaxiWaitsForPlayer"))(1);
    InterlockedExchange(&rid_probe_frame,0);assert(tuf_calls[tuf_claim("SetPlayerTaxiWaitsForPlayer")]==1 && !uirep_queue_count);uirep_ui_depth=0;
    uiguard_uninstall();uiguard_unclaim(NULL);uiguard_armed=0;uiguard_live=0;uifac_armed=0;
    if(uiguard_region) {VirtualFree(uiguard_region,0,MEM_RELEASE);uiguard_region=NULL;}
    uitrade_owner_fn=NULL;uiguard_tab=saved_tab;uiguard_count=saved_tab_count;memcpy(pilot_slots,saved_pilot,sizeof(saved_pilot));
    test_uirep_reset();VirtualFree(region,0,MEM_RELEASE);
    pnat_base=saved_base;image=saved_image;uirep_fn=saved_fn;ui_thread=saved_ui;stats.issued=saved_issued;pilot_stop=saved_stop;pstk_capture_start=saved_start;
    pstk_capturing=saved_capturing;pnat_armed=saved_armed;pnat_mode=saved_mode;pslot_armed=saved_slots;pres_armed=saved_present;pstk_capture_slot=saved_capture_slot;
    pilot_slot_count=saved_count;pstk_slot_mask=saved_mask;
    printf("UI faction: claims, capture, tokens, parts, factions and coop apply, authority, receipt and pass on stand-ins\n");
}
static void test_ui_faction_native_image(const char *path) {
    unsigned char *prior=image,*prior_base=pnat_base;const uiguard_entry *saved_tab=uiguard_tab;unsigned saved_count=uiguard_count;
    image=test_pmath_map(path);pnat_base=image;uiguard_tab=uiguard_table;uiguard_count=UIGUARD_ENTRIES;
    uifac_pin_tab=uifac_pin_table;uifac_pin_count=UIFAC_PIN_COUNT;uifac_pins_checked=0;assert(uifac_pins());
    /* Every claim is an FFI export of its name in the generated table, and no family covers it (one patch per entry). */
    unsigned dir=test_uiguard_export_dir(path);assert(dir);
    for(unsigned c=0;c<UIFAC_CLAIMS;c++) {
        const uiguard_entry *e=NULL;
        for(unsigned i=0;i<UIGUARD_ENTRIES;i++) if(uiguard_table[i].kind==UIGUARD_FFI && !strcmp(uiguard_table[i].name,uifac_claims[c].name)) e=&uiguard_table[i];
        assert(e && test_uiguard_export(image,dir,e->name)==e->rva && !e->covered && e->checkonly<0 && e->ret==UIGUARD_RET_ZERO);
        assert(e->group==UIGUARD_G_FACTION || e->group==UIGUARD_G_FLEET || e->group==UIGUARD_G_TRADE);
        for(unsigned i=0;uifac_claims[c].spec[i];i++) if(uifac_claims[c].spec[i]=='I') assert(uifac_claims[c].subject==(signed char)i);   /* the only component is the subject */
    }
    /* The rule managers' lookups the run mirrors: RemoveBlacklist / RemoveFightRule / RemoveTradeRule read [manager]+0x18, the
     * key at +0x20, the children at +8 / +0x10, the rule at +0x28 (their bytes, the guard pins them too). */
    static const unsigned removes[3]={0x1b62e0u,0x1b6670u,0x1b6b70u};
    for(unsigned k=0;k<3;k++) {
        const unsigned char *p=image+removes[k];int32_t rel=0;memcpy(&rel,p+7,4);
        assert(p[4]==0x4c && p[5]==0x8b && p[6]==0x0d && removes[k]+11u+(unsigned)rel==uifac_manager_rva[k+1]);   /* mov r9,[manager] */
        assert(p[11]==0x49 && p[12]==0x8b && p[13]==0x41 && p[14]==UIFAC_M_ROOT && p[0x20]==0x39 && p[0x22]==UIFAC_N_KEY);
    }
    /* Tampered code refuses. */
    image[0x371370u+0x20]^=1;uifac_pins_checked=0;assert(!uifac_pins() && pin_failed_rva==0x371370u);image[0x371370u+0x20]^=1;
    image[0x1f0e70u+0x26]^=1;uifac_pins_checked=0;assert(!uifac_pins() && pin_failed_rva==0x1f0e70u);image[0x1f0e70u+0x26]^=1;
    uifac_pins_checked=0;assert(uifac_pins());uifac_pins_checked=0;
    uiguard_tab=saved_tab;uiguard_count=saved_count;
    VirtualFree(image,0,MEM_RELEASE);image=prior;pnat_base=prior_base;
    printf("UI faction verified on the pinned image: %u claims, %u pinned functions, the three rule lookups\n",(unsigned)UIFAC_CLAIMS,(unsigned)UIFAC_PIN_COUNT);
}

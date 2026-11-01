/* UI replay, trade family (lockstep_ui_trade.h) on a private executable region: its entries looked up by name in a stand-in
 * guard table of the real table's shape, the pins and their refusals, the install through the pass and every kind of hook
 * called for real through its entry (a component, an int32, a string, a NULL string, a bool, a float in xmm1) outside the
 * local phases (X4's own: arguments intact), inside them (one event, nothing ran), in the probe's frame and during an apply
 * (passed); the section rules; the apply in the slot's faction scope (factions mode: X4's function sees slot k's faction as
 * the player faction and it comes back after; coop and slot 1: nothing changes), a subject gone, identities that differ, no
 * faction, a scope changed under the call, a fault inside it, a scope outside an apply; the balances through the account walk
 * and the receipt; the pass (a scope left open, a hook lost, the window end). The image test: every entry, Lua global and
 * pinned function on the pinned X4.exe, the ship vtable's owner slot, a whole install on the real bytes and back. */
#define TUT_BASE 0x2000u
#define TUT_STRIDE 0x40u
#define TUT_ENTRIES (UITRADE_HOOKS+UITRADE_LUA_NAMES)
typedef uint64_t (*tut_int_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef void (*tut_float_fn)(uint64_t,float);
static uiguard_entry test_uitrade_tab[TUT_ENTRIES];
static char test_uitrade_shas[TUT_ENTRIES][65];
static unsigned char *test_uitrade_region;
static uint64_t test_uitrade_args[UITRADE_ARGS];
static float test_uitrade_float;
static unsigned test_uitrade_calls;
static void *test_uitrade_seen;          /* g_PlayerFaction while X4's function ran */
static int test_uitrade_change,test_uitrade_fault;
static void *test_uitrade_now(void) {void *f=NULL;memcpy(&f,test_uitrade_region+UITRADE_PLAYER_FACTION,8);return f;}
static void test_uitrade_set_now(void *f) {memcpy(test_uitrade_region+UITRADE_PLAYER_FACTION,&f,8);}
static uint64_t test_uitrade_int(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e) {
    if(test_uitrade_fault) {volatile int *nothing=NULL;*nothing=1;}
    uint64_t v[UITRADE_ARGS]={a,b,c,d,e};memcpy(test_uitrade_args,v,sizeof(v));++test_uitrade_calls;test_uitrade_seen=test_uitrade_now();
    if(test_uitrade_change) test_uitrade_set_now((void*)(uintptr_t)1);
    return 0;
}
static void test_uitrade_flt(uint64_t a,float f) {test_uitrade_args[0]=a;test_uitrade_float=f;++test_uitrade_calls;test_uitrade_seen=test_uitrade_now();}
/* The world: two slot ships, two stations, two factions with an account each (cents), the null account. */
static __attribute__((aligned(16))) unsigned char test_uitrade_obj[5][0x400],test_uitrade_fac[2][0x400],test_uitrade_data[2][0x400];
typedef struct {void **vtable;int64_t cents;} tut_account;
static int64_t test_uitrade_cents(void *account) {return ((tut_account*)account)->cents;}
static void *test_uitrade_vt[4]={NULL,NULL,(void*)test_uitrade_cents,NULL};
static tut_account test_uitrade_acc[3]={{test_uitrade_vt,0},{test_uitrade_vt,0},{test_uitrade_vt,0}};
static int test_uitrade_no_account;
static uint64_t test_uitrade_salt;
static unsigned char *test_uitrade_object(uint64_t id) {return id>=0x7001 && id<=0x7005?test_uitrade_obj[id-0x7001]:NULL;}
static void *test_uitrade_find(void *map,uint64_t id,unsigned flags) {(void)map;assert(flags==4);return test_uitrade_object(id);}
static uint64_t test_uitrade_portable(uint64_t id) {return test_uitrade_object(id)?(id*31u)^test_uitrade_salt:0;}
static void *test_uitrade_owner(void *object) {void *f=NULL;memcpy(&f,(unsigned char*)object+0x348,8);return f;}
static void test_uitrade_own(uint64_t id,unsigned faction) {void *f=test_uitrade_fac[faction];memcpy(test_uitrade_object(id)+0x348,&f,8);}
static void *test_uitrade_account(void *handle) {
    for(unsigned i=0;i<2;i++) if(handle==test_uitrade_data[i]+UITRADE_FACTION_ACCOUNT) return test_uitrade_no_account && i==1?NULL:&test_uitrade_acc[i];
    return NULL;
}
static unsigned test_uitrade_rva(unsigned hook) {return TUT_BASE+TUT_STRIDE*hook;}
static void test_uitrade_entries(void) {
    static const unsigned char pro[5]={0x48,0x89,0x5c,0x24,0x08};
    for(unsigned i=0;i<TUT_ENTRIES;i++) {
        uiguard_entry *e=&test_uitrade_tab[i];unsigned rva=TUT_BASE+TUT_STRIDE*i;unsigned char code[32];unsigned length;
        memset(e,0,sizeof(*e));memcpy(code,pro,5);
        if(i<UITRADE_HOOKS) {   /* mov [rsp+8],rbx; jmp [rip] the stand-in: called through its entry, X4's shape */
            void *target=strchr(uitrade_defs[i].spec,'f')?(void*)test_uitrade_flt:(void*)test_uitrade_int;
            length=5+pview_abs_jump(code+5,target);e->name=uitrade_defs[i].name;e->kind=UIGUARD_FFI;
        } else {code[5]=0xc3;length=6;e->name=uitrade_lua_names[i-UITRADE_HOOKS];e->kind=UIGUARD_LUA;}
        memcpy(test_uitrade_region+rva,code,length);
        e->rva=rva;e->group=UIGUARD_G_TRADE;e->covered=UIREP_FAMILY_TRADE;e->ret=UIGUARD_RET_ZERO;e->checkonly=-1;e->prefix=5;memcpy(e->bytes,code,5);
        e->pin=length;test_uiguard_sha(code,length,test_uitrade_shas[i]);e->sha=test_uitrade_shas[i];
    }
}
static const uirep_event *test_uitrade_last(void) {assert(uirep_queue_count);return &uirep_queue[(uirep_queue_head+uirep_queue_count-1)%UIREP_QUEUE];}
static void test_ui_trade(void) {
    unsigned char *saved_base=pnat_base,*saved_image=image;uirep_fns saved_fn=uirep_fn;DWORD saved_ui=ui_thread;uint64_t saved_issued=stats.issued;unsigned saved_slot=stats.slot;
    uint64_t saved_stop=pilot_stop,saved_start=pstk_capture_start;unsigned saved_delay=pilot_delay,saved_count=pilot_slot_count,saved_mask=pstk_slot_mask;
    int saved_capturing=pstk_capturing,saved_armed=pnat_armed,saved_mode=pnat_mode,saved_slots=pslot_armed,saved_present=pres_armed;
    const uiguard_entry *saved_tab=uiguard_tab;unsigned saved_tab_count=uiguard_count;pilot_slot saved_pilot[2];memcpy(saved_pilot,pilot_slots,sizeof(saved_pilot));
    unsigned char *region=VirtualAlloc(NULL,TEST_PNAT_REGION,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(region);
    test_uitrade_region=region;pnat_base=image=region;ui_thread=GetCurrentThreadId();test_uirep_reset();
    {void *map=test_uitrade_obj;memcpy(region+UIREP_MAP,&map,8);}
    uirep_fn=(uirep_fns){.find=test_uitrade_find,.portable=test_uitrade_portable};
    test_uitrade_entries();uiguard_tab=test_uitrade_tab;uiguard_count=TUT_ENTRIES;
    uitrade_pin_count=0;uitrade_pins_checked=0;uitrade_owner_fn=test_uitrade_owner;uitrade_account_fn=test_uitrade_account;
    for(unsigned i=0;i<UITRADE_HOOKS;i++) uitrade_tramp[i]=NULL;
    uitrade_installed=0;uitrade_restored=1;uitrade_stage="off";uitrade_why_text="-";uitrade_scope_open=0;uitrade_page=NULL;
    uitrade_captured=uitrade_passed=uitrade_refused=uitrade_runs=uitrade_skipped=uitrade_scopes=uitrade_scope_same=uitrade_scope_refused=0;
    for(unsigned f=0;f<2;f++) {unsigned char *d=test_uitrade_data[f];memcpy(test_uitrade_fac[f]+UITRADE_FACTION_DATA,&d,8);}
    {tut_account *null_account=&test_uitrade_acc[2];test_uitrade_acc[2].cents=777;memcpy(region+UITRADE_NULL_ACCOUNT,&null_account,8);}   /* never read */
    test_uitrade_acc[0].cents=500000;test_uitrade_acc[1].cents=12345;
    /* The world: slot 1's ship 0x7001 and the station 0x7004 are the player's (faction 0), slot 2's ship 0x7002 and the station
     * 0x7003 are x4lan_player2's (faction 1); g_PlayerFaction is the player faction. */
    test_uitrade_own(0x7001,0);test_uitrade_own(0x7002,1);test_uitrade_own(0x7003,1);test_uitrade_own(0x7004,0);test_uitrade_own(0x7005,0);
    test_uitrade_set_now(test_uitrade_fac[0]);
    memset(pilot_slots,0,sizeof(saved_pilot));pilot_slots[0].runtime=0x7001;pilot_slots[1].runtime=0x7002;
    /* Arming the replay with the family alone. */
    pnat_armed=1;pnat_mode=PNAT_MODE_STOCK;pstk_capturing=1;pilot_slot_count=2;pstk_slot_mask=3;pslot_armed=1;pres_armed=1;
    assert(uirep_arm_using(UIREP_FAMILY_TRADE) && uitrade_wanted() && (UIREP_FAMILIES&UIREP_FAMILY_TRADE));
    /* The section: the family's ops, its globals' numbers, every FFI byte accounted for. */
    {const char *why="";uint16_t c=0;
     unsigned char s[64]={UIREP_VERSION,0x64,0,1,UITRADE_OP_LUA,2,1,0};
     assert(uirep_section_ok(2,s,8,&c,&why));
     s[6]=0;assert(!uirep_section_ok(2,s,8,&c,&why) && !strcmp(why,"ui_trade"));
     s[6]=UITRADE_LUA_GLOBALS+1;assert(!uirep_section_ok(2,s,8,&c,&why) && !strcmp(why,"ui_trade"));
     s[6]=UITRADE_LUA_GLOBALS;assert(uirep_section_ok(1,s,8,&c,&why));
     unsigned char f[64]={UIREP_VERSION,0x64,0,1,UITRADE_OP_FFI,1+UIREP_REF,UITRADE_H_PRODUCTION_OFFERS};
     assert(uirep_section_ok(2,f,6+1+UIREP_REF,&c,&why));
     f[5]=UIREP_REF;assert(!uirep_section_ok(2,f,6+UIREP_REF,&c,&why) && !strcmp(why,"ui_trade"));
     f[5]=1+UIREP_REF;f[6]=UITRADE_HOOKS;assert(!uirep_section_ok(2,f,6+1+UIREP_REF,&c,&why) && !strcmp(why,"ui_trade"));f[6]=UITRADE_H_PRODUCTION_OFFERS;
     {unsigned fam=uirep_families;uirep_families=UIREP_FAMILY_ORDERS;assert(!uirep_section_ok(2,s,8,&c,&why) && !strcmp(why,"ui_trade"));uirep_families=fam;}
     unsigned char b[]={UITRADE_H_WORKFORCE,0,0,0,0,0,0,0,0,0,0,0,0,2};   /* a bool is 0 or 1 */
     assert(!uitrade_length(b,sizeof(b)));b[13]=1;assert(uitrade_length(b,sizeof(b))==sizeof(b));
     unsigned char z[]={UITRADE_H_TRADE_RULE,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,1,'x',0xff,1};   /* ruletype "x", ware NULL */
     assert(uitrade_length(z,sizeof(z))==sizeof(z));z[19]=UIREP_STRING_MAX+1;assert(!uitrade_length(z,sizeof(z)));}
    /* Pins: every entry by name, of the trade group, left to the family by the guard table. */
    assert(uitrade_pins() && uitrade_entry[UITRADE_H_TRADE_RULE]==&test_uitrade_tab[0]);
    test_uitrade_tab[3].covered=0;uitrade_pins_checked=0;assert(!uitrade_pins() && pin_failed_rva==test_uitrade_rva(3));test_uitrade_tab[3].covered=UIREP_FAMILY_TRADE;
    test_uitrade_tab[UITRADE_HOOKS].name="Nope";uitrade_pins_checked=0;assert(!uitrade_pins());test_uitrade_tab[UITRADE_HOOKS].name=uitrade_lua_names[0];
    region[test_uitrade_rva(5)+7]^=1;uitrade_pins_checked=0;assert(!uitrade_pins() && pin_failed_rva==test_uitrade_rva(5));region[test_uitrade_rva(5)+7]^=1;
    uitrade_pins_checked=0;assert(uitrade_pins());
    /* The first pass installs: every entry jumps to its stride. */
    stats.issued=1;pilot_stop=1000;
    assert(uitrade_pass(1) && uitrade_installed && uitrade_intact() && !strcmp(uitrade_stage,"live"));
    for(unsigned i=0;i<UITRADE_HOOKS;i++) assert(region[test_uitrade_rva(i)]==0xe9);
    tut_int_fn rule=(tut_int_fn)(void*)(region+test_uitrade_rva(UITRADE_H_TRADE_RULE));
    tut_float_fn factor=(tut_float_fn)(void*)(region+test_uitrade_rva(UITRADE_H_PRICE_FACTOR));
    tut_int_fn workforce=(tut_int_fn)(void*)(region+test_uitrade_rva(UITRADE_H_WORKFORCE));
    tut_int_fn limit=(tut_int_fn)(void*)(region+test_uitrade_rva(UITRADE_H_BUY_LIMIT));
    /* Outside the local phases X4's own: the stock function with its arguments. */
    test_uitrade_calls=0;rule(0x7003,7,(uint64_t)(uintptr_t)"buy",(uint64_t)(uintptr_t)"energycells",1);
    assert(test_uitrade_calls==1 && test_uitrade_args[0]==0x7003 && test_uitrade_args[1]==7 && !strcmp((const char*)(uintptr_t)test_uitrade_args[3],"energycells") && test_uitrade_args[4]==1);
    factor(0x7003,1.25f);assert(test_uitrade_calls==2 && test_uitrade_float==1.25f && uitrade_passed==2);
    /* Client 2's UI: one event each, nothing runs. */
    stats.slot=2;stats.issued=100;pstk_capture_start=50;pilot_delay=3;uirep_ui_depth=1;test_uitrade_calls=0;
    rule(0x7003,7,(uint64_t)(uintptr_t)"buy",(uint64_t)(uintptr_t)"energycells",1);
    assert(!test_uitrade_calls && uirep_queue_count==1 && uitrade_captured==1);
    const uirep_event *e=test_uitrade_last();
    assert(e->op==UITRADE_OP_FFI && e->length==1+UIREP_REF+4+4+12+1 && e->data[0]==UITRADE_H_TRADE_RULE && uitrade_length(e->data,e->length)==e->length);
    assert(e->data[1+UIREP_REF]==7 && e->data[1+UIREP_REF+4]==3 && !memcmp(e->data+1+UIREP_REF+5,"buy",3) && e->data[e->length-1]==1);
    rule(0x7003,7,(uint64_t)(uintptr_t)"sell",0,0);e=test_uitrade_last();
    assert(uirep_queue_count==2 && e->data[1+UIREP_REF+4+5]==0xff && uitrade_length(e->data,e->length)==e->length);   /* every ware */
    factor(0x7003,0.75f);e=test_uitrade_last();
    {float back=0;uint32_t bits=uitrade_get32(e->data+1+UIREP_REF);memcpy(&back,&bits,4);assert(uirep_queue_count==3 && back==0.75f && !test_uitrade_calls);}
    workforce(0x7004,0x101,0,0,0);e=test_uitrade_last();assert(uirep_queue_count==4 && e->data[1+UIREP_REF]==1);   /* a bool's low byte */
    limit(0x7003,(uint64_t)(uintptr_t)"energycells",(uint64_t)(uint32_t)-5,0,0);e=test_uitrade_last();
    assert(uirep_queue_count==5 && (int32_t)uitrade_get32(e->data+e->length-4)==-5);
    LONG64 refused=uirep_refused;
    rule(0x7003,7,0,0,1);assert(uirep_queue_count==5 && uirep_refused==refused+1 && uitrade_refused==1);   /* no rule type */
    rule(0x7999,7,(uint64_t)(uintptr_t)"buy",0,1);assert(uirep_queue_count==5 && uirep_refused==refused+2);  /* no component */
    {char text[512];assert(uirep_refusals_text(text,sizeof(text)) && strstr(text,"SetContainerTradeRule:string") && strstr(text,"SetContainerTradeRule:unmapped"));}
    /* The probe's own frame and an apply pass. */
    InterlockedExchange(&rid_probe_frame,1);workforce(0x7004,1,0,0,0);assert(test_uitrade_calls==1);InterlockedExchange(&rid_probe_frame,0);
    InterlockedExchange(&uirep_replaying,1);workforce(0x7004,1,0,0,0);assert(test_uitrade_calls==2 && uirep_queue_count==5);InterlockedExchange(&uirep_replaying,0);
    uirep_ui_depth=0;
    /* The wire takes them. */
    {unsigned char section[UIREP_SECTION_MAX];const char *why="";uint16_t c=0;unsigned length=uirep_take(100,UIREP_SECTION_MAX,section);
     assert(length && section[3]==5 && uirep_section_ok(2,section,length,&c,&why));}
    /* The apply, factions mode: X4's function sees slot 2's faction as the player faction, and it comes back. */
    unsigned char ev[UIREP_EVENT_MAX];unsigned n=0;char what[96];
    ev[n++]=UITRADE_H_TRADE_RULE;uirep_put_ref(ev+n,0x7003);n+=UIREP_REF;uitrade_put32(ev+n,9);n+=4;ev[n++]=3;memcpy(ev+n,"buy",3);n+=3;
    ev[n++]=11;memcpy(ev+n,"energycells",11);n+=11;ev[n++]=1;
    assert(uitrade_length(ev,n)==n);
    {uint64_t subject=0;assert(uitrade_subject(ev,n,&subject)==1 && subject==0x7003);}
    InterlockedExchange(&uirep_replaying,1);test_uitrade_calls=0;
    assert(uitrade_run(2,ev,n,what,sizeof(what))==1 && test_uitrade_calls==1 && test_uitrade_seen==test_uitrade_fac[1] && test_uitrade_now()==test_uitrade_fac[0]);
    assert(!strcmp(what,"SetContainerTradeRule scope") && test_uitrade_args[0]==0x7003 && test_uitrade_args[1]==9 && !strcmp((const char*)(uintptr_t)test_uitrade_args[2],"buy"));
    assert(!uitrade_scope_open && uitrade_scopes==1 && uitrade_runs==1);
    /* Slot 1 is the player faction: nothing changes. Coop (slot 2's ship the player's too): the same. */
    assert(uitrade_run(1,ev,n,what,sizeof(what))==1 && test_uitrade_seen==test_uitrade_fac[0] && !strcmp(what,"SetContainerTradeRule") && uitrade_scope_same==1);
    test_uitrade_own(0x7002,0);assert(uitrade_run(2,ev,n,what,sizeof(what))==1 && test_uitrade_seen==test_uitrade_fac[0] && uitrade_scope_same==2);test_uitrade_own(0x7002,1);
    /* A float and a NULL ware arrive as they were. */
    {unsigned char fe[1+UIREP_REF+4];float v=2.5f;uint32_t bits;memcpy(&bits,&v,4);fe[0]=UITRADE_H_PRICE_FACTOR;uirep_put_ref(fe+1,0x7003);uitrade_put32(fe+1+UIREP_REF,bits);
     assert(uitrade_run(2,fe,sizeof(fe),what,sizeof(what))==1 && test_uitrade_float==2.5f && test_uitrade_seen==test_uitrade_fac[1]);}
    {unsigned char ze[]={UITRADE_H_TRADE_RULE,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,1,'x',0xff,1};uirep_put_ref(ze+1,0x7003);
     assert(uitrade_run(2,ze,sizeof(ze),what,sizeof(what))==1 && !test_uitrade_args[3] && !strcmp((const char*)(uintptr_t)test_uitrade_args[2],"x"));}
    /* Gone, the same everywhere; identities that differ stop; no faction (no ship) runs nothing. */
    {unsigned char ge[1+UIREP_REF];ge[0]=UITRADE_H_PRODUCTION_OFFERS;uirep_put_ref(ge+1,0x7003);memset(ge+1,0,8);ge[1]=0x99;LONG64 calls=test_uitrade_calls;
     assert(!uitrade_run(2,ge,sizeof(ge),what,sizeof(what)) && strstr(what,"gone") && test_uitrade_calls==calls);
     uint64_t subject=0;assert(!uitrade_subject(ge,sizeof(ge),&subject));}
    test_uitrade_salt=1;assert(uitrade_run(2,ev,n,what,sizeof(what))==-1 && !strcmp(what,"ref_mismatch"));
    {uint64_t subject=0;assert(uitrade_subject(ev,n,&subject)==-1);}test_uitrade_salt=0;
    pilot_slots[1].runtime=0;assert(!uitrade_run(2,ev,n,what,sizeof(what)) && strstr(what,"no_faction"));pilot_slots[1].runtime=0x7002;
    assert(uitrade_run(1,ev,n-1,what,sizeof(what))==-1 && !strcmp(what,"decode"));
    /* Something changing g_PlayerFaction inside the call stops (and it is put back); a fault stops with the scope closed. */
    test_uitrade_change=1;assert(uitrade_run(2,ev,n,what,sizeof(what))==-1 && !strcmp(what,"scope") && test_uitrade_now()==test_uitrade_fac[0]);test_uitrade_change=0;
    test_uitrade_fault=1;assert(uitrade_run(2,ev,n,what,sizeof(what))==-1 && !strcmp(what,"fault") && !uitrade_scope_open && test_uitrade_now()==test_uitrade_fac[0]);
    test_uitrade_fault=0;
    /* The scope only inside an apply, on the frame thread, with no other scope open. */
    InterlockedExchange(&uirep_replaying,0);assert(uitrade_scope_enter(2)==-1);InterlockedExchange(&uirep_replaying,1);
    InterlockedExchange(&pres_open,1);assert(uitrade_scope_enter(2)==-1);InterlockedExchange(&pres_open,0);
    pstk_scope_slot=2;assert(uitrade_scope_enter(2)==-1);pstk_scope_slot=0;
    assert(uitrade_scope_enter(2)==1 && test_uitrade_now()==test_uitrade_fac[1] && uitrade_scope_enter(2)==-1 && uitrade_scope_leave() && test_uitrade_now()==test_uitrade_fac[0]);
    InterlockedExchange(&uirep_replaying,0);
    /* Balances as GetPlayerMoney reads them, per slot faction; the null account when a faction has none. */
    {char text[128];int64_t cents=0;
     assert(uitrade_balance(test_uitrade_fac[1],&cents)==1 && cents==12345 && !uitrade_balance(NULL,&cents));
     assert(uitrade_frame_text(text,sizeof(text)) && !strcmp(text,"UITRADE_1:500000,12345"));
     test_uitrade_own(0x7002,0);assert(uitrade_frame_text(text,sizeof(text)) && !strcmp(text,"UITRADE_1:500000,500000"));test_uitrade_own(0x7002,1);  /* coop */
     pilot_slots[1].runtime=0x7999;assert(uitrade_frame_text(text,sizeof(text)) && !strcmp(text,"UITRADE_1:500000,x"));pilot_slots[1].runtime=0x7002;
     test_uitrade_no_account=1;assert(uitrade_balance(test_uitrade_fac[1],&cents)==-1 && uitrade_frame_text(text,sizeof(text)) && !strcmp(text,"UITRADE_1:500000,n"));
     test_uitrade_no_account=0;   /* no account: n, never the dummy's (777) */
     assert(uitrade_status(text,sizeof(text)) && strstr(text,"ui_trade=live") && strstr(text,"hooks=17"));}
    /* The pass: a scope left open stops (and closes it), a lost patch stops, the window end takes every patch back. */
    InterlockedExchange(&uirep_replaying,1);assert(uitrade_scope_enter(2)==1);InterlockedExchange(&uirep_replaying,0);
    assert(!uitrade_pass(2) && !strcmp(uitrade_why_text,"ui_trade_scope_open") && !uitrade_scope_open && test_uitrade_now()==test_uitrade_fac[0]);
    uitrade_why_text="-";uitrade_stage="live";
    {unsigned char keep=region[test_uitrade_rva(4)+1];region[test_uitrade_rva(4)+1]^=0xff;
     assert(!uitrade_pass(3) && !strcmp(uitrade_why_text,"ui_trade_hook_lost"));region[test_uitrade_rva(4)+1]=keep;uitrade_why_text="-";uitrade_stage="live";}
    assert(uitrade_pass(4) && uitrade_intact());
    assert(uitrade_pass(1001) && !uitrade_installed && uitrade_restored && !strcmp(uitrade_stage,"stopped"));
    for(unsigned i=0;i<UITRADE_HOOKS;i++) assert(!memcmp(region+test_uitrade_rva(i),test_uitrade_tab[i].bytes,5));
    /* Without the family nothing of it runs. */
    {unsigned fam=uirep_families;uirep_families=UIREP_FAMILY_ORDERS;assert(!uitrade_wanted() && uitrade_pass(5));uirep_families=fam;}
    if(uitrade_page) {VirtualFree(uitrade_page,0,MEM_RELEASE);uitrade_page=NULL;}
    for(unsigned i=0;i<UITRADE_HOOKS;i++) uitrade_tramp[i]=NULL;
    uitrade_owner_fn=NULL;uitrade_account_fn=NULL;uitrade_pin_count=UITRADE_PIN_COUNT;uitrade_pins_checked=0;uitrade_stage="off";uitrade_why_text="-";
    uiguard_tab=saved_tab;uiguard_count=saved_tab_count;memcpy(pilot_slots,saved_pilot,sizeof(saved_pilot));
    test_uirep_reset();VirtualFree(region,0,MEM_RELEASE);
    pnat_base=saved_base;image=saved_image;uirep_fn=saved_fn;ui_thread=saved_ui;stats.issued=saved_issued;stats.slot=saved_slot;pilot_stop=saved_stop;
    pstk_capture_start=saved_start;pilot_delay=saved_delay;pilot_slot_count=saved_count;pstk_slot_mask=saved_mask;pstk_capturing=saved_capturing;
    pnat_armed=saved_armed;pnat_mode=saved_mode;pslot_armed=saved_slots;pres_armed=saved_present;
    printf("UI trade: hooks, capture, section, faction scope, balances and pass on stand-ins\n");
}
static void test_ui_trade_native_image(const char *path) {
    unsigned char *prior=image,*prior_base=pnat_base;const uiguard_entry *saved_tab=uiguard_tab;unsigned saved_count=uiguard_count;
    image=test_pmath_map(path);pnat_base=image;uiguard_tab=uiguard_table;uiguard_count=UIGUARD_ENTRIES;
    uitrade_pin_tab=uitrade_pin_table;uitrade_pin_count=UITRADE_PIN_COUNT;uitrade_pins_checked=0;
    assert(uitrade_pins());
    /* Every hooked entry is the export of its name, of the trade group, left to the family; the Lua globals are none. */
    unsigned dir=test_uiguard_export_dir(path);assert(dir);
    for(unsigned i=0;i<UITRADE_HOOKS;i++) {
        const uiguard_entry *e=uitrade_entry[i];
        assert(e && e->kind==UIGUARD_FFI && test_uiguard_export(image,dir,e->name)==e->rva && (e->covered&UIREP_FAMILY_TRADE) && e->group==UIGUARD_G_TRADE);
    }
    for(unsigned i=0;i<UITRADE_LUA_NAMES;i++) {const uiguard_entry *e=uitrade_lookup(uitrade_lua_names[i],UIGUARD_LUA);assert(e && (e->covered&UIREP_FAMILY_TRADE) && !test_uiguard_export(image,dir,e->name));}
    /* What the family does not take stays the guard's alone. */
    {unsigned covered=0;for(unsigned i=0;i<UIGUARD_ENTRIES;i++) if(uiguard_table[i].covered&UIREP_FAMILY_TRADE) {assert(uiguard_table[i].group==UIGUARD_G_TRADE);++covered;}
     assert(covered==UITRADE_HOOKS+UITRADE_LUA_NAMES);}
    /* The money walk's functions and the ship's owner slot, tampered: refused. */
    image[0x288080u+0x40]^=1;uitrade_pins_checked=0;assert(!uitrade_pins() && pin_failed_rva==0x288080u);image[0x288080u+0x40]^=1;
    {unsigned char *slot=image+UITRADE_SHIP_VTABLE+8u*UITRADE_OWNER_SLOT;slot[0]^=8;uitrade_pins_checked=0;assert(!uitrade_pins() && pin_failed_rva==UITRADE_SHIP_VTABLE);slot[0]^=8;}
    uitrade_pins_checked=0;assert(uitrade_pins());
    /* A whole install on the real bytes, every patch at its stub, and back. */
    for(unsigned i=0;i<UITRADE_HOOKS;i++) uitrade_tramp[i]=NULL;
    uitrade_installed=0;uitrade_page=NULL;
    assert(uitrade_install() && uitrade_intact());
    for(unsigned i=0;i<UITRADE_HOOKS;i++) {
        int32_t rel=0;memcpy(&rel,image+uitrade_entry[i]->rva+1,4);
        assert(image[uitrade_entry[i]->rva]==0xe9 && image+uitrade_entry[i]->rva+5+rel==uitrade_stride(i)+UITRADE_STUB);
        assert(!memcmp(uitrade_stride(i),uitrade_entry[i]->bytes,uitrade_entry[i]->prefix));
    }
    uitrade_uninstall();assert(!uitrade_installed && uitrade_restored);
    for(unsigned i=0;i<UITRADE_HOOKS;i++) assert(!memcmp(image+uitrade_entry[i]->rva,uitrade_entry[i]->bytes,uitrade_entry[i]->prefix));
    if(uitrade_page) {VirtualFree(uitrade_page,0,MEM_RELEASE);uitrade_page=NULL;}
    for(unsigned i=0;i<UITRADE_HOOKS;i++) uitrade_tramp[i]=NULL;
    uitrade_pins_checked=0;uiguard_tab=saved_tab;uiguard_count=saved_count;
    VirtualFree(image,0,MEM_RELEASE);image=prior;pnat_base=prior_base;
    printf("UI trade verified on the pinned image: %u hooks, %u Lua globals, %u pinned functions, the ship's owner slot\n",
        (unsigned)UITRADE_HOOKS,(unsigned)UITRADE_LUA_NAMES,(unsigned)UITRADE_PIN_COUNT);
}

/* UI guard (lockstep_ui_guard.h) on a private executable region with stand-in functions for every shape the table
 * holds (integer and float arguments, stack arguments, a check-only flag, a tail jump, a const char* and a struct
 * returned through the hidden pointer, a RIP-relative prefix, a conditional branch in the prefix, a Lua C function):
 * each patched for real, called through its entry outside and inside the local phases, in the probe's frame, during an
 * apply, from another thread; the refusal values, the counters, the log, covered and unselected entries left alone, the
 * pass (install, intact, lost patch, window end) and the restore. The generated table's invariants against the replay's
 * own hooks. The image test: every entry's pins and its export on the pinned X4.exe, a whole install and back. */
static void test_uiguard_sha(const void *data,unsigned length,char out[65]) {
    BCRYPT_ALG_HANDLE a=NULL;BCRYPT_HASH_HANDLE h=NULL;unsigned char raw[32];
    assert(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0 && BCryptCreateHash(a,&h,NULL,0,NULL,0,0)>=0);
    assert(BCryptHashData(h,(PUCHAR)data,length,0)>=0 && BCryptFinishHash(h,raw,32,0)>=0);
    BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);hex32(raw,out);
}
enum {TUG_INT,TUG_FLOAT,TUG_STACK,TUG_TAIL,TUG_STRING,TUG_STRUCT,TUG_RIP,TUG_JCC,TUG_LUA,TUG_COVERED,TUG_UNSELECTED,TUG_COUNT};
static uiguard_entry test_uiguard_entries[TUG_COUNT];
static char test_uiguard_shas[TUG_COUNT][65];
typedef uint64_t (*tug_int_fn)(uint64_t,uint64_t,uint64_t,uint64_t);
typedef double (*tug_float_fn)(double,double,double,double);
typedef uint64_t (*tug_stack_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef const char *(*tug_string_fn)(const char*);
typedef struct {const char *reason;uint64_t a,b;} tug_struct;
typedef tug_struct (*tug_struct_fn)(uint64_t);
typedef uint64_t (*tug_rip_fn)(void);
typedef int (*tug_jcc_fn)(int);
typedef int (*tug_lua_fn)(void*);
/* A Lua call's stack as lua_tolstring answers it: arguments 1..3, NULL for none or a value that is no string. */
typedef struct {const char *s[3];} tug_lua_stack;
static const char *test_uiguard_lua_string(void *L,int index,size_t *length) {
    const tug_lua_stack *k=(const tug_lua_stack*)L;const char *s=index>=1 && index<=3?k->s[index-1]:NULL;
    if(s && length) *length=strlen(s);
    return s;
}
static unsigned char *test_uiguard_region;
static DWORD WINAPI test_uiguard_thread(LPVOID unused) {
    (void)unused;return (DWORD)((tug_int_fn)(void*)(test_uiguard_region+0x1000))(1,2,3,4);
}
static void test_uiguard_entry(unsigned i,const char *name,unsigned rva,const unsigned char *code,unsigned length,unsigned prefix,unsigned char ret,
    unsigned char size,unsigned char strings,signed char checkonly,unsigned tail,unsigned short covered,unsigned char group) {
    uiguard_entry *e=&test_uiguard_entries[i];
    memcpy(test_uiguard_region+rva,code,length);
    memset(e,0,sizeof(*e));
    e->name=name;e->rva=rva;e->kind=i==TUG_LUA?UIGUARD_LUA:UIGUARD_FFI;e->group=group;e->covered=covered;e->ret=ret;e->size=size;e->strings=strings;
    e->checkonly=checkonly;e->prefix=(unsigned char)prefix;e->tail=tail;memcpy(e->bytes,code,prefix);e->pin=length;
    test_uiguard_sha(code,length,test_uiguard_shas[i]);e->sha=test_uiguard_shas[i];
}
static void test_ui_guard(void) {
    unsigned char *saved_base=pnat_base,*saved_image=image;DWORD saved_ui=ui_thread;uint64_t saved_issued=stats.issued,saved_stop=pilot_stop;
    int saved_rep_armed=uirep_armed;unsigned saved_families=uirep_families;
    unsigned char *region=VirtualAlloc(NULL,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(region);
    test_uiguard_region=region;pnat_base=image=region;ui_thread=GetCurrentThreadId();
    memset(region,0xcc,0x10000);
    static const unsigned char pro[5]={0x48,0x89,0x5c,0x24,0x08};   /* mov [rsp+8],rbx: the usual first bytes */
    {   /* a + b + c + d */
        unsigned char c[16];memcpy(c,pro,5);static const unsigned char body[]={0x48,0x8d,0x04,0x11,0x4c,0x01,0xc0,0x4c,0x01,0xc8,0xc3};memcpy(c+5,body,sizeof(body));
        test_uiguard_entry(TUG_INT,"IntFn",0x1000,c,5+sizeof(body),5,UIGUARD_RET_ZERO,0,0,-1,0,0,UIGUARD_G_FLIGHT);
    }
    {   /* xmm0 + xmm1 + xmm2 + xmm3 */
        unsigned char c[24];memcpy(c,pro,5);static const unsigned char body[]={0xf2,0x0f,0x58,0xc1,0xf2,0x0f,0x58,0xc2,0xf2,0x0f,0x58,0xc3,0xc3};memcpy(c+5,body,sizeof(body));
        test_uiguard_entry(TUG_FLOAT,"FloatFn",0x1100,c,5+sizeof(body),5,UIGUARD_RET_ZERO,0,0,-1,0,0,UIGUARD_G_TRADE);
    }
    {   /* the 5th + the 6th argument (the stack); the 6th is the check-only flag */
        unsigned char c[24];memcpy(c,pro,5);static const unsigned char body[]={0x48,0x8b,0x44,0x24,0x28,0x48,0x03,0x44,0x24,0x30,0xc3};memcpy(c+5,body,sizeof(body));
        test_uiguard_entry(TUG_STACK,"StackFn",0x1200,c,5+sizeof(body),5,UIGUARD_RET_ZERO,0,0,5,0,0,UIGUARD_G_ORDERS);
    }
    {   /* jmp IntFn */
        unsigned char c[5]={0xe9};int32_t rel=(int32_t)(0x1000-(0x1300+5));memcpy(c+1,&rel,4);
        test_uiguard_entry(TUG_TAIL,"TailFn",0x1300,c,5,5,UIGUARD_RET_ZERO,0,0,-1,0x1000,0,UIGUARD_G_ASSETS);
    }
    {   /* returns its argument */
        unsigned char c[12];memcpy(c,pro,5);static const unsigned char body[]={0x48,0x89,0xc8,0xc3};memcpy(c+5,body,sizeof(body));
        test_uiguard_entry(TUG_STRING,"StringFn",0x1400,c,5+sizeof(body),5,UIGUARD_RET_STRING,0,0,-1,0,0,UIGUARD_G_ASSETS);
    }
    {   /* out->reason = 7; return out (the hidden pointer) */
        unsigned char c[20];memcpy(c,pro,5);static const unsigned char body[]={0x48,0xc7,0x01,0x07,0x00,0x00,0x00,0x48,0x89,0xc8,0xc3};memcpy(c+5,body,sizeof(body));
        test_uiguard_entry(TUG_STRUCT,"StructFn",0x1500,c,5+sizeof(body),5,UIGUARD_RET_STRUCT,24,1,-1,0,0,UIGUARD_G_ASSETS);
    }
    {   /* mov rax,[rip+value]; ret: the copy reads the same value */
        uint64_t value=0x1122334455667788ULL;memcpy(region+0x8000,&value,8);
        unsigned char c[8]={0x48,0x8b,0x05};int32_t disp=(int32_t)(0x8000-(0x1600+7));memcpy(c+3,&disp,4);c[7]=0xc3;
        test_uiguard_entry(TUG_RIP,"RipFn",0x1600,c,8,7,UIGUARD_RET_ZERO,0,0,-1,0,0,UIGUARD_G_MISSIONS);
        test_uiguard_entries[TUG_RIP].rip[0]=3;
    }
    {   /* test ecx,ecx; jle +10; mov eax,1; ret; ...; mov eax,2; ret */
        static const unsigned char c[20]={0x85,0xc9,0x7e,0x0a,0xb8,0x01,0,0,0,0xc3,0xcc,0xcc,0xcc,0xcc,0xb8,0x02,0,0,0,0xc3};
        test_uiguard_entry(TUG_JCC,"JccFn",0x1700,c,sizeof(c),9,UIGUARD_RET_ZERO,0,0,-1,0,0,UIGUARD_G_EVENTS);
        test_uiguard_entries[TUG_JCC].jcc_at=2;test_uiguard_entries[TUG_JCC].jcc_len=2;test_uiguard_entries[TUG_JCC].jcc_target=0x1700+14;
    }
    {   /* a Lua C function: one result */
        unsigned char c[12];memcpy(c,pro,5);static const unsigned char body[]={0xb8,0x01,0,0,0,0xc3};memcpy(c+5,body,sizeof(body));
        test_uiguard_entry(TUG_LUA,"LuaFn",0x1800,c,5+sizeof(body),5,UIGUARD_RET_ZERO,0,0,-1,0,0,UIGUARD_G_CONVERSATION);
        test_uiguard_entry(TUG_COVERED,"CoveredFn",0x1900,c,5+sizeof(body),5,UIGUARD_RET_ZERO,0,0,-1,0,UIREP_FAMILY_DOCK,UIGUARD_G_FLIGHT);
        test_uiguard_entry(TUG_UNSELECTED,"EditorFn",0x1a00,c,5+sizeof(body),5,UIGUARD_RET_ZERO,0,0,-1,0,0,UIGUARD_G_EDITOR);
    }
    const uiguard_entry *saved_tab=uiguard_tab;unsigned saved_count=uiguard_count;
    uiguard_tab=test_uiguard_entries;uiguard_count=TUG_COUNT;uiguard_pins_checked=0;
    /* Pins: the stand-ins as pinned; a changed byte refuses. */
    assert(uiguard_pins());
    region[0x1100+7]^=1;uiguard_pins_checked=0;assert(!uiguard_pins() && pin_failed_rva==0x1100);region[0x1100+7]^=1;uiguard_pins_checked=0;assert(uiguard_pins());
    /* Arming: after the replay, groups known. */
    uiguard_armed=0;uirep_armed=0;assert(!uiguard_arm_using(UIGUARD_ALL_GROUPS) && !strcmp(uiguard_arm_why,"needs_ui_replay"));
    uirep_armed=1;assert(!uiguard_arm_using(0) && !uiguard_arm_using(UIGUARD_ALL_GROUPS+1) && !strcmp(uiguard_arm_why,"groups"));
    uirep_families=UIREP_FAMILY_DOCK;
    assert(uiguard_arm_using(UIGUARD_ALL_GROUPS&~(1u<<UIGUARD_G_EDITOR)) && uiguard_armed && !uiguard_arm_using(UIGUARD_ALL_GROUPS) && !strcmp(uiguard_arm_why,"rerun"));
    /* Install at the pass: every selected entry the replay does not take, nothing else. */
    stats.issued=50;pilot_stop=1000;uirep_stopped=0;uirep_replaying=0;uirep_ui_depth=0;uirep_dispatch_depth=0;rid_probe_frame=0;
    assert(uiguard_pass(50) && uiguard_installed && uiguard_live && !strcmp(uiguard_stage,"live"));
    for(unsigned i=0;i<TUG_COUNT;i++) assert((region[test_uiguard_entries[i].rva]==0xe9)==(i!=TUG_COVERED && i!=TUG_UNSELECTED) && uiguard_on[i]==(i!=TUG_COVERED && i!=TUG_UNSELECTED));
    assert(region[0x1300]==0xe9 && !memcmp(region+0x1301,uiguard_patched[TUG_TAIL]+1,4));   /* the tail jump's own bytes replaced */
    assert(uiguard_installed_count()==TUG_COUNT-2);
    /* Outside the local phases (simulation, MD): every function runs as X4's, arguments and results intact. */
    tug_int_fn int_fn=(tug_int_fn)(void*)(region+0x1000);tug_float_fn float_fn=(tug_float_fn)(void*)(region+0x1100);
    tug_stack_fn stack_fn=(tug_stack_fn)(void*)(region+0x1200);tug_int_fn tail_fn=(tug_int_fn)(void*)(region+0x1300);
    tug_string_fn string_fn=(tug_string_fn)(void*)(region+0x1400);tug_struct_fn struct_fn=(tug_struct_fn)(void*)(region+0x1500);
    tug_rip_fn rip_fn=(tug_rip_fn)(void*)(region+0x1600);tug_jcc_fn jcc_fn=(tug_jcc_fn)(void*)(region+0x1700);
    tug_lua_fn lua_fn=(tug_lua_fn)(void*)(region+0x1800),covered_fn=(tug_lua_fn)(void*)(region+0x1900),editor_fn=(tug_lua_fn)(void*)(region+0x1a00);
    static const char hello[]="hello";
    #define TUG_ALL_PASS() do { \
        assert(int_fn(1,2,3,40)==46 && float_fn(1.5,2.0,3.25,4.0)==10.75 && stack_fn(0,0,0,0,5,6)==11 && tail_fn(1,2,3,40)==46 && string_fn(hello)==hello); \
        {tug_struct s=struct_fn(0);assert((uint64_t)(uintptr_t)s.reason==7);} \
        assert(rip_fn()==0x1122334455667788ULL && jcc_fn(5)==1 && jcc_fn(-1)==2 && lua_fn(NULL)==1 && covered_fn(NULL)==1 && editor_fn(NULL)==1); } while(0)
    LONG64 passed=uiguard_passed;
    TUG_ALL_PASS();
    assert(uiguard_passed==passed+11 && !uiguard_refused);   /* the tail jump reaches IntFn's hook too, the jcc stand-in runs twice; covered and unselected are not hooked */
    /* Another thread is not the UI: it passes even inside a local phase of the UI thread. */
    uirep_ui_depth=1;
    {HANDLE t=CreateThread(NULL,0,test_uiguard_thread,NULL,0,NULL);DWORD code=0;assert(t && WaitForSingleObject(t,10000)==WAIT_OBJECT_0 && GetExitCodeThread(t,&code) && code==10);CloseHandle(t);}
    /* Inside a local phase (slot 6): refused, the refusal value, nothing ran. */
    stats.issued=60;
    assert(!int_fn(1,2,3,40) && float_fn(1.5,2.0,3.25,4.0)==0.0 && !stack_fn(0,0,0,0,5,0) && !tail_fn(1,2,3,40));
    {const char *s=string_fn(hello);assert(s && s!=hello && !*s);}
    {tug_struct s=struct_fn(0);assert(s.reason && !*s.reason && !s.a && !s.b);}
    assert(!rip_fn() && !jcc_fn(5) && !lua_fn(NULL));
    assert(covered_fn(NULL)==1 && editor_fn(NULL)==1);                                   /* not hooked */
    assert(stack_fn(0,0,0,0,5,1)==6 && uiguard_queries==1);                             /* the check-only flag: a query */
    assert(uiguard_refused==9 && uiguard_refused_by[TUG_INT]==1 && uiguard_refused_by[TUG_STACK]==1);
    {char text[4096];static const char first[]="60:IntFn:flight:refused:-,60:FloatFn:trade:refused:-,60:StackFn:orders:refused:-,60:TailFn:assets:refused:-,"
         "60:StringFn:assets:refused:-";
     assert(uiguard_refusals_text(text,sizeof(text)) && !strncmp(text,first,strlen(first)));
     assert(strstr(text,"60:LuaFn:conversation:refused:-") && !uiguard_refusals_text(text,sizeof(text)));
     assert(uiguard_status(text,sizeof(text)) && strstr(text,"ui_guard=live") && strstr(text,"installed=9") && strstr(text,"refused=9") && strstr(text,"queries=1") &&
            strstr(text,"by=IntFn:1,"));}
    /* The other local phases refuse alike; the probe's frame, an apply and a stopped replay pass. */
    uirep_ui_depth=0;
    InterlockedExchange(&uirep_dispatch_depth,1);assert(!int_fn(1,2,3,4));InterlockedExchange(&uirep_dispatch_depth,0);
    InterlockedExchange(&render_rng_in_slot,1);assert(!int_fn(1,2,3,4));InterlockedExchange(&render_rng_in_slot,0);
    InterlockedExchange(&pres_open,1);assert(!int_fn(1,2,3,4));
    InterlockedExchange(&rid_probe_frame,1);TUG_ALL_PASS();InterlockedExchange(&rid_probe_frame,0);
    InterlockedExchange(&uirep_replaying,1);TUG_ALL_PASS();InterlockedExchange(&uirep_replaying,0);
    uirep_stopped=1;TUG_ALL_PASS();uirep_stopped=0;
    assert(!int_fn(1,2,3,4));InterlockedExchange(&pres_open,0);
    assert(uiguard_refused==13 && uiguard_refused_by[TUG_INT]==5);
    /* A full log ring loses rows, never refusals. */
    {char text[8192];assert(uiguard_refusals_text(text,sizeof(text)) &&
                            !strcmp(text,"60:IntFn:flight:refused:-,60:IntFn:flight:refused:-,60:IntFn:flight:refused:-,60:IntFn:flight:refused:-"));
     uirep_ui_depth=1;for(unsigned i=0;i<UIGUARD_RING+3;i++) assert(!lua_fn(NULL));uirep_ui_depth=0;
     assert(uiguard_ring_lost==3 && uiguard_refusals_text(text,sizeof(text)) && uiguard_refused_by[TUG_LUA]==UIGUARD_RING+4 && !uiguard_refusals_text(text,sizeof(text)));}
    /* The pass: intact every tick, a lost patch stops. */
    assert(uiguard_pass(51) && uiguard_intact());
    region[0x1400+2]^=1;assert(!uiguard_pass(52) && !strcmp(uiguard_why_text,"ui_guard_hook_lost") && !strcmp(uiguard_stage,"failed"));region[0x1400+2]^=1;
    uiguard_why_text="-";uiguard_stage="live";assert(uiguard_pass(53));
    /* Through the replay: a guard failure is the replay's stop 164. */
    {unsigned char keep=region[0x1000+1];region[0x1000+1]^=1;uirep_failed=0;uirep_why="-";
     assert(!uirep_guard_pass(54) && uirep_failed && !strcmp(uirep_why,"ui_guard_hook_lost"));
     region[0x1000+1]=keep;uirep_failed=0;uirep_why="-";uiguard_why_text="-";uiguard_stage="live";}
    /* Behind the window everything is back and runs as X4's. */
    assert(uiguard_pass(1001) && !uiguard_installed && uiguard_restored && !uiguard_live && !strcmp(uiguard_stage,"stopped"));
    for(unsigned i=0;i<TUG_COUNT;i++) assert(!memcmp(region+test_uiguard_entries[i].rva,test_uiguard_entries[i].bytes,5));
    uirep_ui_depth=1;TUG_ALL_PASS();uirep_ui_depth=0;
    /* A restore off the pass (the gate's stop) too. */
    uiguard_installed=0;assert(uiguard_install() && region[0x1000]==0xe9);uiguard_restore();assert(!uiguard_installed && uiguard_restored && region[0x1000]==0x48);
    /* Named rows and the UI-only events: the two events entries alone (Lua C functions answering 1), their arguments read
     * as the stock function reads them (lua_tolstring of 1 and 2), here from a stand-in stack. */
    {   unsigned char c[12];memcpy(c,pro,5);static const unsigned char body[]={0xb8,0x01,0,0,0,0xc3};memcpy(c+5,body,sizeof(body));
        test_uiguard_entry(0,"AddUITriggeredEvent",0x1b00,c,5+sizeof(body),5,UIGUARD_RET_ZERO,0,0,-1,0,0,UIGUARD_G_EVENTS);
        test_uiguard_entry(1,"SignalObject",0x1c00,c,5+sizeof(body),5,UIGUARD_RET_ZERO,0,0,-1,0,0,UIGUARD_G_EVENTS);
        test_uiguard_entries[0].kind=test_uiguard_entries[1].kind=UIGUARD_LUA;
        uiguard_count=2;uiguard_pins_checked=0;assert(uiguard_pins());
        uiguard_armed=0;assert(uiguard_arm_using(UIGUARD_ALL_GROUPS) && uiguard_args_kind[0]==UIGUARD_ARGS_UI_EVENT && uiguard_args_kind[1]==UIGUARD_ARGS_SIGNAL);
        uiguard_lua_string=test_uiguard_lua_string;
        assert(uiguard_pass(60) && uiguard_installed_count()==2);
        tug_lua_fn ev_fn=(tug_lua_fn)(void*)(region+0x1b00),sig_fn=(tug_lua_fn)(void*)(region+0x1c00);
        tug_lua_stack top={{"TopLevelMenu","",NULL}},map={{"MapMenu","selection,ship:1",NULL}},sig={{NULL,"accept",NULL}},
            close={{"TopLevelMenu","menu_close",NULL}},screen_only={{"TopLevelMenu",NULL,NULL}};
        stats.issued=70;uirep_ui_depth=1;
        assert(ev_fn(&top)==1 && uiguard_allowed==1 && !uiguard_refused);          /* UI-only: X4's own, here */
        assert(!ev_fn(&map) && !sig_fn(&sig) && !ev_fn(&close) && !ev_fn(&screen_only) && uiguard_refused==4);
        uiguard_lua_string=NULL;assert(!ev_fn(&top) && uiguard_refused==5 && uiguard_allowed==1);   /* unreadable: refused, unnamed */
        uiguard_lua_string=test_uiguard_lua_string;uirep_ui_depth=0;
        assert(ev_fn(&map)==1 && sig_fn(&sig)==1 && uiguard_refused==5);           /* outside the local phases: X4's own */
        char text[1024];
        assert(uiguard_refusals_text(text,sizeof(text)) && !strcmp(text,"70:AddUITriggeredEvent:events:ui_only:TopLevelMenu/,"
            "70:AddUITriggeredEvent:events:refused:MapMenu/selection?ship?1,70:SignalObject:events:refused:accept,"
            "70:AddUITriggeredEvent:events:refused:TopLevelMenu/menu_close,70:AddUITriggeredEvent:events:refused:TopLevelMenu/,"
            "70:AddUITriggeredEvent:events:refused:-"));
        assert(uiguard_status(text,sizeof(text)) && strstr(text,"allowed=1 refused=5") && strstr(text,"by=AddUITriggeredEvent:4,SignalObject:1"));
        uiguard_restore();assert(!uiguard_installed && uiguard_restored && region[0x1b00]==0x48);
        uiguard_lua_string=NULL;uiguard_allowed=0;
    }
    /* The events family's op on the wire: slot 1 only, its family armed, a known global. */
    {unsigned f=uirep_families;uint16_t c16=0;const char *why="";unsigned char s[]={UIREP_VERSION,1,0,1,UIREP_OP_LUA_GLOBAL,2,1,0};
     uirep_families=UIREP_FAMILY_EVENTS;assert(uirep_section_ok(1,s,sizeof(s),&c16,&why));
     assert(!uirep_section_ok(2,s,sizeof(s),&c16,&why) && !strcmp(why,"ui_events"));
     s[6]=UIREP_LUA_GLOBALS+1;assert(!uirep_section_ok(1,s,sizeof(s),&c16,&why));s[6]=0;assert(!uirep_section_ok(1,s,sizeof(s),&c16,&why));s[6]=1;
     uirep_families=UIREP_FAMILY_ORDERS;assert(!uirep_section_ok(1,s,sizeof(s),&c16,&why) && !strcmp(why,"ui_events"));
     uirep_families=f;}
    /* The inventory family's op: its family armed, a known kind with arguments; slots 2..N only with their own scope
     * (missions per player). The bit is one of the implemented families and no other family's. */
    {unsigned f=uirep_families;int m=mpp_armed;uint16_t c16=0;const char *why="";unsigned char s[]={UIREP_VERSION,1,0,1,UIINV_OP_CLICK,2,1,0};
     assert((UIREP_FAMILIES&UIREP_FAMILY_INVENTORY) && !(UIREP_FAMILY_INVENTORY&0xffffu) && !strcmp(uirep_op_name(UIINV_OP_CLICK),"inventory_click"));
     uirep_families=UIREP_FAMILY_INVENTORY;mpp_armed=0;assert(uirep_section_ok(1,s,sizeof(s),&c16,&why));
     assert(!uirep_section_ok(2,s,sizeof(s),&c16,&why) && !strcmp(why,"ui_inventory"));
     mpp_armed=1;assert(uirep_section_ok(2,s,sizeof(s),&c16,&why));
     s[6]=UIINV_KINDS;assert(uirep_section_ok(2,s,sizeof(s),&c16,&why));
     s[6]=UIINV_KINDS+1;assert(!uirep_section_ok(1,s,sizeof(s),&c16,&why));s[6]=0;assert(!uirep_section_ok(1,s,sizeof(s),&c16,&why));s[6]=1;
     {unsigned char one[]={UIREP_VERSION,1,0,1,UIINV_OP_CLICK,1,1};assert(!uirep_section_ok(1,one,sizeof(one),&c16,&why) && !strcmp(why,"ui_inventory"));}
     uirep_families=UIREP_FAMILY_EVENTS;assert(!uirep_section_ok(1,s,sizeof(s),&c16,&why) && !strcmp(why,"ui_inventory"));
     uirep_families=f;mpp_armed=m;}
    /* The inventory menu's Drop (lockstep_ui_inventory.h): DropInventory's arguments in the Lua codec's bytes (count; nil or the
     * lockbox; ware, amount as a double), what does not fit an event refused, and the claim on the guard's entry. */
    {unsigned char out[UIREP_EVENT_MAX];const char *why="";uiinv_ware_amount w[UIINV_DROP_WARES+1];
     static const unsigned char want[]={UIINV_KIND_DROP,5,0, 4,5,'s','c','r','a','p', 3,0,0,0,0,0,0,0,0x40, 4,4,'c','h','i','p', 3,0,0,0,0,0,0,0xf0,0x3f};
     for(unsigned i=0;i<UIINV_DROP_WARES+1;i++) {w[i].ware="inv_a_ware_with_a_long_name";w[i].amount=1;}
     w[0].ware="scrap";w[0].amount=2;w[1].ware="chip";
     unsigned n=uiinv_drop_event(NULL,w,2,out,&why);assert(n==sizeof(want) && !memcmp(out,want,n));
     n=uiinv_drop_event("box",w,1,out,&why);assert(n==2+5+7+9 && out[1]==3 && out[2]==UIINV_TAG_STRING && out[3]==3 && !memcmp(out+4,"box",3));
     assert(!uiinv_drop_event(NULL,w,0,out,&why) && !strcmp(why,"wares") && !uiinv_drop_event(NULL,NULL,1,out,&why));
     assert(!uiinv_drop_event(NULL,w,UIINV_DROP_WARES+1,out,&why) && !strcmp(why,"wares"));
     assert(!uiinv_drop_event(NULL,w,UIINV_DROP_WARES,out,&why) && !strcmp(why,"too_long"));        /* more than an event holds */
     assert(!uiinv_drop_event("",w,1,out,&why) && !strcmp(why,"lockbox"));
     w[1].amount=0;assert(!uiinv_drop_event(NULL,w,2,out,&why) && !strcmp(why,"amount"));w[1].amount=1;
     w[1].ware=NULL;assert(!uiinv_drop_event(NULL,w,2,out,&why) && !strcmp(why,"ware"));
     w[1].ware="";assert(!uiinv_drop_event(NULL,w,2,out,&why) && !strcmp(why,"ware"));w[1].ware="chip";
     /* a kind 3 event passes the section rule */
     {unsigned f=uirep_families;uint16_t c16=0;unsigned char s[UIREP_HEAD+2+sizeof(want)]={UIREP_VERSION,1,0,1,UIINV_OP_CLICK,(unsigned char)sizeof(want)};
      memcpy(s+UIREP_HEAD+2,want,sizeof(want));uirep_families=UIREP_FAMILY_INVENTORY;assert(uirep_section_ok(1,s,sizeof(s),&c16,&why));uirep_families=f;}
     /* the arming: the family, the guard, the entry (free, then another family's) */
     /* (on the generated table: the stand-in table of this test has no DropInventory) */
     {unsigned f=uirep_families;int ra=uirep_armed,ga=uiguard_armed;unsigned drop=UINT_MAX;char text[160];
      const uiguard_entry *stand_tab=uiguard_tab;unsigned stand_count=uiguard_count;uiguard_tab=saved_tab;uiguard_count=saved_count;
      for(unsigned i=0;i<uiguard_count;i++) if(uiguard_tab[i].kind==UIGUARD_FFI && !strcmp(uiguard_tab[i].name,"DropInventory")) drop=i;
      assert(drop!=UINT_MAX);uiguard_unclaim(NULL);
      uirep_armed=1;uirep_families=UIREP_FAMILY_EVENTS;uiguard_armed=1;assert(!uiinv_arm() && !strcmp(uiinv_why,"needs_family"));
      uirep_families=UIREP_FAMILY_INVENTORY;uiguard_armed=0;assert(!uiinv_arm() && !strcmp(uiinv_why,"needs_ui_guard"));
      uiguard_armed=1;assert(uiinv_arm() && uiinv_armed && uiinv_drop_index==drop && uiguard_claimed[drop] && uiguard_claim_by[drop]==uiinv_claim_decide);
      assert(uiinv_arm() && uiguard_claim_by[drop]==uiinv_claim_decide);                              /* a rerun keeps it */
      assert(uiinv_status(text,sizeof(text))>0 && strstr(text,"ui_inventory=armed claims=7 drops=0/0"));
      {uint64_t ret=7,frame[12]={0};assert(!uiinv_claim_decide(drop+1,frame,&ret) && ret==0);}     /* not its entry */
      /* the recorder: with no scope open the claim declines (the guard's rule); in a scope a call is a row and does not run */
      {uint64_t ret=7,frame[12]={0};const unsigned money=uiinv_call_index[0],research=uiinv_call_index[2];int lost=1;
       assert(money!=UINT_MAX && research!=UINT_MAX && uiguard_claim_by[money]==uiinv_claim_decide && uiguard_claim_by[research]==uiinv_claim_decide);
       frame[4]=(uint64_t)-1250000ll;assert(!uiinv_claim_decide(money,frame,&ret));                 /* no scope */
       const DWORD t=ui_thread;const int cap=pstk_capturing;
       ui_thread=GetCurrentThreadId();pstk_capturing=0;assert(!uiinv_scope_begin());                /* not capturing */
       pstk_capturing=1;
       {const LONG live=InterlockedExchange(&pnat_live,0);assert(!uiinv_scope_begin());InterlockedExchange(&pnat_live,1);   /* not live; live */
        assert(uiinv_scope_begin() && uiinv_claim_decide(money,frame,&ret) && ret==0);
        frame[4]=(uint64_t)(uintptr_t)"research_x";frame[3]=4242;assert(uiinv_claim_decide(research,frame,&ret));
        assert(!strcmp(uiinv_scope_end(&lost),"AddPlayerMoney\t-1250000\nStartResearch\tresearch_x\t4242\n") && !lost && !uiinv_scope_open);
        assert(uiinv_scope_begin());frame[4]=(uint64_t)(uintptr_t)"bad\tname";assert(uiinv_claim_decide(uiinv_call_index[1],frame,&ret));
        uiinv_scope_end(&lost);assert(lost);                                                      /* a string the rows cannot carry */
        assert(uiinv_scope_begin());frame[4]=0;assert(uiinv_claim_decide(uiinv_call_index[1],frame,&ret));uiinv_scope_end(&lost);assert(lost);
        InterlockedExchange(&pnat_live,live);}
       /* the row writer itself, whatever the session state */
       uiinv_record_length=0;uiinv_record[0]=0;frame[4]=77;frame[3]=(uint64_t)(uintptr_t)"project_y";
       assert(uiinv_record_call(4,frame) && !strcmp(uiinv_record,"StartTerraformingProject\t77\tproject_y\n"));
       frame[4]=78;assert(uiinv_record_call(5,frame) && strstr(uiinv_record,"\nAbortActiveTerraformingProject\t78\n"));
       {unsigned rows=2;while(uiinv_record_call(5,frame)) ++rows;assert(rows>20 && uiinv_record_length<UIINV_RECORD_MAX);}   /* full: refused, never overrun */
       uiinv_record_length=0;uiinv_record[0]=0;ui_thread=t;pstk_capturing=cap;}
      uiguard_unclaim(NULL);assert(uiguard_claim(drop,pfoot_claim_decide) && !uiinv_arm() && !strcmp(uiinv_why,"ui_inventory_claim_taken:DropInventory") && !uiinv_armed);
      uiguard_unclaim(NULL);assert(uiguard_claim(uiinv_entry_of("StartResearch"),pfoot_claim_decide) && !uiinv_arm() &&
          !strcmp(uiinv_why,"ui_inventory_claim_taken:StartResearch") && !uiguard_claimed[drop]);     /* nothing stays claimed */
      uiguard_tab=stand_tab;uiguard_count=stand_count;assert(!uiinv_arm() && !strcmp(uiinv_why,"ui_inventory_guard_entry:DropInventory"));     /* a table without the entry */
      uiguard_unclaim(NULL);uiinv_armed=0;uiinv_drop_index=UINT_MAX;uirep_families=f;uirep_armed=ra;uiguard_armed=ga;}}
    /* The generated table: shapes the code relies on, and the entries the replay hooks itself are covered by that family. */
    for(unsigned i=0;i<UIGUARD_ENTRIES;i++) {
        const uiguard_entry *e=&uiguard_table[i];
        assert(e->group<UIGUARD_GROUPS && e->ret<=UIGUARD_RET_STRUCT && e->prefix>=5 && e->prefix<=16 && e->pin>=e->prefix && (e->rva&15u)<=11u);
        assert((e->ret==UIGUARD_RET_STRUCT)==(e->size>8) && e->checkonly<16 && (e->kind==UIGUARD_FFI || (e->ret==UIGUARD_RET_ZERO && e->checkonly<0)));
        for(unsigned r=0;r<2;r++) assert(!e->rip[r] || e->rip[r]+4u<=e->prefix);
        assert(!e->jcc_len || ((e->jcc_len==2 || e->jcc_len==6) && e->jcc_at+e->jcc_len<=e->prefix && e->jcc_target));
        assert(!e->tail || e->prefix==5);
        for(unsigned j=0;j<i;j++) assert(uiguard_table[j].rva!=e->rva && (strcmp(uiguard_table[j].name,e->name)!=0 || uiguard_table[j].kind!=e->kind));
        for(unsigned h=0;h<UIREP_HOOKS;h++) if(uirep_hooks[h].rva==e->rva) assert(uirep_hooks[h].family && (e->covered&uirep_hooks[h].family)==uirep_hooks[h].family);
    }
    /* Everything off. */
    uiguard_tab=saved_tab;uiguard_count=saved_count;uiguard_pins_checked=0;uiguard_armed=0;uiguard_groups=0;uiguard_stage="off";uiguard_why_text="-";uirep_stage_name="off";
    if(uiguard_region) {VirtualFree(uiguard_region,0,MEM_RELEASE);uiguard_region=NULL;}
    VirtualFree(region,0,MEM_RELEASE);
    pnat_base=saved_base;image=saved_image;ui_thread=saved_ui;stats.issued=saved_issued;pilot_stop=saved_stop;uirep_armed=saved_rep_armed;uirep_families=saved_families;
    uiguard_passed=uiguard_queries=uiguard_refused=uiguard_ring_lost=0;memset((void*)uiguard_refused_by,0,sizeof(uiguard_refused_by));
    printf("UI guard on stand-ins and an executable region verified\n");
}
/* The export of a name on an image mapped by section (test_pmath_map: no headers), 0 when none; the export
 * directory's RVA from the file's own header. */
static unsigned test_uiguard_export_dir(const char *path) {
    FILE *file=fopen(path,"rb");IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;assert(file);
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && !fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt));
    fclose(file);return nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
}
static unsigned test_uiguard_export(const unsigned char *img,unsigned dir,const char *name) {
    const IMAGE_EXPORT_DIRECTORY *ex=(const IMAGE_EXPORT_DIRECTORY*)(img+dir);
    const DWORD *names=(const DWORD*)(img+ex->AddressOfNames),*functions=(const DWORD*)(img+ex->AddressOfFunctions);const WORD *ordinals=(const WORD*)(img+ex->AddressOfNameOrdinals);
    for(DWORD i=0;i<ex->NumberOfNames;i++) if(!strcmp((const char*)(img+names[i]),name)) return functions[ordinals[i]];
    return 0;
}
static void test_ui_guard_native_image(const char *path) {
    unsigned char *prior=image,*prior_base=pnat_base;unsigned saved_families=uirep_families;int saved_armed=uirep_armed;
    image=test_pmath_map(path);pnat_base=image;
    uiguard_pins_checked=0;assert(uiguard_pins());
    /* Every FFI entry is the export of its name; a Lua entry is no export. */
    unsigned ffi=0,dir=test_uiguard_export_dir(path);assert(dir);
    for(unsigned i=0;i<UIGUARD_ENTRIES;i++) {
        const uiguard_entry *e=&uiguard_table[i];
        if(e->kind==UIGUARD_FFI) {assert(test_uiguard_export(image,dir,e->name)==e->rva);++ffi;}
        if(e->tail) assert(image[e->rva]==0xe9);
        if(e->jcc_len==2) assert((image[e->rva+e->jcc_at]&0xf0)==0x70 && e->rva+e->jcc_at+2u+(int8_t)image[e->rva+e->jcc_at+1]==e->jcc_target);
    }
    assert(ffi>200);
    /* A whole install on the real bytes (no family armed: every entry), every patch at its stub, and back. */
    uirep_families=0;uiguard_groups=UIGUARD_ALL_GROUPS;uiguard_installed=0;uiguard_region=NULL;memset(uiguard_on,0,sizeof(uiguard_on));
    assert(uiguard_install() && uiguard_intact() && uiguard_installed_count()==UIGUARD_ENTRIES);
    for(unsigned i=0;i<UIGUARD_ENTRIES;i++) {
        int32_t rel=0;memcpy(&rel,image+uiguard_table[i].rva+1,4);
        assert(image[uiguard_table[i].rva]==0xe9 && image+uiguard_table[i].rva+5+rel==uiguard_stride(i)+UIGUARD_STUB && uiguard_stride(i)[UIGUARD_STUB]==0x41);
    }
    uiguard_uninstall();assert(!uiguard_installed && uiguard_restored);
    for(unsigned i=0;i<UIGUARD_ENTRIES;i++) assert(!memcmp(image+uiguard_table[i].rva,uiguard_table[i].bytes,uiguard_table[i].prefix));
    /* With the replay's families armed, what they hook is left to them. */
    uirep_families=UIREP_FAMILIES;assert(uiguard_install());
    for(unsigned h=0;h<UIREP_HOOKS;h++) for(unsigned i=0;i<UIGUARD_ENTRIES;i++) if(uiguard_table[i].rva==uirep_hooks[h].rva) assert(!uiguard_on[i]);
    uiguard_uninstall();assert(uiguard_restored);
    {unsigned k=0;while(uiguard_table[k].pin<=uiguard_table[k].prefix) ++k;   /* a byte behind the prefix, inside the pin */
     image[uiguard_table[k].rva+uiguard_table[k].prefix]^=1;uiguard_pins_checked=0;assert(!uiguard_pins() && pin_failed_rva==uiguard_table[k].rva);
     image[uiguard_table[k].rva+uiguard_table[k].prefix]^=1;uiguard_pins_checked=0;assert(uiguard_pins());}
    if(uiguard_region) {VirtualFree(uiguard_region,0,MEM_RELEASE);uiguard_region=NULL;}
    uiguard_groups=0;uirep_families=saved_families;uirep_armed=saved_armed;
    VirtualFree(image,0,MEM_RELEASE);
    image=prior;pnat_base=prior_base;
    printf("UI guard verified on the pinned image: %u entries, pins, exports, patches\n",(unsigned)UIGUARD_ENTRIES);
}

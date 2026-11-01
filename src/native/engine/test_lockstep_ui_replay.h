/* UI replay (lockstep_ui_replay.h) on stubs and on a private executable region: arming and its refusals, capture
 * in and outside the local phases, the queue and its budget, the section's validation, the delay rule, travel for
 * slot k through the adapter (availability, the last ship's ID, start and stop) and for slot 1 through X4's
 * functions, the soft target's two blocks with the stale, mismatch and hint rules, the order hooks and guards, the
 * Lua-run list, the receipt being equal on two clients, every stride executed for real, the restore, and the
 * stop reason. The image test checks every pinned function, the prefixes and the vtable slots relied on. */
typedef struct {char what;void *a;uint64_t b,c;unsigned char scope;} test_uirep_call;
static test_uirep_call test_uirep_calls[128];
static unsigned test_uirep_call_count;
static int test_uirep_available,test_uirep_drive_on,test_uirep_enter_ok=1,test_uirep_leave_ok=1,test_uirep_fault,test_uirep_events_back;
static unsigned char test_uirep_drive[0x400];
static uint64_t test_uirep_portable_salt;
static __attribute__((aligned(16))) unsigned char test_uirep_player[0x7000],test_uirep_ship[0x400],test_uirep_target[0x100],test_uirep_element[0x100];
static unsigned char test_uirep_offset_seen[UIREP_OFFSET];
static void test_uirep_log(char what,void *a,uint64_t b,uint64_t c) {
    assert(test_uirep_call_count<128);
    test_uirep_calls[test_uirep_call_count++]=(test_uirep_call){what,a,b,c,(unsigned char)pstk_scope_slot};
}
static unsigned test_uirep_count(char what) {unsigned n=0;for(unsigned i=0;i<test_uirep_call_count;i++) n+=test_uirep_calls[i].what==what;return n;}
static void test_uirep_ui(void *self) {test_uirep_log('U',self,(uint64_t)uirep_ui_depth,0);}
static void test_uirep_start(void *a,int activity,unsigned char flag) {test_uirep_log('S',a,(uint64_t)activity,flag);}
static void test_uirep_stop(void *a,int activity,unsigned char flag) {test_uirep_log('E',a,(uint64_t)activity,flag);}
static void test_uirep_set_activity(void *player,int activity) {
    if(test_uirep_fault) {volatile int *nothing=NULL;*nothing=1;}
    test_uirep_log('A',player,(uint64_t)activity,0);
    /* As X4's: the teardown's events reach U::Player while the activity still reads the old one, its answer lands here. */
    if(test_uirep_events_back) {
        int old=0;memcpy(&old,(unsigned char*)player+UIREP_PLAYER_ACTIVITY,4);
        uirep_activity_stop(NULL,old,1);uirep_activity_start(NULL,activity,0);
    }
    memcpy((unsigned char*)player+UIREP_PLAYER_ACTIVITY,&activity,4);
}
static unsigned char test_uirep_can_travel(void *ship) {assert(ship==test_uirep_ship);return (unsigned char)test_uirep_available;}
static void **test_uirep_drive_of(void *ship,void **out) {
    assert(ship==test_uirep_ship);*out=test_uirep_drive;test_uirep_drive[UIREP_DRIVE_ACTIVE]=(unsigned char)test_uirep_drive_on;return out;
}
static void test_uirep_target_set(void *association,const void *offset,unsigned char flag) {
    test_uirep_log('T',association,flag,0);memcpy(test_uirep_offset_seen,offset,UIREP_OFFSET);uirep_set_hint(0xdead);
}
static void test_uirep_target_clear(void *association) {test_uirep_log('C',association,0,0);uirep_set_hint(0xbeef);}
static void *test_uirep_find(void *map,uint64_t id,unsigned flags) {
    assert(flags==4 && map==(void*)test_uirep_drive);
    if(id==0x7001) return test_uirep_target;
    return NULL;
}
static unsigned char test_uirep_p1[0x7000],*test_uirep_universe;
static int test_uirep_enter(unsigned k) {
    test_uirep_log('{',NULL,k,0);
    if(!test_uirep_enter_ok) return 0;
    if(test_uirep_universe) {void *p=pslots[k].player;memcpy(test_uirep_universe+VPS_SINGLETON,&p,8);}
    pstk_scope_slot=k;return 1;
}
static int test_uirep_leave(unsigned k) {
    if(test_uirep_universe) {void *p=test_uirep_p1;memcpy(test_uirep_universe+VPS_SINGLETON,&p,8);}
    pstk_scope_slot=0;test_uirep_log('}',NULL,k,0);return test_uirep_leave_ok;
}
static uint64_t test_uirep_portable(uint64_t runtime) {return runtime==0x7001?0x123456^test_uirep_portable_salt:0;}
static uint64_t test_uirep_forward(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) {
    test_uirep_log('F',(void*)(uintptr_t)a,b,c);assert(d==4 && e==5 && f==6);(void)g;(void)h;return 77;
}
/* The generic calls: what reaches the trampoline, with the singleton of the moment. */
static uint64_t test_uirep_call_args[UIREP_ARGS];
/* The queue editor: the queue's length and X4's answer. */
static uint32_t test_uirep_orders;static uint64_t test_uirep_queue_answer;static int test_uirep_queue_strings;
static char test_uirep_queue_string[UIREP_STRING_MAX+1];
static uint32_t test_uirep_num_orders(uint64_t ctl) {assert(ctl==0x7001);return test_uirep_orders;}
static uint64_t test_uirep_queue_fwd(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) {
    uint64_t v[UIREP_ARGS]={a,b,c,d,e,f,g,h};memcpy(test_uirep_call_args,v,sizeof(v));
    if(test_uirep_queue_strings) snprintf(test_uirep_queue_string,sizeof(test_uirep_queue_string),"%s",(const char*)(uintptr_t)b);
    test_uirep_log('Q',(void*)(uintptr_t)a,b,c);return test_uirep_queue_answer;
}
static unsigned char test_uirep_call_vector[16];
static char test_uirep_call_string[UIREP_STRING_MAX+1];
static uint64_t test_uirep_call_fwd(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) {
    uint64_t v[UIREP_ARGS]={a,b,c,d,e,f,g,h};memcpy(test_uirep_call_args,v,sizeof(v));
    test_uirep_log('K',pnat_player(),a,g);return 55+(g&0xff)+(e&0xff)*2;
}
static uint64_t test_uirep_call_vec_fwd(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) {
    if(c) memcpy(test_uirep_call_vector,(const void*)(uintptr_t)c,16);
    return test_uirep_call_fwd(a,b,c,d,e,f,g,h);
}
static uint64_t test_uirep_call_str_fwd(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) {
    if(b) snprintf(test_uirep_call_string,sizeof(test_uirep_call_string),"%s",(const char*)(uintptr_t)b);
    return test_uirep_call_fwd(a,b,c,d,e,f,g,h);
}
/* The soft target's route outside the local phases: the image return addresses above 0xaecb60, as the walk would find them. */
static uint32_t test_uirep_route[UIREP_ROUTE_DEPTH];
static unsigned test_uirep_route_count,test_uirep_route_site;
static unsigned test_uirep_route_of(unsigned site,uint32_t *frames,unsigned capacity) {
    unsigned n=test_uirep_route_count<capacity?test_uirep_route_count:capacity;
    test_uirep_route_site=site;memcpy(frames,test_uirep_route,n*sizeof(*frames));return n;
}
static void test_uirep_route_set(unsigned n,const uint32_t *frames) {
    assert(n<=UIREP_ROUTE_DEPTH);test_uirep_route_count=n;memcpy(test_uirep_route,frames,n*sizeof(*frames));uirep_route_override=test_uirep_route_of;
}
static DWORD WINAPI test_uirep_off_thread(LPVOID unused) {(void)unused;uirep_target_clear_stub(test_uirep_target);return 0;}
static void test_uirep_reset(void) {
    uirep_target_ui=uirep_target_ui_refused=uirep_target_md=uirep_route_faults=0;uirep_route_last_count=0;uirep_route_override=NULL;uirep_route_reached=0;
    test_uirep_call_count=0;test_uirep_available=1;test_uirep_drive_on=0;test_uirep_enter_ok=test_uirep_leave_ok=1;test_uirep_portable_salt=0;
    uirep_armed=0;uirep_families=0;uirep_failed=0;uirep_stopped=0;uirep_installed=0;uirep_restored=1;uirep_why="-";uirep_stage_name="off";
    uirep_queue_head=uirep_queue_count=0;uirep_refusal_head=uirep_refusal_tail=0;memset(uirep_ring,0,sizeof(uirep_ring));
    uirep_frame_tick=uirep_results_tick=uirep_frame_hash=uirep_results_hash=0;uirep_frame_events=0;uirep_ui_depth=0;uirep_dispatch_depth=0;uirep_replaying=0;
    memset(test_uirep_player,0,sizeof(test_uirep_player));memset(test_uirep_ship,0,sizeof(test_uirep_ship));
    pnat_failed=0;pnat_fail_stage=NULL;pstk_scope_slot=0;
    uirep_captured=uirep_refused=uirep_taken=uirep_staged_count=uirep_applied_native=uirep_applied_lua=uirep_stale=uirep_passed=0;
    uirep_scope_start=uirep_scope_stop=uirep_scope_refused=uirep_scope_nested=uirep_unavailable=uirep_guard_refused=uirep_target_passed=uirep_lua_errors=uirep_waits=0;
}
/* A stride reached as its patch reaches it: inside X4's frame (rsp 16-aligned, shadow space there), by a jump. */
extern void test_uirep_site_back(void);
__attribute__((naked)) static void test_uirep_run_site(__attribute__((unused)) void *stride,__attribute__((unused)) void *rbx_value,
    __attribute__((unused)) void *r13_value,__attribute__((unused)) void *rax_value) {
    __asm__ volatile("pushq %rbx\n\tpushq %r13\n\tsubq $0x28, %rsp\n\tmovq %rdx, %rbx\n\tmovq %r8, %r13\n\tmovq %r9, %rax\n\tjmpq *%rcx\n\t"
        ".globl test_uirep_site_back\n\ttest_uirep_site_back:\n\tmovq %rbx, test_uirep_rbx_after(%rip)\n\tmovq %r13, test_uirep_r13_after(%rip)\n\t"
        "addq $0x28, %rsp\n\tpopq %r13\n\tpopq %rbx\n\tretq\n\t");
}
__attribute__((used)) static void *test_uirep_rbx_after,*test_uirep_r13_after;
static void test_ui_replay_section(void) {
    /* Sections as the wire carries them. */
    unsigned char s[64]={UIREP_VERSION,0x64,0,1,UIREP_OP_ACTIVITY_START,2,1,0};uint16_t c=0;const char *why="";
    assert(uirep_section_ok(2,s,8,&c,&why) && c==0x64);
    assert(!uirep_section_ok(2,s,7,&c,&why) && !strcmp(why,"ui_section"));              /* truncated */
    assert(!uirep_section_ok(2,s,9,&c,&why) && !strcmp(why,"ui_section"));              /* trailing */
    s[0]=2;assert(!uirep_section_ok(2,s,8,&c,&why));s[0]=UIREP_VERSION;
    s[3]=0;assert(!uirep_section_ok(2,s,8,&c,&why));s[3]=1;
    s[4]=9;assert(!uirep_section_ok(2,s,8,&c,&why) && !strcmp(why,"ui_op"));s[4]=UIREP_OP_ACTIVITY_START;
    s[6]=4;assert(!uirep_section_ok(2,s,8,&c,&why) && !strcmp(why,"ui_activity"));      /* SETA on slot 2 */
    assert(uirep_section_ok(1,s,8,&c,&why));                                            /* slot 1: MD decides as ever */
    s[6]=1;s[7]=2;assert(!uirep_section_ok(2,s,8,&c,&why) && !strcmp(why,"ui_activity"));s[7]=0;
    {unsigned f=uirep_families;uirep_families=UIREP_FAMILY_ORDERS;assert(!uirep_section_ok(2,s,8,&c,&why));uirep_families=f;}
    unsigned char t[UIREP_HEAD+2+UIREP_REF]={UIREP_VERSION,1,0,1,UIREP_OP_TARGET_CLEAR,UIREP_REF};
    assert(uirep_section_ok(2,t,sizeof(t),&c,&why));
    t[5]=UIREP_REF-1;assert(!uirep_section_ok(2,t,sizeof(t)-1,&c,&why) && !strcmp(why,"ui_target"));
    assert(!uirep_section_ok(2,s,UIREP_SECTION_MAX+1,&c,&why));
}
static void test_ui_replay(void) {
    unsigned char *saved_base=pnat_base;uirep_fns saved_fn=uirep_fn;DWORD saved_ui=ui_thread;uint64_t saved_issued=stats.issued;unsigned saved_slot=stats.slot;
    uint64_t saved_stop=pilot_stop,saved_start=pstk_capture_start;unsigned saved_delay=pilot_delay,saved_count=pilot_slot_count,saved_mask=pstk_slot_mask;
    int saved_capturing=pstk_capturing,saved_armed=pnat_armed,saved_mode=pnat_mode,saved_slots=pslot_armed,saved_present=pres_armed,saved_rid=rid_mode;
    pslot_slot saved_pslot=pslots[2];
    unsigned char *region=VirtualAlloc(NULL,TEST_PNAT_REGION,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(region);
    pnat_base=region;ui_thread=GetCurrentThreadId();test_uirep_reset();
    {void *map=test_uirep_drive;memcpy(region+UIREP_MAP,&map,8);}
    uirep_fn=(uirep_fns){.ui_move=test_uirep_ui,.start=test_uirep_start,.stop=test_uirep_stop,.set_activity=test_uirep_set_activity,.can_travel=test_uirep_can_travel,
        .drive=test_uirep_drive_of,.target_set=test_uirep_target_set,.target_clear=test_uirep_target_clear,.find=test_uirep_find,.enter=test_uirep_enter,.leave=test_uirep_leave,
        .portable=test_uirep_portable};
    for(unsigned i=UIREP_H_CLEAR;i<UIREP_HOOKS;i++) uirep_fn.forward[i]=test_uirep_forward;
    /* Arming: what it needs, one refusal at a time. */
    pnat_armed=0;pnat_mode=0;pstk_capturing=0;pilot_slot_count=2;pstk_slot_mask=3;pslot_armed=0;pres_armed=0;rid_mode=RID_OBSERVE;
    assert(!uirep_arm_using(7) && !strcmp(uirep_arm_why,"needs_stock"));
    pnat_armed=1;pnat_mode=PNAT_MODE_STOCK;pstk_capturing=1;
    assert(!uirep_arm_using(7) && !strcmp(uirep_arm_why,"needs_all_stock_slots"));
    pslot_armed=1;assert(!uirep_arm_using(7) && !strcmp(uirep_arm_why,"needs_present"));
    pres_armed=1;assert(!uirep_arm_using(0) && !uirep_arm_using(UIREP_FAMILIES+1) && !strcmp(uirep_arm_why,"families"));
    pstk_slot_mask=1;assert(!uirep_arm_using(7) && !strcmp(uirep_arm_why,"needs_all_stock_slots"));pstk_slot_mask=3;
    assert(uirep_arm_using(7) && uirep_armed && uirep_arm_using(7) && !uirep_arm_using(3) && !strcmp(uirep_arm_why,"rerun"));
    test_ui_replay_section();
    /* The verdicts the families open. */
    assert(pstk_verdict_slot(PSTK_ACTION,0x12f,2)==PSTK_REPLAY && pstk_verdict_slot(PSTK_ACTION,0x131,2)==PSTK_REFUSE && pstk_verdict_slot(PSTK_ACTION,0xe1,2)==PSTK_REFUSE);
    assert(pstk_verdict(PSTK_STATE,0x5b)==PSTK_REPLAY && pstk_verdict(PSTK_STATE,0x18)==PSTK_REFUSE);
    {unsigned f=uirep_families;uirep_families=0;assert(pstk_verdict_slot(PSTK_ACTION,0x12f,2)==PSTK_REFUSE && pstk_verdict(PSTK_STATE,0x5b)==PSTK_REFUSE);uirep_families=f;}
    /* Capture on client 2. Simulation code passes through, the UI is captured and never applied here. */
    stats.slot=2;stats.issued=100;pstk_capture_start=50;pilot_stop=1000;pilot_delay=3;
    uirep_activity_start((void*)1,1,0);assert(test_uirep_count('S')==1 && uirep_passed==1 && !uirep_queue_count);
    uirep_ui_depth=1;uirep_activity_start(NULL,1,0);assert(test_uirep_count('S')==1 && uirep_queue_count==1 && uirep_captured==1);uirep_ui_depth=0;
    uirep_dispatch_depth=1;uirep_activity_stop(NULL,1,1);assert(test_uirep_count('E')==0 && uirep_queue_count==2);uirep_dispatch_depth=0;
    InterlockedExchange(&render_rng_in_slot,1);uirep_activity_stop(NULL,1,1);assert(uirep_queue_count==3);InterlockedExchange(&render_rng_in_slot,0);
    InterlockedExchange(&pres_open,1);uirep_activity_start(NULL,4,0);assert(uirep_queue_count==3 && uirep_refused==1);InterlockedExchange(&pres_open,0); /* SETA on slot 2 */
    {char text[256];assert(uirep_refusals_text(text,sizeof(text)) && !strcmp(text,"100:activity_start:activity"));}
    /* The UI's Move is a local phase for its whole length. */
    test_uirep_ui(NULL);test_uirep_call_count=0;uirep_ui_move((void*)5);assert(test_uirep_count('U')==1 && test_uirep_calls[0].b==1 && !uirep_ui_depth);
    /* Outside the window: refused, nothing runs. */
    test_uirep_call_count=0;stats.issued=20;uirep_ui_depth=1;uirep_activity_start(NULL,1,0);assert(!test_uirep_call_count && uirep_queue_count==3 && uirep_refused==2);
    stats.issued=1001;uirep_activity_start(NULL,1,0);assert(uirep_queue_count==3 && uirep_refused==3);uirep_ui_depth=0;stats.issued=100;
    /* The queue: budget, order, the rest kept. */
    unsigned char section[UIREP_SECTION_MAX];
    assert(!uirep_take(100,UIREP_HEAD+3,section) && uirep_waits==1 && uirep_queue_count==3);
    unsigned length=uirep_take(100,UIREP_HEAD+4+4,section);
    assert(length==UIREP_HEAD+8 && section[3]==2 && section[1]==100 && uirep_queue_count==1 && uirep_taken==2);
    assert(section[4]==UIREP_OP_ACTIVITY_START && section[8]==UIREP_OP_ACTIVITY_STOP && section[10]==1 && section[11]==1);
    /* Stage: the one apply tick, ahead of every pass; identical again is fine, different is not. */
    const char *why="";
    assert(!uirep_stage(3,section,length,101,&why) && !strcmp(why,"ui_slot"));
    assert(uirep_stage(2,section,length,101,&why) && uirep_stage(2,section,length,101,&why) && uirep_staged_count==1);
    section[11]=0;assert(!uirep_stage(2,section,length,101,&why) && !strcmp(why,"ui_conflict"));section[11]=1;
    assert(!uirep_stage(2,section,length,103,&why) && !strcmp(why,"ui_late"));                   /* apply tick == now */
    {unsigned char early[UIREP_SECTION_MAX];memcpy(early,section,length);early[1]=40;assert(!uirep_stage(2,early,length,41,&why) && !strcmp(why,"ui_late"));} /* before the capture window */
    /* Slot 2's seat for the apply. */
    {uint64_t id=0x5001;memcpy(test_uirep_ship+UIREP_ID,&id,8);}
    memset(&pslots[2],0,sizeof(pslots[2]));pslots[2].player=test_uirep_player;pslots[2].ship=test_uirep_ship;
    /* Apply at 103: start then stop, in slot 2's scope, through the setter; the capturing client did nothing before. */
    test_uirep_call_count=0;stats.issued=103;
    assert(uirep_frame(102,&why) && !test_uirep_call_count && !uirep_frame_events);  /* nothing due */
    assert(uirep_frame(103,&why) && uirep_frame_events==2 && uirep_applied_native==2 && !uirep_failed);
    assert(test_uirep_call_count==4 && test_uirep_calls[0].what=='{' && test_uirep_calls[0].b==2 && test_uirep_calls[3].what=='}');
    assert(test_uirep_calls[1].what=='A' && test_uirep_calls[1].a==test_uirep_player && test_uirep_calls[1].b==1 && test_uirep_calls[1].scope==2);
    assert(test_uirep_calls[2].what=='A' && test_uirep_calls[2].b==0);
    {uint64_t last=0;memcpy(&last,test_uirep_player+UIREP_PLAYER_LAST_SHIP,8);assert(last==0x5001);}   /* 0x710ed0 stops travel on this ID */
    assert(!uirep_scope_refused && uirep_scope_start==1 && uirep_scope_stop==1);
    /* The two wrappers inside a slot scope (the J key's record, the simulation's own stops): the adapter. */
    test_uirep_call_count=0;pstk_scope_slot=2;
    test_uirep_available=0;uirep_activity_start(NULL,1,0);assert(!test_uirep_call_count && uirep_unavailable==1);   /* MD's check */
    test_uirep_drive_on=1;uirep_activity_start(NULL,1,0);assert(test_uirep_count('A')==1);test_uirep_drive_on=0;test_uirep_available=1; /* already charging */
    uirep_activity_start(NULL,1,0);assert(test_uirep_count('A')==1);                                                 /* already the activity */
    uirep_activity_start(NULL,3,0);assert(test_uirep_count('A')==1 && uirep_scope_refused==1);                       /* only travel here */
    uirep_activity_stop(NULL,3,1);assert(test_uirep_count('A')==1);                                                  /* not the activity */
    uirep_activity_stop(NULL,1,1);assert(test_uirep_count('A')==2 && test_uirep_calls[1].b==0);
    uirep_activity_stop(NULL,0,1);assert(test_uirep_count('A')==2);                                                  /* none */
    assert(!test_uirep_count('S') && !test_uirep_count('E'));pstk_scope_slot=0;
    /* The setter's own events coming back (live u1..u3, a stack overflow at the stop): the setter runs once, stop and start. */
    {LONG64 nested=uirep_scope_nested;test_uirep_call_count=0;pstk_scope_slot=2;test_uirep_events_back=1;
     uirep_activity_start(NULL,1,0);assert(test_uirep_count('A')==1 && test_uirep_calls[0].b==1 && uirep_scope_nested==nested+2);
     int now=0;memcpy(&now,test_uirep_player+UIREP_PLAYER_ACTIVITY,4);assert(now==1);
     uirep_activity_stop(NULL,1,1);assert(test_uirep_count('A')==2 && test_uirep_calls[1].b==0 && uirep_scope_nested==nested+4);
     memcpy(&now,test_uirep_player+UIREP_PLAYER_ACTIVITY,4);assert(!now && !uirep_setter_depth);
     /* Afterwards the adapter answers again: the simulation's own stop of a later travel still ends it. */
     test_uirep_events_back=0;uirep_activity_start(NULL,1,0);uirep_activity_stop(NULL,1,1);assert(test_uirep_count('A')==4);
     pstk_scope_slot=0;}
    /* Slot 1 goes through X4's own functions, canonical: MD on every client. */
    test_uirep_call_count=0;stats.slot=1;uirep_ui_depth=1;uirep_activity_start(NULL,4,0);uirep_activity_stop(NULL,1,1);uirep_ui_depth=0;stats.slot=2;
    assert(!test_uirep_call_count && uirep_queue_count==3);   /* slot 1 captures any activity; the left-over event of client 2 goes first */
    uirep_queue_head=uirep_queue_count=0;memset(uirep_ring,0,sizeof(uirep_ring));uirep_staged_count=0;
    {unsigned char one[]={UIREP_VERSION,110,0,2,UIREP_OP_ACTIVITY_START,2,4,0,UIREP_OP_ACTIVITY_STOP,2,1,1};
     assert(uirep_stage(1,one,sizeof(one),111,&why));
     test_uirep_call_count=0;assert(uirep_frame(113,&why) && test_uirep_call_count==2);
     assert(test_uirep_calls[0].what=='S' && test_uirep_calls[0].b==4 && test_uirep_calls[0].scope==0 && test_uirep_calls[1].what=='E' && test_uirep_calls[1].b==1 && test_uirep_calls[1].c==1);}
    /* A fault in a replayed call stops, the scope closed. */
    {unsigned char one[]={UIREP_VERSION,120,0,1,UIREP_OP_ACTIVITY_START,2,1,0};
     memset(test_uirep_player+UIREP_PLAYER_ACTIVITY,0,4);assert(uirep_stage(2,one,sizeof(one),121,&why));
     test_uirep_fault=1;test_uirep_call_count=0;
     assert(!uirep_frame(123,&why) && !strcmp(why,"ui_fault") && uirep_failed && !strcmp(uirep_why,"ui_fault") && test_uirep_count('}')==1 && !pstk_scope_slot && !uirep_replaying && !uirep_setter_depth);
     test_uirep_fault=0;uirep_failed=0;uirep_why="-";
     assert(uirep_stage(2,one,sizeof(one),121,&why));   /* the same section, staged again by the test */
     test_uirep_enter_ok=0;assert(!uirep_frame(123,&why) && !strcmp(why,"ui_scope"));test_uirep_enter_ok=1;uirep_failed=0;uirep_why="-";
     test_uirep_leave_ok=0;assert(!uirep_frame(123,&why) && !strcmp(why,"ui_scope_leave"));test_uirep_leave_ok=1;uirep_failed=0;uirep_why="-";}
    /* The soft target's blocks: the UI half goes on, the simulation half becomes an event. */
    {uint64_t id=0x7001;memset(test_uirep_target,0,sizeof(test_uirep_target));memcpy(test_uirep_target+UIREP_ID,&id,8);}
    for(unsigned i=0;i<UIREP_OFFSET;i++) test_uirep_element[UIREP_ELEMENT_OFFSET+i]=(unsigned char)(0x40+i);
    test_uirep_element[UIREP_ELEMENT_FLAG]=1;
    stats.slot=2;stats.issued=200;uirep_queue_head=uirep_queue_count=0;memset(uirep_ring,0,sizeof(uirep_ring));test_uirep_call_count=0;
    uirep_set_hint(0x11);
    /* Outside the local phases, MD's clear_player_target / set_player_target (their call's return address above 0xaecb60): at once, stock. */
    {static const uint32_t md_clear[]={0xae849au,0xaebf87u,0xae809fu,UIREP_MD_CLEAR_RETURN},md_set[]={0xaeb2b0u,0xb3f84bu,UIREP_MD_SET_RETURN,0x1111aaau};
     test_uirep_route_set(4,md_clear);
     uirep_target_clear_stub(test_uirep_target);
     assert(test_uirep_count('C')==1 && uirep_get_hint()==0xbeef && uirep_target_passed==1 && uirep_target_md==1 && test_uirep_route_site==UIREP_S_TARGET_CLEAR);
     test_uirep_route_set(4,md_set);
     uirep_target_set_stub(test_uirep_target,test_uirep_element);
     assert(test_uirep_count('T')==1 && uirep_target_passed==2 && test_uirep_calls[1].a==test_uirep_target+UIREP_ASSOCIATION && test_uirep_calls[1].b==1 && test_uirep_offset_seen[3]==0x43);
     assert(uirep_target_md==2 && test_uirep_route_site==UIREP_S_TARGET_SET && !uirep_target_ui && !uirep_queue_count);}
    test_uirep_call_count=0;uirep_set_hint(0x11);InterlockedExchange(&pres_open,1);
    uirep_target_clear_stub(test_uirep_target);assert(!test_uirep_call_count && uirep_queue_count==1 && !uirep_get_hint());
    uirep_target_set_stub(test_uirep_target,test_uirep_element);assert(!test_uirep_call_count && uirep_queue_count==2 && uirep_get_hint()==0x7001);
    InterlockedExchange(&pres_open,0);
    length=uirep_take(200,UIREP_SECTION_MAX,section);assert(length==UIREP_HEAD+2+UIREP_REF+2+UIREP_REF+1+UIREP_OFFSET);
    /* The UI's own writes outside the local phases (missile1: its target element dying with the slot ship, 0xaeb4e0 -> 0xaeb970 ->
     * 0xaecb60): nothing at once, the very events a click makes, the route in the status. An address next to MD's is not MD. */
    {static const uint32_t ui[]={0xaeba54u,0xaeb510u,0xad7064u,UIREP_MD_SET_RETURN-1u,UIREP_MD_CLEAR_RETURN+1u,0x9fdf8bu,0xef46b6u};
     unsigned char again[UIREP_SECTION_MAX];char text[768];
     test_uirep_route_set(7,ui);test_uirep_call_count=0;uirep_set_hint(0x11);
     uirep_target_clear_stub(test_uirep_target);assert(!test_uirep_call_count && uirep_queue_count==1 && !uirep_get_hint() && uirep_target_ui==1);
     uirep_target_set_stub(test_uirep_target,test_uirep_element);assert(!test_uirep_call_count && uirep_queue_count==2 && uirep_get_hint()==0x7001 && uirep_target_ui==2);
     assert(uirep_take(200,UIREP_SECTION_MAX,again)==length && !memcmp(again,section,length) && uirep_target_passed==2 && uirep_target_md==2);
     assert(uirep_status(text,sizeof(text)) && strstr(text," target_ui=2/0 target_md=2 target_route=aeba54,aeb510,ad7064,1135ea5,1135f3c,9fdf8b route_faults=0"));
     /* Off the frame thread: refused, nothing runs anywhere, nothing queued; the shown route stays the frame thread's. */
     HANDLE h=CreateThread(NULL,0,test_uirep_off_thread,NULL,0,NULL);assert(h);assert(WaitForSingleObject(h,5000)==WAIT_OBJECT_0);CloseHandle(h);
     assert(!test_uirep_call_count && !uirep_queue_count && uirep_target_ui_refused==1 && uirep_target_ui==2 && uirep_route_last_count==6);
     /* The replay applying and the time past the window stay stock, whatever the route. */
     InterlockedExchange(&uirep_replaying,1);uirep_target_clear_stub(test_uirep_target);InterlockedExchange(&uirep_replaying,0);
     assert(test_uirep_count('C')==1 && uirep_target_passed==3 && !uirep_queue_count);
     uirep_stopped=1;uirep_target_set_stub(test_uirep_target,test_uirep_element);uirep_stopped=0;
     assert(test_uirep_count('T')==1 && uirep_target_passed==4 && !uirep_queue_count && uirep_target_ui==2 && uirep_target_md==2);
     /* No frame at all (a walk that unwinds nothing): not MD, captured. */
     test_uirep_route_set(0,ui);test_uirep_call_count=0;uirep_target_clear_stub(test_uirep_target);
     assert(!test_uirep_call_count && uirep_queue_count==1 && uirep_target_ui==3 && !uirep_route_last_count);
     uirep_queue_head=uirep_queue_count=0;uirep_route_override=NULL;test_uirep_call_count=0;}
    /* Two clients apply it alike: the same calls in slot 2's scope, the same receipt, the local hint kept. */
    char receipt[2][192];
    for(unsigned client=0;client<2;client++) {
        const char *w="";test_uirep_call_count=0;memset(uirep_ring,0,sizeof(uirep_ring));
        assert(uirep_stage(2,section,length,201,&w));
        uirep_set_hint(client?0x22:0x7001);
        assert(uirep_frame(203,&w) && uirep_frame_events==2);
        assert(test_uirep_call_count==4 && test_uirep_calls[1].what=='C' && test_uirep_calls[2].what=='T' && test_uirep_calls[1].a==test_uirep_target+UIREP_ASSOCIATION && test_uirep_calls[2].scope==2);
        assert(test_uirep_calls[2].b==1 && test_uirep_offset_seen[15]==0x4f && uirep_get_hint()==(client?0x22u:0x7001u));
        assert(uirep_frame_text(receipt[client],sizeof(receipt[client])));
    }
    assert(!strcmp(receipt[0],receipt[1]) && !strncmp(receipt[0],"PUIREP_1:203:2:",15));
    /* The receipt holds every slot's activity and soft target: what took on one client only shows. */
    {char text[192];memcpy(test_uirep_player+UIREP_PLAYER_ACTIVITY,(int[]){1},4);*(uint64_t*)(test_uirep_player+PRES_TARGET)=0x7001;
     assert(uirep_frame_text(text,sizeof(text)) && strstr(text,"|0/0,1/7001"));
     memset(test_uirep_player+UIREP_PLAYER_ACTIVITY,0,4);*(uint64_t*)(test_uirep_player+PRES_TARGET)=0;}
    /* A dead slot's event is consumed and hashed, not applied. */
    {const char *w="";memset(uirep_ring,0,sizeof(uirep_ring));assert(uirep_stage(2,section,length,201,&w));LONG64 stale=uirep_stale;test_uirep_call_count=0;
     pdeath_armed=1;pdeath_dead=2u;assert(uirep_frame(203,&w) && uirep_frame_events==2 && !test_uirep_call_count && uirep_stale==stale+1);pdeath_armed=0;pdeath_dead=0;}
    /* A target gone is the same everywhere, a portable ID that differs stops. */
    {unsigned char gone[UIREP_HEAD+2+UIREP_REF]={UIREP_VERSION,210&0xff,0,1,UIREP_OP_TARGET_CLEAR,UIREP_REF,0x99};
     LONG64 stale=uirep_stale;assert(uirep_stage(1,gone,sizeof(gone),211,&why));test_uirep_call_count=0;
     assert(uirep_frame(213,&why) && !test_uirep_call_count && uirep_stale==stale+1);
     memset(uirep_ring,0,sizeof(uirep_ring));assert(uirep_stage(2,section,length,201,&why));
     test_uirep_portable_salt=1;assert(!uirep_frame(203,&why) && !strcmp(why,"ui_ref_mismatch") && uirep_failed);test_uirep_portable_salt=0;uirep_failed=0;uirep_why="-";}
    /* Order hooks: captured in the UI for Lua to run, forwarded otherwise. Guards refuse with the failure value. */
    test_uirep_call_count=0;uirep_queue_head=uirep_queue_count=0;memset(uirep_ring,0,sizeof(uirep_ring));
    assert(uirep_orders_clear2(0x7001,1,0,4,5,6,7,8)==77 && test_uirep_count('F')==1);
    assert(uirep_guard_create3(0x7001,1,2,4,5,6,7,8)==77 && test_uirep_count('F')==2 && !uirep_guard_refused);
    uirep_ui_depth=1;test_uirep_call_count=0;
    assert(uirep_orders_clear(0x7001,0,0,0,0,0,7,8)==1 && uirep_orders_clear2(0x7001,1,1,0,0,0,7,8)==1 && !test_uirep_call_count && uirep_queue_count==2);
    assert(!uirep_guard_enable(0x7001,1,2,4,5,6,7,8) && !uirep_guard_adjust(1,1,2,4,5,0,7,8) && uirep_guard_refused==2 && !test_uirep_call_count);
    assert(uirep_guard_adjust(1,1,2,4,5,6,7,8)==77 && uirep_guard_refused==2 && test_uirep_count('F')==1);test_uirep_call_count=0;   /* check-only: a query */
    {char text[256];assert(uirep_refusals_text(text,sizeof(text)) && strstr(text,"200:EnableOrder:not_replayed_yet"));}
    InterlockedExchange(&uirep_replaying,1);assert(uirep_guard_enable(0x7001,1,2,4,5,6,7,8)==77);assert(uirep_orders_clear2(0x7001,1,2,4,5,6,7,8)==77);InterlockedExchange(&uirep_replaying,0);
    uirep_stopped=1;assert(uirep_guard_enable(0x7001,1,2,4,5,6,7,8)==77);uirep_stopped=0;
    uirep_ui_depth=0;
    /* Lua's own event, in order with them; the Lua-run list; no scope opens for them natively. */
    {unsigned char lua_event[3]={2,0,0};assert(uirep_push(UIREP_OP_LUA_CALL,lua_event,3));}
    length=uirep_take(200,UIREP_SECTION_MAX,section);assert(length && section[3]==3);
    assert(uirep_stage(2,section,length,201,&why));test_uirep_call_count=0;
    assert(uirep_frame(203,&why) && !test_uirep_call_count && uirep_frame_events==3);
    {char text[1024];int n=uirep_lua_events(203,text,sizeof(text));
     assert(n>0 && !strncmp(text,"2:65:",5) && strstr(text,";2:66:") && strstr(text,";2:67:020000") && uirep_applied_lua==3);
     assert(!uirep_lua_events(203,text,sizeof(text)));}
    uirep_result(203,1);uirep_result(203,0);assert(uirep_lua_errors==1);
    {char text[192];assert(uirep_frame_text(text,sizeof(text)));char ok_only[192];uint64_t h=uirep_results_hash;uirep_results_tick=0;uirep_result(203,1);uirep_result(203,1);assert(uirep_frame_text(ok_only,sizeof(ok_only)) && strcmp(text,ok_only) && uirep_results_hash!=h);}
    /* A push that is too long or a full queue refuses and runs nothing. */
    {static unsigned char big[UIREP_EVENT_MAX+1];unsigned before=uirep_queue_count;LONG64 refused=uirep_refused;
     assert(!uirep_push(UIREP_OP_LUA_CALL,big,UIREP_EVENT_MAX+1) && uirep_queue_count==before && uirep_refused==refused+1);
     for(unsigned i=0;i<UIREP_QUEUE;i++) assert(uirep_push(UIREP_OP_LUA_CALL,big,1));
     assert(!uirep_push(UIREP_OP_LUA_CALL,big,1) && uirep_queue_count==UIREP_QUEUE);
     unsigned n=uirep_take(201,UIREP_SECTION_MAX,section);assert(n && section[3]==UIREP_EVENTS_MAX && uirep_queue_count==UIREP_QUEUE-UIREP_EVENTS_MAX);
     uirep_queue_head=uirep_queue_count=0;}
    /* Generic calls: autopilot, dock and weapon hooks capture by their spec and apply through the trampoline with the
     * slot's player. A fake Universe: slot 1's player, swapped for slot 2's inside its scope. */
    {   static unsigned char universe[0x400];test_uirep_universe=universe;
        void *u=universe,*p1=test_uirep_p1;memcpy(region+PNAT_UNIVERSE,&u,8);memcpy(universe+VPS_SINGLETON,&p1,8);
        assert(pnat_player()==test_uirep_p1);
        uirep_queue_head=uirep_queue_count=0;memset(uirep_ring,0,sizeof(uirep_ring));test_uirep_call_count=0;
        for(unsigned i=UIREP_H_AP_START;i<UIREP_HOOKS;i++) uirep_fn.forward[i]=test_uirep_call_fwd;
        uirep_fn.forward[UIREP_H_AP_START]=test_uirep_call_vec_fwd;uirep_fn.forward[UIREP_H_WMODE]=test_uirep_call_str_fwd;
        unsigned saved_families=uirep_families;uirep_families=UIREP_FAMILIES&~UIREP_FAMILY_FLIGHT;
        __attribute__((aligned(16))) float offset[4]={1.5f,-2.0f,3.25f,0.0f};
        /* Outside the local phases every hook is X4's own; a check-only call is a query everywhere. */
        assert(uirep_call_ap_start((uint64_t)(uintptr_t)test_uirep_p1,(uint64_t)(uintptr_t)test_uirep_target,(uint64_t)(uintptr_t)offset,1,1,3,0,0)==55+2 &&
               test_uirep_count('K')==1 && !uirep_queue_count);
        uirep_ui_depth=1;
        assert(uirep_call_ap_start((uint64_t)(uintptr_t)test_uirep_p1,(uint64_t)(uintptr_t)test_uirep_target,(uint64_t)(uintptr_t)offset,1,0,3,1,0)==55+1 &&
               test_uirep_count('K')==2 && !uirep_queue_count);
        /* Captured: nothing runs here, the event holds the target and the offset. */
        assert(uirep_call_ap_start((uint64_t)(uintptr_t)test_uirep_p1,(uint64_t)(uintptr_t)test_uirep_target,(uint64_t)(uintptr_t)offset,1,0,3,0,0)==1 &&
               test_uirep_count('K')==2 && uirep_queue_count==1);
        /* Stop predicts its answer through the check-only variant: one forward with the flag set, then it is queued. */
        assert(uirep_call_ap_stop((uint64_t)(uintptr_t)test_uirep_p1,1,0,1,0,0,0,0)==57 && test_uirep_count('K')==3 &&
               test_uirep_call_args[4]==1 && uirep_queue_count==2);
        /* Another player, an unknown component, a long string: refused, nothing runs, nothing queued. */
        LONG64 refused=uirep_refused;
        assert(!uirep_call_quickdock((uint64_t)(uintptr_t)test_uirep_player,0,0,0,0,0,0,0) && uirep_refused==refused+1);
        assert(!uirep_call_dockat(0x9999,0,0,0,0,0,0,0) && uirep_refused==refused+2);
        {static char longmode[UIREP_STRING_MAX+2];memset(longmode,'x',sizeof(longmode)-1);
         assert(!uirep_call_wmode(0x7001,(uint64_t)(uintptr_t)longmode,0,0,0,0,0,0) && uirep_refused==refused+3);}
        {char text[512];assert(uirep_refusals_text(text,sizeof(text)) && strstr(text,"QuickDock:not_the_player") && strstr(text,"RequestDockAt:unmapped") &&
                               strstr(text,"SetWeaponMode:string"));}
        assert(test_uirep_count('K')==3 && uirep_queue_count==2);
        /* Ids, bytes, words and strings. */
        assert(uirep_call_wgroup_set(0x7001,0x7001,1,0x01020304,0,0,0,0)==1 && uirep_call_wmode(0x7001,(uint64_t)(uintptr_t)"attackenemies",0,0,0,0,0,0)==1);
        assert(uirep_call_undock((uint64_t)(uintptr_t)test_uirep_p1,0,0,0,0,0,0,0)==55+0 && test_uirep_call_args[1]==1 && uirep_queue_count==5);
        uirep_ui_depth=0;test_uirep_call_count=0;
        /* On the wire, staged, applied in slot 2's scope: the player is slot 2's, the target resolved, the offset copied. */
        length=uirep_take(300,UIREP_SECTION_MAX,section);assert(length && section[3]==5 && !uirep_queue_count);
        {uint16_t c16=0;const char *w="";assert(uirep_section_ok(2,section,length,&c16,&w));
         unsigned char bad[UIREP_SECTION_MAX];memcpy(bad,section,length);bad[5]-=1;assert(!uirep_section_ok(2,bad,length,&c16,&w) && !strcmp(w,"ui_call"));
         uirep_families=UIREP_FAMILY_TRAVEL;assert(!uirep_section_ok(2,section,length,&c16,&w) && !strcmp(w,"ui_call"));uirep_families=UIREP_FAMILIES&~UIREP_FAMILY_FLIGHT;}
        assert(uirep_stage(2,section,length,301,&why));
        memset(test_uirep_call_vector,0,16);LONG64 applied=uirep_calls_applied;
        assert(uirep_frame(303,&why) && uirep_calls_applied==applied+5);
        unsigned seen=0;
        for(unsigned i=0;i<test_uirep_call_count;i++) if(test_uirep_calls[i].what=='K') {
            assert(test_uirep_calls[i].scope==2 && test_uirep_calls[i].a==test_uirep_player);++seen;
        }
        assert(seen==5 && !memcmp(test_uirep_call_vector,offset,16));
        assert(!strcmp(test_uirep_call_string,"attackenemies"));
        /* The second call's arguments were the weapon group's: ids as ids, the bool, the word. */
        /* Slot 1 applies canonical, with slot 1's player. */
        uirep_ui_depth=1;stats.slot=1;assert(uirep_call_quickdock((uint64_t)(uintptr_t)test_uirep_p1,0,0,0,0,0,0,0)==1);uirep_ui_depth=0;
        length=uirep_take(310,UIREP_SECTION_MAX,section);assert(uirep_stage(1,section,length,311,&why));test_uirep_call_count=0;
        assert(uirep_frame(313,&why) && test_uirep_count('K')==1 && test_uirep_calls[0].a==test_uirep_p1 && test_uirep_calls[0].scope==0 && !test_uirep_count('{'));
        stats.slot=2;
        /* The receipt: autopilot on and the weapon groups, from the canonical bank for slot 1 and slot 2's store. */
        {   static __attribute__((aligned(16))) unsigned char ship1[0x700],ship2[0x700];
            pilot_slot saved_slots[2];memcpy(saved_slots,pilot_slots,sizeof(saved_slots));
            pilot_slots[0].resolved=pilot_slots[1].resolved=1;pilot_slots[0].object=ship1;pilot_slots[1].object=ship2;
            *(int32_t*)(ship1+UIREP_GROUP_PRIMARY)=0;*(int32_t*)(ship1+UIREP_GROUP_PRIMARY+4)=1;
            *(int32_t*)(ship2+UIREP_GROUP_PRIMARY)=2;*(int32_t*)(ship2+UIREP_GROUP_PRIMARY+4)=-1;
            *(uint64_t*)(region+UIREP_AUTOPILOT)=0x1234;
            pslots[2].length=pslot_store_init(pslots[2].store);
            unsigned at=test_pslot_store_offset(UIREP_AUTOPILOT);memset(pslots[2].store+at,0,8);
            char text[192];assert(uirep_frame_text(text,sizeof(text)) && strstr(text,"/1/0.1,") && strstr(text,"/0/2.-1"));
            *(uint64_t*)(pslots[2].store+at)=7;assert(uirep_frame_text(text,sizeof(text)) && strstr(text,"/1/2.-1"));
            uirep_families=UIREP_FAMILY_TRAVEL;assert(uirep_frame_text(text,sizeof(text)) && !strstr(text,"/1/0.1"));uirep_families=UIREP_FAMILIES&~UIREP_FAMILY_FLIGHT;
            memcpy(pilot_slots,saved_slots,sizeof(saved_slots));*(uint64_t*)(region+UIREP_AUTOPILOT)=0;pslots[2].length=0;
        }
        /* The flight family: flight assist on the player (the singleton's at the capture; slot 2's own, P_2, in its scope at
         * the apply), the HUD's own-ship buttons by their number. Captured in the UI, nothing runs here; on the wire, staged,
         * applied in slot 2's scope; refused on the wire without the family. Its receipt part: assist and drones armed. */
        {   uirep_families=UIREP_FAMILY_FLIGHT;uirep_queue_head=uirep_queue_count=0;test_uirep_call_count=0;
            assert(uirep_call_fassist((uint64_t)(uintptr_t)test_uirep_p1,0,0,0,0,0,0,0)==55 && test_uirep_count('K')==1);   /* outside: X4's own */
            uirep_ui_depth=1;test_uirep_call_count=0;
            assert(uirep_call_fassist((uint64_t)(uintptr_t)test_uirep_p1,0,0,0,0,0,0,0)==1 && uirep_call_turret_mode(3,0,0,0,0,0,0,0)==1 &&
                   uirep_call_turret_missile(1,0,0,0,0,0,0,0)==1 && uirep_call_drones(2,0,0,0,0,0,0,0)==1 && uirep_call_drone_mode(3,0,0,0,0,0,0,0)==1);
            assert(!test_uirep_call_count && uirep_queue_count==5);
            {LONG64 refused=uirep_refused;   /* another player than the singleton: refused, nothing queued */
             assert(!uirep_call_fassist((uint64_t)(uintptr_t)test_uirep_ship,0,0,0,0,0,0,0) && uirep_refused==refused+1 && uirep_queue_count==5);
             char text[256];assert(uirep_refusals_text(text,sizeof(text)) && strstr(text,"FlightAssistToggle:not_the_player"));}
            uirep_ui_depth=0;
            length=uirep_take(320,UIREP_SECTION_MAX,section);assert(length && section[3]==5 && !uirep_queue_count);
            {uint16_t c16=0;const char *w="";assert(uirep_section_ok(2,section,length,&c16,&w));
             uirep_families=UIREP_FAMILY_WEAPONS;assert(!uirep_section_ok(2,section,length,&c16,&w) && !strcmp(w,"ui_call"));uirep_families=UIREP_FAMILY_FLIGHT;}
            assert(uirep_stage(2,section,length,321,&why));
            LONG64 applied=uirep_calls_applied;
            assert(uirep_frame(323,&why) && uirep_calls_applied==applied+5 && test_uirep_count('K')==5);
            static const uint64_t args[5]={0,3,1,2,3};
            for(unsigned i=0,j=0;i<test_uirep_call_count;i++) {
                if(test_uirep_calls[i].what!='K') continue;
                assert(test_uirep_calls[i].scope==2 && test_uirep_calls[i].a==test_uirep_player);
                assert(test_uirep_calls[i].b==(j?args[j]:(uint64_t)(uintptr_t)test_uirep_player));   /* the player argument is slot 2's */
                ++j;
            }
            /* Slot 1 canonical: the singleton's own player, no scope. */
            uirep_ui_depth=1;assert(uirep_call_fassist((uint64_t)(uintptr_t)test_uirep_p1,0,0,0,0,0,0,0)==1);uirep_ui_depth=0;
            length=uirep_take(330,UIREP_SECTION_MAX,section);assert(uirep_stage(1,section,length,331,&why));test_uirep_call_count=0;
            assert(uirep_frame(333,&why) && test_uirep_count('K')==1 && test_uirep_calls[0].a==test_uirep_p1 && test_uirep_calls[0].b==(uint64_t)(uintptr_t)test_uirep_p1 &&
                   !test_uirep_calls[0].scope && !test_uirep_count('{'));
            static __attribute__((aligned(16))) unsigned char ship1[0x700],ship2[0x700];
            pilot_slot saved_slots[2];memcpy(saved_slots,pilot_slots,sizeof(saved_slots));
            pilot_slots[0].resolved=pilot_slots[1].resolved=1;pilot_slots[0].object=ship1;pilot_slots[1].object=ship2;
            test_uirep_p1[UIREP_PLAYER_ASSIST]=1;test_uirep_player[UIREP_PLAYER_ASSIST]=0;ship1[UIREP_SHIP_DRONES+1]=1;ship2[UIREP_SHIP_DRONES+2]=1;
            char text[256];assert(uirep_frame_text(text,sizeof(text)) && strstr(text,"|0/0/f1.010,0/0/f0.001"));
            test_uirep_player[UIREP_PLAYER_ASSIST]=1;assert(uirep_frame_text(text,sizeof(text)) && strstr(text,",0/0/f1.001"));
            uirep_families=UIREP_FAMILY_TRAVEL;assert(uirep_frame_text(text,sizeof(text)) && !strstr(text,"/f"));
            memcpy(pilot_slots,saved_slots,sizeof(saved_slots));test_uirep_p1[UIREP_PLAYER_ASSIST]=0;test_uirep_player[UIREP_PLAYER_ASSIST]=0;
            uirep_families=UIREP_FAMILIES&~UIREP_FAMILY_FLIGHT;test_uirep_call_count=0;
        }
        /* The stock record: dock and weapon keys replayed, the autopilot key run here, all off without their family. */
        assert(pstk_verdict_slot(PSTK_ACTION,0xaf,2)==PSTK_REPLAY && pstk_verdict_slot(PSTK_ACTION,0xd3,2)==PSTK_REPLAY);
        assert(pstk_verdict_slot(PSTK_ACTION,0x8c,2)==PSTK_REPLAY && pstk_verdict_slot(PSTK_ACTION,0x9e,1)==PSTK_REPLAY &&
               pstk_verdict_slot(PSTK_ACTION,0x9,2)==PSTK_REPLAY && pstk_verdict_slot(PSTK_ACTION,0x141,2)==PSTK_REPLAY);
        assert(pstk_verdict_slot(PSTK_ACTION,0xb3,2)==PSTK_LOCAL && pstk_verdict(PSTK_ACTION,0x13f)==PSTK_REFUSE);
        uirep_families=UIREP_FAMILY_TRAVEL;
        assert(pstk_verdict(PSTK_ACTION,0xaf)==PSTK_REFUSE && pstk_verdict(PSTK_ACTION,0x8c)==PSTK_REFUSE && pstk_verdict(PSTK_ACTION,0xb3)==PSTK_REFUSE);
        uirep_families=saved_families;
        memset(region+PNAT_UNIVERSE,0,8);test_uirep_universe=NULL;
        for(unsigned i=UIREP_H_CLEAR;i<UIREP_HOOKS;i++) uirep_fn.forward[i]=test_uirep_forward;
    }
    /* The queue editor (family queue): captured in the UI as Lua-run events in call order; a create answers with the
     * index it predicts (queue length + 1 + creates in flight); applied, X4's index becomes the alias the slot's events
     * captured before it applied are mapped through; gone and slot ships run nothing, the same everywhere. */
    {   uirep_queue_head=uirep_queue_count=0;memset(uirep_ring,0,sizeof(uirep_ring));test_uirep_call_count=0;
        unsigned saved_families=uirep_families,saved_capture=pstk_capture_slot;uirep_families=UIREP_FAMILY_QUEUE;pstk_capture_slot=2;
        uirep_queue_reset();uirep_num_orders_override=test_uirep_num_orders;test_uirep_orders=3;test_uirep_queue_answer=9;
        for(unsigned i=UIREP_H_CREATE3;i<=UIREP_H_DEPLOY;i++) uirep_fn.forward[i]=test_uirep_queue_fwd;
        uirep_fn.forward[UIREP_H_LOOP]=uirep_fn.forward[UIREP_H_LOOP_CURRENT]=test_uirep_queue_fwd;
        const uint64_t wait=(uint64_t)(uintptr_t)"Wait",move=(uint64_t)(uintptr_t)"MoveWait";
        /* Outside the local phases X4's own. */
        assert(uirep_guard_create3(0x7001,move,0,0,0,0,0,0)==9 && test_uirep_count('Q')==1 && !uirep_queue_count);
        uirep_ui_depth=1;test_uirep_call_count=0;
        assert(uirep_guard_create3(0x7001,move,0,0,0,0,0,0)==4 && uirep_guard_create3(0x7001,wait,0,0,0,0,0,0)==5);   /* 3 + 1, then one in flight */
        assert(uirep_guard_create3(0x7001,wait,1,0,0,0,0,0)==0);                     /* the planned default order: not in the queue */
        assert(uirep_guard_enable(0x7001,4,0,0,0,0,0,0)==1);
        assert(uirep_guard_adjust(0x7001,5,1,1,0,1,0,0)==9 && test_uirep_count('Q')==1 && test_uirep_call_args[5]==1);   /* check only: a query */
        assert(uirep_guard_adjust(0x7001,5,1,1,0,0,0,0)==9 && test_uirep_count('Q')==2 && test_uirep_call_args[5]==1);   /* predicted by the query */
        assert(uirep_guard_reset(0x7001,4,1,0,0,0,0,0)==0 && uirep_guard_loop(0x7001,4,0,0,0,0,0,0)==9 && test_uirep_count('Q')==3);
        assert(!test_uirep_queue_strings && uirep_queue_count==7 && uirep_q_captured==7 && uirep_q_captured_creates==2);
        /* Refused, nothing runs, nothing queued: unknown, a slot ship, a long definition. */
        {   LONG64 refused=uirep_refused;unsigned char *saved_object=pilot_slots[1].object;static char longdef[UIREP_STRING_MAX+2];
            memset(longdef,'x',sizeof(longdef)-1);
            assert(!uirep_guard_create3(0x9999,move,0,0,0,0,0,0) && !uirep_guard_create3(0x7001,(uint64_t)(uintptr_t)longdef,0,0,0,0,0,0));
            pilot_slots[1].object=test_uirep_target;assert(!uirep_guard_enable(0x7001,4,0,0,0,0,0,0));pilot_slots[1].object=saved_object;
            char text[512];assert(uirep_refused==refused+3 && uirep_refusals_text(text,sizeof(text)) && strstr(text,"CreateOrder3:unmapped") &&
                                  strstr(text,"CreateOrder3:string") && strstr(text,"EnableOrder:slot_ship"));
            assert(uirep_queue_count==7 && test_uirep_count('Q')==3);
        }
        uirep_ui_depth=0;
        /* On the wire: Lua-run (no scope, nothing native), its length checked, the family required. */
        length=uirep_take(400,UIREP_SECTION_MAX,section);assert(length && section[3]==7);
        {uint16_t c16=0;const char *w="";assert(uirep_section_ok(2,section,length,&c16,&w));
         unsigned char bad[UIREP_SECTION_MAX];memcpy(bad,section,length);bad[5]-=1;assert(!uirep_section_ok(2,bad,length,&c16,&w) && !strcmp(w,"ui_queue"));
         uirep_families=UIREP_FAMILY_ORDERS;assert(!uirep_section_ok(2,section,length,&c16,&w) && !strcmp(w,"ui_queue"));uirep_families=UIREP_FAMILY_QUEUE;}
        assert(uirep_stage(2,section,length,401,&why));test_uirep_call_count=0;
        assert(uirep_frame(403,&why) && uirep_frame_events==7 && !test_uirep_call_count);
        /* Applied in call order (the Lua core hands each to gate.ui_replay_queue_run): X4 puts the creates at 6 and 7. */
        {   char what[96];unsigned at=UIREP_HEAD;const unsigned char *events[7];unsigned lengths[7];
            for(unsigned i=0;i<7;i++) {assert(section[at]==UIREP_OP_QUEUE);events[i]=section+at+2;lengths[i]=section[at+1];at+=2u+section[at+1];}
            test_uirep_queue_strings=1;test_uirep_queue_answer=6;
            assert(uirep_queue_run(2,events[0],lengths[0],what,sizeof(what))==1 && !strcmp(what,"CreateOrder3=6/4") && !strcmp(test_uirep_queue_string,"MoveWait"));
            test_uirep_queue_answer=7;assert(uirep_queue_run(2,events[1],lengths[1],what,sizeof(what))==1 && !strcmp(what,"CreateOrder3=7/5"));
            test_uirep_queue_answer=0;assert(uirep_queue_run(2,events[2],lengths[2],what,sizeof(what))==1 && uirep_q_applied[1]==2);
            test_uirep_queue_strings=0;test_uirep_queue_answer=1;
            assert(uirep_queue_run(2,events[3],lengths[3],what,sizeof(what))==1 && test_uirep_call_args[1]==6 && !strcmp(what,"EnableOrder=1"));
            assert(uirep_queue_run(2,events[4],lengths[4],what,sizeof(what))==1 && test_uirep_call_args[1]==7 && test_uirep_call_args[2]==1 &&
                   test_uirep_call_args[3]==1 && !test_uirep_call_args[5]);   /* the call itself, not the query */
            assert(uirep_queue_run(2,events[5],lengths[5],what,sizeof(what))==1 && test_uirep_call_args[1]==6 && test_uirep_call_args[2]==1 &&
                   !strcmp(what,"ResetOrder"));
            assert(uirep_queue_run(2,events[6],lengths[6],what,sizeof(what))==1 && test_uirep_call_args[1]==6);
            assert(uirep_q_calls==7 && uirep_q_creates==2 && uirep_q_moved==2 && uirep_q_mapped==4);
            /* Another slot's index is its own; an event captured after the creates applied means what it says. */
            assert(uirep_q_map(1,0x7001,4,0)==4 && uirep_q_map(2,0x7001,4,1)==4 && uirep_q_map(2,0x7001,5,1)==7 && uirep_q_map(2,0x7002,4,0)==4);   /* one more mapped: 5 */
            /* Captured now: nothing in flight (both applied), seen 2: its 4 is today's 4. */
            uirep_ui_depth=1;assert(uirep_q_own_applied()==2 && uirep_guard_create3(0x7001,wait,0,0,0,0,0,0)==4 && uirep_guard_enable(0x7001,4,0,0,0,0,0,0)==1);
            uirep_ui_depth=0;length=uirep_take(410,UIREP_SECTION_MAX,section);assert(length && section[3]==2);
            test_uirep_queue_answer=4;assert(uirep_queue_run(2,section+UIREP_HEAD+2,section[UIREP_HEAD+1],what,sizeof(what))==1 && uirep_q_applied[1]==3);
            at=UIREP_HEAD+2u+section[UIREP_HEAD+1];
            assert(uirep_queue_run(2,section+at+2,section[at+1],what,sizeof(what))==1 && test_uirep_call_args[1]==4 && uirep_q_mapped==5);
            /* Gone: nothing runs, the create is numbered all the same; references that differ and a bad length stop. */
            unsigned char gone[64];unsigned n=0;gone[n++]=UIREP_H_CREATE3;gone[n++]=0;gone[n++]=0;uirep_put_ref(gone+n,0x9999);n+=UIREP_REF;
            gone[n++]=4;memcpy(gone+n,"Wait",4);n+=4;gone[n++]=0;gone[n++]=0;gone[n++]=0;gone[n++]=9;gone[n++]=0;gone[n++]=0;gone[n++]=0;
            LONG64 calls=uirep_q_calls;
            assert(uirep_queue_run(2,gone,n,what,sizeof(what))==0 && !strcmp(what,"CreateOrder3 gone") && uirep_q_calls==calls && uirep_q_applied[1]==4 &&
                   uirep_q_skipped==1 && uirep_q_map(2,0x9999,9,0)==0);
            assert(uirep_queue_run(2,gone,n-1,what,sizeof(what))==-1 && !strcmp(what,"decode"));
            test_uirep_portable_salt=1;assert(uirep_queue_run(2,events[3],lengths[3],what,sizeof(what))==-1 && !strcmp(what,"ref_mismatch"));test_uirep_portable_salt=0;
            /* A slot ship (a PlayerController flies it) runs nothing, the same everywhere. */
            unsigned char *saved_object=pilot_slots[0].object;pilot_slots[0].object=test_uirep_target;
            assert(uirep_queue_run(2,events[3],lengths[3],what,sizeof(what))==0 && !strcmp(what,"EnableOrder slot_ship"));pilot_slots[0].object=saved_object;
            char status[1024];assert(uirep_status(status,sizeof(status)) && strstr(status,"queue=9/9/2 creates=4 mapped=6 moved=3"));
        }
        uirep_num_orders_override=NULL;uirep_queue_reset();uirep_families=saved_families;pstk_capture_slot=saved_capture;
        for(unsigned i=UIREP_H_CLEAR;i<UIREP_HOOKS;i++) uirep_fn.forward[i]=test_uirep_forward;
        uirep_queue_head=uirep_queue_count=0;memset(uirep_ring,0,sizeof(uirep_ring));test_uirep_call_count=0;
    }
    uirep_families=UIREP_FAMILIES&~UIREP_FAMILY_TRADE;   /* every hook of this header (trade: test_lockstep_ui_trade.h) */
    /* Install on executable fake bytes: every patch one CAS, every stride run for real, everything back. */
    for(unsigned i=0;i<UIREP_HOOKS;i++) {
        memset(region+uirep_hooks[i].rva,0xcc,0x20);memcpy(region+uirep_hooks[i].rva,uirep_hooks[i].bytes,uirep_hooks[i].prefix);
        region[uirep_hooks[i].rva+uirep_hooks[i].prefix]=0xc3;   /* ret behind the prefix */
    }
    for(unsigned i=0;i<UIREP_SITES;i++) memcpy(region+uirep_sites[i].rva,uirep_sites[i].bytes,5);
    {   /* The continuations go back into the runner. */
        const void *back=(const void*)test_uirep_site_back;
        pview_abs_jump(region+uirep_sites[0].resume,back);pview_abs_jump(region+uirep_sites[1].resume,back);pview_abs_jump(region+UIREP_CLEAR_ERROR,back);
    }
    uirep_fns stubs=uirep_fn;
    uirep_fn.ui_move=NULL;uirep_fn.start=uirep_fn.stop=NULL;memset(uirep_fn.forward,0,sizeof(uirep_fn.forward));
    assert(uirep_install() && uirep_installed && !uirep_restored && uirep_intact());
    for(unsigned i=0;i<UIREP_HOOKS;i++) assert(region[uirep_hooks[i].rva]==0xe9);
    assert(region[uirep_sites[0].rva]==0xe9 && region[uirep_sites[1].rva]==0xe9);
    assert(((uirep_sites[0].rva+4)&~15u)==(uirep_sites[0].rva&~15u) && ((uirep_sites[1].rva+4)&~15u)==(uirep_sites[1].rva&~15u));
    /* A hook's whole chain: X4's entry -> the near page -> our wrapper -> the trampoline -> behind the prefix. */
    ((void(*)(void*))(region+uirep_hooks[UIREP_H_UI].rva))((void*)9);
    assert(!uirep_ui_depth);
    /* Prefix bytes then the jump behind them, on the page. */
    assert(!memcmp(uirep_stride(UIREP_H_START),uirep_hooks[UIREP_H_START].bytes,7) && uirep_stride(UIREP_H_START)[7]==0xff && uirep_stride(UIREP_H_START)[8]==0x25);
    assert(uirep_fn.start==(uirep_activity_fn)uirep_stride(UIREP_H_START) && uirep_fn.forward[UIREP_H_DEPLOY]==(uirep_forward_fn)uirep_stride(UIREP_H_DEPLOY));
    {unsigned char t1[UIREP_STRIDE],t2[UIREP_STRIDE];assert(uirep_write_site(0,t1)<=UIREP_STRIDE && uirep_write_site(1,t2)<=UIREP_STRIDE);}
    /* The soft target's strides, entered as their patches enter them (on MD's route: the stock functions). */
    uirep_fn=stubs;test_uirep_call_count=0;
    {static const uint32_t md[]={UIREP_MD_SET_RETURN};test_uirep_route_set(1,md);}
    test_uirep_run_site(uirep_site_stride(UIREP_S_TARGET_SET),test_uirep_target,test_uirep_element,NULL);
    assert(test_uirep_count('T')==1 && test_uirep_calls[0].a==test_uirep_target+UIREP_ASSOCIATION && test_uirep_rbx_after==test_uirep_target && test_uirep_r13_after==test_uirep_element);
    test_uirep_run_site(uirep_site_stride(UIREP_S_TARGET_CLEAR),NULL,test_uirep_element,test_uirep_target);
    assert(test_uirep_count('C')==1 && test_uirep_calls[1].a==test_uirep_target+UIREP_ASSOCIATION && test_uirep_rbx_after==test_uirep_target);
    test_uirep_run_site(uirep_site_stride(UIREP_S_TARGET_CLEAR),(void*)1,test_uirep_element,NULL);  /* no component: X4's error path, nothing called */
    assert(test_uirep_count('C')==1 && test_uirep_rbx_after==NULL);
    /* The real walk from both strides: this test's frames and the stub by their tables up to the stride on the page; the fake image
     * at the block's resume has no table, so no MD: the UI's route, captured, nothing called, the registers kept. */
    uirep_route_override=NULL;test_uirep_call_count=0;
    {uint64_t issued=stats.issued,start=pstk_capture_start,stop=pilot_stop;LONG64 ui=uirep_target_ui,faults=uirep_route_faults;
     stats.issued=200;pstk_capture_start=50;pilot_stop=1000;uirep_queue_head=uirep_queue_count=0;
     uirep_route_reached=0;test_uirep_run_site(uirep_site_stride(UIREP_S_TARGET_SET),test_uirep_target,test_uirep_element,NULL);
     assert(uirep_route_reached && !test_uirep_call_count && uirep_queue_count==1 && uirep_target_ui==ui+1 && !uirep_route_last_count);
     assert(test_uirep_rbx_after==test_uirep_target && test_uirep_r13_after==test_uirep_element);
     uirep_route_reached=0;test_uirep_run_site(uirep_site_stride(UIREP_S_TARGET_CLEAR),NULL,test_uirep_element,test_uirep_target);
     assert(uirep_route_reached && !test_uirep_call_count && uirep_queue_count==2 && uirep_target_ui==ui+2 && test_uirep_rbx_after==test_uirep_target);
     assert(uirep_route_faults==faults && uirep_queue[0].op==UIREP_OP_TARGET_SET && uirep_queue[1].op==UIREP_OP_TARGET_CLEAR);
     /* A call that is not from a stride (directly from C): the walk never reaches the page, nothing unwound, still not MD. */
     uirep_route_reached=0;uirep_target_clear_stub(test_uirep_target);assert(!uirep_route_reached && !test_uirep_call_count && uirep_queue_count==3);
     uirep_queue_head=uirep_queue_count=0;stats.issued=issued;pstk_capture_start=start;pilot_stop=stop;}
    /* The pass: runtime IDs in set mode (armed after this module), installed once, intact checked, a lost patch stops with 164. */
    pilot_start=60;pilot_stop=1000;
    {int installed=uirep_installed;uirep_installed=0;rid_mode=RID_OBSERVE;
     assert(!uirep_pass(100) && uirep_failed && !strcmp(uirep_why,"ui_runtime_ids"));
     uirep_failed=0;uirep_why="-";rid_mode=RID_SET;uirep_installed=installed;}
    assert(uirep_pass(100) && !uirep_failed);
    {unsigned char keep=region[uirep_hooks[UIREP_H_STOP].rva+1];region[uirep_hooks[UIREP_H_STOP].rva+1]^=1;
     assert(!uirep_pass(101) && uirep_failed && !strcmp(uirep_why,"ui_hook_lost"));
     pnat_mode=PNAT_MODE_STOCK;pnat_failed=1;assert(pilot_stop_reason()==UIREP_STOP_REASON);pnat_failed=0;
     region[uirep_hooks[UIREP_H_STOP].rva+1]=keep;uirep_failed=0;uirep_why="-";}
    /* Behind the window everything is back, and whatever still runs is stock. */
    assert(uirep_pass(1001) && !uirep_installed && uirep_restored && uirep_stopped);
    for(unsigned i=0;i<UIREP_HOOKS;i++) assert(!memcmp(region+uirep_hooks[i].rva,uirep_hooks[i].bytes,uirep_hooks[i].prefix));
    for(unsigned i=0;i<UIREP_SITES;i++) assert(!memcmp(region+uirep_sites[i].rva,uirep_sites[i].bytes,5));
    uirep_ui_depth=1;test_uirep_call_count=0;uirep_activity_start(NULL,1,0);assert(test_uirep_count('S')==1);uirep_ui_depth=0;
    /* Only the armed families' code is touched; a restore off the pass works too. */
    uirep_families=UIREP_FAMILY_TRAVEL;uirep_stopped=0;
    assert(uirep_install() && region[uirep_hooks[UIREP_H_UI].rva]==0xe9 && region[uirep_hooks[UIREP_H_START].rva]==0xe9 && region[uirep_hooks[UIREP_H_CLEAR].rva]==0x48 && region[uirep_sites[0].rva]==0x48);
    uirep_restore();assert(!uirep_installed && uirep_restored && region[uirep_hooks[UIREP_H_START].rva]==0x48);
    /* A status line a log can carry. */
    {char text[640];assert(uirep_status(text,sizeof(text)) && strstr(text,"ui_replay=") && strstr(text,"families=1"));}
    /* Everything off. */
    test_uirep_reset();
    if(uirep_page) {VirtualFree(uirep_page,0,MEM_RELEASE);uirep_page=NULL;}
    VirtualFree(region,0,MEM_RELEASE);
    pnat_base=saved_base;uirep_fn=saved_fn;ui_thread=saved_ui;stats.issued=saved_issued;stats.slot=saved_slot;pilot_stop=saved_stop;pstk_capture_start=saved_start;
    pilot_delay=saved_delay;pilot_slot_count=saved_count;pstk_slot_mask=saved_mask;pstk_capturing=saved_capturing;pnat_armed=saved_armed;pnat_mode=saved_mode;
    pslot_armed=saved_slots;pres_armed=saved_present;rid_mode=saved_rid;pslots[2]=saved_pslot;pilot_start=0;
    printf("UI replay on stubs and an executable region verified\n");
}
/* X4's own unwind table (.pdata) inside the private mapping, for RtlAddFunctionTable. */
static PRUNTIME_FUNCTION test_uirep_tables(const char *path,unsigned char *mapped,DWORD *count) {
    FILE *file=fopen(path,"rb");IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(file && fread(&dos,1,sizeof(dos),file)==sizeof(dos) && !fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt));
    fclose(file);
    IMAGE_DATA_DIRECTORY d=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    *count=d.Size/(DWORD)sizeof(RUNTIME_FUNCTION);return (PRUNTIME_FUNCTION)(mapped+d.VirtualAddress);
}
/* A synthetic stack for a chain of image frames: 0xaecb60 at a block's resume, then each function's return address written where
 * X4's own unwind data reads it (zero above the last). The walk's second half then lists the image return addresses it finds. */
static uint64_t test_uirep_stack[0x4000];
static unsigned test_uirep_chain(unsigned char *mapped,unsigned resume,const uint32_t *returns,unsigned count,uint32_t *frames,unsigned capacity) {
    CONTEXT c,start;memset(test_uirep_stack,0,sizeof(test_uirep_stack));memset(&c,0,sizeof(c));
    c.Rsp=(DWORD64)(uintptr_t)(test_uirep_stack+0x200);c.Rip=(DWORD64)(uintptr_t)(mapped+resume);start=c;
    for(unsigned i=0;i<count;i++) {
        CONTEXT probe=c;DWORD64 module=0;PVOID handler=NULL;DWORD64 frame=0;
        PRUNTIME_FUNCTION fn=RtlLookupFunctionEntry(probe.Rip,&module,NULL);assert(fn && module==(DWORD64)(uintptr_t)mapped);
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,module,probe.Rip,fn,&probe,&handler,&frame,NULL);
        uint64_t *slot=(uint64_t*)(uintptr_t)(probe.Rsp-8);
        assert(slot>=test_uirep_stack && slot<test_uirep_stack+0x4000-0x200);
        *slot=(uint64_t)(uintptr_t)(mapped+returns[i]);
        probe=c;RtlVirtualUnwind(UNW_FLAG_NHANDLER,module,probe.Rip,fn,&probe,&handler,&frame,NULL);
        assert(probe.Rip==(DWORD64)(uintptr_t)(mapped+returns[i]));
        c=probe;
    }
    return uirep_route_unwind(&start,mapped,frames,capacity);
}
/* The pinned image, read-only apart from a private mapping: pins, prefixes, the slots relied on, a whole install. */
static void test_ui_replay_native_image(const char *path) {
    unsigned char *prior=image,*prior_base=pnat_base;uirep_fns saved_fn=uirep_fn;int saved_mode=pnat_mode;
    image=test_pmath_map(path);pnat_base=image;
    uirep_pins_checked=0;assert(uirep_pins());
    /* Ship vtable slot 1008 is what CanStartTravelMode calls; slots 4 and 5 of the component's +0x48 interface. */
    {void **ship_vt=(void**)(image+0x2b7feb8u);assert(ship_vt[UIREP_SHIP_CAN_TRAVEL]==(void*)(image+0x7e1c80u));}
    {void **assoc=(void**)(image+0x2a93178u);assert(assoc[4]==(void*)(image+0x3e73f0u) && assoc[5]==(void*)(image+0x3e7560u));}
    /* MD's two soft target actions: their vtable slot 3, the calls whose return addresses name MD's route, and vfunc 72 of
     * UI::CUIController that set_player_target calls. */
    {void **set_vt=(void**)(image+UIREP_MD_SET_VTABLE),**clear_vt=(void**)(image+UIREP_MD_CLEAR_VTABLE),**ui_vt=(void**)(image+0x2bc07f0u);
     static const unsigned char vf72[]={0xff,0x90,0x40,0x02,0x00,0x00};int32_t rel=0;
     assert(set_vt[3]==(void*)(image+UIREP_MD_SET_FN) && clear_vt[3]==(void*)(image+UIREP_MD_CLEAR_FN) && ui_vt[72]==(void*)(image+0xb3f7c0u));
     assert(!memcmp(image+UIREP_MD_SET_RETURN-6,vf72,6) && image[UIREP_MD_CLEAR_RETURN-5]==0xe8);
     memcpy(&rel,image+UIREP_MD_CLEAR_RETURN-4,4);assert(UIREP_MD_CLEAR_RETURN+(uint32_t)rel==0xae8080u);}
    /* The walk's second half on X4's own unwind data (registered for this mapping), from either block's resume: MD's set route
     * (0xaecb60 <- 0xaeb150 <- vfunc 72 0xb3f7c0 <- MD), its clear route (<- 0xae8460 <- 0xaebef0 <- 0xae8080 <- MD) and the UI's
     * own from missile1 (<- 0xaeb970 <- the target element's destructor 0xaeb4e0, tail-called by 0xad7440 <- 0xad7050). */
    {DWORD count=0;PRUNTIME_FUNCTION table=test_uirep_tables(path,image,&count);
     assert(count>80000 && RtlAddFunctionTable(table,count,(DWORD64)(uintptr_t)image));
     static const uint32_t md_set[]={0xaeb2b0u,0xb3f84bu,UIREP_MD_SET_RETURN},md_clear[]={0xae849au,0xaebf87u,0xae809fu,UIREP_MD_CLEAR_RETURN},
         element[]={0xaeba54u,0xaeb510u,0xad7064u};
     const struct {const uint32_t *returns;unsigned count;} chains[3]={{md_set,3},{md_clear,4},{element,3}};
     for(unsigned site=0;site<UIREP_SITES;site++) for(unsigned i=0;i<3;i++) {
         uint32_t frames[UIREP_ROUTE_DEPTH];
         unsigned n=test_uirep_chain(image,uirep_sites[site].resume,chains[i].returns,chains[i].count,frames,UIREP_ROUTE_DEPTH);
         assert(n==chains[i].count && !memcmp(frames,chains[i].returns,n*sizeof(*frames)));
     }
     assert(RtlDeleteFunctionTable(table));}
    /* CanStartTravelMode's own call and the stops' uses of the same fields (no other meaning of the offsets). */
    {static const unsigned char call[]={0x48,0xff,0xa0,0x80,0x1f,0x00,0x00};assert(!memcmp(image+0x177658u,call,sizeof(call)));}
    /* The exports that reach the two activity functions by a tail jump. */
    {int32_t rel=0;assert(image[0x1bc5a6u]==0xe9);memcpy(&rel,image+0x1bc5a7u,4);assert(0x1bc5abu+rel==0x9fd120u);
     assert(image[0x1bcb66u]==0xe9);memcpy(&rel,image+0x1bcb67u,4);assert(0x1bcb6bu+rel==0x9fd330u);}
    /* CreateOrder and CreateOrder2 end in CreateOrder3, RemoveOrder in RemoveOrder2. */
    {int32_t rel=0;assert(image[0x1f3d2cu]==0xe8);memcpy(&rel,image+0x1f3d2du,4);assert(0x1f3d31u+rel==0x1f3b30u);
     assert(image[0x1f3d09u]==0xe8);memcpy(&rel,image+0x1f3d0au,4);assert(0x1f3d0eu+rel==0x1f3b30u);
     assert(image[0x1feab9u]==0xe8);memcpy(&rel,image+0x1feabau,4);assert(0x1feabeu+rel==0x1fe9d0u);}
    /* Every patch on the real bytes, one CAS each, and back. */
    uirep_families=UIREP_FAMILIES;uirep_armed=1;uirep_fn=(uirep_fns){0};uirep_page=NULL;uirep_installed=0;uirep_stopped=0;
    assert(uirep_install() && uirep_intact());
    for(unsigned i=0;i<UIREP_HOOKS;i++) assert(image[uirep_hooks[i].rva]==0xe9);
    /* The site's resume points: the block ends the report names. */
    {static const unsigned char after_clear[]={0x4d,0x85,0xed},after_set[]={0x80,0x3d};
     assert(!memcmp(image+uirep_sites[UIREP_S_TARGET_CLEAR].resume,after_clear,3) && !memcmp(image+uirep_sites[UIREP_S_TARGET_SET].resume,after_set,2));}
    uirep_uninstall();assert(!uirep_installed && uirep_restored);
    uirep_pins_checked=0;assert(uirep_pins());
    image[0x3e73f0u+3]^=1;uirep_pins_checked=0;assert(!uirep_pins() && pin_failed_rva==0x3e73f0u);image[0x3e73f0u+3]^=1;uirep_pins_checked=0;
    image[UIREP_MD_SET_RETURN-4]^=1;assert(!uirep_pins() && pin_failed_rva==UIREP_MD_SET_FN);image[UIREP_MD_SET_RETURN-4]^=1;uirep_pins_checked=0;
    {void **clear_vt=(void**)(image+UIREP_MD_CLEAR_VTABLE),*keep=clear_vt[3];clear_vt[3]=(void*)(image+UIREP_MD_SET_FN);
     assert(!uirep_pins() && pin_failed_rva==UIREP_MD_CLEAR_VTABLE);clear_vt[3]=keep;uirep_pins_checked=0;assert(uirep_pins());}
    uirep_armed=0;uirep_families=0;
    if(uirep_page) {VirtualFree(uirep_page,0,MEM_RELEASE);uirep_page=NULL;}
    VirtualFree(image,0,MEM_RELEASE);
    image=prior;pnat_base=prior_base;uirep_fn=saved_fn;pnat_mode=saved_mode;
    printf("UI replay verified on the pinned image: pins, prefixes, vtable slots, patches\n");
}

/* Missions and conversations of slot 1 (lockstep_ui_missions.h) on stand-ins: cue references (resolved, gone, another cue
 * under the ID, not a cue), the capture the UI guard's hook asks for (each export's encoding and answer, slot 2 and a
 * family off left to the refusal, the dialog button read from a stand-in Lua stack), the section rules for both ops, the
 * apply through stand-in exports (gone, mismatch, decode), the prerequisites, the receipt and state rows, and the guard's
 * real hook on an executable region: captured in a local phase, X4's function at the apply and outside the local phases.
 * The image test: the pins, the event numbers and offsets the report reads, and the guard's table holding every entry. */
static __attribute__((aligned(16))) unsigned char test_uimis_cues[3][0x200],test_uimis_definition[3][0x200],test_uimis_module[3][0x80];
static unsigned char test_uimis_other[0x200];
static unsigned char *test_uimis_map_token;
static uint64_t test_uimis_calls[8][4];
static unsigned test_uimis_call_count;
/* An MSVC std::string at module+0x18: the name inline (shorter than 16), its size at +16, capacity 15 at +24. */
static void test_uimis_module_name(unsigned i,const char *name) {
    unsigned char *s=test_uimis_module[i]+UIMIS_MODULE_NAME;uint64_t size=strlen(name),cap=15;
    assert(size<16);memset(s,0,32);memcpy(s,name,size);memcpy(s+16,&size,8);memcpy(s+24,&cap,8);
}
static void test_uimis_make_cue(unsigned i,const char *module) {
    unsigned char *cue=test_uimis_cues[i];uintptr_t vt=(uintptr_t)(image+SCRIPT_RNG_CUE_VT),def=(uintptr_t)test_uimis_definition[i],mod=(uintptr_t)test_uimis_module[i];
    memset(cue,0,0x200);memcpy(cue,&vt,8);memcpy(cue+UIMIS_CUE_DEFINITION,&def,8);
    memset(test_uimis_definition[i],0,0x200);memcpy(test_uimis_definition[i]+UIMIS_DEFINITION_MODULE,&mod,8);
    test_uimis_module_name(i,module);
}
/* IDs 0x9001..0x9003 are the three cues, 0x9100 an object that is no cue, anything else unknown. */
static void *test_uimis_find(void *map,uint64_t id,unsigned flags) {
    assert(map==test_uimis_map_token);
    if(flags==4) return NULL;                       /* the component lookup: cues are no components */
    assert(flags==0);
    if(id>=0x9001 && id<=0x9003) return test_uimis_cues[id-0x9001];
    if(id==0x9100) return test_uimis_other;
    return NULL;
}
static uint64_t test_uimis_export(unsigned k,uint64_t a,uint64_t b,uint64_t c) {
    assert(test_uimis_call_count<8);
    test_uimis_calls[test_uimis_call_count][0]=k;test_uimis_calls[test_uimis_call_count][1]=a;test_uimis_calls[test_uimis_call_count][2]=b;
    test_uimis_calls[test_uimis_call_count][3]=c;++test_uimis_call_count;
    if(k==UIMIS_BRIEFING_START) assert(!strcmp((const char*)(uintptr_t)b,"x4lan_rt"));
    return k==UIMIS_ACTIVE?1:0;
}
static uint64_t test_uimis_abort(uint64_t a,uint64_t b,uint64_t c) {return test_uimis_export(UIMIS_ABORT,a,b,c);}
static uint64_t test_uimis_active(uint64_t a,uint64_t b,uint64_t c) {return test_uimis_export(UIMIS_ACTIVE,a,b,c);}
static uint64_t test_uimis_interact(uint64_t a,uint64_t b,uint64_t c) {return test_uimis_export(UIMIS_INTERACT,a,b,c);}
static uint64_t test_uimis_start(uint64_t a,uint64_t b,uint64_t c) {return test_uimis_export(UIMIS_BRIEFING_START,a,b,c);}
static uint64_t test_uimis_stop(uint64_t a,uint64_t b,uint64_t c) {return test_uimis_export(UIMIS_BRIEFING_STOP,a,b,c);}
/* A Lua stack as lua_tonumber answers it: argument 1 only. */
typedef struct {double button;int none;} test_uimis_lua_t;
static double test_uimis_number(void *L,int index) {const test_uimis_lua_t *s=(const test_uimis_lua_t*)L;return index==1 && !s->none?s->button:0.0;}
/* The guard's saved block for a call: r10, r9, r8, rdx, rcx, return address, home space, then the stack arguments. */
static void test_uimis_frame(uint64_t frame[12],uint64_t a,uint64_t b,uint64_t c) {memset(frame,0,12*8);frame[4]=a;frame[3]=b;frame[2]=c;}
static unsigned test_uimis_queued(unsigned at,unsigned char *op,unsigned char *data) {
    const uirep_event *e=&uirep_queue[(uirep_queue_head+at)%UIREP_QUEUE];*op=e->op;memcpy(data,e->data,e->length);return e->length;
}
static void test_ui_missions(void) {
    unsigned char *saved_base=pnat_base,*saved_image=image;uirep_fns saved_fn=uirep_fn;DWORD saved_ui=ui_thread;uint64_t saved_issued=stats.issued,saved_stop=pilot_stop;
    uint64_t saved_start=pstk_capture_start;unsigned saved_capture=pstk_capture_slot,saved_families=uirep_families,saved_groups=uiguard_groups,saved_mask=pstk_slot_mask;
    int saved_capturing=pstk_capturing,saved_rep=uirep_armed,saved_guard=uiguard_armed;
    unsigned char *region=VirtualAlloc(NULL,TEST_PNAT_REGION,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(region);
    pnat_base=image=region;ui_thread=GetCurrentThreadId();test_uirep_reset();
    test_uimis_map_token=region+0x100;{void *map=test_uimis_map_token;memcpy(region+UIREP_MAP,&map,8);}
    uirep_fn=(uirep_fns){0};uirep_fn.find=test_uimis_find;
    test_uimis_make_cue(0,"GM_Find_Object");test_uimis_make_cue(1,"X4LAN_UiReplay");test_uimis_make_cue(2,"Guidance");
    /* Cue references: the ID and its module's name. */
    {unsigned char ref[UIMIS_REF],other[UIMIS_REF];uint64_t id=0;
     assert(uimis_cue(0x9001)==test_uimis_cues[0] && !uimis_cue(0x9100) && !uimis_cue(0x9999) && !uimis_cue(0));
     assert(uimis_put_cue(ref,0x9001) && ref[0]==0x01 && ref[1]==0x90 && !ref[2] && (ref[8]&1u));
     assert(uimis_resolve(ref,&id)==1 && id==0x9001);
     assert(uimis_put_cue(other,0x9002) && memcmp(ref+8,other+8,4));               /* another ID, another module: another check */
     assert(!uimis_put_cue(other,0x9100) && !uimis_put_cue(other,0x9999));         /* no cue: no reference */
     test_uimis_module_name(0,"GM_Patrol");assert(uimis_resolve(ref,&id)==-1);      /* another cue under the ID: stop */
     test_uimis_module_name(0,"GM_Find_Object");assert(uimis_resolve(ref,&id)==1);
     ref[0]=0x99;assert(!uimis_resolve(ref,&id) && id==0x9099);ref[0]=0x01;        /* gone here */
     {uintptr_t zero=0;memcpy(test_uimis_cues[2]+UIMIS_CUE_DEFINITION,&zero,8);assert(!uimis_put_cue(other,0x9003));   /* unreadable: never guessed */
      test_uimis_make_cue(2,"Guidance");}}
    /* Arming: the families first, the pins, then the guard with the families' groups. */
    int saved_rid=rid_mode;pstk_capturing=1;pstk_capture_start=10;pilot_stop=1000;stats.issued=100;pstk_capture_slot=1;
    uimis_armed=0;uimis_pins_checked=1;uirep_armed=1;uirep_families=UIREP_FAMILY_EVENTS;
    assert(uimis_ready() && !uimis_arm(test_uimis_number) && !strcmp(uimis_why,"needs_family"));
    uirep_families=UIREP_FAMILY_EVENTS|UIREP_FAMILY_MISSIONS|UIREP_FAMILY_CONVERSATION;
    assert(!uimis_ready() && !strcmp(uimis_why,"ui_missions_not_armed"));
    uiguard_armed=0;assert(!uimis_arm(test_uimis_number) && !strcmp(uimis_why,"ui_missions_need_guard") && !uimis_armed);
    uiguard_armed=1;uiguard_groups=1u<<UIGUARD_G_MISSIONS;assert(!uimis_arm(test_uimis_number) && !strcmp(uimis_why,"ui_missions_need_guard_group"));
    uiguard_groups=UIGUARD_ALL_GROUPS;uimis_pins_checked=-1;assert(!uimis_arm(test_uimis_number) && !strncmp(uimis_why,"pins_ui_missions:",17));
    uimis_pins_checked=1;assert(uimis_arm(test_uimis_number) && uimis_armed && uimis_ready());
    {unsigned claimed=0;for(unsigned i=0;i<UIGUARD_MAX;i++) claimed+=uiguard_claim_by[i]==uimis_claim;assert(claimed==9);}   /* 5 exports, 4 C functions */
    /* The pass refuses a missing arming with the replay's stop. */
    {int keep=uimis_armed;uimis_armed=0;uirep_failed=0;uirep_why="-";uirep_installed=0;rid_mode=RID_SET;
     assert(!uirep_pass(5) && !strcmp(uirep_why,"ui_missions_not_armed"));uirep_failed=0;uirep_why="-";uirep_stage_name="armed";uimis_armed=keep;}
    uimis_fn[UIMIS_ABORT]=test_uimis_abort;uimis_fn[UIMIS_ACTIVE]=test_uimis_active;uimis_fn[UIMIS_INTERACT]=test_uimis_interact;
    uimis_fn[UIMIS_BRIEFING_START]=test_uimis_start;uimis_fn[UIMIS_BRIEFING_STOP]=test_uimis_stop;
    /* Capture: slot 1's calls become events (nothing runs), in call order. */
    uint64_t frame[12],ret=7;unsigned char op=0,data[UIREP_EVENT_MAX];
    test_uimis_frame(frame,0x9001,0,0);assert(uimis_guard("AbortMission",0,frame,&ret)==1 && ret==0 && uirep_queue_count==1);
    assert(test_uimis_queued(0,&op,data)==1+UIMIS_REF && op==UIREP_OP_MISSION && data[0]==UIMIS_ABORT && data[1]==0x01 && data[2]==0x90);
    test_uimis_frame(frame,0,0,0);assert(uimis_guard("SetActiveMission",0,frame,&ret)==1 && ret==1);   /* none: a zero reference, true */
    assert(test_uimis_queued(1,&op,data)==1+UIMIS_REF && data[0]==UIMIS_ACTIVE && !memcmp(data+1,"\0\0\0\0\0\0\0\0\0\0\0\0",UIMIS_REF));
    test_uimis_frame(frame,0x9002,(uint64_t)(uintptr_t)"x4lan_rt",0x101);assert(uimis_guard("StartBriefing",0,frame,&ret)==1 && ret==0);
    assert(test_uimis_queued(2,&op,data)==1+UIMIS_REF+2+8 && data[0]==UIMIS_BRIEFING_START && data[13]==1 && data[14]==8 && !memcmp(data+15,"x4lan_rt",8));
    test_uimis_frame(frame,0x9002,0x100,0);assert(uimis_guard("StopBriefing",0,frame,&ret)==1);       /* only the flag's byte counts */
    assert(test_uimis_queued(3,&op,data)==1+UIMIS_REF+1 && data[0]==UIMIS_BRIEFING_STOP && data[13]==0);
    test_uimis_frame(frame,0x9001,0,0);assert(uimis_guard("MissionBriefingInteractButtonClicked",0,frame,&ret)==1);
    /* The dialog: the button from the Lua stack, the core codec's encoding of one number; the others take no argument. */
    {test_uimis_lua_t L={3.0,0};test_uimis_frame(frame,(uint64_t)(uintptr_t)&L,0,0);
     assert(uimis_guard("SelectDialogOption",1,frame,&ret)==1 && ret==0);
     double three=3.0;assert(test_uimis_queued(5,&op,data)==11 && op==UIREP_OP_CONVERSATION && data[0]==UIMIS_C_SELECT && data[1]==1 && data[2]==3 && !memcmp(data+3,&three,8));
     assert(uimis_guard("CancelConversation",1,frame,&ret)==1 && test_uimis_queued(6,&op,data)==2 && data[0]==UIMIS_C_CANCEL && !data[1]);
     L.button=2.5;assert(!uimis_guard("SelectDialogOption",1,frame,&ret));L.button=300;assert(!uimis_guard("SelectDialogOption",1,frame,&ret));
     L.none=1;L.button=0;assert(uimis_guard("SelectDialogOption",1,frame,&ret)==1);   /* nil reads 0, as the stock function's own read */
     assert(!uimis_guard("StartConversationFromMenu",1,frame,&ret));                 /* component arguments: the Lua core's, else refused */
     assert(!uimis_guard("AddUITriggeredEvent",1,frame,&ret));}
    assert(uirep_queue_count==8);
    /* Not ours: unmapped IDs, a long texture, another slot, a family off, the replay off. Each is the guard's refusal then. */
    {unsigned before=uirep_queue_count;
     test_uimis_frame(frame,0x9100,0,0);assert(!uimis_guard("AbortMission",0,frame,&ret) && uimis_unmapped>=1);
     test_uimis_frame(frame,0x9002,(uint64_t)(uintptr_t)"a_render_target_name_longer_than_forty_eight_bytes_x",0);assert(!uimis_guard("StartBriefing",0,frame,&ret));
     test_uimis_frame(frame,0x9001,0,0);
     pstk_capture_slot=2;assert(!uimis_guard("AbortMission",0,frame,&ret));pstk_capture_slot=1;
     uirep_families=UIREP_FAMILY_CONVERSATION;assert(!uimis_guard("AbortMission",0,frame,&ret));
     uirep_families=UIREP_FAMILY_MISSIONS;{test_uimis_lua_t L={1.0,0};uint64_t f2[12];test_uimis_frame(f2,(uint64_t)(uintptr_t)&L,0,0);assert(!uimis_guard("SelectDialogOption",1,f2,&ret));}
     uirep_families=UIREP_FAMILY_EVENTS|UIREP_FAMILY_MISSIONS|UIREP_FAMILY_CONVERSATION;
     uirep_armed=0;assert(!uimis_guard("AbortMission",0,frame,&ret));uirep_armed=1;
     stats.issued=5;assert(!uimis_guard("AbortMission",0,frame,&ret));stats.issued=100;   /* before the window: the replay's not_live */
     assert(uirep_queue_count==before);}
    {char text[1024];assert(uimis_rows_text(text,sizeof(text)) && !strncmp(text,"100:72:AbortMission:36865,100:72:SetActiveMission:0,100:72:StartBriefing:36866,",79) &&
                            strstr(text,"100:73:SelectDialogOption:0,100:73:CancelConversation:0") && !uimis_rows_text(text,sizeof(text)));}
    /* The section: slot 1, the family, exact lengths. */
    {const char *why="";unsigned char abort_event[1+UIMIS_REF]={UIMIS_ABORT};
     assert(uimis_event_ok(1,UIREP_OP_MISSION,abort_event,sizeof(abort_event),&why));
     assert(!uimis_event_ok(2,UIREP_OP_MISSION,abort_event,sizeof(abort_event),&why) && !strcmp(why,"ui_missions"));
     assert(!uimis_event_ok(1,UIREP_OP_MISSION,abort_event,sizeof(abort_event)-1,&why));
     abort_event[0]=UIMIS_KINDS;assert(!uimis_event_ok(1,UIREP_OP_MISSION,abort_event,sizeof(abort_event),&why));abort_event[0]=0;
     assert(!uimis_event_ok(1,UIREP_OP_MISSION,abort_event,sizeof(abort_event),&why));abort_event[0]=UIMIS_ABORT;
     unsigned char start[1+UIMIS_REF+2+3]={UIMIS_BRIEFING_START};start[13]=1;start[14]=3;
     assert(uimis_event_ok(1,UIREP_OP_MISSION,start,sizeof(start),&why) && !uimis_event_ok(1,UIREP_OP_MISSION,start,sizeof(start)-1,&why));
     start[13]=2;assert(!uimis_event_ok(1,UIREP_OP_MISSION,start,sizeof(start),&why));start[13]=1;
     unsigned char conv[2]={UIMIS_C_CANCEL,0};
     assert(uimis_event_ok(1,UIREP_OP_CONVERSATION,conv,2,&why) && !uimis_event_ok(2,UIREP_OP_CONVERSATION,conv,2,&why) && !strcmp(why,"ui_conversation"));
     conv[0]=UIMIS_CONV_KINDS;assert(!uimis_event_ok(1,UIREP_OP_CONVERSATION,conv,2,&why));conv[0]=UIMIS_C_CANCEL;
     /* The hire scope's fee (lockstep_ui_missions.h UIMIS_C_HIRE_MONEY): slot 1 only, no other number above the globals. */
     conv[0]=UIMIS_C_HIRE_MONEY;assert(uimis_event_ok(1,UIREP_OP_CONVERSATION,conv,2,&why) && !uimis_event_ok(2,UIREP_OP_CONVERSATION,conv,2,&why));
     conv[0]=UIMIS_C_HIRE_MONEY-1;assert(!uimis_event_ok(1,UIREP_OP_CONVERSATION,conv,2,&why));conv[0]=UIMIS_C_HIRE_MONEY+1;
     assert(!uimis_event_ok(1,UIREP_OP_CONVERSATION,conv,2,&why));conv[0]=UIMIS_C_CANCEL;
     assert(!uimis_event_ok(1,0x4a,conv,2,&why) && !strcmp(why,"ui_op"));
     uirep_families=UIREP_FAMILY_CONVERSATION;assert(!uimis_event_ok(1,UIREP_OP_MISSION,abort_event,sizeof(abort_event),&why));
     uirep_families=UIREP_FAMILY_MISSIONS;assert(!uimis_event_ok(1,UIREP_OP_CONVERSATION,conv,2,&why));
     uirep_families=UIREP_FAMILY_EVENTS|UIREP_FAMILY_MISSIONS|UIREP_FAMILY_CONVERSATION;
     /* through the replay's own section check (its default branch) */
     uint16_t c16=0;unsigned char s[UIREP_HEAD+2+1+UIMIS_REF]={UIREP_VERSION,1,0,1,UIREP_OP_MISSION,1+UIMIS_REF,UIMIS_ABORT};
     assert(uirep_section_ok(1,s,sizeof(s),&c16,&why) && !uirep_section_ok(2,s,sizeof(s),&c16,&why) && !strcmp(why,"ui_missions"));}
    /* The apply: the exports with the arguments, gone and mismatch, every client the same. */
    {char what[128];unsigned char ev[1+UIMIS_REF+2+8];
     InterlockedExchange(&uirep_replaying,1);test_uimis_call_count=0;
     ev[0]=UIMIS_ABORT;assert(uimis_put_cue(ev+1,0x9001));
     assert(uimis_run(1,ev,1+UIMIS_REF,what,sizeof(what))==1 && !strcmp(what,"AbortMission(36865)") && test_uimis_call_count==1 && test_uimis_calls[0][1]==0x9001);
     ev[0]=UIMIS_ACTIVE;memset(ev+1,0,UIMIS_REF);
     assert(uimis_run(1,ev,1+UIMIS_REF,what,sizeof(what))==1 && !strcmp(what,"SetActiveMission(0)=1") && test_uimis_calls[1][1]==0);
     ev[0]=UIMIS_BRIEFING_START;assert(uimis_put_cue(ev+1,0x9002));ev[13]=1;ev[14]=8;memcpy(ev+15,"x4lan_rt",8);
     assert(uimis_run(1,ev,sizeof(ev),what,sizeof(what))==1 && !strcmp(what,"StartBriefing(36866,x4lan_rt,1)") && test_uimis_calls[2][3]==1);
     ev[0]=UIMIS_BRIEFING_STOP;ev[13]=0;
     assert(uimis_run(1,ev,1+UIMIS_REF+1,what,sizeof(what))==1 && !strcmp(what,"StopBriefing(36866,0)") && test_uimis_calls[3][2]==0);
     ev[0]=UIMIS_INTERACT;assert(uimis_run(1,ev,1+UIMIS_REF,what,sizeof(what))==1 && !strcmp(what,"MissionBriefingInteractButtonClicked(36866)"));
     LONG64 gone=uimis_gone;ev[0]=UIMIS_ABORT;ev[1]=0x77;   /* 0x9077: no cue on this client */
     assert(!uimis_run(1,ev,1+UIMIS_REF,what,sizeof(what)) && !strcmp(what,"AbortMission:gone") && uimis_gone==gone+1 && test_uimis_call_count==5);
     assert(uimis_put_cue(ev+1,0x9001));test_uimis_module_name(0,"GM_Patrol");
     assert(uimis_run(1,ev,1+UIMIS_REF,what,sizeof(what))==-1 && !strcmp(what,"ref_mismatch") && test_uimis_call_count==5);
     test_uimis_module_name(0,"GM_Find_Object");
     assert(uimis_run(2,ev,1+UIMIS_REF,what,sizeof(what))==-1 && !strcmp(what,"decode"));
     assert(uimis_run(1,ev,1+UIMIS_REF-1,what,sizeof(what))==-1);
     InterlockedExchange(&uirep_replaying,0);}
    /* Receipt and rows: the offers, the conversation manager; the mission count only in the state row. */
    {static __attribute__((aligned(16))) unsigned char missions[0x400],conversations[0x400],list[0x5d0*3];
     void *m=missions,*c=conversations;memcpy(region+UIMIS_MISSION_MANAGER,&m,8);memcpy(region+UIMIS_CONVERSATION_MANAGER,&c,8);
     uintptr_t b[2]={(uintptr_t)list,(uintptr_t)list+2*UIMIS_OFFER_SIZE};memcpy(missions+UIMIS_OFFERS,b,16);
     b[1]=(uintptr_t)list+3*UIMIS_MISSION_SIZE;memcpy(missions+UIMIS_MISSIONS,b,16);
     int state=4;uintptr_t current=0x1234;memcpy(conversations+UIMIS_CONV_STATE,&state,4);memcpy(conversations+UIMIS_CONV_CURRENT,&current,8);
     char text[256];
     assert(uimis_frame_text(text,sizeof(text)) && !strcmp(text,"UIMIS_1:o2/c4.1"));
     uirep_families=UIREP_FAMILY_MISSIONS;assert(uimis_frame_text(text,sizeof(text)) && !strcmp(text,"UIMIS_1:o2"));
     uirep_families=UIREP_FAMILY_CONVERSATION;state=1;current=0;memcpy(conversations+UIMIS_CONV_STATE,&state,4);memcpy(conversations+UIMIS_CONV_CURRENT,&current,8);
     assert(uimis_frame_text(text,sizeof(text)) && !strcmp(text,"UIMIS_1:/c1.0"));
     assert(uimis_state_text(text,sizeof(text)) && !strcmp(text,"missions=3 offers=2 conversation=1.0"));
     b[1]=(uintptr_t)list+5;memcpy(missions+UIMIS_OFFERS,b,16);assert(uimis_state_text(text,sizeof(text)) && strstr(text,"offers=-1"));   /* not whole entries */
     uirep_families=UIREP_FAMILY_EVENTS|UIREP_FAMILY_MISSIONS|UIREP_FAMILY_CONVERSATION;
     assert(uimis_status(text,sizeof(text)) && strstr(text,"ui_missions=armed") && strstr(text,"families=4800") && strstr(text,"captured=1/1/1/1/1"));}
    /* The guard's real hook: an export named AbortMission and a Lua C function named SelectDialogOption on an executable
     * region. In a local phase slot 1's calls are captured (X4's function does not run), at the apply and outside the
     * local phases X4's function runs; slot 2's call is the guard's refusal with its row. */
    {   const uiguard_entry *saved_tab=uiguard_tab;unsigned saved_count=uiguard_count;int saved_installed=uiguard_installed;
        static const unsigned char ffi_code[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x8d,0x41,0x01,0xc3};   /* mov [rsp+8],rbx; lea rax,[rcx+1]; ret */
        static const unsigned char lua_code[]={0x48,0x89,0x5c,0x24,0x08,0xb8,0x01,0,0,0,0xc3};      /* mov [rsp+8],rbx; mov eax,1; ret */
        test_uiguard_region=region;
        test_uiguard_entry(0,"AbortMission",0x2000,ffi_code,sizeof(ffi_code),5,UIGUARD_RET_ZERO,0,0,-1,0,0,UIGUARD_G_MISSIONS);
        test_uiguard_entry(1,"SelectDialogOption",0x2100,lua_code,sizeof(lua_code),5,UIGUARD_RET_ZERO,0,0,-1,0,0,UIGUARD_G_CONVERSATION);
        test_uiguard_entries[1].kind=UIGUARD_LUA;
        uiguard_tab=test_uiguard_entries;uiguard_count=2;uiguard_pins_checked=0;assert(uiguard_pins());
        assert(uimis_claim_entries() && uiguard_claim_by[0]==uimis_claim && uiguard_claim_by[1]==uimis_claim);   /* this table's entries */
        uiguard_armed=0;assert(uiguard_arm_using(UIGUARD_ALL_GROUPS));uiguard_installed=0;
        rid_probe_frame=0;uirep_stopped=0;assert(uiguard_pass(100) && uiguard_installed_count()==2);
        typedef uint64_t (*abort_fn)(uint64_t);typedef int (*lua_fn)(void*);
        abort_fn abort_entry=(abort_fn)(void*)(region+0x2000);lua_fn select_entry=(lua_fn)(void*)(region+0x2100);
        test_uimis_lua_t L={2.0,0};unsigned queued=uirep_queue_count;LONG64 refused=uiguard_refused;
        assert(abort_entry(0x9001)==0x9002 && select_entry(&L)==1);                                   /* outside the local phases: X4's */
        uirep_ui_depth=1;
        assert(abort_entry(0x9001)==0 && select_entry(&L)==0 && uirep_queue_count==queued+2 && uiguard_refused==refused);   /* captured */
        assert(abort_entry(0x9100)==0 && uiguard_refused==refused+1);                                 /* unmapped: the guard's refusal */
        pstk_capture_slot=2;assert(abort_entry(0x9001)==0 && select_entry(&L)==0 && uiguard_refused==refused+3 && uirep_queue_count==queued+2);
        pstk_capture_slot=1;
        InterlockedExchange(&uirep_replaying,1);assert(abort_entry(0x9001)==0x9002 && select_entry(&L)==1);InterlockedExchange(&uirep_replaying,0);
        uirep_ui_depth=0;
        {char text[1024];assert(uiguard_refusals_text(text,sizeof(text)) && !strcmp(text,"100:AbortMission:missions:refused:-,100:AbortMission:missions:refused:-,"
                                                                                  "100:SelectDialogOption:conversation:refused:-"));}
        uiguard_restore();assert(!uiguard_installed && region[0x2000]==0x48);uiguard_unclaim(uimis_claim);
        uiguard_tab=saved_tab;uiguard_count=saved_count;uiguard_pins_checked=0;uiguard_installed=saved_installed;
        if(uiguard_region) {VirtualFree(uiguard_region,0,MEM_RELEASE);uiguard_region=NULL;}
        uiguard_passed=uiguard_queries=uiguard_refused=uiguard_ring_lost=0;memset((void*)uiguard_refused_by,0,sizeof(uiguard_refused_by));
        uiguard_ring_head=uiguard_ring_tail=0;uiguard_stage="off";uiguard_why_text="-";
    }
    /* Everything off. */
    uimis_armed=0;uimis_pins_checked=0;uimis_why="-";uimis_row_head=uimis_row_tail=0;uimis_lua_number=NULL;memset(uimis_fn,0,sizeof(uimis_fn));
    uimis_applied=uimis_gone=uimis_unmapped=uimis_rows_lost=0;memset((void*)uimis_captured,0,sizeof(uimis_captured));memset((void*)uimis_conv_captured,0,sizeof(uimis_conv_captured));
    test_uirep_reset();
    VirtualFree(region,0,MEM_RELEASE);
    pnat_base=saved_base;image=saved_image;uirep_fn=saved_fn;ui_thread=saved_ui;stats.issued=saved_issued;pilot_stop=saved_stop;pstk_capture_start=saved_start;
    pstk_capture_slot=saved_capture;uirep_families=saved_families;uiguard_groups=saved_groups;pstk_slot_mask=saved_mask;pstk_capturing=saved_capturing;
    uirep_armed=saved_rep;uiguard_armed=saved_guard;rid_mode=saved_rid;
    printf("UI missions and conversations on stand-ins and the guard's hook verified\n");
}
/* The pinned image: the pins, what each export raises, the offsets read, and the guard's table holding every entry the
 * families capture (in its groups missions and conversation). */
static void test_ui_missions_native_image(const char *path) {
    unsigned char *prior=image,*prior_base=pnat_base;
    image=test_pmath_map(path);pnat_base=image;
    uimis_pins_checked=0;assert(uimis_pins());
    /* mov edx,<event>; ... call/jmp MD::Cue_RaiseCueEvent 0x111e370 */
    static const struct {unsigned at;unsigned short event;} raises[]={{0x175902u,0x63b},{0x1b4acc+0u,0x640},{0x1bbe5du,0x641},{0x1bcaaeu,0x642},{0x1122959u,0x63e}};
    for(unsigned i=0;i<sizeof(raises)/sizeof(*raises);i++) {
        uint32_t value=0;assert(image[raises[i].at]==0xba);memcpy(&value,image+raises[i].at+1,4);assert(value==raises[i].event);
    }
    /* StartBriefing reaches event_briefing_started through 0x11228d0 (call at 0x1bbe93). */
    {int32_t rel=0;assert(image[0x1bbe93u]==0xe8);memcpy(&rel,image+0x1bbe94u,4);assert(0x1bbe98u+rel==0x11228d0u);}
    /* StartConversationFromMenu: cmp dword [rsi+0x1a0],1 on the manager it loaded from 0x38ee230. */
    {static const unsigned char idle[]={0x83,0xbe,0xa0,0x01,0x00,0x00,0x01};assert(!memcmp(image+0x276b19u,idle,sizeof(idle)));
     int32_t rel=0;memcpy(&rel,image+0x276b04u,4);assert(0x276b08u+rel==UIMIS_CONVERSATION_MANAGER);}
    /* The mission manager: 0x3d0 bytes, stored at 0x6cf1658. */
    {int32_t rel=0;uint32_t size=0;memcpy(&size,image+0x12334c9u,4);assert(size==0x3d0);memcpy(&rel,image+0x12334e8u,4);assert(0x12334ecu+rel==UIMIS_MISSION_MANAGER);}
    /* Every entry of the families is in the guard's table, in its group. */
    for(unsigned k=1;k<UIMIS_KINDS;k++) {
        unsigned found=0;
        for(unsigned i=0;i<UIGUARD_ENTRIES;i++) if(!strcmp(uiguard_table[i].name,uimis_exports[k].name)) {
            assert(uiguard_table[i].kind==UIGUARD_FFI && uiguard_table[i].group==UIGUARD_G_MISSIONS && uiguard_table[i].rva==uimis_exports[k].rva);found=1;}
        assert(found);
    }
    for(unsigned k=1;k<UIMIS_CONV_KINDS;k++) {
        unsigned found=0;
        for(unsigned i=0;i<UIGUARD_ENTRIES;i++) if(!strcmp(uiguard_table[i].name,uimis_conv_names[k])) {
            assert(uiguard_table[i].kind==UIGUARD_LUA && uiguard_table[i].group==UIGUARD_G_CONVERSATION);found=1;}
        assert(found);
    }
    image[0x1bbcb0u+0x40]^=1;uimis_pins_checked=0;assert(!uimis_pins() && pin_failed_rva==0x1bbcb0u);image[0x1bbcb0u+0x40]^=1;uimis_pins_checked=0;
    VirtualFree(image,0,MEM_RELEASE);
    image=prior;pnat_base=prior_base;
    printf("UI missions verified on the pinned image: pins, event numbers, manager offsets, guard entries\n");
}

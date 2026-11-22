/* lockstep_md_media.h on synthetic memory: the start in the UI's place, the owner, what the UI's four functions are
 * answered, the timeout, the table's upkeep, the refusals and the restore. */
#define TMDV_NOTES 6u
static unsigned char test_mdv_manager[0x100],test_mdv_notes[0x60],test_mdv_sentinel[0x18];
static unsigned char test_mdv_note[TMDV_NOTES][0x90],test_mdv_desc[TMDV_NOTES][0xb0],test_mdv_scene[TMDV_NOTES][0x210],test_mdv_node[TMDV_NOTES][0x18];
static int test_mdv_alive[TMDV_NOTES],test_mdv_next_scene,test_mdv_pin_answer;
static unsigned test_mdv_original_ran,test_mdv_starts,test_mdv_stops,test_mdv_removes,test_mdv_make_next;
static struct {void *manager,*key,*first,*second;int type,id;uintptr_t a7,mode,target,a10,a11,a12,a13;} test_mdv_start_args;
static double test_mdv_time;
static int test_mdv_pin(void) {return test_mdv_pin_answer;}
static int test_mdv_now(double *now) {*now=test_mdv_time;return 1;}
static void *test_mdv_find(int id) {
    for(unsigned i=0;i<TMDV_NOTES;i++) {int at=0;memcpy(&at,test_mdv_note[i]+8,4);if(test_mdv_alive[i] && at==id) return test_mdv_note[i];}
    return NULL;
}
static void *test_mdv_target(uint64_t *out,const void *name,int zero) {
    const struct {const char *text;size_t length;} *n=name;
    assert(!zero && n->length==strlen(mdv_target_name) && !memcmp(n->text,mdv_target_name,n->length));
    *out=0x500001234ull;return out;
}
/* The manager's list +0x30 as the image has it: node[0] next, node[1] previous, node[2] the cutscene. */
static void test_mdv_link(unsigned char *node,unsigned char *scene) {
    unsigned char *last=NULL;memcpy(&last,test_mdv_sentinel+8,8);
    unsigned char *sentinel=test_mdv_sentinel;
    memcpy(node,&sentinel,8);memcpy(node+8,&last,8);memcpy(node+0x10,&scene,8);
    memcpy(last,&node,8);memcpy(test_mdv_sentinel+8,&node,8);
}
static void *test_mdv_start(void *manager,void *key,int type,int id,void *first,void *second,uintptr_t a7,uintptr_t mode,uintptr_t target,
        uintptr_t a10,uintptr_t a11,uintptr_t a12,uintptr_t a13) {
    test_mdv_start_args.manager=manager;test_mdv_start_args.key=key;test_mdv_start_args.type=type;test_mdv_start_args.id=id;
    test_mdv_start_args.first=first;test_mdv_start_args.second=second;test_mdv_start_args.a7=a7;test_mdv_start_args.mode=mode;
    test_mdv_start_args.target=target;test_mdv_start_args.a10=a10;test_mdv_start_args.a11=a11;test_mdv_start_args.a12=a12;test_mdv_start_args.a13=a13;
    ++test_mdv_starts;
    if(test_mdv_next_scene<0) return NULL;   /* a key the manager does not know */
    assert(test_mdv_next_scene<(int)TMDV_NOTES);
    unsigned char *scene=test_mdv_scene[test_mdv_next_scene];const int cid=0x700+test_mdv_next_scene;
    memset(scene,0,sizeof(test_mdv_scene[0]));memcpy(scene+8,&cid,4);
    test_mdv_link(test_mdv_node[test_mdv_next_scene],scene);++test_mdv_next_scene;
    return scene;
}
static void test_mdv_stop(void *manager,int id) {
    unsigned char *node=NULL;memcpy(&node,test_mdv_sentinel,8);
    assert(manager==(void*)test_mdv_manager);++test_mdv_stops;
    for(;node!=test_mdv_sentinel;memcpy(&node,node,8)) {
        unsigned char *scene=NULL,*next=NULL,*previous=NULL,*running=NULL;int at=0;
        memcpy(&scene,node+0x10,8);memcpy(&at,scene+8,4);
        if(at!=id) continue;
        scene[0x200]=1;{const int reason=1;memcpy(scene+0x204,&reason,4);}
        memcpy(&running,test_mdv_manager+0xa8,8);if(running==scene) memset(test_mdv_manager+0xa8,0,8);
        memcpy(&next,node,8);memcpy(&previous,node+8,8);memcpy(previous,&next,8);memcpy(next+8,&previous,8);
        return;
    }
}
/* The notification goes; its destructor stops its descriptor's cutscene. */
static void test_mdv_remove(void *notes,int *id) {
    assert(notes==(void*)test_mdv_notes);++test_mdv_removes;
    for(unsigned i=0;i<TMDV_NOTES;i++) {
        int at=0,cid=0;memcpy(&at,test_mdv_note[i]+8,4);
        if(!test_mdv_alive[i] || at!=*id) continue;
        test_mdv_alive[i]=0;memcpy(&cid,test_mdv_desc[i]+0xa0,4);if(cid) test_mdv_stop(test_mdv_manager,cid);
    }
}
static void *test_mdv_make_fn(void *self) {return self;}
static unsigned test_mdv_adds;
static void test_mdv_add(void *note,void *person,void *third,uintptr_t fourth,uintptr_t fifth) {(void)note;(void)person;(void)third;(void)fourth;(void)fifth;++test_mdv_adds;}
/* The interaction handle table and the UI's answer: one descriptor of the test's, its ID at +8. */
static unsigned char *test_mdv_ask;
static int test_mdv_ask_id,test_mdv_raised_id;
static unsigned test_mdv_raised;
static void *test_mdv_interaction(int id) {return test_mdv_ask && id==test_mdv_ask_id?(void*)test_mdv_ask:NULL;}
static void *test_mdv_raise(void *notes,void *text,int id) {assert(notes==(void*)test_mdv_notes && text);++test_mdv_raised;test_mdv_raised_id=id;return text;}
/* The action: the target monitor branch makes the notification test_mdv_make_next-1 (0: another branch, none). */
static void test_mdv_original(void *node,void *exec,void *frame) {
    (void)node;(void)exec;(void)frame;++test_mdv_original_ran;
    if(test_mdv_make_next) assert(mdv_make(test_mdv_note[test_mdv_make_next-1])==(void*)test_mdv_note[test_mdv_make_next-1]);
}
static void test_mdv_plant(unsigned i,int id,float seconds,int interactive) {
    unsigned char *desc=test_mdv_desc[i],*interaction=interactive?test_mdv_note[i]+0x88:NULL;
    memset(test_mdv_note[i],0,sizeof(test_mdv_note[0]));memset(desc,0,sizeof(test_mdv_desc[0]));
    memcpy(test_mdv_note[i]+8,&id,4);memcpy(test_mdv_note[i]+0x1c,&seconds,4);memcpy(test_mdv_note[i]+0x78,&desc,8);
    memcpy(test_mdv_note[i]+0x80,&interaction,8);memcpy(desc+0xa4,&id,4);test_mdv_alive[i]=1;
}
static int test_mdv_int(const unsigned char *at) {int v=0;memcpy(&v,at,4);return v;}
/* The UI's four Lua functions: X4's own (counted, answering 7 results) and a Lua state of one argument. */
static double test_mdv_lua_number,test_mdv_lua_pushed;
static void *test_mdv_lua_pointer;
static unsigned test_mdv_lua_originals[MDV_UI];
static double test_mdv_lua_tonumber(lua_State *L,int index) {(void)L;return index==1?test_mdv_lua_number:0.0;}
static void *test_mdv_lua_touserdata(lua_State *L,int index) {(void)L;return index==1?test_mdv_lua_pointer:NULL;}
static void test_mdv_lua_pushnumber(lua_State *L,double value) {(void)L;test_mdv_lua_pushed=value;}
#define TEST_MDV_LUA(i) static int test_mdv_lua_original_##i(lua_State *L) {(void)L;++test_mdv_lua_originals[i];return 7;}
TEST_MDV_LUA(0) TEST_MDV_LUA(1) TEST_MDV_LUA(2) TEST_MDV_LUA(3)
static void test_md_media(void) {
    const uint64_t issued_was=stats.issued;const DWORD ui_was=ui_thread;const uint32_t active_was=stats.active,slot_was=stats.slot;
    const unsigned scope_was=pstk_scope_slot,depth_was=mpp_depth,depth1_was=mpp_depth1;
    unsigned char manager_was[8],notes_was[8],slot_bytes_was[8];
    memcpy(manager_was,image+MDV_MANAGER,8);memcpy(notes_was,image+MDV_NOTES,8);memcpy(slot_bytes_was,image+MDV_ACTION_VTABLE+MDV_SLOT,8);
    {unsigned char *p=test_mdv_manager;memcpy(image+MDV_MANAGER,&p,8);p=test_mdv_notes;memcpy(image+MDV_NOTES,&p,8);
     p=image+MDV_ACTION;memcpy(image+MDV_ACTION_VTABLE+MDV_SLOT,&p,8);}
    memset(test_mdv_manager,0,sizeof(test_mdv_manager));memset(test_mdv_notes,0,sizeof(test_mdv_notes));
    {unsigned char *sentinel=test_mdv_sentinel;memcpy(test_mdv_sentinel,&sentinel,8);memcpy(test_mdv_sentinel+8,&sentinel,8);
     memcpy(test_mdv_manager+0x30,&sentinel,8);test_mdv_manager[0xa0]=1;const int mode=7;memcpy(test_mdv_manager+0xc0,&mode,4);}
    memset(test_mdv_alive,0,sizeof(test_mdv_alive));test_mdv_next_scene=0;test_mdv_original_ran=test_mdv_starts=test_mdv_stops=test_mdv_removes=0;
    mdv_armed=0;mdv_pin=test_mdv_pin;test_mdv_pin_answer=1;mdv_slot_installed=mdv_site_installed=0;
    ui_thread=GetCurrentThreadId();stats.active=1;stats.issued=0;stats.slot=1;pstk_scope_slot=0;mpp_depth=0;mpp_depth1=0;
    /* Arming: after the gate's start, before the first tick, on the frame thread, with the pins. */
    stats.active=0;assert(!mdv_arm() && !strcmp(mdv_why,"not_started"));stats.active=1;
    stats.issued=1;assert(!mdv_arm() && !strcmp(mdv_why,"late_or_thread"));stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!mdv_arm());ui_thread=GetCurrentThreadId();
    test_mdv_pin_answer=0;assert(!mdv_arm() && !strcmp(mdv_why,"pins") && !mdv_armed);test_mdv_pin_answer=1;
    assert(mdv_arm() && mdv_armed && mdv_arm() && mdv_fn.action==(mdv_action_fn)(image+MDV_ACTION) && mdv_fn.start==(mdv_start_fn)(image+MDV_START));
    mdv_fn.action=test_mdv_original;mdv_fn.make=test_mdv_make_fn;mdv_fn.start=test_mdv_start;mdv_fn.stop=test_mdv_stop;mdv_fn.find=test_mdv_find;
    mdv_fn.remove=test_mdv_remove;mdv_fn.target=test_mdv_target;mdv_fn.now=test_mdv_now;test_mdv_time=100.0;
    assert(mdv_fn.interaction==(mdv_interaction_fn)(image+MDV_INTERACTION) && mdv_fn.raise==(mdv_raise_fn)(image+MDV_RAISE));
    mdv_fn.interaction=test_mdv_interaction;mdv_fn.raise=test_mdv_raise;test_mdv_ask=NULL;test_mdv_raised=0;
    /* Without the slots' near page nothing is installed (a session of one); the slot itself swaps both ways. */
    {unsigned char *const page=pslot_page;pslot_page=NULL;assert(mdv_frame()==0 && !mdv_install_tried && !mdv_slot_installed);pslot_page=page;}
    mdv_install_tried=1;   /* the call site is the site helper's own test's; nothing is installed here */
    {void *now=NULL;assert(mdv_slot_swap(image+MDV_ACTION,(void*)mdv_action));memcpy(&now,image+MDV_ACTION_VTABLE+MDV_SLOT,8);assert(now==(void*)mdv_action);
     assert(!mdv_slot_swap(image+MDV_ACTION,(void*)mdv_action) && mdv_slot_swap((void*)mdv_action,image+MDV_ACTION));}
    /* An action of another branch (full screen, a menu's render target): X4's own, nothing noted. */
    test_mdv_make_next=0;mdv_action(NULL,NULL,NULL);assert(test_mdv_original_ran==1 && mdv_actions==1 && !mdv_started && !test_mdv_starts);
    /* Nobody's frame: the notification's cutscene starts at once, as the UI's StartCutscene starts it; everybody's. */
    test_mdv_plant(0,0x41,0.0f,0);test_mdv_make_next=1;mdv_action(NULL,NULL,NULL);
    assert(mdv_started==1 && test_mdv_starts==1 && !test_mdv_stops && test_mdv_start_args.manager==(void*)test_mdv_manager &&
        test_mdv_start_args.key==(void*)test_mdv_desc[0] && test_mdv_start_args.type==2 && test_mdv_start_args.id==0x41 &&
        test_mdv_start_args.first==(void*)(test_mdv_desc[0]+0x20) && test_mdv_start_args.second==(void*)(test_mdv_desc[0]+0x60) &&
        !test_mdv_start_args.a7 && test_mdv_start_args.mode==7 && test_mdv_start_args.target==0x500001234ull && test_mdv_start_args.a10==1 &&
        !test_mdv_start_args.a11 && !test_mdv_start_args.a12 && !test_mdv_start_args.a13);
    {unsigned char *running=NULL;memcpy(&running,test_mdv_manager+0xa8,8);assert(running==test_mdv_scene[0] && test_mdv_int(test_mdv_desc[0]+0xa0)==0x700);}
    assert(mdv[0].used && mdv[0].id==0x41 && mdv[0].cutscene==0x700 && !mdv[0].owner && mdv[0].end==0.0);
    assert(mdv_ui_cutscene(test_mdv_desc[0])==0x700 && mdv_ui_cutscene(test_mdv_desc[1])==-1 && mdv_ui_cutscene(NULL)==-1 && mdv_ui_starts==1);
    assert(mdv_ui_keep(0x700)==1 && !mdv_ui_keep(0x701) && !mdv_ui_keep(0) && mdv_ui_note(0x41,0)==1 && mdv_ui_note(0x41,1)==1 && !mdv_ui_note(0x42,0));
    assert(mdv_ui_stops==1 && mdv_ui_releases==1 && !mdv_ui_hidden && mdv_frame()==1);
    /* The same through the UI's functions: an ID or a descriptor of ours is answered here, everything else by X4. */
    {   double (*const number_was)(lua_State *,int)=p_lua_tonumber;void *(*const pointer_was)(lua_State *,int)=p_lua_touserdata;
        void (*const push_was)(lua_State *,double)=p_lua_pushnumber;
        p_lua_tonumber=test_mdv_lua_tonumber;p_lua_touserdata=test_mdv_lua_touserdata;p_lua_pushnumber=test_mdv_lua_pushnumber;
        mdv_ui_original[0]=test_mdv_lua_original_0;mdv_ui_original[1]=test_mdv_lua_original_1;mdv_ui_original[2]=test_mdv_lua_original_2;
        mdv_ui_original[3]=test_mdv_lua_original_3;memset(test_mdv_lua_originals,0,sizeof(test_mdv_lua_originals));
        test_mdv_lua_number=0x41;assert(mdv_lua_priority(NULL)==7 && test_mdv_lua_originals[0]==1);          /* everybody's: X4 answers its priority */
        assert(mdv_lua_release(NULL)==0 && !test_mdv_lua_originals[3]);                                       /* ours: not removed by a UI */
        test_mdv_lua_number=0x99;assert(mdv_lua_release(NULL)==7 && test_mdv_lua_originals[3]==1 && mdv_lua_stop(NULL)==7 && test_mdv_lua_originals[2]==1);
        test_mdv_lua_number=0x700;assert(mdv_lua_stop(NULL)==0 && test_mdv_lua_originals[2]==1);             /* ours: not stopped by a UI */
        test_mdv_lua_pointer=test_mdv_desc[0];test_mdv_lua_pushed=0;
        assert(mdv_lua_start(NULL)==1 && test_mdv_lua_pushed==(double)0x700 && !test_mdv_lua_originals[1]);   /* ours: the running one's ID */
        test_mdv_lua_pointer=test_mdv_desc[1];assert(mdv_lua_start(NULL)==7 && test_mdv_lua_originals[1]==1); /* a menu's own descriptor */
        test_mdv_lua_pointer=NULL;test_mdv_lua_number=1.5;assert(mdv_lua_start(NULL)==7 && mdv_lua_priority(NULL)==7);   /* not an ID, no descriptor */
        stats.slot=2;test_mdv_lua_number=0x41;assert(mdv_lua_priority(NULL)==7);stats.slot=1;                /* nobody's: every client's monitor */
        ui_thread=GetCurrentThreadId()+1;test_mdv_lua_number=0x700;assert(mdv_lua_stop(NULL)==7);ui_thread=GetCurrentThreadId();   /* another thread */
        p_lua_tonumber=number_was;p_lua_touserdata=pointer_was;p_lua_pushnumber=push_was;
    }
    {const LONG64 starts=mdv_ui_starts,stops=mdv_ui_stops,releases=mdv_ui_releases;assert(starts==2 && stops==2 && releases==2);
     mdv_ui_starts=1;mdv_ui_stops=1;mdv_ui_releases=1;}   /* the counts as before this block */
    /* A frame of slot 2's own cue: the manager's one notification cutscene ends with reason 3 and slot 2's starts;
     * client 1's monitor is not told of it, client 2's is; the first one runs no more and starts nothing again. */
    pstk_scope_slot=2;mpp_depth=1;test_mdv_plant(1,0x42,0.0f,0);test_mdv_make_next=2;mdv_action(NULL,NULL,NULL);pstk_scope_slot=0;mpp_depth=0;
    assert(mdv_started==2 && mdv_replaced==1 && test_mdv_stops==1 && test_mdv_scene[0][0x200]==1 && test_mdv_int(test_mdv_scene[0]+0x204)==3);
    {unsigned char *running=NULL;memcpy(&running,test_mdv_manager+0xa8,8);assert(running==test_mdv_scene[1] && test_mdv_int(test_mdv_desc[1]+0xa0)==0x701);}
    assert(mdv[1].used && mdv[1].owner==2 && mdv_ui_note(0x42,0)==3 && mdv_ui_hidden==1 && mdv_ui_note(0x41,0)==1);
    {   double (*const number_was)(lua_State *,int)=p_lua_tonumber;const unsigned asked=test_mdv_lua_originals[0];
        p_lua_tonumber=test_mdv_lua_tonumber;test_mdv_lua_number=0x42;
        assert(mdv_lua_priority(NULL)==0 && test_mdv_lua_originals[0]==asked && mdv_ui_hidden==2);            /* slot 2's on client 1: no answer */
        stats.slot=2;assert(mdv_lua_priority(NULL)==7 && test_mdv_lua_originals[0]==asked+1);stats.slot=1;    /* on client 2: X4's */
        p_lua_tonumber=number_was;mdv_ui_hidden=1;
    }
    stats.slot=2;assert(mdv_ui_note(0x42,0)==1 && mdv_ui_cutscene(test_mdv_desc[1])==0x701 && mdv_ui_hidden==1);stats.slot=1;
    assert(mdv_ui_cutscene(test_mdv_desc[0])==0 && mdv_ui_none==1 && test_mdv_starts==2);
    /* A frame of slot 1's own cue: client 2's monitor is not told. */
    mpp_depth1=1;test_mdv_plant(2,0x43,0.0f,0);test_mdv_make_next=3;mdv_action(NULL,NULL,NULL);mpp_depth1=0;
    assert(mdv[2].used && mdv[2].owner==1 && mdv_replaced==2 && mdv_ui_note(0x43,0)==1);
    stats.slot=2;assert(mdv_ui_note(0x43,0)==3);stats.slot=1;
    /* The script's stop_cutscene removed a notification: its entry leaves the table and nothing is ours any more. */
    {int id=0x42;test_mdv_remove(test_mdv_notes,&id);}
    assert(mdv_frame()==2 && !mdv[1].used && mdv_gone==1 && !mdv_ui_note(0x42,0) && mdv_ui_cutscene(test_mdv_desc[1])==-1 && !mdv_ui_keep(0x701));
    /* A notification with an interaction (a fleet ship asks its player for orders) is the simulation's too: its picture
     * starts in the action on every client, in nobody's frame it is everybody's, and it ends with the script's stop. */
    test_mdv_plant(3,0x44,0.0f,1);test_mdv_make_next=4;mdv_action(NULL,NULL,NULL);
    assert(mdv_interactive==1 && mdv_started==4 && mdv_replaced==3 && mdv_ui_note(0x44,0)==1 && mdv_ui_cutscene(test_mdv_desc[3])==0x703);
    stats.slot=2;assert(mdv_ui_note(0x44,0)==1);stats.slot=1;
    /* Its question: an interaction ID is this client's own, so the answer travels as the notification's place among the
     * started ones (the fourth here) and every client raises the event for its own ID of that question. */
    {static unsigned char ask[0x20];const int aid=0x5001;unsigned char *p=ask;
     assert(!mdv_question() && !mdv_interaction_seq(0x5001));          /* the planted descriptor is not in the handle table */
     memset(ask,0,sizeof(ask));memcpy(ask+8,&aid,4);memcpy(test_mdv_note[3]+0x80,&p,8);test_mdv_ask=ask;test_mdv_ask_id=aid;
     assert(mdv_interaction_seq(0x5001)==4 && !mdv_interaction_seq(0x5002) && !mdv_interaction_seq(0) && mdv_question()==0x5001);
     assert(mdv_answer(4)==1 && test_mdv_raised==1 && test_mdv_raised_id==0x5001 && mdv_answers==1);
     assert(!mdv_answer(3) && !mdv_answer(9) && !mdv_answer(0) && test_mdv_raised==1 && mdv_answers_gone==2);   /* no question; no such one */
     stats.active=0;assert(!mdv_interaction_seq(0x5001) && !mdv_answer(4) && !mdv_question());stats.active=1;}
    {int id=0x44;test_mdv_remove(test_mdv_notes,&id);}
    assert(!mdv_answer(4) && !mdv_question());                           /* the notification is gone: so is its question */
    assert(mdv_frame()==2 && mdv_gone==2 && !mdv_ui_note(0x44,0) && mdv_ui_cutscene(test_mdv_desc[3])==-1);
    /* A key the manager does not know starts nothing and is not noted. */
    test_mdv_plant(3,0x45,0.0f,0);test_mdv_next_scene=-1;mdv_action(NULL,NULL,NULL);test_mdv_next_scene=3;
    {unsigned char *running=test_mdv_manager;memcpy(&running,test_mdv_manager+0xa8,8);assert(!running && mdv_failed==1 && mdv_started==4 && !mdv_ui_note(0x45,0));}
    /* A timeout counts game time from the start: at its end the notification is removed (its cutscene stops). */
    test_mdv_time=200.0;test_mdv_plant(4,0x46,10.0f,0);test_mdv_make_next=5;{const unsigned removes=test_mdv_removes;mdv_action(NULL,NULL,NULL);
    assert(mdv_started==5 && mdv[1].used && mdv[1].id==0x46 && mdv[1].end==210.0);
    test_mdv_time=209.9;assert(mdv_frame()==3 && test_mdv_removes==removes && mdv_ui_cutscene(test_mdv_desc[4])==0x703);
    test_mdv_time=210.0;assert(mdv_frame()==2 && test_mdv_removes==removes+1 && mdv_timeouts==1 && !test_mdv_alive[4] && test_mdv_scene[3][0x200]==1 &&
        test_mdv_int(test_mdv_scene[3]+0x204)==1 && mdv_ui_cutscene(test_mdv_desc[4])==-1);}
    /* The ID the UI says it displays is not one of ours (AbortCurrentNotification removes that one); another's stays. */
    {int shown=0x43;memcpy(test_mdv_notes+0x50,&shown,4);(void)mdv_frame();assert(!test_mdv_int(test_mdv_notes+0x50) && mdv_ui_shown==1);
     shown=0x99;memcpy(test_mdv_notes+0x50,&shown,4);(void)mdv_frame();assert(test_mdv_int(test_mdv_notes+0x50)==0x99 && mdv_ui_shown==1);
     memset(test_mdv_notes+0x50,0,4);}
    /* Off the frame thread and after the session: X4's own action, nothing answered as ours. */
    {const unsigned ran=test_mdv_original_ran;const LONG64 actions=mdv_actions;test_mdv_make_next=0;
     ui_thread=GetCurrentThreadId()+1;mdv_action(NULL,NULL,NULL);assert(test_mdv_original_ran==ran+1 && mdv_actions==actions && !mdv_ui_note(0x43,0) && !mdv_frame());
     ui_thread=GetCurrentThreadId();stats.active=0;mdv_action(NULL,NULL,NULL);assert(test_mdv_original_ran==ran+2 && mdv_ui_cutscene(test_mdv_desc[2])==-1);stats.active=1;}
    /* A full table leaves the next one to X4's own way. */
    {mdv_t kept[MDV_MAX];memcpy(kept,mdv,sizeof(kept));
     for(unsigned i=0;i<MDV_MAX;i++) {mdv[i]=kept[0];}   /* sixteen entries of a live notification */
     test_mdv_plant(5,0x47,0.0f,0);test_mdv_make_next=6;{const unsigned starts=test_mdv_starts;mdv_action(NULL,NULL,NULL);assert(mdv_full==1 && test_mdv_starts==starts);}
     memcpy(mdv,kept,sizeof(kept));test_mdv_make_next=0;}
    {char status[400];assert(mdv_status(status,sizeof(status))>0 && strstr(status,"armed=1 installed=0/0/0 live=2/1 ") && strstr(status," started=5 replaced=3 failed=1 interactive=1 full=1 timeouts=1 "));}
    /* A conversation's picture (header, talk): X4's add, then the start; owned by the conversation's owner (hidden on the
     * other clients), without an owner by the frame's slot (here none: every client). */
    {const int armed_was=cvo_armed;const unsigned owner_was=cvo_owner;const LONG64 started=mdv_started;
     mdv_fn.add=test_mdv_add;test_mdv_adds=0;cvo_armed=1;cvo_owner=2;
     test_mdv_plant(5,0x48,0.0f,0);mdv_conv_add(test_mdv_note[5],NULL,NULL,0,0);
     assert(test_mdv_adds==1 && mdv_conv_adds==1 && mdv_started==started+1 && mdv_ui_note(0x48,0)==3);   /* client 1: player 2's */
     stats.slot=2;assert(mdv_ui_note(0x48,0)==1);stats.slot=1;
     {int id=0x48;test_mdv_remove(test_mdv_notes,&id);}(void)mdv_frame();
     cvo_owner=0;test_mdv_plant(5,0x49,0.0f,0);mdv_conv_add(test_mdv_note[5],NULL,NULL,0,0);
     assert(test_mdv_adds==2 && mdv_started==started+2 && mdv_ui_note(0x49,0)==1);
     {int id=0x49;test_mdv_remove(test_mdv_notes,&id);}(void)mdv_frame();
     ui_thread=GetCurrentThreadId()+1;mdv_conv_add(test_mdv_note[5],NULL,NULL,0,0);assert(test_mdv_adds==3 && mdv_conv_adds==2);   /* not live: X4's add alone */
     ui_thread=GetCurrentThreadId();cvo_armed=armed_was;cvo_owner=owner_was;
     {char status[400];assert(mdv_status(status,sizeof(status))>0 && strstr(status," conv=0/2"));}}
    /* The end: the table is empty and nothing is answered. */
    mdv_restore();assert(!mdv_armed && !mdv[0].used && !mdv_ui_note(0x41,0) && !strcmp(mdv_why,"-"));
    mdv_pin=mdv_pins;mdv_why="-";memset(&mdv_fn,0,sizeof(mdv_fn));mdv_install_tried=0;memset(mdv_ui_original,0,sizeof(mdv_ui_original));
    memcpy(image+MDV_MANAGER,manager_was,8);memcpy(image+MDV_NOTES,notes_was,8);memcpy(image+MDV_ACTION_VTABLE+MDV_SLOT,slot_bytes_was,8);
    pstk_scope_slot=scope_was;mpp_depth=depth_was;mpp_depth1=depth1_was;stats.issued=issued_was;ui_thread=ui_was;stats.active=active_was;stats.slot=slot_was;
    printf("md media: a notification's cutscene started in the UI's place, owner, UI answers, timeout, upkeep, refusals, a conversation's picture PASS\n");
}
/* The real image: every pinned routine, the vtable slot, the constructor's call inside one 16-byte block. */
static void test_md_media_native_image(const char *path) {
    unsigned char *prior=image;int32_t rel=0;
    image=test_pmath_map(path);
    mdv_pins_checked=0;assert(mdv_pins());mdv_pins_checked=0;
    assert(image[MDV_MAKE_SITE]==0xe8 && (MDV_MAKE_SITE&~15u)==((MDV_MAKE_SITE+4u)&~15u));
    memcpy(&rel,image+MDV_MAKE_SITE+1,4);assert(MDV_MAKE_SITE+5u+(uint32_t)rel==MDV_MAKE);
    /* what the start here repeats of the UI's StartCutscene: the manager's start called with the descriptor's own ID */
    memcpy(&rel,image+0xb130c4u+1,4);assert(image[0xb130c4u]==0xe8 && 0xb130c4u+5u+(uint32_t)rel==MDV_START);
    {static const unsigned char id_field[7]={0x45,0x8b,0xa5,0xa4,0x00,0x00,0x00};assert(!memcmp(image+0xb12facu,id_field,7));}   /* mov r12d,[r13+0xa4] */
    {static const unsigned char dtor_id[6]={0x8b,0x93,0xa0,0x00,0x00,0x00};assert(!memcmp(image+0x156c78u,dtor_id,6));}        /* mov edx,[rbx+0xa0] */
    /* a conversation's add: Move pinned, its call of the add inside one 16-byte block, the notification in RCX (mov rcx,r12) */
    assert(hash_ok(image+MDV_CONV_MOVE,MDV_CONV_MOVE_SIZE,MDV_CONV_MOVE_SHA));
    assert(image[MDV_CONV_SITE]==0xe8 && (MDV_CONV_SITE&~15u)==((MDV_CONV_SITE+4u)&~15u));
    memcpy(&rel,image+MDV_CONV_SITE+1,4);assert(MDV_CONV_SITE+5u+(uint32_t)rel==MDV_ADD);
    {static const unsigned char rcx_note[3]={0x49,0x8b,0xcc};assert(!memcmp(image+MDV_CONV_SITE-3u,rcx_note,3));}
    image=prior;
    for(unsigned i=0;i<MDV_UI;i++) assert((mdv_ui[i].rva&15u)==0);   /* an entry's six bytes inside one 16-byte block */
    printf("pinned native image md media verified (the action, the UI's four functions, the manager's start and stop, the notification's routines)\n");
}

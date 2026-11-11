/* native_gate_path_transforms (lockstep_path_transforms.h). Unit: the call decision with stand-ins for 0x3dde10 and
 * 0x3db8a0 over fake components (thread class, TLS view, flag, ancestor chain, spaces only, NavigationController links, the pass
 * time, a faulting composition, 'observe', rows and their cap, the tick check, status). Native image: the real arm on
 * the relocated X4.exe (pins, the five swaps, the view for other modules' pins, step chain refused), a call entered
 * through each stub running the real GetTransformRelativeTo over fake components at the pass time, then tamper,
 * restore and refusals. Included by test_lockstep_gate.c. */
typedef struct {void *vtable;uint64_t id;unsigned char pad[0x60];unsigned char *parent;unsigned char pad2[0x200-0x78];unsigned char *controller;
    unsigned char tail[0x40];} test_ptf_component;
_Static_assert(offsetof(test_ptf_component,id)==8 && offsetof(test_ptf_component,parent)==PTF_PARENT && offsetof(test_ptf_component,controller)==PTF_CONTROLLER,
    "test component layout");
typedef struct {void *vtable;unsigned char pad[0x20];void *owner;} test_ptf_controller;
static void *test_ptf_vt_object[0x2a0],*test_ptf_vt_space[0x2a0];
static float test_ptf_offset(const void *self);
static double test_ptf_seen_time;static unsigned test_ptf_locals;
/* Slot 0x1440 of the fake components: translate by the component's offset (rotation untouched). */
static void test_ptf_local(void *self,float *m,double time,uint64_t r9,unsigned char flag) {
    (void)r9;(void)flag;test_ptf_seen_time=time;++test_ptf_locals;
    const float d=test_ptf_offset(self);m[0]+=d;m[1]+=2.0f*d;m[2]+=3.0f*d;
}
static char test_ptf_is_object(void *self,int klass) {(void)self;return klass==PTF_CLASS_OBJECT;}
static char test_ptf_is_space(void *self,int klass) {(void)self;(void)klass;return 0;}
static test_ptf_component test_ptf_parts[6];
static float test_ptf_offset(const void *self) {
    for(unsigned i=0;i<6;i++) if(self==&test_ptf_parts[i]) return (float)(i+1)*10.0f;
    return 0.0f;
}
static void test_ptf_vtables(void) {
    test_ptf_vt_object[PTF_CLASS_SLOT/8]=(void*)test_ptf_is_object;test_ptf_vt_space[PTF_CLASS_SLOT/8]=(void*)test_ptf_is_space;
    test_ptf_vt_object[0x1440/8]=(void*)test_ptf_local;test_ptf_vt_space[0x1440/8]=(void*)test_ptf_local;
}
/* parts: 0 sector (space), 1 zone (space, child of 0), 2 wreck (object with a plain controller, child of 1),
 * 3 agent ship (object with a NavigationController, child of 1), 4 part of the wreck (space class, child of 2),
 * 5 another zone of the sector. */
static test_ptf_controller test_ptf_wreck_ctrl,test_ptf_nav_ctrl;
static void test_ptf_world(void) {
    memset(test_ptf_parts,0,sizeof(test_ptf_parts));test_ptf_vtables();
    for(unsigned i=0;i<6;i++) {test_ptf_parts[i].vtable=test_ptf_vt_space;test_ptf_parts[i].id=0x5000+i;}
    test_ptf_parts[1].parent=(unsigned char*)&test_ptf_parts[0];test_ptf_parts[5].parent=(unsigned char*)&test_ptf_parts[0];
    test_ptf_parts[2].parent=(unsigned char*)&test_ptf_parts[1];test_ptf_parts[3].parent=(unsigned char*)&test_ptf_parts[1];
    test_ptf_parts[4].parent=(unsigned char*)&test_ptf_parts[2];
    test_ptf_parts[2].vtable=test_ptf_vt_object;test_ptf_parts[3].vtable=test_ptf_vt_object;
    test_ptf_wreck_ctrl.vtable=image+0x2c3f5f0u;test_ptf_wreck_ctrl.owner=&test_ptf_parts[2];
    test_ptf_nav_ctrl.vtable=image+PTF_NAV_VTABLE;test_ptf_nav_ctrl.owner=&test_ptf_parts[3];
    test_ptf_parts[2].controller=(unsigned char*)&test_ptf_wreck_ctrl;test_ptf_parts[3].controller=(unsigned char*)&test_ptf_nav_ctrl;
}
static unsigned test_ptf_native_calls;static float test_ptf_native_value;
static void *test_ptf_native(void *a,float *m,void *b,uint64_t flag) {
    (void)a;(void)b;(void)flag;++test_ptf_native_calls;m[0]=test_ptf_native_value;return (void*)0x77;
}
static unsigned test_ptf_gtr_calls,test_ptf_gtr_fault;static void *test_ptf_gtr_a,*test_ptf_gtr_b;static double test_ptf_gtr_time;
static void test_ptf_gtr(void *a,float *m,double time,void *b,unsigned char flag) {
    assert(!flag);++test_ptf_gtr_calls;test_ptf_gtr_a=a;test_ptf_gtr_b=b;test_ptf_gtr_time=time;
    if(test_ptf_gtr_fault) {m[0]=-1.0f;*(volatile int*)(uintptr_t)8=1;}
    m[0]+=100.0f;
}
/* X4's walk for a chain without an Agent: one call of its end site (site 1); the second row stands for an Agent's pose. */
static unsigned test_ptf_walks,test_ptf_walk_fault;static void *test_ptf_walk_from,*test_ptf_walk_to;
static const unsigned char *test_ptf_walk_tls,*test_ptf_walk_manager;static unsigned char *test_ptf_walk_page;
static void test_ptf_walk(float *m,void *velocity,void **from,void **to) {
    assert(!velocity && ptf_dock_depth==1);++test_ptf_walks;test_ptf_walk_from=*from;test_ptf_walk_to=*to;
    if(test_ptf_walk_fault) *(volatile int*)(uintptr_t)8=1;
    if(test_ptf_walk_page) ((ptf_hier_fn)(void*)(test_ptf_walk_page+0x10u))(*from,m,*to,0);
    else ptf_call_using(1,*from,m,*to,0,PATH_THREAD_PASS,test_ptf_walk_tls,test_ptf_native,test_ptf_gtr,test_ptf_walk_manager);
    m[5]+=1000.0f;
}
static void test_ptf_counts_reset(void) {
    ptf_dock_agents=0;
    memset((void*)ptf_site_calls,0,sizeof(ptf_site_calls));
    ptf_outside=ptf_trivial=ptf_spaces=ptf_diverted=ptf_objects=ptf_nav=ptf_not_ancestor=ptf_differs=ptf_failures=ptf_rows=ptf_dropped=ptf_tick_rows=0;
}
static void test_path_transforms(void) {
    int saved_active=stats.active,saved_phase=boot_phase,owned_identities=!world_identities;uint64_t saved_issued=stats.issued;
    if(owned_identities) world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(0x5002,0x176ad5) && world_identity_record(0x5001,0x13e75f));
    test_ptf_world();test_ptf_counts_reset();test_ptf_native_calls=test_ptf_gtr_calls=test_ptf_gtr_fault=0;
    static ptf_row_t ring[PTF_RING];ptf_row_t *saved_ring=ptf_ring;ptf_ring=ring;memset(ring,0,sizeof(ring));
    static unsigned char tls[0x400],manager[0x400];memset(tls,0,sizeof(tls));memset(manager,0,sizeof(manager));
    const double pass=82.25;memcpy(manager+PTF_PASS_TIME,&pass,8);
    unsigned char *wreck=(unsigned char*)&test_ptf_parts[2],*zone=(unsigned char*)&test_ptf_parts[1],*sector=(unsigned char*)&test_ptf_parts[0];
    unsigned char *ship=(unsigned char*)&test_ptf_parts[3],*part=(unsigned char*)&test_ptf_parts[4],*other=(unsigned char*)&test_ptf_parts[5];
    float m[16]={1,2,3,0, 1,0,0,0, 0,1,0,0, 0,0,1,0};
    #define PTF_CALL(site,a,b,flag,cls) ptf_call_using(site,a,m,b,flag,cls,tls,test_ptf_native,test_ptf_gtr,manager)
    stats.active=1;boot_phase=3;stats.issued=2468;ptf_mode=PTF_SIM;
    /* Not armed: native. */
    InterlockedExchange(&ptf_active,0);
    test_ptf_native_value=5.0f;assert(PTF_CALL(1,wreck,zone,0,PATH_THREAD_PASS)==(void*)0x77 && m[0]==5.0f && ptf_outside==1 && !test_ptf_gtr_calls);
    InterlockedExchange(&ptf_active,1);
    /* A batch job in a joined pass: the composition at the pass time, a row for the wreck. */
    m[0]=1.0f;assert(PTF_CALL(1,wreck,zone,0,PATH_THREAD_PASS)==m && m[0]==101.0f && test_ptf_gtr_calls==1 && test_ptf_native_calls==1);
    assert(test_ptf_gtr_a==wreck && test_ptf_gtr_b==zone && test_ptf_gtr_time==pass && ptf_diverted==1 && ptf_objects==1 && ptf_rows==1);
    char out[1024];
    assert(ptf_row_text(0,out,sizeof(out))>0 && strstr(out,"index=0 tick=2468 site=walk_end mode=sim a=0000000000176ad5/0/0 b=000000000013e75f/0/0 object=0000000000176ad5/0/0 ctrl=02c3f5f0 time=40549000") &&
           strstr(out,"differs=0 sim=0000ca42") && !strstr(out,"native="));
    assert(!ptf_row_text(1,out,sizeof(out)));
    /* Two links up (a part of the wreck to the sector): the object found on the way. */
    assert(PTF_CALL(0,part,sector,0,PATH_THREAD_PASS)==m && test_ptf_gtr_calls==2 && ptf_objects==2 && ptf_rows==2);
    /* Spaces only: native (static, the same everywhere), no row. */
    assert(PTF_CALL(2,zone,sector,0,PATH_THREAD_PASS)==(void*)0x77 && test_ptf_gtr_calls==2 && ptf_spaces==1 && ptf_diverted==2 && ptf_objects==2 && ptf_rows==2);
    /* Other threads, a thread with a view, no TLS: native. */
    const unsigned classes[3]={PATH_THREAD_MOVEWORKER,PATH_THREAD_MAIN,PATH_THREAD_OTHER};
    for(unsigned i=0;i<3;i++) assert(PTF_CALL(1,wreck,zone,0,classes[i])==(void*)0x77);
    *(uint32_t*)(tls+PTF_TLS_VIEW)=1;assert(PTF_CALL(1,wreck,zone,0,PATH_THREAD_PASS)==(void*)0x77);*(uint32_t*)(tls+PTF_TLS_VIEW)=0;
    assert(ptf_call_using(1,wreck,m,zone,0,PATH_THREAD_PASS,NULL,test_ptf_native,test_ptf_gtr,manager)==(void*)0x77);
    boot_phase=2;assert(PTF_CALL(1,wreck,zone,0,PATH_THREAD_PASS)==(void*)0x77);boot_phase=3;
    assert(ptf_outside==7 && test_ptf_gtr_calls==2);
    /* A flag, a == b: native. */
    assert(PTF_CALL(1,wreck,zone,1,PATH_THREAD_PASS)==(void*)0x77 && PTF_CALL(1,zone,zone,0,PATH_THREAD_PASS)==(void*)0x77 && ptf_trivial==2);
    /* b not an ancestor (a sibling zone, the wreck's own part): native. */
    assert(PTF_CALL(2,zone,other,0,PATH_THREAD_PASS)==(void*)0x77 && PTF_CALL(1,wreck,part,0,PATH_THREAD_PASS)==(void*)0x77 && ptf_not_ancestor==2);
    /* An agent's ship on the way: native (its slot 0x1440 refreshes the controller). Its back pointer must match. */
    assert(PTF_CALL(1,ship,zone,0,PATH_THREAD_PASS)==(void*)0x77 && ptf_nav==1);
    test_ptf_nav_ctrl.owner=wreck;assert(PTF_CALL(1,ship,zone,0,PATH_THREAD_PASS)==m && ptf_nav==1 && ptf_objects==3);test_ptf_nav_ctrl.owner=ship;
    assert(test_ptf_gtr_calls==3 && ptf_diverted==3 && !ptf_failures);
    /* No pass time, a non-finite one, a faulting composition: native, counted as failures, the matrix intact for it. */
    assert(ptf_call_using(1,wreck,m,zone,0,PATH_THREAD_PASS,tls,test_ptf_native,test_ptf_gtr,NULL)==(void*)0x77 && ptf_failures==1 && !strcmp(ptf_stage,"pass_time"));
    const double nan=NAN;memcpy(manager+PTF_PASS_TIME,&nan,8);assert(PTF_CALL(1,wreck,zone,0,PATH_THREAD_PASS)==(void*)0x77 && ptf_failures==2);
    memcpy(manager+PTF_PASS_TIME,&pass,8);
    test_ptf_gtr_fault=1;m[0]=9.0f;test_ptf_native_value=6.0f;
    assert(PTF_CALL(1,wreck,zone,0,PATH_THREAD_PASS)==(void*)0x77 && m[0]==6.0f && ptf_failures==3 && !strcmp(ptf_stage,"compose"));
    test_ptf_gtr_fault=0;
    /* 'observe': both computed, the native result kept, differences counted and rowed (spaces too). */
    ptf_mode=PTF_OBSERVE;LONG64 rows=ptf_rows;test_ptf_native_value=1.0f;m[0]=1.0f;
    assert(PTF_CALL(2,zone,sector,0,PATH_THREAD_PASS)==(void*)0x77 && m[0]==1.0f && ptf_differs==1 && ptf_rows==rows+1);
    assert(ptf_row_text((uint64_t)rows,out,sizeof(out))>0 && strstr(out,"site=point_space mode=observe") && strstr(out,"object=- ctrl=00000000") &&
           strstr(out,"differs=1 sim=0000ca42") && strstr(out," native=0000803f"));
    test_ptf_native_value=101.0f;m[0]=1.0f;
    assert(PTF_CALL(2,zone,sector,0,PATH_THREAD_PASS)==(void*)0x77 && ptf_differs==1 && ptf_rows==rows+1); /* equal: no row */
    ptf_mode=PTF_SIM;
    /* The per-tick cap, reset by the tick check; failures fail it. */
    ptf_tick_rows=PTF_ROWS_PER_TICK;rows=ptf_rows;assert(PTF_CALL(1,wreck,zone,0,PATH_THREAD_PASS)==m && ptf_rows==rows && ptf_dropped==1);
    ptf_installed=0;assert(!ptf_check() && !ptf_tick_rows);ptf_failures=0;assert(ptf_check());
    assert(PTF_CALL(1,wreck,zone,0,PATH_THREAD_PASS)==m && ptf_rows==rows+1);
    /* Formation window: every formation call of its ticks is a row with its outcome and result; other sites and ticks not. */
    assert(!ptf_window(0,5) && !ptf_window(9,5) && !ptf_window(1,700) && ptf_window(2460,2470));
    rows=ptf_rows;test_ptf_native_value=7.0f;
    assert(PTF_CALL(PTF_SITE_FORMATION,zone,other,0,PATH_THREAD_PASS)==(void*)0x77 && ptf_rows==rows+1);
    assert(ptf_row_text((uint64_t)rows,out,sizeof(out))>0 && strstr(out,"site=formation ") && strstr(out," a=000000000013e75f/0/0 ") &&
           strstr(out," sim=0000e040") && strstr(out," outcome=not_ancestor chain="));
    assert(PTF_CALL(PTF_SITE_FORMATION,wreck,zone,0,PATH_THREAD_MOVEWORKER)==(void*)0x77 && ptf_rows==rows+2);
    assert(ptf_row_text((uint64_t)rows+1,out,sizeof(out))>0 && strstr(out," outcome=outside"));
    assert(PTF_CALL(PTF_SITE_FORMATION,wreck,zone,0,PATH_THREAD_PASS)==m && ptf_rows==rows+4); /* the object row and the window row */
    assert(ptf_row_text((uint64_t)rows+3,out,sizeof(out))>0 && strstr(out," outcome=diverted") && ptf_row_text((uint64_t)rows+2,out,sizeof(out))>0 && !strstr(out,"outcome="));
    assert(PTF_CALL(2,zone,other,0,PATH_THREAD_PASS)==(void*)0x77 && ptf_rows==rows+4);
    stats.issued=2471;assert(PTF_CALL(PTF_SITE_FORMATION,zone,other,0,PATH_THREAD_PASS)==(void*)0x77 && ptf_rows==rows+4);stats.issued=2468;
    /* The steering step's dock sites run X4's walk; inside it the walk's sites divert spans of spaces and parts too (outside
     * they keep stock's bits there). A chain with an Agent's object is walked as well (dock_agents). Every call in the window
     * is a row. Not an ancestor, another thread: native, no walk. A faulting walk: native, a failure. */
    {   const LONG64 spaces0=ptf_spaces,diverted0=ptf_diverted,rows0=ptf_rows,walk_end0=ptf_site_calls[1],failures0=ptf_failures,na0=ptf_not_ancestor;
        const unsigned gtr0=test_ptf_gtr_calls;
        ptf_walk_native=test_ptf_walk;test_ptf_walk_tls=tls;test_ptf_walk_manager=manager;test_ptf_walks=0;
        m[0]=1.0f;m[5]=0.0f;assert(PTF_CALL(PTF_SITE_DOCK,zone,sector,0,PATH_THREAD_PASS)==m && m[0]==101.0f && m[5]==1000.0f);
        assert(test_ptf_walks==1 && test_ptf_walk_from==zone && test_ptf_walk_to==sector && test_ptf_gtr_a==zone && test_ptf_gtr_b==sector && !ptf_dock_depth);
        assert(ptf_spaces==spaces0 && ptf_diverted==diverted0+2 && test_ptf_gtr_calls==gtr0+1 && ptf_rows==rows0+1 && !ptf_dock_agents);
        assert(ptf_row_text((uint64_t)rows0,out,sizeof(out))>0 && strstr(out,"site=dock_point ") && strstr(out," outcome=diverted chain="));
        assert(PTF_CALL(PTF_SITE_DOCK+1,part,sector,0,PATH_THREAD_PASS)==m && ptf_diverted==diverted0+4 && ptf_rows==rows0+4); /* its object: the end site's row, the dock's, the window's */
        assert(ptf_row_text((uint64_t)rows0+3,out,sizeof(out))>0 && strstr(out,"site=dock_frame ") && strstr(out," outcome=diverted"));
        test_ptf_native_value=7.0f;m[5]=0.0f;
        assert(PTF_CALL(PTF_SITE_DOCK,ship,zone,0,PATH_THREAD_PASS)==m && m[5]==1000.0f && test_ptf_walks==3 && ptf_dock_agents==1);
        assert(ptf_row_text((uint64_t)ptf_rows-1,out,sizeof(out))>0 && strstr(out,"site=dock_point ") && strstr(out," outcome=diverted"));
        assert(PTF_CALL(PTF_SITE_DOCK,ship,other,0,PATH_THREAD_PASS)==(void*)0x77 && PTF_CALL(PTF_SITE_DOCK,zone,other,0,PATH_THREAD_PASS)==(void*)0x77 &&
               ptf_not_ancestor==na0+2 && test_ptf_walks==3);
        assert(PTF_CALL(PTF_SITE_DOCK,zone,sector,0,PATH_THREAD_MOVEWORKER)==(void*)0x77 && test_ptf_walks==3);
        test_ptf_walk_fault=1;m[0]=9.0f;test_ptf_native_value=6.0f;
        assert(PTF_CALL(PTF_SITE_DOCK,zone,sector,0,PATH_THREAD_PASS)==(void*)0x77 && m[0]==6.0f && ptf_failures==failures0+1 && !strcmp(ptf_stage,"dock_walk") && !ptf_dock_depth);
        test_ptf_walk_fault=0;ptf_failures=failures0;
        /* 'observe': the walk's result against the cache's, the native result kept; the walk's own sites compose all the same. */
        {   const LONG64 differs0=ptf_differs;const unsigned gtr1=test_ptf_gtr_calls;
            ptf_mode=PTF_OBSERVE;test_ptf_native_value=3.0f;m[0]=1.0f;
            assert(PTF_CALL(PTF_SITE_DOCK,zone,sector,0,PATH_THREAD_PASS)==(void*)0x77 && m[0]==3.0f && ptf_differs==differs0+1 && test_ptf_gtr_calls==gtr1+1);
            assert(ptf_row_text((uint64_t)ptf_rows-2,out,sizeof(out))>0 && strstr(out,"site=dock_point mode=observe") && strstr(out,"differs=1 sim=0000ca42") && strstr(out," native=00004040"));
            ptf_mode=PTF_SIM;
        }
        assert(InterlockedCompareExchange64(&ptf_site_calls[PTF_SITE_DOCK],0,0)==7 && InterlockedCompareExchange64(&ptf_site_calls[PTF_SITE_DOCK+1],0,0)==1 &&
               ptf_site_calls[1]==walk_end0+4);
        InterlockedExchange64(&ptf_site_calls[1],walk_end0);ptf_walk_native=NULL;
    }
    ptf_installed=1;assert(!ptf_window(1,2) && ptf_window(2460,2470));ptf_installed=0;ptf_window_from=ptf_window_to=0;
    rows=ptf_rows-1;
    /* Rows evicted from the ring say so. */
    ptf_rows=PTF_RING+5;assert(ptf_row_text(2,out,sizeof(out))>0 && !strcmp(out,"index=2 evicted=1"));ptf_rows=rows+1;
    assert(ptf_status(out,sizeof(out))>0 && strstr(out,"mode=sim installed=0 ") && strstr(out," spaces=1 diverted=") && strstr(out," time=4054900000000000 sites=walk:1,walk_end:") &&
           strstr(out,"point_space:5,agent_space:0,formation:4,dock_point:7,dock_frame:1") && strstr(out," dock_agents=1 time="));
    assert(!ptf_status(out,40));
    #undef PTF_CALL
    InterlockedExchange(&ptf_active,0);ptf_mode=PTF_OFF;ptf_ring=saved_ring;ptf_stage="idle";test_ptf_counts_reset();ptf_last_time=0;
    stats.active=saved_active;boot_phase=saved_phase;stats.issued=saved_issued;
    if(owned_identities) {VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;}
    puts("path transforms: pass jobs compose at the pass time; other threads, views, flags, non-ancestors, agents native; failures, observe, rows, cap, check, status PASS");
}
static void test_path_transforms_native_image(const char *path) {
    unsigned char *prior=image;DWORD saved_ui=ui_thread,saved_main=main_thread;uint32_t saved_active=stats.active;uint64_t saved_issued=stats.issued;
    int saved_boot=boot_mode,saved_phase=boot_phase,saved_path=path_installed;unsigned saved_options=path_options;LONG saved_open=path_pass_open;
    image=test_pmath_map(path);
    assert(ptf_pinned());
    for(unsigned i=0;i<PTF_SITES;i++) assert(ptf_site_state(i)==0);
    unsigned char walk[PTF_WALK_SIZE],point[0x18];memcpy(walk,image+PTF_WALK,sizeof(walk));memcpy(point,image+0xe8d5dc,sizeof(point));
    /* Refused: not the serial join, not before the first admitted frame, a mode. */
    stats.active=1;stats.issued=0;boot_mode=1;boot_phase=0;ui_thread=GetCurrentThreadId();path_installed=1;path_options=0;
    assert(!ptf_arm(PTF_SIM) && !strcmp(ptf_stage,"state"));path_options=1;
    stats.issued=1;assert(!ptf_arm(PTF_SIM));stats.issued=0;assert(!ptf_arm(PTF_OFF) && !ptf_arm(7));
    /* A changed pin and a foreign site refuse before anything is written. */
    image[0xeac06f+4]^=1;assert(!ptf_arm(PTF_SIM) && !strcmp(ptf_stage,"pins"));image[0xeac06f+4]^=1;
    image[ptf_sites[2].site+1]^=1;assert(!ptf_arm(PTF_SIM) && !strcmp(ptf_stage,"sites"));image[ptf_sites[2].site+1]^=1;
    assert(!ptf_installed && !ptf_done);
    /* Armed: the seven calls go to the stubs, other readers see the file's bytes. */
    assert(ptf_arm(PTF_SIM) && ptf_installed && ptf_done==(1u<<PTF_SITES)-1 && ptf_owned() && ptf_check() && !strcmp(ptf_stage,"armed"));
    assert(ptf_arm(PTF_SIM) && !ptf_arm(PTF_OBSERVE)); /* a reloaded UI state: only the same mode */
    for(unsigned i=0;i<PTF_SITES;i++) {
        assert(ptf_site_state(i)==1);int32_t d;memcpy(&d,image+ptf_sites[i].site+1,4);
        assert(image+ptf_sites[i].site+5+d==ptf_page+0x10u*i);
    }
    assert(memcmp(image+PTF_WALK,walk,sizeof(walk)) && hash_ok(image+PTF_WALK,PTF_WALK_SIZE,PTF_WALK_SHA) && ptf_pinned());
    /* The dock sites lie in the steering step, which the join, the request and the solver pin whole: still the file's bytes. */
    assert(ptf_sites[PTF_SITE_DOCK].site>0xec7200u && ptf_sites[PTF_SITE_DOCK+1].site<0xec7200u+10109u &&
           hash_ok(image+0xec7200u,10109,"bff420d07fb655ff195560b395af5decc0ab5b2ecf9691481e578eb3282eb796"));
    {int32_t d;memcpy(&d,image+0xec7cffu,4);assert(image+0xec7d03u+d==ptf_page+0x10u*PTF_SITE_DOCK);}
    {unsigned char view[PTF_WALK_SIZE];assert(read_local(image+PTF_WALK,view,sizeof(view)) && !memcmp(view,walk,sizeof(view)));}
    {unsigned char view[0x18];assert(read_local(image+0xe8d5dc,view,sizeof(view)) && !memcmp(view,point,sizeof(view)));}
    /* FR20's pt_obj stub cannot take 0xe8d5ea while this is in. */
    assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins") && !st_chain_installed);
    /* Each stub, entered as a job thread in a joined pass, runs the real GetTransformRelativeTo over the fake
     * components at the pass time: the zone's child (fast path) and the two links of the wreck's part. */
    test_ptf_world();test_ptf_counts_reset();
    static unsigned char tls[0x400],manager[0x400];memset(tls,0,sizeof(tls));memset(manager,0,sizeof(manager));
    const double pass=82.25;memcpy(manager+PTF_PASS_TIME,&pass,8);
    unsigned char *saved_manager;memcpy(&saved_manager,image+PTF_MANAGER,8);{unsigned char *mp=manager;memcpy(image+PTF_MANAGER,&mp,8);}
    ptf_test_tls=tls;ptf_hier_native=test_ptf_native;ptf_gtr_native=NULL;
    ptf_walk_native=test_ptf_walk;test_ptf_walk_page=ptf_page; /* the dock stubs: the walk stand-in calls the end site's stub */
    stats.active=1;boot_phase=3;stats.issued=2468;ui_thread=GetCurrentThreadId()+1;main_thread=GetCurrentThreadId()+2;path_pass_open=1;
    unsigned char *wreck=(unsigned char*)&test_ptf_parts[2],*zone=(unsigned char*)&test_ptf_parts[1],*part=(unsigned char*)&test_ptf_parts[4];
    for(unsigned i=0;i<PTF_SITES;i++) {
        ptf_hier_fn stub=(ptf_hier_fn)(void*)(ptf_page+0x10u*i);
        __declspec(align(16)) float m[16]={1,2,3,0, 1,0,0,0, 0,1,0,0, 0,0,1,0};test_ptf_locals=0;test_ptf_seen_time=0;
        assert(stub(wreck,m,zone,0)==m && test_ptf_locals==1 && test_ptf_seen_time==pass);
        assert(m[0]==31.0f && m[1]==62.0f && m[2]==93.0f && m[4]==1.0f && m[9]==1.0f && m[14]==1.0f && m[5]==(i>=PTF_SITE_DOCK?1000.0f:0.0f));
        float n[16]={0,0,0,0, 1,0,0,0, 0,1,0,0, 0,0,1,0};test_ptf_locals=0;
        assert(stub(part,n,zone,0)==n && test_ptf_locals==2 && n[0]==80.0f && n[1]==160.0f && n[2]==240.0f);
        assert(InterlockedCompareExchange64(&ptf_site_calls[i],0,0)==2);
    }
    /* Each dock call also made one call of the end site inside its walk. */
    assert(ptf_diverted==2*PTF_SITES+4 && ptf_objects==2*PTF_SITES+4 && !ptf_failures && ptf_site_calls[1]==6 && !ptf_dock_agents);
    ptf_walk_native=NULL;test_ptf_walk_page=NULL;
    /* The MoveWorker gets the native call. */
    ui_thread=GetCurrentThreadId();{float m[16]={0};test_ptf_native_value=4.0f;
        assert(((ptf_hier_fn)(void*)(ptf_page+0x10u))(wreck,m,zone,0)==(void*)0x77 && m[0]==4.0f && ptf_outside==1);}
    memcpy(image+PTF_MANAGER,&saved_manager,8);ptf_test_tls=NULL;ptf_hier_native=NULL;
    /* Tamper: the tick check stops the gate; a foreign site is not restored over. */
    stats.issued=0;boot_phase=0;
    unsigned char keep[4];memcpy(keep,image+ptf_sites[3].site+1,4);image[ptf_sites[3].site+1]^=1;
    assert(!ptf_owned() && !ptf_check() && !strcmp(ptf_stage,"site_changed"));
    ptf_restore();assert(ptf_installed && ptf_done==(1u<<3) && !strcmp(ptf_stage,"restore_foreign"));
    memcpy(image+ptf_sites[3].site+1,keep,4);ptf_restore();ptf_failures=0;
    assert(!ptf_installed && ptf_restored && !ptf_done && !memcmp(image+PTF_WALK,walk,sizeof(walk)) && !memcmp(image+0xe8d5dc,point,sizeof(point)) && !ptf_on());
    for(unsigned i=0;i<PTF_SITES;i++) assert(ptf_site_state(i)==0);
    char out[512];assert(ptf_status(out,sizeof(out))>0 && strstr(out,"mode=sim installed=0 restored=1 stage=restored"));
    test_ptf_counts_reset();ptf_mode=PTF_OFF;ptf_restored=0;ptf_stage="idle";ptf_last_time=0;
    if(ptf_page) {VirtualFree(ptf_page,0,MEM_RELEASE);ptf_page=NULL;}
    stats.active=saved_active;stats.issued=saved_issued;ui_thread=saved_ui;main_thread=saved_main;boot_mode=saved_boot;boot_phase=saved_phase;
    path_installed=saved_path;path_options=saved_options;path_pass_open=saved_open;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("path transforms on X4.exe: pins, seven swaps (five pathing, two dock), view, step chain excluded, each stub runs GetTransformRelativeTo at the pass time; tamper, restore PASS");
}

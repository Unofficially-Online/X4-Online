/* native_gate_guidance_local (lockstep_guidance_local.h): play39p-long's two shared IDs on client 2 alone, with the
 * runtime IDs' stand-in maps of test_lockstep_runtime_ids.h. */
#define TEST_GLOCAL_ROUTE(expected,...) do {const uint32_t f[]={__VA_ARGS__};assert(rid_route_of(f,sizeof(f)/sizeof(*f))==(expected));} while(0)
/* The stacks of play39p-long's RUNTIME_IDS_CALLER rows (route=shared there). */
static const uint32_t test_glocal_end[]={0xcaab18,0x1111aaa,0x111c477,0x111e50a,0x112383d,0xb58ca9,0x111c821,0x1122083};   /* EndGuidance */
static const uint32_t test_glocal_betty[]={0xcaab18,0x1111aaa,0x111c477,0x9cb962,0x99c5b0,0x99a4ed,0xbc3d27,0xb58ca9};  /* BettyNotifications */
static const uint32_t test_glocal_junction[]={0x5c0017,0x5c7a99,0x5c7d57,0x5c8435,0x5ca2d6,0xa32fe3,0x1148cbf,0x113cc36}; /* client 1's route refresh */
static void test_glocal_scope(script_rng_frame *f,script_rng_frame *parent,unsigned kind,const char *name,unsigned state) {
    memset(f,0,sizeof(*f));f->parent=parent;f->depth=parent?parent->depth+1:1;f->id.kind=kind;f->state=state;
    if(name) snprintf(f->id.name,sizeof(f->id.name),"%s",name);
    f->owner=state==SR_SAME && parent?parent->owner:f;
}
/* One allocation as rid_assign makes it: its route, then the list. */
static uint64_t test_glocal_alloc(test_rid_process *p,const uint32_t *frames,unsigned count,unsigned char *object) {
    return rid_route_of(frames,count)==RID_ROUTE_UI?rid_assign_ui(object):test_rid_insert(p);
}
static void test_guidance_local(void) {
    const uint32_t saved_active=stats.active;const DWORD saved_ui=ui_thread;void *saved_map=*(void**)(image+RID_MAP);
    const int saved_created=created_installed,saved_armed=script_rng_armed,saved_cue=script_rng_cue_installed,saved_timed=md_timed_armed;
    const unsigned saved_mask=script_rng_mask;int saved_sites[MD_TIMED_SITES];memcpy(saved_sites,md_timed_installed,sizeof(saved_sites));
    script_rng_frame *const saved_current=script_rng_current;const LONG saved_rng_active=script_rng_active;
    char text[256];unsigned char object[16];
    ui_thread=GetCurrentThreadId();stats.active=1;glocal_armed=0;InterlockedExchange(&glocal_on,0);glocal_md=glocal_route=glocal_unnamed=0;glocal_stage="off";
    /* Arming: after a set runtime IDs with local lists, before the seal, with the script RNG's MD frames and listener frames; pinned. */
    {
        rid_mode=RID_OBSERVE;assert(!glocal_arm() && !strcmp(glocal_stage,"guidance_phase"));
        rid_mode=RID_SET;rid_sealed=1;assert(!glocal_arm() && !strcmp(glocal_stage,"guidance_phase"));rid_sealed=0;
        ui_thread=GetCurrentThreadId()+1u;assert(!glocal_arm() && !strcmp(glocal_stage,"guidance_phase"));ui_thread=GetCurrentThreadId();
        InterlockedExchange(&rid_local_on,0);assert(!glocal_arm() && !strcmp(glocal_stage,"guidance_lists"));InterlockedExchange(&rid_local_on,1);
        script_rng_armed=1;script_rng_mask=SCRIPT_RNG_AI;script_rng_cue_installed=1;md_timed_armed=1;
        for(unsigned i=0;i<MD_TIMED_SITES;i++) md_timed_installed[i]=1;
        assert(!glocal_arm() && !strcmp(glocal_stage,"guidance_frames"));            /* no MD bit: no cue names */
        script_rng_mask=SCRIPT_RNG_AI|SCRIPT_RNG_MD;script_rng_cue_installed=0;assert(!glocal_arm() && !strcmp(glocal_stage,"guidance_frames"));
        script_rng_cue_installed=1;md_timed_armed=0;assert(!glocal_arm() && !strcmp(glocal_stage,"guidance_frames"));   /* no listener frames */
        md_timed_armed=1;md_timed_installed[2]=0;assert(!glocal_arm() && !strcmp(glocal_stage,"guidance_frames"));md_timed_installed[2]=1;
        script_rng_armed=0;assert(!glocal_arm() && !strcmp(glocal_stage,"guidance_frames"));script_rng_armed=1;
        assert(!glocal_arm() && !strcmp(glocal_stage,"guidance_pins") && !glocal_armed && !glocal_on);   /* the plain image has no pinned code */
        assert(glocal_status(text,sizeof(text))>0 && !strcmp(text,"armed=0 on=0 stage=guidance_pins md=0 route=0 unnamed=0 module=Guidance"));
        /* Nothing was installed for real: the boundary's ownership checks read the plain image. */
        script_rng_cue_installed=saved_cue;memcpy(md_timed_installed,saved_sites,sizeof(saved_sites));
    }
    /* Two clients, the same simulation: client 2 alone holds a guidance, set in its presentation scope. */
    test_rid_process a=test_rid_new(),b=test_rid_new();uint64_t old[8];
    static rid_local_t lists_a[RID_ROUTES],lists_b[RID_ROUTES];
    test_rid_preload(&a,old,8,300);test_rid_preload(&b,old,8,1500);
    assert(rid_apply(a.map,0x10000) && rid_apply(b.map,0x10000));
    rid_base=0x10000;rid_failures=0;rid_dropped=0;rid_stage="set";
    rid_remove_native=test_rid_remove_native;rid_assign_native=test_rid_assign_stub;rid_allocate_native=test_rid_allocate;
    *(void**)(image+RID_MAP)=a.map;assert(rid_local_build(a.map));memcpy(lists_a,rid_local,sizeof(rid_local));
    *(void**)(image+RID_MAP)=b.map;assert(rid_local_build(b.map));memcpy(lists_b,rid_local,sizeof(rid_local));
    InterlockedExchange(&rid_active,1);InterlockedExchange(&rid_local_on,1);rid_sealed=1;rid_mode=RID_SET;InterlockedExchange(&script_rng_active,1);
    script_rng_frame s_near,s_end,s_betty,s_same,s_other,s_unnamed,s_ai;
    test_glocal_scope(&s_near,NULL,SCRIPT_RNG_MD,"Guidance.NearComponent",SR_OUTER);        /* its actions, from MDScheduler_Update after the delay */
    test_glocal_scope(&s_end,&s_near,SCRIPT_RNG_MD,"Guidance.EndGuidance",SR_CHILD);          /* the Cue_RaiseCueEvent listener */
    test_glocal_scope(&s_betty,&s_end,SCRIPT_RNG_MD,"Notifications.BettyNotifications",SR_CHILD); /* signal_objects player.computer */
    /* Routing. */
    {
        /* Off: what play39p-long had. */
        script_rng_current=&s_end;TEST_GLOCAL_ROUTE(RID_ROUTE_SHARED,0xcaab18,0x1111aaa,0x111c477,0x111e50a,0x112383d);
        TEST_GLOCAL_ROUTE(RID_ROUTE_SHARED,0x5c0017,0x5c7a99,0xa32fe3,0x1148cbf);
        assert(!glocal_md && !glocal_route);
        InterlockedExchange(&glocal_on,1);
        /* The guidance's own cues and what they make. */
        TEST_GLOCAL_ROUTE(RID_ROUTE_UI,0xcaab18,0x1111aaa,0x111c477,0x111e50a,0x112383d,0xb58ca9,0x111c821,0x1122083);
        script_rng_current=&s_near;TEST_GLOCAL_ROUTE(RID_ROUTE_UI,0xcaab18,0x1111aaa,0x111c477,0x111e50a);
        script_rng_current=&s_betty;TEST_GLOCAL_ROUTE(RID_ROUTE_UI,0xcaab18,0x1111aaa,0x111c477,0x9cb962,0x99c5b0,0x99a4ed,0xbc3d27,0xb58ca9);
        assert(glocal_md==3 && !glocal_route);
        /* A frame of the same cue as its owner has no name of its own: the owner's counts. */
        test_glocal_scope(&s_same,&s_end,SCRIPT_RNG_MD,NULL,SR_SAME);script_rng_current=&s_same;TEST_GLOCAL_ROUTE(RID_ROUTE_UI,0xcaab18,0x1111aaa);
        /* Betty from the engine's autopilot signal, another script, an AI script by that name: shared as before. */
        test_glocal_scope(&s_other,NULL,SCRIPT_RNG_MD,"Notifications.BettyNotifications",SR_OUTER);script_rng_current=&s_other;
        TEST_GLOCAL_ROUTE(RID_ROUTE_SHARED,0xcaab18,0x1111aaa,0x111c477,0x9cb962);
        snprintf(s_other.id.name,sizeof(s_other.id.name),"GuidanceTutorial.Start");TEST_GLOCAL_ROUTE(RID_ROUTE_SHARED,0xcaab18,0x1111aaa);
        snprintf(s_other.id.name,sizeof(s_other.id.name),"Guidance");TEST_GLOCAL_ROUTE(RID_ROUTE_SHARED,0xcaab18,0x1111aaa);
        test_glocal_scope(&s_ai,NULL,SCRIPT_RNG_AI,"Guidance.x",SR_OUTER);script_rng_current=&s_ai;TEST_GLOCAL_ROUTE(RID_ROUTE_SHARED,0xcaab18,0x1111aaa);
        assert(glocal_md==4 && !glocal_unnamed);
        /* An MD frame without a name is counted, never guessed. */
        test_glocal_scope(&s_unnamed,NULL,SCRIPT_RNG_MD,NULL,SR_UNKEYED);script_rng_current=&s_unnamed;
        TEST_GLOCAL_ROUTE(RID_ROUTE_SHARED,0xcaab18,0x1111aaa);assert(glocal_unnamed==1);
        s_unnamed.parent=&s_near;TEST_GLOCAL_ROUTE(RID_ROUTE_UI,0xcaab18,0x1111aaa);assert(glocal_unnamed==1 && glocal_md==5);
        /* No frame: shared. */
        script_rng_current=NULL;TEST_GLOCAL_ROUTE(RID_ROUTE_SHARED,0xcaab18,0x1111aaa,0x111c477,0x111e50a);
        /* The guidance route, wherever it sits in the stack; exactly that return address. */
        TEST_GLOCAL_ROUTE(RID_ROUTE_UI,0x5c0017,0x5c7a99,0x5c7d57,0x5c8435,0x5ca2d6,0xa32fe3,0x1148cbf,0x113cc36);
        TEST_GLOCAL_ROUTE(RID_ROUTE_UI,0xa32fe3);
        TEST_GLOCAL_ROUTE(RID_ROUTE_SHARED,0x5c0017,0xa32fe2);TEST_GLOCAL_ROUTE(RID_ROUTE_SHARED,0x5c0017,0xa32fe4);
        TEST_GLOCAL_ROUTE(RID_ROUTE_SHARED,0x5c0017,0x5c7a99,0x5ca2d6,0x72c2b6);   /* the autopilot's own route search */
        assert(glocal_route==2);
        /* Another thread: the frames are the frame thread's, the route rule is any thread's. */
        script_rng_current=&s_end;ui_thread=GetCurrentThreadId()+1u;
        TEST_GLOCAL_ROUTE(RID_ROUTE_SHARED,0xcaab18,0x1111aaa,0x111c477,0x111e50a);TEST_GLOCAL_ROUTE(RID_ROUTE_UI,0x5c0017,0xa32fe3);
        ui_thread=GetCurrentThreadId();
        /* The table's own routes stand: a view message under the guidance stays the view's, a UI site the UI's. */
        TEST_GLOCAL_ROUTE(RID_ROUTE_VIEW,0xaddb73,0xada912,0xadab6e,0x9bd5f9);TEST_GLOCAL_ROUTE(RID_ROUTE_UI,0x3d2431,0x7429dc,0x80d42e);
        /* Only with the local lists, after the seal. */
        InterlockedExchange(&rid_local_on,0);TEST_GLOCAL_ROUTE(RID_ROUTE_SHARED,0xcaab18,0x1111aaa,0xa32fe3);InterlockedExchange(&rid_local_on,1);
        rid_sealed=0;TEST_GLOCAL_ROUTE(RID_ROUTE_SHARED,0xcaab18,0x1111aaa,0xa32fe3);rid_sealed=1;
        script_rng_current=NULL;InterlockedExchange(&glocal_on,0);glocal_md=glocal_route=glocal_unnamed=0;
    }
    /* The case, without and with: both clients make the same 40 simulation allocations; client 2 also ends its guidance
     * (EndGuidance, BettyNotifications), client 1 refreshes its guidance route twice (8 junctions). */
    for(int on=0;on<2;on++) {
        InterlockedExchange(&glocal_on,on);
        uint64_t next[2],digest[2],shared[2][40];const uint64_t digest0=rid_digest;
        for(unsigned client=0;client<2;client++) {
            test_rid_process *p=client?&b:&a;
            if(!client) test_rid_use(&a,on?lists_b:NULL,lists_a);else test_rid_use(&b,lists_a,lists_b);
            rid_digest=digest0;
            for(unsigned i=0;i<40;i++) {
                shared[client][i]=test_glocal_alloc(p,test_glocal_end+1,2,object);   /* a Get_Military_Jobs-like cue, no guidance frame */
                if(client==1 && i==10) {
                    script_rng_current=&s_end;(void)test_glocal_alloc(p,test_glocal_end,8,object);
                    script_rng_current=&s_betty;(void)test_glocal_alloc(p,test_glocal_betty,8,object);
                    script_rng_current=NULL;
                }
                if(client==0 && (i==5 || i==30)) for(unsigned j=0;j<4;j++) (void)test_glocal_alloc(p,test_glocal_junction,8,object);
            }
            assert(rid_frame(6150+on));next[client]=rid_next_id;digest[client]=rid_digest;
        }
        if(!on) assert(next[0]!=next[1] && digest[0]!=digest[1] && memcmp(shared[0],shared[1],sizeof(shared[0])));
        else assert(next[0]==next[1] && digest[0]==digest[1] && !memcmp(shared[0],shared[1],sizeof(shared[0])) && glocal_md==2 && glocal_route==8);
        if(on) memcpy(lists_b,rid_local,sizeof(rid_local));
        /* The second pass starts both from the same head again. */
        if(!on) {
            const uint32_t head_a=*(uint32_t*)(a.map+0x50),head_b=*(uint32_t*)(b.map+0x50);
            while(*(uint32_t*)(a.map+0x50)<(head_a>head_b?head_a:head_b)) test_rid_insert(&a);
            while(*(uint32_t*)(b.map+0x50)<(head_a>head_b?head_a:head_b)) test_rid_insert(&b);
            memcpy(lists_b,rid_local,sizeof(rid_local));
        }
    }
    /* Through the hook: an EndGuidance-like allocation under the guidance's frame comes off the ui list, its row says so. */
    {
        test_rid_use(&b,NULL,lists_b);memset(rid_callers,0,sizeof(rid_callers));rid_caller_count=0;char row[192];
        const uint32_t head=*(uint32_t*)(b.map+0x50);
        script_rng_current=&s_end;const uint64_t local=rid_assign(object,image+0xcaab18);script_rng_current=NULL;
        assert((local&0x1ffffffu)>=0x1f00001u && *(uint32_t*)(b.map+0x50)==head && glocal_md==3);
        assert(rid_caller_count==1 && rid_caller_row(0,row,sizeof(row))>0 && strstr(row," route=ui count=1 "));
        const uint64_t shared=rid_assign(object,image+0xcaab18);assert(shared==head);
        memset(rid_callers,0,sizeof(rid_callers));rid_caller_count=0;
    }
    /* The frames go away (the script RNG restored, a cue slot or MD site lost): the boundary stops the gate. */
    {
        assert(rid_frame(6200) && glocal_on);
        InterlockedExchange(&script_rng_active,0);
        assert(!rid_frame(6201) && !strcmp(rid_stage,"guidance_frames") && !glocal_on && rid_failures==1);
        assert(glocal_status(text,sizeof(text))>0 && strstr(text,"on=0 stage=guidance_frames md=3 route=8 unnamed=0"));
        rid_failures=0;rid_stage="set";InterlockedExchange(&script_rng_active,1);
        /* Off, nothing is checked. */
        assert(rid_frame(6202));
    }
    /* A rerun of the arming after a loading-UI reload answers what it armed. */
    glocal_armed=1;InterlockedExchange(&glocal_on,1);assert(glocal_arm());InterlockedExchange(&glocal_on,0);assert(!glocal_arm());glocal_armed=0;
    InterlockedExchange(&rid_active,0);InterlockedExchange(&rid_local_on,0);InterlockedExchange(&glocal_on,0);
    rid_remove_native=NULL;rid_assign_native=NULL;rid_allocate_native=NULL;rid_local_built=0;memset(rid_local,0,sizeof(rid_local));memset(rid_local_now,0,sizeof(rid_local_now));
    rid_mode=RID_OFF;rid_base=0;rid_sealed=0;rid_failures=0;rid_dropped=0;rid_stage="idle";rid_digest=0;rid_next_id=0;rid_frames_done=0;rid_cut=rid_skipped=rid_head_before=rid_below_base=0;
    memset(&rid_now,0,sizeof(rid_now));memset(&rid_before,0,sizeof(rid_before));
    test_rid_free_process(&a);test_rid_free_process(&b);
    glocal_md=glocal_route=glocal_unnamed=0;glocal_stage="off";
    script_rng_current=saved_current;InterlockedExchange(&script_rng_active,saved_rng_active);
    script_rng_armed=saved_armed;script_rng_cue_installed=saved_cue;md_timed_armed=saved_timed;script_rng_mask=saved_mask;memcpy(md_timed_installed,saved_sites,sizeof(saved_sites));
    *(void**)(image+RID_MAP)=saved_map;stats.active=saved_active;ui_thread=saved_ui;created_installed=saved_created;
    printf("guidance local arming/routing/two clients/hook/frames tests PASS\n");
}
/* The pins on the pinned image: the route function, its call of the search, its five callers; each tampered refuses. */
static void test_guidance_local_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    assert(glocal_pins());
    const uint32_t tamper[]={GLOCAL_ROUTE_FN,GLOCAL_ROUTE_FN+GLOCAL_ROUTE_SIZE-1u,GLOCAL_ROUTE_CALL+1u,0xde4835u,0xde4870u,0xde48b2u,0x1148cbbu,0x1148d22u,0x1148d21u};
    for(unsigned i=0;i<sizeof(tamper)/sizeof(*tamper);i++) {image[tamper[i]]^=1;assert(!glocal_pins());image[tamper[i]]^=1;}
    assert(glocal_pins());
    /* The route function reads the universe (0x3d198f8) and returns what the search returns: no allocation of its own. */
    assert(0xa32f8bu+*(int32_t*)(image+0xa32f87)==0x3d198f8u && image[0xa32fe3]==0x48 && image[0xa32fe7]==0xc3);
    /* The stacks of play39p-long: the junction constructor allocates (AssignNewID 0xcc220), Cue_CreateInstance's constructor
     * returns to RID_CUE_CREATE, the listener call of Cue_RaiseCueEvent is the MD timed site the listener frames ride on. */
    assert(image[0x5c0012]==0xe8 && 0x5c0017u+*(int32_t*)(image+0x5c0013)==0xcc220u);
    assert(image[0x111e505]==0xe8 && 0x111e50au+*(int32_t*)(image+0x111e506)==MD_TIMED_CHECK);
    /* Arming on the pinned image, every precondition in place; the rerun answers the same. */
    {
        const uint32_t saved_active=stats.active;const DWORD saved_ui=ui_thread;
        const int saved_armed=script_rng_armed,saved_cue=script_rng_cue_installed,saved_timed=md_timed_armed;const unsigned saved_mask=script_rng_mask;
        int saved_sites[MD_TIMED_SITES];memcpy(saved_sites,md_timed_installed,sizeof(saved_sites));
        stats.active=1;ui_thread=GetCurrentThreadId();rid_mode=RID_SET;rid_sealed=0;InterlockedExchange(&rid_local_on,1);
        script_rng_armed=1;script_rng_mask=SCRIPT_RNG_MD;script_rng_cue_installed=1;md_timed_armed=1;for(unsigned i=0;i<MD_TIMED_SITES;i++) md_timed_installed[i]=1;
        glocal_armed=0;assert(glocal_arm() && glocal_armed && glocal_on && !strcmp(glocal_stage,"armed") && glocal_arm());
        char text[256];assert(glocal_status(text,sizeof(text))>0 && !strncmp(text,"armed=1 on=1 stage=armed ",25));
        glocal_armed=0;InterlockedExchange(&glocal_on,0);glocal_stage="off";InterlockedExchange(&rid_local_on,0);rid_mode=RID_OFF;
        script_rng_armed=saved_armed;script_rng_cue_installed=saved_cue;md_timed_armed=saved_timed;script_rng_mask=saved_mask;memcpy(md_timed_installed,saved_sites,sizeof(saved_sites));
        stats.active=saved_active;ui_thread=saved_ui;
    }
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("guidance local pins and stacks verified on the pinned image");
}

/* lockstep_view_cameras.h on synthetic memory: the note at the two shot sites, the latch, the stand-in cameras in the
 * shadow universe's list, the anchor found again by its ID, the ends, and the pins on the real image. */
typedef struct {void **vtable;uint64_t id;unsigned char pad[0x60];void *parent;unsigned char rest[0x200];} test_vc_object;
static void *test_vc_vtable[704];
static test_vc_object test_vc_anchor,test_vc_room,test_vc_ship,test_vc_far;
static unsigned char test_vc_shot[0x100],test_vc_owner[0x80],test_vc_cutscene[0x500],test_vc_manager[0x100];
static unsigned char *test_vc_nodes[2][3][3];   /* [list][0 sentinel, 1..2 nodes][next, prev, cutscene] */
static void *test_vc_found,*test_vc_frame_shot;
static int test_vc_frame_r14;static uint32_t test_vc_frame_site;
static void *test_vc_log[16];static unsigned test_vc_logged;
/* The stand-in rule of the natives, in small: a camera that is active and anchored in the object or below it. */
static int test_vc_native(void *object) {
    unsigned char *universe=*(unsigned char**)(view_page+VIEW_PAGE_UNIVERSE);
    assert(universe==view_page+VIEW_PAGE_SHADOW);
    const uint64_t count=*(uint64_t*)(universe+0x3c0);assert(count<=VCAM_MAX);
    for(uint64_t i=0;i<count;i++) {
        unsigned char *camera=*(unsigned char**)(universe+0x2c0+i*8);
        assert(camera==vcam_record((unsigned)i) && *(void**)camera==(void*)(image+VCAM_VTABLE) && !*(int*)(camera+0x868) && !*(void**)(camera+0x20));
        if(!camera[0x870]) {assert(!*(void**)(camera+0x770) && !*(void**)(camera+0x778));continue;}
        assert(*(void**)(camera+0x770)==*(void**)(camera+0x778));
        for(test_vc_object *p=*(void**)(camera+0x770);p;p=p->parent) if(p==object) return 11;
    }
    return 3;
}
static void *test_vc_find(void *map,uint64_t id,int kind) {assert(map==(void*)test_vc_manager && kind==4);return id==0x7001?test_vc_found:NULL;}
static void *test_vc_frame(uint32_t site,int r14) {test_vc_frame_site=site;test_vc_frame_r14=r14;return test_vc_frame_shot;}
static void test_vc_set_attention(void *object,int level) {assert(level==1);if(test_vc_logged<16) test_vc_log[test_vc_logged]=object;++test_vc_logged;}
/* The manager's two lists: `first` holds the cutscene in the simulation's list, `second` in the other one. */
static void test_vc_lists(int first,int second) {
    for(unsigned l=0;l<2;l++) {
        const int has=l?second:first;
        test_vc_nodes[l][0][0]=(unsigned char*)(has?test_vc_nodes[l][1]:test_vc_nodes[l][0]);
        test_vc_nodes[l][1][0]=(unsigned char*)test_vc_nodes[l][0];test_vc_nodes[l][1][2]=test_vc_cutscene;
        void *sentinel=test_vc_nodes[l][0];memcpy(test_vc_manager+(l?0x50:0x30),&sentinel,8);
    }
}
static void test_view_cameras(void) {
    const uint64_t issued_was=stats.issued;const DWORD ui_was=ui_thread;const uint32_t active_was=stats.active;
    const int mode_was=view_mode,installed_was=view_installed,trigger_was=view_trigger_mode,pins_was=vcam_pins_checked;const LONG view_active_was=view_active;
    unsigned char manager_was[8],map_was[8];memcpy(manager_was,image+VCAM_MANAGER,8);memcpy(map_was,image+VCAM_MAP,8);
    if(!view_page) view_page=view_page_near();assert(view_page);
    if(!view_lock_ready) {InitializeCriticalSection(&view_lock);view_lock_ready=1;}
    memset(view_page,0,VIEW_PAGE_SHADOW+VIEW_SHADOW_SIZE);
    test_vc_vtable[703]=(void*)test_vc_native;
    test_vc_object *all[]={&test_vc_anchor,&test_vc_room,&test_vc_ship,&test_vc_far};
    for(unsigned i=0;i<4;i++) {memset(all[i],0,sizeof(test_vc_object));all[i]->vtable=test_vc_vtable;all[i]->id=0x7001+i;}
    test_vc_anchor.parent=&test_vc_room;test_vc_room.parent=&test_vc_ship;
    ((unsigned char*)&test_vc_anchor)[0xd1]=1;((unsigned char*)&test_vc_ship)[0xd1]=1;   /* the room follows its ship */
    memset(test_vc_shot,0,sizeof(test_vc_shot));memset(test_vc_owner,0,sizeof(test_vc_owner));memset(test_vc_cutscene,0,sizeof(test_vc_cutscene));
    memset(test_vc_manager,0,sizeof(test_vc_manager));
    {void *p=test_vc_owner;memcpy(test_vc_shot+0x50,&p,8);p=test_vc_cutscene;memcpy(test_vc_owner+0x70,&p,8);
     uint64_t id=0x7001;memcpy(test_vc_shot+0x20,&id,8);int cid=1629;memcpy(test_vc_cutscene+8,&cid,4);
     p=test_vc_manager;memcpy(image+VCAM_MANAGER,&p,8);memcpy(image+VCAM_MAP,&p,8);}
    test_vc_lists(0,0);test_vc_found=&test_vc_anchor;test_vc_frame_shot=test_vc_shot;
    vcam_frame_override=test_vc_frame;view_set_attention_override=test_vc_set_attention;test_vc_logged=0;
    ui_thread=GetCurrentThreadId();stats.active=1;stats.issued=0;view_active=1;view_latched=1;view_count=0;view_depth=0;
    vcam_armed=0;vcam_pins_checked=1;vcam_shown=0;
    /* Not armed: a shot site is nobody's. Arming needs view points 'players', the first tick still ahead, the pins. */
    vcam_note(VCAM_SITE_SHOT);assert(!vcam_adds && !vcam_no_shot);
    view_mode=VIEW_OFF;view_installed=1;assert(!vcam_arm() && !strcmp(vcam_why,"view_points"));
    view_mode=VIEW_PLAYERS;stats.issued=1;assert(!vcam_arm() && !strcmp(vcam_why,"late_or_thread"));stats.issued=0;
    vcam_pins_checked=-1;assert(!vcam_arm() && !strcmp(vcam_why,"pins"));vcam_pins_checked=1;
    assert(vcam_arm() && vcam_armed && vcam_arm());vcam_find=test_vc_find;
    /* A shot of a cutscene in no list, then in the manager's other list: counted, not noted. Other sites: nothing. */
    vcam_note(VCAM_SITE_SHOT);assert(vcam_other==1 && !vcam_adds && test_vc_frame_site==VCAM_SITE_SHOT && test_vc_frame_r14==1);
    test_vc_lists(0,1);vcam_note(VCAM_SITE_SHOT);assert(vcam_local==1 && !vcam_adds);
    vcam_note(0x10d686bu);vcam_note(0x7cdde0u);assert(vcam_other==1 && vcam_local==1 && !vcam_no_shot);
    /* The simulation's cutscene: noted through the trigger's own path (refused as before), a viewer only from the latch on. */
    test_vc_lists(1,0);view_trigger_mode=VIEW_TRIGGER_BLOCK;*(int*)((unsigned char*)&test_vc_anchor+0x1ec)=6;
    {int kept=0;const uint64_t blocked=view_trigger_blocked;
     assert(view_trigger_apply(&test_vc_anchor,view_trigger_match(VCAM_SITE_SHOT),&kept)==1 && kept==6 && view_trigger_blocked==blocked+1);}
    assert(vcam_adds==1 && !vcam_shown && view_attention(&test_vc_ship)==3);
    vcam_note(VCAM_SITE_SHOT);assert(vcam_adds==1);   /* the same shot again: one entry */
    vcam_latch();
    assert(vcam_shown==1 && vcam_shown_ids[0]==0x7001 && *(uint64_t*)(view_page+VIEW_PAGE_SHADOW+0x3c0)==1 && vcam_peak==1);
    assert(vcam_chains==1 && test_vc_logged==2 && test_vc_log[0]==&test_vc_anchor && test_vc_log[1]==&test_vc_ship);
    assert(view_attention(&test_vc_anchor)==11 && view_attention(&test_vc_room)==11 && view_attention(&test_vc_ship)==11 && view_attention(&test_vc_far)==3);
    /* With a view point: every evaluation sees the camera as well; the highest level wins. */
    view_count=1;view_points[0]=&test_vc_far;assert(view_attention(&test_vc_ship)==11 && view_attention(&test_vc_far)==3);view_count=0;
    /* The anchor gone between two latches: the stand-in shows nothing; back, it shows again. */
    test_vc_found=NULL;assert(view_attention(&test_vc_ship)==3 && !vcam_record(0)[0x870]);
    test_vc_found=&test_vc_anchor;assert(view_attention(&test_vc_ship)==11);
    vcam_latch();assert(vcam_shown==1 && vcam_chains==1 && test_vc_logged==2);   /* nothing changed: nothing recomputed */
    {char status[512];assert(vcam_status(status,sizeof(status))>0 && strstr(status,"armed=1 shown=1 ids=7001 noted=1 adds=1 ends=0 ") && strstr(status,"local=1 other=1 "));}
    /* The shot's end (the shot in rbx): the viewer stops at the next latch and the chain is recomputed. */
    test_vc_frame_shot=test_vc_owner;vcam_note(VCAM_SITE_END);assert(!vcam_ends && test_vc_frame_r14==0);   /* another shot's end */
    test_vc_frame_shot=test_vc_shot;vcam_note(VCAM_SITE_END);vcam_note(VCAM_SITE_END);assert(vcam_ends==1 && vcam_shown==1);
    vcam_latch();
    assert(!vcam_shown && !*(uint64_t*)(view_page+VIEW_PAGE_SHADOW+0x3c0) && !*(void**)(view_page+VIEW_PAGE_SHADOW+0x2c0) && vcam_chains==2 && test_vc_logged==4);
    assert(view_attention(&test_vc_ship)==3 && !vcam_dropped);
    /* A cutscene that leaves the list without the shot's end, another cutscene at the same address, an anchor that is gone. */
    vcam_note(VCAM_SITE_SHOT);vcam_latch();assert(vcam_adds==2 && vcam_shown==1);
    test_vc_lists(0,0);vcam_latch();assert(!vcam_shown && vcam_dropped==1 && vcam_chains==4);
    test_vc_lists(1,0);vcam_note(VCAM_SITE_SHOT);vcam_latch();assert(vcam_shown==1);
    {int cid=1631;memcpy(test_vc_cutscene+8,&cid,4);vcam_latch();assert(!vcam_shown && vcam_dropped==2);}
    vcam_note(VCAM_SITE_SHOT);vcam_latch();assert(vcam_shown==1);
    test_vc_found=NULL;vcam_latch();assert(!vcam_shown && vcam_dropped==3 && vcam_gone==1);test_vc_found=&test_vc_anchor;
    /* No frame of the site on the stack, a shot without an owner or a target: counted. More shots than places: counted. */
    test_vc_frame_shot=NULL;vcam_note(VCAM_SITE_SHOT);assert(vcam_no_shot==1);test_vc_frame_shot=test_vc_shot;
    {uint64_t none=0,id=0x7001;memcpy(test_vc_shot+0x20,&none,8);vcam_note(VCAM_SITE_SHOT);assert(vcam_no_shot==2);memcpy(test_vc_shot+0x20,&id,8);}
    {static unsigned char shots[VCAM_MAX+1][0x100];const uint64_t adds=vcam_adds;
     for(unsigned i=0;i<=VCAM_MAX;i++) {memcpy(shots[i],test_vc_shot,0x100);test_vc_frame_shot=shots[i];vcam_note(VCAM_SITE_SHOT);}
     assert(vcam_adds==adds+VCAM_MAX && vcam_full==1);
     vcam_latch();assert(vcam_shown==VCAM_MAX && vcam_peak==VCAM_MAX && *(uint64_t*)(view_page+VIEW_PAGE_SHADOW+0x3c0)==VCAM_MAX);
     assert(view_attention(&test_vc_ship)==11);}
    /* The end of the session: nothing of ours stays in the shadow's list. */
    vcam_stop();assert(!vcam_armed && !vcam_shown && !*(uint64_t*)(view_page+VIEW_PAGE_SHADOW+0x3c0) && !*(void**)(view_page+VIEW_PAGE_SHADOW+0x2c0));
    assert(view_attention(&test_vc_ship)==3);
    vcam_frame_override=NULL;view_set_attention_override=NULL;vcam_find=NULL;vcam_why="-";vcam_pins_checked=pins_was;
    view_mode=mode_was;view_installed=installed_was;view_trigger_mode=trigger_was;view_active=view_active_was;view_latched=0;view_count=0;
    memset(view_points,0,sizeof(view_points));
    memcpy(image+VCAM_MANAGER,manager_was,8);memcpy(image+VCAM_MAP,map_was,8);
    stats.issued=issued_was;ui_thread=ui_was;stats.active=active_was;
    VirtualFree(view_page,0,MEM_RELEASE);view_page=NULL;
    printf("view cameras: shots noted at their sites, viewers from the latch, anchors by ID, ends and orphans PASS\n");
}
/* The real image: the functions the stand-in rests on are the ones read, the two sites hold the shot where it is taken from. */
static void test_view_cameras_native_image(const char *path) {
    unsigned char *prior=image;const int pins_was=vcam_pins_checked;
    image=test_pmath_map(path);vcam_pins_checked=0;
    assert(vcam_pins());
    assert(!memcmp(image+0x10d7fd0u,"\x4c\x8b\xf1",3));                       /* camera shot update: mov r14,rcx (the shot) */
    assert(!memcmp(image+0x10d8312u,"\x48\x8b\xce\xe8",4));                   /* mov rcx,rsi; call SetAttention -> 0x10d831a */
    assert(!memcmp(image+0x10d5f0fu,"\x48\x8b\xd9",3));                       /* the shot's end: mov rbx,rcx */
    assert(!memcmp(image+0x10d7fe8u,"\x4d\x8b\x6e\x50",4) && !memcmp(image+0x10d7ff1u,"\x4d\x8b\x6d\x70",4));   /* [[shot+0x50]+0x70]: the cutscene */
    assert(view_trigger_match(VCAM_SITE_SHOT)==7 && view_trigger_match(VCAM_SITE_END)==5);
    image=prior;vcam_pins_checked=pins_was;
    printf("pinned native image view cameras verified (camera position, shot update and end, the cutscene camera's slot 1)\n");
}

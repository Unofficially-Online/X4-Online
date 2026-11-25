/* lockstep_md_fullscreen.h on synthetic memory: a full-screen cutscene's owner from the action, X4's presentation on the
 * owner's client only (slots 1, 2, 3 and 8), the shots' cameras made inactive on the others, the end jumped over there,
 * the listener's camera, the RNG word held around what runs on one client, the table's upkeep, the arm's refusals. */
#define TMFS_SCENES 4u
#define TMFS_SHOTS 3u
static unsigned char test_mfs_manager[0x100],test_mfs_sentinel[0x18],test_mfs_finished[0x18],test_mfs_universe[0x400];
static unsigned char test_mfs_scene[TMFS_SCENES][0x540],test_mfs_lnode[TMFS_SCENES][0x18];
static unsigned char test_mfs_tnode[TMFS_SHOTS][0x48],test_mfs_shot[TMFS_SHOTS][0x58],test_mfs_playback[TMFS_SHOTS][0x78];
static unsigned char test_mfs_camera[TMFS_SHOTS*2][0x880],test_mfs_entry[TMFS_SHOTS*2][8],test_mfs_listener_camera[0x10];
static unsigned test_mfs_enters,test_mfs_inserts,test_mfs_recomputes,test_mfs_selects;
static void *test_mfs_key_seen;
static uint64_t test_mfs_rng;
static int test_mfs_pin_answer;
static int test_mfs_pin(void) {return test_mfs_pin_answer;}
static uint64_t *test_mfs_rng_word(void) {return &test_mfs_rng;}
/* X4's presentation: once (+0x4ec), and then (here) it draws from the thread's RNG word, as a camera event's handler might. */
static void test_mfs_enter(void *cutscene) {
    unsigned char *const c=cutscene;++test_mfs_enters;
    if(!c[MFS_FULL] || c[MFS_ENTERED]) return;
    c[MFS_ENTERED]=1;test_mfs_rng+=7;
}
static void *test_mfs_insert(void *tree,void **key) {
    unsigned char *const camera=(unsigned char *)tree-MFS_ACTIVITY;
    for(unsigned i=0;i<TMFS_SHOTS*2;i++) if(camera==test_mfs_camera[i]) {++test_mfs_inserts;test_mfs_key_seen=*key;return test_mfs_entry[i];}
    assert(0);return NULL;
}
/* The camera's +0x870 from the highest entry: the shot's own {100, on} below ours. */
static void test_mfs_recompute(void *camera) {
    unsigned char *const c=camera;++test_mfs_recomputes;
    for(unsigned i=0;i<TMFS_SHOTS*2;i++) if(c==test_mfs_camera[i]) {uint32_t p=0;memcpy(&p,test_mfs_entry[i],4);c[0x870]=p>100u?test_mfs_entry[i][4]:1;test_mfs_rng+=3;}
}
static void *test_mfs_select(void *manager) {
    unsigned char *sentinel=NULL,*node=NULL;++test_mfs_selects;
    memcpy(&sentinel,(unsigned char *)manager+MFS_LIST,8);memcpy(&node,sentinel,8);
    for(;node!=sentinel;memcpy(&node,node,8)) {
        unsigned char *scene=NULL;memcpy(&scene,node+0x10,8);
        if(scene[MFS_LETTERBOX] || scene[MFS_CINEMATIC]) return test_mfs_listener_camera;
    }
    return NULL;
}
static void test_mfs_list_reset(unsigned char *sentinel) {memcpy(sentinel,&sentinel,8);memcpy(sentinel+8,&sentinel,8);}
static void test_mfs_link(unsigned char *sentinel,unsigned char *node,unsigned char *scene) {
    unsigned char *last=NULL;memcpy(&last,sentinel+8,8);
    memcpy(node,&sentinel,8);memcpy(node+8,&last,8);memcpy(node+0x10,&scene,8);memcpy(last,&node,8);memcpy(sentinel+8,&node,8);
}
static void test_mfs_unlink(unsigned char *node) {
    unsigned char *next=NULL,*previous=NULL;memcpy(&next,node,8);memcpy(&previous,node+8,8);memcpy(previous,&next,8);memcpy(next+8,&previous,8);
}
static void test_mfs_plant_scene(unsigned i,int id,int full,int letterbox) {
    unsigned char *const s=test_mfs_scene[i];
    memset(s,0,sizeof(test_mfs_scene[0]));memcpy(s+MFS_ID,&id,4);s[MFS_FULL]=(unsigned char)full;s[MFS_LETTERBOX]=(unsigned char)letterbox;
}
/* Shot k of scene i (its cameras made at its first pass, as the shot's update makes them), hung under the tree's root. */
static void test_mfs_plant_shot(unsigned i,unsigned k) {
    unsigned char *const s=test_mfs_scene[i],*node=test_mfs_tnode[k],*shot=test_mfs_shot[k],*playback=test_mfs_playback[k];
    unsigned char *const camera=test_mfs_camera[2*k],*environment=test_mfs_camera[2*k+1];
    unsigned char *const overlay=image+MFS_OVERLAY_VTABLE,*envtable=image+MFS_ENVIRONMENT_VTABLE;
    memset(node,0,sizeof(test_mfs_tnode[0]));memset(shot,0,sizeof(test_mfs_shot[0]));memset(playback,0,sizeof(test_mfs_playback[0]));
    memset(camera,0,sizeof(test_mfs_camera[0]));memset(environment,0,sizeof(test_mfs_camera[0]));memset(test_mfs_entry[2*k],0,16);
    memcpy(camera,&overlay,8);memcpy(environment,&envtable,8);camera[0x870]=environment[0x870]=1;
    memcpy(shot+MFS_SHOT_CAMERA,&camera,8);memcpy(shot+MFS_SHOT_ENVIRONMENT,&environment,8);memcpy(shot+MFS_SHOT_PLAYBACK,&playback,8);
    memcpy(playback+MFS_PLAYBACK_CUTSCENE,&s,8);memcpy(node+MFS_NODE_SHOT,&shot,8);
    unsigned char *root=NULL;memcpy(&root,s+MFS_SHOTS+8,8);
    if(!root) memcpy(s+MFS_SHOTS+8,&node,8);
    else {unsigned char *right=NULL;memcpy(&right,root+MFS_NODE_RIGHT,8);memcpy(right?right+MFS_NODE_LEFT:root+MFS_NODE_RIGHT,&node,8);}
}
/* The action, as mdv_action brackets it: scene i joins the list inside it, made in owner's frame. */
static void test_mfs_action(unsigned i,unsigned owner) {
    const int marked=mfs_before();
    test_mfs_link(test_mfs_sentinel,test_mfs_lnode[i],test_mfs_scene[i]);
    mfs_after(marked,owner);
}
/* One client (its slot) sees a scene of `owner` from the action to its end. */
static void test_mfs_client(unsigned slot,unsigned owner) {
    const int other=owner && owner!=slot;
    memset(mfs,0,sizeof(mfs));stats.slot=slot;test_mfs_list_reset(test_mfs_sentinel);
    test_mfs_plant_scene(0,0x901,1,1);
    const LONG64 seen=mfs_seen,owned=mfs_owned,hidden=mfs_hidden,shown=mfs_shown,cameras=mfs_cameras,skipped=mfs_skipped,held=mfs_held,
        listener=mfs_listener_local,moved=mfs_rng_moved;
    test_mfs_action(0,owner);
    assert(mfs_seen==seen+1 && mfs_owned==owned+(owner?1:0) && mfs[0].used && mfs[0].owner==owner && mfs[0].cutscene==test_mfs_scene[0] && mfs[0].id==0x901);
    test_mfs_plant_shot(0,0);   /* its first shot's first pass, in the manager's update before the presentation's call */
    test_mfs_enters=test_mfs_inserts=test_mfs_recomputes=0;test_mfs_rng=0x1000;
    mfs_enter(test_mfs_scene[0]);
    assert(test_mfs_scene[0][MFS_ENTERED]==1);
    if(other) {
        /* X4's presentation never ran here: the player's cameras, HUD, input and cursor stay this client's; both cameras of
         * the shot draw nothing, through X4's own tree with our key; the RNG word is where it was. */
        assert(!test_mfs_enters && test_mfs_inserts==2 && test_mfs_recomputes==2 && test_mfs_key_seen==(void*)mfs_key && mfs_key[1]==MFS_KEY_ID);
        assert(!test_mfs_camera[0][0x870] && !test_mfs_camera[1][0x870] && !test_mfs_entry[0][4]);
        {uint32_t p=0;memcpy(&p,test_mfs_entry[0],4);assert(p==MFS_HIDE_PRIORITY);}
        assert(mfs_hidden==hidden+1 && mfs_shown==shown && mfs_cameras==cameras+2 && test_mfs_rng==0x1000 && mfs_rng_moved==moved+2);
        /* every frame: nothing twice; a later shot's cameras the frame they come */
        mfs_enter(test_mfs_scene[0]);assert(test_mfs_inserts==2 && mfs_hidden==hidden+1);
        test_mfs_plant_shot(0,1);mfs_enter(test_mfs_scene[0]);
        assert(test_mfs_inserts==4 && !test_mfs_camera[2][0x870] && !test_mfs_camera[3][0x870] && mfs_cameras==cameras+4 && !test_mfs_enters);
        /* a shot of another scene in its tree (not its playback's): left alone */
        {unsigned char *elsewhere=test_mfs_scene[3];memcpy(test_mfs_playback[1]+MFS_PLAYBACK_CUTSCENE,&elsewhere,8);
         test_mfs_camera[2][0x870]=1;memset(mfs[0].camera,0,sizeof(mfs[0].camera));mfs[0].cameras=0;
         mfs_enter(test_mfs_scene[0]);assert(test_mfs_inserts==6 && test_mfs_camera[2][0x870]==1 && mfs[0].cameras==2);}
        /* the listener's camera: none for it, so the listener stays on this client's camera */
        assert(mfs_listener(test_mfs_manager)==NULL && mfs_listener_local==listener+1);
        /* its end: jumped over */
        assert(mfs_leave(test_mfs_scene[0])==1 && mfs_skipped==skipped+1 && !mfs[0].used && !mfs_end_held && mfs_held==held);
    } else {
        /* the owner (or everybody's): X4's presentation, its cameras untouched */
        assert(test_mfs_enters==1 && !test_mfs_inserts && test_mfs_camera[0][0x870]==1 && test_mfs_camera[1][0x870]==1 && mfs_hidden==hidden);
        if(owner) assert(mfs_shown==shown+1 && test_mfs_rng==0x1000 && mfs_rng_moved==moved+1);   /* the owner's alone: its draw put back */
        else assert(mfs_shown==shown && test_mfs_rng==0x1007 && mfs_rng_moved==moved);          /* every client's: X4's own */
        mfs_enter(test_mfs_scene[0]);assert(test_mfs_enters==2);   /* X4 itself returns at +0x4ec */
        assert(mfs_listener(test_mfs_manager)==(void*)test_mfs_listener_camera && mfs_listener_local==listener);
        test_mfs_rng=0x2000;
        assert(mfs_leave(test_mfs_scene[0])==0 && !mfs[0].used && mfs_skipped==skipped);
        if(owner) {
            assert(mfs_end_held && mfs_held==held+1);
            test_mfs_rng+=11;mfs_leave_done();   /* X4's end ran and drew */
            assert(!mfs_end_held && test_mfs_rng==0x2000 && mfs_rng_moved==moved+2);
        } else assert(!mfs_end_held && mfs_held==held);
    }
    assert(mfs_leave(test_mfs_scene[0])==0);   /* gone from the table: X4's way */
}
static void test_md_fullscreen(void) {
    const uint64_t issued_was=stats.issued;const DWORD ui_was=ui_thread;const uint32_t active_was=stats.active,slot_was=stats.slot;
    const int mdv_was=mdv_armed;uint64_t *(*const rng_was)(void)=vps_rng_word;const LONG64 pres_moved_was=pres_rng_moved;
    unsigned char manager_was[8];memcpy(manager_was,image+MFS_MANAGER,8);
    {unsigned char *p=test_mfs_manager;memcpy(image+MFS_MANAGER,&p,8);}
    memset(test_mfs_manager,0,sizeof(test_mfs_manager));test_mfs_list_reset(test_mfs_sentinel);test_mfs_list_reset(test_mfs_finished);
    {unsigned char *p=test_mfs_sentinel;memcpy(test_mfs_manager+MFS_LIST,&p,8);p=test_mfs_finished;memcpy(test_mfs_manager+MFS_FINISHED,&p,8);}
    (void)test_mfs_universe;
    mfs_armed=0;mfs_pin=test_mfs_pin;test_mfs_pin_answer=1;ui_thread=GetCurrentThreadId();stats.active=1;stats.issued=0;stats.slot=1;
    vps_rng_word=test_mfs_rng_word;
    /* Arming: after the gate's start, before the first tick, on the frame thread, with md media and the pins. */
    stats.active=0;assert(!mfs_arm() && !strcmp(mfs_why,"not_started"));stats.active=1;
    stats.issued=1;assert(!mfs_arm() && !strcmp(mfs_why,"late_or_thread"));stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!mfs_arm());ui_thread=GetCurrentThreadId();
    mdv_armed=0;assert(!mfs_arm() && !strcmp(mfs_why,"md_media"));mdv_armed=1;
    test_mfs_pin_answer=0;assert(!mfs_arm() && !strcmp(mfs_why,"pins") && !mfs_armed);test_mfs_pin_answer=1;
    assert(mfs_arm() && mfs_armed && mfs_arm() && mfs_fn.enter==(mfs_enter_fn)(image+MFS_ENTER) && mfs_fn.select==(mfs_select_fn)(image+MFS_SELECT) &&
        mfs_base==image);
    mfs_fn.enter=test_mfs_enter;mfs_fn.insert=test_mfs_insert;mfs_fn.recompute=test_mfs_recompute;mfs_fn.select=test_mfs_select;
    /* Without the slots' near page nothing is installed; here the site helpers' own tests stand for the install. */
    {unsigned char *const page=pslot_page;pslot_page=NULL;assert(mfs_frame()==0 && !mfs_install_tried && !mfs_enter_installed);pslot_page=page;}
    mfs_install_tried=1;
    /* Each client of a scene of slot 2's, of slot 1's, of slot 3's and of slot 8's; and one of nobody's. */
    test_mfs_client(1,2);test_mfs_client(2,2);test_mfs_client(3,2);
    test_mfs_client(1,1);test_mfs_client(2,1);test_mfs_client(3,1);
    test_mfs_client(1,3);test_mfs_client(2,3);test_mfs_client(3,3);
    test_mfs_client(8,8);test_mfs_client(3,8);
    test_mfs_client(1,0);test_mfs_client(2,0);test_mfs_client(3,0);
    stats.slot=1;memset(mfs,0,sizeof(mfs));test_mfs_list_reset(test_mfs_sentinel);
    /* What the action did not make, or makes on a render target, is not noted. */
    {const LONG64 seen=mfs_seen;
     test_mfs_plant_scene(1,0x911,1,1);test_mfs_link(test_mfs_sentinel,test_mfs_lnode[1],test_mfs_scene[1]);   /* there before the action */
     test_mfs_plant_scene(2,0x912,0,0);test_mfs_action(2,2);                                                     /* a render target's */
     assert(mfs_seen==seen && !mfs[0].used && !mfs_find(test_mfs_scene[1]) && !mfs_find(test_mfs_scene[2]));
     test_mfs_enters=0;mfs_enter(test_mfs_scene[1]);mfs_enter(test_mfs_scene[2]);assert(test_mfs_enters==2);    /* X4's own */
     /* a slot of more than eight is nobody's */
     test_mfs_plant_scene(3,0x913,1,0);test_mfs_action(3,PILOT_SLOTS+1);assert(mfs_seen==seen+1 && mfs[0].used && !mfs[0].owner);}
    /* The listener's camera: the scene 0x10f2110 takes is the first with a letterbox; another slot's further down does
     * not change its answer. */
    {stats.slot=1;memset(mfs,0,sizeof(mfs));test_mfs_list_reset(test_mfs_sentinel);
     test_mfs_plant_scene(1,0x921,1,1);test_mfs_action(1,0);test_mfs_plant_scene(2,0x922,1,1);test_mfs_action(2,2);
     assert(mfs_listener(test_mfs_manager)==(void*)test_mfs_listener_camera);
     test_mfs_unlink(test_mfs_lnode[1]);assert(mfs_listener(test_mfs_manager)==NULL);
     test_mfs_plant_scene(1,0x921,1,0);test_mfs_link(test_mfs_sentinel,test_mfs_lnode[1],test_mfs_scene[1]);
     assert(mfs_listener(test_mfs_manager)==NULL);   /* a scene without a letterbox behind it: still the first's */
     ui_thread=GetCurrentThreadId()+1;assert(mfs_listener(test_mfs_manager)==NULL);ui_thread=GetCurrentThreadId();   /* any thread asks */
     /* the table's upkeep: an entry whose scene is in neither list leaves; one in the finished list stays */
     {const LONG64 gone=mfs_gone;
      assert(mfs_frame()==2);test_mfs_unlink(test_mfs_lnode[1]);
      test_mfs_unlink(test_mfs_lnode[2]);test_mfs_link(test_mfs_finished,test_mfs_lnode[2],test_mfs_scene[2]);
      assert(mfs_frame()==1 && mfs_gone==gone+1 && mfs_find(test_mfs_scene[2]) && !mfs_find(test_mfs_scene[1]));
      test_mfs_unlink(test_mfs_lnode[2]);assert(mfs_frame()==0 && mfs_gone==gone+2);}
     /* a scene whose memory is taken by another with a new ID is not the old one's */
     test_mfs_plant_scene(2,0x922,1,1);test_mfs_action(2,3);{const int id=0x923;memcpy(test_mfs_scene[2]+MFS_ID,&id,4);}
     assert(!mfs_find(test_mfs_scene[2]));
     {char status[400];assert(mfs_status(status,sizeof(status))>0 && strstr(status,"armed=1 installed=0/0/0/0/0 live=1/1/1 ") && strstr(status," why=-"));}
     memset(mfs,0,sizeof(mfs));test_mfs_list_reset(test_mfs_sentinel);}
    /* A full table: the next scene is everybody's (counted). */
    {mfs_t kept=mfs[0];for(unsigned i=0;i<MFS_MAX;i++) {mfs[i]=kept;mfs[i].used=1;mfs[i].cutscene=test_mfs_scene[3];mfs[i].id=0x7000+(int)i;}
     const LONG64 full=mfs_full;test_mfs_plant_scene(1,0x931,1,1);test_mfs_action(1,2);assert(mfs_full==full+1 && !mfs_find(test_mfs_scene[1]));
     memset(mfs,0,sizeof(mfs));test_mfs_list_reset(test_mfs_sentinel);}
    /* Off the frame thread or after the session: X4's own presentation, nothing noted, nothing jumped over. */
    {test_mfs_plant_scene(1,0x941,1,1);test_mfs_action(1,2);assert(mfs[0].used);
     test_mfs_enters=0;ui_thread=GetCurrentThreadId()+1;mfs_enter(test_mfs_scene[1]);assert(test_mfs_enters==1 && mfs_leave(test_mfs_scene[1])==0 && mfs[0].used);
     test_mfs_plant_scene(2,0x942,1,1);test_mfs_action(2,2);assert(!mfs_find(test_mfs_scene[2]));ui_thread=GetCurrentThreadId();
     stats.active=0;test_mfs_plant_scene(1,0x941,1,1);mfs_enter(test_mfs_scene[1]);assert(test_mfs_enters==2 && mfs_leave(test_mfs_scene[1])==0);
     assert(mfs_listener(test_mfs_manager)==(void*)test_mfs_listener_camera);stats.active=1;}
    /* The end's stub: X4's mov edx,2 and the jump back, or (al set) the jump past the block to X4's last instruction. */
    {unsigned char stub[PSND_STUB_ROOM];const unsigned n=mfs_leave_stub(stub,sizeof(stub));void *to=NULL;
     assert(n>9u+28u && n<=sizeof(stub));
     memcpy(&to,stub+n-8,8);assert(to==(void*)(pnat_base+MFS_DONE_SITE) && stub[n-14]==0xff && stub[n-13]==0x25);
     memcpy(&to,stub+n-22,8);assert(to==(void*)(pnat_base+MFS_LEAVE_RESUME) && stub[n-28]==0xff && stub[n-27]==0x25);
     assert(!memcmp(stub+n-28-9,mfs_leave_tail,9) && stub[n-28-9+3]==0x13);   /* jnz over five bytes and one jump */
     assert(!mfs_leave_stub(stub,100));}
    /* The end: the table is empty, X4's own way again. */
    mfs_restore();assert(!mfs_armed && !mfs[0].used && !strcmp(mfs_why,"-"));
    mfs_pin=mfs_pins;memset(&mfs_fn,0,sizeof(mfs_fn));mfs_install_tried=0;mfs_base=NULL;
    memcpy(image+MFS_MANAGER,manager_was,8);
    mdv_armed=mdv_was;vps_rng_word=rng_was;pres_rng_moved=pres_moved_was;
    stats.issued=issued_was;ui_thread=ui_was;stats.active=active_was;stats.slot=slot_was;
    printf("md fullscreen: owner from the action, presentation on its owner's client only (slots 1, 2, 3, 8), cameras off elsewhere, end, listener, RNG held, upkeep PASS\n");
}
/* The real image: the pins, the four sites as X4 has them, each inside one 16-byte block, and the cameras' classes. */
static void test_md_fullscreen_native_image(const char *path) {
    unsigned char *prior=image;int32_t rel=0;
    image=test_pmath_map(path);
    mfs_pins_checked=0;assert(mfs_pins());mfs_pins_checked=0;
    /* the presentation's call, for each cutscene of the list (mov rcx,[rbx+0x10]) */
    assert(image[MFS_ENTER_SITE]==0xe8 && (MFS_ENTER_SITE&~15u)==((MFS_ENTER_SITE+4u)&~15u));
    memcpy(&rel,image+MFS_ENTER_SITE+1,4);assert(MFS_ENTER_SITE+5u+(uint32_t)rel==MFS_ENTER);
    {static const unsigned char load[4]={0x48,0x8b,0x4b,0x10};assert(!memcmp(image+MFS_ENTER_SITE-4u,load,4));}
    /* the presentation starts with its two tests: cmp byte [rcx+0x4e9],0 and cmp byte [rcx+0x4ec],0 */
    {static const unsigned char full[7]={0x80,0xb9,0xe9,0x04,0x00,0x00,0x00},entered[7]={0x80,0xb9,0xec,0x04,0x00,0x00,0x00};
     assert(!memcmp(image+0x10ea15au,full,7) && !memcmp(image+0x10ea167u,entered,7));}
    /* the end: cmp byte [r15+0x4e9],0; je 0x10f1d9f; mov edx,2 ... mov [r15+0x450],rdi */
    {static const unsigned char test[8]={0x41,0x80,0xbf,0xe9,0x04,0x00,0x00,0x00};assert(!memcmp(image+0x10f1bf2u,test,8) && image[0x10f1bfau]==0x0f && image[0x10f1bfbu]==0x84);
     memcpy(&rel,image+0x10f1bfcu,4);assert(0x10f1c00u+(uint32_t)rel==MFS_DONE_RESUME);}
    assert(!memcmp(image+MFS_LEAVE_SITE,mfs_leave_head,5) && (MFS_LEAVE_SITE&~15u)==((MFS_LEAVE_SITE+4u)&~15u));
    assert(!memcmp(image+MFS_DONE_SITE,mfs_done_head,7) && (MFS_DONE_SITE&~15u)==((MFS_DONE_SITE+6u)&~15u) && MFS_DONE_SITE+7u==MFS_DONE_RESUME);
    /* the listener's call of 0x10f2110 (mov rcx,r10 before it) */
    {static const unsigned char load[3]={0x49,0x8b,0xca};assert(!memcmp(image+MFS_LISTENER_SITE-3u,load,3));}
    assert(image[MFS_LISTENER_SITE]==0xe8 && (MFS_LISTENER_SITE&~15u)==((MFS_LISTENER_SITE+4u)&~15u));
    memcpy(&rel,image+MFS_LISTENER_SITE+1,4);assert(MFS_LISTENER_SITE+5u+(uint32_t)rel==MFS_SELECT);
    /* the shot's cameras: the constructors the shot update calls (0x10d8049, 0x10d80eb) set these vtables */
    memcpy(&rel,image+0x10d8049u+1,4);assert(image[0x10d8049u]==0xe8 && 0x10d804eu+(uint32_t)rel==0x985f50u);
    memcpy(&rel,image+0x10d80ebu+1,4);assert(image[0x10d80ebu]==0xe8 && 0x10d80f0u+(uint32_t)rel==0x985310u);
    memcpy(&rel,image+0x985f69u,4);assert(image[0x985f66u]==0x48 && image[0x985f67u]==0x8d && 0x985f6du+(uint32_t)rel==MFS_OVERLAY_VTABLE);
    memcpy(&rel,image+0x985342u,4);assert(image[0x98533fu]==0x48 && image[0x985340u]==0x8d && 0x985346u+(uint32_t)rel==MFS_ENVIRONMENT_VTABLE);
    /* the cutscene's keys: the presentation registers 0x10f2180 for every id of mfs_key_ids (lea r8,[rip+d]; mov edx,id),
     * STOP_CUTSCENE after its abortable test; 0x9fbd40's call of the UI's event inside one 16-byte block */
    {static const struct {unsigned at;int id;} reg[8]={{0x10ea2d4u,0x164},{0x10ea2e8u,0x71},{0x10ea2fcu,0x16a},{0x10ea310u,0x165},
        {0x10ea324u,0x166},{0x10ea338u,0x167},{0x10ea34cu,0x168},{0x10ea369u,0xa5}};
     for(unsigned i=0;i<8;i++) {
        int32_t d=0,id=0;memcpy(&d,image+reg[i].at+3,4);memcpy(&id,image+reg[i].at+8,4);int listed=0;
        assert(image[reg[i].at]==0x4c && image[reg[i].at+1]==0x8d && image[reg[i].at+2]==0x05 && reg[i].at+7u+(uint32_t)d==MFS_KEY);
        assert(image[reg[i].at+7]==0xba && id==reg[i].id);
        for(unsigned j=0;j<sizeof(mfs_key_ids)/sizeof(*mfs_key_ids);j++) listed+=mfs_key_ids[j]==id;
        assert(listed==1);
     }}
    assert(image[MFS_RAISE_SITE]==0xe8 && (MFS_RAISE_SITE&~15u)==((MFS_RAISE_SITE+4u)&~15u));
    memcpy(&rel,image+MFS_RAISE_SITE+1,4);assert(MFS_RAISE_SITE+5u+(uint32_t)rel==MFS_RAISE);
    image=prior;
    printf("pinned native image md fullscreen verified (the presentation and its end, the listener's camera, the cameras' tree and classes, the keys' registrations and the UI event's call)\n");
}
/* The cutscene's keys through the stock record: the verdict, the record's checks, this client's dispatch (the id's
 * other handlers here, X4's handler recorded only where the scene is presented), the replay (the handler alone, once,
 * on every client), a stop that never takes another slot's scene, the UI event on the key's own client only. */
static unsigned test_mfs_key_calls,test_mfs_other_calls,test_mfs_raised;
static int test_mfs_key_id,test_mfs_key_abortable[TMFS_SCENES];
static void test_mfs_raise_fn(void *sink,void *name) {(void)sink;(void)name;++test_mfs_raised;}
/* X4's handler: what the scenes' +0x4ed were at the call, and (as 0x9fbd40 does for a stop) the UI's event. */
static void test_mfs_key_fn(int id) {
    ++test_mfs_key_calls;test_mfs_key_id=id;
    for(unsigned i=0;i<TMFS_SCENES;i++) test_mfs_key_abortable[i]=test_mfs_scene[i][MFS_ABORTABLE];
    if(id==MFS_KEY_STOP) mfs_raise(NULL,NULL);
}
static void test_mfs_other_fn(int id) {(void)id;++test_mfs_other_calls;}
static unsigned char test_mfs_input[0x6440];
static pnat_node test_mfs_nodes[2];
/* The actions' multimap at input+0x63e0 for 0xa5: X4's handler (when presented) and one other. */
static void test_mfs_map(int presented) {
    pnat_node *const header=(pnat_node*)(test_mfs_input+0x63e0),*a=&test_mfs_nodes[0],*b=&test_mfs_nodes[1];
    memset(header,0,sizeof(*header));memset(test_mfs_nodes,0,sizeof(test_mfs_nodes));
    b->key=MFS_KEY_STOP;b->handler=(void*)test_mfs_other_fn;
    if(presented) {a->key=MFS_KEY_STOP;a->handler=(void*)test_mfs_key_fn;a->parent=header;a->right=b;b->parent=a;header->left=a;}
    else {b->parent=header;header->left=b;}
}
static void test_md_fullscreen_keys(void) {
    const uint64_t issued_was=stats.issued;const DWORD ui_was=ui_thread;const uint32_t active_was=stats.active,slot_was=stats.slot;
    const int mdv_was=mdv_armed,capturing_was=pstk_capturing,focus_was=pstk_focus_forced;const unsigned capture_was=pstk_capture_slot,scope_was=pstk_scope_slot;
    unsigned char *const input_was=pnat_input;const LONG live_was=pnat_live;const pstk_record build_was=pstk_build;
    unsigned char manager_was[8];memcpy(manager_was,image+MFS_MANAGER,8);
    {unsigned char *p=test_mfs_manager;memcpy(image+MFS_MANAGER,&p,8);}
    memset(test_mfs_manager,0,sizeof(test_mfs_manager));test_mfs_list_reset(test_mfs_sentinel);test_mfs_list_reset(test_mfs_finished);
    {unsigned char *p=test_mfs_sentinel;memcpy(test_mfs_manager+MFS_LIST,&p,8);p=test_mfs_finished;memcpy(test_mfs_manager+MFS_FINISHED,&p,8);}
    ui_thread=GetCurrentThreadId();stats.active=1;stats.issued=0;stats.slot=1;mdv_armed=1;
    /* Without the module the ids are presentation ids, as before. */
    mfs_armed=0;assert(pstk_verdict(PSTK_ACTION,MFS_KEY_STOP)==PSTK_LOCAL && !mfs_key_replayed(PSTK_ACTION,0x164));
    mfs_pin=test_mfs_pin;test_mfs_pin_answer=1;assert(mfs_arm());
    assert(mfs_fn.key==(mfs_key_fn)(image+MFS_KEY) && mfs_fn.raise==(mfs_raise_fn)(image+MFS_RAISE));
    mfs_fn.key=test_mfs_key_fn;mfs_fn.raise=test_mfs_raise_fn;mfs_install_tried=1;
    /* The verdict and the record: the same on every client, for slot 1 and slots 2..N. */
    for(unsigned i=0;i<sizeof(mfs_key_ids)/sizeof(*mfs_key_ids);i++) {
        assert(pstk_verdict(PSTK_ACTION,mfs_key_ids[i])==PSTK_REPLAY && pstk_verdict_slot(PSTK_ACTION,mfs_key_ids[i],2)==PSTK_REPLAY &&
            pstk_verdict_slot(PSTK_ACTION,mfs_key_ids[i],3)==PSTK_REPLAY && pstk_verdict_slot(PSTK_ACTION,mfs_key_ids[i],8)==PSTK_REPLAY);
        assert(pstk_verdict(PSTK_STATE,mfs_key_ids[i])==(mfs_key_ids[i]==0x71?PSTK_REPLAY:PSTK_LOCAL));   /* the state 0x71 is flight's */
    }
    assert(pstk_verdict(PSTK_ACTION,0xa6)==PSTK_LOCAL && pstk_verdict(PSTK_ACTION,0x169)==PSTK_LOCAL);
    {pstk_record r,d;unsigned char b[PSTK_BYTES_MAX];memset(&r,0,sizeof(r));r.count=2;
     r.events[0]=(pstk_event){PSTK_ACTION,MFS_KEY_STOP,0};r.events[1]=(pstk_event){PSTK_ACTION,0x164,0};
     const unsigned n=pstk_encode(&r,b);assert(pstk_decode(b,n,&d) && d.count==2 && d.events[0].id==MFS_KEY_STOP);
     assert(pstk_slot_record_ok(&d,2) && pstk_slot_record_ok(&d,3) && pstk_slot_record_ok(&d,8));}
    /* Three scenes in the list: slot 1's, slot 2's, nobody's; all abortable. */
    test_mfs_plant_scene(0,0xa01,1,1);test_mfs_action(0,1);test_mfs_plant_scene(1,0xa02,1,1);test_mfs_action(1,2);
    test_mfs_plant_scene(2,0xa03,1,1);test_mfs_action(2,0);
    for(unsigned i=0;i<3;i++) test_mfs_scene[i][MFS_ABORTABLE]=1;
    pnat_input=test_mfs_input;pstk_capturing=1;pstk_focus_forced=1;
    for(unsigned client=1;client<=3;client++) {
        unsigned char *const map=test_mfs_input+0x63e0;
        stats.slot=client;pstk_capture_slot=client;memset(&pstk_build,0,sizeof(pstk_build));
        /* This client presents a scene: the other handler here, X4's into its own record. */
        test_mfs_map(1);test_mfs_key_calls=test_mfs_other_calls=0;
        {const LONG64 recorded=mfs_key_recorded;
         assert(mfs_key_local(map,MFS_KEY_STOP,0)==1 && test_mfs_other_calls==1 && !test_mfs_key_calls && mfs_key_recorded==recorded+1 &&
             pstk_build.count==1 && pstk_build.events[0].kind==PSTK_ACTION && pstk_build.events[0].id==MFS_KEY_STOP && !pstk_build.events[0].value);}
        /* ...through the dispatcher (a real key) and as a scripted input_local (pstk_local_press, the pass's drain) */
        InterlockedExchange(&pnat_live,1);
        pstk_dispatch(PSTK_ACTION,map,MFS_KEY_STOP,0);assert(pstk_build.count==2 && test_mfs_other_calls==2 && !test_mfs_key_calls);
        pstk_local_queued=0;assert(pstk_local_press(PSTK_ACTION,MFS_KEY_STOP,0) && pstk_local_queued==1);
        pstk_local_drain();assert(pstk_build.count==3 && test_mfs_other_calls==3 && !pstk_local_queued);
        /* without the focus a real key is dropped; a scripted one is no key of the window */
        pstk_focus_forced=-1;{const LONG64 unfocused=mfs_key_unfocused;pstk_dispatch(PSTK_ACTION,map,MFS_KEY_STOP,0);
         assert(mfs_key_unfocused==unfocused+1 && pstk_build.count==3 && test_mfs_other_calls==3);}pstk_focus_forced=1;
        InterlockedExchange(&pnat_live,0);
        /* Another client's scene, not presented here: the other handlers only, nothing recorded. */
        test_mfs_map(0);{const LONG64 unpresented=mfs_key_unpresented;
         assert(mfs_key_local(map,MFS_KEY_STOP,1)==1 && test_mfs_other_calls==4 && pstk_build.count==3 && mfs_key_unpresented==unpresented+1);}
        /* A client that records nothing: the handler's part is dropped. */
        test_mfs_map(1);pstk_capturing=0;{const LONG64 dropped=mfs_key_dropped;
         assert(mfs_key_local(map,MFS_KEY_STOP,1)==1 && pstk_build.count==3 && mfs_key_dropped==dropped+1);}pstk_capturing=1;
        /* The replay of slot k's STOP_CUTSCENE on this client: X4's handler once, never the multimap's others; another
         * slot's abortable scenes masked for the call and back after it; the UI's event on slot k's own client only. */
        for(unsigned k=1;k<=3;k++) {
            pstk_scope_slot=k==1?0u:k;test_mfs_key_calls=test_mfs_other_calls=test_mfs_raised=0;
            const LONG64 applied=mfs_key_applied,foreign=mfs_key_foreign,masked=mfs_key_masked,raises=mfs_key_raises;
            {pstk_event x={PSTK_ACTION,MFS_KEY_STOP,0};assert(pstk_replay_handlers_ok(test_mfs_input+0x63e0,&x));}   /* not a stock handler: not walked */
            pstk_replay_walk(map,PSTK_ACTION,MFS_KEY_STOP,0);
            assert(test_mfs_key_calls==1 && !test_mfs_other_calls && test_mfs_key_id==MFS_KEY_STOP && mfs_key_applied==applied+1);
            assert(test_mfs_key_abortable[0]==(k==1) && test_mfs_key_abortable[1]==(k==2) && test_mfs_key_abortable[2]==1);   /* own and nobody's */
            assert(mfs_key_masked==masked+(k==3?2:1) && test_mfs_scene[0][MFS_ABORTABLE]==1 && test_mfs_scene[1][MFS_ABORTABLE]==1);
            assert(mfs_key_foreign==foreign+(k!=client) && test_mfs_raised==(k==client) && mfs_key_raises==raises+(k!=client) && !mfs_key_foreign_now);
        }
        /* Another key (the letterbox) masks nothing. */
        pstk_scope_slot=2;{const LONG64 masked=mfs_key_masked;pstk_replay_walk(map,PSTK_ACTION,0x164,0);
         assert(test_mfs_key_id==0x164 && mfs_key_masked==masked && test_mfs_key_abortable[0]==1);}
        pstk_scope_slot=0;
    }
    /* A scene that left the list is not masked; one whose stop already came (not abortable) is left as it is. */
    stats.slot=2;pstk_scope_slot=2;test_mfs_unlink(test_mfs_lnode[0]);
    {const LONG64 masked=mfs_key_masked;pstk_replay_walk(test_mfs_input+0x63e0,PSTK_ACTION,MFS_KEY_STOP,0);assert(mfs_key_masked==masked);}
    test_mfs_link(test_mfs_sentinel,test_mfs_lnode[0],test_mfs_scene[0]);test_mfs_scene[0][MFS_ABORTABLE]=0;
    {const LONG64 masked=mfs_key_masked;pstk_replay_walk(test_mfs_input+0x63e0,PSTK_ACTION,MFS_KEY_STOP,0);
     assert(mfs_key_masked==masked && !test_mfs_scene[0][MFS_ABORTABLE]);}
    pstk_scope_slot=0;
    /* The UI's event outside an apply: X4's. */
    test_mfs_raised=0;mfs_raise(NULL,NULL);assert(test_mfs_raised==1);
    {char status[512];assert(mfs_status(status,sizeof(status))>0 && strstr(status,"installed=0/0/0/0/0 ") && strstr(status," keys="));}
    mfs_restore();assert(!mfs_armed && pstk_verdict(PSTK_ACTION,MFS_KEY_STOP)==PSTK_LOCAL);
    mfs_pin=mfs_pins;memset(&mfs_fn,0,sizeof(mfs_fn));mfs_install_tried=0;mfs_base=NULL;memset(mfs,0,sizeof(mfs));
    memcpy(image+MFS_MANAGER,manager_was,8);
    pnat_input=input_was;InterlockedExchange(&pnat_live,live_was);pstk_build=build_was;pstk_local_queued=0;
    mdv_armed=mdv_was;pstk_capturing=capturing_was;pstk_focus_forced=focus_was;pstk_capture_slot=capture_was;pstk_scope_slot=scope_was;
    stats.issued=issued_was;ui_thread=ui_was;stats.active=active_was;stats.slot=slot_was;
    printf("md fullscreen keys: verdict and record, this client's dispatch (presented, not presented, unfocused, scripted), the replay "
           "of slots 1, 2 and 3 on clients 1, 2 and 3 (the handler alone, another slot's scene masked, the UI event on its own client) PASS\n");
}

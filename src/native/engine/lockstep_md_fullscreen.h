/* A mission's full-screen cutscene (play_cutscene without targetmonitor and without a render target: the camera shot of a
 * story scene) on its owner's screen only. native_gate_md_fullscreen, a session default with native_gate_md_media; Windows
 * gate; needs the slots' near page; no stop reason of its own (pins that do not hold refuse the arm). Included by
 * lockstep_ui_replay.h after lockstep_md_media.h, whose action hook notes the owner.
 *
 * What X4 does (build 9.00, read from the image):
 *   start    Cutscenes::PlayCutsceneAction 0x10d25e0 makes the cutscene (0x540 bytes, constructor 0x10e71b0) and puts it
 *            into the cutscene manager's list ([0x3d19900]+0x30, node[0] next, node[2] the cutscene; count +0x38), a
 *            cinematic one into +0x40 too. The cutscene: +8 its ID, +0x4e8 started, +0x4e9 full screen (no render target
 *            of type 5), +0x4ea letterbox, +0x4eb cinematic mode, +0x4ec its presentation entered, +0x4ed abortable,
 *            +0x530 the sound camera's mode. Its camera shots are a tree at +0x2d0 (root +0x2d8, a node's +8 left, +0x10
 *            right, +0x40 the shot); a shot's +0x18 is its U::OverlayCamera (vtable 0x2bae0b8), +0x48 its
 *            U::EnvironmentCamera (0x2bae0f8), +0x50 its Cutscenes::Shot, whose +0x70 is the cutscene.
 *   frame    the manager's frame update 0x10f1930 (U::UniverseFrameUpdate 0x9fe407: simulation, every client at one tick)
 *            runs the list's pass 0x10f1a50 (each cutscene's update 0x10e9a60: its shots' updates 0x10d7f60 / 0x10e22f0
 *            make the shot's cameras at their first pass, put {shot: 100, on} into each camera's activity tree +0x790
 *            and set the manager's +0xb0 / +0xb8 to them), the finished list's pass, then 0x10ea140 for every cutscene of
 *            the list (the call at 0x10f1974).
 *   present  0x10ea140, once per full-screen cutscene (+0x4e9 set, +0x4ec clear), is the whole presentation: the player
 *            camera [g_Universe+0x3d0] and its environment camera [+0x3d8] get {cutscene: 100, off} in their activity
 *            trees (0xa4deb0, then 0x9829f0: the entry of the highest priority sets the camera's +0x870, which the
 *            render reads, Camera::vfunc3 0x983160: an inactive camera draws nothing), the UI hides three HUD layers for
 *            the cutscene's ID (UI controller slot 67 0xb3fc80), the input object pushes the cutscene's context 0xe
 *            (0x1252950, priority 500 or 400) and takes the keys STOP_CUTSCENE 0xa5 (abortable only), the letterbox,
 *            the cinematic camera's channels and its map key (0x1255610, handler 0x10f2180), the cursor goes off; then
 *            +0x4ec = 1. Its end is a block of the list's pass (0x10f1bf2: cmp [r15+0x4e9],0; je over it): the same
 *            undone (0x1400de470, 0x12558d0, 0x1252af0, 0x982910, slot 69 0xb3fd90), then +0x450 = 0, before the
 *            cutscene's destructor. Nothing else reads +0x4e9 or +0x4ec.
 *   sound    the listener's camera 0x9f9ee0 (U::Player 0x70bc10 sets the sound manager's listener room from it; the
 *            emitters' transforms 0x3e8470, a bullet's) takes the camera 0x10f2110 names: the manager's +0xb0 while the
 *            first cutscene of the list with a letterbox or cinematic mode has a sound camera mode 0, else none.
 * Every other reader of the manager is the simulation's or is routed already: +0xb0 (MD's isinfullscreencutscene,
 * U::Player's update 0x7113e0 at 0x713041, the scenes, traffic and highway helpers 0x9fc950.., the bullets' cadence) stays
 * as X4 leaves it, and so do both lists, every cutscene's own bytes and its shots (lockstep_view_cameras.h makes the
 * shots' viewers for the attention from the same list; the camera events that a camera's activity change posts at once,
 * 0x97dff0, are this client's own under native_gate_view_triggers 'block').
 *
 * With this module, on every client, deterministic in what it changes (only this client's cameras, UI and input):
 *   owner    the action hook of lockstep_md_media.h: the cutscenes the action put into the list with +0x4e9 set are
 *            noted with the slot whose own cue's frame made them (mpp_show_slot: slot k's scope with an owned frame, or
 *            a cue of slot 1's own); made in nobody's frame a cutscene is everybody's, as before. At most MFS_MAX at
 *            once; a full table leaves the next one everybody's (counted).
 *   another  on a client that is not the owner's: the call at 0x10f1974 comes here (slots' near page) and X4's 0x10ea140
 *            does not run for that cutscene: the player's cameras stay on, the HUD, the input context, the keys and the
 *            cursor stay this client's; +0x4ec is set as X4 sets it. Every camera of its shots gets {key: priority
 *            0x7fffffff, off} in its activity tree (X4's 0xa4deb0 and 0x9829f0, the same frame the shot made it): it
 *            draws nothing here. The end block is jumped over (0x10f1c00 -> stub: X4's mov edx,2 or 0x10f1d98, where
 *            +0x450 = 0 runs as always). The listener's camera answers none for it (0x9f9f42 -> stub, rax..r11 kept: the
 *            caller holds r9..r11 across X4's call): this client's own camera, as without the scene.
 *   owner's  X4's presentation and its end, unchanged: the shot, the hidden HUD, the cutscene's input context and
 *            keys, exactly as a game of one shows it to its player. The thread's simulation RNG word is held around
 *            them (and around the cameras' changes on the others): what runs on one client only must not move it
 *            (rng_moved= counts a draw that was put back).
 * Status MD_FULLSCREEN: armed, installed (enter/end/after/listener), seen (full-screen cutscenes the action made), owned
 * (of them with an owner), live, hidden/shown (cutscenes this client did not / did present), cameras (made inactive
 * here), passes, ends skipped/held, listener answers, rng_moved, gone, full, why.
 * Pinned (refused on another build): the presentation 0x10ea140 (the pin of lockstep_virtual_players.h), the manager's
 * frame update 0x10f1930, the list's pass 0x10f1a50 (both sites as X4 has them), the listener's camera 0x9f9ee0, 0x10f2110,
 * the activity tree's insert 0xa4deb0 and recompute 0x9829f0, a cutscene's update 0x10e9a60 (the shots' tree) and both
 * shot updates up to their cameras (0x10d7f60, 0x10e22f0).
 * The cutscene's keys. The presentation registers X4's handler 0x10f2180 (input+0x63e0, the actions' multimap) for
 * STOP_CUTSCENE 0xa5 (abortable scenes; the key Escape while the cutscene's context 0xe is on), the letterbox 0x164,
 * the cinematic camera's channels 0x165..0x168 and lock 0x16a, and 0x71 (the map key, which ends a cinematic mode). The
 * handler changes the simulation: 0xa5 stops the first abortable cutscene of the list (0x10f1800: +0x200, reason 2,
 * into the finished list; with a cinematic one 0x10f16f0 as well), 0x71 the cinematic ones, both post
 * deactivate_cinematic_camera to the player entity (0x9fbd40, which also raises cinematic_camera_deactivated in the UI:
 * the docked menu comes back), 0x164 flips the manager's +0xc0 and every cutscene's +0x4ea, the others signal the player
 * entity. Run by the client whose key it was, alone, it parted the worlds. With this module they ride the stock record
 * (lockstep_pilot_stock.h: their verdict is replay, the record carries them as actions):
 *   capture  this client's dispatch of one of these ids (a key, or a scripted input_local) runs the id's other handlers
 *            at once, as a presentation id (the map opens), and records the id in its own slot's record when X4's
 *            handler is registered here, i.e. when this client presents the scene: the owner's Escape skips its scene.
 *            Another player's Escape is not this id at all (its own menu: the cutscene's context is not on there), and
 *            one that comes as this id anyway runs X4's other handlers only (unpresented=).
 *   apply    every client, at the record's tick in its slot's own replay (slot 1 canonical, slot k in its scope): X4's
 *            handler once for the id, whatever this client registered. A stop takes the first abortable scene that is
 *            the slot's own or nobody's: another slot's abortable scenes are masked (+0x4ed) for the call, so player 2's
 *            Escape never ends player 1's scene. The UI's cinematic_camera_deactivated is raised on the key's own
 *            client only (0x9fbed0 -> stub +0x8c0).
 * Status keys=recorded/applied/foreign/unpresented/unfocused/dropped/masked/raises.
 * Not covered: a joiner during a scene presents it (the join package carries no owner). */
#define MFS_ENTER 0x10ea140u               /* the presentation of one cutscene of the list (rcx: the cutscene) */
#define MFS_ENTER_SIZE 0x376u
#define MFS_ENTER_SHA "9b39fc8641f8de54fc29873524d453db983f89b9df8cff42d68ab1ecb86f2eb3"
#define MFS_UPDATE 0x10f1930u              /* the manager's frame update */
#define MFS_UPDATE_SIZE 0x11bu
#define MFS_UPDATE_SHA "7ccdec616bd2aa0b537f47f7cc7c208573da921a22a967f6b09e6f2d9c6d8b10"
#define MFS_ENTER_SITE 0x10f1974u          /* its call of the presentation, for each cutscene of the list */
#define MFS_PASS 0x10f1a50u                /* a list's pass: ends a stopped cutscene, the presentation's end inside */
#define MFS_PASS_SIZE 0x6aeu
#define MFS_PASS_SHA "be0843248361e2291e0adb7f3e9db3274e19a9a47e572d52cd93908fdd2e3774"
#define MFS_LEAVE_SITE 0x10f1c00u          /* mov edx,2: the end's first instruction, after cmp [r15+0x4e9],0; je 0x10f1d9f */
#define MFS_LEAVE_RESUME 0x10f1c05u
#define MFS_DONE_SITE 0x10f1d98u           /* mov [r15+0x450],rdi: the end's last instruction */
#define MFS_DONE_RESUME 0x10f1d9fu
#define MFS_LISTENER 0x9f9ee0u             /* the listener's camera */
#define MFS_LISTENER_SIZE 0xa7u
#define MFS_LISTENER_SHA "88edccf45ac7987d3eedf7fb58c8f94855ec2b6edf58e1fa9392095f99d3df9d"
#define MFS_LISTENER_SITE 0x9f9f42u        /* call 0x10f2110 (rcx: the manager; r9..r11 live across it) */
#define MFS_LISTENER_RESUME 0x9f9f47u
#define MFS_SELECT 0x10f2110u              /* the camera of the first cutscene with a letterbox or cinematic mode */
#define MFS_SELECT_SIZE 0x64u
#define MFS_SELECT_SHA "5734bf374c4abffa2a023492e09e69d4ccbd20f660f9c38aea6c2dcb308c46cb"
#define MFS_INSERT 0xa4deb0u               /* an activity tree's entry for a key (tree, &key): {int priority, byte on} */
#define MFS_INSERT_SIZE 0x7fu
#define MFS_INSERT_SHA "a2a6d54cc2387f5182c7029a202a34780c37a7b1efc5bcd5bae4efa5e93a80b0"
#define MFS_RECOMPUTE 0x9829f0u            /* a camera's +0x870 from its tree's entry of the highest priority */
#define MFS_RECOMPUTE_SIZE 0xc3u
#define MFS_RECOMPUTE_SHA "3f0e4000d842d8fce6b8338d621c20418375453423d53764796f1c6c11e2c6ab"
#define MFS_CUTSCENE_UPDATE 0x10e9a60u     /* a cutscene's update: its shots' trees +0x2d0 / +0x310, a node's +0x40 */
#define MFS_CUTSCENE_UPDATE_SIZE 0x6dbu
#define MFS_CUTSCENE_UPDATE_SHA "4b5a72fe94799b0155bd38739149c6b628b3e4c507027e47cfd5bbec06fdfc00"
#define MFS_SHOT_UPDATE 0x10d7f60u         /* a camera shot's update up to its cameras' entries (+0x18, +0x48) */
#define MFS_SHOT_UPDATE_SIZE 0x214u
#define MFS_SHOT_UPDATE_SHA "38a4b85f09197ffefabab2b6a5a48c26ed5bb2a5acb7de6919ebfde9c52096d5"
#define MFS_ANIMATED_UPDATE 0x10e22f0u     /* an animated camera shot's, the same */
#define MFS_ANIMATED_UPDATE_SIZE 0x390u
#define MFS_ANIMATED_UPDATE_SHA "7541741b4d38e2704b18e3a6a41b301b7e0962c7111a007444b22b692c57abc8"
#define MFS_OVERLAY_VTABLE 0x2bae0b8u      /* U::OverlayCamera: a shot's camera */
#define MFS_ENVIRONMENT_VTABLE 0x2bae0f8u  /* U::EnvironmentCamera: a shot's environment camera */
#define MFS_MANAGER 0x3d19900u
#define MFS_UNIVERSE 0x3d198f8u
#define MFS_PLAYER_CAMERA 0x3d0u
#define MFS_LIST 0x30u                     /* the manager's cutscenes (sentinel pointer; node[0] next, node[2] the cutscene) */
#define MFS_FINISHED 0x50u                 /* the ones that end in the next pass */
#define MFS_ID 8u
#define MFS_FULL 0x4e9u
#define MFS_LETTERBOX 0x4eau
#define MFS_CINEMATIC 0x4ebu
#define MFS_ENTERED 0x4ecu
#define MFS_SHOTS 0x2d0u                   /* the camera shots' tree (header; +8 its root) */
#define MFS_NODE_LEFT 8u
#define MFS_NODE_RIGHT 0x10u
#define MFS_NODE_SHOT 0x40u
#define MFS_SHOT_CAMERA 0x18u
#define MFS_SHOT_ENVIRONMENT 0x48u
#define MFS_SHOT_PLAYBACK 0x50u
#define MFS_PLAYBACK_CUTSCENE 0x70u
#define MFS_ACTIVITY 0x790u                /* a camera's activity tree */
#define MFS_HIDE_PRIORITY 0x7fffffffu      /* above every shot's 100 */
#define MFS_KEY_ID 0xfffffff0u             /* the tree orders by the key's +8 (an ID of a cutscene or shot elsewhere) */
#define MFS_LEAVE_STUB 0x640u              /* on the slots' near page: three stubs of PSND_STUB_ROOM and a jump */
#define MFS_DONE_STUB 0x720u
#define MFS_LISTENER_STUB 0x7e0u
#define MFS_ENTER_STUB 0x8a0u
#define MFS_RAISE_STUB 0x8c0u
#define MFS_KEY 0x10f2180u                 /* the cutscene's keys' handler (id) */
#define MFS_KEY_SIZE 0x54eu                /* with its two jump tables */
#define MFS_KEY_SHA "5aefe6a2051cfe0e200d4272c180ae4e41da5f1b9e279ed425c0bd07275b6cc7"
#define MFS_STOP_FIRST 0x10f1800u          /* stop the first abortable cutscene (+0x4ed) of the list */
#define MFS_STOP_FIRST_SIZE 0xb1u
#define MFS_STOP_FIRST_SHA "873182c2870bfb0e958db5f83630a71b4b4c2b243699b21eb6b4a7b19ba9ed08"
#define MFS_STOP_CINEMATIC 0x10f16f0u      /* stop the abortable cinematic ones */
#define MFS_STOP_CINEMATIC_SIZE 0x10bu
#define MFS_STOP_CINEMATIC_SHA "6efc74bf6b68640dfca94b023914e30008913c953ec7ecb67a39e2129e217226"
#define MFS_DEACTIVATE 0x9fbd40u           /* deactivate_cinematic_camera: the post, then the UI's event */
#define MFS_DEACTIVATE_SIZE 0x1a4u
#define MFS_DEACTIVATE_SHA "59f3459e916876907bd805fd542f9ac8b1fa2affe29df4156b5b35d783d846f7"
#define MFS_RAISE_SITE 0x9fbed0u           /* ...its call of UI::LuaEventSink_Raise (sink, &name) */
#define MFS_RAISE 0xb3af30u
#define MFS_ABORTABLE 0x4edu
#define MFS_KEY_STOP 0xa5
#define MFS_KEY_MAP 0x71
#define MFS_MAX 16u
#define MFS_CAMERAS 16u
#define MFS_LIST_MAX 256u
#define MFS_TREE_MAX 256u
typedef void (*mfs_enter_fn)(void *);
typedef void *(*mfs_insert_fn)(void *,void **);
typedef void (*mfs_recompute_fn)(void *);
typedef void *(*mfs_select_fn)(void *);
typedef void (*mfs_key_fn)(int);
typedef void (*mfs_raise_fn)(void *,void *);
typedef struct {mfs_enter_fn enter;mfs_insert_fn insert;mfs_recompute_fn recompute;mfs_select_fn select;mfs_key_fn key;mfs_raise_fn raise;} mfs_fns;
static mfs_fns mfs_fn;                     /* the image's; tests set their own */
typedef struct {unsigned char *cutscene;int id;unsigned owner,cameras;unsigned char used,counted;unsigned char *camera[MFS_CAMERAS];} mfs_t;
static mfs_t mfs[MFS_MAX];
static int mfs_armed,mfs_pins_checked,mfs_install_tried,mfs_enter_installed,mfs_leave_installed,mfs_done_installed,mfs_listener_installed,
           mfs_raise_installed;
static unsigned char mfs_enter_patched[5],mfs_leave_patched[5],mfs_done_patched[7],mfs_listener_patched[5],mfs_raise_patched[5];
static volatile LONG64 mfs_key_recorded,mfs_key_applied,mfs_key_foreign,mfs_key_unpresented,mfs_key_unfocused,mfs_key_dropped,mfs_key_masked,
                       mfs_key_raises;
static int mfs_key_foreign_now;           /* inside the apply of another slot's key: its UI event is not this client's */
static const uint16_t mfs_key_ids[]={0x71,0xa5,0x164,0x165,0x166,0x167,0x168,0x16a};
static volatile LONG64 mfs_seen,mfs_owned,mfs_hidden,mfs_shown,mfs_cameras,mfs_passes,mfs_skipped,mfs_held,mfs_listener_local,mfs_rng_moved,
                       mfs_gone,mfs_full,mfs_camera_full;
static const char *mfs_why="-";
static unsigned char *mfs_base;            /* the image, or a test region */
static uint64_t mfs_key[2]={0,(uint64_t)MFS_KEY_ID};   /* the key object every hide entry names: +8 its ID */
/* The action's mark: the cutscenes in the list before it ran (pointer and ID), on the frame thread. */
static struct {unsigned char *cutscene[MFS_MAX];int id[MFS_MAX];unsigned count,depth,valid;} mfs_mark;
static int mfs_end_held;static uint64_t mfs_end_word,*mfs_end_rng;
static const unsigned char mfs_leave_head[5]={0xba,0x02,0x00,0x00,0x00};                 /* mov edx,2 */
static const unsigned char mfs_done_head[7]={0x49,0x89,0xbf,0x50,0x04,0x00,0x00};        /* mov [r15+0x450],rdi */
static const unsigned char mfs_leave_load[3]={0x4c,0x89,0xf9};                            /* mov rcx,r15: the cutscene */
/* The end's stub tail: test al,al; jnz +0x13 (over X4's mov edx,2 and the jump back, to the jump past the block). */
static const unsigned char mfs_leave_tail[9]={0x84,0xc0,0x75,0x13,0xba,0x02,0x00,0x00,0x00};
static int mfs_live(void) {return mfs_armed && stats.active && GetCurrentThreadId()==ui_thread;}
static unsigned char *mfs_manager(void) {unsigned char *m=NULL;return mfs_base && read_local(mfs_base+MFS_MANAGER,&m,8)?m:NULL;}
/* A list of the manager (+0x30 or +0x50) holds the cutscene with this ID. */
static int mfs_listed(unsigned char *manager,unsigned list,const unsigned char *cutscene,int id) {
    unsigned char *sentinel=NULL,*node=NULL;
    if(!manager || !read_local(manager+list,&sentinel,8) || !sentinel || !read_local(sentinel,&node,8)) return 0;
    for(unsigned steps=0;node && node!=sentinel && steps<MFS_LIST_MAX;steps++) {
        unsigned char *at=NULL;int at_id=0;
        if(!read_local(node+0x10,&at,8)) return 0;
        if(at==cutscene && read_local(at+MFS_ID,&at_id,4) && at_id==id) return 1;
        if(!read_local(node,&node,8)) return 0;
    }
    return 0;
}
static mfs_t *mfs_find(const unsigned char *cutscene) {
    int id=0;
    if(!cutscene || !read_local(cutscene+MFS_ID,&id,4)) return NULL;
    for(unsigned i=0;i<MFS_MAX;i++) if(mfs[i].used && mfs[i].cutscene==cutscene && mfs[i].id==id) return &mfs[i];
    return NULL;
}
/* Another slot's: this client presents nothing of it. */
static int mfs_other(const mfs_t *e) {return e && e->owner && e->owner!=stats.slot;}
/* ---- the owner, from the action (lockstep_md_media.h mdv_action) ---- */
/* Before X4's action: the list as it is. 1 when the mark was taken (the module is live, not inside another action). */
static int mfs_before(void) {
    unsigned char *manager=NULL,*sentinel=NULL,*node=NULL;
    if(!mfs_live() || mfs_mark.depth) return 0;
    mfs_mark.depth=1;mfs_mark.count=0;mfs_mark.valid=0;
    if(!(manager=mfs_manager()) || !read_local(manager+MFS_LIST,&sentinel,8) || !sentinel || !read_local(sentinel,&node,8)) return 1;
    for(unsigned steps=0;node && node!=sentinel && steps<MFS_LIST_MAX;steps++) {
        unsigned char *at=NULL;int id=0;
        if(!read_local(node+0x10,&at,8) || !at || !read_local(at+MFS_ID,&id,4)) return 1;
        if(mfs_mark.count<MFS_MAX) {mfs_mark.cutscene[mfs_mark.count]=at;mfs_mark.id[mfs_mark.count]=id;++mfs_mark.count;}
        else return 1;   /* a list this long is no action's to tell apart: nothing noted */
        if(!read_local(node,&node,8)) return 1;
    }
    mfs_mark.valid=1;return 1;
}
/* After it: every full-screen cutscene it put into the list is the owner's (0: everybody's). */
static void mfs_after(int marked,unsigned owner) {
    unsigned char *manager=NULL,*sentinel=NULL,*node=NULL;
    if(!marked) return;
    mfs_mark.depth=0;
    if(!mfs_mark.valid || !(manager=mfs_manager()) || !read_local(manager+MFS_LIST,&sentinel,8) || !sentinel || !read_local(sentinel,&node,8)) return;
    for(unsigned steps=0;node && node!=sentinel && steps<MFS_LIST_MAX;steps++) {
        unsigned char *at=NULL,full=0;int id=0,known=0;
        if(!read_local(node+0x10,&at,8) || !at || !read_local(at+MFS_ID,&id,4) || !read_local(at+MFS_FULL,&full,1)) return;
        for(unsigned i=0;i<mfs_mark.count && !known;i++) known=mfs_mark.cutscene[i]==at && mfs_mark.id[i]==id;
        if(!known && full && !mfs_find(at)) {
            mfs_t *e=NULL;
            for(unsigned i=0;i<MFS_MAX && !e;i++) if(!mfs[i].used) e=&mfs[i];
            InterlockedIncrement64(&mfs_seen);
            if(!e) InterlockedIncrement64(&mfs_full);   /* everybody's, as before */
            else {
                memset(e,0,sizeof(*e));e->cutscene=at;e->id=id;e->owner=owner<=PILOT_SLOTS?owner:0;e->used=1;
                if(e->owner) InterlockedIncrement64(&mfs_owned);
            }
        }
        if(!read_local(node,&node,8)) return;
    }
}
/* ---- another slot's scene on this client ---- */
static int mfs_has_camera(const mfs_t *e,const unsigned char *camera) {
    for(unsigned i=0;i<e->cameras;i++) if(e->camera[i]==camera) return 1;
    return 0;
}
/* One camera of the scene draws nothing here: an entry above the shot's own in its activity tree. */
static void mfs_hide_camera(mfs_t *e,unsigned char *camera,uintptr_t vtable) {
    uintptr_t at=0;uint64_t word=0;
    if(!camera || mfs_has_camera(e,camera) || !read_local(camera,&at,8) || at!=(uintptr_t)(mfs_base+vtable)) return;
    if(e->cameras>=MFS_CAMERAS) {InterlockedIncrement64(&mfs_camera_full);return;}
    uint64_t *const rng=pres_rng_hold(&word);
    void *key=(void*)mfs_key;
    unsigned char *entry=mfs_fn.insert(camera+MFS_ACTIVITY,&key);
    if(entry) {const uint32_t priority=MFS_HIDE_PRIORITY;memcpy(entry,&priority,4);entry[4]=0;mfs_fn.recompute(camera);}
    if(rng && *rng!=word) {InterlockedIncrement64(&mfs_rng_moved);*rng=word;}
    if(!entry) return;
    e->camera[e->cameras++]=camera;InterlockedIncrement64(&mfs_cameras);
}
/* Every camera the scene's shots made so far (the shots' tree, any order; a shot is the scene's own by its playback). */
static void mfs_hide_cameras(mfs_t *e) {
    unsigned char *stack[64],*node=NULL;unsigned depth=0,visited=0;
    if(!read_local(e->cutscene+MFS_SHOTS+8,&node,8) || !node) return;
    stack[depth++]=node;
    while(depth && visited<MFS_TREE_MAX) {
        unsigned char *n=stack[--depth],*left=NULL,*right=NULL,*shot=NULL,*playback=NULL,*owner=NULL,*camera=NULL,*environment=NULL;
        ++visited;
        if(!read_local(n+MFS_NODE_LEFT,&left,8) || !read_local(n+MFS_NODE_RIGHT,&right,8) || !read_local(n+MFS_NODE_SHOT,&shot,8)) continue;
        if(shot && read_local(shot+MFS_SHOT_PLAYBACK,&playback,8) && playback && read_local(playback+MFS_PLAYBACK_CUTSCENE,&owner,8) &&
           owner==e->cutscene) {
            if(read_local(shot+MFS_SHOT_CAMERA,&camera,8)) mfs_hide_camera(e,camera,MFS_OVERLAY_VTABLE);
            if(read_local(shot+MFS_SHOT_ENVIRONMENT,&environment,8)) mfs_hide_camera(e,environment,MFS_ENVIRONMENT_VTABLE);
        }
        if(left && depth<64) stack[depth++]=left;
        if(right && depth<64) stack[depth++]=right;
    }
}
/* The call at 0x10f1974, for every cutscene of the list in every frame. */
__attribute__((noinline)) static void mfs_enter(void *cutscene) {
    unsigned char *const c=cutscene;mfs_t *const e=mfs_live()?mfs_find(c):NULL;
    if(e && mfs_other(e)) {
        unsigned char full=0,entered=0;
        InterlockedIncrement64(&mfs_passes);
        mfs_hide_cameras(e);
        if(read_local(c+MFS_FULL,&full,1) && full && read_local(c+MFS_ENTERED,&entered,1) && !entered) {
            c[MFS_ENTERED]=1;   /* as X4's presentation leaves it */
            if(!e->counted) {e->counted=1;InterlockedIncrement64(&mfs_hidden);}
        }
        return;
    }
    if(e && e->owner) {   /* the owner's: X4's presentation, which runs on this client alone */
        uint64_t word=0;unsigned char entered=1;
        if(!e->counted && read_local(c+MFS_ENTERED,&entered,1) && !entered) {e->counted=1;InterlockedIncrement64(&mfs_shown);}
        uint64_t *const rng=pres_rng_hold(&word);
        mfs_fn.enter(cutscene);
        if(rng && *rng!=word) {InterlockedIncrement64(&mfs_rng_moved);*rng=word;}
        return;
    }
    mfs_fn.enter(cutscene);
}
/* The end's first instruction (stub at 0x10f1c00, rcx: the cutscene): 1 jumps over X4's end of a presentation this client
 * never made; 0 runs it (the owner's with the RNG word held until the end's last instruction). The entry goes: the
 * cutscene's destructor follows. */
__attribute__((noinline)) static uintptr_t mfs_leave(void *cutscene) {
    mfs_t *const e=mfs_live()?mfs_find(cutscene):NULL;
    if(!e) return 0;
    const int other=mfs_other(e),owned=e->owner!=0;
    memset(e,0,sizeof(*e));
    if(other) {InterlockedIncrement64(&mfs_skipped);return 1;}
    if(owned && !mfs_end_held) {mfs_end_rng=pres_rng_hold(&mfs_end_word);mfs_end_held=1;InterlockedIncrement64(&mfs_held);}
    return 0;
}
/* The end's last instruction (stub at 0x10f1d98): the RNG word back. */
__attribute__((noinline)) static void mfs_leave_done(void) {
    if(!mfs_end_held) return;
    mfs_end_held=0;
    if(mfs_end_rng && *mfs_end_rng!=mfs_end_word) {InterlockedIncrement64(&mfs_rng_moved);*mfs_end_rng=mfs_end_word;}
    mfs_end_rng=NULL;
}
/* The listener's camera asks 0x10f2110 (stub at 0x9f9f42): none for another slot's scene, so the listener stays on this
 * client's own camera. Read only: any thread. */
__attribute__((noinline)) static void *mfs_listener(void *manager) {
    void *const camera=mfs_fn.select(manager);
    unsigned char *sentinel=NULL,*node=NULL;
    if(!camera || !mfs_armed || !stats.active || !read_local((unsigned char*)manager+MFS_LIST,&sentinel,8) || !sentinel || !read_local(sentinel,&node,8))
        return camera;
    for(unsigned steps=0;node && node!=sentinel && steps<MFS_LIST_MAX;steps++) {   /* the cutscene 0x10f2110 took */
        unsigned char *at=NULL,letterbox=0,cinematic=0;
        if(!read_local(node+0x10,&at,8)) return camera;
        if(at && read_local(at+MFS_LETTERBOX,&letterbox,1) && read_local(at+MFS_CINEMATIC,&cinematic,1) && (letterbox || cinematic)) {
            if(!mfs_other(mfs_find(at))) return camera;
            InterlockedIncrement64(&mfs_listener_local);return NULL;
        }
        if(!read_local(node,&node,8)) return camera;
    }
    return camera;
}
/* ---- the cutscene's keys, through the stock record (lockstep_pilot_stock.h) ---- */
/* One of the handler's ids while the module runs: the same answer on every client (the record's verdict). */
static int mfs_key_replayed(unsigned kind,int id) {
    if(!mfs_armed || kind!=PSTK_ACTION) return 0;
    for(unsigned i=0;i<sizeof(mfs_key_ids)/sizeof(*mfs_key_ids);i++) if(mfs_key_ids[i]==id) return 1;
    return 0;
}
/* This client's dispatch of the id (map: the actions' multimap): the other handlers run here, as a presentation id's;
 * X4's handler is the record's, when this client registered it (it presents the scene). 1: handled. */
static int mfs_key_local(unsigned char *map,int id,int scripted) {
    pnat_node *upper=NULL,*n=map?pnat_first((pnat_node*)map,id,&upper):NULL;int ours=0;
    if(!scripted && !pstk_focused()) {InterlockedIncrement64(&mfs_key_unfocused);return 1;}   /* the focus rule: an action engages */
    pstk_local_note(PSTK_ACTION,id,scripted);
    if(pres_slot && pilot_slots[pres_slot-1].dead && !pres_has_seat()) {InterlockedIncrement64(&mfs_key_dropped);return 1;}
    const int presented=pres_local_enter();
    InterlockedIncrement(&uirep_dispatch_depth);
    while(n && n!=upper) {
        pnat_node *next=pnat_next(n);
        if(n->handler==(void*)mfs_fn.key) ours=1;else pstk_call(n->handler,PSTK_ACTION,id,0);
        n=next;
    }
    InterlockedDecrement(&uirep_dispatch_depth);
    pres_local_leave(presented);
    if(!ours) {InterlockedIncrement64(&mfs_key_unpresented);return 1;}   /* no scene of this client's takes it */
    if(!pstk_capturing) {InterlockedIncrement64(&mfs_key_dropped);return 1;}
    if(pstk_record_event(PSTK_ACTION,(unsigned)id,0)) InterlockedIncrement64(&mfs_key_recorded);
    return 1;
}
/* The record's id, applied on every client in its slot's replay (pstk_scope_slot: 0 in the canonical one, slot 1's). */
static void mfs_key_apply(int id) {
    const unsigned slot=pstk_scope_slot?pstk_scope_slot:1u;
    unsigned char *masked[MFS_MAX],*const manager=mfs_manager();unsigned n=0;
    if(!mfs_fn.key) return;
    if(id==MFS_KEY_STOP || id==MFS_KEY_MAP)   /* a stop takes the first abortable scene: not another slot's */
        for(unsigned i=0;i<MFS_MAX;i++) {
            unsigned char *const c=mfs[i].cutscene;unsigned char abortable=0;
            if(!mfs[i].used || !mfs[i].owner || mfs[i].owner==slot || !mfs_listed(manager,MFS_LIST,c,mfs[i].id) ||
               !read_local(c+MFS_ABORTABLE,&abortable,1) || !abortable) continue;
            c[MFS_ABORTABLE]=0;masked[n++]=c;
        }
    const int foreign=slot!=stats.slot;
    mfs_key_foreign_now=foreign;
    mfs_fn.key(id);
    mfs_key_foreign_now=0;
    for(unsigned i=0;i<n;i++) masked[i][MFS_ABORTABLE]=1;
    InterlockedIncrement64(&mfs_key_applied);
    if(foreign) InterlockedIncrement64(&mfs_key_foreign);
    if(n) InterlockedExchangeAdd64(&mfs_key_masked,(LONG64)n);
}
/* 0x9fbd40's call of the UI's event (stub at 0x9fbed0): not raised here for another slot's key. */
__attribute__((noinline)) static void mfs_raise(void *sink,void *name) {
    if(mfs_key_foreign_now && GetCurrentThreadId()==ui_thread) {InterlockedIncrement64(&mfs_key_raises);return;}
    mfs_fn.raise(sink,name);
}
/* ---- arm, install, the frame, restore ---- */
static int mfs_pins(void) {
    if(!mfs_pins_checked) {
        int ok=hash_ok(image+MFS_ENTER,MFS_ENTER_SIZE,MFS_ENTER_SHA) && hash_ok(image+MFS_UPDATE,MFS_UPDATE_SIZE,MFS_UPDATE_SHA) &&
            hash_ok(image+MFS_PASS,MFS_PASS_SIZE,MFS_PASS_SHA) && hash_ok(image+MFS_LISTENER,MFS_LISTENER_SIZE,MFS_LISTENER_SHA) &&
            hash_ok(image+MFS_SELECT,MFS_SELECT_SIZE,MFS_SELECT_SHA) && hash_ok(image+MFS_INSERT,MFS_INSERT_SIZE,MFS_INSERT_SHA) &&
            hash_ok(image+MFS_RECOMPUTE,MFS_RECOMPUTE_SIZE,MFS_RECOMPUTE_SHA) &&
            hash_ok(image+MFS_CUTSCENE_UPDATE,MFS_CUTSCENE_UPDATE_SIZE,MFS_CUTSCENE_UPDATE_SHA) &&
            hash_ok(image+MFS_SHOT_UPDATE,MFS_SHOT_UPDATE_SIZE,MFS_SHOT_UPDATE_SHA) &&
            hash_ok(image+MFS_ANIMATED_UPDATE,MFS_ANIMATED_UPDATE_SIZE,MFS_ANIMATED_UPDATE_SHA) &&
            hash_ok(image+MFS_KEY,MFS_KEY_SIZE,MFS_KEY_SHA) && hash_ok(image+MFS_STOP_FIRST,MFS_STOP_FIRST_SIZE,MFS_STOP_FIRST_SHA) &&
            hash_ok(image+MFS_STOP_CINEMATIC,MFS_STOP_CINEMATIC_SIZE,MFS_STOP_CINEMATIC_SHA) &&
            hash_ok(image+MFS_DEACTIVATE,MFS_DEACTIVATE_SIZE,MFS_DEACTIVATE_SHA);
        mfs_pins_checked=ok?1:-1;
    }
    return mfs_pins_checked==1;
}
static int (*mfs_pin)(void)=mfs_pins;      /* tests replace it */
/* gate.md_fullscreen(1): before the first tick, after gate.md_media(1). The sites change in the probe's frame, once the
 * slots' near page is there (mfs_frame); until then X4's own way. */
static int mfs_arm(void) {
    if(mfs_armed) return stats.active!=0;   /* an identical re-arm after a loading-UI reload */
    if(!image || !stats.active) {mfs_why="not_started";return 0;}
    if(stats.issued || GetCurrentThreadId()!=ui_thread) {mfs_why="late_or_thread";return 0;}
    if(!mdv_armed) {mfs_why="md_media";return 0;}
    if(!mfs_pin()) {mfs_why="pins";return 0;}
    mfs_base=image;
    mfs_fn.enter=(mfs_enter_fn)(image+MFS_ENTER);mfs_fn.insert=(mfs_insert_fn)(image+MFS_INSERT);
    mfs_fn.recompute=(mfs_recompute_fn)(image+MFS_RECOMPUTE);mfs_fn.select=(mfs_select_fn)(image+MFS_SELECT);
    mfs_fn.key=(mfs_key_fn)(image+MFS_KEY);mfs_fn.raise=(mfs_raise_fn)(image+MFS_RAISE);
    mfs_key_recorded=mfs_key_applied=mfs_key_foreign=mfs_key_unpresented=mfs_key_unfocused=mfs_key_dropped=mfs_key_masked=mfs_key_raises=0;
    mfs_key_foreign_now=0;
    memset(mfs,0,sizeof(mfs));memset(&mfs_mark,0,sizeof(mfs_mark));mfs_install_tried=0;mfs_end_held=0;mfs_end_rng=NULL;
    mfs_seen=mfs_owned=mfs_hidden=mfs_shown=mfs_cameras=mfs_passes=mfs_skipped=mfs_held=mfs_listener_local=mfs_rng_moved=0;
    mfs_gone=mfs_full=mfs_camera_full=0;
    mfs_armed=1;mfs_why="-";return 1;
}
/* The end's stub: X4's end of the presentation or the jump past it (mfs_leave's answer). */
static unsigned mfs_leave_stub(unsigned char *p,unsigned room) {
    const unsigned n=psnd_stub(p,room,mfs_leave_load,sizeof(mfs_leave_load),(const void*)mfs_leave,PSND_OUT_RAX,0,mfs_leave_tail,
                               sizeof(mfs_leave_tail),pnat_base+MFS_LEAVE_RESUME);
    if(!n || n+14u>room) return 0;
    return n+pview_abs_jump(p+n,pnat_base+MFS_DONE_SITE);
}
static int mfs_leave_install(void) {
    DWORD old,ignored;unsigned char stub[PSND_STUB_ROOM];
    if(!pslot_page || memcmp(pnat_base+MFS_LEAVE_SITE,mfs_leave_head,sizeof(mfs_leave_head))) return 0;
    const unsigned n=mfs_leave_stub(stub,sizeof(stub));
    if(!n || !VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return 0;
    memset(pslot_page+MFS_LEAVE_STUB,0xcc,PSND_STUB_ROOM);memcpy(pslot_page+MFS_LEAVE_STUB,stub,n);
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    const int32_t rel=(int32_t)((intptr_t)(pslot_page+MFS_LEAVE_STUB)-(intptr_t)(pnat_base+MFS_LEAVE_SITE+5));
    mfs_leave_patched[0]=0xe9;memcpy(mfs_leave_patched+1,&rel,4);
    return pview_cas(MFS_LEAVE_SITE,mfs_leave_head,mfs_leave_patched,5);
}
/* Everything of ours back that still holds what we wrote; 1 when nothing is left. */
static int mfs_uninstall(void) {
    if(mfs_enter_installed && pres_site_uninstall(MFS_ENTER_SITE,MFS_ENTER,mfs_enter_patched)) mfs_enter_installed=0;
    if(mfs_raise_installed && pres_site_uninstall(MFS_RAISE_SITE,MFS_RAISE,mfs_raise_patched)) mfs_raise_installed=0;
    if(mfs_leave_installed && pview_cas(MFS_LEAVE_SITE,mfs_leave_patched,mfs_leave_head,5)) mfs_leave_installed=0;
    if(mfs_done_installed && pview_cas(MFS_DONE_SITE,mfs_done_patched,mfs_done_head,7)) mfs_done_installed=0;
    if(mfs_listener_installed) {
        unsigned char original[5]={0xe8,0,0,0,0};const int32_t rel=(int32_t)(MFS_SELECT-(MFS_LISTENER_SITE+5u));memcpy(original+1,&rel,4);
        if(pview_cas(MFS_LISTENER_SITE,mfs_listener_patched,original,5)) mfs_listener_installed=0;
    }
    return !(mfs_enter_installed || mfs_leave_installed || mfs_done_installed || mfs_listener_installed || mfs_raise_installed);
}
/* In the probe's frame (the frame thread, outside the manager's update and outside the listener's callers). The end's
 * last instruction first (the end's stub jumps there), then the end, the listener, the presentation's call: all or none. */
static void mfs_install(void) {
    if(mfs_install_tried || !pslot_page) return;
    mfs_install_tried=1;
    if(!psnd_site_install(MFS_DONE_SITE,mfs_done_head,7,MFS_DONE_STUB,mfs_leave_load,0,(const void*)mfs_leave_done,PSND_OUT_NONE,0,mfs_done_head,7,
                          MFS_DONE_RESUME,mfs_done_patched)) {mfs_why="done_site";return;}
    mfs_done_installed=1;
    if(!mfs_leave_install()) {mfs_uninstall();mfs_why="leave_site";return;}
    mfs_leave_installed=1;
    {   unsigned char original[5]={0xe8,0,0,0,0};const int32_t rel=(int32_t)(MFS_SELECT-(MFS_LISTENER_SITE+5u));memcpy(original+1,&rel,4);
        if(!psnd_site_install(MFS_LISTENER_SITE,original,5,MFS_LISTENER_STUB,mfs_leave_load,0,(const void*)mfs_listener,PSND_OUT_RAX,0,mfs_leave_load,0,
                              MFS_LISTENER_RESUME,mfs_listener_patched)) {mfs_uninstall();mfs_why="listener_site";return;}
        mfs_listener_installed=1;
    }
    if(!pres_site_install(MFS_RAISE_SITE,MFS_RAISE,MFS_RAISE_STUB,(void*)mfs_raise,mfs_raise_patched)) {mfs_uninstall();mfs_why="raise_site";return;}
    mfs_raise_installed=1;
    if(!pres_site_install(MFS_ENTER_SITE,MFS_ENTER,MFS_ENTER_STUB,(void*)mfs_enter,mfs_enter_patched)) {mfs_uninstall();mfs_why="enter_site";return;}
    mfs_enter_installed=1;
}
/* Once per step, in the probe's frame, on every client (from gate.md_media_frame): the install, and the entries whose
 * cutscene is in neither list leave the table (a cutscene the manager dropped without its end). Answers the live count. */
static int mfs_frame(void) {
    unsigned live=0;
    if(!mfs_live()) return 0;
    mfs_install();
    unsigned char *const manager=mfs_manager();
    for(unsigned i=0;i<MFS_MAX;i++) {
        if(!mfs[i].used) continue;
        if(!mfs_listed(manager,MFS_LIST,mfs[i].cutscene,mfs[i].id) && !mfs_listed(manager,MFS_FINISHED,mfs[i].cutscene,mfs[i].id)) {
            memset(&mfs[i],0,sizeof(mfs[i]));InterlockedIncrement64(&mfs_gone);continue;
        }
        ++live;
    }
    return (int)live;
}
/* The session's end: X4's own way again. */
static void mfs_restore(void) {
    if(!mfs_armed) return;
    if(!mfs_uninstall()) mfs_why="restore";
    mfs_leave_done();
    memset(mfs,0,sizeof(mfs));memset(&mfs_mark,0,sizeof(mfs_mark));mfs_armed=0;
}
static int mfs_status(char *out,size_t capacity) {
    unsigned live=0,owned=0,other=0;
    for(unsigned i=0;i<MFS_MAX;i++) if(mfs[i].used) {++live;owned+=mfs[i].owner!=0;other+=mfs_other(&mfs[i]);}
    int n=snprintf(out,capacity,"armed=%d installed=%d/%d/%d/%d/%d live=%u/%u/%u seen=%lld owned=%lld hidden=%lld shown=%lld cameras=%lld passes=%lld "
        "ends=%lld/%lld listener=%lld rng_moved=%lld gone=%lld full=%lld/%lld keys=%lld/%lld/%lld/%lld/%lld/%lld/%lld/%lld why=%s",mfs_armed,
        mfs_enter_installed,mfs_leave_installed,mfs_done_installed,mfs_listener_installed,mfs_raise_installed,live,owned,other,(long long)mfs_seen,
        (long long)mfs_owned,(long long)mfs_hidden,(long long)mfs_shown,(long long)mfs_cameras,(long long)mfs_passes,(long long)mfs_skipped,
        (long long)mfs_held,(long long)mfs_listener_local,(long long)mfs_rng_moved,(long long)mfs_gone,(long long)mfs_full,(long long)mfs_camera_full,
        (long long)mfs_key_recorded,(long long)mfs_key_applied,(long long)mfs_key_foreign,(long long)mfs_key_unpresented,(long long)mfs_key_unfocused,
        (long long)mfs_key_dropped,(long long)mfs_key_masked,(long long)mfs_key_raises,mfs_why);
    return n>0 && (size_t)n<capacity?n:0;
}

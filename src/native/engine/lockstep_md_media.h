/* A mission's picture on the target monitor (play_cutscene targetmonitor="true": the face of "Incoming call", a
 * mission's camera on a ship): started by the simulation, shown on its owner's client. native_gate_md_media, a session
 * default; Windows gate; needs the pilot slots' near page; no stop reason of its own. Included by lockstep_ui_replay.h.
 *
 * What X4 does (build 9.00, read from the image):
 *   Cutscenes::PlayCutsceneAction 0x10d25e0, its target monitor branch (0x10d2aac), only makes a Notification (0x688
 *   bytes, constructor 0x156980 called at 0x10d2adb: +8 its ID, +0x10 priority 10, +0x1c a timeout in seconds, +0x78 a
 *   cutscene descriptor, +0x80 an interaction), tells the UI its ID and answers MD the handle 2:<ID>. The descriptor
 *   (0xb0 bytes): +0 the key, +0x20 and +0x60 the parameters, +0xa0 the running cutscene's ID, +0xa4 the notification's.
 *   The UI's monitor script queues the notification and, when its monitor is free, calls StartCutscene(descriptor, its
 *   render target): Lua 0xb12e20 ends the manager's current notification cutscene (manager +0xa8, reason 3) and starts
 *   this one through 0x10f1310 with the handle. The manager's next update posts U::CutsceneStartedEvent, and the stock
 *   dialogue library (md/lib_dialog.xml Cutscene_Started) waits for exactly that event before the first line is spoken.
 *   The picture ends when the notification goes: stop_cutscene on the handle removes it (0x157860) and the
 *   Notification's destructor 0x156c50 stops the descriptor's cutscene (0x10f1630). The UI removes it too, on its own:
 *   at the notification's timeout, when its monitor changes mode and asks for everything again, when the picture stops.
 * So when a mission's dialogue starts, and whether it starts, was each client's UI. Every client's UI had to do the same
 * thing in the same frame, which is why every client showed every player's call (playtest reports of 12 and 14 Nov), and
 * a client whose monitor was busy or switched off would have started the dialogue later than the others.
 *
 * With this module the picture is the simulation's:
 *   start    the action's notification (noted at the constructor's call, read after the action) starts its cutscene at
 *            once, on every client, as the UI's StartCutscene would: the manager's current notification cutscene ends
 *            with reason 3, the new one starts with the handle and the monitor's render target and its ID goes into the
 *            descriptor. Nothing asks a UI. A notification with an interaction (a fleet ship that asks its player for
 *            orders, aiscripts/player.interaction.xml: found an abandoned ship, attacked, halted by the police) is
 *            started the same way since 19 Nov: left to each client's UI its picture began and ended at ticks of the
 *            client's own, the script waits for both events, and two clients parted 160 ticks after a frigate of the
 *            player's saw an abandoned ship (tools/dev/action_tests.py claim --ask, run p2). The answer itself is the
 *            UI's RaisePlayerInteractionEvent(interaction ID) [0xb0d970 -> 0x157950(notification manager, &error, ID):
 *            the descriptor by its ID (0x151d10, a handle table of its own), a U::PlayerInteractionEvent with the
 *            descriptor's two parameters posted to the player entity]. Slot 1's is captured by the conversation family
 *            (lockstep_ui_missions.h UIMIS_C_ANSWER) as the notification's sequence number here (mdv_interaction_seq:
 *            started notifications are counted in the simulation's order, the same on every client; an interaction ID
 *            is each client's own, the UI makes descriptors for whatever its player targets), and every client raises
 *            the event at the apply tick (mdv_answer). Slots 2+ stay refused: the conversation that follows the answer
 *            is slot 1's, as every conversation is.
 *   owner    the slot whose own cue's frame makes it (lockstep_mission_owners.h mpp_show_slot) owns it; made in nobody's
 *            frame it is everybody's, as before.
 *   UI       four Lua functions of the UI answer here first (their entries jump to mdv_lua_* in lockstep_gate.c; the
 *            first instructions run from trampolines on the slots' near page). The monitor script has its own
 *            environment: globals wrapped by the probe never reached it (the first run, 15 Nov).
 *            GetNotificationPriority answers nothing for another player's notification ("removed already" to the
 *            script), so that client's monitor never learns of it; StartCutscene answers the running cutscene's ID for
 *            a descriptor of ours and starts nothing; StopCutscene and ReleaseNotification do nothing for ours. The
 *            owner's monitor shows the picture and its caption and plays its sound; the others keep their target
 *            display. Every other caller and every other notification: X4's own function.
 *   end      the script's stop_cutscene, as before. A timeout is counted here in game time from the start and removes
 *            the notification on every client at one tick (gate.md_media_frame, the probe's frame). The ID the UI says
 *            it displays (notification manager +0x50, what AbortCurrentNotification removes) is cleared there when it
 *            is one of ours.
 * One notification cutscene at a time stays X4's rule: a second call ends the first one's picture (its lines go on,
 * on their owner's client). Not covered: the owner's monitor that takes the notification up after its ready event (it
 * shows the caption over noise), a joiner during a call (the join package does not carry notifications), full-screen
 * cutscenes of a mission (they still play on every client), the answer of slots 2+ to a notification's question. */
#define MDV_ACTION_VTABLE 0x2c6d730u     /* Cutscenes::PlayCutsceneAction */
#define MDV_ACTION 0x10d25e0u            /* ...its execute (vtable slot 3) */
#define MDV_ACTION_SIZE 0xe7eu
#define MDV_ACTION_SHA "91645059656dae8e72408cf2bd99480397fae10aa70a772e42c7aa750cd28228"
#define MDV_SLOT 0x18u
#define MDV_MAKE_SITE 0x10d2adbu         /* the target monitor branch: the call of the Notification's constructor */
#define MDV_MAKE 0x156980u
#define MDV_MAKE_STUB 0x300u             /* on the slots' near page, after the suit's zone (0x1c0, 0x1e0) */
#define MDV_LUA_START 0xb12e20u          /* the UI's StartCutscene: what mdv_begin does in its place */
#define MDV_LUA_START_SIZE 0x39au
#define MDV_LUA_START_SHA "1e98264a2c335829a0329a707ec46ac368ebc96002fa6d4d1cdaa2694f2e6d81"
#define MDV_START 0x10f1310u             /* the manager's start */
#define MDV_START_SIZE 0x1edu
#define MDV_START_SHA "d1436ac5b74967635bd85864495da6879483fa3a376c2f532a733c7d15cd1562"
#define MDV_STOP 0x10f1630u              /* the manager's stop by cutscene ID (reason 1) */
#define MDV_STOP_SIZE 0xbdu
#define MDV_STOP_SHA "2ef41404455b430fa83cf5a3b52bb21d06ed2d6e82c57e391b344bf529fe512f"
#define MDV_NOTE_END 0x156c50u           /* the Notification's destructor: stops its descriptor's cutscene */
#define MDV_NOTE_END_SIZE 0x90u
#define MDV_NOTE_END_SHA "5c59cfbe43b1fc7cae72a016f4454579ad2a6691d9d9806187896c935dca2b43"
#define MDV_FIND 0x151dc0u               /* a notification by its ID */
#define MDV_FIND_SIZE 0xa8u
#define MDV_FIND_SHA "0f0c3f186afa5f3722b525be087f3e39f8e83ade0b1cb344bbe55887d298b1c3"
#define MDV_REMOVE 0x157860u             /* (notification manager, &ID): the notification goes, its event is posted */
#define MDV_REMOVE_SIZE 0xe7u
#define MDV_REMOVE_SHA "f2d96dc21ef2e4d3a898426256b9c703289b55b47c25e5de92950fa4ac989a02"
#define MDV_TARGET 0x317ae0u             /* a render target's ID from its name */
#define MDV_TARGET_SIZE 0x2edu
#define MDV_TARGET_SHA "5081615ccb68b6d00fd8e076d41d9ee01e073b6ead694d1909c953ef4e5061d8"
#define MDV_INTERACTION 0x151d10u        /* an interaction descriptor by its ID; a notification's +0x80 is one, its ID at +8 */
#define MDV_INTERACTION_SIZE 0xa8u
#define MDV_INTERACTION_SHA "0fdaca3ec9a92020c552ff89f3d72b22522b02cabb410df54d2b55a33cb5d6bc"
#define MDV_RAISE 0x157950u              /* (notification manager, std::string *error, interaction ID): the player's answer */
#define MDV_RAISE_SIZE 0x1e7u
#define MDV_RAISE_SHA "c183a931f80daac74542de5acd28adf4dd1a7c2148327508af9ca446f5515dc1"
#define MDV_MANAGER 0x3d19900u           /* the cutscene manager: +0x30 its cutscenes, +0xa0 on, +0xa8 the notification's, +0xc0 a mode */
#define MDV_NOTES 0x6cf1798u             /* the notification manager: +0x50 the ID the UI says it displays */
#define MDV_MAX 16u
#define MDV_LIST_MAX 256u
#define MDV_UI 4u                        /* the UI's Lua functions: priority, start, stop, release */
typedef struct lua_State lua_State;
typedef int (*mdv_lua_fn)(lua_State *);
typedef void (*mdv_action_fn)(void *,void *,void *);
typedef void *(*mdv_make_fn)(void *);
typedef void *(*mdv_start_fn)(void *,void *,int,int,void *,void *,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
typedef void (*mdv_stop_fn)(void *,int);
typedef void *(*mdv_find_fn)(int);
typedef void (*mdv_remove_fn)(void *,int *);
typedef void *(*mdv_target_fn)(uint64_t *,const void *,int);
typedef void *(*mdv_interaction_fn)(int);
typedef void *(*mdv_raise_fn)(void *,void *,int);
typedef struct {mdv_action_fn action;mdv_make_fn make;mdv_start_fn start;mdv_stop_fn stop;mdv_find_fn find;mdv_remove_fn remove;
    mdv_target_fn target;int (*now)(double *);mdv_interaction_fn interaction;mdv_raise_fn raise;} mdv_fns;
static mdv_fns mdv_fn;                   /* the image's; tests set their own */
static unsigned char *mdv_base;          /* the image, or a test region */
typedef struct {unsigned char *note,*desc;double end;int id,cutscene;unsigned owner,seq;unsigned char used;} mdv_t;
static mdv_t mdv[MDV_MAX];
static unsigned mdv_seq;                 /* started notifications, in the simulation's order: the same on every client */
static volatile LONG64 mdv_answers,mdv_answers_gone;
static int mdv_armed,mdv_slot_installed,mdv_site_installed,mdv_install_tried,mdv_pins_checked;
/* The four entries: each starts with whole instructions in its first six bytes (push rbx; sub rsp,0x20 or four pushes). */
static const struct {const char *name;uint32_t rva,size;const char *sha;unsigned char head[6];unsigned stub,trampoline;} mdv_ui[MDV_UI]={
    {"GetNotificationPriority",0xb11310u,0x91u,"aa0874ec421914c5b88d7186c31bd5cf2aa18b210421d37c039abf6e6f833289",{0x40,0x53,0x48,0x83,0xec,0x20},0x320u,0x340u},
    {"StartCutscene",MDV_LUA_START,MDV_LUA_START_SIZE,MDV_LUA_START_SHA,{0x40,0x53,0x55,0x56,0x41,0x54},0x360u,0x380u},
    {"StopCutscene",0xb14820u,0x82u,"15cd82d00328924a3796bc11149ae8053cb99af560017ff88747092e7460f471",{0x40,0x53,0x48,0x83,0xec,0x20},0x3a0u,0x3c0u},
    {"ReleaseNotification",0xb113b0u,0x8bu,"7563e16871813571a1269769026de597f5fbf4c2e6d75f196406b23c766f6949",{0x40,0x53,0x48,0x83,0xec,0x20},0x3e0u,0x400u}};
static mdv_lua_fn mdv_ui_original[MDV_UI];   /* the trampolines; tests set their own */
static int mdv_ui_installed[MDV_UI];
static unsigned char mdv_ui_patched[MDV_UI][6];
static int mdv_lua_priority(lua_State *L),mdv_lua_start(lua_State *L),mdv_lua_stop(lua_State *L),mdv_lua_release(lua_State *L);   /* lockstep_gate.c */
static unsigned char mdv_site_patched[5];
static unsigned char *mdv_made;          /* the notification the action that runs now has made */
static volatile LONG64 mdv_actions,mdv_started,mdv_replaced,mdv_failed,mdv_interactive,mdv_full,mdv_timeouts,mdv_gone,
                       mdv_ui_starts,mdv_ui_none,mdv_ui_stops,mdv_ui_releases,mdv_ui_hidden,mdv_ui_shown;
static const char *mdv_why="-";
/* The monitor script's own render target (ui/core/lua/monitors.lua config.smallRenderTarget). */
static const char mdv_target_name[]="ui\\core\\presentations\\monitors\\monitors_recovered\\targetmonitor2-rendertarget";
static unsigned mpp_show_slot(void);     /* lockstep_mission_owners.h */
static int mdv_live(void) {return mdv_armed && stats.active && GetCurrentThreadId()==ui_thread;}
/* The cutscene is one of the manager's (its list +0x30: node[0] next, node[2] the cutscene). */
static int mdv_listed(unsigned char *manager,const unsigned char *cutscene) {
    unsigned char *sentinel=NULL,*node=NULL;
    if(!cutscene || !read_local(manager+0x30,&sentinel,8) || !sentinel || !read_local(sentinel,&node,8)) return 0;
    for(unsigned steps=0;node && node!=sentinel && steps<MDV_LIST_MAX;steps++) {
        unsigned char *at=NULL;
        if(!read_local(node+0x10,&at,8)) return 0;
        if(at==cutscene) return 1;
        if(!read_local(node,&node,8)) return 0;
    }
    return 0;
}
/* An entry whose notification is still the one it was made for; one that is gone leaves the table. */
static mdv_t *mdv_valid(mdv_t *e) {
    if(!e->used) return NULL;
    if(mdv_fn.find(e->id)==(void*)e->note) return e;
    InterlockedIncrement64(&mdv_gone);memset(e,0,sizeof(*e));return NULL;
}
/* The cutscene an entry started, while it is still the manager's notification cutscene; else 0. */
static int mdv_running(const mdv_t *e) {
    unsigned char *manager=NULL,*now=NULL;int id=0;
    if(!read_local(mdv_base+MDV_MANAGER,&manager,8) || !manager || !read_local(manager+0xa8,&now,8) || !now || !mdv_listed(manager,now) ||
       !read_local(now+8,&id,4)) return 0;
    return id==e->cutscene?id:0;
}
/* After the action: its notification's cutscene starts here, as the UI's StartCutscene 0xb12e20 starts it. */
static void mdv_begin(unsigned char *note) {
    unsigned char *desc=NULL,*interaction=NULL,*manager=NULL,*running=NULL,*cutscene=NULL;
    int id=0,mode=0,cid=0;float seconds=0;double now=0;uint64_t target=0;unsigned char on=0;mdv_t *e=NULL;
    if(!read_local(note+8,&id,4) || !id || !read_local(note+0x78,&desc,8) || !desc || !read_local(note+0x80,&interaction,8) ||
       !read_local(note+0x1c,&seconds,4)) {InterlockedIncrement64(&mdv_failed);return;}
    if(interaction) InterlockedIncrement64(&mdv_interactive);   /* the player may answer it; its picture is the simulation's all the same */
    for(unsigned i=0;i<MDV_MAX;i++) if(mdv[i].used) (void)mdv_valid(&mdv[i]);
    for(unsigned i=0;i<MDV_MAX && !e;i++) if(!mdv[i].used) e=&mdv[i];
    if(!e) {InterlockedIncrement64(&mdv_full);return;}
    if(!read_local(mdv_base+MDV_MANAGER,&manager,8) || !manager || !read_local(manager+0xa0,&on,1) || !on) {InterlockedIncrement64(&mdv_failed);return;}
    if(read_local(manager+0xa8,&running,8) && running) {
        int rid=0;
        if(mdv_listed(manager,running) && read_local(running+8,&rid,4) && rid) {
            mdv_fn.stop(manager,rid);
            if(running[0x200]) {const int reason=3;memcpy(running+0x204,&reason,4);}   /* ended by the next notification */
            InterlockedIncrement64(&mdv_replaced);
        }
        running=NULL;memcpy(manager+0xa8,&running,8);
    }
    {struct {const char *text;size_t length;} name={mdv_target_name,sizeof(mdv_target_name)-1};(void)mdv_fn.target(&target,&name,0);}
    memcpy(&mode,manager+0xc0,4);
    cutscene=mdv_fn.start(manager,desc,2,id,desc+0x20,desc+0x60,0,(uintptr_t)(unsigned)mode,(uintptr_t)target,1,0,0,0);
    memcpy(manager+0xa8,&cutscene,8);
    if(!cutscene) {InterlockedIncrement64(&mdv_failed);return;}   /* a key the manager does not know: X4 says so in its log */
    memcpy(&cid,cutscene+8,4);memcpy(desc+0xa0,&cid,4);
    memset(e,0,sizeof(*e));e->note=note;e->desc=desc;e->id=id;e->cutscene=cid;e->owner=mpp_show_slot();e->seq=++mdv_seq;
    if(seconds>0 && mdv_fn.now && mdv_fn.now(&now)) e->end=now+(double)seconds;
    e->used=1;InterlockedIncrement64(&mdv_started);
}
/* Cutscenes::PlayCutsceneAction's execute (its vtable slot). */
static void mdv_action(void *node,void *exec,void *frame) {
    const mdv_action_fn original=mdv_fn.action;
    if(!mdv_live()) {original(node,exec,frame);return;}
    unsigned char *const outer=mdv_made;mdv_made=NULL;
    InterlockedIncrement64(&mdv_actions);
    original(node,exec,frame);
    unsigned char *const made=mdv_made;mdv_made=outer;
    if(made) mdv_begin(made);
}
/* The action's call of the Notification's constructor. */
__attribute__((noinline)) static void *mdv_make(void *self) {
    void *made=mdv_fn.make(self);
    if(mdv_live()) mdv_made=made;
    return made;
}
/* ---- what the UI's four functions answer (mdv_lua_* in lockstep_gate.c, behind the Lua ABI) ---- */
/* StartCutscene(descriptor): the running cutscene's ID; 0 one of ours that runs no more (nothing starts); -1 not ours. */
static int mdv_ui_cutscene(const unsigned char *desc) {
    if(!mdv_live() || !desc) return -1;
    for(unsigned i=0;i<MDV_MAX;i++) if(mdv[i].used && mdv[i].desc==desc) {
        const mdv_t *e=mdv_valid(&mdv[i]);
        if(!e) return -1;
        const int id=mdv_running(e);
        InterlockedIncrement64(id?&mdv_ui_starts:&mdv_ui_none);return id;
    }
    return -1;
}
/* StopCutscene(ID): 1 when the cutscene is one of ours (the UI stops nothing). */
static int mdv_ui_keep(int cutscene) {
    if(!mdv_live() || !cutscene) return 0;
    for(unsigned i=0;i<MDV_MAX;i++) if(mdv[i].used && mdv[i].cutscene==cutscene && mdv_valid(&mdv[i])) {InterlockedIncrement64(&mdv_ui_stops);return 1;}
    return 0;
}
/* A notification's ID: bit 1 it is ours (ReleaseNotification removes nothing), bit 2 it is another player's (this
 * client's monitor is not told). `release` counts the first, else the second. */
static int mdv_ui_note(int id,int release) {
    if(!mdv_live() || !id) return 0;
    for(unsigned i=0;i<MDV_MAX;i++) if(mdv[i].used && mdv[i].id==id) {
        const mdv_t *e=mdv_valid(&mdv[i]);
        if(!e) return 0;
        const int hidden=e->owner && e->owner!=stats.slot;
        if(release) InterlockedIncrement64(&mdv_ui_releases);else if(hidden) InterlockedIncrement64(&mdv_ui_hidden);
        return 1|(hidden?2:0);
    }
    return 0;
}
/* ---- the answer to a notification's question ---- */
/* An entry's interaction: its ID on this client (the descriptor's +8, checked against the handle table); 0: none. */
static int mdv_interaction_id(const mdv_t *e) {
    unsigned char *interaction=NULL;int id=0;
    if(!read_local(e->note+0x80,&interaction,8) || !interaction || !read_local(interaction+8,&id,4) || id<=0 ||
       !mdv_fn.interaction || mdv_fn.interaction(id)!=(void*)interaction) return 0;
    return id;
}
/* The sequence number of the live notification whose question has this interaction ID here; 0: not one of ours. */
static unsigned mdv_interaction_seq(int id) {
    if(!mdv_live() || id<=0) return 0;
    for(unsigned i=0;i<MDV_MAX;i++) if(mdv[i].used) {
        const mdv_t *e=mdv_valid(&mdv[i]);
        if(e && mdv_interaction_id(e)==id) return e->seq;
    }
    return 0;
}
/* The interaction ID of the newest live notification that asks something; 0 none (gate.md_media_question). */
static int mdv_question(void) {
    int best=0;unsigned seq=0;
    if(!mdv_live()) return 0;
    for(unsigned i=0;i<MDV_MAX;i++) if(mdv[i].used) {
        const mdv_t *e=mdv_valid(&mdv[i]);const int id=e?mdv_interaction_id(e):0;
        if(id && e->seq>seq) {seq=e->seq;best=id;}
    }
    return best;
}
/* The apply, every client at one tick: the question of notification `seq` is answered as the UI's call answers it.
 * 1 the event is raised, 0 the notification or its question is gone (the same everywhere). */
static int mdv_answer(unsigned seq) {
    unsigned char *notes=NULL;
    if(!mdv_live() || !seq || !mdv_fn.raise || !read_local(mdv_base+MDV_NOTES,&notes,8) || !notes) return 0;
    for(unsigned i=0;i<MDV_MAX;i++) if(mdv[i].used && mdv[i].seq==seq) {
        const mdv_t *e=mdv_valid(&mdv[i]);const int id=e?mdv_interaction_id(e):0;
        if(!id) break;
        unsigned char text[32];memset(text,0,sizeof(text));   /* X4 builds its std::string answer here: empty when raised */
        (void)mdv_fn.raise(notes,text,id);
        InterlockedIncrement64(&mdv_answers);return 1;
    }
    InterlockedIncrement64(&mdv_answers_gone);return 0;
}
/* ---- arm, install, the frame, restore ---- */
static int mdv_pins(void) {
    if(!mdv_pins_checked) {
        void *slot=NULL;
        int ok=hash_ok(image+MDV_ACTION,MDV_ACTION_SIZE,MDV_ACTION_SHA) &&
            hash_ok(image+MDV_START,MDV_START_SIZE,MDV_START_SHA) && hash_ok(image+MDV_STOP,MDV_STOP_SIZE,MDV_STOP_SHA) &&
            hash_ok(image+MDV_NOTE_END,MDV_NOTE_END_SIZE,MDV_NOTE_END_SHA) && hash_ok(image+MDV_FIND,MDV_FIND_SIZE,MDV_FIND_SHA) &&
            hash_ok(image+MDV_REMOVE,MDV_REMOVE_SIZE,MDV_REMOVE_SHA) && hash_ok(image+MDV_TARGET,MDV_TARGET_SIZE,MDV_TARGET_SHA) &&
            hash_ok(image+MDV_INTERACTION,MDV_INTERACTION_SIZE,MDV_INTERACTION_SHA) && hash_ok(image+MDV_RAISE,MDV_RAISE_SIZE,MDV_RAISE_SHA) &&
            read_local(image+MDV_ACTION_VTABLE+MDV_SLOT,&slot,8) && slot==(void*)(image+MDV_ACTION);
        for(unsigned i=0;ok && i<MDV_UI;i++) ok=hash_ok(image+mdv_ui[i].rva,mdv_ui[i].size,mdv_ui[i].sha) && !memcmp(image+mdv_ui[i].rva,mdv_ui[i].head,6);
        mdv_pins_checked=ok?1:-1;
    }
    return mdv_pins_checked==1;
}
static int (*mdv_pin)(void)=mdv_pins;    /* tests replace it */
static int mdv_slot_swap(void *expected,void *value) {
    void **slot=(void **)(mdv_base+MDV_ACTION_VTABLE+MDV_SLOT);DWORD old,ignored;
    if(!VirtualProtect(slot,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((PVOID volatile*)slot,value,expected);
    VirtualProtect(slot,8,old,&ignored);return actual==expected;
}
/* gate.md_media(1): before the first tick. The call and the vtable slot change in the probe's frame, once the slots'
 * near page is there (mdv_frame); until then X4's own way. */
static int mdv_arm(void) {
    if(mdv_armed) return stats.active!=0;   /* an identical re-arm after a loading-UI reload */
    if(!image || !stats.active) {mdv_why="not_started";return 0;}
    if(stats.issued || GetCurrentThreadId()!=ui_thread) {mdv_why="late_or_thread";return 0;}
    if(!mdv_pin()) {mdv_why="pins";return 0;}
    mdv_base=image;
    mdv_fn.action=(mdv_action_fn)(image+MDV_ACTION);mdv_fn.make=(mdv_make_fn)(image+MDV_MAKE);mdv_fn.start=(mdv_start_fn)(image+MDV_START);
    mdv_fn.stop=(mdv_stop_fn)(image+MDV_STOP);mdv_fn.find=(mdv_find_fn)(image+MDV_FIND);mdv_fn.remove=(mdv_remove_fn)(image+MDV_REMOVE);
    mdv_fn.target=(mdv_target_fn)(image+MDV_TARGET);mdv_fn.now=pnat_time_tls;
    mdv_fn.interaction=(mdv_interaction_fn)(image+MDV_INTERACTION);mdv_fn.raise=(mdv_raise_fn)(image+MDV_RAISE);
    memset(mdv,0,sizeof(mdv));mdv_made=NULL;mdv_install_tried=0;mdv_seq=0;mdv_answers=mdv_answers_gone=0;
    mdv_actions=mdv_started=mdv_replaced=mdv_failed=mdv_interactive=mdv_full=mdv_timeouts=mdv_gone=0;
    mdv_ui_starts=mdv_ui_none=mdv_ui_stops=mdv_ui_releases=mdv_ui_hidden=mdv_ui_shown=0;
    mdv_armed=1;mdv_why="-";return 1;
}
/* One of the UI's four entries: its first six bytes run from the trampoline, the entry jumps to the function here. */
static int mdv_ui_install(unsigned i) {
    static const mdv_lua_fn hooks[MDV_UI]={mdv_lua_priority,mdv_lua_start,mdv_lua_stop,mdv_lua_release};
    DWORD old,ignored;unsigned char *const stub=pslot_page+mdv_ui[i].stub,*const trampoline=pslot_page+mdv_ui[i].trampoline;
    if(memcmp(pnat_base+mdv_ui[i].rva,mdv_ui[i].head,6) || !VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return 0;
    pview_abs_jump(stub,(void*)hooks[i]);
    memcpy(trampoline,mdv_ui[i].head,6);pview_abs_jump(trampoline+6,pnat_base+mdv_ui[i].rva+6);
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    const int32_t rel=(int32_t)((intptr_t)stub-(intptr_t)(pnat_base+mdv_ui[i].rva+5));
    mdv_ui_patched[i][0]=0xe9;memcpy(mdv_ui_patched[i]+1,&rel,4);mdv_ui_patched[i][5]=0x90;
    mdv_ui_original[i]=(mdv_lua_fn)(void*)trampoline;
    if(!pview_cas(mdv_ui[i].rva,mdv_ui[i].head,mdv_ui_patched[i],6)) return 0;
    mdv_ui_installed[i]=1;return 1;
}
/* Everything of ours back that still holds what we wrote; 1 when nothing is left. */
static int mdv_uninstall(void) {
    for(unsigned i=0;i<MDV_UI;i++) if(mdv_ui_installed[i] && pview_cas(mdv_ui[i].rva,mdv_ui_patched[i],mdv_ui[i].head,6)) mdv_ui_installed[i]=0;
    if(mdv_slot_installed && mdv_slot_swap((void*)mdv_action,(void*)mdv_fn.action)) mdv_slot_installed=0;
    if(mdv_site_installed && pres_site_uninstall(MDV_MAKE_SITE,MDV_MAKE,mdv_site_patched)) mdv_site_installed=0;
    int left=mdv_slot_installed || mdv_site_installed;
    for(unsigned i=0;i<MDV_UI;i++) left|=mdv_ui_installed[i];
    return !left;
}
/* In the probe's frame (the frame thread, outside MD and outside the UI's functions: nothing is inside what changes).
 * All of it or nothing: the start without the UI's four answers would be started over by every UI. */
static void mdv_install(void) {
    if(mdv_install_tried || !pslot_page) return;
    mdv_install_tried=1;
    for(unsigned i=0;i<MDV_UI;i++) if(!mdv_ui_install(i)) {mdv_uninstall();mdv_why=mdv_ui[i].name;return;}
    if(!pres_site_install(MDV_MAKE_SITE,MDV_MAKE,MDV_MAKE_STUB,(void*)mdv_make,mdv_site_patched)) {mdv_uninstall();mdv_why="site";return;}
    mdv_site_installed=1;
    if(!mdv_slot_swap((void*)mdv_fn.action,(void*)mdv_action)) {mdv_uninstall();mdv_why="slot";return;}
    mdv_slot_installed=1;
}
/* gate.md_media_frame(): once per step, in the probe's frame, on every client. The notifications that are gone leave
 * the table, a timeout that is over removes its notification (its destructor stops the cutscene), and the UI's
 * displayed ID is not one of ours. Answers how many pictures the table holds. */
static int mdv_frame(void) {
    unsigned live=0;double now=0;unsigned char *notes=NULL;int shown=0;
    if(!mdv_live()) return 0;
    mdv_install();
    const int timed=mdv_fn.now && mdv_fn.now(&now);
    const int have_notes=read_local(mdv_base+MDV_NOTES,&notes,8) && notes;
    for(unsigned i=0;i<MDV_MAX;i++) {
        mdv_t *e=mdv_valid(&mdv[i]);
        if(!e) continue;
        if(timed && have_notes && e->end>0 && now>=e->end) {
            int id=e->id;memset(e,0,sizeof(*e));
            mdv_fn.remove(notes,&id);InterlockedIncrement64(&mdv_timeouts);continue;
        }
        ++live;
    }
    if(have_notes && read_local(notes+0x50,&shown,4) && shown)
        for(unsigned i=0;i<MDV_MAX;i++) if(mdv[i].used && mdv[i].id==shown) {shown=0;memcpy(notes+0x50,&shown,4);InterlockedIncrement64(&mdv_ui_shown);break;}
    return (int)live;
}
/* The session's end: X4's own way again; what runs is the UI's from here. */
static void mdv_restore(void) {
    if(!mdv_armed) return;
    if(!mdv_uninstall()) mdv_why="restore";
    memset(mdv,0,sizeof(mdv));mdv_made=NULL;mdv_armed=0;
}
static int mdv_status(char *out,size_t capacity) {
    unsigned live=0,owned=0,hooks=0;
    for(unsigned i=0;i<MDV_MAX;i++) if(mdv[i].used) {++live;owned+=mdv[i].owner!=0;}
    for(unsigned i=0;i<MDV_UI;i++) hooks+=mdv_ui_installed[i]!=0;
    int n=snprintf(out,capacity,"armed=%d installed=%d/%d/%u live=%u/%u actions=%lld started=%lld replaced=%lld failed=%lld interactive=%lld full=%lld "
        "timeouts=%lld gone=%lld ui=%lld/%lld/%lld/%lld/%lld/%lld why=%s",mdv_armed,mdv_site_installed,mdv_slot_installed,hooks,live,owned,
        (long long)mdv_actions,(long long)mdv_started,(long long)mdv_replaced,(long long)mdv_failed,(long long)mdv_interactive,(long long)mdv_full,
        (long long)mdv_timeouts,(long long)mdv_gone,(long long)mdv_ui_starts,(long long)mdv_ui_none,(long long)mdv_ui_stops,
        (long long)mdv_ui_releases,(long long)mdv_ui_hidden,(long long)mdv_ui_shown,mdv_why);
    return n>0 && (size_t)n<capacity?n:0;
}

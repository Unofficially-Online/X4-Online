/* What an MD cue shows, on its owner's client only (C6 of reports/LOCKSTEP-UI-MISSIONS-20261101.txt). Armed with missions
 * per player (native_gate_missions_per_player, on by default); Windows gate; no stop reason of its own.
 *
 * MD runs on every client. A cue that belongs to slot k (a mission slot k accepted, the story of the start slot k took:
 * lockstep_mission_owners.h) runs in slot k's scope on every client, and so did what it only SHOWS: help texts,
 * notifications, a menu it opens, the cockpit tint, a screen fade, music, an achievement. Player 1 got player 2's hints
 * and player 2's music.
 *
 * Those are Scripts::*Action objects; the interpreter calls their vtable slot 3 (action node, exec state, frame). The slot
 * of each action below points at a wrapper here: inside an MD frame of a cue slot k owns (the frame rule: the slot's scope
 * is open and mpp_depth counts an owned frame), on a client whose own slot is not k, the action is not run. Everywhere
 * else the stock function runs: on client k, in a frame nobody owns (the session's own story and the world's cues show
 * on every client as before), off the simulation's thread, before the session and after it.
 *
 * The actions here change no simulation state by what they are for (UI queues, sound, the screen, the platform's
 * achievements); whether one of them draws from the script's RNG stream or takes a runtime ID on the way is not read
 * statically: a run with an owned story compares the RNG and ID rows of the owner's client (ran) and another client's
 * (skipped). Spoken lines are not here: they must run everywhere (their end is an event), only their voice is local
 * (lockstep_speech_local.h). write_to_logbook and raise_lua_event are not here either (a logbook is in the save; a Lua
 * event can be anything). */
typedef void (*mdp_fn)(void *,void *,void *);
#define MDP_ACTIONS 11u
#define MDP_SLOT 0x18u   /* vtable slot 3: the action's execute */
static const struct {const char *name;uint32_t vtable,execute;} mdp_actions[MDP_ACTIONS]={
    {"show_help",0x2c0f760u,0xb7a1b0u},{"show_help_multi",0x2c0f6e8u,0xb7b1e0u},{"show_help_overlay",0x2c0f710u,0xb7bfb0u},
    {"remove_help_overlay",0x2c0f7d8u,0xb7c290u},{"remove_help",0x2c0f7b0u,0xb7dfe0u},{"show_notification",0x2c0e0c0u,0xbb9ab0u},
    {"open_menu",0x2c0c550u,0xbf4320u},{"set_cockpit_tint",0x2c0b5e0u,0xc08b80u},{"unlock_achievement",0x2c0aaf8u,0xc1e8d0u},
    {"fade_screen",0x2c0a458u,0xc32f10u},{"music",0x2c0abe8u,0xc1db30u}};
static mdp_fn mdp_original[MDP_ACTIONS];
static int mdp_installed[MDP_ACTIONS],mdp_armed;
static volatile LONG64 mdp_runs,mdp_skips[MDP_ACTIONS];
static const char *mdp_why="-";
static unsigned pstk_scope_slot;   /* lockstep_pilot_stock.h */
static unsigned mpp_depth;         /* lockstep_mission_owners.h */
/* This call is inside an MD frame of a cue another player owns. */
static int mdp_foreign(void) {
    const unsigned k=pstk_scope_slot;
    return mdp_armed && stats.active && GetCurrentThreadId()==ui_thread && k>=2 && mpp_depth && k!=stats.slot;
}
static void mdp_run(unsigned i,void *node,void *exec,void *frame) {
    if(mdp_foreign()) {InterlockedIncrement64(&mdp_skips[i]);return;}
    InterlockedIncrement64(&mdp_runs);mdp_original[i](node,exec,frame);
}
#define MDP_WRAP(i) static void mdp_wrap_##i(void *node,void *exec,void *frame) {mdp_run(i,node,exec,frame);}
MDP_WRAP(0) MDP_WRAP(1) MDP_WRAP(2) MDP_WRAP(3) MDP_WRAP(4) MDP_WRAP(5) MDP_WRAP(6) MDP_WRAP(7) MDP_WRAP(8) MDP_WRAP(9) MDP_WRAP(10)
static const mdp_fn mdp_wrappers[MDP_ACTIONS]={mdp_wrap_0,mdp_wrap_1,mdp_wrap_2,mdp_wrap_3,mdp_wrap_4,mdp_wrap_5,mdp_wrap_6,mdp_wrap_7,
    mdp_wrap_8,mdp_wrap_9,mdp_wrap_10};
static int mdp_slot_swap(unsigned i,void *expected,void *value) {
    void **slot=(void **)(image+mdp_actions[i].vtable+MDP_SLOT);DWORD old,ignored;
    if(!VirtualProtect(slot,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((PVOID volatile*)slot,value,expected);
    VirtualProtect(slot,8,old,&ignored);return actual==expected;
}
/* Every slot back that still holds our wrapper; 1 when none of ours is left. */
static int mdp_uninstall(void) {
    int clean=1;
    for(unsigned i=0;i<MDP_ACTIONS;i++) if(mdp_installed[i]) {
        if(mdp_slot_swap(i,(void*)mdp_wrappers[i],(void*)mdp_original[i])) mdp_installed[i]=0;else clean=0;
    }
    return clean;
}
/* gate.md_presentation(1): before the first tick, on the frame thread (MD runs on it: no action is inside a slot now).
 * Every vtable must hold the stock execute of build 9.00; one that does not refuses the whole module and nothing stays. */
static int mdp_arm(void) {
    if(mdp_armed) return 1;
    if(!image || !stats.active) {mdp_why="not_started";return 0;}   /* the gate's start finds the image */
    if(stats.issued || GetCurrentThreadId()!=ui_thread) {mdp_why="late_or_thread";return 0;}
    for(unsigned i=0;i<MDP_ACTIONS;i++) {
        void *now=NULL;
        if(!read_local(image+mdp_actions[i].vtable+MDP_SLOT,&now,8) || now!=(void*)(image+mdp_actions[i].execute)) {mdp_why=mdp_actions[i].name;return 0;}
    }
    for(unsigned i=0;i<MDP_ACTIONS;i++) {
        mdp_original[i]=(mdp_fn)(image+mdp_actions[i].execute);
        if(!mdp_slot_swap(i,(void*)mdp_original[i],(void*)mdp_wrappers[i])) {mdp_uninstall();mdp_why="swap";return 0;}
        mdp_installed[i]=1;
    }
    mdp_runs=0;memset((void*)mdp_skips,0,sizeof(mdp_skips));mdp_armed=1;mdp_why="-";return 1;
}
/* The session's end: the stock functions back in their slots. */
static void mdp_restore(void) {
    if(!mdp_armed) return;
    if(!mdp_uninstall()) mdp_why="restore";
    mdp_armed=0;
}
static int mdp_status(char *out,size_t capacity) {
    unsigned installed=0;long long skipped=0;
    for(unsigned i=0;i<MDP_ACTIONS;i++) {installed+=mdp_installed[i]!=0;skipped+=(long long)mdp_skips[i];}
    int n=snprintf(out,capacity,"armed=%d installed=%u/%u ran=%lld skipped=%lld why=%s by=",mdp_armed,installed,MDP_ACTIONS,(long long)mdp_runs,skipped,mdp_why);
    for(unsigned i=0;n>0 && (size_t)n<capacity && i<MDP_ACTIONS;i++)
        n+=snprintf(out+n,capacity-(size_t)n,"%s%lld",i?"/":"",(long long)mdp_skips[i]);
    return n>0 && (size_t)n<capacity?n:0;
}

/* What MD may READ of this client's own screen: nothing (gate.md_menu_state(1), armed in every session before the first
 * tick; restored at the stop).
 *
 * player.isinfullscreenmenu is each PC's own answer: the property evaluator calls 0x8cd710 (is any full-screen menu
 * displayed; the export IsFullscreenMenuDisplayed(true) jumps there too) at 0xcdaa75 and stores its byte. 69 uses in 26
 * stock MD scripts wait or queue while a menu is open: hints, dialogs, the crew's ambient use cases. In a person's session
 * of 11 Nov one client had the map open: md/npc_usecases.xml queued a use case there and started it on the other client,
 * and the runtime IDs were one apart on every tick from 22776 on (read from the logs by the hot-join session:
 * RUNTIME_IDS_CALLER cue=FindUseCaseActors 28 against 27).
 * While a session runs the call is `xor eax,eax`: MD sees no full-screen menu, on every client. What waits for a menu to
 * close does not wait (a hint or an ambient line can come while a map is open). The UI's own callers of 0x8cd710 are not
 * touched. player.isinconversation reads the conversation manager's state, which is the simulation's; player.
 * isinfullscreencutscene (the call right behind this one) reads a cutscene MD itself starts: both stay. */
#define MDM_SITE 0xcdaa75u
#define MDM_PIN 0xcdaa55u   /* 64 bytes around the site: this property's and its neighbours' evaluation */
#define MDM_PIN_SHA "6bff3d2b92c3f1b4ee546d2eae00c355e212004eb0a5f0196d4b9e2e749c5215"
static const unsigned char mdm_stock[5]={0xe8,0x96,0x2c,0xbf,0xff};     /* call 0x8cd710 */
static const unsigned char mdm_patched[5]={0x31,0xc0,0x90,0x90,0x90};   /* xor eax,eax; nop x3 */
static int mdm_armed;
static const char *mdm_why="-";
static int mdm_pin_image(void) {return hash_ok(image+MDM_PIN,64,MDM_PIN_SHA);}
static int (*mdm_pin)(void)=mdm_pin_image;   /* tests replace it */
static int mdm_write(const unsigned char *expected,const unsigned char *value) {
    unsigned char *at=image+MDM_SITE;DWORD old,ignored;
    if(memcmp(at,expected,5) || !VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&old)) return 0;
    memcpy(at,value,5);VirtualProtect(at,5,old,&ignored);FlushInstructionCache(GetCurrentProcess(),at,5);return 1;
}
static int mdm_arm(void) {
    if(mdm_armed) return 1;
    if(!image || !stats.active) {mdm_why="not_started";return 0;}   /* the gate's start finds the image */
    if(stats.issued || GetCurrentThreadId()!=ui_thread) {mdm_why="late_or_thread";return 0;}
    if(!mdm_pin()) {mdm_why="pin";return 0;}
    if(!mdm_write(mdm_stock,mdm_patched)) {mdm_why="site";return 0;}
    mdm_armed=1;mdm_why="-";return 1;
}
static void mdm_restore(void) {
    if(!mdm_armed) return;
    if(!mdm_write(mdm_patched,mdm_stock)) mdm_why="restore";
    mdm_armed=0;
}
static int mdm_status(char *out,size_t capacity) {
    /* Before the gate's start there is no image to read the site from (a status asked then crashed two clients, 11 Nov). */
    int n=snprintf(out,capacity,"armed=%d site=%02x%02x why=%s",mdm_armed,image?image[MDM_SITE]:0u,image?image[MDM_SITE+1]:0u,mdm_why);
    return n>0 && (size_t)n<capacity?n:0;
}

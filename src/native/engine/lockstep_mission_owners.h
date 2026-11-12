/* Personal missions per player (reports/LOCKSTEP-UI-MISSIONS-20261101.txt section 4: C1 (i)(ii) and C5), ON BY DEFAULT
 * in sessions (native_gate_missions_per_player, tools/dev/native_gate_defaults.lua; false turns it off). Needs the pilot
 * slots, the UI replay's missions and events families and native_gate_script_rng with its MD bit, whose scopes see every
 * MD frame and its cue.
 *
 * Model (c) for slots 2..N in its first form: offers stay the session's (the generic missions manager offers around slot
 * 1's player), and whoever accepts one owns it.
 *  calls   slot k's replayed mission calls (the events family's SignalObject and AddUITriggeredEvent, the missions family's
 *          briefing, abort and active mission) run on every client inside slot k's scope (lockstep_pilot_slots.h
 *          pslot_enter: the universe's player singleton is P_k, the canonical player block holds slot k's store), so MD's
 *          player.entity, player.ship and player.sector are slot k's in every action they reach (C5). A scope that cannot
 *          be entered stops the replay (ui_replay_fail): the call never runs as slot 1's on some clients.
 *  claims  an accept by slot k (SignalObject(actor, 'accept', offer), the stock menus' form) claims the offer cue (the
 *          mission's cue: create_offer cue=, create_mission offercue=) by its runtime ID (+8) for slot k: an ownership
 *          table filled by replayed calls only, so the same on every client (C1 i). Not the cue's root instance (+0x170):
 *          a library mission's root instance can be the manager that made every offer, and a static cue's is its script.
 *  frames  every MD frame whose cue is an owned cue or one of its descendants (the parent chain at +0x158, nearest owner
 *          first; the script RNG's MD scopes: a cue's actions, its timed and waiting checks, its event listeners) enters
 *          its slot's scope at the frame's start and leaves it at its end, so the mission's objectives, distances,
 *          inventory and rewards follow P_k (C1 ii). A frame already in that slot's scope continues in it (inside=); a
 *          frame of another slot's mission inside a scope is counted (conflicts=) and runs in the scope it found.
 *  active  the mission manager's active mission (+0x78 its ID, +0x80 its index in the missions vector; set by
 *          0x113b920, which raises the cue events 0x63c/0x63d) is per slot: slot k's scope (pslot_enter/pslot_leave, so
 *          slot k's replayed SetActiveMission, its accept and its missions' frames) runs on slot k's active mission and
 *          leaves the canonical one (slot 1's) as it was, the same on every client; client k's presentation scope
 *          (pres_enter/pres_leave) shows slot k's to its own HUD and menus and changes nothing. The index is found
 *          again by ID at every exchange (the vector moves when a mission ends); an ID that is gone is no active mission.
 *          The manager's guidance timer (+0x30 on g_Ticker, flag +0x8c) stays the manager's one: slot k's guidance is
 *          refreshed where its scope runs it, not by the timer [I].
 *  lists   a client's mission menus list its own slot's missions (the probe wraps the Lua list readers by mpp_owner_of;
 *          a mission nobody claimed, e.g. a client's own guidance, stays everybody's). Slot 1's accepts claim too (owner
 *          1: no scope, the list rule only).
 *  offers  generic mission offers per slot (C4). X4's GenericMissions.Manager generates around "the player" (its sector,
 *          its relations) at the game's start, on its sector change and every ten minutes; that stays slot 1's. For slot k
 *          the probe signals our MD cue (native_gate_ui_replay.xml SlotOffers) inside slot k's scope at the same tick on
 *          every client, and the cue signals the manager's EvaluateSectorMissions instantly: the whole selection runs in the
 *          scope, so it reads slot k's sector and P_k. While that call runs (mpp_generate), and whenever a frame of a cue
 *          slot k owns is on the MD stack (mpp_depth), every root instance MD makes is claimed for slot k at its
 *          construction (mpp_create_hook on the two calls that construct root instances). The selection cue is made in
 *          the call; it instantiates the generic mission in a later frame, which runs in slot k's scope because the
 *          selection is slot k's, and so the mission's root is claimed there. The new offer's own cues (its distance and sector checks, its timeout, its briefing)
 *          then run in slot k's scope like an accepted mission's, and the offer is not removed because slot 1 is
 *          elsewhere. The same rule gives an accepted mission's helper cues to its owner. Whoever accepts an offer takes it
 *          over (the nearest owner is the accepted offer cue). A full table drops the claims of cues that are gone.
 *  story   a start's story for the player who took that start (native_gate_story; tools/dev/native_gate_session_start.py
 *          story_files). The start script's root cue is a STATIC cue and runs as X4 wrote it; only its condition is the
 *          session's own signal. gate.mpp_story(k, "<Script>.<Cue>"), inside slot k's call scope, names it: the next MD
 *          frame of the cue with that path (the script RNG's cue identity, FNV-1a of the static path) claims the cue for
 *          slot k, once, and runs as slot k's with everything below it. Not an instance of it: in an instance a name
 *          that points at a cue of a branch not started yet is the static cue's, and the signal goes nowhere (st1, 11
 *          Nov: "Signalled cue md.GS_Terran1.Ch1_2_Wingmate_Orders has no corresponding listeners").
 *  faction a mission scope of slot k (a frame of an owned cue, a replayed call) runs with g_PlayerFaction = slot k's
 *          faction, the owner of its ship (lockstep_ui_trade.h), and puts X4's back when it leaves (C3): in factions
 *          mode reward_player, player.money and faction.player inside slot k's mission are x4lan_playerk's, and a start's
 *          story moves its own player's relations. Coop: the same faction, nothing changes. Not the piloting scopes.
 * Not yet: conversations of slots 2..N (one conversation manager per process).
 * Hot join: the table at T rides the package as the probe's 'missions_per_player' state (mpp_state on the saver,
 * mpp_restore on the joiner before its first tick), in table order, so the joiner's frames and hash follow the same claims. */
#define MPP_OWNERS 512u
#define MPP_DEPTH 24u         /* parents from a frame's cue up to its owner (JOIN_CUE_DEPTH is 64 for any tree) */
#define MPP_CUE_PARENT 0x158u /* the parent cue (JOIN_CUE_PARENT) */
#define MPP_CUE_ROOT 0x170u   /* an instance's root instance (JOIN_CUE_ROOT; a static cue: 0) */
#define MPP_CUE_ID 0x8u       /* the cue's runtime ID, its key in the universe map */
typedef struct {uint64_t cue;unsigned slot;} mpp_owner_rec;
static mpp_owner_rec mpp_owners[MPP_OWNERS];
static unsigned mpp_count;
static int mpp_armed;
static volatile LONG64 mpp_claims,mpp_claim_gone,mpp_full,mpp_frames,mpp_entered,mpp_inside,mpp_conflicts,mpp_refused,
                       mpp_leave_failed,mpp_calls,mpp_call_refused,mpp_generations,mpp_generated,mpp_pruned,mpp_created;
static unsigned mpp_depth;        /* frames of the open scope's slot's own cues on the MD stack now */
static unsigned mpp_generating;   /* the slot whose generic offers are being generated now (inside its scope), else 0 */
static unsigned mpp_story_slot;   /* the slot whose start story is being begun now (inside its scope), else 0 */
static uint64_t mpp_story_key;    /* ...and the path key of the static cue that begins it */
static int mpp_story_missed;      /* the last ask claimed nothing (yet) */
static volatile LONG64 mpp_story_asked,mpp_story_claimed;

static unsigned mpp_owner_of(uint64_t cue_id) {
    for(unsigned i=0;i<mpp_count;i++) if(mpp_owners[i].cue==cue_id) return mpp_owners[i].slot;
    return 0;
}
/* The slot owning this MD cue (the cue or its nearest owned ancestor), 0 = none, not a cue or unreadable. The frames' path:
 * guarded copies (sample_read), no system call per parent. */
static unsigned mpp_slot_of(const unsigned char *cue) {
    uintptr_t vt=0,at=(uintptr_t)cue;
    if(!cue || !sample_read(cue,&vt,8) || vt!=(uintptr_t)(image+SCRIPT_RNG_CUE_VT)) return 0;
    for(unsigned up=0;at && up<MPP_DEPTH;up++) {
        uint64_t id=0;uintptr_t parent=0;
        if(!sample_read((void *)(at+MPP_CUE_ID),&id,8)) return 0;
        const unsigned k=id?mpp_owner_of(id):0;
        if(k) return k;
        if(!sample_read((void *)(at+MPP_CUE_PARENT),&parent,8)) return 0;
        at=parent;
    }
    return 0;
}
/* A full table: the claims of cues that are gone leave it, the others keep their order (the same on every client: the cues
 * are the simulation's). Only with the engine's map at hand. Returns how many left. */
static unsigned mpp_prune(void) {
    unsigned kept=0;
    if(!uirep_fn.find) return 0;
    for(unsigned i=0;i<mpp_count;i++) if(uimis_cue(mpp_owners[i].cue)) mpp_owners[kept++]=mpp_owners[i];
    const unsigned gone=mpp_count-kept;
    mpp_count=kept;InterlockedExchangeAdd64(&mpp_pruned,(LONG64)gone);return gone;
}
/* Slot k owns this cue from now on (a later claim by another slot takes it over). 1 claimed, 0 the table is full. */
static int mpp_claim_id(unsigned k,uint64_t cue_id) {
    for(unsigned i=0;i<mpp_count;i++) if(mpp_owners[i].cue==cue_id) {mpp_owners[i].slot=k;InterlockedIncrement64(&mpp_claims);return 1;}
    if(mpp_count>=MPP_OWNERS && !mpp_prune()) {InterlockedIncrement64(&mpp_full);return 0;}
    mpp_owners[mpp_count].cue=cue_id;mpp_owners[mpp_count].slot=k;++mpp_count;InterlockedIncrement64(&mpp_claims);return 1;
}
/* A replayed accept: slot k claims the offer cue with this runtime ID. 1 claimed, 0 the cue is gone here (the same on every
 * client when the worlds agree) or the table is full. */
static int mpp_claim(unsigned k,uint64_t cue_id) {
    if(!mpp_armed || k<1 || k>PILOT_SLOTS) return 0;
    if(!uimis_cue(cue_id)) {InterlockedIncrement64(&mpp_claim_gone);return 0;}
    return mpp_claim_id(k,cue_id);
}
/* ---- new root instances follow their slot ---- */
/* MD::Cue's instance constructor 0x1111a40 (this, the static cue, the parent instance) has four callers; two pass no
 * parent and append the result to the static cue's +0x1a8 list: they make ROOT instances (0x111c472 in Cue_CheckConditions
 * 0x111c290, the instantiating check; 0x111cc68 its second form). Both calls go through mpp_create_hook (a jump stub on a
 * page of our own near the image; the thunk page is full). A frame's cue is the static cue while its conditions pass and its
 * actions run, so the frame rule never sees the instance being made: the constructor does. */
#define MPP_CREATE 0x1111a40u
#define MPP_CREATE_SITES 2u
static const uint32_t mpp_create_site[MPP_CREATE_SITES]={0x111c472u,0x111cc68u};
typedef void *(*mpp_create_fn)(void *self,void *model,void *parent);
static unsigned char *mpp_page;
static unsigned char mpp_site_original[MPP_CREATE_SITES][5],mpp_site_patched[MPP_CREATE_SITES][5];
static int mpp_site_installed[MPP_CREATE_SITES],mpp_hook_restored;
static const char *mpp_hook_why="-";
/* A root instance was made: while slot k's own work is running in its scope (its offers' generation, or a frame of a cue
 * it owns further up the MD stack), the new instance is slot k's. That is how a generation's selection cue, the generic
 * mission it instantiates in a later frame and an accepted mission's helper cues follow their slot. */
static void mpp_created_root(const unsigned char *made) {
    const unsigned k=pstk_scope_slot;uint64_t id=0;
    if(!mpp_armed || !made || GetCurrentThreadId()!=ui_thread || k<2 || k>PILOT_SLOTS || !(mpp_generating==k || mpp_depth)) return;
    InterlockedIncrement64(&mpp_created);
    if(mpp_slot_of(made)==k) return;   /* already slot k's through its parents (an instantiating sub-cue of an owned tree) */
    if(read_local(made+MPP_CUE_ID,&id,8) && id && !mpp_owner_of(id) && mpp_claim_id(k,id)) InterlockedIncrement64(&mpp_generated);
}
static __attribute__((noinline)) void *mpp_create_hook(void *self,void *model,void *parent) {
    void *made=((mpp_create_fn)(image+MPP_CREATE))(self,model,parent);
    if(!parent) mpp_created_root(made);
    return made;
}
/* The constructor's head (its ID comes from 0xcaab00, called at +0x65) and the two calls with no parent (xor r8d,r8d). */
static int mpp_create_pins(void) {
    static const unsigned char no_parent[3]={0x45,0x33,0xc0};
    if(!hash_ok(image+MPP_CREATE,101,"e8b5c180ed3f9f257feac249d4a982a0405f5ccdaca35a1fe66d986254d2bb6d")) return 0;
    for(unsigned i=0;i<MPP_CREATE_SITES;i++) {
        unsigned char bytes[5],before[9];int32_t rel;
        if(!read_local(image+mpp_create_site[i],bytes,5) || bytes[0]!=0xe8 || !read_local(image+mpp_create_site[i]-9,before,9) ||
           memcmp(before,no_parent,3)) return 0;
        memcpy(&rel,bytes+1,4);
        if(mpp_create_site[i]+5u+(uint32_t)rel!=MPP_CREATE) return 0;
    }
    return 1;
}
static int mpp_site_write(unsigned i,const unsigned char *bytes) {
    unsigned char *at=image+mpp_create_site[i];DWORD old,ignored;
    if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&old)) return 0;
    memcpy(at,bytes,5);VirtualProtect(at,5,old,&ignored);FlushInstructionCache(GetCurrentProcess(),at,5);return 1;
}
static int mpp_hook_uninstall(void) {
    int ok=1;
    for(unsigned i=0;i<MPP_CREATE_SITES;i++) if(mpp_site_installed[i]) {
        if(!memcmp(image+mpp_create_site[i],mpp_site_patched[i],5) && mpp_site_write(i,mpp_site_original[i])) mpp_site_installed[i]=0;else ok=0;
    }
    return ok;
}
/* On the frame thread at the arming (MD runs on this thread: no call is inside the two sites now). */
static int mpp_hook_install(void) {
    DWORD old,ignored;
    if(mpp_site_installed[0] && mpp_site_installed[1]) return 1;
    if(GetCurrentThreadId()!=ui_thread) {mpp_hook_why="thread";return 0;}
    if(!mpp_create_pins()) {mpp_hook_why="pins";return 0;}
    if(!mpp_page) {
        const uintptr_t at=(uintptr_t)(image+MPP_CREATE)&~(uintptr_t)65535;
        for(uintptr_t distance=65536;distance<0x70000000 && !mpp_page;distance+=65536)
            for(int sign=-1;sign<=1 && !mpp_page;sign+=2) {
                unsigned char *p=VirtualAlloc((void*)(sign<0?at-distance:at+distance),PVIEW_PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
                if(!p) continue;
                const intptr_t d=(intptr_t)p-(intptr_t)(image+MPP_CREATE);
                if(d>INT32_MIN+0x1000000 && d<INT32_MAX-0x1000000) mpp_page=p;else VirtualFree(p,0,MEM_RELEASE);
            }
        if(!mpp_page) {mpp_hook_why="page";return 0;}
    }
    if(!VirtualProtect(mpp_page,PVIEW_PAGE,PAGE_READWRITE,&old)) {mpp_hook_why="page_protect";return 0;}
    memset(mpp_page,0xcc,64);pview_abs_jump(mpp_page,(void*)mpp_create_hook);
    if(!VirtualProtect(mpp_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) {mpp_hook_why="page_protect";return 0;}
    FlushInstructionCache(GetCurrentProcess(),mpp_page,PVIEW_PAGE);
    for(unsigned i=0;i<MPP_CREATE_SITES;i++) {
        const int32_t rel=(int32_t)((intptr_t)mpp_page-(intptr_t)(image+mpp_create_site[i]+5));
        memcpy(mpp_site_original[i],image+mpp_create_site[i],5);
        mpp_site_patched[i][0]=0xe8;memcpy(mpp_site_patched[i]+1,&rel,4);
    }
    for(unsigned i=0;i<MPP_CREATE_SITES;i++) {
        if(!mpp_site_write(i,mpp_site_patched[i])) {mpp_hook_uninstall();mpp_hook_why="site_write";return 0;}
        mpp_site_installed[i]=1;
    }
    return 1;
}
/* The session's end (uirep_restore): the two calls go back; the table stays for the last status row. */
static void mpp_restore_hook(void) {
    if(!mpp_armed) return;
    mpp_hook_restored=mpp_hook_uninstall();mpp_generating=0;mpp_depth=0;
}
/* gate.mpp_generate(k): slot k's generic offers are generated until gate.mpp_generate(0); only inside slot k's call scope. */
static int mpp_generate(unsigned k) {
    if(!k) {mpp_generating=0;return 1;}
    if(!mpp_armed || k<2 || k>PILOT_SLOTS || pstk_scope_slot!=k) return 0;
    mpp_generating=k;InterlockedIncrement64(&mpp_generations);return 1;
}
/* ---- the mission's faction (C3) ---- */
static void *uitrade_slot_faction(unsigned slot);static void *volatile *uitrade_player_faction(void);   /* lockstep_ui_trade.h, behind this file */
static void *mpp_faction_of_native(unsigned k) {return uitrade_slot_faction(k);}
static void *volatile *mpp_faction_cell_native(void) {return pnat_base?uitrade_player_faction():NULL;}
static void *(*mpp_faction_of)(unsigned)=mpp_faction_of_native;                 /* tests replace them */
static void *volatile *(*mpp_faction_cell)(void)=mpp_faction_cell_native;
static void *mpp_faction_saved,*mpp_faction_put;static unsigned mpp_faction_open;
static volatile LONG64 mpp_faction_swaps,mpp_faction_same,mpp_faction_changed;
/* A mission scope of slot k opens: the player faction becomes slot k's. 1: this call opened it (its mpp_faction_leave
 * closes it); 0: nothing to do, or a mission scope further out holds it already. */
static int mpp_faction_enter(unsigned k) {
    void *volatile *cell=mpp_faction_cell();
    if(mpp_faction_open || !cell) return 0;
    void *faction=mpp_faction_of(k),*now=*cell;
    if(!faction || !now) return 0;
    if(faction==now) {InterlockedIncrement64(&mpp_faction_same);return 0;}
    mpp_faction_saved=now;mpp_faction_put=faction;*cell=faction;mpp_faction_open=k;InterlockedIncrement64(&mpp_faction_swaps);return 1;
}
/* ...and closes: X4's own player faction again (a change inside the scope is counted and not kept). */
static void mpp_faction_leave(unsigned k) {
    void *volatile *cell=mpp_faction_cell();
    if(!k || mpp_faction_open!=k || !cell) return;
    if(*cell!=mpp_faction_put) InterlockedIncrement64(&mpp_faction_changed);
    *cell=mpp_faction_saved;mpp_faction_open=0;mpp_faction_saved=mpp_faction_put=NULL;
}
/* The script RNG's MD scope (lockstep_script_rng.h script_rng_enter). The answer goes back to mpp_frame_leave: the slot
 * whose scope this frame entered (low byte, 0 none) and MPP_FRAME_OWNED when the frame is one of the open scope's slot's
 * own (counted in mpp_depth until it leaves). */
#define MPP_FRAME_OWNED 0x100u
#define MPP_FRAME_FACTION 0x200u   /* this frame opened its slot's faction (a mission frame inside a piloting scope does too) */
/* gate.mpp_story(k, path): the static cue with this path is slot k's from its next frame on (inside slot k's call scope,
 * one cue per ask). gate.mpp_story(0): the ask is over; 1 when its cue was claimed (or nothing was asked), 0 when no
 * frame of that cue came or the cue was somebody's already. */
static int mpp_story(unsigned k,const char *path) {
    if(!k) {const int claimed=!mpp_story_missed;mpp_story_slot=0;mpp_story_key=0;mpp_story_missed=0;return claimed;}
    const size_t n=path?strlen(path):0;
    if(!mpp_armed || k<2 || k>PILOT_SLOTS || pstk_scope_slot!=k || !n || n>=CUE_PATH_TEXT) return 0;
    for(size_t i=0;i<n;i++) {
        const char c=path[i];
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='.')) return 0;   /* as cue_encode leaves it */
    }
    mpp_story_key=cue_fnv(CUE_FNV_BASIS,path,n);mpp_story_slot=k;mpp_story_missed=1;InterlockedIncrement64(&mpp_story_asked);return 1;
}
/* The frame that answers an ask: this frame's cue has the asked path (script_rng_current is this frame, its identity the
 * static cue's path key) and is nobody's yet. */
static void mpp_story_frame(void *cue,unsigned scope) {
    uint64_t id=0;uintptr_t vt=0;
    if(scope!=mpp_story_slot || !script_rng_current || script_rng_current->id.owner!=mpp_story_key) return;
    if(!cue || !sample_read(cue,&vt,8) || vt!=(uintptr_t)(image+SCRIPT_RNG_CUE_VT) || !sample_read((unsigned char*)cue+MPP_CUE_ID,&id,8) || !id) return;
    if(!mpp_owner_of(id) && mpp_claim_id(mpp_story_slot,id)) {mpp_story_missed=0;InterlockedIncrement64(&mpp_story_claimed);}
    mpp_story_slot=0;mpp_story_key=0;
}
static unsigned mpp_frame_enter(void *cue) {
    if(!mpp_armed) return 0;
    const unsigned scope=pstk_scope_slot;
    if(mpp_story_slot) mpp_story_frame(cue,scope);
    if(!mpp_count) return 0;
    const unsigned k=mpp_slot_of(cue);
    if(k<2) return 0;   /* nobody's, or slot 1's: the canonical player as it is */
    InterlockedIncrement64(&mpp_frames);
    /* The lines an owned frame starts are its owner's (lockstep_speech_local.h): client k hears them, the others do not. */
    if(scope==k) {InterlockedIncrement64(&mpp_inside);++mpp_depth;spl_owner_enter(k);return MPP_FRAME_OWNED|(mpp_faction_enter(k)?MPP_FRAME_FACTION:0u);}
    if(scope) {InterlockedIncrement64(&mpp_conflicts);return 0;}
    if(!pslots[k].player || !pslot_scope_begin(k,&pslots[k],"mission")) {InterlockedIncrement64(&mpp_refused);return 0;}
    InterlockedIncrement64(&mpp_entered);++mpp_depth;spl_owner_enter(k);return k|MPP_FRAME_OWNED|(mpp_faction_enter(k)?MPP_FRAME_FACTION:0u);
}
static void mpp_frame_leave(unsigned code) {
    if(code&MPP_FRAME_OWNED) spl_owner_leave();
    if((code&MPP_FRAME_OWNED) && mpp_depth) --mpp_depth;
    if(code&MPP_FRAME_FACTION) mpp_faction_leave(mpp_faction_open);
    if((code&0xffu) && !pslot_leave(code&0xffu)) InterlockedIncrement64(&mpp_leave_failed);
}
/* ---- AI frames of a slot's own entities (native_gate_missions_per_player_ai_frames, a session default) ----
 * X4 runs a few AI scripts on "the player": on the player entity itself and on its children, above all the ship computer,
 * which runs move.autopilot. Such a script asks for the player by name (player.entity, player.computer, player.age,
 * player.activemissionwaypoint, stop_player_autopilot). The start of slot k's autopilot is replayed inside slot k's scope
 * and the script's first slice runs there, but every later frame came from the AI update outside any scope, where the
 * player is slot 1's: move.autopilot's own check `$ship.pilot != player.entity` at its label start (reached again after a
 * guidance update or an invalid target) then failed with "You are not piloting this ship" for a player who was (the user's
 * report of 11 Nov, client 2 on the Terran cadet story), and its stop_player_autopilot at the end stopped slot 1's.
 * The rule: an AI frame (an execution, a range, an event's conditions and actions, a call stack's event) whose script
 * context's entity (AIScriptContext +0x120) is slot k's player P_k or a component whose parent (+0x70) is P_k, k >= 2,
 * runs in slot k's scope with slot k's faction. Inside slot k's scope already (the replayed start, slot k's flight tick):
 * as it is. Inside another slot's scope: as it is, counted. Unlike an owned MD frame it is not an owned mission frame:
 * mpp_depth and the speech owner stay as they are (what the script shows and says goes to every client as before).
 * A scope that cannot open now (no store yet, a presentation scope, inside X4's controller update) is not asked for:
 * the frame runs as before and is counted, on every client alike. */
#define MPP_AI_ENTITY 0x120u   /* AI::AIScriptContext: the entity that runs the script */
#define MPP_AI_PARENT 0x70u    /* a component's parent */
static int mpp_ai_on;
static volatile LONG64 mpp_ai_frames,mpp_ai_entered,mpp_ai_inside,mpp_ai_conflicts,mpp_ai_refused;
/* The slot (2..N) whose player is this AI script context's entity or that entity's parent; 0 none. */
static unsigned mpp_ai_slot_of(uintptr_t context) {
    uintptr_t vtable=0,entity=0,parent=0;unsigned k=2;
    while(k<=PILOT_SLOTS && !pslots[k].player) k++;
    if(k>PILOT_SLOTS || !context) return 0;   /* no slot 2..N has a player: nothing is read */
    if(!script_rng_read((const void*)context,&vtable,8) || vtable!=(uintptr_t)(image+SCRIPT_RNG_AI_VT) ||
       !script_rng_read((const void*)(context+MPP_AI_ENTITY),&entity,8) || !entity) return 0;
    for(unsigned i=k;i<=PILOT_SLOTS;i++) if(pslots[i].player && (uintptr_t)pslots[i].player==entity) return i;
    if(!script_rng_read((const void*)(entity+MPP_AI_PARENT),&parent,8) || !parent) return 0;
    for(unsigned i=k;i<=PILOT_SLOTS;i++) if(pslots[i].player && (uintptr_t)pslots[i].player==parent) return i;
    return 0;
}
/* The script RNG's AI scope (lockstep_script_rng.h script_rng_enter); the answer goes back to mpp_frame_leave. */
static unsigned mpp_ai_frame_enter(uintptr_t context) {
    if(!mpp_armed || !mpp_ai_on) return 0;
    const unsigned k=mpp_ai_slot_of(context);
    if(k<2) return 0;
    InterlockedIncrement64(&mpp_ai_frames);
    const unsigned scope=pstk_scope_slot;
    if(scope==k) {InterlockedIncrement64(&mpp_ai_inside);return mpp_faction_enter(k)?MPP_FRAME_FACTION:0u;}
    if(scope) {InterlockedIncrement64(&mpp_ai_conflicts);return 0;}
    if(!pslots[k].length || pres_open || !vpl_reentry_clear(pnat_base) || !pslot_scope_begin(k,&pslots[k],"ai_frame")) {
        InterlockedIncrement64(&mpp_ai_refused);return 0;
    }
    InterlockedIncrement64(&mpp_ai_entered);return k|(mpp_faction_enter(k)?MPP_FRAME_FACTION:0u);
}
/* A replayed call of slot k (C5): 1 = inside slot k's scope now (mpp_call_leave after the call), 0 = refused. */
static int mpp_call_enter(unsigned k) {
    if(!mpp_armed || k<2 || k>PILOT_SLOTS || !pslots[k].player || pstk_scope_slot || !pslot_scope_begin(k,&pslots[k],"mission_call")) {
        InterlockedIncrement64(&mpp_call_refused);return 0;
    }
    mpp_faction_enter(k);
    InterlockedIncrement64(&mpp_calls);return 1;
}
static int mpp_call_leave(unsigned k) {
    mpp_generating=0;mpp_depth=0;mpp_story_slot=0;mpp_story_key=0;mpp_story_missed=0;
    mpp_faction_leave(k);   /* the call's own; a frame's inside it closed with that frame */
    if(pstk_scope_slot!=k || !pslot_leave(k)) {InterlockedIncrement64(&mpp_leave_failed);return 0;}
    return 1;
}
/* ---- the active mission per slot ---- */
#define MPP_MGR_ACTIVE 0x78u
#define MPP_MGR_ACTIVE_INDEX 0x80u
#define MPP_MISSIONS_MAX 4096u
static uint64_t mpp_active[PILOT_SLOTS+1];   /* slot k's active mission ID while the manager holds another slot's (2..N) */
static uint64_t mpp_active_saved;            /* the canonical ID while a scope is open */
static unsigned mpp_active_open;             /* the slot whose active mission the manager holds now, 0 = canonical */
static volatile LONG64 mpp_active_swaps,mpp_active_gone,mpp_active_shown,mpp_active_shown_changed;
static uint64_t mpp_active_shown_id;       /* what this client's last presentation scope showed as its active mission */
/* The manager's active mission becomes this ID with its index of now; an ID not in the vector is none. */
static void mpp_active_put(unsigned char *m,uint64_t id) {
    unsigned char *begin=NULL,*end=NULL;uint64_t index=0;int found=0;
    if(id && sample_read(m+UIMIS_MISSIONS,&begin,8) && sample_read(m+UIMIS_MISSIONS+8,&end,8) && begin && end>=begin) {
        size_t n=(size_t)(end-begin)/UIMIS_MISSION_SIZE;
        if(n>MPP_MISSIONS_MAX) n=0;
        for(size_t i=0;i<n && !found;i++) {uint64_t at=0;if(!sample_read(begin+i*UIMIS_MISSION_SIZE,&at,8)) break;if(at==id) {index=i;found=1;}}
    }
    if(id && !found) {id=0;InterlockedIncrement64(&mpp_active_gone);}
    *(volatile uint64_t*)(m+MPP_MGR_ACTIVE)=id;*(volatile uint64_t*)(m+MPP_MGR_ACTIVE_INDEX)=index;
}
/* A scope of slot k opens (the simulation's: pslot_enter; client k's presentation: pres_enter). */
static void mpp_active_enter(unsigned k) {
    unsigned char *m;uint64_t now=0;
    if(!mpp_armed || k<2 || k>PILOT_SLOTS || mpp_active_open || !(m=uimis_manager(UIMIS_MISSION_MANAGER)) || !sample_read(m+MPP_MGR_ACTIVE,&now,8)) return;
    mpp_active_saved=now;mpp_active_put(m,mpp_active[k]);mpp_active_open=k;InterlockedIncrement64(&mpp_active_swaps);
}
/* ...and closes: keep = the simulation's scope (what it left is slot k's active mission from now on); a presentation scope
 * keeps nothing (a change there is counted: the UI's SetActiveMission is captured and replayed, never run here). */
static void mpp_active_leave(unsigned k,int keep) {
    unsigned char *m;uint64_t now=0;
    if(!k || mpp_active_open!=k) return;
    mpp_active_open=0;
    if(!(m=uimis_manager(UIMIS_MISSION_MANAGER)) || !sample_read(m+MPP_MGR_ACTIVE,&now,8)) return;
    if(keep) mpp_active[k]=now;
    else {mpp_active_shown_id=mpp_active[k];InterlockedIncrement64(&mpp_active_shown);if(now!=mpp_active[k] && !(mpp_active[k] && !now)) InterlockedIncrement64(&mpp_active_shown_changed);}
    mpp_active_put(m,mpp_active_saved);
}
/* The claims as the rows compare them: a hash of (cue, slot) in table order. */
static uint64_t mpp_owners_hash(void) {
    uint64_t h=14695981039346656037ULL;
    for(unsigned i=0;i<mpp_count;i++) h=(h^lockstep_mix(mpp_owners[i].cue*31u+mpp_owners[i].slot))*1099511628211ULL;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) if(mpp_active[k]) h=(h^lockstep_mix(mpp_active[k]*31u+k+0x100u))*1099511628211ULL;
    return h;
}
/* The table as one line for the join package: "v2|", "slot:hexid" per claim in table order, comma separated, "|", then
 * "slot:hexid" per slot 2..N with an active mission. 0 = no room. */
static int mpp_state(char *out,size_t cap) {
    int n=snprintf(out,cap,"v2|");unsigned listed=0;
    for(unsigned i=0;n>0 && (size_t)n<cap && i<mpp_count;i++)
        n+=snprintf(out+n,cap-(size_t)n,"%s%u:%llx",i?",":"",mpp_owners[i].slot,(unsigned long long)mpp_owners[i].cue);
    if(n>0 && (size_t)n<cap) n+=snprintf(out+n,cap-(size_t)n,"|");
    for(unsigned k=2;n>0 && (size_t)n<cap && k<=PILOT_SLOTS;k++) if(mpp_active[k])
        n+=snprintf(out+n,cap-(size_t)n,"%s%u:%llx",listed++?",":"",k,(unsigned long long)mpp_active[k]);
    return n>0 && (size_t)n<cap?n:0;
}
/* One "slot:hexid" list up to the text's end or a '|': the rows and their count, -1 malformed. Slots first..PILOT_SLOTS,
 * an ID never 0 and never twice; one_per_slot for the active missions. */
static int mpp_parse_list(const char *text,size_t *at,size_t length,unsigned first,int one_per_slot,mpp_owner_rec *rows,unsigned cap) {
    unsigned count=0;
    while(*at<length && text[*at]!='|') {
        unsigned slot=0;uint64_t id=0;size_t digits=0;
        while(*at<length && text[*at]>='0' && text[*at]<='9' && slot<100) {slot=slot*10u+(unsigned)(text[(*at)++]-'0');++digits;}
        if(!digits || slot<first || slot>PILOT_SLOTS || *at>=length || text[(*at)++]!=':') return -1;
        digits=0;
        for(;*at<length && text[*at]!=',' && text[*at]!='|';(*at)++,digits++) {
            const char c=text[*at];const unsigned v=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):16u;
            if(v>15 || digits>=16) return -1;
            id=id<<4|v;
        }
        if(!digits || !id || count>=cap) return -1;
        for(unsigned i=0;i<count;i++) if(rows[i].cue==id || (one_per_slot && rows[i].slot==slot)) return -1;
        if(*at<length && text[*at]==',' && (++*at==length || text[*at]=='|')) return -1;   /* a trailing comma */
        rows[count].cue=id;rows[count].slot=slot;++count;
    }
    return (int)count;
}
/* A joiner's table and active missions from the saver's line, armed and still empty: 1 restored, 0 refused (malformed, a
 * slot out of range, an ID 0 or repeated, more than the table holds; nothing is kept then). The cues need not be resolvable
 * here: the IDs are the session's (the package's runtime IDs). */
static int mpp_restore(const char *text,size_t length) {
    if(!mpp_armed || mpp_count || mpp_active_open || !text || length<4 || memcmp(text,"v2|",3)) return 0;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) if(mpp_active[k]) return 0;
    static mpp_owner_rec rows[MPP_OWNERS];mpp_owner_rec active[PILOT_SLOTS];size_t at=3;
    const int count=mpp_parse_list(text,&at,length,1,0,rows,MPP_OWNERS);
    if(count<0 || at>=length || text[at++]!='|') return 0;
    const int actives=mpp_parse_list(text,&at,length,2,1,active,PILOT_SLOTS);
    if(actives<0 || at!=length) return 0;
    memcpy(mpp_owners,rows,(size_t)count*sizeof(*rows));mpp_count=(unsigned)count;
    for(int i=0;i<actives;i++) mpp_active[active[i].slot]=active[i].cue;
    return 1;
}
static int mpp_arm(void) {
    if(mpp_armed) return 1;
    if(stats.issued) return 0;   /* before the first tick, as every module the probe arms */
    if(!mpp_hook_install()) return 0;
    mpp_count=0;memset(mpp_active,0,sizeof(mpp_active));mpp_active_open=0;mpp_depth=0;mpp_generating=0;
    mpp_story_slot=0;mpp_story_key=0;mpp_story_missed=0;
    mpp_ai_frames=mpp_ai_entered=mpp_ai_inside=mpp_ai_conflicts=mpp_ai_refused=0;mpp_armed=1;return 1;
}
static int mpp_status(char *out,size_t cap) {
    return snprintf(out,cap,"armed=%d owners=%u hash=%016llx claims=%lld gone=%lld full=%lld frames=%lld entered=%lld inside=%lld "
        "conflicts=%lld refused=%lld calls=%lld call_refused=%lld leave_failed=%lld active=%llx/%llx/%llx swaps=%lld active_gone=%lld "
        "shown=%lld/%lld/%llx generations=%lld generated=%lld/%lld pruned=%lld depth=%u hook=%d/%s faction=%lld/%lld/%lld story=%lld/%lld "
        "ai=%d/%lld/%lld/%lld/%lld/%lld",mpp_armed,mpp_count,(unsigned long long)mpp_owners_hash(),
        (long long)mpp_claims,(long long)mpp_claim_gone,(long long)mpp_full,(long long)mpp_frames,(long long)mpp_entered,(long long)mpp_inside,
        (long long)mpp_conflicts,(long long)mpp_refused,(long long)mpp_calls,(long long)mpp_call_refused,(long long)mpp_leave_failed,
        (unsigned long long)mpp_active[2],(unsigned long long)mpp_active[3],(unsigned long long)mpp_active[4],(long long)mpp_active_swaps,
        (long long)mpp_active_gone,(long long)mpp_active_shown,(long long)mpp_active_shown_changed,(unsigned long long)mpp_active_shown_id,(long long)mpp_generations,(long long)mpp_generated,
        (long long)mpp_created,(long long)mpp_pruned,mpp_depth,mpp_site_installed[0]+mpp_site_installed[1],mpp_hook_why,
        (long long)mpp_faction_swaps,(long long)mpp_faction_same,(long long)mpp_faction_changed,(long long)mpp_story_asked,(long long)mpp_story_claimed,
        mpp_ai_on,(long long)mpp_ai_frames,(long long)mpp_ai_entered,(long long)mpp_ai_inside,(long long)mpp_ai_conflicts,(long long)mpp_ai_refused);
}

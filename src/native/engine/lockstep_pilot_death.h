/* A slot ship's death does not end the session (native_gate_pilot_death, default off; stop reason
 * 163; report LOCKSTEP-VIRTUAL-PLAYERS-RE-20261029 section 16.17).
 *
 * What X4 does (build 9.00). A hull reaching zero runs U::Destructible::vfunc150 518210. If the object
 * is the one the singleton player occupies and it is no spacesuit: when the player's emergency eject
 * is active (710a90: Player+0x6a90, set by the constructor and the save, and the user option
 * "emergencyeject", default on) 70efd0(player, 1,1,1,1) builds a spacesuit next to the ship, with
 * minimum hull 100%, and Player::TeleportTo 7154b0 puts the player in it at once (event: vfunc76
 * 7052f0, which also deletes the old controller). Then, always, Kill 3e51b0 posts a deferred KillEvent;
 * KillRecursive 3e5240 takes the ship and everything below it. A player who is still aboard then gets
 * U::Player::vfunc9 7046e0 with itself ("Game Over - Player has been killed by"), which calls the game
 * over 5c3660: flag 0x400 of 6adf9fc (IsGameOver, read by some 60 functions), SETA off, GameOverEvents
 * for U::Galaxy::vfunc6 5c3d20, the "showGameOver" UI event (CUIController::vfunc89 b3d3e0) and the
 * menu with the load prompt. So in a stock session a ship's death is the eject; the game over needs a
 * player who dies with what it sits in. A later teleport away from a suit kills the suit (vfunc76).
 *
 * Slot 1, X4's own player. The eject above runs inside the simulation, on every client in the same
 * tick, so the slot's dead state is X4's own: the player floats in a suit, the HUD and camera are
 * X4's. The stop reason was only the stock controller going away with it (155). The user option is
 * refused when off (a client without the eject would keep a dead player). The stock kill of the
 * player cannot be reached by a ship death with the eject on, and a player killed in any other way
 * (the suit by a script) never reaches the game over: the guard re-points the call in vfunc9 while
 * the session runs and stops with player_killed (163), no flag, no menu, no save touched.
 *
 * Slots 2..N. P_k is in no child list, so the kill does not touch it, and the eject is never reached
 * for it (the singleton's ship is what vfunc150 compares). Its seat ends: released in its scope when
 * the ship is still whole (the order of the stock eject: release, then the kill arrives), else the
 * controller stays with the dying ship under its vtable copy, its scoped entries doing nothing
 * (pslot_retire). Client k presents X4's own player without the overlay meanwhile: slot 1's view, or
 * slot 1's suit.
 *
 * A suit for slot k (native_gate_pilot_death_suit, default off). X4's way to a suit, 70efd0, ends in
 * Player::TeleportTo, whose event runs on a player outside any scope. The seat needs none of it: MD
 * builds a spacesuit of the player's macro (create_ship, as stock does for an entity; minimum hull as
 * the eject), and P_k takes it with the seat's own take in its scope (U::SpaceSuit keeps Ship's
 * 1074 and 1075). The overlay then presents it with X4's camera and HUD, and client k sees what client 1
 * sees. Its records stay consumed and ignored. Seated PDEATH_SUIT_LAG ticks after the death; the next
 * seat releases it and kills it with reason 13, as vfunc76 does with the player's old suit.
 *
 * The slot state, every client, from the pass of the first tick that sees it (the hull is simulation
 * state, so this is the same tick everywhere): no record of the slot is applied, each is still
 * consumed and its hash stays in the declared state, flight control goes off once as the stock
 * callback would have turned it off, the guns are given up. A controller change whose cause is not the
 * slot's own ship dying still stops with 155. The dead mask is part of the declared state (|D).
 *
 * Respawn (native_gate_pilot_respawn = delay in ticks, needs the MD file of that name): at dead+delay
 * the probe raises the UI event, MD builds a ship of the slot's macro at its start, X4's create_ship
 * and for slot 1 X4's teleport_player, on every client in the same frame. At create+PDEATH_SEAT_LAG
 * (the MD answer can lag) every client takes the slot back: slot 1's controller and ship are latched
 * again and what its last records left held is released; slot k is seated as at the start with its
 * first store. A slot's input held across the death has to be pressed again.
 *
 * On foot (native_gate_pilot_foot, _foot_slots, _foot_bodies; reports/LOCKSTEP-ON-FOOT-20261101.txt section 13). Who is
 * aboard decides, from simulation state on every client:
 *   seated     a death as above (slot 1 leaving, X4's stand-up still pending: the eject took the player, the GET_UP is
 *              void; slot k's body leaves the room before the seat ends, if the kill guard has not taken it already).
 *   off seat   slot 1 with no PlayerController after a GET_UP (X4 stood its player up), slot k walking: the ship's loss
 *              is an asset's, not a death (no eject, no suit, deaths unchanged, event lost:k, PILOT_DEATH_STATUS
 *              losses=). Slot 1 keeps walking as X4 has it and gets no ship (it cannot sit down in a session yet, W7).
 *              Slot k walked in the seat room's frame, which goes with the ship: its walk ends (pfslot_lose), the seat's
 *              pointers go back, and it waits stranded, as a dead slot without a suit (records consumed and ignored,
 *              slot 1's view on client k), until the respawn, if armed, seats it in a new ship at its start.
 *   killed     X4 killing its player on foot (a Player killed with what it stands in) reaches the game over: the guard
 *              stops the session with player_killed (163), no game over, no menu, nothing saved. Our own take-off of a
 *              body (stop+1, a park, the kill guard, a seat that ends) never reaches it: X4's parent change kills an
 *              unheld Player left without a parent ('removed'), so P_k is held for that call (lockstep_pilot_foot_body.h,
 *              pfbody_call; live d1s stopped 163 in the teardown before that). */
#define PDEATH_STOP_REASON 163u
#define PDEATH_LIFECYCLE 0xd3u                /* Component+0xd3: 1 dying, 2 dead (KillRecursive) */
#define PDEATH_SEAT_LAG 15u                   /* past the identity's FOLD_LAG (10): the MD answer is back by then */
#define PDEATH_GAME_OVER 0x5c3660u
#define PDEATH_GAME_OVER_CALL 0x70498au       /* in U::Player::vfunc9 */
#define PDEATH_STUB 0x80u                     /* on the slots' near page, after the presentation's */
#define PDEATH_PLAYER_KILLED_SLOT 9u
#define PDEATH_SUIT_LAG 5u                    /* the MD answer came in MD's own frame in the respawn runs */
#define PDEATH_KILL_SLOT (0x1548u/8u)         /* component vtable: Kill */
#define PDEATH_KILL_REMOVED 13u               /* what vfunc76 kills the player's old suit with */
enum {PDEATH_ALIVE,PDEATH_DEAD,PDEATH_CREATING};
typedef struct {
    unsigned state,deaths,respawns;uint64_t dead_tick,create_tick,seat_tick,ship,suit_tick,suit;int in_suit;
    int lost;unsigned losses;     /* the ship went while the slot was off its seat (an asset's loss, not a death) */
    unsigned out;uint64_t home;   /* slots 2..N through the airlock: 1 leaving (the suit asked for), 2 in its suit; the ship it left */
    unsigned ask,ranged;double range;   /* in its suit: (ask unused since X4's dock handlers run) the probe's distance to the home ship */
    uint64_t dock_tick;                 /* in its suit: the tick of its last dock key or dock request, given to X4's handler */
    unsigned arrive;unsigned char *arrive_room;   /* in its suit: X4 asked to put its player into a ship (through this room, or none named) */
} pdeath_slot;
static int pfbody_release_slot(unsigned k);   /* lockstep_pilot_foot_body.h: slot k's body off its room */
static void pfslot_lose(unsigned k);          /* lockstep_pilot_foot_slots.h: slot k's walk ends without its ship */
static pdeath_slot pdeath_slots[PILOT_SLOTS];
static unsigned pdeath_delay;
static int pdeath_suit;
static int pdeath_pins_checked,pdeath_guard_installed;
static unsigned char pdeath_guard_patched[5];
static const char *pdeath_stage="off";
static const char *pdeath_why="-";
static char pdeath_events[128];
static size_t pdeath_event_length;
static volatile LONG64 pdeath_game_overs;
static unsigned pdeath_released,pdeath_retired;   /* how seats ended: whole ship, or the kill already through */
static const struct {unsigned rva,size;const char *sha;} pdeath_pin_table[]={
    {0x518210,1396,"521c1f3b8aee2f13c5ca701477cc2c8c7061ed928f17bb9774072cdf28734906"},  /* the hull at zero */
    {0x710a90,60,"a45746776fb1d406eab65098063421f2423e9c6be634f6320f876859515db7ba"},    /* eject active */
    {0x70efd0,5055,"00ae115c9190fe969bdaa82d59007880d8281133f9a0e05d2596c4fb728b9bff"},  /* the eject */
    {0x7046e0,1343,"454f24948e328debb1f721c1bfa1ec978d99da02994af76a488c472680845c76"},  /* the player killed */
    {0x5c3660,1726,"72630dcf8622f42576b5979a635ebfde6f52e2653961ea310e942d9975d192f9"},  /* the game over */
    {0x7052f0,1965,"c1344ac923adb1aca9fcf4ef5ed44173145a42631183992a873e131a6d61443c"},  /* the teleport event */
    {0x7154b0,4538,"7f5210c56c082b9baf59be35b6d58c1f4072ee18a3ddcd57ffdd0bea9d77d9ce"},  /* the teleport */
    /* the suit's weapons as the eject installs them (pvehk_equip) */
    {0x4d08a0,0x23d,"73bec8bb05e85b2b1b8b883d2f8be28c9c537b0cd562dc2caa7ab0497b533382"},  /* a ship's weapon of a macro */
    {0x4e54c0,0x37,"2083f135f1e033fe41391afdd048b628ea732a6add082fc7cdc9346b71c6651b"},   /* into its weapon group */
    {0x4e4a70,0x2d,"0ba1635085785d8ec7ed4cf3f1aa4dd81b546856c166d0880d5b630378330125"},   /* the active group */
    {0x59bdb0,0xbb,"e263a08b00707549efe1aba1f6b740af0eadfb7bf15096b5fc36bc0a2dd2702e"},   /* the ship macro's data (its group counts) */
    {0x11ded0,0x1e,"cc74f421cb491db7782bd543d0c3555c2d6fc2a8e739ebd1d1463ac642ccbc71"}};  /* the install description's end */
static int pdeath_pins(void) {
    if(!pdeath_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<sizeof(pdeath_pin_table)/sizeof(*pdeath_pin_table);i++)
            if(!(ok=hash_ok(image+pdeath_pin_table[i].rva,pdeath_pin_table[i].size,pdeath_pin_table[i].sha))) pin_failed_rva=pdeath_pin_table[i].rva;
        if(ok && !(ok=((void**)(image+VPS_PLAYER_VTABLE))[PDEATH_PLAYER_KILLED_SLOT]==(void*)(image+0x7046e0))) pin_failed_rva=VPS_PLAYER_VTABLE;
        pdeath_pins_checked=ok?1:-1;
    }
    return pdeath_pins_checked==1;
}
static int pdeath_fail(const char *why) {
    pdeath_stage="failed";if(!strcmp(pdeath_why,"-")) pdeath_why=why;
    pdeath_failed=1;return 0;
}
static void pdeath_event(const char *kind,unsigned k) {
    int n=snprintf(pdeath_events+pdeath_event_length,sizeof(pdeath_events)-pdeath_event_length,"%s%s:%u",pdeath_event_length?",":"",kind,k);
    if(n>0 && pdeath_event_length+(size_t)n<sizeof(pdeath_events)) pdeath_event_length+=(size_t)n;
}
/* The game over call in U::Player::vfunc9: the player was killed some other way than with a ship. */
static void pdeath_game_over(void) {
    InterlockedIncrement64(&pdeath_game_overs);
    pdeath_stage="failed";if(!strcmp(pdeath_why,"-")) pdeath_why="player_killed";
    pdeath_failed=1;pnat_async_fail("player_killed");
}
/* ---- X4's own entry of a suited player into a ship, for slots 2..N ----
 * 0x710390(player, suit, room, ..) is X4's "this suited player goes into that ship now": called by the dock core's
 * spacesuit branch (0x730ff0 at 0x7313e6: the dock key with the suit at an entry) and by 0x731560 (at 0x7315d5), which
 * U::Player's own update 0x7113e0 runs for EVERY player object every frame: after a dock request it waits for the suit
 * to reach the ship. A slot's player P_k is such an object, so X4 itself decided the arrival and then moved P_k out of
 * the suit as it moves its own player: the suit's PlayerController deleted, P_k standing in the ship. The slot's seat
 * still pointed at that controller: both clients crashed in the slots' update of the same frame (two people, 14 Nov:
 * dock request 2525, the suit flown to the ship, access violation at 0xea29e7 under d95180 at 3618, controller +0x40
 * NULL). X4's decision is kept, its move is not run for a slot's player: both call sites are re-pointed, a call for P_k
 * is noted (arrive, with the room X4 named) and returns, and the pass makes the way home as before (the seat in the
 * suit released, the suit killed as X4 kills its player's old suit, the slot seated in the ship it left). Another
 * ship than the one it left: nothing yet (counted). Every other player passes to X4. Installed with the game over
 * guard (after the UI replay's pins of 0x730ff0 were checked at the arming); pinned, else not installed and X4's own
 * runs. Status enter=installed/noted/elsewhere. */
#define PVEHK_ENTER 0x710390u
#define PVEHK_ENTER_SIZE 0xf7u
#define PVEHK_ENTER_SHA "09b162991c875b59a604b1f4265d485a37d4eb8783ee1caf469cab801b731843"
#define PVEHK_ENTER_SITE_KEY 0x7313e6u
#define PVEHK_ENTER_SITE_FRAME 0x7315d5u
#define PVEHK_ENTER_STUB 0x100u                 /* on the slots' near page, after the lookup's (0xe0) */
typedef void (*pvehk_enter_fn)(void *player,void *suit,void *room,void *fourth,unsigned char fifth);
static int pvehk_enter_installed;
static unsigned char pvehk_enter_patched[2][5];
static volatile LONG64 pvehk_enters,pvehk_enters_else;
static pvehk_enter_fn pvehk_enter_original;   /* tests: in place of 0x710390 */
static void pvehk_enter(void *player,void *suit,void *room,void *fourth,unsigned char fifth) {
    for(unsigned k=2;player && k<=PILOT_SLOTS;k++) {
        if(pslots[k].player!=(unsigned char *)player) continue;
        pdeath_slot *d=&pdeath_slots[k-1];
        if(pdeath_armed && d->out==2 && !pilot_slots[k-1].dead) {d->arrive_room=(unsigned char *)room;d->arrive=1;}
        InterlockedIncrement64(&pvehk_enters);
        return;   /* never X4's move for a slot's player */
    }
    (pvehk_enter_original?pvehk_enter_original:(pvehk_enter_fn)(pnat_base+PVEHK_ENTER))(player,suit,room,fourth,fifth);
}
/* ---- the screen fade of a suit exit and of a ship entry ----
 * X4's eject (0x70efd0, at 0x7100ae) and its entry of a suited player (0x710390's body, at 0x71050d) start the screen's
 * fade to black through 0xfa7330, the routine behind FadeScreen2: it only sets the renderer's fade state (the times,
 * the mode) and answers that state's address, which both callers keep. The action is replayed on every client, so
 * every client's screen went black for a moment when ONE player left its ship or came back (a playtest report of 14
 * Nov: player 1 into its suit, client 2's screen faded too). The two call sites each straddle a 16-byte block (the
 * site helper's one CAS cannot take them: the first form here never installed), so the routine's entry is hooked
 * (its first two instructions run from a trampoline on the slots' near page) and the callers are told apart by the
 * return address: from those two sites the fade runs on the client whose own slot the action is running for (outside
 * any scope: slot 1; inside slot k's scope: slot k) and on every other client nothing is set and the caller gets the
 * same state address; every other caller (the UI's FadeScreen, the game's own) passes. The renderer's state is no
 * part of the simulation. Pinned; else X4's own. Status fade=installed/shown/left out. */
#define PVEHK_FADE 0xfa7330u
#define PVEHK_FADE_SIZE 0x170u
#define PVEHK_FADE_SHA "bec27a340681d75915721d0c085fe3975db8516c4ffed84796f9ee9b5bd5125a"
#define PVEHK_FADE_STATE 0x31bef20u            /* what 0xfa7330 answers */
#define PVEHK_FADE_RET_EJECT 0x7100b3u         /* the return addresses of the two calls */
#define PVEHK_FADE_RET_ENTER 0x710512u
#define PVEHK_FADE_STUB 0x120u                 /* on the slots' near page, after the entry's (0x100): the jump to the wrapper */
#define PVEHK_FADE_TRAMPOLINE 0x140u           /* the routine's first two instructions, then on at +9 */
typedef void *(*pvehk_fade_fn)(double,double,double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static const unsigned char pvehk_fade_head[9]={0x48,0x83,0xec,0x48,0x48,0x8b,0x44,0x24,0x70};   /* sub rsp,0x48; mov rax,[rsp+0x70] */
static int pvehk_fade_installed,pvehk_fade_site_test=-1;   /* tests: 1 the call is from one of the two sites, 0 it is not */
static unsigned char pvehk_fade_patched[5];
static volatile LONG64 pvehk_fades_shown,pvehk_fades_kept;
static pvehk_fade_fn pvehk_fade_original;   /* tests: in place of the trampoline */
__attribute__((noinline)) static void *pvehk_fade(double out,double in,double hold,double now,uint64_t a5,uint64_t a6,uint64_t a7,uint64_t a8,uint64_t a9) {
    const uintptr_t ret=(uintptr_t)__builtin_return_address(0)-(uintptr_t)pnat_base;
    const int site=pvehk_fade_site_test>=0?pvehk_fade_site_test:(ret==PVEHK_FADE_RET_EJECT || ret==PVEHK_FADE_RET_ENTER);
    pvehk_fade_fn original=pvehk_fade_original?pvehk_fade_original:(pvehk_fade_fn)(void*)(pslot_page+PVEHK_FADE_TRAMPOLINE);
    if(site) {
        const unsigned running=pstk_scope_slot?pstk_scope_slot:1u,own=pstk_capture_slot?pstk_capture_slot:1u;
        if(running!=own) {InterlockedIncrement64(&pvehk_fades_kept);return pnat_base+PVEHK_FADE_STATE;}
        InterlockedIncrement64(&pvehk_fades_shown);
    }
    return original(out,in,hold,now,a5,a6,a7,a8,a9);
}
static void pvehk_fade_install(void) {
    DWORD old,ignored;int32_t rel=0;
    if(pvehk_fade_installed || !pslot_page || !hash_ok(pnat_base+PVEHK_FADE,PVEHK_FADE_SIZE,PVEHK_FADE_SHA) ||
       memcmp(pnat_base+PVEHK_FADE,pvehk_fade_head,sizeof(pvehk_fade_head))) return;
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return;
    pview_abs_jump(pslot_page+PVEHK_FADE_STUB,(void*)pvehk_fade);
    memcpy(pslot_page+PVEHK_FADE_TRAMPOLINE,pvehk_fade_head,sizeof(pvehk_fade_head));
    pview_abs_jump(pslot_page+PVEHK_FADE_TRAMPOLINE+sizeof(pvehk_fade_head),pnat_base+PVEHK_FADE+sizeof(pvehk_fade_head));
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return;
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    rel=(int32_t)((intptr_t)(pslot_page+PVEHK_FADE_STUB)-(intptr_t)(pnat_base+PVEHK_FADE+5));
    pvehk_fade_patched[0]=0xe9;memcpy(pvehk_fade_patched+1,&rel,4);
    if(pview_cas(PVEHK_FADE,pvehk_fade_head,pvehk_fade_patched,5)) pvehk_fade_installed=1;
}
static void pvehk_fade_restore(void) {
    if(pvehk_fade_installed && pview_cas(PVEHK_FADE,pvehk_fade_patched,pvehk_fade_head,5)) pvehk_fade_installed=0;
}
/* ---- a flying slot's reference frame while X4's own player has no flight control ----
 * A PlayerController with a reference object (+0x19390: set while it docks) refreshes its frame offsets in 0xd9b250,
 * which walks up from the environment zone (image 0x6b661e8, one of the slot bank's regions) without a test for
 * none. X4 calls it from the controller's pose query (0xd8d096) during its own tree walk, outside every scope, where
 * the zone is X4's own player's: none while that player is on foot. In the plain game the only PlayerController is
 * that player's, so the two never meet. Here slot k's suit docked at its ship while slot 1 stood in its own (scripted,
 * two clients, 15 Nov): both clients faulted at 0xd9b279 in the same tick. The routine's entry is hooked (as the
 * fade's): for slot k's controller outside its scope the zone is slot k's own for the call (its stored bank's value,
 * on the frame thread; the live one back after it); a zone that is none: the offsets stay as they are, no call.
 * Pinned; else X4's own. Status zone=installed/own/none. */
#define PVEHK_ZONE 0xd9b250u
#define PVEHK_ZONE_SIZE 0x150u
#define PVEHK_ZONE_SHA "fd0f07686216a79b10edba28734546d7b2d301793798b5003884996c96965679"
#define PVEHK_ZONE_POINTER 0x6b661e8u         /* lockstep_virtual_players.h: environment_zone */
#define PVEHK_ZONE_STUB 0x1c0u                /* on the slots' near page, after the speech's (0x180, 0x1a0) */
#define PVEHK_ZONE_TRAMPOLINE 0x1e0u          /* the routine's first instruction, then on at +5 */
static const unsigned char pvehk_zone_head[5]={0x48,0x89,0x74,0x24,0x20};
typedef void (*pvehk_zone_fn)(void *controller);
static int pvehk_zone_installed;
static unsigned char pvehk_zone_patched[5];
static volatile LONG64 pvehk_zone_own,pvehk_zone_none;
static pvehk_zone_fn pvehk_zone_original;   /* tests: in place of the trampoline */
/* Slot k's environment zone while its scope is closed: its place in the slot's stored bank (pslot_exchange's order). */
static int pvehk_zone_stored(unsigned k,void **zone) {
    unsigned n=0;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(r->base!=VPL_BASE_IMAGE || !(PSLOT_CLASSES>>r->cls&1u)) continue;
        if(r->offset==PVEHK_ZONE_POINTER && r->size==8 && n+8u<=pslots[k].length) {memcpy(zone,pslots[k].store+n,8);return 1;}
        n+=r->size;
    }
    return 0;
}
__attribute__((noinline)) static void pvehk_zone(void *controller) {
    const pvehk_zone_fn original=pvehk_zone_original?pvehk_zone_original:(pvehk_zone_fn)(void*)(pslot_page+PVEHK_ZONE_TRAMPOLINE);
    void *volatile *const live=(void*volatile*)(pnat_base+PVEHK_ZONE_POINTER);
    const unsigned k=pslot_of(controller);
    void *own=NULL;
    if(k && pstk_scope_slot!=k && pslot_frame_thread() && pvehk_zone_stored(k,&own)) {
        if(!own) {InterlockedIncrement64(&pvehk_zone_none);return;}
        void *const was=*live;
        *live=own;original(controller);*live=was;
        InterlockedIncrement64(&pvehk_zone_own);return;
    }
    if(!*live) {InterlockedIncrement64(&pvehk_zone_none);return;}
    original(controller);
}
static void pvehk_zone_install(void) {
    DWORD old,ignored;int32_t rel=0;
    if(pvehk_zone_installed || !pslot_page || !hash_ok(pnat_base+PVEHK_ZONE,PVEHK_ZONE_SIZE,PVEHK_ZONE_SHA) ||
       memcmp(pnat_base+PVEHK_ZONE,pvehk_zone_head,sizeof(pvehk_zone_head))) return;
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return;
    pview_abs_jump(pslot_page+PVEHK_ZONE_STUB,(void*)pvehk_zone);
    memcpy(pslot_page+PVEHK_ZONE_TRAMPOLINE,pvehk_zone_head,sizeof(pvehk_zone_head));
    pview_abs_jump(pslot_page+PVEHK_ZONE_TRAMPOLINE+sizeof(pvehk_zone_head),pnat_base+PVEHK_ZONE+sizeof(pvehk_zone_head));
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return;
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    rel=(int32_t)((intptr_t)(pslot_page+PVEHK_ZONE_STUB)-(intptr_t)(pnat_base+PVEHK_ZONE+5));
    pvehk_zone_patched[0]=0xe9;memcpy(pvehk_zone_patched+1,&rel,4);
    if(pview_cas(PVEHK_ZONE,pvehk_zone_head,pvehk_zone_patched,5)) pvehk_zone_installed=1;
}
static void pvehk_zone_restore(void) {
    if(pvehk_zone_installed && pview_cas(PVEHK_ZONE,pvehk_zone_patched,pvehk_zone_head,5)) pvehk_zone_installed=0;
}
/* ---- X4's entry check for a suited slot while X4's own player has no flight control ----
 * U::Player's update (0x7113e0) asks every frame whether its ship is docked and has arrived (0x7ced40: the dock it is
 * connected to, state +0x368 not 6; 0x7d1c70) and then runs 0x731560, which for a spacesuit is the entry 0x710390.
 * That whole part sits behind one test of the flight control byte (0x712160: image 0x2f4b828), which outside the
 * scopes is X4's own player's. With slot 1 on foot the check never ran for anybody: a slot's suit flew to its ship,
 * docked and stayed outside (scripted 15 Nov, limit after 900 ticks 2 m from the ship; the same by hand: the suit at
 * the right place, nothing happens). While that byte is off, the pass asks X4's same three routines for every slot
 * in its suit; 0x731560's entry call is this module's (pvehk_enter above), so an arrival is noted as always. Pinned
 * (0x731560 up to that call); else nothing. Status poll=on/asked/entered. */
#define PVEHK_DOCKED 0x7ced40u                /* (ship, &dock) */
#define PVEHK_DOCKED_SIZE 0x6du
#define PVEHK_DOCKED_SHA "0122c090f3b4047b9ade0772cf0e131d122b02794d89aef09c6d7e4ca5649fa2"
#define PVEHK_ARRIVED 0x7d1c70u               /* (ship) */
#define PVEHK_ARRIVED_SIZE 0x84u
#define PVEHK_ARRIVED_SHA "af84c27d9efd9e487504a551eea3e57a3afb102e4c1b431c0f135133472efad5"
#define PVEHK_DOCK_STEP 0x731560u             /* (player) */
#define PVEHK_DOCK_STEP_HEAD 0x75u
#define PVEHK_DOCK_STEP_SHA "33fcd7d1956a1bc1e9f66c6fc0c0f768a0cbcf4a44c52b28be018a97e3409477"
#define PVEHK_DOCK_GATE 0x712160u             /* 0x7113e0: the flight control byte, then the three */
#define PVEHK_DOCK_GATE_SIZE 0x86u
#define PVEHK_DOCK_GATE_SHA "b7872ef6dff4fd3fecfe8e0a3e5bc9e0223ad031daa3bef72e7e5f66ccb96641"
#define PVEHK_DOCK_STATE 0x368u
static int pvehk_poll_on;
static volatile LONG64 pvehk_polls,pvehk_poll_entered;
static void pvehk_poll_pins(void) {
    pvehk_poll_on=pnat_base && hash_ok(pnat_base+PVEHK_DOCKED,PVEHK_DOCKED_SIZE,PVEHK_DOCKED_SHA) &&
        hash_ok(pnat_base+PVEHK_ARRIVED,PVEHK_ARRIVED_SIZE,PVEHK_ARRIVED_SHA) &&
        hash_ok(pnat_base+PVEHK_DOCK_STEP,PVEHK_DOCK_STEP_HEAD,PVEHK_DOCK_STEP_SHA) &&
        hash_ok(pnat_base+PVEHK_DOCK_GATE,PVEHK_DOCK_GATE_SIZE,PVEHK_DOCK_GATE_SHA);
}
static void pvehk_dock_poll(unsigned k) {
    pslot_slot *x=&pslots[k];unsigned char *dock=NULL;int state=6,fault=0;
    if(!pvehk_poll_on || !pvehk_enter_installed || !x->seated || !x->ship || !x->player || *PNAT_U8(PNAT_FLIGHT)) return;
    InterlockedIncrement64(&pvehk_polls);
    __try {
        (void)((void*(*)(void*,void**))(void*)(pnat_base+PVEHK_DOCKED))(x->ship,(void**)&dock);
        if(dock && read_local(dock+PVEHK_DOCK_STATE,&state,4) && state!=6 && ((unsigned char(*)(void*))(void*)(pnat_base+PVEHK_ARRIVED))(x->ship)) {
            ((void(*)(void*))(void*)(pnat_base+PVEHK_DOCK_STEP))(x->player);InterlockedIncrement64(&pvehk_poll_entered);
        }
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    if(fault) pvehk_poll_on=0;
}
static void pvehk_enter_install(void) {
    if(pvehk_enter_installed || !pslot_page || !hash_ok(pnat_base+PVEHK_ENTER,PVEHK_ENTER_SIZE,PVEHK_ENTER_SHA)) return;
    if(!pres_site_install(PVEHK_ENTER_SITE_FRAME,PVEHK_ENTER,PVEHK_ENTER_STUB,(void*)pvehk_enter,pvehk_enter_patched[0])) return;
    if(!pres_site_install(PVEHK_ENTER_SITE_KEY,PVEHK_ENTER,PVEHK_ENTER_STUB,(void*)pvehk_enter,pvehk_enter_patched[1])) {
        (void)pres_site_uninstall(PVEHK_ENTER_SITE_FRAME,PVEHK_ENTER,pvehk_enter_patched[0]);return;
    }
    pvehk_enter_installed=1;
}
static void pvehk_enter_restore(void) {
    if(!pvehk_enter_installed) return;
    if(pres_site_uninstall(PVEHK_ENTER_SITE_FRAME,PVEHK_ENTER,pvehk_enter_patched[0]) &&
       pres_site_uninstall(PVEHK_ENTER_SITE_KEY,PVEHK_ENTER,pvehk_enter_patched[1])) pvehk_enter_installed=0;
}
static int pdeath_guard_install(void) {
    if(pdeath_guard_installed) return 1;
    if(!pslot_page && !(pslot_page=pslot_alloc_page())) return 0;
    if(!pres_site_install(PDEATH_GAME_OVER_CALL,PDEATH_GAME_OVER,PDEATH_STUB,(void*)pdeath_game_over,pdeath_guard_patched)) return 0;
    pvehk_poll_pins();       /* before the entry's call site inside 0x731560 is re-pointed */
    pvehk_enter_install();   /* X4's entry of a suited player (above): where this image has it */
    pvehk_fade_install();    /* the fade of an exit or an entry: on the client whose player it is */
    pvehk_zone_install();    /* a flying slot's reference frame: its own environment zone */
    pdeath_guard_installed=1;return 1;
}
static void pdeath_restore(void) {
    pvehk_zone_restore();
    pvehk_fade_restore();
    pvehk_enter_restore();
    if(pdeath_guard_installed && pres_site_uninstall(PDEATH_GAME_OVER_CALL,PDEATH_GAME_OVER,pdeath_guard_patched)) pdeath_guard_installed=0;
}
/* What a slot 1 record left held (states and ranges): released at the seat, on every client. */
static unsigned char pdeath_held[2][512];
static void pdeath_note(const pstk_event *x) {
    if((x->kind==PSTK_STATE || x->kind==PSTK_RANGE) && x->id<512) pdeath_held[x->kind==PSTK_RANGE][x->id]=x->value!=0;
}
static int pdeath_release_held(void) {
    unsigned char *map=pnat_input+0x63e0;
    for(unsigned kind=0;kind<2;kind++) for(unsigned id=0;id<512;id++) {
        if(!pdeath_held[kind][id]) continue;
        unsigned which=kind?PSTK_RANGE:PSTK_STATE;
        pnat_node *upper=NULL,*n=pnat_first((pnat_node*)(map+0x40*which),(int)id,&upper);
        for(;n && n!=upper;n=pnat_next(n)) if(!pstk_handler_ok(n->handler)) return 0;
        pstk_walk(map+0x40*which,which,(int)id,0);
        pdeath_held[kind][id]=0;
    }
    return 1;
}
/* The slot's ship is going or gone; a ship that resolved once and a registry that no longer knows it,
 * or a hull at zero (518210's own test). */
static int pdeath_dying(const pilot_slot *s) {
    double hull=1.0;
    if(!s->seen) return 0;
    if(!s->found) return 1;
    return read_local(s->found+PDEATH_HULL,&hull,8) && fabs(hull)<PDEATH_HULL_EPSILON;
}
static int pdeath_die(unsigned k,uint64_t tick) {
    pilot_slot *s=&pilot_slots[k-1];pdeath_slot *d=&pdeath_slots[k-1];
    if(k==1) {
        /* X4's own controller went with the ship: the stock callback's half of leaving flight control,
         * on every client, because its local half (pstk_space) only records. */
        if(pstk_flight) {
            double now=0;
            if(!pnat_fn.time(&now)) return pdeath_fail("die_time");
            pstk_space_replay(0,now);
        }
        pstk_flight=0;pstk_flight_known=1;pnat_controller=NULL;pnat_ship=NULL;
        pfoot_void_leave(tick);   /* a GET_UP X4 had not taken yet: the eject took the player into its suit */
    } else {
        pslot_slot *x=&pslots[k];unsigned char lifecycle=1;
        if(!pfbody_release_slot(k)) {pdeath_why=pfbody_why;return 0;}   /* the body off its room before the seat ends */
        if(x->seated) {
            if(s->found==x->ship && read_local(x->ship+PDEATH_LIFECYCLE,&lifecycle,1) && !lifecycle && pslot_controller_has(x->ship,0,x->controller)==1) {
                if(!pslot_release(k)) {pdeath_why=pslot_why;return 0;}
                ++pdeath_released;
            } else {pslot_retire(k);++pdeath_retired;}
        }
        pstk_slot_frames[k-1].flight=0;pstk_slot_frames[k-1].flight_known=1;
        if(k==pres_slot) {pres_anchor_home();pres_target_set=0;pres_local_target=0;pview_slot_target=0;}
    }
    if(pfire_armed) {(void)pfire_end(s,&pfire_slots[k-1],k);memset(&pfire_slots[k-1],0,sizeof(pfire_slots[k-1]));}
    s->dead=1;s->resolved=0;s->object=s->agent=s->solver=NULL;s->player=0;
    pdeath_dead|=1u<<(k-1);
    d->state=PDEATH_DEAD;d->dead_tick=tick;++d->deaths;d->ship=0;
    if(pdeath_delay) {d->create_tick=tick+pdeath_delay;d->seat_tick=d->create_tick+PDEATH_SEAT_LAG;}
    pdeath_event("die",k);
    if(pdeath_suit && k>=2) {d->suit_tick=tick+PDEATH_SUIT_LAG;d->suit=0;d->in_suit=0;pdeath_event("suit",k);}
    return 1;
}
/* The slot is not aboard its ship: slot 1 off its seat with no PlayerController (X4 stood its player up; a player
 * who was aboard sits in the eject's suit, which has one), slot k walking (lockstep_pilot_foot_slots.h). */
static int pdeath_off_seat(unsigned k,const unsigned char *controller) {
    if(k==1) return pfoot_armed && pfoot_state!=PFOOT_SEATED && !controller;
    return pslots[k].seated && pslots[k].walking;
}
/* Its ship went while the slot was off its seat: the loss of an asset, not a death (header). */
static int pdeath_lose(unsigned k,uint64_t tick) {
    pilot_slot *s=&pilot_slots[k-1];pdeath_slot *d=&pdeath_slots[k-1];
    if(pfire_armed) {(void)pfire_end(s,&pfire_slots[k-1],k);memset(&pfire_slots[k-1],0,sizeof(pfire_slots[k-1]));}
    d->lost=1;++d->losses;d->ship=0;
    pdeath_event("lost",k);
    if(k==1) {pnat_ship=NULL;return 1;}   /* X4's player walks on; pilot_resolve finds no ship each pass */
    /* Slot k: the walk in the seat room's frame ends before that room goes; then the seat's pointers. */
    pfslot_lose(k);
    if(!pfbody_release_slot(k)) {pdeath_why=pfbody_why;return 0;}   /* none while it walks (parked); for the order */
    if(!pslot_release(k)) {pdeath_why=pslot_why;return 0;}
    pstk_slot_frames[k-1].flight=0;pstk_slot_frames[k-1].flight_known=1;
    if(k==pres_slot) {pres_anchor_home();pres_target_set=0;pres_local_target=0;pview_slot_target=0;}
    /* Stranded: what a dead slot without a suit is, until a respawn seats it. */
    s->dead=1;s->resolved=0;s->object=s->agent=s->solver=NULL;s->player=0;
    pdeath_dead|=1u<<(k-1);
    d->state=PDEATH_DEAD;d->dead_tick=tick;
    if(pdeath_delay) {d->create_tick=tick+pdeath_delay;d->seat_tick=d->create_tick+PDEATH_SEAT_LAG;}
    return 1;
}
/* A dead slot k: into the suit MD built, on every client at the same tick. */
static int pdeath_suit_pass(unsigned k,uint64_t tick) {
    pslot_slot *x=&pslots[k];pdeath_slot *d=&pdeath_slots[k-1];
    if(d->in_suit) {
        /* Not by a hit (minimum hull). The seat ends, the slot stays dead: slot 1's view again. */
        if(x->seated && pdeath_find(pdeath_map,x->runtime,4)!=x->ship) {pslot_retire(k);d->in_suit=0;pdeath_event("nosuit",k);}
        return 1;
    }
    if(!d->suit_tick || tick<d->suit_tick) return 1;
    d->suit_tick=0;
    if(!d->suit) return pdeath_fail("suit_no_ship");
    unsigned char *suit=pdeath_find(pdeath_map,d->suit,4);
    if(!suit) return pdeath_fail("suit_gone");
    if(!pslot_seat_ship(k,suit)) {pdeath_why=pslot_why;return 0;}
    d->in_suit=1;pdeath_event("insuit",k);
    return 1;
}
/* create+lag: the ship MD built is the slot's now. */
static int pdeath_seat(unsigned k,unsigned char *controller) {
    pilot_slot *s=&pilot_slots[k-1];pdeath_slot *d=&pdeath_slots[k-1];unsigned char *back=NULL;
    if(!d->ship) return pdeath_fail("respawn_no_ship");
    if(k>=2 && d->in_suit) {
        /* Out of the suit first, and it goes as the player's old suit goes after a teleport (7052f0). */
        unsigned char *suit=pslots[k].ship;void **vtable=NULL;double now=0;
        if(!pfbody_release_slot(k)) {pdeath_why=pfbody_why;return 0;}   /* a dead slot has no body; for the order (pvehk_pass) */
        if(pslots[k].seated && !pslot_release(k)) {pdeath_why=pslot_why;return 0;}
        d->in_suit=0;
        if(pnat_fn.time(&now) && read_local(suit,&vtable,8) && vtable) ((hitn_kill_fn)vtable[PDEATH_KILL_SLOT])(suit,PDEATH_KILL_REMOVED,NULL,now);
    }
    s->runtime=d->ship;s->reversed=1;s->renewed=1;s->dead=0;s->seen=0;
    if(!pilot_resolve(s,pdeath_find,pdeath_map,pdeath_player)) {s->dead=1;return pdeath_fail("respawn_unresolved");}
    if(k==1) {
        if(!controller || !read_local(controller+0x28,&back,8) || back!=s->object) {
            s->dead=1;s->resolved=0;s->object=NULL;return pdeath_fail("respawn_not_in_control");
        }
        pnat_controller=controller;pnat_ship=s->object;
        pstk_flight=*PNAT_U8(PNAT_FLIGHT);pstk_flight_known=1;
        if(!pdeath_release_held()) return pdeath_fail("respawn_foreign_handler");
    } else {
        pstk_slot_frames[k-1].flight=0;pstk_slot_frames[k-1].flight_known=1;
        if(!pslot_seat(k)) {pdeath_why=pslot_why;return 0;}
    }
    memset(&pfire_slots[k-1],0,sizeof(pfire_slots[k-1]));
    /* The local controls stayed active through the death: the edge the new flight control needs. */
    if(pstk_capturing && pstk_capture_slot==k && pstk_local_active==1) pstk_record_event(PSTK_SPACE,8,1u);
    pdeath_dead&=~(1u<<(k-1));
    d->state=PDEATH_ALIVE;++d->respawns;d->lost=0;   /* a stranded slot has a ship again */
    pdeath_event("live",k);
    return 1;
}
/* ---- slot 1's vehicle changes that are no death (package 1; lockstep_ui_compslot.h allows them) ----
 * X4's own player leaves its ship through an airlock: U::CycleAirlockAction, the room's AirlockCycledEvent handler
 * 0x773340 and the eject 0x70efd0 build a spacesuit and Player::TeleportTo puts the player into it, on every client in
 * the same step once the action is replayed. The player then has a PlayerController on another ship than slot 1's, which
 * the walking pass took for a stop (foot_controller). With an applied airlock action behind it (pveh_allow, a window of
 * PVEH_WINDOW ticks: the action is a timed one) the pass adopts the suit as slot 1's ship, as the respawn adopts a new
 * ship: the slot's runtime ID renewed and resolved, the controller and ship latched, the guns given up. Flight control
 * comes as at a sit-down: X4's context edge on slot 1's client, recorded and replayed.
 * The way back: the suit docks at a ship (the dock family), X4 teleports the player in and kills the suit ('removed',
 * 0x7052f0). A suit of slot 1 that is gone while the player lives is that, not a death: the slot has no ship (as after a
 * loss on foot), walks, and the next seat X4 gives its player is adopted. X4 takes the player out first and removes the
 * suit after: right at the ship the player stood inside 19 ticks after its dock request with the suit still whole, and
 * the pass took the missing PlayerController of a seated slot for a stop (two people, 13 Nov: both gates stopped with
 * stock_controller_changed at the tick the player arrived; from 500 m away the day before the suit had gone in the same
 * pass). So the sign is the player off the suit's controls while it lives, with the suit whole or not. Events suit1 /
 * seat1 / left1 in
 * PILOT_DEATH's frame; status vehicle=adopts/lefts/in_suit. Slots 2..N: not yet (their player is not X4's own). */
#define PVEH_WINDOW 1800u
#define PVEH_SUIT_VTABLE 0x2b789b0u            /* U::SpaceSuit */
static uint64_t pveh_allow_until,pveh_adopts,pveh_lefts;   /* pveh_in_suit: lockstep_pilot_native.h (the stop's text) */
static const char *pveh_why="-";
static void pveh_allow(uint64_t tick) {pveh_allow_until=tick+PVEH_WINDOW;}
/* Another ship than slot 1's holds X4's player: expected when the slot has no ship, or inside the window of an applied
 * action. */
static int pveh_adopt_ok(uint64_t tick,const unsigned char *ship_now) {
    if(!pdeath_armed) return 0;
    return !ship_now || (pveh_allow_until && tick<=pveh_allow_until);
}
static int pveh_adopt(uint64_t tick,unsigned char *controller) {
    pilot_slot *s=&pilot_slots[0];pdeath_slot *d=&pdeath_slots[0];unsigned char *ship=NULL;uint64_t id=0;void *vtable=NULL;
    (void)tick;
    if(!controller || !read_local(controller+0x28,&ship,8) || !ship || !read_local(ship+8,&id,8) || !id) {pveh_why="vehicle_controller";return 0;}
    if(pfire_armed) {(void)pfire_end(s,&pfire_slots[0],1);memset(&pfire_slots[0],0,sizeof(pfire_slots[0]));}
    s->runtime=id;s->reversed=1;s->renewed=1;s->dead=0;s->seen=0;
    if(!pilot_resolve(s,pdeath_find,pdeath_map,pdeath_player) || s->object!=ship) {pveh_why="vehicle_unresolved";return 0;}
    pnat_controller=controller;pnat_ship=ship;
    d->lost=0;d->state=PDEATH_ALIVE;
    pveh_in_suit=read_local(ship,&vtable,8) && pnat_base && vtable==(void*)(pnat_base+PVEH_SUIT_VTABLE);
    pveh_allow_until=0;++pveh_adopts;pdeath_event(pveh_in_suit?"suit1":"seat1",1);
    return 1;
}
/* Slot 1's player is off its suit and lives (the suit gone, or X4's player without a PlayerController): X4 teleported it
 * into a ship. 1: handled here.
 * The life asked is the Player entity's (slot 1's own, X4's singleton outside every scope). Until 15 Nov it was asked of
 * pdeath_player, which is the object the PlayerController holds (the pass's player_object) and so none exactly when
 * the controller is: this never took, and slot 1's dock-in from its suit stopped both gates with
 * stock_controller_changed (scripted, two clients, 26 ticks after the dock request: changed=ctl:none ... suit:1). */
static const unsigned char *pveh_player_test;   /* tests: in place of X4's player */
/* ...and where X4 put it. Its entry from a docked suit (0x710390 -> 0x7154b0) stands the player on foot in the ship the
 * suit docked at. That ship is slot 1's from there on, as after a stand-up in it: no controller, the walking pass adopts
 * the next seat in it. Left without a ship (the first form, 15 Nov, scripted) the slot's viewpoint stayed the removed
 * suit: the ship around X4's player lost its walk mesh and its interior within a few seconds (the digest part navmesh
 * changed 70 ticks after the entry), client 1 showed open space and the walking keys moved nothing. A ship is the
 * player's first ancestor of the ship class (716e20's own walk, vtable +0x11e8 with 0x70); a spacesuit (still under
 * its own suit) or a ship that is going is none. Asked again every pass for PVEH_WINDOW ticks after the suit was left,
 * while slot 1 lives without a ship and without a controller (X4 may take the player in some ticks after it took the
 * controls); a slot whose ship died under its feet is not asked (it has no ship: the loss's rule). Event aboard1; status
 * vehicle=../../../aboards. */
#define PVEH_SHIP_CLASS 0x70
static uint64_t pveh_aboards,pveh_aboard_until;
static unsigned char *(*pveh_around_test)(const unsigned char *);   /* tests: in place of the walk up the parents */
static unsigned char *pveh_ship_around(const unsigned char *player) {
    unsigned char *c=NULL;
    if(pveh_around_test) return pveh_around_test(player);
    if(!player || !read_local(player+0x70,&c,8)) return NULL;
    for(unsigned depth=0;c && depth<32;depth++) {
        void **vtable=NULL;unsigned char is=0;int fault=0;
        if(!read_local(c,&vtable,8) || !vtable) return NULL;
        __try {is=((unsigned char(*)(void*,int))vtable[0x11e8u/8u])(c,PVEH_SHIP_CLASS);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
        if(fault) return NULL;
        if(is) return c;
        if(!read_local(c+0x70,&c,8)) return NULL;
    }
    return NULL;
}
static int pveh_aboard(pilot_slot *s,pdeath_slot *d) {
    const unsigned char *player=pveh_player_test?pveh_player_test:pslot_p1?pslot_p1:pnat_player();
    unsigned char life=1,*ship;uint64_t id=0;void *vtable=NULL;
    if(!d->lost || s->dead || pveh_in_suit || !player || !read_local(player+PDEATH_LIFECYCLE,&life,1) || life) return 0;
    if(!(ship=pveh_ship_around(player)) || !read_local(ship+8,&id,8) || !id || !read_local(ship+PDEATH_LIFECYCLE,&life,1) || life) return 0;
    if(read_local(ship,&vtable,8) && pnat_base && vtable==(void*)(pnat_base+PVEH_SUIT_VTABLE)) return 0;
    const uint64_t was=s->runtime;
    s->runtime=id;s->reversed=1;s->renewed=1;s->seen=0;
    if(!pilot_resolve(s,pdeath_find,pdeath_map,pdeath_player) || s->object!=ship) {s->runtime=was;s->seen=0;return 0;}   /* stays without a ship */
    pnat_ship=ship;pnat_controller=NULL;d->lost=0;d->state=PDEATH_ALIVE;++pveh_aboards;pveh_aboard_until=0;pdeath_event("aboard1",1);
    return 1;
}
static int pveh_left(pilot_slot *s,pdeath_slot *d,uint64_t tick,const unsigned char *controller) {
    unsigned char life=1;
    if(!pveh_in_suit || s->dead || d->lost || (controller && !pdeath_dying(s))) return 0;
    const unsigned char *player=pveh_player_test?pveh_player_test:pslot_p1?pslot_p1:pnat_player();
    if(!player || !read_local(player+PDEATH_LIFECYCLE,&life,1) || life) return 0;   /* the player goes too: a death */
    pveh_in_suit=0;++pveh_lefts;
    if(pfire_armed) {(void)pfire_end(s,&pfire_slots[0],1);memset(&pfire_slots[0],0,sizeof(pfire_slots[0]));}
    d->lost=1;d->ship=0;   /* no ship until X4 seats its player again (pveh_adopt_ok: any seat) */
    pnat_ship=NULL;pnat_controller=NULL;
    if(pfoot_armed) {pfoot_state=PFOOT_WALKING;pfoot_transition_tick=tick;}
    pdeath_event("left1",1);
    pveh_aboard_until=tick+PVEH_WINDOW;(void)pveh_aboard(s,d);
    return 1;
}
/* ---- slots 2..N through the airlock (package 1) ----
 * X4's airlock action ends in the eject of its own player (above), slot 1's. For slot k the way is the death suit's: at
 * the apply of slot k's captured airlock action (lockstep_ui_compslot.h: every client, the same tick, never X4's action)
 * the slot is marked leaving (event out:k). The probe has MD build a spacesuit at the ship (the Suit cue, in the ship's
 * sector), MD's answer comes through gate.pilot_death_suit, and PDEATH_SUIT_LAG ticks after the apply every client ends
 * the walk (as a lost ship ends it, without the stranded mark), gives the seat's pointers back and seats P_k in the suit
 * as a respawn seats it in a new ship: the slot's runtime ID renewed, its records fly the suit (event suited:k). The ship
 * it left stays where it is: the slot's home. No answer by then, or the slot no longer walking: nothing (noout:k).
 * The same without the console: a walking slot's dock key (DOCK_ACTION, UNDOCK) or dock request in its record is the way
 * out too (pvehk_home_key), for a client whose on-foot click does not reach the console.
 * The way back is X4's own [F, pinned 9.00]. In the plain game a player in a spacesuit targets a ship and gives the dock
 * key or the interact menu's dock request: the dock core 0x730ff0 sees its player's ship is a spacesuit (class 0x60),
 * walks the target's entries, takes the one the player's interactables list holds in reach (0x73aeb0, state 4) and docks
 * the suit there (0x710390 -> 0x4a76a0); X4 then puts the player inside and removes the suit. Slot k's dock keys
 * (DOCK_ACTION 0xaf, UNDOCK 0xd3) and its dock requests (lockstep_ui_replay.h, the dock family) ride its record and its
 * UI events into those same handlers in slot k's scope, where the singleton is P_k, its ship the suit and its target the
 * slot's: every client, the same tick. Until 13 Nov this module took them for itself in a suit and seated the slot in
 * its ship from up to 1000 m away (and on GET_UP too, which the game never does: playtest reports of 12 Nov); the
 * direction since then is the game's behaviour, not a rule of ours. GET_UP in the suit is consumed and void (X4's
 * stand-up is never run for P_k).
 * What is left here is to notice what X4 did: the suit under the ship it left (docked at it: the suit's parent chain
 * reaches the ship), or, after a dock key (PVEHK_DOCK_WINDOW ticks), P_k alive with a parent chain that reaches that ship
 * and not the suit (X4 took it in). Then the seat in the suit is released (a suit X4 already removed: retired), a suit
 * still whole is killed as X4 kills its player's old suit (reason 13), and the slot is seated in its home ship (home:k).
 * Still ours, to be replaced by X4's arrival on foot: the seat. A home ship that is gone leaves the slot in its suit.
 * Needs native_gate_pilot_death_suit. Status airlock=outs/homes/dock keys given to X4, taken=<homes by X4's take>,
 * d<k>=.../out<state>. Not yet: a hot join while a slot is out (its state is not in the join's tables), a suit that can
 * die (minimum hull as the death suit's), oxygen. */
#define PVEHK_HOME_RANGE 1000.0
#define PVEHK_DOCK_WINDOW 900u       /* ticks after a suited slot's dock key in which X4 taking its player in is looked for */
static int uics_under(const unsigned char *component,const unsigned char *ship);   /* lockstep_ui_compslot.h */
static void pfbody_place_seat(unsigned k);   /* lockstep_pilot_foot_body.h: P_k's place for MD in a seat without a body */
/* The suit's weapon groups [F, pinned: the eject 70efd0]. X4 builds its player's suit WITH its equipment: 70f7ff..70f94c
 * puts the engine and the component macros of the player's suit wares (inventory) into a loadout and 862580 creates the
 * suit from it. Then (70fa2f..70ff17) it restores the weapon groups the Player keeps for a suit: two tables (Player+0x6a58
 * primary, +0x6a70 secondary: vectors of {begin,end,cap} of macro pointers); for each macro 4d08a0(suit,&weapon,&filter)
 * FINDS the suit's weapon of that macro (the filter: vtable 2ac18a0, an empty inline list of four, the macro at +0x40;
 * ended by 11ded0) and 4e54c0(suit,weapon,group,primary) puts it into its group; then the Player's active groups
 * (+0x6a88, +0x6a8c) through 4e4a70(suit,group,primary) where the suit's macro has that many (59bdb0(suit+0x60): +0x10b8,
 * +0x10bc). Slot k's suit: the equipment comes from MD (native_gate_pilot_respawn.xml Suit: the repair laser added on its
 * mount, the default weapon groups; live sx1 and sx2 showed an MD-built suit has its engine and no weapon), and the
 * group restore here runs the same calls on every client at the tick it is seated, from P_k's tables or, while P_k keeps
 * none, from slot 1's player's (simulation state, the same everywhere; empty until a player has left a suit once).
 * Left out: X4's removal of ungrouped weapons and its 81e0d0 step for class 7 weapons. Status airlock=.../weapons grouped/
 * faults. */
#define PVEHK_WEAPONS_PRIMARY 0x6a58u
#define PVEHK_WEAPONS_SECONDARY 0x6a70u
#define PVEHK_ACTIVE_PRIMARY 0x6a88u
#define PVEHK_ACTIVE_SECONDARY 0x6a8cu
#define PVEHK_DESCRIPTION_VTABLE 0x2ac18a0u
typedef struct {
    void (*install)(void *ship,void **weapon,void *description);                  /* 4d08a0: the ship's weapon of the macro, or none */
    void (*group)(void *ship,void *weapon,unsigned group,unsigned char primary);  /* 4e54c0 */
    void (*activate)(void *ship,unsigned group,unsigned char primary);            /* 4e4a70 */
    unsigned char *(*macro_data)(void *macro);                                    /* 59bdb0 */
    void (*description_end)(void *description);                                   /* 11ded0 */
} pvehk_functions;
static pvehk_functions pvehk_fn;
static uint64_t pvehk_weapons;static unsigned pvehk_equip_faults;
static void pvehk_use_image(void) {
    pvehk_fn.install=(void(*)(void*,void**,void*))(image+0x4d08a0);pvehk_fn.group=(void(*)(void*,void*,unsigned,unsigned char))(image+0x4e54c0);
    pvehk_fn.activate=(void(*)(void*,unsigned,unsigned char))(image+0x4e4a70);pvehk_fn.macro_data=(unsigned char*(*)(void*))(image+0x59bdb0);
    pvehk_fn.description_end=(void(*)(void*))(image+0x11ded0);
}
static int pvehk_table_any(const unsigned char *player,unsigned at) {
    unsigned char *begin=NULL,*end=NULL;
    return player && read_local(player+at,&begin,8) && read_local(player+at+8,&end,8) && begin && end>begin;
}
static unsigned pvehk_equip_table(unsigned char *suit,const unsigned char *player,unsigned at,unsigned char primary) {
    unsigned char *begin=NULL,*end=NULL;unsigned n=0;
    if(!read_local(player+at,&begin,8) || !read_local(player+at+8,&end,8) || !begin || end<begin || (end-begin)%24 || (end-begin)/24>32) return 0;
    const unsigned groups=(unsigned)((end-begin)/24);
    for(unsigned g=0;g<groups;g++) {
        void **m=NULL,**last=NULL;
        if(!read_local(begin+24u*g,&m,8) || !read_local(begin+24u*g+8,&last,8) || !m || last<m || last-m>16) continue;
        for(;m<last;m++) {
            void *macro=NULL,*weapon=NULL,*vtable=pnat_base+PVEHK_DESCRIPTION_VTABLE;const uint64_t four=4;
            _Alignas(16) unsigned char description[0x60];
            if(!read_local(m,&macro,8) || !macro) continue;
            memset(description,0,sizeof(description));memcpy(description,&vtable,8);memcpy(description+0x28,&four,8);memcpy(description+0x40,&macro,8);
            pvehk_fn.install(suit,&weapon,description);pvehk_fn.description_end(description);
            if(!weapon) continue;
            pvehk_fn.group(suit,weapon,g,primary);++n;
        }
    }
    return n;
}
static void pvehk_equip(unsigned k,unsigned char *suit) {
    const unsigned char *from=pslots[k].player;volatile unsigned n=0;int fault=0;
    if(!pvehk_fn.install || !suit) return;
    if(!pvehk_table_any(from,PVEHK_WEAPONS_PRIMARY) && !pvehk_table_any(from,PVEHK_WEAPONS_SECONDARY)) from=pslot_p1;   /* P_k keeps none */
    if(!from) return;
    __try {
        n+=pvehk_equip_table(suit,from,PVEHK_WEAPONS_PRIMARY,1);n+=pvehk_equip_table(suit,from,PVEHK_WEAPONS_SECONDARY,0);
        int primary=-1,secondary=-1;unsigned char *macro=NULL,*data=NULL;unsigned primaries=0,secondaries=0;
        if(read_local(from+PVEHK_ACTIVE_PRIMARY,&primary,4) && read_local(from+PVEHK_ACTIVE_SECONDARY,&secondary,4) &&
           read_local(suit+0x60,&macro,8) && macro && (data=pvehk_fn.macro_data(macro)) &&
           read_local(data+0x10b8,&primaries,4) && read_local(data+0x10bc,&secondaries,4)) {
            if(primary>=0 && (unsigned)primary<primaries) pvehk_fn.activate(suit,(unsigned)primary,1);
            if(secondary>=0 && (unsigned)secondary<secondaries) pvehk_fn.activate(suit,(unsigned)secondary,0);
        }
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    pvehk_weapons+=n;if(fault) ++pvehk_equip_faults;
}
static uint64_t pvehk_outs,pvehk_homes,pvehk_far;
static int pvehk_suited(unsigned k) {
    return pdeath_armed && k>=2 && k<=pilot_slot_count && pdeath_slots[k-1].out==2 && !pilot_slots[k-1].dead;
}
static int pvehk_may(unsigned k);static int pvehk_ask(unsigned k,uint64_t tick);
/* Slot k's replayed action key (lockstep_pilot_stock.h) or dock request (lockstep_ui_replay.h): 1 = it is this module's
 * (the caller leaves X4's handler out). In its suit GET_UP is void and the dock keys are X4's (0: the caller runs its
 * handler in the slot's scope; the tick is noted). Walking in its own
 * ship the dock keys (DOCK_ACTION 0xaf, UNDOCK 0xd3) are the way out: the airlock without its console. On client k the
 * on-foot click does not reach X4's pick yet (the user's session of 11 Nov 20:25: client 2's clicks arrived as the
 * ship's TARGETMOUSE_SELECT 0x82), and a walking slot's dock key has no business with X4's dock handler anyway (nobody
 * is at its ship's controls). GET_UP while walking stays the sit-down (lockstep_pilot_foot_slots.h). */
static int pvehk_home_key(unsigned k,unsigned id) {
    if(id!=PFOOT_GET_UP && id!=0xafu && id!=0xd3u) return 0;
    if(pvehk_suited(k)) {
        if(id==PFOOT_GET_UP) return 1;
        pdeath_slots[k-1].dock_tick=stats.issued;++pvehk_far;return 0;
    }
    if(id!=PFOOT_GET_UP && pdeath_armed && k>=2 && k<=pilot_slot_count && pslots[k].seated && pslots[k].walking) {
        (void)pvehk_ask(k,stats.issued);return 1;   /* out through the airlock when it may (pvehk_may); consumed either way */
    }
    return 0;
}
/* gate.pilot_death_home(slot): the ship a slot in its suit left; gate.pilot_death_range(slot, meters): the probe's
 * distance between the suit and that ship (negative: none, another sector). */
static uint64_t pvehk_home(unsigned k) {return pvehk_suited(k)?pdeath_slots[k-1].home:0;}
static int pvehk_range(unsigned k,double meters) {
    if(!pvehk_suited(k) || !isfinite(meters)) return 0;
    pdeath_slots[k-1].ranged=meters>=0;pdeath_slots[k-1].range=meters;return 1;
}
/* Slot k may leave now: alive, walking in its ship, not on its way out already. */
static int pvehk_may(unsigned k) {
    if(!pdeath_armed || !pdeath_suit || k<2 || k>pilot_slot_count) return 0;
    return !pilot_slots[k-1].dead && !pdeath_slots[k-1].lost && !pdeath_slots[k-1].out && pslots[k].seated && pslots[k].walking;
}
/* The apply of slot k's airlock action. */
static int pvehk_ask(unsigned k,uint64_t tick) {
    if(!pvehk_may(k)) return 0;
    pdeath_slot *d=&pdeath_slots[k-1];
    d->out=1;d->suit=0;d->suit_tick=tick+PDEATH_SUIT_LAG;pdeath_event("out",k);
    return 1;
}
/* Slot k, alive and off any seat, into the ship `runtime` (its suit, its home), as the respawn's seat. */
static int pvehk_seat(unsigned k,uint64_t runtime,const char *event) {
    pilot_slot *s=&pilot_slots[k-1];
    if(pfire_armed) {(void)pfire_end(s,&pfire_slots[k-1],k);memset(&pfire_slots[k-1],0,sizeof(pfire_slots[k-1]));}
    s->runtime=runtime;s->reversed=1;s->renewed=1;s->dead=0;s->seen=0;
    if(!pilot_resolve(s,pdeath_find,pdeath_map,pdeath_player)) return pdeath_fail("vehicle_unresolved");
    pstk_slot_frames[k-1].flight=0;pstk_slot_frames[k-1].flight_known=1;
    if(!pslot_seat(k)) {pdeath_why=pslot_why;return 0;}
    pfbody_place_seat(k);   /* a suit has no room for a body: P_k gets its place for MD here */
    /* The local controls stayed active through the change: the edge the new flight control needs. */
    if(pstk_capturing && pstk_capture_slot==k && pstk_local_active==1) pstk_record_event(PSTK_SPACE,8,1u);
    pdeath_event(event,k);
    return 1;
}
static int pvehk_pass(unsigned k,uint64_t tick) {
    pslot_slot *x=&pslots[k];pilot_slot *s=&pilot_slots[k-1];pdeath_slot *d=&pdeath_slots[k-1];
    if(s->dead || d->lost) {   /* its ship went first: the loss's or the death's way, no suit of this kind */
        if(d->lost && d->out==1) {d->suit_tick=0;d->suit=0;}
        d->out=0;d->home=0;d->ask=d->ranged=0;d->dock_tick=0;d->arrive=0;d->arrive_room=NULL;return 1;
    }
    if(d->out==1) {
        if(!d->suit_tick || tick<d->suit_tick) return 1;
        unsigned char *suit=d->suit?pdeath_find(pdeath_map,d->suit,4):NULL;
        d->suit_tick=0;
        if(!suit || !x->seated || !x->walking) {d->out=0;d->suit=0;pdeath_event("noout",k);return 1;}
        const uint64_t home=x->runtime;
        pfslot_lose(k);                                                 /* the walk ends where it is */
        if(!pfbody_release_slot(k)) {pdeath_why=pfbody_why;return 0;}   /* none while it walks (parked); for the order */
        if(!pslot_release(k)) {pdeath_why=pslot_why;return 0;}          /* the seat's pointers back */
        pvehk_equip(k,suit);                                            /* its weapon groups, as X4's eject restores them */
        if(!pvehk_seat(k,d->suit,"suited")) return 0;
        d->home=home;d->out=2;d->ask=d->ranged=0;d->dock_tick=0;d->arrive=0;d->arrive_room=NULL;++pvehk_outs;
        return 1;
    }
    /* In its suit: home once X4 asked to put its player into the ship it left (pvehk_enter), or docked the suit there
     * (the suit under that ship). */
    unsigned char *home=d->home?pdeath_find(pdeath_map,d->home,4):NULL,*parent=NULL,*suit=x->ship;void **vtable=NULL;double now=0;
    int arrived=0;
    if(d->arrive) {
        unsigned char *room=d->arrive_room;
        d->arrive=0;d->arrive_room=NULL;
        arrived=home && (!room || uics_under(room,home));
        if(!arrived) InterlockedIncrement64(&pvehk_enters_else);
    }
    if(!home || !x->seated || !suit || !read_local(suit+0x70,&parent,8) || !(arrived || uics_under(parent,home))) return 1;
    /* The body off the suit first. A spacesuit HAS a cockpit room, and once everyone sits the slot's body is connected
     * there (lockstep_pilot_foot_body.h, PFBODY_SETTLE ticks after the seat). The suit's kill below then walked into P_k:
     * X4 killed the slot's player with its suit and never came out of the teardown (live 12 Nov, both clients spinning in
     * one tick after the home key: SpaceSuit and its Cockpit dead, P_2 dying with no parent, an MD action at the root).
     * Earlier runs went home before the body had settled. */
    if(!pfbody_release_slot(k)) {pdeath_why=pfbody_why;return 0;}
    if(!pslot_release(k)) {pdeath_why=pslot_why;return 0;}
    if(pnat_fn.time(&now) && read_local(suit,&vtable,8) && vtable) ((hitn_kill_fn)vtable[PDEATH_KILL_SLOT])(suit,PDEATH_KILL_REMOVED,NULL,now);
    if(!pvehk_seat(k,d->home,"home")) return 0;
    d->out=0;d->home=0;d->suit=0;d->ask=d->ranged=0;d->dock_tick=0;d->arrive=0;d->arrive_room=NULL;++pvehk_homes;
    return 1;
}
/* X4 took slot k's player out of its suit into the ship it left (header, the way back): after a dock key, P_k alive, its
 * parent chain at that ship and not at the suit. Before the pass asks whether the suit died with the slot aboard: the
 * suit X4 removes after the take is no death. 1 handled (home), 0 not this, -1 failed. */
static uint64_t pvehk_taken_in;
static int pvehk_taken(unsigned k,uint64_t tick) {
    pslot_slot *x=&pslots[k];pilot_slot *s=&pilot_slots[k-1];pdeath_slot *d=&pdeath_slots[k-1];
    unsigned char *inside=NULL,*suit=x->ship,life=1,lifecycle=1;void **vtable=NULL;double now=0;
    if(d->out!=2 || s->dead || d->lost || !d->dock_tick || tick>d->dock_tick+PVEHK_DOCK_WINDOW || !x->seated || !suit || !x->player) return 0;
    unsigned char *home=d->home?pdeath_find(pdeath_map,d->home,4):NULL;
    if(!home || home==suit || !read_local(x->player+PDEATH_LIFECYCLE,&life,1) || life) return 0;
    if(!read_local(x->player+0x70,&inside,8) || !inside || uics_under(inside,suit) || !uics_under(inside,home)) return 0;
    if(!pfbody_release_slot(k)) {pdeath_why=pfbody_why;return -1;}
    if(read_local(suit+PDEATH_LIFECYCLE,&lifecycle,1) && !lifecycle && pslot_controller_has(suit,0,x->controller)==1) {
        if(!pslot_release(k)) {pdeath_why=pslot_why;return -1;}
        if(pnat_fn.time(&now) && read_local(suit,&vtable,8) && vtable) ((hitn_kill_fn)vtable[PDEATH_KILL_SLOT])(suit,PDEATH_KILL_REMOVED,NULL,now);
    } else pslot_retire(k);
    if(!pvehk_seat(k,d->home,"home")) return -1;
    d->out=0;d->home=0;d->suit=0;d->ask=d->ranged=0;d->dock_tick=0;++pvehk_homes;++pvehk_taken_in;
    return 1;
}
/* First thing in a pass, before the pieces that rely on the slot ships. */
static int pdeath_pass(uint64_t tick,unsigned char *controller) {
    uint64_t take=pilot_start-PILOT_SUPPRESS_LEAD;
    if(pdeath_failed) return pdeath_fail(pdeath_why);
    if(!pdeath_guard_installed && !pdeath_guard_install()) return pdeath_fail("guard_install");
    if(memcmp(pnat_base+PDEATH_GAME_OVER_CALL,pdeath_guard_patched,5)) return pdeath_fail("guard_lost");
    if(tick<=take+1 || tick>pilot_stop) return 1;
    for(unsigned k=1;k<=pilot_slot_count;k++) {
        if(!(pstk_slot_mask>>(k-1)&1u)) continue;
        /* Absent (hot join): its ship is nobody's until it is seated (enrolled at H, seated at H-1); a loss before stops at
         * the seat (161 absent_ship_lost). */
        if(!pilot_slot_enrolled(k,tick) || (pilot_slot_absent(k) && !pslots[k].seated && !pilot_slots[k-1].dead)) continue;
        pilot_slot *s=&pilot_slots[k-1];pdeath_slot *d=&pdeath_slots[k-1];
        if(k==1 && pveh_left(s,d,tick,controller)) continue;   /* out of its spacesuit by a teleport into a ship: no death */
        if(k==1 && d->lost && !s->dead && !controller && pveh_aboard_until && tick<=pveh_aboard_until) (void)pveh_aboard(s,d);   /* ...and on foot in a ship by now */
        if(k>=2 && d->out==2 && !s->dead && !d->lost) pvehk_dock_poll(k);   /* X4's entry check, where its own player's byte keeps it off */
        if(k>=2 && d->out==2) {const int taken=pvehk_taken(k,tick);if(taken<0) return 0;if(taken) continue;}   /* X4 took it home */
        if(!s->dead && !d->lost && pdeath_dying(s)) {   /* who is aboard decides (header) */
            if(pdeath_off_seat(k,controller)?!pdeath_lose(k,tick):!pdeath_die(k,tick)) return 0;
        }
        if(k>=2 && d->out && !pvehk_pass(k,tick)) return 0;   /* through the airlock: into its suit, home again */
        if(!s->dead) continue;
        if(pdeath_suit && k>=2 && !pdeath_suit_pass(k,tick)) return 0;
        if(!pdeath_delay) continue;
        if(d->state==PDEATH_DEAD && tick>=d->create_tick && d->seat_tick<pilot_stop) {d->state=PDEATH_CREATING;pdeath_event("create",k);}
        if(d->state==PDEATH_CREATING && tick>=d->seat_tick && !pdeath_seat(k,controller)) return 0;
    }
    pdeath_stage=pdeath_dead?"dead":"live";
    return 1;
}
/* Arming (Lua, before the first tick, after gate.pilot_stock). delay 0: a dead slot stays dead. */
static int pdeath_arm_using(unsigned delay,int suit) {
    unsigned char *options=NULL;
    if(pdeath_armed) return pdeath_delay==delay && pdeath_suit==suit?1:pstk_arm_refuse("death_rerun");
    if(!pnat_armed || pnat_mode!=PNAT_MODE_STOCK) return pstk_arm_refuse("death_without_stock");
    if(delay>PILOT_TICK_MAX) return pstk_arm_refuse("death_delay");
    if(!read_local(pnat_base+PNAT_OPTIONS,&options,8) || !options) return pstk_arm_refuse("death_options");
    if(!pnat_fn.option(options+0x118,"emergencyeject",1)) return pstk_arm_refuse("death_eject_off");
    memset(pdeath_slots,0,sizeof(pdeath_slots));memset(pdeath_held,0,sizeof(pdeath_held));
    pdeath_delay=delay;pdeath_suit=suit;pdeath_dead=0;pdeath_failed=0;pdeath_stage="armed";pdeath_why="-";pdeath_event_length=0;pdeath_events[0]=0;
    pdeath_guard_installed=0;pdeath_game_overs=0;pdeath_released=pdeath_retired=0;pvehk_outs=pvehk_homes=pvehk_far=0;pvehk_taken_in=0;pvehk_weapons=0;pvehk_equip_faults=0;
    pdeath_armed=1;return 1;
}
static int pdeath_arm(unsigned delay,int suit) {
    if(!pdeath_armed) {
        if(stats.issued) return pstk_arm_refuse("death_after_first_tick");
        if(!pdeath_pins()) {snprintf(pstk_arm_why,sizeof(pstk_arm_why),"pins_death:%x",pin_failed_rva);return 0;}
        pvehk_use_image();
    }
    return pdeath_arm_using(delay,suit);
}
/* gate.pilot_death_frame: what changed since the last call, "die:1,create:2,live:1". */
static const char *pdeath_take_events(void) {
    if(!pdeath_event_length) return NULL;
    pdeath_event_length=0;return pdeath_events;
}
/* gate.pilot_death_suit: the MD answer; only for a slot waiting for one. */
static int pdeath_suit_ship(unsigned k,uint64_t runtime) {
    if(!pdeath_armed || !pdeath_suit || k<2 || k>pilot_slot_count || !runtime || !pdeath_slots[k-1].suit_tick || pdeath_slots[k-1].suit) return 0;
    pdeath_slots[k-1].suit=runtime;return 1;
}
/* gate.pilot_death_ship: the MD answer; only for a slot waiting for one. */
static int pdeath_ship(unsigned k,uint64_t runtime) {
    if(!pdeath_armed || k<1 || k>pilot_slot_count || !runtime || pdeath_slots[k-1].state!=PDEATH_CREATING || pdeath_slots[k-1].ship) return 0;
    pdeath_slots[k-1].ship=runtime;return 1;
}
static int pdeath_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," death=%s death_why=%s dead=%x delay=%u suit=%d guard=%d game_overs=%lld seats=%u/%u",pdeath_stage,pdeath_why,pdeath_dead,pdeath_delay,
        pdeath_suit,pdeath_guard_installed,(long long)pdeath_game_overs,pdeath_released,pdeath_retired);
    unsigned losses=0;
    for(unsigned k=1;n>0 && (size_t)n<capacity && k<=pilot_slot_count;k++) {
        const pdeath_slot *d=&pdeath_slots[k-1];
        int more=snprintf(out+n,capacity-(size_t)n," d%u=%u/%u/%u/%llu%s%s%s",k,d->state,d->deaths,d->respawns,(unsigned long long)d->dead_tick,
            d->in_suit?"/suit":"",d->lost?"/lost":"",d->out==1?"/out1":d->out==2?"/out2":"");
        if(more<0) return 0;
        n+=more;losses+=d->losses;
    }
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," losses=%u",losses);   /* ships lost while off the seat */
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," taken=%llu enter=%d/%lld/%lld",(unsigned long long)pvehk_taken_in,
                                              pvehk_enter_installed,(long long)pvehk_enters,(long long)pvehk_enters_else);
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," fade=%d/%lld/%lld",pvehk_fade_installed,
                                              (long long)pvehk_fades_shown,(long long)pvehk_fades_kept);
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," poll=%d/%lld/%lld",pvehk_poll_on,(long long)pvehk_polls,
                                              (long long)pvehk_poll_entered);
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," zone=%d/%lld/%lld",pvehk_zone_installed,
                                              (long long)pvehk_zone_own,(long long)pvehk_zone_none);
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," vehicle=%llu/%llu/%d/%llu",(unsigned long long)pveh_adopts,
                                              (unsigned long long)pveh_lefts,pveh_in_suit,   /* slot 1: adopted vehicles, suits left, in a suit, */
                                              (unsigned long long)pveh_aboards);              /* on foot into a ship */
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," airlock=%llu/%llu/%llu/%llu/%u",(unsigned long long)pvehk_outs,
                                              (unsigned long long)pvehk_homes,(unsigned long long)pvehk_far,(unsigned long long)pvehk_weapons,
                                              pvehk_equip_faults);   /* slots 2..N: into a suit, home again, asked too far away; suit weapons installed, faults */
    return n>0 && (size_t)n<capacity?n:0;
}

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
static int pfslot_ship_docked(unsigned char *ship);   /* lockstep_pilot_foot_slots.h: docked, or stored in a container */
static int pfslot_armed;
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
 * suit released, the suit killed as X4 kills its player's old suit, the slot seated in the ship it left). Another ship than the one it
 * left (X4 asks it for a ship the suit may enter: one the player faction owns, such as a ship just claimed with the
 * suit's repair laser; the user's chain of 24 Nov: shoot a friendly until its pilot bails, out in the suit, the claim,
 * aboard, fly it and dock): the same way, the slot seated in that ship (pvehk_aboard). Stock X4 stands its own player on
 * foot there (0x7154b0) and the player walks to the pilot's seat; a slot sits down at once, as it does at home. Another
 * slot's ship stays no ship to go into (two players at one ship's controls): counted, the slot stays in its suit. Every
 * other player passes to X4. Installed with the game over guard (after the UI replay's pins of 0x730ff0 were checked at
 * the arming); pinned, else not installed and X4's own runs. Status enter=installed/noted/elsewhere/aboard. */
#define PVEHK_ENTER 0x710390u
#define PVEHK_ENTER_SIZE 0xf7u
#define PVEHK_ENTER_SHA "09b162991c875b59a604b1f4265d485a37d4eb8783ee1caf469cab801b731843"
#define PVEHK_ENTER_SITE_KEY 0x7313e6u
#define PVEHK_ENTER_SITE_FRAME 0x7315d5u
#define PVEHK_ENTER_STUB 0x100u                 /* on the slots' near page, after the lookup's (0xe0) */
typedef void (*pvehk_enter_fn)(void *player,void *suit,void *room,void *fourth,unsigned char fifth);
static int pvehk_enter_installed;
static unsigned char pvehk_enter_patched[2][5];
/* Since 24 Nov (crash rk9-suit, lockstep_pilot_teleport.h: the board): where the board's way is live, X4's entry DOES run for
 * a suited slot's player, in slot k's scope (the one it is called in, or opened here; another slot's open: not now, X4 asks
 * again next frame): X4's fade (on client k only, the scope's slot), its TeleportTo, whose event the teleport module's entry
 * takes; X4's handler never reaches P_k. The ship it left gone: not run (X4 asks again; counted board_no_home). Status
 * enter=installed/noted/elsewhere/x4/no_home/unscoped/aboard/others. */
static int ptele_board_on(void);static int ptele_board(unsigned k);   /* lockstep_pilot_teleport.h */
static unsigned char *pveh_ship_from(unsigned char *c);static int pvehk_others_ship(unsigned k,unsigned char *ship);   /* below */
static volatile LONG64 pvehk_enters,pvehk_enters_else,pvehk_enters_x4,pvehk_board_no_home,pvehk_enters_unscoped,pvehk_aboards,pvehk_board_others;
static pvehk_enter_fn pvehk_enter_original;   /* tests: in place of 0x710390 */
/* The ship slot k left through its airlock, alive (lockstep_pilot_teleport.h, the board: its seat). */
static unsigned char *pvehk_home_ship(unsigned k) {
    const pdeath_slot *d=k>=2 && k<=PILOT_SLOTS?&pdeath_slots[k-1]:NULL;unsigned char *home=NULL,life=1;
    if(!d || !d->home || !pdeath_find || !(home=pdeath_find(pdeath_map,d->home,4)) || !read_local(home+PDEATH_LIFECYCLE,&life,1) || life) return NULL;
    return home;
}
/* Where X4's entry puts the player when it names no room (0x710390 at 0x7103e7..0x710473, its call from the player's
 * update once the suit has its dock, 0x731560): the suit's connection 14 (the connections at suit+0xc0, found by
 * 0x4a76a0 with X4's own flags byte and filter), that connection's component (+0x20, +0x18) and, from that component's
 * parent on, its first ancestor of class 0x6f. Asked at X4's call, when the connection is the one X4 reads. 0x4a76a0
 * pinned (pvehk_find, set at the install), else none: the arrival is the ship it left's, as before. */
#define PVEHK_CONNECTIONS 0xc0u
#define PVEHK_CONNECTION_SLOT 0xeu
#define PVEHK_CONNECTION_FIND 0x4a76a0u
#define PVEHK_CONNECTION_FIND_SIZE 295u
#define PVEHK_CONNECTION_FIND_SHA "4d01bfc7c99eaaccf9be672ddaad51a275a940258a37c6cb20033d98910357fa"
#define PVEHK_CONNECTION_FLAGS 0x391be0eu   /* the byte X4 passes */
#define PVEHK_CONNECTION_FILTER 0x31bc518u  /* the filter object X4 passes */
#define PVEHK_ENTRY_CLASS 0x6f
typedef void *(*pvehk_find_fn)(uint64_t *out,void *connections,unsigned slot,unsigned char flags,void *filter);
static pvehk_find_fn pvehk_find;            /* 0x4a76a0 once pinned; tests set their own */
static unsigned char *pvehk_entry_target(unsigned char *suit) {
    uint64_t out[8]={0};unsigned char *connections=NULL,*holder=NULL,*c=NULL,flags=0;int fault=0;
    if(!pvehk_find || !suit || !read_local(suit+PVEHK_CONNECTIONS,&connections,8) || !connections ||
       !read_local(pnat_base+PVEHK_CONNECTION_FLAGS,&flags,1)) return NULL;
    __try {pvehk_find(out,connections,PVEHK_CONNECTION_SLOT,flags,pnat_base+PVEHK_CONNECTION_FILTER);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    if(fault || !out[0] || !out[1] || !read_local((unsigned char *)(uintptr_t)out[1]+0x20,&holder,8) || !holder ||
       !read_local(holder+0x18,&c,8) || !c || !read_local(c+0x70,&c,8)) return NULL;
    for(unsigned depth=0;c && depth<32;depth++) {
        void **vtable=NULL;unsigned char is=0;
        if(!read_local(c,&vtable,8) || !vtable) return NULL;
        __try {is=((unsigned char(*)(void*,int))vtable[0x11e8u/8u])(c,PVEHK_ENTRY_CLASS);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return NULL;}
        if(is) return c;
        if(!read_local(c+0x70,&c,8)) return NULL;
    }
    return NULL;
}
static void pvehk_enter(void *player,void *suit,void *room,void *fourth,unsigned char fifth) {
    const pvehk_enter_fn original=pvehk_enter_original?pvehk_enter_original:(pvehk_enter_fn)(pnat_base+PVEHK_ENTER);
    for(unsigned k=2;player && k<=PILOT_SLOTS;k++) {
        if(pslots[k].player!=(unsigned char *)player) continue;
        pdeath_slot *d=&pdeath_slots[k-1];
        InterlockedIncrement64(&pvehk_enters);
        if(!pdeath_armed || d->out!=2 || pilot_slots[k-1].dead) return;   /* never X4's move for a slot's player otherwise */
        if(ptele_board_on()) {
            if(!pvehk_home_ship(k)) {InterlockedIncrement64(&pvehk_board_no_home);return;}
            if(pvehk_others_ship(k,pveh_ship_from(room?(unsigned char *)room:pvehk_entry_target((unsigned char *)suit)))) {
                InterlockedIncrement64(&pvehk_board_others);return;   /* another player's ship: no way in (counted), the suit stays */
            }
            const int own=pstk_scope_slot==k;
            if(!own && (pstk_scope_slot || !pslot_enter(k))) {InterlockedIncrement64(&pvehk_enters_unscoped);return;}
            int fault=0;
            __try {original(player,suit,room,fourth,fifth);}
            __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
            if(!own && !pslot_leave(k)) (void)pdeath_fail(pslot_leave_named("enter_leave"));
            if(fault) (void)pdeath_fail("enter_fault");
            InterlockedIncrement64(&pvehk_enters_x4);
            return;
        }
        d->arrive_room=room?(unsigned char *)room:pvehk_entry_target((unsigned char *)suit);d->arrive=1;   /* the old way (below) */
        return;
    }
    original(player,suit,room,fourth,fifth);
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
/* A third caller, U::Player's update (0x7113e0 at 0x713c3f): the fade of X4's move of a player on foot out of a place no
 * character may stand (a connection tagged trigger_invalid_character_position) to its ship's bridge transporter. It runs
 * in the player's Move: outside the scopes for slot 1, in slot k's scope for slot k (lockstep_pilot_teleport.h, which
 * pins the call and sets ptele_fade_site). */
#define PVEHK_FADE_RET_TELEPORT 0x713c44u
static int ptele_fade_site;
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
    const int site=pvehk_fade_site_test>=0?pvehk_fade_site_test:
        (ret==PVEHK_FADE_RET_EJECT || ret==PVEHK_FADE_RET_ENTER || (ptele_fade_site && ret==PVEHK_FADE_RET_TELEPORT));
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
/* ---- the menu X4 opens when it seats a player ----
 * X4's seating of a player (0x716670, and its helper 0x70dc50, and the player's own vfunc at 0x705190 for a docked
 * ship) ends by opening a menu by name through 0x8c82f0(std::string*): "DockedMenu" for a docked ship, "TopLevelMenu"
 * in flight, others for other posts. The seating is simulation and runs on every client; the menu is this process's
 * UI. So when player 1's ship had docked and X4 seated it there, the dock menu opened on EVERY client, on another
 * player's screen as that player's own "Ship Interactions" menu, where it held the flight controls and, with X4's
 * "stop in menus", the ship: from that tick player 2 could neither fly by hand nor by autopilot until somebody closed
 * a menu they had never asked for (two scripted clients, 16 Nov: a ship that did not answer a thrust key for 1000
 * ticks, the menu on its window's picture). As with the screen fade above: the routine's entry is hooked and the
 * callers are told apart by the return address; from the seating's sites the menu opens on the client whose own slot
 * the seating runs for (outside any scope: slot 1; inside slot k's scope: slot k) and nowhere else; every other
 * caller (a person's key, the UI's own) passes. The menu system is no part of the simulation. Pinned; else X4's own.
 * Status menu=installed/shown/left out. */
#define PVEHK_MENU 0x8c82f0u
#define PVEHK_MENU_SIZE 0x25bu
#define PVEHK_MENU_SHA "7e901a2b3eebe35935bc5f456df7841eb55647ac1656b89ccc70f6a5601610f5"
#define PVEHK_MENU_STUB 0x940u                 /* on the slots' near page, after the controller's end (0x900, 0x920) */
#define PVEHK_MENU_TRAMPOLINE 0x960u           /* the routine's first instruction, then on at +5 */
static const unsigned char pvehk_menu_head[5]={0x48,0x89,0x5c,0x24,0x08};   /* mov [rsp+8],rbx */
typedef void (*pvehk_menu_fn)(void *name);
static int pvehk_menu_installed,pvehk_menu_site_test=-1;   /* tests: 1 the call is from a seating site, 0 it is not */
static unsigned char pvehk_menu_patched[5];
static volatile LONG64 pvehk_menus_shown,pvehk_menus_kept;
static pvehk_menu_fn pvehk_menu_original;   /* tests: in place of the trampoline */
__attribute__((noinline)) static void pvehk_menu(void *name) {
    const uintptr_t ret=(uintptr_t)__builtin_return_address(0)-(uintptr_t)pnat_base;
    const int site=pvehk_menu_site_test>=0?pvehk_menu_site_test:
        ((ret>=0x70dc50u && ret<0x70e0e1u) || (ret>=0x716670u && ret<0x716d7bu) || ret==0x705292u);
    pvehk_menu_fn original=pvehk_menu_original?pvehk_menu_original:(pvehk_menu_fn)(void*)(pslot_page+PVEHK_MENU_TRAMPOLINE);
    if(site) {
        const unsigned running=pstk_scope_slot?pstk_scope_slot:1u,own=pstk_capture_slot?pstk_capture_slot:1u;
        if(running!=own) {InterlockedIncrement64(&pvehk_menus_kept);return;}
        InterlockedIncrement64(&pvehk_menus_shown);
    }
    original(name);
}
static void pvehk_menu_install(void) {
    DWORD old,ignored;int32_t rel=0;
    if(pvehk_menu_installed || !pslot_page || !hash_ok(pnat_base+PVEHK_MENU,PVEHK_MENU_SIZE,PVEHK_MENU_SHA) ||
       memcmp(pnat_base+PVEHK_MENU,pvehk_menu_head,sizeof(pvehk_menu_head))) return;
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return;
    pview_abs_jump(pslot_page+PVEHK_MENU_STUB,(void*)pvehk_menu);
    memcpy(pslot_page+PVEHK_MENU_TRAMPOLINE,pvehk_menu_head,sizeof(pvehk_menu_head));
    pview_abs_jump(pslot_page+PVEHK_MENU_TRAMPOLINE+sizeof(pvehk_menu_head),pnat_base+PVEHK_MENU+sizeof(pvehk_menu_head));
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return;
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    rel=(int32_t)((intptr_t)(pslot_page+PVEHK_MENU_STUB)-(intptr_t)(pnat_base+PVEHK_MENU+5));
    pvehk_menu_patched[0]=0xe9;memcpy(pvehk_menu_patched+1,&rel,4);
    if(pview_cas(PVEHK_MENU,pvehk_menu_head,pvehk_menu_patched,5)) pvehk_menu_installed=1;
}
static void pvehk_menu_restore(void) {
    if(pvehk_menu_installed && pview_cas(PVEHK_MENU,pvehk_menu_patched,pvehk_menu_head,5)) pvehk_menu_installed=0;
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
/* ---- MD's object and room events for a slot's player ----
 * When X4 moves its own player into another ship or room, the player's parent change (Player vfunc 0x1380 -> 0x553660)
 * posts U::ChangedObjectEvent and U::ChangedRoomEvent on the player (0x554177, 0x55420c: 0x28 bytes, the old ID at
 * +0x18, the new one at +0x20): MD's event_object_changed_object / event_object_changed_room. A slot's player goes
 * into its suit and home through the seat's pointers (pvehk_seat), never through that parent change, so MD heard
 * nothing: a slot's own story stood still at the step that waits for its player in a spacesuit (the cadet story's
 * repair, a playtest of 15 Nov: the satellite repaired, the mission's success logged, the chapter never closed
 * because its dialogue starts from event_object_changed_room on player.entity). After the seat the pass posts the
 * same two events on P_k, old and new ship, old and new cockpit room, through X4's deferred post (dispatched in
 * X4's own event phase, where a claimed cue's frames run in their owner's scope): every client, the same tick.
 * Pinned (the builder's part of 0x553660, the allocator and the post as the cutscene module pins them); else none.
 * Status events=on/posted.
 * The audit of 15 Nov (the mission fixture's listeners, the same scripted actions as slot 1 and as slot 2) named three
 * more that only X4's own player got: U::ControlStartedEvent / U::ControlStoppedEvent (0x70dc50 at 0x70e062, 0x70e0f0
 * at 0x70e504: 0x28 bytes, the controlled object's ID at +0x18, its class from vtable +0x11d0 at +0x20; MD's
 * event_player_started_control / stopped_control, 53 uses in the stock scripts) and U::TeleportSuccessfulEvent
 * (0x7052f0 at 0x705918: 0x30 bytes, the room left at +0x18, the room reached at +0x20, a flag byte at +0x28;
 * event_player_teleport_successful, 36 uses). X4's own player hears: stand-up = stopped(ship); into the suit =
 * teleport(suit's room) then started(suit); back = teleport(ship's room) then stopped(suit). A slot's player gets the
 * same at the gate's own steps (the stand-up and sit-down of lockstep_pilot_foot_slots.h, the suit, the way home),
 * and started(ship) at its arrival, which is the pilot seat here.
 * These three are MD's event_player_* kind: the listening cue is on the slot's own player object (read from the
 * listener tables, 15 Nov), but a first form that posted them deferred, as the object and room events are, was never
 * heard (twelve posts, no row): the script side takes them only while their source is the universe's player, and at
 * X4's event phase that is slot 1's. So they are posted with immediate dispatch inside slot k's scope (the scope is
 * opened for the post where the pass has none), as X4's own posts of that kind reach a slot's missions (travel
 * mode, activity: posted while the slot's input is replayed in its scope). Status events=on/posted/more/unscoped. */
#define PVEHK_EVENT_OBJECT 0x2ba9820u          /* U::ChangedObjectEvent's vtable */
#define PVEHK_EVENT_ROOM 0x2ba9938u            /* U::ChangedRoomEvent's */
#define PVEHK_EVENT_SIZE 0x28u
#define PVEHK_EVENT_TELEPORT 0x2ba9858u        /* U::TeleportSuccessfulEvent's */
#define PVEHK_EVENT_TELEPORT_SIZE 0x30u
#define PVEHK_EVENT_CONTROL_ON 0x2ba9040u      /* U::ControlStartedEvent's */
#define PVEHK_EVENT_CONTROL_OFF 0x2ba9008u     /* U::ControlStoppedEvent's */
#define PVEHK_EVENT_CLASS_SLOT (0x11d0u/8u)    /* a component's class, as the control events carry it */
static const struct {unsigned rva,size;const char *sha;} pvehk_event_builders[]={
    {0x70e033u,0x76u,"a14a1d34dc1275aed0164ad17a270f8e56dd2070ebaebed4b75a130d9c845ed6"},   /* control started */
    {0x70e4d5u,0x76u,"23b6a4808c652b33071e79b329e685aa4b2deac846ae2da4592fc07a77c01e3e"},   /* control stopped */
    {0x7058e5u,0x59u,"291a315822eafec3fadbdf7260f766bca1244f2685bc5db797892c176d6b5fcf"},   /* teleport successful */
    {0x705a64u,0x11u,"643b0d3d441e87bb62301aa2a2da4b06ceb7aafd3388ef24713bcc54635b850a"}};  /* ...its post */
static int pvehk_events_more;                  /* the three above: their builders hold */
#define PVEHK_EVENT_POST_NOW 0x99a3f0u         /* EventSource::PostEvent(source, event, time, immediate) */
#define PVEHK_EVENT_POST_NOW_SIZE 807u
#define PVEHK_EVENT_POST_NOW_SHA "5ca12a5d17041238645af6d6028c718bb3714cc53e9dd6963769d51827087c0d"
static volatile LONG64 pvehk_events_unscoped;
static int pslot_enter(unsigned k);static int pslot_leave(unsigned k);
#define PVEHK_EVENT_BUILD 0x55411du            /* 0x553660: both events built and posted */
#define PVEHK_EVENT_BUILD_SIZE 0x11cu
#define PVEHK_EVENT_BUILD_SHA "65487259c1e9d0a804b3ac7d3bac2c09ab3c8f838dcb07c1bc5c33ba76335322"
#define PVEHK_EVENT_ALLOCATE 0x14b7140u        /* SMem::Allocate(size, -, -, -, alignment) */
#define PVEHK_EVENT_POST 0x99a720u             /* EventSource::PostEventDeferred(source, event, time) */
#define PVEHK_EVENT_POST_SIZE 0xedu
#define PVEHK_EVENT_POST_SHA "58b80a15cd07746aeb8e15c6b91230adeb30b3355fe80f58dc1b93b5c72925c3"
static int pvehk_events_on;
static volatile LONG64 pvehk_events_posted;
static void (*pvehk_event_test)(void *player,unsigned vtable,uint64_t was,uint64_t is);   /* tests: in place of the post */
static void pvehk_events_pins(void) {
    pvehk_events_on=pnat_base && hash_ok(pnat_base+PVEHK_EVENT_BUILD,PVEHK_EVENT_BUILD_SIZE,PVEHK_EVENT_BUILD_SHA) &&
        hash_ok(pnat_base+PVEHK_EVENT_POST,PVEHK_EVENT_POST_SIZE,PVEHK_EVENT_POST_SHA);
    pvehk_events_more=pvehk_events_on && hash_ok(pnat_base+PVEHK_EVENT_POST_NOW,PVEHK_EVENT_POST_NOW_SIZE,PVEHK_EVENT_POST_NOW_SHA);
    for(unsigned i=0;pvehk_events_more && i<sizeof(pvehk_event_builders)/sizeof(*pvehk_event_builders);i++)
        pvehk_events_more=hash_ok(pnat_base+pvehk_event_builders[i].rva,pvehk_event_builders[i].size,pvehk_event_builders[i].sha);
}
/* One event of `size` bytes with the two words at +0x18 and +0x20 (what follows stays zero), posted on the player. */
static void pvehk_event_raw(unsigned char *player,unsigned vtable,unsigned size,uint64_t first,uint64_t second,double now) {
    InterlockedIncrement64(&pvehk_events_posted);
    if(pvehk_event_test) {pvehk_event_test(player,vtable,first,second);return;}
    __try {
        unsigned char *event=((void*(*)(size_t,uintptr_t,uintptr_t,uintptr_t,unsigned))(void*)(pnat_base+PVEHK_EVENT_ALLOCATE))(size,0,0,0,0x10);
        if(event) {
            void *vt=pnat_base+vtable;
            memset(event,0,size);memcpy(event,&vt,8);memcpy(event+0x18,&first,8);memcpy(event+0x20,&second,8);
            ((void(*)(void*,void*,double))(void*)(pnat_base+PVEHK_EVENT_POST))(player,event,now);
        }
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {pvehk_events_on=0;pvehk_events_more=0;}
}
/* X4's own deferred posts on a slot's player (its target change: 0xaecb60 posts U::PlayerChangedTargetEvent through
 * the deferred post while the slot's replayed input runs in its scope) are due at X4's event phase, outside every
 * scope, and were dropped the same way (the audit: changed_target for slot 1 only). A due event whose source is a
 * slot's player is delivered inside that slot's scope (lockstep_event_order.h's delivery asks here). A scope that
 * cannot be opened there (another one open, another thread): delivered as before, counted.
 * A delivery that finds the slot's scope open already (17 Nov: a menu's UI event, AddUITriggeredEvent, replayed for
 * slot k: its deferred post is delivered while the replay's scope is still open) opens nothing, and it used to answer
 * "no scope": the scripts nobody owns never heard it (a ping from player 2's client reached only player 2's own cues,
 * player 1's reached the shared cue). The answer is now the slot with a minus sign: nothing to leave, but the event
 * is forwarded like any other. Status deliver=in/out/inside/unseated. */
static volatile LONG64 pvehk_delivered_scoped,pvehk_delivered_plain,pvehk_delivered_inside,pvehk_delivered_unseated;
static int pvehk_deliver_enter(void *source) {
    if(!source || !pvehk_events_more) return 0;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        if(pslots[k].player!=(unsigned char *)source) continue;
        if(!pslots[k].seated) {InterlockedIncrement64(&pvehk_delivered_unseated);return 0;}
        if(pstk_scope_slot==k) {InterlockedIncrement64(&pvehk_delivered_inside);return -(int)k;}
        if(pslot_enter(k)) {InterlockedIncrement64(&pvehk_delivered_scoped);return (int)k;}
        InterlockedIncrement64(&pvehk_delivered_plain);return 0;
    }
    return 0;
}
static void pvehk_deliver_leave(int scope) {if(scope>=2 && scope<=(int)PILOT_SLOTS && pstk_scope_slot==(unsigned)scope) (void)pslot_leave((unsigned)scope);}
/* ---- Scripts nobody owns hear every player (native_gate_shared_listeners, a session default) ----
 * A script that belongs to no player (the world's plots, story activators, statistics, the managers: the data vaults,
 * the signal leak manager, "the player entered sector X") registered its player listeners at the game's start, on the
 * universe's player of that moment: player 1's object. What players 2..N did never reached it: only player 1 could
 * open a data vault for the plot, start a story by flying somewhere, count a kill. (Read from a live session, 15 Nov:
 * player 1's object carries listeners for 92 event types, 41 cues on its sector change alone; a slot's player has its
 * own missions' and the engine's.) An event of slot k's player that the gate delivers inside slot k's scope (its own
 * posts, X4's due posts on that player) is therefore also handed to player 1's listeners of that type, as X4's
 * delivery 0x99c470 hands it over (lookup 0x999ae0(source, type, 0) -> the listener pointers at +8..+0x10; each one
 * still registered, 0x999dc0, gets vtable[4](listener, source, time, type, event); here once with player 1's object
 * as the source and once with slot k's, see the call), with these limits:
 *   - only listeners that are MD cues (vtable 0x2c75170) owned by no slot (lockstep_mission_owners.h): a mission
 *     player 1 accepted keeps hearing player 1 only, and the engine's own listeners (mission manager, UI) too; nor a
 *     plot the other players run as copies of their own (lockstep_mission_owners.h mpp_script_is_first);
 *   - not the notices about a library that is slot k's faction's alone (research, blueprints, its production:
 *     lockstep_research_owner.h rsch_event_private): the world's scripts take those as the one player faction's;
 *   - inside slot k's scope, so the script's player.entity, player.ship and player.sector are slot k's and what it
 *     starts is seen from that player (an instance it makes stays everybody's unless a claim is on the stack).
 * A one-time script goes to whoever triggers it first. X4's immediate posts of a sector, zone or cluster change are not
 * the gate's to deliver: they come through pvehk_place (below), which also hands what they start to slot k.
 * Status shared=on/events/calls/owned/over. */
#define PVEHK_FWD_LOOKUP 0x999ae0u
#define PVEHK_FWD_LOOKUP_SIZE 0x265u
#define PVEHK_FWD_LOOKUP_SHA "084258cbc58aec67bc26725e55bf8b8af8e2a3294900f6dda6cda4df102e6954"
#define PVEHK_FWD_LISTENS 0x999dc0u
#define PVEHK_FWD_LISTENS_SIZE 0x99u
#define PVEHK_FWD_LISTENS_SHA "a812d3d260bae97bf2547e3e90698adbe20ab0c4f4e486957f45d7708ee03028"
#define PVEHK_FWD_MAX 512u
#define PVEHK_FWD_UI_EVENT 0x2ba6e90u   /* U::UITriggeredEvent::vftable: MD's event_ui_triggered takes it from any source */
/* U::PlayerOwnedObjectAttackedObjectEvent::vftable: its MD handler (0xcabfa0) answers event_player_owned_attacked_object
 * whatever the source, so the two forms would both run the cue (lockstep_pilot_attack.h hands slot k's here). */
#define PVEHK_FWD_OWNED_ATTACK_EVENT 0x2ba95f0u
static int pvehk_forward_on;
static volatile LONG64 pvehk_forward_events,pvehk_forward_calls,pvehk_forward_owned,pvehk_forward_over;
static unsigned pvehk_forward_slot;   /* the slot whose event is being handed to player 1's listeners now, else 0 (lockstep_mission_owners.h's notices) */
static unsigned mpp_slot_of(const unsigned char *cue);   /* lockstep_mission_owners.h */
static int mpp_script_is_first(const unsigned char *cue);   /* ...a plot the others have copies of: player 1's own */
static int rsch_event_private(unsigned k,unsigned type);    /* lockstep_research_owner.h: a notice about slot k's faction's own library */
/* lockstep_mission_owners.h, a place event's first player (pvehk_place below): the scripts it is not handed to, a cue's
 * state, the call's frame rule and the claim of a static cue it made fire */
static int mpp_place_denied(const unsigned char *cue);static uint32_t mpp_cue_state(const unsigned char *cue);
static void mpp_place_call(unsigned k,const unsigned char *cue);static void mpp_place_fired(unsigned k,const unsigned char *cue,uint32_t before);
static int pvehk_place_mode;   /* the forward hands over a place event of slot k's player now (pvehk_place_forward) */
static volatile LONG64 pvehk_place_denied;
/* tests: in place of X4's lookup (then no registration check) and of the cue's owner */
static unsigned char *(*pvehk_forward_lookup_test)(void *source,unsigned type);
static unsigned (*pvehk_forward_owner_test)(const unsigned char *cue);
static void pvehk_due_install(void);   /* below: a slot's player's due events nobody on it listens for */
static void pvehk_place_install(void); /* below: a slot's player's sector, zone and cluster changes */
static int pvehk_forward_arm(void) {
    pvehk_forward_on=pnat_base && hash_ok(pnat_base+PVEHK_FWD_LOOKUP,PVEHK_FWD_LOOKUP_SIZE,PVEHK_FWD_LOOKUP_SHA) &&
        hash_ok(pnat_base+PVEHK_FWD_LISTENS,PVEHK_FWD_LISTENS_SIZE,PVEHK_FWD_LISTENS_SHA);
    if(pvehk_forward_on) {pvehk_due_install();pvehk_place_install();}
    return pvehk_forward_on;
}
/* Inside slot k's scope: the event to the listeners of `first` (player 1's object, or player 1's ship computer) that no
 * slot owns; `own` is slot k's counterpart for the player form (below), NULL for an object's event (the object form only). */
static void pvehk_forward_from(unsigned k,unsigned char *first,unsigned char *own,unsigned char *event,double now) {
    void *listeners[PVEHK_FWD_MAX];unsigned n=0,type=0;
    if(!pvehk_forward_on || k<2 || k>PILOT_SLOTS || pstk_scope_slot!=k || !event || !first || first==own) return;
    __try {
        void **vtable=NULL;unsigned char *entry=NULL;void **at=NULL,**end=NULL;
        /* A UI event's condition names no source: both forms below would match it and the cue would run twice (seen
         * live, 17 Nov: two rows for each ping of player 2, one for player 1's). It is handed over once, as the
         * slot's player's. */
        const int once=own && read_local(event,&vtable,8) && (vtable==(void**)(void*)(pnat_base+PVEHK_FWD_UI_EVENT) ||
                                                              vtable==(void**)(void*)(pnat_base+PVEHK_FWD_OWNED_ATTACK_EVENT));
        if(read_local(event,&vtable,8) && vtable) {
            type=((unsigned(*)(void*))vtable[1])(event);
            if(!rsch_event_private(k,type))
                entry=pvehk_forward_lookup_test?pvehk_forward_lookup_test(first,type):
                      ((unsigned char*(*)(void*,unsigned,unsigned char))(void*)(pnat_base+PVEHK_FWD_LOOKUP))(first,type,0);
        }
        if(entry && read_local(entry+8,&at,8) && read_local(entry+0x10,&end,8) && at && end>at) {
            for(;at<end;at++) {
                if(n==PVEHK_FWD_MAX) {InterlockedIncrement64(&pvehk_forward_over);break;}
                if(!read_local((const void*)at,&listeners[n],8)) break;
                n++;
            }
        }
        if(n) InterlockedIncrement64(&pvehk_forward_events);
        for(unsigned i=0;i<n;i++) {
            unsigned char *const cue=listeners[i];void **cv=NULL;
            /* a listener an earlier one took away is skipped before anything of it is read, as X4's delivery does */
            if(!cue || (!pvehk_forward_lookup_test &&
                        !((unsigned char(*)(void*,void*,unsigned))(void*)(pnat_base+PVEHK_FWD_LISTENS))(first,cue,type))) continue;
            if(!read_local(cue,&cv,8) || cv!=(void**)(void*)(pnat_base+SCRIPT_RNG_CUE_VT)) continue;   /* the engine's own listeners stay player 1's */
            if(pvehk_forward_owner_test?pvehk_forward_owner_test(cue):mpp_slot_of(cue) || mpp_script_is_first(cue)) {
                InterlockedIncrement64(&pvehk_forward_owned);continue;
            }
            if(pvehk_place_mode && mpp_place_denied(cue)) {InterlockedIncrement64(&pvehk_place_denied);continue;}
            InterlockedIncrement64(&pvehk_forward_calls);
            const uint32_t before=pvehk_place_mode?mpp_cue_state(cue):0u;
            if(pvehk_place_mode) mpp_place_call(k,cue);   /* the cue's frame for it is slot k's */
            /* Twice, for the two forms a script's condition has. An object condition (event_object_... object=
             * "player.entity") holds player 1's object and takes the event from that source; a player condition
             * (event_player_...) takes it from the universe's player, which in this scope is slot k's (the audit:
             * with player 1 as the source only the object form ran). Each call can match one form only.
             * Either way the event is slot k's (a notice it starts is slot k's: pvehk_forward_slot). */
            const unsigned outer=pvehk_forward_slot;pvehk_forward_slot=k;
            if(!once) ((void(*)(void*,void*,double,unsigned,void*))cv[4])(cue,first,now,type,event);
            if(own && (once || pvehk_forward_lookup_test || ((unsigned char(*)(void*,void*,unsigned))(void*)(pnat_base+PVEHK_FWD_LISTENS))(first,cue,type)))
                ((void(*)(void*,void*,double,unsigned,void*))cv[4])(cue,own,now,type,event);
            pvehk_forward_slot=outer;
            if(pvehk_place_mode) {mpp_place_call(0,NULL);mpp_place_fired(k,cue,before);}   /* a static cue it made fire: slot k's */
        }
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {
        pvehk_forward_on=0;pvehk_forward_slot=0;if(pvehk_place_mode) mpp_place_call(0,NULL);
    }
}
/* A slot's player's event: to player 1's object's listeners, in both forms. */
static void pvehk_forward(unsigned k,unsigned char *event,double now) {
    if(k>=2 && k<=PILOT_SLOTS) pvehk_forward_from(k,pslot_p1,pslots[k].player,event,now);
}
static void pvehk_deliver_forward(int scope,void *event,double time) {
    const int k=scope<0?-scope:scope;   /* minus: the scope was open already */
    if(k>=2 && k<=(int)PILOT_SLOTS) pvehk_forward((unsigned)k,event,time);
}
/* An event of a slot's player that nobody on that player listens for (17 Nov). X4's pass over the due events
 * (ProcessDueEvents 0x99bb50) asks for the source's listeners of the event's type (0x99bcf6: 0x999ae0(source, type,
 * 0)) and puts the event into the delivery batch only when there are some, or the source takes the type itself;
 * otherwise the event ends there and DeliverEvent, where the forward above sits, never sees it. A menu's UI event
 * (U::UITriggeredEvent) is such a type: no engine object listens for it on a player, so a ping from player 2's client
 * reached the script nobody owns only while player 2 had a mission of its own that listened for UI events (live: a
 * row with it, none without; a target change always arrived, X4 listens for that on every player).
 * The call goes through pvehk_due_lookup: for a seated slot's player without a listener of the type, when player
 * 1's object has listeners of it, the answer is an entry that is not empty and has nothing behind it (+0x20, all the
 * pass's next call 0x99c740 reads of it, taken from the real entry if there is one). X4 then batches the event; it
 * is delivered in the portable order with every other, inside the slot's scope, to nobody on that player, and the
 * forward hands it to the scripts nobody owns. Installed after the event order's own pin of the whole pass (its arm
 * is at boot) and only with it; the block is pinned. Status due=installed/batched. */
#define PVEHK_DUE_SITE 0x99bcf6u
#define PVEHK_DUE_PIN 0x99bcdfu
#define PVEHK_DUE_PIN_SIZE 0x70u
#define PVEHK_DUE_PIN_SHA "845bcac9e15b207c9195c5ed42ddcdedc028d0649d0d18d3ebd2bb8711b66851"
#define PVEHK_DUE_STUB 0x500u            /* on the slots' near page, after the area jobs' (0x4a0, 0x4c0) */
static __declspec(align(16)) unsigned char pvehk_due_fake[0x30];
static unsigned char pvehk_due_patched[5];
static int pvehk_due_installed;
static const char *pvehk_due_why="-";
static volatile LONG64 pvehk_due_batched;
static int pvehk_due_none(unsigned char *entry) {
    unsigned char *begin=NULL,*end=NULL;
    return !entry || !read_local(entry+8,&begin,8) || !read_local(entry+0x10,&end,8) || begin==end;
}
static unsigned char *pvehk_due_lookup(unsigned char *source,unsigned type,unsigned char create) {
    typedef unsigned char *(*lookup_fn)(void*,unsigned,unsigned char);
    unsigned char *const entry=pvehk_forward_lookup_test?pvehk_forward_lookup_test(source,type):
        ((lookup_fn)(void*)(pnat_base+PVEHK_FWD_LOOKUP))(source,type,create);
    unsigned char *const first=pslot_p1;
    if(!pvehk_forward_on || !source || !first || source==first) return entry;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        if(pslots[k].player!=source) continue;
        if(!pslots[k].seated || !pvehk_due_none(entry)) break;
        if(pvehk_due_none(pvehk_forward_lookup_test?pvehk_forward_lookup_test(first,type):((lookup_fn)(void*)(pnat_base+PVEHK_FWD_LOOKUP))(first,type,0))) break;
        void *behind=NULL,*begin=pvehk_due_fake,*end=pvehk_due_fake+8;
        if(entry) read_local(entry+0x20,&behind,8);
        memset(pvehk_due_fake,0,sizeof(pvehk_due_fake));
        memcpy(pvehk_due_fake+8,&begin,8);memcpy(pvehk_due_fake+0x10,&end,8);memcpy(pvehk_due_fake+0x20,&behind,8);
        InterlockedIncrement64(&pvehk_due_batched);
        return pvehk_due_fake;
    }
    return entry;
}
static void pvehk_due_install(void) {
    if(pvehk_due_installed) return;
    if(!InterlockedCompareExchange(&event_order_armed,0,0)) {pvehk_due_why="event_order";return;}
    if(!pslot_page || !hash_ok(pnat_base+PVEHK_DUE_PIN,PVEHK_DUE_PIN_SIZE,PVEHK_DUE_PIN_SHA)) {pvehk_due_why="pins";return;}
    if(!pres_site_install(PVEHK_DUE_SITE,PVEHK_FWD_LOOKUP,PVEHK_DUE_STUB,(void*)pvehk_due_lookup,pvehk_due_patched)) {pvehk_due_why="site";return;}
    pvehk_due_installed=1;pvehk_due_why="-";
}
static void pvehk_due_restore(void) {
    if(pvehk_due_installed && pres_site_uninstall(PVEHK_DUE_SITE,PVEHK_FWD_LOOKUP,pvehk_due_patched)) pvehk_due_installed=0;
}
/* The same event dispatched at once inside slot k's scope (the event_player_* kind; header). flag: the byte at +0x28 of
 * an event of 0x30 bytes (U::TeleportSuccessfulEvent's, X4's vfunc76 writes its own flag there; lockstep_pilot_teleport.h). */
static unsigned char pvehk_event_test_flag;   /* tests: the flag of the last post */
static void pvehk_event_now_flag(unsigned k,unsigned vtable,unsigned size,uint64_t first,uint64_t second,unsigned char flag,double now) {
    unsigned char *const player=pslots[k].player;
    if(pvehk_event_test) {InterlockedIncrement64(&pvehk_events_posted);pvehk_event_test_flag=flag;pvehk_event_test(player,vtable,first,second);return;}
    const int entered=pstk_scope_slot==k?0:pslot_enter(k);
    if(pstk_scope_slot!=k) {InterlockedIncrement64(&pvehk_events_unscoped);return;}
    InterlockedIncrement64(&pvehk_events_posted);
    __try {
        unsigned char *event=((void*(*)(size_t,uintptr_t,uintptr_t,uintptr_t,unsigned))(void*)(pnat_base+PVEHK_EVENT_ALLOCATE))(size,0,0,0,0x10);
        if(event) {
            void *vt=pnat_base+vtable;
            memset(event,0,size);memcpy(event,&vt,8);
            if(size>=0x20u) memcpy(event+0x18,&first,8);
            if(size>=0x28u) memcpy(event+0x20,&second,8);
            if(size>=0x30u) event[0x28]=flag;
            pvehk_forward(k,event,now);   /* the scripts nobody owns first: X4's post may free the event */
            ((void(*)(void*,void*,double,unsigned char))(void*)(pnat_base+PVEHK_EVENT_POST_NOW))(player,event,now,1);
        }
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {pvehk_events_more=0;}
    if(entered) (void)pslot_leave(k);
}
static void pvehk_event_now(unsigned k,unsigned vtable,unsigned size,uint64_t first,uint64_t second,double now) {
    pvehk_event_now_flag(k,vtable,size,first,second,0,now);
}
/* What the ship a slot sits in did. Six notices of X4 have one shape: the event is posted on the object that did it
 * (the actor), then 0x9f9250 is asked for the object the universe's player controls and, when that is the actor, the
 * same event is posted on the universe's player. That last post is MD's event_player_* form of it, the trigger of the
 * kill, repair, collect and bomb missions. The notices run in the simulation outside every scope, where the universe's
 * player is player 1: what slot k's ship or suit did told nobody.
 *   kill       0x3e54d6..  U::KilledComponentEvent 0x2baadc8, 0x28: method dword +0x18, killed ID +0x20
 *   leak       0x6511cb..  U::SignalLeakRepairedEvent 0x2ba65d0, 0x28: leak ID +0x18, its parent's ID +0x20
 *   powerup    0x3cc4f4..  U::CollectedPowerUpEvent 0x2ac1668, 0x18: no fields
 *   ammo       0x3cd138..  U::CollectedAmmoEvent 0x2ba9d98, 0x28: +0x18 qword, +0x20 dword
 *   repaired   0x5180cb..  U::DestructibleRepairedEvent 0x2ba6678, 0x28: repaired ID +0x18, +0x20 a second ID
 *   defused    0x846881..  U::BombDefusedEvent 0x2bab260, 0x20: +0x18 qword
 * The actor's post is answered here (its call carries the actor and the event): X4's post runs unchanged and, when
 * the actor is the ship slot k sits in, an event with the same fields is dispatched on slot k's player inside its
 * scope (pvehk_event_now). The repaired notice's posts lie across two 16-byte blocks, which one exchange cannot
 * re-point; there the call of the event's constructor (0x9cfc70, which reads no r9) is answered and its stub hands
 * over the actor the block holds in rbx. (The call of 0x9f9250 itself lies across two blocks in the kill notice.)
 * Status did=installed/sites/seen/told (seen: notices that came by, anybody's), kill= and leak= their own counts. */
#define PVEHK_KILL_ASK 0x9f9250u
#define PVEHK_KILL_ASK_SIZE 0x100u
#define PVEHK_KILL_ASK_SHA "8658e6b8562c4baea792f82004644aa7434e561df2f80333ffa1d611705c38ed"
#define PVEHK_EVENT_KILLED 0x2baadc8u
#define PVEHK_EVENT_LEAK 0x2ba65d0u
#define PVEHK_EVENT_POWERUP 0x2ac1668u
#define PVEHK_EVENT_AMMO 0x2ba9d98u
#define PVEHK_EVENT_REPAIRED 0x2ba6678u
#define PVEHK_EVENT_DEFUSED 0x2bab260u
#define PVEHK_REPAIRED_CTOR 0x9cfc70u
#define PVEHK_REPAIRED_CTOR_SIZE 0x40u
#define PVEHK_REPAIRED_CTOR_SHA "eff31ea1abc2056f615b0a8cd5857451db5fde1254bce8359d9deca8db216734"
enum {PVEHK_DID_KILL=0,PVEHK_DID_LEAK,PVEHK_DID_POWERUP,PVEHK_DID_AMMO,PVEHK_DID_DEFUSED,PVEHK_DID_REPAIRED,PVEHK_DID_SITES};
typedef struct {
    unsigned site,target,block,stub,vtable,size;   /* the call, what it calls, the pinned 0x100 bytes, the stub on the slots' near page */
    const char *sha;
    int installed;unsigned char patched[5];volatile LONG64 told,seen;   /* seen: notices of this kind that came by, anybody's */
} pvehk_did_site;
static pvehk_did_site pvehk_did_sites[PVEHK_DID_SITES]={
    [PVEHK_DID_KILL]    ={0x3e5563u,PVEHK_EVENT_POST_NOW,0x3e5560u,0x200u,PVEHK_EVENT_KILLED,0x28u,"abfbe982a41f6af4bd4e6b5fe7628bd8ac1a446479211178c24752f1cc7a1ab2",0,{0},0,0},
    [PVEHK_DID_LEAK]    ={0x651249u,PVEHK_EVENT_POST_NOW,0x651200u,0x220u,PVEHK_EVENT_LEAK,0x28u,"e47b2103857684f9d8b6fb5eede8aff7bd159cc515924466edf071e4f24aa10d",0,{0},0,0},
    [PVEHK_DID_POWERUP] ={0x3cc565u,PVEHK_EVENT_POST_NOW,0x3cc500u,0x240u,PVEHK_EVENT_POWERUP,0x18u,"e8d8a38ea129bbe7be8eb3a79fe644159b2dd77379f0d744ec8bd02ce88c41bf",0,{0},0,0},
    [PVEHK_DID_AMMO]    ={0x3cd1b0u,PVEHK_EVENT_POST_NOW,0x3cd150u,0x260u,PVEHK_EVENT_AMMO,0x28u,"417a8accead609baa50b184fbeb8a5e94ceefc95af5896476e1a8fb9309a96f8",0,{0},0,0},
    [PVEHK_DID_DEFUSED] ={0x8468e1u,PVEHK_EVENT_POST_NOW,0x846880u,0x2a0u,PVEHK_EVENT_DEFUSED,0x20u,"79db70aa9d92b41e6b26addb92d2e0a526e8572d56e6c17ae55e715085c9877b",0,{0},0,0},
    [PVEHK_DID_REPAIRED]={0x518158u,PVEHK_REPAIRED_CTOR,0x518100u,0x280u,PVEHK_EVENT_REPAIRED,0x28u,"cee9e10334bbbcff93e5bbc93f0a311acf88b620f57530b4eac0c52f407ee3fe",0,{0},0,0},
};
/* The slot whose ship the actor is (0: none, or player 1's, which X4 tells itself); the event goes out. */
static unsigned pvehk_did(unsigned which,unsigned char *actor,uint64_t first,uint64_t second) {
    double now=0;
    if(which>=PVEHK_DID_SITES || !actor || (!pvehk_events_more && !pvehk_event_test)) return 0;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        if(!pslots[k].seated || !pslots[k].player || pslots[k].ship!=actor) continue;
        if(!pvehk_event_test && (!pnat_fn.time || !pnat_fn.time(&now))) return 0;
        InterlockedIncrement64(&pvehk_did_sites[which].told);
        pvehk_event_now(k,pvehk_did_sites[which].vtable,pvehk_did_sites[which].size,first,second,now);
        return k;
    }
    return 0;
}
static unsigned pvehk_killed(unsigned char *killer,uint64_t killed_id,unsigned method) {
    return killed_id?pvehk_did(PVEHK_DID_KILL,killer,method,killed_id):0;
}
/* The leak's parent may be none (X4 writes 0 then); it goes out as X4 wrote it. */
static unsigned pvehk_leak_repaired(unsigned char *repairer,uint64_t leak_id,uint64_t parent_id) {
    return leak_id?pvehk_did(PVEHK_DID_LEAK,repairer,leak_id,parent_id):0;
}
/* The fields of an event X4 built, by its vtable: which notice it is, or PVEHK_DID_SITES. */
static unsigned pvehk_did_read(const unsigned char *event,uint64_t *first,uint64_t *second) {
    void *vtable=NULL;
    *first=0;*second=0;
    if(!event || !read_local(event,&vtable,8)) return PVEHK_DID_SITES;
    for(unsigned i=0;i<PVEHK_DID_SITES;i++) {
        if(vtable!=(void*)(pnat_base+pvehk_did_sites[i].vtable)) continue;
        if(pvehk_did_sites[i].size>=0x20u && !read_local(event+0x18,first,8)) return PVEHK_DID_SITES;
        if(pvehk_did_sites[i].size>=0x28u && !read_local(event+0x20,second,8)) return PVEHK_DID_SITES;
        return i;
    }
    return PVEHK_DID_SITES;
}
/* The actor's post. The fields are read before it: an event dispatched at once may be gone after it. */
static uintptr_t pvehk_did_post(unsigned char *actor,unsigned char *event,double time,unsigned char immediate) {
    uint64_t first=0,second=0;
    const unsigned which=actor && !pstk_scope_slot?pvehk_did_read(event,&first,&second):PVEHK_DID_SITES;
    const uintptr_t answer=((uintptr_t(*)(void*,void*,double,unsigned char))(void*)(pnat_base+PVEHK_EVENT_POST_NOW))(actor,event,time,immediate);
    if(which<PVEHK_DID_SITES) InterlockedIncrement64(&pvehk_did_sites[which].seen);
    if(which==PVEHK_DID_KILL) (void)pvehk_killed(actor,second,(unsigned)first);
    else if(which==PVEHK_DID_LEAK) (void)pvehk_leak_repaired(actor,first,second);
    else if(which<PVEHK_DID_SITES) (void)pvehk_did(which,actor,first,second);
    return answer;
}
/* The repaired notice: the constructor of the actor's event, with the actor from the stub. */
static void *pvehk_did_ctor(unsigned char *event,void *repaired,void *other,unsigned char *actor) {
    void *made=((void*(*)(void*,void*,void*))(void*)(pnat_base+PVEHK_REPAIRED_CTOR))(event,repaired,other);
    uint64_t first=0,second=0;
    InterlockedIncrement64(&pvehk_did_sites[PVEHK_DID_REPAIRED].seen);
    if(actor && !pstk_scope_slot && pvehk_did_read(event,&first,&second)==PVEHK_DID_REPAIRED && first) (void)pvehk_did(PVEHK_DID_REPAIRED,actor,first,second);
    return made;
}
static void pvehk_did_install(void) {
    static const unsigned char head[3]={0x49,0x89,0xd9};   /* mov r9,rbx: the repaired notice's actor */
    if(!pslot_page || !pvehk_events_more || !hash_ok(pnat_base+PVEHK_KILL_ASK,PVEHK_KILL_ASK_SIZE,PVEHK_KILL_ASK_SHA)) return;
    for(unsigned i=0;i<PVEHK_DID_SITES;i++) {
        pvehk_did_site *d=&pvehk_did_sites[i];
        if(d->installed || !hash_ok(pnat_base+d->block,0x100u,d->sha)) continue;
        if(i!=PVEHK_DID_REPAIRED) {
            if(pres_site_install(d->site,d->target,d->stub,(void*)pvehk_did_post,d->patched)) d->installed=1;
            continue;
        }
        if(!hash_ok(pnat_base+PVEHK_REPAIRED_CTOR,PVEHK_REPAIRED_CTOR_SIZE,PVEHK_REPAIRED_CTOR_SHA)) continue;
        DWORD old,ignored;int32_t rel=(int32_t)((intptr_t)(pnat_base+d->target)-(intptr_t)(pnat_base+d->site+5));
        unsigned char original[5]={0xe8,0,0,0,0};memcpy(original+1,&rel,4);
        if(memcmp(pnat_base+d->site,original,5) || !VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) continue;
        memcpy(pslot_page+d->stub,head,sizeof(head));
        pview_abs_jump(pslot_page+d->stub+sizeof(head),(void*)pvehk_did_ctor);
        if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) continue;
        FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
        rel=(int32_t)((intptr_t)(pslot_page+d->stub)-(intptr_t)(pnat_base+d->site+5));
        d->patched[0]=0xe8;memcpy(d->patched+1,&rel,4);
        if(pview_cas(d->site,original,d->patched,5)) d->installed=1;
    }
}
static void pvehk_did_restore(void) {
    for(unsigned i=0;i<PVEHK_DID_SITES;i++) {
        pvehk_did_site *d=&pvehk_did_sites[i];
        if(d->installed && pres_site_uninstall(d->site,d->target,d->patched)) d->installed=0;
    }
}
static int pvehk_did_installed(void) {int n=0;for(unsigned i=0;i<PVEHK_DID_SITES;i++) n+=pvehk_did_sites[i].installed!=0;return n;}
static long long pvehk_did_told(void) {long long n=0;for(unsigned i=0;i<PVEHK_DID_SITES;i++) n+=(long long)pvehk_did_sites[i].told;return n;}
static int pvehk_did_seen(char *out,size_t capacity) {   /* kill:leak:powerup:ammo:defused:repaired */
    int n=0;
    for(unsigned i=0;i<PVEHK_DID_SITES && n>=0 && (size_t)n<capacity;i++) n+=snprintf(out+n,capacity-(size_t)n,"%s%lld",i?":":"",(long long)pvehk_did_sites[i].seen);
    return n;
}
/* A player's scan and unlock routine. U::Player's own update (0x7113e0) runs for every player object every frame and
 * calls 0x71cf10(player, 0) at 0x7118ec: the progress of a signal leak scan or a lock's unlock, with six posts at once
 * on that player (U::SignalUnlockStarted / Finished / Failed / Impossible ...: MD's event_player_signal_unlock_*, the
 * most used mission trigger of the stock scripts). For a slot's player it ran outside every scope, where the
 * universe's player is player 1 and MD takes the event_player_* kind from nobody else: slot k's missions never heard
 * its own unlocks. The routine's entry jumps here (its one call, 0x7118ec, lies across two 16-byte blocks): for the
 * player of a seated slot it runs inside that slot's scope, as it does for X4's own player; for every other player
 * object it is X4's routine unchanged. A scope that cannot be opened there: X4's routine, counted.
 * Status scan=installed/scoped/plain. */
#define PVEHK_SCAN 0x71cf10u
#define PVEHK_SCAN_SIZE 0x70u
#define PVEHK_SCAN_SHA "da24aed61fc050968cfa30091a8b21a5ed05adc14eda67d29d518b11757781b7"
#define PVEHK_SCAN_STUB 0x2c0u                /* on the slots' near page, after the notices' (0x200..0x2a0) */
#define PVEHK_SCAN_TRAMPOLINE 0x2e0u          /* the routine's first two instructions, then on at +6 */
static const unsigned char pvehk_scan_head[6]={0x48,0x8b,0xc4,0x88,0x50,0x10};   /* mov rax,rsp; mov [rax+0x10],dl */
static int pvehk_scan_installed;
static unsigned char pvehk_scan_patched[5];
static volatile LONG64 pvehk_scans_scoped,pvehk_scans_plain;
static void (*pvehk_scan_test)(void *player,unsigned char flag);
/* The slot (2..N) whose seated player this is, when no scope is open; 0 otherwise. */
static unsigned pvehk_scan_slot(const unsigned char *player) {
    if(!player || pstk_scope_slot) return 0;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) if(pslots[k].seated && pslots[k].player==player) return k;
    return 0;
}
static void pvehk_scan(unsigned char *player,unsigned char flag) {
    void (*const native)(void*,unsigned char)=pvehk_scan_test?pvehk_scan_test:(void(*)(void*,unsigned char))(void*)(pslot_page+PVEHK_SCAN_TRAMPOLINE);
    const unsigned k=pvehk_scan_slot(player);
    if(!k) {native(player,flag);return;}
    if(!pslot_enter(k)) {InterlockedIncrement64(&pvehk_scans_plain);native(player,flag);return;}
    InterlockedIncrement64(&pvehk_scans_scoped);
    native(player,flag);
    (void)pslot_leave(k);
}
/* The routine's own posts. Inside slot k's scope they reach slot k's listeners; the scripts nobody owns keep theirs on
 * player 1's object (pvehk_forward): the data vault plot waits there for event_player_signal_unlock_finished (28 cues
 * in a live session), the signal leak manager too. Five of the routine's six posts on its player are answered (the
 * sixth, the failed unlock at 0x71d90d, lies across two 16-byte blocks): the event goes to those scripts first, then
 * X4's post runs unchanged. Pinned with the routine's body. Status scanposts=installed/5/forwarded. */
#define PVEHK_SCAN_BODY 0x71cf80u
#define PVEHK_SCAN_BODY_SIZE 0x20b5u
#define PVEHK_SCAN_BODY_SHA "d562229b9cc2af6e4a8cae53299d4ef74c0dabd865c24e878a78657c13d77b53"
#define PVEHK_SCAN_POST_STUB 0x460u           /* on the slots' near page, after the speech queue's (0x420, 0x440) */
#define PVEHK_SCAN_POSTS 5u
static const unsigned pvehk_scan_post_sites[PVEHK_SCAN_POSTS]={0x71d9d2u,0x71d9e8u,0x71db78u,0x71ed2au,0x71efd7u};
static unsigned char pvehk_scan_post_patched[PVEHK_SCAN_POSTS][5];
static int pvehk_scan_posts_installed;
static volatile LONG64 pvehk_scan_posts_forwarded;
static uintptr_t pvehk_scan_post(unsigned char *source,unsigned char *event,double time,unsigned char immediate) {
    const unsigned k=pstk_scope_slot;
    if(k>=2 && k<=PILOT_SLOTS && source && source==pslots[k].player && event) {
        InterlockedIncrement64(&pvehk_scan_posts_forwarded);
        pvehk_forward(k,event,time);
    }
    return ((uintptr_t(*)(void*,void*,double,unsigned char))(void*)(pnat_base+PVEHK_EVENT_POST_NOW))(source,event,time,immediate);
}
static void pvehk_scan_posts_install(void) {
    if(pvehk_scan_posts_installed || !pvehk_scan_installed || !hash_ok(pnat_base+PVEHK_SCAN_BODY,PVEHK_SCAN_BODY_SIZE,PVEHK_SCAN_BODY_SHA)) return;
    for(unsigned i=0;i<PVEHK_SCAN_POSTS;i++) {
        if(pres_site_install(pvehk_scan_post_sites[i],PVEHK_EVENT_POST_NOW,PVEHK_SCAN_POST_STUB,(void*)pvehk_scan_post,pvehk_scan_post_patched[i])) {
            pvehk_scan_posts_installed++;continue;
        }
        while(i--) if(pres_site_uninstall(pvehk_scan_post_sites[i],PVEHK_EVENT_POST_NOW,pvehk_scan_post_patched[i])) pvehk_scan_posts_installed--;
        return;   /* all five or none */
    }
}
static void pvehk_scan_posts_restore(void) {
    for(unsigned i=0;i<PVEHK_SCAN_POSTS && pvehk_scan_posts_installed;i++)
        if(pres_site_uninstall(pvehk_scan_post_sites[i],PVEHK_EVENT_POST_NOW,pvehk_scan_post_patched[i])) pvehk_scan_posts_installed--;
}
static void pvehk_scan_install(void) {
    DWORD old,ignored;int32_t rel=0;
    if(pvehk_scan_installed || !pslot_page || !pvehk_events_more || !hash_ok(pnat_base+PVEHK_SCAN,PVEHK_SCAN_SIZE,PVEHK_SCAN_SHA) ||
       memcmp(pnat_base+PVEHK_SCAN,pvehk_scan_head,sizeof(pvehk_scan_head))) return;
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return;
    pview_abs_jump(pslot_page+PVEHK_SCAN_STUB,(void*)pvehk_scan);
    memcpy(pslot_page+PVEHK_SCAN_TRAMPOLINE,pvehk_scan_head,sizeof(pvehk_scan_head));
    pview_abs_jump(pslot_page+PVEHK_SCAN_TRAMPOLINE+sizeof(pvehk_scan_head),pnat_base+PVEHK_SCAN+sizeof(pvehk_scan_head));
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return;
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    rel=(int32_t)((intptr_t)(pslot_page+PVEHK_SCAN_STUB)-(intptr_t)(pnat_base+PVEHK_SCAN+5));
    pvehk_scan_patched[0]=0xe9;memcpy(pvehk_scan_patched+1,&rel,4);
    if(pview_cas(PVEHK_SCAN,pvehk_scan_head,pvehk_scan_patched,5)) pvehk_scan_installed=1;
}
static void pvehk_scan_restore(void) {
    if(pvehk_scan_installed && pview_cas(PVEHK_SCAN,pvehk_scan_patched,pvehk_scan_head,5)) pvehk_scan_installed=0;
}
/* ---- a slot's own ship computer (native_gate_computer_forward, a session default; Windows gate) ----
 * X4's ship computer speaks through md/notifications.xml's cue BettyNotifications: event_object_signalled
 * object="player.computer", and on 'autopilot_engaged' a speak of line 920 by player.computer (disengaged 921). A script
 * nobody owns, so its listener sits on player 1's computer (registered at the game's start, as the player's object's
 * listeners are). The autopilot's own routines signal the computer of the player they run for: AutopilotStart 0x72bf20
 * (0x703ac0(player) at 0x72bfc8 into rsi; the U::SignalEvent with 'autopilot_engaged', 0x2b4b390, built into r14; posted
 * at 0x72c0ac with 0x99a3f0(computer, event, time, immediate 1)) and AutopilotStop 0x72bb10 ('autopilot_disengaged',
 * 0x2b4b358, posted at 0x72bcab the same way). Slot k's autopilot is replayed in slot k's scope (lockstep_ui_replay.h), so
 * the signal went to P_k's computer, where nothing listens: player 2's Shift+A gave no voice (24 Nov: no SPEECH_REQUEST
 * of 10002:920 in the whole log). Immediate posts are delivered at once (0x99a3f0 -> 0x99c470), never through the due
 * batch that the event order delivers, so the delivery's forward (above) never saw them.
 * Both posts are answered here, before X4's post (which may free the event): when the source is the computer of a seated
 * slot k's player (k = 2..N, 0x703ac0(P_k)), inside slot k's scope (opened when it is not open; another scope open:
 * counted, nothing forwarded), the event goes to the listeners of player 1's computer that no slot owns, in the object
 * form only (the cue's object condition holds player 1's computer): pvehk_forward_from. pvehk_forward_slot is k during
 * the call, and the notices' rule (lockstep_mission_owners.h mpp_notice_slot) reads a frame whose event object is player
 * 1's computer as slot k's then: the cue's frame is slot k's (scope and owner), its speak (player.computer: P_k's
 * computer in slot k's scope) is voiced on client k and muted on the others (lockstep_speech_local.h), and its emitter
 * is placed on client k (lockstep_pilot_present.h, the sound's source). With the rule on, the notices' rule also reads
 * an event object that is a slot player's computer as that slot's (pvehk_computer_slot_by_id): slot 1's own autopilot
 * line is slot 1's notice (voiced on client 1 only). The same on every client: the replay runs everywhere.
 * The sites: the engaged post's call lies across two 16-byte blocks, so the two instructions before it (mov rdx,r14;
 * mov rcx,rsi at 0x72c0a6, six bytes in one block) jump to a stub of ours that calls the note with (rsi, r14, xmm2) and
 * runs them again; the disengaged post's call (0x72bcab) is re-pointed to pvehk_computer_post. Both routines' entries are
 * the UI replay's (its pins read them first, before this module's first pass installs), so the pins start behind them:
 * 0x72bf40..0x72c0b1 and 0x72bb20..0x72bcb0. The sites are checked every gate tick (221 player sound,
 * why=computer_site_lost). Not covered: the computer's other signals (entering_atmosphere 0x711bd4,
 * entering_hazardous_region 0x711e5e) from U::Player's update, which runs for P_k outside every scope.
 * Status computer=on/installed/seen/forwarded/unscoped. */
#define PVEHK_COMPUTER_ENGAGED 0x72c0a6u             /* mov rdx,r14; mov rcx,rsi before the engaged post at 0x72c0ac */
#define PVEHK_COMPUTER_ENGAGED_RESUME 0x72c0acu
#define PVEHK_COMPUTER_ENGAGED_PIN 0x72bf40u
#define PVEHK_COMPUTER_ENGAGED_PIN_SIZE 0x171u
#define PVEHK_COMPUTER_ENGAGED_SHA "b80da66133fb3a0946916ecba2d8541ba67b340aa2eb7f42c1fef77464652302"
#define PVEHK_COMPUTER_DISENGAGED 0x72bcabu          /* call 0x99a3f0: the disengaged post */
#define PVEHK_COMPUTER_DISENGAGED_PIN 0x72bb20u
#define PVEHK_COMPUTER_DISENGAGED_PIN_SIZE 0x190u
#define PVEHK_COMPUTER_DISENGAGED_SHA "fdfffadae05bce9a6e09eef0b206bb169860c7c96df7f2b50216d791807c6ec2"
#define PVEHK_COMPUTER_ENGAGED_STUB 0xdc0u           /* on the slots' near page (free from 0xaa0 to 0xeff, 24 Nov), PSND_STUB_ROOM */
#define PVEHK_COMPUTER_DISENGAGED_STUB 0xe80u
static const unsigned char pvehk_computer_movs[6]={0x49,0x8b,0xd6,0x48,0x8b,0xce};   /* mov rdx,r14; mov rcx,rsi */
static int pvehk_computer_on,pvehk_computer_installed;
static unsigned char pvehk_computer_engaged_patched[6],pvehk_computer_disengaged_patched[5];
static volatile LONG64 pvehk_computer_seen,pvehk_computer_forwarded,pvehk_computer_unscoped;
static void *(*pvehk_computer_fn)(void *player,void **out);   /* tests: in place of 0x703ac0 */
static int pvehk_computer_pins(void) {
    return hash_ok(image+PVEHK_COMPUTER_ENGAGED_PIN,PVEHK_COMPUTER_ENGAGED_PIN_SIZE,PVEHK_COMPUTER_ENGAGED_SHA) &&
        hash_ok(image+PVEHK_COMPUTER_DISENGAGED_PIN,PVEHK_COMPUTER_DISENGAGED_PIN_SIZE,PVEHK_COMPUTER_DISENGAGED_SHA) &&
        hash_ok(image+SPL_SPEAKER,SPL_SPEAKER_SIZE,SPL_SPEAKER_SHA);   /* 0x703ac0, the player's computer among its children */
}
/* A player's ship computer (0x703ac0), NULL none. */
static unsigned char *pvehk_computer_of(unsigned char *player) {
    void *entity=NULL;
    if(!player || !pvehk_computer_on) return NULL;
    __try {(pvehk_computer_fn?pvehk_computer_fn:(void*(*)(void*,void**))(void*)(pnat_base+SPL_SPEAKER))(player,&entity);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {entity=NULL;}
    return entity;
}
/* The seated slot (2..N) whose player's computer this is; 0 none. */
static unsigned pvehk_computer_slot(const unsigned char *computer) {
    if(!computer) return 0;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) if(pslots[k].seated && pslots[k].player && pvehk_computer_of(pslots[k].player)==computer) return k;
    return 0;
}
/* The slot (1..N) whose player's computer has this runtime ID; 0 none or the rule off (lockstep_mission_owners.h). */
static unsigned pvehk_computer_slot_by_id(uint64_t id) {
    if(!pvehk_computer_installed || !id) return 0;
    for(unsigned k=1;k<=PILOT_SLOTS;k++) {
        unsigned char *const player=k==1?pslot_p1:pslots[k].player,*computer=NULL;uint64_t at=0;
        if(!player || (k>=2 && !pslots[k].seated) || !(computer=pvehk_computer_of(player))) continue;
        if(read_local(computer+8,&at,8) && at==id) return k;
    }
    return 0;
}
/* Before X4's post of a computer's signal: slot k's computer's event to the cues nobody owns on player 1's computer. */
static void pvehk_computer_note(unsigned char *source,unsigned char *event,double now) {
    if(!pvehk_computer_installed || !pvehk_forward_on || !source || !event || GetCurrentThreadId()!=ui_thread) return;
    InterlockedIncrement64(&pvehk_computer_seen);
    const unsigned k=pvehk_computer_slot(source);
    unsigned char *const first=k?pvehk_computer_of(pslot_p1):NULL;
    if(!k || !first || first==source) return;
    const int entered=pstk_scope_slot==k?0:pslot_enter(k);
    if(pstk_scope_slot!=k) {InterlockedIncrement64(&pvehk_computer_unscoped);return;}
    InterlockedIncrement64(&pvehk_computer_forwarded);
    pvehk_forward_from(k,first,NULL,event,now);
    if(entered) (void)pslot_leave(k);
}
static uintptr_t (*pvehk_computer_post_test)(void *source,void *event,double time,unsigned char immediate);   /* tests: X4's post */
static uintptr_t pvehk_computer_post(unsigned char *source,unsigned char *event,double time,unsigned char immediate) {
    pvehk_computer_note(source,event,time);
    if(pvehk_computer_post_test) return pvehk_computer_post_test(source,event,time,immediate);
    return ((uintptr_t(*)(void*,void*,double,unsigned char))(void*)(pnat_base+PVEHK_EVENT_POST_NOW))(source,event,time,immediate);
}
/* gate.computer_forward(1): before the first tick, with the forward armed; installed at this module's first pass. */
static int pvehk_computer_arm(void) {
    if(pvehk_computer_on) return 1;
    if(!pvehk_forward_on || !stats.active || stats.issued || GetCurrentThreadId()!=ui_thread || !pvehk_computer_pins() ||
       memcmp(image+PVEHK_COMPUTER_ENGAGED,pvehk_computer_movs,6)) return 0;
    pvehk_computer_seen=pvehk_computer_forwarded=pvehk_computer_unscoped=0;pvehk_computer_on=1;return 1;
}
static void pvehk_computer_install(void) {
    static const unsigned char load[]={0x48,0x89,0xf1,0x4c,0x89,0xf2};   /* mov rcx,rsi; mov rdx,r14 (the time is in xmm2 already) */
    if(pvehk_computer_installed || !pvehk_computer_on || !pslot_page) return;
    if(!pres_site_install(PVEHK_COMPUTER_DISENGAGED,PVEHK_EVENT_POST_NOW,PVEHK_COMPUTER_DISENGAGED_STUB,(void*)pvehk_computer_post,pvehk_computer_disengaged_patched)) return;
    if(!psnd_site_install(PVEHK_COMPUTER_ENGAGED,pvehk_computer_movs,6,PVEHK_COMPUTER_ENGAGED_STUB,load,sizeof(load),(const void*)pvehk_computer_note,PSND_OUT_NONE,0,
                          pvehk_computer_movs,6,PVEHK_COMPUTER_ENGAGED_RESUME,pvehk_computer_engaged_patched)) {
        (void)pres_site_uninstall(PVEHK_COMPUTER_DISENGAGED,PVEHK_EVENT_POST_NOW,pvehk_computer_disengaged_patched);return;
    }
    pvehk_computer_installed=1;
}
static int pvehk_computer_intact(void) {
    return !pvehk_computer_installed || (!memcmp(pnat_base+PVEHK_COMPUTER_ENGAGED,pvehk_computer_engaged_patched,6) &&
                                         !memcmp(pnat_base+PVEHK_COMPUTER_DISENGAGED,pvehk_computer_disengaged_patched,5));
}
static void pvehk_computer_restore(void) {
    if(!pvehk_computer_installed) return;
    pvehk_computer_installed=0;   /* the note does nothing from here on */
    const int a=pview_cas(PVEHK_COMPUTER_ENGAGED,pvehk_computer_engaged_patched,pvehk_computer_movs,6);
    const int b=pres_site_uninstall(PVEHK_COMPUTER_DISENGAGED,PVEHK_EVENT_POST_NOW,pvehk_computer_disengaged_patched);
    if(!a || !b) pvehk_computer_installed=1;   /* not ours any more: the check names it */
}
/* ---- player sound: the tick check of this package's sites (stop 221, PSND_STOP_REASON) ----
 * lockstep_pilot_present.h's sound entry and sound source, the computer's two sites above. Status PLAYER_SOUND. */
static const char *psnd_why="-";
static int psnd_installed(void) {return pres_sound_installed || pres_source_installed || pvehk_computer_installed;}
static int psnd_check(void) {
    const char *why=!pres_sound_intact()?"sound_entry_lost":!pres_source_intact()?"source_entry_lost":!pvehk_computer_intact()?"computer_site_lost":NULL;
    if(!why) return 1;
    if(!strcmp(psnd_why,"-")) psnd_why=why;
    return 0;
}
static int pvehk_computer_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity," computer=%d/%d/%lld/%lld/%lld",pvehk_computer_on,pvehk_computer_installed,(long long)pvehk_computer_seen,
        (long long)pvehk_computer_forwarded,(long long)pvehk_computer_unscoped);
    return n>0 && (size_t)n<capacity?n:0;
}
/* ---- a slot's player's sector, zone and cluster changes for the scripts nobody owns (with native_gate_shared_listeners) ----
 * MD's event_object_changed_sector / _zone / _cluster object="player.entity" (129 sector and 4 zone listeners in the
 * stock scripts: story activators, the Terran prelude's Teladi scene, Story_Terran_Core's restricted core, the
 * notifications' "Entering system", world setups, the subscriptions' mission threads) come from one place in X4:
 * U::Object's parent change 0x6cdab0 (vtable slot 624 of every object class), when a ship (or anything) moves into
 * another zone. It gathers the objects (class 0x49) and the entities (0x2c) of its subtree from the children tables
 * (+0xa8; 0x3d9690 / 0x3d9820) and posts U::ChangedClusterEvent / ChangedSectorEvent / ChangedZoneEvent (0x28 bytes:
 * the old place's ID at +0x18, the new one's at +0x20; vtables 0x2ba9af8 / 0x2ba9b30 / 0x2ba9a88) on the object itself,
 * on each object and on each entity, with EventSource::PostEvent 0x99a3f0 and the immediate flag: delivered at once
 * (0x99c470) to that source's listeners, never through the due events that the gate's delivery forwards. A player sits
 * in its ship's cockpit room, so X4's own player hears its arrival from the entities' posts. Two things kept slot k's
 * arrival from the scripts nobody owns:
 *   - their listeners are on player 1's object (registered at the game's start), and X4's post on P_k reaches P_k's
 *     own listeners only (slot k's missions and story copy); the forward's other sites never see an immediate post;
 *   - P_k is in the room's children table only while its body is (lockstep_pilot_foot_body.h: 'parked' while anybody
 *     walks, none in a suit, walking or at a roomless seat); without it no tree walk reaches P_k and X4 posts nothing on
 *     it, not even for slot k's own missions.
 * Eight of the routine's posts are answered (each call lies in one 16-byte block; the self cluster post at 0x6ced3f
 * crosses one, and no stock script listens for a player's cluster change): the entities' three (0x6cf155, 0x6cf200,
 * 0x6cf29a), the objects' three (0x6cef55, 0x6cf000, 0x6cf09a) and the moved object's sector and zone (0x6cedf6,
 * 0x6ceea7). On the frame thread, before X4's post (which may free the event):
 *   own    an entity post on P_k (k = 2..N): inside slot k's scope (opened when no scope is open and P_k is where its
 *          seat or walk says, as the scope's end checks), the event goes to the listeners of player 1's object that no
 *          slot owns, in the object form (pvehk_forward_from), in place mode: the scripts of one player space are not
 *          handed it, and what it starts is slot k's (lockstep_mission_owners.h 'a place event's first player');
 *   body   a post on the object P_k is in (the first object above P_k's parent pointers: its ship, its suit, the
 *          station it walks in) while P_k is in no children table: an event with X4's fields is posted on P_k
 *          (immediate, as X4 posts its own player's), inside slot k's scope, after the same forward. X4's post on that
 *          object comes first in its routine and P_k is not among the entities, so the event comes once.
 * X4's post then runs unchanged. The same on every client: the parent change is the simulation's, the scope is opened
 * by the same rules. Another slot's scope open, a scope that cannot open: nothing forwarded (counted); a body event is
 * still posted on P_k for its own listeners. Pinned (the routine's posting part, 0x6cecb9..0x6cf2ad); another build
 * keeps X4's posts. Stub 0xee0 on the slots' near page (free there now: 0xaa0..0xbff, 0xcc0..0xcff, 0xea0..0xedf).
 * Status place=installed/seen/own/body/forwarded/unscoped/denied. */
#define PVEHK_PLACE_PIN 0x6cecb9u
#define PVEHK_PLACE_PIN_SIZE 0x5f4u
#define PVEHK_PLACE_PIN_SHA "12f3cd069729aa08788d49b7cb1eef39e5e05c4e8f5dbeb88b01c8fc7ddb8842"
#define PVEHK_PLACE_STUB 0xee0u
#define PVEHK_PLACE_SITES 8u
#define PVEHK_EVENT_SECTOR 0x2ba9b30u    /* U::ChangedSectorEvent's vtable */
#define PVEHK_EVENT_ZONE 0x2ba9a88u      /* U::ChangedZoneEvent's */
#define PVEHK_EVENT_CLUSTER 0x2ba9af8u   /* U::ChangedClusterEvent's */
#define PVEHK_PLACE_OBJECT_CLASS 0x49u   /* X4's component class 'object' (IsClass, vtable +0x11e8) */
#define PVEHK_PLACE_IS_CLASS (0x11e8u/8u)
#define PVEHK_PLACE_UP 16u               /* parents from P_k up to its object */
static const unsigned pvehk_place_sites[PVEHK_PLACE_SITES]={0x6cf155u,0x6cf200u,0x6cf29au,0x6cef55u,0x6cf000u,0x6cf09au,0x6cedf6u,0x6ceea7u};
static unsigned char pvehk_place_patched[PVEHK_PLACE_SITES][5];
static int pvehk_place_installed;
static volatile LONG64 pvehk_place_seen,pvehk_place_own,pvehk_place_body,pvehk_place_forwarded,pvehk_place_unscoped;
/* tests: in place of X4's class test, the children table's answer and the two posts (X4's on its source, ours on P_k) */
static int (*pvehk_place_object_test)(const unsigned char *component);
static int (*pvehk_place_listed_test)(const unsigned char *player);
static uintptr_t (*pvehk_place_post_test)(void *source,void *event,double time,unsigned char immediate);
static void (*pvehk_place_tell_test)(unsigned char *player,const unsigned char *event);
/* Which place event this is (its vtable): 1 sector, 2 zone, 3 cluster; 0 another. */
static unsigned pvehk_place_kind(const unsigned char *event) {
    void *vt=NULL;
    if(!event || !read_local(event,&vt,8) || !vt || !pnat_base) return 0;
    return vt==(void*)(pnat_base+PVEHK_EVENT_SECTOR)?1u:vt==(void*)(pnat_base+PVEHK_EVENT_ZONE)?2u:vt==(void*)(pnat_base+PVEHK_EVENT_CLUSTER)?3u:0u;
}
static int pvehk_place_is_object(const unsigned char *component) {
    void **vt=NULL;
    if(pvehk_place_object_test) return pvehk_place_object_test(component);
    if(!component || !read_local(component,&vt,8) || !vt) return 0;
    return ((unsigned char(*)(const void*,unsigned))vt[PVEHK_PLACE_IS_CLASS])(component,PVEHK_PLACE_OBJECT_CLASS)!=0;
}
/* The object P_k is in: the first object above it by its parent pointers (+0x70); NULL none. */
static unsigned char *pvehk_place_carrier(const unsigned char *player) {
    unsigned char *at=NULL;
    if(!player || !read_local(player+0x70,&at,8)) return NULL;
    for(unsigned up=0;at && up<PVEHK_PLACE_UP;up++) {
        if(pvehk_place_is_object(at)) return at;
        if(!read_local(at+0x70,&at,8)) return NULL;
    }
    return NULL;
}
/* Slot k's scope for the forward: 1 opened here (pslot_leave after), 0 open already or none (pstk_scope_slot says). */
static int pvehk_place_enter(unsigned k) {
    unsigned char *parent=NULL;
    if(pstk_scope_slot) return 0;
    if(!read_local(pslots[k].player+0x70,&parent,8) || parent!=pslot_at(&pslots[k])) return 0;   /* the scope's end would refuse it */
    return pslot_enter(k);
}
static void pvehk_place_forward(unsigned k,unsigned char *event,double now) {
    pvehk_place_mode=1;pvehk_forward_from(k,pslot_p1,NULL,event,now);pvehk_place_mode=0;
    InterlockedIncrement64(&pvehk_place_forwarded);
}
/* own: X4's post on P_k itself. */
static void pvehk_place_own_note(unsigned k,unsigned char *event,double now) {
    InterlockedIncrement64(&pvehk_place_own);
    const int entered=pvehk_place_enter(k);
    if(pstk_scope_slot==k) pvehk_place_forward(k,event,now);else InterlockedIncrement64(&pvehk_place_unscoped);
    if(entered) (void)pslot_leave(k);
}
/* body: X4's post on the object P_k is in, while P_k is in no children table: the same event on P_k. */
static void pvehk_place_tell(unsigned k,const unsigned char *model,double now) {
    unsigned char *const player=pslots[k].player;uint64_t was=0,is=0;void *vt=NULL;
    if(!read_local(model,&vt,8) || !read_local(model+0x18,&was,8) || !read_local(model+0x20,&is,8)) return;
    InterlockedIncrement64(&pvehk_place_body);
    const int entered=pvehk_place_enter(k);
    __try {
        unsigned char *event=pvehk_place_tell_test?(unsigned char*)calloc(1,PVEHK_EVENT_SIZE):
            ((unsigned char*(*)(size_t,uintptr_t,uintptr_t,uintptr_t,unsigned))(void*)(pnat_base+PVEHK_EVENT_ALLOCATE))(PVEHK_EVENT_SIZE,0,0,0,0x10);
        if(event) {
            memset(event,0,PVEHK_EVENT_SIZE);memcpy(event,&vt,8);memcpy(event+0x18,&was,8);memcpy(event+0x20,&is,8);
            if(pstk_scope_slot==k) pvehk_place_forward(k,event,now);else InterlockedIncrement64(&pvehk_place_unscoped);
            if(pvehk_place_tell_test) {pvehk_place_tell_test(player,event);free(event);}
            else ((void(*)(void*,void*,double,unsigned char))(void*)(pnat_base+PVEHK_EVENT_POST_NOW))(player,event,now,1);
        }
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {pvehk_events_more=0;}
    if(entered) (void)pslot_leave(k);
}
static void pvehk_place_note(unsigned char *source,unsigned char *event,double now) {
    if(!pvehk_place_installed || !pvehk_forward_on || !source || !pslot_p1 || GetCurrentThreadId()!=ui_thread || !pvehk_place_kind(event)) return;
    InterlockedIncrement64(&pvehk_place_seen);
    for(unsigned k=2;k<=PILOT_SLOTS;k++) if(pslots[k].player==source) {pvehk_place_own_note(k,event,now);return;}
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        unsigned char *const player=pslots[k].player;
        if(!player || (!pvehk_events_more && !pvehk_place_tell_test) || pvehk_place_carrier(player)!=source) continue;
        if(pvehk_place_listed_test?pvehk_place_listed_test(player):pslot_listed(player)) continue;   /* X4 posts on it itself */
        pvehk_place_tell(k,event,now);
    }
}
static uintptr_t pvehk_place_post(unsigned char *source,unsigned char *event,double time,unsigned char immediate) {
    pvehk_place_note(source,event,time);
    if(pvehk_place_post_test) return pvehk_place_post_test(source,event,time,immediate);
    return ((uintptr_t(*)(void*,void*,double,unsigned char))(void*)(pnat_base+PVEHK_EVENT_POST_NOW))(source,event,time,immediate);
}
/* With the forward armed (pvehk_forward_arm): all eight sites or none. */
static void pvehk_place_install(void) {
    if(pvehk_place_installed || !pvehk_forward_on || !pslot_page || !hash_ok(pnat_base+PVEHK_PLACE_PIN,PVEHK_PLACE_PIN_SIZE,PVEHK_PLACE_PIN_SHA)) return;
    for(unsigned i=0;i<PVEHK_PLACE_SITES;i++) {
        if(pres_site_install(pvehk_place_sites[i],PVEHK_EVENT_POST_NOW,PVEHK_PLACE_STUB,(void*)pvehk_place_post,pvehk_place_patched[i])) continue;
        while(i--) (void)pres_site_uninstall(pvehk_place_sites[i],PVEHK_EVENT_POST_NOW,pvehk_place_patched[i]);
        return;
    }
    pvehk_place_installed=1;
}
static void pvehk_place_restore(void) {
    if(!pvehk_place_installed) return;
    pvehk_place_installed=0;   /* the note does nothing from here on */
    for(unsigned i=0;i<PVEHK_PLACE_SITES;i++) if(!pres_site_uninstall(pvehk_place_sites[i],PVEHK_EVENT_POST_NOW,pvehk_place_patched[i])) pvehk_place_installed=1;
}
static int pvehk_place_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity," place=%d/%lld/%lld/%lld/%lld/%lld/%lld",pvehk_place_installed,(long long)pvehk_place_seen,(long long)pvehk_place_own,
        (long long)pvehk_place_body,(long long)pvehk_place_forwarded,(long long)pvehk_place_unscoped,(long long)pvehk_place_denied);
    return n>0 && (size_t)n<capacity?n:0;
}
static void pvehk_event_post(unsigned char *player,unsigned vtable,uint64_t was,uint64_t is,double now) {
    if(!was || !is || was==is) return;
    pvehk_event_raw(player,vtable,PVEHK_EVENT_SIZE,was,is,now);
}
/* A component's ID and class as a control event carries them (0 and 0x79 for none, as X4 writes them). */
static void pvehk_control_of(unsigned char *object,uint64_t *id,unsigned *cls) {
    void **vtable=NULL;
    *id=0;*cls=0x79u;
    if(!object || !read_local(object+8,id,8) || !*id || pvehk_event_test) return;
    __try {if(read_local(object,&vtable,8) && vtable) *cls=((unsigned(*)(void*))vtable[PVEHK_EVENT_CLASS_SLOT])(object);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {*cls=0x79u;}
}
/* Slot k's player took (started) or left the controls of the object with that ID and class. */
static void pvehk_control_id(unsigned k,int started,uint64_t id,unsigned cls) {
    double now=0;
    if((!pvehk_events_more && !pvehk_event_test) || k<2 || k>PILOT_SLOTS || !pslots[k].player || !id || !pnat_fn.time || !pnat_fn.time(&now)) return;
    pvehk_event_now(k,started?PVEHK_EVENT_CONTROL_ON:PVEHK_EVENT_CONTROL_OFF,PVEHK_EVENT_SIZE,id,cls,now);
}
/* ...of the ship its seat is in now. */
static void pvehk_control(unsigned k,int started) {
    uint64_t id=0;unsigned cls=0x79u;
    if(k<2 || k>PILOT_SLOTS || !pslots[k].seated) return;
    pvehk_control_of(pslots[k].ship,&id,&cls);pvehk_control_id(k,started,id,cls);
}
/* The IDs of slot k's ship and cockpit room as its seat has them now (0: none). */
static void pvehk_where(unsigned k,uint64_t *object,uint64_t *room) {
    const pslot_slot *x=&pslots[k];
    *object=*room=0;
    if(x->seated && x->ship) (void)read_local(x->ship+8,object,8);
    if(x->seated && x->room) (void)read_local(x->room+8,room,8);
}
/* After slot k's seat changed (its suit, its ship again): what MD hears of it. The object and the room, then (X4's
 * order for its own player) the teleport into the new room. */
static void pvehk_changed(unsigned k,uint64_t old_object,uint64_t old_room) {
    uint64_t object=0,room=0;double now=0;
    if((!pvehk_events_on && !pvehk_event_test) || !pslots[k].player || !pnat_fn.time || !pnat_fn.time(&now)) return;
    pvehk_where(k,&object,&room);
    pvehk_event_post(pslots[k].player,PVEHK_EVENT_OBJECT,old_object,object,now);
    pvehk_event_post(pslots[k].player,PVEHK_EVENT_ROOM,old_room,room,now);
    if((pvehk_events_more || pvehk_event_test) && old_room && room && old_room!=room)
        pvehk_event_now(k,PVEHK_EVENT_TELEPORT,PVEHK_EVENT_TELEPORT_SIZE,old_room,room,now);
}
static void pvehk_enter_install(void) {
    if(pvehk_enter_installed || !pslot_page || !hash_ok(pnat_base+PVEHK_ENTER,PVEHK_ENTER_SIZE,PVEHK_ENTER_SHA)) return;
    pvehk_find=hash_ok(pnat_base+PVEHK_CONNECTION_FIND,PVEHK_CONNECTION_FIND_SIZE,PVEHK_CONNECTION_FIND_SHA)?
               (pvehk_find_fn)(void*)(pnat_base+PVEHK_CONNECTION_FIND):NULL;
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
    pvehk_events_pins();     /* MD's object and room events for a slot's player */
    event_order_scope_enter=pvehk_deliver_enter;event_order_scope_leave=pvehk_deliver_leave;   /* ...and its due events in its scope */
    event_order_scope_forward=pvehk_deliver_forward;
    pvehk_enter_install();   /* X4's entry of a suited player (above): where this image has it */
    pvehk_fade_install();    /* the fade of an exit or an entry: on the client whose player it is */
    pvehk_menu_install();    /* the menu of a seat change: on the client whose player it is */
    pvehk_zone_install();    /* a flying slot's reference frame: its own environment zone */
    pvehk_did_install();     /* what a slot's ship did (kill, repair, collect, defuse): told to that slot's player */
    pvehk_scan_install();    /* a slot's player's scan and unlock progress: inside its scope */
    pvehk_scan_posts_install();   /* ...and its posts also to the scripts nobody owns */
    pvehk_computer_install();     /* a slot's ship computer's autopilot signals also to the scripts nobody owns (after the UI replay's pins) */
    pdeath_guard_installed=1;return 1;
}
static void pdeath_restore(void) {
    event_order_scope_enter=NULL;event_order_scope_leave=NULL;event_order_scope_forward=NULL;
    pvehk_due_restore();
    pvehk_place_restore();
    pvehk_computer_restore();
    pvehk_scan_posts_restore();
    pvehk_scan_restore();
    pvehk_did_restore();
    pvehk_zone_restore();
    pvehk_menu_restore();
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
    if(k==1) return pfoot_armed && pfoot_state!=PFOOT_SEATED && pfoot_state!=PFOOT_DOCKED && !controller;   /* docked: aboard, in its seat */
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
static unsigned char *pveh_ship_from(unsigned char *c);
static unsigned char *pveh_ship_around(const unsigned char *player) {
    unsigned char *c=NULL;
    if(pveh_around_test) return pveh_around_test(player);
    if(!player || !read_local(player+0x70,&c,8)) return NULL;
    return pveh_ship_from(c);
}
/* c or its first ancestor of the ship class. */
static unsigned char *pveh_ship_from(unsigned char *c) {
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
 * Left out: X4's 4e5880 for each weapon the tables left ungrouped (it puts the weapon into the default groups; MD's
 * reset_weapongroups did so for the suit already) and its 81e0d0 step for class 7 weapons. Status airlock=.../weapons grouped/
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
static int ptele_undock_key(unsigned k);   /* lockstep_pilot_teleport.h: UNDOCK on foot, as X4's 71b190 */
/* Slot k's replayed action key (lockstep_pilot_stock.h) or dock request (lockstep_ui_replay.h): 1 = it is this module's
 * (the caller leaves X4's handler out). In its suit GET_UP is void and the dock keys are X4's (0: the caller runs its
 * handler in the slot's scope; the tick is noted). Walking, the dock keys are X4's own player's on foot (24 Nov; until
 * then they took the slot out through its ship's airlock into a spacesuit, a stopgap: Shift+D on foot put player 2
 * into a spacesuit): UNDOCK (0xd3) into its own ship docked where it stands, as X4's 71b190 (lockstep_pilot_teleport.h
 * ptele_undock_key); DOCK_ACTION (0xaf) and a replayed dock request nothing: X4's dock core (730ff0) asks for the ship
 * the player controls, and a walking player controls none (for a slot's player X4's handler would find the seat's
 * kept vector instead: never run). The spacesuit is the transporter menu's (lockstep_ui_compslot.h 'terminal').
 * GET_UP while walking stays the sit-down (lockstep_pilot_foot_slots.h). */
static int pvehk_home_key(unsigned k,unsigned id) {
    if(id!=PFOOT_GET_UP && id!=0xafu && id!=0xd3u) return 0;
    if(pvehk_suited(k)) {
        if(id==PFOOT_GET_UP) return 1;
        pdeath_slots[k-1].dock_tick=stats.issued;++pvehk_far;return 0;
    }
    if(id!=PFOOT_GET_UP && k>=2 && k<=pilot_slot_count && pslots[k].seated && pslots[k].walking) {
        return id==0xd3u?ptele_undock_key(k):1;
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
/* standing: the seat is the board's (lockstep_pilot_teleport.h), stood up at once: no flight control edge. */
static int pvehk_seat_as(unsigned k,uint64_t runtime,const char *event,int standing) {
    pilot_slot *s=&pilot_slots[k-1];
    if(pfire_armed) {(void)pfire_end(s,&pfire_slots[k-1],k);memset(&pfire_slots[k-1],0,sizeof(pfire_slots[k-1]));}
    s->runtime=runtime;s->reversed=1;s->renewed=1;s->dead=0;s->seen=0;
    if(!pilot_resolve(s,pdeath_find,pdeath_map,pdeath_player)) return pdeath_fail("vehicle_unresolved");
    pstk_slot_frames[k-1].flight=0;pstk_slot_frames[k-1].flight_known=1;
    if(!pslot_seat(k)) {pdeath_why=pslot_why;return 0;}
    pfbody_place_seat(k);   /* P_k's place for MD from this tick on (its body, where the seat has one, comes later) */
    /* The local controls stayed active through the change: the edge the new flight control needs. */
    if(!standing && pstk_capturing && pstk_capture_slot==k && pstk_local_active==1) pstk_record_event(PSTK_SPACE,8,1u);
    pdeath_event(event,k);
    return 1;
}
static int pvehk_seat(unsigned k,uint64_t runtime,const char *event) {return pvehk_seat_as(k,runtime,event,0);}
/* The ship X4 put slot k's suited player into, when that is another ship than the one it left (header, X4's own entry):
 * the room's ship (its first ancestor of the ship class), not the suit, not under it, not a ship of any slot (slot 1's
 * vehicle included: its player sits there). NULL: none of these. X4's entry from its player's update (0x731560 at
 * 0x7315d5, once the suit has the dock it asked for: within 0.9 of a dock's range 0x7113e0 asks the ship, 0x7d03e0)
 * names no room: then X4's own target stands for it (pvehk_entry_target, noted at the call). */
static void *uitrade_owner(void *object);   /* lockstep_ui_trade.h */
static int (*pvehk_owner_test)(unsigned char *ship);   /* tests: 1 = the slot's faction owns it */
/* A ship of another slot than k (its seat's ship, or the vehicle the session gave it; slot 1's included): two players at
 * one ship's controls are none of X4's. The board (lockstep_pilot_teleport.h) does not go into one either. */
static int pvehk_others_ship(unsigned k,unsigned char *ship) {
    uint64_t id=0;
    if(!ship || !read_local(ship+8,&id,8) || !id) return 0;
    for(unsigned j=1;j<=pilot_slot_count && j<=PILOT_SLOTS;j++) {
        if(j==k) continue;
        if(pilot_slots[j-1].resolved && pilot_slots[j-1].runtime==id) return 1;
        if(j>=2 && pslots[j].ship==ship) return 1;
    }
    return 0;
}
static unsigned char *pvehk_aboard_of(unsigned k,unsigned char *ship,const unsigned char *home) {
    unsigned char *const suit=pslots[k].ship;uint64_t id=0;
    if(!ship || ship==home || !suit || ship==suit || uics_under(ship,suit) || !read_local(ship+8,&id,8) || !id) return NULL;
    /* the slot's own faction's only: X4 lets a suit into an NPC's ship too (crash rk9-suit), where its player stands */
    if(!pvehk_owner_test && uitrade_owner(ship)!=uitrade_owner(suit)) return NULL;
    if(pvehk_owner_test && !pvehk_owner_test(ship)) return NULL;
    for(unsigned j=1;j<=pilot_slot_count;j++) {
        if(pilot_slots[j-1].resolved && pilot_slots[j-1].runtime==id) return NULL;
        if(j>=2 && pslots[j].ship==ship) return NULL;
    }
    if(pslot_p1 && uics_under(pslot_p1,ship)) return NULL;
    return ship;
}
static unsigned char *(*pvehk_aboard_test)(unsigned k,unsigned char *room);   /* tests: in place of the answer */
static unsigned char *pvehk_aboard(unsigned k,unsigned char *room,const unsigned char *home) {
    if(pvehk_aboard_test) return pvehk_aboard_test(k,room);
    return room?pvehk_aboard_of(k,pveh_ship_from(room),home):NULL;
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
        uint64_t left_object=0,left_room=0;pvehk_where(k,&left_object,&left_room);
        pfslot_lose(k);                                                 /* the walk ends where it is */
        if(!pfbody_release_slot(k)) {pdeath_why=pfbody_why;return 0;}   /* none while it walks (parked); for the order */
        if(!pslot_release(k)) {pdeath_why=pslot_why;return 0;}          /* the seat's pointers back */
        pvehk_equip(k,suit);                                            /* its weapon groups, as X4's eject restores them */
        if(!pvehk_seat(k,d->suit,"suited")) return 0;
        pvehk_changed(k,left_object,left_room);                         /* MD: its player changed object and room */
        pvehk_control(k,1);                                             /* ...and took the suit's controls */
        d->home=home;d->out=2;d->ask=d->ranged=0;d->dock_tick=0;d->arrive=0;d->arrive_room=NULL;++pvehk_outs;
        return 1;
    }
    /* In its suit: home once X4 asked to put its player into the ship it left (pvehk_enter), or docked the suit there
     * (the suit under that ship); aboard another ship once X4 asked to put it there (pvehk_aboard). */
    /* The board's way (lockstep_pilot_teleport.h): X4 decides the entry into any ship, its move is the board: on foot
     * where X4 puts its own player, as player 1 lands (the old way below seats the slot at once; without the board's way
     * only). */
    if(ptele_board_on()) {d->arrive=0;d->arrive_room=NULL;return ptele_board(k);}
    unsigned char *home=d->home?pdeath_find(pdeath_map,d->home,4):NULL,*parent=NULL,*suit=x->ship,*aboard=NULL;void **vtable=NULL;double now=0;
    int arrived=0;uint64_t to=d->home;
    if(d->arrive) {
        /* Without a room X4 names the ship the suit is docked at: home only where the suit is under it (below). Until
         * 24 Nov an arrival without a room was home whatever ship took the suit (crash rk9-suit: an NPC capital ship). */
        unsigned char *room=d->arrive_room;
        d->arrive=0;d->arrive_room=NULL;
        arrived=home && room && uics_under(room,home);
        if(!arrived && x->seated && (aboard=pvehk_aboard(k,room,home))) (void)read_local(aboard+8,&to,8);
        else if(!arrived && room) InterlockedIncrement64(&pvehk_enters_else);
    }
    if(aboard) {if(!x->seated || !suit) return 1;}
    else if(!home || !x->seated || !suit || !read_local(suit+0x70,&parent,8) || !(arrived || uics_under(parent,home))) return 1;
    /* The body off the suit first. A spacesuit HAS a cockpit room, and once everyone sits the slot's body is connected
     * there (lockstep_pilot_foot_body.h, PFBODY_SETTLE ticks after the seat). The suit's kill below then walked into P_k:
     * X4 killed the slot's player with its suit and never came out of the teardown (live 12 Nov, both clients spinning in
     * one tick after the home key: SpaceSuit and its Cockpit dead, P_2 dying with no parent, an MD action at the root).
     * Earlier runs went home before the body had settled. */
    uint64_t left_object=0,left_room=0;unsigned left_class=0x79u;pvehk_where(k,&left_object,&left_room);
    {uint64_t id=0;pvehk_control_of(suit,&id,&left_class);}
    if(!pfbody_release_slot(k)) {pdeath_why=pfbody_why;return 0;}
    if(!pslot_release(k)) {pdeath_why=pslot_why;return 0;}
    if(pnat_fn.time(&now) && read_local(suit,&vtable,8) && vtable) ((hitn_kill_fn)vtable[PDEATH_KILL_SLOT])(suit,PDEATH_KILL_REMOVED,NULL,now);
    {   const int untaken=pfslot_armed && pfslot_ship_docked(aboard?aboard:home);   /* a docked or stored ship: the docked seat */
        pslot_seat_untaken=untaken;
        const int seated=pvehk_seat(k,to,aboard?"aboard":"home");
        pslot_seat_untaken=0;
        if(!seated) return 0;
    }
    pvehk_changed(k,left_object,left_room);
    pvehk_control_id(k,0,left_object,left_class);pvehk_control(k,1);   /* off the suit's controls, at its ship's (the arrival is the seat) */
    d->out=0;d->home=0;d->suit=0;d->ask=d->ranged=0;d->dock_tick=0;d->arrive=0;d->arrive_room=NULL;
    if(aboard) InterlockedIncrement64(&pvehk_aboards);else ++pvehk_homes;
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
    uint64_t left_object=0,left_room=0;unsigned left_class=0x79u;pvehk_where(k,&left_object,&left_room);
    {uint64_t id=0;pvehk_control_of(suit,&id,&left_class);}
    if(!pfbody_release_slot(k)) {pdeath_why=pfbody_why;return -1;}
    if(read_local(suit+PDEATH_LIFECYCLE,&lifecycle,1) && !lifecycle && pslot_controller_has(suit,0,x->controller)==1) {
        if(!pslot_release(k)) {pdeath_why=pslot_why;return -1;}
        if(pnat_fn.time(&now) && read_local(suit,&vtable,8) && vtable) ((hitn_kill_fn)vtable[PDEATH_KILL_SLOT])(suit,PDEATH_KILL_REMOVED,NULL,now);
    } else pslot_retire(k);
    if(!pvehk_seat(k,d->home,"home")) return -1;
    pvehk_changed(k,left_object,left_room);
    pvehk_control_id(k,0,left_object,left_class);pvehk_control(k,1);
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
        /* X4's entry check, where its own player's byte keeps it off: a slot in its suit. (A slot's ship at its dock is
         * docked by the same step from the slot's own Move, which runs inside its scope: lockstep_pilot_foot_body.h.) */
        if(k>=2 && !s->dead && !d->lost && d->out==2) pvehk_dock_poll(k);
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
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," taken=%llu enter=%d/%lld/%lld/%lld/%lld/%lld/%lld/%lld",
                                              (unsigned long long)pvehk_taken_in,pvehk_enter_installed,(long long)pvehk_enters,(long long)pvehk_enters_else,
                                              (long long)pvehk_aboards,(long long)pvehk_enters_x4,(long long)pvehk_board_no_home,
                                              (long long)pvehk_enters_unscoped,(long long)pvehk_board_others);
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," fade=%d/%lld/%lld",pvehk_fade_installed,
                                              (long long)pvehk_fades_shown,(long long)pvehk_fades_kept);
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," menu=%d/%lld/%lld",pvehk_menu_installed,
                                              (long long)pvehk_menus_shown,(long long)pvehk_menus_kept);
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," events=%d/%lld/%d/%lld deliver=%lld/%lld/%lld/%lld kill=%d/%lld leak=%d/%lld did=%d/%d/%lld seen=",pvehk_events_on,(long long)pvehk_events_posted,pvehk_events_more,(long long)pvehk_events_unscoped,
                                              (long long)pvehk_delivered_scoped,(long long)pvehk_delivered_plain,(long long)pvehk_delivered_inside,(long long)pvehk_delivered_unseated,
                                              pvehk_did_sites[PVEHK_DID_KILL].installed,(long long)pvehk_did_sites[PVEHK_DID_KILL].told,
                                              pvehk_did_sites[PVEHK_DID_LEAK].installed,(long long)pvehk_did_sites[PVEHK_DID_LEAK].told,
                                              pvehk_did_installed(),(int)PVEHK_DID_SITES,pvehk_did_told());
    if(n>0 && (size_t)n<capacity) n+=pvehk_did_seen(out+n,capacity-(size_t)n);
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," due=%d/%lld/%s shared=%d/%lld/%lld/%lld/%lld",pvehk_due_installed,(long long)pvehk_due_batched,pvehk_due_why,pvehk_forward_on,(long long)pvehk_forward_events,
                                              (long long)pvehk_forward_calls,(long long)pvehk_forward_owned,(long long)pvehk_forward_over);
    if(n>0 && (size_t)n<capacity) {const int more=pvehk_computer_status(out+n,capacity-(size_t)n);if(!more) return 0;n+=more;}
    if(n>0 && (size_t)n<capacity) {const int more=pvehk_place_status(out+n,capacity-(size_t)n);if(!more) return 0;n+=more;}
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," scan=%d/%lld/%lld scanposts=%d/%u/%lld",pvehk_scan_installed,(long long)pvehk_scans_scoped,
                                              (long long)pvehk_scans_plain,pvehk_scan_posts_installed,PVEHK_SCAN_POSTS,(long long)pvehk_scan_posts_forwarded);
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

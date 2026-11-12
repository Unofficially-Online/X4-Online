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
    unsigned ask,ranged;double range;   /* in its suit: the way home asked for (a key); the probe's distance to the home ship */
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
static int pdeath_guard_install(void) {
    if(pdeath_guard_installed) return 1;
    if(!pslot_page && !(pslot_page=pslot_alloc_page())) return 0;
    if(!pres_site_install(PDEATH_GAME_OVER_CALL,PDEATH_GAME_OVER,PDEATH_STUB,(void*)pdeath_game_over,pdeath_guard_patched)) return 0;
    pdeath_guard_installed=1;return 1;
}
static void pdeath_restore(void) {
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
 * loss on foot), walks, and the next seat X4 gives its player is adopted. Events suit1 / seat1 / left1 in
 * PILOT_DEATH's frame; status vehicle=adopts/lefts/in_suit. Slots 2..N: not yet (their player is not X4's own). */
#define PVEH_WINDOW 1800u
#define PVEH_SUIT_VTABLE 0x2b789b0u            /* U::SpaceSuit */
static uint64_t pveh_allow_until,pveh_adopts,pveh_lefts;
static int pveh_in_suit;
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
/* Slot 1's suit is gone and the player lives: it went with the player's teleport into a ship. 1: handled here. */
static int pveh_left(pilot_slot *s,pdeath_slot *d,uint64_t tick) {
    unsigned char life=1;
    if(!pveh_in_suit || s->dead || d->lost || !pdeath_dying(s)) return 0;
    if(!pdeath_player || !read_local((const unsigned char *)pdeath_player+PDEATH_LIFECYCLE,&life,1) || life) return 0;   /* the player goes too: a death */
    pveh_in_suit=0;++pveh_lefts;
    if(pfire_armed) {(void)pfire_end(s,&pfire_slots[0],1);memset(&pfire_slots[0],0,sizeof(pfire_slots[0]));}
    d->lost=1;d->ship=0;   /* no ship until X4 seats its player again (pveh_adopt_ok: any seat) */
    pnat_ship=NULL;pnat_controller=NULL;
    if(pfoot_armed) {pfoot_state=PFOOT_WALKING;pfoot_transition_tick=tick;}
    pdeath_event("left1",1);
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
 * The way back: the slot asks for it in its suit with GET_UP or a dock key or the menu's dock request (its record and its
 * UI events, so every client sees the same ask at the same tick; none of them reaches X4's handlers for a suited slot)
 * while its suit is within PVEHK_HOME_RANGE of the ship it left, or the suit is under that ship (docked at it: the suit's
 * parent chain reaches the ship). The distance is the probe's, from the slot ships' positions it declares every tick
 * (gate.pilot_death_home / pilot_death_range), not a read of this module: the same value on every client. Then the seat
 * in the suit is released, the suit killed as X4 kills its player's old suit (reason 13), and the slot is seated in its
 * home ship (home:k). Too far: nothing (far:k). A home ship that is gone leaves the slot in its suit. Needs
 * native_gate_pilot_death_suit. Status airlock=outs/homes/far, d<k>=.../out<state>. Not yet: a hot join while a slot is
 * out (its state is not in the join's tables), a suit that can die (minimum hull as the death suit's), oxygen. */
#define PVEHK_HOME_RANGE 1000.0
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
 * (the caller leaves X4's handler out). In its suit GET_UP and the dock keys ask for the way home. Walking in its own
 * ship the dock keys (DOCK_ACTION 0xaf, UNDOCK 0xd3) are the way out: the airlock without its console. On client k the
 * on-foot click does not reach X4's pick yet (the user's session of 11 Nov 20:25: client 2's clicks arrived as the
 * ship's TARGETMOUSE_SELECT 0x82), and a walking slot's dock key has no business with X4's dock handler anyway (nobody
 * is at its ship's controls). GET_UP while walking stays the sit-down (lockstep_pilot_foot_slots.h). */
static int pvehk_home_key(unsigned k,unsigned id) {
    if(id!=PFOOT_GET_UP && id!=0xafu && id!=0xd3u) return 0;
    if(pvehk_suited(k)) {pdeath_slots[k-1].ask=1;return 1;}
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
        d->out=0;d->home=0;d->ask=d->ranged=0;return 1;
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
        d->home=home;d->out=2;d->ask=d->ranged=0;++pvehk_outs;
        return 1;
    }
    /* In its suit: home when it asked near the ship it left, or once the suit is under that ship. */
    unsigned char *home=d->home?pdeath_find(pdeath_map,d->home,4):NULL,*parent=NULL,*suit=x->ship;void **vtable=NULL;double now=0;
    int at_home=0;
    if(d->ask) {
        d->ask=0;at_home=home && d->ranged && d->range<=PVEHK_HOME_RANGE;
        if(!at_home) {++pvehk_far;pdeath_event("far",k);}
    }
    if(!home || !x->seated || !suit || !read_local(suit+0x70,&parent,8) || !(at_home || uics_under(parent,home))) return 1;
    /* The body off the suit first. A spacesuit HAS a cockpit room, and once everyone sits the slot's body is connected
     * there (lockstep_pilot_foot_body.h, PFBODY_SETTLE ticks after the seat). The suit's kill below then walked into P_k:
     * X4 killed the slot's player with its suit and never came out of the teardown (live 12 Nov, both clients spinning in
     * one tick after the home key: SpaceSuit and its Cockpit dead, P_2 dying with no parent, an MD action at the root).
     * Earlier runs went home before the body had settled. */
    if(!pfbody_release_slot(k)) {pdeath_why=pfbody_why;return 0;}
    if(!pslot_release(k)) {pdeath_why=pslot_why;return 0;}
    if(pnat_fn.time(&now) && read_local(suit,&vtable,8) && vtable) ((hitn_kill_fn)vtable[PDEATH_KILL_SLOT])(suit,PDEATH_KILL_REMOVED,NULL,now);
    if(!pvehk_seat(k,d->home,"home")) return 0;
    d->out=0;d->home=0;d->suit=0;d->ask=d->ranged=0;++pvehk_homes;
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
        if(k==1 && pveh_left(s,d,tick)) continue;       /* its spacesuit went with a teleport into a ship: no death */
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
    pdeath_guard_installed=0;pdeath_game_overs=0;pdeath_released=pdeath_retired=0;pvehk_outs=pvehk_homes=pvehk_far=0;pvehk_weapons=0;pvehk_equip_faults=0;
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
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," vehicle=%llu/%llu/%d",(unsigned long long)pveh_adopts,
                                              (unsigned long long)pveh_lefts,pveh_in_suit);   /* slot 1: adopted vehicles, suits left, in a suit */
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," airlock=%llu/%llu/%llu/%llu/%u",(unsigned long long)pvehk_outs,
                                              (unsigned long long)pvehk_homes,(unsigned long long)pvehk_far,(unsigned long long)pvehk_weapons,
                                              pvehk_equip_faults);   /* slots 2..N: into a suit, home again, asked too far away; suit weapons installed, faults */
    return n>0 && (size_t)n<capacity?n:0;
}

/* Slots 2..N in X4's world tree, stage 1: a body at the seat (native_gate_pilot_foot_bodies = true, DEFAULT OFF;
 * needs native_gate_pilot_foot and stock slots 2..N; stop reason 185, why body_*). Report:
 * reports/LOCKSTEP-ON-FOOT-20261101.txt section 8, with the death module section 13.
 *
 * P_k (lockstep_pilot_slots.h) is X4's own U::Player built by the factory, seated through upward pointers only: its
 * parent +0x70 is slot k's cockpit room, but the room's child list does not hold it, so nothing that walks the tree
 * (the render walks, the census, the room's own walks) reaches it, and no client can draw slot k. X4 seats its own
 * player with 716670 [F]: 3d8420(player, &filter 2f60e70 = TemplateConnectionFilterByIsPlayerSlot, room, 0), i.e.
 * U::Component::ConnectTo (3d6e50) at the room's player slot connection, then the control position and 70eee0 (its
 * FirstPersonController). 3d8420 with no target is 3d8240(component, 0x15): off its parent with no kill of its own
 * [F]; the parent change behind it kills an unheld Player left without a parent, and a Player's first connect gives it
 * a camera of its own: both kept out by pfbody_call (below, after the kill guard's paragraph).
 *
 * This stage connects P_k the same way, on every client, at the same step:
 *   take-1   after pslot_seat_all (the seat's upward pointers set): in scope k (singleton = P_k), the upward
 *            pointer cleared, 3d8420(P_k, filter, room, 0) held and with the stand-in camera (pfbody_call); P_k's
 *            parent must then be the room (else off again,
 *            the upward pointer back, stop body_connect). A roomless seat (no cockpit room) gets no body (roomless=).
 *   live     every pass: each body still has the room as its parent (body_moved), its seat still holds.
 *   stop+1   before the seats' release: in scope k, 3d8420(P_k, filter, NULL, 0) held, the upward pointer back (the seat's
 *            state its release expects); an abnormal stop does the same on the frame thread outside any scope.
 * Nothing else: no FirstPersonController for P_k yet (70eee0), no control position change, no walking. P_k sits where
 * X4 seats a pilot. What this answers live (report section 9, B1): whether a U::Player that is not the singleton is
 * drawn (client 1 sees a figure in slot 2's cockpit; client 2 may see its own head from inside: the presented eye is
 * at the same seat), whether a second Player in a room's child list keeps the census and receipts equal, and what X4
 * does with it (events, attention).
 *
 * A ship's death [F, 9.00]: the hull at zero (518210) kills the ship at once: U::Component::KillRecursive 3e5240 asks
 * each component it reaches whether to spare it (vtable +0x1408, 3e525b; a Player answers 0), then marks it dying
 * (+0xd3), tells its killer (vfunc680), posts its KilledEvent with immediate dispatch (99a3f0, r9b 1: a Player's event
 * handler vfunc9 7046e0 with itself is X4's game over) and walks its children (+0xa8). A body in the cockpit room is
 * one of them, long before any pass of ours. So the call at 3e525b is re-pointed while bodies are armed (the kill
 * guard, take-1 to stop+1, slots' near page +0xc0): asked about a slot's seat ship or cockpit room, the guard takes
 * that slot's body off the room in its scope first (as X4 ejects its own player before the kill), then answers what
 * X4 answers; asked about a connected body (no scope could open: another thread, a scope open, inside d95180), it
 * spares it (1: KillRecursive returns at once) and the death pass takes it off before it ends the seat (counted late).
 * The death module also takes a body off before any seat it ends; a dead slot, one in its suit, a walking one and a
 * roomless seat have no body; a seat a respawn gives gets its body PFBODY_SETTLE ticks later, while nobody is up.
 *
 * Parked while anyone is off its seat (live footlook, smoke d0d50e95: bodies + slot 1's GET_UP at 2400 ended both X4s
 * with 0xC0000005 in the frame of the stand-up, 2403, right after 70e0f0's last log line; without bodies the same
 * stand-up walked, w1bfoot/s1k1). Which X4 code faults was not found offline [I: a walk of the tree that reaches P_k
 * once the player is on foot; P_k is a factory player whose helmet controller (Player::vfunc578, 6fb230) poses from the
 * singleton's PlayerController or FPC (d9f190 -> 9fc550 / 9fc670), and slot 1's controllers change during 716e20].
 * Until then every body leaves the tree in the pass that replays a get-up (slot 1's GET_UP: pfoot_state leaving; slot
 * k's: lockstep_pilot_foot_slots.h), before X4's event phase stands anyone up, and comes back PFBODY_SETTLE ticks after
 * everyone sits again: the tree only ever holds P_k in the state B1 ran (both seated). Status bodies=parked parks=.
 *
 * What X4's connect and disconnect do to a Player besides the tree [F, pinned 9.00; reports/LOCKSTEP-ON-FOOT-20261101.txt
 * section 13g-13i], and why every call of 3d8420 here goes through pfbody_call:
 *   removed  3d8420 without a target is 3d8240(P_k, 0x15): no kill there, but Player::vfunc 0x1380 (702f40) -> 553660 ->
 *            748790 -> U::Component's parent change 3d7010, which kills a component left with no parent (3d7253 -> 3d7830)
 *            that was in the tree, is alive (+0xd3) and has no hold (+0xd4 == 0, 3d7851): Kill(component, 13 'removed')
 *            at 3d78b7, a deferred KillEvent, KillRecursive, the KilledEvent, Player::vfunc9 with itself: "Game Over -
 *            Player has died (killmethod=removed)". So every body that left its room was killed: at stop+1 (live d1s:
 *            stop 163 player_killed in the teardown, no death), at each park (live df2 at slot 1's GET_UP), at the kill
 *            guard and the death module's take-off. +0xd4 is X4's own hold count (MD's cue actors take one, 3e5680 drops
 *            one and kills the same way at 0): P_k holds one for the length of the call, so X4 takes it out of the tree
 *            as it moves a held component, and nothing else changes (the count is back to what it was afterwards).
 *   camera   the first connect of a Player without a camera (+0x4e8 NULL; 703051) runs 71a560: a new camera for it,
 *            Universe+0x3c8 and +0x3d0 (X4's camera on this client) set to it (97df60, 71a744), camera_anchor_scene moved
 *            under it, and a cockpit head controller made by d895a0 for 9fc550's PlayerController: in scope k that is C_k.
 *            Live d1s/df2: from take-1 every client's camera was P_2's (BULLET_CADENCE camera=0:<P_2>). X4 drops a head
 *            controller only from the camera at +0x3d0 when a PlayerController dies (d84730 -> d89840), so when X4 stood
 *            slot 1 up (its PlayerController deleted) the head controller of slot 1's own camera stayed bound to the freed
 *            controller, and the camera loop's next refill (9fe573 -> 979ce0 -> 9799f0 -> 97a300 -> 988bac) read it:
 *            0xC0000005 at ea75e1 on both clients (df2, tick 3803; footlook in section 12c). P_k holds a zeroed stand-in
 *            camera for the length of the call, so 702f40 skips 71a560; +0x4e8 is NULL again afterwards.
 * pfbody_call fails loudly (stop 185) when X4 did any of it anyway: P_k dying (body_killed), the Universe's cameras or the
 * stand-in changed (body_camera), the hold count moved (body_hold). */
#define PFBODY_CONNECT 0x3d8420u            /* 3d8420(component, filter, target, 0) */
#define PFBODY_HOLD 0xd4u                   /* Component: X4's hold count (3d7851 reads it before the 'removed' kill) */
#define PFBODY_LIFECYCLE 0xd3u              /* Component: 1 dying, 2 dead */
#define PFBODY_CAMERA 0x4e8u                /* Player: its camera (702f40 makes one at 71a560 when NULL) */
#define PFBODY_CAMERAS 0x3c8u               /* Universe: +0x3c8 and +0x3d0, the cameras 71a560/97df60 set to the new one */
#define PFBODY_CAMERA_SIZE 0x920u           /* a camera's allocation in 71a560 */
static __attribute__((aligned(16))) unsigned char pfbody_no_camera[PFBODY_CAMERA_SIZE];   /* the stand-in: zero, owned by nobody */
static volatile LONG64 pfbody_held;         /* X4 calls made with P_k held and its stand-in camera */
#define PFBODY_FILTER 0x2f60e70u            /* the filter object 716670 passes (lea rdx at 71673b) */
#define PFBODY_FILTER_VTABLE 0x2b9ec18u     /* U::TemplateConnectionFilterByIsPlayerSlot */
typedef void (*pfbody_connect_fn)(void *,const void *,void *,void *);
static pfbody_connect_fn pfbody_connect;
static const void *pfbody_filter;
static int pfbody_armed,pfbody_failed,pfbody_pins_checked;
static unsigned pfbody_mask;                /* bit k-1: P_k is a child of its seat's room now */
static const char *pfbody_stage="off",*pfbody_why="-";
static volatile LONG64 pfbody_connects,pfbody_disconnects,pfbody_checks,pfbody_parks,pfbody_unparks;
#define PFBODY_SETTLE 24u                   /* ticks everyone sits before the bodies come back */
static int pfbody_parked;                   /* every body off the tree while anyone is off its seat */
static uint64_t pfbody_seated_since;
static uint64_t pfbody_back[PILOT_SLOTS+1]; /* since when slot k's seat is a body seat again without its body (a respawn) */
static volatile LONG64 pfbody_returns,pfbody_roomless;
static int pfslot_any_walking(void);static int pfslot_getup_due(uint64_t tick);   /* lockstep_pilot_foot_slots.h */
#define PFBODY_KILL_RVA 0x3e5240u           /* U::Component::KillRecursive */
#define PFBODY_KILL_SITE 0x3e525bu          /* its first call: call [rax+0x1408], spare this one (1: return at once) */
#define PFBODY_KILL_SLOT (0x1408u/8u)
#define PFBODY_KILL_STUB 0xc0u              /* on the slots' near page, after the foot update's (0xa0) */
static const unsigned char pfbody_kill_bytes[8]={0xff,0x90,0x08,0x14,0x00,0x00,0x84,0xc0};   /* the call, then test al,al */
static unsigned char pfbody_kill_patched[5];
static int pfbody_kill_installed;
/* A parked body keeps its place for the scripts (the user's session of 11 Nov: slot 2 off its seat, its story's cue
 * ApproachObject_Success_V4 logged "Property lookup failed: $ApproachObject.distanceto" on both clients, $ApproachObject =
 * player.entity = P_2 in slot 2's scope, and the mission went on without the ship having moved). MD's distanceto for a
 * component (the component evaluator 0xcedeb0, case at 0xcf2409) first asks 3d9620: the component's byte +0xd1, 'in the
 * universe'. U::Component's parent change 3d7010 sets it from the new parent's (3d9460, down the component's own
 * children) and clears it for no parent: a body in its room has it, and the park (every body off the tree while anyone
 * is up, and around a join save) cleared it with the seat's upward pointer put back by a write. So for as long as
 * anyone walked, no slot's player had a place a script could take a distance from, the walker's own included. Now the
 * park marks P_k again with X4's own setter when its room is in the universe, and the seat's end (the upward pointer
 * gone) takes the mark off. A body that is not parked is X4's own business as before. Status placed=marks/unmarks. */
#define PFBODY_PLACED 0xd1u                 /* Component: in the universe (3d9620 reads it, 3d9460 sets it) */
#define PFBODY_MARK 0x3d9460u               /* (component, flag): the byte, down its children */
typedef void (*pfbody_mark_fn)(void *,unsigned char);
static pfbody_mark_fn pfbody_mark;          /* the image's at the arming, a test's */
static volatile LONG64 pfbody_marks,pfbody_unmarks;
static void pfbody_unplace(unsigned char *player) {
    unsigned char in=0;
    if(!pfbody_mark || !player || !read_local(player+PFBODY_PLACED,&in,1) || !in) return;
    pfbody_mark(player,0);InterlockedIncrement64(&pfbody_unmarks);
}
static volatile LONG64 pfbody_kill_off,pfbody_kill_spared,pfbody_kill_late;
static const struct {unsigned rva,size;const char *sha;} pfbody_pin_table[]={
    {0x3d8420u,0x89,"46a8f865b44c4e326737a70cfdf828bd65324779303b13ecaf81990f6290f7af"},    /* ConnectTo by filter; no target: off */
    {PFBODY_KILL_RVA,0x241,"ff09d24ee15551fb542937e99b6a8fe9fc502daa0fd98dfa0ed186279435b0fc"},   /* KillRecursive (the kill guard) */
    {0x3d8240u,0x144,"f087638cc9205c49f677a4ecc81c01a6cd2921be07abe28a06933ebea8752e65"},   /* off the parent (0x15: no kill of its own) */
    {0x3d7010u,0x9d2,"bab31fa2e3f80a0c1d887471c08552b31f8d2c71fdd54c9166a987fe04d520b2"},   /* parent change: unheld and parentless -> 'removed' */
    {0x702f40u,0xaf2,"640162bec27d8b862203290b22a1738885db92be92bf6366752a3535f8a47045"},   /* Player::vfunc 0x1380: no camera -> 71a560 */
    {0x71a560u,0x668,"d3c3b97836592d28fd8172139f60804933003eabc690025ca0b3447845674e41"},   /* the Player's camera, made X4's */
    {PFBODY_MARK,0x1ae,"5754e5297dee0c7dfece3833ca2a26e2e592e2703831c0e3f3c2369c33961671"}};  /* 'in the universe', down the children */
static int pfbody_fail(const char *why) {
    pfbody_stage="failed";if(!strcmp(pfbody_why,"-")) pfbody_why=why;
    pfbody_failed=1;pfoot_failed=1;pfoot_why=pfbody_why;return 0;   /* stop reason 185 */
}
static unsigned char *pfbody_parent(const pslot_slot *s) {unsigned char *parent=NULL;return read_local(s->player+0x70,&parent,8)?parent:(unsigned char*)1;}
/* X4's 3d8420 for P_k (its scope open): with a target into it, without one off its parent. For the call P_k holds one
 * more hold (+0xd4) and, without a camera, the stand-in camera (+0x4e8), so X4 neither kills it as 'removed' nor gives it
 * a camera of its own (header); both fields are back afterwards. NULL, or the why X4 did it anyway (checked after a fault
 * as well). A P_k that has a camera (X4 gave it one: a failure before) is not put into a room again; taking it off needs
 * no stand-in (702f40 makes a camera only for a target). */
static const char *pfbody_call(pslot_slot *s,void *target,int *fault) {
    unsigned char *p=s->player,*universe=NULL,*cameras[2]={NULL,NULL},*now[2]={NULL,NULL},*camera=(unsigned char*)1,*after=NULL;
    int32_t hold=-1,held=-1;unsigned char life=1;const char *why=NULL;
    if(!read_local(pnat_base+VPL_UNIVERSE,&universe,8) || !universe || !read_local(universe+PFBODY_CAMERAS,cameras,sizeof(cameras)))
        return "body_universe";
    if(!read_local(p+PFBODY_CAMERA,&camera,8) || (camera && target)) return "body_camera";
    if(!read_local(p+PFBODY_HOLD,&hold,4) || hold<0 || hold==INT32_MAX) return "body_hold";
    if(!read_local(p+PFBODY_LIFECYCLE,&life,1) || life) return "body_killed";        /* already dying: nothing to move */
    *(volatile int32_t*)(p+PFBODY_HOLD)=hold+1;
    if(!camera) *(unsigned char *volatile*)(p+PFBODY_CAMERA)=pfbody_no_camera;
    __try {pfbody_connect(p,pfbody_filter,target,NULL);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {*fault=1;}
    InterlockedIncrement64(&pfbody_held);
    /* Our hold off again by a write (3e5680 would kill an unheld parentless component), the stand-in out. */
    if(!read_local(p+PFBODY_HOLD,&held,4) || held!=hold+1) why="body_hold";
    *(volatile int32_t*)(p+PFBODY_HOLD)=held>0?held-1:0;
    if(!read_local(p+PFBODY_CAMERA,&after,8) || after!=(camera?camera:pfbody_no_camera)) why="body_camera";   /* X4's camera stays */
    else if(!camera) *(unsigned char *volatile*)(p+PFBODY_CAMERA)=NULL;
    for(unsigned i=0;i<PFBODY_CAMERA_SIZE;i++) if(pfbody_no_camera[i]) {why="body_camera";memset(pfbody_no_camera,0,sizeof(pfbody_no_camera));break;}
    if(!read_local(universe+PFBODY_CAMERAS,now,sizeof(now)) || now[0]!=cameras[0] || now[1]!=cameras[1]) why="body_camera";
    if(!read_local(p+PFBODY_LIFECYCLE,&life,1) || life) why="body_killed";          /* X4 kills it: the game over follows */
    return why;
}
/* P_k into its seat's room, in its scope. */
static int pfbody_connect_slot(unsigned k) {
    pslot_slot *s=&pslots[k];int fault=0;
    if(!s->seated || s->roomless || !s->room || !s->player) return pfbody_fail("body_seat");
    if(pfbody_parent(s)!=s->room) return pfbody_fail("body_parent");
    if(!pslot_enter(k)) return pfbody_fail("body_scope");
    *(unsigned char *volatile*)(s->player+0x70)=NULL;   /* the seat's upward pointer: ConnectTo takes it out of no list */
    const char *why=pfbody_call(s,s->room,&fault);
    unsigned char *parent=pfbody_parent(s);
    if(fault || why || parent!=s->room) {   /* not at the seat as asked: off again, the upward pointer back, then stop */
        if(!fault && parent) {const char *off=pfbody_call(s,NULL,&fault);if(!why) why=off;}
        *(unsigned char *volatile*)(s->player+0x70)=s->room;
    }
    int left=pslot_leave(k);
    if(fault) return pfbody_fail("body_connect_fault");
    if(why) return pfbody_fail(why);
    if(parent!=s->room) return pfbody_fail("body_connect");
    if(!left) return pfbody_fail("body_leave");
    pfbody_mask|=1u<<(k-1);InterlockedIncrement64(&pfbody_connects);
    return 1;
}
/* P_k off its room again, in its scope; the seat's upward pointer back for its release. place: the seat goes on (a
 * park), so P_k keeps the mark of its room (above). */
static int pfbody_disconnect_slot_as(unsigned k,int place) {
    pslot_slot *s=&pslots[k];int fault=0;
    if(!(pfbody_mask>>(k-1)&1u)) return 1;
    if(!pslot_enter(k)) return pfbody_fail("body_scope");
    const char *why=pfbody_call(s,NULL,&fault);
    unsigned char *parent=pfbody_parent(s);
    if(!fault && !parent) {
        unsigned char in=0;
        *(unsigned char *volatile*)(s->player+0x70)=s->room;pfbody_mask&=~(1u<<(k-1));InterlockedIncrement64(&pfbody_disconnects);
        if(place && !why && pfbody_mark && s->room && read_local(s->room+PFBODY_PLACED,&in,1) && in) {
            pfbody_mark(s->player,1);InterlockedIncrement64(&pfbody_marks);
        }
    }
    int left=pslot_leave(k);
    if(fault) return pfbody_fail("body_disconnect_fault");
    if(why) return pfbody_fail(why);
    if(parent) return pfbody_fail("body_disconnect");
    return left?1:pfbody_fail("body_leave");
}
static int pfbody_disconnect_slot(unsigned k) {return pfbody_disconnect_slot_as(k,0);}
/* A seat a body sits at: slot k alive and seated in its own ship's cockpit room, not walking (a dead slot, one in its
 * suit, a stranded one and a roomless seat have none). */
static int pfbody_seat_ok(unsigned k) {
    const pslot_slot *s=&pslots[k];
    return (pslot_mask>>(k-1)&1u) && s->seated && !s->walking && !s->roomless && s->room && s->player && !pilot_slots[k-1].dead;
}
/* lockstep_pilot_death.h, before it ends slot k's seat: the body off its room (if the kill guard could not take it). */
static int pfbody_release_slot(unsigned k) {
    if(!pfbody_armed || k<2 || k>PILOT_SLOTS || !(pfbody_mask>>(k-1)&1u)) return 1;
    if(!pfbody_disconnect_slot(k)) return 0;
    InterlockedIncrement64(&pfbody_kill_late);
    return 1;
}

/* ---- the kill guard (X4's KillRecursive asks at 3e525b, before anything of the kill) ---- */
/* Asked about one component (any thread X4 kills on): 1 spares it. A slot's seat ship or cockpit room: that slot's body
 * off the room now, in its scope, where one can open (the frame thread outside any scope and d95180); otherwise it
 * stays and is spared below when the walk reaches it, and the death pass takes it off. */
__attribute__((used,noinline)) static int pfbody_kill_query(unsigned char *component) {
    unsigned mask=pfbody_mask;
    if(!mask || !component) return 0;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        if(!(mask>>(k-1)&1u)) continue;
        pslot_slot *s=&pslots[k];
        if(component==s->player) {InterlockedIncrement64(&pfbody_kill_spared);return 1;}
        if(component!=s->ship && component!=s->room) continue;
        if(!pfbody_failed && pslot_can_enter(k)) {
            if(pfbody_disconnect_slot(k)) InterlockedIncrement64(&pfbody_kill_off);
            else pnat_async_fail(pfbody_why);   /* X4 kept the body: spared below, the next pass stops (185) */
        }
    }
    return 0;
}
__attribute__((used)) static int (*pfbody_kill_query_ptr)(unsigned char *)=pfbody_kill_query;
/* X4's call was 6 bytes (ff 90 08 14 00 00), ours 5 (e8 to the near page): back one byte further. Then the query with
 * X4's argument registers kept; 0 continues as X4's own call would (the component's vtable +0x1408, which returns
 * to X4's test al,al), 1 answers it here. Entered as X4 calls (rsp 8 mod 16). */
__attribute__((naked)) static void pfbody_kill_entry(void) {
    __asm__ volatile("incq (%rsp)\n\t"
        "pushq %rcx\n\tpushq %rdx\n\tpushq %r8\n\tpushq %r9\n\tsubq $0x28, %rsp\n\t"
        "callq *pfbody_kill_query_ptr(%rip)\n\t"
        "addq $0x28, %rsp\n\tpopq %r9\n\tpopq %r8\n\tpopq %rdx\n\tpopq %rcx\n\t"
        "testl %eax, %eax\n\tjnz 1f\n\t"
        "movq (%rcx), %rax\n\tjmpq *0x1408(%rax)\n"
        "1:\n\tmovb $1, %al\n\tretq\n\t");
}
static int pfbody_kill_install(void) {
    if(pfbody_kill_installed) return 1;
    if(!pslot_page || memcmp(pnat_base+PFBODY_KILL_SITE,pfbody_kill_bytes,sizeof(pfbody_kill_bytes))) return 0;
    unsigned char *stub=pslot_page+PFBODY_KILL_STUB;DWORD old,ignored;
    intptr_t rel=(intptr_t)stub-(intptr_t)(pnat_base+PFBODY_KILL_SITE+5);
    if(rel<=INT32_MIN || rel>=INT32_MAX) return 0;
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return 0;
    pview_abs_jump(stub,(void*)pfbody_kill_entry);
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    int32_t r=(int32_t)rel;pfbody_kill_patched[0]=0xe8;memcpy(pfbody_kill_patched+1,&r,4);
    if(!pview_cas(PFBODY_KILL_SITE,pfbody_kill_bytes,pfbody_kill_patched,5)) return 0;
    pfbody_kill_installed=1;return 1;
}
static int pfbody_kill_uninstall(void) {
    if(!pfbody_kill_installed) return 1;
    if(!pview_cas(PFBODY_KILL_SITE,pfbody_kill_patched,pfbody_kill_bytes,5)) return 0;
    pfbody_kill_installed=0;return 1;
}
static int pfbody_kill_intact(void) {
    unsigned char jump[14];
    if(!pfbody_kill_installed || !pslot_page || memcmp(pnat_base+PFBODY_KILL_SITE,pfbody_kill_patched,5) ||
       memcmp(pnat_base+PFBODY_KILL_SITE+5,pfbody_kill_bytes+5,3)) return 0;
    pview_abs_jump(jump,(void*)pfbody_kill_entry);return !memcmp(pslot_page+PFBODY_KILL_STUB,jump,14);
}
/* The pass (pnat_pass, stock mode): phase 0 before the slots' part (stop+1: off before the seats' release), phase 1
 * after it (take-1: on after the seats; live: checks). */
static int pfbody_pass(uint64_t tick,int phase) {
    uint64_t take=pilot_start-PILOT_SUPPRESS_LEAD;
    if(!pfbody_armed) return 1;
    if(pfbody_failed) return 0;
    if(phase==0) {
        if(tick!=pilot_stop+1) return 1;
        for(unsigned k=2;k<=PILOT_SLOTS;k++) if(!pfbody_disconnect_slot(k)) return 0;
        if(!pfbody_kill_uninstall()) return pfbody_fail("body_kill_restore");   /* no body left for it to guard */
        pfbody_stage="released";return 1;
    }
    if(tick+1==take) {
        if(!pfbody_kill_install()) return pfbody_fail("body_kill_install");    /* before the first body is in a room */
        /* A seat without a cockpit room has no player slot to sit at: no body for it (counted), not a stop. */
        for(unsigned k=2;k<=PILOT_SLOTS;k++) {
            if(!(pslot_mask>>(k-1)&1u) || !pilot_slot_enrolled(k,take)) continue;   /* absent (hot join): its body after its seat */
            if(!pfbody_seat_ok(k)) {InterlockedIncrement64(&pfbody_roomless);continue;}
            pjoin_mark(k,PJOIN_CONNECT,0);
            const int ok=pfbody_connect_slot(k);
            pjoin_mark(k,PJOIN_CONNECT,1);
            if(!ok) return 0;
        }
        pfbody_stage="connected";return 1;
    }
    if(tick>=take && tick<=pilot_stop) {
        InterlockedIncrement64(&pfbody_checks);
        if(!pfbody_kill_intact()) return pfbody_fail("body_kill_lost");
        for(unsigned k=2;k<=PILOT_SLOTS;k++) {
            if(!(pfbody_mask>>(k-1)&1u)) continue;
            if(!pslots[k].seated) return pfbody_fail("body_unseated");
            if(pfbody_parent(&pslots[k])!=pslots[k].room) return pfbody_fail("body_moved");
        }
        pfbody_stage=pfbody_parked?"parked":"live";
    }
    return 1;
}
/* Anyone off its seat now: slot 1 leaving or walking (a GET_UP replayed in this pass sets it), a slot k walking or
 * getting up in this tick's records. */
static int pfbody_someone_up(uint64_t tick) {return (pfoot_armed && pfoot_state!=PFOOT_SEATED) || pfslot_any_walking() || pfslot_getup_due(tick);}
/* After this tick's replay of slot 1 and the staging of slots 2..N, before their replay and before X4's event phase
 * (pnat_pass, live): every body off the tree while anyone is up; back after PFBODY_SETTLE ticks of everyone seated. */
static int pfbody_guard(uint64_t tick) {
    uint64_t take=pilot_start-PILOT_SUPPRESS_LEAD;
    if(!pfbody_armed) return 1;
    if(pfbody_failed) return 0;
    if(tick<take || tick>pilot_stop) return 1;
    /* A join save after this step (lockstep_pilot_join.h, armed on every client of the session): X4 loads a save with one
     * player only (reports/LOCKSTEP-JOIN-IN-WINDOW-20261104.txt W1b), so every body leaves the tree in this pass as for a
     * get-up, and comes back PFBODY_SETTLE ticks later the same way. */
    const int join_save=pjoin_park_tick && tick==pjoin_park_tick;
    if(join_save) {pjoin_park_tick=0;pjoin_last_park=tick;InterlockedIncrement64(&pjoin_parks);}
    if(pfbody_someone_up(tick) || join_save) {
        pfbody_seated_since=0;memset(pfbody_back,0,sizeof(pfbody_back));
        if(!pfbody_mask) return 1;
        for(unsigned k=2;k<=PILOT_SLOTS;k++) if(!pfbody_disconnect_slot_as(k,1)) return 0;   /* parked: the seats go on */
        if(!pfbody_parked) InterlockedIncrement64(&pfbody_parks);
        pfbody_parked=1;pfbody_stage="parked";return 1;
    }
    /* A body whose seat is none any more (the death module takes it off before it ends a seat): off. */
    for(unsigned k=2;k<=PILOT_SLOTS;k++) if((pfbody_mask>>(k-1)&1u) && !pfbody_seat_ok(k) && !pfbody_disconnect_slot(k)) return 0;
    if(pfbody_parked) {
        if(!pfbody_seated_since) pfbody_seated_since=tick;
        if(tick<pfbody_seated_since+PFBODY_SETTLE) return 1;
        for(unsigned k=2;k<=PILOT_SLOTS;k++) if(pfbody_seat_ok(k) && !(pfbody_mask>>(k-1)&1u) && !pfbody_connect_slot(k)) return 0;
        pfbody_parked=0;pfbody_seated_since=0;memset(pfbody_back,0,sizeof(pfbody_back));InterlockedIncrement64(&pfbody_unparks);pfbody_stage="live";
        return 1;
    }
    /* A seat that became one again while the other bodies stayed (a respawn, lockstep_pilot_death.h): its body after
     * PFBODY_SETTLE ticks of it, nobody up meanwhile. */
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        if(!pfbody_seat_ok(k) || (pfbody_mask>>(k-1)&1u)) {pfbody_back[k]=0;continue;}
        if(!pfbody_back[k]) pfbody_back[k]=tick;
        if(tick<pfbody_back[k]+PFBODY_SETTLE) continue;
        if(!pfbody_connect_slot(k)) return 0;
        pfbody_back[k]=0;InterlockedIncrement64(&pfbody_returns);
    }
    return 1;
}
/* An abnormal stop (pnat_restore, before the seats' release): off on the frame thread outside any scope, else kept
 * and said so (the seats stay as well). */
static void pfbody_restore(void) {
    if(!pfbody_armed) return;
    if(pfbody_mask) {
        if(GetCurrentThreadId()!=ui_thread || pstk_scope_slot) {pfbody_stage="left_connected";return;}   /* the guard stays: a body is in a room */
        for(unsigned k=2;k<=PILOT_SLOTS;k++) (void)pfbody_disconnect_slot(k);
        if(pfbody_mask) {pfbody_stage="left_connected";return;}
    }
    if(!pfbody_kill_uninstall()) pfbody_stage="kill_left";   /* the site keeps the stub, which answers as X4 then */
}
static int pfbody_pins(void) {
    if(!pfbody_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<sizeof(pfbody_pin_table)/sizeof(*pfbody_pin_table);i++)
            if(!(ok=hash_ok(image+pfbody_pin_table[i].rva,pfbody_pin_table[i].size,pfbody_pin_table[i].sha))) pin_failed_rva=pfbody_pin_table[i].rva;
        /* 716670 passes this filter to this function (the seat); the filter object is the player slot's. */
        if(ok && !(ok=image[0x71673b]==0x48 && image[0x71673c]==0x8d && image[0x71673d]==0x15 &&
                      (unsigned)(0x716742u+*(const int32_t*)(image+0x71673e))==PFBODY_FILTER && image[0x716745]==0xe8 &&
                      (unsigned)(0x71674au+*(const int32_t*)(image+0x716746))==PFBODY_CONNECT)) pin_failed_rva=0x71673bu;
        /* The kill guard's site: KillRecursive's first call, inside one 16-byte block, its answer tested next. */
        if(ok && !(ok=!memcmp(image+PFBODY_KILL_SITE,pfbody_kill_bytes,sizeof(pfbody_kill_bytes)) &&
                      ((PFBODY_KILL_SITE+4u)&~15u)==(PFBODY_KILL_SITE&~15u))) pin_failed_rva=PFBODY_KILL_SITE;
        pfbody_pins_checked=ok?1:-1;
    }
    return pfbody_pins_checked==1;
}
/* After gate.pilot_foot (and the stock slots), before the first tick; identical rerun 1. */
static int pfbody_arm_using(void) {
    if(pfbody_armed) return 1;
    if(!pfoot_armed) return pstk_arm_refuse("bodies_without_foot");
    if(!pslot_armed || !(pslot_mask&~1u)) return pstk_arm_refuse("bodies_without_slots");
    if(!pfbody_connect || !pfbody_filter) return pstk_arm_refuse("bodies_functions");
    pfbody_failed=0;pfbody_mask=0;pfbody_parked=0;pfbody_seated_since=0;memset(pfbody_back,0,sizeof(pfbody_back));pfbody_returns=pfbody_roomless=0;
    pfbody_kill_installed=0;pfbody_kill_off=pfbody_kill_spared=pfbody_kill_late=0;pfbody_held=0;memset(pfbody_no_camera,0,sizeof(pfbody_no_camera));
    pfbody_stage="armed";pfbody_why="-";pfbody_armed=1;return 1;
}
static int pfbody_arm(void) {
    if(!pfbody_armed) {
        if(stats.issued) return pstk_arm_refuse("bodies_after_first_tick");
        if(GetCurrentThreadId()!=ui_thread) return pstk_arm_refuse("thread");
        if(!pfbody_pins()) {snprintf(pstk_arm_why,sizeof(pstk_arm_why),"pins_bodies:%x",pin_failed_rva);return 0;}
        /* The running process's filter object: its vtable is the player slot filter's (relocated with the image). */
        const void *vtable=NULL;
        if(!pnat_base || !read_local(pnat_base+PFBODY_FILTER,&vtable,8) || vtable!=(const void*)(pnat_base+PFBODY_FILTER_VTABLE))
            return pstk_arm_refuse("bodies_filter");
        pfbody_connect=(pfbody_connect_fn)(image+PFBODY_CONNECT);pfbody_filter=pnat_base+PFBODY_FILTER;
        pfbody_mark=(pfbody_mark_fn)(image+PFBODY_MARK);pslot_unplace=pfbody_unplace;
    }
    return pfbody_arm_using();
}
static int pfbody_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," bodies=%s bodies_why=%s bodies_mask=%x connects=%lld/%lld body_checks=%lld parks=%lld/%lld returns=%lld kill=%d/%lld/%lld/%lld roomless=%lld held=%lld placed=%lld/%lld",
        pfbody_stage,pfbody_why,pfbody_mask,(long long)pfbody_connects,(long long)pfbody_disconnects,(long long)pfbody_checks,(long long)pfbody_parks,
        (long long)pfbody_unparks,(long long)pfbody_returns,pfbody_kill_installed,(long long)pfbody_kill_off,(long long)pfbody_kill_spared,(long long)pfbody_kill_late,(long long)pfbody_roomless,
        (long long)pfbody_held,(long long)pfbody_marks,(long long)pfbody_unmarks);
    return n>0 && (size_t)n<capacity?n:0;
}

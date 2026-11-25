/* X4's move of a player on foot to its ship's bridge, for slots 2..N (native_gate_pilot_teleport, a default; needs
 * native_gate_pilot_foot_slots and native_gate_pilot_foot_bodies; stop reason 222). Crash rk7 of 24 Nov
 * (.local/crash-rk7-dock/diagnosis.md): player 2 got up in a fighter docked in a war transport, both clients stopped with
 * 185 and crashed 30 s later.
 *
 * What X4 does [F, pinned 9.00]:
 *   decision  U::Player's update (vfunc674 0x7113e0, 0x7135b0..0x713711), for a player on foot (control position none,
 *             +0x6ab8 == [0x6d113d0]): from its first positional ancestor (481610) up to the limit 75b5f0 names, every
 *             level's macro connections (level+0x58 -> +0x170..+0x178, 0x160 bytes each) with bit 40 in their flags (+0xf0)
 *             and a shape (+0x128) are asked whether the player's transform relative to the level (3db8a0) lies inside the
 *             shape's box (74cd30). Bit 40 is the connection tag 'trigger_invalid_character_position' (0x88a9c3: the tag's
 *             id at 0x395cf44): a volume where no character may stand. On the first hit: the level's ship (3d9160, class
 *             0x70, the level itself included) and Player::TeleportTo 0x7154b0(player, ship, 0, 1, 0, 1, now, NULL, NULL, 0)
 *             (the room and the offset are rbx, zero since 0x711952); when that succeeds the screen fades (0x713c3f:
 *             0xfa7330, its handle kept at [0x6cf1578]+0x1d0).
 *   target    7154b0 -> 7143c0(player, &out, ship, 0): for a ship (class 0x75) whose control room (+0x410, class 0x19) is a
 *             cockpit (class 0x10), the connection of the room's macro (or the ship's) that carries both tags 'transporter'
 *             (id at 0x395cf34) and 'location_cockpit' (0x395ccf4): the bridge's transporter. The place is 74be30 of that
 *             connection relative to the room: a fixed spot, not where the player walked. 7143c0's draw (0x714508,
 *             cand_rng_pcg 0x4512f0) belongs to a control-position teleport (its fourth argument: here 0x7136d6 xor r8d,
 *             0x7154fa movzx ebx,r8b, 0x715b4b r9d=bl, 0x714463 test bl,bl / je 0x714587): nothing is drawn.
 *   event     U::TeleportEvent (vtable 0x2ba9890, 0x70 bytes: the room's ID +0x18, the place +0x20..+0x5f as a PosRot,
 *             +0x60 'into control' 0, +0x61 1, +0x62 'no offset' 0), posted deferred to the player at now.
 *   handler   its dispatch 0x9b1f80 resolves the room (class 0x54 by +0x11e0) and calls the listener's vtable slot 76
 *             (U::Player's entry 0x2b32c08 -> 0x7052f0) with (player, target, time, room, place, +0x60, +0x61, +0x62). A
 *             room that is none, flagged (+0x68) or refuses (vfunc 0x1398) gets a TeleportFailedEvent, nobody moves. Else:
 *             716e20(player, 0, ..) (the stand-up: StopControl, the UI's playerGetUp, process-global contexts),
 *             ConnectTo 3d8420(player, slot filter, room, place), 99a870 (pending GetUpEvents, type 0xcf, dropped), an old
 *             suit killed, 70eee0, the UI's ship notice (0x705581), the container's sound refresh (vfunc 0x1c50, bff80),
 *             d828f0 on the FPC, and U::TeleportSuccessfulEvent (0x30: old room ID +0x18, new +0x20, +0x61 at +0x28) at once.
 * So the plain game puts a player that stands where no character may stand (here: up in a fighter docked inside a war
 * transport) at its ship's bridge transporter, with a fade. Where the dock can be walked no such volume holds the player:
 * it walks off its ship and X4's room change moves it between rooms as it walks (lockstep_pilot_foot_body.h), no teleport,
 * no fade. This module is about the first case only.
 *
 * Slot k in rk7: P_k walks parked (an upward pointer, off X4's tree) and its Move runs in its scope, so X4's decision and
 * its TeleportTo were the same on every client (both stopped at 42884). The handler then ran X4's stand-up and ConnectTo on
 * P_k: P_k into the bridge's child list, a camera made for it (P_k had none) and made X4's camera on EVERY client; the
 * scope's exit check stopped both with 185 (pilot=drive_leave), and the release wrote P_k+0x70 = NULL behind X4's back.
 *
 * Here the entry at 0x2b32c08 points at ptele_handler (as the room wrapper's at slot 24):
 *   slot k    P_k of a seated, walking, parked, live slot, a room X4's handler takes, not into control (+0x60 0), with a
 *             place (+0x62 0): in slot k's scope (the delivery's, or opened here), no 716e20 and no ConnectTo: the upward
 *             pointer to the room and walk_room, then X4's 55d2a0(P_k, P_k, time, room, place, 1, 1), which finds P_k in that
 *             room and sets the place (its vfunc 0x1468, TransportedToOffsetEvent, EntityTransportationFinishedEvent): the
 *             transporter carry's recipe (lockstep_pilot_foot_transport.h). P_k stands where X4 puts its own player and
 *             walks on from there (its room changes, doors and the bridge's transporter as in any room). MD hears what a
 *             walk into another room tells it (pfbody_room_notice: object, room) and U::TeleportSuccessfulEvent with X4's
 *             flag, at once, in the scope. Every client, the same tick.
 *   held      any other event for a slot's player (seated, in a suit, dead, its body in the tree, into control, without a
 *             place, its parent not its seat's pointer, no scope): nothing; X4's handler on P_k always breaks the seat
 *             (716e20 and ConnectTo on an upward-pointer seat). Counted and named in the row; X4 asks again next frame.
 *   X4's      P_1, every other entity, and a room X4's handler refuses (it posts its failure and touches nobody).
 * Presentation: the fade of 0x713c3f shows on the client whose slot moved only (lockstep_pilot_death.h pvehk_fade, by the
 * return address 0x713c44: slot 1's on client 1, slot k's on client k); no camera changes on any client (client k's eye
 * follows P_k's room, lockstep_pilot_eye.h). Left out for slot k, named: 716e20 (the slot stands already), 99a870 (no
 * GetUpEvent of P_k), 72bb10 (AutopilotStop when the object changes: a walking slot has none running), the suit kill (a
 * walking slot has no suit), the UI's ship notice and the container's sound refresh (presentation), d828f0 (the carry's
 * recipe does without it).
 *
 * The fail-safe, while armed:
 *   detect    every pass from take to stop, for every seated slot: P_k with a camera (+0x4e8, not the bodies' stand-in), or,
 *             its body not in the tree, P_k in its parent's child table (X4 connected it): stop 222, why slot_camera /
 *             slot_connected (teleport_slot= names the slot). A failed parent check of a scope's exit asks the same at once
 *             (pslot_moved_note), so the stop says what happened rather than drive_leave.
 *   restore   an abnormal stop (pnat_restore, before the seats' release): this client's cameras back to its own player's
 *             when they are a slot player's (X4's setter 97df60 for +0x3c8, +0x3d0 written as 71a560 writes it); a P_k that
 *             X4 holds in a child table taken off with X4's own call under the bodies' guard (pfbody_call: held, no new
 *             camera), in its scope, the seat's upward pointer back for its release. Where that cannot be done P_k stays
 *             where X4 put it: pslot_unseat_player never writes +0x70 = NULL over a parent that lists P_k. A stop leaves a
 *             running game.
 * The board: a slot's suit into a ship (crash rk9-suit of 24 Nov: player 2's suit docked at a capital ship it did not own,
 * the way home of lockstep_pilot_death.h sat it at the controls of the ship it had left, a fighter stored in that capital
 * ship: no controller, stop 161 take_controller). What X4 does for its own player [F, pinned 9.00]: once its suit is
 * docked at a ship (any ship: its own, an NPC's, a claimable one; X4's dock request decides whether the suit may dock
 * there at all), U::Player's update (0x731560: no TeleportEvent pending, type 0x1d6) or the dock key (0x730ff0 at
 * 0x7313e6) calls 0x710390(player, suit, room, ..): the ship above the room (or, without a room, the ship the suit's dock
 * belongs to; class 0x6f), the fade (0xfa7330 at 0x71050d, without a room; the event then due after it), 0x10b2d60 with
 * the suit's ID, and TeleportTo 0x7154b0(player, ship, 0, 1, 0, 1, time, room, ..): the event above, with the place 7143c0
 * picks (the bridge's transporter of a ship that has one). Its handler stands the player up there (716e20, ConnectTo),
 * kills the old suit, makes the FPC: X4's player stands on foot at that spot, never at the controls, and walks or takes
 * the transporter from there.
 * For slot k: 0x710390 itself runs for P_k, in slot k's scope (lockstep_pilot_death.h pvehk_enter: the fade on client k
 * only), and its event reaches this entry: noted (the room, its ID, the place, X4's flag), nothing done in the dispatch.
 * The death module's pass (the next tick, outside every scope) boards: the body off the suit, the suit's seat released,
 * the suit killed as the way home kills it, slot k seated WITHOUT a take in the ship it left (its own: the slot's ship and
 * faction stay its own; lockstep_pilot_slots.h pslot_seat_untaken) and stood up at once without X4's stand-up placement
 * (lockstep_pilot_foot_slots.h pfslot_stand), then put where X4 puts its player as above: the upward pointer to the room,
 * walk_room, 55d2a0 with the event's place. MD: the object and the room (deferred, from the suit to the ship and room it
 * stands in), U::TeleportSuccessfulEvent (the suit's room to that room, X4's flag) and ControlStopped of the suit, in
 * X4's order. A sit-down from there is the walking slot's (its own ship's seat: the docked seat where that ship is docked
 * or stored, never a take of another ship). Left out, named: the UI's ship notice and the container's sound refresh
 * (presentation), d828f0. No home (the ship it left is gone): X4's entry is not run (lockstep_pilot_death.h), the suit
 * stays docked (counted board_no_home there).
 * The undock key on foot (Shift+D; UNDOCK 0xd3): until 24 Nov a walking slot's dock keys took it out through its ship's
 * airlock into a spacesuit (lockstep_pilot_death.h, a stopgap; the user's game of 24 Nov: Shift+D on foot put player 2
 * into a spacesuit). What X4 does for its own player [F, pinned 9.00]: 71bac0 runs 71b190 for 0xd3. In a ship it controls, the
 * undock request (72fe40). On foot: from the player up to the first container (class 0x6f) its filter takes; nothing
 * while a conversation runs (0x38ee230 +0x1a0 not 1); the player faction's ships docked there (3f7370 under the
 * container, owner g_PlayerFaction 0x38f4bd0), and the container itself when it is such a ship (class 0x75) X4 may take
 * the player into (715330 == 1); none: nothing at all (no notice); one that 715330 takes: the UI's notice of the action
 * (1257950: 0xd3, 8, 0x4d) and TeleportTo(player, ship, 0, 1, 0, 1, now, NULL, NULL, 0): its event above, the place
 * 7143c0 picks there (the bridge's transporter, a small ship's own spot), on foot, not into control; more than one: the
 * PlatformUndockMenu to choose. For slot k (ptele_undock_key, in its scope): the ship is its own (its seat's ship: the
 * slot's ship and faction), docked or stored under the container P_k stands in, or that container itself; 715330 for
 * P_k; then the notice on client k only and X4's TeleportTo for P_k, whose event this module's entry applies as any
 * walking slot's (header, slot k). Named differences: the container's filter (993c10 vtable 0x31bda30) is not asked,
 * only its class; the other players' ships of the player faction are no candidates (in coop X4's list would hold them
 * too, and their menu); so there is never the menu. DOCK_ACTION (0xaf) on foot: see lockstep_pilot_death.h.
 * Status (PILOT_STOCK row): teleport=stage/installed/applied/held/refused/passed/digest teleport_why= teleport_slot=
 * tp<k>=applied/held/room x4=cameras/taken/kept board=noted/done/gone undock=keys/moved/none/refused/busy. Rows: PILOT_TELEPORT (gate.pilot_teleport_frame) for every event that
 * reaches a player of the session: slot, tick, the room's ID, the old room's ID, the place's position, the three flags, and
 * applied / held:<why> / refused / x4 (slot 1's, X4's own handler). The digest (tick, slot, room ID, place) is the same on
 * every client. */
#define PTELE_STOP_REASON 222u
#define PTELE_ENTRY 0x2b32c08u            /* U::Player vtable 0x2b329a8, slot 76 */
#define PTELE_HANDLER 0x7052f0u           /* U::Player::vfunc76 */
#define PTELE_DISPATCH_SLOT 0x2ba98a0u    /* U::TeleportEvent's vtable 0x2ba9890, slot 2 */
#define PTELE_DISPATCH 0x9b1f80u
#define PTELE_SET_CAMERA 0x97df60u        /* the Universe's camera (+0x3c8) and its UI notice */
#define PTELE_ROOM_FLAG 0x68u             /* a room X4's handler refuses when this dword is not 0 */
#define PTELE_ROOM_ASK (0x1398u/8u)       /* ...or when this vfunc answers 0 */
#define PTELE_DOMAIN 0x50544c45504f5254ULL /* "PTLEPORT" */
#define PTELE_TELEPORT_TO 0x7154b0u      /* (player, target, control position, 1, 0, 1, time, room, offset, 0) */
#define PTELE_MAY_TELEPORT 0x715330u     /* (player, target, control position, 1): 1 = X4 may take the player there */
#define PTELE_NOTICE 0x1257950u          /* the UI's notice of an action (71b190: 0xd3, 8, 0x4d, 0x31c0f60) */
#define PTELE_NOTICE_OWNER 0x6cf1890u
#define PTELE_NOTICE_DATA 0x31c0f60u
#define PTELE_ROWS 8u
#define PTELE_ROW 200u
typedef void (*ptele_fn)(void *self,void *target,double time,void *room,void *place,unsigned char control,unsigned char posted,unsigned char bare);
static ptele_fn ptele_original;                     /* tests: in place of X4's handler */
static pfbody_room_fn ptele_room_handler;           /* 55d2a0 (the image's at the arming, a test's) */
static void (*ptele_set_camera)(void *camera);      /* 97df60 */
static unsigned char (*ptele_room_ok_test)(void *room);   /* tests: in place of the room's vfunc 0x1398 */
static int ptele_armed,ptele_installed,ptele_pins_checked;   /* ptele_failed: lockstep_pilot_inputs.h (the stop reason) */
static unsigned ptele_failed_slot;
static const char *ptele_stage="off",*ptele_why="-";
static volatile LONG64 ptele_applied,ptele_held,ptele_refused,ptele_passed,ptele_cameras_back,ptele_taken_off,ptele_kept;
static uint64_t ptele_digest=PTELE_DOMAIN;
typedef struct {
    volatile LONG64 applied,held;uint64_t room;
    int board;unsigned char board_flag;unsigned char *board_room;uint64_t board_id,board_tick;   /* the board (header): noted, due */
    __attribute__((aligned(16))) float board_place[16];
} ptele_slot;
static volatile LONG64 ptele_board_noted,ptele_board_done,ptele_board_gone;
static ptele_slot ptele_slots[PILOT_SLOTS+1];
static char ptele_rows[PTELE_ROWS][PTELE_ROW];
static unsigned ptele_row_count;
static volatile LONG64 ptele_rows_dropped;
static const struct {unsigned rva,size;const char *sha;} ptele_pin_table[]={
    {PTELE_HANDLER,0x7ad,"c1344ac923adb1aca9fcf4ef5ed44173145a42631183992a873e131a6d61443c"},   /* vfunc76: its tests and the success event */
    {PTELE_DISPATCH,0xcd,"40f56590a12b083aa33625da39d3a2fa7633684c49f396943b7a12270522c211"},    /* the event's fields into slot 76 */
    {0x7154b0u,0x11ba,"7f5210c56c082b9baf59be35b6d58c1f4072ee18a3ddcd57ffdd0bea9d77d9ce"},       /* TeleportTo: the event built */
    {0x7143c0u,0xcbc,"31cf74b2a1a5f4c57e16f2b024ae3dd54faa13ccfdd1b60623db23f58252c65d"},       /* the target: no draw on this path */
    {0x713600u,0x120,"08e00ac261731c60f6720c156363151feecaea3094847c03e58aec4a128ece93"},       /* the decision and its TeleportTo call */
    {0x713c11u,0x3a,"bdf5ab3336e1225540432dfd061f19ee253cff18d734f98bd91d6a0bc5e52aa8"},        /* the fade after it (0x713c3f) */
    {0x74cd30u,0x182,"9920467f27e46e6d87922129f93ea58885d2dae1535ce8619a782b4f7b2bf228"},       /* the volume test (bit 40) */
    {PFBODY_ROOM_HANDLER,PFBODY_ROOM_SIZE,PFBODY_ROOM_SHA},                                     /* 55d2a0: the place in the room */
    {PTELE_SET_CAMERA,0x90,"fabbba3eb786506eb40aafeb0b2e25dcf9841d45b6cb523a150b1856ef4deb47"},  /* the camera setter (the restore) */
    {0x71b190u,0x525,"34f00e13ce4c53be686e7fdd941add01fcfc5172b75af9aa7896491e91596518"},       /* UNDOCK's handler: what the key does on foot */
    {PTELE_MAY_TELEPORT,0x176,"243c9e1b022fd3ab0c0d6efdb7a166b7c9587b7b31bc74a39d575f269ee427ae"}, /* may X4 take the player there */
    {PTELE_NOTICE,0x4f,"49413c6fba9dc915f92b1f477e5c72924d8f0d5ec6d4cff0c7e47aadf0090d65"}};    /* the UI's notice of an action */
#define PTELE_PIN_COUNT (sizeof(ptele_pin_table)/sizeof(*ptele_pin_table))
static int ptele_fail(const char *why) {
    ptele_stage="failed";if(!strcmp(ptele_why,"-")) ptele_why=why;
    ptele_failed=1;pnat_async_fail(why);return 0;   /* stop reason 222 */
}
/* One row for the probe (PILOT_TELEPORT): rare, so a full ring drops and counts. */
static void ptele_row(unsigned k,unsigned char *room,unsigned char *old,const float *place,unsigned char control,unsigned char posted,
                      unsigned char bare,const char *what) {
    uint64_t to=0,from=0;float at[3]={0,0,0};
    if(room) (void)read_local(room+8,&to,8);
    if(old) from=pfbody_up_id(old,0x54);
    if(place) (void)read_local(place,at,sizeof(at));
    if(ptele_row_count>=PTELE_ROWS) {InterlockedIncrement64(&ptele_rows_dropped);return;}
    snprintf(ptele_rows[ptele_row_count++],PTELE_ROW,"slot=%u tick=%llu room=%llx from=%llx at=%.3f,%.3f,%.3f flags=%u/%u/%u %s",k,
        (unsigned long long)stats.issued,(unsigned long long)to,(unsigned long long)from,(double)at[0],(double)at[1],(double)at[2],control,posted,bare,what);
}
/* gate.pilot_teleport_frame: the rows since the last call, ' | ' between them, NULL none. */
static const char *ptele_take_rows(void) {
    static char out[PTELE_ROWS*(PTELE_ROW+4)+48];size_t n=0;
    if(!ptele_row_count && !ptele_rows_dropped) return NULL;
    out[0]=0;
    for(unsigned i=0;i<ptele_row_count;i++) n+=(size_t)snprintf(out+n,sizeof(out)-n,"%s%s",i?" | ":"",ptele_rows[i]);
    if(ptele_rows_dropped && n<sizeof(out)) n+=(size_t)snprintf(out+n,sizeof(out)-n,"%sdropped=%lld",n?" | ":"",(long long)ptele_rows_dropped);
    ptele_row_count=0;ptele_rows_dropped=0;
    return out;
}
static void ptele_x4(void *self,void *target,double time,void *room,void *place,unsigned char control,unsigned char posted,unsigned char bare) {
    (ptele_original?ptele_original:(ptele_fn)(void*)(image+PTELE_HANDLER))(self,target,time,room,place,control,posted,bare);
}
/* X4's handler's own first test of the room (0x705346..): there, not flagged, and its vfunc 0x1398. */
static int ptele_room_takes(unsigned char *room) {
    int32_t flag=1;void **vt=NULL;unsigned char ok=0;int fault=0;
    if(!room || !read_local(room+PTELE_ROOM_FLAG,&flag,4) || flag) return 0;
    if(ptele_room_ok_test) return ptele_room_ok_test(room)!=0;
    if(!read_local(room,&vt,8) || !vt) return 0;
    __try {ok=((unsigned char(*)(void*))vt[PTELE_ROOM_ASK])(room);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    return !fault && ok;
}
/* MD's U::TeleportSuccessfulEvent on P_k, at once, in its scope (X4's order: after the move, after the room notices). */
static void ptele_teleported(unsigned k,uint64_t was,uint64_t is,unsigned char flag) {
    double now=0;
    if((!pvehk_events_more && !pvehk_event_test) || !is || !pnat_fn.time || !pnat_fn.time(&now)) return;
    pvehk_event_now_flag(k,PVEHK_EVENT_TELEPORT,PVEHK_EVENT_TELEPORT_SIZE,was,is,flag,now);
}
/* Slot k's move (header): NULL done, else why it was held. */
static const char *ptele_apply(unsigned k,double time,unsigned char *room,void *place,unsigned char control,unsigned char posted,unsigned char bare) {
    pslot_slot *s=&pslots[k];unsigned char *old=NULL,life=1,in=0;int fault=0;
    if(!s->seated) return "unseated";
    if(pilot_slots[k-1].dead) return "dead";
    if(pdeath_armed && pdeath_slots[k-1].out) return "suit";
    if(!s->walking || !pfslot_walking_fpc(k)) return "seated";
    if(pfbody_mask>>(k-1)&1u) return "body";
    if(pslot_ship_dying(s)) return "dying";
    if(control) return "control";
    if(bare || !place) return "no_place";
    if(!read_local(room+PFBODY_LIFECYCLE,&life,1) || life || !read_local(room+PFBODY_PLACED,&in,1) || !in) return "room";
    if(!read_local(s->player+0x70,&old,8) || !old || old!=pslot_at(s)) return "moved";
    if(!ptele_room_handler) return "functions";
    const int own=pstk_scope_slot==k;
    if(!own && (pstk_scope_slot || !pslot_enter(k))) return "scope";
    const uint64_t was=pfbody_up_id(old,0x54);uint64_t is=0;(void)read_local(room+8,&is,8);
    *(unsigned char *volatile*)(s->player+0x70)=room;s->walk_room=room==s->room?NULL:room;
    __try {ptele_room_handler(s->player,s->player,time,room,place,1,1);}   /* X4's own: P_k in that room, so the place and the events */
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    if(!fault) {pfbody_room_notice(k,old,room);ptele_teleported(k,was,is,posted);}
    if(!own && !pslot_leave(k)) (void)ptele_fail(pslot_leave_named("teleport_leave"));
    if(fault) {(void)ptele_fail("teleport_fault");return "fault";}
    uint32_t at[3]={0,0,0};(void)read_local(place,at,sizeof(at));   /* the place's position, as bits */
    const uint64_t words[5]={stats.issued,k,is,(uint64_t)at[0]|((uint64_t)at[1]<<32),at[2]};
    for(unsigned i=0;i<5;i++) for(unsigned b=0;b<8;b++) {ptele_digest^=(unsigned char)(words[i]>>(8*b));ptele_digest*=0x100000001b3ULL;}
    ptele_slots[k].room=is;InterlockedIncrement64(&ptele_slots[k].applied);InterlockedIncrement64(&ptele_applied);
    return NULL;
}
/* The board's way (header): armed and live, and the pieces it needs. */
static int ptele_board_on(void) {return ptele_armed && ptele_installed && !ptele_failed && pfslot_armed && pfbody_armed && ptele_room_handler;}
/* A suited slot's event (header, the board): noted for the pass; NULL noted, else why it was held. */
static const char *ptele_board_note(unsigned k,unsigned char *room,void *place,unsigned char control,unsigned char posted,unsigned char bare) {
    ptele_slot *t=&ptele_slots[k];unsigned char life=1,in=0;uint64_t id=0;
    if(!pslots[k].seated || pilot_slots[k-1].dead || pdeath_slots[k-1].lost) return "suit";
    if(control) return "control";
    if(bare || !place) return "no_place";
    if(!read_local(room+8,&id,8) || !id || !read_local(room+PFBODY_LIFECYCLE,&life,1) || life || !read_local(room+PFBODY_PLACED,&in,1) || !in) return "room";
    if(!pvehk_home_ship(k)) return "no_home";
    t->board=1;t->board_flag=posted;t->board_room=room;t->board_id=id;t->board_tick=stats.issued;memcpy(t->board_place,place,sizeof(t->board_place));
    InterlockedIncrement64(&ptele_board_noted);
    return NULL;
}
/* The board (header), from the death module's pass outside every scope: 1 done or nothing due (X4 asks again), 0 failed
 * (pdeath_why names it). */
static int ptele_board(unsigned k) {
    ptele_slot *t=&ptele_slots[k];pslot_slot *x=&pslots[k];pdeath_slot *d=&pdeath_slots[k-1];
    if(!t->board) return 1;
    t->board=0;
    unsigned char *room=t->board_room,*suit=x->ship,*home=pvehk_home_ship(k),life=1,in=0;uint64_t id=0;void **vtable=NULL;double now=0;int fault=0;
    if(!home || !x->seated || !suit || suit==home || !x->player || pilot_slots[k-1].dead || d->lost || !room || !read_local(room+8,&id,8) ||
       id!=t->board_id || !read_local(room+PFBODY_LIFECYCLE,&life,1) || life || !read_local(room+PFBODY_PLACED,&in,1) || !in || !pnat_fn.time(&now)) {
        InterlockedIncrement64(&ptele_board_gone);return 1;
    }
    /* The suit, as the way home leaves it (lockstep_pilot_death.h pvehk_pass): its body off it, its seat's pointers back, killed. */
    uint64_t left_object=0,left_room=0;unsigned left_class=0x79u;pvehk_where(k,&left_object,&left_room);
    {uint64_t control_id=0;pvehk_control_of(suit,&control_id,&left_class);}
    if(!pfbody_release_slot(k)) {pdeath_why=pfbody_why;return 0;}
    if(!pslot_release(k)) {pdeath_why=pslot_why;return 0;}
    if(read_local(suit,&vtable,8) && vtable) ((hitn_kill_fn)vtable[PDEATH_KILL_SLOT])(suit,PDEATH_KILL_REMOVED,NULL,now);
    /* Seated without a take in the ship it left, then on foot where X4 puts its own player: in its scope. */
    pslot_seat_untaken=1;
    const int seated=pvehk_seat_as(k,d->home,"board",1);
    pslot_seat_untaken=0;
    if(!seated) return 0;
    if(!pslot_enter(k)) {pdeath_why="board_scope";return 0;}
    const int stood=pfslot_stand(k,now,0,NULL);
    if(stood) {
        x->docked=0;
        *(unsigned char *volatile*)(x->player+0x70)=room;x->walk_room=room==x->room?NULL:room;
        __try {ptele_room_handler(x->player,x->player,now,room,t->board_place,1,1);}   /* X4's own: P_k in that room, the place */
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    }
    const int left=pslot_leave(k);
    if(!stood) {pdeath_why=pfslot_why;return 0;}
    if(fault) {pdeath_why="board_fault";return 0;}
    if(!left) {pdeath_why=pslot_leave_named("board_leave");return 0;}
    /* MD, in X4's order for its own player: the object and the room (deferred), the teleport at once, the suit's controls left. */
    if(pvehk_events_on || pvehk_event_test) {
        pvehk_event_post(x->player,PVEHK_EVENT_OBJECT,left_object,pfbody_up_id(room,0x49),now);
        pvehk_event_post(x->player,PVEHK_EVENT_ROOM,left_room,id,now);
    }
    ptele_teleported(k,left_room,id,t->board_flag);
    pvehk_control_id(k,0,left_object,left_class);
    d->out=0;d->home=0;d->suit=0;d->ask=d->ranged=0;d->dock_tick=0;d->arrive=0;d->arrive_room=NULL;
    uint32_t at[3]={0,0,0};memcpy(at,t->board_place,sizeof(at));
    const uint64_t words[5]={stats.issued,k|0x100u,id,(uint64_t)at[0]|((uint64_t)at[1]<<32),at[2]};
    for(unsigned i=0;i<5;i++) for(unsigned b=0;b<8;b++) {ptele_digest^=(unsigned char)(words[i]>>(8*b));ptele_digest*=0x100000001b3ULL;}
    t->room=id;InterlockedIncrement64(&ptele_board_done);
    return 1;
}
/* ---- the undock key on foot (header) ---- */
typedef unsigned char (*ptele_teleport_fn)(void *,void *,unsigned char,unsigned char,unsigned char,unsigned char,double,void *,void *,unsigned char);
typedef int (*ptele_may_fn)(void *,void *,unsigned char,unsigned char);
typedef void (*ptele_notice_fn)(void *,unsigned,unsigned,unsigned,void *);
static ptele_teleport_fn ptele_teleport_to;   /* the image's at the arming, a test's */
static ptele_may_fn ptele_may;
static ptele_notice_fn ptele_notice;
static volatile LONG64 ptele_keys,ptele_keys_moved,ptele_keys_none,ptele_keys_refused,ptele_keys_busy;
/* The first container (class 0x6f) from a component up, the component itself included. */
static unsigned char *ptele_container(unsigned char *c) {
    for(unsigned depth=0;c && depth<16;depth++) {
        void **vt=NULL;unsigned char is=0;int fault=0;
        if(!read_local(c,&vt,8) || !vt) return NULL;
        __try {is=((unsigned char(*)(void*,int))vt[0x11e8/8])(c,0x6f);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
        if(fault) return NULL;
        if(is) return c;
        if(!read_local(c+0x70,&c,8)) return NULL;
    }
    return NULL;
}
static int ptele_below(const unsigned char *c,const unsigned char *above) {
    for(unsigned depth=0;c && depth<16;depth++) {
        if(c==above) return 1;
        if(!read_local(c+0x70,&c,8)) return 0;
    }
    return 0;
}
/* Slot k's UNDOCK on foot, from its replayed record (lockstep_pilot_death.h pvehk_home_key, in slot k's scope or none):
 * 1 always (the key is this module's). */
static int ptele_undock_key(unsigned k) {
    pslot_slot *s=&pslots[k];unsigned char *conversation=NULL;int state=1,may=0,fault=0;double now=0;
    InterlockedIncrement64(&ptele_keys);
    if(!ptele_armed || !ptele_installed || ptele_failed || !ptele_teleport_to || !ptele_may || !s->seated || !s->walking || !s->player ||
       pilot_slots[k-1].dead || !pnat_fn.time || !pnat_fn.time(&now)) {InterlockedIncrement64(&ptele_keys_none);return 1;}
    if(read_local(pnat_base+PFSLOT_CONVERSATION,&conversation,8) && conversation && read_local(conversation+0x1a0,&state,4) && state!=1) {
        InterlockedIncrement64(&ptele_keys_busy);return 1;
    }
    unsigned char *container=ptele_container(pslot_at(s)),*ship=s->ship;
    if(!container || !ship || !ptele_below(ship,container)) {InterlockedIncrement64(&ptele_keys_none);return 1;}   /* X4: nothing */
    const int own=pstk_scope_slot==k;
    if(!own && (pstk_scope_slot || !pslot_enter(k))) {InterlockedIncrement64(&ptele_keys_refused);return 1;}
    __try {
        may=ptele_may(s->player,ship,0,1);
        if(may==1) {
            if((pstk_capture_slot?pstk_capture_slot:1u)==k && ptele_notice)   /* the notice: client k's own UI only */
                ptele_notice(*(void**)(pnat_base+PTELE_NOTICE_OWNER),0xd3u,8u,0x4du,pnat_base+PTELE_NOTICE_DATA);
            (void)ptele_teleport_to(s->player,ship,0,1,0,1,now,NULL,NULL,0);
        }
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    if(!own && !pslot_leave(k)) (void)ptele_fail(pslot_leave_named("undock_leave"));
    if(fault) {(void)ptele_fail("undock_fault");return 1;}
    InterlockedIncrement64(may==1?&ptele_keys_moved:&ptele_keys_refused);
    return 1;
}
/* U::Player's slot 76 (header). */
static void ptele_handler(void *self,void *target,double time,void *room,void *place,unsigned char control,unsigned char posted,unsigned char bare) {
    unsigned k=0;
    for(unsigned i=2;self && i<=PILOT_SLOTS;i++) if(pslots[i].player==(unsigned char*)self) {k=i;break;}
    if(!k || self!=target) {   /* X4's own player, every other entity, a target that is not the listener (X4 returns) */
        if(self && self==(void*)pslot_p1 && self==target) {
            unsigned char *from=NULL;(void)read_local((unsigned char*)self+0x70,&from,8);
            ptele_row(1,room,from,place,control,posted,bare,"x4");
        }
        InterlockedIncrement64(&ptele_passed);
        ptele_x4(self,target,time,room,place,control,posted,bare);
        return;
    }
    unsigned char *old=NULL;(void)read_local(pslots[k].player+0x70,&old,8);
    if(!ptele_room_takes(room)) {   /* X4's failure event only, in the slot's scope where one can open */
        const int entered=pstk_scope_slot?0:pslot_enter(k);
        InterlockedIncrement64(&ptele_refused);ptele_row(k,room,old,place,control,posted,bare,"refused");
        ptele_x4(self,target,time,room,place,control,posted,bare);
        if(entered && !pslot_leave(k)) (void)ptele_fail(pslot_leave_named("teleport_leave"));
        return;
    }
    const int suited=pdeath_armed && pdeath_slots[k-1].out==2 && ptele_board_on();
    const char *held=suited?ptele_board_note(k,room,place,control,posted,bare):ptele_apply(k,time,room,place,control,posted,bare);
    if(!held) {ptele_row(k,room,old,place,control,posted,bare,suited?"board":"applied");return;}
    char what[40];snprintf(what,sizeof(what),"held:%s",held);
    InterlockedIncrement64(&ptele_held);InterlockedIncrement64(&ptele_slots[k].held);
    ptele_row(k,room,old,place,control,posted,bare,what);
}
static int ptele_install(int on) {
    if(on && !ptele_installed) {
        if(!script_rng_slot_swap(PTELE_ENTRY,image+PTELE_HANDLER,(void*)ptele_handler)) return 0;
        ptele_installed=1;
    } else if(!on && ptele_installed) {
        if(!script_rng_slot_swap(PTELE_ENTRY,(void*)ptele_handler,image+PTELE_HANDLER)) return 0;
        ptele_installed=0;
    }
    return 1;
}

/* ---- the fail-safe ---- */
/* X4 took P_k: it gave it a camera, or (its body not ours in the tree) holds it in its parent's child table. */
static const char *ptele_x4_took(unsigned k) {
    const pslot_slot *s=&pslots[k];unsigned char *camera=NULL;
    if(!s->seated || !s->player) return NULL;
    if(read_local(s->player+PFBODY_CAMERA,&camera,8) && camera && camera!=pfbody_no_camera) return "slot_camera";
    if(!(pfbody_mask>>(k-1)&1u) && pslot_listed(s->player)) return "slot_connected";
    return NULL;
}
/* pslot_leave's parent check failed for slot k (lockstep_pilot_slots.h pslot_moved_note). */
static void ptele_moved(unsigned k) {
    if(!ptele_armed || ptele_failed || k<2 || k>PILOT_SLOTS) return;
    const char *why=ptele_x4_took(k);
    if(why) {ptele_failed_slot=k;(void)ptele_fail(why);}
}
/* pnat_pass, every tick, before the foot slots' pass. */
static int ptele_pass(uint64_t tick) {
    const uint64_t take=pilot_start-PILOT_SUPPRESS_LEAD;
    if(!ptele_armed) return 1;
    if(ptele_failed) return 0;
    if(tick==pilot_stop+1) {   /* before the seats' release: X4's handler for everybody from here */
        if(!ptele_install(0)) return ptele_fail("teleport_entry_restore");
        ptele_stage="released";return 1;
    }
    if(tick<take || tick>pilot_stop) return 1;
    if(ptele_installed && *(void*volatile*)(image+PTELE_ENTRY)!=(void*)ptele_handler) return ptele_fail("teleport_entry_lost");
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        const char *why=ptele_x4_took(k);
        if(why) {ptele_failed_slot=k;return ptele_fail(why);}
    }
    ptele_stage="live";return 1;
}
/* An abnormal stop (pnat_restore, after the bodies' restore, before the seats'): this client's cameras back to its own
 * player's, a P_k that X4 holds taken off with X4's own call (header). The frame thread outside every scope only. */
static void ptele_restore(void) {
    if(!ptele_armed) return;
    if(!ptele_install(0)) ptele_stage="entry_left";
    if(GetCurrentThreadId()!=ui_thread || pstk_scope_slot || !pnat_base) {ptele_stage="left";return;}
    unsigned char *universe=NULL,*own=NULL;
    if(!read_local(pnat_base+VPL_UNIVERSE,&universe,8) || !universe) return;
    if(pslot_p1) (void)read_local(pslot_p1+PFBODY_CAMERA,&own,8);
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        pslot_slot *s=&pslots[k];unsigned char *camera=NULL,*now[2]={NULL,NULL};
        if(!s->seated || !s->player) continue;
        if(read_local(s->player+PFBODY_CAMERA,&camera,8) && camera && camera!=pfbody_no_camera && own && own!=camera &&
           read_local(universe+PFBODY_CAMERAS,now,sizeof(now)) && (now[0]==camera || now[1]==camera)) {
            if(now[0]==camera && ptele_set_camera) {   /* X4's setter: +0x3c8 and the UI's notice */
                __try {ptele_set_camera(own);}
                __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {}
            }
            unsigned char *volatile *const cams=(unsigned char *volatile*)(universe+PFBODY_CAMERAS);
            if(cams[0]==camera) cams[0]=own;   /* the setter faulted or is not there */
            if(cams[1]==camera) cams[1]=own;   /* +0x3d0, as 71a560 writes it (0x71a744) */
            InterlockedIncrement64(&ptele_cameras_back);
        }
        if((pfbody_mask>>(k-1)&1u) || !pslot_listed(s->player)) continue;
        if(!pfbody_connect || !pfbody_filter || !pslot_enter(k)) {InterlockedIncrement64(&ptele_kept);continue;}
        int fault=0;const char *why=pfbody_call(s,NULL,&fault);unsigned char *parent=pfbody_parent(s);
        if(!fault && !parent) {*(unsigned char *volatile*)(s->player+0x70)=pslot_at(s);InterlockedIncrement64(&ptele_taken_off);}   /* the seat's pointer, for its release */
        else InterlockedIncrement64(&ptele_kept);
        (void)why;(void)pslot_leave(k);
    }
}

/* ---- pins, arming, status ---- */
static int ptele_pins(void) {
    if(!ptele_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<PTELE_PIN_COUNT;i++)
            if(!(ok=hash_ok(image+ptele_pin_table[i].rva,ptele_pin_table[i].size,ptele_pin_table[i].sha))) pin_failed_rva=ptele_pin_table[i].rva;
        /* The two vtable entries (absolute: relocated in the game, the file's base in a mapped copy). */
        static const struct {unsigned entry,target;} entries[]={{PTELE_ENTRY,PTELE_HANDLER},{PTELE_DISPATCH_SLOT,PTELE_DISPATCH}};
        for(unsigned i=0;ok && i<sizeof(entries)/sizeof(*entries);i++) {
            uint64_t at=0;memcpy(&at,image+entries[i].entry,8);
            if(!(ok=at==(uint64_t)(uintptr_t)(image+entries[i].target) || at==0x140000000ull+entries[i].target)) pin_failed_rva=entries[i].entry;
        }
        /* The fade's call at 0x713c3f returns to 0x713c44 (pvehk_fade's third site). */
        if(ok && !(ok=image[0x713c3f]==0xe8 && (unsigned)(0x713c44u+*(const int32_t*)(image+0x713c40))==PVEHK_FADE && PVEHK_FADE_RET_TELEPORT==0x713c44u))
            pin_failed_rva=0x713c3fu;
        ptele_pins_checked=ok?1:-1;
    }
    return ptele_pins_checked==1;
}
static void ptele_use_image(void) {
    ptele_room_handler=(pfbody_room_fn)(image+PFBODY_ROOM_HANDLER);ptele_set_camera=(void(*)(void*))(image+PTELE_SET_CAMERA);
    ptele_teleport_to=(ptele_teleport_fn)(image+PTELE_TELEPORT_TO);ptele_may=(ptele_may_fn)(image+PTELE_MAY_TELEPORT);
    ptele_notice=(ptele_notice_fn)(image+PTELE_NOTICE);
}
/* After gate.pilot_foot_slots and gate.pilot_foot_bodies, before the first tick; identical rerun 1. */
static int ptele_arm_using(void) {
    if(ptele_armed) return 1;
    if(!pfslot_armed) return pstk_arm_refuse("teleport_without_foot_slots");
    if(!pfbody_armed) return pstk_arm_refuse("teleport_without_bodies");
    if(!ptele_room_handler || !ptele_set_camera) return pstk_arm_refuse("teleport_functions");
    ptele_failed=0;ptele_failed_slot=0;ptele_why="-";ptele_digest=PTELE_DOMAIN;memset(ptele_slots,0,sizeof(ptele_slots));
    ptele_applied=ptele_held=ptele_refused=ptele_passed=ptele_cameras_back=ptele_taken_off=ptele_kept=0;ptele_row_count=0;ptele_rows_dropped=0;
    ptele_board_noted=ptele_board_done=ptele_board_gone=0;ptele_keys=ptele_keys_moved=ptele_keys_none=ptele_keys_refused=ptele_keys_busy=0;
    if(!ptele_install(1)) return pstk_arm_refuse("teleport_entry");
    pslot_moved_note=ptele_moved;ptele_fade_site=1;
    ptele_stage="armed";ptele_armed=1;return 1;
}
static int ptele_arm(void) {
    if(!ptele_armed) {
        if(stats.issued) return pstk_arm_refuse("teleport_after_first_tick");
        if(GetCurrentThreadId()!=ui_thread) return pstk_arm_refuse("thread");
        if(!ptele_pins()) {snprintf(pstk_arm_why,sizeof(pstk_arm_why),"pins_teleport:%x",pin_failed_rva);return 0;}
        ptele_use_image();
    }
    return ptele_arm_using();
}
static int ptele_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," teleport=%s/%d/%lld/%lld/%lld/%lld/%016llx teleport_why=%s teleport_slot=%u x4=%lld/%lld/%lld board=%lld/%lld/%lld",
        ptele_stage,ptele_installed,(long long)ptele_applied,(long long)ptele_held,(long long)ptele_refused,(long long)ptele_passed,
        (unsigned long long)ptele_digest,ptele_why,ptele_failed_slot,(long long)ptele_cameras_back,(long long)ptele_taken_off,(long long)ptele_kept,
        (long long)ptele_board_noted,(long long)ptele_board_done,(long long)ptele_board_gone);
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," undock=%lld/%lld/%lld/%lld/%lld",(long long)ptele_keys,(long long)ptele_keys_moved,
        (long long)ptele_keys_none,(long long)ptele_keys_refused,(long long)ptele_keys_busy);
    for(unsigned k=2;n>0 && (size_t)n<capacity && k<=PILOT_SLOTS;k++) {
        if(!(pslot_mask>>(k-1)&1u)) continue;
        const int more=snprintf(out+n,capacity-(size_t)n," tp%u=%lld/%lld/%llx",k,(long long)ptele_slots[k].applied,(long long)ptele_slots[k].held,
            (unsigned long long)ptele_slots[k].room);
        if(more<0) return 0;
        n+=more;
    }
    return n>0 && (size_t)n<capacity?n:0;
}

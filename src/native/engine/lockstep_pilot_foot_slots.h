/* Slots 2..N get up and walk (K2), DEFAULT OFF, stop reason 185 (native_gate_pilot_foot_slots = true; needs
 * native_gate_pilot_foot and the stock slots 2..N; native_gate_pilot_foot_bodies and native_gate_pilot_death are
 * optional). Report: reports/LOCKSTEP-ON-FOOT-20261101.txt section 10, with the death module section 13.
 *
 * With the death module (lockstep_pilot_death.h): a slot that dies seated (its ship, or its suit's wait) cannot get up
 * (its records are consumed and ignored, the docked menu's claim refuses a dead slot); after a respawn it is seated
 * in the new ship as at the start and gets up from there (its FPC, made at an earlier stand-up, stays P_k's movement
 * controller: 70eee0 keeps it). A slot walking when its ship dies is not aboard (no suit, no death): its walk is in the
 * seat room's frame (upward pointers, 10e), which goes with the ship, so the walk ends there (pfslot_lose, as a sit-down
 * leaves P_k but without a seat) and the death module keeps the slot stranded until a respawn seats it. A walker whose
 * ship died earlier in the frame is not updated in it (the next pass ends the walk).
 *
 * What X4 does for its own player [F, pinned 9.00]: the GetUpEvent's handler (Player::vfunc537 706cc0) asks 716d80
 * (may stand up: a control position, the macro flag, +0x6abc, a docked class 0x54 ancestor whose macro allows it) and
 * the conversation manager (0x38ee230 +0x1a0 == 1), then 716e20(player, 1, 0): the local camera, StopControl events, the
 * contexts (70e0f0, 70e940), 70eee0 (a FirstPersonController unless 9fc670 finds one: 3dde10, d7c230, Positional::
 * vfunc829 replacing the movement controller), the UI event, the placement (8d3f60 -> the connection type; 455f90 on the
 * ship, else 771430 on the docked class 0x54 ancestor; a class 0x4d target -> 7176f0 animated or 717c30 direct), the
 * control position none (0x6d113d0) and GET_UP_ACTION withdrawn. The FPC's first pose query (d81ec0 with +0x80 clear)
 * runs d7cdb0: the walking bank reset, d81d20(1) (this process's FIRST_PERSON contexts, unless 0x38f4e7c says they are
 * requested), +0x2e0 = d7d2e0 (in its seat: the camera on the player or a control position). d7f8d0 then walks the
 * singleton's FPC once per frame at 9fe604. The FPC's pose (d81ec0) reads only its own fields after the first update.
 * The walking update never reparents the player (no ConnectTo, 3d8420/3d8240 or Player::Move below d7f8d0, depth 4).
 *
 * K2 here, every step on every client at the same tick, in slot k's scope (singleton = P_k, its banks swapped in):
 *   K2-1  slot k's GET_UP (the key on client k, or the docked menu's Get Up claimed for slot k) rides slot k's record;
 *         its replay never reaches 71bac0 (a GetUpEvent would reach P_k outside any scope): pfslot_get_up consumes it.
 *         Slot k's walking ids, FP_RUN and mouselook become slot k's replay ids (pstk_verdict_slot).
 *   K2-2  the conversation manager idle and 716d80(P_k): refused = kept seated (counted kept).
 *   K2-3  the seat's controller released (pslot_release's take-down: the stock vtable back, the ship's vtable 1075);
 *         P_k keeps its parent (the cockpit room) and its controlled-object vector (9fc550 then answers none).
 *   K2-4  70eee0(P_k): its FPC (Positional::vfunc829 replaces what the factory left), then one pose query: its first
 *         update (d7cdb0) at the seat, as X4's own player's FPC had it long before it stands; this client's FIRST_PERSON
 *         flags (0x38f4e7c requested = 1, 0x38f4e80 share = 0) are held for the stand-up so d81d20(1) requests nothing.
 *   K2-5  the placement as 716e20 finds it (8d3f60; 455f90 and the connection's record 455f20 on the ship, else 771430
 *         and 7713c0 on the docked ancestor; a class 0x4d target), with the routine 716e20 picks: 7176f0 when the
 *         record carries the get-up tag (image 0x395cac8; with the record's +0x128), else 717c30; both as X4's
 *         immediate stand-up calls them (flag 1: the FPC posed at the connection the routine finds for the control
 *         position). No target: it stands at its seat (counted unplaced).
 *         Until 15 Nov this was always 717c30. In a small ship's cockpit the seat's record carries the tag, and
 *         717c30 looks for another connection, finds none and leaves the FPC at the seat's pose with a fixed step
 *         (its own fallback): slot k stood inside its seat, 0.4 m above the floor, and was pushed out of it in any
 *         direction (playtest reports of 14 and 15 Nov: out of the ship, too far forward). Seen with execute
 *         breakpoints, two clients: every stand-up of slot 2 took 717c30's fallback (0x717e04); X4's own player's
 *         GET_UP ran 7176f0 (call at 0x717391). 7176f0 also queues the seat's animation on the ship (its event
 *         0x25 with the record's name, 99a3f0), as for X4's own player: every client, the same tick.
 *         Then the control position none, the FPC's seat flag +0x2e0 = 0 (d7d2e0's answer for a player without one
 *         and no camera on it), P_k's +0x6a0d = 1 without its event (70ecf0's contexts are this process's).
 *   K2-6  slot k's walking bank: 0x38f4e40..0x38f4e93 without 0x38f4e7c and 0x38f4e80..83 (this client's own), swapped
 *         by pslot_enter/pslot_leave; started as d7d6c0's bank half (zero, e70 = -1.0, e7d = 1) after the stand-up.
 *   K2-7  the call at 9fe604 re-pointed (near page, stub 0xa0): d7f8d0 for the singleton's FPC as X4, then
 *         d7f8d0(FPC_k) in scope k for every walking slot; a frame without the call runs them from the next pass.
 *   K2-8  a walking slot stays 'seated' for the slot module (its parent, vector and scope) without a controller:
 *         no d95180, no controller checks; checked here: its FPC still its movement controller, its parent the room.
 *         Client k's contexts follow (K1, lockstep_pilot_foot.h: pfoot_own_walking).
 *   K2-9  client k's eye: the presented pose is P_k's FPC pose (lockstep_pilot_eye.h, stage 'walk').
 *   K2-10 the receipt: '|W<k>:<walking>:<hash>' per stock slot k: its bank and its FPC's pose at the pass.
 * Slot k's GET_UP while walking sits it back down (the take in scope k as at its seat, the control position back,
 * the seat flag 1, the bank's active flag 0): the walk back to the seat through the pick is not replayed (W7).
 * The chair's animation at the sit-down and the stand-up: native_gate_pilot_chair, below (pfslot_chair).
 * Not here: MD's player.entity for slot k (no room events), its own contexts on other clients (none), seeing P_k (its
 * body: lockstep_pilot_foot_body.h parks every body while anyone is off its seat), the ship's NPC pilot that 716e20
 * sends back to its chair at X4's own player's stand-up (0x717159: signal_pilotreturntochair, not sent for slot k). */
#define PFSLOT_MAY_STAND 0x716d80u
#define PFSLOT_FPC_MAKE 0x70eee0u
#define PFSLOT_FPC_UPDATE 0xd7f8d0u
#define PFSLOT_FPC_POSE 0xd81ec0u
#define PFSLOT_PLACE 0x717c30u
#define PFSLOT_CONN_TYPE 0x8d3f60u
#define PFSLOT_SHIP_CONN 0x455f90u
#define PFSLOT_DOCK_CONN 0x771430u
#define PFSLOT_PLACE_TAGGED 0x7176f0u     /* (player, target, the record's +0x128, flag) */
#define PFSLOT_SHIP_RECORD 0x455f20u      /* (ship, type) -> the connection's record */
#define PFSLOT_DOCK_RECORD 0x7713c0u
#define PFSLOT_TAG_HAS 0xcb170u           /* (tags, &tag) */
#define PFSLOT_GETUP_TAG 0x395cac8u       /* image: the tag 716e20 asks the record for (runtime value) */
#define PFSLOT_RECORD_TAGS 0x100u
#define PFSLOT_RECORD_NAME 0x128u
#define PFSLOT_CALL_SITE 0x9fe604u        /* call d7f8d0 in UniverseFrameUpdate (9fc670's answer in rcx) */
#define PFSLOT_STUB 0xa0u                 /* on the slots' near page: d95180 0, refill 0x40, pick 0x60, game over 0x80 */
#define PFSLOT_POSITION_NONE 0x6d113d0u   /* the control position 716e20 writes when the player stands */
#define PFSLOT_CONVERSATION 0x38ee230u    /* the conversation manager (pointer): +0x1a0 == 1 idle (706cc0) */
#define PFSLOT_POSITION 0x6ab8u           /* Player: control position */
#define PFSLOT_FPC_READY 0x80u            /* FPC: its first update ran (d81ec0 -> d7cdb0 otherwise) */
#define PFSLOT_FPC_SEAT 0x2e0u            /* FPC: in its seat (the pose held at +0x2b0..0x2d0) */
#define PFSLOT_FPC_CLASS 0x16
#define PFSLOT_SHIP_CLASS 0x70
#define PFSLOT_DOCK_CLASS 0x54
#define PFSLOT_TARGET_CLASS 0x4d          /* vtable +0x11e0 (716e20) */
#define PFSLOT_CONN_NONE 0xa
#define PFSLOT_DOMAIN 0x50464f4f54534cULL /* "PFOOTSL" */
typedef struct {
    unsigned char (*may_stand)(void *);                   /* 716d80 */
    void (*make_fpc)(void *);                             /* 70eee0 */
    void (*update)(void *);                               /* d7f8d0 */
    void (*pose)(void *,float *,double,unsigned char);    /* d81ec0 */
    void (*place)(void *,void *,unsigned char);           /* 717c30 */
    int (*conn_type)(int);                                /* 8d3f60 */
    unsigned char (*ship_conn)(void *,void *,int);        /* 455f90 */
    unsigned char (*dock_conn)(void *,void *,int);        /* 771430 */
    void (*place_tagged)(void *,void *,void *,unsigned char);   /* 7176f0 */
    void *(*ship_record)(void *,int);                     /* 455f20 */
    void *(*dock_record)(void *,int);                     /* 7713c0 */
    unsigned char (*tag_has)(void *,const int *);         /* cb170 */
} pfslot_fns;
typedef struct {
    unsigned char bank[PFOOT_BANK_SIZE];                  /* slot k's walking bank while no scope of it is open */
    unsigned char *fpc;
    int walking;
    uint64_t stood_tick,frame_hash,frame_bank,frame_pose;   /* the receipt's hash; its two halves for the status (fw=) */
    unsigned char frame_state;
    volatile LONG64 getups,kept,stood,sat,ignored,updates,placed,unplaced,lost,tagged;
    unsigned char *reseat;uint64_t reseat_id;   /* a sit-down in another ship, due in the pass (pfslot_reseat) */
} pfslot_slot;
static pfslot_fns pfslot_fn;
static pfslot_slot pfslots[PILOT_SLOTS+1];
static int pfslot_armed,pfslot_failed,pfslot_pins_checked,pfslot_hook_installed;
static unsigned char pfslot_site_patched[5];
static uint64_t pfslot_update_tick;
static const char *pfslot_stage="off",*pfslot_why="-";
static volatile LONG64 pfslot_hook_calls,pfslot_late,pfslot_getups_claimed,pfslot_dying_skips;
static const struct {unsigned rva,size;const char *sha;} pfslot_pin_table[]={
    {0x716d80u,0xa0,"799f73503d1970523fe2e4f5e9d49765d5f064ee1075a5165a386c928d13d1db"},    /* may stand up */
    {0x70eee0u,0xc5,"cbfda6c137772e7a8e953f36f47af499bd3fcf7ec7e6a208c1024f7377810e39"},    /* the FPC unless the singleton has one */
    {0xd7f8d0u,0x1a6f,"e703bcdc053ab3b42cbb1bdf1269fa90eb5cadb7b07b981b47cf0d251db47e1d"},  /* the walking update */
    {0xd81ec0u,0x46f,"b1214c17c25e0ce8060da52b5d4a69159dfd89d74975f6bffd3092ec5f77ae8c"},   /* the FPC's pose (first update at +0x80) */
    {0x717c30u,0x298,"91972c68e91f771f7366c9522d6864ee3bdb8782ade6955fad7f22964e1a06a5"},   /* the direct placement */
    {0x8d3f60u,0xa6,"6be169d571c8b043238d1fccd2b6854f18dc42f11ebddd3a7043568fbd8c70bd"},    /* control position -> connection type */
    {0x455f90u,0x8a,"c8e8ac2a8a45a68206392e25ce4d0c5d8fe3c54eb5a30f3aa99fef8f39b7fc9c"},    /* the ship's stand-up connection */
    {0x771430u,0x8a,"5334855facc9ccec856d9a78ebfc0da5220958b8c4f470c4cc4bbafc45d6f5e5"},    /* the dock's */
    {0x7176f0u,0x391,"7d20eeec5b27c2a81840902abef34d6bd28c9023a91cd1ebab6c904c7b682d74"},   /* the placement of a tagged connection */
    {0x455f20u,0x6b,"1e6b432c3a126d022e34fee5d94b2c42a7f5bc3d060850101f47443879f3a654"},    /* the ship's connection record */
    {0x7713c0u,0x6b,"21d7e35b1eda7bb38a1f68e271b99e70a8845783df4d9511a60e2b2b54ddd79e"},    /* the dock's */
    {0xcb170u,0x7e,"15bdf418895812f828a0e1ccec525e68d5afa9bf0edda94ebf87978ae79062af"},     /* a tag in a record's tags */
    {0x7172dau,0xce,"226d36091a9a390a1a7160d252042b0320751337051b8ca18d174d4e12c644fc"},    /* 716e20's placement and control position none */
    {0xd7d2e0u,0x84,"bb97323466584f509d5a158283f79a6a71e421b05b9a6593f1ac88ea64d7c5be"},    /* in its seat: the camera, a control position */
    {0x706cc0u,0x39,"-"}};                                                                    /* GetUpEvent's handler (bytes checked below) */
#define PFSLOT_PIN_COUNT (sizeof(pfslot_pin_table)/sizeof(*pfslot_pin_table))
/* 706cc0: 716d80, the conversation manager idle, then 716e20(player, 1, 0). */
static const unsigned char pfslot_handler_bytes[0x39]={0x48,0x3b,0xd1,0x75,0x33,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0xae,0x00,0x01,0x00,
    0x84,0xc0,0x74,0x1d,0x48,0x8b,0x05,0x53,0x75,0x1e,0x03,0x83,0xb8,0xa0,0x01,0x00,0x00,0x01,0x75,0x0d,0x45,0x33,0xc0,0xb2,0x01,0x48,0x8b,0xcb,
    0xe8,0x2d,0x01,0x01,0x00,0x48,0x83,0xc4,0x20,0x5b,0xc3};
static int pfslot_fail(const char *why) {
    why=pslot_leave_named(why);   /* "slot_update_leave:parent" (lockstep_pilot_slots.h) */
    pfslot_stage="failed";if(!strcmp(pfslot_why,"-")) pfslot_why=why;
    pfslot_failed=1;pfoot_failed=1;pfoot_why=pfslot_why;return 0;   /* stop reason 185 */
}
static void pfslot_async(const char *why) {why=pslot_leave_named(why);pfslot_fail(why);pnat_async_fail(why);}

/* ---- the walking bank of slot k (K2-6) ---- */
static int pfslot_bank_byte(unsigned i) {
    return i!=PFOOT_FP_REQUESTED-PFOOT_BANK && !(i>=PFOOT_COUNTER-PFOOT_BANK && i<PFOOT_COUNTER-PFOOT_BANK+4u);
}
/* Live <-> slot k's store (pslot_enter / pslot_leave). */
static void pfslot_bank_exchange(unsigned k) {
    if(!pfslot_armed || k<2 || k>PILOT_SLOTS || !pnat_base) return;
    volatile unsigned char *at=pnat_base+PFOOT_BANK;unsigned char *store=pfslots[k].bank;
    for(unsigned i=0;i<PFOOT_BANK_SIZE;i++) if(pfslot_bank_byte(i)) {unsigned char t=at[i];at[i]=store[i];store[i]=t;}
}
/* The swapped bytes as they are now (the canonical bank outside a scope). */
static uint64_t pfslot_bank_hash_live(void) {
    uint64_t h=0xcbf29ce484222325ULL;
    if(!pnat_base) return h;
    for(unsigned i=0;i<PFOOT_BANK_SIZE;i++) if(pfslot_bank_byte(i)) {h^=pnat_base[PFOOT_BANK+i];h*=0x100000001b3ULL;}
    return h;
}
/* d7d6c0's bank half on the live bank (slot k's inside its scope): zero, e70 = -1.0, e7d = active. */
static void pfslot_bank_start(unsigned char active) {
    static const struct {unsigned at,size;} zero[]={{0x40,8},{0x48,4},{0x4c,8},{0x54,4},{0x58,8},{0x60,4},{0x64,8},{0x6c,4},{0x78,4},{0x84,8},
        {0x8c,4},{0x90,1}};
    for(unsigned i=0;i<sizeof(zero)/sizeof(*zero);i++) memset(pnat_base+0x38f4e00u+zero[i].at,0,zero[i].size);
    double minus=-1.0;memcpy(pnat_base+0x38f4e70u,&minus,8);
    pnat_base[PFOOT_ACTIVE]=active;
}

/* ---- the component walks the stand-up needs ---- */
static int pfslot_is(void *component,int cls,unsigned slot) {
    void **vtable=*(void***)component;return ((unsigned char(*)(void*,int))vtable[slot/8u])(component,cls)!=0;
}
static unsigned char *pfslot_ancestor(unsigned char *component,int cls) {
    unsigned char *c=NULL;
    if(!component || !read_local(component+0x70,&c,8)) return NULL;
    for(unsigned depth=0;c && depth<32;depth++) {
        if(pfslot_is(c,cls,0x11e8u)) return c;
        if(!read_local(c+0x70,&c,8)) return NULL;
    }
    return NULL;
}
/* The FirstPersonController in a player's movement controller chain (9fc670 for any player). */
static unsigned char *pfslot_fpc_of(unsigned char *player) {
    unsigned char *c=NULL;
    if(!player || !read_local(player+0x200,&c,8)) return NULL;
    for(unsigned steps=0;c && steps<16;steps++) {
        void **vtable=*(void***)c;
        if(((unsigned char(*)(void*,int))vtable[3])(c,PFSLOT_FPC_CLASS)) return c;
        c=((unsigned char*(*)(void*))vtable[20])(c);
    }
    return NULL;
}
/* The seat's connection and its record for P_k's control position, as 716e20 (get up) and 716670 (sit down) find them:
 * 8d3f60 -> the connection type; 455f90 and the connection's record 455f20 on the ship, else 771430 and 7713c0 on the docked
 * class 0x54 ancestor (or the ship's cockpit room). out = {target, connection}, both NULL when there is none; the record or
 * NULL. Callers catch faults. */
static unsigned char *pfslot_seat_lookup(pslot_slot *s,void *out[2]) {
    unsigned char *ship=pfslot_ancestor(s->player,PFSLOT_SHIP_CLASS),*dock=pfslot_ancestor(s->player,PFSLOT_DOCK_CLASS);
    if(!dock && ship) {unsigned char *room=NULL;if(read_local(ship+PSLOT_ROOM,&room,8) && room && pfslot_is(room,PSLOT_ROOM_CLASS,0x11e8u)) dock=room;}
    int position=0,type=PFSLOT_CONN_NONE;
    memcpy(&position,s->player+PFSLOT_POSITION,4);
    if(ship && dock) type=pfslot_fn.conn_type(position);
    unsigned char *record=NULL;out[0]=out[1]=NULL;
    if(type!=PFSLOT_CONN_NONE) {
        if(pfslot_fn.ship_conn(ship,out,type)) record=pfslot_fn.ship_record(ship,type);
        else {
            out[0]=out[1]=NULL;
            if(pfslot_fn.dock_conn(dock,out,type)) record=pfslot_fn.dock_record(dock,type);else out[0]=out[1]=NULL;
        }
    }
    return record;
}
/* K2-5, in scope k with this client's FIRST_PERSON flags held, after the FPC's first update: X4's target, the direct
 * placement; 0 = none found (P_k stands where the first update put it, at its seat). */
static int pfslot_place(pslot_slot *s) {
    void *out[2]={NULL,NULL};unsigned char *record=pfslot_seat_lookup(s,out);
    if(!out[0] || !out[1] || !pfslot_is(out[0],PFSLOT_TARGET_CLASS,0x11e0u)) return 0;
    /* 716e20's choice (0x71735d): the record's get-up tag. */
    int tag=0;memcpy(&tag,pnat_base+PFSLOT_GETUP_TAG,4);
    if(record && pfslot_fn.tag_has(record+PFSLOT_RECORD_TAGS,&tag)) {
        void *name=NULL;memcpy(&name,record+PFSLOT_RECORD_NAME,8);
        pfslot_fn.place_tagged(s->player,out[0],name,1);
        return 2;
    }
    pfslot_fn.place(s->player,out[0],1);
    return 1;
}

/* ---- the pilot chair of slots 2..N (native_gate_pilot_chair, a default; stop reason 226) ----
 * What X4 does for its own player [F, pinned 9.00]. A small ship's pilot chair (interiors_gen_chair_01: part_chair_01..07,
 * sequences chair_default_inactive 1-3, _activating 3-33 with a start sound, _active 33-34, _deactivating 34-63 with a start
 * sound; parts 03..07 with platform collision) is ship state, moved by U::TriggerAnimationEvent (vtable 0x2bab1f0, 0x48
 * bytes: +0x18 the trigger, +0x20 the connection record's name, +0x40/+0x41 instant) posted to the ship.
 *   sit down  716670's tail (0x716c2e) looks the control position's connection up as 716e20 does (pfslot_seat_lookup) and,
 *             for a record with the get-up tag, calls 7173f0(player, {target, connection}, record, instant). Besides its slot
 *             reservation (55b4d0: it reconnects the player, which fights the seat's upward pointers here: stop 185
 *             slot_moved) and the UI anchor camera (none on P_k: +0x4e8 NULL), 7173f0 (0x717458..0x717599), for a record
 *             with a name (+0x128) and a target of class 0x4d (vtable +0x11e8), builds two events with 9a1830(event,
 *             trigger, name, instant, instant) in SMem::Allocate(0x48, alignment 0x10) and posts them to the target with
 *             PostEvent 99a3f0 at the game time (TLS +0x300), not immediate: first the character's chair config (P+0x60 ->
 *             5a3e40 -> +0x368: a name at +0x300, its trigger id from 3148d0 when not 0, else 0x11 chairconfig_default),
 *             then 6 activate_chair. An untagged record goes to 717a90, which posts nothing.
 *   get up    7176f0 (pfslot_place's tagged placement, already X4's own for slot k) posts 0x25 deactivate_chair.
 *   instant   ControlPositionAction (the seat click, 0x35bdc8) and the teleport into a ship's seat (vfunc76 7052f0 at
 *             0x7055ff: the suit, the way back, a respawn) pass 0: the chair plays; HelperModule::InitializeUI's seat
 *             (7142a0 -> 0x71430b: the game's start, a load) passes 1: the chair is active at once.
 * Slot k had none of it: pslot_seat_ship and pfslot_sit_down seat P_k themselves, so its chair stayed inactive, the
 * deactivate trigger at its get-up had nothing to play and player 2 saw no chair move at either end (report of 24 Nov:
 * the get-up looked like a jump out of the seat). Here, every step on every client at the same tick, in slot k's scope:
 *   pfslot_chair(k, instant)  7173f0's trigger part: the same lookup, the tag and 7173f0's own tests, X4's chair config, the
 *             two events to the target; no tag (or no name, no target): nothing, as stock. Never 7173f0 itself.
 *   at the sit-down (pfslot_sit_down, the flying and the docked branch): instant 0, the seat click's.
 *   at a seat (pslot_seat_ship -> pfslot_chair_seat): the slot's first seat of the session (take-1, or a late enrollment's
 *             H-1) instant 1 as the game's start; every later one (the suit, the way home, a respawn: X4's teleports)
 *             instant 0. A suit's seat has no tagged record: nothing.
 *   a joiner's rebuild of the seats its save holds (lockstep_pilot_join.h pjoin_rebuild_slot, pchair_hold): nothing (the
 *             save carries the chair's sequence state), counted skipped; it counts as the slot's seat, so the joiner's
 *             later seats take the session's instant flag. Its replayed transitions post as everybody's do.
 * The instant flag comes from the replayed step only, never from this client. Status (PILOT_INPUTS row, after fs<k>):
 * chair=stage/posted/untagged/skipped/digest (digest: tick, slot, target ID, config, instant of every post: the same on
 * every client) and chair<k>=posted/untagged/skipped/instant/config. */
#define PCHAIR_STOP_REASON 226u
#define PCHAIR_ALLOCATE 0x14b7140u        /* SMem::Allocate(size, -, -, -, alignment) */
#define PCHAIR_EVENT 0x9a1830u            /* TriggerAnimationEvent's constructor (event, trigger, name, instant, instant) */
#define PCHAIR_POST 0x99a3f0u             /* EventSource::PostEvent(source, event, time, immediate) */
#define PCHAIR_CHARACTER 0x5a3e40u        /* (macro) -> the character's properties: +0x368 the chair config */
#define PCHAIR_TRIGGER_ID 0x3148d0u       /* (-, &{text, length}) -> the trigger id of that name, 0 none */
#define PCHAIR_EVENT_SIZE 0x48u
#define PCHAIR_EVENT_VTABLE 0x2bab1f0u
#define PCHAIR_MACRO 0x60u                /* Player: its macro (7173f0: [player+0x60]) */
#define PCHAIR_CONFIG 0x368u              /* the character's chair config, NULL: none */
#define PCHAIR_CONFIG_NAME 0x300u         /* ...its name (a std::string: text or buffer, length +0x10, capacity +0x18) */
#define PCHAIR_CONFIG_DEFAULT 0x11        /* chairconfig_default */
#define PCHAIR_ACTIVATE 6                 /* activate_chair */
#define PCHAIR_DEACTIVATE 0x25            /* deactivate_chair (7176f0) */
#define PCHAIR_TARGET_SLOT 0x11e8u        /* 7173f0's class test of the target (716e20's is +0x11e0) */
#define PCHAIR_DOMAIN 0x50434841495255ULL /* "PCHAIRU" */
typedef struct {
    void *(*allocate)(size_t,uintptr_t,uintptr_t,uintptr_t,unsigned);   /* 14b7140 */
    void *(*event)(void *,int,void *,unsigned char,unsigned char);       /* 9a1830 */
    void (*post)(void *,void *,double,unsigned char);                    /* 99a3f0 */
    unsigned char *(*character)(void *);                                 /* 5a3e40 */
    int (*trigger_id)(uintptr_t,const void *);                           /* 3148d0 */
} pchair_fns;
typedef struct {volatile LONG64 posted,untagged,skipped,instant;unsigned seats;int config;} pchair_slot;
static pchair_fns pchair_fn;
static pchair_slot pchairs[PILOT_SLOTS+1];
static int pchair_armed,pchair_pins_checked,pchair_hold;   /* pchair_failed: lockstep_pilot_inputs.h (the stop reason) */
static uint64_t pchair_digest=PCHAIR_DOMAIN;
static const char *pchair_stage="off",*pchair_why="-";
static const struct {unsigned rva,size;const char *sha;} pchair_pin_table[]={
    {0x9a1830u,0x73,"48338cd62fa0b31ee523158af6e9a833d89f8386a25beffcf47ee0eb9fafc8d4"},    /* the event's constructor */
    {0x717458u,0x141,"b6d39dcba76f0afeec52d49c90baf0fd790bb0bbac51b25afb7868a497d82bf6"},   /* 7173f0's chair config and its two posts */
    {0x716c2eu,0xae,"68f6a7c3b9e41cfa89532a5fc0326119bc8d8a1ee7d3bd5cba5ae80be606dab4"},    /* 716670's lookup, tag and 7173f0 call */
    {0x5a3e40u,0xc1,"079531847363ec3d42ed46b7ec0b643b5c0ec1e85cd32fc91757db62782a9c28"},    /* the character's properties */
    {0x3148d0u,0x160,"c53271af00f390f1223e9b691f028e4cb3c425c0a8a4950c1599577f47adc1b8"},   /* a trigger's id by its name */
    {0x99a3f0u,0x327,"5ca12a5d17041238645af6d6028c718bb3714cc53e9dd6963769d51827087c0d"},   /* PostEvent */
    {0x14b7140u,0x160,"2f92bf1bda4f4f9e7bfa7bdd22e5f146ef38f9bca8caa7cabfba71a89939e38e"},  /* SMem::Allocate */
    {0x7142f2u,0x1e,"0e2925f9907678878c9093facb03a839daa50bcc970970af4686331ab843a5dd"},    /* the game's start seat: instant 1 */
    {0x7055eau,0x1a,"84c01e697b334e4393034f7160f74f5a749bfc07d8699ab3ef1ac6f6dc2ea1e0"}};   /* the teleport's seat: instant 0 */
#define PCHAIR_PIN_COUNT (sizeof(pchair_pin_table)/sizeof(*pchair_pin_table))
/* The engine's trigger table (16-byte entries {name, id}): the three ids used here, by name. */
static const struct {unsigned entry,name;int id;const char *text;} pchair_triggers[]={
    {0x22b8680u,0x2a10e08u,PCHAIR_ACTIVATE,"activate_chair"},{0x22b8730u,0x2a10f10u,PCHAIR_CONFIG_DEFAULT,"chairconfig_default"},
    {0x22b8870u,0x2a11078u,PCHAIR_DEACTIVATE,"deactivate_chair"}};
static int pchair_fail(const char *why) {
    pchair_stage="failed";if(!strcmp(pchair_why,"-")) pchair_why=why;
    pchair_failed=1;pnat_async_fail(why);return 0;   /* stop reason 226 */
}
/* 7173f0's chair config (0x717474..0x7174c6): the character's own trigger, else chairconfig_default. */
static int pchair_config(unsigned char *player) {
    void *macro=NULL;unsigned char *character=NULL,*config=NULL;int id=PCHAIR_CONFIG_DEFAULT;
    memcpy(&macro,player+PCHAIR_MACRO,8);
    character=pchair_fn.character(macro);
    if(character) memcpy(&config,character+PCHAIR_CONFIG,8);
    if(config) {
        const unsigned char *name=config+PCHAIR_CONFIG_NAME;uint64_t length=0,capacity=0;const char *text=(const char*)name;
        memcpy(&length,name+0x10,8);memcpy(&capacity,name+0x18,8);
        if(capacity>=0x10) memcpy(&text,name,8);
        __attribute__((aligned(16))) struct {const char *text;uint64_t length;} view={text,length};
        const int got=pchair_fn.trigger_id((uintptr_t)length,&view);
        if(got) id=got;
    }
    return id;
}
/* In slot k's scope, at the replay of a sit-down or a seat: 7173f0's trigger part for P_k's control position. */
static int pfslot_chair(unsigned k,unsigned char instant) {
    pslot_slot *s=k>=2 && k<=PILOT_SLOTS?&pslots[k]:NULL;
    if(!pchair_armed) return 1;
    if(pchair_failed) return 0;
    if(!s || !s->player || pstk_scope_slot!=k) return pchair_fail("chair_scope");
    pchair_slot *c=&pchairs[k];double now=0;
    if(!pnat_fn.time(&now)) return pchair_fail("chair_time");
    void *out[2]={NULL,NULL},*name=NULL;unsigned char *record=NULL;int fault=0,tagged=0,config=PCHAIR_CONFIG_DEFAULT;uint64_t target=0;
    __try {
        record=pfslot_seat_lookup(s,out);
        /* 716670's choice (0x716c8b: a target, a connection, a record with the get-up tag), then 7173f0's own tests
         * (0x71742c: the target of class 0x4d; 0x71746b: the record's name). */
        int tag=0;memcpy(&tag,pnat_base+PFSLOT_GETUP_TAG,4);
        if(out[0] && out[1] && record && pfslot_fn.tag_has(record+PFSLOT_RECORD_TAGS,&tag) && pfslot_is(out[0],PFSLOT_TARGET_CLASS,PCHAIR_TARGET_SLOT)) {
            memcpy(&name,record+PFSLOT_RECORD_NAME,8);
            if(name) {
                tagged=1;config=pchair_config(s->player);(void)read_local((unsigned char*)out[0]+8,&target,8);
                for(unsigned i=0;i<2 && !fault;i++) {
                    void *event=pchair_fn.allocate(PCHAIR_EVENT_SIZE,0,0,0,0x10);
                    if(!event) {fault=2;break;}
                    pchair_fn.event(event,i?PCHAIR_ACTIVATE:config,name,instant,instant);
                    pchair_fn.post(out[0],event,now,0);
                }
            }
        }
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    if(fault) return pchair_fail(fault==2?"chair_allocate":"chair_fault");
    if(!tagged) {InterlockedIncrement64(&c->untagged);return 1;}
    InterlockedIncrement64(&c->posted);if(instant) InterlockedIncrement64(&c->instant);
    c->config=config;
    const uint64_t words[5]={stats.issued,k,target,(uint64_t)(uint32_t)config,instant};
    for(unsigned i=0;i<5;i++) for(unsigned b=0;b<8;b++) {pchair_digest^=(unsigned char)(words[i]>>(8*b));pchair_digest*=0x100000001b3ULL;}
    return 1;
}
/* pslot_seat_ship, in slot k's scope after the take: the slot's first seat of the session instant (the game's start),
 * every later one played (X4's teleports into a ship's seat); a joiner's rebuild of a seat in its save posts nothing. */
static void pfslot_chair_seat(unsigned k) {
    if(!pchair_armed || k<2 || k>PILOT_SLOTS) return;
    pchair_slot *c=&pchairs[k];
    const unsigned char instant=c->seats==0;
    c->seats++;
    if(pchair_hold) {InterlockedIncrement64(&c->skipped);return;}
    (void)pfslot_chair(k,instant);
}

/* ---- the stand-up and the sit-down (pstk_slot_replay, slot k's GET_UP, in scope k) ---- */
/* ---- a docked ship's seat (lockstep_pilot_slots.h pslot_ctl_end) ----
 * A slot whose ship docked sits in it without a PlayerController, as X4's own player does (X4 took the controller for
 * its docking one). From there, every step on every client at the same tick:
 *   get up     as from a flying seat, without the controller's release (there is none); the placement is X4's own for
 *              a docked ship (pfslot_place: the dock's connection).
 *   sit down   in the docked ship: the seat without a take (X4 gives a docked ship no PlayerController): the control
 *              position, the FPC's seat flag, first person off, the seat 'docked' again.
 *   undocked   the ship is free again (no dock, no docking or undocking controller on it) and the slot sits in it:
 *              the take, as the sit-down's, in the slot's scope (pfslot_pass). For X4's own player X4 does it itself
 *              and the pass adopts the new controller (lockstep_pilot_foot.h).
 * pfslot_ship_docked: X4's own answer (0x7ced40: the dock a ship is connected to), or its docking / undocking
 * controller still on the ship, or the ship inside another container (below). Status docked=mask/takes.
 * Stored (live 24 Nov, the rk8 session's Terran carrier): a small ship docked at one of an XL carrier's inner bays goes
 * down the bay's lift into the carrier's storage. There it has no dock connection (7ced40 answers none) and no docking
 * controller, and X4's take of its controls makes none: slot 2's sit-down from the carrier's bridge took it for a free
 * ship and stopped the session (185 slot_take_controller). A ship with a container (class 0x6f) above it is docked at a
 * station or a ship, or stored in one: docked here, so the seat is the docked seat until the ship is out in space again. */
static volatile LONG64 pfslot_dock_takes,pfslot_dock_sits,pfslot_dock_getups,pfslot_dock_inside;
static int (*pfslot_ship_docked_test)(const unsigned char *ship);   /* tests: in place of X4's answer */
static int pfslot_ship_inside(const unsigned char *ship) {
    unsigned char *c=(unsigned char*)ship;
    for(unsigned depth=0;depth<16;depth++) {
        void **vt=NULL;unsigned char is=0;int fault=0;
        if(!read_local(c+0x70,&c,8) || !c || !read_local(c,&vt,8) || !vt) return 0;
        __try {is=((unsigned char(*)(void*,int))vt[0x11e8/8])(c,0x6f);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
        if(fault) return 0;
        if(is) return 1;
    }
    return 0;
}
static int pfslot_ship_docked(unsigned char *ship) {
    unsigned char *dock=NULL;int fault=0;
    if(pfslot_ship_docked_test) return pfslot_ship_docked_test(ship);
    if(!ship || !pnat_base) return 0;
    /* Its docking controller: docked or on the way in. Its undocking controller says nothing by itself: the ship
     * keeps it after X4 has let it go (seen live, 16 Nov: slot 2's ship hung 30 m above its pad for good, its seat
     * 'docked', its thrust keys dead), so there X4's own answer decides. */
    if(pfoot_ship_docks(ship) && !pfoot_ship_undocks(ship)) return 1;
    if(pfoot_ship_undocks(ship)) {
        /* X4 lets an undocking ship go (5423c0, from the dock's event about 10 s after the request) by connecting it
         * to its zone: no docking bay (class 0x28) above it any more. The dock connection that 7ced40 reads stays
         * (seen live, 17 Nov: its answer was 'docked' for good), so the parents decide here. */
        unsigned char *c=ship;
        for(unsigned depth=0;depth<16;depth++) {
            void **vt=NULL;unsigned char is=0;int fault=0;
            if(!read_local(c+0x70,&c,8) || !c || !read_local(c,&vt,8) || !vt) return 0;
            __try {is=((unsigned char(*)(void*,int))vt[0x11e8/8])(c,0x28);}
            __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
            if(fault) return 1;
            if(is) return 1;
        }
        return 0;
    }
    if(pfslot_ship_inside(ship)) {InterlockedIncrement64(&pfslot_dock_inside);return 1;}   /* stored in a carrier, or at a dock (header) */
    if(!pvehk_poll_on) return 1;   /* X4's answer cannot be asked on this image: a docked seat stays docked */
    __try {(void)((void*(*)(void*,void**))(void*)(pnat_base+PVEHK_DOCKED))(ship,(void**)&dock);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    return !fault && dock!=NULL;
}
/* A seat's question (lockstep_pilot_slots.h pslot_seat): docked or stored by X4's own answers only, the docking or
 * undocking controller, a container above the ship, or X4's dock connection (7ced40, pinned); where X4 cannot be asked,
 * not docked (pfslot_ship_docked keeps a docked seat docked then; a new seat takes, as before). */
static int pfslot_seat_docked(unsigned char *ship) {
    unsigned char *dock=NULL;int fault=0;
    if(pfslot_ship_docked_test) return pfslot_ship_docked_test(ship);
    if(!ship || !pnat_base) return 0;
    if(pfoot_ship_undocks(ship)) return pfslot_ship_docked(ship);   /* on its way out: X4's answer as the walking seat has it */
    if(pfoot_ship_docks(ship) || pfslot_ship_inside(ship)) return 1;
    if(!pvehk_poll_on && !hash_ok(pnat_base+PVEHK_DOCKED,PVEHK_DOCKED_SIZE,PVEHK_DOCKED_SHA)) return 0;
    __try {(void)((void*(*)(void*,void**))(void*)(pnat_base+PVEHK_DOCKED))(ship,(void**)&dock);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    return !fault && dock!=NULL;
}
/* The seat's take (in scope k): a PlayerController for slot k's ship, under C_k's vtable copy. 1 taken, 2 the ship is
 * held by another controller (nothing taken), 0 failed. */
static int pfslot_take(unsigned k) {
    pslot_slot *s=&pslots[k];void **vtable=NULL,*take=NULL,*controller=NULL,*cvt=NULL,*back=NULL;
    int fault=0,position=0;
    if(!read_local(s->ship,&vtable,8) || !vtable || !read_local(vtable+PSLOT_TAKE,&take,8) || take!=(void*)pslot_fn.take) return pfslot_fail("slot_take_vtable");
    if(pslot_controller_has(s->ship,PSLOT_CONTROLLER_CLASS,NULL)!=0) return 2;   /* the ship is held */
    if(!read_local(pnat_base+PSLOT_PILOT_POSITION,&position,4) || !position) return pfslot_fail("slot_pilot_position");
    memcpy(s->player+PFSLOT_POSITION,&position,4);
    /* The take's bank reset (d84c20) starts the new controller's flight control from THIS client's input object
     * (lockstep_pilot_slots.h pslot_seat_ship): the sitting player's own client has it off (it walks), another client
     * has its own flight on. For the take the byte is the slot's own flight control (the store's, in this scope: off
     * since its stand-up; its SPACE edge follows in its record), then this client's again. */
    volatile unsigned char *flag_at=NULL;unsigned char flag_was=0;int flag_held=0;
    {   unsigned char *object=NULL;
        if(read_local(pnat_base+PSLOT_TAKE_INPUT,&object,8) && object && read_local(object+PSLOT_TAKE_FLIGHT_FLAG,&flag_was,1)) {
            const unsigned char want=*PNAT_U8(PNAT_FLIGHT)?1u:0u;
            flag_at=object+PSLOT_TAKE_FLIGHT_FLAG;
            if((flag_was!=0)!=(want!=0)) {*flag_at=want;flag_held=1;InterlockedIncrement64(&pslot_take_flags);}
        }
    }
    __try {((void(*)(void*))take)(s->ship);controller=pnat_fn.controller(pnat_base+PNAT_CONTROLLER_KEY);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    if(flag_held) *flag_at=flag_was;
    if(fault) return pfslot_fail("slot_take_fault");
    if(!controller || !read_local(controller,&cvt,8) || cvt!=(void*)(pnat_base+PSLOT_VTABLE) || !read_local((unsigned char*)controller+0x28,&back,8) ||
       back!=s->ship || pslot_controller_has(s->ship,0,controller)!=1) return pfslot_fail("slot_take_controller");
    if(InterlockedCompareExchangePointer((void*volatile*)controller,&s->vt.slots[0],pnat_base+PSLOT_VTABLE)!=(void*)(pnat_base+PSLOT_VTABLE))
        return pfslot_fail("slot_controller_vtable");
    s->controller=controller;pslot_set_routes(s,1);
    return 1;
}
/* ---- a sit-down in another ship than the seat's ----
 * A slot on foot can stand in another ship than its seat's: the board (lockstep_pilot_teleport.h: its suit docked at any
 * ship, X4 put it on foot there), a transporter. X4's own player sits down in the chair it clicks, in the ship it stands
 * in; it takes the controls of a ship of its own faction (a ship just claimed with the suit's repair laser: the claim
 * chain of 24 Nov, lockstep_pilot_attack.h and lockstep_pilot_death.h), in a docked or stored one the docked seat, and an
 * NPC's ship has no seat for it. For slot k (GET_UP while walking, or its seat click, lockstep_ui_compslot.h):
 *   - the ship around P_k's room is its seat's ship, or its seat's ship is docked or stored under it (its fighter on the
 *     carrier's pad or in its hangar, rk7/rk9), or it stands in no ship (a station): the sit-down below, as before;
 *   - another ship of the slot's own faction (its owner the owner of the slot's ship), alive, nobody else's (no other
 *     slot's ship, slot 1's player not in it), with the slot ship's vtable: the seat changes there, in the next pass
 *     outside every scope (pfslot_reseat: the walk ends, the seat released, the slot seated in that ship as a respawn
 *     seats it: its ship and faction that ship's from then on; the docked seat where it is docked or stored, else the take);
 *   - any other: nothing (counted foreign; stock has no seat for it there).
 * Status reseat=done/foreign. */
static void *uitrade_owner(void *object);   /* lockstep_ui_trade.h */
static int (*pfslot_owner_test)(unsigned char *ship,unsigned char *seat);   /* tests: 1 = one faction owns both */
static volatile LONG64 pfslot_reseats,pfslot_foreign;
static unsigned char *pfslot_ship_of(unsigned char *c) {
    for(unsigned depth=0;c && depth<32;depth++) {
        void **vt=NULL;unsigned char is=0;int fault=0;
        if(!read_local(c,&vt,8) || !vt) return NULL;
        __try {is=((unsigned char(*)(void*,int))vt[0x11e8/8])(c,PFSLOT_SHIP_CLASS);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
        if(fault) return NULL;
        if(is) return c;
        if(!read_local(c+0x70,&c,8)) return NULL;
    }
    return NULL;
}
/* The other ship slot k stands in (header): NULL for its seat's ship, a ship its seat's ship is under, or none. */
static unsigned char *pfslot_ship_here(unsigned k) {
    pslot_slot *s=&pslots[k];
    unsigned char *here=pfslot_ship_of(pslot_at(s));
    if(!here || here==s->ship || uics_under(s->ship,here)) return NULL;
    return here;
}
static int pfslot_may_sit(unsigned k,unsigned char *ship) {
    pslot_slot *s=&pslots[k];void **vtable=NULL,*take=NULL;unsigned char life=1;uint64_t id=0;
    if(!ship || !s->ship || !read_local(ship+8,&id,8) || !id || !read_local(ship+PFBODY_LIFECYCLE,&life,1) || life) return 0;
    if(!read_local(ship,&vtable,8) || !vtable || !read_local(vtable+PSLOT_TAKE,&take,8) || take!=(void*)pslot_fn.take) return 0;
    if(pfslot_owner_test?!pfslot_owner_test(ship,s->ship):(!uitrade_owner(ship) || uitrade_owner(ship)!=uitrade_owner(s->ship))) return 0;
    for(unsigned j=1;j<=pilot_slot_count && j<=PILOT_SLOTS;j++) {
        if(j==k) continue;
        if(pilot_slots[j-1].resolved && pilot_slots[j-1].runtime==id) return 0;
        if(j>=2 && pslots[j].ship==ship) return 0;
    }
    if(pslot_p1 && uics_under(pslot_p1,ship)) return 0;
    return 1;
}
/* The seat's change (header), in the pass outside every scope: 1 done or nothing due, 0 failed. */
static int pvehk_seat_as(unsigned k,uint64_t runtime,const char *event,int standing);   /* lockstep_pilot_death.h */
static int pfslot_reseat(unsigned k) {
    pfslot_slot *f=&pfslots[k];pslot_slot *s=&pslots[k];unsigned char *ship=f->reseat;uint64_t id=0;
    if(!ship) return 1;
    f->reseat=NULL;
    if(!pdeath_armed || !s->seated || !f->walking || pilot_slots[k-1].dead || !read_local(ship+8,&id,8) || id!=f->reseat_id ||
       !pfslot_may_sit(k,ship) || pfslot_ship_here(k)!=ship) {InterlockedIncrement64(&pfslot_foreign);return 1;}
    unsigned char *const at=pslot_at(s);
    const uint64_t left_object=pfbody_up_id(at,0x49),left_room=pfbody_up_id(at,0x54);
    if(f->fpc) f->fpc[PFSLOT_FPC_SEAT]=1;   /* the walk ends as a sit-down ends it */
    s->player[PFOOT_PLAYER_FLAG]=0;memset(f->bank,0,sizeof(f->bank));f->walking=0;
    if(!pfbody_release_slot(k)) return pfslot_fail("slot_reseat_body");
    if(!pslot_release(k)) return pfslot_fail("slot_reseat_release");
    const int docked=pfslot_ship_docked(ship);
    pslot_seat_untaken=docked;
    const int seated=pvehk_seat_as(k,id,"seat",docked);
    pslot_seat_untaken=0;
    if(!seated) return pfslot_fail("slot_reseat_seat");
    pvehk_changed(k,left_object,left_room);   /* MD: its player in that ship's cockpit now */
    if(!docked) pvehk_control(k,1);            /* ...at its controls (a docked one: at the undock, pfslot_undocked) */
    InterlockedIncrement64(&f->sat);InterlockedIncrement64(&pfslot_reseats);
    return 1;
}
static int pfslot_sit_down(unsigned k) {
    pslot_slot *s=&pslots[k];pfslot_slot *f=&pfslots[k];
    {   unsigned char *here=pfslot_ship_here(k);uint64_t id=0;   /* another ship than its seat's (above) */
        if(here) {
            if(!pdeath_armed || !pfslot_may_sit(k,here) || !read_local(here+8,&id,8)) {InterlockedIncrement64(&f->ignored);InterlockedIncrement64(&pfslot_foreign);return 1;}
            f->reseat=here;f->reseat_id=id;return 1;   /* the seat changes in the pass (pfslot_reseat) */
        }
    }
    pfbody_walk_home(k,1);   /* from another room: P_k under its seat's room again (the walk back is not replayed, W7) */
    if(pfslot_ship_docked(s->ship)) {   /* a docked ship's seat: no controller (header above) */
        int position=0;
        if(!read_local(pnat_base+PSLOT_PILOT_POSITION,&position,4) || !position) return pfslot_fail("slot_pilot_position");
        memcpy(s->player+PFSLOT_POSITION,&position,4);
        f->fpc[PFSLOT_FPC_SEAT]=1;pnat_base[PFOOT_ACTIVE]=0;s->player[PFOOT_PLAYER_FLAG]=0;
        f->walking=0;s->walking=0;s->docked=1;s->dock_checked=1;InterlockedIncrement64(&f->sat);InterlockedIncrement64(&pfslot_dock_sits);
        (void)pfslot_chair(k,0);   /* the chair plays as at X4's seat click (a failure stops with 226 after this pass) */
        return 1;
    }
    const int taken=pfslot_take(k);
    if(!taken) return 0;
    if(taken==2) {InterlockedIncrement64(&f->ignored);return 1;}
    f->fpc[PFSLOT_FPC_SEAT]=1;pnat_base[PFOOT_ACTIVE]=0;s->player[PFOOT_PLAYER_FLAG]=0;
    f->walking=0;s->walking=0;InterlockedIncrement64(&f->sat);
    pvehk_control(k,1);   /* MD: its player took the ship's controls (lockstep_pilot_death.h, the control events) */
    (void)pfslot_chair(k,0);   /* 716670's tail, after its control events: the chair plays (instant 0, the seat click's) */
    return 1;
}
/* A docked slot whose ship is free again takes its controls (pfslot_pass, every tick, outside every scope). */
static int pfslot_undocked(void) {
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        pslot_slot *s=&pslots[k];
        if(!s->seated || !s->docked || s->walking || !s->dock_checked || pilot_slots[k-1].dead || pfslot_ship_docked(s->ship)) continue;
        if(!pslot_enter(k)) return pfslot_fail("slot_undock_scope");
        const int taken=pfslot_take(k);
        if(taken==1) {s->docked=0;InterlockedIncrement64(&pfslot_dock_takes);}
        const int left=pslot_leave(k);
        if(!taken) return 0;
        if(!left) return pfslot_fail("slot_undock_leave");
        if(taken==1) pvehk_control(k,1);   /* MD: its player took the ship's controls */
    }
    return 1;
}
/* K2-4..K2-6 for slot k in its scope, its seat's controller gone (released, or a seat without one): the FPC, its first
 * update at the seat (as X4's own player's FPC had it long before it stands: the placement then poses an initialised FPC),
 * the placement when `place` (X4's stand-up placement; *placed as pfslot_place answers), all with this client's
 * FIRST_PERSON flags held; then the control position none, the FPC's seat flag 0, first person on and slot k's walking
 * bank. Without `place` the caller puts P_k where X4 puts its own player (lockstep_pilot_teleport.h, the board). */
static int pfslot_stand(unsigned k,double now,int place,int *placed) {
    pslot_slot *s=&pslots[k];pfslot_slot *f=&pfslots[k];int fault=0;
    volatile unsigned char *requested=pnat_base+PFOOT_FP_REQUESTED;volatile uint32_t *share=(volatile uint32_t*)(pnat_base+PFOOT_COUNTER);
    unsigned char was_requested=*requested;uint32_t was_share=*share;unsigned char *fpc=NULL;int at=0;
    __attribute__((aligned(16))) float pose[16];
    *requested=1;*share=0;
    __try {
        pfslot_fn.make_fpc(s->player);fpc=pfslot_fpc_of(s->player);
        if(fpc) {if(!fpc[PFSLOT_FPC_READY]) pfslot_fn.pose(fpc,pose,now,1);if(place) at=pfslot_place(s);}
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    *requested=was_requested;*share=was_share;
    if(fault) return pfslot_fail("slot_stand_fault");
    if(!fpc) return pfslot_fail("slot_fpc");
    if(!fpc[PFSLOT_FPC_READY]) return pfslot_fail("slot_fpc_first_update");
    {int none=*(volatile int*)(pnat_base+PFSLOT_POSITION_NONE);memcpy(s->player+PFSLOT_POSITION,&none,4);}
    fpc[PFSLOT_FPC_SEAT]=0;s->player[PFOOT_PLAYER_FLAG]=1;
    pfslot_bank_start(1);   /* K2-6: the live bank is slot k's in this scope */
    f->fpc=fpc;f->walking=1;s->walking=1;f->stood_tick=stats.issued;InterlockedIncrement64(&f->stood);
    if(placed) *placed=at;
    return 1;
}
static int pfslot_get_up(unsigned k,double now) {
    pslot_slot *s=k>=2 && k<=PILOT_SLOTS?&pslots[k]:NULL;pfslot_slot *f=s?&pfslots[k]:NULL;
    if(!s || !pfslot_armed || pstk_scope_slot!=k || !pslot_frame_thread()) return pfslot_fail("slot_scope");
    InterlockedIncrement64(&f->getups);
    if(f->walking) return pfslot_sit_down(k);
    if(!s->seated || (!s->controller && !s->docked) || pilot_slots[k-1].dead) {InterlockedIncrement64(&f->ignored);return 1;}
    unsigned char may=0,*conversation=NULL;int fault=0,state=0;
    /* K2-2: as the GetUpEvent's handler asks (706cc0): the conversation manager idle, then 716d80. */
    if(read_local(pnat_base+PFSLOT_CONVERSATION,&conversation,8) && conversation && read_local(conversation+0x1a0,&state,4) && state!=1) {
        InterlockedIncrement64(&f->kept);return 1;
    }
    __try {may=pfslot_fn.may_stand(s->player);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    if(fault) return pfslot_fail("slot_may_stand_fault");
    if(!may) {InterlockedIncrement64(&f->kept);return 1;}
    /* K2-3: the seat's controller released in this scope, as pslot_release does at stop+1. A docked seat has none. */
    const int from_dock=s->docked && !s->controller;
    if(!from_dock) {
        void **vtable=NULL,*release=NULL;
        if(!read_local(s->ship,&vtable,8) || !vtable || !read_local(vtable+PSLOT_RELEASE,&release,8) || release!=(void*)pslot_fn.release)
            return pfslot_fail("slot_release_vtable");
        if(InterlockedCompareExchangePointer((void*volatile*)s->controller,pnat_base+PSLOT_VTABLE,&s->vt.slots[0])!=(void*)&s->vt.slots[0])
            return pfslot_fail("slot_controller_vtable");
        ++pslot_own_release;
        __try {((void(*)(void*))release)(s->ship);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
        --pslot_own_release;
        s->controller=NULL;
        if(fault) return pfslot_fail("slot_release_fault");
    }
    int placed=0;
    if(!pfslot_stand(k,now,1,&placed)) return 0;
    if(from_dock) {s->docked=0;InterlockedIncrement64(&pfslot_dock_getups);}
    else pvehk_control(k,0);   /* MD: its player left the ship's controls */
    InterlockedIncrement64(placed?&f->placed:&f->unplaced);
    if(placed==2) InterlockedIncrement64(&f->tagged);
    return 1;
}

/* The death module (lockstep_pilot_death.h pdeath_lose): slot k's ship went while it walked. The walk ends where it is,
 * as the sit-down leaves P_k (the FPC's seat flag 1, first person off, its bank cleared and inactive) but without a
 * take: there is no ship. pslot_release then gives the seat's upward pointers back (the walking branch) before the
 * room they point at goes; the next seat (a respawn) starts from there, the next stand-up from the FPC P_k keeps. */
static void pfslot_lose(unsigned k) {
    pfslot_slot *f=k>=2 && k<=PILOT_SLOTS?&pfslots[k]:NULL;
    if(!f || !pfslot_armed || !f->walking) return;
    if(f->fpc) f->fpc[PFSLOT_FPC_SEAT]=1;
    if(pslots[k].player) pslots[k].player[PFOOT_PLAYER_FLAG]=0;
    memset(f->bank,0,sizeof(f->bank));   /* outside any scope the store is slot k's bank */
    f->walking=0;InterlockedIncrement64(&f->lost);
}

/* ---- the update (K2-7) ---- */
static void pfslot_update_walkers(void) {
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        pfslot_slot *f=&pfslots[k];int fault=0;
        if(!f->walking) continue;
        /* Its ship died earlier in this frame (lockstep_pilot_death.h): its room, the walk's frame, is being killed. */
        if(pslot_ship_dying(&pslots[k])) {InterlockedIncrement64(&pfslot_dying_skips);continue;}
        if(!pslot_enter(k)) {pfslot_async("slot_update_scope");return;}
        __try {pfslot_fn.update(f->fpc);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
        int left=pslot_leave(k);
        InterlockedIncrement64(&f->updates);
        if(fault) {pfslot_async("slot_update_fault");return;}
        if(!left) {pfslot_async("slot_update_leave");return;}
    }
}
/* The call at 9fe604: X4's update of the singleton's FPC, then every walking slot's in its scope. */
static void pfslot_update(void *fpc) {
    pfslot_fn.update(fpc);
    InterlockedIncrement64(&pfslot_hook_calls);
    if(!pslot_frame_thread() || pstk_scope_slot) {pfslot_async("slot_update_outside_frame");return;}
    pfslot_update_tick=stats.issued;
    pfslot_update_walkers();
}
static int pfslot_any_walking(void) {
    for(unsigned k=2;pfslot_armed && k<=PILOT_SLOTS;k++) if(pfslots[k].walking) return 1;
    return 0;
}
/* This client's own slot walks (K1: its FIRST_PERSON contexts, lockstep_pilot_foot.h). */
/* X4's own player holds a control position (it sits): its seat is one of X4's first-person count (lockstep_pilot_foot.h K1). */
static int pfslot_canon_seated(void) {
    int position=0,none=0;
    if(!pslot_p1 || !pnat_base || !read_local(pslot_p1+PFSLOT_POSITION,&position,4) || !read_local(pnat_base+PFSLOT_POSITION_NONE,&none,4)) return 0;
    return position!=none;
}
static int pfslot_own_walking(void) {
    unsigned k=pstk_capture_slot;
    return pfslot_armed && pstk_capturing && k>=2 && k<=PILOT_SLOTS && pfslots[k].walking;
}
/* The docked menu's Get Up on client k (lockstep_pilot_foot_guard.h): slot k sits in its ship. */
static int pfslot_claim_ok(unsigned k) {
    return pfslot_armed && k>=2 && k<=PILOT_SLOTS && pslots[k].seated && (pslots[k].controller || pslots[k].docked) && !pfslots[k].walking && !pilot_slots[k-1].dead;
}
/* A seated slot's GET_UP in this tick's records (lockstep_pilot_foot_body.h parks the bodies before any stand-up). */
static int pfslot_getup_due(uint64_t tick) {
    for(unsigned k=2;pfslot_armed && k<=PILOT_SLOTS;k++) {
        const pstk_slot_frame *frame=&pstk_slot_frames[k-1];
        if(frame->tick!=tick || pfslots[k].walking || !pslots[k].seated) continue;
        for(unsigned i=0;i<frame->r.count;i++) if(frame->r.events[i].kind==PSTK_ACTION && frame->r.events[i].id==PFOOT_GET_UP) return 1;
    }
    return 0;
}
/* Client k's eye (lockstep_pilot_eye.h): slot k's FPC while it walks. */
static unsigned char *pfslot_walking_fpc(unsigned k) {return pfslot_armed && k>=2 && k<=PILOT_SLOTS && pfslots[k].walking?pfslots[k].fpc:NULL;}

/* ---- the pass (pnat_pass, stock mode, before the slots' part) ---- */
static int pfslot_hook_install(void) {
    if(pfslot_hook_installed) return 1;
    if(!pres_site_install(PFSLOT_CALL_SITE,PFSLOT_FPC_UPDATE,PFSLOT_STUB,(void*)pfslot_update,pfslot_site_patched)) return 0;
    pfslot_hook_installed=1;return 1;
}
static int pfslot_hook_uninstall(void) {
    if(!pfslot_hook_installed) return 1;
    if(!pres_site_uninstall(PFSLOT_CALL_SITE,PFSLOT_FPC_UPDATE,pfslot_site_patched)) return 0;
    pfslot_hook_installed=0;return 1;
}
/* K2-8: a walking slot's FPC is still its movement controller, its parent still its room (its seat's, or the one it
 * walked into: lockstep_pilot_foot_body.h). */
static int pfslot_check(unsigned k) {
    pslot_slot *s=&pslots[k];pfslot_slot *f=&pfslots[k];unsigned char *parent=NULL;int refs=0;
    if(!s->seated || !s->walking) return pfslot_fail("slot_unseated");
    if(pfslot_fpc_of(s->player)!=f->fpc) return pfslot_fail("slot_fpc_lost");
    if(!read_local(s->player+0x70,&parent,8) || parent!=pslot_at(s)) return pfslot_fail("slot_moved");
    if(!read_local(s->player+0x40,&refs,4) || refs<1) return pfslot_fail("slot_player_lost");
    return 1;
}
static int pfslot_pass(uint64_t tick) {
    uint64_t take=pilot_start-PILOT_SUPPRESS_LEAD;
    if(!pfslot_armed) return 1;
    if(pfslot_failed) return 0;
    if(tick==pilot_stop+1) {   /* before the seats' release: no walking update after this */
        if(!pfslot_hook_uninstall()) return pfslot_fail("slot_hook_restore");
        for(unsigned k=2;k<=PILOT_SLOTS;k++) pfslots[k].walking=0;   /* the release takes their seats' pointers (pslot_release) */
        pfslot_stage="released";return 1;
    }
    if(tick<take || tick>pilot_stop) return 1;
    if(!pfslot_hook_installed) {
        if(!pfslot_hook_install()) return pfslot_fail("slot_hook_install");
        pfslot_update_tick=tick?tick-1:0;pfslot_stage="live";
    } else if(memcmp(pnat_base+PFSLOT_CALL_SITE,pfslot_site_patched,5)) return pfslot_fail("slot_hook_lost");
    for(unsigned k=2;k<=PILOT_SLOTS;k++) if(pfslots[k].reseat && !pfslot_reseat(k)) return 0;   /* a sit-down in another ship */
    if(!pfslot_undocked()) return 0;   /* a docked slot whose ship is free again: its controls */
    if(!pfslot_any_walking()) return 1;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) if(pfslots[k].walking && !pfslot_check(k)) return 0;
    if(pfslot_update_tick+1!=tick) {   /* X4 skipped the call (no FPC on its own player): the walking slots update here */
        InterlockedIncrement64(&pfslot_late);pfslot_update_walkers();
        if(pfslot_failed) return 0;
    }
    pfslot_stage="walking";return 1;
}
/* K2-10, after this tick's replay: slot k's bank and its FPC's pose (the same on every client). */
static void pfslot_frame(void) {
    double now=0;
    if(!pfslot_armed || !pnat_fn.time(&now)) return;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        pfslot_slot *f=&pfslots[k];
        f->frame_state=(unsigned char)f->walking;f->frame_hash=0;f->frame_bank=f->frame_pose=0;
        if(!f->walking) continue;
        uint64_t h=PFSLOT_DOMAIN+k;
        for(unsigned i=0;i<PFOOT_BANK_SIZE;i++) if(pfslot_bank_byte(i)) {h^=f->bank[i];h*=0x100000001b3ULL;}
        f->frame_bank=h;   /* the bank alone: which half parts when a receipt does (gu3: W2 apart under a person's mouse look) */
        __attribute__((aligned(16))) float pose[16];memset(pose,0,sizeof(pose));
        __try {pfslot_fn.pose(f->fpc,pose,now,1);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {memset(pose,0xff,sizeof(pose));}
        for(unsigned i=0;i<16;i++) if((i&3)!=3) {uint32_t v;memcpy(&v,&pose[i],4);for(unsigned b=0;b<4;b++) {h^=(unsigned char)(v>>(8*b));h*=0x100000001b3ULL;}}
        {uint64_t p=PFSLOT_DOMAIN^0x706f7365ULL;
         for(unsigned i=0;i<16;i++) if((i&3)!=3) {uint32_t v;memcpy(&v,&pose[i],4);for(unsigned b=0;b<4;b++) {p^=(unsigned char)(v>>(8*b));p*=0x100000001b3ULL;}}
         f->frame_pose=p;}
        f->frame_hash=h|1u;
    }
}
static int pfslot_frame_text(char *out,size_t capacity) {
    int n=0;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        if(!(pslot_mask>>(k-1)&1u)) continue;
        int more=snprintf(out+n,capacity-(size_t)n,"|W%u:%u:%016llx",k,pfslots[k].frame_state,(unsigned long long)pfslots[k].frame_hash);
        if(more<=0 || (size_t)(n+more)>=capacity) return 0;
        n+=more;
    }
    return n;
}
/* An abnormal stop: the call site back (a CAS, any thread once the frame is out of it). */
static void pfslot_restore(void) {
    if(!pfslot_armed) return;
    if(!pfslot_hook_uninstall()) pfslot_stage="hook_left";
}

/* ---- pins, arming, status ---- */
static int pfslot_pins(void) {
    if(!pfslot_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<PFSLOT_PIN_COUNT;i++) {
            if(pfslot_pin_table[i].sha[0]=='-') ok=!memcmp(image+pfslot_pin_table[i].rva,pfslot_handler_bytes,sizeof(pfslot_handler_bytes));
            else ok=hash_ok(image+pfslot_pin_table[i].rva,pfslot_pin_table[i].size,pfslot_pin_table[i].sha);
            if(!ok) pin_failed_rva=pfslot_pin_table[i].rva;
        }
        /* The call the stub replaces: call d7f8d0 at 9fe604. */
        if(ok && !(ok=image[PFSLOT_CALL_SITE]==0xe8 && (unsigned)(PFSLOT_CALL_SITE+5u+*(const int32_t*)(image+PFSLOT_CALL_SITE+1))==PFSLOT_FPC_UPDATE))
            pin_failed_rva=PFSLOT_CALL_SITE;
        pfslot_pins_checked=ok?1:-1;
    }
    return pfslot_pins_checked==1;
}
static void pfslot_use_image(void) {
    pfslot_fn.may_stand=(unsigned char(*)(void*))(image+PFSLOT_MAY_STAND);pfslot_fn.make_fpc=(void(*)(void*))(image+PFSLOT_FPC_MAKE);
    pfslot_fn.update=(void(*)(void*))(image+PFSLOT_FPC_UPDATE);pfslot_fn.pose=(void(*)(void*,float*,double,unsigned char))(image+PFSLOT_FPC_POSE);
    pfslot_fn.place=(void(*)(void*,void*,unsigned char))(image+PFSLOT_PLACE);pfslot_fn.conn_type=(int(*)(int))(image+PFSLOT_CONN_TYPE);
    pfslot_fn.ship_conn=(unsigned char(*)(void*,void*,int))(image+PFSLOT_SHIP_CONN);pfslot_fn.dock_conn=(unsigned char(*)(void*,void*,int))(image+PFSLOT_DOCK_CONN);
    pfslot_fn.place_tagged=(void(*)(void*,void*,void*,unsigned char))(image+PFSLOT_PLACE_TAGGED);
    pfslot_fn.ship_record=(void*(*)(void*,int))(image+PFSLOT_SHIP_RECORD);pfslot_fn.dock_record=(void*(*)(void*,int))(image+PFSLOT_DOCK_RECORD);
    pfslot_fn.tag_has=(unsigned char(*)(void*,const int*))(image+PFSLOT_TAG_HAS);
}
/* After gate.pilot_foot (and the stock slots), before the first tick; identical rerun 1. */
static int pfslot_arm_using(void) {
    if(pfslot_armed) return 1;
    if(!pfoot_armed) return pstk_arm_refuse("foot_slots_without_foot");
    if(!pslot_armed || !(pslot_mask&~1u)) return pstk_arm_refuse("foot_slots_without_slots");
    if(!pfslot_fn.may_stand || !pfslot_fn.make_fpc || !pfslot_fn.update || !pfslot_fn.pose || !pfslot_fn.place || !pfslot_fn.conn_type ||
       !pfslot_fn.ship_conn || !pfslot_fn.dock_conn || !pfslot_fn.place_tagged || !pfslot_fn.ship_record || !pfslot_fn.dock_record ||
       !pfslot_fn.tag_has) return pstk_arm_refuse("foot_slots_functions");
    memset(pfslots,0,sizeof(pfslots));pfslot_dying_skips=0;
    pfslot_failed=0;pfslot_hook_installed=0;pfslot_update_tick=0;pfslot_stage="armed";pfslot_why="-";pfslot_armed=1;return 1;
}
static int pfslot_arm(void) {
    if(!pfslot_armed) {
        if(stats.issued) return pstk_arm_refuse("foot_slots_after_first_tick");
        if(GetCurrentThreadId()!=ui_thread) return pstk_arm_refuse("thread");
        if(!pfslot_pins()) {snprintf(pstk_arm_why,sizeof(pstk_arm_why),"pins_foot_slots:%x",pin_failed_rva);return 0;}
        pfslot_use_image();
    }
    return pfslot_arm_using();
}
/* ---- the pilot chair's pins and arming (gate.pilot_chair, after gate.pilot_foot_slots) ---- */
static int pchair_pins(void) {
    if(!pchair_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<PCHAIR_PIN_COUNT;i++)
            if(!(ok=hash_ok(image+pchair_pin_table[i].rva,pchair_pin_table[i].size,pchair_pin_table[i].sha))) pin_failed_rva=pchair_pin_table[i].rva;
        /* The trigger table's entries: the name's address (relocated in the game, the file's base in a mapped copy) and
         * the id the posts use. */
        for(unsigned i=0;ok && i<sizeof(pchair_triggers)/sizeof(*pchair_triggers);i++) {
            uint64_t at=0;int32_t id=0;const size_t n=strlen(pchair_triggers[i].text)+1;
            memcpy(&at,image+pchair_triggers[i].entry,8);memcpy(&id,image+pchair_triggers[i].entry+8,4);
            ok=(at==(uint64_t)(uintptr_t)(image+pchair_triggers[i].name) || at==0x140000000ull+pchair_triggers[i].name) &&
               id==pchair_triggers[i].id && !memcmp(image+pchair_triggers[i].name,pchair_triggers[i].text,n);
            if(!ok) pin_failed_rva=pchair_triggers[i].entry;
        }
        pchair_pins_checked=ok?1:-1;
    }
    return pchair_pins_checked==1;
}
static void pchair_use_image(void) {
    pchair_fn.allocate=(void*(*)(size_t,uintptr_t,uintptr_t,uintptr_t,unsigned))(image+PCHAIR_ALLOCATE);
    pchair_fn.event=(void*(*)(void*,int,void*,unsigned char,unsigned char))(image+PCHAIR_EVENT);
    pchair_fn.post=(void(*)(void*,void*,double,unsigned char))(image+PCHAIR_POST);
    pchair_fn.character=(unsigned char*(*)(void*))(image+PCHAIR_CHARACTER);
    pchair_fn.trigger_id=(int(*)(uintptr_t,const void*))(image+PCHAIR_TRIGGER_ID);
}
/* After gate.pilot_foot_slots (its lookup and the tag), before the first tick; identical rerun 1. */
static int pchair_arm_using(void) {
    if(pchair_armed) return 1;
    if(!pfslot_armed) return pstk_arm_refuse("chair_without_foot_slots");
    if(!pchair_fn.allocate || !pchair_fn.event || !pchair_fn.post || !pchair_fn.character || !pchair_fn.trigger_id) return pstk_arm_refuse("chair_functions");
    memset(pchairs,0,sizeof(pchairs));pchair_digest=PCHAIR_DOMAIN;pchair_hold=0;
    pchair_failed=0;pchair_stage="live";pchair_why="-";pchair_armed=1;return 1;
}
static int pchair_arm(void) {
    if(!pchair_armed) {
        if(stats.issued) return pstk_arm_refuse("chair_after_first_tick");
        if(GetCurrentThreadId()!=ui_thread) return pstk_arm_refuse("thread");
        if(!pchair_pins()) {snprintf(pstk_arm_why,sizeof(pstk_arm_why),"pins_chair:%x",pin_failed_rva);return 0;}
        pchair_use_image();
    }
    return pchair_arm_using();
}
static int pchair_status(char *out,size_t capacity) {
    long long posted=0,untagged=0,skipped=0;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {posted+=pchairs[k].posted;untagged+=pchairs[k].untagged;skipped+=pchairs[k].skipped;}
    int n=snprintf(out,capacity," chair=%s/%lld/%lld/%lld/%016llx chair_why=%s",pchair_stage,posted,untagged,skipped,(unsigned long long)pchair_digest,pchair_why);
    for(unsigned k=2;n>0 && (size_t)n<capacity && k<=PILOT_SLOTS;k++) {
        const pchair_slot *c=&pchairs[k];
        if(!(pslot_mask>>(k-1)&1u)) continue;
        /* posted/untagged/skipped/instant/config: the seats and sit-downs that posted, found no tagged record, held (a joiner's
         * rebuild); the instant posts among them; the last chair config (hex) */
        int more=snprintf(out+n,capacity-(size_t)n," chair%u=%lld/%lld/%lld/%lld/%x",k,(long long)c->posted,(long long)c->untagged,(long long)c->skipped,
            (long long)c->instant,(unsigned)c->config);
        if(more<0) return 0;
        n+=more;
    }
    return n>0 && (size_t)n<capacity?n:0;
}
static int pfslot_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," foot_slots=%s foot_slots_why=%s foot_slots_hook=%d/%lld/%lld claimed=%lld dying_skips=%lld",pfslot_stage,pfslot_why,
        pfslot_hook_installed,(long long)pfslot_hook_calls,(long long)pfslot_late,(long long)pfslot_getups_claimed,(long long)pfslot_dying_skips);
    {   unsigned docked=0;   /* docked seats (mask) / takes after an undocking / sit-downs and get-ups in a docked ship */
        for(unsigned k=2;k<=PILOT_SLOTS;k++) if(pslots[k].seated && pslots[k].docked) docked|=1u<<(k-1);
        int more=n>0 && (size_t)n<capacity?snprintf(out+n,capacity-(size_t)n," docked=%x/%lld/%lld/%lld dock_inside=%lld reseat=%lld/%lld docked_seats=%lld",docked,(long long)pfslot_dock_takes,
            (long long)pfslot_dock_sits,(long long)pfslot_dock_getups,(long long)pfslot_dock_inside,(long long)pfslot_reseats,(long long)pfslot_foreign,(long long)pslot_docked_seats):-1;   /* answers 'docked' for a ship inside a container */
        if(more<0) return 0;
        n+=more;
        /* per docked seat: its ship's controller (vtable RVA), X4's own answer about its dock, the one-time check */
        for(unsigned k=2;k<=PILOT_SLOTS && n>0 && (size_t)n<capacity;k++) if(pslots[k].seated && pslots[k].docked && pslots[k].ship && pnat_base) {
            unsigned char *c=NULL,*dock=NULL;void *vt=NULL;int fault=0;
            if(read_local(pslots[k].ship+0x200,&c,8) && c) read_local(c,&vt,8);
            if(pvehk_poll_on) {
                __try {(void)((void*(*)(void*,void**))(void*)(pnat_base+PVEHK_DOCKED))(pslots[k].ship,(void**)&dock);}
                __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
            }
            more=snprintf(out+n,capacity-(size_t)n," dock%u=%llx/%d/%d",k,vt?(unsigned long long)((unsigned char*)vt-pnat_base):0ull,
                fault?-1:dock!=NULL,pslots[k].dock_checked);
            if(more<0) return 0;
            n+=more;
        }}
    for(unsigned k=2;n>0 && (size_t)n<capacity && k<=PILOT_SLOTS;k++) {
        pfslot_slot *f=&pfslots[k];
        if(!(pslot_mask>>(k-1)&1u)) continue;
        /* walking/getups/kept/stood/sat/ignored/updates/placed/unplaced/lost (lost: the ship went while it walked) */
        int more=snprintf(out+n,capacity-(size_t)n," fs%u=%d/%lld/%lld/%lld/%lld/%lld/%lld/%lld/%lld/%lld",k,f->walking,(long long)f->getups,(long long)f->kept,
            (long long)f->stood,(long long)f->sat,(long long)f->ignored,(long long)f->updates,(long long)f->placed,(long long)f->unplaced,(long long)f->lost);
        if(more<0) return 0;
        n+=more;
        if(f->walking && (size_t)n<capacity) {   /* the last receipt's halves: the bank, the FPC's pose */
            more=snprintf(out+n,capacity-(size_t)n," fw%u=%016llx/%016llx",k,(unsigned long long)f->frame_bank,(unsigned long long)f->frame_pose);
            if(more<0) return 0;
            n+=more;
        }
    }
    if(pchair_armed && n>0 && (size_t)n<capacity) {int more=pchair_status(out+n,capacity-(size_t)n);if(!more) return 0;n+=more;}
    return n>0 && (size_t)n<capacity?n:0;
}

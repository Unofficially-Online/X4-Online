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
 *   K2-5  the placement as 716e20 finds it (8d3f60, 455f90, 771430, class 0x4d), always the direct 717c30(P_k, target,
 *         1) on the initialised FPC, never the animation 7176f0; no target: it stands at its seat (counted unplaced).
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
 * Not here: MD's player.entity for slot k (no room events), the animation of the stand-up, its own contexts on other
 * clients (none), seeing P_k (its body: lockstep_pilot_foot_body.h parks every body while anyone is off its seat). */
#define PFSLOT_MAY_STAND 0x716d80u
#define PFSLOT_FPC_MAKE 0x70eee0u
#define PFSLOT_FPC_UPDATE 0xd7f8d0u
#define PFSLOT_FPC_POSE 0xd81ec0u
#define PFSLOT_PLACE 0x717c30u
#define PFSLOT_CONN_TYPE 0x8d3f60u
#define PFSLOT_SHIP_CONN 0x455f90u
#define PFSLOT_DOCK_CONN 0x771430u
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
} pfslot_fns;
typedef struct {
    unsigned char bank[PFOOT_BANK_SIZE];                  /* slot k's walking bank while no scope of it is open */
    unsigned char *fpc;
    int walking;
    uint64_t stood_tick,frame_hash;
    unsigned char frame_state;
    volatile LONG64 getups,kept,stood,sat,ignored,updates,placed,unplaced,lost;
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
    {0x7172dau,0xce,"226d36091a9a390a1a7160d252042b0320751337051b8ca18d174d4e12c644fc"},    /* 716e20's placement and control position none */
    {0xd7d2e0u,0x84,"bb97323466584f509d5a158283f79a6a71e421b05b9a6593f1ac88ea64d7c5be"},    /* in its seat: the camera, a control position */
    {0x706cc0u,0x39,"-"}};                                                                    /* GetUpEvent's handler (bytes checked below) */
#define PFSLOT_PIN_COUNT (sizeof(pfslot_pin_table)/sizeof(*pfslot_pin_table))
/* 706cc0: 716d80, the conversation manager idle, then 716e20(player, 1, 0). */
static const unsigned char pfslot_handler_bytes[0x39]={0x48,0x3b,0xd1,0x75,0x33,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0xae,0x00,0x01,0x00,
    0x84,0xc0,0x74,0x1d,0x48,0x8b,0x05,0x53,0x75,0x1e,0x03,0x83,0xb8,0xa0,0x01,0x00,0x00,0x01,0x75,0x0d,0x45,0x33,0xc0,0xb2,0x01,0x48,0x8b,0xcb,
    0xe8,0x2d,0x01,0x01,0x00,0x48,0x83,0xc4,0x20,0x5b,0xc3};
static int pfslot_fail(const char *why) {
    pfslot_stage="failed";if(!strcmp(pfslot_why,"-")) pfslot_why=why;
    pfslot_failed=1;pfoot_failed=1;pfoot_why=pfslot_why;return 0;   /* stop reason 185 */
}
static void pfslot_async(const char *why) {pfslot_fail(why);pnat_async_fail(why);}

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
/* K2-5, in scope k with this client's FIRST_PERSON flags held, after the FPC's first update: X4's target, the direct
 * placement; 0 = none found (P_k stands where the first update put it, at its seat). */
static int pfslot_place(pslot_slot *s) {
    unsigned char *ship=pfslot_ancestor(s->player,PFSLOT_SHIP_CLASS),*dock=pfslot_ancestor(s->player,PFSLOT_DOCK_CLASS);
    if(!dock && ship) {unsigned char *room=NULL;if(read_local(ship+PSLOT_ROOM,&room,8) && room && pfslot_is(room,PSLOT_ROOM_CLASS,0x11e8u)) dock=room;}
    int position=0,type=PFSLOT_CONN_NONE;
    memcpy(&position,s->player+PFSLOT_POSITION,4);
    if(ship && dock) type=pfslot_fn.conn_type(position);
    void *out[2]={NULL,NULL};
    if(type!=PFSLOT_CONN_NONE) {
        if(!pfslot_fn.ship_conn(ship,out,type)) {out[0]=out[1]=NULL;if(!pfslot_fn.dock_conn(dock,out,type)) out[0]=out[1]=NULL;}
    }
    if(out[0] && out[1] && pfslot_is(out[0],PFSLOT_TARGET_CLASS,0x11e0u)) {pfslot_fn.place(s->player,out[0],1);return 1;}
    return 0;
}

/* ---- the stand-up and the sit-down (pstk_slot_replay, slot k's GET_UP, in scope k) ---- */
static int pfslot_sit_down(unsigned k) {
    pslot_slot *s=&pslots[k];pfslot_slot *f=&pfslots[k];void **vtable=NULL,*take=NULL,*controller=NULL,*cvt=NULL,*back=NULL;
    int fault=0,position=0;
    if(!read_local(s->ship,&vtable,8) || !vtable || !read_local(vtable+PSLOT_TAKE,&take,8) || take!=(void*)pslot_fn.take) return pfslot_fail("slot_take_vtable");
    if(pslot_controller_has(s->ship,PSLOT_CONTROLLER_CLASS,NULL)!=0) {InterlockedIncrement64(&f->ignored);return 1;}   /* the ship is held */
    if(!read_local(pnat_base+PSLOT_PILOT_POSITION,&position,4) || !position) return pfslot_fail("slot_pilot_position");
    memcpy(s->player+PFSLOT_POSITION,&position,4);
    __try {((void(*)(void*))take)(s->ship);controller=pnat_fn.controller(pnat_base+PNAT_CONTROLLER_KEY);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    if(fault) return pfslot_fail("slot_take_fault");
    if(!controller || !read_local(controller,&cvt,8) || cvt!=(void*)(pnat_base+PSLOT_VTABLE) || !read_local((unsigned char*)controller+0x28,&back,8) ||
       back!=s->ship || pslot_controller_has(s->ship,0,controller)!=1) return pfslot_fail("slot_take_controller");
    if(InterlockedCompareExchangePointer((void*volatile*)controller,&s->vt.slots[0],pnat_base+PSLOT_VTABLE)!=(void*)(pnat_base+PSLOT_VTABLE))
        return pfslot_fail("slot_controller_vtable");
    s->controller=controller;pslot_set_routes(s,1);
    f->fpc[PFSLOT_FPC_SEAT]=1;pnat_base[PFOOT_ACTIVE]=0;s->player[PFOOT_PLAYER_FLAG]=0;
    f->walking=0;s->walking=0;InterlockedIncrement64(&f->sat);
    return 1;
}
static int pfslot_get_up(unsigned k,double now) {
    pslot_slot *s=k>=2 && k<=PILOT_SLOTS?&pslots[k]:NULL;pfslot_slot *f=s?&pfslots[k]:NULL;
    if(!s || !pfslot_armed || pstk_scope_slot!=k || !pslot_frame_thread()) return pfslot_fail("slot_scope");
    InterlockedIncrement64(&f->getups);
    if(f->walking) return pfslot_sit_down(k);
    if(!s->seated || !s->controller || pilot_slots[k-1].dead) {InterlockedIncrement64(&f->ignored);return 1;}
    unsigned char may=0,*conversation=NULL;int fault=0,state=0;
    /* K2-2: as the GetUpEvent's handler asks (706cc0): the conversation manager idle, then 716d80. */
    if(read_local(pnat_base+PFSLOT_CONVERSATION,&conversation,8) && conversation && read_local(conversation+0x1a0,&state,4) && state!=1) {
        InterlockedIncrement64(&f->kept);return 1;
    }
    __try {may=pfslot_fn.may_stand(s->player);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    if(fault) return pfslot_fail("slot_may_stand_fault");
    if(!may) {InterlockedIncrement64(&f->kept);return 1;}
    /* K2-3: the seat's controller released in this scope, as pslot_release does at stop+1. */
    void **vtable=NULL,*release=NULL;
    if(!read_local(s->ship,&vtable,8) || !vtable || !read_local(vtable+PSLOT_RELEASE,&release,8) || release!=(void*)pslot_fn.release)
        return pfslot_fail("slot_release_vtable");
    if(InterlockedCompareExchangePointer((void*volatile*)s->controller,pnat_base+PSLOT_VTABLE,&s->vt.slots[0])!=(void*)&s->vt.slots[0])
        return pfslot_fail("slot_controller_vtable");
    __try {((void(*)(void*))release)(s->ship);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    s->controller=NULL;
    if(fault) return pfslot_fail("slot_release_fault");
    /* K2-4, K2-5: the FPC, its first update at the seat (as X4's own player's FPC had it long before it stands: the
     * placement then poses an initialised FPC), the placement, all with this client's FIRST_PERSON flags held. */
    volatile unsigned char *requested=pnat_base+PFOOT_FP_REQUESTED;volatile uint32_t *share=(volatile uint32_t*)(pnat_base+PFOOT_COUNTER);
    unsigned char was_requested=*requested;uint32_t was_share=*share;unsigned char *fpc=NULL;int placed=0;
    __attribute__((aligned(16))) float pose[16];
    *requested=1;*share=0;
    __try {
        pfslot_fn.make_fpc(s->player);fpc=pfslot_fpc_of(s->player);
        if(fpc) {if(!fpc[PFSLOT_FPC_READY]) pfslot_fn.pose(fpc,pose,now,1);placed=pfslot_place(s);}
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
    InterlockedIncrement64(placed?&f->placed:&f->unplaced);
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
    return pfslot_armed && k>=2 && k<=PILOT_SLOTS && pslots[k].seated && pslots[k].controller && !pfslots[k].walking && !pilot_slots[k-1].dead;
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
/* K2-8: a walking slot's FPC is still its movement controller, its parent still its room. */
static int pfslot_check(unsigned k) {
    pslot_slot *s=&pslots[k];pfslot_slot *f=&pfslots[k];unsigned char *parent=NULL;int refs=0;
    if(!s->seated || !s->walking) return pfslot_fail("slot_unseated");
    if(pfslot_fpc_of(s->player)!=f->fpc) return pfslot_fail("slot_fpc_lost");
    if(!read_local(s->player+0x70,&parent,8) || parent!=s->room) return pfslot_fail("slot_moved");
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
        f->frame_state=(unsigned char)f->walking;f->frame_hash=0;
        if(!f->walking) continue;
        uint64_t h=PFSLOT_DOMAIN+k;
        for(unsigned i=0;i<PFOOT_BANK_SIZE;i++) if(pfslot_bank_byte(i)) {h^=f->bank[i];h*=0x100000001b3ULL;}
        __attribute__((aligned(16))) float pose[16];memset(pose,0,sizeof(pose));
        __try {pfslot_fn.pose(f->fpc,pose,now,1);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {memset(pose,0xff,sizeof(pose));}
        for(unsigned i=0;i<16;i++) if((i&3)!=3) {uint32_t v;memcpy(&v,&pose[i],4);for(unsigned b=0;b<4;b++) {h^=(unsigned char)(v>>(8*b));h*=0x100000001b3ULL;}}
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
}
/* After gate.pilot_foot (and the stock slots), before the first tick; identical rerun 1. */
static int pfslot_arm_using(void) {
    if(pfslot_armed) return 1;
    if(!pfoot_armed) return pstk_arm_refuse("foot_slots_without_foot");
    if(!pslot_armed || !(pslot_mask&~1u)) return pstk_arm_refuse("foot_slots_without_slots");
    if(!pfslot_fn.may_stand || !pfslot_fn.make_fpc || !pfslot_fn.update || !pfslot_fn.pose || !pfslot_fn.place || !pfslot_fn.conn_type ||
       !pfslot_fn.ship_conn || !pfslot_fn.dock_conn) return pstk_arm_refuse("foot_slots_functions");
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
static int pfslot_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," foot_slots=%s foot_slots_why=%s foot_slots_hook=%d/%lld/%lld claimed=%lld dying_skips=%lld",pfslot_stage,pfslot_why,
        pfslot_hook_installed,(long long)pfslot_hook_calls,(long long)pfslot_late,(long long)pfslot_getups_claimed,(long long)pfslot_dying_skips);
    for(unsigned k=2;n>0 && (size_t)n<capacity && k<=PILOT_SLOTS;k++) {
        pfslot_slot *f=&pfslots[k];
        if(!(pslot_mask>>(k-1)&1u)) continue;
        /* walking/getups/kept/stood/sat/ignored/updates/placed/unplaced/lost (lost: the ship went while it walked) */
        int more=snprintf(out+n,capacity-(size_t)n," fs%u=%d/%lld/%lld/%lld/%lld/%lld/%lld/%lld/%lld/%lld",k,f->walking,(long long)f->getups,(long long)f->kept,
            (long long)f->stood,(long long)f->sat,(long long)f->ignored,(long long)f->updates,(long long)f->placed,(long long)f->unplaced,(long long)f->lost);
        if(more<0) return 0;
        n+=more;
    }
    return n>0 && (size_t)n<capacity?n:0;
}

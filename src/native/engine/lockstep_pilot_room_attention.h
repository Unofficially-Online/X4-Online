/* ---- A player's own room at X4's own player's attention (native_gate_pilot_slots_room_attention, a session default) ----
 * Players 2..N's autopilot ended by itself, unheard, at some zone changes (the user's rk7 flights 3 and 4, rk8 flights 6
 * and 7: the drop out of travel, the gate and the arrival went unheard; the ship cruised on until Shift+A). Repro: the
 * mission sweep gm:GM_DeployInPlace for both players (runs 20261124-165029, -175347, -183400), always at tick 3280 with
 * the PILOT_AI rows (diagnostics on contrib/ferrite-p2-autopilot): a zone change of ship 2 set its cockpit's attention 9 -> 5 -> 9 within the tick. The cockpit passed
 * each level down to P_2 and on to its ship computer (PILOT_AI_ATTN, chain 83afe8 -> 3d6fc2 -> SetAttention 3d5660 ->
 * the room's notifier 76e6da -> the propagation 3d5880 -> P_2 -> its computer). move.autopilot runs only from attention
 * visible, so its stack heard its entity's change (0x1062e30), AIDirector::Move switched the context to the level-5
 * block, which holds nothing, and the script ended without its own end (PILOT_AI_STACK_END, chain 106e629): autopilot on,
 * nobody listening.
 * What X4 does for its own player (9.00): a room's attention (0x76e520, slot 703 of U::Room, U::Cockpit and
 * U::ControlRoom) is inroom (11) when the room is an ancestor of X4's player (Universe +0x230), else 10 on that player's
 * ship, else what its ship has; an entity (0x753350) takes the attention of its first class 0x16 ancestor, and the
 * propagation hands an entity child its parent's level as it is. X4's player sits in an inroom room, so its computer
 * stays at 11 through every zone change (PILOT_AI_WATCH: slot 1's computer 11, slot 2's 9).
 * Here: a room that is an ancestor of a seated slot 2..N's player (P_k +0x70 upwards: its seat, or the room it walks in)
 * is inroom too, as X4 makes its own player's. The other rooms of that ship keep X4's answer (no adjacent-room rule for
 * P_k). Every input is simulation state (the slots, P_k's parent chain), the same on every client.
 * native_gate_view_points (a session default) rewrites the compute's g_Universe load (0x76e572) and SetAttention's call of
 * slot 703 (0x3d567d, to view_attention, which runs the object's slot 703 once per view point and keeps the highest): the
 * hook here is that slot, so view points ask it too. The pins read those two sites as X4's (view_as_original).
 * Hooks: the three vtable entries of slot 703 (data, compare-and-swap), pinned with the compute's bytes and the entries,
 * installed at the seat with the slots' hooks and restored with them. The pass (prat_pass, frame thread, no scope open):
 * a seated slot's room below inroom is recomputed once by X4's own SetAttention (0x3d5660(room, 1), pinned; only a room
 * X4 itself recomputes, +0xd1 set), so the player and its computer start inroom at the seat and wherever it walks.
 * Status rat=installed:asked/raised/recomputed rat_why=- (or what refused: off, pins:<rva>, entry). */
#define PRAT_ROOM_COMPUTE 0x76e520u          /* U::Room::vfunc703: a room's attention */
#define PRAT_ROOM_COMPUTE_SIZE 0x180u
#define PRAT_ROOM_COMPUTE_SHA "61ccd72154b8d25d246bb3832ae056b500c6c40ed942ed9e44db05b8841df146"
#define PRAT_SET_ATTENTION 0x3d5660u         /* SetAttention(component, level): level 1 recomputes (slot 703) when +0xd1 is set */
#define PRAT_SET_ATTENTION_SIZE 0x64u        /* its first chunk, as lockstep_view_points.h pins it (the join swaps its two posts) */
#define PRAT_SET_ATTENTION_SHA "8e948a0788bbacca91415cb51e228e73bc416c608d2398b07f05312fb19b56eb"
#define PRAT_SLOT 703u
#define PRAT_INROOM 11
#define PRAT_ATTENTION 0x1ecu                /* Component: its attention level */
#define PRAT_LIVE 0xd1u                      /* Component: X4 recomputes its attention (3d5671) */
#define PRAT_PARENT 0x70u
#define PRAT_DEPTH 32u
typedef int32_t (*prat_compute_fn)(void *room);
/* lockstep_view_points.h: [rva, rva+size) at base with its own patched sites read as X4's; 0 a site holds anything else. */
static int view_as_original(const unsigned char *base,unsigned rva,unsigned size,unsigned char *copy);
typedef void (*prat_set_fn)(void *component,int level);
/* U::Room, U::Cockpit, U::ControlRoom: every room class whose slot 703 is 0x76e520 and whose notifier is the room's
 * (U::DockingBay shares the compute, not the notifier, and holds no player). */
static const uint32_t prat_vtables[]={0x2b26a78u,0x2aa0628u,0x2a8adf8u};
#define PRAT_VTABLES (sizeof(prat_vtables)/sizeof(*prat_vtables))
static int prat_on;                          /* native_gate_pilot_slots_room_attention */
static int prat_installed;
static const char *prat_why="-";
static unsigned prat_failed_rva;
static prat_compute_fn prat_compute_override;   /* tests: in place of X4's compute */
static prat_set_fn prat_set_override;           /* tests: in place of X4's SetAttention */
static volatile LONG64 prat_asked,prat_raised,prat_recomputed;
static void **prat_entry(unsigned i) {
    return (void**)(pnat_base+prat_vtables[i]+8u*PRAT_SLOT);
}
/* The seated slot 2..N whose player has this room among its ancestors, 0 none. */
static unsigned prat_holder(const unsigned char *room) {
    if(!room) {
        return 0;
    }
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        const pslot_slot *s=&pslots[k];
        unsigned char *at=NULL;
        if(!s->player || !s->seated || !read_local(s->player+PRAT_PARENT,&at,8)) {
            continue;
        }
        for(unsigned depth=0;at && depth<PRAT_DEPTH;depth++) {
            if(at==room) {
                return k;
            }
            if(!read_local(at+PRAT_PARENT,&at,8)) {
                break;
            }
        }
    }
    return 0;
}
/* Slot 703 of the room classes: X4's answer, or inroom for a room a slot's player is in. */
static int32_t prat_room_attention(void *room) {
    prat_compute_fn original=prat_compute_override;
    if(!original) {
        original=(prat_compute_fn)(void*)(pnat_base+PRAT_ROOM_COMPUTE);
    }
    const int32_t level=original(room);
    InterlockedIncrement64(&prat_asked);
    if(level<PRAT_INROOM && prat_holder((const unsigned char*)room)) {
        InterlockedIncrement64(&prat_raised);
        return PRAT_INROOM;
    }
    return level;
}
/* 1 when this component is one of the hooked room classes (by its vtable). */
static int prat_is_room(const unsigned char *component) {
    uintptr_t vtable=0;
    if(!component || !read_local(component,&vtable,8)) {
        return 0;
    }
    for(unsigned i=0;i<PRAT_VTABLES;i++) {
        if(vtable==(uintptr_t)(pnat_base+prat_vtables[i])) {
            return 1;
        }
    }
    return 0;
}
/* The pass (pslot_pass, live window, frame thread, no scope open): each seated slot's room below inroom is recomputed by
 * X4, once it answers inroom it is left alone. */
static void prat_pass(void) {
    if(!prat_installed) {
        return;
    }
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        const pslot_slot *s=&pslots[k];
        unsigned char *room=NULL;
        int32_t level=PRAT_INROOM;
        unsigned char live=0;
        if(!s->player || !s->seated || !read_local(s->player+PRAT_PARENT,&room,8) || !prat_is_room(room) ||
           !read_local(room+PRAT_ATTENTION,&level,4) || level>=PRAT_INROOM || !read_local(room+PRAT_LIVE,&live,1) || !live) {
            continue;
        }
        prat_set_fn set=prat_set_override;
        if(!set) {
            set=(prat_set_fn)(void*)(pnat_base+PRAT_SET_ATTENTION);
        }
        set(room,1);
        InterlockedIncrement64(&prat_recomputed);
    }
}
/* The compute's and SetAttention's bytes, and the three entries (X4's, or this hook). 1 fine; 0 with the failed RVA. */
static int prat_pins_at(const unsigned char *base,unsigned *failed_rva) {
    static unsigned char copy[PRAT_ROOM_COMPUTE_SIZE];
    *failed_rva=0;
    if(!base || !view_as_original(base,PRAT_ROOM_COMPUTE,PRAT_ROOM_COMPUTE_SIZE,copy) ||
       !hash_ok(copy,PRAT_ROOM_COMPUTE_SIZE,PRAT_ROOM_COMPUTE_SHA)) {
        *failed_rva=PRAT_ROOM_COMPUTE;
        return 0;
    }
    if(!view_as_original(base,PRAT_SET_ATTENTION,PRAT_SET_ATTENTION_SIZE,copy) ||
       !hash_ok(copy,PRAT_SET_ATTENTION_SIZE,PRAT_SET_ATTENTION_SHA)) {
        *failed_rva=PRAT_SET_ATTENTION;
        return 0;
    }
    for(unsigned i=0;i<PRAT_VTABLES;i++) {
        void *entry=NULL;
        const uint32_t at=prat_vtables[i]+8u*PRAT_SLOT;
        if(!read_local(base+at,&entry,8) || (entry!=(void*)(base+PRAT_ROOM_COMPUTE) && entry!=(void*)prat_room_attention)) {
            *failed_rva=at;
            return 0;
        }
    }
    return 1;
}
/* gate.pilot_slots_room_attention(1) (lockstep_pilot_inputs_lua.h): the pins on the live image at the option's arming. */
static int prat_arm_check(const unsigned char *base,unsigned *failed_rva) {
    const int ok=prat_pins_at(base,failed_rva);
    if(!ok) {
        pin_failed_rva=*failed_rva;
    }
    return ok;
}
static int prat_entry_back(unsigned i) {
    return *(void*volatile*)prat_entry(i)==pnat_base+PRAT_ROOM_COMPUTE ||
           pnat_swap8(prat_entry(i),(void*)prat_room_attention,pnat_base+PRAT_ROOM_COMPUTE,1);
}
/* At the seat, with the slots' hooks (pslot_hook_install): all three entries or none. */
static void prat_install(void) {
    if(prat_installed) {
        return;
    }
    if(!prat_on) {
        prat_why="off";
        return;
    }
    if(!prat_pins_at(pnat_base,&prat_failed_rva)) {
        prat_why="pins";
        return;
    }
    for(unsigned i=0;i<PRAT_VTABLES;i++) {
        if(!pnat_swap8(prat_entry(i),pnat_base+PRAT_ROOM_COMPUTE,(void*)prat_room_attention,1)) {
            while(i--) {
                (void)prat_entry_back(i);
            }
            prat_why="entry";
            return;
        }
    }
    prat_installed=1;
    prat_why="-";
}
static int prat_restore(void) {
    if(!prat_installed) {
        return 1;
    }
    int back=1;
    for(unsigned i=PRAT_VTABLES;i--;) {
        back&=prat_entry_back(i);
    }
    if(back) {
        prat_installed=0;
    }
    return !prat_installed;
}
static int prat_intact(void) {
    if(!prat_installed) {
        return 1;
    }
    for(unsigned i=0;i<PRAT_VTABLES;i++) {
        if(*(void*volatile*)prat_entry(i)!=(void*)prat_room_attention) {
            return 0;
        }
    }
    return 1;
}
static void prat_reset(void) {
    prat_asked=0;
    prat_raised=0;
    prat_recomputed=0;
}
/* The status's part, after n bytes of it: the new length, 0 when it does not fit; nothing while the option is off. */
static int prat_status(char *out,size_t capacity,int n) {
    char why[32];
    if(n<=0 || (size_t)n>=capacity) {
        return 0;
    }
    if(!prat_on && !prat_installed) {
        return n;
    }
    if(!strcmp(prat_why,"pins")) {
        snprintf(why,sizeof(why),"pins:%x",prat_failed_rva);
    } else {
        snprintf(why,sizeof(why),"%s",prat_why);
    }
    const int more=snprintf(out+n,capacity-(size_t)n," rat=%d:%lld/%lld/%lld rat_why=%s",prat_installed,(long long)prat_asked,
        (long long)prat_raised,(long long)prat_recomputed,why);
    if(more<0 || (size_t)(n+more)>=capacity) {
        return 0;
    }
    return n+more;
}

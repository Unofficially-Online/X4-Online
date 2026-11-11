/* Slot 1 on foot, DEFAULT OFF, stop reason 185 (native_gate_pilot_foot; needs stock piloting,
 * lockstep_pilot_stock.h, and the UI guard for the docked menu's Get Up button). Report:
 * reports/LOCKSTEP-ON-FOOT-20261101.txt. The canonical player (slot 1 on every client) gets up and
 * walks with X4's own first-person path on every client, from slot 1's client's input delayed
 * through the stock record, as slot 1 flies.
 *
 * What X4 does [F, pinned 9.00]:
 *   input      9ed1f0 registers U::FirstPersonController's handlers in the input object's
 *              multimaps (+0x63e0): states FP_MOVE_FORWARD 0x1a .. FP_JUMP 0x24 and FP_RUN_FORWARD 0x57
 *              -> d7d3b0, ranges FP_YAW 0xd, FP_PITCH 0xe, FP_WALK 0xf, FP_STRAFE 0x10 and
 *              MOUSELOOK_YAW/PITCH 7/8 -> d7d5c0, action FP_RUN 0xdc -> d7d690. Each writes one
 *              process global of the walking bank 0x38f4e40..0x38f4e93 (a lock byte of MD's
 *              input locks 0x38f4db7/dc8/dcb/dcc first), as the PlayerController's handlers
 *              write the flight bank. Ranges 7/8 have a second handler, the camera's 122d970.
 *   context    9ed1f0 also pushes d7d6c0 into the input object's context callback vector
 *              (+0x10, next to the SPACE callback d88710): for INPUT_CONTEXT_FIRST_PERSON (0xd)
 *              it resets the bank (0x38f4e70 = -1.0, 0x38f4e7d = active, the rest zero), flags
 *              the singleton player (+0x6a0d) with its event (70ecf0 on, 70ed50 off) and copies
 *              0x38f4e7d to the input object's +0x9063 (this client's own).
 *   update     U::UniverseFrameUpdate 9fe5f7: 9fc670 finds the singleton player's movement
 *              controller of class 0x16 (U::FirstPersonController at +0x200) and d7f8d0 runs it
 *              on the bank, once per frame on the frame thread, as d95180 runs the
 *              PlayerController at 9fe2d7 (which 9fe2b4 skips while the player has none).
 *   get up     the GetUp export 1aba30 (the docked menu's button) and 71bac0's GET_UP case
 *              (71bd80) do the same: 716d80 (can stand up), no GetUpEvent pending (99a210,
 *              0xcf), then a U::GetUpEvent posted to the player at the thread's game time +
 *              0.01 s (99a3f0); the key also wants the conversation manager idle (+0x1a0 == 1).
 *   interact   FP_INTERACTION_MENU_MOUSECLICK 0x78 -> 71c640 -> 8c2830: what the camera pick
 *              (the player's +0x6c08..+0x6cd0, DEVICE: this client's camera) points at is used
 *              (a seat, a console, a person). FP_INTERACTION_MENU 0x17 -> af3e70: the UI's
 *              interact menu (its world changes are guarded or replayed Lua/FFI calls).
 *
 * With this module (slot 1, the stock record):
 *   walk ids   the FP states, ranges and FP_RUN above, and GET_UP 0x115: slot 1's are replay ids
 *              (recorded on its client, run through the stock handlers on every client at the
 *              apply tick); the handlers they may reach are d7d3b0, d7d5c0, d7d690 (and 71bac0).
 *              Slots 2..N: refused, unless native_gate_pilot_foot_slots (lockstep_pilot_foot_slots.h).
 *   mouselook  ranges 7/8 while this client's FIRST_PERSON context is on or slot 1 walks (the
 *              replayed state): locally every handler but d7d5c0 runs (the camera); slot 1's client
 *              records the value and the replay runs d7d5c0 only. Otherwise as before (local).
 *   interact   0x78 refused for every slot (the pick is this client's; not replayed yet).
 *   context    the vector entry d7d6c0 is swapped for pfoot_context: this client's own edge runs
 *              the input side only (+0x9063) and slot 1's client records it (SPACE kind, id 0xd);
 *              the replay runs d7d6c0 itself and puts this client's +0x9063 back.
 *   Get Up     the docked menu's GetUp() is claimed in the UI guard (entry GetUp, group flight):
 *              on slot 1's client it becomes the GET_UP key in the record and answers true;
 *              slots 2..N: the guard refuses (info line).
 *   pass       after a replayed GET_UP slot 1's player may have no PlayerController: walking
 *              (9fc550 answers none) is a valid pass state, with flight control 0 once the
 *              replayed SPACE edge says so (a grace of PFOOT_GRACE ticks covers the edge's delay);
 *              a PlayerController again on slot 1's ship is adopted (sat down). Slots 2..N then
 *              update the frame after (lockstep_pilot_slots.h, as with a dead slot 1).
 *   receipt    '|W<state>.<on>:<bank>' joins PSTOCK_1: the walking bank and the player's +0x6a0d
 *              hashed while slot 1 walks: a walk that took on one client only differs at once.
 * Not here (reports/LOCKSTEP-ON-FOOT-20261101.txt): slots 2..N on foot (a body, a seat release and a
 * per-slot walking bank and update in their scopes), interactions through the pick (sit down, talk
 * on foot, consoles), transporter and teleport (guarded), MD's player.entity for slots 2..N. */
#define PFOOT_STOP_REASON 185u
#define PFOOT_CONTEXT 0xdu                /* INPUT_CONTEXT_FIRST_PERSON */
#define PFOOT_CONTEXT_RVA 0xd7d6c0u       /* its callback in the input object's context vector */
#define PFOOT_STATE_HANDLER 0xd7d3b0u
#define PFOOT_RANGE_HANDLER 0xd7d5c0u
#define PFOOT_ACTION_HANDLER 0xd7d690u
#define PFOOT_GET_UP 0x115u               /* INPUT_ACTION_GET_UP */
#define PFOOT_PICK_CLICK 0x78u            /* INPUT_STATE_FP_INTERACTION_MENU_MOUSECLICK */
#define PFOOT_BANK 0x38f4e40u             /* the walking bank, through 0x38f4e93 */
#define PFOOT_BANK_SIZE 0x54u
#define PFOOT_ACTIVE 0x38f4e7du           /* the bank's FIRST_PERSON flag (d7d6c0) */
#define PFOOT_COUNTER 0x38f4e80u          /* a counter inside the bank that seating, conversations and others move: not hashed */
#define PFOOT_INPUT_FLAG 0x9063u          /* input object: this client's FIRST_PERSON context */
#define PFOOT_PLAYER_FLAG 0x6a0du         /* player: first person controls started (70ecf0 / 70ed50) */
#define PFOOT_GRACE 24u                   /* ticks after a transition without the flight control check */
#define PFOOT_LEAVE_TICKS 90u             /* a GET_UP X4 did not take (still seated) is over after this */
#define PFOOT_DOMAIN 0x50464f4f54314cULL  /* "PFOOT1L" */
#define PFOOT_FP_REQUESTED 0x38f4e7cu     /* the FPC's input contexts requested (d81d20): this client's own, not hashed */
#define PFOOT_FP_REFRESH_RVA 0xd81d20u    /* the FPC's context refresh, d9cc30's twin: FIRST_PERSON, FIRST_PERSON_INTERACTION */
#define PFOOT_CONTROLS_ENABLED 0x2f4b7e0u /* the PlayerController's controls enabled (d9cc30 reads it) */
enum {PFOOT_SEATED=0,PFOOT_LEAVING=1,PFOOT_WALKING=2};
enum {PFOOT_NONE=0,PFOOT_WALK_ID=1,PFOOT_SPLIT_ID=2,PFOOT_REFUSE_ID=3};
typedef void (*pfoot_context_fn)(int,unsigned char);
/* Defined later (lockstep_pilot_native.h, lockstep_pilot_foot_guard.h). */
static int pstk_arm_refuse(const char *why);
static char pstk_arm_why[48];
static unsigned pin_failed_rva;
static void pnat_async_fail(const char *why);
static int pfoot_claim(void);
static int pfoot_armed,pfoot_failed,pfoot_installed,pfoot_restored=1,pfoot_pins_checked;
static int pfoot_on;                      /* replayed: slot 1's FIRST_PERSON context (the last edge applied) */
static int pfoot_local_fp=-1;             /* this client's own FIRST_PERSON context (input side), -1 unknown */
static unsigned pfoot_state;
static uint64_t pfoot_leave_tick,pfoot_transition_tick;
static const char *pfoot_stage="off",*pfoot_why="-";
static pfoot_context_fn pfoot_context_original;  /* the image's d7d6c0, or a test's */
static const void *pfoot_handler_ptrs[3];  /* d7d3b0, d7d5c0, d7d690 (image, or a test's) */
static volatile LONG64 pfoot_edges_local,pfoot_edges_recorded,pfoot_edges_replayed,pfoot_split_local,pfoot_split_recorded,pfoot_split_replayed,
    pfoot_getups_claimed,pfoot_getups_replayed,pfoot_walk_passes,pfoot_adopted,pfoot_kept_seated,pfoot_refused_pick,pfoot_aim_off_seat;
static uint64_t pfoot_frame_hash;
static unsigned char pfoot_frame_flag;

/* Which of this module's ids (0: none). */
static unsigned pfoot_id(unsigned kind,int id) {
    if(kind==1u) {   /* PSTK_STATE */
        if((id>=0x1a && id<=0x24) || id==0x57) return PFOOT_WALK_ID;
        if(id==(int)PFOOT_PICK_CLICK) return PFOOT_REFUSE_ID;
    }
    if(kind==2u) {   /* PSTK_RANGE */
        if(id>=0xd && id<=0x10) return PFOOT_WALK_ID;
        if(id==7 || id==8) return PFOOT_SPLIT_ID;
    }
    if(kind==0u && (id==0xdc || id==(int)PFOOT_GET_UP)) return PFOOT_WALK_ID;   /* FP_RUN, GET_UP */
    return PFOOT_NONE;
}
/* Mouselook is slot 1's walking input while this client's FIRST_PERSON context is on or slot 1 walks. */
static int pfoot_split_now(void) {return pfoot_on || pfoot_local_fp==1;}
/* A handler a replayed walking id may reach (beside pstk_handler_ok's). */
static int pfoot_handler_ok(unsigned kind,int id,const void *handler) {
    if(!pfoot_armed || !handler || !pfoot_id(kind,id)) return 0;
    for(unsigned i=0;i<3;i++) if(handler==pfoot_handler_ptrs[i]) return 1;
    return 0;
}
/* The replay of a mouselook range runs the walking handler only (the camera's is presentation). */
static int pfoot_replay_skips(unsigned kind,int id,const void *handler) {
    return pfoot_armed && pfoot_id(kind,id)==PFOOT_SPLIT_ID && handler!=pfoot_handler_ptrs[1];
}
static void pfoot_fail_async(const char *why);
static int pfoot_fail(const char *why) {pfoot_why=why;pfoot_stage="failed";pfoot_failed=1;return 0;}
/* ---- the run's double-tap clock (W9; reports/LOCKSTEP-ON-FOOT-20261101.txt 14) ----
 * FP_MOVE_FORWARD pressed (d7d3b0's case 0x1a, d7d48f): e9f90 reads TimerBase's real time (0x6b60e40 under the time lock
 * 0x6b60e10), the bank keeps it in 0x38f4e70 and sets the run flag 0x38f4e79 when the press before is less than 0.4 s
 * (0x2cbe7c0) older [F]. Real time is the process's own: under the gate it advances by the session's fixed dt, but its value
 * is the boot's real time at tick 0 plus that (df2b: 5.679 s on client 1, 5.508 s on client 2). So slot 1's first replayed
 * forward press parted 0x38f4e70 and with it |W2.1 at its apply tick (df2b 3903, w1bfoot 2523), and two presses close to
 * 0.4 s apart could set the run flag on one client only. Every replayed call of the walking state handler now runs with the
 * time lock held and the real time set to the session's clock, the replay thread's game time (pnat_fn.time, the clock the
 * replay's other events read): the same on every client, advancing as real time does under the gate. The real time is put
 * back before the lock is left; X4 reads it only under that lock [F: every reference of 0x6b60e40 in the image sits between
 * EnterCriticalSection and LeaveCriticalSection of 0x6b60e10, FrameTick's writes included], so no other thread sees the
 * session clock. A CRITICAL_SECTION is recursive: e9f90 enters it again on this thread. Slots 2..N (their banks swapped in
 * their scopes) take the same path. */
#define PFOOT_TIME_LOCK 0x6b60e10u        /* TimerBase's lock (e9f90: EnterCriticalSection, LeaveCriticalSection) */
#define PFOOT_REAL_TIME 0x6b60e40u        /* TimerBase's real time (REAL_RVA) */
#define PFOOT_TAP_TIME 0x38f4e70u         /* the bank's last forward press (d7d4cb), -1.0 after a reset */
static CRITICAL_SECTION *pfoot_time_lock;  /* the image's (pfoot_use_image) or a test's; NULL in fixtures without one */
static volatile LONG64 pfoot_clock_calls;
/* A replayed call that takes the session clock: X4's walking state handler while the module is armed. */
static int pfoot_replay_clock(unsigned kind,const void *handler) {
    return pfoot_armed && kind==PSTK_STATE && handler && handler==pfoot_handler_ptrs[0];   /* d7d3b0 */
}
static void pfoot_replay_state(void *handler,int id,unsigned char active) {
    double now=0,real=0;
    if(!pnat_fn.time || !pnat_fn.time(&now)) {pfoot_fail_async("foot_clock");return;}
    CRITICAL_SECTION *lock=pfoot_time_lock;
    if(lock) EnterCriticalSection(lock);
    memcpy(&real,pnat_base+PFOOT_REAL_TIME,8);memcpy(pnat_base+PFOOT_REAL_TIME,&now,8);
    __try {((void(*)(int,unsigned char))handler)(id,active);}
    __finally {memcpy(pnat_base+PFOOT_REAL_TIME,&real,8);if(lock) LeaveCriticalSection(lock);}
    InterlockedIncrement64(&pfoot_clock_calls);
}
/* Slot 1 off its seat (a replayed GET_UP X4 may have taken, or walking): X4 then skips d95180 (9fe2b4) and slot 1's ship has
 * no player aim; lockstep_pilot_slots.h updates slots 2..N the frame after, pstk_aim answers X4's own. */
static int pfoot_walking(void) {return pfoot_armed && pfoot_state!=PFOOT_SEATED;}

/* ---- the bank and the receipt ---- */
static uint64_t pfoot_bank_hash(void) {
    unsigned char bank[PFOOT_BANK_SIZE],flag=0,*player=NULL,*universe=NULL;
    if(!pnat_base || !read_local(pnat_base+PFOOT_BANK,bank,sizeof(bank))) return 0;
    memset(bank+(PFOOT_COUNTER-PFOOT_BANK),0,4);
    bank[PFOOT_FP_REQUESTED-PFOOT_BANK]=0;   /* this client's own context requests (K1 below) */
    uint64_t h=PFOOT_DOMAIN;
    for(unsigned i=0;i<sizeof(bank);i++) {h^=bank[i];h*=0x100000001b3ULL;}
    if(read_local(pnat_base+0x3d198f8u,&universe,8) && universe && read_local(universe+0x230,&player,8) && player &&
       read_local(player+PFOOT_PLAYER_FLAG,&flag,1)) {h^=flag;h*=0x100000001b3ULL;}
    return h|1u;
}
/* The tick's receipt part (after the replay): hashed while slot 1 walks or its context is on. */
static void pfoot_frame(void) {
    pfoot_frame_flag=(unsigned char)(pfoot_state|(pfoot_on?4u:0u));
    pfoot_frame_hash=(pfoot_on || pfoot_state!=PFOOT_SEATED)?pfoot_bank_hash():0;
}
static int pfoot_frame_text(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"|W%u.%u:%016llx",pfoot_frame_flag&3u,(pfoot_frame_flag>>2)&1u,(unsigned long long)pfoot_frame_hash);
    return n>0 && (size_t)n<capacity?n:0;
}

/* ---- the FIRST_PERSON context edge ---- */
static unsigned char *pfoot_input(void) {unsigned char *input=NULL;return pnat_base && read_local(pnat_base+0x6cf1560u,&input,8)?input:NULL;}
static int pstk_record_event(unsigned kind,unsigned id,uint32_t value);  /* lockstep_pilot_stock.h */
static int pnat_is_live(void);
/* This client's own edge (the input system's callback vector): the input side only; slot 1's client records it. */
static void pfoot_context(int context,unsigned char active) {
    if(context!=(int)PFOOT_CONTEXT) return;            /* d7d6c0 handles FIRST_PERSON only */
    if(!pfoot_armed || !pnat_is_live()) {pfoot_context_original(context,active);return;}
    unsigned char *input=pfoot_input();
    if(!input) {pfoot_fail_async("foot_input");return;}
    /* X4 calls every entry of the vector (two or more, pfoot_install): the first call of an edge acts. */
    if(pfoot_local_fp==(active?1:0)) return;
    input[PFOOT_INPUT_FLAG]=active?1:0;pfoot_local_fp=active?1:0;
    InterlockedIncrement64(&pfoot_edges_local);
    if(pstk_capturing && pstk_capture_slot==1 && pstk_record_event(3u,PFOOT_CONTEXT,active?1u:0u)) InterlockedIncrement64(&pfoot_edges_recorded);
}
/* Slot 1's edge on every client at the apply tick: X4's callback, this client's +0x9063 kept. */
static void pfoot_context_replay(unsigned char active,uint64_t tick) {
    unsigned char *input=pfoot_input(),keep=input?input[PFOOT_INPUT_FLAG]:0;
    pfoot_context_original((int)PFOOT_CONTEXT,active);
    if(input) input[PFOOT_INPUT_FLAG]=keep;
    pfoot_on=active!=0;pfoot_transition_tick=tick;
    InterlockedIncrement64(&pfoot_edges_replayed);
}
/* Every replayed event of slot 1 passes here after it ran (pstk_frame). */
static void pfoot_note_replay(unsigned kind,unsigned id,uint64_t tick) {
    if(!pfoot_armed) return;
    if(kind==0u && id==PFOOT_GET_UP) {
        InterlockedIncrement64(&pfoot_getups_replayed);
        if(pfoot_state==PFOOT_SEATED) {pfoot_state=PFOOT_LEAVING;pfoot_leave_tick=tick;pfoot_transition_tick=tick;}
    }
    if(kind==2u && pfoot_id(kind,(int)id)==PFOOT_SPLIT_ID) InterlockedIncrement64(&pfoot_split_replayed);
}

/* ---- K1: this client's input contexts follow its own slot ----
 * The input contexts are one set per process [F]: the PlayerController requests SPACE, SHIP and FLIGHT (8, 0xf, 0x10,
 * requester 6) through d9cc30/d8d9d0 and withdraws them (1252850) when "the player has a controller" is false; the
 * FirstPersonController requests FIRST_PERSON and FIRST_PERSON_INTERACTION (0xd, 0xe, requester 5) through d81d20 and
 * withdraws them through d81ce0. X4 asks both about the canonical player, i.e. slot 1: when slot 1 gets up (716e20 ->
 * 70e0f0 -> d9cc30(0), d81d20(1)) or dies, every client loses its flight contexts and gains the walking ones, and
 * d9ccc0 (a menu closed) withdraws them again on every client while slot 1 walks. On client k that took slot k's
 * flight: the SPACE edge went into slot k's record (its flight control off on every client) and client k's keys
 * became walking ids. Now, on a client whose own slot is 2..N (foot armed): pstk_space refreshes with "this client's
 * slot holds its controls"; a SPACE off edge while it does (enabled, no menu share) is the canonical player's, so it
 * is not recorded, nor the on edge that answers it; every live pass puts both sets back to what this client's own
 * slot wants (d9cc30 and d81d20 with its answers: X4's own request and withdraw). Client 1 is X4's own: unchanged. */
static void *vpl_slot_controller(unsigned k);   /* lockstep_pilot_slots.h: slot k's PlayerController while seated */
static void (*pfoot_fp_refresh)(char);          /* d81d20, or a test's */
static int pfoot_k1_held;                       /* a foreign SPACE off edge was not recorded: nor its on edge */
static volatile LONG64 pfoot_k1_ignored,pfoot_k1_fixed;
static int pfoot_k1(void) {return pfoot_armed && pstk_capturing && pstk_capture_slot>=2 && pstk_capture_slot<=PILOT_SLOTS;}
/* This client's own slot k (2..N) holds its ship's controls: seated (its ship, or a suit). A dead slot keeps them as
 * before this module (lockstep_pilot_death.h: its keys are dropped, the respawn's edge relies on them). */
static int pfoot_own_seated(void) {
    unsigned k=pstk_capture_slot;
    if(k<2 || k>PILOT_SLOTS) return 0;
    return pilot_slots[k-1].dead || vpl_slot_controller(k)!=NULL;
}
/* This client's own slot k (2..N) walks (lockstep_pilot_foot_slots.h; never without native_gate_pilot_foot_slots). */
static int pfoot_own_walking(void);
static int pfslot_armed;static int pfslot_own_walking(void);
/* pstk_space's refresh answer on this client (X4's: the canonical player has a controller). */
static char pfoot_refresh_answer(char canonical) {return pfoot_k1()?(char)pfoot_own_seated():canonical;}
/* A SPACE edge on this client that is not to be recorded as its slot's (1), or one that is (0). */
static int pfoot_space_foreign(unsigned char active) {
    if(!pfoot_k1()) return 0;
    if(!active) {
        uint32_t share=*(volatile uint32_t*)(pnat_base+0x2f4b7e4u);
        if(pfoot_own_seated() && *(volatile unsigned char*)(pnat_base+PFOOT_CONTROLS_ENABLED) && !share) {
            pfoot_k1_held=1;InterlockedIncrement64(&pfoot_k1_ignored);return 1;
        }
        pfoot_k1_held=0;return 0;   /* a menu, the map, a lock: this client's own, recorded */
    }
    if(pfoot_k1_held) {pfoot_k1_held=0;InterlockedIncrement64(&pfoot_k1_ignored);return 1;}
    return 0;
}
/* Every live pass on a client of slot 2..N: both context sets as this client's own slot wants them.
 * The walking set and X4's count 0x38f4e80 (PFOOT_COUNTER): d81d20 requests FIRST_PERSON only while that count is zero, and
 * the count holds one for a seated player (70dc50 adds it when the player sits, 70e0f0 takes it back when it stands), next to
 * the conversation's and the menus'. On a client of slot k that one is the canonical player's seat, slot 1's: with it the
 * request never came while slot 1 sat, and a person on client k who stood up could not walk (the user's session of 11 Nov:
 * slot 2 up for 850 ticks, k1=0/0, no walking id in any of its records). So while this client's own slot walks and the
 * canonical player sits, the refresh sees the count without that one; everything else in it (a menu, a conversation on
 * this client) still withdraws the set. */
static int pfslot_canon_seated(void);   /* lockstep_pilot_foot_slots.h: X4's own player holds a control position */
static volatile LONG64 pfoot_k1_unseated;
static void pfoot_k1_reconcile(void) {
    if(!pfoot_k1() || !pnat_base) return;
    unsigned char flight=*(volatile unsigned char*)(pnat_base+0x2f4b829u),walking=*(volatile unsigned char*)(pnat_base+PFOOT_FP_REQUESTED);
    pnat_fn.refresh((char)pfoot_own_seated());
    if(pfoot_fp_refresh) {
        const char walks=(char)pfoot_own_walking();volatile uint32_t *count=(volatile uint32_t*)(pnat_base+PFOOT_COUNTER);
        const uint32_t was=*count;const int seat=walks && was && pfslot_canon_seated();
        if(seat) {*count=was-1u;InterlockedIncrement64(&pfoot_k1_unseated);}
        pfoot_fp_refresh(walks);
        if(seat) *count=was;
    }
    if(*(volatile unsigned char*)(pnat_base+0x2f4b829u)!=flight || *(volatile unsigned char*)(pnat_base+PFOOT_FP_REQUESTED)!=walking)
        InterlockedIncrement64(&pfoot_k1_fixed);
}

/* ---- the swap of the context vector entries (pnat's vector helpers) ----
 * X4 pushes d7d6c0 into the vector once at GameClient::Init (9ed1f0) and once more for every FirstPersonController
 * it constructs (d7c530, at a world load, a seat, a teleport); the destructor d7cc30 removes none [F]. A client with a
 * loaded world therefore holds two or more entries, and a different number than another client (each process's own
 * history). Live w1foot (smoke 76bbbb97): 'exactly one entry' refused the install at take+1, stop 185 at tick 37,
 * before the window. Every entry is swapped now, a later one at the next pass (an FPC built during the session);
 * pfoot_context acts once per edge (X4 calls each entry), the replay calls X4's callback once on every client. */
static unsigned pnat_vector_find(unsigned char *input,void *value,void ***where);
static int pnat_swap8(void *slot,void *expected,void *value,int protect);
static unsigned pfoot_entries;            /* the entries swapped (this client's own number) */
static volatile LONG64 pfoot_entries_late;/* swapped at a later pass: an FPC constructed during the session */
/* Every entry holding `from` becomes `to`: the number swapped, -1 if one could not be (the vector as pnat reads it). */
static int pfoot_swap_all(unsigned char *input,void *from,void *to) {
    void **begin=NULL,**end=NULL;int n=0;
    if(!input || !read_local(input+0x10,&begin,8) || !read_local(input+0x18,&end,8) || !begin || end<begin || end-begin>256) return -1;
    for(void **p=begin;p<end;p++) {
        void *entry=NULL;
        if(!read_local(p,&entry,8) || entry!=from) continue;
        if(!pnat_swap8(p,from,to,0)) return -1;
        ++n;
    }
    return n;
}
static int pfoot_install(void) {
    unsigned char *input=pfoot_input();
    if(pfoot_installed) return 1;
    if(!input || !pfoot_context_original || !pnat_vector_find(input,(void*)pfoot_context_original,NULL) ||
       pnat_vector_find(input,(void*)pfoot_context,NULL)) return 0;
    int n=pfoot_swap_all(input,(void*)pfoot_context_original,(void*)pfoot_context);
    if(n<1) {(void)pfoot_swap_all(input,(void*)pfoot_context,(void*)pfoot_context_original);return 0;}
    pfoot_entries=(unsigned)n;pfoot_installed=1;pfoot_restored=0;
    pfoot_local_fp=input[PFOOT_INPUT_FLAG]?1:0;
    return 1;
}
/* Every pass: an entry pushed since is swapped too; then none of X4's is left and at least one is ours. */
static int pfoot_intact(void) {
    unsigned char *input=pfoot_input();
    if(!pfoot_installed || !input) return 0;
    int more=pfoot_swap_all(input,(void*)pfoot_context_original,(void*)pfoot_context);
    if(more<0) return 0;
    if(more) {pfoot_entries+=(unsigned)more;InterlockedExchangeAdd64(&pfoot_entries_late,more);}
    return pnat_vector_find(input,(void*)pfoot_context,NULL)>=1 && !pnat_vector_find(input,(void*)pfoot_context_original,NULL);
}
static void pfoot_uninstall(void) {
    unsigned char *input=pfoot_input();
    if(!pfoot_installed) {pfoot_restored=1;return;}
    if(input && pfoot_swap_all(input,(void*)pfoot_context,(void*)pfoot_context_original)>=0 && !pnat_vector_find(input,(void*)pfoot_context,NULL)) {
        pfoot_installed=0;pfoot_restored=1;
    } else pfoot_restored=0;
}

/* ---- the pass (pnat_check, stock mode): 1 a walking pass (the controller checks are done), 0 the
 * seated checks apply, -1 failed (pfoot_why) ---- */
static int pfoot_check(uint64_t tick,unsigned char *controller,unsigned char **pnat_controller_io,unsigned char *pnat_ship_now,unsigned flight,
                       unsigned replayed_flight) {
    unsigned char *ship=NULL;
    if(!pfoot_armed) return 0;
    if(pfoot_state==PFOOT_SEATED) return 0;
    if(pfoot_state==PFOOT_LEAVING) {
        if(controller && controller==*pnat_controller_io) {
            if(tick>pfoot_leave_tick+PFOOT_LEAVE_TICKS) {pfoot_state=PFOOT_SEATED;InterlockedIncrement64(&pfoot_kept_seated);}
            return 0;                                  /* X4 has not taken the player out (yet): seated */
        }
        if(controller) {pfoot_fail("foot_controller");return -1;}
        /* X4 deleted the seat's PlayerController with the stand-up (716e20 -> 70e0f0): nothing may hold it now; a
         * controller on slot 1's ship again is adopted below. */
        pfoot_state=PFOOT_WALKING;pfoot_transition_tick=tick;*pnat_controller_io=NULL;
    }
    if(!controller) {
        /* Walking: flight control is the replayed SPACE state once its edge (capture + delay) is through. */
        if(tick>pfoot_transition_tick+PFOOT_GRACE && flight!=replayed_flight) {pfoot_fail("foot_flight_drift");return -1;}
        InterlockedIncrement64(&pfoot_walk_passes);pfoot_stage="walking";
        return 1;
    }
    /* A PlayerController again: slot 1 sat down in its own ship (adopted) or something else (stop). */
    if(!read_local(controller+0x28,&ship,8) || !ship || ship!=pnat_ship_now) {pfoot_fail("foot_controller");return -1;}
    *pnat_controller_io=controller;pfoot_state=PFOOT_SEATED;pfoot_transition_tick=tick;pfoot_stage="seated";
    InterlockedIncrement64(&pfoot_adopted);
    return 0;
}
/* lockstep_pilot_death.h: slot 1's ship died with the player still aboard while a replayed GET_UP waited for X4's
 * stand-up (leaving): the eject took the player into its suit, the GET_UP is void (counted as kept). */
static void pfoot_void_leave(uint64_t tick) {
    if(!pfoot_armed || pfoot_state!=PFOOT_LEAVING) return;
    pfoot_state=PFOOT_SEATED;pfoot_transition_tick=tick;InterlockedIncrement64(&pfoot_kept_seated);
}
/* Within the grace after a transition the flight control check of a seated pass waits for the replayed edge. */
static int pfoot_flight_grace(uint64_t tick) {
    return pfoot_armed && pfoot_transition_tick && tick<=pfoot_transition_tick+PFOOT_GRACE;
}

/* ---- pins, arming, status ---- */
static const struct {unsigned rva,size;const char *sha;} pfoot_pin_table[]={
    {0xd7d3b0u,0x202,"58d60fcacdfafd3774ced130c44131e71f4ca3a2e6b6f808bedabaf2dab77b11"},   /* FP state handler: the bank bytes, locks */
    {0xd7d5c0u,0xcc,"4b0c11eb1c5beb2c0374d749f7c31d7c1116114fbd7f6250edc377f068c66b2e"},    /* FP range handler (and MOUSELOOK 7/8) */
    {0xd7d690u,0x25,"1f705e75f9b1dd357870d6b9aa3f3353cdbdbb286e084da30ed8e075d8acee0f"},    /* FP_RUN */
    {0xd7d6c0u,0x11b,"8b2bdb8076d8482fce964d20814c1a37db65e594ed45027da14c41ff44ddcd76"},   /* FIRST_PERSON context: bank reset, +0x6a0d, +0x9063 */
    {0x9ed1f0u,0x1e9,"16fe9769e787fa69363180eb299f39f16cf23e06714eb705ff93ebfcb451337e"},   /* the registrations, the vector push */
    {0x1aba30u,0xee,"660225ab26c7338fed04323a4aa29276d76ea24ba91b7f01a0340eb8424fafad"},    /* GetUp: the GetUpEvent as 71bd80 posts it */
    {0x9fe5f7u,0x12,"af7021c000f35c075f5c978cd7468a5ed5cc41282fa1fe09286eb81a076e8737"},    /* UniverseFrameUpdate: 9fc670 -> d7f8d0 */
    {0x9fe29du,0x3a,"fc26060e914758cedd0dd5b6c530350a8c19e32929aed8cd1dcf403a51408f9c"},    /* ... d95180 skipped without a PlayerController */
    {0x9fc670u,0x71,"543cf545335045bfeaae3d2ff0613e2c38eba05eb8dcd6ab978177ab409c952e"},    /* the singleton's FirstPersonController (class 0x16) */
    {0xd81ce0u,0x1de,"af55f30ec7fe532266d7dbfaf7d8730a27803cbace6610a79846dca3360d1266"},   /* FPC contexts: withdraw d81ce0, refresh d81d20 (K1) */
    {0xe9f90u,0x39,"ae6892af6cb95a48d9756135fc891531d4f3aefceb69dc7b9f6d88a965f1cf19"},     /* the double-tap clock: real time under the time lock */
};
#define PFOOT_PIN_COUNT (sizeof(pfoot_pin_table)/sizeof(*pfoot_pin_table))
static int pfoot_pins(void) {
    if(!pfoot_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<PFOOT_PIN_COUNT;i++)
            if(!(ok=hash_ok(image+pfoot_pin_table[i].rva,pfoot_pin_table[i].size,pfoot_pin_table[i].sha))) pin_failed_rva=pfoot_pin_table[i].rva;
        pfoot_pins_checked=ok?1:-1;
    }
    return pfoot_pins_checked==1;
}
static void pfoot_use_image(void) {
    pfoot_context_original=(pfoot_context_fn)(image+PFOOT_CONTEXT_RVA);
    pfoot_handler_ptrs[0]=image+PFOOT_STATE_HANDLER;pfoot_handler_ptrs[1]=image+PFOOT_RANGE_HANDLER;pfoot_handler_ptrs[2]=image+PFOOT_ACTION_HANDLER;
    pfoot_fp_refresh=(void(*)(char))(image+PFOOT_FP_REFRESH_RVA);
    pfoot_time_lock=(CRITICAL_SECTION*)(image+PFOOT_TIME_LOCK);
}
/* Slots 2..N walk with native_gate_pilot_foot_slots (lockstep_pilot_foot_slots.h): client k then wants the walking
 * contexts while its own slot is on foot, and the flight contexts never (pfoot_own_seated: no controller). */
static int pfoot_own_walking(void) {return pfslot_own_walking();}
static int pfoot_reset_state(void) {
    pfoot_k1_held=0;pfoot_failed=0;pfoot_on=0;pfoot_local_fp=-1;pfoot_state=PFOOT_SEATED;pfoot_leave_tick=pfoot_transition_tick=0;
    pfoot_stage="armed";pfoot_why="-";pfoot_frame_hash=0;pfoot_frame_flag=0;return 1;
}
/* After gate.pilot_stock, before the first tick; identical rerun 1. */
static int pfoot_arm_using(void) {
    if(pfoot_armed) return 1;
    if(!pnat_armed || pnat_mode!=PNAT_MODE_STOCK) return pstk_arm_refuse("foot_without_stock");
    if(!pfoot_context_original || !pfoot_handler_ptrs[0]) return pstk_arm_refuse("foot_functions");
    if(!pfoot_claim()) return 0;   /* the docked menu's GetUp: this module's guard entry */
    pfoot_reset_state();pfoot_armed=1;return 1;
}
static int pfoot_arm(void) {
    if(!pfoot_armed) {
        if(stats.issued) return pstk_arm_refuse("foot_after_first_tick");
        if(GetCurrentThreadId()!=ui_thread) return pstk_arm_refuse("thread");
        if(!pfoot_pins()) {snprintf(pstk_arm_why,sizeof(pstk_arm_why),"pins_foot:%x",pin_failed_rva);return 0;}
        if(!pfoot_context_original) pfoot_use_image();
    }
    return pfoot_arm_using();
}
static int uiguard_armed_now(void);       /* lockstep_ui_guard.h */
static int pfoot_claim_ready(void);       /* lockstep_pilot_foot_guard.h: the GetUp entry is this module's */
/* The live pass's part (pnat_pass, stock mode, from the first live tick to the stop): the swap installed
 * and intact; the guard holds GetUp (else the docked menu's button would run X4's GetUp on one client). */
static int pfoot_pass(void) {
    if(!pfoot_armed) return 1;
    if(pfoot_failed) return 0;
    if(!uiguard_armed_now() || !pfoot_claim_ready()) return pfoot_fail("foot_needs_guard");
    if(!pfoot_installed) {if(!pfoot_install()) return pfoot_fail("foot_install");pfoot_stage="live";}
    else if(!pfoot_intact()) return pfoot_fail("foot_hook_lost");
    pfoot_k1_reconcile();   /* a client of slot 2..N: its own contexts (K1) */
    return 1;
}
static void pfoot_fail_async(const char *why) {pfoot_fail(why);pnat_async_fail(why);}
/* Diagnostic, read only (reports/LOCKSTEP-ON-FOOT-20261101.txt 10c): w1bfoot's |W2.1 bank hash agreed while slot 1 stood
 * (2460, 2520) and parted from the first walking input (2580), then each client kept its own bank at rest. Written with the
 * PILOT_INPUTS row (the status stride) while slot 1 is off its seat or its context is on:
 *   foot_bank=   the walking bank as the receipt hashes it (e7c and the counter e80 zeroed), 0x54 bytes;
 *   foot_fpc=    the canonical player's U::FirstPersonController blocks d7f8d0 integrates: +0x20..+0x60 (head),
 *                +0x80..+0xe0 (first-update flag, last update time +0x88, the look and move fields +0x90..+0xdc),
 *                +0x100..+0x1a0 (its transforms), +0x2e0; '-' when the controller is another class;
 *   foot_parent= the player's parent's portable key and class (the room it stands in);
 *   foot_locals= every client-local value d7f8d0 and its callees read [F]: ext (9fc0e0: Universe+0x3d0 +0x868, d7f8d0 then
 *                skips the head and look step), cut (the cutscene manager's camera +0xb0: d7f8d0 skips the update),
 *                dev (d7f0c0's device branch: input+0x9053, input+0x64a0 set, *0x6cf1578, [*0x6cf1568+8]+0x197),
 *                sens (d7ed80: input+0x6380 sensitivities of FP_YAW 0xd and FP_PITCH 0xe, '-' when unset), view (0x38f4ee0,
 *                the MoveWorker's view-cache read bank, object+0xe0+view*0x80: a double-buffer parity per process by design,
 *                not the slot a client presents; no instruction of the FPC's code d7c000..d83000, d7f8d0 and the handlers
 *                included, references it [F]).
 * Two clients must print the same bank and FPC blocks; a local value that differs names an input of the walk. Per client by
 * design in the FPC blocks (df2b): +0x83..+0x87 (padding after the two flags) and the pointer at +0xa8 (set on client 1
 * only; no read of it found in d7f8d0 or its local callees). df2b's only difference in the bank was 0x38f4e70, the double-tap clock
 * (pfoot_replay_state above). */
#define PFOOT_FPC_VTABLE 0x2c3eec0u
static int pfoot_hex(char *out,size_t cap,const unsigned char *p,unsigned n) {
    static const char digits[]="0123456789abcdef";
    if(cap<2u*n+1u) return 0;
    for(unsigned i=0;i<n;i++) {out[2*i]=digits[p[i]>>4];out[2*i+1]=digits[p[i]&15];}
    out[2*n]=0;return (int)(2*n);
}
/* input+0x6380: X4's ordered map {range id (+0x1c) -> sensitivity (+0x20)}, header +0x6380, root at +8, left +8, right +0x10 (d7ed80). */
static int pfoot_sensitivity(unsigned char *input,int range,float *value) {
    unsigned char *header=input+0x6380,*node=NULL,*found=header;
    if(!read_local(header+8,&node,8)) return 0;
    for(unsigned steps=0;node && steps<64;steps++) {
        int key=0;if(!read_local(node+0x1c,&key,4)) return 0;
        unsigned char *next=NULL;
        if(key<range) {if(!read_local(node+0x10,&next,8)) return 0;}
        else {found=node;if(!read_local(node+8,&next,8)) return 0;}
        node=next;
    }
    int key=0;
    return found!=header && read_local(found+0x1c,&key,4) && key==range && read_local(found+0x20,value,4);
}
static int pfoot_diag(char *out,size_t cap) {
    if(!pnat_base || cap<1024) return 0;
    int n=0;unsigned char bank[PFOOT_BANK_SIZE];
    if(!read_local(pnat_base+PFOOT_BANK,bank,sizeof(bank))) return 0;
    memset(bank+(PFOOT_COUNTER-PFOOT_BANK),0,4);bank[PFOOT_FP_REQUESTED-PFOOT_BANK]=0;
    n+=snprintf(out+n,cap-(size_t)n," foot_bank=");n+=pfoot_hex(out+n,cap-(size_t)n,bank,sizeof(bank));
    unsigned char *universe=NULL,*player=NULL,*fpc=NULL,*parent=NULL;void *vtable=NULL;
    int have=read_local(pnat_base+0x3d198f8u,&universe,8) && universe && read_local(universe+0x230,&player,8) && player;
    if(have && read_local(player+0x200,&fpc,8) && fpc && read_local(fpc,&vtable,8) && vtable==(void *)(pnat_base+PFOOT_FPC_VTABLE)) {
        unsigned char a[0x40],b[0x60],c[0xa0],d=0;
        if(read_local(fpc+0x20,a,sizeof(a)) && read_local(fpc+0x80,b,sizeof(b)) && read_local(fpc+0x100,c,sizeof(c)) && read_local(fpc+0x2e0,&d,1)) {
            n+=snprintf(out+n,cap-(size_t)n," foot_fpc=");n+=pfoot_hex(out+n,cap-(size_t)n,a,sizeof(a));
            out[n++]='/';n+=pfoot_hex(out+n,cap-(size_t)n,b,sizeof(b));out[n++]='/';n+=pfoot_hex(out+n,cap-(size_t)n,c,sizeof(c));
            n+=snprintf(out+n,cap-(size_t)n,"/%02x",d);
        } else n+=snprintf(out+n,cap-(size_t)n," foot_fpc=unreadable");
    } else n+=snprintf(out+n,cap-(size_t)n," foot_fpc=-");
    uint64_t parent_id=0;unsigned klass=WORLD_KEY_NONE;uint64_t key=0;
    if(have && read_local(player+0x70,&parent,8) && parent && read_local(parent+8,&parent_id,8)) key=world_identity_key(parent_id,&klass);
    n+=snprintf(out+n,cap-(size_t)n," foot_parent=%016llx/%u",(unsigned long long)key,klass);
    unsigned char *camera=NULL,*manager=NULL,*cut=NULL,*input=pfoot_input(),*device=NULL,*flagp=NULL,*holder=NULL,*inner=NULL;
    uint32_t mode=0,view=0;unsigned char dev_on=0,flag=0,inner_flag=0;float yaw=0,pitch=0;
    int ext_ok=universe && read_local(universe+0x3d0,&camera,8) && camera && read_local(camera+0x868,&mode,4);
    int cut_ok=read_local(pnat_base+0x3d19900u,&manager,8);if(cut_ok && manager) cut_ok=read_local(manager+0xb0,&cut,8);
    int dev_ok=input && read_local(input+0x9053,&dev_on,1) && read_local(input+0x64a0,&device,8);
    int flag_ok=read_local(pnat_base+0x6cf1578u,&flagp,8) && (!flagp || read_local(flagp,&flag,1));
    int inner_ok=read_local(pnat_base+0x6cf1568u,&holder,8) && (!holder || (read_local(holder+8,&inner,8) && (!inner || read_local(inner+0x197,&inner_flag,1))));
    int view_ok=read_local(pnat_base+0x38f4ee0u,&view,4);
    char sy[24]="-",sp[24]="-";
    if(input && pfoot_sensitivity(input,0xd,&yaw)) snprintf(sy,sizeof(sy),"%.9g",yaw);
    if(input && pfoot_sensitivity(input,0xe,&pitch)) snprintf(sp,sizeof(sp),"%.9g",pitch);
    n+=snprintf(out+n,cap-(size_t)n," foot_locals=ext:%s/cut:%s/dev:",ext_ok?(mode?"1":"0"):"?",cut_ok?(cut?"1":"0"):"?");
    if(dev_ok && flag_ok && inner_ok) n+=snprintf(out+n,cap-(size_t)n,"%d,%d,%d,%d",dev_on!=0,device!=NULL,flagp?flag!=0:0,inner?inner_flag!=0:0);
    else n+=snprintf(out+n,cap-(size_t)n,"?");
    n+=snprintf(out+n,cap-(size_t)n,"/sens:%s,%s/view:%s",sy,sp,view_ok?(view?"1":"0"):"?");
    return n>0 && (size_t)n<cap?n:0;
}
static int pfoot_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," foot=%s foot_why=%s foot_state=%u foot_on=%d foot_local=%d edges=%lld/%lld/%lld mouselook=%lld/%lld/%lld getup=%lld/%lld walk=%lld adopted=%lld kept=%lld pick_refused=%lld aim_off_seat=%lld foot_restored=%d",
        pfoot_stage,pfoot_why,pfoot_state,pfoot_on,pfoot_local_fp,(long long)pfoot_edges_local,(long long)pfoot_edges_recorded,(long long)pfoot_edges_replayed,
        (long long)pfoot_split_local,(long long)pfoot_split_recorded,(long long)pfoot_split_replayed,(long long)pfoot_getups_claimed,(long long)pfoot_getups_replayed,
        (long long)pfoot_walk_passes,(long long)pfoot_adopted,(long long)pfoot_kept_seated,(long long)pfoot_refused_pick,(long long)pfoot_aim_off_seat,pfoot_restored);
    if(n>0 && (size_t)n<capacity)
        n+=snprintf(out+n,capacity-(size_t)n," foot_entries=%u/%lld k1=%lld/%lld foot_clock=%lld k1_fp=%d/%u/%lld",pfoot_entries,(long long)pfoot_entries_late,
            (long long)pfoot_k1_ignored,(long long)pfoot_k1_fixed,(long long)pfoot_clock_calls,
            pnat_base?(int)*(volatile unsigned char*)(pnat_base+PFOOT_FP_REQUESTED):-1,pnat_base?*(volatile uint32_t*)(pnat_base+PFOOT_COUNTER):0u,
            (long long)pfoot_k1_unseated);   /* this client's FIRST_PERSON request, X4's count, passes that left the canonical seat out */
    if(n>0 && (size_t)n<capacity && pfoot_armed && (pfoot_state!=PFOOT_SEATED || pfoot_on)) {
        int d=pfoot_diag(out+n,capacity-(size_t)n);if(d>0) n+=d;
    }
    return n>0 && (size_t)n<capacity?n:0;
}

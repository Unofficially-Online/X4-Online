/* Slots 2..N in X4's world tree, stage 1: a body at the seat (native_gate_pilot_foot_bodies = true, DEFAULT OFF;
 * needs native_gate_pilot_foot and stock slots 2..N; stop reason 185, why body_*). Report:
 * reports/LOCKSTEP-ON-FOOT-20261101.txt section 8, with the death module section 13.
 *
 * P_k (lockstep_pilot_slots.h) is X4's own U::Player built by the factory, seated through upward pointers only: its
 * parent +0x70 is slot k's cockpit room, but the room's child list does not hold it, so nothing that walks the tree
 * (the render walks, the census, the room's own walks) reaches it, and no client can draw slot k. X4 seats its own
 * player with 716670 [F]: 3d8420(player, &filter 2f60e70 = TemplateConnectionFilterByIsPlayerSlot, room, 0), i.e.
 * U::Component::ConnectTo (3d6e50) at the room's player slot connection, then the control position and 70eee0 (its
 * FirstPersonController). 3d8420 with no target is 3d8240(component, 0x15): off its parent without a kill [F].
 *
 * This stage connects P_k the same way, on every client, at the same step:
 *   take-1   after pslot_seat_all (the seat's upward pointers set): in scope k (singleton = P_k), the upward
 *            pointer cleared, 3d8420(P_k, filter, room, 0); P_k's parent must then be the room (else off again,
 *            the upward pointer back, stop body_connect). A roomless seat (no cockpit room) gets no body (roomless=).
 *   live     every pass: each body still has the room as its parent (body_moved), its seat still holds.
 *   stop+1   before the seats' release: in scope k, 3d8420(P_k, filter, NULL, 0), the upward pointer back (the seat's
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
 * everyone sits again: the tree only ever holds P_k in the state B1 ran (both seated). Status bodies=parked parks=. */
#define PFBODY_CONNECT 0x3d8420u            /* 3d8420(component, filter, target, 0) */
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
static volatile LONG64 pfbody_kill_off,pfbody_kill_spared,pfbody_kill_late;
static const struct {unsigned rva,size;const char *sha;} pfbody_pin_table[]={
    {0x3d8420u,0x89,"46a8f865b44c4e326737a70cfdf828bd65324779303b13ecaf81990f6290f7af"},    /* ConnectTo by filter; no target: off */
    {PFBODY_KILL_RVA,0x241,"ff09d24ee15551fb542937e99b6a8fe9fc502daa0fd98dfa0ed186279435b0fc"}};   /* KillRecursive (the kill guard) */
static int pfbody_fail(const char *why) {
    pfbody_stage="failed";if(!strcmp(pfbody_why,"-")) pfbody_why=why;
    pfbody_failed=1;pfoot_failed=1;pfoot_why=pfbody_why;return 0;   /* stop reason 185 */
}
static unsigned char *pfbody_parent(const pslot_slot *s) {unsigned char *parent=NULL;return read_local(s->player+0x70,&parent,8)?parent:(unsigned char*)1;}
/* P_k into its seat's room, in its scope. */
static int pfbody_connect_slot(unsigned k) {
    pslot_slot *s=&pslots[k];int fault=0;
    if(!s->seated || s->roomless || !s->room || !s->player) return pfbody_fail("body_seat");
    if(pfbody_parent(s)!=s->room) return pfbody_fail("body_parent");
    if(!pslot_enter(k)) return pfbody_fail("body_scope");
    *(unsigned char *volatile*)(s->player+0x70)=NULL;   /* the seat's upward pointer: ConnectTo takes it out of no list */
    __try {pfbody_connect(s->player,pfbody_filter,s->room,NULL);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    unsigned char *parent=pfbody_parent(s);
    if(parent!=s->room) {   /* not at the seat: off again, the upward pointer back, then stop */
        if(!fault && parent) {
            __try {pfbody_connect(s->player,pfbody_filter,NULL,NULL);}
            __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
        }
        *(unsigned char *volatile*)(s->player+0x70)=s->room;
    }
    int left=pslot_leave(k);
    if(fault) return pfbody_fail("body_connect_fault");
    if(parent!=s->room) return pfbody_fail("body_connect");
    if(!left) return pfbody_fail("body_leave");
    pfbody_mask|=1u<<(k-1);InterlockedIncrement64(&pfbody_connects);
    return 1;
}
/* P_k off its room again, in its scope; the seat's upward pointer back for its release. */
static int pfbody_disconnect_slot(unsigned k) {
    pslot_slot *s=&pslots[k];int fault=0;
    if(!(pfbody_mask>>(k-1)&1u)) return 1;
    if(!pslot_enter(k)) return pfbody_fail("body_scope");
    __try {pfbody_connect(s->player,pfbody_filter,NULL,NULL);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    unsigned char *parent=pfbody_parent(s);
    if(!fault && !parent) {*(unsigned char *volatile*)(s->player+0x70)=s->room;pfbody_mask&=~(1u<<(k-1));InterlockedIncrement64(&pfbody_disconnects);}
    int left=pslot_leave(k);
    if(fault) return pfbody_fail("body_disconnect_fault");
    if(parent) return pfbody_fail("body_disconnect");
    return left?1:pfbody_fail("body_leave");
}
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
            if(!(pslot_mask>>(k-1)&1u)) continue;
            if(!pfbody_seat_ok(k)) {InterlockedIncrement64(&pfbody_roomless);continue;}
            if(!pfbody_connect_slot(k)) return 0;
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
    if(pfbody_someone_up(tick)) {
        pfbody_seated_since=0;memset(pfbody_back,0,sizeof(pfbody_back));
        if(!pfbody_mask) return 1;
        for(unsigned k=2;k<=PILOT_SLOTS;k++) if(!pfbody_disconnect_slot(k)) return 0;
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
    pfbody_kill_installed=0;pfbody_kill_off=pfbody_kill_spared=pfbody_kill_late=0;
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
    }
    return pfbody_arm_using();
}
static int pfbody_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," bodies=%s bodies_why=%s bodies_mask=%x connects=%lld/%lld body_checks=%lld parks=%lld/%lld returns=%lld kill=%d/%lld/%lld/%lld roomless=%lld",
        pfbody_stage,pfbody_why,pfbody_mask,(long long)pfbody_connects,(long long)pfbody_disconnects,(long long)pfbody_checks,(long long)pfbody_parks,
        (long long)pfbody_unparks,(long long)pfbody_returns,pfbody_kill_installed,(long long)pfbody_kill_off,(long long)pfbody_kill_spared,(long long)pfbody_kill_late,(long long)pfbody_roomless);
    return n>0 && (size_t)n<capacity?n:0;
}

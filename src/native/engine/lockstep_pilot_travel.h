/* Players 2..N's travel drive tells its player as X4's own does, identically on every client (native_gate_pilot_travel,
 * a session default; stop 238).
 * Measured 25 Nov (x4ls play --solo 1 / --solo 2 of a continued rk9 session, the same script for both: an autopilot to
 * Mars' accelerator, a stop, the Moon's superhighway): X4's own player's activity reads travel from the autopilot's
 * travel start (tick 540) until the superhighway's entry (1765), where the computer shows "Travel Mode disrupted" and the
 * ship leaves the highway's far gate at 157 m/s. Player 2's activity read none all the time (the receipt's activity
 * field) and its ship left the highway at 2.2 km/s, the travel drive running.
 *
 * What X4 does [F, X4.exe 9.00, read in the image]. Three routines run a ship's travel drive:
 *   start   0x6d8bf0 (the ship's StartTravelMode event 0x6d0f10, the activity setter 0x710ed0, ForceStartTravelMode),
 *   stop    0x6d9190 (the ship's StopTravelMode event 0x6d0f20, the setter's teardown, a container, scripts),
 *   disrupt 0x6d9600 (the TravelDisabledEvent a DisableTravelInfluence posts, e.g. a superhighway's, through the
 *           Object slot 258 thunk 0x6d0a70; damage 0x51fa70; DisableTravelMode scripts; the player's controller; the
 *           action handler 0xd86c80, a key's: in slot k's scope already, and pinned by lockstep_pilot_native.h).
 * Each asks whether the ship is X4's player's (0x9f7860, the universe player's ship) and tells that player only: the start
 * and the stop post TravelModeChargeStarted / TravelModeStarted / ChargeAborted / TravelModeStopped on it (U::Player
 * answers them with Start / StopPlayerActivity: the activity follows the drive), the disrupt stops the player's travel
 * activity (StopPlayerActivity 0x9fd330: the activity's end tears the drive down and the computer says "disrupted")
 * where it stops another ship's drive directly (0x6d9190). For slot k's ship none of that reached P_k: the autopilot's
 * drive ran with slot k's activity at none, the highway's disrupt stopped the drive without the activity, and what X4
 * does for its player's travel never happened for slot k's.
 *
 * Here, on every client alike (simulation): eleven calls of those three routines go through trampolines on this
 * module's page near the image to one rule (and four of their posts on X4's player, below; the setter's own activity
 * event is lockstep_ui_replay.h's uirep_activity_tell). When the ship is a seated slot 2..N ship and no scope is open (the frame
 * thread, no presentation scope, P_k where its seat says, as the scope's end checks), X4's routine runs inside slot k's
 * scope (lockstep_pilot_slots.h): the universe player is P_k there, so X4's own player test answers slot k's ship and
 * X4's own steps tell P_k (its events, its activity through lockstep_ui_replay.h's adapter, the notices of slot k's
 * frame on client k only). The adapter's travel start that a start routine's own event asks for runs when the routine
 * has returned, still in the scope, as md/modes.xml's for X4's player: the drive charging or running, then the setter
 * (MD starts it later for X4's player, the drive charging by then). A disrupt whose slot had no travel activity before also gets X4's direct stop of the drive
 * inside the scope (what X4 does for a ship whose player is not in travel), so no drive is left running. Slot k's scope
 * open already (its own input's replay): X4's routine as it is. Another scope open, another thread, a scope that cannot
 * open: X4's routine as it is (counted). The inputs are the simulation's (the seats, the ships, the scope rules), the
 * routine runs where X4 runs it: the same on every client, and the activity is in the pilot receipt (PUIREP).
 * Not covered: md/modes.xml's sounds and cockpit tint for a travel start of slot k (the adapter's, as for slot k's key);
 * the Linux gate (no twin: a mixed session leaves this off).
 * Pins (refuse the arming): the three routines, the player test, the two thunks and a block around each site, the sites'
 * calls as X4's. Installed before the snapshot load (nothing runs them then); every gate tick checks the sites (stop 238,
 * why site_lost); the stop restores them. Status PILOT_TRAVEL. */
#define PTRV_STOP_REASON 238u
#define PTRV_START 0x6d8bf0u
#define PTRV_STOP 0x6d9190u
#define PTRV_DISRUPT 0x6d9600u
#define PTRV_PLAYER_SHIP 0x9f7860u
#define PTRV_PAGE 0x1000u
#define PTRV_TRAMP 16u
#define PTRV_ROWS 16u
enum {PTRV_KIND_START=0,PTRV_KIND_STOP=1,PTRV_KIND_DISRUPT=2,PTRV_KIND_POST=3};
#define PTRV_POST_NOW 0x99a3f0u          /* EventSource::PostEvent(source, event, time, immediate) */
#define PTRV_POST_EPSILON 0x2cbe470u     /* ...delivers at once what is due before now + this (0.0001 s) */
enum {PTRV_OP_CALL=0,PTRV_OP_JMP=1,PTRV_OP_JCC=2};   /* e8 rel32, e9 rel32, 0f 84 rel32 */
typedef struct {unsigned site,target;unsigned char op,kind;unsigned block,block_size;const char *block_sha;unsigned char event_size;} ptrv_site;
static const ptrv_site ptrv_sites[]={
    {0x6d0f19u,PTRV_START,PTRV_OP_JCC,PTRV_KIND_START,0x6d0f00u,0x20u,
     "42f60e31131c759c6d346f84191549da0286296765cfc95ec5237b1b61a397a8",0u},   /* the ship's StartTravelMode event */
    {0xbe4182u,PTRV_START,PTRV_OP_CALL,PTRV_KIND_START,0xbe4170u,0x20u,
     "eeb09986ce820f5afd275912af61a74079f8192b7e6c30aac84b6944fa6992fb",0u},   /* ForceStartTravelModeAction */
    {0x6d0f36u,PTRV_STOP,PTRV_OP_CALL,PTRV_KIND_STOP,0x6d0f20u,0x20u,
     "baafbf833c5a13a4adbb0ec96dfd38d4bdb998609cd956ba124a0f8f1a85a041",0u},   /* the ship's StopTravelMode event */
    {0x442b8eu,PTRV_STOP,PTRV_OP_CALL,PTRV_KIND_STOP,0x442b70u,0x30u,
     "a405b7d6209f592e4c1eef8cbc9d8bbe2955fd58c580e8e823ac8540222aded9",0u},   /* a container's (0x4426b0) */
    {0x7cdf9au,PTRV_STOP,PTRV_OP_CALL,PTRV_KIND_STOP,0x7cdf80u,0x20u,
     "70034cba24a285737c1b23a239b4b767cf3679250c12e1638c75c43e21ec55b0",0u},   /* Ship slot 94 */
    {0x11fc09eu,PTRV_STOP,PTRV_OP_CALL,PTRV_KIND_STOP,0x11fc080u,0x30u,
     "decd428968a06a5f20979fd962f9747a255bf189843c84051f3e9ccf36e0caf8",0u},   /* a script action (0x11fbfe0) */
    {0x120f924u,PTRV_STOP,PTRV_OP_CALL,PTRV_KIND_STOP,0x120f910u,0x20u,
     "6413ee105d73011c5ee3d1b25242d3c9d7efe8841e5188df072db12f6c5fd6c5",0u},   /* a script action (0x120bca0) */
    {0x6d0a78u,PTRV_DISRUPT,PTRV_OP_JMP,PTRV_KIND_DISRUPT,0x6d0a60u,0x20u,
     "32d4dfc16e9cb314e91c973361974c74d8d21db35c7debf9c53d4d8e286bdbe1",0u},   /* Object slot 258: TravelDisabledEvent */
    {0x51fb14u,PTRV_DISRUPT,PTRV_OP_CALL,PTRV_KIND_DISRUPT,0x51fb00u,0x20u,
     "cca33ba7684544c5302c60907c00720b9b618c3d73086cdf765b42e488d51d07",0u},   /* damage (Destructible 0x51fa70) */
    {0xbe3ebau,PTRV_DISRUPT,PTRV_OP_CALL,PTRV_KIND_DISRUPT,0xbe3ea0u,0x20u,
     "7b0880e637cae0202ee22fc807c2e05234ed341f297893a970d46819f35bc6de",0u},   /* DisableTravelModeAction */
    {0xd8d570u,PTRV_DISRUPT,PTRV_OP_CALL,PTRV_KIND_DISRUPT,0xd8d560u,0x20u,
     "fac9686d3ecc8e53a6fbf44906a20234264b88890e413fe042fb38b46147b55b",0u},   /* the player's controller (slot 22) */
    /* the routines' own posts on X4's player (inside slot k's scope: on P_k), handed to the scripts nobody owns after */
    {0x6d8de6u,PTRV_POST_NOW,PTRV_OP_CALL,PTRV_KIND_POST,0x6d8dd0u,0x20u,
     "880a782ce813b33046ee7f97ab2fcf2d3668678c7afbf90597d0fec848cc61f7",0x18u},   /* the start: TravelModeStarted at once */
    {0x6d8ec7u,PTRV_POST_NOW,PTRV_OP_CALL,PTRV_KIND_POST,0x6d8eb0u,0x20u,
     "b9a2cb538c944d3d17374a9345f7c77a21f9b761c5ad8fbc66a67a0ad0979dfe",0x20u},   /* the start: TravelModeChargeStarted */
    {0x6d92aeu,PTRV_POST_NOW,PTRV_OP_CALL,PTRV_KIND_POST,0x6d9290u,0x30u,
     "bf360aab6cda1bcb0b41627ceea0e630938144d8232c41a26ebfd5e214fd9572",0x18u},   /* the stop: TravelModeChargeAborted */
    {0x6d9473u,PTRV_POST_NOW,PTRV_OP_CALL,PTRV_KIND_POST,0x6d9460u,0x20u,
     "7e17f89c46afac4eb459bc6e3a7bb4e39dce42a3381dc6b6eab668d7aea70b12",0x18u}};  /* the stop: TravelModeStopped */
#define PTRV_SITES (sizeof(ptrv_sites)/sizeof(*ptrv_sites))
static const struct {unsigned rva,size;const char *sha;} ptrv_fn_pins[]={
    {PTRV_START,0x597u,"9fcdd42e034b1b804c859e25622b57a2c69b0531a209fbc321a1a1fb885a6506"},
    {PTRV_STOP,0x470u,"9524c110c63b958d6e765b2fc2fa9d2722a5854b2ffe118559aaf438bbdc8cb2"},
    {PTRV_DISRUPT,0x242u,"ab0e8f2c46d983645cd7df31c277138b66b97ceb9303743b5eba311d8ebf486b"},
    {PTRV_PLAYER_SHIP,0x80u,"96315d9de27d35313ebffdbd4a99dec44c819a850762d8b01b9d720000606718"},
    {PTRV_POST_NOW,807u,"5ca12a5d17041238645af6d6028c718bb3714cc53e9dd6963769d51827087c0d"},   /* the posts' PostEvent */
    {0x6d0f10u,0x48u,"bed221d50ca1413af479eeccca2a0ba5a7bf9b0fce763dd7c25674417627e072"},   /* the two event thunks */
    {0x6d0a70u,0x0du,"c06b9a134d2de5ffd098e0297fb5b4dfbc22d28cb1aea568af3fa3700913e91e"}};
typedef uintptr_t (*ptrv_fn)(void *ship,double duration);   /* the disrupt reads xmm1; the others ignore it */
static int ptrv_armed,ptrv_installed;
static const char *ptrv_why="-",*ptrv_check_why="-";
static unsigned char *ptrv_page;
static unsigned char ptrv_patched[PTRV_SITES][6];
static volatile LONG64 ptrv_calls[4],ptrv_slot_calls[3],ptrv_scoped[3],ptrv_inside,ptrv_busy,ptrv_refused,ptrv_stops,ptrv_told,ptrv_resumed;
typedef struct {uint64_t tick;unsigned char site,slot,before,after,mode;} ptrv_row;
static ptrv_row ptrv_rows[PTRV_ROWS];static volatile LONG64 ptrv_row_count;
/* tests: in place of X4's routines, the slot's ship, the scope's entry and end */
static uintptr_t (*ptrv_target_test)(unsigned kind,void *ship,double duration);
static unsigned char *(*ptrv_slot_ship_test)(unsigned k);
static uintptr_t (*ptrv_post_test)(void *source,void *event,double time,unsigned char immediate);
static void (*ptrv_tell_test)(unsigned k,const unsigned char *event,double time);
static void (*ptrv_resume_test)(unsigned k);   /* in place of the setter at a held travel start */
static int (*ptrv_enter_test)(unsigned k);static void (*ptrv_leave_test)(unsigned k);
static unsigned char *ptrv_seated_ship(unsigned k) {
    if(ptrv_slot_ship_test) return ptrv_slot_ship_test(k);
    const pslot_slot *s=&pslots[k];
    return s->seated && s->ship?s->ship:NULL;
}
static unsigned ptrv_slot_of(const unsigned char *ship) {
    for(unsigned k=2;ship && k<=PILOT_SLOTS;k++) if(ptrv_seated_ship(k)==ship) return k;
    return 0;
}
static int ptrv_activity(unsigned k) {
    int activity=0;const unsigned char *player=pslots[k].player;
    if(player) read_local(player+UIREP_PLAYER_ACTIVITY,&activity,4);
    return activity;
}
static uintptr_t ptrv_call(unsigned i,void *ship,double duration) {
    const ptrv_site *s=&ptrv_sites[i];
    if(ptrv_target_test) return ptrv_target_test(s->kind,ship,duration);
    return ((ptrv_fn)(void*)(image+s->target))(ship,duration);
}
/* Slot k's scope for X4's routine (as lockstep_pilot_death.h's place forward opens it): 1 opened. */
static int ptrv_enter(unsigned k) {
    if(ptrv_enter_test) return ptrv_enter_test(k);
    unsigned char *parent=NULL;
    if(pstk_scope_slot || pres_open || GetCurrentThreadId()!=ui_thread) return 0;
    if(!read_local(pslots[k].player+0x70,&parent,8) || parent!=pslot_at(&pslots[k])) return 0;   /* the scope's end would refuse it */
    return pslot_enter(k);
}
static void ptrv_leave(unsigned k) {if(ptrv_leave_test) ptrv_leave_test(k);else (void)pslot_leave(k);}
static void ptrv_note(unsigned i,unsigned k,int before,int after,char mode) {
    const LONG64 n=InterlockedIncrement64(&ptrv_row_count)-1;ptrv_row *r=&ptrv_rows[(uint64_t)n%PTRV_ROWS];
    r->tick=stats.issued;r->site=(unsigned char)i;r->slot=(unsigned char)k;r->before=(unsigned char)before;r->after=(unsigned char)after;
    r->mode=(unsigned char)mode;
}
static uintptr_t ptrv_rule(unsigned i,unsigned char *ship,double duration) {
    const unsigned kind=ptrv_sites[i].kind;
    InterlockedIncrement64(&ptrv_calls[kind]);
    const unsigned k=ptrv_installed?ptrv_slot_of(ship):0u;
    if(!k) return ptrv_call(i,ship,duration);
    InterlockedIncrement64(&ptrv_slot_calls[kind]);
    const int before=ptrv_activity(k);
    if(pstk_scope_slot==k) {   /* its own input's replay: X4's player there is P_k already */
        InterlockedIncrement64(&ptrv_inside);
        const uintptr_t answer=ptrv_call(i,ship,duration);ptrv_note(i,k,before,ptrv_activity(k),'I');return answer;
    }
    if(!ptrv_enter(k)) {
        InterlockedIncrement64(pstk_scope_slot?&ptrv_busy:&ptrv_refused);
        const uintptr_t answer=ptrv_call(i,ship,duration);ptrv_note(i,k,before,ptrv_activity(k),pstk_scope_slot?'B':'R');return answer;
    }
    uintptr_t answer=0;
    __try {
        /* a start: the adapter's travel start (U::Player's answer to the routine's own event) waits for its end */
        if(kind==PTRV_KIND_START) {uirep_travel_defer=1;uirep_travel_held=NULL;}
        answer=ptrv_call(i,ship,duration);
        if(kind==PTRV_KIND_START) {
            pslot_slot *const held=uirep_travel_held;uirep_travel_defer=0;uirep_travel_held=NULL;
            /* md/modes.xml's travel start for X4's player: the drive charging or running is enough (the adapter's own
             * check asks for one that can start), then set_player_activity: the setter, the ship as the last one */
            if(held && ptrv_activity(k)!=UIREP_ACTIVITY_TRAVEL) {
                uint64_t id=0;
                if(read_local(ship+UIREP_ID,&id,8)) memcpy(held->player+UIREP_PLAYER_LAST_SHIP,&id,8);
                if(ptrv_resume_test) ptrv_resume_test(k);else uirep_set_activity(held,UIREP_ACTIVITY_TRAVEL);
                InterlockedIncrement64(&ptrv_resumed);
            }
        }
        /* X4's disrupt of its player's ship ends the travel activity, and the activity's end the drive; with no travel
         * activity it stops the drive itself for any other ship. */
        if(kind==PTRV_KIND_DISRUPT && before!=UIREP_ACTIVITY_TRAVEL) {
            InterlockedIncrement64(&ptrv_stops);
            if(ptrv_target_test) ptrv_target_test(PTRV_KIND_STOP,ship,0.0);else ((ptrv_fn)(void*)(image+PTRV_STOP))(ship,0.0);
        }
    }
    __finally {uirep_travel_defer=0;uirep_travel_held=NULL;ptrv_leave(k);}
    InterlockedIncrement64(&ptrv_scoped[kind]);
    ptrv_note(i,k,before,ptrv_activity(k),'S');
    return answer;
}
/* X4's post of one of those events on its player: inside slot k's scope that is P_k, and X4 delivers it to P_k's own
 * listeners only, at once (the scripts nobody owns listen on player 1's object: md/notifications.xml's "Travel Mode
 * charging", for one). After X4's post a copy goes to them as slot k's, as the place forward does (lockstep_pilot_death.h
 * pvehk_forward): only an event X4 delivered at once (immediate, due before now + 0.0001 s; a later one is delivered at
 * its time, in slot k's scope, and forwarded there). */
static int ptrv_due_now(double time) {
    double now=0.0,epsilon=0.0001;
    if(ptrv_post_test) return time<=0.0;
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    if(!slots || !slots[0] || !read_local((unsigned char*)slots[0]+0x300,&now,8)) return 0;
    read_local(image+PTRV_POST_EPSILON,&epsilon,8);
    return time<now+epsilon;
}
static uintptr_t ptrv_post_rule(unsigned i,unsigned char *source,unsigned char *event,double time,unsigned char immediate) {
    _Alignas(16) unsigned char copy[0x20];unsigned k=0;
    const unsigned size=ptrv_sites[i].event_size;
    InterlockedIncrement64(&ptrv_calls[PTRV_KIND_POST]);
    if(ptrv_installed && event && immediate && size<=sizeof(copy) && pstk_scope_slot>=2 && pstk_scope_slot<=PILOT_SLOTS &&
       source==pslots[pstk_scope_slot].player && GetCurrentThreadId()==ui_thread && ptrv_due_now(time) && read_local(event,copy,size))
        k=pstk_scope_slot;
    const uintptr_t answer=ptrv_post_test?ptrv_post_test(source,event,time,immediate):
        ((uintptr_t(*)(void*,void*,double,unsigned char))(void*)(image+PTRV_POST_NOW))(source,event,time,immediate);
    if(k) {
        InterlockedIncrement64(&ptrv_told);
        if(ptrv_tell_test) ptrv_tell_test(k,copy,time);else pvehk_forward(k,copy,time);
    }
    return answer;
}
#define PTRV_RULE(n) static uintptr_t ptrv_rule_##n(unsigned char *ship,double duration) {return ptrv_rule(n,ship,duration);}
PTRV_RULE(0) PTRV_RULE(1) PTRV_RULE(2) PTRV_RULE(3) PTRV_RULE(4) PTRV_RULE(5) PTRV_RULE(6) PTRV_RULE(7) PTRV_RULE(8)
PTRV_RULE(9) PTRV_RULE(10)
#undef PTRV_RULE
#define PTRV_POST_RULE(n) static uintptr_t ptrv_rule_##n(unsigned char *source,unsigned char *event,double time,unsigned char immediate) \
    {return ptrv_post_rule(n,source,event,time,immediate);}
PTRV_POST_RULE(11) PTRV_POST_RULE(12) PTRV_POST_RULE(13) PTRV_POST_RULE(14)
#undef PTRV_POST_RULE
static void *const ptrv_rules[PTRV_SITES]={(void*)ptrv_rule_0,(void*)ptrv_rule_1,(void*)ptrv_rule_2,(void*)ptrv_rule_3,(void*)ptrv_rule_4,
    (void*)ptrv_rule_5,(void*)ptrv_rule_6,(void*)ptrv_rule_7,(void*)ptrv_rule_8,(void*)ptrv_rule_9,(void*)ptrv_rule_10,
    (void*)ptrv_rule_11,(void*)ptrv_rule_12,(void*)ptrv_rule_13,(void*)ptrv_rule_14};
static unsigned ptrv_length(unsigned i) {return ptrv_sites[i].op==PTRV_OP_JCC?6u:5u;}
/* The site's bytes aimed at `to`: X4's (its routine) or ours (the site's trampoline). */
static void ptrv_bytes(unsigned i,const unsigned char *to,unsigned char out[6]) {
    const ptrv_site *s=&ptrv_sites[i];const unsigned n=ptrv_length(i);
    const int32_t rel=(int32_t)((intptr_t)to-(intptr_t)(image+s->site+n));
    if(s->op==PTRV_OP_JCC) {out[0]=0x0f;out[1]=0x84;memcpy(out+2,&rel,4);}
    else {out[0]=s->op==PTRV_OP_CALL?0xe8:0xe9;memcpy(out+1,&rel,4);out[5]=0;}
}
static void ptrv_original(unsigned i,unsigned char out[6]) {ptrv_bytes(i,image+ptrv_sites[i].target,out);}
/* jmp [rip]; abs64 rule */
static void ptrv_write_trampoline(unsigned char *p,unsigned i) {p[0]=0xff;p[1]=0x25;memset(p+2,0,4);memcpy(p+6,&ptrv_rules[i],8);}
static int ptrv_pins(void) {
    for(unsigned i=0;i<sizeof(ptrv_fn_pins)/sizeof(*ptrv_fn_pins);i++)
        if(!hash_ok(image+ptrv_fn_pins[i].rva,ptrv_fn_pins[i].size,ptrv_fn_pins[i].sha)) {pin_failed_rva=ptrv_fn_pins[i].rva;return 0;}
    for(unsigned i=0;i<PTRV_SITES;i++) {
        const ptrv_site *s=&ptrv_sites[i];unsigned char want[6],now[6];const unsigned n=ptrv_length(i);ptrv_original(i,want);
        if(s->block>s->site || s->site+n>s->block+s->block_size || !hash_ok(image+s->block,s->block_size,s->block_sha) ||
           !read_local(image+s->site,now,n) || memcmp(now,want,n)) {pin_failed_rva=s->site;return 0;}
    }
    return 1;
}
static unsigned char *ptrv_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+0x900000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x60000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,PTRV_PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int reachable=1;
            for(unsigned i=0;i<PTRV_SITES;i++) {
                intptr_t d=(intptr_t)p-(intptr_t)(image+ptrv_sites[i].site+6);
                if(d<=INT32_MIN+0x10000 || d>=INT32_MAX-0x10000) reachable=0;
            }
            if(reachable) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void ptrv_uninstall(void) {
    if(!ptrv_installed) return;
    ptrv_installed=0;   /* the rules call X4's routines as they are from here on */
    for(unsigned i=0;i<PTRV_SITES;i++) {
        unsigned char original[6];ptrv_original(i,original);
        if(!view_cas_bytes(ptrv_sites[i].site,ptrv_patched[i],original,ptrv_length(i))) ptrv_why="restore";
    }
}
/* The trampolines and the sites (CAS of each 16-byte block a site touches). 0 refused, nothing written. */
static int ptrv_install(void) {
    if(ptrv_installed) return 1;
    if(!ptrv_page) ptrv_page=ptrv_alloc_page();
    if(!ptrv_page) {ptrv_why="page";return 0;}
    DWORD old,ignored;
    if(!VirtualProtect(ptrv_page,PTRV_PAGE,PAGE_READWRITE,&old)) {ptrv_why="page";return 0;}
    memset(ptrv_page,0xcc,PTRV_PAGE);
    for(unsigned i=0;i<PTRV_SITES;i++) ptrv_write_trampoline(ptrv_page+i*PTRV_TRAMP,i);
    if(!VirtualProtect(ptrv_page,PTRV_PAGE,PAGE_EXECUTE_READ,&ignored)) {ptrv_why="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),ptrv_page,PTRV_PAGE);
    ptrv_installed=1;
    for(unsigned i=0;i<PTRV_SITES;i++) {
        unsigned char original[6];ptrv_original(i,original);ptrv_bytes(i,ptrv_page+i*PTRV_TRAMP,ptrv_patched[i]);
        if(!view_cas_bytes(ptrv_sites[i].site,original,ptrv_patched[i],ptrv_length(i))) {
            for(unsigned j=0;j<i;j++) {unsigned char o[6];ptrv_original(j,o);view_cas_bytes(ptrv_sites[j].site,ptrv_patched[j],o,ptrv_length(j));}
            ptrv_installed=0;ptrv_why="site";return 0;
        }
    }
    return 1;
}
static int ptrv_check(void) {
    if(!ptrv_installed) return 1;
    for(unsigned i=0;i<PTRV_SITES;i++) {
        unsigned char now[6];
        if(!read_local(image+ptrv_sites[i].site,now,ptrv_length(i)) || memcmp(now,ptrv_patched[i],ptrv_length(i))) {
            if(!strcmp(ptrv_check_why,"-")) ptrv_check_why="site_lost";
            return 0;
        }
    }
    return 1;
}
/* gate.pilot_travel(): before the snapshot load and the first admitted frame, on the UI thread; 1 armed. */
static int ptrv_arm(void) {
    if(ptrv_installed) return stats.active!=0;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) {ptrv_why="late";return 0;}
    if(!ptrv_pins()) {ptrv_why="pins";return 0;}
    if(!ptrv_install()) return 0;
    ptrv_armed=1;return 1;
}
static int ptrv_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d calls=%lld/%lld/%lld/%lld slot=%lld/%lld/%lld scoped=%lld/%lld/%lld inside=%lld busy=%lld "
        "refused=%lld stops=%lld told=%lld activity=%lld deferred=%lld resumed=%lld why=%s check=%s last=",ptrv_installed,(long long)ptrv_calls[0],(long long)ptrv_calls[1],(long long)ptrv_calls[2],
        (long long)ptrv_calls[3],
        (long long)ptrv_slot_calls[0],(long long)ptrv_slot_calls[1],(long long)ptrv_slot_calls[2],(long long)ptrv_scoped[0],(long long)ptrv_scoped[1],
        (long long)ptrv_scoped[2],(long long)ptrv_inside,(long long)ptrv_busy,(long long)ptrv_refused,(long long)ptrv_stops,(long long)ptrv_told,
        (long long)uirep_activity_told,(long long)uirep_travel_deferred,(long long)ptrv_resumed,ptrv_why,ptrv_check_why);
    const LONG64 count=ptrv_row_count,first=count>(LONG64)PTRV_ROWS?count-(LONG64)PTRV_ROWS:0;
    for(LONG64 j=first;n>0 && (size_t)n<capacity && j<count;j++) {   /* tick:site:slot:activity before>after:mode */
        const ptrv_row *r=&ptrv_rows[(uint64_t)j%PTRV_ROWS];
        n+=snprintf(out+n,capacity-(size_t)n,"%s%llu:%u:%u:%u>%u:%c",j>first?",":"",(unsigned long long)r->tick,
                    r->site,r->slot,r->before,r->after,r->mode?r->mode:'?');
    }
    if(!count && n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n,"-");
    return n>0 && (size_t)n<capacity?n:0;
}

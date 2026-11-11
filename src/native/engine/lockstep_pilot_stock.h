/* Stock piloting, DEFAULT OFF, stop reason 155 (native_gate_pilot_stock): step 2
 * of reports/LOCKSTEP-VIRTUAL-PLAYERS-RE-20261029.txt. Slot 1, the ship every
 * client sits in, flies with X4's own player control path on every client, from
 * the capturing client's input delayed through the wire. It reuses the
 * native-controls pieces of lockstep_pilot_native.h (dispatcher, cursor and SPACE
 * entries) in their 'stock' mode; the authority predicate and the transform
 * routers stay stock (the PlayerController moves the ship itself).
 *   capture  the client whose slot is the stock slot (capture source 'stock'):
 *            the dispatchers record every control id of the replay set as (kind,
 *            id, value) in dispatch order instead of running it; the SPACE
 *            context edge (menus, map) becomes an event in the same order; the
 *            three outputs of cursor/direct mouse steering (d88a10, 2f4b738..740)
 *            of the frame are recorded. Presentation ids (camera, menus, map,
 *            targets, walking, mouse steering modes) run locally as before.
 *   wire     one record per tick, input section 'stock' (kind 0x53).
 *   replay   every client, in path_release of tick c+d (MoveWorker, pathing pass
 *            joined, slots 3/4/6 of the previous frame complete): SPACE edges
 *            through the simulation half of d88710 (flight control 2f4b828, the
 *            bank fields it zeroes or times, d9c250 / d9c590, the Flight Stopped /
 *            Started events; the input object's flags 9062/9064 stay this
 *            client's), each recorded id through every handler the multimap holds
 *            for it (only the stock handlers d86c80, d870b0, d87f70, 71bac0,
 *            71c640 are accepted), and d88a10's output in this frame's d95180
 *            replaced by the recorded values. d95180, the control scheme, the
 *            solver d93440, boost, travel / long-range scan / SETA (md/modes.xml)
 *            and flight assist then run stock on every client.
 *   local    a client that does not capture runs no control id of the replay or
 *            refused sets (counted as dropped); every client keeps the input
 *            object side of its own SPACE edges and its own d9cc30 refresh.
 * Refused everywhere (logged): fire and weapon groups (aim is the camera pick,
 * 717ed0), match speed and scan mode (they follow the soft target, which is
 * still local), autopilot, guidance, dock/undock, get up, squad orders, deploys,
 * countermeasures and drones (object creation), cover/stance, the option
 * toggles (autoroll, aim assist, reduced speed) and the debug features.
 * Settings the stock path reads that are user options (autoroll, stop ship in
 * menus, aim assist, collision assist, boost toggle, force shooting at cursor,
 * throttle axis mode, invert flags, roll mix, hold/mix timings, wheel step, aim
 * assist angle and range, the mouse response curve sampled through 14e0180)
 * form a fingerprint in the per-tick receipt: differing settings are a relay
 * desync at the first tick.
 * Aim (live finding st3-P15C: slot 1's gimbal weapons posed from each client's
 * camera). Weapon::Move asks its controller for the aim point at 8141c2 (vtable
 * slot 21; for the player's guns U::PlayerShootController::vfunc21 7e8650).
 * 7e8650 reads the camera pick of 717ed0 (player+0x6c60 point in the frame of the
 * player's class 0x6d ancestor, +0x6c80 distance, +0x6c88 picked component) or,
 * without a pick or on the own ship, the cursor ray of the local camera (719750),
 * and decides manual aim from the input object's mouse steering flag (+0x9053,
 * vfunc15 7e8e60, also aim assist mode 2). Every other input is simulation or a
 * fingerprinted option. So the record carries slot 1's aim point in the ship's
 * frame and its +0x9053 (capture: after this frame's 717ed0, from the pick or
 * 719750 relative to the ship); every client replays it: the PlayerShootController
 * vtable slot 21 is replaced (from the first pass of the session) by a wrapper
 * that, around each stock call on the frame thread, writes the replayed point
 * (transformed into the player's class 0x6d frame), a valid distance, a picked
 * component that is not the ship and the replayed +0x9053, and puts the four
 * fields back afterwards. Before the first replayed record (and for a record
 * without aim) the aim is neutral: straight ahead, no mouse steering.
 * Slots 2..N (native_gate_pilot_stock_slots, lockstep_pilot_slots.h, stop reason
 * 161): every stock slot's own client captures its record the same way (no aim:
 * the aim stays slot 1's; travel, long-range scan and SETA refused for slots
 * 2..N, they are MD activities of player.entity); every client stages every stock
 * slot's record per slot, pstk_frame keeps slot k's record of the tick and the
 * slot scope replays it (pstk_slot_replay) with slot k's player as the singleton
 * and slot k's bank; d88a10's outputs and the view answer per scope. */
#define PSTK_STOP_REASON 155u
#define PSTK_VERSION 3u
#define PSTK_HEAD 43u
#define PSTK_EVENT 6u
#define PSTK_EVENTS_MAX 31u
#define PSTK_BYTES_MAX (PSTK_HEAD+PSTK_EVENT*PSTK_EVENTS_MAX) /* 229: the section fits a 240-byte blob alone */
#define PSTK_FLAG_ACTIVE 1u   /* this tick ended with the capturing client's SPACE context active */
#define PSTK_FLAG_CURSOR 2u   /* the cursor values are this frame's d88a10 outputs */
#define PSTK_FLAG_AIM 4u      /* the aim point (ship frame) and mouse steering flag of this frame */
#define PSTK_FLAG_VIEW 8u     /* the view of this frame: cockpit/floating bits, FOV factor, soft target */
#define PSTK_FLAG_FIRE 16u    /* primary fire held (only with native_gate_pilot_fire, lockstep_pilot_fire.h) */
#define PSTK_FLAG_FIRE2 32u   /* secondary fire held (missiles: pilot fire and the UI replay's missile family) */
#define PSTK_FLAGS_KNOWN (PSTK_FLAG_ACTIVE|PSTK_FLAG_CURSOR|PSTK_FLAG_AIM|PSTK_FLAG_VIEW|PSTK_FLAG_FIRE|PSTK_FLAG_FIRE2)
#define PSTK_SHOOT_VTABLE 0x2b7d1a8u  /* U::PlayerShootController */
#define PSTK_AIM_SLOT 21u
#define PSTK_AIM_RVA 0x7e8650u        /* PlayerShootController::vfunc21 */
#define PSTK_AIM_ZONE_CLASS 0x6d
#define PSTK_AIM_NEUTRAL 1000.0f      /* straight ahead (+z of the ship) [I] */
#define PSTK_RING 64u
#define PSTK_DOMAIN 0x5053544f434b3156ULL /* "PSTOCK1V" */
enum {PSTK_ACTION=0,PSTK_STATE=1,PSTK_RANGE=2,PSTK_SPACE=3,PSTK_KINDS=4};
enum {PSTK_LOCAL=0,PSTK_REPLAY=1,PSTK_REFUSE=2};
typedef struct {unsigned char kind;uint16_t id;uint32_t value;} pstk_event; /* state/space 0|1, range float bits, action 0 */
typedef struct {unsigned char flags;uint16_t capture16;float cursor[3];float aim[3];unsigned char steer,view;float fov;uint64_t target;
    unsigned count;pstk_event events[PSTK_EVENTS_MAX];} pstk_record;
typedef struct {uint64_t tick;unsigned length,present;unsigned char bytes[PSTK_BYTES_MAX];} pstk_staged;
typedef struct {uint64_t capture,apply;unsigned length,used;unsigned char bytes[PSTK_BYTES_MAX];} pstk_local;
typedef void (*pstk_handoff_fn)(void *,char);   /* d9c590(controller, authority) */
typedef void (*pstk_release_fn)(void *);        /* d9c250(controller) */
typedef void (*pstk_event_fn)(void *);          /* 70eb10 / 70ec00 (player): Flight Started / Stopped */
typedef float (*pstk_curve_fn)(void *,float);   /* 14e0180 */
typedef unsigned char (*pstk_aim_fn)(void *,float *,float *,void *,void *,void *,float,float,unsigned char,unsigned char,
    unsigned char,unsigned char,unsigned char);  /* PlayerShootController::vfunc21 7e8650 */
typedef void (*pstk_transform_fn)(void *,float *,void *,uint64_t); /* 3dde10: from's transform in to's frame (pos, three axes) */
typedef void **(*pstk_ship_fn)(void *,void **);  /* 55aeb0: the player's ship */
typedef void (*pstk_ray_fn)(void *,float *,void *,uint64_t); /* 719750: the camera cursor ray point relative to a component */
typedef struct {
    pstk_handoff_fn handoff;pstk_release_fn release;pstk_event_fn started,stopped;pstk_curve_fn curve;
    unsigned char (*authority)(void *);         /* d8dd50, stock */
    pstk_aim_fn aim;pstk_transform_fn transform;pstk_ship_fn ship;pstk_ray_fn ray;
    unsigned char *(*zone)(unsigned char *);    /* the player's first class 0x6d ancestor (IsClass, vtable +0x11e8) */
} pstk_fns;
static pstk_fns pstk_fn;
static int pstk_capturing;                      /* this client records its own slot's input */
static unsigned pstk_slot;                      /* 1: the slot flown through the canonical player */
static unsigned pstk_capture_slot;              /* the slot this client records (1 in slot-1 mode) */
static unsigned pstk_slot_mask;                 /* bit s-1: slot s is flown through the stock path */
static unsigned pstk_scope_slot;                /* 2..N inside a slot scope (lockstep_pilot_slots.h), else 0 */
static pstk_record pstk_build;                  /* the tick being captured */
static float pstk_cursor_live[3];
static int pstk_cursor_live_set,pstk_local_active=-1;
static pstk_staged pstk_ring[PILOT_SLOTS][PSTK_RING];
/* Slots 2..N: this tick's record, replayed in the slot's scope after the canonical update. */
typedef struct {uint64_t tick,hash;pstk_record r;unsigned flight,flight_known;} pstk_slot_frame;
static pstk_slot_frame pstk_slot_frames[PILOT_SLOTS];
static pstk_local pstk_locals[PSTK_RING];
static float pstk_cursor_replay[3];
static uint64_t pstk_cursor_tick,pstk_capture_start,pstk_first_apply,pstk_frame_tick,pstk_frame_hash,pstk_settings;
static unsigned pstk_frame_events,pstk_flight,pstk_flight_known;
static volatile LONG64 pstk_recorded,pstk_dropped,pstk_replayed,pstk_records,pstk_staged_count,pstk_space_local,pstk_space_replayed,
    pstk_cursor_calls,pstk_injected,pstk_overflows,pstk_max_events,pstk_unfocused,
    pstk_cursor_unfocused,pstk_aim_unfocused,pstk_local_unfocused; /* the focus rule's drops: cursor frames, aim samples, local ids */
/* Aim: the replayed sample of this tick, the vtable slot, counters. */
static int pfire_armed;       /* lockstep_pilot_fire.h */
static int pstk_focused(void);
static int pstk_aim_take(float aim[3],unsigned char *steer);   /* the focus rule's aim */
/* Gun aim per player (lockstep_pilot_aim.h): every stock slot's record carries its aim; byte 28 bit 1 = manual aim,
 * bit 2 = the aim assist on the slot's target. */
#define PAIM_STEER_MANUAL 2u
#define PAIM_STEER_ASSIST 4u
static int paim_armed,paim_script_fire;   /* the second: a scripted primary trigger for runs without people */
static int paim_take(unsigned slot,float aim[3],unsigned char *steer);
/* The trigger of the record applied at each slot's last tick (native_gate_pilot_fire). */
static uint64_t pstk_fire_tick[PILOT_SLOTS];
static unsigned char pstk_fire_on[PILOT_SLOTS],pstk_fire2_on[PILOT_SLOTS];
static int pstk_fire2_scripted;   /* gate.ui_replay_missile_hold: a scripted secondary trigger for runs without people */
static int pstk_focused(void);
/* The secondary trigger the record carries: only with pilot fire and the UI replay's missile family (else X4's own
 * secondary fire is refused as before). */
static int pstk_fire2_held(void) {
    return pfire_armed && (uirep_families&UIREP_FAMILY_MISSILES) && (((pnat_fire_buttons()&4) && pstk_focused()) || pstk_fire2_scripted);
}
static float pstk_aim_replay[3];
static unsigned char pstk_aim_steer_replay;
static int pstk_aim_valid_replay,pstk_aim_installed;
static uint64_t pstk_aim_tick;
static volatile LONG pstk_aim_on;
static volatile LONG64 pstk_aim_calls,pstk_aim_replaced,pstk_aim_neutral,pstk_aim_samples,pstk_aim_sample_failures,pstk_aim_foreign,
    pstk_aim_scoped;
#include "lockstep_pilot_stock_view.h"
#include "lockstep_pilot_foot.h"   /* slot 1 on foot: its ids and the FIRST_PERSON edge (native_gate_pilot_foot) */
/* Slots 2..N on foot (lockstep_pilot_foot_slots.h, native_gate_pilot_foot_slots). */
static int pfslot_get_up(unsigned k,double now);static int pfslot_frame_text(char *out,size_t capacity);static int pfslot_status(char *out,size_t capacity);
static const uint16_t pstk_replay_actions[]={0x7b,0x12f,0x131,0xe1,0xdd,0x105,0x148};
static const uint16_t pstk_replay_states[]={2,3,4,5,6,7,8,9,0xd,0xe,0xf,0x10,0x11,0x12,0x13,0x5a,0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x50,0x85};
static const uint16_t pstk_replay_ranges[]={1,2,3,4,5,6,0xa};
static const uint16_t pstk_refuse_actions[]={0x130,0xb3,0x13f,0xaf,0xd3,0x115,0x133,0x149,0x157,0x158,0x159,0x15a,0x15b,0x15c,0x15d,0x15e,
    0x15f,0x160,0x179,0x17d,0x122,0x123,0x8,0x9,0xa,0xb,0x8b,0x8c,0x8d,0x8e,0x8f,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,
    0x9a,0x9b,0x9c,0x9d,0x9e,0x141,0x14a,0x14b,0x176,0x147,0x81,0x16c,0x16d,0xd0,0x142,0x143,0x107};
static const uint16_t pstk_refuse_states[]={0x5b,0x18,0x19,0x30,0x31};
static const uint16_t pstk_refuse_ranges[]={0x11,0x12,0x13,0x14};
static int pstk_listed(const uint16_t *list,unsigned count,int id) {
    for(unsigned i=0;i<count;i++) if(list[i]==id) return 1;
    return 0;
}
#define PSTK_COUNT(a) (sizeof(a)/sizeof(*(a)))
/* Held inputs per slot (drops, lockstep_pilot_inputs.h): the last replayed value of every replayable state and range,
 * kept from the replay itself, so the same on every client. A dropped slot's first silent tick replays them released and
 * then the stop action (0x7b, d86c80: the occupied ship's engine target to 0): its ship comes to a stop under flight
 * assist instead of flying on with its last keys. The mouse steering is neutral by itself (pstk_cursor: no frame). */
#define PSTK_ACTION_STOP 0x7bu
static uint32_t pstk_held_states[PILOT_SLOTS][PSTK_COUNT(pstk_replay_states)],pstk_held_ranges[PILOT_SLOTS][PSTK_COUNT(pstk_replay_ranges)];
static volatile LONG64 pstk_releases,pstk_released_events;
static void pstk_hold(unsigned slot,const pstk_event *x) {
    if(slot<1 || slot>PILOT_SLOTS) return;
    if(x->kind==PSTK_STATE) {for(unsigned i=0;i<PSTK_COUNT(pstk_replay_states);i++) if(pstk_replay_states[i]==x->id) pstk_held_states[slot-1][i]=x->value;}
    else if(x->kind==PSTK_RANGE) {for(unsigned i=0;i<PSTK_COUNT(pstk_replay_ranges);i++) if(pstk_replay_ranges[i]==x->id) pstk_held_ranges[slot-1][i]=x->value;}
}
/* The release record of slot (its held states and ranges back to 0, then stop); the table is cleared. */
static unsigned pstk_release_record(unsigned slot,pstk_record *r) {
    memset(r,0,sizeof(*r));
    if(slot<1 || slot>PILOT_SLOTS) return 0;
    for(unsigned i=0;i<PSTK_COUNT(pstk_replay_states) && r->count+1<PSTK_EVENTS_MAX;i++)
        if(pstk_held_states[slot-1][i]) r->events[r->count++]=(pstk_event){PSTK_STATE,pstk_replay_states[i],0};
    for(unsigned i=0;i<PSTK_COUNT(pstk_replay_ranges) && r->count+1<PSTK_EVENTS_MAX;i++)
        if(pstk_held_ranges[slot-1][i]) r->events[r->count++]=(pstk_event){PSTK_RANGE,pstk_replay_ranges[i],0};
    r->events[r->count++]=(pstk_event){PSTK_ACTION,PSTK_ACTION_STOP,0};
    memset(pstk_held_states[slot-1],0,sizeof(pstk_held_states[slot-1]));memset(pstk_held_ranges[slot-1],0,sizeof(pstk_held_ranges[slot-1]));
    InterlockedIncrement64(&pstk_releases);InterlockedExchangeAdd64(&pstk_released_events,(LONG64)r->count);
    return r->count;
}
/* Slot's first silent tick after its drop (protocols 4 and 5), or after an ABORT of an absent or rejoinable slot (a hot
 * join's drop, lockstep_pilot_inputs.h): its record of tick-delay is the first one it lacks. */
static int pstk_release_due(unsigned slot,uint64_t tick) {
    if(tick<pilot_delay || slot<1 || slot>PILOT_SLOTS) return 0;
    const uint64_t c=tick-pilot_delay;
    if(pilot_slot_dropped(slot,c)) return c==pilot_drop_from[slot-1];
    return pilot_slot_absent(slot) && pilot_until[slot-1]!=UINT64_MAX && c==pilot_until[slot-1] && pilot_from[slot-1]<c;
}
static unsigned pstk_verdict(unsigned kind,int id) {
    /* Slot 1 on foot (lockstep_pilot_foot.h): the walking ids and GET_UP ride the record; mouselook too, split at the
     * local dispatch (pstk_dispatch_split); the pick's interaction click is refused everywhere. */
    if(pfoot_armed) {
        unsigned f=pfoot_id(kind,id);
        if(f==PFOOT_WALK_ID || f==PFOOT_SPLIT_ID) return PSTK_REPLAY;
        if(f==PFOOT_REFUSE_ID) return PSTK_REFUSE;
    }
    /* Match speed follows the soft target: with the target replayed it can be replayed too. */
    if(kind==PSTK_STATE && id==0x5b && (uirep_families&UIREP_FAMILY_TARGET)) return PSTK_REPLAY;
    /* The UI replay's families (lockstep_ui_replay.h): dock and weapon keys ride the record into their stock handlers in
     * the slot's scope; the autopilot key runs on the client's own dispatch, where its start and stop are captured with
     * the target its own guidance names (the guidance is not the same on every client). */
    if(kind==PSTK_ACTION && (uirep_families&UIREP_FAMILY_DOCK) && (id==0xaf || id==0xd3)) return PSTK_REPLAY;
    if(kind==PSTK_ACTION && (uirep_families&UIREP_FAMILY_WEAPONS) && ((id>=0x8 && id<=0xb) || (id>=0x8b && id<=0x9e) || id==0x141)) return PSTK_REPLAY;
    if(kind==PSTK_ACTION && (uirep_families&UIREP_FAMILY_AUTOPILOT) && id==0xb3) return PSTK_LOCAL;
    if(kind==PSTK_ACTION) return pstk_listed(pstk_replay_actions,PSTK_COUNT(pstk_replay_actions),id)?PSTK_REPLAY:
        pstk_listed(pstk_refuse_actions,PSTK_COUNT(pstk_refuse_actions),id)?PSTK_REFUSE:PSTK_LOCAL;
    if(kind==PSTK_STATE) return pstk_listed(pstk_replay_states,PSTK_COUNT(pstk_replay_states),id)?PSTK_REPLAY:
        pstk_listed(pstk_refuse_states,PSTK_COUNT(pstk_refuse_states),id)?PSTK_REFUSE:PSTK_LOCAL;
    if(kind==PSTK_RANGE) return pstk_listed(pstk_replay_ranges,PSTK_COUNT(pstk_replay_ranges),id)?PSTK_REPLAY:
        pstk_listed(pstk_refuse_ranges,PSTK_COUNT(pstk_refuse_ranges),id)?PSTK_REFUSE:PSTK_LOCAL;
    return PSTK_LOCAL;
}
/* Slots 2..N run in a scope whose singleton is the slot's own player: every handler
 * then acts on the slot's ship, except the MD activities (travel, long range scan,
 * SETA: md/modes.xml listens on player.entity, which stays slot 1's for MD). */
static const uint16_t pstk_slot_refused_actions[]={0x12f,0x131,0xe1};
static unsigned pstk_verdict_slot(unsigned kind,int id,unsigned slot) {
    unsigned v=pstk_verdict(kind,id);
    /* Slots 2..N walk only with native_gate_pilot_foot_slots (lockstep_pilot_foot_slots.h: GET_UP is consumed there). */
    if(slot>=2 && v==PSTK_REPLAY && pfoot_armed && pfoot_id(kind,id) && !pfslot_armed) return PSTK_REFUSE;
    if(slot>=2 && v==PSTK_REPLAY && kind==PSTK_ACTION && pstk_listed(pstk_slot_refused_actions,PSTK_COUNT(pstk_slot_refused_actions),id) &&
       !(id==0x12f && (uirep_families&UIREP_FAMILY_TRAVEL))) /* travel: the UI replay's adapter acts on the slot's player */
        return PSTK_REFUSE;
    return v;
}
static int pstk_slot_record_ok(const pstk_record *r,unsigned slot) {
    for(unsigned i=0;i<r->count;i++) {
        if(r->events[i].kind==PSTK_SPACE && slot>=2 && r->events[i].id!=8) return 0;   /* FIRST_PERSON: slot 1's only */
        if(r->events[i].kind!=PSTK_SPACE && pstk_verdict_slot(r->events[i].kind,r->events[i].id,slot)!=PSTK_REPLAY) return 0;
    }
    return slot<2 || !(r->flags&PSTK_FLAG_AIM) || paim_armed;   /* slots 2..N aim with lockstep_pilot_aim.h only */
}
/* The stock handlers a replayed id may reach (anything else registered under a
 * replay id fails closed); production pointers are image+RVA (pnat_use_image). */
static const unsigned pstk_handlers[]={0xd86c80,0xd870b0,0xd87f70,0x71bac0,0x71c640,0xaf1db0}; /* af1db0: weapon groups, UI replay only */
#define PSTK_HANDLERS (sizeof(pstk_handlers)/sizeof(*pstk_handlers))
static const void *pstk_handler_ptrs[PSTK_HANDLERS];
static int pstk_handler_ok(const void *handler) {
    for(unsigned i=0;i<PSTK_HANDLERS;i++) if(handler && handler==pstk_handler_ptrs[i]) return 1;
    return 0;
}

/* Record v2, little endian: 0 version | 1 flags | 2..3 low 16 bits of the capture
 * tick | 4..15 cursor outputs, three float32 (zero bits without the cursor flag) |
 * 16..27 aim point in the ship's frame, three float32, and 28 the mouse steering
 * flag 0|1 (zero without the aim flag) | 29 view bits (1 cockpit, 2 floating),
 * 30..33 FOV factor float32, 34..41 soft target u64 (portable ID, 1 = unportable,
 * 0 = none; all zero without the view flag) | 42 event count n (0..31) | then n
 * events of 6 bytes: u16 kind<<14|id (id < 512), u32 value (action 0, state and
 * SPACE 0|1, range float32 bits, finite). */
static unsigned pstk_encode(const pstk_record *r,unsigned char *out) {
    out[0]=PSTK_VERSION;out[1]=r->flags;out[2]=(unsigned char)r->capture16;out[3]=(unsigned char)(r->capture16>>8);
    for(unsigned i=0;i<3;i++) {uint32_t v;memcpy(&v,&r->cursor[i],4);for(unsigned b=0;b<4;b++) out[4+4*i+b]=(unsigned char)(v>>(8*b));}
    for(unsigned i=0;i<3;i++) {uint32_t v;memcpy(&v,&r->aim[i],4);for(unsigned b=0;b<4;b++) out[16+4*i+b]=(unsigned char)(v>>(8*b));}
    out[28]=r->steer;out[29]=r->view;
    {uint32_t v;memcpy(&v,&r->fov,4);for(unsigned b=0;b<4;b++) out[30+b]=(unsigned char)(v>>(8*b));}
    for(unsigned b=0;b<8;b++) out[34+b]=(unsigned char)(r->target>>(8*b));
    out[42]=(unsigned char)r->count;
    for(unsigned i=0;i<r->count;i++) {
        unsigned char *e=out+PSTK_HEAD+PSTK_EVENT*i;uint16_t head=(uint16_t)(r->events[i].kind<<14|r->events[i].id);
        e[0]=(unsigned char)head;e[1]=(unsigned char)(head>>8);
        for(unsigned b=0;b<4;b++) e[2+b]=(unsigned char)(r->events[i].value>>(8*b));
    }
    return PSTK_HEAD+PSTK_EVENT*r->count;
}
static int pstk_decode(const unsigned char *in,unsigned length,pstk_record *r) {
    if(length<PSTK_HEAD || in[0]!=PSTK_VERSION || (in[1]&~PSTK_FLAGS_KNOWN) || in[42]>PSTK_EVENTS_MAX || length!=PSTK_HEAD+PSTK_EVENT*in[42]) return 0;
    memset(r,0,sizeof(*r));r->flags=in[1];r->capture16=(uint16_t)(in[2]|in[3]<<8);r->count=in[42];r->steer=in[28];r->view=in[29];
    {uint32_t v=0;for(unsigned b=0;b<4;b++) v|=(uint32_t)in[30+b]<<(8*b);memcpy(&r->fov,&v,4);}
    for(unsigned b=0;b<8;b++) r->target|=(uint64_t)in[34+b]<<(8*b);
    if(r->flags&PSTK_FLAG_VIEW) {if(r->view>3 || !isfinite(r->fov)) return 0;}
    else if(r->view || r->target) return 0;
    else {uint32_t v;memcpy(&v,&r->fov,4);if(v) return 0;}
    for(unsigned i=0;i<3;i++) {
        uint32_t v=0;for(unsigned b=0;b<4;b++) v|=(uint32_t)in[4+4*i+b]<<(8*b);
        memcpy(&r->cursor[i],&v,4);
        if(!isfinite(r->cursor[i]) || (!(r->flags&PSTK_FLAG_CURSOR) && v)) return 0;
        v=0;for(unsigned b=0;b<4;b++) v|=(uint32_t)in[16+4*i+b]<<(8*b);
        memcpy(&r->aim[i],&v,4);
        if(!isfinite(r->aim[i]) || (!(r->flags&PSTK_FLAG_AIM) && v)) return 0;
    }
    if(r->steer>(1u|PAIM_STEER_MANUAL|PAIM_STEER_ASSIST) || (!(r->flags&PSTK_FLAG_AIM) && r->steer)) return 0;
    for(unsigned i=0;i<r->count;i++) {
        const unsigned char *e=in+PSTK_HEAD+PSTK_EVENT*i;uint16_t head=(uint16_t)(e[0]|e[1]<<8);uint32_t v=0;
        for(unsigned b=0;b<4;b++) v|=(uint32_t)e[2+b]<<(8*b);
        pstk_event *x=&r->events[i];x->kind=(unsigned char)(head>>14);x->id=(uint16_t)(head&0x3fff);x->value=v;
        if(x->id>=512) return 0;
        if(x->kind==PSTK_ACTION && v) return 0;
        if((x->kind==PSTK_STATE || x->kind==PSTK_SPACE) && v>1) return 0;
        if(x->kind==PSTK_SPACE && x->id!=8 && !(pfoot_armed && x->id==PFOOT_CONTEXT)) return 0;
        if(x->kind==PSTK_RANGE) {float f;memcpy(&f,&v,4);if(!isfinite(f)) return 0;}
        if(x->kind!=PSTK_SPACE && pstk_verdict(x->kind,x->id)!=PSTK_REPLAY) return 0;
    }
    return 1;
}
static uint64_t pstk_fnv(const unsigned char *b,size_t n,uint64_t h) {
    for(size_t i=0;i<n;i++) {h^=b[i];h*=0x100000001b3ULL;}
    return h;
}
/* Capture side (MoveWorker): one event in dispatch order; an overflowing tick fails closed. */
static void pstk_async_fail(const char *why);
static int pstk_record_event(unsigned kind,unsigned id,uint32_t value) {
    if(pstk_build.count>=PSTK_EVENTS_MAX) {InterlockedIncrement64(&pstk_overflows);pstk_async_fail("stock_overflow");return 0;}
    pstk_event *e=&pstk_build.events[pstk_build.count++];e->kind=(unsigned char)kind;e->id=(uint16_t)id;e->value=value;
    if((LONG64)pstk_build.count>pstk_max_events) pstk_max_events=(LONG64)pstk_build.count;
    InterlockedIncrement64(&pstk_recorded);return 1;
}
/* Point p (from's frame) into to's frame with t = 3dde10(from, t, to): pos + p.x*ax + p.y*ay + p.z*az. */
static void pstk_apply(const float t[16],const float p[3],float out[3]) {
    for(unsigned i=0;i<3;i++) out[i]=t[i]+p[0]*t[4+i]+p[1]*t[8+i]+p[2]*t[12+i];
}
static void pstk_identity(float t[16]) {memset(t,0,64);t[4]=1.0f;t[9]=1.0f;t[14]=1.0f;}
static unsigned char *pstk_ship_of(unsigned char *player) {
    void *slot=NULL;
    if(!player || !pstk_fn.ship) return NULL;
    pstk_fn.ship(player,&slot);return (unsigned char*)slot;
}
/* The class 0x6d ancestor 7e8650 transforms the pick from (parent chain at +0x70). */
static unsigned char *pstk_zone_native(unsigned char *player) {
    unsigned char *c=NULL;
    if(!player || !read_local(player+0x70,&c,8)) return NULL;
    for(unsigned depth=0;c && depth<32;depth++) {
        void **vt=NULL;
        if(!read_local(c,&vt,8) || !vt) return NULL;
        if(((unsigned char(*)(void*,int))vt[0x11e8/8])(c,PSTK_AIM_ZONE_CLASS)) return c;
        if(!read_local(c+0x70,&c,8)) return NULL;
    }
    return NULL;
}
/* Slot 1's aim of this frame (capturing client, Lua, after 717ed0): what 7e8650
 * would aim at, as a point in the ship's frame, and the mouse steering flag. */
static int pstk_aim_sample(float out[3],unsigned char *steer) {
    unsigned char *player=pnat_player(),*ship=pstk_ship_of(player),*picked=NULL,*input=NULL;float distance=-1.0f,p[4]={0},t[16];
    if(!player || !ship || !read_local(pnat_base+PNAT_INPUT,&input,8) || !input || !pstk_fn.transform || !pstk_fn.ray || !pstk_fn.zone ||
       !read_local(player+0x6c80,&distance,4) || !read_local(player+0x6c88,&picked,8) || !read_local(input+0x9053,steer,1)) return 0;
    if(!(distance<0.0f) && picked && picked!=ship) {
        unsigned char *zone=pstk_fn.zone(player);
        if(!zone || !read_local(player+0x6c60,p,16)) return 0;
        pstk_identity(t);pstk_fn.transform(zone,t,ship,0);pstk_apply(t,p,out);
    } else {
        pstk_fn.ray(player,p,ship,0);memcpy(out,p,12);
    }
    *steer=*steer!=0;
    return isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]);
}
/* This tick's record (Lua, slot 6, after slot 4's dispatch of the frame). */
static int pstk_capture(uint64_t tick,unsigned char out[PSTK_BYTES_MAX],unsigned *length,const char **why) {
    pstk_local *l=&pstk_locals[tick%PSTK_RING];
    if(!pstk_capturing || !tick) {*why="not_capturing";return 0;}
    if(l->used && l->capture==tick) {memcpy(out,l->bytes,l->length);*length=l->length;return 1;}
    /* An absent slot's client (the joiner before its handover): its row is the relay's stand-in, so nothing is proposed;
     * what its dispatch recorded meanwhile is dropped, not carried into its first record. */
    if(!pilot_slot_present(pstk_capture_slot,tick)) {
        InterlockedExchangeAdd64(&pstk_dropped,(LONG64)pstk_build.count);memset(&pstk_build,0,sizeof(pstk_build));pstk_cursor_live_set=0;
        *why="absent";return 0;
    }
    if(!pstk_capture_start || tick<pstk_capture_start) {*why="not_live";return 0;}
    /* Past the window no pass applies a record, so none is built (play42f2: window 40-4200 in a 4500-tick run
     * filled the local ring 62 ticks after the end and stopped the session). */
    if(pilot_stop && tick+pilot_delay>pilot_stop) {*why="not_live";return 0;}
    pstk_build.capture16=(uint16_t)tick;pstk_build.flags=pstk_local_active==1?PSTK_FLAG_ACTIVE:0;
    if(pstk_cursor_live_set) {pstk_build.flags|=PSTK_FLAG_CURSOR;memcpy(pstk_build.cursor,pstk_cursor_live,sizeof(pstk_cursor_live));}
    else memset(pstk_build.cursor,0,sizeof(pstk_build.cursor));
    /* The aim is slot 1's only (the local crosshair is the canonical player's) unless lockstep_pilot_aim.h is armed: then
     * every slot's own client records its aim. A dead slot has no aim and no view to record: its records are consumed
     * and ignored. */
    int own_dead=pstk_capture_slot>=1 && pstk_capture_slot<=PILOT_SLOTS && pilot_slots[pstk_capture_slot-1].dead;
    if(pfire_armed && (((pnat_fire_buttons()&1) && pstk_focused()) || paim_script_fire)) pstk_build.flags|=PSTK_FLAG_FIRE;
    if(pstk_fire2_held()) pstk_build.flags|=PSTK_FLAG_FIRE2;
    const int aims=(pstk_capture_slot==1 || paim_armed) && !own_dead;
    const int aim=aims?paim_take(pstk_capture_slot,pstk_build.aim,&pstk_build.steer):0;
    if(aim>0) {pstk_build.flags|=PSTK_FLAG_AIM;InterlockedIncrement64(&pstk_aim_samples);}
    else {memset(pstk_build.aim,0,sizeof(pstk_build.aim));pstk_build.steer=0;if(!aim && aims) InterlockedIncrement64(&pstk_aim_sample_failures);}
    if(!own_dead && pview_sample(&pstk_build.view,&pstk_build.fov,&pstk_build.target)) pstk_build.flags|=PSTK_FLAG_VIEW;
    else {pstk_build.view=0;pstk_build.fov=0.0f;pstk_build.target=0;}
    *length=pstk_encode(&pstk_build,out);
    memset(&pstk_build,0,sizeof(pstk_build));pstk_cursor_live_set=0;
    if(l->used && l->apply>pstk_frame_tick) {*why="local_ring_full";return 0;}
    l->capture=tick;l->apply=tick+pilot_delay;l->length=*length;l->used=1;memcpy(l->bytes,out,*length);
    return 1;
}
/* Staging from the wire (every client): the capture tick plus the agreed delay is the
 * one apply tick, still ahead of every pass, never before the capture window. */
static int pstk_stage(unsigned slot,const unsigned char *bytes,unsigned length,uint64_t now) {
    pstk_record r;
    if(!pstk_slot_mask || slot<1 || slot>PILOT_SLOTS || !(pstk_slot_mask>>(slot-1)&1u) || !pstk_decode(bytes,length,&r) ||
       !pstk_slot_record_ok(&r,slot)) return 0;
    uint64_t back=(uint16_t)(now-r.capture16);
    if(back>now) return 0;
    uint64_t capture=now-back,apply=capture+pilot_delay;
    if(!capture || capture<pstk_capture_start || apply<=now || apply>=now+PSTK_RING) return 0;
    if(!pilot_slot_present(slot,capture)) return 0;   /* an absent slot's row is a stand-in: never a record (hot join) */
    if(pstk_capturing && slot==pstk_capture_slot) for(unsigned i=0;i<PSTK_RING;i++) {
        pstk_local *l=&pstk_locals[i];
        if(l->used && l->capture==capture && (l->length!=length || memcmp(l->bytes,bytes,length))) return 0; /* echo mismatch */
    }
    pstk_staged *e=&pstk_ring[slot-1][apply%PSTK_RING];
    if(e->present && e->tick==apply) return e->length==length && !memcmp(e->bytes,bytes,length);
    if(e->present && e->tick>now) return 0;
    memset(e,0,sizeof(*e));e->tick=apply;e->length=length;memcpy(e->bytes,bytes,length);e->present=1;
    InterlockedIncrement64(&pstk_staged_count);return 1;
}
/* Settings the stock path reads that are user options; the response curve is
 * sampled through its own evaluator (its points live on the heap). */
static uint64_t pstk_settings_using(unsigned char *base) {
    unsigned char *input=NULL,*config=NULL,bytes[64];unsigned n=0;
    uint64_t h=0xcbf29ce484222325ULL;
    static const unsigned options[]={0x2f4b9be,0x2f4b9bf,0x2f4b9c0,0x2f4b9c1,0x2f4b9c2,0x2f4b9c3,0x2f4b9c4,0x2f4b9c5,0x2f4b9c6,0x2f4b9c7,
        0x2f4b9cc,0x38f52d7,0x38f52d8};  /* ... boost toggle, force shooting at cursor (7e8e60) */
    for(unsigned i=0;i<PSTK_COUNT(options);i++) if(!sample_read(base+options[i],&bytes[n++],1)) return 0;
    if(!sample_read(base+0x6cf1560,&input,8) || !input || !sample_read(base+0x6cf1538,&config,8) || !config) return 0;
    static const unsigned input_fields[]={0x8e20,0x905b,0x905c,0x912c,0x912d,0x912e,0x912f};
    for(unsigned i=0;i<PSTK_COUNT(input_fields);i++) if(!sample_read(input+input_fields[i],&bytes[n++],1)) return 0;
    h=pstk_fnv(bytes,n,h);
    static const struct {unsigned offset,size;} config_fields[]={{0x68,4},{0x6c,4},{0x80,8},{0x88,8},{0x90,4},{0x94,4},
        {0xc8,4},{0x294,4}};  /* ..., aim assist angle and range (7e8650) */
    for(unsigned i=0;i<PSTK_COUNT(config_fields);i++) {
        if(!sample_read(config+config_fields[i].offset,bytes,config_fields[i].size)) return 0;
        h=pstk_fnv(bytes,config_fields[i].size,h);
    }
    static const float samples[]={0.0f,0.1f,0.25f,0.5f,0.75f,1.0f};
    for(unsigned i=0;pstk_fn.curve && i<PSTK_COUNT(samples);i++) {
        float y=pstk_fn.curve(config+8,samples[i]);
        h=pstk_fnv((const unsigned char*)&y,4,h);
    }
    return h|1u;
}

/* ---- live pieces: the pnat entries in stock mode (lockstep_pilot_native.h) ---- */
static void pstk_async_fail(const char *why) {pnat_async_fail(why);}
/* Every handler of one id, in the stock order of 1255cd0/1255dd0/1255ee0. */
static void pstk_call(void *handler,unsigned kind,int id,uint32_t value) {
    if(kind==PSTK_ACTION) ((void(*)(int))handler)(id);
    else if(kind==PSTK_STATE) ((void(*)(int,unsigned char))handler)(id,(unsigned char)value);
    else {float f;memcpy(&f,&value,4);((void(*)(int,float))handler)(id,f);}
}
static void pstk_walk(unsigned char *header,unsigned kind,int id,uint32_t value) {
    pnat_node *upper=NULL,*n=pnat_first((pnat_node*)header,id,&upper);
    if(!n) return;
    while(n!=upper) {pnat_node *next=pnat_next(n);pstk_call(n->handler,kind,id,value);n=next;}
}
/* A replayed id: every handler, except the camera's under a mouselook range of slot 1 walking; X4's walking state handler
 * with the session's clock as its real time (the run's double-tap, lockstep_pilot_foot.h). */
static void pstk_replay_walk(unsigned char *header,unsigned kind,int id,uint32_t value) {
    pnat_node *upper=NULL,*n=pnat_first((pnat_node*)header,id,&upper);
    if(!n) return;
    while(n!=upper) {
        pnat_node *next=pnat_next(n);
        if(pfoot_replay_skips(kind,id,n->handler)) {}
        else if(pfoot_replay_clock(kind,n->handler)) pfoot_replay_state(n->handler,id,(unsigned char)value);
        else pstk_call(n->handler,kind,id,value);
        n=next;
    }
}
/* Every handler a replayed id reaches must be a stock one (or the camera's, skipped above). */
static int pstk_replay_handlers_ok(unsigned char *map,const pstk_event *x) {
    pnat_node *upper=NULL,*n=pnat_first((pnat_node*)(map+0x40*x->kind),x->id,&upper);
    for(;n && n!=upper;n=pnat_next(n))
        if(!pstk_handler_ok(n->handler) && !pfoot_handler_ok(x->kind,x->id,n->handler) && !pfoot_replay_skips(x->kind,x->id,n->handler)) return 0;
    return 1;
}
static int pres_local_enter(void);static void pres_local_leave(int entered); /* lockstep_pilot_present.h */
static void pdeath_note(const pstk_event *x);                                /* lockstep_pilot_death.h */
static unsigned pres_slot;static int pres_has_seat(void);
/* Two windows on one PC: keyboard and mouse only reach the window with the focus, a pad may reach both.
 * A control that engages is recorded only with the focus (a release always, so nothing stays held);
 * scripted input does not come through here. Tests: 1 focused, -1 not, 0 the real window. */
static int pstk_focus_forced;
static int pstk_focused(void) {
    if(pstk_focus_forced) return pstk_focus_forced>0;
    DWORD owner=0;HWND window=GetForegroundWindow();
    if(window) GetWindowThreadProcessId(window,&owner);
    return owner==GetCurrentProcessId();
}
/* Two windows on one PC share one cursor: X4 reads it in the window without the focus too, outside that window and so at
 * its edge when the cursor is in the other one. Its cursor steering (d88a10) then turned that client's ship on every client
 * and its camera pick aimed the guns there (9 Nov, two clients on one PC). Without the focus the record carries the neutral
 * cursor and no aim (pstk_cursor, pstk_aim_take), and an engaging presentation id does not run (pstk_dispatch). */
/* Slot 1's aim of this frame for the record: 1 sampled, 0 not readable, -1 no focus (neutral: straight ahead). */
static int pstk_aim_take(float aim[3],unsigned char *steer) {
    if(!pstk_focused()) {InterlockedIncrement64(&pstk_aim_unfocused);return -1;}
    return pstk_aim_sample(aim,steer);
}
static int pstk_is_fire(unsigned kind,int id) {
    if(kind==PSTK_STATE) return id==0x18 || id==0x19 || id==0x30 || id==0x31;
    return kind==PSTK_RANGE && id>=0x11 && id<=0x14;
}
static int pstk_engages(unsigned kind,uint32_t value) {
    if(kind==PSTK_ACTION) return 1;
    if(kind==PSTK_RANGE) {float f;memcpy(&f,&value,4);return f!=0.0f;}
    return value!=0;
}
/* What this client's own input ran locally (presentation ids: camera, menus, map, targets), named in
 * the status: local_keys= the last PSTK_LOCAL_KEYS action and state dispatches as tick:a|s:id (a
 * scripted one ends in '*'), local_ranges= the range count, local_pressed= the scripted ones. p4-long2:
 * one local action on client 4 opened the map and parted the world (lockstep_view_points.h,
 * native_gate_view_ui); its dispatch counter said that much, not which id. */
#define PSTK_LOCAL_KEYS 8u
typedef struct {uint64_t tick;uint16_t id;unsigned char kind,scripted;} pstk_local_key;
static pstk_local_key pstk_local_keys[PSTK_LOCAL_KEYS];
static uint64_t pstk_local_key_count;
static volatile LONG64 pstk_local_ranges,pstk_local_pressed;
static void pstk_local_note(unsigned kind,int id,int scripted) {
    if(kind==PSTK_RANGE) {InterlockedIncrement64(&pstk_local_ranges);return;}
    pstk_local_key *k=&pstk_local_keys[pstk_local_key_count++%PSTK_LOCAL_KEYS];
    k->tick=stats.issued;k->id=(uint16_t)id;k->kind=(unsigned char)kind;k->scripted=(unsigned char)(scripted!=0);
}
/* The local keys oldest first, "-" without any; stops before an entry that does not fit. */
static void pstk_local_keys_text(char *out,size_t capacity) {
    size_t k=0;const uint64_t first=pstk_local_key_count>PSTK_LOCAL_KEYS?pstk_local_key_count-PSTK_LOCAL_KEYS:0;
    if(capacity<2) {if(capacity) out[0]=0;return;}
    out[0]='-';out[1]=0;
    for(uint64_t i=first;i<pstk_local_key_count;i++) {
        const pstk_local_key *e=&pstk_local_keys[i%PSTK_LOCAL_KEYS];char item[48];
        int n=snprintf(item,sizeof(item),"%s%llu:%c:%x%s",k?",":"",(unsigned long long)e->tick,e->kind==PSTK_ACTION?'a':'s',e->id,e->scripted?"*":"");
        if(n<=0 || k+(size_t)n+1>capacity) break;
        memcpy(out+k,item,(size_t)n+1);k+=(size_t)n;
    }
}
/* A presentation id through every handler X4 holds for it, as this client's own (in a presenting
 * client's scope). */
static void pstk_local_run(unsigned kind,unsigned char *map,int id,uint32_t value) {
    int presented=pres_local_enter();
    InterlockedIncrement(&uirep_dispatch_depth);
    pstk_walk(map+0x40*kind,kind,id,value);
    InterlockedDecrement(&uirep_dispatch_depth);
    pres_local_leave(presented);
}
/* Mouselook with slot 1 walking (lockstep_pilot_foot.h): the camera's handlers here, the walking one in the replay. */
static void pstk_dispatch_split(unsigned char *map,int id,uint32_t value) {
    if(!pfoot_split_now()) {   /* seated, nobody walking: a presentation id as before */
        if(pstk_engages(PSTK_RANGE,value) && !pstk_focused()) {InterlockedIncrement64(&pstk_local_unfocused);return;}
        pstk_local_note(PSTK_RANGE,id,0);
        if(pres_slot && pilot_slots[pres_slot-1].dead && !pres_has_seat()) {InterlockedIncrement64(&pstk_dropped);return;}
        pstk_local_run(PSTK_RANGE,map,id,value);
        return;
    }
    if(pstk_engages(PSTK_RANGE,value) && !pstk_focused()) InterlockedIncrement64(&pstk_local_unfocused);   /* the camera's part: focus rule */
    else {
        int presented=pres_local_enter();
        pnat_node *upper=NULL,*n=pnat_first((pnat_node*)(map+0x40*PSTK_RANGE),id,&upper);
        InterlockedIncrement(&uirep_dispatch_depth);
        while(n && n!=upper) {pnat_node *next=pnat_next(n);if(n->handler!=pfoot_handler_ptrs[1]) pstk_call(n->handler,PSTK_RANGE,id,value);n=next;}
        InterlockedDecrement(&uirep_dispatch_depth);
        pres_local_leave(presented);}
    InterlockedIncrement64(&pfoot_split_local);
    if(pstk_capturing && (pstk_capture_slot==1 || pfoot_own_walking())) {   /* slot 1's, or this client's own walking slot's */
        if(pstk_engages(PSTK_RANGE,value) && !pstk_focused()) {InterlockedIncrement64(&pstk_unfocused);return;}
        if(pstk_record_event(PSTK_RANGE,(unsigned)id,value)) InterlockedIncrement64(&pfoot_split_recorded);
    }
}
/* A dispatch on this client: presentation ids run (on a presenting client as its own slot's
 * player: the camera, target and menu handlers read the singleton), replay ids are recorded (the
 * capturing client) or dropped, refused ids are logged. map = 1255xx0's argument. */
static void pstk_dispatch(unsigned kind,unsigned char *map,int id,uint32_t value) {
    if(pfoot_armed && pnat_is_live() && pfoot_id(kind,id)==PFOOT_SPLIT_ID) {pstk_dispatch_split(map,id,value);return;}
    unsigned verdict=pnat_is_live()?pstk_verdict_slot(kind,id,pstk_capturing?pstk_capture_slot:1):PSTK_LOCAL;
    if(verdict==PSTK_LOCAL) {
        if(pstk_engages(kind,value) && !pstk_focused()) {InterlockedIncrement64(&pstk_local_unfocused);return;}   /* focus rule, a release runs */
        pstk_local_note(kind,id,0);
        /* A presenting client whose slot is dead and not in a suit has no player to act as: X4's singleton is slot 1's. */
        if(pres_slot && pilot_slots[pres_slot-1].dead && !pres_has_seat()) {InterlockedIncrement64(&pstk_dropped);return;}
        pstk_local_run(kind,map,id,value);
        return;
    }
    if(verdict==PSTK_REFUSE) {
        int shown=kind==PSTK_ACTION?1:kind==PSTK_STATE?(int)value:0;
        if(kind==PSTK_RANGE) {float f;memcpy(&f,&value,4);shown=fabsf(f)>=0.1f?(f>0?1:-1):0;}
        /* Fire never runs in X4's own path here: with pilot fire its held state rides the record. */
        pnat_fire_input(kind,id,shown!=0);
        if(pfoot_armed && pfoot_id(kind,id)==PFOOT_REFUSE_ID) InterlockedIncrement64(&pfoot_refused_pick);
        if(pfire_armed && pstk_is_fire(kind,id)) return;
        pnat_refuse(kind,id,shown);return;
    }
    if(pstk_capturing && pstk_engages(kind,value) && !pstk_focused()) {InterlockedIncrement64(&pstk_unfocused);return;}
    if(pstk_capturing) pstk_record_event(kind,(unsigned)id,value);else InterlockedIncrement64(&pstk_dropped);
}
/* SPACE on this client: only the input object's side of d88710 (mouse steering
 * save/restore, 9062/9064 as d9c590/d9c250 leave them) and the context refresh;
 * the capturing client records the edge. Flight control and the handoff are replayed. */
static void pstk_space(unsigned char active) {
    volatile unsigned char *input=NULL;
    if(!read_local(pnat_base+PNAT_INPUT,(void*)&input,8) || !input) {pstk_async_fail("space_input");return;}
    if(active) {
        if(input[0x9053]) {
            unsigned char *options=NULL;
            if(read_local(pnat_base+PNAT_OPTIONS,&options,8) && options &&
               pnat_fn.option(options+0x118,(const char*)(pnat_base+PNAT_PERSISTENT_NAME),0)) {
                pnat_fn.steer_mode((void*)input,(char)*PNAT_U8(PNAT_STEER_SAVED));
                pnat_fn.steer_direct((void*)input,(char)*PNAT_U8(PNAT_DIRECT_SAVED));
            }
            *PNAT_U8(PNAT_STEER_SAVED)=0;*PNAT_U8(PNAT_DIRECT_SAVED)=0;
        }
        input[0x9064]=1;input[0x9062]=1;
    } else {
        unsigned char was=input[0x9056];void **cursor=NULL;
        *PNAT_U8(PNAT_STEER_SAVED)=was;
        if(was) {
            input[0x9056]=0;memcpy(&cursor,(const void*)(input+0x8fe0),8);
            if(cursor) ((void(*)(void*,char))(*(void***)cursor)[1])(cursor,0);
        }
        was=input[0x9058];*PNAT_U8(PNAT_DIRECT_SAVED)=was;
        if(was) {
            input[0x9058]=0;pnat_fn.steer_reset((void*)input,0);memcpy(&cursor,(const void*)(input+0x8fe0),8);
            if(cursor) ((void(*)(void*,char))(*(void***)cursor)[2])(cursor,0);
        }
        input[0x9064]=0;input[0x9062]=0;
    }
    pstk_local_active=active?1:0;InterlockedIncrement64(&pstk_space_local);
    /* K1 (lockstep_pilot_foot.h): on a client of slot 2..N an edge of the canonical player's seat is not its slot's. */
    if(pstk_capturing && !pfoot_space_foreign(active)) pstk_record_event(PSTK_SPACE,8,active?1u:0u);
    pnat_fn.refresh(pfoot_refresh_answer((char)(pnat_fn.controller(pnat_base+PNAT_CONTROLLER_KEY)!=NULL)));
}
/* The simulation half of d88710 on every client, in its order; the input object's
 * 9062/9064 written by d9c590/d9c250 are put back (they follow this client's UI). */
static void pstk_space_replay_slot(unsigned slot,unsigned char active,double now) {
    unsigned char *controller=(unsigned char*)pnat_fn.controller(pnat_base+PNAT_CONTROLLER_KEY),*player=pnat_player();
    volatile unsigned char *input=pnat_input;unsigned char keep62=input?input[0x9062]:0,keep64=input?input[0x9064]:0;
    if(!active) {
        *PNAT_U8(PNAT_FLIGHT)=0;
        static const unsigned zero8[]={0x2f4b750,0x2f4b788,0x2f4b720,0x2f4b794},zero4[]={0x2f4b758,0x2f4b728};
        for(unsigned i=0;i<4;i++) *(volatile uint64_t*)(pnat_base+zero8[i])=0;
        for(unsigned i=0;i<2;i++) *(volatile uint32_t*)(pnat_base+zero4[i])=0;
        if(controller && !*(volatile uint64_t*)(pnat_base+PNAT_AUTOPILOT)) pstk_fn.release(controller);
        if(player) pstk_fn.stopped(player);
    } else {
        for(unsigned i=0;i<5;i++) ((volatile double*)(pnat_base+PNAT_TIMERS))[i]=now+0.5;
        if(controller) {
            unsigned char authority=pstk_fn.authority(controller);
            if(!*(volatile uint64_t*)(pnat_base+PNAT_AUTOPILOT)) pstk_fn.handoff(controller,(char)authority);
        }
        *PNAT_U8(PNAT_FLIGHT)=1;
        if(player) pstk_fn.started(player);
    }
    if(input) {input[0x9062]=keep62;input[0x9064]=keep64;}
    if(slot>=2) {pstk_slot_frames[slot-1].flight=active?1u:0u;pstk_slot_frames[slot-1].flight_known=1;}
    else {pstk_flight=active?1u:0u;pstk_flight_known=1;}
    InterlockedIncrement64(&pstk_space_replayed);
}
static void pstk_space_replay(unsigned char active,double now) {pstk_space_replay_slot(1,active,now);}
/* d88a10 in stock mode, every client, inside this frame's d95180: the stock body
 * runs (with flight control for its own call on the capturing client while its
 * own controls are active but the replayed state is still off), the capturing
 * client keeps its outputs for the record, and every client's d95180 then reads
 * the replayed values of this tick (zero before the first replayed record). */
static void pstk_cursor(void *controller) {
    InterlockedIncrement64(&pstk_cursor_calls);
    volatile unsigned char *flight=PNAT_U8(PNAT_FLIGHT);unsigned char was=*flight;
    unsigned scope=pstk_scope_slot;
    int window=pstk_capturing && pstk_local_active==1 && !was && !scope;
    if(window) *flight=1;
    __try {pnat_fn.cursor_original(controller);} __finally {if(window) *flight=was;}
    volatile float *out=PNAT_F32(0x2f4b738);
    /* The local device is read in the canonical update only (the slot scopes run on stored banks). */
    /* ...with the focus only: without it the record's cursor is neutral (the focus rule, pstk_aim_take). */
    if(pstk_capturing && !scope) {
        const int focused=pstk_focused();
        for(unsigned i=0;i<3;i++) pstk_cursor_live[i]=focused?out[i]:0.0f;
        pstk_cursor_live_set=1;
        if(!focused) InterlockedIncrement64(&pstk_cursor_unfocused);
    }
    int replay;const float *source;
    if(scope>=2) {
        const pstk_slot_frame *f=&pstk_slot_frames[scope-1];
        replay=f->tick && f->tick==stats.issued && (f->r.flags&PSTK_FLAG_CURSOR);source=f->r.cursor;
    } else {replay=pstk_cursor_tick && pstk_cursor_tick==stats.issued;source=pstk_cursor_replay;}
    for(unsigned i=0;i<3;i++) out[i]=replay?source[i]:0.0f;
}
/* PlayerShootController::vfunc21 on every client (vtable slot 21, from the first
 * pass of the session to stop+1): the stock call runs on the replayed aim of this
 * tick (or the neutral one) and the four camera fields are put back afterwards.
 * Weapon::Move calls it on the frame thread; a call from any other thread (a
 * presentation reader) gets the stock result and is counted (foreign). */
static unsigned char pstk_aim(void *controller,float *first,float *second,void *space,void *weapon,void *transform,float a7,float a8,
                              unsigned char a9,unsigned char a10,unsigned char a11,unsigned char a12,unsigned char a13) {
    InterlockedIncrement64(&pstk_aim_calls);
    if(!InterlockedCompareExchange(&pstk_aim_on,0,0))
        return pstk_fn.aim(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    if(GetCurrentThreadId()!=ui_thread) {
        InterlockedIncrement64(&pstk_aim_foreign);
        return pstk_fn.aim(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    }
    if(pstk_scope_slot) {InterlockedIncrement64(&pstk_aim_scoped);return pstk_fn.aim(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);}
    unsigned char *player=pnat_player(),*ship=pstk_ship_of(player),*zone=player && pstk_fn.zone?pstk_fn.zone(player):NULL;
    volatile unsigned char *input=NULL;
    if(!read_local(pnat_base+PNAT_INPUT,(void*)&input,8) || !player || !ship || !zone || !input || !pstk_fn.transform) {
        /* A dying or dead slot 1: X4's player may be between ships. */
        int dying=pilot_slots[0].dead;double hull=1.0;
        if(!dying && pdeath_armed && pilot_slots[0].found) dying=read_local(pilot_slots[0].found+PDEATH_HULL,&hull,8) && fabs(hull)<PDEATH_HULL_EPSILON;
        /* Slot 1 off its seat (lockstep_pilot_foot.h): no ship of the player's to aim from; X4's own answer, counted. */
        if(!dying && pfoot_walking()) {InterlockedIncrement64(&pfoot_aim_off_seat);return pstk_fn.aim(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);}
        if(!dying) pstk_async_fail("stock_aim_resolve");
        return pstk_fn.aim(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    }
    float p[3]={0.0f,0.0f,PSTK_AIM_NEUTRAL},t[16],q[4]={0.0f,0.0f,0.0f,0.0f},valid=1.0f,saved_distance=0.0f;unsigned char steer=0;
    if(pstk_aim_tick && pstk_aim_tick==stats.issued && pstk_aim_valid_replay) {memcpy(p,pstk_aim_replay,sizeof(p));steer=pstk_aim_steer_replay&1u;}
    else InterlockedIncrement64(&pstk_aim_neutral);
    pstk_identity(t);pstk_fn.transform(ship,t,zone,0);pstk_apply(t,p,q);
    unsigned char saved_point[16],saved_picked[8],saved_steer=input[0x9053];
    memcpy(saved_point,player+0x6c60,16);memcpy(&saved_distance,player+0x6c80,4);memcpy(saved_picked,player+0x6c88,8);
    memcpy(player+0x6c60,q,16);memcpy(player+0x6c80,&valid,4);memcpy(player+0x6c88,&player,8);input[0x9053]=steer;
    unsigned char result=pstk_fn.aim(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    memcpy(player+0x6c60,saved_point,16);memcpy(player+0x6c80,&saved_distance,4);memcpy(player+0x6c88,saved_picked,8);input[0x9053]=saved_steer;
    InterlockedIncrement64(&pstk_aim_replaced);
    return result;
}
/* Scripted input for the capturing client (smokes without a human): one control id
 * of the replay set, recorded as if X4 had dispatched it. */
static int pstk_inject(unsigned kind,unsigned id,uint32_t value) {
    if(!pstk_capturing || !pnat_is_live() || kind>PSTK_RANGE || pstk_verdict_slot(kind,(int)id,pstk_capture_slot)!=PSTK_REPLAY) return 0;
    if(kind==PSTK_ACTION && value) return 0;
    if(kind==PSTK_STATE && value>1) return 0;
    if(kind==PSTK_RANGE) {float f;memcpy(&f,&value,4);if(!isfinite(f)) return 0;}
    if(!pstk_record_event(kind,id,value)) return 0;
    InterlockedIncrement64(&pstk_injected);return 1;
}
/* Scripted local input for runs without people (gate.pilot_stock_local, native_gate_pilot_local_script):
 * one presentation id of this client, as if its own key had dispatched it (a key of the replay or
 * refused sets is not one: 0). The probe queues it in its frame (inside a Lua call, where X4's
 * handlers must not run); the next pass runs it on the frame thread from path_release, in C as the
 * stock replay runs its ids, through the local path a real key takes (pstk_local_run). */
#define PSTK_LOCAL_QUEUE 8u
static struct {unsigned kind,id;uint32_t value;} pstk_local_queue[PSTK_LOCAL_QUEUE];
static unsigned pstk_local_queued;
static int pstk_local_press(unsigned kind,unsigned id,uint32_t value) {
    if(!pnat_is_live() || GetCurrentThreadId()!=ui_thread || kind>PSTK_RANGE || id>=512 || pstk_local_queued>=PSTK_LOCAL_QUEUE) return 0;
    if(pstk_verdict_slot(kind,(int)id,pstk_capturing?pstk_capture_slot:1)!=PSTK_LOCAL) return 0;
    if((kind==PSTK_ACTION && value) || (kind==PSTK_STATE && value>1)) return 0;
    if(kind==PSTK_RANGE) {float f;memcpy(&f,&value,4);if(!isfinite(f)) return 0;}
    pstk_local_queue[pstk_local_queued].kind=kind;pstk_local_queue[pstk_local_queued].id=id;pstk_local_queue[pstk_local_queued].value=value;
    ++pstk_local_queued;return 1;
}
/* The pass (pnat_pass, stock mode, live): every queued key once, in order. */
static void pstk_local_drain(void) {
    const unsigned n=pstk_local_queued;pstk_local_queued=0;
    if(!pnat_input) return;
    for(unsigned i=0;i<n;i++) {
        /* A presenting client whose slot is dead and not in a suit has no player to act as (as pstk_dispatch). */
        if(pres_slot && pilot_slots[pres_slot-1].dead && !pres_has_seat()) {InterlockedIncrement64(&pstk_dropped);continue;}
        pstk_local_note(pstk_local_queue[i].kind,(int)pstk_local_queue[i].id,1);
        pstk_local_run(pstk_local_queue[i].kind,pnat_input+0x63e0,(int)pstk_local_queue[i].id,pstk_local_queue[i].value);
        InterlockedIncrement64(&pstk_local_pressed);
    }
}
/* Replay of this tick's record (every client, path_release, before the pass). */
static int pstk_frame(uint64_t tick,const char **why) {
    pstk_frame_tick=tick;pstk_frame_events=0;pstk_frame_hash=0;
    if(!(pstk_settings=pstk_settings_using(pnat_base))) {*why="stock_settings";return 0;}
    if(tick<pstk_first_apply || tick>pilot_stop) return 1;
    pstk_staged *e=&pstk_ring[0][tick%PSTK_RING];pstk_record r;double now=0;
    /* Slot 1 dropped (protocol 5, lockstep_pilot_inputs.h): no record from c+delay; the canonical update replays nothing
     * (as for a dead slot: no view, no trigger), its hash is 0, the slots 2..N below go on. */
    const int silent1=(pstk_slot_mask&1u) && tick>=pilot_delay && pilot_slot_dropped(1,tick-pilot_delay);
    unsigned char *map=pnat_input?pnat_input+0x63e0:NULL;
    if(silent1) {
        if(e->present && e->tick==tick) {*why="stock_absent_record";return 0;}
        if(!map) {*why="stock_time";return 0;}
        if(pstk_release_due(1,tick) && !pilot_slots[0].dead) { /* its keys released and the stop action, once */
            pstk_record q;pstk_release_record(1,&q);
            for(unsigned i=0;i<q.count;i++) if(!pstk_replay_handlers_ok(map,&q.events[i])) {*why="stock_foreign_handler";return 0;}
            for(unsigned i=0;i<q.count;i++) pstk_replay_walk(map+0x40*q.events[i].kind,q.events[i].kind,q.events[i].id,q.events[i].value);
            pstk_frame_events=q.count;
        }
        pview_replay(1,tick,0,0,0.0f,0);pstk_fire_tick[0]=tick;pstk_fire_on[0]=0;pstk_fire2_on[0]=0;
        InterlockedIncrement64(&pilot_drop_skips);
        if(pfoot_armed) pfoot_frame();
        pstk_frame_hash=0;
    }
    else if(!e->present || e->tick!=tick) {*why="stock_missing";return 0;}
    if(!silent1) {
        if(!pstk_decode(e->bytes,e->length,&r) || r.capture16!=(uint16_t)(tick-pilot_delay)) {*why="stock_delay_mismatch";return 0;}
        if(!pnat_input || !pnat_fn.time(&now)) {*why="stock_time";return 0;}
        if(pilot_slots[0].dead) { /* consumed and ignored: its hash stays in the declared state, cursor and aim stay neutral */
            pview_replay(1,tick,0,0,0.0f,0);
            pstk_fire_tick[0]=tick;pstk_fire_on[0]=0;pstk_fire2_on[0]=0;
        } else {
            for(unsigned i=0;i<r.count;i++) { /* every handler is a stock one before any runs */
                pstk_event *x=&r.events[i];
                if(x->kind==PSTK_SPACE) continue;
                if(!pstk_replay_handlers_ok(map,x)) {*why="stock_foreign_handler";return 0;}
            }
            for(unsigned i=0;i<r.count;i++) {
                pstk_event *x=&r.events[i];
                if(x->kind==PSTK_SPACE && x->id==PFOOT_CONTEXT) pfoot_context_replay((unsigned char)x->value,tick);
                else if(x->kind==PSTK_SPACE) pstk_space_replay((unsigned char)x->value,now);
                else {pstk_replay_walk(map+0x40*x->kind,x->kind,x->id,x->value);pstk_hold(1,x);if(pdeath_armed) pdeath_note(x);pfoot_note_replay(x->kind,x->id,tick);}
            }
            InterlockedExchangeAdd64(&pstk_replayed,(LONG64)r.count);
            if(r.flags&PSTK_FLAG_CURSOR) memcpy(pstk_cursor_replay,r.cursor,sizeof(pstk_cursor_replay));
            else memset(pstk_cursor_replay,0,sizeof(pstk_cursor_replay));
            pstk_aim_valid_replay=(r.flags&PSTK_FLAG_AIM)!=0;memcpy(pstk_aim_replay,r.aim,sizeof(pstk_aim_replay));pstk_aim_steer_replay=r.steer;
            pstk_aim_tick=tick;
            pview_replay(1,tick,(r.flags&PSTK_FLAG_VIEW)!=0,r.view,r.fov,r.target);
            pstk_fire_tick[0]=tick;pstk_fire_on[0]=(r.flags&PSTK_FLAG_FIRE)!=0;pstk_fire2_on[0]=(r.flags&PSTK_FLAG_FIRE2)!=0;
            pstk_cursor_tick=tick;pstk_frame_events=r.count;
        }
        if(pfoot_armed) pfoot_frame();
        pstk_frame_hash=pstk_fnv(e->bytes,e->length,PSTK_DOMAIN);
        e->present=0;InterlockedIncrement64(&pstk_records);
    }
    /* Slots 2..N: staged here, replayed in their scopes after the canonical update (lockstep_pilot_slots.h). */
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        if(!(pstk_slot_mask>>(k-1)&1u)) continue;
        pstk_staged *s=&pstk_ring[k-1][tick%PSTK_RING];pstk_slot_frame *f=&pstk_slot_frames[k-1];pstk_record q;
        /* Absent (hot join, lockstep_pilot_inputs.h): no record before H+delay; dropped (protocol 5): none from c+delay.
         * Neutral: no events, no view, no trigger; its frame keeps its last tick, so the receipt says S<k>:0:0 and the slot
         * scope replays nothing. */
        if(pilot_slot_silent(k,tick,pilot_delay)) {
            if(s->present && s->tick==tick) {*why="stock_absent_record";return 0;}
            if(pstk_release_due(k,tick) && !pilot_slots[k-1].dead) { /* its keys released and the stop action, once */
                pstk_release_record(k,&f->r);
                for(unsigned i=0;i<f->r.count;i++) if(!pstk_replay_handlers_ok(map,&f->r.events[i])) {*why="stock_foreign_handler";return 0;}
                f->tick=tick;f->hash=0;
            }
            pview_replay(k,tick,0,0,0.0f,0);pstk_fire_tick[k-1]=tick;pstk_fire_on[k-1]=0;pstk_fire2_on[k-1]=0;
            InterlockedIncrement64(tick>=pilot_delay && pilot_slot_dropped(k,tick-pilot_delay)?&pilot_drop_skips:&pilot_absent_skips);continue;
        }
        if(!s->present || s->tick!=tick) {*why="stock_missing_slot";return 0;}
        if(!pstk_decode(s->bytes,s->length,&q) || q.capture16!=(uint16_t)(tick-pilot_delay) || !pstk_slot_record_ok(&q,k)) {
            *why="stock_delay_mismatch";return 0;
        }
        for(unsigned i=0;i<q.count;i++) {
            pstk_event *x=&q.events[i];
            if(x->kind==PSTK_SPACE) continue;
            if(!pstk_replay_handlers_ok(map,x)) {*why="stock_foreign_handler";return 0;}
        }
        f->tick=tick;f->r=q;f->hash=pstk_fnv(s->bytes,s->length,PSTK_DOMAIN+k);s->present=0;
        /* A dead slot's record is consumed the same way: pslot_replay skips it (not seated). */
        int dead=pilot_slots[k-1].dead!=0;
        pview_replay(k,tick,!dead && (q.flags&PSTK_FLAG_VIEW)!=0,q.view,q.fov,q.target);
        pstk_fire_tick[k-1]=tick;pstk_fire_on[k-1]=!dead && (q.flags&PSTK_FLAG_FIRE)!=0;pstk_fire2_on[k-1]=!dead && (q.flags&PSTK_FLAG_FIRE2)!=0;
        InterlockedIncrement64(&pstk_records);
    }
    return 1;
}
/* Slot k's record of this tick, replayed inside its scope (singleton = the slot's
 * player, the bank = the slot's): SPACE edges through the simulation half of
 * d88710, every other event through the stock handlers. */
static int pstk_slot_replay(unsigned k,double now) {
    pstk_slot_frame *f=&pstk_slot_frames[k-1];
    if(k<2 || k>PILOT_SLOTS || f->tick!=stats.issued || !pnat_input) return 0;
    unsigned char *map=pnat_input+0x63e0;
    for(unsigned i=0;i<f->r.count;i++) {
        pstk_event *x=&f->r.events[i];
        if(x->kind==PSTK_SPACE) pstk_space_replay_slot(k,(unsigned char)x->value,now);
        else if(pfslot_armed && x->kind==PSTK_ACTION && x->id==PFOOT_GET_UP) {if(!pfslot_get_up(k,now)) return 0;}   /* never 71bac0 for P_k */
        else {pstk_replay_walk(map+0x40*x->kind,x->kind,x->id,x->value);pstk_hold(k,x);}
    }
    InterlockedExchangeAdd64(&pstk_replayed,(LONG64)f->r.count);
    return 1;
}
static int hyb_slot_private(unsigned slot); /* lockstep_hybrid.h: an owner's ship belongs to its sector's state */
/* The declared-state receipt of this tick: applied record, flight state, settings. With hybrid sectors
 * (native_gate_hybrid) an owning slot's flight state is its sector's, not shared: 'h' instead.
 * PSTK_FRAME_TEXT holds it for PILOT_SLOTS slots with every part armed (about 400 bytes with eight slots; a 320 byte
 * buffer held six slots and stopped an eight-slot session at its first step). */
#define PSTK_FRAME_TEXT 1024u
static int pstk_frame_text(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"PSTOCK_1:%llu:%u:%016llx:%u:%016llx",(unsigned long long)pstk_frame_tick,pstk_frame_events,
        (unsigned long long)pstk_frame_hash,pstk_flight,(unsigned long long)pstk_settings);
    for(unsigned k=2;n>0 && (size_t)n<capacity && k<=PILOT_SLOTS;k++) {
        const pstk_slot_frame *f=&pstk_slot_frames[k-1];
        if(!(pstk_slot_mask>>(k-1)&1u)) continue;
        int fresh=f->tick==pstk_frame_tick;
        int more=hyb_slot_private(k)?snprintf(out+n,capacity-(size_t)n,"|S%u:%u:%016llx:h",k,fresh?f->r.count:0u,(unsigned long long)(fresh?f->hash:0)):
                 snprintf(out+n,capacity-(size_t)n,"|S%u:%u:%016llx:%u",k,fresh?f->r.count:0u,(unsigned long long)(fresh?f->hash:0),f->flight);
        if(more<0) return 0;
        n+=more;
    }
    if(n>0 && (size_t)n<capacity && pfoot_armed) { /* slot 1 on foot: its state and walking bank */
        int more=pfoot_frame_text(out+n,capacity-(size_t)n);
        if(!more) return 0;
        n+=more;
    }
    if(n>0 && (size_t)n<capacity && pfslot_armed) { /* slots 2..N on foot: each one's state, walking bank and FPC pose */
        int more=pfslot_frame_text(out+n,capacity-(size_t)n);
        if(!more) return 0;
        n+=more;
    }
    if(n>0 && (size_t)n<capacity && pdeath_armed) { /* who has no ship: the same tick on every client */
        int more=snprintf(out+n,capacity-(size_t)n,"|D%x",pdeath_dead);
        if(more<0) return 0;
        n+=more;
    }
    return n>0 && (size_t)n<capacity?n:0;
}
static int pdeath_status(char *out,size_t capacity);                       /* lockstep_pilot_death.h */
static int pslot_armed;static int pslot_status(char *out,size_t capacity); /* lockstep_pilot_slots.h */
static int pres_armed;static int pres_status(char *out,size_t capacity);   /* lockstep_pilot_present.h */
static int pfbody_armed;static int pfbody_status(char *out,size_t capacity); /* lockstep_pilot_foot_body.h */
static int pstk_status(char *out,size_t capacity) {
    char keys[PSTK_LOCAL_KEYS*32+2];pstk_local_keys_text(keys,sizeof(keys));
    int n=snprintf(out,capacity," stock=%s slot=%u slots=%x flight=%u/%u capture_start=%llu first_apply=%llu recorded=%lld dropped=%lld "
        "replayed=%lld records=%lld staged=%lld space=%lld/%lld cursor=%lld injected=%lld unfocused=%lld overflows=%lld max_events=%lld settings=%016llx "
        "aim=%d/%lld/%lld/%lld/%lld/%lld/%lld local_keys=%s local_ranges=%lld local_pressed=%lld releases=%lld/%lld "
        "unfocused_drops=%lld/%lld/%lld",
        pstk_capturing?"capture":"replay",pstk_capturing?pstk_capture_slot:pstk_slot,pstk_slot_mask,pstk_flight,pstk_flight_known,
        (unsigned long long)pstk_capture_start,
        (unsigned long long)pstk_first_apply,(long long)pstk_recorded,(long long)pstk_dropped,(long long)pstk_replayed,(long long)pstk_records,
        (long long)pstk_staged_count,(long long)pstk_space_local,(long long)pstk_space_replayed,(long long)pstk_cursor_calls,
        (long long)pstk_injected,(long long)pstk_unfocused,(long long)pstk_overflows,(long long)pstk_max_events,(unsigned long long)pstk_settings,
        pstk_aim_installed,(long long)pstk_aim_calls,(long long)pstk_aim_replaced,(long long)pstk_aim_neutral,(long long)pstk_aim_samples,
        (long long)pstk_aim_sample_failures,(long long)pstk_aim_foreign,keys,(long long)pstk_local_ranges,(long long)pstk_local_pressed,
        (long long)pstk_releases,(long long)pstk_released_events,(long long)pstk_cursor_unfocused,(long long)pstk_aim_unfocused,
        (long long)pstk_local_unfocused);
    if(n<=0 || (size_t)n>=capacity) return 0;
    int more=pview_status(out+n,capacity-(size_t)n);
    if(!more) return 0;
    n+=more;
    if(pslot_armed) {more=pslot_status(out+n,capacity-(size_t)n);if(!more) return 0;n+=more;}
    if(pres_armed) {more=pres_status(out+n,capacity-(size_t)n);if(!more) return 0;n+=more;}
    if(pdeath_armed) {more=pdeath_status(out+n,capacity-(size_t)n);if(!more) return 0;n+=more;}
    if(pfoot_armed) {more=pfoot_status(out+n,capacity-(size_t)n);if(!more) return 0;n+=more;}
    if(pfbody_armed) {more=pfbody_status(out+n,capacity-(size_t)n);if(!more) return 0;n+=more;}
    if(pfslot_armed) {more=pfslot_status(out+n,capacity-(size_t)n);if(!more) return 0;n+=more;}
    return n;
}

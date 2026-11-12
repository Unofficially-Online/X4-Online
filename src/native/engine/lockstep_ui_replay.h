/* UI replay, DEFAULT OFF, stop reason 164 (native_gate_ui_replay; needs stock slots, the presentation
 * and native_gate_runtime_ids). Stock UI actions that change the world become events of this client's
 * slot: captured before they mutate, carried by the input wire (section 'ui', kind 0x55), applied on
 * every client at the capture tick plus the agreed delay, in the slot's scope, through X4's own function.
 * Design and inventory: reports/LOCKSTEP-UI-REPLAY-20261030.txt.
 *
 *   capture   only in a local phase: slot 6 (UI::CUIController::Move, entry hooked here), slot 10 (the
 *             render split's flag), the slot 4 dispatch of presentation ids, an open presentation scope.
 *             The local client never applies early. Simulation code on the same functions passes through.
 *   families  travel  0x9fd120 / 0x9fd330, where StartPlayerActivity, StopPlayerActivity and the action
 *                     handler d86c80 all end: post startactivity / stopactivity on the singleton. The J
 *                     key rides the stock record (action 0x12f, replayed for slots 2+ with this family).
 *             target  the two simulation blocks inlined in the UI's soft target writer 0xaecb60 (0xaecfdc
 *                     clear, 0xaed115 set) jump to a stub; the UI half keeps running at once. Match speed
 *                     (state 0x5b) leaves the refused list: it reads the target through 9fcd70, per scope.
 *                     Outside the local phases only MD's set/clear_player_target is the simulation's (stock at
 *                     once); the UI's own writes there (its target element dying with its component) are
 *                     captured like a click (uirep_target_route; reports/LOCKSTEP-SUIT-VIEW-20261101.txt).
 *             orders  RemoveAllOrders, RemoveAllOrders2 captured natively, Lua CreateOrder wrapped in Lua
 *                     (tools/dev/native_gate_ui_replay_core.lua), the queue editor's FFI mutators refused
 *                     here with their failure value (UIREP_GUARD), counted.
 *             autopilot  0x72bf20 (start: player, target, offset, flags) and 0x72bb10 (stop), where ToggleAutoPilot,
 *                     the key (TOGGLE_AUTOPILOT 0xb3, now run on the client's own dispatch so it reaches them) and Lua
 *                     StartAutoPilot / StopAutoPilot end: the target is resolved on the capturing client from its own
 *                     guidance (the mission manager, which stays local) and travels with the event.
 *             dock    0x730ff0 (QuickDock's core), RequestDockAt, RequestDockAtReason, 0x72fe40 (UndockPlayerShip's
 *                     core); the keys DOCK_ACTION 0xaf and UNDOCK 0xd3 ride the stock record into 71bac0 in the slot's scope.
 *             weapons SetDefensibleActiveWeaponGroup, SetWeaponGroup, SetWeaponMode; the group keys (0x8..0xb, 0x8b..0x9e,
 *                     ammo 0x141) ride the stock record into af1db0 in the slot's scope.
 *             flight  0x710d50, the player's flight assist toggle, where ToggleFlightAssist (a tail jump on the singleton)
 *                     and the key's handler 71bac0 (TOGGLE_FLIGHT_ASSIST 0xdd, already on the stock record) end: it flips the
 *                     player's +0x6aac/+0x6aad and sets the controller 9fc550 gives (0x710ad0 / 0x710cb0); in slot k's scope
 *                     those are P_k and C_k. The HUD's own-ship buttons (crosshair handling.lua) CycleTurretMode,
 *                     CycleTurretMissile, ToggleDrones and CycleNextDroneMode find the ship through the singleton (+0x70
 *                     walk, +0x308 vector: slot k's ship in its scope). Applied in slot k's scope on every client.
 *                     Deployables: LaunchSatellite, LaunchNavBeacon, LaunchResourceProbe, LaunchMine, LaunchLaserTower
 *                     (ship, macro name; the ship interact menu and the map's deploy tables). Each looks the ship and
 *                     the macro up and calls its core (0x4e94f0, 0x4ea370, 0x4eaa30, 0x4e9ba0, 0x4eb0f0): one unit off
 *                     the ship's stock (+0x510), the launch pose from the ship, the object through
 *                     ComponentFactory::Create 0x8e0250, the launch events at the thread's game time. The create draws
 *                     from the thread's RNG word (the constructor's seed 0x6bb620, the loop at 0x3cf286): at the apply
 *                     that is the simulation's word at the same point of the tick on every client, and the new object
 *                     takes a shared runtime ID. The deploy keys (DEPLOY_* 0x159..0x160 -> 71bac0 -> 0x73ff50 and its
 *                     siblings) stay refused: another path with its own draw.
 *                     A named ship's settings from the docked menu, the map and the interact menu: SetWeaponArmed,
 *                     SetAmmoOfWeapon, SetTurretGroupMode2 / SetTurretGroupArmed (ship, the group's context or 0, path,
 *                     group), SetAll[Missile|NonMissile]TurretModes / ...TurretsArmed, SetDroneMode, SetDroneTypeArmed,
 *                     SetAllDronesArmed; a deployable's ActivateObject and SelfDestructComponent; DropCargo (container,
 *                     ware, amount). Each names its object: no singleton, the same call in any scope. None draws from
 *                     the RNG in its direct calls three deep (static reading).
 *             These are generic calls (op 5): a hook's arguments by a spec (the player, components as references, bytes,
 *             words, 16-byte vectors, strings), a check-only argument passes through and predicts the return value,
 *             and the apply calls the same function through its trampoline with the slot's player.
 *   apply     native events in path_release after the stock records of the tick, slot 1 canonical, slot k
 *             in its slot scope: 0x710ed0 for travel in a scope (md/modes.xml does not listen on P_k),
 *             0x9fd120 / 0x9fd330 on slot 1 (MD on every client); 0x3e73f0 / 0x3e7560 for the soft target.
 *             Lua-run events (op >= 0x40) in slot 6 of the same tick from the probe's frame, canonical.
 *   section   version | capture tick low 16 bits | count | events: op u8, length u8, data. A component is
 *             its runtime ID u64 and a 32-bit check of its portable ID (little endian): runtime IDs are the
 *             same on every client under native_gate_runtime_ids, a check that differs stops the session.
 *   events  (P3, slot 1 only) AddUITriggeredEvent and SignalObject from slot 1's menus as Lua-run events (op 0x47): MD's
 *             player.entity is slot 1's player, so the event means the same on every client; a section of another slot
 *             holding one stops (ui_events). Slots 2+ are refused by the Lua core (and by lockstep_ui_guard.h natively).
 *   guard     lockstep_ui_guard.h (native_gate_ui_guard): what no armed family captures is refused in the local phases.
 *   receipt   PUIREP_1:tick:events:hash:results|activity/target of every slot's player (with the flight family also
 *             /f<assist>.<drones armed, +0x6a4..+0x6a6> of the slot's player and ship) joins the pilot receipt, so
 *             an event that took on one client only shows at once, whatever it was.
 * Refused at capture (nothing ran anywhere, nothing diverged): counted and logged (UI_REPLAY_REFUSED). A
 * section or reference that fails, a hook lost, a fault in a replayed call: stop reason 164. */
static int mpp_armed;static void mpp_restore_hook(void);   /* lockstep_mission_owners.h: slots 2+ apply the events family in their own scope when armed */
#define UIREP_STOP_REASON 164u
#define UIREP_VERSION 1u
#define UIREP_HEAD 4u
#define UIREP_SECTION_MAX 236u      /* INPUT_MUX_MAX less the container and this section's header */
#define UIREP_EVENT_MAX 200u
#define UIREP_EVENTS_MAX 32u
#define UIREP_QUEUE 64u
#define UIREP_RING 64u
#define UIREP_REF 12u
#define UIREP_OFFSET 16u
#define UIREP_DOMAIN 0x5549524550315631ULL /* "UIREP1V1" */
#define UIREP_ACTIVITY_TRAVEL 1
#define UIREP_ACTIVITY_MAX 4
#define UIREP_PLAYER_ACTIVITY 0x6ab4u
#define UIREP_PLAYER_LAST_SHIP 0x6a30u    /* last controlled ship's runtime ID: 0x710ed0 stops travel on it */
#define UIREP_LAST_TARGET 0x38f0a88u      /* cycling hint of the target actions, UI only; 0x3e73f0 writes it too */
#define UIREP_MAP 0x6cf1500u
#define UIREP_ASSOCIATION 0x48u           /* Component: UI::ICom::UIAssociation, slots 4 and 5 are the two functions */
#define UIREP_ELEMENT_OFFSET 0x20u        /* UI target element: offset in the target, +0x6e30 of the player */
#define UIREP_ELEMENT_FLAG 0x31u          /* UI target element: +0x6e20 of the player */
#define UIREP_ID 8u                       /* Component: its runtime ID */
#define UIREP_DRIVE_ACTIVE 0x340u         /* the travel drive of 0x6d6d80: charging or active (0xea7650) */
#define UIREP_SHIP_CAN_TRAVEL 1008u       /* Ship vtable: CanStartTravelMode's call (+0x1f80) */
#define UIREP_PAGE 0x1000u
#define UIREP_STRIDE 0x40u
#define UIREP_STUB 0x20u                  /* in a hook's stride: the jump the entry patch targets */
enum {UIREP_OP_ACTIVITY_START=1,UIREP_OP_ACTIVITY_STOP=2,UIREP_OP_TARGET_SET=3,UIREP_OP_TARGET_CLEAR=4,UIREP_OP_CALL=5,
    UIREP_OP_LUA_FIRST=0x40,UIREP_OP_ORDERS_CLEAR=0x41,UIREP_OP_ORDERS_CLEAR2=0x42,UIREP_OP_LUA_CALL=0x43,
    UIREP_OP_QUEUE=0x44,UIREP_OP_QUEUE_PARAM=0x45,UIREP_OP_QUEUE_LIST=0x46,UIREP_OP_LUA_GLOBAL=0x47,
    UIREP_OP_MISSION=0x48,UIREP_OP_CONVERSATION=0x49};   /* lockstep_ui_missions.h */
/* The events family (P3, slot 1 only): Lua C globals the Lua core wraps by name and replays, numbered 1.. on the wire
 * (native_gate_ui_replay_core.lua K.GLOBALS: AddUITriggeredEvent, SignalObject). */
#define UIREP_LUA_GLOBALS 2u
/* The trade family (lockstep_ui_trade.h, included at the end): its two Lua-run ops, its section rule and its part of a pass. */
/* Component slot player actions (lockstep_ui_compslot.h, included at the end): one native op, its section rule and its
 * apply; the two perform hooks' wrappers for the table below. */
#define UICS_OP 0x08u
static int uics_event_ok(unsigned slot,unsigned op,const unsigned char *data,unsigned n,const char **why);
static int uics_run(unsigned slot,const unsigned char *data,const char **why);
static int uics_status(char *out,size_t capacity);
static unsigned char uics_perform_single(void *container,unsigned index);
static unsigned char uics_perform_multi(void *container,unsigned index);
#define UITRADE_OP_LUA 0x58u
#define UITRADE_OP_FFI 0x59u
static int uitrade_event_ok(unsigned slot,unsigned op,const unsigned char *data,unsigned n,const char **why);
/* The inventory family (tools/dev/native_gate_ui_inventory_core.lua, all Lua): one click of a stock inventory button as one
 * Lua-run event of the slot, its kind (1 craft, 2 an NPC trader's wares, 3 dropped wares, 4 blueprints or licences bought,
 * 5/6 research started or cancelled, 7/8 a terraforming project started or aborted, 9 wares sent ahead of a large trade, 10 an
 * encyclopedia entry, 11 a trade deal with a station of the player's cancelled) and then the codec's arguments. It runs on every
 * client inside the slot's scope, where X4's "the player" is the slot's own player entity and so the inventory its own:
 * slots 2..N need missions per player armed, whose call scope it is (lockstep_mission_owners.h mpp_call_enter). */
#define UIINV_OP_CLICK 0x90u
#define UIINV_KINDS 11u   /* native_gate_ui_inventory_core.lua K.KIND; 3: the menu's Drop, captured natively (lockstep_ui_inventory.h) */
static int uitrade_pass(uint64_t tick);
static void uitrade_restore(void);
static const char *uitrade_why_text;
enum {UIREP_H_UI=0,UIREP_H_START,UIREP_H_STOP,UIREP_H_CLEAR,UIREP_H_CLEAR2,UIREP_H_CREATE3,UIREP_H_ENABLE,UIREP_H_ADJUST,UIREP_H_REMOVE2,
    UIREP_H_RESET,UIREP_H_PLANNED_ENABLE,UIREP_H_PLANNED_REMOVE,UIREP_H_DEPLOY,
    UIREP_H_AP_START,UIREP_H_AP_STOP,UIREP_H_QUICKDOCK,UIREP_H_DOCKAT,UIREP_H_DOCKREASON,UIREP_H_UNDOCK,
    UIREP_H_WGROUP_ACTIVE,UIREP_H_WGROUP_SET,UIREP_H_WMODE,UIREP_H_LOOP,UIREP_H_LOOP_CURRENT,
    UIREP_H_FASSIST,UIREP_H_TURRET_MODE,UIREP_H_TURRET_MISSILE,UIREP_H_DRONES,UIREP_H_DRONE_MODE,
    UIREP_H_LAUNCH_LASERTOWER,UIREP_H_LAUNCH_MINE,UIREP_H_LAUNCH_NAVBEACON,UIREP_H_LAUNCH_PROBE,UIREP_H_LAUNCH_SATELLITE,
    UIREP_H_WEAPON_ARMED,UIREP_H_WEAPON_AMMO,UIREP_H_TGROUP_MODE,UIREP_H_TGROUP_ARMED,UIREP_H_TURRETS_MODE,UIREP_H_TURRETS_ARMED,UIREP_H_MTURRETS_MODE,UIREP_H_MTURRETS_ARMED,
    UIREP_H_NTURRETS_MODE,UIREP_H_NTURRETS_ARMED,UIREP_H_DRONE_SET_MODE,UIREP_H_DRONE_TYPE_ARMED,UIREP_H_DRONES_ARMED,UIREP_H_ACTIVATE,UIREP_H_SELF_DESTRUCT,UIREP_H_DROP_CARGO,
    UIREP_H_CS_SINGLE,UIREP_H_CS_MULTI,UIREP_HOOKS};
#define UIREP_ARGS 8u
#define UIREP_STRING_MAX 48u
#define UIREP_AUTOPILOT 0x2f4b7d8u        /* the input bank's autopilot target (SIM, per slot) */
#define UIREP_GROUP_PRIMARY 0x668u        /* Defensible: active primary weapon group, +0x66c the secondary */
#define UIREP_PLAYER_ASSIST 0x6aacu       /* the player's flight assist on (0x710d50; IsFlightAssistActive reads it) */
#define UIREP_SHIP_DRONES 0x6a4u          /* Defensible: drones armed, types 2, 1, 3 (ToggleDrones reads them) */
enum {UIREP_S_TARGET_CLEAR=0,UIREP_S_TARGET_SET=1,UIREP_SITES=2};
_Static_assert((UIREP_HOOKS+UIREP_SITES)*UIREP_STRIDE<=UIREP_PAGE,"the hooks' and sites' strides fit the page");
typedef void (*uirep_activity_fn)(void *,int,unsigned char);
typedef uint64_t (*uirep_forward_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef struct {
    void (*ui_move)(void *);
    uirep_activity_fn start,stop;                  /* 0x9fd120, 0x9fd330 */
    void (*set_activity)(void *,int);              /* 0x710ed0(player, activity) */
    unsigned char (*can_travel)(void *);           /* the ship's vtable slot 1008 */
    void **(*drive)(void *,void **);               /* 0x6d6d80(ship, out): its travel drive */
    void (*target_set)(void *,const void *,unsigned char);  /* 0x3e73f0(component+0x48, offset, flag) */
    void (*target_clear)(void *);                  /* 0x3e7560(component+0x48) */
    void *(*find)(void *,uint64_t,unsigned);       /* 0xced70 */
    int (*enter)(unsigned);                        /* pslot_enter */
    int (*leave)(unsigned);                        /* pslot_leave */
    uint64_t (*portable)(uint64_t);                /* world_identity_portable */
    uirep_forward_fn forward[UIREP_HOOKS];         /* the trampolines of the call hooks */
} uirep_fns;
typedef struct {unsigned char op,length;unsigned char data[UIREP_EVENT_MAX];} uirep_event;
typedef struct {uint64_t tick;unsigned present,length;unsigned char bytes[UIREP_SECTION_MAX];} uirep_staged;
typedef struct {uint64_t tick;const char *what,*why;} uirep_refusal;
#define UIREP_REFUSALS 32u
static uirep_fns uirep_fn;
static const char *uirep_stage_name="off",*uirep_why="-";
static int uirep_installed,uirep_restored=1,uirep_pins_checked;
static volatile LONG uirep_replaying;
static int uirep_stopped;     /* past the window: every wrapper is stock again, wherever the patches still are */
static unsigned char *uirep_page;
static unsigned char uirep_patched[UIREP_HOOKS][5],uirep_site_patched[UIREP_SITES][5];
static uirep_event uirep_queue[UIREP_QUEUE];
static unsigned uirep_queue_head,uirep_queue_count;
static uirep_staged uirep_ring[PILOT_SLOTS][UIREP_RING];
static uirep_refusal uirep_refusals[UIREP_REFUSALS];
static unsigned uirep_refusal_head,uirep_refusal_tail;
static uint64_t uirep_frame_tick,uirep_frame_hash,uirep_results_hash,uirep_results_tick;
static unsigned uirep_frame_events;
static volatile LONG64 uirep_captured,uirep_refused,uirep_taken,uirep_staged_count,uirep_applied_native,uirep_applied_lua,uirep_stale,
    uirep_passed,uirep_scope_start,uirep_scope_stop,uirep_scope_refused,uirep_scope_nested,uirep_unavailable,uirep_guard_refused,uirep_target_passed,uirep_lua_errors,
    uirep_waits,uirep_calls_applied;

/* ---- pins and sites (build 9.00) ---- */
static void uirep_ui_move(void *self);
static void uirep_activity_start(void *a,int activity,unsigned char flag);
static void uirep_activity_stop(void *a,int activity,unsigned char flag);
static uint64_t uirep_orders_clear(uint64_t id,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g);
static uint64_t uirep_orders_clear2(uint64_t id,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g);
static uint64_t uirep_call(unsigned hook,uint64_t a[UIREP_ARGS]);
#define UIREP_CALL(n,hook) static uint64_t uirep_call_##n(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) { \
    uint64_t args[UIREP_ARGS]={a,b,c,d,e,f,g,h};return uirep_call(hook,args);}
UIREP_CALL(ap_start,UIREP_H_AP_START)
UIREP_CALL(ap_stop,UIREP_H_AP_STOP)
UIREP_CALL(quickdock,UIREP_H_QUICKDOCK)
UIREP_CALL(dockat,UIREP_H_DOCKAT)
UIREP_CALL(dockreason,UIREP_H_DOCKREASON)
UIREP_CALL(undock,UIREP_H_UNDOCK)
UIREP_CALL(wgroup_active,UIREP_H_WGROUP_ACTIVE)
UIREP_CALL(wgroup_set,UIREP_H_WGROUP_SET)
UIREP_CALL(wmode,UIREP_H_WMODE)
UIREP_CALL(fassist,UIREP_H_FASSIST)
UIREP_CALL(turret_mode,UIREP_H_TURRET_MODE)
UIREP_CALL(turret_missile,UIREP_H_TURRET_MISSILE)
UIREP_CALL(drones,UIREP_H_DRONES)
UIREP_CALL(drone_mode,UIREP_H_DRONE_MODE)
UIREP_CALL(launch_lasertower,UIREP_H_LAUNCH_LASERTOWER)
UIREP_CALL(launch_mine,UIREP_H_LAUNCH_MINE)
UIREP_CALL(launch_navbeacon,UIREP_H_LAUNCH_NAVBEACON)
UIREP_CALL(launch_probe,UIREP_H_LAUNCH_PROBE)
UIREP_CALL(launch_satellite,UIREP_H_LAUNCH_SATELLITE)
UIREP_CALL(weapon_armed,UIREP_H_WEAPON_ARMED)
UIREP_CALL(weapon_ammo,UIREP_H_WEAPON_AMMO)
UIREP_CALL(tgroup_mode,UIREP_H_TGROUP_MODE)
UIREP_CALL(tgroup_armed,UIREP_H_TGROUP_ARMED)
UIREP_CALL(turrets_mode,UIREP_H_TURRETS_MODE)
UIREP_CALL(turrets_armed,UIREP_H_TURRETS_ARMED)
UIREP_CALL(mturrets_mode,UIREP_H_MTURRETS_MODE)
UIREP_CALL(mturrets_armed,UIREP_H_MTURRETS_ARMED)
UIREP_CALL(nturrets_mode,UIREP_H_NTURRETS_MODE)
UIREP_CALL(nturrets_armed,UIREP_H_NTURRETS_ARMED)
UIREP_CALL(drone_set_mode,UIREP_H_DRONE_SET_MODE)
UIREP_CALL(drone_type_armed,UIREP_H_DRONE_TYPE_ARMED)
UIREP_CALL(drones_armed,UIREP_H_DRONES_ARMED)
UIREP_CALL(activate,UIREP_H_ACTIVATE)
UIREP_CALL(self_destruct,UIREP_H_SELF_DESTRUCT)
UIREP_CALL(drop_cargo,UIREP_H_DROP_CARGO)
static int uirep_local_phase(void);
static void uirep_guard_refuse(unsigned slot);
static uint64_t uirep_queue_call(unsigned hook,uint64_t a[UIREP_ARGS]);
/* The build and assets families (lockstep_ui_build.h, included at the end): their parts (op 0x50) on the wire, and their
 * open batch, which an event of another family closes first. */
#define UIBUILD_OP_PART 0x50u
static int uibuild_event_ok(unsigned slot,unsigned op,const unsigned char *data,unsigned n,const char **why);
static void uibuild_before_push(unsigned op);
/* The faction family (lockstep_ui_faction.h, included at the end): a part of one claimed call's record (op 0x5a, Lua-run),
 * its section rule, its prerequisite at the first pass and its part of every pass. */
#define UIFAC_OP_PART 0x5au
static int uifac_event_ok(unsigned slot,unsigned op,const unsigned char *data,unsigned n,const char **why);
static int uifac_ready(void);
static int uifac_pass(uint64_t tick);
static const char *uifac_why;
static void uicons_cid_ui(int enter);   /* lockstep_ui_construct.h: the construction ID counter at this client's UI move */
static void uicons_cid_probe(int on);
/* The queue editor's FFI mutators: with the queue family they are captured (uirep_queue_call); with the orders family
 * alone a call from the UI gets the failure value, the others pass, and so does a call with its check-only flag (the
 * menus ask that to enable a button; it changes nothing). */
#define UIREP_GUARD(n,slot,checkonly) static uint64_t uirep_guard_##n(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) { \
    if(uirep_families&UIREP_FAMILY_QUEUE) {uint64_t args[UIREP_ARGS]={a,b,c,d,e,f,g,h};return uirep_queue_call(slot,args);} \
    if((checkonly) || InterlockedCompareExchange(&uirep_replaying,0,0) || uirep_stopped || !uirep_local_phase()) return uirep_fn.forward[slot](a,b,c,d,e,f,g,h); \
    uirep_guard_refuse(slot);return 0;}
UIREP_GUARD(create3,UIREP_H_CREATE3,0)
UIREP_GUARD(enable,UIREP_H_ENABLE,0)
UIREP_GUARD(adjust,UIREP_H_ADJUST,f&0xff)              /* (controllable, idx, newidx, enabled, forcestates, checkonly) */
UIREP_GUARD(remove2,UIREP_H_REMOVE2,d&0xff)            /* (controllable, idx, playercancelled, checkonly, onlyimmediate) */
UIREP_GUARD(reset,UIREP_H_RESET,0)
UIREP_GUARD(planned_enable,UIREP_H_PLANNED_ENABLE,b&0xff) /* (controllable, checkonly) */
UIREP_GUARD(planned_remove,UIREP_H_PLANNED_REMOVE,0)
UIREP_GUARD(deploy,UIREP_H_DEPLOY,0)
UIREP_GUARD(loop,UIREP_H_LOOP,c&0xff)                  /* (controllable, idx, checkonly) */
UIREP_GUARD(loop_current,UIREP_H_LOOP_CURRENT,e&0xff)  /* (controllable, idx, enabled, forcestates, checkonly) */
#define UIREP_QUEUE_HOOK (UIREP_FAMILY_ORDERS|UIREP_FAMILY_QUEUE)
#define UIREP_NUM_ORDERS 0x1f7370u           /* GetNumOrders (the queue editor family) */
#define UIREP_ENTRY5 {0x48,0x89,0x5c,0x24,0x08}   /* mov [rsp+8],rbx */
#define UIREP_ENTRY7 {0x48,0x8b,0xc4,0x48,0x89,0x58,0x10}   /* mov rax,rsp; mov [rax+0x10],rbx */
static const struct {const char *name;unsigned rva,prefix,family;unsigned char bytes[9];void *wrapper;unsigned pin;const char *sha;} uirep_hooks[UIREP_HOOKS]={
    {"UIMove",0xb3bad0u,5,0,{0x48,0x89,0x5c,0x24,0x10},(void*)uirep_ui_move,206,"87a23ed23ef4be0fdf493a983342282860054eb3eb14f2388e134abc88762261"},
    {"StartActivity",0x9fd120u,7,UIREP_FAMILY_TRAVEL,UIREP_ENTRY7,(void*)uirep_activity_start,521,"3a686a762638a9d022d99bd44ab484ea5b225701adddf72b6cf40bf0d938df76"},
    {"StopActivity",0x9fd330u,7,UIREP_FAMILY_TRAVEL,UIREP_ENTRY7,(void*)uirep_activity_stop,501,"0b1d88c092f89e0479b3375abbd2a3c91fd96e4436b08decf71f7f6a441361b3"},
    {"RemoveAllOrders",0x1fe640u,5,UIREP_FAMILY_ORDERS,UIREP_ENTRY5,(void*)uirep_orders_clear,130,"5b18900b96d3f47edef7c8fd2fd6e547f5f94eeeb20c7992332d64b4a0ecfc2e"},
    {"RemoveAllOrders2",0x1fe6d0u,5,UIREP_FAMILY_ORDERS,UIREP_ENTRY5,(void*)uirep_orders_clear2,152,"27222818e2b7d5f90e2fc31f3e5b58f2f5014415de540580b4720f5e96bff171"},
    {"CreateOrder3",0x1f3b30u,5,UIREP_QUEUE_HOOK,UIREP_ENTRY5,(void*)uirep_guard_create3,459,"5a806d32b71aa8658b46c2611794995ce5d02d1246906b42189043ac9df52f75"},
    {"EnableOrder",0x1f3d40u,5,UIREP_QUEUE_HOOK,UIREP_ENTRY5,(void*)uirep_guard_enable,204,"b85f431536a8c264ad38455f0bb2fa5a95f5164764e8e30d4871ab59e8510a19"},
    {"AdjustOrder",0x1f2da0u,5,UIREP_QUEUE_HOOK,UIREP_ENTRY5,(void*)uirep_guard_adjust,195,"204f34e28d4a7abea9e0577b4f76ff7ff270a6144c56099b0ef64eaf6d94f5f6"},
    {"RemoveOrder2",0x1fe9d0u,5,UIREP_QUEUE_HOOK,UIREP_ENTRY5,(void*)uirep_guard_remove2,214,"a03780a483599329386676186590d88cf4c440e5154f81579a082447a46e40b6"},
    {"ResetOrder",0x1fef70u,5,UIREP_QUEUE_HOOK,UIREP_ENTRY5,(void*)uirep_guard_reset,279,"bb81c2c0928e55754bb709555a6bbe6ac21669b9ae895f12cec327e2fd5c272a"},
    {"EnablePlannedDefaultOrder",0x1f3e10u,5,UIREP_QUEUE_HOOK,UIREP_ENTRY5,(void*)uirep_guard_planned_enable,229,"1d5004fd5e115c97cc150dc2873d9c6904a8aa8c80e066c166ae7e927856e93b"},
    {"RemovePlannedDefaultOrder",0x1feed0u,5,UIREP_QUEUE_HOOK,UIREP_ENTRY5,(void*)uirep_guard_planned_remove,150,"313a8f3739710504ac59d2723414bf80fa94d7db010664b730b58443219aa31e"},
    {"CreateDeployToStationOrder",0x1f37b0u,5,UIREP_QUEUE_HOOK,UIREP_ENTRY5,(void*)uirep_guard_deploy,256,"2bdd9fdcb3a132a402b87fe3d4fbd66f8d5f6f33d891b511735a6016b951d0ea"},
    {"AutopilotStart",0x72bf20u,7,UIREP_FAMILY_AUTOPILOT,UIREP_ENTRY7,(void*)uirep_call_ap_start,0x7ff,"a8d10c65bdbeef5d4aac9e565c4562e33eeea441daa605da14a20acfe2ceff0b"},
    {"AutopilotStop",0x72bb10u,5,UIREP_FAMILY_AUTOPILOT,UIREP_ENTRY5,(void*)uirep_call_ap_stop,0x2aa,"6880a359c6abb1e6ee0559b302a767be9dff61a265f4fafd263de018fad587a7"},
    {"QuickDock",0x730ff0u,5,UIREP_FAMILY_DOCK,{0x48,0x89,0x5c,0x24,0x20},(void*)uirep_call_quickdock,0x570,"a46ad31c65cc70596298af1066c9b6c0aeafe1efa1dc13125eae4c6439d20f85"},
    {"RequestDockAt",0x1ef750u,5,UIREP_FAMILY_DOCK,UIREP_ENTRY5,(void*)uirep_call_dockat,0xba,"72d522f3f44a40eaf4a36fd6af8a33b432f6e4a990fe132123f19b862e9cc98c"},
    {"RequestDockAtReason",0x1ef810u,5,UIREP_FAMILY_DOCK,UIREP_ENTRY5,(void*)uirep_call_dockreason,0x14a,"2187f9f074819ec9deebdfe877e0d070b132a7602c4cda00d3184d5ee5cd9205"},
    {"Undock",0x72fe40u,5,UIREP_FAMILY_DOCK,{0x48,0x89,0x5c,0x24,0x10},(void*)uirep_call_undock,0x66d,"78799231170be696549e7b046455c4bd617894d7ea26171de23fb22edf92f6b5"},
    {"SetDefensibleActiveWeaponGroup",0x224430u,5,UIREP_FAMILY_WEAPONS,UIREP_ENTRY5,(void*)uirep_call_wgroup_active,0x8d,"58e10c804156e1cfb1d49ef497c7ec6f3637a291fec33f31781b94d3813032da"},
    {"SetWeaponGroup",0x224ba0u,5,UIREP_FAMILY_WEAPONS,UIREP_ENTRY5,(void*)uirep_call_wgroup_set,0xc8,"cfc9c8abc9ffaf9f378e7d23a0239bc64260afa0f8f2c484161dd35b7c72aae6"},
    {"SetWeaponMode",0x1ba7b0u,5,UIREP_FAMILY_WEAPONS,UIREP_ENTRY5,(void*)uirep_call_wmode,0xd9,"e1decc7cc69776b44f33e8f234c78bfab909d42181f23f26bc10c987865482b2"},
    {"SetOrderLoop",0x200640u,5,UIREP_FAMILY_QUEUE,UIREP_ENTRY5,(void*)uirep_guard_loop,0x94,"ad863e7eba6a6ad800b06c78353561a5ac742c7c5f6ee5b8b13b683c33d2f241"},
    {"SetCurrentLoopOrder",0x1ff850u,5,UIREP_FAMILY_QUEUE,UIREP_ENTRY5,(void*)uirep_guard_loop_current,0xb7,"a1bf2c2658233879bb4cffe0403b0de1fe38b145cc4b908db008498228bb57b0"},
    /* The flight family: the player's flight assist toggle below ToggleFlightAssist (its export starts RIP-relative), and the
     * HUD's own-ship buttons at their exports (the UI guard's prefixes and pins: covered by this family there). */
    {"FlightAssistToggle",0x710d50u,6,UIREP_FAMILY_FLIGHT,{0x40,0x53,0x48,0x83,0xec,0x20},(void*)uirep_call_fassist,0xb0,"b10eb334b7e2a05d9526df15f161003812990101f23fd7b9b27dc9fd1394885f"},
    {"CycleTurretMode",0xb01ff0u,7,UIREP_FAMILY_FLIGHT,{0x40,0x53,0x55,0x48,0x83,0xec,0x28},(void*)uirep_call_turret_mode,0xc4,"352dbd161746b89f47b1dd0ae830ecfdc230b785fb6b0ebafcfa71f51b180d46"},
    {"CycleTurretMissile",0xb01ed0u,5,UIREP_FAMILY_FLIGHT,{0x48,0x89,0x5c,0x24,0x18},(void*)uirep_call_turret_missile,0x113,"d4ad4e38bd44aa947dce004b02c8a047fb42f130f77e8d910aa036550d70711d"},
    {"ToggleDrones",0xb0b390u,6,UIREP_FAMILY_FLIGHT,{0x40,0x53,0x48,0x83,0xec,0x20},(void*)uirep_call_drones,0xaa,"9e369e342f929eb58a3debf44020b4c8fddf4d145419551a55a2848c7daa72b4"},
    {"CycleNextDroneMode",0xb01e70u,6,UIREP_FAMILY_FLIGHT,{0x40,0x53,0x48,0x83,0xec,0x20},(void*)uirep_call_drone_mode,0x54,"8f31760ccdabee6ec964695a4ddbcc372d6a376faa94c3dbd6d47623dcab4e69"},
    {"LaunchLaserTower",0x222db0u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_launch_lasertower,0x200,"5537e42fee6292be96ec013b94bf9c5476caac3772e14688190350ca9c9c007f"},
    {"LaunchMine",0x223030u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_launch_mine,0x200,"5809aedde75280a91535ff5669bee519d0eb25b0cd13d4989a64d9c27296b734"},
    {"LaunchNavBeacon",0x2232a0u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_launch_navbeacon,0x200,"ba9edd4e7c1548e3b6e0dc8d37e493421e183ae122e04a27d5e75df5ebc0426f"},
    {"LaunchResourceProbe",0x223520u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_launch_probe,0x200,"acb1af23d2328fa51b15175d02d4d388f7494e81717df1fbb5d39656e8354de3"},
    {"LaunchSatellite",0x2237a0u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_launch_satellite,0x200,"c12ab8196115e5e44b4d9c81696e77388581f33678725e3d2c6d7cb31afb1200"},
    {"SetWeaponArmed",0x1ba6d0u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_weapon_armed,0xd4,"49bd983a3a2d34c1ca43801b5009d70d2e9c77165f863747b707dcbbd95e9884"},
    {"SetAmmoOfWeapon",0x1b70c0u,5,UIREP_FAMILY_FLIGHT,{0x40,0x56,0x57,0x41,0x56},(void*)uirep_call_weapon_ammo,0x7d,"ac15bb767a356cbfa325467b0a62ce32e7470406ac89ea880e9d9eecfb2ded7c"},
    {"SetTurretGroupMode2",0x224920u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_tgroup_mode,0x200,"8268e8d3b3f68a3a8f2b11a0c68153d9059d8fa9a7e3cb7a3565632053cd50b9"},
    {"SetTurretGroupArmed",0x2246b0u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_tgroup_armed,0x200,"e663df64aaa77d8b0883c9b190dd95b4f3fd2679f11e775035612575a80ebbc4"},
    {"SetAllTurretModes",0x224220u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_turrets_mode,0x172,"80b9582595304ab2bb5976ed282e631f607fdfbb3bddf495af49be64a590048b"},
    {"SetAllTurretsArmed",0x2243a0u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_turrets_armed,0x89,"633ebb8350e6be190fd1f507ab8382297ccb7a7ef57111658670fb96741a9538"},
    {"SetAllMissileTurretModes",0x223b60u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_mturrets_mode,0xe0,"4e8b1c54a1c02de409ae162f961da604ecdfecc28ad575c3acbb24a33906189a"},
    {"SetAllMissileTurretsArmed",0x223c40u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_mturrets_armed,0x90,"0aa0fd8d110a4ab89a83df134d000d966d374ba379fe08c7794a68d686ad31c0"},
    {"SetAllNonMissileTurretModes",0x223cd0u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_nturrets_mode,0x1b6,"2899ac5ec902308057c8d6cd87a931dbe3b67d718e0e8613749b79c7f0484529"},
    {"SetAllNonMissileTurretsArmed",0x223e90u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_nturrets_armed,0x16e,"c1ebcf9dcc63156ee7a8bb252c2349aa4f6aceaae1d4f50fb2363c53dd894cc9"},
    {"SetDroneMode",0x2244c0u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_drone_set_mode,0x119,"009aca5566ce52b816442f8d7cd3c1378a0b1c0113d5d91a639e4fca2ba9a0fd"},
    {"SetDroneTypeArmed",0x2245e0u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_drone_type_armed,0xce,"fc089c82ba04e5658471b105e7c5d8af34959d6944c2b22e91ab4b1e1f878771"},
    {"SetAllDronesArmed",0x223aa0u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_drones_armed,0xbd,"bee6f6863e2136534fbd8fbfa8e94e2439aa03ddb5ef4aee8ffa90a2176cf248"},
    {"ActivateObject",0x175930u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_activate,0xc4,"03b9261b41e88429f25fcaccf557322733c3f91f0be54cf880ab02f4aee06dd7"},
    {"SelfDestructComponent",0x1b6f70u,5,UIREP_FAMILY_FLIGHT,{0x48,0x89,0x5c,0x24,0x10},(void*)uirep_call_self_destruct,0x97,"b0efb2eca9222111ea7f02503ea2e297b1f97e9f0ce9d41819882de0c8c69b29"},
    {"DropCargo",0x1df990u,5,UIREP_FAMILY_FLIGHT,UIREP_ENTRY5,(void*)uirep_call_drop_cargo,0x200,"7815910f72151e103062f91fddbf11393bde25af84f66b10911ec6d0e63e43c7"},
    /* the action containers' perform (lockstep_ui_compslot.h): installed while that module is armed (uirep_hook_used), they
     * pass outside its calls */
    {"SingleActionPerform",0x3508e0u,5,0,UIREP_ENTRY5,(void*)uics_perform_single,0x5d,"3a48fd72ded5585551dcaa1396bc9c0400b3e5083bb99ce7b370a70b75c104fc"},
    {"MultiActionPerform",0x350d60u,5,0,UIREP_ENTRY5,(void*)uics_perform_multi,0x63,"1cec820bfc25fb70dd61961933c29c1bd980e08778606aae86042857f04fb741"},
};
/* The generic calls: argument spec (P the player, C a component pointer, I a UniverseID, b a byte, w a word, V a
 * pointer to 16 bytes, s a string, k the check-only flag: set, the call is a query and passes; - passed through as 0),
 * and what a captured call returns (the check-only variant's answer, or 1). */
enum {UIREP_RET_ONE=0,UIREP_RET_PREDICT=1};
typedef struct {unsigned hook;const char *spec;unsigned char returns;} uirep_call_spec;
static const uirep_call_spec uirep_call_specs[]={
    {UIREP_H_AP_START,"PCVbbbk",UIREP_RET_ONE},       /* player, target, offset, r9, 5th, flag (+0xc3 of the guidance), check only */
    {UIREP_H_AP_STOP,"Pbbbk",UIREP_RET_PREDICT},
    {UIREP_H_QUICKDOCK,"P",UIREP_RET_ONE},
    {UIREP_H_DOCKAT,"Ik",UIREP_RET_PREDICT},          /* container, check only */
    {UIREP_H_DOCKREASON,"Ik",UIREP_RET_PREDICT},
    {UIREP_H_UNDOCK,"Pk",UIREP_RET_PREDICT},          /* player, check only: the status UndockPlayerShip turns into its text */
    {UIREP_H_WGROUP_ACTIVE,"Ibw",UIREP_RET_ONE},      /* defensible, primary, group (1-based) */
    {UIREP_H_WGROUP_SET,"IIbwb",UIREP_RET_ONE},       /* defensible, weapon, primary, group, active */
    {UIREP_H_WMODE,"Is",UIREP_RET_ONE},               /* weapon, mode */
    {UIREP_H_FASSIST,"P",UIREP_RET_ONE},              /* the player: its assist, its controller's (void) */
    {UIREP_H_TURRET_MODE,"w",UIREP_RET_ONE},          /* turret group number of the singleton's ship, 1-based (void) */
    {UIREP_H_TURRET_MISSILE,"w",UIREP_RET_ONE},
    {UIREP_H_DRONES,"w",UIREP_RET_ONE},               /* drone type 1..3 (void) */
    {UIREP_H_DRONE_MODE,"w",UIREP_RET_ONE},
    {UIREP_H_LAUNCH_LASERTOWER,"Is",UIREP_RET_ONE},  /* the launching ship, the deployable's macro name (void) */
    {UIREP_H_LAUNCH_MINE,"Is",UIREP_RET_ONE},
    {UIREP_H_LAUNCH_NAVBEACON,"Is",UIREP_RET_ONE},
    {UIREP_H_LAUNCH_PROBE,"Is",UIREP_RET_ONE},
    {UIREP_H_LAUNCH_SATELLITE,"Is",UIREP_RET_ONE},
    {UIREP_H_WEAPON_ARMED,"Ib",UIREP_RET_ONE},
    {UIREP_H_WEAPON_AMMO,"Is",UIREP_RET_ONE},
    {UIREP_H_TGROUP_MODE,"Iisss",UIREP_RET_ONE},
    {UIREP_H_TGROUP_ARMED,"Iissb",UIREP_RET_ONE},
    {UIREP_H_TURRETS_MODE,"Is",UIREP_RET_ONE},
    {UIREP_H_TURRETS_ARMED,"Ib",UIREP_RET_ONE},
    {UIREP_H_MTURRETS_MODE,"Is",UIREP_RET_ONE},
    {UIREP_H_MTURRETS_ARMED,"Ib",UIREP_RET_ONE},
    {UIREP_H_NTURRETS_MODE,"Is",UIREP_RET_ONE},
    {UIREP_H_NTURRETS_ARMED,"Ib",UIREP_RET_ONE},
    {UIREP_H_DRONE_SET_MODE,"Iss",UIREP_RET_ONE},
    {UIREP_H_DRONE_TYPE_ARMED,"Isb",UIREP_RET_ONE},
    {UIREP_H_DRONES_ARMED,"Ib",UIREP_RET_ONE},
    {UIREP_H_ACTIVATE,"Ib",UIREP_RET_ONE},
    {UIREP_H_SELF_DESTRUCT,"I",UIREP_RET_ONE},
    {UIREP_H_DROP_CARGO,"Isw",UIREP_RET_ONE}};
#define UIREP_CALL_SPECS (sizeof(uirep_call_specs)/sizeof(*uirep_call_specs))
/* The queue editor (family queue), Lua-run events in call order, applied in the probe's frame: q is an order index
 * (as the capturing menu saw the queue), the create's answer is predicted (UIREP_RET_ORDER), void returns nothing. */
enum {UIREP_RET_ORDER=2,UIREP_RET_VOID=3};
static const uirep_call_spec uirep_queue_specs[]={
    {UIREP_H_CREATE3,"Isbbb",UIREP_RET_ORDER},        /* controllable, order definition, default, override, temporary */
    {UIREP_H_DEPLOY,"I",UIREP_RET_ORDER},
    {UIREP_H_ENABLE,"Iq",UIREP_RET_ONE},
    {UIREP_H_ADJUST,"Iqqbbk",UIREP_RET_PREDICT},      /* idx, newidx, enabled, forcestates, check only */
    {UIREP_H_REMOVE2,"Iqbkb",UIREP_RET_PREDICT},      /* idx, playercancelled, check only, onlyimmediate */
    {UIREP_H_RESET,"Iqb",UIREP_RET_VOID},             /* idx, usedefault */
    {UIREP_H_PLANNED_ENABLE,"Ik",UIREP_RET_PREDICT},
    {UIREP_H_PLANNED_REMOVE,"I",UIREP_RET_VOID},
    {UIREP_H_LOOP,"Iqk",UIREP_RET_PREDICT},
    {UIREP_H_LOOP_CURRENT,"Iqbbk",UIREP_RET_VOID}};
static const uirep_call_spec *uirep_queue_spec_of(unsigned hook) {
    for(unsigned i=0;i<sizeof(uirep_queue_specs)/sizeof(*uirep_queue_specs);i++) if(uirep_queue_specs[i].hook==hook) return &uirep_queue_specs[i];
    return NULL;
}
/* hook, seen (2), the arguments, the predicted index (4) of a create; 0 = not a valid queue event. */
static unsigned uirep_queue_length(const unsigned char *data,unsigned n) {
    const uirep_call_spec *c=n>=3?uirep_queue_spec_of(data[0]):NULL;unsigned at=3;
    if(!c) return 0;
    for(unsigned i=0;c->spec[i];i++) {
        unsigned need=c->spec[i]=='I'?UIREP_REF:c->spec[i]=='q'?4:c->spec[i]=='b'?1:0;
        if(c->spec[i]=='s') {if(at>=n || data[at]>UIREP_STRING_MAX) return 0;need=1u+data[at];}
        if(need>n-at) return 0;
        at+=need;
    }
    if(c->returns==UIREP_RET_ORDER) {if(n-at<4) return 0;at+=4;}
    return at;
}
static const uirep_call_spec *uirep_spec_of(unsigned hook) {
    for(unsigned i=0;i<UIREP_CALL_SPECS;i++) if(uirep_call_specs[i].hook==hook) return &uirep_call_specs[i];
    return NULL;
}
static int uirep_spec_checkonly(const char *spec) {const char *k=strchr(spec,'k');return k?(int)(k-spec):-1;}
/* The soft target blocks: five bytes inside one aligned 16-byte block (one CAS), the continuation behind each
 * block. The clear block starts across a 16-byte boundary, so the patch sits on the lookup's result in front of
 * it (mov rbx,rax; test rax,rax; jne 0xaecfdc): the stride repeats those and goes on to the stock error path at
 * 0xaecfcb without a component. */
#define UIREP_CLEAR_ERROR 0xaecfcbu
static const struct {unsigned rva,resume;unsigned char bytes[5];} uirep_sites[UIREP_SITES]={
    {0xaecfc3u,0xaed0b3u,{0x48,0x8b,0xd8,0x48,0x85}},  /* mov rbx,rax; test rax,rax */
    {0xaed115u,0xaed219u,{0x41,0x0f,0xb6,0x6d,0x31}},  /* movzx ebp,byte [r13+0x31]: rbx = the target, r13 = the element */
};
static const struct {unsigned rva,size;const char *sha;} uirep_pin_table[]={
    {0x710ed0u,1268,"9ec1a432995414451cee06a8e063412551a756680fb20e3e6a32097dfa90c4c9"},  /* the activity setter */
    {0x55aeb0u,142,"b95c394b389e23c19cd668e9d15d5210cafde0810e5ebe1d3d957a73757b2a51"},   /* the player's ship */
    {0x7e1c80u,301,"f60d70b2f2888c1ae13328d2ca150332b3f47df28b5dddbcc3b85280ceae8a5c"},   /* Ship::vfunc1008, travel available */
    {0x6d6d80u,40,"4e40c27773f0457d146b6307457bffe330fa2fc92c933d4c53d6199954d1c0aa"},    /* the ship's travel drive */
    {0x314e80u,34,"69af1b534cdebe97eee6c3732cfc8fbb7ac33bf31e991ec53f92933ad43121cc"},    /* a stored ID's component (+0x6a30) */
    {0x3e73f0u,355,"bac88a495e92607e803b53d50bb580380adcfeca989e59cc465d4ccbd0dd586c"},   /* target set */
    {0x3e7560u,288,"a9edb6b8cfea1d92476c39ac4f09760431e347dba5a66ddb14242241be5abeee"},   /* target clear */
    {0xaecb60u,1915,"eec1c6e468a9a2dfa26dab7fd4915b96aea64f5d4a78d7c8d4afc1e48d32fa57"},  /* the UI's writer, both sites */
    {0xaf1db0u,0x1ccc,"bd760df9eaf2e6bb4203a012be153bdfef709853df591149f4f86cefa7e42eae"},  /* the weapon group keys' handler */
    {UIREP_NUM_ORDERS,0xae,"3986c71ab2b808b44a27f37c0e7ff3338b09f6a8057187ba5ea8c4a9eb133bd6"},  /* GetNumOrders: the queue's length (+0x4b0) */
    {0x1135bc0u,0x300,"d0148366c93c695bedf997aad74fe065379ced6b1b147d5d73d14d1c442dce72"},   /* MD set_player_target: its call at 0x1135ea0 */
    {0x1135f20u,0x3a,"7ed58062e2316ef6581bf44921b466a3aac4b45a8924f506d4fbc16303832af2"},    /* MD clear_player_target: its call at 0x1135f36 */
};
/* MD's two soft target actions (MD::SetPlayerTargetAction / ClearPlayerTargetAction, vtable slot 3): the return addresses of their
 * calls toward 0xaecb60 name the simulation's own route (uirep_target_route). */
#define UIREP_MD_SET_FN 0x1135bc0u
#define UIREP_MD_SET_VTABLE 0x2c74740u
#define UIREP_MD_SET_RETURN 0x1135ea6u    /* behind call [rax+0x240] at 0x1135ea0: UI::CUIController vfunc 72 0xb3f7c0 -> 0xaeb150 */
#define UIREP_MD_CLEAR_FN 0x1135f20u
#define UIREP_MD_CLEAR_VTABLE 0x2c747b8u
#define UIREP_MD_CLEAR_RETURN 0x1135f3bu  /* behind call 0xae8080 at 0x1135f36: -> 0xaebef0 -> 0xae8460 */
#define UIREP_PIN_COUNT (sizeof(uirep_pin_table)/sizeof(*uirep_pin_table))
static int uirep_pins(void) {
    if(!uirep_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<UIREP_HOOKS;i++)
            if(!(ok=hash_ok(image+uirep_hooks[i].rva,uirep_hooks[i].pin,uirep_hooks[i].sha) && !memcmp(image+uirep_hooks[i].rva,uirep_hooks[i].bytes,uirep_hooks[i].prefix))) pin_failed_rva=uirep_hooks[i].rva;
        for(unsigned i=0;ok && i<UIREP_PIN_COUNT;i++)
            if(!(ok=hash_ok(image+uirep_pin_table[i].rva,uirep_pin_table[i].size,uirep_pin_table[i].sha))) pin_failed_rva=uirep_pin_table[i].rva;
        for(unsigned i=0;ok && i<UIREP_SITES;i++)
            if(!(ok=!memcmp(image+uirep_sites[i].rva,uirep_sites[i].bytes,5))) pin_failed_rva=uirep_sites[i].rva;
        if(ok && ((void**)(image+UIREP_MD_SET_VTABLE))[3]!=(void*)(image+UIREP_MD_SET_FN)) {ok=0;pin_failed_rva=UIREP_MD_SET_VTABLE;}
        if(ok && ((void**)(image+UIREP_MD_CLEAR_VTABLE))[3]!=(void*)(image+UIREP_MD_CLEAR_FN)) {ok=0;pin_failed_rva=UIREP_MD_CLEAR_VTABLE;}
        uirep_pins_checked=ok?1:-1;
    }
    return uirep_pins_checked==1;
}
static unsigned char uirep_can_travel_vtable(void *ship) {return ((unsigned char(*)(void*))(*(void***)ship)[UIREP_SHIP_CAN_TRAVEL])(ship);}
static void uirep_use_image(void) {
    uirep_fn.set_activity=(void(*)(void*,int))(image+0x710ed0u);uirep_fn.can_travel=uirep_can_travel_vtable;
    uirep_fn.drive=(void**(*)(void*,void**))(image+0x6d6d80u);
    uirep_fn.target_set=(void(*)(void*,const void*,unsigned char))(image+0x3e73f0u);uirep_fn.target_clear=(void(*)(void*))(image+0x3e7560u);
    uirep_fn.find=(void*(*)(void*,uint64_t,unsigned))(image+0xced70u);
    uirep_fn.enter=pslot_enter;uirep_fn.leave=pslot_leave;uirep_fn.portable=world_identity_portable;
}

static int uirep_fail(const char *why) {uirep_stage_name="failed";if(!strcmp(uirep_why,"-")) uirep_why=why;uirep_failed=1;return 0;}
/* ---- capture ---- */
static int uirep_local_phase(void) {
    return GetCurrentThreadId()==ui_thread && (uirep_ui_depth>0 || uirep_dispatch_depth>0 || pres_open || render_rng_in_slot);
}
static int uirep_capture_now(void) {return !InterlockedCompareExchange(&uirep_replaying,0,0) && !uirep_stopped && uirep_local_phase();}
static void uirep_note_refusal(const char *what,const char *why) {
    InterlockedIncrement64(&uirep_refused);
    if(uirep_refusal_head-uirep_refusal_tail>=UIREP_REFUSALS) return;
    uirep_refusal *r=&uirep_refusals[uirep_refusal_head%UIREP_REFUSALS];
    r->tick=stats.issued;r->what=what;r->why=why;++uirep_refusal_head;
}
static void uirep_guard_refuse(unsigned slot) {
    InterlockedIncrement64(&uirep_guard_refused);uirep_note_refusal(uirep_hooks[slot].name,"not_replayed_yet");
}
static const char *uirep_op_name(unsigned op) {
    switch(op) {
    case UIREP_OP_ACTIVITY_START:return "activity_start";
    case UIREP_OP_ACTIVITY_STOP:return "activity_stop";
    case UIREP_OP_TARGET_SET:return "target_set";
    case UIREP_OP_TARGET_CLEAR:return "target_clear";
    case UIREP_OP_CALL:return "call";
    case UIREP_OP_ORDERS_CLEAR:return "orders_clear";
    case UIREP_OP_ORDERS_CLEAR2:return "orders_clear2";
    case UIREP_OP_LUA_CALL:return "lua_call";
    case UIREP_OP_QUEUE:return "queue";
    case UIREP_OP_QUEUE_PARAM:return "queue_param";
    case UIREP_OP_QUEUE_LIST:return "queue_list_param";
    case UIREP_OP_LUA_GLOBAL:return "lua_global";
    case UIREP_OP_MISSION:return "mission";
    case UIREP_OP_CONVERSATION:return "conversation";
    case UICS_OP:return "compslot";
    case UITRADE_OP_LUA:return "trade_lua";
    case UITRADE_OP_FFI:return "trade_ffi";
    case UIINV_OP_CLICK:return "inventory_click";
    case UIFAC_OP_PART:return "faction_part";
    default:return "?";
    }
}
/* One event of this client's slot, in call order. 0 = refused, nothing ran. */
static int uirep_push(unsigned op,const unsigned char *data,unsigned length) {
    const char *why=NULL;
    uibuild_before_push(op);
    if(!uirep_armed || !pstk_capturing) why="not_capturing";
    else if(length>UIREP_EVENT_MAX) why="too_long";
    else if(!pstk_capture_start || stats.issued<pstk_capture_start || stats.issued>pilot_stop) why="not_live";
    else if(uirep_queue_count>=UIREP_QUEUE) why="queue_full";
    if(why) {uirep_note_refusal(uirep_op_name(op),why);return 0;}
    uirep_event *e=&uirep_queue[(uirep_queue_head+uirep_queue_count)%UIREP_QUEUE];
    e->op=(unsigned char)op;e->length=(unsigned char)length;memcpy(e->data,data,length);++uirep_queue_count;
    InterlockedIncrement64(&uirep_captured);return 1;
}
static uint32_t uirep_check_of(uint64_t runtime) {
    uint64_t portable=uirep_fn.portable?uirep_fn.portable(runtime):0;
    return portable?(uint32_t)lockstep_mix(portable)|1u:0u;
}
static void uirep_put_ref(unsigned char *out,uint64_t runtime) {
    uint32_t check=uirep_check_of(runtime);
    for(unsigned i=0;i<8;i++) out[i]=(unsigned char)(runtime>>(8*i));
    for(unsigned i=0;i<4;i++) out[8+i]=(unsigned char)(check>>(8*i));
}
static uint64_t uirep_get_ref(const unsigned char *in,uint32_t *check) {
    uint64_t runtime=0;uint32_t c=0;
    for(unsigned i=0;i<8;i++) runtime|=(uint64_t)in[i]<<(8*i);
    for(unsigned i=0;i<4;i++) c|=(uint32_t)in[8+i]<<(8*i);
    *check=c;return runtime;
}

/* ---- travel: the two functions every activity start and stop ends in ---- */
static int uirep_activity_valid(unsigned slot,int activity,int stop) {
    if(activity==UIREP_ACTIVITY_TRAVEL) return 1;
    if(slot>=2) return activity==2 && scl_armed;           /* slots 2+: the adapter below has travel, and the long-range scan with lockstep_scan_long.h */
    return activity>=(stop?0:1) && activity<=UIREP_ACTIVITY_MAX;
}
static void uirep_activity_capture(unsigned op,int activity,unsigned char flag) {
    if(!uirep_activity_valid(stats.slot,activity,op==UIREP_OP_ACTIVITY_STOP)) {uirep_note_refusal(uirep_op_name(op),"activity");return;}
    unsigned char data[2]={(unsigned char)activity,flag!=0};
    uirep_push(op,data,2);
}
static int uirep_drive_active(void *ship) {
    void *drive=NULL;unsigned char active=0;
    uirep_fn.drive(ship,&drive);
    return drive && read_local((unsigned char*)drive+UIREP_DRIVE_ACTIVE,&active,1) && active;
}
/* The setter tearing a slot's travel down: 0x6d9190 posts TravelModeChargeAborted / Stopped on the singleton at once,
 * U::Player's slots 262 and 264 (0x706000) answer with 0x9fd330 while the activity still reads travel (0x710ed0 writes it
 * after the teardown). In stock that is an event for MD, harmless by then; here it comes back into the adapter, and into
 * the setter (live u1..u3: stack overflow at the stop). What the setter causes is the setter's. */
static int uirep_setter_depth;
static void uirep_set_activity(pslot_slot *s,int activity) {
    ++uirep_setter_depth;
    __try {uirep_fn.set_activity(s->player,activity);}
    __finally {--uirep_setter_depth;}
}
/* md/modes.xml for travel on slot k's player: its check, then its set_player_activity. */
static void uirep_adapter_start(pslot_slot *s,int activity) {
    if(uirep_setter_depth) {InterlockedIncrement64(&uirep_scope_nested);return;}
    int current=0;memcpy(&current,s->player+UIREP_PLAYER_ACTIVITY,4);
    if(activity==current) return;
    if(activity==2 && scl_armed) {   /* md/modes.xml's long-range scan: X4's own "can scan" test, then the activity */
        if(!scl_may_start(s->player)) {InterlockedIncrement64(&uirep_unavailable);return;}
        if(current) uirep_set_activity(s,0);
        uirep_set_activity(s,activity);
        InterlockedIncrement64(&uirep_scope_start);return;
    }
    if(activity!=UIREP_ACTIVITY_TRAVEL) {InterlockedIncrement64(&uirep_scope_refused);return;}
    if(!uirep_fn.can_travel(s->ship) && !uirep_drive_active(s->ship)) {InterlockedIncrement64(&uirep_unavailable);return;}
    uint64_t id=0;
    if(read_local(s->ship+UIREP_ID,&id,8)) memcpy(s->player+UIREP_PLAYER_LAST_SHIP,&id,8);
    uirep_set_activity(s,activity);
    InterlockedIncrement64(&uirep_scope_start);
}
static void uirep_adapter_stop(pslot_slot *s,int activity) {
    if(uirep_setter_depth) {InterlockedIncrement64(&uirep_scope_nested);return;}
    int current=0;memcpy(&current,s->player+UIREP_PLAYER_ACTIVITY,4);
    if(!current || activity!=current) return;
    uirep_set_activity(s,0);
    InterlockedIncrement64(&uirep_scope_stop);
}
static pslot_slot *uirep_scope_of_thread(void) {
    return pstk_scope_slot>=2 && GetCurrentThreadId()==ui_thread?&pslots[pstk_scope_slot]:NULL;
}
static void uirep_activity_start(void *a,int activity,unsigned char flag) {
    pslot_slot *s=uirep_scope_of_thread();
    if(s) {uirep_adapter_start(s,activity);return;}
    if(uirep_capture_now()) {uirep_activity_capture(UIREP_OP_ACTIVITY_START,activity,flag);return;}
    InterlockedIncrement64(&uirep_passed);uirep_fn.start(a,activity,flag);
}
static void uirep_activity_stop(void *a,int activity,unsigned char flag) {
    pslot_slot *s=uirep_scope_of_thread();
    if(s) {uirep_adapter_stop(s,activity);return;}
    if(uirep_capture_now()) {uirep_activity_capture(UIREP_OP_ACTIVITY_STOP,activity,flag);return;}
    InterlockedIncrement64(&uirep_passed);uirep_fn.stop(a,activity,flag);
}

/* ---- soft target: the blocks of 0xaecb60 ----
 * Who writes outside the local phases (live census missile1, 1 November; reports/LOCKSTEP-SUIT-VIEW-20261101.txt): slot 2's ship
 * died at 1733 while it was slot 1's soft target. On client 1 the UI's target element of that ship went with it (its destructor
 * 0xaeb4e0 -> 0xaeb970 -> 0xaecb60, from the simulation's kill, in no local phase): the target manager cleared it and chose the
 * next one, both blocks ran stock at once (target_passed=2), and slot 1's +0x6dd0 changed on client 1 alone. Client 2's UI targets
 * for slot 2, its element of that ship was nobody's target, nothing ran. MD's Player_Changed_Target then took one more shared
 * runtime ID on client 1 and the world parted. The target manager is presentation: what it writes follows this client's view.
 * The simulation's own writes are MD's set_player_target and clear_player_target (every client, the same tick), named by their
 * call's return address among the image return addresses above 0xaecb60. So, outside a local phase:
 *   MD on the stack                     stock at once (target_md=; target_passed counts every stock call);
 *   the replay applying, past the window stock at once, as before;
 *   anything else on the frame thread   the UI's own: this client's slot event like a click (target_ui=, target_route=);
 *   anything else elsewhere             refused, nothing runs anywhere (target_ui=../n).
 * A route the walk cannot unwind is "not MD": captured or refused, never applied on one client. */
#define UIREP_ROUTE_DEPTH 12u             /* image frames looked at above 0xaecb60 */
#define UIREP_ROUTE_SHOWN 6u              /* of the last UI route, in the status */
#define UIREP_ROUTE_OWN 8u                /* our frames between the walk and the stride */
enum {UIREP_ROUTE_STOCK,UIREP_ROUTE_CAPTURE,UIREP_ROUTE_REFUSE};
static volatile LONG64 uirep_target_ui,uirep_target_ui_refused,uirep_target_md,uirep_route_faults;
static uint32_t uirep_route_last[UIREP_ROUTE_SHOWN];
static unsigned uirep_route_last_count;
static volatile int uirep_route_reached;  /* the last walk got to the stride (tests) */
static unsigned (*uirep_route_override)(unsigned site,uint32_t *frames,unsigned capacity);  /* tests: the frames above 0xaecb60 */
/* The image return addresses above a context inside an image function, by the unwind tables (X4's own .pdata), at most capacity;
 * frames outside the image (a hook of ours) are unwound and not listed; stops at the first frame without a table. */
static unsigned uirep_route_unwind(CONTEXT *c,const unsigned char *base,uint32_t *frames,unsigned capacity) {
    unsigned n=0;
    for(unsigned step=0;n<capacity && step<2*UIREP_ROUTE_DEPTH;step++) {
        DWORD64 module=0;PRUNTIME_FUNCTION fn=RtlLookupFunctionEntry(c->Rip,&module,NULL);
        if(!fn) break;
        PVOID handler=NULL;DWORD64 frame=0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,module,c->Rip,fn,c,&handler,&frame,NULL);
        uintptr_t rip=(uintptr_t)c->Rip;
        if(!rip) break;
        if(rip>=(uintptr_t)base && rip-(uintptr_t)base<VPL_IMAGE_SPAN) frames[n++]=(uint32_t)(rip-(uintptr_t)base);
    }
    return n;
}
/* From the stub: our frames by their tables up to the stride (on the replay's page, no table), which runs in 0xaecb60's own frame
 * and moves no stack before its call, so the unwound context is 0xaecb60's at the block; it goes on from the block's resume
 * (inside 0xaecb60's range and not an epilogue, unlike the patched site's jump). A fault counts and ends the walk. */
static __attribute__((noinline)) unsigned uirep_route_walk(unsigned site,uint32_t *frames,unsigned capacity) {
    CONTEXT c;volatile unsigned n=0;
    uirep_route_reached=0;
    __try {
        uintptr_t page=(uintptr_t)uirep_page;unsigned own=0;int reached=0;
        RtlCaptureContext(&c);
        while(page && own<UIREP_ROUTE_OWN) {
            if((uintptr_t)c.Rip>=page && (uintptr_t)c.Rip<page+UIREP_PAGE) {reached=1;break;}
            DWORD64 module=0;PRUNTIME_FUNCTION fn=RtlLookupFunctionEntry(c.Rip,&module,NULL);
            if(!fn) break;
            PVOID handler=NULL;DWORD64 frame=0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER,module,c.Rip,fn,&c,&handler,&frame,NULL);
            ++own;
        }
        if(reached) {
            uirep_route_reached=1;
            c.Rip=(DWORD64)(uintptr_t)(pnat_base+uirep_sites[site].resume);
            n=uirep_route_unwind(&c,pnat_base,frames,capacity);
        }
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {
        InterlockedIncrement64(&uirep_route_faults);n=0;
    }
    return n;
}
/* What a block's call is: the capture of this client's slot, the stock function at once, or nothing. */
static int uirep_target_route(unsigned site) {
    if(uirep_capture_now()) return UIREP_ROUTE_CAPTURE;   /* a local phase */
    if(InterlockedCompareExchange(&uirep_replaying,0,0) || uirep_stopped) return UIREP_ROUTE_STOCK;
    uint32_t frames[UIREP_ROUTE_DEPTH];
    unsigned n=uirep_route_override?uirep_route_override(site,frames,UIREP_ROUTE_DEPTH):uirep_route_walk(site,frames,UIREP_ROUTE_DEPTH);
    if(n>UIREP_ROUTE_DEPTH) n=UIREP_ROUTE_DEPTH;
    for(unsigned i=0;i<n;i++)
        if(frames[i]==UIREP_MD_SET_RETURN || frames[i]==UIREP_MD_CLEAR_RETURN) {InterlockedIncrement64(&uirep_target_md);return UIREP_ROUTE_STOCK;}
    if(GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&uirep_target_ui_refused);return UIREP_ROUTE_REFUSE;}
    uirep_route_last_count=n<UIREP_ROUTE_SHOWN?n:UIREP_ROUTE_SHOWN;memcpy(uirep_route_last,frames,uirep_route_last_count*sizeof(*frames));
    InterlockedIncrement64(&uirep_target_ui);return UIREP_ROUTE_CAPTURE;
}
static uint64_t uirep_component_id(const unsigned char *component) {
    uint64_t id=0;if(component) read_local(component+UIREP_ID,&id,8);return id;
}
static void uirep_set_hint(uint64_t id) {*(volatile uint64_t*)(pnat_base+UIREP_LAST_TARGET)=id;}
static uint64_t uirep_get_hint(void) {return *(volatile uint64_t*)(pnat_base+UIREP_LAST_TARGET);}
static void uirep_target_set_stub(unsigned char *component,unsigned char *element) {
    unsigned char flag=0,offset[UIREP_OFFSET];
    memset(offset,0,sizeof(offset));
    read_local(element+UIREP_ELEMENT_FLAG,&flag,1);read_local(element+UIREP_ELEMENT_OFFSET,offset,UIREP_OFFSET);
    const int route=uirep_target_route(UIREP_S_TARGET_SET);
    if(route==UIREP_ROUTE_STOCK) {
        InterlockedIncrement64(&uirep_target_passed);
        uirep_fn.target_set(component+UIREP_ASSOCIATION,offset,flag);return;
    }
    if(route==UIREP_ROUTE_REFUSE) return;
    uint64_t id=uirep_component_id(component);unsigned char data[UIREP_REF+1+UIREP_OFFSET];
    uirep_put_ref(data,id);data[UIREP_REF]=flag;memcpy(data+UIREP_REF+1,offset,UIREP_OFFSET);
    if(uirep_push(UIREP_OP_TARGET_SET,data,sizeof(data))) uirep_set_hint(id);
}
static void uirep_target_clear_stub(unsigned char *component) {
    const int route=uirep_target_route(UIREP_S_TARGET_CLEAR);
    if(route==UIREP_ROUTE_STOCK) {
        InterlockedIncrement64(&uirep_target_passed);
        uirep_fn.target_clear(component+UIREP_ASSOCIATION);return;
    }
    if(route==UIREP_ROUTE_REFUSE) return;
    unsigned char data[UIREP_REF];uint64_t id=uirep_component_id(component);
    uirep_put_ref(data,id);
    if(uirep_push(UIREP_OP_TARGET_CLEAR,data,sizeof(data))) uirep_set_hint(0);
}

/* ---- orders: the FFI side (Lua runs them, in order with the Lua calls) ---- */
static uint64_t uirep_orders_clear(uint64_t id,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g) {
    if(!uirep_capture_now()) {InterlockedIncrement64(&uirep_passed);return uirep_fn.forward[UIREP_H_CLEAR](id,a,b,c,d,e,f,g);}
    unsigned char data[UIREP_REF];uirep_put_ref(data,id);
    return (uint64_t)uirep_push(UIREP_OP_ORDERS_CLEAR,data,sizeof(data));
}
static uint64_t uirep_orders_clear2(uint64_t id,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g) {
    if(!uirep_capture_now()) {InterlockedIncrement64(&uirep_passed);return uirep_fn.forward[UIREP_H_CLEAR2](id,a,b,c,d,e,f,g);}
    unsigned char data[UIREP_REF+1];uirep_put_ref(data,id);
    data[UIREP_REF]=(unsigned char)(((a&0xff)!=0)|(((b&0xff)!=0)<<1));
    return (uint64_t)uirep_push(UIREP_OP_ORDERS_CLEAR2,data,sizeof(data));
}
/* ---- generic calls ---- */
static unsigned char *uirep_component(uint64_t runtime);
static uint64_t uirep_forward(unsigned hook,const uint64_t a[UIREP_ARGS]) {
    return uirep_fn.forward[hook](a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]);
}
static uint64_t uirep_call(unsigned hook,uint64_t a[UIREP_ARGS]) {
    const uirep_call_spec *c=uirep_spec_of(hook);
    if(!c || !uirep_capture_now()) {InterlockedIncrement64(&uirep_passed);return uirep_forward(hook,a);}
    int k=uirep_spec_checkonly(c->spec);
    if(k>=0 && (a[k]&0xffu)) return uirep_forward(hook,a);   /* a query changes nothing */
    unsigned char data[UIREP_EVENT_MAX];unsigned n=0;const char *why=NULL;
    data[n++]=(unsigned char)hook;
    for(unsigned i=0;c->spec[i] && !why;i++) {
        switch(c->spec[i]) {
        case 'P': if((unsigned char*)(uintptr_t)a[i]!=pnat_player()) why="not_the_player";break;
        case 'C': {
            uint64_t id=a[i]?uirep_component_id((const unsigned char*)(uintptr_t)a[i]):0;
            if(!id || !uirep_check_of(id)) why="unmapped";else {uirep_put_ref(data+n,id);n+=UIREP_REF;}
            break;
        }
        case 'I': if(!a[i] || !uirep_component(a[i]) || !uirep_check_of(a[i])) why="unmapped";else {uirep_put_ref(data+n,a[i]);n+=UIREP_REF;} break;
        case 'i':   /* a component or 0 */
            if(!a[i]) {memset(data+n,0,UIREP_REF);n+=UIREP_REF;}
            else if(!uirep_component(a[i]) || !uirep_check_of(a[i])) why="unmapped";
            else {uirep_put_ref(data+n,a[i]);n+=UIREP_REF;}
            break;
        case 'b': data[n++]=(unsigned char)a[i];break;
        case 'w': for(unsigned b=0;b<4;b++) data[n++]=(unsigned char)(a[i]>>(8*b));break;
        case 'V': if(!a[i] || !read_local((const void*)(uintptr_t)a[i],data+n,16)) why="vector";else n+=16;break;
        case 's': {
            const char *s=(const char*)(uintptr_t)a[i];size_t len=s?strnlen(s,UIREP_STRING_MAX+1):0;
            if(!s || len>UIREP_STRING_MAX) why="string";else {data[n++]=(unsigned char)len;memcpy(data+n,s,len);n+=(unsigned)len;}
            break;
        }
        default:break;   /* k, - */
        }
    }
    if(why) {uirep_note_refusal(uirep_hooks[hook].name,why);return 0;}
    if(!uirep_push(UIREP_OP_CALL,data,n)) return 0;
    if(c->returns==UIREP_RET_PREDICT && k>=0) {a[k]=1;return uirep_forward(hook,a);}
    return 1;
}
/* The bytes a call's arguments take on the wire; 0 = not a valid call. */
static unsigned uirep_call_length(const uirep_call_spec *c,const unsigned char *data,unsigned n) {
    unsigned at=1;
    for(unsigned i=0;c->spec[i];i++) {
        unsigned need=c->spec[i]=='C' || c->spec[i]=='I' || c->spec[i]=='i'?UIREP_REF:c->spec[i]=='b'?1:c->spec[i]=='w'?4:c->spec[i]=='V'?16:0;
        if(c->spec[i]=='s') {if(at>=n || data[at]>UIREP_STRING_MAX) return 0;need=1u+data[at];}
        if(need>n-at) return 0;
        at+=need;
    }
    return at;
}
static void uirep_ui_move(void *self) {
    if(InterlockedIncrement(&uirep_ui_depth)==1) uicons_cid_ui(1);   /* the station editor's ID draws stay local (lockstep_ui_construct.h) */
    __try {uirep_fn.ui_move(self);}
    __finally {if(!InterlockedDecrement(&uirep_ui_depth)) uicons_cid_ui(0);}
}

/* ---- the section ---- */
static int uimis_event_ok(unsigned slot,unsigned op,const unsigned char *data,unsigned n,const char **why);   /* lockstep_ui_missions.h */
static int uirep_event_ok(unsigned slot,unsigned op,const unsigned char *data,unsigned n,const char **why) {
    switch(op) {
    case UIREP_OP_ACTIVITY_START:case UIREP_OP_ACTIVITY_STOP:
        if(!(uirep_families&UIREP_FAMILY_TRAVEL) || n!=2 || data[1]>1 || !uirep_activity_valid(slot,data[0],op==UIREP_OP_ACTIVITY_STOP)) {*why="ui_activity";return 0;}
        return 1;
    case UIREP_OP_TARGET_SET:
        if(!(uirep_families&UIREP_FAMILY_TARGET) || n!=UIREP_REF+1+UIREP_OFFSET || data[UIREP_REF]>1) {*why="ui_target";return 0;}
        return 1;
    case UIREP_OP_TARGET_CLEAR:
        if(!(uirep_families&UIREP_FAMILY_TARGET) || n!=UIREP_REF) {*why="ui_target";return 0;}
        return 1;
    case UIREP_OP_ORDERS_CLEAR:
        if(!(uirep_families&UIREP_FAMILY_ORDERS) || n!=UIREP_REF) {*why="ui_orders";return 0;}
        return 1;
    case UIREP_OP_ORDERS_CLEAR2:
        if(!(uirep_families&UIREP_FAMILY_ORDERS) || n!=UIREP_REF+1 || data[UIREP_REF]>3) {*why="ui_orders";return 0;}
        return 1;
    case UIREP_OP_LUA_CALL:
        if(!(uirep_families&UIREP_FAMILY_ORDERS) || !n) {*why="ui_orders";return 0;}
        return 1;
    case UIREP_OP_QUEUE:
        if(!(uirep_families&UIREP_FAMILY_QUEUE) || !n || uirep_queue_length(data,n)!=n) {*why="ui_queue";return 0;}
        return 1;
    case UIREP_OP_QUEUE_PARAM:case UIREP_OP_QUEUE_LIST:
        if(!(uirep_families&UIREP_FAMILY_QUEUE) || !n) {*why="ui_queue";return 0;}
        return 1;
    case UIREP_OP_LUA_GLOBAL:   /* slot 1 only: MD's player.entity is slot 1's player */
        if(!(uirep_families&UIREP_FAMILY_EVENTS) || (slot!=1 && !mpp_armed) || n<2 || !data[0] || data[0]>UIREP_LUA_GLOBALS) {*why="ui_events";return 0;}
        return 1;
    case UICS_OP:return uics_event_ok(slot,op,data,n,why);   /* lockstep_ui_compslot.h */
    case UITRADE_OP_LUA:case UITRADE_OP_FFI: return uitrade_event_ok(slot,op,data,n,why);
    case UIINV_OP_CLICK:
        if(!(uirep_families&UIREP_FAMILY_INVENTORY) || (slot!=1 && !mpp_armed) || n<2 || !data[0] || data[0]>UIINV_KINDS) {*why="ui_inventory";return 0;}
        return 1;
    case UIREP_OP_CALL: {
        const uirep_call_spec *c=n && data[0]<UIREP_HOOKS?uirep_spec_of(data[0]):NULL;
        if(!c || !(uirep_families&uirep_hooks[data[0]].family) || uirep_call_length(c,data,n)!=n) {*why="ui_call";return 0;}
        return 1;
    }
    case UIBUILD_OP_PART:return uibuild_event_ok(slot,op,data,n,why);
    case UIFAC_OP_PART:return uifac_event_ok(slot,op,data,n,why);   /* lockstep_ui_faction.h */
    case UIREP_OP_MISSION:case UIREP_OP_CONVERSATION:return uimis_event_ok(slot,op,data,n,why);   /* lockstep_ui_missions.h */
    default:*why="ui_op";return 0;
    }
}
/* A whole section as the wire carries it; on the wire every byte is accounted for. */
static int uirep_section_ok(unsigned slot,const unsigned char *b,unsigned n,uint16_t *capture16,const char **why) {
    if(n<UIREP_HEAD+2 || n>UIREP_SECTION_MAX || b[0]!=UIREP_VERSION) {*why="ui_section";return 0;}
    unsigned count=b[3],at=UIREP_HEAD;
    if(!count || count>UIREP_EVENTS_MAX) {*why="ui_section";return 0;}
    for(unsigned i=0;i<count;i++) {
        if(n-at<2) {*why="ui_section";return 0;}
        unsigned op=b[at],length=b[at+1];at+=2;
        if(length>n-at || length>UIREP_EVENT_MAX) {*why="ui_section";return 0;}
        if(!uirep_event_ok(slot,op,b+at,length,why)) return 0;
        at+=length;
    }
    if(at!=n) {*why="ui_section";return 0;}
    *capture16=(uint16_t)(b[1]|b[2]<<8);return 1;
}
/* This tick's section of the queued events that fit (Lua, the mux's proposal): 0 = nothing this tick. */
static unsigned uirep_take(uint64_t tick,unsigned budget,unsigned char *out) {
    if(budget>UIREP_SECTION_MAX) budget=UIREP_SECTION_MAX;
    if(!uirep_queue_count) return 0;
    if(!pilot_slot_present(stats.slot,tick)) {InterlockedIncrement64(&uirep_waits);return 0;}   /* absent (hot join): queued until H */
    unsigned at=UIREP_HEAD,count=0;
    while(count<uirep_queue_count && count<UIREP_EVENTS_MAX) {
        const uirep_event *e=&uirep_queue[(uirep_queue_head+count)%UIREP_QUEUE];
        if(at+2u+e->length>budget) break;
        out[at]=e->op;out[at+1]=e->length;memcpy(out+at+2,e->data,e->length);at+=2u+e->length;++count;
    }
    if(!count) {InterlockedIncrement64(&uirep_waits);return 0;}
    out[0]=UIREP_VERSION;out[1]=(unsigned char)tick;out[2]=(unsigned char)(tick>>8);out[3]=(unsigned char)count;
    uirep_queue_head=(uirep_queue_head+count)%UIREP_QUEUE;uirep_queue_count-=count;
    InterlockedExchangeAdd64(&uirep_taken,(LONG64)count);
    return at;
}
/* Staging from the wire (every client): one apply tick, still ahead of every pass. */
static int uirep_stage(unsigned slot,const unsigned char *bytes,unsigned length,uint64_t now,const char **why) {
    uint16_t capture16=0;
    if(slot<1 || slot>PILOT_SLOTS || !(pstk_slot_mask>>(slot-1)&1u)) {*why="ui_slot";return 0;}
    if(!uirep_section_ok(slot,bytes,length,&capture16,why)) return 0;
    uint64_t back=(uint16_t)(now-capture16);
    if(back>now) {*why="ui_tick";return 0;}
    uint64_t capture=now-back,apply=capture+pilot_delay;
    if(!capture || capture<pstk_capture_start || apply<=now || apply>=now+UIREP_RING) {*why="ui_late";return 0;}
    if(!pilot_slot_present(slot,capture)) {*why="ui_absent";return 0;}   /* a stand-in row carries none (hot join) */
    uirep_staged *e=&uirep_ring[slot-1][apply%UIREP_RING];
    if(e->present && e->tick==apply) {
        if(e->length==length && !memcmp(e->bytes,bytes,length)) return 1;
        *why="ui_conflict";return 0;
    }
    if(e->present && e->tick>now) {*why="ui_ring";return 0;}
    memset(e,0,sizeof(*e));e->tick=apply;e->length=length;memcpy(e->bytes,bytes,length);e->present=1;
    InterlockedIncrement64(&uirep_staged_count);return 1;
}

/* ---- apply: native events of the tick (path_release) ---- */
static unsigned char *uirep_component(uint64_t runtime) {
    void *map=NULL;
    if(!uirep_fn.find || !read_local(pnat_base+UIREP_MAP,&map,8)) return NULL;
    return (unsigned char*)uirep_fn.find(map,runtime,4);
}
/* 1 = resolved, 0 = gone (the same on every client), -1 = the identities differ: stop. */
static int uirep_resolve(const unsigned char *data,uint64_t *runtime,unsigned char **component) {
    uint32_t check=0;*runtime=uirep_get_ref(data,&check);
    *component=*runtime?uirep_component(*runtime):NULL;
    if(!*component) return 0;
    return uirep_check_of(*runtime)==check?1:-1;
}
static int uirep_run_native(unsigned slot,unsigned op,const unsigned char *data,const char **why) {
    uint64_t runtime=0;unsigned char *component=NULL;
    switch(op) {
    case UIREP_OP_ACTIVITY_START:
        if(slot>=2) uirep_adapter_start(&pslots[slot],data[0]);else uirep_fn.start(NULL,data[0],data[1]);
        break;
    case UIREP_OP_ACTIVITY_STOP:
        if(slot>=2) uirep_adapter_stop(&pslots[slot],data[0]);else uirep_fn.stop(NULL,data[0],data[1]);
        break;
    case UIREP_OP_TARGET_SET:case UIREP_OP_TARGET_CLEAR: {
        int resolved=uirep_resolve(data,&runtime,&component);
        if(resolved<0) {*why="ui_ref_mismatch";return 0;}
        if(!resolved) {InterlockedIncrement64(&uirep_stale);break;}
        /* The stock functions also write the cycling hint: it stays what this client's UI left. */
        uint64_t hint=uirep_get_hint();
        if(op==UIREP_OP_TARGET_SET) {
            unsigned char offset[UIREP_OFFSET];memcpy(offset,data+UIREP_REF+1,UIREP_OFFSET);
            uirep_fn.target_set(component+UIREP_ASSOCIATION,offset,data[UIREP_REF]);
        } else uirep_fn.target_clear(component+UIREP_ASSOCIATION);
        uirep_set_hint(hint);
        break;
    }
    case UIREP_OP_CALL: {
        const uirep_call_spec *c=uirep_spec_of(data[0]);uint64_t a[UIREP_ARGS]={0};
        static __attribute__((aligned(16))) unsigned char vectors[UIREP_ARGS][16];static char strings[UIREP_ARGS][UIREP_STRING_MAX+1];
        unsigned at=1;
        for(unsigned i=0;c->spec[i];i++) {
            switch(c->spec[i]) {
            case 'P': a[i]=(uint64_t)(uintptr_t)pnat_player();break;
            case 'C':case 'I': {
                int resolved=uirep_resolve(data+at,&runtime,&component);at+=UIREP_REF;
                if(resolved<0) {*why="ui_ref_mismatch";return 0;}
                if(!resolved) {InterlockedIncrement64(&uirep_stale);InterlockedIncrement64(&uirep_applied_native);return 1;}
                a[i]=c->spec[i]=='C'?(uint64_t)(uintptr_t)component:runtime;
                break;
            }
            case 'i': {   /* 0 as sent; else as 'I' */
                uint32_t check=0;
                if(!uirep_get_ref(data+at,&check) && !check) {at+=UIREP_REF;a[i]=0;break;}
                int resolved=uirep_resolve(data+at,&runtime,&component);at+=UIREP_REF;
                if(resolved<0) {*why="ui_ref_mismatch";return 0;}
                if(!resolved) {InterlockedIncrement64(&uirep_stale);InterlockedIncrement64(&uirep_applied_native);return 1;}
                a[i]=runtime;
                break;
            }
            case 'b': a[i]=data[at++];break;
            case 'w': {uint32_t w=0;for(unsigned b=0;b<4;b++) w|=(uint32_t)data[at+b]<<(8*b);at+=4;a[i]=w;break;}
            case 'V': memcpy(vectors[i],data+at,16);at+=16;a[i]=(uint64_t)(uintptr_t)vectors[i];break;
            case 's': {unsigned len=data[at++];memcpy(strings[i],data+at,len);strings[i][len]=0;at+=len;a[i]=(uint64_t)(uintptr_t)strings[i];break;}
            default:a[i]=0;break;
            }
        }
        /* A slot in its own spacesuit (lockstep_pilot_death.h pvehk_*): its dock request is the way home, not X4's. */
        if(slot>=2 && (data[0]==UIREP_H_QUICKDOCK || data[0]==UIREP_H_DOCKAT || data[0]==UIREP_H_DOCKREASON) && pvehk_home_key(slot,0xafu)) {
            InterlockedIncrement64(&uirep_calls_applied);break;
        }
        uirep_forward(data[0],a);InterlockedIncrement64(&uirep_calls_applied);
        break;
    }
    case UICS_OP:
        if(!uics_run(slot,data,why)) return 0;
        break;
    default:break;
    }
    InterlockedIncrement64(&uirep_applied_native);return 1;
}
static int uirep_run_section(unsigned slot,const uirep_staged *e,const char **why) {
    unsigned count=e->bytes[3],at=UIREP_HEAD;
    for(unsigned i=0;i<count;i++) {
        unsigned op=e->bytes[at],length=e->bytes[at+1];const unsigned char *data=e->bytes+at+2;
        if(op<UIREP_OP_LUA_FIRST && !uirep_run_native(slot,op,data,why)) return 0;
        at+=2u+length;
    }
    return 1;
}
/* ---- the queue editor (family queue) ----
 * The map's order rows: FFI CreateOrder/CreateOrder2/CreateOrder3 (one function), CreateDeployToStationOrder,
 * EnableOrder, AdjustOrder, RemoveOrder/RemoveOrder2, ResetOrder, Enable/RemovePlannedDefaultOrder, SetOrderLoop,
 * SetCurrentLoopOrder (hooked here) and Lua SetOrderParam / RemoveOrderListParam (wrapped by the Lua core): Lua-run
 * events, applied in call order in the probe's frame on every client, the capturing one included and not earlier.
 * A create answers at once with what the menu goes on with: the index the order will have, predicted as the queue's
 * length (GetNumOrders) + 1 + this client's creates on that queue still in flight. Its apply records the index X4 gave
 * (an alias); the slot's later events that were captured before it applied (their seen count of the slot's applied
 * creates is lower than its number) take the real index for the predicted one. Every client derives the same aliases
 * from the same events: a prediction that misses moves where an edit lands, never whether the clients agree. The
 * cost: the capturing menu sees its own edit after the delay, and a read-back in the same frame (GetNumOrders,
 * GetPlannedDefaultOrder) still sees the queue before it. */
#define UIREP_Q_ALIASES 32u
#define UIREP_Q_PENDING 32u
typedef struct {unsigned slot;uint64_t ctl;uint32_t predicted,real,seq;} uirep_q_alias;
static uirep_q_alias uirep_q_aliases[UIREP_Q_ALIASES];
static unsigned uirep_q_alias_count;
static uint64_t uirep_q_pending_ctl[UIREP_Q_PENDING];
static uint32_t uirep_q_pending_seq[UIREP_Q_PENDING],uirep_q_captured_creates,uirep_q_applied[PILOT_SLOTS];
static volatile LONG64 uirep_q_captured,uirep_q_calls,uirep_q_mapped,uirep_q_moved,uirep_q_creates,uirep_q_skipped;
static uint32_t (*uirep_num_orders_override)(uint64_t); /* tests */
static void uirep_queue_reset(void) {
    memset(uirep_q_aliases,0,sizeof(uirep_q_aliases));uirep_q_alias_count=0;memset(uirep_q_pending_ctl,0,sizeof(uirep_q_pending_ctl));
    memset(uirep_q_pending_seq,0,sizeof(uirep_q_pending_seq));uirep_q_captured_creates=0;memset(uirep_q_applied,0,sizeof(uirep_q_applied));
    uirep_q_captured=uirep_q_calls=uirep_q_mapped=uirep_q_moved=uirep_q_creates=uirep_q_skipped=0;
}
static uint32_t uirep_q_own_applied(void) {unsigned s=pstk_capture_slot;return s>=1 && s<=PILOT_SLOTS?uirep_q_applied[s-1]:0;}
static unsigned uirep_q_pending_of(uint64_t ctl) {
    unsigned n=0;uint32_t applied=uirep_q_own_applied();
    for(unsigned i=0;i<UIREP_Q_PENDING;i++) if(uirep_q_pending_ctl[i]==ctl && uirep_q_pending_seq[i]>applied) ++n;
    return n;
}
static int uirep_slot_ship(uint64_t runtime) {
    unsigned char *object=runtime?uirep_component(runtime):NULL;
    for(unsigned i=0;object && i<pilot_slot_count && i<PILOT_SLOTS;i++) if(pilot_slots[i].object==object) return 1;
    return 0;
}
static uint32_t uirep_num_orders(uint64_t ctl) {
    return uirep_num_orders_override?uirep_num_orders_override(ctl):((uint32_t(*)(uint64_t))(void*)(pnat_base+UIREP_NUM_ORDERS))(ctl);
}
/* The index an event of this slot meant: the real one of a create it could not have seen applied. */
static uint32_t uirep_q_map(unsigned slot,uint64_t ctl,uint32_t index,uint32_t seen) {
    for(unsigned i=uirep_q_alias_count;i-->0 && uirep_q_alias_count-i<=UIREP_Q_ALIASES;) {
        const uirep_q_alias *a=&uirep_q_aliases[i%UIREP_Q_ALIASES];
        if(a->slot==slot && a->ctl==ctl && a->predicted==index && seen<a->seq) {
            if(a->real!=index) InterlockedIncrement64(&uirep_q_mapped);
            return a->real;
        }
    }
    return index;
}
static uint64_t uirep_queue_call(unsigned hook,uint64_t a[UIREP_ARGS]) {
    const uirep_call_spec *c=uirep_queue_spec_of(hook);
    if(!c || !uirep_capture_now()) {InterlockedIncrement64(&uirep_passed);return uirep_forward(hook,a);}
    int k=uirep_spec_checkonly(c->spec);
    if(k>=0 && (a[k]&0xffu)) return uirep_forward(hook,a);   /* a query changes nothing */
    unsigned char data[UIREP_EVENT_MAX];unsigned n=0;const char *why=NULL;uint32_t seen=uirep_q_own_applied(),predicted=0;
    data[n++]=(unsigned char)hook;data[n++]=(unsigned char)seen;data[n++]=(unsigned char)(seen>>8);
    for(unsigned i=0;c->spec[i] && !why;i++) {
        switch(c->spec[i]) {
        case 'I':
            if(!a[i] || !uirep_component(a[i]) || !uirep_check_of(a[i])) why="unmapped";
            else if(uirep_slot_ship(a[i])) why="slot_ship";   /* a PlayerController flies it: no queue (the Lua side alike) */
            else {uirep_put_ref(data+n,a[i]);n+=UIREP_REF;}
            break;
        case 'q': if(a[i]>0xffffffffu) why="index";else for(unsigned b=0;b<4;b++) data[n++]=(unsigned char)(a[i]>>(8*b));break;
        case 'b': data[n++]=(unsigned char)a[i];break;
        case 's': {
            const char *s=(const char*)(uintptr_t)a[i];size_t len=s?strnlen(s,UIREP_STRING_MAX+1):0;
            if(!s || len>UIREP_STRING_MAX) why="string";else {data[n++]=(unsigned char)len;memcpy(data+n,s,len);n+=(unsigned)len;}
            break;
        }
        default:break;   /* k */
        }
    }
    if(seen>0xffffu) why="seen";
    if(!why && c->returns==UIREP_RET_ORDER) {
        int planned_default=hook==UIREP_H_CREATE3 && (a[2]&0xffu);   /* the planned default order is not in the queue: 0 */
        predicted=planned_default?0:uirep_num_orders(a[0])+1u+uirep_q_pending_of(a[0]);
        for(unsigned b=0;b<4;b++) data[n++]=(unsigned char)(predicted>>(8*b));
    }
    if(why) {uirep_note_refusal(uirep_hooks[hook].name,why);return 0;}
    if(!uirep_push(UIREP_OP_QUEUE,data,n)) return 0;
    InterlockedIncrement64(&uirep_q_captured);
    if(c->returns==UIREP_RET_ORDER) {
        if(predicted) {uint32_t seq=++uirep_q_captured_creates;uirep_q_pending_ctl[seq%UIREP_Q_PENDING]=a[0];uirep_q_pending_seq[seq%UIREP_Q_PENDING]=seq;}
        return predicted;
    }
    if(c->returns==UIREP_RET_PREDICT && k>=0) {a[k]=1;return uirep_forward(hook,a);}
    return c->returns==UIREP_RET_ONE;
}
/* One queue event of slot `slot` (the probe's frame, every client): 1 = X4 ran it (its answer in what), 0 = nothing ran,
 * the same everywhere (the controllable is gone or a slot ship), -1 = stop (decode, references differ). */
static int uirep_queue_run(unsigned slot,const unsigned char *data,unsigned n,char *what,size_t capacity) {
    const uirep_call_spec *c=n?uirep_queue_spec_of(data[0]):NULL;uint64_t a[UIREP_ARGS]={0},ctl=0;
    static char strings[UIREP_ARGS][UIREP_STRING_MAX+1];
    if(!c || slot<1 || slot>PILOT_SLOTS || uirep_queue_length(data,n)!=n) {snprintf(what,capacity,"decode");return -1;}
    uint32_t seen=data[1]|(uint32_t)data[2]<<8,predicted=0;unsigned at=3;int gone=0;
    for(unsigned i=0;c->spec[i];i++) {
        switch(c->spec[i]) {
        case 'I': {
            uint64_t runtime=0;unsigned char *component=NULL;int resolved=uirep_resolve(data+at,&runtime,&component);at+=UIREP_REF;
            if(resolved<0) {snprintf(what,capacity,"ref_mismatch");return -1;}
            if(!resolved) gone=1;
            a[i]=runtime;if(!i) ctl=runtime;
            break;
        }
        case 'q': {uint32_t w=0;for(unsigned b=0;b<4;b++) w|=(uint32_t)data[at+b]<<(8*b);at+=4;a[i]=uirep_q_map(slot,ctl,w,seen);break;}
        case 'b': a[i]=data[at++];break;
        case 's': {unsigned len=data[at++];memcpy(strings[i],data+at,len);strings[i][len]=0;at+=len;a[i]=(uint64_t)(uintptr_t)strings[i];break;}
        default:a[i]=0;break;   /* k: the call itself */
        }
    }
    if(c->returns==UIREP_RET_ORDER) for(unsigned b=0;b<4;b++) predicted|=(uint32_t)data[at+b]<<(8*b);
    const char *name=uirep_hooks[data[0]].name;uint64_t result=0;
    if(!gone && uirep_slot_ship(ctl)) {gone=2;}
    if(!gone) {result=uirep_forward(data[0],a);InterlockedIncrement64(&uirep_q_calls);}
    else InterlockedIncrement64(&uirep_q_skipped);
    if(c->returns==UIREP_RET_ORDER && predicted) {   /* numbered on every client alike, whether it ran or not */
        uirep_q_alias *al=&uirep_q_aliases[uirep_q_alias_count++%UIREP_Q_ALIASES];
        al->slot=slot;al->ctl=ctl;al->predicted=predicted;al->real=gone?0:(uint32_t)result;al->seq=++uirep_q_applied[slot-1];
        InterlockedIncrement64(&uirep_q_creates);
        if(al->real!=predicted) InterlockedIncrement64(&uirep_q_moved);
    }
    if(gone) snprintf(what,capacity,"%s %s",name,gone==2?"slot_ship":"gone");
    else if(c->returns==UIREP_RET_VOID) snprintf(what,capacity,"%s",name);
    else if(c->returns==UIREP_RET_ORDER) snprintf(what,capacity,"%s=%u/%u",name,(unsigned)(uint32_t)result,(unsigned)predicted);
    else snprintf(what,capacity,"%s=%u",name,(unsigned)(result&0xffu));
    return gone?0:1;
}
static int uirep_has_native(const uirep_staged *e) {
    unsigned count=e->bytes[3],at=UIREP_HEAD;
    for(unsigned i=0;i<count;i++) {
        if(e->bytes[at]<UIREP_OP_LUA_FIRST) return 1;
        at+=2u+e->bytes[at+1];
    }
    return 0;
}
static int uirep_frame_run(uint64_t tick,const char **why) {
    uirep_frame_tick=tick;uirep_frame_events=0;uirep_frame_hash=0;
    for(unsigned k=1;k<=PILOT_SLOTS;k++) {
        const uirep_staged *e=&uirep_ring[k-1][tick%UIREP_RING];
        if(!e->present || e->tick!=tick) continue;
        uirep_frame_events+=e->bytes[3];
        uirep_frame_hash=pstk_fnv(e->bytes,e->length,uirep_frame_hash^(UIREP_DOMAIN+k));
        if(!uirep_has_native(e)) continue;
        if(pdeath_armed && (pdeath_dead>>(k-1)&1u)) {InterlockedIncrement64(&uirep_stale);continue;} /* no ship: the hash stays, nothing applies */
        int scoped=k>=2,ok=0,left=1,fault=0;
        if(scoped && !uirep_fn.enter(k)) {*why="ui_scope";return 0;}
        InterlockedExchange(&uirep_replaying,1);
        __try {ok=uirep_run_section(k,e,why);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
        InterlockedExchange(&uirep_replaying,0);
        if(scoped) left=uirep_fn.leave(k);
        if(fault) {*why="ui_fault";return 0;}
        if(!left) {*why="ui_scope_leave";return 0;}
        if(!ok) return 0;
    }
    return 1;
}
static int uirep_frame(uint64_t tick,const char **why) {
    return uirep_frame_run(tick,why) || uirep_fail(*why);
}
/* The Lua-run events of this tick for the probe's frame: "slot:op:hex;...", NULL when none. */
static int uirep_lua_events(uint64_t tick,char *out,size_t capacity) {
    int n=0;
    for(unsigned k=1;k<=PILOT_SLOTS;k++) {
        uirep_staged *e=&uirep_ring[k-1][tick%UIREP_RING];
        if(!e->present || e->tick!=tick) continue;
        unsigned count=e->bytes[3],at=UIREP_HEAD;
        for(unsigned i=0;i<count;i++) {
            unsigned op=e->bytes[at],length=e->bytes[at+1];
            if(op>=UIREP_OP_LUA_FIRST) {
                char hex[2*UIREP_EVENT_MAX+1];pilot_hex(e->bytes+at+2,length,hex);
                int more=snprintf(out+n,capacity-(size_t)n,"%s%u:%u:%s",n?";":"",k,op,hex);
                if(more<0 || (size_t)more>=capacity-(size_t)n) return -1;
                n+=more;InterlockedIncrement64(&uirep_applied_lua);
            }
            at+=2u+length;
        }
        e->present=0;
    }
    return n;
}
static void uirep_result(uint64_t tick,int ok) {
    if(uirep_results_tick!=tick) {uirep_results_tick=tick;uirep_results_hash=0;}
    unsigned char b=(unsigned char)(ok!=0);uirep_results_hash=pstk_fnv(&b,1,uirep_results_hash^UIREP_DOMAIN);
    if(!ok) InterlockedIncrement64(&uirep_lua_errors);
}
/* An image region's bytes in slot k's store (the slot's own values while no scope is open). */
static int uirep_store_read(const pslot_slot *s,unsigned rva,void *out,unsigned size) {
    unsigned n=0;
    if(!s->length) return 0;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(r->base!=VPL_BASE_IMAGE || !(PSLOT_CLASSES>>r->cls&1u)) continue;
        if(rva>=r->offset && rva+size<=r->offset+r->size) {memcpy(out,s->store+n+(rva-r->offset),size);return 1;}
        n+=r->size;
    }
    return 0;
}
static int uirep_frame_text(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"PUIREP_1:%llu:%u:%016llx:%016llx",(unsigned long long)uirep_frame_tick,uirep_frame_events,(unsigned long long)uirep_frame_hash,
        (unsigned long long)(uirep_results_tick==uirep_frame_tick?uirep_results_hash:0));
    for(unsigned k=1;n>0 && (size_t)n<capacity && k<=pilot_slot_count && k<=PILOT_SLOTS;k++) {
        const unsigned char *player=k==1?pnat_player():pslots[k].player;int activity=0;uint64_t target=0;
        if(player) {read_local(player+UIREP_PLAYER_ACTIVITY,&activity,4);read_local(player+PRES_TARGET,&target,8);}
        if(target && !uirep_component(target)) target=0;   /* dead: 9fcd70 clears it when someone asks, which is not simulation */
        int more=snprintf(out+n,capacity-(size_t)n,"%s%d/%llx",k==1?"|":",",activity,(unsigned long long)target);
        if(more<0 || (size_t)more>=capacity-(size_t)n) return 0;
        n+=more;
        const unsigned char *ship=pilot_slots[k-1].resolved?pilot_slots[k-1].object:NULL;
        if(uirep_families&(UIREP_FAMILY_AUTOPILOT|UIREP_FAMILY_DOCK|UIREP_FAMILY_WEAPONS)) {
            /* autopilot on (the bank's target, slot 1 live, slot k in its store), active primary/secondary group of the ship */
            uint64_t autopilot=0;int32_t groups[2]={-2,-2};
            if(k==1) read_local(pnat_base+UIREP_AUTOPILOT,&autopilot,8);else uirep_store_read(&pslots[k],UIREP_AUTOPILOT,&autopilot,8);
            if(ship) read_local(ship+UIREP_GROUP_PRIMARY,groups,8);
            more=snprintf(out+n,capacity-(size_t)n,"/%d/%d.%d",autopilot!=0,groups[0],groups[1]);
            if(more<0 || (size_t)more>=capacity-(size_t)n) return 0;
            n+=more;
        }
        if(uirep_families&UIREP_FAMILY_FLIGHT) {
            /* the flight family: the slot's player's flight assist (P_k's own field), its ship's drones armed (types 2, 1, 3) */
            unsigned char assist=0,drones[3]={0,0,0};
            if(player) read_local(player+UIREP_PLAYER_ASSIST,&assist,1);
            if(ship) read_local(ship+UIREP_SHIP_DRONES,drones,3);
            more=snprintf(out+n,capacity-(size_t)n,"/f%u.%u%u%u",assist!=0,drones[0]!=0,drones[1]!=0,drones[2]!=0);
            if(more<0 || (size_t)more>=capacity-(size_t)n) return 0;
            n+=more;
        }
    }
    return n>0 && (size_t)n<capacity?n:0;
}

/* ---- install ---- */
static unsigned char *uirep_alloc_page(void) {
    uintptr_t at=(uintptr_t)(pnat_base+0x800000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            unsigned char *p=VirtualAlloc((void*)(sign<0?at-distance:at+distance),UIREP_PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t a=(intptr_t)p-(intptr_t)(pnat_base+0x100000u),b=(intptr_t)p-(intptr_t)(pnat_base+0xc00000u);
            if(a>INT32_MIN+0x10000 && a<INT32_MAX-0x10000 && b>INT32_MIN+0x10000 && b<INT32_MAX-0x10000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int uics_hooks_wanted(void);   /* lockstep_ui_compslot.h: its two perform hooks only while it is armed */
static int uirep_hook_used(unsigned i) {
    if(i==UIREP_H_CS_SINGLE || i==UIREP_H_CS_MULTI) return uics_hooks_wanted();
    return !uirep_hooks[i].family || (uirep_families&uirep_hooks[i].family);
}
static unsigned char *uirep_stride(unsigned i) {return uirep_page+UIREP_STRIDE*i;}
static unsigned char *uirep_site_stride(unsigned i) {return uirep_page+UIREP_STRIDE*(UIREP_HOOKS+i);}
/* mov rcx,rbx; [mov rdx,r13;] mov rax,stub; call rax; jmp [rip] resume: the frame is X4's, aligned, with its shadow
 * space, and rbx, r13 and rdi (zero) survive the stub. */
static unsigned uirep_write_site(unsigned i,unsigned char *p) {
    static const unsigned char head[8]={0x48,0x8b,0xd8,0x48,0x85,0xc0,0x75,0x0e},mov_rcx[3]={0x48,0x89,0xd9},mov_rdx[3]={0x4c,0x89,0xea},call_rax[2]={0xff,0xd0};
    void *stub=i==UIREP_S_TARGET_SET?(void*)uirep_target_set_stub:(void*)uirep_target_clear_stub;unsigned n=0;
    memset(p,0xcc,UIREP_STRIDE);
    if(i==UIREP_S_TARGET_CLEAR) {   /* no component: the stock error path; the jne hops over this 14-byte jump */
        memcpy(p,head,8);n=8;n+=pview_abs_jump(p+n,pnat_base+UIREP_CLEAR_ERROR);
    }
    memcpy(p+n,mov_rcx,3);n+=3;
    if(i==UIREP_S_TARGET_SET) {memcpy(p+n,mov_rdx,3);n+=3;}
    n+=pview_mov_rax(p+n,stub);memcpy(p+n,call_rax,2);n+=2;
    n+=pview_abs_jump(p+n,pnat_base+uirep_sites[i].resume);
    return n;
}
static void uirep_write_hook(unsigned i,unsigned char *p) {
    memset(p,0xcc,UIREP_STRIDE);
    memcpy(p,uirep_hooks[i].bytes,uirep_hooks[i].prefix);
    pview_abs_jump(p+uirep_hooks[i].prefix,pnat_base+uirep_hooks[i].rva+uirep_hooks[i].prefix);
    pview_abs_jump(p+UIREP_STUB,uirep_hooks[i].wrapper);
}
static int uirep_write(unsigned rva,const unsigned char *expected,const unsigned char *value) {return pview_cas(rva,expected,value,5);}
static void uirep_uninstall(void) {
    for(unsigned i=UIREP_SITES;i-->0;) {
        if(!(uirep_families&UIREP_FAMILY_TARGET) || !uirep_page || memcmp(pnat_base+uirep_sites[i].rva,uirep_site_patched[i],5)) continue;
        uirep_write(uirep_sites[i].rva,uirep_site_patched[i],uirep_sites[i].bytes);
    }
    for(unsigned i=UIREP_HOOKS;i-->0;) {
        if(!uirep_hook_used(i) || !uirep_page || memcmp(pnat_base+uirep_hooks[i].rva,uirep_patched[i],5)) continue;
        uirep_write(uirep_hooks[i].rva,uirep_patched[i],uirep_hooks[i].bytes);
    }
    int clean=1;
    for(unsigned i=0;i<UIREP_HOOKS;i++) if(uirep_hook_used(i) && uirep_page && memcmp(pnat_base+uirep_hooks[i].rva,uirep_hooks[i].bytes,5)) clean=0;
    if(uirep_families&UIREP_FAMILY_TARGET) for(unsigned i=0;i<UIREP_SITES;i++) if(uirep_page && memcmp(pnat_base+uirep_sites[i].rva,uirep_sites[i].bytes,5)) clean=0;
    uirep_restored=clean;
    if(clean) uirep_installed=0;
}
static int uirep_install(void) {
    if(uirep_installed) return 1;
    for(unsigned i=0;i<UIREP_HOOKS;i++) if(uirep_hook_used(i) && memcmp(pnat_base+uirep_hooks[i].rva,uirep_hooks[i].bytes,uirep_hooks[i].prefix)) return 0;
    if(uirep_families&UIREP_FAMILY_TARGET) for(unsigned i=0;i<UIREP_SITES;i++) if(memcmp(pnat_base+uirep_sites[i].rva,uirep_sites[i].bytes,5)) return 0;
    if(!uirep_page && !(uirep_page=uirep_alloc_page())) return 0;
    DWORD old,ignored;
    if(!VirtualProtect(uirep_page,UIREP_PAGE,PAGE_READWRITE,&old)) return 0;
    for(unsigned i=0;i<UIREP_HOOKS;i++) uirep_write_hook(i,uirep_stride(i));
    for(unsigned i=0;i<UIREP_SITES;i++) uirep_write_site(i,uirep_site_stride(i));
    if(!VirtualProtect(uirep_page,UIREP_PAGE,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),uirep_page,UIREP_PAGE);
    if(!uirep_fn.ui_move) uirep_fn.ui_move=(void(*)(void*))uirep_stride(UIREP_H_UI);
    if(!uirep_fn.start) uirep_fn.start=(uirep_activity_fn)uirep_stride(UIREP_H_START);
    if(!uirep_fn.stop) uirep_fn.stop=(uirep_activity_fn)uirep_stride(UIREP_H_STOP);
    for(unsigned i=UIREP_H_CLEAR;i<UIREP_HOOKS;i++) if(!uirep_fn.forward[i]) uirep_fn.forward[i]=(uirep_forward_fn)uirep_stride(i); /* guards and calls */
    for(unsigned i=0;i<UIREP_HOOKS;i++) {
        int32_t rel=(int32_t)((intptr_t)(uirep_stride(i)+UIREP_STUB)-(intptr_t)(pnat_base+uirep_hooks[i].rva+5));
        uirep_patched[i][0]=0xe9;memcpy(uirep_patched[i]+1,&rel,4);
    }
    for(unsigned i=0;i<UIREP_SITES;i++) {
        int32_t rel=(int32_t)((intptr_t)uirep_site_stride(i)-(intptr_t)(pnat_base+uirep_sites[i].rva+5));
        uirep_site_patched[i][0]=0xe9;memcpy(uirep_site_patched[i]+1,&rel,4);
    }
    uirep_restored=0;uirep_installed=1;
    for(unsigned i=0;i<UIREP_HOOKS;i++)
        if(uirep_hook_used(i) && !uirep_write(uirep_hooks[i].rva,uirep_hooks[i].bytes,uirep_patched[i])) {uirep_uninstall();uirep_installed=0;return 0;}
    if(uirep_families&UIREP_FAMILY_TARGET) for(unsigned i=0;i<UIREP_SITES;i++)
        if(!uirep_write(uirep_sites[i].rva,uirep_sites[i].bytes,uirep_site_patched[i])) {uirep_uninstall();uirep_installed=0;return 0;}
    return 1;
}
static int uirep_intact(void) {
    if(!uirep_installed || !uirep_page) return 0;
    for(unsigned i=0;i<UIREP_HOOKS;i++) if(uirep_hook_used(i) && memcmp(pnat_base+uirep_hooks[i].rva,uirep_patched[i],5)) return 0;
    if(uirep_families&UIREP_FAMILY_TARGET) for(unsigned i=0;i<UIREP_SITES;i++) if(memcmp(pnat_base+uirep_sites[i].rva,uirep_site_patched[i],5)) return 0;
    return 1;
}
/* The UI guard (lockstep_ui_guard.h, included at the end): its pass and restore ride the replay's. */
static int uiguard_armed;
static const char *uiguard_why_text;
static int uiguard_pass(uint64_t tick);
static void uiguard_restore(void);
static int uirep_guard_pass(uint64_t tick) {
    return (uitrade_pass(tick) || uirep_fail(uitrade_why_text)) && (uifac_pass(tick) || uirep_fail(uifac_why)) && (uiguard_pass(tick) || uirep_fail(uiguard_why_text));
}
static int uimis_ready(void);   /* lockstep_ui_missions.h, included at the end */
static const char *uimis_why;
/* Lua found a problem (a refused section, a reference): the next pass stops, on the frame thread. */
static void uirep_async_fail(const char *why) {
    uirep_fail(why);pnat_async_fail(why);
}
/* The module's part of a pass (pnat_pass, every tick, frame thread, before the stock records). */
static int uirep_pass(uint64_t tick) {
    if(uirep_failed) return uirep_fail(uirep_why);
    if(tick>pilot_stop) {
        uirep_stopped=1;
        if(uirep_installed) {uirep_uninstall();if(!uirep_restored) return uirep_fail("ui_restore");uirep_stage_name="stopped";}
        return uirep_guard_pass(tick);
    }
    if(!uirep_installed) {
        if(rid_mode!=RID_SET) return uirep_fail("ui_runtime_ids"); /* set right before the load, after the arming */
        if((uirep_families&UIREP_FAMILY_MISSILES) && !pfire_armed) return uirep_fail("ui_missiles_need_fire"); /* the launchers: pilot fire */
        if(!uimis_ready()) return uirep_fail(uimis_why);   /* missions, conversation: the guard with their groups */
        if(!uifac_ready()) return uirep_fail(uifac_why);   /* faction: armed, with the guard */
        if(!uirep_install()) return uirep_fail("ui_install");
        uirep_stage_name="live";return uirep_guard_pass(tick);
    }
    if(!uirep_intact()) return uirep_fail("ui_hook_lost");
    return uirep_guard_pass(tick);
}
static void pss_restore(void);   /* lockstep_player_settings.h (through lockstep_ui_faction.h, included at the end) */
static void uirep_restore(void) {
    if(!uirep_armed) return;
    uirep_stopped=1;
    mpp_restore_hook();
    uiguard_restore();
    uitrade_restore();
    pss_restore();
    if(uirep_installed) uirep_uninstall();
    if(uirep_installed) uirep_stage_name="restore_refused";
}
/* Arming (Lua, before the first admitted tick, after gate.pilot_stock and gate.pilot_present). */
static const char *uirep_arm_why="-";
static int uirep_refuse(const char *why) {uirep_arm_why=why;return 0;}
static const unsigned char *uirep_slot_player(unsigned slot) {return slot==1?pnat_player():slot<=PILOT_SLOTS?pslots[slot].player:NULL;}
static int uirep_arm_using(unsigned families) {
    if(uirep_armed) return uirep_families==families?1:uirep_refuse("rerun");
    if(!families || (families&~UIREP_FAMILIES)) return uirep_refuse("families");
    /* Missiles fly at the slot's soft target: only the target family makes it the same on every client. */
    if((families&UIREP_FAMILY_MISSILES) && !(families&UIREP_FAMILY_TARGET)) return uirep_refuse("missiles_need_target");
    if(!pnat_armed || pnat_mode!=PNAT_MODE_STOCK || !pstk_capturing) return uirep_refuse("needs_stock");
    if(pilot_slot_count<1 || pstk_slot_mask!=(1u<<pilot_slot_count)-1u || (pilot_slot_count>=2 && !pslot_armed)) return uirep_refuse("needs_all_stock_slots");
    if(!pres_armed) return uirep_refuse("needs_present");
    uirep_families=families;uirep_failed=0;uirep_stopped=0;uirep_stage_name="armed";uirep_why="-";uirep_restored=1;uirep_installed=0;uirep_arm_why="-";
    memset(uirep_ring,0,sizeof(uirep_ring));uirep_queue_head=uirep_queue_count=0;uirep_refusal_head=uirep_refusal_tail=0;
    uirep_frame_tick=uirep_results_tick=0;uirep_queue_reset();
    uirep_armed=1;return 1;
}
static int uirep_arm(unsigned families) {
    if(uirep_armed) return uirep_arm_using(families);
    if(stats.issued) return uirep_refuse("after_first_tick");
    if(GetCurrentThreadId()!=ui_thread) return uirep_refuse("thread");
    if(!uirep_pins()) {static char pin_why[32];snprintf(pin_why,sizeof(pin_why),"pins_ui_replay:%x",pin_failed_rva);return uirep_refuse(pin_why);}
    uirep_use_image();
    return uirep_arm_using(families);
}
static int uirep_refusals_text(char *out,size_t capacity) {
    int n=0;out[0]=0;
    while(uirep_refusal_tail!=uirep_refusal_head) {
        const uirep_refusal *r=&uirep_refusals[uirep_refusal_tail%UIREP_REFUSALS];
        int more=snprintf(out+n,capacity-(size_t)n,"%s%llu:%s:%s",n?",":"",(unsigned long long)r->tick,r->what,r->why);
        if(more<0 || (size_t)more>=capacity-(size_t)n) break;
        n+=more;++uirep_refusal_tail;
    }
    return n;
}
static int uirep_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"ui_replay=%s why=%s families=%x installed=%d restored=%d captured=%lld refused=%lld guard=%lld taken=%lld waits=%lld queued=%u staged=%lld "
        "native=%lld calls=%lld lua=%lld lua_errors=%lld stale=%lld passed=%lld target_passed=%lld scope=%lld/%lld/%lld nested=%lld unavailable=%lld",uirep_stage_name,uirep_why,uirep_families,uirep_installed,uirep_restored,
        (long long)uirep_captured,(long long)uirep_refused,(long long)uirep_guard_refused,(long long)uirep_taken,(long long)uirep_waits,uirep_queue_count,(long long)uirep_staged_count,
        (long long)uirep_applied_native,(long long)uirep_calls_applied,(long long)uirep_applied_lua,(long long)uirep_lua_errors,(long long)uirep_stale,(long long)uirep_passed,(long long)uirep_target_passed,
        (long long)uirep_scope_start,(long long)uirep_scope_stop,(long long)uirep_scope_refused,(long long)uirep_scope_nested,(long long)uirep_unavailable);
    if(n>0 && (size_t)n<capacity && (uirep_families&UIREP_FAMILY_QUEUE))
        n+=snprintf(out+n,capacity-(size_t)n," queue=%lld/%lld/%lld creates=%lld mapped=%lld moved=%lld",(long long)uirep_q_captured,(long long)uirep_q_calls,
            (long long)uirep_q_skipped,(long long)uirep_q_creates,(long long)uirep_q_mapped,(long long)uirep_q_moved);
    if(n>0 && (size_t)n<capacity && (uirep_families&UIREP_FAMILY_TARGET)) {
        /* the soft target outside the local phases: the UI's own captured/refused, MD's, the last UI route (image return RVAs) */
        char route[UIREP_ROUTE_SHOWN*9+2]="-";int k=0;
        for(unsigned i=0;i<uirep_route_last_count && i<UIREP_ROUTE_SHOWN;i++) k+=snprintf(route+k,sizeof(route)-(size_t)k,"%s%x",i?",":"",uirep_route_last[i]);
        n+=snprintf(out+n,capacity-(size_t)n," target_ui=%lld/%lld target_md=%lld target_route=%s route_faults=%lld",(long long)uirep_target_ui,
            (long long)uirep_target_ui_refused,(long long)uirep_target_md,route,(long long)uirep_route_faults);
    }
    if(n>0 && (size_t)n<capacity) n+=uics_status(out+n,capacity-(size_t)n);   /* lockstep_ui_compslot.h */
    if(n>0 && (size_t)n<capacity && uirh_armed) n+=uirh_status(out+n,capacity-(size_t)n);   /* lockstep_ui_reload_hold.h */
    return n>0 && (size_t)n<capacity?n:0;
}
/* The UI guard: containment for what no armed family replays yet (native_gate_ui_guard, default off). */
#include "lockstep_ui_guard.h"
#include "lockstep_ui_compslot.h"        /* component slot player actions by class (native_gate_ui_compslot) */
#include "lockstep_pilot_foot_guard.h"   /* slot 1 on foot: the docked menu's GetUp claimed (lockstep_pilot_foot.h) */
/* The build and assets families: claims on the guard's hooks (native_gate_ui_replay lists build/assets, default off). */
#include "lockstep_ui_build.h"
/* Missions and conversations of slot 1 (families missions, conversation; captured in the guard's hook). */
#include "lockstep_ui_missions.h"
#include "lockstep_mission_owners.h"   /* personal missions per player: slot k's calls and owned cue trees in slot k's scope */
#include "lockstep_md_presentation.h"   /* what an owned cue shows (help, notifications, menus, music): on its owner's client only */
/* The trade family of the replay (native_gate_ui_replay lists trade, default off). */
#include "lockstep_ui_trade.h"
/* The faction family: the player faction's settings for every slot (claims on the guard's hooks; uses the trade scope). */
#include "lockstep_ui_faction.h"
/* The inventory family's native part: the inventory menu's Drop (a claim on the guard's hook). */
#include "lockstep_ui_inventory.h"

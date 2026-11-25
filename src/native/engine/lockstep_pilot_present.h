/* Presentation of a stock slot on its own client, DEFAULT OFF, stop reason 162
 * (native_gate_pilot_present; needs stock slots 2..N, lockstep_pilot_slots.h, and the
 * render split). Client k's stock camera, HUD and UI present slot k's ship as "the
 * player": X4's own code runs for presentation and finds slot k's ship when it asks about
 * the player. The simulation never sees it: every byte is back before simulation code runs.
 *
 * The singleton does not change. stock-K1 ran UI code with the singleton = P_k and faulted
 * at once (ui_fault:3de6aa, a NULL component into the view-cache transform): P_k was built
 * by the factory and is not in the tree, so it has no published view cache and no state from
 * a world load (report section 16.12). So X4's own player, which has all of that, keeps its
 * object and takes slot k's identity for the length of a scope:
 *
 *   presentation scope (this client's own slot only, frame thread, no slot scope open)
 *     on the canonical player: parent +0x70 = slot k's cockpit room, controlled-object
 *     vector +0x308 = P_k's, control position +0x6ab8 = P_k's, the published parent of its
 *     view cache for this thread's bank = the room, its SIM fields = P_k's; on the room:
 *     attention 11, live and published (Room::vfunc703 0x76e520 gives the player's room 11,
 *     another ship's cockpit its ship's 9 or less; the simulation's value is back at the exit);
 *     SIM and DEVICE image regions: a COPY of slot k's store (the store is never touched,
 *     so a write refuses itself: it is discarded at the exit, and a changed SIM byte is
 *     counted and its region named, refused=). LOCAL stays this client's. At the exit every
 *     byte comes back. The camera stays anchored on the canonical player as in stock play,
 *     its cockpit mode then finds slot k's ship through the same walks. The player's own pose
 *     in the room, live and published, is moved from slot 1's seat to slot k's: another ship
 *     model seats its pilot elsewhere in its room (lockstep_pilot_eye.h, status eye=).
 *   entry points (nothing else runs in it)
 *     VBI slot 6   UI::CUIController::Move, vtable entry 0x2bc07d8: the UI and its FFI
 *                  queries, except the probe's own frame (gate.pilot_present_pause/resume:
 *                  gate modules read the canonical state);
 *     VBI slot 10  GameRender::Move (through the render split's wrapper): HUD draw,
 *                  animation, every camera;
 *     VBI slot 4   the dispatch of presentation ids (camera, menus, map, targets);
 *     VBI slot 9   XSfx::Base::Move, vtable entry 0x2caf290: the sound update, every voice placed and occluded
 *                  (native_gate_pilot_present_sound, the sound below);
 *     9fe573       the camera's view bank refill in UniverseFrameUpdate (re-pointed
 *                  through the near page);
 *     9fe5d0       the camera pick 717ed0 (crosshair, what is under the cursor): it writes
 *                  the player's DEVICE block from the camera and must not hit ship k itself.
 *     9fe5ed       the click's pick 718d50, which X4 runs right after it in the frame after a mouse click asked for
 *                  one (the flag 0x3d19b08, cleared at 9fe5e3): the records 8c2830 uses for a click in the world
 *                  (+0x6c40, +0x6cc0: the seat, a console, a person). Outside the scope it ran as slot 1's player,
 *                  in another ship: client k's clicks on foot found nothing (the user's session of 11 Nov 22:25:
 *                  twenty clicks, no pick). Its call site straddles a 16-byte block (no atomic patch), so the
 *                  frame pick's wrapper takes it over: flag set, it clears the flag and runs 718d50 in the same
 *                  scope; X4's own test then finds the flag clear.
 *   a self-test at the start, in the scope, before any UI runs in it: X4's own
 *     GetPlayerOccupiedShipID and the view-cache ancestor lookup 0x3d91e0 must answer slot
 *     k's ship and room (identity_occupied, identity_view_parent: stop 162);
 *     GetPlayerControlledShipID and GetPlayerZoneID are logged (asks=). The real engine
 *     confirms the overlay and the cache layout offline tests cannot.
 *   the HUD's place (mode 'hud', the default; 'camera' leaves it out): the UI anchor
 *     (Universe+0x470/0x478, ui_screen_positions) in a save hangs under the player's own
 *     camera_anchor_scene (Universe+0x428), so it follows the player in the scope with no
 *     connection at all. Only when it hangs elsewhere (a room) X4's own 0x8e8450 connects it
 *     to the player's cockpit room, in the scope; then a real connection, local to this
 *     client: a census difference in a --present run that --present camera does not show is
 *     this. hud=mode/moves: stock-K3 reissued it every tick (571), it should now say 0.
 *   PILOT_PRESENT_DIFF (once at the start, gate.pilot_present_diff): the words where slot 1's
 *     ship and room differ from slot k's in a way that matters (one zero, or small numbers):
 *     what a player's presence set on them.
 * The start, the anchor reissue and the stop run on this client only, inside the frame: the
 * thread's simulation RNG word is put back around them (rng_moved= counts a draw).
 * Faults: an access violation inside a presentation entry point closes the scope and
 * takes everything down at once, on the frame thread (UI entry, refill call, the HUD's
 * place: present_restored=1), and the session stops (162, why=<entry>_fault:<rva>).
 * present_fault= has the address, access, registers and the unwound call chain.
 * The soft target (+0x6dd0): what client k selects stays in the scope from then on (its HUD shows
 * it) and rides the record (a per-slot reader can see it); it does not reach P_k or any other
 * client's simulation yet, as slot 1's target stays local on client 1 (targets=). With the UI replay's
 * target family it is P_k's own, replayed on every client (lockstep_ui_replay.h), and nothing here keeps it.
 * Not yet: other world-changing UI actions as recorded events (refused, counted), Player::Move's
 * camera rule for external views outside slot 1's sector. */
#define PRES_STOP_REASON 162u
#define PRES_UI_ENTRY 0x2bc07d8u        /* UI::CUIController, XVbiHandler vtable 0x2bc07d0 slot 1 */
#define PRES_UI_MOVE 0xb3bad0u
#define PRES_UI_LOCK 0x70u              /* its critical section: entered at the top, left at the bottom */
#define PRES_REFILL_CALL 0x9fe573u      /* UniverseFrameUpdate: call 0x979ce0 for each listed camera */
#define PRES_REFILL 0x979ce0u
#define PRES_PICK_CALL 0x9fe5d0u        /* UniverseFrameUpdate: call 0x717ed0 with the player */
#define PRES_PICK 0x717ed0u
#define PRES_PICK_STUB 0x60u            /* on the slots' near page, after the refill's stub */
#define PRES_CLICK 0x718d50u            /* the click's pick (header, 9fe5ed) */
#define PRES_CLICK_ASKED 0x3d19b08u     /* its request flag: a dword, set by a click, cleared by 9fe5e3 before the call */
#define PRES_CLICK_SITE 0x9fe5d5u       /* the flag's test, its clear and the call, right after the frame pick's call */
#define PRES_ATTENTION 0x1ecu           /* Component: attention */
#define PRES_VIEW_ATTENTION 0x118u      /* Component: its published attention, +bank*0x80 */
#define PRES_ROOM_ATTENTION 11
#define PRES_DIFF_SHIP 0xc00u
#define PRES_DIFF_ROOM 0x400u
#define PRES_UI_ANCHORS 0x8e8450u       /* Universe: connects the UI anchor to the player's cockpit room */
#define PRES_UI_ANCHOR 0x470u           /* Universe: the anchor, +0x478 the cutscene one */
#define PRES_PARENT 0x70u               /* Component */
#define PRES_VECTOR 0x308u              /* Player: controlled-object vector (begin, end, capacity) */
#define PRES_POSITION 0x6ab8u           /* Player: control position */
#define PRES_TARGET 0x6dd0u             /* Player: soft target */
#define PRES_VIEW_CACHE 0xe0u           /* Component: view cache [A] at +0xe0+A*0x80, +0 the published parent */
#define PRES_VIEW_STRIDE 0x80u
#define PRES_VIEW_INDEX 0x38f4ee0u      /* the bank a thread with the MoveWorker's TLS flag reads (0x3de630, 0x3d91e0): the render walks */
#define PRES_MAIN_INDEX 0x38f4eb4u      /* the bank every other thread reads (0x979f0e): X4's own readers inside the scope, the HUD's */
#define PRES_OCCUPIED 0xb05e30u         /* GetPlayerOccupiedShipID */
#define PRES_CONTROLLED 0x1e9bd0u       /* GetPlayerControlledShipID */
#define PRES_ZONE 0x1a4f90u             /* GetPlayerZoneID */
#define PRES_ANCESTOR 0x3d91e0u         /* the view-cache ancestor lookup (component, out, class, 0: from the published parent) */
#define PRES_STUB_OFFSET 0x40u          /* on the slots' near page, after the d95180 jump */
#define PRES_CAMERA 0x3d0u              /* Universe: the local camera */
#define PRES_MODE 0x868u
#define PRES_ID 8u                      /* Component: its runtime ID */
#define PRES_CHAIN_MAX 24u
/* The sound (header of the sound, below the entry points). */
#define PRES_SOUND_ENTRY 0x2caf290u     /* XSfx::Base, XVbiHandler vtable 0x2caf288 slot 1 (VBI slot 9) */
#define PRES_SOUND_MOVE 0x14ea490u      /* XSfx::Base::Move */
#define PRES_SOUND_MOVE_SIZE 0x1eeu
#define PRES_SOUND_MOVE_SHA "c47bb5a417ee26077e37ee59df3b27a93b6f37307cc80b9b9c794f4555ee653a"
#define PRES_LISTENER 0x70bc10u         /* U::Player (player, 0): the sound manager's listener room from the camera anchor's */
#define PRES_LISTENER_SIZE 0x2bau
#define PRES_LISTENER_SHA "617a4a7ddd2ad40af350cf847dfa5b094249460e2bc2428b819ff98f85845720"
#define PRES_SOUND_MANAGER 0x6cf15b8u   /* the sound manager; +0x88 the listener's room (a component's +0x30) */
#define PRES_LISTENER_ROOM 0x88u
#define PSND_STOP_REASON 221u           /* player sound: this, the sound's source, lockstep_pilot_death.h's computer */
static int pres_sound,pres_sound_installed;
static void (*pres_sound_original)(void *);                  /* X4's update (pres_use_image), or a test's */
static void (*pres_listener_fn)(void *,unsigned char);       /* X4's 0x70bc10, or a test's */
typedef struct {
    void (*ui_move)(void *);
    void (*refill)(void *);
    void (*anchors)(void *);
    uint64_t (*occupied)(void);
    uint64_t (*controlled)(void);
    uint64_t (*zone)(void);
    void **(*ancestor)(void *,void **,int,char);
    void (*pick)(void *);
    void (*click)(void *);
} pres_fns;
static pres_fns pres_fn;
static const struct {unsigned rva,size;const char *sha;} pres_pin_table[]={
    {PRES_UI_MOVE,206,"87a23ed23ef4be0fdf493a983342282860054eb3eb14f2388e134abc88762261"},
    {PRES_REFILL,221,"dbcedb05d414d1b42c092b8ac98f15ad81442e524a020496c4a59bf94150c1e7"},
    {PRES_UI_ANCHORS,2105,"edf2e6fd86b091a01ce37af61a99dcf409422b2cbda5e1d7738804c54d6f9393"},
    {PRES_OCCUPIED,75,"68bd4e0d9276b98dcfab77a1cc25a1203c5de68b1411dd3a96abf6970d90e1e0"},
    {PRES_CONTROLLED,36,"86c093f6d8e6afadfd95d3f6749744d32d24febedde94f4996bdec057d192d39"},
    {PRES_ZONE,105,"e30fedfabb28e3bb89c200b429c417cce27329e4d30db7e5cbf6406dd9bd1e0f"},
    {PRES_ANCESTOR,219,"a77acbafe216c2d105ceb10605dbdcdb13f50fe7862e09ec3944a601fe38a45a"},
    {PRES_PICK,3710,"46342dd30a7245bf93a0634836f03a0d9a106fe1b5ebef736f2002223375262d"},
    {PRES_CLICK,2560,"e1d78c3b5fb4179e06eb0966678473ce0c87dd20de9a4f6a0c26e0f635673ea7"},
    {PRES_CLICK_SITE,0x1d,"441a97d8dfea6a03bdc619a84af5d8000cc9086e78676d85435abce7e6385fb5"},
    {0x75d110,0x142,"5dda1ded31999c6c7984952e2fef76d8d458d3571186e2e78740e8a3f3180c57"},   /* the seat transform (lockstep_pilot_eye.h) */
    {0xd81ec0,0x46f,"b1214c17c25e0ce8060da52b5d4a69159dfd89d74975f6bffd3092ec5f77ae8c"},   /* FirstPersonController slot 6 */
    {0x9fe55a,0x32,"e1658b86f806df2d83e4129e52a4d59715d886d7bcc7ed7b686b420a94d709b3"}}; /* the refill loop */
#define PRES_PIN_COUNT (sizeof(pres_pin_table)/sizeof(*pres_pin_table))
static const unsigned char pres_refill_bytes[5]={0xe8,0x68,0xb7,0xf7,0xff}; /* call 0x979ce0 from 0x9fe578 */
static const unsigned char pres_pick_bytes[5]={0xe8,0xfb,0x98,0xd1,0xff};   /* call 0x717ed0 from 0x9fe5d5 */
static int pres_armed,pres_active,pres_paused,pres_ui_installed,pres_refill_installed,pres_pick_installed,pres_pins_checked,pres_hud,pres_anchor_moved;
static int pres_room_attention_saved,pres_room_view_attention_saved,pres_diff_ready;
/* The frame thread's bank, when it is the other one (lockstep_pilot_eye.h peye_enter_main): the player's published
 * parent and the room's published attention there, as in the render bank. */
static unsigned char *pres_main_at,*pres_main_parent_saved;
static int pres_main_attention_saved;
static char pres_diff_text[3584];
static int pres_restored=1;
static unsigned pres_slot;
static const char *pres_stage="off",*pres_why="-",*pres_refused_region="-";
static char pres_fault_text[48],pres_fault_detail[640];
static unsigned char pres_saved[VPL_BLOCK_MAX],pres_player_saved[0x40],*pres_camera;
static unsigned char *pres_parent_saved,*pres_view_parent_saved,*pres_view_at,*pres_vector_saved[3];
static int pres_position_saved,pres_target_set;
/* Player settings per slot (lockstep_player_settings.h, native_gate_player_settings_per_slot): while armed, the scope also
 * holds slot k's settings in X4's player's fields (1 at the end of the enter, 0 at the start of the leave). */
static void (*pres_settings_hook)(int enter);
static uint64_t pres_local_target;
static volatile LONG64 pres_targets;
/* A walking slot on its own client's screen (native_gate_pilot_present_walk_hud). X4 draws the cockpit HUD and the radar
 * while the player controls a ship (9f9250 / 55aeb0: the first entry of the player's controlled-object vector +0x308 is
 * the ship above it). X4's own stand-up empties that; slot k's stand-up keeps P_k's vector (the seat's checks and the
 * sit-down use it, lockstep_pilot_foot_slots.h), so client k kept the cockpit HUD and the radar while its player walked
 * (a playtest report of 11 Nov: the radar stayed while the player walked). Inside the presentation scope only, a walking slot's
 * player shows an empty vector: this client's screen, never the simulation. */
static int pres_walk_hud;
static volatile LONG64 pres_walk_scopes;
/* ...and the UI's game plan. The radar, the target monitor, the crosshair and the info bars follow the UI event
 * "gameplanchange" (cockpit / firstperson) and Lua's IsFirstPerson(), which is one byte of the UI manager's presenter
 * ([[0x6cf18f8]+8]+0x6c). X4's presenter sets it and fires the event from two of its own handlers, 0xb3be20 (the player
 * sits: 0) and 0xb3be90 (the player is on foot: 1), which X4 reaches from its own player's stand-up and sit-down. A
 * slot's stand-up never runs those on its client, so with the empty vector alone the speed bar went but the radar and
 * the target monitor stayed (the user's second session, 11 Nov), and slot 1's stand-up, replayed here, switched THIS
 * client's HUD. Once per tick while presenting (pres_pass) the byte is compared with what this client's own slot does
 * (walking or not) and, when it differs, X4's own handler for that state runs inside the presentation scope. Client
 * local, UI only. The two handlers are pinned; without the pins, or after a fault, the game plan is left to X4. */
#define PRES_UI_MANAGER 0x6cf18f8u
#define PRES_PLAN_FLAG 0x6cu
#define PRES_PLAN_COCKPIT 0xb3be20u
#define PRES_PLAN_FIRSTPERSON 0xb3be90u
#define PRES_PLAN_SIZE 0x6eu
#define PRES_PLAN_COCKPIT_SHA "1510368a3704f153ee09042dbb68a600e919f3642f776895e5bf216529d59940"
#define PRES_PLAN_FIRSTPERSON_SHA "01ddbd01d92235a1efc3e5dde7db325c5602d8579a4bae116cd82b51988f0aa9"
static void (*pres_plan_fn[2])(void *);   /* [0] sits, [1] on foot (tests replace them) */
static int pres_plan_state;               /* 0 not set up, 1 usable, -1 left to X4 (pins or a fault) */
static int pres_plan_first=-1;            /* the byte before this module's first change (put back at the stop) */
static volatile LONG64 pres_plan_changes,pres_plan_faults,pres_plan_unreadable,pres_plan_kicks;
#define PRES_PLAN_KICKS 2u               /* the plan told again: at the start of presenting and once more a little later */
#define PRES_PLAN_KICK_LATER 150u
static unsigned pres_plan_kicked;static uint64_t pres_plan_kick_due;
static unsigned char pres_site_patched[5],pres_pick_patched[5];
static uint64_t pres_started_tick,pres_asked_ship,pres_asked_controlled,pres_asked_zone;
static volatile LONG64 pres_scopes,pres_ui,pres_render,pres_local,pres_refills,pres_picks,pres_clicks,pres_refused,pres_refused_player,pres_faults,pres_skipped,pres_anchor_moves,pres_rng_moved;
/* This client's own calls must not move the thread's simulation stream. */
static uint64_t *pres_rng_hold(uint64_t *word) {
    uint64_t *rng=vps_rng_word?vps_rng_word():NULL;
    *word=rng?*rng:0;return rng;
}
static void pres_rng_back(uint64_t *rng,uint64_t word) {
    if(rng && *rng!=word) {InterlockedIncrement64(&pres_rng_moved);*rng=word;}
}
#include "lockstep_pilot_eye.h"
#include "lockstep_pilot_walk_camera.h"   /* a walking slot's camera without slot 1's seated chain */
#include "lockstep_pilot_hud_zone.h"      /* the HUD's objects around this client's own slot (its zone's scanners) */
/* A walking slot's on-foot interaction state. [F, 9.00] What a walking player can use is offered through UI messages whose
 * actions test the player themselves. U::ActivateTransporterRoomAction (vtable 0x2b45a38: the terminal of a ship's
 * transporter room, "Use Transporter Room", whose menu holds "Use Spacesuit") is possible (its vtable +8, 35d140) only if
 *   - the frame pick is on its connection (the scope's pick already is slot k's);
 *   - the player's first-person controller (9fc670: the chain at Player+0x200) stands in the picked component: its floor
 *     words +0x380 component, +0x388 part, +0x390 the component's id (cd480 resolves it), +0x398 part;
 *   - the player's list of interactables (Player+0x8a698: head, count; per ship its connections with a state, 73ad30 /
 *     73aca0) has the connection in state 4;
 *   - the player stands close to the part (3db8a0: the live chain, the scope's).
 * The scope kept slot 1's controller and list on X4's player, both about slot 1's ship: on client k the terminal's action
 * was never possible, the target system counted no displayed action, held the element inactive and never made it the soft
 * target (a run of 12 Nov: the terminal was the crosshair message and targetable, no label, the click nothing; in the plain
 * game the label and one click for the menu). While this client's slot walks, the scope puts P_k's list words on the
 * player and P_k's floor words into the player's controller; slot 1's go back at the exit. They point into P_k's own state
 * (X4's UI reads it): list words that changed in a scope are counted (foot=scopes/moved) and carried nowhere.
 * The same scope puts P_k's inventory on the player (Player+0x398, the inventory's object): a station's control panel
 * (U::HackPanelAction, lockstep_ui_compslot.h hack) is possible only while the player holds the panel type's wares, and
 * the target monitor lists them against the player's inventory; with slot 1's on X4's player, client k's panel was
 * offered by what player 1 carried. A pointer that changed in the scope is counted with the list (moved). */
#define PRES_FOOT_LIST 0x8a698u         /* Player: the interactables list (head, count) */
#define PRES_FOOT_INVENTORY 0x398u      /* Player: its inventory */
#define PRES_FOOT_LIST_SIZE 16u
#define PRES_FOOT_FLOOR 0x380u          /* FirstPersonController: component, part, component id, part */
#define PRES_FOOT_FLOOR_SIZE 0x20u
static unsigned char pres_foot_list_saved[PRES_FOOT_LIST_SIZE],pres_foot_floor_saved[PRES_FOOT_FLOOR_SIZE],*pres_foot_ctl;
static unsigned pres_foot_list_at,pres_foot_floor_at;   /* 0: off; the two offsets with the real image (pres_use_image), a test's own */
static unsigned pres_foot_inventory_at;                   /* 0: off; PRES_FOOT_INVENTORY with the real image */
static unsigned char *pres_foot_inventory_saved;
static int pres_foot_open;
static volatile LONG64 pres_foot_scopes,pres_foot_moved;
static void pres_foot_enter(unsigned char *p1,const pslot_slot *s,unsigned k) {
    unsigned char *fpc=s->walking?pfslot_walking_fpc(k):NULL,*ctl=NULL;void *vt=NULL;
    pres_foot_open=0;
    if(!pres_foot_list_at || !pres_foot_floor_at) return;
    if(!fpc || !read_local(p1+PEYE_MOVEMENT,&ctl,8) || !ctl || ctl==fpc || !read_local(ctl,&vt,8) ||
       (vt!=(void*)peye_fpc_stock && vt!=(void*)&peye_vt[1])) return;
    memcpy(pres_foot_list_saved,p1+pres_foot_list_at,PRES_FOOT_LIST_SIZE);memcpy(p1+pres_foot_list_at,s->player+pres_foot_list_at,PRES_FOOT_LIST_SIZE);
    memcpy(pres_foot_floor_saved,ctl+pres_foot_floor_at,PRES_FOOT_FLOOR_SIZE);memcpy(ctl+pres_foot_floor_at,fpc+pres_foot_floor_at,PRES_FOOT_FLOOR_SIZE);
    if(pres_foot_inventory_at) {memcpy(&pres_foot_inventory_saved,p1+pres_foot_inventory_at,8);memcpy(p1+pres_foot_inventory_at,s->player+pres_foot_inventory_at,8);}
    pres_foot_ctl=ctl;pres_foot_open=1;InterlockedIncrement64(&pres_foot_scopes);
}
static void pres_foot_leave(unsigned char *p1,const pslot_slot *s) {
    if(!pres_foot_open) return;
    pres_foot_open=0;
    if(memcmp(p1+pres_foot_list_at,s->player+pres_foot_list_at,PRES_FOOT_LIST_SIZE)) InterlockedIncrement64(&pres_foot_moved);
    memcpy(p1+pres_foot_list_at,pres_foot_list_saved,PRES_FOOT_LIST_SIZE);
    if(pres_foot_inventory_at) {
        if(memcmp(p1+pres_foot_inventory_at,s->player+pres_foot_inventory_at,8)) InterlockedIncrement64(&pres_foot_moved);
        memcpy(p1+pres_foot_inventory_at,&pres_foot_inventory_saved,8);
    }
    memcpy(pres_foot_ctl+pres_foot_floor_at,pres_foot_floor_saved,PRES_FOOT_FLOOR_SIZE);
}
static int pres_pins(void) {
    if(!pres_pins_checked) {
        int ok=!memcmp(image+PRES_REFILL_CALL,pres_refill_bytes,5);
        if(!ok) pin_failed_rva=PRES_REFILL_CALL;
        if(ok && memcmp(image+PRES_PICK_CALL,pres_pick_bytes,5)) {ok=0;pin_failed_rva=PRES_PICK_CALL;}
        for(unsigned i=0;ok && i<PRES_PIN_COUNT;i++)
            if(!(ok=hash_ok(image+pres_pin_table[i].rva,pres_pin_table[i].size,pres_pin_table[i].sha))) pin_failed_rva=pres_pin_table[i].rva;
        if(ok && *(void**)(image+PRES_UI_ENTRY)!=(void*)(image+PRES_UI_MOVE)) {ok=0;pin_failed_rva=PRES_UI_ENTRY;}
        if(ok && ((void**)(image+PEYE_FPC_VTABLE))[PEYE_POSE_SLOT]!=(void*)(image+PEYE_FPC_POSE)) {ok=0;pin_failed_rva=PEYE_FPC_VTABLE;}
        pres_pins_checked=ok?1:-1;
    }
    return pres_pins_checked==1;
}
/* The game plan's two handlers, pinned (every client: client 1 uses them too, pres_plan_canon). */
static void pres_plan_bind(void) {
    pres_plan_fn[0]=(void(*)(void*))(image+PRES_PLAN_COCKPIT);pres_plan_fn[1]=(void(*)(void*))(image+PRES_PLAN_FIRSTPERSON);
    pres_plan_state=hash_ok(image+PRES_PLAN_COCKPIT,PRES_PLAN_SIZE,PRES_PLAN_COCKPIT_SHA) &&
                    hash_ok(image+PRES_PLAN_FIRSTPERSON,PRES_PLAN_SIZE,PRES_PLAN_FIRSTPERSON_SHA)?1:-1;
}
static void pres_use_image(void) {
    pres_fn.ui_move=(void(*)(void*))(image+PRES_UI_MOVE);pres_fn.refill=(void(*)(void*))(image+PRES_REFILL);
    pres_fn.anchors=(void(*)(void*))(image+PRES_UI_ANCHORS);
    pres_fn.occupied=(uint64_t(*)(void))(image+PRES_OCCUPIED);pres_fn.controlled=(uint64_t(*)(void))(image+PRES_CONTROLLED);
    pres_fn.zone=(uint64_t(*)(void))(image+PRES_ZONE);
    pres_fn.ancestor=(void**(*)(void*,void**,int,char))(image+PRES_ANCESTOR);
    pres_fn.pick=(void(*)(void*))(image+PRES_PICK);pres_fn.click=(void(*)(void*))(image+PRES_CLICK);
    pres_plan_bind();
    peye_seat=(peye_seat_fn)(image+PEYE_SEAT);peye_fpc_pose=(peye_pose_fn)(image+PEYE_FPC_POSE);peye_fpc_stock=(void**)(image+PEYE_FPC_VTABLE);
    pres_foot_list_at=PRES_FOOT_LIST;pres_foot_floor_at=PRES_FOOT_FLOOR;pres_foot_inventory_at=PRES_FOOT_INVENTORY;
    pres_sound_original=(void(*)(void*))(image+PRES_SOUND_MOVE);pres_listener_fn=(void(*)(void*,unsigned char))(image+PRES_LISTENER);
}
/* ---- the presentation scope ---- */
static int pres_enter(void);static void pres_leave(void);
static void rsch_view_enter(unsigned k);static void rsch_view_leave(unsigned k);   /* lockstep_research_owner.h */
/* lockstep_faction_present.h: the slot's own faction in the player faction's place for the length of the scope
 * (factions mode); while it is, fpres_canonical is X4's player faction as the simulation knows it. */
static int fpres_open;static void *fpres_canonical;
static int fpres_enter(unsigned slot);static void fpres_leave(void);
/* The simulation's update of a container of slot k's own faction runs with that faction as X4's player faction
 * (fpres_sim_enter): while it does, fpres_sim_slot is k and fpres_sim_saved the simulation's own player faction. */
static unsigned fpres_sim_slot;static void *fpres_sim_saved;
static unsigned char *pres_plan_presenter(unsigned char *flag) {
    unsigned char *manager=NULL,*presenter=NULL;
    if(!read_local(pnat_base+PRES_UI_MANAGER,&manager,8) || !manager || !read_local(manager+8,&presenter,8) || !presenter ||
       !read_local(presenter+PRES_PLAN_FLAG,flag,1)) return NULL;
    return presenter;
}
/* The UI's game plan becomes first person (1) or the cockpit (0) through X4's own handler; scoped: as this client's slot. */
static void pres_plan_set(int firstperson,int scoped) {
    unsigned char flag=0,*presenter;
    if(pres_plan_state!=1 || !pres_plan_fn[0] || !pres_plan_fn[1]) return;
    if(!(presenter=pres_plan_presenter(&flag))) {InterlockedIncrement64(&pres_plan_unreadable);return;}
    if((flag!=0)==(firstperson!=0)) return;
    if(scoped && !pres_enter()) return;   /* no scope now: the next tick asks again */
    if(pres_plan_first<0) pres_plan_first=flag!=0;
    int fault=0;uint64_t word=0,*rng=pres_rng_hold(&word);
    __try {pres_plan_fn[firstperson!=0](presenter);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    pres_rng_back(rng,word);
    if(scoped) pres_leave();
    if(fault) {InterlockedIncrement64(&pres_plan_faults);pres_plan_state=-1;return;}
    InterlockedIncrement64(&pres_plan_changes);
}
/* Once per tick while presenting: this client's HUD is on foot exactly while its own slot walks. */
static void pres_plan_follow(const pslot_slot *s) {
    if(pres_walk_hud) pres_plan_set(s->seated && s->walking,1);
}
/* The plan this client's slot is in, told to the UI again although the byte agrees. X4 tells its UI when its own
 * player's state changes; client k's slot is seated from the take without any change X4 saw as this client's, and the
 * UI's radar stayed off until the first stand-up and sit-down (the user's session of 11 Nov 22:25, a scripted run's
 * window the same night: speed bar and ticker, no radar; with it after a get-up and sit-down). X4's handlers tell the
 * UI only a plan that changed (b3e420), so the other plan's handler runs first and then the slot's own: the two events a
 * stand-up and sit-down give the UI, in one scope, with the byte as it was at the end. Twice: at the first pass that
 * presents and PRES_PLAN_KICK_LATER ticks after it. */
static void pres_plan_kick(const pslot_slot *s,uint64_t tick) {
    unsigned char flag=0,*presenter;const int firstperson=s->seated && s->walking;
    if(!pres_walk_hud || pres_plan_state!=1 || !pres_plan_fn[0] || !pres_plan_fn[1] || pres_plan_kicked>=PRES_PLAN_KICKS || tick<pres_plan_kick_due) return;
    if(!(presenter=pres_plan_presenter(&flag)) || (flag!=0)!=(firstperson!=0)) return;   /* the follow's business first */
    if(!pres_enter()) return;
    int fault=0;uint64_t word=0,*rng=pres_rng_hold(&word);
    __try {pres_plan_fn[firstperson==0](presenter);pres_plan_fn[firstperson!=0](presenter);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    pres_rng_back(rng,word);pres_leave();
    if(fault) {InterlockedIncrement64(&pres_plan_faults);pres_plan_state=-1;return;}
    ++pres_plan_kicked;pres_plan_kick_due=tick+PRES_PLAN_KICK_LATER;InterlockedIncrement64(&pres_plan_kicks);
}
/* The end of presenting: the byte as it was before the first change, as X4's own player. */
static void pres_plan_home(void) {
    if(pres_plan_first>=0) pres_plan_set(pres_plan_first,0);
    pres_plan_first=-1;pres_plan_kicked=0;pres_plan_kick_due=0;
}
/* Client 1's game plan. X4 sets it for its own player, but a slot 2..N's seat and suit changes run X4's sit-down
 * handler too, in that slot's scope and on this process's one UI. Two clients, 14 Nov: player 2 took its suit while
 * player 1 walked to its transporter keypad; client 1's byte read cockpit from then on, with the radar in the walking
 * view and no "Use Transporter Room" at the keypad. Every live pass on client 1 (lockstep_pilot_foot.h's reconcile):
 * X4's own player walks (the foot module says so and it holds no control position) and the byte says cockpit, or it
 * sits (both say so) and the byte says on foot: X4's own handler for the state its player is in. In between (the
 * stand-up's transition, a suit without a control position) the plan is X4's. Client local, UI only; counted with
 * the plan's changes. */
static void pres_plan_canon(void) {
    unsigned char flag=0,*presenter;int position=0,none=0;
    if(stats.slot!=1 || pres_plan_state!=1 || !pres_plan_fn[0] || !pres_plan_fn[1] || !pfoot_armed || pstk_scope_slot || !pslot_p1 || !pnat_base) return;
    if(!read_local(pslot_p1+PEYE_POSITION,&position,4) || !read_local(pnat_base+PEYE_POSITION_NONE,&none,4)) return;
    const int walks=pfoot_state==PFOOT_WALKING && position==none,sits=(pfoot_state==PFOOT_SEATED || pfoot_state==PFOOT_DOCKED) && position!=none;
    if(!walks && !sits) return;
    if(!(presenter=pres_plan_presenter(&flag))) {InterlockedIncrement64(&pres_plan_unreadable);return;}
    if((flag!=0)==walks) return;
    int fault=0;uint64_t word=0,*rng=pres_rng_hold(&word);
    __try {pres_plan_fn[walks](presenter);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    pres_rng_back(rng,word);
    if(fault) {InterlockedIncrement64(&pres_plan_faults);pres_plan_state=-1;return;}
    InterlockedIncrement64(&pres_plan_changes);
}
static void pres_stop_presenting(const char *why) {
    pres_active=0;pres_stage="failed";
    if(!strcmp(pres_why,"-")) pres_why=why;
    pres_failed=1;pnat_async_fail(why);
}
static int pres_has_seat(void) {return pslots[pres_slot].seated;}
static unsigned char *pres_room;static int pres_room_is;   /* the open scope's room, and whether it is one */
static int pres_enter(void) {
    pslot_slot *s=&pslots[pres_slot];unsigned char *universe=NULL,*now=NULL,*p1=pslot_p1;unsigned n=0,p=0,bank=0;
    if(!pres_active) return 0;
    /* The seat's ship went earlier in this frame (lockstep_pilot_death.h): the room may be gone, the next pass ends the seat. */
    if(pdeath_armed && s->seated && (!pdeath_find || pdeath_find(pdeath_map,s->runtime,4)!=s->ship)) {InterlockedIncrement64(&pres_skipped);return 0;}
    if(pres_open || pstk_scope_slot || GetCurrentThreadId()!=ui_thread || !s->seated || !p1 || !vpl_reentry_clear(pnat_base) ||
       !read_local(pnat_base+VPL_UNIVERSE,&universe,8) || !universe || !read_local(universe+VPS_SINGLETON,&now,8) || now!=p1 ||
       !read_local(pnat_base+PRES_VIEW_INDEX,&bank,4) || bank>1) {
        InterlockedIncrement64(&pres_skipped);return 0;
    }
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(r->base==VPL_BASE_PLAYER) {
            if(r->cls==VPL_SIM) {
                memcpy(pres_player_saved+p,p1+r->offset,r->size);memcpy(p1+r->offset,s->player+r->offset,r->size);p+=r->size;
            }
            continue;
        }
        if(!(PSLOT_CLASSES>>r->cls&1u)) continue;
        if(r->cls!=VPL_LOCAL) {
            memcpy(pres_saved+n,(const void*)(pnat_base+r->offset),r->size);
            memcpy((void*)(pnat_base+r->offset),s->store+n,r->size);
        }
        n+=r->size;
    }
    if(pres_target_set) memcpy(p1+PRES_TARGET,&pres_local_target,8);
    /* The player's identity: upward pointers only, its children and the room's child list are as they were. */
    memcpy(&pres_position_saved,p1+PRES_POSITION,4);memcpy(p1+PRES_POSITION,s->player+PRES_POSITION,4);
    memcpy(pres_vector_saved,p1+PRES_VECTOR,sizeof(pres_vector_saved));
    if(pres_walk_hud && s->walking) {   /* on foot: it controls nothing (the cockpit HUD and the radar go) */
        void *none[3]={&s->vector[0],&s->vector[0],&s->vector[0]};
        memcpy(p1+PRES_VECTOR,none,sizeof(none));InterlockedIncrement64(&pres_walk_scopes);
    } else memcpy(p1+PRES_VECTOR,s->player+PRES_VECTOR,sizeof(pres_vector_saved));
    /* The room the slot's player is in: its seat's, or the one it walked into (lockstep_pilot_foot_body.h); kept for
     * the scope's exit. */
    pres_room=pslot_at(s);pres_room_is=!s->roomless || s->walk_room!=NULL;
    pres_parent_saved=*(unsigned char *volatile*)(p1+PRES_PARENT);*(unsigned char *volatile*)(p1+PRES_PARENT)=pres_room;
    pres_view_at=p1+PRES_VIEW_CACHE+bank*PRES_VIEW_STRIDE;
    pres_view_parent_saved=*(unsigned char *volatile*)pres_view_at;*(unsigned char *volatile*)pres_view_at=pres_room;
    /* The room as the player's room: only when it is a room (a ship without one keeps its attention). */
    if(pres_room_is) {
        memcpy(&pres_room_attention_saved,pres_room+PRES_ATTENTION,4);
        memcpy(&pres_room_view_attention_saved,pres_room+PRES_VIEW_ATTENTION+bank*PRES_VIEW_STRIDE,4);
        *(volatile int*)(pres_room+PRES_ATTENTION)=PRES_ROOM_ATTENTION;
        *(volatile int*)(pres_room+PRES_VIEW_ATTENTION+bank*PRES_VIEW_STRIDE)=PRES_ROOM_ATTENTION;
    }
    pres_main_at=NULL;
    {   unsigned other=bank;
        if(read_local(pnat_base+PRES_MAIN_INDEX,&other,4) && other<=1 && other!=bank) {
            pres_main_at=p1+PRES_VIEW_CACHE+other*PRES_VIEW_STRIDE;
            pres_main_parent_saved=*(unsigned char *volatile*)pres_main_at;*(unsigned char *volatile*)pres_main_at=pres_room;
            if(pres_room_is) {
                memcpy(&pres_main_attention_saved,pres_room+PRES_VIEW_ATTENTION+other*PRES_VIEW_STRIDE,4);
                *(volatile int*)(pres_room+PRES_VIEW_ATTENTION+other*PRES_VIEW_STRIDE)=PRES_ROOM_ATTENTION;
            }
        }
    }
    /* The eye at slot k's seat (lockstep_pilot_eye.h): the player's own pose, live and published, with D applied. */
    peye_enter(p1,pres_view_at,bank,s,pres_parent_saved);
    if(pres_main_at) peye_enter_main(pres_main_at,(unsigned)((size_t)(pres_main_at-(p1+PRES_VIEW_CACHE))/PRES_VIEW_STRIDE));
    peye_anchor_enter(p1,pres_camera);   /* the HUD's anchor scene: no head movement of X4's own player in it */
    pwcam_enter(pres_camera,s);          /* a walking slot: the camera on its eye, as after X4's own stand-up */
    pres_foot_enter(p1,s,pres_slot);   /* a walking slot: its interactables and its floor (the terminal, header above) */
    InterlockedExchange(&pres_open,1);InterlockedIncrement64(&pres_scopes);
    mpp_active_enter(pres_slot);   /* this client's own active mission on its HUD and in its menus */
    rsch_view_enter(pres_slot);    /* and its own faction's research, when it has its own faction */
    if(pres_settings_hook) pres_settings_hook(1);
    fpres_enter(pres_slot);        /* last in, first out: and its own faction as the player faction */
    return 1;
}
static void pres_leave(void) {
    pslot_slot *s=&pslots[pres_slot];unsigned char *p1=pslot_p1;unsigned n=0,p=0;
    if(!pres_open) return;
    fpres_leave();
    if(pres_settings_hook) pres_settings_hook(0);
    rsch_view_leave(pres_slot);
    mpp_active_leave(pres_slot,0);
    pres_foot_leave(p1,s);
    pwcam_leave();
    peye_leave();
    if(pres_room_is) {
        size_t bank=(size_t)(pres_view_at-(p1+PRES_VIEW_CACHE));
        *(volatile int*)(pres_room+PRES_ATTENTION)=pres_room_attention_saved;
        *(volatile int*)(pres_room+PRES_VIEW_ATTENTION+bank)=pres_room_view_attention_saved;
    }
    if(pres_main_at) {
        const size_t other=(size_t)(pres_main_at-(p1+PRES_VIEW_CACHE));
        if(pres_room_is) *(volatile int*)(pres_room+PRES_VIEW_ATTENTION+other)=pres_main_attention_saved;
        if(*(unsigned char *volatile*)pres_main_at==pres_room) *(unsigned char *volatile*)pres_main_at=pres_main_parent_saved;   /* else the engine's own by now */
        pres_main_at=NULL;
    }
    *(unsigned char *volatile*)pres_view_at=pres_view_parent_saved;
    *(unsigned char *volatile*)(p1+PRES_PARENT)=pres_parent_saved;
    memcpy(p1+PRES_VECTOR,pres_vector_saved,sizeof(pres_vector_saved));
    memcpy(p1+PRES_POSITION,&pres_position_saved,4);
    {   uint64_t now=0,was=0;
        memcpy(&now,p1+PRES_TARGET,8);
        if(pres_target_set) was=pres_local_target;else memcpy(&was,s->player+PRES_TARGET,8);
        if(now!=was && !(uirep_families&UIREP_FAMILY_TARGET)) {pres_local_target=now;pres_target_set=1;pview_slot_target=now;InterlockedIncrement64(&pres_targets);}
        memcpy(p1+PRES_TARGET,s->player+PRES_TARGET,8);   /* not a refused write: the player's own value goes back below */
    }
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(r->base==VPL_BASE_PLAYER) {
            if(r->cls==VPL_SIM) {
                if(memcmp(p1+r->offset,s->player+r->offset,r->size)) {InterlockedIncrement64(&pres_refused_player);pres_refused_region=r->name;}
                memcpy(p1+r->offset,pres_player_saved+p,r->size);
                p+=r->size;
            }
            continue;
        }
        if(!(PSLOT_CLASSES>>r->cls&1u)) continue;
        if(r->cls!=VPL_LOCAL) {
            if(r->cls==VPL_SIM && memcmp((const void*)(pnat_base+r->offset),s->store+n,r->size)) {
                InterlockedIncrement64(&pres_refused);pres_refused_region=r->name;
            }
            memcpy((void*)(pnat_base+r->offset),pres_saved+n,r->size);
        }
        n+=r->size;
    }
    InterlockedExchange(&pres_open,0);
}
/* ---- a fault: what it was and how the code got there ---- */
static void pres_fault_describe(const EXCEPTION_POINTERS *info) {
    const CONTEXT *c=info->ContextRecord;const EXCEPTION_RECORD *e=info->ExceptionRecord;
    uintptr_t base=(uintptr_t)image,at=(uintptr_t)e->ExceptionAddress;
    int n=snprintf(pres_fault_detail,sizeof(pres_fault_detail),"addr=%llx write=%llu rcx=%llx rdx=%llx rbx=%llx rsi=%llx rdi=%llx r8=%llx r9=%llx chain=",
        e->NumberParameters>1?(unsigned long long)e->ExceptionInformation[1]:0ull,e->NumberParameters>0?(unsigned long long)e->ExceptionInformation[0]:0ull,
        (unsigned long long)c->Rcx,(unsigned long long)c->Rdx,(unsigned long long)c->Rbx,(unsigned long long)c->Rsi,(unsigned long long)c->Rdi,
        (unsigned long long)c->R8,(unsigned long long)c->R9);
    CONTEXT walk=*c;
    (void)at;
    for(unsigned frame=0;frame<PRES_CHAIN_MAX && n>0 && (size_t)n<sizeof(pres_fault_detail) && walk.Rip;frame++) {
        uintptr_t rip=(uintptr_t)walk.Rip;
        int more=rip>=base && rip-base<VPL_IMAGE_SPAN?snprintf(pres_fault_detail+n,sizeof(pres_fault_detail)-(size_t)n,"%s%llx",frame?">":"",(unsigned long long)(rip-base)):
            snprintf(pres_fault_detail+n,sizeof(pres_fault_detail)-(size_t)n,"%s@%llx",frame?">":"",(unsigned long long)rip);
        if(more<0) break;
        n+=more;
        DWORD64 module=0;PRUNTIME_FUNCTION fn=RtlLookupFunctionEntry(walk.Rip,&module,NULL);
        if(fn) {
            PVOID handler=NULL;DWORD64 frame_base=0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER,module,walk.Rip,fn,&walk,&handler,&frame_base,NULL);
        } else {
            DWORD64 back=0;
            if(!read_local((const void*)(uintptr_t)walk.Rsp,&back,8)) break;
            walk.Rip=back;walk.Rsp+=8;
        }
    }
}
static int pres_fault_filter(EXCEPTION_POINTERS *info,const char *entry) {
    if(info->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t at=(uintptr_t)info->ExceptionRecord->ExceptionAddress,base=(uintptr_t)image;
    snprintf(pres_fault_text,sizeof(pres_fault_text),"%s_fault:%llx",entry,(unsigned long long)(at>=base && at-base<VPL_IMAGE_SPAN?at-base:at));
    pres_fault_describe(info);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void pres_stop(void);
/* After a fault nothing presents any more: every byte back now, on this thread. */
static void pres_fault_stop(void) {
    InterlockedIncrement64(&pres_faults);
    pres_stop_presenting(pres_fault_text);
    pres_leave();pres_paused=0;
    pres_stop();
}
/* ---- entry points ---- */
static int ophud_enter(void);static void ophud_leave(void);static int ophud_in_ui;   /* lockstep_own_predict_hud.h: the HUD on the displayed camera */
static void pres_ui_move(void *self) {
    ophud_in_ui=1;ophud_enter();   /* before the scope, as slot 10 orders them: the poses, the camera bank, then the presentation */
    if(!pres_enter()) {
        __try {pres_fn.ui_move(self);} __finally {ophud_leave();ophud_in_ui=0;}
        return;
    }
    InterlockedIncrement64(&pres_ui);
    phz_scope_pass();   /* the HUD's zone scanners on this slot's zone, before X4's UI move (lockstep_pilot_hud_zone.h) */
    __try {pres_fn.ui_move(self);pres_leave();pres_paused=0;}
    __except(pres_fault_filter(GetExceptionInformation(),"ui")) {
        pres_fault_stop();
        /* The fault skipped X4's LeaveCriticalSection at the bottom of the entry. */
        CRITICAL_SECTION *lock=(CRITICAL_SECTION*)((unsigned char*)self+PRES_UI_LOCK);
        if(lock->OwningThread==(HANDLE)(uintptr_t)GetCurrentThreadId()) LeaveCriticalSection(lock);
    }
    ophud_leave();ophud_in_ui=0;
}
/* ---- the sound (native_gate_pilot_present_sound, a session default) ----
 * X4 places every sound against a camera (OpenAL's listener stays at the origin, sound init 0x14ed910). The per-voice
 * update 0x14f1120 -> 0x14f1740 asks the emitter (a component's +0x30, vtable slot 2: 0x3e8470) for its transform
 * relative to the listener camera 0x9f9ee0. In camera mode 0 that answer needs the Cluster (class 0xf, 0x490040 ->
 * 0x3d91e0 over the published view-cache parents) of the camera's bank component [camera+0x30+bank*0x2b0], X4's player,
 * to be the emitter's Cluster; otherwise it is 0 and the voice goes to (1e5, 1e5, 1e5) m (0x14f187c, constant
 * 0x2cc2910), 173 km away: silent. The occlusion (emitter +0x38, 0x3e89b0) takes the listener's room from the sound
 * manager (0x6cf15b8)+0x88, which U::Player 0x70bc10 sets from the camera anchor's room in Player::Move (0x713094, the
 * universe update). The sound update is VBI slot 9, XSfx::Base::Move 0x14ea490 (XSfx::Base's XVbiHandler vtable
 * 0x2caf288, slot 1 at 0x2caf290), and it was no presentation entry: on client k it ran with X4's player in slot 1's
 * cockpit and Cluster, so every sound around slot k's ship (its own guns, engines and travel drive, the others') failed
 * that test (24 Nov: player 2 in another system than player 1 heard no positional sound; the starts ran, sound=5572/5572;
 * with both players in one cluster some sound came back). Which component: the camera refill 0x9799f0 writes the
 * camera's anchor [camera+0x770] into the bank (block +0, with its parents' transforms), and the anchor is X4's player on
 * every client (the camera stays on the canonical player, header); a ship's sounds have its +0x30 interface as emitter
 * (0x7c8bc0), slot 2 0x75a190 -> 0x3e8470. The same voice update evaluates the emitter's curves: slot 5 at 0x14f1c72
 * (Engine 0x54ee80, the cockpit factor for "the player's ship" through the player's live parents; Ship 0x7dbef0 ->
 * 0x7dd1d0, the ship controller's values on the input bank), slot 6 (0x14f9450) and slot 8 (0x14f1219): outside a scope
 * those answered for slot 1's ship and slot 1's input on client k (stock-J2d: lower on client 2).
 * On client k (k = 2..N, its own slot) the vtable entry is ours (a data CAS, as the UI's entry 0x2bc07d8): the update
 * runs in the presentation scope, after X4's own 0x70bc10(X4's player, 0) in the same scope, so the listener's room is
 * slot k's cockpit room; the sound manager's room is put back after the update (the universe update sets it every
 * frame). A scope that cannot be opened (slot k not seated, another scope open): X4's update as before (plain=). Client
 * 1 never installs it. Render-only frames NULL the table entry (lockstep_render_frames.h), not the vtable. Presentation
 * only: the scope's rules hold (a SIM write is discarded and counted, refused=), the listener call holds the thread's
 * RNG word (rng_moved=). A fault in the update ends presentation as one in the UI does (162, why=sound_fault:<rva>).
 * Pinned: the update, the listener routine, the entry's value; the entry is checked at every gate tick (stop 221
 * player sound, why=sound_entry_lost, lockstep_pilot_death.h psnd_check). Status sound=on/installed/scopes/plain/
 * listener/faults. */
static volatile LONG64 pres_sound_scopes,pres_sound_plain,pres_sound_listener,pres_sound_faults;
static int pres_sound_pins(void) {
    return hash_ok(image+PRES_SOUND_MOVE,PRES_SOUND_MOVE_SIZE,PRES_SOUND_MOVE_SHA) && hash_ok(image+PRES_LISTENER,PRES_LISTENER_SIZE,PRES_LISTENER_SHA) &&
        *(void**)(image+PRES_SOUND_ENTRY)==(void*)(image+PRES_SOUND_MOVE);
}
static void pres_sound_move(void *self) {
    void (*const original)(void*)=pres_sound_original;
    if(!pres_enter()) {InterlockedIncrement64(&pres_sound_plain);original(self);return;}
    InterlockedIncrement64(&pres_sound_scopes);
    unsigned char *manager=NULL,*room=NULL;
    volatile int have=0;uint64_t word=0,*volatile rng=NULL;
    if(pres_listener_fn && read_local(pnat_base+PRES_SOUND_MANAGER,&manager,8) && manager && read_local(manager+PRES_LISTENER_ROOM,&room,8)) have=1;
    __try {
        if(have) {rng=pres_rng_hold(&word);pres_listener_fn(pslot_p1,0);pres_rng_back(rng,word);rng=NULL;InterlockedIncrement64(&pres_sound_listener);}
        original(self);
        if(have) *(unsigned char *volatile*)(manager+PRES_LISTENER_ROOM)=room;
        pres_leave();
    }
    __except(pres_fault_filter(GetExceptionInformation(),"sound")) {
        if(rng) pres_rng_back(rng,word);
        if(have) *(unsigned char *volatile*)(manager+PRES_LISTENER_ROOM)=room;
        InterlockedIncrement64(&pres_sound_faults);pres_fault_stop();
    }
}
/* The entry ours (pres_start, client k >= 2 only) and X4's again (pres_unhook). The tick check reads installed. */
static int pres_sound_install(void) {
    if(pres_sound_installed) return 1;
    if(!pres_sound_original || !pnat_swap8(pnat_base+PRES_SOUND_ENTRY,(void*)pres_sound_original,(void*)pres_sound_move,1)) return 0;
    pres_sound_installed=1;return 1;
}
static void pres_sound_uninstall(void) {
    if(!pres_sound_installed) return;
    pres_sound_installed=0;
    if(!pnat_swap8(pnat_base+PRES_SOUND_ENTRY,(void*)pres_sound_move,(void*)pres_sound_original,1)) pres_sound_installed=1;   /* not ours: the check names it */
}
static int pres_sound_intact(void) {return !pres_sound_installed || *(void*volatile*)(pnat_base+PRES_SOUND_ENTRY)==(void*)pres_sound_move;}
static int pres_sound_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity," sound=%d/%d/%lld/%lld/%lld/%lld",pres_sound,pres_sound_installed,(long long)pres_sound_scopes,(long long)pres_sound_plain,
        (long long)pres_sound_listener,(long long)pres_sound_faults);
    return n>0 && (size_t)n<capacity?n:0;
}
/* A stub on the slots' near page entered by a jump from inside an X4 function: rax..r11, the flags and xmm0..xmm5 are
 * kept; `load` (at most 16 bytes) puts the arguments into rcx/rdx (xmm2 stays as it came: a third argument of type
 * double); `fn` is called; its answer goes nowhere (PSND_OUT_NONE), into r12 (PSND_OUT_R12), into r9 (PSND_OUT_R9: the
 * saved r9 is replaced before the pops) or into rax (PSND_OUT_RAX, the same way); then X4's own `tail` (at most 16 bytes: the instructions the site's jump
 * replaced) and a jump to `resume`. at_entry: the site is a function's first instruction (rsp 8 mod 16 there), else
 * inside a body between calls (rsp 16-aligned). The bytes written; 0 when they do not fit in `room`. The sound's source
 * below and lockstep_pilot_death.h's ship computer place theirs with it. */
#define PSND_STUB_ROOM 0xc0u
enum {PSND_OUT_NONE,PSND_OUT_R12,PSND_OUT_R9,PSND_OUT_RAX};
static unsigned psnd_stub(unsigned char *p,unsigned room,const unsigned char *load,unsigned load_size,const void *fn,int out,int at_entry,
                          const unsigned char *tail,unsigned tail_size,const void *resume) {
    static const unsigned char save[]={0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53,0x9c};   /* push rax..r11, pushfq */
    static const unsigned char xsave[]={0xf3,0x0f,0x7f,0x44,0x24,0x20,0xf3,0x0f,0x7f,0x4c,0x24,0x30,0xf3,0x0f,0x7f,0x54,0x24,0x40,
        0xf3,0x0f,0x7f,0x5c,0x24,0x50,0xf3,0x0f,0x7f,0x64,0x24,0x60,0xf3,0x0f,0x7f,0x6c,0x24,0x70};        /* movdqu [rsp+0x20..0x7f], xmm0..5 */
    static const unsigned char xback[]={0xf3,0x0f,0x6f,0x44,0x24,0x20,0xf3,0x0f,0x6f,0x4c,0x24,0x30,0xf3,0x0f,0x6f,0x54,0x24,0x40,
        0xf3,0x0f,0x6f,0x5c,0x24,0x50,0xf3,0x0f,0x6f,0x64,0x24,0x60,0xf3,0x0f,0x6f,0x6c,0x24,0x70};
    static const unsigned char back[]={0x9d,0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58,0x5a,0x59,0x58};   /* popfq, pop r11..rax */
    const uint32_t frame=at_entry?0x88u:0x80u;   /* the call with rsp 16-aligned, 0x20 of shadow, xmm0..5 above it */
    unsigned n=0;
    if(load_size>16 || tail_size>16 || out<PSND_OUT_NONE || out>PSND_OUT_RAX ||
       sizeof(save)+7+sizeof(xsave)+load_size+12+8+sizeof(xback)+7+sizeof(back)+tail_size+14>room) return 0;
    memcpy(p+n,save,sizeof(save));n+=sizeof(save);
    p[n++]=0x48;p[n++]=0x81;p[n++]=0xec;memcpy(p+n,&frame,4);n+=4;     /* sub rsp, frame */
    memcpy(p+n,xsave,sizeof(xsave));n+=sizeof(xsave);
    memcpy(p+n,load,load_size);n+=load_size;
    p[n++]=0x48;p[n++]=0xb8;memcpy(p+n,&fn,8);n+=8;                       /* mov rax, fn */
    p[n++]=0xff;p[n++]=0xd0;                                               /* call rax */
    if(out==PSND_OUT_R12) {p[n++]=0x49;p[n++]=0x89;p[n++]=0xc4;}           /* mov r12, rax */
    if(out==PSND_OUT_R9 || out==PSND_OUT_RAX) {   /* mov [rsp+frame+0x18] (the saved r9) or [rsp+frame+0x38] (the saved rax), rax */
        const uint32_t at=frame+(out==PSND_OUT_R9?0x18u:0x38u);p[n++]=0x48;p[n++]=0x89;p[n++]=0x84;p[n++]=0x24;memcpy(p+n,&at,4);n+=4;
    }
    memcpy(p+n,xback,sizeof(xback));n+=sizeof(xback);
    p[n++]=0x48;p[n++]=0x81;p[n++]=0xc4;memcpy(p+n,&frame,4);n+=4;     /* add rsp, frame */
    memcpy(p+n,back,sizeof(back));n+=sizeof(back);
    if(tail_size) {memcpy(p+n,tail,tail_size);n+=tail_size;}
    n+=pview_abs_jump(p+n,resume);
    return n;
}
/* A site of `size` (5..8) bytes in one 16-byte block becomes a jump to the stub at `offset` on the slots' near page and
 * nops: the stub written first (the page writable for the write only), then the site by CAS. 1 done. */
static int psnd_site_install(unsigned site,const unsigned char *original,unsigned size,unsigned offset,const unsigned char *load,unsigned load_size,
                             const void *fn,int out,int at_entry,const unsigned char *tail,unsigned tail_size,unsigned resume,unsigned char *patched) {
    DWORD old,ignored;unsigned char stub[PSND_STUB_ROOM];
    if(!pslot_page || size<5 || size>8 || offset+PSND_STUB_ROOM>PVIEW_PAGE || memcmp(pnat_base+site,original,size) || ((site+size-1u)&~15u)!=(site&~15u)) return 0;
    const unsigned n=psnd_stub(stub,sizeof(stub),load,load_size,fn,out,at_entry,tail,tail_size,pnat_base+resume);
    if(!n || !VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return 0;
    memset(pslot_page+offset,0xcc,PSND_STUB_ROOM);memcpy(pslot_page+offset,stub,n);
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    const int32_t rel=(int32_t)((intptr_t)(pslot_page+offset)-(intptr_t)(pnat_base+site+5));
    patched[0]=0xe9;memcpy(patched+1,&rel,4);memset(patched+5,0x90,size-5);
    return pview_cas(site,original,patched,size);
}
/* ---- the sound's source (native_gate_sound_source, a session default) ----
 * A sound whose emitter is a slot player P_k (k = 2..N) or something it holds had no place either. X4 gives a sound its
 * emitter at the start (0x14e8ec0, the emitter in r9: a component's +0x30); the universe's player is the emitter of
 * every "player" sound, and inside slot k's scope that is P_k, built by the factory outside the component tree with no
 * published view cache: 0x3e8470 answers 0 for it and the voice is parked 173 km away. So on client k:
 *   - the first line of every call (SpeakNextLine 0x561330 takes the universe's player as a comm line's emitter at
 *     0x56160d and hands it to the sound start at 0x561799; the first line starts at once in its owner's frame, in slot
 *     k's scope: 24 Nov, all 37 lines started in scope 2 during a call were silent, the 45 its queue started later in no
 *     scope were heard);
 *   - every line of slot k's own ship computer (its target readout, its autopilot lines: the entity is a child of P_k);
 *   - the UI feedback sounds X4 starts on the player (the autopilot's 0xd9c920 from 0x72bf20, the time warp, quick dock,
 *     boost, target cycling) for slot k's actions, replayed in slot k's scope.
 * The sound start's entry jumps to a stub of ours (its first two instructions, mov rax,rsp; mov [rax+0x10],rbx, run
 * there before X4's code goes on at +7; nothing else in the function is touched, so the emitter X4 keeps in its home
 * slot and in rsi is ours from the start): when the emitter's object is this client's own slot's player P_k or has it
 * among its +0x70 parents, the emitter becomes X4's own player's (pslot_p1+0x30). X4's player is where the listener is:
 * on client k its sound update runs in the presentation scope, X4's player at slot k's seat (the sound above). Another
 * slot's player stays as X4 had it (no place: silent there, as before; its lines are muted there anyway). And slot 1's
 * own: X4's player (or something it holds) as the emitter of a sound started outside every scope (no slot scope, no
 * presentation scope open: slot 1's replayed actions, its UI feedback sounds) is slot 1's; with the sound update in
 * slot k's scope X4's player stands where client k's listener is, so such a sound would be heard on client k as its
 * own. While client k presents its seated slot (the sound update then runs in the scope) it gets this client's own
 * slot player's emitter instead, which has no place (parked, as slot 1's were before the sound update moved). A voice line is left to the speech module (SpeakNextLine's start, return 0x56179e: its mute
 * decides, and the lines of client k's own calls that its queue starts later, outside every scope, take X4's player
 * as theirs). Client 1 never installs it. Presentation only: the emitter goes to the sound system and nowhere else.
 * Installed with the sound's entry at presenting's start; pinned (the whole start routine, SpeakNextLine's call of it);
 * the entry checked every gate tick (221, why=source_entry_lost). Status source=on/installed/starts/moved/kept/parked. */
#define PRES_SOURCE 0x14e8ec0u           /* the sound start (manager, out, sound id, emitter, ...) */
#define PRES_SOURCE_SIZE 0x622u
#define PRES_SOURCE_SHA "6287e9144c2ac433c5697ced8b2529761e784e2fa1974244441e707c304155aa"
#define PRES_SOURCE_HEAD 7u
#define PRES_SOURCE_STUB 0xd00u          /* on the slots' near page (free from 0xaa0 to 0xeff, 24 Nov), PSND_STUB_ROOM */
#define PRES_SOURCE_SPEECH 0x56179eu     /* the return address of SpeakNextLine's sound start (call at 0x561799) */
#define PRES_SOURCE_DEPTH 16u
static const unsigned char pres_source_head[PRES_SOURCE_HEAD]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10};   /* mov rax,rsp; mov [rax+0x10],rbx */
static int pres_source,pres_source_installed;
static unsigned char pres_source_patched[PRES_SOURCE_HEAD];
static volatile LONG64 pres_source_starts,pres_source_moved,pres_source_kept,pres_source_parked;
static int pres_source_pins(void) {
    const int32_t rel=(int32_t)(PRES_SOURCE-PRES_SOURCE_SPEECH);
    return hash_ok(image+PRES_SOURCE,PRES_SOURCE_SIZE,PRES_SOURCE_SHA) && !memcmp(image+PRES_SOURCE,pres_source_head,PRES_SOURCE_HEAD) &&
        image[PRES_SOURCE_SPEECH-5u]==0xe8 && !memcmp(image+PRES_SOURCE_SPEECH-4u,&rel,4);
}
/* The slot whose player this object is or lies below (its +0x70 parents): 2..N, 1 for X4's own player; 0 none. */
static unsigned pres_source_slot(const unsigned char *object) {
    for(unsigned depth=0;object && depth<PRES_SOURCE_DEPTH;depth++) {
        for(unsigned k=2;k<=PILOT_SLOTS;k++) if(pslots[k].player && pslots[k].player==object) return k;
        if(pslot_p1 && object==pslot_p1) return 1;
        if(!read_local(object+0x70,&object,8)) return 0;
    }
    return 0;
}
/* The emitter a sound starts with (`from` its call's return address): X4's own player's for one of this client's own
 * slot player; this slot player's (no place) for slot 1's player outside every scope but a voice line; else as it came. */
static uintptr_t pres_source_rule(uintptr_t emitter,uintptr_t from) {
    InterlockedIncrement64(&pres_source_starts);
    if(!pres_source_installed || !pres_slot || !pslot_p1 || emitter<=0x30u) return emitter;
    const unsigned k=pres_source_slot((const unsigned char*)(emitter-0x30u));
    if(!k) return emitter;
    if(k==1) {
        unsigned char *const own=pslots[pres_slot].player;
        if(!own || !pres_active || !pslots[pres_slot].seated || pstk_scope_slot || pres_open || from==(uintptr_t)(pnat_base+PRES_SOURCE_SPEECH)) return emitter;
        InterlockedIncrement64(&pres_source_parked);
        return (uintptr_t)(own+0x30);
    }
    if(k!=pres_slot) {InterlockedIncrement64(&pres_source_kept);return emitter;}
    InterlockedIncrement64(&pres_source_moved);
    return (uintptr_t)(pslot_p1+0x30);
}
/* The stub's arguments: mov rcx,r9 (the emitter); mov rdx,[rsp+0xc8] (the return address: 0x40 of pushes, 0x88 of frame) */
static const unsigned char pres_source_load[11]={0x4c,0x89,0xc9,0x48,0x8b,0x94,0x24,0xc8,0,0,0};
static int pres_source_install(void) {
    if(pres_source_installed) return 1;
    if(!psnd_site_install(PRES_SOURCE,pres_source_head,PRES_SOURCE_HEAD,PRES_SOURCE_STUB,pres_source_load,sizeof(pres_source_load),(const void*)pres_source_rule,PSND_OUT_R9,1,
                          pres_source_head,PRES_SOURCE_HEAD,PRES_SOURCE+PRES_SOURCE_HEAD,pres_source_patched)) return 0;
    pres_source_installed=1;return 1;
}
static void pres_source_uninstall(void) {
    if(!pres_source_installed) return;
    pres_source_installed=0;   /* the rule answers X4's emitter from here on */
    if(!pview_cas(PRES_SOURCE,pres_source_patched,pres_source_head,PRES_SOURCE_HEAD)) pres_source_installed=1;   /* not ours: the check names it */
}
static int pres_source_intact(void) {return !pres_source_installed || !memcmp(pnat_base+PRES_SOURCE,pres_source_patched,PRES_SOURCE_HEAD);}
static int pres_source_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity," source=%d/%d/%lld/%lld/%lld/%lld",pres_source,pres_source_installed,(long long)pres_source_starts,(long long)pres_source_moved,
        (long long)pres_source_kept,(long long)pres_source_parked);
    return n>0 && (size_t)n<capacity?n:0;
}
/* ---- a sound's first play (native_gate_sound_play_scope, a session default; stop 223) ----
 * X4 decides at once whether a new voice is heard. The sound start 0x14e8ec0 makes the voice (0x14eed60, state 1..3,
 * play bit +0xe8 bit 0) and, with the play bit, plays it there (0x14e94ce: 0x14efac0, alSourcePlay at 0x14efe3d, then the
 * voice's update 0x14f1500). That update places the voice (0x14f1120 -> 0x14f1740, the emitter's +0x30 slot 2) and
 * measures it (0x14f0200: the emitter's distance against the sound's range [voice+0xf0] or [def+0xdc] km x 1000 x 1.5 +
 * 14 m); a playing voice out of range is stopped at 0x14f171e: for good unless its flags (+0xe8: 0x20, 2, bit 9, bit
 * 11) make it a virtual one that X4's sound update plays again when it comes into range. A loop (an engine, a travel
 * drive) is virtual; a one-shot (a gun's shot: the bullet's launch effect, an EffectObject whose element sound starts at
 * 0xd7994a; an impact, an explosion) is not. Most sounds start in the simulation (Weapon::Move's effects, the effect
 * creators, the ships' sound sets), outside every presentation scope, so on client k that first placement was made
 * from slot 1's view: the emitter's Cluster is not slot 1's camera's (0x3e8470 answers 0, the voice at 1e5 m), a
 * shot of slot k's own guns was out of range and stopped for good before X4's sound update, which runs in the scope
 * (native_gate_pilot_present_sound), could place it. Player 2 heard no shot of its own (24 Nov, rk8, the whole game),
 * while its engines and travel drive, loops, came back at the next update.
 * On client k (k = 2..N, presenting its seated slot, the sound update in the scope) the play at 0x14e94c7 (je over the
 * call, xor edx,edx; mov rcx,r13: 7 bytes, one block) jumps to a stub that asks pres_play_rule: a voice with an emitter
 * (+0x80) and a place (+0x60, 3D) that starts outside the presentation scope (another thread, or the frame thread with
 * no scope open) is not played there; its play bit stays, and X4's sound update, in the scope of the same or the next
 * frame, finds it in state 1..3 with the bit and plays it (0x14f1500 -> 0x14efac0), placed and measured from slot k's
 * view, at the start of its sample. Every other voice plays at once, as X4 does. Client 1 never installs it.
 * Presentation only: the voice, its handle (the start's answer, [voice+8]) and every simulation byte stay as they were;
 * the play draws nothing (the start's two draws, in 0x14eed60, are before the site and on the sound words of
 * native_gate_sound_rng). Pinned: the start (as the source), the play 0x14efac0, the voice update 0x14f1500 and the
 * update loop 0x14f6700; the site's bytes are checked every gate tick (stop 223, why=play_site_lost). Status
 * play=on/installed/starts/deferred. */
#define PRES_PLAY_SITE 0x14e94c7u        /* je 0x14e94d3; xor edx,edx; mov rcx,r13 (then call 0x14efac0 at 0x14e94ce) */
#define PRES_PLAY_SIZE 7u
#define PRES_PLAY_CALL 0x14e94ceu        /* the call of the play */
#define PRES_PLAY_AFTER 0x14e94d3u       /* mov eax,[r13+8]: the start's answer */
#define PRES_PLAY_STUB 0xc00u            /* on the slots' near page (free from 0xaa0 to 0xcff, 24 Nov), PSND_STUB_ROOM */
#define PRES_PLAY_FN 0x14efac0u
#define PRES_PLAY_FN_SIZE 0x496u
#define PRES_PLAY_FN_SHA "c1d3c08310976d425a92e08b3b3e51deb5ac00fbf2b641741fbc00cb414ffb94"
#define PRES_VOICE_UPDATE 0x14f1500u
#define PRES_VOICE_UPDATE_SIZE 0x238u
#define PRES_VOICE_UPDATE_SHA "80dba5135c4850c42fd6a757a1d1a99890fb4dbaa199bd48c27cc4368296da05"
#define PRES_VOICES 0x14f6700u           /* XSfx::Base::Move's loop over every voice: 0x14f1500 for each */
#define PRES_VOICES_SIZE 0x2bfu
#define PRES_VOICES_SHA "fabbdee3f70f2fc975b9a1be68ace8237809e8bc7ed3bcba3b9d6135134cc125"
#define PRES_VOICE_FLAGS 0xe8u
#define PRES_VOICE_PLACED 0x60u          /* byte: a 3D voice */
#define PRES_VOICE_EMITTER 0x80u
#define PSCTX_STOP_REASON 223u           /* player sound context: the play site, the own ship's sound set */
static const unsigned char pres_play_head[PRES_PLAY_SIZE]={0x74,0x0a,0x33,0xd2,0x49,0x8b,0xcd};
/* The stub's tail: test al,al; jz +0x13 (over X4's two instructions and the jump to the call, to the jump past it);
 * xor edx,edx; mov rcx,r13 (X4's own). psnd_stub then jumps to the call; the stub ends with the jump past it. */
static const unsigned char pres_play_tail[9]={0x84,0xc0,0x74,0x13,0x33,0xd2,0x49,0x8b,0xcd};
static const unsigned char pres_play_load[3]={0x4c,0x89,0xe9};   /* mov rcx,r13: the voice */
static int pres_play,pres_play_installed;
static unsigned char pres_play_patched[PRES_PLAY_SIZE];
static volatile LONG64 pres_play_starts,pres_play_deferred;
static int pres_play_pins(void) {
    return pres_source_pins() && !memcmp(image+PRES_PLAY_SITE,pres_play_head,PRES_PLAY_SIZE) && image[PRES_PLAY_CALL]==0xe8 &&
        *(const int32_t*)(image+PRES_PLAY_CALL+1)==(int32_t)(PRES_PLAY_FN-PRES_PLAY_AFTER) &&
        hash_ok(image+PRES_PLAY_FN,PRES_PLAY_FN_SIZE,PRES_PLAY_FN_SHA) && hash_ok(image+PRES_VOICE_UPDATE,PRES_VOICE_UPDATE_SIZE,PRES_VOICE_UPDATE_SHA) &&
        hash_ok(image+PRES_VOICES,PRES_VOICES_SIZE,PRES_VOICES_SHA);
}
/* 1: X4 plays the voice now; 0: not now (its play bit clear, or X4's sound update in client k's scope plays it). */
static uintptr_t pres_play_rule(const unsigned char *voice) {
    uint32_t flags=0;unsigned char placed=0;const void *emitter=NULL;
    if(!voice || !read_local(voice+PRES_VOICE_FLAGS,&flags,4) || !(flags&1u)) return 0;   /* X4's je */
    InterlockedIncrement64(&pres_play_starts);
    if(!pres_play_installed || !pres_slot || !pres_active || !pres_sound_installed || !pslots[pres_slot].seated) return 1;
    if(pres_open && GetCurrentThreadId()==ui_thread) return 1;   /* in the scope: placed from this client's view */
    if(!read_local(voice+PRES_VOICE_PLACED,&placed,1) || !placed || !read_local(voice+PRES_VOICE_EMITTER,&emitter,8) || !emitter) return 1;
    InterlockedIncrement64(&pres_play_deferred);
    return 0;
}
/* The play's stub into p (room bytes): the rule's answer in rax (nothing else changed), then X4's two instructions and
 * X4's call (`call`), or the jump past it (`after`). The bytes written; 0 when they do not fit. */
static unsigned pres_play_stub(unsigned char *p,unsigned room,const void *fn,const void *call,const void *after) {
    const unsigned n=psnd_stub(p,room,pres_play_load,sizeof(pres_play_load),fn,PSND_OUT_RAX,0,pres_play_tail,sizeof(pres_play_tail),call);
    if(!n || n+14u>room) return 0;
    return n+pview_abs_jump(p+n,after);
}
static int pres_play_install(void) {
    DWORD old,ignored;unsigned char stub[PSND_STUB_ROOM];
    if(pres_play_installed) return 1;
    if(!pslot_page || memcmp(pnat_base+PRES_PLAY_SITE,pres_play_head,PRES_PLAY_SIZE)) return 0;
    const unsigned n=pres_play_stub(stub,sizeof(stub),(const void*)pres_play_rule,pnat_base+PRES_PLAY_CALL,pnat_base+PRES_PLAY_AFTER);
    if(!n) return 0;
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return 0;
    memset(pslot_page+PRES_PLAY_STUB,0xcc,PSND_STUB_ROOM);memcpy(pslot_page+PRES_PLAY_STUB,stub,n);
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    const int32_t rel=(int32_t)((intptr_t)(pslot_page+PRES_PLAY_STUB)-(intptr_t)(pnat_base+PRES_PLAY_SITE+5));
    pres_play_patched[0]=0xe9;memcpy(pres_play_patched+1,&rel,4);pres_play_patched[5]=pres_play_patched[6]=0x90;
    if(!pview_cas(PRES_PLAY_SITE,pres_play_head,pres_play_patched,PRES_PLAY_SIZE)) return 0;
    pres_play_installed=1;return 1;
}
static void pres_play_uninstall(void) {
    if(!pres_play_installed) return;
    pres_play_installed=0;   /* the rule plays every voice at once from here on */
    if(!pview_cas(PRES_PLAY_SITE,pres_play_patched,pres_play_head,PRES_PLAY_SIZE)) pres_play_installed=1;   /* not ours: the check names it */
}
static int pres_play_intact(void) {return !pres_play_installed || !memcmp(pnat_base+PRES_PLAY_SITE,pres_play_patched,PRES_PLAY_SIZE);}
static int pres_play_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity," play=%d/%d/%lld/%lld",pres_play,pres_play_installed,(long long)pres_play_starts,(long long)pres_play_deferred);
    return n>0 && (size_t)n<capacity?n:0;
}
/* ---- the own ship's sound set (native_gate_ship_sound_context, a session default; stop 223) ----
 * X4 starts a ship's own sounds (its cockpit ambience, the ship detail group: the macro's sounds, ship+0xb68) when the
 * player's camera enters it: U::Object::SetParent's anchor test (0x6ce5cc) calls 0x97dff0 for the camera's anchor and
 * its ancestors, which posts CameraEnteredEvent / CameraLeftEvent at once, and Ship slot 366 0x7cddc0 / slot 367 0x7cde70
 * (every ship class, the space suit too) recompute the ship's attention and call slot 1143 0x7c8bc0 (start: the list
 * {[0x395d024]}, the name at 0x2f61360, inside 1) / slot 1144 0x7c8c60 (stop: the list). The test follows view point 1
 * on every client (native_gate_camera_context='view', lockstep_region_camera.h: the attention part is simulation), so
 * slot k's ship never had its own sounds on client k (inventory G09: boarding, sitting down, getting up, transport,
 * suit, ship change; a sector, zone, gate, highway, dock or undock change re-parents the ship, not into or out of it,
 * and keeps them, as in stock). On client k (k = 2..N) each pass of the presentation (the frame thread, outside every
 * scope) finds the ship slot k's player is in now (the class-0x49 ancestor of its room; none while it is not seated:
 * dead, in transit) and, when it changed, calls slot 1144 on the one it left (found again by its ID: a destroyed ship
 * stopped its own) and slot 1143 on the new one, with X4's own arguments; the end of presenting stops the last one.
 * Only the two sound slots run, never 366/367 (their attention part is the simulation's). Presentation only: the
 * starts go through 0x14e8ec0 (their draws on native_gate_sound_rng's words, slot 1143's through native_gate_ship_sound_rng
 * when armed) with the ship as emitter, and the play above plays them in the scope. Slot 1's ship keeps the sounds the
 * simulation's camera events give it on every client (a 0.05 volume loop with a 10 m range at slot 1's cockpit).
 * Pinned: both handlers (their arguments) and both slots. A fault counts and ends the module (stop 223,
 * why=context_fault). Status context=on/installed/starts/stops/faults/ship ID. */
#define PSCTX_ENTERED 0x7cddc0u
#define PSCTX_ENTERED_SIZE 0xa3u
#define PSCTX_ENTERED_SHA "9a6e057e4e9e142a9c7e53088cb51e5a1cef3167c58eb635e0eca6fa668a04d0"
#define PSCTX_LEFT 0x7cde70u
#define PSCTX_LEFT_SIZE 0x99u
#define PSCTX_LEFT_SHA "60e8389ac294f5f34dc5a21b8c5f18b59795b176df1b0979990190a1b9af8467"
#define PSCTX_STOP_FN 0x7c8c60u
#define PSCTX_STOP_FN_SIZE 0x10u
#define PSCTX_STOP_FN_SHA "a94800dfec37fb51e9229a17db7f7a92a918bb05ea3357a5340564283f8afbab"
#define PSCTX_START_SLOT 0x23b8u         /* Ship slot 1143 */
#define PSCTX_STOP_SLOT 0x23c0u          /* Ship slot 1144 */
#define PSCTX_KIND 0x395d024u            /* the list's one entry (read live: set at X4's start) */
#define PSCTX_NAME 0x2f61360u            /* the name argument */
#define PSCTX_SHIP_CLASS 0x49
#define PSCTX_DEPTH 8u
/* X4's small list of 32-bit entries (0xce7c0's layout): four inline, the capacity 4 at +0x10, the count at +0x18. */
typedef struct {uint32_t inline_[4];uint64_t capacity,count;} psctx_list;
typedef void (*psctx_start_fn)(void *,psctx_list *,void *,unsigned char);
typedef void (*psctx_stop_fn)(void *,psctx_list *);
static int psctx,psctx_installed;
static uint64_t psctx_ship_id;
static volatile LONG64 psctx_starts,psctx_stops,psctx_faults;
static const char *psctx_why="-";
static void *(*psctx_find)(uint64_t);   /* tests; NULL: X4's registry (0xced70) */
static int psctx_pins(void) {
    return hash_ok(image+PSCTX_ENTERED,PSCTX_ENTERED_SIZE,PSCTX_ENTERED_SHA) && hash_ok(image+PSCTX_LEFT,PSCTX_LEFT_SIZE,PSCTX_LEFT_SHA) &&
        hash_ok(image+PSCTX_STOP_FN,PSCTX_STOP_FN_SIZE,PSCTX_STOP_FN_SHA) && hash_ok(image+SS_TARGET,SS_BODY,SS_BODY_SHA);
}
/* The ship (class 0x49) a component lies in, itself included; NULL when none. */
static unsigned char *psctx_ship_of(unsigned char *p) {
    for(unsigned depth=0;p && depth<PSCTX_DEPTH;depth++) {
        void **vtable=NULL;unsigned char (*is_a)(void *,int)=NULL;   /* X4's answer is al */
        if(!read_local(p,&vtable,8) || !vtable || !read_local((unsigned char*)vtable+0x11e8,&is_a,8) || !is_a) return NULL;
        if(is_a(p,PSCTX_SHIP_CLASS)) return p;
        if(!read_local(p+0x70,&p,8)) return NULL;
    }
    return NULL;
}
static void *psctx_lookup(uint64_t id) {
    void *map=NULL;
    if(psctx_find) return psctx_find(id);
    return id && read_local(pnat_base+0x6cf1500,&map,8) && map?((void*(*)(void*,uint64_t,int))(pnat_base+0xced70))(map,id,4):NULL;
}
/* Slot 1143 or 1144 of a ship, as its camera handler calls it. 0 on a fault (the module ends). */
static int psctx_call(unsigned char *ship,int start) {
    psctx_list list;void **vtable=NULL;void *fn=NULL;uint32_t kind=0;
    memset(&list,0,sizeof(list));list.capacity=4;list.count=1;
    if(!read_local(pnat_base+PSCTX_KIND,&kind,4) || !read_local(ship,&vtable,8) || !vtable ||
       !read_local((unsigned char*)vtable+(start?PSCTX_START_SLOT:PSCTX_STOP_SLOT),&fn,8) || !fn) return 1;
    list.inline_[0]=kind;
    uint64_t word=0,*volatile rng=pres_rng_hold(&word);volatile int fault=0;
    __try {
        if(start) ((psctx_start_fn)fn)(ship,&list,pnat_base+PSCTX_NAME,1);else ((psctx_stop_fn)fn)(ship,&list);
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    pres_rng_back(rng,word);
    if(fault) {InterlockedIncrement64(&psctx_faults);psctx_installed=0;psctx_why="context_fault";return 0;}
    InterlockedIncrement64(start?&psctx_starts:&psctx_stops);
    return 1;
}
/* The pass: the ship slot k's player is in now, against the one it was in. */
static void psctx_follow(const pslot_slot *s) {
    if(!psctx_installed) return;
    unsigned char *ship=s->seated && s->player?psctx_ship_of(pslot_at(s)):NULL;uint64_t id=0;
    if(ship && (!read_local(ship+8,&id,8) || !id)) ship=NULL;
    if(id==psctx_ship_id) return;
    if(psctx_ship_id) {unsigned char *left=psctx_lookup(psctx_ship_id);if(left && !psctx_call(left,0)) return;}
    psctx_ship_id=0;
    if(ship && psctx_call(ship,1)) psctx_ship_id=id;
}
static void psctx_end(void) {
    if(psctx_installed && psctx_ship_id) {unsigned char *left=psctx_lookup(psctx_ship_id);if(left) (void)psctx_call(left,0);}
    psctx_ship_id=0;psctx_installed=0;
}
static int psctx_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity," context=%d/%d/%lld/%lld/%lld/%llx",psctx,psctx_installed,(long long)psctx_starts,(long long)psctx_stops,
        (long long)psctx_faults,(unsigned long long)psctx_ship_id);
    return n>0 && (size_t)n<capacity?n:0;
}
/* The tick check of this package (stop 223): the play site still ours, the context module not ended by a fault. */
static const char *psctx_check_why="-";
static int psctx_armed_any(void) {return pres_play_installed || psctx || pres_play;}
static int psctx_check(void) {
    const char *why=!pres_play_intact()?"play_site_lost":strcmp(psctx_why,"-")?psctx_why:NULL;
    if(!why) return 1;
    if(!strcmp(psctx_check_why,"-")) psctx_check_why=why;
    return 0;
}
static void pres_render_call(render_rng_fn original,void *self) {
    if(!pres_enter()) {original(self);return;}
    InterlockedIncrement64(&pres_render);
    __try {original(self);pres_leave();}
    __except(pres_fault_filter(GetExceptionInformation(),"render")) {pres_fault_stop();}
}
static int pres_local_enter(void) {
    if(!pres_enter()) return 0;
    InterlockedIncrement64(&pres_local);return 1;
}
static void pres_local_leave(int entered) {if(entered) pres_leave();}
static void pres_refill_call(void *camera) {
    if(camera!=(void*)pres_camera || !pres_enter()) {pres_fn.refill(camera);return;}
    InterlockedIncrement64(&pres_refills);
    __try {pres_fn.refill(camera);pres_leave();}
    __except(pres_fault_filter(GetExceptionInformation(),"refill")) {pres_fault_stop();}
}
/* The click's pick was asked for (header, 9fe5ed): taken over from X4's own call, in the scope that is open. */
static void pres_click_take(void *player) {
    uint32_t asked=0;
    if(!pres_fn.click || !read_local(pnat_base+PRES_CLICK_ASKED,&asked,4) || !asked) return;
    *(volatile uint32_t*)(pnat_base+PRES_CLICK_ASKED)=0;   /* as 9fe5e3 does before its call */
    InterlockedIncrement64(&pres_clicks);
    pres_fn.click(player);
}
static void pres_pick_call(void *player) {
    if(!pres_enter()) {pres_fn.pick(player);return;}
    InterlockedIncrement64(&pres_picks);
    __try {pres_fn.pick(player);pres_click_take(player);pres_leave();}
    __except(pres_fault_filter(GetExceptionInformation(),"pick")) {pres_fault_stop();}
}
/* gate.pilot_present_pause / resume: the probe's own frame runs canonical inside slot 6. The resume answers 1 when
 * it opened the scope again: the UI replay's scripted clicks run in it as a person's would, then pause once more. */
static int pres_pause(void) {
    if(!pres_open || GetCurrentThreadId()!=ui_thread) return 0;
    pres_leave();pres_paused=1;return 1;
}
static int pres_resume(void) {
    if(pres_paused && !pres_open) {pres_paused=0;return pres_enter();}
    return 0;
}
/* ---- the HUD's place ---- */
/* Under the player (camera_anchor_scene in a save, or the player itself when the ship has no room) or in
 * the room: the anchor follows the scope without a connection. */
static int pres_anchor_placed(unsigned char *universe,const pslot_slot *s) {
    for(unsigned i=0;i<2;i++) {
        unsigned char *at=NULL;
        if(!read_local(universe+PRES_UI_ANCHOR+8*i,&at,8) || !at) continue;
        for(unsigned step=0;at && step<4;step++) {
            if(!read_local(at+PRES_PARENT,&at,8)) break;
            if(at==pslot_at(s) || (at && at==pslot_p1)) return 1;
        }
    }
    return 0;
}
/* In the pass: X4 moved it back for slot 1's player. The caller is outside any scope. */
static int pres_anchor_check(void) {
    pslot_slot *s=&pslots[pres_slot];unsigned char *universe=NULL;
    if(!pres_hud || !read_local(pnat_base+VPL_UNIVERSE,&universe,8) || !universe || pres_anchor_placed(universe,s)) return 1;
    if(!pres_enter()) return 1;   /* the next pass */
    int fault=0;uint64_t word=0,*rng=pres_rng_hold(&word);
    __try {pres_fn.anchors(universe);InterlockedIncrement64(&pres_anchor_moves);pres_anchor_moved=1;}
    __except(pres_fault_filter(GetExceptionInformation(),"anchors")) {fault=1;InterlockedIncrement64(&pres_faults);}
    pres_rng_back(rng,word);
    pres_leave();
    return !fault;
}
/* ---- start and stop (frame thread, pass) ---- */
static int pres_fail(const char *why) {pres_stage="failed";if(!strcmp(pres_why,"-")) pres_why=why;pres_failed=1;return 0;}
static int pres_entry_swap(void *expected,void *value) {
    return pnat_swap8(pnat_base+PRES_UI_ENTRY,expected,value,1);
}
/* A call in UniverseFrameUpdate re-pointed through the slots' near page. */
static int pres_site_install(unsigned site,unsigned target,unsigned stub_offset,void *wrapper,unsigned char patched[5]) {
    int32_t rel=(int32_t)((intptr_t)(pnat_base+target)-(intptr_t)(pnat_base+site+5));
    unsigned char original[5]={0xe8,0,0,0,0};memcpy(original+1,&rel,4);
    if(memcmp(pnat_base+site,original,5) || !pslot_page) return 0;
    DWORD old,ignored;unsigned char *stub=pslot_page+stub_offset;
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return 0;
    pview_abs_jump(stub,wrapper);
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    rel=(int32_t)((intptr_t)stub-(intptr_t)(pnat_base+site+5));
    patched[0]=0xe8;memcpy(patched+1,&rel,4);
    return pview_cas(site,original,patched,5);
}
static int pres_site_uninstall(unsigned site,unsigned target,const unsigned char patched[5]) {
    int32_t rel=(int32_t)((intptr_t)(pnat_base+target)-(intptr_t)(pnat_base+site+5));
    unsigned char original[5]={0xe8,0,0,0,0};memcpy(original+1,&rel,4);
    return pview_cas(site,patched,original,5);
}
static int pres_refill_install(void) {
    if(!pres_site_install(PRES_REFILL_CALL,PRES_REFILL,PRES_STUB_OFFSET,(void*)pres_refill_call,pres_site_patched)) return 0;
    pres_refill_installed=1;return 1;
}
static void pres_refill_uninstall(void) {
    if(pres_refill_installed && pres_site_uninstall(PRES_REFILL_CALL,PRES_REFILL,pres_site_patched)) pres_refill_installed=0;
}
static int pres_pick_install(void) {
    if(!pres_site_install(PRES_PICK_CALL,PRES_PICK,PRES_PICK_STUB,(void*)pres_pick_call,pres_pick_patched)) return 0;
    pres_pick_installed=1;return 1;
}
static void pres_pick_uninstall(void) {
    if(pres_pick_installed && pres_site_uninstall(PRES_PICK_CALL,PRES_PICK,pres_pick_patched)) pres_pick_installed=0;
}
/* What a player's presence set on a ship and its room: the words of slot 1's and slot k's where one is
 * zero or both are small and differ (pointers and floats that merely differ say nothing). */
static int pres_diff_words(char *out,size_t capacity,int n,const char *name,const unsigned char *a,const unsigned char *b,unsigned size) {
    static unsigned char left[PRES_DIFF_SHIP],right[PRES_DIFF_SHIP];
    if(size>PRES_DIFF_SHIP || !read_local(a,left,size) || !read_local(b,right,size)) return n;
    for(unsigned at=0;at+8<=size && n>0 && (size_t)n+40<capacity;at+=8) {
        uint64_t x,y;memcpy(&x,left+at,8);memcpy(&y,right+at,8);
        if(x==y || !((!x)!=(!y) || (x<0x10000 && y<0x10000))) continue;
        int more=snprintf(out+n,capacity-(size_t)n," %s+%x:%llx/%llx",name,at,(unsigned long long)x,(unsigned long long)y);
        if(more<0) break;
        n+=more;
    }
    return n;
}
static void pres_diff_build(const pslot_slot *s) {
    unsigned char *ship1=pilot_slots[0].object,*room1=NULL;
    int n=snprintf(pres_diff_text,sizeof(pres_diff_text),"slot=%u",pres_slot);
    if(!ship1 || !read_local(ship1+PSLOT_ROOM,&room1,8)) return;
    n=pres_diff_words(pres_diff_text,sizeof(pres_diff_text),n,"ship",ship1,s->ship,PRES_DIFF_SHIP);
    if(room1 && !s->roomless) pres_diff_words(pres_diff_text,sizeof(pres_diff_text),n,"room",room1,s->room,PRES_DIFF_ROOM);
    pres_diff_ready=1;
}
/* The UI entry and the re-pointed calls back (compare-and-swap, from any thread once no scope is open). */
static void pres_unhook(void) {
    pres_refill_uninstall();pres_pick_uninstall();pres_sound_uninstall();pres_source_uninstall();pres_play_uninstall();
    psctx_installed=0;psctx_ship_id=0;   /* pres_stop ended the own ship's set on the frame thread first */
    if(pres_ui_installed && pres_entry_swap((void*)pres_ui_move,(void*)pres_fn.ui_move)) pres_ui_installed=0;
    pres_restored=!pres_ui_installed && !pres_refill_installed && !pres_pick_installed && !pres_sound_installed && !pres_source_installed && !pres_play_installed;
}
static int pres_start(uint64_t tick) {
    pslot_slot *s=&pslots[pres_slot];unsigned char *universe=NULL;uint64_t want=0;
    if(!s->seated || !s->player) return pres_fail("slot_not_seated");
    if(!read_local(pnat_base+VPL_UNIVERSE,&universe,8) || !universe || !read_local(universe+PRES_CAMERA,&pres_camera,8) || !pres_camera) return pres_fail("no_camera");
    if(!read_local(s->ship+PRES_ID,&want,8) || !want) return pres_fail("ship_id");
    pres_restored=0;
    if(!pres_entry_swap((void*)pres_fn.ui_move,(void*)pres_ui_move)) return pres_fail("ui_entry");
    pres_ui_installed=1;
    if(!pres_refill_install()) return pres_fail("refill_site");
    if(!pres_pick_install()) return pres_fail("pick_site");
    if(pres_sound && !pres_sound_install()) return pres_fail("sound_entry");     /* the sound update in the scope (the sound, above) */
    if(pres_source && !pres_source_install()) return pres_fail("sound_source");  /* this slot's own sounds at X4's player (the sound's source) */
    if(pres_play && !pres_play_install()) return pres_fail("sound_play");        /* a voice started outside the scope plays in it (a sound's first play) */
    psctx_installed=psctx;psctx_ship_id=0;psctx_why="-";                          /* the own ship's sound set (followed by pres_pass) */
    pres_diff_build(s);
    peye_update(s);
    pres_active=1;
    if(!pres_enter()) {pres_active=0;return pres_fail("start_scope");}
    int fault=0;uint64_t word=0,*rng=pres_rng_hold(&word);void *up=NULL;
    __try {
        /* X4 itself says whose ship this is now, before any UI runs in the scope. */
        pres_asked_ship=pres_fn.occupied();pres_asked_controlled=pres_fn.controlled();pres_asked_zone=pres_fn.zone();
        if(!s->roomless) {
            pres_fn.ancestor(pslot_p1,&up,PSLOT_ROOM_CLASS,0);
            if(up) InterlockedDecrement((volatile LONG*)((unsigned char*)up+0x40));   /* the lookup's reference */
        }
        if(pres_hud && pres_asked_ship==want && !pres_anchor_placed(universe,s)) {pres_fn.anchors(universe);InterlockedIncrement64(&pres_anchor_moves);pres_anchor_moved=1;}
    }
    __except(pres_fault_filter(GetExceptionInformation(),"start")) {fault=1;InterlockedIncrement64(&pres_faults);}
    pres_rng_back(rng,word);
    pres_leave();
    if(fault) {pres_active=0;return pres_fail(pres_fault_text);}
    if(pres_asked_ship!=want) {pres_active=0;return pres_fail("identity_occupied");}
    if(!s->roomless && up!=(void*)s->room) {pres_active=0;return pres_fail("identity_view_parent");}
    pres_started_tick=tick;pres_stage="presenting";
    return 1;
}
/* The HUD's place back with the canonical player (the end of presenting, or the slot's ship died). */
static void pres_anchor_home(void) {
    if(!pres_anchor_moved) return;
    uint64_t word=0,*rng=pres_rng_hold(&word);unsigned char *universe=NULL;
    pres_anchor_moved=0;
    __try {if(read_local(pnat_base+VPL_UNIVERSE,&universe,8) && universe) pres_fn.anchors(universe);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {InterlockedIncrement64(&pres_faults);}
    pres_rng_back(rng,word);
}
/* Everything back, as the canonical player, on the frame thread. */
static void pres_stop(void) {
    if(pres_open) pres_leave();
    psctx_end();
    pres_plan_home();
    pres_active=0;peye_on=0;
    pres_anchor_home();
    pres_camera=NULL;pres_target_set=0;pres_local_target=0;pview_slot_target=0;
    pres_unhook();
    if(strcmp(pres_stage,"failed")) pres_stage="stopped";
}
/* The presentation's part of a pass (pnat_pass, every tick, before the slots' part). */
/* The tick presenting starts: the window's start, or for an absent slot (hot join: this client is the joiner) its seat's
 * take+3 as for the others (seated at H-1, presented from H+3); never before its handover. A rejoinable slot (roadmap 15)
 * presents from the window's start until it is dropped; dropped (this client is its rejoiner, in its catch-up) not before
 * its new handover H, then from H+3. */
static uint64_t pres_begin(void) {
    if(!pilot_slot_absent(pres_slot)) return pilot_start;
    const unsigned i=pres_slot-1;
    if((pilot_rejoin>>i&1u) && pilot_until[i]!=UINT64_MAX) return UINT64_MAX;
    const uint64_t e=(pilot_rejoin>>i&1u)?pilot_from[i]:pilot_enroll[i];
    if(e==UINT64_MAX) return UINT64_MAX;
    return e+PILOT_SUPPRESS_LEAD>pilot_start?e+PILOT_SUPPRESS_LEAD:pilot_start;
}
static int pres_pass(uint64_t tick) {
    if(!pres_armed || !pres_slot) return 1;
    if(pres_failed) return pres_fail(pres_why);
    const uint64_t begin=pres_begin();
    if(begin<=pilot_stop && tick==begin) return pres_start(tick);
    if(tick>begin && tick<=pilot_stop) {
        if(pres_open) return pres_fail("scope_left_open");
        if(*(void*volatile*)(pnat_base+PRES_UI_ENTRY)!=(void*)pres_ui_move) return pres_fail("ui_entry_lost");
        if(memcmp(pnat_base+PRES_REFILL_CALL,pres_site_patched,5)) return pres_fail("refill_site_lost");
        if(memcmp(pnat_base+PRES_PICK_CALL,pres_pick_patched,5)) return pres_fail("pick_site_lost");
        peye_update(&pslots[pres_slot]);
        if(!pres_anchor_check()) return pres_fail(pres_fault_text);
        pres_plan_follow(&pslots[pres_slot]);pres_plan_kick(&pslots[pres_slot],tick);
        psctx_follow(&pslots[pres_slot]);
        return 1;
    }
    if(tick==pilot_stop+1) pres_stop();
    return 1;
}
/* Arming (Lua, before the first tick, after gate.pilot_stock): client 1 presents slot 1 as X4 always does. */
static int pres_arm_using(int hud) {
    if(pres_armed) return pres_hud==hud;
    pres_hud=hud;
    if(!pnat_armed || pnat_mode!=PNAT_MODE_STOCK) return pstk_arm_refuse("present_without_stock");
    pres_slot=stats.slot>=2 && (pstk_slot_mask>>(stats.slot-1)&1u)?stats.slot:0;
    if(stats.slot>=2 && !pres_slot) return pstk_arm_refuse("present_slot_not_stock");
    if(pres_slot && !pslot_armed) return pstk_arm_refuse("present_without_slots");
    if(pres_slot && !render_rng_installed) return pstk_arm_refuse("present_without_render_split");
    if(pres_slot && (!pres_fn.ui_move || !pres_fn.refill || !pres_fn.anchors || !pres_fn.occupied || !pres_fn.controlled || !pres_fn.zone || !pres_fn.ancestor || !pres_fn.pick || !pres_fn.click))
        return pstk_arm_refuse("present_functions");
    peye_reset();pwcam_reset();
    pres_failed=0;pres_active=0;pres_stage=pres_slot?"armed":"own_player";pres_why="-";pres_refused_region="-";pres_fault_detail[0]=0;pres_restored=1;pres_anchor_moved=0;
    pres_armed=1;return 1;
}
static int pres_arm(int hud) {
    if(!pres_armed && stats.issued) return pstk_arm_refuse("present_after_first_tick");
    if(!pres_armed && stats.slot>=2) {
        if(!pres_pins()) {snprintf(pstk_arm_why,sizeof(pstk_arm_why),"pins_present:%x",pin_failed_rva);return 0;}
        pres_use_image();
    } else if(!pres_armed) pres_plan_bind();   /* client 1: the game plan's handlers alone (pres_plan_canon) */
    return pres_arm_using(hud);
}
static void pres_restore(void) {
    if(!pres_armed || !pres_slot) return;
    if(GetCurrentThreadId()==ui_thread && !pstk_scope_slot) {pres_stop();return;}
    /* Off the frame thread (a stop at the session's end): nothing presents from here on, the entry and
     * the call go back once the frame thread is out of its scope. The anchor stays where it is: only
     * the frame thread may connect it, and the session is over. */
    pres_active=0;
    for(unsigned i=0;i<200 && pres_open;i++) Sleep(1);
    if(pres_open) {if(strcmp(pres_stage,"failed")) pres_stage="left_open";return;}
    pres_unhook();
    if(strcmp(pres_stage,"failed")) pres_stage=pres_anchor_moved?"stopped_anchor_left":"stopped";
}
/* gate.pilot_present_diff: the text once, nil before and after. */
static const char *pres_diff_take(void) {
    if(!pres_diff_ready) return NULL;
    pres_diff_ready=0;return pres_diff_text;
}
static int pres_status(char *out,size_t capacity) {
    int mode=-1;unsigned char *universe=NULL,*camera=NULL;
    if(read_local(pnat_base+VPL_UNIVERSE,&universe,8) && universe && read_local(universe+PRES_CAMERA,&camera,8) && camera) read_local(camera+PRES_MODE,&mode,4);
    int n=snprintf(out,capacity," present=%s present_why=%s present_slot=%u scopes=%lld ui=%lld render=%lld local=%lld refills=%lld picks=%lld skipped=%lld "
        "refused=%lld/%lld/%s targets=%lld/%llx faults=%lld camera_mode=%d asks=%llx/%llx/%llx hud=%d/%lld rng_moved=%lld walk_hud=%d/%lld plan=%d/%lld/%lld/%lld/%lld clicks=%lld foot=%lld/%lld",pres_stage,pres_why,pres_slot,(long long)pres_scopes,(long long)pres_ui,
        (long long)pres_render,(long long)pres_local,(long long)pres_refills,(long long)pres_picks,(long long)pres_skipped,(long long)pres_refused,(long long)pres_refused_player,
        pres_refused_region,(long long)pres_targets,(unsigned long long)pres_local_target,(long long)pres_faults,mode,(unsigned long long)pres_asked_ship,(unsigned long long)pres_asked_controlled,(unsigned long long)pres_asked_zone,
        pres_hud,(long long)pres_anchor_moves,(long long)pres_rng_moved,pres_walk_hud,(long long)pres_walk_scopes,
        pres_plan_state,(long long)pres_plan_changes,(long long)pres_plan_faults,(long long)pres_plan_unreadable,(long long)pres_plan_kicks,(long long)pres_clicks,(long long)pres_foot_scopes,(long long)pres_foot_moved);
    if(n<=0 || (size_t)n>=capacity) return 0;
    const int sound=pres_sound_status(out+n,capacity-(size_t)n);
    if(!sound) return 0;
    n+=sound;
    const int source=pres_source_status(out+n,capacity-(size_t)n);
    if(!source) return 0;
    n+=source;
    const int hud_zone=phz_status(out+n,capacity-(size_t)n);
    if(!hud_zone) return 0;
    n+=hud_zone;
    int eye=peye_status(out+n,capacity-(size_t)n);
    if(!eye) return 0;
    n+=eye;
    const int walk_cam=pwcam_status(out+n,capacity-(size_t)n);
    if(!walk_cam) return 0;
    n+=walk_cam;
    int more=snprintf(out+n,capacity-(size_t)n," present_restored=%d%s%s",pres_restored,pres_fault_detail[0]?" present_fault=":"",pres_fault_detail);
    return more>0 && (size_t)(n+more)<capacity?n+more:0;
}

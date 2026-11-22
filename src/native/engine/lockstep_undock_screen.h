/* An undock's UI events reach the undocking player's screen only (native_gate_undock_screen, a session default;
 * Windows gate).
 *
 * Action parity runs of 21 Nov (tools/dev/action_parity.py, scenario dockpair: both players docked at one station,
 * their dock menus read every 10 ticks): either player's undock closed the OTHER player's dock menu while that player
 * stayed docked (player 1 undocked at 3848, player 2's menu closed at 3856; with --swap player 2 at 4684, player 1's
 * at 4688), and after its own undock player 2 never got the HUD's top level menu back.
 *
 * What X4 does [static, 9.00]. Two Lua events of the UI, both raised by 0xb3af30 (events, &name):
 *   playerUndock    UndockPlayerShip's core 0x72fe40 raises it at 0x73000a, after it started move.undock on the ship.
 *                   lockstep_ui_replay.h replays that core on every client (slots 2..N inside the slot's scope), and
 *                   the UNDOCK key's record runs it there too: menu_docked.lua's close and the menu helper's
 *                   "undocking" ran on every screen.
 *   playerUndocked  the end of an undock 0x5423c0 raises it at 0x5426ee only for the ship 0x9f7a40 calls the
 *                   player's (cmp at 0x5426a6): outside a slot's scope X4's own player, slot 1. menu_toplevel.lua
 *                   opens the top level menu on it and the helper's "undocking" ends: slot 1's undock on every
 *                   screen, a slot 2..N's on none.
 * Both are UI: their handlers open and close this client's menus.
 *
 * The change: the raise's entry is ours (its first instruction on a trampoline of the slots' near page, as
 * lockstep_anim_listeners.h). A call from one of the two sites (its return address) is the undock of the slot whose
 * scope is open (pstk_scope_slot; none: slot 1) and goes through on that slot's client only; on any other client it
 * is held and counted. Every other event is X4's. The simulation is untouched: move.undock, the undock's end and all
 * their other calls run on every client as before. A slot 2..N has no playerUndocked of X4's: the UI replay core
 * raises it on that slot's own client, in its presentation scope, at the tick its ship is out of the dock
 * (native_gate_ui_replay_core.lua undock_watch). The call sites themselves are not re-pointed: 0x5426ee's five bytes
 * lie across a 16-byte block, which the gate's atomic patch does not write.
 * Fail closed: the raise's first bytes and both sites' chunks (each with its lea of the event's name) are pinned and
 * the names read; restored on stop. Status installed= why= undock=shown/held undocked=shown/held. */
#define UDSCR_RAISE 0xb3af30u            /* the UI's Lua event raise (events, &name) */
#define UDSCR_RAISE_SIZE 0x40u
#define UDSCR_RAISE_SHA "6249e9c70fc49f4d3880a4a0c54263a3010e2483a7db04ccd7ce2b9c76891824"
#define UDSCR_UNDOCK_PIN 0x72ffdcu       /* UndockPlayerShip's core: lea 'playerUndock' ... call 0xb3af30 */
#define UDSCR_UNDOCK_PIN_SIZE 0x33u
#define UDSCR_UNDOCK_PIN_SHA "dcf89666728935c969e2cc2edf3b62e0fb649dcc907cef0adeb54dea70a49ed1"
#define UDSCR_UNDOCK_RET 0x73000fu       /* its return address */
#define UDSCR_UNDOCK_NAME 0x2b4b620u
#define UDSCR_UNDOCKED_PIN 0x5426a1u     /* the undock's end: the player's ship? lea 'playerUndocked' ... call 0xb3af30 */
#define UDSCR_UNDOCKED_PIN_SIZE 0x52u
#define UDSCR_UNDOCKED_PIN_SHA "1214d3e2a8ada7ea1d0f31b392b61c12f8a4c183165ddf326fe189d1a78a7e7d"
#define UDSCR_UNDOCKED_RET 0x5426f3u
#define UDSCR_UNDOCKED_NAME 0x2aeca88u
#define UDSCR_STUB 0xa40u                /* on the slots' near page (free from 0x980 to 0xeff, 21 Nov) */
#define UDSCR_TRAMPOLINE 0xa60u          /* the raise's first instruction, then on at +5 */
enum {UDSCR_OTHER=0,UDSCR_UNDOCK=1,UDSCR_UNDOCKED=2,UDSCR_SITES=3};
static const unsigned char udscr_head[5]={0x48,0x89,0x5c,0x24,0x08};   /* mov [rsp+8],rbx */
typedef uint64_t (*udscr_fn)(void *events,void *name);
static unsigned char udscr_patched[5];
static int udscr_installed;
static const char *udscr_why="-";
static volatile LONG64 udscr_shown[UDSCR_SITES],udscr_held[UDSCR_SITES];
static udscr_fn udscr_original_test;     /* tests: X4's raise */
static unsigned (*udscr_scope_test)(void);
/* Which of the two sites a return address is. */
static unsigned udscr_site(uintptr_t from,uintptr_t base) {
    return from==base+UDSCR_UNDOCK_RET?UDSCR_UNDOCK:from==base+UDSCR_UNDOCKED_RET?UDSCR_UNDOCKED:UDSCR_OTHER;
}
/* 1: the event goes to this client's UI. A site's event is the undock of the slot whose scope is open (none: slot 1). */
static int udscr_pass(unsigned site,unsigned scope_slot,unsigned own_slot) {
    return site==UDSCR_OTHER || (scope_slot?scope_slot:1u)==own_slot;
}
static uint64_t udscr_raise_impl(udscr_fn original,uintptr_t from,void *events,void *name) {
    const unsigned site=udscr_site(from,(uintptr_t)pnat_base);
    if(site!=UDSCR_OTHER) {
        if(!udscr_pass(site,udscr_scope_test?udscr_scope_test():pstk_scope_slot,stats.slot)) {InterlockedIncrement64(&udscr_held[site]);return 0;}
        InterlockedIncrement64(&udscr_shown[site]);
    }
    return original(events,name);
}
static __attribute__((noinline)) uint64_t udscr_raise(void *events,void *name) {
    return udscr_raise_impl(udscr_original_test?udscr_original_test:(udscr_fn)(void*)(pslot_page+UDSCR_TRAMPOLINE),
                            (uintptr_t)__builtin_return_address(0),events,name);
}
/* The image as this module believes it: the raise, the two chunks, their calls of the raise and the names. */
static int udscr_pins(const unsigned char *base) {
    int32_t a=0,b=0;memcpy(&a,base+UDSCR_UNDOCK_RET-4u,4);memcpy(&b,base+UDSCR_UNDOCKED_RET-4u,4);
    return hash_ok(base+UDSCR_RAISE,UDSCR_RAISE_SIZE,UDSCR_RAISE_SHA) && !memcmp(base+UDSCR_RAISE,udscr_head,sizeof(udscr_head)) &&
           hash_ok(base+UDSCR_UNDOCK_PIN,UDSCR_UNDOCK_PIN_SIZE,UDSCR_UNDOCK_PIN_SHA) &&
           hash_ok(base+UDSCR_UNDOCKED_PIN,UDSCR_UNDOCKED_PIN_SIZE,UDSCR_UNDOCKED_PIN_SHA) &&
           base[UDSCR_UNDOCK_RET-5u]==0xe8 && (uint32_t)((int64_t)UDSCR_UNDOCK_RET+a)==UDSCR_RAISE &&
           base[UDSCR_UNDOCKED_RET-5u]==0xe8 && (uint32_t)((int64_t)UDSCR_UNDOCKED_RET+b)==UDSCR_RAISE &&
           !memcmp(base+UDSCR_UNDOCK_NAME,"playerUndock",13) && !memcmp(base+UDSCR_UNDOCKED_NAME,"playerUndocked",15);
}
/* gate.undock_screen(1), once the pilot modules know the game's image. 1: the entry is hooked (or was). */
static int udscr_arm(void) {
    DWORD old,ignored;int32_t rel=0;
    if(udscr_installed) return 1;
    if(!pnat_base) {udscr_why="no_image";return 0;}
    if(!pslot_page && !(pslot_page=pslot_alloc_page())) {udscr_why="page";return 0;}
    if(!udscr_pins(pnat_base)) {udscr_why="pins";return 0;}
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) {udscr_why="protect";return 0;}
    pview_abs_jump(pslot_page+UDSCR_STUB,(void*)udscr_raise);
    memcpy(pslot_page+UDSCR_TRAMPOLINE,udscr_head,sizeof(udscr_head));
    pview_abs_jump(pslot_page+UDSCR_TRAMPOLINE+sizeof(udscr_head),pnat_base+UDSCR_RAISE+sizeof(udscr_head));
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) {udscr_why="protect";return 0;}
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    rel=(int32_t)((intptr_t)(pslot_page+UDSCR_STUB)-(intptr_t)(pnat_base+UDSCR_RAISE+5));
    udscr_patched[0]=0xe9;memcpy(udscr_patched+1,&rel,4);
    if(!pview_cas(UDSCR_RAISE,udscr_head,udscr_patched,5)) {udscr_why="entry";return 0;}
    udscr_installed=1;udscr_why="-";return 1;
}
static void udscr_restore(void) {
    if(udscr_installed && pview_cas(UDSCR_RAISE,udscr_patched,udscr_head,5)) udscr_installed=0;
}
static int udscr_status(char *out,size_t capacity) {
    return snprintf(out,capacity,"installed=%d why=%s undock=%lld/%lld undocked=%lld/%lld",udscr_installed,udscr_why,
                    (long long)udscr_shown[UDSCR_UNDOCK],(long long)udscr_held[UDSCR_UNDOCK],
                    (long long)udscr_shown[UDSCR_UNDOCKED],(long long)udscr_held[UDSCR_UNDOCKED]);
}

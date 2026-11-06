/* The player's fleet rebuild timer on game time on the Linux X4 9.00 executable: the Linux twin of
 * lockstep_player_timer.h (native_gate_player_timer), whose first-call table, rebase (the first admitted call's time +
 * 15 s), arm, check, restore and status are shared. Included by gate_linux.c only.
 *
 * lx8 (Linux vs Linux, native_gate_actor_rng_rows): the player ship 0ae79c's ship scope drew once at tick 816 on client 2
 * and at tick 1032 on client 1, the class of Windows' long-L16: the timer's first deadline comes from the Player
 * constructor while loading (and the import does not restore it), so each client checks it at its own tick and the
 * re-arm's draw lands in the ship's stream on that client only; lx7's and lx8's first split (06dbd2, 07eb70, 07eb7c at
 * 1440) follows.
 *
 * Linux sites: U::Player::vfunc674 (Windows 0x7113e0) is 0x138e6a0, in slot +0x1558 (Windows +0x1510) of U::Player
 * 0x3b5f7a0 (gate_world_classes.h); the fleet rebuild deadline is Player+0x8a528 (Windows +0x8a538), checked against
 * the thread's game time at 0x13902a1 and re-armed as time + 60 + FloatState(word, 10) (0x139030e..0x139033b); the
 * constructor 0x1daf300 sets the first one (0x1daf7de..0x1daf811); the autopilot deadline +0x8a530 (Windows +0x8a540)
 * is left alone and reported. The thread's game time is fs:-0x2728 (lockstep_tls.h). Windows also requires its TLS
 * ready byte (slot0+0x10), whose Linux place is not mapped: the wrapper admits only the MoveWorker in an admitted frame,
 * where that thread's time is always synced, and requires a finite time. Pins: the Move's head, the check and re-arm
 * block, the constructor's first deadline (lx_sites player_timer_*), checked every gate tick (stop reason 142). */
#define PT_OWN_PLATFORM
#define PT_ENTRY (LX_PLAYER_MOVE_SLOT_VA-LX_IMAGE_BASE)
#define PT_MOVE (LX_PLAYER_MOVE_VA-LX_IMAGE_BASE)
#define PT_DUE 0x8a528u
#define PT_AUTOPILOT_DUE 0x8a530u
static int pt_pins(void); /* the shared arm and check call it; below */
#include "lockstep_player_timer.h"
static const lx_site *pt_site(const char *name) {
    for(unsigned i=0;i<gate_site_count;i++) if(!strcmp(gate_sites[i].name,name)) return &gate_sites[i];
    return NULL;
}
static int pt_pins(void) {
    return site_ok(pt_site("player_timer_head")) && site_ok(pt_site("player_timer_check")) && site_ok(pt_site("player_timer_init"));
}
/* base: the calling thread's TLS base (the thread pointer), NULL when unknown. */
static void pt_run(pt_fn original,void *self,unsigned char *base) {
    InterlockedIncrement64(&pt_calls);
    if(!stats.active || boot_phase!=3 || !stats.issued || GetCurrentThreadId()!=ui_thread) {original(self);return;}
    double now=0,due=0;double *time=gate_tls_time(base);
    if(!time || !read_local(time,&now,8) || !isfinite(now) || !read_local((unsigned char*)self+PT_DUE,&due,8)) {++pt_unreadable;original(self);return;}
    ++pt_admitted;
    int first=pt_first_call(self);
    if(first<0) ++pt_full;
    else if(first) {
        double value=now+PT_FIRST;
        memcpy(&pt_before,&due,8);memcpy((unsigned char*)self+PT_DUE,&value,8);memcpy(&pt_after,&value,8);due=value;
        if(!read_local((unsigned char*)self+PT_AUTOPILOT_DUE,&pt_autopilot,8)) pt_autopilot=0;
        ++pt_rebased;if(!pt_first) pt_first=stats.issued;
    }
    original(self);
    double after=0;
    if(read_local((unsigned char*)self+PT_DUE,&after,8) && memcmp(&after,&due,8)) {++pt_fires;pt_last_fire=stats.issued;memcpy(&pt_next,&after,8);}
}
static __attribute__((noinline)) void pt_move(void *self) {
    pt_run(pt_original_override?pt_original_override:(pt_fn)(void*)(image+PT_MOVE),self,gate_tls_self());
}
static int lua_player_timer(lua_State *L) {p_lua_pushnumber(L,pt_arm());return 1;}
static int lua_player_timer_status(lua_State *L) {
    char out[768];int n=accept_ui()?pt_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}

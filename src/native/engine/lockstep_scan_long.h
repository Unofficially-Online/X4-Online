/* Long-range scan for every player (native_gate_scan_long; needs the stock slots, pilot fire and the UI replay's travel
 * and missiles families; Windows gate; no stop reason of its own). Built 11 Nov, not yet run in the game.
 *
 * Why: the cadet story's chapter 2 waits for a long-range scan (event_player_changed_activity, then
 * event_long_range_scan_sent and _ping), and in a session nobody can make one. In the stock game the scan mode is an
 * activity (2; md/modes.xml sets it), and while it is on the secondary fire key charges the scan and its release sends
 * it: input state 0x19 in 0x71c640 calls 0x731a50(player) on the press (start time and a drawn duration into two
 * globals, 0x2f4b6c0 and 0x2f4b6ac, the player's +0x8a660 = 1) and 0x731bc0(player) on the release (the scan itself,
 * with the power in the player's +0x8a678). The power is computed every frame in the player's own update
 * (U::Player::vfunc674, 0x712f19): ((now - start) / duration)^2 up to the duration, 1 for 0.2 s more, then the charge is
 * lost (start = -1, power 0). In a session the fire states never reach X4's handler (the triggers ride the stock record
 * and lockstep_pilot_fire.h fires from them), the mode's toggle and activity are refused for players 2..N, and a player
 * 2..N has no per-frame update at all.
 *
 * This module does the three steps for every slot from what the session already agrees on, at the slot's replay of the
 * tick (slot 1 canonical, slot k inside its scope, every client alike):
 *   the mode    action 0x131 is replayed for slots 2..N too and the travel adapter accepts activity 2 for them when X4's
 *               own test 0x731830 says the player can scan (lockstep_pilot_stock.h, lockstep_ui_replay.h);
 *   the charge  the record's secondary trigger goes down while the slot's activity is 2 and 0x731830 agrees: the start
 *               time is this tick's game time, the duration a value between X4's two bounds ([0x6cf1538]+0x4d34 / +0x4d38)
 *               chosen by the tick and the slot (X4 draws it from the thread's generator: not the same on two clients);
 *               slot 1 writes X4's two globals and its own update computes the power, slot k's power is written here
 *               every tick by the same rule;
 *   the send    the trigger goes up: 0x731bc0 with the slot's player. Leaving the mode while charging loses the charge.
 * While a slot's mode is on, its secondary trigger launches no missiles (lockstep_pilot_fire.h asks scl_mode_on).
 * Not done: the charge's sound (0x731730, a sound handle in the player), the HUD's charge display on other clients than
 * the owner's is whatever X4 draws from the presented player. A hot join while a slot charges: the joiner starts that
 * slot uncharged (the state here is not in the join package). */
#define SCL_ACTIVITY 0x6ab4u
#define SCL_SCAN 2
#define SCL_RESOURCE 0x8a648u
#define SCL_CHARGING 0x8a660u
#define SCL_POWER 0x8a678u
#define SCL_CAN 0x731830u
#define SCL_CAN_ARG 0x38f0870u
#define SCL_SEND 0x731bc0u
#define SCL_START 0x2f4b6c0u       /* double, -1 while no charge runs */
#define SCL_DURATION 0x2f4b6acu    /* float */
#define SCL_SETTINGS 0x6cf1538u    /* +0x4d34 / +0x4d38: the duration's bounds */
#define SCL_FULL 0.2               /* seconds at full power before the charge is lost (0x2cbe750) */
typedef struct {unsigned char charging,held;double start;float duration;} scl_slot_t;
typedef struct {unsigned char (*can)(void *,void *);void (*send)(void *);} scl_fns;
static scl_slot_t scl_slots[PILOT_SLOTS];
static scl_fns scl_fn;
static int scl_pins_checked;
static uint64_t scl_starts,scl_sends,scl_lost,scl_cancelled,scl_refused,scl_no_resource,scl_last_tick;
static float scl_last_power;
static const char *scl_why="-";
static int scl_pins(void) {
    if(!scl_pins_checked) {
        const double full=SCL_FULL;double image_full=0;
        int ok=hash_ok(image+SCL_CAN,0x90u,"2eb08d5cc47cdc29ad342933f5c47ab21605ab6b31fc2e0c5d9f2248667715ae") &&
            hash_ok(image+SCL_SEND,0x93u,"56e1ad500a4beba957d79395ec95dcbaa831def1660062760d16aa367a52936a") &&
            hash_ok(image+0x731a50u,0x16du,"045954fd1375cab1b5cfcef9dd0596db36d3ec1e1a3575ad3d7da8f2475189d7") &&   /* the charge's start, as read */
            hash_ok(image+0x712f19u,0x106u,"2675997c6a8f5bc14e0209df55657a00eacb1894fbd23048de5a5ab0c50e6d7d") &&   /* the per-frame power */
            read_local(image+0x2cbe750u,&image_full,8) && image_full==full;
        scl_pins_checked=ok?1:-1;
    }
    return scl_pins_checked==1;
}
/* X4's own test for "this player can scan now" (in control of the ship it sits in, the ship's scanner). */
static int scl_may_start(unsigned char *player) {
    void *arg=NULL;
    if(!scl_armed || !player || !scl_fn.can || !read_local(pnat_base+SCL_CAN_ARG,&arg,8)) return 0;
    return scl_fn.can(player,arg)!=0;
}
static int scl_mode_on(unsigned slot) {
    const unsigned char *player=scl_armed && slot>=1 && slot<=PILOT_SLOTS?uirep_slot_player(slot):NULL;int activity=0;
    return player && read_local(player+SCL_ACTIVITY,&activity,4) && activity==SCL_SCAN;
}
/* The duration of a charge: between X4's bounds, by the tick and the slot (the same on every client). */
static float scl_duration(unsigned k,uint64_t tick) {
    unsigned char *settings=NULL;float low=0,high=0;
    if(!read_local(pnat_base+SCL_SETTINGS,&settings,8) || !settings || !read_local(settings+0x4d34,&low,4) || !read_local(settings+0x4d38,&high,4) ||
       !(low>0.0f) || !(high>=low)) return 0.0f;
    uint64_t h=0xcbf29ce484222325ULL;
    for(unsigned i=0;i<8;i++) {h^=(tick>>(8*i))&0xffu;h*=0x100000001b3ULL;}
    h^=k;h*=0x100000001b3ULL;
    h^=h>>33;h*=0xff51afd7ed558ccdULL;h^=h>>33;h*=0xc4ceb9fe1a85ec53ULL;h^=h>>33;   /* the slot reaches every bit */
    return low+(high-low)*(float)((h>>40)&0xffffffu)/16777216.0f;
}
/* One slot's step at its replay of the tick: slot 1 canonical, slot k inside its scope. */
static int scl_frame_slot(unsigned k,double now) {
    if(!scl_armed || k<1 || k>PILOT_SLOTS) return 1;
    unsigned char *player=(unsigned char*)uirep_slot_player(k);scl_slot_t *s=&scl_slots[k-1];
    if(!player) {s->charging=s->held=0;return 1;}
    int activity=0;void *resource=NULL;
    const int held=pstk_fire_tick[k-1]==stats.issued && pstk_fire2_on[k-1];
    const int mode=read_local(player+SCL_ACTIVITY,&activity,4) && activity==SCL_SCAN;
    const int pressed=held && !s->held,released=!held && s->held;
    s->held=(unsigned char)held;
    if(!s->charging) {
        if(!pressed || !mode) return 1;
        if(!read_local(player+SCL_RESOURCE,&resource,8) || !resource) {++scl_no_resource;return 1;}
        if(!scl_may_start(player)) {++scl_refused;return 1;}
        const float duration=scl_duration(k,stats.issued);
        if(!(duration>0.0f)) {++scl_refused;scl_why="duration";return 1;}
        s->charging=1;s->start=now;s->duration=duration;player[SCL_CHARGING]=1;
        if(k==1) {memcpy(pnat_base+SCL_START,&now,8);memcpy(pnat_base+SCL_DURATION,&duration,4);}
        ++scl_starts;scl_last_tick=stats.issued;return 1;
    }
    if(!mode) {   /* the mode left while charging: the charge is gone, nothing is sent */
        const double none=-1.0;const float zero=0.0f;
        s->charging=0;player[SCL_CHARGING]=0;memcpy(player+SCL_POWER,&zero,4);
        if(k==1) memcpy(pnat_base+SCL_START,&none,8);
        ++scl_cancelled;return 1;
    }
    if(k>=2) {   /* slot 1's own update computes this from the two globals */
        const double t=now-s->start;float power=0.0f;
        if(s->start>-0.9999 && t>0.0) {
            if(t<=(double)s->duration) {const float f=(float)t/s->duration;power=f*f;}
            else if(t<=(double)s->duration+SCL_FULL) power=1.0f;
            else {s->start=-1.0;++scl_lost;}
        }
        memcpy(player+SCL_POWER,&power,4);
    }
    if(released) {
        memcpy(&scl_last_power,player+SCL_POWER,4);
        s->charging=0;++scl_sends;scl_last_tick=stats.issued;
        scl_fn.send(player);   /* clears +0x8a660 itself */
    }
    return 1;
}
/* gate.scan_long(1): after the stock slots and the UI replay, before the first tick. */
static int scl_arm(void) {
    if(scl_armed) return stats.active!=0;   /* an identical re-arm after a loading-UI reload */
    if(!stats.active || stats.issued || GetCurrentThreadId()!=ui_thread) {scl_why="late_or_thread";return 0;}
    if(!pnat_armed || pnat_mode!=PNAT_MODE_STOCK || !uirep_armed || !(uirep_families&UIREP_FAMILY_TRAVEL)) {scl_why="needs_stock_and_travel";return 0;}
    if(!scl_pins()) {scl_why="pins";return 0;}
    memset(scl_slots,0,sizeof(scl_slots));
    scl_starts=scl_sends=scl_lost=scl_cancelled=scl_refused=scl_no_resource=scl_last_tick=0;scl_last_power=0.0f;
    scl_fn.can=(unsigned char(*)(void*,void*))(image+SCL_CAN);scl_fn.send=(void(*)(void*))(image+SCL_SEND);
    scl_armed=1;scl_why="-";return 1;
}
static void scl_stop(void) {scl_armed=0;memset(scl_slots,0,sizeof(scl_slots));}
static int scl_status(char *out,size_t capacity) {
    char slots[PILOT_SLOTS*8+2]="-";int k=0;
    for(unsigned i=0;i<PILOT_SLOTS && i<pilot_slot_count;i++)
        k+=snprintf(slots+k,sizeof(slots)-(size_t)k,"%s%u:%d%d%d",i?",":"",i+1,scl_mode_on(i+1),scl_slots[i].held,scl_slots[i].charging);
    int n=snprintf(out,capacity,"armed=%d slots=%s starts=%llu sends=%llu lost=%llu cancelled=%llu refused=%llu no_resource=%llu last=%llu/%.4f why=%s",
        scl_armed,slots,(unsigned long long)scl_starts,(unsigned long long)scl_sends,(unsigned long long)scl_lost,(unsigned long long)scl_cancelled,
        (unsigned long long)scl_refused,(unsigned long long)scl_no_resource,(unsigned long long)scl_last_tick,(double)scl_last_power,scl_why);
    return n>0 && (size_t)n<capacity?n:0;
}

/* Scan mode for every player (native_gate_scan_mode; needs the stock slots and the UI replay's travel family; Windows
 * gate; no stop reason of its own).
 *
 * Why: X4's scan mode is an activity of the player (3; md/modes.xml sets it when the mode's key, input action 0x130,
 * is pressed). With it on, signal leaks show and open, and the ship's deep scan and a suit's module scan are offered.
 * In a session the key was refused for everybody (lockstep_pilot_stock.h: the mode once followed a soft target that
 * was each client's own) and the activity for players 2..N (md/modes.xml listens on player.entity, which stays
 * player 1's for MD).
 *
 * With this on:
 *   the key       0x130 rides the stock record like the travel key and reaches X4's own handler (0xd86c80, its case
 *                 at 0xd86d53: stop when the activity is 3, start otherwise) at the slot's replay of the tick, slot k
 *                 inside its scope, on every client;
 *   the activity  the handler ends in the two activity functions the UI replay already answers: for player 1 X4's own
 *                 path (the signal to md/modes.xml), for players 2..N the adapter there sets the activity on the
 *                 slot's player after the script's one check (the player sits in a ship; a suit is one);
 *   the end       md/modes.xml ends a mode when its player leaves the room it was started in or the ship is gone.
 *                 Nobody does that for a slot's player, so each slot's replay of the tick ends the mode here when
 *                 the slot walks or sits in another ship than the one the mode was started in.
 * Not done: the mode's cockpit tint, overlay effect and sounds for players 2..N (md/modes.xml's actions on
 * player.entity); the ship's deep scan (input action 0x147 stays refused: its progress is computed in player 1's
 * per-frame update from the client's own camera, 0x712c8c..0x712f19). A hot join while a slot's mode is on: the
 * activity is the player's own field and comes with the world; the ship it was started in is not in the join package,
 * so a joiner ends that slot's mode at the first replayed tick only if the others do too (the ship ID is set when the
 * mode starts; 0 never matches a seated ship: the mode would end on the joiner alone). So the ID is taken from the
 * slot the first time a frame meets the mode on without one. */
#define SCM_ACTIVITY 0x6ab4u
#define SCM_SCAN 3
#define SCM_KEY_CASE 0xd86d53u
#define SCM_KEY_CASE_SIZE 0x9cu
#define SCM_KEY_CASE_SHA "bfbd767706cc68915c6d19627f429f78522c2c4f051bac1c7418cfba7169f224"
static int scm_pins_checked;
static const char *scm_why="-";
static int scm_pins(void) {
    if(!scm_pins_checked) scm_pins_checked=hash_ok(image+SCM_KEY_CASE,SCM_KEY_CASE_SIZE,SCM_KEY_CASE_SHA)?1:-1;
    return scm_pins_checked==1;
}
static int scm_mode_on(unsigned slot) {
    const unsigned char *player=scm_armed && slot>=1 && slot<=PILOT_SLOTS?uirep_slot_player(slot):NULL;int activity=0;
    return player && read_local(player+SCM_ACTIVITY,&activity,4) && activity==SCM_SCAN;
}
/* One slot's step at its replay of the tick, inside its scope (slots 2..N; player 1 has md/modes.xml). */
static int scm_frame_slot(unsigned k) {
    if(!scm_armed || k<2 || k>PILOT_SLOTS) return 1;
    pslot_slot *s=&pslots[k];int activity=0;
    if(!s->player || !read_local(s->player+SCM_ACTIVITY,&activity,4) || activity!=SCM_SCAN) {scm_ship[k]=0;return 1;}
    if(!scm_ship[k]) scm_ship[k]=s->runtime;   /* a joiner: the mode came with the world */
    if(!s->walking && s->runtime && s->runtime==scm_ship[k]) return 1;
    uirep_set_activity(s,0);scm_ship[k]=0;++scm_left;
    return 1;
}
/* gate.scan_mode(1): after the stock slots and the UI replay, before the first tick. */
static int scm_arm(void) {
    if(scm_armed) return stats.active!=0;   /* an identical re-arm after a loading-UI reload */
    if(!stats.active || stats.issued || GetCurrentThreadId()!=ui_thread) {scm_why="late_or_thread";return 0;}
    if(!pnat_armed || pnat_mode!=PNAT_MODE_STOCK || !uirep_armed || !(uirep_families&UIREP_FAMILY_TRAVEL)) {scm_why="needs_stock_and_travel";return 0;}
    if(!scm_pins()) {scm_why="pins";return 0;}
    memset(scm_ship,0,sizeof(scm_ship));
    scm_starts=scm_stops=scm_unavailable=scm_left=0;
    scm_armed=1;scm_why="-";return 1;
}
static void scm_stop(void) {scm_armed=0;memset(scm_ship,0,sizeof(scm_ship));}
static int scm_status(char *out,size_t capacity) {
    char slots[PILOT_SLOTS*4+2]="-";int k=0;
    for(unsigned i=0;i<PILOT_SLOTS && i<pilot_slot_count;i++)
        k+=snprintf(slots+k,sizeof(slots)-(size_t)k,"%s%u:%d",i?",":"",i+1,scm_mode_on(i+1));
    int n=snprintf(out,capacity,"armed=%d slots=%s starts=%llu stops=%llu left=%llu unavailable=%llu why=%s",
        scm_armed,slots,(unsigned long long)scm_starts,(unsigned long long)scm_stops,(unsigned long long)scm_left,
        (unsigned long long)scm_unavailable,scm_why);
    return n>0 && (size_t)n<capacity?n:0;
}

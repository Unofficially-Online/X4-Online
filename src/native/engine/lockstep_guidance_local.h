/* A client's guidance stays its own, DEFAULT OFF (native_gate_guidance_local = true). Windows only, part of
 * the runtime IDs' local lists (lockstep_runtime_ids.h, native_gate_runtime_ids_local): what X4 runs in the
 * simulation for this client's guidance takes its IDs from the ui list, so the next shared runtime ID stays
 * the same on every client. A failure stops with the runtime IDs' reason 130 (stage guidance_*).
 *
 * play39p-long (two people in their own cockpits, UI replay families off) [F: logs, binary, md/guidance.xml]:
 * client 2 opened the map (OPEN_MAP 0x71 at 5634) and set guidance on slot 1's ship ("Fly to: Discoverer
 * Vanguard", tick 6049). SetGuidance 0x1b8570 -> 0x72cb00 signals player.entity 'set guidance' through
 * PostEvent 0x99a3f0, dispatched at once (0x99c5b0 -> U::ObjectSignalledEvent::Dispatch 0x9cb930): inside the
 * presentation scope md.Guidance.NewTarget's instance and GuidanceMission's create_mission took ui IDs (route=ui).
 * From then on md.Guidance.GuidanceMission and its sub-cues exist on client 2 alone. Its NearComponent
 * (checkinterval 1s, MDScheduler_Update 0x1121f80) measures from player.entity, which the simulation keeps in
 * slot 1's ship: 0.8 m, true at once. After its delay of 3 s (tick 6141) its signal_cue_instantly instantiates
 * EndGuidance (Cue_RaiseCueEvent 0x111e370) and EndGuidance's signal_objects player.computer 'guidance_ended'
 * (SignalObjectsAction 0xbc39c0) instantiates md.Notifications.BettyNotifications (every player.computer signal
 * does): two shared IDs on client 2 only, the relay's digest parts from 6143 on and never meets again. Client 1's
 * own guidance (to slot 2's ship, 8050) ended the same way at 9400, and while it lasted the mission manager
 * refreshed the guidance route in the simulation every 150 ticks: 0x11461c0 -> 0xa32f80 -> the galaxy route
 * search 0x5c97e0 -> U::Junction 0x5bfff0, 8 shared IDs each time (8200, 8350, 8500). The refused TOGGLE_AUTOPILOT
 * presses (6088 on) ran nothing: the dispatcher refuses before any handler, the guidance came from the map.
 *
 * Guidance is the client's own (SetGuidance stays local, lockstep_ui_replay.h): the mission manager's active
 * mission is one per world and the HUD's "Fly to" and the map's route are this client's. Replayed, slot k's
 * guidance would replace every other client's. So what it runs in the simulation stays local; two rules, read
 * by rid_route_of only for a stack the route table calls shared:
 *  md     an MD frame of the Guidance script is open on the frame thread: a scope of lockstep_script_rng.h (a
 *         cue's actions through ExecState_Run, its event conditions, the MD timed checks, Cue_EnterWaiting and
 *         the Cue_RaiseCueEvent listeners of lockstep_script_rng_md.h) whose static cue is "Guidance.*". Covers
 *         every instance of NewTarget, UpdateTarget, EndGuidance and EndGuidanceToTarget, whoever signals them
 *         (conversations, diplomacy and the barter mission do), and whatever their actions make, as the
 *         BettyNotifications above. Where the Guidance script runs the same on every client (a mission's
 *         guidance), every client takes local IDs: the shared list does not move anywhere.
 *  route  the stack holds 0xa32fe3, 0xa32f80's call of the galaxy route search: 0xa32f80 is called only by the
 *         map (0xde46ef, three calls) and the mission manager's objective route (0x11461c0, two calls) [F: every
 *         E8/E9 in .text, no data reference]. The autopilot (0x72bf20) and the AI search routes on their own.
 * What stays as it was: the MD state itself (GuidanceMission active on one client: its cues, the mission), and
 * arrival is measured from slot 1's player in the simulation, so slot k's guidance to slot 1's ship ends on
 * its own after 3 s. A Guidance-only frame chain without a name (the cue tree unreadable) is counted
 * (unnamed=), never guessed: the barrier still shows anything left.
 * Needs native_gate_runtime_ids_local, native_gate_script_rng with its MD bit (the frames and their names) and
 * native_gate_md_timed_rng (the listener frames); refused otherwise. Every gate boundary checks that the
 * script RNG's cue slot and the MD timed sites are still ours (stage guidance_frames). */
#define GLOCAL_ROUTE_FN 0xa32f80u      /* the map's and the mission manager's guidance route */
#define GLOCAL_ROUTE_SIZE 0x68u
#define GLOCAL_ROUTE_CALL 0xa32fdeu    /* E8 -> the galaxy route search */
#define GLOCAL_ROUTE_RET 0xa32fe3u     /* its return address: what the route rule looks for in a stack */
#define GLOCAL_ROUTE_SEARCH 0x5c97e0u
#define GLOCAL_ROUTE_SHA "7ef4f74e074b8fe9fb28f891c48ef7c7bdb1f02628109bd2adada0d72920288b"
#define GLOCAL_CALLERS 5u
static const uint32_t glocal_callers[GLOCAL_CALLERS]={0xde4834u,0xde486fu,0xde48b1u,0x1148cbau,0x1148d21u};
static const char glocal_module[]="Guidance.";  /* the encoded "module." prefix of a static cue's name */
enum {GLOCAL_NONE=0,GLOCAL_MD=1,GLOCAL_ROUTE=2};
static volatile LONG glocal_on;
static int glocal_armed;
static const char *glocal_stage="off";
static volatile LONG64 glocal_md,glocal_route,glocal_unnamed;
/* An E8 at site that calls target. */
static int glocal_calls(uint32_t site,uint32_t target) {
    int32_t rel;memcpy(&rel,image+site+1u,4);
    return image[site]==0xe8 && (uint32_t)((int64_t)site+5+rel)==target;
}
static int glocal_pins(void) {
    if(!hash_ok(image+GLOCAL_ROUTE_FN,GLOCAL_ROUTE_SIZE,GLOCAL_ROUTE_SHA) || !glocal_calls(GLOCAL_ROUTE_CALL,GLOCAL_ROUTE_SEARCH) ||
       GLOCAL_ROUTE_CALL+5u!=GLOCAL_ROUTE_RET) return 0;
    for(unsigned i=0;i<GLOCAL_CALLERS;i++) if(!glocal_calls(glocal_callers[i],GLOCAL_ROUTE_FN)) return 0;
    return 1;
}
/* A scope whose static cue belongs to the Guidance script. */
static int glocal_named(const script_rng_frame *f) {
    return f->id.kind==SCRIPT_RNG_MD && !strncmp(f->id.name,glocal_module,sizeof(glocal_module)-1u);
}
/* rid_route_of, a stack the table calls shared: GLOCAL_MD, GLOCAL_ROUTE or GLOCAL_NONE. */
static unsigned glocal_route_of(const uint32_t *frames,unsigned count) {
    if(!InterlockedCompareExchange(&glocal_on,0,0)) return GLOCAL_NONE;
    for(unsigned i=0;i<count;i++) if(frames[i]==GLOCAL_ROUTE_RET) {InterlockedIncrement64(&glocal_route);return GLOCAL_ROUTE;}
    if(GetCurrentThreadId()!=ui_thread) return GLOCAL_NONE;
    int unnamed=0;
    for(const script_rng_frame *f=script_rng_current;f;f=f->parent) {
        if(glocal_named(f)) {InterlockedIncrement64(&glocal_md);return GLOCAL_MD;}
        /* A frame of the same cue as its owner carries no name of its own; any other MD frame should. */
        if(f->id.kind==SCRIPT_RNG_MD && !f->id.name[0] && f->state!=SR_SAME) unnamed=1;
    }
    if(unnamed) InterlockedIncrement64(&glocal_unnamed);
    return GLOCAL_NONE;
}
/* Right after rid_local_arm, before the load (UI thread). A rerun after a loading-UI reload is accepted. */
static int glocal_arm(void) {
    if(glocal_armed) return InterlockedCompareExchange(&glocal_on,0,0)!=0;
    if(!stats.active || rid_mode!=RID_SET || rid_sealed || GetCurrentThreadId()!=ui_thread) {glocal_stage="guidance_phase";return 0;}
    if(!InterlockedCompareExchange(&rid_local_on,0,0)) {glocal_stage="guidance_lists";return 0;}
    if(!script_rng_armed || !(script_rng_mask&SCRIPT_RNG_MD) || !script_rng_cue_installed || !md_timed_armed ||
       md_timed_sites_installed()!=MD_TIMED_SITES) {glocal_stage="guidance_frames";return 0;}
    if(!glocal_pins()) {glocal_stage="guidance_pins";return 0;}
    glocal_armed=1;glocal_stage="armed";InterlockedExchange(&glocal_on,1);return 1;
}
/* Every gate boundary: the frames the md rule reads are still the script RNG's. */
static int glocal_frame(void) {
    if(!InterlockedCompareExchange(&glocal_on,0,0)) return 1;
    if(InterlockedCompareExchange(&script_rng_active,0,0) && script_rng_owned() && md_timed_owned()) return 1;
    glocal_stage="guidance_frames";InterlockedExchange(&glocal_on,0);return 0;
}
static int glocal_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%d on=%ld stage=%s md=%lld route=%lld unnamed=%lld module=%.*s",glocal_armed,
        (long)InterlockedCompareExchange(&glocal_on,0,0),glocal_stage,(long long)glocal_md,(long long)glocal_route,(long long)glocal_unnamed,
        (int)(sizeof(glocal_module)-2u),glocal_module);
    return n>0 && (size_t)n<capacity?n:0;
}

/* Scan mode and the ship's deep scan for every player (native_gate_scan_mode; needs the stock slots and the UI
 * replay's travel family, the deep scan also its flight family; Windows gate; no stop reason of its own).
 *
 * Why: X4's scan mode is an activity of the player (3; md/modes.xml sets it when the mode's key, input action 0x130,
 * is pressed). With it on, signal leaks show and open, and the ship's deep scan and a suit's module scan are offered.
 * In a session the key was refused for everybody (lockstep_pilot_stock.h: the mode once followed a soft target that
 * was each client's own) and the activity for players 2..N (md/modes.xml listens on player.entity, which stays
 * player 1's for MD). The scan key on a target (input action 0x147) was refused too.
 *
 * The mode:
 *   the key       0x130 rides the stock record like the travel key and reaches X4's own handler (0xd86c80, its case
 *                 at 0xd86d53: stop when the activity is 3, start otherwise) at the slot's replay of the tick, slot k
 *                 inside its scope, on every client;
 *   the activity  the handler ends in the two activity functions the UI replay already answers: for player 1 X4's own
 *                 path (the signal to md/modes.xml), for players 2..N the adapter there sets the activity on the
 *                 slot's player after the script's one check (the player sits in a ship; a suit is one);
 *   the end       md/modes.xml ends a mode when its player leaves the room it was started in or the ship is gone.
 *                 Nobody does that for a slot's player, so each slot's replay of the tick ends the mode here when
 *                 the slot walks or sits in another ship than the one the mode was started in.
 *
 * The deep scan (a ship scanned for its cargo and crew; police and "find the ship that carries ..." missions wait for
 * event_scan_finished). In the stock game the client's UI offers a scan action for the target; its key handler
 * (0x357370) performs it (ScanAction::Perform 0x3581f0), which calls U::Player's start 0x735000(player, target, data,
 * force): can-scan test 0x734e50, the target into player+0x8a668, progress 0 into +0x8a678, the Scan Started events.
 * The progress is then computed in U::Player's per-frame update (0x712c27..0x712f19 of vfunc674): while the target is
 * in range and in the ship's forward cone it grows by half the frame's time (two seconds on target), two seconds off
 * target abort it (0x735330), a changed target aborts it at once, and at 1.0 the finish 0x735550(player, target,
 * level) discovers the target and posts the Scan Finished events. That update runs once per frame for player 1 and at
 * no agreed moments for a slot's player, and its "changed target" test asks for the universe's player's target.
 * Here:
 *   the start     0x147 is this client's own key again; the start it ends in is one of the UI replay's generic calls
 *                 (StartScan, lockstep_ui_replay.h): captured on the pressing client, run on every client at the
 *                 slot's replay inside its scope, with no data and no force (data is read only by the block below);
 *   the progress  X4's block is switched off by two pinned jumps (0x712c34 je -> jmp over its abort tests, 0x712c90
 *                 je -> jmp over its body) and each slot's replay of the tick does the step, slot 1 canonical, slot k
 *                 inside its scope: the target still in the world (its +0xd1) and X4's can-scan test true (asked
 *                 with the player's target field empty: it is the test for a start) -> progress
 *                 += half a tick; else two seconds of grace (the player's own +0x8a680, as X4 keeps it), then X4's
 *                 abort; at 1.0 X4's finish. The state is the player's own fields: nothing here for a join.
 * Differences to the stock game, on purpose: the can-scan test (range 1.5 times the scan range, in control, target
 * scannable) stands for the range and cone tests of 0x75bf40, so the target need not stay ahead of the ship; changing
 * the target does not abort the scan; the HUD's three-colour scan state (+0x8a688) stays at its start value.
 * Live, 17 Nov (two clients, the mission matrix's deepscan type: X4's own scan library with a ship and the explicit
 * scan): player 1 and player 2 each scanned a ship 300 m ahead in 60 ticks, the library heard the scan finish for
 * each, every row equal on both clients. The start went through gate.scan_start (the wrapper the UI's action ends
 * in); the action's own key, which needs the UI to have offered the scan, is a person's test. Three things met on
 * the way are fixed where they belong: the receipt's and the UI replay's view of a target that is out of the world
 * (lockstep_ui_replay.h, uirep_target_sweep's note).
 * Not done: the mode's cockpit tint, overlay effect and sounds for players 2..N (md/modes.xml's actions on
 * player.entity). A hot join while a slot's mode is on: the activity is the player's own field and comes with the
 * world; the ship it was started in is taken from the slot the first time a frame meets the mode on without one. */
#define SCM_ACTIVITY 0x6ab4u
#define SCM_SCAN 3
#define SCM_KEY_CASE 0xd86d53u
#define SCM_KEY_CASE_SIZE 0x9cu
#define SCM_KEY_CASE_SHA "bfbd767706cc68915c6d19627f429f78522c2c4f051bac1c7418cfba7169f224"
#define SCM_TARGET 0x8a668u      /* the scanned component, 0: no scan */
#define SCM_PROGRESS 0x8a678u    /* float 0..1 */
#define SCM_LEVEL 0x8a67cu       /* the level the finish discovers the target at */
#define SCM_GRACE 0x8a680u       /* double: the time the scan is lost at while off target, -1 on target */
#define SCM_ALIVE 0xd1u          /* component: in the world */
#define SCM_BLOCK 0x712c27u      /* X4's per-frame progress, to 0x712f19 */
#define SCM_BLOCK_SIZE 0x2f2u
#define SCM_BLOCK_SHA "8414260d8ecf083d389b0a83a07efd12ade5af9c17864142f72885884c5b9e5f"
#define SCM_SKIP_TESTS 0x712c34u /* je 0x712c8c -> jmp: no abort tests */
#define SCM_SKIP_BODY 0x712c90u  /* je 0x712f15 -> jmp: no progress, no finish */
#define SCM_CAN 0x734e50u
#define SCM_CAN_SIZE 0x1a5u
#define SCM_CAN_SHA "95ee1ae213c58b98a04e8ba2312d6b27093437a50f115ae06131592e3ce4dbf0"
#define SCM_ABORT 0x735330u
#define SCM_ABORT_SIZE 0x220u
#define SCM_ABORT_SHA "88e12c0e552ccbcd8acdc50bcbc457df113c54bb853742711569a8ca910c6694"
#define SCM_FINISH 0x735550u
#define SCM_FINISH_SIZE 0x32du
#define SCM_FINISH_SHA "72ca6825a7f910dcf7a0f24e00fb5d35fa560b4c2edfceaa8c71f24cf43c6e59"
#define SCM_GRACE_SECONDS 2.0    /* 0x2cbe920 */
#define SCM_RATE 0.5             /* 0x2cbe430: progress per second on target */
typedef struct {unsigned char (*can)(void *,void *,unsigned char);void (*abort)(void *);void (*finish)(void *,void *,int);} scm_fns;
static const unsigned char scm_tests_was[2]={0x74,0x56},scm_tests_off[2]={0xeb,0x56};
static const unsigned char scm_body_was[6]={0x0f,0x84,0x7f,0x02,0x00,0x00},scm_body_off[6]={0xe9,0x80,0x02,0x00,0x00,0x90};
static scm_fns scm_fn;
static int scm_pins_checked,scm_scan_pins_checked,scm_patched;
static uint64_t scm_scan_ticks,scm_scan_finished,scm_scan_aborted,scm_scan_lost;
static const char *scm_why="-",*scm_scan_why="-";
static int scm_pins(void) {
    if(!scm_pins_checked) scm_pins_checked=hash_ok(image+SCM_KEY_CASE,SCM_KEY_CASE_SIZE,SCM_KEY_CASE_SHA)?1:-1;
    return scm_pins_checked==1;
}
static int scm_scan_pins(void) {
    if(!scm_scan_pins_checked) {
        const double grace=SCM_GRACE_SECONDS;const float rate=(float)SCM_RATE;double image_grace=0;float image_rate=0;
        scm_scan_pins_checked=hash_ok(image+SCM_BLOCK,SCM_BLOCK_SIZE,SCM_BLOCK_SHA) && hash_ok(image+SCM_CAN,SCM_CAN_SIZE,SCM_CAN_SHA) &&
            hash_ok(image+SCM_ABORT,SCM_ABORT_SIZE,SCM_ABORT_SHA) && hash_ok(image+SCM_FINISH,SCM_FINISH_SIZE,SCM_FINISH_SHA) &&
            read_local(image+0x2cbe920u,&image_grace,8) && image_grace==grace && read_local(image+0x2cbe430u,&image_rate,4) && image_rate==rate?1:-1;
    }
    return scm_scan_pins_checked==1;
}
static int scm_mode_on(unsigned slot) {
    const unsigned char *player=scm_armed && slot>=1 && slot<=PILOT_SLOTS?uirep_slot_player(slot):NULL;int activity=0;
    return player && read_local(player+SCM_ACTIVITY,&activity,4) && activity==SCM_SCAN;
}
/* One player's scan at its slot's replay of the tick: slot 1 canonical, slot k inside its scope. */
static int scm_scan_step(unsigned char *player,double now) {
    unsigned char *target=NULL;float progress=0.0f;double grace=-1.0;int level=0;unsigned char alive=0;
    if(!scm_scan_on || !player || !read_local(player+SCM_TARGET,&target,8) || !target) return 1;
    if(!read_local(player+SCM_PROGRESS,&progress,4) || !read_local(player+SCM_GRACE,&grace,8) || !read_local(player+SCM_LEVEL,&level,4)) return 1;
    ++scm_scan_ticks;
    if(!read_local(target+SCM_ALIVE,&alive,1) || !alive) {scm_fn.abort(player);++scm_scan_lost;return 1;}
    /* X4's test is "can this scan START": it also says no for the target the universe's player already scans
     * (0x3ebaf0 compares it with that player's +0x8a668). The field is empty for the question. */
    const void *none_target=NULL;unsigned char on=0;
    memcpy(player+SCM_TARGET,&none_target,8);on=scm_fn.can(player,target,0);memcpy(player+SCM_TARGET,&target,8);
    if(on) {
        const double none=-1.0;
        progress+=(float)(gate_tick_dt()*SCM_RATE);
        memcpy(player+SCM_PROGRESS,&progress,4);memcpy(player+SCM_GRACE,&none,8);
        if(progress>=1.0f) {scm_fn.finish(player,target,level);++scm_scan_finished;}
        return 1;
    }
    if(grace<=-0.9999) {grace=now+SCM_GRACE_SECONDS;memcpy(player+SCM_GRACE,&grace,8);return 1;}
    if(now>grace) {scm_fn.abort(player);++scm_scan_aborted;}
    return 1;
}
/* One slot's step at its replay of the tick, inside its scope (slots 2..N; player 1 has md/modes.xml for the mode and
 * its scan step in lockstep_pilot_native.h). */
static int scm_frame_slot(unsigned k,double now) {
    if(!scm_armed || k<2 || k>PILOT_SLOTS) return 1;
    pslot_slot *s=&pslots[k];int activity=0;
    if(s->player) scm_scan_step(s->player,now);
    if(!s->player || !read_local(s->player+SCM_ACTIVITY,&activity,4) || activity!=SCM_SCAN) {scm_ship[k]=0;return 1;}
    if(!scm_ship[k]) scm_ship[k]=s->runtime;   /* a joiner: the mode came with the world */
    if(!s->walking && s->runtime && s->runtime==scm_ship[k]) return 1;
    uirep_set_activity(s,0);scm_ship[k]=0;++scm_left;
    return 1;
}
/* This client's player starts the scan of its soft target, through the wrapper the UI's scan action ends in. The
 * target is read from this client's own slot's player; the wrapper is handed the universe's player, as X4's UI
 * hands it (outside a scope that is player 1 on every client; the replay runs it for the capturing slot). */
static int scm_scan_script(void) {
    const unsigned char *own=uirep_slot_player(stats.slot);unsigned char *component=NULL;uint64_t target=0;
    if(!scm_scan_on || !own || !pnat_player() || !read_local(own+PRES_TARGET,&target,8) || !target || !(component=uirep_component(target))) return 0;
    return uirep_call_scan_start((uint64_t)(uintptr_t)pnat_player(),(uint64_t)(uintptr_t)component,0,0,0,0,0,0)!=0;
}
/* What X4's can-scan test 0x734e50 would find for this client's player and its soft target, part by part (a run's
 * diagnosis): control=<vfunc 0x1c50(0)> held=<controlled objects>/<the first is the player's ship> bit=<the ship's
 * scan capability> view=<TLS view>/<state index>/<the other> flags=<the target's state flags, both> scanning=<0x3ebaf0>
 * can=<the test itself, asked as for a start>. */
static int scm_scan_diag(char *out,size_t capacity) {
    unsigned char *player=(unsigned char*)(uintptr_t)uirep_slot_player(stats.slot),*component=NULL,*ship=NULL,*first=NULL,*table=NULL;uint64_t target=0;
    int n=0,control=-1,held=0,match=0,bit=-1,view=-1,can=-1;uint32_t index[2]={0,0},flags[2]={0,0};void *scanned=NULL;
    unsigned char *const tls=(unsigned char*)event_order_tls();   /* outside the guarded block (see its note) */
    if(!player || !read_local(player+PRES_TARGET,&target,8) || !target || !(component=uirep_component(target)))
        return snprintf(out,capacity,"player=%d target=%llx component=0",player!=NULL,(unsigned long long)target);
    __try {
        void **vt=*(void***)player;unsigned char **at=NULL,**end=NULL;
        control=((unsigned char(*)(void*,int))vt[0x1c50/8])(player,0)!=0;
        memcpy(&at,player+0x308,8);memcpy(&end,player+0x310,8);
        for(;at && at<end;at++) if(*at) {if(!first) first=*at;++held;}
        for(ship=*(unsigned char**)(player+0x70);ship;ship=*(unsigned char**)(ship+0x70))
            if(((unsigned char(*)(void*,int))(*(void***)ship)[0x11e8/8])(ship,0x70)) break;
        match=ship && ship==first;
        if(ship && read_local(image+0x38f0868u,&table,8) && table) {
            const unsigned char c=table[0x16c];uint64_t have=0;memcpy(&have,ship+0x450,8);
            bit=c==0xff?0:(int)((have>>c)&1u);
        }
        if(tls) memcpy(&view,tls+0x314,4);
        memcpy(&index[0],image+0x38f4eb4u,4);memcpy(&index[1],image+0x38f4ee0u,4);
        for(unsigned i=0;i<2;i++) if(index[i]<8) memcpy(&flags[i],component+((size_t)index[i]<<7)+0x11c,4);
        memcpy(&scanned,player+SCM_TARGET,8);
        if(scm_fn.can) {const void *none=NULL;memcpy(player+SCM_TARGET,&none,8);can=scm_fn.can(player,component,0)!=0;memcpy(player+SCM_TARGET,&scanned,8);}
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {n=-1;}
    if(n<0) return snprintf(out,capacity,"fault");
    return snprintf(out,capacity,"control=%d held=%d/%d bit=%d view=%d/%u/%u flags=%08x/%08x scanning=%d can=%d",control,held,match,bit,view,
        (unsigned)index[0],(unsigned)index[1],(unsigned)flags[0],(unsigned)flags[1],scanned==(void*)component,can);
}
static void scm_scan_off(void) {
    if(scm_patched&2) {if(pview_cas(SCM_SKIP_BODY,scm_body_off,scm_body_was,sizeof(scm_body_was))) scm_patched&=~2;}
    if(scm_patched&1) {if(pview_cas(SCM_SKIP_TESTS,scm_tests_off,scm_tests_was,sizeof(scm_tests_was))) scm_patched&=~1;}
    scm_scan_on=0;
}
/* The deep scan's part of the arming: without the flight family or its pins the mode alone is armed. */
static void scm_scan_arm(void) {
    scm_scan_ticks=scm_scan_finished=scm_scan_aborted=scm_scan_lost=0;
    if(!(uirep_families&UIREP_FAMILY_FLIGHT)) {scm_scan_why="needs_flight";return;}
    if(!scm_scan_pins()) {scm_scan_why="pins";return;}
    scm_fn.can=(unsigned char(*)(void*,void*,unsigned char))(image+SCM_CAN);scm_fn.abort=(void(*)(void*))(image+SCM_ABORT);
    scm_fn.finish=(void(*)(void*,void*,int))(image+SCM_FINISH);
    if(pview_cas(SCM_SKIP_TESTS,scm_tests_was,scm_tests_off,sizeof(scm_tests_off))) scm_patched|=1;
    if((scm_patched&1) && pview_cas(SCM_SKIP_BODY,scm_body_was,scm_body_off,sizeof(scm_body_off))) scm_patched|=2;
    if(scm_patched!=3) {scm_scan_off();scm_scan_why="patch";return;}
    scm_scan_on=1;scm_scan_why="-";
}
/* gate.scan_mode(1): after the stock slots and the UI replay, before the first tick. */
static int scm_arm(void) {
    if(scm_armed) return stats.active!=0;   /* an identical re-arm after a loading-UI reload */
    if(!stats.active || stats.issued || GetCurrentThreadId()!=ui_thread) {scm_why="late_or_thread";return 0;}
    if(!pnat_armed || pnat_mode!=PNAT_MODE_STOCK || !uirep_armed || !(uirep_families&UIREP_FAMILY_TRAVEL)) {scm_why="needs_stock_and_travel";return 0;}
    if(!scm_pins()) {scm_why="pins";return 0;}
    memset(scm_ship,0,sizeof(scm_ship));
    scm_starts=scm_stops=scm_unavailable=scm_left=0;
    scm_armed=1;scm_why="-";
    scm_scan_arm();
    return 1;
}
static void scm_stop(void) {scm_scan_off();scm_armed=0;memset(scm_ship,0,sizeof(scm_ship));}
static int scm_status(char *out,size_t capacity) {
    char slots[PILOT_SLOTS*24+2]="-";int k=0;
    for(unsigned i=0;i<PILOT_SLOTS && i<pilot_slot_count;i++) {
        const unsigned char *player=scm_armed?uirep_slot_player(i+1):NULL;void *target=NULL;float progress=0.0f;
        if(player) {read_local(player+SCM_TARGET,&target,8);read_local(player+SCM_PROGRESS,&progress,4);}
        k+=snprintf(slots+k,sizeof(slots)-(size_t)k,"%s%u:%d/%d/%.2f",i?",":"",i+1,scm_mode_on(i+1),target!=NULL,(double)(target?progress:0.0f));
    }
    int n=snprintf(out,capacity,"armed=%d slots=%s starts=%llu stops=%llu left=%llu unavailable=%llu why=%s scan=%d/%llu/%llu/%llu/%llu scan_why=%s",
        scm_armed,slots,(unsigned long long)scm_starts,(unsigned long long)scm_stops,(unsigned long long)scm_left,
        (unsigned long long)scm_unavailable,scm_why,scm_scan_on,(unsigned long long)scm_scan_ticks,(unsigned long long)scm_scan_finished,
        (unsigned long long)scm_scan_aborted,(unsigned long long)scm_scan_lost,scm_scan_why);
    return n>0 && (size_t)n<capacity?n:0;
}

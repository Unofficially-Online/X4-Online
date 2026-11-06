/* Render-only frames, DEFAULT OFF (native_gate_render_fps = N, stop reason 182; Windows gate,
 * protocols 4 and 5). The simulation keeps its fixed step (native_gate_tick_hz: dt = 1/hz, one
 * step per admitted frame, paced to real time); between two steps X4 draws extra frames in which
 * game time does not move and no simulation code runs. reports/LOCKSTEP-RENDER-FRAMES-20261101.txt.
 *
 * How X4 frames are built (X4 9.00, read in the binary). The main loop calls FrameTick 0xfa2d70
 * once per frame (the gate's hook sits on that call). FrameTick advances game, real and unpaused
 * time by dt only when dt > 0 (0xfa330f: comisd/jbe skips the whole block), publishes the view
 * cache index, flips the render lists, releases the MoveWorker (g_semSimStart) and draws the
 * previous frame's lists while the MoveWorker builds the next frame. With the gate's dt patch
 * (0xfa3302 nop'd) dt is the word 0x6adfc60, which only FrameTick reads (the other three
 * references write it from the movie-capture options). The MoveWorker frame (0xef4400): the
 * Gravidar publish (moved into the pathing kick by the gravidar module), the wait for
 * g_semSimStart, the pathing kick (path_release), the thread's game time, the camera view-block
 * reset, VBI slots 0..16 from g_VbiTable (a NULL entry is skipped, 0xef469b), the sound
 * accumulator (game time minus the last frame's: nothing at a zero difference), g_semSimDone,
 * then the mass traffic and region kicks (each only when its task's ready byte is set).
 *
 * A render-only frame is one more FrameTick between two steps, with:
 *   - the dt word 0.0 for that one call: no game, real or unpaused time advance (the native
 *     zero-dt path); put back to 1/hz before the call returns to the main loop;
 *   - every g_VbiTable entry except slot 10 (GameRender::Move: UI, character and camera render
 *     banks) set to NULL for the gap and put back by CAS before the next step: no universe
 *     update (3), input dispatch (4), UI and probe (6), animation (8), sound (9), jobs (11),
 *     Steam (14) or timers (1) run, so local input waits in X4's own queue for the next step's
 *     slot 4 (nothing is consumed, nothing is lost or seen twice) and the probe's frame stays
 *     once per step;
 *   - the pathing kick skipped (path_release returns at once: no pass, no join, no per-step
 *     gate work), the mass traffic and region ready bytes held at 0 so the frame's tail kicks
 *     start no pass (the stride's own mechanism, lockstep_mass_traffic.h), the Gravidar publish
 *     a no-op (gravidar module).
 * Slot 10 runs inside the render RNG split (its draws stay on the private render word), its
 * effect objects take IDs from the view lists (native_gate_runtime_ids_local) and its
 * presentation scope works per call (lockstep_pilot_present.h). The main thread draws and
 * presents as in any frame; X4's own frame limit (frameratelimit/frameratetarget) still holds.
 * Required (else no render-only frame, ready=0 missing=<what>): boot mode with the gate's dt word,
 * protocol 4 or 5, serial pathing (the pathing kick hook), the Gravidar move (radar serial), the
 * render RNG split, mass traffic and region pass 'sync', no portable or diagnostic transport.
 *
 * When: after the step's report is sealed and sent, the gate plans with the held permit and the
 * pace deadline of the next step (gate_pace_peek): a render-only frame starts no earlier than
 * one frame interval (1/min(native_gate_render_fps, X4's frame-rate target)) after the previous
 * frame and only while it is expected to end before the deadline (estimate: moving mean of the
 * measured frames). A missing permit after the deadline keeps the screen alive at the same rate
 * (counted late=, the step then waits for at most one frame). The step itself runs as before:
 * permit taken, pace hold, FrameTick with dt = 1/hz. Every gate entry still runs the gate's
 * checks; the per-step work (joins, created identities, the report, the digest) runs once.
 * Each gap entry checks that game, real and unpaused time, the dt word and the masked table are
 * as left (fail closed: stop 182, why=). Stop puts every entry, the dt word and the holds back. */
#define RF_STOP_REASON 182u
#define RF_SLOTS 17u
#define RF_RENDER_SLOT 10u
#define RF_FPS_MAX 360u
#define RF_DT_WORD 0x6adfc60u          /* FrameTick's dt with the gate's patch (g_movieCaptureFrameTime) */
#define RF_UNPAUSED 0x6b60e48u         /* TimerBase unpaused real time */
#define RF_LIMIT_ENABLED 0x6adfbb0u    /* X4's frame-rate limit on (PresentAndFrameLimit 0xfa2ca1) */
#define RF_LIMIT_FPS 0x6adfbb4u        /* its target, 0 = 60 */
#define RF_LOADING 0x130u              /* g_VbiTable: loading */
#define RF_PARK_MS 5000u
#define RF_MARGIN_US 1000u
#define RF_STARTED 0x10000u            /* rf_admit: a render-only frame ran, the gap is open */
enum {RF_PLAN_STEP,RF_PLAN_RENDER};
/* What the gate knows when it plans (performance counter units): the permit for the next step,
 * the step's pace deadline (due; now when nothing holds it), the previous frame's start, the
 * frame interval and the expected cost of one render-only frame. */
typedef struct {int held;int64_t now,due,last_start,interval,estimate;} rf_plan_in;
/* Pure: STEP when the step is due (or nothing fits before it and the permit is here), RENDER
 * with the earliest start otherwise; late=1 when that frame does not fit before the deadline
 * (waiting for a permit). */
static int rf_plan(const rf_plan_in *in,int64_t *at,int *late) {
    int64_t next=in->last_start+in->interval;if(next<in->now) next=in->now;
    *at=next;*late=0;
    if(next+in->estimate<=in->due) return RF_PLAN_RENDER;
    if(in->held) return RF_PLAN_STEP;
    *late=1;return RF_PLAN_RENDER;
}
static unsigned rf_fps;
static char rf_reply[GATE_PERMIT_MAX];
static int rf_masked,rf_gap,rf_dt_zero,rf_held_traffic,rf_held_region,rf_restored=1,rf_failed;
static unsigned char *rf_table;
static void *rf_saved[RF_SLOTS];
static unsigned rf_masked_count;
static uint64_t rf_game_bits,rf_real_bits,rf_unpaused_bits;
static ULONGLONG rf_gap_started;
static LARGE_INTEGER rf_ft_begin;
static int64_t rf_last_start,rf_estimate,rf_frame_start,rf_interval,rf_gap_first; /* rf_gap_first: the gap's first render-only frame (lockstep_render_display.h) */
static volatile LONG rf_ready_flag; /* the last boundary's rf_ready() (read in slot 10) */
static uint64_t rf_steps,rf_frames,rf_gap_frames,rf_last_gap,rf_max_gap,rf_hist[5],rf_late,rf_late_us_max,rf_not_ready,rf_holds;
static uint64_t rf_period_us,rf_period_sum_us,rf_period_max_us,rf_main_us,rf_main_sum_us,rf_mw_us,rf_mw_frames,rf_slot10_r_us,rf_slot10_step_us,rf_slot10_r_calls;
static int64_t rf_mw_started;
static unsigned rf_eff_fps,rf_x4_limit;
static volatile LONG64 rf_failures;
static const char *rf_stage="off",*rf_missing="unchecked",*rf_why="-";
/* What a render-only frame relies on in X4.exe 9.00 (checked once, before the first one). */
static const struct {unsigned rva,size;const char *sha;} rf_pin_table[]={
    {0xfa3304u,17u,"162af07c7a600edb4c7a1f47009becee5352b78352f17522013e7a0a9125139e"},  /* FrameTick: dt from the word, no advance at dt <= 0 */
    {0xef4681u,62u,"32056c7aba60c3f7345284bb1f620971c0120dc433c135ecf3367d04dc84db20"},  /* MoveWorker: slots 0..16, a NULL entry skipped */
    {0xef46bfu,28u,"924be6fc0dcb5d566e6582cf2a23670bdef8828ae2c55238c16832b79dfafaf4"}}; /* MoveWorker: sound step only on a later game time */
static int rf_pins_checked;
static int rf_pins(void) {
    if(!rf_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<sizeof(rf_pin_table)/sizeof(*rf_pin_table);i++) ok=hash_ok(image+rf_pin_table[i].rva,rf_pin_table[i].size,rf_pin_table[i].sha);
        rf_pins_checked=ok?1:-1;
    }
    return rf_pins_checked==1;
}
static int rf_fail(const char *why) {if(!strcmp(rf_why,"-")) rf_why=why;rf_stage="failed";rf_failed=1;InterlockedIncrement64(&rf_failures);return 0;}
static int64_t rf_qpc(void) {LARGE_INTEGER n;QueryPerformanceCounter(&n);return n.QuadPart;}
static int64_t rf_freq(void) {static LARGE_INTEGER f;if(!f.QuadPart) QueryPerformanceFrequency(&f);return f.QuadPart;}
static uint64_t rf_us(int64_t ticks) {int64_t f=rf_freq();return ticks>0 && f?(uint64_t)(ticks*1000000/f):0;}
/* gate.render_frames(fps): before the start (after gate.tick_rate); afterwards only the same value. */
static int rf_configure(double fps,int started) {
    if(!isfinite(fps) || floor(fps)!=fps || fps<=(double)gate_tick_hz || fps>RF_FPS_MAX) return 0;
    if(started) return rf_fps==(unsigned)fps;
    rf_fps=(unsigned)fps;rf_stage="armed";return 1;
}
/* The frame interval: the configured cap, and X4's own frame-rate limit when it is on. */
static int64_t rf_frame_interval(void) {
    unsigned char enabled=0;uint32_t limit=0;unsigned fps=rf_fps;
    if(read_local(image+RF_LIMIT_ENABLED,&enabled,1) && enabled && read_local(image+RF_LIMIT_FPS,&limit,4)) {
        rf_x4_limit=limit?limit:60u;if(rf_x4_limit<fps) fps=rf_x4_limit;
    } else rf_x4_limit=0;
    if(!fps) fps=1;
    rf_eff_fps=fps;return rf_freq()/(int64_t)fps;
}
/* Protocol 5 is on (lockstep_adaptive.h, included after this file). */
static int wire_adaptive;
/* Everything a render-only frame relies on; rf_missing names the first gap. */
static int rf_ready(void) {
    const char *missing=
        !rf_fps?"off":!stats.active?"inactive":!boot_mode || boot_phase!=3?"boot":!dt_installed?"dt_word":
        !wire_lookahead && !wire_adaptive?"lookahead":portable_wire_enabled || gd_enabled || portable_native_configured?"transport":
        !path_installed || !(path_options&1)?"serial_pathing":!gravidar_installed?"radar_serial":
        !render_rng_installed || render_rng_mode!=RENDER_RNG_SPLIT || !InterlockedCompareExchange(&render_rng_active,0,0)?"render_rng_split":
        !mt_installed || !InterlockedCompareExchange(&mt_active,0,0) || mt_traffic.mode!=MT_SYNC || mt_region.mode!=MT_SYNC?"pass_sync":
        !stats.issued?"first_step":!rf_pins()?"pins":NULL;
    rf_missing=missing?missing:"-";InterlockedExchange(&rf_ready_flag,!missing);return !missing;
}
/* ---- the slot table ---- */
static int rf_mask_apply(void) {
    unsigned char *table=NULL;int loading=1;
    if(!read_local(image+TABLE_RVA,&table,8) || !table || !read_local(table+RF_LOADING,&loading,4) || loading) return rf_fail("table");
    if(!read_local(table,rf_saved,sizeof(rf_saved)) || !rf_saved[RF_RENDER_SLOT]) return rf_fail("render_slot");
    rf_table=table;rf_masked_count=0;rf_masked=1;
    for(unsigned i=0;i<RF_SLOTS;i++) {
        if(i==RF_RENDER_SLOT || !rf_saved[i]) continue;
        if(InterlockedCompareExchangePointer((void*volatile*)(table+8*i),NULL,rf_saved[i])!=rf_saved[i]) {rf_saved[i]=NULL;return rf_fail("table_changed");}
        ++rf_masked_count;
    }
    return 1;
}
static int rf_mask_intact(void) {
    if(!rf_masked) return 1;
    void *now[RF_SLOTS];
    if(!read_local(rf_table,now,sizeof(now)) || now[RF_RENDER_SLOT]!=rf_saved[RF_RENDER_SLOT]) return 0;
    for(unsigned i=0;i<RF_SLOTS;i++) if(i!=RF_RENDER_SLOT && rf_saved[i] && now[i]) return 0;
    return 1;
}
/* Every masked entry back by CAS (MoveWorker past its slot loop); a foreign value stays. */
static int rf_mask_restore(void) {
    if(!rf_masked) return 1;
    int ok=1;
    for(unsigned i=0;i<RF_SLOTS;i++) {
        if(i==RF_RENDER_SLOT || !rf_saved[i]) continue;
        if(InterlockedCompareExchangePointer((void*volatile*)(rf_table+8*i),rf_saved[i],NULL)!=NULL) ok=0;
        else rf_saved[i]=NULL;
    }
    if(ok) {rf_masked=0;rf_masked_count=0;}
    return ok;
}
/* ---- the pass holds (MoveWorker parked in its frame-start wait, passes joined) ---- */
static void rf_hold(void) {
    if(mt_traffic.mode==MT_SYNC && mt_task(&mt_traffic) && !mt_traffic.held) {mt_hold(&mt_traffic);rf_held_traffic=1;++rf_holds;}
    if(mt_region.mode==MT_SYNC && mt_task(&mt_region) && !mt_region.held) {mt_hold(&mt_region);rf_held_region=1;++rf_holds;}
}
static int rf_release_holds(int wait) {
    if(!rf_held_traffic && !rf_held_region) return 1;
    if(wait) {
        /* The last render-only frame's tail kicks must have passed (found the bytes at 0). */
        ULONGLONG started=GetTickCount64();
        while(!mt_worker_parked()) {if(GetTickCount64()-started>=RF_PARK_MS) return rf_fail("worker_park");SwitchToThread();}
    }
    if(rf_held_traffic) {mt_unhold(&mt_traffic);rf_held_traffic=0;}
    if(rf_held_region) {mt_unhold(&mt_region);rf_held_region=0;}
    return 1;
}
/* ---- the dt word for one FrameTick ---- */
static int rf_dt_zero_set(void) {
    static const double zero=0.0;uint64_t z;memcpy(&z,&zero,8);
    if((uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+RF_DT_WORD),(LONG64)z,(LONG64)fixed_dt_bits)!=fixed_dt_bits) return rf_fail("dt_word");
    rf_dt_zero=1;return 1;
}
static int rf_dt_back(void) {
    if(!rf_dt_zero) return 1;
    static const double zero=0.0;uint64_t z;memcpy(&z,&zero,8);
    if((uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+RF_DT_WORD),(LONG64)fixed_dt_bits,(LONG64)z)!=z) return rf_fail("dt_word_back");
    rf_dt_zero=0;return 1;
}
/* Close the gap before a step (MoveWorker joined): holds back, then the table. */
static int rf_close_gap(void) {
    if(!rf_gap) return 1;
    int ok=rf_release_holds(1) && rf_mask_restore();
    if(!ok) return rf_fail("restore");
    rf_gap=0;rf_stage="step";return 1;
}
/* finish(): everything back, whatever thread stops (a foreign value is never overwritten). */
static void rf_restore(void) {
    InterlockedExchange(&rf_frame_open,0);
    int ok=rf_dt_back();
    if(!rf_release_holds(0)) ok=0;
    if(!rf_mask_restore()) ok=0;
    rf_gap=0;rf_restored=ok;
    if(rf_fps && strcmp(rf_stage,"failed")) rf_stage="stopped";
}
/* ---- MoveWorker side (forward-declared for path_release, mt_wait and the render wrapper) ---- */
/* The pathing kick of a render-only frame: no pass, no join, none of the per-step work. */
static BOOL rf_path_release(HANDLE semaphore,LONG count,LPLONG previous) {
    unsigned char *manager=*(unsigned char**)(image+0x6cf21c8);
    if(GetCurrentThreadId()!=ui_thread || !manager || count!=1 || previous || semaphore!=*(HANDLE*)(manager+0x18)) {
        rf_fail("pathing_kick");finish(RF_STOP_REASON);return ReleaseSemaphore(semaphore,count,previous);
    }
    rf_mw_started=rf_qpc();++rf_mw_frames;return TRUE;
}
/* The MoveWorker back in its frame-start wait (mt_wait): the render-only frame's own time. */
static void rf_worker_end(void) {
    if(!rf_mw_started) return;
    rf_mw_us=rf_us(rf_qpc()-rf_mw_started);rf_mw_started=0;
}
/* Slot 10's duration (render RNG wrapper): in a render-only frame, or in the step's frame. */
static void rf_slot10_done(int64_t ticks) {
    uint64_t us=rf_us(ticks);
    if(InterlockedCompareExchange(&rf_frame_open,0,0)) {rf_slot10_r_us=us;++rf_slot10_r_calls;}
    else rf_slot10_step_us=us;
}
/* ---- main thread ---- */
/* One render-only frame: masked table, held passes, dt 0 for this FrameTick only. */
static unsigned rf_render(tick_fn original,void *frame,unsigned char loading,int late,int64_t lateness) {
    if(!rf_gap) {
        rf_gap_frames=0;
        if(!rf_mask_apply()) {rf_mask_restore();return RF_STOP_REASON;}
        rf_hold();rf_gap=1;rf_stage="gap";
    }
    if(!rf_dt_zero_set()) return RF_STOP_REASON;
    if(late) {++rf_late;uint64_t us=rf_us(lateness);if(us>rf_late_us_max) rf_late_us_max=us;}
    rf_frame_start=rf_last_start=rf_qpc();
    if(!rf_gap_frames) rf_gap_first=rf_frame_start; /* the step's picture is presented by this FrameTick */
    InterlockedExchange(&rf_frame_open,1);
    gate_original_once(original,frame,loading);
    int64_t done=rf_qpc();rf_main_us=rf_us(done-rf_frame_start);rf_main_sum_us+=rf_main_us;
    if(!rf_dt_back()) return RF_STOP_REASON;
    ++rf_frames;++rf_gap_frames;
    return RF_STARTED;
}
/* The frame just run was render-only: account it and check that nothing moved. */
static int rf_after_frame(void) {
    InterlockedExchange(&rf_frame_open,0);
    int64_t now=rf_qpc(),period=now-rf_frame_start;
    rf_period_us=rf_us(period);rf_period_sum_us+=rf_period_us;if(rf_period_us>rf_period_max_us) rf_period_max_us=rf_period_us;
    rf_estimate=rf_estimate?(7*rf_estimate+period)/8:period;
    uint64_t game,real,unpaused;
    memcpy(&game,image+GAME_RVA,8);memcpy(&real,image+REAL_RVA,8);memcpy(&unpaused,image+RF_UNPAUSED,8);
    if(game!=rf_game_bits || real!=rf_real_bits || unpaused!=rf_unpaused_bits) return rf_fail("time_moved");
    if(*(uint64_t*)(image+RF_DT_WORD)!=fixed_dt_bits) return rf_fail("dt_word");
    if(!rf_mask_intact()) return rf_fail("table_changed");
    return 1;
}
/* The socket until 'at' (or the first datagram), without busy waiting longer than a spin. */
static void rf_poll(int64_t at) {
    int64_t left=at-rf_qpc();
    if(left<=0) return;
    uint64_t us=rf_us(left);
    if(us<=GATE_PACE_SPIN_US) {YieldProcessor();return;}
    fd_set readable;FD_ZERO(&readable);FD_SET(connection,&readable);
    struct timeval tv;us-=GATE_PACE_SPIN_US/2;tv.tv_sec=(long)(us/1000000);tv.tv_usec=(long)(us%1000000);
    select(0,&readable,NULL,NULL,&tv);
}
static int lookahead_held(uint64_t tick) {
    const lookahead_permit *e=&lookahead_permits[tick%LOOKAHEAD_RING];return e->used && e->tick==tick;
}
/* Protocol 5 (lockstep_adaptive.h, included after this file): the gap runs the same way over the adaptive permits (their
 * schedule, gap permits, epochs and pings stay that module's; a ping answered between two render-only frames reports how
 * long it waited unread, as between two steps). */
static uint64_t adapt_ready;
static unsigned adapt_drain(char *reply,size_t capacity);
static void adapt_resend(uint64_t completed,ULONGLONG now);
static int adapt_held(uint64_t tick);
static unsigned adapt_take(uint64_t tick);
static unsigned adapt_quiet_check(void);
static unsigned adapt_frame(const char *packet,size_t length,char *reply,size_t capacity);
static unsigned rf_wire_drain(char *reply,size_t capacity) {return wire_adaptive?adapt_drain(reply,capacity):lookahead_drain(reply,capacity);}
static void rf_wire_resend(ULONGLONG now) {if(wire_adaptive) adapt_resend(stats.completed,now);else lookahead_resend(stats.completed,now);}
static int rf_wire_held(uint64_t tick) {return wire_adaptive?adapt_held(tick):lookahead_held(tick);}
/* The step's permit taken: 0, else the stop reason. */
static unsigned rf_wire_take(uint64_t tick) {
    if(wire_adaptive) {
        unsigned taken=adapt_take(tick);
        if(taken==1) {if(!rf_gap_frames) ++adapt_ready;return 0;}
        if(taken) return taken-1;                          /* the schedule's own stop (190/191) */
    } else if(lookahead_take(tick)) {if(!rf_gap_frames) ++lookahead_ready;return 0;}
    rf_fail("permit");return RF_STOP_REASON;
}
/* The step's due time: its pace deadline, or now when nothing would hold it. */
static int64_t rf_due(int64_t now) {
    if(!gate_tick_pace) return now;
    int64_t deadline=gate_pace_peek(&gate_pace,now,rf_freq(),gate_tick_hz);
    return deadline && deadline>now?deadline:now;
}
/* Between a step's report and the next step: 0 = the permit for completed+1 is taken (step now),
 * RF_STARTED = a render-only frame ran, otherwise the stop reason. */
static unsigned rf_admit(tick_fn original,void *frame,unsigned char loading,char *reply,size_t capacity) {
    uint64_t want=stats.completed+1;
    for(;;) {
        ULONGLONG ms=GetTickCount64();
        if(closed_window()) return 6;
        if(ms-rf_gap_started>timeout_ms) return 7;
        unsigned reason=rf_wire_drain(reply,capacity);
        if(reason) return reason;
        /* As the waits (lookahead_wait, adapt_wait): only while the step's permit is missing. With it held, the window's
         * later reports are in flight, not lost (a206r resent every one of them: 6,264 resends, 6,130 stale permits; a1d 337). */
        if(!rf_wire_held(want)) rf_wire_resend(ms);
        double drift=fabs(*(double*)(image+GAME_RVA)-stats.game);
        if(drift>stats.wait_game_delta) stats.wait_game_delta=drift;
        if(drift>0) return 5;
        int64_t now=rf_qpc(),at=0;int late=0;
        rf_plan_in in={rf_wire_held(want),now,rf_due(now),rf_last_start,rf_interval,rf_estimate+(int64_t)RF_MARGIN_US*rf_freq()/1000000};
        int plan=rf_plan(&in,&at,&late);
        if(plan==RF_PLAN_STEP) return rf_wire_take(want);
        if(at>now) {rf_poll(at);continue;}
        return rf_render(original,frame,loading,late,at+in.estimate-in.due);
    }
}
/* At the step boundary (report built): seal and send it, then plan. Without the preconditions
 * this is lookahead_frame (protocol 5: adapt_frame) exactly. */
static unsigned rf_boundary(tick_fn original,void *frame,unsigned char loading,const char *packet,size_t length,
                            char *reply,size_t capacity,LARGE_INTEGER ft_begin) {
    /* A joiner before its handover takes its steps as fast as they come, without render-only frames in between. */
    if(!rf_ready() || join_catching_up()) {++rf_not_ready;return wire_adaptive?adapt_frame(packet,length,reply,capacity):lookahead_frame(packet,length,reply,capacity);}
    if(wire_adaptive) {unsigned quiet=adapt_quiet_check();if(quiet) return quiet;}
    if(!lookahead_seal(stats.completed,packet,length,blob_proposal)) return 56;
    lookahead_send(&lookahead_reports[stats.completed%LOOKAHEAD_RING],GetTickCount64());
    rf_interval=rf_frame_interval();
    if(!rf_estimate) rf_estimate=rf_interval/2; /* until the first frame is measured */
    rf_gap_started=GetTickCount64();rf_ft_begin=ft_begin;rf_gap_frames=0;
    memcpy(&rf_game_bits,image+GAME_RVA,8);memcpy(&rf_real_bits,image+REAL_RVA,8);memcpy(&rf_unpaused_bits,image+RF_UNPAUSED,8);
    unsigned reason=rf_admit(original,frame,loading,reply,capacity);
    if(reason==RF_STARTED) return reason;
    if(!rf_close_gap() && !reason) reason=RF_STOP_REASON;
    return reason;
}
/* Counted once per admitted step (both paths). */
static void rf_step_counted(void) {
    if(!rf_fps) return;
    ++rf_steps;rf_last_gap=rf_gap_frames;if(rf_gap_frames>rf_max_gap) rf_max_gap=rf_gap_frames;
    ++rf_hist[rf_gap_frames<4?rf_gap_frames:4];rf_gap_frames=0;
}
static int rf_status(char *out,size_t capacity) {
    uint64_t total=rf_steps+rf_frames;
    int n=snprintf(out,capacity,
        "fps=%u eff_fps=%u x4_limit=%u ready=%d missing=%s stage=%s why=%s steps=%llu rframes=%llu per_step_x100=%llu "
        "hist=%llu/%llu/%llu/%llu/%llu last_gap=%llu max_gap=%llu period_us=%llu mean_period_us=%llu max_period_us=%llu "
        "main_us=%llu mw_us=%llu mw_frames=%llu slot10_r_us=%llu slot10_step_us=%llu est_us=%llu interval_us=%llu "
        "late=%llu late_us_max=%llu not_ready=%llu holds=%llu masked=%u failures=%lld restored=%d",
        rf_fps,rf_eff_fps,rf_x4_limit,strcmp(rf_missing,"-")==0,rf_missing,rf_stage,rf_why,
        (unsigned long long)rf_steps,(unsigned long long)rf_frames,(unsigned long long)(rf_steps?total*100/rf_steps:0),
        (unsigned long long)rf_hist[0],(unsigned long long)rf_hist[1],(unsigned long long)rf_hist[2],(unsigned long long)rf_hist[3],(unsigned long long)rf_hist[4],
        (unsigned long long)rf_last_gap,(unsigned long long)rf_max_gap,(unsigned long long)rf_period_us,
        (unsigned long long)(rf_frames?rf_period_sum_us/rf_frames:0),(unsigned long long)rf_period_max_us,
        (unsigned long long)rf_main_us,(unsigned long long)rf_mw_us,(unsigned long long)rf_mw_frames,
        (unsigned long long)rf_slot10_r_us,(unsigned long long)rf_slot10_step_us,(unsigned long long)rf_us(rf_estimate),(unsigned long long)rf_us(rf_interval),
        (unsigned long long)rf_late,(unsigned long long)rf_late_us_max,(unsigned long long)rf_not_ready,(unsigned long long)rf_holds,rf_masked_count,
        (long long)rf_failures,rf_restored);
    return n>0 && (size_t)n<capacity?n:0;
}
/* The split of the last step's MoveWorker frame (sim_us): slot 10 (render preparation: UI, character
 * and camera render banks) and the rest (universe update, UI and probe, input, sound, jobs), plus the
 * render-only frames of the gap before the current step. Needs the render RNG wrapper for slot10_us. */
static int rf_split(char *out,size_t capacity) {
    long long rest=(long long)gate_sim_us-(long long)rf_slot10_step_us;
    int n=snprintf(out,capacity,"slot10_us=%llu sim_rest_us=%lld rframes=%llu rframe_us=%llu",
        (unsigned long long)rf_slot10_step_us,rest,(unsigned long long)rf_last_gap,(unsigned long long)rf_period_us);
    return n>0 && (size_t)n<capacity?n:0;
}

/* native_gate_render_fps (lockstep_render_frames.h): the plan, the pace peek, the configuration, a
 * render-only frame on fake engine memory (slot table, dt word, pass holds, the pathing kick, the
 * checks after the frame, the gap closed, the stop), the admission against a loopback relay, the
 * status and the slot 10 split; with X4.exe the pinned sites. No native frame runs here. */
static __declspec(align(16)) unsigned char test_rf_traffic[0x100],test_rf_region[0x200],test_rf_manager[0x40];
static __declspec(align(16)) unsigned char test_rf_table[0x140];
static unsigned char test_rf_handler_objects[RF_SLOTS][16];
static unsigned test_rf_calls;
static int test_rf_seen_zero,test_rf_seen_open,test_rf_seen_masked,test_rf_move_time;
static void test_rf_frametick(void *frame,unsigned char loading) {
    (void)frame;(void)loading;++test_rf_calls;
    test_rf_seen_zero=*(double*)(image+RF_DT_WORD)==0.0;
    test_rf_seen_open=(int)InterlockedCompareExchange(&rf_frame_open,0,0);
    void **table=*(void***)(image+TABLE_RVA);int masked=table[RF_RENDER_SLOT]==test_rf_handler_objects[RF_RENDER_SLOT];
    for(unsigned i=0;i<RF_SLOTS;i++) if(i!=RF_RENDER_SLOT && table[i]) masked=0;
    test_rf_seen_masked=masked;
    if(test_rf_move_time) *(double*)(image+GAME_RVA)+=1.0/30.0; /* a FrameTick that advanced time anyway */
}
static void test_rf_plan(void) {
    const int64_t f=1000000;int64_t at=0;int late=0;
    /* Room before the deadline: render at the next frame slot, never earlier than one interval after the last frame. */
    rf_plan_in in={1,100*f/1000,133*f/1000,95*f/1000,f/60,8*f/1000};
    assert(rf_plan(&in,&at,&late)==RF_PLAN_RENDER && at==95*f/1000+f/60 && !late);
    in.last_start=50*f/1000;assert(rf_plan(&in,&at,&late)==RF_PLAN_RENDER && at==in.now);
    /* No room: step (the pace hold waits for the deadline). */
    in.due=105*f/1000;assert(rf_plan(&in,&at,&late)==RF_PLAN_STEP);
    in.due=in.now;assert(rf_plan(&in,&at,&late)==RF_PLAN_STEP);
    /* No permit and no room: keep drawing at the frame rate, counted late. */
    in.held=0;assert(rf_plan(&in,&at,&late)==RF_PLAN_RENDER && late && at==in.now);
    in.last_start=in.now;assert(rf_plan(&in,&at,&late)==RF_PLAN_RENDER && late && at==in.now+f/60);
    /* No permit but room before the deadline: an ordinary render-only frame. */
    in.due=in.now+f;assert(rf_plan(&in,&at,&late)==RF_PLAN_RENDER && !late);
    /* The pace peek: the deadline the next pace call holds to, without taking it. */
    gate_pace_state p={0};
    assert(!gate_pace_peek(&p,5000,f,30));
    assert(!gate_pace_deadline(&p,5000,f,30));
    assert(gate_pace_peek(&p,5000,f,30)==5000+f/30 && !p.count);
    assert(gate_pace_peek(&p,6000,f,30)==gate_pace_deadline(&p,6000,f,30) && p.count==1);
    assert(gate_pace_peek(&p,6000,f,30)==5000+2*f/30);
    assert(!gate_pace_peek(&p,5000+2*f/30+f*GATE_PACE_CATCHUP_MS/1000+1,f,30)); /* that late, the pace re-bases: nothing held */
    assert(!gate_pace_peek(&p,6000,f,0));
}
static void test_rf_config(void) {
    unsigned saved_hz=gate_tick_hz;gate_tick_hz=30;rf_fps=0;
    assert(!rf_configure(30,0) && !rf_configure(20,0) && !rf_configure(60.5,0) && !rf_configure(NAN,0) && !rf_configure(RF_FPS_MAX+1,0) && !rf_fps);
    assert(rf_configure(60,0) && rf_fps==60 && rf_configure(144,0) && rf_fps==144);
    assert(rf_configure(144,1) && !rf_configure(60,1) && rf_fps==144);
    gate_tick_hz=60;assert(!rf_configure(60,0) && rf_configure(61,0));
    /* X4's own frame-rate limit caps the interval: 144 configured, 60 target (0 = 60), or off. */
    rf_fps=144;image[RF_LIMIT_ENABLED]=1;*(uint32_t*)(image+RF_LIMIT_FPS)=0;
    assert(rf_frame_interval()==rf_freq()/60 && rf_eff_fps==60 && rf_x4_limit==60);
    *(uint32_t*)(image+RF_LIMIT_FPS)=165;assert(rf_frame_interval()==rf_freq()/144 && rf_eff_fps==144 && rf_x4_limit==165);
    image[RF_LIMIT_ENABLED]=0;assert(rf_frame_interval()==rf_freq()/144 && !rf_x4_limit);
    *(uint32_t*)(image+RF_LIMIT_FPS)=0;gate_tick_hz=saved_hz;rf_fps=0;rf_stage="off";
}
/* The fake engine: g_VbiTable with handlers in slots 0..16 except 5 and 12, the dt word at 1/30,
 * both pass tasks idle and ready, the MoveWorker parked, the pathing manager's semaphore. */
static void test_rf_engine(HANDLE semaphore) {
    memset(test_rf_table,0,sizeof(test_rf_table));
    for(unsigned i=0;i<RF_SLOTS;i++) ((void**)test_rf_table)[i]=i==5 || i==12?NULL:test_rf_handler_objects[i];
    *(unsigned char**)(image+TABLE_RVA)=test_rf_table;
    double dt=1.0/30.0;memcpy(&fixed_dt_bits,&dt,8);memcpy(image+RF_DT_WORD,&dt,8);dt_installed=1;
    memset(test_rf_traffic,0,sizeof(test_rf_traffic));memset(test_rf_region,0,sizeof(test_rf_region));
    InitializeCriticalSection((CRITICAL_SECTION*)(test_rf_traffic+MT_TASK_LOCK));InitializeCriticalSection((CRITICAL_SECTION*)(test_rf_region+MT_REGION_LOCK));
    test_rf_traffic[MT_READY]=1;test_rf_region[MT_REGION_READY]=1;
    *(unsigned char**)(image+MT_TASK)=test_rf_traffic;*(unsigned char**)(image+MT_REGION_TASK)=test_rf_region;
    mt_pass_reset(&mt_traffic);mt_pass_reset(&mt_region);mt_traffic.mode=mt_region.mode=MT_SYNC;
    mt_worker_waits=1;mt_worker_wakes=0;
    memset(test_rf_manager,0,sizeof(test_rf_manager));*(HANDLE*)(test_rf_manager+0x18)=semaphore;
    *(unsigned char**)(image+0x6cf21c8)=test_rf_manager;
    *(double*)(image+GAME_RVA)=100.0;*(double*)(image+REAL_RVA)=200.0;*(double*)(image+RF_UNPAUSED)=150.0;
    memcpy(&rf_game_bits,image+GAME_RVA,8);memcpy(&rf_real_bits,image+REAL_RVA,8);memcpy(&rf_unpaused_bits,image+RF_UNPAUSED,8);
    stats.game=100.0;
}
static void test_rf_engine_done(void) {
    DeleteCriticalSection((CRITICAL_SECTION*)(test_rf_traffic+MT_TASK_LOCK));DeleteCriticalSection((CRITICAL_SECTION*)(test_rf_region+MT_REGION_LOCK));
    *(unsigned char**)(image+TABLE_RVA)=NULL;*(unsigned char**)(image+MT_TASK)=NULL;*(unsigned char**)(image+MT_REGION_TASK)=NULL;
    *(unsigned char**)(image+0x6cf21c8)=NULL;memset(image+RF_DT_WORD,0,8);
    mt_pass_reset(&mt_traffic);mt_pass_reset(&mt_region);mt_worker_waits=mt_worker_wakes=0;
}
static void test_rf_reset(void) {
    rf_masked=rf_gap=rf_dt_zero=rf_held_traffic=rf_held_region=rf_failed=0;rf_restored=1;rf_table=NULL;rf_masked_count=0;
    memset(rf_saved,0,sizeof(rf_saved));rf_failures=0;rf_stage="armed";rf_why="-";rf_missing="unchecked";rf_frame_open=0;
    rf_steps=rf_frames=rf_gap_frames=rf_last_gap=rf_max_gap=rf_late=rf_late_us_max=rf_not_ready=rf_holds=0;memset(rf_hist,0,sizeof(rf_hist));
    rf_period_us=rf_period_sum_us=rf_period_max_us=rf_main_us=rf_main_sum_us=rf_mw_us=rf_mw_frames=rf_slot10_r_us=rf_slot10_step_us=rf_slot10_r_calls=0;
    rf_mw_started=0;rf_estimate=rf_last_start=rf_frame_start=rf_interval=0;
}
static int test_rf_table_original(void) {
    for(unsigned i=0;i<RF_SLOTS;i++) if(((void**)test_rf_table)[i]!=(i==5 || i==12?NULL:test_rf_handler_objects[i])) return 0;
    return 1;
}
static void test_rf_frame(void) {
    HANDLE semaphore=CreateSemaphoreW(NULL,0,8,NULL);assert(semaphore);
    DWORD saved_ui=ui_thread;int saved_boot=boot_mode,saved_phase=boot_phase,saved_dt=dt_installed;uint64_t saved_fixed=fixed_dt_bits;
    ui_thread=GetCurrentThreadId();boot_mode=1;boot_phase=3;
    test_rf_reset();test_rf_engine(semaphore);rf_fps=60;
    /* Preconditions name what is missing; the rest of the gate is not armed in this test. */
    stats.active=1;stats.issued=5;assert(!rf_ready() && strcmp(rf_missing,"-"));
    /* One render-only frame: dt 0 and the table masked during FrameTick, both passes held, the gap open. */
    test_rf_calls=0;
    assert(rf_render(test_rf_frametick,NULL,0,0,0)==RF_STARTED && test_rf_calls==1);
    assert(test_rf_seen_zero && test_rf_seen_open && test_rf_seen_masked && rf_gap && rf_masked && rf_masked_count==14);
    assert(*(uint64_t*)(image+RF_DT_WORD)==fixed_dt_bits && !rf_dt_zero && rf_frames==1 && rf_gap_frames==1);
    assert(!test_rf_traffic[MT_READY] && !test_rf_region[MT_REGION_READY] && mt_traffic.held && mt_region.held && rf_holds==2);
    /* The MoveWorker's pathing kick in it: no release, no pass; its time ends at the next frame-start wait. */
    assert(InterlockedCompareExchange(&rf_frame_open,0,0));
    assert(path_release(semaphore,1,NULL) && WaitForSingleObject(semaphore,0)==WAIT_TIMEOUT && rf_mw_frames==1 && rf_mw_started);
    rf_worker_end();assert(!rf_mw_started);
    rf_slot10_done(rf_freq()/1000*3);assert(rf_slot10_r_us==3000 && rf_slot10_r_calls==1 && !rf_slot10_step_us);
    /* The next gate entry: nothing moved; a second frame keeps the mask and the holds. */
    assert(rf_after_frame() && !rf_frame_open && rf_estimate>0);
    rf_slot10_done(rf_freq()/1000*4);assert(rf_slot10_step_us==4000);
    assert(rf_render(test_rf_frametick,NULL,0,1,rf_freq()/1000*2)==RF_STARTED && test_rf_calls==2 && test_rf_seen_masked && rf_holds==2);
    assert(rf_late==1 && rf_late_us_max==2000 && rf_gap_frames==2);
    assert(rf_after_frame());
    /* The step: holds back (MoveWorker parked), table back, then counted with the gap's frames. */
    assert(rf_close_gap() && !rf_gap && !rf_masked && test_rf_table_original());
    assert(test_rf_traffic[MT_READY]==1 && test_rf_region[MT_REGION_READY]==1 && !mt_traffic.held && !mt_region.held);
    rf_step_counted();assert(rf_steps==1 && rf_last_gap==2 && rf_hist[2]==1 && !rf_gap_frames);
    rf_step_counted();assert(rf_steps==2 && rf_hist[0]==1 && rf_max_gap==2);
    /* A pass the stride already holds stays the stride's. */
    mt_hold(&mt_traffic);
    assert(rf_render(test_rf_frametick,NULL,0,0,0)==RF_STARTED && !rf_held_traffic && rf_held_region && rf_after_frame());
    assert(rf_close_gap() && mt_traffic.held && !test_rf_traffic[MT_READY] && test_rf_region[MT_REGION_READY]==1);
    mt_unhold(&mt_traffic);
    /* The MoveWorker still in the frame's tail (not parked): the holds wait, then fail closed. */
    assert(rf_render(test_rf_frametick,NULL,0,0,0)==RF_STARTED && rf_after_frame());
    mt_worker_wakes=1;ULONGLONG t0=GetTickCount64();
    assert(!rf_close_gap() && rf_failed && !strcmp(rf_why,"worker_park") && GetTickCount64()-t0>=RF_PARK_MS-50);
    mt_worker_wakes=0;rf_restore();assert(rf_restored && test_rf_table_original() && test_rf_traffic[MT_READY]==1 && !rf_gap);
    /* Fail closed after a frame: time moved, the dt word changed, a slot registered meanwhile. */
    test_rf_reset();test_rf_move_time=1;
    assert(rf_render(test_rf_frametick,NULL,0,0,0)==RF_STARTED && !rf_after_frame() && !strcmp(rf_why,"time_moved"));
    test_rf_move_time=0;rf_restore();*(double*)(image+GAME_RVA)=100.0;assert(test_rf_table_original());
    test_rf_reset();assert(rf_render(test_rf_frametick,NULL,0,0,0)==RF_STARTED);
    *(double*)(image+RF_DT_WORD)=0.5;assert(!rf_after_frame() && !strcmp(rf_why,"dt_word"));
    memcpy(image+RF_DT_WORD,&fixed_dt_bits,8);rf_restore();
    test_rf_reset();assert(rf_render(test_rf_frametick,NULL,0,0,0)==RF_STARTED);
    ((void**)test_rf_table)[3]=test_rf_handler_objects[0];assert(!rf_after_frame() && !strcmp(rf_why,"table_changed"));
    rf_restore();assert(!rf_restored && ((void**)test_rf_table)[3]==test_rf_handler_objects[0]); /* a foreign value stays */
    ((void**)test_rf_table)[3]=NULL;assert(rf_mask_restore() && test_rf_table_original() && !rf_masked);
    /* A stop in the middle of a frame: the dt word, the holds and the table come back. */
    test_rf_reset();assert(rf_mask_apply() && rf_masked);rf_hold();rf_gap=1;assert(rf_dt_zero_set() && *(double*)(image+RF_DT_WORD)==0.0);
    InterlockedExchange(&rf_frame_open,1);rf_restore();
    assert(rf_restored && !rf_frame_open && *(uint64_t*)(image+RF_DT_WORD)==fixed_dt_bits && test_rf_table_original() &&
           test_rf_traffic[MT_READY]==1 && test_rf_region[MT_REGION_READY]==1 && !strcmp(rf_stage,"stopped"));
    /* Refusals before anything is written: loading, no slot 10, a dt word the gate does not own. */
    test_rf_reset();*(int*)(test_rf_table+RF_LOADING)=1;assert(!rf_mask_apply() && !strcmp(rf_why,"table") && test_rf_table_original());
    *(int*)(test_rf_table+RF_LOADING)=0;((void**)test_rf_table)[RF_RENDER_SLOT]=NULL;rf_why="-";assert(!rf_mask_apply() && !strcmp(rf_why,"render_slot"));
    ((void**)test_rf_table)[RF_RENDER_SLOT]=test_rf_handler_objects[RF_RENDER_SLOT];
    test_rf_reset();*(double*)(image+RF_DT_WORD)=0.25;assert(!rf_dt_zero_set() && *(double*)(image+RF_DT_WORD)==0.25 && !rf_dt_zero);
    /* Not a render-only frame: the pathing kick releases as before (the gate's own path is off here). */
    test_rf_reset();
    /* Status and split lines. */
    rf_steps=3;rf_frames=3;rf_hist[1]=3;rf_last_gap=1;rf_slot10_step_us=4100;gate_sim_us=22000;rf_period_us=16000;
    char out[768];assert(rf_status(out,sizeof(out)) && strstr(out,"fps=60 ") && strstr(out,"per_step_x100=200 ") && strstr(out,"hist=0/3/0/0/0 ") &&
                         strstr(out,"restored=1") && strlen(out)<700);
    assert(rf_split(out,sizeof(out)) && !strcmp(out,"slot10_us=4100 sim_rest_us=17900 rframes=1 rframe_us=16000"));
    assert(!rf_status(out,64) && !rf_split(out,16));
    gate_sim_us=0;test_rf_reset();test_rf_engine_done();CloseHandle(semaphore);rf_fps=0;rf_stage="off";
    ui_thread=saved_ui;boot_mode=saved_boot;boot_phase=saved_phase;dt_installed=saved_dt;fixed_dt_bits=saved_fixed;stats.active=0;stats.issued=0;
}
/* The admission: a loopback relay socket, the pace schedule and the fake engine. */
static void test_rf_admit(void) {
    WSADATA w;assert(!WSAStartup(MAKEWORD(2,2),&w));
    HANDLE semaphore=CreateSemaphoreW(NULL,0,8,NULL);assert(semaphore);
    SOCKET saved_connection=connection;unsigned saved_timeout=timeout_ms,saved_hz=gate_tick_hz;int saved_pace=gate_tick_pace;
    DWORD saved_ui=ui_thread;int saved_boot=boot_mode,saved_phase=boot_phase,saved_dt=dt_installed;uint64_t saved_fixed=fixed_dt_bits;
    la_relay=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(la_relay!=INVALID_SOCKET);
    struct sockaddr_in relay;memset(&relay,0,sizeof(relay));relay.sin_family=AF_INET;relay.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    int size=sizeof(relay);
    assert(!bind(la_relay,(struct sockaddr*)&relay,sizeof(relay)) && !getsockname(la_relay,(struct sockaddr*)&relay,&size));
    DWORD drain_ms=30,client_ms=25;
    assert(!setsockopt(la_relay,SOL_SOCKET,SO_RCVTIMEO,(const char*)&drain_ms,sizeof(drain_ms)));
    connection=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(connection!=INVALID_SOCKET);
    assert(!setsockopt(connection,SOL_SOCKET,SO_RCVTIMEO,(const char*)&client_ms,sizeof(client_ms)));
    assert(!connect(connection,(struct sockaddr*)&relay,sizeof(relay)));
    size=sizeof(la_client);assert(!getsockname(connection,(struct sockaddr*)&la_client,&size));
    strcpy(secret,LA_TOKEN);stats.slot=2;wire_version=3;wire_clients=2;wire_lookahead=1;lookahead_reset();
    ui_thread=GetCurrentThreadId();boot_mode=1;boot_phase=3;stats.active=1;timeout_ms=2000;
    test_rf_reset();test_rf_engine(semaphore);rf_fps=60;gate_tick_hz=30;gate_tick_pace=1;
    stats.issued=stats.completed=4;
    const int64_t f=rf_freq();rf_interval=f/60;rf_estimate=f/200; /* 5 ms a frame */
    /* Reports 0..6 sealed with this client's proposals: a permit carries the proposals of tick-1-k. */
    for(uint64_t c=0;c<=6;c++) {
        char own[GATE_BLOB_HEX_MAX+1],sealed[GATE_STATE_MAX];la_blob(2,c,own);la_state(sealed,sizeof(sealed),c,own);
        assert(lookahead_seal(c,sealed,strlen(sealed),own));lookahead_reports[c%LOOKAHEAD_RING].sent=GetTickCount64();
    }
    /* Permit 5 held, the step due in 50 ms: a render-only frame now. */
    la_send_permit(5);Sleep(10);
    int64_t now=rf_qpc();memset(&gate_pace,0,sizeof(gate_pace));gate_pace.origin=now+f/20-f/30;
    rf_last_start=now-f/60;rf_gap_started=GetTickCount64();test_rf_calls=0;
    assert(rf_admit(test_rf_frametick,NULL,0,rf_reply,sizeof(rf_reply))==RF_STARTED && test_rf_calls==1 && rf_gap && !rf_late);
    assert(rf_after_frame());
    /* The next frame slot (16.7 ms after the last) still ends before the deadline: wait for it, then render. */
    now=rf_qpc();
    assert(rf_admit(test_rf_frametick,NULL,0,rf_reply,sizeof(rf_reply))==RF_STARTED && test_rf_calls==2);
    assert(rf_frame_start-now>=f/60-f/500 && rf_after_frame());
    /* No room left: the step (the permit taken, its inputs those of tick 3). */
    gate_pace.origin=rf_qpc()+f/500-f/30;
    assert(!rf_admit(test_rf_frametick,NULL,0,rf_reply,sizeof(rf_reply)) && test_rf_calls==2 && !lookahead_held(5));
    char expected[GATE_BLOB_HEX_MAX+1];
    for(unsigned s=1;s<=2;s++) {la_blob(s,3,expected);assert(!strcmp(blob_inputs[s-1],expected));}
    assert(rf_close_gap() && test_rf_table_original());rf_step_counted();assert(rf_last_gap==2);
    /* No pacing and no permit: frames at the frame rate while the network is late, each counted late. */
    stats.issued=stats.completed=5;gate_tick_pace=0;rf_last_start=rf_qpc();rf_gap_started=GetTickCount64();now=rf_qpc();
    assert(rf_admit(test_rf_frametick,NULL,0,rf_reply,sizeof(rf_reply))==RF_STARTED && test_rf_calls==3 && rf_late==1);
    assert(rf_frame_start-now>=f/60-f/500 && rf_after_frame());
    /* The permit arrives: the step at once. */
    la_send_permit(6);Sleep(10);
    assert(!rf_admit(test_rf_frametick,NULL,0,rf_reply,sizeof(rf_reply)) && test_rf_calls==3);
    assert(rf_close_gap());rf_step_counted();
    /* Still no permit after the timeout: reason 7; the relay's stop: reason 1; game time moved: 5. */
    stats.issued=stats.completed=6;rf_gap_started=GetTickCount64()-timeout_ms-1;
    assert(rf_admit(test_rf_frametick,NULL,0,rf_reply,sizeof(rf_reply))==7);
    rf_gap_started=GetTickCount64();const char *stop="NGSTOP|4|" LA_TOKEN "|2|6";la_send(stop,strlen(stop));Sleep(10);
    assert(rf_admit(test_rf_frametick,NULL,0,rf_reply,sizeof(rf_reply))==1);
    *(double*)(image+GAME_RVA)=100.5;assert(rf_admit(test_rf_frametick,NULL,0,rf_reply,sizeof(rf_reply))==5);*(double*)(image+GAME_RVA)=100.0;
    /* The boundary without the preconditions is lookahead_frame exactly (no render-only frame). */
    stats.issued=stats.completed=6;char packet[GATE_STATE_MAX];char own[GATE_BLOB_HEX_MAX+1];unsigned counts[16];la_drain(counts);
    la_blob(2,6,own);strcpy(blob_proposal,own);la_state(packet,sizeof(packet),6,own);la_send_permit(7);Sleep(10);
    LARGE_INTEGER begin;QueryPerformanceCounter(&begin);
    assert(!rf_boundary(test_rf_frametick,NULL,0,packet,strlen(packet),rf_reply,sizeof(rf_reply),begin) && rf_not_ready==1 && test_rf_calls==3);
    la_drain(counts);assert(counts[6]==1);
    closesocket(connection);closesocket(la_relay);la_relay=INVALID_SOCKET;connection=saved_connection;WSACleanup();
    test_rf_reset();test_rf_engine_done();CloseHandle(semaphore);rf_fps=0;rf_stage="off";lookahead_reset();blob_proposal[0]=0;
    for(unsigned i=0;i<GATE_CLIENTS_MAX;i++) blob_inputs[i][0]=0;
    memset(&gate_pace,0,sizeof(gate_pace));gate_tick_hz=saved_hz;gate_tick_pace=saved_pace;timeout_ms=saved_timeout;
    wire_version=2;wire_clients=2;wire_lookahead=0;stats.issued=stats.completed=0;stats.active=0;stats.slot=0;stats.game=0;
    ui_thread=saved_ui;boot_mode=saved_boot;boot_phase=saved_phase;dt_installed=saved_dt;fixed_dt_bits=saved_fixed;
}
static void test_render_frames(void) {
    gate_stats saved=stats;char saved_secret[sizeof(secret)];memcpy(saved_secret,secret,sizeof(secret));
    test_rf_plan();test_rf_config();test_rf_frame();test_rf_admit();
    stats=saved;memcpy(secret,saved_secret,sizeof(secret));
    puts("render-only frames: plan and pace peek, configuration and X4's frame limit, one frame on fake engine memory "
         "(dt 0, slots masked, passes held, no pathing kick), checks and restore, admission against a loopback relay PASS");
}
static void test_render_frames_native_image(const char *path) {
    unsigned char *prior=image;int saved=rf_pins_checked;
    image=test_pmath_map(path);rf_pins_checked=0;assert(rf_pins());
    /* What the pins hold: the dt load from the word, the zero-dt skip, the NULL slot skip, the sound step's compare. */
    int32_t d;memcpy(&d,image+0xfa3308,4);assert(0xfa330cu+(uint32_t)d==RF_DT_WORD);
    assert(!memcmp(image+0xfa330c,"\x0f\x57\xc0\x66\x0f\x2f\xf0\x76",8) && 0xfa3315u+image[0xfa3314]==0xfa3384u);
    assert(!memcmp(image+0xef4697,"\x48\x8b\x0c\xdf\x48\x85\xc9\x74",8) && !memcmp(image+0xef46b9,"\x48\x83\xfb\x11",4));
    memcpy(&d,image+0xef4684,4);assert(0xef4688u+(uint32_t)d==TABLE_RVA && *(uint32_t*)(image+0xef468a)==RF_LOADING);
    memcpy(&d,image+0xfa2ca3,4);assert(0xfa2ca7u+(uint32_t)d==RF_LIMIT_ENABLED);
    memcpy(&d,image+0xfa2c76,4);assert(0xfa2c7au+(uint32_t)d==RF_LIMIT_FPS);
    image[0xef469e]^=1;rf_pins_checked=0;assert(!rf_pins());image[0xef469e]^=1;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;rf_pins_checked=saved;
    puts("render-only frames on X4.exe: FrameTick's zero-dt skip, the NULL slot skip and the sound step compare pinned");
}

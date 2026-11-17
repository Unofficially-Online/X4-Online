/* native_gate_move_rng: a Move of the station people or a signal leak runs on a word keyed by tick, object and class,
 * the caller's word untouched; inactive, off the simulation thread or without a word it passes through. */
static uint64_t *test_mrng_word,test_mrng_seen;
static unsigned test_mrng_draws;
static void test_mrng_original(void *self) {
    (void)self;test_mrng_seen=*test_mrng_word;
    for(unsigned i=0;i<test_mrng_draws;i++) {uint64_t s=*test_mrng_word*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;*test_mrng_word=(s>>30)|(s<<34);}
}
static double test_bclk_time;
static double test_bclk_now(void) {return test_bclk_time;}
static void test_move_rng(void) {
    const uint64_t saved_issued=stats.issued;const DWORD saved_ui=ui_thread;const uint32_t saved_active=stats.active;const int saved_phase=boot_phase;
    unsigned char object[16];memset(object,0,sizeof(object));
    uint64_t word=0x99;test_mrng_word=&word;test_mrng_draws=2;
    memset((void*)mrng_calls,0,sizeof(mrng_calls));memset((void*)mrng_drew,0,sizeof(mrng_drew));memset((void*)mrng_draws,0,sizeof(mrng_draws));
    mrng_unkeyed=mrng_foreign=mrng_unknown=mrng_failures=0;mrng_tick=UINT64_MAX;
    /* Inactive: the caller's word. */
    mrng_active=0;mrng_run(0,test_mrng_original,&word,object);assert(test_mrng_seen==0x99 && word!=0x99 && mrng_calls[0]==1 && !mrng_drew[0]);
    /* Active, an object without a portable ID: keyed by its ordinal in the tick; the caller's word stays. */
    stats.active=1;boot_phase=3;stats.issued=500;ui_thread=GetCurrentThreadId();mrng_active=1;word=0x99;
    mrng_run(0,test_mrng_original,&word,object);
    assert(word==0x99 && test_mrng_seen==lockstep_seed(MRNG_DOMAIN,500,0,0,1,0) && mrng_drew[0]==1 && mrng_draws[0]==2 && mrng_unkeyed==1);
    mrng_run(0,test_mrng_original,&word,object);assert(word==0x99 && test_mrng_seen==lockstep_seed(MRNG_DOMAIN,500,0,0,2,0));
    mrng_run(1,test_mrng_original,&word,object);assert(word==0x99 && test_mrng_seen==lockstep_seed(MRNG_DOMAIN,500,0,1,1,0) && mrng_draws[1]==2);
    stats.issued=501;mrng_run(0,test_mrng_original,&word,object);assert(test_mrng_seen==lockstep_seed(MRNG_DOMAIN,501,0,0,1,0));   /* a new tick: ordinals from 1 */
    /* The same tick, object and class give the same word whatever the caller's word is. */
    {   const uint64_t first=test_mrng_seen;mrng_tick=UINT64_MAX;word=0x1234567;mrng_run(0,test_mrng_original,&word,object);
        assert(test_mrng_seen==first && word==0x1234567);}
    /* No draws: counted as a call only. */
    {   const LONG64 drew=mrng_drew[0];test_mrng_draws=0;mrng_run(0,test_mrng_original,&word,object);assert(mrng_drew[0]==drew);test_mrng_draws=2;}
    /* Off the simulation thread, before the first tick, without a word: straight through. */
    ui_thread=GetCurrentThreadId()+1;word=0x99;mrng_run(0,test_mrng_original,&word,object);assert(test_mrng_seen==0x99 && word!=0x99 && mrng_foreign==1);
    ui_thread=GetCurrentThreadId();stats.issued=0;word=0x99;mrng_run(0,test_mrng_original,&word,object);assert(test_mrng_seen==0x99 && word!=0x99);
    stats.issued=501;{const LONG64 calls=mrng_calls[1];test_mrng_word=&word;mrng_run(1,test_mrng_original,NULL,object);assert(mrng_calls[1]==calls+1);}
    {char status[512];assert(mrng_status(status,sizeof(status))>0 && strstr(status,"installed=0 npc=") && strstr(status," foreign=1 ") && strstr(status," failures=0"));}
    /* The animation clock: for the NPC whose Move runs, a noted play answers by the simulation's time; the motion's
     * key is its low 24 bits and its type byte (the other bits of a handle are this process's). */
    {   double elapsed=0,whole=0;
        bclk_now_override=test_bclk_now;memset(bclk_entries,0,sizeof(bclk_entries));bclk_answers=0;
        bclk_clock_on=1;bclk_inside=1;bclk_npc=0x77;ui_thread=GetCurrentThreadId();
        bclk_entry_t *e=bclk_entry(0x77,bclk_motion_key(0x0000000700000021ULL),1);e->start=10.0;e->whole=0.8333;
        test_bclk_time=10.5;assert(bclk_clock(0xbe877707cf000021ULL,&elapsed,&whole) && fabs(elapsed-0.5)<1e-9 && whole==0.8333 && bclk_answers==1);
        test_bclk_time=9.0;assert(bclk_clock(0x0000000700000021ULL,&elapsed,&whole) && elapsed==0.0);   /* never negative */
        /* Over by this clock: answered so for the rest of that tick, spent from the next (X4 answers again). */
        stats.issued=700;test_bclk_time=11.0;assert(bclk_clock(0x0000000700000021ULL,&elapsed,&whole) && elapsed>=whole && e->over_tick==700);
        assert(bclk_clock(0x0000000700000021ULL,&elapsed,&whole));stats.issued=701;assert(!bclk_clock(0x0000000700000021ULL,&elapsed,&whole));
        e->over_tick=0;test_bclk_time=10.5;stats.issued=501;
        assert(!bclk_clock(0x0000000700000022ULL,&elapsed,&whole) && !bclk_clock(0x0000000800000021ULL,&elapsed,&whole));   /* another motion, another type */
        bclk_npc=0x78;assert(!bclk_clock(0x0000000700000021ULL,&elapsed,&whole));           /* another person */
        bclk_npc=0x77;e->whole=-1;assert(!bclk_clock(0x0000000700000021ULL,&elapsed,&whole));   /* a play without a control: X4's answers */
        e->whole=0.8333;bclk_inside=0;assert(!bclk_clock(0x0000000700000021ULL,&elapsed,&whole));   /* outside a Move */
        bclk_inside=1;bclk_clock_on=0;assert(!bclk_clock(0x0000000700000021ULL,&elapsed,&whole));   /* off */
        bclk_inside=0;bclk_npc=0;bclk_now_override=NULL;memset(bclk_entries,0,sizeof(bclk_entries));bclk_answers=0;
    }
    /* Arming: a zero image (no pins) refuses and installs nothing. */
    stats.issued=0;boot_phase=0;assert(!mrng_arm() && !mrng_installed);
    mrng_active=0;memset((void*)mrng_calls,0,sizeof(mrng_calls));memset((void*)mrng_drew,0,sizeof(mrng_drew));memset((void*)mrng_draws,0,sizeof(mrng_draws));
    mrng_unkeyed=mrng_foreign=mrng_unknown=mrng_failures=0;mrng_tick=UINT64_MAX;mrng_stage="idle";
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_phase=saved_phase;
    puts("move RNG: a keyed word per tick, object and class, the caller's word untouched, pass-through cases, arming refusal PASS");
}

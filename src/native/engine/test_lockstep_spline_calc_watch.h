/* native_gate_spline_calc_watch (lockstep_spline_calc_watch.h): the row, the sink, the status; on the executable's bytes the
 * calculator's entry armed, owned, the view keeping its pin, a foreign entry failing the tick check, restored, and the request
 * reads the hook mirrors. The hook's call into the game is not executed here. */
static char tscw_rows[4096];static size_t tscw_length;
static void tscw_sink(const char *text,int n) {
    assert(n>0 && tscw_length+(size_t)n<sizeof(tscw_rows));memcpy(tscw_rows+tscw_length,text,(size_t)n);tscw_length+=(size_t)n;tscw_rows[tscw_length]=0;
}
static void test_spline_calc_watch(void) {
    char row[320];
    int n=scw_row(row,sizeof(row),3721,1,8123,3722,0,-1,4242,2,0x000000000006f1e7ull);assert(n>0 && row[n-2]=='\r' && row[n-1]=='\n');
    assert(strstr(row,"[X4LAN] NATIVE_GATE SPLINE_CALC tick=3721 frame=1 us=8123 tick_out=3722 frame_out=0 us_out=-1 thread=4242 index=2 key=000000000006f1e7"));
    assert(!scw_row(row,40,1,0,0,1,0,0,1,0,0));   /* too small: nothing */
    scw_sink=tscw_sink;tscw_length=0;n=scw_row(row,sizeof(row),1,0,5,1,0,6,7,0,0);
    scw_append(row,n);assert(scw_rows==1 && !tscw_length);scw_flush();assert(tscw_length==(size_t)n && !scw_used);
    scw_flush();assert(tscw_length==(size_t)n);
    scw_sink=NULL;free(scw_buffer);scw_buffer=NULL;scw_used=scw_capacity=0;scw_rows=0;
    {   /* the frame clock: none before a frame began */
        const LONG64 q=scw_frame_qpc,hz=scw_qpc_hz;scw_frame_qpc=0;scw_qpc_hz=10000000;assert(scw_us_since_frame()==-1);
        LARGE_INTEGER now;QueryPerformanceCounter(&now);scw_frame_qpc=now.QuadPart;assert(scw_us_since_frame()>=0);scw_frame_qpc=q;scw_qpc_hz=hz;
    }
    {char out[400];assert(scw_status(out,sizeof(out))>0 && strstr(out,"installed=0 restored=0 stage=idle window=0:0") && strstr(out," xform=0:0"));}
    {   /* SPLINE_XFORM: the row */
        unsigned char m[0x40];for(unsigned i=0;i<0x40;i++) m[i]=(unsigned char)i;
        n=scw_xform_row(row,sizeof(row),3722,1,0x6f1e7,2,0x2697,0x72b,m,NULL);assert(n>0 && row[n-2]=='\r' && row[n-1]=='\n');
        assert(strstr(row,"[X4LAN] NATIVE_GATE SPLINE_XFORM tick=3722 site=1 request=000000000006f1e7 index=2 key=0000000000002697 space=000000000000072b m=000102030405"));
        assert(strstr(row,"3c3d3e3f b=-\r\n") && !scw_xform_row(row,100,1,0,0,0,0,0,m,NULL));
        unsigned char b[0x60];for(unsigned i=0;i<0x60;i++) b[i]=(unsigned char)(0xa0+i);
        char big[640];n=scw_xform_row(big,sizeof(big),3722,0,1,0,2,3,m,b);assert(n>0 && strstr(big," b=a0a1a2") && strstr(big,"fdfeff\r\n"));
    }
    printf("spline calc watch: the row, the sink, the frame clock, the transform row PASS\n");
}
/* SPLINE_PHASE: the drain against a queue of our own (the calculator a thread that empties it once the phase opens), the hold. */
static CRITICAL_SECTION tscw_cs;static uint64_t tscw_count;
static DWORD WINAPI tscw_calculator(void *p) {
    (void)p;if(WaitForSingleObject(scw_phase_event,2000)!=WAIT_OBJECT_0) return 1;
    Sleep(5);EnterCriticalSection(&tscw_cs);tscw_count=0;LeaveCriticalSection(&tscw_cs);return 0;
}
static DWORD WINAPI tscw_opener(void *p) {(void)p;Sleep(20);SetEvent(scw_phase_event);return 0;}
static void test_spline_phase(void) {
    InitializeCriticalSection(&tscw_cs);CRITICAL_SECTION *lock=scw_queue_lock;const uint64_t *at=scw_queue_at;
    scw_queue_lock=&tscw_cs;scw_queue_at=&tscw_count;scw_phase_event=CreateEventW(NULL,TRUE,FALSE,NULL);assert(scw_phase_event);
    scw_phase_on=0;tscw_count=3;assert(scw_phase_drain() && !scw_phase_drains);           /* off: nothing */
    scw_phase_on=1;tscw_count=0;assert(scw_phase_drain() && !scw_phase_drains);           /* an empty queue: not opened */
    tscw_count=2;HANDLE t=CreateThread(NULL,0,tscw_calculator,NULL,0,NULL);assert(t);
    assert(scw_phase_drain() && scw_phase_drains==1 && !tscw_count && WaitForSingleObject(scw_phase_event,0)==WAIT_TIMEOUT);   /* drained, closed again */
    WaitForSingleObject(t,2000);CloseHandle(t);
    scw_drain_ms=30;tscw_count=1;assert(!scw_phase_drain() && !strcmp(scw_stage,"phase_drain_timeout") && scw_failures==1);   /* nobody empties it */
    assert(WaitForSingleObject(scw_phase_event,0)==WAIT_TIMEOUT);scw_failures=0;scw_drain_ms=SCW_DRAIN_MS;
    /* the hold: open passes at once; closed waits for the phase; closed past its bound counts a failure and runs */
    SetEvent(scw_phase_event);scw_phase_hold();assert(!scw_phase_held);ResetEvent(scw_phase_event);
    t=CreateThread(NULL,0,tscw_opener,NULL,0,NULL);assert(t);scw_phase_hold();assert(scw_phase_held==1 && !scw_phase_timeouts);
    WaitForSingleObject(t,2000);CloseHandle(t);ResetEvent(scw_phase_event);
    scw_hold_ms=10;scw_phase_hold();assert(scw_phase_held==2 && scw_phase_timeouts==1 && scw_failures==1);scw_hold_ms=SCW_PHASE_WAIT_MS;
    {char out[400];assert(scw_status(out,sizeof(out))>0 && strstr(out," phase=1:2:"));}   /* two drains opened: the emptied one and the timed-out one */
    scw_restore();assert(!scw_phase_on && WaitForSingleObject(scw_phase_event,0)==WAIT_OBJECT_0);   /* not installed: the event released */
    CloseHandle(scw_phase_event);scw_phase_event=NULL;scw_queue_lock=lock;scw_queue_at=at;DeleteCriticalSection(&tscw_cs);
    scw_phase_drains=scw_phase_jobs=scw_phase_held=scw_phase_timeouts=scw_phase_max_us=0;scw_failures=0;scw_stage="idle";
    printf("spline phase: off, empty, drained by the calculator and closed again, the drain bound, the hold (open, opened, bound) and the restore's release PASS\n");
}
static void test_spline_calc_watch_native_image(const char *path) {
    unsigned char *prior=image,*saved=scw_page;image=test_pmath_map(path);scw_page=NULL;
    assert(scw_pins() && scw_state()==0 && !memcmp(image+SCW_ENTRY,scw_original,7));
    image[SCW_ENTRY+0x80]^=1;assert(!scw_pins());image[SCW_ENTRY+0x80]^=1;
    /* the calculator's two transform calls: E8 to 0x3dde10 (tamper refused) */
    assert(scw_xform_pins() && scw_xform_state(0)==0 && scw_xform_state(1)==0);
    image[0xec13dc]^=1;assert(!scw_xform_pins() && scw_xform_state(0)<0);image[0xec13dc]^=1;
    /* the request reads the hook mirrors: rsi is the request (mov rsi,rcx), its +0x70 index and +0x28 runtime ID */
    assert(!memcmp(image+0xec0d12,"\x48\x8b\xf1",3) && !memcmp(image+0xec0d91,"\x44\x8b\x7e\x70",4) && !memcmp(image+0xec0d9a,"\x48\x8b\x7e\x28",4));
    const uint32_t sa=stats.active;const uint64_t si=stats.issued;const DWORD su=ui_thread;const int sb=boot_mode;
    stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();boot_mode=0;
    assert(!scw_arm(0,3) && !strcmp(scw_stage,"window"));
    stats.issued=4;assert(!scw_arm(3700,3800) && !strcmp(scw_stage,"state"));stats.issued=0;
    assert(scw_arm(3700,3800) && scw_installed && scw_state()==1 && scw_owned() && scw_check() && !strcmp(scw_stage,"armed"));
    assert(scw_xform_on && scw_xform_state(0)==1 && scw_xform_state(1)==1 && scw_xform_pins());   /* patched; the view keeps the pins */
    for(unsigned i=0;i<SCW_XFORM_SITES;i++) {
        unsigned char viewed[7],raw[5];int32_t a,b;assert(read_local(image+scw_xform_sites[i]-1,viewed,7));memcpy(raw,image+scw_xform_sites[i],5);
        memcpy(&a,viewed+2,4);memcpy(&b,raw+1,4);
        assert(viewed[1]==0xe8 && a==scw_xform_rel(i,image+SCW_XFORM_FN) && b==scw_xform_rel(i,scw_page+SCW_XFORM_STUB+0x20u*i));
    }
    assert(scw_arm(3700,3800) && !scw_arm(3700,3801) && scw_installed && scw_to==3800);   /* the setup's repeat: the same window only */
    assert(scw_loop_pins() && scw_phase_arm() && scw_phase_on && scw_phase_event && scw_phase_arm() && scw_installed);   /* the phase shares the entry */
    {char out[400];assert(scw_status(out,sizeof(out))>0 && strstr(out," phase=1:0:0:0:0:0"));}
    assert(scw_pins() && image[SCW_ENTRY]==0xe9 && image[SCW_ENTRY+5]==0xcc && image[SCW_ENTRY+6]==0xcc);   /* the view keeps the pin */
    {unsigned char viewed[9];assert(read_local(image+SCW_ENTRY-1,viewed,9) && !memcmp(viewed+1,scw_original,7));}
    {char out[400];assert(scw_status(out,sizeof(out))>0 && strstr(out,"installed=1 restored=0 stage=armed window=3700:3800"));}
    {   unsigned char ours[7];memcpy(ours,image+SCW_ENTRY,7);unsigned char other[7]={0xe9,1,0,0,0,0xcc,0xcc};
        assert(view_cas_bytes(SCW_ENTRY,ours,other,7) && !scw_owned() && !scw_check() && !strcmp(scw_stage,"site_changed"));
        assert(view_cas_bytes(SCW_ENTRY,other,ours,7) && scw_owned() && !scw_check());scw_failures=0;assert(scw_check());
    }
    scw_restore();assert(!scw_installed && scw_restored && scw_state()==0 && !strcmp(scw_stage,"restored") && !scw_any && scw_pins() && !scw_phase_on);
    assert(!scw_xform_on && scw_xform_state(0)==0 && scw_xform_state(1)==0 && scw_xform_pins());
    CloseHandle(scw_phase_event);scw_phase_event=NULL;scw_queue_lock=NULL;scw_queue_at=NULL;
    stats.active=sa;stats.issued=si;ui_thread=su;boot_mode=sb;
    VirtualFree(scw_page,0,MEM_RELEASE);scw_page=saved;scw_restored=0;scw_stage="idle";scw_from=scw_to=0;
    scw_calls=scw_in_frame_calls=scw_rows=scw_dropped=scw_failures=0;scw_xform_rows=0;
    VirtualFree(image,0,MEM_RELEASE);image=prior;
    printf("spline calc watch native image: the calculator 0xec0cb0 pinned (tamper refused), its request reads, window/state refusals, "
           "armed/owned (the view keeps the pin), a foreign entry fails the tick check, restored; the transform sites pinned, patched, "
           "hidden by the view, restored PASS\n");
}

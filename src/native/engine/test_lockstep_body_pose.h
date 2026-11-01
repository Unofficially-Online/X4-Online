/* native_gate_body_pose (lockstep_body_pose.h). Unit: the call decision with stand-ins for 0x3dde10 and 0x3db8a0 over
 * the path transforms tests' fake components (arming, admitted frame, MoveWorker or another thread, flag, a == b, the
 * TLS time and its init byte, a faulting composition, the comparison with the native result, 'observe', rows and their
 * cap, the tick check, status). Native image: the real arm on the relocated X4.exe (state, pins and site refusals, the swap, the view for
 * other modules' pins), the stub entered on the MoveWorker running the real GetTransformRelativeTo over fake components
 * at the TLS time, another thread native, then tamper, restore. Included by test_lockstep_gate.c after
 * test_lockstep_path_transforms.h (its fixtures). */
static void test_bpose_counts_reset(void) {
    bpose_calls=bpose_outside=bpose_trivial=bpose_other=bpose_diverted=bpose_differs=bpose_failures=bpose_rows=bpose_dropped=bpose_tick_rows=0;
}
static void test_body_pose(void) {
    int saved_active=stats.active,saved_phase=boot_phase,owned_identities=!world_identities;uint64_t saved_issued=stats.issued;
    if(owned_identities) world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(0x5002,0x176ad5) && world_identity_record(0x5001,0x13e75f));
    test_ptf_world();test_bpose_counts_reset();test_ptf_native_calls=test_ptf_gtr_calls=test_ptf_gtr_fault=0;
    static bpose_row_t ring[BPOSE_RING];bpose_row_t *saved_ring=bpose_ring;bpose_ring=ring;memset(ring,0,sizeof(ring));
    static unsigned char tls[0x400];memset(tls,0,sizeof(tls));
    const double now=90.25;tls[BPOSE_TLS_INIT]=1;memcpy(tls+BPOSE_TLS_TIME,&now,8);
    unsigned char *wreck=(unsigned char*)&test_ptf_parts[2],*zone=(unsigned char*)&test_ptf_parts[1],*part=(unsigned char*)&test_ptf_parts[4];
    float m[16]={1,2,3,0, 1,0,0,0, 0,1,0,0, 0,0,1,0};
    #define BPOSE_CALL(a,b,flag,mw,t) bpose_call_using(a,m,b,flag,mw,t,test_ptf_native,test_ptf_gtr)
    stats.active=1;boot_phase=3;stats.issued=2492;bpose_mode=BPOSE_SIM;
    /* Not armed: native. */
    InterlockedExchange(&bpose_active,0);
    test_ptf_native_value=5.0f;assert(BPOSE_CALL(wreck,zone,0,1,tls)==(void*)0x77 && m[0]==5.0f && bpose_outside==1 && !test_ptf_gtr_calls);
    InterlockedExchange(&bpose_active,1);
    /* The MoveWorker in an admitted frame: the composition at its TLS game time; the native one into a copy, a row
     * because they differ (the stale published pose). */
    m[0]=1.0f;assert(BPOSE_CALL(wreck,zone,0,1,tls)==m && m[0]==101.0f && test_ptf_gtr_calls==1 && test_ptf_native_calls==2);
    assert(test_ptf_gtr_a==wreck && test_ptf_gtr_b==zone && test_ptf_gtr_time==now && bpose_diverted==1 && bpose_differs==1 && bpose_rows==1 &&
           bpose_last_time==now);
    char out[1024];
    assert(bpose_row_text(0,out,sizeof(out))>0 &&
           strstr(out,"index=0 tick=2492 kind=sim a=0000000000176ad5/0/0 b=000000000013e75f/0/0 ctrl=02c3f5f0 time=4056900000000000 sim=0000ca42") &&
           strstr(out," native=0000a040"));
    assert(!bpose_row_text(1,out,sizeof(out)));
    /* Equal results: diverted, no row. A part of the wreck: the same composition. */
    test_ptf_native_value=101.0f;m[0]=1.0f;assert(BPOSE_CALL(wreck,zone,0,1,tls)==m && m[0]==101.0f && bpose_diverted==2 && bpose_differs==1 && bpose_rows==1);
    test_ptf_native_value=5.0f;m[0]=1.0f;assert(BPOSE_CALL(part,zone,0,1,tls)==m && test_ptf_gtr_calls==3 && bpose_diverted==3 && bpose_differs==2);
    assert(bpose_row_text(1,out,sizeof(out))>0 && strstr(out,"kind=sim a=0000000000176ad5/0/1 ") && strstr(out," ctrl=00000000 "));
    /* Another thread in an admitted frame: native, counted and rowed. */
    test_ptf_native_value=7.0f;assert(BPOSE_CALL(wreck,zone,0,0,tls)==(void*)0x77 && m[0]==7.0f && bpose_other==1 && test_ptf_gtr_calls==3);
    assert(bpose_row_text(2,out,sizeof(out))>0 &&
           strstr(out,"kind=other a=0000000000176ad5/0/0 b=000000000013e75f/0/0 ctrl=02c3f5f0 time=0000000000000000 sim=- native=0000e040"));
    /* Outside an admitted frame or the gate: native. */
    boot_phase=2;assert(BPOSE_CALL(wreck,zone,0,1,tls)==(void*)0x77);boot_phase=3;
    stats.active=0;assert(BPOSE_CALL(wreck,zone,0,1,tls)==(void*)0x77);stats.active=1;
    assert(bpose_outside==3 && test_ptf_gtr_calls==3);
    /* A flag, a == b: native. */
    assert(BPOSE_CALL(wreck,zone,1,1,tls)==(void*)0x77 && BPOSE_CALL(zone,zone,0,1,tls)==(void*)0x77 && bpose_trivial==2);
    /* No TLS, its init byte clear, a non-finite time, a faulting composition: native, failures, the matrix the native one. */
    assert(BPOSE_CALL(wreck,zone,0,1,NULL)==(void*)0x77 && bpose_failures==1 && !strcmp(bpose_stage,"tls_time"));
    tls[BPOSE_TLS_INIT]=0;assert(BPOSE_CALL(wreck,zone,0,1,tls)==(void*)0x77 && bpose_failures==2);tls[BPOSE_TLS_INIT]=1;
    const double nan=NAN;memcpy(tls+BPOSE_TLS_TIME,&nan,8);assert(BPOSE_CALL(wreck,zone,0,1,tls)==(void*)0x77 && bpose_failures==3);
    memcpy(tls+BPOSE_TLS_TIME,&now,8);
    test_ptf_gtr_fault=1;m[0]=9.0f;test_ptf_native_value=6.0f;
    assert(BPOSE_CALL(wreck,zone,0,1,tls)==(void*)0x77 && m[0]==6.0f && bpose_failures==4 && !strcmp(bpose_stage,"compose"));
    test_ptf_gtr_fault=0;assert(bpose_diverted==3 && bpose_rows==3);
    /* 'observe': both computed, the native result kept, differences counted and rowed. */
    bpose_mode=BPOSE_OBSERVE;test_ptf_native_value=1.0f;m[0]=1.0f;
    assert(BPOSE_CALL(wreck,zone,0,1,tls)==(void*)0x77 && m[0]==1.0f && bpose_differs==3 && bpose_rows==4);
    assert(bpose_row_text(3,out,sizeof(out))>0 && strstr(out,"kind=observe ") && strstr(out," sim=0000ca42") && strstr(out," native=0000803f"));
    test_ptf_native_value=101.0f;m[0]=1.0f;
    assert(BPOSE_CALL(wreck,zone,0,1,tls)==(void*)0x77 && m[0]==101.0f && bpose_differs==3 && bpose_rows==4); /* equal: no row */
    assert(bpose_diverted==3);
    bpose_mode=BPOSE_SIM;
    /* The per-tick cap, reset by the tick check; failures fail it. */
    test_ptf_native_value=5.0f;
    bpose_tick_rows=BPOSE_ROWS_PER_TICK;LONG64 rows=bpose_rows;assert(BPOSE_CALL(wreck,zone,0,1,tls)==m && bpose_rows==rows && bpose_dropped==1);
    bpose_installed=0;assert(!bpose_check() && !bpose_tick_rows);bpose_failures=0;assert(bpose_check());
    assert(BPOSE_CALL(wreck,zone,0,1,tls)==m && bpose_rows==rows+1);
    /* Rows evicted from the ring say so. */
    bpose_rows=BPOSE_RING+5;assert(bpose_row_text(2,out,sizeof(out))>0 && !strcmp(out,"index=2 evicted=1"));bpose_rows=rows+1;
    assert(bpose_status(out,sizeof(out))>0 && strstr(out,"mode=sim installed=0 restored=0 stage=compose calls=") &&
           strstr(out," other=1 diverted=5 differs=5 rows=5 dropped=1 failures=0 time=4056900000000000"));
    assert(!bpose_status(out,40));
    #undef BPOSE_CALL
    InterlockedExchange(&bpose_active,0);bpose_mode=BPOSE_OFF;bpose_ring=saved_ring;bpose_stage="idle";test_bpose_counts_reset();bpose_last_time=0;
    stats.active=saved_active;boot_phase=saved_phase;stats.issued=saved_issued;
    if(owned_identities) {VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;}
    puts("body pose: the MoveWorker composes at its game time, rows where the published pose differs; other threads, outside frames, flags native; failures, observe, cap, check, status PASS");
}
static void test_body_pose_native_image(const char *path) {
    unsigned char *prior=image;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;uint64_t saved_issued=stats.issued;
    int saved_boot=boot_mode,saved_phase=boot_phase;
    image=test_pmath_map(path);
    assert(bpose_pinned() && bpose_site_state()==0);
    unsigned char create[BPOSE_CREATE_SIZE];memcpy(create,image+BPOSE_CREATE,sizeof(create));
    /* The site is CreateBody's view-cache branch: xor r9d,r9d; mov r8,rdi; lea rdx,[rsp+0x90]; mov rcx,r12; call 0x3dde10. */
    {static const unsigned char setup[16]={0x45,0x33,0xc9,0x4c,0x8b,0xc7,0x48,0x8d,0x94,0x24,0x90,0,0,0,0x49,0x8b};
     assert(!memcmp(image+BPOSE_SITE-0x11,setup,sizeof(setup)) && image[BPOSE_SITE]==0xe8);}
    /* Refused: not the UI thread before the first admitted frame, a mode. */
    stats.active=1;stats.issued=0;boot_mode=1;boot_phase=1;ui_thread=GetCurrentThreadId();
    assert(!bpose_arm(BPOSE_SIM) && !strcmp(bpose_stage,"state"));boot_phase=0;
    stats.issued=1;assert(!bpose_arm(BPOSE_SIM));stats.issued=0;assert(!bpose_arm(BPOSE_OFF) && !bpose_arm(7));
    ui_thread=GetCurrentThreadId()+1;assert(!bpose_arm(BPOSE_SIM));ui_thread=GetCurrentThreadId();
    /* A changed pin and a foreign site refuse before anything is written. */
    image[BPOSE_CREATE+0x100]^=1;assert(!bpose_arm(BPOSE_SIM) && !strcmp(bpose_stage,"pins"));image[BPOSE_CREATE+0x100]^=1;
    image[PTF_GTR+0x40]^=1;assert(!bpose_arm(BPOSE_SIM) && !strcmp(bpose_stage,"pins"));image[PTF_GTR+0x40]^=1;
    image[BPOSE_SITE+1]^=1;assert(!bpose_arm(BPOSE_SIM) && !strcmp(bpose_stage,"site"));image[BPOSE_SITE+1]^=1;
    assert(!bpose_installed && !bpose_done);
    /* Armed: the call goes to the stub, other readers see the file's bytes. */
    assert(bpose_arm(BPOSE_SIM) && bpose_installed && bpose_done && bpose_owned() && bpose_check() && !strcmp(bpose_stage,"armed"));
    assert(bpose_arm(BPOSE_SIM) && !bpose_arm(BPOSE_OBSERVE)); /* a reloaded UI state: only the same mode */
    {int32_t d;memcpy(&d,image+BPOSE_SITE+1,4);assert(bpose_site_state()==1 && image+BPOSE_SITE+5+d==bpose_page);}
    assert(memcmp(image+BPOSE_CREATE,create,sizeof(create)) && hash_ok(image+BPOSE_CREATE,BPOSE_CREATE_SIZE,BPOSE_CREATE_SHA) && bpose_pinned());
    {unsigned char view[BPOSE_CREATE_SIZE];assert(read_local(image+BPOSE_CREATE,view,sizeof(view)) && !memcmp(view,create,sizeof(view)));}
    /* The stub, entered on the MoveWorker in an admitted frame, runs the real GetTransformRelativeTo over the fake
     * components at the thread's game time: the zone's child (one link) and the wreck's part (two links). */
    test_ptf_world();test_bpose_counts_reset();
    static unsigned char tls[0x400];memset(tls,0,sizeof(tls));
    const double now=90.25;tls[BPOSE_TLS_INIT]=1;memcpy(tls+BPOSE_TLS_TIME,&now,8);
    bpose_test_tls=tls;bpose_hier_native=test_ptf_native;bpose_gtr_native=NULL;test_ptf_native_value=0.0f;
    stats.active=1;boot_phase=3;stats.issued=2492;
    unsigned char *wreck=(unsigned char*)&test_ptf_parts[2],*zone=(unsigned char*)&test_ptf_parts[1],*part=(unsigned char*)&test_ptf_parts[4];
    ptf_hier_fn stub=(ptf_hier_fn)(void*)bpose_page;
    {__declspec(align(16)) float m[16]={1,2,3,0, 1,0,0,0, 0,1,0,0, 0,0,1,0};test_ptf_locals=0;test_ptf_seen_time=0;
     assert(stub(wreck,m,zone,0)==m && test_ptf_locals==1 && test_ptf_seen_time==now);
     assert(m[0]==31.0f && m[1]==62.0f && m[2]==93.0f && m[4]==1.0f && m[9]==1.0f && m[14]==1.0f);}
    {__declspec(align(16)) float n[16]={0,0,0,0, 1,0,0,0, 0,1,0,0, 0,0,1,0};test_ptf_locals=0;
     assert(stub(part,n,zone,0)==n && test_ptf_locals==2 && n[0]==80.0f && n[1]==160.0f && n[2]==240.0f);}
    assert(bpose_diverted==2 && bpose_calls==2 && bpose_differs==2 && bpose_rows==2 && !bpose_failures && bpose_last_time==now);
    {char row[1024];assert(bpose_row_text(0,row,sizeof(row))>0 && strstr(row,"kind=sim ") && strstr(row," sim=0000f841") && strstr(row," native=00000000"));}
    /* Another thread gets the native call. */
    ui_thread=GetCurrentThreadId()+1;
    {float m[16]={0};test_ptf_native_value=4.0f;assert(stub(wreck,m,zone,0)==(void*)0x77 && m[0]==4.0f && bpose_other==1);}
    ui_thread=GetCurrentThreadId();bpose_test_tls=NULL;bpose_hier_native=NULL;
    /* Tamper: the tick check stops the gate; a foreign site is not restored over. */
    stats.issued=0;boot_phase=0;
    unsigned char keep[4];memcpy(keep,image+BPOSE_SITE+1,4);image[BPOSE_SITE+1]^=1;
    assert(!bpose_owned() && !bpose_check() && !strcmp(bpose_stage,"site_changed"));
    bpose_restore();assert(bpose_installed && bpose_done && !strcmp(bpose_stage,"restore_foreign"));
    memcpy(image+BPOSE_SITE+1,keep,4);bpose_restore();bpose_failures=0;
    assert(!bpose_installed && bpose_restored && !bpose_done && !memcmp(image+BPOSE_CREATE,create,sizeof(create)) && !bpose_on() && bpose_site_state()==0);
    char out[512];assert(bpose_status(out,sizeof(out))>0 && strstr(out,"mode=sim installed=0 restored=1 stage=restored"));
    test_bpose_counts_reset();bpose_mode=BPOSE_OFF;bpose_restored=0;bpose_stage="idle";bpose_last_time=0;
    if(bpose_page) {VirtualFree(bpose_page,0,MEM_RELEASE);bpose_page=NULL;}
    stats.active=saved_active;stats.issued=saved_issued;ui_thread=saved_ui;boot_mode=saved_boot;boot_phase=saved_phase;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("body pose on X4.exe: pins, the swap, view, the stub runs GetTransformRelativeTo at the MoveWorker's game time; other threads native; tamper, restore PASS");
}

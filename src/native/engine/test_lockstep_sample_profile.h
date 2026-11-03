/* native_gate_sample_profile: module tables and lookups against the OS's own, chained
 * owners, walks of a live suspended thread (marks, inner, leaf classes), bounds and
 * unknown code, the inner table's overflow, rows, status bounds, arm/sample/restore. */
static volatile LONG test_sprof_run;
static unsigned char *volatile test_sprof_worker_teb;
__attribute__((noinline)) static uint64_t test_sprof_leaf(uint64_t x) {
    while(InterlockedCompareExchange(&test_sprof_run,0,0)) x=x*6364136223846793005ULL+1442695040888963407ULL;
    return x;
}
__attribute__((noinline)) static uint64_t test_sprof_mid(uint64_t x) {uint64_t r=test_sprof_leaf(x+1);return r^(x*7);}
__attribute__((noinline)) static uint64_t test_sprof_top(uint64_t x) {uint64_t r=test_sprof_mid(x+2);return r+x*3;}
static DWORD WINAPI test_sprof_worker(LPVOID p) {
    test_sprof_worker_teb=(unsigned char *)NtCurrentTeb();
    return (DWORD)test_sprof_top((uint64_t)(uintptr_t)p);
}
static uint32_t test_sprof_start(const void *fn,uintptr_t base) {
    DWORD64 module=0;PRUNTIME_FUNCTION f=RtlLookupFunctionEntry((DWORD64)(uintptr_t)fn,&module,NULL);
    assert(f && module==base);return f->BeginAddress;
}
static void test_sample_profile(void) {
    const uintptr_t self=(uintptr_t)GetModuleHandleW(NULL);
    /* Tables: every loaded module, sorted, the test executable standing in for X4. */
    assert(sprof_tables((const unsigned char *)self) && sprof_module_count>=3 && sprof_x4!=UINT_MAX && sprof_modules[sprof_x4].base==self);
    for(unsigned i=1;i<sprof_module_count;i++) assert(sprof_modules[i-1].end<=sprof_modules[i].base);
    assert(sprof_module_of((uintptr_t)(void *)test_sprof_mid)==sprof_x4 && sprof_module_of(0x1000)==UINT_MAX);
    const uintptr_t k32=(uintptr_t)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetTickCount");
    const unsigned k32_index=sprof_module_of(k32);assert(k32_index!=UINT_MAX && k32_index!=sprof_x4);
    /* Lookups: the same RUNTIME_FUNCTION the OS finds, in the executable and in kernel32. */
    const uintptr_t probes[]={(uintptr_t)(void *)test_sprof_mid,(uintptr_t)(void *)test_sprof_top+3,(uintptr_t)(void *)test_sample_profile+40,k32};
    for(unsigned i=0;i<sizeof(probes)/sizeof(*probes);i++) {
        DWORD64 module=0;PRUNTIME_FUNCTION os=RtlLookupFunctionEntry((DWORD64)probes[i],&module,NULL);
        const unsigned index=sprof_module_of(probes[i]);assert(index!=UINT_MAX && (!os || sprof_modules[index].base==(uintptr_t)module));
        assert(sprof_entry(&sprof_modules[index],probes[i])==os);
    }
    const uint32_t mid=test_sprof_start(test_sprof_mid,self),top=test_sprof_start(test_sprof_top,self);
    assert(sprof_owner(&sprof_modules[sprof_x4],sprof_entry(&sprof_modules[sprof_x4],(uintptr_t)(void *)test_sprof_mid))==mid);
    /* A chained fragment answers its parent's start. */
    {   static __declspec(align(16)) unsigned char fake[0x200];memset(fake,0,sizeof(fake));
        RUNTIME_FUNCTION *table=(RUNTIME_FUNCTION *)(fake+0x100);
        table[0].BeginAddress=0x10;table[0].EndAddress=0x40;table[0].UnwindData=0x80;
        table[1].BeginAddress=0x40;table[1].EndAddress=0x60;table[1].UnwindData=0x90;
        fake[0x80]=1;                                      /* version 1, no chain */
        fake[0x90]=1|(UNW_FLAG_CHAININFO<<3);fake[0x92]=1; /* one code slot, rounded to two */
        memcpy(fake+0x90+4+4,&table[0],sizeof(RUNTIME_FUNCTION));
        sprof_module m={(uintptr_t)fake,(uintptr_t)fake+sizeof(fake),table,2};
        assert(sprof_entry(&m,(uintptr_t)fake+0x50)==&table[1] && sprof_owner(&m,&table[1])==0x10 && sprof_owner(&m,&table[0])==0x10);
        assert(!sprof_entry(&m,(uintptr_t)fake+0x60) && !sprof_entry(&m,(uintptr_t)fake+0x08));
        RUNTIME_FUNCTION parent;memcpy(&parent,fake+0x98,sizeof(parent));parent.UnwindData=0x7f00; /* outside the image: stop there */
        memcpy(fake+0x98,&parent,sizeof(parent));assert(sprof_owner(&m,&table[1])==0x10);
    }
    /* A live thread, suspended and walked: top -> mid -> leaf on its stack. */
    uint64_t saved_issued=stats.issued;uint32_t saved_active=stats.active;const LONGLONG saved_sim=gate_sim_started.QuadPart;
    gate_sim_started.QuadPart=0;
    uint32_t saved_rva[3];for(unsigned k=0;k<3;k++) saved_rva[k]=sprof_marks[k].rva;
    sprof_marks[0].rva=top;sprof_marks[1].rva=mid;sprof_marks[2].rva=0xffffffu;
    for(unsigned k=0;k<SPROF_MARKS;k++) sprof_marks[k].count=sprof_marks[k].step=0;
    memset(sprof_inner,0,sizeof(sprof_inner));
    sprof_samples=sprof_step=sprof_walked=sprof_leaf_x4=sprof_leaf_gate=sprof_leaf_other=sprof_leaf_unknown=0;
    sprof_truncated=sprof_unwalked=sprof_faults=sprof_failed=sprof_dropped=sprof_frames=sprof_pause_us=sprof_pause_max_us=0;
    test_sprof_run=1;test_sprof_worker_teb=NULL;
    HANDLE worker=CreateThread(NULL,0,test_sprof_worker,(LPVOID)(uintptr_t)5,0,NULL);assert(worker);
    while(!test_sprof_worker_teb) Sleep(1);
    Sleep(20);
    sprof_target=worker;sprof_teb=test_sprof_worker_teb;
    for(unsigned i=0;i<40;i++) {sprof_sample();Sleep(1);}
    InterlockedExchange(&test_sprof_run,0);WaitForSingleObject(worker,5000);CloseHandle(worker);sprof_target=NULL;sprof_teb=NULL;
    assert(sprof_samples==40 && sprof_walked==40 && !sprof_faults && !sprof_failed && !sprof_unwalked && !sprof_step);
    assert(sprof_leaf_x4>=36 && sprof_pause_us>0 && sprof_pause_max_us>0 && sprof_frames>=40*3);
    assert(sprof_marks[0].count>=36 && sprof_marks[1].count>=36 && !sprof_marks[2].count);
    {   const uint32_t leaf_rva=(uint32_t)((uintptr_t)(void *)test_sprof_leaf-self);
        DWORD64 module=0;PRUNTIME_FUNCTION f=RtlLookupFunctionEntry((DWORD64)(uintptr_t)(void *)test_sprof_leaf,&module,NULL);
        uint64_t leaf_samples=0;
        for(unsigned i=0;i<SPROF_INNER;i++) if(sprof_inner[i].count) {
            const uint32_t key=sprof_inner[i].key;
            if(f?key==f->BeginAddress:(key>=leaf_rva && key<leaf_rva+0x40)) leaf_samples+=sprof_inner[i].count;
        }
        assert(leaf_samples>=36); /* the spin loop's function (or, without unwind data, its pc) */
    }
    for(unsigned k=0;k<3;k++) sprof_marks[k].rva=saved_rva[k];
    for(unsigned k=0;k<SPROF_MARKS;k++) sprof_marks[k].count=sprof_marks[k].step=0;
    /* Bounds: end of stack, code in no module, a stack pointer outside the thread's stack. */
    {   CONTEXT c;memset(&c,0,sizeof(c));int why=-1;uintptr_t pcs[4];
        assert(sprof_walk(&c,0,~(uintptr_t)0,pcs,4,&why)==0 && why==0);
        c.Rip=0x1000;assert(sprof_walk(&c,0,~(uintptr_t)0,pcs,4,&why)==1 && why==2 && pcs[0]==0x1000);
        c.Rip=(DWORD64)(uintptr_t)(void *)test_sprof_mid;c.Rsp=0x5000;assert(sprof_walk(&c,0x10000,0x20000,pcs,4,&why)==1 && why==3);
    }
    /* Tally: a leaf in kernel32 under an X4 return address is a site; unknown and empty samples. */
    {   uint64_t before=sprof_leaf_other,site_count=0;const uint32_t site=(uint32_t)((uintptr_t)(void *)test_sprof_mid+5-self);
        uintptr_t pcs[2]={k32,(uintptr_t)(void *)test_sprof_mid+5};sprof_tally(pcs,2,0,1);
        assert(sprof_leaf_other==before+1 && sprof_step==1 && sprof_marks[0].count==0);
        for(unsigned i=0;i<SPROF_INNER;i++) if(sprof_inner[i].key==(SPROF_SITE|site)) site_count=sprof_inner[i].count;
        assert(site_count==1);
        uintptr_t lost[1]={0x1000};before=sprof_leaf_unknown;sprof_tally(lost,1,2,0);assert(sprof_leaf_unknown==before+1);
        before=sprof_unwalked;sprof_tally(NULL,0,0,0);assert(sprof_unwalked==before+1);
    }
    /* Rows: marks first, then the inner entries by count, ties by key; nothing past the end. */
    {   char row[192];memset(sprof_inner,0,sizeof(sprof_inner));
        for(unsigned i=0;i<5;i++) for(unsigned j=0;j<=i;j++) sprof_inner_add(0x1000u+i*0x10u,j&1);
        sprof_inner_add(SPROF_SITE|0x2000u,0);
        sprof_marks[0].count=7;sprof_marks[0].step=3;
        assert(sprof_row(0,row,sizeof(row))>0 && !strcmp(row,"index=0 kind=mark label=moveworker_run rva=ef4400 count=7 step=3"));
        assert(sprof_row((unsigned)SPROF_MARKS,row,sizeof(row))>0 && strstr(row,"kind=inner rva=1040 site=0 count=5 step=2"));
        assert(sprof_row((unsigned)SPROF_MARKS+4,row,sizeof(row))>0 && strstr(row,"rva=1000 site=0 count=1 step=0"));
        assert(sprof_row((unsigned)SPROF_MARKS+5,row,sizeof(row))>0 && strstr(row,"rva=2000 site=1 count=1 step=0"));
        assert(!sprof_row((unsigned)SPROF_MARKS+6,row,sizeof(row)));
        /* Overflow: a full table counts what it cannot hold. */
        memset(sprof_inner,0,sizeof(sprof_inner));sprof_dropped=0;
        for(uint32_t key=1;key<=SPROF_INNER+3;key++) sprof_inner_add(key,0);
        assert(sprof_dropped==3);
        unsigned full=0;for(unsigned i=0;i<SPROF_INNER;i++) full+=sprof_inner[i].count!=0;assert(full==SPROF_INNER);
        assert(sprof_row((unsigned)SPROF_MARKS,row,sizeof(row))>0 && sprof_top_count==SPROF_TOP);
    }
    /* Status at the widest values fits the Lua buffer. */
    {   char out[768];sprof_armed=1;sprof_period_ms=50;sprof_module_count=SPROF_MODULES;sprof_stage="stop_timeout";sprof_retargets=UINT64_MAX;
        sprof_samples=sprof_step=sprof_walked=sprof_leaf_x4=sprof_leaf_gate=sprof_leaf_other=sprof_leaf_unknown=UINT64_MAX;
        sprof_truncated=sprof_unwalked=sprof_faults=sprof_failed=sprof_dropped=sprof_frames=sprof_pause_us=sprof_pause_max_us=UINT64_MAX;
        int n=sprof_status(out,sizeof(out));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE SAMPLE_PROFILE tick=18446744073709551615 \n")<768);
        sprof_armed=0;sprof_stage="idle";sprof_retargets=0;
    }
    /* Arm on this thread (it stands in for the MoveWorker), sample while active, restore. */
    DWORD saved_ui=ui_thread;ui_thread=GetCurrentThreadId();stats.active=0;stats.issued=1;
    assert(!sprof_arm_on(2,(const unsigned char *)self)); /* inactive */
    stats.active=1;assert(!sprof_arm_on(0,(const unsigned char *)self) && !sprof_arm_on(51,(const unsigned char *)self));
    ui_thread=GetCurrentThreadId()+1;assert(!sprof_arm_on(2,(const unsigned char *)self));ui_thread=GetCurrentThreadId();
    assert(!sprof_arm_on(2,(const unsigned char *)0x10000) && !strcmp(sprof_stage,"tables")); /* no module there */
    assert(sprof_arm_on(2,(const unsigned char *)self) && sprof_armed && sprof_thread && !strcmp(sprof_stage,"sampling"));
    assert(sprof_arm_on(2,(const unsigned char *)self) && !sprof_arm_on(3,(const unsigned char *)self)); /* identical rerun only */
    for(unsigned i=0;i<200 && sprof_samples<5;i++) Sleep(5);
    assert(sprof_samples>=5 && sprof_target_id==GetCurrentThreadId() && !sprof_retargets);
    /* The gate adopts another MoveWorker (snapshot boot): the sampler follows ui_thread to it. */
    sprof_marks[1].rva=mid;sprof_marks[1].count=0;
    test_sprof_run=1;test_sprof_worker_teb=NULL;DWORD worker_id=0;
    worker=CreateThread(NULL,0,test_sprof_worker,(LPVOID)(uintptr_t)9,0,&worker_id);assert(worker);
    while(!test_sprof_worker_teb) Sleep(1);
    ui_thread=worker_id;
    for(unsigned i=0;i<400 && sprof_marks[1].count<5;i++) Sleep(5);
    assert(sprof_marks[1].count>=5 && sprof_retargets==1 && sprof_target_id==worker_id && sprof_teb==test_sprof_worker_teb);
    sprof_restore();
    InterlockedExchange(&test_sprof_run,0);WaitForSingleObject(worker,5000);CloseHandle(worker);
    sprof_marks[1].rva=saved_rva[1];ui_thread=GetCurrentThreadId();
    assert(sprof_samples>=10 && sprof_walked==sprof_samples && !sprof_faults && !sprof_failed && !strcmp(sprof_stage,"stopped"));
    assert(!sprof_thread && !sprof_target && !sprof_stop_event && !sprof_target_id);
    const uint64_t frozen=sprof_samples;Sleep(20);assert(sprof_samples==frozen); /* the sampler has ended */
    sprof_restore(); /* idempotent */
    sprof_armed=0;sprof_stage="idle";sprof_period_ms=0;
    for(unsigned k=0;k<SPROF_MARKS;k++) sprof_marks[k].count=sprof_marks[k].step=0;
    ui_thread=saved_ui;stats.issued=saved_issued;stats.active=saved_active;gate_sim_started.QuadPart=saved_sim;
    printf("sample profile tables/lookups/chains/live walk/bounds/tally/rows/overflow/status/arm/follow/restore tests PASS\n");
}

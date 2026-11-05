/* lockstep_join_passes.h: the kicks are MoveWorker::Run's (each task's conditions, its ready byte, SetEvent on a parked pass
 * thread), the arming only before the first tick, and the pass of T refused by name when not armed. */
static void test_join_passes(void) {
    static __declspec(align(16)) unsigned char traffic[0x200],region[0x200];
    void *saved_traffic=*(void**)(image+MT_TASK),*saved_region=*(void**)(image+MT_REGION_TASK);
    const int saved_traffic_mode=mt_traffic.mode,saved_region_mode=mt_region.mode;
    memset(traffic,0,sizeof(traffic));memset(region,0,sizeof(region));
    InitializeCriticalSection((CRITICAL_SECTION*)(traffic+MT_TASK_LOCK));InitializeCriticalSection((CRITICAL_SECTION*)(region+MT_REGION_LOCK));
    HANDLE traffic_wake=CreateEventW(NULL,TRUE,FALSE,NULL),region_wake=CreateEventW(NULL,TRUE,FALSE,NULL);
    assert(traffic_wake && region_wake);
    memcpy(traffic+0xa8,&traffic_wake,8);memcpy(region+0x160,&region_wake,8);
    *(void**)(image+MT_TASK)=traffic;*(void**)(image+MT_REGION_TASK)=region;mt_traffic.mode=mt_region.mode=MT_SYNC;
    const uint64_t kicks=jpass_kicks;const int32_t one=1,zero=0;
    /* Traffic: +0x18 set and +0x20 clear, no kick; +0x20 set, ready and parked: kicked, its wake set; not ready: nothing. */
    memcpy(traffic+0x18,&one,4);traffic[MT_READY]=1;memcpy(traffic+0xa0,&one,4);
    jpass_kick_traffic();assert(traffic[MT_READY]==1 && jpass_kicks==kicks && WaitForSingleObject(traffic_wake,0)==WAIT_TIMEOUT);
    memcpy(traffic+0x20,&one,4);
    jpass_kick_traffic();assert(!traffic[MT_READY] && jpass_kicks==kicks+1 && WaitForSingleObject(traffic_wake,0)==WAIT_OBJECT_0);
    jpass_kick_traffic();assert(jpass_kicks==kicks+1);
    /* Traffic idle task (+0x18 and +0x1c clear): kicked; a thread that is not parked gets no wake. */
    memcpy(traffic+0x18,&zero,4);memcpy(traffic+0x20,&zero,4);memcpy(traffic+0xa0,&zero,4);traffic[MT_READY]=1;ResetEvent(traffic_wake);
    jpass_kick_traffic();assert(!traffic[MT_READY] && jpass_kicks==kicks+2 && WaitForSingleObject(traffic_wake,0)==WAIT_TIMEOUT);
    /* Region: no work (+0xf8, +0xfc clear), no kick; work but busy (+0x144), no kick; work, idle, ready, parked: kicked. */
    region[MT_REGION_READY]=1;memcpy(region+0x158,&one,4);
    jpass_kick_region();assert(region[MT_REGION_READY]==1 && jpass_kicks==kicks+2);
    memcpy(region+0xfc,&one,4);memcpy(region+0x144,&one,4);
    jpass_kick_region();assert(region[MT_REGION_READY]==1 && jpass_kicks==kicks+2 && WaitForSingleObject(region_wake,0)==WAIT_TIMEOUT);
    memcpy(region+0x144,&zero,4);
    jpass_kick_region();assert(!region[MT_REGION_READY] && jpass_kicks==kicks+3 && WaitForSingleObject(region_wake,0)==WAIT_OBJECT_0);
    /* A pass that is not synced is never kicked. */
    mt_region.mode=MT_OBSERVE;region[MT_REGION_READY]=1;jpass_kick_region();assert(region[MT_REGION_READY]==1 && jpass_kicks==kicks+3);
    *(void**)(image+MT_TASK)=saved_traffic;*(void**)(image+MT_REGION_TASK)=saved_region;
    mt_traffic.mode=saved_traffic_mode;mt_region.mode=saved_region_mode;
    DeleteCriticalSection((CRITICAL_SECTION*)(traffic+MT_TASK_LOCK));DeleteCriticalSection((CRITICAL_SECTION*)(region+MT_REGION_LOCK));
    CloseHandle(traffic_wake);CloseHandle(region_wake);
    /* Arming: only before the first tick; it sets the admission's hook. Not armed: the pass of T refuses by name. */
    const uint64_t issued=stats.issued;
    jpass_armed=0;join_admit_passes=NULL;jpass_why="-";
    assert(!jpass_admit(7) && !strcmp(jpass_why,"not_armed"));
    stats.issued=5;assert(!jpass_arm() && !jpass_armed && !join_admit_passes);
    stats.issued=0;assert(jpass_arm() && jpass_armed && join_admit_passes==jpass_admit);
    jpass_armed=0;join_admit_passes=NULL;stats.issued=issued;jpass_why="-";
    printf("join passes: MoveWorker's traffic and region kicks (conditions, ready byte, wake of a parked thread), arming before the first tick PASS\n");
}

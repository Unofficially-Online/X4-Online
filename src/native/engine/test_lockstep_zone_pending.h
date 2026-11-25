/* native_gate_zone_pending: in the frame's own call on the simulation thread of a running session a busy lock is waited for,
 * ZPEND_WAIT_MS at most; a call from anywhere else, another thread and no session ask as X4 does. */
static HANDLE test_zp_held;
static DWORD test_zp_hold_ms;
static DWORD WINAPI test_zp_holder(void *p) {
    CRITICAL_SECTION *lock=p;EnterCriticalSection(lock);SetEvent(test_zp_held);Sleep(test_zp_hold_ms);LeaveCriticalSection(lock);return 0;
}
static HANDLE test_zp_hold(CRITICAL_SECTION *lock,DWORD ms) {
    test_zp_hold_ms=ms;ResetEvent(test_zp_held);HANDLE holder=CreateThread(NULL,0,test_zp_holder,lock,0,NULL);
    assert(holder && WaitForSingleObject(test_zp_held,5000)==WAIT_OBJECT_0);return holder;
}
static void test_zone_pending(void) {
    DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;CRITICAL_SECTION lock;InitializeCriticalSection(&lock);
    const unsigned char *frame=image+ZPEND_FRAME_RETURN,*nested=image+0x842035u;
    /* The stub's 32 bytes on the slots' near page are nobody else's (18 Nov: the area jobs' probe stub sat at the same offset). */
    for(unsigned i=0;i<AREAJ_SITES;i++) assert(areaj_sites[i].stub+0x20u<=ZPEND_STUB || ZPEND_STUB+0x20u<=areaj_sites[i].stub);
    assert(PVEHK_DUE_STUB+0x100u<=ZPEND_STUB && ZPEND_STUB+0x20u<=PSLOT_CTL_END_STUB && ZPEND_STUB+0x20u<=PVIEW_PAGE);
    ui_thread=GetCurrentThreadId();stats.active=1;zpend_calls=zpend_waited=zpend_lost=zpend_nested=zpend_other=0;
    assert(zpend_enter(&lock,frame)==TRUE && zpend_calls==1 && !zpend_waited && !zpend_other);LeaveCriticalSection(&lock);
    assert(zpend_enter(&lock,nested)==TRUE && !zpend_nested);LeaveCriticalSection(&lock);          /* a free lock: taken, whoever asks */
    test_zp_held=CreateEventW(NULL,TRUE,FALSE,NULL);assert(test_zp_held);
    HANDLE holder=test_zp_hold(&lock,60);
    assert(zpend_enter(&lock,frame)==TRUE && zpend_waited==1 && !zpend_lost);LeaveCriticalSection(&lock);   /* the frame's call: waited for */
    assert(WaitForSingleObject(holder,5000)==WAIT_OBJECT_0);CloseHandle(holder);
    holder=test_zp_hold(&lock,60);
    assert(zpend_enter(&lock,nested)==FALSE && zpend_nested==1 && zpend_waited==1);                 /* Zone slot 921's call: as X4, at once */
    ui_thread=GetCurrentThreadId()+1;assert(zpend_enter(&lock,frame)==FALSE && zpend_other==1);      /* not the simulation thread: as X4 asks */
    ui_thread=GetCurrentThreadId();stats.active=0;assert(zpend_enter(&lock,frame)==FALSE && zpend_other==2); /* no session running */
    stats.active=1;assert(WaitForSingleObject(holder,5000)==WAIT_OBJECT_0);CloseHandle(holder);
    /* A holder that does not let go: the frame's wait ends after ZPEND_WAIT_MS and the half is stepped over, counted. */
    holder=test_zp_hold(&lock,ZPEND_WAIT_MS+400);
    const ULONGLONG before=GetTickCount64();
    assert(zpend_enter(&lock,frame)==FALSE && zpend_waited==2 && zpend_lost==1);
    const ULONGLONG took=GetTickCount64()-before;assert(took>=ZPEND_WAIT_MS-20 && took<ZPEND_WAIT_MS+300);
    assert(WaitForSingleObject(holder,5000)==WAIT_OBJECT_0);CloseHandle(holder);CloseHandle(test_zp_held);
    char status[200];assert(zpend_status(status,sizeof(status))>0 && strstr(status,"installed=0/2 why=- calls=7 waited=2 lost=1 nested=1 other=2"));
    DeleteCriticalSection(&lock);zpend_calls=zpend_waited=zpend_lost=zpend_nested=zpend_other=0;ui_thread=saved_ui;stats.active=saved_active;
    puts("zone pending lock tests PASS");
}
/* The slots' near page: every module picks its stub offsets by hand, and a module armed second writes over the first
 * (18 Nov: the zone pending stub over the area jobs' probe stub; the first resource probe ended both clients). Every
 * stub and trampoline the modules place there, each in its own bytes and inside the page. */
static void test_near_page(void) {
    struct {const char *name;unsigned at,size;} used[80];unsigned n=0;
    #define NEAR_STUB(what,offset,bytes) do {assert(n<80);used[n].name=what;used[n].at=(offset);used[n].size=(bytes);n++;} while(0)
    NEAR_STUB("the slots' update",0,0x40);NEAR_STUB("refill",PRES_STUB_OFFSET,0x20);NEAR_STUB("pick",PRES_PICK_STUB,0x20);NEAR_STUB("game over",PDEATH_STUB,0x20);
    NEAR_STUB("foot update",PFSLOT_STUB,0x20);NEAR_STUB("kill guard",PFBODY_KILL_STUB,0x20);NEAR_STUB("lookup",PSLOT_LOOKUP_STUB,0x20);
    NEAR_STUB("enter",PVEHK_ENTER_STUB,0x20);NEAR_STUB("fade",PVEHK_FADE_STUB,0x20);NEAR_STUB("fade trampoline",PVEHK_FADE_TRAMPOLINE,0x20);
    NEAR_STUB("speech option",SPL_OPTION_STUB,0x20);NEAR_STUB("speech next",SPL_NEXT_STUB,0x20);
    NEAR_STUB("zone",PVEHK_ZONE_STUB,0x20);NEAR_STUB("zone trampoline",PVEHK_ZONE_TRAMPOLINE,0x20);
    for(unsigned i=0;i<PVEHK_DID_SITES;i++) NEAR_STUB("notice",pvehk_did_sites[i].stub,0x20);
    NEAR_STUB("scan",PVEHK_SCAN_STUB,0x20);NEAR_STUB("scan trampoline",PVEHK_SCAN_TRAMPOLINE,0x20);NEAR_STUB("media make",MDV_MAKE_STUB,0x20);
    for(unsigned i=0;i<MDV_UI;i++) {NEAR_STUB("media ui",mdv_ui[i].stub,0x20);NEAR_STUB("media ui trampoline",mdv_ui[i].trampoline,0x20);}
    NEAR_STUB("speech queue",SPL_QUEUE_STUB,0x20);NEAR_STUB("speech queue trampoline",SPL_QUEUE_TRAMPOLINE,0x20);NEAR_STUB("scan post",PVEHK_SCAN_POST_STUB,0x20);
    for(unsigned i=0;i<AREAJ_SITES;i++) NEAR_STUB("area job",areaj_sites[i].stub,0x20);
    NEAR_STUB("due lookup",PVEHK_DUE_STUB,0x20);NEAR_STUB("animation listeners",ANIML_STUB,0x20);NEAR_STUB("animation listeners trampoline",ANIML_TRAMPOLINE,0x20);
    NEAR_STUB("faction known to",FPRES_KNOWN_STUB,0x20);NEAR_STUB("faction info",FPRES_INFO_STUB,0x20);NEAR_STUB("faction info trampoline",FPRES_INFO_TRAMPOLINE,0x20);
    NEAR_STUB("faction known to trampoline",FPRES_KNOWN_TRAMPOLINE,0x20);NEAR_STUB("faction character lookup",FPRES_CAST_STUB,0x20);NEAR_STUB("faction character lookup trampoline",FPRES_CAST_TRAMPOLINE,0x20);
    NEAR_STUB("zone pending",ZPEND_STUB,0x20);NEAR_STUB("controller end",PSLOT_CTL_END_STUB,0x20);NEAR_STUB("controller end trampoline",PSLOT_CTL_END_TRAMPOLINE,0x20);
    NEAR_STUB("menu",PVEHK_MENU_STUB,0x20);NEAR_STUB("menu trampoline",PVEHK_MENU_TRAMPOLINE,0x20);
    NEAR_STUB("ui holds add",UIH_STUB_ADD,0x20);NEAR_STUB("ui holds drop",UIH_STUB_DROP,0x20);NEAR_STUB("transporter",PFTR_CARRY_STUB,0x20);NEAR_STUB("figure",FFIG_BODY_STUB,0x20);
    NEAR_STUB("undock events",UDSCR_STUB,0x20);NEAR_STUB("undock events trampoline",UDSCR_TRAMPOLINE,0x20);
    NEAR_STUB("media conversation",MDV_CONV_STUB,0x20);NEAR_STUB("subtitle pick",SPL_PICK_STUB,0x20);
    NEAR_STUB("sound source",PRES_SOURCE_STUB,PSND_STUB_ROOM);NEAR_STUB("computer engaged",PVEHK_COMPUTER_ENGAGED_STUB,PSND_STUB_ROOM);
    NEAR_STUB("computer disengaged",PVEHK_COMPUTER_DISENGAGED_STUB,0x20);
    NEAR_STUB("place events",PVEHK_PLACE_STUB,0x20);
    #undef NEAR_STUB
    for(unsigned i=0;i<n;i++) {
        assert(used[i].at+used[i].size<=PVIEW_PAGE);
        for(unsigned j=i+1;j<n;j++)
            if(used[i].at<used[j].at+used[j].size && used[j].at<used[i].at+used[i].size) {
                printf("near page: %s (0x%x) and %s (0x%x) share bytes\n",used[i].name,used[i].at,used[j].name,used[j].at);assert(0);
            }
    }
    printf("near page: %u stubs, none shares bytes; tests PASS\n",n);
}

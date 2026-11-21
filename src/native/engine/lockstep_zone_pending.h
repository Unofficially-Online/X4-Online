/* The frame's pending physics work on every frame (native_gate_zone_pending, a session default since 19 Nov; Windows gate).
 *
 * Soak before it became a default: a two-client session left alone (players idle, the NPC universe running) for 108,116
 * ticks, every digest part and every world census equal, waited=0 lost=0 on both clients; the wait path ran in sessions
 * with ten phantom players (waited= 8 and 11, lost=0, 9,026 ticks in sync), where the run without it parted at tick 3191
 * on the runtime IDs from the frame's CreateBody stack.
 *
 * Found with every zone at high attention (17 Nov): the runtime ID parts of two clients differed for single ticks
 * (ticks 10, 39, 58, 64, 85 of one session; allocation counts 14, 6 and 12 apart, equal again a tick later), always
 * through one stack: c0039 <- b8601 <- U::Destructible::CreateBody 0x51b85f <- 0x841060 <- 0x83ed8a <- UniverseFrameUpdate
 * 0x9fe42c. The same bodies were made on both clients, a tick apart; later such a session parted for good.
 *
 * Mechanism (X4 9.00, static). UniverseFrameUpdate calls 0x83e960 once a frame (0x9fe427; Zone slot 921 calls it at
 * 0x842030 too). It has two halves, each behind TryEnterCriticalSection (0x83e97b on the lock at 0x3949b28, 0x83ec3a on
 * the one at 0x3949b98): the first hands back what a list holds and posts an event for each zone of an array, the
 * second releases the body records of a list and then walks the zones that wait for their physics bodies (0x841110,
 * 0x840fd0 -> CreateBody). A half whose lock another thread holds at that moment (the threads that put finished work
 * on those lists) is skipped, and its zones wait for the next frame. Whether the lock is free is a matter of
 * microseconds, so two clients build the same bodies in different ticks: a played session rarely has anything
 * waiting there, a session that raises a thousand zones has every tick.
 *
 * The fix: both calls go through a stub that, on the simulation thread of a running session and in the frame's own call
 * (0x83e960 called from UniverseFrameUpdate, return address 0x9fe42c), waits for the lock when it is not free, so both
 * halves run in every frame on every client. Any other thread keeps TryEnterCriticalSection. What the lists hold when
 * the frame gets there is still the other threads' own (not looked at here); the walk of the waiting zones is the
 * simulation's.
 *
 * Only the frame's call, and never without an end (17 Nov, runs phantom6..8: both clients of a session with ten phantom
 * players stood still at tick 3191). Zone slot 921 calls 0x83e960 too (0x842030), there from an AI script's
 * LaunchDeployableAction through an attention change, with the first lock already held (0x842018), and the second lock
 * was held by a spline worker (SplineFlightControlModel 0xebfced..0xebfdb8) that did not let go: the first form of this
 * wrapper waited there for good, where X4's TryEnterCriticalSection steps over the half. So a call from anywhere else
 * asks as X4 does (nested=), and the frame's wait gives up after ZPEND_WAIT_MS and steps over the half as X4 would
 * (lost=, a watched counter: that frame's half may then run a tick apart on two clients, which is what X4 does anyway).
 *
 * The sites are six bytes (`call [rip+disp32]`) and the first one crosses a 16-byte block, so only the first five are
 * swapped (one compare-and-swap of a block) for `call stub`; the stub reads who called 0x83e960 (its return address:
 * seven pushes and 0x20 bytes above the body's stack pointer at both sites), moves its own return one byte further,
 * past the sixth, and jumps to the wrapper.
 * Fail closed: the function is pinned, both sites must be `call [rip+disp32]` to the import and the frame's call must be
 * `call 0x83e960`; a build that differs keeps X4's calls. Status installed=<sites>/2 why= calls= waited= lost= nested= other=. */
#define ZPEND_FN 0x83e960u
#define ZPEND_FN_SIZE 0x554u
#define ZPEND_FN_SHA "e7ce0741dc8ed391a44a006c4c67434c77d49d078b17a37f85e2a034a79ee2e2"
#define ZPEND_IMPORT 0x1bc14c8u   /* the import slot of TryEnterCriticalSection */
/* On the slots' near page, in the free stretch behind the due stub (0x500). It was at 0x4e0 until 18 Nov: the area jobs'
 * probe site took that offset on integration the same day this module was written, the module armed second won, and the
 * first resource probe an NPC dropped (phantom12, tick 3191) called this stub in place of its job stub: both clients ended
 * with an access violation at 0xb32b0 a tick later. So the stub's bytes must be untouched (or this module's) before
 * they are written: why=stub_taken. It was at 0x600 until 19 Nov: the faction presentation's character lookup stub
 * (lockstep_faction_present.h, a session default) took that offset the same day; test_near_page() lists both now. */
#define ZPEND_STUB 0x700u
#define ZPEND_FRAME_CALL 0x9fe427u /* UniverseFrameUpdate: call 0x83e960 */
#define ZPEND_FRAME_RETURN 0x9fe42cu
#define ZPEND_WAIT_MS 250u
#define ZPEND_SITES 2u
static const unsigned zpend_sites[ZPEND_SITES]={0x83e97bu,0x83ec3au};
static int zpend_installed[ZPEND_SITES];
static unsigned char zpend_original[ZPEND_SITES][6],zpend_patched[ZPEND_SITES][5];
static const char *zpend_why="-";
static volatile LONG64 zpend_calls,zpend_waited,zpend_lost,zpend_nested,zpend_other;
/* The lock as X4 asks for it, except in the frame's own call on the simulation thread of a running session: there a busy
 * lock is waited for, ZPEND_WAIT_MS at most. caller: the return address of the 0x83e960 call this one sits in. */
static BOOL zpend_enter(CRITICAL_SECTION *lock,const unsigned char *caller) {
    InterlockedIncrement64(&zpend_calls);
    if(GetCurrentThreadId()!=ui_thread || !stats.active) {InterlockedIncrement64(&zpend_other);return TryEnterCriticalSection(lock);}
    if(TryEnterCriticalSection(lock)) return TRUE;
    if(caller!=image+ZPEND_FRAME_RETURN) {InterlockedIncrement64(&zpend_nested);return FALSE;}
    InterlockedIncrement64(&zpend_waited);
    for(const ULONGLONG start=GetTickCount64();GetTickCount64()-start<ZPEND_WAIT_MS;) {
        Sleep(1);
        if(TryEnterCriticalSection(lock)) return TRUE;
    }
    InterlockedIncrement64(&zpend_lost);return FALSE;
}
static int zpend_installed_count(void) {int n=0;for(unsigned i=0;i<ZPEND_SITES;i++) n+=zpend_installed[i]!=0;return n;}
/* gate.zone_pending(1), once the pilot modules know the game's image. 1: both sites are in (or were). */
static int zpend_arm(void) {
    if(zpend_installed_count()==(int)ZPEND_SITES) return 1;
    if(!pnat_base) {zpend_why="no_image";return 0;}
    if(!pslot_page && !(pslot_page=pslot_alloc_page())) {zpend_why="page";return 0;}
    if(!hash_ok(image+ZPEND_FN,ZPEND_FN_SIZE,ZPEND_FN_SHA)) {zpend_why="pin";return 0;}
    int32_t frame_rel=0;memcpy(&frame_rel,image+ZPEND_FRAME_CALL+1,4);
    if(image[ZPEND_FRAME_CALL]!=0xe8 || (intptr_t)ZPEND_FRAME_CALL+5+frame_rel!=(intptr_t)ZPEND_FN) {zpend_why="frame_call";return 0;}
    DWORD old,ignored;unsigned char *stub=pslot_page+ZPEND_STUB;
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) {zpend_why="page_protect";return 0;}
    /* mov rdx,[rsp+0x60] (who called 0x83e960); add qword [rsp],1; jmp [rip+8]; the wrapper's address at +0x18 */
    static const unsigned char code[24]={0x48,0x8b,0x54,0x24,0x60, 0x48,0x83,0x04,0x24,0x01, 0xff,0x25,0x08,0x00,0x00,0x00, 0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc};
    void *wrapper=(void*)zpend_enter;unsigned char want[32],empty[32]={0};memcpy(want,code,sizeof(code));memcpy(want+0x18,&wrapper,8);
    if(memcmp(stub,empty,sizeof(empty)) && memcmp(stub,want,sizeof(want))) {
        VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored);zpend_why="stub_taken";return 0;
    }
    memcpy(stub,want,sizeof(want));
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) {zpend_why="page_protect";return 0;}
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    zpend_why="-";
    for(unsigned i=0;i<ZPEND_SITES;i++) {
        if(zpend_installed[i]) continue;
        const unsigned site=zpend_sites[i];
        int32_t disp=(int32_t)((intptr_t)ZPEND_IMPORT-(intptr_t)(site+6u));
        zpend_original[i][0]=0xff;zpend_original[i][1]=0x15;memcpy(zpend_original[i]+2,&disp,4);
        int32_t rel=(int32_t)((intptr_t)stub-(intptr_t)(pnat_base+site+5));
        zpend_patched[i][0]=0xe8;memcpy(zpend_patched[i]+1,&rel,4);
        if(memcmp(pnat_base+site,zpend_original[i],6)) {zpend_why="site";continue;}
        if(pview_cas(site,zpend_original[i],zpend_patched[i],5)) zpend_installed[i]=1;
        else zpend_why="site_swap";
    }
    return zpend_installed_count()==(int)ZPEND_SITES;
}
static void zpend_restore(void) {
    for(unsigned i=0;i<ZPEND_SITES;i++)
        if(zpend_installed[i] && pview_cas(zpend_sites[i],zpend_patched[i],zpend_original[i],5)) zpend_installed[i]=0;
}
static int zpend_status(char *out,size_t capacity) {
    return snprintf(out,capacity,"installed=%d/%u why=%s calls=%lld waited=%lld lost=%lld nested=%lld other=%lld",zpend_installed_count(),ZPEND_SITES,zpend_why,
                    (long long)zpend_calls,(long long)zpend_waited,(long long)zpend_lost,(long long)zpend_nested,(long long)zpend_other);
}

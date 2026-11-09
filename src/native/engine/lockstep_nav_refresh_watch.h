/* NAV_REFRESH (diagnostic, DEFAULT OFF: native_gate_nav_refresh_watch = {from,to}): which thread runs a
 * NavigationController's refresh first after each pathing bank flip.
 *
 * RefreshFromPathingState 0xea2a20 (rcx the controller) refreshes when the calling thread's game time (TLS+0x300) is
 * past the stamp +0xc0 and the read selector 0x38f4ee4 differs from the cursor +0xc8: it stores that thread's time into
 * +0xc0 and the selector into +0xc8 (0xea2aa6/0xea2ab3), re-parents the ship (0xea35e0) and posts events. Any transform
 * query (GetTransformImpl 0xea5a90) or Move runs it first, on whatever thread asks; path_determinism bit 1 stamps the
 * batched agents on the main thread after the pathing join, the others stay lazy [w1z151/j1-335: Zone::GetSafePos's
 * ship boxes differ in rotation on the joiner without breakpoints, w1z153: equal with them].
 *
 * Entry hook (count and record only, the original runs unchanged): the first five bytes, mov [rsp+0x20],rsi (no rip
 * operand), become jmp rel32 to a stub on our page; the trampoline replays them and jumps to 0xea2a25. Each call counts
 * by thread (the UI thread, others) and, predicted from the same test the original makes, whether it refreshes. A
 * refreshing call while stats.issued lies in [from,to] writes a row: tick, thread, ui, caller (return address less the
 * image), the controller's object runtime id ([+0x28]+8), the selector, the cursor and stamp before (hex bits), the
 * calling thread's time (hex bits; '-' before its TLS block is set up), and won (+0xc8 equals the selector after the
 * original: 0 when another thread stored after it). Rows go to the sink (the crash notes) at each gate tick.
 * native_gate_nav_refresh_watch_owner = runtime ID (hj23, 9 Nov): rows only for that object's controller, each with the
 * stack in the image (frames=, RtlCaptureStackBackTrace as the runtime IDs' callers): who reads a slot ship's transform
 * on a joiner from H-1 (its PlayerController refreshed every tick there, its stamp still on the session).
 * Pins: the function's 537 bytes (the join's and the world hash's pin) through the view; the entry raw. Installed and
 * restored with every other thread suspended (portable math's freeze) and none in the 5-byte window; armed on the UI
 * thread before the first admitted frame; any change at a gate tick stops the gate with reason 198. */
#define NRW_STOP_REASON 198u
#define NRW_ENTRY 0xea2a20u
#define NRW_RESUME 0xea2a25u
#define NRW_SIZE 537u
#define NRW_SHA "d301bea94b279ddce92ced403bafdd17a90ed2eb83c5cc5944cf2ae600fc4a4f"
#define NRW_SELECTOR 0x38f4ee4u
#define NRW_STAMP 0xc0u
#define NRW_CURSOR 0xc8u
#define NRW_OWNER 0x28u
#define NRW_PAGE_ANCHOR 0xea0000u
#define NRW_STUB 0x00u
#define NRW_TRAMP 0x20u
#define NRW_CODE 0x40u
#define NRW_CAP ((size_t)32<<20)
static const unsigned char nrw_original[5]={0x48,0x89,0x74,0x24,0x20};
typedef uint64_t (*nrw_fn)(void *,void *,void *,void *);
static int nrw_installed,nrw_restored;
static unsigned char *nrw_page;
static volatile LONG nrw_any;
static uint64_t nrw_from,nrw_to,nrw_owner_only;
#define NRW_FRAMES 14u
static void (*nrw_sink)(const char *,int);
static SRWLOCK nrw_lock=SRWLOCK_INIT;
static char *nrw_buffer;static size_t nrw_used,nrw_capacity;
static volatile LONG64 nrw_calls_ui,nrw_calls_other,nrw_refresh_ui,nrw_refresh_other,nrw_lost,nrw_rows,nrw_dropped,nrw_failures;
static const char *nrw_stage="idle";

/* The calling thread's game time (the X4 TLS block +0x300 once its +0x10 flag is set); 0 when not yet. */
static int nrw_thread_now(double *now) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);   /* direct reads: the original reads the same words next */
    if(!slots || !slots[0] || !((volatile unsigned char*)slots[0])[0x10]) return 0;
    memcpy(now,(const unsigned char*)slots[0]+0x300,8);return 1;
}
typedef struct {int ok,refresh,have_now;double now,stamp;uint32_t sel,cursor;uint64_t owner;} nrw_before;
/* The original's test, read before it runs (0xea2a55..0xea2a75: now > stamp ordered, selector != cursor). */
static void nrw_read_before(const unsigned char *nav,uint32_t sel,int have_now,double now,nrw_before *b) {
    memset(b,0,sizeof(*b));b->sel=sel;b->have_now=have_now;b->now=now;
    unsigned char *owner=NULL;   /* direct reads: the controller's words the original reads next */
    if(!nav) return;
    memcpy(&b->stamp,nav+NRW_STAMP,8);memcpy(&b->cursor,nav+NRW_CURSOR,4);memcpy(&owner,nav+NRW_OWNER,8);
    if(owner) memcpy(&b->owner,owner+8,8);
    b->ok=1;b->refresh=have_now && now>b->stamp && sel!=b->cursor;
}
static int nrw_row(char *out,size_t capacity,uint64_t tick,int ui,uintptr_t caller,const nrw_before *b,int won,const char *frames) {
    uint64_t sb,nb;memcpy(&sb,&b->stamp,8);memcpy(&nb,&b->now,8);char now_text[20];
    if(b->have_now) snprintf(now_text,sizeof(now_text),"%016llx",(unsigned long long)nb);else strcpy(now_text,"-");
    const int n=snprintf(out,capacity,"[X4LAN] NATIVE_GATE NAV_REFRESH tick=%llu thread=%lu ui=%d caller=%llx nav=%016llx sel=%u cursor_before=%u t_before=%016llx now=%s won=%d%s%s\r\n",
        (unsigned long long)tick,(unsigned long)GetCurrentThreadId(),ui,(unsigned long long)caller,(unsigned long long)b->owner,
        b->sel,b->cursor,(unsigned long long)sb,now_text,won,frames[0]?" frames=":"",frames);
    return n>0 && (size_t)n<capacity?n:0;
}
/* The calling stack's frames in the image, as image offsets (the owner filter's rows). */
static void nrw_frames(char *out,size_t capacity) {
    void *stack[NRW_FRAMES+4];const USHORT got=RtlCaptureStackBackTrace(1,NRW_FRAMES+4,stack,NULL);
    const uintptr_t low=(uintptr_t)image,high=low+0x6fa9000u;size_t at=0;unsigned n=0;out[0]=0;
    for(USHORT i=0;i<got && n<NRW_FRAMES && at+12<capacity;i++)
        if((uintptr_t)stack[i]>=low && (uintptr_t)stack[i]<high)
            at+=(size_t)snprintf(out+at,capacity-at,"%s%llx",n++?",":"",(unsigned long long)((uintptr_t)stack[i]-low));
}
static void nrw_append(const char *row,int n) {
    AcquireSRWLockExclusive(&nrw_lock);
    if(nrw_used+(size_t)n>nrw_capacity && nrw_capacity<NRW_CAP) {
        size_t want=nrw_capacity?nrw_capacity*2:(size_t)1<<20;while(want<nrw_used+(size_t)n) want*=2;
        char *grown=(char*)realloc(nrw_buffer,want);if(grown) {nrw_buffer=grown;nrw_capacity=want;}
    }
    if(nrw_used+(size_t)n<=nrw_capacity) {memcpy(nrw_buffer+nrw_used,row,(size_t)n);nrw_used+=(size_t)n;InterlockedIncrement64(&nrw_rows);}
    else InterlockedIncrement64(&nrw_dropped);
    ReleaseSRWLockExclusive(&nrw_lock);
}
static void nrw_flush(void) {
    if(!nrw_sink) return;
    AcquireSRWLockExclusive(&nrw_lock);
    if(nrw_used) {nrw_sink(nrw_buffer,(int)nrw_used);nrw_used=0;}
    ReleaseSRWLockExclusive(&nrw_lock);
}
static __attribute__((noinline)) uint64_t nrw_hook(void *nav,void *a2,void *a3,void *a4) {
    const int ui=GetCurrentThreadId()==ui_thread;
    InterlockedIncrement64(ui?&nrw_calls_ui:&nrw_calls_other);
    uint32_t sel=0;double now=0.0;nrw_before b;
    const int have_now=nrw_thread_now(&now);sel=*(volatile uint32_t*)(image+NRW_SELECTOR);
    nrw_read_before((const unsigned char*)nav,sel,have_now,now,&b);
    const uintptr_t caller=(uintptr_t)__builtin_return_address(0)-(uintptr_t)image;
    const uint64_t result=((nrw_fn)(void*)(nrw_page+NRW_TRAMP))(nav,a2,a3,a4);
    if(b.ok && b.refresh) {
        InterlockedIncrement64(ui?&nrw_refresh_ui:&nrw_refresh_other);
        const uint32_t after=*(volatile uint32_t*)((unsigned char*)nav+NRW_CURSOR);const int won=after==sel;
        if(!won) InterlockedIncrement64(&nrw_lost);
        const uint64_t tick=stats.issued;
        if(nrw_to && tick>=nrw_from && tick<=nrw_to && (!nrw_owner_only || b.owner==nrw_owner_only)) {
            char frames[NRW_FRAMES*12+4];frames[0]=0;if(nrw_owner_only) nrw_frames(frames,sizeof(frames));
            char row[512];const int n=nrw_row(row,sizeof(row),tick,ui,caller,&b,won,frames);
            if(n) nrw_append(row,n);else InterlockedIncrement64(&nrw_dropped);
        }
    }
    return result;
}
/* ---- the page and the entry (lockstep_safe_pos_order.h's scheme) ---- */
static void nrw_code(unsigned char out[NRW_CODE]) {
    memset(out,0xcc,NRW_CODE);
    spo_jump_to(out+NRW_STUB,(const void*)nrw_hook);
    memcpy(out+NRW_TRAMP,nrw_original,5);spo_jump_to(out+NRW_TRAMP+5,(const void*)(image+NRW_RESUME));
}
static int nrw_reach(const unsigned char *page) {
    const intptr_t d=(intptr_t)(page+NRW_STUB)-(intptr_t)(image+NRW_ENTRY+5);
    return d>INT32_MIN+0x2000 && d<INT32_MAX-0x2000;
}
static unsigned char *nrw_alloc_page(void) {
    const uintptr_t at=(uintptr_t)(image+NRW_PAGE_ANCHOR)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            const uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            if(nrw_reach(p)) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int nrw_write_page(void) {
    DWORD old,ignored;
    if(!VirtualProtect(nrw_page,0x1000,PAGE_READWRITE,&old)) return 0;
    memset(nrw_page,0xcc,0x1000);nrw_code(nrw_page);
    if(!VirtualProtect(nrw_page,0x1000,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),nrw_page,0x1000);return 1;
}
static int nrw_page_ok(void) {unsigned char want[NRW_CODE];if(!nrw_page) return 0;nrw_code(want);return !memcmp(nrw_page,want,NRW_CODE);}
static void nrw_patch_bytes(unsigned char out[5]) {
    const int32_t rel=(int32_t)((intptr_t)(nrw_page+NRW_STUB)-(intptr_t)(image+NRW_ENTRY+5));out[0]=0xe9;memcpy(out+1,&rel,4);
}
static int nrw_state(void) {
    unsigned char now[5],ours[5];memcpy(now,(const void*)(image+NRW_ENTRY),5);
    if(!memcmp(now,nrw_original,5)) return 0;
    if(nrw_page && nrw_reach(nrw_page)) {nrw_patch_bytes(ours);if(!memcmp(now,ours,5)) return 1;}
    return -1;
}
static int nrw_pins(void) {
    static unsigned char body[NRW_SIZE];
    return read_local(image+NRW_ENTRY,body,NRW_SIZE) && hash_ok(body,NRW_SIZE,NRW_SHA) && !memcmp(body,nrw_original,5);
}
static int nrw_overlaps(const void *source,size_t bytes) {
    if(!*(volatile LONG*)&nrw_any || !image || !source || !bytes) return 0;
    const uintptr_t a=(uintptr_t)source,lo=(uintptr_t)(image+NRW_ENTRY),hi=lo+5;
    return a<hi && a+bytes>lo;
}
static void nrw_unpatch_copy(const void *source,void *copy,size_t bytes) {
    if(!nrw_overlaps(source,bytes)) return;
    const uintptr_t a=(uintptr_t)source,lo=(uintptr_t)(image+NRW_ENTRY);
    for(unsigned i=0;i<5;i++) if(lo+i>=a && lo+i<a+bytes) ((unsigned char*)copy)[lo+i-a]=nrw_original[i];
}
static int nrw_window_clear(void) {
    const uintptr_t entry=(uintptr_t)(image+NRW_ENTRY);
    for(unsigned i=0;i<pmath_frozen_count;i++) {
        CONTEXT c;memset(&c,0,sizeof(c));c.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(pmath_frozen[i],&c)) return 0;
        if(c.Rip>entry && c.Rip<entry+5) return 0;
    }
    return 1;
}
static int nrw_switch(int install) {
    unsigned char ours[5];nrw_patch_bytes(ours);
    if(install) InterlockedExchange(&nrw_any,1);
    for(unsigned attempt=0;attempt<200;attempt++) {
        if(!pmath_freeze()) break;
        if(!nrw_window_clear()) {pmath_thaw();Sleep(1);continue;}
        const int ok=view_cas_bytes(NRW_ENTRY,install?nrw_original:ours,install?ours:nrw_original,5);
        pmath_thaw();
        if(ok!=install) InterlockedExchange(&nrw_any,0);
        return ok;
    }
    if(install) InterlockedExchange(&nrw_any,0);
    return 0;
}
/* gate.nav_refresh_watch(from,to): UI thread, before the first admitted frame. Armed again with the same window (the
 * probe's arming block runs twice, hj16 9 Nov: x4ls play refused at 0.00) it answers 1 and changes nothing; another window is
 * refused. */
static int nrw_arm(double from,double to) {
    if(!isfinite(from) || !isfinite(to) || floor(from)!=from || floor(to)!=to || from<1 || to<from || to>1e9) {if(!nrw_installed) nrw_stage="window";return 0;}
    if(nrw_installed) return stats.active && (uint64_t)from==nrw_from && (uint64_t)to==nrw_to;
    if(!stats.active || stats.issued || (boot_mode && boot_phase) || GetCurrentThreadId()!=ui_thread) {nrw_stage="state";return 0;}
    if(!nrw_pins()) {nrw_stage="pins";return 0;}
    if(nrw_state()!=0) {nrw_stage="entry";return 0;}
    if(!nrw_page) nrw_page=nrw_alloc_page();
    if(!nrw_page || !nrw_write_page() || !nrw_page_ok()) {nrw_stage="page";return 0;}
    nrw_from=(uint64_t)from;nrw_to=(uint64_t)to;
    nrw_calls_ui=nrw_calls_other=nrw_refresh_ui=nrw_refresh_other=nrw_lost=nrw_rows=nrw_dropped=nrw_failures=0;nrw_restored=0;
    if(!nrw_switch(1)) {nrw_stage="busy";return 0;}
    nrw_installed=1;nrw_stage="armed";return 1;
}
/* gate.nav_refresh_watch_window(from,to): another window for an installed watch (native_gate_nav_refresh_watch_handover: the
 * probe's handover callback, the same tick on every client, sets H-N..H+N once H is known). */
static int nrw_window(double from,double to) {
    if(!nrw_installed || !isfinite(from) || !isfinite(to) || floor(from)!=from || floor(to)!=to || from<1 || to<from || to>1e9) return 0;
    nrw_from=(uint64_t)from;nrw_to=(uint64_t)to;return 1;
}
static int nrw_owned(void) {return !nrw_installed || (nrw_state()==1 && nrw_page_ok());}
static int nrw_check(void) {
    nrw_flush();
    if(InterlockedCompareExchange64(&nrw_failures,0,0)) return 0;
    if(!nrw_owned()) {nrw_stage="site_changed";InterlockedIncrement64(&nrw_failures);return 0;}
    return 1;
}
static void nrw_restore(void) {
    if(!nrw_installed) return;
    if(nrw_state()!=1) {nrw_stage="restore_foreign";return;}
    if(nrw_switch(0)) {nrw_installed=0;nrw_restored=1;nrw_stage="restored";nrw_flush();}
    else nrw_stage="restore_busy";
}
static int nrw_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity,"installed=%d restored=%d stage=%s window=%llu:%llu owner=%llx calls_ui=%lld calls_other=%lld refresh_ui=%lld refresh_other=%lld lost=%lld rows=%lld dropped=%lld failures=%lld",
        nrw_installed,nrw_restored,nrw_stage,(unsigned long long)nrw_from,(unsigned long long)nrw_to,(unsigned long long)nrw_owner_only,
        (long long)nrw_calls_ui,(long long)nrw_calls_other,
        (long long)nrw_refresh_ui,(long long)nrw_refresh_other,(long long)nrw_lost,(long long)nrw_rows,(long long)nrw_dropped,(long long)nrw_failures);
    return n>0 && (size_t)n<capacity?n:0;
}

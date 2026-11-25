/* native_gate_body_watch = {'<saved ID, 16 hex>', which, first, last} (diagnostic, Windows gate, default off; needs
 * native_gate_physics_trace >= first): hardware write watches on one Jolt body's position (its x word, body +0x00, and its
 * z word, +0x08), DR2 and DR3 on every thread of the process, from the end of tick first's physics step to the end of tick
 * last's. The body is the physics trace's: the step list body whose owner has that saved ID and whose record is `which`
 * (0 the owner's +0x260 record, 1 its +0x268, 2 another: ptrace_identity). A vectored handler keeps one BODY_WATCH_ROW
 * per write: the tick, the thread, the RVA after the writing instruction (a data breakpoint traps after it), the caller
 * chain unwound from the trap's context (image RVAs, outermost last) and the x/y/z words written. Why (rk10, 25 Nov,
 * continues of c3-t18300): station BHC-891's build module 0x3f4bd has a third body record, a sensor loaded 67.8 km off;
 * between tick 1's step and tick 2's one client moves it onto the module and the other does not. DR0/DR1 stay the bank
 * watch's (lockstep_bank_watch_win.h). The calling thread's registers are set by a helper thread while it waits (a
 * thread cannot set its own debug registers by SetThreadContext); threads created after the arm are not watched. */
#include <tlhelp32.h>
#define BDW_ROWS 1024u
#define BDW_FRAMES 10u
typedef struct {uint64_t tick;uint32_t thread,rva,word,frames[BDW_FRAMES];unsigned count;float pos[3];} bdw_row;
static bdw_row bdw_rows[BDW_ROWS];
static volatile LONG64 bdw_count;
static uint64_t bdw_saved,bdw_first,bdw_last;
static unsigned bdw_which;
static unsigned char *bdw_body;
static int bdw_configured,bdw_armed;
static unsigned bdw_threads;
static void *bdw_veh;
static const char *bdw_stage="idle";
static uint32_t bdw_rva(uintptr_t at) {return at>=(uintptr_t)image && at<(uintptr_t)image+0x6fa9000?(uint32_t)(at-(uintptr_t)image):0xffffffffu;}
static LONG CALLBACK bdw_handler(EXCEPTION_POINTERS *e) {
    if(e->ExceptionRecord->ExceptionCode!=EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c=e->ContextRecord;const DWORD64 dr6=c->Dr6;
    if(!(dr6&0xcu)) return EXCEPTION_CONTINUE_SEARCH;   /* DR2/DR3: ours; DR0/DR1 the bank watch's */
    const LONG64 i=InterlockedIncrement64(&bdw_count)-1;
    if(i>=0 && i<(LONG64)BDW_ROWS) {
        bdw_row *r=&bdw_rows[i];
        r->tick=stats.issued;r->thread=GetCurrentThreadId();r->rva=bdw_rva((uintptr_t)c->Rip);r->word=(dr6&4u)?0u:2u;r->count=0;
        if(bdw_body) read_local(bdw_body,r->pos,12);
        CONTEXT walk=*c;
        for(unsigned f=0;f<BDW_FRAMES && walk.Rip;f++) {
            DWORD64 module=0;PRUNTIME_FUNCTION fn=RtlLookupFunctionEntry(walk.Rip,&module,NULL);
            if(fn) {PVOID handler=NULL;DWORD64 frame=0;RtlVirtualUnwind(UNW_FLAG_NHANDLER,module,walk.Rip,fn,&walk,&handler,&frame,NULL);}
            else {DWORD64 back=0;if(!read_local((const void *)(uintptr_t)walk.Rsp,&back,8)) break;walk.Rip=back;walk.Rsp+=8;}
            if(!walk.Rip) break;
            r->frames[r->count++]=bdw_rva((uintptr_t)walk.Rip);
        }
    }
    c->Dr6=0;return EXCEPTION_CONTINUE_EXECUTION;
}
static int bdw_set_thread(DWORD tid,int on) {
    HANDLE h=OpenThread(THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_SUSPEND_RESUME,FALSE,tid);int ok=0;
    if(!h) return 0;
    if(SuspendThread(h)!=(DWORD)-1) {
        CONTEXT c;memset(&c,0,sizeof(c));c.ContextFlags=CONTEXT_DEBUG_REGISTERS;
        if(GetThreadContext(h,&c)) {
            c.Dr7&=~(DWORD64)0xff0000f0u;   /* L2, G2, L3, G3; RW/LEN 2 and 3 */
            if(on) {c.Dr2=(DWORD64)(uintptr_t)bdw_body;c.Dr3=(DWORD64)(uintptr_t)(bdw_body+8);c.Dr7|=0x10u|0x40u|(1u<<24)|(3u<<26)|(1u<<28)|(3u<<30);}  /* write, 4 bytes */
            else {c.Dr2=c.Dr3=0;}
            ok=SetThreadContext(h,&c)!=0;
        }
        ResumeThread(h);
    }
    CloseHandle(h);return ok;
}
static volatile LONG bdw_helper_on;
static DWORD WINAPI bdw_helper(void *param) {return (DWORD)bdw_set_thread((DWORD)(uintptr_t)param,(int)bdw_helper_on);}
static unsigned bdw_set_all(int on) {
    const DWORD self=GetCurrentThreadId(),pid=GetCurrentProcessId();unsigned n=0;
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snap!=INVALID_HANDLE_VALUE) {
        THREADENTRY32 t;t.dwSize=sizeof(t);
        if(Thread32First(snap,&t)) do {
            if(t.th32OwnerProcessID==pid && t.th32ThreadID!=self) n+=(unsigned)bdw_set_thread(t.th32ThreadID,on);
        } while(Thread32Next(snap,&t));
        CloseHandle(snap);
    }
    InterlockedExchange(&bdw_helper_on,on);
    HANDLE helper=CreateThread(NULL,0,bdw_helper,(void *)(uintptr_t)self,0,NULL);
    if(helper) {DWORD code=0;WaitForSingleObject(helper,5000);if(GetExitCodeThread(helper,&code) && code) ++n;CloseHandle(helper);}
    return n;
}
/* The physics trace's step list (ptrace_begin, MoveWorker): the watched owner's body. */
static void bdw_note(unsigned char *body,uint64_t key,unsigned which) {
    if(bdw_configured && !bdw_armed && !bdw_body && key==bdw_saved && which==bdw_which) bdw_body=body;
}
/* After a physics step (ptrace_end, MoveWorker): arm after tick first's, disarm after tick last's. */
static void bdw_step_end(uint64_t tick) {
    if(!bdw_configured) return;
    if(!bdw_armed && tick>=bdw_first && tick<=bdw_last) {
        if(!bdw_body) {bdw_stage="unresolved";return;}
        if(!bdw_veh) bdw_veh=AddVectoredExceptionHandler(1,bdw_handler);
        if(!bdw_veh) {bdw_stage="handler";bdw_configured=0;return;}
        bdw_threads=bdw_set_all(1);bdw_armed=1;bdw_stage="armed";
    } else if(bdw_armed && tick>=bdw_last) {
        bdw_set_all(0);bdw_armed=0;bdw_configured=0;bdw_stage="done";
    }
}
static void bdw_restore(void) {
    if(bdw_armed) {bdw_set_all(0);bdw_armed=0;}
    if(bdw_veh) {RemoveVectoredExceptionHandler(bdw_veh);bdw_veh=NULL;}
    bdw_configured=0;
}
static int bdw_configure(uint64_t saved,unsigned which,uint64_t first,uint64_t last) {
    if(bdw_armed || !saved || which>2 || !first || last<=first || last-first>600) return 0;
    if(bdw_configured) return saved==bdw_saved && which==bdw_which && first==bdw_first && last==bdw_last;   /* the probe's second pass */
    bdw_saved=saved;bdw_which=which;bdw_first=first;bdw_last=last;bdw_body=NULL;bdw_count=0;bdw_configured=1;bdw_stage="configured";
    return 1;
}
static int bdw_text(uint64_t index,char *out,size_t cap) {
    const LONG64 count=InterlockedCompareExchange64(&bdw_count,0,0);
    if((LONG64)index>=count || index>=BDW_ROWS) return 0;
    const bdw_row *r=&bdw_rows[index];
    int n=snprintf(out,cap,"index=%llu tick=%llu thread=%x rva=%x word=%u pos=%.3f/%.3f/%.3f chain=",(unsigned long long)index,
        (unsigned long long)r->tick,r->thread,r->rva,r->word,(double)r->pos[0],(double)r->pos[1],(double)r->pos[2]);
    for(unsigned f=0;f<r->count && n>0 && (size_t)n<cap;f++) n+=snprintf(out+n,cap-(size_t)n,"%s%x",f?",":"",r->frames[f]);
    return n>0 && (size_t)n<cap?n:0;
}
static int bdw_status(char *out,size_t cap) {
    const int n=snprintf(out,cap,"configured=%d armed=%d saved=%016llx which=%u window=%llu-%llu threads=%u writes=%lld body=%llx stage=%s",
        bdw_configured,bdw_armed,(unsigned long long)bdw_saved,bdw_which,(unsigned long long)bdw_first,(unsigned long long)bdw_last,bdw_threads,
        (long long)InterlockedCompareExchange64(&bdw_count,0,0),(unsigned long long)(uintptr_t)bdw_body,bdw_stage);
    return n>0 && (size_t)n<cap?n:0;
}

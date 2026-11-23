/* native_gate_bank_watch = {'<saved ID, 16 hex>', first, last} (diagnostic, Windows gate, default off): hardware write
 * watches on a ship's two pathing banks' linear velocity x word (controller +0x30 = agent +0xad0, +0x40; the second bank
 * 0x1a0 further), DR0 and DR1 on every thread of the process, in ticks first..last; a vectored handler keeps one
 * BANK_WATCH_ROW per write: the tick, the thread, the RVA after the writing instruction (a data breakpoint traps after
 * it), the bank and the value written. Why: the 1203 split (reports/LOCKSTEP-LINUX-CATCHUP-20261104.txt): the two
 * builds' Steering updates write equal working copies, the committed bank is written later by something else, and
 * only that differs. The UI thread's own registers are set by a helper thread while the UI thread waits for it (a
 * thread cannot set its own debug registers by SetThreadContext). Threads created after the arm are not watched. */
#include <tlhelp32.h>
#define BW_ROWS 4096u
typedef struct {uint64_t tick;uint32_t thread,rva,bank,value;} bw_row;
static bw_row bw_rows[BW_ROWS];
static volatile LONG64 bw_count;
static uint64_t bw_saved,bw_first,bw_last;
static uintptr_t bw_addr[2];
static int bw_configured,bw_armed;
static unsigned bw_threads;
static void *bw_veh;
static const char *bw_stage="idle";
static LONG CALLBACK bw_handler(EXCEPTION_POINTERS *e) {
    if(e->ExceptionRecord->ExceptionCode!=EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c=e->ContextRecord;const DWORD64 dr6=c->Dr6;
    if(!(dr6&3u)) return EXCEPTION_CONTINUE_SEARCH;
    const unsigned b=(dr6&1u)?0u:1u;
    const LONG64 i=InterlockedIncrement64(&bw_count)-1;
    if(i>=0 && i<(LONG64)BW_ROWS) {
        bw_row *r=&bw_rows[i];uint32_t v=0;read_local((const void *)bw_addr[b],&v,4);
        const uintptr_t rip=(uintptr_t)c->Rip;
        r->tick=stats.issued;r->thread=GetCurrentThreadId();r->rva=rip>=(uintptr_t)image && rip<(uintptr_t)image+0x6fa9000?(uint32_t)(rip-(uintptr_t)image):0xffffffffu;
        r->bank=b;r->value=v;
    }
    c->Dr6=0;return EXCEPTION_CONTINUE_EXECUTION;
}
static int bw_set_thread(DWORD tid,int on) {
    HANDLE h=OpenThread(THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_SUSPEND_RESUME,FALSE,tid);int ok=0;
    if(!h) return 0;
    if(SuspendThread(h)!=(DWORD)-1) {
        CONTEXT c;memset(&c,0,sizeof(c));c.ContextFlags=CONTEXT_DEBUG_REGISTERS;
        if(GetThreadContext(h,&c)) {
            c.Dr7&=~(DWORD64)0xff000fu;
            if(on) {c.Dr0=bw_addr[0];c.Dr1=bw_addr[1];c.Dr7|=1u|4u|(1u<<16)|(3u<<18)|(1u<<20)|(3u<<22);}   /* L0, L1; write, 4 bytes */
            else {c.Dr0=c.Dr1=0;}
            ok=SetThreadContext(h,&c)!=0;
        }
        ResumeThread(h);
    }
    CloseHandle(h);return ok;
}
static volatile LONG bw_helper_on;
static DWORD WINAPI bw_helper(void *param) {return (DWORD)bw_set_thread((DWORD)(uintptr_t)param,(int)bw_helper_on);}
/* Every thread of the process; the calling thread through a helper. The number of threads set. */
static unsigned bw_set_all(int on) {
    const DWORD self=GetCurrentThreadId(),pid=GetCurrentProcessId();unsigned n=0;
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snap!=INVALID_HANDLE_VALUE) {
        THREADENTRY32 t;t.dwSize=sizeof(t);
        if(Thread32First(snap,&t)) do {
            if(t.th32OwnerProcessID==pid && t.th32ThreadID!=self) n+=(unsigned)bw_set_thread(t.th32ThreadID,on);
        } while(Thread32Next(snap,&t));
        CloseHandle(snap);
    }
    InterlockedExchange(&bw_helper_on,on);
    HANDLE helper=CreateThread(NULL,0,bw_helper,(void *)(uintptr_t)self,0,NULL);
    if(helper) {DWORD code=0;WaitForSingleObject(helper,5000);if(GetExitCodeThread(helper,&code) && code) ++n;CloseHandle(helper);}
    return n;
}
/* The UI thread, every gate tick (lockstep_gate.c's frame): arm at first, disarm after last. */
static void bw_tick(void) {
    if(!bw_configured || !stats.active || boot_phase!=3) return;
    const uint64_t tick=stats.issued;
    if(!bw_armed && tick>=bw_first && tick<=bw_last) {
        const uint64_t runtime=pilot_reverse(bw_saved);unsigned char *object=runtime?motion_watch_lookup(runtime):NULL,*controller=NULL,*slots=NULL;
        if(!object || !read_local(object+0x200,&controller,8) || !controller || !read_local(controller+0x30,&slots,8) || !slots) {bw_stage="unresolved";return;}
        bw_addr[0]=(uintptr_t)(slots+0x40);bw_addr[1]=(uintptr_t)(slots+0x1a0+0x40);
        if(!bw_veh) bw_veh=AddVectoredExceptionHandler(1,bw_handler);
        if(!bw_veh) {bw_stage="handler";bw_configured=0;return;}
        bw_threads=bw_set_all(1);bw_armed=1;bw_stage="armed";
    } else if(bw_armed && tick>bw_last) {
        bw_set_all(0);bw_armed=0;bw_configured=0;bw_stage="done";
    }
}
static void bw_restore(void) {
    if(bw_armed) {bw_set_all(0);bw_armed=0;}
    if(bw_veh) {RemoveVectoredExceptionHandler(bw_veh);bw_veh=NULL;}
    bw_configured=0;
}
static int bw_text(uint64_t index,char *out,size_t cap) {
    const LONG64 count=InterlockedCompareExchange64(&bw_count,0,0);
    if((LONG64)index>=count || index>=BW_ROWS) return 0;
    const bw_row *r=&bw_rows[index];float f;memcpy(&f,&r->value,4);
    const int n=snprintf(out,cap,"index=%llu tick=%llu thread=%x rva=%x bank=%u value=%08x/%.9g",(unsigned long long)index,
        (unsigned long long)r->tick,r->thread,r->rva,r->bank,r->value,(double)f);
    return n>0 && (size_t)n<cap?n:0;
}
static int bw_status(char *out,size_t cap) {
    const int n=snprintf(out,cap,"configured=%d armed=%d saved=%016llx window=%llu-%llu threads=%u writes=%lld addr=%llx/%llx stage=%s",
        bw_configured,bw_armed,(unsigned long long)bw_saved,(unsigned long long)bw_first,(unsigned long long)bw_last,bw_threads,
        (long long)InterlockedCompareExchange64(&bw_count,0,0),(unsigned long long)bw_addr[0],(unsigned long long)bw_addr[1],bw_stage);
    return n>0 && (size_t)n<cap?n:0;
}

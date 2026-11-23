/* native_gate_fit_rows on the Windows gate (lockstep_fit_rows.h; Linux twin linux/gate_fit_trace.h): the Steering
 * step's call of the flight integration wrapper 0x1197fe0 at 0xec92c0 is retargeted (its rel32, by CAS) to a stub
 * on a near page that jumps to fitw_hook, which notes the five arguments' leading bytes, calls 0x1197fe0 with them
 * (rcx, rdx, r8, r9 and the stack argument: the wrapper reads one, [rsp+0x28] at its entry) and notes them again.
 * Diagnostic, read-only, default off; armed before the first tick on the UI thread, restored at stop. */
#define FITW_SITE 0xec92c0u
#define FITW_TARGET 0x1197fe0u
typedef uint64_t (*fitw_fn)(void *,void *,void *,void *,void *);
static unsigned char *fitw_stub;
static int fitw_installed;
static const char *fitw_stage="idle";
static int fitw_read(const void *p,void *out,size_t n) {return read_local(p,out,n);}
static uint64_t fitw_hook(void *a1,void *a2,void *a3,void *a4,void *a5) {
    void *args[FIT_ARGS]={a1,a2,a3,a4,a5};fit_row *r=fit_claim(stats.issued);
    if(r) {r->thread=GetCurrentThreadId();fit_note(r,0,args,fitw_read);}
    const uint64_t result=((fitw_fn)(void *)(image+FITW_TARGET))(a1,a2,a3,a4,a5);
    fit_note(r,1,args,fitw_read);
    return result;
}
/* The Steering step itself (0xec7200, reached through the flight module's wrapper flight_update, which owns the vtable
 * slot): a row with the bank's first 0xc0 bytes and the model's steering output (+0x1d0) before and after, the update's
 * cheap flag in bits 56..62 of the row's thread word. */
static void fitw_step_observer(void *model,void *bank,unsigned after,unsigned char cheap) {
    void *args[FIT_ARGS]={bank,bank?(unsigned char *)bank+0x40:NULL,bank?(unsigned char *)bank+0x80:NULL,model?(unsigned char *)model+0x1d0:NULL,model};
    if(!after) {
        fit_row *r=fit_claim(stats.issued);
        if(r) {r->thread=GetCurrentThreadId()|FIT_STEP_TAG|((uint64_t)(cheap&0x7f)<<56);fit_note(r,0,args,fitw_read);}
        if(r) fit_open_put(GetCurrentThreadId(),r);
        return;
    }
    fit_row *r=fit_open_take(GetCurrentThreadId());
    if(r) fit_note(r,1,args,fitw_read);
}
static int32_t fitw_rel_to(const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+FITW_SITE+5));}
/* 0 original, 1 ours, -1 anything else. */
static int fitw_state(void) {
    unsigned char now[5];int32_t d;
    if(!read_local(image+FITW_SITE,now,5) || now[0]!=0xe8) return -1;
    memcpy(&d,now+1,4);
    if(d==fitw_rel_to(image+FITW_TARGET)) return 0;
    return fitw_stub && d==fitw_rel_to(fitw_stub)?1:-1;
}
static unsigned char *fitw_alloc_near(void) {
    const uintptr_t at=(uintptr_t)(image+FITW_SITE)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            unsigned char *p=VirtualAlloc((void *)(sign<0?at-distance:at+distance),4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            const intptr_t lo=(intptr_t)p-(intptr_t)(image+FITW_SITE+5),hi=(intptr_t)(p+64)-(intptr_t)(image+FITW_SITE+5);
            if(lo>=INT32_MIN && hi<=INT32_MAX) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int fitw_cas(int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(FITW_SITE+1,e,v,4);
}
static int fitw_arm(uint64_t first,uint64_t last) {
    if(fitw_installed) return stats.active && first==fit_first && last==fit_last;
    if(!first || last<first || last-first>600 || !stats.active || (boot_mode && boot_phase) || stats.issued ||
       GetCurrentThreadId()!=ui_thread) {fitw_stage="guard";return 0;}
    if(fitw_state()!=0) {fitw_stage="site";return 0;}
    if(((FITW_SITE+1)&15u)>12u) {fitw_stage="line";return 0;}   /* the rel32 in one 16-byte line: view_cas_bytes swaps it at once */
    /* the wrapper's head as this build has it: push rbx; sub rsp,0x60; movups xmm2,[rdx] */
    {static const unsigned char head[10]={0x40,0x53,0x48,0x83,0xec,0x60,0x0f,0x10,0x12,0x0f};unsigned char now[10];
     if(!read_local(image+FITW_TARGET,now,10) || memcmp(now,head,10)) {fitw_stage="pins";return 0;}}
    if(!fit_rows) fit_rows=VirtualAlloc(NULL,sizeof(fit_row)*FIT_ROWS,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!fitw_stub) fitw_stub=fitw_alloc_near();
    if(!fit_rows || !fitw_stub) {fitw_stage="memory";return 0;}
    DWORD old,ignored;
    if(!VirtualProtect(fitw_stub,4096,PAGE_READWRITE,&old)) {fitw_stage="stub";return 0;}
    {static const unsigned char jump[6]={0xff,0x25,0,0,0,0};void *target=(void *)fitw_hook;memset(fitw_stub,0xcc,64);
     memcpy(fitw_stub,jump,6);memcpy(fitw_stub+6,&target,8);}
    if(!VirtualProtect(fitw_stub,4096,PAGE_EXECUTE_READ,&ignored)) {fitw_stage="stub";return 0;}
    FlushInstructionCache(GetCurrentProcess(),fitw_stub,64);
    fit_first=first;fit_last=last;fit_claimed=fit_dropped=fit_calls=0;fit_drained=0;fit_on=1;
    if(!fitw_cas(fitw_rel_to(image+FITW_TARGET),fitw_rel_to(fitw_stub))) {fit_on=0;fitw_stage="cas";return 0;}
    flight_step_observer=fitw_step_observer;
    fitw_installed=1;fitw_stage="armed";return 1;
}
static void fitw_restore(void) {
    if(!fitw_installed) return;
    fit_on=0;
    flight_step_observer=NULL;
    if(fitw_state()==1 && fitw_cas(fitw_rel_to(fitw_stub),fitw_rel_to(image+FITW_TARGET))) {fitw_installed=0;fitw_stage="restored";}
}
static int fitw_status(char *out,size_t cap) {
    const int n=snprintf(out,cap,"installed=%d step=%d window=%llu-%llu calls=%lld rows=%lld dropped=%lld stage=%s",fitw_installed,flight_step_observer!=NULL,
        (unsigned long long)fit_first,(unsigned long long)fit_last,(long long)fit_calls,(long long)fit_claimed,(long long)fit_dropped,fitw_stage);
    return n>0 && (size_t)n<cap?n:0;
}

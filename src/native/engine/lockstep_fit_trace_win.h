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
        fit_open_take(GetCurrentThreadId());   /* a stale row of an update that raised before its after call */
        fit_row *r=fit_claim(stats.issued);
        if(r) {r->thread=GetCurrentThreadId()|FIT_STEP_TAG|((uint64_t)(cheap&0x7f)<<56);fit_note(r,0,args,fitw_read);}
        if(r) fit_open_put(GetCurrentThreadId(),r);
        return;
    }
    fit_row *r=fit_open_take(GetCurrentThreadId());
    if(r) fit_note(r,1,args,fitw_read);
}
/* The frame function 0x3df3f0 (a component's transform and velocity into a frame from the published view cache; rcx the
 * component, rdx the matrix out, r8 the velocity out, r9 the frame, two stack bytes): an entry detour. Its first
 * instructions, cmp r9,rcx (3) and je rel32 (6) to 0x3e021f, become jmp rel32 to a stub that jumps to fitw_frame_hook;
 * the trampoline replays the cmp, takes the je absolutely and jumps back to 0x3df3f9. */
#define FITW_FRAME 0x3df3f0u
#define FITW_FRAME_SAME 0x3e021fu
typedef void (*fitw_frame_fn)(void *,void *,void *,void *,uint64_t,uint64_t);
static const unsigned char fitw_frame_head[9]={0x4c,0x3b,0xc9,0x0f,0x84,0x26,0x0e,0x00,0x00};
static unsigned char *fitw_frame_page;
static unsigned char fitw_frame_saved[9];
static unsigned char *fitw_alloc_near(void);
static int fitw_frame_installed;
static void fitw_frame_hook(void *component,void *matrix,void *velocity,void *frame,uint64_t a5,uint64_t a6) {
    fit_row *r=fit_claim(stats.issued);void *args[FIT_ARGS]={matrix,velocity,component,frame,NULL};
    if(r) {r->thread=GetCurrentThreadId()|FIT_FRAME_TAG;fit_note(r,0,args,fitw_read);}
    ((fitw_frame_fn)(void *)(fitw_frame_page+0x20))(component,matrix,velocity,frame,a5,a6);
    fit_note(r,1,args,fitw_read);
}
static int fitw_frame_arm(void) {
    unsigned char now[9];
    if(!read_local(image+FITW_FRAME,now,9) || memcmp(now,fitw_frame_head,9)) return 0;
    if(!fitw_frame_page) fitw_frame_page=fitw_alloc_near();
    if(!fitw_frame_page) return 0;
    DWORD old,ignored;if(!VirtualProtect(fitw_frame_page,4096,PAGE_READWRITE,&old)) return 0;
    static const unsigned char jump[6]={0xff,0x25,0,0,0,0};unsigned char *p=fitw_frame_page;
    memset(p,0xcc,0x80);
    {void *t=(void *)fitw_frame_hook;memcpy(p,jump,6);memcpy(p+6,&t,8);}                 /* +0x00: jmp [rip] -> the hook */
    p=fitw_frame_page+0x20;
    p[0]=0x4c;p[1]=0x3b;p[2]=0xc9;p[3]=0x75;p[4]=0x0e;                                   /* cmp r9,rcx; jne +14 */
    {void *t=image+FITW_FRAME_SAME;memcpy(p+5,jump,6);memcpy(p+11,&t,8);}                 /* jmp [rip] -> 0x3e021f */
    {void *t=image+FITW_FRAME+9;memcpy(p+19,jump,6);memcpy(p+25,&t,8);}                   /* jmp [rip] -> 0x3df3f9 */
    if(!VirtualProtect(fitw_frame_page,4096,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),fitw_frame_page,0x80);
    unsigned char patch[9];memset(patch,0xcc,9);patch[0]=0xe9;
    {const int32_t rel=(int32_t)((intptr_t)fitw_frame_page-(intptr_t)(image+FITW_FRAME+5));memcpy(patch+1,&rel,4);}
    memcpy(fitw_frame_saved,now,9);
    if(!view_cas_bytes(FITW_FRAME,now,patch,9)) return 0;
    fitw_frame_installed=1;return 1;
}
static void fitw_frame_restore(void) {
    if(!fitw_frame_installed) return;
    unsigned char now[9];
    if(read_local(image+FITW_FRAME,now,9) && view_cas_bytes(FITW_FRAME,now,fitw_frame_saved,9)) fitw_frame_installed=0;
}
/* NavigationController::SetVelocity 0xea5790 (rcx the controller, rdx two vec4 velocities): an entry detour. Its first
 * seven bytes (mov rax,rsp; mov [rax+0x18],rbx) are replayed in a trampoline that jumps back to 0xea5797; a row per call
 * with the caller's RVA. */
#define FITW_NAV 0xea5790u
#define FITW_NAV_STOLEN 7u
typedef void (*fitw_nav_fn)(void *,const void *);
static const unsigned char fitw_nav_head[12]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x18,0x48,0x89,0x70,0x20,0x57};
static unsigned char *fitw_nav_page;
static unsigned char fitw_nav_saved[FITW_NAV_STOLEN];
static int fitw_nav_installed;
static void fitw_nav_hook(void *controller,const void *value) {
    const unsigned char *ret=__builtin_return_address(0);
    const uint64_t rva=ret>=image && ret<image+0x6fa9000?(uint64_t)(ret-image):0;
    void *object=NULL;if(controller) read_local((unsigned char *)controller+0x28,&object,8);
    fit_row *r=fit_claim(stats.issued);void *args[FIT_ARGS]={(void *)value,object,controller?(unsigned char *)controller+0xa0:NULL,controller,NULL};
    if(r) {r->thread=(uint64_t)GetCurrentThreadId()|(rva<<32)|FIT_NAV_TAG;fit_note(r,0,args,fitw_read);}
    ((fitw_nav_fn)(void *)(fitw_nav_page+0x20))(controller,value);
    fit_note(r,1,args,fitw_read);
}
static int fitw_nav_arm(void) {
    unsigned char now[FITW_NAV_STOLEN];
    {unsigned char head[sizeof(fitw_nav_head)];if(!read_local(image+FITW_NAV,head,sizeof(head)) || memcmp(head,fitw_nav_head,sizeof(head))) return 0;}
    if(!read_local(image+FITW_NAV,now,FITW_NAV_STOLEN)) return 0;
    if(!fitw_nav_page) fitw_nav_page=fitw_alloc_near();
    if(!fitw_nav_page) return 0;
    DWORD old,ignored;if(!VirtualProtect(fitw_nav_page,4096,PAGE_READWRITE,&old)) return 0;
    static const unsigned char jump[6]={0xff,0x25,0,0,0,0};unsigned char *p=fitw_nav_page;
    memset(p,0xcc,0x80);
    {void *t=(void *)fitw_nav_hook;memcpy(p,jump,6);memcpy(p+6,&t,8);}                   /* +0x00: jmp [rip] -> the hook */
    p=fitw_nav_page+0x20;memcpy(p,now,FITW_NAV_STOLEN);                                  /* +0x20: the stolen bytes */
    {void *t=image+FITW_NAV+FITW_NAV_STOLEN;memcpy(p+FITW_NAV_STOLEN,jump,6);memcpy(p+FITW_NAV_STOLEN+6,&t,8);} /* -> 0xea5797 */
    if(!VirtualProtect(fitw_nav_page,4096,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),fitw_nav_page,0x80);
    unsigned char patch[FITW_NAV_STOLEN];memset(patch,0xcc,sizeof(patch));patch[0]=0xe9;
    {const int32_t rel=(int32_t)((intptr_t)fitw_nav_page-(intptr_t)(image+FITW_NAV+5));memcpy(patch+1,&rel,4);}
    memcpy(fitw_nav_saved,now,FITW_NAV_STOLEN);
    if(!view_cas_bytes(FITW_NAV,now,patch,FITW_NAV_STOLEN)) return 0;
    fitw_nav_installed=1;return 1;
}
static void fitw_nav_restore(void) {
    if(!fitw_nav_installed) return;
    unsigned char now[FITW_NAV_STOLEN];
    if(read_local(image+FITW_NAV,now,FITW_NAV_STOLEN) && view_cas_bytes(FITW_NAV,now,fitw_nav_saved,FITW_NAV_STOLEN)) fitw_nav_installed=0;
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
    fitw_frame_arm();
    fitw_nav_arm();
    fitw_installed=1;fitw_stage="armed";return 1;
}
static void fitw_restore(void) {
    if(!fitw_installed) return;
    fit_on=0;
    flight_step_observer=NULL;
    fitw_frame_restore();
    fitw_nav_restore();
    if(fitw_state()==1 && fitw_cas(fitw_rel_to(fitw_stub),fitw_rel_to(image+FITW_TARGET))) {fitw_installed=0;fitw_stage="restored";}
}
static int fitw_status(char *out,size_t cap) {
    const int n=snprintf(out,cap,"installed=%d step=%d frame=%d nav=%d window=%llu-%llu calls=%lld rows=%lld dropped=%lld stage=%s",fitw_installed,flight_step_observer!=NULL,fitw_frame_installed,fitw_nav_installed,
        (unsigned long long)fit_first,(unsigned long long)fit_last,(long long)fit_calls,(long long)fit_claimed,(long long)fit_dropped,fitw_stage);
    return n>0 && (size_t)n<cap?n:0;
}

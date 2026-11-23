/* native_gate_fit_rows on the Linux gate (lockstep_fit_rows.h; Windows: lockstep_fit_trace_win.h): the Steering step
 * 0x13e9d50's call of the flight integration 0x19fc190 at 0x13eacf8 (rdi the ship's flight object, rsi the result as
 * doubles, rdx the velocities, rcx the transform, r8 a fifth block) is retargeted (its rel32, in its 16-byte line by
 * CAS) to a near-page stub that jumps to lxfit_hook, which notes the five arguments' leading bytes before and after
 * calling 0x19fc190 with them. Diagnostic, read-only, default off; armed before the first tick, restored at stop. */
#define LXFIT_SITE_VA 0x13eacf8u
#define LXFIT_TARGET_VA 0x19fc190u
typedef uint64_t (*lxfit_fn)(void *,void *,void *,void *,void *);
static unsigned char *lxfit_page;
static int lxfit_installed;
static const char *lxfit_stage="idle";
static int lxfit_read(const void *p,void *out,size_t n) {return gate_read_local(p,out,n);}
static uint64_t lxfit_hook(void *a1,void *a2,void *a3,void *a4,void *a5) {
    void *args[FIT_ARGS]={a1,a2,a3,a4,a5};fit_row *r=fit_claim(stats.issued);
    if(r) {r->thread=(uint64_t)GetCurrentThreadId();fit_note(r,0,args,lxfit_read);}
    const uint64_t result=((lxfit_fn)(void *)LX(LXFIT_TARGET_VA))(a1,a2,a3,a4,a5);
    fit_note(r,1,args,lxfit_read);
    return result;
}
/* The Steering step itself (0x13e9d50, reached through the flight module's lxf_update, which owns the vtable slot and
 * pins the step's head): rows as Windows' (the model's output +0x1c0), the cheap flag in bits 56..62 of the thread word. */
static void lxfit_step_observer(void *model,void *bank,unsigned after,unsigned char cheap) {
    void *args[FIT_ARGS]={bank,bank?(unsigned char *)bank+0x40:NULL,bank?(unsigned char *)bank+0x80:NULL,model?(unsigned char *)model+0x1c0:NULL,model};
    if(!after) {
        fit_open_take((uint64_t)GetCurrentThreadId());   /* a stale row, if any */
        fit_row *r=fit_claim(stats.issued);
        if(r) {r->thread=(uint64_t)GetCurrentThreadId()|FIT_STEP_TAG|((uint64_t)(cheap&0x7f)<<56);fit_note(r,0,args,lxfit_read);}
        if(r) fit_open_put((uint64_t)GetCurrentThreadId(),r);
        return;
    }
    fit_row *r=fit_open_take((uint64_t)GetCurrentThreadId());
    if(r) fit_note(r,1,args,lxfit_read);
}
/* The frame function 0x18a3980 (Windows 0x3df3f0: a component's transform and velocity into a frame, read from the
 * published view cache; rdi the component, rsi the matrix out, rdx the velocity out, rcx the frame, r8d, r9d): an entry
 * detour (its three pushes, 6 bytes, replayed in a trampoline) writes a row per call, outputs after. */
#define LXFIT_FRAME_VA 0x18a3980u
#define LXFIT_FRAME_STOLEN 6u
typedef void (*lxfit_frame_fn)(void *,void *,void *,void *,uint64_t,uint64_t);
static const unsigned char lxfit_frame_head[16]={0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x55,0x48,0x89,0xfd,0x53,0x48,0x81,0xec};
static unsigned char *lxfit_frame_page;
static unsigned __int128 lxfit_frame_original,lxfit_frame_patched;
static int lxfit_frame_installed;
static void lxfit_frame_hook(void *component,void *matrix,void *velocity,void *frame,uint64_t a5,uint64_t a6) {
    fit_row *r=fit_claim(stats.issued);void *args[FIT_ARGS]={matrix,velocity,component,frame,NULL};
    if(r) {r->thread=(uint64_t)GetCurrentThreadId()|FIT_FRAME_TAG;fit_note(r,0,args,lxfit_read);}
    ((lxfit_frame_fn)(void *)(lxfit_frame_page+0x20))(component,matrix,velocity,frame,a5,a6);
    fit_note(r,1,args,lxfit_read);
}
static int lxfit_frame_arm(void) {
    unsigned char *at=LX(LXFIT_FRAME_VA);
    if(((uintptr_t)at&15) || memcmp(at,lxfit_frame_head,sizeof(lxfit_frame_head))) return 0;
    if(!lxfit_frame_page) lxfit_frame_page=gate_near_page(at);
    if(!lxfit_frame_page || mprotect(lxfit_frame_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxfit_frame_page,0xcc,GATE_PAGE);
    lxw_jump_write(lxfit_frame_page,(void *)lxfit_frame_hook);
    memcpy(lxfit_frame_page+0x20,at,LXFIT_FRAME_STOLEN);lxw_jump_write(lxfit_frame_page+0x20+LXFIT_FRAME_STOLEN,at+LXFIT_FRAME_STOLEN);
    if(mprotect(lxfit_frame_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    memcpy(&lxfit_frame_original,at,16);lxfit_frame_patched=lxfit_frame_original;
    {unsigned char jmp[5];const int32_t rel=(int32_t)((intptr_t)lxfit_frame_page-(intptr_t)(at+5));jmp[0]=0xe9;memcpy(jmp+1,&rel,4);
     memcpy((unsigned char*)&lxfit_frame_patched,jmp,5);}
    if(!gate_code_swap128(at,lxfit_frame_original,lxfit_frame_patched)) return 0;
    lxfit_frame_installed=1;return 1;
}
static void lxfit_frame_restore(void) {
    if(lxfit_frame_installed && gate_code_swap128(LX(LXFIT_FRAME_VA),lxfit_frame_patched,lxfit_frame_original)) lxfit_frame_installed=0;
}
/* NavigationController::SetVelocity 0x141fe50 (Windows 0xea5790; rdi the controller, rsi two vec4 velocities): an entry
 * detour (push r12; push rbp; push rbx; mov rbx,rdi: 7 bytes, replayed in a trampoline), a row per call with the caller. */
#define LXFIT_NAV_VA 0x141fe50u
#define LXFIT_NAV_STOLEN 7u
typedef void (*lxfit_nav_fn)(void *,const void *);
static const unsigned char lxfit_nav_head[16]={0x41,0x54,0x55,0x53,0x48,0x89,0xfb,0x48,0x81,0xec,0x90,0x00,0x00,0x00,0x8b,0x05};
static unsigned char *lxfit_nav_page;
static unsigned __int128 lxfit_nav_original,lxfit_nav_patched;
static int lxfit_nav_installed;
static void lxfit_nav_hook(void *controller,const void *value) {
    const uintptr_t ret=(uintptr_t)__builtin_return_address(0),base=(uintptr_t)LX(LX_IMAGE_BASE);
    const uint64_t rva=ret>=base && ret-base<0x10000000u?(uint64_t)(ret-base):0;
    void *object=NULL;if(controller) gate_read_local((unsigned char *)controller+0x20,&object,8);
    fit_row *r=fit_claim(stats.issued);void *args[FIT_ARGS]={(void *)value,object,controller?(unsigned char *)controller+0xa0:NULL,controller,NULL};
    if(r) {r->thread=((uint64_t)GetCurrentThreadId()&0xffffffffu)|(rva<<32)|FIT_NAV_TAG;fit_note(r,0,args,lxfit_read);}
    ((lxfit_nav_fn)(void *)(lxfit_nav_page+0x20))(controller,value);
    fit_note(r,1,args,lxfit_read);
}
static int lxfit_nav_arm(void) {
    unsigned char *at=LX(LXFIT_NAV_VA);
    if(((uintptr_t)at&15) || memcmp(at,lxfit_nav_head,sizeof(lxfit_nav_head))) return 0;
    if(!lxfit_nav_page) lxfit_nav_page=gate_near_page(at);
    if(!lxfit_nav_page || mprotect(lxfit_nav_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxfit_nav_page,0xcc,GATE_PAGE);
    lxw_jump_write(lxfit_nav_page,(void *)lxfit_nav_hook);
    memcpy(lxfit_nav_page+0x20,at,LXFIT_NAV_STOLEN);lxw_jump_write(lxfit_nav_page+0x20+LXFIT_NAV_STOLEN,at+LXFIT_NAV_STOLEN);
    if(mprotect(lxfit_nav_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    memcpy(&lxfit_nav_original,at,16);lxfit_nav_patched=lxfit_nav_original;
    {unsigned char jmp[7]={0xe9,0,0,0,0,0xcc,0xcc};const int32_t rel=(int32_t)((intptr_t)lxfit_nav_page-(intptr_t)(at+5));memcpy(jmp+1,&rel,4);
     memcpy((unsigned char*)&lxfit_nav_patched,jmp,7);}
    if(!gate_code_swap128(at,lxfit_nav_original,lxfit_nav_patched)) return 0;
    lxfit_nav_installed=1;return 1;
}
static void lxfit_nav_restore(void) {
    if(lxfit_nav_installed && gate_code_swap128(LX(LXFIT_NAV_VA),lxfit_nav_patched,lxfit_nav_original)) lxfit_nav_installed=0;
}
static int lxfit_rel(const void *target,int32_t *out) {
    const intptr_t d=(intptr_t)target-(intptr_t)(LX(LXFIT_SITE_VA)+5);
    if(d<INT32_MIN || d>INT32_MAX) return 0;
    *out=(int32_t)d;return 1;
}
/* 0 original, 1 ours, -1 anything else. */
static int lxfit_state(void) {
    const unsigned char *at=LX(LXFIT_SITE_VA);int32_t d,want;
    if(at[0]!=0xe8) return -1;
    memcpy(&d,at+1,4);
    if(lxfit_rel(LX(LXFIT_TARGET_VA),&want) && d==want) return 0;
    return lxfit_page && lxfit_rel(lxfit_page,&want) && d==want?1:-1;
}
static int lxfit_swap(const void *from,const void *to) {
    unsigned char *site=LX(LXFIT_SITE_VA),*line=(unsigned char*)((uintptr_t)(site+1)&~(uintptr_t)15);
    const unsigned at=(unsigned)(site+1-line);int32_t e,v,now;
    if(at>12 || !lxfit_rel(from,&e) || !lxfit_rel(to,&v)) return 0;
    unsigned __int128 before,after;memcpy(&before,line,16);after=before;memcpy(&now,(unsigned char*)&before+at,4);
    if(now!=e) return 0;
    memcpy((unsigned char*)&after+at,&v,4);
    return gate_code_swap128(line,before,after);
}
static int lxfit_arm(uint64_t first,uint64_t last) {
    if(lxfit_installed) return stats.active && first==fit_first && last==fit_last;
    if(!first || last<first || last-first>600 || !stats.active || (boot_mode && boot_phase) || stats.issued ||
       GetCurrentThreadId()!=ui_thread) {lxfit_stage="guard";return 0;}
    if(lxfit_state()!=0) {lxfit_stage="site";return 0;}
    /* the integration's head as this build has it: push r15; then an SSE move */
    {static const unsigned char head[4]={0x41,0x57,0x66,0x0f};if(memcmp(LX(LXFIT_TARGET_VA),head,4)) {lxfit_stage="pins";return 0;}}
    if(!fit_rows) {void *p=mmap(NULL,sizeof(fit_row)*FIT_ROWS,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);fit_rows=p==MAP_FAILED?NULL:p;}
    if(!lxfit_page) lxfit_page=gate_near_page(LX(LXFIT_SITE_VA));
    if(!fit_rows || !lxfit_page || mprotect(lxfit_page,GATE_PAGE,PROT_READ|PROT_WRITE)) {lxfit_stage="memory";return 0;}
    {static const unsigned char jump[6]={0xff,0x25,0,0,0,0};const void *target=(const void *)lxfit_hook;memset(lxfit_page,0xcc,64);
     memcpy(lxfit_page,jump,6);memcpy(lxfit_page+6,&target,8);}
    if(mprotect(lxfit_page,GATE_PAGE,PROT_READ|PROT_EXEC)) {lxfit_stage="memory";return 0;}
    fit_first=first;fit_last=last;fit_claimed=fit_dropped=fit_calls=0;fit_drained=0;fit_on=1;
    if(!lxfit_swap(LX(LXFIT_TARGET_VA),lxfit_page)) {fit_on=0;lxfit_stage="cas";return 0;}
    lxf_step_observer=lxfit_step_observer;
    lxfit_frame_arm();
    lxfit_nav_arm();
    lxfit_installed=1;lxfit_stage="armed";return 1;
}
static void lxfit_restore(void) {
    if(!lxfit_installed) return;
    fit_on=0;
    lxf_step_observer=NULL;
    lxfit_frame_restore();
    lxfit_nav_restore();
    if(lxfit_state()==1 && lxfit_swap(lxfit_page,LX(LXFIT_TARGET_VA))) {lxfit_installed=0;lxfit_stage="restored";}
}
static int lxfit_status(char *out,size_t cap) {
    const int n=snprintf(out,cap,"installed=%d step=%d frame=%d nav=%d window=%llu-%llu calls=%lld rows=%lld dropped=%lld stage=%s",lxfit_installed,lxf_step_observer!=NULL,lxfit_frame_installed,lxfit_nav_installed,
        (unsigned long long)fit_first,(unsigned long long)fit_last,(long long)fit_calls,(long long)fit_claimed,(long long)fit_dropped,lxfit_stage);
    return n>0 && (size_t)n<cap?n:0;
}
static int lua_fit_rows(lua_State *L) {
    const double first=p_lua_tonumber(L,1),last=p_lua_tonumber(L,2);
    p_lua_pushnumber(L,accept_ui() && isfinite(first) && isfinite(last) && first>=1 && last>=first && floor(first)==first &&
        floor(last)==last && lxfit_arm((uint64_t)first,(uint64_t)last));return 1;
}
static int lua_fit_row(lua_State *L) {
    const double index=p_lua_tonumber(L,1);static char out[FIT_ARGS*(4*FIT_BYTES+8)+96];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && floor(index)==index) n=fit_text((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_fit_status(lua_State *L) {
    char out[256];const int n=accept_ui()?lxfit_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}

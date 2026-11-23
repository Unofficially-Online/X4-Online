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
    lxfit_installed=1;lxfit_stage="armed";return 1;
}
static void lxfit_restore(void) {
    if(!lxfit_installed) return;
    fit_on=0;
    lxf_step_observer=NULL;
    if(lxfit_state()==1 && lxfit_swap(lxfit_page,LX(LXFIT_TARGET_VA))) {lxfit_installed=0;lxfit_stage="restored";}
}
static int lxfit_status(char *out,size_t cap) {
    const int n=snprintf(out,cap,"installed=%d step=%d window=%llu-%llu calls=%lld rows=%lld dropped=%lld stage=%s",lxfit_installed,lxf_step_observer!=NULL,
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

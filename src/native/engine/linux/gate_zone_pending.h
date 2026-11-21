/* native_gate_zone_pending on the Linux X4 9.00 executable: the Linux twin of lockstep_zone_pending.h (the mechanism,
 * the evidence and why only the frame's call waits are there). Included by gate_linux.c only.
 *
 * Linux facts. The frame's pending physics work is 0x1161bb0 (Windows 0x83e960); UniverseFrameUpdate 0xfeaff0 calls it
 * at 0xfeb82b (return 0xfeb830; Windows 0x9fe427), after AIDirector::Move as on Windows, and 0x11634f0 calls it at
 * 0x1163907 (Windows: Zone slot 921 at 0x842030). Its two halves sit behind pthread_mutex_trylock (PLT 0x657f90; 0 is
 * success, Windows' TryEnterCriticalSection gives TRUE): the lock at 0x3e79248 at 0x1161bc5 (Windows 0x3949b28), the
 * lock at 0x3e792c8 for the half that frees a list's body records and then walks the waiting zones (0x1161910, Windows
 * 0x841110). GCC asks for the second lock at two sites, 0x1161be6 and 0x1161c0f (the latter when the list is empty but
 * zones wait), and not at all when nothing waits.
 *
 * The three calls are `call rel32` to the PLT; each rel32 is re-pointed (one compare-exchange of the 16-byte line that
 * holds it) at a stub on a page of this module's own near the image: mov rsi,[rsp+0x50] (who called 0x1161bb0: six
 * pushes and 0x18 bytes above the body's stack pointer at all three sites, plus this call's return), jmp [rip] to
 * lxzp_enter(mutex, caller). The call is five bytes, so the return needs no adjustment (Windows' six-byte call does).
 * Fail closed: the function's head (pin zone_pending, read with the three calls as X4's), the frame's call (pin
 * zone_pending_frame) and each site's call to the PLT; a build that differs keeps X4's calls. Before the first admitted
 * frame, on the MoveWorker; the per-tick check (stop reason 214) and the restore are this file's. Status as Windows':
 * installed=<sites>/3 why= calls= waited= lost= nested= other=. */
#define LXZP_STOP_REASON 214u
#define LXZP_FN_VA 0x1161bb0u
#define LXZP_TRYLOCK_PLT_VA 0x657f90u
#define LXZP_FRAME_CALL_VA 0xfeb82bu
#define LXZP_FRAME_RETURN_VA 0xfeb830u
#define LXZP_WAIT_MS 250u
#define LXZP_SITES 3u
#define LXZP_STUB 0x00u
#define LXZP_STUB_BYTES 0x13u
static const uint32_t lxzp_sites[LXZP_SITES]={0x1161bc5u,0x1161be6u,0x1161c0fu};
static unsigned char *lxzp_page;
static int lxzp_installed[LXZP_SITES],lxzp_restored;
static const char *lxzp_why="-";
static volatile int64_t lxzp_calls,lxzp_waited,lxzp_lost,lxzp_nested,lxzp_other;
/* The lock as X4 asks for it, except in the frame's own call on the simulation thread of a running session: there a busy
 * lock is waited for, LXZP_WAIT_MS at most. caller: the return address of the 0x1161bb0 call this one sits in. */
static int lxzp_enter(pthread_mutex_t *lock,const unsigned char *caller) {
    InterlockedIncrement64(&lxzp_calls);
    if(GetCurrentThreadId()!=ui_thread || !stats.active) {InterlockedIncrement64(&lxzp_other);return pthread_mutex_trylock(lock);}
    int busy=pthread_mutex_trylock(lock);
    if(!busy) return 0;
    if(caller!=LX(LXZP_FRAME_RETURN_VA)) {InterlockedIncrement64(&lxzp_nested);return busy;}
    InterlockedIncrement64(&lxzp_waited);
    for(const ULONGLONG start=GetTickCount64();GetTickCount64()-start<LXZP_WAIT_MS;) {
        usleep(1000);
        if(!(busy=pthread_mutex_trylock(lock))) return 0;
    }
    InterlockedIncrement64(&lxzp_lost);return busy;
}
static int lxzp_rel(int32_t *out,const unsigned char *from_end,const void *target) {
    const intptr_t d=(intptr_t)target-(intptr_t)from_end;
    if(d!=(intptr_t)(int32_t)d) return 0;
    *out=(int32_t)d;return 1;
}
/* mov rsi,[rsp+0x50]; jmp [rip+0]; the wrapper's address. */
static void lxzp_code(unsigned char out[LXZP_STUB_BYTES]) {
    static const unsigned char head[11]={0x48,0x8b,0x74,0x24,0x50,0xff,0x25,0x00,0x00,0x00,0x00};
    const uint64_t wrapper=(uint64_t)(uintptr_t)lxzp_enter;
    memcpy(out,head,sizeof(head));memcpy(out+sizeof(head),&wrapper,8);
}
static int lxzp_page_ok(void) {
    unsigned char want[LXZP_STUB_BYTES];lxzp_code(want);return lxzp_page && !memcmp(lxzp_page+LXZP_STUB,want,sizeof(want));
}
/* A site's call: X4's (to the PLT) or ours (to the stub). */
static int lxzp_call(unsigned i,int ours,unsigned char out[5]) {
    int32_t r;out[0]=0xe8;
    if(ours && !lxzp_page) return 0;
    if(!lxzp_rel(&r,LX(lxzp_sites[i])+5,ours?(const void*)(lxzp_page+LXZP_STUB):(const void*)LX(LXZP_TRYLOCK_PLT_VA))) return 0;
    memcpy(out+1,&r,4);return 1;
}
static int lxzp_state(unsigned i) { /* 0 X4's, 1 ours, -1 other */
    unsigned char now[5],want[5];
    if(!gate_read_local(LX(lxzp_sites[i]),now,5)) return -1;
    if(lxzp_call(i,0,want) && !memcmp(now,want,5)) return 0;
    if(lxzp_call(i,1,want) && !memcmp(now,want,5)) return 1;
    return -1;
}
static int lxzp_installed_count(void) {int n=0;for(unsigned i=0;i<LXZP_SITES;i++) n+=lxzp_installed[i]!=0;return n;}
/* The head as X4 has it (the three calls put back to the PLT in a copy) and the frame's call to 0x1161bb0. */
static int lxzp_pins(void) {
    const lx_site *s=site_named("zone_pending"),*f=site_named("zone_pending_frame");unsigned char copy[0x80];
    if(!s || !f || s->va!=LXZP_FN_VA || f->va!=LXZP_FRAME_CALL_VA || f->length!=5 || s->length>sizeof(copy) ||
       !gate_read_local(LX(s->va),copy,s->length) || !hash_ok(LX(f->va),f->length,f->sha256)) return 0;
    for(unsigned i=0;i<LXZP_SITES;i++) {
        if(lxzp_sites[i]<s->va || lxzp_sites[i]+5>s->va+s->length || lxzp_state(i)<0 || !lxzp_call(i,0,copy+(lxzp_sites[i]-s->va))) return 0;
    }
    return hash_ok(copy,s->length,s->sha256);
}
/* The rel32 of a site, in one compare-exchange of the 16-byte line that holds it. */
static int lxzp_swap(unsigned i,const unsigned char from[5],const unsigned char to[5]) {
    unsigned char *rel=LX(lxzp_sites[i])+1,*line=(unsigned char*)((uintptr_t)rel&~(uintptr_t)15);const unsigned at=(unsigned)(rel-line);
    unsigned __int128 current,desired;
    if(at+4>16 || LX(lxzp_sites[i])[0]!=0xe8) return 0;
    memcpy(&current,line,16);
    if(memcmp((unsigned char*)&current+at,from+1,4)) return 0;
    desired=current;memcpy((unsigned char*)&desired+at,to+1,4);
    return gate_code_swap128(line,current,desired);
}
/* gate.zone_pending(1): before the first admitted frame, on the MoveWorker. 1: all three sites are in (or were). */
static int lxzp_arm(void) {
    if(lxzp_installed_count()==(int)LXZP_SITES) return stats.active;
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {lxzp_why="guard";return 0;}
    if(lxzp_installed_count()) {lxzp_why="partial";return 0;}
    for(unsigned i=0;i<LXZP_SITES;i++) if(lxzp_state(i)!=0) {lxzp_why="site";return 0;}
    if(!lxzp_pins()) {lxzp_why="pin";return 0;}
    if(!lxzp_page) lxzp_page=gate_near_page(LX(LXZP_FN_VA));
    if(!lxzp_page || mprotect(lxzp_page,GATE_PAGE,PROT_READ|PROT_WRITE)) {lxzp_why="page";return 0;}
    memset(lxzp_page,0xcc,GATE_PAGE);lxzp_code(lxzp_page+LXZP_STUB);
    if(mprotect(lxzp_page,GATE_PAGE,PROT_READ|PROT_EXEC)) {lxzp_why="page_protect";return 0;}
    for(unsigned i=0;i<LXZP_SITES;i++) {
        unsigned char from[5],to[5];
        if(!lxzp_call(i,0,from) || !lxzp_call(i,1,to) || !lxzp_swap(i,from,to)) {
            lxzp_why="site_swap";
            for(unsigned j=0;j<i;j++) if(lxzp_installed[j] && lxzp_call(j,1,to) && lxzp_call(j,0,from) && lxzp_swap(j,to,from)) lxzp_installed[j]=0;
            return 0;
        }
        lxzp_installed[i]=1;
    }
    lxzp_restored=0;lxzp_why="-";return 1;
}
static int lxzp_check(void) {
    const char *why=NULL;
    for(unsigned i=0;i<LXZP_SITES && !why;i++) if(lxzp_installed[i] && lxzp_state(i)!=1) why="site_changed";
    if(!why && !lxzp_page_ok()) why="page_changed";
    if(!why && !lxzp_pins()) why="block_changed";
    if(why) {lxzp_why=why;return 0;}
    return 1;
}
static void lxzp_restore(void) {
    for(unsigned i=0;i<LXZP_SITES;i++) {
        unsigned char from[5],to[5];
        if(!lxzp_installed[i]) continue;
        if(lxzp_state(i)!=1 || !lxzp_call(i,1,from) || !lxzp_call(i,0,to)) {lxzp_why="restore_foreign";continue;}
        if(lxzp_swap(i,from,to)) lxzp_installed[i]=0;
    }
    if(!lxzp_installed_count()) lxzp_restored=1; /* the page stays */
}
static int lxzp_status(char *out,size_t size) {
    const int n=snprintf(out,size,"installed=%d/%u why=%s calls=%lld waited=%lld lost=%lld nested=%lld other=%lld",lxzp_installed_count(),LXZP_SITES,
        lxzp_why,(long long)lxzp_calls,(long long)lxzp_waited,(long long)lxzp_lost,(long long)lxzp_nested,(long long)lxzp_other);
    return n>0 && (size_t)n<size?n:0;
}
static int lua_zone_pending(lua_State *L) {p_lua_pushnumber(L,p_lua_tonumber(L,1)==1.0 && lxzp_arm());return 1;}
static int lua_zone_pending_status(lua_State *L) {
    char out[200];const int n=accept_ui()?lxzp_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}

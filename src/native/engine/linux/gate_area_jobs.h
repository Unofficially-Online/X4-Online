/* native_gate_area_jobs on the Linux gate: the Linux twin of lockstep_area_jobs.h (a Windows session default), the
 * same three jobs run at the tick of their request, the same keyed word, the same status text. Included by gate_linux.c
 * after gate_fit_trace.h.
 *
 * Why (the mechanism is in that header): a sector's resource area job, a script's construction sequence and a resource
 * probe's scan are queued on the JobThreadManager, and what each job posts into the simulation is taken in whichever
 * tick the job happened to finish before. W98's world census against lx-1545s (23 Nov): at tick 1545 a salvage ship's
 * order.salvage.routine found a resource area on Windows (find_closest_resource, then find_recyclable_with_yield_in_sector
 * made a Xenon debris Recyclable) and none on Linux, whose job thread finished later; the routine drew 12 words against
 * 5 and made the same debris 100 ticks later (CREATED_ROW 1545 against 1645), the census's first split after 1500.
 *
 * Linux sites. QueueJob is 0x1a55930 (Windows 0x14bc2d0; rdi the manager, rsi the callback, rdx the payload, ecx and
 * r8d). Each site's call goes, by its rel32 (one compare-exchange of its 16-byte line), to a jmp [rip] on a page near the
 * image and on to lxaj_queue:
 *   area          0x1d1fba3 in 0x1d1f650 (Windows 0x78057a), callback 0x1297850, body 0x12815c0 (ResourceAreaRespawn-
 *                 JobFinishedEvent, vtable 0x3b50a60, Windows 0x2ba5cd8)
 *   construction  0xe57e44 in 0xe57920 (Windows 0xc25003), callback 0x189b230, body 0x1828fa0 (the sequence generator,
 *                 Windows 0xd5c6f0)
 *   probe         0x12e94e6 in 0x12e8df0 (Windows 0x76b288), callback 0x12f69b0, body 0x12cda50
 *                 (SetDetectedResourceAreaOverlapFractionEvent, vtable 0x3b55ed0, Windows 0x2ba5ca0)
 * The payload is the job's {object, body, this adjustment, .., cleanup flag}; each callback calls the body on the object
 * and then, with the flag, the cleanup, as Windows' thunks. A call whose callback and payload body are a site's runs the
 * callback on the asking thread with a job thread's TLS context (game time and previous time 0, the published view 0,
 * the word lockstep_seed("X4LAREAJ", tick, ordinal) as on Windows) and returns 0, as Windows' does (no caller reads it:
 * the area and probe askers return, the construction asker goes on without eax); any other call, or one from a thread
 * without the game's TLS, is queued as X4 queues it. Pinned (areaj_<site>_ask up to the call, _thunk, _body); the call is
 * checked by its state every gate tick (stop reason 220); restored at stop. X4's exceptions end the process, so there
 * is no counterpart to Windows' __try around the job. Status installed=<sites>/3 why= inlined=<area>/<construction>/<probe>
 * forwarded= failed=. */
#define LXAJ_QUEUE_VA 0x1a55930u
#define LXAJ_DOMAIN 0x58344c415245414aULL /* "X4LAREAJ", Windows' AREAJ_DOMAIN */
#define LXAJ_SITES 3u
#define LXAJ_STOP_REASON 220u
typedef int (*lxaj_queue_fn)(void *,void *,void *,unsigned,unsigned);
typedef void (*lxaj_job_fn)(void *,void *);
typedef struct {
    const char *name;
    uint32_t call,thunk,body;
    const char *pins[3];
    int installed;volatile int64_t inlined;
} lxaj_site;
static lxaj_site lxaj_sites[LXAJ_SITES]={
    {"area",0x1d1fba3u,0x1297850u,0x12815c0u,{"areaj_area_ask","areaj_area_thunk","areaj_area_body"},0,0},
    {"construction",0xe57e44u,0x189b230u,0x1828fa0u,{"areaj_construction_ask","areaj_construction_thunk","areaj_construction_body"},0,0},
    {"probe",0x12e94e6u,0x12f69b0u,0x12cda50u,{"areaj_probe_ask","areaj_probe_thunk","areaj_probe_body"},0,0},
};
static unsigned char *lxaj_page;
static const char *lxaj_why="-";
static volatile int64_t lxaj_forwarded,lxaj_failed;
static uint64_t lxaj_tick=UINT64_MAX;
static uint64_t lxaj_ordinal;
static lxaj_queue_fn lxaj_queue_override;   /* tests: in place of QueueJob */
static lxaj_job_fn lxaj_job_override;       /* tests: in place of the site's callback */
/* The site whose job this is: its callback, and the payload's body. */
static lxaj_site *lxaj_site_of(const void *callback,const void *payload) {
    void *object=NULL,*body=NULL;
    if(!callback || !payload || !gate_read_local(payload,&object,8) || !gate_read_local((const unsigned char *)payload+8,&body,8) || !object) return NULL;
    for(unsigned i=0;i<LXAJ_SITES;i++)
        if(lxaj_sites[i].installed && callback==(const void *)LX(lxaj_sites[i].thunk) && body==(void *)LX(lxaj_sites[i].body)) return &lxaj_sites[i];
    return NULL;
}
/* The job on the asking thread with a job thread's TLS context, then the thread's own context back. */
static void lxaj_run(lxaj_job_fn job,void *payload,unsigned char *tls) {
    uint64_t old_time,old_previous,old_rng,zero=0;uint32_t old_view,zero_view=0;
    if(stats.issued!=lxaj_tick) {lxaj_tick=stats.issued;lxaj_ordinal=0;}
    const uint64_t seed=lockstep_seed(LXAJ_DOMAIN,stats.issued,++lxaj_ordinal,0,0,0);
    memcpy(&old_time,tls+GATE_TLS_LINUX_TIME,8);memcpy(&old_previous,tls+GATE_TLS_LINUX_PREVIOUS_TIME,8);
    memcpy(&old_rng,tls+GATE_TLS_LINUX_RNG,8);memcpy(&old_view,tls+GATE_TLS_LINUX_VIEW,4);
    memcpy(tls+GATE_TLS_LINUX_TIME,&zero,8);memcpy(tls+GATE_TLS_LINUX_PREVIOUS_TIME,&zero,8);
    memcpy(tls+GATE_TLS_LINUX_RNG,&seed,8);memcpy(tls+GATE_TLS_LINUX_VIEW,&zero_view,4);
    job(payload,NULL);
    memcpy(tls+GATE_TLS_LINUX_TIME,&old_time,8);memcpy(tls+GATE_TLS_LINUX_PREVIOUS_TIME,&old_previous,8);
    memcpy(tls+GATE_TLS_LINUX_RNG,&old_rng,8);memcpy(tls+GATE_TLS_LINUX_VIEW,&old_view,4);
}
static int lxaj_queue_impl(lxaj_queue_fn queue,unsigned char *tls,void *manager,void *callback,void *payload,unsigned category,unsigned priority) {
    lxaj_site *s=tls?lxaj_site_of(callback,payload):NULL;
    if(!s) {__atomic_add_fetch(&lxaj_forwarded,1,__ATOMIC_SEQ_CST);return queue(manager,callback,payload,category,priority);}
    lxaj_run(lxaj_job_override?lxaj_job_override:(lxaj_job_fn)callback,payload,tls);
    __atomic_add_fetch(&s->inlined,1,__ATOMIC_SEQ_CST);
    return 0;
}
static int lxaj_queue(void *manager,void *callback,void *payload,unsigned category,unsigned priority) {
    return lxaj_queue_impl(lxaj_queue_override?lxaj_queue_override:(lxaj_queue_fn)(void *)LX(LXAJ_QUEUE_VA),gate_tls_self(),
                           manager,callback,payload,category,priority);
}
/* A site's call: 0 QueueJob, 1 our page, -1 anything else. */
static int lxaj_rel(uint32_t call,const unsigned char *target,int32_t *out) {
    const intptr_t d=(intptr_t)target-(intptr_t)(LX(call)+5);
    if(d!=(intptr_t)(int32_t)d) return 0;
    *out=(int32_t)d;return 1;
}
static int lxaj_state(const lxaj_site *s) {
    unsigned char now[5];int32_t d,want;
    if(!gate_read_local(LX(s->call),now,5) || now[0]!=0xe8) return -1;
    memcpy(&d,now+1,4);
    if(lxaj_rel(s->call,LX(LXAJ_QUEUE_VA),&want) && d==want) return 0;
    return lxaj_page && lxaj_rel(s->call,lxaj_page,&want) && d==want?1:-1;
}
static int lxaj_swap(const lxaj_site *s,const unsigned char *from,const unsigned char *to) {
    unsigned char *line=(unsigned char *)((uintptr_t)LX(s->call)&~(uintptr_t)15);const unsigned at=(unsigned)(LX(s->call)-line);
    unsigned __int128 current,desired;int32_t f,t;
    if(at+5>16 || !lxaj_rel(s->call,from,&f) || !lxaj_rel(s->call,to,&t)) return 0;
    memcpy(&current,line,16);
    if(((unsigned char *)&current)[at]!=0xe8 || memcmp((unsigned char *)&current+at+1,&f,4)) return 0;
    desired=current;memcpy((unsigned char *)&desired+at+1,&t,4);
    return gate_code_swap128(line,current,desired);
}
static int lxaj_pins(const lxaj_site *s) {
    for(unsigned k=0;k<3;k++) if(!site_ok(site_named(s->pins[k]))) return 0;
    return 1;
}
static int lxaj_installed_count(void) {int n=0;for(unsigned i=0;i<LXAJ_SITES;i++) n+=lxaj_sites[i].installed!=0;return n;}
static void lxaj_jump(unsigned char out[14]) {
    const void *to=(const void *)lxaj_queue;out[0]=0xff;out[1]=0x25;memset(out+2,0,4);memcpy(out+6,&to,8);
}
static int lxaj_page_ok(void) {unsigned char want[14];if(!lxaj_page) return 0;lxaj_jump(want);return !memcmp(lxaj_page,want,14);}
/* gate.area_jobs(1), before the first admitted frame on the UI thread. 1: every site is in (also on an identical rerun). */
static int lxaj_arm(void) {
    if(lxaj_installed_count()==(int)LXAJ_SITES) return stats.active;
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {lxaj_why="guard";return 0;}
    for(unsigned i=0;i<LXAJ_SITES;i++) {
        if(lxaj_state(&lxaj_sites[i])!=0) {lxaj_why="site";return 0;}
        if(!lxaj_pins(&lxaj_sites[i])) {lxaj_why="pins";return 0;}
    }
    if(!lxaj_page) lxaj_page=gate_near_page(LX(lxaj_sites[0].call));
    if(!lxaj_page || mprotect(lxaj_page,GATE_PAGE,PROT_READ|PROT_WRITE)) {lxaj_why="page";return 0;}
    memset(lxaj_page,0xcc,GATE_PAGE);lxaj_jump(lxaj_page);
    if(mprotect(lxaj_page,GATE_PAGE,PROT_READ|PROT_EXEC)) {lxaj_why="page";return 0;}
    for(unsigned i=0;i<LXAJ_SITES;i++) lxaj_sites[i].inlined=0;
    lxaj_forwarded=lxaj_failed=0;lxaj_tick=UINT64_MAX;lxaj_ordinal=0;
    /* the site field first: a job asked the moment the call is ours finds its site installed */
    for(unsigned i=0;i<LXAJ_SITES;i++) {
        lxaj_sites[i].installed=1;
        if(!lxaj_swap(&lxaj_sites[i],LX(LXAJ_QUEUE_VA),lxaj_page)) {lxaj_sites[i].installed=0;lxaj_why="cas";break;}
    }
    if(lxaj_installed_count()!=(int)LXAJ_SITES) {
        for(unsigned i=0;i<LXAJ_SITES;i++)
            if(lxaj_sites[i].installed && lxaj_swap(&lxaj_sites[i],lxaj_page,LX(LXAJ_QUEUE_VA))) lxaj_sites[i].installed=0;
        return 0;
    }
    lxaj_why="-";return 1;
}
/* Every gate tick while installed: each call still ours, the page, the pins. */
static int lxaj_check(void) {
    if(!lxaj_page_ok()) {lxaj_why="page_changed";return 0;}
    for(unsigned i=0;i<LXAJ_SITES;i++)
        if(lxaj_sites[i].installed && (lxaj_state(&lxaj_sites[i])!=1 || !lxaj_pins(&lxaj_sites[i]))) {lxaj_why="site_changed";return 0;}
    return 1;
}
static void lxaj_restore(void) {
    for(unsigned i=0;i<LXAJ_SITES;i++)
        if(lxaj_sites[i].installed && lxaj_state(&lxaj_sites[i])==1 && lxaj_swap(&lxaj_sites[i],lxaj_page,LX(LXAJ_QUEUE_VA))) lxaj_sites[i].installed=0;
}
static int lxaj_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity,"installed=%d/%u why=%s inlined=%lld/%lld/%lld forwarded=%lld failed=%lld",lxaj_installed_count(),
                         LXAJ_SITES,lxaj_why,(long long)lxaj_sites[0].inlined,(long long)lxaj_sites[1].inlined,(long long)lxaj_sites[2].inlined,
                         (long long)lxaj_forwarded,(long long)lxaj_failed);
    return n>0 && (size_t)n<capacity?n:0;
}
static int lua_area_jobs(lua_State *L) {p_lua_pushnumber(L,p_lua_tonumber(L,1)==1.0 && accept_ui() && lxaj_arm());return 1;}
static int lua_area_jobs_status(lua_State *L) {
    char out[192];const int n=lxaj_status(out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}

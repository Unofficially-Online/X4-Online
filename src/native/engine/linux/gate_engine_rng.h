/* A ship's next engine-parameter refresh rescheduled from a keyed stream on the Linux X4 9.00
 * executable: the Linux twin of lockstep_engine_rng.h (native_gate_engine_refresh_rng), whose domain,
 * counters and status are shared, and whose seeds this reproduces, so a refresh is due at the same
 * time on both executables. Included by gate_linux.c only.
 *
 * Linux sites: the listener (Windows NavigationController::OnUpdateEngineParameters 0xea1d70) is
 * 0x14382f0. As on Windows it refreshes the slot when +0xd8 is clear (Windows +0xc8; the call at
 * 0x1438408, then back to 0x1438305), asks whether event 0x20f is already due for its owner
 * [+0x20] (Windows [+0x18]), and if not posts the next UpdateEngineParametersEvent at
 * CurTime + 8.0 + FloatRange(word, 2.0). GCC inlined FloatRange (Windows 0x14ddc90): 0x1438348..
 * 0x1438381 steps the thread's word in place, ror(word*0x5851f42d4c957f2d+0x14057b7ef767814f, 30)
 * at fs:-0x2720, then takes its low 32 bits * 2^-32 + itself (Windows * 2.0: the same value), and
 * adds 8.0 + CurTime (Windows CurTime + 8.0 + the draw: the same sum). Both constants are those
 * of Windows (2^-32, 8.0).
 *
 * With this, the aligned word at 0x1438348 (movabs rax, the multiplier) starts with a jmp to a stub
 * on a private page near the image, which keeps the function's red zone, saves xmm1 (8.0), calls
 * erng_lx_step(owner in rbx), restores xmm1, replays the displaced mov esi,0x10 / mov edi,0x18 and
 * the addsd of CurTime (its bytes copied from the pinned image) and jumps on to 0x143838a with the
 * stepped state in rax. In admitted frames the state is the Windows keyed one, (tick, the owner's
 * portable key), or for an owner without a key (tick, its class's Windows vtable RVA via the class
 * table, as cam-L1's fallback), and the thread's word is left untouched; otherwise the thread's
 * word steps as the native code would. Pins: the listener's head up to the word and its tail with
 * the refresh path (lx_sites erng_listener_head/_tail), the word itself original or ours, checked
 * at arm and every gate tick (stop reason 136, as on Windows); restored at stop. */
#define ERNG_OWN_PLATFORM
#include "lockstep_engine_rng.h"
#define ERNG_PCG_MUL 0x5851f42d4c957f2dull
#define ERNG_PCG_INC 0x14057b7ef767814full
#define ERNG_STUB_HELPER 0x40u  /* abs64 erng_lx_step */
#define ERNG_STUB_RESUME 0x48u  /* abs64 the listener's 0x143838a */
static uint64_t erng_lx_pcg(uint64_t s) {s=s*ERNG_PCG_MUL+ERNG_PCG_INC;return (s>>30)|(s<<34);}
/* The Windows erng_run's choice of state for one draw (testable): 1 with *state set when the draw
 * is keyed or falls back, 0 when it runs on the thread's word. */
static int erng_lx_state(const void *ship,uint64_t *state) {
    if(!(stats.active && boot_phase==3)) {++erng_native;return 0;}
    uint64_t runtime=0,key=0;unsigned klass=WORLD_KEY_NONE;
    if(ship && read_local((const unsigned char*)ship+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
    if(!key) {
        void *vt=NULL;const lxw_class *c=ship && read_local(ship,&vt,8)?lxw_class_of((uintptr_t)vt):NULL;
        if(c) {*state=lockstep_seed(ERNG_DOMAIN,stats.issued,0,c->windows_rva,1,WORLD_KEY_NONE);++erng_fallback;return 1;}
        ++erng_unmapped;return 0;
    }
    *state=lockstep_seed(ERNG_DOMAIN,stats.issued,key,0,0,klass);++erng_keyed;
    return 1;
}
/* Called from the stub: the stepped state the inline code goes on with. */
static uint64_t erng_lx_step(const void *ship) {
    uint64_t state=0;
    if(erng_lx_state(ship,&state)) return erng_lx_pcg(state);
    uint64_t *word=gate_tls_rng(gate_tls_self());
    return *word=erng_lx_pcg(*word);
}
static int erng_lx_word_state(void) { /* 0 original, 1 ours, -1 anything else */
    uint64_t now;if(!gate_read_local(LX(LX_ERNG_SITE_VA),&now,8)) return -1;
    if(now==LX_ERNG_SITE_ORIGINAL) return 0;
    if(!erng_page) return -1;
    unsigned char want[8];memcpy(want,&now,8);
    int32_t d=(int32_t)((intptr_t)erng_page-(intptr_t)(LX(LX_ERNG_SITE_VA)+5));
    unsigned char ours[8];uint64_t original=LX_ERNG_SITE_ORIGINAL;memcpy(ours,&original,8);ours[0]=0xe9;memcpy(ours+1,&d,4);
    return memcmp(want,ours,8)?-1:1;
}
static const lx_site *erng_site(const char *name) {
    for(unsigned i=0;i<gate_site_count;i++) if(!strcmp(gate_sites[i].name,name)) return &gate_sites[i];
    return NULL;
}
static int erng_pins(void) {
    return erng_lx_word_state()>=0 && site_ok(erng_site("erng_listener_head")) && site_ok(erng_site("erng_listener_tail"));
}
/* sub rsp,0xa0 (past the red zone, 16-aligned); movsd [rsp],xmm1; mov rdi,rbx; call [rip->helper]; movsd xmm1,[rsp];
 * add rsp,0xa0; mov esi,0x10; mov edi,0x18; the pinned addsd xmm1,fs:CurTime; jmp [rip->resume]; then the two pointers. */
static int erng_write_stub(unsigned char *p) {
    static const unsigned char head[]={0x48,0x81,0xec,0xa0,0,0,0, 0xf2,0x0f,0x11,0x0c,0x24, 0x48,0x89,0xdf, 0xff,0x15,0,0,0,0,
                                       0xf2,0x0f,0x10,0x0c,0x24, 0x48,0x81,0xc4,0xa0,0,0,0, 0xbe,0x10,0,0,0, 0xbf,0x18,0,0,0};
    size_t n=sizeof(head);memcpy(p,head,n);
    int32_t call=(int32_t)(ERNG_STUB_HELPER-(15+6));memcpy(p+17,&call,4);
    if(!gate_read_local(LX(LX_ERNG_ADDSD_VA),p+n,LX_ERNG_ADDSD_LENGTH)) return 0;
    n+=LX_ERNG_ADDSD_LENGTH;
    p[n]=0xff;p[n+1]=0x25;int32_t jump=(int32_t)(ERNG_STUB_RESUME-(n+6));memcpy(p+n+2,&jump,4);n+=6;
    if(n>ERNG_STUB_HELPER) return 0;
    void *helper=(void*)erng_lx_step,*resume=LX(LX_ERNG_RESUME_VA);
    memcpy(p+ERNG_STUB_HELPER,&helper,8);memcpy(p+ERNG_STUB_RESUME,&resume,8);
    return 1;
}
static int erng_stub_ok(void) {
    unsigned char want[0x50];memset(want,0xcc,sizeof(want));
    return erng_page && erng_write_stub(want) && !memcmp(erng_page,want,ERNG_STUB_RESUME+8);
}
static uint64_t erng_lx_patched(void) {
    uint64_t word=LX_ERNG_SITE_ORIGINAL;unsigned char b[8];memcpy(b,&word,8);
    int32_t d=(int32_t)((intptr_t)erng_page-(intptr_t)(LX(LX_ERNG_SITE_VA)+5));b[0]=0xe9;memcpy(b+1,&d,4);memcpy(&word,b,8);
    return word;
}
/* Before the first admitted frame, on the MoveWorker; needs the world identities. */
static int erng_arm(void) {
    if(erng_installed) return stats.active!=0;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed) {erng_stage="needs";return 0;}
    if(!erng_pins() || erng_lx_word_state()!=0) {erng_stage="pins";return 0;}
    if(!erng_page) erng_page=gate_near_page(LX(LX_ERNG_SITE_VA));
    if(!erng_page) {erng_stage="page";return 0;}
    intptr_t distance=(intptr_t)erng_page-(intptr_t)(LX(LX_ERNG_SITE_VA)+5);
    if(distance<INT32_MIN || distance>INT32_MAX) {erng_stage="page";return 0;}
    if(mprotect(erng_page,GATE_PAGE,PROT_READ|PROT_WRITE)) {erng_stage="page";return 0;}
    memset(erng_page,0xcc,GATE_PAGE);
    if(!erng_write_stub(erng_page) || mprotect(erng_page,GATE_PAGE,PROT_READ|PROT_EXEC)) {erng_stage="page";return 0;}
    erng_keyed=erng_native=erng_unmapped=erng_fallback=0;erng_failures=0;erng_restored=0;
    if(!gate_code_swap64(LX(LX_ERNG_SITE_VA),LX_ERNG_SITE_ORIGINAL,erng_lx_patched())) {erng_stage="cas";return 0;}
    erng_done=1;erng_installed=1;erng_stage="armed";return 1;
}
static int erng_owned(void) {return !erng_done || (erng_lx_word_state()==1 && erng_stub_ok());}
/* Main-thread tick check; any failure stops the gate with reason 136 (ERNG_STOP_REASON). */
static int erng_check(void) {
    if(InterlockedCompareExchange64(&erng_failures,0,0)) return 0;
    const char *why=!erng_owned()?"site_changed":!erng_pins()?"block_changed":NULL;
    if(why) {erng_stage=why;InterlockedIncrement64(&erng_failures);return 0;}
    return 1;
}
static void erng_restore(void) {
    if(!erng_installed) return;
    if(erng_done && erng_lx_word_state()==1 && gate_code_swap64(LX(LX_ERNG_SITE_VA),erng_lx_patched(),LX_ERNG_SITE_ORIGINAL)) erng_done=0;
    if(!erng_done) {erng_installed=0;erng_restored=1;}
}
static int lua_engine_refresh_rng(lua_State *L) {p_lua_pushnumber(L,erng_arm());return 1;}
static int lua_engine_refresh_rng_status(lua_State *L) {
    char out[512];int n=accept_ui()?erng_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}

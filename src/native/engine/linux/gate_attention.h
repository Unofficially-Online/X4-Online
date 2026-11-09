/* Every portable object's attention timer on the Linux X4 9.00 executable: the Linux twin of
 * lockstep_attention.h's 'all' (native_gate_attention_all, with the hook from native_gate_attention_rng or
 * _initial_refresh), whose first-call deadline, keyed word, table and status are shared. Included by gate_linux.c
 * only. The mechanism is in that header: the load leaves each object's next due at a value from that client's
 * loading frames, so the first re-evaluation lands on a client-dependent tick and draws from the natural word.
 *
 * win-sub4 shows why it comes now: of about 23,000 frame-level draws, only two call chains are ever on one client
 * alone, and this timer is one of them (49 of 113 draws, the first at tick 47). The Linux word first parts at tick
 * 46-47 once the nav deadlines agree (runs 24, 24L, 25L).
 *
 * Linux site: PeriodicAttentionRefresh 0x1465030 (Windows 0x6d1ff0): when the thread's game time (fs:-0x2728)
 * reaches object+0x2d8 (Windows +0x2e0) it sets +0x2d8 = now + 0.75 + the thread word (fs:-0x2720) as a float of
 * 0.5, inlined PCG, and tail-jumps to SetAttention. Its first 5 bytes (push rbp; push rbx; mov rbx,rdi) move into
 * a trampoline; the entry's aligned 8 bytes change in one compare-exchange.
 *
 * The battle fixture's own path (Windows attention_refresh's fixture branch and attention_execute; boot options
 * 2^28 attention_diag, 2^29 attention_rng, 2^30 attention_initial_refresh, any of them installs the hook, as on
 * Windows): a bound actor's ship (gate_battle.h, its runtime ID unchanged) takes it in an admitted frame whatever
 * 'all' says: per tick a call ordinal; _initial_refresh sets the due to time - dt at the actor's first call; _rng runs
 * the call with the word seeded from actor_seed("ATTENTIO", tick, actor, 0, ordinal, 0) and gives it back; the
 * ATTENTION row (gate.attention) is Windows' text. Without it the fixture took 'all' (the ATTALLKY key), so its timer
 * drew other dues than on Windows. With only the hook, every other call runs natively. */
#define ATTENTION_OWN_PLATFORM
#define ATTENTION_DUE LX_ATTENTION_DUE
#define LXA_THUNK 0x780u /* the relocated prologue, then jmp [rip] -> the timer + 5 */
#define LXA_HOOK 0x7c0u  /* jmp [rip] -> lxa_refresh */
#define __try
#define __finally
#define GetLastError() 0u
#define SetLastError(error) ((void)(error))
#define AbnormalTermination() 0 /* X4's exceptions end the process */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable" /* the fixture actors' counters and flags */
#include "lockstep_attention.h"
#pragma GCC diagnostic pop
#undef __try
#undef __finally
#undef GetLastError
#undef SetLastError
#undef AbnormalTermination
/* The battle fixture's path. */
#define LXA_ACTORS 12u
#define LXA_FIXTURE_DOMAIN 0x415454454e54494fULL /* "ATTENTIO", Windows' attention_execute */
#define LXA_LEVEL 0x1ecu                         /* the attention level, as on Windows */
typedef struct {uint64_t tick,calls,time,before,after,due_before,due_after;unsigned level_before,level_after,initialized;} lxa_sample;
static lxa_sample lxa_samples[LXA_ACTORS];
static unsigned char lxa_initialized[LXA_ACTORS];
static uint64_t lxa_inits;
static unsigned lxb_fixture_ship(void *self); /* gate_battle.h: the actor 1..12, else 0 */
static void lxa_fixture(attention_fn original,void *self,unsigned actor) {
    lxa_sample *s=&lxa_samples[actor-1];unsigned char *object=self,*base=gate_tls_self();
    if(s->tick!=stats.issued) {s->tick=stats.issued;s->calls=0;}
    ++s->calls;++attention_calls;
    uint64_t *rng=base?gate_tls_rng(base):NULL;double time=NAN;
    if(base) memcpy(&time,gate_tls_time(base),8);
    if(!rng || !isfinite(time)) {++attention_failures;original(self);return;}
    memcpy(&s->time,&time,8);
    const uint64_t saved=*rng;s->initialized=0;
    memcpy(&s->due_before,object+ATTENTION_DUE,8);memcpy(&s->level_before,object+LXA_LEVEL,4);
    if(attention_initial && !lxa_initialized[actor-1]) {
        const double due=time-gate_tick_dt();memcpy(object+ATTENTION_DUE,&due,8);
        lxa_initialized[actor-1]=1;s->initialized=1;++lxa_inits;
    }
    if(attention_seeded) *rng=lockstep_seed(LXA_FIXTURE_DOMAIN,stats.issued,actor,0,s->calls,0); /* actor_seed: the fixture's number */
    s->before=*rng;
    original(self); /* X4's exceptions end the process: nothing to unwind */
    s->after=*rng;
    if(attention_seeded) *rng=saved;
    memcpy(&s->due_after,object+ATTENTION_DUE,8);memcpy(&s->level_after,object+LXA_LEVEL,4);
}
static int lxa_text(unsigned actor,char *out,size_t size) {
    if(!attention_installed || actor<1 || actor>LXA_ACTORS) return 0;
    const lxa_sample *s=&lxa_samples[actor-1];
    const int n=snprintf(out,size,"tick=%llu calls=%llu time=%016llx level_before=%u level_after=%u due_before=%016llx due_after=%016llx initialized=%u rng_before=%016llx rng_after=%016llx",
        (unsigned long long)s->tick,(unsigned long long)s->calls,(unsigned long long)s->time,s->level_before,s->level_after,
        (unsigned long long)s->due_before,(unsigned long long)s->due_after,s->initialized,(unsigned long long)s->before,(unsigned long long)s->after);
    return n>0 && (size_t)n<size?n:0;
}
static int lua_attention(lua_State *L) {
    const double actor=p_lua_tonumber(L,1);char out[384];
    const int n=accept_ui() && isfinite(actor) && actor>=1 && actor<=LXA_ACTORS && floor(actor)==actor?lxa_text((unsigned)actor,out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static void lxa_refresh(void *self) {
    attention_fn original=(attention_fn)(void*)(thunk+LXA_THUNK);
    const int admitted=stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread;
    const unsigned actor=admitted?lxb_fixture_ship(self):0;
    if(actor) {lxa_fixture(original,self,actor);return;}
    if(attention_all_armed && admitted) {
        unsigned char *base=gate_tls_self();double time=NAN;
        if(base) memcpy(&time,gate_tls_time(base),8);
        attention_all_apply(original,self,gate_tls_rng(base),time);return;
    }
    original(self);
}
static void lxa_thunk_write(unsigned char *page) {
    lxw_jump_write(page+LXA_HOOK,(void*)lxa_refresh);
    unsigned char *p=page+LXA_THUNK;memcpy(p,"\x55\x53\x48\x89\xfb",LX_ATTENTION_PROLOGUE); /* push rbp; push rbx; mov rbx,rdi */
    lxw_jump_write(p+LX_ATTENTION_PROLOGUE,LX(LX_ATTENTION_VA)+LX_ATTENTION_PROLOGUE);
}
/* From the start options, in the boot before loading; a rerun keeps the installed hook. seed and initial: the
 * fixture path's (Windows attention_install). */
static int attention_install(int seed,int initial) {
    if(attention_installed) return seed==attention_seeded && initial==attention_initial;
    if(!boot_mode || !thunk || rd64(LX(LX_ATTENTION_VA))!=LX_ATTENTION_ENTRY_ORIGINAL) return 0;
    attention_original_word=LX_ATTENTION_ENTRY_ORIGINAL;
    if(!lxw_patch(LX(LX_ATTENTION_VA),attention_original_word,LXA_HOOK,&attention_patched_word)) return 0;
    memset(lxa_samples,0,sizeof(lxa_samples));memset(lxa_initialized,0,sizeof(lxa_initialized));lxa_inits=0;
    attention_seeded=seed;attention_initial=initial;attention_installed=1;attention_restored=0;return 1;
}
static void attention_restore(void) {
    if(!attention_installed) return;
    attention_restored=gate_code_swap64(LX(LX_ATTENTION_VA),attention_patched_word,attention_original_word);
    if(attention_restored) attention_installed=0;
}
static int attention_owned(void) {return !attention_installed || rd64(LX(LX_ATTENTION_VA))==attention_patched_word;}
static int lua_attention_all(lua_State *L) {p_lua_pushnumber(L,attention_all_arm());return 1;}
static int lua_attention_all_status(lua_State *L) {
    char out[512];int n=accept_ui()?attention_all_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}

/* Per-ship RNG keys for every ship with a portable identity on the Linux X4 9.00 executable: the Linux twin of
 * lockstep_actor_all.h (native_gate_actor_rng_all), whose scopes, keys, ordinals, tallies, rows and status are shared,
 * so a ship's scope draws the same seed on both executables. Included by gate_linux.c only.
 *
 * The two umbrella scopes and their hooks:
 *  - ship: Ship Move (Windows 0x7cc810) is 0x12b81d0, in slot +0x1558 (Windows +0x1510) of every ship vtable. Windows
 *    swaps the slot of U::ShipSmall and U::ShipExtraLarge only (lockstep_ship.h SHIP_ENTRY, SHIP_XL_ENTRY), although
 *    ShipMedium and ShipLarge reach the same Move; to keep both executables' streams equal this swaps the same two:
 *    U::ShipSmall 0x3d21748 (slot 0x3d22ca0) and U::ShipExtraLarge 0x3d3ee90 (slot 0x3d403e8). Sim thread only.
 *  - agent: the pathing Agent update (Windows 0xe8f250, its call at 0xeaed6f) is 0x1444c70, called only at 0x13e33a2 in
 *    the batch job 0x13e32f0 (Windows 0xeaecc0) as (agent, commands, dt xmm0, time xmm1, cheap dl), Windows' arguments in
 *    System V order; the call's rel32 points (CAS of the aligned 16 bytes) at a jmp stub on a private page. Any thread.
 *    The agent's object is +0xf10 (Windows +0xef0, LXW_AGENT_OWNER).
 * Boot options 256 (native_gate_agent_rng) and 2048 (native_gate_ship_rng) install the hooks (no fixture actors on
 * Linux yet: every call takes the all-ship path); gate.actor_rng_all() arms the keys before the first admitted frame.
 * Not ported: the steering, navigation and weapon fallbacks (Windows counts them 'outside', expected zero), and the
 * draw watch. Pins: Ship Move's head and the agent update's head; the call's block is path_update_batch's (checked at
 * the gate's start); the slots and the call as found. */
#define ACTOR_ALL_OWN_PLATFORM
#define ACTOR_ALL_AGENT_OBJECT LXW_AGENT_OWNER
#define __try
#define __finally
#define AbnormalTermination() 0
#define LONG long /* the status prints the armed word with %ld */
/* The shared header's three per-thread variables. A _Thread_local of this module would need __tls_get_addr (the dynamic
 * loader, which the gate does not link) and space in a dlopen'd static TLS block; pthread keys need neither. The agent
 * depth must be per thread: the pathing JobThreads run agent scopes in parallel. */
typedef struct {int shard_slot;unsigned timer,agent_depth;} laa_thread_t;
static pthread_key_t laa_key;
static pthread_once_t laa_key_once=PTHREAD_ONCE_INIT;
static laa_thread_t laa_thread_spare={-1,0,0};
static void laa_key_make(void) {if(pthread_key_create(&laa_key,free)) laa_key=(pthread_key_t)-1;}
static laa_thread_t *laa_thread_here(void) {
    pthread_once(&laa_key_once,laa_key_make);
    if(laa_key==(pthread_key_t)-1) return &laa_thread_spare;
    laa_thread_t *t=pthread_getspecific(laa_key);
    if(!t) {t=calloc(1,sizeof(*t));if(!t) return &laa_thread_spare;t->shard_slot=-1;if(pthread_setspecific(laa_key,t)) {free(t);return &laa_thread_spare;}}
    return t;
}
#define actor_all_shard_slot (laa_thread_here()->shard_slot)
#define actor_all_timer (laa_thread_here()->timer)
#define actor_all_agent_depth (laa_thread_here()->agent_depth)
#ifndef InterlockedIncrement
#define InterlockedIncrement(p) __atomic_add_fetch((p),1,__ATOMIC_SEQ_CST)
#endif
#ifndef InterlockedExchangeAdd64
#define InterlockedExchangeAdd64(p,v) __atomic_fetch_add((p),(v),__ATOMIC_SEQ_CST)
#endif
#include "lockstep_actor_all.h"
#undef __try
#undef __finally
#undef AbnormalTermination
#undef LONG
#define LAA_SHIP_SLOTS 2u
#define LAA_STUB_AGENT 0x00u
static const uint32_t laa_ship_slots[LAA_SHIP_SLOTS]={LX_SHIP_SMALL_MOVE_SLOT_VA,LX_SHIP_XL_MOVE_SLOT_VA};
static int laa_ship_installed,laa_agent_installed,laa_restored;
static unsigned laa_ship_swapped;
static unsigned char *laa_page;
static volatile int64_t laa_failures;
static const char *laa_stage="idle";
typedef void (*laa_ship_fn)(void *);
typedef void (*laa_agent_fn)(void *,void *,float,double,unsigned char);
static void laa_ship_move(void *self) {
    laa_ship_fn original=(laa_ship_fn)(void*)LX(LX_SHIP_MOVE_VA);
    if(stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread && actor_all_on()) actor_all_ship_execute(original,self,actor_all_tls_rng());
    else original(self);
}
static void laa_agent_update(void *self,void *commands,float dt,double time,unsigned char cheap) {
    laa_agent_fn original=(laa_agent_fn)(void*)LX(LX_AGENT_UPDATE_VA);
    if(stats.active && boot_phase==3 && actor_all_on()) actor_all_agent_execute(original,self,commands,dt,time,cheap,actor_all_tls_rng());
    else original(self,commands,dt,time,cheap);
}
static const lx_site *laa_site(const char *name) {
    for(unsigned i=0;i<gate_site_count;i++) if(!strcmp(gate_sites[i].name,name)) return &gate_sites[i];
    return NULL;
}
static void *laa_slot(unsigned i) {void *v=NULL;return gate_read_local(LX(laa_ship_slots[i]),&v,8)?v:NULL;}
/* 0 original, 1 ours, -1 anything else. */
static int laa_call_state(void) {
    unsigned char op;int32_t d;
    if(!gate_read_local(LX(LX_AGENT_CALL_VA),&op,1) || op!=0xe8 || !gate_read_local(LX(LX_AGENT_CALL_VA)+1,&d,4)) return -1;
    unsigned char *to=LX(LX_AGENT_CALL_VA)+5+d;
    if(to==LX(LX_AGENT_UPDATE_VA)) return 0;
    if(laa_page && to==laa_page+LAA_STUB_AGENT) return 1;
    return -1;
}
static int laa_call_block(const void *target,unsigned __int128 *out) {
    unsigned at=LX_AGENT_CALL_VA+1-LX_AGENT_CALL_BLOCK_VA;unsigned char b[16];
    if(at>12u) return 0;
    memcpy(b,LX(LX_AGENT_CALL_BLOCK_VA),16);
    int32_t d=(int32_t)((intptr_t)target-(intptr_t)(LX(LX_AGENT_CALL_VA)+5));memcpy(b+at,&d,4);memcpy(out,b,16);return 1;
}
static int laa_call_swap(const void *from,const void *to) {
    unsigned __int128 e,v;return laa_call_block(from,&e) && laa_call_block(to,&v) && gate_code_swap128(LX(LX_AGENT_CALL_BLOCK_VA),e,v);
}
/* The heads of Ship Move and the agent update; the call's block is lx_sites' path_update_batch, checked at the gate's start
 * (gate_path_budget.h patches 16 bytes inside it later, so it is not rehashed here): the call itself must be the
 * original or ours. */
static int laa_pins(void) {
    return site_ok(laa_site("ship_move_head")) && site_ok(laa_site("agent_update_head")) && laa_call_state()>=0;
}
/* Boot option 2048: the two Move slots. */
static int laa_ship_install(void) {
    if(laa_ship_installed) return 1;
    void *native=LX(LX_SHIP_MOVE_VA);
    if(!laa_pins()) {laa_stage="pins";return 0;}
    for(unsigned i=0;i<LAA_SHIP_SLOTS;i++) if(laa_slot(i)!=native) {laa_stage="slot";return 0;}
    for(unsigned i=0;i<LAA_SHIP_SLOTS;i++) {
        if(!gate_data_swap64(LX(laa_ship_slots[i]),(uint64_t)(uintptr_t)native,(uint64_t)(uintptr_t)laa_ship_move)) {
            for(unsigned j=0;j<i;j++) if(gate_data_swap64(LX(laa_ship_slots[j]),(uint64_t)(uintptr_t)laa_ship_move,(uint64_t)(uintptr_t)native)) laa_ship_swapped&=~(1u<<j);
            laa_stage="cas";return 0;
        }
        laa_ship_swapped|=1u<<i;
    }
    laa_ship_installed=1;return 1;
}
/* Boot option 256: the agent update's call. */
static int laa_agent_install(void) {
    if(laa_agent_installed) return 1;
    if(!laa_pins() || laa_call_state()!=0) {laa_stage="pins";return 0;}
    if(!laa_page) laa_page=gate_near_page(LX(LX_AGENT_CALL_VA));
    if(!laa_page) {laa_stage="page";return 0;}
    intptr_t distance=(intptr_t)(laa_page+LAA_STUB_AGENT)-(intptr_t)(LX(LX_AGENT_CALL_VA)+5);
    if(distance<INT32_MIN || distance>INT32_MAX || mprotect(laa_page,GATE_PAGE,PROT_READ|PROT_WRITE)) {laa_stage="page";return 0;}
    memset(laa_page,0xcc,GATE_PAGE);
    void *t=(void*)laa_agent_update;unsigned char *s=laa_page+LAA_STUB_AGENT;s[0]=0xff;s[1]=0x25;memset(s+2,0,4);memcpy(s+6,&t,8);
    if(mprotect(laa_page,GATE_PAGE,PROT_READ|PROT_EXEC)) {laa_stage="page";return 0;}
    if(!laa_call_swap(LX(LX_AGENT_UPDATE_VA),laa_page+LAA_STUB_AGENT)) {laa_stage="cas";return 0;}
    laa_agent_installed=1;return 1;
}
static int laa_stub_ok(void) {
    if (!laa_page) return 0;
    unsigned char expected[14] = {0xff, 0x25, 0, 0, 0, 0};
    void *target = (void *)laa_agent_update;
    memcpy(expected + 6, &target, sizeof(target));
    return !memcmp(laa_page + LAA_STUB_AGENT, expected, sizeof(expected));
}
/* gate.actor_rng_all(): before loading, after the boot installed both hooks (as Windows' agent and ship modules). */
static int laa_arm(void) {
    if(actor_all_armed) return stats.active && laa_ship_installed && laa_agent_installed;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !world_identity_installed ||
       !laa_ship_installed || !laa_agent_installed) return 0;
    QueryPerformanceCounter(&actor_all_qpc0);actor_all_tsc0=__builtin_ia32_rdtsc();
    InterlockedExchange(&actor_all_armed,1);laa_stage="armed";return 1;
}
static int laa_owned(void) {
    if(laa_ship_installed) for(unsigned i=0;i<LAA_SHIP_SLOTS;i++) if((laa_ship_swapped&(1u<<i)) && laa_slot(i)!=(void*)laa_ship_move) return 0;
    if(laa_agent_installed && (laa_call_state()!=1 || !laa_stub_ok())) return 0;
    return 1;
}
/* Main-thread tick check; a failure stops the gate with reason 161 (Linux only: Windows has no tick check here). */
static int laa_check(void) {
    if(InterlockedCompareExchange64(&laa_failures,0,0) || InterlockedCompareExchange64(&actor_all_failures,0,0)) {laa_stage="failed";return 0;}
    const char *why=!laa_owned()?"site_changed":!laa_pins()?"block_changed":actor_all_table_full?"ordinal_table":NULL;
    if(why) {laa_stage=why;InterlockedIncrement64(&laa_failures);return 0;}
    return 1;
}
static void laa_restore(void) {
    InterlockedExchange(&actor_all_armed,0);
    void *native=LX(LX_SHIP_MOVE_VA);
    for(unsigned i=0;i<LAA_SHIP_SLOTS;i++)
        if((laa_ship_swapped&(1u<<i)) && gate_data_swap64(LX(laa_ship_slots[i]),(uint64_t)(uintptr_t)laa_ship_move,(uint64_t)(uintptr_t)native)) laa_ship_swapped&=~(1u<<i);
    if(!laa_ship_swapped) laa_ship_installed=0;
    if(laa_agent_installed && laa_call_state()==1 && laa_call_swap(laa_page+LAA_STUB_AGENT,LX(LX_AGENT_UPDATE_VA))) laa_agent_installed=0;
    if(!laa_ship_installed && !laa_agent_installed) laa_restored=1;
}
static int lua_actor_rng_all(lua_State *L) {p_lua_pushnumber(L,laa_arm());return 1;}
static int lua_actor_rng_all_status(lua_State *L) {
    char out[3072];int n=accept_ui()?actor_all_status(out,sizeof(out)):0;
    if(n>0 && (size_t)n+64<sizeof(out)) n+=snprintf(out+n,sizeof(out)-(size_t)n," linux_stage=%s linux_restored=%d",laa_stage,laa_restored);
    if(n>0 && (size_t)n<sizeof(out)) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_actor_rng_rows(lua_State *L) {
    int ok=actor_all_on() && !stats.issued && GetCurrentThreadId()==ui_thread;
    if(ok && !actor_all_rows_armed) {actor_all_rows=0;InterlockedExchange(&actor_all_rows_armed,1);}
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_actor_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=actor_all_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}

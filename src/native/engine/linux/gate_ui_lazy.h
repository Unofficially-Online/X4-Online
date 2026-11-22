/* native_gate_ui_lazy on the Linux gate (Windows: lockstep_ui_lazy.h, the same modes, rules, rows and status text):
 * the Lua UI's queries do not bring containers up to date, and with 'key' the simulation's own lazy updates draw from
 * a word keyed by what they update, not from the calling thread's stream. Included by gate_linux.c.
 *
 * Linux targets (each found by its own logic and fields against the Windows function, 22 Nov 2026):
 *   cargo  0x184dfa0 (Windows 0x41a910): (container rdi, game time xmm0, force sil); without force it returns when
 *          +0xa48 (Windows +0xa18), the time it was last brought to, is set and not older; else it stores the time.
 *   trade  0x1851fd0 (Windows 0x4079e0): (container rdi, force esi, edx, ecx); the same type test (+0x44 == 3), global
 *          flag and vfunc (+0x23a8, Windows +0x2318); the next update's due time at +0x938 (Windows +0x928).
 *   units  0x17acbf0 (Windows 0x4d8110, Container/Defensible vtable slot 1105; Linux slot 1123: the two vtables
 *          differ at the same slots 18 further on, 1101/1103/1104/1106 against 1119/1121/1122/1124): (container rdi,
 *          filter rsi, flag dl); without both it returns the cached total +0x670 when +0x674 is clear (Windows
 *          +0x680/+0x684).
 *   build  0x185f500 (Windows 0x4280e0): (container rdi, time xmm0, force sil); +0xa88 (Windows +0xa58).
 *   map    0x15286d0 (Windows 0xdcaa00): (behavior rdi, ship rsi); drops the ship's pending event 0x20f and posts one
 *          due now (six callers in the map's routines, as on Windows).
 * Display sites (Windows 0xdd081f, 0xdd1322 in the map's draw routine 0xdcc0d0): the Linux draw routine 0x1538d70
 * brings shown stations' cargo up to now through 0x1850ff0 (Windows 0x3f9760) at 0x153d423, 0x185c130 -> 0x1851ed0
 * (Windows 0x4254a0 -> 0x424390) at 0x153f182 and 0x18515b0 at 0x153d6e1 (cargo, then a storage read); a call with
 * one of their return addresses on the stack is a UI query.
 * A UI query is a call on the UI thread in an admitted frame with a LuaJIT frame (libluajit-5.1.so.2's executable
 * segment, from dl_iterate_phdr) or a display site on its stack (glibc backtrace). The Linux gate replays no UI
 * actions, so there is no replayed-call exception (Windows uirep_replaying), and no factions-mode build owner
 * (Windows fpres_sim_enter). Each entry's first whole instructions (5, 6, 7, 6, 9 bytes; none RIP-relative; each entry
 * 16-byte aligned) become a jmp to an entry stub on a near page; the trampoline runs them and jumps back. Pins: each
 * function's head through the fields it decides on, and the three display calls. Armed before the snapshot load on
 * the UI thread; every gate tick re-checks the jumps and the stubs (stop reason 170, Windows' number); stop restores. */
#include <execinfo.h>
#include <link.h>
#define LXUL_TARGETS 5u
#define LXUL_STOP_REASON 170u
#define LXUL_RING 16384u
#define LXUL_CHAIN 6u
#define LXUL_STUB 0x40u
#define LXUL_KEY_DOMAIN 0x58344c554c4b4559ULL /* "X4LULKEY", Windows' UL_KEY_DOMAIN */
enum {LXUL_OFF=0,LXUL_OBSERVE=1,LXUL_BLOCK=2,LXUL_KEY=3};
enum {LXUL_CARGO=0,LXUL_TRADE=1,LXUL_UNITS=2,LXUL_BUILD=3,LXUL_MAP=4};
static const struct {uint32_t va,stolen;const char *pin,*name;} lxul_targets[LXUL_TARGETS]={
    {0x184dfa0u,5u,"ui_lazy_cargo","cargo"},{0x1851fd0u,6u,"ui_lazy_trade","trade"},{0x17acbf0u,7u,"ui_lazy_units","units"},
    {0x185f500u,6u,"ui_lazy_build","build"},{0x15286d0u,9u,"ui_lazy_map","map"},
};
#define LXUL_CARGO_TIME 0xa48u
#define LXUL_TRADE_DUE 0x938u
#define LXUL_UNITS_TOTAL 0x670u
#define LXUL_UNITS_PENDING 0x674u
#define LXUL_BUILD_TIME 0xa88u
static const uint32_t lxul_display_sites[]={0x153d428u,0x153f187u,0x153d6e6u};
static const char *const lxul_display_pins[]={"ui_lazy_display_cargo","ui_lazy_display_station","ui_lazy_display_amount"};
typedef void (*lxul_cargo_fn)(void *,double,uint64_t);
typedef uint64_t (*lxul_trade_fn)(void *,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*lxul_units_fn)(void *,void *,uint64_t);
typedef void (*lxul_build_fn)(void *,double,uint64_t);
typedef void (*lxul_map_fn)(void *,void *);
typedef struct {volatile int64_t ready;uint64_t tick,key,a1,a2,state;uint32_t chain[LXUL_CHAIN];uint8_t target,klass,blocked,lua,other;} lxul_row_t;
static uint64_t lxul_window_from,lxul_window_to;
static int lxul_mode,lxul_installed,lxul_restored;
static unsigned lxul_done;
static unsigned char *lxul_page; /* per target: trampoline at +0, entry stub at +0x20 */
static lxul_row_t *lxul_ring;
static volatile int64_t lxul_rows,lxul_failures,lxul_other[LXUL_TARGETS];
static uint64_t lxul_lua[LXUL_TARGETS],lxul_sim[LXUL_TARGETS],lxul_blocked,lxul_unmapped,lxul_keyed[LXUL_TARGETS],lxul_unkeyed;
static uint64_t lxul_map_blocked,lxul_map_passed,lxul_display_blocked,lxul_display_passed;
static uint64_t *(*lxul_tls_word_fn)(void); /* tests substitute */
static uint64_t (*lxul_key_fn)(uint64_t,unsigned *)=world_identity_key; /* tests substitute */
static int (*lxul_from_lua_fn)(uint32_t *chain,int *display); /* tests substitute */
static uintptr_t lxul_lua_lo,lxul_lua_hi;
static const char *lxul_stage="idle";
static unsigned char *lxul_trampoline(unsigned i) {return lxul_page+LXUL_STUB*i;}
/* LuaJIT on the stack (frames above this one), the image frames (Linux VAs) into chain, a display site anywhere below
 * the first LuaJIT frame into *display. */
static int lxul_stack(uint32_t *chain,int *display) {
    void *frames[48];const int got=backtrace(frames,48);int lua=0;unsigned k=0;
    const uintptr_t low=(uintptr_t)image,high=low+(LX_TEXT_END_VA-LX_IMAGE_BASE);
    *display=0;
    for(unsigned i=0;i<LXUL_CHAIN;i++) chain[i]=0xffffffffu;
    for(int j=1;j<got;j++) {
        const uintptr_t a=(uintptr_t)frames[j];
        if(a>=lxul_lua_lo && a<lxul_lua_hi) {lua=1;break;}
        if(a<low || a>=high) continue;
        for(unsigned s=0;s<sizeof(lxul_display_sites)/sizeof(*lxul_display_sites);s++) if(a==(uintptr_t)LX(lxul_display_sites[s])) *display=1;
        if(k<LXUL_CHAIN) chain[k++]=(uint32_t)(a-low+LX_IMAGE_BASE);
    }
    return lua;
}
static int lxul_lua_segment(struct dl_phdr_info *info,size_t size,void *data) {
    (void)size;(void)data;
    if(!info->dlpi_name || !strstr(info->dlpi_name,"libluajit")) return 0;
    for(unsigned i=0;i<info->dlpi_phnum;i++) {
        const ElfW(Phdr) *ph=&info->dlpi_phdr[i];
        if(ph->p_type!=PT_LOAD || !(ph->p_flags&PF_X)) continue;
        lxul_lua_lo=(uintptr_t)info->dlpi_addr+ph->p_vaddr;lxul_lua_hi=lxul_lua_lo+ph->p_memsz;return 1;
    }
    return 0;
}
static int lxul_lua_module(void) {
    lxul_lua_lo=lxul_lua_hi=0;
    return dl_iterate_phdr(lxul_lua_segment,NULL) && lxul_lua_hi>lxul_lua_lo;
}
static void lxul_record(unsigned target,const void *container,const uint32_t *chain,int blocked,int lua,uint64_t a1,uint64_t a2,uint64_t state) {
    uint64_t runtime=0,key=0;unsigned klass=WORLD_KEY_NONE;
    if(container && gate_read_local((const unsigned char*)container+8,&runtime,8) && runtime) key=lxul_key_fn(runtime,&klass);
    if(!key && lua) ++lxul_unmapped;
    if(!lxul_ring) return;
    const int64_t index=__atomic_fetch_add(&lxul_rows,1,__ATOMIC_SEQ_CST);lxul_row_t *r=&lxul_ring[(uint64_t)index%LXUL_RING];
    __atomic_store_n(&r->ready,-1,__ATOMIC_SEQ_CST);
    r->tick=stats.issued;r->key=key;memcpy(r->chain,chain,sizeof(r->chain));r->target=(uint8_t)target;r->klass=(uint8_t)klass;
    r->blocked=(uint8_t)blocked;r->lua=(uint8_t)lua;r->a1=a1;r->a2=a2;r->state=state;r->other=GetCurrentThreadId()!=ui_thread;
    __atomic_store_n(&r->ready,index,__ATOMIC_SEQ_CST);
}
/* What decides the call's work (Windows ul_state_of, at the Linux fields). */
static uint64_t lxul_state_of(unsigned target,const void *container) {
    const unsigned char *c=container;uint64_t v=0;uint32_t total=0;uint8_t pending=0;
    if(!c) return 0;
    if(target==LXUL_CARGO) gate_read_local(c+LXUL_CARGO_TIME,&v,8);
    else if(target==LXUL_TRADE) gate_read_local(c+LXUL_TRADE_DUE,&v,8);
    else if(target==LXUL_BUILD) gate_read_local(c+LXUL_BUILD_TIME,&v,8);
    else if(target==LXUL_MAP) v=0;
    else {gate_read_local(c+LXUL_UNITS_TOTAL,&total,4);gate_read_local(c+LXUL_UNITS_PENDING,&pending,1);v=total|((uint64_t)pending<<32);}
    return v;
}
/* Simulation calls by caller (Windows UI_LAZY_CALLERS): (target, the image return address, the one above) -> count. */
#define LXUL_CALLERS 1024u
typedef struct {uint32_t c0,c1,count;uint8_t target,used;} lxul_caller_t;
static lxul_caller_t lxul_callers[LXUL_CALLERS];
static uint64_t lxul_callers_full;
static void lxul_caller_count(unsigned target,const uint32_t *chain) {
    const uint32_t h=(chain[0]*0x9e3779b1u)^(chain[1]*0x85ebca6bu)^target;
    for(unsigned i=0;i<LXUL_CALLERS;i++) {
        lxul_caller_t *e=&lxul_callers[(h+i)&(LXUL_CALLERS-1)];
        if(!e->used) {e->used=1;e->target=(uint8_t)target;e->c0=chain[0];e->c1=chain[1];e->count=1;return;}
        if(e->target==target && e->c0==chain[0] && e->c1==chain[1]) {++e->count;return;}
    }
    ++lxul_callers_full;
}
static int lxul_caller_order(const void *a,const void *b) {
    const lxul_caller_t *p=a,*q=b;
    if(p->target!=q->target) return p->target<q->target?-1:1;
    if(p->c0!=q->c0) return p->c0<q->c0?-1:1;
    return p->c1<q->c1?-1:p->c1>q->c1;
}
static int lxul_callers_text(char *out,size_t cap) {
    static lxul_caller_t sorted[LXUL_CALLERS];unsigned n=0;
    for(unsigned i=0;i<LXUL_CALLERS;i++) if(lxul_callers[i].used) sorted[n++]=lxul_callers[i];
    qsort(sorted,n,sizeof(sorted[0]),lxul_caller_order);
    int w=snprintf(out,cap,"sites=%u full=%llu",n,(unsigned long long)lxul_callers_full);
    for(unsigned i=0;i<n && w>0 && (size_t)w<cap;i++)
        w+=snprintf(out+w,cap-(size_t)w," %c:%x/%x=%u","ctubm"[sorted[i].target<5?sorted[i].target:0],sorted[i].c0,sorted[i].c1,sorted[i].count);
    return w>0 && (size_t)w<cap?w:0;
}
/* 1: a UI query in an admitted frame that 'block' skips; 0: run natively (Windows ul_ui_query). */
static int lxul_ui_query(unsigned target,const void *container,uint64_t a1,uint64_t a2) {
    if(!lxul_mode || !(stats.active && boot_phase==3)) return 0;
    const int window=lxul_window_to && stats.issued>=lxul_window_from && stats.issued<=lxul_window_to;
    int display=0;
    if(GetCurrentThreadId()!=ui_thread) {
        __atomic_add_fetch(&lxul_other[target],1,__ATOMIC_RELAXED);
        if(window) {uint32_t off[LXUL_CHAIN];(lxul_from_lua_fn?lxul_from_lua_fn:lxul_stack)(off,&display);
                    lxul_record(target,container,off,0,0,a1,a2,lxul_state_of(target,container));}
        return 0;
    }
    uint32_t chain[LXUL_CHAIN];const int lua=(lxul_from_lua_fn?lxul_from_lua_fn:lxul_stack)(chain,&display);
    if(!lua && display) {
        const int skip=lxul_mode>=LXUL_BLOCK;
        if(skip) ++lxul_display_blocked;else ++lxul_display_passed;
        if(window) lxul_record(target,container,chain,skip,0,a1,a2,lxul_state_of(target,container));
        return skip;
    }
    if(!lua) {
        ++lxul_sim[target];lxul_caller_count(target,chain);
        if(window) lxul_record(target,container,chain,0,0,a1,a2,lxul_state_of(target,container));
        return 0;
    }
    ++lxul_lua[target];const int block=lxul_mode>=LXUL_BLOCK;if(block) ++lxul_blocked;
    lxul_record(target,container,chain,block,1,a1,a2,lxul_state_of(target,container));
    return block;
}
static int lxul_window(uint64_t from,uint64_t to) {
    if(lxul_installed) return from==lxul_window_from && to==lxul_window_to;
    if(to && (from<1 || to<from || to-from>600)) return 0;
    lxul_window_from=from;lxul_window_to=to;return 1;
}
static uint64_t lxul_filter_class(const void *filter) {
    uintptr_t vt=0;if(!filter) return 0;
    if(!gate_read_local(filter,&vt,8)) return 0xffffffffu;
    return vt>=(uintptr_t)image && vt<(uintptr_t)image+0x8000000u?(uint32_t)(vt-(uintptr_t)image+LX_IMAGE_BASE):0xffffffffu;
}
/* 'key': the TLS word to seed for a call that ran past lxul_ui_query (NULL: run as is); Windows' seeds. */
static uint64_t *lxul_key_word(unsigned target,const void *container,uint64_t bits,uint64_t *seed) {
    if(lxul_mode!=LXUL_KEY || !(stats.active && boot_phase==3) || GetCurrentThreadId()!=ui_thread) return NULL;
    uint64_t runtime=0,key=0;unsigned klass=WORLD_KEY_NONE;
    if(container && gate_read_local((const unsigned char*)container+8,&runtime,8) && runtime) key=lxul_key_fn(runtime,&klass);
    uint64_t *word=key?(lxul_tls_word_fn?lxul_tls_word_fn():gate_tls_rng(gate_tls_self())):NULL;
    if(!word) {++lxul_unkeyed;return NULL;}
    ++lxul_keyed[target];
    *seed=target==LXUL_CARGO?lockstep_seed(LXUL_KEY_DOMAIN,bits,key,0,0,0):
        lockstep_seed(LXUL_KEY_DOMAIN,stats.issued,key,target,lxul_state_of(target,container),0);
    return word;
}
static void lxul_cargo(void *container,double now,uint64_t force) {
    uint64_t bits,seed=0;memcpy(&bits,&now,8);
    if(lxul_ui_query(LXUL_CARGO,container,bits,force&0xff)) return;
    uint64_t *word=lxul_key_word(LXUL_CARGO,container,bits,&seed);
    if(!word) {((lxul_cargo_fn)(void *)lxul_trampoline(LXUL_CARGO))(container,now,force);return;}
    const uint64_t saved=*word;*word=seed;
    ((lxul_cargo_fn)(void *)lxul_trampoline(LXUL_CARGO))(container,now,force);
    *word=saved;
}
static uint64_t lxul_trade(void *container,uint64_t force,uint64_t a3,uint64_t a4) {
    if(lxul_ui_query(LXUL_TRADE,container,force&0xff,a3&0xff)) return 0;
    uint64_t seed=0,*word=lxul_key_word(LXUL_TRADE,container,0,&seed);
    if(!word) return ((lxul_trade_fn)(void *)lxul_trampoline(LXUL_TRADE))(container,force,a3,a4);
    const uint64_t saved=*word;*word=seed;
    const uint64_t result=((lxul_trade_fn)(void *)lxul_trampoline(LXUL_TRADE))(container,force,a3,a4);
    *word=saved;return result;
}
static uint64_t lxul_units(void *container,void *filter,uint64_t flag) {
    if(lxul_ui_query(LXUL_UNITS,container,lxul_filter_class(filter),flag&0xff)) {
        uint32_t total=0;gate_read_local((unsigned char*)container+LXUL_UNITS_TOTAL,&total,4);return total;
    }
    uint64_t seed=0,*word=lxul_key_word(LXUL_UNITS,container,0,&seed);
    if(!word) return ((lxul_units_fn)(void *)lxul_trampoline(LXUL_UNITS))(container,filter,flag);
    const uint64_t saved=*word;*word=seed;
    const uint64_t result=((lxul_units_fn)(void *)lxul_trampoline(LXUL_UNITS))(container,filter,flag);
    *word=saved;return result;
}
static void lxul_build(void *container,double now,uint64_t force) {
    uint64_t bits;memcpy(&bits,&now,8);
    if(lxul_ui_query(LXUL_BUILD,container,bits,force&0xff)) return;
    ((lxul_build_fn)(void *)lxul_trampoline(LXUL_BUILD))(container,now,force);
}
static void lxul_map(void *behavior,void *ship) {
    if(lxul_mode>=LXUL_BLOCK && stats.active && boot_phase==3) {++lxul_map_blocked;return;}
    ++lxul_map_passed;
    ((lxul_map_fn)(void *)lxul_trampoline(LXUL_MAP))(behavior,ship);
}
static void *const lxul_entries[LXUL_TARGETS]={(void*)lxul_cargo,(void*)lxul_trade,(void*)lxul_units,(void*)lxul_build,(void*)lxul_map};
/* Trampoline: the stolen bytes, then jmp [rip] back; entry stub (+0x20): jmp [rip] to the C entry. `at` is where the
 * target's code is (the image; tests pass their own), `entry` the C entry. */
static void lxul_write_code(unsigned char *p,const unsigned char *at,unsigned stolen,const void *entry) {
    static const unsigned char jmp[6]={0xff,0x25,0,0,0,0};
    const void *back=at+stolen;
    memset(p,0xcc,LXUL_STUB);memcpy(p,at,stolen);memcpy(p+stolen,jmp,6);memcpy(p+stolen+6,&back,8);
    memcpy(p+0x20,jmp,6);memcpy(p+0x26,&entry,8);
}
static int lxul_rel(unsigned i,int32_t *out) {
    const intptr_t d=(intptr_t)(lxul_page+LXUL_STUB*i+0x20)-(intptr_t)(LX(lxul_targets[i].va)+5);
    if(d<INT32_MIN || d>INT32_MAX) return 0;
    *out=(int32_t)d;return 1;
}
/* 0 original (its pin), 1 ours, -1 anything else. */
static int lxul_state(unsigned i) {
    const unsigned char *at=LX(lxul_targets[i].va);int32_t want;
    if(at[0]==0xe9 && lxul_page && lxul_rel(i,&want)) {int32_t d;memcpy(&d,at+1,4);return d==want?1:-1;}
    const lx_site *pin=site_named(lxul_targets[i].pin);
    return pin && site_ok(pin)?0:-1;
}
static int lxul_code_ok(unsigned i) {
    unsigned char want[LXUL_STUB];const unsigned char *p=lxul_page+LXUL_STUB*i;const void *back=NULL;
    memcpy(&back,p+lxul_targets[i].stolen+6,8);
    if(back!=LX(lxul_targets[i].va)+lxul_targets[i].stolen) return 0;
    /* the stolen bytes as the trampoline keeps them, the rest as written */
    unsigned char original[16];memcpy(original,p,lxul_targets[i].stolen);
    lxul_write_code(want,LX(lxul_targets[i].va),lxul_targets[i].stolen,lxul_entries[i]);memcpy(want,original,lxul_targets[i].stolen);
    return !memcmp(want,p,LXUL_STUB);
}
static int lxul_line(unsigned i,int ours,unsigned __int128 *line) {
    unsigned char *at=LX(lxul_targets[i].va);int32_t d;
    if((uintptr_t)at&15) return 0;
    memcpy(line,at,16);
    if(ours) {
        if(!lxul_rel(i,&d)) return 0;
        unsigned char *b=(unsigned char*)line;b[0]=0xe9;memcpy(b+1,&d,4);
    } else memcpy(line,lxul_page+LXUL_STUB*i,5);   /* the original first five bytes, kept by the trampoline */
    return 1;
}
static void lxul_restore(void) {
    if(!lxul_installed) return;
    for(unsigned i=0;i<LXUL_TARGETS;i++) {
        unsigned __int128 now,orig;
        if(!((lxul_done>>i)&1u) || lxul_state(i)!=1 || !lxul_line(i,1,&now) || !lxul_line(i,0,&orig)) continue;
        if(gate_code_swap128(LX(lxul_targets[i].va),now,orig)) lxul_done&=~(1u<<i);
    }
    if(!lxul_done) {lxul_installed=0;lxul_restored=1;lxul_mode=LXUL_OFF;lxul_stage="restored";}
}
static int lxul_pins(void) {
    for(unsigned i=0;i<LXUL_TARGETS;i++) {const lx_site *pin=site_named(lxul_targets[i].pin);if(!pin || !site_ok(pin)) return 0;}
    for(unsigned i=0;i<sizeof(lxul_display_pins)/sizeof(*lxul_display_pins);i++) {
        const lx_site *pin=site_named(lxul_display_pins[i]);if(!pin || !site_ok(pin)) return 0;
    }
    return 1;
}
/* Before the snapshot load and the first admitted frame, on the UI thread (Windows ul_arm). */
static int lxul_arm(int mode) {
    if(lxul_installed) return stats.active && lxul_mode==mode;
    if(mode!=LXUL_OBSERVE && mode!=LXUL_BLOCK && mode!=LXUL_KEY) {lxul_stage="mode";return 0;}
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) {lxul_stage="guard";return 0;}
    if(!world_identity_installed) {lxul_stage="needs";return 0;}
    if(!lxul_pins()) {lxul_stage="pins";return 0;}
    for(unsigned i=0;i<LXUL_TARGETS;i++) if(lxul_state(i)!=0) {lxul_stage="pins";return 0;}
    if(!lxul_from_lua_fn && !lxul_lua_module()) {lxul_stage="lua";return 0;}
    {void *first[2];backtrace(first,2);}   /* the first backtrace loads libgcc_s: here, not on some X4 thread */
    if(!lxul_ring) {void *ring=mmap(NULL,sizeof(lxul_row_t)*LXUL_RING,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
                    lxul_ring=ring==MAP_FAILED?NULL:ring;}
    if(!lxul_page) lxul_page=gate_near_page(LX(lxul_targets[LXUL_CARGO].va));
    if(!lxul_ring || !lxul_page || mprotect(lxul_page,GATE_PAGE,PROT_READ|PROT_WRITE)) {lxul_stage="page";return 0;}
    for(unsigned i=0;i<LXUL_TARGETS;i++) {
        int32_t d;
        if(!lxul_rel(i,&d)) {lxul_stage="page";return 0;}
        lxul_write_code(lxul_page+LXUL_STUB*i,LX(lxul_targets[i].va),lxul_targets[i].stolen,lxul_entries[i]);
    }
    if(mprotect(lxul_page,GATE_PAGE,PROT_READ|PROT_EXEC)) {lxul_stage="page";return 0;}
    memset(lxul_lua,0,sizeof(lxul_lua));memset(lxul_sim,0,sizeof(lxul_sim));memset(lxul_callers,0,sizeof(lxul_callers));lxul_callers_full=0;
    for(unsigned i=0;i<LXUL_TARGETS;i++) lxul_other[i]=0;
    lxul_blocked=lxul_unmapped=lxul_unkeyed=0;memset(lxul_keyed,0,sizeof(lxul_keyed));lxul_rows=0;lxul_failures=0;lxul_restored=0;
    lxul_map_blocked=lxul_map_passed=lxul_display_blocked=lxul_display_passed=0;
    lxul_mode=mode;lxul_installed=1;
    for(unsigned i=0;i<LXUL_TARGETS;i++) {
        unsigned __int128 orig,ours;
        if(!lxul_line(i,0,&orig) || !lxul_line(i,1,&ours) || !gate_code_swap128(LX(lxul_targets[i].va),orig,ours)) {
            lxul_stage="cas";lxul_restore();lxul_installed=0;lxul_mode=LXUL_OFF;return 0;
        }
        lxul_done|=1u<<i;
    }
    lxul_stage="armed";return 1;
}
static int lxul_owned(void) {
    for(unsigned i=0;i<LXUL_TARGETS;i++) if((lxul_done>>i)&1u && (lxul_state(i)!=1 || !lxul_code_ok(i))) return 0;
    return 1;
}
/* The tick check (stop 170 on a failure). */
static int lxul_check(void) {
    if(!lxul_installed) return 1;
    if(__atomic_load_n(&lxul_failures,__ATOMIC_SEQ_CST)) return 0;
    if(!lxul_owned()) {lxul_stage="site_changed";__atomic_add_fetch(&lxul_failures,1,__ATOMIC_SEQ_CST);return 0;}
    return 1;
}
static int lxul_row(uint64_t index,char *out,size_t cap) {
    const int64_t count=__atomic_load_n(&lxul_rows,__ATOMIC_SEQ_CST);int n;
    if(!lxul_ring || (int64_t)index>=count) return 0;
    if(count>(int64_t)LXUL_RING && index<(uint64_t)(count-(int64_t)LXUL_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const lxul_row_t *r=&lxul_ring[index%LXUL_RING];
    if(__atomic_load_n(&r->ready,__ATOMIC_SEQ_CST)!=(int64_t)index) return 0;
    static const char *const classes[3]={"saved","created","none"};
    n=snprintf(out,cap,"index=%llu tick=%llu target=%s class=%s key=%016llx lua=%u blocked=%u a1=%llx a2=%llx state=%016llx chain=%08x,%08x,%08x,%08x,%08x,%08x other=%u",
        (unsigned long long)index,(unsigned long long)r->tick,lxul_targets[r->target<LXUL_TARGETS?r->target:0].name,classes[r->klass<3?r->klass:2],
        (unsigned long long)r->key,(unsigned)r->lua,(unsigned)r->blocked,(unsigned long long)r->a1,(unsigned long long)r->a2,(unsigned long long)r->state,
        r->chain[0],r->chain[1],r->chain[2],r->chain[3],r->chain[4],r->chain[5],(unsigned)r->other);
    return n>0 && (size_t)n<cap?n:0;
}
static int lxul_status(char *out,size_t cap) {
    static const char *const modes[4]={"off","observe","block","key"};
    const int n=snprintf(out,cap,"installed=%d mode=%s frame=%llu lua=%llu/%llu/%llu sim=%llu/%llu/%llu other=%lld/%lld/%lld blocked=%llu unmapped=%llu rows=%lld window=%llu-%llu restored=%d stage=%s failures=%lld keyed=%llu/%llu/%llu unkeyed=%llu build=%llu/%llu/%lld map=%llu/%llu display=%llu/%llu",
        lxul_installed,modes[lxul_mode>=0 && lxul_mode<4?lxul_mode:0],(unsigned long long)stats.issued,(unsigned long long)lxul_lua[0],(unsigned long long)lxul_lua[1],
        (unsigned long long)lxul_lua[2],(unsigned long long)lxul_sim[0],(unsigned long long)lxul_sim[1],(unsigned long long)lxul_sim[2],(long long)lxul_other[0],(long long)lxul_other[1],(long long)lxul_other[2],
        (unsigned long long)lxul_blocked,(unsigned long long)lxul_unmapped,(long long)lxul_rows,(unsigned long long)lxul_window_from,(unsigned long long)lxul_window_to,lxul_restored,lxul_stage,
        (long long)lxul_failures,(unsigned long long)lxul_keyed[0],(unsigned long long)lxul_keyed[1],(unsigned long long)lxul_keyed[2],(unsigned long long)lxul_unkeyed,
        (unsigned long long)lxul_lua[LXUL_BUILD],(unsigned long long)lxul_sim[LXUL_BUILD],(long long)lxul_other[LXUL_BUILD],
        (unsigned long long)lxul_map_blocked,(unsigned long long)lxul_map_passed,(unsigned long long)lxul_display_blocked,(unsigned long long)lxul_display_passed);
    return n>0 && (size_t)n<cap?n:0;
}
static int lua_ui_lazy(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    const int m=mode && length==5 && !memcmp(mode,"block",5)?LXUL_BLOCK:mode && length==3 && !memcmp(mode,"key",3)?LXUL_KEY:
        mode && length==7 && !memcmp(mode,"observe",7)?LXUL_OBSERVE:-1;
    const double from=p_lua_tonumber(L,2),to=p_lua_tonumber(L,3);int window=1;
    if(isfinite(from) && isfinite(to) && to>0) window=from>=1 && to>=from && to<9007199254740992.0 && floor(from)==from && floor(to)==to &&
        lxul_window((uint64_t)from,(uint64_t)to);
    p_lua_pushnumber(L,accept_ui() && m>0 && window && lxul_arm(m));return 1;
}
static int lua_ui_lazy_callers(lua_State *L) {
    static char out[65536];const int n=accept_ui()?lxul_callers_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ui_lazy_status(lua_State *L) {
    char out[384];const int n=accept_ui()?lxul_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ui_lazy_row(lua_State *L) {
    const double index=p_lua_tonumber(L,1);char out[288];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=lxul_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}

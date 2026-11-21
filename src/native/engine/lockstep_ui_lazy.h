/* Determinism fix, DEFAULT OFF: Lua UI queries do not bring containers up to
 * date (native_gate_ui_lazy = 'observe' | 'block', needs the world identities).
 *
 * The 3600 transporter class now starts with one keyed due event at tick 1078:
 * station 340cf's SupplyWaresAddedEvent listener (0x42b900, wanted supply
 * orders 0x429ad0, one Random::U64 draw) and the supply changes of stations
 * 1f2df and 2bb56 that a script's trade offer query brings up at 1077
 * (DoIf -> PropertyLookup -> 0x40a3f0 -> Container::UpdateTradeOffers 0x4079e0
 * -> supply-added post 0x428970) happen on both clients, on neither or on one
 * (full-G9 both, full-G10s neither, full-G8/G10 one): the stations' cargo,
 * unit and trade offer state already differs before 1077, with the same
 * scripts. That state is evaluated lazily, at whatever game time something
 * asks: cargo and production by 0x41a910 (container, time, force), units in
 * flight by Defensible slot 1105 0x4d8110 (applies arrived units and posts
 * their supply changes), trade offers by 0x4079e0. Besides the simulation, the
 * Lua UI asks through GetComponentData property lookups and the Lua storage
 * functions (GetStorageData, GetUnitStorageData from the target monitor, the
 * map, the station overview) on what each client shows, with refreshes paced
 * by real time; native_gate_ui_cargo covers only ten explicit exports and saw
 * none of them.
 *
 * The three functions get an entry detour (E9 to a stub on a private page;
 * the overwritten whole instructions, 7, 7 and 5 bytes, run in a trampoline
 * that jumps back). In an admitted frame on the MoveWorker, a call with the
 * Lua runtime (lua51_64.dll) on the stack is a UI query: 'observe' counts it
 * per function, keeps one UI_LAZY_ROW (tick, function, container portable key,
 * six image return addresses) and runs natively; 'block' also returns without
 * the update (units: the container's cached unit total +0x680; the UI shows
 * the state the simulation last brought it to). Every other call runs
 * natively. With a row window (native_gate_ui_lazy_rows = {from, to}, at
 * most 600 ticks) every call of an admitted frame in it is a row, simulation
 * calls too (lua=0), with the arguments and the state that decide the work:
 * cargo a1 = game time bits, a2 = force, state = +0xa18 (the time it was last
 * brought to); trade a1 = force, a2 = third argument, state = +0x928 (next
 * update time); units a1 = the filter's class vtable RVA (0: none, ffffffff:
 * not in the image), a2 = flag, state = +0x680 cached total | +0x684 << 32.
 * Calls of any other thread run natively: counted per function (other= in the
 * status) and, in the window, a row with other=1 (the gate's frames do not bound
 * these rows: main-thread calls between two admitted frames are rows too, with
 * the last issued tick).
 * 'key' is 'block' plus keyed cargo: a simulation call of 0x41a910 in an
 * admitted frame on the MoveWorker draws its production-cycle RNG from the
 * calling thread's TLS word (slot0+0x318), so whichever actor first asks
 * decides the station's stream, and the draws move that actor's own stream.
 * With 'key' the word is set to lockstep_seed(UL_KEY_DOMAIN, game time bits,
 * container portable key) for the call and restored after it: bringing a
 * station to a time gives the same result whoever triggers it. Trade offers
 * (0x4079e0, the next update's due time) and units draw the same way and have
 * no time argument: their seed is (tick, portable key, target, the state above).
 * keyed=cargo/trade/units, unkeyed= (no portable key or no TLS word: native).
 * Pins: each function's first bytes. Written by CAS at arm (before
 * the snapshot load), re-checked every gate tick (stop reason 170), restored at
 * stop.
 *
 * Fourth target, 'build' (17 Nov 2026): a container's builds are brought up to a game time the same lazy way, by
 * 0x4280e0(container, time, force): without force it returns when +0xa58 (the time it was last brought to) is not
 * older, else it stores the time and works through the container's build tasks (a station's construction, a shipyard's
 * ships): what can start starts, building material is taken from the storage. The simulation asks at its own moments;
 * the station menu asks every frame through GetCurrentBuildProgress (0x1e4580 -> BuildProcessor 0x38c100(update) ->
 * 0x38dd40 -> here). Run sb2 (two clients, each player building a station by real clicks): the client whose menu showed
 * the station took every 60 s portion of the material one portion earlier than the other and finished the module 1,800
 * ticks earlier; the first digest part to differ was the runtime IDs, 4,600 ticks after the builder's order. A UI query
 * is blocked as the other three are (the menu shows the progress the simulation last brought the station to); a
 * replayed call, which every client runs at the same tick, runs. Not keyed: simulation calls run as they are.
 *
 * Fifth target, 'map' (17 Nov 2026): the open map refreshes the ships it shows. U::UniverseMapBehavior (vfunc144
 * 0xdc3a00 and 0xdc8dd0, six call sites) calls 0xdcaa00(behavior, ship) for every shown object whose attention is
 * below 7 (not next to a player): it removes the ship's pending U::UpdateEngineParametersEvent (type 0x20f) and posts
 * one due now. The NavigationController's handler 0xea1d70 then copies the ship's and its engine's parameters (speed,
 * boost, travel mode) into the written flight slot at once and re-posts its own periodic event with a draw from the
 * thread's random word. Without a map that refresh comes every 1 to 2 s (lockstep_nav_cadence.h). Run play5 (two
 * clients; player 2 opened the map, nothing else): on that client four moving ships of the watched zone were
 * refreshed every tick from the map's first frame (1,138 times against 18 on the other client), one took its travel
 * mode flag at tick 3660 where the other client's copy waited for the periodic refresh, and the world check 540 ticks
 * later had two ships at other places. The same in every earlier run where a second player opened the map while a
 * ship flew near a station under construction, and in another session's runs with a map left open on one client.
 * The routine has no caller but the map, so in a session it does nothing: the map shows the parameters the
 * simulation last refreshed. Status map=<blocked>/<passed>. */
#define UL_TARGETS 5u
#define UL_STOP_REASON 170u /* 155 stock piloting, 160-164 virtual players, 165 Jolt, 166-169 camera */
#define UL_RING 16384u
#define UL_CHAIN 6u
#define UL_STUB 0x40u
enum {UL_OFF=0,UL_OBSERVE=1,UL_BLOCK=2,UL_KEY=3};
#define UL_KEY_DOMAIN 0x58344c554c4b4559ULL /* "X4LULKEY" */
enum {UL_CARGO=0,UL_TRADE=1,UL_UNITS=2,UL_BUILD=3,UL_MAP=4};
static const struct {uint32_t rva,stolen,pin_size;const char *sha,*name;} ul_targets[UL_TARGETS]={
    {0x41a910u,7u,0x70u,"5b94da94f107e9c097551381a91e1ff9868ace79480c4cbaf7958292f77f666a","cargo"},
    {0x4079e0u,7u,0x60u,"ed89bcfb65a436f4e90ab33fea2493352ab953f8c6df8ec3d3fa6c07815b45e7","trade"},
    {0x4d8110u,5u,0x40u,"43aa700d29d4015a81690be9b8847b50f6db872c42dd133e7029fc69430027b4","units"},
    {0x4280e0u,7u,0x60u,"28158cae28c0e6a97f4b43565063d3cd89b8825a3ffa6444ea0f9f59bba6fbcd","build"},
    {0xdcaa00u,5u,0x8au,"08d888bafa3fa417939add8ff0ad7c4611cba9f19590235c1bbb61ba648fb58c","map"},
};
typedef void (*ul_cargo_fn)(void *,double,uint64_t);
typedef uint64_t (*ul_trade_fn)(void *,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*ul_units_fn)(void *,void *,uint64_t);
typedef void (*ul_build_fn)(void *,double,uint64_t);
typedef void (*ul_map_fn)(void *,void *);
static uint64_t ul_map_blocked,ul_map_passed;
typedef struct {volatile LONG64 ready;uint64_t tick,key,a1,a2,state;uint32_t chain[UL_CHAIN];uint8_t target,klass,blocked,lua,other;} ul_row_t;
static uint64_t ul_window_from,ul_window_to; /* every call of these ticks is a row; 0/0: UI queries only */
static int ul_mode,ul_installed,ul_restored;
static unsigned ul_done;
static unsigned char *ul_page; /* per target: trampoline at +0, entry stub at +0x20 */
static ul_row_t *ul_ring;
static volatile LONG64 ul_rows,ul_failures,ul_other[UL_TARGETS];
static uint64_t ul_lua[UL_TARGETS],ul_sim[UL_TARGETS],ul_blocked,ul_unmapped,ul_keyed[UL_TARGETS],ul_unkeyed;
static uint64_t ul_replayed;   /* forced updates of replayed calls: run, not blocked */
static uint64_t *(*ul_tls_word_fn)(void); /* tests substitute */
static uintptr_t ul_lua_base,ul_lua_end;
static const char *ul_stage="idle";
static int (*ul_from_lua_fn)(uint32_t *chain); /* tests substitute */
static uint32_t ul_rva(const void *p) {const unsigned char *q=p;return q>=image && q<image+0x6fa9000?(uint32_t)(q-image):0xffffffffu;}
/* The Lua runtime somewhere above us; the image frames go to chain. */
static int ul_stack_from_lua(uint32_t *chain) {
    void *frames[48];USHORT n=RtlCaptureStackBackTrace(1,48,frames,NULL);int lua=0;unsigned k=0;
    for(unsigned i=0;i<UL_CHAIN;i++) chain[i]=0xffffffffu;
    for(USHORT j=0;j<n;j++) {
        uintptr_t a=(uintptr_t)frames[j];
        if(a>=ul_lua_base && a<ul_lua_end) {lua=1;break;}
        uint32_t rva=ul_rva(frames[j]);if(rva!=0xffffffffu && k<UL_CHAIN) chain[k++]=rva;
    }
    return lua;
}
static unsigned char *ul_trampoline(unsigned i) {return ul_page+UL_STUB*i;}
static void ul_record(unsigned target,const void *container,const uint32_t *chain,int blocked,int lua,uint64_t a1,uint64_t a2,uint64_t state) {
    uint64_t runtime=0,key=0;unsigned klass=WORLD_KEY_NONE;
    if(container && read_local((const unsigned char*)container+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
    if(!key && lua) ++ul_unmapped;
    if(!ul_ring) return;
    LONG64 index=InterlockedIncrement64(&ul_rows)-1;ul_row_t *r=&ul_ring[(uint64_t)index%UL_RING];
    InterlockedExchange64(&r->ready,-1);
    r->tick=stats.issued;r->key=key;memcpy(r->chain,chain,sizeof(r->chain));r->target=(uint8_t)target;r->klass=(uint8_t)klass;r->blocked=(uint8_t)blocked;
    r->lua=(uint8_t)lua;r->a1=a1;r->a2=a2;r->state=state;r->other=GetCurrentThreadId()!=ui_thread;
    InterlockedExchange64(&r->ready,index);
}
/* What decides the call's work (see the header). */
static uint64_t ul_state_of(unsigned target,const void *container) {
    const unsigned char *c=container;uint64_t v=0;uint32_t total=0;uint8_t pending=0;
    if(!c) return 0;
    if(target==UL_CARGO) read_local(c+0xa18,&v,8);
    else if(target==UL_TRADE) read_local(c+0x928,&v,8);
    else if(target==UL_BUILD) read_local(c+0xa58,&v,8);
    else if(target==UL_MAP) v=0;
    else {read_local(c+0x680,&total,4);read_local(c+0x684,&pending,1);v=total|((uint64_t)pending<<32);}
    return v;
}
/* Simulation calls by caller (always on while armed): (target, the image return address of the call, the
 * one above it) -> count, so that a run whose clients part on a lazy update names the call site without a
 * row window (the timing-dependent cases do not reproduce at the same tick). MoveWorker only: no lock. */
#define UL_CALLERS 1024u
typedef struct {uint32_t c0,c1,count;uint8_t target,used;} ul_caller_t;
static ul_caller_t ul_callers[UL_CALLERS];
static uint64_t ul_callers_full;
static void ul_caller_count(unsigned target,const uint32_t *chain) {
    uint32_t h=(chain[0]*0x9e3779b1u)^(chain[1]*0x85ebca6bu)^target;
    for(unsigned i=0;i<UL_CALLERS;i++) {
        ul_caller_t *e=&ul_callers[(h+i)&(UL_CALLERS-1)];
        if(!e->used) {e->used=1;e->target=(uint8_t)target;e->c0=chain[0];e->c1=chain[1];e->count=1;return;}
        if(e->target==target && e->c0==chain[0] && e->c1==chain[1]) {++e->count;return;}
    }
    ++ul_callers_full;
}
static int ul_caller_order(const void *a,const void *b) {
    const ul_caller_t *p=a,*q=b;
    if(p->target!=q->target) return p->target<q->target?-1:1;
    if(p->c0!=q->c0) return p->c0<q->c0?-1:1;
    return p->c1<q->c1?-1:p->c1>q->c1;
}
/* UI_LAZY_CALLERS: one token per call site in a fixed order, 'c|t|u:caller/above=count'. */
static int ul_callers_text(char *out,size_t cap) {
    static ul_caller_t sorted[UL_CALLERS];unsigned n=0;
    for(unsigned i=0;i<UL_CALLERS;i++) if(ul_callers[i].used) sorted[n++]=ul_callers[i];
    qsort(sorted,n,sizeof(sorted[0]),ul_caller_order);
    int w=snprintf(out,cap,"sites=%u full=%llu",n,(unsigned long long)ul_callers_full);
    for(unsigned i=0;i<n && w>0 && (size_t)w<cap;i++)
        w+=snprintf(out+w,cap-(size_t)w," %c:%x/%x=%u","ctubm"[sorted[i].target<5?sorted[i].target:0],sorted[i].c0,sorted[i].c1,sorted[i].count);
    return w>0 && (size_t)w<cap?w:0;
}
/* Display code of X4 itself that asks like the Lua UI does, with no Lua on the stack (18 Nov 2026): the open map
 * draws the storage of the stations it shows and asks 0x3f9760 for it (call at 0xdd081a in the map's draw routine
 * 0xdcc0d0; the routine's only other caller is a Lua function), which brings the station's cargo and production up
 * to now (0x3f9799: 0x41a910(container, game time, 0)). Run mg4 (two clients, player 1's map open on its two solar
 * power plants, player 2 in the cockpit): 6,242 such calls on client 1 in 11,520 ticks and none on client 2, each
 * with the calls an update makes in its turn; client 1's station held a production cycle more than client 2's for
 * hundreds of ticks (1050 against 700 energy cells), the two clients' counts of trade offer updates and of unit
 * updates had parted (5252/5254, 15695/15697), and no digest part holds a station's cargo. A call with one of these
 * return addresses in its chain is a UI query: 'block' and 'key' skip it (the map shows the storage the simulation
 * last brought the station to, as the Lua menus do), 'observe' counts it. display=<skipped>/<run>.
 * The same draw routine asks 0x4254a0 for the station at 0xdd131d (-> 0x424390 -> 0x41a910 at 0x424424): run mg5,
 * with the first site skipped, still had 8,419 cargo updates on the map's client alone, all of this chain. */
static const uint32_t ul_display_sites[]={0xdd081fu,0xdd1322u};
static uint64_t ul_display_blocked,ul_display_passed;
static int ul_from_display(const uint32_t *chain) {
    for(unsigned i=0;i<UL_CHAIN;i++)
        for(unsigned s=0;s<sizeof(ul_display_sites)/sizeof(*ul_display_sites);s++) if(chain[i]==ul_display_sites[s]) return 1;
    return 0;
}
/* 1: a UI query in an admitted frame that 'block' skips; 0: run natively. */
static int ul_ui_query(unsigned target,const void *container,uint64_t a1,uint64_t a2) {
    if(!ul_mode || !(stats.active && boot_phase==3)) return 0;
    int window=ul_window_to && stats.issued>=ul_window_from && stats.issued<=ul_window_to;
    if(GetCurrentThreadId()!=ui_thread) {
        InterlockedIncrement64(&ul_other[target]);
        if(window) {uint32_t off[UL_CHAIN];(ul_from_lua_fn?ul_from_lua_fn:ul_stack_from_lua)(off);ul_record(target,container,off,0,0,a1,a2,ul_state_of(target,container));}
        return 0;
    }
    uint32_t chain[UL_CHAIN];int lua=(ul_from_lua_fn?ul_from_lua_fn:ul_stack_from_lua)(chain);
    if(!lua && ul_from_display(chain)) {
        int skip=ul_mode>=UL_BLOCK;
        if(skip) ++ul_display_blocked; else ++ul_display_passed;
        if(window) ul_record(target,container,chain,skip,0,a1,a2,ul_state_of(target,container));
        return skip;
    }
    if(!lua) {
        ++ul_sim[target];ul_caller_count(target,chain);
        if(window) ul_record(target,container,chain,0,0,a1,a2,ul_state_of(target,container));
        return 0;
    }
    /* A replayed call (lockstep_ui_replay.h: every client runs it at the same tick, in the same order) that FORCES the
     * update is the simulation's own step, not a query of one client's UI: X4's StartResearch ends with
     * 0x41a910(station, now, 1), which starts the queued item. Blocked, a research module that nothing else brings up
     * never starts (hq7, 16 Nov: the headquarters of slot 2's faction kept its queue for good; X4's own player's started
     * only because a story script asked that station for something). It runs keyed like a simulation call. A replayed
     * call that reaches a container's builds (a plan commit, a cancelled construction, a ship order) is such a step too,
     * forced or not. */
    if(((target==UL_CARGO && (a2&0xff)) || target==UL_BUILD) && InterlockedCompareExchange(&uirep_replaying,0,0)) {
        ++ul_replayed;
        if(window) ul_record(target,container,chain,0,1,a1,a2,ul_state_of(target,container));
        return 0;
    }
    ++ul_lua[target];int block=ul_mode>=UL_BLOCK;if(block) ++ul_blocked;
    ul_record(target,container,chain,block,1,a1,a2,ul_state_of(target,container));
    return block;
}
/* The row window; a configuration rerun with the same window is accepted. Before arming. */
static int ul_window(uint64_t from,uint64_t to) {
    if(ul_installed) return from==ul_window_from && to==ul_window_to;
    if(to && (from<1 || to<from || to-from>600)) return 0;
    ul_window_from=from;ul_window_to=to;return 1;
}
static uint64_t ul_filter_class(const void *filter) {
    void *vt=NULL;if(!filter) return 0;
    if(!read_local(filter,&vt,8)) return 0xffffffffu;
    return ul_rva(vt);
}
/* 'key': the TLS word to seed for a call that ran past ul_ui_query (NULL: run as is). */
static uint64_t *ul_key_word(unsigned target,const void *container,uint64_t bits,uint64_t *seed) {
    if(ul_mode!=UL_KEY || !(stats.active && boot_phase==3) || GetCurrentThreadId()!=ui_thread) return NULL;
    uint64_t runtime=0,key=0;unsigned klass=WORLD_KEY_NONE;
    if(container && read_local((const unsigned char*)container+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
    uint64_t *word=key?(ul_tls_word_fn?ul_tls_word_fn:actor_all_tls_rng)():NULL;
    if(!word) {++ul_unkeyed;return NULL;}
    ++ul_keyed[target];
    *seed=target==UL_CARGO?lockstep_seed(UL_KEY_DOMAIN,bits,key,0,0,0):lockstep_seed(UL_KEY_DOMAIN,stats.issued,key,target,ul_state_of(target,container),0);
    return word;
}
static void ul_cargo(void *container,double now,uint64_t force) {
    uint64_t bits,seed=0;memcpy(&bits,&now,8);
    if(ul_ui_query(UL_CARGO,container,bits,force&0xff)) return;
    uint64_t *word=ul_key_word(UL_CARGO,container,bits,&seed);
    if(!word) {((ul_cargo_fn)(void *)ul_trampoline(UL_CARGO))(container,now,force);return;}
    uint64_t saved=*word;*word=seed;
    __try {((ul_cargo_fn)(void *)ul_trampoline(UL_CARGO))(container,now,force);}
    __finally {*word=saved;}
}
static uint64_t ul_trade(void *container,uint64_t force,uint64_t a3,uint64_t a4) {
    if(ul_ui_query(UL_TRADE,container,force&0xff,a3&0xff)) return 0;
    uint64_t seed=0,*word=ul_key_word(UL_TRADE,container,0,&seed);
    if(!word) return ((ul_trade_fn)(void *)ul_trampoline(UL_TRADE))(container,force,a3,a4);
    uint64_t saved=*word,result=0;*word=seed;
    __try {result=((ul_trade_fn)(void *)ul_trampoline(UL_TRADE))(container,force,a3,a4);}
    __finally {*word=saved;}
    return result;
}
static uint64_t ul_units(void *container,void *filter,uint64_t flag) {
    if(ul_ui_query(UL_UNITS,container,ul_filter_class(filter),flag&0xff)) {uint32_t total=0;read_local((unsigned char*)container+0x680,&total,4);return total;}
    uint64_t seed=0,*word=ul_key_word(UL_UNITS,container,0,&seed);
    if(!word) return ((ul_units_fn)(void *)ul_trampoline(UL_UNITS))(container,filter,flag);
    uint64_t saved=*word,result=0;*word=seed;
    __try {result=((ul_units_fn)(void *)ul_trampoline(UL_UNITS))(container,filter,flag);}
    __finally {*word=saved;}
    return result;
}
static void ul_build(void *container,double now,uint64_t force) {
    uint64_t bits;memcpy(&bits,&now,8);
    if(ul_ui_query(UL_BUILD,container,bits,force&0xff)) return;
    /* a container of a player's own faction (factions mode): built as that player's (lockstep_faction_present.h) */
    const int own=fpres_sim_enter(container);
    __try {((ul_build_fn)(void *)ul_trampoline(UL_BUILD))(container,now,force);}
    __finally {if(own) fpres_sim_leave();}
}
/* The map's refresh of a shown ship: nothing in a session ('block' and 'key'); 'observe' counts and runs it. */
static void ul_map(void *behavior,void *ship) {
    if(ul_mode>=UL_BLOCK && stats.active && boot_phase==3) {++ul_map_blocked;return;}
    ++ul_map_passed;
    ((ul_map_fn)(void *)ul_trampoline(UL_MAP))(behavior,ship);
}
static void *const ul_entries[UL_TARGETS]={(void*)ul_cargo,(void*)ul_trade,(void*)ul_units,(void*)ul_build,(void*)ul_map};
/* Trampoline: the stolen bytes, then jmp [rip] back; entry stub (+0x20): jmp [rip] to the C entry. */
static void ul_write_code(unsigned char *p,unsigned i,const unsigned char *stolen) {
    static const unsigned char jmp[6]={0xff,0x25,0,0,0,0};
    memset(p,0xcc,UL_STUB);memcpy(p,stolen,ul_targets[i].stolen);
    void *back=image+ul_targets[i].rva+ul_targets[i].stolen;memcpy(p+ul_targets[i].stolen,jmp,6);memcpy(p+ul_targets[i].stolen+6,&back,8);
    memcpy(p+0x20,jmp,6);memcpy(p+0x26,&ul_entries[i],8);
}
static int32_t ul_rel(unsigned i) {return (int32_t)((intptr_t)(ul_page+UL_STUB*i+0x20)-(intptr_t)(image+ul_targets[i].rva+5));}
/* 0 original (pins), 1 ours, -1 anything else. */
static int ul_state(unsigned i) {
    unsigned char now[5];if(!read_local(image+ul_targets[i].rva,now,5)) return -1;
    if(now[0]==0xe9 && ul_page) {int32_t d;memcpy(&d,now+1,4);return d==ul_rel(i)?1:-1;}
    return hash_ok(image+ul_targets[i].rva,ul_targets[i].pin_size,ul_targets[i].sha)?0:-1;
}
static int ul_code_ok(unsigned i) {
    unsigned char want[UL_STUB],stolen[8];memcpy(stolen,ul_page+UL_STUB*i,ul_targets[i].stolen);
    ul_write_code(want,i,stolen);return !memcmp(want,ul_page+UL_STUB*i,UL_STUB);
}
static unsigned char *ul_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+0x420000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int ok=1;
            for(unsigned i=0;i<UL_TARGETS && ok;i++) {intptr_t d=(intptr_t)p-(intptr_t)(image+ul_targets[i].rva+5);ok=d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000;}
            if(ok) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void ul_restore(void) {
    if(!ul_installed) return;
    for(unsigned i=0;i<UL_TARGETS;i++) {
        if(!((ul_done>>i)&1u) || ul_state(i)!=1) continue;
        unsigned char now[5],orig[5];read_local(image+ul_targets[i].rva,now,5);memcpy(orig,ul_page+UL_STUB*i,5);
        if(view_cas_bytes(ul_targets[i].rva,now,orig,5)) ul_done&=~(1u<<i);
    }
    if(!ul_done) {ul_installed=0;ul_restored=1;ul_mode=UL_OFF;}
}
static int ul_lua_module(void) {
    HMODULE lua=GetModuleHandleW(L"lua51_64.dll");if(!lua) return 0;
    IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    if(!read_local(lua,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE || !read_local((unsigned char*)lua+dos.e_lfanew,&nt,sizeof(nt))) return 0;
    ul_lua_base=(uintptr_t)lua;ul_lua_end=ul_lua_base+nt.OptionalHeader.SizeOfImage;return 1;
}
/* Before the snapshot load and the first admitted frame, on the MoveWorker. */
static int ul_arm(int mode) {
    if(ul_installed) return stats.active && ul_mode==mode;
    if(mode!=UL_OBSERVE && mode!=UL_BLOCK && mode!=UL_KEY) {ul_stage="mode";return 0;}
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed) {ul_stage="needs";return 0;}
    for(unsigned i=0;i<UL_TARGETS;i++) if(ul_state(i)!=0) {ul_stage="pins";return 0;}
    if(!ul_from_lua_fn && !ul_lua_module()) {ul_stage="lua";return 0;}
    if(!ul_ring) ul_ring=VirtualAlloc(NULL,sizeof(ul_row_t)*UL_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ul_page) ul_page=ul_alloc_page();
    if(!ul_ring || !ul_page) {ul_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(ul_page,0x1000,PAGE_READWRITE,&old)) {ul_stage="page";return 0;}
    for(unsigned i=0;i<UL_TARGETS;i++) ul_write_code(ul_page+UL_STUB*i,i,image+ul_targets[i].rva);
    if(!VirtualProtect(ul_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {ul_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),ul_page,0x1000);
    memset(ul_lua,0,sizeof(ul_lua));memset(ul_sim,0,sizeof(ul_sim));memset(ul_callers,0,sizeof(ul_callers));ul_callers_full=0;for(unsigned i=0;i<UL_TARGETS;i++) ul_other[i]=0;ul_blocked=ul_unmapped=ul_unkeyed=0;memset(ul_keyed,0,sizeof(ul_keyed));ul_rows=0;ul_failures=0;ul_restored=0;ul_map_blocked=ul_map_passed=0;ul_display_blocked=ul_display_passed=0;
    ul_mode=mode;ul_installed=1;
    for(unsigned i=0;i<UL_TARGETS;i++) {
        unsigned char orig[5],patch[5]={0xe9};int32_t d=ul_rel(i);memcpy(orig,image+ul_targets[i].rva,5);memcpy(patch+1,&d,4);
        if(!view_cas_bytes(ul_targets[i].rva,orig,patch,5)) {ul_stage="cas";ul_restore();ul_installed=0;ul_mode=UL_OFF;return 0;}
        ul_done|=1u<<i;
    }
    ul_stage="armed";return 1;
}
static int ul_owned(void) {
    for(unsigned i=0;i<UL_TARGETS;i++) if((ul_done>>i)&1u && (ul_state(i)!=1 || !ul_code_ok(i))) return 0;
    return 1;
}
static int ul_check(void) {
    if(InterlockedCompareExchange64(&ul_failures,0,0)) return 0;
    if(!ul_owned()) {ul_stage="site_changed";InterlockedIncrement64(&ul_failures);return 0;}
    return 1;
}
static int ul_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&ul_rows,0,0);int n;
    if(!ul_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)UL_RING && index<(uint64_t)(count-(LONG64)UL_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const ul_row_t *r=&ul_ring[index%UL_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index) return 0;
    static const char *const classes[3]={"saved","created","none"};
    n=snprintf(out,cap,"index=%llu tick=%llu target=%s class=%s key=%016llx lua=%u blocked=%u a1=%llx a2=%llx state=%016llx chain=%08x,%08x,%08x,%08x,%08x,%08x other=%u",
        (unsigned long long)index,(unsigned long long)r->tick,ul_targets[r->target<UL_TARGETS?r->target:0].name,classes[r->klass<3?r->klass:2],
        (unsigned long long)r->key,(unsigned)r->lua,(unsigned)r->blocked,(unsigned long long)r->a1,(unsigned long long)r->a2,(unsigned long long)r->state,
        r->chain[0],r->chain[1],r->chain[2],r->chain[3],r->chain[4],r->chain[5],(unsigned)r->other);
    return n>0 && (size_t)n<cap?n:0;
}
static int ul_status(char *out,size_t cap) {
    static const char *const modes[4]={"off","observe","block","key"};
    int n=snprintf(out,cap,"installed=%d mode=%s frame=%llu lua=%llu/%llu/%llu sim=%llu/%llu/%llu other=%lld/%lld/%lld blocked=%llu unmapped=%llu rows=%lld window=%llu-%llu restored=%d stage=%s failures=%lld keyed=%llu/%llu/%llu unkeyed=%llu build=%llu/%llu/%lld map=%llu/%llu display=%llu/%llu",
        ul_installed,modes[ul_mode>=0 && ul_mode<4?ul_mode:0],(unsigned long long)stats.issued,(unsigned long long)ul_lua[0],(unsigned long long)ul_lua[1],
        (unsigned long long)ul_lua[2],(unsigned long long)ul_sim[0],(unsigned long long)ul_sim[1],(unsigned long long)ul_sim[2],(long long)ul_other[0],(long long)ul_other[1],(long long)ul_other[2],(unsigned long long)ul_blocked,
        (unsigned long long)ul_unmapped,(long long)ul_rows,(unsigned long long)ul_window_from,(unsigned long long)ul_window_to,ul_restored,ul_stage,(long long)ul_failures,(unsigned long long)ul_keyed[0],(unsigned long long)ul_keyed[1],(unsigned long long)ul_keyed[2],(unsigned long long)ul_unkeyed,
        (unsigned long long)ul_lua[UL_BUILD],(unsigned long long)ul_sim[UL_BUILD],(long long)ul_other[UL_BUILD],   /* build: UI queries / simulation calls / other threads */
        (unsigned long long)ul_map_blocked,(unsigned long long)ul_map_passed,
        (unsigned long long)ul_display_blocked,(unsigned long long)ul_display_passed);
    return n>0 && (size_t)n<cap?n:0;
}

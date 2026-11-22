/* native_gate_worker_layout = W on the Linux gate (4..32; the defaults' 30): Windows' lockstep_worker_layout.h. Included
 * by gate_linux.c after gate_path.h.
 *
 * X4's Linux build sizes the same two structures from its job thread count, and that count is the PC's:
 * at 0x730d96 sysconf(_SC_NPROCESSORS_ONLN) - 2, clamped to 4..32, goes to [*(0x3daab88)+0x550] (Windows counts the
 * logical processors of GetLogicalProcessorInformation into [*(0x6cf1540)+0x528]; the same number on one PC): 22 on a
 * 24-thread laptop against 30 on a 32-thread Windows PC.
 *  1. The PathingManager's constructor (tail 0x13e92c0, Windows 0xeabb9e) makes k1 = max(1, min(W-1, floor(2W/3)))
 *     batches of kind 1 at +0x2c8 and W-k1 of kind 0 at +0x2b0 (Windows +0x198 / +0x180): reserve 0x13d0190, then one
 *     emplace_back(kind) 0x1f3c7a0 per batch. AssignAgentToBatch 0x13c7580 (Windows 0xeaf2b0) deals every new Agent
 *     round-robin (+0x2e8 / +0x2f0, Windows +0x1b8 / +0x1c0) into them; the new-agent list is +0x2a0 count, +0x2a8
 *     sentinel (Windows +0x170 / +0x178). A batch is 0x20 bytes as on Windows: +0 kind, +8 count, +0x10 its sentinel
 *     node (0x18 bytes from X4's allocator, linked to itself), +0x18 the cursor (at the sentinel while empty).
 *  2. XPhys::XJobSystem::GetMaxConcurrency (vtable 0x3b0dff8 slot 2, 0xed4eb0; Windows slot 1, 0xc5940) returns
 *     min(that count, INT_MAX) to Jolt.
 * As on Windows, before the world's first pass (the first pathing wake after the arm, lxp_path_release, while the
 * pathing thread waits for it) each batch vector whose size is not the one for W is rebuilt as the constructor builds
 * it: X4's ~vector<Batch> 0x6a0bf4 (every batch's nodes and the storage freed), the vector zeroed, reserve, W's
 * batches made by emplace_back into the reserved room (nothing is moved: X4's reallocation copies a batch's cursor
 * onto the old batch's sentinel, Windows run wlE1). Both round-robin indices go to 0; slot 2 of the XJobSystem vtable
 * returns W. Every batch must still be empty, otherwise nothing is written and the first admitted step stops
 * (reason 224, stage occupied). The job threads stay the PC's. Same Lua calls and status text as Windows. Pins:
 * wlay_count, wlay_ctor_block, wlay_assign, wlay_reserve, wlay_emplace, wlay_vector_dtor, wlay_jolt. */
#define LXWL_JTM_VA 0x3daab88u              /* JobThreadManager* (Windows 0x6cf1540) */
#define LXWL_JTM_WORKERS 0x550u             /* its worker count (Windows +0x528) */
#define LXWL_VECTORS 0x2b0u                 /* the batch vectors {begin, end, capacity}: kind 0 +0x2b0, kind 1 +0x2c8 */
#define LXWL_NEXT 0x2e8u                    /* AssignAgentToBatch's round-robin index of each vector (+0x2e8, +0x2f0) */
#define LXWL_PENDING 0x2a0u                 /* the new-agent list: count +0x2a0, sentinel +0x2a8 */
#define LXWL_BATCH 0x20u
#define LXWL_RESERVE_VA 0x13d0190u          /* std::vector<Batch>::reserve(vector, n) */
#define LXWL_EMPLACE_VA 0x1f3c7a0u          /* std::vector<Batch>::emplace_back(vector, const char &kind) */
#define LXWL_DTOR_VA 0x6a0bf4u              /* std::vector<Batch>::~vector(vector) */
#define LXWL_JOLT_SLOT_VA (0x3b0dff8u+2u*8u) /* XPhys::XJobSystem vtable, slot 2 */
#define LXWL_JOLT_VA 0xed4eb0u              /* GetMaxConcurrency */
#define LXWL_STOP_REASON 224u               /* Windows' WLAY_STOP_REASON */
#define LXWL_MIN 4u
#define LXWL_MAX 32u
typedef void (*lxwl_reserve_fn)(void *vector,uint64_t n);
typedef void (*lxwl_emplace_fn)(void *vector,const char *kind);
typedef void (*lxwl_dtor_fn)(void *vector);
static lxwl_reserve_fn lxwl_reserve;static lxwl_emplace_fn lxwl_emplace;static lxwl_dtor_fn lxwl_dtor; /* tests may plant them */
static unsigned lxwl_workers,lxwl_native_workers,lxwl_layout[2],lxwl_native[2],lxwl_next[2],lxwl_pc,lxwl_pc_layout[2];
static volatile int lxwl_pending;
static int lxwl_armed,lxwl_applied,lxwl_jolt,lxwl_restored;
static uint64_t lxwl_pass_at,lxwl_pending_agents;
static volatile int lxwl_jolt_value;
static const char *lxwl_stage="idle";
/* X4's constructor's split for w workers (0x13e92c0): out[0] kind 0 batches, out[1] kind 1. */
static void lxwl_split(unsigned w,unsigned out[2]) {
    unsigned a=w-1u,b=(2u*w)/3u,k1=a<b?a:b;if(k1<1u) k1=1u;out[1]=k1;out[0]=w-k1;
}
static int lxwl_jolt_concurrency(const void *self) {(void)self;return lxwl_jolt_value;}
static unsigned char *lxwl_manager(void) {unsigned char *m=NULL;return gate_read_local(LX(LX_PATH_MANAGER_VA),&m,8)?m:NULL;}
static int lxwl_pins(void) {
    static const char *const names[]={"wlay_count","wlay_ctor_block","wlay_assign","wlay_reserve","wlay_emplace","wlay_vector_dtor","wlay_jolt"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {const lx_site *s=site_named(names[i]);if(!s || !site_ok(s)) return 0;}
    return 1;
}
/* Arm (the probe, before the load): the pins, the path hook, the counts, the Jolt slot; the batches wait for the first
 * pathing wake. pc (test only, 0 = none): first lay the batches out as X4 does on a PC with pc workers. */
static int lxwl_arm(unsigned w,unsigned pc) {
    if(lxwl_armed) return lxwl_workers==w && lxwl_pc==pc;
    if(w<LXWL_MIN || w>LXWL_MAX || (pc && (pc<LXWL_MIN || pc>LXWL_MAX))) {lxwl_stage="value";return 0;}
    if(!lxwl_pins()) {lxwl_stage="pins";return 0;}
    if(!path_installed) {lxwl_stage="path";return 0;}   /* the layout is applied in the pathing release (serial pathing) */
    unsigned char *jtm=NULL;
    if(!gate_read_local(LX(LXWL_JTM_VA),&jtm,8) || !jtm || !gate_read_local(jtm+LXWL_JTM_WORKERS,&lxwl_native_workers,4)) {lxwl_stage="jtm";return 0;}
    if(!lxwl_manager()) {lxwl_stage="manager";return 0;}
    if(rd64(LX(LXWL_JOLT_SLOT_VA))!=(uint64_t)(uintptr_t)LX(LXWL_JOLT_VA)) {lxwl_stage="slot";return 0;}
    if(!lxwl_reserve) lxwl_reserve=(lxwl_reserve_fn)(void*)LX(LXWL_RESERVE_VA);
    if(!lxwl_emplace) lxwl_emplace=(lxwl_emplace_fn)(void*)LX(LXWL_EMPLACE_VA);
    if(!lxwl_dtor) lxwl_dtor=(lxwl_dtor_fn)(void*)LX(LXWL_DTOR_VA);
    lxwl_workers=w;lxwl_split(w,lxwl_layout);lxwl_jolt_value=(int)w;lxwl_pc=pc;
    if(pc) lxwl_split(pc,lxwl_pc_layout);else memset(lxwl_pc_layout,0,sizeof(lxwl_pc_layout));
    if(!gate_data_swap64(LX(LXWL_JOLT_SLOT_VA),(uint64_t)(uintptr_t)LX(LXWL_JOLT_VA),(uint64_t)(uintptr_t)lxwl_jolt_concurrency)) {lxwl_stage="slot_swap";return 0;}
    lxwl_jolt=1;lxwl_armed=1;lxwl_restored=0;lxwl_applied=0;lxwl_stage="armed";
    __atomic_store_n(&lxwl_pending,1,__ATOMIC_SEQ_CST);return 1;
}
/* An empty batch: no member, its cursor at its sentinel, the sentinel linked to itself. */
static int lxwl_batch_empty(const unsigned char *batch) {
    uint64_t count=1;uintptr_t sentinel=0,cursor=0,links[2]={0,0};
    return gate_read_local(batch+8,&count,8) && gate_read_local(batch+0x10,&sentinel,8) && gate_read_local(batch+0x18,&cursor,8) &&
        !count && sentinel && cursor==sentinel && gate_read_local((const void*)sentinel,links,16) && links[0]==sentinel && links[1]==sentinel;
}
/* Each vector whose size is not want[v] rebuilt as the constructor builds it (every batch is empty, lxwl_apply checked). */
static void lxwl_resize(unsigned char *manager,const unsigned want[2]) {
    for(unsigned v=0;v<2;v++) {
        unsigned char *vector=manager+LXWL_VECTORS+0x18u*v;
        unsigned char *begin=*(unsigned char**)vector,*end=*(unsigned char**)(vector+8);
        if((uintptr_t)(end-begin)==(uintptr_t)want[v]*LXWL_BATCH) continue;
        lxwl_dtor(vector);memset(vector,0,24);   /* destroyed, then as a new empty vector */
        lxwl_reserve(vector,want[v]);
        const char kind=(char)v;
        for(unsigned b=0;b<want[v];b++) lxwl_emplace(vector,&kind);
    }
}
/* lxp_path_release (the MoveWorker, before the wake: no pass runs). 1 = laid out or nothing to do, 0 = refused. */
static int lxwl_apply(unsigned char *manager) {
    if(!__atomic_load_n(&lxwl_pending,__ATOMIC_SEQ_CST)) return 1;
    __atomic_store_n(&lxwl_pending,0,__ATOMIC_SEQ_CST);
    if(!manager) {lxwl_stage="manager";return 0;}
    uint64_t pending=1;
    if(!gate_read_local(manager+LXWL_PENDING,&pending,8)) {lxwl_stage="unreadable";return 0;}
    unsigned char *bounds[2][3];
    for(unsigned v=0;v<2;v++) {
        if(!gate_read_local(manager+LXWL_VECTORS+0x18u*v,bounds[v],24) || !bounds[v][0] || bounds[v][1]<bounds[v][0] ||
           bounds[v][2]<bounds[v][1] || (uintptr_t)(bounds[v][1]-bounds[v][0])%LXWL_BATCH) {lxwl_stage="unreadable";return 0;}
        lxwl_native[v]=(unsigned)((uintptr_t)(bounds[v][1]-bounds[v][0])/LXWL_BATCH);
        for(unsigned b=0;b<lxwl_native[v];b++) {
            const unsigned char *batch=bounds[v][0]+(uintptr_t)b*LXWL_BATCH;
            if(!lxwl_batch_empty(batch) || batch[0]!=(unsigned char)v) {lxwl_stage="occupied";return 0;}
        }
    }
    lxwl_pending_agents=pending;   /* Agents waiting on the new-agent list are dealt at the pass: after the layout, from index 0. */
    if(lxwl_pc) lxwl_resize(manager,lxwl_pc_layout);   /* test: this client's PC as X4 would lay it out */
    lxwl_resize(manager,lxwl_layout);
    for(unsigned v=0;v<2;v++) {
        uint64_t zero=0;
        if(!gate_read_local(manager+LXWL_NEXT+8u*v,&lxwl_next[v],4)) lxwl_next[v]=UINT32_MAX;
        memcpy(manager+LXWL_NEXT+8u*v,&zero,8);
    }
    for(unsigned v=0;v<2;v++) {   /* As laid out: the counts, kinds and empty lists. */
        unsigned char *begin=*(unsigned char**)(manager+LXWL_VECTORS+0x18u*v),*end=*(unsigned char**)(manager+LXWL_VECTORS+0x18u*v+8);
        if((uintptr_t)(end-begin)!=(uintptr_t)lxwl_layout[v]*LXWL_BATCH) {lxwl_stage="layout";return 0;}
        for(unsigned b=0;b<lxwl_layout[v];b++) if(!lxwl_batch_empty(begin+(uintptr_t)b*LXWL_BATCH) || begin[(uintptr_t)b*LXWL_BATCH]!=(unsigned char)v)
            {lxwl_stage="layout";return 0;}
    }
    lxwl_applied=1;lxwl_pass_at=stats.issued;lxwl_stage="laid_out";return 1;
}
/* The first pathing wake after the arm (any boot phase): the layout. */
static void lxwl_wake(void) {
    if(__atomic_load_n(&lxwl_pending,__ATOMIC_SEQ_CST)) lxwl_apply(lxwl_manager());
}
/* Every admitted step's release, after lxwl_wake: laid out, and the Jolt slot still ours. */
static int lxwl_check(void) {
    if(!lxwl_armed) return 1;
    if(!lxwl_applied) {if(!strcmp(lxwl_stage,"armed")) lxwl_stage="not_laid_out";return 0;}
    if(lxwl_jolt && rd64(LX(LXWL_JOLT_SLOT_VA))!=(uint64_t)(uintptr_t)lxwl_jolt_concurrency) {lxwl_stage="slot_changed";return 0;}
    return 1;
}
/* Stop: Jolt's slot back. The batches keep the layout (empty or not, X4 deals into whatever there is). */
static void lxwl_restore(void) {
    __atomic_store_n(&lxwl_pending,0,__ATOMIC_SEQ_CST);
    if(lxwl_jolt) {
        if(gate_data_swap64(LX(LXWL_JOLT_SLOT_VA),(uint64_t)(uintptr_t)lxwl_jolt_concurrency,(uint64_t)(uintptr_t)LX(LXWL_JOLT_VA))) lxwl_jolt=0;
        else lxwl_stage="restore_foreign";
    }
    if(lxwl_armed && !lxwl_jolt) {lxwl_armed=0;lxwl_restored=1;}
}
static int lxwl_status(char *out,size_t capacity) {
    return snprintf(out,capacity,"armed=%d applied=%d workers=%u native_workers=%u layout=%u+%u native=%u+%u pc=%u next=%u/%u pending=%llu jolt=%d:%d restored=%d at=%llu stage=%s",
        lxwl_armed,lxwl_applied,lxwl_workers,lxwl_native_workers,lxwl_layout[0],lxwl_layout[1],lxwl_native[0],lxwl_native[1],lxwl_pc,
        lxwl_next[0],lxwl_next[1],(unsigned long long)lxwl_pending_agents,lxwl_jolt,lxwl_jolt_value,lxwl_restored,(unsigned long long)lxwl_pass_at,lxwl_stage);
}
/* gate.worker_layout(W[, P]) and gate.worker_layout_status(), as Windows. */
static int lua_worker_layout(lua_State *L) {
    double w=p_lua_tonumber(L,1),pc=p_lua_tonumber(L,2);
    int ok=accept_ui() && isfinite(w) && w>=LXWL_MIN && w<=LXWL_MAX && floor(w)==w &&
        isfinite(pc) && (pc==0 || (pc>=LXWL_MIN && pc<=LXWL_MAX)) && floor(pc)==pc;
    p_lua_pushnumber(L,ok && lxwl_arm((unsigned)w,(unsigned)pc));return 1;
}
static int lua_worker_layout_status(lua_State *L) {
    char out[320];int n=lxwl_status(out,sizeof(out));p_lua_pushlstring(L,out,(size_t)(n>0 && (size_t)n<sizeof(out)?n:0));return 1;
}

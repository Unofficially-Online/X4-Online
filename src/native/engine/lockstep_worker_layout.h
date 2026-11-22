/* Worker layout, a session default: native_gate_worker_layout = W (4..32, the defaults' 30).
 *
 * X4 sizes two simulation structures from its job thread count, and that count is the PC's:
 * JobThreadManager 0x14bcf80 stores clamp(logical processors - 2, 4, 32) at [*(0x6cf1540)+0x528] (popcount of every
 * RelationProcessorCore mask of GetLogicalProcessorInformation): 30 on a 16-core/32-thread PC, 22 on a 12/24, 14 on 8/16.
 *  1. The PathingManager's constructor (0xeabb9e) makes k1 = max(1, min(W-1, floor(2W/3))) batches of kind 1 at +0x198
 *     and W-k1 of kind 0 at +0x180 (30: 10 + 20; 22: 8 + 14). AssignAgentToBatch 0xeaf2b0 deals every new Agent
 *     round-robin (+0x1b8 / +0x1c0, mod the vector's size) into them. So two PCs put the same ships into other batches,
 *     and the session defaults' pathing decisions follow the batches: native_gate_path_budget's N full updates per batch
 *     and pass (the rest cheap), native_gate_travel_determinism's batch order. Read live on this PC (21 Nov 2026):
 *     workers 30, batches 10 + 20, the 20 of kind 1 holding about 500 Agents each. Two PCs with other thread counts
 *     part within ticks; a one-PC test never sees it (both clients have the same count). The hot join's PATH section
 *     refuses a joiner whose counts are not the session's (why=shape check=batches).
 *  2. XPhys::XJobSystem::GetMaxConcurrency (vtable 0x2a2aba0 slot 1, 0xc5940) returns the same count to Jolt, which
 *     sizes its jobs by it. Every Jolt job runs on the MoveWorker in insertion order (lockstep_physics_trace.h), so
 *     the count most likely changes no result; it is fixed as well because it costs nothing.
 * This module lays out both as X4 does for W workers on every client, whatever its PC: before the world's first pass
 * (the first pathing wake after the arm, path_release, while the pathing thread waits for it) the two batch vectors
 * are resized with X4's own code to the counts above for W (reserve 0xf1fad0, a batch's list 0xf16920 as the
 * constructor makes it, destroy range 0xf28870 for surplus ones) and both round-robin indices set to 0; slot 1 of
 * the XJobSystem vtable returns W. Every batch must still be empty (count 0, cursor at its sentinel); Agents the load
 * has put on the new-agent list (+0x170) are dealt at that pass, into the new layout from index 0. Otherwise nothing
 * is written and the first admitted step stops (reason 224, stage occupied). The job threads stay the PC's: they only
 * run jobs.
 * Test only: native_gate_worker_layout_test = {slot, W'} gives one client W' (another PC's layout on this one, as
 * without this module); {slot, P, W'} first lays that client out as X4 does on a PC with P workers, then as for W'
 * (another PC with this module).
 * Linux: X4's Linux build counts sysconf(_SC_NPROCESSORS_ONLN) - 2; the twin is linux/gate_worker_layout.h (same Lua
 * calls, status text and stop reason 224). */
#define WLAY_JTM 0x6cf1540u              /* JobThreadManager* */
#define WLAY_JTM_WORKERS 0x528u          /* its worker count (unsigned) */
#define WLAY_VECTORS 0x180u              /* the batch vectors {begin, end, capacity}: kind 0 at +0x180, kind 1 at +0x198 */
#define WLAY_NEXT 0x1b8u                 /* AssignAgentToBatch's round-robin index of each vector (+0x1b8, +0x1c0) */
#define WLAY_PENDING 0x170u              /* the new-agent list: count +0x170, sentinel +0x178 */
#define WLAY_BATCH 0x20u                 /* a batch: +0 kind byte, +8 count, +0x10 sentinel, +0x18 cursor */
#define WLAY_RESERVE 0xf1fad0u           /* std::vector<Batch>::_Reallocate_exactly(vector, n) */
#define WLAY_BATCH_CTOR 0xf16920u        /* a batch's list at element+8 (count, sentinel, cursor at the sentinel) */
#define WLAY_DESTROY 0xf28870u           /* _Destroy_range(first, last) of batches */
#define WLAY_CTOR_BLOCK 0xeabb9eu        /* the constructor's layout from the worker count */
#define WLAY_JOLT_SLOT 0x2a2aba8u        /* XPhys::XJobSystem vtable 0x2a2aba0, slot 1 */
#define WLAY_JOLT_FN 0xc5940u            /* GetMaxConcurrency: min([JTM+0x528], INT_MAX) */
#define WLAY_STOP_REASON 224u
#define WLAY_MIN 4u
#define WLAY_MAX 32u
typedef void (*wlay_reserve_fn)(void *vector,uint64_t n);
typedef void *(*wlay_ctor_fn)(void *list,void *element);
typedef void (*wlay_destroy_fn)(void *first,void *last);
static wlay_reserve_fn wlay_reserve;static wlay_ctor_fn wlay_ctor;static wlay_destroy_fn wlay_destroy;
static unsigned wlay_workers,wlay_native_workers,wlay_layout[2],wlay_native[2],wlay_next[2],wlay_pc,wlay_pc_layout[2];
static volatile LONG wlay_pending;
static int wlay_armed,wlay_applied,wlay_jolt,wlay_restored;
static uint64_t wlay_pass_at,wlay_pending_agents;
static volatile int wlay_jolt_value;
static const char *wlay_stage="idle";
/* X4's constructor's split for w workers: out[0] kind 0 batches, out[1] kind 1. */
static void wlay_split(unsigned w,unsigned out[2]) {
    unsigned a=w-1u,b=(2u*w)/3u,k1=a<b?a:b;if(k1<1u) k1=1u;out[1]=k1;out[0]=w-k1;
}
static int wlay_jolt_concurrency(const void *self) {(void)self;return wlay_jolt_value;}
static unsigned char *wlay_manager(void) {unsigned char *m=NULL;return image && read_local(image+PATH_DET_MANAGER,&m,8)?m:NULL;}
static int wlay_pins(void) {
    return hash_ok(image+0xeaf2b0,216,"c44bc6f94a518ef5613a518587617c01250558926488dac7a80f14a970039e11") &&   /* AssignAgentToBatch */
        hash_ok(image+WLAY_CTOR_BLOCK,215,"1d2a5d094b5eea5ff4dceac3425a346095793b15099dbab5c6605e9eb441f253") &&
        hash_ok(image+WLAY_BATCH_CTOR,94,"dde98228fe9e00dd7cc6fb359eae2a42a1e0d0a550a9483ec29493021b524c30") &&
        hash_ok(image+WLAY_RESERVE,202,"3db8966ae437726388698f3f932bd78bb60eeba5256096fdaca57713ba0e6ea7") &&
        hash_ok(image+WLAY_DESTROY,128,"9a89da1efd2da25e4d7edd808c8b25893db847fa2125249607f4131bd91c22dc") &&
        hash_ok(image+WLAY_JOLT_FN,24,"ea41983c8853e368253719bebeee39612ea260cb5719e633669388722b04aaa6") &&
        hash_ok(image+0x14bd095,75,"1cdaade74bb0a78b3e6c4a556565cc531071d26ec2c627ad7c4fc24cf3286599");   /* the count's clamp */
}
static int wlay_slot_swap(void *expected,void *value) {
    unsigned char *p=image+WLAY_JOLT_SLOT;DWORD old,ignored;
    if(!VirtualProtect(p,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)p,value,expected);
    VirtualProtect(p,8,old,&ignored);return actual==expected;
}
/* Arm (the probe, before the load): the pins, the counts, the Jolt slot; the batches wait for the first pathing wake.
 * pc (test only, 0 = none): first lay the batches out as X4 does on a PC with pc workers. */
static int wlay_arm(unsigned w,unsigned pc) {
    if(wlay_armed) return wlay_workers==w && wlay_pc==pc;
    if(w<WLAY_MIN || w>WLAY_MAX || (pc && (pc<WLAY_MIN || pc>WLAY_MAX)) || !image) {wlay_stage="value";return 0;}
    if(!wlay_pins()) {wlay_stage="pins";return 0;}
    unsigned char *jtm=NULL;
    if(!read_local(image+WLAY_JTM,&jtm,8) || !jtm || !read_local(jtm+WLAY_JTM_WORKERS,&wlay_native_workers,4)) {wlay_stage="jtm";return 0;}
    if(!wlay_manager()) {wlay_stage="manager";return 0;}
    if(*(void**)(image+WLAY_JOLT_SLOT)!=(void*)(image+WLAY_JOLT_FN)) {wlay_stage="slot";return 0;}
    if(!wlay_reserve) wlay_reserve=(wlay_reserve_fn)(void*)(image+WLAY_RESERVE);
    if(!wlay_ctor) wlay_ctor=(wlay_ctor_fn)(void*)(image+WLAY_BATCH_CTOR);
    if(!wlay_destroy) wlay_destroy=(wlay_destroy_fn)(void*)(image+WLAY_DESTROY);
    wlay_workers=w;wlay_split(w,wlay_layout);wlay_jolt_value=(int)w;wlay_pc=pc;
    if(pc) wlay_split(pc,wlay_pc_layout);else memset(wlay_pc_layout,0,sizeof(wlay_pc_layout));
    if(!wlay_slot_swap(image+WLAY_JOLT_FN,(void*)wlay_jolt_concurrency)) {wlay_stage="slot_swap";return 0;}
    wlay_jolt=1;wlay_armed=1;wlay_restored=0;wlay_applied=0;wlay_stage="armed";
    InterlockedExchange(&wlay_pending,1);return 1;
}
/* An empty batch: no member, its cursor at its sentinel, the sentinel linked to itself. */
static int wlay_batch_empty(unsigned char *batch) {
    uint64_t count=1;uintptr_t sentinel=0,cursor=0,links[2]={0,0};
    return read_local(batch+8,&count,8) && read_local(batch+0x10,&sentinel,8) && read_local(batch+0x18,&cursor,8) &&
        !count && sentinel && cursor==sentinel && read_local((void*)sentinel,links,16) && links[0]==sentinel && links[1]==sentinel;
}
/* The two vectors resized to want[] batches with X4's own code: surplus batches destroyed, missing ones made as the
 * constructor makes them (kind byte, empty list). Every batch is empty (wlay_apply checked). */
static void wlay_resize(unsigned char *manager,const unsigned want_layout[2]) {
    for(unsigned v=0;v<2;v++) {
        unsigned char *vector=manager+WLAY_VECTORS+0x18u*v;unsigned want=want_layout[v],have=(unsigned)
            ((uintptr_t)(*(unsigned char**)(vector+8)-*(unsigned char**)vector)/WLAY_BATCH);
        if(have>want) {
            unsigned char *begin=*(unsigned char**)vector,*end=*(unsigned char**)(vector+8),*keep=begin+(uintptr_t)want*WLAY_BATCH;
            wlay_destroy(keep,end);*(unsigned char**)(vector+8)=keep;
        } else if(have<want) {
            uint64_t capacity=(uint64_t)((uintptr_t)(*(unsigned char**)(vector+0x10)-*(unsigned char**)vector)/WLAY_BATCH);
            if(capacity<want) {
                /* As the constructor grows it: from an empty vector. X4's reserve copies every batch over with 0xf28d00,
                 * cursor included, so a copied batch's cursor would name the old batch's sentinel, which the reserve
                 * then frees (run wlE1: stage layout). The batches are empty: destroy them and make all anew. */
                unsigned char *begin=*(unsigned char**)vector,*end=*(unsigned char**)(vector+8);
                wlay_destroy(begin,end);*(unsigned char**)(vector+8)=begin;have=0;
                wlay_reserve(vector,want);
            }
            for(unsigned b=have;b<want;b++) {
                unsigned char *element=*(unsigned char**)(vector+8);element[0]=(unsigned char)v;
                wlay_ctor(element+8,element);*(unsigned char**)(vector+8)=element+WLAY_BATCH;
            }
        }
    }
}
/* path_release (the MoveWorker, before the wake: no pass runs). 1 = laid out or nothing to do, 0 = refused. */
static int wlay_apply(unsigned char *manager) {
    if(!InterlockedCompareExchange(&wlay_pending,0,0)) return 1;
    InterlockedExchange(&wlay_pending,0);
    if(!manager) {wlay_stage="manager";return 0;}
    uint64_t pending=1;
    if(!read_local(manager+WLAY_PENDING,&pending,8)) {wlay_stage="unreadable";return 0;}
    unsigned char *bounds[2][3];
    for(unsigned v=0;v<2;v++) {
        if(!read_local(manager+WLAY_VECTORS+0x18u*v,bounds[v],24) || !bounds[v][0] || bounds[v][1]<bounds[v][0] ||
           bounds[v][2]<bounds[v][1] || (uintptr_t)(bounds[v][1]-bounds[v][0])%WLAY_BATCH) {wlay_stage="unreadable";return 0;}
        wlay_native[v]=(unsigned)((uintptr_t)(bounds[v][1]-bounds[v][0])/WLAY_BATCH);
        for(unsigned b=0;b<wlay_native[v];b++) {
            unsigned char *batch=bounds[v][0]+(uintptr_t)b*WLAY_BATCH;
            if(!wlay_batch_empty(batch) || batch[0]!=(unsigned char)v) {wlay_stage="occupied";return 0;}
        }
    }
    wlay_pending_agents=pending;   /* Agents waiting on the new-agent list are dealt at the pass: after the layout, from index 0. */
    if(wlay_pc) wlay_resize(manager,wlay_pc_layout);   /* test: this client's PC as X4 would lay it out */
    wlay_resize(manager,wlay_layout);
    for(unsigned v=0;v<2;v++) {
        uint64_t zero=0;
        if(!read_local(manager+WLAY_NEXT+8u*v,&wlay_next[v],4)) wlay_next[v]=UINT32_MAX;
        memcpy(manager+WLAY_NEXT+8u*v,&zero,8);
    }
    for(unsigned v=0;v<2;v++) {   /* As laid out: the counts, kinds and empty lists. */
        unsigned char *begin=*(unsigned char**)(manager+WLAY_VECTORS+0x18u*v),*end=*(unsigned char**)(manager+WLAY_VECTORS+0x18u*v+8);
        if((uintptr_t)(end-begin)!=(uintptr_t)wlay_layout[v]*WLAY_BATCH) {wlay_stage="layout";return 0;}
        for(unsigned b=0;b<wlay_layout[v];b++) if(!wlay_batch_empty(begin+(uintptr_t)b*WLAY_BATCH) || begin[(uintptr_t)b*WLAY_BATCH]!=(unsigned char)v)
            {wlay_stage="layout";return 0;}
    }
    wlay_applied=1;wlay_pass_at=stats.issued;wlay_stage="laid_out";return 1;
}
/* path_release of every admitted step, after wlay_apply: laid out, and the Jolt slot still ours. */
static int wlay_check(void) {
    if(!wlay_armed) return 1;
    if(!wlay_applied) {if(!strcmp(wlay_stage,"armed")) wlay_stage="not_laid_out";return 0;}
    if(wlay_jolt && *(void**)(image+WLAY_JOLT_SLOT)!=(void*)wlay_jolt_concurrency) {wlay_stage="slot_changed";return 0;}
    return 1;
}
/* Stop: Jolt's slot back. The batches keep the layout (empty or not, X4 deals into whatever there is). */
static void wlay_restore(void) {
    InterlockedExchange(&wlay_pending,0);
    if(wlay_jolt) {
        if(wlay_slot_swap((void*)wlay_jolt_concurrency,image+WLAY_JOLT_FN)) wlay_jolt=0;else wlay_stage="restore_foreign";
    }
    if(wlay_armed && !wlay_jolt) {wlay_armed=0;wlay_restored=1;}
}
static int wlay_status(char *out,size_t capacity) {
    return snprintf(out,capacity,"armed=%d applied=%d workers=%u native_workers=%u layout=%u+%u native=%u+%u pc=%u next=%u/%u pending=%llu jolt=%d:%d restored=%d at=%llu stage=%s",
        wlay_armed,wlay_applied,wlay_workers,wlay_native_workers,wlay_layout[0],wlay_layout[1],wlay_native[0],wlay_native[1],wlay_pc,
        wlay_next[0],wlay_next[1],(unsigned long long)wlay_pending_agents,wlay_jolt,wlay_jolt_value,wlay_restored,(unsigned long long)wlay_pass_at,wlay_stage);
}

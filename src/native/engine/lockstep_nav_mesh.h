/* Determinism fix, DEFAULT OFF: native_gate_nav_mesh = 'observe' | 'sync' (stop reason 186;
 * reports/LOCKSTEP-ON-FOOT-20261101.txt sections 10 and 11).
 *
 * w1bfoot / s1k1 (both ships 3 km from the wharf AVK-777): the relay's digest differed at ticks
 * 65-66 and 68-71 (66-67 in s1k1) and agreed again at 67 and 72, while every logged row agreed.
 * census_runtime_callers.py --first: the shared runtime-ID list parts at tick 70, one allocation
 * more on client 2 from the stack caab18 <- 1111aaa <- 111c477 <- 9d0127 (NPCSlotsValidatedEvent
 * slot 2) <- c9d5c8 <- 99c5b0 <- 99a4ed <- 6b86a1, cue OnNPCSlotsValidated (md/npc_instantiation.xml:
 * InstantiateNPCs places the station's people). The digest carries the next runtime ID
 * (native_gate_runtime_ids_barrier), so the same NPCs created two ticks apart are a transient
 * difference; their scripts start two ticks apart for good.
 *
 * What X4 does [F, pinned 9.00]:
 *   request   an object that gets its props (GeneratePropsEvent 0xce -> Positional slot 521 0x753de0,
 *             also WalkableModule slot 521) asks AI::NavMeshCache (*0x6cf21f8) for its interior nav
 *             helper: 0x75b3d0 -> 0x1087df0(cache, id, 1, priority: 0 the player's own container, 1, 2)
 *             puts the ID into one of four priority sets (sizes cache+0x248+0x40*i); a rebuild 0x75b440
 *             appends to the list +0x320 (size +0x328). Both under the lock +0x1f0, and both release the
 *             cache's semaphore (+0x58) once per call.
 *   debounce  a request that adds its ID (0x1087fbb) and every rebuild request (0x75b538) also put the ID
 *             into the busy set +0x170 (lock +0x148). The cache's frame update (primary vtable slot 1,
 *             0x1087d90) clears +0x1b0 and swaps it with +0x170; it runs only from 0xef41e0 on the
 *             Movement worker (TLS+0x314), i.e. once per UniverseFrameUpdate (0x9fe5a4). The busy test
 *             0x108ca70 reads both sets, so an ID stays busy until the second UniverseFrameUpdate after
 *             its request.
 *   thread    the cache's own thread (secondary vtable slot 1, 0x1087810) waits on the semaphore
 *             (0x10878d9, WaitForSingleObject through the import 0x1bc14b0), then takes the lowest ID of
 *             each set in turn and builds it: 0x108bb90(cache, id) at 0x108793c; then the rebuild list:
 *             0x108adb0(cache, id, params) at 0x1087a76. An ID is erased from its set only after its build
 *             returned. Both builds take the build lock +0x30 and Sleep(10) while their ID is busy, so
 *             natively a helper is built at wall-clock time 10 ms after the busy sets let it go.
 *   publish   the build stores the helper with the thread's TLS game time (+0x18 of the record) and
 *             0x108c800 publishes it in the map +0xd0 (lock +0xa0; lookups 0x75b300) and posts
 *             U::NavHelperGeneratedEvent (0x2baf4c8) to the object with the thread's TLS game time through
 *             0x99a720 (PostEventDeferred); a rebuild posts NavHelperRegeneratedEvent (0x2baf570). Their
 *             dispatch is listener slot 454/455: NavContext 0x6b8550 posts U::NPCSlotsValidatedEvent to the
 *             interior and its station -> md/npc_instantiation.xml OnNPCSlotsValidated.
 *   stop      the thread runs while the byte cache+0x60 is set (0x10878b3, tested at 0x1087d25); the
 *             cache's shutdown (0x1226ddd) clears it, releases the semaphore and waits for the thread.
 * So when the people of a station appear, and when a walking NPC or the player finds the interior's nav
 * helper, is the cache thread's wall-clock time on each client.
 *
 * 'sync': three call sites of the thread loop go through our code: the two builds (5-byte calls, through
 * jump stubs) and the thread's wait on its semaphore (the 6-byte import call keeps its length: its
 * displacement points at our pointer cell, so the return address is X4's own). From the thread's first
 * arrival at its wait after arming the thread neither takes an ID nor builds outside the gate's joins:
 *   take      after the semaphore wait returns, the thread waits at the take gate while the builds are
 *             closed. So it takes IDs only inside a join, from the sets as the joined world left them, in
 *             X4's own loop order: which ID it holds no longer depends on wall-clock time.
 *   build     a build waits while the builds are closed. Inside a join a busy ID is not built (the thread
 *             would Sleep(10) in X4's build until a UniverseFrameUpdate the join holds back): it is held,
 *             deferred to a later join, and the join does not wait for it. The busy sets are only changed
 *             by requests and UniverseFrameUpdates, so which join builds an ID is the same on every client.
 *   join      the gate's main-thread join (MoveWorker joined, before FrameTick, right after the nav octree
 *             join) opens the builds, waits until the thread holds nothing it can build (no build running;
 *             no ID left in the four sets or the rebuild list, or the held ID deferred) and closes them
 *             again. Inside the join the builder's TLS game time is the join's game time (the frame
 *             thread's TLS time at the join), so the record's time and the event's time are the same on
 *             every client; it is put back after the build.
 * Before the first join (the snapshot load and the boot) nothing is built: every helper the load asks for
 * waits, and the joins drain them as the busy sets let them go. The boot runs one UniverseFrameUpdate, so
 * the load's requests are still busy at join 0 (one deferral) and are built at join 1, at tick 1's game
 * time; requests made later in the boot frame follow at join 2. Arming releases the semaphore once so an
 * idle thread comes round to its (now ours) wait in the menu; a build taken before that arrival runs
 * natively while the menu lasts (pre_gate=); once the load is requested (boot phase 1) the builds close
 * whether or not the thread came round (cycled=0).
 * Build 162 (footr1, footr2) stopped at boot with join_timeout: builds were open until the first join, the
 * thread took a load ID at once and slept in X4's build on the busy sets; the join held back the
 * UniverseFrameUpdate that would clear them. Now nothing waits on the busy sets inside a join, and a build
 * found sleeping on them there (it entered X4's build outside a join) stops at once (build_busy) instead
 * of after the 30 s timeout.
 * The receipt NMESH_1:<builds>:<hash> joins the declared state: the hash folds, per join with builds,
 * the tick, the count and an order-independent sum over (portable key, class, kind) of what the join
 * built, so a helper built at another join on one client, or another set, differs at once.
 * 'observe' installs the same sites without waiting: NAV_MESH_ROW per finished build with the tick read
 * on the builder thread (the frame in which the helper became visible), key, kind, time and the build's
 * microseconds; nothing else changes.
 * Residual [I]: a thread that does not come round to its wait before the load (cycled=0: it was in a
 * native build of the menu's world when the load was requested) holds the first ID it takes after that
 * from wall-clock time; an ID taken in the menu and destroyed by the load is built (a no-op) at the first
 * join it is not busy at, counted in that client's receipt (unkeyed=). The order of the builds inside a
 * join follows X4's loop; events posted at the same time keep that order.
 * Pins: the thread loop (with its three calls as X4 has them), the build, its publish-and-post, the
 * rebuild, the busy test and the frame update. Stop reason 186: a site or stub no longer ours, a pin
 * change, a join timeout (a build that does not end, left open), a build sleeping on the busy sets inside a
 * join (build_busy, left open), a full join list, the cache's shutdown while the gate runs (shutdown: the
 * thread is let go). Stop opens the builds before the sites go back; a build or take waiting in our code
 * then runs as natively. Windows gate only. */
#define NMESH_STOP_REASON 186u
#define NMESH_CACHE 0x6cf21f8u            /* AI::NavMeshCache* */
#define NMESH_GENERATE 0x108bb90u         /* (cache, id) */
#define NMESH_REGENERATE 0x108adb0u       /* (cache, id, params) */
#define NMESH_BUSY 0x108ca70u             /* (cache, id): in the frame's busy sets */
#define NMESH_WAIT_IAT 0x1bc14b0u         /* KERNEL32!WaitForSingleObject: the thread's wait on its semaphore */
#define NMESH_SEMAPHORE 0x58u             /* cache+0x58: released once per request */
#define NMESH_RUN_FLAG 0x60u              /* cache+0x60: the thread runs while set; cleared only by the cache's shutdown */
#define NMESH_TLS_INIT 0x174f5fcu         /* the thread's TLS on demand (as every X4 function does first) */
#define NMESH_TLS_TIME 0x300u
#define NMESH_SITES 3u
#define NMESH_WAIT_SITE 2u
#define NMESH_JOIN_MS 30000u
#define NMESH_POLL_MS 50u                 /* a waiting thread looks at the cache's run flag this often */
#define NMESH_RING 4096u
#define NMESH_JOIN_MAX 1024u
#define NMESH_STUB_SIZE 0x1000u
enum {NMESH_OFF,NMESH_OBSERVE,NMESH_SYNC};
enum {NMESH_K_GENERATE,NMESH_K_REGENERATE};
enum {NMESH_GATE_WAIT,NMESH_GATE_ARRIVED,NMESH_GATE_FORCED};
/* The two builds are 5-byte calls (e8 rel32 to the function); the wait is a 6-byte import call (ff 15 disp32 to the
 * import's slot). Only the 4-byte displacement at the end changes. */
static const struct {unsigned rva,target,len;} nmesh_sites[NMESH_SITES]={
    {0x108793cu,NMESH_GENERATE,5},{0x1087a76u,NMESH_REGENERATE,5},{0x10878d9u,NMESH_WAIT_IAT,6}};
/* The sizes the thread's emptiness test reads (0x1087adb): the four priority sets and the rebuild list. */
static const unsigned nmesh_count_offsets[5]={0x248u,0x288u,0x2c8u,0x308u,0x328u};
static const struct {unsigned rva,size;const char *sha;} nmesh_pins[]={
    {0x1087810u,0x571u,"88483ea207403b2ed3834cc79edef441a64cb5674e5fb1664a7e60fd20ba2ca0"},  /* the thread loop (sites as X4 has them) */
    {0x108bb90u,0xc66u,"e1a9ecc1e6510fc92961c349fbc0525f638d1638b2c4046727be4857c8d63ae3"},  /* the build */
    {0x108c800u,0x26eu,"f148e1529e7c4959a5709d8179c3275ef7afc3b5852629d752b48a924585bc70"},  /* publish and post */
    {0x108adb0u,0xa98u,"96a941cf3aee59be9c745683739639169fdda486b2d24c322468aebd3bff9857"},  /* the rebuild */
    {0x108ca70u,0xbdu,"630db395df5523e2bce2ebd2aac2bae4ea086ca1a4b37631dad4c087c2c6f312"},   /* the busy test */
    {0x1087d90u,0x5du,"ba8bd83cd65fe4a912a7bcbe0a4027e398a34f8172698fb1da6e1e610b55889a"}};  /* the frame update: clears the busy sets */
typedef uint64_t (*nmesh_generate_fn)(void *,uint64_t);
typedef uint64_t (*nmesh_regenerate_fn)(void *,uint64_t,void *);
typedef unsigned char (*nmesh_busy_fn)(void *,uint64_t);
typedef unsigned char *(*nmesh_tls_fn)(void);
typedef struct {uint64_t tick,id,us,time;uint8_t kind,joined;} nmesh_row;
static int nmesh_mode,nmesh_installed,nmesh_restored,nmesh_lock_ready;
static unsigned nmesh_done;
static unsigned char *nmesh_stubs;
static CRITICAL_SECTION nmesh_lock;
static HANDLE nmesh_open;                 /* manual reset; signaled = builds may run */
static int nmesh_closed,nmesh_busy_now,nmesh_held,nmesh_deferred,nmesh_joining,nmesh_gate;
static uint64_t nmesh_held_id,nmesh_join_tick,nmesh_join_seq,nmesh_deferred_seq;   /* every join opens a new sequence */
static nmesh_busy_fn nmesh_held_busy;     /* the busy test the held build uses (X4's, or a test's) */
static double nmesh_time;                 /* the join's game time, the builder's TLS time inside it */
static uint64_t nmesh_join_ids[NMESH_JOIN_MAX];static uint8_t nmesh_join_kinds[NMESH_JOIN_MAX];static unsigned nmesh_join_count;
static nmesh_row nmesh_ring[NMESH_RING];
static volatile LONG64 nmesh_rows,nmesh_failures;
static uint64_t nmesh_generated,nmesh_regenerated,nmesh_join_builds,nmesh_free_builds,nmesh_waited,nmesh_deferrals,nmesh_held_deferred,
                nmesh_joins,nmesh_join_passes,nmesh_join_us,nmesh_max_join_us,nmesh_build_us,nmesh_max_build_us,nmesh_last_pending,
                nmesh_max_pending,nmesh_tls_set,nmesh_tls_inits,nmesh_tls_missing,nmesh_receipt_builds,nmesh_unkeyed,nmesh_gated,
                nmesh_pre_gate,nmesh_released;
static uint64_t nmesh_receipt=0xcbf29ce484222325ull;
/* Hot join (lockstep_join.h NMESH): every build term the receipt folded, sorted, each once (every client), and a joiner's
 * catch-up: the session's terms, each left out of the fold once at the joiner's joins up to nmesh_skip_until. A joiner's load
 * builds the helpers the session built long before (j1-141: the session's sixteen at ticks 1..3 and 366, the joiner's twelve
 * of them at 634), so its receipt started from the FNV basis and folded them; with the session's receipt adopted and those
 * builds left out it reads as the session's. A later build folds on both (j1-141: 176b36 at 1144 on both). */
static uint64_t *nmesh_known,*nmesh_skip;static unsigned char *nmesh_skip_used;
static uint32_t nmesh_nknown,nmesh_known_cap,nmesh_nskip;static uint64_t nmesh_skip_until,nmesh_skipped;
static const char *nmesh_stage="idle";
static unsigned char *nmesh_tls_native(void);
static nmesh_tls_fn nmesh_tls=nmesh_tls_native;   /* a test's own block */
static int nmesh_fail(const char *why) {nmesh_stage=why;InterlockedIncrement64(&nmesh_failures);return 0;}
static uint64_t nmesh_fnv(uint64_t hash,uint64_t value) {for(unsigned i=0;i<8;i++) {hash^=(value>>(8*i))&0xffu;hash*=0x100000001b3ull;}return hash;}
static int64_t nmesh_term_find(const uint64_t *a,uint32_t n,uint64_t v) {
    uint32_t lo=0,hi=n;
    while(lo<hi) {uint32_t mid=lo+(hi-lo)/2u;if(a[mid]<v) lo=mid+1u;else hi=mid;}
    return lo<n && a[lo]==v?(int64_t)lo:-1;
}
static void nmesh_known_add(uint64_t term) {
    uint32_t lo=0,hi=nmesh_nknown;
    while(lo<hi) {uint32_t mid=lo+(hi-lo)/2u;if(nmesh_known[mid]<term) lo=mid+1u;else hi=mid;}
    if(lo<nmesh_nknown && nmesh_known[lo]==term) return;
    if(nmesh_nknown==nmesh_known_cap) {
        uint32_t cap=nmesh_known_cap?nmesh_known_cap*2u:64u;uint64_t *grown=realloc(nmesh_known,(size_t)cap*sizeof(*grown));
        if(!grown) return;
        nmesh_known=grown;nmesh_known_cap=cap;
    }
    memmove(nmesh_known+lo+1,nmesh_known+lo,(size_t)(nmesh_nknown-lo)*sizeof(*nmesh_known));nmesh_known[lo]=term;++nmesh_nknown;
}
static void nmesh_join_forget(void) {
    free(nmesh_known);free(nmesh_skip);free(nmesh_skip_used);nmesh_known=nmesh_skip=NULL;nmesh_skip_used=NULL;
    nmesh_nknown=nmesh_known_cap=nmesh_nskip=0;nmesh_skip_until=nmesh_skipped=0;
}
static uint64_t nmesh_us(LARGE_INTEGER from,LARGE_INTEGER to,LARGE_INTEGER frequency) {
    return frequency.QuadPart && to.QuadPart>=from.QuadPart?(uint64_t)((to.QuadPart-from.QuadPart)*1000000/frequency.QuadPart):0;
}
/* The builder thread's TLS block (slot 0), made on demand as X4's own code makes it. */
static unsigned char *nmesh_tls_native(void) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    if(!slots || !slots[0]) return NULL;
    unsigned char *block=slots[0];
    if(!block[0x10]) {((void(*)(void))(void *)(image+NMESH_TLS_INIT))();++nmesh_tls_inits;}
    return block[0x10]?block:NULL;
}
/* IDs waiting in the four sets and the rebuild list (-1 unreadable). */
static int64_t nmesh_pending(const unsigned char *cache) {
    uint64_t total=0;
    for(unsigned i=0;i<5;i++) {uint64_t n=0;if(!cache || !read_local(cache+nmesh_count_offsets[i],&n,8)) return -1;total+=n;}
    return (int64_t)total;
}
/* ---- the builder thread's side ---- */
/* The cache's shutdown cleared its run flag: it waits for this thread, which must not wait for a join. */
static int nmesh_stopping(const unsigned char *cache) {return !cache || !*(volatile const unsigned char *)(cache+NMESH_RUN_FLAG);}
/* Under the lock: let the thread go for good (the session cannot go on deterministically). */
static void nmesh_release_locked(void) {
    ++nmesh_released;nmesh_closed=0;nmesh_joining=0;if(nmesh_open) SetEvent(nmesh_open);nmesh_fail("shutdown");
}
/* Under the lock, 'sync' only: the thread came round to its wait, or the load was requested before it did; from now
 * on it takes and builds only inside joins. A failed module stays open. */
static void nmesh_gate_close_locked(int how) {
    if(nmesh_gate!=NMESH_GATE_WAIT) return;
    nmesh_gate=how;
    if(!InterlockedCompareExchange64(&nmesh_failures,0,0) && !nmesh_joining) {nmesh_closed=1;if(nmesh_open) ResetEvent(nmesh_open);}
}
/* Under the lock: true while a take or build before the gate closed may run natively (the menu, before the load). */
static int nmesh_pre_gate_locked(void) {
    if(nmesh_mode!=NMESH_SYNC || nmesh_gate!=NMESH_GATE_WAIT) return 0;
    if(boot_mode && !boot_phase && !stats.issued) return 1;
    nmesh_gate_close_locked(NMESH_GATE_FORCED);return 0;
}
/* Under the lock: wait while the builds are closed, or until the cache shuts down; the counter counts a take or build
 * once however many joins it waits for. */
static void nmesh_wait_closed_locked(const unsigned char *cache,uint64_t *counter,int *counted) {
    while(nmesh_mode==NMESH_SYNC && nmesh_closed) {
        if(nmesh_stopping(cache)) {nmesh_release_locked();break;}
        if(!*counted) {++*counter;*counted=1;}
        LeaveCriticalSection(&nmesh_lock);WaitForSingleObject(nmesh_open,NMESH_POLL_MS);EnterCriticalSection(&nmesh_lock);
    }
}
/* The thread's wait on its semaphore (X4's call at 0x10878d9 through our pointer cell): its first arrival closes the
 * gate; after the wait it takes nothing while the builds are closed. */
static DWORD nmesh_wait_on(const unsigned char *cache,HANDLE semaphore,DWORD ms) {
    if(nmesh_lock_ready) {
        EnterCriticalSection(&nmesh_lock);
        if(nmesh_mode==NMESH_SYNC) nmesh_gate_close_locked(NMESH_GATE_ARRIVED);
        LeaveCriticalSection(&nmesh_lock);
    }
    DWORD result=WaitForSingleObject(semaphore,ms);
    if(result==WAIT_OBJECT_0 && nmesh_lock_ready) {
        int counted=0;EnterCriticalSection(&nmesh_lock);
        if(!nmesh_pre_gate_locked()) nmesh_wait_closed_locked(cache,&nmesh_gated,&counted);
        LeaveCriticalSection(&nmesh_lock);
    }
    return result;
}
static DWORD WINAPI nmesh_wait(HANDLE semaphore,DWORD ms) {
    return nmesh_wait_on(*(unsigned char *volatile *)(image+NMESH_CACHE),semaphore,ms);
}
static uint64_t nmesh_original(unsigned kind,void *cache,uint64_t id,void *params,void *original) {
    return kind==NMESH_K_GENERATE?((nmesh_generate_fn)original)(cache,id):((nmesh_regenerate_fn)original)(cache,id,params);
}
static uint64_t nmesh_run(unsigned kind,void *cache,uint64_t id,void *params,void *original,nmesh_busy_fn busy) {
    if(!nmesh_lock_ready) return nmesh_original(kind,cache,id,params,original);
    EnterCriticalSection(&nmesh_lock);
    if(nmesh_pre_gate_locked()) {++nmesh_pre_gate;LeaveCriticalSection(&nmesh_lock);return nmesh_original(kind,cache,id,params,original);}
    nmesh_held=1;nmesh_held_id=id;nmesh_held_busy=busy;nmesh_deferred=0;
    int waited=0;
    for(;;) {
        nmesh_wait_closed_locked(cache,&nmesh_waited,&waited);
        /* Open (a join, or for good): an ID in the busy sets would Sleep(10) inside X4's build until a UniverseFrameUpdate
         * clears it; in 'sync' it waits for a later join instead (the join does not wait for it). */
        if(nmesh_mode!=NMESH_SYNC || !busy || !nmesh_joining || !busy(cache,id)) break;
        uint64_t seq=nmesh_join_seq;nmesh_deferred=1;nmesh_deferred_seq=seq;++nmesh_deferrals;
        /* until this join closes or the next one opens (that one tries again: a deferral counts only in its own join) or
         * the builds are opened for good (stop, failure) */
        while(nmesh_mode==NMESH_SYNC && !nmesh_closed && nmesh_joining && nmesh_join_seq==seq) {
            LeaveCriticalSection(&nmesh_lock);Sleep(1);EnterCriticalSection(&nmesh_lock);
        }
        nmesh_deferred=0;
    }
    int joined=nmesh_mode==NMESH_SYNC && nmesh_joining;double time=nmesh_time;
    uint64_t tick=joined?nmesh_join_tick:*(volatile uint64_t*)&stats.issued;
    nmesh_busy_now=1;
    LeaveCriticalSection(&nmesh_lock);
    unsigned char *tls=nmesh_tls?nmesh_tls():NULL;double saved=0,seen=0;
    if(tls) memcpy(&saved,tls+NMESH_TLS_TIME,8);
    if(joined && tls) memcpy(tls+NMESH_TLS_TIME,&time,8);
    if(tls) memcpy(&seen,tls+NMESH_TLS_TIME,8);
    LARGE_INTEGER t0,t1,frequency;QueryPerformanceCounter(&t0);
    uint64_t result=nmesh_original(kind,cache,id,params,original);
    QueryPerformanceCounter(&t1);QueryPerformanceFrequency(&frequency);uint64_t us=nmesh_us(t0,t1,frequency);
    if(joined && tls) memcpy(tls+NMESH_TLS_TIME,&saved,8);
    EnterCriticalSection(&nmesh_lock);
    nmesh_busy_now=0;nmesh_held=0;nmesh_deferred=0;
    if(kind==NMESH_K_GENERATE) ++nmesh_generated;else ++nmesh_regenerated;
    nmesh_build_us+=us;if(us>nmesh_max_build_us) nmesh_max_build_us=us;
    if(joined) {
        ++nmesh_join_builds;if(tls) ++nmesh_tls_set;else ++nmesh_tls_missing;
        if(nmesh_join_count>=NMESH_JOIN_MAX) nmesh_fail("join_full");
        else {nmesh_join_ids[nmesh_join_count]=id;nmesh_join_kinds[nmesh_join_count++]=(uint8_t)kind;}
    } else ++nmesh_free_builds;
    uint64_t bits;memcpy(&bits,&seen,8);
    LONG64 at=InterlockedIncrement64(&nmesh_rows)-1;
    nmesh_ring[(uint64_t)at%NMESH_RING]=(nmesh_row){tick,id,us,tls?bits:0,(uint8_t)kind,(uint8_t)joined};
    LeaveCriticalSection(&nmesh_lock);
    return result;
}
static uint64_t nmesh_generate(void *cache,uint64_t id) {
    return nmesh_run(NMESH_K_GENERATE,cache,id,NULL,(void *)(image+NMESH_GENERATE),(nmesh_busy_fn)(void *)(image+NMESH_BUSY));
}
static uint64_t nmesh_regenerate(void *cache,uint64_t id,void *params) {
    return nmesh_run(NMESH_K_REGENERATE,cache,id,params,(void *)(image+NMESH_REGENERATE),(nmesh_busy_fn)(void *)(image+NMESH_BUSY));
}
/* ---- the main thread's join (MoveWorker joined, before the frame's tick report) ---- */
static int nmesh_join_with(unsigned char *cache,uint64_t tick,double now,unsigned timeout_ms) {
    if(!nmesh_installed || !nmesh_lock_ready || nmesh_mode!=NMESH_SYNC) return 1;
    if(InterlockedCompareExchange64(&nmesh_failures,0,0)) return 0;
    LARGE_INTEGER t0,t,frequency;QueryPerformanceCounter(&t0);QueryPerformanceFrequency(&frequency);
    EnterCriticalSection(&nmesh_lock);
    nmesh_gate_close_locked(NMESH_GATE_FORCED);   /* a join is never before the gate */
    nmesh_time=now;nmesh_join_tick=tick;nmesh_join_count=0;++nmesh_join_seq;nmesh_joining=1;nmesh_closed=0;SetEvent(nmesh_open);
    LeaveCriticalSection(&nmesh_lock);
    uint64_t passes=0,us=0;
    for(;;) {
        EnterCriticalSection(&nmesh_lock);
        int64_t pending=nmesh_pending(cache);
        if(pending<0) {nmesh_joining=0;LeaveCriticalSection(&nmesh_lock);return nmesh_fail("counts");}   /* left open */
        if(!passes) {nmesh_last_pending=(uint64_t)pending;if((uint64_t)pending>nmesh_max_pending) nmesh_max_pending=(uint64_t)pending;}
        /* A running build whose ID is busy sleeps in X4's build until a UniverseFrameUpdate this join holds back. */
        if(nmesh_busy_now && nmesh_held && nmesh_held_busy && nmesh_held_busy(cache,nmesh_held_id)) {
            nmesh_joining=0;LeaveCriticalSection(&nmesh_lock);return nmesh_fail("build_busy");   /* left open */
        }
        int idle=!nmesh_busy_now && (nmesh_held?nmesh_deferred && nmesh_deferred_seq==nmesh_join_seq:pending==0);
        if(idle) {if(nmesh_held) ++nmesh_held_deferred;nmesh_joining=0;nmesh_closed=1;ResetEvent(nmesh_open);}
        LeaveCriticalSection(&nmesh_lock);
        QueryPerformanceCounter(&t);us=nmesh_us(t0,t,frequency);
        if(idle) break;
        if(us>(uint64_t)timeout_ms*1000) {
            EnterCriticalSection(&nmesh_lock);nmesh_joining=0;LeaveCriticalSection(&nmesh_lock);   /* left open */
            return nmesh_fail("join_timeout");
        }
        Sleep(passes++<64?0:1);
    }
    if(InterlockedCompareExchange64(&nmesh_failures,0,0)) return 0;   /* join_full, shutdown */
    /* The builds of this join, closed: their portable keys, folded order-independently. */
    if(nmesh_join_count) {
        uint64_t sum=0;unsigned folded=0;
        for(unsigned i=0;i<nmesh_join_count;i++) {
            unsigned klass=WORLD_KEY_NONE;uint64_t key=world_identity_key(nmesh_join_ids[i],&klass);
            if(klass==WORLD_KEY_NONE) ++nmesh_unkeyed;
            const uint64_t term=nmesh_fnv(nmesh_fnv(nmesh_fnv(0xcbf29ce484222325ull,key),klass),nmesh_join_kinds[i]);
            nmesh_known_add(term);
            if(tick<=nmesh_skip_until) {   /* a hot joiner's catch-up build of a helper the session had (lockstep_join.h NMESH) */
                const int64_t k=nmesh_term_find(nmesh_skip,nmesh_nskip,term);
                if(k>=0 && !nmesh_skip_used[k]) {nmesh_skip_used[k]=1;++nmesh_skipped;continue;}
            }
            sum+=term;++folded;
        }
        if(folded) {nmesh_receipt=nmesh_fnv(nmesh_fnv(nmesh_fnv(nmesh_receipt,tick),folded),sum);nmesh_receipt_builds+=folded;}
    }
    nmesh_join_passes+=passes;++nmesh_joins;nmesh_join_us+=us;if(us>nmesh_max_join_us) nmesh_max_join_us=us;
    return 1;
}
static int nmesh_join(uint64_t tick,unsigned timeout_ms) {
    unsigned char *cache=NULL;double now=0;
    if(!nmesh_installed || nmesh_mode!=NMESH_SYNC) return 1;
    if(!read_local(image+NMESH_CACHE,&cache,8) || !cache) return nmesh_fail("cache");
    if(!pnat_time_tls(&now)) return nmesh_fail("time");
    return nmesh_join_with(cache,tick,now,timeout_ms);
}
/* Builds and takes run freely (stop, failure, restore). */
static void nmesh_open_builds(void) {
    if(!nmesh_lock_ready) return;
    EnterCriticalSection(&nmesh_lock);nmesh_closed=0;nmesh_joining=0;if(nmesh_open) SetEvent(nmesh_open);LeaveCriticalSection(&nmesh_lock);
}
/* ---- sites, stubs, pins ---- */
/* A build site's stub is a jump to our function; the wait site's is the pointer cell its import call reads. */
static unsigned char *nmesh_stub(unsigned i) {return nmesh_stubs+0x20u*i;}
static int32_t nmesh_rel_to(unsigned i,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+nmesh_sites[i].rva+nmesh_sites[i].len));}
/* The site's bytes as X4 has them (call or import call, its displacement to the function or the import's slot). */
static void nmesh_site_original(unsigned i,unsigned char *out) {
    int32_t d=nmesh_rel_to(i,image+nmesh_sites[i].target);
    if(nmesh_sites[i].len==5) out[0]=0xe8;else {out[0]=0xff;out[1]=0x15;}
    memcpy(out+nmesh_sites[i].len-4,&d,4);
}
/* 0 original, 1 ours (its stub), -1 anything else. */
static int nmesh_site_state(unsigned i) {
    unsigned len=nmesh_sites[i].len;unsigned char now[6];int32_t d;
    if(!read_local(image+nmesh_sites[i].rva,now,len) || (len==5?now[0]!=0xe8:(now[0]!=0xff || now[1]!=0x15))) return -1;
    memcpy(&d,now+len-4,4);
    if(d==nmesh_rel_to(i,image+nmesh_sites[i].target)) return 0;
    return nmesh_stubs && d==nmesh_rel_to(i,nmesh_stub(i))?1:-1;
}
static int nmesh_pins_ok(void) {
    static unsigned char copy[0xd00];
    for(unsigned p=0;p<sizeof(nmesh_pins)/sizeof(*nmesh_pins);p++) {
        if(nmesh_pins[p].size>sizeof(copy) || !read_local(image+nmesh_pins[p].rva,copy,nmesh_pins[p].size)) return 0;
        for(unsigned i=0;i<NMESH_SITES;i++) {
            unsigned at=nmesh_sites[i].rva;if(at<nmesh_pins[p].rva || at+nmesh_sites[i].len>nmesh_pins[p].rva+nmesh_pins[p].size) continue;
            if(nmesh_site_state(i)<0) return 0;
            nmesh_site_original(i,copy+(at-nmesh_pins[p].rva));
        }
        if(!hash_ok(copy,nmesh_pins[p].size,nmesh_pins[p].sha)) return 0;
    }
    return 1;
}
static unsigned char *nmesh_alloc_near(unsigned size) {
    uintptr_t at=(uintptr_t)(image+nmesh_sites[0].rva)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int fits=1;
            for(unsigned i=0;fits && i<NMESH_SITES;i++) {
                intptr_t next=(intptr_t)(image+nmesh_sites[i].rva+nmesh_sites[i].len);
                fits=(intptr_t)p-next>=INT32_MIN && (intptr_t)(p+size)-next<=INT32_MAX;
            }
            if(fits) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void *nmesh_target(unsigned i) {return i==0?(void *)nmesh_generate:i==1?(void *)nmesh_regenerate:(void *)nmesh_wait;}
static int nmesh_write_stubs(void) {
    DWORD old,ignored;if(!VirtualProtect(nmesh_stubs,NMESH_STUB_SIZE,PAGE_READWRITE,&old)) return 0;
    for(unsigned i=0;i<NMESH_SITES;i++) {
        static const unsigned char jump[6]={0xff,0x25,0,0,0,0};void *target=nmesh_target(i);
        if(i==NMESH_WAIT_SITE) memcpy(nmesh_stub(i),&target,8);
        else {memcpy(nmesh_stub(i),jump,6);memcpy(nmesh_stub(i)+6,&target,8);}
    }
    int ok=VirtualProtect(nmesh_stubs,NMESH_STUB_SIZE,PAGE_EXECUTE_READ,&ignored)!=0;
    FlushInstructionCache(GetCurrentProcess(),nmesh_stubs,NMESH_STUB_SIZE);return ok;
}
static int nmesh_stubs_owned(void) {
    for(unsigned i=0;i<NMESH_SITES;i++) {
        void *t=NULL;
        if(i==NMESH_WAIT_SITE) {if(!read_local(nmesh_stub(i),&t,8) || t!=nmesh_target(i)) return 0;continue;}
        if(!read_local(nmesh_stub(i)+6,&t,8) || t!=nmesh_target(i) || nmesh_stub(i)[0]!=0xff || nmesh_stub(i)[1]!=0x25) return 0;
    }
    return 1;
}
/* The displacement only: one 16-byte block for the wait site (0x10878db..0x10878de), as before for the build sites. */
static int nmesh_cas(unsigned i,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(nmesh_sites[i].rva+nmesh_sites[i].len-4,e,v,4);
}
static int nmesh_unpatch(void) {
    for(unsigned i=0;i<NMESH_SITES;i++) if(nmesh_done&(1u<<i)) {
        int state=nmesh_site_state(i);
        if(state==1 && nmesh_cas(i,nmesh_rel_to(i,nmesh_stub(i)),nmesh_rel_to(i,image+nmesh_sites[i].target))) nmesh_done&=~(1u<<i);
        else if(state==0) nmesh_done&=~(1u<<i);
    }
    return !nmesh_done;
}
static void nmesh_reset_counters(void) {
    nmesh_closed=nmesh_busy_now=nmesh_held=nmesh_deferred=nmesh_joining=0;nmesh_gate=NMESH_GATE_WAIT;nmesh_held_id=nmesh_join_tick=nmesh_join_seq=nmesh_deferred_seq=0;nmesh_held_busy=NULL;
    nmesh_time=0;nmesh_join_count=0;nmesh_rows=nmesh_failures=0;
    nmesh_generated=nmesh_regenerated=nmesh_join_builds=nmesh_free_builds=nmesh_waited=nmesh_deferrals=nmesh_held_deferred=0;
    nmesh_joins=nmesh_join_passes=nmesh_join_us=nmesh_max_join_us=nmesh_build_us=nmesh_max_build_us=nmesh_last_pending=nmesh_max_pending=0;
    nmesh_tls_set=nmesh_tls_inits=nmesh_tls_missing=nmesh_receipt_builds=nmesh_unkeyed=nmesh_gated=nmesh_pre_gate=nmesh_released=0;
    nmesh_receipt=0xcbf29ce484222325ull;nmesh_restored=0;nmesh_join_forget();
}
/* Under the lock: the mode's first state. 'sync' starts open with the gate waiting for the thread's arrival (or the
 * load's request); 'observe' never closes. */
static void nmesh_begin_locked(int mode) {
    nmesh_mode=mode;nmesh_closed=0;nmesh_gate=mode==NMESH_SYNC?NMESH_GATE_WAIT:NMESH_GATE_ARRIVED;if(nmesh_open) SetEvent(nmesh_open);
}
/* Before the first admitted frame, on the MoveWorker (the probe's arming). */
static int nmesh_arm(int mode) {
    if(nmesh_installed) return stats.active && nmesh_mode==mode;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || (mode!=NMESH_OBSERVE && mode!=NMESH_SYNC)) return 0;
    for(unsigned i=0;i<NMESH_SITES;i++) if(nmesh_site_state(i)!=0) {nmesh_stage="site";return 0;}
    if(!nmesh_pins_ok()) {nmesh_stage="pins";return 0;}
    unsigned char *cache=NULL;HANDLE semaphore=NULL;
    if(!read_local(image+NMESH_CACHE,&cache,8) || !cache || nmesh_pending(cache)<0 || !read_local(cache+NMESH_SEMAPHORE,&semaphore,8) ||
       !semaphore || nmesh_stopping(cache)) {nmesh_stage="cache";return 0;}
    if(!nmesh_stubs) nmesh_stubs=nmesh_alloc_near(NMESH_STUB_SIZE);
    if(!nmesh_stubs) {nmesh_stage="memory";return 0;}
    if(!nmesh_write_stubs()) {nmesh_stage="stubs";return 0;}
    if(!nmesh_open) nmesh_open=CreateEventW(NULL,TRUE,TRUE,NULL);
    if(!nmesh_open) {nmesh_stage="event";return 0;}
    if(!nmesh_lock_ready) {InitializeCriticalSection(&nmesh_lock);nmesh_lock_ready=1;}
    EnterCriticalSection(&nmesh_lock);nmesh_reset_counters();nmesh_begin_locked(mode);LeaveCriticalSection(&nmesh_lock);
    nmesh_tls=nmesh_tls_native;
    for(unsigned i=0;i<NMESH_SITES;i++) {
        if(!nmesh_cas(i,nmesh_rel_to(i,image+nmesh_sites[i].target),nmesh_rel_to(i,nmesh_stub(i)))) {
            nmesh_unpatch();nmesh_mode=NMESH_OFF;nmesh_stage="cas";return 0;
        }
        nmesh_done|=1u<<i;
    }
    nmesh_installed=1;nmesh_stage="armed";
    /* An idle thread sits in X4's own wait (called before the patch): one empty pass brings it round to ours. */
    if(mode==NMESH_SYNC) ReleaseSemaphore(semaphore,1,NULL);
    return 1;
}
static int nmesh_owned(void) {
    if(!nmesh_installed) return 1;
    for(unsigned i=0;i<NMESH_SITES;i++) if((nmesh_done&(1u<<i)) && nmesh_site_state(i)!=1) return 0;
    return nmesh_stubs_owned();
}
/* Main-thread tick check; any failure stops the gate with reason 186. */
static int nmesh_check(void) {
    if(InterlockedCompareExchange64(&nmesh_failures,0,0)) {nmesh_open_builds();return 0;}
    const char *why=!nmesh_owned()?"site_changed":!nmesh_pins_ok()?"block_changed":NULL;
    if(why) {nmesh_open_builds();return nmesh_fail(why);}
    return 1;
}
static void nmesh_restore(void) {
    if(!nmesh_installed) return;
    nmesh_open_builds();
    if(nmesh_unpatch()) {nmesh_installed=0;nmesh_restored=1;}
}
static const char *nmesh_mode_name(void) {return nmesh_mode==NMESH_SYNC?"sync":nmesh_mode==NMESH_OBSERVE?"observe":"off";}
static const char *nmesh_gate_name(void) {return nmesh_gate==NMESH_GATE_ARRIVED?"arrived":nmesh_gate==NMESH_GATE_FORCED?"forced":"wait";}
/* Hot join: the receipt, its build count and the folded terms ('sync' only; the saver's package). */
static int nmesh_join_state(uint64_t *receipt,uint64_t *builds,const uint64_t **terms,uint32_t *n) {
    if(!nmesh_installed || nmesh_mode!=NMESH_SYNC) return 0;
    *receipt=nmesh_receipt;*builds=nmesh_receipt_builds;*terms=nmesh_known;*n=nmesh_nknown;return 1;
}
/* A joiner's admission: the session's receipt and count, its terms left out once each at the joins up to until. */
static int nmesh_join_adopt(uint64_t receipt,uint64_t builds,const uint64_t *terms,uint32_t n,uint64_t until) {
    if(!nmesh_installed || nmesh_mode!=NMESH_SYNC) return 0;
    uint64_t *skip=n?malloc((size_t)n*sizeof(*skip)):NULL;unsigned char *used=n?calloc(n,1):NULL;
    if(n && (!skip || !used)) {free(skip);free(used);return 0;}
    if(n) memcpy(skip,terms,(size_t)n*sizeof(*skip));
    free(nmesh_skip);free(nmesh_skip_used);nmesh_skip=skip;nmesh_skip_used=used;nmesh_nskip=n;nmesh_skip_until=until;nmesh_skipped=0;
    for(uint32_t i=0;i<n;i++) nmesh_known_add(terms[i]);
    nmesh_receipt=receipt;nmesh_receipt_builds=builds;return 1;
}
/* The receipt for the declared state ('sync'). */
static int nmesh_frame_text(char *out,size_t cap) {
    if(!nmesh_installed || nmesh_mode!=NMESH_SYNC) return 0;
    int n=snprintf(out,cap,"NMESH_1:%llu:%016llx",(unsigned long long)nmesh_receipt_builds,(unsigned long long)nmesh_receipt);
    return n>0 && (size_t)n<cap?n:0;
}
/* On the MoveWorker: the object's portable key is resolved here. */
static int nmesh_row_text(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&nmesh_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)NMESH_RING && index<(uint64_t)(count-(LONG64)NMESH_RING)) return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    nmesh_row r;
    if(nmesh_lock_ready) EnterCriticalSection(&nmesh_lock);
    r=nmesh_ring[index%NMESH_RING];
    if(nmesh_lock_ready) LeaveCriticalSection(&nmesh_lock);
    unsigned klass=WORLD_KEY_NONE;uint64_t key=world_identity_key(r.id,&klass);
    n=snprintf(out,cap,"index=%llu tick=%llu kind=%s key=%016llx klass=%u joined=%u time=%016llx us=%llu",(unsigned long long)index,
               (unsigned long long)r.tick,r.kind==NMESH_K_GENERATE?"generate":"regenerate",(unsigned long long)key,klass,r.joined,
               (unsigned long long)r.time,(unsigned long long)r.us);
    return n>0 && (size_t)n<cap?n:0;
}
static int nmesh_status(char *out,size_t cap) {
    if(nmesh_lock_ready) EnterCriticalSection(&nmesh_lock);
    int n=snprintf(out,cap,"mode=%s installed=%d frame=%llu generated=%llu regenerated=%llu join_builds=%llu free_builds=%llu waited=%llu "
        "gate=%s gated=%llu pre_gate=%llu deferrals=%llu held_deferred=%llu held=%d closed=%d joins=%llu join_passes=%llu join_us=%llu "
        "max_join_us=%llu build_us=%llu max_build_us=%llu last_pending=%llu max_pending=%llu tls_set=%llu tls_inits=%llu tls_missing=%llu "
        "receipt=%llu/%016llx unkeyed=%llu released=%llu rows=%lld restored=%d stage=%s failures=%lld",nmesh_mode_name(),nmesh_installed,
        (unsigned long long)stats.issued,(unsigned long long)nmesh_generated,(unsigned long long)nmesh_regenerated,
        (unsigned long long)nmesh_join_builds,(unsigned long long)nmesh_free_builds,(unsigned long long)nmesh_waited,nmesh_gate_name(),
        (unsigned long long)nmesh_gated,(unsigned long long)nmesh_pre_gate,(unsigned long long)nmesh_deferrals,
        (unsigned long long)nmesh_held_deferred,nmesh_held,nmesh_closed,(unsigned long long)nmesh_joins,(unsigned long long)nmesh_join_passes,
        (unsigned long long)nmesh_join_us,(unsigned long long)nmesh_max_join_us,(unsigned long long)nmesh_build_us,
        (unsigned long long)nmesh_max_build_us,(unsigned long long)nmesh_last_pending,(unsigned long long)nmesh_max_pending,
        (unsigned long long)nmesh_tls_set,(unsigned long long)nmesh_tls_inits,(unsigned long long)nmesh_tls_missing,
        (unsigned long long)nmesh_receipt_builds,(unsigned long long)nmesh_receipt,(unsigned long long)nmesh_unkeyed,
        (unsigned long long)nmesh_released,(long long)nmesh_rows,nmesh_restored,nmesh_stage,(long long)nmesh_failures);
    if(nmesh_lock_ready) LeaveCriticalSection(&nmesh_lock);
    return n>0 && (size_t)n<cap?n:0;
}

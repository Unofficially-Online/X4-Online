/* Hot join, DEFAULT OFF: the joiner's saved objects at the session's runtime IDs (native_gate_join_ids, joiner only;
 * needs native_gate_runtime_ids set mode and the created identities hook).
 *
 * Why. X4 writes runtime IDs into a save (id="[0x107e7]") and the loader gives every object a new one:
 * UniverseClass::Import makes the component first (U::Component's constructor takes the map's head, AssignNewID
 * 0xcc220) and only then reads its id attribute (0x79f720 maps the save's ID to the new one, for the save's
 * references). The session's clients agree because they load the same snapshot, in the same order, from the same
 * head (lockstep_runtime_ids.h); a joiner loads the join save in its own order, so every saved object sits at
 * another ID than on the session, and whatever is seeded, hashed or ordered by +8 parts. j1-114: both sides place
 * a waiting job ship in step 648, each the other one; the Split faction logic draws 77 times on the session and 81
 * on the joiner, and the job's request names its ship as 0x107e7 there (its save ID) and 0x10cad here.
 *
 * What. From join_load to the admission the joiner's shared allocations go two ways:
 *  - The one a saved object's constructor makes takes that object's own slot. Two calls of the import go through
 *    thunk slots that read a node's id attribute (0xccff0, key 0x1fe) into a pending ID of their thread: the call
 *    right before the class switch (0x8e320f, rcx the component's node) and the read of a connection's name
 *    (0x8e573b, rcx the connection's node). The next AssignNewID of the kind it names, from U::Component's
 *    constructors (returning to 0x3d2431 or 0x3d1dd1) or from the connection maker 0x997210 (to 0x9972f3), takes
 *    that slot through InsertAtSlot 0xcc500 if the slot is unused, of the ID's generation, below the session's
 *    next slot and on the shared list; otherwise it takes the load list (refused). The connection's get-or-make
 *    0x3d87e0 (its call 0x8e5764, a third slot) and the ID import drop a pending ID that nothing took.
 *  - Everything else takes the load list: block 29 of the map, the shared list's limit (RID_BLOCK_LIMIT), so no
 *    slot a saved object needs, nor any the session hands out later.
 * Before the load the shared list gets every block up to the session's next slot (EnsureFreeSlots, as the
 * session's load and play appended them; they are fresh, generation 0, as on the session, where set mode never
 * hands a slot out twice). At the admission the head skips to the session's next slot (join_admit_rid) over the
 * run the load left, holes and all; that run stays unused. Then the saved objects hold the session's IDs, every
 * allocation from the admission on gets the session's next one, and what only the joiner's load made sits in
 * block 29, whose frees go back on its own list (rid_local_of). The ID import counts each saved object, equal (its
 * ID is the save's) or other. JOIN_STATUS ids=stage,nodes/connections/no_id,placed/refused,equal/other,load
 * allocations/free,appended blocks,skipped. */
#define JOIN_IDS_NODE_SITE 0x8e320fu    /* UniverseClass::Import: the attribute lookup right before the class switch */
#define JOIN_IDS_NODE_ENTRY 0x7a9b50u
#define JOIN_IDS_NODE_THUNK 144u
#define JOIN_IDS_CONN_SITE 0x8e573bu    /* a connection node's name (attribute 0xe4) */
#define JOIN_IDS_CONN_THUNK 160u
#define JOIN_IDS_MADE_SITE 0x8e5764u    /* the connection's get-or-make */
#define JOIN_IDS_MADE_ENTRY 0x3d87e0u
#define JOIN_IDS_MADE_THUNK 176u
#define JOIN_IDS_ATTRIBUTE 0xccff0u     /* (node, key, fallback) -> const std::string *, the value or the fallback */
#define JOIN_IDS_KEY 0x1feu             /* id */
#define JOIN_IDS_EMPTY 0x31c0f60u       /* the empty string the import passes as the fallback */
#define JOIN_IDS_INSERT 0xcc500u        /* InsertAtSlot(map, object, slot, {bucket, entry}) -> generation << 25 | slot */
#define JOIN_IDS_ENSURE 0xcc670u        /* EnsureFreeSlots(map) */
#define JOIN_IDS_COMPONENT_A 0x3d2431u  /* AssignNewID returns here: U::Component's two constructors */
#define JOIN_IDS_COMPONENT_B 0x3d1dd1u
#define JOIN_IDS_CONNECTION 0x9972f3u   /* and here: the connection 0x997210 makes at an index */
enum {JOIN_IDS_NONE,JOIN_IDS_COMPONENT,JOIN_IDS_CONN};
typedef struct {uint64_t saved;unsigned kind;} join_ids_pending_t;
typedef uint64_t (*join_ids_insert_fn)(void *map,void *object,uint32_t slot,void *ref);
typedef unsigned char (*join_ids_ensure_fn)(void *map);
typedef const void *(*join_ids_attribute_fn)(void *node,uint64_t key,const void *fallback);
typedef uint64_t (*join_ids_node_fn)(void *,uint64_t,uint64_t,uint64_t);
typedef void *(*join_ids_made_fn)(void *,void *,void *,void *);
static __thread join_ids_pending_t join_ids_pending;
static __thread uint64_t join_ids_placing;
static int join_ids_on;   /* native_gate_join_ids */
static uint32_t join_ids_want;
static const char *join_ids_stage="idle";
static volatile LONG64 join_ids_nodes,join_ids_conns,join_ids_noid,join_ids_placed,join_ids_refused,join_ids_equal,join_ids_other,
                       join_ids_appended,join_ids_skipped;
static join_ids_insert_fn join_ids_insert_native;          /* tests: replace the engine's */
static join_ids_ensure_fn join_ids_ensure_native;
static join_ids_attribute_fn join_ids_attribute_native;
static int join_ids_fail(const char *why) {join_ids_stage=why;return 0;}

/* Under the map's critical section: the object into its saved slot, if that slot is free for it; 0 leaves the map as it was. */
static uint64_t join_ids_place(unsigned char *map,void *object,uint64_t saved) {
    const uint32_t slot=(uint32_t)saved&0x1ffffffu,generation=(uint32_t)(saved>>25);
    if(!slot || saved>>41 || slot<rid_base || slot>=join_ids_want) return 0;
    unsigned entry;unsigned char *bucket=rid_bucket(map,slot,&entry);
    if(!bucket || rid_slot_used(map,slot) || rid_slot_generation(map,slot)!=generation) return 0;
    const uint64_t word=*rid_slot(map,slot);const uint32_t prev=(uint32_t)word,next=(uint32_t)(word>>32);
    const volatile uint64_t *p=prev?rid_slot(map,prev):NULL,*q=next?rid_slot(map,next):NULL;
    if(prev?(!p || rid_slot_used(map,prev) || (uint32_t)(*p>>32)!=slot):*(volatile uint32_t *)(map+RID_F_HEAD)!=slot) return 0;
    if(next?(!q || rid_slot_used(map,next) || (uint32_t)*q!=slot):*(volatile uint32_t *)(map+RID_F_TAIL)!=slot) return 0;
    struct {unsigned char *bucket;uint32_t entry,pad;} ref={bucket,entry,0};
    const uint64_t id=join_ids_insert_native?join_ids_insert_native(map,object,slot,&ref):
        ((join_ids_insert_fn)(void *)(image+JOIN_IDS_INSERT))(map,object,slot,&ref);
    memcpy((unsigned char *)object+8,&id,8);   /* as AssignNewID */
    return id;
}
static uint64_t join_ids_assign_load(void *object) {return rid_local_assign(&join_ids_list,object);}
static uint64_t join_ids_assign_pinned(void *object) {
    unsigned char *map=rid_map();const uint64_t saved=join_ids_placing;join_ids_placing=0;
    if(map) {
        CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(map+0x10);EnterCriticalSection(lock);
        const uint64_t id=join_ids_place(map,object,saved);
        LeaveCriticalSection(lock);
        if(id) {InterlockedIncrement64(&join_ids_placed);join_rw_load_hit(object,id);return id;}   /* JOIN_REFWATCH in the load */
    }
    InterlockedIncrement64(&join_ids_refused);return join_ids_assign_load(object);
}
/* Replay (W3, the lockstep session's pilot layer): objects the join save leaves out (the slots' virtual players) are rebuilt on
 * a joiner after its admission with the session's IDs, holes below the head here. join_ids_replay(ids, n) arms the calling
 * thread's next n shared allocations to take those slots in order (gen << 25 | slot each, every one below the head, unused and
 * of its generation; a private list swapped in for the call as the local lists are, so AssignNewID pops them natively; a hole
 * below the head is on no list in set mode, its links stale). join_ids_replay_end() disarms: 1 only if exactly n were placed, each
 * the one it names, and nothing else of that thread's shared allocations came between. Other threads allocate as before. */
#define JOIN_IDS_REPLAY_MAX 256u
static rid_local_t join_ids_replay_list;
static volatile LONG join_ids_replay_on;
static DWORD join_ids_replay_thread;
static uint64_t join_ids_replay_ids[JOIN_IDS_REPLAY_MAX],join_ids_replay_got[JOIN_IDS_REPLAY_MAX];   /* named; taken (at +8) */
static unsigned join_ids_replay_n,join_ids_replay_done,join_ids_replay_other,join_ids_replay_wrong;
static volatile LONG64 join_ids_replays,join_ids_replay_failures;
static uint64_t join_ids_assign_replay(void *object) {
    unsigned char *map=rid_map();const created_assign_fn original=rid_assign_original();
    if(!map) return original(object);
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(map+0x10);EnterCriticalSection(lock);
    uint64_t result;
    if(join_ids_replay_list.free && join_ids_replay_list.head) {
        rid_fields_t shared;rid_local_in(map,&join_ids_replay_list,&shared);
        result=original(object);
        rid_local_out(map,&join_ids_replay_list,&shared);
        /* The ID is the one AssignNewID stored at +8: the native returns no ID in rax (w1u: wrong=2 from that alone). */
        uint64_t id=0;(void)read_local((unsigned char *)object+8,&id,8);
        if(join_ids_replay_done<JOIN_IDS_REPLAY_MAX) join_ids_replay_got[join_ids_replay_done]=id;
        if(join_ids_replay_done<join_ids_replay_n && id!=join_ids_replay_ids[join_ids_replay_done]) ++join_ids_replay_wrong;
        ++join_ids_replay_done;
    } else {result=original(object);++join_ids_replay_other;}   /* past the n: the shared head, counted against the replay */
    LeaveCriticalSection(lock);return result;
}
__attribute__((unused)) static int join_ids_replay(const uint64_t *ids,unsigned n) {
    if(InterlockedCompareExchange(&join_ids_replay_on,0,0) || !ids || !n || n>JOIN_IDS_REPLAY_MAX || rid_mode!=RID_SET) return 0;
    unsigned char *map=rid_map();if(!map) return 0;
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(map+0x10);EnterCriticalSection(lock);
    const uint32_t head=*(volatile uint32_t *)(map+RID_F_HEAD);int ok=head!=0;
    for(unsigned i=0;i<n && ok;i++) {
        const uint32_t slot=(uint32_t)ids[i]&0x1ffffffu;
        ok=slot && !(ids[i]>>41) && slot<head && rid_slot(map,slot) && !rid_slot_used(map,slot) && rid_slot_generation(map,slot)==(uint32_t)(ids[i]>>25);
        for(unsigned j=0;j<i && ok;j++) ok=((uint32_t)ids[j]&0x1ffffffu)!=slot;
    }
    if(ok) {
        for(unsigned i=0;i<n;i++) {
            const uint32_t prev=i?(uint32_t)ids[i-1]&0x1ffffffu:0u,next=i+1<n?(uint32_t)ids[i+1]&0x1ffffffu:0u;
            *rid_slot(map,(uint32_t)ids[i]&0x1ffffffu)=(uint64_t)prev|((uint64_t)next<<32);
            join_ids_replay_ids[i]=ids[i];
        }
        memset(&join_ids_replay_list,0,sizeof(join_ids_replay_list));
        join_ids_replay_list.head=(uint32_t)ids[0]&0x1ffffffu;join_ids_replay_list.tail=(uint32_t)ids[n-1]&0x1ffffffu;join_ids_replay_list.free=n;
        join_ids_replay_n=n;join_ids_replay_done=join_ids_replay_other=join_ids_replay_wrong=0;join_ids_replay_thread=GetCurrentThreadId();
        InterlockedExchange(&join_ids_replay_on,1);InterlockedIncrement64(&join_ids_replays);
    } else InterlockedIncrement64(&join_ids_replay_failures);
    LeaveCriticalSection(lock);return ok;
}
__attribute__((unused)) static int join_ids_replay_end(void) {
    if(!InterlockedExchange(&join_ids_replay_on,0)) return 0;
    const int ok=join_ids_replay_done==join_ids_replay_n && !join_ids_replay_other && !join_ids_replay_wrong;
    if(!ok) InterlockedIncrement64(&join_ids_replay_failures);
    memset(&join_ids_replay_list,0,sizeof(join_ids_replay_list));join_ids_replay_thread=0;return ok;
}
/* rid_assign, a shared allocation: as before, while the joiner loads its saved slot or the load list, or a replay's slot. */
static created_assign_fn join_ids_route(created_assign_fn shared,uint32_t caller) {
    if(InterlockedCompareExchange(&join_ids_replay_on,0,0) && GetCurrentThreadId()==join_ids_replay_thread) return join_ids_assign_replay;
    if(join_ids_admit_load && join_ids_built) return join_ids_assign_load;   /* ATTN states at the admission (lockstep_join_world.h) */
    if(!InterlockedCompareExchange(&join_ids_active,0,0)) return shared;
    const unsigned kind=caller==JOIN_IDS_COMPONENT_A || caller==JOIN_IDS_COMPONENT_B?JOIN_IDS_COMPONENT:caller==JOIN_IDS_CONNECTION?JOIN_IDS_CONN:JOIN_IDS_NONE;
    if(kind && join_ids_pending.kind==kind) {join_ids_placing=join_ids_pending.saved;join_ids_pending.kind=JOIN_IDS_NONE;return join_ids_assign_pinned;}
    return join_ids_assign_load;
}
/* A node about to be made into a saved object: its id attribute is this thread's pending ID. */
static void join_ids_note(void *node,unsigned kind) {
    InterlockedIncrement64(kind==JOIN_IDS_COMPONENT?&join_ids_nodes:&join_ids_conns);
    const void *fallback=image+JOIN_IDS_EMPTY;
    const void *text=join_ids_attribute_native?join_ids_attribute_native(node,JOIN_IDS_KEY,fallback):
        ((join_ids_attribute_fn)(void *)(image+JOIN_IDS_ATTRIBUTE))(node,JOIN_IDS_KEY,fallback);
    uint64_t saved=0;
    if(text && world_identity_parse((void *)text,&saved)) {join_ids_pending.saved=saved;join_ids_pending.kind=kind;}
    else {join_ids_pending.kind=JOIN_IDS_NONE;InterlockedIncrement64(&join_ids_noid);}
}
static __attribute__((noinline)) uint64_t join_ids_node_hook(void *node,uint64_t key,uint64_t a,uint64_t b) {
    const uint64_t result=((join_ids_node_fn)(void *)(image+JOIN_IDS_NODE_ENTRY))(node,key,a,b);
    if(InterlockedCompareExchange(&join_ids_active,0,0)) {const DWORD error=GetLastError();join_ids_note(node,JOIN_IDS_COMPONENT);SetLastError(error);}
    return result;
}
static __attribute__((noinline)) const void *join_ids_conn_hook(void *node,uint64_t key,const void *fallback) {
    const void *result=((join_ids_attribute_fn)(void *)(image+JOIN_IDS_ATTRIBUTE))(node,key,fallback);
    if(InterlockedCompareExchange(&join_ids_active,0,0)) {const DWORD error=GetLastError();join_ids_note(node,JOIN_IDS_CONN);SetLastError(error);}
    return result;
}
static __attribute__((noinline)) void *join_ids_made_hook(void *a,void *b,void *c,void *d) {
    void *result=((join_ids_made_fn)(void *)(image+JOIN_IDS_MADE_ENTRY))(a,b,c,d);
    join_ids_pending.kind=JOIN_IDS_NONE;return result;
}
/* The ID import of a saved object (world_identity_observe): its ID against the save's; whatever is still pending is dropped. */
static void join_ids_imported(void *object,uint64_t saved) {
    if(!InterlockedCompareExchange(&join_ids_active,0,0)) return;
    join_ids_pending.kind=JOIN_IDS_NONE;uint64_t runtime=0;
    InterlockedIncrement64(read_local((unsigned char *)object+8,&runtime,8) && runtime==saved?&join_ids_equal:&join_ids_other);
}
/* Under the critical section, at the admission: the head to the session's next slot over the run the load left. 0 with why set. */
static int join_ids_skip(unsigned char *map,uint32_t want,const char **why) {
    const rid_counts_t c=rid_counts(map);uint32_t at=c.head,n=0;
    while(at && at!=want) {
        const volatile uint64_t *p=rid_slot(map,at);
        if(at>want || n>=c.free || !p || rid_slot_used(map,at)) {*why="ids_run";return 0;}
        at=(uint32_t)(*p>>32);++n;
    }
    volatile uint64_t *first=rid_slot(map,want);
    if(at!=want || !first || rid_slot_used(map,want) || rid_slot_generation(map,want) || c.free<n+1u) {*why="ids_target";return 0;}
    *first&=0xffffffff00000000ULL;*(volatile uint32_t *)(map+RID_F_HEAD)=want;*(volatile uint32_t *)(map+RID_F_FREE)=c.free-n;
    join_ids_skipped=n;return 1;
}
/* Under the critical section, before the load: the shared list's blocks up to the session's next slot (EnsureFreeSlots with
 * the free count below its threshold, one block a call), then the load list in block 29 (block: zeroed, RID_BLOCK_BYTES). */
static int join_ids_prepare(unsigned char *map,uint32_t want,void *block) {
    if(*(void *volatile *)(map+RID_F_BLOCKS+RID_JOIN_BLOCK*8u)) return join_ids_fail("block");
    while(*(volatile uint32_t *)(map+RID_F_TAIL)<want) {
        const uint32_t free=*(volatile uint32_t *)(map+RID_F_FREE),tail=*(volatile uint32_t *)(map+RID_F_TAIL);
        *(volatile uint32_t *)(map+RID_F_FREE)=0;
        const int appended=join_ids_ensure_native?join_ids_ensure_native(map)!=0:((join_ids_ensure_fn)(void *)(image+JOIN_IDS_ENSURE))(map)!=0;
        const int ok=appended && *(volatile uint32_t *)(map+RID_F_TAIL)==tail+0x100000u && *(volatile uint32_t *)(map+RID_F_FREE)==0x100000u;
        *(volatile uint32_t *)(map+RID_F_FREE)=free+(ok?0x100000u:0u);
        if(!ok) return join_ids_fail("append");
        InterlockedIncrement64(&join_ids_appended);
    }
    rid_local_link(map,&join_ids_list,RID_JOIN_BLOCK,block);join_ids_built=1;return 1;
}
#ifndef JOIN_OWN_PLATFORM
static const uint32_t join_ids_sites[3]={JOIN_IDS_NODE_SITE,JOIN_IDS_CONN_SITE,JOIN_IDS_MADE_SITE};
static const uint32_t join_ids_entries[3]={JOIN_IDS_NODE_ENTRY,JOIN_IDS_ATTRIBUTE,JOIN_IDS_MADE_ENTRY};
static const unsigned join_ids_thunks[3]={JOIN_IDS_NODE_THUNK,JOIN_IDS_CONN_THUNK,JOIN_IDS_MADE_THUNK};
static int32_t join_ids_original[3],join_ids_patched[3];
static unsigned char join_ids_taken[3];
static int join_ids_call_is(uint32_t site,uint32_t target) {
    int32_t rel;return image[site]==0xe8 && (memcpy(&rel,image+site+1,4),(uint32_t)((int64_t)site+5+rel)==target);
}
static int join_ids_pins(void) {
    return hash_ok(image+0x8e31fd,51,"64be49ff23405847ee437020a54898b8819c6ad6f4246b969fa9564dda859fc3") &&   /* the class switch's head */
           hash_ok(image+0x8e566a,32,"0b261b64082f66243f6de359e3109cba4dd2a25a75e15a9995ce35a591a74649") &&   /* the component's ID import */
           hash_ok(image+0x8e5728,104,"568e08e78ddb78a51d081aa50958affe8ed2a1d4c735e0897004871542adaaab") &&  /* the connection's */
           hash_ok(image+JOIN_IDS_NODE_ENTRY,64,"6998bde5b7bc54ff63a794b7be9a27614ef582b767169c5324769d15f2459e80") &&
           hash_ok(image+JOIN_IDS_ATTRIBUTE,64,"8465f9fb097ed4733d06851e4d26d3075c3611a352d9277840c87db4d9dececd") &&
           join_ids_call_is(JOIN_IDS_COMPONENT_A-5u,0xcc220u) && join_ids_call_is(JOIN_IDS_COMPONENT_B-5u,0xcc220u) &&
           join_ids_call_is(JOIN_IDS_CONNECTION-5u,0xcc220u);
}
static void join_ids_restore(void) {
    InterlockedExchange(&join_ids_active,0);
    for(unsigned i=0;i<3;i++) if(join_ids_taken[i] && join_precise_rel(join_ids_sites[i],join_ids_patched[i],join_ids_original[i],0)) join_ids_taken[i]=0;
}
/* join_load (UI thread, before LoadGame, after a set rid_arm): the session's next slot from the package. 1 armed or off. */
static int join_ids_arm(uint64_t next) {
    if(!join_ids_on || InterlockedCompareExchange(&join_ids_active,0,0)) return 1;
    const uint32_t want=(uint32_t)next&0x1ffffffu;
    if(rid_mode!=RID_SET || !InterlockedCompareExchange(&rid_active,0,0) || rid_sealed) return join_ids_fail("rid");
    if(next>>25 || want<=rid_base || ((want-1u)>>20)>=RID_JOIN_BLOCK-1u) return join_ids_fail("next");
    if(!created_installed || !world_identity_installed) return join_ids_fail("hooks");
    for(unsigned i=0;i<3;i++) if(!thunk || !thunk_slot_ready(join_ids_thunks[i])) return join_ids_fail("thunk");
    if(!join_ids_built && !join_ids_pins()) return join_ids_fail("pins");
    unsigned char *map=rid_map();if(!map) return join_ids_fail("map");
    if(!join_ids_built) {
        if(*(void *volatile *)(map+RID_F_BLOCKS+RID_JOIN_BLOCK*8u)) return join_ids_fail("block");
        void *block=rid_allocate(RID_BLOCK_BYTES);if(!block) return join_ids_fail("allocate");
        memset(block,0,RID_BLOCK_BYTES);
        CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(map+0x10);EnterCriticalSection(lock);
        const int ok=join_ids_prepare(map,want,block);
        LeaveCriticalSection(lock);
        if(!ok) return 0;
    }
    for(unsigned i=0;i<3;i++)
        if(!join_precise_take(&join_ids_sites[i],1,join_ids_entries[i],join_ids_thunks[i],&join_ids_original[i],&join_ids_patched[i],&join_ids_taken[i],0))
            {join_ids_restore();return join_ids_fail("site");}
    join_ids_want=want;join_ids_stage="armed";InterlockedExchange(&join_ids_active,1);return 1;
}
#else
static int join_ids_arm(uint64_t next) {(void)next;return !join_ids_on;}   /* the Linux gate: its own sites (not yet) */
static void join_ids_restore(void) {}
#endif
static int join_ids_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity,"%s,%lld/%lld/%lld,%lld/%lld,%lld/%lld,%llu/%u,%lld,%lld",join_ids_stage,(long long)join_ids_nodes,
        (long long)join_ids_conns,(long long)join_ids_noid,(long long)join_ids_placed,(long long)join_ids_refused,(long long)join_ids_equal,
        (long long)join_ids_other,(unsigned long long)join_ids_list.allocs,join_ids_list.free,(long long)join_ids_appended,(long long)join_ids_skipped);
    return n>0 && (size_t)n<capacity?n:0;
}

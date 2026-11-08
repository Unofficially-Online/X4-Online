/* find_* candidate lists on saved IDs, DEFAULT OFF
 * (native_gate_find_order = 'observe' | 'sort').
 *
 * Census big-BW (tick 63) and W600a (tick 3): FactionSubgoal_Recon.Evaluate
 * runs under the same script key on both clients but makes different numbers
 * of draws (48/54; 34/32 and 30/31); in big-BW the next tick sends a different
 * ship on recon (6f93b vs 6f8fb) and motion diverges from tick 120. Evaluate
 * calls find_ship_by_true_owner (multiple, per space of $FindShipSpaces) and
 * shuffle_list on the result (ShuffleListAction 0xbe63d0: Fisher-Yates on the
 * TLS PCG, N-1 draws for N entries), then commandeers the first entries. Same
 * seed, other draw count: some candidate list had another length; another
 * pick with the same lengths would need another order. W600a's size changes
 * go both ways and leave every pick equal; big-BW's changes the pick.
 *
 * Native path (X4 9.00): FindComponentAction (vtable 0x2c0f3c8; the ctor
 * 0xb87510 maps find_ship 7/8, find_ship_by_true_owner 9, find_station 10,
 * find_station_by_true_owner 11, ...) Execute 0xb87a30 builds a vector
 * {begin,end,cap} of Component* with 0xc5c3c0: per space of space= it walks
 * the sub-space index (space+0xa8, 0x5c20c0/0x480ab0) and per zone the owner
 * bucket of zone+0xb8 (0x3b9590 -> 0x48c970 -> 0x4a77d0/0x49acc0: pointer
 * vectors with holes, chained per key). 0xc5f680 filters (distance), then the
 * call at 0xb87ae3 to 0xd2ab50 stores the whole list (multiple) or one element
 * picked with the TLS PCG. Nothing orders candidates by a portable key: the
 * order is container insertion history, and nothing removes a component seen
 * twice. Which of order or membership differs between the clients is not yet
 * established; the rows below decide it.
 *
 * Hook: that E8 rel32 (4-byte CAS inside one cache line) goes through
 * THUNK_JUMP 4080 to find_order_assign. 'sort' rewrites the vector in place:
 * a repeated pointer keeps its first position only; components with a saved
 * UniverseID (world identity map) come first in saved-ID order, components
 * created after the load follow in their native relative order (counted
 * unmapped: not yet portable). With native_gate_portable_created a component
 * with a created identity sorts between them, by created ID (counted created;
 * it enters set/native hashes and ids like a saved ID), so only components
 * without any portable identity keep their native relative order: order
 * (class, ID) of lockstep_world_identity.h. 'observe' computes the same without writing.
 * The wrapper then tail-jumps (musttail) to 0xd2ab50, so the native return
 * address stays 0xb87ae8, which the world values observer checks.
 * Rows (FIND_ORDER_ROW): tick, the enclosing script RNG owner key and name
 * (portable; 0 outside a keyed scope), a per-(tick, scope) ordinal, raw and
 * duplicate counts, unmapped count, an order-free hash of the saved IDs, the
 * hash of the native saved-ID sequence and up to 32 saved IDs in sorted order.
 * Failures (foreign return address, unreadable vector or component, oversize
 * list, two components with one saved ID) stop the gate with reason 92; stop
 * restores the call.
 *
 * Second site, the range finds (reports/LOCKSTEP-ON-FOOT-20261101.txt 15j;
 * session-defaults run d1s3: GM_BoardShip at tick 1 and GM_SupplyFactory at
 * tick 7 pick their target cluster from find_cluster_in_range object=
 * player.entity maxdistance=2 + shuffle_list under equal script keys and draw
 * counts, yet client 1 tries other clusters first; the two offers' sectors
 * are where the census parts at 1800). FindSpaceInRangeAction (vtable
 * 0x2c0f660, find_cluster_in_range 0x750 / find_sector_in_range) Execute
 * 0xb8a890 collects the spaces within the jump range with 0x3bb200 (clusters)
 * or 0x78cc30 (sectors) into a red-black tree on its stack KEYED BY THE
 * Component* VALUE (insert 0x3135a0 / 0x345700: unsigned pointer compare),
 * erases the ones its match conditions refuse, and at 0xb8af09 calls 0xc602e0
 * (declaration, tree, exec), whose only store 0xd2ada0 walks the tree in order:
 * appends every space (multiple) or takes the Random::U64(count)-th (single).
 * The result is in address order of the space objects: a per-process
 * property (the load's allocation interleaving), which keyed draws cannot
 * repair. The tree (Execute's rbp-0x79): +0x20 first (leftmost) node, +0x28
 * count, the in-order walk ends at the tree object itself; node +0 parent, +8
 * left, +0x10 right, +0x20 Component*, +0x28 int jumps. 'sort' rewrites the
 * nodes' (Component*, jumps) pairs along the in-order walk into the same
 * saved-ID order as above (the tree's key order is broken on purpose: after
 * this call nothing searches it, Execute only frees its node blocks).
 * Hook: the E8 rel32 at 0xb8af09 (one cache line) through THUNK_JUMP 48 to
 * find_order_space_assign, which tail-jumps to 0xc602e0. Pins: Execute,
 * 0xc602e0 and 0xd2ada0 whole. Rows carry site=space (site=component for the
 * first site); the status counts both and the range finds apart (space_*).
 * The Linux gate inlines the store into Execute; linux/gate_find_order.h hooks its two entries. */
/* The Linux gate (linux/gate_find_order.h) defines these for its executable, and its own hook, pins and swap. */
#ifndef FIND_ORDER_OWN_PLATFORM
#define FIND_ORDER_EXECUTE 0xb87a30u /* FindComponentAction::Execute */
#define FIND_ORDER_SITE 0xb87ae3u    /* E8 -> FIND_ORDER_ASSIGN */
#define FIND_ORDER_ASSIGN 0xd2ab50u  /* result store: (declaration, vector*, exec, component) */
#define FIND_ORDER_THUNK 4080u
#define FIND_ORDER_SPACE_EXECUTE 0xb8a890u /* FindSpaceInRangeAction::Execute, 0x6f3 bytes */
#define FIND_ORDER_SPACE_SITE 0xb8af09u    /* E8 -> FIND_ORDER_SPACE_ASSIGN */
#define FIND_ORDER_SPACE_ASSIGN 0xc602e0u  /* result store from the range tree: (declaration, tree, exec), 0x21b bytes */
#define FIND_ORDER_SPACE_STORE 0xd2ada0u   /* its in-order walk of the tree (only caller 0xc604d8), 0x315 bytes */
#define FIND_ORDER_SPACE_THUNK 48u
#endif
#define FIND_ORDER_SPACE_MAX 4096u /* nodes of one range tree (X4 9.00: a few hundred sectors) */
#define FIND_ORDER_SPACE_DEPTH 128u /* steps of one in-order successor (red-black height of 4096 nodes <= 24) */
enum {FIND_ORDER_SITE_COMPONENT=0,FIND_ORDER_SITE_SPACE=1};
#define FIND_ORDER_MAX 16384u
#define FIND_ORDER_SCOPES 1024u
#define FIND_ORDER_RING 4096u
#define FIND_ORDER_IDS 32u
enum {FIND_ORDER_OFF=0,FIND_ORDER_OBSERVE=1,FIND_ORDER_SORT=2};
typedef uintptr_t (*find_assign_fn)(void *,void *,void *,void *);
typedef uint64_t (*find_order_lookup_fn)(uint64_t,unsigned *); /* runtime -> portable key and WORLD_KEY_* class */
typedef struct {uint64_t saved;uintptr_t pointer;uint32_t index,klass;} find_order_entry; /* saved: portable key, 0 none */
typedef struct {uint64_t tick,scope;uint32_t count,used;} find_order_scope_t;
typedef struct {
    uint64_t tick,scope,ordinal,set,native,ids[FIND_ORDER_IDS];
    uint32_t raw,duplicates,unmapped,count,moved,shown;uint8_t mode,site;char name[64];
} find_order_row_t;
static volatile LONG find_order_active;
static int find_order_armed,find_order_installed,find_order_restored,find_order_mode,find_order_space_installed;
static int32_t find_order_original_rel,find_order_patched_rel;
#ifdef FIND_ORDER_SPACE_SITE
static int32_t find_order_space_original_rel,find_order_space_patched_rel;
#endif
static uintptr_t find_order_space_nodes[FIND_ORDER_SPACE_MAX];
static int32_t find_order_space_jumps[FIND_ORDER_SPACE_MAX];
static volatile LONG64 find_order_space_calls,find_order_space_candidates,find_order_space_reordered;
static find_order_entry find_order_work[FIND_ORDER_MAX];
static uintptr_t find_order_raw[FIND_ORDER_MAX];
static struct {uintptr_t pointer;uint32_t stamp;} find_order_seen[2*FIND_ORDER_MAX];
static uint32_t find_order_stamp;
static find_order_scope_t find_order_scopes[FIND_ORDER_SCOPES];
static uint64_t find_order_scopes_tick=UINT64_MAX;
static find_order_row_t find_order_ring[FIND_ORDER_RING];
static volatile LONG64 find_order_calls,find_order_applied,find_order_observed,find_order_passed,find_order_threads,find_order_empty,
    find_order_candidates,find_order_duplicates,find_order_unmapped,find_order_reordered,find_order_collisions,find_order_oversize,
    find_order_unreadable,find_order_foreign,find_order_cursor,find_order_failures,find_order_created;

static int find_order_entry_less(const find_order_entry *a,const find_order_entry *b) {
    if(a->klass!=b->klass) return a->klass<b->klass; /* saved, then created, then none */
    if(a->klass!=WORLD_KEY_NONE && a->saved!=b->saved) return a->saved<b->saved;
    return a->index<b->index; /* unmapped: native relative order */
}
static int find_order_compare(const void *x,const void *y) {
    const find_order_entry *a=x,*b=y;return find_order_entry_less(a,b)?-1:find_order_entry_less(b,a)?1:0;
}
/* Per-(tick, scope) ordinal; 0 when the bounded table is exhausted. */
static uint64_t find_order_ordinal(uint64_t tick,uint64_t scope) {
    if(find_order_scopes_tick!=tick) {memset(find_order_scopes,0,sizeof(find_order_scopes));find_order_scopes_tick=tick;}
    unsigned first=(unsigned)(lockstep_mix(scope^lockstep_mix(tick))&(FIND_ORDER_SCOPES-1));
    for(unsigned n=0;n<64;n++) {
        find_order_scope_t *s=&find_order_scopes[(first+n)&(FIND_ORDER_SCOPES-1)];
        if(!s->used) {s->used=1;s->tick=tick;s->scope=scope;s->count=1;return 1;}
        if(s->scope==scope) return ++s->count;
    }
    return 0;
}
/* First occurrence of a pointer in this call; the table is cleared lazily by stamp. */
static int find_order_first(uintptr_t pointer,unsigned mask) {
    unsigned at=(unsigned)(lockstep_mix(pointer)&mask);
    for(unsigned n=0;n<=mask;n++) {
        unsigned i=(at+n)&mask;
        if(find_order_seen[i].stamp!=find_order_stamp) {find_order_seen[i].stamp=find_order_stamp;find_order_seen[i].pointer=pointer;return 1;}
        if(find_order_seen[i].pointer==pointer) return 0;
    }
    return 0;
}
static void find_order_record(uint64_t tick,uint64_t scope,const char *name,uint64_t ordinal,uint64_t set,uint64_t native,uint32_t raw,
                              uint32_t duplicates,uint32_t unmapped,uint32_t count,uint32_t moved,unsigned mode,unsigned site) {
    LONG64 index=InterlockedIncrement64(&find_order_cursor)-1;find_order_row_t *r=&find_order_ring[(uint64_t)index%FIND_ORDER_RING];
    memset(r,0,sizeof(*r));r->tick=tick;r->scope=scope;r->ordinal=ordinal;r->set=set;r->native=native;r->raw=raw;r->duplicates=duplicates;
    r->unmapped=unmapped;r->count=count;r->moved=moved;r->mode=(uint8_t)mode;r->site=(uint8_t)site;
    r->shown=count<FIND_ORDER_IDS?count:FIND_ORDER_IDS;for(unsigned i=0;i<r->shown;i++) r->ids[i]=find_order_work[i].saved;
    if(name) {size_t n=strlen(name);if(n>=sizeof(r->name)) n=sizeof(r->name)-1;memcpy(r->name,name,n);}
}
typedef struct {uint32_t count,duplicates,unmapped,created,moved;uint64_t set,native;} find_order_measure_t;
static void find_order_fail(volatile LONG64 *counter) {InterlockedIncrement64(counter);InterlockedIncrement64(&find_order_failures);}
/* The shared core of both sites: find_order_raw[0..raw) -> find_order_work in
 * canonical order (a repeated pointer keeps its first position), the portable
 * keys' order-free set, the native sequence hash and the moved count. 0 on a
 * failure, counted (unreadable component, two components with one key). */
static int find_order_measure(uint32_t raw,int (*reader)(const void *,void *,size_t),find_order_lookup_fn lookup,find_order_measure_t *m) {
    unsigned mask=1;while(mask<2*raw) mask<<=1;mask-=1;
    if(!++find_order_stamp) {memset(find_order_seen,0,sizeof(find_order_seen));find_order_stamp=1;}
    uint32_t count=0,duplicates=0,unmapped=0,created=0;uint64_t set=0,native=0x58344c46494e444fULL; /* "X4LFINDO" */
    for(uint32_t i=0;i<raw;i++) {
        uintptr_t p=find_order_raw[i];
        if(!find_order_first(p,mask)) {++duplicates;continue;}
        uint64_t runtime=0,saved=0;unsigned klass=WORLD_KEY_NONE;
        if(p && (!reader((unsigned char*)p+8,&runtime,8))) {find_order_fail(&find_order_unreadable);return 0;}
        if(p && runtime) saved=lookup(runtime,&klass);
        if(!saved || klass>WORLD_KEY_NONE) klass=WORLD_KEY_NONE;
        find_order_entry *e=&find_order_work[count];e->saved=saved;e->pointer=p;e->index=count;e->klass=klass;
        if(!saved) ++unmapped;else {set+=lockstep_mix(saved);created+=klass==WORLD_KEY_CREATED;}
        native=lockstep_mix(native^saved);++count;
    }
    set+=(uint64_t)unmapped*0x9e3779b97f4a7c15ULL;
    if(count>1) {
        if(count<=32) {
            for(uint32_t i=1;i<count;i++) {
                find_order_entry v=find_order_work[i];uint32_t j=i;
                while(j && find_order_entry_less(&v,&find_order_work[j-1])) {find_order_work[j]=find_order_work[j-1];--j;}
                find_order_work[j]=v;
            }
        } else qsort(find_order_work,count,sizeof(*find_order_work),find_order_compare);
    }
    uint32_t moved=0;
    for(uint32_t i=0;i<count;i++) {
        if(find_order_work[i].index!=i) ++moved;
        if(i && find_order_work[i].klass!=WORLD_KEY_NONE && find_order_work[i].klass==find_order_work[i-1].klass &&
           find_order_work[i].saved==find_order_work[i-1].saved) {find_order_fail(&find_order_collisions);return 0;}
    }
    m->count=count;m->duplicates=duplicates;m->unmapped=unmapped;m->created=created;m->moved=moved;m->set=set;m->native=native;
    return 1;
}
/* Counters and the row of one measured call (after its write, if any). */
static void find_order_finish(uint32_t raw,const find_order_measure_t *m,unsigned site) {
    if(!raw) InterlockedIncrement64(&find_order_empty);
    find_order_candidates+=raw;find_order_duplicates+=m->duplicates;find_order_unmapped+=m->unmapped;find_order_created+=m->created;
    if(m->moved || m->duplicates) ++find_order_reordered;
    if(site==FIND_ORDER_SITE_SPACE) {find_order_space_candidates+=raw;if(m->moved) ++find_order_space_reordered;}
    if(find_order_mode==FIND_ORDER_SORT) InterlockedIncrement64(&find_order_applied);else InterlockedIncrement64(&find_order_observed);
    const script_rng_frame *f=script_rng_current,*o=f?f->owner:NULL;
    uint64_t scope=o?o->key:0,tick=stats.issued,ordinal=find_order_ordinal(tick,scope);
    find_order_record(tick,scope,o?o->id.name:(f?f->id.name:NULL),ordinal,m->set,m->native,raw,m->duplicates,m->unmapped,m->count,m->moved,
                      (unsigned)find_order_mode,site);
}
/* Admission shared by both sites: 1 to measure, 0 failed or passed through (counted). */
static int find_order_admit(uintptr_t back,uintptr_t expected) {
    if(back!=expected) {find_order_fail(&find_order_foreign);return 0;}
    if(!InterlockedCompareExchange(&find_order_active,0,0) || !stats.active || find_order_mode==FIND_ORDER_OFF) {InterlockedIncrement64(&find_order_passed);return 0;}
    if(GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&find_order_threads);return 0;}
    return 1;
}
/* Canonicalizes (sort) or measures (observe) one candidate vector. reader
 * reads foreign memory; lookup maps a runtime ID to its portable key and class
 * (world_identity_key: the saved ID, a created ID with portable keys on, else 0). */
static void find_order_process(uintptr_t back,void *vector,int (*reader)(const void *,void *,size_t),find_order_lookup_fn lookup) {
    InterlockedIncrement64(&find_order_calls);
    if(!find_order_admit(back,(uintptr_t)(image+FIND_ORDER_SITE+5))) return;
    uintptr_t head[3];
    if(!vector || !reader(vector,head,sizeof(head)) || head[1]<head[0] || head[2]<head[1] || ((head[1]-head[0])&7) || (head[0]&7)) {
        find_order_fail(&find_order_unreadable);return;
    }
    uint64_t raw=(head[1]-head[0])/8;
    if(raw>FIND_ORDER_MAX) {find_order_fail(&find_order_oversize);return;}
    if(raw && !reader((void*)head[0],find_order_raw,(size_t)raw*8)) {find_order_fail(&find_order_unreadable);return;}
    find_order_measure_t m;if(!find_order_measure((uint32_t)raw,reader,lookup,&m)) return;
    if(find_order_mode==FIND_ORDER_SORT && (m.moved || m.duplicates)) {
        int ok=0;
        __try {
            uintptr_t *out=(uintptr_t*)head[0];for(uint32_t i=0;i<m.count;i++) out[i]=find_order_work[i].pointer;
            ((uintptr_t*)vector)[1]=head[0]+(uintptr_t)m.count*8;ok=1;
        } __except(EXCEPTION_EXECUTE_HANDLER) {ok=0;}
        if(!ok) {find_order_fail(&find_order_unreadable);return;}
    }
    find_order_finish((uint32_t)raw,&m,FIND_ORDER_SITE_COMPONENT);
}
/* In-order successor in the range tree (0xd2ada0's and Execute's walk): the
 * leftmost node of the right subtree, else the first ancestor reached from its
 * left subtree; the tree object itself ends the walk (the root's parent, whose
 * +8 is the root). 0 on an unreadable or misaligned link or a walk longer than
 * FIND_ORDER_SPACE_DEPTH. */
static uintptr_t find_order_space_next(uintptr_t node,int (*reader)(const void *,void *,size_t)) {
    uintptr_t right=0,link=0;
    if(!reader((void*)(node+0x10),&right,8)) return 0;
    if(right) {
        for(unsigned n=0;n<FIND_ORDER_SPACE_DEPTH;n++) {
            if((right&7) || !reader((void*)(right+8),&link,8)) return 0;
            if(!link) return right;
            right=link;
        }
        return 0;
    }
    uintptr_t child=node,parent=0;
    for(unsigned n=0;n<FIND_ORDER_SPACE_DEPTH;n++) {
        if(!reader((void*)child,&parent,8) || !parent || (parent&7) || !reader((void*)(parent+8),&link,8)) return 0;
        if(link==child) return parent;
        child=parent;
    }
    return 0;
}
/* Canonicalizes (sort) or measures (observe) the range tree of one
 * find_cluster_in_range / find_sector_in_range before its store walks it:
 * the in-order walk gets the (Component*, jumps) pairs in saved-ID order.
 * A walk that does not end at the tree object after exactly count nodes, an
 * unreadable node or a repeated Component* (a tree's keys are unique) fails. */
static void find_order_space_process(uintptr_t back,void *tree,int (*reader)(const void *,void *,size_t),find_order_lookup_fn lookup) {
    InterlockedIncrement64(&find_order_calls);InterlockedIncrement64(&find_order_space_calls);
#ifdef FIND_ORDER_SPACE_SITE
    if(!find_order_admit(back,(uintptr_t)(image+FIND_ORDER_SPACE_SITE+5))) return;
#else
    (void)back;find_order_fail(&find_order_foreign);return; /* no second site on this platform */
#endif
    uintptr_t t=(uintptr_t)tree,node=0;uint64_t raw=0;
    if(!t || (t&7) || !reader((void*)(t+0x20),&node,8) || !reader((void*)(t+0x28),&raw,8)) {find_order_fail(&find_order_unreadable);return;}
    if(raw>FIND_ORDER_SPACE_MAX) {find_order_fail(&find_order_oversize);return;}
    for(uint32_t i=0;i<(uint32_t)raw;i++) {
        uintptr_t key=0;int32_t jumps=0;
        if(!node || node==t || (node&7) || !reader((void*)(node+0x20),&key,8) || !reader((void*)(node+0x28),&jumps,4)) {
            find_order_fail(&find_order_unreadable);return;
        }
        find_order_space_nodes[i]=node;find_order_raw[i]=key;find_order_space_jumps[i]=jumps;
        node=find_order_space_next(node,reader);
    }
    if(node!=t) {find_order_fail(&find_order_unreadable);return;}
    find_order_measure_t m;if(!find_order_measure((uint32_t)raw,reader,lookup,&m)) return;
    if(m.duplicates || m.count!=(uint32_t)raw) {find_order_fail(&find_order_unreadable);return;}
    if(find_order_mode==FIND_ORDER_SORT && m.moved) {
        int ok=0;
        __try {
            for(uint32_t i=0;i<m.count;i++) {
                unsigned char *n=(unsigned char*)find_order_space_nodes[i];int32_t jumps=find_order_space_jumps[find_order_work[i].index];
                memcpy(n+0x20,&find_order_work[i].pointer,8);memcpy(n+0x28,&jumps,4);
            }
            ok=1;
        } __except(EXCEPTION_EXECUTE_HANDLER) {ok=0;}
        if(!ok) {find_order_fail(&find_order_unreadable);return;}
    }
    find_order_finish((uint32_t)raw,&m,FIND_ORDER_SITE_SPACE);
}
#ifndef FIND_ORDER_OWN_PLATFORM
/* noinline: the return address is the native caller's (the thunk jumps); the
 * tail jump keeps it for the native store and any observer behind it. */
static __attribute__((noinline)) uintptr_t find_order_assign(void *declaration,void *vector,void *exec,void *component) {
    find_order_process((uintptr_t)__builtin_return_address(0),vector,read_local,world_identity_key);
    __attribute__((musttail)) return ((find_assign_fn)(image+FIND_ORDER_ASSIGN))(declaration,vector,exec,component);
}
/* The range finds' store takes (declaration, tree, exec); the fourth register
 * passes through untouched, as with the first site. */
static __attribute__((noinline)) uintptr_t find_order_space_assign(void *declaration,void *tree,void *exec,void *spare) {
    find_order_space_process((uintptr_t)__builtin_return_address(0),tree,read_local,world_identity_key);
    __attribute__((musttail)) return ((find_assign_fn)(image+FIND_ORDER_SPACE_ASSIGN))(declaration,tree,exec,spare);
}
/* Execute with the call site (the store's entry may carry another observer). */
static int find_order_pinned(void) {
    return hash_ok(image+FIND_ORDER_EXECUTE,275,"b0e683b728526d447edd027a564797983e766ca0ebba80981f857fa45f937a69");
}
/* The range finds' Execute (tree layout, filter, the site), the store and its
 * in-order walk, whole: the node layout and the walk are what the sort relies on. */
static int find_order_space_pinned(void) {
    return hash_ok(image+FIND_ORDER_SPACE_EXECUTE,0x6f3,"ee0fa41fbaaf6089224fd3a63ec2cb8a197993d4b8f8e62d5a49dc6fa48530b1") &&
           hash_ok(image+FIND_ORDER_SPACE_ASSIGN,0x21b,"bb0d89f37086bed1f87cdaaccb1df8be8511483fbd38eec978176d3cc296a449") &&
           hash_ok(image+FIND_ORDER_SPACE_STORE,0x315,"4537797de1b6f7d80f86f69d89cadeae6dba24dd841008cacba55f3405dc84ec");
}
/* The rel32 at site+1 sits inside one 64-byte line: one locked CAS. */
static int find_order_site_swap(unsigned site,int32_t expected,int32_t value) {
    unsigned char *rel=image+site+1;DWORD old,ignored;
    if(((uintptr_t)rel&63)>60 || !VirtualProtect(rel,4,PAGE_EXECUTE_READWRITE,&old)) return 0;
    int32_t actual=(int32_t)InterlockedCompareExchange((volatile LONG*)rel,(LONG)value,(LONG)expected);
    VirtualProtect(rel,4,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+site,5);
    return actual==expected;
}
static int find_order_swap(int32_t expected,int32_t value) {return find_order_site_swap(FIND_ORDER_SITE,expected,value);}
static int find_order_space_swap(int32_t expected,int32_t value) {return find_order_site_swap(FIND_ORDER_SPACE_SITE,expected,value);}
#endif
/* E8 at site to target; the rel32 that sends it to the thunk slot instead. */
static int find_order_site_prepare(unsigned site,unsigned target,unsigned slot,int32_t *original,int32_t *patched) {
    unsigned char call[5];int32_t rel;
    if(!read_local(image+site,call,5) || call[0]!=0xe8) return 0;
    memcpy(&rel,call+1,4);if((uintptr_t)(image+site+5)+(intptr_t)rel!=(uintptr_t)(image+target)) return 0;
    intptr_t distance=(intptr_t)(thunk+slot)-(intptr_t)(image+site+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    *original=rel;*patched=(int32_t)distance;return 1;
}
static int find_order_prepare(void) {
    return find_order_site_prepare(FIND_ORDER_SITE,FIND_ORDER_ASSIGN,FIND_ORDER_THUNK,&find_order_original_rel,&find_order_patched_rel);
}
#if defined(FIND_ORDER_SPACE_SITE) && !defined(FIND_ORDER_SPACE_OWN_SITE) /* Linux: jmp hooks into an inlined store (linux/gate_find_order.h) */
static int find_order_space_prepare(void) {
    return find_order_site_prepare(FIND_ORDER_SPACE_SITE,FIND_ORDER_SPACE_ASSIGN,FIND_ORDER_SPACE_THUNK,
                                   &find_order_space_original_rel,&find_order_space_patched_rel);
}
#endif
static int find_order_owned(void) {
    int32_t rel=0;
#ifdef FIND_ORDER_SPACE_SITE
    if(find_order_space_installed) {memcpy(&rel,image+FIND_ORDER_SPACE_SITE+1,4);if(rel!=find_order_space_patched_rel) return 0;}
#endif
    if(!find_order_installed) return 1;
    memcpy(&rel,image+FIND_ORDER_SITE+1,4);return rel==find_order_patched_rel;
}
/* Armed once at boot, before load (idempotent on a loading-UI reload with the
 * same mode). Needs the world identity map: without it every entry is unmapped.
 * Where the second site exists, both sites install or neither does. */
static int find_order_arm(int mode) {
    if(mode!=FIND_ORDER_OBSERVE && mode!=FIND_ORDER_SORT) return 0;
    if(find_order_armed) return stats.active && find_order_installed && find_order_mode==mode;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !thunk || !world_identity_installed ||
       !find_order_pinned() || !thunk_slot_ready(FIND_ORDER_THUNK) || !find_order_prepare()) return 0;
#ifdef FIND_ORDER_SPACE_SITE
    if(!find_order_space_pinned() || !thunk_slot_ready(FIND_ORDER_SPACE_THUNK) || !find_order_space_prepare()) return 0;
#endif
    if(!find_order_swap(find_order_original_rel,find_order_patched_rel)) return 0;
#ifdef FIND_ORDER_SPACE_SITE
    if(!find_order_space_swap(find_order_space_original_rel,find_order_space_patched_rel)) {
        (void)find_order_swap(find_order_patched_rel,find_order_original_rel);return 0;
    }
    find_order_space_installed=1;
#endif
    find_order_mode=mode;find_order_installed=1;find_order_armed=1;InterlockedExchange(&find_order_active,1);return 1;
}
static void find_order_restore(void) {
    if(!find_order_armed) return;
    InterlockedExchange(&find_order_active,0);
#ifdef FIND_ORDER_SPACE_SITE
    if(find_order_space_installed && find_order_space_swap(find_order_space_patched_rel,find_order_space_original_rel)) find_order_space_installed=0;
#endif
    if(find_order_installed && find_order_swap(find_order_patched_rel,find_order_original_rel)) find_order_installed=0;
    find_order_restored=!find_order_installed && !find_order_space_installed;
}
static int find_order_status(char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&find_order_cursor,0,0);
    LONG64 dropped=cursor>(LONG64)FIND_ORDER_RING?cursor-(LONG64)FIND_ORDER_RING:0;
    int n=snprintf(out,cap,"armed=%d installed=%d active=%ld mode=%d restored=%d calls=%lld applied=%lld observed=%lld passed=%lld threads=%lld"
        " empty=%lld candidates=%lld duplicates=%lld unmapped=%lld reordered=%lld collisions=%lld oversize=%lld unreadable=%lld foreign=%lld"
        " records=%lld dropped=%lld ring=%u failures=%lld created=%lld space_installed=%d space_calls=%lld space_candidates=%lld"
        " space_reordered=%lld",
        find_order_armed,find_order_installed,find_order_active,find_order_mode,find_order_restored,(long long)find_order_calls,
        (long long)find_order_applied,(long long)find_order_observed,(long long)find_order_passed,(long long)find_order_threads,
        (long long)find_order_empty,(long long)find_order_candidates,(long long)find_order_duplicates,(long long)find_order_unmapped,
        (long long)find_order_reordered,(long long)find_order_collisions,(long long)find_order_oversize,(long long)find_order_unreadable,
        (long long)find_order_foreign,(long long)cursor,(long long)dropped,FIND_ORDER_RING,(long long)find_order_failures,
        (long long)find_order_created,find_order_space_installed,(long long)find_order_space_calls,(long long)find_order_space_candidates,
        (long long)find_order_space_reordered);
    return n>0 && (size_t)n<cap?n:0;
}
/* One census row per call; evicted rows report evicted=1 so the probe keeps going. */
static int find_order_row(uint64_t index,char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&find_order_cursor,0,0);
    if((LONG64)index>=cursor) return 0;
    int n;
    if(cursor>(LONG64)FIND_ORDER_RING && index<(uint64_t)(cursor-(LONG64)FIND_ORDER_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const find_order_row_t *r=&find_order_ring[index%FIND_ORDER_RING];char name[sizeof(r->name)];
    memcpy(name,r->name,sizeof(name));name[sizeof(name)-1]=0;
    for(size_t i=0;name[i];i++) if((unsigned char)name[i]<=32 || (unsigned char)name[i]>126 || name[i]=='|' || name[i]==',') name[i]='_';
    n=snprintf(out,cap,"index=%llu tick=%llu scope=%016llx ord=%llu mode=%u raw=%u dups=%u unmapped=%u n=%u moved=%u set=%016llx native=%016llx name=%s ids=",
        (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->scope,(unsigned long long)r->ordinal,r->mode,r->raw,
        r->duplicates,r->unmapped,r->count,r->moved,(unsigned long long)r->set,(unsigned long long)r->native,name[0]?name:"-");
    if(n<=0 || (size_t)n>=cap) return 0;
    for(unsigned i=0;i<r->shown && i<FIND_ORDER_IDS;i++) {
        int k=snprintf(out+n,cap-(size_t)n,i?",%llx":"%llx",(unsigned long long)r->ids[i]);
        if(k<=0 || (size_t)k>=cap-(size_t)n) return 0;
        n+=k;
    }
    if(!r->shown) {if((size_t)n+1>=cap) return 0;out[n++]='-';out[n]=0;}
    int k=snprintf(out+n,cap-(size_t)n," site=%s",r->site==FIND_ORDER_SITE_SPACE?"space":"component");
    if(k<=0 || (size_t)k>=cap-(size_t)n) return 0;
    return n+k;
}

/* Determinism fix, a default (native_gate_island_order = true; needs native_gate_world_identities): the contacts of a physics
 * island are solved in an order every client computes alike.
 *
 * With mDeterministicSimulation (PhysicsSystem +0x2ac, the default Scene::Init keeps) the velocity solve sorts each island's
 * contact constraints first: 0x13d71b0 calls 0x1483c30 for the other constraints, then at 0x13d71dd QuickSort 0x13eb190 on
 * the island's constraint indices, the contact manager (system +0x2b8) as the comparator's capture. The comparator (0x13eb280)
 * orders by the constraint's 64-bit key +0x10, then by body 1's and body 2's BodyIDs (body +0x70). A ContactConstraint is
 * 0x360 bytes (the array at manager +0xa8): body 1 +0, body 2 +8, the key +0x10, the world normal +0x18, then friction,
 * inverse masses and inertia scales to +0x38, the point count +0x38 and up to four points of 0xc8 bytes from +0x40 (each the
 * non-penetration part first: r1 + u and r2 cross the normal). AddContactConstraint (0x13e4776 and its sibling instantiations)
 * stores the key as the FNV-1a hash of the SubShapeIDPair: BodyID 1, SubShapeID 1, BodyID 2, SubShapeID 2. BodyIDs come from
 * the body manager's free list in creation order, so a client that loaded the world (a hot joiner) holds other IDs for the
 * same bodies and solves the same contacts in another order. [hq128/hq129, 19 Nov: after a late join 61 of 71 live bodies had
 * other IDs; a docking transport's four contacts with a station's DockArea, the same on both clients (bodies, normals, depths,
 * points), were solved in another order at tick 21921, its pose 1.7 cm apart, and the census of 22200 parted in it.]
 *
 * Fix: the sort call goes through a stub that first gives every constraint of the range the FNV-1a hash of what every client
 * holds alike, then calls the original sort: each body's owner (the XPhys record, body +0x50, its owner +0; the owner's
 * portable ID and which of its two records +0x260/+0x268 the body is), the normal through the inertia scales (+0x18..+0x38),
 * the point count and each point's first 24 bytes. A body without a keyed owner adds its BodyID instead (counted unkeyed).
 * Counted: sorts, constraints, unkeyed bodies, equal keys left after the sort (ties: the comparator then falls back to the
 * BodyIDs), pointer-like words among the hashed bytes (a layout check, 0 expected) and out-of-range counts.
 * The key is the solve order's only input the stub changes; the contact cache keeps its own hashes. Armed before the first
 * frame on the UI thread, the session's clients and a joiner alike. Pins: the deterministic block (with the call restored),
 * the comparator, an AddContactConstraint store. Any failure stops the gate with reason 215.
 * The same stub carries the CONSTRAINT trace (native_gate_constraint_diag, lockstep_constraint_trace.h, the text the
 * Linux gate prints too): it installs the stub alone, without the rekey, when the island order is off. The Linux
 * gate's twin is linux/gate_island_order.h (the key's bytes alike; test_island_order checks the same golden key). */
#define IORDER_SITE 0x13d71ddu
#define IORDER_SORT 0x13eb190u
#define IORDER_STRIDE 0x360u
#define IORDER_ARRAY 0xa8u
#define IORDER_POINTS 4u
#define IORDER_POINT 0xc8u
#define IORDER_STUB_SIZE 0x1000u
#define IORDER_STOP_REASON 215u
typedef void (*iorder_sort_fn)(uint32_t *,uint32_t *,void *);
static const struct {unsigned rva,size;const char *sha;} iorder_pins[]={
    {0x13d71b0u,0x32u,"743c199599b6f654802c8fe34c4c32c06d2884aa0b73c47e0ac6f03b5001dc52"},   /* the deterministic block, the sort call */
    {0x13eb280u,0x49u,"43ea1f4363b09227a14024d60460bc9c684b71d78ab179e78a4fbd4a420fea3a"},   /* the comparator: key +0x10, BodyIDs */
    {0x13e4776u,0x2eu,"6a8c3a3ec82bb8168bc8cf46c98e90a026bef597ef42435c3958063dfc0cd7e5"},   /* AddContactConstraint: 0x360, bodies, key */
};
static unsigned char *iorder_stub;
static int iorder_installed,iorder_restored,iorder_rekey;   /* rekey 0: the stub for the CONSTRAINT trace alone */
static const char *iorder_stage="idle";
static volatile LONG64 iorder_failures;
static uint64_t iorder_sorts,iorder_constraints,iorder_unkeyed,iorder_ties,iorder_pointerish,iorder_badcount;
static uint64_t iorder_owner_key_native(uint64_t runtime) {unsigned klass;return world_identity_key(runtime,&klass);}
static uint64_t (*iorder_owner_key)(uint64_t)=iorder_owner_key_native;   /* tests replace it */
static void (*iorder_original)(uint32_t *,uint32_t *,void *);             /* tests replace it (0: the image's sort) */
#include "lockstep_constraint_trace.h"
static uint64_t iorder_fnv(uint64_t h,const void *p,size_t n) {
    const unsigned char *b=p;for(size_t i=0;i<n;i++) {h^=b[i];h*=0x100000001b3ull;}return h;
}
/* A qword that reads like a user-space pointer (the high dword 1 .. 0x7fff): two floats would make it a denormal. */
static int iorder_pointer_like(const unsigned char *p,size_t n) {
    int hits=0;
    for(size_t i=0;i+8<=n;i+=8) {uint64_t q;memcpy(&q,p+i,8);const uint32_t hi=(uint32_t)(q>>32);if(hi>=1u && hi<=0x7fffu) ++hits;}
    return hits;
}
/* A body's part of the key: its owner's portable ID and record (keyed 1), else its BodyID (keyed 0). */
static int iorder_body(const unsigned char *body,uint64_t *key,unsigned char *which) {
    unsigned char *record=NULL,*owner=NULL,*records[2]={NULL,NULL};uint64_t runtime=0;uint32_t id=0;
    *key=0;*which=3;
    if(body && read_local(body+0x50,&record,8) && record && read_local(record,&owner,8) && owner && read_local(owner+8,&runtime,8) &&
       runtime && read_local(owner+0x260,records,16)) {
        const uint64_t k=iorder_owner_key(runtime);
        if(k && (record==records[0] || record==records[1])) {*key=k;*which=record==records[0]?0u:1u;return 1;}
    }
    if(body) read_local(body+0x70,&id,4);
    *key=id;*which=2;return 0;
}
/* The key of one constraint; 0 when unreadable. */
static uint64_t iorder_key(const unsigned char *c) {
    unsigned char *body[2]={NULL,NULL},fields[0x20],point[24];uint32_t count=0;uint64_t h=0xcbf29ce484222325ull;
    if(!read_local(c,body,16) || !read_local(c+0x18,fields,0x20) || !read_local(c+0x38,&count,4)) return 0;
    for(unsigned k=0;k<2;k++) {
        uint64_t key;unsigned char which;
        if(!iorder_body(body[k],&key,&which)) ++iorder_unkeyed;
        h=iorder_fnv(h,&key,8);h=iorder_fnv(h,&which,1);
    }
    iorder_pointerish+=(uint64_t)iorder_pointer_like(fields,0x20);
    h=iorder_fnv(h,fields,0x20);
    if(!count || count>IORDER_POINTS) {++iorder_badcount;count=0;}
    h=iorder_fnv(h,&count,4);
    for(uint32_t k=0;k<count;k++) {
        if(!read_local(c+0x40+IORDER_POINT*k,point,24)) return 0;
        iorder_pointerish+=(uint64_t)iorder_pointer_like(point,24);
        h=iorder_fnv(h,point,24);
    }
    return h?h:1u;
}
/* The keys of the constraints [begin, end) of the manager's array; 1, 0 when one was unreadable. */
static int iorder_rekey_range(uint32_t *begin,uint32_t *end,const unsigned char *manager) {
    unsigned char *array=NULL;
    if(!begin || end<begin || !manager || !read_local(manager+IORDER_ARRAY,&array,8) || !array) return 0;
    for(uint32_t *i=begin;i<end;i++) {
        unsigned char *c=array+(size_t)*i*IORDER_STRIDE;const uint64_t key=iorder_key(c);
        if(!key) return 0;
        memcpy(c+0x10,&key,8);
    }
    return 1;
}
static uint64_t iorder_ties_in(const uint32_t *begin,const uint32_t *end,const unsigned char *manager) {
    unsigned char *array=NULL;uint64_t ties=0,prev=0;
    if(!read_local(manager+IORDER_ARRAY,&array,8) || !array) return 0;
    for(const uint32_t *i=begin;i<end;i++) {
        uint64_t key=0;read_local(array+(size_t)*i*IORDER_STRIDE+0x10,&key,8);
        if(i>begin && key==prev) ++ties;
        prev=key;
    }
    return ties;
}
static void iorder_sort(uint32_t *begin,uint32_t *end,void *manager) {
    iorder_sort_fn original=iorder_original?iorder_original:(iorder_sort_fn)(void *)(image+IORDER_SORT);
    const int rekey=iorder_rekey && end>begin+1;
    if(rekey) {
        ++iorder_sorts;iorder_constraints+=(uint64_t)(end-begin);
        if(!iorder_rekey_range(begin,end,manager)) InterlockedIncrement64(&iorder_failures);
    }
    original(begin,end,manager);
    if(rekey) iorder_ties+=iorder_ties_in(begin,end,manager);
    if(ctrace_on && end>begin) {
        unsigned char *array=NULL;
        if(manager && read_local((unsigned char *)manager+IORDER_ARRAY,&array,8) && array) ctrace_capture(begin,end,array,stats.issued);
        else InterlockedIncrement64(&ctrace_failures);
    }
}
/* The CONSTRAINT trace's fixture actor (1..12) for a body, as lockstep_contact.h's contact_actor without the registry. */
static unsigned iorder_trace_actor(const unsigned char *body) {
    void *record=NULL,*component=NULL;uint64_t id=0;
    if(!body || !read_local(body+0x50,&record,8) || !record || !read_local(record,&component,8) || !component ||
       !read_local((unsigned char *)component+8,&id,8)) return 0;
    for(unsigned i=0;i<ACTOR_CAPACITY;i++) if(component==actor_components[i] && id==actor_ids[i]) return i+1;
    return 0;
}
static int32_t iorder_rel_to(const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+IORDER_SITE+5));}
/* 0 original, 1 ours, -1 anything else. */
static int iorder_call_state(void) {
    unsigned char now[5];int32_t d;
    if(!read_local(image+IORDER_SITE,now,5) || now[0]!=0xe8) return -1;
    memcpy(&d,now+1,4);
    if(d==iorder_rel_to(image+IORDER_SORT)) return 0;
    return iorder_stub && d==iorder_rel_to(iorder_stub)?1:-1;
}
static int iorder_pins_ok(void) {
    static unsigned char copy[0x80];
    for(unsigned p=0;p<sizeof(iorder_pins)/sizeof(*iorder_pins);p++) {
        const unsigned rva=iorder_pins[p].rva,size=iorder_pins[p].size;
        if(size>sizeof(copy) || !read_local(image+rva,copy,size)) return 0;
        if(IORDER_SITE>=rva && IORDER_SITE+5<=rva+size) {
            if(iorder_call_state()<0) return 0;
            const int32_t d=iorder_rel_to(image+IORDER_SORT);memcpy(copy+(IORDER_SITE-rva)+1,&d,4);
        }
        if(!hash_ok(copy,size,iorder_pins[p].sha)) return 0;
    }
    return 1;
}
static unsigned char *iorder_alloc_near(void) {
    const uintptr_t at=(uintptr_t)(image+IORDER_SITE)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            unsigned char *p=VirtualAlloc((void *)(sign<0?at-distance:at+distance),IORDER_STUB_SIZE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            const intptr_t lo=(intptr_t)p-(intptr_t)(image+IORDER_SITE+5),hi=(intptr_t)(p+IORDER_STUB_SIZE)-(intptr_t)(image+IORDER_SITE+5);
            if(lo>=INT32_MIN && hi<=INT32_MAX) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int iorder_stub_owned(void) {
    void *t=NULL;
    return iorder_stub && iorder_stub[0]==0xff && iorder_stub[1]==0x25 && read_local(iorder_stub+6,&t,8) && t==(void *)iorder_sort;
}
static int iorder_cas(int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(IORDER_SITE+1,e,v,4);
}
static int iorder_install(int rekey) {
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(iorder_call_state()!=0) {iorder_stage="site";return 0;}
    if(!iorder_pins_ok()) {iorder_stage="pins";return 0;}
    if(!iorder_stub) iorder_stub=iorder_alloc_near();
    if(!iorder_stub) {iorder_stage="memory";return 0;}
    DWORD old,ignored;
    if(!VirtualProtect(iorder_stub,IORDER_STUB_SIZE,PAGE_READWRITE,&old)) {iorder_stage="stub";return 0;}
    {static const unsigned char jump[6]={0xff,0x25,0,0,0,0};void *target=(void *)iorder_sort;memset(iorder_stub,0xcc,64);
     memcpy(iorder_stub,jump,6);memcpy(iorder_stub+6,&target,8);}
    if(!VirtualProtect(iorder_stub,IORDER_STUB_SIZE,PAGE_EXECUTE_READ,&ignored)) {iorder_stage="stub";return 0;}
    FlushInstructionCache(GetCurrentProcess(),iorder_stub,IORDER_STUB_SIZE);
    iorder_sorts=iorder_constraints=iorder_unkeyed=iorder_ties=iorder_pointerish=iorder_badcount=0;iorder_failures=0;
    iorder_rekey=rekey;
    if(!iorder_cas(iorder_rel_to(image+IORDER_SORT),iorder_rel_to(iorder_stub))) {iorder_rekey=0;iorder_stage="cas";return 0;}
    iorder_installed=1;iorder_restored=0;iorder_stage="armed";return 1;
}
static int iorder_arm(void) {
    if(iorder_installed && iorder_rekey) return stats.active;   /* identical rerun after a loading-UI reload */
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed) {iorder_stage="needs";return 0;}
    if(iorder_installed) {iorder_rekey=1;iorder_stage="armed";return 1;}   /* the trace's stub, now rekeying */
    return iorder_install(1);
}
/* gate.constraint_diag(): the CONSTRAINT trace through the island order's stub (installed here when it is off). */
static int iorder_trace_arm(void) {
    if(!iorder_installed && !iorder_install(0)) return 0;
    ctrace_actor=iorder_trace_actor;ctrace_on=1;return 1;
}
static int iorder_owned(void) {return !iorder_installed || (iorder_call_state()==1 && iorder_stub_owned());}
/* Main-thread tick check; any failure stops the gate with reason 215. */
static int iorder_check(void) {
    if(InterlockedCompareExchange64(&iorder_failures,0,0)) {iorder_stage="unreadable";return 0;}
    if(InterlockedCompareExchange64(&ctrace_failures,0,0)) {iorder_stage="trace";return 0;}
    const char *why=!iorder_owned()?"site_changed":!iorder_pins_ok()?"block_changed":NULL;
    if(why) {iorder_stage=why;InterlockedIncrement64(&iorder_failures);return 0;}
    return 1;
}
static void iorder_restore(void) {
    if(!iorder_installed) return;
    ctrace_on=0;
    if(iorder_call_state()==1 && iorder_cas(iorder_rel_to(iorder_stub),iorder_rel_to(image+IORDER_SORT))) {iorder_installed=0;iorder_restored=1;}
}
static int iorder_status(char *out,size_t cap) {
    const int n=snprintf(out,cap,"installed=%d sorts=%llu constraints=%llu unkeyed=%llu ties=%llu pointerish=%llu badcount=%llu restored=%d "
        "stage=%s failures=%lld",iorder_installed && iorder_rekey,(unsigned long long)iorder_sorts,(unsigned long long)iorder_constraints,
        (unsigned long long)iorder_unkeyed,(unsigned long long)iorder_ties,(unsigned long long)iorder_pointerish,
        (unsigned long long)iorder_badcount,iorder_restored,iorder_stage,(long long)iorder_failures);
    return n>0 && (size_t)n<cap?n:0;
}

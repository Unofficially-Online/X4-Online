/* Hybrid sectors, slice H1, DEFAULT OFF (Windows gate, stop reason 187):
 *   native_gate_hybrid = {owners={k,...}, tick=T, poses=P}
 * Design, authority boundary, limits and the live-test spec: reports/LOCKSTEP-HYBRID-SECTORS-20261101.txt.
 *
 * From the latch of tick T on, the sector S_k (class 0x58) that holds pilot slot k's ship at that latch is
 * owned by client k, for every listed slot k (one sector per owner, never two owners in one sector).
 * - Attention (lockstep_view_points.h calls in): client k evaluates the objects of S_k (S_k itself and
 *   everything below it along the +0x70 parents) with the shared view points plus its own slot's ship;
 *   every other object, on every client, with the shared view points only (the pilot slots that own no
 *   sector). The other clients leave slot k out of the latched list altogether, so S_k runs there at the
 *   attention it would have without that player (no physics scene, no stand-in camera, the linear flight
 *   model, scripted combat). What follows the latched list (scenes, stand-ins, policy, shape intervals)
 *   follows each client's own list: on client k it still holds slot k, so S_k keeps its scene and stand-in
 *   there. At the latch of T every client recomputes the chains of its former and its new view points (the
 *   owner too, whose list did not change), so the ancestors of S_k (cluster, galaxy) drop to the level they
 *   have without slot k everywhere.
 * - Boundary, fail closed: at every latch from T on an owner's ship must still be alive and inside its
 *   sector (owner_lost, owner_left) and no other slot's ship inside an owned sector (other_entered): the
 *   handover a second player or a departure would need does not exist in H1. Any failure stops the gate at
 *   its next tick (187, why=).
 * - Members: at the first hybrid frame every imported object (and, with native_gate_created_identities,
 *   every live created object) below an owned sector, found through the identity tables and the native
 *   find; rebuilt from every census capture (entered=, left=). A member whose movement controller the
 *   census decodes (the NavigationController's current pathing slot, or the two getter forms) is a mover.
 * - Record (the owner, every frame): section 'hybrid' (input kind 0x59) of its blob, little endian:
 *     0 version 1 | 1 flags 0 | 2..5 the state's tick (u32) | 6..13 the sector's portable key |
 *     14..15 movers (u16) | 16..23 digest of every mover's quantized entry | 24 entries n (0..P) |
 *     then n entries of 36 bytes in a round robin over the movers: portable key (u64), parent tag (u32,
 *     the low half of the mixed portable key of the parent), position in the parent's frame (3 x int32,
 *     mm), rotation (3 x int16, x/y/z of the unit quaternion with w >= 0 of the matrix whose columns are
 *     the local axes, Q15), linear velocity (3 x int16, 1/4 m/s; 0 for a getter form).
 *   25 + 36 n bytes; n shrinks to the room the blob has left (squeezed=).
 * - Apply (every client, the admitted blobs, identical everywhere): a record is accepted from an owning
 *   slot only, for that slot's sector, with a state tick newer than that slot's last and older than the
 *   admitted tick; anything else stops every client alike (record_*). The receipt (records, entries,
 *   missing, fold of the payloads) joins the declared state through the input multiplexer. Proxies: on
 *   the clients that do not own the sector each entry is kept as that member's published pose and
 *   compared with the local copy (error, parent mismatches, unknown keys): status only, nothing is
 *   written into the engine.
 * - Census (native_gate_world_hash_stride): each captured item is classified (live: its parent chain;
 *   absent: the member table) after the capture hashed it; the outside items go into four outside roots
 *   exactly as the capture's domain roots take them (saved ID little endian, domain SHA-256, key order),
 *   the inside items into one inside root. gate.hybrid_census returns HYBRID_OUTSIDE_1:tick:count:r0:r1:
 *   r2:r3 for the declared state in place of the full roots, and a summary.
 * - Private slots: from T on an owner's ship is left out of the slot receipts of the declared state
 *   (PSTOCK flight state, pilot fire, PILOT_SHIPS rows): its state is S_k's.
 * Not in H1 (report section 7): handover; objects crossing the boundary (counted); shared engine resources
 * used inside an owned sector on one client only (runtime ID head, natural RNG draws, events to MD and AI,
 * the pathing budget); display of the proxies; frozen proxies. Reads only, no patch, no hook: the
 * attention split rides lockstep_view_points.h's evaluation, the census its capture. */
#include "lockstep_sha256.h"
#define HYB_STOP_REASON 187u
#define HYB_SECTOR_CLASS 0x58
#define HYB_SLOTS VIEW_MAX
#define HYB_MEMBERS 65536u
#define HYB_KIND 0x59u
#define HYB_VERSION 1u
#define HYB_HEADER 25u
#define HYB_ENTRY 36u
#define HYB_POSES_MAX 5u              /* 25 + 5 x 36 = 205 of the blob's 240 bytes */
#define HYB_RECORD_MAX (HYB_HEADER+HYB_POSES_MAX*HYB_ENTRY)
#define HYB_GRACE 16u                 /* ticks after T before a missing record counts (delay + lookahead) */
#define HYB_NAV_KIND 0x2c51158u       /* NavigationController vtable RVA (the census' motion domain) */
#define HYB_BANK 0x38f4ee4u           /* the pathing state's current index */
#define HYB_GETTER_A 0x10ba20u
#define HYB_GETTER_B 0x46ff70u
#define HYB_MAP 0x6cf1500u
#define HYB_FIND 0xced70u
#define HYB_DOMAIN 0x4859425249443131ULL /* "HYBRID11" */
typedef struct {
    uint64_t key,runtime;unsigned char *object;
    unsigned char sector,mover,gone,proxied;
    uint32_t published,parent;int32_t position[3];
} hyb_member;
typedef struct {double p[3],r[9],v[3];uint32_t parent;int velocity;} hyb_pose;
typedef void *(*hyb_find_fn)(void *,uint64_t,unsigned);
static int hyb_armed,hyb_active,hyb_scanned;
static unsigned hyb_owned,hyb_local,hyb_poses;
static uint64_t hyb_tick,hyb_started;
static unsigned char *hyb_sector[HYB_SLOTS];
static uint64_t hyb_sector_runtime[HYB_SLOTS],hyb_sector_key[HYB_SLOTS];
static int hyb_view_index=-1;          /* this client's owned point in the latched list, -1 none */
static void hyb_view_index_set(int index) {hyb_view_index=index;}
static hyb_member *hyb_members,*hyb_next;
static unsigned hyb_member_count,hyb_next_count;
static volatile LONG64 hyb_failures;
static const char *hyb_why="none";
static hyb_find_fn hyb_find_override;  /* tests; NULL: the native find */
/* Counters (status). */
static volatile LONG64 hyb_eval_inside,hyb_eval_outside;
static uint64_t hyb_scan_us,hyb_frame_us,hyb_frame_us_max,hyb_rebuilds,hyb_entered,hyb_left,hyb_member_overflow;
static uint64_t hyb_built,hyb_proposed,hyb_squeezed,hyb_bytes_sum,hyb_bytes_max,hyb_entries_out,hyb_pose_misses;
static unsigned hyb_movers_live,hyb_movers_own;
static uint64_t hyb_digest,hyb_cursor_key;
static unsigned char hyb_record[HYB_RECORD_MAX];
static unsigned hyb_record_entries;
static uint64_t hyb_record_tick=UINT64_MAX,hyb_proposal_tick=UINT64_MAX;
static char hyb_proposal_hex[2*HYB_RECORD_MAX+1];
static uint64_t hyb_last_state[HYB_SLOTS],hyb_records_in[HYB_SLOTS],hyb_entries_in,hyb_missing,hyb_applied_tick;
static uint64_t hyb_proxy_hits,hyb_proxy_unknown,hyb_proxy_parent,hyb_proxy_unread,hyb_err_n;
static double hyb_err_sum,hyb_err_max;
/* Census. */
static uint64_t hyb_cen_tick=UINT64_MAX,hyb_cen_outside,hyb_cen_inside,hyb_cen_absent,hyb_cen_captures;
static lockstep_sha256_ctx hyb_cen_roots[4],hyb_cen_inner;
static char hyb_cen_state[400],hyb_cen_summary[640];

static int hyb_fail(const char *why) {
    if(!InterlockedCompareExchange64(&hyb_failures,0,0)) hyb_why=why;
    InterlockedIncrement64(&hyb_failures);return 0;
}
static int hyb_check(void) {return !InterlockedCompareExchange64(&hyb_failures,0,0);}
static int hyb_owner(void) {return hyb_local>=1 && hyb_local<=HYB_SLOTS && ((hyb_owned>>(hyb_local-1))&1u);}
static uint64_t hyb_qpc_us(void) {
    LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart>0?(uint64_t)((unsigned __int128)(uint64_t)now.QuadPart*1000000u/(uint64_t)frequency.QuadPart):0;
}
/* Index+1 of the owned sector holding p (p itself or one of its +0x70 ancestors), 0 when none. */
static unsigned hyb_inside(const unsigned char *p) {
    for(unsigned depth=0;p && depth<17;depth++) {
        for(unsigned k=0;k<HYB_SLOTS;k++) if(hyb_sector[k] && p==hyb_sector[k]) return k+1;
        if(!read_local(p+0x70,&p,8)) return 0;
    }
    return 0;
}
/* The first ancestor (p included) of the class, through the object's own IsOfClass (vtable +0x11e8). */
static unsigned char *hyb_ancestor(unsigned char *p,int klass) {
    for(unsigned depth=0;p && depth<17;depth++) {
        void **vtable=NULL;int (*is_a)(void *,int)=NULL;
        if(!read_local(p,&vtable,8) || !vtable || !read_local((unsigned char*)vtable+0x11e8,&is_a,8) || !is_a) return NULL;
        if(is_a(p,klass)) return p;
        if(!read_local(p+0x70,&p,8)) return NULL;
    }
    return NULL;
}
/* View point i of the latched list counts for the object: every point but this client's owned one, the
 * owned one only inside its own sector. Called under view_lock (lockstep_view_points.h view_evaluate). */
static int hyb_point_counts(unsigned i,const void *object) {
    if(!hyb_active || hyb_view_index<0 || (int)i!=hyb_view_index) return 1;
    if(hyb_inside(object)==hyb_local) {InterlockedIncrement64(&hyb_eval_inside);return 1;}
    InterlockedIncrement64(&hyb_eval_outside);return 0;
}
/* Latch helpers (view_frame_locked, under view_lock): another client's owned slot is no view point here;
 * this client's own owned slot is, and its index is remembered. */
static int hyb_latch_skip(unsigned slot_index) {
    return hyb_active && slot_index<HYB_SLOTS && ((hyb_owned>>slot_index)&1u) && slot_index+1!=hyb_local;
}
static int hyb_latch_own(unsigned slot_index) {
    return hyb_active && slot_index<HYB_SLOTS && ((hyb_owned>>slot_index)&1u) && slot_index+1==hyb_local;
}
/* The latch of the first tick >= T: every owned slot's sector, the same on every client (the slot ships
 * are shared state up to here). 1 when the split starts at this latch. */
static int hyb_latch_begin(uint64_t tick) {
    if(!hyb_armed || hyb_active || !hyb_check() || tick<hyb_tick) return 0;
    if(!pilot_slot_count) return hyb_fail("no_slots");
    unsigned char *sectors[HYB_SLOTS]={0};uint64_t runtimes[HYB_SLOTS]={0},keys[HYB_SLOTS]={0};
    for(unsigned i=0;i<HYB_SLOTS;i++) {
        if(!((hyb_owned>>i)&1u)) continue;
        if(i>=pilot_slot_count) return hyb_fail("owner_slot");
        pilot_slot *s=&pilot_slots[i];uint64_t id=0;
        if(!s->resolved || !s->object || !read_local(s->object+8,&id,8) || id!=s->runtime) return hyb_fail("owner_lost");
        unsigned char *sector=hyb_ancestor(s->object,HYB_SECTOR_CLASS);
        if(!sector || !read_local(sector+8,&runtimes[i],8) || !runtimes[i]) return hyb_fail("owner_sector");
        for(unsigned j=0;j<i;j++) if(sectors[j]==sector) return hyb_fail("shared_sector");
        if(!(keys[i]=world_identity_portable(runtimes[i]))) return hyb_fail("sector_key");
        sectors[i]=sector;
    }
    memcpy(hyb_sector,sectors,sizeof(sectors));memcpy(hyb_sector_runtime,runtimes,sizeof(runtimes));memcpy(hyb_sector_key,keys,sizeof(keys));
    hyb_active=1;hyb_started=tick;return 1;
}
/* After every latch from T on: the boundary of H1 (no handover). */
static void hyb_latch_check(void) {
    if(!hyb_active || !hyb_check()) return;
    for(unsigned i=0;i<HYB_SLOTS;i++) if(hyb_sector[i]) {
        uint64_t id=0;if(!read_local(hyb_sector[i]+8,&id,8) || id!=hyb_sector_runtime[i]) {hyb_fail("sector_lost");return;}
    }
    for(unsigned i=0;i<pilot_slot_count && i<HYB_SLOTS;i++) {
        pilot_slot *s=&pilot_slots[i];const int owned=(hyb_owned>>i)&1u;uint64_t id=0;
        const int live=s->resolved && s->object && read_local(s->object+8,&id,8) && id==s->runtime;
        if(!live) {if(owned) {hyb_fail("owner_lost");return;} continue;}
        const unsigned in=hyb_inside(s->object);
        if(owned && in!=i+1) {hyb_fail("owner_left");return;}
        if(!owned && in) {hyb_fail("other_entered");return;}
    }
}
static int hyb_slot_private(unsigned slot) {return hyb_active && slot>=1 && slot<=HYB_SLOTS && ((hyb_owned>>(slot-1))&1u);}

/* Members. */
static void *hyb_lookup(uint64_t runtime) {
    void *map=NULL;uint64_t id=0;
    if(!runtime || !read_local(image+HYB_MAP,&map,8) || !map) return NULL;
    hyb_find_fn find=hyb_find_override?hyb_find_override:(hyb_find_fn)(void *)(image+HYB_FIND);
    unsigned char *object=find(map,runtime,4);
    return object && read_local(object+8,&id,8) && id==runtime?object:NULL;
}
static uint32_t hyb_parent_tag(const unsigned char *object) {
    unsigned char *parent=NULL;uint64_t id=0,key;
    if(!read_local(object+0x70,&parent,8) || !parent || !read_local(parent+8,&id,8) || !(key=world_identity_portable(id))) return 0;
    return (uint32_t)lockstep_mix(key);
}
/* Positional (class 77) as the census decides it (world_class_flags: the verified IsRealClass bitmap): only those
 * have the movement controller at +0x200 the census reads. */
static int (*hyb_positional_override)(const unsigned char *object); /* tests; NULL: the class bitmap */
static int hyb_positional(const unsigned char *object) {
    if(hyb_positional_override) return hyb_positional_override(object);
    uintptr_t vtable=0;if(!read_local(object,&vtable,8)) return 0;
    const unsigned flags=world_class_flags(vtable);return !(flags&0x80000000u) && (flags&2u);
}
/* A mover's pose as the census reads its motion: the NavigationController's current pathing slot (position,
 * the three axis rows, linear velocity) or a getter form's four vectors (position, axis rows). */
static int hyb_read_pose(unsigned char *object,hyb_pose *out) {
    unsigned char *controller=NULL;uintptr_t vtable=0;float f[24];
    memset(out,0,sizeof(*out));
    if(!hyb_positional(object)) return 0;
    if(!read_local(object+0x200,&controller,8) || !controller || !read_local(controller,&vtable,8)) return 0;
    vtable=pslot_canonical_vtable(vtable);
    if(vtable<(uintptr_t)image || vtable>=(uintptr_t)(image+0x6fa9000)) return 0;
    if(vtable-(uintptr_t)image==HYB_NAV_KIND) {
        unsigned char *slots=NULL;uint32_t index=0;
        if(!read_local(controller+0x30,&slots,8) || !slots || !read_local(image+HYB_BANK,&index,4) || index>1 ||
           !read_local(slots+index*0x1a0u,f,0x60)) return 0;
        out->velocity=1;
    } else {
        uintptr_t getter=0;unsigned char *at=NULL;
        if(!read_local((void*)(vtable+0x30),&getter,8)) return 0;
        if(getter==(uintptr_t)(image+HYB_GETTER_A)) at=controller+0x10;
        else if(getter==(uintptr_t)(image+HYB_GETTER_B)) at=controller+0x20;
        if(!at || !read_local(at,f,0x40)) return 0;
    }
    for(unsigned i=0;i<3;i++) {
        out->p[i]=f[i];out->r[i]=f[4+i];out->r[3+i]=f[8+i];out->r[6+i]=f[12+i];out->v[i]=out->velocity?f[16+i]:0.0;
    }
    for(unsigned i=0;i<3;i++) if(!isfinite(out->p[i]) || !isfinite(out->v[i])) return 0;
    for(unsigned i=0;i<9;i++) if(!isfinite(out->r[i])) return 0;
    out->parent=hyb_parent_tag(object);return 1;
}
static int hyb_member_compare(const void *a,const void *b) {
    uint64_t x=((const hyb_member*)a)->key,y=((const hyb_member*)b)->key;return x<y?-1:x>y;
}
static hyb_member *hyb_member_find(uint64_t key) {
    unsigned lo=0,hi=hyb_member_count;
    while(lo<hi) {unsigned mid=lo+(hi-lo)/2;if(hyb_members[mid].key<key) lo=mid+1;else hi=mid;}
    return lo<hyb_member_count && hyb_members[lo].key==key?&hyb_members[lo]:NULL;
}
static int hyb_member_add(uint64_t key,uint64_t runtime,unsigned char *object,unsigned sector) {
    if(hyb_next_count>=HYB_MEMBERS) {++hyb_member_overflow;return 0;}
    hyb_pose pose;hyb_member *m=&hyb_next[hyb_next_count++];memset(m,0,sizeof(*m));
    m->key=key;m->runtime=runtime;m->object=object;m->sector=(unsigned char)sector;m->mover=(unsigned char)hyb_read_pose(object,&pose);
    return 1;
}
/* hyb_next becomes the member table; entered/left against the old one, published poses carried over. */
static void hyb_members_swap(int count_changes) {
    qsort(hyb_next,hyb_next_count,sizeof(*hyb_next),hyb_member_compare);
    unsigned i=0,j=0;
    while(i<hyb_member_count || j<hyb_next_count) {
        if(j>=hyb_next_count || (i<hyb_member_count && hyb_members[i].key<hyb_next[j].key)) {if(count_changes && !hyb_members[i].gone) ++hyb_left;++i;}
        else if(i>=hyb_member_count || hyb_next[j].key<hyb_members[i].key) {if(count_changes) ++hyb_entered;++j;}
        else {
            hyb_member *n=&hyb_next[j];const hyb_member *o=&hyb_members[i];
            n->proxied=o->proxied;n->published=o->published;n->parent=o->parent;memcpy(n->position,o->position,sizeof(n->position));
            ++i;++j;
        }
    }
    hyb_member *t=hyb_members;hyb_members=hyb_next;hyb_next=t;hyb_member_count=hyb_next_count;hyb_next_count=0;++hyb_rebuilds;
}
/* The first hybrid frame: every identity below an owned sector. */
static int hyb_scan(void) {
    hyb_next_count=0;
    if(world_identities) for(unsigned i=0;i<WORLD_IDENTITY_CAPACITY;i++) {
        const world_identity_entry *e=&world_identities[i];if(!e->runtime) continue;
        unsigned char *object=hyb_lookup(e->runtime);const unsigned in=object?hyb_inside(object):0;
        if(in && !hyb_member_add(e->saved,e->runtime,object,in-1)) return hyb_fail("members_full");
    }
    if(created_enabled() && created_live) {
        int ok=1;AcquireSRWLockShared(&created_lock);
        for(unsigned i=0;ok && i<created_live_count;i++) {
            unsigned char *object=hyb_lookup(created_live[i].runtime);const unsigned in=object?hyb_inside(object):0;
            if(in) ok=hyb_member_add(created_live[i].created,created_live[i].runtime,object,in-1);
        }
        ReleaseSRWLockShared(&created_lock);
        if(!ok) return hyb_fail("members_full");
    }
    hyb_members_swap(0);return 1;
}

/* Record. */
static int32_t hyb_i32(double v,double scale) {
    double x=floor(v*scale+0.5);return x>2147483647.0?INT32_MAX:x<-2147483647.0?-INT32_MAX:(int32_t)x;
}
static int16_t hyb_i16(double v,double scale) {
    double x=floor(v*scale+0.5);return (int16_t)(x>32767.0?32767:x<-32767.0?-32767:(int)x);
}
/* Unit quaternion (x,y,z,w; w >= 0) of the matrix whose columns are the axis rows r[0..2], r[3..5], r[6..8]. */
static void hyb_quaternion(const double r[9],double q[4]) {
    const double m00=r[0],m01=r[3],m02=r[6],m10=r[1],m11=r[4],m12=r[7],m20=r[2],m21=r[5],m22=r[8];
    const double trace=m00+m11+m22;double s;
    if(trace>0) {s=sqrt(trace+1.0)*2;q[3]=0.25*s;q[0]=(m21-m12)/s;q[1]=(m02-m20)/s;q[2]=(m10-m01)/s;}
    else if(m00>m11 && m00>m22) {s=sqrt(1.0+m00-m11-m22)*2;q[3]=(m21-m12)/s;q[0]=0.25*s;q[1]=(m01+m10)/s;q[2]=(m02+m20)/s;}
    else if(m11>m22) {s=sqrt(1.0+m11-m00-m22)*2;q[3]=(m02-m20)/s;q[0]=(m01+m10)/s;q[1]=0.25*s;q[2]=(m12+m21)/s;}
    else {s=sqrt(1.0+m22-m00-m11)*2;q[3]=(m10-m01)/s;q[0]=(m02+m20)/s;q[1]=(m12+m21)/s;q[2]=0.25*s;}
    double n=sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    if(!(n>0) || !isfinite(n)) {q[0]=q[1]=q[2]=0;q[3]=1;return;}
    for(unsigned i=0;i<4;i++) q[i]/=n;
    if(q[3]<0) for(unsigned i=0;i<4;i++) q[i]=-q[i];
}
static void hyb_entry(uint64_t key,const hyb_pose *p,unsigned char out[HYB_ENTRY]) {
    double q[4];hyb_quaternion(p->r,q);
    memcpy(out,&key,8);memcpy(out+8,&p->parent,4);
    for(unsigned i=0;i<3;i++) {int32_t v=hyb_i32(p->p[i],1000.0);memcpy(out+12+4*i,&v,4);}
    for(unsigned i=0;i<3;i++) {int16_t v=hyb_i16(q[i],32767.0);memcpy(out+24+2*i,&v,2);}
    for(unsigned i=0;i<3;i++) {int16_t v=hyb_i16(p->v[i],4.0);memcpy(out+30+2*i,&v,2);}
}
static uint64_t hyb_bytes_hash(const unsigned char *p,size_t n,uint64_t h) {
    for(size_t i=0;i<n;i++) {h^=p[i];h*=0x100000001b3ULL;}
    return h;
}
/* The owner's mover pass: the digest of every own mover and the next entries of the round robin. A mover
 * no longer below the sector (or gone) is a boundary crossing: counted, dropped from the pass. */
static void hyb_build(uint64_t tick) {
    unsigned char after[HYB_POSES_MAX][HYB_ENTRY],before[HYB_POSES_MAX][HYB_ENTRY];unsigned na=0,nb=0,live=0,own=0;
    uint64_t digest=0xcbf29ce484222325ULL;
    for(unsigned i=0;i<hyb_member_count;i++) {
        hyb_member *m=&hyb_members[i];
        if(m->sector+1u!=hyb_local || !m->mover || m->gone) continue;
        ++own;uint64_t id=0;hyb_pose pose;unsigned char entry[HYB_ENTRY];
        if(!read_local(m->object+8,&id,8) || id!=m->runtime || hyb_inside(m->object)!=hyb_local) {m->gone=1;++hyb_left;continue;}
        if(!hyb_read_pose(m->object,&pose)) {++hyb_pose_misses;continue;}
        hyb_entry(m->key,&pose,entry);digest=hyb_bytes_hash(entry,HYB_ENTRY,digest);++live;
        if(m->key>hyb_cursor_key) {if(na<hyb_poses) memcpy(after[na++],entry,HYB_ENTRY);}
        else if(nb<hyb_poses) memcpy(before[nb++],entry,HYB_ENTRY);
    }
    unsigned n=0;
    for(unsigned i=0;i<na && n<hyb_poses;i++) memcpy(hyb_record+HYB_HEADER+HYB_ENTRY*n++,after[i],HYB_ENTRY);
    for(unsigned i=0;i<nb && n<hyb_poses;i++) memcpy(hyb_record+HYB_HEADER+HYB_ENTRY*n++,before[i],HYB_ENTRY);
    const uint32_t tick32=(uint32_t)tick;const uint16_t movers=(uint16_t)(live>65535u?65535u:live);
    const uint64_t key=hyb_sector_key[hyb_local-1];
    hyb_record[0]=HYB_VERSION;hyb_record[1]=0;memcpy(hyb_record+2,&tick32,4);memcpy(hyb_record+6,&key,8);
    memcpy(hyb_record+14,&movers,2);memcpy(hyb_record+16,&digest,8);hyb_record[24]=(unsigned char)n;
    hyb_record_entries=n;hyb_record_tick=tick;hyb_digest=digest;hyb_movers_live=live;hyb_movers_own=own;++hyb_built;
}
/* gate.hybrid_frame(tick): after the latch of every admitted frame. */
static int hyb_frame(uint64_t tick) {
    if(!hyb_armed || GetCurrentThreadId()!=ui_thread || tick!=stats.issued) {hyb_fail("frame_guard");return 0;}
    if(!hyb_active || !hyb_check()) return 1;
    const uint64_t since=hyb_qpc_us();
    if(!hyb_scanned) {
        hyb_scanned=1;if(!hyb_scan()) return 1;
        hyb_scan_us=hyb_qpc_us()-since;
    }
    if(hyb_owner()) hyb_build(tick);
    const uint64_t spent=hyb_qpc_us()-since;hyb_frame_us=spent;if(spent>hyb_frame_us_max && hyb_built>1) hyb_frame_us_max=spent;
    return 1;
}
static const char hyb_hexdigits[]="0123456789abcdef";
/* gate.hybrid_proposal(tick, budget hex characters): this client's record for the report tick. */
static int hyb_proposal_text(uint64_t tick,unsigned budget_hex,char *out,size_t cap) {
    if(!hyb_active || !hyb_owner() || !hyb_check()) return 0;
    if(tick==hyb_proposal_tick) {size_t n=strlen(hyb_proposal_hex);if(!n || n+1>cap) return 0;memcpy(out,hyb_proposal_hex,n+1);return (int)n;}
    if(hyb_record_tick!=tick) return 0;
    hyb_proposal_tick=tick;hyb_proposal_hex[0]=0;
    const unsigned budget=budget_hex/2;
    if(budget<HYB_HEADER) {++hyb_squeezed;return 0;}
    unsigned m=hyb_record_entries;const unsigned fit=(budget-HYB_HEADER)/HYB_ENTRY;
    if(m>fit) {m=fit;++hyb_squeezed;}
    const unsigned length=HYB_HEADER+m*HYB_ENTRY;
    if(2*length+1>cap || 2*length+1>sizeof(hyb_proposal_hex)) return 0;
    hyb_record[24]=(unsigned char)m;
    for(unsigned i=0;i<length;i++) {hyb_proposal_hex[2*i]=hyb_hexdigits[hyb_record[i]>>4];hyb_proposal_hex[2*i+1]=hyb_hexdigits[hyb_record[i]&15];}
    hyb_proposal_hex[2*length]=0;memcpy(out,hyb_proposal_hex,2*length+1);
    if(m) memcpy(&hyb_cursor_key,hyb_record+HYB_HEADER+HYB_ENTRY*(m-1),8);
    ++hyb_proposed;hyb_entries_out+=m;hyb_bytes_sum+=length+2;if(length+2>hyb_bytes_max) hyb_bytes_max=length+2;
    return (int)(2*length);
}
static int hyb_hex_byte(const char *s,unsigned char *out) {
    unsigned v=0;
    for(unsigned i=0;i<2;i++) {
        char c=s[i];unsigned d=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):16u;
        if(d>15) return 0;v=v*16+d;
    }
    *out=(unsigned char)v;return 1;
}
/* One admitted entry on a client that does not own the sector: the member's published pose, compared
 * with the local copy. Status only. */
static void hyb_proxy(const unsigned char *entry,uint32_t state) {
    uint64_t key;uint32_t parent;int32_t position[3];
    memcpy(&key,entry,8);memcpy(&parent,entry+8,4);memcpy(position,entry+12,12);
    hyb_member *m=hyb_member_find(key);
    if(!m) {++hyb_proxy_unknown;return;}
    m->proxied=1;m->published=state;m->parent=parent;memcpy(m->position,position,sizeof(position));++hyb_proxy_hits;
    uint64_t id=0;hyb_pose pose;
    if(m->gone || !read_local(m->object+8,&id,8) || id!=m->runtime || !hyb_read_pose(m->object,&pose)) {++hyb_proxy_unread;return;}
    if(pose.parent!=parent) {++hyb_proxy_parent;return;}
    double d2=0;for(unsigned i=0;i<3;i++) {double d=pose.p[i]-position[i]/1000.0;d2+=d*d;}
    const double error=sqrt(d2);hyb_err_sum+=error;++hyb_err_n;if(error>hyb_err_max) hyb_err_max=error;
}
/* gate.hybrid_apply(tick, clients, payloads): the admitted 'hybrid' sections of every slot, comma separated
 * ('' for none); the receipt for the declared state. A record that is not acceptable stops the gate at its
 * next tick on every client alike (the admitted blobs are the same everywhere) and the receipt names it;
 * 0 (nil) only for malformed arguments. */
static int hyb_record_check(unsigned k,const unsigned char *bytes,size_t length,uint64_t tick,const char **why) {
    *why="record_shape";
    if(length<HYB_HEADER || length>HYB_RECORD_MAX) return 0;
    if(!hyb_active || k>=HYB_SLOTS || !((hyb_owned>>k)&1u)) {*why="record_slot";return 0;}
    const unsigned count=bytes[24];uint32_t state;uint64_t sector;
    memcpy(&state,bytes+2,4);memcpy(&sector,bytes+6,8);
    if(bytes[0]!=HYB_VERSION || bytes[1] || count>HYB_POSES_MAX || length!=HYB_HEADER+count*HYB_ENTRY) return 0;
    if(sector!=hyb_sector_key[k]) {*why="record_sector";return 0;}
    if(state<hyb_started || state>=tick || state<=hyb_last_state[k]) {*why="record_tick";return 0;}
    return 1;
}
static int hyb_apply_text(uint64_t tick,unsigned clients,const char *payloads,char *out,size_t cap) {
    if(!hyb_armed || !payloads || clients<1 || clients>GATE_CLIENTS_MAX || tick<=hyb_applied_tick) return 0;
    unsigned char bytes[GATE_CLIENTS_MAX][HYB_RECORD_MAX];size_t lengths[GATE_CLIENTS_MAX];const char *at=payloads,*why=NULL;
    for(unsigned k=0;k<clients;k++) { /* parse everything first: a malformed argument changes nothing */
        const size_t n=strcspn(at,",");lengths[k]=n/2;
        if(n%2 || n/2>HYB_RECORD_MAX) {if(!why) why="record_shape";lengths[k]=0;}
        else for(size_t i=0;i<n/2;i++) if(!hyb_hex_byte(at+2*i,&bytes[k][i])) return 0;
        at+=n;
        if(k+1<clients) {if(*at!=',') return 0;++at;}
    }
    if(*at) return 0;
    hyb_applied_tick=tick;
    uint64_t fold=0;unsigned records=0,entries=0;
    for(unsigned k=0;k<clients && !why;k++) {
        if(!lengths[k]) {
            if(hyb_active && k<HYB_SLOTS && ((hyb_owned>>k)&1u) && tick>hyb_started+HYB_GRACE) ++hyb_missing;
            continue;
        }
        if(!hyb_record_check(k,bytes[k],lengths[k],tick,&why)) break;
        why=NULL;
        const unsigned count=bytes[k][24];uint32_t state;memcpy(&state,bytes[k]+2,4);
        hyb_last_state[k]=state;++hyb_records_in[k];++records;entries+=count;hyb_entries_in+=count;
        fold=lockstep_mix(fold^lockstep_seed(HYB_DOMAIN,tick,k+1u,lengths[k],hyb_bytes_hash(bytes[k],lengths[k],0xcbf29ce484222325ULL),0));
        if(k+1!=hyb_local) for(unsigned e=0;e<count;e++) hyb_proxy(bytes[k]+HYB_HEADER+HYB_ENTRY*e,state);
    }
    int w;
    if(why) {hyb_fail(why);w=snprintf(out,cap,"HYB_1|%llu|refused|%s",(unsigned long long)tick,why);}
    else w=snprintf(out,cap,"HYB_1|%llu|%u|%u|%llu|%016llx",(unsigned long long)tick,records,entries,(unsigned long long)hyb_missing,
                    (unsigned long long)fold);
    return w>0 && (size_t)w<cap?w:0;
}

/* Census: world_hash_capture_with calls this for every item after hashing it (world_hash_after_item). */
static void hyb_census_item(uint64_t tick,const world_hash_item *item,unsigned char *object) {
    if(!hyb_active) return;
    if(tick!=hyb_cen_tick) {
        hyb_cen_tick=tick;hyb_cen_outside=hyb_cen_inside=hyb_cen_absent=0;hyb_next_count=0;
        for(unsigned d=0;d<4;d++) lockstep_sha256_init(&hyb_cen_roots[d]);
        lockstep_sha256_init(&hyb_cen_inner);
    }
    unsigned in=0;
    if(object) in=hyb_inside(object);
    else {const hyb_member *m=hyb_member_find(item->saved);if(m) {in=m->sector+1u;++hyb_cen_absent;}}
    if(in) {
        ++hyb_cen_inside;lockstep_sha256_update(&hyb_cen_inner,&item->saved,8);
        for(unsigned d=0;d<4;d++) lockstep_sha256_update(&hyb_cen_inner,item->hash[d],32);
        if(object) hyb_member_add(item->saved,item->runtime,object,in-1);
    } else {
        ++hyb_cen_outside;
        for(unsigned d=0;d<4;d++) {lockstep_sha256_update(&hyb_cen_roots[d],&item->saved,8);lockstep_sha256_update(&hyb_cen_roots[d],item->hash[d],32);}
    }
}
/* gate.hybrid_census(tick), after that tick's capture: the outside state and the summary; the member table
 * is rebuilt from the capture's inside items. 0 before T, without a complete capture of the tick. */
static int hyb_census_text(uint64_t tick) {
    if(!hyb_active || hyb_cen_tick!=tick || world_hash_tick!=tick || !world_hash_scans) return 0;
    const unsigned total=world_hash_count+world_hash_created_count;
    if(hyb_cen_outside+hyb_cen_inside!=total) {hyb_fail("census_count");return 0;}
    char roots[4][65],inner[65];unsigned char raw[32];
    for(unsigned d=0;d<4;d++) {lockstep_sha256_final(&hyb_cen_roots[d],raw);hex32(raw,roots[d]);}
    lockstep_sha256_final(&hyb_cen_inner,raw);hex32(raw,inner);
    const uint64_t entered=hyb_entered,left=hyb_left;
    if(hyb_member_overflow) {hyb_fail("members_full");return 0;}
    hyb_members_swap(1);++hyb_cen_captures;hyb_cen_tick=UINT64_MAX; /* a second call for the tick is refused */
    unsigned movers=0;for(unsigned i=0;i<hyb_member_count;i++) movers+=hyb_members[i].mover;
    snprintf(hyb_cen_state,sizeof(hyb_cen_state),"HYBRID_OUTSIDE_1:%llu:%llu:%s:%s:%s:%s",(unsigned long long)tick,
             (unsigned long long)hyb_cen_outside,roots[0],roots[1],roots[2],roots[3]);
    snprintf(hyb_cen_summary,sizeof(hyb_cen_summary),"tick=%llu outside=%llu inside=%llu absent_inside=%llu members=%u movers=%u "
             "entered=%llu left=%llu inside_root=%s state=%s",(unsigned long long)tick,(unsigned long long)hyb_cen_outside,
             (unsigned long long)hyb_cen_inside,(unsigned long long)hyb_cen_absent,hyb_member_count,movers,
             (unsigned long long)(hyb_entered-entered),(unsigned long long)(hyb_left-left),inner,hyb_cen_state);
    return 1;
}
/* Before the first admitted frame, on the MoveWorker, with view points 'players' installed. */
static int hyb_arm(double owned,double tick,double poses) {
    if(!isfinite(owned) || !isfinite(tick) || !isfinite(poses) || floor(owned)!=owned || floor(tick)!=tick || floor(poses)!=poses ||
       owned<1 || owned>=(double)(1u<<HYB_SLOTS) || tick<2 || tick>PILOT_TICK_MAX || poses<0 || poses>HYB_POSES_MAX) return 0;
    if(hyb_armed) return hyb_owned==(unsigned)owned && hyb_tick==(uint64_t)tick && hyb_poses==(unsigned)poses; /* a loading-UI reload */
    if(!view_installed || view_mode!=VIEW_PLAYERS || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(stats.slot<1 || stats.slot>GATE_CLIENTS_MAX) return 0;
    if(world_hash_after_item && world_hash_after_item!=hyb_census_item) return 0;
    if(!hyb_members) hyb_members=VirtualAlloc(NULL,sizeof(*hyb_members)*HYB_MEMBERS,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!hyb_next) hyb_next=VirtualAlloc(NULL,sizeof(*hyb_next)*HYB_MEMBERS,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!hyb_members || !hyb_next) return 0;
    hyb_owned=(unsigned)owned;hyb_tick=(uint64_t)tick;hyb_poses=(unsigned)poses;hyb_local=stats.slot;
    world_hash_after_item=hyb_census_item;hyb_armed=1;return 1;
}
static int hyb_status(char *out,size_t cap) {
    char sectors[HYB_SLOTS*24+2]="-";int k=0;
    for(unsigned i=0;i<HYB_SLOTS;i++) if(hyb_sector_key[i])
        k+=snprintf(sectors+k,sizeof(sectors)-(size_t)k,"%s%u:%llx",k?",":"",i+1,(unsigned long long)hyb_sector_key[i]);
    char records[HYB_SLOTS*24+2];int r=0;
    for(unsigned i=0;i<HYB_SLOTS;i++) r+=snprintf(records+r,sizeof(records)-(size_t)r,"%s%llu",i?"/":"",(unsigned long long)hyb_records_in[i]);
    const double mean=hyb_err_n?hyb_err_sum/(double)hyb_err_n:0.0,bytes=hyb_proposed?(double)hyb_bytes_sum/(double)hyb_proposed:0.0;
    unsigned movers=0;for(unsigned i=0;i<hyb_member_count;i++) movers+=hyb_members[i].mover;
    int n=snprintf(out,cap,"armed=%d active=%d owners=%x local=%u tick=%llu started=%llu sectors=%s owner=%d view_index=%d "
        "members=%u movers=%u own_movers=%u live_movers=%u scan_us=%llu frame_us=%llu frame_us_max=%llu rebuilds=%llu entered=%llu left=%llu "
        "eval_inside=%lld eval_outside=%lld built=%llu proposed=%llu squeezed=%llu entries_out=%llu bytes_mean=%.1f bytes_max=%llu "
        "full_bytes=%u digest=%016llx pose_misses=%llu records=%s entries_in=%llu missing=%llu proxies=%llu unknown=%llu parent_mismatch=%llu "
        "unread=%llu err_mean_m=%.3f err_max_m=%.3f census=%llu failures=%lld why=%s",
        hyb_armed,hyb_active,hyb_owned,hyb_local,(unsigned long long)hyb_tick,(unsigned long long)hyb_started,sectors,hyb_owner(),hyb_view_index,
        hyb_member_count,movers,hyb_movers_own,hyb_movers_live,(unsigned long long)hyb_scan_us,(unsigned long long)hyb_frame_us,
        (unsigned long long)hyb_frame_us_max,(unsigned long long)hyb_rebuilds,(unsigned long long)hyb_entered,(unsigned long long)hyb_left,
        (long long)hyb_eval_inside,(long long)hyb_eval_outside,(unsigned long long)hyb_built,(unsigned long long)hyb_proposed,
        (unsigned long long)hyb_squeezed,(unsigned long long)hyb_entries_out,bytes,(unsigned long long)hyb_bytes_max,
        HYB_HEADER+2u+HYB_ENTRY*hyb_movers_live,(unsigned long long)hyb_digest,(unsigned long long)hyb_pose_misses,records,
        (unsigned long long)hyb_entries_in,(unsigned long long)hyb_missing,(unsigned long long)hyb_proxy_hits,(unsigned long long)hyb_proxy_unknown,
        (unsigned long long)hyb_proxy_parent,(unsigned long long)hyb_proxy_unread,mean,hyb_err_max,(unsigned long long)hyb_cen_captures,
        (long long)hyb_failures,hyb_why);
    return n>0 && (size_t)n<cap?n:0;
}

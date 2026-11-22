/* native_gate_island_order on the Linux gate (a Windows default; Lua-armed as there, needs the world identities):
 * Windows' lockstep_island_order.h, the velocity solve's contact sort keyed by what every client holds alike. With it
 * the sort stub also carries the CONSTRAINT trace (native_gate_constraint_diag, ../lockstep_constraint_trace.h),
 * which installs the stub alone when the island order is off. Included by gate_linux.c after gate_contact.h.
 *
 * Why: Windows gives every constraint of an island the FNV-1a hash of its bodies' owners' portable IDs, its normal
 * through the inertia scales and its points before Jolt's sort; the Linux gate left Jolt's key (the hash of BodyIDs
 * and SubShapeIDs). A session with Windows and Linux PCs then solved two contacts of one island in other orders, and
 * every island with more than one contact parted.
 *
 * Linux site: JobSolveVelocityConstraints' deterministic block (0x1b212c8: cmp byte [system+0x214],0 for Windows'
 * +0x2ac, the contact manager at system+0x220 for +0x2b8; at 0x1b21757 sSortConstraints 0x1b7db40, Windows 0x1483c30)
 * and at 0x1b21769 call QuickSort 0x1ae7a00 (Windows 0x13eb190) with begin, end, the manager in rdi, rsi, rdx. Its
 * inlined comparator reads the constraint array at manager +0xb0 (Windows +0xa8): the 64-bit key +0x10, then body 1's
 * and body 2's BodyIDs (body +0x70). FinishConstraintBuffer (0x1b1ef14) frees system+0x2d0, manager+0xb0. A
 * ContactConstraint is Jolt's own 0x360 bytes as on Windows; StoreAppliedImpulses (0x1b21691) reads the count +0x38,
 * the points of 0xc8 from +0x40, the non-penetration lambda +0x3c and the cache point +0xc0. An owner's two records sit
 * 8 lower than Windows' (+0x258/+0x260 for +0x260/+0x268, as gate_battle.h found for the ships); the owner's runtime
 * ID +8 and world_identity_key as Windows. The key's bytes are Windows'.
 *
 * The call's rel32 goes to a jmp [rip] on a page near the image (one compare-exchange of its 16-byte line). Pinned
 * (iorder_block, iorder_calls up to the call, iorder_sort, iorder_store, iorder_buffer); the call is checked by its
 * state. Counters as Windows', atomic here. Any failure stops the gate with reason 215 as Windows. */
#define LXIO_SITE_VA 0x1b21769u
#define LXIO_SORT_VA 0x1ae7a00u
#define LXIO_ARRAY 0xb0u
#define LXIO_RECORDS 0x258u
#define LXIO_STRIDE 0x360u
#define LXIO_POINTS 4u
#define LXIO_POINT 0xc8u
#define LXIO_STOP_REASON 215u
typedef void (*lxio_sort_fn)(uint32_t *,uint32_t *,void *);
static const char *const lxio_pin_names[]={"iorder_block","iorder_calls","iorder_sort","iorder_store","iorder_buffer"};
static unsigned char *lxio_page;
static int lxio_installed,lxio_restored,lxio_rekey,lxio_mass;   /* lxio_mass: native_gate_contact_mass (below) */
static const char *lxio_stage="idle";
static volatile int64_t lxio_failures;
static volatile uint64_t lxio_sorts,lxio_constraints,lxio_unkeyed,lxio_ties,lxio_pointerish,lxio_badcount;
static uint64_t lxio_owner_key_native(uint64_t runtime) {unsigned klass;return world_identity_key(runtime,&klass);}
static uint64_t (*lxio_owner_key)(uint64_t)=lxio_owner_key_native;   /* tests replace it */
static lxio_sort_fn lxio_original_override;                            /* tests (0: the image's sort) */
#include "../lockstep_constraint_trace.h"
static uint64_t lxio_fnv(uint64_t h,const void *p,size_t n) {
    const unsigned char *b=p;for(size_t i=0;i<n;i++) {h^=b[i];h*=0x100000001b3ull;}return h;
}
static int lxio_pointer_like(const unsigned char *p,size_t n) {
    int hits=0;
    for(size_t i=0;i+8<=n;i+=8) {uint64_t q;memcpy(&q,p+i,8);const uint32_t hi=(uint32_t)(q>>32);if(hi>=1u && hi<=0x7fffu) ++hits;}
    return hits;
}
/* A body's part of the key: its owner's portable ID and record (keyed 1), else its BodyID (keyed 0). */
static int lxio_body(const unsigned char *body,uint64_t *key,unsigned char *which) {
    unsigned char *record=NULL,*owner=NULL,*records[2]={NULL,NULL};uint64_t runtime=0;uint32_t id=0;
    *key=0;*which=3;
    if(body && gate_read_local(body+0x50,&record,8) && record && gate_read_local(record,&owner,8) && owner &&
       gate_read_local(owner+8,&runtime,8) && runtime && gate_read_local(owner+LXIO_RECORDS,records,16)) {
        const uint64_t k=lxio_owner_key(runtime);
        if(k && (record==records[0] || record==records[1])) {*key=k;*which=record==records[0]?0u:1u;return 1;}
    }
    if(body) gate_read_local(body+0x70,&id,4);
    *key=id;*which=2;return 0;
}
/* The key of one constraint, Windows' bytes; 0 when unreadable. */
static uint64_t lxio_key(const unsigned char *c) {
    unsigned char *body[2]={NULL,NULL},fields[0x20],point[24];uint32_t count=0;uint64_t h=0xcbf29ce484222325ull;
    if(!gate_read_local(c,body,16) || !gate_read_local(c+0x18,fields,0x20) || !gate_read_local(c+0x38,&count,4)) return 0;
    for(unsigned k=0;k<2;k++) {
        uint64_t key;unsigned char which;
        if(!lxio_body(body[k],&key,&which)) __atomic_add_fetch(&lxio_unkeyed,1,__ATOMIC_SEQ_CST);
        h=lxio_fnv(h,&key,8);h=lxio_fnv(h,&which,1);
    }
    __atomic_add_fetch(&lxio_pointerish,(uint64_t)lxio_pointer_like(fields,0x20),__ATOMIC_SEQ_CST);
    h=lxio_fnv(h,fields,0x20);
    if(!count || count>LXIO_POINTS) {__atomic_add_fetch(&lxio_badcount,1,__ATOMIC_SEQ_CST);count=0;}
    h=lxio_fnv(h,&count,4);
    for(uint32_t k=0;k<count;k++) {
        if(!gate_read_local(c+0x40+LXIO_POINT*k,point,24)) return 0;
        __atomic_add_fetch(&lxio_pointerish,(uint64_t)lxio_pointer_like(point,24),__ATOMIC_SEQ_CST);
        h=lxio_fnv(h,point,24);
    }
    return h?h:1u;
}
static int lxio_rekey_range(uint32_t *begin,uint32_t *end,const unsigned char *manager) {
    unsigned char *array=NULL;
    if(!begin || end<begin || !manager || !gate_read_local(manager+LXIO_ARRAY,&array,8) || !array) return 0;
    for(uint32_t *i=begin;i<end;i++) {
        unsigned char *c=array+(size_t)*i*LXIO_STRIDE;const uint64_t key=lxio_key(c);
        if(!key) return 0;
        memcpy(c+0x10,&key,8);
    }
    return 1;
}
static uint64_t lxio_ties_in(const uint32_t *begin,const uint32_t *end,const unsigned char *manager) {
    unsigned char *array=NULL;uint64_t ties=0,prev=0;
    if(!gate_read_local(manager+LXIO_ARRAY,&array,8) || !array) return 0;
    for(const uint32_t *i=begin;i<end;i++) {
        uint64_t key=0;gate_read_local(array+(size_t)*i*LXIO_STRIDE+0x10,&key,8);
        if(i>begin && key==prev) ++ties;
        prev=key;
    }
    return ties;
}
/* native_gate_contact_mass (Linux gate only, through the same stub; Windows needs nothing): each contact constraint's
 * effective masses in MSVC's order. Jolt's TemplatedCalculateInverseEffectiveMass adds m1 + d1 + m2 + d2 (d the dot
 * product of a body's invI (r x axis) with r x axis); GCC keeps that order, MSVC reassociated it differently per inlined
 * copy. Windows' AddContactConstraint for two dynamic bodies (0x13e43b0): the non-penetration part (0x13e4efb..0x13e4fa2)
 * (((p1x + p1y) + p1z) + m1) + ((p2x + p2y) + (p2z + m2)), both friction parts (0x13e5145..0x13e51cf, 0x13e52c8..
 * 0x13e533e) ((p2x + p2y) + (p2z + m2)) + ((p1x + p1y) + (p1z + m1)), p the products' lanes; the effective mass
 * 1 / inv, 0 with the total lambda 0 for inv 0 (Deactivate). W36s against lx74a: every input of the 54 axis parts equal,
 * the effective masses 1 ulp apart in 9 (ticks 132, 133); the formulas give Windows' word for all 54, Jolt's order
 * Linux's. Here, after X4's setup and before the warm start, every constraint of two dynamic bodies gets them from its
 * own stored vectors and masses (+0x28, +0x30: the scaled inverse masses the setup used). Other motion types are left
 * (counted: other): their Windows copies are not decoded yet, nor GetContactsFromCache's (0x13e82e0, 0x13e8e00, 0x13ea720). */
static volatile uint64_t lxio_mass_constraints,lxio_mass_changed,lxio_mass_other;
static float lxio_inv_nonpen(const float *a,const float *b,float m1,float m2) {
    float p1x=a[6]*a[0],p1y=a[7]*a[1],p1z=a[8]*a[2],p2x=b[9]*b[3],p2y=b[10]*b[4],p2z=b[11]*b[5];
    float d1=p1x+p1y;d1=d1+p1z;d1=d1+m1;
    float q=p2x+p2y,r=p2z+m2;q=q+r;
    return d1+q;
}
static float lxio_inv_friction(const float *a,float m1,float m2) {
    float p1x=a[6]*a[0],p1y=a[7]*a[1],p1z=a[8]*a[2],p2x=a[9]*a[3],p2y=a[10]*a[4],p2z=a[11]*a[5];
    float q=p2x+p2y,r=p2z+m2;q=q+r;
    float s=p1x+p1y,t=p1z+m1;s=s+t;
    return q+s;
}
static void lxio_mass_part(unsigned char *part,int friction,float m1,float m2) {
    float f[16];memcpy(f,part,sizeof(f));
    const float inv=friction?lxio_inv_friction(f,m1,m2):lxio_inv_nonpen(f,f,m1,m2);
    float mass=0.0f,lambda=f[15];
    if(inv==0.0f) lambda=0.0f;else mass=1.0f/inv;
    if(memcmp(&mass,part+0x30,4) || memcmp(&lambda,part+0x3c,4)) {
        memcpy(part+0x30,&mass,4);memcpy(part+0x3c,&lambda,4);__atomic_add_fetch(&lxio_mass_changed,1,__ATOMIC_SEQ_CST);
    }
}
static int lxio_mass_range(const uint32_t *begin,const uint32_t *end,const unsigned char *manager) {
    unsigned char *array=NULL;
    if(!begin || end<begin || !manager || !gate_read_local(manager+LXIO_ARRAY,&array,8) || !array) return 0;
    for(const uint32_t *i=begin;i<end;i++) {
        unsigned char *c=array+(size_t)*i*LXIO_STRIDE,*body[2]={NULL,NULL},type[2]={0,0};uint32_t count=0;float m[2];
        if(!gate_read_local(c,body,16) || !body[0] || !body[1] || !gate_read_local(body[0]+0x78,&type[0],1) ||
           !gate_read_local(body[1]+0x78,&type[1],1) || !gate_read_local(c+0x38,&count,4) || count>LXIO_POINTS ||
           !gate_read_local(c+0x28,&m[0],4) || !gate_read_local(c+0x30,&m[1],4)) return 0;
        if(type[0]!=2 || type[1]!=2) {__atomic_add_fetch(&lxio_mass_other,1,__ATOMIC_SEQ_CST);continue;}
        for(uint32_t k=0;k<count;k++) for(unsigned part=0;part<3;part++) lxio_mass_part(c+0x40+LXIO_POINT*k+0x40*part,part!=0,m[0],m[1]);
        __atomic_add_fetch(&lxio_mass_constraints,1,__ATOMIC_SEQ_CST);
    }
    return 1;
}
static void lxio_sort(uint32_t *begin,uint32_t *end,void *manager) {
    const lxio_sort_fn original=lxio_original_override?lxio_original_override:(lxio_sort_fn)(void*)LX(LXIO_SORT_VA);
    const int rekey=lxio_rekey && end>begin+1;
    if(rekey) {
        __atomic_add_fetch(&lxio_sorts,1,__ATOMIC_SEQ_CST);__atomic_add_fetch(&lxio_constraints,(uint64_t)(end-begin),__ATOMIC_SEQ_CST);
        if(!lxio_rekey_range(begin,end,manager)) __atomic_add_fetch(&lxio_failures,1,__ATOMIC_SEQ_CST);
    }
    if(lxio_mass && end>begin && !lxio_mass_range(begin,end,manager)) __atomic_add_fetch(&lxio_failures,1,__ATOMIC_SEQ_CST);
    original(begin,end,manager);
    if(rekey) __atomic_add_fetch(&lxio_ties,lxio_ties_in(begin,end,manager),__ATOMIC_SEQ_CST);
    if(ctrace_on && end>begin) {
        unsigned char *array=NULL;
        if(manager && gate_read_local((unsigned char*)manager+LXIO_ARRAY,&array,8) && array) ctrace_capture(begin,end,array,stats.issued);
        else InterlockedIncrement64(&ctrace_failures);
    }
}
static unsigned lxio_trace_actor(const unsigned char *body) {unsigned kind;return lxct_actor(body,&kind);}
static void lxio_jump(unsigned char out[14]) {
    const void *to=(const void*)lxio_sort;out[0]=0xff;out[1]=0x25;memset(out+2,0,4);memcpy(out+6,&to,8);
}
static int lxio_page_ok(void) {unsigned char want[14];if(!lxio_page) return 0;lxio_jump(want);return !memcmp(lxio_page,want,14);}
static int lxio_rel(const unsigned char *target,int32_t *out) {
    const intptr_t d=(intptr_t)target-(intptr_t)(LX(LXIO_SITE_VA)+5);
    if(d!=(intptr_t)(int32_t)d) return 0;
    *out=(int32_t)d;return 1;
}
/* 0 the image's sort, 1 our page, -1 anything else. */
static int lxio_call_state(void) {
    unsigned char now[5];int32_t d,want;
    if(!gate_read_local(LX(LXIO_SITE_VA),now,5) || now[0]!=0xe8) return -1;
    memcpy(&d,now+1,4);
    if(lxio_rel(LX(LXIO_SORT_VA),&want) && d==want) return 0;
    return lxio_page && lxio_rel(lxio_page,&want) && d==want?1:-1;
}
static int lxio_pins(void) {
    for(unsigned i=0;i<sizeof(lxio_pin_names)/sizeof(*lxio_pin_names);i++) if(!site_ok(site_named(lxio_pin_names[i]))) return 0;
    return lxio_call_state()>=0;
}
static int lxio_swap(const unsigned char *from_target,const unsigned char *to_target) {
    unsigned char *line=(unsigned char*)((uintptr_t)LX(LXIO_SITE_VA)&~(uintptr_t)15);const unsigned at=(unsigned)(LX(LXIO_SITE_VA)-line);
    unsigned __int128 current,desired;int32_t from,to;
    if(at+5>16 || !lxio_rel(from_target,&from) || !lxio_rel(to_target,&to)) return 0;
    memcpy(&current,line,16);
    if(((unsigned char*)&current)[at]!=0xe8 || memcmp((unsigned char*)&current+at+1,&from,4)) return 0;
    desired=current;memcpy((unsigned char*)&desired+at+1,&to,4);
    return gate_code_swap128(line,current,desired);
}
/* Before the first admitted frame, on the MoveWorker; rekey 0 for the trace alone. */
static int lxio_install(int rekey) {
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {lxio_stage="guard";return 0;}
    if(lxio_call_state()!=0) {lxio_stage="site";return 0;}
    if(!lxio_pins()) {lxio_stage="pins";return 0;}
    if(!lxio_page) lxio_page=gate_near_page(LX(LXIO_SITE_VA));
    if(!lxio_page || mprotect(lxio_page,GATE_PAGE,PROT_READ|PROT_WRITE)) {lxio_stage="memory";return 0;}
    memset(lxio_page,0xcc,GATE_PAGE);lxio_jump(lxio_page);
    if(mprotect(lxio_page,GATE_PAGE,PROT_READ|PROT_EXEC)) {lxio_stage="memory";return 0;}
    lxio_sorts=lxio_constraints=lxio_unkeyed=lxio_ties=lxio_pointerish=lxio_badcount=0;lxio_failures=0;
    lxio_rekey=rekey;
    if(!lxio_swap(LX(LXIO_SORT_VA),lxio_page)) {lxio_rekey=0;lxio_stage="cas";return 0;}
    lxio_installed=1;lxio_restored=0;lxio_stage="armed";return 1;
}
/* gate.island_order(): Windows' iorder_arm. */
static int lxio_arm(void) {
    if(lxio_installed && lxio_rekey) return stats.active;   /* identical rerun after a loading-UI reload */
    if(!world_identity_installed) {lxio_stage="needs";return 0;}
    if(lxio_installed) {
        if(!stats.active || stats.issued || GetCurrentThreadId()!=ui_thread) {lxio_stage="guard";return 0;}
        lxio_rekey=1;lxio_stage="armed";return 1;
    }
    return lxio_install(1);
}
/* gate.constraint_diag(): the CONSTRAINT trace, through the island order's stub (installed here when it is off). */
/* gate.contact_mass(): the effective masses in MSVC's order, through the island order's stub (installed here when it is off). */
static int lxio_mass_arm(void) {
    if(!lxio_installed && !lxio_install(0)) return 0;
    if(!lxio_mass) {lxio_mass_constraints=lxio_mass_changed=lxio_mass_other=0;}
    lxio_mass=1;return 1;
}
static int lxio_mass_status(char *out,size_t cap) {
    const int n=snprintf(out,cap,"tick=%llu armed=%d installed=%d constraints=%llu changed=%llu other=%llu stage=%s failures=%lld",
        (unsigned long long)stats.issued,lxio_mass,lxio_installed,(unsigned long long)lxio_mass_constraints,
        (unsigned long long)lxio_mass_changed,(unsigned long long)lxio_mass_other,lxio_stage,(long long)lxio_failures);
    return n>0 && (size_t)n<cap?n:0;
}
static int lxio_trace_arm(void) {
    if(!lxio_installed && !lxio_install(0)) return 0;
    ctrace_actor=lxio_trace_actor;ctrace_on=1;return 1;
}
static int lxio_owned(void) {return !lxio_installed || (lxio_call_state()==1 && lxio_page_ok());}
/* Main-thread tick check; any failure stops the gate with reason 215. */
static int lxio_check(void) {
    if(__atomic_load_n(&lxio_failures,__ATOMIC_SEQ_CST)) {lxio_stage="unreadable";return 0;}
    if(InterlockedCompareExchange64(&ctrace_failures,0,0)) {lxio_stage="trace";return 0;}
    const char *why=!lxio_owned()?"site_changed":!lxio_pins()?"block_changed":NULL;
    if(why) {lxio_stage=why;__atomic_add_fetch(&lxio_failures,1,__ATOMIC_SEQ_CST);return 0;}
    return 1;
}
static void lxio_restore(void) {
    if(!lxio_installed) return;
    ctrace_on=0;lxio_mass=0;
    if(lxio_call_state()==1 && lxio_swap(lxio_page,LX(LXIO_SORT_VA))) {lxio_installed=0;lxio_restored=1;} /* the page stays */
}
static int lxio_status(char *out,size_t cap) {
    const int n=snprintf(out,cap,"installed=%d sorts=%llu constraints=%llu unkeyed=%llu ties=%llu pointerish=%llu badcount=%llu restored=%d "
        "stage=%s failures=%lld",lxio_installed && lxio_rekey,(unsigned long long)lxio_sorts,(unsigned long long)lxio_constraints,
        (unsigned long long)lxio_unkeyed,(unsigned long long)lxio_ties,(unsigned long long)lxio_pointerish,
        (unsigned long long)lxio_badcount,lxio_restored,lxio_stage,(long long)lxio_failures);
    return n>0 && (size_t)n<cap?n:0;
}
static int lua_island_order(lua_State *L) {p_lua_pushnumber(L,lxio_arm());return 1;}
static int lua_island_order_status(lua_State *L) {
    char out[384];const int n=accept_ui()?lxio_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_constraint_diag(lua_State *L) {p_lua_pushnumber(L,lxio_trace_arm());return 1;}
/* gate.constraint(i): 0 the rows held, 1.. a row's text (the shared CONSTRAINT text). */
static int lua_constraint(lua_State *L) {
    const double index=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(index) || index<0 || floor(index)!=index) {p_lua_pushnil(L);return 1;}
    if(index==0) {p_lua_pushnumber(L,ctrace_rows_held());return 1;}
    static char out[8192];const int n=ctrace_row_text((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_constraint_clear(lua_State *L) {
    const int ok=GetCurrentThreadId()==ui_thread;if(ok) ctrace_clear();
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_contact_mass(lua_State *L) {p_lua_pushnumber(L,lxio_mass_arm());return 1;}
static int lua_contact_mass_status(lua_State *L) {
    char out[256];const int n=accept_ui()?lxio_mass_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_constraint_status(lua_State *L) {
    char out[200];const int n=accept_ui()?ctrace_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}

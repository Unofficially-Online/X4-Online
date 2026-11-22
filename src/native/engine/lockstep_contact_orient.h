/* Determinism fix, a default (native_gate_contact_orient = true; needs native_gate_world_identities): the two bodies of a
 * contact pair are put in an order every client computes alike, not by their BodyIDs.
 *
 * Jolt orders the bodies of a pair by BodyID in five places: ProcessBodyPair 0x13d5f50 (body 1 the higher motion type, and
 * for equal types the lower BodyID: the narrow phase then runs in body 1's space), and ContactConstraintManager's
 * GetContactsFromCache 0x13e10b0, AddBodyPair 0x13e1b00, AddContactConstraint 0x13e1df0 and OnCCDContactAdded (each swaps
 * the bodies so that body 1 has the lower BodyID: the contact caches are keyed that way, the constraint is built that way).
 * A contact's arithmetic is not symmetric in its two bodies (the non-penetration part's r1 + u reaches body 2's contact
 * point from body 1's centre of mass, r2 its own), so the same contact between the same bodies gives other impulses when
 * the bodies come the other way round. BodyIDs come from the body manager's free list in creation order: a client that
 * loaded the world (a hot joiner) holds other IDs for the same bodies [hq129, 19 Nov: 61 of 71 live bodies after a late
 * join], and bodies made in one frame take theirs in heap-address order (lockstep_body_order.h), which is local to a
 * process. The island order (lockstep_island_order.h) put each island's contacts in a portable order; which body of a
 * contact is body 1 still followed the BodyIDs.
 *
 * Fix: at each of the five places the compare of the two BodyIDs becomes a jump to a stub that calls corient_cf(x, y, site)
 * and sets the carry flag from its answer, so the native jae / cmovae after it take the bodies in the portable order: first
 * bodies whose owner has a portable ID (the XPhys record body +0x50, its owner +0, the owner's ID and which of its records
 * +0x260/+0x268 the body is, as lockstep_island_order.h iorder_body) by (owner's ID, which), then bodies whose owner has
 * none by (the owner's runtime ID, which), then bodies without an owner by their BodyIDs as before (corient_body). All five
 * places use the same order, so the caches stay consistent: they are keyed by the pair in that order instead of the BodyID
 * order. The stub saves rax, rcx, rdx, r8-r11 and xmm0-5, replays the instructions its jump displaced and jumps back.
 * observe: the native BodyID order kept, the same counts and digest (a diagnostic); reverse: every pair the other way round
 * from the portable order (a diagnostic: what turning contacts round does to a run). Counted per place: calls, flips (the
 * portable order is not the BodyID order of this process); pairs with a runtime-keyed body (the last such owner's runtime
 * ID and vtable shown) and with an ownerless one; and the agree digest: the sum over
 * GetContactsFromCache's calls (every broad phase pair of a step, cache hit or not) of a hash of the tick, the pair in
 * portable order and whether this process's BodyIDs agree with it. Two clients with equal physics and equal agree
 * digests oriented every pair alike natively too; a joiner's other digest shows the pairs the native order turned round.
 * Armed before the first frame on the UI thread, every client and a joiner alike. Pins: each place's block (the patched
 * bytes restored for the hash). Any failure stops the gate with reason 219. Windows only: the Linux gate needs the twin
 * before Windows and Linux players share a session with this on. */
#define CORIENT_SITES 5u
#define CORIENT_STUB 0x180u
#define CORIENT_PAGE 0x1000u
#define CORIENT_STOP_REASON 219u
enum {CORIENT_OFF,CORIENT_OBSERVE,CORIENT_ON,CORIENT_REVERSE};
enum {CR_RAX=0,CR_RCX=1,CR_RDX=2,CR_RSI=6,CR_RDI=7,CR_R8=8,CR_R9=9,CR_R10=10,CR_R11=11,CR_R14=14,CR_R15=15};
typedef struct {
    const char *name;
    unsigned rva,resume,taken;       /* the jump's 5 bytes at rva; back at resume; a branch place's jae target (0: cmov) */
    unsigned char x,y;               /* the native code's carry = x.ID < y.ID; the stub's = corient_cf(x, y) */
    unsigned char pre_n,post_n;      /* replayed before the call (pre) and after the carry is set (post) */
    unsigned char pre[8],post[24],orig[5];
    unsigned pin,pin_size;const char *pin_sha;
} corient_site;
static const corient_site corient_sites[CORIENT_SITES]={
    /* ProcessBodyPair, equal motion types: cmp r8d,r10d (B.ID, A.ID); jae keep -> swap A (rsi) and B (rdi) */
    {"pair",0x13d5ff2u,0x13d5ff7u,0x13d6000u,CR_RDI,CR_RSI,0,0,{0},{0},{0x45,0x3b,0xc2,0x73,0x09},
     0x13d5fdfu,0x24u,"cc447324efff09734352a38830261f397428118f4124655ae6dad79d85bfbabb"},
    /* GetContactsFromCache: cmp r10d,[r9+0x70] (inBody1 r15, inBody2 r9); the two out flags cleared; cmovae swap */
    {"cache",0x13e10f1u,0x13e1104u,0,CR_R15,CR_R9,0,15,{0},
     {0xc6,0x00,0x00,0x4d,0x0f,0x43,0xf7,0x41,0xc6,0x00,0x00,0x4d,0x0f,0x43,0xf9},{0x45,0x3b,0x51,0x70,0xc6},
     0x13e10e6u,0x22u,"225b57072bf1c51b58e5acb9977c9ff34bd13eb889422b67fc6d0228c84ee85d"},
    /* AddBodyPair: cmp r10d,[r9+0x70] (inBody1 r8, inBody2 r9); mov rsi,r9; mov rdi,r8; mov [rsp+0xb8],rbx; cmovae swap */
    {"body_pair",0x13e1b14u,0x13e1b2eu,0,CR_R8,CR_R9,0,22,{0},
     {0x49,0x8b,0xf1,0x49,0x8b,0xf8,0x48,0x89,0x9c,0x24,0xb8,0x00,0x00,0x00,0x49,0x0f,0x43,0xf9,0x49,0x0f,0x43,0xf0},
     {0x45,0x3b,0x51,0x70,0x49},
     0x13e1b0du,0x24u,"059144f5f1b9a3169f8bd880fdbe462860db18c31fd0edea686b2ba8ca40e9c9"},
    /* AddContactConstraint: mov eax,[r9+0x70]; mov rbp,rdx; cmp [rcx+0x70],eax (inBody2 rcx, inBody1 r9); jae keep */
    {"constraint",0x13e1e3au,0x13e1e46u,0x13e1e72u,CR_RCX,CR_R9,7,0,{0x41,0x8b,0x41,0x70,0x48,0x8b,0xea},{0},
     {0x41,0x8b,0x41,0x70,0x48},
     0x13e1e2du,0x53u,"3100839628da2adaf6325d1aa6a7a08ea35ee60b4c7e7b3280a9c0aef4d5d94c"},
    /* OnCCDContactAdded: cmp [r15+0x70],eax (inBody2 r15, eax inBody1 r14's ID); jae keep */
    {"ccd",0x13e209bu,0x13e20a1u,0x13e20cbu,CR_R15,CR_R14,0,0,{0},{0},{0x41,0x39,0x47,0x70,0x73},
     0x13e2068u,0x63u,"f3578e924f97f985b6d0e65d43c22644d0a207abe901cde2aed16db5b6c24071"},
};
static unsigned char *corient_page;
static int corient_mode,corient_installed,corient_restored;
static unsigned corient_done;
static const char *corient_stage="idle";
static volatile LONG64 corient_failures,corient_calls[CORIENT_SITES],corient_flips[CORIENT_SITES],corient_unkeyed,corient_bodyid,corient_agree;
static uint64_t corient_hash(uint64_t h,const void *p,size_t n) {
    const unsigned char *b=p;for(size_t i=0;i<n;i++) {h^=b[i];h*=0x100000001b3ull;}return h;
}
/* A body's place in the order: rank 0 its owner's portable ID and record (0/1), rank 1 the owner's runtime ID (the gate keeps
 * runtime IDs alike on every client: the rid digest part) and record (0/1, 2 another record of that owner), rank 2 its BodyID
 * (no owner). [hqc2, 22 Nov: from tick 12060 one body of nearly every contact had an owner without a portable ID.] */
static volatile LONG64 corient_last_owner,corient_last_vtable;
static unsigned corient_body(const unsigned char *body,uint64_t *key,unsigned char *which) {
    unsigned char *record=NULL,*owner=NULL,*records[2]={NULL,NULL};uint64_t runtime=0;uint32_t id=0;
    if(body && read_local(body+0x50,&record,8) && record && read_local(record,&owner,8) && owner && read_local(owner+8,&runtime,8) && runtime) {
        const int mine=read_local(owner+0x260,records,16) && (record==records[0] || record==records[1]);
        *which=mine?(record==records[0]?0u:1u):2u;
        const uint64_t k=mine?iorder_owner_key(runtime):0;
        if(k) {*key=k;return 0;}
        void *vt=NULL;read_local(owner,&vt,8);
        InterlockedExchange64(&corient_last_owner,(LONG64)runtime);
        InterlockedExchange64(&corient_last_vtable,(LONG64)((uintptr_t)vt>=(uintptr_t)image?(uintptr_t)vt-(uintptr_t)image:0));
        *key=runtime;return 1;
    }
    if(body) read_local(body+0x70,&id,4);
    *key=id;*which=3;return 2;
}
/* The carry the native compare at `site` would leave for x.ID < y.ID, in the portable order (CORIENT_ON) or the native one. */
static int corient_cf(const unsigned char *x,const unsigned char *y,unsigned site) {
    uint32_t ix=0,iy=0;
    if(!x || !y || site>=CORIENT_SITES || !read_local(x+0x70,&ix,4) || !read_local(y+0x70,&iy,4)) {
        InterlockedIncrement64(&corient_failures);return ix<iy;
    }
    const int native=ix<iy;
    uint64_t kx,ky;unsigned char wx,wy;
    const unsigned rx=corient_body(x,&kx,&wx),ry=corient_body(y,&ky,&wy);
    int less;
    if(rx!=ry) less=rx<ry;               /* a portable owner first, then a runtime-keyed one, then an ownerless body */
    else if(rx==2u) less=native;         /* two ownerless bodies: their BodyIDs, as before */
    else if(kx!=ky) less=kx<ky;
    else if(wx!=wy) less=wx<wy;
    else less=native;                    /* one owner's other records: their BodyIDs */
    InterlockedIncrement64(&corient_calls[site]);
    if(less!=native) InterlockedIncrement64(&corient_flips[site]);
    if(rx==1u || ry==1u) InterlockedIncrement64(&corient_unkeyed);
    if(rx==2u || ry==2u) InterlockedIncrement64(&corient_bodyid);
    else if(site==1u) {
        const uint64_t tick=stats.issued,k1=less?kx:ky,k2=less?ky:kx;const unsigned char w1=less?wx:wy,w2=less?wy:wx,agree=less==native;
        uint64_t h=0xcbf29ce484222325ull;
        h=corient_hash(h,&tick,8);h=corient_hash(h,&k1,8);h=corient_hash(h,&w1,1);h=corient_hash(h,&k2,8);h=corient_hash(h,&w2,1);
        h=corient_hash(h,&agree,1);
        InterlockedExchangeAdd64(&corient_agree,(LONG64)h);
    }
    return corient_mode==CORIENT_ON?less:corient_mode==CORIENT_REVERSE?!less:native;
}
static size_t corient_mov(unsigned char *p,unsigned dst,unsigned src) {   /* mov dst,src (64-bit) */
    if(dst==src) return 0;
    p[0]=(unsigned char)(0x48|(dst>=8?4:0)|(src>=8?1:0));p[1]=0x8b;p[2]=(unsigned char)(0xc0|((dst&7)<<3)|(src&7));return 3;
}
/* The stub of place `index` written to `stub`, to run at `at`, its targets in `base` (the image; a test's page). 0 when it
 * does not fit or a target is out of a jump's reach. */
static size_t corient_write_stub(unsigned char *stub,unsigned char *at,const corient_site *s,unsigned index,unsigned char *base,void *helper) {
    static const unsigned char save[]={
        0x48,0x81,0xec,0xc0,0x00,0x00,0x00,                                         /* sub rsp,0xc0 */
        0x48,0x89,0x84,0x24,0x80,0,0,0, 0x48,0x89,0x8c,0x24,0x88,0,0,0,             /* rax, rcx */
        0x48,0x89,0x94,0x24,0x90,0,0,0, 0x4c,0x89,0x84,0x24,0x98,0,0,0,             /* rdx, r8 */
        0x4c,0x89,0x8c,0x24,0xa0,0,0,0, 0x4c,0x89,0x94,0x24,0xa8,0,0,0,             /* r9, r10 */
        0x4c,0x89,0x9c,0x24,0xb0,0,0,0,                                             /* r11 */
        0x0f,0x11,0x44,0x24,0x20, 0x0f,0x11,0x4c,0x24,0x30, 0x0f,0x11,0x54,0x24,0x40,   /* xmm0-2 */
        0x0f,0x11,0x5c,0x24,0x50, 0x0f,0x11,0x64,0x24,0x60, 0x0f,0x11,0x6c,0x24,0x70,   /* xmm3-5 */
    };
    static const unsigned char restore[]={
        0x89,0x84,0x24,0xb8,0,0,0,                                                  /* mov [rsp+0xb8],eax */
        0x0f,0x10,0x44,0x24,0x20, 0x0f,0x10,0x4c,0x24,0x30, 0x0f,0x10,0x54,0x24,0x40,
        0x0f,0x10,0x5c,0x24,0x50, 0x0f,0x10,0x64,0x24,0x60, 0x0f,0x10,0x6c,0x24,0x70,
        0x48,0x8b,0x84,0x24,0x80,0,0,0, 0x48,0x8b,0x8c,0x24,0x88,0,0,0,
        0x48,0x8b,0x94,0x24,0x90,0,0,0, 0x4c,0x8b,0x84,0x24,0x98,0,0,0,
        0x4c,0x8b,0x8c,0x24,0xa0,0,0,0, 0x4c,0x8b,0x94,0x24,0xa8,0,0,0,
        0x4c,0x8b,0x9c,0x24,0xb0,0,0,0,
        0x0f,0xba,0xa4,0x24,0xb8,0,0,0,0x00,                                        /* bt dword [rsp+0xb8],0: CF = answer */
        0x48,0x8d,0xa4,0x24,0xc0,0,0,0,                                             /* lea rsp,[rsp+0xc0] (flags kept) */
    };
    if((s->x==CR_RDX && s->y==CR_RCX) || s->pre_n>sizeof(s->pre) || s->post_n>sizeof(s->post)) return 0;
    unsigned char *p=stub;
    memset(stub,0xcc,CORIENT_STUB);
    memcpy(p,s->pre,s->pre_n);p+=s->pre_n;
    memcpy(p,save,sizeof(save));p+=sizeof(save);
    if(s->x==CR_RDX) {p+=corient_mov(p,CR_RCX,s->x);p+=corient_mov(p,CR_RDX,s->y);}
    else {p+=corient_mov(p,CR_RDX,s->y);p+=corient_mov(p,CR_RCX,s->x);}
    *p++=0x41;*p++=0xb8;memcpy(p,&index,4);p+=4;                                  /* mov r8d,index */
    *p++=0x48;*p++=0xb8;memcpy(p,&helper,8);p+=8;                                 /* mov rax,helper */
    *p++=0xff;*p++=0xd0;                                                          /* call rax */
    memcpy(p,restore,sizeof(restore));p+=sizeof(restore);
    memcpy(p,s->post,s->post_n);p+=s->post_n;
    if(s->taken) {
        int64_t d=(int64_t)(intptr_t)(base+s->taken)-(int64_t)(intptr_t)(at+(p-stub)+6);
        if(d<INT32_MIN || d>INT32_MAX) return 0;
        int32_t r=(int32_t)d;*p++=0x0f;*p++=0x83;memcpy(p,&r,4);p+=4;            /* jae taken */
    }
    int64_t d=(int64_t)(intptr_t)(base+s->resume)-(int64_t)(intptr_t)(at+(p-stub)+5);
    if(d<INT32_MIN || d>INT32_MAX) return 0;
    {int32_t r=(int32_t)d;*p++=0xe9;memcpy(p,&r,4);p+=4;}                          /* jmp resume */
    return (size_t)(p-stub)<=CORIENT_STUB?(size_t)(p-stub):0;
}
static int32_t corient_rel(unsigned i) {
    return (int32_t)((intptr_t)(corient_page+CORIENT_STUB*i)-(intptr_t)(image+corient_sites[i].rva+5));
}
/* 0 original, 1 ours, -1 anything else. */
static int corient_state_of(unsigned i) {
    unsigned char now[5];if(!read_local(image+corient_sites[i].rva,now,5)) return -1;
    if(!memcmp(now,corient_sites[i].orig,5)) return 0;
    if(corient_page && now[0]==0xe9) {int32_t d;memcpy(&d,now+1,4);if(d==corient_rel(i)) return 1;}
    return -1;
}
static int corient_pins(void) {
    for(unsigned i=0;i<CORIENT_SITES;i++) {
        const corient_site *s=&corient_sites[i];unsigned char copy[0x80];
        if(corient_state_of(i)<0 || s->pin_size>sizeof(copy) || s->rva<s->pin || s->rva+5>s->pin+s->pin_size ||
           !read_local(image+s->pin,copy,s->pin_size)) return 0;
        memcpy(copy+(s->rva-s->pin),s->orig,5);
        if(!hash_ok(copy,s->pin_size,s->pin_sha)) return 0;
    }
    return 1;
}
static unsigned char *corient_alloc_page(void) {
    const uintptr_t at=(uintptr_t)(image+0x13e0000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            unsigned char *p=VirtualAlloc((void *)(sign<0?at-distance:at+distance),CORIENT_PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int ok=1;
            for(unsigned i=0;i<CORIENT_SITES && ok;i++) {
                const intptr_t lo=(intptr_t)p-(intptr_t)(image+corient_sites[i].rva+5),hi=(intptr_t)(p+CORIENT_PAGE)-(intptr_t)(image+corient_sites[i].rva);
                ok=lo>INT32_MIN+0x1000 && hi<INT32_MAX-0x1000;
            }
            if(ok) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void corient_restore(void) {
    if(!corient_installed) return;
    for(unsigned i=0;i<CORIENT_SITES;i++) {
        if(!((corient_done>>i)&1u) || corient_state_of(i)!=1) continue;
        unsigned char now[5];memcpy(now,image+corient_sites[i].rva,5);
        if(view_cas_bytes(corient_sites[i].rva,now,corient_sites[i].orig,5)) corient_done&=~(1u<<i);
    }
    if(!corient_done) {corient_installed=0;corient_restored=1;}
}
/* Before the first frame on the UI thread (needs the world identities); a rerun with the same mode is accepted. */
static int corient_arm(int mode) {
    if(corient_installed) return stats.active && corient_mode==mode;
    if(mode!=CORIENT_OBSERVE && mode!=CORIENT_ON && mode!=CORIENT_REVERSE) {corient_stage="mode";return 0;}
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed) {corient_stage="needs";return 0;}
    for(unsigned i=0;i<CORIENT_SITES;i++) if(corient_state_of(i)!=0) {corient_stage="site";return 0;}
    if(!corient_pins()) {corient_stage="pins";return 0;}
    if(!corient_page) corient_page=corient_alloc_page();
    if(!corient_page) {corient_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(corient_page,CORIENT_PAGE,PAGE_READWRITE,&old)) {corient_stage="page";return 0;}
    for(unsigned i=0;i<CORIENT_SITES;i++)
        if(!corient_write_stub(corient_page+CORIENT_STUB*i,corient_page+CORIENT_STUB*i,&corient_sites[i],i,image,(void *)corient_cf)) {
            corient_stage="stub";return 0;
        }
    if(!VirtualProtect(corient_page,CORIENT_PAGE,PAGE_EXECUTE_READ,&ignored)) {corient_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),corient_page,CORIENT_PAGE);
    for(unsigned i=0;i<CORIENT_SITES;i++) {corient_calls[i]=corient_flips[i]=0;}
    corient_unkeyed=corient_bodyid=corient_agree=corient_failures=corient_last_owner=corient_last_vtable=0;corient_restored=0;
    corient_mode=mode;corient_installed=1;
    for(unsigned i=0;i<CORIENT_SITES;i++) {
        unsigned char patch[5]={0xe9};const int32_t d=corient_rel(i);memcpy(patch+1,&d,4);
        if(!view_cas_bytes(corient_sites[i].rva,corient_sites[i].orig,patch,5)) {
            corient_stage="cas";corient_restore();corient_installed=0;corient_mode=CORIENT_OFF;return 0;
        }
        corient_done|=1u<<i;
    }
    corient_stage="armed";return 1;
}
static int corient_owned(void) {
    if(!corient_installed) return 1;
    for(unsigned i=0;i<CORIENT_SITES;i++) if((corient_done>>i)&1u) {
        unsigned char want[CORIENT_STUB];
        if(corient_state_of(i)!=1 || !corient_write_stub(want,corient_page+CORIENT_STUB*i,&corient_sites[i],i,image,(void *)corient_cf) ||
           memcmp(corient_page+CORIENT_STUB*i,want,CORIENT_STUB)) return 0;
    }
    return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 219. */
static int corient_check(void) {
    if(InterlockedCompareExchange64(&corient_failures,0,0)) {corient_stage="unreadable";return 0;}
    const char *why=!corient_owned()?"site_changed":!corient_pins()?"block_changed":NULL;
    if(why) {corient_stage=why;InterlockedIncrement64(&corient_failures);return 0;}
    return 1;
}
static int corient_status(char *out,size_t cap) {
    unsigned long long c[CORIENT_SITES],f[CORIENT_SITES];
    for(unsigned i=0;i<CORIENT_SITES;i++) {
        c[i]=(unsigned long long)InterlockedCompareExchange64(&corient_calls[i],0,0);
        f[i]=(unsigned long long)InterlockedCompareExchange64(&corient_flips[i],0,0);
    }
    const int n=snprintf(out,cap,"installed=%d mode=%s calls=%llu/%llu/%llu/%llu/%llu flips=%llu/%llu/%llu/%llu/%llu unkeyed=%llu "
        "bodyid=%llu last=%llx/%llx agree=%016llx restored=%d stage=%s failures=%lld",corient_installed,
        corient_mode==CORIENT_ON?"on":corient_mode==CORIENT_OBSERVE?"observe":corient_mode==CORIENT_REVERSE?"reverse":"off",c[0],c[1],c[2],c[3],c[4],f[0],f[1],f[2],f[3],f[4],
        (unsigned long long)InterlockedCompareExchange64(&corient_unkeyed,0,0),
        (unsigned long long)InterlockedCompareExchange64(&corient_bodyid,0,0),
        (unsigned long long)InterlockedCompareExchange64(&corient_last_owner,0,0),
        (unsigned long long)InterlockedCompareExchange64(&corient_last_vtable,0,0),
        (unsigned long long)InterlockedCompareExchange64(&corient_agree,0,0),corient_restored,corient_stage,
        (long long)InterlockedCompareExchange64(&corient_failures,0,0));
    return n>0 && (size_t)n<cap?n:0;
}

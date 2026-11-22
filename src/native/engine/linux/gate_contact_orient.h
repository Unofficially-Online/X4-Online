/* native_gate_contact_orient on the Linux gate (Windows: lockstep_contact_orient.h, the same order, counts and status
 * text): the two bodies of a contact pair are put in an order every client computes alike, not by their BodyIDs.
 * Included by gate_linux.c after gate_island_order.h (lxio_owner_key, LXIO_RECORDS).
 *
 * Linux sites. GCC inlined Jolt's ContactConstraintManager into fewer functions than MSVC, but each of Windows' five
 * BodyID decisions about a pair has exactly one compare here (every other compare of two BodyIDs in the Jolt code is the
 * island sort's or a min/max key, symmetric in the pair):
 *   0 pair        ProcessBodyPair 0x1b1fae0, equal motion types: cmp esi,ecx (the pair's B, A) at 0x1b1ff1c; jb keeps B
 *                 (rbp) as body 1, else swap (Windows 0x13d5ff2). The window starts at 0x1b1ff18 (cmp dl,al; jne, both
 *                 replayed) to stay in one 16-byte line.
 *   1 cache       GetContactsFromCache 0x1af6210 (the two out flags cleared first, as Windows'): cmp eax,ebp (inBody1 rdx,
 *                 inBody2 rcx) at 0x1af6238; jae keeps (Windows 0x13e10f1). The window starts at the second flag store.
 *   2 body_pair   AddBodyPair 0x1afa180: cmp eax,r9d (rdx, rcx) at 0x1afa193; jae keeps (Windows 0x13e1b14).
 *   3 constraint  AddContactConstraint 0x1afa480 (every templated motion-type form inlined): cmp [r8+0x70],eax (inBody2 r8,
 *                 inBody1 rcx) at 0x1afa4ca; jb -> SwapShapes (Windows 0x13e1e3a).
 *   4 ccd         OnCCDContactAdded 0x1afeae0: cmp [r12+0x70],eax (inBody2 r12, inBody1 rbp) at 0x1afeb7a; jb -> swap
 *                 (Windows 0x13e209b).
 * Each window (whole instructions, the compare and its branch, nothing branches into them) becomes a jmp to a stub: the
 * replayed instructions before the compare, then lea rsp past the red zone, push y, push x, push the site and call the
 * common routine lxco_common, which saves rax..r11 and xmm0-15, calls lxco_answer(x, y, site) and sets CF to the
 * answer (ZF 0) with a cmp; the stub drops its pushes with lea (flags kept) and takes the branch it displaced (its own
 * target, then the resume address). No code after a resume point reads the flags before it writes them. Pins: each
 * function's head through its site. Armed before the first frame on the UI thread; every gate tick re-checks the
 * windows, the stubs and the pins (stop reason 219, Windows' number); stop restores. */
#define LXCO_SITES 5u
#define LXCO_STUB 0x60u
#define LXCO_STOP_REASON 219u
enum {LXCO_OFF,LXCO_OBSERVE,LXCO_ON,LXCO_REVERSE};
enum {LR_RAX=0,LR_RCX=1,LR_RDX=2,LR_RBX=3,LR_RBP=5,LR_RSI=6,LR_RDI=7,LR_R8=8,LR_R9=9,LR_R12=12};
typedef struct {
    const char *name;
    uint32_t va,resume,taken;        /* the window at va; the branch's target; resume after the window */
    unsigned char window,x,y;        /* window bytes; the bodies of the compare's first (x) and second (y) operand */
    unsigned char jcc;               /* the displaced branch's condition opcode (0x82 jb, 0x83 jae; 0x0f prefix emitted) */
    unsigned char pre_n,pre[8];      /* replayed before the call */
    uint32_t pre_jne;                /* the pair site: cmp dl,al; jne here (replayed with its own target) */
    unsigned char orig[12];
} lxco_site;
static const lxco_site lxco_sites[LXCO_SITES]={
    {"pair",0x1b1ff18u,0x1b1ff24u,0x1b1fb37u,12,LR_RBP,LR_R12,0x82,2,{0x38,0xc2},0x1b1ff24u,
     {0x38,0xc2,0x75,0x08,0x39,0xce,0x0f,0x82,0x13,0xfc,0xff,0xff}},
    {"cache",0x1af6234u,0x1af623cu,0x1af624bu,8,LR_RDX,LR_RCX,0x83,4,{0x41,0xc6,0x00,0x00},0,
     {0x41,0xc6,0x00,0x00,0x39,0xe8,0x73,0x0f}},
    {"body_pair",0x1afa193u,0x1afa198u,0x1afa1a9u,5,LR_RDX,LR_RCX,0x83,0,{0},0,
     {0x44,0x39,0xc8,0x73,0x11}},
    {"constraint",0x1afa4cau,0x1afa4d4u,0x1afaa78u,10,LR_R8,LR_RCX,0x82,0,{0},0,
     {0x41,0x39,0x40,0x70,0x0f,0x82,0xa4,0x05,0x00,0x00}},
    {"ccd",0x1afeb7au,0x1afeb85u,0x1afed58u,11,LR_R12,LR_RBP,0x82,0,{0},0,
     {0x41,0x39,0x44,0x24,0x70,0x0f,0x82,0xd3,0x01,0x00,0x00}},
};
static const char *const lxco_pin_names[]={"corient_pair","corient_pair_window","corient_cache","corient_constraint","corient_ccd",
                                          "corient_body_pair"};
static unsigned char *lxco_page;
static int lxco_mode,lxco_installed,lxco_restored;
static unsigned lxco_done;
static const char *lxco_stage="idle";
static volatile int64_t lxco_failures,lxco_calls[LXCO_SITES],lxco_flips[LXCO_SITES],lxco_unkeyed,lxco_bodyid,lxco_agree,
                        lxco_last_owner,lxco_last_vtable;
static uint64_t lxco_hash(uint64_t h,const void *p,size_t n) {
    const unsigned char *b=p;for(size_t i=0;i<n;i++) {h^=b[i];h*=0x100000001b3ull;}return h;
}
/* Windows' corient_body: rank 0 the owner's portable ID and record (0/1), rank 1 the owner's runtime ID and record (0/1,
 * 2 another record), rank 2 the BodyID (no owner). The owner's records sit at +0x258/+0x260 here (Windows +0x260/+0x268). */
static unsigned lxco_body(const unsigned char *body,uint64_t *key,unsigned char *which) {
    unsigned char *record=NULL,*owner=NULL,*records[2]={NULL,NULL};uint64_t runtime=0;uint32_t id=0;
    if(body && gate_read_local(body+0x50,&record,8) && record && gate_read_local(record,&owner,8) && owner &&
       gate_read_local(owner+8,&runtime,8) && runtime) {
        const int mine=gate_read_local(owner+LXIO_RECORDS,records,16) && (record==records[0] || record==records[1]);
        *which=mine?(record==records[0]?0u:1u):2u;
        const uint64_t k=mine?lxio_owner_key(runtime):0;
        if(k) {*key=k;return 0;}
        uintptr_t vt=0;gate_read_local(owner,&vt,8);
        __atomic_store_n(&lxco_last_owner,(int64_t)runtime,__ATOMIC_RELAXED);
        __atomic_store_n(&lxco_last_vtable,(int64_t)(vt>=LX_IMAGE_BASE?vt-LX_IMAGE_BASE:0),__ATOMIC_RELAXED);
        *key=runtime;return 1;
    }
    if(body) gate_read_local(body+0x70,&id,4);
    *key=id;*which=3;return 2;
}
/* The carry the native compare at `site` leaves for x.ID < y.ID, in the portable order (on) or the native one (Windows'
 * corient_cf, line for line). */
static int lxco_cf(const unsigned char *x,const unsigned char *y,unsigned site) {
    uint32_t ix=0,iy=0;
    if(!x || !y || site>=LXCO_SITES || !gate_read_local(x+0x70,&ix,4) || !gate_read_local(y+0x70,&iy,4)) {
        __atomic_add_fetch(&lxco_failures,1,__ATOMIC_SEQ_CST);return ix<iy;
    }
    const int native=ix<iy;
    uint64_t kx,ky;unsigned char wx,wy;
    const unsigned rx=lxco_body(x,&kx,&wx),ry=lxco_body(y,&ky,&wy);
    int less;
    if(rx!=ry) less=rx<ry;
    else if(rx==2u) less=native;
    else if(kx!=ky) less=kx<ky;
    else if(wx!=wy) less=wx<wy;
    else less=native;
    __atomic_add_fetch(&lxco_calls[site],1,__ATOMIC_RELAXED);
    if(less!=native) __atomic_add_fetch(&lxco_flips[site],1,__ATOMIC_RELAXED);
    if(rx==1u || ry==1u) __atomic_add_fetch(&lxco_unkeyed,1,__ATOMIC_RELAXED);
    if(rx==2u || ry==2u) __atomic_add_fetch(&lxco_bodyid,1,__ATOMIC_RELAXED);
    else if(site==1u) {
        const uint64_t tick=stats.issued,k1=less?kx:ky,k2=less?ky:kx;const unsigned char w1=less?wx:wy,w2=less?wy:wx,agree=less==native;
        uint64_t h=0xcbf29ce484222325ull;
        h=lxco_hash(h,&tick,8);h=lxco_hash(h,&k1,8);h=lxco_hash(h,&w1,1);h=lxco_hash(h,&k2,8);h=lxco_hash(h,&w2,1);
        h=lxco_hash(h,&agree,1);
        __atomic_add_fetch(&lxco_agree,(int64_t)h,__ATOMIC_RELAXED);
    }
    return lxco_mode==LXCO_ON?less:lxco_mode==LXCO_REVERSE?!less:native;
}
/* The common routine's C half: the byte the final cmp reads, 0 for a set carry, 2 for a clear one (ZF clear either way). */
__attribute__((used,visibility("hidden"))) unsigned lxco_answer(const unsigned char *x,const unsigned char *y,unsigned site) {
    return lxco_cf(x,y,site)?0u:2u;
}
/* lxco_common: entered by `call` from a site stub with [rsp+8] the site, [rsp+16] x, [rsp+24] y; keeps every register
 * (rax..r11, xmm0-15; the callee-saved ones lxco_answer keeps) and returns with CF = the answer, ZF clear. */
__asm__(
    ".text\n.p2align 4\n.globl lxco_common\n.hidden lxco_common\n.type lxco_common,@function\nlxco_common:\n"
    "push %rbp\nmov %rsp,%rbp\nsub $0x200,%rsp\nand $-16,%rsp\n"
    "mov %rax,0x100(%rsp)\nmov %rcx,0x108(%rsp)\nmov %rdx,0x110(%rsp)\nmov %rsi,0x118(%rsp)\nmov %rdi,0x120(%rsp)\n"
    "mov %r8,0x128(%rsp)\nmov %r9,0x130(%rsp)\nmov %r10,0x138(%rsp)\nmov %r11,0x140(%rsp)\n"
    "movdqu %xmm0,0x00(%rsp)\nmovdqu %xmm1,0x10(%rsp)\nmovdqu %xmm2,0x20(%rsp)\nmovdqu %xmm3,0x30(%rsp)\n"
    "movdqu %xmm4,0x40(%rsp)\nmovdqu %xmm5,0x50(%rsp)\nmovdqu %xmm6,0x60(%rsp)\nmovdqu %xmm7,0x70(%rsp)\n"
    "movdqu %xmm8,0x80(%rsp)\nmovdqu %xmm9,0x90(%rsp)\nmovdqu %xmm10,0xa0(%rsp)\nmovdqu %xmm11,0xb0(%rsp)\n"
    "movdqu %xmm12,0xc0(%rsp)\nmovdqu %xmm13,0xd0(%rsp)\nmovdqu %xmm14,0xe0(%rsp)\nmovdqu %xmm15,0xf0(%rsp)\n"
    "mov 24(%rbp),%rdi\nmov 32(%rbp),%rsi\nmov 16(%rbp),%edx\n"
    "call lxco_answer\n"
    "mov %al,-8(%rbp)\n"
    "movdqu 0x00(%rsp),%xmm0\nmovdqu 0x10(%rsp),%xmm1\nmovdqu 0x20(%rsp),%xmm2\nmovdqu 0x30(%rsp),%xmm3\n"
    "movdqu 0x40(%rsp),%xmm4\nmovdqu 0x50(%rsp),%xmm5\nmovdqu 0x60(%rsp),%xmm6\nmovdqu 0x70(%rsp),%xmm7\n"
    "movdqu 0x80(%rsp),%xmm8\nmovdqu 0x90(%rsp),%xmm9\nmovdqu 0xa0(%rsp),%xmm10\nmovdqu 0xb0(%rsp),%xmm11\n"
    "movdqu 0xc0(%rsp),%xmm12\nmovdqu 0xd0(%rsp),%xmm13\nmovdqu 0xe0(%rsp),%xmm14\nmovdqu 0xf0(%rsp),%xmm15\n"
    "mov 0x100(%rsp),%rax\nmov 0x108(%rsp),%rcx\nmov 0x110(%rsp),%rdx\nmov 0x118(%rsp),%rsi\nmov 0x120(%rsp),%rdi\n"
    "mov 0x128(%rsp),%r8\nmov 0x130(%rsp),%r9\nmov 0x138(%rsp),%r10\nmov 0x140(%rsp),%r11\n"
    "cmpb $1,-8(%rbp)\n"
    "mov %rbp,%rsp\npop %rbp\nret\n"
    ".size lxco_common,.-lxco_common\n");
extern void lxco_common(void) __attribute__((visibility("hidden")));
static size_t lxco_push(unsigned char *p,unsigned reg) {   /* push reg */
    if(reg>=8) {p[0]=0x41;p[1]=(unsigned char)(0x50+(reg&7));return 2;}
    p[0]=(unsigned char)(0x50+reg);return 1;
}
static int lxco_rel32(unsigned char *p,const unsigned char *from_end,const void *to) {
    const intptr_t d=(intptr_t)to-(intptr_t)from_end;
    if(d<INT32_MIN || d>INT32_MAX) return 0;
    const int32_t r=(int32_t)d;memcpy(p,&r,4);return 1;
}
/* Stub i at `stub`, as it runs at `at`; `base` the image (LX). 0 when a jump does not reach. */
static size_t lxco_write_site(unsigned char *stub,unsigned char *at,const lxco_site *s,unsigned i,unsigned char *(*lx)(uint32_t),
                              void *common) {
    unsigned char *p=stub;
    memset(stub,0xcc,LXCO_STUB);
    memcpy(p,s->pre,s->pre_n);p+=s->pre_n;
    if(s->pre_jne) {p[0]=0x0f;p[1]=0x85;if(!lxco_rel32(p+2,at+(p-stub)+6,lx(s->pre_jne))) return 0;p+=6;}
    static const unsigned char drop[]={0x48,0x8d,0x64,0x24,0x80};      /* lea rsp,[rsp-0x80]: past the red zone */
    memcpy(p,drop,sizeof(drop));p+=sizeof(drop);
    p+=lxco_push(p,s->y);p+=lxco_push(p,s->x);
    *p++=0x68;memcpy(p,&i,4);p+=4;                                       /* push imm32 site */
    p[0]=0xff;p[1]=0x15;                                                 /* call [rip+disp]: the routine's address at +0x50 */
    {const int32_t d=(int32_t)((stub+0x50)-(p+6));memcpy(p+2,&d,4);}p+=6;
    static const unsigned char back[]={0x48,0x8d,0xa4,0x24,0x98,0x00,0x00,0x00}; /* lea rsp,[rsp+0x98]: flags kept */
    memcpy(p,back,sizeof(back));p+=sizeof(back);
    p[0]=0x0f;p[1]=s->jcc;if(!lxco_rel32(p+2,at+(p-stub)+6,lx(s->taken))) return 0;p+=6;
    p[0]=0xe9;if(!lxco_rel32(p+1,at+(p-stub)+5,lx(s->resume))) return 0;p+=5;
    if(p-stub>0x50) return 0;
    memcpy(stub+0x50,&common,8);
    return LXCO_STUB;
}
static size_t lxco_write_stub(unsigned char *stub,unsigned char *at,unsigned i,unsigned char *(*lx)(uint32_t),void *common) {
    return lxco_write_site(stub,at,&lxco_sites[i],i,lx,common);
}
static unsigned char *lxco_lx(uint32_t va) {return LX(va);}
/* The window's 16-byte line with the jmp in it, or the original. */
static int lxco_line(unsigned i,int ours,unsigned __int128 *line) {
    unsigned char *at=LX(lxco_sites[i].va),*block=(unsigned char*)((uintptr_t)at&~(uintptr_t)15);const unsigned off=(unsigned)(at-block);
    if(off+5>16) return 0;
    memcpy(line,block,16);
    if(ours) {
        unsigned char jmp[5]={0xe9};if(!lxco_page || !lxco_rel32(jmp+1,at+5,lxco_page+LXCO_STUB*i)) return 0;
        memcpy((unsigned char*)line+off,jmp,5);
    } else memcpy((unsigned char*)line+off,lxco_sites[i].orig,5);
    return 1;
}
/* 0 original (the window's bytes), 1 ours, -1 anything else. */
static int lxco_state(unsigned i) {
    const unsigned char *at=LX(lxco_sites[i].va);
    if(!memcmp(at,lxco_sites[i].orig,lxco_sites[i].window)) return 0;
    if(lxco_page && at[0]==0xe9) {int32_t d;memcpy(&d,at+1,4);if(at+5+d==lxco_page+LXCO_STUB*i) return 1;}
    return -1;
}
static int lxco_pins(void) {
    for(unsigned i=0;i<sizeof(lxco_pin_names)/sizeof(*lxco_pin_names);i++) {
        const lx_site *pin=site_named(lxco_pin_names[i]);
        if(!pin) return 0;
        /* a pin over a patched window is checked on a copy with the window's original bytes */
        unsigned char copy[0x100];if(pin->length>sizeof(copy)) return 0;
        memcpy(copy,LX(pin->va),pin->length);
        for(unsigned k=0;k<LXCO_SITES;k++) {
            const lxco_site *s=&lxco_sites[k];
            if(lxco_state(k)==1 && s->va>=pin->va && s->va+5<=pin->va+pin->length) memcpy(copy+(s->va-pin->va),s->orig,5);
        }
        if(!hash_ok(copy,pin->length,pin->sha256)) return 0;
    }
    return 1;
}
static void lxco_restore(void) {
    if(!lxco_installed) return;
    for(unsigned i=0;i<LXCO_SITES;i++) {
        unsigned __int128 ours,orig;
        if(!((lxco_done>>i)&1u) || lxco_state(i)!=1 || !lxco_line(i,1,&ours) || !lxco_line(i,0,&orig)) continue;
        if(gate_code_swap128((unsigned char*)((uintptr_t)LX(lxco_sites[i].va)&~(uintptr_t)15),ours,orig)) lxco_done&=~(1u<<i);
    }
    if(!lxco_done) {lxco_installed=0;lxco_restored=1;lxco_stage="restored";}
}
static int lxco_arm(int mode) {
    if(lxco_installed) return stats.active && lxco_mode==mode;
    if(mode!=LXCO_OBSERVE && mode!=LXCO_ON && mode!=LXCO_REVERSE) {lxco_stage="mode";return 0;}
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {lxco_stage="guard";return 0;}
    if(!world_identity_installed) {lxco_stage="needs";return 0;}
    for(unsigned i=0;i<LXCO_SITES;i++) if(lxco_state(i)!=0) {lxco_stage="site";return 0;}
    if(!lxco_pins()) {lxco_stage="pins";return 0;}
    if(!lxco_page) lxco_page=gate_near_page(LX(lxco_sites[0].va));
    if(!lxco_page || mprotect(lxco_page,GATE_PAGE,PROT_READ|PROT_WRITE)) {lxco_stage="page";return 0;}
    memset(lxco_page,0xcc,GATE_PAGE);
    for(unsigned i=0;i<LXCO_SITES;i++)
        if(!lxco_write_stub(lxco_page+LXCO_STUB*i,lxco_page+LXCO_STUB*i,i,lxco_lx,(void*)lxco_common)) {lxco_stage="stub";return 0;}
    if(mprotect(lxco_page,GATE_PAGE,PROT_READ|PROT_EXEC)) {lxco_stage="page";return 0;}
    for(unsigned i=0;i<LXCO_SITES;i++) {lxco_calls[i]=lxco_flips[i]=0;}
    lxco_unkeyed=lxco_bodyid=lxco_agree=lxco_failures=lxco_last_owner=lxco_last_vtable=0;lxco_restored=0;
    lxco_mode=mode;lxco_installed=1;
    for(unsigned i=0;i<LXCO_SITES;i++) {
        unsigned __int128 orig,ours;
        if(!lxco_line(i,0,&orig) || !lxco_line(i,1,&ours) ||
           !gate_code_swap128((unsigned char*)((uintptr_t)LX(lxco_sites[i].va)&~(uintptr_t)15),orig,ours)) {
            lxco_stage="cas";lxco_restore();lxco_installed=0;lxco_mode=LXCO_OFF;return 0;
        }
        lxco_done|=1u<<i;
    }
    lxco_stage="armed";return 1;
}
static int lxco_owned(void) {
    if(!lxco_installed) return 1;
    for(unsigned i=0;i<LXCO_SITES;i++) if((lxco_done>>i)&1u) {
        unsigned char want[LXCO_STUB];
        if(lxco_state(i)!=1 || !lxco_write_stub(want,lxco_page+LXCO_STUB*i,i,lxco_lx,(void*)lxco_common) ||
           memcmp(lxco_page+LXCO_STUB*i,want,LXCO_STUB)) return 0;
    }
    return 1;
}
/* The tick check (stop 219 on a failure). */
static int lxco_check(void) {
    if(!lxco_installed) return 1;
    if(__atomic_load_n(&lxco_failures,__ATOMIC_SEQ_CST)) {lxco_stage="unreadable";return 0;}
    const char *why=!lxco_owned()?"site_changed":!lxco_pins()?"block_changed":NULL;
    if(why) {lxco_stage=why;__atomic_add_fetch(&lxco_failures,1,__ATOMIC_SEQ_CST);return 0;}
    return 1;
}
static int lxco_status(char *out,size_t cap) {
    unsigned long long c[LXCO_SITES],f[LXCO_SITES];
    for(unsigned i=0;i<LXCO_SITES;i++) {c[i]=(unsigned long long)__atomic_load_n(&lxco_calls[i],__ATOMIC_RELAXED);
                                        f[i]=(unsigned long long)__atomic_load_n(&lxco_flips[i],__ATOMIC_RELAXED);}
    const int n=snprintf(out,cap,"installed=%d mode=%s calls=%llu/%llu/%llu/%llu/%llu flips=%llu/%llu/%llu/%llu/%llu unkeyed=%llu "
        "bodyid=%llu last=%llx/%llx agree=%016llx restored=%d stage=%s failures=%lld",lxco_installed,
        lxco_mode==LXCO_ON?"on":lxco_mode==LXCO_OBSERVE?"observe":lxco_mode==LXCO_REVERSE?"reverse":"off",c[0],c[1],c[2],c[3],c[4],
        f[0],f[1],f[2],f[3],f[4],(unsigned long long)lxco_unkeyed,(unsigned long long)lxco_bodyid,(unsigned long long)lxco_last_owner,
        (unsigned long long)lxco_last_vtable,(unsigned long long)lxco_agree,lxco_restored,lxco_stage,(long long)lxco_failures);
    return n>0 && (size_t)n<cap?n:0;
}
static int lua_contact_orient(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    const int m=mode && length==2 && !memcmp(mode,"on",2)?LXCO_ON:mode && length==7 && !memcmp(mode,"observe",7)?LXCO_OBSERVE:
        mode && length==7 && !memcmp(mode,"reverse",7)?LXCO_REVERSE:LXCO_OFF;
    p_lua_pushnumber(L,accept_ui() && m!=LXCO_OFF && lxco_arm(m));
    return 1;
}
static int lua_contact_orient_status(lua_State *L) {
    char out[384];int n=accept_ui()?lxco_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}

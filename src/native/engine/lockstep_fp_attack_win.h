/* FR29b on the Windows X4 9.00 executable (lockstep_fp_attack.h): the call sites, their hooks and the install. Included
 * by lockstep_fp_redirect_win.h after fr_page/fr_rel; the stubs live on fr_page (fr_write_code, FR_FA_STUB: one
 * jmp [rip] -> hook per kind, 0x20 apart), so the capture arms only after native_gate_fp_redirect.
 * Arguments as the call sites pass them (rcx, rdx, r8, r9, then the stack; the first four are integers or pointers at
 * every hooked call, so a hook taking uint64_t stack slots passes every byte on unchanged):
 *   GetAttackStrength 0x4f43d0 (attacker, out1, out2, list4, list5, target, b7, b8, b9, i10, b11: the action stores a
 *     byte in the eleventh slot) from the action 0xbcd8a0 at 0xbcda62 and 0xbcdac7;
 *   the weapon loop 0x4f2c10 (attacker, out1, out2, list4, list5, a6 byte, a7 double, a8 float distance, target, a10,
 *     a11, a12, a13 object, a14 float, a15 float, a16 double, a17, a18, a19) at 0x4f55f8;
 *   per-weapon damage 0xf1140 (weapon, out, -, flag byte) at 0x4f3739 (loop) and 0x4f27e0 (drones 0x4f1eb0);
 *   the damage add 0xf0260 (acc, addend; returns acc; a leaf) at 0x4f3f4d/0x4f3f62 (loop), eleven sites in
 *     GetAttackStrength, 0x4f42fb (missiles 0x4f40a0), 0x4f27a0/0x4f2b49 (drones);
 *   Random::FloatState 0x14ddc90 (state, bound in xmm1; float) at the loop's eight and the drones' nine sites;
 *   the distance 0x3e1160 (a, b, c, time in xmm3, mode, s6; float) at 0x4f4b86, 0x4f4bce, 0x4f4f68: its callers
 *     pass five, but it owns a sixth slot and writes it (0x3e1191, 0x3e123b, [rbp+0x77] = entry rsp + 0x30), so the
 *     hook declares six: the native's write lands in the hook's own outgoing area, never in the hook's frame;
 *   the apply's damage 0x5209c0 (target, attacker, struct, p4, p5, flag; returns the result in al) at 0xbceb0f.
 * FR29b live (fr29w, client 1, 0xC0000005 at tick 66, the first captured call): the leaf add 0xf0260 writes only rax
 * and xmm0-5, and MSVC's GetAttackStrength relies on that. It loads the accumulator into rcx once and passes it to the
 * next add again (0x4f5629 -> 0x4f56e1, 0x4f5764 -> 0x4f5820 -> 0x4f58ba -> 0x4f5971), and it tests r8d (the flag
 * [rbp-0x80]) at 0x4f572b after two adds. A C hook keeps none of these. On the fast path the tail call left rcx = acc
 * by luck (r8 = 0, the scope argument: the 0x4f5737 virtual was called where native skips it); a captured call
 * returned with rcx = what fa_put's memcpy left, and the next add read and wrote through it. So the leaf kinds (add,
 * and the draw 0x14ddc90, whose hot path keeps rcx, r8-r11, xmm1-5) enter through fa_keep_add / fa_keep_rng: they
 * keep rcx, rdx, r8-r11 and xmm1-5 around the C hook (a superset of what the natives keep; rax and xmm0 are the
 * results), pass the site's return address for the scope, and carry SEH unwind information (.pdata), so a stack walk
 * or an exception passes through them. The other kinds' callees write every volatile register (checked: through
 * their calls, the transitive write sets), so their callers can rely on none.
 * Checked offline (scratchpad fr29c/ipra5.py) over every hooked call: with each callee's transitive write set and the
 * registers a later call takes as input, exactly the five add sites above read a register the native keeps; every
 * stack argument slot each callee touches lies inside what its hook passes. Pins (through the original-byte view):
 * the eleven functions the sites and the hooks' argument layout depend on; each site raw e8 + rel32 to its callee and
 * the view equal to it. */
#define FA_W_SITES 42u
static const struct {uint32_t site,target;uint8_t kind,scope;} fa_w_sites[FA_W_SITES]={
    {0xbcda62u,0x4f43d0u,FA_GAS,FA_IN_EXEC},{0xbcdac7u,0x4f43d0u,FA_GAS,FA_IN_EXEC},
    {0x4f55f8u,0x4f2c10u,FA_WL,FA_IN_GAS},
    {0x4f3739u,0xf1140u,FA_DPS,FA_IN_WL},{0x4f27e0u,0xf1140u,FA_DPS,FA_IN_DRONES},
    {0x4f3f4du,0xf0260u,FA_ADD,FA_IN_WL},{0x4f3f62u,0xf0260u,FA_ADD,FA_IN_WL},
    {0x4f5232u,0xf0260u,FA_ADD,FA_IN_GAS},{0x4f5292u,0xf0260u,FA_ADD,FA_IN_GAS},{0x4f52f2u,0xf0260u,FA_ADD,FA_IN_GAS},
    {0x4f5352u,0xf0260u,FA_ADD,FA_IN_GAS},{0x4f53afu,0xf0260u,FA_ADD,FA_IN_GAS},{0x4f5629u,0xf0260u,FA_ADD,FA_IN_GAS},
    {0x4f56e1u,0xf0260u,FA_ADD,FA_IN_GAS},{0x4f5764u,0xf0260u,FA_ADD,FA_IN_GAS},{0x4f5820u,0xf0260u,FA_ADD,FA_IN_GAS},
    {0x4f58bau,0xf0260u,FA_ADD,FA_IN_GAS},{0x4f5971u,0xf0260u,FA_ADD,FA_IN_GAS},
    {0x4f42fbu,0xf0260u,FA_ADD,FA_IN_MISSILES},{0x4f27a0u,0xf0260u,FA_ADD,FA_IN_DRONES},{0x4f2b49u,0xf0260u,FA_ADD,FA_IN_DRONES},
    {0x4f2e2eu,0x14ddc90u,FA_RNG,FA_IN_WL},{0x4f2e79u,0x14ddc90u,FA_RNG,FA_IN_WL},{0x4f2f96u,0x14ddc90u,FA_RNG,FA_IN_WL},
    {0x4f31fau,0x14ddc90u,FA_RNG,FA_IN_WL},{0x4f3262u,0x14ddc90u,FA_RNG,FA_IN_WL},{0x4f32bdu,0x14ddc90u,FA_RNG,FA_IN_WL},
    {0x4f3558u,0x14ddc90u,FA_RNG,FA_IN_WL},{0x4f3587u,0x14ddc90u,FA_RNG,FA_IN_WL},
    {0x4f2012u,0x14ddc90u,FA_RNG,FA_IN_DRONES},{0x4f20e3u,0x14ddc90u,FA_RNG,FA_IN_DRONES},{0x4f23abu,0x14ddc90u,FA_RNG,FA_IN_DRONES},
    {0x4f2499u,0x14ddc90u,FA_RNG,FA_IN_DRONES},{0x4f24fbu,0x14ddc90u,FA_RNG,FA_IN_DRONES},{0x4f2511u,0x14ddc90u,FA_RNG,FA_IN_DRONES},
    {0x4f26a7u,0x14ddc90u,FA_RNG,FA_IN_DRONES},{0x4f26cdu,0x14ddc90u,FA_RNG,FA_IN_DRONES},{0x4f2747u,0x14ddc90u,FA_RNG,FA_IN_DRONES},
    {0x4f4b86u,0x3e1160u,FA_DIST,FA_IN_GAS},{0x4f4bceu,0x3e1160u,FA_DIST,FA_IN_GAS},{0x4f4f68u,0x3e1160u,FA_DIST,FA_IN_GAS},
    {0xbceb0fu,0x5209c0u,FA_APPLY,FA_IN_APPLY},
};
#define FA_W_PINS 11u
static const struct {uint32_t rva,size;const char *sha;} fa_w_pins[FA_W_PINS]={
    {0xbcd8a0u,0x81du,"c46f2b27af573c1218fa11599b05898d3c5f10355f9641c1ec8d9568bbb99441"}, /* GetAttackStrengthAction::Execute */
    {0xbce910u,0x425u,"d406fcde4296d8f2c2c739ac5e35878d34b9b93ee55cb648d8ac6c14f490876e"}, /* ApplyAttackStrengthAction::Execute */
    {0x4f43d0u,0x15ffu,"ad741c5eab64552c2c6e9d2835f869cd1bb95ea6db455aee141c00f1462b428a"}, /* GetAttackStrength */
    {0x4f2c10u,0x1483u,"243aa25732f2844d7981fc1282ab0d7dad0b55f0a9025c0422c520101ffce478"}, /* the weapon loop */
    {0x4f1eb0u,0xd5du,"cc927ad3b0f2b260dbf012aeaadbfc5e65e6d9938c2ff543f46f091c017d4ec2"},  /* drones */
    {0x4f40a0u,0x325u,"f60980ea94389e067daf6ae2bb5c66971bfac69d4cd7bde93df91aeea3667a93"},  /* missiles */
    {0xf1140u,0x27cu,"ea0a487acb82d1ab9aed20b77ccd6255442bf75364d1ef1a34ebfa56ea712af8"},   /* per-weapon damage */
    {0x3e1160u,0x3b5u,"b00ea250658462bdb3a870ff523c061af48ac5c4b62f682f1ee33433729c1476"},  /* the distance */
    {0x5209c0u,0x36u,"117ccf1c97ece95e08245dafebe4ac7f0d333d78ae830240eed7f6fdae7cd9cc"},   /* the apply's damage (first chunk) */
    {0xf0260u,0xfcu,"bf2e7cf60c0a7bc7134fd6a5724ec4626adff964f74540ea79a94e81e978c2f1"},    /* the damage add (leaf, to its ret) */
    {0x14ddc90u,0x78u,"584854a7bdbf956043c7756157888674152bd0ffc0a307c00f29537332e8780e"},  /* Random::FloatState (leaf) */
};
static uint64_t fa_done; /* bit i: site i is ours */
/* The originals the hooks call (set at arm; tests point them at stand-ins). */
typedef uint64_t (*fa_w11_fn)(void *,void *,void *,void *,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*fa_w19_fn)(void *,void *,void *,void *,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,
                              uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*fa_w4_fn)(void *,void *,uint64_t,uint64_t);
typedef uint64_t (*fa_w2_fn)(void *,void *);
typedef float (*fa_wrng_fn)(uint64_t *,float);
typedef float (*fa_wdist_fn)(void *,void *,uint64_t,double,uint64_t,uint64_t);
typedef uint64_t (*fa_w6_fn)(void *,void *,void *,void *,uint64_t,uint64_t);
static fa_w11_fn fa_native_gas;static fa_w19_fn fa_native_wl;static fa_w4_fn fa_native_dps;static fa_w2_fn fa_native_add;
static fa_wrng_fn fa_native_rng;static fa_wdist_fn fa_native_dist;static fa_w6_fn fa_native_apply;
static uint64_t *fa_tls_rng(void) {
    if(fa_test_rng) return fa_test_rng;
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return slots && slots[0]?(uint64_t*)((unsigned char*)slots[0]+0x318):NULL;
}
static double *fa_tls_time(void) {
    if(fa_test_time) return fa_test_time;
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return slots && slots[0]?(double*)((unsigned char*)slots[0]+0x300):NULL;
}
/* The scope of the site this hook was called from (its return address is the site + 5). */
static unsigned fa_scope_of(const void *ret) {
    const uintptr_t at=(uintptr_t)ret;
    for(unsigned i=0;i<FA_W_SITES;i++) if((uintptr_t)(image+fa_w_sites[i].site+5u)==at) return fa_w_sites[i].scope;
    return FA_IN_EXEC;
}
static uint32_t fa_f32_bits(float f) {uint32_t b;memcpy(&b,&f,4);return b;}
static uint64_t fa_f64_bits(double d) {uint64_t b;memcpy(&b,&d,8);return b;}
/* ---- Hooks ---- */
/* An exception that unwinds through a captured get_attackstrength (the __finally below) closes its context: no row, the
 * thread's next call is captured again, fa_open stays the count of calls in progress. */
static __attribute__((noinline)) void fa_gas_unwound(fa_ctx *c) {
    c->depth=0;c->gas_out[0]=c->gas_out[1]=c->wl_out[0]=c->wl_out[1]=NULL;
    InterlockedAdd64(&fa_open,-1);InterlockedIncrement64(&fa_unwound);
}
static uint64_t fa_gas_w(void *attacker,void *out1,void *out2,void *list4,uint64_t list5,uint64_t target,uint64_t b7,uint64_t b8,uint64_t b9,
                         uint64_t i10,uint64_t b11) {
    fa_record r;fa_ctx *c=fa_gas_enter(&r,attacker,(void*)(uintptr_t)target,out1,out2);
    if(c) {
        r.nargs=7;r.args[0]=list4!=NULL;r.args[1]=list5!=0;r.args[2]=b7&0xffu;r.args[3]=b8&0xffu;r.args[4]=b9&0xffu;
        r.args[5]=i10&0xffffffffu;r.args[6]=b11&0xffu; /* a byte: the slot's upper bytes are the action's stack */
    }
    uint64_t ret=0;
    __try {ret=fa_native_gas(attacker,out1,out2,list4,list5,target,b7,b8,b9,i10,b11);}
    __finally {if(AbnormalTermination() && c) fa_gas_unwound(c);}
    if(c) fa_gas_exit(c,&r,out1,out2);
    return ret;
}
static uint64_t fa_wl_w(void *attacker,void *out1,void *out2,void *list4,uint64_t list5,uint64_t a6,uint64_t a7,uint64_t a8,uint64_t target,
                        uint64_t a10,uint64_t a11,uint64_t a12,uint64_t a13,uint64_t a14,uint64_t a15,uint64_t a16,uint64_t a17,uint64_t a18,uint64_t a19) {
    const uint64_t args[FA_WL_ARGS]={a6&0xffu,a7,a8&0xffffffffu,a10&0xffu,a11&0xffu,a12&0xffu,a14&0xffffffffu,a15&0xffffffffu,a17&0xffu,a18&0xffu,
                                     list4!=NULL,list5!=0,a16,a19&0xffu};
    fa_record r;fa_ctx *c=fa_wl_enter(&r,out1,out2,(const void*)(uintptr_t)a13,args);
    if(c) fa_obj(&r,3,(const void*)(uintptr_t)target);
    const uint64_t ret=fa_native_wl(attacker,out1,out2,list4,list5,a6,a7,a8,target,a10,a11,a12,a13,a14,a15,a16,a17,a18,a19);
    if(c) fa_wl_exit(c,&r,out1,out2);
    return ret;
}
static uint64_t fa_dps_w(void *weapon,void *out,uint64_t a3,uint64_t flag) {
    fa_record r;fa_ctx *c=fa_sub_enter(&r,FA_DPS,FA_IN_EXEC);
    if(c) r.scope=fa_scope_of(__builtin_return_address(0)); /* only for a captured call */
    if(c) {fa_obj(&r,2,weapon);r.nargs=1;r.args[0]=flag&0xffu;fa_blob(&r,0,out);}
    const uint64_t ret=fa_native_dps(weapon,out,a3,flag);
    if(c) {fa_blob(&r,1,out);fa_sub_exit(&r);}
    return ret;
}
/* The leaf kinds are called by fa_keep_add / fa_keep_rng (below) with the site's return address as the third argument. */
static uint64_t fa_add_w(void *acc,void *addend,const void *site_ret) {
    fa_record r;fa_ctx *c=fa_sub_enter(&r,FA_ADD,FA_IN_EXEC);
    if(c) r.scope=fa_scope_of(site_ret); /* only for a captured call */
    if(c) {r.label=fa_label(c,acc);fa_blob(&r,0,acc);fa_blob(&r,1,addend);}
    const uint64_t ret=fa_native_add(acc,addend);
    if(c) {fa_blob(&r,2,acc);fa_sub_exit(&r);}
    return ret;
}
static float fa_rng_w(uint64_t *state,float bound,const void *site_ret) {
    fa_record r;fa_ctx *c=fa_sub_enter(&r,FA_RNG,FA_IN_EXEC);
    if(c) r.scope=fa_scope_of(site_ret); /* only for a captured call */
    if(c && state) r.rng_in=*state; /* the draw's own state (the loop passes the thread's word) */
    const float v=fa_native_rng(state,bound);
    if(c) {r.nargs=2;r.args[0]=fa_f32_bits(bound);r.args[1]=fa_f32_bits(v);fa_rng_exit(&r,state);}
    return v;
}
static float fa_dist_w(void *a,void *b,uint64_t c3,double time,uint64_t mode,uint64_t s6) {
    fa_record r;fa_ctx *c=fa_sub_enter(&r,FA_DIST,FA_IN_EXEC);
    if(c) r.scope=fa_scope_of(__builtin_return_address(0)); /* only for a captured call */
    if(c) {fa_obj(&r,2,a);fa_obj(&r,3,b);}
    const float v=fa_native_dist(a,b,c3,time,mode,s6); /* s6: the slot the native writes, in our outgoing area */
    if(c) {r.nargs=4;r.args[0]=fa_f64_bits(time);r.args[1]=c3&0xffffffffu;r.args[2]=mode&0xffffffffu;r.args[3]=fa_f32_bits(v);fa_sub_exit(&r);}
    return v;
}
static uint64_t fa_apply_w(void *target,void *attacker,void *damage,void *p4,uint64_t p5,uint64_t flag) {
    fa_record r;const int on=fa_apply_enter(&r,attacker,target,damage);
    if(on) {r.nargs=1;r.args[0]=flag&0xffu;}
    const uint64_t ret=fa_native_apply(target,attacker,damage,p4,p5,flag);
    if(on) {r.ret=ret&0xffu;fa_sub_exit(&r);}
    return ret;
}
/* The leaf kinds' entries (the stub's jmp lands here, [rsp] = the site + 5): rcx, rdx, r8-r11 and xmm1-5 saved in a
 * 0xa8 frame (rsp 16-aligned at the call), the C hook called with the site's return address in r8 (the third
 * argument; the add's two and the draw's rcx/xmm1 are untouched), those registers back, rax/xmm0 the hook's result.
 * .seh_proc/.seh_stackalloc give the frame its unwind entry (the DLL's .pdata), so an exception or a stack walk from
 * inside the hook unwinds through it to the X4 caller. fa_keep_calls keeps the hooks referenced from C (ABI, no
 * removal); the entries call them through it. */
static void *const fa_keep_calls[2] __attribute__((used))={(void*)fa_add_w,(void*)fa_rng_w};
#define FA_KEEP_ENTRY(name,slot) \
    ".text\n.p2align 4\n.def " name "; .scl 3; .type 32; .endef\n.seh_proc " name "\n" name ":\n" \
    " subq $0xa8,%rsp\n .seh_stackalloc 0xa8\n .seh_endprologue\n" \
    " movq %rcx,0x20(%rsp)\n movq %rdx,0x28(%rsp)\n movq %r8,0x30(%rsp)\n movq %r9,0x38(%rsp)\n movq %r10,0x40(%rsp)\n movq %r11,0x48(%rsp)\n" \
    " movaps %xmm1,0x50(%rsp)\n movaps %xmm2,0x60(%rsp)\n movaps %xmm3,0x70(%rsp)\n movaps %xmm4,0x80(%rsp)\n movaps %xmm5,0x90(%rsp)\n" \
    " movq 0xa8(%rsp),%r8\n call *fa_keep_calls+" slot "(%rip)\n" \
    " movq 0x20(%rsp),%rcx\n movq 0x28(%rsp),%rdx\n movq 0x30(%rsp),%r8\n movq 0x38(%rsp),%r9\n movq 0x40(%rsp),%r10\n movq 0x48(%rsp),%r11\n" \
    " movaps 0x50(%rsp),%xmm1\n movaps 0x60(%rsp),%xmm2\n movaps 0x70(%rsp),%xmm3\n movaps 0x80(%rsp),%xmm4\n movaps 0x90(%rsp),%xmm5\n" \
    " addq $0xa8,%rsp\n ret\n.seh_endproc\n"
__asm__(FA_KEEP_ENTRY("fa_keep_add","0") FA_KEEP_ENTRY("fa_keep_rng","8"));
extern char fa_keep_add[],fa_keep_rng[];
static void *fa_hook_of(unsigned kind) {
    switch(kind) {
    case FA_GAS: return (void*)fa_gas_w;
    case FA_WL: return (void*)fa_wl_w;
    case FA_DPS: return (void*)fa_dps_w;
    case FA_ADD: return (void*)fa_keep_add;
    case FA_RNG: return (void*)fa_keep_rng;
    case FA_DIST: return (void*)fa_dist_w;
    default: return (void*)fa_apply_w;
    }
}
/* ---- Install ---- */
static unsigned char *fa_stub(unsigned kind) {return fr_page+FR_FA_STUB+0x20u*(kind-1u);}
static void fa_write_code(unsigned char *p) {
    for(unsigned k=FA_GAS;k<FA_KINDS;k++) fr_write_jump(p+FR_FA_STUB+0x20u*(k-1u),fa_hook_of(k));
}
static int32_t fa_rel_native(unsigned i) {return fr_rel(fa_w_sites[i].site,image+fa_w_sites[i].target);}
static int32_t fa_rel_ours(unsigned i) {return fr_rel(fa_w_sites[i].site,fa_stub(fa_w_sites[i].kind));}
static int fa_reach(unsigned i) {
    const intptr_t d=(intptr_t)fa_stub(fa_w_sites[i].kind)-(intptr_t)(image+fa_w_sites[i].site+5u);
    return d>INT32_MIN && d<INT32_MAX;
}
/* 0 original, 1 ours, -1 anything else (raw bytes: the view shows the original ones). */
static int fa_site_state(unsigned i) {
    unsigned char now[5];memcpy(now,(const void*)(image+fa_w_sites[i].site),5);
    if(now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==fa_rel_native(i)) return 0;
    if(fr_page && fa_reach(i) && d==fa_rel_ours(i)) return 1;
    return -1;
}
static int fa_site_clear(unsigned i) {
    unsigned char viewed[5];
    return read_local(image+fa_w_sites[i].site,viewed,5) && !memcmp((const void*)(image+fa_w_sites[i].site),viewed,5);
}
static int fa_pins_ok(void) {
    static unsigned char body[0x1600];
    for(unsigned i=0;i<FA_W_PINS;i++)
        if(fa_w_pins[i].size>sizeof(body) || !read_local(image+fa_w_pins[i].rva,body,fa_w_pins[i].size) ||
           !hash_ok(body,fa_w_pins[i].size,fa_w_pins[i].sha)) return 0;
    return 1;
}
static int fa_site_cas(unsigned i,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(fa_w_sites[i].site+1u,e,v,4);
}
/* The original-byte view (fr_len_overlaps / fr_len_unpatch_copy) over our rel32s. */
static int fa_overlaps(const uintptr_t a,const uintptr_t b) {
    if(!fa_done) return 0;
    for(unsigned i=0;i<FA_W_SITES;i++) {
        const uintptr_t s=(uintptr_t)(image+fa_w_sites[i].site+1u);
        if((fa_done>>i)&1u && s<b && s+4>a) return 1;
    }
    return 0;
}
static void fa_unpatch_copy(const uintptr_t a,unsigned char *copy,size_t bytes) {
    if(!fa_done) return;
    for(unsigned i=0;i<FA_W_SITES;i++) {
        if(!((fa_done>>i)&1u)) continue;
        const int32_t d=fa_rel_native(i);unsigned char rel[4];memcpy(rel,&d,4);
        const uintptr_t s=(uintptr_t)(image+fa_w_sites[i].site+1u);
        for(unsigned k=0;k<4;k++) if(s+k>=a && s+k<a+bytes) copy[s+k-a]=rel[k];
    }
}
static void fa_restore(void) {
    if(!fa_installed) return;
    for(unsigned i=0;i<FA_W_SITES;i++)
        if((fa_done>>i)&1u && fa_site_state(i)==1 && fa_site_cas(i,fa_rel_ours(i),fa_rel_native(i))) fa_done&=~(1ull<<i);
    if(!fa_done) {fa_installed=0;fa_restored=1;fa_stage="restored";fa_sites_hooked=0;}
}
static int fa_owned(void) {
    for(unsigned i=0;i<FA_W_SITES;i++) if((fa_done>>i)&1u && fa_site_state(i)!=1) return 0;
    return 1;
}
/* gate.fp_attack(first, last): after fp_redirect (UI thread, before the first admitted frame). */
static int fa_arm(uint64_t first,uint64_t last) {
    if(fa_installed) return fa_first==first && fa_last==last;
    if(!fa_arm_ready(first,last)) return 0;
    if(!fr_page) {fa_stage="page";return 0;}
    if(!fa_pins_ok()) {fa_stage="pins";return 0;}
    for(unsigned i=0;i<FA_W_SITES;i++) {
        if(fa_site_state(i)!=0 || !fa_site_clear(i)) {fa_stage="pins";return 0;}
        if(!fa_reach(i)) {fa_stage="page";return 0;}
    }
    fa_native_gas=(fa_w11_fn)(void*)(image+0x4f43d0u);fa_native_wl=(fa_w19_fn)(void*)(image+0x4f2c10u);
    fa_native_dps=(fa_w4_fn)(void*)(image+0xf1140u);fa_native_add=(fa_w2_fn)(void*)(image+0xf0260u);
    fa_native_rng=(fa_wrng_fn)(void*)(image+0x14ddc90u);fa_native_dist=(fa_wdist_fn)(void*)(image+0x3e1160u);
    fa_native_apply=(fa_w6_fn)(void*)(image+0x5209c0u);
    fa_installed=1;fa_restored=0;fa_done=0;fa_sites_hooked=0;fa_sites_skipped=0;
    for(unsigned i=0;i<FA_W_SITES;i++) {
        if(!fa_site_cas(i,fa_rel_native(i),fa_rel_ours(i))) {fa_stage="cas";fa_restore();fa_installed=fa_done!=0;return 0;}
        fa_done|=1ull<<i;++fa_sites_hooked;
    }
    fa_stage="armed";return 1;
}

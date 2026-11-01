/* FR29b (lockstep_fp_attack.h) on the Linux X4 9.00 executable: the call sites, their hooks and the install. Included
 * by gate_fp_redirect.h before its thunk writer (the stubs: LXF_FA_STUB, one jmp [rip] -> hook per kind, 0x20 apart).
 * Arguments (System V: integers rdi, rsi, rdx, rcx, r8, r9, floats xmm0..; then the stack in order):
 *   GetAttackStrength 0x1d1b9d0 (attacker, out1, out2, list4, list5, target; stack b7, b8, b9, i10) from the action
 *     0xe07150 at 0xe07346 and 0xe077cb;
 *   the weapon loop 0x1deb960 (attacker, out1, out2, list4, list5, a6 byte; xmm0 a7 double, xmm1 a8 float distance,
 *     xmm2 a14, xmm3 a15; stack target, a10, a11, a12, a13 object, a17, a18; Windows' a16 and a19 are not passed) at
 *     0x1d1c48b;
 *   per-weapon damage 0xf05ff0 (weapon, out, edx, ecx flag byte) at 0x1dec155, 0x1dec51f (loop), 0x1ded68b (drones);
 *   the damage add 0xf05be0 (acc, addend; returns acc; a leaf) in GetAttackStrength (with the module path Linux
 *     inlines), the loop, drones 0x1ded490 and missiles 0x1ced2c0;
 *   Random::FloatState 0x1a491c0 (xmm0 bound; float; it steps the thread word fs:-0x2720) in GetAttackStrength, the
 *     loop and drones;
 *   the distance 0x1dea820 (a, b, edx, ecx mode, xmm0 time; float) at 0x1d1bc01, 0x1d1bef8, 0x1d1cba6;
 *   the apply's damage 0x17f8d90 (target, attacker, struct, rcx, r8, r9b flag; returns the result in al) at 0xdfc994.
 * A site's rel32 changes in one compare-exchange of its aligned 16-byte line. Eight sites whose rel32 crosses a line
 * are not hooked (status skipped=8): the adds 0x1d1c5fe, 0x1d1ca4d, 0x1deda7c and the draws 0x1d1e0ed, 0x1dec03d,
 * 0x1ded2fc, 0x1ded5ce, 0x1dedd5e (the reader aligns draws by their state). No hooked site reads a volatile register
 * after the call before writing it (checked offline). The executable's file pin covers every byte. */
#define FA_L_SITES 48u
#define FA_L_SKIPPED 8u
static const struct {uint32_t site,target;uint8_t kind,scope;} fa_l_sites[FA_L_SITES]={
    {0xe07346u,0x1d1b9d0u,FA_GAS,FA_IN_EXEC},{0xe077cbu,0x1d1b9d0u,FA_GAS,FA_IN_EXEC},
    {0x1d1c48bu,0x1deb960u,FA_WL,FA_IN_GAS},
    {0x1dec155u,0xf05ff0u,FA_DPS,FA_IN_WL},{0x1dec51fu,0xf05ff0u,FA_DPS,FA_IN_WL},{0x1ded68bu,0xf05ff0u,FA_DPS,FA_IN_DRONES},
    {0x1dec25au,0xf05be0u,FA_ADD,FA_IN_WL},{0x1dec400u,0xf05be0u,FA_ADD,FA_IN_WL},
    {0x1d1c5aau,0xf05be0u,FA_ADD,FA_IN_GAS},{0x1d1c651u,0xf05be0u,FA_ADD,FA_IN_GAS},{0x1d1c6aau,0xf05be0u,FA_ADD,FA_IN_GAS},
    {0x1d1c700u,0xf05be0u,FA_ADD,FA_IN_GAS},{0x1d1c7cfu,0xf05be0u,FA_ADD,FA_IN_GAS},{0x1d1c84fu,0xf05be0u,FA_ADD,FA_IN_GAS},
    {0x1d1c936u,0xf05be0u,FA_ADD,FA_IN_GAS},{0x1d1c9cbu,0xf05be0u,FA_ADD,FA_IN_GAS},{0x1d1cacfu,0xf05be0u,FA_ADD,FA_IN_GAS},
    {0x1d1da88u,0xf05be0u,FA_ADD,FA_IN_GAS},{0x1d1ddc8u,0xf05be0u,FA_ADD,FA_IN_GAS},
    {0x1ced534u,0xf05be0u,FA_ADD,FA_IN_MISSILES},{0x1ded6f7u,0xf05be0u,FA_ADD,FA_IN_DRONES},{0x1deda4bu,0xf05be0u,FA_ADD,FA_IN_DRONES},
    {0x1d1c0abu,0x1a491c0u,FA_RNG,FA_IN_GAS},{0x1d1c1ebu,0x1a491c0u,FA_RNG,FA_IN_GAS},{0x1d1d619u,0x1a491c0u,FA_RNG,FA_IN_GAS},
    {0x1d1dc77u,0x1a491c0u,FA_RNG,FA_IN_GAS},{0x1d1dc9au,0x1a491c0u,FA_RNG,FA_IN_GAS},{0x1d1e27fu,0x1a491c0u,FA_RNG,FA_IN_GAS},
    {0x1d1e427u,0x1a491c0u,FA_RNG,FA_IN_GAS},
    {0x1debbf2u,0x1a491c0u,FA_RNG,FA_IN_WL},{0x1debd26u,0x1a491c0u,FA_RNG,FA_IN_WL},{0x1debf43u,0x1a491c0u,FA_RNG,FA_IN_WL},
    {0x1decae4u,0x1a491c0u,FA_RNG,FA_IN_WL},{0x1deccd7u,0x1a491c0u,FA_RNG,FA_IN_WL},{0x1decd01u,0x1a491c0u,FA_RNG,FA_IN_WL},
    {0x1dece89u,0x1a491c0u,FA_RNG,FA_IN_WL},{0x1ded02bu,0x1a491c0u,FA_RNG,FA_IN_WL},
    {0x1ded8f3u,0x1a491c0u,FA_RNG,FA_IN_DRONES},{0x1ded9e7u,0x1a491c0u,FA_RNG,FA_IN_DRONES},{0x1deda0au,0x1a491c0u,FA_RNG,FA_IN_DRONES},
    {0x1dedac8u,0x1a491c0u,FA_RNG,FA_IN_DRONES},{0x1dedadfu,0x1a491c0u,FA_RNG,FA_IN_DRONES},{0x1dedf65u,0x1a491c0u,FA_RNG,FA_IN_DRONES},
    {0x1dee298u,0x1a491c0u,FA_RNG,FA_IN_DRONES},
    {0x1d1bc01u,0x1dea820u,FA_DIST,FA_IN_GAS},{0x1d1bef8u,0x1dea820u,FA_DIST,FA_IN_GAS},{0x1d1cba6u,0x1dea820u,FA_DIST,FA_IN_GAS},
    {0xdfc994u,0x17f8d90u,FA_APPLY,FA_IN_APPLY},
};
static uint64_t fa_done; /* bit i: site i is ours */
/* The originals the hooks call (set at arm; tests point them at stand-ins). */
typedef uint64_t (*fa_l10_fn)(void *,void *,void *,void *,void *,void *,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*fa_l17_fn)(void *,void *,void *,void *,void *,uint64_t,double,double,double,double,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,
                              uint64_t,uint64_t);
typedef uint64_t (*fa_l4_fn)(void *,void *,uint64_t,uint64_t);
typedef uint64_t (*fa_l2_fn)(void *,void *);
typedef float (*fa_lrng_fn)(float);
typedef float (*fa_ldist_fn)(void *,void *,uint64_t,uint64_t,double);
typedef uint64_t (*fa_l6_fn)(void *,void *,void *,uint64_t,uint64_t,uint64_t);
static fa_l10_fn fa_native_gas;static fa_l17_fn fa_native_wl;static fa_l4_fn fa_native_dps;static fa_l2_fn fa_native_add;
static fa_lrng_fn fa_native_rng;static fa_ldist_fn fa_native_dist;static fa_l6_fn fa_native_apply;
static uint64_t *fa_tls_rng(void) {return fa_test_rng?fa_test_rng:gate_tls_rng(gate_tls_self());}
static double *fa_tls_time(void) {return fa_test_time?fa_test_time:gate_tls_time(gate_tls_self());}
/* The scope of the site this hook was called from (its return address is the site + 5). */
static unsigned fa_scope_of(const void *ret) {
    for(unsigned i=0;i<FA_L_SITES;i++) if((const void*)LX(fa_l_sites[i].site+5u)==ret) return fa_l_sites[i].scope;
    return FA_IN_EXEC;
}
static uint32_t fa_f32_bits(float f) {uint32_t b;memcpy(&b,&f,4);return b;}
static uint64_t fa_f64_bits(double d) {uint64_t b;memcpy(&b,&d,8);return b;}
/* ---- Hooks ---- */
static uint64_t fa_gas_l(void *attacker,void *out1,void *out2,void *list4,void *list5,void *target,uint64_t b7,uint64_t b8,uint64_t b9,uint64_t i10) {
    fa_record r;fa_ctx *c=fa_gas_enter(&r,attacker,target,out1,out2);
    if(c) {r.nargs=7;r.args[0]=list4!=NULL;r.args[1]=list5!=NULL;r.args[2]=b7&0xffu;r.args[3]=b8&0xffu;r.args[4]=b9&0xffu;r.args[5]=i10&0xffffffffu;}
    const uint64_t ret=fa_native_gas(attacker,out1,out2,list4,list5,target,b7,b8,b9,i10);
    if(c) fa_gas_exit(c,&r,out1,out2);
    return ret;
}
static uint64_t fa_wl_l(void *attacker,void *out1,void *out2,void *list4,void *list5,uint64_t a6,double a7,double a8,double a14,double a15,
                        uint64_t target,uint64_t a10,uint64_t a11,uint64_t a12,uint64_t a13,uint64_t a17,uint64_t a18) {
    const uint64_t args[FA_WL_ARGS]={a6&0xffu,fa_f64_bits(a7),fa_f64_bits(a8)&0xffffffffu,a10&0xffu,a11&0xffu,a12&0xffu,fa_f64_bits(a14)&0xffffffffu,
                                     fa_f64_bits(a15)&0xffffffffu,a17&0xffu,a18&0xffu,list4!=NULL,list5!=NULL,0,0};
    fa_record r;fa_ctx *c=fa_wl_enter(&r,out1,out2,(const void*)(uintptr_t)a13,args);
    if(c) fa_obj(&r,3,(const void*)(uintptr_t)target);
    const uint64_t ret=fa_native_wl(attacker,out1,out2,list4,list5,a6,a7,a8,a14,a15,target,a10,a11,a12,a13,a17,a18);
    if(c) fa_wl_exit(c,&r,out1,out2);
    return ret;
}
static uint64_t fa_dps_l(void *weapon,void *out,uint64_t a3,uint64_t flag) {
    fa_record r;fa_ctx *c=fa_sub_enter(&r,FA_DPS,FA_IN_EXEC);
    if(c) r.scope=fa_scope_of(__builtin_return_address(0)); /* only for a captured call */
    if(c) {fa_obj(&r,2,weapon);r.nargs=1;r.args[0]=flag&0xffu;fa_blob(&r,0,out);}
    const uint64_t ret=fa_native_dps(weapon,out,a3,flag);
    if(c) {fa_blob(&r,1,out);fa_sub_exit(&r);}
    return ret;
}
static uint64_t fa_add_l(void *acc,void *addend) {
    fa_record r;fa_ctx *c=fa_sub_enter(&r,FA_ADD,FA_IN_EXEC);
    if(c) r.scope=fa_scope_of(__builtin_return_address(0)); /* only for a captured call */
    if(c) {r.label=fa_label(c,acc);fa_blob(&r,0,acc);fa_blob(&r,1,addend);}
    const uint64_t ret=fa_native_add(acc,addend);
    if(c) {fa_blob(&r,2,acc);fa_sub_exit(&r);}
    return ret;
}
static float fa_rng_l(float bound) {
    fa_record r;fa_ctx *c=fa_sub_enter(&r,FA_RNG,FA_IN_EXEC);
    if(c) r.scope=fa_scope_of(__builtin_return_address(0)); /* only for a captured call; rng_in: the thread word it steps */
    const float v=fa_native_rng(bound);
    if(c) {r.nargs=2;r.args[0]=fa_f32_bits(bound);r.args[1]=fa_f32_bits(v);fa_rng_exit(&r,fa_tls_rng());}
    return v;
}
static float fa_dist_l(void *a,void *b,uint64_t c3,uint64_t mode,double time) {
    fa_record r;fa_ctx *c=fa_sub_enter(&r,FA_DIST,FA_IN_EXEC);
    if(c) r.scope=fa_scope_of(__builtin_return_address(0)); /* only for a captured call */
    if(c) {fa_obj(&r,2,a);fa_obj(&r,3,b);}
    const float v=fa_native_dist(a,b,c3,mode,time);
    if(c) {r.nargs=4;r.args[0]=fa_f64_bits(time);r.args[1]=c3&0xffffffffu;r.args[2]=mode&0xffffffffu;r.args[3]=fa_f32_bits(v);fa_sub_exit(&r);}
    return v;
}
static uint64_t fa_apply_l(void *target,void *attacker,void *damage,uint64_t p4,uint64_t p5,uint64_t flag) {
    fa_record r;const int on=fa_apply_enter(&r,attacker,target,damage);
    if(on) {r.nargs=1;r.args[0]=flag&0xffu;}
    const uint64_t ret=fa_native_apply(target,attacker,damage,p4,p5,flag);
    if(on) {r.ret=ret&0xffu;fa_sub_exit(&r);}
    return ret;
}
static void *fa_hook_of(unsigned kind) {
    switch(kind) {
    case FA_GAS: return (void*)fa_gas_l;
    case FA_WL: return (void*)fa_wl_l;
    case FA_DPS: return (void*)fa_dps_l;
    case FA_ADD: return (void*)fa_add_l;
    case FA_RNG: return (void*)fa_rng_l;
    case FA_DIST: return (void*)fa_dist_l;
    default: return (void*)fa_apply_l;
    }
}
/* ---- Install ---- */
static unsigned char *fa_stub(unsigned kind) {return thunk+LXF_FA_STUB+0x20u*(kind-1u);}
static void fa_thunk_write(unsigned char *page) {
    for(unsigned k=FA_GAS;k<FA_KINDS;k++) lxw_jump_write(page+LXF_FA_STUB+0x20u*(k-1u),fa_hook_of(k));
}
/* 0 original, 1 ours, -1 anything else. */
static int fa_site_state(unsigned i) {
    const unsigned char *site=LX(fa_l_sites[i].site);int32_t d;
    if(site[0]!=0xe8) return -1;
    memcpy(&d,site+1,4);
    if(site+5+d==LX(fa_l_sites[i].target)) return 0;
    if(thunk && site+5+d==fa_stub(fa_l_sites[i].kind)) return 1;
    return -1;
}
/* The rel32 in its aligned 16-byte line (every listed site: (site+1)&15 <= 12). */
static int fa_swap(unsigned i,const void *expected,const void *value) {
    unsigned char *site=LX(fa_l_sites[i].site),*line=(unsigned char*)((uintptr_t)(site+1)&~(uintptr_t)15);
    const unsigned at=(unsigned)(site+1-line);
    const int64_t e=(intptr_t)expected-(intptr_t)(site+5),v=(intptr_t)value-(intptr_t)(site+5);
    if(at>12 || e<INT32_MIN || e>INT32_MAX || v<INT32_MIN || v>INT32_MAX) return 0;
    const int32_t e32=(int32_t)e,v32=(int32_t)v;int32_t now;
    unsigned __int128 before,after;memcpy(&before,line,16);after=before;memcpy(&now,(unsigned char*)&before+at,4);
    if(now!=e32) return 0;
    memcpy((unsigned char*)&after+at,&v32,4);
    return gate_code_swap128(line,before,after);
}
static void fa_restore(void) {
    if(!fa_installed) return;
    for(unsigned i=0;i<FA_L_SITES;i++)
        if((fa_done>>i)&1u && fa_site_state(i)==1 && fa_swap(i,fa_stub(fa_l_sites[i].kind),LX(fa_l_sites[i].target))) fa_done&=~(1ull<<i);
    if(!fa_done) {fa_installed=0;fa_restored=1;fa_stage="restored";fa_sites_hooked=0;}
}
static int fa_owned(void) {
    for(unsigned i=0;i<FA_L_SITES;i++) if((fa_done>>i)&1u && fa_site_state(i)!=1) return 0;
    return 1;
}
/* gate.fp_attack(first, last): after fp_redirect, before the first admitted frame. */
static int fa_arm(uint64_t first,uint64_t last) {
    if(fa_installed) return fa_first==first && fa_last==last;
    if(!fa_arm_ready(first,last)) return 0;
    if(!thunk) {fa_stage="page";return 0;}
    for(unsigned i=0;i<FA_L_SITES;i++) if(fa_site_state(i)!=0 || ((fa_l_sites[i].site+1u)&15u)>12u) {fa_stage="pins";return 0;}
    fa_native_gas=(fa_l10_fn)(void*)LX(0x1d1b9d0u);fa_native_wl=(fa_l17_fn)(void*)LX(0x1deb960u);
    fa_native_dps=(fa_l4_fn)(void*)LX(0xf05ff0u);fa_native_add=(fa_l2_fn)(void*)LX(0xf05be0u);
    fa_native_rng=(fa_lrng_fn)(void*)LX(0x1a491c0u);fa_native_dist=(fa_ldist_fn)(void*)LX(0x1dea820u);
    fa_native_apply=(fa_l6_fn)(void*)LX(0x17f8d90u);
    fa_installed=1;fa_restored=0;fa_done=0;fa_sites_hooked=0;fa_sites_skipped=FA_L_SKIPPED;
    for(unsigned i=0;i<FA_L_SITES;i++) {
        if(!fa_swap(i,LX(fa_l_sites[i].target),fa_stub(fa_l_sites[i].kind))) {fa_stage="cas";fa_restore();fa_installed=fa_done!=0;return 0;}
        fa_done|=1ull<<i;++fa_sites_hooked;
    }
    fa_stage="armed";return 1;
}

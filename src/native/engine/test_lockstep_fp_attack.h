/* FR29b (lockstep_fp_attack.h, lockstep_fp_attack_win.h) on Windows: the scenario of test_fp_attack_scenario.h through
 * the hooks (synthetic: the hooks called directly; native image: through the stubs on the fp redirect page after the
 * real arm), the rows both gates print, the window and the watch list, status, arm refusals; on the image: the pins
 * (through the view), the 42 sites raw and viewed, the swaps, the stubs, ownership, the scopes and the restoration.
 * Included by test_lockstep_fp_redirect.h. */
#include "test_fp_attack_scenario.h"
static int twfa_stubs; /* 1: the sub-calls through the stubs on fr_page (the native image), 0: the hooks themselves */
static void *twfa_entry(unsigned kind) {return twfa_stubs?(void*)fa_stub(kind):fa_hook_of(kind);}
static uint64_t twfa_wl(void *attacker,void *out1,void *out2,void *list4,uint64_t list5,uint64_t a6,uint64_t a7,uint64_t a8,uint64_t target,
                        uint64_t a10,uint64_t a11,uint64_t a12,uint64_t a13,uint64_t a14,uint64_t a15,uint64_t a16,uint64_t a17,uint64_t a18,uint64_t a19) {
    ++tfa_native[FA_WL];
    double d7;memcpy(&d7,&a7,8);float f8,f14,f15;const uint32_t l8=(uint32_t)a8,l14=(uint32_t)a14,l15=(uint32_t)a15;
    memcpy(&f8,&l8,4);memcpy(&f14,&l14,4);memcpy(&f15,&l15,4);
    assert(attacker==tfa_attacker && (void*)(uintptr_t)target==tfa_target && !list4 && !list5 && a6==0 && d7==-1.0 && f8==1204.5f && f14==150.0f && f15==30.0f);
    assert(a10==1 && a11==0 && a12==1 && a13==0 && a16==0 && a17==0 && a18==1 && a19==0 && out2);
    (void)((fa_wrng_fn)twfa_entry(FA_RNG))(&tfa_word,150.0f);
    tfa_damage local;tfa_damage_init(&local);
    (void)((fa_w4_fn)twfa_entry(FA_DPS))(tfa_weapon,&local,0,1);
    tfa_scale(&local,0.5);
    (void)((fa_w2_fn)twfa_entry(FA_ADD))(out1,&local);
    return 0;
}
static uint64_t twfa_gas(void *attacker,void *out1,void *out2,void *list4,uint64_t list5,uint64_t target,uint64_t b7,uint64_t b8,uint64_t b9,
                         uint64_t i10,uint64_t f11) {
    ++tfa_native[FA_GAS];
    assert(b7==1 && b8==0 && b9==1 && i10==1 && f11==0 && !list4 && !list5);
    const float d=((fa_wdist_fn)twfa_entry(FA_DIST))((void*)(uintptr_t)target,attacker,0,tfa_time,2,0x1234);assert(d==1234.5f);
    if(attacker!=tfa_attacker) return 0;
    tfa_damage wl1,wl2;tfa_damage_init(&wl1);tfa_damage_init(&wl2);
    const double a7=-1.0;const float fa8=d-30.0f,f150=150.0f,f30=30.0f;uint64_t b7_=0,b8_=0,b14=0,b15=0;
    memcpy(&b7_,&a7,8);memcpy(&b8_,&fa8,4);memcpy(&b14,&f150,4);memcpy(&b15,&f30,4);
    ((fa_w19_fn)twfa_entry(FA_WL))(attacker,&wl1,&wl2,NULL,0,0,b7_,b8_,target,1,0,1,0,b14,b15,0,0,1,0);
    (void)((fa_w2_fn)twfa_entry(FA_ADD))(out1,&wl1);
    (void)out2;return 0;
}
static __attribute__((used)) float twfa_dist(void *a,void *b,uint64_t c3,double time,uint64_t mode,uint64_t s6) {
    ++tfa_native[FA_DIST];assert(c3==0 && mode==2 && time==tfa_time && (a==tfa_target || a==tfa_other) && b);
    assert((uint32_t)s6==0x5a5a5a5au); /* the shim's write, as 0x3e1160 writes its sixth slot */
    return 1234.5f;
}
/* The distance's stand-in entry does what 0x3e1160 does to its frame: it checks rsp at entry (8 off 16) and writes a
 * dword into its sixth slot (entry rsp + 0x30) before the C body reads its arguments. The hook must own that slot. */
__asm__(".text\n.p2align 4\ntwfa_dist_entry:\n lea 8(%rsp),%rax\n test $15,%al\n jz 1f\n ud2\n1:\n movl $0x5a5a5a5a,0x30(%rsp)\n jmp twfa_dist\n");
extern char twfa_dist_entry[];
static float twfa_rng(uint64_t *state,float bound) {return tfa_rng_body(state,bound);}
static uint64_t twfa_dps(void *weapon,void *out,uint64_t a3,uint64_t flag) {assert(a3==0 && flag==1);tfa_dps_body(weapon,(tfa_damage*)out);return 0;}
static uint64_t twfa_add(void *acc,void *addend) {tfa_add_body((tfa_damage*)acc,(const tfa_damage*)addend);return (uint64_t)(uintptr_t)acc;}
static uint64_t twfa_apply(void *target,void *attacker,void *damage,void *p4,uint64_t p5,uint64_t flag) {
    ++tfa_native[FA_APPLY];assert(target==tfa_target && attacker==tfa_attacker && damage && !p4 && !p5 && flag==1);return 0x101; /* al = 1 */
}
static void twfa_natives(void) {
    fa_native_gas=twfa_gas;fa_native_wl=twfa_wl;fa_native_dps=twfa_dps;fa_native_add=twfa_add;fa_native_rng=twfa_rng;fa_native_dist=(fa_wdist_fn)(void*)twfa_dist_entry;
    fa_native_apply=twfa_apply;
}
/* The scenario (the caller has armed or set the window {540,550} and the watch 07336a). */
static void twfa_scenario(void) {
    tfa_objects();stats.issued=546;
    tfa_damage out1,out2,dmg;tfa_damage_init(&out1);tfa_damage_init(&out2);
    const fa_w11_fn gas=(fa_w11_fn)twfa_entry(FA_GAS);
    gas(tfa_attacker,&out1,&out2,NULL,0,(uint64_t)(uintptr_t)tfa_target,1,0,1,1,0);
    assert(tfa_native[FA_GAS]==1 && tfa_native[FA_WL]==1 && tfa_native[FA_DPS]==1 && tfa_native[FA_ADD]==2 && tfa_native[FA_RNG]==1 && tfa_native[FA_DIST]==1);
    dmg=out1;assert(dmg.v[0]==5.25 && dmg.v[1]==10.125 && dmg.v[4]==15.375);
    assert((((fa_w6_fn)twfa_entry(FA_APPLY))(tfa_target,tfa_attacker,&dmg,NULL,0,1)&0xffu)==1);
    stats.issued=600;gas(tfa_attacker,&out1,&out2,NULL,0,(uint64_t)(uintptr_t)tfa_target,1,0,1,1,0);
    stats.issued=546;gas(tfa_other,&out1,&out2,NULL,0,(uint64_t)(uintptr_t)tfa_other,1,0,1,1,0);
    assert(tfa_native[FA_GAS]==3 && tfa_native[FA_WL]==2 && tfa_native[FA_DIST]==3 && tfa_native[FA_APPLY]==1);
    tfa_check_rows(fa_row,NULL);
    char out[1024];assert(fa_status(out,sizeof(out))>0);
    assert(strstr(out," captured=1 nested=0 no_context=0 outside=1 unread=0 open=0 records=8 dropped=0 rows=8"));
    assert(strstr(out," hits=gas:3,wl:2,dps:2,add:4,rng:2,dist:3,apply:1 kept=gas:1,wl:1,dps:1,add:2,rng:1,dist:1,apply:1"));
}
static world_identity_entry *twfa_prior_ids;static unsigned twfa_prior_count;static LONG twfa_prior_sealed;static int twfa_prior_installed;
static void twfa_identities(void) {
    twfa_prior_ids=world_identities;twfa_prior_count=world_identity_count;twfa_prior_sealed=world_identity_sealed;twfa_prior_installed=world_identity_installed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;world_identity_installed=1;
    assert(world_identity_record(0x501,0x96587) && world_identity_record(0x502,0x7336a) && world_identity_record(0x503,0x73371));
}
static void twfa_identities_back(void) {
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=twfa_prior_ids;world_identity_count=twfa_prior_count;
    world_identity_sealed=twfa_prior_sealed;world_identity_installed=twfa_prior_installed;
}
static void twfa_reset(void) {
    fa_installed=0;fa_restored=0;fa_stage="idle";fa_id_count=0;fa_records=0;fa_scan=fa_emitted=0;fa_test_rng=NULL;fa_test_time=NULL;
    fa_native_gas=NULL;fa_native_wl=NULL;fa_native_dps=NULL;fa_native_add=NULL;fa_native_rng=NULL;fa_native_dist=NULL;fa_native_apply=NULL;
}
static void test_fa_synthetic(void) {
    const uint64_t saved_issued=stats.issued;const DWORD saved_ui=ui_thread;const uint32_t saved_active=stats.active;
    const int saved_boot=boot_mode,saved_phase=boot_phase;const unsigned saved_fr=fr_installed;unsigned char *saved_page=fr_page;
    twfa_identities();
    stats.active=1;boot_mode=0;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();fr_page=NULL;
    /* refusals: fp_redirect not armed, the window, identities, the page */
    fr_installed=0;assert(!fa_arm(540,550) && !strcmp(fa_stage,"refused"));fr_installed=1;
    assert(!fa_arm(0,550) && !strcmp(fa_stage,"window") && !fa_arm(551,550) && !strcmp(fa_stage,"window"));
    world_identity_installed=0;assert(!fa_arm(540,550) && !strcmp(fa_stage,"needs"));world_identity_installed=1;
    assert(!fa_arm(540,550) && !strcmp(fa_stage,"page") && !fa_installed);
    assert(fa_watch(0x7336a) && fa_watch(0x7336a) && !fa_watch(0) && fa_id_count==1);
    /* as armed (the hooks themselves; the sites need the image) */
    assert(fa_arm_ready(540,550));fa_installed=1;twfa_natives();twfa_stubs=0;
    twfa_scenario();
    twfa_reset();twfa_identities_back();
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;fr_installed=saved_fr;fr_page=saved_page;
    printf("fp attack FR29b synthetic: arm refusals, the scenario through the hooks, rows equal to the shared text (both gates), window/watch, status PASS\n");
}
/* ---- FR29b on the executable's own bytes (fr29w: client 1 died in the first captured get_attackstrength) ----
 * This copy is mapped read-write: the pages that run are made executable for these cases only: GetAttackStrength's
 * add chain (0x4f5602..0x4f5976), the leaf scale 0xf01b0 and add 0xf0260, the draw 0x14ddc90. */
#define TWFA_EXEC 3u
static const uint32_t twfa_exec[TWFA_EXEC][2]={{0x4f5000u,0x1000u},{0xf0000u,0x1000u},{0x14dd000u,0x1000u}};
static DWORD twfa_exec_old[TWFA_EXEC];
static void twfa_exec_on(void) {
    for(unsigned i=0;i<TWFA_EXEC;i++) {
        assert(VirtualProtect(image+twfa_exec[i][0],twfa_exec[i][1],PAGE_EXECUTE_READWRITE,&twfa_exec_old[i]));
        FlushInstructionCache(GetCurrentProcess(),image+twfa_exec[i][0],twfa_exec[i][1]);
    }
}
static void twfa_exec_off(void) {
    for(unsigned i=0;i<TWFA_EXEC;i++) {DWORD ignored;assert(VirtualProtect(image+twfa_exec[i][0],twfa_exec[i][1],twfa_exec_old[i],&ignored));}
}
/* One call with every register chosen: twfa_regs_run(block) loads rax..r15, rbp (qwords 1..15) and xmm0-15 (bytes
 * 0x80..0x17f) from block, calls twfa_regs_target with rsp 16-aligned, stores them back at +0x180 (same layout) and
 * rsp after the call minus rsp before it at +0x300. twfa_regs_ret is the call's return address. */
#define TWR_IN(r) (r)
#define TWR_OUT(r) (48u+(r))
enum {TWR_RAX=1,TWR_RCX,TWR_RDX,TWR_R8,TWR_R9,TWR_R10,TWR_R11,TWR_RBX,TWR_RSI,TWR_RDI,TWR_R12,TWR_R13,TWR_R14,TWR_R15,TWR_RBP,TWR_XMM=16};
static volatile uint64_t twfa_regs_target __attribute__((used)),twfa_regs_block __attribute__((used)),twfa_regs_rsp __attribute__((used)),
                         twfa_regs_rax __attribute__((used));
__asm__(".text\n.p2align 4\ntwfa_regs_run:\n"
" push %rbp\n push %rbx\n push %rsi\n push %rdi\n push %r12\n push %r13\n push %r14\n push %r15\n sub $0xa8,%rsp\n"
" movdqu %xmm6,0x00(%rsp)\n movdqu %xmm7,0x10(%rsp)\n movdqu %xmm8,0x20(%rsp)\n movdqu %xmm9,0x30(%rsp)\n movdqu %xmm10,0x40(%rsp)\n"
" movdqu %xmm11,0x50(%rsp)\n movdqu %xmm12,0x60(%rsp)\n movdqu %xmm13,0x70(%rsp)\n movdqu %xmm14,0x80(%rsp)\n movdqu %xmm15,0x90(%rsp)\n"
" mov %rcx,twfa_regs_block(%rip)\n sub $0x40,%rsp\n mov %rsp,twfa_regs_rsp(%rip)\n mov %rcx,%r11\n"
" movdqu 0x80(%r11),%xmm0\n movdqu 0x90(%r11),%xmm1\n movdqu 0xa0(%r11),%xmm2\n movdqu 0xb0(%r11),%xmm3\n movdqu 0xc0(%r11),%xmm4\n"
" movdqu 0xd0(%r11),%xmm5\n movdqu 0xe0(%r11),%xmm6\n movdqu 0xf0(%r11),%xmm7\n movdqu 0x100(%r11),%xmm8\n movdqu 0x110(%r11),%xmm9\n"
" movdqu 0x120(%r11),%xmm10\n movdqu 0x130(%r11),%xmm11\n movdqu 0x140(%r11),%xmm12\n movdqu 0x150(%r11),%xmm13\n movdqu 0x160(%r11),%xmm14\n"
" movdqu 0x170(%r11),%xmm15\n"
" mov 0x08(%r11),%rax\n mov 0x10(%r11),%rcx\n mov 0x18(%r11),%rdx\n mov 0x20(%r11),%r8\n mov 0x28(%r11),%r9\n mov 0x30(%r11),%r10\n"
" mov 0x40(%r11),%rbx\n mov 0x48(%r11),%rsi\n mov 0x50(%r11),%rdi\n mov 0x58(%r11),%r12\n mov 0x60(%r11),%r13\n mov 0x68(%r11),%r14\n"
" mov 0x70(%r11),%r15\n mov 0x78(%r11),%rbp\n mov 0x38(%r11),%r11\n"
" call *twfa_regs_target(%rip)\ntwfa_regs_ret:\n"
" mov %rax,twfa_regs_rax(%rip)\n mov twfa_regs_block(%rip),%rax\n"
" mov %rcx,0x190(%rax)\n mov %rdx,0x198(%rax)\n mov %r8,0x1a0(%rax)\n mov %r9,0x1a8(%rax)\n mov %r10,0x1b0(%rax)\n mov %r11,0x1b8(%rax)\n"
" mov %rbx,0x1c0(%rax)\n mov %rsi,0x1c8(%rax)\n mov %rdi,0x1d0(%rax)\n mov %r12,0x1d8(%rax)\n mov %r13,0x1e0(%rax)\n mov %r14,0x1e8(%rax)\n"
" mov %r15,0x1f0(%rax)\n mov %rbp,0x1f8(%rax)\n"
" movdqu %xmm0,0x200(%rax)\n movdqu %xmm1,0x210(%rax)\n movdqu %xmm2,0x220(%rax)\n movdqu %xmm3,0x230(%rax)\n movdqu %xmm4,0x240(%rax)\n"
" movdqu %xmm5,0x250(%rax)\n movdqu %xmm6,0x260(%rax)\n movdqu %xmm7,0x270(%rax)\n movdqu %xmm8,0x280(%rax)\n movdqu %xmm9,0x290(%rax)\n"
" movdqu %xmm10,0x2a0(%rax)\n movdqu %xmm11,0x2b0(%rax)\n movdqu %xmm12,0x2c0(%rax)\n movdqu %xmm13,0x2d0(%rax)\n movdqu %xmm14,0x2e0(%rax)\n"
" movdqu %xmm15,0x2f0(%rax)\n"
" mov %rsp,%rcx\n sub twfa_regs_rsp(%rip),%rcx\n mov %rcx,0x300(%rax)\n mov twfa_regs_rax(%rip),%rcx\n mov %rcx,0x188(%rax)\n"
" mov twfa_regs_rsp(%rip),%rsp\n add $0x40,%rsp\n"
" movdqu 0x00(%rsp),%xmm6\n movdqu 0x10(%rsp),%xmm7\n movdqu 0x20(%rsp),%xmm8\n movdqu 0x30(%rsp),%xmm9\n movdqu 0x40(%rsp),%xmm10\n"
" movdqu 0x50(%rsp),%xmm11\n movdqu 0x60(%rsp),%xmm12\n movdqu 0x70(%rsp),%xmm13\n movdqu 0x80(%rsp),%xmm14\n movdqu 0x90(%rsp),%xmm15\n"
" add $0xa8,%rsp\n pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n");
extern void twfa_regs_run(uint64_t *block);
extern char twfa_regs_ret[];
/* Every register a sentinel (rax..rbp: 0x51005100510000rr; xmm k: 0x610061006100000k / 0x710071007100000k), then
 * the arguments. */
static void twfa_regs_fill(uint64_t block[97]) {
    memset(block,0,97*8);
    for(unsigned r=TWR_RAX;r<=TWR_RBP;r++) block[r]=0x5100510051000000ull+r;
    for(unsigned k=0;k<16;k++) {block[TWR_XMM+2*k]=0x6100610061000000ull+k;block[TWR_XMM+2*k+1]=0x7100710071000000ull+k;}
}
static void twfa_regs_call(const void *target,uint64_t block[97]) {twfa_regs_target=(uint64_t)(uintptr_t)target;twfa_regs_run(block);}
/* out == in for register r (a general one, or xmm k as TWR_XMM+2k). */
static int twfa_kept(const uint64_t block[97],unsigned r) {
    if(r>=TWR_XMM) return block[TWR_OUT(r)]==block[TWR_IN(r)] && block[TWR_OUT(r+1)]==block[TWR_IN(r+1)];
    return block[TWR_OUT(r)]==block[TWR_IN(r)];
}
static int twfa_nonvolatile_kept(const uint64_t block[97]) {
    static const unsigned nv[]={TWR_RBX,TWR_RSI,TWR_RDI,TWR_R12,TWR_R13,TWR_R14,TWR_R15,TWR_RBP};
    for(unsigned i=0;i<sizeof(nv)/sizeof(nv[0]);i++) if(!twfa_kept(block,nv[i])) return 0;
    for(unsigned k=6;k<16;k++) if(!twfa_kept(block,TWR_XMM+2*k)) return 0;
    return block[96]==0; /* rsp back where it was */
}
/* A captured get_attackstrength on this thread (attacker 96587, target 07336a watched, tick 546), opened and closed as
 * the gas hook does around the native. */
static fa_ctx *twfa_open(fa_record *r,tfa_damage *out1,tfa_damage *out2) {
    stats.issued=546;fa_ctx *c=fa_gas_enter(r,tfa_attacker,tfa_target,out1,out2);assert(c && c->depth==1);return c;
}
/* The rows since the last drain: how many contain text; *total all of them. */
static unsigned twfa_rows_with(const char *text,unsigned *total) {
    char out[2048];unsigned n=0,t=0;
    while(fa_row(fa_emitted,out,sizeof(out))>0) {++t;n+=strstr(out,text)!=NULL;}
    if(total) *total=t;
    return n;
}
/* The leaf kinds' register contract through the real stub: add 0xf0260 and the draw 0x14ddc90 on their own bytes,
 * called directly and through fa_stub(kind) (fast path, then inside a captured call): the results equal and every
 * register the native keeps kept (add: rcx, rdx, r8-r11; draw: rcx, r8-r11, xmm1-5; both: rbx..rbp, xmm6-15, rsp). */
static void twfa_leaf_contract(int captured) {
    static __declspec(align(16)) uint64_t n[97],h[97];
    fa_record gr;tfa_damage o1,o2;fa_ctx *c=NULL;
    if(captured) {tfa_damage_init(&o1);tfa_damage_init(&o2);c=twfa_open(&gr,&o1,&o2);}
    const LONG64 add0=fa_kept[FA_ADD],rng0=fa_kept[FA_RNG];
    /* the add */
    tfa_damage acc[2],addend[2];
    for(unsigned i=0;i<2;i++) {
        tfa_damage_init(&acc[i]);tfa_damage_init(&addend[i]);
        for(unsigned k=0;k<5;k++) {acc[i].v[k]=1.5*(k+1);addend[i].v[k]=0.25*(k+3);}
        acc[i].f[3]=1.0f;addend[i].f[3]=0.5f;acc[i].flags=3;addend[i].flags=5;
    }
    twfa_regs_fill(n);n[TWR_RCX]=(uint64_t)(uintptr_t)&acc[0];n[TWR_RDX]=(uint64_t)(uintptr_t)&addend[0];
    twfa_regs_fill(h);h[TWR_RCX]=(uint64_t)(uintptr_t)&acc[1];h[TWR_RDX]=(uint64_t)(uintptr_t)&addend[1];
    twfa_regs_call(image+0xf0260u,n);twfa_regs_call(fa_stub(FA_ADD),h);
    assert(!memcmp(&acc[0],&acc[1],sizeof(acc[0])) && !memcmp(&addend[0],&addend[1],sizeof(addend[0])));
    assert(n[TWR_OUT(TWR_RAX)]==n[TWR_RCX] && h[TWR_OUT(TWR_RAX)]==h[TWR_RCX]);
    static const unsigned add_kept[]={TWR_RCX,TWR_RDX,TWR_R8,TWR_R9,TWR_R10,TWR_R11};
    for(unsigned i=0;i<sizeof(add_kept)/sizeof(add_kept[0]);i++) {
        assert(twfa_kept(n,add_kept[i])); /* what the native keeps (why GetAttackStrength keeps rcx and r8 across it) */
        if(!twfa_kept(h,add_kept[i])) {printf("fp attack FR29b: the add stub lost register %u (%s)\n",add_kept[i],captured?"captured":"fast");fflush(stdout);assert(0);}
    }
    assert(twfa_nonvolatile_kept(n) && twfa_nonvolatile_kept(h));
    /* the draw */
    uint64_t state[2]={0x1111222233334444ull,0x1111222233334444ull};const float bound=150.0f;uint32_t b;memcpy(&b,&bound,4);
    twfa_regs_fill(n);n[TWR_RCX]=(uint64_t)(uintptr_t)&state[0];n[TWR_XMM+2]=0x6101610100000000ull|b;
    twfa_regs_fill(h);h[TWR_RCX]=(uint64_t)(uintptr_t)&state[1];h[TWR_XMM+2]=0x6101610100000000ull|b;
    twfa_regs_call(image+0x14ddc90u,n);twfa_regs_call(fa_stub(FA_RNG),h);
    assert(state[0]==state[1] && state[0]!=0x1111222233334444ull && (uint32_t)n[TWR_OUT(TWR_XMM)]==(uint32_t)h[TWR_OUT(TWR_XMM)]);
    static const unsigned rng_kept[]={TWR_RCX,TWR_R8,TWR_R9,TWR_R10,TWR_R11,TWR_XMM+2,TWR_XMM+4,TWR_XMM+6,TWR_XMM+8,TWR_XMM+10};
    for(unsigned i=0;i<sizeof(rng_kept)/sizeof(rng_kept[0]);i++) {
        assert(twfa_kept(n,rng_kept[i]));
        if(!twfa_kept(h,rng_kept[i])) {printf("fp attack FR29b: the draw stub lost register %u (%s)\n",rng_kept[i],captured?"captured":"fast");fflush(stdout);assert(0);}
    }
    assert(twfa_nonvolatile_kept(n) && twfa_nonvolatile_kept(h));
    if(captured) {
        fa_gas_exit(c,&gr,&o1,&o2);
        assert(fa_kept[FA_ADD]==add0+1 && fa_kept[FA_RNG]==rng0+1);
        unsigned total;assert(twfa_rows_with(" kind=add in=exec ",&total)==1 && total==3); /* add, draw, gas */
    } else assert(fa_kept[FA_ADD]==add0 && fa_kept[FA_RNG]==rng0);
}
/* GetAttackStrength's add chain on its own bytes: entered at 0x4f5602 with a frame (rbp), r14 (its globals: the
 * factors +0x54e0..+0x54f8, the capital factor +0x5528), r12 (the target's defensible: a vtable whose +0x2160 is
 * twfa_capital), xmm7 (|x| mask), xmm8 (the threshold), xmm14 (1.0), and run to 0x4f5976 (a jump planted there leads
 * back to twfa_block_back). In between: the scale 0xf01b0, the virtual at 0x4f5737 when r8d (the flag [rbp-0x80],
 * loaded at 0x4f5602) is 0, and the adds at the hooked sites 0x4f5629 0x4f56e1 0x4f5764 0x4f5820 0x4f58ba 0x4f5971,
 * the second, fourth, fifth and sixth passing the rcx the add before them kept. */
static volatile uint64_t twfa_block_ptr __attribute__((used)),twfa_block_rsp __attribute__((used));
__asm__(".text\n.p2align 4\ntwfa_block_run:\n"
" push %rbp\n push %rbx\n push %rsi\n push %rdi\n push %r12\n push %r13\n push %r14\n push %r15\n sub $0xa8,%rsp\n"
" movdqu %xmm6,0x00(%rsp)\n movdqu %xmm7,0x10(%rsp)\n movdqu %xmm8,0x20(%rsp)\n movdqu %xmm9,0x30(%rsp)\n movdqu %xmm10,0x40(%rsp)\n"
" movdqu %xmm11,0x50(%rsp)\n movdqu %xmm12,0x60(%rsp)\n movdqu %xmm13,0x70(%rsp)\n movdqu %xmm14,0x80(%rsp)\n movdqu %xmm15,0x90(%rsp)\n"
" mov %rcx,twfa_block_ptr(%rip)\n mov %rsp,twfa_block_rsp(%rip)\n mov %rcx,%rax\n"
" mov 0x08(%rax),%rbp\n mov 0x10(%rax),%r12\n mov 0x18(%rax),%r13\n mov 0x20(%rax),%r14\n"
" movdqu 0x30(%rax),%xmm7\n movdqu 0x40(%rax),%xmm8\n movdqu 0x50(%rax),%xmm14\n"
" movabs $0x5b5b5b5b5b5b5b01,%rbx\n movabs $0x5b5b5b5b5b5b5b02,%rsi\n movabs $0x5b5b5b5b5b5b5b03,%rdi\n movabs $0x5b5b5b5b5b5b5b04,%r15\n"
" movabs $0x5c5c5c5c5c5c5c01,%rcx\n movabs $0x5c5c5c5c5c5c5c02,%rdx\n movabs $0x5c5c5c5c5c5c5c03,%r8\n movabs $0x5c5c5c5c5c5c5c04,%r9\n"
" movabs $0x5c5c5c5c5c5c5c05,%r10\n movabs $0x5c5c5c5c5c5c5c06,%r11\n"
" sub $0x100,%rsp\n jmp *0x00(%rax)\n"
"twfa_block_back:\n mov twfa_block_ptr(%rip),%rax\n"
" movdqu %xmm14,0x60(%rax)\n mov %rbx,0x70(%rax)\n mov %r12,0x78(%rax)\n mov %r13,0x80(%rax)\n mov %r14,0x88(%rax)\n mov %rbp,0x90(%rax)\n"
" mov %rsp,%rcx\n add $0x100,%rcx\n sub twfa_block_rsp(%rip),%rcx\n mov %rcx,0x98(%rax)\n"
" mov twfa_block_rsp(%rip),%rsp\n"
" movdqu 0x00(%rsp),%xmm6\n movdqu 0x10(%rsp),%xmm7\n movdqu 0x20(%rsp),%xmm8\n movdqu 0x30(%rsp),%xmm9\n movdqu 0x40(%rsp),%xmm10\n"
" movdqu 0x50(%rsp),%xmm11\n movdqu 0x60(%rsp),%xmm12\n movdqu 0x70(%rsp),%xmm13\n movdqu 0x80(%rsp),%xmm14\n movdqu 0x90(%rsp),%xmm15\n"
" add $0xa8,%rsp\n pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n");
extern void twfa_block_run(uint64_t *block);
extern char twfa_block_back[];
#define TWFA_BLOCK_ENTRY 0x4f5602u
#define TWFA_BLOCK_END 0x4f5976u
static __declspec(align(16)) unsigned char twfa_frame[0x600],twfa_consts[0x5540];
static void *twfa_vtable[0x2160/8+1];static void *twfa_object[4];
static unsigned twfa_capital_calls;static unsigned char twfa_capital_answer;
static unsigned char twfa_capital(void *self) {assert(self==(void*)twfa_object);++twfa_capital_calls;return twfa_capital_answer;}
typedef struct {tfa_damage acc[2];unsigned char frame[0x600];uint64_t xmm14[2],rbx;unsigned capital;} twfa_block_result;
static unsigned char twfa_end_original[14];
static void twfa_block_plant(int on) {
    if(on) {
        memcpy(twfa_end_original,image+TWFA_BLOCK_END,14);
        unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)twfa_block_back;memcpy(back+6,&to,8);memcpy(image+TWFA_BLOCK_END,back,14);
    } else memcpy(image+TWFA_BLOCK_END,twfa_end_original,14);
    FlushInstructionCache(GetCurrentProcess(),image+TWFA_BLOCK_END,14);
}
/* flag: [rbp-0x80] (0: the virtual is called, as native would); small: [rbp+0x1a8] = [rbp+0x158] = 0 (0x4f576b's
 * path: the r8 test and the add at 0x4f5764 skipped, rcx loaded at 0x4f576b). */
static void twfa_block_case(uint32_t flag,int small,unsigned char answer,twfa_block_result *out) {
    static __declspec(align(16)) uint64_t block[20];
    unsigned char *rbp=twfa_frame+0x100;
    for(unsigned i=0;i<sizeof(twfa_frame)/8;i++) {const double v=1.0+(double)(i%37)*0.0625;memcpy(twfa_frame+8*i,&v,8);}
    memcpy(rbp-0x80,&flag,4);
    if(small) {const double z=0.0;memcpy(rbp+0x1a8,&z,8);memcpy(rbp+0x158,&z,8);}
    static tfa_damage acc[2]; /* one place for every run: the frame holds their addresses */
    for(unsigned i=0;i<2;i++) {
        tfa_damage_init(&acc[i]);
        for(unsigned k=0;k<5;k++) acc[i].v[k]=2.0*(k+1)+i;
    }
    void *a0=&acc[0],*a1=&acc[1];memcpy(rbp+0x4c0,&a0,8);memcpy(rbp+0x4b8,&a1,8); /* gas.out2, gas.out1 */
    memset(twfa_consts,0,sizeof(twfa_consts));
    const float k54e0=0.5f,k54e4=1.25f,k54ec=0.875f,k54f4=1.5f,k54f8=0.625f,k5528[4]={0.75f,2.0f,3.0f,4.0f};
    memcpy(twfa_consts+0x54e0,&k54e0,4);memcpy(twfa_consts+0x54e4,&k54e4,4);memcpy(twfa_consts+0x54ec,&k54ec,4);
    memcpy(twfa_consts+0x54f4,&k54f4,4);memcpy(twfa_consts+0x54f8,&k54f8,4);memcpy(twfa_consts+0x5528,k5528,16);
    for(unsigned i=0;i<sizeof(twfa_vtable)/sizeof(twfa_vtable[0]);i++) twfa_vtable[i]=NULL;
    twfa_vtable[0x2160/8]=(void*)twfa_capital;twfa_object[0]=(void*)twfa_vtable;
    twfa_capital_calls=0;twfa_capital_answer=answer;
    memset(block,0,sizeof(block));
    block[0]=(uint64_t)(uintptr_t)(image+TWFA_BLOCK_ENTRY);block[1]=(uint64_t)(uintptr_t)rbp;block[2]=(uint64_t)(uintptr_t)twfa_object;
    block[3]=0x2a36550u;block[4]=(uint64_t)(uintptr_t)twfa_consts;
    block[6]=block[7]=0x7fffffffffffffffull;const double eps=1e-6;memcpy(&block[8],&eps,8);
    const float one=1.0f;uint32_t ob;memcpy(&ob,&one,4);block[10]=ob;
    twfa_block_plant(1);twfa_block_run(block);twfa_block_plant(0);
    assert(block[0x78/8]==(uint64_t)(uintptr_t)twfa_object && block[0x80/8]==0x2a36550u && block[0x88/8]==(uint64_t)(uintptr_t)twfa_consts);
    assert(block[0x90/8]==(uint64_t)(uintptr_t)rbp && block[0x98/8]==0); /* rbp and rsp as entered */
    memcpy(out->acc,acc,sizeof(acc));memcpy(out->frame,twfa_frame,sizeof(twfa_frame));out->xmm14[0]=block[12];out->xmm14[1]=block[13];out->rbx=block[14];
    out->capital=twfa_capital_calls;
}
#define TWFA_CASES 4u
static const struct {uint32_t flag;int small;unsigned char answer;} twfa_cases[TWFA_CASES]={{5,0,1},{0,0,1},{0,0,0},{5,1,1}};
static twfa_block_result twfa_reference[TWFA_CASES];
static int twfa_block_same(const twfa_block_result *a,const twfa_block_result *b) {
    return !memcmp(a->acc,b->acc,sizeof(a->acc)) && !memcmp(a->frame,b->frame,sizeof(a->frame)) && a->xmm14[0]==b->xmm14[0] &&
           a->xmm14[1]==b->xmm14[1] && a->rbx==b->rbx && a->capital==b->capital;
}
/* Before the arm: the native run of each case (the sites still call 0xf0260). */
static void twfa_block_references(void) {
    for(unsigned i=0;i<TWFA_CASES;i++) twfa_block_case(twfa_cases[i].flag,twfa_cases[i].small,twfa_cases[i].answer,&twfa_reference[i]);
    assert(twfa_reference[0].capital==0 && twfa_reference[1].capital==1 && twfa_reference[2].capital==1 && twfa_reference[3].capital==0);
    assert((uint32_t)twfa_reference[0].xmm14[0]==0x3f800000u && (uint32_t)twfa_reference[1].xmm14[0]==0x3f400000u &&
           (uint32_t)twfa_reference[2].xmm14[0]==0x3f800000u);
    assert(memcmp(twfa_reference[0].acc,twfa_reference[1].acc,sizeof(twfa_reference[0].acc))); /* the capital factor shows */
}
/* After the arm, through the hooked sites: fast path, then each case inside a captured call (rows: the six adds, or
 * the five of the short path, from GetAttackStrength's sites: in=gas). */
static void twfa_block_hooked(void) {
    for(unsigned i=0;i<TWFA_CASES;i++) {
        twfa_block_result got;
        twfa_block_case(twfa_cases[i].flag,twfa_cases[i].small,twfa_cases[i].answer,&got);
        if(!twfa_block_same(&got,&twfa_reference[i])) {
            printf("fp attack FR29b: GetAttackStrength's add chain case %u (fast path) differs from native (capital %u/%u)\n",i,got.capital,twfa_reference[i].capital);
            fflush(stdout);assert(0);
        }
        fa_record gr;tfa_damage o1,o2;tfa_damage_init(&o1);tfa_damage_init(&o2);
        fa_ctx *c=twfa_open(&gr,&o1,&o2);
        twfa_block_case(twfa_cases[i].flag,twfa_cases[i].small,twfa_cases[i].answer,&got);
        fa_gas_exit(c,&gr,&o1,&o2);
        if(!twfa_block_same(&got,&twfa_reference[i])) {
            printf("fp attack FR29b: GetAttackStrength's add chain case %u (captured) differs from native (capital %u/%u)\n",i,got.capital,twfa_reference[i].capital);
            fflush(stdout);assert(0);
        }
        unsigned total;const unsigned adds=twfa_rows_with(" kind=add in=gas ",&total);
        if(adds!=(twfa_cases[i].small?5u:6u) || total!=adds+1u) {printf("fp attack FR29b: case %u rows %u adds %u\n",i,total,adds);fflush(stdout);assert(0);}
    }
}
/* Unwinding: the leaf entries have unwind entries; a stack walk from inside the add's native reaches the caller of the
 * stub; an exception raised in the add's or the draw's native or in get_attackstrength's passes the stub, entry and
 * hook to a handler above them; a captured call it unwinds is closed (no row, fa_open back, unwound counted). */
#define TWFA_RAISED 0xE0465A29u
static void *twfa_walk[6];static USHORT twfa_walked;
static uint64_t twfa_walk_add(void *acc,void *addend) {
    twfa_walked=RtlCaptureStackBackTrace(0,6,twfa_walk,NULL);
    tfa_add_body((tfa_damage*)acc,(const tfa_damage*)addend);return (uint64_t)(uintptr_t)acc;
}
static uint64_t twfa_raise_add(void *acc,void *addend) {(void)acc;(void)addend;RaiseException(TWFA_RAISED,0,0,NULL);return 0;}
static float twfa_raise_rng(uint64_t *state,float bound) {(void)state;(void)bound;RaiseException(TWFA_RAISED,0,0,NULL);return 0.0f;}
static uint64_t twfa_raise_gas(void *a,void *o1,void *o2,void *l4,uint64_t l5,uint64_t t,uint64_t b7,uint64_t b8,uint64_t b9,uint64_t i10,uint64_t b11) {
    (void)a;(void)o1;(void)o2;(void)l4;(void)l5;(void)t;(void)b7;(void)b8;(void)b9;(void)i10;(void)b11;
    RaiseException(TWFA_RAISED,0,0,NULL);return 0;
}
static uint64_t twfa_plain_gas(void *a,void *o1,void *o2,void *l4,uint64_t l5,uint64_t t,uint64_t b7,uint64_t b8,uint64_t b9,uint64_t i10,uint64_t b11) {
    (void)a;(void)o1;(void)o2;(void)l4;(void)l5;(void)t;(void)b7;(void)b8;(void)b9;(void)i10;(void)b11;return 7;
}
static __attribute__((noinline)) int twfa_raise_through(unsigned kind,void *a,void *b) {
    volatile int caught=0;
    __try {
        if(kind==FA_ADD) (void)((fa_w2_fn)(void*)fa_stub(FA_ADD))(a,b);
        else if(kind==FA_RNG) (void)((fa_wrng_fn)(void*)fa_stub(FA_RNG))((uint64_t*)a,150.0f);
        else (void)((fa_w11_fn)(void*)fa_stub(FA_GAS))(tfa_attacker,a,b,NULL,0,(uint64_t)(uintptr_t)tfa_target,1,0,1,1,0);
    } __except(GetExceptionCode()==TWFA_RAISED?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    return caught;
}
static void twfa_unwind(void) {
    for(unsigned k=0;k<2;k++) {
        unsigned char *entry=(unsigned char*)fa_hook_of(k?FA_RNG:FA_ADD);DWORD64 base=0;
        PRUNTIME_FUNCTION f=RtlLookupFunctionEntry((DWORD64)(uintptr_t)(entry+0x40),&base,NULL);
        assert(f && (unsigned char*)(uintptr_t)(base+f->BeginAddress)==entry && base+f->EndAddress>(DWORD64)(uintptr_t)(entry+0x40));
    }
    tfa_damage a,b;tfa_damage_init(&a);tfa_damage_init(&b);uint64_t word=1;
    for(int captured=0;captured<2;captured++) {
        fa_record gr;tfa_damage o1,o2;tfa_damage_init(&o1);tfa_damage_init(&o2);fa_ctx *c=captured?twfa_open(&gr,&o1,&o2):NULL;
        /* the stack walk from inside the native: the hook, the add's entry, then the stub's caller */
        static __declspec(align(16)) uint64_t blk[97];
        fa_native_add=twfa_walk_add;twfa_walked=0;
        twfa_regs_fill(blk);blk[TWR_RCX]=(uint64_t)(uintptr_t)&a;blk[TWR_RDX]=(uint64_t)(uintptr_t)&b;twfa_regs_call(fa_stub(FA_ADD),blk);
        int entry_seen=0,caller_seen=0;
        for(USHORT i=0;i<twfa_walked;i++) {
            entry_seen+=(unsigned char*)twfa_walk[i]>(unsigned char*)fa_keep_add && (unsigned char*)twfa_walk[i]<(unsigned char*)fa_keep_add+0x100;
            caller_seen+=twfa_walk[i]==(void*)twfa_regs_ret;
        }
        if(entry_seen!=1 || caller_seen!=1) {printf("fp attack FR29b: stack walk %u frames, entry %d caller %d\n",twfa_walked,entry_seen,caller_seen);fflush(stdout);assert(0);}
        /* exceptions through the stub, the entry and the hook */
        fa_native_add=twfa_raise_add;assert(twfa_raise_through(FA_ADD,&a,&b));
        fa_native_rng=twfa_raise_rng;assert(twfa_raise_through(FA_RNG,&word,NULL));
        fa_native_add=(fa_w2_fn)(void*)(image+0xf0260u);fa_native_rng=(fa_wrng_fn)(void*)(image+0x14ddc90u);
        if(captured) {assert(c->depth==1);fa_gas_exit(c,&gr,&o1,&o2);}
        unsigned total;(void)twfa_rows_with("-",&total);assert(total==(unsigned)(captured?2:0)); /* the walked add, the gas */
    }
    /* an exception out of get_attackstrength in a captured call: closed, not left open */
    const LONG64 open0=fa_open,unwound0=fa_unwound,kept0=fa_kept[FA_GAS],captured0=fa_calls_captured;stats.issued=546;
    tfa_damage o1,o2;tfa_damage_init(&o1);tfa_damage_init(&o2);
    fa_native_gas=twfa_raise_gas;assert(twfa_raise_through(FA_GAS,&o1,&o2));
    fa_ctx *c=fa_context(0);
    assert(c && c->depth==0 && fa_open==open0 && fa_unwound==unwound0+1 && fa_kept[FA_GAS]==kept0 && fa_calls_captured==captured0+1);
    fa_native_gas=twfa_plain_gas;
    assert(((fa_w11_fn)(void*)fa_stub(FA_GAS))(tfa_attacker,&o1,&o2,NULL,0,(uint64_t)(uintptr_t)tfa_target,1,0,1,1,0)==7);
    assert(fa_kept[FA_GAS]==kept0+1 && fa_calls_captured==captured0+2 && fa_open==open0);
    unsigned total;assert(twfa_rows_with(" kind=gas ",&total)==1 && total==1);
    char out[1024];assert(fa_status(out,sizeof(out))>0 && strstr(out," unwound=1"));
    fa_native_gas=(fa_w11_fn)(void*)(image+0x4f43d0u);
}
/* Inside test_fp_redirect_native_image, while fp_redirect is armed in 'redirect' (exact reciprocal on: the view covers
 * the image): the pins, the sites, the arm, the stubs, the scenario through them, ownership and the view. */
static void test_fa_native_image(void) {
    twfa_exec_on();
    twfa_block_references(); /* native, before the arm (the planted end jump taken out again) */
    assert(fa_pins_ok());
    for(unsigned i=0;i<FA_W_PINS;i++) {
        unsigned char *b=image+fa_w_pins[i].rva+1u;*b^=1;assert(!fa_pins_ok());*b^=1;
    }
    assert(fa_pins_ok());
    for(unsigned i=0;i<FA_W_SITES;i++) {
        const uint32_t s=fa_w_sites[i].site;int32_t d;memcpy(&d,image+s+1,4);
        assert(image[s]==0xe8 && s+5u+(uint32_t)d==fa_w_sites[i].target && fa_site_state(i)==0 && fa_site_clear(i));
        unsigned in=0;for(unsigned j=0;j<FA_W_PINS;j++) in+=s>=fa_w_pins[j].rva && s+5u<=fa_w_pins[j].rva+fa_w_pins[j].size;
        assert(in==1); /* every site inside a pinned caller */
    }
    twfa_identities();
    assert(fa_watch(0x7336a) && fa_arm(540,550) && fa_installed && !strcmp(fa_stage,"armed") && fa_arm(540,550) && !fa_arm(1,2));
    assert(fa_native_gas==(fa_w11_fn)(void*)(image+0x4f43d0u) && fa_native_apply==(fa_w6_fn)(void*)(image+0x5209c0u) && fa_sites_hooked==FA_W_SITES);
    for(unsigned i=0;i<FA_W_SITES;i++) {
        const uint32_t s=fa_w_sites[i].site;int32_t d;memcpy(&d,image+s+1,4);unsigned char viewed[5];
        assert(fa_site_state(i)==1 && image+s+5+d==fa_stub(fa_w_sites[i].kind) && !fa_site_clear(i));
        assert(read_local(image+s,viewed,5) && viewed[0]==0xe8 && !memcmp(viewed+1,(int32_t[]){fa_rel_native(i)},4)); /* the view: the original */
        assert(fa_scope_of(image+s+5)==fa_w_sites[i].scope);
    }
    assert(fa_pins_ok() && fr_owned() && fr_check()); /* the pins see the original rel32s */
    for(unsigned k=FA_GAS;k<FA_KINDS;k++) {void *t;memcpy(&t,fa_stub(k)+6,8);assert(!memcmp(fa_stub(k),"\xff\x25\0\0\0\0",6) && t==fa_hook_of(k));}
    /* the scenario through the stubs (stand-in callees) */
    twfa_natives();twfa_stubs=1;
    twfa_scenario();
    twfa_stubs=0;
    /* ownership: a site changed under us */
    {const uint32_t s=fa_w_sites[5].site;image[s+2]^=1;assert(!fa_owned() && !fr_owned());image[s+2]^=1;assert(fa_owned() && fr_owned());}
    /* the executable's own bytes through the stubs (the real natives again) */
    fa_native_add=(fa_w2_fn)(void*)(image+0xf0260u);fa_native_rng=(fa_wrng_fn)(void*)(image+0x14ddc90u);
    fa_native_gas=(fa_w11_fn)(void*)(image+0x4f43d0u);
    twfa_leaf_contract(0);twfa_leaf_contract(1);
    twfa_block_hooked();
    twfa_unwind();
    twfa_exec_off();
    twfa_identities_back();fa_test_rng=NULL;fa_test_time=NULL;stats.issued=0;
    printf("fp attack FR29b native image: pins (eleven functions through the view), 42 call sites raw/viewed/scoped, armed to the stubs, the scenario through them, rows equal to the shared text, ownership PASS\n");
    printf("fp attack FR29b on the executable's bytes: the add 0xf0260 and the draw 0x14ddc90 through their stubs keep every register the "
           "natives keep (fast, captured); GetAttackStrength's add chain 0x4f5602..0x4f5976 (rcx and r8 kept across the adds) through the "
           "hooked sites equals native in %u cases (fast, captured; rows in=gas); unwind entries, a stack walk and exceptions through "
           "stub/entry/hook; a get_attackstrength unwound by an exception closed PASS\n",TWFA_CASES);
}
/* After fr_restore: every site back to its callee, nothing of ours left. */
static void test_fa_native_image_restored(void) {
    assert(!fa_installed && fa_restored && !fa_done && !strcmp(fa_stage,"restored"));
    for(unsigned i=0;i<FA_W_SITES;i++) assert(fa_site_state(i)==0 && fa_site_clear(i));
    twfa_reset();
}

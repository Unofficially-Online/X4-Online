/* FR30 on the Linux X4 9.00 executable (lockstep_fp_redirect.h fdr_*, lockstep_drone_factors.h): the four drone factor
 * sites in 0x1ded490, their stubs on the thunk page (LXF_DR_STUB, written with the page) and the install. Included by
 * gate_fp_redirect.h after the FR29b part; armed after native_gate_fp_redirect.
 * Each site's first five bytes change in one compare-exchange of their aligned 16-byte line (0x1dedf9a +10, 0x1dee22a
 * +10, 0x1dedb03 +3, 0x1dee2c1 +1; none of those lines holds an FR29b rel32). No branch lands inside the bytes a jmp
 * covers or skips (checked over the function: its branches are all direct, and no e8/e9 of the image lands there).
 * The flags are dead at every back address: 0x1dedfa2 comiss; 0x1dee234 mulss, movss, jmp 0x1deda9b movss, pxor,
 * comiss; 0x1dedb0d mulss, movss, movss, comiss; 0x1dee2d9 comiss. The time and roll sites' tail, mulss
 * xmm0,[rsp+0x48], runs in the stub after every register is back (rsp as the function's). The executable's file pin
 * covers every byte; the arm also checks the sites' bytes and the three constants their native ops read. */
#define LXF_DR_STUB 0xe20u /* FR30: the drone factor sites' stubs (0xe20..0xfe0, FDR_STUB_SIZE each) */
_Static_assert(LXF_DR_STUB+FDR_STUB_SIZE*FDR_SITES<=GATE_PAGE,"the thunk page holds every stub");
static uint32_t fdr_done; /* bit i: site i is ours */
__asm__(".text\n.p2align 4\n.type fdr_common_l,@function\nfdr_common_l:\n"
        " pushq %rbp\n movq %rsp,%rbp\n"
        FDR_ASM_SAVE
        " movl 0x70(%rsp),%edi\n movq %rsp,%rsi\n callq *fdr_hook_calls(%rip)\n"
        FDR_ASM_RESTORE
        ".size fdr_common_l,.-fdr_common_l\n");
extern char fdr_common_l[];
static unsigned char *fdr_stub(unsigned i) {return thunk+LXF_DR_STUB+FDR_STUB_SIZE*i;}
static void fdr_thunk_write(unsigned char *page) {
    for(unsigned i=0;i<FDR_SITES;i++) fdr_stub_write(page+LXF_DR_STUB+FDR_STUB_SIZE*i,i,(void*)fdr_common_l,(void*)LX(fdr_sites[i].back));
}
static unsigned fdr_len(unsigned i) {return fdr_sites[i].back-fdr_sites[i].site;}
static void fdr_jump(unsigned i,unsigned char out[5]) {
    const int32_t d=(int32_t)((intptr_t)fdr_stub(i)-(intptr_t)(LX(fdr_sites[i].site)+5));out[0]=0xe9;memcpy(out+1,&d,4);
}
static int fdr_reach(unsigned i) {
    const intptr_t d=(intptr_t)fdr_stub(i)-(intptr_t)(LX(fdr_sites[i].site)+5);
    return d>INT32_MIN && d<INT32_MAX;
}
/* 0 original, 1 our jmp (the rest of the site as it was), -1 anything else. */
static int fdr_state(unsigned i) {
    const unsigned char *at=LX(fdr_sites[i].site);const unsigned n=fdr_len(i);unsigned char jump[5];
    if(!memcmp(at,fdr_sites[i].orig,n)) return 0;
    if(thunk && fdr_reach(i) && !memcmp(at+5,fdr_sites[i].orig+5,n-5)) {fdr_jump(i,jump);if(!memcmp(at,jump,5)) return 1;}
    return -1;
}
/* The first five bytes in their aligned line (every site: (site&15)+5 <= 16). */
static int fdr_swap(unsigned i,const unsigned char *expected,const unsigned char *value) {
    unsigned char *at=LX(fdr_sites[i].site),*line=(unsigned char*)((uintptr_t)at&~(uintptr_t)15);
    const unsigned offset=(unsigned)(at-line);
    unsigned __int128 current,desired;memcpy(&current,line,16);desired=current;
    if(offset+5>16 || memcmp((unsigned char*)&current+offset,expected,5)) return 0;
    memcpy((unsigned char*)&desired+offset,value,5);
    return gate_code_swap128(line,current,desired);
}
static int fdr_pins_ok(void) {
    for(unsigned i=0;i<FDR_SITES;i++) {
        const fdr_site *s=&fdr_sites[i];
        if(((s->site&15u)+5u)>16u) return 0;
        for(unsigned k=0;k<2;k++) {
            if(!s->rip[k]) continue;
            int32_t d;uint32_t v;memcpy(&d,s->orig+s->rip[k],4);memcpy(&v,LX(s->konst_at[k]),4);
            if(s->site+s->rip[k]+4u+(uint32_t)d!=s->konst_at[k] || v!=s->konst[k]) return 0;
        }
    }
    return 1;
}
static void fdr_restore(void) {
    if(!fdr_installed) return;
    for(unsigned i=0;i<FDR_SITES;i++)
        if((fdr_done>>i)&1u && fdr_state(i)==1) {
            unsigned char jump[5];fdr_jump(i,jump);
            if(fdr_swap(i,jump,(const unsigned char*)fdr_sites[i].orig)) fdr_done&=~(1u<<i);
        }
    if(!fdr_done) {fdr_installed=0;fdr_mode=FDR_OFF;fdr_stage="restored";}
}
static int fdr_owned(void) {
    for(unsigned i=0;i<FDR_SITES;i++) if((fdr_done>>i)&1u && fdr_state(i)!=1) return 0;
    return 1;
}
/* gate.fp_redirect_drones(mode): after fp_redirect, before the first admitted frame; a rerun with the same mode is
 * accepted. */
static int fdr_arm(int mode) {
    if(fdr_installed) return fdr_mode==mode;
    if(!fdr_arm_ready(mode)) return 0;
    if(!thunk) {fdr_stage="page";return 0;}
    if(!fdr_pins_ok()) {fdr_stage="pins";return 0;}
    for(unsigned i=0;i<FDR_SITES;i++) {
        if(fdr_state(i)!=0) {fdr_stage="pins";return 0;}
        if(!fdr_reach(i)) {fdr_stage="page";return 0;}
    }
    fdr_mode=mode;fdr_installed=1;fdr_done=0;
    for(unsigned i=0;i<FDR_SITES;i++) {
        unsigned char jump[5];fdr_jump(i,jump);
        if(!fdr_swap(i,(const unsigned char*)fdr_sites[i].orig,jump)) {
            fdr_restore();if(!fdr_done) {fdr_installed=0;fdr_mode=FDR_OFF;}
            fdr_stage="cas";return 0;
        }
        fdr_done|=1u<<i;
    }
    fdr_stage="armed";return 1;
}

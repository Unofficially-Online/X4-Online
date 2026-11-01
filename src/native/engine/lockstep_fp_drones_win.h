/* FR30 on the Windows X4 9.00 executable (lockstep_fp_redirect.h fdr_*, lockstep_drone_factors.h): the four drone
 * factor sites in 0x4f1eb0, their stubs on fr_page (FR_DR_STUB, written by fr_write_code) and the install. Included
 * by lockstep_fp_redirect_win.h after the FR29b part; armed after native_gate_fp_redirect (its page).
 * Each site's first five bytes become jmp rel32 to its stub (one aligned 16-byte block each: 0x4f2100, 0x4f24c1,
 * 0x4f252b, 0x4f2778); no branch lands inside the bytes a jmp covers or skips (checked over the function: its
 * branches are all direct, and no e8/e9 of the image lands there). The flags are dead at every back address: 0x4f2108
 * comiss; 0x4f24c6 mulss, mulss, jmp 0x4f24e4 comiss; 0x4f2530 mulss, mulss, movss, comiss; 0x4f2784 comiss.
 * Pins (through the original-byte view): the drone function's 0xd5d bytes (FR29b's pin), each site's bytes raw, the
 * two constants its native ops read (1/pi 0x2cbe394, 0.3/750 0x2cbe0b8). The view shows the original bytes over our
 * jmps, so FR29b's pins keep passing in either arming order. */
#define FDR_W_FN 0x4f1eb0u
#define FDR_W_FN_SIZE 0xd5du
#define FDR_W_FN_SHA "cc927ad3b0f2b260dbf012aeaadbfc5e65e6d9938c2ff543f46f091c017d4ec2"
static uint32_t fdr_done; /* bit i: site i is ours */
_Static_assert(FR_CODE<=0x1000u,"the fp redirect page holds every stub");
__asm__(".text\n.p2align 4\n.def fdr_common_w; .scl 3; .type 32; .endef\n.seh_proc fdr_common_w\nfdr_common_w:\n"
        " pushq %rbp\n .seh_pushreg %rbp\n movq %rsp,%rbp\n .seh_setframe %rbp,0\n .seh_endprologue\n"
        FDR_ASM_SAVE
        " movl 0x70(%rsp),%ecx\n movq %rsp,%rdx\n callq *fdr_hook_calls(%rip)\n"
        FDR_ASM_RESTORE
        ".seh_endproc\n");
extern char fdr_common_w[];
static unsigned char *fdr_stub(unsigned i) {return fr_page+FR_DR_STUB+FDR_STUB_SIZE*i;}
static void fdr_write_code(unsigned char *p) {
    for(unsigned i=0;i<FDR_SITES;i++) fdr_stub_write(p+FR_DR_STUB+FDR_STUB_SIZE*i,i,(void*)fdr_common_w,(void*)(image+fdr_sites[i].back));
}
static unsigned fdr_len(unsigned i) {return fdr_sites[i].back-fdr_sites[i].site;}
static int fdr_reach(unsigned i) {
    const intptr_t d=(intptr_t)fdr_stub(i)-(intptr_t)(image+fdr_sites[i].site+5);
    return d>INT32_MIN && d<INT32_MAX;
}
static void fdr_jump(unsigned i,unsigned char out[5]) {out[0]=0xe9;const int32_t d=fr_rel(fdr_sites[i].site,fdr_stub(i));memcpy(out+1,&d,4);}
/* 0 original, 1 our jmp (the rest of the site as it was), -1 anything else (raw bytes). */
static int fdr_state(unsigned i) {
    unsigned char now[24],ours[5];const unsigned n=fdr_len(i);memcpy(now,(const void*)(image+fdr_sites[i].site),n);
    if(!memcmp(now,fdr_sites[i].orig,n)) return 0;
    if(fr_page && fdr_reach(i) && !memcmp(now+5,fdr_sites[i].orig+5,n-5)) {fdr_jump(i,ours);if(!memcmp(now,ours,5)) return 1;}
    return -1;
}
/* No other module's patch over the site: the raw bytes are what the view shows. */
static int fdr_clear(unsigned i) {
    unsigned char viewed[24];const unsigned n=fdr_len(i);
    return read_local(image+fdr_sites[i].site,viewed,n) && !memcmp((const void*)(image+fdr_sites[i].site),viewed,n);
}
static int fdr_pins_ok(void) {
    static unsigned char body[FDR_W_FN_SIZE];
    if(!read_local(image+FDR_W_FN,body,FDR_W_FN_SIZE) || !hash_ok(body,FDR_W_FN_SIZE,FDR_W_FN_SHA)) return 0;
    for(unsigned i=0;i<FDR_SITES;i++) {
        const fdr_site *s=&fdr_sites[i];
        if(s->site<FDR_W_FN || s->back>FDR_W_FN+FDR_W_FN_SIZE || memcmp(body+(s->site-FDR_W_FN),s->orig,fdr_len(i))) return 0;
        for(unsigned k=0;k<2;k++) {
            if(!s->rip[k]) continue;
            int32_t d;uint32_t v;memcpy(&d,s->orig+s->rip[k],4);
            if(s->site+s->rip[k]+4u+(uint32_t)d!=s->konst_at[k] || !read_local(image+s->konst_at[k],&v,4) || v!=s->konst[k]) return 0;
        }
    }
    return 1;
}
/* The original-byte view (fr_len_overlaps / fr_len_unpatch_copy) over our jmps. */
static int fdr_overlaps(const uintptr_t a,const uintptr_t b) {
    if(!fdr_done) return 0;
    for(unsigned i=0;i<FDR_SITES;i++) {
        const uintptr_t s=(uintptr_t)(image+fdr_sites[i].site);
        if((fdr_done>>i)&1u && s<b && s+5>a) return 1;
    }
    return 0;
}
static void fdr_unpatch_copy(const uintptr_t a,unsigned char *copy,size_t bytes) {
    if(!fdr_done) return;
    for(unsigned i=0;i<FDR_SITES;i++) {
        if(!((fdr_done>>i)&1u)) continue;
        const uintptr_t s=(uintptr_t)(image+fdr_sites[i].site);
        for(unsigned k=0;k<5;k++) if(s+k>=a && s+k<a+bytes) copy[s+k-a]=(unsigned char)fdr_sites[i].orig[k];
    }
}
static void fdr_restore(void) {
    if(!fdr_installed) return;
    for(unsigned i=0;i<FDR_SITES;i++)
        if((fdr_done>>i)&1u && fdr_state(i)==1) {
            unsigned char jump[5];fdr_jump(i,jump);
            if(view_cas_bytes(fdr_sites[i].site,jump,(const unsigned char*)fdr_sites[i].orig,5)) fdr_done&=~(1u<<i);
        }
    if(!fdr_done) {fdr_installed=0;fdr_mode=FDR_OFF;fdr_stage="restored";}
}
static int fdr_owned(void) {
    for(unsigned i=0;i<FDR_SITES;i++) if((fdr_done>>i)&1u && fdr_state(i)!=1) return 0;
    return 1;
}
/* gate.fp_redirect_drones(mode): after fp_redirect (UI thread, before the first admitted frame); a rerun with the same
 * mode is accepted. */
static int fdr_arm(int mode) {
    if(fdr_installed) return fdr_mode==mode;
    if(!fdr_arm_ready(mode)) return 0;
    if(!fr_page) {fdr_stage="page";return 0;}
    if(!fdr_pins_ok()) {fdr_stage="pins";return 0;}
    for(unsigned i=0;i<FDR_SITES;i++) {
        if(fdr_state(i)!=0 || !fdr_clear(i) || ((fdr_sites[i].site+4u)&~15u)!=(fdr_sites[i].site&~15u)) {fdr_stage="pins";return 0;}
        if(!fdr_reach(i)) {fdr_stage="page";return 0;}
    }
    fdr_mode=mode;fdr_installed=1;fdr_done=0;
    for(unsigned i=0;i<FDR_SITES;i++) {
        unsigned char jump[5];fdr_jump(i,jump);
        if(!view_cas_bytes(fdr_sites[i].site,(const unsigned char*)fdr_sites[i].orig,jump,5)) {
            fdr_restore();if(!fdr_done) {fdr_installed=0;fdr_mode=FDR_OFF;}
            fdr_stage="cas";return 0;
        }
        fdr_done|=1u<<i;
    }
    fdr_stage="armed";return 1;
}

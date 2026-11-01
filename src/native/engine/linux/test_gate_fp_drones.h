/* FR30 (gate_fp_drones.h) on the executable's bytes: the four sites of the drone path 0x1ded490 (their bytes in one
 * aligned line each, the inputs where the table says, the constants), then the windows run on their own bytes
 * natively (Linux's order: the tick-546 roll gives 0x3f5b5eeb), through the armed sites in 'shadow' (every register
 * and the frame equal to native) and in 'redirect' (the factor ours, Windows' order: 0x3f5b5eea; everything else equal
 * to native), ownership, the exact restoration. Included by test_gate_linux.c. */
/* tldr_run(block): rsp = block qword 1 (a frame of ours), xmm0-15 from bytes 0x20.., rax..r15 but rsp from 0x120..
 * (rax rcx rdx rbx rsi rdi r8..r15 rbp), jmp to qword 0; a jump planted at the window's end leads to tldr_back:
 * xmm0-15 to 0x1a0.., the registers to 0x2a0.., rsp's change at 0x318. */
static volatile uint64_t tldr_block __attribute__((used)),tldr_saved __attribute__((used)),tldr_rax __attribute__((used)),tldr_entry __attribute__((used));
__asm__(".text\n.p2align 4\ntldr_run:\n"
" push %rbp\n push %rbx\n push %r12\n push %r13\n push %r14\n push %r15\n sub $8,%rsp\n mov %rsp,tldr_saved(%rip)\n"
" mov %rdi,tldr_block(%rip)\n mov (%rdi),%rax\n mov %rax,tldr_entry(%rip)\n mov %rdi,%rax\n mov 0x08(%rax),%rsp\n"
" movdqu 0x20(%rax),%xmm0\n movdqu 0x30(%rax),%xmm1\n movdqu 0x40(%rax),%xmm2\n movdqu 0x50(%rax),%xmm3\n movdqu 0x60(%rax),%xmm4\n"
" movdqu 0x70(%rax),%xmm5\n movdqu 0x80(%rax),%xmm6\n movdqu 0x90(%rax),%xmm7\n movdqu 0xa0(%rax),%xmm8\n movdqu 0xb0(%rax),%xmm9\n"
" movdqu 0xc0(%rax),%xmm10\n movdqu 0xd0(%rax),%xmm11\n movdqu 0xe0(%rax),%xmm12\n movdqu 0xf0(%rax),%xmm13\n movdqu 0x100(%rax),%xmm14\n"
" movdqu 0x110(%rax),%xmm15\n"
" mov 0x128(%rax),%rcx\n mov 0x130(%rax),%rdx\n mov 0x138(%rax),%rbx\n mov 0x140(%rax),%rsi\n mov 0x148(%rax),%rdi\n"
" mov 0x150(%rax),%r8\n mov 0x158(%rax),%r9\n mov 0x160(%rax),%r10\n mov 0x168(%rax),%r11\n mov 0x170(%rax),%r12\n mov 0x178(%rax),%r13\n"
" mov 0x180(%rax),%r14\n mov 0x188(%rax),%r15\n mov 0x190(%rax),%rbp\n mov 0x120(%rax),%rax\n"
" jmp *tldr_entry(%rip)\n"
"tldr_back:\n mov %rax,tldr_rax(%rip)\n mov tldr_block(%rip),%rax\n"
" movdqu %xmm0,0x1a0(%rax)\n movdqu %xmm1,0x1b0(%rax)\n movdqu %xmm2,0x1c0(%rax)\n movdqu %xmm3,0x1d0(%rax)\n movdqu %xmm4,0x1e0(%rax)\n"
" movdqu %xmm5,0x1f0(%rax)\n movdqu %xmm6,0x200(%rax)\n movdqu %xmm7,0x210(%rax)\n movdqu %xmm8,0x220(%rax)\n movdqu %xmm9,0x230(%rax)\n"
" movdqu %xmm10,0x240(%rax)\n movdqu %xmm11,0x250(%rax)\n movdqu %xmm12,0x260(%rax)\n movdqu %xmm13,0x270(%rax)\n movdqu %xmm14,0x280(%rax)\n"
" movdqu %xmm15,0x290(%rax)\n"
" mov %rcx,0x2a8(%rax)\n mov %rdx,0x2b0(%rax)\n mov %rbx,0x2b8(%rax)\n mov %rsi,0x2c0(%rax)\n mov %rdi,0x2c8(%rax)\n mov %r8,0x2d0(%rax)\n"
" mov %r9,0x2d8(%rax)\n mov %r10,0x2e0(%rax)\n mov %r11,0x2e8(%rax)\n mov %r12,0x2f0(%rax)\n mov %r13,0x2f8(%rax)\n mov %r14,0x300(%rax)\n"
" mov %r15,0x308(%rax)\n mov %rbp,0x310(%rax)\n mov tldr_rax(%rip),%rcx\n mov %rcx,0x2a0(%rax)\n"
" mov %rsp,%rcx\n sub 0x08(%rax),%rcx\n mov %rcx,0x318(%rax)\n"
" mov tldr_saved(%rip),%rsp\n add $8,%rsp\n pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbx\n pop %rbp\n ret\n");
extern void tldr_run(uint64_t *block);
extern char tldr_back[];
typedef struct {uint64_t q[0x320/8];} tldr_block_t;
/* The function's frame for a window (its rsp-relative slots), with room below for the stub and the hook. */
static unsigned char tldr_stack[0x10000] __attribute__((aligned(16)));
#define TLDR_RSP (tldr_stack+0xf000)
static float tldr_f(uint32_t b) {float f;memcpy(&f,&b,4);return f;}
static uint32_t tldr_b(float f) {uint32_t b;memcpy(&b,&f,4);return b;}
static void tldr_xmm(tldr_block_t *b,unsigned k,uint32_t lane0) {memcpy((unsigned char*)b+0x20+0x10*k,&lane0,4);}
static float tldr_in(const tldr_block_t *b,unsigned k) {float f;memcpy(&f,(const unsigned char*)b+0x20+0x10*k,4);return f;}
static uint32_t tldr_out(const tldr_block_t *b,unsigned k) {uint32_t v;memcpy(&v,(const unsigned char*)b+0x1a0+0x10*k,4);return v;}
static float tldr_slot(unsigned off) {float f;memcpy(&f,TLDR_RSP+off,4);return f;}
static void tldr_set(unsigned off,float f) {memcpy(TLDR_RSP+off,&f,4);}
/* The windows: entered where the site's inputs are set up, run to a planted jump. */
static const struct {uint32_t entry,end;} tldr_windows[FDR_SITES]={{0x1dedf6au,0x1dedfa2u},{0x1dee223u,0x1dee240u},{0x1dedaf0u,0x1dedb19u},{0x1dee2bdu,0x1dee2d9u}};
static uint32_t tldr_seed;
static float tldr_range(float a,float b) {tldr_seed=tldr_seed*1664525u+1013904223u;return a+(b-a)*((float)(tldr_seed>>8)*(1.0f/16777216.0f));}
/* Case i of window w (the frame's slots too: kept in qwords 2..5 for the expectations). */
static void tldr_case(unsigned w,unsigned i,tldr_block_t *b) {
    memset(b,0,sizeof(*b));memset(tldr_stack+0xe000,0x77,0x2000);
    for(unsigned k=0;k<16;k++) for(unsigned j=0;j<4;j++) {const uint32_t v=0x61000000u+(k<<8)+j;memcpy((unsigned char*)b+0x20+0x10*k+4*j,&v,4);}
    for(unsigned r=0;r<15;r++) b->q[0x120/8+r]=0x5100510051000000ull+r;
    b->q[0]=(uint64_t)(uintptr_t)LX(tldr_windows[w].entry);b->q[1]=(uint64_t)(uintptr_t)TLDR_RSP;
    const float f1=i%3?tldr_range(0.5f,1.0f):1.0f,f31=tldr_range(0.05f,1.0f);
    if(w==FDR_SPREAD) {
        const float m=tldr_range(1.0f,3000.0f),r=tldr_range(0.0f,m),p15=tldr_range(1.0f,20.0f);
        tldr_xmm(b,0,tldr_b(r));tldr_set(4,m);tldr_set(0,p15);b->q[2]=tldr_b(m);b->q[3]=tldr_b(p15);
    } else if(w==FDR_TIME) {
        tldr_xmm(b,6,0x42c80000u);tldr_xmm(b,1,tldr_b(tldr_range(0.0f,100.0f)));tldr_set(0x48,f1);tldr_set(0x20,f31);b->q[2]=tldr_b(f1);b->q[3]=tldr_b(f31);
    } else if(w==FDR_ROLL) {
        float q=tldr_range(0.0f,99.0f),r=tldr_range(0.0f,q),g1=f1,g31=f31;
        if(i==0) {r=tldr_f(0x402c128fu);q=tldr_f(0x40dc54bcu);g1=1.0f;g31=tldr_f(0x3f666666u);} /* tick 546 */
        tldr_xmm(b,0,tldr_b(r));tldr_xmm(b,1,tldr_b(q));tldr_xmm(b,6,0x42c80000u);tldr_set(0x48,g1);tldr_set(0x20,g31);b->q[2]=tldr_b(g1);b->q[3]=tldr_b(g31);
    } else {
        const float p16=tldr_range(250.001f,999.999f);
        tldr_xmm(b,7,tldr_b(p16));tldr_xmm(b,3,0x437a0000u);tldr_xmm(b,1,0);tldr_set(0x4c,p16);b->q[2]=tldr_b(p16);
    }
}
static unsigned char tldr_end_original[FDR_SITES][14];
static void tldr_plant(int on) {
    assert(!mprotect((void*)LX(0x1ded000u),0x2000,PROT_READ|PROT_WRITE|PROT_EXEC)); /* a site's swap leaves its page r-x */
    for(unsigned w=0;w<FDR_SITES;w++) {
        unsigned char *at=LX(tldr_windows[w].end);
        if(on) {memcpy(tldr_end_original[w],at,14);unsigned char j[14]={0xff,0x25,0,0,0,0};void *to=(void*)tldr_back;memcpy(j+6,&to,8);memcpy(at,j,14);}
        else memcpy(at,tldr_end_original[w],14);
    }
    __builtin___clear_cache((char*)LX(0x1ded000u),(char*)LX(0x1def000u));
}
/* The result and the frame slot it lands in. */
static uint32_t tldr_result(unsigned w,const tldr_block_t *b) {
    if(w==FDR_SPREAD) return tldr_out(b,7);
    if(w==FDR_MISS) return tldr_out(b,1);
    return tldr_b(tldr_slot(0x20));
}
static void tldr_go(unsigned w,tldr_block_t *b,uint32_t *result) {tldr_plant(1);tldr_run(b->q);tldr_plant(0);*result=tldr_result(w,b);}
/* Linux's order and ours (Windows') of each window from its inputs. */
static uint32_t tldr_expect(unsigned w,const tldr_block_t *b,int windows) {
    const float g1=tldr_f((uint32_t)b->q[2]),g31=tldr_f((uint32_t)b->q[3]);
    if(w==FDR_SPREAD) {
        const float s=((tldr_f((uint32_t)b->q[2])+tldr_in(b,0))*0.5f)*tldr_f((uint32_t)b->q[3]);
        return tldr_b(windows?fdr_spread(s):s/tldr_f(0x40490fdbu));
    }
    if(w==FDR_MISS) {const float p16=tldr_f((uint32_t)b->q[2]);return tldr_b(windows?fdr_miss(p16):0.0f+(tldr_f(0x3e99999au)*(p16-250.0f))/750.0f);}
    const float p=w==FDR_TIME?tldr_in(b,1):(tldr_in(b,1)+tldr_in(b,0))*0.5f;
    const float q=windows?fdr_keep(p):(100.0f-p)/100.0f;
    return tldr_b((q*g1)*g31);
}
/* The site's own result (the factor before the rest of the window): ours vs Linux's differ here, counted by fdr_seen. */
static int tldr_site_split(unsigned w,const tldr_block_t *b) {
    if(w==FDR_SPREAD || w==FDR_MISS) return tldr_expect(w,b,0)!=tldr_expect(w,b,1);
    const float p=w==FDR_TIME?tldr_in(b,1):(tldr_in(b,1)+tldr_in(b,0))*0.5f;
    return tldr_b(fdr_keep(p))!=tldr_b((100.0f-p)/100.0f);
}
#define TLDR_CASES 600u
static tldr_block_t tldr_ref[FDR_SITES][TLDR_CASES];
static uint32_t tldr_ref_result[FDR_SITES][TLDR_CASES];
static void test_fdr_elf(void) {
    assert(fdr_selftest());
    /* the sites: their bytes, one aligned line, no FR29b rel32 in that line, the constants their native ops read */
    for(unsigned i=0;i<FDR_SITES;i++) {
        const fdr_site *s=&fdr_sites[i];const uint32_t line=s->site&~15u;
        assert(fdr_state(i)==0 && (s->site&15u)+5u<=16u && s->back-s->site>=5u);
        for(unsigned k=0;k<FA_L_SITES;k++) assert(((fa_l_sites[k].site+1u)&~15u)!=line);
    }
    assert(fdr_pins_ok());
    /* the inputs where the table says: x in xmm1 and 100 in xmm6 (0x1dedae4..0x1dedaff), 100 and the time loss
     * (0x1dee21d..0x1dee226), s in xmm7 (0x1dedf95: mulss xmm7,[rsp]), p16 stored at [rsp+0x4c] (0x1ded501) and loaded
     * into xmm7 (0x1dee2a5), 250 subtracted (0x1dee2bd), xmm1 zeroed (0x1dee2ab) */
    assert(!memcmp(LX(0x1dedae4u),"\xf3\x0f\x10\x4c\x24\x2c\xf3\x0f\x10\x74\x24\x70\xf3\x0f\x58\xc8",16) && !memcmp(LX(0x1dedafcu),"\x0f\x28\xc6\xf3\x0f\x5c\xc1",7));
    assert(!memcmp(LX(0x1dee21du),"\xf3\x0f\x10\x74\x24\x74\x0f\x28\xc6\xf3\x0f\x5c\xc1",13) && !memcmp(LX(0x1dedf95u),"\xf3\x0f\x59\x3c\x24",5));
    assert(!memcmp(LX(0x1ded501u),"\xf3\x0f\x11\x54\x24\x4c",6) && !memcmp(LX(0x1dee2a5u),"\xf3\x0f\x10\x7c\x24\x4c",6));
    assert(!memcmp(LX(0x1dee2bdu),"\xf3\x0f\x5c\xfb",4) && !memcmp(LX(0x1dee2abu),"\x66\x0f\xef\xc9",4) && !memcmp(LX(0x1dee2d9u),"\x0f\x2f\xc1",3));
    assert(!memcmp(LX(0x1dedfa2u),"\x0f\x2f\xc7",3) && !memcmp(LX(0x1dedb0du),"\xf3\x0f\x59\x44\x24\x20\xf3\x0f\x11\x44\x24\x20",12));
    assert(!memcmp(LX(0x1dee234u),"\xf3\x0f\x59\x44\x24\x20\xf3\x0f\x11\x44\x24\x20\xe9",13));
    assert(!mprotect((void*)LX(0x1ded000u),0x2000,PROT_READ|PROT_WRITE|PROT_EXEC));
    unsigned char saved[FDR_SITES][16];
    for(unsigned i=0;i<FDR_SITES;i++) memcpy(saved[i],LX(fdr_sites[i].site&~15u),16);
    /* native: Linux's order (the tick-546 roll: Linux's live factor) */
    tldr_seed=0x30303030u;
    for(unsigned w=0;w<FDR_SITES;w++) for(unsigned i=0;i<TLDR_CASES;i++) {
        tldr_case(w,i,&tldr_ref[w][i]);tldr_go(w,&tldr_ref[w][i],&tldr_ref_result[w][i]);
        if(tldr_ref_result[w][i]!=tldr_expect(w,&tldr_ref[w][i],0)) {printf("FR30 L window %u case %u: native %08x Linux order %08x\n",w,i,tldr_ref_result[w][i],tldr_expect(w,&tldr_ref[w][i],0));assert(0);}
    }
    assert(tldr_ref_result[FDR_ROLL][0]==0x3f5b5eebu && tldr_ref[FDR_ROLL][0].q[0x318/8]==0);
    unsigned split[FDR_SITES]={0},site_split[FDR_SITES]={0};
    for(unsigned w=0;w<FDR_SITES;w++) for(unsigned i=0;i<TLDR_CASES;i++) {
        split[w]+=tldr_ref_result[w][i]!=tldr_expect(w,&tldr_ref[w][i],1);site_split[w]+=tldr_site_split(w,&tldr_ref[w][i]);
    }
    for(unsigned w=0;w<FDR_SITES;w++) assert(split[w]>TLDR_CASES/20);
    /* armed */
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    thunk=make_thunk();assert(thunk);fr_installed=1;
    for(unsigned i=0;i<FDR_SITES;i++) {
        unsigned char want[FDR_STUB_SIZE];fdr_stub_write(want,i,(void*)fdr_common_l,(void*)LX(fdr_sites[i].back));
        assert(!memcmp(fdr_stub(i),want,FDR_STUB_SIZE));
    }
    for(int mode=FDR_SHADOW;mode<=FDR_REDIRECT;mode++) {
        assert(!fdr_arm(7) && !strcmp(fdr_stage,"mode"));
        assert(fdr_arm(mode) && fdr_installed && !strcmp(fdr_stage,"armed") && fdr_arm(mode) && !fdr_arm(mode==FDR_SHADOW?FDR_REDIRECT:FDR_SHADOW));
        for(unsigned i=0;i<FDR_SITES;i++) {
            const unsigned char *line=LX(fdr_sites[i].site&~15u);const unsigned at=fdr_sites[i].site&15u;int32_t d;memcpy(&d,LX(fdr_sites[i].site)+1,4);
            assert(fdr_state(i)==1 && LX(fdr_sites[i].site)+5+d==fdr_stub(i) && !memcmp(line,saved[i],at) && !memcmp(line+at+5,saved[i]+at+5,11-at));
        }
        assert(fdr_owned() && fr_owned());
        tldr_seed=0x30303030u;
        for(unsigned w=0;w<FDR_SITES;w++) for(unsigned i=0;i<TLDR_CASES;i++) {
            static tldr_block_t b;uint32_t r;tldr_case(w,i,&b);tldr_go(w,&b,&r);
            const uint32_t want=mode==FDR_SHADOW?tldr_ref_result[w][i]:tldr_expect(w,&b,1);
            /* every register as native, but the result's (and in 'redirect' the time/roll result register xmm0, which holds it) */
            tldr_block_t x=b,y=tldr_ref[w][i];
            if(mode==FDR_REDIRECT) {
                const unsigned k=w==FDR_SPREAD?7:w==FDR_MISS?1:0;memset((unsigned char*)&x+0x1a0+0x10*k,0,4);memset((unsigned char*)&y+0x1a0+0x10*k,0,4);
            }
            if(r!=want || memcmp((unsigned char*)&x+0x1a0,(unsigned char*)&y+0x1a0,0x180)) {
                printf("FR30 L window %u case %u mode %d: result %08x want %08x\n",w,i,mode,r,want);
                for(unsigned k=0;k<0x180;k+=8) {uint64_t u,v;memcpy(&u,(unsigned char*)&x+0x1a0+k,8);memcpy(&v,(unsigned char*)&y+0x1a0+k,8);if(u!=v) printf("  +%03x %016llx vs %016llx\n",0x1a0+k,(unsigned long long)u,(unsigned long long)v);}
                assert(0);
            }
        }
        for(unsigned w=0;w<FDR_SITES;w++) {
            if(fdr_n[w]!=TLDR_CASES || fdr_diff[w]!=site_split[w]) {printf("FR30 L site %u: n %lld diff %lld, expected %u/%u (results %u)\n",w,(long long)fdr_n[w],(long long)fdr_diff[w],TLDR_CASES,site_split[w],split[w]);assert(0);}
            assert(site_split[w]>=split[w]);
        }
        assert(fdr_ours==(mode==FDR_REDIRECT?FDR_SITES*TLDR_CASES:0) && fdr_max_ulp>=1 && fdr_off==0);
        char out[2048];assert(fr_status(out,sizeof(out))>0 && strstr(out,mode==FDR_REDIRECT?" drones=redirect dr_stage=armed":" drones=shadow dr_stage=armed"));
        /* ownership: a site's jmp changed under us */
        unsigned char *line=LX(fdr_sites[1].site&~15u);unsigned __int128 l0,l1;memcpy(&l0,line,16);
        l1=l0^((unsigned __int128)1<<(8*((fdr_sites[1].site&15u)+2u)));
        assert(gate_code_swap128(line,l0,l1) && !fdr_owned() && !fr_owned() && gate_code_swap128(line,l1,l0) && fdr_owned());
        if(mode==FDR_SHADOW) {fdr_restore();assert(!fdr_installed && !fdr_done && !strcmp(fdr_stage,"restored"));}
    }
    /* the tick-546 roll through the redirect: Windows' live factor from Linux's own bytes */
    {static tldr_block_t b;uint32_t r;tldr_seed=0x30303030u;tldr_case(FDR_ROLL,0,&b);tldr_go(FDR_ROLL,&b,&r);assert(r==0x3f5b5eeau);}
    lxf_done=0;lxf_sp_entry_done=0;fsp_installed=0;fa_installed=0;fa_done=0;
    fr_restore();assert(!fdr_installed && !fdr_done && !fr_installed);
    for(unsigned i=0;i<FDR_SITES;i++) assert(fdr_state(i)==0 && !memcmp(LX(fdr_sites[i].site&~15u),saved[i],16));
    fr_restored=0;fr_stage="idle";fr_mode=FR_OFF;fdr_stage="idle";
    for(unsigned i=0;i<FDR_SITES;i++) fdr_n[i]=fdr_diff[i]=0;
    fdr_ours=fdr_max_ulp=fdr_off=0;
    assert(!mprotect((void*)LX(0x1ded000u),0x2000,PROT_READ|PROT_EXEC));
    printf("fp redirect FR30 on the executable: the drone path 0x1ded490's four sites (one line each, inputs and constants as read), "
           "its own bytes natively = Linux's order (%u/%u/%u/%u of %u cases per site off Windows' order; tick 546's roll 0x3f5b5eeb), "
           "through the armed sites 'shadow' = native in every register and 'redirect' = Windows' order (0x3f5b5eea) with every other "
           "register native, ownership, restored exactly PASS\n",split[0],split[1],split[2],split[3],TLDR_CASES);
}

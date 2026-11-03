/* FR30 (lockstep_drone_factors.h, lockstep_fp_redirect.h fdr_*, lockstep_fp_drones_win.h): the factors' known answers
 * against Linux's order, the stub bytes, fdr_seen on a frame, the status, the arm refusals; then on the executable's
 * own bytes the four windows of the drone path 0x4f1eb0 run natively and through the armed sites ('shadow' and
 * 'redirect'): every register equal to native (Windows' order is ours), the counters, the view, ownership, the
 * restoration. Included by test_lockstep_fp_redirect.h. */
static float tfdr_f(uint32_t b) {float f;memcpy(&f,&b,4);return f;}
static uint32_t tfdr_b(float f) {uint32_t b;memcpy(&b,&f,4);return b;}
/* Linux's order of the same four (0x1dedf9a, 0x1dee22a, 0x1dedb03, 0x1dee2c1), for the known answers' contrast. */
static uint32_t tfdr_linux(unsigned kind,uint32_t in) {
    volatile uint32_t v=in;const float x=tfdr_f(v);
    if(kind==FDR_SPREAD) return tfdr_b(x/tfdr_f(0x40490fdbu));
    if(kind==FDR_MISS) return tfdr_b(0.0f+(tfdr_f(0x3e99999au)*(x-250.0f))/750.0f);
    return tfdr_b((100.0f-x)/100.0f);
}
static void test_fdr_synthetic(void) {
    assert(fdr_selftest());
    static const struct {unsigned kind;uint32_t in,w,l;} kat[]={
        {FDR_ROLL,0x40992f02u,0x3f73beccu,0x3f73becdu},{FDR_TIME,0x42053333u,0x3f2ac082u,0x3f2ac083u},{FDR_SPREAD,0x40400000u,0x3f747644u,0x3f747645u},
        {FDR_MISS,0x43c80000u,0x3d75c290u,0x3d75c28fu},{FDR_MISS,0x44160000u,0x3e0f5c29u,0x3e0f5c2au}};
    for(unsigned i=0;i<sizeof(kat)/sizeof(kat[0]);i++) {
        assert(tfdr_b(fdr_value(kat[i].kind,tfdr_f(kat[i].in)))==kat[i].w && tfdr_linux(kat[i].kind,kat[i].in)==kat[i].l);
    }
    assert(fdr_miss(250.0f)==0.0f && fdr_miss(1000.0f)==tfdr_f(0x3e99999au) && fdr_miss(tfdr_f(0x7fc00000u))==0.0f && fdr_keep(100.0f)==0.0f);
    /* the stubs: head, tail, jmp back, the native op with its constants' copies, the store of its result, the pointers */
    static const unsigned char head[0x15]={0x48,0x8d,0x64,0x24,0x80,0x6a,0x00,0xff,0x15,0x4b,0,0,0,0x48,0x8d,0xa4,0x24,0x88,0,0,0};
    static const unsigned char store[FDR_SITES][7]={{0xf3,0x41,0x0f,0x11,0x4b,0x74,0xc3},{0xf3,0x41,0x0f,0x11,0x43,0x74,0xc3},
                                                    {0xf3,0x41,0x0f,0x11,0x4b,0x74,0xc3},{0xf3,0x41,0x0f,0x11,0x4b,0x74,0xc3}};
    void *const common=(void*)(uintptr_t)0x1122334455667788ull,*const back=(void*)(uintptr_t)0x0123456789abcdefull;
    for(unsigned i=0;i<FDR_SITES;i++) {
        const fdr_site *s=&fdr_sites[i];const unsigned len=s->back-s->site,tail=len-s->op_len;
        unsigned char st[FDR_STUB_SIZE];fdr_stub_write(st,i,common,back);
        assert(!memcmp(st,head,6) && st[6]==i && !memcmp(st+7,head+7,sizeof(head)-7) && tail==0); /* Windows: no tail */
        int32_t d;memcpy(&d,st+0x17,4);assert(st[0x15]==0xff && st[0x16]==0x25 && 0x1b+d==0x60);
        unsigned char op[24];memcpy(op,s->orig,s->op_len);
        for(unsigned k=0;k<2;k++) if(s->rip[k]) {
            memcpy(&d,st+FDR_MINI+s->rip[k],4);assert((int)(FDR_MINI+s->rip[k]+4)+d==(int)(0x4c+4*k));
            uint32_t c;memcpy(&c,st+0x4c+4*k,4);assert(c==s->konst[k]);memcpy(op+s->rip[k],st+FDR_MINI+s->rip[k],4);
        }
        assert(!memcmp(st+FDR_MINI,op,s->op_len) && !memcmp(st+FDR_MINI+s->op_len,store[i],7));
        void *p;memcpy(&p,st+0x58,8);assert(p==common);memcpy(&p,st+0x60,8);assert(p==back);
        assert(FDR_MINI+s->op_len+7<=0x4c && s->site>=FDR_W_FN && s->back<=FDR_W_FN+FDR_W_FN_SIZE && ((s->site+4u)&~15u)==(s->site&~15u));
    }
    /* fdr_seen on a frame: the roll site (x in xmm0, native in the frame, the result to xmm1's lane 0) */
    static fdr_frame f;memset(&f,0x5a,sizeof(f));
    const uint32_t x=0x40992f02u,u=tfdr_b(100.0f-tfdr_f(x)),l=0x3f73becdu;
    memcpy(f.xmm[0],&x,4);memcpy(f.xmm[1],&u,4);f.native=l;
    unsigned char kept[sizeof(f)];
    for(unsigned i=0;i<FDR_SITES;i++) fdr_n[i]=fdr_diff[i]=0;
    fdr_ours=fdr_max_ulp=fdr_off=0;
    const uint32_t saved_active=stats.active;stats.active=1;
    fdr_mode=FDR_SHADOW;memcpy(kept,&f,sizeof(f));fdr_seen(FDR_ROLL,&f);
    uint32_t got;memcpy(&got,f.xmm[1],4);assert(got==l && !memcmp(f.xmm[1]+4,kept+offsetof(fdr_frame,xmm)+0x10+4,12));
    memcpy((unsigned char*)&f+offsetof(fdr_frame,xmm)+0x10,&u,4);assert(!memcmp(&f,kept,sizeof(f))); /* nothing else written */
    fdr_mode=FDR_REDIRECT;fdr_seen(FDR_ROLL,&f);memcpy(&got,f.xmm[1],4);assert(got==0x3f73beccu);
    f.native=0x3f73beccu;fdr_seen(FDR_ROLL,&f);
    stats.active=0;f.native=l;memcpy(f.xmm[1],&u,4);fdr_seen(FDR_ROLL,&f);memcpy(&got,f.xmm[1],4);assert(got==l); /* the gate stopped: native */
    stats.active=1;
    /* the threshold reads its p16 from xmm1 here; the spread from xmm1; the time loss from xmm6 */
    const uint32_t p16=0x43c80000u,s3=0x40400000u,pct=0x42053333u;
    memcpy(f.xmm[1],&p16,4);f.native=0x3d75c28fu;fdr_seen(FDR_MISS,&f);memcpy(&got,f.xmm[1],4);assert(got==0x3d75c290u);
    memcpy(f.xmm[1],&s3,4);f.native=0x3f747645u;fdr_seen(FDR_SPREAD,&f);memcpy(&got,f.xmm[1],4);assert(got==0x3f747644u);
    memcpy(f.xmm[6],&pct,4);f.native=0x3f2ac082u;fdr_seen(FDR_TIME,&f);memcpy(&got,f.xmm[0],4);assert(got==0x3f2ac082u);
    assert(fdr_n[FDR_ROLL]==4 && fdr_diff[FDR_ROLL]==3 && fdr_n[FDR_MISS]==1 && fdr_diff[FDR_MISS]==1 && fdr_n[FDR_SPREAD]==1 && fdr_diff[FDR_SPREAD]==1);
    assert(fdr_n[FDR_TIME]==1 && fdr_diff[FDR_TIME]==0 && fdr_max_ulp==1 && fdr_ours==5 && fdr_off==1);
    char out[2048];fdr_stage="armed";
    assert(fr_status(out,sizeof(out))>0 && strstr(out," drones=redirect dr_stage=armed dr=1:5:7 dr_sites=spread:1/1,time:0/1,roll:3/4,miss:1/1 dr_ours=5 dr_off=1"));
    /* arm refusals: the mode, fp_redirect not armed, no page */
    const DWORD saved_ui=ui_thread;const uint64_t saved_issued=stats.issued;const int saved_boot=boot_mode,saved_phase=boot_phase,saved_fr=fr_installed;
    unsigned char *saved_page=fr_page;fr_page=NULL;
    ui_thread=GetCurrentThreadId();stats.issued=0;boot_mode=0;boot_phase=0;fr_installed=0;fdr_mode=FDR_OFF;
    assert(!fdr_arm(7) && !strcmp(fdr_stage,"mode") && !fdr_arm(FDR_REDIRECT) && !strcmp(fdr_stage,"refused"));
    fr_installed=1;stats.issued=1;assert(!fdr_arm(FDR_REDIRECT) && !strcmp(fdr_stage,"refused"));stats.issued=0;
    assert(!fdr_arm(FDR_REDIRECT) && !strcmp(fdr_stage,"page") && !fdr_installed && !fdr_done);
    fr_installed=saved_fr;fr_page=saved_page;ui_thread=saved_ui;stats.issued=saved_issued;boot_mode=saved_boot;boot_phase=saved_phase;
    fdr_mode=FDR_OFF;fdr_stage="idle";stats.active=saved_active;
    for(unsigned i=0;i<FDR_SITES;i++) fdr_n[i]=fdr_diff[i]=0;
    fdr_ours=fdr_max_ulp=fdr_off=0;
    printf("fp redirect FR30 drone factors: known answers (each one Linux's order misses by an ulp), stub bytes, fdr_seen shadow/redirect/stopped, status, arm refusals PASS\n");
}
/* ---- On the executable's bytes ----
 * tfdr_run(block) loads xmm0-15 (block bytes 0x20..0x11f), rax..r15 but rsp (qwords 0x24..0x32: rax rcx rdx rbx rsi rdi
 * r8..r15; rbp from qword 1) and jumps to qword 0 with rsp 16-aligned; a jump planted at the window's end leads to
 * tfdr_back, which stores xmm0-15 at 0x120.., the general registers at qwords 0x34.. and rsp's change at qword 0x42. */
#define TFDR_Q 0x48u
static volatile uint64_t tfdr_block __attribute__((used)),tfdr_rsp __attribute__((used)),tfdr_rax __attribute__((used)),tfdr_entry __attribute__((used));
__asm__(".text\n.p2align 4\ntfdr_run:\n"
" push %rbp\n push %rbx\n push %rsi\n push %rdi\n push %r12\n push %r13\n push %r14\n push %r15\n sub $0xa8,%rsp\n"
" movdqu %xmm6,0x00(%rsp)\n movdqu %xmm7,0x10(%rsp)\n movdqu %xmm8,0x20(%rsp)\n movdqu %xmm9,0x30(%rsp)\n movdqu %xmm10,0x40(%rsp)\n"
" movdqu %xmm11,0x50(%rsp)\n movdqu %xmm12,0x60(%rsp)\n movdqu %xmm13,0x70(%rsp)\n movdqu %xmm14,0x80(%rsp)\n movdqu %xmm15,0x90(%rsp)\n"
" mov %rcx,tfdr_block(%rip)\n mov (%rcx),%rax\n mov %rax,tfdr_entry(%rip)\n sub $0x200,%rsp\n mov %rsp,tfdr_rsp(%rip)\n mov %rcx,%rax\n"
" movdqu 0x20(%rax),%xmm0\n movdqu 0x30(%rax),%xmm1\n movdqu 0x40(%rax),%xmm2\n movdqu 0x50(%rax),%xmm3\n movdqu 0x60(%rax),%xmm4\n"
" movdqu 0x70(%rax),%xmm5\n movdqu 0x80(%rax),%xmm6\n movdqu 0x90(%rax),%xmm7\n movdqu 0xa0(%rax),%xmm8\n movdqu 0xb0(%rax),%xmm9\n"
" movdqu 0xc0(%rax),%xmm10\n movdqu 0xd0(%rax),%xmm11\n movdqu 0xe0(%rax),%xmm12\n movdqu 0xf0(%rax),%xmm13\n movdqu 0x100(%rax),%xmm14\n"
" movdqu 0x110(%rax),%xmm15\n"
" mov 0x128(%rax),%rcx\n mov 0x130(%rax),%rdx\n mov 0x138(%rax),%rbx\n mov 0x140(%rax),%rsi\n mov 0x148(%rax),%rdi\n"
" mov 0x150(%rax),%r8\n mov 0x158(%rax),%r9\n mov 0x160(%rax),%r10\n mov 0x168(%rax),%r11\n mov 0x170(%rax),%r12\n mov 0x178(%rax),%r13\n"
" mov 0x180(%rax),%r14\n mov 0x188(%rax),%r15\n mov 0x08(%rax),%rbp\n mov 0x120(%rax),%rax\n"
" jmp *tfdr_entry(%rip)\n"
"tfdr_back:\n mov %rax,tfdr_rax(%rip)\n mov tfdr_block(%rip),%rax\n"
" movdqu %xmm0,0x1a0(%rax)\n movdqu %xmm1,0x1b0(%rax)\n movdqu %xmm2,0x1c0(%rax)\n movdqu %xmm3,0x1d0(%rax)\n movdqu %xmm4,0x1e0(%rax)\n"
" movdqu %xmm5,0x1f0(%rax)\n movdqu %xmm6,0x200(%rax)\n movdqu %xmm7,0x210(%rax)\n movdqu %xmm8,0x220(%rax)\n movdqu %xmm9,0x230(%rax)\n"
" movdqu %xmm10,0x240(%rax)\n movdqu %xmm11,0x250(%rax)\n movdqu %xmm12,0x260(%rax)\n movdqu %xmm13,0x270(%rax)\n movdqu %xmm14,0x280(%rax)\n"
" movdqu %xmm15,0x290(%rax)\n"
" mov %rcx,0x2a8(%rax)\n mov %rdx,0x2b0(%rax)\n mov %rbx,0x2b8(%rax)\n mov %rsi,0x2c0(%rax)\n mov %rdi,0x2c8(%rax)\n mov %r8,0x2d0(%rax)\n"
" mov %r9,0x2d8(%rax)\n mov %r10,0x2e0(%rax)\n mov %r11,0x2e8(%rax)\n mov %r12,0x2f0(%rax)\n mov %r13,0x2f8(%rax)\n mov %r14,0x300(%rax)\n"
" mov %r15,0x308(%rax)\n mov %rbp,0x310(%rax)\n mov tfdr_rax(%rip),%rcx\n mov %rcx,0x2a0(%rax)\n"
" mov %rsp,%rcx\n sub tfdr_rsp(%rip),%rcx\n mov %rcx,0x318(%rax)\n"
" mov tfdr_rsp(%rip),%rsp\n add $0x200,%rsp\n"
" movdqu 0x00(%rsp),%xmm6\n movdqu 0x10(%rsp),%xmm7\n movdqu 0x20(%rsp),%xmm8\n movdqu 0x30(%rsp),%xmm9\n movdqu 0x40(%rsp),%xmm10\n"
" movdqu 0x50(%rsp),%xmm11\n movdqu 0x60(%rsp),%xmm12\n movdqu 0x70(%rsp),%xmm13\n movdqu 0x80(%rsp),%xmm14\n movdqu 0x90(%rsp),%xmm15\n"
" add $0xa8,%rsp\n pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n");
extern void tfdr_run(uint64_t *block);
extern char tfdr_back[];
/* block layout (bytes): 0 entry, 8 rbp, 0x10 the spread's p15 (the frame's [rbp+0x270]), 0x20 xmm in, 0x120 rax rcx rdx rbx rsi rdi r8..r15 in, 0x1a0 xmm out,
 * 0x2a0 the same registers out, 0x310 rbp out, 0x318 rsp change. */
typedef struct {uint64_t q[0x320/8];} tfdr_block_t;
static void tfdr_xmm(tfdr_block_t *b,unsigned k,uint32_t lane0) {memcpy((unsigned char*)b+0x20+0x10*k,&lane0,4);}
static uint32_t tfdr_out(const tfdr_block_t *b,unsigned k) {uint32_t v;memcpy(&v,(const unsigned char*)b+0x1a0+0x10*k,4);return v;}
/* The four windows: entered at the first instruction that sets up the site's inputs, run to a planted jump. */
static const struct {uint32_t entry,end;} tfdr_windows[FDR_SITES]={{0x4f20e8u,0x4f2108u},{0x4f24b0u,0x4f24d0u},{0x4f2516u,0x4f253au},{0x4f2774u,0x4f2784u}};
static unsigned char tfdr_frame_buf[0x400];
static uint32_t tfdr_seed;
static float tfdr_range(float a,float b) {tfdr_seed=tfdr_seed*1664525u+1013904223u;return a+(b-a)*((float)(tfdr_seed>>8)*(1.0f/16777216.0f));}
/* One case of window w: every register a sentinel, then the window's inputs. */
static void tfdr_case(unsigned w,unsigned i,tfdr_block_t *b,uint32_t hundred) {
    memset(b,0,sizeof(*b));
    for(unsigned k=0;k<16;k++) for(unsigned j=0;j<4;j++) {const uint32_t v=0x61000000u+(k<<8)+j;memcpy((unsigned char*)b+0x20+0x10*k+4*j,&v,4);}
    for(unsigned r=0;r<14;r++) b->q[0x120/8+r]=0x5100510051000000ull+r;
    b->q[0]=(uint64_t)(uintptr_t)(image+tfdr_windows[w].entry);b->q[1]=(uint64_t)(uintptr_t)(tfdr_frame_buf+0x100);
    const float f1=i%3?tfdr_range(0.5f,1.0f):1.0f,f31=tfdr_range(0.05f,1.0f);
    if(w==FDR_SPREAD) {
        const float m=tfdr_range(1.0f,3000.0f),r=tfdr_range(0.0f,m),p15=tfdr_range(1.0f,20.0f);
        tfdr_xmm(b,0,tfdr_b(r));tfdr_xmm(b,6,tfdr_b(m));tfdr_xmm(b,9,0x3f000000u);memcpy(tfdr_frame_buf+0x100+0x270,&p15,4);b->q[2]=tfdr_b(p15); /* [rbp+0x270]; kept in qword 2 */
    } else if(w==FDR_TIME) {
        tfdr_xmm(b,13,hundred);tfdr_xmm(b,6,tfdr_b(tfdr_range(0.0f,100.0f)));tfdr_xmm(b,9,tfdr_b(f1));tfdr_xmm(b,10,tfdr_b(f31));
    } else if(w==FDR_ROLL) {
        const float q=tfdr_range(0.0f,99.0f),r=tfdr_range(0.0f,q);
        tfdr_xmm(b,0,tfdr_b(r));tfdr_xmm(b,11,tfdr_b(q));tfdr_xmm(b,13,hundred);tfdr_xmm(b,8,0x3c23d70au);tfdr_xmm(b,9,tfdr_b(f1));tfdr_xmm(b,10,tfdr_b(f31));
        if(i==0) {tfdr_xmm(b,0,0x402c128fu);tfdr_xmm(b,11,0x40dc54bcu);tfdr_xmm(b,9,0x3f800000u);tfdr_xmm(b,10,0x3f666666u);} /* tick 546 */
    } else {
        tfdr_xmm(b,14,tfdr_b(tfdr_range(250.001f,999.999f)));tfdr_xmm(b,2,0x437a0000u);
    }
}
static unsigned char tfdr_end_original[FDR_SITES][14];
static void tfdr_plant(int on) {
    for(unsigned w=0;w<FDR_SITES;w++) {
        unsigned char *at=image+tfdr_windows[w].end;
        if(on) {memcpy(tfdr_end_original[w],at,14);unsigned char j[14]={0xff,0x25,0,0,0,0};void *to=(void*)tfdr_back;memcpy(j+6,&to,8);memcpy(at,j,14);}
        else memcpy(at,tfdr_end_original[w],14);
    }
    FlushInstructionCache(GetCurrentProcess(),image+0x4f2000u,0x1000);
}
static void tfdr_go(tfdr_block_t *b) {tfdr_plant(1);tfdr_run(b->q);tfdr_plant(0);}
/* Equal: every register out, the frame. */
static int tfdr_same(const tfdr_block_t *a,const tfdr_block_t *b) {return !memcmp((const unsigned char*)a+0x1a0,(const unsigned char*)b+0x1a0,0x180);}
#define TFDR_CASES 600u
static tfdr_block_t tfdr_ref[FDR_SITES][TFDR_CASES];
static unsigned tfdr_out_reg(unsigned w) {return w==FDR_SPREAD?1:w==FDR_MISS?1:10;}
static float tfdr_in(const tfdr_block_t *b,unsigned k) {float f;memcpy(&f,(const unsigned char*)b+0x20+0x10*k,4);return f;}
/* The window's result from its inputs with ours (lockstep_drone_factors.h) where the site is, Windows' order around it. */
static uint32_t tfdr_expect(unsigned w,const tfdr_block_t *b) {
    if(w==FDR_SPREAD) {const float p15=tfdr_f((uint32_t)b->q[2]);return tfdr_b(fdr_spread(((tfdr_in(b,0)+tfdr_in(b,6))*0.5f)*p15));}
    if(w==FDR_TIME) return tfdr_b(tfdr_in(b,10)*(fdr_keep(tfdr_in(b,6))*tfdr_in(b,9)));
    if(w==FDR_ROLL) return tfdr_b(tfdr_in(b,10)*(fdr_keep((tfdr_in(b,0)+tfdr_in(b,11))*0.5f)*tfdr_in(b,9)));
    return tfdr_b(fdr_miss(tfdr_in(b,14)));
}
static void test_fdr_native_image(void) {
    DWORD old;assert(VirtualProtect(image+0x4f1000u,0x2000,PAGE_EXECUTE_READWRITE,&old));
    FlushInstructionCache(GetCurrentProcess(),image+0x4f1000u,0x2000);
    /* pins: the function through the view (FR29b's rel32s there are armed: the view shows the originals), the constants */
    assert(fa_installed && fdr_pins_ok());
    image[FDR_W_FN+0x300]^=1;assert(!fdr_pins_ok());image[FDR_W_FN+0x300]^=1;
    image[0x2cbe394u]^=1;assert(!fdr_pins_ok());image[0x2cbe394u]^=1;image[0x2cbe0b8u+2]^=1;assert(!fdr_pins_ok());image[0x2cbe0b8u+2]^=1;
    for(unsigned i=0;i<FDR_SITES;i++) assert(fdr_state(i)==0 && fdr_clear(i) && fdr_reach(i));
    /* the inputs where the table says: s in xmm1 (mulss xmm1,xmm6 at 0x4f20fc), 100 - p in xmm0 from xmm13 and xmm6 (0x4f24b0),
     * 0.01 in xmm8 (0x4f24b8), x in xmm0 and 100 - x in xmm1 (0x4f251b, 0x4f2523), p16 in xmm1 from xmm14 and 250 in xmm2 */
    assert(!memcmp(image+0x4f20fc,"\xf3\x0f\x59\xce",4) && !memcmp(image+0x4f24b0,"\x41\x0f\x28\xc5\xf3\x0f\x5c\xc6\xf3\x44\x0f\x10\x05",13));
    assert(!memcmp(image+0x4f2516,"\xf3\x41\x0f\x58\xc3\xf3\x0f\x59\x05",9) && !memcmp(image+0x4f2523,"\x41\x0f\x28\xcd\xf3\x0f\x5c\xc8",8));
    assert(!memcmp(image+0x4f274c,"\xf3\x0f\x10\x15",4) && !memcmp(image+0x4f2774,"\x41\x0f\x28\xce",4) && !memcmp(image+0x4f2784,"\x0f\x2f\xc1",3));
    /* native: the references */
    tfdr_seed=0x30303030u;
    for(unsigned w=0;w<FDR_SITES;w++) for(unsigned i=0;i<TFDR_CASES;i++) {tfdr_case(w,i,&tfdr_ref[w][i],0x42c80000u);tfdr_go(&tfdr_ref[w][i]);}
    assert(tfdr_out(&tfdr_ref[FDR_ROLL][0],10)==0x3f5b5eeau && tfdr_ref[FDR_ROLL][0].q[0x318/8]==0); /* tick 546: Windows' live factor */
    for(unsigned w=0;w<FDR_SITES;w++) for(unsigned i=0;i<TFDR_CASES;i++) { /* Windows native is ours, from the inputs */
        const tfdr_block_t *b=&tfdr_ref[w][i];
        if(tfdr_out(b,tfdr_out_reg(w))!=tfdr_expect(w,b)) {
            printf("FR30 window %u case %u: native %08x ours %08x\n",w,i,tfdr_out(b,tfdr_out_reg(w)),tfdr_expect(w,b));fflush(stdout);assert(0);
        }
    }
    /* armed: 'shadow', then 'redirect': every register as native (on Windows native is Windows' order) */
    const uint32_t sa=stats.active;stats.active=1;
    for(int mode=FDR_SHADOW;mode<=FDR_REDIRECT;mode++) {
        assert(fdr_arm(mode) && fdr_installed && !strcmp(fdr_stage,"armed") && fdr_arm(mode) && !fdr_arm(mode==FDR_SHADOW?FDR_REDIRECT:FDR_SHADOW));
        for(unsigned i=0;i<FDR_SITES;i++) {
            unsigned char viewed[24];int32_t d;memcpy(&d,image+fdr_sites[i].site+1,4);
            assert(fdr_state(i)==1 && !fdr_clear(i) && image+fdr_sites[i].site+5+d==fdr_stub(i));
            assert(read_local(image+fdr_sites[i].site,viewed,fdr_len(i)) && !memcmp(viewed,fdr_sites[i].orig,fdr_len(i))); /* the view */
        }
        assert(fdr_pins_ok() && fa_pins_ok() && fr_owned() && fr_check()); /* the pins see the original bytes */
        tfdr_seed=0x30303030u;
        for(unsigned w=0;w<FDR_SITES;w++) for(unsigned i=0;i<TFDR_CASES;i++) {
            static tfdr_block_t b;tfdr_case(w,i,&b,0x42c80000u);tfdr_go(&b);
            if(!tfdr_same(&b,&tfdr_ref[w][i])) {
                printf("FR30 window %u case %u mode %d differs from native (out %08x native %08x)\n",w,i,mode,tfdr_out(&b,tfdr_out_reg(w)),tfdr_out(&tfdr_ref[w][i],tfdr_out_reg(w)));
                for(unsigned k=0;k<0x180;k+=8) {uint64_t x,y;memcpy(&x,(unsigned char*)&b+0x1a0+k,8);memcpy(&y,(unsigned char*)&tfdr_ref[w][i]+0x1a0+k,8);if(x!=y) printf("  +%03x %016llx vs %016llx\n",0x1a0+k,(unsigned long long)x,(unsigned long long)y);}
                fflush(stdout);assert(0);
            }
        }
        for(unsigned i=0;i<FDR_SITES;i++) assert(fdr_n[i]==TFDR_CASES && fdr_diff[i]==0);
        assert(fdr_ours==(mode==FDR_REDIRECT?FDR_SITES*TFDR_CASES:0) && fdr_max_ulp==0 && fdr_off==0);
        /* a native that is not Windows' order (100 moved by an ulp where native reads it, xmm13): 'shadow' keeps it,
         * 'redirect' writes ours */
        static tfdr_block_t b;unsigned changed=0;tfdr_seed=0x31313131u;
        for(unsigned i=1;i<200;i++) {
            tfdr_case(FDR_ROLL,i,&b,0x42c80001u);tfdr_go(&b);
            uint32_t r,q,f1,f31;memcpy(&r,(unsigned char*)&b+0x20,4);memcpy(&q,(unsigned char*)&b+0x20+0x10*11,4);
            memcpy(&f1,(unsigned char*)&b+0x20+0x10*9,4);memcpy(&f31,(unsigned char*)&b+0x20+0x10*10,4);
            const float x=(tfdr_f(r)+tfdr_f(q))*0.5f,native_t=((tfdr_f(0x42c80001u)-x)*FDR_HUNDREDTH)*tfdr_f(f1),ours_t=fdr_keep(x)*tfdr_f(f1);
            const uint32_t want=tfdr_b(tfdr_f(f31)*(mode==FDR_REDIRECT?ours_t:native_t));
            assert(tfdr_out(&b,10)==want);changed+=tfdr_b(native_t)!=tfdr_b(ours_t);
        }
        assert(changed>20 && fdr_diff[FDR_ROLL]>=changed);
        char out[2048];assert(fr_status_win(out,sizeof(out))>0 && strstr(out,mode==FDR_REDIRECT?" drones=redirect dr_stage=armed":" drones=shadow dr_stage=armed"));
        /* ownership: a site changed under us */
        image[fdr_sites[2].site+3]^=1;assert(!fdr_owned() && !fr_owned());image[fdr_sites[2].site+3]^=1;assert(fdr_owned() && fr_owned());
        fdr_restore();assert(!fdr_installed && !fdr_done && fdr_mode==FDR_OFF && !strcmp(fdr_stage,"restored"));
        for(unsigned i=0;i<FDR_SITES;i++) assert(fdr_state(i)==0 && fdr_clear(i));
    }
    /* armed again for fr_restore (which takes the sites back first) */
    assert(fdr_arm(FDR_REDIRECT) && fdr_done==(1u<<FDR_SITES)-1u);
    stats.active=sa;
    DWORD ignored;assert(VirtualProtect(image+0x4f1000u,0x2000,old,&ignored));
    printf("fp redirect FR30 native image: the drone path 0x4f1eb0's four sites pinned (through the view, with FR29b's rel32s armed), "
           "armed/owned/restored in 'shadow' and 'redirect', the view showing the original bytes; its own bytes through the sites on "
           "%u random cases per site (tick 546's roll 0x3f5b5eea among them): every register equal to native (dr=0:0), and a native "
           "off Windows' order kept in 'shadow', replaced in 'redirect' PASS\n",TFDR_CASES);
}
/* After fr_restore: the sites back, nothing of ours left. */
static void test_fdr_native_image_restored(void) {
    assert(!fdr_installed && !fdr_done && fdr_mode==FDR_OFF && !strcmp(fdr_stage,"restored"));
    for(unsigned i=0;i<FDR_SITES;i++) assert(fdr_state(i)==0 && fdr_clear(i));
    fdr_stage="idle";for(unsigned i=0;i<FDR_SITES;i++) fdr_n[i]=fdr_diff[i]=0;
    fdr_ours=fdr_max_ulp=fdr_off=0;
}

/* The gate exit sum in the Windows order (gate_warp_exit.h): the native bytes and the stub run through a register
 * harness (GCC's order against MSVC's, on inputs where they round apart); on the synthetic image the arming refusals,
 * then with the native bytes planted: armed, owned, the patched entry run end to end, a changed stub or block stops
 * it, the restore; on the real executable the pin and the same end-to-end run on its own bytes. Included by
 * test_gate_linux.c. */
typedef struct {uint64_t entry,g,s,rbp;uint32_t x2,x4,out0,out1;} twx_block_t;
static volatile uint64_t twx_block __attribute__((used));
/* twx_run(block): rdx = g, rax = s, rbp, xmm2 and xmm4 (the abs mask) as the poster has them at 0x1615324, then jmp to
 * block->entry; the code under test ends at twx_back (planted after it), which keeps xmm0 and xmm1. */
__asm__(".text\n.p2align 4\ntwx_run:\n"
" push %rbp\n push %rbx\n sub $8,%rsp\n mov %rdi,twx_block(%rip)\n"
" mov 0x8(%rdi),%rdx\n mov 0x10(%rdi),%rax\n mov 0x18(%rdi),%rbp\n movss 0x20(%rdi),%xmm2\n movss 0x24(%rdi),%xmm4\n"
" jmp *(%rdi)\n"
"twx_back:\n mov twx_block(%rip),%rdi\n movss %xmm0,0x28(%rdi)\n movss %xmm1,0x2c(%rdi)\n"
" add $8,%rsp\n pop %rbx\n pop %rbp\n ret\n"
"twx_back_addr:\n lea twx_back(%rip),%rax\n ret\n");
extern void twx_run(twx_block_t *block);
extern void *twx_back_addr(void);
/* The executable's bytes 0x1615324..0x1615352 (GCC: ((a + b) + c) + |d|). */
static const unsigned char twx_native[LXWX_SUM]={
    0xf3,0x0f,0x10,0x82,0x68,0x02,0x00,0x00, 0xf3,0x0f,0x10,0x88,0x78,0x02,0x00,0x00, 0xf3,0x0f,0x58,0x82,0x78,0x02,0x00,0x00,
    0xf3,0x0f,0x58,0x80,0x68,0x02,0x00,0x00, 0xf3,0x0f,0x11,0x95,0x58,0xfd,0xff,0xff, 0x0f,0x54,0xcc, 0xf3,0x0f,0x58,0xc1};
/* What the stub must run (MSVC: (a + b) + (|d| + c)), written out on its own. */
static const unsigned char twx_windows_order[LXWX_SUM]={
    0xf3,0x0f,0x10,0x82,0x68,0x02,0x00,0x00, /* movss xmm0,[rdx+0x268] */
    0xf3,0x0f,0x10,0x88,0x78,0x02,0x00,0x00, /* movss xmm1,[rax+0x278] */
    0xf3,0x0f,0x58,0x82,0x78,0x02,0x00,0x00, /* addss xmm0,[rdx+0x278] */
    0x0f,0x54,0xcc,                          /* andps xmm1,xmm4 */
    0xf3,0x0f,0x58,0x88,0x68,0x02,0x00,0x00, /* addss xmm1,[rax+0x268] */
    0xf3,0x0f,0x11,0x95,0x58,0xfd,0xff,0xff, /* movss [rbp-0x2a8],xmm2 */
    0xf3,0x0f,0x58,0xc1};                    /* addss xmm0,xmm1 */
#define TWX_A 2900.5f
#define TWX_B 80.125f
#define TWX_C 1.7f
#define TWX_D (-0.0123f)
#define TWX_GCC 0x453a6565u   /* ((a + b) + c) + |d| */
#define TWX_MSVC 0x453a6566u  /* (a + b) + (c + |d|) */
static uint32_t twx_bits(float f) {uint32_t u;memcpy(&u,&f,4);return u;}
static void twx_plant_jmp(unsigned char *at,const void *to) {at[0]=0xff;at[1]=0x25;memset(at+2,0,4);memcpy(at+6,&to,8);}
static void twx_go(twx_block_t *b,const void *entry) {
    static unsigned char g[0x300] __attribute__((aligned(16))),s[0x300] __attribute__((aligned(16))),frame[0x300];
    float v=TWX_A;memcpy(g+0x268,&v,4);v=TWX_B;memcpy(g+0x278,&v,4);v=TWX_C;memcpy(s+0x268,&v,4);v=TWX_D;memcpy(s+0x278,&v,4);
    memset(frame,0,sizeof(frame));memset(b,0,sizeof(*b));
    b->entry=(uint64_t)(uintptr_t)entry;b->g=(uint64_t)(uintptr_t)g;b->s=(uint64_t)(uintptr_t)s;b->rbp=(uint64_t)(uintptr_t)(frame+0x2b0);
    b->x2=0x42731fcau;b->x4=0x7fffffffu;
    twx_run(b);
    uint32_t kept;memcpy(&kept,frame+8,4);assert(kept==0x42731fcau); /* [rbp-0x2a8] = xmm2 on both */
}
static void test_warp_exit_linux(void) {
    volatile float a=TWX_A,b=TWX_B,c=TWX_C,d=TWX_D;float w=(a+b)+(c+fabsf(d)),l=((a+b)+c)+fabsf(d),cd=c+fabsf(d);
    assert(twx_bits(w)==TWX_MSVC && twx_bits(l)==TWX_GCC);
    /* The arithmetic on pages near the code: the native bytes then a jmp back give GCC's order; the stub MSVC's, counted. */
    {unsigned char *page=gate_near_page((const void*)twx_run),*data=page?gate_near_page(page):NULL,*nat=data?gate_near_page(page):NULL;
     assert(page && data && nat);
     lxwx_stub_bytes(page,page,data,twx_native,twx_back_addr());
     assert(!memcmp(page,"\xf0\x48\xff\x05",4) && !memcmp(page+8,twx_windows_order,LXWX_SUM) && page[55]==0xff && page[56]==0x25);
     {int32_t disp;memcpy(&disp,page+4,4);assert(page+8+disp==data);void *back;memcpy(&back,page+61,8);assert(back==twx_back_addr());}
     memcpy(nat,twx_native,LXWX_SUM);twx_plant_jmp(nat+LXWX_SUM,twx_back_addr());
     assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_EXEC) && !mprotect(nat,GATE_PAGE,PROT_READ|PROT_EXEC));
     twx_block_t blk;uint64_t n;
     twx_go(&blk,nat);memcpy(&n,data,8);assert(blk.out0==TWX_GCC && blk.out1==twx_bits(fabsf(TWX_D)) && !n);
     twx_go(&blk,page);memcpy(&n,data,8);assert(blk.out0==TWX_MSVC && blk.out1==twx_bits(cd) && n==1);
     twx_go(&blk,page);memcpy(&n,data,8);assert(n==2);
     munmap(page,GATE_PAGE);munmap(data,GATE_PAGE);munmap(nat,GATE_PAGE);}
    /* Arming on the synthetic image: before the first admitted frame, on the MoveWorker, the entry as the native movss. */
    reset_session();DWORD self=GetCurrentThreadId();
    stats.active=1;boot_mode=1;boot_phase=0;ui_thread=self;stats.issued=1;wx_installed=wx_restored=0;wx_failures=0;wx_stage="idle";
    assert(!wx_arm());stats.issued=0;ui_thread=self+1;assert(!wx_arm());ui_thread=self;
    assert(lxwx_state()<0 && !wx_arm() && !strcmp(wx_stage,"entry"));
    unsigned i=0;while(i<gate_site_count && strcmp(gate_sites[i].name,"warp_exit_sum")) ++i;
    assert(i<gate_site_count && gate_sites==synth_sites && lx_sites[i].va==0x16152fc && lx_sites[i].length==0x57);
    unsigned char *pg=(unsigned char*)((uintptr_t)LX(LX_WX_ENTRY_VA)&~(uintptr_t)(GATE_PAGE-1)),saved[0x80];
    memcpy(saved,LX(0x16152fc),sizeof(saved));
    memcpy(LX(LX_WX_ENTRY_VA),twx_native,LXWX_SUM);
    assert(lxwx_state()==0 && !wx_arm() && !strcmp(wx_stage,"pins")); /* the synthetic pin is the zeros' */
    char hex[65];assert(digest_hex(LX(lx_sites[i].va),lx_sites[i].length,hex));memcpy(synth_hashes[i],hex,65);
    twx_plant_jmp(LX(LX_WX_RETURN_VA),twx_back_addr()); /* past the pin: the end-to-end run's way back */
    unsigned char line[16];memcpy(line,LX(LXWX_LINE),16);
    assert(lxwx_pins() && wx_arm() && wx_installed && !strcmp(wx_stage,"armed") && lxwx_state()==1 && lxwx_owned() && wx_check() && wx_arm());
    {unsigned char e[16];memcpy(e,LX(LXWX_LINE),16);int32_t disp;memcpy(&disp,e+5,4);
     assert(!memcmp(e,line,4) && !memcmp(e+12,line+12,4) && e[4]==0xe9 && e[9]==0x0f && e[10]==0x1f && e[11]==0x00 &&
            LX(LX_WX_ENTRY_VA)+5+disp==lxwx_page && !memcmp(lxwx_page+8,twx_windows_order,LXWX_SUM));}
    twx_block_t blk;uint64_t before=lxwx_calls();twx_go(&blk,LX(LX_WX_ENTRY_VA));assert(blk.out0==TWX_MSVC && lxwx_calls()==before+1);
    char status[256];
    assert(wx_status(status,sizeof(status))>0 && strstr(status," installed=1 restored=0 calls=1 failures=0 state=1 pin=1 stage=armed"));
    /* A changed stub or block stops it (stop reason 146 in the tick). */
    assert(!mprotect(lxwx_page,GATE_PAGE,PROT_READ|PROT_WRITE));lxwx_page[20]^=1;assert(!wx_check() && !strcmp(wx_stage,"entry_changed"));
    lxwx_page[20]^=1;assert(!mprotect(lxwx_page,GATE_PAGE,PROT_READ|PROT_EXEC));
    assert(!mprotect(pg,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));*LX(0x1615305)^=1;assert(!wx_check() && !strcmp(wx_stage,"block_changed"));
    *LX(0x1615305)^=1;wx_failures=0;wx_stage="armed";assert(wx_check());
    /* Restored: the line as found, the native order again. */
    wx_restore();assert(!wx_installed && wx_restored && lxwx_state()==0 && !memcmp(line,LX(LXWX_LINE),16));
    twx_go(&blk,LX(LX_WX_ENTRY_VA));assert(blk.out0==TWX_GCC);
    wx_restore();assert(wx_restored);
    /* A foreign entry after arming: refused by the check, left by the restore. */
    assert(wx_arm() && wx_installed);
    assert(!mprotect(pg,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));unsigned char ours=LX(LX_WX_ENTRY_VA)[7];LX(LX_WX_ENTRY_VA)[7]=0x90;
    assert(lxwx_state()<0 && !wx_check() && !strcmp(wx_stage,"entry_changed"));wx_restore();assert(wx_installed && LX(LX_WX_ENTRY_VA)[7]==0x90);
    LX(LX_WX_ENTRY_VA)[7]=ours;wx_restore();assert(!wx_installed && lxwx_state()==0);
    assert(!mprotect(pg,GATE_PAGE,PROT_READ|PROT_WRITE));memcpy(LX(0x16152fc),saved,sizeof(saved));
    assert(digest_hex(LX(lx_sites[i].va),lx_sites[i].length,hex));memcpy(synth_hashes[i],hex,65);
    munmap(lxwx_page,GATE_PAGE);munmap(lxwx_data,GATE_PAGE);lxwx_page=lxwx_data=NULL;
    wx_restored=0;wx_failures=0;wx_stage="idle";stats.active=0;
    printf("warp exit sum order/stub/arming/check/restore PASS\n");
}
/* The real executable: the pin, its own bytes, armed, the patched entry run end to end in the Windows order, restored. */
static void test_warp_exit_elf(void) {
    unsigned char sum[LXWX_SUM];assert(lxwx_pins() && lxwx_state()==0 && lxwx_sum(sum) && !memcmp(sum,twx_native,LXWX_SUM));
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();wx_installed=wx_restored=0;
    unsigned char *pg=(unsigned char*)((uintptr_t)LX(LX_WX_ENTRY_VA)&~(uintptr_t)(GATE_PAGE-1)),saved[14],line[16];
    memcpy(saved,LX(LX_WX_RETURN_VA),14);memcpy(line,LX(LXWX_LINE),16);
    assert(!mprotect(pg,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));twx_plant_jmp(LX(LX_WX_RETURN_VA),twx_back_addr());
    twx_block_t blk;twx_go(&blk,LX(LX_WX_ENTRY_VA));assert(blk.out0==TWX_GCC); /* the executable's own order */
    assert(wx_arm() && wx_installed && lxwx_state()==1 && lxwx_owned() && wx_check() && !memcmp(lxwx_page+8,twx_windows_order,LXWX_SUM));
    uint64_t before=lxwx_calls();twx_go(&blk,LX(LX_WX_ENTRY_VA));assert(blk.out0==TWX_MSVC && lxwx_calls()==before+1);
    wx_restore();assert(!wx_installed && wx_restored && lxwx_state()==0 && !memcmp(line,LX(LXWX_LINE),16));
    twx_go(&blk,LX(LX_WX_ENTRY_VA));assert(blk.out0==TWX_GCC);
    assert(!mprotect(pg,GATE_PAGE,PROT_READ|PROT_WRITE));memcpy(LX(LX_WX_RETURN_VA),saved,14);assert(lxwx_pins());
    munmap(lxwx_page,GATE_PAGE);munmap(lxwx_data,GATE_PAGE);lxwx_page=lxwx_data=NULL;wx_restored=0;stats.active=0;
    printf("warp exit sum ELF PASS\n");
}

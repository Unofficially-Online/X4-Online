/* The flight limits in the Windows order (gate_flight_limits.h): the native block and the stub run through a register
 * harness on inputs where x / m and x * (1 / m) round apart (GCC's division against MSVC's hoisted reciprocal); on the
 * synthetic image the arming refusals, then with the native bytes and their constants planted: armed, owned, the
 * patched entry run end to end, a changed stub or block stops it, the restore; on the real executable the pin and the
 * same end-to-end run on its own bytes. Included by test_gate_linux.c. */
typedef struct {uint64_t entry,rsp,rbx,r13,saved;} tfl_block_t;
static volatile uint64_t tfl_block __attribute__((used));
/* tfl_run(block): rbx = the destination, r13 = the constants (+0x318 the floor k), rsp = the block's frame ([rsp] m,
 * +0x120.. the vectors), then jmp to block->entry; the code under test ends at tfl_back (planted at its jmp target). */
__asm__(".text\n.p2align 4\ntfl_run:\n"
" push %rbp\n push %rbx\n push %r12\n push %r13\n push %r14\n push %r15\n sub $8,%rsp\n"
" mov %rdi,tfl_block(%rip)\n mov %rsp,0x20(%rdi)\n"
" mov 0x10(%rdi),%rbx\n mov 0x18(%rdi),%r13\n mov 0x8(%rdi),%rsp\n"
" jmp *(%rdi)\n"
"tfl_back:\n mov tfl_block(%rip),%rdi\n mov 0x20(%rdi),%rsp\n"
" add $8,%rsp\n pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbx\n pop %rbp\n ret\n"
"tfl_back_addr:\n lea tfl_back(%rip),%rax\n ret\n");
extern void tfl_run(tfl_block_t *block);
extern void *tfl_back_addr(void);
/* The executable's bytes 0x148d812..0x148d936. */
static const unsigned char tfl_native[LXFL_BLOCK]={
    0xf3,0x0f,0x10,0x1d,0x86,0x70,0x99,0x01,0xf3,0x0f,0x10,0x0c,0x24,0xf3,0x0f,0x10,0x94,0x24,0x28,0x01,
    0x00,0x00,0xf3,0x41,0x0f,0x10,0x85,0x18,0x03,0x00,0x00,0xf3,0x0f,0x7e,0x35,0x1f,0x99,0x9a,0x01,0x0f,
    0x54,0xd3,0xf3,0x0f,0x5f,0xc2,0xf3,0x0f,0x5e,0xc1,0xf3,0x0f,0x5f,0x03,0xf3,0x0f,0x11,0x03,0xf3,0x41,
    0x0f,0x10,0xa5,0x18,0x03,0x00,0x00,0xf3,0x0f,0x5f,0xe2,0xf3,0x0f,0x10,0x94,0x24,0x20,0x01,0x00,0x00,
    0x0f,0x54,0xd3,0x0f,0x28,0xc4,0xf3,0x0f,0x7e,0x25,0x28,0x60,0x8d,0x01,0xf3,0x0f,0x5e,0xc1,0xf3,0x0f,
    0x7e,0xec,0xf3,0x0f,0x5f,0x43,0x04,0xf3,0x0f,0x11,0x43,0x04,0xf3,0x41,0x0f,0x10,0x85,0x18,0x03,0x00,
    0x00,0xf3,0x0f,0x5f,0xc2,0xf3,0x0f,0x10,0x94,0x24,0x24,0x01,0x00,0x00,0x0f,0x54,0xd3,0xf3,0x0f,0x5e,
    0xc1,0xf3,0x0f,0x5f,0x43,0x08,0xf3,0x0f,0x11,0x43,0x08,0xf3,0x41,0x0f,0x10,0x85,0x18,0x03,0x00,0x00,
    0xf3,0x0f,0x5f,0xc2,0xf3,0x0f,0x7e,0x94,0x24,0x30,0x01,0x00,0x00,0x0f,0x54,0xd6,0xf3,0x0f,0x5e,0xc1,
    0xf3,0x0f,0x5f,0x43,0x0c,0xf3,0x0f,0x11,0x43,0x0c,0xf3,0x0f,0x7e,0xc2,0x0f,0xc2,0xc5,0x01,0x66,0x0f,
    0x38,0x14,0xd4,0xf3,0x0f,0x12,0xc1,0xf3,0x0f,0x7e,0x63,0x18,0xf3,0x0f,0x7e,0xd2,0x0f,0x16,0x05,0x97,
    0x51,0x81,0x01,0x0f,0x5e,0xd0,0xf3,0x0f,0x7e,0xc4,0xf3,0x0f,0x7e,0xea,0x0f,0xc2,0xc5,0x01,0x66,0x0f,
    0x38,0x14,0xe2,0xf3,0x0f,0x10,0x94,0x24,0x38,0x01,0x00,0x00,0x0f,0x13,0x63,0x18,0x0f,0x54,0xd3,0xf3,
    0x0f,0x10,0x1d,0x7b,0x5f,0x8d,0x01,0xf3,0x0f,0x5f,0xda,0x0f,0x28,0xc3,0xf3,0x0f,0x5e,0xc1,0xf3,0x0f,
    0x5f,0x43,0x20,0xf3,0x0f,0x11,0x43,0x20,0xe9,0x27,0xf7,0xff,0xff
};
static void tfl_plant_jmp(unsigned char *at,const void *to) {at[0]=0xff;at[1]=0x25;memset(at+2,0,4);memcpy(at+6,&to,8);}
static uint32_t tfl_bits(float f) {uint32_t u;memcpy(&u,&f,4);return u;}
#define TFL_M 3.0f
static const float tfl_v[6]={5.0f,11.0f,7.0f,13.0f,17.0f,19.0f}; /* [rsp+0x120], +0x124, +0x128, +0x130, +0x134, +0x138 */
static float tfl_dest[12] __attribute__((aligned(16)));
/* The seven words both ways: dest +0x0 and +0x4 from v.z, +0x8 v.x, +0xc v.y, +0x18/+0x1c the pair, +0x20 the last. */
static void tfl_expect(uint32_t out[7],int windows) {
    volatile float m=TFL_M,r=1.0f/m;const float x[7]={tfl_v[2],tfl_v[2],tfl_v[0],tfl_v[1],tfl_v[3],tfl_v[4],tfl_v[5]};
    for(unsigned i=0;i<7;i++) {volatile float a=x[i];out[i]=tfl_bits(windows?a*r:a/m);}
}
static void tfl_go(const void *entry,uint32_t out[7]) {
    static unsigned char frame[0x200] __attribute__((aligned(16))),consts[0x400] __attribute__((aligned(16)));
    memset(frame,0,sizeof(frame));memset(consts,0,sizeof(consts));memset(tfl_dest,0,sizeof(tfl_dest));
    float k=0.5f;memcpy(consts+0x318,&k,4);float m=TFL_M;memcpy(frame,&m,4);
    memcpy(frame+0x120,&tfl_v[0],12);memcpy(frame+0x130,&tfl_v[3],12);
    tfl_block_t b;memset(&b,0,sizeof(b));
    b.entry=(uint64_t)(uintptr_t)entry;b.rsp=(uint64_t)(uintptr_t)frame;b.rbx=(uint64_t)(uintptr_t)tfl_dest;b.r13=(uint64_t)(uintptr_t)consts;
    tfl_run(&b);
    const unsigned at[7]={0,1,2,3,6,7,8};
    for(unsigned i=0;i<7;i++) memcpy(&out[i],&tfl_dest[at[i]],4);
}
/* The block's RIP-relative constants on the synthetic image (the abs masks, pi/4, the divps' 1.0 lanes). */
static unsigned char tfl_const_saved[4][8];
static const uint32_t tfl_const_va[4]={0x2e248a0u,0x2e37158u,0x2d63898u,0x2ca2a88u};
static void tfl_plant_constants(int plant) {
    static const uint32_t values[4][2]={{0x7fffffffu,0},{0x7fffffffu,0x7fffffffu},{0x3f490fdbu,0x3f490fdbu},{0x3f800000u,0x3f800000u}};
    for(unsigned i=0;i<4;i++) {
        unsigned char *at=LX(tfl_const_va[i]);
        assert(!mprotect((void*)((uintptr_t)at&~(uintptr_t)(GATE_PAGE-1)),GATE_PAGE,PROT_READ|PROT_WRITE));
        if(plant) {memcpy(tfl_const_saved[i],at,8);memcpy(at,values[i],8);} else memcpy(at,tfl_const_saved[i],8);
    }
}
static void test_flight_limits_linux(void) {
    uint32_t gcc[7],msvc[7],got[7];tfl_expect(gcc,0);tfl_expect(msvc,1);
    unsigned apart=0;for(unsigned i=0;i<7;i++) apart+=gcc[i]!=msvc[i];
    assert(apart>=3); /* 7/3, 5/3, 11/3, 13/3, 17/3, 19/3: at least some round apart */
    /* Arming on the synthetic image: before the first admitted frame, on the MoveWorker, the entry as the native movss. */
    reset_session();DWORD self=GetCurrentThreadId();
    stats.active=1;boot_mode=1;boot_phase=0;ui_thread=self;stats.issued=1;fl_installed=fl_restored=0;fl_failures=0;fl_stage="idle";
    assert(!fl_arm());stats.issued=0;ui_thread=self+1;assert(!fl_arm());ui_thread=self;
    assert(lxfl_state()<0 && !fl_arm() && !strcmp(fl_stage,"entry"));
    unsigned i=0;while(i<gate_site_count && strcmp(gate_sites[i].name,"flight_limits_block")) ++i;
    assert(i<gate_site_count && gate_sites==synth_sites && lx_sites[i].va==LX_FL_ENTRY_VA && lx_sites[i].length==LXFL_BLOCK);
    unsigned char *pg=(unsigned char*)((uintptr_t)LX(LX_FL_ENTRY_VA)&~(uintptr_t)(GATE_PAGE-1)),saved[LXFL_BLOCK],back_saved[14];
    assert(!mprotect(pg,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));
    memcpy(saved,LX(LX_FL_ENTRY_VA),LXFL_BLOCK);memcpy(back_saved,LX(LX_FL_RETURN_VA),14);
    memcpy(LX(LX_FL_ENTRY_VA),tfl_native,LXFL_BLOCK);
    assert(lxfl_state()==0 && !fl_arm() && !strcmp(fl_stage,"pins")); /* the synthetic pin is the zeros' */
    char hex[65];assert(digest_hex(LX(lx_sites[i].va),lx_sites[i].length,hex));memcpy(synth_hashes[i],hex,65);
    tfl_plant_jmp(LX(LX_FL_RETURN_VA),tfl_back_addr()); /* outside the pin: the end-to-end run's way back */
    tfl_plant_constants(1);
    tfl_go(LX(LX_FL_ENTRY_VA),got);
    assert(!memcmp(got,gcc,sizeof(got))); /* the native block divides */
    unsigned char line[16];memcpy(line,LX(LXFL_LINE),16);
    assert(lxfl_pins() && fl_arm() && fl_installed && !strcmp(fl_stage,"armed") && lxfl_state()==1 && lxfl_owned() && fl_check() && fl_arm());
    {unsigned char e[16];memcpy(e,LX(LXFL_LINE),16);int32_t disp;memcpy(&disp,e+3,4);
     assert(!memcmp(e,line,2) && !memcmp(e+10,line+10,6) && e[2]==0xe9 && e[7]==0x0f && e[8]==0x1f && e[9]==0x00 &&
            LX(LX_FL_ENTRY_VA)+5+disp==lxfl_page && lxfl_page[0]==0xf0);}
    uint64_t before=lxfl_calls();tfl_go(LX(LX_FL_ENTRY_VA),got);
    assert(!memcmp(got,msvc,sizeof(got)) && lxfl_calls()==before+1);
    char status[256];
    assert(fl_status(status,sizeof(status))>0 && strstr(status," installed=1 restored=0 calls=1 failures=0 state=1 pin=1 stage=armed"));
    /* A changed stub or block stops it (stop reason 189 in the tick). */
    assert(!mprotect(lxfl_page,GATE_PAGE,PROT_READ|PROT_WRITE));lxfl_page[40]^=1;assert(!fl_check() && !strcmp(fl_stage,"entry_changed"));
    lxfl_page[40]^=1;assert(!mprotect(lxfl_page,GATE_PAGE,PROT_READ|PROT_EXEC));
    assert(!mprotect(pg,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));*LX(LX_FL_ENTRY_VA+0x30)^=1;assert(!fl_check() && !strcmp(fl_stage,"entry_changed")); /* the stub follows the block: owned fails first */
    *LX(LX_FL_ENTRY_VA+0x30)^=1;fl_failures=0;fl_stage="armed";assert(fl_check());
    *LX(LX_FL_ENTRY_VA+0x121)^=1;assert(!fl_check() && !strcmp(fl_stage,"block_changed")); /* the final jmp: pinned, not copied */
    *LX(LX_FL_ENTRY_VA+0x121)^=1;fl_failures=0;fl_stage="armed";assert(fl_check());
    /* Restored: the line as found, the native division again. */
    fl_restore();assert(!fl_installed && fl_restored && lxfl_state()==0 && !memcmp(line,LX(LXFL_LINE),16));
    tfl_go(LX(LX_FL_ENTRY_VA),got);assert(!memcmp(got,gcc,sizeof(got)));
    fl_restore();assert(fl_restored);
    /* A foreign entry after arming: refused by the check, left by the restore. */
    assert(fl_arm() && fl_installed);
    assert(!mprotect(pg,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));unsigned char ours=LX(LX_FL_ENTRY_VA)[7];LX(LX_FL_ENTRY_VA)[7]=0x90;
    assert(lxfl_state()<0 && !fl_check() && !strcmp(fl_stage,"entry_changed"));fl_restore();assert(fl_installed && LX(LX_FL_ENTRY_VA)[7]==0x90);
    LX(LX_FL_ENTRY_VA)[7]=ours;fl_restore();assert(!fl_installed && lxfl_state()==0);
    tfl_plant_constants(0);
    assert(!mprotect(pg,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));memcpy(LX(LX_FL_ENTRY_VA),saved,LXFL_BLOCK);memcpy(LX(LX_FL_RETURN_VA),back_saved,14);assert(!mprotect(pg,GATE_PAGE,PROT_READ|PROT_WRITE));
    assert(digest_hex(LX(lx_sites[i].va),lx_sites[i].length,hex));memcpy(synth_hashes[i],hex,65);
    munmap(lxfl_page,GATE_PAGE);munmap(lxfl_data,GATE_PAGE);lxfl_page=lxfl_data=NULL;
    fl_restored=0;fl_failures=0;fl_stage="idle";stats.active=0;
    printf("flight limits order/stub/arming/check/restore PASS\n");
}
/* The real executable: the pin, its own bytes, armed, the patched entry run end to end in the Windows order, restored. */
static void test_flight_limits_elf(void) {
    unsigned char block[LXFL_BLOCK];assert(lxfl_pins() && lxfl_state()==0 && lxfl_block(block) && !memcmp(block,tfl_native,LXFL_BLOCK));
    uint32_t gcc[7],msvc[7],got[7];tfl_expect(gcc,0);tfl_expect(msvc,1);
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();fl_installed=fl_restored=0;
    unsigned char *pg=(unsigned char*)((uintptr_t)LX(LX_FL_ENTRY_VA)&~(uintptr_t)(GATE_PAGE-1)),saved[14],line[16];
    memcpy(saved,LX(LX_FL_RETURN_VA),14);memcpy(line,LX(LXFL_LINE),16);
    assert(!mprotect(pg,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));tfl_plant_jmp(LX(LX_FL_RETURN_VA),tfl_back_addr());
    tfl_go(LX(LX_FL_ENTRY_VA),got);assert(!memcmp(got,gcc,sizeof(got))); /* the executable's own division */
    assert(fl_arm() && fl_installed && lxfl_state()==1 && lxfl_owned() && fl_check());
    uint64_t before=lxfl_calls();tfl_go(LX(LX_FL_ENTRY_VA),got);assert(!memcmp(got,msvc,sizeof(got)) && lxfl_calls()==before+1);
    fl_restore();assert(!fl_installed && fl_restored && lxfl_state()==0 && !memcmp(line,LX(LXFL_LINE),16));
    tfl_go(LX(LX_FL_ENTRY_VA),got);assert(!memcmp(got,gcc,sizeof(got)));
    assert(!mprotect(pg,GATE_PAGE,PROT_READ|PROT_WRITE));memcpy(LX(LX_FL_RETURN_VA),saved,14);assert(lxfl_pins());
    munmap(lxfl_page,GATE_PAGE);munmap(lxfl_data,GATE_PAGE);lxfl_page=lxfl_data=NULL;fl_restored=0;stats.active=0;
    printf("flight limits ELF PASS\n");
}

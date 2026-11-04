/* Engine-parameter refreshes on keyed streams (gate_engine_rng.h): the state choice and the step, the
 * stub's machine code run for real against a landing pad, and on the real executable the pins, the
 * patch and the restore. Included by test_gate_linux.c. */
/* ters_call(stub, ship, io): rbx = ship, xmm1 = io[3], jump to the stub with its resume pointer set to
 * the landing pad, which stores rax, rsi, rdi, xmm1 and rbx into io[0..4]. 16-aligned at the jump, as
 * at the site (the listener's push rbx; sub rsp,0x10). */
__asm__(".pushsection .text\n"
        ".p2align 4\n"
        ".type ters_call,@function\n"
        "ters_call:\n"
        "  push %rbx\n  push %r12\n  push %r13\n"
        "  mov %rsp,%r12\n  and $-16,%rsp\n  mov %rdx,%r13\n  mov %rsi,%rbx\n  movsd 24(%rdx),%xmm1\n"
        "  lea 1f(%rip),%rax\n  mov %rax,0x48(%rdi)\n  jmp *%rdi\n"
        "1:\n  mov %rax,0(%r13)\n  mov %rsi,8(%r13)\n  mov %rdi,16(%r13)\n  movsd %xmm1,24(%r13)\n  mov %rbx,32(%r13)\n"
        "  mov %r12,%rsp\n  pop %r13\n  pop %r12\n  pop %rbx\n  ret\n"
        ".size ters_call,.-ters_call\n"
        ".popsection\n");
void ters_call(unsigned char *stub,const void *ship,uint64_t *io);
static uint64_t ters_ror30(uint64_t s) {s=s*0x5851f42d4c957f2dull+0x14057b7ef767814full;return (s>>30)|(s<<34);}
static void test_engine_rng_linux(void) {
    reset_session();stats.active=1;boot_mode=1;boot_phase=3;stats.issued=598;ui_thread=GetCurrentThreadId();
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);world_identity_installed=1;
    world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(9201,0x401));
    /* The step is the inlined FloatRange's (and Windows Random::FloatRange's) state step. */
    assert(erng_lx_pcg(0)==ters_ror30(0) && erng_lx_pcg(0x123456789abcdefull)==ters_ror30(0x123456789abcdefull));
    const lxw_class *ship_class=&lxw_classes[LXW_CLASS_COUNT/2];
    void *class_vt=LX(ship_class->linux_vtable),*foreign=(void*)0x10;
    unsigned char mapped[16],unmapped[16],stranger[16];uint64_t runtime;
    memcpy(mapped,&class_vt,8);runtime=9201;memcpy(mapped+8,&runtime,8);
    memcpy(unmapped,&class_vt,8);runtime=9999;memcpy(unmapped+8,&runtime,8);
    memcpy(stranger,&foreign,8);memcpy(stranger+8,&runtime,8);
    erng_keyed=erng_native=erng_unmapped=erng_fallback=0;
    uint64_t state=0;
    /* Keyed: Windows' (tick, key, 0, 0, class). */
    assert(erng_lx_state(mapped,&state)==1 && state==lockstep_seed(ERNG_DOMAIN,598,0x401,0,0,WORLD_KEY_SAVED) && erng_keyed==1);
    /* No key: (tick, 0, the class's Windows vtable RVA, 1, none), as cam-L1's fallback. */
    assert(erng_lx_state(unmapped,&state)==1 && state==lockstep_seed(ERNG_DOMAIN,598,0,ship_class->windows_rva,1,WORLD_KEY_NONE) && erng_fallback==1);
    /* Neither a key nor a known class, or not an admitted frame: the thread's word. */
    assert(erng_lx_state(stranger,&state)==0 && erng_unmapped==1);
    boot_phase=2;assert(erng_lx_state(mapped,&state)==0 && erng_native==1);boot_phase=3;
    /* The step: keyed leaves the thread's word alone; natively it steps it in place. */
    uint64_t *word=gate_tls_rng(gate_tls_self());
    *word=0x4444;assert(erng_lx_step(mapped)==ters_ror30(lockstep_seed(ERNG_DOMAIN,598,0x401,0,0,WORLD_KEY_SAVED)) && *word==0x4444);
    *word=0x4444;assert(erng_lx_step(stranger)==ters_ror30(0x4444) && *word==ters_ror30(0x4444));
    /* The stub, run: the pinned addsd's bytes in place on the synthetic image, the stub on an executable page. */
    static const unsigned char addsd[LX_ERNG_ADDSD_LENGTH]={0x64,0xf2,0x0f,0x58,0x0c,0x25,0xd8,0xd8,0xff,0xff};
    uintptr_t code=(uintptr_t)LX(LX_ERNG_ADDSD_VA)&~(uintptr_t)(GATE_PAGE-1); /* earlier patch tests leave text pages r-x */
    assert(!mprotect((void*)code,2*GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));
    memcpy(LX(LX_ERNG_ADDSD_VA),addsd,sizeof(addsd));
    unsigned char *page=mmap(NULL,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(page!=MAP_FAILED);
    memset(page,0xcc,GATE_PAGE);assert(erng_write_stub(page));
    double now;__asm__ volatile("movsd %%fs:-0x2728,%0":"=x"(now));
    uint64_t io[5]={0,0,0,0,0};double eight=8.0;memcpy(&io[3],&eight,8);
    *word=0x5555;ters_call(page,mapped,io);
    double x1;memcpy(&x1,&io[3],8);
    assert(io[0]==ters_ror30(lockstep_seed(ERNG_DOMAIN,598,0x401,0,0,WORLD_KEY_SAVED)) && *word==0x5555); /* keyed, word untouched */
    assert((uint32_t)io[1]==0x10 && (uint32_t)io[2]==0x18 && x1==8.0+now && io[4]==(uint64_t)(uintptr_t)mapped);
    memcpy(&io[3],&eight,8);ters_call(page,stranger,io);assert(io[0]==ters_ror30(0x5555) && *word==ters_ror30(0x5555)); /* native */
    munmap(page,GATE_PAGE);
    char out[256];assert(erng_status(out,sizeof(out))>0 && strstr(out," fallback=1 "));
    world_identity_installed=0;world_identity_count=0;free(world_identities);world_identities=NULL;
    printf("engine refresh rng PASS\n");
}
/* The real executable: pins, the word as found, the patch to the stub and back. */
static void test_engine_rng_elf(void) {
    uint64_t now;memcpy(&now,LX(LX_ERNG_SITE_VA),8);
    assert(now==LX_ERNG_SITE_ORIGINAL && erng_pins() && erng_lx_word_state()==0);
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    world_identity_installed=0;assert(!erng_arm() && !strcmp(erng_stage,"needs"));
    world_identity_installed=1;erng_page=NULL;
    assert(erng_arm() && erng_installed && erng_lx_word_state()==1 && erng_stub_ok() && erng_owned() && erng_check());
    assert(!memcmp(erng_page+43,LX(LX_ERNG_ADDSD_VA),LX_ERNG_ADDSD_LENGTH)); /* the replayed addsd is the image's */
    unsigned char *at=LX(LX_ERNG_SITE_VA);int32_t rel;memcpy(&rel,at+1,4);assert(at[0]==0xe9 && at+5+rel==erng_page);
    erng_restore();assert(!erng_installed && erng_restored && erng_lx_word_state()==0);
    memcpy(&now,LX(LX_ERNG_SITE_VA),8);assert(now==LX_ERNG_SITE_ORIGINAL);
    world_identity_installed=0;
    printf("engine refresh rng ELF PASS\n");
}

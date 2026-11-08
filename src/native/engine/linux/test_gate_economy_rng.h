/* Native instruction ABI canary. This runs only in the synthetic test binary. */
#include <fenv.h>
__attribute__((used)) static uint64_t economy_canary_registers[16];
__attribute__((used)) static uint64_t economy_canary_rsp[2];
__attribute__((used)) static uint64_t economy_canary_redzone[2];
__attribute__((used,aligned(64))) static unsigned char economy_canary_before[4096];
__attribute__((used,aligned(64))) static unsigned char economy_canary_after[4096];
__attribute__((used)) static uint32_t economy_canary_mask;
__attribute__((used)) static uint32_t economy_canary_mxcsr = 0x3f81;
__attribute__((used)) static uint32_t economy_canary_clobber_mxcsr = 0x1f80;
__attribute__((used)) static uint32_t economy_canary_mxcsr_before;
__attribute__((used,aligned(64))) static uint64_t economy_canary_vectors[32][8];
__attribute__((used)) static float economy_canary_range = 12.5f;
__attribute__((used)) static float economy_canary_scale = 0x1p-32f;
__attribute__((used)) static double economy_canary_base = 1024.25;
__attribute__((used)) static double economy_canary_due;
__attribute__((used)) static int economy_canary_direction;
__attribute__((used)) static void *economy_canary_stub;

/* Make the helper actively destroy caller-saved vector and x87 state. */
__attribute__((naked,noinline)) static void economy_rng_canary_clobber(void) {
    __asm__ volatile("vzeroall\n\t"
        "vpxord %zmm16,%zmm16,%zmm16\n\t"
        "vpxord %zmm17,%zmm17,%zmm17\n\t"
        "vpxord %zmm18,%zmm18,%zmm18\n\t"
        "vpxord %zmm19,%zmm19,%zmm19\n\t"
        "vpxord %zmm20,%zmm20,%zmm20\n\t"
        "vpxord %zmm21,%zmm21,%zmm21\n\t"
        "vpxord %zmm22,%zmm22,%zmm22\n\t"
        "vpxord %zmm23,%zmm23,%zmm23\n\t"
        "vpxord %zmm24,%zmm24,%zmm24\n\t"
        "vpxord %zmm25,%zmm25,%zmm25\n\t"
        "vpxord %zmm26,%zmm26,%zmm26\n\t"
        "vpxord %zmm27,%zmm27,%zmm27\n\t"
        "vpxord %zmm28,%zmm28,%zmm28\n\t"
        "vpxord %zmm29,%zmm29,%zmm29\n\t"
        "vpxord %zmm30,%zmm30,%zmm30\n\t"
        "vpxord %zmm31,%zmm31,%zmm31\n\t"
        "kxorq %k0,%k0,%k0\n\t"
        "kxorq %k1,%k1,%k1\n\t"
        "kxorq %k2,%k2,%k2\n\t"
        "kxorq %k3,%k3,%k3\n\t"
        "kxorq %k4,%k4,%k4\n\t"
        "kxorq %k5,%k5,%k5\n\t"
        "kxorq %k6,%k6,%k6\n\t"
        "kxorq %k7,%k7,%k7\n\t"
        "fld1\n\tldmxcsr economy_canary_clobber_mxcsr(%rip)\n\tret\n\t");
}

__asm__(
".pushsection .text\n"
".type economy_canary_enter,@function\n"
"economy_canary_enter:\n"
" push %rbp\n push %rbx\n push %r12\n push %r13\n push %r14\n push %r15\n"
" sub $24,%rsp\n mov %rdi,(%rsp)\n"
" mov %rsp,economy_canary_rsp(%rip)\n"
" movabs $0x123456789abcdef0,%rax\n mov %rax,-8(%rsp)\n mov %rax,-128(%rsp)\n"
" vmovdqu64 economy_canary_vectors+0(%rip),%zmm0\n"
" vmovdqu64 economy_canary_vectors+64(%rip),%zmm1\n"
" vmovdqu64 economy_canary_vectors+128(%rip),%zmm2\n"
" vmovdqu64 economy_canary_vectors+192(%rip),%zmm3\n"
" vmovdqu64 economy_canary_vectors+256(%rip),%zmm4\n"
" vmovdqu64 economy_canary_vectors+320(%rip),%zmm5\n"
" vmovdqu64 economy_canary_vectors+384(%rip),%zmm6\n"
" vmovdqu64 economy_canary_vectors+448(%rip),%zmm7\n"
" vmovdqu64 economy_canary_vectors+512(%rip),%zmm8\n"
" vmovdqu64 economy_canary_vectors+576(%rip),%zmm9\n"
" vmovdqu64 economy_canary_vectors+640(%rip),%zmm10\n"
" vmovdqu64 economy_canary_vectors+704(%rip),%zmm11\n"
" vmovdqu64 economy_canary_vectors+768(%rip),%zmm12\n"
" vmovdqu64 economy_canary_vectors+832(%rip),%zmm13\n"
" vmovdqu64 economy_canary_vectors+896(%rip),%zmm14\n"
" vmovdqu64 economy_canary_vectors+960(%rip),%zmm15\n"
" vmovdqu64 economy_canary_vectors+1024(%rip),%zmm16\n"
" vmovdqu64 economy_canary_vectors+1088(%rip),%zmm17\n"
" vmovdqu64 economy_canary_vectors+1152(%rip),%zmm18\n"
" vmovdqu64 economy_canary_vectors+1216(%rip),%zmm19\n"
" vmovdqu64 economy_canary_vectors+1280(%rip),%zmm20\n"
" vmovdqu64 economy_canary_vectors+1344(%rip),%zmm21\n"
" vmovdqu64 economy_canary_vectors+1408(%rip),%zmm22\n"
" vmovdqu64 economy_canary_vectors+1472(%rip),%zmm23\n"
" vmovdqu64 economy_canary_vectors+1536(%rip),%zmm24\n"
" vmovdqu64 economy_canary_vectors+1600(%rip),%zmm25\n"
" vmovdqu64 economy_canary_vectors+1664(%rip),%zmm26\n"
" vmovdqu64 economy_canary_vectors+1728(%rip),%zmm27\n"
" vmovdqu64 economy_canary_vectors+1792(%rip),%zmm28\n"
" vmovdqu64 economy_canary_vectors+1856(%rip),%zmm29\n"
" vmovdqu64 economy_canary_vectors+1920(%rip),%zmm30\n"
" vmovdqu64 economy_canary_vectors+1984(%rip),%zmm31\n"
" mov $4369,%rax\n kmovq %rax,%k0\n"
" mov $8738,%rax\n kmovq %rax,%k1\n"
" mov $13107,%rax\n kmovq %rax,%k2\n"
" mov $17476,%rax\n kmovq %rax,%k3\n"
" mov $21845,%rax\n kmovq %rax,%k4\n"
" mov $26214,%rax\n kmovq %rax,%k5\n"
" mov $30583,%rax\n kmovq %rax,%k6\n"
" mov $34952,%rax\n kmovq %rax,%k7\n"
" movss economy_canary_range(%rip),%xmm0\n movsd economy_canary_base(%rip),%xmm1\n"
" stmxcsr economy_canary_mxcsr_before(%rip)\n ldmxcsr economy_canary_mxcsr(%rip)\n fld1\n"
" mov economy_canary_mask(%rip),%eax\n xor %edx,%edx\n xsave64 economy_canary_before(%rip)\n"
" mov $0x101,%rax\n mov $0x102,%rbx\n mov $0x103,%rcx\n mov $0x104,%rdx\n"
" mov $0x105,%rbp\n mov $0x106,%rsi\n mov $0x107,%rdi\n mov $0x108,%r8\n"
" mov $0x109,%r9\n mov $0x10a,%r10\n mov $0x10b,%r11\n mov $0x10c,%r12\n"
" mov $0x10d,%r13\n mov $0x10e,%r14\n mov $0x10f,%r15\n"
" cmpl $0,economy_canary_direction(%rip)\n je 1f\n std\n jmp *economy_canary_stub(%rip)\n"
"1: cld\n jmp *economy_canary_stub(%rip)\n"
".type economy_canary_resume,@function\n"
"economy_canary_resume:\n"
" mov %rax,economy_canary_registers(%rip)\n mov %rbx,economy_canary_registers+8(%rip)\n"
" mov %rcx,economy_canary_registers+16(%rip)\n mov %rdx,economy_canary_registers+24(%rip)\n"
" mov %rbp,economy_canary_registers+32(%rip)\n mov %rsi,economy_canary_registers+40(%rip)\n"
" mov %rdi,economy_canary_registers+48(%rip)\n mov %r8,economy_canary_registers+56(%rip)\n"
" mov %r9,economy_canary_registers+64(%rip)\n mov %r10,economy_canary_registers+72(%rip)\n"
" mov %r11,economy_canary_registers+80(%rip)\n mov %r12,economy_canary_registers+88(%rip)\n"
" mov %r13,economy_canary_registers+96(%rip)\n mov %r14,economy_canary_registers+104(%rip)\n"
" mov %r15,economy_canary_registers+112(%rip)\n"
" mov -8(%rsp),%rax\n mov %rax,economy_canary_redzone(%rip)\n"
" mov -128(%rsp),%rax\n mov %rax,economy_canary_redzone+8(%rip)\n"
" pushfq\n pop economy_canary_registers+120(%rip)\n"
" cld\n"
" mov %rsp,economy_canary_rsp+8(%rip)\n"
" mov economy_canary_mask(%rip),%eax\n xor %edx,%edx\n xsave64 economy_canary_after(%rip)\n"
/* This is the pinned native continuation's arithmetic with an owned store. */
" mov economy_canary_registers(%rip),%eax\n cvtsi2ss %rax,%xmm2\n"
" mulss economy_canary_scale(%rip),%xmm2\n mulss %xmm2,%xmm0\n"
" cvtss2sd %xmm0,%xmm0\n subsd %xmm0,%xmm1\n movsd %xmm1,economy_canary_due(%rip)\n"
" fstp %st(0)\n ldmxcsr economy_canary_mxcsr_before(%rip)\n"
" add $24,%rsp\n pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbx\n pop %rbp\n ret\n"
".popsection\n");
void economy_canary_enter(void *object);
void economy_canary_resume(void);
static int economy_test_stop_started, economy_test_stop_done;
static void *economy_test_stop_thread(void *unused) {
    (void)unused;
    __atomic_store_n(&economy_test_stop_started, 1, __ATOMIC_RELEASE);
    economy_rng_restore();
    __atomic_store_n(&economy_test_stop_done, 1, __ATOMIC_RELEASE);
    return NULL;
}

static void test_economy_rng_ownership(void) {
    unsigned char *site = LX(ECONOMY_RNG_SITE);
    unsigned char *code_page = (unsigned char *)((uintptr_t)site & ~(uintptr_t)(GATE_PAGE - 1));
    unsigned __int128 original;
    memcpy(&original, site, sizeof(original));
    economy_rng_original = original;
    economy_rng_patched = original ^ ((unsigned __int128)1 << 64);
    economy_rng_page = mmap(NULL, GATE_PAGE, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(economy_rng_page != MAP_FAILED);
    economy_rng_write_stub(economy_rng_page);
    assert(!mprotect(economy_rng_page, GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(gate_code_swap128(site, original, economy_rng_patched));
    economy_rng_installed = economy_rng_enabled = economy_rng_ever_installed = 1;
    assert(economy_rng_owned());
    assert(!mprotect(economy_rng_page, GATE_PAGE, PROT_READ | PROT_WRITE));
    economy_rng_page[0] ^= 1;
    assert(!mprotect(economy_rng_page, GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(!economy_rng_owned());
    economy_rng_restore();
    assert(economy_rng_installed && !economy_rng_restored && !economy_rng_enabled);
    assert(!mprotect(economy_rng_page, GATE_PAGE, PROT_READ | PROT_WRITE));
    economy_rng_page[0] ^= 1;
    assert(!mprotect(economy_rng_page, GATE_PAGE, PROT_READ | PROT_EXEC));
    economy_rng_restore();
    assert(!economy_rng_installed && economy_rng_restored);
    assert(!economy_rng_arm() && !strcmp(economy_rng_stage, "one-shot"));
    unsigned __int128 restored;
    memcpy(&restored, site, sizeof(restored));
    assert(restored == original);
    assert(!mprotect(code_page, GATE_PAGE, PROT_READ | PROT_WRITE));
    assert(!munmap(economy_rng_page, GATE_PAGE));
    economy_rng_page = NULL;
    economy_rng_ever_installed = 0; /* This synthetic generation was unmapped. */
}

/* The ELF is copied into the test image; no game file is patched or run. */
static void test_economy_rng_elf(void) {
    reset_session();
    stats.active = 1;
    boot_mode = 1;
    boot_phase = 0;
    ui_thread = GetCurrentThreadId();
    world_identity_installed = 1;
    assert(economy_rng_pins());
    unsigned char original[16], patched[16];
    memcpy(original, LX(ECONOMY_RNG_SITE), sizeof(original));
    assert(economy_rng_arm() && economy_rng_owned() && economy_rng_check());
    memcpy(patched, LX(ECONOMY_RNG_SITE), sizeof(patched));
    assert(!memcmp(original, patched, 8) && patched[8] == 0xe9);
    assert(!memcmp(original + 13, patched + 13, 3));
    assert(economy_rng_pins());
    economy_rng_restore();
    assert(economy_rng_restored && !economy_rng_installed);
    assert(!memcmp(original, LX(ECONOMY_RNG_SITE), sizeof(original)));
    assert(!economy_rng_arm() && !strcmp(economy_rng_stage, "one-shot"));
    /* Isolated test generations have no threads executing the test image. */
    economy_rng_ever_installed = 0;
    economy_rng_test_swap_failure = 1; /* CAS happened; close result ambiguous. */
    assert(!economy_rng_arm() && economy_rng_installed && economy_rng_cleanup_uncertain);
    economy_rng_restore();
    assert(!economy_rng_installed && !economy_rng_restored && economy_rng_cleanup_uncertain);
    assert(!memcmp(original, LX(ECONOMY_RNG_SITE), sizeof(original)));
    economy_rng_ever_installed = economy_rng_cleanup_uncertain = 0;
    economy_rng_test_swap_failure = 2; /* No owned bytes; close is still unknown. */
    assert(!economy_rng_arm() && !economy_rng_installed && economy_rng_cleanup_uncertain);
    economy_rng_restore();
    assert(!economy_rng_restored && economy_rng_cleanup_uncertain);
    assert(!memcmp(original, LX(ECONOMY_RNG_SITE), sizeof(original)));
    economy_rng_cleanup_uncertain = 0; /* Only the copied test image was touched. */
    world_identity_installed = 0;
    printf("economy RNG copied ELF pins, patch and restore PASS\n");
}

static void test_economy_rng(void) {
    const uint32_t xsave_features = (1u << 26) | (1u << 27);
    assert(economy_rng_cpu_capable(13, xsave_features));
    assert(!economy_rng_cpu_capable(12, xsave_features));
    assert(!economy_rng_cpu_capable(13, 1u << 26));
    assert(!economy_rng_cpu_capable(13, 1u << 27));
    economy_rng_xcomponent components[8] = {{0}};
    components[2] = (economy_rng_xcomponent){256, 576, 0};
    components[5] = (economy_rng_xcomponent){64, 1088, 0};
    components[6] = (economy_rng_xcomponent){512, 1152, 0};
    components[7] = (economy_rng_xcomponent){1024, 1664, 0};
    assert(economy_rng_layout(0xe7, 0xe7, 2688, components));
    assert(!economy_rng_layout(0x400e7, 0x400e7, 2688, components));
    assert(!economy_rng_layout(0xe7, 7, 2688, components));
    assert(!economy_rng_layout(0xe7, 0xe7, 1024, components));
    assert(!economy_rng_layout(0x6, 0xe7, 2688, components));
    assert(!economy_rng_layout(0xe7, 0xe7, 4097, components));
    components[2].size = 0;
    assert(!economy_rng_layout(0xe7, 0xe7, 2688, components));
    components[2].size = 256;
    components[6].offset = components[5].offset;
    assert(!economy_rng_layout(0xe7, 0xe7, 2688, components));
    components[6].offset = 1152;
    components[6].flags = 1;
    assert(!economy_rng_layout(0xe7, 0xe7, 2688, components));
    economy_test_reset();
    stats.active = 1;
    boot_mode = 1;
    boot_phase = 1;
    assert(economy_rng_xstate());
    if (economy_rng_xstate_mask != 0xe7) {
        printf("economy RNG AVX-512 canary SKIP: host lacks selected state\n");
        return;
    }
    for (unsigned reg = 0; reg < 32; ++reg)
        for (unsigned lane = 0; lane < 8; ++lane)
            economy_canary_vectors[reg][lane] = UINT64_C(0x1234000000000000) | ((uint64_t)reg << 8) | lane;
    economy_canary_mask = economy_rng_xstate_mask;
    economy_rng_continuation = (uintptr_t)economy_canary_resume;
    economy_canary_stub = mmap(NULL, GATE_PAGE, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(economy_canary_stub != MAP_FAILED);
    unsigned char *synthetic_site = economy_canary_stub;
    int32_t displacement = 64 - 5;
    synthetic_site[0] = 0xe9;
    memcpy(synthetic_site + 1, &displacement, sizeof(displacement));
    economy_rng_write_stub(synthetic_site + 64);
    assert(!mprotect(economy_canary_stub, GATE_PAGE, PROT_READ | PROT_EXEC));
    memset(economy_canary_before, 0, sizeof(economy_canary_before));
    memset(economy_canary_after, 0, sizeof(economy_canary_after));
    uint64_t expected_seed = lockstep_seed(ECONOMY_RNG_DOMAIN, 0, 0x36fe9,
                                            UINT64_C(0x4070000000000000), 0, 0);
    uint64_t expected = expected_seed * UINT64_C(0x5851f42d4c957f2d) + UINT64_C(0x14057b7ef767814f);
    expected = (expected >> 30) | (expected << 34);
    economy_rng_keyed = economy_rng_fallback = economy_rng_failures = 0;
    economy_rng_enabled = 1;
    economy_rng_test_tls = UINT64_C(0x123456789abcdef0);
    economy_canary_direction = 0;
    economy_canary_enter(economy_test_station);
    assert(economy_rng_keyed == 1 && !economy_rng_fallback && !economy_rng_failures);
    assert(economy_rng_test_tls == UINT64_C(0x123456789abcdef0));
    assert(economy_canary_registers[0] == expected);
    assert(economy_canary_registers[3] == UINT64_C(0x14057b7ef767814f));
    assert(!(economy_canary_registers[15] & 0x400));
    int previous_rounding = fegetround();
    assert(!fesetround(FE_DOWNWARD));
    volatile uint32_t low_draw = (uint32_t)expected;
    volatile float converted = (float)low_draw;
    volatile float fraction = converted * 0x1p-32f;
    volatile float spread = fraction * economy_canary_range;
    volatile double due = economy_canary_base - (double)spread;
    assert(economy_canary_due == due);
    assert(!fesetround(previous_rounding));
    uint64_t reference_flags;
    __asm__ volatile("mov %[seed],%%rax; movabs $0x5851f42d4c957f2d,%%rcx;"
                     "imul %%rcx,%%rax; movabs $0x14057b7ef767814f,%%rdx;"
                     "add %%rdx,%%rax; ror $30,%%rax; pushfq; pop %[flags]"
                     : [flags] "=r"(reference_flags) : [seed] "r"(expected_seed)
                     : "rax", "rcx", "rdx", "cc");
    /* OF after a multi-bit ROR is undefined; compare the other arithmetic flags. */
    assert((economy_canary_registers[15] & 0xd5) == (reference_flags & 0xd5));

    for (unsigned i = 1; i < 15; ++i) {
        if (i != 3) assert(economy_canary_registers[i] == 0x101 + i);
    }
    assert(economy_canary_rsp[0] == economy_canary_rsp[1]);
    assert(economy_canary_redzone[0] == UINT64_C(0x123456789abcdef0));
    assert(economy_canary_redzone[1] == UINT64_C(0x123456789abcdef0));
    for (unsigned i = 0; i < 512; ++i) {
        if (i >= 0xc0 && i < 0xd0) {
            assert(economy_canary_after[i] == 0); /* Native PXOR clears XMM2. */
            continue;
        }
        assert(economy_canary_before[i] == economy_canary_after[i]);
    }
    assert(!memcmp(economy_canary_before + 576, economy_canary_after + 576, 1856));
    economy_canary_direction = 1;
    economy_canary_enter(economy_test_station);
    assert(economy_canary_registers[15] & 0x400);
    assert(economy_canary_registers[0] == expected);
    assert(economy_rng_test_tls == UINT64_C(0x123456789abcdef0));
    boot_phase = 0; /* Missing key uses the native stream and latches failure. */
    economy_canary_enter(economy_test_station);
    uint64_t fallback = UINT64_C(0x123456789abcdef0) * UINT64_C(0x5851f42d4c957f2d)
                        + UINT64_C(0x14057b7ef767814f);
    fallback = (fallback >> 30) | (fallback << 34);
    assert(economy_canary_registers[0] == fallback && economy_rng_test_tls == fallback);
    assert((economy_canary_registers[15] & 0x400) && economy_rng_fallback == 1 && economy_rng_failures == 1);
    boot_phase = 1;
    uint64_t absent_runtime = 101;
    memcpy(economy_test_station + LXW_OBJ_RUNTIME, &absent_runtime, sizeof(absent_runtime));
    economy_rng_failure_sample_state = 0;
    economy_canary_enter(economy_test_station);
    assert(economy_rng_failure_sample_state == 2 && economy_rng_failure_reason == 4);
    assert(economy_rng_failure_runtime == 101 && economy_rng_failure_phase == 1);
    assert(economy_rng_fallback == 2 && economy_rng_failures == 2);
    uint64_t runtime = 100;
    memcpy(economy_test_station + LXW_OBJ_RUNTIME, &runtime, sizeof(runtime));
    uint64_t last_a = UINT64_C(0x4070000000000000), last_b = UINT64_C(0x4080000000000000);
    economy_rng_key_xor = economy_rng_key_sum = economy_rng_draw_xor = economy_rng_draw_sum = 0;
    memcpy(economy_test_station + 0xa70, &last_a, 8);
    economy_canary_enter(economy_test_station);
    uint64_t draw_a = economy_canary_registers[0];
    memcpy(economy_test_station + 0xa70, &last_b, 8);
    economy_canary_enter(economy_test_station);
    assert(draw_a != economy_canary_registers[0]);
    uint64_t key_xor = economy_rng_key_xor, key_sum = economy_rng_key_sum;
    uint64_t draw_xor = economy_rng_draw_xor, draw_sum = economy_rng_draw_sum;
    economy_rng_key_xor = economy_rng_key_sum = economy_rng_draw_xor = economy_rng_draw_sum = 0;
    economy_canary_enter(economy_test_station);
    memcpy(economy_test_station + 0xa70, &last_a, 8);
    economy_canary_enter(economy_test_station);
    assert(key_xor == economy_rng_key_xor && key_sum == economy_rng_key_sum);
    assert(draw_xor == economy_rng_draw_xor && draw_sum == economy_rng_draw_sum);
    int allocated_imports = !lxw_inserts;
    if (allocated_imports) lxw_inserts = calloc(LXW_INSERTS_MAX, sizeof(*lxw_inserts));
    assert(lxw_inserts && lxw_insert_count == 0);
    unsigned char stored_node[0x30] = {0}, pending_node[0x30] = {0};
    memcpy(stored_node + 0x28, &runtime, sizeof(runtime));
    lxw_inserts[0] = (lxw_insert){0x36fe9, stored_node};
    lxw_inserts[1] = (lxw_insert){0x777, pending_node};
    lxw_insert_count = 2;
    world_identity_sealed = 0;
    economy_canary_enter(economy_test_station);
    assert(lxw_insert_count == 1 && lxw_inserts[0].node == pending_node);
    assert(lxw_identity_probe(runtime) == 0x36fe9);
    lxw_insert_count = 0;
    world_identity_sealed = 1;
    if (allocated_imports) {
        free(lxw_inserts);
        lxw_inserts = NULL;
    }
    errno = EDOM;
    economy_canary_enter((void *)1);
    assert(errno == EDOM && economy_rng_fallback == 3 && economy_rng_failures == 3);
    pthread_t stopper;
    economy_test_stop_started = economy_test_stop_done = 0;
    assert(!pthread_rwlock_rdlock(&economy_rng_readers));
    assert(!pthread_create(&stopper, NULL, economy_test_stop_thread, NULL));
    while (!__atomic_load_n(&economy_test_stop_started, __ATOMIC_ACQUIRE)) sched_yield();
    assert(!__atomic_load_n(&economy_test_stop_done, __ATOMIC_ACQUIRE));
    assert(!pthread_rwlock_unlock(&economy_rng_readers));
    assert(!pthread_join(stopper, NULL));
    assert(economy_test_stop_done && !economy_rng_enabled);
    economy_canary_enter(economy_test_station);
    assert(economy_rng_fallback == 4 && economy_rng_failures == 4);
    assert(!munmap(economy_canary_stub, GATE_PAGE));
    world_find = wt_find;
    world_reader = gate_read_local;
    test_economy_rng_ownership();
    printf("economy RNG private PCG and saved-state canary PASS\n");
}

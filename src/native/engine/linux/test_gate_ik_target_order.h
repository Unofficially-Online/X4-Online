/* Offline only. Uses the existing anonymous-ELF fixture/bridge helpers. The
 * controlled endpoint executes the original target store then returns; no
 * game entry, initializer or simulation is launched. Lifecycle cases are
 * isolated because trap identities and continuations remain permanent. */
typedef struct {
    uint32_t requested, current, n, pi, native, windows_order, windows_sample;
    unsigned tick, group, index;
} ik_order_vector;
#include "test_gate_ik_target_order_vectors.h"
__attribute__((used)) static uintptr_t iko_destination;
__attribute__((used)) static uint64_t iko_flags, iko_rax, iko_rdx, iko_r8, iko_r9, iko_redzone;
__attribute__((used)) static uintptr_t iko_rsp_before, iko_rsp_after;
__attribute__((used, aligned(16))) static unsigned char iko_fx[512];
__attribute__((used, aligned(16))) static uint32_t iko_n[4] = {0x4049c67fu, 0x11111111u, 0x22222222u, 0x33333333u};
__attribute__((used, aligned(16))) static uint32_t iko_pi[4] = {0x40490fdbu, 0x44444444u, 0x55555555u, 0x66666666u};
__attribute__((used, aligned(16))) static uint32_t iko_current[4] = {0x3ab1ed6bu, 0x77777777u, 0x88888888u, 0x99999999u};
__attribute__((used, aligned(16))) static uint32_t iko_other[4] = {1, 2, 3, 4};
__attribute__((used)) static uint32_t iko_mxcsr = 0x1f80u;
__attribute__((used)) static unsigned iko_store_count;
static unsigned char iko_store_original[32];
static int iko_fault_mode, iko_close_failure;
static int iko_barrier_entered, iko_barrier_release, iko_arm_result;
/* The marker at RSP-120 is inside the SysV 128-byte red zone. Signal delivery
 * is permitted to overwrite lower addresses before our stub starts. */
__asm__(
    ".pushsection .text\n"
    ".type iko_machine_run,@function\niko_machine_run:\n"
    " push %rbx\n sub $96,%rsp\n mov %rsp,iko_rsp_before(%rip)\n lea ikt_joint(%rip),%rbx\n"
    " movdqa iko_n(%rip),%xmm3\n movdqa iko_pi(%rip),%xmm6\n movdqa iko_current(%rip),%xmm7\n"
    " movdqa iko_other(%rip),%xmm2\n ldmxcsr iko_mxcsr(%rip)\n fld1\n"
    " movabs $0x123456789abcdef0,%rax\n mov %rax,-120(%rsp)\n"
    " movabs $0xabcddcba12344321,%rax\n movabs $0x9876678911223344,%rdx\n"
    " mov $0x108,%r8\n mov $0x109,%r9\n cmp %rax,%rax\n std\n jmp *iko_destination(%rip)\n"
    ".type iko_machine_resume,@function\niko_machine_resume:\n"
    " pushfq\n pop iko_flags(%rip)\n cld\n mov %rax,iko_rax(%rip)\n mov %rdx,iko_rdx(%rip)\n"
    " mov %r8,iko_r8(%rip)\n mov %r9,iko_r9(%rip)\n mov %rsp,iko_rsp_after(%rip)\n"
    " fxsave64 iko_fx(%rip)\n mov -120(%rsp),%rax\n mov %rax,iko_redzone(%rip)\n"
    " incl iko_store_count(%rip)\n add $96,%rsp\n pop %rbx\n ret\n"
    ".popsection\n");
void iko_machine_run(void);
void iko_machine_resume(void);
static uint32_t iko_word(const unsigned char *bytes) {
    uint32_t word;
    memcpy(&word, bytes, 4);
    return word;
}
static void iko_store_endpoint(int install) {
    unsigned char *at = LX(0x1565b3du);
    ikt_page(at, PROT_READ | PROT_WRITE);
    if (install) {
        memcpy(iko_store_original, at, sizeof(iko_store_original));
        const unsigned char prefix[] = {0xf3, 0x0f, 0x11, 0x43, 0x1c, 0xff, 0x25, 0, 0, 0, 0};
        memcpy(at, prefix, sizeof(prefix));
        uintptr_t resume = (uintptr_t)iko_machine_resume;
        memcpy(at + sizeof(prefix), &resume, 8);
        memset(at + sizeof(prefix) + 8, 0x90, sizeof(iko_store_original) - sizeof(prefix) - 8);
    } else memcpy(at, iko_store_original, sizeof(iko_store_original));
    ikt_page(at, PROT_READ | PROT_EXEC);
}
static uint32_t iko_run_vector(const ik_order_vector *vector) {
    float requested;
    memcpy(&requested, &vector->requested, 4);
    put32(ikt_joint + 8, vector->current);
    ((void (*)(void *, float))(void *)ikt_bridge)(ikt_joint, requested);
    return iko_word(ikt_joint + 0x1c);
}
static uint32_t iko_reference(uint32_t csr, int corrected, uint32_t *result, uint32_t *native_xmm3) {
    uint32_t after, low, joint_low;
    if (corrected) {
        __asm__ volatile("ldmxcsr %3; movdqa %4,%%xmm3; movdqa %5,%%xmm6; movdqa %6,%%xmm7; "
            "movaps %%xmm3,%%xmm0; subss %%xmm6,%%xmm3; addss %%xmm7,%%xmm0; subss %%xmm6,%%xmm0; "
            "stmxcsr %0; movd %%xmm0,%1; movd %%xmm3,%2"
            : "=m"(after), "=m"(low), "=m"(joint_low) : "m"(csr), "m"(iko_n), "m"(iko_pi), "m"(iko_current)
            : "xmm0", "xmm3", "xmm6", "xmm7", "memory");
    } else {
        __asm__ volatile("ldmxcsr %3; movdqa %4,%%xmm3; movdqa %5,%%xmm6; movdqa %6,%%xmm7; "
            "subss %%xmm6,%%xmm3; movaps %%xmm3,%%xmm0; addss %%xmm7,%%xmm0; "
            "stmxcsr %0; movd %%xmm0,%1; movd %%xmm3,%2"
            : "=m"(after), "=m"(low), "=m"(joint_low) : "m"(csr), "m"(iko_n), "m"(iko_pi), "m"(iko_current)
            : "xmm0", "xmm3", "xmm6", "xmm7", "memory");
    }
    *result = low;
    *native_xmm3 = joint_low;
    return after;
}
static void iko_machine_check(int corrected) {
    uint32_t saved_csr;
    __asm__ volatile("stmxcsr %0" : "=m"(saved_csr));
    uint32_t expected_low, expected_xmm3;
    uint32_t expected_csr = iko_reference(iko_mxcsr, corrected, &expected_low, &expected_xmm3);
    memset(iko_fx, 0, sizeof(iko_fx));
    iko_machine_run();
    __asm__ volatile("fstp %%st(0); ldmxcsr %0" :: "m"(saved_csr) : "memory");
    if (iko_rsp_before != iko_rsp_after || iko_redzone != UINT64_C(0x123456789abcdef0))
        fprintf(stderr, "machine stack failure: corrected=%d csr=%08x before=%llx after=%llx redzone=%016llx\n",
            corrected, iko_mxcsr, (unsigned long long)iko_rsp_before,
            (unsigned long long)iko_rsp_after, (unsigned long long)iko_redzone);
    assert(iko_rsp_before == iko_rsp_after && iko_redzone == UINT64_C(0x123456789abcdef0));
    assert(iko_rax == UINT64_C(0xabcddcba12344321) && iko_rdx == UINT64_C(0x9876678911223344));
    assert(iko_r8 == 0x108 && iko_r9 == 0x109);
    assert((iko_flags & 0xcd5u) == 0x444u); /* CF/PF/AF/ZF/SF/DF/OF after CMP+STD */
    assert(!memcmp(iko_fx + 160 + 2 * 16, iko_other, 16));
    assert(!memcmp(iko_fx + 160 + 6 * 16, iko_pi, 16));
    assert(!memcmp(iko_fx + 160 + 7 * 16, iko_current, 16));
    assert(!memcmp(iko_fx + 164, iko_n + 1, 12));
    assert(!memcmp(iko_fx + 160 + 3 * 16 + 4, iko_n + 1, 12));
    assert(iko_word(iko_fx + 160 + 3 * 16) == expected_xmm3);
    assert(iko_word(iko_fx + 160) == expected_low);
    assert(iko_word(iko_fx + 24) == expected_csr);
    const unsigned char one[10] = {0, 0, 0, 0, 0, 0, 0, 0x80, 0xff, 0x3f};
    assert(!memcmp(iko_fx + 32, one, sizeof(one)));
}
static void test_ik_order_corpus_and_canary(void) {
    ikt_prepare();
    ikt_bridge_prepare();
    const unsigned count = sizeof(ik_order_live_vectors) / sizeof(ik_order_live_vectors[0]);
    assert(count == 34);
    iko_store_endpoint(1);
    for (unsigned i = 0; i < count; i++) assert(iko_run_vector(&ik_order_live_vectors[i]) == ik_order_live_vectors[i].native);
    iko_destination = (uintptr_t)LX(IK_TARGET_ORDER_SITE);
    iko_machine_check(0);
    iko_store_endpoint(0);
    assert(ik_target_order_pins() && ik_target_order_arm());
    assert(ik_target_order_arm());
    assert(!ik_target_arm(10, 30) && !lxis_arm());
    iko_store_endpoint(1);
    unsigned sample_matches = 0, sample_differences = 0;
    const uint32_t observed_mxcsr = 0x1fbfu;
    __asm__ volatile("ldmxcsr %0" :: "m"(observed_mxcsr));
    for (unsigned i = 0; i < count; i++) {
        const ik_order_vector *vector = &ik_order_live_vectors[i];
        assert(iko_run_vector(vector) == vector->windows_order);
        if (vector->windows_sample == vector->windows_order) ++sample_matches;
        else ++sample_differences;
    }
    iko_machine_check(1);
    /* A deliberate sticky-flag difference: native(pi-pi)+small_normal is
     * exact, while (pi+small_normal)-pi adds precision/inexact naturally. */
    iko_n[0] = iko_pi[0];
    iko_current[0] = 0x00800000u;
    uint32_t low, joint_low;
    assert((iko_reference(0x1f80u, 0, &low, &joint_low) & 0x3fu) == 0);
    iko_machine_check(1);
    assert((iko_word(iko_fx + 24) & 0x3fu) == 0x20u && (ik_target_order_added_flags_or & 0x20u));
    iko_n[0] = 0x4049c67fu;
    iko_current[0] = 0x3ab1ed6bu;
    assert(ik_target_order_calls == count + 2 && ik_target_order_corrected == count + 2);
    assert(!ik_target_order_unsupported && !ik_target_order_active);
    assert(sample_matches == 29 && sample_differences == 5);
    assert(iko_store_count == 2 * count + 3);
    iko_store_endpoint(0);
    assert(ik_target_order_check());
    ik_target_order_restore();
    assert(ik_target_order_restored && !ik_target_order_installed && !ik_target_order_cleanup);
    assert(!ik_target_arm(10, 30) && !lxis_arm() && !ik_target_order_arm());
    /* Model a trap delivered after restoration using its permanent entry. */
    iko_store_endpoint(1);
    iko_destination = (uintptr_t)ik_target_order_entry;
    iko_machine_check(1);
    iko_store_endpoint(0);
    assert(ik_target_order_late == 1 && !ik_target_order_active && ik_target_order_corrected == count + 3);
    assert(ik_target_order_pins());
    puts("IK target order: all 34 actual Linux inputs/native outputs and Windows-order counterfactuals PASS; later 5 Windows sample differences retained");
    puts("IK target order: native XMM3/upper lanes, other XMM, scratch GPRs, flags/redzone/x87/MXCSR and late trap PASS");
}
static void test_ik_order_observer_collision(void) {
    ikt_prepare();
    assert(ik_target_arm(10, 30));
    assert(!ik_target_order_arm());
    ik_target_restore();
    assert(!ik_target_order_arm() && !strcmp(ik_target_order_stage, "conflict"));
    assert(!ik_target_order_ever_installed && !stock_trap_ik_order_ready);
    puts("IK target order: prior permanent observer identity refuses correction even after restore PASS");
}
static void test_ik_order_edge_operands(void) {
    /* Terminal-only synthetic operands. Some nonfinite/out-of-range N values
     * are not claimed reachable from the normalizer. NaN bits/flags are checked
     * against the actual three/four SSE operations on this CPU, without a
     * portable NaN-payload or cross-platform exception-equivalence claim. */
    static const uint32_t operands[][2] = {
        {0x00000000u, 0x00000000u},
        {0x80000000u, 0x80000000u},
        {0x40490fdbu, 0x80000000u},
        {0x40490fdbu, 0x00000000u},
        {0x40c90fdbu, 0xc0490fdbu},
        {0x00000000u, 0xc0490fdbu},
        {0xc0490fdbu, 0x40490fdbu},
        {0x7f7fffffu, 0x7f7fffffu},
        {0x7f7fffffu, 0xff7fffffu},
        {0x7f800000u, 0xff800000u},
        {0xff800000u, 0x7f800000u},
        {0x7fc12345u, 0x3f800000u},
        {0x3f800000u, 0x7fc54321u},
        {0x7f812345u, 0x00000000u}
    };
    const unsigned count = sizeof(operands) / sizeof(operands[0]);
    ikt_prepare();
    iko_destination = (uintptr_t)LX(IK_TARGET_ORDER_SITE);
    iko_store_endpoint(1);
    for (unsigned i = 0; i < count; i++) {
        iko_n[0] = operands[i][0];
        iko_current[0] = operands[i][1];
        iko_machine_check(0);
    }
    iko_store_endpoint(0);
    assert(ik_target_order_arm());
    iko_store_endpoint(1);
    for (unsigned i = 0; i < count; i++) {
        iko_n[0] = operands[i][0];
        iko_current[0] = operands[i][1];
        iko_machine_check(1);
    }
    iko_store_endpoint(0);
    assert(ik_target_order_calls == count && ik_target_order_corrected == count);
    assert(!ik_target_order_unsupported && !ik_target_order_active && iko_store_count == 2 * count);
    assert(ik_target_order_check());
    ik_target_order_restore();
    puts("IK target order: 14 terminal-only zero/negative/wrap/overflow/Inf/NaN operand pairs and exact CPU flags PASS");
}
static void test_ik_order_open_late_failure(void) {
    ikt_prepare();
    assert(ik_target_order_arm());
    world_identity_installed = 0;
    finish(1);
    assert(stats.reason == 1 && !ik_target_order_failure());
    iko_store_endpoint(1);
    iko_destination = (uintptr_t)ik_target_order_entry;
    iko_mxcsr = 0x3f80u;
    iko_machine_check(0); /* model an already delivered trap entering late */
    iko_store_endpoint(0);
    assert(stats.reason == 1 && ik_target_order_failure() && ik_target_order_late == 1);
    char status[768];
    assert(ik_target_order_status_text(status, sizeof(status)) && strstr(status, "failure=1 ") &&
        strstr(status, "quiescence=unproven"));
    puts("OPEN teardown limit reproduced: a late unsupported trap can follow recorded STOP 1; counters do not prove global arithmetic cleanliness");
}
static void test_ik_order_unsupported(void) {
    ikt_prepare();
    assert(ik_target_order_arm());
    iko_store_endpoint(1);
    iko_destination = (uintptr_t)LX(IK_TARGET_ORDER_SITE);
    const uint32_t controls[] = {0x3f80u, 0x5f80u, 0x7f80u, 0x1fc0u, 0x9f80u};
    for (unsigned i = 0; i < sizeof(controls) / sizeof(controls[0]); i++) {
        iko_mxcsr = controls[i];
        iko_machine_check(0);
    }
    /* Unmask each exception with exact finite operands, so native fallback
     * can be tested without deliberately generating a process-fatal SIGFPE. */
    iko_n[0] = iko_pi[0];
    iko_current[0] = 0;
    for (unsigned bit = 7; bit <= 12; bit++) {
        iko_mxcsr = 0x1f80u & ~(1u << bit);
        iko_machine_check(0);
    }
    assert(ik_target_order_calls == 11 && ik_target_order_unsupported == 11 && !ik_target_order_corrected);
    assert(ik_target_order_failure() && !ik_target_order_check());
    iko_store_endpoint(0);
    /* These callbacks were observed with no later admitted frame:
     * a normal requested STOP must still report the latched failure. */
    world_identity_installed = 0; /* synthetic prerequisite, not an installed import hook */
    finish(1);
    assert(stats.reason == IK_TARGET_ORDER_STOP_REASON && !ik_target_order_installed);
    char status[768];
    assert(ik_target_order_status_text(status, sizeof(status)) && strstr(status, "unsupported=11 ") && strstr(status, "failure=1 "));
    assert(ik_target_order_last_unsupported_mxcsr == 0x0f80u);
    assert(!ik_target_order_check());
    puts("IK target order: unsupported controls run native, latch failure and override normal final STOP 213 PASS");
}
static void iko_unsupported_during_arm(unsigned phase) {
    if (phase != 2) return;
    iko_store_endpoint(1);
    iko_destination = (uintptr_t)LX(IK_TARGET_ORDER_SITE);
    iko_mxcsr = 0x3f80u;
    iko_machine_check(0);
    iko_store_endpoint(0);
}
static void test_ik_order_unsupported_during_arm(void) {
    ikt_prepare();
    ik_target_order_test_arm_hook = iko_unsupported_during_arm;
    assert(!ik_target_order_arm());
    assert(ik_target_order_failure() && ik_target_order_unsupported == 1);
    assert(!ik_target_order_installed && !ik_target_order_cleanup && ik_target_order_restored);
    assert(!strcmp(ik_target_order_stage, "unsupported_controls") && !ik_target_order_check());
    puts("IK target order: observed unsupported callback during installation refuses arm and retains failure PASS");
}
static int iko_fault_swap(void *at, unsigned __int128 expected, unsigned __int128 desired) {
    int ok = stock_trap_swap(at, expected, desired);
    if (ok && desired == ik_target_order_patched && iko_fault_mode) {
        iko_fault_mode = 0;
        return 0;
    }
    return ok;
}
static int iko_fault_close(void *at) {
    if (iko_close_failure) {
        iko_close_failure = 0;
        return 0;
    }
    return gate_code_close(at);
}
static void test_ik_order_partial_foreign(void) {
    ikt_prepare();
    ik_target_order_swap = iko_fault_swap;
    ik_target_order_close = iko_fault_close;
    iko_fault_mode = iko_close_failure = 1;
    assert(!ik_target_order_arm());
    assert(ik_target_order_cleanup && !ik_target_order_restored);
    unsigned char *line = LX(IK_TARGET_ORDER_LINE);
    ikt_page(line, PROT_READ | PROT_WRITE);
    line[1] ^= 1;
    ikt_page(line, PROT_READ | PROT_EXEC);
    unsigned char foreign = line[1];
    ik_target_order_restore();
    assert(ik_target_order_cleanup && !ik_target_order_restored && line[1] == foreign);
    ikt_page(line, PROT_READ | PROT_WRITE);
    memcpy(line, &ik_target_order_original, 16);
    ikt_page(line, PROT_READ | PROT_EXEC);
    ik_target_order_restore();
    assert(!ik_target_order_cleanup && !ik_target_order_installed && ik_target_order_restored);
    assert(stock_trap_ik_order_ready && ik_target_order_continue && ik_target_order_pins());
    puts("IK target order: written-but-failed CAS, RX retry, foreign-byte preservation and permanent identity PASS");
}
static void iko_arm_barrier(unsigned phase) {
    if (phase != 2) return;
    __atomic_store_n(&iko_barrier_entered, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&iko_barrier_release, __ATOMIC_ACQUIRE)) sched_yield();
}
static void *iko_arm_thread(void *unused) {
    (void)unused;
    ui_thread = GetCurrentThreadId();
    iko_arm_result = ik_target_order_arm();
    return NULL;
}
static void *iko_stop_thread(void *unused) {
    (void)unused;
    ik_target_order_restore();
    return NULL;
}
static void test_ik_order_stop_race(void) {
    ikt_prepare();
    ik_target_order_test_arm_hook = iko_arm_barrier;
    pthread_t arm, stop;
    assert(!pthread_create(&arm, NULL, iko_arm_thread, NULL));
    while (!__atomic_load_n(&iko_barrier_entered, __ATOMIC_ACQUIRE)) sched_yield();
    assert(!pthread_create(&stop, NULL, iko_stop_thread, NULL));
    while (!__atomic_load_n(&ik_target_order_stopping, __ATOMIC_ACQUIRE)) sched_yield();
    __atomic_store_n(&iko_barrier_release, 1, __ATOMIC_RELEASE);
    assert(!pthread_join(arm, NULL) && !pthread_join(stop, NULL));
    assert(!iko_arm_result && !ik_target_order_installed && !ik_target_order_cleanup && ik_target_order_restored);
    assert(ik_target_order_pins() && !ik_target_order_calls);
    puts("IK target order: STOP during installation restores bytes without losing permanent dispatch PASS");
}
static void test_ik_order_refusals(void) {
    ikt_prepare();
    lxb_configured = 0;
    assert(!ik_target_order_arm());
    lxb_configured = 1;
    fs.state = FS_ARMED;
    assert(!ik_target_order_arm());
    fs.state = FS_OFF;
    cov_armed = 1;
    assert(!ik_target_order_arm());
    cov_armed = 0;
    lxis_installed = 1;
    assert(!ik_target_order_arm());
    lxis_installed = 0;
    ikt_page(LX(IK_TARGET_ORDER_TAU), PROT_READ | PROT_WRITE);
    put32(LX(IK_TARGET_ORDER_TAU), 0x40c90fdau);
    assert(!ik_target_order_arm());
    put32(LX(IK_TARGET_ORDER_TAU), 0x40c90fdbu);
    ikt_page(LX(0x1566673u), PROT_READ | PROT_WRITE);
    unsigned char saved = LX(0x1566673u)[0];
    LX(0x1566673u)[0] ^= 1;
    assert(!ik_target_order_arm());
    LX(0x1566673u)[0] = saved;
    assert(!ik_target_order_arm()); /* initial RX is required independently */
    ikt_page(LX(IK_TARGET_ORDER_SITE), PROT_READ | PROT_EXEC);
    assert(ik_target_order_arm());
    char status[768];
    ik_target_order_calls = ik_target_order_corrected = ik_target_order_late = UINT64_MAX;
    assert(ik_target_order_status_text(status, sizeof(status)) && strstr(status, "late=18446744073709551615 "));
    ik_target_order_restore();
    puts("IK target order: manifest/fork/coverage/ikstep/full pin/constants/RX refusals and maximum status PASS");
}
static void test_ik_target_order_elf(void) {
    test_stock_observer_isolated(test_ik_order_corpus_and_canary);
    test_stock_observer_isolated(test_ik_order_observer_collision);
    test_stock_observer_isolated(test_ik_order_edge_operands);
    test_stock_observer_isolated(test_ik_order_open_late_failure);
    test_stock_observer_isolated(test_ik_order_unsupported);
    test_stock_observer_isolated(test_ik_order_unsupported_during_arm);
    test_stock_observer_isolated(test_ik_order_partial_foreign);
    test_stock_observer_isolated(test_ik_order_stop_race);
    test_stock_observer_isolated(test_ik_order_refusals);
}

/* Offline only. Execute the pinned angular branch in an anonymous ELF copy,
 * with a controlled return after its ORIGINAL target store. No game entry,
 * initializer, world simulation or live process is executed. Each lifecycle
 * case has its own child: permanent trap identities are never reused. */
static unsigned char ikt_root[0x300], ikt_object[0x300], ikt_container[0x50], ikt_skeleton[0x30];
static unsigned char ikt_joint[LXIK_JOINT] __attribute__((aligned(16)));
static uintptr_t ikt_groups[3], ikt_vtable[8], ikt_map;
static unsigned char ikt_store_original[32];
static unsigned char *ikt_bridge;
static unsigned ikt_store_calls;
static int ikt_swap_failure, ikt_close_failure, ikt_stop_stage;
static int ikt_barrier_entered, ikt_barrier_release, ikt_arm_result;
__attribute__((used)) static uintptr_t ikt_rsp_before, ikt_rsp_after;
__attribute__((used)) static uint64_t ikt_flags_after, ikt_r8_after, ikt_r9_after, ikt_redzone_after;
__attribute__((used)) static uint32_t ikt_result_after, ikt_mxcsr_after, ikt_x87_after;
__attribute__((used)) static const uint32_t ikt_mxcsr_initial = 0x1f80u;
__attribute__((used)) static const uint32_t ikt_n = 0x4049c67fu, ikt_pi = 0x40490fdbu, ikt_current = 0x3ab1ed6bu;
__attribute__((used, aligned(16))) static unsigned char ikt_xmm_before[16] = {1, 2, 3, 4, 5, 6, 7, 8};
__attribute__((used, aligned(16))) static unsigned char ikt_xmm_after[16];
__asm__(
    ".pushsection .text\n"
    ".type ikt_machine_run,@function\n"
    "ikt_machine_run:\n"
    " push %rbx\n sub $16,%rsp\n mov %rsp,ikt_rsp_before(%rip)\n"
    " lea ikt_joint(%rip),%rbx\n movabs $0x123456789abcdef0,%rax\n mov %rax,-136(%rsp)\n"
    " mov $0x108,%r8\n mov $0x109,%r9\n movdqu ikt_xmm_before(%rip),%xmm2\n"
    " movss ikt_n(%rip),%xmm3\n movss ikt_pi(%rip),%xmm6\n movss ikt_current(%rip),%xmm7\n"
    " ldmxcsr ikt_mxcsr_initial(%rip)\n fld1\n xor %eax,%eax\n std\n call ik_target_terminal\n"
    " pushfq\n pop ikt_flags_after(%rip)\n cld\n fstps ikt_x87_after(%rip)\n"
    " mov %r8,ikt_r8_after(%rip)\n mov %r9,ikt_r9_after(%rip)\n"
    " mov -136(%rsp),%rax\n mov %rax,ikt_redzone_after(%rip)\n mov %rsp,ikt_rsp_after(%rip)\n"
    " movdqu %xmm2,ikt_xmm_after(%rip)\n movd %xmm0,ikt_result_after(%rip)\n stmxcsr ikt_mxcsr_after(%rip)\n"
    " add $16,%rsp\n pop %rbx\n ret\n"
    ".type ikt_machine_resume,@function\nikt_machine_resume:\n ret\n"
    ".popsection\n");
void ikt_machine_run(void);
void ikt_machine_resume(void);
static void ikt_machine_state(void) {
    assert(economy_rng_xstate());
    ik_target_xstate_mask = economy_rng_xstate_mask;
    ik_target_terminal_continue = (uintptr_t)ikt_machine_resume;
    uint32_t original_mxcsr, expected_mxcsr;
    __asm__ volatile("stmxcsr %0" : "=m"(original_mxcsr));
    __asm__ volatile("ldmxcsr %1; movss %2,%%xmm3; movss %3,%%xmm6; movss %4,%%xmm7; "
        "subss %%xmm6,%%xmm3; movaps %%xmm3,%%xmm0; addss %%xmm7,%%xmm0; stmxcsr %0"
        : "=m"(expected_mxcsr) : "m"(ikt_mxcsr_initial), "m"(ikt_n), "m"(ikt_pi), "m"(ikt_current)
        : "xmm0", "xmm3", "xmm6", "xmm7", "memory");
    ik_target_test_clobber = 1;
    ikt_machine_run();
    ik_target_test_clobber = 0;
    __asm__ volatile("ldmxcsr %0" :: "m"(original_mxcsr));
    assert(ikt_rsp_before == ikt_rsp_after && ikt_r8_after == 0x108 && ikt_r9_after == 0x109);
    assert((ikt_flags_after & 0x440u) == 0x440u && ikt_redzone_after == UINT64_C(0x123456789abcdef0));
    assert(!memcmp(ikt_xmm_before, ikt_xmm_after, 16));
    assert(ikt_result_after == 0x3c4ce1adu && ikt_mxcsr_after == expected_mxcsr && ikt_x87_after == 0x3f800000u);
    ik_target_terminal_continue = 0;
}
static unsigned char *ikt_find(void *map, uint64_t runtime, unsigned type, unsigned flags) {
    (void)map;
    (void)type;
    (void)flags;
    return runtime == 111 ? ikt_root : runtime == 222 ? ikt_object : NULL;
}
static void ikt_page(void *address, int protection) {
    uintptr_t page = (uintptr_t)address & ~(uintptr_t)(GATE_PAGE - 1);
    assert(!mprotect((void *)page, GATE_PAGE, protection));
}
static void ikt_prepare(void) {
    reset_session();
    /* Mirror x4gate_start: earlier frame-gate tests can have stopped these
     * unused diagnostics. Their begin functions retain the one-shot guard
     * whenever permanent trap identities have already been published. */
    ik_target_begin();
    ik_target_order_begin();
    stats.active = 1;
    boot_mode = 1;
    ui_thread = GetCurrentThreadId();
    gate_env.file = 1;
    lxb_configured = 1;
    lxb_bound = LXB_ACTORS;
    world_identity_installed = 1;
    memset(lxb_actors, 0, sizeof(lxb_actors));
    lxb_actors[5].object = ikt_root;
    lxb_actors[5].runtime = 111;
    lxb_actors[5].enrolled = 1;
    put64(ikt_root + 8, 111);
    put64(ikt_object + 8, 222);
    put64(ikt_object + 0x70, (uintptr_t)ikt_root);
    put64(ikt_object + LXIK_CONTAINER, (uintptr_t)ikt_container);
    put64(ikt_container + LXIK_SKELETON, (uintptr_t)ikt_skeleton);
    put64(ikt_skeleton + LXIK_GROUP_VEC, (uintptr_t)ikt_groups);
    put64(ikt_skeleton + LXIK_GROUP_VEC + 8, (uintptr_t)(ikt_groups + 3));
    ikt_groups[0] = (uintptr_t)ikt_joint;
    ikt_groups[1] = (uintptr_t)(ikt_joint + LXIK_JOINT);
    ikt_groups[2] = ikt_groups[1];
    put64(ikt_joint, (uintptr_t)ikt_vtable);
    put32(ikt_joint + 8, 0x3ab1ed6bu);
    put64(ikt_joint + 0x10, UINT64_C(0x401e11111111110f));
    put32(ikt_joint + 0x18, 0x3d26ce94u);
    put32(ikt_joint + 0x1c, 0x3c4e1c00u);
    ikt_joint[0x21] = 1;
    world_find = (lxw_find_fn)(void *)ikt_find;
    ikt_map = 1;
    world_component_map = (void **)&ikt_map;
    LX(IK_TARGET_BACKEND)[0] = 0;
    ikt_page(LX(IK_TARGET_ENTRY_SITE), PROT_READ | PROT_EXEC);
    ikt_page(LX(IK_TARGET_FUNCTION), PROT_READ | PROT_EXEC);
    assert(ik_target_pins());
}
static void ikt_bind(void) {
    stats.issued = 1;
    assert(ik_target_bind(6, 1, 222));
    assert(ik_target_bind(6, 1, 222));
    assert(ik_target_joint_count == 1 && ik_target_object_count == 1);
    assert(!ik_target_bind(6, 2, 222));
    assert(!ik_target_bind(5, 1, 222));
    assert(!ik_target_bind(6, 0, 222));
    put64(ikt_object + 8, 333);
    assert(!ik_target_bind(6, 2, 222));
    put64(ikt_object + 8, 222);
    put64(ikt_object + 0x70, (uintptr_t)ikt_object);
    assert(!ik_target_bind(6, 2, 222));
    put64(ikt_object + 0x70, (uintptr_t)ikt_root);
    stats.issued = 15;
}
static void ikt_store_endpoint(int install) {
    unsigned char *at = LX(0x1565b3du);
    ikt_page(at, PROT_READ | PROT_WRITE);
    if (install) {
        memcpy(ikt_store_original, at, sizeof(ikt_store_original));
        unsigned char code[32] = {0xf3, 0x0f, 0x11, 0x43, 0x1c, 0x48, 0xb8};
        uintptr_t count = (uintptr_t)&ikt_store_calls;
        memcpy(code + 7, &count, 8);
        const unsigned char tail[] = {0xff, 0x00, 0x48, 0x83, 0xc4, 0x60, 0x5b, 0xc3};
        memcpy(code + 15, tail, sizeof(tail));
        memset(code + 23, 0x90, sizeof(code) - 23);
        memcpy(at, code, sizeof(code));
    } else memcpy(at, ikt_store_original, sizeof(ikt_store_original));
    ikt_page(at, PROT_READ | PROT_EXEC);
}
static void ikt_bridge_prepare(void) {
    ikt_bridge = gate_near_page(LX(IK_TARGET_ENTRY_SITE));
    assert(ikt_bridge);
    /* A jump, not CALL into the middle of the function: its [RSP] scratch
     * must not overwrite a return address. The controlled store endpoint
     * releases these96bytes and savedRBX before returning to the test. */
    unsigned char code[] = {0x53, 0x48, 0x83, 0xec, 0x60, 0x48, 0x89, 0xfb,
        0xe9, 0, 0, 0, 0};
    intptr_t distance = (intptr_t)LX(IK_TARGET_ENTRY_SITE) - (intptr_t)(ikt_bridge + sizeof(code));
    assert(distance >= INT32_MIN && distance <= INT32_MAX);
    int32_t relative = (int32_t)distance;
    memcpy(code + 9, &relative, 4);
    memcpy(ikt_bridge, code, sizeof(code));
    assert(!mprotect(ikt_bridge, GATE_PAGE, PROT_READ | PROT_EXEC));
}
static uint32_t ikt_run(uint32_t angle) {
    float requested;
    memcpy(&requested, &angle, 4);
    put32(ikt_joint + 8, 0x3ab1ed6bu);
    ((void (*)(void *, float))(void *)ikt_bridge)(ikt_joint, requested);
    uint32_t result;
    memcpy(&result, ikt_joint + 0x1c, 4);
    return result;
}
static void ikt_fake_state(uint64_t saved[16], unsigned char state[4096], uintptr_t joint, uint32_t requested) {
    memset(saved, 0, 16 * sizeof(uint64_t));
    memset(state, 0, 4096);
    saved[13] = joint;
    put32(state + 160, requested);
    put32(state + 160 + 6 * 16, 0x40490fdbu);
    put32(state + 160 + 7 * 16, 0x3ab1ed6bu);
    put32(state + 24, 0x1f80u);
}
static void ikt_pairing(void) {
    uint64_t frames[IK_TARGET_DEPTH + 1][16];
    unsigned char state[4096] __attribute__((aligned(64)));
    uint64_t first = ik_target_count;
    for (unsigned i = 0; i <= IK_TARGET_DEPTH; i++) {
        ikt_fake_state(frames[i], state, (uintptr_t)ikt_joint, 0x3c4ce1adu + i);
        ik_target_observe(frames[i], state, 0, 0);
    }
    assert(ik_target_pending_count == IK_TARGET_DEPTH && ik_target_suppressed_depth == 1);
    ik_target_observe(frames[IK_TARGET_DEPTH], state, 1, 0x4049c67fu);
    assert(!ik_target_suppressed_depth && ik_target_pending_count == IK_TARGET_DEPTH);
    for (unsigned i = IK_TARGET_DEPTH; i-- > 0;) ik_target_observe(frames[i], state, 1, 0x4049c67fu);
    assert(!ik_target_pending_count);
    for (unsigned i = 0; i < IK_TARGET_DEPTH; i++) {
        assert(ik_target_rows[first + i].ready && ik_target_rows[first + i].entry_known);
        assert(ik_target_rows[first + i].requested == 0x3c4ce1adu + i);
    }
    first = ik_target_count;
    ikt_fake_state(frames[0], state, (uintptr_t)ikt_joint, 1);
    ik_target_observe(frames[0], state, 0, 0);
    put32(state + 160, 2);
    ik_target_observe(frames[0], state, 0, 0);
    ik_target_observe(frames[0], state, 1, 3);
    assert(!ik_target_rows[first].ready && ik_target_rows[first + 1].ready);
    assert(ik_target_rows[first + 1].requested == 2 && ik_target_unmatched);
    first = ik_target_count;
    ik_target_observe(frames[1], state, 1, 3);
    assert(ik_target_rows[first].ready && !ik_target_rows[first].entry_known);
    ik_target_in_callback = 1;
    uint64_t reentry = ik_target_reentry;
    ik_target_observe(frames[0], state, 0, 0);
    ik_target_in_callback = 0;
    assert(ik_target_reentry == reentry + 1);
}
static void *ikt_worker(void *unused) {
    (void)unused;
    uint64_t frame[16];
    unsigned char state[4096] __attribute__((aligned(64)));
    ikt_fake_state(frame, state, (uintptr_t)ikt_joint, 7);
    ik_target_observe(frame, state, 0, 0);
    ik_target_observe(frame, state, 1, 8);
    return NULL;
}
static void test_ik_target_functional(void) {
    ikt_prepare();
    ikt_machine_state();
    ikt_bridge_prepare();
    uint32_t angles[64], baseline[64];
    angles[0] = 0x3c4ce1adu; /* Synthetic witness, NOT a retained live requested input. */
    for (unsigned i = 1; i < 64; i++) angles[i] = 0x3a000000u + i * 0x100013u;
    const uint32_t edges[] = {0xbc4ce1adu, 0xc0c90fdbu, 0x40c90fdbu, 0x40490fdbu,
        0xc0490fdbu, 0x7fc00000u, 0x7f800000u};
    memcpy(angles + 1, edges, sizeof(edges));
    /* Only the copied image's fmodf PLT is redirected to the host's pure math
     * function, so the pinned NaN fallbacks can run without a dynamic loader. */
    unsigned char plt_original[14];
    unsigned char *plt = LX(0x659220u);
    memcpy(plt_original, plt, sizeof(plt_original));
    ikt_page(plt, PROT_READ | PROT_WRITE);
    const unsigned char jump[6] = {0xff, 0x25, 0, 0, 0, 0};
    memcpy(plt, jump, sizeof(jump));
    void *native_fmodf = (void *)fmodf;
    memcpy(plt + 6, &native_fmodf, 8);
    ikt_page(plt, PROT_READ | PROT_EXEC);
    ikt_store_endpoint(1);
    for (unsigned i = 0; i < 64; i++) baseline[i] = ikt_run(angles[i]);
    ikt_store_endpoint(0);
    assert(ik_target_pins() && ikt_store_calls == 64);
    assert(ik_target_arm(10, 30));
    ikt_bind();
    stats.active = 0;
    assert(!ik_target_arm(10, 30));
    stats.active = 1;
    assert(ik_target_arm(10, 30)); /* Reload never resets samples or bindings. */
    assert(!ik_target_arm(11, 30));
    assert(!lxis_arm());
    ik_target_test_clobber = 1;
    ikt_store_endpoint(1);
    for (unsigned i = 0; i < 64; i++) {
        assert(ikt_run(angles[i]) == baseline[i]);
        const ik_target_row *row = &ik_target_rows[i];
        assert(row->ready && row->entry_known && row->requested == angles[i]);
        assert(row->entry_current == 0x3ab1ed6bu && row->angular == 1 && !row->backend);
        assert(row->native_result_before_store == baseline[i] && row->pi == 0x40490fdbu);
        assert(row->terminal_current == 0x3ab1ed6bu && row->actor == 6 && row->weapon == 1);
    }
    ikt_store_endpoint(0);
    ik_target_test_clobber = 0;
    assert(ikt_store_calls == 128 && ik_target_check());
    ikt_page(plt, PROT_READ | PROT_WRITE);
    memcpy(plt, plt_original, sizeof(plt_original));
    ikt_page(plt, PROT_READ | PROT_EXEC);
    assert(ik_target_rows[0].terminal_n == 0x4049c67fu);
    assert(ik_target_rows[0].native_result_before_store == 0x3c4ce1adu);
    ikt_pairing();
    uint64_t reserved = ik_target_count;
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, ikt_worker, NULL));
    assert(!pthread_join(worker, NULL));
    assert(ik_target_rows[reserved].ready && !ik_target_rows[reserved].owner_context && ik_target_foreign_thread >= 2);
    uint64_t frame[16];
    unsigned char state[4096] __attribute__((aligned(64)));
    ikt_fake_state(frame, state, (uintptr_t)ikt_joint, 0x12345678u);
    reserved = ik_target_count;
    ik_target_observe(frame, state, 0, 0);
    assert(!ik_target_rows[reserved].ready);
    ik_target_restore();
    assert(ik_target_restored && !ik_target_installed && !ik_target_cleanup && ik_target_pins());
    char status[768];
    assert(ik_target_status_text(status, sizeof(status)) && strstr(status, "pending=2 "));
    ik_target_observe(frame, state, 1, 0x87654321u);
    assert(ik_target_rows[reserved].ready && ik_target_rows[reserved].late && ik_target_late_ready == 1);
    assert(!ik_target_arm(10, 30));
    assert(ik_target_row_text((unsigned)reserved, status, sizeof(status)) && strstr(status, "native_result_before_store="));
    puts("IK target: copied native arithmetic/store64vectors, fixture binding, pairing, worker and late STOP PASS");
}
static int ikt_fault_swap(void *site, unsigned __int128 expected, unsigned __int128 desired) {
    int ok = stock_trap_swap(site, expected, desired);
    for (unsigned index = 0; index < 2; index++) {
        if (ok && site == ik_target_line(index) && desired == ik_target_patched[index]) {
            assert(!ik_target_enabled); /* No rows during a partial installation. */
            if (ikt_swap_failure != (int)index + 1) continue;
            ikt_swap_failure = 0;
            return 0; /* CAS succeeded, caller only sees failure. */
        }
    }
    return ok;
}
static void test_ik_target_second_site_failure(void) {
    ikt_prepare();
    ik_target_swap = ikt_fault_swap;
    ikt_swap_failure = 2;
    assert(!ik_target_arm(10, 30));
    assert(!ik_target_enabled && !ik_target_installed && !ik_target_cleanup && ik_target_restored);
    assert(ik_target_pins() && !ik_target_count);
    puts("IK target: second-site write reported as failure restores both owned bytes PASS");
}
static void test_ik_target_foreign(void) {
    ikt_prepare();
    assert(ik_target_arm(10, 30));
    unsigned char *line = ik_target_line(1);
    ikt_page(line, PROT_READ | PROT_WRITE);
    line[1] ^= 1;
    ikt_page(line, PROT_READ | PROT_EXEC);
    assert(!ik_target_check());
    ik_target_restore();
    assert(ik_target_cleanup == 2 && !ik_target_restored && line[13] == 0xcc);
    assert(line[1] != ((unsigned char *)&ik_target_original[1])[1]);
    /* Test-only recovery after proving the foreign line was not overwritten. */
    ikt_page(line, PROT_READ | PROT_WRITE);
    memcpy(line, &ik_target_original[1], 16);
    ikt_page(line, PROT_READ | PROT_EXEC);
    ik_target_restore();
    assert(!ik_target_cleanup && !ik_target_installed && ik_target_restored && ik_target_pins());
    puts("IK target: foreign adjacent byte is retained and cleanup remains uncertain PASS");
}
static void test_ik_target_limits(void) {
    ikt_prepare();
    assert(ik_target_arm(10, 30));
    ikt_bind();
    uint64_t frame[16];
    unsigned char state[4096] __attribute__((aligned(64)));
    ikt_fake_state(frame, state, (uintptr_t)ikt_joint + 1, 1);
    ik_target_observe(frame, state, 0, 0);
    assert(ik_target_unknown == 1 && !ik_target_count);
    put64(ikt_object + 8, 333);
    ikt_fake_state(frame, state, (uintptr_t)ikt_joint, 1);
    ik_target_observe(frame, state, 0, 0);
    assert(ik_target_unknown == 2 && !ik_target_count);
    put64(ikt_object + 8, 222);
    unsigned char *pages = mmap(NULL, GATE_PAGE * 2, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(pages != MAP_FAILED);
    assert(!mprotect(pages + GATE_PAGE, GATE_PAGE, PROT_NONE));
    uintptr_t edge = (uintptr_t)(pages + GATE_PAGE - 8);
    put64((void *)edge, (uintptr_t)ikt_vtable);
    ik_target_bindings[1] = ik_target_bindings[0];
    ik_target_bindings[1].joint = edge;
    __atomic_store_n(&ik_target_joint_count, 2, __ATOMIC_RELEASE);
    ikt_fake_state(frame, state, edge, 1);
    ik_target_observe(frame, state, 0, 0);
    ik_target_observe(frame, state, 1, 2);
    assert(ik_target_rows[0].ready && !ik_target_rows[0].entry_known && ik_target_read_failed == 1);
    assert(!munmap(pages, GATE_PAGE * 2));
    ik_target_count = IK_TARGET_ROWS;
    ikt_fake_state(frame, state, (uintptr_t)ikt_joint, 1);
    ik_target_observe(frame, state, 0, 0);
    ik_target_observe(frame, state, 1, 2);
    assert(ik_target_overflow == 2 && !ik_target_pending_count);
    char text[768];
    assert(!ik_target_row_text(IK_TARGET_ROWS, text, sizeof(text)));
    ik_target_count = ik_target_ready_count = ik_target_overflow = UINT64_MAX;
    ik_target_unknown = ik_target_reentry = ik_target_read_failed = UINT64_MAX;
    ik_target_unmatched = ik_target_late_ready = ik_target_outside = ik_target_foreign_thread = UINT64_MAX;
    assert(ik_target_status_text(text, sizeof(text)) && strstr(text, "foreign_thread=18446744073709551615"));
    ik_target_restore();
    puts("IK target: unknown/runtime mismatch, unreadable entry, row overflow and maximum status PASS");
}
static int ikt_fault_close(void *site) {
    if (ikt_close_failure) {
        ikt_close_failure = 0;
        return 0;
    }
    return gate_code_close(site);
}
static void test_ik_target_partial(void) {
    ikt_prepare();
    ik_target_swap = ikt_fault_swap;
    ik_target_close = ikt_fault_close;
    ikt_swap_failure = ikt_close_failure = 1;
    assert(!ik_target_arm(10, 30));
    assert(!ik_target_enabled && ik_target_cleanup && !ik_target_restored);
    ik_target_restore();
    assert(!ik_target_cleanup && !ik_target_installed && ik_target_restored && ik_target_pins());
    assert(stock_trap_ik_entry_ready && stock_trap_ik_terminal_ready && ik_target_entry_replay && ik_target_terminal_continue);
    assert(!ik_target_arm(10, 30));
    puts("IK target: partial CAS, independent RX retry and permanent late identities PASS");
}
static void ikt_arm_barrier(unsigned stage) {
    if ((int)stage != ikt_stop_stage) return;
    __atomic_store_n(&ikt_barrier_entered, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&ikt_barrier_release, __ATOMIC_ACQUIRE)) sched_yield();
}
static void *ikt_arm_thread(void *unused) {
    (void)unused;
    ui_thread = GetCurrentThreadId();
    ikt_arm_result = ik_target_arm(10, 30);
    return NULL;
}
static void *ikt_stop_thread(void *unused) {
    (void)unused;
    ik_target_restore();
    return NULL;
}
static void test_ik_target_stop_race(void) {
    ikt_prepare();
    ikt_stop_stage = 2;
    ik_target_test_arm_hook = ikt_arm_barrier;
    pthread_t arm, stop;
    assert(!pthread_create(&arm, NULL, ikt_arm_thread, NULL));
    while (!__atomic_load_n(&ikt_barrier_entered, __ATOMIC_ACQUIRE)) sched_yield();
    assert(!ik_target_enabled && ik_target_installed == 1);
    assert(!pthread_create(&stop, NULL, ikt_stop_thread, NULL));
    while (!__atomic_load_n(&ik_target_stopping, __ATOMIC_ACQUIRE)) sched_yield();
    __atomic_store_n(&ikt_barrier_release, 1, __ATOMIC_RELEASE);
    assert(!pthread_join(arm, NULL) && !pthread_join(stop, NULL));
    assert(!ikt_arm_result && !ik_target_enabled && !ik_target_installed && !ik_target_cleanup && ik_target_restored);
    assert(!ik_target_count && ik_target_pins());
    puts("IK target: STOP racing between trap installations PASS");
}
static void test_ik_target_refusals(void) {
    ikt_prepare();
    assert(!ik_target_arm(0, 30) && !ik_target_arm(10, 70));
    lxb_configured = 0;
    assert(!ik_target_arm(10, 30));
    lxb_configured = 1;
    cov_armed = 1;
    assert(!ik_target_arm(10, 30));
    cov_armed = 0;
    ikt_page(LX(IK_TARGET_TAU), PROT_READ | PROT_WRITE);
    put32(LX(IK_TARGET_TAU), 0x40c90fdau);
    assert(!ik_target_arm(10, 30));
    put32(LX(IK_TARGET_TAU), 0x40c90fdbu);
    ikt_page(LX(0x1566673u), PROT_READ | PROT_WRITE);
    unsigned char saved = LX(0x1566673u)[0];
    LX(0x1566673u)[0] ^= 1;
    assert(!ik_target_arm(10, 30));
    LX(0x1566673u)[0] = saved;
    assert(!ik_target_arm(10, 30) && !strcmp(ik_target_stage, "protection"));
    ikt_page(LX(IK_TARGET_ENTRY_SITE), PROT_READ | PROT_EXEC);
    struct sigaction foreign = {0};
    foreign.sa_handler = stock_test_foreign_trap;
    sigemptyset(&foreign.sa_mask);
    struct sigaction original;
    assert(!sigaction(SIGTRAP, &foreign, &original));
    assert(!ik_target_arm(10, 30));
    assert(!sigaction(SIGTRAP, &original, NULL));
    sigset_t blocked, previous;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGTRAP);
    assert(!pthread_sigmask(SIG_BLOCK, &blocked, &previous));
    assert(!ik_target_arm(10, 30));
    assert(!pthread_sigmask(SIG_SETMASK, &previous, NULL));
    assert(ik_target_arm(10, 30));
    ik_target_restore();
    puts("IK target: manifest/window/coverage/full function/constants/RX/trap refusals PASS");
}
static void test_ik_target_begin_after_unused_stop(void) {
    assert(!ik_target_ever_installed && !ik_target_order_ever_installed);
    ik_target_restore();
    ik_target_order_restore();
    assert(ik_target_stopping && ik_target_order_stopping);
    ikt_prepare();
    assert(!ik_target_stopping && !ik_target_order_stopping);
    assert(ik_target_arm(10, 30));
    ik_target_restore();
    assert(ik_target_stopping && !ik_target_installed && !ik_target_cleanup);
    ik_target_begin();
    assert(ik_target_stopping && !ik_target_arm(10, 30));
    puts("IK target: unused STOP/start resets admission; installed one-shot remains stopped PASS");
}
static void test_ik_target_observer_elf(void) {
    test_stock_observer_isolated(test_ik_target_begin_after_unused_stop);
    test_stock_observer_isolated(test_ik_target_functional);
    test_stock_observer_isolated(test_ik_target_partial);
    test_stock_observer_isolated(test_ik_target_second_site_failure);
    test_stock_observer_isolated(test_ik_target_foreign);
    test_stock_observer_isolated(test_ik_target_limits);
    test_stock_observer_isolated(test_ik_target_stop_race);
    test_stock_observer_isolated(test_ik_target_refusals);
}

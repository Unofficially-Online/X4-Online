/* Synthetic RANGE trampoline and lifecycle tests: no X4 process is started. */
__attribute__((used)) static uintptr_t range_test_target;
__attribute__((used)) static uint64_t range_test_flags, range_test_id_register, range_test_stack_register;
__attribute__((used)) static uint64_t range_test_rsp_before, range_test_rsp_after;
__attribute__((used, aligned(16))) static unsigned char range_test_xmm_before[16] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
};
__attribute__((used, aligned(16))) static unsigned char range_test_xmm_after[16];
__attribute__((used, aligned(64))) static unsigned char range_test_zmm_before[64], range_test_zmm_after[64];
__attribute__((used)) static uint64_t range_test_mask_after;
__asm__(
".pushsection .text\n"
".type range_test_run,@function\n"
"range_test_run:\n"
" push %rbp\n push %r12\n push %r14\n push %r15\n sub $40,%rsp\n"
" mov %rdi,%r12\n mov %rsi,%rbp\n mov %rdx,16(%rsp)\n"
" mov %rsp,range_test_rsp_before(%rip)\n"
" movdqu range_test_xmm_before(%rip),%xmm0\n std\n stc\n"
" call *range_test_target(%rip)\n"
" pushfq\n pop range_test_flags(%rip)\n cld\n"
" mov %r14,range_test_id_register(%rip)\n mov %r15,range_test_stack_register(%rip)\n"
" mov %rsp,range_test_rsp_after(%rip)\n movdqu %xmm0,range_test_xmm_after(%rip)\n"
" add $40,%rsp\n pop %r15\n pop %r14\n pop %r12\n pop %rbp\n ret\n"
".type range_test_run_zmm,@function\n"
"range_test_run_zmm:\n"
" sub $8,%rsp\n vmovdqu64 range_test_zmm_before(%rip),%zmm16\n"
" mov $0x1234,%rax\n kmovq %rax,%k1\n call range_test_run\n"
" vmovdqu64 %zmm16,range_test_zmm_after(%rip)\n kmovq %k1,%rax\n"
" mov %rax,range_test_mask_after(%rip)\n vzeroupper\n add $8,%rsp\n ret\n"
".type range_test_resume,@function\n"
"range_test_resume:\n ret\n"
".popsection\n");
void range_test_run(void *row, void *owner, uintptr_t stack_value);
void range_test_resume(void);
void range_test_run_zmm(void *row, void *owner, uintptr_t stack_value);
static int range_test_barrier_stage, range_test_barrier_entered, range_test_barrier_release;
static int range_test_arm_result, range_test_swap_mode, range_test_close_fail;
static void range_test_barrier(int stage) {
    if (stage != range_test_barrier_stage) return;
    __atomic_store_n(&range_test_barrier_entered, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&range_test_barrier_release, __ATOMIC_ACQUIRE)) sched_yield();
}
static void *range_test_arm_thread(void *unused) {
    (void)unused;
    ui_thread = GetCurrentThreadId();
    range_test_arm_result = stock_range_arm();
    return NULL;
}
static void *range_test_stop_thread(void *unused) {
    (void)unused;
    stock_range_restore();
    return NULL;
}
static int range_test_swap(void *site, unsigned __int128 expected, unsigned __int128 desired) {
    int result = stock_trap_swap(site, expected, desired);
    if (range_test_swap_mode == 3) range_test_barrier(3);
    if (range_test_swap_mode == 1) {
        range_test_swap_mode = 0;
        return 0;
    }
    return result;
}
static int range_test_close(void *site) {
    if (range_test_close_fail) {
        range_test_close_fail = 0;
        return 0;
    }
    return gate_code_close(site);
}
static void range_test_stop_race(int stage) {
    pthread_t arm, stop;
    stock_range_begin();
    range_test_barrier_stage = stage;
    range_test_barrier_entered = range_test_barrier_release = 0;
    stock_range_test_arm_hook = range_test_barrier;
    range_test_swap_mode = stage == 3 ? 3 : 0;
    stock_range_swap = range_test_swap;
    assert(!pthread_create(&arm, NULL, range_test_arm_thread, NULL));
    while (!__atomic_load_n(&range_test_barrier_entered, __ATOMIC_ACQUIRE)) sched_yield();
    assert(!pthread_create(&stop, NULL, range_test_stop_thread, NULL));
    while (!__atomic_load_n(&stock_range_stopping, __ATOMIC_ACQUIRE)) sched_yield();
    __atomic_store_n(&range_test_barrier_release, 1, __ATOMIC_RELEASE);
    assert(!pthread_join(arm, NULL) && !pthread_join(stop, NULL));
    assert(!range_test_arm_result && !stock_range_installed && !stock_range_cleanup_uncertain && !stock_range_enabled);
    assert(!memcmp(LX(STOCK_RANGE_SITE), stock_range_block, 16));
    stock_range_test_arm_hook = NULL;
    stock_range_swap = stock_trap_swap;
    range_test_swap_mode = 0;
    stock_range_ever_installed = 0;
    stock_range_restored = 0;
}

static void test_stock_range_observer_elf(void) {
    unsigned char input[0x6500] = {0};
    unsigned char range_input[0x24] = {0};
    uintptr_t input_pointer = (uintptr_t)input;
    uint32_t id = 42, bits = 0x3f400000u;
    memcpy(range_input + 0x1c, &id, sizeof(id));
    memcpy(range_input + 0x20, &bits, sizeof(bits));
    reset_session();
    stock_range_installed = stock_range_restored = stock_range_cleanup_uncertain = 0;
    stock_range_ever_installed = stock_range_enabled = 0;
    stock_range_begin();
    assert(!stock_range_arm());
    stats.active = 1;
    ui_thread = GetCurrentThreadId();
    __atomic_store_n(&stock_state_last_issued, 7, __ATOMIC_RELEASE);
    assert(stock_range_pins());
    cov_armed = 1;
    assert(!stock_range_arm() && !strcmp(stock_range_stage, "coverage"));
    cov_armed = 0;
    unsigned char *site = LX(STOCK_RANGE_SITE);
    site[0] ^= 1;
    assert(!stock_range_arm());
    site[0] ^= 1;
    assert(stock_range_arm() && stock_range_owned() && stock_range_check());
    memcpy(LX(STOCK_RANGE_INPUT_GLOBAL), &input_pointer, sizeof(input_pointer));
    stock_range_continuation = (uintptr_t)range_test_resume;
    range_test_target = (uintptr_t)site;
    range_test_run(range_input, input, UINT64_C(0x123456789abcdef0));
    assert(stock_range_count == 1 && stock_range_rows[0].value == bits &&
           range_test_id_register == id && range_test_stack_register == UINT64_C(0x123456789abcdef0));
    stock_range_restore();
    assert(stock_range_restored && !stock_range_installed && !memcmp(site, stock_range_block, 16));
    stock_range_begin();
    assert(!stock_range_arm() && !strcmp(stock_range_stage, "one-shot"));
    stock_range_ever_installed = 0;
    stock_range_restored = 0;
    assert(stock_range_arm() && stock_range_owned());
    unsigned char foreign[16];
    memcpy(foreign, stock_range_block, sizeof(foreign));
    foreign[15] ^= 1;
    unsigned __int128 foreign_line;
    memcpy(&foreign_line, foreign, sizeof(foreign_line));
    assert(gate_code_swap128(site, stock_range_patched, foreign_line));
    stock_range_restore();
    assert(stock_range_installed && stock_range_cleanup_uncertain && !memcmp(site, foreign, 16));
    assert(gate_code_swap128(site, foreign_line, stock_range_patched));
    stock_range_restore();
    assert(!stock_range_installed && stock_range_restored && !stock_range_cleanup_uncertain);
    stock_range_ever_installed = 0;
    stock_range_restored = 0;

    range_test_target = (uintptr_t)stock_range_entry;
    assert(economy_rng_xstate());
    stock_range_xstate_mask = economy_rng_xstate_mask;
    stock_range_continuation = (uintptr_t)range_test_resume;
    stock_range_owner_thread = GetCurrentThreadId();
    __atomic_store_n(&stock_state_last_issued, 7, __ATOMIC_RELEASE);
    memcpy(LX(STOCK_RANGE_INPUT_GLOBAL), &input_pointer, sizeof(input_pointer));
    memset(stock_range_rows, 0, sizeof(stock_range_rows));
    stock_range_count = stock_range_overflow = stock_range_reentry = stock_range_invalid = 0;
    stock_range_enabled = 1;
    stock_range_test_clobber = 1;
    range_test_run(range_input, input, UINT64_C(0x123456789abcdef0));
    assert(range_test_id_register == id && range_test_stack_register == UINT64_C(0x123456789abcdef0));
    assert(range_test_rsp_before == range_test_rsp_after && (range_test_flags & 1u) && (range_test_flags & (1u << 10)));
    assert(!memcmp(range_test_xmm_before, range_test_xmm_after, sizeof(range_test_xmm_before)));
    assert(stock_range_count == 1 && stock_range_rows[0].ready && stock_range_rows[0].id == id &&
           stock_range_rows[0].value == bits && stock_range_rows[0].known_context);
    if (stock_range_xstate_mask == 0xe7) {
        for (unsigned i = 0; i < sizeof(range_test_zmm_before); ++i)
            range_test_zmm_before[i] = (unsigned char)(i + 1);
        stock_range_test_clobber = 2;
        range_test_run_zmm(range_input, input, 3);
        assert(!memcmp(range_test_zmm_before, range_test_zmm_after, sizeof(range_test_zmm_before)) &&
               range_test_mask_after == 0x1234);
        stock_range_test_clobber = 1;
    }
    unsigned unknown_index = stock_range_xstate_mask == 0xe7 ? 2u : 1u;
    bits = 0x80000000u;
    memcpy(range_input + 0x20, &bits, sizeof(bits));
    range_test_run(range_input, NULL, 3);
    assert(stock_range_count == unknown_index + 1 && stock_range_rows[unknown_index].value == bits && !stock_range_rows[unknown_index].known_context);
    bits = 0x7fc00000u;
    memcpy(range_input + 0x20, &bits, sizeof(bits));
    range_test_run(range_input, input, 3);
    assert(stock_range_count == unknown_index + 1 && stock_range_invalid == 1);
    stock_range_count = STOCK_RANGE_ROWS;
    bits = 0;
    memcpy(range_input + 0x20, &bits, sizeof(bits));
    range_test_run(range_input, input, 3);
    assert(stock_range_overflow == 1);
    stock_range_in_callback = 1;
    range_test_run(range_input, input, 3);
    stock_range_in_callback = 0;
    assert(stock_range_reentry == 1);
    stock_range_enabled = 0;
    stock_range_test_clobber = 0;
    range_test_stop_race(1);
    range_test_stop_race(2);
    range_test_stop_race(3);
    stock_range_begin();
    ui_thread = GetCurrentThreadId();
    stock_trap_test_sync_failures = 1;
    assert(!stock_range_arm());
    assert(!stock_range_installed && !stock_range_cleanup_uncertain && !stock_trap_sync_uncertain);
    assert(!memcmp(site, stock_range_block, 16));
    stock_range_ever_installed = 0;
    stock_range_restored = 0;
    stock_range_begin();
    ui_thread = GetCurrentThreadId();
    range_test_swap_mode = 1;
    stock_range_swap = range_test_swap;
    range_test_close_fail = 1;
    stock_range_close = range_test_close;
    assert(!stock_range_arm());
    assert(stock_range_cleanup_uncertain && !stock_range_restored);
    stock_range_restore();
    assert(!stock_range_cleanup_uncertain && stock_range_restored && !stock_range_installed);
    assert(!memcmp(site, stock_range_block, 16));
    stock_range_swap = stock_trap_swap;
    stock_range_close = gate_code_close;
    stock_range_ever_installed = 0;
    puts("stock range observer: pins, ABI, rows and lifecycle PASS");
}

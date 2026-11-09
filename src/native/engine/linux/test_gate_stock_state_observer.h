/* Synthetic tests only: no X4 process is launched. The copied dispatcher runs
 * against a synthetic map and handler. The real ELF is mapped as data
 * to exercise complete file/site pins and ownership-safe restoration. */
__attribute__((used)) static uint64_t stock_test_rcx, stock_test_rsp_before, stock_test_rsp_after;
__attribute__((used)) static uint64_t stock_test_flags, stock_test_r8, stock_test_r9, stock_test_redzone;
__attribute__((used)) static unsigned char stock_test_zero;
__attribute__((used, aligned(16))) static unsigned char stock_test_xmm_before[16] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
};
__attribute__((used, aligned(16))) static unsigned char stock_test_xmm_after[16];
__attribute__((used, aligned(64))) static unsigned char stock_test_zmm_before[64], stock_test_zmm_after[64];
__attribute__((used)) static uint64_t stock_test_mask_after;
__asm__(
".pushsection .text\n"
".type stock_test_run,@function\n"
"stock_test_run:\n"
" sub $8,%rsp\n"
" mov %rsp,stock_test_rsp_before(%rip)\n"
" movabs $0x123456789abcdef0,%rax\n mov %rax,-136(%rsp)\n"
" mov $0x108,%r8\n mov $0x109,%r9\n std\n"
" movdqu stock_test_xmm_before(%rip),%xmm0\n"
" call stock_state_entry\n"
" pushfq\n pop stock_test_flags(%rip)\n cld\n"
" mov %r8,stock_test_r8(%rip)\n mov %r9,stock_test_r9(%rip)\n"
" mov -136(%rsp),%rax\n mov %rax,stock_test_redzone(%rip)\n"
" mov %rsp,stock_test_rsp_after(%rip)\n"
" movdqu %xmm0,stock_test_xmm_after(%rip)\n"
" add $8,%rsp\n ret\n"
".type stock_test_resume,@function\n"
"stock_test_resume:\n"
" setz stock_test_zero(%rip)\n"
" mov %rcx,stock_test_rcx(%rip)\n"
" ret\n"
".type stock_test_run_zmm,@function\n"
"stock_test_run_zmm:\n"
" sub $8,%rsp\n vmovdqu64 stock_test_zmm_before(%rip),%zmm16\n"
" mov $0x1234,%rax\n kmovq %rax,%k1\n call stock_state_entry\n"
" vmovdqu64 %zmm16,stock_test_zmm_after(%rip)\n kmovq %k1,%rax\n"
" mov %rax,stock_test_mask_after(%rip)\n vzeroupper\n add $8,%rsp\n ret\n"
".popsection\n");
void stock_test_run(void *map, int id, unsigned char value);
void stock_test_resume(void);
void stock_test_run_zmm(void *map, int id, unsigned char value);
static unsigned stock_test_handler_calls, stock_test_handler_id, stock_test_handler_value;
static int stock_test_barrier_stage, stock_test_barrier_entered, stock_test_barrier_release;
static int stock_test_arm_result, stock_test_swap_mode, stock_test_close_fail;
static void stock_test_barrier(int stage) {
    if (stage != stock_test_barrier_stage) return;
    __atomic_store_n(&stock_test_barrier_entered, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&stock_test_barrier_release, __ATOMIC_ACQUIRE)) sched_yield();
}
static void *stock_test_arm_thread(void *unused) {
    (void)unused;
    ui_thread = GetCurrentThreadId();
    stock_test_arm_result = stock_state_arm();
    return NULL;
}
static void *stock_test_stop_thread(void *unused) {
    (void)unused;
    stock_state_restore();
    return NULL;
}
static int stock_test_swap(void *site, unsigned __int128 expected, unsigned __int128 desired) {
    int result = stock_trap_swap(site, expected, desired);
    if (stock_test_swap_mode == 3) stock_test_barrier(3);
    if (stock_test_swap_mode == 1) {
        stock_test_swap_mode = 0;
        return 0;
    }
    return result;
}
static int stock_test_close(void *site) {
    if (stock_test_close_fail) {
        stock_test_close_fail = 0;
        return 0;
    }
    return gate_code_close(site);
}
static void stock_test_stop_race(int stage) {
    pthread_t arm, stop;
    stock_state_begin();
    stock_test_barrier_stage = stage;
    stock_test_barrier_entered = stock_test_barrier_release = 0;
    stock_state_test_arm_hook = stock_test_barrier;
    stock_test_swap_mode = stage == 3 ? 3 : 0;
    stock_state_swap = stock_test_swap;
    assert(!pthread_create(&arm, NULL, stock_test_arm_thread, NULL));
    while (!__atomic_load_n(&stock_test_barrier_entered, __ATOMIC_ACQUIRE)) sched_yield();
    assert(!pthread_create(&stop, NULL, stock_test_stop_thread, NULL));
    while (!__atomic_load_n(&stock_state_stopping, __ATOMIC_ACQUIRE)) sched_yield();
    __atomic_store_n(&stock_test_barrier_release, 1, __ATOMIC_RELEASE);
    assert(!pthread_join(arm, NULL) && !pthread_join(stop, NULL));
    assert(!stock_test_arm_result && !stock_state_installed && !stock_state_cleanup_uncertain && !stock_state_enabled);
    assert(!memcmp(LX(STOCK_STATE_SITE), stock_state_block, 16));
    stock_state_test_arm_hook = NULL;
    stock_state_swap = stock_trap_swap;
    stock_test_swap_mode = 0;
    stock_state_ever_installed = 0;
    stock_state_restored = 0;
}
static void stock_test_handler(int id, unsigned char value) {
    ++stock_test_handler_calls;
    stock_test_handler_id = (unsigned)id;
    stock_test_handler_value = value;
}

static void test_stock_state_observer_elf(void) {
    static unsigned char input[0x6500];
    static uintptr_t node[6];
    unsigned char *map = input + STOCK_STATE_MAP_OFFSET;
    unsigned char original[16];
    uintptr_t input_ptr = (uintptr_t)input, root = UINT64_C(0x12345678);
    reset_session();
    stock_state_installed = stock_state_restored = stock_state_cleanup_uncertain = 0;
    stock_state_ever_installed = stock_state_enabled = 0;
    stock_state_begin();
    assert(!stock_state_arm());
    stats.active = 1;
    stats.issued = 12;
    ui_thread = GetCurrentThreadId();
    __atomic_store_n(&stock_state_last_issued, 12, __ATOMIC_RELEASE);
    assert(stock_state_pins());
    cov_armed = 1;
    assert(!stock_state_arm() && !strcmp(stock_state_stage, "coverage"));
    cov_armed = 0;
    unsigned char *site = LX(STOCK_STATE_SITE);
    memcpy(original, site, sizeof(original));
    site[0] ^= 1;
    assert(!stock_state_arm());
    site[0] ^= 1;
    const char *exe = gate_env.exe;
    gate_env.exe = "/dev/null";
    assert(!stock_state_arm());
    gate_env.exe = exe;
    assert(stock_state_arm() && stock_state_owned() && stock_state_check());
    assert(stock_state_installed && memcmp(original, site, 16));
    /* The pinned dispatch body itself has only internal branches. Execute its
     * copied, patched bytes against an owned one-node tree and fake handler. */
    memset(node, 0, sizeof(node));
    node[0] = (uintptr_t)(map + 0x40);
    node[4] = 17;
    node[5] = (uintptr_t)stock_test_handler;
    root = (uintptr_t)node;
    memcpy(map + 0x48, &root, sizeof(root));
    memcpy(LX(STOCK_STATE_INPUT_GLOBAL), &input_ptr, sizeof(input_ptr));
    /* The copied function spans two pages; fake_map starts non-executable. */
    unsigned char *next_page = (unsigned char *)(((uintptr_t)site & ~(uintptr_t)(GATE_PAGE-1)) + GATE_PAGE);
    assert(!mprotect(next_page, GATE_PAGE, PROT_READ|PROT_EXEC));
    stock_test_handler_calls = 0;
    ((void(*)(void*, int, unsigned char))(void*)site)(map, 17, 1);
    assert(!mprotect(next_page, GATE_PAGE, PROT_READ|PROT_WRITE));
    assert(stock_test_handler_calls == 1 && stock_test_handler_id == 17 && stock_test_handler_value == 1);
    assert(stock_state_count == 1 && stock_state_rows[0].ready && stock_state_rows[0].known_context);
    stock_state_restore();
    assert(stock_state_restored && !stock_state_installed && !memcmp(original, site, 16));
    stock_state_begin();
    assert(!stock_state_arm() && !strcmp(stock_state_stage, "one-shot"));
    /* The synthetic copied image has no active callers. */
    stock_state_ever_installed = 0;
    stock_state_restored = 0;
    assert(stock_state_arm() && stock_state_owned());
    unsigned char foreign[16];
    memcpy(foreign, stock_state_block, sizeof(foreign));
    foreign[15] ^= 1;
    unsigned __int128 foreign_line;
    memcpy(&foreign_line, foreign, sizeof(foreign_line));
    assert(gate_code_swap128(site, stock_state_patched, foreign_line));
    stock_state_restore();
    assert(stock_state_installed && stock_state_cleanup_uncertain && !memcmp(site, foreign, 16));
    assert(gate_code_swap128(site, foreign_line, stock_state_patched));
    stock_state_restore();
    assert(!stock_state_installed && stock_state_restored && !stock_state_cleanup_uncertain);
    stock_state_ever_installed = 0;
    stock_state_restored = 0;
    stock_state_cleanup_uncertain = 0;

    /* The naked trampoline runs with a fake map, then returns through our
     * continuation. This verifies displaced TEST/MOV and preserved XMM0. */
    assert(economy_rng_xstate());
    stock_state_xstate_mask = economy_rng_xstate_mask;
    stock_state_continuation = (uintptr_t)stock_test_resume;
    stock_state_owner_thread = GetCurrentThreadId();
    __atomic_store_n(&stock_state_last_issued, 12, __ATOMIC_RELEASE);
    memcpy(LX(STOCK_STATE_INPUT_GLOBAL), &input_ptr, sizeof(input_ptr));
    root = UINT64_C(0x12345678);
    memcpy(map + 0x48, &root, sizeof(root));
    memset(stock_state_rows, 0, sizeof(stock_state_rows));
    stock_state_count = stock_state_overflow = stock_state_reentry = 0;
    stock_state_enabled = 1;
    stock_state_test_clobber = 1;
    stock_test_run(map, 17, 1);
    assert(stock_test_rcx == root && !stock_test_zero && stock_test_rsp_before == stock_test_rsp_after);
    assert((stock_test_flags & (1u<<10)) && stock_test_r8 == 0x108 && stock_test_r9 == 0x109 &&
           stock_test_redzone == UINT64_C(0x123456789abcdef0));
    assert(!memcmp(stock_test_xmm_before, stock_test_xmm_after, sizeof(stock_test_xmm_before)));
    assert(stock_state_count == 1 && stock_state_rows[0].ready && stock_state_rows[0].tick == 12 &&
           stock_state_rows[0].id == 17 && stock_state_rows[0].value == 1 && stock_state_rows[0].known_context);
    if (stock_state_xstate_mask == 0xe7) {
        for (unsigned i = 0; i < sizeof(stock_test_zmm_before); ++i)
            stock_test_zmm_before[i] = (unsigned char)(i + 1);
        stock_state_test_clobber = 2;
        /* destroys ZMM16 and k1 in the helper */
        stock_test_run_zmm(map, 18, 1);
        assert(!memcmp(stock_test_zmm_before, stock_test_zmm_after, sizeof(stock_test_zmm_before)) && stock_test_mask_after == 0x1234);
        stock_state_test_clobber = 1;
    }
    root = 0;
    memcpy(map + 0x48, &root, sizeof(root));
    stock_state_owner_thread = GetCurrentThreadId() + 1;
    stock_test_run(map, 19, 0);
    unsigned unknown_index = stock_state_xstate_mask == 0xe7 ? 2u : 1u;
    assert(stock_test_zero && stock_test_rcx == 0 && stock_state_rows[unknown_index].id == 19 &&
           stock_state_rows[unknown_index].value == 0 && !stock_state_rows[unknown_index].known_context);
    stock_state_count = STOCK_STATE_ROWS;
    stock_test_run(map, 20, 1);
    assert(stock_state_overflow == 1);
    stock_state_in_callback = 1;
    stock_test_run(map, 21, 1);
    stock_state_in_callback = 0;
    assert(stock_state_reentry == 1);
    stock_state_enabled = 0;
    stock_state_test_clobber = 0;
    ui_thread = GetCurrentThreadId();
    stock_test_stop_race(1);
    /* STOP while arm is hashing the executable */
    stock_test_stop_race(2);
    /* STOP just before compare-exchange */
    stock_test_stop_race(3);
    /* STOP after the write, before CAS returns */
    stock_state_begin();
    ui_thread = GetCurrentThreadId();
    stock_test_swap_mode = 1;
    stock_state_swap = stock_test_swap;
    stock_test_close_fail = 1;
    stock_state_close = stock_test_close;
    assert(!stock_state_arm());
    assert(stock_state_cleanup_uncertain && !stock_state_restored);
    stock_state_restore();
    assert(!stock_state_cleanup_uncertain && stock_state_restored && !stock_state_installed);
    assert(!memcmp(LX(STOCK_STATE_SITE), stock_state_block, 16));
    stock_state_swap = stock_trap_swap;
    stock_state_close = gate_code_close;
    stock_state_ever_installed = 0;
    reset_session();
    printf("stock state observer: ELF pins, ownership, XMM, native instructions and partial-context rows PASS\n");
}

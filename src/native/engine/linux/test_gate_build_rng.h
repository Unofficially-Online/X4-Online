/* Synthetic TLS/identity and exact patch ownership tests; no X4 process runs. */
static unsigned build_rng_test_calls;
static uint64_t build_rng_test_seen;
static const void *build_rng_test_stub_storage;
static float build_rng_test_stub_range;

static float build_rng_test_draw(float range) {
    uint64_t *word = gate_tls_rng(gate_tls_self());
    ++build_rng_test_calls;
    build_rng_test_seen = *word;
    *word = erng_lx_pcg(*word);
    return range / 4.0f;
}

static float build_rng_test_stub_target(const void *storage, float range) {
    build_rng_test_stub_storage = storage;
    build_rng_test_stub_range = range;
    return range / 2.0f;
}

/* The callsite supplies storage in RBX and the native float argument in XMM0. */
__asm__(".pushsection .text\n"
        ".p2align 4\n"
        ".type build_rng_test_call_stub,@function\n"
        "build_rng_test_call_stub:\n"
        "  push %rbx\n"
        "  mov %rdi,%rax\n"
        "  mov %rsi,%rbx\n"
        "  call *%rax\n"
        "  pop %rbx\n"
        "  ret\n"
        ".size build_rng_test_call_stub,.-build_rng_test_call_stub\n"
        ".popsection\n");
float build_rng_test_call_stub(void *stub, const void *storage, float range);

static void test_build_rng_linux(void) {
    reset_session();
    stats.active = 1;
    boot_mode = 1;
    boot_phase = 1;
    ui_thread = GetCurrentThreadId();
    world_identities = calloc(WORLD_IDENTITY_CAPACITY, sizeof(*world_identities));
    assert(world_identities);
    world_identity_installed = 1;
    world_identity_sealed = 0;
    world_identity_count = 0;
    int allocated_imports = lxw_inserts == NULL;
    if (allocated_imports) lxw_inserts = calloc(LXW_INSERTS_MAX, sizeof(*lxw_inserts));
    assert(lxw_inserts);
    /* The import node is recorded but its identity is not sealed yet. */
    unsigned char import_node[0x30] = {0};
    uint64_t imported_runtime = 7001;
    memcpy(import_node + 0x28, &imported_runtime, 8);
    lxw_insert_count = 1;
    lxw_inserts[0] = (lxw_insert){0x123, import_node};
    unsigned char storage[16];
    uintptr_t vtable = (uintptr_t)LX(0x3c76e90u); /* constructor-time Container */
    uint64_t runtime = 7001;
    memcpy(storage, &vtable, 8);
    memcpy(storage + 8, &runtime, 8);
    double game_time = 7.125;
    uint64_t time_bits;
    memcpy(&time_bits, &game_time, 8);
    memcpy(gate_tls_time(gate_tls_self()), &game_time, 8);
    uint64_t *word = gate_tls_rng(gate_tls_self());
    *word = 0x9876;
    build_rng_test_calls = 0;
    linux_build_rng_keyed = linux_build_rng_loading = linux_build_rng_fallback = 0;
    linux_build_rng_unmapped = linux_build_rng_native = 0;
    linux_build_rng_failures = 0;
    assert(linux_build_rng_run(build_rng_test_draw, storage, 10.0f, (uintptr_t)LX(LX_BUILD_CALL_VA) + 5) == 2.5f);
    assert(build_rng_test_calls == 1 && *word == 0x9876 && linux_build_rng_keyed == 1 && linux_build_rng_loading == 1);
    assert(lxw_insert_count == 0 && world_identity_count == 1);
    assert(build_rng_test_seen == lockstep_seed(LINUX_BUILD_RNG_DOMAIN, 0, 0x123, time_bits, 0, WORLD_KEY_SAVED));
    runtime = 9999;
    memcpy(storage + 8, &runtime, 8);
    build_rng_test_calls = 0;
    assert(linux_build_rng_run(build_rng_test_draw, storage, 10.0f, (uintptr_t)LX(LX_BUILD_CALL_VA) + 5) == 2.5f);
    assert(build_rng_test_calls == 1 && *word == 0x9876 && linux_build_rng_fallback == 1);
    assert(build_rng_test_seen == lockstep_seed(LINUX_BUILD_RNG_DOMAIN, 0, 0, 0x2a8f1f0, time_bits, WORLD_KEY_NONE));
    assert(linux_build_rng_row_count == 2 && linux_build_rng_rows[0].ready && linux_build_rng_rows[1].ready);
    assert(linux_build_rng_rows[0].identity == 0x123 && !linux_build_rng_rows[0].fallback && linux_build_rng_rows[0].phase == 1);
    assert(linux_build_rng_rows[1].identity == 0 && linux_build_rng_rows[1].fallback);
    assert(linux_build_rng_rows[0].seed != linux_build_rng_rows[1].seed);
    assert(linux_build_rng_rows[0].caller_rva == LX_BUILD_CALL_VA + 5 - LX_IMAGE_BASE);

    vtable = 1;
    memcpy(storage, &vtable, 8);
    build_rng_test_calls = 0;
    assert(linux_build_rng_run(build_rng_test_draw, storage, 10.0f, (uintptr_t)LX(LX_BUILD_CALL_VA) + 5) == 2.5f);
    assert(build_rng_test_calls == 1 && build_rng_test_seen == 0x9876 && *word == erng_lx_pcg(0x9876));
    assert(linux_build_rng_unmapped == 1 && linux_build_rng_native == 1 && linux_build_rng_failures == 1);
    vtable = (uintptr_t)LX(0x3c76e90u);
    memcpy(storage, &vtable, 8);
    build_rng_test_calls = 0;
    *word = 0x1111;
    assert(linux_build_rng_run(build_rng_test_draw, storage, 10.0f, 1) == 2.5f);
    assert(build_rng_test_calls == 1 && build_rng_test_seen == 0x1111 && *word == erng_lx_pcg(0x1111));
    assert(linux_build_rng_failures == 2);
    linux_build_rng_row_count = LINUX_BUILD_RNG_ROW_CAPACITY;
    assert(linux_build_rng_run(build_rng_test_draw, storage, 10.0f, (uintptr_t)LX(LX_BUILD_CALL_VA) + 5) == 2.5f);
    assert(linux_build_rng_row_count == LINUX_BUILD_RNG_ROW_CAPACITY + 1);

    unsigned char *page = mmap(NULL, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(page != MAP_FAILED);
    linux_build_rng_write_stub(page);
    void *target = (void *)build_rng_test_stub_target;
    memcpy(page + 9, &target, sizeof(target));
    build_rng_test_stub_storage = NULL;
    assert(build_rng_test_call_stub(page, storage, 10.0f) == 5.0f);
    assert(build_rng_test_stub_storage == storage && build_rng_test_stub_range == 10.0f);
    assert(!munmap(page, GATE_PAGE));
    if (allocated_imports) {
        free(lxw_inserts);
        lxw_inserts = NULL;
    }
    free(world_identities);
    world_identities = NULL;
    world_identity_installed = 0;
    world_identity_count = 0;
    printf("build RNG TLS, loading identity and stub ABI PASS\n");
}

static void test_build_rng_elf(void) {
    reset_session();
    stats.active = 1;
    boot_mode = 1;
    boot_phase = 0;
    ui_thread = GetCurrentThreadId();
    world_identity_installed = 0;
    assert(!linux_build_rng_arm());
    world_identity_installed = 1;
    assert(linux_build_rng_pins() && linux_build_rng_arm() && linux_build_rng_owned() && linux_build_rng_check());
    assert(linux_build_rng_arm());
    unsigned char original_range[4], changed_range[4];
    memcpy(original_range, LX(0x2e27150u), sizeof(original_range));
    memcpy(changed_range, original_range, sizeof(changed_range));
    changed_range[0] ^= 1;
    tl_write(LX(0x2e27150u), changed_range, sizeof(changed_range));
    assert(!linux_build_rng_check() && !linux_build_rng_arm());
    tl_write(LX(0x2e27150u), original_range, sizeof(original_range));
    assert(linux_build_rng_check());
    assert(!mprotect(linux_build_rng_page, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
    linux_build_rng_page[0] ^= 1;
    assert(!linux_build_rng_owned() && !linux_build_rng_arm());
    linux_build_rng_restore();
    assert(linux_build_rng_installed && !linux_build_rng_restored);
    linux_build_rng_write_stub(linux_build_rng_page);
    assert(!mprotect(linux_build_rng_page, GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(linux_build_rng_owned());
    unsigned char installed[16];
    memcpy(installed, LX(LX_BUILD_CALL_BLOCK_VA), sizeof(installed));
    assert(!memcmp(installed, linux_build_rng_original_block, 7));
    assert(!memcmp(installed + 11, linux_build_rng_original_block + 11, 5));
    int32_t relative;
    memcpy(&relative, installed + 7, sizeof(relative));
    assert(LX(LX_BUILD_CALL_VA) + 5 + relative == linux_build_rng_page);
    unsigned char foreign[16];
    memcpy(foreign, installed, sizeof(foreign));
    foreign[0] ^= 1;
    tl_write(LX(LX_BUILD_CALL_BLOCK_VA), foreign, sizeof(foreign));
    assert(!linux_build_rng_owned() && !linux_build_rng_check() && !linux_build_rng_arm());
    linux_build_rng_restore();
    assert(linux_build_rng_installed && !linux_build_rng_restored);
    assert(!memcmp(LX(LX_BUILD_CALL_BLOCK_VA), foreign, sizeof(foreign)));
    tl_write(LX(LX_BUILD_CALL_BLOCK_VA), installed, sizeof(installed));
    linux_build_rng_failures = 0;
    assert(linux_build_rng_owned());
    linux_build_rng_restore();
    assert(!linux_build_rng_installed && linux_build_rng_restored);
    assert(!memcmp(LX(LX_BUILD_CALL_BLOCK_VA), linux_build_rng_original_block, sizeof(linux_build_rng_original_block)));
    world_identity_installed = 0;
    printf("build RNG ELF pins, ownership and restore PASS\n");
}

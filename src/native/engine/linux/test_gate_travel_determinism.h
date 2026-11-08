/* Synthetic dispatch and pinned ELF patch checks. No X4 code is executed. */
#include <sys/wait.h>
static unsigned inline_test_jobs, inline_test_forwards, inline_test_splices;
static unsigned char inline_test_manager[0x400];
static unsigned char inline_test_tls[0x3000];
#define INLINE_TLS (inline_test_tls + 0x2800)
static void inline_test_job(void *payload, void *scratch) {
    (void)payload;
    assert(!scratch);
    uint64_t time, previous, rng;
    uint32_t view;
    memcpy(&time, INLINE_TLS + GATE_TLS_LINUX_TIME, 8);
    memcpy(&previous, INLINE_TLS + GATE_TLS_LINUX_PREVIOUS_TIME, 8);
    memcpy(&rng, INLINE_TLS + GATE_TLS_LINUX_RNG, 8);
    memcpy(&view, INLINE_TLS + GATE_TLS_LINUX_VIEW, 4);
    assert(!time && !previous && !view && rng == lockstep_seed(LXT_INLINE_DOMAIN, stats.issued, inline_test_jobs + 1, 0, 0, 0));
    uint32_t pending, count, maximum;
    memcpy(&pending, inline_test_manager + 0x2e0, 4);
    memcpy(&count, inline_test_manager + 0x350, 4);
    memcpy(&maximum, inline_test_manager + 0x354, 4);
    assert(pending == 1 && maximum == 1 && count <= 1);
    pending = 0;
    count = 1; /* decrement, saturating release, then splice */
    memcpy(inline_test_manager + 0x2e0, &pending, 4);
    memcpy(inline_test_manager + 0x350, &count, 4);
    ++inline_test_splices;
    ++inline_test_jobs;
}
static int inline_test_queue(void *manager, void *job, void *payload, unsigned category, unsigned priority) {
    (void)manager;
    (void)job;
    (void)payload;
    (void)category;
    (void)priority;
    ++inline_test_forwards;
    return 73;
}
static void test_travel_inline_dispatch(void) {
    uint64_t payload[5] = {0};
    void *body = (void *)(uintptr_t)0x12340, *batch = payload, *manager = payload;
    memcpy(payload, &batch, 8);
    memcpy((unsigned char *)payload + 8, &body, 8);
    memcpy(LX(LX_FS_JTM_VA), &manager, 8);
    unsigned char *path_manager = inline_test_manager;
    memset(path_manager, 0, sizeof(inline_test_manager));
    void *path_ptr = path_manager;
    uint32_t one = 1;
    memcpy(LX(LX_PATH_MANAGER_VA), &path_ptr, 8);
    memcpy(path_manager + 0x2e0, &one, 4);
    memcpy(path_manager + 0x354, &one, 4);
    path_pass_open = 1;
    memset(inline_test_tls, 0x5a, sizeof(inline_test_tls));
    unsigned char original[0x3000];
    memcpy(original, inline_test_tls, sizeof(original));
    inline_test_jobs = inline_test_forwards = inline_test_splices = 0;
    lxt_inline_tick = UINT64_MAX;
    lxt_inline_passes = lxt_inline_batches = lxt_inline_ordinal = 0;
    travel_inline_installed = 1;
    stats.active = 1;
    boot_phase = 3;
    stats.issued = 7;
    assert(lxt_inline_dispatch(inline_test_queue, inline_test_job, body, INLINE_TLS, manager,
        (void *)inline_test_job, payload, 1, 0) == 0);
    uint32_t pending, count;
    memcpy(&pending, path_manager + 0x2e0, 4);
    memcpy(&count, path_manager + 0x350, 4);
    assert(!pending && count == 1 && inline_test_splices == 1);
    memcpy(path_manager + 0x2e0, &one, 4); /* next RunPass submission */
    assert(lxt_inline_dispatch(inline_test_queue, inline_test_job, body, INLINE_TLS, manager,
        (void *)inline_test_job, payload, 1, 0) == 0);
    memcpy(&pending, path_manager + 0x2e0, 4);
    memcpy(&count, path_manager + 0x350, 4);
    assert(!pending && count == 1 && inline_test_splices == 2);
    assert(inline_test_jobs == 2 && !inline_test_forwards && lxt_inline_ordinal == 2 && lxt_inline_passes == 1);
    memcpy(path_manager + 0x2e0, &one, 4);
    assert(!memcmp(original, inline_test_tls, sizeof(original)));
    /* Each mismatch exits before either dispatch target and emits a fatal marker. */
    for (unsigned kind = 0; kind < 3; ++kind) {
        int pipefd[2];
        assert(!pipe(pipefd));
        pid_t child = fork();
        assert(child >= 0);
        if (!child) {
            close(pipefd[0]);
            assert(dup2(pipefd[1], STDERR_FILENO) >= 0);
            close(pipefd[1]);
            if (kind == 0) payload[2] = 1;
            if (kind == 1) {
                uint32_t two = 2;
                memcpy(path_manager + 0x2e0, &two, 4);
            }
            if (kind == 2) {
                uint32_t two = 2;
                memcpy(path_manager + 0x354, &two, 4);
            }
            (void)lxt_inline_dispatch(inline_test_queue, inline_test_job, body, INLINE_TLS, manager,
                (void *)inline_test_job, payload, 1, 0);
            _exit(111);
        }
        close(pipefd[1]);
        char diagnostic[160] = {0};
        ssize_t count = read(pipefd[0], diagnostic, sizeof(diagnostic) - 1);
        close(pipefd[0]);
        int status;
        assert(waitpid(child, &status, 0) == child);
        assert(count > 0 && strstr(diagnostic, "LINUX_TRAVEL_INLINE_FATAL"));
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 190);
    }
    stats.active = 0;
    assert(lxt_inline_dispatch(inline_test_queue, inline_test_job, body, INLINE_TLS, manager,
        (void *)inline_test_job, payload, 1, 0) == 73);
    assert(inline_test_forwards == 1 && inline_test_jobs == 2);
    travel_inline_installed = 0;
    path_pass_open = 0;
    reset_session();
}
static void test_travel_inline_elf(void) {
    reset_session();
    thunk = make_thunk();
    assert(thunk);
    stats.active = 1;
    boot_mode = 1;
    ui_thread = GetCurrentThreadId();
    path_installed = 1;
    path_options = 1;
    path_det_mask = PATH_DET_BUDGET_BIT;
    tsnap_installed = 1;
    assert(!lxt_inline_arm());
    tsnap_installed = 0;
    unsigned char before_a[8], before_b[16];
    memcpy(before_a, LX(LXT_INLINE_SITE_A), 8);
    memcpy(before_b, LX(LXT_INLINE_SITE_B), 16);
    assert(lxt_inline_arm() && lxt_inline_owned());
    assert(!tsnap_arm());
    int32_t first_displacement, second_displacement;
    memcpy(&first_displacement, LX(0x13e3e0b), 4);
    memcpy(&second_displacement, LX(0x13e3e85), 4);
    assert(LX(0x13e3e0f) + first_displacement == thunk + LXT_INLINE_STUB);
    assert(LX(0x13e3e89) + second_displacement == thunk + LXT_INLINE_STUB);
    /* A foreign patch must not be overwritten during cleanup. */
    uintptr_t page = (uintptr_t)LX(LXT_INLINE_SITE_A) & ~(uintptr_t)(GATE_PAGE - 1);
    assert(!mprotect((void *)page, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
    LX(LXT_INLINE_SITE_A)[0] ^= 1;
    assert(!mprotect((void *)page, GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(!lxt_inline_owned());
    lxt_inline_restore();
    assert(travel_inline_installed && !lxt_inline_restored && lxt_inline_first_installed);
    assert(!lxt_inline_arm());
    assert(!mprotect((void *)page, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
    LX(LXT_INLINE_SITE_A)[0] ^= 1;
    assert(!mprotect((void *)page, GATE_PAGE, PROT_READ | PROT_EXEC));
    lxt_inline_restore();
    assert(!travel_inline_installed && lxt_inline_restored && lxt_inline_owned());
    assert(!memcmp(before_a, LX(LXT_INLINE_SITE_A), 8) && !memcmp(before_b, LX(LXT_INLINE_SITE_B), 16));
    /* Boot agent RNG and the optional budget patch both precede inline arm.
     * The unmodified hash must fail with these hooks physically installed. */
    assert(laa_agent_install());
    assert(!hash_ok(LX(LXT_INLINE_BODY), 0x24b,
        "3222ca2e412f8df6cc8db1c64295b34b0f0ce1c377a01a6a329430d6cb0f2229"));
    assert(lxt_inline_body_pin());
    assert(lxt_inline_arm() && lxt_inline_owned());
    lxt_inline_restore();
    assert(lxt_inline_restored);
    laa_agent_installed = 0;
    assert(!lxt_inline_arm() && !strcmp(lxt_inline_stage, "batch_pin"));
    laa_agent_installed = 1;
    assert(pbud_arm(4));
    assert(lxt_inline_body_pin());
    assert(lxt_inline_arm() && lxt_inline_owned());
    lxt_inline_restore();
    assert(lxt_inline_restored);

    /* The call can still target our stub while a foreign displacement changes
     * where its indirect jump reads the target. Verify all four bytes. */
    for (unsigned displacement_byte = 2; displacement_byte < 6; ++displacement_byte) {
        assert(laa_stub_ok());
        assert(!mprotect(laa_page, GATE_PAGE, PROT_READ | PROT_WRITE));
        laa_page[LAA_STUB_AGENT + displacement_byte] ^= 1;
        assert(!mprotect(laa_page, GATE_PAGE, PROT_READ | PROT_EXEC));
        assert(laa_call_state() == 1 && !laa_stub_ok());
        assert(!lxt_inline_body_pin());
        assert(!lxt_inline_arm() && !travel_inline_installed && !strcmp(lxt_inline_stage, "batch_pin"));
        assert(!memcmp(before_a, LX(LXT_INLINE_SITE_A), 8) && !memcmp(before_b, LX(LXT_INLINE_SITE_B), 16));
        assert(!mprotect(laa_page, GATE_PAGE, PROT_READ | PROT_WRITE));
        laa_page[LAA_STUB_AGENT + displacement_byte] ^= 1;
        assert(!mprotect(laa_page, GATE_PAGE, PROT_READ | PROT_EXEC));
        assert(laa_stub_ok() && lxt_inline_body_pin());
    }

    /* A foreign agent displacement or a foreign budget patch is never masked. */
    uintptr_t call_page = (uintptr_t)LX(LX_AGENT_CALL_VA) & ~(uintptr_t)(GATE_PAGE - 1);
    assert(!mprotect((void *)call_page, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
    LX(LX_AGENT_CALL_VA + 1)[0] ^= 1;
    assert(!mprotect((void *)call_page, GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(!lxt_inline_arm() && !strcmp(lxt_inline_stage, "batch_pin"));
    assert(!mprotect((void *)call_page, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
    LX(LX_AGENT_CALL_VA + 1)[0] ^= 1;
    LX(LX_PBUD_SITE)[0] ^= 1;
    assert(!mprotect((void *)call_page, GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(!lxt_inline_arm() && !strcmp(lxt_inline_stage, "batch_pin"));
    assert(!mprotect((void *)call_page, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
    LX(LX_PBUD_SITE)[0] ^= 1;
    assert(!mprotect((void *)call_page, GATE_PAGE, PROT_READ | PROT_EXEC));
    pbud_restore();
    laa_restore();
    assert(!pbud_installed && !laa_agent_installed);
    assert(lxt_inline_body_pin());
    /* A changed completion body is refused before either site can be patched. */
    uintptr_t body_page = (uintptr_t)LX(LXT_INLINE_BODY) & ~(uintptr_t)(GATE_PAGE - 1);
    assert(!mprotect((void *)body_page, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
    LX(LXT_INLINE_BODY)[0x230] ^= 1;
    assert(!mprotect((void *)body_page, GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(!lxt_inline_arm() && !travel_inline_installed);
    assert(!memcmp(before_a, LX(LXT_INLINE_SITE_A), 8) && !memcmp(before_b, LX(LXT_INLINE_SITE_B), 16));
    assert(!mprotect((void *)body_page, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
    LX(LXT_INLINE_BODY)[0x230] ^= 1;
    assert(!mprotect((void *)body_page, GATE_PAGE, PROT_READ | PROT_EXEC));
    path_installed = 0;
    path_options = 0;
    path_det_mask = 0;
    reset_session();
}

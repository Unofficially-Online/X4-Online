#define WBM_TEST
#include "lockstep_gate.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static int fail_rx_close;
static BOOL WINAPI test_protect(LPVOID address, SIZE_T size, DWORD protection, PDWORD old) {
    if (fail_rx_close && protection == PAGE_EXECUTE_READ) {
        return FALSE;
    }
    return VirtualProtect(address, size, protection, old);
}

static void put_pointer(unsigned char *location, const void *value) {
    memcpy(location, &value, sizeof(value));
}

static int partial_motion_write(void *destination, const void *source, size_t length) {
    assert(length >= 4);
    memcpy(destination, source, 4);
    return 0;
}

static void test_fallback_filter(void) {
    unsigned char body[0x80] = {0}, settings[0x80] = {0};
    unsigned char shape[0x40] = {0}, inner[0x30] = {0};
    unsigned char record[0x40] = {0}, motion[0x60] = {0};
    const float inverse_mass = 0.25f;
    float result[8];
    void *vtable = image + WBM_VTABLE;
    put_pointer(body + 0x40, shape);
    put_pointer(body + 0x48, motion);
    put_pointer(shape, vtable);
    put_pointer(shape + 0x20, inner);
    put_pointer(settings + 0x40, record);
    memcpy(motion + 0x58, &inverse_mass, 4);
    settings[0x60] = 2;
    inner[0x18] = 1;
    inner[0x19] = 8;
    record[0x3a] = 1;

    wbm_apply(body, settings);
    memcpy(result, motion + 0x20, sizeof(result));
    assert(wbm_seen == 1 && wbm_changed == 1 && wbm_failures == 0);
    for (unsigned i = 0; i < 4; i++) {
        assert(result[i] == 0.625f);
    }
    assert(result[4] == 0.0f && result[5] == 0.0f && result[6] == 0.0f && result[7] == 1.0f);
    assert(!memcmp(motion + 0x58, &inverse_mass, 4));
    wbm_apply(body, settings);
    assert(wbm_seen == 2 && wbm_already == 1 && wbm_changed == 1);
    record[0x3a] = 0;
    wbm_apply(body, settings);
    assert(wbm_updated == 1 && wbm_changed == 1);
    record[0x3a] = 1;
    inner[0x19] = 7;
    wbm_apply(body, settings);
    assert(wbm_seen == 2 && wbm_changed == 1);
    inner[0x19] = 8;
    memset(motion + 0x20, 0, 32);
    float invalid = 0.0f;
    memcpy(motion + 0x58, &invalid, 4);
    wbm_apply(body, settings);
    assert(wbm_failures == 1 && wbm_changed == 1);
    /* A matching fallback pattern is still invalid when inverse mass is zero. */
    result[0] = result[1] = result[2] = result[3] = 0.0f;
    result[4] = result[5] = result[6] = 0.0f;
    result[7] = 1.0f;
    memcpy(motion + 0x20, result, sizeof(result));
    wbm_apply(body, settings);
    assert(wbm_failures == 2 && wbm_already == 1);
    uint32_t nan = 0x7fc00000u;
    memcpy(motion + 0x58, &nan, 4);
    for (unsigned i = 0; i < 4; i++) {
        memcpy(motion + 0x20 + i * 4, &nan, 4);
    }
    wbm_apply(body, settings);
    assert(wbm_failures == 3 && wbm_already == 1);
    void *inaccessible = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    assert(inaccessible);
    put_pointer(body + 0x40, inaccessible);
    wbm_apply(body, settings);
    assert(wbm_failures == 4);
    assert(VirtualFree(inaccessible, 0, MEM_RELEASE));
    put_pointer(body + 0x40, shape);
    memcpy(motion + 0x58, &inverse_mass, 4);
    memset(motion + 0x20, 0, 32);
    wbm_test_write = partial_motion_write;
    wbm_apply(body, settings);
    wbm_test_write = NULL;
    assert(wbm_failures == 5 && wbm_write_uncertain == 1 && !wbm_clean());
    /* Isolate the following lifecycle cases from this intentionally uncertain write. */
    wbm_write_uncertain = 0;
}

static void test_lifecycle(void) {
    DWORD old;
    unsigned char ours[16], foreign[16];
    memcpy(image + WBM_ALLOCATE, wbm_original, 16);
    assert(VirtualProtect(image + WBM_ALLOCATE, 16, PAGE_EXECUTE_READ, &old));
    assert(wbm_prepare());
    assert(wbm_page_ok());
    /* Check every instruction boundary, including both continuation instructions. */
    static const unsigned offsets[] = {0, 3, 4, 5, 12, 16, 26};
    const uint64_t saved_rdi = 0x1111222233334444ULL;
    const uint64_t saved_rbp = 0x5555666677778888ULL;
    const uint64_t return_address = 0x123456789abcdef0ULL;
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        uint64_t stack[40] = {0};
        uint64_t *caller_stack = stack + 30;
        caller_stack[0] = return_address;
        caller_stack[-1] = saved_rbp;
        caller_stack[-2] = saved_rdi;
        CONTEXT context;
        memset(&context, 0, sizeof(context));
        context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        context.Rbp = offsets[i] < 4 ? saved_rbp : 0;
        context.Rdi = offsets[i] < 5 ? saved_rdi : 0;
        unsigned pushed = offsets[i] < 4 ? 0 : offsets[i] < 5 ? 1 : 2;
        unsigned allocated = offsets[i] >= 12 ? 0xd8 / 8 : 0;
        context.Rsp = (DWORD64)(uintptr_t)(caller_stack - pushed - allocated);
        context.Rip = (DWORD64)(uintptr_t)(wbm_page + WBM_TRAMPOLINE + offsets[i]);
        PVOID handler_data = NULL;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, (DWORD64)(uintptr_t)wbm_page, context.Rip,
                         &wbm_unwind_function, &context, &handler_data, &establisher, NULL);
        assert(context.Rdi == saved_rdi && context.Rbp == saved_rbp);
        assert(context.Rip == return_address && context.Rsp == (DWORD64)(uintptr_t)(caller_stack + 1));
    }
    wbm_entry(ours);
    assert(wbm_switch(wbm_original, ours, 0));
    wbm_installed = wbm_ever_armed = 1;
    InterlockedExchange(&wbm_enabled, 1);
    assert(wbm_state() == 1 && wbm_owned());
    wbm_restore();
    assert(wbm_state() == 0 && wbm_restored && wbm_owned());
    stats.active = 1;
    ui_thread = GetCurrentThreadId();
    assert(!wbm_arm()); /* Never rearm after STOP. */

    /* A successful CAS followed by failed RX close remains owned and retryable. */
    wbm_protect = test_protect;
    fail_rx_close = 1;
    InterlockedExchange(&wbm_stop_requested, 0);
    InterlockedExchange(&wbm_enabled, 0);
    assert(!wbm_switch(wbm_original, ours, 1));
    assert(wbm_state() == 1 && wbm_rx_uncertain && !wbm_enabled);
    wbm_installed = 1;
    wbm_restore();
    assert(!wbm_restored || wbm_cleanup_failed);
    assert(!wbm_owned());
    fail_rx_close = 0;
    wbm_restore();
    assert(wbm_state() == 0 && wbm_restored && wbm_owned());

    /* Never overwrite a foreign patch, even when a previous patch was ours. */
    assert(VirtualProtect(image + WBM_ALLOCATE, 16, PAGE_EXECUTE_READWRITE, &old));
    memcpy(foreign, wbm_original, sizeof(foreign));
    foreign[0] ^= 1;
    memcpy(image + WBM_ALLOCATE, foreign, sizeof(foreign));
    assert(VirtualProtect(image + WBM_ALLOCATE, 16, PAGE_EXECUTE_READ, &old));
    assert(!wbm_switch(wbm_original, ours, 0));
    assert(wbm_rx(image + WBM_ALLOCATE));
    assert(!memcmp(image + WBM_ALLOCATE, foreign, sizeof(foreign)));
    wbm_installed = 1;
    wbm_restore();
    assert(wbm_cleanup_failed && !wbm_owned());
    assert(!memcmp(image + WBM_ALLOCATE, foreign, sizeof(foreign)));
}

static HANDLE original_entered, original_release;
static unsigned char blocked_body[0x80], blocked_settings[0x80];
static unsigned char blocked_shape[0x40], blocked_inner[0x30], blocked_record[0x40], blocked_motion[0x60];

static void *blocked_original(void *manager, const void *settings) {
    (void)manager;
    (void)settings;
    SetEvent(original_entered);
    assert(WaitForSingleObject(original_release, 5000) == WAIT_OBJECT_0);
    return blocked_body;
}

static DWORD WINAPI run_blocked_allocate(void *unused) {
    (void)unused;
    return wbm_allocate(NULL, blocked_settings) == blocked_body ? 0 : 1;
}

static void test_stop_during_allocator(void) {
    float inverse_mass = 0.25f;
    void *vtable = image + WBM_VTABLE;
    put_pointer(blocked_body + 0x40, blocked_shape);
    put_pointer(blocked_body + 0x48, blocked_motion);
    put_pointer(blocked_shape, vtable);
    put_pointer(blocked_shape + 0x20, blocked_inner);
    put_pointer(blocked_settings + 0x40, blocked_record);
    blocked_settings[0x60] = 2;
    blocked_inner[0x18] = 1;
    blocked_inner[0x19] = 8;
    blocked_record[0x3a] = 1;
    memcpy(blocked_motion + 0x58, &inverse_mass, 4);
    original_entered = CreateEventW(NULL, TRUE, FALSE, NULL);
    original_release = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(original_entered && original_release);
    wbm_original_override = blocked_original;
    InterlockedExchange(&wbm_enabled, 1);
    HANDLE thread = CreateThread(NULL, 0, run_blocked_allocate, NULL, 0, NULL);
    assert(thread && WaitForSingleObject(original_entered, 5000) == WAIT_OBJECT_0);
    LONG64 seen = wbm_seen;
    wbm_restore();
    assert(!wbm_enabled);
    SetEvent(original_release);
    assert(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    DWORD exit_code = 1;
    assert(GetExitCodeThread(thread, &exit_code) && exit_code == 0);
    assert(wbm_seen == seen);
    unsigned char zero[32] = {0};
    assert(!memcmp(blocked_motion + 0x20, zero, sizeof(zero)));
    CloseHandle(thread);
    CloseHandle(original_entered);
    CloseHandle(original_release);
    wbm_original_override = NULL;
}

static HANDLE phase_entered, phase_release;
static int blocked_phase;

static void wait_at_phase(int phase) {
    if (phase != blocked_phase) {
        return;
    }
    SetEvent(phase_entered);
    assert(WaitForSingleObject(phase_release, 5000) == WAIT_OBJECT_0);
}

static DWORD WINAPI run_arm(void *unused) {
    (void)unused;
    return wbm_arm() ? 1 : 0;
}

static DWORD WINAPI run_stop(void *unused) {
    (void)unused;
    wbm_restore();
    return 0;
}

static void test_stop_during_arm(void) {
    DWORD old;
    assert(VirtualProtect(image + WBM_ALLOCATE, 16, PAGE_EXECUTE_READWRITE, &old));
    memcpy(image + WBM_ALLOCATE, wbm_original, 16);
    assert(VirtualProtect(image + WBM_ALLOCATE, 16, PAGE_EXECUTE_READ, &old));
    wbm_test_skip_pins = 1;
    wbm_test_phase = wait_at_phase;
    stats.active = 1;
    stats.issued = 0;
    boot_mode = 0;
    for (int phase = 1; phase <= 3; phase++) {
        blocked_phase = phase;
        wbm_installed = wbm_ever_armed = wbm_restored = wbm_cleanup_failed = 0;
        wbm_rx_uncertain = wbm_cache_uncertain = 0;
        InterlockedExchange(&wbm_stop_requested, 0);
        InterlockedExchange(&wbm_enabled, 0);
        phase_entered = CreateEventW(NULL, TRUE, FALSE, NULL);
        phase_release = CreateEventW(NULL, TRUE, FALSE, NULL);
        assert(phase_entered && phase_release);
        HANDLE arm = CreateThread(NULL, 0, run_arm, NULL, CREATE_SUSPENDED, NULL);
        assert(arm);
        ui_thread = GetThreadId(arm);
        assert(ui_thread && ResumeThread(arm) != (DWORD)-1);
        assert(WaitForSingleObject(phase_entered, 5000) == WAIT_OBJECT_0);
        HANDLE stop = CreateThread(NULL, 0, run_stop, NULL, 0, NULL);
        assert(stop);
        for (unsigned i = 0; i < 5000 && !InterlockedCompareExchange(&wbm_stop_requested, 0, 0); i++) {
            Sleep(1);
        }
        assert(wbm_stop_requested);
        SetEvent(phase_release);
        assert(WaitForSingleObject(arm, 5000) == WAIT_OBJECT_0);
        assert(WaitForSingleObject(stop, 5000) == WAIT_OBJECT_0);
        DWORD result = 1;
        assert(GetExitCodeThread(arm, &result) && result == 0);
        assert(!wbm_enabled && !wbm_installed && wbm_state() == 0);
        assert(wbm_rx(image + WBM_ALLOCATE));
        CloseHandle(arm);
        CloseHandle(stop);
        CloseHandle(phase_entered);
        CloseHandle(phase_release);
    }
    wbm_test_phase = NULL;
    wbm_test_skip_pins = 0;
}

static LONG NTAPI fake_enumeration_error(HANDLE process, HANDLE cursor, ACCESS_MASK access,
                                         ULONG attributes, ULONG flags, PHANDLE found) {
    (void)process;
    (void)cursor;
    (void)access;
    (void)attributes;
    (void)flags;
    (void)found;
    return (LONG)0xc0000001u;
}

static LONG NTAPI fake_enumeration_one(HANDLE process, HANDLE cursor, ACCESS_MASK access,
                                       ULONG attributes, ULONG flags, PHANDLE found) {
    (void)process;
    (void)access;
    (void)attributes;
    (void)flags;
    if (cursor) {
        return (LONG)0x8000001au;
    }
    assert(DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                           found, 0, FALSE, DUPLICATE_SAME_ACCESS));
    return 0;
}

static DWORD WINAPI fake_unknown_thread(HANDLE thread) {
    (void)thread;
    SetLastError(4321);
    return 0;
}
static DWORD WINAPI fake_other_thread(HANDLE thread) {
    (void)thread;
    return GetCurrentThreadId() + 1;
}
static DWORD WINAPI fake_suspend_failure(HANDLE thread) {
    (void)thread;
    SetLastError(1234);
    return (DWORD)-1;
}

static DWORD WINAPI fake_wait_failure(HANDLE thread, DWORD timeout) {
    (void)thread;
    (void)timeout;
    SetLastError(5678);
    return WAIT_FAILED;
}

static HANDLE exited_thread;
static DWORD WINAPI exit_immediately(void *unused) {
    (void)unused;
    return 0;
}

static LONG NTAPI fake_enumerate_exited(HANDLE process, HANDLE cursor, ACCESS_MASK access,
                                        ULONG attributes, ULONG flags, PHANDLE found) {
    (void)process;
    (void)access;
    (void)attributes;
    (void)flags;
    if (cursor) {
        /* NtGetNextThread requires the preceding cursor to remain open. */
        assert(WaitForSingleObject(cursor, 0) == WAIT_OBJECT_0);
        return (LONG)0x8000001au;
    }
    assert(DuplicateHandle(GetCurrentProcess(), exited_thread, GetCurrentProcess(),
                           found, 0, FALSE, DUPLICATE_SAME_ACCESS));
    return 0;
}

static LONG NTAPI fake_no_threads(HANDLE process, HANDLE cursor, ACCESS_MASK access,
                                  ULONG attributes, ULONG flags, PHANDLE found);

static void test_freeze_failures(void) {
    pmath_test_next = fake_enumeration_error;
    assert(!pmath_freeze() && pmath_frozen_count == 0);
    assert(pmath_freeze_diag_stage == PMATH_FREEZE_ENUM && (DWORD)pmath_freeze_diag_ntstatus == 0xc0000001u);
    assert(pmath_freeze_diag_pass == 1 && pmath_freeze_diag_seen == 0 && pmath_freeze_diag_suspended == 0);
    pmath_test_next = fake_enumeration_one;
    pmath_test_thread_id = fake_unknown_thread;
    assert(!pmath_freeze() && pmath_frozen_count == 0);
    assert(pmath_freeze_diag_stage == PMATH_FREEZE_THREAD_ID && pmath_freeze_diag_winerror == 4321);
    assert(pmath_freeze_diag_thread == 0 && pmath_freeze_diag_seen == 1);
    pmath_test_thread_id = fake_other_thread;
    pmath_test_suspend = fake_suspend_failure;
    assert(!pmath_freeze() && pmath_frozen_count == 0);
    assert(pmath_freeze_diag_stage == PMATH_FREEZE_SUSPEND && pmath_freeze_diag_winerror == 1234);
    assert((DWORD)pmath_freeze_diag_thread == GetCurrentThreadId() + 1 && pmath_freeze_diag_seen == 1);
    assert((DWORD)pmath_freeze_diag_wait == WAIT_TIMEOUT && pmath_freeze_diag_wait_error == 0);
    pmath_test_wait = fake_wait_failure;
    assert(!pmath_freeze() && pmath_frozen_count == 0);
    assert(pmath_freeze_diag_stage == PMATH_FREEZE_SUSPEND && pmath_freeze_diag_winerror == 1234);
    assert((DWORD)pmath_freeze_diag_wait == WAIT_FAILED && pmath_freeze_diag_wait_error == 5678);
    pmath_test_wait = NULL;
    char status[768];
    assert(pmath_status(status, sizeof(status)) > 0 && strstr(status, "freeze=suspend"));
    assert(strstr(status, "freeze_held=0 freeze_nt=00000000 freeze_win=1234"));
    assert(strstr(status, "freeze_wait=ffffffff freeze_wait_win=5678 freeze_exited=0"));

    exited_thread = CreateThread(NULL, 0, exit_immediately, NULL, 0, NULL);
    assert(exited_thread && WaitForSingleObject(exited_thread, 5000) == WAIT_OBJECT_0);
    pmath_test_next = fake_enumerate_exited;
    pmath_test_thread_id = NULL;
    assert(pmath_freeze());
    assert(pmath_freeze_diag_stage == PMATH_FREEZE_OK && pmath_freeze_diag_exited == 1);
    assert((DWORD)pmath_freeze_diag_wait == WAIT_OBJECT_0 && pmath_frozen_count == 0);
    assert(pmath_thaw() && pmath_cleanup_clear());
    assert(CloseHandle(exited_thread));
    pmath_test_next = fake_enumeration_error;
    pmath_test_thread_id = NULL;
    pmath_test_suspend = NULL;
    assert(!pmath_switch_once(1));
    assert(pmath_freeze_diag_held && pmath_freeze_diag_stage == PMATH_FREEZE_ENUM);
    pmath_test_next = fake_no_threads;
    assert(pmath_freeze());
    assert(pmath_thaw());
    assert(pmath_freeze_diag_held && pmath_freeze_diag_stage == PMATH_FREEZE_ENUM);
    assert((DWORD)pmath_freeze_diag_ntstatus == 0xc0000001u);
    InterlockedExchange(&pmath_freeze_diag_held, 0);
    pmath_test_next = NULL;
}

static LONG NTAPI fake_no_threads(HANDLE process, HANDLE cursor, ACCESS_MASK access,
                                  ULONG attributes, ULONG flags, PHANDLE found) {
    (void)process;
    (void)cursor;
    (void)access;
    (void)attributes;
    (void)flags;
    (void)found;
    return (LONG)0x8000001au;
}

static void test_freeze_restore_reporting(void) {
    void *hook = (void *)fpenv_thread_start;
    memcpy(image + FPENV_HOOK_PTR, &hook, sizeof(hook));
    fpenv_installed = 1;
    fpenv_restored = fpenv_hook_restored = fpenv_thread_restore_uncertain = 0;
    fpenv_mode = FPENV_DEFAULT;
    fpenv_record_count = 0;
    fpenv_ui_before = fpenv_getcsr();
    ui_thread = GetCurrentThreadId();
    fpenv_last_sample = fpenv_samples = fpenv_failures = 0;
    pmath_test_next = fake_enumeration_error;
    assert(!fpenv_tick(1) && fpenv_failures == 1);
    fpenv_restore();
    assert(fpenv_installed && !fpenv_restored && fpenv_hook_restored);
    assert(fpenv_thread_restore_uncertain && !gate_freeze_hooks_restored());
    assert(fpenv_hook_state() == 0);
    pmath_test_next = fake_no_threads;
    fpenv_restore();
    assert(!fpenv_installed && fpenv_restored && !fpenv_thread_restore_uncertain);
    assert(gate_freeze_hooks_restored());

    /* A frozen-set failure leaves an installed path hook visible to STOP. */
    memcpy(image + PBUD_BLOCK, pbud_ours, PBUD_SIZE);
    pbud_installed = 1;
    pmath_test_next = fake_enumeration_error;
    pbud_restore();
    assert(pbud_installed && !gate_freeze_hooks_restored());
    pbud_installed = 0;
    pmath_test_next = NULL;
    assert(gate_freeze_hooks_restored());
    spo_installed = 1;
    assert(!gate_freeze_hooks_restored());
    spo_installed = 0;
    nrw_installed = 1;
    assert(!gate_freeze_hooks_restored());
    nrw_installed = 0;
    wai_installed = 1;
    assert(!gate_freeze_hooks_restored());
    wai_installed = 0;
    scw_installed = 1;
    assert(!gate_freeze_hooks_restored());
    scw_installed = 0;
    scw_xform_on = 1;
    assert(!gate_freeze_hooks_restored());
    scw_xform_on = 0;
}

static DWORD WINAPI test_resume_failure(HANDLE thread) {
    (void)thread;
    SetLastError(654);
    return (DWORD)-1;
}
static BOOL WINAPI test_close_failure(HANDLE handle) {
    (void)handle;
    SetLastError(321);
    return FALSE;
}
static DWORD WINAPI test_worker_exits(void *unused) {
    (void)unused;
    return 0;
}

static void test_thaw_retains_and_retries(void) {
    HANDLE worker = CreateThread(NULL, 0, test_worker_exits, NULL, CREATE_SUSPENDED, NULL);
    assert(worker);
    pmath_test_resume = test_resume_failure;
    AcquireSRWLockExclusive(&pmath_freeze_lock);
    pmath_frozen[0] = worker;
    pmath_frozen_count = 1;
    assert(!pmath_thaw());
    assert(!pmath_cleanup_clear() && pmath_pending_count == 1 && pmath_pending[0].needs_resume);
    assert(pmath_release_diag_stage == PMATH_RELEASE_RESUME && pmath_release_diag_winerror == 654);
    assert((DWORD)pmath_release_diag_wait == WAIT_TIMEOUT);
    assert(!gate_freeze_hooks_restored());
    pmath_test_next = fake_no_threads;
    assert(!pmath_freeze()); /* The still-live suspended thread remains retained. */
    assert(pmath_pending_count == 1 && pmath_pending[0].needs_resume);
    pmath_test_resume = NULL;
    pmath_test_close = test_close_failure;
    assert(!pmath_freeze()); /* Resumed successfully, but retain the unclosed handle. */
    assert(pmath_pending_count == 1 && !pmath_pending[0].needs_resume);
    assert(pmath_release_diag_stage == PMATH_RELEASE_CLOSE && pmath_release_diag_winerror == 321);
    assert(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0);
    pmath_test_close = NULL;
    assert(pmath_freeze());
    assert(pmath_pending_count == 0 && pmath_cleanup_clear());
    assert(pmath_thaw() && gate_freeze_hooks_restored());

    /* A confirmed exited thread does not need another resume. */
    worker = CreateThread(NULL, 0, test_worker_exits, NULL, 0, NULL);
    assert(worker && WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0);
    pmath_test_resume = test_resume_failure;
    AcquireSRWLockExclusive(&pmath_freeze_lock);
    pmath_frozen[0] = worker;
    pmath_frozen_count = 1;
    assert(pmath_thaw());
    assert(pmath_cleanup_clear() && !pmath_pending_count);
    pmath_test_resume = NULL;

    /* Failure to close an enumeration-only handle is also retained. */
    pmath_test_next = fake_enumeration_one;
    pmath_test_close = test_close_failure;
    assert(!pmath_freeze());
    assert(pmath_pending_count == 1 && !pmath_pending[0].needs_resume);
    pmath_test_close = NULL;
    pmath_test_next = fake_no_threads;
    assert(pmath_freeze() && pmath_thaw());
    assert(pmath_cleanup_clear() && !pmath_pending_count);
    pmath_test_next = NULL;
}

static HANDLE thaw_worker, thaw_worker_release;
static DWORD WINAPI wait_for_thaw(void *unused) {
    (void)unused;
    return WaitForSingleObject(thaw_worker_release, 5000) == WAIT_OBJECT_0 ? 0 : 1;
}
static LONG NTAPI enumerate_thaw_worker(HANDLE process, HANDLE cursor, ACCESS_MASK access,
                                       ULONG attributes, ULONG flags, PHANDLE found) {
    (void)process;
    (void)access;
    (void)attributes;
    (void)flags;
    if (cursor) {
        return (LONG)0x8000001au;
    }
    assert(DuplicateHandle(GetCurrentProcess(), thaw_worker, GetCurrentProcess(),
                           found, 0, FALSE, DUPLICATE_SAME_ACCESS));
    return 0;
}
static void test_arm_failed_thaw_keeps_patch_owned(void) {
    thaw_worker_release = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(thaw_worker_release);
    thaw_worker = CreateThread(NULL, 0, wait_for_thaw, NULL, 0, NULL);
    assert(thaw_worker);
    wbm_installed = wbm_ever_armed = wbm_restored = wbm_cleanup_failed = 0;
    wbm_rx_uncertain = wbm_cache_uncertain = 0;
    InterlockedExchange(&wbm_stop_requested, 0);
    InterlockedExchange(&wbm_enabled, 0);
    wbm_test_skip_pins = 1;
    stats.active = 1;
    stats.issued = 0;
    boot_mode = 0;
    ui_thread = GetCurrentThreadId();
    pmath_test_next = enumerate_thaw_worker;
    pmath_test_resume = test_resume_failure;
    assert(!wbm_arm());
    assert(!wbm_enabled && wbm_installed && wbm_state() == 1 && !wbm_clean());
    assert(pmath_pending_count == 1 && pmath_pending[0].needs_resume);
    pmath_test_resume = NULL;
    pmath_test_next = fake_no_threads;
    wbm_restore();
    assert(!wbm_installed && wbm_state() == 0 && wbm_clean());
    SetEvent(thaw_worker_release);
    assert(WaitForSingleObject(thaw_worker, 5000) == WAIT_OBJECT_0);
    CloseHandle(thaw_worker);
    CloseHandle(thaw_worker_release);
    pmath_test_next = NULL;
    wbm_test_skip_pins = 0;
}

static HANDLE locked_worker_ready, locked_worker_release;
static DWORD WINAPI hold_wbm_locks(void *unused) {
    (void)unused;
    AcquireSRWLockExclusive(&wbm_lifecycle_lock);
    AcquireSRWLockExclusive(&wbm_apply_lock);
    SetEvent(locked_worker_ready);
    DWORD result = WaitForSingleObject(locked_worker_release, 5000);
    ReleaseSRWLockExclusive(&wbm_apply_lock);
    ReleaseSRWLockExclusive(&wbm_lifecycle_lock);
    return result == WAIT_OBJECT_0 ? 0 : 1;
}
static void test_stop_recovers_suspended_lock_owner(void) {
    locked_worker_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    locked_worker_release = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(locked_worker_ready && locked_worker_release);
    HANDLE worker = CreateThread(NULL, 0, hold_wbm_locks, NULL, 0, NULL);
    assert(worker && WaitForSingleObject(locked_worker_ready, 5000) == WAIT_OBJECT_0);
    assert(SuspendThread(worker) != (DWORD)-1);
    HANDLE retained = NULL;
    assert(DuplicateHandle(GetCurrentProcess(), worker, GetCurrentProcess(), &retained,
                           0, FALSE, DUPLICATE_SAME_ACCESS));
    AcquireSRWLockExclusive(&pmath_freeze_lock);
    pmath_retain(retained, 1);
    ReleaseSRWLockExclusive(&pmath_freeze_lock);
    pmath_test_resume = test_resume_failure;
    wbm_ever_armed = 1;
    wbm_installed = 0;
    InterlockedExchange(&wbm_cleanup_failed, 0);
    InterlockedExchange(&wbm_enabled, 1);
    wbm_restore(); /* Bounded failure, not a permanent wait for the suspended owner. */
    assert(!wbm_enabled && wbm_stop_requested);
    assert(wbm_cleanup_failed && !wbm_clean() && pmath_pending_count == 1);
    SetEvent(locked_worker_release);
    pmath_test_resume = NULL;
    pmath_test_next = fake_no_threads;
    wbm_restore(); /* Retry resumes the owner, then waits for both locks. */
    assert(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0);
    assert(wbm_clean() && pmath_cleanup_clear() && !pmath_pending_count);
    CloseHandle(worker);
    CloseHandle(locked_worker_ready);
    CloseHandle(locked_worker_release);
    pmath_test_next = NULL;
}

int main(void) {
    size_t image_size = (size_t)FPENV_HOOK_PTR + 0x2000;
    image = VirtualAlloc(NULL, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(image);
    test_freeze_failures();
    test_freeze_restore_reporting();
    test_thaw_retains_and_retries();
    test_fallback_filter();
    test_lifecycle();
    test_stop_during_allocator();
    test_stop_during_arm();
    test_arm_failed_thaw_keeps_patch_owned();
    test_stop_recovers_suspended_lock_owner();
    return 0;
}

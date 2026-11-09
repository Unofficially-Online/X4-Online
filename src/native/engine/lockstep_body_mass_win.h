/* Opt-in creation-time fallback for X4's second physics bodies on Windows.
 * Historical captures showed Windows creating empty compounds, but the 2026-11-09
 * mixed-platform capture also showed populated compounds there. The option now
 * normalizes both platforms, without changing the state fields used by the digest.
 *
 * The allocator's first 16 bytes form a complete Windows x64 prologue. Its original
 * RUNTIME_FUNCTION describes exactly this interval; the copied prologue uses the
 * same UNWIND_INFO and a permanent dynamically registered function table entry.
 * The continuation uses the image's own chained unwind entries. Neither the page
 * nor its function table are released: a loader may still be executing there at STOP.
 */
#define WBM_ALLOCATE 0x13c4540u
#define WBM_DEFINED_MASS 0x0b5a10u
#define WBM_VTABLE 0x2a2b0b0u
#define WBM_STOP_REASON 204u
#define WBM_TRAMPOLINE 0x20u
#define WBM_UNWIND 0x50u
#define WBM_PAGE_SIZE 0x1000u

static const unsigned char wbm_original[16] = {
    0x48, 0x8b, 0xc4, 0x55, 0x57, 0x48, 0x81, 0xec,
    0xd8, 0x00, 0x00, 0x00, 0x80, 0x7a, 0x62, 0x00
};
static const unsigned char wbm_unwind_info[12] = {
    0x01, 0x0c, 0x04, 0x00, 0x0c, 0x01,
    0x1b, 0x00, 0x05, 0x70, 0x04, 0x50
};
typedef void *(*wbm_allocate_fn)(void *, const void *);
static unsigned char *wbm_page;
static RUNTIME_FUNCTION wbm_unwind_function;
static SRWLOCK wbm_apply_lock = SRWLOCK_INIT;
static SRWLOCK wbm_lifecycle_lock = SRWLOCK_INIT;
static volatile LONG wbm_stop_requested;
static volatile LONG wbm_enabled;
static volatile LONG64 wbm_seen, wbm_already, wbm_changed, wbm_updated, wbm_failures, wbm_write_uncertain;
static int wbm_installed, wbm_restored, wbm_ever_armed, wbm_unwind_added, wbm_unwind_verified;
static int wbm_rx_uncertain, wbm_cache_uncertain;
static volatile LONG wbm_cleanup_failed;
static BOOL (WINAPI *wbm_protect)(LPVOID, SIZE_T, DWORD, PDWORD) = VirtualProtect;
static DWORD wbm_saved_protection;
static const char *wbm_stage = "idle";
static wbm_allocate_fn wbm_original_override;
#ifdef WBM_TEST
static void (*wbm_test_phase)(int);
static int wbm_test_skip_pins;
static int (*wbm_test_write)(void *, const void *, size_t);
static void wbm_phase(int phase) {
    if (wbm_test_phase) {
        wbm_test_phase(phase);
    }
}
#else
static void wbm_phase(int phase) {
    (void)phase;
}
#endif

/* A failed thaw can leave a lock owner suspended. Retry its retained handle
 * while waiting, and report failure instead of blocking STOP indefinitely. */
static int wbm_acquire(SRWLOCK *lock) {
    ULONGLONG started = GetTickCount64();
    do {
        if (TryAcquireSRWLockExclusive(lock)) {
            return 1;
        }
        (void)pmath_retry_pending();
        Sleep(1);
    } while (GetTickCount64() - started < 1000);
    return 0;
}

static int wbm_write_motion(void *destination, const void *source, size_t length) {
    int ok = 1;
#ifdef WBM_TEST
    if (wbm_test_write) {
        return wbm_test_write(destination, source, length);
    }
#endif
    __try {
        sample_copy_fn(destination, source, length);
    } __except((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                GetExceptionCode() == EXCEPTION_GUARD_PAGE) ?
               EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        ok = 0;
    }
    return ok;
}

static void wbm_apply(unsigned char *body, const unsigned char *settings) {
    unsigned char kind = 0, fixed = 0, types[2] = {0, 0};
    unsigned char *shape = NULL, *inner = NULL, *record = NULL, *motion = NULL;
    void *vtable = NULL;
    uint32_t words[8], inverse_mass_bits;
    float inverse_mass;

    if (!sample_read(settings + 0x60, &kind, 1)) {
        InterlockedIncrement64(&wbm_failures);
        return;
    }
    if (kind != 2) {
        return;
    }
    if (!sample_read(body + 0x40, &shape, 8) || !shape || !sample_read(shape, &vtable, 8)) {
        InterlockedIncrement64(&wbm_failures);
        return;
    }
    if (vtable != image + WBM_VTABLE) {
        return;
    }
    if (!sample_read(shape + 0x20, &inner, 8) || !inner || !sample_read(inner + 0x18, types, 2)) {
        InterlockedIncrement64(&wbm_failures);
        return;
    }
    if (types[0] != 1 || types[1] != 8) {
        return;
    }
    if (!sample_read(settings + 0x40, &record, 8) || !record ||
        !sample_read(record + 0x3a, &fixed, 1)) {
        InterlockedIncrement64(&wbm_failures);
        return;
    }
    if (fixed != 1) {
        InterlockedIncrement64(&wbm_updated);
        return;
    }
    if (!sample_read(body + 0x48, &motion, 8) || !motion ||
        !sample_read(motion + 0x20, words, sizeof(words)) ||
        !sample_read(motion + 0x58, &inverse_mass_bits, 4)) {
        InterlockedIncrement64(&wbm_failures);
        return;
    }
    InterlockedIncrement64(&wbm_seen);
    memcpy(&inverse_mass, &inverse_mass_bits, sizeof(inverse_mass));
    if (!isfinite(inverse_mass) || inverse_mass <= 0.0f || !isfinite(2.5f * inverse_mass)) {
        InterlockedIncrement64(&wbm_failures);
        return;
    }
    if (body_mass_is_fallback(words, inverse_mass_bits)) {
        InterlockedIncrement64(&wbm_already);
        return;
    }
    const float diagonal = 2.5f * inverse_mass;
    const float fallback[8] = {diagonal, diagonal, diagonal, diagonal, 0.0f, 0.0f, 0.0f, 1.0f};
    if (!wbm_write_motion(motion + 0x20, fallback, sizeof(fallback))) {
        /* A faulting copy may have written a prefix. Do not claim clean restore. */
        InterlockedIncrement64(&wbm_write_uncertain);
        InterlockedIncrement64(&wbm_failures);
        return;
    }
    InterlockedIncrement64(&wbm_changed);
}

static __attribute__((noinline)) void *wbm_allocate(void *manager, const void *settings) {
    wbm_allocate_fn original = wbm_original_override ? wbm_original_override :
        (wbm_allocate_fn)(void *)(wbm_page + WBM_TRAMPOLINE);
    void *body = original(manager, settings);
    DWORD last_error = GetLastError();
    AcquireSRWLockShared(&wbm_apply_lock);
    __try {
        if (wbm_enabled && !InterlockedCompareExchange(&wbm_stop_requested, 0, 0) && body && settings)
            wbm_apply(body, settings);
    } __finally {
        ReleaseSRWLockShared(&wbm_apply_lock);
    }
    SetLastError(last_error);
    return body;
}

static void wbm_jump(unsigned char *out, const void *target) {
    out[0] = 0xff;
    out[1] = 0x25;
    memset(out + 2, 0, 4);
    memcpy(out + 6, &target, 8);
}

/* The exact allocator does not use R11; it is volatile under the Windows x64 ABI.
 * A register jump also avoids the RIP-indirect form being mistaken for an epilogue
 * by RtlVirtualUnwind at the continuation boundary. */
static void wbm_trampoline_jump(unsigned char out[13], const void *target) {
    out[0] = 0x49;
    out[1] = 0xbb;
    memcpy(out + 2, &target, 8);
    out[10] = 0x41;
    out[11] = 0xff;
    out[12] = 0xe3;
}

static void wbm_entry(unsigned char out[16]) {
    wbm_jump(out, (const void *)wbm_allocate);
    memcpy(out + 14, wbm_original + 14, 2);
}

static int wbm_state(void) {
    unsigned char now[16], ours[16];
    if (!image || !sample_read(image + WBM_ALLOCATE, now, sizeof(now))) {
        return -1;
    }
    if (!memcmp(now, wbm_original, sizeof(now))) {
        return 0;
    }
    wbm_entry(ours);
    return wbm_page && !memcmp(now, ours, sizeof(now)) ? 1 : -1;
}

static int wbm_rx(void *address) {
    MEMORY_BASIC_INFORMATION memory;
    return VirtualQuery(address, &memory, sizeof(memory)) == sizeof(memory) &&
        memory.State == MEM_COMMIT && memory.Protect == PAGE_EXECUTE_READ;
}

static int wbm_page_ok(void) {
    unsigned char expected[29];
    if (!wbm_page || !wbm_rx(wbm_page)) {
        return 0;
    }
    memcpy(expected, wbm_original, 16);
    wbm_trampoline_jump(expected + 16, image + WBM_ALLOCATE + 16);
    return !memcmp(wbm_page + WBM_TRAMPOLINE, expected, sizeof(expected)) &&
        !memcmp(wbm_page + WBM_UNWIND, wbm_unwind_info, sizeof(wbm_unwind_info));
}

static int wbm_pins(void) {
    unsigned char body[64];
    void *vtable_entry = NULL;
    if (!sample_read(image + WBM_ALLOCATE, body, sizeof(body))) {
        return 0;
    }
    if (wbm_state() == 1) {
        memcpy(body, wbm_original, sizeof(wbm_original));
    }
    return hash_ok(body, sizeof(body), "e235835124ec7b2eebf4308b8ac8bfec142847ba1bd6e896cae0cd925a30a892") &&
        hash_ok(image + WBM_DEFINED_MASS, 64, "453f1c137e0ff6870418c21f17812fadf3604737927e399f6cd173b5472a37b6") &&
        sample_read(image + WBM_VTABLE + 7 * 8, &vtable_entry, 8) && vtable_entry == image + WBM_DEFINED_MASS;
}

static int wbm_prepare(void) {
    DWORD old;
    DWORD64 base = 0;
    if (wbm_page) {
        return wbm_unwind_added && wbm_unwind_verified && wbm_page_ok();
    }
    wbm_page = VirtualAlloc(NULL, WBM_PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!wbm_page) {
        return 0;
    }
    memset(wbm_page, 0xcc, WBM_PAGE_SIZE);
    memcpy(wbm_page + WBM_TRAMPOLINE, wbm_original, sizeof(wbm_original));
    wbm_trampoline_jump(wbm_page + WBM_TRAMPOLINE + 16, image + WBM_ALLOCATE + 16);
    memcpy(wbm_page + WBM_UNWIND, wbm_unwind_info, sizeof(wbm_unwind_info));
    if (!VirtualProtect(wbm_page, WBM_PAGE_SIZE, PAGE_EXECUTE_READ, &old) ||
        !FlushInstructionCache(GetCurrentProcess(), wbm_page, WBM_PAGE_SIZE) || !wbm_page_ok()) return 0;
    wbm_unwind_function.BeginAddress = WBM_TRAMPOLINE;
    wbm_unwind_function.EndAddress = WBM_TRAMPOLINE + 29;
    wbm_unwind_function.UnwindData = WBM_UNWIND;
    if (!RtlAddFunctionTable(&wbm_unwind_function, 1, (DWORD64)(uintptr_t)wbm_page)) {
        return 0;
    }
    wbm_unwind_added = 1;
    wbm_unwind_verified = RtlLookupFunctionEntry((DWORD64)(uintptr_t)(wbm_page + WBM_TRAMPOLINE + 16), &base, NULL) ==
        &wbm_unwind_function && base == (DWORD64)(uintptr_t)wbm_page;
    return wbm_unwind_verified;
}

static int wbm_window_clear(void) {
    uintptr_t entry = (uintptr_t)(image + WBM_ALLOCATE);
    for (unsigned i = 0; i < pmath_frozen_count; i++) {
        CONTEXT context;
        memset(&context, 0, sizeof(context));
        context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(pmath_frozen[i], &context)) {
            return 0;
        }
        if (context.Rip > entry && context.Rip < entry + 16) {
            return 0;
        }
    }
    return 1;
}

/* Return success only when both the byte swap and RX restoration succeeded.
 * A successful CAS is recorded even if closing RX fails, so STOP can retry it. */
static int wbm_switch(const unsigned char from[16], const unsigned char to[16], int publish) {
    unsigned __int128 expected, desired, actual;
    DWORD old, ignored;
    int swapped = 0, closed = 0;
    /* A resumed allocator must wait until all thread releases are known. */
    if (publish && !wbm_acquire(&wbm_apply_lock)) {
        return 0;
    }
    if (!pmath_freeze()) {
        if (publish) {
            ReleaseSRWLockExclusive(&wbm_apply_lock);
        }
        return 0;
    }
    if (!wbm_window_clear() || !wbm_rx(image + WBM_ALLOCATE)) {
        goto done;
    }
    if (!wbm_protect(image + WBM_ALLOCATE, 16, PAGE_EXECUTE_READWRITE, &old)) {
        goto done;
    }
    wbm_saved_protection = old;
    wbm_rx_uncertain = 1;
    memcpy(&expected, from, 16);
    memcpy(&desired, to, 16);
    actual = __sync_val_compare_and_swap((unsigned __int128 *)(image + WBM_ALLOCATE), expected, desired);
    swapped = actual == expected;
    closed = wbm_protect(image + WBM_ALLOCATE, 16, old, &ignored) && wbm_rx(image + WBM_ALLOCATE);
    if (closed) {
        wbm_rx_uncertain = 0;
    }
    wbm_cache_uncertain = !FlushInstructionCache(GetCurrentProcess(), image + WBM_ALLOCATE, 16);
    if (wbm_cache_uncertain) {
        closed = 0;
    }
done:
    int thawed = pmath_thaw();
    if (publish) {
        if (swapped && closed && thawed && !InterlockedCompareExchange(&wbm_stop_requested, 0, 0)) {
            InterlockedExchange(&wbm_enabled, 1);
            if (InterlockedCompareExchange(&wbm_stop_requested, 0, 0)) {
                InterlockedExchange(&wbm_enabled, 0);
            }
        }
        ReleaseSRWLockExclusive(&wbm_apply_lock);
    }
    return swapped && closed && thawed;
}

static void wbm_restore_locked(void) {
    unsigned char ours[16];
    DWORD ignored;
    if (!wbm_ever_armed && !wbm_rx_uncertain && !wbm_installed) {
        if (pmath_cleanup_clear()) {
            InterlockedExchange(&wbm_cleanup_failed, 0);
        }
        return;
    }
    if (!wbm_acquire(&wbm_apply_lock)) {
        InterlockedExchange(&wbm_cleanup_failed, 1);
        return;
    }
    InterlockedExchange(&wbm_enabled, 0);
    ReleaseSRWLockExclusive(&wbm_apply_lock);
    if (wbm_rx_uncertain && wbm_protect(image + WBM_ALLOCATE, 16, wbm_saved_protection, &ignored) &&
        wbm_rx(image + WBM_ALLOCATE)) wbm_rx_uncertain = 0;
    if (wbm_state() == 1) {
        wbm_entry(ours);
        (void)wbm_switch(ours, wbm_original, 0);
    }
    if (wbm_cache_uncertain && FlushInstructionCache(GetCurrentProcess(), image + WBM_ALLOCATE, 16))
        wbm_cache_uncertain = 0;
    if (wbm_state() == 0 && !wbm_rx_uncertain && !wbm_cache_uncertain && wbm_rx(image + WBM_ALLOCATE)) {
        wbm_installed = 0;
        InterlockedExchange(&wbm_cleanup_failed, 0);
        wbm_restored = 1;
        wbm_stage = "restored";
    } else {
        InterlockedExchange(&wbm_cleanup_failed, 1);
        wbm_restored = 0;
        wbm_stage = "restore_failed";
    }
}

static void wbm_stop_begin(void) {
    InterlockedExchange(&wbm_stop_requested, 1);
    InterlockedExchange(&wbm_enabled, 0);
}

static void wbm_restore(void) {
    wbm_stop_begin();
    if (!wbm_acquire(&wbm_lifecycle_lock)) {
        InterlockedExchange(&wbm_cleanup_failed, 1);
        return;
    }
    wbm_restore_locked();
    ReleaseSRWLockExclusive(&wbm_lifecycle_lock);
}

/* Only before the first gate STOP in a process. A stopped session and its
 * possible in-flight loader calls must never be confused with a later one. */
static int wbm_arm_locked(void) {
    unsigned char ours[16];
    HMODULE module;
    if (wbm_stop_requested) {
        return 0;
    }
    if (wbm_installed) {
        return stats.active && wbm_enabled && wbm_state() == 1 && !wbm_rx_uncertain;
    }
    if (wbm_ever_armed || !stats.active || stats.issued || (boot_mode && boot_phase) ||
        GetCurrentThreadId() != ui_thread || !IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128)) {
        wbm_stage = "guard";
        return 0;
    }
    wbm_phase(1);
#ifdef WBM_TEST
    int pins_ok = wbm_test_skip_pins || wbm_pins();
#else
    int pins_ok = wbm_pins();
#endif
    if (wbm_state() != 0 || !wbm_rx(image + WBM_ALLOCATE) || !pins_ok || !wbm_prepare()) {
        wbm_stage = "pins_or_page";
        return 0;
    }
    /* The wrapper and registered function record also have to outlive in-flight calls. */
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)(const void *)wbm_allocate, &module)) {
        wbm_stage = "module_pin";
        return 0;
    }
    if (wbm_stop_requested) {
        wbm_stage = "stopped";
        return 0;
    }
    wbm_entry(ours);
    wbm_ever_armed = 1;
    wbm_restored = 0;
    wbm_phase(2);
    if (!wbm_switch(wbm_original, ours, 1)) {
        wbm_installed = wbm_state() == 1;
        wbm_restore_locked();
        wbm_stage = "install_failed";
        return 0;
    }
    wbm_installed = 1;
    wbm_phase(3);
    if (wbm_stop_requested) {
        wbm_restore_locked();
        wbm_stage = "stopped";
        return 0;
    }
    wbm_stage = "armed";
    return 1;
}

static int wbm_arm(void) {
    int ok;
    if (!wbm_acquire(&wbm_lifecycle_lock)) {
        return 0;
    }
    ok = wbm_arm_locked();
    ReleaseSRWLockExclusive(&wbm_lifecycle_lock);
    return ok;
}

static int wbm_owned(void) {
    return pmath_cleanup_clear() && !wbm_rx_uncertain && !wbm_cache_uncertain && !InterlockedCompareExchange(&wbm_cleanup_failed, 0, 0) && (!wbm_installed ||
        (wbm_state() == 1 && wbm_rx(image + WBM_ALLOCATE) && wbm_page_ok()));
}

static int wbm_check(void) {
    return wbm_owned() && wbm_pins() && InterlockedCompareExchange64(&wbm_failures, 0, 0) == 0;
}

static int wbm_clean(void) {
    return pmath_cleanup_clear() && !wbm_installed && !InterlockedCompareExchange(&wbm_cleanup_failed, 0, 0) && !wbm_rx_uncertain && !wbm_cache_uncertain &&
        !InterlockedCompareExchange64(&wbm_write_uncertain, 0, 0);
}

static int wbm_status(char *out, size_t size) {
    int n = snprintf(out, size, "tick=%llu installed=%d restored=%d seen=%lld already=%lld changed=%lld updated=%lld failures=%lld state=%d rx_uncertain=%d cache_uncertain=%d cleanup_failed=%d write_uncertain=%lld stage=%s",
        (unsigned long long)stats.issued, wbm_installed, wbm_restored,
        (long long)InterlockedCompareExchange64(&wbm_seen, 0, 0),
        (long long)InterlockedCompareExchange64(&wbm_already, 0, 0),
        (long long)InterlockedCompareExchange64(&wbm_changed, 0, 0),
        (long long)InterlockedCompareExchange64(&wbm_updated, 0, 0),
        (long long)InterlockedCompareExchange64(&wbm_failures, 0, 0), wbm_state(), wbm_rx_uncertain, wbm_cache_uncertain, (int)InterlockedCompareExchange(&wbm_cleanup_failed, 0, 0),
        (long long)InterlockedCompareExchange64(&wbm_write_uncertain, 0, 0), wbm_stage);
    return n > 0 && (size_t)n < size ? n : 0;
}

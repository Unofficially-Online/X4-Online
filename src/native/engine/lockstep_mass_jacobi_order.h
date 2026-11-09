/* DEFAULT OFF: native_gate_mass_jacobi_order = true.
 * Pinned Windows Jacobi theta: (0.5 / a) * h -> (0.5 * h) / a, matching Linux.
 * Only two scalar SSE instructions exchange order. The intervening MOVAPS,
 * registers, flags, ABI, and all four inverse-inertia output lanes stay intact.
 * Synthetic copied-image evidence does not establish gameplay agreement.
 *
 * Install before snapshot load, under the shared process-thread freeze. Refuse
 * suspended RIPs strictly inside the complete 12-byte instruction window. A
 * thread at its start can safely execute the new sequence. No trampoline.
 * Foreign bytes are never overwritten. Failed writes, cache flushes, RX closes,
 * and thread releases remain visible through STOP. Like start_gate's `used`
 * guard, the STOP marker is permanent for this process; UI reload is idempotent.
 * Requires the checked shared freeze/release interface from 437c921. */
#define MASS_JACOBI_ORDER_STOP_REASON 211u
#define MASS_JACOBI_FUNCTION_RVA 0x1404f40u
#define MASS_JACOBI_FUNCTION_SIZE 2507u
#define MASS_JACOBI_FUNCTION_SHA "cc66a0c7b176437991b2dc243ffff119dd65f8cac9470244ad25f6cf403a4af9"
#define MASS_JACOBI_BLOCK_RVA 0x1405277u
#define MASS_JACOBI_BLOCK_SIZE 23u
#define MASS_JACOBI_PATCH_OFFSET 7u
#define MASS_JACOBI_PATCH_SIZE 12u
#define MASS_JACOBI_PATCH_RVA (MASS_JACOBI_BLOCK_RVA + MASS_JACOBI_PATCH_OFFSET)

static const unsigned char mass_jacobi_original[MASS_JACOBI_BLOCK_SIZE] = {
    0x41, 0x0f, 0x28, 0xd6, 0x0f, 0x57, 0xc9,
    0xf3, 0x0f, 0x5e, 0xd4, 0x41, 0x0f, 0x28, 0xda, 0xf3, 0x0f, 0x59, 0xd5,
    0x41, 0x0f, 0x2f, 0xd5
};
static const unsigned char mass_jacobi_reordered[MASS_JACOBI_BLOCK_SIZE] = {
    0x41, 0x0f, 0x28, 0xd6, 0x0f, 0x57, 0xc9,
    0xf3, 0x0f, 0x59, 0xd5, 0x41, 0x0f, 0x28, 0xda, 0xf3, 0x0f, 0x5e, 0xd4,
    0x41, 0x0f, 0x2f, 0xd5
};
static SRWLOCK mass_jacobi_lifecycle_lock = SRWLOCK_INIT;
static volatile LONG mass_jacobi_stop_requested;
static volatile LONG mass_jacobi_cleanup_uncertain;
static volatile LONG mass_jacobi_installed;
static volatile LONG mass_jacobi_restored;
static volatile LONG mass_jacobi_dirty;
static volatile LONG mass_jacobi_rx_pending;
static volatile LONG mass_jacobi_cache_pending;
static volatile LONG mass_jacobi_known = 1;
static volatile LONG mass_jacobi_failures;
static unsigned char mass_jacobi_owned_bytes[MASS_JACOBI_PATCH_SIZE];
static DWORD mass_jacobi_saved_protection;
static unsigned mass_jacobi_attempts;
static unsigned mass_jacobi_busy;
static const char *volatile mass_jacobi_stage = "idle";
static void mass_jacobi_restore(void);

static int mass_jacobi_stopping(void) {
    return InterlockedCompareExchange(&mass_jacobi_stop_requested, 0, 0) != 0;
}
static void mass_jacobi_request_stop(void) {
    InterlockedExchange(&mass_jacobi_stop_requested, 1);
}
static void mass_jacobi_fail(const char *stage) {
    mass_jacobi_stage = stage;
    InterlockedIncrement(&mass_jacobi_failures);
}
/* A shared-freeze failure can leave the lifecycle owner suspended. Never wait
 * indefinitely for it; retry retained release obligations without blocking. */
static int mass_jacobi_acquire(void) {
    for (unsigned attempt = 0; attempt < 200; attempt++) {
        if (TryAcquireSRWLockExclusive(&mass_jacobi_lifecycle_lock)) return 1;
        (void)pmath_retry_pending();
        Sleep(1);
    }
    InterlockedExchange(&mass_jacobi_cleanup_uncertain, 1);
    mass_jacobi_fail("lifecycle_busy");
    return 0;
}
static int mass_jacobi_block_matches(const unsigned char *middle) {
    const unsigned char *now = image + MASS_JACOBI_BLOCK_RVA;
    return !memcmp(now, mass_jacobi_original, MASS_JACOBI_PATCH_OFFSET) &&
        !memcmp(now + MASS_JACOBI_PATCH_OFFSET, middle, MASS_JACOBI_PATCH_SIZE) &&
        !memcmp(now + MASS_JACOBI_PATCH_OFFSET + MASS_JACOBI_PATCH_SIZE,
                mass_jacobi_original + MASS_JACOBI_PATCH_OFFSET + MASS_JACOBI_PATCH_SIZE, 4);
}
static int mass_jacobi_state(void) {
    if (!image) return -1;
    if (mass_jacobi_block_matches(mass_jacobi_original + MASS_JACOBI_PATCH_OFFSET)) return 0;
    if (mass_jacobi_block_matches(mass_jacobi_reordered + MASS_JACOBI_PATCH_OFFSET)) return 1;
    return -1;
}
static int mass_jacobi_protection(DWORD expected) {
    MEMORY_BASIC_INFORMATION memory;
    const uintptr_t start = (uintptr_t)(image + MASS_JACOBI_PATCH_RVA);
    if (VirtualQuery((void *)start, &memory, sizeof(memory)) != sizeof(memory)) return 0;
    const uintptr_t region = (uintptr_t)memory.BaseAddress;
    return memory.State == MEM_COMMIT && memory.Protect == expected &&
        start >= region && start - region <= memory.RegionSize &&
        memory.RegionSize - (start - region) >= MASS_JACOBI_PATCH_SIZE;
}
static int mass_jacobi_overlaps(const void *source, size_t bytes) {
    if (!mass_jacobi_dirty || !image || !source || !bytes) return 0;
    const uintptr_t at = (uintptr_t)source;
    const uintptr_t low = (uintptr_t)(image + MASS_JACOBI_PATCH_RVA);
    return at < low + MASS_JACOBI_PATCH_SIZE && (at >= low || bytes > low - at);
}
static void mass_jacobi_unpatch_copy(const void *source, void *copy, size_t bytes) {
    if (!mass_jacobi_overlaps(source, bytes) || mass_jacobi_state() != 1) return;
    const uintptr_t at = (uintptr_t)source;
    const uintptr_t low = (uintptr_t)(image + MASS_JACOBI_PATCH_RVA);
    for (unsigned i = 0; i < MASS_JACOBI_PATCH_SIZE; i++) {
        if (low + i >= at && low + i - at < bytes) {
            ((unsigned char *)copy)[low + i - at] = mass_jacobi_original[MASS_JACOBI_PATCH_OFFSET + i];
        }
    }
}
static int mass_jacobi_pins(void) {
    unsigned char block[MASS_JACOBI_BLOCK_SIZE];
    return image && read_local(image + MASS_JACOBI_BLOCK_RVA, block, sizeof(block)) &&
        !memcmp(block, mass_jacobi_original, sizeof(block)) &&
        hash_ok(image + MASS_JACOBI_FUNCTION_RVA, MASS_JACOBI_FUNCTION_SIZE, MASS_JACOBI_FUNCTION_SHA);
}
/* Shared freeze is held. Check the whole instruction window, including the
 * middle MOVAPS and the second scalar operation, not only modified opcodes. */
static int mass_jacobi_window_clear(void) {
    const uintptr_t start = (uintptr_t)(image + MASS_JACOBI_PATCH_RVA);
    for (unsigned i = 0; i < pmath_frozen_count; i++) {
        CONTEXT context;
        memset(&context, 0, sizeof(context));
        context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(pmath_frozen[i], &context)) return -1;
        if (context.Rip > start && context.Rip < start + MASS_JACOBI_PATCH_SIZE) return 0;
    }
    return 1;
}
/* Remember only an exact, reported prefix of our write. An unexpected byte is
 * foreign/unknown; neither rollback nor STOP may overwrite it. */
static int mass_jacobi_record_write(const unsigned char *before, const unsigned char *after, SIZE_T written) {
    unsigned char expected[MASS_JACOBI_PATCH_SIZE];
    if (written <= MASS_JACOBI_PATCH_SIZE) {
        memcpy(expected, before, sizeof(expected));
        memcpy(expected, after, written);
        if (mass_jacobi_block_matches(expected)) {
            memcpy(mass_jacobi_owned_bytes, expected, sizeof(expected));
            mass_jacobi_known = 1;
            mass_jacobi_dirty = memcmp(expected, mass_jacobi_original + MASS_JACOBI_PATCH_OFFSET, sizeof(expected)) != 0;
            return 1;
        }
    }
    mass_jacobi_known = 0;
    mass_jacobi_dirty = 1;
    return 0;
}
static int mass_jacobi_flush(void) {
    if (!FlushInstructionCache(GetCurrentProcess(), image + MASS_JACOBI_PATCH_RVA, MASS_JACOBI_PATCH_SIZE)) return 0;
    mass_jacobi_cache_pending = 0;
    return 1;
}
/* Lifecycle lock and shared freeze are held. No heap, BCrypt, or loader calls.
 * Even failed arm retains enough state for STOP to retry byte/RX/cache cleanup. */
static int mass_jacobi_write(int install) {
    unsigned char before[MASS_JACOBI_PATCH_SIZE];
    const unsigned char *after = (install ? mass_jacobi_reordered : mass_jacobi_original) + MASS_JACOBI_PATCH_OFFSET;
    memcpy(before, install ? mass_jacobi_original + MASS_JACOBI_PATCH_OFFSET : mass_jacobi_owned_bytes, sizeof(before));
    if (!mass_jacobi_known || !mass_jacobi_block_matches(before)) {
        mass_jacobi_fail(install ? "foreign" : "restore_foreign");
        return 0;
    }
    if (!mass_jacobi_protection(PAGE_EXECUTE_READ) &&
        !(mass_jacobi_rx_pending && mass_jacobi_protection(PAGE_EXECUTE_READWRITE))) {
        mass_jacobi_fail("protect_foreign");
        return 0;
    }
    if (install && mass_jacobi_stopping()) return 0;
    DWORD previous;
    DWORD ignored;
    if (!VirtualProtect(image + MASS_JACOBI_PATCH_RVA, MASS_JACOBI_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &previous)) {
        mass_jacobi_fail("protect_write");
        return 0;
    }
    if (!mass_jacobi_rx_pending) mass_jacobi_saved_protection = previous;
    mass_jacobi_rx_pending = 1;
    /* A protection change between query and VirtualProtect is foreign too. */
    if (mass_jacobi_saved_protection != PAGE_EXECUTE_READ) {
        InterlockedExchange(&mass_jacobi_cleanup_uncertain, 1);
        mass_jacobi_fail("protect_changed");
        if (VirtualProtect(image + MASS_JACOBI_PATCH_RVA, MASS_JACOBI_PATCH_SIZE, previous, &ignored)) {
            mass_jacobi_rx_pending = 0;
        }
        return 0;
    }
    SIZE_T written = 0;
    const int wrote = WriteProcessMemory(GetCurrentProcess(), image + MASS_JACOBI_PATCH_RVA,
                                         after, MASS_JACOBI_PATCH_SIZE, &written) != 0;
    if (written) mass_jacobi_cache_pending = 1;
    const int known = mass_jacobi_record_write(before, after, written);
    int ok = wrote && written == MASS_JACOBI_PATCH_SIZE && known;
    if (!ok) mass_jacobi_fail(known ? "write_partial" : "write_foreign");
    if (ok && !mass_jacobi_flush()) {
        ok = 0;
        mass_jacobi_fail("flush");
    }
    if (!ok && known && memcmp(mass_jacobi_owned_bytes, before, sizeof(before))) {
        unsigned char partial[MASS_JACOBI_PATCH_SIZE];
        memcpy(partial, mass_jacobi_owned_bytes, sizeof(partial));
        SIZE_T rolled = 0;
        const int rollback = WriteProcessMemory(GetCurrentProcess(), image + MASS_JACOBI_PATCH_RVA,
                                               before, sizeof(before), &rolled) != 0;
        if (rolled) mass_jacobi_cache_pending = 1;
        const int rollback_known = mass_jacobi_record_write(partial, before, rolled);
        if (!rollback || rolled != sizeof(before) || !rollback_known || !mass_jacobi_flush()) {
            mass_jacobi_fail("rollback_failed");
        }
    }
    if (!VirtualProtect(image + MASS_JACOBI_PATCH_RVA, MASS_JACOBI_PATCH_SIZE,
                        mass_jacobi_saved_protection, &ignored) || !mass_jacobi_protection(PAGE_EXECUTE_READ)) {
        mass_jacobi_fail("protect_restore");
        return 0;
    }
    mass_jacobi_rx_pending = 0;
    return ok;
}
static int mass_jacobi_thaw(void) {
    if (pmath_thaw() && pmath_cleanup_clear()) return 1;
    InterlockedExchange(&mass_jacobi_cleanup_uncertain, 1);
    mass_jacobi_fail("thaw_pending");
    return 0;
}
/* Lifecycle lock remains held through byte changes and publication. STOP sets
 * its atomic marker before waiting on that lock, so arm cannot publish later. */
static int mass_jacobi_switch(int install) {
    for (unsigned attempt = 0; attempt < 200; attempt++) {
        if (install && mass_jacobi_stopping()) return 0;
        ++mass_jacobi_attempts;
        if (!pmath_freeze()) {
            if (!pmath_cleanup_clear()) InterlockedExchange(&mass_jacobi_cleanup_uncertain, 1);
            mass_jacobi_fail("freeze");
            return 0;
        }
        const int clear = mass_jacobi_window_clear();
        if (clear <= 0) {
            if (!mass_jacobi_thaw()) return 0;
            if (clear < 0) {
                mass_jacobi_fail("context");
                return 0;
            }
            ++mass_jacobi_busy;
            Sleep(1);
            continue;
        }
        int result = mass_jacobi_write(install);
        if (install && mass_jacobi_stopping()) {
            if (mass_jacobi_dirty || mass_jacobi_rx_pending || mass_jacobi_cache_pending) {
                (void)mass_jacobi_write(0);
            }
            result = 0;
        }
        if (!mass_jacobi_thaw()) return 0;
        return result;
    }
    mass_jacobi_stage = "busy";
    return 0;
}
static int mass_jacobi_clean(void) {
    return !mass_jacobi_installed && !mass_jacobi_dirty && !mass_jacobi_rx_pending &&
        !mass_jacobi_cache_pending && !InterlockedCompareExchange(&mass_jacobi_cleanup_uncertain, 0, 0) &&
        pmath_cleanup_clear();
}
static int mass_jacobi_owned_locked(void) {
    if (!mass_jacobi_installed) {
        return !mass_jacobi_dirty && !mass_jacobi_rx_pending && !mass_jacobi_cache_pending &&
            !InterlockedCompareExchange(&mass_jacobi_cleanup_uncertain, 0, 0);
    }
    return !mass_jacobi_stopping() && mass_jacobi_known && !mass_jacobi_rx_pending &&
        !mass_jacobi_cache_pending && !InterlockedCompareExchange(&mass_jacobi_cleanup_uncertain, 0, 0) &&
        pmath_cleanup_clear() && mass_jacobi_state() == 1 && mass_jacobi_protection(PAGE_EXECUTE_READ);
}
static int mass_jacobi_check_locked(void) {
    if (InterlockedCompareExchange(&mass_jacobi_failures, 0, 0)) return 0;
    if (!mass_jacobi_owned_locked()) {
        mass_jacobi_fail("site_changed");
        return 0;
    }
    return 1;
}
/* A resumed gate thread must not inspect dirty/install publication midway
 * through thaw. It waits on the same bounded lock as arm and STOP. */
static int mass_jacobi_owned(void) {
    if (!mass_jacobi_acquire()) return 0;
    const int result = mass_jacobi_owned_locked();
    ReleaseSRWLockExclusive(&mass_jacobi_lifecycle_lock);
    return result;
}
static int mass_jacobi_check(void) {
    if (!mass_jacobi_acquire()) return 0;
    const int result = mass_jacobi_check_locked();
    ReleaseSRWLockExclusive(&mass_jacobi_lifecycle_lock);
    return result;
}
static void mass_jacobi_restore_locked(void) {
    if (!pmath_retry_pending()) {
        InterlockedExchange(&mass_jacobi_cleanup_uncertain, 1);
        mass_jacobi_fail("cleanup_pending");
        return;
    }
    if (mass_jacobi_installed || mass_jacobi_dirty || mass_jacobi_rx_pending || mass_jacobi_cache_pending) {
        if (!mass_jacobi_switch(0)) return;
        mass_jacobi_installed = 0;
        mass_jacobi_restored = 1;
        mass_jacobi_stage = "restored";
    }
    if (mass_jacobi_cleanup_uncertain && mass_jacobi_attempts &&
        (mass_jacobi_state() != 0 || !mass_jacobi_protection(PAGE_EXECUTE_READ))) {
        mass_jacobi_fail("cleanup_foreign");
        return;
    }
    if (pmath_cleanup_clear()) InterlockedExchange(&mass_jacobi_cleanup_uncertain, 0);
}
static int mass_jacobi_arm_locked(void) {
    if (mass_jacobi_stopping() || !stats.active || GetCurrentThreadId() != ui_thread) {
        mass_jacobi_stage = "state";
        return 0;
    }
    if (mass_jacobi_installed) return mass_jacobi_check_locked();
    if (stats.issued || !boot_mode || boot_phase || !mass_jacobi_clean()) {
        mass_jacobi_stage = "state";
        return 0;
    }
    if (!mass_jacobi_pins() || mass_jacobi_state() != 0 || !mass_jacobi_protection(PAGE_EXECUTE_READ)) {
        mass_jacobi_stage = "pins";
        return 0;
    }
    if (mass_jacobi_stopping()) return 0;
    mass_jacobi_known = 1;
    mass_jacobi_failures = 0;
    mass_jacobi_restored = 0;
    memcpy(mass_jacobi_owned_bytes, mass_jacobi_original + MASS_JACOBI_PATCH_OFFSET, MASS_JACOBI_PATCH_SIZE);
    if (!mass_jacobi_switch(1)) {
        if (mass_jacobi_stopping()) mass_jacobi_restore_locked();
        return 0;
    }
    if (mass_jacobi_stopping()) {
        mass_jacobi_restore_locked();
        return 0;
    }
    InterlockedExchange(&mass_jacobi_installed, 1);
    if (mass_jacobi_stopping()) {
        mass_jacobi_restore_locked();
        return 0;
    }
    mass_jacobi_stage = "armed";
    return 1;
}
static int mass_jacobi_arm(void) {
    if (mass_jacobi_stopping() || !mass_jacobi_acquire()) return 0;
    int result = mass_jacobi_arm_locked();
    if (mass_jacobi_stopping()) {
        mass_jacobi_restore_locked();
        result = 0;
    }
    ReleaseSRWLockExclusive(&mass_jacobi_lifecycle_lock);
    if (mass_jacobi_stopping()) {
        mass_jacobi_restore();
        return 0;
    }
    return result;
}
static void mass_jacobi_restore(void) {
    mass_jacobi_request_stop();
    if (!mass_jacobi_acquire()) return;
    mass_jacobi_restore_locked();
    ReleaseSRWLockExclusive(&mass_jacobi_lifecycle_lock);
}
static int mass_jacobi_status(char *out, size_t capacity) {
    const int n = snprintf(out, capacity,
        "installed=%ld restored=%ld dirty=%ld rx_pending=%ld cache_pending=%ld known=%ld state=%d "
        "stage=%s attempts=%u busy=%u failures=%ld stop=%d cleanup_uncertain=%ld order=half_h_div_a",
        (long)mass_jacobi_installed, (long)mass_jacobi_restored, (long)mass_jacobi_dirty,
        (long)mass_jacobi_rx_pending, (long)mass_jacobi_cache_pending, (long)mass_jacobi_known,
        mass_jacobi_state(), mass_jacobi_stage, mass_jacobi_attempts, mass_jacobi_busy,
        (long)InterlockedCompareExchange(&mass_jacobi_failures, 0, 0), mass_jacobi_stopping(),
        (long)InterlockedCompareExchange(&mass_jacobi_cleanup_uncertain, 0, 0));
    return n > 0 && (size_t)n < capacity ? n : 0;
}

/* Experimental ordered Linux pathing jobs, default off.
 *
 * This diverts only the two pinned RunPass queue calls. The callsites increment
 * manager's outstanding batch count before calling; running the complete job
 * thunk synchronously therefore also performs the native completion and splice.
 * Queue bypass changes scheduler bookkeeping. Known direct category readers do
 * not select these jobs, but indirect readers and transitive TLS effects have
 * not been proven absent; this is an experiment, not a production guarantee.
 * The native snapshot mode is mutually exclusive with this mode. A live
 * dispatch mismatch writes a diagnostic and immediately exits this disposable
 * game process with status 190. It cannot emit a clean STOP or restore hooks;
 * the runner must stop its peer and record this as a failed native run. */
#define LXT_INLINE_SITE_A 0x13e3e08u
#define LXT_INLINE_SITE_B 0x13e3e80u
#define LXT_INLINE_QUEUE 0x1a55930u
#define LXT_INLINE_BODY 0x13e32f0u
#define LXT_INLINE_STUB 0xff0u
_Static_assert(LXT_INLINE_STUB + 14 <= GATE_PAGE, "inline jump fits the thunk page");
#define LXT_INLINE_DOMAIN 0x58344c5452415645ULL

typedef int (*lxt_inline_queue_fn)(void *, void *, void *, unsigned, unsigned);
typedef void (*lxt_inline_job_fn)(void *, void *);
static uint64_t lxt_inline_original_first, lxt_inline_patched_first;
static unsigned __int128 lxt_inline_original_second, lxt_inline_patched_second;
static int lxt_inline_first_installed, lxt_inline_second_installed, lxt_inline_restored;
static uint64_t lxt_inline_tick = UINT64_MAX, lxt_inline_passes, lxt_inline_batches;
static unsigned lxt_inline_ordinal;
static const char *lxt_inline_stage = "idle";

/* Actor hooks are defined later in gate_linux.c. */
static int laa_call_state(void);
static int laa_stub_ok(void);

/* The boot agent hook and optional path budget patch precede this arm call.
 * Normalize only their verified owned bytes in a private copy, then require
 * the original SHA for every byte of the complete batch body. */
static int lxt_inline_body_pin(void) {
    unsigned char body[0x24b];
    memcpy(body, LX(LXT_INLINE_BODY), sizeof(body));
    if (laa_agent_installed) {
        if (laa_call_state() != 1 || !laa_stub_ok()) return 0;
        int32_t original_displacement = (int32_t)(LX_AGENT_UPDATE_VA - (LX_AGENT_CALL_VA + 5));
        memcpy(body + (LX_AGENT_CALL_VA + 1 - LXT_INLINE_BODY),
               &original_displacement, sizeof(original_displacement));
    }
    if (pbud_installed) {
        if (!pbud_owned()) return 0;
        memcpy(body + (LX_PBUD_SITE - LXT_INLINE_BODY), lx_pbud_native, sizeof(lx_pbud_native));
    }
    return hash_ok(body, sizeof(body),
        "3222ca2e412f8df6cc8db1c64295b34b0f0ce1c377a01a6a329430d6cb0f2229");
}

/* Check the fields consumed by the pinned zero-cleanup thunk path. */
static int lxt_inline_payload_ok(const void *payload, const void *body) {
    void *batch = NULL, *function = NULL;
    uint64_t adjustment = 1, cleanup = 1;
    return payload && gate_read_local(payload, &batch, 8) &&
        gate_read_local((const unsigned char *)payload + 8, &function, 8) &&
        gate_read_local((const unsigned char *)payload + 0x10, &adjustment, 8) &&
        gate_read_local((const unsigned char *)payload + 0x20, &cleanup, 8) &&
        batch && function == body && adjustment == 0 && cleanup == 0;
}

static int lxt_inline_pending_ok(void *path_manager) {
    uint32_t pending = 0, count = 0, maximum = 0;
    unsigned char *manager = path_manager;
    return manager && InterlockedCompareExchange(&path_pass_open, 0, 0) &&
        gate_read_local(manager + 0x2e0, &pending, 4) &&
        gate_read_local(manager + 0x350, &count, 4) &&
        gate_read_local(manager + 0x354, &maximum, 4) &&
        pending == 1 && maximum == 1 && count <= 1;
}

static void lxt_inline_fatal(void) {
    static const char diagnostic[] = "X4LAN LINUX_TRAVEL_INLINE_FATAL dispatch mismatch; exiting 190\n";
    (void)write(STDERR_FILENO, diagnostic, sizeof(diagnostic) - 1);
    _exit(190);
}

static int lxt_inline_dispatch(lxt_inline_queue_fn queue, lxt_inline_job_fn job,
                               void *body, unsigned char *tls, void *manager,
                               void *callback, void *payload, unsigned category, unsigned priority) {
    if (!travel_inline_installed || !stats.active || boot_phase != 3)
        return queue(manager, callback, payload, category, priority);
    if (callback != (void *)job || manager != *(void **)LX(LX_FS_JTM_VA) ||
        category != 1 || priority != 0 || !tls ||
        !lxt_inline_pending_ok(*(void **)LX(LX_PATH_MANAGER_VA)) ||
        !lxt_inline_payload_ok(payload, body))
        lxt_inline_fatal();
    if (stats.issued != lxt_inline_tick) {
        lxt_inline_tick = stats.issued;
        lxt_inline_ordinal = 0;
        ++lxt_inline_passes;
    }
    uint64_t old_time, old_previous, old_rng, zero = 0;
    uint32_t old_view, zero_view = 0;
    uint64_t seed = lockstep_seed(LXT_INLINE_DOMAIN, stats.issued, ++lxt_inline_ordinal, 0, 0, 0);
    memcpy(&old_time, tls + GATE_TLS_LINUX_TIME, 8);
    memcpy(&old_previous, tls + GATE_TLS_LINUX_PREVIOUS_TIME, 8);
    memcpy(&old_rng, tls + GATE_TLS_LINUX_RNG, 8);
    memcpy(&old_view, tls + GATE_TLS_LINUX_VIEW, 4);
    memcpy(tls + GATE_TLS_LINUX_TIME, &zero, 8);
    memcpy(tls + GATE_TLS_LINUX_PREVIOUS_TIME, &zero, 8);
    memcpy(tls + GATE_TLS_LINUX_RNG, &seed, 8);
    memcpy(tls + GATE_TLS_LINUX_VIEW, &zero_view, 4);
    job(payload, NULL);
    memcpy(tls + GATE_TLS_LINUX_TIME, &old_time, 8);
    memcpy(tls + GATE_TLS_LINUX_PREVIOUS_TIME, &old_previous, 8);
    memcpy(tls + GATE_TLS_LINUX_RNG, &old_rng, 8);
    memcpy(tls + GATE_TLS_LINUX_VIEW, &old_view, 4);
    ++lxt_inline_batches;
    /* Both pinned callers discard eax; this value is not a scheduler handle. */
    return 0;
}

static int lxt_inline_queue(void *manager, void *callback, void *payload,
                            unsigned category, unsigned priority) {
    return lxt_inline_dispatch((lxt_inline_queue_fn)(void *)LX(LXT_INLINE_QUEUE),
        (lxt_inline_job_fn)(void *)LX(LX_PATH_JOB_VA), LX(LXT_INLINE_BODY),
        gate_tls_self(), manager, callback, payload, category, priority);
}

static void lxt_inline_thunk_write(unsigned char *page) {
    lxw_jump_write(page + LXT_INLINE_STUB, (void *)lxt_inline_queue);
}

static void lxt_inline_restore(void) {
    if (!travel_inline_installed) return;
    if (lxt_inline_first_installed && gate_code_swap64(LX(LXT_INLINE_SITE_A), lxt_inline_patched_first, lxt_inline_original_first))
        lxt_inline_first_installed = 0;
    if (lxt_inline_second_installed && gate_code_swap128(LX(LXT_INLINE_SITE_B), lxt_inline_patched_second, lxt_inline_original_second))
        lxt_inline_second_installed = 0;
    lxt_inline_restored = !lxt_inline_first_installed && !lxt_inline_second_installed;
    if (lxt_inline_restored) travel_inline_installed = 0;
}

static int lxt_inline_owned(void) {
    if (!travel_inline_installed) return 1;
    uint64_t first_word;
    unsigned __int128 second_word;
    memcpy(&first_word, LX(LXT_INLINE_SITE_A), 8);
    memcpy(&second_word, LX(LXT_INLINE_SITE_B), 16);
    return first_word == lxt_inline_patched_first && second_word == lxt_inline_patched_second;
}

static int lxt_inline_arm(void) {
    if (travel_inline_installed)
        return stats.active && !tsnap_installed && lxt_inline_first_installed &&
            lxt_inline_second_installed && lxt_inline_owned();
    lxt_inline_stage = "state";
    if (!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId() != ui_thread ||
        !thunk || !path_installed || !(path_options & 1) || !(path_det_mask & PATH_DET_BUDGET_BIT) || tsnap_installed)
        return 0;
    lxt_inline_stage = "runpass_pin";
    if (!hash_ok(LX(0x13e3d86), 0x116, "bf41201f69cd1aac19d13349ef89eec5ae943392b05a4db95b76dcb6b340579c")) return 0;
    lxt_inline_stage = "batch_pin";
    if (!lxt_inline_body_pin()) return 0;
    lxt_inline_stage = "completion_pins";
    if (!hash_ok(LX(0x13c45c0), 0x36, "8a95d70d88b3e3fcda8d82e34592726184e102a6c5113f865650ecf67baa849b") ||
        !hash_ok(LX(0x13e91d8), 0xa9, "a3634bde30e731d984d0e138d90f2357c9210961c4b5a0ed9b5c3fa845be961f") ||
        !hash_ok(LX(0x1a23520), 0x40, "45527f113535fa1e3702f38c5d5d96b8be073d0f7d814468702b9b0336221e09") ||
        !hash_ok(LX(0x1a23be0), 0x6a, "2f7e3764d17dc9ff4269fd115a22ed3ec472a7612846d55b6b33a0e771c7f7bd"))
        return 0;
    memcpy(&lxt_inline_original_first, LX(LXT_INLINE_SITE_A), 8);
    memcpy(&lxt_inline_original_second, LX(LXT_INLINE_SITE_B), 16);
    lxt_inline_patched_first = lxt_inline_original_first; lxt_inline_patched_second = lxt_inline_original_second;
    lxt_inline_stage = "displacement";
    intptr_t first_distance = (intptr_t)(thunk + LXT_INLINE_STUB) - (intptr_t)LX(0x13e3e0f);
    intptr_t second_distance = (intptr_t)(thunk + LXT_INLINE_STUB) - (intptr_t)LX(0x13e3e89);
    if (first_distance < INT32_MIN || first_distance > INT32_MAX ||
        second_distance < INT32_MIN || second_distance > INT32_MAX) return 0;
    int32_t first_displacement = (int32_t)first_distance;
    int32_t second_displacement = (int32_t)second_distance;
    memcpy((unsigned char *)&lxt_inline_patched_first + 3, &first_displacement, 4);
    memcpy((unsigned char *)&lxt_inline_patched_second + 5, &second_displacement, 4);
    lxt_inline_stage = "swap";
    lxt_inline_restored = 0;
    travel_inline_installed = 1;
    lxt_inline_first_installed = gate_code_swap64(LX(LXT_INLINE_SITE_A), lxt_inline_original_first, lxt_inline_patched_first);
    lxt_inline_second_installed = lxt_inline_first_installed && gate_code_swap128(LX(LXT_INLINE_SITE_B), lxt_inline_original_second, lxt_inline_patched_second);
    if (!lxt_inline_second_installed) { lxt_inline_restore(); return 0; }
    lxt_inline_stage = "armed";
    return 1;
}

static int lua_travel_determinism(lua_State *L) {
    p_lua_pushnumber(L, lxt_inline_arm()); return 1;
}
static int lua_travel_determinism_status(lua_State *L) {
    char out[256];
    int n = accept_ui() ? snprintf(out, sizeof(out),
        "armed=%d installed=%d tick=%llu passes=%llu batches=%u total_batches=%llu restored=%d failures=0 stage=%s",
        travel_inline_installed, travel_inline_installed, (unsigned long long)lxt_inline_tick,
        (unsigned long long)lxt_inline_passes, lxt_inline_ordinal, (unsigned long long)lxt_inline_batches,
        lxt_inline_restored, lxt_inline_stage) : 0;
    if (n > 0 && (size_t)n < sizeof(out)) p_lua_pushlstring(L, out, (size_t)n); else p_lua_pushnil(L);
    return 1;
}

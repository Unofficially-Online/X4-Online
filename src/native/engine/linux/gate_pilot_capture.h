/* Opt-in, read-only pilot channel diagnostic for the pinned Linux X4 build.
 * This is not pilot_inputs and does not construct or transmit a pilot record.
 * See reports/LOCKSTEP-LINUX-PILOT-CAPTURE-ABI-20261109.txt: the six channel
 * meanings and signs are not yet established. No input hook is installed.
 *
 * Include after gate_linux.c defines image, stats, ui_thread and hash_ok.
 * The caller must disarm on session stop and before replacing the image. */
#define LINUX_PILOT_CONTROLLER_VA 0x1dd19d0u
#define LINUX_PILOT_CHANNEL_VA 0x1663740u
#define LINUX_PILOT_CONTROLLER_VTABLE_VA 0x3b7c208u
#define LINUX_PILOT_PIN_BYTES 64u
#define LINUX_PILOT_FIRST_CHANNEL 0x6cu
#define LINUX_PILOT_CHANNELS 6u

static pthread_mutex_t linux_pilot_capture_lock = PTHREAD_MUTEX_INITIALIZER;
/* Atomic lifecycle: 0 ready, 1 stopping, 2 armed. */
static int linux_pilot_capture_state;
static int linux_pilot_capture_armed;
static const char *linux_pilot_capture_reason = "not_armed";
static DWORD linux_pilot_capture_thread;
static unsigned char *linux_pilot_capture_image;
typedef void *(*linux_pilot_controller_fn)(void);
typedef float (*linux_pilot_channel_fn)(void *, unsigned int);
#ifdef GATE_TLS_TEST
/* Fake calls for white-box tests only. Pins and object checks still run. */
static linux_pilot_controller_fn linux_pilot_test_controller;
static linux_pilot_channel_fn linux_pilot_test_channel;
static void (*linux_pilot_test_before_hash)(void);
#endif

static void linux_pilot_capture_set_reason(const char *reason) {
    __atomic_store_n(&linux_pilot_capture_reason, reason, __ATOMIC_SEQ_CST);
}

static const char *linux_pilot_capture_get_reason(void) {
    return __atomic_load_n(&linux_pilot_capture_reason, __ATOMIC_SEQ_CST);
}

/* The caller holds the diagnostic lock. This lock is independent of gate_code_lock. */
static void linux_pilot_capture_clear_locked(void) {
    linux_pilot_capture_armed = 0;
    linux_pilot_capture_set_reason("not_armed");
    linux_pilot_capture_thread = 0;
    linux_pilot_capture_image = NULL;
}

static void linux_pilot_capture_disarm(void) {
    /* Publish STOP before waiting: an in-progress arm must not publish success. */
    __atomic_store_n(&linux_pilot_capture_state, 1, __ATOMIC_SEQ_CST);
    pthread_mutex_lock(&linux_pilot_capture_lock);
    linux_pilot_capture_clear_locked();
    pthread_mutex_unlock(&linux_pilot_capture_lock);
}

/* Called when a new gate session is ready to become active. */
static void linux_pilot_capture_begin(void) {
    pthread_mutex_lock(&linux_pilot_capture_lock);
    linux_pilot_capture_clear_locked();
    __atomic_store_n(&linux_pilot_capture_state, 0, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock(&linux_pilot_capture_lock);
}

static int linux_pilot_capture_pins(void) {
    static const struct { uintptr_t va; const char *sha; } pins[] = {
        {LINUX_PILOT_CONTROLLER_VA, "120b8981a3005df746d47880de89635fbc5cab0aeeb1d0f048778268e35e2aff"},
        {LINUX_PILOT_CHANNEL_VA, "a2ce98004b5a8a12c1f2608894e4dae83c05fa5b5638c60fa9f7815239a29c9e"}
    };
    unsigned char bytes[LINUX_PILOT_PIN_BYTES];
    if (!image) return 0;
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        if (!gate_read_local(LX(pins[i].va), bytes, sizeof(bytes)) ||
            !hash_ok(bytes, sizeof(bytes), pins[i].sha)) return 0;
    }
    return 1;
}

/* Hash the whole executable even if the ordinary gate's test options disable
 * file checks. This probe cannot arm from a synthetic or mismatched image. */
static int linux_pilot_capture_arm_locked(void) {
    unsigned char digest[32];
    char hex[65];
    linux_pilot_capture_clear_locked();
    int prior = __atomic_load_n(&linux_pilot_capture_state, __ATOMIC_SEQ_CST);
    if (prior == 1 || (prior == 2 && !__atomic_compare_exchange_n(&linux_pilot_capture_state, &prior, 0, 0,
                                                                    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))) {
        linux_pilot_capture_set_reason("session"); return 0;
    }
    if (!stats.active) { linux_pilot_capture_set_reason("session"); return 0; }
    if (!ui_thread || GetCurrentThreadId() != ui_thread) { linux_pilot_capture_set_reason("thread"); return 0; }
    if (!image) { linux_pilot_capture_set_reason("image"); return 0; }
#ifdef GATE_TLS_TEST
    if (linux_pilot_test_before_hash) linux_pilot_test_before_hash();
#endif
    if (!gate_env.exe || !gate_file_sha256(gate_env.exe, LX_FILE_SIZE, digest)) {
        linux_pilot_capture_set_reason("file"); return 0;
    }
    hex32(digest, hex);
    if (strcmp(hex, LX_FILE_SHA)) { linux_pilot_capture_set_reason("file"); return 0; }
    if (!linux_pilot_capture_pins()) { linux_pilot_capture_set_reason("pins"); return 0; }
    /* Publication and STOP share one atomic state: whichever wins is first. */
    int ready = 0;
    if (!__atomic_compare_exchange_n(&linux_pilot_capture_state, &ready, 2, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        linux_pilot_capture_set_reason("session"); return 0;
    }
    linux_pilot_capture_image = image;
    linux_pilot_capture_thread = ui_thread;
    linux_pilot_capture_armed = 1;
    linux_pilot_capture_set_reason("ok");
    return 1;
}

/* Writes output only when all six calls succeeded and all values are finite.
 * get_controller() and get_channel() call native game code: the exact-build
 * pins, verified active session, UI thread and controller vptr are prerequisites,
 * not a general fault barrier for arbitrary corrupt engine state. */
static int linux_pilot_capture_read_locked(char *output, size_t capacity) {
    float values[LINUX_PILOT_CHANNELS];
    uintptr_t vtable = 0;
    char text[320];
    size_t used = 0;
    linux_pilot_controller_fn controller_fn;
    linux_pilot_channel_fn channel_fn;
    if (!output || !capacity) { linux_pilot_capture_set_reason("capacity"); return 0; }
    if ((__atomic_load_n(&linux_pilot_capture_state, __ATOMIC_SEQ_CST) == 1)) { linux_pilot_capture_set_reason("session"); return 0; }
    if (!linux_pilot_capture_armed) { linux_pilot_capture_set_reason("not_armed"); return 0; }
    if (!stats.active) { linux_pilot_capture_set_reason("session"); return 0; }
    if (!ui_thread || ui_thread != linux_pilot_capture_thread ||
        GetCurrentThreadId() != linux_pilot_capture_thread) { linux_pilot_capture_set_reason("thread"); return 0; }
    if (image != linux_pilot_capture_image) { linux_pilot_capture_set_reason("image"); return 0; }
    if (!linux_pilot_capture_pins()) { linux_pilot_capture_set_reason("pins"); return 0; }
    controller_fn = (linux_pilot_controller_fn)(void *)LX(LINUX_PILOT_CONTROLLER_VA);
    channel_fn = (linux_pilot_channel_fn)(void *)LX(LINUX_PILOT_CHANNEL_VA);
#ifdef GATE_TLS_TEST
    if (linux_pilot_test_controller) controller_fn = linux_pilot_test_controller;
    if (linux_pilot_test_channel) channel_fn = linux_pilot_test_channel;
#endif
    void *controller = controller_fn();
    if (!controller || !gate_read_local(controller, &vtable, sizeof(vtable)) ||
        vtable != (uintptr_t)LX(LINUX_PILOT_CONTROLLER_VTABLE_VA)) { linux_pilot_capture_set_reason("controller"); return 0; }
    for (unsigned i = 0; i < LINUX_PILOT_CHANNELS; ++i) {
        if ((__atomic_load_n(&linux_pilot_capture_state, __ATOMIC_SEQ_CST) == 1)) { linux_pilot_capture_set_reason("session"); return 0; }
        values[i] = channel_fn(controller, LINUX_PILOT_FIRST_CHANNEL + i);
        if (!isfinite(values[i])) { linux_pilot_capture_set_reason("nonfinite"); return 0; }
    }
    if ((__atomic_load_n(&linux_pilot_capture_state, __ATOMIC_SEQ_CST) == 1)) { linux_pilot_capture_set_reason("session"); return 0; }
    for (unsigned i = 0; i < LINUX_PILOT_CHANNELS; ++i) {
        uint32_t bits;
        memcpy(&bits, &values[i], sizeof(bits));
        int n = snprintf(text + used, sizeof(text) - used, "%s0x%02x=%.9g(0x%08x)",
                         i ? " " : "", LINUX_PILOT_FIRST_CHANNEL + i,
                         (double)values[i], bits);
        if (n < 0 || (size_t)n >= sizeof(text) - used) { linux_pilot_capture_set_reason("capacity"); return 0; }
        used += (size_t)n;
    }
    if (used + 1 > capacity) { linux_pilot_capture_set_reason("capacity"); return 0; }
    memcpy(output, text, used + 1);
    linux_pilot_capture_set_reason("ok");
    return (int)used;
}

/* STOP waits for an in-flight native read; no general gate lock is held. */
static int linux_pilot_capture_arm(void) {
    pthread_mutex_lock(&linux_pilot_capture_lock);
    const int result = linux_pilot_capture_arm_locked();
    pthread_mutex_unlock(&linux_pilot_capture_lock);
    return result;
}

static int linux_pilot_capture_read(char *output, size_t capacity) {
    pthread_mutex_lock(&linux_pilot_capture_lock);
    const int result = linux_pilot_capture_read_locked(output, capacity);
    pthread_mutex_unlock(&linux_pilot_capture_lock);
    return result;
}

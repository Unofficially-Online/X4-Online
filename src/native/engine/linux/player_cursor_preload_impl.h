#ifndef X4_PLAYER_CURSOR_PRELOAD_IMPL_H
#define X4_PLAYER_CURSOR_PRELOAD_IMPL_H
/* Included by the resident pre-main view-query preload. The install helper is
 * deliberately not called until a complete opt-in gate consumer exists. */
#include "player_cursor_preload.h"
#define CURSOR_UPDATE_ENTRY UINT64_C(0x166d020)
#define CURSOR_UPDATE_LENGTH 15106u
#define CURSOR_SITE UINT64_C(0x166d3ac)
#define CURSOR_RESUME UINT64_C(0x166d3b3)
static const unsigned char cursor_original_site[7] = {0x80, 0x3d, 0xc7, 0x06, 0x72, 0x02, 0x00};
static unsigned char cursor_stub_frame[64] __attribute__((aligned(8)));
static _Atomic unsigned cursor_installed, cursor_active, cursor_targets_ready;
static uint32_t cursor_owner;
static uintptr_t cursor_canonical, cursor_guest;
/* Retain every distinct thread/controller pair since pre-main installation.
 * A callback already past this hook may still be executing stock Update when
 * targets are published; counting only callbacks currently inside the hook
 * cannot establish exclusion. Exhaustion fails closed at publication. */
#define CURSOR_HISTORY_MAX 4096
static struct { uintptr_t controller; uint32_t thread; } cursor_history[CURSOR_HISTORY_MAX];
static unsigned cursor_history_count, cursor_history_overflow;
static atomic_flag cursor_history_lock = ATOMIC_FLAG_INIT;
static void cursor_lock(void) { while (atomic_flag_test_and_set_explicit(&cursor_history_lock, memory_order_acquire)) {} }
static void cursor_unlock(void) { atomic_flag_clear_explicit(&cursor_history_lock, memory_order_release); }
static int (*cursor_callback)(uintptr_t);
static unsigned char *cursor_stub;
static unsigned char cursor_patch[7];
static player_cursor_preload_info cursor_information;
extern void player_cursor_native_bridge(void) __attribute__((visibility("hidden")));
extern const unsigned char player_cursor_native_bridge_end[] __attribute__((visibility("hidden")));
__attribute__((visibility("hidden"))) uintptr_t player_cursor_native_resume = CURSOR_RESUME;

/* This callback runs inside stock Update. It makes no engine call. The bound
 * gate callback owns thread/controller/phase authentication and either handles
 * or explicitly ignores the current controller. Never swallow errno. */
__attribute__((visibility("hidden"))) void player_cursor_native_dispatch(uintptr_t controller) {
    int saved_errno = errno;
    uint32_t thread = current_thread();
    cursor_lock();
    unsigned active = atomic_load_explicit(&cursor_active, memory_order_acquire);
    unsigned targets_ready = atomic_load_explicit(&cursor_targets_ready, memory_order_acquire);
    int foreign = active && targets_ready && thread != cursor_owner &&
        (controller == cursor_canonical || controller == cursor_guest);
    if (!targets_ready) {
        unsigned i;
        for (i = 0; i < cursor_history_count; ++i)
            if (cursor_history[i].controller == controller && cursor_history[i].thread == thread) break;
        if (i == cursor_history_count) {
            if (i == CURSOR_HISTORY_MAX) cursor_history_overflow = 1;
            else { cursor_history[i].controller = controller; cursor_history[i].thread = thread; ++cursor_history_count; }
        }
    }
    cursor_unlock();
    if (foreign) {
        static const char reason[] = "X4 CURSOR FATAL reason=foreign_controller\n";
        (void)write(2, reason, sizeof reason - 1);
        _exit(223);
    }
    if (active && thread == cursor_owner && (!cursor_callback || !cursor_callback(controller))) {
        static const char reason[] = "X4 CURSOR FATAL reason=convergence\n";
        (void)write(2, reason, sizeof reason - 1);
        _exit(223);
    }
    errno = saved_errno;
}
EXPORT int x4mp_cursor_info(player_cursor_preload_info *out) {
    if (!out || !atomic_load_explicit(&cursor_installed, memory_order_acquire)) return 0;
    *out = cursor_information;
    return 1;
}
EXPORT int x4mp_cursor_bind(uint32_t owner, int (*callback)(uintptr_t)) {
    if (!atomic_load_explicit(&cursor_installed, memory_order_acquire) ||
        atomic_load_explicit(&cursor_active, memory_order_acquire) || !owner ||
        current_thread() != owner || !callback) return 0;
    cursor_owner = owner;
    cursor_callback = callback;
    atomic_store_explicit(&cursor_active, 1, memory_order_release);
    return 1;
}

/* Serialized with dispatch, including all observations before bind. A past
 * foreign invocation is conservatively rejected even if it has since ended. */
EXPORT int x4mp_cursor_targets(uintptr_t canonical, uintptr_t guest) {
    if (!atomic_load_explicit(&cursor_active, memory_order_acquire) || current_thread() != cursor_owner ||
        !canonical || !guest || canonical == guest) return 0;
    cursor_lock();
    int valid = !atomic_load_explicit(&cursor_targets_ready, memory_order_relaxed) && !cursor_history_overflow;
    for (unsigned i = 0; valid && i < cursor_history_count; ++i)
        if (cursor_history[i].thread != cursor_owner &&
            (cursor_history[i].controller == canonical || cursor_history[i].controller == guest)) valid = 0;
    if (valid) {
        cursor_canonical = canonical;
        cursor_guest = guest;
        atomic_store_explicit(&cursor_targets_ready, 1, memory_order_release);
    }
    cursor_unlock();
    return valid;
}

/* Stack-neutral jump in the middle of Update: its CFA is the stock Update
 * frame, not a function-entry rsp+8 frame. Use the process libgcc registry. */
static int cursor_register_stub_frame(uintptr_t start, unsigned char frame[64]) {
    void *library = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!library) return 0;
    void (*register_frame)(void *) = (void (*)(void *))dlsym(library, "__register_frame");
    if (!register_frame) return 0;
    static const unsigned char cie[32] = {
        28,0,0,0, 0,0,0,0, 1,0,1,0x78,16,
        0x0c,7,0xc0,0x05, 0x90,1, 0x8f,2, 0x8e,3, 0x8d,4,
        0x8c,5, 0x86,6, 0x83,7, 0
    };
    uint32_t length = 20, cie_offset = 36;
    uint64_t address = start, range = 14;
    memset(frame, 0, 64);
    memcpy(frame, cie, sizeof cie);
    memcpy(frame + 32, &length, 4);
    memcpy(frame + 36, &cie_offset, 4);
    memcpy(frame + 40, &address, 8);
    memcpy(frame + 48, &range, 8);
    register_frame(frame);
    return 1;
}

static int cursor_make_patch(uintptr_t site, uintptr_t stub, unsigned char output[7]) {
    if (!output || site > INT64_MAX - 5 || stub > INT64_MAX) return 0;
    int64_t distance = (int64_t)stub - (int64_t)(site + 5);
    if (distance < INT32_MIN || distance > INT32_MAX) return 0;
    int32_t displacement = (int32_t)distance;
    output[0] = 0xe9;
    memcpy(output + 1, &displacement, sizeof displacement);
    output[5] = output[6] = 0x90;
    return 1;
}

/* Constructor only, before any X4-created thread. Unlike the aligned entry
 * patches, this seven-byte instruction cannot use an atomic 64-bit CAS. Any
 * install failure after mprotect or mutation must terminate the process. */
static __attribute__((unused)) int cursor_install_before_main(void) {
    unsigned char digest[32];
    if (!single_thread() || !is_rx(CURSOR_UPDATE_ENTRY) ||
        !is_rx(CURSOR_UPDATE_ENTRY + CURSOR_UPDATE_LENGTH - 1) ||
        memcmp((const void *)CURSOR_SITE, cursor_original_site, sizeof cursor_original_site)) return 0;
    lockstep_sha256((const void *)CURSOR_UPDATE_ENTRY, CURSOR_UPDATE_LENGTH, digest);
    if (!digest_matches(digest, "f5fb3e3afe84b9c4f1ecd7c27088d49e6cf6e84971c2d8af3c05bdba15e4143f")) return 0;
    unsigned char *page = allocate_near();
    if (!page) return 0;
    memset(page, 0xcc, PVQ_PAGE);
    emit_entry_stub(page, (uintptr_t)player_cursor_native_bridge);
    if (mprotect(page, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx((uintptr_t)page) ||
        !cursor_register_stub_frame((uintptr_t)page, cursor_stub_frame)) return 0;
    if (!cursor_make_patch(CURSOR_SITE, (uintptr_t)page, cursor_patch)) return 0;
    uintptr_t base = CURSOR_SITE & ~(uintptr_t)(PVQ_PAGE - 1);
    if (!single_thread() || mprotect((void *)base, PVQ_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC)) return 0;
    /* After opening code permissions, any uncertain effect is fatal to the
     * constructor; caller must not fall back to a partially patched process. */
    if (memcmp((const void *)CURSOR_SITE, cursor_original_site, sizeof cursor_original_site)) return 0;
    memcpy((void *)CURSOR_SITE, cursor_patch, sizeof cursor_patch);
    __builtin___clear_cache((char *)CURSOR_SITE, (char *)CURSOR_SITE + sizeof cursor_patch);
    if (mprotect((void *)base, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx(CURSOR_SITE) ||
        memcmp((const void *)CURSOR_SITE, cursor_patch, sizeof cursor_patch)) return 0;
    cursor_stub = page;
    cursor_information = (player_cursor_preload_info){
        .version = 1, .installed = 1, .site = CURSOR_SITE, .stub = (uintptr_t)page,
        .bridge = (uintptr_t)player_cursor_native_bridge,
        .dispatcher = (uintptr_t)player_cursor_native_dispatch,
        .resume_slot = (uintptr_t)&player_cursor_native_resume,
        .frame = (uintptr_t)cursor_stub_frame, .stub_size = 14,
        .bridge_size = (uint32_t)(player_cursor_native_bridge_end - (const unsigned char *)(uintptr_t)player_cursor_native_bridge),
        .frame_size = sizeof cursor_stub_frame, .page_size = PVQ_PAGE,
    };
    memcpy(cursor_information.original, cursor_original_site, sizeof cursor_original_site);
    memcpy(cursor_information.patch, cursor_patch, sizeof cursor_patch);
    atomic_store_explicit(&cursor_installed, 1, memory_order_release);
    return 1;
}
#endif

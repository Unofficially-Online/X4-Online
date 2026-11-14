#ifndef X4_PLAYER_STOCK_VIEW_FOV_PRELOAD_IMPL_H
#define X4_PLAYER_STOCK_VIEW_FOV_PRELOAD_IMPL_H
/* Included only by the sealed pre-main view-query provider. The caller must
 * terminate on any installer failure, including failed partial restoration.
 * No hot install, unpatch or code-page reclamation is supported. */
#include "player_stock_view_fov_preload.h"
#include "player_stock_view_fov_cfi.h"
#include "player_stock_view_policy_guard.h"

static const uintptr_t stock_view_fov_sites[3] = {0x1666d2a, 0x1666e87, 0x1666d58};
static const uint64_t stock_view_fov_original_words[3] = {
    UINT64_C(0x0ff300000880978b), UINT64_C(0x0ff300000880878b),
    UINT64_C(0x100ff3002bff63e8)
};
static _Atomic unsigned stock_view_fov_ready, stock_view_fov_active, stock_view_fov_inflight;
static player_stock_view_mode_policy stock_view_fov_mode_policy;
static player_stock_view_fov_policy stock_view_fov_value_policy;
static _Atomic unsigned stock_view_fov_observe_active, stock_view_fov_observe_inflight;
static player_stock_view_fov_observer stock_view_fov_observer;
static float (*stock_view_fov_original)(void *) = (float (*)(void *))0x1926cc0;
static _Thread_local unsigned stock_view_fov_depth;
static player_stock_view_fov_info stock_view_fov_information;
static unsigned char stock_view_fov_mode_frames[2][PLAYER_STOCK_VIEW_FOV_FRAME_SIZE] __attribute__((aligned(8)));
static unsigned char stock_view_fov_call_frame[48] __attribute__((aligned(8)));
extern void player_stock_view_mode_edx_bridge(void) __attribute__((visibility("hidden")));
extern void player_stock_view_mode_eax_bridge(void) __attribute__((visibility("hidden")));
extern void player_stock_view_fov_bridge(void) __attribute__((visibility("hidden")));

/* Called by the assembly mode bridges. A policy must explicitly pass foreign
 * controllers through and reject stale/ambiguous owned controllers. */
__attribute__((visibility("hidden"), noinline)) int player_stock_view_mode_dispatch(uintptr_t controller, int original, unsigned site) {
    int saved_errno = errno, result = original;
    unsigned state = stock_view_policy_enter(&stock_view_fov_active, &stock_view_fov_inflight);
    if (state == 1) {
        if (stock_view_fov_depth++ || !stock_view_fov_mode_policy(controller, original, site, &result)) _exit(223);
        --stock_view_fov_depth;
    }
    stock_view_policy_leave(&stock_view_fov_inflight);
    errno = saved_errno;
    return result;
}

__attribute__((visibility("hidden"), noinline)) float player_stock_view_fov_dispatch(void *camera, uintptr_t controller) {
    int before = errno;
    unsigned policy = stock_view_policy_enter(&stock_view_fov_active, &stock_view_fov_inflight);
    unsigned observe = stock_view_policy_enter(&stock_view_fov_observe_active, &stock_view_fov_observe_inflight);
    if ((policy || observe) && stock_view_fov_depth++) _exit(223);
    errno = before;
    float original = stock_view_fov_original(camera);
    int after = errno;
    float result = original;
    if (observe) stock_view_fov_observer(controller, (uintptr_t)camera, original);
    errno = after; /* observer must not alter the existing policy input */
    if (policy && !stock_view_fov_value_policy(controller, original, &result)) _exit(223);
    if (policy || observe) --stock_view_fov_depth;
    stock_view_policy_leave(&stock_view_fov_observe_inflight);
    stock_view_policy_leave(&stock_view_fov_inflight);
    errno = after;
    return result;
}

/* Independent additive observer; never changes the existing policy ABI. */
EXPORT int x4mp_stock_view_fov_observe_bind(player_stock_view_fov_observer observer) {
    if (!observer || !atomic_load_explicit(&stock_view_fov_ready, memory_order_acquire) ||
        !stock_view_policy_begin_bind(&stock_view_fov_observe_active, &stock_view_fov_observe_inflight)) return 0;
    stock_view_fov_observer = observer;
    atomic_store_explicit(&stock_view_fov_observe_active, 1, memory_order_seq_cst);
    return 1;
}

EXPORT int x4mp_stock_view_fov_info(player_stock_view_fov_info *out) {
    if (!out || !atomic_load_explicit(&stock_view_fov_ready, memory_order_acquire)) return 0;
    *out = stock_view_fov_information;
    return 1;
}
EXPORT int x4mp_stock_view_fov_bind(player_stock_view_mode_policy mode, player_stock_view_fov_policy fov) {
    if (!mode || !fov || !atomic_load_explicit(&stock_view_fov_ready, memory_order_acquire)) return 0;
    if (!stock_view_policy_begin_bind(&stock_view_fov_active, &stock_view_fov_inflight)) return 0;
    stock_view_fov_mode_policy = mode;
    stock_view_fov_value_policy = fov;
    atomic_store_explicit(&stock_view_fov_active, 1, memory_order_seq_cst);
    return 1;
}

static int stock_view_fov_register_mode_frame(uintptr_t start, unsigned char *frame) {
    void *library = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!library) return 0;
    void (*register_frame)(void *) = (void (*)(void *))dlsym(library, "__register_frame");
    if (!register_frame) return 0;
    player_stock_view_fov_frame(frame, start);
    register_frame(frame);
    return 1;
}

static int stock_view_fov_patch_word(uintptr_t address, uintptr_t destination, unsigned call, uint64_t original, uint64_t *patched) {
    int64_t distance = (int64_t)destination - (int64_t)(address + 5);
    if (!patched || distance < INT32_MIN || distance > INT32_MAX) return 0;
    unsigned char bytes[8];
    memcpy(bytes, &original, sizeof bytes);
    bytes[0] = call ? 0xe8 : 0xe9;
    int32_t relative = (int32_t)distance;
    memcpy(bytes + 1, &relative, 4);
    if (!call) bytes[5] = 0x90; /* only the six-byte mode load is replaced */
    memcpy(patched, bytes, sizeof bytes);
    return 1;
}

static int install_stock_view_fov(void) {
    unsigned char hash[32];
    if (!single_thread() || !is_rx(0x16666b0) || !is_rx(0x1667184) ||
        !is_rx(0x1926cc0) || !is_rx(0x1926e84)) return 0;
    lockstep_sha256((void *)0x16666b0, 2773, hash);
    if (!digest_matches(hash, "844509812707345ef5b8de3dfb05becfe3eedec23c188b70001546f0db7ae538")) return 0;
    lockstep_sha256((void *)0x1926cc0, 0x1c5, hash);
    if (!digest_matches(hash, "529f00a038c89f27d56d14e5804ee101db07e8ccb8b33492372c6d46d98ab3cc")) return 0;
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned byte = 0; byte < 8; ++byte)
            if (((volatile const unsigned char *)stock_view_fov_sites[i])[byte] !=
                ((const unsigned char *)&stock_view_fov_original_words[i])[byte]) return 0;
    }
    unsigned char *page = allocate_near();
    if (!page) return 0;
    memset(page, 0xcc, PVQ_PAGE);
    uintptr_t bridges[3] = {(uintptr_t)player_stock_view_mode_edx_bridge,
                            (uintptr_t)player_stock_view_mode_eax_bridge,
                            (uintptr_t)player_stock_view_fov_bridge};
    uint64_t patched[3];
    for (unsigned i = 0; i < 3; ++i) {
        emit_entry_stub(page + i * 16, bridges[i]);
        if (!stock_view_fov_patch_word(stock_view_fov_sites[i], (uintptr_t)page + i * 16,
                                       i == 2, stock_view_fov_original_words[i], &patched[i])) return 0;
    }
    if (mprotect(page, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx((uintptr_t)page) ||
        !stock_view_fov_register_mode_frame((uintptr_t)page, stock_view_fov_mode_frames[0]) ||
        !stock_view_fov_register_mode_frame((uintptr_t)page + 16, stock_view_fov_mode_frames[1]) ||
        !register_frame_for_entry((uintptr_t)page + 32, stock_view_fov_call_frame)) return 0;
    uintptr_t text_page = stock_view_fov_sites[0] & ~(uintptr_t)(PVQ_PAGE - 1);
    if (!single_thread() || mprotect((void *)text_page, PVQ_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC)) return 0;
    unsigned installed = 0;
    for (; installed < 3; ++installed) {
#ifdef STOCK_VIEW_FOV_TEST
        stock_view_fov_test_before_cas(installed);
#endif
        uint64_t expected = stock_view_fov_original_words[installed];
        if (!__atomic_compare_exchange_n((uint64_t *)stock_view_fov_sites[installed], &expected,
                                         patched[installed], 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) break;
    }
    if (installed != 3) {
        while (installed) {
            --installed;
            uint64_t expected = patched[installed];
            (void)__atomic_compare_exchange_n((uint64_t *)stock_view_fov_sites[installed], &expected,
                                              stock_view_fov_original_words[installed], 0,
                                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        }
    }
    __builtin___clear_cache((char *)text_page, (char *)(text_page + PVQ_PAGE));
    if (mprotect((void *)text_page, PVQ_PAGE, PROT_READ | PROT_EXEC) || installed != 3 || !is_rx(text_page)) return 0;
    stock_view_fov_information = (player_stock_view_fov_info){
        .version = 2, .installed = 1, .page = (uintptr_t)page,
        .mode_bridge = {bridges[0], bridges[1]}, .fov_bridge = bridges[2],
        .mode_dispatch = (uintptr_t)player_stock_view_mode_dispatch,
        .fov_dispatch = (uintptr_t)player_stock_view_fov_dispatch,
        .mode_frame = {(uintptr_t)stock_view_fov_mode_frames[0], (uintptr_t)stock_view_fov_mode_frames[1]},
        .call_frame = (uintptr_t)stock_view_fov_call_frame, .original_fov_slot = (uintptr_t)&stock_view_fov_original,
    };
    memcpy(stock_view_fov_information.original, stock_view_fov_original_words, sizeof patched);
    memcpy(stock_view_fov_information.patched, patched, sizeof patched);
    atomic_store_explicit(&stock_view_fov_ready, 1, memory_order_release);
    return 1;
}
#endif

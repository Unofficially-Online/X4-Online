#ifndef X4_PLAYER_STOCK_VIEW_SHIP_PRELOAD_IMPL_H
#define X4_PLAYER_STOCK_VIEW_SHIP_PRELOAD_IMPL_H
/* Included by the sealed pre-main provider. Caller must terminate if install
 * fails after any mutation; this resident detour is never hot-uninstalled. */
#include "player_stock_view_ship_preload.h"
#include "player_stock_view_fov_cfi.h"
#include "player_stock_view_policy_guard.h"
static _Atomic unsigned stock_view_ship_ready, stock_view_ship_active, stock_view_ship_inflight;
static player_stock_view_ship_policy stock_view_ship_policy;
static _Thread_local unsigned stock_view_ship_depth;
static player_stock_view_ship_info stock_view_ship_information;
static unsigned char stock_view_ship_frame[PLAYER_STOCK_VIEW_FOV_FRAME_SIZE] __attribute__((aligned(8)));
extern void player_stock_view_ship_bridge(void) __attribute__((visibility("hidden")));

__attribute__((visibility("hidden"), noinline)) int player_stock_view_ship_dispatch(uintptr_t ship, int original) {
    int saved_errno = errno, result = original;
    unsigned state = stock_view_policy_enter(&stock_view_ship_active, &stock_view_ship_inflight);
    if (state == 1) {
        if (stock_view_ship_depth++ || !stock_view_ship_policy(ship, original, &result)) _exit(223);
        --stock_view_ship_depth;
    }
    stock_view_policy_leave(&stock_view_ship_inflight);
    errno = saved_errno;
    return result;
}
EXPORT int x4mp_stock_view_ship_info(player_stock_view_ship_info *out) {
    if (!out || !atomic_load_explicit(&stock_view_ship_ready, memory_order_acquire)) return 0;
    *out = stock_view_ship_information;
    return 1;
}
EXPORT int x4mp_stock_view_ship_bind(player_stock_view_ship_policy policy) {
    if (!policy || !atomic_load_explicit(&stock_view_ship_ready, memory_order_acquire)) return 0;
    if (!stock_view_policy_begin_bind(&stock_view_ship_active, &stock_view_ship_inflight)) return 0;
    stock_view_ship_policy = policy;
    atomic_store_explicit(&stock_view_ship_active, 1, memory_order_seq_cst);
    return 1;
}
static int install_stock_view_ship(void) {
    const uintptr_t site = 0x129bb21;
    const uint64_t original = UINT64_C(0xc985000008808a8b);
    unsigned char hash[32];
    if (!single_thread() || !is_rx(0x129bad0) || !is_rx(0x129bc97)) return 0;
    for (unsigned index = 0; index < 8; ++index)
        if (((volatile const unsigned char *)site)[index] != ((const unsigned char *)&original)[index]) return 0;
    unsigned char code[456];
    memcpy(code, (const void *)0x129bad0, sizeof code);
#ifdef VIEW_ANCESTRY_SHARED_COMPONENT
    /* The shared installer has already checked the full stock image before
     * installing ancestry. Reconstruct only its exact owned entry patch. */
    if (view_ancestry_installed) {
        uint64_t entry;
        memcpy(&entry, code, sizeof entry);
        if (view_ancestry_information.blocks[2] != 0x129bad0 ||
            view_ancestry_information.original_words[2] != UINT64_C(0xfd89485554415541) ||
            entry != view_ancestry_information.patched_words[2]) return 0;
        memcpy(code, &view_ancestry_information.original_words[2], sizeof entry);
    }
#endif
    lockstep_sha256(code, sizeof code, hash);
    if (!digest_matches(hash, "cff3c5caae429ad58f93402f0d0b754c82d467823d8298c3641b1459a1605e50")) return 0;
    unsigned char *page = allocate_near();
    if (!page) return 0;
    memset(page, 0xcc, PVQ_PAGE);
    emit_entry_stub(page, (uintptr_t)player_stock_view_ship_bridge);
    int64_t distance = (int64_t)(uintptr_t)page - (int64_t)(site + 5);
    if (distance < INT32_MIN || distance > INT32_MAX) return 0;
    unsigned char patch[8]; memcpy(patch, &original, 8); patch[0] = 0xe9; patch[5] = 0x90;
    int32_t relative = (int32_t)distance; memcpy(patch + 1, &relative, 4);
    uint64_t patched; memcpy(&patched, patch, 8);
    void *library = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!library) return 0;
    void (*register_frame)(void *) = (void (*)(void *))dlsym(library, "__register_frame");
    if (!register_frame || mprotect(page, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx((uintptr_t)page)) return 0;
    player_stock_view_ship_frame(stock_view_ship_frame, (uintptr_t)page);
    register_frame(stock_view_ship_frame);
    uintptr_t text_page = site & ~(uintptr_t)(PVQ_PAGE - 1);
    if (!single_thread() || mprotect((void *)text_page, PVQ_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC)) return 0;
#ifdef STOCK_VIEW_SHIP_TEST
    stock_view_ship_test_before_cas();
#endif
    uint64_t expected = original;
    int installed = __atomic_compare_exchange_n((uint64_t *)site, &expected, patched, 0,
                                                 __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    __builtin___clear_cache((char *)site, (char *)(site + 8));
    if (mprotect((void *)text_page, PVQ_PAGE, PROT_READ | PROT_EXEC) || !installed || !is_rx(text_page)) return 0;
    stock_view_ship_information = (player_stock_view_ship_info){1, 1, (uintptr_t)page,
        (uintptr_t)player_stock_view_ship_bridge, (uintptr_t)player_stock_view_ship_dispatch,
        (uintptr_t)stock_view_ship_frame, original, patched};
    atomic_store_explicit(&stock_view_ship_ready, 1, memory_order_release);
    return 1;
}
#endif

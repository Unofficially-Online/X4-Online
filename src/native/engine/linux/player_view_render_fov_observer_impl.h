#ifndef X4_PLAYER_VIEW_RENDER_FOV_OBSERVER_IMPL_H
#define X4_PLAYER_VIEW_RENDER_FOV_OBSERVER_IMPL_H
/* Included only in the pre-main, sealed view-query image. */
#include "player_view_render_fov_observer.h"
#include "player_stock_view_policy_guard.h"
static _Atomic unsigned render_fov_ready, render_fov_active, render_fov_inflight;
static _Thread_local unsigned render_fov_depth;
static player_view_render_fov_observer render_fov_callback;
static float (*render_fov_original)(void *) = (float (*)(void *))0x1926cc0;
static player_view_render_fov_info render_fov_info;
static unsigned char render_fov_frame[48] __attribute__((aligned(8)));

/* Preserve stock errno and exact result. The callback never supplies a new
 * value, and a foreign call is the callback owner's decision to ignore. */
__attribute__((visibility("hidden"), noinline)) float player_view_render_fov_dispatch(void *camera) {
    int before = errno;
    unsigned active = stock_view_policy_enter(&render_fov_active, &render_fov_inflight);
    if (active && render_fov_depth++) _exit(223);
    errno = before;
    float result = render_fov_original(camera);
    int after = errno;
    if (active) { render_fov_callback((uintptr_t)camera, result); --render_fov_depth; }
    stock_view_policy_leave(&render_fov_inflight);
    errno = after;
    return result;
}
EXPORT int x4mp_view_render_fov_info(player_view_render_fov_info *out) {
    if (!out || !atomic_load_explicit(&render_fov_ready, memory_order_acquire)) return 0;
    *out = render_fov_info; return 1;
}
EXPORT int x4mp_view_render_fov_bind(player_view_render_fov_observer observer) {
    if (!observer || !atomic_load_explicit(&render_fov_ready, memory_order_acquire) ||
        !stock_view_policy_begin_bind(&render_fov_active, &render_fov_inflight)) return 0;
    render_fov_callback = observer;
    atomic_store_explicit(&render_fov_active, 1, memory_order_seq_cst);
    return 1;
}
static int install_render_fov_observer(void) {
    const uintptr_t function = 0x1926f80, site = 0x1926fca;
    const uint64_t word = UINT64_C(0x100ff3fffffcf1e8);
    unsigned char hash[32];
    if (!single_thread() || !is_rx(function) || !is_rx(function + 0xa0)) return 0;
    lockstep_sha256((void *)function, 0xa1, hash);
    if (!digest_matches(hash, "1da4645c6eed9fabfdb4f9a9eef4676e8cbf6efaf1dc28abf510eea77a95ef88") ||
        memcmp((void *)site, &word, 8)) return 0;
    unsigned char *page = allocate_near();
    if (!page) return 0;
    memset(page, 0xcc, PVQ_PAGE);
    emit_entry_stub(page, (uintptr_t)player_view_render_fov_dispatch);
    int64_t distance = (int64_t)(uintptr_t)page - (int64_t)(site + 5);
    if (distance < INT32_MIN || distance > INT32_MAX) return 0;
    uint64_t patched = word; int32_t displacement = (int32_t)distance;
    memcpy((unsigned char *)&patched + 1, &displacement, 4);
    if (mprotect(page, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx((uintptr_t)page) ||
        !register_frame_for_entry((uintptr_t)page, render_fov_frame)) return 0;
    uintptr_t text_page = site & ~(uintptr_t)(PVQ_PAGE - 1);
    if (!single_thread() || mprotect((void *)text_page, PVQ_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC)) return 0;
    uint64_t expected = word;
    int ok = __atomic_compare_exchange_n((uint64_t *)site, &expected, patched, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    __builtin___clear_cache((char *)text_page, (char *)(text_page + PVQ_PAGE));
    if (mprotect((void *)text_page, PVQ_PAGE, PROT_READ | PROT_EXEC) || !ok || !is_rx(text_page)) return 0;
    render_fov_info = (player_view_render_fov_info){1, 1, (uintptr_t)page,
        (uintptr_t)player_view_render_fov_dispatch, (uintptr_t)&render_fov_original,
        (uintptr_t)render_fov_frame, word, patched};
    atomic_store_explicit(&render_fov_ready, 1, memory_order_release);
    return 1;
}
#endif

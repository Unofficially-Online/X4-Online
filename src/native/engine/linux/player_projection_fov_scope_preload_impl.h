#ifndef X4_PLAYER_PROJECTION_FOV_SCOPE_PRELOAD_IMPL_H
#define X4_PLAYER_PROJECTION_FOV_SCOPE_PRELOAD_IMPL_H
#include "player_projection_fov_scope_preload.h"

#define PROJECTION_FOV_SCOPE_SITE UINT64_C(0xfeb977)
#define PROJECTION_FOV_SCOPE_WORD UINT64_C(0xc8280f0093b604e8)
static _Atomic unsigned projection_fov_installed, projection_fov_bound;
static uint32_t projection_fov_owner;
static uintptr_t projection_fov_camera;
static player_projection_fov_scope_callback projection_fov_enter, projection_fov_leave;
static float (*projection_fov_original)(void *) = (float (*)(void *))0x1926f80;
static player_projection_fov_scope_info projection_fov_information;
static unsigned char projection_fov_frame[48] __attribute__((aligned(8)));

typedef struct {
    void *camera;
    float result;
    int admitted;
} projection_fov_invocation;

static void projection_fov_call_original(void *value) {
    projection_fov_invocation *invocation = value;
    invocation->result = projection_fov_original(invocation->camera);
}
static int projection_fov_enter_guard(uintptr_t value) {
    projection_fov_invocation *invocation = (projection_fov_invocation *)value;
    invocation->admitted = projection_fov_enter((uintptr_t)invocation->camera);
    return invocation->admitted != 0;
}
static int projection_fov_leave_guard(uintptr_t value) {
    projection_fov_invocation *invocation = (projection_fov_invocation *)value;
    return projection_fov_leave((uintptr_t)invocation->camera);
}
__attribute__((visibility("hidden"), noinline)) float player_projection_fov_dispatch(void *camera) {
    int before = errno;
    if (atomic_load_explicit(&projection_fov_bound, memory_order_acquire) != 1 ||
        (uintptr_t)camera != projection_fov_camera) {
        errno = before;
        return projection_fov_original(camera);
    }
    if (current_thread() != projection_fov_owner || projection_fov_depth || ui_depth[0] || ui_depth[1] ||
        game_render_depth || camera_refill_depth) ui_overlay_abort();
    ++projection_fov_depth;
    projection_fov_invocation invocation = {.camera = camera};
    errno = before;
    player_ui_escape_guard_call(projection_fov_call_original, &invocation,
                                projection_fov_enter_guard, projection_fov_leave_guard);
    int after = errno;
    --projection_fov_depth;
    if (invocation.admitted == 1) ++projection_fov_completed;
    errno = after;
    return invocation.result;
}
EXPORT int x4mp_projection_fov_scope_info(player_projection_fov_scope_info *out) {
    if (!out || !atomic_load_explicit(&projection_fov_installed, memory_order_acquire)) return 0;
    *out = projection_fov_information;
    return 1;
}
EXPORT int x4mp_projection_fov_scope_bind(uint32_t owner, uintptr_t camera,
        player_projection_fov_scope_callback enter, player_projection_fov_scope_callback leave) {
    if (!atomic_load(&projection_fov_installed) || atomic_load(&projection_fov_bound) ||
        !owner || !camera || !enter || !leave || current_thread() != owner ||
        owner != atomic_load(&owner_tid) || ui_depth[0] != 1 || ui_depth[1] || game_render_depth ||
        camera_refill_depth || projection_fov_depth || !player_ui_escape_guard_prepare()) return 0;
    projection_fov_owner = owner;
    projection_fov_camera = camera;
    projection_fov_enter = enter;
    projection_fov_leave = leave;
    atomic_store_explicit(&projection_fov_bound, 4, memory_order_release);
    return 1;
}
EXPORT int x4mp_projection_fov_scope_activate(void) {
    unsigned expected = 4;
    return current_thread() == projection_fov_owner && ui_depth[0] == 1 && !ui_depth[1] &&
        atomic_compare_exchange_strong_explicit(&projection_fov_bound, &expected, 1,
                                                memory_order_acq_rel, memory_order_acquire);
}
EXPORT int x4mp_projection_fov_scope_disarm(void) {
    unsigned expected = 1;
    return current_thread() == projection_fov_owner && !projection_fov_depth &&
        atomic_compare_exchange_strong_explicit(&projection_fov_bound, &expected, 3,
                                                memory_order_acq_rel, memory_order_acquire);
}
static int install_projection_fov_scope(void) {
    const uintptr_t loop = 0xfeb940;
    const uint64_t word = PROJECTION_FOV_SCOPE_WORD;
    unsigned char hash[32];
    if (!single_thread() || !is_rx(loop) || !is_rx(loop + 0x81) ||
        memcmp((void *)PROJECTION_FOV_SCOPE_SITE, &word, 8)) return 0;
    lockstep_sha256((void *)loop, 0x82, hash);
    if (!digest_matches(hash, "e5b77561f311eab417410c549f180c3e0219392ae444abd39c60339dfd7e653a")) return 0;
    unsigned char *page = allocate_near();
    if (!page) return 0;
    memset(page, 0xcc, PVQ_PAGE);
    emit_entry_stub(page, (uintptr_t)player_projection_fov_dispatch);
    int64_t delta = (int64_t)(uintptr_t)page - (int64_t)(PROJECTION_FOV_SCOPE_SITE + 5);
    if (delta < INT32_MIN || delta > INT32_MAX) return 0;
    uint64_t patched = word;
    int32_t relative = (int32_t)delta;
    memcpy((unsigned char *)&patched + 1, &relative, 4);
    if (mprotect(page, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx((uintptr_t)page) ||
        !register_frame_for_entry((uintptr_t)page, projection_fov_frame)) return 0;
    uintptr_t code_page = PROJECTION_FOV_SCOPE_SITE & ~(uintptr_t)(PVQ_PAGE - 1);
    if (!single_thread() || mprotect((void *)code_page, PVQ_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC)) return 0;
    uint64_t expected = word;
    int installed = __atomic_compare_exchange_n((uint64_t *)PROJECTION_FOV_SCOPE_SITE, &expected, patched, 0,
                                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    __builtin___clear_cache((char *)code_page, (char *)(code_page + PVQ_PAGE));
    if (mprotect((void *)code_page, PVQ_PAGE, PROT_READ | PROT_EXEC) || !installed || !is_rx(code_page)) return 0;
    projection_fov_information = (player_projection_fov_scope_info){1, 1, PROJECTION_FOV_SCOPE_SITE,
        (uintptr_t)page, (uintptr_t)player_projection_fov_dispatch, (uintptr_t)&projection_fov_original,
        (uintptr_t)projection_fov_frame, word, patched, 14, 48, PVQ_PAGE};
    atomic_store_explicit(&projection_fov_installed, 1, memory_order_release);
    return 1;
}
#endif

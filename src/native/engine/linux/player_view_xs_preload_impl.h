#ifndef X4_PLAYER_VIEW_XS_PRELOAD_IMPL_H
#define X4_PLAYER_VIEW_XS_PRELOAD_IMPL_H
#include "player_view_xs_preload.h"
#include "player_stock_view_policy_guard.h"

static const uintptr_t xs_sites[3] = {0x129a6be, 0x129a75c, 0x129a779};
static const unsigned char xs_original_bytes[3][5] = {
    {0xe8, 0xbd, 0x5c, 0x9f, 0x00}, {0xe8, 0xbf, 0xc8, 0xb6, 0x00}, {0xe8, 0x12, 0xdf, 0x9f, 0x00}};
static uintptr_t (*xs_target_original)(void) = (uintptr_t (*)(void))0x1c90380;
static uint64_t (*xs_external_original)(void) = (uint64_t (*)(void))0x1e07020;
static float (*xs_distance_original)(void *, void *,
                                     unsigned) = (float (*)(void *, void *, unsigned))0x1c98690;
static player_view_xs_target_policy xs_target_policy;
static player_view_xs_external_policy xs_external_policy;
static player_view_xs_distance_policy xs_distance_policy;
static _Atomic unsigned xs_installed, xs_active, xs_inflight;
static _Thread_local unsigned xs_depth;
static player_view_xs_preload_info xs_information;
static unsigned char xs_frames[3][48] __attribute__((aligned(8)));

__asm__(
    ".text\n.hidden player_view_xs_target_bridge\n.type player_view_xs_target_bridge,@function\n"
    "player_view_xs_target_bridge:\n.cfi_startproc\nmov %r12,%rdi\n"
    "jmp player_view_xs_target_dispatch\n.cfi_endproc\n"
    ".size player_view_xs_target_bridge,.-player_view_xs_target_bridge\n");
extern void player_view_xs_target_bridge(void) __attribute__((visibility("hidden")));

static int xs_still_owned(void);
#ifdef PLAYER_VIEW_XS_TEST
static uintptr_t xs_test_targets[3] = {0x1c90380, 0x1e07020, 0x1c98690};
#endif
static unsigned xs_enter(void) {
    unsigned active = stock_view_policy_enter(&xs_active, &xs_inflight);
    if (active && (xs_depth++ || !xs_still_owned()))
        _exit(223);
    return active;
}
static void xs_leave(unsigned active) {
    if (active)
        --xs_depth;
    stock_view_policy_leave(&xs_inflight);
}
__attribute__((visibility("hidden"), noinline)) uintptr_t
player_view_xs_target_dispatch(uintptr_t ship) {
    int before = errno;
    unsigned active = xs_enter();
    uintptr_t result = 0;
    int decision = active ? xs_target_policy(ship, 0, &result) : 2;
    if (decision != 1 && decision != 2) {
        static const char message[] = "X4 VIEW_XS failure=target_scope_or_identity\n";
        (void)write(STDERR_FILENO, message, sizeof message - 1);
        _exit(223);
    }
    errno = before;
    if (decision == 2)
        result = xs_target_original();
    int after = errno;
    xs_leave(active);
    errno = after;
    return result;
}
__attribute__((visibility("hidden"), noinline)) uint64_t player_view_xs_external_dispatch(void) {
    int before = errno;
    unsigned active = xs_enter();
    errno = before;
    uint64_t original = xs_external_original(), result = original;
    int after = errno;
    if (active && !xs_external_policy(original, &result)) {
        static const char message[] = "X4 VIEW_XS failure=external_scope_or_identity\n";
        (void)write(STDERR_FILENO, message, sizeof message - 1);
        _exit(223);
    }
    if (active && (result & ~UINT64_C(0xff)) != (original & ~UINT64_C(0xff)))
        _exit(223);
    xs_leave(active);
    errno = after;
    return result;
}
__attribute__((visibility("hidden"), noinline)) float
player_view_xs_distance_dispatch(void *ship, void *camera, unsigned flags) {
    int before = errno;
    unsigned active = xs_enter();
    errno = before;
    float original = xs_distance_original(ship, camera, flags), result = original;
    int after = errno;
    if (active &&
        !xs_distance_policy((uintptr_t)ship, (uintptr_t)camera, flags, original, &result)) {
        static const char message[] = "X4 VIEW_XS failure=distance_scope_or_identity\n";
        (void)write(STDERR_FILENO, message, sizeof message - 1);
        _exit(223);
    }
    xs_leave(active);
    errno = after;
    return result;
}
EXPORT int x4mp_view_xs_bound(void) {
    return atomic_load_explicit(&xs_active, memory_order_acquire) == 1 && xs_still_owned();
}
EXPORT int x4mp_view_xs_info(player_view_xs_preload_info *out) {
    if (!out || !atomic_load_explicit(&xs_installed, memory_order_acquire))
        return 0;
    *out = xs_information;
    return 1;
}
static int xs_still_owned(void) {
#ifdef PLAYER_VIEW_XS_TEST
    const uintptr_t *targets = xs_test_targets;
#else
    static const uintptr_t targets[3] = {0x1c90380, 0x1e07020, 0x1c98690};
#endif
    if (!atomic_load_explicit(&xs_installed, memory_order_acquire) || xs_information.version != 1 ||
        xs_information.installed != 1 || (uintptr_t)xs_target_original != targets[0] ||
        (uintptr_t)xs_external_original != targets[1] ||
        (uintptr_t)xs_distance_original != targets[2])
        return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (memcmp((void *)xs_sites[i], xs_information.patched[i], 5))
            return 0;
    return 1;
}
EXPORT int x4mp_view_xs_bind(player_view_xs_target_policy target,
                             player_view_xs_external_policy external,
                             player_view_xs_distance_policy distance) {
    if (!target || !external || !distance || !xs_still_owned() ||
        !stock_view_policy_begin_bind(&xs_active, &xs_inflight))
        return 0;
    if (!xs_still_owned()) {
        atomic_store_explicit(&xs_active, 0, memory_order_seq_cst);
        return 0;
    }
    xs_target_policy = target;
    xs_external_policy = external;
    xs_distance_policy = distance;
    atomic_store_explicit(&xs_active, 1, memory_order_seq_cst);
    return 1;
}
static int install_view_xs(void) {
    unsigned char digest[32];
    if (!single_thread() || !is_rx(0x129a680) || !is_rx(0x129a790))
        return 0;
    lockstep_sha256((void *)0x129a680, 0x111, digest);
    if (!digest_matches(digest, "e491a0bd3d0a73760d66ada4215e20b07b4af0dc6b894d232dfba2472bb9142b"))
        return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (memcmp((void *)xs_sites[i], xs_original_bytes[i], 5))
            return 0;
    unsigned char *page = allocate_near();
    if (!page)
        return 0;
    memset(page, 0xcc, PVQ_PAGE);
    uintptr_t bridges[3] = {(uintptr_t)player_view_xs_target_bridge,
                            (uintptr_t)player_view_xs_external_dispatch,
                            (uintptr_t)player_view_xs_distance_dispatch};
    unsigned char patched[3][5];
    for (unsigned i = 0; i < 3; ++i) {
        emit_entry_stub(page + i * 16, bridges[i]);
        int64_t displacement = (int64_t)(uintptr_t)(page + i * 16) - (int64_t)(xs_sites[i] + 5);
        if (displacement < INT32_MIN || displacement > INT32_MAX)
            return 0;
        int32_t relative = (int32_t)displacement;
        patched[i][0] = 0xe8;
        memcpy(patched[i] + 1, &relative, 4);
    }
    if (mprotect(page, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx((uintptr_t)page))
        return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (!register_frame_for_entry((uintptr_t)(page + i * 16), xs_frames[i]))
            return 0;
    uintptr_t code_page = xs_sites[0] & ~(uintptr_t)(PVQ_PAGE - 1);
    if (!single_thread() ||
        mprotect((void *)code_page, PVQ_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC))
        return 0;
    int ok = single_thread();
    for (unsigned i = 0; ok && i < 3; ++i)
        ok = !memcmp((void *)xs_sites[i], xs_original_bytes[i], 5);
    if (ok)
        for (unsigned i = 0; i < 3; ++i)
            memcpy((void *)xs_sites[i], patched[i], 5);
    __builtin___clear_cache((char *)code_page, (char *)(code_page + PVQ_PAGE));
    if (mprotect((void *)code_page, PVQ_PAGE, PROT_READ | PROT_EXEC) || !ok || !is_rx(code_page))
        return 0;
    xs_information = (player_view_xs_preload_info){
        .version = 1,
        .installed = 1,
        .page = (uintptr_t)page,
        .bridge = {bridges[0], bridges[1], bridges[2]},
        .original_slot = {(uintptr_t)&xs_target_original, (uintptr_t)&xs_external_original,
                          (uintptr_t)&xs_distance_original},
        .frame = {(uintptr_t)xs_frames[0], (uintptr_t)xs_frames[1], (uintptr_t)xs_frames[2]}};
    memcpy(xs_information.original, xs_original_bytes, sizeof xs_information.original);
    memcpy(xs_information.patched, patched, sizeof xs_information.patched);
    atomic_store_explicit(&xs_installed, 1, memory_order_release);
    return 1;
}
#endif

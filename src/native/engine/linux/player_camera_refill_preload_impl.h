#ifndef X4_PLAYER_CAMERA_REFILL_PRELOAD_IMPL_H
#define X4_PLAYER_CAMERA_REFILL_PRELOAD_IMPL_H
#include "player_camera_refill_preload.h"

#define CAMERA_REFILL_ENTRY UINT64_C(0x192a310)
#define CAMERA_REFILL_CALL_RETURN UINT64_C(0xfeb974)
#define CAMERA_REFILL_WORD UINT64_C(0x8949c0ef0f665741)
static _Atomic unsigned camera_refill_installed, camera_refill_bound;
static uint32_t camera_refill_owner;
static uintptr_t camera_refill_target;
static player_camera_refill_callback camera_refill_enter, camera_refill_leave;
static uintptr_t (*camera_refill_original)(void *, void *);
static player_camera_refill_info camera_refill_information;
static unsigned char camera_refill_frame[48] __attribute__((aligned(8)));

/* Copy the two complete native instructions and continue into its own FDE. */
__asm__(".text\n.hidden camera_refill_trampoline\n.type camera_refill_trampoline,@function\n"
        "camera_refill_trampoline:\n.cfi_startproc\npush %r15\n"
        ".cfi_def_cfa_offset 16\n.cfi_offset %r15,-16\npxor %xmm0,%xmm0\n"
        "movabs $0x192a316,%r11\njmp *%r11\n.cfi_endproc\n"
        ".size camera_refill_trampoline,.-camera_refill_trampoline\n"
        ".hidden camera_refill_trampoline_end\ncamera_refill_trampoline_end:\n");
extern uintptr_t camera_refill_trampoline(void *, void *) __attribute__((visibility("hidden")));
extern const unsigned char camera_refill_trampoline_end[] __attribute__((visibility("hidden")));

typedef struct { void *camera, *output; uintptr_t result; int admitted; } camera_refill_invocation;
static void camera_refill_call_original(void *value) {
    camera_refill_invocation *call = value;
    call->result = camera_refill_original(call->camera, call->output);
}
static int camera_refill_enter_guard(uintptr_t value) {
    camera_refill_invocation *call = (camera_refill_invocation *)value;
    call->admitted = camera_refill_enter((uintptr_t)call->camera, (uintptr_t)call->output);
    return call->admitted != 0;
}
static int camera_refill_leave_guard(uintptr_t value) {
    camera_refill_invocation *call = (camera_refill_invocation *)value;
    return camera_refill_leave((uintptr_t)call->camera, (uintptr_t)call->output);
}
static uintptr_t camera_refill_dispatch_from(void *camera, void *output, uintptr_t caller) {
    int before = errno;
    if (atomic_load_explicit(&camera_refill_bound, memory_order_acquire) != 1 ||
        caller != CAMERA_REFILL_CALL_RETURN || (uintptr_t)camera != camera_refill_target) {
        errno = before;
        return camera_refill_original(camera, output);
    }
    if (current_thread() != camera_refill_owner || camera_refill_depth || ui_depth[0] || ui_depth[1] || game_render_depth)
        ui_overlay_abort();
    ++camera_refill_depth;
    camera_refill_invocation call = {camera, output, 0, 0};
    errno = before;
    player_ui_escape_guard_call(camera_refill_call_original, &call, camera_refill_enter_guard, camera_refill_leave_guard);
    int after = errno;
    --camera_refill_depth;
    if (call.admitted == 1) ++camera_refill_completed;
    errno = after;
    return call.result;
}
static uintptr_t camera_refill_dispatch(void *camera, void *output) {
    return camera_refill_dispatch_from(camera, output, (uintptr_t)__builtin_return_address(0));
}
EXPORT int x4mp_camera_refill_info(player_camera_refill_info *out) {
    if (!out || !atomic_load_explicit(&camera_refill_installed, memory_order_acquire)) return 0;
    *out = camera_refill_information;
    return 1;
}
EXPORT int x4mp_camera_refill_bind(uint32_t owner, uintptr_t camera,
                                   player_camera_refill_callback enter, player_camera_refill_callback leave) {
    if (!atomic_load(&camera_refill_installed) || atomic_load(&camera_refill_bound) || !owner || !camera || !enter || !leave ||
        current_thread() != owner || owner != atomic_load(&owner_tid) || ui_depth[0] != 1 || ui_depth[1] ||
        game_render_depth || camera_refill_depth || !player_ui_escape_guard_prepare()) return 0;
    camera_refill_owner = owner;
    camera_refill_target = camera;
    camera_refill_enter = enter;
    camera_refill_leave = leave;
    atomic_store_explicit(&camera_refill_bound, 4, memory_order_release);
    return 1;
}
EXPORT int x4mp_camera_refill_activate(void) {
    unsigned expected = 4;
    return current_thread() == camera_refill_owner && ui_depth[0] == 1 && !ui_depth[1] &&
        atomic_compare_exchange_strong_explicit(&camera_refill_bound, &expected, 1,
                                                memory_order_acq_rel, memory_order_acquire);
}
EXPORT int x4mp_camera_refill_disarm(void) {
    unsigned expected = 1;
    return current_thread() == camera_refill_owner && !camera_refill_depth &&
        atomic_compare_exchange_strong_explicit(&camera_refill_bound, &expected, 3,
                                                memory_order_acq_rel, memory_order_acquire);
}
static int install_camera_refill(void) {
    if (!single_thread() || !is_rx(CAMERA_REFILL_ENTRY) || !is_rx(CAMERA_REFILL_ENTRY + 0x587) ||
        memcmp((void *)CAMERA_REFILL_ENTRY, &(uint64_t){CAMERA_REFILL_WORD}, 8)) return 0;
    unsigned char hash[32];
    lockstep_sha256((void *)CAMERA_REFILL_ENTRY, 0x588, hash);
    if (!digest_matches(hash, "acb114cd09879defbadd3a2dde0901975b3bbdc6ef371335c47120b820ea3430")) return 0;
    unsigned char *page = allocate_near();
    if (!page) return 0;
    memset(page, 0xcc, PVQ_PAGE);
    emit_entry_stub(page, (uintptr_t)camera_refill_dispatch);
    if (mprotect(page, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx((uintptr_t)page) ||
        !register_frame_for_entry((uintptr_t)page, camera_refill_frame)) return 0;
    int64_t distance = (int64_t)(uintptr_t)page - (int64_t)(CAMERA_REFILL_ENTRY + 5);
    if (distance < INT32_MIN || distance > INT32_MAX) return 0;
    unsigned char code[8]; uint64_t patched, expected = CAMERA_REFILL_WORD;
    memcpy(code, &expected, sizeof code);
    code[0] = 0xe9; int32_t displacement = (int32_t)distance;
    memcpy(code + 1, &displacement, sizeof displacement);
    code[5] = 0x90; /* +6 starts the next native instruction and must remain intact. */
    memcpy(&patched, code, sizeof patched);
    camera_refill_original = camera_refill_trampoline;
    uintptr_t page_base = CAMERA_REFILL_ENTRY & ~(uintptr_t)(PVQ_PAGE - 1);
    if (!single_thread() || mprotect((void *)page_base, PVQ_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC) ||
        !__atomic_compare_exchange_n((uint64_t *)CAMERA_REFILL_ENTRY, &expected, patched, 0,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return 0;
    __builtin___clear_cache((char *)CAMERA_REFILL_ENTRY, (char *)CAMERA_REFILL_ENTRY + 8);
    if (mprotect((void *)page_base, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx(CAMERA_REFILL_ENTRY)) return 0;
    camera_refill_information = (player_camera_refill_info){1, 1, CAMERA_REFILL_ENTRY, (uintptr_t)page,
        (uintptr_t)camera_refill_trampoline, (uintptr_t)camera_refill_dispatch, (uintptr_t)&camera_refill_original,
        (uintptr_t)camera_refill_frame, CAMERA_REFILL_WORD, patched, 14,
        (uint32_t)(camera_refill_trampoline_end - (const unsigned char *)(uintptr_t)camera_refill_trampoline), 48, PVQ_PAGE};
    atomic_store_explicit(&camera_refill_installed, 1, memory_order_release);
    return 1;
}
#endif

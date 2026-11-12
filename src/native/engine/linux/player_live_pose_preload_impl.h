#ifndef X4_PLAYER_LIVE_POSE_PRELOAD_IMPL_H
#define X4_PLAYER_LIVE_POSE_PRELOAD_IMPL_H

#include <sys/uio.h>
#include <execinfo.h>
#include "player_ui_live_correction.h"

#define LIVE_POSE_ENTRY UINT64_C(0x1628f20)
#define LIVE_POSE_WORD UINT64_C(0x4855a8678d4c5441)
static const char live_pose_sha[] = "fbf95e0c5dc034ca657533ec711a24ad63bf4936937d06f224ac04af839aa21e";
static player_live_pose_info live_pose_information;
static player_live_pose_snapshot live_pose_capture;
static player_live_pose_scope_snapshot live_pose_scopes;
static unsigned live_pose_scope_pending;
static int live_correction_enabled;
static _Alignas(16) float live_correction_delta[16];
static uintptr_t live_correction_stack_low, live_correction_stack_high;
static player_live_correction_snapshot live_correction_capture;
static unsigned char live_pose_frame[48] __attribute__((aligned(8)));
static _Atomic unsigned live_pose_active;
static uint32_t live_pose_owner;
static uintptr_t live_pose_target;
static uint64_t live_pose_last_move;
static _Thread_local unsigned live_pose_depth;
static uintptr_t (*live_pose_original)(void *, void *, unsigned, double);

/* The live observer is already opt-in. Keep one bounded native stack for
 * each known caller/scope/callback combination so camera updates outside the
 * presentation lease can be located without changing their return values. */
static unsigned live_pose_stack_recorded[PLAYER_LIVE_POSE_SCOPE_COUNT][4];
static void live_pose_record_stack(uintptr_t caller, unsigned scope) {
    unsigned caller_bit = caller == UINT64_C(0x1314317) ? 1u :
                          caller == UINT64_C(0x1314407) ? 2u : 0u;
    unsigned phase = game_render_depth ? 3u : ui_depth[0] ? 1u : ui_depth[1] ? 2u : 0u;
    if (!caller_bit || scope >= PLAYER_LIVE_POSE_SCOPE_COUNT ||
        (live_pose_stack_recorded[scope][phase] & caller_bit)) return;
    live_pose_stack_recorded[scope][phase] |= caller_bit;
    void *frames[16];
    int count = backtrace(frames, 16);
    char record[768];
    int used = snprintf(record, sizeof record,
        "X4 UI LIVE_STACK v=1 tick=%llu scope=%u phase=%u caller=%llx count=%d addresses=",
        (unsigned long long)live_pose_last_move, scope, phase,
        (unsigned long long)caller, count);
    if (used < 0 || (size_t)used >= sizeof record) return;
    for (int index = 0; index < count; ++index) {
        int written = snprintf(record + used, sizeof record - (size_t)used, "%s%llx",
            index ? "," : "", (unsigned long long)(uintptr_t)frames[index]);
        if (written < 0 || (size_t)written >= sizeof record - (size_t)used) return;
        used += written;
    }
    if ((size_t)used + 1 >= sizeof record) return;
    record[used++] = '\n';
    (void)write(STDERR_FILENO, record, (size_t)used);
}

static _Noreturn void live_correction_fatal(const char *message, size_t size) {
    (void)write(STDERR_FILENO, message, size);
    _exit(223);
}
#define LIVE_CORRECTION_FATAL(message) live_correction_fatal(message, sizeof(message) - 1)

/* The stock function's source return type is unknown. Preserve its raw RAX. */
__asm__(".text\n"
        ".hidden player_live_pose_original\n"
        ".type player_live_pose_original,@function\n"
        "player_live_pose_original:\n"
        ".cfi_startproc\n"
        "push %r12\n"
        ".cfi_def_cfa_offset 16\n"
        ".cfi_offset %r12,-16\n"
        "lea -0x58(%rdi),%r12\n"
        "push %rbp\n"
        ".cfi_def_cfa_offset 24\n"
        ".cfi_offset %rbp,-24\n"
        "movabs $0x1628f27,%r11\n"
        "jmp *%r11\n"
        ".cfi_endproc\n"
        ".size player_live_pose_original,.-player_live_pose_original\n"
        ".hidden player_live_pose_original_end\n"
        "player_live_pose_original_end:\n");
extern uintptr_t player_live_pose_original(void *, void *, unsigned, double)
    __attribute__((visibility("hidden")));
extern const unsigned char player_live_pose_original_end[] __attribute__((visibility("hidden")));

static int live_pose_copy(void *destination, const void *source) {
    struct iovec local = {destination, 64};
    struct iovec remote = {(void *)source, 64};
    if (syscall(SYS_process_vm_readv, (long)syscall(SYS_getpid), &local, 1UL,
                &remote, 1UL, 0UL) == 64) return 1;
    memset(destination, 0, 64);
    return 0;
}

/* Only the owner writes samples. Other calls have no recording-side effects. */
__attribute__((noinline)) static uintptr_t live_pose_dispatch(void *self, void *output,
                                                              unsigned mode, double time) {
    int entry_errno = errno;
    if (!atomic_load_explicit(&live_pose_active, memory_order_acquire) ||
        (uintptr_t)self != live_pose_target || current_thread() != live_pose_owner) {
        errno = entry_errno;
        return live_pose_original(self, output, mode, time);
    }
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    ++live_pose_capture.started;
    uint64_t ordinal = live_pose_capture.started;
    unsigned scope = live_pose_scopes.state;
    if (scope >= PLAYER_LIVE_POSE_SCOPE_COUNT) _exit(223);
    ++live_pose_scopes.counts[scope];
    if (ui_depth[0] && ui_depth[1]) ++live_pose_capture.phase_both;
    else if (ui_depth[0]) ++live_pose_capture.phase_move;
    else if (ui_depth[1]) ++live_pose_capture.phase_render;
    else ++live_pose_capture.phase_other;
    if ((mode & 255u) == 0) ++live_pose_capture.mode_zero;
    else if ((mode & 255u) == 1) ++live_pose_capture.mode_one;
    else ++live_pose_capture.mode_other;
    if (live_pose_depth++) {
        ++live_pose_capture.nested;
        errno = entry_errno;
        uintptr_t result = live_pose_original(self, output, mode, time);
        int result_errno = errno;
        --live_pose_depth;
        ++live_pose_capture.returned;
        if (live_correction_enabled && scope == PLAYER_LIVE_POSE_OPEN)
            LIVE_CORRECTION_FATAL("X4 UI SEAT_CORRECTION FATAL reason=nested\n");
        errno = result_errno;
        return result;
    }
    player_live_pose_row sample = {0};
    sample.caller = caller;
    sample.last_move = live_pose_last_move;
    memcpy(&sample.time_bits, &time, sizeof time);
    sample.mode = mode & 255u;
    sample.move_depth = ui_depth[0];
    sample.render_depth = ui_depth[1];
    sample.before_valid = live_pose_copy(sample.before, output);
    errno = entry_errno;
    uintptr_t result = live_pose_original(self, output, mode, time);
    int result_errno = errno;
    sample.after_valid = live_pose_copy(sample.after, output);
    if (live_correction_enabled && scope == PLAYER_LIVE_POSE_OPEN) {
        unsigned char corrected[64], checked[64];
        if (!((ui_depth[0] == 1 && !ui_depth[1] && !game_render_depth) ||
              (game_render_depth == 1 && !ui_depth[0]) ||
              (camera_refill_depth == 1 && !ui_depth[0] && !ui_depth[1] && !game_render_depth)) || !sample.after_valid ||
            !player_ui_live_correction(caller, (uintptr_t)output, live_correction_stack_low,
                                       live_correction_stack_high, mode & 255u, time,
                                       live_correction_delta, sample.after, corrected)) {
            char diagnostic[320];
            int length = snprintf(diagnostic, sizeof diagnostic,
                "X4 UI SEAT_CORRECTION REFUSAL stage=output_guard caller=%llx mode=%u move=%u ui_render=%u game_render=%u output=%llx stack_low=%llx stack_high=%llx after_valid=%u\n",
                (unsigned long long)caller, mode & 255u, ui_depth[0], ui_depth[1], game_render_depth,
                (unsigned long long)(uintptr_t)output, (unsigned long long)live_correction_stack_low,
                (unsigned long long)live_correction_stack_high, sample.after_valid);
            if (length > 0 && (size_t)length < sizeof diagnostic) (void)write(2, diagnostic, (size_t)length);
            LIVE_CORRECTION_FATAL("X4 UI SEAT_CORRECTION FATAL reason=output_guard\n");
        }
        struct iovec local = {corrected, sizeof corrected};
        struct iovec remote = {output, sizeof corrected};
        if (syscall(SYS_process_vm_writev, (long)syscall(SYS_getpid), &local, 1UL,
                    &remote, 1UL, 0UL) != 64 || !live_pose_copy(checked, output) ||
            memcmp(checked, corrected, sizeof checked))
            LIVE_CORRECTION_FATAL("X4 UI SEAT_CORRECTION FATAL reason=output_write\n");
        ++live_correction_capture.calls;
        if (live_correction_capture.count < PLAYER_LIVE_CORRECTION_ROWS) {
            player_live_correction_row *row = &live_correction_capture.rows[live_correction_capture.count++];
            row->ordinal = ordinal;
            row->last_move = live_pose_last_move;
            row->caller = caller;
            memcpy(&row->time_bits, &time, sizeof time);
            memcpy(row->raw, sample.after, 64);
            memcpy(row->corrected, corrected, 64);
        }
    }
    ++live_pose_capture.returned;
    --live_pose_depth;
    if (live_pose_scopes.count[scope] < PLAYER_LIVE_POSE_SCOPE_ROWS) {
        player_live_pose_scope_row *representative =
            &live_pose_scopes.rows[scope][live_pose_scopes.count[scope]++];
        representative->ordinal = ordinal;
        representative->row = sample;
    }
    if (live_pose_capture.count < PLAYER_LIVE_POSE_ROWS) {
        live_pose_capture.rows[live_pose_capture.count++] = sample;
    } else {
        ++live_pose_capture.overflow;
    }
    live_pose_record_stack(caller, scope);
    errno = result_errno;
    return result;
}

EXPORT unsigned x4mp_live_pose_info(player_live_pose_info *out) {
    if (!out || !live_pose_information.installed) return 0;
    *out = live_pose_information;
    return PLAYER_LIVE_POSE_VERSION;
}
EXPORT int x4mp_live_pose_bind(uint32_t tid, uintptr_t secondary_controller) {
    if (!live_pose_information.installed || !tid || tid != current_thread() ||
        tid != atomic_load_explicit(&owner_tid, memory_order_acquire) ||
        !secondary_controller || ui_depth[0] != 1 || ui_depth[1] ||
        live_pose_owner || atomic_load_explicit(&live_pose_active, memory_order_acquire)) return 0;
    live_pose_owner = tid;
    live_pose_target = secondary_controller;
    live_pose_last_move = preload_startup.presentation_first;
    atomic_store_explicit(&live_pose_active, 1, memory_order_release);
    return 1;
}
EXPORT int x4mp_live_pose_tick(uint64_t generation) {
    if (!atomic_load_explicit(&live_pose_active, memory_order_acquire) ||
        current_thread() != live_pose_owner || live_pose_depth ||
        generation != live_pose_last_move + 1 ||
        generation > (preload_session_budget.frames ? preload_session_budget.presentation_last : 168u)) return 0;
    live_pose_last_move = generation;
    return 1;
}
EXPORT int x4mp_live_pose_finish(player_live_pose_snapshot *out) {
    if (!out || !live_pose_owner || current_thread() != live_pose_owner ||
        !atomic_load_explicit(&live_pose_active, memory_order_acquire) ||
        live_pose_last_move != (preload_session_budget.frames ? preload_session_budget.presentation_last : 168u) ||
        live_pose_depth ||
        live_pose_scopes.state != PLAYER_LIVE_POSE_OUTSIDE ||
        live_pose_scopes.closes != (preload_session_budget.frames ? 2u * preload_session_budget.presentation_count + camera_refill_completed :
                                    atomic_load(&game_render_installed) ? 320u + camera_refill_completed : 160u) ||
        live_pose_scopes.opens != live_pose_scopes.closes + live_pose_scopes.pauses ||
        live_pose_capture.started != live_pose_capture.returned ||
        live_pose_capture.nested) return 0;
    atomic_store_explicit(&live_pose_active, 0, memory_order_release);
    live_pose_capture.closed = 1;
    *out = live_pose_capture;
    return 1;
}

/* Owner-only notifications describe completed changes, not other readers or
 * the interval while the underlying four fields are being changed. */
static int live_pose_scope_owner(void) {
    return atomic_load_explicit(&live_pose_active, memory_order_acquire) &&
        current_thread() == live_pose_owner && !live_pose_depth &&
        ((ui_depth[0] == 1 && !ui_depth[1] && !game_render_depth) ||
         (game_render_depth == 1 && !ui_depth[0] && !ui_depth[1] && !camera_refill_depth) ||
         (camera_refill_depth == 1 && !ui_depth[0] && !ui_depth[1] && !game_render_depth));
}
EXPORT int x4mp_live_pose_scope_begin(unsigned next) {
    if (!live_pose_scope_owner()) return 0;
    unsigned previous = live_pose_scopes.state;
    if (!((next == PLAYER_LIVE_POSE_OPEN &&
           (previous == PLAYER_LIVE_POSE_OUTSIDE || previous == PLAYER_LIVE_POSE_PAUSED)) ||
          (previous == PLAYER_LIVE_POSE_OPEN &&
           (next == PLAYER_LIVE_POSE_PAUSED || next == PLAYER_LIVE_POSE_OUTSIDE)))) return 0;
    live_pose_scope_pending = next;
    live_pose_scopes.state = PLAYER_LIVE_POSE_CHANGING;
    return 1;
}
EXPORT int x4mp_live_pose_scope(unsigned state) {
    if (!live_pose_scope_owner() || live_pose_scopes.state != PLAYER_LIVE_POSE_CHANGING ||
        live_pose_scope_pending != state) return 0;
    if (state == PLAYER_LIVE_POSE_OPEN) ++live_pose_scopes.opens;
    else if (state == PLAYER_LIVE_POSE_PAUSED) ++live_pose_scopes.pauses;
    else if (state == PLAYER_LIVE_POSE_OUTSIDE) ++live_pose_scopes.closes;
    else return 0;
    live_pose_scopes.state = state;
    return 1;
}
EXPORT int x4mp_live_pose_scope_result(player_live_pose_scope_snapshot *out) {
    if (!out || current_thread() != live_pose_owner || !live_pose_capture.closed ||
        atomic_load_explicit(&live_pose_active, memory_order_acquire)) return 0;
    *out = live_pose_scopes;
    return 1;
}

EXPORT int x4mp_live_correction_configure(uint32_t tid, const float delta[16],
                                             uintptr_t stack_low, uintptr_t stack_high) {
    float checked[16];
    if (live_correction_enabled || !live_pose_scope_owner() || tid != live_pose_owner ||
        live_pose_scopes.state != PLAYER_LIVE_POSE_OUTSIDE || stack_low >= stack_high ||
        stack_high - stack_low < 64 || !live_pose_copy(checked, delta) ||
        !player_ui_seat_transform_rigid(checked)) return 0;
    memcpy(live_correction_delta, checked, sizeof checked);
    live_correction_stack_low = stack_low;
    live_correction_stack_high = stack_high;
    live_correction_enabled = 1;
    return 1;
}
EXPORT int x4mp_live_correction_result(player_live_correction_snapshot *out) {
    if (!out || !live_correction_enabled || current_thread() != live_pose_owner ||
        !live_pose_capture.closed || atomic_load_explicit(&live_pose_active, memory_order_acquire)) return 0;
    *out = live_correction_capture;
    return 1;
}

static int install_live_pose(void) {
    /* Resolve the unwinder before the game starts its worker threads. */
    void *warmup[2];
    (void)backtrace(warmup, 2);
    if (!single_thread() || !is_rx(LIVE_POSE_ENTRY) || !is_rx(LIVE_POSE_ENTRY + 0x95) ||
        memcmp((const void *)LIVE_POSE_ENTRY, &(uint64_t){LIVE_POSE_WORD}, 8)) return 0;
    unsigned char digest[32];
    lockstep_sha256((const void *)LIVE_POSE_ENTRY, 0x96, digest);
    if (!digest_matches(digest, live_pose_sha)) return 0;
    unsigned char *page = allocate_near();
    if (!page) return 0;
    memset(page, 0xcc, PVQ_PAGE);
    emit_entry_stub(page, (uintptr_t)live_pose_dispatch);
    if (mprotect(page, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx((uintptr_t)page) ||
        !register_frame_for_entry((uintptr_t)page, live_pose_frame)) return 0;
    live_pose_original = player_live_pose_original;
    uint64_t patched, expected = LIVE_POSE_WORD;
    if (!make_entry_patch(LIVE_POSE_ENTRY, (uintptr_t)page, LIVE_POSE_WORD, &patched) ||
        !single_thread()) return 0;
    uintptr_t base = LIVE_POSE_ENTRY & ~(uintptr_t)(PVQ_PAGE - 1);
    if (mprotect((void *)base, PVQ_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC) ||
        !__atomic_compare_exchange_n((uint64_t *)LIVE_POSE_ENTRY, &expected, patched, 0,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return 0;
    __builtin___clear_cache((char *)LIVE_POSE_ENTRY, (char *)LIVE_POSE_ENTRY + 8);
    if (mprotect((void *)base, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx(LIVE_POSE_ENTRY)) return 0;
    live_pose_information = (player_live_pose_info){
        .version = PLAYER_LIVE_POSE_VERSION, .installed = 1, .entry = LIVE_POSE_ENTRY,
        .stub = (uintptr_t)page, .trampoline = (uintptr_t)player_live_pose_original,
        .dispatcher = (uintptr_t)live_pose_dispatch, .original_slot = (uintptr_t)&live_pose_original,
        .entry_frame = (uintptr_t)live_pose_frame, .original_word = LIVE_POSE_WORD,
        .patched_word = patched, .stub_size = 14,
        .trampoline_size = (uint32_t)(player_live_pose_original_end - (const unsigned char *)(uintptr_t)player_live_pose_original),
        .entry_frame_size = sizeof live_pose_frame, .page_size = PVQ_PAGE,
    };
    static const char active[] = "X4 LIVE POSE PRELOAD ACTIVE version=1\n";
    (void)write(2, active, sizeof active - 1);
    return 1;
}
#endif

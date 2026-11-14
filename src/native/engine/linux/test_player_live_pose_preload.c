#define PLAYER_VIEW_QUERY_TEST
#include "player_view_query_preload.c"
#include <assert.h>
#include <pthread.h>
#include <sys/wait.h>

struct dwarf_eh_bases { void *text, *data, *function; };
extern const void *_Unwind_Find_FDE(void *, struct dwarf_eh_bases *);
static unsigned original_calls;
static uintptr_t expected_self;
static unsigned expected_mode;
static double expected_time;
static uintptr_t fake_original(void *self, void *output, unsigned mode, double time) {
    assert((uintptr_t)self == expected_self);
    assert(mode == expected_mode);
    assert(time == expected_time);
    ++original_calls;
    memset(output, 0xa5, 64);
    errno = ERANGE;
    return UINT64_C(0xfedcba9876543210);
}
static void *foreign_scope(void *argument) {
    (void)argument;
    assert(!x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OPEN));
    return NULL;
}
static void *foreign_call(void *argument) {
    unsigned char output[64] = {0};
    uintptr_t (*entry)(void *, void *, unsigned, double) = argument;
    assert(entry((void *)expected_self, output, expected_mode, expected_time) ==
           UINT64_C(0xfedcba9876543210));
    return NULL;
}
int main(void) {
    unsigned char *page = mmap(NULL, PVQ_PAGE, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(page != MAP_FAILED);
    emit_entry_stub(page, (uintptr_t)live_pose_dispatch);
    assert(!mprotect(page, PVQ_PAGE, PROT_READ | PROT_EXEC));
    assert(register_frame_for_entry((uintptr_t)page, live_pose_frame));
    struct dwarf_eh_bases bases = {0};
    assert(_Unwind_Find_FDE(page, &bases) && bases.function == page);
    assert(_Unwind_Find_FDE((void *)player_live_pose_original, &bases));
    /* Execute the actual relocated prologue and continuation at the pinned VA. */
    unsigned char *stock = mmap((void *)0x1628000, PVQ_PAGE, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    unsigned char *near = mmap((void *)0x1630000, PVQ_PAGE, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(stock != MAP_FAILED && near != MAP_FAILED);
    unsigned char machine[] = {
        0x41,0x54,0x4c,0x8d,0x67,0xa8,0x55, /* exact stolen instructions */
        0x48,0x89,0xf5, 0x89,0x55,0x00,       /* mode to output */
        0xf2,0x0f,0x11,0x45,0x08,           /* time to output */
        0x48,0xb8,0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe,
        0x5d,0x41,0x5c,0xc3
    };
    memcpy(stock + 0xf20, machine, sizeof machine);
    emit_entry_stub(near, (uintptr_t)live_pose_dispatch);
    uint64_t patched = 0, initial = LIVE_POSE_WORD;
    assert(make_entry_patch(LIVE_POSE_ENTRY, (uintptr_t)near, initial, &patched));
    assert(__atomic_compare_exchange_n((uint64_t *)(stock + 0xf20), &initial, patched, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
    assert(!mprotect(stock, PVQ_PAGE, PROT_READ | PROT_EXEC));
    assert(!mprotect(near, PVQ_PAGE, PROT_READ | PROT_EXEC));
    live_pose_original = player_live_pose_original;
    unsigned char native_output[64] = {0};
    uintptr_t (*stock_entry)(void *, void *, unsigned, double) = (void *)LIVE_POSE_ENTRY;
    assert(stock_entry((void *)0x12345000, native_output, 1, 7.125) ==
           UINT64_C(0xfedcba9876543210));
    double native_time = 0;
    memcpy(&native_time, native_output + 8, sizeof native_time);
    assert(native_output[0] == 1 && native_time == 7.125);
    assert(!munmap(stock, PVQ_PAGE) && !munmap(near, PVQ_PAGE));
    live_pose_information.installed = 1;
    live_pose_original = fake_original;
    expected_self = UINT64_C(0x12345000);
    expected_mode = 1;
    expected_time = 7.125;
    uintptr_t (*entry)(void *, void *, unsigned, double) = (void *)page;
    unsigned char output[64] = {0};
    errno = EINVAL;
    assert(entry((void *)expected_self, output, expected_mode, expected_time) ==
           UINT64_C(0xfedcba9876543210));
    assert(errno == ERANGE && original_calls == 1 && !live_pose_capture.started);
    atomic_store(&owner_tid, current_thread());
    ui_depth[0] = 1;

    assert(x4mp_live_pose_bind(current_thread(), expected_self));
    assert(!x4mp_live_pose_bind(current_thread(), expected_self));
    assert(!x4mp_live_pose_tick(9));
    memset(output, 0x12, sizeof output);
    errno = EINVAL;
    assert(entry((void *)expected_self, output, expected_mode, expected_time) ==
           UINT64_C(0xfedcba9876543210));
    assert(errno == ERANGE && original_calls == 2);
    assert(live_pose_capture.started == 1 && live_pose_capture.returned == 1);
    assert(live_pose_capture.count == 1 && live_pose_capture.rows[0].last_move == 9);
    assert(live_pose_capture.rows[0].before_valid && live_pose_capture.rows[0].after_valid);
    assert(live_pose_capture.rows[0].before[0] == 0x12 && live_pose_capture.rows[0].after[0] == 0xa5);
    assert(live_pose_capture.rows[0].move_depth == 1 && live_pose_capture.rows[0].render_depth == 0);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, foreign_call, page));
    assert(!pthread_join(thread, NULL));
    assert(original_calls == 3 && live_pose_capture.started == 1);
    pid_t nested_child = fork();
    assert(nested_child >= 0);
    if (!nested_child) {
        live_pose_owner = current_thread();
        live_pose_depth = 1;
        assert(entry((void *)expected_self, output, expected_mode, expected_time) ==
               UINT64_C(0xfedcba9876543210));
        assert(live_pose_capture.nested == 1);
        player_live_pose_snapshot rejected;
        assert(!x4mp_live_pose_finish(&rejected));
        _exit(0);
    }
    int nested_status = 0;
    assert(waitpid(nested_child, &nested_status, 0) == nested_child);
    assert(WIFEXITED(nested_status) && WEXITSTATUS(nested_status) == 0);
    assert(!x4mp_live_pose_tick(11));
    for (uint64_t generation = 10; generation <= 168; ++generation)
        assert(x4mp_live_pose_tick(generation));
    assert(live_pose_capture.phase_move == 1 && live_pose_capture.mode_one == 1);
    expected_mode = 0;
    for (unsigned index = 0; index < 40; ++index) {
        assert(entry((void *)expected_self, output, expected_mode, expected_time) ==
               UINT64_C(0xfedcba9876543210));
    }
    assert(live_pose_capture.count == PLAYER_LIVE_POSE_ROWS && live_pose_capture.overflow == 9);
    assert(live_pose_capture.mode_zero == 40 && live_pose_capture.mode_one == 1);
    assert(!x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_PAUSED));
    assert(!pthread_create(&thread, NULL, foreign_scope, NULL));
    assert(!pthread_join(thread, NULL));
    player_live_pose_scope_snapshot scopes;
    assert(!x4mp_live_pose_scope_result(&scopes));
    for (unsigned index = 0; index < 160; ++index) {
        assert(x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OPEN));
        assert(!x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OPEN));
        assert(!x4mp_live_pose_scope(PLAYER_LIVE_POSE_PAUSED));
        assert(live_pose_scopes.state == PLAYER_LIVE_POSE_CHANGING);
        if (!index) {
            for (unsigned sample = 0; sample < 2; ++sample)
                assert(entry((void *)expected_self, output, expected_mode, expected_time) ==
                       UINT64_C(0xfedcba9876543210));
        }
        assert(x4mp_live_pose_scope(PLAYER_LIVE_POSE_OPEN));
        assert(!x4mp_live_pose_scope(PLAYER_LIVE_POSE_OPEN));
        player_live_pose_snapshot unfinished;
        assert(!x4mp_live_pose_finish(&unfinished));
        if (!index) {
            for (unsigned sample = 0; sample < 2; ++sample)
                assert(entry((void *)expected_self, output, expected_mode, expected_time) ==
                       UINT64_C(0xfedcba9876543210));
            assert(x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_PAUSED));
            assert(x4mp_live_pose_scope(PLAYER_LIVE_POSE_PAUSED));
            for (unsigned sample = 0; sample < 2; ++sample)
                assert(entry((void *)expected_self, output, expected_mode, expected_time) ==
                       UINT64_C(0xfedcba9876543210));
            assert(x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OPEN));
            assert(x4mp_live_pose_scope(PLAYER_LIVE_POSE_OPEN));
        }
        assert(x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OUTSIDE));
        assert(x4mp_live_pose_scope(PLAYER_LIVE_POSE_OUTSIDE));
    }
    unsigned char zeros[64];
    memset(zeros, 0xff, sizeof zeros);
    assert(!live_pose_copy(zeros, (void *)1));
    for (unsigned index = 0; index < sizeof zeros; ++index) assert(!zeros[index]);
    unsigned char *partial = mmap(NULL, 2 * PVQ_PAGE, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(partial != MAP_FAILED);
    memset(partial + PVQ_PAGE - 32, 0x55, 32);
    assert(!mprotect(partial + PVQ_PAGE, PVQ_PAGE, PROT_NONE));
    memset(zeros, 0xff, sizeof zeros);
    assert(!live_pose_copy(zeros, partial + PVQ_PAGE - 32));
    for (unsigned index = 0; index < sizeof zeros; ++index) assert(!zeros[index]);
    assert(!munmap(partial, 2 * PVQ_PAGE));
    player_live_pose_snapshot snapshot;
    ++live_pose_capture.started;
    assert(!x4mp_live_pose_finish(&snapshot));
    --live_pose_capture.started;
    assert(x4mp_live_pose_finish(&snapshot) && snapshot.closed && snapshot.count == PLAYER_LIVE_POSE_ROWS);
    assert(x4mp_live_pose_scope_result(&scopes));
    assert(scopes.counts[PLAYER_LIVE_POSE_OUTSIDE] == 41);
    assert(scopes.counts[PLAYER_LIVE_POSE_OPEN] == 2 && scopes.counts[PLAYER_LIVE_POSE_PAUSED] == 2);
    assert(scopes.counts[PLAYER_LIVE_POSE_CHANGING] == 2);
    assert(scopes.opens == 161 && scopes.pauses == 1 && scopes.closes == 160);
    assert(scopes.rows[PLAYER_LIVE_POSE_OPEN][0].ordinal == 44);
    assert(scopes.rows[PLAYER_LIVE_POSE_PAUSED][0].ordinal == 46);
    assert(scopes.state == PLAYER_LIVE_POSE_OUTSIDE);
    assert(!x4mp_live_pose_finish(&snapshot));

    /* A 180-frame opt-in closes all 170 Move/Render scopes and the 169
     * separately restored refill/projection pairs after its first frame. */
    assert(player_ui_session_parse("180", &preload_session_budget));
    memset(&live_pose_capture, 0, sizeof live_pose_capture);
    memset(&live_pose_scopes, 0, sizeof live_pose_scopes);
    live_pose_last_move = 9;
    atomic_store(&live_pose_active, 1);
    atomic_store(&game_render_installed, 1);
    for (unsigned index = 0; index < 170; ++index) {
        if (index) assert(x4mp_live_pose_tick(index + 9));
        ui_depth[0] = 1;
        assert(x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OPEN));
        assert(x4mp_live_pose_scope(PLAYER_LIVE_POSE_OPEN));
        assert(x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OUTSIDE));
        assert(x4mp_live_pose_scope(PLAYER_LIVE_POSE_OUTSIDE));
        ui_depth[0] = 0;
        game_render_depth = 1;
        assert(x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OPEN));
        assert(x4mp_live_pose_scope(PLAYER_LIVE_POSE_OPEN));
        assert(x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OUTSIDE));
        assert(x4mp_live_pose_scope(PLAYER_LIVE_POSE_OUTSIDE));
        game_render_depth = 0;
        if (index) {
            camera_refill_depth = 1;
            assert(x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OPEN));
            assert(x4mp_live_pose_scope(PLAYER_LIVE_POSE_OPEN));
            assert(x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OUTSIDE));
            assert(x4mp_live_pose_scope(PLAYER_LIVE_POSE_OUTSIDE));
            camera_refill_depth = 0;
            ++camera_refill_completed;
            projection_fov_depth = 2;
            assert(!x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OPEN));
            projection_fov_depth = 1;
            camera_refill_depth = 1;
            assert(!x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OPEN));
            camera_refill_depth = 0;
            assert(x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OPEN));
            assert(x4mp_live_pose_scope(PLAYER_LIVE_POSE_OPEN));
            assert(x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OUTSIDE));
            assert(x4mp_live_pose_scope(PLAYER_LIVE_POSE_OUTSIDE));
            projection_fov_depth = 0;
            ++projection_fov_completed;
        }
        if (index < 169) assert(!x4mp_live_pose_finish(&snapshot));
    }
    assert(live_pose_last_move == 178 && live_pose_scopes.closes == 678);
    --projection_fov_completed;
    assert(!x4mp_live_pose_finish(&snapshot));
    ++projection_fov_completed;
    assert(x4mp_live_pose_finish(&snapshot));
    assert(player_ui_session_parse("18000", &preload_session_budget));
    atomic_store(&live_pose_active, 1);
    live_pose_last_move = 17997;
    assert(x4mp_live_pose_tick(17998));
    assert(!x4mp_live_pose_tick(17999));
    atomic_store(&live_pose_active, 0);
    return 0;
}

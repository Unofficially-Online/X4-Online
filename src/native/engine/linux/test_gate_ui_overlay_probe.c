/* Synthetic integration of the one-callback lease; no game or preload. */
#define main presentation_fork_fixture_main
#include "test_gate_player_presentation_fork.c"
#undef main

static int synthetic_owner(void *context, uintptr_t p1, uintptr_t p2, uintptr_t room, uint32_t bank) {
    (void)context;
    return p1 == presentation_fork.request.player1 && p2 == presentation_fork.request.player2 &&
        room == presentation_fork.request.room && bank <= 1;
}
static uint64_t synthetic_occupied(void) {
    uintptr_t parent = 0;
    memcpy(&parent, (void *)(presentation_fork.request.player1 + 0x70), 8);
    return parent == presentation_fork.request.room ? 22 : 11;
}
static void expect_exit_223(pid_t child) {
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 223);
}
/* Run complete handler pairs against an actual synthetic lease. */
static void test_update_count(native_ui_overlay_probe *probe, unsigned updates, int leave_paused,
                              int expected_success, const char *expected_text) {
    int error_pipe[2];
    assert(pipe(error_pipe) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(error_pipe[0]);
        assert(dup2(error_pipe[1], STDERR_FILENO) == STDERR_FILENO);
        close(error_pipe[1]);
        probe->lifecycle.owner_tid = (uint64_t)syscall(SYS_gettid);
        for (unsigned i = 0; i < updates; ++i) {
            ui_overlay_pause_update(probe);
            if (leave_paused) break;
            assert(player_ui_overlay_resume(&probe->lifecycle, probe->object));
        }
        assert(ui_overlay_leave_callback(probe->object));
        _exit(0);
    }
    close(error_pipe[1]);
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status));
    assert(WEXITSTATUS(status) == (expected_success ? 0 : 223));
    char diagnostic[1024] = {0};
    ssize_t received = read(error_pipe[0], diagnostic, sizeof diagnostic - 1);
    assert(received > 0);
    close(error_pipe[0]);
    assert(strstr(diagnostic, expected_text));
}

static unsigned synthetic_scope = PLAYER_LIVE_POSE_OUTSIDE;
static unsigned synthetic_scope_calls;
static unsigned synthetic_scope_pending;
static unsigned *synthetic_scope_shared;
static int synthetic_live_pose_scope_begin(unsigned next) {
    if (!((next == PLAYER_LIVE_POSE_OPEN &&
           (synthetic_scope == PLAYER_LIVE_POSE_OUTSIDE || synthetic_scope == PLAYER_LIVE_POSE_PAUSED)) ||
          (synthetic_scope == PLAYER_LIVE_POSE_OPEN &&
           (next == PLAYER_LIVE_POSE_PAUSED || next == PLAYER_LIVE_POSE_OUTSIDE)))) return 0;
    synthetic_scope_pending = next;
    synthetic_scope = PLAYER_LIVE_POSE_CHANGING;
    if (synthetic_scope_shared) *synthetic_scope_shared = synthetic_scope;
    return 1;
}
static int synthetic_live_pose_scope(unsigned next) {
    if (synthetic_scope != PLAYER_LIVE_POSE_CHANGING || synthetic_scope_pending != next) return 0;
    synthetic_scope = next;
    if (synthetic_scope_shared) *synthetic_scope_shared = synthetic_scope;
    ++synthetic_scope_calls;
    return 1;
}

static int synthetic_scope_finish(player_live_pose_snapshot *out) {
    memset(out, 0, sizeof *out);
    out->started = out->returned = 2;
    out->phase_move = out->mode_one = 2;
    out->count = 2;
    out->closed = 1;
    return 1;
}
static int synthetic_scope_result(player_live_pose_scope_snapshot *out) {
    memset(out, 0, sizeof *out);
    out->counts[PLAYER_LIVE_POSE_OUTSIDE] = 1;
    out->counts[PLAYER_LIVE_POSE_OPEN] = 1;
    out->count[PLAYER_LIVE_POSE_OUTSIDE] = 1;
    out->count[PLAYER_LIVE_POSE_OPEN] = 1;
    out->rows[PLAYER_LIVE_POSE_OUTSIDE][0].ordinal = 1;
    out->rows[PLAYER_LIVE_POSE_OPEN][0].ordinal = 2;
    out->opens = out->closes = 160;
    return 1;
}
static void test_live_pose_scope_receipt(void) {
    int output[2];
    assert(!pipe(output));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(output[0]);
        assert(dup2(output[1], STDERR_FILENO) == STDERR_FILENO);
        close(output[1]);
        ui_overlay_multi_count = 160;
        ui_live_pose_api.finish = synthetic_scope_finish;
        ui_live_pose_api.scope_result = synthetic_scope_result;
        ui_live_pose_close();
        _exit(0);
    }
    close(output[1]);
    char text[4096] = {0};
    size_t length = 0;
    ssize_t received;
    while (length < sizeof text - 1 &&
           (received = read(output[0], text + length, sizeof text - 1 - length)) > 0)
        length += (size_t)received;
    close(output[0]);
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    const char *legacy = strstr(text, "X4 UI LIVE_POSE v=1 stage=close");
    const char *outside = strstr(text, "X4 UI POSE_SCOPE v=1 stage=sample scope=outside ordinal=1");
    const char *open = strstr(text, "X4 UI POSE_SCOPE v=1 stage=sample scope=open ordinal=2");
    const char *close = strstr(text, "X4 UI POSE_SCOPE v=1 stage=close tick=168 outside=1 open=1 paused=0 changing=0 opens=160 pauses=0 closes=160 state=outside");
    assert(legacy && outside && open && close && legacy < outside && outside < open && open < close);
}

static uint32_t synthetic_selected_bank;
static int synthetic_bank_read(uint32_t *bank) {
    if (!bank || synthetic_selected_bank > 1) return 0;
    *bank = synthetic_selected_bank;
    return 1;
}
static int synthetic_correction_canonical(void) { return 1; }
static int synthetic_correction_canonical_refuse(void) { return 0; }
static int synthetic_correction_owner(void *context, uintptr_t actor, unsigned bank) {
    (void)context;
    return actor == presentation_fork.request.player1 && bank == ui_overlay_probe.move_bank;
}
static void test_coupled_transaction(native_ui_overlay_probe *probe, float *original, float *other_bank_pose) {
    unsigned *shared = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert(shared != MAP_FAILED);
    for (unsigned failure = 0; failure < 4; ++failure) {
        shared[0] = PLAYER_LIVE_POSE_OUTSIDE;
        shared[1] = 0;
        int diagnostic_pipe[2];
        assert(!pipe(diagnostic_pipe));
        pid_t child = fork();
        assert(child >= 0);
        if (!child) {
            close(diagnostic_pipe[0]);
            assert(dup2(diagnostic_pipe[1], STDERR_FILENO) == STDERR_FILENO);
            close(diagnostic_pipe[1]);
            probe->lifecycle.owner_tid = (uint64_t)syscall(SYS_gettid);
            ui_live_pose_enabled = 1;
            ui_live_pose_api.scope_begin = synthetic_live_pose_scope_begin;
            ui_live_pose_api.scope = synthetic_live_pose_scope;
            synthetic_scope_shared = shared;
            ui_correction_enabled = 1;
            ui_overlay_test_bank_read = synthetic_bank_read;
            synthetic_selected_bank = 1;
            assert(!ui_overlay_capture_move_bank(probe));
            synthetic_selected_bank = 0;
            ui_correction_test_exchange_count = &shared[1];
            assert(ui_overlay_capture_move_bank(probe));
            ui_correction_move_begin();
            ui_correction_test_canonical = failure == 2 ? synthetic_correction_canonical_refuse :
                synthetic_correction_canonical;
            ui_correction_test_owner = synthetic_correction_owner;
            ui_correction_delta[0] = 3;
            ui_correction_delta[4] = ui_correction_delta[9] = ui_correction_delta[14] = 1;
            if (failure == 1) original[4] = 2;
            assert(player_ui_overlay_enter(&probe->lifecycle, probe->object));
            assert(failure != 1 && failure != 2 && synthetic_scope == PLAYER_LIVE_POSE_OPEN);
            if (failure == 3) {
                synthetic_selected_bank = 1;
                (void)player_ui_overlay_pause(&probe->lifecycle, probe->object);
                _exit(1);
            }
            uintptr_t slot = presentation_fork.request.player1 + 0x108;
            uintptr_t value = 0;
            memcpy(&value, (void *)slot, 8);
            assert(value == (uintptr_t)ui_correction_scratch[0] && ui_correction_scratch[0][0] == 5);
            assert(synthetic_occupied() == 22);
            assert(player_ui_overlay_pause(&probe->lifecycle, probe->object));
            memcpy(&value, (void *)slot, 8);
            assert(value == (uintptr_t)original && synthetic_occupied() == 11 &&
                   synthetic_scope == PLAYER_LIVE_POSE_PAUSED);
            assert(player_ui_overlay_resume(&probe->lifecycle, probe->object));
            memcpy(&value, (void *)slot, 8);
            assert(value == (uintptr_t)ui_correction_scratch[1] && value != (uintptr_t)ui_correction_scratch[0]);
            assert(ui_correction_scratch[0][0] == 5 && ui_correction_scratch[1][0] == 5);
            probe->final_restore = 1;
            assert(player_ui_overlay_return(&probe->lifecycle, probe->object));
            memcpy(&value, (void *)slot, 8);
            assert(value == (uintptr_t)original && synthetic_occupied() == 11 &&
                   synthetic_scope == PLAYER_LIVE_POSE_OUTSIDE);
            assert(ui_correction_applies == 2 && ui_correction_restores == 2);
            assert(ui_correction_move_closed(1));
            assert(presentation_fork.request.bank == 0);
            assert(player_ui_overlay_init(&probe->lifecycle, &probe->lifecycle.ops,
                                          probe->lifecycle.owner_tid, probe->object));
            probe->applies = probe->restores = probe->final_restore = 0;
            atomic_store_explicit(&ui_overlay_completed, 1, memory_order_release);
            synthetic_selected_bank = 1;
            assert(ui_overlay_capture_move_bank(probe));
            ui_correction_move_begin();
            uintptr_t before_second = 0;
            memcpy(&before_second, (void *)(presentation_fork.request.player1 + 0x188), 8);
            assert(before_second == (uintptr_t)other_bank_pose);
            assert(ui_correction_owner(probe, presentation_fork.request.player1, 1));
            assert(player_ui_overlay_enter(&probe->lifecycle, probe->object));
            uintptr_t second_slot = presentation_fork.request.player1 + 0x188;
            memcpy(&value, (void *)second_slot, 8);
            assert(value == (uintptr_t)ui_correction_scratch[2]);
            assert(ui_correction_scratch[2][0] == 7);
            memcpy(&value, (void *)slot, 8);
            assert(value == (uintptr_t)original);
            probe->final_restore = 1;
            assert(player_ui_overlay_return(&probe->lifecycle, probe->object));
            memcpy(&value, (void *)second_slot, 8);
            assert(value == (uintptr_t)other_bank_pose);
            assert(ui_correction_move_closed(2));
            assert(ui_correction_bank_moves[0] == 1 && ui_correction_bank_moves[1] == 1);
            assert(presentation_fork.request.bank == 0);

            /* GameRender uses another resident scratch block and restores the
             * same selected slot without invoking the Lua pause path. */
            assert(player_ui_overlay_init(&probe->lifecycle, &probe->lifecycle.ops,
                                          probe->lifecycle.owner_tid, probe->object));
            probe->applies = probe->restores = probe->final_restore = 0;
            synthetic_selected_bank = 0;
            assert(ui_overlay_capture_move_bank(probe));
            ui_correction_move_begin();
            ui_game_render_active = 1;
            assert(player_ui_overlay_enter(&probe->lifecycle, probe->object));
            memcpy(&value, (void *)slot, 8);
            assert(value == (uintptr_t)ui_render_correction_scratch[0] &&
                   value != (uintptr_t)ui_correction_scratch[0] &&
                   ui_render_correction_scratch[0][0] == 5);
            probe->final_restore = 1;
            assert(player_ui_overlay_return(&probe->lifecycle, probe->object));
            memcpy(&value, (void *)slot, 8);
            assert(value == (uintptr_t)original && synthetic_scope == PLAYER_LIVE_POSE_OUTSIDE);
            assert(ui_render_correction_applies == 1 && presentation_fork.request.bank == 0);
            ui_game_render_active = 0;

            /* Session mappings are distinct and remain unique across both scopes. */
            assert(player_ui_session_parse("180", &player_session_budget));
            assert(ui_correction_allocate_session());
            assert(!ui_correction_allocate_session());
            ui_move_correction_applies = ui_render_correction_applies = 0;
            assert(player_ui_overlay_init(&probe->lifecycle, &probe->lifecycle.ops,
                                          probe->lifecycle.owner_tid, probe->object));
            probe->applies = probe->restores = probe->final_restore = 0;
            assert(ui_overlay_capture_move_bank(probe));
            ui_correction_move_begin();
            assert(player_ui_overlay_enter(&probe->lifecycle, probe->object));
            memcpy(&value, (void *)slot, 8);
            assert(value == (uintptr_t)ui_session_move_scratch[0] &&
                   value != (uintptr_t)ui_correction_scratch[0]);
            probe->final_restore = 1;
            assert(player_ui_overlay_return(&probe->lifecycle, probe->object));
            assert(player_ui_overlay_init(&probe->lifecycle, &probe->lifecycle.ops,
                                          probe->lifecycle.owner_tid, probe->object));
            probe->applies = probe->restores = probe->final_restore = 0;
            assert(ui_overlay_capture_move_bank(probe));
            ui_correction_move_begin();
            ui_game_render_active = 1;
            assert(player_ui_overlay_enter(&probe->lifecycle, probe->object));
            memcpy(&value, (void *)slot, 8);
            assert(value == (uintptr_t)ui_session_render_scratch[0] &&
                   value != (uintptr_t)ui_session_move_scratch[0]);
            probe->final_restore = 1;
            assert(player_ui_overlay_return(&probe->lifecycle, probe->object));
            memcpy(&value, (void *)slot, 8);
            assert(value == (uintptr_t)original && presentation_fork.request.bank == 0);
            _exit(0);
        }
        close(diagnostic_pipe[1]);
        char diagnostic[2048] = {0};
        size_t received = 0;
        ssize_t chunk;
        while (received < sizeof diagnostic - 1 &&
               (chunk = read(diagnostic_pipe[0], diagnostic + received, sizeof diagnostic - 1 - received)) > 0)
            received += (size_t)chunk;
        close(diagnostic_pipe[0]);
        int status = 0;
        assert(waitpid(child, &status, 0) == child);
        if (!WIFEXITED(status)) fprintf(stderr, "fixture failure=%u signal=%d diagnostic=%s\n", failure, WTERMSIG(status), diagnostic);
        assert(WIFEXITED(status));
        if (WEXITSTATUS(status) != (failure ? 223 : 0))
            fprintf(stderr, "fixture failure=%u status=%d diagnostic=%s\n", failure, WEXITSTATUS(status), diagnostic);
        assert(WEXITSTATUS(status) == (failure ? 223 : 0));
        assert(*shared == (failure ? PLAYER_LIVE_POSE_CHANGING : PLAYER_LIVE_POSE_OUTSIDE));
        if (failure) {
            if (failure == 3) assert(shared[1] == 1);
            assert(received > 0);
            if (failure < 3) {
                assert(strstr(diagnostic, failure == 1 ? "stage=published_install" : "stage=canonical"));
                assert(strstr(diagnostic, "X4 UI OVERLAY FATAL reason=apply"));
            } else {
                assert(strstr(diagnostic, "X4 UI OVERLAY FATAL reason=restore"));
            }
        }
    }
    assert(!munmap(shared, 4096));
}

static unsigned synthetic_progress_value;
static unsigned synthetic_progress(void) { return synthetic_progress_value; }

static void test_frames(native_ui_overlay_probe *probe, unsigned count) {
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        int quiet = open("/dev/null", O_WRONLY);
        assert(quiet >= 0 && dup2(quiet, STDERR_FILENO) == STDERR_FILENO);
        close(quiet);
        probe->lifecycle.owner_tid = (uint64_t)syscall(SYS_gettid);
        ui_overlay_multi_count = count;
        ui_overlay_progress = synthetic_progress;
        synthetic_progress_value = 1;
        atomic_store(&ui_overlay_armed, 1);
        atomic_store(&ui_overlay_completed, 0);
        atomic_store(&ui_overlay_main_passes, 0);
        stats.active = 1;
        /* Fixture starts inside an existing scope; make it canonical before tick 9. */
        assert(player_ui_overlay_pause(&probe->lifecycle, probe->object));
        assert(player_ui_overlay_init(&probe->lifecycle, &probe->lifecycle.ops,
                                      probe->lifecycle.owner_tid, probe->object));
        probe->applies = probe->restores = 0;
        atomic_store(&player_recurring_main_tick, 8);
        player_ui_overlay_main_interlock();
        pid_t missing = fork();
        assert(missing >= 0);
        if (!missing) {
            atomic_store(&player_recurring_main_tick, 9);
            player_ui_overlay_main_interlock();
            _exit(1);
        }
        expect_exit_223(missing);
        for (unsigned index = 0; index < count; ++index) {
            stats.issued = 9 + index;
            assert(ui_overlay_generation_matches(index, stats.issued, stats.issued));
            assert(!ui_overlay_generation_matches(index, stats.issued + 1, stats.issued));
            ui_overlay_rearm_for_entry(probe, index);
            probe->before = synthetic_occupied();
            assert(player_ui_overlay_enter(&probe->lifecycle, probe->object));
            ui_overlay_pause_update(probe);
            assert(player_ui_overlay_resume(&probe->lifecycle, probe->object));
            assert(ui_overlay_leave_callback(probe->object));
            assert(atomic_load(&ui_overlay_completed) == index + 1);
            assert(synthetic_occupied() == 11);
            synthetic_progress_value = index + 1 == count ? 3 : 1;
            atomic_store(&player_recurring_main_tick, 9 + index);
            if (index == 0) {
                pid_t inflight = fork();
                assert(inflight >= 0);
                if (!inflight) {
                    synthetic_progress_value = 2;
                    player_ui_overlay_main_interlock();
                    _exit(1);
                }
                expect_exit_223(inflight);
            }
            player_ui_overlay_main_interlock();
            if (index == 0) {
                pid_t missing_render = fork();
                assert(missing_render >= 0);
                if (!missing_render) {
                    ui_game_render_enabled = 1;
                    ui_game_render_progress = synthetic_progress;
                    atomic_store(&ui_game_render_closed, 0);
                    atomic_store(&player_recurring_main_tick, 9);
                    player_ui_overlay_main_interlock();
                    _exit(1);
                }
                expect_exit_223(missing_render);
            }
        }
        assert(!ui_overlay_generation_matches(count, 9 + count, 9 + count));
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
}

static void test_plan_kick_mutex_admission(void) {
    unsigned char *owner = private_page(0x1000);
    pthread_mutex_t *mutex = (pthread_mutex_t *)(owner + 0x80);
    assert(!pthread_mutex_init(mutex, NULL));
    assert(!ui_plan_kick_trylock((uintptr_t)owner));
    assert(!pthread_mutex_destroy(mutex));
    pthread_mutexattr_t attributes;
    assert(!pthread_mutexattr_init(&attributes));
    assert(!pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE));
    assert(!pthread_mutex_init(mutex, &attributes));
    assert(!pthread_mutexattr_destroy(&attributes));
    assert(ui_plan_kick_trylock((uintptr_t)owner));
    assert(!pthread_mutex_lock(mutex)); /* Native handler's same-thread recursive acquisition. */
    assert(!pthread_mutex_unlock(mutex));
    assert(!pthread_mutex_unlock(mutex));
    assert(!pthread_mutex_destroy(mutex));
    assert(!ui_plan_kick_trylock(UINTPTR_MAX));
}

static unsigned live_pose_tick_calls;
static int synthetic_live_pose_tick(uint64_t generation) {
    ++live_pose_tick_calls;
    return generation == 10;
}
static void test_live_pose_first_generation(void) {
    ui_live_pose_enabled = 1;
    ui_live_pose_api.tick = synthetic_live_pose_tick;
    ui_live_pose_generation = 9;
    assert(ui_live_pose_advance(0, 9) && live_pose_tick_calls == 0);
    assert(ui_live_pose_advance(0, 10) && live_pose_tick_calls == 1);
    assert(ui_live_pose_advance(1, 10) && live_pose_tick_calls == 1);
    assert(!ui_live_pose_advance(1, 11) && live_pose_tick_calls == 2);
    ui_live_pose_enabled = 0;
}

int main(void) {
    ui_overlay_multi_count = 160;
    assert(ui_overlay_full_window_selected());
    ui_overlay_multi_count = 8;
    assert(!ui_overlay_full_window_selected());
    assert(player_ui_session_parse("900", &player_session_budget));
    assert(!ui_overlay_full_window_selected());
    ui_overlay_multi_count = 160;
    assert(!ui_overlay_full_window_selected());
    ui_overlay_multi_count = 890;
    assert(ui_overlay_full_window_selected());
    ++ui_overlay_multi_count;
    assert(!ui_overlay_full_window_selected());
    player_session_budget = (player_ui_session_budget){0};
    ui_overlay_multi_count = 0;
    assert(ui_game_render_sequence(1, 0, 9, 9, 0x1234, 0));
    assert(ui_game_render_sequence(160, 159, 168, 168, 0x1234, 0x1234));
    assert(!ui_game_render_sequence(1, 1, 9, 9, 0x1234, 0x1234)); /* duplicate */
    assert(!ui_game_render_sequence(2, 0, 10, 10, 0x1234, 0x1234)); /* missing */
    assert(!ui_game_render_sequence(1, 0, 9, 9, 0x5678, 0x1234));
    assert(!ui_game_render_sequence(1, 0, 9, 10, 0x1234, 0x1234));
    test_live_pose_first_generation();
    test_live_pose_scope_receipt();
    ui_seat_query_identity first_identity = {0}, second_identity = {0};
    assert(!ui_correction_identity_difference(&first_identity, &second_identity));
    second_identity.actor_controller = 1;
    second_identity.position = 3;
    assert(ui_correction_identity_difference(&first_identity, &second_identity) ==
           ((UINT32_C(1) << 3) | (UINT32_C(1) << 15)));
    image = private_page(0x4000000);
    test_plan_kick_mutex_admission();
    void *p1 = private_page(0x7000), *p2 = private_page(0x7000);
    void *old_room = private_page(0x1000), *guest_room = private_page(0x1000);
    uintptr_t *old_vector = private_page(0x1000), *new_vector = private_page(0x1000);
    old_vector[0] = 0x1110; new_vector[0] = 0x2220;
    player_presentation_vector first = {(uintptr_t)old_vector, (uintptr_t)(old_vector + 1), (uintptr_t)(old_vector + 1)};
    player_presentation_vector second = {(uintptr_t)new_vector, (uintptr_t)(new_vector + 1), (uintptr_t)(new_vector + 1)};
    put_pointer(p1, 0x70, (uintptr_t)old_room); put_pointer(p1, 0xe0, (uintptr_t)old_room);
    put_pointer(p2, 0x70, (uintptr_t)guest_room);
    memcpy((unsigned char *)p1 + 0x308, &first, sizeof first);
    memcpy((unsigned char *)p2 + 0x308, &second, sizeof second);
    uint32_t old_position = 3, new_position = 7;
    memcpy((unsigned char *)p1 + 0x6aa8, &old_position, 4);
    memcpy((unsigned char *)p2 + 0x6aa8, &new_position, 4);
    presentation_fork.request = (presentation_fork_request){.player1=(uintptr_t)p1, .player2=(uintptr_t)p2,
        .room=(uintptr_t)guest_room, .expected_p1_ship=11, .expected_p2_ship=22, .bank=0};
    presentation_fork.occupied = synthetic_occupied;
    native_ui_overlay_probe *probe = &ui_overlay_probe;
    probe->object = 0x1234;
    probe->operations = (player_presentation_overlay_ops){probe, synthetic_owner, ui_overlay_read, ui_overlay_write};
    player_ui_overlay_ops lifecycle = {probe, ui_overlay_tid, ui_overlay_apply, ui_overlay_restore, ui_overlay_fatal};
    assert(player_ui_overlay_init(&probe->lifecycle, &lifecycle, syscall(SYS_gettid), probe->object));
    float *published_pose = private_page(4096);
    published_pose[0] = 2;
    published_pose[4] = published_pose[9] = published_pose[14] = 1;
    put_pointer(p1, 0x108, (uintptr_t)published_pose);
    float *other_bank_pose = private_page(4096);
    other_bank_pose[0] = 4;
    other_bank_pose[4] = other_bank_pose[9] = other_bank_pose[14] = 1;
    put_pointer(p1, 0x160, (uintptr_t)old_room);
    put_pointer(p1, 0x188, (uintptr_t)other_bank_pose);
    test_coupled_transaction(probe, published_pose, other_bank_pose);
    pid_t scope_child = fork();
    assert(scope_child >= 0);
    if (!scope_child) {
        probe->lifecycle.owner_tid = (uint64_t)syscall(SYS_gettid);
        ui_live_pose_enabled = 1;
        ui_live_pose_api.scope_begin = synthetic_live_pose_scope_begin;
        ui_live_pose_api.scope = synthetic_live_pose_scope;
        assert(player_ui_overlay_enter(&probe->lifecycle, probe->object));
        assert(synthetic_scope == PLAYER_LIVE_POSE_OPEN);
        assert(player_ui_overlay_pause(&probe->lifecycle, probe->object));
        assert(synthetic_scope == PLAYER_LIVE_POSE_PAUSED);
        assert(player_ui_overlay_resume(&probe->lifecycle, probe->object));
        assert(synthetic_scope == PLAYER_LIVE_POSE_OPEN);
        probe->final_restore = 1;
        assert(player_ui_overlay_return(&probe->lifecycle, probe->object));
        assert(synthetic_scope == PLAYER_LIVE_POSE_OUTSIDE && synthetic_scope_calls == 4);
        _exit(0);
    }
    int scope_status = 0;
    assert(waitpid(scope_child, &scope_status, 0) == scope_child);
    assert(WIFEXITED(scope_status) && WEXITSTATUS(scope_status) == 0);
    assert(player_ui_overlay_enter(&probe->lifecycle, probe->object));
    assert(synthetic_occupied() == 22 && probe->during == 22);
    stats.issued = 9;
    test_frames(probe, 8);
    test_frames(probe, 160);
    test_update_count(probe, 0, 0, 0, "tick=9 applies=1 restores=0 phase=2");
    test_update_count(probe, 1, 0, 1, "v=2 status=pass tick=9 updates=1 applies=2 restores=2");
    test_update_count(probe, 2, 0, 1, "v=2 status=pass tick=9 updates=2 applies=3 restores=3");
    test_update_count(probe, 8, 0, 1, "v=2 status=pass tick=9 updates=8 applies=9 restores=9");
    test_update_count(probe, 9, 0, 0, "X4 UI OVERLAY FATAL reason=update_limit");
    test_update_count(probe, 1, 1, 0, "tick=9 applies=1 restores=1 phase=3");
    pid_t child = fork(); assert(child >= 0);
    if (!child) { player_ui_overlay_frame_interlock(&probe->lifecycle); _exit(1); }
    expect_exit_223(child);
    assert(player_ui_overlay_pause(&probe->lifecycle, probe->object));
    assert(synthetic_occupied() == 11 && probe->paused == 11);
    int error_pipe[2];
    assert(pipe(error_pipe) == 0);
    child = fork(); assert(child >= 0);
    if (!child) {
        close(error_pipe[0]);
        assert(dup2(error_pipe[1], STDERR_FILENO) == STDERR_FILENO);
        close(error_pipe[1]);
        /* The child must pass owner authentication to exercise tamper refusal. */
        probe->lifecycle.owner_tid = (uint64_t)syscall(SYS_gettid);
        uint32_t wrong = 9;
        memcpy((unsigned char *)p1 + 0x6aa8, &wrong, 4);
        player_ui_overlay_resume(&probe->lifecycle, probe->object);
        _exit(1);
    }
    close(error_pipe[1]);
    expect_exit_223(child);
    char diagnostic[256] = {0};
    ssize_t received = read(error_pipe[0], diagnostic, sizeof diagnostic - 1);
    assert(received > 0);
    close(error_pipe[0]);
    assert(strstr(diagnostic, "X4 UI OVERLAY FATAL reason=apply") != NULL);
    assert(player_ui_overlay_resume(&probe->lifecycle, probe->object));
    assert(synthetic_occupied() == 22 && probe->resumed == 22);
    assert(player_ui_overlay_return(&probe->lifecycle, probe->object));
    assert(synthetic_occupied() == 11 && probe->restored == 11);
    assert(player_ui_overlay_frame_interlock(&probe->lifecycle));
    /* Missing refill at an eligible worker tail must terminate this disposable run. */
    ui_camera_refill_enabled = 0;
    player_ui_camera_refill_tail(player_startup()->presentation_first + 1);
    ui_camera_refill_enabled = 1;
    ui_overlay_multi_count = 160;
    ui_camera_refill_last_tick = 0;
    child = fork();
    assert(child >= 0);
    if (!child) {
        player_ui_camera_refill_tail(player_startup()->presentation_first + 1);
        _exit(1);
    }
    expect_exit_223(child);
    ui_camera_refill_enabled = 0;
    puts("UI overlay one-callback integration PASS");
    return 0;
}

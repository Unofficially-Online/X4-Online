#define PLAYER_VIEW_QUERY_TEST
#include "player_view_query_preload.c"
#include <assert.h>
#include <pthread.h>
#include <setjmp.h>
#include <sys/wait.h>

static unsigned enters, leaves, calls;
static int recurse, jump_from_original;
static jmp_buf jump_target;
static int begin(uintptr_t object) {
    assert(object == 0x1234 && game_render_depth == 1);
    ++enters;
    errno = ECHILD;
    return 1;
}
static int end(uintptr_t object) {
    assert(object == 0x1234 && game_render_depth == 1);
    ++leaves;
    return 1;
}
static void original(void *object) {
    assert((uintptr_t)object == 0x1234 && errno == EINVAL);
    ++calls;
    if (jump_from_original) _longjmp(jump_target, 1);
    if (recurse) game_render_dispatch(object);
    errno = ERANGE;
}
static void *foreign(void *object) {
    game_render_dispatch(object);
    return NULL;
}
static void expect_fatal(pid_t child) {
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 223);
}
int main(void) {
    game_render_original = original;
    atomic_store(&owner_tid, current_thread());
    atomic_store(&game_render_installed, 1);
    ui_depth[0] = 1;
    assert(x4mp_view_query_game_render_bind(current_thread(), begin, end));
    assert(x4mp_view_query_game_render_progress() == 4);
    errno = EINVAL;
    game_render_dispatch((void *)0x1234);
    assert(errno == ERANGE && calls == 1 && !enters && !leaves);
    calls = 0;
    assert(x4mp_view_query_game_render_activate());
    ui_depth[0] = 0;
    assert(!x4mp_view_query_game_render_bind(current_thread(), begin, end));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        atomic_store(&owner_tid, current_thread());
        game_render_tid = current_thread();
        recurse = 1;
        errno = EINVAL;
        game_render_dispatch((void *)0x1234);
        _exit(1);
    }
    expect_fatal(child);
    child = fork();
    assert(child >= 0);
    if (!child) {
        game_render_tid = current_thread();
        if (setjmp(jump_target)) _exit(1);
        jump_from_original = 1;
        errno = EINVAL;
        game_render_dispatch((void *)0x1234);
        _exit(1);
    }
    expect_fatal(child);
    child = fork();
    assert(child >= 0);
    if (!child) {
        pthread_t thread;
        assert(!pthread_create(&thread, NULL, foreign, (void *)0x1234));
        pthread_join(thread, NULL);
        _exit(1);
    }
    expect_fatal(child);
    for (unsigned i = 0; i < 160; ++i) {
        errno = EINVAL;
        game_render_dispatch((void *)0x1234);
        assert(errno == ERANGE && enters == i + 1 && leaves == i + 1 && calls == i + 1);
        assert(x4mp_view_query_game_render_progress() == (i == 159 ? 3u : 1u));
    }
    errno = EINVAL;
    game_render_dispatch((void *)0x1234);
    assert(errno == ERANGE && enters == 160 && leaves == 160 && calls == 161);

    assert(player_ui_session_parse("180", &preload_session_budget));
    assert(preload_session_budget.presentation_count == 170 &&
           preload_session_budget.recurring_end == 184 && preload_session_budget.last_capture == 182);
    assert(session_preload_config(&preload_session_budget, "1", "1", "1", "1", "1", NULL, NULL, NULL));
    assert(!session_preload_config(&preload_session_budget, "1", "1", "1", "1", "1", NULL, NULL, "1"));
    assert(!session_preload_config(&preload_session_budget, "1", "1", NULL, "1", "1", NULL, NULL, NULL));
    assert(session_preload_config(&preload_session_budget, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
    atomic_store(&game_render_binding, 0);
    ui_depth[0] = 1;
    assert(x4mp_view_query_game_render_bind(current_thread(), begin, end));
    assert(game_render_remaining == 170 && x4mp_view_query_game_render_activate());
    ui_depth[0] = 0;
    enters = leaves = calls = 0;
    for (unsigned i = 0; i < 170; ++i) {
        errno = EINVAL;
        game_render_dispatch((void *)0x1234);
    }
    assert(enters == 170 && leaves == 170 && calls == 170 && x4mp_view_query_game_render_progress() == 3);
    assert(player_ui_session_parse("18000", &preload_session_budget));
    assert(preload_session_budget.presentation_count == 17990 &&
           preload_session_budget.presentation_last == 17998 && preload_session_budget.report_tick == 17999);
    atomic_store(&game_render_binding, 0);
    ui_depth[0] = 1;
    assert(x4mp_view_query_game_render_bind(current_thread(), begin, end));
    assert(game_render_remaining == 17990 && x4mp_view_query_game_render_activate());
    ui_depth[0] = 0;
    enters = leaves = calls = 0;
    for (unsigned i = 0; i < 17990; ++i) {
        errno = EINVAL;
        game_render_dispatch((void *)0x1234);
    }
    assert(enters == 17990 && leaves == 17990 && calls == 17990 && x4mp_view_query_game_render_progress() == 3);
    assert(!player_ui_session_parse("179", &preload_session_budget));
    assert(!player_ui_session_parse("18001", &preload_session_budget));
    assert(!player_ui_session_parse("0180", &preload_session_budget));
    assert(!player_ui_session_parse("99999999999999999999999", &preload_session_budget));
    assert(!player_ui_session_parse("180x", &preload_session_budget));
    return 0;
}

#define PLAYER_VIEW_QUERY_TEST
#include "player_view_query_preload.c"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <sys/wait.h>

static _Atomic unsigned calls[2];
static _Atomic uintptr_t seen_object;
static int recurse;
static int drain_test, bind_requested;
static unsigned overlay_enters, overlay_leaves;
static int fake_overlay_enter(uintptr_t object) {
    assert(object == 0x1234); ++overlay_enters; errno = ECHILD; return 1;
}
static int fake_overlay_leave(uintptr_t object) {
    assert(object == 0x1234); ++overlay_leaves; errno = ECHILD; return 1;
}
static _Atomic int foreign_entered, release_foreign;

static uint64_t fake_query(void) {
    errno = EDOM;
    return UINT64_C(0x987654321);
}

static void fake_move(void *object) {
    assert(errno == EINVAL);
    ++calls[0];
    seen_object = (uintptr_t)object;
    assert(player_view_query_dispatch() == UINT64_C(0x987654321));
    assert(errno == EDOM);
    if (recurse) {
        recurse = 0;
        ui_render_dispatch(object);
    }
    if (bind_requested) {
        int mode = bind_requested;
        bind_requested = 0;
        assert(mode == 3 ?
            x4mp_view_query_ui_bind_count(current_thread(), (uintptr_t)object, fake_overlay_enter, fake_overlay_leave, 160) :
            mode == 2 ? x4mp_view_query_ui_bind_eight(current_thread(), (uintptr_t)object, fake_overlay_enter, fake_overlay_leave) :
            x4mp_view_query_ui_bind(current_thread(), (uintptr_t)object, fake_overlay_enter, fake_overlay_leave));
    }
    if (drain_test) assert(x4mp_view_query_ui_finish());
    errno = ERANGE;
}

static void fake_render(void *object) {
    assert(errno == EDOM || errno == EINVAL);
    ++calls[1];
    seen_object = (uintptr_t)object;
    if (drain_test && current_thread() != atomic_load(&owner_tid)) {
        atomic_store(&foreign_entered, 1);
        while (!atomic_load(&release_foreign)) sched_yield();
    }
    errno = EAGAIN;
}

static void *foreign_callback(void *object) {
    player_ui_callback_snapshot denied;
    assert(!x4mp_view_query_ui_callbacks(&denied));
    errno = EINVAL;
    ui_render_dispatch(object);
    assert(errno == EAGAIN);
    return NULL;
}

int main(void) {
    information.installed = 1;
    original_query = fake_query;
    ui_original[0] = fake_move;
    ui_original[1] = fake_render;
    assert(x4mp_view_query_configure(current_thread(), current_thread()));
    atomic_store(&ui_installed, 1);
    void *object = (void *)(uintptr_t)0x1234;
    recurse = 1;
    errno = EINVAL;
    ui_move_dispatch(object);
    assert(errno == ERANGE && calls[0] == 1 && calls[1] == 1);
    assert(seen_object == (uintptr_t)object);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, foreign_callback, (void *)(uintptr_t)0x5678));
    assert(!pthread_join(thread, NULL));
    player_ui_callback_snapshot snapshot;
    assert(x4mp_view_query_ui_callbacks(&snapshot));
    assert(snapshot.entered[0] == 1 && snapshot.left[0] == 1 && snapshot.queries[0] == 1);
    assert(snapshot.entered[1] == 2 && snapshot.left[1] == 2 && snapshot.nested[1] == 1);
    assert(snapshot.foreign[1] == 1 && snapshot.object_mismatch[1] == 1);
    assert(snapshot.first_tid[0] == current_thread());
    assert(snapshot.first_tid[1] == current_thread() && snapshot.thread_mismatch[1] == 1);
    assert(snapshot.object == (uintptr_t)object && !snapshot.inflight[0] && !snapshot.inflight[1]);
    /* Binding inside one Move targets the next invocation, never this one. */
    bind_requested = 1;
    errno = EINVAL; ui_move_dispatch(object);
    assert(!overlay_enters && !overlay_leaves);
    pid_t bad = fork();
    assert(bad >= 0);
    if (!bad) { ui_move_dispatch((void *)(uintptr_t)0x9876); _exit(1); }
    int status = 0;
    assert(waitpid(bad, &status, 0) == bad && WIFEXITED(status) && WEXITSTATUS(status) == 223);
    errno = EINVAL; ui_move_dispatch(object);
    assert(errno == ERANGE && overlay_enters == 1 && overlay_leaves == 1);
    errno = EINVAL; ui_move_dispatch(object);
    assert(overlay_enters == 1 && overlay_leaves == 1);
    assert(!x4mp_view_query_ui_bind(current_thread(), (uintptr_t)object, fake_overlay_enter, fake_overlay_leave));

    /* An eight-callback lease closes each scope before rearming. */
    pid_t eight_child = fork();
    assert(eight_child >= 0);
    if (!eight_child) {
        atomic_store(&owner_tid, current_thread());
        atomic_store(&ui_overlay_binding, 0);
        overlay_enters = overlay_leaves = 0;
        bind_requested = 2;
        errno = EINVAL; ui_move_dispatch(object);
        assert(x4mp_view_query_ui_overlay_progress() == 1);
        for (unsigned i = 0; i < 8; ++i) {
            errno = EINVAL; ui_move_dispatch(object);
            assert(errno == ERANGE && overlay_enters == i + 1 && overlay_leaves == i + 1);
            assert(x4mp_view_query_ui_overlay_progress() == (i == 7 ? 3U : 1U));
        }
        errno = EINVAL; ui_move_dispatch(object);
        assert(overlay_enters == 8 && overlay_leaves == 8);
        _exit(0);
    }
    assert(waitpid(eight_child, &status, 0) == eight_child && WIFEXITED(status) && !WEXITSTATUS(status));

    pid_t long_child = fork();
    assert(long_child >= 0);
    if (!long_child) {
        atomic_store(&owner_tid, current_thread());
        atomic_store(&ui_overlay_binding, 0);
        overlay_enters = overlay_leaves = 0;
        assert(!x4mp_view_query_ui_bind_count(current_thread(), (uintptr_t)object,
                                               fake_overlay_enter, fake_overlay_leave, 159));
        assert(!x4mp_view_query_ui_bind_count(current_thread(), (uintptr_t)object,
                                               fake_overlay_enter, fake_overlay_leave, 1));
        bind_requested = 3;
        errno = EINVAL; ui_move_dispatch(object);
        for (unsigned i = 0; i < 160; ++i) {
            errno = EINVAL; ui_move_dispatch(object);
            assert(errno == ERANGE && overlay_enters == i + 1 && overlay_leaves == i + 1);
            assert(x4mp_view_query_ui_overlay_progress() == (i == 159 ? 3U : 1U));
        }
        errno = EINVAL; ui_move_dispatch(object);
        assert(overlay_enters == 160 && overlay_leaves == 160);
        _exit(0);
    }
    assert(waitpid(long_child, &status, 0) == long_child && WIFEXITED(status) && !WEXITSTATUS(status));

    /* Arrange a foreign callback already in flight when the owner closes.
     * The next entry is not admitted and cannot postpone the drain. */
    memset(ui_counts, 0, sizeof ui_counts);
    atomic_store(&ui_object, 0);
    atomic_store(&ui_epoch, 0);
    drain_test = 1;
    assert(!pthread_create(&thread, NULL, foreign_callback, object));
    while (!atomic_load(&foreign_entered)) sched_yield();
    errno = EINVAL;
    ui_move_dispatch(object);
    assert(atomic_load(&ui_epoch) == (UI_REQUEST | 1));
    errno = EINVAL;
    ui_render_dispatch(object);
    assert(errno == EAGAIN);
    assert(atomic_load(&ui_counts[1].entered) == 1);
    atomic_store(&release_foreign, 1);
    assert(!pthread_join(thread, NULL));
    assert(atomic_load(&ui_epoch) == UI_CLOSED);
    assert(x4mp_view_query_ui_callbacks(&snapshot) && snapshot.closed);
    assert(snapshot.entered[0] == snapshot.left[0] && snapshot.entered[1] == snapshot.left[1]);
    assert(!snapshot.inflight[0] && !snapshot.inflight[1] && snapshot.foreign[1] == 1);
    assert(!x4mp_view_query_ui_finish());

    uintptr_t *page = mmap(NULL, PVQ_PAGE, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(page != MAP_FAILED);
    page[0] = 0x1111;
    page[1] = 0x2222;
    assert(!mprotect(page, PVQ_PAGE, PROT_READ));
    const uintptr_t originals[2] = {0x1111, 0x2222};
    const uintptr_t wrong[2] = {0x1111, 0x3333};
    const uintptr_t replacements[2] = {0x4444, 0x5555};
    assert(!install_ui_slot_pair((uintptr_t)&page[0], (uintptr_t)&page[1], wrong, replacements));
    assert(page[0] == originals[0] && page[1] == originals[1]);
    assert(install_ui_slot_pair((uintptr_t)&page[0], (uintptr_t)&page[1], originals, replacements));
    assert(page[0] == replacements[0] && page[1] == replacements[1]);
    assert(has_permissions((uintptr_t)page, "r--p"));
    assert(!munmap(page, PVQ_PAGE));
    /* The synthetic executable is not the pinned game and must refuse. */
    assert(!install_ui_callbacks());
    return 0;
}

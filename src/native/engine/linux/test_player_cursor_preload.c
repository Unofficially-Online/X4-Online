#define PLAYER_VIEW_QUERY_TEST
#include "player_view_query_preload.c"
#include "player_cursor_preload_impl.h"
#include <assert.h>
#include <pthread.h>
#include <sys/wait.h>

struct cursor_test_bases { void *text, *data, *function; };
extern void *_Unwind_Find_FDE(void *, struct cursor_test_bases *);
static int accept_controller(uintptr_t controller) { (void)controller; return 1; }
static void *foreign_call(void *argument) {
    player_cursor_native_dispatch((uintptr_t)argument);
    return NULL;
}
int main(void) {
    unsigned char patch[7];
    assert(cursor_make_patch(CURSOR_SITE, 0x20000, patch));
    int32_t displacement;
    memcpy(&displacement, patch + 1, sizeof displacement);
    assert((intptr_t)CURSOR_SITE + 5 + displacement == 0x20000);
    assert(patch[0] == 0xe9 && patch[5] == 0x90 && patch[6] == 0x90);
    assert(!cursor_make_patch(CURSOR_SITE, UINTPTR_MAX, patch));
    unsigned char *page = mmap(NULL, PVQ_PAGE, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(page != MAP_FAILED);
    emit_entry_stub(page, (uintptr_t)player_cursor_native_bridge);
    assert(!mprotect(page, PVQ_PAGE, PROT_READ | PROT_EXEC));
    assert(cursor_register_stub_frame((uintptr_t)page, cursor_stub_frame));
    struct cursor_test_bases bases = {0};
    assert(_Unwind_Find_FDE(page, &bases) == cursor_stub_frame + 32 && bases.function == page);
    memset(&bases, 0, sizeof bases);
    assert(_Unwind_Find_FDE((void *)player_cursor_native_bridge, &bases) &&
           bases.function == (void *)player_cursor_native_bridge);
    /* Prior foreign calls, including those before bind, can still be in stock
     * Update after leaving the hook. Reject either matching target. */
    pthread_t other;
    assert(!pthread_create(&other, NULL, foreign_call, (void *)0x1000));
    assert(!pthread_join(other, NULL));
    atomic_store(&cursor_installed, 1);
    assert(x4mp_cursor_bind(current_thread(), accept_controller));
    assert(!x4mp_cursor_targets(0x1000, 0x2000));
    assert(!x4mp_cursor_targets(0x2000, 0x1000));
    cursor_history_overflow = 1;
    assert(!x4mp_cursor_targets(0x2000, 0x3000));
    cursor_history_overflow = 0;
    atomic_store(&cursor_active, 0);
    cursor_history_count = 0;
    assert(x4mp_cursor_bind(current_thread(), accept_controller));
    assert(x4mp_cursor_targets(0x1000, 0x2000));
    assert(!x4mp_cursor_targets(0x1000, 0x2000));
    assert(!pthread_create(&other, NULL, foreign_call, (void *)0x3000));
    assert(!pthread_join(other, NULL));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        assert(!pthread_create(&other, NULL, foreign_call, (void *)0x2000));
        (void)pthread_join(other, NULL);
        _exit(1);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 223);
    atomic_store(&cursor_active, 0);
    atomic_store(&cursor_installed, 0);
    atomic_store(&cursor_targets_ready, 0);
    /* Optionally reconstruct the real pinned function in private synthetic
     * memory, without loading or starting X4, and exercise the actual install. */
    const char *image = getenv("X4_CURSOR_TEST_ELF");
    if (image) {
        int fd = open(image, O_RDONLY | O_CLOEXEC);
        assert(fd >= 0);
        unsigned char *code = mmap((void *)0x166d000, 0x4000, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        assert(code != MAP_FAILED);
        assert(pread(fd, code + 0x20, CURSOR_UPDATE_LENGTH, CURSOR_UPDATE_ENTRY - 0x400000) == CURSOR_UPDATE_LENGTH);
        close(fd);
        assert(!mprotect(code, 0x4000, PROT_READ | PROT_EXEC));
        assert(cursor_install_before_main());
        assert(!memcmp((void *)CURSOR_SITE, cursor_patch, sizeof cursor_patch));
        assert(!cursor_install_before_main());
        player_cursor_preload_info installed;
        assert(x4mp_cursor_info(&installed) && installed.site == CURSOR_SITE && installed.stub == (uintptr_t)cursor_stub);
    }
    /* Registered frame pages remain resident while the registry refers to them. */
    return 0;
}

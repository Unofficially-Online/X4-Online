#define PLAYER_VIEW_QUERY_TEST
#include "player_view_query_preload.c"
#include <assert.h>
#include <execinfo.h>
#include <pthread.h>

struct dwarf_eh_bases { void *text, *data, *function; };
extern const void *_Unwind_Find_FDE(void *, struct dwarf_eh_bases *);
static unsigned calls;
static uint64_t value;
static uint64_t fake_query(void) {
    void *frames[16];
    assert(backtrace(frames, 16) >= 3);
    ++calls;
    errno = EDOM;
    return value;
}
static void *foreign_call(void *address) {
    uint64_t (*query)(void) = address;
    assert(query() == value);
    return NULL;
}
int main(void) {
    unsigned char *page = mmap(NULL, PVQ_PAGE, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(page != MAP_FAILED);
    emit_entry_stub(page, (uintptr_t)player_view_query_dispatch);
    uint64_t initial = UINT64_C(0x48c3909090909090), patched, expected = initial;
    memcpy(page + 64, &initial, 8);
    assert(make_entry_patch((uintptr_t)page + 64, (uintptr_t)page, initial, &patched));
    assert(((unsigned char *)&patched)[7] == 0x48);
    assert(__atomic_compare_exchange_n((uint64_t *)(page + 64), &expected, patched, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
    assert(!mprotect(page, PVQ_PAGE, PROT_READ | PROT_EXEC));
    assert(register_entry_frame((uintptr_t)page));
    struct dwarf_eh_bases bases = {0};
    assert(_Unwind_Find_FDE(page, &bases));
    assert(bases.function == page);
    assert(_Unwind_Find_FDE((void *)player_view_query_original, &bases));
    information.installed = 1;
    original_query = fake_query;
    uint64_t (*query)(void) = (void *)(page + 64);
    uint32_t owner = current_thread();
    value = 11;
    assert(query() == 11 && calls == 1);
    assert(atomic_load(&other_calls) == 0 && atomic_load(&ui_calls) == 0 && atomic_load(&worker_calls) == 0);
    assert(x4mp_view_query_configure(owner, owner));
    assert(!x4mp_view_query_configure(owner, owner));
    assert(x4mp_view_query_phase(7, 0));
    value = 0;
    errno = 0;
    assert(query() == 0 && calls == 2 && errno == EDOM);
    value = 42;
    assert(query() == 42 && calls == 3 && errno == EDOM);
    assert(x4mp_view_query_phase(8, 1));
    assert(query() == 42 && calls == 4);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, foreign_call, page + 64));
    assert(!pthread_join(thread, NULL));
    for (uint64_t tick = 100; tick < 170; ++tick) {
        assert(x4mp_view_query_phase(tick, 0));
        assert(query() == 42);
    }
    for (uint64_t index = 0; index < 70; ++index) {
        value = 1000 + index;
        assert(query() == value);
    }
    value = 42;
    player_view_query_snapshot result, frozen;
    assert(x4mp_view_query_finish(&result));
    assert(result.ui_calls == 143 && result.worker_calls == 0 && result.other_calls == 1);
    assert(result.probe_calls == 1 && result.count == 64 && result.overflow == 9);
    assert(result.rows[0].result == 0 && result.rows[1].result == 42);
    assert(result.rows[0].first_tick == 7 && result.rows[0].last_tick == 7);
    assert(result.rows[1].first_tick == 7 && result.rows[1].last_tick == 7);
    assert(result.rows[2].first_tick == 100 && result.rows[2].last_tick == 169 && result.rows[2].count == 70);
    assert(!x4mp_view_query_phase(9, 0));
    assert(query() == 42 && calls == 146);
    assert(x4mp_view_query_finish(&frozen));
    assert(!memcmp(&result, &frozen, sizeof result));
    return 0;
}

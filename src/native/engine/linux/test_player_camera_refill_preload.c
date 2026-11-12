#define PLAYER_VIEW_QUERY_TEST
#include "player_view_query_preload.c"
#include <assert.h>

static unsigned original_calls, enters, leaves;
static uintptr_t original(void *camera, void *output) {
    assert(camera == (void *)0x12340000 && output == (void *)0x12350000);
    ++original_calls;
    errno = ERANGE;
    return UINT64_C(0x123456789abcdef0);
}
static int enter(uintptr_t camera, uintptr_t output) {
    assert(camera == 0x12340000 && output == 0x12350000 && camera_refill_depth == 1);
    assert(original_calls == enters);
    ++enters;
    return 1;
}
static int leave(uintptr_t camera, uintptr_t output) {
    assert(camera == 0x12340000 && output == 0x12350000 && camera_refill_depth == 1);
    assert(original_calls == leaves + 1);
    ++leaves;
    return 1;
}
int main(void) {
    /* Execute the genuine relocated prologue and its continuation at the pinned VA. */
    unsigned char *stock = mmap((void *)0x192a000, PVQ_PAGE, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(stock != MAP_FAILED);
    const unsigned char continuation[] = {
        0x0f, 0x11, 0x06, /* movups xmm0,(rsi): displaced pxor must have executed */
        0x48, 0xb8, 0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
        0x41, 0x5f, 0xc3
    };
    memcpy(stock + 0x316, continuation, sizeof continuation);
    assert(!mprotect(stock, PVQ_PAGE, PROT_READ | PROT_EXEC));
    unsigned char output[16];
    memset(output, 0xff, sizeof output);
    assert(camera_refill_trampoline(NULL, output) == UINT64_C(0x123456789abcdef0));
    for (unsigned index = 0; index < sizeof output; ++index) assert(output[index] == 0);
    assert(!munmap(stock, PVQ_PAGE));
    assert(player_ui_escape_guard_prepare());
    camera_refill_original = original;
    camera_refill_owner = current_thread();
    camera_refill_target = 0x12340000;
    camera_refill_enter = enter;
    camera_refill_leave = leave;
    atomic_store(&camera_refill_bound, 1);
    errno = EDOM;
    assert(camera_refill_dispatch_from((void *)0x12340000, (void *)0x12350000, 0xfeb974) == UINT64_C(0x123456789abcdef0));
    assert(errno == ERANGE && original_calls == 1 && enters == 1 && leaves == 1 && camera_refill_completed == 1);
    assert(x4mp_camera_refill_disarm());
    assert(!x4mp_camera_refill_disarm());
    assert(camera_refill_dispatch_from((void *)0x12340000, (void *)0x12350000, 0xfeb974) == UINT64_C(0x123456789abcdef0));
    assert(original_calls == 2 && enters == 1 && leaves == 1 && camera_refill_completed == 1);
    return 0;
}

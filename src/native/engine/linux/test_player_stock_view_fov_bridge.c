#include <assert.h>
#include <execinfo.h>
#include <stdint.h>
#include <string.h>

static uintptr_t expected_controller;
static unsigned calls;
__attribute__((visibility("hidden"))) int player_stock_view_mode_dispatch(uintptr_t controller, int original, unsigned site) {
    assert(controller == expected_controller && original == 7 && site < 2);
    void *frames[16];
    assert(backtrace(frames, 16) >= 4);
    __asm__ volatile("pxor %%xmm0,%%xmm0; pxor %%xmm15,%%xmm15" ::: "xmm0", "xmm15");
    ++calls;
    return 11;
}
__attribute__((visibility("hidden"))) float player_stock_view_fov_dispatch(void *camera, uintptr_t controller) {
    (void)camera; (void)controller; return 0.0f;
}
__attribute__((visibility("hidden"))) int player_stock_view_ship_dispatch(uintptr_t ship, int original) {
    return player_stock_view_mode_dispatch(ship, original, 0);
}
extern int test_ship_mode(void *camera, uintptr_t ship);
extern int test_mode_edx(void *camera, uintptr_t controller);
extern int test_mode_eax(void *camera, uintptr_t controller);
int main(void) {
    unsigned char camera[0x884] = {0};
    int native_mode = 7;
    memcpy(camera + 0x880, &native_mode, sizeof native_mode);
    expected_controller = 0x12345678;
    assert(test_mode_edx(camera, expected_controller) == 11);
    assert(test_mode_eax(camera, expected_controller) == 11);
    assert(test_ship_mode(camera, expected_controller) == 11);
    assert(calls == 3);
    return 0;
}

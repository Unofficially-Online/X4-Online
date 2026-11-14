#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "player_stock_view_state.h"

static int sample(void *context, unsigned char *bits, float *fov, uint64_t *target) {
    unsigned *calls = context;
    ++*calls;
    *bits = 3;
    *fov = 1.25f;
    *target = 1234;
    return 1;
}

static int bad_sample(void *context, unsigned char *bits, float *fov, uint64_t *target) {
    (void)context;
    *bits = 1;
    *fov = NAN;
    *target = 9;
    return 1;
}

static unsigned native_calls;
static float native_fov(void *camera) {
    assert(camera == (void *)(uintptr_t)7);
    ++native_calls;
    return 2.0f;
}

int main(void) {
    unsigned calls = 0;
    stock_input_record bad = {.capture16 = 38};
    stock_input_record unchanged = bad;
    assert(!player_stock_view_capture(&bad, bad_sample, NULL));
    assert(!memcmp(&bad, &unchanged, sizeof bad));
    stock_input_record capture = {.capture16 = 38};
    assert(player_stock_view_capture(&capture, sample, &calls));
    assert(calls == 1 && capture.flags == STOCK_INPUT_VIEW && capture.view == 3 &&
           capture.fov == 1.25f && capture.target == 1234);
    assert(!player_stock_view_capture(&capture, sample, &calls) && calls == 1);
    const uint64_t saved[2] = {100, 200};
    stock_input_record p1 = {.flags = STOCK_INPUT_VIEW, .capture16 = 38,
                             .view = 3, .fov = 1.5f, .target = 321};
    stock_input_record p2 = {.flags = STOCK_INPUT_VIEW, .capture16 = 38,
                             .view = 0, .fov = 0.75f, .target = 654};
    unsigned char a[STOCK_INPUT_MAX_BYTES], b[STOCK_INPUT_MAX_BYTES];
    unsigned na = stock_input_encode(&p1, a, sizeof a);
    unsigned nb = stock_input_encode(&p2, b, sizeof b);
    player_stock_view_state state = {0}, previous;
    player_stock_view view;
    assert(player_stock_view_stage(&state, 40, saved, a, na, b, nb));
    assert(player_stock_view_read(&state, 40, 1, 100, &view));
    assert(view.present && view.cockpit && view.floating && view.target == 321);
    assert(player_stock_view_fov_factor(&view) == 1.5f);
    assert(player_stock_view_read(&state, 40, 2, 200, &view));
    assert(!view.cockpit && view.target == 654 && player_stock_view_fov_factor(&view) == 1.0f);
    assert(!player_stock_view_read(&state, 40, 1, 200, &view));
    assert(!player_stock_view_read(&state, 39, 1, 100, &view));
    const uintptr_t controllers[2] = {0x10000, 0x20000};
    int mode = -1;
    unsigned slot = 99;
    float fov = 0;
    assert(player_stock_view_controller_mode(&state, 40, controllers, 0x10000, 3, &mode, &slot));
    assert(mode == 0 && slot == 1);
    assert(player_stock_view_controller_mode(&state, 40, controllers, 0x20000, 0, &mode, &slot));
    assert(mode == 1 && slot == 2);
    assert(player_stock_view_controller_mode(&state, 40, controllers, 0x30000, 7, &mode, &slot));
    assert(mode == 7 && slot == 0);
    assert(!player_stock_view_controller_mode(&state, 39, controllers, 0x10000, 0, &mode, &slot));
    assert(player_stock_view_controller_fov(&state, 40, controllers, 0x10000,
                                            (void *)(uintptr_t)7, native_fov, &fov));
    assert(native_calls == 1 && fov == 1.5f);
    assert(player_stock_view_controller_fov(&state, 40, controllers, 0x30000,
                                            (void *)(uintptr_t)7, native_fov, &fov));
    assert(native_calls == 2 && fov == 2.0f);
    assert(!player_stock_view_controller_fov(&state, 39, controllers, 0x10000,
                                             (void *)(uintptr_t)7, native_fov, &fov));
    assert(native_calls == 2);
    previous = state;
    p1.capture16 = 39;
    na = stock_input_encode(&p1, a, sizeof a);
    b[2] = 39;
    b[29] = 4;
    assert(!player_stock_view_stage(&state, 41, saved, a, na, b, nb));
    assert(!memcmp(&state, &previous, sizeof state));
    p2 = (stock_input_record){.capture16 = 39};
    na = stock_input_encode(&p1, a, sizeof a);
    nb = stock_input_encode(&p2, b, sizeof b);
    assert(player_stock_view_stage(&state, 41, saved, a, na, b, nb));
    assert(player_stock_view_read(&state, 41, 2, 200, &view));
    assert(!view.present && view.cockpit && !view.floating && view.fov == 1.0f && !view.target);
    previous = state;
    const uint64_t changed[2] = {100, 201};
    assert(!player_stock_view_stage(&state, 42, changed, a, na, b, nb));
    assert(!player_stock_view_stage(&state, 43, saved, a, na, b, nb));
    assert(!memcmp(&state, &previous, sizeof state));
    return 0;
}

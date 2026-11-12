#include "player_ui_seat_transform.h"
#include <assert.h>
#include <stdint.h>

static void assert_close(float actual, float expected) {
    assert(fabs((double)actual - expected) < 0.0001);
}

int main(void) {
    float canonical[16] = {2, 3, 4, 11,  0, 1, 0, 12,  -1, 0, 0, 13,  0, 0, 1, 14};
    float guest[16] = {-3, 5, 7, 21,  0, -1, 0, 22,  1, 0, 0, 23,  0, 0, 1, 24};
    float delta[16], output[16];
    assert(player_ui_seat_transform_delta(canonical, guest, delta));
    assert(player_ui_seat_transform_apply(canonical, delta, output));
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned column = 0; column < 3; ++column) {
            assert_close(output[row * 4 + column], guest[row * 4 + column]);
        }
        assert(memcmp(&output[row * 4 + 3], &canonical[row * 4 + 3], sizeof(float)) == 0);
    }
    float offset_pose[16];
    memcpy(offset_pose, canonical, sizeof offset_pose);
    offset_pose[0] += 1;
    assert(player_ui_seat_transform_apply(offset_pose, delta, output));
    assert_close(output[0], -4);
    assert_close(output[1], 5);
    assert_close(output[2], 7);
    assert(player_ui_seat_transform_delta(canonical, canonical, delta));
    assert(player_ui_seat_transform_apply(canonical, delta, output));
    for (unsigned i = 0; i < 16; ++i) assert_close(output[i], canonical[i]);

    /* Rounded non-axis-aligned bases must not turn identical seats into a
     * small, unintended correction. Metadata is not part of seat geometry. */
    float rounded_seat[16] = {2, 3, 4, 11, 0.6f, 0.8f, 0, 12,
                             -0.8f, 0.6f, 0, 13, 0, 0, 1, 14};
    float same_geometry[16];
    memcpy(same_geometry, rounded_seat, sizeof same_geometry);
    same_geometry[3] = 99;
    assert(player_ui_seat_transform_delta(rounded_seat, same_geometry, delta));
    for (unsigned i = 0; i < 16; ++i) {
        assert(delta[i] == (i == 4 || i == 9 || i == 14 ? 1.0f : 0.0f));
    }
    assert(player_ui_seat_transform_apply(rounded_seat, delta, output));
    assert(memcmp(output, rounded_seat, sizeof output) == 0);

    uint32_t negative_zero = UINT32_C(0x80000000);
    memcpy(&rounded_seat[6], &negative_zero, sizeof negative_zero);
    assert(player_ui_seat_transform_apply(rounded_seat, delta, output));
    assert(memcmp(output, rounded_seat, sizeof output) == 0);
    memcpy(&canonical[3], &negative_zero, sizeof negative_zero);
    assert(player_ui_seat_transform_apply(canonical, delta, output));
    assert(memcmp(&output[3], &negative_zero, sizeof negative_zero) == 0);

    float untouched[16];
    for (unsigned i = 0; i < 16; ++i) untouched[i] = 77;
    guest[4] = NAN;
    assert(!player_ui_seat_transform_delta(canonical, guest, untouched));
    for (unsigned i = 0; i < 16; ++i) assert(untouched[i] == 77);
    guest[4] = 0; guest[5] = -2;
    assert(!player_ui_seat_transform_delta(canonical, guest, untouched));
    float identity[16] = {FLT_MAX, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    float large_delta[16] = {FLT_MAX, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    float negative_large[16];
    memcpy(negative_large, identity, sizeof negative_large);
    negative_large[0] = -FLT_MAX;
    assert(!player_ui_seat_transform_delta(negative_large, identity, untouched));
    assert(!player_ui_seat_transform_apply(identity, large_delta, untouched));
    for (unsigned i = 0; i < 16; ++i) assert(untouched[i] == 77);
    return 0;
}

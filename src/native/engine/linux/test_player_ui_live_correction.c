#include "player_ui_live_correction.h"
#include <assert.h>
#include <math.h>

int main(void) {
    _Alignas(16) float raw[16] = {0};
    _Alignas(16) float delta[16] = {0};
    _Alignas(16) float result[16];
    raw[4] = raw[9] = raw[14] = 1;
    raw[0] = 2;
    raw[3] = 7;
    delta[4] = delta[9] = delta[14] = 1;
    delta[0] = 3;
    uintptr_t low = (uintptr_t)raw;
    uintptr_t high = low + sizeof raw;
    assert(player_ui_live_correction(0x1314317, low, low, high, 1, 1.25, delta,
                                     (unsigned char *)raw, (unsigned char *)result));
    assert(result[0] == 5 && result[3] == 7);
    assert(!player_ui_live_correction(0x1314407, low, low, high, 1, 1.25, delta,
                                      (unsigned char *)raw, (unsigned char *)result));
    assert(!player_ui_live_correction(0x1314317, low, low, high, 0, 1.25, delta,
                                      (unsigned char *)raw, (unsigned char *)result));
    assert(!player_ui_live_correction(0x1314317, low, low, high, 1, NAN, delta,
                                      (unsigned char *)raw, (unsigned char *)result));
    assert(!player_ui_live_correction(0x1314317, low, low, high, 1, -1, delta,
                                      (unsigned char *)raw, (unsigned char *)result));
    assert(!player_ui_live_correction(0x1314317, low + 1, low, high, 1, 1, delta,
                                      (unsigned char *)raw, (unsigned char *)result));
    assert(!player_ui_live_correction(0x1314317, low, low, high - 1, 1, 1, delta,
                                      (unsigned char *)raw, (unsigned char *)result));
    raw[4] = 2;
    assert(!player_ui_live_correction(0x1314317, low, low, high, 1, 1, delta,
                                      (unsigned char *)raw, (unsigned char *)result));
    return 0;
}

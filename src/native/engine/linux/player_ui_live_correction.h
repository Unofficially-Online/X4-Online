#ifndef X4_PLAYER_UI_LIVE_CORRECTION_H
#define X4_PLAYER_UI_LIVE_CORRECTION_H
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "player_ui_seat_transform.h"

/* The first bounded experiment accepts only the observed stack-scratch
 * branch. The caller owns validation of thread, scope and target. */
static inline int player_ui_live_correction(uintptr_t caller, uintptr_t output,
                                             uintptr_t stack_low, uintptr_t stack_high,
                                             unsigned mode, double time, const float delta[16],
                                             const unsigned char raw[64], unsigned char corrected[64]) {
    if (caller != UINT64_C(0x1314317) || mode != 1 || !isfinite(time) || time < 0 ||
        !output || (output & 15) || stack_low >= stack_high || output < stack_low ||
        output > stack_high || stack_high - output < 64 || !delta || !raw || !corrected) return 0;
    float source[16], result[16];
    memcpy(source, raw, sizeof source);
    if (!player_ui_seat_transform_apply(source, delta, result)) return 0;
    memcpy(corrected, result, sizeof result);
    return 1;
}
#endif

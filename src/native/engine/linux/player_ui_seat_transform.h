#ifndef X4_PLAYER_UI_SEAT_TRANSFORM_H
#define X4_PLAYER_UI_SEAT_TRANSFORM_H

#include <float.h>
#include <math.h>
#include <string.h>

/* PosRot has translation in row 0 and basis in rows 1..3. The fourth
 * word of each row is metadata and is preserved from the canonical pose. */
static inline int player_ui_seat_transform_rigid(const float value[16]) {
    for (unsigned i = 0; i < 16; ++i) {
        if (!isfinite(value[i])) return 0;
    }
    for (unsigned row = 1; row < 4; ++row) {
        for (unsigned other = 1; other <= row; ++other) {
            double dot = 0;
            for (unsigned column = 0; column < 3; ++column) {
                dot += (double)value[4 * row + column] * value[4 * other + column];
            }
            if (fabs(dot - (row == other ? 1.0 : 0.0)) > 0.001) return 0;
        }
    }
    double determinant = (double)value[4] * ((double)value[9] * value[14] - (double)value[10] * value[13]) -
                         (double)value[5] * ((double)value[8] * value[14] - (double)value[10] * value[12]) +
                         (double)value[6] * ((double)value[8] * value[13] - (double)value[9] * value[12]);
    return fabs(determinant - 1.0) <= 0.001;
}

static inline int player_ui_seat_transform_float(double value, float *result) {
    if (!isfinite(value) || value > FLT_MAX || value < -FLT_MAX) return 0;
    *result = (float)value;
    return isfinite(*result);
}

/* Calculate inverse(canonical seat) * guest seat for row-vector transforms. */
static inline int player_ui_seat_transform_delta(const float canonical[16],
                                                  const float guest[16], float delta[16]) {
    if (!canonical || !guest || !delta || !player_ui_seat_transform_rigid(canonical) ||
        !player_ui_seat_transform_rigid(guest)) return 0;
    /* Identical seats need no correction. Avoid introducing roundoff from
     * multiplying a rounded rotation by its transpose in this common case. */
    int same_seat = 1;
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned column = 0; column < 3; ++column) {
            if (canonical[4 * row + column] != guest[4 * row + column]) {
                same_seat = 0;
            }
        }
    }
    if (same_seat) {
        float identity[16] = {0};
        identity[4] = identity[9] = identity[14] = 1.0f;
        memcpy(delta, identity, sizeof identity);
        return 1;
    }
    double rotation[3][3];
    double inverse_translation[3];
    float result[16] = {0};
    for (unsigned column = 0; column < 3; ++column) {
        inverse_translation[column] = 0;
        for (unsigned k = 0; k < 3; ++k) {
            inverse_translation[column] -= (double)canonical[k] * canonical[4 * (column + 1) + k];
        }
    }
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned column = 0; column < 3; ++column) {
            rotation[row][column] = 0;
            for (unsigned k = 0; k < 3; ++k) {
                rotation[row][column] += (double)canonical[4 * (k + 1) + row] * guest[4 * (k + 1) + column];
            }
            if (!player_ui_seat_transform_float(rotation[row][column], &result[4 * (row + 1) + column])) return 0;
        }
    }
    for (unsigned column = 0; column < 3; ++column) {
        double translation = guest[column];
        for (unsigned k = 0; k < 3; ++k) translation += inverse_translation[k] * guest[4 * (k + 1) + column];
        if (!player_ui_seat_transform_float(translation, &result[column])) return 0;
    }
    memcpy(delta, result, sizeof result);
    return 1;
}

/* Apply the delta without changing the original's four metadata words. */
static inline int player_ui_seat_transform_apply(const float canonical_pose[16],
                                                  const float delta[16], float output[16]) {
    if (!canonical_pose || !delta || !output || !player_ui_seat_transform_rigid(canonical_pose) ||
        !player_ui_seat_transform_rigid(delta)) return 0;
    float result[16];
    memcpy(result, canonical_pose, sizeof result);
    int identity_delta = 1;
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned column = 0; column < 3; ++column) {
            float expected = row == column + 1 ? 1.0f : 0.0f;
            if (delta[4 * row + column] != expected) identity_delta = 0;
        }
    }
    if (identity_delta) {
        memcpy(output, result, sizeof result);
        return 1;
    }
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned column = 0; column < 3; ++column) {
            double value = row == 0 ? delta[column] : 0;
            for (unsigned k = 0; k < 3; ++k) {
                value += (double)canonical_pose[4 * row + k] * delta[4 * (k + 1) + column];
            }
            if (!player_ui_seat_transform_float(value, &result[4 * row + column])) return 0;
        }
    }
    memcpy(output, result, sizeof result);
    return 1;
}
#endif

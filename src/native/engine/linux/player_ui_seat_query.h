#ifndef X4_PLAYER_UI_SEAT_QUERY_H
#define X4_PLAYER_UI_SEAT_QUERY_H
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    void *context;
    int (*reference)(void *context, int room, int acquire);
    int (*query)(void *context, unsigned index);
} player_ui_seat_query_ops;

/* A failed operation is terminal to the caller; do not retry or invent a
 * reference rollback after a native call whose effect is uncertain. */
static inline int player_ui_seat_query_run(const player_ui_seat_query_ops *ops) {
    if (!ops || !ops->reference || !ops->query) return 0;
    return ops->reference(ops->context, 0, 1) && ops->reference(ops->context, 1, 1) &&
           ops->query(ops->context, 0) && ops->query(ops->context, 1) &&
           ops->reference(ops->context, 1, 0) && ops->reference(ops->context, 0, 0);
}

/* The stock helper returns four float32 rows. This classifies its returned
 * bytes, without treating a rigid transform as camera or readiness proof. */
typedef struct {
    uint32_t raw[16], normalized[16];
    int finite, rigid;
} player_ui_seat_query_result;
static inline void player_ui_seat_query_classify(const float *output, player_ui_seat_query_result *result) {
    memcpy(result->raw, output, sizeof result->raw);
    memcpy(result->normalized, result->raw, sizeof result->raw);
    result->finite = 1;
    for (unsigned i = 0; i < 16; ++i) {
        if ((result->raw[i] & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000)) result->finite = 0;
        if (!(result->raw[i] & UINT32_C(0x7fffffff))) result->normalized[i] = 0;
    }
    result->rigid = result->finite;
    if (!result->finite) return;
    for (unsigned row = 1; row < 4; ++row) {
        for (unsigned other = 1; other <= row; ++other) {
            double dot = 0;
            for (unsigned column = 0; column < 3; ++column)
                dot += (double)output[4 * row + column] * (double)output[4 * other + column];
            if (fabs(dot - (row == other ? 1.0 : 0.0)) > 0.001) result->rigid = 0;
        }
    }
    double determinant = (double)output[4] * ((double)output[9] * output[14] - (double)output[10] * output[13]) -
                         (double)output[5] * ((double)output[8] * output[14] - (double)output[10] * output[12]) +
                         (double)output[6] * ((double)output[8] * output[13] - (double)output[9] * output[12]);
    if (fabs(determinant - 1.0) > 0.001) result->rigid = 0;
}
#endif

#ifndef X4_PLAYER_CAMERA_PROBE_H
#define X4_PLAYER_CAMERA_PROBE_H

/* Engine-free, default-unused camera diagnostic. Identity and pin callbacks are
 * supplied by the native adapter. The observation is only a target-ID sample:
 * it does not prove rendered output, a cockpit, or exclusion of other threads.
 * A native Possible call can reach unresolved virtual methods; a Set call has
 * known context/reference/client effects. Neither is enabled by this helper. */
#include <stdint.h>
#include <stddef.h>

#define PLAYER_CAMERA_IMAGE_SHA "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20"
#define PLAYER_CAMERA_TARGET_SAVED UINT64_C(0x175310)

typedef struct {
    uintptr_t target;
    uint64_t runtime;
    uint64_t saved;
} player_camera_target;

typedef struct {
    void *context;
    int (*pins)(void *context);
    int (*admit)(void *context);
    int (*held_target)(void *context, player_camera_target *target);
    uint64_t (*current)(void *context);
} player_camera_probe_operations;

typedef struct {
    player_camera_target target;
    uint64_t current;
    unsigned matches;
} player_camera_observation;

static int player_camera_same_target(const player_camera_target *left, const player_camera_target *right) {
    return left->target == right->target && left->runtime == right->runtime && left->saved == right->saved;
}

/* The caller must establish that no competing mutation/lifetime change can
 * occur during the sample. Double checking detects changes, not ABA or races. */
static inline int player_camera_observe(const player_camera_probe_operations *operations,
                                 player_camera_observation *observation) {
    if (!operations || !observation || !operations->pins || !operations->admit ||
        !operations->held_target || !operations->current ||
        !operations->pins(operations->context) || !operations->admit(operations->context)) {
        return 0;
    }
    player_camera_target before = {0};
    player_camera_target after = {0};
    if (!operations->held_target(operations->context, &before) || !before.target || !before.runtime ||
        before.saved != PLAYER_CAMERA_TARGET_SAVED) {
        return 0;
    }
    uint64_t current = operations->current(operations->context);
    if (!operations->held_target(operations->context, &after) || !player_camera_same_target(&before, &after) ||
        !operations->admit(operations->context)) {
        return 0;
    }
    *observation = (player_camera_observation){before, current, current == before.runtime};
    return 1;
}
#endif

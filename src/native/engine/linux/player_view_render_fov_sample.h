#ifndef X4_PLAYER_VIEW_RENDER_FOV_SAMPLE_H
#define X4_PLAYER_VIEW_RENDER_FOV_SAMPLE_H
#include <math.h>
#include <pthread.h>
#include <stdint.h>
/* The caller authenticates local presentation, camera, controller and epoch
 * before publish. Mutex protects cross-thread publication and consumption. */
typedef struct {
    pthread_mutex_t mutex;
    uint64_t generation;
    uintptr_t camera, controller;
    float value;
    unsigned initialized;
} player_view_render_fov_sample;
static inline int player_view_render_fov_publish(player_view_render_fov_sample *s,
        uint64_t generation, uintptr_t camera, uintptr_t controller, float value) {
    if (!s || !generation || !camera || !controller || !isfinite(value)) return 0;
    if (pthread_mutex_lock(&s->mutex)) return 0;
    int ok = !s->initialized || generation >= s->generation;
    if (ok) { s->generation = generation; s->camera = camera; s->controller = controller;
              s->value = value; s->initialized = 1; }
    if (pthread_mutex_unlock(&s->mutex)) return 0;
    return ok;
}
static inline int player_view_render_fov_read(player_view_render_fov_sample *s,
        uint64_t generation, uintptr_t camera, uintptr_t controller, float *out) {
    if (!s || !generation || !camera || !controller || !out || pthread_mutex_lock(&s->mutex)) return 0;
    int ok = s->initialized && s->camera == camera && s->controller == controller &&
        s->generation <= generation && generation - s->generation <= 1;
    float value = s->value;
    if (pthread_mutex_unlock(&s->mutex)) return 0;
    if (ok) *out = value;
    return ok;
}
#endif

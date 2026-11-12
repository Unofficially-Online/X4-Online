#ifndef X4_PLAYER_UI_VIEWSTATE_H
#define X4_PLAYER_UI_VIEWSTATE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* These are bounded observations; the camera object is a candidate, not a
 * claim about the camera selected by the renderer. */
enum { PLAYER_UI_VIEWSTATE_PLAN_MAX = 32 };
typedef int (*player_ui_viewstate_read_fn)(void *, uintptr_t, void *, size_t);
typedef struct {
    uint32_t selector;
    unsigned char first_person;
    unsigned char plan[PLAYER_UI_VIEWSTATE_PLAN_MAX];
    size_t plan_length;
    uintptr_t camera, target;
    uint32_t camera_mode;
    uint64_t target_id;
} player_ui_viewstate;

static inline int player_ui_viewstate_read_at(player_ui_viewstate_read_fn read, void *context,
                                               uintptr_t object, size_t offset, void *out, size_t size) {
    const uintptr_t limit = UINT64_C(0x00007fffffffffff);
    if (!read || object < 0x10000 || object > limit || offset > limit - object ||
        size > limit - (object + offset)) return 0;
    return read(context, object + offset, out, size);
}

static inline int player_ui_viewstate_sample(player_ui_viewstate_read_fn read, void *context,
                                              uintptr_t universe, uintptr_t presenter,
                                              uintptr_t controller, player_ui_viewstate *out) {
    if (!out) return 0;
    player_ui_viewstate sample = {0};
    uintptr_t plan = 0, plan_again = 0, camera_again = 0, target_again = 0;
    size_t length = 0, length_again = 0;
    if (!player_ui_viewstate_read_at(read, context, presenter, 0x68, &sample.selector, 4) ||
        !player_ui_viewstate_read_at(read, context, presenter, 0x6c, &sample.first_person, 1) ||
        sample.first_person > 1 ||
        !player_ui_viewstate_read_at(read, context, controller, 0x3e0, &plan, 8) ||
        !player_ui_viewstate_read_at(read, context, controller, 0x3e8, &length, 8) ||
        length > PLAYER_UI_VIEWSTATE_PLAN_MAX ||
        (length && !player_ui_viewstate_read_at(read, context, plan, 0, sample.plan, length)) ||
        !player_ui_viewstate_read_at(read, context, controller, 0x3e0, &plan_again, 8) ||
        !player_ui_viewstate_read_at(read, context, controller, 0x3e8, &length_again, 8) ||
        plan != plan_again || length != length_again ||
        !player_ui_viewstate_read_at(read, context, universe, 0x3e8, &sample.camera, 8)) return 0;
    sample.plan_length = length;
    if (sample.camera &&
        (!player_ui_viewstate_read_at(read, context, sample.camera, 0x880, &sample.camera_mode, 4) ||
         !player_ui_viewstate_read_at(read, context, sample.camera, 0x780, &sample.target, 8) ||
         (sample.target && !player_ui_viewstate_read_at(read, context, sample.target, 8, &sample.target_id, 8)) ||
         !player_ui_viewstate_read_at(read, context, sample.camera, 0x780, &target_again, 8) ||
         sample.target != target_again)) return 0;
    if (!player_ui_viewstate_read_at(read, context, universe, 0x3e8, &camera_again, 8) ||
        sample.camera != camera_again) return 0;
    *out = sample;
    return 1;
}
static inline int player_ui_viewstate_format(char *out, size_t capacity, const char *stage,
                                               uintptr_t manager, uintptr_t presenter,
                                               uintptr_t controller, uintptr_t universe,
                                               const player_ui_viewstate *sample) {
    if (!out || !capacity || !stage || !sample || sample->plan_length > PLAYER_UI_VIEWSTATE_PLAN_MAX ||
        (strcmp(stage, "before") && strcmp(stage, "during") && strcmp(stage, "after"))) return 0;
    char plan_hex[PLAYER_UI_VIEWSTATE_PLAN_MAX * 2 + 1];
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sample->plan_length; ++i) {
        plan_hex[2 * i] = hex[sample->plan[i] >> 4];
        plan_hex[2 * i + 1] = hex[sample->plan[i] & 15];
    }
    plan_hex[2 * sample->plan_length] = 0;
    int length = snprintf(out, capacity, "X4 UI VIEWSTATE v=1 tick=9 stage=%s status=observed "
        "manager=%llx presenter=%llx controller=%llx universe=%llx selector=%u first=%u plan_len=%zu plan=%s "
        "camera_candidate=%llx camera_mode=%u external_target=%llx target_id=%llu\n",
        stage, (unsigned long long)manager, (unsigned long long)presenter, (unsigned long long)controller,
        (unsigned long long)universe, sample->selector, (unsigned)sample->first_person, sample->plan_length,
        sample->plan_length ? plan_hex : "-", (unsigned long long)sample->camera, sample->camera_mode,
        (unsigned long long)sample->target, (unsigned long long)sample->target_id);
    return length > 0 && (size_t)length < capacity ? length : 0;
}
#endif

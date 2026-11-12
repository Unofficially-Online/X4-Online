#ifndef X4_PLAYER_UI_SEAT_POSE_H
#define X4_PLAYER_UI_SEAT_POSE_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Linux transform walker 0x18a2900 reads bank+0x28 as a published local
 * transform; this does not prove renderer use. Reads are compared twice. */
typedef int (*player_ui_seat_read)(void *, uintptr_t, void *, size_t);
typedef struct {
    uintptr_t actor, parent, controller, vtable, pose;
    uint32_t position, words[16], raw_words[16];
    int finite, rigid;
} player_ui_seat_pose;

static inline int player_ui_seat_read_at(player_ui_seat_read read, void *context,
                                         uintptr_t object, size_t offset, void *out, size_t size) {
    const uintptr_t limit = UINT64_C(0x00007fffffffffff);
    return read && object >= 0x10000 && object <= limit && offset <= limit - object &&
        size <= limit - (object + offset) && read(context, object + offset, out, size);
}

static inline int player_ui_seat_sample(player_ui_seat_read read, void *context, uintptr_t actor,
                                        unsigned bank, player_ui_seat_pose *out) {
    if (!out || bank > 1) return 0;
    player_ui_seat_pose sample = {0};
    uintptr_t parent2 = 0, controller2 = 0, vtable2 = 0, pose2 = 0, mapped = 0;
    uint32_t position2 = 0, copy[16];
    const size_t pose_offset = 0xe0 + bank * 0x80 + 0x28;
    sample.actor = actor;
    if (!player_ui_seat_read_at(read, context, actor, 0x70, &sample.parent, 8) ||
        !player_ui_seat_read_at(read, context, actor, 0x200, &sample.controller, 8) ||
        !player_ui_seat_read_at(read, context, actor, 0x6aa8, &sample.position, 4) ||
        !player_ui_seat_read_at(read, context, actor, pose_offset, &sample.pose, 8)) return 0;
    if (sample.controller &&
        (!player_ui_seat_read_at(read, context, sample.controller, 0, &sample.vtable, 8) ||
         !player_ui_seat_read_at(read, context, sample.vtable, 0, &mapped, 8))) return 0;
    if (sample.pose && ((sample.pose & 15) ||
        !player_ui_seat_read_at(read, context, sample.pose, 0, sample.words, 64))) return 0;
    if (!player_ui_seat_read_at(read, context, actor, 0x70, &parent2, 8) || parent2 != sample.parent ||
        !player_ui_seat_read_at(read, context, actor, 0x200, &controller2, 8) || controller2 != sample.controller ||
        !player_ui_seat_read_at(read, context, actor, 0x6aa8, &position2, 4) || position2 != sample.position ||
        !player_ui_seat_read_at(read, context, actor, pose_offset, &pose2, 8) || pose2 != sample.pose ||
        (sample.controller && (!player_ui_seat_read_at(read, context, sample.controller, 0, &vtable2, 8) || vtable2 != sample.vtable)) ||
        (sample.pose && (!player_ui_seat_read_at(read, context, sample.pose, 0, copy, 64) || memcmp(copy, sample.words, 64)))) return 0;
    if (sample.pose) {
        memcpy(sample.raw_words, sample.words, sizeof sample.raw_words);
        float values[16];
        memcpy(values, sample.words, sizeof values);
        sample.finite = 1;
        for (unsigned i = 0; i < 16; ++i) {
            if ((sample.words[i] & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000)) sample.finite = 0;
            if ((sample.words[i] & UINT32_C(0x7fffffff)) == 0) sample.words[i] = 0;
        }
        sample.rigid = sample.finite;
        if (sample.rigid) {
            for (unsigned row = 1; row < 4; ++row) {
                for (unsigned other = 1; other <= row; ++other) {
                    double dot = 0;
                    for (unsigned col = 0; col < 3; ++col) dot += (double)values[4 * row + col] * (double)values[4 * other + col];
                    if (fabs(dot - (row == other ? 1.0 : 0.0)) > 0.001) sample.rigid = 0;
                }
            }
            double determinant = (double)values[4] * ((double)values[9] * values[14] - (double)values[10] * values[13]) -
                                 (double)values[5] * ((double)values[8] * values[14] - (double)values[10] * values[12]) +
                                 (double)values[6] * ((double)values[8] * values[13] - (double)values[9] * values[12]);
            if (fabs(determinant - 1.0) > 0.001) sample.rigid = 0;
        }
    }
    *out = sample;
    return 1;
}

static inline int player_ui_seat_format(char *buffer, size_t size, const char *stage, const char *role,
                                        uintptr_t ship, uintptr_t room, unsigned bank,
                                        const player_ui_seat_pose *pose) {
    if (!buffer || !stage || !role || !pose || bank > 1) return 0;
    char words[129], raw[129];
    for (unsigned i = 0; i < 16; ++i) {
        snprintf(words + i * 8, 9, "%08x", pose->words[i]);
        snprintf(raw + i * 8, 9, "%08x", pose->raw_words[i]);
    }
    int count = snprintf(buffer, size, "X4 UI SEAT_POSE v=1 tick=9 stage=%s role=%s status=observed "
        "actor=%llx ship=%llx room=%llx parent=%llx controller=%llx vtable=%llx bank=%u view=1 position=%u "
        "candidate=%llx finite=%d rigid=%d words=%s raw=%s\n", stage, role,
        (unsigned long long)pose->actor, (unsigned long long)ship, (unsigned long long)room,
        (unsigned long long)pose->parent, (unsigned long long)pose->controller, (unsigned long long)pose->vtable,
        bank, pose->position, (unsigned long long)pose->pose, pose->pose ? pose->finite : -1,
        pose->pose ? pose->rigid : -1, pose->pose ? words : "-", pose->pose ? raw : "-");
    return count > 0 && (size_t)count < size ? count : 0;
}
#endif

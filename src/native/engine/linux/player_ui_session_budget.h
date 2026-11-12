#ifndef X4_PLAYER_UI_SESSION_BUDGET_H
#define X4_PLAYER_UI_SESSION_BUDGET_H

#include <stdint.h>
#include "player_startup_phases.h"

/* Frames are physics steps, nominally 30 per second. The final six steps
 * drain the guarded presentation lease before the disposable process exits. */
typedef struct {
    uint32_t frames;
    uint32_t last_capture;
    uint32_t recurring_end;
    uint32_t presentation_count;
    uint32_t presentation_last;
    uint32_t report_tick;
} player_ui_session_budget;

static inline int player_ui_session_parse_phased(const char *text, const player_startup_phases *phase,
                                                 player_ui_session_budget *out) {
    if (!text || !out || !player_startup_phases_valid(phase) || !*text || (*text == '0' && text[1])) return 0;
    uint32_t frames = 0;
    for (const char *cursor = text; *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9' || frames > 18000u / 10u) return 0;
        frames = frames * 10u + (uint32_t)(*cursor - '0');
        if (frames > 18000u) return 0;
    }
    if (frames < 180u) return 0;
    uint64_t end = phase->apply + frames - 1u;
    uint64_t last = phase->presentation_first + frames - 11u;
    if (end > UINT32_MAX || last >= end) return 0;
    *out = (player_ui_session_budget){frames, (uint32_t)(end - 2u), (uint32_t)end,
                                      frames - 10u, (uint32_t)last, (uint32_t)(last + 1u)};
    return 1;
}
static inline int player_ui_session_parse(const char *text, player_ui_session_budget *out) {
    player_startup_phases legacy;
    return player_startup_phases_parse(NULL, &legacy) && player_ui_session_parse_phased(text, &legacy, out);
}
#endif

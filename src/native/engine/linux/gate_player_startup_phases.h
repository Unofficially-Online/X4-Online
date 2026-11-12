#ifndef X4_GATE_PLAYER_STARTUP_PHASES_H
#define X4_GATE_PLAYER_STARTUP_PHASES_H

#include "player_startup_phases.h"
#include "player_ui_session_budget.h"
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

/* Parse once before any phase-controlled engine mutation. The published tuple
 * is immutable for the life of this disposable process. */
static pthread_once_t player_startup_once = PTHREAD_ONCE_INIT;
static player_startup_phases player_startup_values;
static int player_startup_valid;
static void player_startup_initialize(void) {
    player_startup_valid = player_startup_phases_parse(getenv("X4_DEV_PLAYER_PHASES"), &player_startup_values);
    if (player_startup_valid && player_startup_values.version) {
        const char *session = getenv("X4_DEV_UI_SESSION_FRAMES");
        const char *physics = getenv("X4_DEV_PLAYER_PHYSICS");
        const char *scripted_throttle = getenv("X4_DEV_P2_THROTTLE");
        const char *recurring = getenv("X4LAN_PLAYER_CONTROL_RECURRING");
        const char *tail = getenv("X4LAN_PLAYER_CONTROL_RECURRING_TAIL");
        const char *end = getenv("X4LAN_PLAYER_CONTROL_RECURRING_END");
        player_ui_session_budget budget;
        char expected_end[32];
        player_startup_valid = player_ui_session_parse_phased(session, &player_startup_values, &budget) &&
            physics && !strcmp(physics, "network-frames-discard") &&
            (!scripted_throttle || !*scripted_throttle) && recurring && !strcmp(recurring, "1") &&
            tail && !strcmp(tail, "1") && end;
        if (player_startup_valid) {
            int length = snprintf(expected_end, sizeof expected_end, "%u", budget.recurring_end);
            player_startup_valid = length > 0 && (size_t)length < sizeof expected_end &&
                !strcmp(end, expected_end);
        }
    }
}
static const player_startup_phases *player_startup(void) {
    if (pthread_once(&player_startup_once, player_startup_initialize) || !player_startup_valid) {
        static const char failure[] = "X4LAN PLAYER_PHASES_FATAL reason=invalid_config\n";
        (void)write(2, failure, sizeof failure - 1);
        _exit(222);
    }
    return &player_startup_values;
}
#endif

#include "player_startup_phases.h"
#include "player_ui_session_budget.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    player_startup_phases phase;
    assert(player_startup_phases_parse(NULL, &phase));
    assert(phase.version == 0 && phase.factory == 2 && phase.seat_request == 2 && phase.seat_preflight == 4 &&
           phase.take == 5 && phase.capture == 3 && phase.apply == 5 && phase.presentation_first == 9);
    assert(player_startup_is_presentation_probe_tick(&phase, 7));
    assert(!player_startup_is_presentation_probe_tick(&phase, 42));
    assert(player_startup_phases_parse("v1-t40", &phase));
    assert(player_startup_is_presentation_probe_tick(&phase, 42));
    assert(!player_startup_is_presentation_probe_tick(&phase, 7));
    assert(phase.version == 1 && phase.factory == 35 && phase.seat_request == 35 && phase.seat_preflight == 36 &&
           phase.take == 37 && phase.capture == 38 && phase.apply == 40 && phase.presentation_first == 44);
    player_ui_session_budget budget;
    assert(player_ui_session_parse_phased("900", &phase, &budget));
    assert(budget.last_capture == 937 && budget.recurring_end == 939 &&
           budget.presentation_count == 890 && budget.presentation_last == 933 && budget.report_tick == 934);
    assert(!player_startup_phases_parse("", &phase));
    assert(!player_startup_phases_parse("v1-t41", &phase));
    assert(!player_startup_phases_parse("v1-t40", NULL));
    assert(player_startup_phases_parse("v1-t40", &phase));
    phase.take = 35; assert(!player_startup_phases_valid(&phase));
    assert(player_startup_phases_parse("v1-t40", &phase));
    phase.capture = 37; assert(!player_startup_phases_valid(&phase));
    assert(player_startup_phases_parse("v1-t40", &phase));
    phase.presentation_arm = 42; assert(!player_startup_phases_valid(&phase));
    assert(player_startup_phases_parse("v1-t40", &phase));
    phase.apply = UINT64_MAX; assert(!player_startup_phases_valid(&phase));
    puts("startup phases validate legacy and common action ordering PASS");
    return 0;
}

#include <assert.h>
#include "player_stock_space.h"

static unsigned char bank[PLAYER_SPACE_BANK_SIZE], input[PLAYER_SPACE_INPUT_SIZE];
static unsigned char other_bank[PLAYER_SPACE_BANK_SIZE];
static unsigned char saved_mouse, saved_direct;
static char calls[32];
static unsigned call_count;
static int option_value;
static int controller, player;
static char drift_after;

static int still_valid(void *context) {
    assert(context == &drift_after);
    return !drift_after || !call_count || calls[call_count - 1] != drift_after;
}

static void note(char code) { assert(call_count < sizeof calls); calls[call_count++] = code; }
static unsigned char authority(void *object) { assert(object == &controller); note('A'); return 1; }
static void handoff(void *object, unsigned char granted) {
    assert(object == &controller && granted == 1); note('H');
    input[PLAYER_SPACE_UI_FIRST] = input[PLAYER_SPACE_UI_SECOND] = 1;
}
static void release(void *object) {
    assert(object == &controller); note('R');
    input[PLAYER_SPACE_UI_FIRST] = input[PLAYER_SPACE_UI_SECOND] = 0;
}
static void started(void *object) { assert(object == &player && bank[PLAYER_SPACE_FLIGHT] == 1); note('S'); }
static void stopped(void *object) { assert(object == &player && bank[PLAYER_SPACE_FLIGHT] == 0); note('T'); }
static int persistent(void *context) { assert(context == &option_value); note('O'); return option_value; }
static void mouse(void *object, unsigned char enabled) {
    assert(object == input && enabled <= 1); note('M'); input[0x9f36] = enabled;
}
static void direct(void *object, unsigned char enabled) {
    assert(object == input && enabled <= 1); note('D'); input[0x9f38] = enabled;
}

static player_space_simulation target(void) {
    return (player_space_simulation){bank, sizeof bank, input, sizeof input, &controller,
        &player, authority, handoff, release, started, stopped, still_valid, &drift_after};
}
static player_space_local local(void) {
    return (player_space_local){input, sizeof input, &saved_mouse, &saved_direct,
        persistent, &option_value, mouse, direct, still_valid, &drift_after};
}
static void reset(void) {
    memset(bank, 0x59, sizeof bank); memset(other_bank, 0x83, sizeof other_bank);
    memset(input, 0, sizeof input); memset(calls, 0, sizeof calls); call_count = 0;
    memset(bank + PLAYER_SPACE_AUTOPILOT, 0, sizeof(uint64_t));
    input[PLAYER_SPACE_UI_FIRST] = 0; input[PLAYER_SPACE_UI_SECOND] = 1;
    saved_mouse = saved_direct = 0; option_value = 1;
    drift_after = 0;
}
static void expect_calls(const char *expected) {
    assert(call_count == strlen(expected) && !memcmp(calls, expected, call_count));
    call_count = 0;
}

int main(void) {
    reset(); player_space_simulation simulation = target();
    unsigned char original_bank[sizeof bank]; memcpy(original_bank, bank, sizeof bank);
    assert(player_space_replay(&simulation, 1, 42.25)); expect_calls("AHS");
    for (unsigned index = 0; index < 5; ++index) {
        double deadline; memcpy(&deadline, bank + PLAYER_SPACE_TIMERS + index * 8, 8);
        assert(deadline == 42.75);
    }
    assert(input[PLAYER_SPACE_UI_FIRST] == 0 && input[PLAYER_SPACE_UI_SECOND] == 1);
    assert(!memcmp(bank, original_bank, PLAYER_SPACE_TIMERS));
    assert(player_space_replay(&simulation, 0, 42.5)); expect_calls("RT");
    assert(input[PLAYER_SPACE_UI_FIRST] == 0 && input[PLAYER_SPACE_UI_SECOND] == 1);
    unsigned char expected[sizeof bank]; memcpy(expected, original_bank, sizeof bank);
    memset(expected, 0, 12); memset(expected + 0x30, 0, 12);
    memset(expected + 0x68, 0, 8); memset(expected + 0x74, 0, 8);
    for (unsigned index = 0; index < 5; ++index) {
        double deadline = 42.75; memcpy(expected + PLAYER_SPACE_TIMERS + index * 8, &deadline, 8);
    }
    expected[PLAYER_SPACE_FLIGHT] = 0;
    assert(!memcmp(bank, expected, sizeof bank)); /* no extra bank bytes changed */
    for (unsigned index = 0; index < sizeof other_bank; ++index) assert(other_bank[index] == 0x83);

    reset(); uint64_t autopilot = 123; memcpy(bank + PLAYER_SPACE_AUTOPILOT, &autopilot, 8);
    assert(player_space_replay(&simulation, 1, 1)); expect_calls("AS");
    assert(player_space_replay(&simulation, 0, 1)); expect_calls("T");
    memcpy(original_bank, bank, sizeof bank);
    assert(!player_space_replay(&simulation, 2, 1));
    assert(!player_space_replay(&simulation, 1, NAN));
    assert(!player_space_replay(&simulation, 0, INFINITY));
    simulation.bank_size--;
    assert(!player_space_replay(&simulation, 1, 1)); simulation = target();
    simulation.input_size--;
    assert(!player_space_replay(&simulation, 1, 1)); simulation = target();
    simulation.controller = NULL;
    assert(!player_space_replay(&simulation, 1, 1));
    assert(!memcmp(original_bank, bank, sizeof bank)); expect_calls("");

    reset(); player_space_local window = local();
    input[0x9f36] = input[0x9f38] = 1;
    memcpy(original_bank, bank, sizeof bank);
    assert(player_space_local_apply(&window, 0)); expect_calls("MD");
    assert(saved_mouse == 1 && saved_direct == 1);
    assert(!input[0x9f36] && !input[0x9f38]);
    input[0x9f33] = 1;
    assert(player_space_local_apply(&window, 1)); expect_calls("OMD");
    assert(input[0x9f36] && input[0x9f38] && !saved_mouse && !saved_direct);
    assert(input[PLAYER_SPACE_UI_FIRST] && input[PLAYER_SPACE_UI_SECOND]);
    assert(!memcmp(original_bank, bank, sizeof bank)); /* local events never mutate simulation bank */

    assert(player_space_local_apply(&window, 0)); expect_calls("MD");
    option_value = 0;
    assert(player_space_local_apply(&window, 1)); expect_calls("O");
    assert(!input[0x9f36] && !input[0x9f38] && !saved_mouse && !saved_direct);
    saved_mouse = saved_direct = 1; input[0x9f33] = 0;
    assert(player_space_local_apply(&window, 1)); expect_calls("");
    assert(saved_mouse && saved_direct); /* untouched when native predicate is false */
    input[0x9f33] = 1; option_value = -1;
    unsigned char original_input[sizeof input]; memcpy(original_input, input, sizeof input);
    assert(!player_space_local_apply(&window, 1)); expect_calls("O");
    assert(!memcmp(original_input, input, sizeof input) && saved_mouse && saved_direct);
    saved_mouse = 2;
    assert(!player_space_local_apply(&window, 0)); expect_calls("");
    assert(!memcmp(original_input, input, sizeof input));

    /* A native callback may replace the InputOwner or controller. Never call
     * the next setter or restore UI bytes through that stale pointer. */
    reset(); window = local(); input[0x9f36] = input[0x9f38] = 1;
    drift_after = 'M';
    assert(!player_space_local_apply(&window, 0)); expect_calls("M");
    assert(input[0x9f38] == 1 && saved_direct == 0);
    reset(); window = local(); input[0x9f33] = 1; saved_mouse = 1;
    drift_after = 'O';
    assert(!player_space_local_apply(&window, 1)); expect_calls("O");
    assert(saved_mouse == 1 && input[0x9f36] == 0);
    reset(); simulation = target(); drift_after = 'H';
    assert(!player_space_replay(&simulation, 1, 1)); expect_calls("AH");
    assert(bank[PLAYER_SPACE_FLIGHT] == 0x59);
    assert(input[PLAYER_SPACE_UI_FIRST] == 1); /* no stale rollback */
    reset(); simulation = target(); drift_after = 'R';
    assert(!player_space_replay(&simulation, 0, 1)); expect_calls("R");
    assert(input[PLAYER_SPACE_UI_SECOND] == 0);
    return 0;
}

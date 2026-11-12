#include <assert.h>
#include <stdint.h>
#include "player_camera_probe.h"

typedef struct {
    int pins;
    int admitted;
    int target_ok;
    int changed;
    unsigned reads;
    unsigned calls;
    uint64_t saved;
    uint64_t runtime;
    uint64_t current;
} fixture;

static int pins(void *context) { return ((fixture *)context)->pins; }
static int admit(void *context) { return ((fixture *)context)->admitted; }
static int held(void *context, player_camera_target *target) {
    fixture *state = context;
    ++state->reads;
    *target = (player_camera_target){0x1234, state->runtime + (state->changed && state->reads > 1), state->saved};
    return state->target_ok;
}
static uint64_t current(void *context) {
    fixture *state = context;
    ++state->calls;
    return state->current;
}

int main(void) {
    fixture state = {1, 1, 1, 0, 0, 0, PLAYER_CAMERA_TARGET_SAVED, 42, 42};
    player_camera_probe_operations operations = {&state, pins, admit, held, current};
    player_camera_observation observation = {0};
    assert(player_camera_observe(&operations, &observation));
    assert(observation.matches && observation.current == 42 && observation.target.runtime == 42);
    state.current = 7;
    assert(player_camera_observe(&operations, &observation) && !observation.matches);
    unsigned calls = state.calls;
    state.pins = 0;
    assert(!player_camera_observe(&operations, &observation) && state.calls == calls);
    state.pins = 1;
    state.admitted = 0;
    assert(!player_camera_observe(&operations, &observation) && state.calls == calls);
    state.admitted = 1;
    state.saved = 99;
    assert(!player_camera_observe(&operations, &observation) && state.calls == calls);
    state.saved = PLAYER_CAMERA_TARGET_SAVED;
    state.runtime = 0;
    assert(!player_camera_observe(&operations, &observation) && state.calls == calls);
    state.runtime = 42;
    state.target_ok = 0;
    assert(!player_camera_observe(&operations, &observation) && state.calls == calls);
    state.target_ok = 1;
    state.reads = 0;
    state.changed = 1;
    assert(!player_camera_observe(&operations, &observation) && state.calls == calls + 1);
    return 0;
}

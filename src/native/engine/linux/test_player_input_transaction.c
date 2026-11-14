#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include "player_stock_view_wire_handoff.h"
static player_stock_view_wire_handoff keyboard_view_wire;
static keyboard_replay keyboard_probe_queue;
static int xs_enabled;
static const char *test_getenv(const char *name) { (void)name; return xs_enabled ? "1" : NULL; }
#define getenv test_getenv
typedef struct { int value; } player_cursor_convergence;
typedef struct { int value; } player_stock_view_transaction;
static player_cursor_convergence player_cursor_state = {1};
static int player_cursor_ready = 1, cursor_ok = 1, view_ok = 1, commit_ok = 1, commits, cursor_prepares, view_prepares;
static int player_cursor_prepare_transaction(uint64_t tick, const uint64_t saved[2], const stock_input_record records[2], player_cursor_convergence *next) {
    (void)tick; (void)saved; (void)records; ++cursor_prepares; next->value = 2; return cursor_ok;
}
static int player_stock_view_prepare_transaction(uint64_t tick, const uint64_t saved[2], const stock_input_record records[2], player_stock_view_transaction *next) {
    (void)tick; (void)saved; (void)records; ++view_prepares; next->value = 3; return view_ok;
}
static int player_stock_view_commit_transaction(const player_stock_view_transaction *view) {
    assert(view->value == 3); ++commits; return commit_ok;
}
#include "gate_player_input_transaction.h"
static int ready(void *context, const keyboard_replay_config *config, uint64_t generation) {
    (void)context; (void)config; (void)generation; return 1;
}

int main(void) {
    uint64_t saved[2] = {1,2}; stock_input_record records[2] = {{0},{0}};
    cursor_ok = 0;
    assert(!player_input_prepare_slots(0, 5, saved, records) && !commits && player_cursor_state.value == 1);
    cursor_ok = 1; view_ok = 0;
    assert(!player_input_prepare_slots(0, 5, saved, records) && !commits && player_cursor_state.value == 1);
    view_ok = 1; commit_ok = 0;
    assert(!player_input_prepare_slots(0, 5, saved, records) && commits == 1 && player_cursor_state.value == 1);
    commit_ok = 1;
    assert(player_input_prepare_slots(0, 5, saved, records) && commits == 2 && player_cursor_state.value == 2);
    keyboard_view_wire = (player_stock_view_wire_handoff){.ready = ready, .admitted = 1, .first = 50, .last = 213};
    keyboard_probe_queue.config = (keyboard_replay_config){.view_capture = 1, .delay = 2, .first_capture = 38, .last_capture = 217};
    xs_enabled = 1;
    const uint64_t ticks[] = {40, 51, 52, 215, 216, 219};
    for (unsigned i = 0; i < sizeof ticks / sizeof ticks[0]; ++i) {
        uint64_t tick = ticks[i];
        unsigned present = tick >= 52 && tick <= 215;
        for (unsigned slot = 0; slot < 2; ++slot) {
            records[slot] = (stock_input_record){.capture16 = (uint16_t)(tick - 2),
                .flags = present ? STOCK_INPUT_VIEW : 0, .view = present ? 1 : 0};
        }
        assert(player_input_prepare_slots(0, tick, saved, records));
        for (unsigned slot = 0; slot < 2; ++slot) {
            int before_cursor = cursor_prepares, before_view = view_prepares, before_commits = commits;
            records[slot].flags ^= STOCK_INPUT_VIEW;
            assert(!player_input_prepare_slots(0, tick, saved, records));
            assert(before_cursor == cursor_prepares && before_view == view_prepares && before_commits == commits);
            records[slot].flags ^= STOCK_INPUT_VIEW;
            records[slot].capture16++;
            assert(!player_input_prepare_slots(0, tick, saved, records));
            assert(before_cursor == cursor_prepares && before_view == view_prepares && before_commits == commits);
            records[slot].capture16--;
            if (present) {
                records[slot].view = 3;
                assert(!player_input_prepare_slots(0, tick, saved, records));
                assert(before_cursor == cursor_prepares && before_view == view_prepares && before_commits == commits);
                records[slot].view = 0;
                assert(!player_input_prepare_slots(0, tick, saved, records));
                records[slot].view = 1;
            }
        }
    }
    assert(!player_input_prepare_slots(0, 39, saved, records));
    assert(!player_input_prepare_slots(0, 220, saved, records));
    keyboard_probe_queue.config.view_capture = 0;
    assert(!player_input_prepare_slots(0, 219, saved, records));
    keyboard_probe_queue.config.view_capture = 1;
    keyboard_view_wire.first = 214;
    assert(!player_input_prepare_slots(0, 219, saved, records));
    keyboard_view_wire.first = 50;
    for (unsigned slot = 0; slot < 2; ++slot)
        records[slot] = (stock_input_record){.capture16 = 50, .flags = STOCK_INPUT_VIEW, .view = 3};
    xs_enabled = 0;
    assert(player_input_prepare_slots(0, 52, saved, records));
    return 0;
}

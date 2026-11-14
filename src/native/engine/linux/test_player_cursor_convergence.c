#include "player_cursor_convergence.h"
#include <assert.h>
#include <math.h>

static void test_local_menu_during_delayed_flight(void) {
    player_cursor_convergence state = {0};
    stock_input_record active = {.flags = STOCK_INPUT_ACTIVE | STOCK_INPUT_CURSOR,
                                  .capture16 = 8, .cursor = {1, -2, 3}};
    stock_input_record inactive = {.capture16 = 8};
    assert(player_cursor_convergence_stage(&state, 10, 0x1000, 0x2000, &active, &inactive));
    float native[3] = {0.25f, -0.5f, 0.75f}, captured[3];
    assert(player_cursor_convergence_apply_local(&state, 10, 0x1000, 1, 0, native));
    /* Remote delayed input still applies; local inactive UI contributes zero. */
    assert(!memcmp(native, active.cursor, sizeof native));
    assert(player_cursor_convergence_capture(&state, 10, captured));
    for (unsigned index = 0; index < 3; ++index) assert(!stock_input_float_bits(captured[index]));
    assert(player_cursor_convergence_apply_local(&state, 10, 0x2000, 1, 0, native));
    for (unsigned index = 0; index < 3; ++index) assert(!stock_input_float_bits(native[index]));
    player_cursor_convergence invalid = {0};
    assert(player_cursor_convergence_capture_only(&invalid, 38, 0x1000, 0x2000));
    assert(!player_cursor_convergence_apply_local(&invalid, 38, 0x1000, 1, 2, native));
    assert(invalid.failed && !invalid.seen);
}

int main(void) {
    test_local_menu_during_delayed_flight();
    player_cursor_convergence early = {0};
    assert(player_cursor_convergence_capture_only(&early, 38, 0x1000, 0x2000));
    float early_input[3] = {1, 2, 3}, early_captured[3];
    assert(player_cursor_convergence_apply(&early, 38, 0x1000, 1, early_input));
    assert(player_cursor_convergence_capture(&early, 38, early_captured));
    assert(early_captured[0] == 1 && early_captured[1] == 2 && early_captured[2] == 3);
    assert(!stock_input_float_bits(early_input[0]) && !stock_input_float_bits(early_input[1]) &&
           !stock_input_float_bits(early_input[2]));
    assert(player_cursor_convergence_capture_only(&early, 39, 0x1000, 0x2000));
    assert(player_cursor_convergence_apply(&early, 39, 0x1000, 0, early_input));
    assert(!player_cursor_convergence_capture_only(&early, 40, 0x1000, 0x3000) && early.failed);
    player_cursor_convergence state = {0};
    stock_input_record first = {.flags = STOCK_INPUT_CURSOR, .capture16 = 8, .cursor = {1, -2, 3}};
    stock_input_record second = {.flags = STOCK_INPUT_CURSOR, .capture16 = 8, .cursor = {4, 5, -6}};
    assert(player_cursor_convergence_stage(&state, 10, 0x1000, 0x2000, &first, &second));
    float guest[3] = {8, 8, 8};
    float canonical[3] = {0.25f, -0.5f, 0.75f};
    assert(player_cursor_convergence_apply(&state, 10, 0x2000, 1, guest));
    assert(!memcmp(guest, second.cursor, sizeof guest));
    assert(player_cursor_convergence_apply(&state, 10, 0x1000, 1, canonical));
    assert(!memcmp(canonical, first.cursor, sizeof canonical));
    float captured[3];
    assert(player_cursor_convergence_capture(&state, 10, captured));
    assert(captured[0] == 0.25f && captured[1] == -0.5f && captured[2] == 0.75f);
    first.capture16 = second.capture16 = 9;
    second.flags = 0;
    memset(second.cursor, 0, sizeof second.cursor);
    assert(player_cursor_convergence_stage(&state, 11, 0x1000, 0x2000, &first, &second));
    assert(player_cursor_convergence_apply(&state, 11, 0x1000, 0, canonical));
    assert(player_cursor_convergence_capture(&state, 11, captured));
    assert(!stock_input_float_bits(captured[0]) && !stock_input_float_bits(captured[1]) && !stock_input_float_bits(captured[2]));
    assert(player_cursor_convergence_apply(&state, 11, 0x2000, 1, guest));
    assert(!stock_input_float_bits(guest[0]) && !stock_input_float_bits(guest[1]) && !stock_input_float_bits(guest[2]));
    assert(!player_cursor_convergence_apply(&state, 11, 0x2000, 1, guest) && state.failed);
    player_cursor_convergence bad = {0};
    second.cursor[0] = NAN;
    assert(!player_cursor_convergence_stage(&bad, 11, 0x1000, 0x2000, &first, &second));
    assert(bad.failed && !player_cursor_convergence_apply(&bad, 11, 0x1000, 1, canonical));
    return 0;
}

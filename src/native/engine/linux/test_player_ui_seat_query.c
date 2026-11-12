#include "player_ui_seat_query.h"
#include <assert.h>

typedef struct { unsigned events[6], count, fail_at; } fixture;
static int reference(void *context, int room, int acquire) {
    fixture *test = context;
    unsigned event = acquire ? (room ? 2 : 1) : (room ? 5 : 6);
    test->events[test->count++] = event;
    return test->count != test->fail_at;
}
static int query(void *context, unsigned index) {
    fixture *test = context;
    test->events[test->count++] = index ? 4 : 3;
    return test->count != test->fail_at;
}
int main(void) {
    fixture test = {0};
    player_ui_seat_query_ops ops = {&test, reference, query};
    assert(player_ui_seat_query_run(&ops) && test.count == 6);
    for (unsigned i = 0; i < 6; ++i) assert(test.events[i] == i + 1);
    for (unsigned failure = 1; failure <= 6; ++failure) {
        memset(&test, 0, sizeof test);
        test.fail_at = failure;
        assert(!player_ui_seat_query_run(&ops) && test.count == failure);
    }
    float identity[16] = {0,0,0,1, 1,0,0,0, 0,1,0,0, 0,0,1,0};
    player_ui_seat_query_result result;
    player_ui_seat_query_classify(identity, &result);
    assert(result.finite && result.rigid);
    uint32_t nan_bits = 0x7fc00000;
    memcpy(&identity[0], &nan_bits, sizeof nan_bits);
    player_ui_seat_query_classify(identity, &result);
    assert(!result.finite && !result.rigid && result.raw[0] == nan_bits);
}

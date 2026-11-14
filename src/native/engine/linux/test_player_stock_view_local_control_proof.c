#define main shared_control_fixture_main
#include "test_player_stock_view_control_proof.c"
#undef main
#include "player_stock_view_local_control_proof.h"

int main(void) {
    fixture test;
    player_stock_view_control_ops ops;
    setup(&test, &ops);
    assert(player_stock_view_local_control_equivalent(&ops));
    setup(&test, &ops);
    test.vector[0] = 0x1111;
    assert(!player_stock_view_local_control_equivalent(&ops));
    setup(&test, &ops);
    test.drift_registry = 1;
    assert(!player_stock_view_local_control_equivalent(&ops));
    setup(&test, &ops);
    set64(test.room + 0x70, 0);
    assert(!player_stock_view_local_control_equivalent(&ops));
    setup(&test, &ops);
    set32(test.block + 28, 0x05510000);
    assert(!player_stock_view_local_control_equivalent(&ops));
    setup(&test, &ops);
    test.auth_ok = 0;
    assert(!player_stock_view_local_control_equivalent(&ops));
    return 0;
}

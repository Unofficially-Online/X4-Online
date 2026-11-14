#include <assert.h>
#include <math.h>
#include "player_stock_view_mailbox.h"
int main(void) {
    player_stock_view_mailbox box = {0};
    assert(!player_stock_view_mailbox_configure(&box, 38, 217, 44, 213, 2, 3));
    assert(player_stock_view_mailbox_configure(&box, 43, 217, 44, 45, 2, 2));
    unsigned present = 9;
    player_stock_view_capture_sample sample = {3, 1.5f, 123}, out = {2, 5.0f, 456};
    assert(!player_stock_view_mailbox_seal(&box, 42, 2, &present, &out));
    assert(player_stock_view_mailbox_seal(&box, 43, 2, &present, &out) && !present);
    assert(!player_stock_view_mailbox_seal(&box, 44, 2, &present, &out));
    assert(!player_stock_view_mailbox_stage(&box, 44, 1, &sample));
    assert(player_stock_view_mailbox_stage(&box, 44, 2, &sample));
    sample.target = 999; /* stage copied the complete sample */
    assert(!player_stock_view_mailbox_stage(&box, 44, 2, &sample));
    assert(!player_stock_view_mailbox_seal(&box, 45, 2, &present, &out));
    assert(player_stock_view_mailbox_seal(&box, 44, 2, &present, &out));
    assert(present && out.bits == 3 && out.fov == 1.5f && out.target == 123);
    assert(!player_stock_view_mailbox_seal(&box, 44, 2, &present, &out));
    sample.fov = NAN;
    assert(!player_stock_view_mailbox_stage(&box, 45, 2, &sample));
    sample.fov = 1.0f;
    assert(player_stock_view_mailbox_stage(&box, 45, 2, &sample));
    assert(player_stock_view_mailbox_seal(&box, 45, 2, &present, &out) && present && out.target == 999);
    assert(player_stock_view_mailbox_seal(&box, 46, 2, &present, &out) && !present);
    return 0;
}

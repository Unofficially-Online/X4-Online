#ifndef X4_GATE_PLAYER_INPUT_TRANSACTION_H
#define X4_GATE_PLAYER_INPUT_TRANSACTION_H
/* Both records and both adapters must validate before publishing either state.
 * A failed VIEW bind can have partial native side effects; the caller must
 * fail-stop before resuming a native frame. */
static int player_input_prepare_slots(void *context, uint64_t generation, const uint64_t saved[2],
                                      const stock_input_record records[2]) {
    (void)context;
    player_cursor_convergence cursor;
    player_stock_view_transaction view;
    if (!player_stock_view_wire_pair_valid(&keyboard_view_wire, &keyboard_probe_queue.config,
                                           generation, records, getenv("X4_DEV_VIEW_XS") != NULL) ||
        !player_cursor_prepare_transaction(generation, saved, records, &cursor) ||
        !player_stock_view_prepare_transaction(generation, saved, records, &view)) return 0;
    if (!player_stock_view_commit_transaction(&view)) return 0;
    if (player_cursor_ready) player_cursor_state = cursor;
    return 1;
}
#endif

#ifndef X4_GATE_PLAYER_STOCK_VIEW_WIRE_HANDOFF_H
#define X4_GATE_PLAYER_STOCK_VIEW_WIRE_HANDOFF_H
/* Include after both Host/Guest capture headers. This dormant bridge has no
 * caller until the coordinator supplies the complete readiness predicate. */
static __attribute__((unused)) int player_stock_view_wire_enable(
        player_stock_view_wire_ready_fn ready, void *context) {
    if (keyboard_probe_requested || keyboard_probe_queue.initialized ||
        stats.issued != 1 || !stats.active || !accept_ui() ||
        !gate_tls_worker(gate_tls_self()) ||
        GetCurrentThreadId() != stock_observer_worker_thread) return 0;
    return player_stock_view_wire_bind(&keyboard_view_wire, ready, context);
}

static int player_stock_view_wire_read_sealed(uint64_t generation, unsigned slot,
                                             player_stock_view_wire_sealed *out) {
    if (!out || !keyboard_probe_worker() || !player_recurring_physics_entered() ||
        generation != stats.issued || slot != stats.slot ||
        generation != player_recurring_tail_token.generation || slot < 1 || slot > 2) return 0;
    const player_stock_view_mailbox *mailbox = slot == 1 ? &host_view_mailbox : &render_view_mailbox;
    const player_stock_view_capture_sample *sample = slot == 1 ? &host_view_sealed : &render_view_sealed;
    unsigned enabled = slot == 1 ? atomic_load_explicit(&host_view_enabled, memory_order_acquire) :
                                  atomic_load_explicit(&render_view_enabled, memory_order_acquire);
    uint64_t owner = slot == 1 ? atomic_load_explicit(&host_view_owner_tid, memory_order_relaxed) :
                                 atomic_load_explicit(&render_view_owner_tid, memory_order_relaxed);
    if (!enabled || owner != (uint64_t)syscall(SYS_gettid) || !mailbox->configured ||
        mailbox->slot != slot || mailbox->last_sealed != generation || mailbox->pending ||
        mailbox->capture_first != keyboard_probe_queue.config.first_capture ||
        mailbox->capture_last != keyboard_probe_queue.config.last_capture ||
        mailbox->first != keyboard_view_wire.first || mailbox->last != keyboard_view_wire.last) return 0;
    player_stock_view_wire_sealed next = {
        .generation = generation, .slot = slot,
        .present = slot == 1 ? host_view_sealed_present : render_view_sealed_present,
        .view = {sample->bits, sample->fov, sample->target}
    };
    *out = next;
    return 1;
}
#endif

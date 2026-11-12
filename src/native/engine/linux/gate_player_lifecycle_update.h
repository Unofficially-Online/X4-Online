#ifndef X4_GATE_PLAYER_LIFECYCLE_UPDATE_H
#define X4_GATE_PLAYER_LIFECYCLE_UPDATE_H

/* One real P2 controller Update in the already entered disposable scope.
 * Optional STATE2 input is owned by the separate lifecycle input probe.
 * No subsequent physics step or gameplay is claimed.
 * This probe refuses known early-return conditions and observes the native
 * late time write; simply returning from the function is insufficient. */
static void lifecycle_discard_update_once(int injected_input) {
    static const lx_site update_pin = {"p2-controller-update", 0x166d020, 15106,
        "f5fb3e3afe84b9c4f1ecd7c27088d49e6cf6e84971c2d8af3c05bdba15e4143f"};
    static const lx_site zone_pin = {"current-player-zone", 0x1dd0270, 85,
        "77e7ef91167073d9dc9d54f5fd9eca2eca0c69f66ab840e6917b23999180b59f"};
    lifecycle_discard_require(site_ok(&update_pin) && site_ok(&zone_pin) &&
        factory_probe_singleton(factory_probe_state.player2) &&
        lifecycle_discard_pointer(lifecycle_discard_ship + 0x200) == lifecycle_discard_controller &&
        (uintptr_t)((void *(*)(void))LX(0x1dd19d0))() == lifecycle_discard_controller,
        "update_identity_or_pins");
    uintptr_t zone = (uintptr_t)((void *(*)(void))LX(0x1dd0270))();
    lifecycle_discard_require(zone &&
        zone == lifecycle_discard_pointer((uintptr_t)LX(PLAYER_ZONE_GLOBAL_VA)), "update_zone_mismatch");

    unsigned char *tls = gate_tls_self();
    double now = 0, previous = 0, last = 0;
    lifecycle_discard_require(gate_tls_worker(tls), "update_not_worker");
    lifecycle_discard_read((uintptr_t)gate_tls_time(tls), &now, sizeof(now));
    lifecycle_discard_read((uintptr_t)(tls + GATE_TLS_LINUX_PREVIOUS_TIME), &previous, sizeof(previous));
    lifecycle_discard_read(lifecycle_discard_controller + 0xe8, &last, sizeof(last));
    /* The sentinel branch initializes last to this value, before later guards.
     * It must differ from now so it cannot imitate the late write at 166e9b1. */
    double elapsed = now - previous;
    double initialized = now - elapsed;
    unsigned char update_in_progress = 0;
    lifecycle_discard_read((uintptr_t)LX(0x3ee9b40), &update_in_progress, sizeof(update_in_progress));
    lifecycle_discard_require(isfinite(now) && now > 0 && isfinite(previous) && previous < now &&
        isfinite(last) && last < now && isfinite(elapsed) && elapsed > 0 &&
        isfinite(initialized) && initialized != now && !update_in_progress, "update_time_or_global_guard");
    char message[512];
    snprintf(message, sizeof(message),
        "X4LAN LIFECYCLE_DISCARD stage=before_update tick=%llu now=%.17g previous=%.17g "
        "last=%.17g initializer=%.17g\n", (unsigned long long)stats.issued, now, previous, last, initialized);
    lifecycle_discard_write(message);
    ((void (*)(void *, double))LX(0x166d020))((void *)lifecycle_discard_controller, now);
    lifecycle_discard_require(factory_probe_singleton(factory_probe_state.player2) &&
        lifecycle_discard_pointer(lifecycle_discard_ship + 0x200) == lifecycle_discard_controller &&
        (uintptr_t)((void *(*)(void))LX(0x1dd19d0))() == lifecycle_discard_controller,
        "update_progress_or_identity");
    lifecycle_discard_read(lifecycle_discard_controller + 0xe8, &last, sizeof(last));
    snprintf(message, sizeof(message), "X4LAN LIFECYCLE_DISCARD stage=after_update last=%.17g\n", last);
    lifecycle_discard_write(message);
    lifecycle_discard_require(last == now && factory_probe_singleton(factory_probe_state.player2) &&
        lifecycle_discard_pointer(lifecycle_discard_ship + 0x200) == lifecycle_discard_controller &&
        (uintptr_t)((void *(*)(void))LX(0x1dd19d0))() == lifecycle_discard_controller &&
        (uintptr_t)((void *(*)(void))LX(0x1dd0270))() == zone &&
        lifecycle_discard_pointer((uintptr_t)LX(PLAYER_ZONE_GLOBAL_VA)) == zone,
        "update_progress_or_identity");
    snprintf(message, sizeof(message), "X4LAN LIFECYCLE_DISCARD update=pass calls=1 input=%s worker=1 "
        "zone_matches=1 controller_identity=1 time=%.17g\n",
        injected_input == 3 ? "record" : injected_input == 2 ? "state3" : injected_input ? "state2" : "none", now);
    lifecycle_discard_write(message);
}
#endif

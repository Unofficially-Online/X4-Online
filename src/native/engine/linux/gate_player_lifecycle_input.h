#ifndef X4_GATE_PLAYER_LIFECYCLE_INPUT_H
#define X4_GATE_PLAYER_LIFECYCLE_INPUT_H

/* Developer-only disposable experiment. Include after the lifecycle helpers.
 * The caller owns the single native Update between press and release. This
 * header invokes STATE only; it never writes keys, flags or axis memory.
 * Neither method returns on failed admission/readback. */
static unsigned lifecycle_input_pressed;
static unsigned lifecycle_input_released;

static int lifecycle_input_pins(void) {
    static const lx_site pins[] = {
        {"input-state", 0x166c6f0, 2349,
         "369d1664b722dec42bee51f949b3c0c32c16bf1353e00412e271ffbedb6dd335"},
        {"input-state-jump-table", 0x2d6ddc4, 32,
         "a250c5a47252b8276313d6872f9bc49aa8055b4370038aa3c000ab197c8e7747"},
    };
    return site_ok(&pins[0]) && site_ok(&pins[1]);
}

static void lifecycle_input_identity(void) {
    lifecycle_discard_require(lifecycle_input_pins() && factory_probe_joined_boundary() &&
        gate_tls_worker(gate_tls_self()) && !atomic_load(&stock_observer_input_depth) &&
        !atomic_load(&sko_session.wrapper_depth) && factory_probe_state.player2 &&
        factory_probe_state.player2_owned && !factory_probe_state.player2_uncertain &&
        factory_probe_singleton(factory_probe_state.player2) && player_target_hold_check() &&
        lifecycle_discard_ship == player_target_hold_resident.identity.target &&
        lifecycle_discard_controller &&
        lifecycle_discard_pointer(lifecycle_discard_ship + 0x200) == lifecycle_discard_controller &&
        lifecycle_discard_pointer(lifecycle_discard_controller) ==
            (uintptr_t)LX(SEAT_PREFLIGHT_CONTROLLER_VTABLE) &&
        lifecycle_discard_pointer(lifecycle_discard_controller + 0x20) == lifecycle_discard_ship,
        "input_identity_or_pins");
}

static float lifecycle_input_read(unsigned char keys[4]) {
    unsigned char flight, enabled, ignored;
    uint64_t autopilot;
    float digital;
    lifecycle_input_identity();
    lifecycle_discard_read((uintptr_t)LX(0x3d8daa8), &flight, 1);
    lifecycle_discard_read((uintptr_t)LX(0x3d8da60), &enabled, 1);
    lifecycle_discard_read((uintptr_t)LX(0x3e5b29a), &ignored, 1);
    lifecycle_discard_read((uintptr_t)LX(0x3d8da58), &autopilot, sizeof(autopilot));
    lifecycle_discard_read((uintptr_t)LX(0x3d8da68), keys, 4);
    lifecycle_discard_read((uintptr_t)LX(0x3d8d9ac), &digital, sizeof(digital));
    lifecycle_discard_require(flight == 1 && enabled == 1 && !ignored && !autopilot &&
                              isfinite(digital), "input_suppressed_or_nonfinite");
    return digital;
}

static void lifecycle_discard_input_press(void) {
    unsigned char keys[4];
    lifecycle_discard_require(!lifecycle_input_pressed && !lifecycle_input_released,
                              "input_duplicate_press");
    float before = lifecycle_input_read(keys);
    lifecycle_discard_require(!memcmp(keys, (unsigned char[4]){0, 0, 0, 0}, 4) && before == 0,
                              "input_not_neutral");
    lifecycle_input_pressed = 1; /* Obligation exists before native entry. */
    ((void (*)(int, unsigned char))LX(0x166c6f0))(2, 1);
    float measured = lifecycle_input_read(keys);
    lifecycle_discard_require(!memcmp(keys, (unsigned char[4]){1, 0, 0, 0}, 4) && measured == 0,
                              "input_press_readback");
    char message[256];
    snprintf(message, sizeof(message), "X4LAN LIFECYCLE_DISCARD input=press id=2 state=1 "
             "keys=1000 digital=%.9g\n", (double)measured);
    lifecycle_discard_write(message);
}

static void lifecycle_discard_input_release(void) {
    unsigned char keys[4];
    lifecycle_discard_require(lifecycle_input_pressed && !lifecycle_input_released,
                              "input_release_without_press");
    float measured = lifecycle_input_read(keys);
    lifecycle_discard_require(!memcmp(keys, (unsigned char[4]){1, 0, 0, 0}, 4) && measured == -1,
                              "input_update_axis_readback");
    ((void (*)(int, unsigned char))LX(0x166c6f0))(2, 0);
    (void)lifecycle_input_read(keys);
    lifecycle_discard_require(!memcmp(keys, (unsigned char[4]){0, 0, 0, 0}, 4),
                              "input_release_readback");
    lifecycle_input_released = 1;
    char message[256];
    /* digital is the measured post-Update, pre-key-up value. No second Update
     * runs here, so this does not claim the released axis has returned to zero. */
    snprintf(message, sizeof(message), "X4LAN LIFECYCLE_DISCARD input=release id=2 state=0 "
             "keys=0000 digital=%.9g\n", (double)measured);
    lifecycle_discard_write(message);
}
#endif

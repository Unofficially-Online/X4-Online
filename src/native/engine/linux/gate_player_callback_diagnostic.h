#ifndef X4_GATE_PLAYER_CALLBACK_DIAGNOSTIC_H
#define X4_GATE_PLAYER_CALLBACK_DIAGNOSTIC_H

/* These callbacks only report the values received from native code and stop
 * the disposable process. Borrowed argument addresses are never dereferenced. */
static _Noreturn void player_physics_callback(unsigned slot, void *controller,
                                               const void *first, const void *second,
                                               unsigned has_mode, unsigned mode) {
    char message[384], reason[64];
    int count = snprintf(message, sizeof message,
        "X4LAN LIFECYCLE_DISCARD physics=callback slot=%u thread=%llu controller=%llx "
        "expected=%llx argument1=%llx argument2=%llx has_mode=%u mode=%u\n",
        slot, (unsigned long long)GetCurrentThreadId(),
        (unsigned long long)(uintptr_t)controller,
        (unsigned long long)lifecycle_discard_controller,
        (unsigned long long)(uintptr_t)first, (unsigned long long)(uintptr_t)second,
        has_mode, mode);
    if (count > 0 && (size_t)count < sizeof message) {
        lifecycle_discard_write(message);
    }
    snprintf(reason, sizeof reason, "physics_callback_%u", slot);
    lifecycle_discard_exit(0, reason);
}

static void player_physics_slot41(void *controller, void *first, void *second) {
    player_physics_callback(41, controller, first, second, 0, 0);
}

static void player_physics_slot42(void *controller, const void *owner) {
    player_physics_callback(42, controller, owner, NULL, 0, 0);
}

static void player_physics_slot43(void *controller, void *first, void *second, unsigned char mode) {
    player_physics_callback(43, controller, first, second, 1, mode);
}
#endif

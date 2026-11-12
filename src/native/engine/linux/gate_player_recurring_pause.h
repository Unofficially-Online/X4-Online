#ifndef X4_GATE_PLAYER_RECURRING_PAUSE_H
#define X4_GATE_PLAYER_RECURRING_PAUSE_H

/* X4's IsGamePaused locks the timer's recursive mutex twice and returns the OR
 * of its two protected counters. This diagnostic tries the same mutex once;
 * failure is unknown, never an unpaused result. No blocking native call. */
static void player_recurring_pause_sample(player_recurring_receipt *receipt) {
    static const lx_site pins[] = {
        {"timer-startup", 0x9a463f, 118,
         "4bcbe8fa1c1b9fe56d1e68fb5654cc702dd28c18b1db797439ff4920ed2e3d2d"},
        {"is-game-paused", 0xc1ea20, 12,
         "5a68f009952d0267dde65837f1fbdd56a3b77a483fa07ff6c6890697d5b89166"},
        {"pause-query", 0x1a3b800, 74,
         "fb72102ba8c2aedb0ff25c030813610e44a40bf968d303a2c3496d028e21bdfa"},
        {"timer-construct", 0x1a23520, 175,
         "45ea5cc8c3ef7900fc626c1ce6ff825a9431b51923e457fbae1e6be7ecefdc8e"},
        {"timer-lock", 0x1a23650, 67,
         "2791cce84f3a2ea1b2defed3155f73b056d74fd25dace8cf7d389d6dd72bb670"},
        {"timer-unlock", 0x1a236a0, 67,
         "bdd2a4017637a4d460ffe4ca404c52dc578debd93c21f21486ae7050afba2305"},
    };
    receipt->paused = -1;
    if (!stats.active || boot_phase != 3 || GetCurrentThreadId() != stock_observer_worker_thread ||
        !gate_tls_worker(gate_tls_self())) return;
    for (unsigned index = 0; index < sizeof pins / sizeof pins[0]; ++index) {
        if (!site_ok(&pins[index])) return;
    }
    pthread_mutex_t *mutex = (pthread_mutex_t *)LX(0x3db8288);
    int error = pthread_mutex_trylock(mutex);
    if (error) {
        receipt->pause_error = error;
        return;
    }
    uint32_t first = 0, second = 0;
    int readable = read_local(LX(0x3db82e4), &first, sizeof first) &&
                   read_local(LX(0x3db82e8), &second, sizeof second);
    if (pthread_mutex_unlock(mutex)) {
        _exit(224); /* A possibly retained engine mutex cannot be ignored. */
    }
    if (readable) {
        receipt->pause_available = 1;
        receipt->paused = !!first || !!second;
    }
}
#endif

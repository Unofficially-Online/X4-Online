#ifndef X4_GATE_PLAYER_RECURRING_RECEIPT_H
#define X4_GATE_PLAYER_RECURRING_RECEIPT_H

/* Copies of independently observed atomics and thread-local token fields.
 * These are diagnostic samples, not an atomic snapshot or an admission. */
typedef struct {
    unsigned available;
    uint64_t thread, state, token_generation, token_thread, main_tick, worker_last;
    uintptr_t token_owner;
    unsigned busy;
    int pause_available, paused, pause_error;
} player_recurring_receipt;

static inline int player_recurring_receipt_format(char *output, size_t capacity,
        const char *stage, const player_recurring_receipt *receipt) {
    int length = snprintf(output, capacity,
        "X4LAN RECURRING_FAILURE stage=%s available=%u thread=%llu state=%llu "
        "generation=%llu flags=%llu token_owner=%llx token_generation=%llu token_thread=%llu "
        "busy=%u main_tick=%llu worker_last=%llu pause_available=%d paused=%d pause_error=%d\n",
        stage, receipt->available, (unsigned long long)receipt->thread,
        (unsigned long long)receipt->state, (unsigned long long)(receipt->state >> 8),
        (unsigned long long)(receipt->state & 255), (unsigned long long)receipt->token_owner,
        (unsigned long long)receipt->token_generation, (unsigned long long)receipt->token_thread,
        receipt->busy, (unsigned long long)receipt->main_tick, (unsigned long long)receipt->worker_last,
        receipt->pause_available, receipt->paused, receipt->pause_error);
    return length > 0 && (size_t)length < capacity;
}
#endif

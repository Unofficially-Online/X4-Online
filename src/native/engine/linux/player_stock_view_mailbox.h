#ifndef X4_PLAYER_STOCK_VIEW_MAILBOX_H
#define X4_PLAYER_STOCK_VIEW_MAILBOX_H

#include "player_stock_view_capture.h"

/* Owned by the input worker. A completed UI sample may be consumed only at
 * the seal for the same generation and local slot. No native pointers escape
 * the capture callback, and this object neither changes nor admits wire data. */
typedef struct {
    uint64_t capture_first, capture_last, first, last, staged_generation, last_sealed;
    unsigned slot, configured, pending;
    player_stock_view_capture_sample sample;
} player_stock_view_mailbox;

static inline int player_stock_view_mailbox_configure(player_stock_view_mailbox *box,
        uint64_t capture_first, uint64_t capture_last, uint64_t first, uint64_t last,
        unsigned slot, unsigned delay) {
    if (!box || box->configured || !capture_first || capture_last < capture_first ||
        first < capture_first || last < first || last > capture_last ||
        slot < 1 || slot > 2 || delay != 2) return 0;
    *box = (player_stock_view_mailbox){.capture_first = capture_first, .capture_last = capture_last,
        .first = first, .last = last, .slot = slot, .configured = 1};
    return 1;
}

static inline int player_stock_view_mailbox_stage(player_stock_view_mailbox *box,
        uint64_t generation, unsigned slot, const player_stock_view_capture_sample *sample) {
    if (!box || !box->configured || !sample || slot != box->slot ||
        generation < box->first || generation > box->last || generation <= box->last_sealed ||
        box->pending || sample->bits > 3 || !isfinite(sample->fov)) return 0;
    box->sample = *sample;
    box->staged_generation = generation;
    box->pending = 1;
    return 1;
}

/* Output is unchanged on failure. Outside the agreed interval, present is 0;
 * inside it a fresh completed sample is mandatory. Seal generations are
 * consecutive after the first call, so a skipped or duplicate seal fails. */
static inline int player_stock_view_mailbox_seal(player_stock_view_mailbox *box,
        uint64_t generation, unsigned slot, unsigned *present, player_stock_view_capture_sample *sample) {
    if (!box || !box->configured || !present || !sample || generation < box->capture_first || generation > box->capture_last || slot != box->slot ||
        (!box->last_sealed && generation != box->capture_first) ||
        (box->last_sealed && (box->last_sealed == UINT64_MAX || generation != box->last_sealed + 1))) return 0;
    unsigned required = generation >= box->first && generation <= box->last;
    if ((required && (!box->pending || box->staged_generation != generation)) ||
        (!required && box->pending)) return 0;
    player_stock_view_capture_sample next = {0};
    if (required) next = box->sample;
    box->last_sealed = generation;
    box->pending = 0;
    *present = required;
    *sample = next;
    return 1;
}
#endif

#ifndef X4_PLAYER_STOCK_VIEW_WIRE_HANDOFF_H
#define X4_PLAYER_STOCK_VIEW_WIRE_HANDOFF_H
#include "stock_keyboard_replay.h"

/* One internal readiness predicate must authenticate the entire VIEW route:
 * capture semantics, both-slot mode/FOV/ship/component/early-engine/floating/
 * target consumers, local/remote scopes, and absent/drain lifecycle. Returning
 * true for installed coefficients alone is insufficient. No environment flag
 * binds this predicate. Generation zero is request-time admission; subsequent
 * calls authenticate the actual seal generation and unchanged configuration. */
typedef int (*player_stock_view_wire_ready_fn)(void *context,
    const keyboard_replay_config *config, uint64_t generation);

typedef struct {
    player_stock_view_wire_ready_fn ready;
    void *context;
    unsigned admitted;
    uint64_t first, last;
} player_stock_view_wire_handoff;

typedef struct {
    uint64_t generation;
    unsigned slot, present;
    linux_stock_view_sample view;
} player_stock_view_wire_sealed;

/* Validate the pair before either adapter can publish state or bind providers.
 * The wire describes capture generations; replay applies them after delay. */
static inline int player_stock_view_wire_pair_valid(
        const player_stock_view_wire_handoff *handoff, const keyboard_replay_config *config,
        uint64_t generation, const stock_input_record records[2], int cockpit_only) {
    if (!handoff || !config || !records || handoff->admitted > 1 ||
        config->view_capture > 1 || handoff->admitted != config->view_capture) return 0;
    if (handoff->admitted) {
        if (!handoff->ready || config->delay != 2 || generation < config->delay ||
            !handoff->first || handoff->last < handoff->first ||
            handoff->first < config->first_capture || handoff->last > config->last_capture)
            return 0;
        uint64_t capture = generation - config->delay;
        if (capture < config->first_capture || capture > config->last_capture) return 0;
        unsigned required = capture >= handoff->first && capture <= handoff->last;
        for (unsigned slot = 0; slot < 2; ++slot) {
            if (records[slot].capture16 != (uint16_t)capture ||
                !!(records[slot].flags & STOCK_INPUT_VIEW) != required) return 0;
        }
    }
    if (cockpit_only) {
        for (unsigned slot = 0; slot < 2; ++slot) {
            if ((records[slot].flags & STOCK_INPUT_VIEW) && records[slot].view != 1) return 0;
        }
    }
    return 1;
}

static inline int player_stock_view_wire_bind(player_stock_view_wire_handoff *handoff,
        player_stock_view_wire_ready_fn ready, void *context) {
    if (!handoff || handoff->ready || handoff->admitted || !ready) return 0;
    handoff->ready = ready;
    handoff->context = context;
    return 1;
}

static inline int player_stock_view_wire_configure(player_stock_view_wire_handoff *handoff,
        keyboard_replay_config *config, uint64_t first, uint64_t last) {
    if (!handoff || !config || handoff->admitted || config->view_capture) return 0;
    if (!handoff->ready) return 1; /* Default off preserves the existing queue. */
    if (!config->independent_players || !config->cursor_active_only || config->delay != 2 ||
        config->lookahead || first < config->first_capture || !first || last < first ||
        last > config->last_capture || handoff->ready(handoff->context, config, 0) != 1) return 0;
    handoff->first = first;
    handoff->last = last;
    handoff->admitted = 1;
    config->view_capture = 1;
    return 1;
}

/* The adapter reads the already sealed local role. Never copy a previous
 * sample, fabricate a target/FOV, or replace expected absence with VIEW. */
static inline int player_stock_view_wire_sample(const player_stock_view_wire_handoff *handoff,
        const keyboard_replay *replay, uint64_t generation,
        const player_stock_view_wire_sealed *sealed, linux_stock_view_sample *out,
        unsigned *present) {
    if (!handoff || !replay || !sealed || !out || !present || !handoff->admitted ||
        !handoff->ready || !replay->config.view_capture || generation != replay->tick ||
        sealed->generation != generation || sealed->slot != replay->config.own_slot ||
        sealed->present > 1 || generation < replay->config.first_capture ||
        generation > replay->config.last_capture ||
        handoff->ready(handoff->context, &replay->config, generation) != 1) return 0;
    unsigned required = generation >= handoff->first && generation <= handoff->last;
    if (sealed->present != required || sealed->view.bits > 3 || !isfinite(sealed->view.fov) ||
        (!required && (sealed->view.bits || stock_input_float_bits(sealed->view.fov) || sealed->view.target))) return 0;
    *out = sealed->view;
    *present = required;
    return 1;
}
#endif

#ifndef X4_PLAYER_STARTUP_PHASES_H
#define X4_PLAYER_STARTUP_PHASES_H

#include <stdint.h>
#include <string.h>

/* Named engine actions have their own epochs. Version zero is the retained
 * Linux diagnostic; version one is the common Windows/Linux startup. */
typedef struct {
    uint32_t version;
    uint64_t factory, seat_request, seat_preflight, take, capture, apply;
    uint64_t presentation_probe, presentation_arm, presentation_first;
} player_startup_phases;

static inline int player_startup_phases_valid(const player_startup_phases *p) {
    return p && p->factory >= 2 && p->factory <= p->seat_request &&
        p->seat_request < p->seat_preflight && p->seat_preflight < p->take && p->take <= p->apply && p->capture >= 3 &&
        p->capture <= UINT64_MAX - 2 && p->capture + 2 == p->apply &&
        p->apply <= UINT64_MAX - 4 && p->presentation_probe == p->apply + 2 &&
        p->presentation_arm == p->apply + 3 && p->presentation_first == p->apply + 4;
}

static inline int player_startup_is_presentation_probe_tick(const player_startup_phases *phase,
                                                             uint64_t completed) {
    return phase && completed == phase->presentation_probe;
}

static inline int player_startup_phases_parse(const char *value, player_startup_phases *out) {
    if (!out) return 0;
    if (!value) {
        *out = (player_startup_phases){0, 2, 2, 4, 5, 3, 5, 7, 8, 9};
    } else if (!strcmp(value, "v1-t40")) {
        *out = (player_startup_phases){1, 35, 35, 36, 37, 38, 40, 42, 43, 44};
    } else {
        return 0;
    }
    return player_startup_phases_valid(out);
}

#endif

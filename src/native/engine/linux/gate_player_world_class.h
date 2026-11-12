#ifndef X4_GATE_PLAYER_WORLD_CLASS_H
#define X4_GATE_PLAYER_WORLD_CLASS_H

#include <stdint.h>
#include <string.h>

/* Read-only census exception for the one retained diagnostic controller.
 * The caller supplies safely read backlinks and a separately pinned stock
 * table; no worker-thread or rendezvous state is inferred here. */
typedef struct {
    uintptr_t ship, controller, held_ship, owned_controller, route_ship, route_controller;
    uintptr_t ship_controller, controller_ship, observed_table, private_table;
    uintptr_t stock_table, expected_stock_table;
    unsigned ready, installed, left, active, held, failures, stock_pin;
    const uintptr_t *stock_words, *private_words;
    uintptr_t replacements[11];
} player_world_class_check;

static int player_world_class_matches(const player_world_class_check *c) {
    static const unsigned slots[11] = {13, 14, 17, 21, 30, 56, 57, 40, 41, 42, 43};
    if (!c || !c->ship || !c->controller || !c->stock_words || !c->private_words ||
        c->ready != 1 || c->installed != 1 || c->left != 1 || c->active != 1 ||
        c->held != 1 || c->failures || c->stock_pin != 1 ||
        c->ship != c->held_ship || c->ship != c->route_ship || c->ship != c->controller_ship ||
        c->controller != c->owned_controller || c->controller != c->route_controller ||
        c->controller != c->ship_controller || !c->private_table ||
        c->observed_table != c->private_table || !c->expected_stock_table ||
        c->stock_table != c->expected_stock_table)
        return 0;
    uintptr_t expected[81];
    memcpy(expected, c->stock_words, sizeof expected);
    for (unsigned i = 0; i < 11; ++i) {
        if (!c->replacements[i]) return 0;
        expected[2 + slots[i]] = c->replacements[i];
    }
    return memcmp(expected, c->private_words, sizeof expected) == 0;
}
#endif

#ifndef X4_LINUX_PLAYER_WEAPON_GROUPS_H
#define X4_LINUX_PLAYER_WEAPON_GROUPS_H

/* Engine-free bounded group reader. The caller authenticates the ship, registry
 * resolver and each weapon's class/ancestry. Output is staged until every read
 * succeeds; this does not hold objects alive or exclude concurrent writers. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PLAYER_WEAPON_MAX 16u
#define PLAYER_WEAPON_GROUP_MAX 64u
#define PLAYER_WEAPON_IDS_MAX 512u

typedef struct {
    uintptr_t object;
    uint64_t runtime;
} player_weapon_item;

typedef struct {
    int (*read)(void *context, uintptr_t address, void *output, size_t size);
    uintptr_t (*resolve)(void *context, uint64_t runtime);
    int (*weapon_under_ship)(void *context, uintptr_t weapon, uintptr_t ship);
    void *context;
} player_weapon_reader;

static inline int player_weapon_add(uintptr_t base, size_t offset, uintptr_t *output) {
    if (base > UINTPTR_MAX - offset) return 0;
    *output = base + offset;
    return 1;
}

/* Preserve native ID order. A missing registry entry is skipped; the resolver
 * can latch a stricter failure in its context. The ancestry predicate returns
 * negative on uncertain/unreadable, zero for a non-weapon, positive for a gun. */
static inline int player_weapon_collect(const player_weapon_reader *reader, uintptr_t ship,
        unsigned secondary, player_weapon_item output[PLAYER_WEAPON_MAX], unsigned *count) {
    if (!reader || !reader->read || !reader->resolve || !reader->weapon_under_ship ||
        !ship || secondary > 1 || !output || !count) return 0;

    uintptr_t address, groups[2], ids[2];
    int32_t selected_group;
    if (!player_weapon_add(ship, secondary ? 0x640 : 0x628, &address) ||
        !reader->read(reader->context, address, groups, sizeof groups) ||
        !player_weapon_add(ship, secondary ? 0x65c : 0x658, &address) ||
        !reader->read(reader->context, address, &selected_group, sizeof selected_group) ||
        groups[1] < groups[0] || (groups[1] - groups[0]) % 24 ||
        (groups[1] - groups[0]) / 24 > PLAYER_WEAPON_GROUP_MAX || selected_group < 0 ||
        (size_t)selected_group >= (groups[1] - groups[0]) / 24) return 0;

    if (!player_weapon_add(groups[0], (size_t)selected_group * 24, &address) ||
        !reader->read(reader->context, address, ids, sizeof ids) || ids[1] < ids[0] ||
        (ids[1] - ids[0]) % 8 || (ids[1] - ids[0]) / 8 > PLAYER_WEAPON_IDS_MAX) return 0;

    player_weapon_item staged[PLAYER_WEAPON_MAX] = {{0}};
    unsigned weapon_count = 0;
    for (uintptr_t entry = ids[0]; entry < ids[1]; entry += 8) {
        uint64_t runtime;
        if (!reader->read(reader->context, entry, &runtime, sizeof runtime)) return 0;
        if (!runtime) continue;
        uintptr_t weapon = reader->resolve(reader->context, runtime);
        if (!weapon) continue;
        int verdict = reader->weapon_under_ship(reader->context, weapon, ship);
        if (verdict < 0) return 0;
        if (!verdict) continue;
        if (weapon_count == PLAYER_WEAPON_MAX) return 0;
        staged[weapon_count++] = (player_weapon_item){weapon, runtime};
    }
    memcpy(output, staged, sizeof staged);
    *count = weapon_count;
    return 1;
}
#endif

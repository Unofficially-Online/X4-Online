#include <assert.h>
#include <string.h>
#include "player_weapon_groups.h"

static int read_memory(void *context, uintptr_t address, void *output, size_t size) {
    (void)context;
    if (!address || address == UINTPTR_MAX) return 0;
    memcpy(output, (void *)address, size);
    return 1;
}
static uintptr_t resolve(void *context, uint64_t runtime) {
    (void)context;
    return runtime == 1 ? 101 : runtime == 2 ? 102 : 0;
}
static int filter_weapon(void *context, uintptr_t object, uintptr_t ship) {
    (void)context;
    (void)ship;
    return object == 101 ? 1 : object == 102 ? 0 : -1;
}

int main(void) {
    unsigned char ship[0x660] = {0};
    uint64_t ids[] = {2, 1, 7};
    uintptr_t group[] = {(uintptr_t)ids, (uintptr_t)(ids + 3), (uintptr_t)(ids + 3)};
    uintptr_t groups[] = {(uintptr_t)group, (uintptr_t)(group + 3)};
    memcpy(ship + 0x628, groups, sizeof groups);
    player_weapon_reader reader = {read_memory, resolve, filter_weapon, NULL};
    player_weapon_item found[PLAYER_WEAPON_MAX] = {{0}};
    unsigned count = 99;
    assert(player_weapon_collect(&reader, (uintptr_t)ship, 0, found, &count));
    assert(count == 1 && found[0].runtime == 1 && found[0].object == 101);

    int32_t selected = -1;
    memcpy(ship + 0x658, &selected, sizeof selected);
    count = 99;
    assert(!player_weapon_collect(&reader, (uintptr_t)ship, 0, found, &count));
    assert(count == 99 && found[0].runtime == 1);
    selected = 0;
    memcpy(ship + 0x658, &selected, sizeof selected);
    ++group[1];
    assert(!player_weapon_collect(&reader, (uintptr_t)ship, 0, found, &count) && count == 99);
    --group[1];
    group[1] = group[0] + 513 * 8;
    assert(!player_weapon_collect(&reader, (uintptr_t)ship, 0, found, &count) && count == 99);
    group[1] = (uintptr_t)(ids + 3);
    groups[1] = groups[0] + 65 * 24;
    memcpy(ship + 0x628, groups, sizeof groups);
    assert(!player_weapon_collect(&reader, (uintptr_t)ship, 0, found, &count) && count == 99);
    groups[1] = (uintptr_t)(group + 3);
    memcpy(ship + 0x628, groups, sizeof groups);
    uint64_t too_many_guns[17];
    for (unsigned index = 0; index < 17; ++index) too_many_guns[index] = 1;
    group[0] = (uintptr_t)too_many_guns;
    group[1] = group[2] = (uintptr_t)(too_many_guns + 17);
    assert(!player_weapon_collect(&reader, (uintptr_t)ship, 0, found, &count) && count == 99);
    return 0;
}

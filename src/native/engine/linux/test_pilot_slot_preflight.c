#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pilot_slot_preflight.h"

typedef struct {
    uintptr_t universe_pointer;
    unsigned char universe[0x250];
    unsigned char first_player[0x80];
    unsigned char second_player[0x80];
    unsigned char macro[0x50];
    unsigned char alternate_macro[0x50];
    unsigned char ships[3][0x20];
    uint64_t runtime[2];
    uintptr_t objects[2];
    unsigned universe_reads;
    const char *change;
} fake_fixture;
static void put(void *base, size_t offset, const void *value, size_t size) {
    memcpy((unsigned char *)base + offset, value, size);
}
static int contains(uintptr_t address, size_t length, const void *base, size_t size) {
    uintptr_t start = (uintptr_t)base;
    return address >= start && address - start <= size && length <= size - (address - start);
}
static void mutate(fake_fixture *fixture) {
    uintptr_t pointer;
    if (!strcmp(fixture->change, "singleton")) {
        pointer = (uintptr_t)fixture->second_player;
        put(fixture->universe, 0x238, &pointer, sizeof(pointer));
    } else if (!strcmp(fixture->change, "vtable")) {
        pointer = 0x8888;
        put(fixture->first_player, 0, &pointer, sizeof(pointer));
    } else if (!strcmp(fixture->change, "macro")) {
        pointer = (uintptr_t)fixture->alternate_macro;
        put(fixture->first_player, 0x60, &pointer, sizeof(pointer));
    } else if (!strcmp(fixture->change, "refs")) {
        int32_t count = 3;
        put(fixture->first_player, 0x40, &count, sizeof(count));
    } else if (!strcmp(fixture->change, "dead_refs")) {
        int32_t count = -1;
        put(fixture->first_player, 0x40, &count, sizeof(count));
    } else if (!strcmp(fixture->change, "class")) {
        uint32_t class_id = 0x4d;
        put(fixture->macro, 0x44, &class_id, sizeof(class_id));
    } else if (!strcmp(fixture->change, "identity")) {
        fixture->runtime[1] = 303;
        fixture->objects[1] = (uintptr_t)fixture->ships[2];
    }
}
static int fake_read(void *context, uintptr_t address, void *out, size_t length) {
    fake_fixture *fixture = context;
    if (address == (uintptr_t)&fixture->universe_pointer &&
        ++fixture->universe_reads == 2) {
        mutate(fixture);
    }
    if (!contains(address, length, fixture, sizeof(*fixture))) {
        return 0;
    }
    memcpy(out, (const void *)address, length);
    return 1;
}
static int fake_resolve(void *context, uint64_t saved_id,
                        uint64_t *runtime_id, uintptr_t *object) {
    fake_fixture *fixture = context;
    if (saved_id != 11 && saved_id != 22) {
        return 0;
    }
    unsigned index = saved_id == 11 ? 0 : 1;
    *runtime_id = fixture->runtime[index];
    *object = fixture->objects[index];
    return 1;
}
static void setup(fake_fixture *fixture, const char *change) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->change = change;
    fixture->universe_pointer = (uintptr_t)fixture->universe;
    uintptr_t player = (uintptr_t)fixture->first_player;
    uintptr_t macro = (uintptr_t)fixture->macro;
    uintptr_t vtable = 0x7770;
    int32_t references = 2;
    uint32_t class_id = 0x4c;
    put(fixture->universe, 0x238, &player, sizeof(player));
    put(fixture->first_player, 0, &vtable, sizeof(vtable));
    put(fixture->first_player, 0x40, &references, sizeof(references));
    put(fixture->first_player, 0x60, &macro, sizeof(macro));
    memcpy(fixture->second_player, fixture->first_player, sizeof(fixture->first_player));
    put(fixture->macro, 0x44, &class_id, sizeof(class_id));
    put(fixture->alternate_macro, 0x44, &class_id, sizeof(class_id));
    fixture->runtime[0] = 101;
    fixture->runtime[1] = 202;
    fixture->objects[0] = (uintptr_t)fixture->ships[0];
    fixture->objects[1] = (uintptr_t)fixture->ships[1];
    uint64_t runtime_ids[3] = {101, 202, 303};
    for (unsigned i = 0; i < 3; ++i) {
        put(fixture->ships[i], 8, &runtime_ids[i], 8);
    }
}
static void test_case(const char *change, int alias_mode) {
    fake_fixture fixture;
    setup(&fixture, change);
    if (alias_mode == 1) {
        fixture.runtime[1] = fixture.runtime[0];
    }
    if (alias_mode == 2) {
        fixture.objects[1] = fixture.objects[0];
    }
    if (alias_mode == 3) {
        fixture.runtime[1] = fixture.runtime[0];
        fixture.objects[1] = fixture.objects[0];
    }
    uint64_t saved_ids[2] = {11, 22};
    pilot_preflight_io io = {&fixture, fake_read, fake_resolve};
    pilot_preflight_snapshot output;
    pilot_preflight_snapshot untouched;
    memset(&output, 0xa5, sizeof(output));
    untouched = output;
    pilot_preflight_result result = pilot_slot_preflight(
        &io, (uintptr_t)&fixture.universe_pointer, 0x7770, saved_ids, 1, &output);
    if (!strcmp(change, "none") && !alias_mode) {
        assert(result == PILOT_PREFLIGHT_OK);
        assert(output.player == (uintptr_t)fixture.first_player && output.references == 2);
        assert(output.ships[0].runtime == 101 && output.ships[1].runtime == 202);
    } else {
        assert(result != PILOT_PREFLIGHT_OK);
        assert(!memcmp(&output, &untouched, sizeof(output)));
    }
}
int main(void) {
    const char *changes[] = {
        "none", "singleton", "vtable", "macro", "refs", "dead_refs", "class", "identity"
    };
    for (unsigned i = 0; i < sizeof(changes) / sizeof(changes[0]); ++i) {
        test_case(changes[i], 0);
    }
    test_case("none", 1);
    test_case("none", 2);
    test_case("none", 3);
    fake_fixture fixture;
    setup(&fixture, "none");
    pilot_preflight_io io = {&fixture, fake_read, fake_resolve};
    pilot_preflight_snapshot output;
    uint64_t duplicate_saved_ids[2] = {11, 11};
    assert(pilot_slot_preflight(
        &io, (uintptr_t)&fixture.universe_pointer, 0x7770, duplicate_saved_ids, 1, &output
    ) == PILOT_PREFLIGHT_ARGUMENT);
    uint64_t saved_ids[2] = {11, 22};
    assert(pilot_slot_preflight(
        &io, (uintptr_t)&fixture.universe_pointer, 0x7770, saved_ids, 0, &output
    ) == PILOT_PREFLIGHT_ARGUMENT);
    assert(!fixture.universe_reads);
    puts("PASS: read-only fixture identities, aliases and changing player descriptors");
    return 0;
}

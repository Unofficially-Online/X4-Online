#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gate_player_body_observation.h"

typedef struct {
    unsigned char ship[0x270];
    unsigned char records[2][0x20];
    unsigned char xbodies[2][0x10];
    unsigned char scenes[2][0x28];
    unsigned char systems[2][0x148];
    unsigned char interfaces[2][0x10];
    unsigned char managers[2][0x18];
    uintptr_t body_arrays[2][4];
    unsigned char bodies[2][0x78];
    unsigned char motions[2][0x20];
    uintptr_t trigger;
    uintptr_t mutation;
    uintptr_t replacement;
    uintptr_t unreadable;
    int did_mutate;
} body_fixture;

static void write_pointer(void *base, size_t offset, const void *value) {
    uintptr_t address = (uintptr_t)value;
    memcpy((unsigned char *)base + offset, &address, sizeof address);
}
static void write_u32(void *base, size_t offset, uint32_t value) {
    memcpy((unsigned char *)base + offset, &value, sizeof value);
}
static void write_u64(void *base, size_t offset, uint64_t value) {
    memcpy((unsigned char *)base + offset, &value, sizeof value);
}
static int fixture_read(void *context, uintptr_t address, void *output, size_t length) {
    body_fixture *fixture = context;
    uintptr_t begin = (uintptr_t)fixture;
    if (address < begin || address - begin > sizeof *fixture ||
        length > sizeof *fixture - (address - begin) || address == fixture->unreadable) {
        return 0;
    }
    memcpy(output, (const void *)address, length);
    if (!fixture->did_mutate && address == fixture->trigger) {
        memcpy((void *)fixture->mutation, &fixture->replacement, sizeof fixture->replacement);
        fixture->did_mutate = 1;
    }
    return 1;
}
static void fixture_setup(body_fixture *fixture) {
    memset(fixture, 0, sizeof *fixture);
    write_pointer(fixture->ship, 0, fixture->ship);
    write_u64(fixture->ship, 8, 42);
    write_pointer(fixture->ship, 0x70, fixture->ship);
    for (unsigned index = 0; index < 2; ++index) {
        write_pointer(fixture->ship, 0x258 + index * 8, fixture->records[index]);
        write_pointer(fixture->records[index], 0, fixture->ship);
        write_pointer(fixture->records[index], 0x10, fixture->xbodies[index]);
        write_pointer(fixture->records[index], 0x18, fixture->scenes[index]);
        write_u32(fixture->xbodies[index], 8, index + 1);
        write_pointer(fixture->scenes[index], 0x20, fixture->systems[index]);
        write_pointer(fixture->systems[index], 0x140, fixture->interfaces[index]);
        write_pointer(fixture->interfaces[index], 8, fixture->managers[index]);
        write_u64(fixture->managers[index], 0, 4);
        write_pointer(fixture->managers[index], 0x10, fixture->body_arrays[index]);
        fixture->body_arrays[index][index + 1] = (uintptr_t)fixture->bodies[index];
        write_pointer(fixture->bodies[index], 0x48, fixture->motions[index]);
        write_pointer(fixture->bodies[index], 0x50, fixture->records[index]);
        write_u32(fixture->bodies[index], 0x70, index + 1);
        float position[3] = {1.0f + index, 2.0f, 3.0f};
        float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float linear[3] = {4.0f, 5.0f, 6.0f};
        float angular[3] = {7.0f, 8.0f, 9.0f};
        memcpy(fixture->bodies[index], position, sizeof position);
        memcpy(fixture->bodies[index] + 0x10, rotation, sizeof rotation);
        memcpy(fixture->motions[index], linear, sizeof linear);
        memcpy(fixture->motions[index] + 0x10, angular, sizeof angular);
    }
}
static int observe(body_fixture *fixture, player_body_observation *output) {
    return player_body_observe(fixture_read, fixture, (uintptr_t)fixture->ship, output);
}
int main(void) {
    body_fixture fixture;
    player_body_observation output;
    fixture_setup(&fixture);
    assert(observe(&fixture, &output));
    assert(output.ship_runtime_id == 42 && output.records[0].links.present && output.records[1].links.present);
    assert(output.records[0].links.body_id == 1 && output.records[1].links.body_id == 2);
    assert(output.records[0].position[0] == 1.0f && output.records[1].position[0] == 2.0f &&
           output.records[0].rotation[3] == 1.0f && output.records[1].linear_velocity[2] == 6.0f &&
           output.records[1].angular_velocity[1] == 8.0f);

    fixture_setup(&fixture);
    write_pointer(fixture.ship, 0x260, NULL);
    assert(observe(&fixture, &output));
    assert(output.records[0].links.present && !output.records[1].links.present);
    write_pointer(fixture.ship, 0x258, NULL);
    assert(observe(&fixture, &output));
    assert(!output.records[0].links.present && !output.records[1].links.present);

    fixture_setup(&fixture);
    write_pointer(fixture.bodies[0], 0x48, NULL);
    assert(observe(&fixture, &output) && output.records[0].links.present && !output.records[0].motion_present);

    fixture_setup(&fixture);
    write_u32(fixture.bodies[0], 0x70, 3);
    memset(&output, 0x5a, sizeof output);
    player_body_observation untouched = output;
    assert(!observe(&fixture, &output));
    assert(!memcmp(&output, &untouched, sizeof output));

    fixture_setup(&fixture);
    write_pointer(fixture.records[0], 0, fixture.records[1]);
    assert(!observe(&fixture, &output));

    fixture_setup(&fixture);
    float invalid = NAN;
    memcpy(fixture.bodies[0] + 0x10, &invalid, sizeof invalid);
    assert(!observe(&fixture, &output));

    fixture_setup(&fixture);
    invalid = INFINITY;
    memcpy(fixture.motions[1] + 0x10, &invalid, sizeof invalid);
    assert(!observe(&fixture, &output));

    fixture_setup(&fixture);
    fixture.unreadable = (uintptr_t)(fixture.bodies[0] + 0x10);
    assert(!observe(&fixture, &output));

    fixture_setup(&fixture);
    fixture.trigger = (uintptr_t)fixture.bodies[0];
    fixture.mutation = (uintptr_t)(fixture.records[0] + 0x18);
    fixture.replacement = (uintptr_t)fixture.scenes[1];
    assert(!observe(&fixture, &output) && fixture.did_mutate);

    fixture_setup(&fixture);
    fixture.trigger = (uintptr_t)fixture.bodies[0];
    fixture.mutation = (uintptr_t)(fixture.ship + 0x260);
    fixture.replacement = 0;
    assert(!observe(&fixture, &output) && fixture.did_mutate);

    fixture_setup(&fixture);
    fixture.trigger = (uintptr_t)fixture.bodies[0];
    fixture.mutation = (uintptr_t)fixture.records[0];
    fixture.replacement = (uintptr_t)fixture.records[1];
    assert(!observe(&fixture, &output) && fixture.did_mutate);

    fixture_setup(&fixture);
    fixture.trigger = (uintptr_t)fixture.bodies[0];
    fixture.mutation = (uintptr_t)(fixture.bodies[0] + 0x70);
    fixture.replacement = 3;
    assert(!observe(&fixture, &output) && fixture.did_mutate);

    fixture_setup(&fixture);
    fixture.trigger = (uintptr_t)fixture.bodies[0];
    fixture.mutation = (uintptr_t)(fixture.ship + 0x70);
    fixture.replacement = (uintptr_t)fixture.records[1];
    assert(!observe(&fixture, &output) && fixture.did_mutate);
    puts("target Jolt bodies: snapshots, missing data and link rechecks PASS");
}

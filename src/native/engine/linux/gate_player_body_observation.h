#ifndef X4_GATE_PLAYER_BODY_OBSERVATION_H
#define X4_GATE_PLAYER_BODY_OBSERVATION_H

/* Read-only snapshots for the disposable player experiment, never installed
 * automatically. Layout is from gate_battle.h:lxb_body_state for the pinned
 * X4 image SHA-256 113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20.
 * The caller authenticates that image and retains the ship and scene objects.
 * A stable double read detects observed link changes, not an ABA, concurrent
 * motion writes, or a torn sample. This helper neither locks Jolt nor proves
 * motion, steering causation, or safety of concurrent native readers. */
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define PLAYER_BODY_RECORD_COUNT 2u
#define PLAYER_BODY_FIRST_SLOT 0x258u
#define PLAYER_BODY_IMAGE_SHA "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20"

typedef int (*player_body_read_fn)(void *context, uintptr_t address, void *output, size_t length);

typedef struct {
    uintptr_t record;
    uintptr_t xbody;
    uintptr_t scene;
    uintptr_t system;
    uintptr_t interface;
    uintptr_t manager;
    uintptr_t body_array;
    uintptr_t body;
    uintptr_t motion;
    uint64_t body_count;
    uint32_t body_id;
    unsigned present;
} player_body_links;

typedef struct {
    player_body_links links;
    unsigned motion_present;
    float position[3];
    float rotation[4];
    float linear_velocity[3];
    float angular_velocity[3];
} player_body_record;

typedef struct {
    uintptr_t ship;
    uintptr_t ship_vtable;
    uintptr_t ship_parent;
    uint64_t ship_runtime_id;
    player_body_record records[PLAYER_BODY_RECORD_COUNT];
} player_body_observation;

static int player_body_read_offset(player_body_read_fn read, void *context, uintptr_t base,
                                   size_t offset, void *output, size_t length) {
    if (!base || offset > UINTPTR_MAX - base || length > UINTPTR_MAX - (base + offset)) {
        return 0;
    }
    return read(context, base + offset, output, length);
}

static int player_body_read_identity(player_body_read_fn read, void *context, uintptr_t ship,
                                     uintptr_t *vtable, uintptr_t *parent, uint64_t *runtime_id) {
    return player_body_read_offset(read, context, ship, 0, vtable, sizeof *vtable) && *vtable &&
           player_body_read_offset(read, context, ship, 8, runtime_id, sizeof *runtime_id) && *runtime_id &&
           player_body_read_offset(read, context, ship, 0x70, parent, sizeof *parent);
}

static int player_body_read_links(player_body_read_fn read, void *context, uintptr_t ship, unsigned index,
                                  player_body_links *links) {
    memset(links, 0, sizeof *links);
    if (index >= PLAYER_BODY_RECORD_COUNT ||
        !player_body_read_offset(read, context, ship, PLAYER_BODY_FIRST_SLOT + index * 8u,
                                 &links->record, sizeof links->record)) {
        return 0;
    }
    if (!links->record) {
        return 1; /* Explicitly missing; never synthesize a zero-valued pose. */
    }
    uintptr_t owner = 0, user_data = 0;
    uint32_t actual_id = 0;
    if (!player_body_read_offset(read, context, links->record, 0, &owner, sizeof owner) || owner != ship ||
        !player_body_read_offset(read, context, links->record, 0x10, &links->xbody, sizeof links->xbody) ||
        !links->xbody ||
        !player_body_read_offset(read, context, links->xbody, 8, &links->body_id, sizeof links->body_id) ||
        links->body_id == UINT32_MAX ||
        !player_body_read_offset(read, context, links->record, 0x18, &links->scene, sizeof links->scene) ||
        !links->scene ||
        !player_body_read_offset(read, context, links->scene, 0x20, &links->system, sizeof links->system) ||
        !links->system ||
        !player_body_read_offset(read, context, links->system, 0x140, &links->interface,
                                 sizeof links->interface) || !links->interface ||
        !player_body_read_offset(read, context, links->interface, 8, &links->manager, sizeof links->manager) ||
        !links->manager ||
        !player_body_read_offset(read, context, links->manager, 0, &links->body_count, sizeof links->body_count) ||
        links->body_count > 0x800000u || (links->body_id & 0x7fffffu) >= links->body_count ||
        !player_body_read_offset(read, context, links->manager, 0x10, &links->body_array,
                                 sizeof links->body_array) || !links->body_array ||
        !player_body_read_offset(read, context, links->body_array, (size_t)(links->body_id & 0x7fffffu) * 8u,
                                 &links->body, sizeof links->body) || !links->body || (links->body & 1u) ||
        !player_body_read_offset(read, context, links->body, 0x70, &actual_id, sizeof actual_id) ||
        actual_id != links->body_id ||
        !player_body_read_offset(read, context, links->body, 0x50, &user_data, sizeof user_data) ||
        user_data != links->record ||
        !player_body_read_offset(read, context, links->body, 0x48, &links->motion, sizeof links->motion)) {
        return 0;
    }
    links->present = 1;
    return 1;
}

static int player_body_finite(const float *values, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        if (!isfinite(values[index])) {
            return 0;
        }
    }
    return 1;
}

/* A successful observation may contain zero, one or two present records.
 * Missing motion properties are explicit and do not imply zero velocity. The
 * caller must compare scene and identities across observations before using
 * values; it must separately establish the needed physics quiescence. */
static int player_body_observe(player_body_read_fn read, void *context, uintptr_t ship,
                               player_body_observation *output) {
    if (!read || !ship || !output) {
        return 0;
    }
    player_body_observation sample = {0};
    sample.ship = ship;
    if (!player_body_read_identity(read, context, ship, &sample.ship_vtable, &sample.ship_parent,
                                   &sample.ship_runtime_id)) {
        return 0;
    }
    for (unsigned index = 0; index < PLAYER_BODY_RECORD_COUNT; ++index) {
        if (!player_body_read_links(read, context, ship, index, &sample.records[index].links)) {
            return 0;
        }
    }
    for (unsigned index = 0; index < PLAYER_BODY_RECORD_COUNT; ++index) {
        player_body_record *record = &sample.records[index];
        if (!record->links.present) {
            continue;
        }
        if (!player_body_read_offset(read, context, record->links.body, 0, record->position,
                                     sizeof record->position) ||
            !player_body_read_offset(read, context, record->links.body, 0x10, record->rotation,
                                     sizeof record->rotation) ||
            !player_body_finite(record->position, 3) || !player_body_finite(record->rotation, 4)) {
            return 0;
        }
        if (record->links.motion) {
            if (!player_body_read_offset(read, context, record->links.motion, 0, record->linear_velocity,
                                         sizeof record->linear_velocity) ||
                !player_body_read_offset(read, context, record->links.motion, 0x10, record->angular_velocity,
                                         sizeof record->angular_velocity) ||
                !player_body_finite(record->linear_velocity, 3) ||
                !player_body_finite(record->angular_velocity, 3)) {
                return 0;
            }
            record->motion_present = 1;
        }
    }
    uintptr_t vtable = 0, parent = 0;
    uint64_t runtime_id = 0;
    if (!player_body_read_identity(read, context, ship, &vtable, &parent, &runtime_id) ||
        vtable != sample.ship_vtable || parent != sample.ship_parent || runtime_id != sample.ship_runtime_id) {
        return 0;
    }
    for (unsigned index = 0; index < PLAYER_BODY_RECORD_COUNT; ++index) {
        player_body_links after = {0};
        if (!player_body_read_links(read, context, ship, index, &after) ||
            memcmp(&after, &sample.records[index].links, sizeof after)) {
            return 0;
        }
    }
    *output = sample;
    return 1;
}
#endif

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { const char *name; unsigned va, length; const char *hash; } lx_site;
typedef struct {
    unsigned char prefix[8];
    uint64_t runtime;
    unsigned char padding[0x30];
    uint32_t count;
    uint32_t marker;
} fake_zone;

static unsigned char image[0x4000000];
static fake_zone zone;
static int allowed, boundary_valid, mapping_valid, pins_valid;
static unsigned current_thread, failed_address, readback_failure, failed_read_address;
static unsigned expire_after_cas;
static unsigned char *foreign_target;
static unsigned assignments;

static unsigned GetCurrentThreadId(void) { return current_thread; }
static int factory_probe_joined_boundary(void) { return boundary_valid; }
static int player_bank_census_pins(void *context) { (void)context; return pins_valid; }
static int site_ok(const lx_site *site) { return pins_valid && site->length && site->hash[0]; }
static int player_bank_census_mapped(void *context, unsigned address, unsigned length) {
    (void)context;
    if (address == failed_address && length == 1) {
        failed_address = 0;
        if (foreign_target) {
            *foreign_target = 0x7f;
        }
        return 0;
    }
    return mapping_valid && address < sizeof(image) && length <= sizeof(image) - address;
}
static uint64_t world_identity_lookup(uint64_t runtime) { return runtime == zone.runtime ? 73 : 0; }
static int read_local(const void *source, void *destination, unsigned length) {
    if (readback_failure && source == image + 0x3d8da60 && length == 1) {
        readback_failure = 0;
        return 0;
    }
    if (failed_read_address && source == image + failed_read_address) {
        failed_read_address = 0;
        return 0;
    }
    memcpy(destination, source, length);
    if (expire_after_cas && source == image + 0x3d8da60 && length == 1) {
        expire_after_cas = 0;
        allowed = 0;
    }
    return 1;
}
static void **native_assign(void **destination, void *const *source) {
    ++assignments;
    fake_zone *incoming = *source;
    fake_zone *old = *destination;
    if (incoming) { ++incoming->count; }
    *destination = incoming;
    if (old) { --old->count; }
    return destination;
}
static void *fake_address(unsigned address) {
    if (address == 0xc61960) { return (void *)(uintptr_t)native_assign; }
    assert(address < sizeof(image));
    return image + address;
}
#define LX(address) fake_address(address)
#include "gate_player_zone_scope.h"
#include "gate_player_control_bank_scope.h"

static int admit(void *context, pcb_phase phase) {
    (void)context;
    (void)phase;
    return allowed;
}
static void setup(void) {
    memset(image, 0, sizeof(image));
    memset(&zone, 0, sizeof(zone));
    memset(&player_zone_resident, 0, sizeof(player_zone_resident));
    memset(&player_bank_resident, 0, sizeof(player_bank_resident));
    current_thread = 17;
    allowed = boundary_valid = mapping_valid = pins_valid = 1;
    failed_address = failed_read_address = readback_failure = assignments = 0;
    expire_after_cas = 0;
    foreign_target = NULL;
    zone.runtime = 42;
    zone.count = 1;
    zone.marker = 2;
    void *live = &zone;
    memcpy(image + PLAYER_ZONE_GLOBAL_VA, &live, sizeof(live));
    for (unsigned region = 0; region < PCB_REGION_COUNT; ++region) {
        for (unsigned index = 0; index < pcb_regions[region].length; ++index) {
            image[pcb_regions[region].va + index] = (unsigned char)(index * 13u + region);
        }
    }
    image[0x3d8da60] = 0;
    image[0x3d8daa9] = 0;
}
static void initialize(void) {
    player_bank_policy policy = {NULL, admit, 1, 1};
    assert(player_bank_initialize(&policy));
    assert(zone.count == 2);
}
static void exact_copy_and_restore(void) {
    setup();
    unsigned char before[PCB_VALUE_BYTES];
    for (unsigned region = 0; region < PCB_REGION_COUNT; ++region) {
        memcpy(before + pcb_regions[region].offset, image + pcb_regions[region].va,
               pcb_regions[region].length);
    }
    initialize();
    for (unsigned index = 0; index < PCB_VALUE_BYTES; ++index) {
        if (index != 0xc0 && index != 0x109) {
            assert(player_bank_resident.slot.bytes[index] == before[index]);
        }
    }
    assert(player_bank_enter());
    assert(image[0x3d8da60] == 1 && image[0x3d8daa9] == 1);
    assert(player_bank_exit());
    for (unsigned region = 0; region < PCB_REGION_COUNT; ++region) {
        assert(!memcmp(before + pcb_regions[region].offset, image + pcb_regions[region].va,
                       pcb_regions[region].length));
    }
    assert(player_bank_close() && zone.count == 1);
}
static void boundary_and_identity(void) {
    setup();
    initialize();
    current_thread = 18;
    assert(!player_bank_enter());
    current_thread = 17;
    allowed = 0;
    assert(!player_bank_enter());
    allowed = 1;
    boundary_valid = 0;
    assert(!player_bank_enter());
    boundary_valid = 1;
    zone.runtime = 43;
    assert(!player_bank_enter());
    zone.runtime = 42;
    assert(player_bank_close());
}
static void failed_and_foreign_writes(void) {
    setup();
    initialize();
    failed_address = 0x3d8daa9;
    assert(!player_bank_enter());
    assert(player_bank_resident.bank.phase == PCB_IDLE);
    assert(image[0x3d8da60] == 0);
    assert(player_bank_close());

    setup();
    initialize();
    failed_address = 0x3d8daa9;
    foreign_target = image + 0x3d8da60;
    assert(!player_bank_enter());
    assert(player_bank_resident.bank.phase == PCB_RESTORING);
    assert(image[0x3d8da60] == 0x7f);
    assert(!player_bank_restore() && !player_bank_close());
    /* The fixture ends like a discarded disposable process. */
}
static void readback_and_pending_zone(void) {
    setup();
    initialize();
    readback_failure = 1;
    assert(!player_bank_enter());
    assert(player_bank_resident.bank.phase == PCB_IDLE);
    assert(player_bank_close());

    setup();
    initialize();
    assert(player_bank_enter());
    player_zone_resident.effect_pending = 1;
    assert(!player_bank_exit());
    assert(player_bank_resident.bank.phase == PCB_ACTIVE);
    assert(!player_bank_close());
    /* Pending zone state remains resident until process discard. */
}
static void initial_failure_and_lost_admission(void) {
    setup();
    player_bank_policy policy = {NULL, admit, 1, 1};
    mapping_valid = 0;
    assert(!player_bank_initialize(&policy));
    assert(!player_bank_resident.started);

    setup();
    failed_read_address = 0x3d8d9a0;
    assert(!player_bank_initialize(&policy));
    assert(!player_bank_enter());
    assert(player_bank_close());
    assert(assignments == 0 && zone.count == 1);

    setup();
    initialize();
    expire_after_cas = 1;
    assert(!player_bank_enter());
    assert(player_bank_resident.bank.phase == PCB_RESTORING);
    assert(!player_bank_close());
    allowed = 1;
    assert(player_bank_restore());
    assert(player_bank_close());
}
static void guards_and_handles(void) {
    setup();
    initialize();
    image[0x3ee9b40] = 1;
    assert(!player_bank_enter());
    image[0x3ee9b40] = 0;
    image[0x3e5b299] = 1;
    assert(!player_bank_enter());
    image[0x3e5b299] = 0;
    image[0x3d8e254] = 1;
    assert(!player_bank_enter());
    image[0x3d8e254] = 0;
    uintptr_t handle = 123;
    memcpy(image + 0x3ed1140, &handle, sizeof(handle));
    assert(!player_bank_enter());
    memset(image + 0x3ed1140, 0, sizeof(handle));
    assert(player_bank_enter());
    memcpy(image + 0x3ee2dc0, &handle, sizeof(handle));
    assert(!player_bank_exit() && player_bank_resident.bank.phase == PCB_ACTIVE);
    assert(!player_bank_close());
    memset(image + 0x3ee2dc0, 0, sizeof(handle));
    assert(player_bank_exit());
    assert(player_bank_close());
}
int main(void) {
    (void)player_zone_initialize;
    exact_copy_and_restore();
    boundary_and_identity();
    failed_and_foreign_writes();
    readback_and_pending_zone();
    initial_failure_and_lost_admission();
    guards_and_handles();
    puts("6 bank adapter scenario groups passed");
}

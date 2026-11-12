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
static unsigned char *mutate_after_cas;
static unsigned fake_identity_changed, identity_change_after_cas, fake_native_unknown;
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
    if (identity_change_after_cas && source == image + 0x3d8daa9 && length == 1) {
        fake_identity_changed = 1;
        identity_change_after_cas = 0;
    }
    if (mutate_after_cas && source == image + 0x3d8daa9 && length == 1) {
        *mutate_after_cas ^= 1;
        mutate_after_cas = NULL;
    }
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
    if (fake_native_unknown) { ++zone.count; }
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

static void setup(void) {
    memset(image, 0, sizeof(image));
    memset(&zone, 0, sizeof(zone));
    memset(&player_zone_resident, 0, sizeof(player_zone_resident));
    memset(&player_bank_resident, 0, sizeof(player_bank_resident));
    current_thread = 17;
    allowed = boundary_valid = mapping_valid = pins_valid = 1;
    failed_address = failed_read_address = readback_failure = assignments = 0;
    expire_after_cas = 0;
    mutate_after_cas = NULL;
    fake_native_unknown = identity_change_after_cas = 0;
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

/* Engine-free canonical identity/manager fixtures. The real translation unit
 * uses the pinned stock collector and native read-only getters instead. */
typedef struct {
    int ready;
    const char *reason;
    uintptr_t input_owner;
    uint64_t identity;
    unsigned char requested_space;
    unsigned char enabled;
} stock_keyboard_identity_sample;
typedef struct {
    int (*pins)(void);
    int (*read)(uintptr_t, void *, size_t);
    uintptr_t (*address)(unsigned);
    uintptr_t (*getter)(unsigned);
    uint64_t (*saved)(uint64_t);
    uintptr_t observer_owner;
} stock_keyboard_identity_ops;
static struct { uintptr_t owner; } sko_session;
static struct { uint64_t issued; int active; } stats;
static _Alignas(16) unsigned char manager[0x4ca0];
static _Alignas(16) unsigned char nodes[3][0x30];
static int stock_keyboard_identity_pins(void) { return pins_valid; }
static int stock_keyboard_identity_read(uintptr_t address, void *output, size_t length) {
    return read_local((void *)address, output, (unsigned)length);
}
static uintptr_t stock_keyboard_identity_address(unsigned address) { return (uintptr_t)LX(address); }
static uintptr_t stock_keyboard_identity_getter(unsigned address) { (void)address; return 1; }
static int stock_keyboard_identity_collect_with(stock_keyboard_identity_sample *sample,
                                                 const stock_keyboard_identity_ops *operations) {
    memset(sample, 0, sizeof(*sample));
    if (!operations->pins()) { return 0; }
    sample->input_owner = (uintptr_t)manager - 0x10;
    sample->identity = fake_identity_changed ? 99 : 42;
    sample->enabled = image[0x3d8da60];
    sample->requested_space = image[0x3d8daa9];
    sample->ready = sample->requested_space == 1;
    sample->reason = sample->ready ? "ready" : "space_context";
    return 1;
}
static int stock_keyboard_identity_collect(stock_keyboard_identity_sample *sample) {
    const stock_keyboard_identity_ops operations = {
        stock_keyboard_identity_pins, stock_keyboard_identity_read, stock_keyboard_identity_address,
        stock_keyboard_identity_getter, world_identity_lookup, sko_session.owner};
    return stock_keyboard_identity_collect_with(sample, &operations);
}
typedef struct { double number; } lua_State;
static char lua_output[1400];
static int stock_observer_worker_boundary(void) { return boundary_valid; }
static void p_lua_pushnumber(lua_State *state, double number) { state->number = number; }
static void p_lua_pushnil(lua_State *state) { (void)state; lua_output[0] = 0; }
static void p_lua_pushlstring(lua_State *state, const char *value, size_t length) {
    (void)state;
    assert(length < sizeof(lua_output));
    memcpy(lua_output, value, length);
    lua_output[length] = 0;
}
#include "gate_player_bank_roundtrip.h"

static void set_u64(unsigned char *location, uintptr_t value) {
    memcpy(location, &value, sizeof(value));
}
static void set_u32(unsigned char *location, uint32_t value) {
    memcpy(location, &value, sizeof(value));
}
static void initialize_tree(void) {
    memset(manager, 0, sizeof(manager));
    memset(nodes, 0, sizeof(nodes));
    uintptr_t header = (uintptr_t)manager + 0x4c50;
    set_u64(manager + 0x4c58, (uintptr_t)nodes[1]);
    set_u64(manager + 0x4c70, (uintptr_t)nodes[0]);
    set_u64(manager + 0x4c78, 3);
    set_u64(manager + 0x4c80, 3);
    set_u64(nodes[1], header);
    set_u64(nodes[1] + 8, (uintptr_t)nodes[0]);
    set_u64(nodes[1] + 16, (uintptr_t)nodes[2]);
    set_u32(nodes[1] + 24, 1);
    set_u64(nodes[0], (uintptr_t)nodes[1]);
    set_u64(nodes[2], (uintptr_t)nodes[1]);
    for (unsigned index = 0; index < 3; ++index) {
        set_u32(nodes[index] + 0x1c, index == 0 ? 8 : index == 1 ? 15 : 16);
        set_u32(nodes[index] + 0x20, 6);
        nodes[index][0x28] = 1;
    }
}
static void setup_probe(void) {
    setup();
    memset(&bank_roundtrip_resident, 0, sizeof(bank_roundtrip_resident));
    initialize_tree();
    fake_identity_changed = 0;
    image[0x3d8da60] = 1;
    image[0x3d8daa9] = 1;
    sko_session.owner = (uintptr_t)manager - 0x10;
    static const unsigned slots[][2] = {
        {0x3b61480, 0x136c820}, {0x3b7c258, 0xd586b0},
        {0x3b7c2d8, 0xa863a0}, {0x3d22938, 0xa389f0},
    };
    for (unsigned index = 0; index < sizeof(slots) / sizeof(slots[0]); ++index) {
        uintptr_t target = (uintptr_t)LX(slots[index][1]);
        memcpy(LX(slots[index][0]), &target, sizeof(target));
    }
    stats.issued = 2;
    stats.active = 1;
    lua_State state = {1};
    assert(lua_player_control_bank_probe_request(&state) == 1);
    assert(state.number == 0);
    assert(!bank_roundtrip_resident.requested);
    assert(bank_roundtrip_request());
    assert(bank_roundtrip_resident.requested);
    assert(player_control_bank_probe_clean());
    stats.issued = 5;
}
static void successful_roundtrip(void) {
    setup_probe();
    assert(player_control_bank_probe_after_join());
    assert(bank_roundtrip_resident.complete && bank_roundtrip_resident.cleanup_complete);
    assert(bank_roundtrip_resident.actual_space == 1);
    assert(!strcmp(bank_roundtrip_resident.actual_identity_reason, "ready"));
    assert(image[0x3d8daa9] == 1 && zone.count == 1);
    assert(player_control_bank_probe_clean());
    lua_State state = {0};
    assert(lua_player_control_bank_probe_status(&state) == 1);
    assert(strstr(lua_output, "canonical_only=1"));
    assert(strstr(lua_output, "temporary_requested_space=0"));
}
static void failures_are_not_clean(void) {
    setup_probe();
    nodes[0][0x20] = 0;
    assert(!player_control_bank_probe_after_join());
    assert(!player_control_bank_probe_clean());
    assert(bank_roundtrip_resident.reason);

    setup_probe();
    failed_address = 0x3d8daa9;
    assert(!player_control_bank_probe_after_join());
    assert(!player_control_bank_probe_clean());
    assert(bank_roundtrip_resident.reason);
}
static void changed_tree_remains_pending(void) {
    setup_probe();
    mutate_after_cas = nodes[0] + 0x2b;
    assert(!player_control_bank_probe_after_join());
    assert(!strcmp(bank_roundtrip_resident.reason, "tree_changed"));
    assert(player_bank_resident.bank.phase == PCB_RESTORING);
    assert(!player_control_bank_probe_clean());
    nodes[0][0x2b] ^= 1;
    assert(player_control_bank_probe_restore());
    assert(bank_roundtrip_resident.cleanup_complete);
    assert(!player_control_bank_probe_clean());
    assert(!strcmp(bank_roundtrip_resident.reason, "tree_changed"));
}
static void lost_identity_and_unknown_zone(void) {
    setup_probe();
    identity_change_after_cas = 1;
    assert(!player_control_bank_probe_after_join());
    assert(!strcmp(bank_roundtrip_resident.reason, "identity_changed"));
    assert(player_bank_resident.bank.phase == PCB_RESTORING);
    assert(!player_control_bank_probe_clean());
    fake_identity_changed = 0;
    assert(player_control_bank_probe_restore());
    assert(!player_control_bank_probe_clean());

    setup_probe();
    fake_native_unknown = 1;
    assert(!player_control_bank_probe_after_join());
    assert(player_zone_resident.effect_pending);
    assert(player_zone_resident.ledger.phase == PZ_POISONED);
    assert(!player_control_bank_probe_restore());
    assert(!player_control_bank_probe_clean());
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--fatal")) {
        setup_probe();
        bank_roundtrip_resident.attempted = 1;
        bank_roundtrip_resident.captured_tick = 5;
        bank_roundtrip_resident.reason = "unknown_zone_effect";
        player_bank_resident.bank.phase = PCB_RESTORING;
        player_bank_resident.bank.pending[3] = PCB_UNCERTAIN;
        player_zone_resident.ledger.phase = PZ_POISONED;
        player_zone_resident.effect_pending = 1;
        player_zone_resident.slot_owned = 1;
        player_control_bank_probe_fatal();
    }
    successful_roundtrip();
    failures_are_not_clean();
    assert(player_control_bank_probe_restore());
    assert(!player_control_bank_probe_clean());
    changed_tree_remains_pending();
    lost_identity_and_unknown_zone();
    puts("4 canonical roundtrip scenario groups passed");
}

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "player_stock_view_state.h"
static uintptr_t registry_global;
static uintptr_t test_address(uintptr_t address) {
    return address == 0x3daa860 ? (uintptr_t)&registry_global : address;
}
#define LX(address) test_address(address)
#define SEAT_PREFLIGHT_SHIP_VTABLE 0x3d21748
static struct { uint64_t issued; } stats;
static struct { uintptr_t ship; } factory_probe_identity;
static uintptr_t lifecycle_discard_ship;
static uintptr_t stock_view_gate_ships[2], stock_view_gate_controllers[2];
static player_stock_view_state stock_view_gate_state;
static int owner_valid;
static int read_local(void *address, void *out, unsigned size) { memcpy(out, address, size); return 1; }
static int stock_view_gate_owner(uintptr_t controller, unsigned *slot) {
    if (!owner_valid || stats.issued != stock_view_gate_state.generation) return 0;
    *slot = controller == stock_view_gate_controllers[0] ? 1 : controller == stock_view_gate_controllers[1] ? 2 : 0;
    return 1;
}
#include "gate_player_stock_view_component_policy.h"
typedef struct { uintptr_t class_pointer; uint64_t runtime; unsigned char padding[0x60]; uintptr_t parent; } component;
int main(void) {
    stats.issued = 42; owner_valid = 1;
    stock_view_gate_ships[0] = factory_probe_identity.ship = 0x1000;
    stock_view_gate_ships[1] = lifecycle_discard_ship = 0x2000;
    stock_view_gate_controllers[0] = 0x3000; stock_view_gate_controllers[1] = 0x4000;
    stock_view_gate_state = (player_stock_view_state){.generation = 42, .saved = {11, 22}, .initialized = 1,
        .slots = {{.cockpit = 1, .floating = 0}, {.cockpit = 0, .floating = 1}}};
    component object = {0x3c6a440, 1, {0}, 0x2000};
    unsigned char result = 0;
    assert(stock_view_component_gate_policy(0, (uintptr_t)&object, 0xa5, &result) && result == 1);
    object.class_pointer = 0x3ccad80;
    uintptr_t result_pointer = 0;
    assert(stock_view_precompare_gate_policy((uintptr_t)&object, 0x2000, 0x1000, &result_pointer) &&
           result_pointer == 0x2000);
    assert(!stock_view_precompare_gate_policy((uintptr_t)&object, 0x1000, 0x1000, &result_pointer));
    assert(!stock_view_precompare_gate_policy((uintptr_t)&object, 0x2000, 0x9000, &result_pointer));
    owner_valid = 0;
    assert(!stock_view_precompare_gate_policy((uintptr_t)&object, 0x2000, 0x1000, &result_pointer));
    object.class_pointer = 0x3c6a440;
    assert(!stock_view_component_gate_policy(0, (uintptr_t)&object, 0xa5, &result));
    unsigned char registry[0x68] = {0}, block[32] = {0};
    uintptr_t block_address = (uintptr_t)block;
    registry_global = (uintptr_t)registry;
    memcpy(registry + 0x60, &block_address, 8);
    component foreign_ship = {SEAT_PREFLIGHT_SHIP_VTABLE, 2, {0}, 0};
    uintptr_t component_address = (uintptr_t)&object, ship_address = (uintptr_t)&foreign_ship;
    memcpy(block, &component_address, 8);
    memcpy(block + 8, &ship_address, 8);
    uint32_t flags = (5u << 16) | (5u << 20);
    memcpy(block + 28, &flags, 4);
    object.parent = ship_address;
    assert(stock_view_component_gate_policy(0, component_address, 0xa5, &result) && result == 0xa5);
    object.class_pointer = 0x3ccad80;
    assert(stock_view_precompare_gate_policy(component_address, ship_address, 0xabcdef, &result_pointer) &&
           result_pointer == 0xabcdef);
    assert(!stock_view_precompare_gate_policy(component_address, 0x2000, 0xabcdef, &result_pointer));
    object.class_pointer = 0x3c6a440;
    foreign_ship.class_pointer = 0xdead;
    assert(!stock_view_component_gate_policy(0, component_address, 0xa5, &result));
    foreign_ship.class_pointer = SEAT_PREFLIGHT_SHIP_VTABLE; foreign_ship.runtime = 0;
    assert(!stock_view_component_gate_policy(0, component_address, 0xa5, &result));
    foreign_ship.runtime = 2; flags = 0;
    memcpy(block + 28, &flags, 4);
    assert(!stock_view_component_gate_policy(0, component_address, 0xa5, &result));
    component intermediate = {0x3c6a440, 66, {0}, 0x2000};
    object.parent = (uintptr_t)&intermediate;
    assert(!stock_view_component_gate_policy(0, (uintptr_t)&object, 0xa5, &result));
    object.parent = 0x2000; owner_valid = 1; stats.issued = 43;
    assert(!stock_view_component_gate_policy(0, (uintptr_t)&object, 0xa5, &result));
    object.class_pointer = 0x3ccad80;
    assert(!stock_view_precompare_gate_policy((uintptr_t)&object, 0x2000, 0x1000, &result_pointer));
    object.class_pointer = 0x3c6a440;
    stats.issued = 42; lifecycle_discard_ship = 0x9000;
    assert(!stock_view_component_gate_policy(0, (uintptr_t)&object, 0xa5, &result));
    return 0;
}

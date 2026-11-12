/* Engine-free identity/context fixture: never invokes a native X4 function. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    unsigned va;
    unsigned length;
    const char *sha256;
} lx_site;
typedef struct {
    int unused;
} lua_State;
static unsigned char *fake_image;
#define LX(va) (fake_image + (va) - 0x400000u)
static unsigned pin_calls;
static unsigned getter_calls;
static unsigned read_calls;
static unsigned fail_read;
static uintptr_t fail_address;
static int pins_valid;
static int boundary_valid;
static unsigned lua_nil;
static char lua_output[1536];
static struct {
    uintptr_t owner;
    atomic_uint active;
} sko_session;
static int site_ok(const lx_site *site) {
    (void)site;
    ++pin_calls;
    return 0;
}
static int read_local(const void *source, void *output, size_t length) {
    memcpy(output, source, length);
    return 1;
}
static uint64_t world_identity_lookup(uint64_t runtime) {
    return runtime ? runtime + 1000 : 0;
}
static int stock_observer_worker_boundary(void) {
    return boundary_valid;
}
static void p_lua_pushnil(lua_State *state) {
    (void)state;
    ++lua_nil;
}
static void p_lua_pushlstring(lua_State *state, const char *text, size_t length) {
    (void)state;
    assert(length < sizeof(lua_output));
    memcpy(lua_output, text, length);
    lua_output[length] = 0;
}
#include "gate_stock_keyboard_identity.h"

static unsigned char universe[0x240];
static unsigned char player[0x320];
static unsigned char controller[0xe0];
static unsigned char ship[0xe0];
static unsigned char input_owner[0xa020];
static uintptr_t getter_ship;
static uintptr_t getter_controller;

static int fixture_pins(void) {
    ++pin_calls;
    return pins_valid;
}
static int fixture_read(uintptr_t address, void *output, size_t length) {
    ++read_calls;
    if (read_calls == fail_read || address == fail_address) {
        return 0;
    }
    memcpy(output, (const void *)address, length);
    return 1;
}
static uintptr_t fixture_getter(unsigned va) {
    ++getter_calls;
    assert(va == 0x1dc8d40 || va == 0x1dd19d0);
    return va == 0x1dc8d40 ? getter_ship : getter_controller;
}
static void pointer_at(void *object, size_t offset, uintptr_t value) {
    memcpy((unsigned char *)object + offset, &value, sizeof(value));
}
static stock_keyboard_identity_ops fixture_operations(void) {
    stock_keyboard_identity_ops operations = {
        fixture_pins,   fixture_read,          stock_keyboard_identity_address,
        fixture_getter, world_identity_lookup, (uintptr_t)input_owner,
    };
    return operations;
}
static void fixture_reset(void) {
    memset(fake_image, 0, 0x3c00000);
    memset(universe, 0, sizeof(universe));
    memset(player, 0, sizeof(player));
    memset(controller, 0, sizeof(controller));
    memset(ship, 0, sizeof(ship));
    memset(input_owner, 0, sizeof(input_owner));
    pointer_at(LX(0x3db6948), 0, (uintptr_t)universe);
    pointer_at(universe, 0x238, (uintptr_t)player);
    pointer_at(player, 0, (uintptr_t)LX(0x3b5f7a0));
    pointer_at(player, 8, 11);
    pointer_at(ship, 0, (uintptr_t)LX(0x3d21748));
    pointer_at(ship, 8, 22);
    pointer_at(controller, 0, (uintptr_t)LX(0x3b7c208));
    pointer_at(controller, 0x20, (uintptr_t)ship);
    pointer_at(LX(0x3daaaf8), 0, (uintptr_t)input_owner);
    input_owner[0xcf0] = 1;
    ship[0xd1] = 1;
    controller[0xd9] = 1;
    *LX(0x3d8daa8) = 1;
    *LX(0x3d8daa9) = 1;
    *LX(0x3d8da60) = 1;
    getter_ship = (uintptr_t)ship;
    getter_controller = (uintptr_t)controller;
    pin_calls = 0;
    getter_calls = 0;
    read_calls = 0;
    fail_read = 0;
    fail_address = 0;
    pins_valid = 1;
}
static void expect_context(const char *reason) {
    stock_keyboard_identity_sample sample;
    stock_keyboard_identity_ops operations = fixture_operations();
    assert(stock_keyboard_identity_collect_with(&sample, &operations));
    assert(!sample.ready && !strcmp(sample.reason, reason));
}
static void test_complete_sample(void) {
    fixture_reset();
    stock_keyboard_identity_sample sample;
    stock_keyboard_identity_ops operations = fixture_operations();
    assert(stock_keyboard_identity_collect_with(&sample, &operations));
    assert(sample.ready && !strcmp(sample.reason, "ready"));
    assert(sample.universe == (uintptr_t)universe && sample.player == (uintptr_t)player);
    assert(sample.controller == (uintptr_t)controller && sample.ship == (uintptr_t)ship);
    assert(sample.controller_owner == sample.ship && sample.input_owner == (uintptr_t)input_owner);
    assert(sample.runtime == 22 && sample.saved == 1022);
    assert(sample.player_runtime == 11 && sample.player_saved == 1011);
    assert(getter_calls == 2);
    unsigned complete_reads = read_calls;
    for (unsigned failed_read = 1; failed_read <= complete_reads; ++failed_read) {
        fixture_reset();
        fail_read = failed_read;
        sample.ready = 1;
        assert(!stock_keyboard_identity_collect_with(&sample, &operations));
        assert(!sample.ready && sample.reason && strcmp(sample.reason, "ready"));
    }
    assert(!stock_keyboard_identity_collect_with(NULL, &operations));
}
static void test_identity_refusals(void) {
    stock_keyboard_identity_sample sample;
    stock_keyboard_identity_ops operations = fixture_operations();
    fixture_reset();
    pins_valid = 0;
    assert(!stock_keyboard_identity_collect_with(&sample, &operations));
    assert(!getter_calls && !read_calls && !strcmp(sample.reason, "pins"));
    fixture_reset();
    pointer_at(player, 0, (uintptr_t)LX(0x3d21748));
    assert(!stock_keyboard_identity_collect_with(&sample, &operations));
    assert(!getter_calls && !strcmp(sample.reason, "player_identity"));
    fixture_reset();
    pointer_at(player, 8, 0);
    assert(!stock_keyboard_identity_collect_with(&sample, &operations));
    assert(!getter_calls && !strcmp(sample.reason, "player_saved_identity"));
    fixture_reset();
    getter_ship = 0;
    assert(!stock_keyboard_identity_collect_with(&sample, &operations));
    assert(getter_calls == 1 && !strcmp(sample.reason, "ship_identity"));
    fixture_reset();
    getter_controller = 0;
    assert(!stock_keyboard_identity_collect_with(&sample, &operations));
    assert(!strcmp(sample.reason, "controller_identity"));
    fixture_reset();
    pointer_at(controller, 0x20, (uintptr_t)player);
    assert(!stock_keyboard_identity_collect_with(&sample, &operations));
    assert(!strcmp(sample.reason, "controller_identity"));
    fixture_reset();
    pointer_at(ship, 8, 0);
    assert(!stock_keyboard_identity_collect_with(&sample, &operations));
    assert(!strcmp(sample.reason, "ship_saved_identity"));
    fixture_reset();
    operations.observer_owner = (uintptr_t)player;
    assert(!stock_keyboard_identity_collect_with(&sample, &operations));
    assert(!strcmp(sample.reason, "input_owner_identity"));
    /* A previous success cannot supply a fallback identity after a fresh null. */
    operations = fixture_operations();
    assert(stock_keyboard_identity_collect_with(&sample, &operations) && sample.ready);
    pointer_at(LX(0x3db6948), 0, 0);
    assert(!stock_keyboard_identity_collect_with(&sample, &operations));
    assert(!sample.ready && !sample.player && !sample.ship);
}
static void test_key_bank(void) {
    fixture_reset();
    stock_keyboard_identity_sample sample;
    stock_keyboard_identity_ops operations = fixture_operations();
    const unsigned char held_keys[4] = {1, 0, 1, 0};
    memcpy(LX(0x3d8da68), held_keys, sizeof(held_keys));
    assert(stock_keyboard_identity_collect_with(&sample, &operations));
    assert(sample.ready && !memcmp(sample.keys, held_keys, sizeof(held_keys)));
    fail_address = (uintptr_t)LX(0x3d8da68);
    assert(!stock_keyboard_identity_collect_with(&sample, &operations));
    assert(!sample.ready && !strcmp(sample.reason, "context_read"));
}

static void test_context_refusals(void) {
    fixture_reset();
    input_owner[0xcf0] = 0;
    expect_context("space_context");
    fixture_reset();
    *LX(0x3d8daa9) = 0;
    expect_context("space_context");
    fixture_reset();
    *LX(0x3d8daa8) = 0;
    expect_context("space_context");
    fixture_reset();
    *LX(0x3d8da60) = 0;
    expect_context("controls_disabled");
    fixture_reset();
    *LX(0x3d8da64) = 1;
    expect_context("controls_disabled");
    fixture_reset();
    *LX(0x3d8da58) = 1;
    expect_context("autopilot");
    fixture_reset();
    *LX(0x3e5b29a) = 1;
    expect_context("input_ignored");
    fixture_reset();
    ship[0xd1] = 0;
    expect_context("controller_inactive");
    fixture_reset();
    controller[0xd9] = 0;
    expect_context("controller_inactive");
    for (unsigned axis_index = 0; axis_index < 3; ++axis_index) {
        fixture_reset();
        input_owner[0x9f36 + axis_index] = 1;
        expect_context("mouse_mode");
        float nonzero = 0.25f;
        float nonfinite = NAN;
        fixture_reset();
        memcpy(LX(0x3d8d9a0) + 4 * axis_index, &nonzero, sizeof(nonzero));
        expect_context("nonzero_axes");
        fixture_reset();
        memcpy(LX(0x3d8d9b8) + 4 * axis_index, &nonzero, sizeof(nonzero));
        expect_context("nonzero_axes");
        fixture_reset();
        memcpy(LX(0x3d8d9a0) + 4 * axis_index, &nonfinite, sizeof(nonfinite));
        expect_context("nonfinite_axes");
        fixture_reset();
        memcpy(LX(0x3d8d9b8) + 4 * axis_index, &nonfinite, sizeof(nonfinite));
        expect_context("nonfinite_axes");
    }
}
static void test_lua_boundary(void) {
    fixture_reset();
    lua_State state = {0};
    boundary_valid = 0;
    lua_nil = 0;
    assert(lua_stock_keyboard_identity(&state) == 1);
    assert(lua_nil == 1 && !pin_calls && !getter_calls);
    boundary_valid = 1;
    atomic_store(&sko_session.active, 0);
    assert(lua_stock_keyboard_identity(&state) == 1);
    assert(lua_nil == 2 && !pin_calls && !getter_calls);
    atomic_store(&sko_session.active, 1);
    assert(lua_stock_keyboard_identity(&state) == 1);
    assert(pin_calls == 1 && !getter_calls);
    assert(strstr(lua_output, "complete=0 ready=0 reason=pins"));
}
int main(void) {
    fake_image = calloc(1, 0x3c00000);
    assert(fake_image);
    test_complete_sample();
    test_identity_refusals();
    test_context_refusals();
    test_key_bank();
    test_lua_boundary();
    free(fake_image);
    puts("stock keyboard identity: synthetic read failures, identity, context, Lua boundary PASS");
    return 0;
}

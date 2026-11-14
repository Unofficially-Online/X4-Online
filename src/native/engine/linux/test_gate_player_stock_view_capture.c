#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
static unsigned char universe[0x400], camera[0x890];
static int keyboard_probe_requested = 1;
static void *player_routes_scope;
static struct { uint64_t issued; unsigned slot; } stats = {42, 2};
static struct { struct { uint64_t first_capture, last_capture; } config; } keyboard_probe_queue = {{38, 217}};
static struct { uintptr_t universe; } keyboard_probe_binding;
static int world_identity_sealed = 1, world_identity_storage;
static int *world_identities = &world_identity_storage;
static unsigned pin_failure, local_valid = 1, float_calls, fov_calls;
typedef struct { const char *name; uintptr_t address; unsigned size; const char *sha; } lx_site;
static int site_ok(const lx_site *site) { return site && !pin_failure; }
static int keyboard_probe_worker(void) { return 1; }
static int stock_observer_worker_boundary(void) { return 1; }
static int keyboard_probe_identity(void) { return 1; }
static int read_local(void *address, void *out, size_t length) { memcpy(out, address, length); return 1; }
static uint64_t world_identity_portable(uint64_t runtime) { return runtime == 7 ? 90 : 0; }
static unsigned char native_floating(void) { ++float_calls; return 1; }
static float native_fov(void *at) { ++fov_calls; assert(at == camera); return 0.5f; }
static uintptr_t lookup(unsigned address) {
    if (address == 0x1d6b1c0) return (uintptr_t)native_floating;
    if (address == 0x1926cc0) return (uintptr_t)native_fov;
    return 0;
}
#define LX(va) ((void *)lookup(va))
#include "gate_player_stock_view_capture.h"
static int local_auth(void *unused, const player_stock_view_capture_identity *id) {
    (void)unused; return local_valid && id->player == 0x1111 && id->controller == 0x2222;
}
static int local_target(void *unused, const player_stock_view_capture_identity *id, uint64_t *runtime) {
    (void)unused; (void)id; *runtime = 7; return 1;
}
int main(void) {
    uintptr_t ptr = (uintptr_t)camera; memcpy(universe + 0x3e8, &ptr, 8);
    keyboard_probe_binding.universe = (uintptr_t)universe;
    player_stock_view_capture_identity id = {42, (uintptr_t)universe, 0x1111, 0x2222, 2};
    player_stock_view_local_selection selection = {NULL, local_auth, local_target};
    player_stock_view_capture_sample sample = {0};
    assert(player_stock_view_capture_native(&selection, &id, &sample));
    assert(sample.bits == 3 && sample.fov == 0.5f && sample.target == 90 && float_calls == 1 && fov_calls == 1);
    pin_failure = 1; assert(!player_stock_view_capture_native(&selection, &id, &sample)); pin_failure = 0;
    local_valid = 0; assert(!player_stock_view_capture_native(&selection, &id, &sample)); local_valid = 1;
    id.local_slot = 1; assert(!player_stock_view_capture_native(&selection, &id, &sample)); id.local_slot = 2;
    id.generation = 41; assert(!player_stock_view_capture_native(&selection, &id, &sample)); id.generation = 42;
    selection.target_runtime = NULL; assert(!player_stock_view_capture_native(&selection, &id, &sample));
    puts("native VIEW capture adapter gate: PASS");
    return 0;
}

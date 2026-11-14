#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "player_stock_view_capture.h"
#include "player_stock_view_control_proof.h"
#include "player_selection_parity.h"

static unsigned char players[2][0x6dc0], ships[2][0x208], controllers[2][0x28];
static unsigned char registry_storage[0x100], block[32], target[16], manager[0x60], entry[0x58];
static uintptr_t registry_global, manager_global;
static struct { unsigned slot; uint64_t issued; } stats;
static struct { unsigned version; } startup = {1};
#define player_startup() (&startup)
static struct { unsigned frames; } player_session_budget = {180};
static struct { struct { uint64_t first_capture, last_capture; } config; } keyboard_probe_queue = {{38, 217}};
static struct { uintptr_t universe, player, ship, controller; } keyboard_probe_binding;
static uintptr_t factory_probe_player1, lifecycle_discard_controller;
static struct { uintptr_t player2; unsigned player2_owned, player2_uncertain; } factory_probe_state;
static struct { uintptr_t controller, ship; uint64_t player_runtime; } factory_probe_identity;
static uint64_t factory_probe_player2_runtime;
static struct { struct { uintptr_t player2; } request; } presentation_fork;
static void *player_routes_scope;
static unsigned worker_ok = 1, physics_ok = 1, lease_ok = 1, registry_ok = 1, pins_ok = 1;
static int world_identity_sealed = 1, world_identity_storage;
static void *world_identities = &world_identity_storage;
static char receipt[8192];
static size_t receipt_length;
static unsigned short_write;
typedef struct { const char *name; uintptr_t address; size_t size; const char *sha; } lx_site;
static int site_ok(const lx_site *site) { return pins_ok && site->sha && strlen(site->sha) == 64; }
static int keyboard_probe_worker(void) { return worker_ok; }
static int player_recurring_physics_entered(void) { return physics_ok; }
static int player_physics_identity(void) { return physics_ok; }
static int stock_view_ui_lease_auth(void *unused, const player_stock_view_capture_identity *id) {
    (void)unused; return lease_ok && id->local_slot == 2;
}
static int stock_view_host_auth(void *unused, const player_stock_view_capture_identity *id) {
    (void)unused; return lease_ok && id->local_slot == 1;
}
static int read_local(void *at, void *out, size_t n) { memcpy(out, at, n); return 1; }
static int stock_view_control_native_read(void *unused, uintptr_t at, void *out, size_t n) {
    (void)unused; return read_local((void *)at, out, n);
}
static int fake_registry(const player_stock_view_control_ops *ops, uintptr_t object, uint64_t runtime) {
    (void)ops; (void)object; return registry_ok && runtime;
}
#define stock_view_control_registry fake_registry
static uintptr_t address(uintptr_t va) {
    if (va == 0x3daa860) return (uintptr_t)&registry_global;
    if (va == 0x3dbafd0) return (uintptr_t)&manager_global;
    return va;
}
#define LX(va) address(va)
static uint64_t GetCurrentThreadId(void) { return 17; }
static uint64_t world_identity_lookup(uint64_t runtime) { return runtime == 1 ? 101 : 0; }
static uint64_t world_identity_portable(uint64_t runtime) { return world_identity_lookup(runtime); }
static ssize_t record_write(int fd, const void *data, size_t size) {
    assert(fd == 2 && size <= sizeof receipt - receipt_length);
    if (short_write) return (ssize_t)size - 1;
    memcpy(receipt + receipt_length, data, size); receipt_length += size;
    return (ssize_t)size;
}
#define write record_write
#include "gate_player_selection_parity.h"
#undef write
static void set64(void *p, uintptr_t v) { memcpy(p, &v, 8); }
static void setup(void) {
    stats.slot = 2; stats.issued = 50; selection_parity_last_generation = 0; receipt_length = 0;
    memset(receipt, 0, sizeof receipt); memset(players, 0, sizeof players);
    factory_probe_player1 = (uintptr_t)players[0];
    factory_probe_state.player2 = (uintptr_t)players[1]; factory_probe_state.player2_owned = 1;
    factory_probe_state.player2_uncertain = 0;
    factory_probe_identity.player_runtime = 10; factory_probe_player2_runtime = 11;
    factory_probe_identity.controller = keyboard_probe_binding.controller = (uintptr_t)controllers[0];
    factory_probe_identity.ship = keyboard_probe_binding.ship = (uintptr_t)ships[0];
    keyboard_probe_binding.universe = 0x1234; keyboard_probe_binding.player = (uintptr_t)players[0];
    lifecycle_discard_controller = (uintptr_t)controllers[1]; presentation_fork.request.player2 = (uintptr_t)players[1];
    for (unsigned i = 0; i < 2; ++i) {
        set64(players[i], 0x3b5f7a0); set64(controllers[i] + 0x20, (uintptr_t)ships[i]);
        set64(ships[i] + 0x200, (uintptr_t)controllers[i]);
    }
    registry_global = (uintptr_t)registry_storage; manager_global = (uintptr_t)manager;
    set64(registry_storage + 0x60, (uintptr_t)block); set64(block, (uintptr_t)target); set64(target + 8, 1);
    uint32_t flags = 0x00050000; memcpy(block + 28, &flags, 4);
    set64(players[0] + 0x6db8, 1); set64(players[1] + 0x6db8, UINT64_C(1) << 25);
    set64(manager + 0x40, (uintptr_t)entry); manager[0x5c] = 1; set64(entry + 0x50, 0);
}
int main(void) {
    setup(); unsetenv("X4_DEV_SELECTION_PARITY"); pins_ok = 0;
    assert(player_selection_parity_capture(50, 2) && !receipt_length);
    pins_ok = 1; assert(!setenv("X4_DEV_SELECTION_PARITY", "1", 1));
    errno = 71; assert(player_selection_parity_capture(50, 2) && errno == 71);
    assert(strstr(receipt, "source=p1_player status=valid") && strstr(receipt, "source=p2_player status=invalid") &&
           strstr(receipt, "source=local_ui status=absent"));
    assert(!player_selection_parity_capture(50, 2)); /* no duplicate generation */
    setup(); lease_ok = 0; assert(!player_selection_parity_capture(50, 2) && !receipt_length); lease_ok = 1;
    setup(); worker_ok = 0; assert(!player_selection_parity_capture(50, 2)); worker_ok = 1;
    setup(); factory_probe_state.player2_uncertain = 1; assert(!player_selection_parity_capture(50, 2));
    setup(); set64(ships[0] + 0x200, 0); assert(!player_selection_parity_capture(50, 2));
    setup(); registry_ok = 0; assert(!player_selection_parity_capture(50, 2)); registry_ok = 1;
    setup(); short_write = 1; assert(!player_selection_parity_capture(50, 2) && !selection_parity_last_generation); short_write = 0;
    setup(); stats.slot = 1; assert(player_selection_parity_capture(50, 1));
    assert(strstr(receipt, "stage=identity tick=50 slot=1"));
    return 0;
}

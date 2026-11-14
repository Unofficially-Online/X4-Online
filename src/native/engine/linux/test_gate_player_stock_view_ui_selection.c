#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "player_stock_view_ui_selection.h"
static unsigned parity_ok = 1;
static int player_selection_parity_capture(uint64_t generation, unsigned slot) {
    (void)generation; (void)slot; return parity_ok;
}
enum { PLAYER_UI_OVERLAY_ACTIVE = 2 };
typedef struct { uintptr_t player1, player2, room; uint32_t bank; uint64_t expected_p2_ship; } presentation_fork_request;
typedef struct { struct { long owner_tid; unsigned state; uintptr_t object; } lifecycle; uintptr_t object; } native_ui_overlay_probe;
static struct { presentation_fork_request request; uintptr_t universe, p2_ship; uint64_t (*occupied)(void); } presentation_fork;
static native_ui_overlay_probe ui_overlay_probe;
static struct { unsigned slot; uint64_t issued; } stats = {2, 44};
static struct { uint64_t generation; } player_recurring_tail_token = {44};
static uintptr_t lifecycle_discard_controller = 0x3333;
static int world_identity_sealed = 1, world_identity_storage;
static int *world_identities = &world_identity_storage;
static unsigned pin_failure, lease_identity = 1, wrong_controlled, getter_calls, fov_calls;
static int ui_game_render_active, ui_camera_refill_active, ui_correction_enabled;
static uint64_t player_physics_worker;
static uint64_t GetCurrentThreadId(void) { return (uint64_t)syscall(SYS_gettid); }
static unsigned char universe[0x400], camera[0x890], manager[0x60], entry[0x60];
static uintptr_t manager_global, registry_global = 0x1234;
static uint64_t handle = 77;
typedef struct { const char *name; uintptr_t address; unsigned size; const char *sha; } lx_site;
static int site_ok(const lx_site *site) {
    assert(site && site->sha && strlen(site->sha) == 64);
    return !pin_failure;
}
static int player_recurring_physics_entered(void) { return 1; }
static uint32_t ui_overlay_expected_bank(const native_ui_overlay_probe *probe) { (void)probe; return 0; }
static int presentation_fork_owner(void *unused, uintptr_t a, uintptr_t b, uintptr_t room, uint32_t bank) {
    (void)unused; (void)a; (void)b; (void)room; (void)bank; return lease_identity;
}
static int ui_overlay_bank_matches(native_ui_overlay_probe *probe, uint32_t bank) { (void)probe; (void)bank; return 1; }
static uint64_t occupied(void) { return 0x4444; }
static int read_local(void *at, void *out, size_t size) { memcpy(out, at, size); return 1; }
static uint64_t world_identity_portable(uint64_t runtime) { return runtime == 77 ? 99 : 0; }
static player_stock_view_softtarget getter(void) {
    ++getter_calls; return (player_stock_view_softtarget){handle, "", 0};
}
static float fov(void *object) { ++fov_calls; assert(object == camera); return 0.8f; }
static uintptr_t lookup(unsigned va) {
    switch (va) {
    case 0x3dbafd0: return (uintptr_t)&manager_global;
    case 0x3daa860: return (uintptr_t)&registry_global;
    case 0xa45060: return (uintptr_t)getter;
    case 0x1926cc0: return (uintptr_t)fov;
    default: return 0;
    }
}
#define LX(va) ((void *)lookup(va))
#include "gate_player_stock_view_ui_selection.h"
static int control_proof(void *unused, const player_stock_view_capture_identity *id) {
    (void)unused; return !wrong_controlled && id->player == 0x2222;
}
int main(void) {
    presentation_fork.request = (presentation_fork_request){0x1111, 0x2222, 0x7777, 0, 0x4444};
    presentation_fork.universe = (uintptr_t)universe; presentation_fork.p2_ship = 0x4444; presentation_fork.occupied = occupied;
    ui_overlay_probe.lifecycle.owner_tid = syscall(SYS_gettid);
    player_physics_worker = GetCurrentThreadId();
    ui_overlay_probe.lifecycle.state = PLAYER_UI_OVERLAY_ACTIVE; ui_overlay_probe.object = 0x8888;
    ui_overlay_probe.lifecycle.object = ui_overlay_probe.object;
    uintptr_t p = (uintptr_t)camera; memcpy(universe + 0x3e8, &p, 8);
    manager_global = (uintptr_t)manager; manager[0x5c] = 1; p = (uintptr_t)entry;
    memcpy(manager + 0x40, &p, 8); memcpy(entry + 0x50, &handle, 8);
    player_stock_view_capture_identity id = {44, (uintptr_t)universe, 0x2222, 0x3333, 2};
    player_stock_view_capture_sample sample = {0};
    player_stock_view_ui_control_proof proof = {NULL, control_proof};
    assert(player_stock_view_ui_capture_native(&id, &proof, &sample));
    assert(sample.bits == 1 && sample.fov == 0.8f && sample.target == 99);
    assert(getter_calls == 2 && fov_calls == 1);
    assert(player_stock_view_ui_capture_observed(&id, &proof, (uintptr_t)camera, 1.25f, &sample));
    assert(sample.fov == 1.25f && fov_calls == 1);
    player_stock_view_capture_sample unchanged = sample;
    parity_ok = 0;
    assert(!player_stock_view_ui_capture_observed(&id, &proof, (uintptr_t)camera, 1.5f, &sample));
    assert(!memcmp(&sample, &unchanged, sizeof sample));
    parity_ok = 1;
    assert(!player_stock_view_ui_capture_observed(&id, &proof, (uintptr_t)camera + 8, 1.25f, &sample));
    unsigned before_getter = getter_calls;
    uint32_t mode = 1; memcpy(camera + 0x880, &mode, 4);
    p = 0x9999; memcpy(camera + 0x780, &p, 8);
    assert(player_stock_view_ui_capture_native(&id, &proof, &sample));
    assert(sample.bits == 2 && sample.fov == 1.0f && sample.target == 99 && getter_calls == before_getter + 2 && fov_calls == 1);
    assert(!player_stock_view_ui_capture_native(&id, NULL, &sample));
    pin_failure = 1; assert(!player_stock_view_ui_capture_native(&id, &proof, &sample)); pin_failure = 0;
    lease_identity = 0; assert(!player_stock_view_ui_capture_native(&id, &proof, &sample)); lease_identity = 1;
    wrong_controlled = 1; assert(!player_stock_view_ui_capture_native(&id, &proof, &sample)); wrong_controlled = 0;
    ui_overlay_probe.lifecycle.state = 0; assert(!player_stock_view_ui_capture_native(&id, &proof, &sample));
    ui_overlay_probe.lifecycle.state = PLAYER_UI_OVERLAY_ACTIVE;
    ui_game_render_active = 1; assert(!player_stock_view_ui_capture_native(&id, &proof, &sample)); ui_game_render_active = 0;
    ui_overlay_probe.lifecycle.object = 0; assert(!player_stock_view_ui_capture_native(&id, &proof, &sample));
    ui_overlay_probe.lifecycle.object = ui_overlay_probe.object;
    id.generation++; assert(!player_stock_view_ui_capture_native(&id, &proof, &sample)); id.generation--;
    id.local_slot = 1; assert(!player_stock_view_ui_capture_native(&id, &proof, &sample));
    puts("native guest UI VIEW capture lease: PASS"); return 0;
}

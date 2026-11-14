#define main control_fixture_main
#include "test_player_stock_view_control_proof.c"
#undef main
#include "player_stock_view_capture.h"
#define X4_GATE_PLAYER_STOCK_VIEW_UI_SELECTION_H
/* Minimal gate surroundings; the actual UI lease has its own fixture. */
typedef struct { void *context; int (*authenticate)(void *, const player_stock_view_capture_identity *); } player_stock_view_ui_control_proof;
static int lease = 1, pin_drift;
static fixture f;
static int stock_view_ui_lease_auth(void *unused, const player_stock_view_capture_identity *id) {
    (void)unused; return lease && id && id->player == (uintptr_t)f.p2 && id->local_slot == 2;
}
static int player_stock_view_ui_capture_native(const player_stock_view_capture_identity *id,
        const player_stock_view_ui_control_proof *proof, player_stock_view_capture_sample *out) {
    if (!proof->authenticate(proof->context, id)) return 0;
    out->bits = 1; out->fov = 1.0f; out->target = 0; return 1;
}
static int player_stock_view_ui_capture_observed(const player_stock_view_capture_identity *id,
        const player_stock_view_ui_control_proof *proof, uintptr_t camera, float fov,
        player_stock_view_capture_sample *out) {
    if (!camera || !player_stock_view_ui_capture_native(id, proof, out)) return 0;
    out->fov = fov; return 1;
}
typedef struct { uint64_t room_runtime, target_runtime; } player_target_hold_snapshot;
static struct { player_target_hold_snapshot identity; uintptr_t target_reference; } player_target_hold_resident;
static struct { struct { uintptr_t player1, player2, room; } request; } presentation_fork;
static struct { uint64_t player_runtime; } factory_probe_identity;
static uint64_t factory_probe_player2_runtime;
typedef struct { const char *name; uintptr_t va; unsigned length; const char *sha256; } lx_site;
static int site_ok(const lx_site *site) { return !pin_drift && site && site->sha256 && strlen(site->sha256) == 64; }
static void *address(uintptr_t va) { return va == 0x3daa860u ? &f.registry_ptr : (void *)va; }
#define LX(va) address(va)
static int read_local(void *address_arg, void *out, size_t size) {
    uintptr_t at = (uintptr_t)address_arg;
    static const struct { uintptr_t address, value; } slots[] = {
        {0x3b5f7a0u + 0x1ce0u, 0x136c820u}, {0x3ca5fc0u + 0x11f0u, 0xa39930u},
        {0x3cb4e88u + 0x11f0u, 0xa398e0u}, {0x3d21748u + 0x11f0u, 0xa389f0u}
    };
    for (unsigned i = 0; i < sizeof slots / sizeof *slots; ++i)
        if (at == slots[i].address && size == 8) { memcpy(out, &slots[i].value, 8); return 1; }
    if ((at == 0x2c81ce0u + 28u || at == 0x2c81d00u + 28u) && size == 4) { uint32_t v = 0; memcpy(out, &v, 4); return 1; }
    if (at == 0x2c822a0u + 28u && size == 4) { uint32_t v = 0x401; memcpy(out, &v, 4); return 1; }
    return read_value(&f, at, out, size);
}
#include "gate_player_stock_view_control_proof.h"
int main(void) {
    player_stock_view_control_ops mock; setup(&f, &mock);
    set64(f.p1, 0x3b5f7a0); set64(f.p2, 0x3b5f7a0);
    set64(f.room, 0x3ca5fc0); set64(f.ship, 0x3d21748);
    presentation_fork.request.player1 = (uintptr_t)f.p1; presentation_fork.request.player2 = (uintptr_t)f.p2;
    presentation_fork.request.room = (uintptr_t)f.room; player_target_hold_resident.target_reference = (uintptr_t)f.ship;
    player_target_hold_resident.identity = (player_target_hold_snapshot){3, 4};
    factory_probe_identity.player_runtime = 1; factory_probe_player2_runtime = 2;
    player_stock_view_capture_identity id = {42, 0x8888, (uintptr_t)f.p2, 0x3333, 2};
    player_stock_view_capture_sample out = {0};
    assert(player_stock_view_ui_verified_capture(&id, &out));
    pin_drift = 1; assert(!player_stock_view_ui_verified_capture(&id, &out)); pin_drift = 0;
    lease = 0; assert(!player_stock_view_ui_verified_capture(&id, &out)); lease = 1;
    id.local_slot = 1; assert(!player_stock_view_ui_verified_capture(&id, &out)); id.local_slot = 2;
    f.vector[0] = 0x9999; assert(!player_stock_view_ui_verified_capture(&id, &out));
    puts("native UI control proof adapter: PASS"); return 0;
}

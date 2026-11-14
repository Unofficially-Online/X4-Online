#include "player_stock_view_control_proof.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    unsigned char p1[0x330], p2[0x330], room[0x80], ship[0x80], registry[0x70], block[64];
    uintptr_t registry_ptr, vector[3];
    unsigned auth_ok, fail_reads, drift_vector, drift_registry;
} fixture;
static void set64(void *p, uint64_t value) { memcpy(p, &value, 8); }
static void set32(void *p, uint32_t value) { memcpy(p, &value, 4); }
static int auth(void *ctx) { return ((fixture *)ctx)->auth_ok; }
static int read_value(void *ctx, uintptr_t at, void *out, size_t size) {
    fixture *f = ctx;
    if (f->fail_reads || at < (uintptr_t)f || size > sizeof *f || at - (uintptr_t)f > sizeof *f - size) return 0;
    memcpy(out, (void *)at, size);
    if (f->drift_registry && at == (uintptr_t)&f->registry_ptr) { f->drift_registry = 0; f->vector[0] = 0x1111; }
    if (f->drift_vector && at == (uintptr_t)&f->vector[1]) {
        f->drift_vector = 0; f->vector[1] = 0;
    }
    return 1;
}
static void setup(fixture *f, player_stock_view_control_ops *ops) {
    memset(f, 0, sizeof *f); f->auth_ok = 1;
    const uintptr_t player_v = 0xaaaa, cockpit_v = 0xbbbb, ship_v = 0xcccc;
    set64(f->p1, player_v); set64(f->p2, player_v); set64(f->room, cockpit_v); set64(f->ship, ship_v);
    set64(f->p1 + 8, 1); set64(f->p2 + 8, 2); set64(f->room + 8, 3); set64(f->ship + 8, 4);
    set64(f->p1 + 0x70, (uintptr_t)f->room); set64(f->p2 + 0x70, (uintptr_t)f->room);
    set64(f->room + 0x70, (uintptr_t)f->ship); f->vector[1] = (uintptr_t)f->ship;
    player_stock_view_control_vector v = {(uintptr_t)f->vector, (uintptr_t)(f->vector + 3), (uintptr_t)(f->vector + 3)};
    memcpy(f->p1 + 0x308, &v, sizeof v); memcpy(f->p2 + 0x308, &v, sizeof v);
    f->registry_ptr = (uintptr_t)f->registry; set64(f->registry + 0x60, (uintptr_t)f->block);
    set64(f->block, (uintptr_t)f->p1); set64(f->block + 8, (uintptr_t)f->p2);
    set64(f->block + 16, (uintptr_t)f->room); set32(f->block + 28, 0x05550000);
    set64(f->block + 32, (uintptr_t)f->ship); set32(f->block + 60, 0x00050000);
    *ops = (player_stock_view_control_ops){f, auth, read_value, (uintptr_t)&f->registry_ptr,
        (uintptr_t)f->p1, (uintptr_t)f->p2, (uintptr_t)f->room, (uintptr_t)f->ship,
        1, 2, 3, 4, player_v, cockpit_v, 0xdddd, ship_v};
}
int main(void) {
    fixture f; player_stock_view_control_ops ops;
    setup(&f, &ops); assert(player_stock_view_control_equivalent(&ops));
    setup(&f, &ops); ops.player2_runtime = (UINT64_C(5) << 25) | 2;
    set64(f.p2 + 8, ops.player2_runtime); set32(f.block + 24, 5u << 16);
    assert(player_stock_view_control_equivalent(&ops));
    setup(&f, &ops); ops.room_runtime = (UINT64_C(7) << 25) | 3;
    set64(f.room + 8, ops.room_runtime); set32(f.block + 28, 0x05550007);
    assert(player_stock_view_control_equivalent(&ops));
    setup(&f, &ops); f.vector[0] = 0x1111; assert(!player_stock_view_control_equivalent(&ops));
    setup(&f, &ops); set64(f.p1 + 0x70, 0); assert(!player_stock_view_control_equivalent(&ops));
    setup(&f, &ops); set64(f.p2 + 0x308, 0); assert(!player_stock_view_control_equivalent(&ops));
    setup(&f, &ops); set32(f.ship + 0x68, 1); assert(!player_stock_view_control_equivalent(&ops));
    setup(&f, &ops); set32(f.block + 28, 0x05510000); assert(!player_stock_view_control_equivalent(&ops));
    setup(&f, &ops); set32(f.block + 24, 1); assert(!player_stock_view_control_equivalent(&ops));
    setup(&f, &ops); set64(f.block + 16, (uintptr_t)f.p2); assert(!player_stock_view_control_equivalent(&ops));
    setup(&f, &ops); f.drift_vector = 1; assert(!player_stock_view_control_equivalent(&ops));
    setup(&f, &ops); f.drift_registry = 1; assert(!player_stock_view_control_equivalent(&ops));
    setup(&f, &ops); f.auth_ok = 0; assert(!player_stock_view_control_equivalent(&ops));
    setup(&f, &ops); f.fail_reads = 1; assert(!player_stock_view_control_equivalent(&ops));
    puts("read-only UI control identity and registry: PASS"); return 0;
}

#include "player_stock_view_capture.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

typedef struct {
    unsigned char universe[0x400], camera[0x890];
    player_stock_view_capture_identity identity;
    uint64_t runtime, mapped;
    float factor;
    unsigned floating, auth_count, read_count, fov_count, floating_count, target_count, map_count;
    unsigned fail_auth, fail_target, fail_map, mutate_camera, mutate_mode, mutate_target;
} fixture;
static void word(void *p, uintptr_t value) { memcpy(p, &value, sizeof value); }
static int authenticate(void *context, const player_stock_view_capture_identity *id) {
    fixture *f = context; ++f->auth_count;
    return f->auth_count != f->fail_auth && !memcmp(id, &f->identity, sizeof *id);
}
static int read_value(void *context, uintptr_t at, void *out, size_t size) {
    fixture *f = context; ++f->read_count;
    if (at < (uintptr_t)f || size > sizeof *f || at - (uintptr_t)f > sizeof *f - size) return 0;
    memcpy(out, (void *)at, size); return 1;
}
static int floating(void *context, unsigned *out) {
    fixture *f = context; ++f->floating_count; *out = f->floating;
    if (f->mutate_camera) word(f->universe + 0x3e8, 0);
    if (f->mutate_mode) f->camera[0x880] = 3;
    return 1;
}
static int fov(void *context, uintptr_t camera, float *out) {
    fixture *f = context; assert(camera == (uintptr_t)f->camera);
    ++f->fov_count; *out = f->factor; return 1;
}
static int target(void *context, const player_stock_view_capture_identity *id, uint64_t *out) {
    fixture *f = context; assert(id->player == f->identity.player);
    ++f->target_count; if (f->target_count == f->fail_target) return 0;
    *out = f->runtime; if (f->mutate_target && f->target_count == 1) ++f->runtime;
    return 1;
}
static int portable(void *context, uint64_t runtime, uint64_t *out) {
    fixture *f = context; ++f->map_count; assert(runtime == f->runtime || (f->mutate_target && runtime + 1 == f->runtime));
    if (f->fail_map) return 0;
    *out = f->mapped; return 1;
}
static void setup(fixture *f, player_stock_view_capture_ops *ops) {
    memset(f, 0, sizeof *f); f->factor = 0.75f; f->runtime = 7; f->mapped = 91;
    f->identity = (player_stock_view_capture_identity){42, (uintptr_t)f->universe, 0x1111, 0x2222, 2};
    word(f->universe + 0x3e8, (uintptr_t)f->camera); word(f->camera + 0x780, 0x1234);
    *ops = (player_stock_view_capture_ops){f, authenticate, read_value, floating, fov, target, portable};
}
static void rejected(fixture *f, player_stock_view_capture_ops *ops) {
    player_stock_view_capture_sample out = {.bits = 0xa5, .fov = -5.0f, .target = UINT64_MAX};
    assert(!player_stock_view_capture_sample_native(ops, &f->identity, &out));
    assert(out.bits == 0xa5 && out.fov == -5.0f && out.target == UINT64_MAX);
}
int main(void) {
    fixture f; player_stock_view_capture_ops ops; player_stock_view_capture_sample out;
    setup(&f, &ops); f.floating = 1;
    assert(player_stock_view_capture_sample_native(&ops, &f.identity, &out));
    assert(out.bits == 3 && out.fov == 0.75f && out.target == 91);
    assert(f.fov_count == 1 && f.floating_count == 1 && f.target_count == 2 && f.map_count == 1);
    setup(&f, &ops); f.camera[0x880] = 2; f.mapped = 0;
    assert(player_stock_view_capture_sample_native(&ops, &f.identity, &out));
    assert(out.bits == 0 && out.fov == 1.0f && out.target == 1 && !f.fov_count);
    setup(&f, &ops); word(f.universe + 0x3e8, 0); f.runtime = 0;
    assert(player_stock_view_capture_sample_native(&ops, &f.identity, &out));
    assert(out.bits == 1 && out.fov == 1.0f && out.target == 0 && !f.fov_count && !f.map_count);
    setup(&f, &ops); f.fail_target = 1; rejected(&f, &ops);
    setup(&f, &ops); f.fail_target = 2; rejected(&f, &ops);
    setup(&f, &ops); f.fail_map = 1; rejected(&f, &ops);
    setup(&f, &ops); f.fail_auth = 2; rejected(&f, &ops);
    setup(&f, &ops); f.mutate_camera = 1; rejected(&f, &ops);
    setup(&f, &ops); f.mutate_mode = 1; rejected(&f, &ops);
    setup(&f, &ops); f.mutate_target = 1; rejected(&f, &ops);
    setup(&f, &ops); f.factor = NAN; rejected(&f, &ops);
    setup(&f, &ops); f.factor = INFINITY; rejected(&f, &ops);
    setup(&f, &ops); f.floating = 2; rejected(&f, &ops);
    setup(&f, &ops); f.identity.local_slot = 0; rejected(&f, &ops);
    setup(&f, &ops); ops.target_runtime = NULL; rejected(&f, &ops);
    puts("stock VIEW capture staging and drift: PASS");
    return 0;
}

#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "player_stock_view_state.h"
#define LX(value) ((void *)(uintptr_t)(value))
#define SEAT_PREFLIGHT_SHIP_VTABLE 0x3d21748u
#define LXW_CLASS_COUNT 3u
typedef struct { uint32_t linux_vtable, windows, flags, family; int32_t top; } lxw_class;
static const lxw_class lxw_classes[] = {{0x3d21748,0,0,0,0},{0x3d282a8,0,0,0,0},{0x3d1d850,0,0,0,0}};
typedef struct { void *context; int (*read)(void *, uintptr_t, void *, size_t); uintptr_t registry_global; } player_stock_view_control_ops;
typedef struct { uintptr_t table; uint64_t runtime; unsigned char pad[0x60]; uintptr_t parent; } object;
static int read_local(const void *at, void *out, size_t size) { memcpy(out, at, size); return 1; }
static int stock_view_component_registry_read(void *context, uintptr_t at, void *out, size_t size) {
    (void)context; return read_local((void *)at, out, size);
}
static int stock_view_control_registry(const player_stock_view_control_ops *ops, uintptr_t at, uint64_t runtime) {
    (void)ops; return at && ((object *)at)->runtime == runtime;
}
static struct { uint64_t issued; } stats;
typedef struct { const void *owner; uint64_t generation, worker_thread; } fwrr_token;
static fwrr_token player_recurring_tail_token;
static struct { uintptr_t ship; } factory_probe_identity;
static uintptr_t lifecycle_discard_ship, stock_view_gate_ships[2], stock_view_gate_controllers[2];
static uint64_t stock_view_gate_tid;
static player_stock_view_state stock_view_gate_state;
static int world_identity_sealed = 1;
static void *world_identities = (void *)1;
static uint64_t world_identity_portable(uint64_t runtime) { return runtime + 100; }
static int stock_view_gate_owner(uintptr_t controller, unsigned *slot) {
    if (stats.issued != stock_view_gate_state.generation) return 0;
    *slot = controller == stock_view_gate_controllers[0] ? 1 : controller == stock_view_gate_controllers[1] ? 2 : 0;
    return *slot != 0;
}
static double tick_time = 3.0;
static unsigned char tls;
static unsigned char *gate_tls_self(void) { return &tls; }
static int gate_tls_worker(unsigned char *p) { return p == &tls; }
static double *gate_tls_time(unsigned char *p) { return p == &tls ? &tick_time : NULL; }
static object *drift_object;
static void lxmc_gtr(void *self, float matrix[16], double now, void *sector) {
    assert(now == 3.0 && sector);
    memset(matrix, 0, 16 * sizeof(float));
    matrix[0] = (float)((object *)self)->runtime;
    if (drift_object) ++drift_object->runtime;
}
static unsigned stock_view_gate_bound = 1, stock_view_gate_component_enabled = 1, stock_view_gate_xs_enabled = 1;
static unsigned stock_view_gate_mode = 2;
static struct { unsigned version; } startup = {1};
static struct { unsigned frames; } player_session_budget = {180};
#define player_startup() (&startup)
typedef struct { int dummy; } player_view_query_api;
typedef struct { int dummy; } player_stock_view_fov_api;
typedef struct { int dummy; } player_stock_view_ship_api;
typedef struct { int dummy; } player_stock_view_component_api;
typedef struct { int dummy; } player_stock_view_precompare_api;
typedef struct { int (*bound)(void); } player_view_xs_preload_api;
typedef struct { const char *name; uintptr_t at; unsigned length; const char *hash; } lx_site;
static int provider_bound = 1;
static unsigned provider_calls;
static int bound(void) { return provider_bound; }
static int site_ok(const lx_site *site) { return site != NULL; }
static int physics_entered = 1;
static int player_recurring_physics_entered(void) { return physics_entered; }
static int view_query_expected_library(const char **sha, uint64_t *size) { *sha = "test"; *size = 1; return 1; }
static int player_view_query_provider(const char *sha, uint64_t size, player_view_query_api *out) {
    (void)sha; (void)size; (void)out; ++provider_calls; return 1;
}
#define PROVIDER(name,type) static int name(const player_view_query_api *sealed, type *out) { (void)sealed; (void)out; return 1; }
PROVIDER(player_stock_view_fov_provider, player_stock_view_fov_api)
PROVIDER(player_stock_view_ship_provider, player_stock_view_ship_api)
PROVIDER(player_stock_view_component_provider, player_stock_view_component_api)
PROVIDER(player_stock_view_precompare_provider, player_stock_view_precompare_api)
#undef PROVIDER
static int player_view_xs_preload_provider(const player_view_query_api *sealed, player_view_xs_preload_api *out) {
    (void)sealed; out->bound = bound; return 1;
}
#include "gate_player_view_xs_adapter.h"
#include "gate_player_view_xs_readiness.h"
int main(void) {
    object sector = {0x3d1d850, 30, {0}, 0};
    object one = {0x3d21748, 10, {0}, (uintptr_t)&sector};
    object two = {0x3d21748, 20, {0}, (uintptr_t)&sector};
    object xs = {0x3d282a8, 50, {0}, (uintptr_t)&sector};
    stock_view_gate_tid = (uint64_t)syscall(SYS_gettid);
    stats.issued = player_recurring_tail_token.generation = 40;
    player_recurring_tail_token.owner = &stats;
    player_recurring_tail_token.worker_thread = stock_view_gate_tid;
    stock_view_gate_ships[0] = factory_probe_identity.ship = (uintptr_t)&one;
    stock_view_gate_ships[1] = lifecycle_discard_ship = (uintptr_t)&two;
    stock_view_gate_controllers[0] = 1; stock_view_gate_controllers[1] = 2;
    stock_view_gate_state = (player_stock_view_state){.generation = 40, .saved = {110,120}, .initialized = 1,
        .slots = {{.cockpit = 0, .target = 150}, {.cockpit = 1}}};
    const char *flags[] = {"X4_DEV_STOCK_VIEW_FOV", "X4_DEV_STOCK_VIEW_SHARED", "X4_DEV_STOCK_VIEW_COMPONENT_POLICY",
        "X4_DEV_VIEW_XS", "X4_DEV_VIEW_ANCESTRY", "X4_DEV_VIEW_COMPONENT_TRACE", "X4_DEV_VIEW_PRECOMPARE_TRACE",
        "X4_DEV_PLAYER_CURSOR", "X4_DEV_PLAYER_FOCUS"};
    for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; ++i) assert(!setenv(flags[i], "1", 1));
    assert(player_view_xs_ready_before_apply());
    assert(player_view_xs_ready_at_generation(40));
    assert(player_view_xs_ready_before_apply() && provider_calls == 1);
    provider_bound = 0; assert(!player_view_xs_ready_at_generation(40)); provider_bound = 1;
    assert(!player_view_xs_ready_at_generation(39));
    player_view_xs_ready_verified = 0;
    assert(!unsetenv("X4_DEV_VIEW_XS")); assert(!player_view_xs_ready_before_apply());
    assert(!setenv("X4_DEV_VIEW_XS", "1", 1));
    assert(player_view_xs_ready_before_apply());
    uintptr_t target = 0;
    assert(player_view_xs_gate_target((uintptr_t)&xs, 0x999, &target) && target == (uintptr_t)&xs);
    uint64_t external = 0;
    assert(player_view_xs_gate_external(0xa5, &external) && external == 1);
    float distance = 0;
    assert(player_view_xs_gate_distance((uintptr_t)&xs, 1, 1, 123, &distance) && distance == 1600.0f);
    drift_object = &xs;
    assert(!player_view_xs_gate_distance((uintptr_t)&xs, 1, 1, 123, &distance));
    drift_object = NULL; xs.runtime = 50;
    ++stats.issued;
    assert(!player_view_xs_gate_target((uintptr_t)&xs, 0x999, &target));
    --stats.issued;
    physics_entered = 0;
    assert(!player_view_xs_gate_external(0xa5, &external));
    player_recurring_tail_token = (fwrr_token){0};
    assert(player_view_xs_gate_target((uintptr_t)&xs, 0x999, &target) == 2);
    assert(player_view_xs_gate_external(0xa5, &external) && external == 0xa5);
    assert(player_view_xs_gate_distance((uintptr_t)&xs, 0, 2, 123, &distance) && distance == 123);
    player_recurring_tail_token.generation = 40;
    assert(!player_view_xs_gate_external(0xa5, &external));
    stock_view_gate_tid += 1;
    assert(!player_view_xs_gate_external(0xa5, &external));
    return 0;
}

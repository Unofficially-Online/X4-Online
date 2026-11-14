#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include "lockstep_stock_record.h"
#include "player_stock_view_fov_preload.h"
#define X4_PLAYER_STOCK_VIEW_FOV_PRELOAD_PIN_H
#define X4_PLAYER_STOCK_VIEW_SHIP_PRELOAD_PIN_H
#define X4_PLAYER_STOCK_VIEW_COMPONENT_PRELOAD_PIN_H
#define X4_PLAYER_STOCK_VIEW_PRECOMPARE_PRELOAD_PIN_H
#define X4_PLAYER_VIEW_XS_PRELOAD_PIN_H
#include "player_view_xs_preload.h"
#include "player_stock_view_precompare_preload.h"
#define X4_GATE_PLAYER_STOCK_VIEW_COMPONENT_POLICY_H
#include "player_stock_view_component_preload.h"
#include "player_stock_view_ship_preload.h"

typedef struct { int dummy; } player_view_query_api;
typedef struct { int (*bind)(player_stock_view_mode_policy, player_stock_view_fov_policy); } player_stock_view_fov_api;
typedef struct { int (*bind)(player_stock_view_ship_policy); } player_stock_view_ship_api;
typedef struct { int (*bind)(player_stock_view_component_policy); } player_stock_view_component_api;
typedef struct { int (*bind)(player_stock_view_precompare_policy); } player_stock_view_precompare_api;
typedef struct { int (*bind)(player_view_xs_target_policy, player_view_xs_external_policy, player_view_xs_distance_policy); } player_view_xs_preload_api;
typedef struct { const char *name; uintptr_t address; unsigned size; const char *hash; } lx_site;
static int site_ok(const lx_site *site) { return site != NULL; }
static struct { uint64_t issued; } stats;
static struct { uint64_t generation; } player_recurring_tail_token;
static struct { uintptr_t controller, ship; } factory_probe_identity;
static uintptr_t lifecycle_discard_ship;
static uintptr_t lifecycle_discard_controller;
static void *player_routes_scope;
static _Atomic unsigned player_physics_ready;
static unsigned player_recurring_physics = 3, player_recurring_tail = 1;
static struct { unsigned version; } startup = {1};
static const void *player_startup_ignored(void) { return &startup; }
#define player_startup() (&startup)
static int keyboard_probe_worker(void) { return 1; }
static int keyboard_probe_identity(void) { return 1; }
static int player_recurring_physics_entered(void) { return 1; }
static int player_physics_identity(void) { return 1; }
static int player_physics_identity_for(unsigned scoped) { (void)scoped; return 1; }
static int view_query_expected_library(const char **sha, uint64_t *size) { *sha = "sealed"; *size = 1; return 1; }
static int player_view_query_provider(const char *sha, uint64_t size, player_view_query_api *out) {
    (void)sha; (void)size; (void)out; return 1;
}
static int reject_provider, reject_bind;
static player_stock_view_mode_policy bound_mode;
static player_stock_view_fov_policy bound_fov;
static int bind_fake(player_stock_view_mode_policy mode, player_stock_view_fov_policy fov) {
    assert(!bound_mode && !bound_fov);
    if (reject_bind) return 0;
    bound_mode = mode; bound_fov = fov; return 1;
}
static int player_stock_view_fov_provider(const player_view_query_api *sealed, player_stock_view_fov_api *out) {
    (void)sealed; if (reject_provider) return 0; out->bind = bind_fake; return 1;
}
static player_stock_view_ship_policy bound_ship;
static int bind_ship_fake(player_stock_view_ship_policy policy) { bound_ship = policy; return 1; }
static int player_stock_view_ship_provider(const player_view_query_api *sealed, player_stock_view_ship_api *out) {
    (void)sealed; out->bind = bind_ship_fake; return 1;
}
static player_stock_view_component_policy bound_component;
static int reject_component_provider, reject_component_bind, reject_precompare_provider, reject_precompare_bind;
static player_stock_view_precompare_policy bound_precompare;
static int stock_view_component_gate_policy(unsigned site, uintptr_t component,
                                             unsigned char original, unsigned char *result) {
    (void)site; (void)component; *result = original; return 1;
}
static int stock_view_precompare_gate_policy(uintptr_t component, uintptr_t ancestor,
                                              uintptr_t original, uintptr_t *result) {
    (void)component; (void)ancestor; *result = original; return 1;
}
static int bind_precompare_fake(player_stock_view_precompare_policy policy) {
    if (reject_precompare_bind) return 0;
    bound_precompare = policy;
    return 1;
}
static int player_stock_view_precompare_provider(const player_view_query_api *sealed,
                                                   player_stock_view_precompare_api *out) {
    (void)sealed;
    if (reject_precompare_provider) return 0;
    out->bind = bind_precompare_fake;
    return 1;
}
static int bind_component_fake(player_stock_view_component_policy policy) {
    if (reject_component_bind) return 0;
    bound_component = policy;
    return 1;
}
static int player_stock_view_component_provider(const player_view_query_api *sealed,
                                                  player_stock_view_component_api *out) {
    (void)sealed;
    if (reject_component_provider) return 0;
    out->bind = bind_component_fake;
    return 1;
}
static int reject_xs_provider, reject_xs_bind, xs_bound;
static int player_view_xs_gate_target(uintptr_t ship, uintptr_t original, uintptr_t *out) {
    (void)ship; *out = original; return 1;
}
static int player_view_xs_gate_external(uint64_t original, uint64_t *out) { *out = original; return 1; }
static int player_view_xs_gate_distance(uintptr_t ship, uintptr_t camera, unsigned flags, float original, float *out) {
    (void)ship; (void)camera; (void)flags; *out = original; return 1;
}
static int bind_xs_fake(player_view_xs_target_policy target, player_view_xs_external_policy external,
                        player_view_xs_distance_policy distance) {
    if (reject_xs_bind) return 0;
    assert(target && external && distance); xs_bound = 1; return 1;
}
static int player_view_xs_preload_provider(const player_view_query_api *sealed, player_view_xs_preload_api *out) {
    (void)sealed; if (reject_xs_provider) return 0; out->bind = bind_xs_fake; return 1;
}
#include "gate_player_stock_view_fov.h"
static void *wrong_thread(void *unused) {
    (void)unused; int result = 0;
    assert(!bound_mode(0x10000, 1, 0, &result));
    assert(!bound_ship(0x40000, 1, &result));
    assert(bound_ship(0x60000, 7, &result) && result == 7);
    return NULL;
}
static void *observe_foreign(void *unused) {
    (void)unused; int result = 0;
    assert(bound_mode(0x10000, 7, 0, &result) && result == 7);
    return NULL;
}
int main(void) {
    (void)player_startup_ignored;
    const uint64_t saved[2] = {100, 200};
    stock_input_record records[2] = {{0}, {0}};
    assert(player_stock_view_prepare_slots(NULL, 40, saved, records)); /* default off */
    assert(!bound_mode && !bound_fov);
    stock_view_gate_mode = 0;
    assert(!setenv("X4_DEV_STOCK_VIEW_FOV", "1", 1));
    stats.issued = player_recurring_tail_token.generation = 40;
    factory_probe_identity.controller = 0x10000;
    lifecycle_discard_controller = 0x20000;
    factory_probe_identity.ship = 0x40000;
    lifecycle_discard_ship = 0x50000;
    atomic_store(&player_physics_ready, 1);
    records[0].capture16 = records[1].capture16 = 38;
    records[0].flags = records[1].flags = STOCK_INPUT_VIEW;
    records[0].view = 1; records[0].fov = 1.5f;
    records[1].view = 0; records[1].fov = 0.75f;
    assert(!setenv("X4_DEV_STOCK_VIEW_COMPONENT_POLICY", "1", 1));
    assert(!setenv("X4_DEV_VIEW_XS", "1", 1));
    player_stock_view_transaction rejected;
    assert(!player_stock_view_prepare_transaction(40, saved, records, &rejected));
    assert(!stock_view_gate_mode && !stock_view_gate_state.initialized && !stock_view_gate_bound && !xs_bound);
    records[1].view = 1;
    records[0].view = 0;
    assert(!player_stock_view_prepare_transaction(40, saved, records, &rejected));
    assert(!stock_view_gate_mode && !stock_view_gate_state.initialized && !stock_view_gate_bound && !xs_bound);
    records[0].view = 1;
    reject_xs_provider = 1;
    assert(!player_stock_view_prepare_slots(NULL, 40, saved, records));
    assert(!xs_bound && !stock_view_gate_bound);
    reject_xs_provider = 0;
    reject_component_provider = 1;
    assert(!player_stock_view_prepare_slots(NULL, 40, saved, records));
    assert(!bound_component && !stock_view_gate_bound);
    reject_component_provider = 0;
    reject_precompare_provider = 1;
    assert(!player_stock_view_prepare_slots(NULL, 40, saved, records));
    assert(!bound_component && !bound_precompare && !stock_view_gate_bound);
    reject_precompare_provider = 0;
    reject_provider = 1;
    assert(!player_stock_view_prepare_slots(NULL, 40, saved, records));
    assert(!stock_view_gate_state.initialized && !stock_view_gate_bound);
    reject_provider = 0; reject_bind = 1;
    assert(!player_stock_view_prepare_slots(NULL, 40, saved, records));
    assert(!stock_view_gate_state.initialized && !stock_view_gate_bound && !stock_view_gate_tid);
    reject_bind = 0;
    player_stock_view_transaction transaction;
    assert(player_stock_view_prepare_transaction(40, saved, records, &transaction));
    assert(!stock_view_gate_state.initialized && !stock_view_gate_bound && !bound_mode);
    ++stats.issued;
    assert(!player_stock_view_commit_transaction(&transaction));
    assert(!stock_view_gate_state.initialized && !stock_view_gate_bound && !bound_mode);
    --stats.issued;
    lifecycle_discard_ship = 0x60000;
    assert(!player_stock_view_commit_transaction(&transaction));
    assert(!stock_view_gate_state.initialized && !stock_view_gate_bound && !bound_mode);
    lifecycle_discard_ship = 0x50000;
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        transaction.tid = (uint32_t)syscall(SYS_gettid);
        reject_component_bind = 1;
        assert(!player_stock_view_commit_transaction(&transaction));
        assert(stock_view_gate_bound && stock_view_gate_state.generation == 40 && !bound_component);
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    child = fork();
    assert(child >= 0);
    if (!child) {
        transaction.tid = (uint32_t)syscall(SYS_gettid);
        reject_precompare_bind = 1;
        assert(!player_stock_view_commit_transaction(&transaction));
        assert(stock_view_gate_bound && bound_component && !bound_precompare);
        _exit(0);
    }
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    child = fork(); assert(child >= 0);
    if (!child) {
        transaction.tid = (uint32_t)syscall(SYS_gettid); reject_xs_bind = 1;
        assert(!player_stock_view_commit_transaction(&transaction));
        assert(stock_view_gate_bound && bound_component && bound_precompare && !xs_bound);
        _exit(0);
    }
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(player_stock_view_commit_transaction(&transaction));
    assert(xs_bound);
    assert(bound_component && bound_precompare && stock_view_gate_component_enabled && stock_view_gate_bound);
    assert(!unsetenv("X4_DEV_STOCK_VIEW_COMPONENT_POLICY"));
    assert(!player_stock_view_prepare_transaction(40, saved, records, &transaction));
    assert(!setenv("X4_DEV_STOCK_VIEW_COMPONENT_POLICY", "1", 1));
    int mode = -1; float value = 0;
    assert(bound_ship(0x40000, 7, &mode) && mode == 0);
    assert(bound_ship(0x50000, 0, &mode) && mode == 0);
    assert(bound_ship(0x60000, 7, &mode) && mode == 7);
    lifecycle_discard_ship = 0x60000;
    assert(!bound_ship(0x50000, 0, &mode));
    lifecycle_discard_ship = 0x50000;
    assert(bound_mode(0x10000, 7, 0, &mode) && mode == 0);
    assert(bound_mode(0x20000, 0, 1, &mode) && mode == 0);
    assert(bound_mode(0x30000, 7, 0, &mode) && mode == 7);
    assert(bound_fov(0x10000, 2.0f, &value) && value == 1.5f);
    assert(bound_fov(0x30000, 2.0f, &value) && value == 2.0f);
    pthread_t thread; assert(!pthread_create(&thread, NULL, wrong_thread, NULL));
    assert(!pthread_join(thread, NULL));
    ++stats.issued;
    assert(!bound_mode(0x10000, 0, 0, &mode)); /* stale owned generation */
    assert(!bound_ship(0x40000, 0, &mode));
    assert(!player_stock_view_prepare_slots(NULL, 41, saved, records));
    stock_view_gate_mode = 3;
    stats.issued = player_recurring_tail_token.generation = 40;
    assert(bound_mode(0x10000, 7, 0, &mode) && mode == 7);
    assert(bound_fov(0x20000, 2.0f, &value) && value == 2.0f);
    assert(!pthread_create(&thread, NULL, observe_foreign, NULL));
    assert(!pthread_join(thread, NULL));
    assert(atomic_load(&stock_view_gate_observations[0][0][1]) == 1);
    assert(atomic_load(&stock_view_gate_observations[0][0][3]) == 1);
    assert(atomic_load(&stock_view_gate_observations[2][1][1]) == 1);
    player_stock_view_observer_close(40);
    return 0;
}

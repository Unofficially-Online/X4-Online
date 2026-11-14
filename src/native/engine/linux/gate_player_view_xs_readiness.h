#ifndef X4_GATE_PLAYER_VIEW_XS_READINESS_H
#define X4_GATE_PLAYER_VIEW_XS_READINESS_H
#include "player_view_rewrite_config.h"

/* Written once at request on the owner before any binding. The bound provider
 * remains resident until process exit; its lightweight bound check revalidates
 * callwords and original slots without rehashing the shared object. */
static player_view_xs_preload_api player_view_xs_ready_api;
static unsigned player_view_xs_ready_verified;

/* Pure provider/setting checks: callable before the first VIEW transaction.
 * No bind, extra native getter or mutation is performed. */
static int player_view_xs_ready_before_apply(void) {
    if (player_view_xs_ready_verified)
        return 1;
    const player_view_rewrite_config config = {
        .rewrite = getenv("X4_DEV_STOCK_VIEW_FOV"),
        .observe = getenv("X4_DEV_STOCK_VIEW_FOV_OBSERVE"),
        .shared = getenv("X4_DEV_STOCK_VIEW_SHARED"),
        .component = getenv("X4_DEV_STOCK_VIEW_COMPONENT_POLICY"),
        .xs = getenv("X4_DEV_VIEW_XS"),
        .ancestry = getenv("X4_DEV_VIEW_ANCESTRY"),
        .component_trace = getenv("X4_DEV_VIEW_COMPONENT_TRACE"),
        .precompare_trace = getenv("X4_DEV_VIEW_PRECOMPARE_TRACE"),
        .cursor = getenv("X4_DEV_PLAYER_CURSOR"),
        .focus = getenv("X4_DEV_PLAYER_FOCUS"),
        .startup_version = player_startup()->version,
        .session_frames = player_session_budget.frames,
    };
    if (!config.rewrite || !player_view_rewrite_config_valid(&config))
        return 0;
    const char *sha = NULL;
    uint64_t size = 0;
    player_view_query_api sealed;
    player_stock_view_fov_api fov;
    player_stock_view_ship_api ship;
    player_stock_view_component_api component;
    player_stock_view_precompare_api precompare;
    player_view_xs_preload_api xs;
    static const lx_site transform = {
        "xs-transform", 0x18a0090, 0x3e0,
        "8489683612cc942bd00c117405c4b2ba6e02307198a653f05b574d2e62b08ccd"};
    if (!view_query_expected_library(&sha, &size) ||
        !player_view_query_provider(sha, size, &sealed) ||
        !player_stock_view_fov_provider(&sealed, &fov) ||
        !player_stock_view_ship_provider(&sealed, &ship) ||
        !player_stock_view_component_provider(&sealed, &component) ||
        !player_stock_view_precompare_provider(&sealed, &precompare) ||
        !player_view_xs_preload_provider(&sealed, &xs) || !xs.bound || !site_ok(&transform))
        return 0;
    player_view_xs_ready_api = xs;
    player_view_xs_ready_verified = 1;
    return 1;
}

/* Called on the owner after a successful frame transaction. The existing
 * coordinator fail-stops on any partial bind, so bound=1 means every preceding
 * provider bind completed; the XS provider additionally reports live ownership. */
static int player_view_xs_ready_at_generation(uint64_t generation) {
    if ((uint64_t)syscall(SYS_gettid) != stock_view_gate_tid || !generation ||
        !stock_view_gate_bound || !stock_view_gate_component_enabled ||
        !stock_view_gate_xs_enabled || stock_view_gate_mode != 2 || generation != stats.issued ||
        generation != player_recurring_tail_token.generation ||
        stock_view_gate_state.generation != generation || !player_recurring_physics_entered() ||
        !player_view_xs_ready_verified)
        return 0;
    for (unsigned i = 0; i < 2; ++i) {
        unsigned slot = 0;
        player_stock_view view;
        if (!stock_view_gate_owner(stock_view_gate_controllers[i], &slot) || slot != i + 1 ||
            factory_probe_identity.ship != stock_view_gate_ships[0] ||
            lifecycle_discard_ship != stock_view_gate_ships[1] ||
            !player_stock_view_read(&stock_view_gate_state, generation, slot,
                                    stock_view_gate_state.saved[i], &view))
            return 0;
    }
    return player_view_xs_ready_api.bound && player_view_xs_ready_api.bound() == 1;
}
#endif

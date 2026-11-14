#include <assert.h>
#include "player_view_rewrite_config.h"
int main(void) {
    player_view_rewrite_config config = {0};
    assert(player_view_rewrite_config_valid(&config));
    config.xs = "1";
    assert(!player_view_rewrite_config_valid(&config));
    config = (player_view_rewrite_config){.rewrite="1", .shared="1", .component="1", .xs="1",
        .ancestry="1", .component_trace="1", .precompare_trace="1", .cursor="1", .focus="1",
        .startup_version=1, .session_frames=180};
    assert(player_view_rewrite_config_valid(&config));
    config.observe="1"; assert(!player_view_rewrite_config_valid(&config)); config.observe=0;
    config.precompare_trace=0; assert(!player_view_rewrite_config_valid(&config)); config.precompare_trace="1";
    config.cursor="0"; assert(!player_view_rewrite_config_valid(&config)); config.cursor="1";
    config.session_frames=179; assert(!player_view_rewrite_config_valid(&config)); config.session_frames=180;
    config.startup_version=0; assert(!player_view_rewrite_config_valid(&config)); config.startup_version=1;
    config.rewrite="2"; assert(!player_view_rewrite_config_valid(&config));
    return 0;
}

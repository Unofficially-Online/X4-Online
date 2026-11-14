#ifndef X4_PLAYER_VIEW_REWRITE_CONFIG_H
#define X4_PLAYER_VIEW_REWRITE_CONFIG_H
#include <string.h>

typedef struct {
    const char *rewrite;
    const char *observe;
    const char *shared;
    const char *component;
    const char *xs;
    const char *ancestry;
    const char *component_trace;
    const char *precompare_trace;
    const char *cursor;
    const char *focus;
    unsigned startup_version;
    unsigned session_frames;
} player_view_rewrite_config;

static inline int player_view_rewrite_one(const char *value) {
    return value && !strcmp(value, "1");
}
static inline int player_view_rewrite_config_valid(const player_view_rewrite_config *config) {
    if (!config)
        return 0;
    if (!config->rewrite)
        return !config->component && !config->xs;
    return player_view_rewrite_one(config->rewrite) && !config->observe &&
           player_view_rewrite_one(config->shared) && player_view_rewrite_one(config->component) &&
           player_view_rewrite_one(config->xs) && player_view_rewrite_one(config->ancestry) &&
           player_view_rewrite_one(config->component_trace) &&
           player_view_rewrite_one(config->precompare_trace) &&
           player_view_rewrite_one(config->cursor) && player_view_rewrite_one(config->focus) &&
           config->startup_version == 1 && config->session_frames >= 180 &&
           config->session_frames <= 900;
}
#endif

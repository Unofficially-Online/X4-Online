#ifndef X4_PLAYER_STOCK_VIEW_PRECOMPARE_PRELOAD_H
#define X4_PLAYER_STOCK_VIEW_PRECOMPARE_PRELOAD_H
#include <stdint.h>

/* The policy receives exact R12 component, RBX ancestor and the untouched
 * original getter result. Return 1 after writing the desired complete pointer;
 * return 0 to fail-stop. The gate owns identity and generation decisions. */
typedef int (*player_stock_view_precompare_policy)(uintptr_t component, uintptr_t ancestor,
                                                   uintptr_t original, uintptr_t *result);
int x4mp_stock_view_precompare_bind(player_stock_view_precompare_policy policy);
#endif

#ifndef X4_PLAYER_STOCK_VIEW_SHARED_INSTALL_H
#define X4_PLAYER_STOCK_VIEW_SHARED_INSTALL_H
/* Ancestry checks every original function hash and callword before the first
 * edit. Ship checks that the sole prior entry edit is ours and reconstructs
 * the full original hash before its own CAS. Any failure is terminal in the
 * constructor; these resident hooks are never hot-uninstalled. */
static int install_stock_view_shared(void) {
    if (!view_ancestry_install() || !install_stock_view_ship()) return 0;
    player_stock_view_component_info info = {0};
    info.version = 2; /* shared page also contains the ancestry ship stub */
    info.installed = 1;
    info.page = view_ancestry_information.stubs[0];
    for (unsigned site = 0; site < 2; ++site) {
        info.bridge[site] = view_ancestry_information.dispatchers[site];
        info.dispatch[site] = site ? (uintptr_t)view_ancestry_engine_dispatch :
                                     (uintptr_t)view_ancestry_weapon_dispatch;
        info.original_slot[site] = view_ancestry_information.original_slots[site];
        info.frame[site] = view_ancestry_information.frames[site];
        info.original[site] = view_ancestry_information.original_words[site];
        info.patched[site] = view_ancestry_information.patched_words[site];
    }
    stock_view_component_information = info;
    atomic_store_explicit(&stock_view_component_ready, 1, memory_order_release);
    return 1;
}
#endif

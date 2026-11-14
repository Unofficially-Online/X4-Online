#ifndef X4_PLAYER_STOCK_VIEW_COMPONENT_PRELOAD_IMPL_H
#define X4_PLAYER_STOCK_VIEW_COMPONENT_PRELOAD_IMPL_H
/* Sealed pre-main provider only. Any failed installation is terminal; no
 * hot installation/uninstallation or reclamation of resident stubs. */
#include "player_stock_view_component_preload.h"
#include "player_stock_view_policy_guard.h"
static const uintptr_t stock_view_component_blocks[2] = {0x1113338, 0x177f9c0};
static const unsigned stock_view_component_offsets[2] = {1, 0};
static const uint64_t stock_view_component_words[2] = {
    UINT64_C(0x835b00c57e82e818), UINT64_C(0x75c0840068765be8)
};
static uint64_t (*stock_view_component_original[2])(void) = {
    (uint64_t (*)(void))0x1d6b1c0, (uint64_t (*)(void))0x1e07020
};
static _Atomic unsigned stock_view_component_ready, stock_view_component_active, stock_view_component_inflight;
static player_stock_view_component_policy stock_view_component_policy;
static _Thread_local unsigned stock_view_component_depth;
static player_stock_view_component_info stock_view_component_information;
static unsigned char stock_view_component_frames[2][48] __attribute__((aligned(8)));
extern void player_stock_view_weapon_bridge(void) __attribute__((visibility("hidden")));
extern void player_stock_view_engine_bridge(void) __attribute__((visibility("hidden")));

/* The entry and finish pair is also used by the ancestry dispatcher. The
 * native call sits between them and must be made exactly once by its owner. */
static unsigned stock_view_component_enter(void) {
    unsigned state = stock_view_policy_enter(&stock_view_component_active, &stock_view_component_inflight);
    unsigned active = state == 1;
    if (active && stock_view_component_depth++) _exit(223);
    return active;
}
static uint64_t stock_view_component_finish(unsigned active, unsigned site, uintptr_t component, uint64_t raw) {
    if (active) {
        unsigned char result = (unsigned char)raw;
        if (!stock_view_component_policy(site, component, (unsigned char)raw, &result) ||
            (result != (unsigned char)raw && result > 1)) _exit(223);
        raw = (raw & ~UINT64_C(0xff)) | result;
        --stock_view_component_depth;
    }
    stock_view_policy_leave(&stock_view_component_inflight);
    return raw;
}
static uint64_t stock_view_component_dispatch(unsigned site, uintptr_t component) {
    int before = errno;
    unsigned active = stock_view_component_enter();
    errno = before;
    uint64_t raw = stock_view_component_original[site]();
    int after = errno;
    raw = stock_view_component_finish(active, site, component, raw);
    errno = after;
    return raw;
}
__attribute__((visibility("hidden"), noinline)) uint64_t player_stock_view_weapon_dispatch(uintptr_t component) {
    return stock_view_component_dispatch(0, component);
}
__attribute__((visibility("hidden"), noinline)) uint64_t player_stock_view_engine_dispatch(uintptr_t component) {
    return stock_view_component_dispatch(1, component);
}
EXPORT int x4mp_stock_view_component_info(player_stock_view_component_info *out) {
    if (!out || !atomic_load_explicit(&stock_view_component_ready, memory_order_acquire)) return 0;
    *out = stock_view_component_information;
    return 1;
}
EXPORT int x4mp_stock_view_component_bind(player_stock_view_component_policy policy) {
    if (!policy || !atomic_load_explicit(&stock_view_component_ready, memory_order_acquire)) return 0;
    if (!stock_view_policy_begin_bind(&stock_view_component_active, &stock_view_component_inflight)) return 0;
    stock_view_component_policy = policy;
    atomic_store_explicit(&stock_view_component_active, 1, memory_order_seq_cst);
    return 1;
}
static __attribute__((unused)) int install_stock_view_components(void) {
    /* The ancestry observer occupies these same call sites. They cannot both
     * install; byte ownership catches either ordering without blind restore. */
    static const struct { uintptr_t address; unsigned size; const char *hash; } pins[] = {
        {0x11132d0, 133, "48dd904179d64a8b5f75a87b5b989a3f7c215342c71ac5132f07b7b86b8d3b58"},
        {0x1113360, 76, "5798f27c36fee251792a259c2e5b74ba1e647907ea96fb177bcccb73b8535fdc"},
        {0x177f900, 224, "d46a2e354ebd8f76037356afe53a778117f45401b2dfd3c64727ae42e8dfd0ef"},
        {0x1d6b1c0, 99, "28662e2439a706b8794ec0c9dbe9b2b31aef33695461446cfbba6466a58967f6"},
        {0x1e07020, 51, "7fbd4e7bdb604524025c4a46cc791ae7137b7c1f19270213781e35e5b7890f51"}
    };
    if (!single_thread()) return 0;
    for (unsigned i = 0; i < sizeof pins / sizeof pins[0]; ++i) {
        unsigned char hash[32];
        if (!is_rx(pins[i].address) || !is_rx(pins[i].address + pins[i].size - 1)) return 0;
        lockstep_sha256((void *)pins[i].address, pins[i].size, hash);
        if (!digest_matches(hash, pins[i].hash)) return 0;
    }
    for (unsigned i = 0; i < 2; ++i)
        for (unsigned byte = 0; byte < 8; ++byte)
            if (((volatile const unsigned char *)stock_view_component_blocks[i])[byte] !=
                ((const unsigned char *)&stock_view_component_words[i])[byte]) return 0;
    unsigned char *page = allocate_near();
    if (!page) return 0;
    memset(page, 0xcc, PVQ_PAGE);
    uintptr_t bridges[2] = {(uintptr_t)player_stock_view_weapon_bridge, (uintptr_t)player_stock_view_engine_bridge};
    uint64_t patched[2];
    for (unsigned i = 0; i < 2; ++i) {
        emit_entry_stub(page + i * 16, bridges[i]);
        uintptr_t instruction = stock_view_component_blocks[i] + stock_view_component_offsets[i];
        int64_t distance = (int64_t)(page + i * 16) - (int64_t)(instruction + 5);
        if (distance < INT32_MIN || distance > INT32_MAX) return 0;
        unsigned char bytes[8]; memcpy(bytes, &stock_view_component_words[i], 8);
        bytes[stock_view_component_offsets[i]] = 0xe8;
        int32_t relative = (int32_t)distance;
        memcpy(bytes + stock_view_component_offsets[i] + 1, &relative, 4);
        memcpy(&patched[i], bytes, 8);
    }
    if (mprotect(page, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx((uintptr_t)page) ||
        !register_frame_for_entry((uintptr_t)page, stock_view_component_frames[0]) ||
        !register_frame_for_entry((uintptr_t)page + 16, stock_view_component_frames[1])) return 0;
    uintptr_t pages[2] = {stock_view_component_blocks[0] & ~(uintptr_t)(PVQ_PAGE - 1),
                          stock_view_component_blocks[1] & ~(uintptr_t)(PVQ_PAGE - 1)};
    if (!single_thread() || mprotect((void *)pages[0], PVQ_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC)) return 0;
    if (mprotect((void *)pages[1], PVQ_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        (void)mprotect((void *)pages[0], PVQ_PAGE, PROT_READ | PROT_EXEC);
        return 0;
    }
    unsigned installed = 0;
    for (; installed < 2; ++installed) {
#ifdef STOCK_VIEW_COMPONENT_TEST
        stock_view_component_test_before_cas(installed);
#endif
        uint64_t expected = stock_view_component_words[installed];
        if (!__atomic_compare_exchange_n((uint64_t *)stock_view_component_blocks[installed], &expected,
                                         patched[installed], 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) break;
    }
    int success = installed == 2;
    if (!success) while (installed) {
        --installed;
        uint64_t expected = patched[installed];
        (void)__atomic_compare_exchange_n((uint64_t *)stock_view_component_blocks[installed], &expected,
                                          stock_view_component_words[installed], 0,
                                          __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }
    for (unsigned i = 0; i < 2; ++i)
        __builtin___clear_cache((char *)pages[i], (char *)(pages[i] + PVQ_PAGE));
    int first_protection = mprotect((void *)pages[0], PVQ_PAGE, PROT_READ | PROT_EXEC);
    int second_protection = mprotect((void *)pages[1], PVQ_PAGE, PROT_READ | PROT_EXEC);
    if (first_protection || second_protection || !success || !is_rx(pages[0]) || !is_rx(pages[1])) return 0;
    stock_view_component_information = (player_stock_view_component_info){
        .version = 1, .installed = 1, .page = (uintptr_t)page,
        .bridge = {bridges[0], bridges[1]},
        .dispatch = {(uintptr_t)player_stock_view_weapon_dispatch, (uintptr_t)player_stock_view_engine_dispatch},
        .original_slot = {(uintptr_t)&stock_view_component_original[0], (uintptr_t)&stock_view_component_original[1]},
        .frame = {(uintptr_t)stock_view_component_frames[0], (uintptr_t)stock_view_component_frames[1]}
    };
    memcpy(stock_view_component_information.original, stock_view_component_words, sizeof patched);
    memcpy(stock_view_component_information.patched, patched, sizeof patched);
    atomic_store_explicit(&stock_view_component_ready, 1, memory_order_release);
    return 1;
}
#endif

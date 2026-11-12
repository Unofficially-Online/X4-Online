#ifndef X4_GATE_CONTEXT_REFRESH_EDGES_H
#define X4_GATE_CONTEXT_REFRESH_EDGES_H

/* Default-off integration helper. Include after the Linux gate and
 * context_refresh_edges.h. Installation and restoration require an external
 * boundary excluding execution of either patched site and its wrappers. The
 * page and all wrapper state remain resident, including after restoration.
 * This manages only the destructor refresh edge, not controller or bank ownership. */
#include <stdbool.h>
#include "context_refresh_edges.h"

#define CRE_PAGE_SIZE 4096u
#define CRE_EDGE_COUNT 1u
static const uint64_t cre_original_words[CRE_EDGE_COUNT] = {
    UINT64_C(0x4800011559e8ff31)
};
static const uint64_t cre_word_vas[CRE_EDGE_COUNT] = {UINT64_C(0x164c300)};

typedef struct {
    unsigned char *page;
    uint64_t patched[CRE_EDGE_COUNT];
    unsigned owned[CRE_EDGE_COUNT];
    unsigned permission_pending[CRE_EDGE_COUNT];
    unsigned thunk_permission_pending;
    unsigned uncertain;
    unsigned installed;
} cre_installation;
static cre_installation cre_resident;

/* Native refresh is void(bool), not the synthetic test callback type. */
static uintptr_t cre_native_original(uintptr_t value) {
    ((void (*)(bool))LX(CRE_REFRESH_VA))(value != 0);
    return 0;
}
static void cre_destruct_wrapper(bool value, const void *actual_controller) {
    (void)cre_dispatch(CRE_DESTRUCT, value, actual_controller, cre_native_original);
}
/* Put only this function in the owned P2 controller's private vtable slot 42.
 * The native virtual accepts controller and pointer to the owner reference. */
#ifndef CRE_BIND_NATIVE
#define CRE_BIND_NATIVE(controller, owner) \
    ((void (*)(void *, const void *))LX(CRE_BIND_HELPER_VA))((controller), (owner))
#endif
static void cre_bind_wrapper(void *controller, const void *owner) {
    CRE_BIND_NATIVE(controller, owner);
    (void)cre_dispatch(CRE_BIND, 1, controller, cre_native_original);
}

/* The operation hooks below use the existing gate functions. Tests define
 * CRE_INSTALL_TEST to provide deterministic fault-injectable equivalents. */
#ifndef CRE_INSTALL_TEST
static int cre_image_pinned(void) {
    unsigned char digest[32]; char hex[65];
    static const lx_site sites[] = {
        {"refresh", CRE_REFRESH_VA, 235, CRE_REFRESH_SHA256},
        {"destructor", 0x164c1f0, 1268, CRE_DESTRUCTOR_SHA256},
        {"bind", CRE_BIND_VA, 23, CRE_BIND_SHA256},
        {"bind-helper", CRE_BIND_HELPER_VA, 97, CRE_BIND_HELPER_SHA256}
    };
    if (!gate_file_sha256(gate_env.exe, LX_FILE_SIZE, digest)) return 0;
    hex32(digest, hex);
    /* verify_image() is a pre-install check: it expects the stock FrameTick
     * call. Here that call must instead still be our active, owned hook. */
    if (strcmp(hex, CRE_IMAGE_SHA256) || !installed || !stats.active || !thunk ||
        call_target() != thunk ||
        memcmp(LX(LX_CALL_WINDOW), &call_patched, sizeof(call_patched))) return 0;
    for (unsigned i = 0; i < 4; ++i) if (!site_ok(&sites[i])) return 0;
    return 1;
}
static unsigned char *cre_allocate(void *near_address) { return gate_near_page(near_address); }
static int cre_thunk_rx(void *page) {
    return mprotect(page, CRE_PAGE_SIZE, PROT_READ | PROT_EXEC) == 0;
}
static int cre_is_rx(void *address) {
    char permissions[5];
    return gate_maps_find((uintptr_t)address, permissions, NULL, 0) &&
           !strcmp(permissions, "r-xp");
}
static int cre_open(void *address) { return gate_code_open(address, 8); }
static int cre_close(void *address) { return gate_code_close(address); }
static int cre_compare_exchange(uint64_t *address, uint64_t expected, uint64_t desired) {
    int changed = __atomic_compare_exchange_n(address, &expected, desired, 0,
                                               __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    if (changed) __builtin___clear_cache((char *)address, (char *)address + 8);
    return changed;
}
static void cre_lock(void) { pthread_mutex_lock(&gate_code_lock); }
static void cre_unlock(void) { pthread_mutex_unlock(&gate_code_lock); }
#endif

/* Obligations are recorded before the first mutation, including open pages.
 * On a collision, never overwrite the current word. */
static int cre_swap(cre_installation *state, unsigned index, int install) {
    uint64_t *address = (uint64_t *)LX(cre_word_vas[index]);
    uint64_t from = install ? cre_original_words[index] : state->patched[index];
    uint64_t to = install ? state->patched[index] : cre_original_words[index];
    cre_lock();
    if (!cre_is_rx(address)) { cre_unlock(); return 0; }
    state->permission_pending[index] = 1;
    if (!cre_open(address)) {
        state->permission_pending[index] = 0;
        cre_unlock();
        return 0;
    }
    unsigned previous_owned = state->owned[index];
    state->owned[index] = 1;
    int changed = cre_compare_exchange(address, from, to);
    state->owned[index] = changed ? (unsigned)install : previous_owned;
    int closed = cre_close(address);
    if (closed) state->permission_pending[index] = 0;
    else state->uncertain = 1;
    cre_unlock();
    return changed && closed;
}

static int cre_restore(cre_installation *state) {
    if (!state) return 0;
    state->installed = 0;
    for (unsigned i = 0; i < CRE_EDGE_COUNT; ++i) {
        if (state->permission_pending[i]) {
            cre_lock();
            if (cre_close(LX(cre_word_vas[i]))) state->permission_pending[i] = 0;
            else state->uncertain = 1;
            cre_unlock();
        }
        if (state->owned[i] && !state->permission_pending[i] && !cre_swap(state, i, 0))
            state->uncertain = 1;
    }
    if (state->thunk_permission_pending) {
        if (cre_thunk_rx(state->page)) state->thunk_permission_pending = 0;
        else state->uncertain = 1;
    }
    for (unsigned i = 0; i < CRE_EDGE_COUNT; ++i)
        if (state->owned[i] || state->permission_pending[i]) return 0;
    return !state->thunk_permission_pending && !state->uncertain;
}

static int cre_install(cre_installation *state) {
    if (!state || state != &cre_resident || state->page || state->installed ||
        state->uncertain || !cre_image_pinned()) return 0;
    for (unsigned i = 0; i < CRE_EDGE_COUNT; ++i) {
        uint64_t *word = (uint64_t *)LX(cre_word_vas[i]);
        if (((uintptr_t)word & 7) || !cre_is_rx(word) ||
            __atomic_load_n(word, __ATOMIC_SEQ_CST) != cre_original_words[i]) return 0;
    }
    state->page = cre_allocate(LX(CRE_DESTRUCTOR_EDGE_VA));
    if (!state->page) return 0;
    state->thunk_permission_pending = 1;
    cre_emit_destructor_jump(state->page, (uintptr_t)cre_destruct_wrapper);
    if (!cre_thunk_rx(state->page)) { state->uncertain = 1; return 0; }
    state->thunk_permission_pending = 0;
    __builtin___clear_cache((char *)state->page, (char *)state->page + 17);
    if (!cre_is_rx(state->page)) { state->uncertain = 1; return 0; }
    for (unsigned i = 0; i < CRE_EDGE_COUNT; ++i) {
        unsigned char edge[5];
        uintptr_t site = (uintptr_t)LX(cre_word_vas[i] + 2);
        if (!cre_encode_edge(edge, 0xe8, site,
                             (uintptr_t)state->page)) return 0;
        state->patched[i] = cre_original_words[i];
        memcpy((unsigned char *)&state->patched[i] + 2, edge, 5);
    }
    for (unsigned i = 0; i < CRE_EDGE_COUNT; ++i) {
        if (!cre_swap(state, i, 1)) {
            (void)cre_restore(state);
            return 0;
        }
    }
    state->installed = 1;
    return 1;
}
#endif

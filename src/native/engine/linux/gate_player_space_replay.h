#ifndef X4_GATE_PLAYER_SPACE_REPLAY_H
#define X4_GATE_PLAYER_SPACE_REPLAY_H
/* SPACE/context-8 callback coordinator. This header has no engine access or
 * installed global hook: the owner supplies all operations and an exact native
 * trampoline. A successful vector CAS is process-resident; no uninstall or
 * retry is attempted. Failed validation after installation is fatal.
 *
 * The boundary provider must prove the actual InputOwner/vector allocation is
 * stable against registration, freeing and callback execution throughout
 * install. A snapshot/CAS does not establish that exclusion. In callback mode
 * it must authenticate the InputBase::Move worker, CAPTURE generation and
 * callback depth; it must not interpret CF0 as updated until the original
 * dispatcher has returned from all callbacks. Once the first capture starts,
 * a context-8 callback before worker binding is INVALID, never PREACTIVE. */
#include "player_space_context_vector.h"
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

typedef enum {
    PLAYER_SPACE_PHASE_PREACTIVE = 0,
    PLAYER_SPACE_PHASE_CAPTURE = 1,
    PLAYER_SPACE_PHASE_INVALID = 2
} player_space_replay_phase;

typedef struct {
    void *context;
    player_space_vector_ops vector;
    int (*owner)(void *, uintptr_t *owner);
    int (*postjoin)(void *, uint64_t generation, uintptr_t owner);
    int (*callback_phase)(void *, uint64_t *generation, player_space_replay_phase *phase);
    int (*local_state)(void *, unsigned *active);
    int (*slot_state)(void *, unsigned slot, unsigned *active);
    int (*cf0)(void *, uintptr_t owner, unsigned *active);
    int (*local)(void *, uint64_t generation, unsigned active);
    int (*queue_edge)(void *, uint64_t generation, unsigned active);
    int (*refresh)(void *, uint64_t generation);
    void (*original)(void *, unsigned context, unsigned char active);
    void (*fatal)(void *, const char *reason);
} player_space_replay_ops;

typedef struct {
    player_space_vector_installation install;
    player_space_vector_snapshot installed_vector;
    uintptr_t owner;
    uint64_t install_generation;
    unsigned requested, configured, attempted, installed;
    _Atomic unsigned callback_busy;
} player_space_replay_state;

static inline _Noreturn void player_space_replay_fatal(const player_space_replay_ops *ops, const char *reason) {
    if (ops && ops->fatal) ops->fatal(ops->context, reason);
    _exit(223);
}
static inline int player_space_replay_ops_valid(const player_space_replay_ops *ops) {
    return ops && ops->owner && ops->postjoin && ops->callback_phase && ops->local_state &&
        ops->slot_state && ops->cf0 && ops->local && ops->queue_edge && ops->refresh &&
        ops->original && ops->fatal && ops->vector.range && ops->vector.read && ops->vector.cas;
}
/* Called during the explicit independent-player request. An absent setting is
 * inert. The install generation is the first admitted main boundary before capture;
 * the worker must bind its identity before accepting any SPACE callback.
 * The owner must independently reject any unrecorded pre-install SPACE edge. */
static inline int player_space_replay_request(player_space_replay_state *state, const char *setting,
        unsigned independent, unsigned cursor, unsigned startup_version, uint64_t install_generation) {
    if (!state || state->configured || state->requested || state->attempted || state->installed) return 0;
    atomic_init(&state->callback_busy, 0);
    if (!setting) {
        state->configured = 1;
        return 1;
    }
    if (strcmp(setting,"context8") || independent!=1 || cursor!=1 || startup_version!=1 || !install_generation) return 0;
    state->requested = 1;
    state->configured = 1;
    state->install_generation = install_generation;
    return 1;
}
static inline int player_space_replay_fresh(const player_space_replay_state *state,
                                             const player_space_replay_ops *ops) {
    uintptr_t owner = 0;
    player_space_vector_snapshot now;
    return state && state->installed && state->install.verified &&
        ops->owner(ops->context,&owner)==1 && owner==state->owner &&
        player_space_vector_snapshot_read(&ops->vector,owner,&now)==PLAYER_SPACE_VECTOR_OK &&
        player_space_vector_same(&state->installed_vector,&now);
}
/* Exactly one main/postjoin installation attempt. Both the callback vector and
 * InputOwner must be re-read before CAS and after it, while the owning boundary
 * holds registration and callback-use exclusion. */
static inline int player_space_replay_mainboundary(player_space_replay_state *state,
        const player_space_replay_ops *ops,uint64_t generation,uint64_t original,uint64_t wrapper) {
    if (!state || !state->configured) return 0;
    if (!state->requested) return 1;
    if (!player_space_replay_ops_valid(ops)) player_space_replay_fatal(ops,"space_ops");
    if (state->installed) {
        if (!player_space_replay_fresh(state,ops)) player_space_replay_fatal(ops,"space_registry_drift");
        return 1;
    }
    if (generation < state->install_generation) return 1;
    if (generation != state->install_generation || state->attempted)
        player_space_replay_fatal(ops,"space_install_epoch");
    state->attempted=1;
    uintptr_t owner = 0, owner_after = 0;
    unsigned expected = 0, actual = 0;
    player_space_vector_snapshot before;
    if (ops->owner(ops->context,&owner)!=1 || !owner || ops->postjoin(ops->context,generation,owner)!=1 ||
        ops->local_state(ops->context,&expected)!=1 || expected>1 ||
        ops->cf0(ops->context,owner,&actual)!=1 || actual!=expected)
        player_space_replay_fatal(ops,"space_install_boundary");
    if (player_space_vector_lookup(&ops->vector,owner,original,wrapper,&before)!=PLAYER_SPACE_VECTOR_OK ||
        player_space_vector_install(&ops->vector,owner,original,wrapper,&state->install)!=PLAYER_SPACE_VECTOR_OK ||
        ops->owner(ops->context,&owner_after)!=1 || owner_after!=owner ||
        ops->postjoin(ops->context,generation,owner)!=1 ||
        player_space_vector_snapshot_read(&ops->vector,owner,&state->installed_vector)!=PLAYER_SPACE_VECTOR_OK ||
        state->installed_vector.begin!=state->install.begin || state->installed_vector.end!=state->install.end ||
        state->installed_vector.capacity!=state->install.capacity ||
        state->install.slot<state->installed_vector.begin || state->install.slot>=state->installed_vector.end ||
        before.slot!=state->install.slot)
        player_space_replay_fatal(ops,"space_install_failed");
    before.entries[(before.slot-before.begin)/8]=wrapper;
    if (!player_space_vector_same(&before,&state->installed_vector))
        player_space_replay_fatal(ops,"space_install_registry_drift");
    state->owner = owner;
    state->installed = 1;
    return 1;
}
static inline int player_space_replay_capture_fresh(const player_space_replay_state *state,
                                                      const player_space_replay_ops *ops,uint64_t generation) {
    uint64_t current = 0;
    player_space_replay_phase phase = PLAYER_SPACE_PHASE_INVALID;
    return player_space_replay_fresh(state,ops) &&
        ops->callback_phase(ops->context,&current,&phase)==1 &&
        phase==PLAYER_SPACE_PHASE_CAPTURE && current==generation;
}
/* Root-owned native trampoline calls this once per stock callback. The native
 * original is invoked exactly once for non-SPACE and pre-active dispatch; an
 * admitted SPACE edge is never sent to original shared-physics logic. */
static inline void player_space_replay_callback(player_space_replay_state *state, const player_space_replay_ops *ops,
                                                 unsigned context, unsigned char active) {
    if (!state || !state->installed || !player_space_replay_ops_valid(ops)) player_space_replay_fatal(ops,"space_not_installed");
    unsigned expected_busy = 0;
    if (!atomic_compare_exchange_strong_explicit(&state->callback_busy,&expected_busy,1,memory_order_acquire,memory_order_relaxed))
        player_space_replay_fatal(ops,"space_callback_reentry");
    if (!player_space_replay_fresh(state,ops)) player_space_replay_fatal(ops,"space_callback_owner");
    uint64_t generation = 0;
    player_space_replay_phase phase = PLAYER_SPACE_PHASE_INVALID;
    if (context!=8) {
        ops->original(ops->context,context,active);
        if (!player_space_replay_fresh(state,ops)) player_space_replay_fatal(ops,"space_original_drift");
    } else {
        if (ops->callback_phase(ops->context,&generation,&phase)!=1 || phase==PLAYER_SPACE_PHASE_INVALID)
            player_space_replay_fatal(ops,"space_callback_phase");
        if (phase==PLAYER_SPACE_PHASE_PREACTIVE) {
            ops->original(ops->context,context,active);
            if (!player_space_replay_fresh(state,ops)) player_space_replay_fatal(ops,"space_original_drift");
            atomic_store_explicit(&state->callback_busy,0,memory_order_release);
            return;
        }
        unsigned before = 0, cf0 = 0, after = 0;
        if (phase!=PLAYER_SPACE_PHASE_CAPTURE || active>1 || !generation ||
            ops->local_state(ops->context,&before)!=1 || before>1 || active==before ||
            ops->cf0(ops->context,state->owner,&cf0)!=1 || cf0!=before)
            player_space_replay_fatal(ops,"space_edge_admission");
        if (ops->local(ops->context,generation,active)!=1 || !player_space_replay_capture_fresh(state,ops,generation) ||
            ops->cf0(ops->context,state->owner,&cf0)!=1 || cf0!=before)
            player_space_replay_fatal(ops,"space_local_failed");
        if (ops->queue_edge(ops->context,generation,active)!=1 || !player_space_replay_capture_fresh(state,ops,generation) ||
            ops->local_state(ops->context,&after)!=1 || after!=active ||
            ops->cf0(ops->context,state->owner,&cf0)!=1 || cf0!=before)
            player_space_replay_fatal(ops,"space_queue_failed");
        if (ops->refresh(ops->context,generation)!=1 || !player_space_replay_capture_fresh(state,ops,generation) ||
            ops->cf0(ops->context,state->owner,&cf0)!=1 || cf0!=before)
            player_space_replay_fatal(ops,"space_refresh_failed");
    }
    atomic_store_explicit(&state->callback_busy,0,memory_order_release);
}
static inline int player_space_replay_expected_local(const player_space_replay_ops *ops,unsigned *active) {
    return ops && ops->local_state && active && ops->local_state(ops->context,active)==1 && *active<=1;
}
static inline int player_space_replay_expected_slot(const player_space_replay_ops *ops,unsigned slot,unsigned *active) {
    return ops && ops->slot_state && active && slot>=1 && slot<=2 && ops->slot_state(ops->context,slot,active)==1 && *active<=1;
}
#endif

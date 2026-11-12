#ifndef X4_PLAYER_CONTEXT_POLICY_H
#define X4_PLAYER_CONTEXT_POLICY_H

#include "gate_player_camera_context_anchor.h"

/* Engine-free policy for one serialized worker owner. The caller must prove
 * object lifetime, reader exclusion, and the claimed outside/scope boundaries.
 * This policy does not acquire engine locks or make those claims itself. */
typedef struct {
    unsigned initialized;
    unsigned failed;
    unsigned scope_open;
    unsigned help_present;
    unsigned scope_count;
    unsigned base_count;
    uint64_t thread;
    uint64_t last_generation;
    uint64_t transitions;
    uintptr_t owner;
    uintptr_t manager;
    uintptr_t ship;
    uintptr_t controller;
    unsigned char adopted[15][16];
    unsigned char scoped[16][16];
} player_context_policy;

static inline int player_context_policy_fail(player_context_policy *policy) {
    if (policy) {
        policy->failed = 1;
    }
    return 0;
}

/* Caller authenticates the source: either the camera anchor or the initial
 * canonical census. Zero-initialize and serialize all calls on the worker. */
static inline int player_context_policy_init(player_context_policy *policy,
        unsigned expected_base_count, uint64_t thread, uintptr_t owner,
        uintptr_t ship, uintptr_t controller, int source_authenticated,
        const input_context_census_sample *sample, const input_context_census_entries *entries) {
    if (!policy || policy->initialized || policy->failed || source_authenticated != 1 ||
        (expected_base_count != 13 && expected_base_count != 15) || !thread || !owner ||
        owner > UINTPTR_MAX - 0x10 || !ship || !controller || !sample ||
        sample->manager != owner + 0x10 ||
        !camera_context_complete(sample, entries, expected_base_count)) {
        return player_context_policy_fail(policy);
    }
    for (unsigned index = 0; index < expected_base_count; ++index) {
        if (camera_context_key(entries->payloads[index], 59, 8)) {
            return player_context_policy_fail(policy);
        }
    }
    policy->thread = thread;
    policy->owner = owner;
    policy->manager = sample->manager;
    policy->ship = ship;
    policy->controller = controller;
    policy->base_count = expected_base_count;
    memcpy(policy->adopted, entries->payloads, expected_base_count * 16u);
    policy->initialized = 1;
    return 1;
}

static inline int player_context_policy_identity(const player_context_policy *policy,
        uint64_t thread, uintptr_t owner, uintptr_t ship, uintptr_t controller,
        const input_context_census_sample *sample) {
    return policy && policy->initialized && !policy->failed && sample &&
        thread == policy->thread && owner == policy->owner && ship == policy->ship &&
        controller == policy->controller && sample->manager == policy->manager;
}

/* Compare the full ordered payloads, including the three opaque tail bytes.
 * Help text is the only admitted additional entry; no requester-wide wildcard. */
static inline int player_context_policy_classify(const player_context_policy *policy,
        const input_context_census_sample *sample, const input_context_census_entries *entries,
        unsigned *help_present) {
    static const unsigned char help[16] = {0x3b, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0};
    if (!policy || !sample || !entries || !help_present ||
        (entries->count != policy->base_count && entries->count != policy->base_count + 1) ||
        !camera_context_complete(sample, entries, entries->count)) {
        return 0;
    }
    unsigned old_index = 0;
    unsigned found = 0;
    for (unsigned index = 0; index < entries->count; ++index) {
        const unsigned char *payload = entries->payloads[index];
        if (!memcmp(payload, help, sizeof help)) {
            if (found++) {
                return 0;
            }
            continue;
        }
        if (old_index == policy->base_count || memcmp(payload, policy->adopted[old_index], 16)) {
            return 0;
        }
        ++old_index;
    }
    if (old_index != policy->base_count || found != entries->count - policy->base_count) {
        return 0;
    }
    *help_present = found;
    return 1;
}

typedef enum {
    PLAYER_CONTEXT_FRAME_BEGIN = 1,
    PLAYER_CONTEXT_FRAME_END = 2
} player_context_boundary;

typedef enum {
    PLAYER_CONTEXT_REFUSED = 0,
    PLAYER_CONTEXT_UNCHANGED = 1,
    PLAYER_CONTEXT_ADDED = 2,
    PLAYER_CONTEXT_REMOVED = 3
} player_context_result;

/* The caller proves the named boundary, including worker/main ownership. */
static inline player_context_result player_context_policy_observe_outside(player_context_policy *policy,
        uint64_t generation, uint64_t thread, uintptr_t owner, uintptr_t ship, uintptr_t controller,
        player_context_boundary boundary, const input_context_census_sample *sample,
        const input_context_census_entries *entries) {
    unsigned observed = 0;
    if (!policy || (boundary != PLAYER_CONTEXT_FRAME_BEGIN && boundary != PLAYER_CONTEXT_FRAME_END) ||
        !generation || policy->scope_open || generation < policy->last_generation ||
        !player_context_policy_identity(policy, thread, owner, ship, controller, sample) ||
        !player_context_policy_classify(policy, sample, entries, &observed)) {
        player_context_policy_fail(policy);
        return PLAYER_CONTEXT_REFUSED;
    }
    player_context_result result = PLAYER_CONTEXT_UNCHANGED;
    if (observed != policy->help_present) {
        if (policy->transitions == UINT64_MAX) {
            player_context_policy_fail(policy);
            return PLAYER_CONTEXT_REFUSED;
        }
        result = observed ? PLAYER_CONTEXT_ADDED : PLAYER_CONTEXT_REMOVED;
        policy->help_present = observed;
        ++policy->transitions;
    }
    policy->last_generation = generation;
    return result;
}

/* Scope endpoints must equal each other byte-for-byte and the last admitted
 * outside state. A help-text change during a scope is always refused. */
static inline int player_context_policy_scope_begin(player_context_policy *policy,
        uint64_t generation, uint64_t thread, uintptr_t owner, uintptr_t ship, uintptr_t controller,
        int scope_authenticated, const input_context_census_sample *sample,
        const input_context_census_entries *entries) {
    unsigned observed = 0;
    if (!policy || scope_authenticated != 1 || !generation || policy->scope_open ||
        generation < policy->last_generation ||
        !player_context_policy_identity(policy, thread, owner, ship, controller, sample) ||
        !player_context_policy_classify(policy, sample, entries, &observed) || observed != policy->help_present) {
        return player_context_policy_fail(policy);
    }
    policy->scope_count = entries->count;
    memcpy(policy->scoped, entries->payloads, entries->count * 16u);
    policy->last_generation = generation;
    policy->scope_open = 1;
    return 1;
}

static inline int player_context_policy_scope_end(player_context_policy *policy,
        uint64_t generation, uint64_t thread, uintptr_t owner, uintptr_t ship, uintptr_t controller,
        int scope_authenticated, const input_context_census_sample *sample,
        const input_context_census_entries *entries) {
    unsigned observed = 0;
    if (!policy || scope_authenticated != 1 || !policy->scope_open ||
        generation != policy->last_generation ||
        !player_context_policy_identity(policy, thread, owner, ship, controller, sample) ||
        !player_context_policy_classify(policy, sample, entries, &observed) || observed != policy->help_present ||
        entries->count != policy->scope_count || memcmp(entries->payloads, policy->scoped, entries->count * 16u)) {
        return player_context_policy_fail(policy);
    }
    policy->scope_open = 0;
    return 1;
}
#endif

#ifndef X4_GATE_PLAYER_CONTEXT_POLICY_ADAPTER_H
#define X4_GATE_PLAYER_CONTEXT_POLICY_ADAPTER_H

#include "player_context_policy.h"
#include "gate_player_startup_phases.h"

/* One worker owns these copies; no native tree pointer is retained. */
static player_context_policy player_context_adapter_policy;
static uint64_t player_context_adapter_epoch;
static char player_context_adapter_hash[65];
static input_context_census_entries player_context_adapter_entries;

static int player_context_adapter_worker(void) {
    return GetCurrentThreadId() == stock_observer_worker_thread &&
        player_recurring_physics_entered() && gate_tls_worker(gate_tls_self());
}

static int player_context_adapter_owner(void) {
    if (!player_context_adapter_worker()) return 0;
    return player_context_adapter_policy.initialized && !player_context_adapter_policy.failed &&
        player_context_adapter_policy.thread == (uint64_t)GetCurrentThreadId() &&
        player_context_adapter_policy.owner == sko_session.owner &&
        player_context_adapter_policy.ship == lifecycle_discard_ship &&
        player_context_adapter_policy.controller == lifecycle_discard_controller &&
        player_physics_identity();
}

static void player_context_adapter_digest(const input_context_census_entries *entries) {
    unsigned char digest[32];
    lockstep_sha256(entries->payloads, entries->count * INPUT_CONTEXT_CENSUS_PAYLOAD_BYTES, digest);
    hex32(digest, player_context_adapter_hash);
}

/* Only copied entries from the current failed census are reported. */
static void player_context_adapter_report(const input_context_census_sample *sample,
                                          const input_context_census_entries *entries, int collected) {
    const player_context_policy *policy = &player_context_adapter_policy;
    player_context_diagnostic("policy_base", stats.issued, (uint64_t)GetCurrentThreadId(),
        policy->manager, 1, 1, 1, policy->base_count, 7, "adopted",
        policy->adopted, policy->base_count);
    player_context_diagnostic("policy_observed", stats.issued, (uint64_t)GetCurrentThreadId(),
        sample->manager, collected, sample->complete, sample->tree_valid, sample->node_count,
        sample->target_present, sample->reason, entries->payloads, entries->count);
}

static int player_context_adapter_check(const input_context_census_sample *sample,
                                        const input_context_census_entries *entries) {
    if (!player_context_adapter_owner() || !sample || !entries) return 0;
    unsigned help = 0;
    player_context_policy *policy = &player_context_adapter_policy;
    if (!player_context_policy_identity(policy, (uint64_t)GetCurrentThreadId(), sko_session.owner,
            lifecycle_discard_ship, lifecycle_discard_controller, sample) ||
        !player_context_policy_classify(policy, sample, entries, &help) || help != policy->optional_mask ||
        (policy->scope_open && (entries->count != policy->scope_count ||
         memcmp(entries->payloads, policy->scoped, entries->count * 16u)))) {
        player_context_adapter_report(sample, entries, sample->complete == 1);
        return 0;
    }
    player_context_adapter_digest(entries);
    return 1;
}

static int player_context_adapter_suffix(char *output, size_t size) {
    if (!player_context_adapter_owner() || !player_context_adapter_hash[0]) return 0;
    int count = snprintf(output, size, " context_policy_v=1 manager=%llx epoch=%llu full_hash=%s",
        (unsigned long long)player_context_adapter_policy.manager,
        (unsigned long long)player_context_adapter_epoch, player_context_adapter_hash);
    return count > 0 && (size_t)count < size;
}

static void player_context_adapter_receipt(const char *event, const char *boundary,
                                           uint64_t generation, const input_context_census_entries *entries) {
    static const char digits[] = "0123456789abcdef";
    char payloads[21 * 16 * 2 + 1], message[1400];
    lifecycle_discard_require(entries && entries->count <= 21, "context_policy_receipt_bounds");
    const unsigned char *bytes = (const unsigned char *)entries->payloads;
    size_t length = entries->count * 16u;
    for (size_t index = 0; index < length; ++index) {
        payloads[2 * index] = digits[bytes[index] >> 4];
        payloads[2 * index + 1] = digits[bytes[index] & 15];
    }
    payloads[2 * length] = 0;
    player_context_adapter_digest(entries);
    int count = snprintf(message, sizeof message,
        "X4LAN CONTEXT_POLICY v=1 event=%s tick=%llu boundary=%s epoch=%llu "
        "owner=%llx manager=%llx target=%llx controller=%llx base_nodes=%u nodes=%u digest=%s payloads=%s\n",
        event, (unsigned long long)generation, boundary, (unsigned long long)player_context_adapter_epoch,
        (unsigned long long)player_context_adapter_policy.owner,
        (unsigned long long)player_context_adapter_policy.manager,
        (unsigned long long)player_context_adapter_policy.ship,
        (unsigned long long)player_context_adapter_policy.controller,
        player_context_adapter_policy.base_count, entries->count, player_context_adapter_hash, payloads);
    lifecycle_discard_require(count > 0 && (size_t)count < sizeof message, "context_policy_receipt_format");
    lifecycle_discard_write(message);
}

static int player_context_adapter_collect(input_context_census_sample *sample) {
    if (!player_context_adapter_owner()) return 0;
    player_context_adapter_entries.count = 0;
    const input_context_census_operations operations = {NULL, player_seat_preflight_read};
    return input_context_census_collect_entries(&operations, sko_session.owner + 0x10,
                                                sample, &player_context_adapter_entries);
}

static int player_context_adapter_inactive_single(void) {
    const char *mode = getenv("X4_DEV_PLAYER_PHYSICS");
    if (!mode || strcmp(mode, "single-frame-discard") || !player_context_adapter_worker()) return 0;
    return !player_context_adapter_policy.initialized && !lifecycle_context_policy_owner &&
        !lifecycle_context_policy_check && !lifecycle_context_policy_suffix;
}

static void player_context_adapter_init(void) {
    const char *mode = getenv("X4_DEV_PLAYER_PHYSICS");
    lifecycle_discard_require(mode && (!strcmp(mode, "bounded-frames-discard") ||
        !strcmp(mode, "network-frames-discard")) && stats.issued == player_startup()->take &&
        player_context_adapter_worker() && player_physics_identity() && sko_session.owner &&
        sko_session.owner <= UINTPTR_MAX - 0x10 && !player_context_adapter_policy.initialized && !lifecycle_context_policy_check &&
        !lifecycle_context_policy_owner && !lifecycle_context_policy_suffix, "context_policy_init_owner");
    const input_context_census_operations operations = {NULL, player_seat_preflight_read};
    input_context_census_sample sample;
    int collected = input_context_census_collect_entries(&operations, sko_session.owner + 0x10,
                                                          &sample, &player_context_adapter_entries);
    int accepted = collected && player_context_policy_init(&player_context_adapter_policy, 13,
        (uint64_t)GetCurrentThreadId(), sko_session.owner, lifecycle_discard_ship,
        lifecycle_discard_controller, 1, &sample, &player_context_adapter_entries);
    if (!accepted) {
        player_context_diagnostic("policy_init", stats.issued, (uint64_t)GetCurrentThreadId(),
            sample.manager, collected, sample.complete, sample.tree_valid, sample.node_count,
            sample.target_present, sample.reason, player_context_adapter_entries.payloads,
            player_context_adapter_entries.count);
    }
    lifecycle_discard_require(accepted, "context_policy_init");
    player_context_adapter_epoch = 1;
    player_context_adapter_receipt("init", "ready", stats.issued, &player_context_adapter_entries);
    lifecycle_context_policy_owner = player_context_adapter_owner;
    lifecycle_context_policy_check = player_context_adapter_check;
    lifecycle_context_policy_suffix = player_context_adapter_suffix;
}

static void player_context_adapter_outside(uint64_t generation, player_context_boundary boundary) {
    if (player_context_adapter_inactive_single()) return;
    input_context_census_sample sample = {0};
    lifecycle_discard_require(generation == stats.issued && player_context_adapter_owner(),
                              "context_policy_outside_owner");
    int collected = player_context_adapter_collect(&sample);
    if (!collected) player_context_adapter_report(&sample, &player_context_adapter_entries, 0);
    lifecycle_discard_require(collected, "context_policy_outside_owner");
    player_context_result result = player_context_policy_observe_outside(&player_context_adapter_policy,
        generation, (uint64_t)GetCurrentThreadId(), sko_session.owner, lifecycle_discard_ship,
        lifecycle_discard_controller, boundary, &sample, &player_context_adapter_entries);
    if (result == PLAYER_CONTEXT_REFUSED)
        player_context_adapter_report(&sample, &player_context_adapter_entries, 1);
    lifecycle_discard_require(result != PLAYER_CONTEXT_REFUSED, "context_policy_outside");
    if (result != PLAYER_CONTEXT_UNCHANGED) {
        lifecycle_discard_require(player_context_adapter_epoch != UINT64_MAX, "context_policy_epoch");
        ++player_context_adapter_epoch;
        player_context_adapter_receipt(result == PLAYER_CONTEXT_ADDED ? "add" : "remove",
            boundary == PLAYER_CONTEXT_FRAME_BEGIN ? "begin" : "end", generation, &player_context_adapter_entries);
    } else {
        player_context_adapter_digest(&player_context_adapter_entries);
    }
}

static void player_context_adapter_scope_begin(uint64_t generation) {
    if (player_context_adapter_inactive_single()) return;
    input_context_census_sample sample = {0};
    lifecycle_discard_require(generation == stats.issued && player_context_adapter_owner(),
                              "context_policy_scope_begin");
    int collected = player_context_adapter_collect(&sample);
    int accepted = collected && player_context_policy_scope_begin(&player_context_adapter_policy, generation,
        (uint64_t)GetCurrentThreadId(), sko_session.owner, lifecycle_discard_ship,
        lifecycle_discard_controller, 1, &sample, &player_context_adapter_entries);
    if (!accepted) player_context_adapter_report(&sample, &player_context_adapter_entries, collected);
    lifecycle_discard_require(accepted, "context_policy_scope_begin");
    player_context_adapter_digest(&player_context_adapter_entries);
}

static void player_context_adapter_scope_end(uint64_t generation) {
    if (player_context_adapter_inactive_single()) return;
    input_context_census_sample sample = {0};
    lifecycle_discard_require(generation == stats.issued && player_context_adapter_owner(),
                              "context_policy_scope_end");
    int collected = player_context_adapter_collect(&sample);
    int accepted = collected && player_context_policy_scope_end(&player_context_adapter_policy, generation,
        (uint64_t)GetCurrentThreadId(), sko_session.owner, lifecycle_discard_ship,
        lifecycle_discard_controller, 1, &sample, &player_context_adapter_entries);
    if (!accepted) player_context_adapter_report(&sample, &player_context_adapter_entries, collected);
    lifecycle_discard_require(accepted, "context_policy_scope_end");
    player_context_adapter_digest(&player_context_adapter_entries);
}

/* The camera Lua entry separately authenticates its UI worker and identity;
 * this rebase checks the entire old policy and exact stock 13-to-15 transition. */
static void player_context_adapter_camera_rebase(const input_context_census_sample *before,
        const input_context_census_entries *old, const input_context_census_sample *after,
        const input_context_census_entries *current) {
    lifecycle_discard_require(accept_ui() && stock_observer_worker_boundary() &&
        GetCurrentThreadId() == stock_observer_worker_thread && gate_tls_worker(gate_tls_self()) &&
        stats.active && stats.issued == player_startup()->apply + 1 && stats.slot == 2 && wire_clients == 2 && boot_phase == 3 &&
        atomic_load_explicit(&player_physics_ready, memory_order_acquire) == 1 && player_physics_identity() &&
        player_context_adapter_policy.initialized && !player_context_adapter_policy.failed,
        "context_policy_camera_owner");
    int accepted = before && old && after && current && !player_context_adapter_policy.scope_open &&
        !player_context_adapter_policy.optional_mask && player_context_adapter_policy.base_count == 13 &&
        player_context_policy_identity(&player_context_adapter_policy, (uint64_t)GetCurrentThreadId(),
            sko_session.owner, lifecycle_discard_ship, lifecycle_discard_controller, before) &&
        camera_context_complete(before, old, 13) &&
        !memcmp(player_context_adapter_policy.adopted, old->payloads, 13 * 16u) &&
        camera_context_transition(before, old, after, current) &&
        camera_context_anchor_matches(&player_camera_anchor, (uint64_t)GetCurrentThreadId(),
            sko_session.owner, lifecycle_discard_ship, lifecycle_discard_controller, after, current);
    if (!accepted) {
        if (before && old) player_context_adapter_report(before, old, before->complete == 1);
        if (after && current) player_context_adapter_report(after, current, after->complete == 1);
    }
    lifecycle_discard_require(accepted, "context_policy_camera_rebase");
    player_context_policy replacement = {0};
    lifecycle_discard_require(player_context_policy_init(&replacement, 15, (uint64_t)GetCurrentThreadId(),
        sko_session.owner, lifecycle_discard_ship, lifecycle_discard_controller, 1, after, current) &&
        player_context_adapter_epoch != UINT64_MAX, "context_policy_camera_init");
    replacement.last_generation = stats.issued;
    replacement.transitions = player_context_adapter_policy.transitions;
    player_context_adapter_policy = replacement;
    ++player_context_adapter_epoch;
    player_context_adapter_receipt("camera", "camera", stats.issued, current);
}
#endif

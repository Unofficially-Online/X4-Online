#ifndef X4_GATE_PLAYER_CAMERA_PROBE_H
#define X4_GATE_PLAYER_CAMERA_PROBE_H

/* Unused native pin/authentication pieces for the camera diagnostic. Include
 * after the context, target-hold and lifecycle headers. No hook or automatic camera change is installed by this file. The one-shot
 * Lua method must be explicitly registered and called by the experiment. */
#include "player_camera_probe.h"
#include "gate_player_camera_id_scope.h"

static int player_camera_native_pins(void) {
    static const lx_site sites[] = {
        {"camera-possible", 0xc030e0, 193, "6f12c34528f8f1e39fa12f6e8d44b158589db5becb7f7a3ae6e4ab31ad23831c"},
        {"camera-set", 0xc03b30, 190, "63359aea0d9ea769fca08a75d3f92dc442b5753d0fba441a10c477716884e4d6"},
        {"camera-current", 0xa41a20, 51, "9cc867a678075ca47384ae1ee291288a2159ea248fe12ee15a3f593a01f57945"},
        {"camera-worker", 0x1d7dc10, 1513, "ba339ecec42d12233c0f9e6616979cc132775454009ea3f6029030cfed2ddcdb"}
    };
    unsigned char digest[32];
    char hex[65];
    if (!gate_file_sha256(gate_env.exe, LX_FILE_SIZE, digest)) {
        return 0;
    }
    hex32(digest, hex);
    if (strcmp(hex, PLAYER_CAMERA_IMAGE_SHA)) {
        return 0;
    }
    for (unsigned index = 0; index < sizeof sites / sizeof sites[0]; ++index) {
        if (!site_ok(&sites[index])) {
            return 0;
        }
    }
    return 1;
}

/* The sealed saved-ID lookup and held identity must still agree. The caller
 * must separately prove a safe UI/worker phase and exclude competing writes. */
static int player_camera_native_target(player_camera_target *target) {
    uintptr_t resolved = 0;
    uint64_t runtime = 0;
    if (!target || !player_physics_identity() ||
        !player_seat_preflight_resolve(NULL, PLAYER_CAMERA_TARGET_SAVED, &resolved, &runtime) ||
        resolved != player_target_hold_resident.identity.target ||
        runtime != player_target_hold_resident.identity.target_runtime || !resolved || !runtime) {
        return 0;
    }
    *target = (player_camera_target){resolved, runtime, PLAYER_CAMERA_TARGET_SAVED};
    return 1;
}

/* SysV exported getter; callers must perform the pin and phase checks above. */
static uint64_t player_camera_native_current(void) {
    return ((uint64_t (*)(void))LX(0xa41a20))();
}

/* One call, on the adopted UI worker after P2 setup and canonical restoration.
 * Camera context and reference effects are observed, not rolled back. */
#ifndef PLAYER_CAMERA_TEST_PIN_HELPERS_ONLY
#include "gate_player_camera_context_anchor.h"
/* Implemented by gate_runtime_ids.h, which is included after this header. */
static int player_camera_rid_ready(void);
static int player_camera_rid_begin(player_camera_id_token *token);
static int player_camera_rid_end(player_camera_id_token *token);

static _Atomic unsigned player_camera_probe_attempted;
static input_context_census_entries player_camera_context_before, player_camera_context_after;

/* Entries contain payload bytes only, sorted by the tree's signed key. */
static void player_camera_log_contexts(const char *stage, const input_context_census_entries *entries) {
    static const char digits[] = "0123456789abcdef";
    for (unsigned index = 0; index < entries->count; ++index) {
        char payload[33], message[160];
        for (unsigned byte = 0; byte < INPUT_CONTEXT_CENSUS_PAYLOAD_BYTES; ++byte) {
            payload[byte * 2] = digits[entries->payloads[index][byte] >> 4];
            payload[byte * 2 + 1] = digits[entries->payloads[index][byte] & 15];
        }
        payload[32] = 0;
        int length = snprintf(message, sizeof message,
            "X4LAN CAMERA_CONTEXT v=2 stage=%s index=%u count=%u payload=%s\n",
            stage, index, entries->count, payload);
        lifecycle_discard_require(length > 0 && (size_t)length < sizeof message, "camera_context_format");
        lifecycle_discard_write(message);
    }
}


static int lua_player_external_camera_probe(lua_State *state) {
    const char *reason = "admission";
    int result = 0;
    if (atomic_exchange_explicit(&player_camera_probe_attempted, 1, memory_order_acq_rel)) {
        reason = "already_attempted";
        goto finish;
    }
    if (!accept_ui() || !stats.active || stats.issued != player_startup()->apply + 1 || boot_phase != 3 ||
        stats.slot != 2 || wire_clients != 2 || !keyboard_probe_requested ||
        keyboard_probe_queue.config.independent_players != 1 || player_recurring_physics != 3 ||
        atomic_load_explicit(&player_physics_ready, memory_order_acquire) != 1 ||
        !stock_observer_worker_boundary() || !gate_tls_worker(gate_tls_self()) ||
        player_routes_scope || player_network_busy || player_physics_replay_busy ||
        player_physics_upkeep_busy || keyboard_probe_inflight ||
        atomic_load(&stock_observer_input_depth) || atomic_load(&sko_session.wrapper_depth)) {
        goto finish;
    }
    if (!player_physics_identity()) {
        lifecycle_discard_exit(0, "camera_identity_before");
    }
    reason = "pins";
    if (!player_camera_native_pins()) {
        goto finish;
    }
    reason = "id_scope_admission";
    if (!player_camera_rid_ready()) {
        goto finish;
    }
    player_camera_target target = {0};
    reason = "target";
    if (!player_camera_native_target(&target) ||
        lifecycle_discard_pointer(target.target) != (uintptr_t)LX(0x3d21748)) {
        lifecycle_discard_exit(0, "camera_target_before");
    }
    unsigned char bank_before[PCB_VALUE_BYTES], bank_after[PCB_VALUE_BYTES];
    unsigned char bank_digest[32];
    char bank_before_hash[65], bank_after_hash[65];
    char context_before_hash[65], context_after_hash[65];
    input_context_census_sample context_before = {0}, context_after = {0};
    uintptr_t context_owner = sko_session.owner;
    const input_context_census_operations operations = {NULL, player_seat_preflight_read};
    uintptr_t id_map = 0;
    uint64_t inserts_before = 0, inserts_after = 0;
    uint32_t live_before = 0, live_after = 0, free_before = 0, free_after = 0;
    reason = "observation_before";
    if (!read_local(LX(0x3daa860), &id_map, sizeof id_map) || !id_map || id_map > UINTPTR_MAX - 0x50 ||
        !read_local((void *)(id_map + 0x40), &inserts_before, sizeof inserts_before) ||
        !read_local((void *)(id_map + 0x48), &live_before, sizeof live_before) ||
        !read_local((void *)(id_map + 0x4c), &free_before, sizeof free_before) ||
        !input_context_census_collect_entries(&operations, sko_session.owner + 0x10, &context_before,
                                              &player_camera_context_before) ||
        !context_before.tree_valid) {
        goto finish;
    }
    lifecycle_routes_read_bank(bank_before);
    lockstep_sha256(bank_before, sizeof bank_before, bank_digest);
    hex32(bank_digest, bank_before_hash);
    lockstep_sha256(context_before.payloads, sizeof context_before.payloads, bank_digest);
    hex32(bank_digest, context_before_hash);
    uint64_t before = player_camera_native_current();
    uint64_t *rng = gate_tls_rng(gate_tls_self());
    if (!rng) {
        reason = "rng_missing";
        goto finish;
    }
    uint64_t rng_before = *rng;
    char message[1024];
    snprintf(message, sizeof message,
        "X4LAN CAMERA stage=before tick=%llu pins=1 saved=%016llx target=%llx runtime=%llu current=%llu "
        "bank=%s contexts=%u/%u context_hash=%s rng=%016llx ids=%llu/%u/%u\n",
        (unsigned long long)stats.issued, (unsigned long long)target.saved, (unsigned long long)target.target,
        (unsigned long long)target.runtime, (unsigned long long)before, bank_before_hash,
        context_before.node_count, context_before.target_present, context_before_hash,
        (unsigned long long)rng_before,
        (unsigned long long)inserts_before, live_before, free_before);
    lifecycle_discard_write(message);
    player_camera_log_contexts("before", &player_camera_context_before);
    unsigned char possible = ((unsigned char (*)(uint64_t, unsigned char))LX(0xc030e0))(target.runtime, 1);
    int scope_ok = 1;
    if (possible) {
        player_camera_id_token token = {0};
        if (!player_camera_rid_begin(&token)) {
            *rng = rng_before;
            reason = "id_scope_begin";
            goto finish;
        }
        ((void (*)(uint64_t, unsigned char))LX(0xc03b30))(target.runtime, 1);
        scope_ok = player_camera_rid_end(&token);
    }
    uint64_t current = player_camera_native_current();
    uint64_t rng_after = *rng;
    *rng = rng_before;
    uint64_t rng_restored = *rng;
    if (!scope_ok) {
        lifecycle_discard_exit(0, "camera_id_scope_end");
    }
    player_camera_target after = {0};
    if (rng_restored != rng_before || !player_physics_identity() || !player_camera_native_target(&after) ||
        !player_camera_same_target(&target, &after)) {
        lifecycle_discard_exit(0, "camera_identity_after");
    }
    uintptr_t id_map_after = 0;
    if (!read_local(LX(0x3daa860), &id_map_after, sizeof id_map_after) || id_map_after != id_map ||
        !read_local((void *)(id_map + 0x40), &inserts_after, sizeof inserts_after) ||
        !read_local((void *)(id_map + 0x48), &live_after, sizeof live_after) ||
        !read_local((void *)(id_map + 0x4c), &free_after, sizeof free_after) ||
        !input_context_census_collect_entries(&operations, sko_session.owner + 0x10, &context_after,
                                              &player_camera_context_after) ||
        !context_after.tree_valid) {
        lifecycle_discard_exit(0, "camera_observation_after");
    }
    lifecycle_routes_read_bank(bank_after);
    if (possible && current == target.runtime) {
        player_camera_target adopted = {0};
        lifecycle_discard_require(sko_session.owner == context_owner && player_physics_identity() &&
            player_camera_native_pins() && player_camera_native_target(&adopted) &&
            player_camera_same_target(&target, &adopted) && !memcmp(bank_before, bank_after, sizeof bank_before) &&
            target.target == lifecycle_discard_ship &&
            camera_context_anchor_adopt(&player_camera_anchor, (uint64_t)GetCurrentThreadId(), context_owner,
                lifecycle_discard_ship, lifecycle_discard_controller, &context_before, &player_camera_context_before,
                &context_after, &player_camera_context_after), "camera_anchor_admission");
    }
    lockstep_sha256(bank_after, sizeof bank_after, bank_digest);
    hex32(bank_digest, bank_after_hash);
    lockstep_sha256(context_after.payloads, sizeof context_after.payloads, bank_digest);
    hex32(bank_digest, context_after_hash);
    snprintf(message, sizeof message,
        "X4LAN CAMERA stage=after tick=%llu possible=%u set=%u current=%llu match=%u "
        "bank=%s bank_changed=%u contexts=%u/%u context_hash=%s context_changed=%u rng=%016llx restored=%016llx "
        "ids=%llu/%u/%u\n",
        (unsigned long long)stats.issued, possible != 0, possible != 0, (unsigned long long)current, current == target.runtime,
        bank_after_hash, memcmp(bank_before, bank_after, sizeof bank_before) != 0,
        context_after.node_count, context_after.target_present, context_after_hash,
        context_before.node_count != context_after.node_count ||
            context_before.target_present != context_after.target_present ||
            memcmp(context_before.payloads, context_after.payloads, sizeof context_before.payloads) != 0,
        (unsigned long long)rng_after, (unsigned long long)rng_restored,
        (unsigned long long)inserts_after, live_after, free_after);
    lifecycle_discard_write(message);
    player_camera_log_contexts("after", &player_camera_context_after);
    result = possible && current == target.runtime;
    reason = !possible ? "not_possible" : result ? "matched" : "getter_mismatch";
finish:
    {
        char message[160];
        snprintf(message, sizeof message, "X4LAN CAMERA stage=result success=%d reason=%s\n", result, reason);
        if (lifecycle_discard_log >= 0) {
            lifecycle_discard_write(message);
        } else {
            (void)write(STDERR_FILENO, message, strlen(message));
        }
    }
    if (result) {
        unsigned char digest[32];
        char digest_hex[65], receipt[256];
        lockstep_sha256(player_camera_anchor.payloads, sizeof player_camera_anchor.payloads, digest);
        hex32(digest, digest_hex);
        snprintf(receipt, sizeof receipt,
            "X4LAN CONTEXT_ANCHOR v=1 tick=%llu owner=%llx manager=%llx target=%llx controller=%llx digest=%s\n",
            (unsigned long long)stats.issued, (unsigned long long)player_camera_anchor.owner, (unsigned long long)player_camera_anchor.manager,
            (unsigned long long)player_camera_anchor.ship, (unsigned long long)player_camera_anchor.controller,
            digest_hex);
        lifecycle_discard_write(receipt);
        player_context_adapter_camera_rebase(&context_before, &player_camera_context_before,
                                             &context_after, &player_camera_context_after);
    }
    p_lua_pushnumber(state, result);
    return 1;
}

#endif /* PLAYER_CAMERA_TEST_PIN_HELPERS_ONLY */

#endif

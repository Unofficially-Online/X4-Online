#ifndef X4_GATE_PLAYER_LIFECYCLE_DISCARD_H
#define X4_GATE_PLAYER_LIFECYCLE_DISCARD_H

/* Explicit developer experiment in a disposable profile, never a gameplay
 * mode. The retained factory player takes and releases a real ship once while
 * the main thread is parked. The probe never returns; success/failure use exit
 * 223, while a deadline, logging failure or native crash can end it earlier. There is
 * deliberately no full rollback, save or further simulation. Explicit submodes
 * additionally observe a single controller Update and its motion getters.
 * Background/render readers are not excluded: this probe measures native
 * lifecycle effects, not the safety of a persistent player-bank exchange.
 * Include after target hold and control-bank descriptors. */
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "gate_player_camera_context_anchor.h"

static uintptr_t lifecycle_discard_controls[32];
static unsigned char lifecycle_discard_initial_bank[PCB_VALUE_BYTES];
static uintptr_t lifecycle_discard_ship;
static uintptr_t lifecycle_discard_controller;
static int lifecycle_discard_log = -1;

static void lifecycle_discard_open_log(void) {
    const char *path = getenv("X4_DEV_PLAYER_LIFECYCLE_LOG");
    if (!path || path[0] != '/') {
        _exit(224);
    }
    lifecycle_discard_log = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    struct stat info;
    if (lifecycle_discard_log < 0 || fstat(lifecycle_discard_log, &info) ||
        !S_ISREG(info.st_mode) || info.st_uid != geteuid() || info.st_nlink != 1) {
        _exit(224);
    }
}

static int player_lifecycle_discard_requested(void) {
    const char *mode = getenv("X4_DEV_PLAYER_LIFECYCLE");
    return mode && (!strcmp(mode, "take-release-discard") ||
                    !strcmp(mode, "take-release-isolated-discard") ||
                    !strcmp(mode, "take-release-update-discard") ||
                    !strcmp(mode, "take-release-routes-discard") ||
                    !strcmp(mode, "take-release-input-discard"));
}

static void lifecycle_discard_write(const char *text) {
    size_t length = strlen(text);
    while (length) {
        ssize_t count = write(lifecycle_discard_log, text, length);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            _exit(224); /* Missing evidence is never a successful experiment. */
        }
        text += count;
        length -= (size_t)count;
    }
}

#include "gate_player_context_diagnostic.h"

static _Noreturn void lifecycle_discard_exit(int passed, const char *reason) {
    char message[512];
    snprintf(message, sizeof(message),
        "X4LAN LIFECYCLE_DISCARD result=%s reason=%s exit=223 restored=0 resumed=0\n",
        passed ? "pass" : "fail", reason);
    lifecycle_discard_write(message);
    if (fsync(lifecycle_discard_log)) {
        _exit(224);
    }
    _exit(223);
}

static void lifecycle_discard_require(int condition, const char *reason) {
    if (!condition) {
        lifecycle_discard_exit(0, reason);
    }
}

static void lifecycle_discard_stage(const char *stage) {
    char message[256];
    snprintf(message, sizeof(message),
        "X4LAN LIFECYCLE_DISCARD stage=%s tick=%llu thread=%llu\n", stage,
        (unsigned long long)stats.issued, (unsigned long long)GetCurrentThreadId());
    lifecycle_discard_write(message);
}

static void lifecycle_discard_read(uintptr_t address, void *output, size_t length) {
    lifecycle_discard_require(address && read_local((void *)address, output, length),
                              "unreadable_state");
}

static uintptr_t lifecycle_discard_pointer(uintptr_t address) {
    uintptr_t value = 0;
    lifecycle_discard_read(address, &value, sizeof(value));
    return value;
}

#include "gate_player_lifecycle_isolation.h"
#include "gate_player_lifecycle_update.h"
#include "gate_player_lifecycle_input.h"

static int lifecycle_discard_pins(void) {
    static const lx_site pins[] = {
        {"ship-take", 0x17a34f0, 66,
         "eac421a04b823c2b682acac822066875c86da1818bdd458f0690f11eb0fbb451"},
        {"ship-take-body", 0x18888a0, 3333,
         "c8b019f44cea931f3a3cbb7bca7417edfcd3b1c5ec953d7478650250c8760413"},
        {"ship-release", 0x12bafe0, 162,
         "915f078f1d51c625e0c4c05802b8bc90d157cbea960f28fb299e3bf52c7a9e27"},
        {"ship-release-body", 0x186a490, 1260,
         "cf147afa84a55de863a3748396aacb382a81e24f04157b15653605c6130d54c5"},
        {"ship-controller-replace", 0x1316f40, 668,
         "1192e8098cfaf5f739be66ef0d032da4bf25c0f0464be38a003b3cdd4e4f50c8"},
        {"current-controller", 0x1dd19d0, 137,
         "3cab7b30231bb96a1276459079a28e655b922186495473633288e7ede5c9f837"},
    };
    for (unsigned index = 0; index < sizeof(pins) / sizeof(pins[0]); ++index) {
        if (!site_ok(&pins[index])) {
            return 0;
        }
    }
    return player_seat_preflight_pins(NULL) && player_zone_pins() &&
           player_target_hold_pins() && factory_probe_helpers();
}

/* The recurring diagnostic installs these only after authenticating its first
 * canonical context set. Earlier one-shot probes retain their original checks. */
static int (*lifecycle_context_policy_check)(const input_context_census_sample *,
                                              const input_context_census_entries *);
static int (*lifecycle_context_policy_owner)(void);
static int (*lifecycle_context_policy_suffix)(char *, size_t);

static int lifecycle_discard_context_collect(const input_context_census_operations *operations,
                                             input_context_census_sample *contexts) {
    if (lifecycle_context_policy_check) {
        static input_context_census_entries policy_entries;
        lifecycle_discard_require(lifecycle_context_policy_owner && lifecycle_context_policy_owner(),
                                  "context_policy_snapshot_owner");
        int collected = input_context_census_collect_entries(operations, sko_session.owner + 0x10,
                                                             contexts, &policy_entries);
        /* Even an incomplete census is retained by the diagnostic callback;
         * neither a partial tree nor a failed policy check can be admitted. */
        int policy_valid = lifecycle_context_policy_check(contexts, &policy_entries);
        lifecycle_discard_require(collected && policy_valid,
                                  "context_policy_snapshot_changed");
        return collected;
    }
    if (!atomic_load_explicit(&player_camera_anchor.active, memory_order_acquire)) {
        return input_context_census_collect(operations, sko_session.owner + 0x10, contexts);
    }
    static input_context_census_entries scratch;
    lifecycle_discard_require((uint64_t)GetCurrentThreadId() == player_camera_anchor.thread &&
        sko_session.owner == player_camera_anchor.owner && lifecycle_discard_ship == player_camera_anchor.ship &&
        lifecycle_discard_controller == player_camera_anchor.controller &&
        lifecycle_discard_pointer(lifecycle_discard_ship + 0x200) == lifecycle_discard_controller &&
        lifecycle_discard_pointer(lifecycle_discard_controller + 0x20) == lifecycle_discard_ship,
        "camera_anchor_owner_changed");
    int ok = input_context_census_collect_entries(operations, sko_session.owner + 0x10, contexts, &scratch);
    int matches = ok && camera_context_anchor_matches(&player_camera_anchor,
        (uint64_t)GetCurrentThreadId(), sko_session.owner, lifecycle_discard_ship, lifecycle_discard_controller,
        contexts, &scratch);
    if (!matches) {
        uint64_t thread = (uint64_t)GetCurrentThreadId();
        player_context_diagnostic("anchor", stats.issued, thread, player_camera_anchor.manager,
            1, 1, 1, 15, 7, "adopted", player_camera_anchor.payloads, 15);
        player_context_diagnostic("observed", stats.issued, thread, contexts->manager,
            ok, contexts->complete, contexts->tree_valid, contexts->node_count, contexts->target_present,
            contexts->reason, scratch.payloads, scratch.count);
    }
    lifecycle_discard_require(matches, "camera_anchor_context_changed");
    return ok;
}

/* Retain complete bank bytes rather than only a digest: a later repair needs
 * the exact offsets modified by the engine. This is evidence, not a restore. */
static void lifecycle_discard_snapshot(const char *label, int initial) {
    unsigned char bank[PCB_VALUE_BYTES];
    char hex[PCB_VALUE_BYTES * 2 + 1];
    static const char digits[] = "0123456789abcdef";
    unsigned changed = 0;
    for (unsigned index = 0; index < PCB_REGION_COUNT; ++index) {
        const pcb_region *region = &pcb_regions[index];
        lifecycle_discard_read((uintptr_t)LX(region->va), bank + region->offset, region->length);
    }
    if (initial) {
        memcpy(lifecycle_discard_initial_bank, bank, sizeof(bank));
    }
    for (unsigned index = 0; index < sizeof(bank); ++index) {
        hex[index * 2] = digits[bank[index] >> 4];
        hex[index * 2 + 1] = digits[bank[index] & 15];
        changed += bank[index] != lifecycle_discard_initial_bank[index];
    }
    hex[sizeof(bank) * 2] = 0;
    uintptr_t zone = lifecycle_discard_pointer((uintptr_t)LX(PLAYER_ZONE_GLOBAL_VA));
    uintptr_t animation_a = lifecycle_discard_pointer((uintptr_t)LX(SEAT_PREFLIGHT_ANIMATION_HANDLE_A));
    uintptr_t animation_b = lifecycle_discard_pointer((uintptr_t)LX(SEAT_PREFLIGHT_ANIMATION_HANDLE_B));
    uintptr_t controller = lifecycle_discard_pointer(lifecycle_discard_ship + 0x200);
    uintptr_t table = controller ? lifecycle_discard_pointer(controller) : 0;
    uint32_t zone_count = 0;
    if (zone) {
        lifecycle_discard_read(zone + 0x40, &zone_count, 4);
    }
    input_context_census_sample contexts;
    const input_context_census_operations operations = {NULL, player_seat_preflight_read};
    int context_ok = lifecycle_discard_context_collect(&operations, &contexts);
    char context_payloads[sizeof(contexts.payloads) * 2 + 1];
    const unsigned char *payload_bytes = (const unsigned char *)contexts.payloads;
    for (unsigned index = 0; index < sizeof(contexts.payloads); ++index) {
        context_payloads[index * 2] = digits[payload_bytes[index] >> 4];
        context_payloads[index * 2 + 1] = digits[payload_bytes[index] & 15];
    }
    context_payloads[sizeof(contexts.payloads) * 2] = 0;
    char anchor_suffix[160] = "";
    if (lifecycle_context_policy_suffix) {
        lifecycle_discard_require(lifecycle_context_policy_suffix(anchor_suffix, sizeof anchor_suffix),
                                  "context_policy_snapshot_receipt");
    } else if (atomic_load_explicit(&player_camera_anchor.active, memory_order_acquire)) {
        unsigned char digest[32];
        char digest_hex[65];
        lockstep_sha256(player_camera_anchor.payloads, sizeof player_camera_anchor.payloads, digest);
        hex32(digest, digest_hex);
        snprintf(anchor_suffix, sizeof anchor_suffix, " context_anchor_v=1 manager=%llx full_hash=%s",
                 (unsigned long long)player_camera_anchor.manager, digest_hex);
    }
    char message[4096];
    snprintf(message, sizeof(message),
        "X4LAN LIFECYCLE_DISCARD snapshot=%s changed_bytes=%u bank=%s zone=%llx zone_count=%u "
        "animation_a=%llx animation_b=%llx controller=%llx controller_vtable=%llx "
        "contexts_ok=%d contexts_valid=%u contexts_nodes=%u contexts_mask=%u contexts_payloads=%s%s\n", label, changed, hex,
        (unsigned long long)zone, zone_count, (unsigned long long)animation_a,
        (unsigned long long)animation_b, (unsigned long long)controller,
        (unsigned long long)table, context_ok, contexts.tree_valid, contexts.node_count,
        contexts.target_present, context_payloads, anchor_suffix);
    lifecycle_discard_write(message);
    /* A legitimate native removal of requester-6 entries is an observation.
     * The census distinguishes missing targets from a structurally invalid tree. */
    lifecycle_discard_require(contexts.tree_valid, "context_tree_unreadable");
}

#include "gate_player_lifecycle_routes.h"

/* The disposable physics mode protects the native ownership transition before
 * Take can publish a controller. Ordinary lifecycle probes do not install this
 * resident guard. Definitions follow in gate_player_physics_discard.h. */
static void player_physics_prepare_take(uintptr_t ship);
static void player_physics_complete_take(uintptr_t controller);

static void lifecycle_discard_begin(unsigned boundary_state, int isolate, int physics) {
    lifecycle_discard_open_log();
    if (physics == 2) {
        lifecycle_discard_write("X4LAN LIFECYCLE_DISCARD physics=frame tick=5 event=begin\n");
    }
    lifecycle_discard_stage("entered");
    /* FWR_MAIN_PARKED (4) and FWR_ENTERED (16), no RELEASED (32) or POISONED (128). */
    lifecycle_discard_require((boundary_state & (4u | 16u | 32u | 128u)) == (4u | 16u) &&
        stats.issued == 5 && factory_probe_joined_boundary() && player_seat_preflight_retained() &&
        factory_probe_release_tick > 5 && lifecycle_discard_pins(), "admission");
    uintptr_t player = factory_probe_state.player2;
    uintptr_t canonical = factory_probe_player1;
    lifecycle_discard_require(factory_probe_singleton(canonical) &&
        !lifecycle_discard_pointer(player + 0x70) && player_target_hold_acquire() &&
        player_target_hold_check(), "player_or_target_hold");
    lifecycle_discard_ship = player_target_hold_resident.identity.target;
    uintptr_t room = player_target_hold_resident.identity.room;
    uintptr_t ship_table = lifecycle_discard_pointer(lifecycle_discard_ship);
    lifecycle_discard_require(
        lifecycle_discard_pointer(ship_table + 1089u * 8) == (uintptr_t)LX(0x17a34f0) &&
        lifecycle_discard_pointer(ship_table + 1090u * 8) == (uintptr_t)LX(0x12bafe0), "ship_vtable");
    uintptr_t head = lifecycle_discard_pointer(lifecycle_discard_ship + 0x200);
    lifecycle_discard_require(!head ||
        (lifecycle_discard_pointer(head) == (uintptr_t)LX(SEAT_PREFLIGHT_NAVIGATION_CONTROLLER_VTABLE) &&
         ((void *(*)(void *))LX(0xa863a0))((void *)head) == NULL), "existing_controller");
    lifecycle_discard_require(
        !lifecycle_discard_pointer((uintptr_t)LX(SEAT_PREFLIGHT_ANIMATION_HANDLE_A)) &&
        !lifecycle_discard_pointer((uintptr_t)LX(SEAT_PREFLIGHT_ANIMATION_HANDLE_B)), "animation_active");

    uintptr_t pilot_post = lifecycle_discard_pointer((uintptr_t)LX(SEAT_PREFLIGHT_PILOT_POST_GLOBAL));
    uint64_t post_index = 0;
    unsigned char player_post = 0;
    lifecycle_discard_require(pilot_post != 0, "pilot_post_missing");
    lifecycle_discard_read(pilot_post + SEAT_PREFLIGHT_POST_INDEX_OFFSET, &post_index, 8);
    lifecycle_discard_read(pilot_post + SEAT_PREFLIGHT_POST_PLAYER_FLAG_OFFSET, &player_post, 1);
    lifecycle_discard_require(player_post == 1 && post_index < 32, "pilot_post_index");
    uintptr_t posts = lifecycle_discard_pointer(lifecycle_discard_ship + 0x410);
    uintptr_t posts_end = lifecycle_discard_pointer(lifecycle_discard_ship + 0x418);
    lifecycle_discard_require(posts && posts_end >= posts &&
        posts_end - posts >= (post_index + 1) * 16 &&
        !lifecycle_discard_pointer(posts + post_index * 16 + 8), "occupied_or_invalid_post");
    uint32_t position = 0;
    uint32_t invalid_position = 0;
    unsigned char assist[4];
    lifecycle_discard_read((uintptr_t)LX(SEAT_PREFLIGHT_PILOT_POSITION_GLOBAL), &position, 4);
    lifecycle_discard_read((uintptr_t)LX(SEAT_PREFLIGHT_INVALID_POSITION_GLOBAL), &invalid_position, 4);
    lifecycle_discard_read(canonical + SEAT_PREFLIGHT_PLAYER_ASSIST_OFFSET, assist, 4);
    lifecycle_discard_require(position != 0 && position != invalid_position, "pilot_position");
    lifecycle_discard_snapshot("initial", 1);
    if (isolate) {
        lifecycle_isolation_prepare();
    }

    /* Owned player, ship and cockpit references stay resident until process
     * termination. The lifecycle probe exits before simulation; the separately
     * admitted physics diagnostic allows one bounded worker pass then exits. */
    lifecycle_discard_controls[post_index] = lifecycle_discard_ship;
    uintptr_t controlled_vector[3] = {
        (uintptr_t)lifecycle_discard_controls,
        (uintptr_t)&lifecycle_discard_controls[post_index + 1],
        (uintptr_t)&lifecycle_discard_controls[post_index + 1],
    };
    memcpy((void *)(player + 0x308), controlled_vector, sizeof(controlled_vector));
    memcpy((void *)(player + 0x70), &room, 8);
    memcpy((void *)(player + SEAT_PREFLIGHT_PLAYER_POSITION_OFFSET), &position, 4);
    memcpy((void *)(player + SEAT_PREFLIGHT_PLAYER_ASSIST_OFFSET), assist, 4);
    memcpy((void *)(posts + post_index * 16 + 8), &player, 8);
    ((void (*)(void *))LX(P1_REFERENCE_ACQUIRE_VA))((void *)canonical);
    lifecycle_discard_stage("before_singleton");
    void **destination = (void **)(factory_probe_universe + 0x238);
    void **assigned = ((void **(*)(void **, void *))LX(FACTORY_PROBE_ASSIGN_VA))(
        destination, (void *)player);
    lifecycle_discard_require(assigned == destination && factory_probe_singleton(player), "singleton_assign");
    lifecycle_discard_stage("after_singleton");
    /* Model the future scope's context flag explicitly. The prepared snapshot
     * separates our changes from the take/release effects in subsequent rows. */
    *(unsigned char *)LX(SEAT_PREFLIGHT_CONTEXTS_REQUESTED) = 0;
    lifecycle_discard_snapshot("prepared", 0);
    lifecycle_discard_stage("before_take");
    if (physics) {
        player_physics_prepare_take(lifecycle_discard_ship);
    }
    ((void (*)(void *))LX(0x17a34f0))((void *)lifecycle_discard_ship);
    lifecycle_discard_stage("after_take");
    lifecycle_discard_controller = (uintptr_t)((void *(*)(void))LX(0x1dd19d0))();
    lifecycle_discard_require(lifecycle_discard_controller &&
        lifecycle_discard_pointer(lifecycle_discard_controller) == (uintptr_t)LX(SEAT_PREFLIGHT_CONTROLLER_VTABLE) &&
        lifecycle_discard_pointer(lifecycle_discard_controller + 0x20) == lifecycle_discard_ship &&
        lifecycle_discard_pointer(lifecycle_discard_ship + 0x200) == lifecycle_discard_controller,
        "take_controller");
    if (physics) {
        player_physics_complete_take(lifecycle_discard_controller);
    }
    lifecycle_discard_snapshot("taken", 0);
}

static _Noreturn void player_lifecycle_discard_run(unsigned boundary_state) {
    const char *mode = getenv("X4_DEV_PLAYER_LIFECYCLE");
    int injected_input = !strcmp(mode, "take-release-input-discard");
    int routes = injected_input || !strcmp(mode, "take-release-routes-discard");
    int update = routes || !strcmp(mode, "take-release-update-discard");
    int isolate = update || !strcmp(mode, "take-release-isolated-discard");
    lifecycle_discard_begin(boundary_state, isolate, 0);
    if (injected_input) {
        lifecycle_discard_input_press();
    }
    if (update) {
        lifecycle_discard_update_once(injected_input);
        if (injected_input) {
            lifecycle_discard_input_release();
        }
        lifecycle_discard_snapshot("updated", 0);
    }
    if (routes) {
        lifecycle_discard_compare_routes();
    }
    if (isolate) {
        lifecycle_isolation_arm_release();
    }
    lifecycle_discard_stage("before_release");
    ((void (*)(void *))LX(0x12bafe0))((void *)lifecycle_discard_ship);
    lifecycle_discard_stage("after_release");
    if (isolate) {
        lifecycle_isolation_finish_release();
    }
    lifecycle_discard_require(lifecycle_discard_pointer(lifecycle_discard_ship + 0x200) !=
                              lifecycle_discard_controller, "controller_not_replaced");
    lifecycle_discard_snapshot("released", 0);
    if (isolate) {
        lifecycle_isolation_restore_globals();
        lifecycle_discard_snapshot("canonical_restored", 0);
        lifecycle_discard_exit(1, injected_input ? "input_lifecycle_returned" :
                              routes ? "routed_lifecycle_returned" :
                              update ? "updated_lifecycle_returned" : "isolated_lifecycle_returned");
    }
    lifecycle_discard_exit(1, "lifecycle_returned");
}
#endif

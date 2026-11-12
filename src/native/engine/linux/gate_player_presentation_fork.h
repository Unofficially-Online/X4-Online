#ifndef X4_GATE_PLAYER_PRESENTATION_FORK_H
#define X4_GATE_PLAYER_PRESENTATION_FORK_H

#include "gate_player_startup_phases.h"

/* One disposable child, requested on the admitted worker and executed by the
 * main-frame quiet boundary. The parent never overlays the live player. */
#include "player_presentation_fork_probe.h"
#include <sys/uio.h>

typedef struct {
    presentation_fork_request request;
    presentation_fork_maps initial_maps;
    uintptr_t universe, p1_room, p1_ship, p2_ship, controller;
    uintptr_t worker_tls;
    pid_t worker_tid;
    uint64_t (*occupied)(void);
    __typeof__(fs) saved_fs;
    fs_lock saved_locks[FS_LOCKS];
    uint64_t held_since;
    char quiet_reason[40], quiet_blocker[64];
    unsigned other_running;
} native_presentation_fork;

static native_presentation_fork presentation_fork;
static _Atomic unsigned presentation_fork_state; /* 0 off, 1 armed, 2 complete, 3 failed */
static _Atomic unsigned presentation_fork_attempted;
static _Atomic unsigned presentation_fork_result_read;
static char presentation_fork_output[768];

#ifndef PRESENTATION_FORK_SYSCALL
#define PRESENTATION_FORK_SYSCALL syscall
#endif

static int presentation_fork_read(void *unused, uintptr_t address, void *out, size_t length) {
    (void)unused;
    struct iovec local = {out, length}, remote = {(void *)address, length};
    return PRESENTATION_FORK_SYSCALL(SYS_process_vm_readv, (long)syscall(SYS_getpid),
                                     &local, 1UL, &remote, 1UL, 0UL) == (long)length;
}

/* Called by the child only, after a private range has been authenticated. */
static int presentation_fork_write(void *unused, uintptr_t address, const void *in, size_t length) {
    (void)unused;
    struct iovec local = {(void *)in, length}, remote = {(void *)address, length};
    return PRESENTATION_FORK_SYSCALL(SYS_process_vm_writev, (long)syscall(SYS_getpid),
                                     &local, 1UL, &remote, 1UL, 0UL) == (long)length;
}

static int presentation_fork_word(uintptr_t at, uintptr_t expected) {
    uintptr_t word = 0;
    return presentation_fork_read(NULL, at, &word, sizeof word) && word == expected;
}

static int presentation_fork_pins(uint64_t (**occupied)(void)) {
    static const lx_site pins[] = {
        {"occupied", 0xa44e40, 0x53, "c4ae051dfa320a46f8049b7e35f5f686c7f4455e98f33ce52b13c2cbe0c134d3"},
        {"ship", 0x169af50, 0x8a, "46a9d0b2850828840e5fef14431aacf27f815e6956225caa57e9ca3fdaa27f67"},
        {"controlled", 0x1699f20, 0x6b, "875a39c3fadc0608768592821830bf99016fb500cdc452f788f7e69fa2091196"},
        {"live", 0x189d8f0, 0x37, "7c5c18f7f341ce22b68eefdc5479b7e8c7e4d55fa0d259ed3e708edd44b951db"},
        {"cached", 0x189f8d0, 0xae, "6b1cdc06228630f355c25b7b29a879b992fedb9d418f4b9d449376b669469a1e"},
        {"release", 0xa14540, 0x25, "7368cef95d1efcc8cda06dfd53df92ca026f1da024481b82f3c7a86b8db75587"},
        {"player predicate", 0x136c820, 68, "38cb2d7e1fe206c4a214526bcfd0d4274ce8237d350f9711334de76d950a578c"},
        {"ship class", 0xa389f0, 48, "6cc846478894fc1636f1416467c973ff24c09d48b3207c8f8b4fa37d6bfd6e42"}
    };
    unsigned char digest[32]; char hex[65];
    if (!occupied || !gate_file_sha256(gate_env.exe, LX_FILE_SIZE, digest)) {
        dprintf(2, "X4 PRESENTATION FORK PIN stage=image_read\n");
        return 0;
    }
    hex32(digest, hex);
    if (strcmp(hex, PLAYER_BODY_IMAGE_SHA)) {
        dprintf(2, "X4 PRESENTATION FORK PIN stage=image_hash expected=%s actual=%s\n", PLAYER_BODY_IMAGE_SHA, hex);
        return 0;
    }
    *occupied = (uint64_t (*)(void))LX(0xa44e40);
    if (!site_ok(&pins[0])) {
        const char *sha; uint64_t size; player_view_query_api api;
        if (!view_query_expected_library(&sha, &size)) {
            dprintf(2, "X4 PRESENTATION FORK PIN stage=occupied_library_env\n");
            return 0;
        }
        if (!player_view_query_provider(sha, size, &api)) {
            dprintf(2, "X4 PRESENTATION FORK PIN stage=occupied_provider\n");
            return 0;
        }
        if (!view_query_occupied_original_pin()) {
            dprintf(2, "X4 PRESENTATION FORK PIN stage=occupied_original\n");
            return 0;
        }
        *occupied = (uint64_t (*)(void))api.information.original_trampoline;
    }
    for (unsigned i = 1; i < sizeof pins / sizeof pins[0]; ++i) {
        if (!site_ok(&pins[i])) {
            unsigned char copy[512], actual_digest[32]; char actual_hex[65] = "unreadable";
            if (pins[i].length <= sizeof copy && read_local(LX(pins[i].va), copy, pins[i].length)) {
                lockstep_sha256(copy, pins[i].length, actual_digest);
                hex32(actual_digest, actual_hex);
            }
            dprintf(2, "X4 PRESENTATION FORK PIN stage=function index=%u va=%llx name=%s expected=%s actual=%s\n",
                    i, (unsigned long long)pins[i].va, pins[i].name, pins[i].sha256, actual_hex);
            return 0;
        }
    }
    static const uintptr_t slots[][2] = {
        {SEAT_PREFLIGHT_PLAYER_VTABLE + 0x1ce0, 0x136c820},
        {SEAT_PREFLIGHT_SHIP_VTABLE + 0x11f0, 0xa389f0},
        {SEAT_PREFLIGHT_CONTROL_ROOM_VTABLE + 0x11f0, 0xa398e0},
        {SEAT_PREFLIGHT_COCKPIT_VTABLE + 0x11f0, 0xa39930}
    };
    for (unsigned i = 0; i < sizeof slots / sizeof slots[0]; ++i) {
        uintptr_t actual = 0;
        int readable = presentation_fork_read(NULL, (uintptr_t)LX(slots[i][0]), &actual, sizeof actual);
        if (!readable || actual != (uintptr_t)LX(slots[i][1])) {
            dprintf(2, "X4 PRESENTATION FORK PIN stage=slot index=%u va=%llx expected=%llx readable=%d errno=%d actual=%llx\n",
                    i, (unsigned long long)slots[i][0], (unsigned long long)(uintptr_t)LX(slots[i][1]),
                    readable, readable ? 0 : errno, (unsigned long long)actual);
            return 0;
        }
    }
    /* Pin the room class function bodies and the class words actually indexed. */
    static const lx_site room_pins[] = {
        {"control room class", 0xa398e0, 0x30, "2074c11d6aa602fe94e95bf83d1f851227d00088bcefb4850d328bc7041dc429"},
        {"cockpit class", 0xa39930, 0x30, "8bf9e521822333a1f5b82954f8081829f44d80b19b641fd7e96a19ed85b416f9"}
    };
    for (unsigned i = 0; i < 2; ++i) {
        if (!site_ok(&room_pins[i])) {
            unsigned char copy[48], actual_digest[32]; char actual_hex[65] = "unreadable";
            if (read_local(LX(room_pins[i].va), copy, sizeof copy)) {
                lockstep_sha256(copy, sizeof copy, actual_digest);
                hex32(actual_digest, actual_hex);
            }
            dprintf(2, "X4 PRESENTATION FORK PIN stage=room_function index=%u va=%llx expected=%s actual=%s\n",
                    i, (unsigned long long)room_pins[i].va, room_pins[i].sha256, actual_hex);
            return 0;
        }
    }
    static const uintptr_t class_words[][2] = {
        {0x2c822bc, 0x401}, {0x2c81d04, 0x42000}, {0x2c81d1c, 0},
        {0x2c81ce4, 0x42001}, {0x2c81cfc, 0}
    };
    for (unsigned i = 0; i < sizeof class_words / sizeof class_words[0]; ++i) {
        uint32_t value = 0;
        int readable = presentation_fork_read(NULL, (uintptr_t)LX(class_words[i][0]), &value, 4);
        if (!readable || value != class_words[i][1]) {
            dprintf(2, "X4 PRESENTATION FORK PIN stage=class_word index=%u va=%llx expected=%x readable=%d errno=%d actual=%x\n",
                    i, (unsigned long long)class_words[i][0], (unsigned)class_words[i][1], readable, readable ? 0 : errno, value);
            return 0;
        }
    }
    return 1;
}

static int presentation_fork_private_range(void *context, uintptr_t address, size_t size) {
    native_presentation_fork *probe = context;
    return presentation_fork_mapping_private(&probe->initial_maps, address, size, 0);
}

static int presentation_fork_owner(void *context, uintptr_t p1, uintptr_t p2,
                                   uintptr_t room, uint32_t bank) {
    native_presentation_fork *probe = context;
    const presentation_fork_request *request = &probe->request;
    const player_target_hold *hold = &player_target_hold_resident;
    uint32_t target_count = 0, room_count = 0, target_marker = 0, room_marker = 0;
    return p1 == request->player1 && p2 == request->player2 && room == request->room &&
        bank == request->bank && atomic_load_explicit(&player_physics_ready, memory_order_acquire) == 1 &&
        factory_probe_state.player2_owned == 1 && !factory_probe_state.player2_uncertain &&
        factory_probe_state.player2 == p2 && hold->target_owned == 1 && hold->room_owned == 1 &&
        !hold->busy && !hold->uncertain && !hold->pending &&
        hold->target_reference == probe->p2_ship && hold->room_reference == room &&
        hold->identity.target == probe->p2_ship && hold->identity.room == room &&
        presentation_fork_read(NULL, probe->p2_ship + 0x40, &target_count, 4) &&
        presentation_fork_read(NULL, probe->p2_ship + 0x44, &target_marker, 4) &&
        presentation_fork_read(NULL, room + 0x40, &room_count, 4) &&
        presentation_fork_read(NULL, room + 0x44, &room_marker, 4) &&
        target_count > 1 && target_count < UINT32_MAX && target_marker == 2 &&
        room_count > 1 && room_count < UINT32_MAX && room_marker == 2 &&
        presentation_fork_word((uintptr_t)LX(FACTORY_PROBE_UNIVERSE_VA), probe->universe) &&
        presentation_fork_word(probe->universe + 0x238, p1) &&
        presentation_fork_word(p1, (uintptr_t)LX(SEAT_PREFLIGHT_PLAYER_VTABLE)) &&
        presentation_fork_word(p2, (uintptr_t)LX(SEAT_PREFLIGHT_PLAYER_VTABLE)) &&
        presentation_fork_word(probe->p2_ship, (uintptr_t)LX(SEAT_PREFLIGHT_SHIP_VTABLE)) &&
        presentation_fork_word(room + 0x70, probe->p2_ship) &&
        presentation_fork_word(probe->p2_ship + 0x200, probe->controller) &&
        presentation_fork_word(probe->controller + 0x20, probe->p2_ship) &&
        presentation_fork_word(probe->controller, (uintptr_t)&player_physics_routes.routes.table[2]);
}

static int presentation_fork_chain(native_presentation_fork *probe, const presentation_fork_maps *maps,
                                   uintptr_t player, uintptr_t room, uintptr_t ship, uint64_t runtime, int require_cached) {
    uintptr_t table = 0, parent = 0;
    uint32_t room_gate = 1, ship_gate = 1;
    uint64_t ship_id = 0;
    player_presentation_vector vector;
    if (!presentation_fork_mapping_private(maps, room, 0x80, 1) ||
        !presentation_fork_mapping_private(maps, ship, 0x208, 1) ||
        !presentation_fork_read(NULL, room, &table, 8) ||
        (table != (uintptr_t)LX(SEAT_PREFLIGHT_CONTROL_ROOM_VTABLE) &&
         table != (uintptr_t)LX(SEAT_PREFLIGHT_COCKPIT_VTABLE)) ||
        !presentation_fork_read(NULL, room + 0x68, &room_gate, 4) || room_gate ||
        !presentation_fork_word(room + 0x70, ship) ||
        !presentation_fork_word(ship, (uintptr_t)LX(SEAT_PREFLIGHT_SHIP_VTABLE)) ||
        !presentation_fork_read(NULL, ship + 0x68, &ship_gate, 4) || ship_gate ||
        !presentation_fork_read(NULL, ship + 8, &ship_id, 8) || ship_id != runtime ||
        !presentation_fork_read(NULL, player + 0x70, &parent, 8) || parent != room ||
        (require_cached && !presentation_fork_word(player + 0xe0 + probe->request.bank * 0x80, room)) ||
        !presentation_fork_read(NULL, player + 0x308, &vector, sizeof vector) ||
        !presentation_overlay_vector_valid(&vector) || vector.end == vector.begin ||
        vector.end - vector.begin > PRESENTATION_FORK_VECTOR_MAX ||
        !presentation_fork_mapping_private(maps, vector.begin, vector.end - vector.begin, 0)) return 0;
    int found = 0;
    for (uintptr_t at = vector.begin; at < vector.end; at += sizeof(uintptr_t)) {
        uintptr_t item = 0;
        if (!presentation_fork_read(NULL, at, &item, sizeof item)) return 0;
        if (item && item != ship) return 0;
        if (item) found = 1;
    }
    return found;
}

static int presentation_fork_private_graph(void *context, const presentation_fork_request *request,
                                           const presentation_fork_maps *maps) {
    native_presentation_fork *probe = context;
    uintptr_t tls = (uintptr_t)gate_tls_self();
    uint32_t worker_view = 0, bank = 2;
    return request == &probe->request && tls && probe->worker_tls &&
        presentation_fork_mapping_private(maps, probe->universe + 0x238, 8, 0) &&
        presentation_fork_mapping_private(maps, probe->controller, 0x28, 1) &&
        presentation_fork_mapping_private(maps, probe->worker_tls + GATE_TLS_LINUX_VIEW, 4, 0) &&
        presentation_fork_mapping_private(maps, tls + GATE_TLS_LINUX_VIEW, 4, 1) &&
        presentation_fork_read(NULL, probe->worker_tls + GATE_TLS_LINUX_VIEW, &worker_view, 4) && worker_view == 1 &&
        presentation_fork_read(NULL, (uintptr_t)LX(0x3e77430), &bank, 4) && bank == request->bank &&
        presentation_fork_owner(probe, request->player1, request->player2, request->room, request->bank) &&
        presentation_fork_chain(probe, maps, request->player1, probe->p1_room, probe->p1_ship, request->expected_p1_ship, 1) &&
        presentation_fork_chain(probe, maps, request->player2, request->room, probe->p2_ship, request->expected_p2_ship, 0);
}

static int presentation_fork_quiet_enter(void *context) {
    native_presentation_fork *probe = context;
    if (fs.state != FS_OFF || atomic_load_explicit(&presentation_fork_reservation, memory_order_acquire) != 1) return 0;
    probe->saved_fs = fs;
    memcpy(probe->saved_locks, fs_locks, sizeof fs_locks);
    fs.worker_tid = probe->worker_tid;
    fs.worker_tls = (unsigned char *)probe->worker_tls;
    if (!fs_quiet(&probe->held_since)) {
        memcpy(probe->quiet_reason, fs.reason, sizeof probe->quiet_reason);
        memcpy(probe->quiet_blocker, fs.blocker, sizeof probe->quiet_blocker);
        probe->other_running = fs.other_running;
        fs = probe->saved_fs;
        memcpy(fs_locks, probe->saved_locks, sizeof fs_locks);
        return 0;
    }
    if (fs.other_running) {
        memcpy(probe->quiet_reason, "other_running", sizeof "other_running");
        probe->other_running = fs.other_running;
        fs_release(FS_RP_WORDS);
        fs = probe->saved_fs;
        memcpy(fs_locks, probe->saved_locks, sizeof fs_locks);
        return 0;
    }
    return 1;
}

static void presentation_fork_quiet_leave(void *context) {
    native_presentation_fork *probe = context;
    fs_release(FS_RP_WORDS);
    fs = probe->saved_fs;
    memcpy(fs_locks, probe->saved_locks, sizeof fs_locks);
}

static int presentation_fork_child_setup(void *context) {
    native_presentation_fork *probe = context;
    unsigned char *tls = gate_tls_self();
    uint32_t worker_view = 0;
    if (!tls || !presentation_fork_read(NULL, probe->worker_tls + GATE_TLS_LINUX_VIEW, &worker_view, 4) || worker_view != 1)
        return 0;
    memcpy(tls + GATE_TLS_LINUX_VIEW, &worker_view, 4);
    return 1;
}
static uint64_t presentation_fork_occupied(void *context) {
    return ((native_presentation_fork *)context)->occupied();
}
static uintptr_t presentation_fork_live(void *context, uintptr_t player) {
    (void)context;
    return (uintptr_t)((void *(*)(void *, int, unsigned char))LX(0x189d8f0))((void *)player, 0x19, 0);
}
static uintptr_t presentation_fork_cached(void *context, uintptr_t player) {
    (void)context;
    void *result = NULL;
    ((void **(*)(void **, void *, int, unsigned char))LX(0x189f8d0))(&result, (void *)player, 0x19, 0);
    return (uintptr_t)result;
}
static int presentation_fork_release(void *context, uintptr_t room) {
    (void)context;
    ((void (*)(void *))LX(0xa14540))((void *)(room + 0x38));
    return 1;
}

static int lua_player_presentation_fork_arm(lua_State *state) {
    int accepted = 0;
    const char *enabled = getenv("X4_DEV_PRESENTATION_FORK");
    if (!enabled || strcmp(enabled, "1")) {
        if (enabled) {
            static const char message[] = "X4 PRESENTATION FORK ARMREFUSED reason=environment\n";
            (void)write(2, message, sizeof message - 1);
        }
        p_lua_pushnumber(state, 0);
        return 1;
    }
    if (atomic_exchange_explicit(&presentation_fork_attempted, 1, memory_order_acq_rel)) {
        (void)write(2, "X4 PRESENTATION FORK ARMREFUSED reason=reentry\n", sizeof "X4 PRESENTATION FORK ARMREFUSED reason=reentry\n" - 1);
        p_lua_pushnumber(state, 0);
        return 1;
    }
    const char *reason = "admission";
    uint32_t bank = 2;
    uintptr_t p1_room = 0;
    long worker_tid = syscall(SYS_gettid);
    if (atomic_load(&presentation_fork_state) ||
        !accept_ui() || !stats.active || (uint64_t)stats.issued != player_startup()->presentation_probe || boot_phase != 3 ||
        (stats.slot != 1 && stats.slot != 2) || wire_clients != 2 || !keyboard_probe_requested ||
        keyboard_probe_queue.config.independent_players != 1 || player_recurring_physics != 3 ||
        atomic_load_explicit(&player_physics_ready, memory_order_acquire) != 1 ||
        !stock_observer_worker_boundary() || !gate_tls_worker(gate_tls_self()) ||
        player_routes_scope || player_network_busy || player_physics_replay_busy ||
        player_physics_upkeep_busy || keyboard_probe_inflight || atomic_load(&stock_observer_input_depth) ||
        atomic_load(&sko_session.wrapper_depth) || !player_physics_identity() ||
        !factory_probe_state.player2_owned || factory_probe_state.player2_uncertain ||
        fs.state != FS_OFF || worker_tid <= 0 || worker_tid > INT32_MAX ||
        !read_local(LX(0x3e77430), &bank, 4) || bank > 1 ||
        !read_local((void *)(factory_probe_player1 + 0x70), &p1_room, 8) || !p1_room) goto finish;
    uint64_t (*occupied)(void) = NULL;
    reason = "pins";
    if (!presentation_fork_pins(&occupied)) goto finish;
    reason = "reservation";
    unsigned expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&presentation_fork_reservation, &expected, 1,
                                                  memory_order_acq_rel, memory_order_acquire)) goto finish;
    memset(&presentation_fork, 0, sizeof presentation_fork);
    presentation_fork.request = (presentation_fork_request){
        .player1 = factory_probe_player1, .player2 = factory_probe_state.player2,
        .room = player_target_hold_resident.identity.room,
        .expected_p1_ship = factory_probe_identity.runtime,
        .expected_p2_ship = player_target_hold_resident.identity.target_runtime,
        .bank = bank, .timeout_ms = 1000
    };
    presentation_fork.universe = factory_probe_universe;
    presentation_fork.p1_room = p1_room;
    presentation_fork.p1_ship = factory_probe_identity.ship;
    presentation_fork.p2_ship = lifecycle_discard_ship;
    presentation_fork.controller = lifecycle_discard_controller;
    presentation_fork.worker_tls = (uintptr_t)gate_tls_self();
    presentation_fork.worker_tid = (pid_t)worker_tid;
    presentation_fork.occupied = occupied;
    reason = "ship_ids";
    if (!presentation_fork.request.expected_p1_ship || !presentation_fork.request.expected_p2_ship ||
        presentation_fork.request.expected_p1_ship == presentation_fork.request.expected_p2_ship) {
        atomic_store_explicit(&presentation_fork_reservation, 0, memory_order_release);
        goto finish;
    }
    atomic_store_explicit(&presentation_fork_state, 1, memory_order_release);
    accepted = 1;
finish:
    if (!accepted) dprintf(2, "X4 PRESENTATION FORK ARMREFUSED reason=%s\n", reason);
    p_lua_pushnumber(state, accepted);
    return 1;
}

static void player_presentation_fork_frame(uint64_t completed) {
    if (atomic_load_explicit(&presentation_fork_state, memory_order_acquire) != 1) return;
    native_presentation_fork *probe = &presentation_fork;
    const char *reason = "frame";
    presentation_fork_result result = {0};
    int success = 0;
    if (!player_startup_is_presentation_probe_tick(player_startup(), completed) ||
        GetCurrentThreadId() != main_thread || fs.state != FS_OFF ||
        atomic_load_explicit(&presentation_fork_reservation, memory_order_acquire) != 1) goto finish;
    uint64_t (*occupied)(void) = NULL;
    reason = "pins";
    if (!presentation_fork_pins(&occupied) || occupied != probe->occupied) goto finish;
    reason = "maps";
    int maps_fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (maps_fd < 0) {
        dprintf(2, "X4 PRESENTATION FORK MAP stage=open length=0 errno=%d\n", errno);
        goto finish;
    }
    presentation_fork_maps_diagnostic maps_diagnostic;
    int maps_ok = presentation_fork_maps_read_detail(maps_fd, &probe->initial_maps, &maps_diagnostic);
    close(maps_fd);
    if (!maps_ok) {
        dprintf(2, "X4 PRESENTATION FORK MAP stage=%s length=%zu errno=%d\n",
                maps_diagnostic.stage, maps_diagnostic.length, maps_diagnostic.error);
        goto finish;
    }
    dprintf(2, "X4 MAP INVENTORY phase=presentation length=%zu capacity=%u\n",
            maps_diagnostic.length, PRESENTATION_FORK_MAP_BYTES);
    const presentation_fork_ops operations = {
        .context = probe, .quiet_enter = presentation_fork_quiet_enter,
        .quiet_leave = presentation_fork_quiet_leave, .private_range = presentation_fork_private_range,
        .private_graph = presentation_fork_private_graph, .child_setup = presentation_fork_child_setup,
        .owner = presentation_fork_owner, .read = presentation_fork_read, .write = presentation_fork_write,
        .occupied = presentation_fork_occupied, .live_room = presentation_fork_live,
        .cached_room = presentation_fork_cached, .release_room = presentation_fork_release
    };
    success = player_presentation_fork_run(&probe->request, &operations, &result);
    reason = result.reason ? result.reason : "missing_reason";
    if (result.maps_diagnostic.stage && strcmp(result.maps_diagnostic.stage, "ok"))
        dprintf(2, "X4 PRESENTATION FORK MAP stage=%s length=%zu errno=%d\n",
                result.maps_diagnostic.stage, result.maps_diagnostic.length, result.maps_diagnostic.error);
    if (result.lease_held) {
        static const char prefix[] = "X4 PRESENTATION FORK FATAL retained_lease=1 reason=";
        (void)write(2, prefix, sizeof prefix - 1);
        (void)write(2, reason, strlen(reason));
        (void)write(2, "\n", 1);
        _exit(223);
    }
    if (success) {
        const presentation_fork_receipt *r = &result.receipt;
        int length = snprintf(presentation_fork_output, sizeof presentation_fork_output,
            "v=1 status=pass parent_unchanged=1 child_reaped=1 lease_held=0 "
            "p1=%llx p2=%llx room=%llx p1_ship=%llu p2_ship=%llu before=%llu overlay=%llu restored=%llu "
            "live=%llx cached=%llx refs=%u,%u,%u marker=%u,%u release_attempts=%u",
            (unsigned long long)probe->request.player1, (unsigned long long)probe->request.player2,
            (unsigned long long)probe->request.room, (unsigned long long)probe->request.expected_p1_ship,
            (unsigned long long)probe->request.expected_p2_ship, (unsigned long long)r->before_ship,
            (unsigned long long)r->overlay_ship, (unsigned long long)r->restored_ship,
            (unsigned long long)r->live_room, (unsigned long long)r->cached_room,
            r->refs_before, r->refs_acquired, r->refs_after, r->marker_before, r->marker_after, r->release_attempts);
        if (length <= 0 || (size_t)length >= sizeof presentation_fork_output) { success = 0; reason = "format"; }
    }
finish:
    if (success) {
        dprintf(2, "X4 PRESENTATION FORK RESULT %s\n", presentation_fork_output);
    } else {
        dprintf(2, "X4 PRESENTATION FORK FAILURE reason=%s quiet=%s blocker=%s other_running=%u\n",
                reason, probe->quiet_reason[0] ? probe->quiet_reason : "none",
                probe->quiet_blocker[0] ? probe->quiet_blocker : "none", probe->other_running);
    }
    atomic_store_explicit(&presentation_fork_reservation, 0, memory_order_release);
    atomic_store_explicit(&presentation_fork_state, success ? 2 : 3, memory_order_release);
}

static int lua_player_presentation_fork_result(lua_State *state) {
    if (!accept_ui() || !stats.active || (uint64_t)stats.issued != player_startup()->presentation_arm ||
        !stock_observer_worker_boundary() || !gate_tls_worker(gate_tls_self()) ||
        syscall(SYS_gettid) != presentation_fork.worker_tid || !player_physics_identity() ||
        atomic_load_explicit(&presentation_fork_state, memory_order_acquire) != 2 ||
        atomic_exchange_explicit(&presentation_fork_result_read, 1, memory_order_acq_rel)) {
        p_lua_pushnil(state);
    } else {
        p_lua_pushlstring(state, presentation_fork_output, strlen(presentation_fork_output));
    }
    return 1;
}
#endif

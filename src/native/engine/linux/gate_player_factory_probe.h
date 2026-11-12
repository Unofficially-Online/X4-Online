#ifndef X4_GATE_PLAYER_FACTORY_PROBE_H
#define X4_GATE_PLAYER_FACTORY_PROBE_H

/* Private-profile diagnostic, not player controls or an engine rollback.
 * Include after gate_stock_keyboard_identity.h. Root must install/restore only
 * at the existing admitted main boundary (all relevant initializer executions
 * excluded), and run at the serialized post-join worker boundary. CAS does not
 * establish instruction-fetch safety. These caller/lifetime requirements must
 * be reviewed independently before enabling this experiment.
 *
 * The exact initializer release consumes a still-owned native reference. Only
 * that scoped callback may acquire P1. No arbitrary raw-pointer acquisition,
 * exception recovery, counter rewrites, seating or P2 simulation is provided.
 * Partial native calls leave the core ledger pending. Discard the isolated
 * process on uncertainty; never guess cleanup. Thunk and metadata stay resident
 * even after restoration because a delayed call may already have loaded them.
 * Ownership clean does not undo IDs, RNG, contexts, rendering or deferred
 * deletion. P2 is never read after its owned result reference is released.
 */
#include "player_factory_probe.h"
#include "player_reference_probe.h"
#include "player_factory_preload_pin.h"

#define FACTORY_PROBE_WORD_VA 0x1daf740u
#define FACTORY_PROBE_CALL_VA 0x1daf741u
#define FACTORY_PROBE_RETURN_VA 0x1daf746u
#define FACTORY_PROBE_ORIGINAL_WORD UINT64_C(0x8b48fec64dfae838)
#define FACTORY_PROBE_UNIVERSE_VA 0x3db6948u
#define FACTORY_PROBE_EMPTY_VA 0x3daa6a0u
#define FACTORY_PROBE_FACTORY_VA 0x119bd70u
#define FACTORY_PROBE_ASSIGN_VA 0xc5df40u

static player_factory_probe factory_probe_state;
static atomic_uint factory_probe_requested;
static atomic_uint factory_probe_scope;
static atomic_uint factory_probe_wrapper_depth;
static atomic_uint factory_probe_wrapper_failure;
static atomic_uint factory_probe_wrapper_calls;
static unsigned factory_probe_patch_owned;
static unsigned factory_probe_code_permission_pending;
static unsigned factory_probe_thunk_permission_pending;
static unsigned factory_probe_inflight;
static uint64_t factory_probe_patched_word;
static uint64_t factory_probe_run_tick;
static uint64_t factory_probe_release_tick;
static uint64_t factory_probe_retained_checks;
static uint64_t factory_probe_released_tick;
static unsigned char *factory_probe_page;
static uintptr_t factory_probe_universe;
static uintptr_t factory_probe_player1;
static uintptr_t factory_probe_macro;
static uint64_t factory_probe_player1_runtime;
static uint32_t factory_probe_count_before;
static uint32_t factory_probe_capture_count;
static uint32_t factory_probe_player2_count;
static uint32_t factory_probe_player2_before_release;
static uint32_t factory_probe_player2_marker;
static uint64_t factory_probe_player2_runtime;
static int factory_probe_restore(void);
static uint32_t factory_probe_restored_count;
static uint32_t factory_probe_released_count;
static uint32_t factory_probe_observed_player1_count;
static uint32_t factory_probe_observed_player2_count;
static unsigned factory_probe_observed_player2_parent;
static stock_keyboard_identity_sample factory_probe_identity;
static const char *factory_probe_problem = "off";
/* Accessed only inside the admitted worker's outermost scoped wrapper.
 * Unexpected/nested wrappers keep errno on their own stack and never touch
 * these fields. Avoid dynamic ELF TLS in the late-loaded gate module. */
static int factory_probe_forward_errno;
static int factory_probe_return_errno;

static int factory_probe_fail(const char *reason) {
    if (!strcmp(factory_probe_problem, "off") || !strcmp(factory_probe_problem, "requested") ||
        !strcmp(factory_probe_problem, "installed") || !strcmp(factory_probe_problem, "running") ||
        !strcmp(factory_probe_problem, "finished") || !strcmp(factory_probe_problem, "retained")) {
        factory_probe_problem = reason;
    }
    return 0;
}

static int factory_probe_helpers(void) {
    /* The reviewed portable-math preload rewrites one instruction window in
     * this function. Verify its exact trampoline before hashing original bytes. */
    if (!factory_probe_factory_pin()) {
        return factory_probe_fail("Player-factory");
    }
    static const lx_site pins[] = {
        {"Player-constructor", 0x1385ff0, 2650,
         "3871c4e88bd186966c4436902b2bc22108090c90e99b68c712399316d67ab90f"},
        {"reference-assign", FACTORY_PROBE_ASSIGN_VA, 92,
         "52d3d628add9f626e4145466f4c097ad404dcc65c39a24c4caf64fc4a2ec3f89"},
        {"reference-acquire", P1_REFERENCE_ACQUIRE_VA, 21,
         "0e7c126afb520730e79452b02c5cedd696ecdc918f11b71aab1501dead392f84"},
        {"reference-release", P1_REFERENCE_RELEASE_VA, 37,
         "7368cef95d1efcc8cda06dfd53df92ca026f1da024481b82f3c7a86b8db75587"},
        {"Player-zero-adjustor", P1_REFERENCE_ZERO_TARGET_VA, 6,
         "6b79cd8bbfcde38ac5dc19c8484ad016c7c5dd0d77087043ca7767f2dcd2305c"},
        {"Player-zero-enqueue", 0x18a7d80, 112,
         "7b2e9ec6193bcbb295089fda312dd6921b47aae5323761c668d9569c509dff4f"},
    };
    for (unsigned index = 0; index < sizeof(pins) / sizeof(pins[0]); ++index) {
        if (!site_ok(&pins[index])) {
            return factory_probe_fail(pins[index].name);
        }
    }
    uintptr_t target = 0;
    if (!read_local(LX(P1_REFERENCE_SUBOBJECT_VTABLE_VA), &target, sizeof(target)) ||
        target != (uintptr_t)LX(P1_REFERENCE_ZERO_TARGET_VA)) {
        return factory_probe_fail("reference_zero_target");
    }
    return 1;
}

static int factory_probe_word(uint64_t expected) {
    uint64_t current = 0;
    return read_local(LX(FACTORY_PROBE_WORD_VA), &current, sizeof(current)) && current == expected;
}

static int factory_probe_main_boundary(void) {
    unsigned native_flags = 2;
    unsigned char in_flight = 1;
    unsigned char loading = 1;
    return stock_observer_main_boundary && GetCurrentThreadId() == main_thread &&
        boot_phase == 3 && stock_observer_worker_thread && ui_thread == stock_observer_worker_thread &&
        ui_thread != main_thread && gate_tls_worker(__atomic_load_n(&ui_tls, __ATOMIC_SEQ_CST)) &&
        stats.completed && stats.issued == stats.completed + 1 && ui_tls_tick == stats.completed &&
        !atomic_load(&stock_observer_input_depth) && !atomic_load(&sko_session.wrapper_depth) &&
        !atomic_load(&factory_probe_wrapper_depth) && !atomic_load(&factory_probe_scope) &&
        read_local(LX(STOCK_OBSERVER_NATIVE_FLAGS), &native_flags, sizeof(native_flags)) &&
        !(native_flags & 2) && read_local(LX(LX_IN_FLIGHT_VA), &in_flight, 1) && !in_flight &&
        read_local(LX(0x3daab00u), &loading, 1) && !loading;
}

static int factory_probe_joined_boundary(void) {
    unsigned native_flags = 2;
    unsigned char loading = 1;
    sko_status observer = sko_get_status();
    return stats.active && boot_phase == 3 && stats.issued >= 2 && path_installed &&
        stock_observer_post_join_boundary && GetCurrentThreadId() == stock_observer_worker_thread &&
        GetCurrentThreadId() != main_thread && ui_thread == stock_observer_worker_thread &&
        gate_tls_worker(gate_tls_self()) && !atomic_load(&stock_observer_input_depth) &&
        !atomic_load(&sko_session.wrapper_depth) && !atomic_load(&stock_observer_thread_failure) &&
        !stock_observer_failed && observer.active && observer.owned == (1u << SKO_SELECTED) - 1 &&
        !observer.uncertain && !observer.pending_readback && !observer.overflow &&
        stock_observer_slot_valid() &&
        !atomic_load(&factory_probe_wrapper_failure) &&
        read_local(LX(STOCK_OBSERVER_NATIVE_FLAGS), &native_flags, sizeof(native_flags)) &&
        !(native_flags & 2) && read_local(LX(0x3daab00u), &loading, 1) && !loading;
}

static int factory_probe_worker_boundary(void) {
    return stats.issued == 2 && factory_probe_joined_boundary() &&
        factory_probe_patch_owned && !factory_probe_code_permission_pending &&
        !factory_probe_thunk_permission_pending && factory_probe_word(factory_probe_patched_word);
}

static int factory_probe_object(uintptr_t player, uintptr_t macro, uint64_t *runtime,
                                 uint32_t *count, uint32_t *marker) {
    uintptr_t vtable = 0;
    uintptr_t reference_vtable = 0;
    uintptr_t actual_macro = 0;
    return player && player <= UINTPTR_MAX - 0x78 &&
        read_local((void *)player, &vtable, 8) && vtable == (uintptr_t)LX(P1_REFERENCE_PLAYER_VTABLE_VA) &&
        read_local((void *)(player + 0x38), &reference_vtable, 8) &&
        reference_vtable == (uintptr_t)LX(P1_REFERENCE_SUBOBJECT_VTABLE_VA) &&
        read_local((void *)(player + 0x60), &actual_macro, 8) && actual_macro == macro &&
        read_local((void *)(player + 8), runtime, 8) && *runtime &&
        read_local((void *)(player + 0x40), count, 4) && *count > 0 && *count < INT32_MAX &&
        read_local((void *)(player + 0x44), marker, 4) && *marker == 2;
}

static int factory_probe_singleton(uintptr_t expected) {
    uintptr_t universe = 0;
    uintptr_t player = 0;
    return read_local(LX(FACTORY_PROBE_UNIVERSE_VA), &universe, 8) &&
        universe == factory_probe_universe && universe <= UINTPTR_MAX - 0x240 &&
        read_local((void *)(universe + 0x238), &player, 8) && player == expected;
}

static int factory_probe_validate(void *context, player_factory_validation phase,
                                   uintptr_t universe, uintptr_t player1, uintptr_t macro,
                                   uintptr_t player2) {
    (void)context;
    if (!factory_probe_worker_boundary() || !factory_probe_helpers() ||
        universe != factory_probe_universe || player1 != factory_probe_player1 ||
        macro != factory_probe_macro) {
        return factory_probe_fail("validation_boundary");
    }
    uint64_t runtime = 0;
    uint32_t count = 0;
    uint32_t marker = 0;
    /* At CONSTRUCTED/RESTORED the captured owned P1 reference keeps it live. */
    if (!factory_probe_object(player1, macro, &runtime, &count, &marker) ||
        runtime != factory_probe_player1_runtime) {
        return factory_probe_fail("validation_player1");
    }
    factory_probe_observed_player1_count = count;
    if (phase == PLAYER_FACTORY_BEFORE) {
        stock_keyboard_identity_sample current;
        memset(&current, 0, sizeof(current));
        int valid = factory_probe_singleton(player1) && count == factory_probe_count_before &&
            stock_keyboard_identity_collect(&current) && current.ready &&
            !memcmp(&current, &factory_probe_identity, sizeof(current));
        return valid || factory_probe_fail("validation_initial_identity");
    }
    if (!player2 || player2 == player1 || !factory_probe_state.player1_owned ||
        !factory_probe_singleton(phase == PLAYER_FACTORY_CONSTRUCTED ? player2 : player1)) {
        return factory_probe_fail("validation_singleton");
    }
    uint64_t runtime2 = 0;
    uint32_t count2 = 0;
    uintptr_t parent = 1;
    if (!factory_probe_object(player2, macro, &runtime2, &count2, &marker) ||
        runtime2 == runtime || !read_local((void *)(player2 + 0x70), &parent, 8)) {
        return factory_probe_fail("validation_player2");
    }
    factory_probe_observed_player2_count = count2;
    factory_probe_observed_player2_parent = parent != 0;
    if (parent) {
        return factory_probe_fail("validation_player2_parent");
    }
    if (phase == PLAYER_FACTORY_CONSTRUCTED) {
        if (count != factory_probe_count_before || count2 < 2) {
            return factory_probe_fail("validation_constructed_counts");
        }
        factory_probe_player2_count = count2;
        factory_probe_player2_runtime = runtime2;
        return 1;
    }
    if (count != factory_probe_count_before + 1 || runtime2 != factory_probe_player2_runtime ||
        count2 != factory_probe_player2_count - 1) {
        return factory_probe_fail("validation_restored_counts");
    }
    factory_probe_restored_count = count;
    stock_keyboard_identity_sample current;
    memset(&current, 0, sizeof(current));
    int valid = stock_keyboard_identity_collect(&current) && current.ready &&
        !memcmp(&current, &factory_probe_identity, sizeof(current));
    return valid || factory_probe_fail("validation_restored_identity");
}

/* Called only while the intercepted native argument still owns old P1. */
static int factory_probe_acquire_player1(void *context, uintptr_t player1) {
    (void)context;
    uint64_t runtime = 0;
    uint32_t count = 0;
    uint32_t marker = 0;
    if (!atomic_load(&factory_probe_scope) || atomic_load(&factory_probe_wrapper_depth) != 1 ||
        !factory_probe_worker_boundary() || !factory_probe_helpers() ||
        player1 != factory_probe_player1 ||
        !factory_probe_object(player1, factory_probe_macro, &runtime, &count, &marker) ||
        runtime != factory_probe_player1_runtime || count != factory_probe_count_before) {
        return 0;
    }
    ((void (*)(void *))LX(P1_REFERENCE_ACQUIRE_VA))((void *)player1);
    uint32_t after = 0;
    if (!read_local((void *)(player1 + 0x40), &after, 4) || after != count + 1) {
        return 0;
    }
    factory_probe_capture_count = after;
    return 1;
}

static int factory_probe_forward_release(void *context, uintptr_t native_reference) {
    (void)context;
    errno = factory_probe_forward_errno;
    ((void (*)(void *))LX(P1_REFERENCE_RELEASE_VA))((void *)native_reference);
    factory_probe_return_errno = errno;
    return 1;
}

__attribute__((noinline)) static void factory_probe_release_wrapper(void *native_reference) {
    int entry_errno = errno;
    unsigned depth = atomic_fetch_add(&factory_probe_wrapper_depth, 1) + 1;
    atomic_fetch_add(&factory_probe_wrapper_calls, 1);
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    int scoped = depth == 1 && atomic_load(&factory_probe_scope) &&
        GetCurrentThreadId() == stock_observer_worker_thread &&
        caller == (uintptr_t)LX(FACTORY_PROBE_RETURN_VA) &&
        (uintptr_t)native_reference >= 0x38 &&
        (uintptr_t)native_reference - 0x38 == factory_probe_player1;
    if (scoped) {
        int prior_forward_errno = factory_probe_forward_errno;
        int prior_return_errno = factory_probe_return_errno;
        factory_probe_forward_errno = entry_errno;
        factory_probe_return_errno = entry_errno;
        player_factory_intercept_release(&factory_probe_state, factory_probe_player1,
                                          (uintptr_t)native_reference);
        entry_errno = factory_probe_return_errno;
        factory_probe_forward_errno = prior_forward_errno;
        factory_probe_return_errno = prior_return_errno;
    } else {
        atomic_store(&factory_probe_wrapper_failure, 1);
        errno = entry_errno;
        ((void (*)(void *))LX(P1_REFERENCE_RELEASE_VA))(native_reference);
        entry_errno = errno;
    }
    atomic_fetch_sub(&factory_probe_wrapper_depth, 1);
    errno = entry_errno;
}

/* Mechanical SysV ABI from retained native exemplars: 17 integer/pointer
 * positions, fresh output storage first; RAX is storage address, never P2. */
typedef void **(*factory_probe_create_fn)(void **, void *, void *, void *, void *, void *,
                                         void *, void *, uint32_t, void *, uint32_t,
                                         const void *, const void *, const void *,
                                         void *, uint32_t, uint32_t);

static int factory_probe_construct(void *context, player_factory_probe *probe, uintptr_t *output) {
    (void)context;
    if (probe != &factory_probe_state || !factory_probe_worker_boundary() ||
        !factory_probe_helpers() || atomic_load(&factory_probe_wrapper_depth) ||
        !factory_probe_singleton(factory_probe_player1)) {
        return 0;
    }
    void *fresh_result = NULL;
    const void *empty = LX(FACTORY_PROBE_EMPTY_VA);
    atomic_store(&factory_probe_scope, 1);
    void **returned = ((factory_probe_create_fn)LX(FACTORY_PROBE_FACTORY_VA))(
        &fresh_result, (void *)factory_probe_universe, NULL, NULL, NULL, NULL,
        (void *)factory_probe_macro, NULL, 1, NULL, 0, empty, empty, empty, NULL, 0, 1);
    atomic_store(&factory_probe_scope, 0);
    *output = (uintptr_t)fresh_result;
    return returned == &fresh_result && !atomic_load(&factory_probe_wrapper_failure);
}

static int factory_probe_assign_player1(void *context, uintptr_t universe,
                                         uintptr_t player1, uintptr_t player2) {
    (void)context;
    if (!factory_probe_validate(NULL, PLAYER_FACTORY_CONSTRUCTED, universe, player1,
                                 factory_probe_macro, player2)) {
        return 0;
    }
    void **destination = (void **)(universe + 0x238);
    void **returned = ((void **(*)(void **, void *))LX(FACTORY_PROBE_ASSIGN_VA))(
        destination, (void *)player1);
    return returned == destination && factory_probe_singleton(player1);
}

static int factory_probe_release_player1(void *context, uintptr_t player1) {
    (void)context;
    if (!factory_probe_worker_boundary() || !factory_probe_helpers() ||
        !factory_probe_singleton(player1)) {
        return 0;
    }
    uint64_t runtime = 0;
    uint32_t count = 0;
    uint32_t marker = 0;
    if (!factory_probe_object(player1, factory_probe_macro, &runtime, &count, &marker) ||
        runtime != factory_probe_player1_runtime || count != factory_probe_count_before + 1) {
        return 0;
    }
    ((void (*)(void *))LX(P1_REFERENCE_RELEASE_VA))((void *)(player1 + 0x38));
    /* Universe owns P1 again; obtain a fresh canonical sample before rereading. */
    stock_keyboard_identity_sample current;
    memset(&current, 0, sizeof(current));
    uint32_t after = 0;
    if (!stock_keyboard_identity_collect(&current) || !current.ready || current.player != player1 ||
        memcmp(&current, &factory_probe_identity, sizeof(current)) ||
        !read_local((void *)(current.player + 0x40), &after, 4) || after != factory_probe_count_before) {
        return 0;
    }
    factory_probe_released_count = after;
    return 1;
}

static int factory_probe_release_player2(void *context, uintptr_t player2) {
    (void)context;
    uint64_t runtime = 0;
    uint32_t count = 0;
    uint32_t marker = 0;
    int boundary = factory_probe_release_tick ?
        (factory_probe_joined_boundary() && stats.issued == factory_probe_release_tick &&
         !factory_probe_patch_owned && !factory_probe_code_permission_pending &&
         !factory_probe_thunk_permission_pending && !atomic_load(&factory_probe_scope) &&
         !atomic_load(&factory_probe_wrapper_depth)) : factory_probe_worker_boundary();
    if (!boundary || !factory_probe_helpers() ||
        !factory_probe_singleton(factory_probe_player1) || !factory_probe_state.player2_owned ||
        player2 != factory_probe_state.player2 ||
        !factory_probe_object(player2, factory_probe_macro, &runtime, &count, &marker) ||
        runtime != factory_probe_player2_runtime ||
        (!factory_probe_release_tick && count != factory_probe_player2_count - 1)) {
        return 0;
    }
    factory_probe_player2_before_release = count;
    factory_probe_player2_marker = marker;
    ((void (*)(void *))LX(P1_REFERENCE_RELEASE_VA))((void *)(player2 + 0x38));
    factory_probe_released_tick = stats.issued;
    /* The owned result reference is consumed on normal return. Never inspect
     * P2 again: zero count invokes the native registry/deferred-delete path. */
    return 1;
}

/* Own patch CAS has separate byte/protection obligations; a boolean helper
 * would hide successful mutation followed by failed RX restoration. */
static int factory_probe_swap(uint64_t expected, uint64_t replacement) {
    void *address = LX(FACTORY_PROBE_WORD_VA);
    char permissions[5];
    pthread_mutex_lock(&gate_code_lock);
    if (!gate_maps_find((uintptr_t)address, permissions, NULL, 0) ||
        (strcmp(permissions, "r-xp") &&
         !(factory_probe_code_permission_pending && !strcmp(permissions, "rwxp")))) {
        pthread_mutex_unlock(&gate_code_lock);
        return 0;
    }
    if (!gate_code_open(address, 8)) {
        pthread_mutex_unlock(&gate_code_lock);
        return 0;
    }
    factory_probe_code_permission_pending = 1;
    uint64_t actual = expected;
    int changed = __atomic_compare_exchange_n((uint64_t *)address, &actual, replacement, 0,
                                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    if (changed) {
        factory_probe_patch_owned = replacement == factory_probe_patched_word;
        __builtin___clear_cache((char *)address, (char *)address + 8);
    }
    int closed = gate_code_close(address);
    if (closed) {
        factory_probe_code_permission_pending = 0;
    }
    pthread_mutex_unlock(&gate_code_lock);
    return changed && closed;
}

static int factory_probe_admit(void) {
    if (!atomic_load(&factory_probe_requested)) {
        return 1;
    }
    if (atomic_load(&factory_probe_wrapper_failure)) {
        return factory_probe_fail("unexpected_wrapper");
    }
    if (!strcmp(factory_probe_problem, "finished") || !strcmp(factory_probe_problem, "retained")) {
        return !factory_probe_patch_owned && !factory_probe_code_permission_pending &&
            !factory_probe_thunk_permission_pending;
    }
    if (strcmp(factory_probe_problem, "requested") && strcmp(factory_probe_problem, "installed")) {
        return 0;
    }
    if (factory_probe_thunk_permission_pending) {
        return factory_probe_fail("thunk_permission_pending");
    }
    if (factory_probe_patch_owned || factory_probe_code_permission_pending) {
        return factory_probe_word(factory_probe_patched_word) &&
            !factory_probe_code_permission_pending && !atomic_load(&factory_probe_wrapper_failure);
    }
    static const lx_site initializer = {"Player-initializer", 0x1daf300, 2423,
        "8666b05926e5ea778188ecfefa0c3e51566d0944b1879d262e036d15ba6eebef"};
    static const lx_site release_call = {"Player-old-reference-release", FACTORY_PROBE_CALL_VA, 5,
        "f5a4aa95f01be3d973cdf61134d138e25f429499441c700827bbb27b25d64d4b"};
    if (stats.issued != 2 || !stats.active || !factory_probe_main_boundary()) {
        return factory_probe_fail("install_boundary");
    }
    if (!stock_observer_install_tick || !atomic_load(&sko_session.active) ||
        !stock_observer_slot_valid()) {
        return factory_probe_fail("install_observer");
    }
    if (!site_ok(&initializer)) {
        return factory_probe_fail("initializer_pin");
    }
    if (!site_ok(&release_call)) {
        return factory_probe_fail("release_call_pin");
    }
    if (!factory_probe_helpers()) {
        return 0;
    }
    if (!factory_probe_word(FACTORY_PROBE_ORIGINAL_WORD)) {
        return factory_probe_fail("release_word");
    }
    if (!factory_probe_page) {
        factory_probe_page = gate_near_page(LX(FACTORY_PROBE_CALL_VA));
        if (!factory_probe_page) {
            return factory_probe_fail("thunk_allocation");
        }
        /* jmp [rip+0] retains the native CALL's return address and all inputs. */
        const unsigned char jump[] = {0xff, 0x25, 0, 0, 0, 0};
        memcpy(factory_probe_page, jump, sizeof(jump));
        uintptr_t target = (uintptr_t)&factory_probe_release_wrapper;
        memcpy(factory_probe_page + sizeof(jump), &target, sizeof(target));
        factory_probe_thunk_permission_pending = 1;
        if (mprotect(factory_probe_page, GATE_PAGE, PROT_READ | PROT_EXEC)) {
            return factory_probe_fail("thunk_rx");
        }
        factory_probe_thunk_permission_pending = 0;
        __builtin___clear_cache((char *)factory_probe_page, (char *)factory_probe_page + 14);
    }
    char thunk_permissions[5];
    if (factory_probe_thunk_permission_pending ||
        !gate_maps_find((uintptr_t)factory_probe_page, thunk_permissions, NULL, 0) ||
        strcmp(thunk_permissions, "r-xp")) {
        return factory_probe_fail("thunk_not_rx");
    }
    intptr_t displacement = (intptr_t)factory_probe_page - (intptr_t)LX(FACTORY_PROBE_RETURN_VA);
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        return factory_probe_fail("thunk_range");
    }
    int32_t relative = (int32_t)displacement;
    factory_probe_patched_word = FACTORY_PROBE_ORIGINAL_WORD;
    memcpy((unsigned char *)&factory_probe_patched_word + 2, &relative, sizeof(relative));
    if (!factory_probe_swap(FACTORY_PROBE_ORIGINAL_WORD, factory_probe_patched_word)) {
        return factory_probe_fail("install_swap");
    }
    factory_probe_problem = "installed";
    return 1;
}

/* The factory's out reference keeps P2 alive across frames. Fresh canonical
 * identity is checked separately: a held reference is not proof of seating or
 * of an unchanged Universe singleton. Never read P2 after releasing that hold. */
static int factory_probe_check_retained(void) {
    stock_keyboard_identity_sample current;
    memset(&current, 0, sizeof(current));
    uint64_t runtime = 0;
    uint32_t count = 0;
    uint32_t marker = 0;
    uintptr_t parent = 1;
    if (!factory_probe_joined_boundary() || factory_probe_patch_owned ||
        factory_probe_code_permission_pending || factory_probe_thunk_permission_pending ||
        atomic_load(&factory_probe_scope) || atomic_load(&factory_probe_wrapper_depth) ||
        !factory_probe_state.player2_owned || factory_probe_state.player2_uncertain ||
        !factory_probe_singleton(factory_probe_player1) ||
        !stock_keyboard_identity_collect(&current) || !current.ready ||
        current.universe != factory_probe_identity.universe ||
        current.player != factory_probe_identity.player ||
        current.player_runtime != factory_probe_identity.player_runtime ||
        current.controller != factory_probe_identity.controller ||
        current.ship != factory_probe_identity.ship ||
        current.runtime != factory_probe_identity.runtime ||
        current.input_owner != factory_probe_identity.input_owner ||
        !factory_probe_object(factory_probe_state.player2, factory_probe_macro, &runtime, &count, &marker) ||
        runtime != factory_probe_player2_runtime ||
        !read_local((void *)(factory_probe_state.player2 + 0x70), &parent, sizeof(parent)) || parent) {
        return factory_probe_fail("retained_identity");
    }
    factory_probe_observed_player2_count = count;
    ++factory_probe_retained_checks;
    return 1;
}

static int factory_probe_after_join(void) {
    if (!atomic_load(&factory_probe_requested)) {
        return 1;
    }
    if (factory_probe_state.attempted) {
        if (atomic_load(&factory_probe_wrapper_failure)) {
            return factory_probe_fail("unexpected_wrapper");
        }
        if (factory_probe_state.phase == PLAYER_FACTORY_RETAINED) {
            if (!factory_probe_check_retained() || stats.issued > factory_probe_release_tick) {
                return factory_probe_fail("retained_release_missed");
            }
            if (stats.issued == factory_probe_release_tick) {
                if (!player_factory_release_retained(&factory_probe_state)) {
                    return factory_probe_fail(factory_probe_state.problem);
                }
                factory_probe_problem = "finished";
            }
            return 1;
        }
        return factory_probe_state.phase == PLAYER_FACTORY_FINISHED;
    }
    if (stats.issued < 2) {
        return 1;
    }
    if (factory_probe_inflight || !factory_probe_worker_boundary() || !factory_probe_helpers()) {
        return factory_probe_fail("worker_boundary");
    }
    memset(&factory_probe_identity, 0, sizeof(factory_probe_identity));
    uint32_t macro_class = 0;
    uint32_t marker = 0;
    uint64_t runtime = 0;
    if (!stock_keyboard_identity_collect(&factory_probe_identity) || !factory_probe_identity.ready) {
        return factory_probe_fail("initial_identity");
    }
    factory_probe_player1 = factory_probe_identity.player;
    factory_probe_universe = factory_probe_identity.universe;
    if (!factory_probe_player1 || factory_probe_player1 > UINTPTR_MAX - 0x78 ||
        !read_local((void *)(factory_probe_player1 + 0x60), &factory_probe_macro, 8) ||
        !factory_probe_macro || factory_probe_macro > UINTPTR_MAX - 0x48 ||
        !read_local((void *)(factory_probe_macro + 0x44), &macro_class, 4) || macro_class != 0x4c ||
        !factory_probe_object(factory_probe_player1, factory_probe_macro, &runtime,
                               &factory_probe_count_before, &marker) || factory_probe_count_before < 2 ||
        runtime != factory_probe_identity.player_runtime) {
        return factory_probe_fail("initial_player");
    }
    factory_probe_player1_runtime = runtime;
    factory_probe_run_tick = stats.issued;
    player_factory_operations operations = {
        NULL, factory_probe_validate, factory_probe_construct, factory_probe_acquire_player1,
        factory_probe_forward_release, factory_probe_assign_player1, factory_probe_release_player1,
        factory_probe_release_player2
    };
    factory_probe_problem = "running";
    factory_probe_inflight = 1;
    int result = factory_probe_release_tick ?
        player_factory_run_retained(&factory_probe_state, &operations, factory_probe_universe,
                                    factory_probe_player1, factory_probe_macro) :
        player_factory_run(&factory_probe_state, &operations, factory_probe_universe,
                           factory_probe_player1, factory_probe_macro);
    factory_probe_inflight = 0;
    if (!result || atomic_load(&factory_probe_wrapper_failure)) {
        return factory_probe_fail(factory_probe_state.problem ? factory_probe_state.problem : "wrapper_failure");
    }
    /* No initializer remains on this worker's stack. Remove the owned call
     * patch at this same reviewed joined-worker boundary, retaining its thunk. */
    if (!factory_probe_restore()) {
        return factory_probe_fail("post_transaction_unpatch");
    }
    factory_probe_problem = factory_probe_release_tick ? "retained" : "finished";
    return 1;
}

/* This scope is the existing cleanup boundary, not a general claim about all
 * engine threads. Main admission or a joined worker may retry byte/RX cleanup
 * after normal return; never while our constructor or wrapper is in flight. */
static int factory_probe_cleanup_boundary(void) {
    if (factory_probe_inflight || atomic_load(&factory_probe_scope) ||
        atomic_load(&factory_probe_wrapper_depth) || atomic_load(&stock_observer_input_depth) ||
        atomic_load(&sko_session.wrapper_depth)) {
        return 0;
    }
    if (factory_probe_main_boundary()) {
        return 1;
    }
    unsigned flags = 2;
    unsigned char loading = 1;
    return stock_observer_post_join_boundary && boot_phase == 3 &&
        stock_observer_worker_thread && GetCurrentThreadId() == stock_observer_worker_thread &&
        GetCurrentThreadId() != main_thread && ui_thread == stock_observer_worker_thread &&
        gate_tls_worker(gate_tls_self()) &&
        read_local(LX(STOCK_OBSERVER_NATIVE_FLAGS), &flags, sizeof(flags)) && !(flags & 2) &&
        read_local(LX(0x3daab00u), &loading, 1) && !loading;
}

static int factory_probe_restore(void) {
    if (!atomic_load(&factory_probe_requested)) {
        return 1;
    }
    if (!factory_probe_patch_owned && !factory_probe_code_permission_pending &&
        !factory_probe_thunk_permission_pending) {
        return 1;
    }
    if (!factory_probe_cleanup_boundary()) {
        return factory_probe_fail("restore_boundary");
    }
    if (factory_probe_thunk_permission_pending) {
        if (mprotect(factory_probe_page, GATE_PAGE, PROT_READ | PROT_EXEC)) {
            return factory_probe_fail("restore_thunk_rx");
        }
        factory_probe_thunk_permission_pending = 0;
    }
    if (factory_probe_patch_owned &&
        !factory_probe_swap(factory_probe_patched_word, FACTORY_PROBE_ORIGINAL_WORD)) {
        return factory_probe_fail("restore_swap");
    }
    if (factory_probe_code_permission_pending) {
        if (!factory_probe_word(FACTORY_PROBE_ORIGINAL_WORD) ||
            !gate_code_close(LX(FACTORY_PROBE_WORD_VA))) {
            return factory_probe_fail("restore_code_rx");
        }
        factory_probe_code_permission_pending = 0;
    }
    return !factory_probe_patch_owned && !factory_probe_code_permission_pending &&
        !factory_probe_thunk_permission_pending && factory_probe_word(FACTORY_PROBE_ORIGINAL_WORD);
}

static int factory_probe_clean(void) {
    return !factory_probe_patch_owned && !factory_probe_code_permission_pending &&
        !factory_probe_thunk_permission_pending && !factory_probe_inflight &&
        !atomic_load(&factory_probe_scope) && !atomic_load(&factory_probe_wrapper_depth) &&
        !atomic_load(&factory_probe_wrapper_failure) &&
        (!factory_probe_state.attempted || factory_probe_state.ownership_clean);
}

static int lua_factory_probe_request(lua_State *L) {
    double release_tick = p_lua_tonumber(L, 1);
    int valid = accept_ui() && stats.active && stats.issued == 1 && boot_phase == 3 &&
        isfinite(release_tick) && (release_tick == 0 ||
            (release_tick >= 4 && release_tick <= 36000 && release_tick == floor(release_tick))) &&
        path_installed && stock_observer_wanted() && atomic_load(&stock_observer_requested) &&
        GetCurrentThreadId() == stock_observer_worker_thread && ui_thread == stock_observer_worker_thread &&
        GetCurrentThreadId() != main_thread && gate_tls_worker(gate_tls_self()) &&
        !atomic_load(&stock_observer_input_depth) && !atomic_load(&sko_session.wrapper_depth) &&
        !atomic_load(&factory_probe_requested) && !atomic_load(&factory_probe_wrapper_failure);
    if (valid) {
        factory_probe_release_tick = (uint64_t)release_tick;
        atomic_store(&factory_probe_requested, 1);
        factory_probe_problem = "requested";
    }
    p_lua_pushnumber(L, valid);
    return 1;
}

static int lua_factory_probe_status(lua_State *L) {
    if (!accept_ui()) {
        p_lua_pushnil(L);
        return 1;
    }
    char output[1100];
    int length = snprintf(output, sizeof(output),
        "requested=%u tick=%llu phase=%u attempted=%u patch_owned=%u code_permission_pending=%u "
        "thunk_permission_pending=%u scope=%u wrapper_depth=%u wrapper_calls=%u wrapper_failure=%u "
        "intercepts=%u forwards_pending=%u p1_owned=%u p1_uncertain=%u constructor_pending=%u "
        "p2_owned=%u p2_uncertain=%u assignment_pending=%u singleton_uncertain=%u "
        "release1_pending=%u release2_pending=%u ownership_clean=%u clean=%u problem=%s "
        "p1_count_before=%u p1_capture_count=%u p2_count=%u p1_restored_count=%u p1_released_count=%u "
        "p2_runtime=%llu p2_before_release=%u p2_marker=%u observed_p1_count=%u "
        "observed_p2_count=%u observed_p2_parent=%u release_tick=%llu retained_checks=%llu released_tick=%llu",
        atomic_load(&factory_probe_requested), (unsigned long long)factory_probe_run_tick,
        (unsigned)factory_probe_state.phase, factory_probe_state.attempted, factory_probe_patch_owned,
        factory_probe_code_permission_pending, factory_probe_thunk_permission_pending,
        atomic_load(&factory_probe_scope), atomic_load(&factory_probe_wrapper_depth),
        atomic_load(&factory_probe_wrapper_calls), atomic_load(&factory_probe_wrapper_failure),
        factory_probe_state.interceptor_calls, factory_probe_state.forward_pending,
        factory_probe_state.player1_owned, factory_probe_state.player1_uncertain,
        factory_probe_state.constructor_pending, factory_probe_state.player2_owned,
        factory_probe_state.player2_uncertain, factory_probe_state.assignment_pending,
        factory_probe_state.singleton_uncertain, factory_probe_state.release1_pending,
        factory_probe_state.release2_pending, factory_probe_state.ownership_clean, factory_probe_clean(),
        factory_probe_problem, factory_probe_count_before, factory_probe_capture_count,
        factory_probe_player2_count, factory_probe_restored_count, factory_probe_released_count,
        (unsigned long long)factory_probe_player2_runtime, factory_probe_player2_before_release,
        factory_probe_player2_marker, factory_probe_observed_player1_count,
        factory_probe_observed_player2_count, factory_probe_observed_player2_parent,
        (unsigned long long)factory_probe_release_tick, (unsigned long long)factory_probe_retained_checks,
        (unsigned long long)factory_probe_released_tick);
    if (length < 0 || (size_t)length >= sizeof(output)) {
        p_lua_pushnil(L);
    } else {
        p_lua_pushlstring(L, output, (size_t)length);
    }
    return 1;
}
#endif

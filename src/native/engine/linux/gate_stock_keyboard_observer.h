/* Default-off native dispatch diagnostic. All original input effects remain.
 *
 * The main thread installs only after a completed, admitted MoveWorker frame.
 * InputBase::Move then brackets the whole native input pass. This provides a
 * concrete boundary for later Lua inspection/restoration on that same worker;
 * a zero selected-handler count alone would not prove that boundary.
 * No input is suppressed, replayed, or sent by this module.
 */
#include "stock_keyboard_observer.h"

#define STOCK_OBSERVER_INPUT_SLOT 0x3aad788u
#define STOCK_OBSERVER_INPUT_UPDATE 0xcec2f0u
#define STOCK_OBSERVER_NATIVE_FLAGS 0x3daab04u

static atomic_uint stock_observer_requested;
static DWORD stock_observer_worker_thread;
static atomic_uint stock_observer_input_depth;
static atomic_uint stock_observer_thread_failure;
static uint64_t stock_observer_input_tick;
static uint64_t stock_observer_install_tick;
static unsigned stock_observer_slot_owned;
static unsigned stock_observer_permission_pending;
static unsigned stock_observer_failed;
static unsigned stock_observer_cleaned;
static const char *stock_observer_problem = "off";
static const char *stock_observer_failure_reason = "none";
static char stock_observer_failure_detail[180];
static int stock_observer_pins(void *context);

/* Explain the first rejected census without changing its acceptance rule.
 * This runs only at the same quiescent boundary as validation. Cached node
 * addresses remain comparison values; reads use the fresh census exclusively.
 */
static const char *stock_observer_dispatch_difference(void) {
    if (!stock_observer_pins(NULL)) {
        return "dispatch_function_pin";
    }
    uintptr_t owner = 0;
    unsigned count = 0;
    if (!sko_census(sko_scratch, &count, &owner)) {
        return "dispatch_unreadable_census";
    }
    if (owner != sko_session.owner) {
        return "dispatch_owner_changed";
    }
    unsigned selected_seen = 0;
    unsigned selected_stable = 1;
    for (unsigned node_index = 0; node_index < count; ++node_index) {
        const sko_node *node = &sko_scratch[node_index];
        int selected_index = sko_index(node->kind, node->key);
        if (selected_index < 0) {
            continue;
        }
        unsigned bit = 1u << selected_index;
        uintptr_t expected = (sko_session.owned & bit) ? sko_wrapper(node->kind)
            : (node->kind == 0 ? sko_session.action_original : sko_session.state_original);
        if ((selected_seen & bit) || node->address != sko_session.selected[selected_index] ||
            node->handler != expected) {
            selected_stable = 0;
        }
        selected_seen |= bit;
    }
    selected_stable &= selected_seen == (1u << SKO_SELECTED) - 1;
    if (count != sko_session.count) {
        snprintf(stock_observer_failure_detail, sizeof(stock_observer_failure_detail),
                 "dispatch_count_%u_to_%u_selected_stable_%u", sko_session.count, count, selected_stable);
        return stock_observer_failure_detail;
    }
    for (unsigned node_index = 0; node_index < count; ++node_index) {
        const sko_node *before = &sko_session.baseline[node_index];
        const sko_node *now = &sko_scratch[node_index];
        int selected_index = sko_index(now->kind, now->key);
        uintptr_t expected = selected_index >= 0 && (sko_session.owned & (1u << selected_index))
            ? sko_wrapper(now->kind) : before->handler;
        if (before->address != now->address || before->parent != now->parent ||
            before->left != now->left || before->right != now->right ||
            before->kind != now->kind || before->key != now->key || now->handler != expected) {
            snprintf(stock_observer_failure_detail, sizeof(stock_observer_failure_detail),
                     "dispatch_node_%u_kind_%u_key_%d_selected_stable_%u_handler_%llx_to_%llx",
                     node_index, now->kind, now->key, selected_stable,
                     (unsigned long long)expected, (unsigned long long)now->handler);
            return stock_observer_failure_detail;
        }
    }
    return "dispatch_validation_changed";
}

static int stock_observer_fail(const char *reason) {
    stock_observer_problem = reason;
    if (!stock_observer_failed) {
        stock_observer_failure_reason = reason;
    }
    stock_observer_failed = 1;
    return 0;
}

static int stock_observer_wanted(void) {
    return atomic_load(&stock_observer_requested) != 0;
}

static int stock_observer_read(void *context, uintptr_t address, void *out, size_t length) {
    (void)context;
    return read_local((const void *)address, out, length);
}

/* The freshly rediscovered node is in a writable heap mapping. The caller's
 * quiescent boundary excludes its allocator/mapping owner while CAS executes.
 */
static sko_cas_result stock_observer_swap_node(void *context, uintptr_t address, uintptr_t expected,
                                               uintptr_t replacement) {
    (void)context;
    char permissions[5];
    if ((address & 7) || !gate_maps_find(address, permissions, NULL, 0) || permissions[0] != 'r' ||
        permissions[1] != 'w') {
        return SKO_CAS_REFUSED;
    }
    return __atomic_compare_exchange_n((uintptr_t *)address, &expected, replacement, 0, __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST)
               ? SKO_CAS_APPLIED
               : SKO_CAS_CHANGED;
}

static int stock_observer_pins(void *context) {
    (void)context;
    static const lx_site pins[] = {{"keyboard_action", 0x166c2f0, 1024,
                                    "6a18f87163e93138826c076d6d06b6db51f5aac46595e714a9c92459ac8bfa02"},
                                   {"keyboard_state", 0x166c6f0, 2349,
                                    "369d1664b722dec42bee51f949b3c0c32c16bf1353e00412e271ffbedb6dd335"},
                                   {"keyboard_state_2_through_9_table", 0x2d6ddc4, 32,
                                    "a250c5a47252b8276313d6872f9bc49aa8055b4370038aa3c000ab197c8e7747"},
                                   {"keyboard_dispatch", 0x1050fa0, 284,
                                    "86f89b2beec85f1893a4ad140d4d36bae995ea1fa107ef9136b5d66c50bc41d3"},
                                   {"keyboard_input_update", 0xcec2f0, 7470,
                                    "c8bad5e7b80e24d1f60f5827e1857a7a9dc33aaf99e15f10710aadeeae3c2e9f"},
                                   {"keyboard_registration", 0x1d95a30, 4874,
                                    "f4d72eb3b84bc48a64f9bc683296683b065c70a801dd2669442f0267ebecb093"}};
    for (unsigned index = 0; index < sizeof(pins) / sizeof(pins[0]); ++index) {
        if (!site_ok(&pins[index])) {
            return 0;
        }
    }
    return 1;
}

static uint64_t stock_observer_thread(void *context) {
    (void)context;
    return GetCurrentThreadId();
}

static void stock_observer_call_action(void *context, uintptr_t original, int id) {
    (void)context;
    ((void (*)(int))original)(id);
}

static void stock_observer_call_state(void *context, uintptr_t original, int id, unsigned char state) {
    (void)context;
    ((void (*)(int, unsigned char))original)(id, state);
}

static void stock_observer_input_update(void *self) {
    int saved_errno = errno;
    atomic_fetch_add(&stock_observer_input_depth, 1);
    uintptr_t owner = 0;
    if (GetCurrentThreadId() != stock_observer_worker_thread ||
        ui_thread != stock_observer_worker_thread || !read_local(LX(SKO_OWNER_VA), &owner, 8) ||
        owner != (uintptr_t)self) {
        atomic_store(&stock_observer_thread_failure, 1);
        atomic_store(&sko_session.active, 0);
    }
    errno = saved_errno;
    ((void (*)(void *))LX(STOCK_OBSERVER_INPUT_UPDATE))(self);
    saved_errno = errno;
    __atomic_store_n(&stock_observer_input_tick, stats.issued, __ATOMIC_RELEASE);
    atomic_fetch_sub(&stock_observer_input_depth, 1);
    errno = saved_errno;
}

/* The vtable page is originally read-only. Track permission restoration apart
 * from pointer ownership: mprotect failure after a successful CAS is not an
 * untouched page and must remain a cleanup obligation.
 */
static int stock_observer_swap_slot(uintptr_t expected, uintptr_t replacement) {
    void *slot = LX(STOCK_OBSERVER_INPUT_SLOT);
    void *page = (void *)((uintptr_t)slot & ~(uintptr_t)(GATE_PAGE - 1));
    char permissions[5];
    if (!gate_maps_find((uintptr_t)slot, permissions, NULL, 0) ||
        strcmp(permissions, stock_observer_permission_pending ? "rw-p" : "r--p")) {
        return 0;
    }
    if (mprotect(page, GATE_PAGE, PROT_READ | PROT_WRITE)) {
        return 0;
    }
    stock_observer_permission_pending = 1;
    int exchanged = __atomic_compare_exchange_n((uintptr_t *)slot, &expected, replacement, 0,
                                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    if (exchanged) {
        stock_observer_slot_owned = replacement == (uintptr_t)&stock_observer_input_update;
    }
    if (!mprotect(page, GATE_PAGE, PROT_READ)) {
        stock_observer_permission_pending = 0;
    }
    return exchanged && !stock_observer_permission_pending;
}

static int stock_observer_worker_boundary(void) {
    return stock_observer_worker_thread && GetCurrentThreadId() == stock_observer_worker_thread &&
           ui_thread == stock_observer_worker_thread && gate_tls_worker(gate_tls_self()) &&
           __atomic_load_n(&stock_observer_input_tick, __ATOMIC_ACQUIRE) == stats.issued &&
           !atomic_load(&stock_observer_input_depth) && !atomic_load(&sko_session.wrapper_depth);
}

static int stock_observer_slot_valid(void) {
    uintptr_t current = 0;
    char permissions[5];
    return stock_observer_slot_owned && !stock_observer_permission_pending &&
           read_local(LX(STOCK_OBSERVER_INPUT_SLOT), &current, sizeof(current)) &&
           current == (uintptr_t)&stock_observer_input_update &&
           gate_maps_find((uintptr_t)LX(STOCK_OBSERVER_INPUT_SLOT), permissions, NULL, 0) &&
           !strcmp(permissions, "r--p");
}

/* Called from finish before the main gate's admission hook is removed. Main
 * callers must be at admission; workers need either the completed input pass
 * or the scoped, verified path-join boundary. Other paths retain wrappers and
 * report restored=0. With the replay experiment those wrappers keep consuming
 * local input; an unresolved stop requires closing the isolated test process.
 */
static void stock_observer_restore(void) {
    if (!atomic_load(&stock_observer_requested)) {
        return;
    }
    atomic_store(&sko_session.active, 0);
    int joined_worker = stock_observer_post_join_boundary && stock_observer_worker_thread &&
        GetCurrentThreadId() == stock_observer_worker_thread && ui_thread == stock_observer_worker_thread &&
        gate_tls_worker(gate_tls_self()) && !atomic_load(&stock_observer_input_depth) &&
        !atomic_load(&sko_session.wrapper_depth);
    int quiescent = (GetCurrentThreadId() == main_thread && stock_observer_main_boundary) ||
                    stock_observer_worker_boundary() || joined_worker;
    if (!quiescent) {
        stock_observer_problem = "cleanup_boundary";
        return;
    }
    int nodes_restored = !sko_initialized || (sko_restore(1) && sko_restored());
    if (!nodes_restored) {
        stock_observer_problem = "cleanup_nodes";
        return;
    }
    if (stock_observer_slot_owned &&
        !stock_observer_swap_slot((uintptr_t)&stock_observer_input_update,
                                  (uintptr_t)LX(STOCK_OBSERVER_INPUT_UPDATE))) {
        stock_observer_problem = "cleanup_input_slot";
        return;
    }
    if (stock_observer_permission_pending) {
        void *page = (void *)((uintptr_t)LX(STOCK_OBSERVER_INPUT_SLOT) & ~(uintptr_t)(GATE_PAGE - 1));
        if (mprotect(page, GATE_PAGE, PROT_READ)) {
            return;
        }
        stock_observer_permission_pending = 0;
    }
    stock_observer_cleaned = 1;
    stock_observer_problem = "restored";
}

static int stock_observer_has_cleanup(void) {
    return atomic_load(&stock_observer_requested) && !stock_observer_cleaned &&
           (stock_observer_slot_owned || stock_observer_permission_pending || sko_session.owned ||
            sko_session.uncertain || sko_session.pending_readback);
}

/* Main-thread call after a successful permit and before FrameTick. */
static int stock_observer_admit(void) {
    if (!atomic_load(&stock_observer_requested)) {
        return 1;
    }
    unsigned native_flags = 0;
    unsigned char in_flight = 1;
    unsigned char loading = 1;
    if (!stock_observer_main_boundary || boot_phase != 3 || !stock_observer_worker_thread ||
        ui_thread != stock_observer_worker_thread || ui_thread == main_thread ||
        !gate_tls_worker(__atomic_load_n(&ui_tls, __ATOMIC_SEQ_CST)) || !stats.active ||
        GetCurrentThreadId() != main_thread || !stats.completed || stats.issued != stats.completed + 1 ||
        ui_tls_tick != stats.completed ||
        !read_local(LX(STOCK_OBSERVER_NATIVE_FLAGS), &native_flags, sizeof(native_flags)) ||
        (native_flags & 2) || !read_local(LX(LX_IN_FLIGHT_VA), &in_flight, 1) || in_flight ||
        !read_local(LX(0x3daab00u), &loading, 1) || loading ||
        atomic_load(&stock_observer_input_depth) || atomic_load(&sko_session.wrapper_depth)) {
        return stock_observer_fail("admission_boundary");
    }
    if (stock_observer_install_tick) {
        if (!stock_observer_slot_valid() || atomic_load(&stock_observer_thread_failure) ||
            atomic_load(&sko_session.overflow)) {
            return stock_observer_fail("changed_dispatch");
        }
        if (!sko_validate(1)) {
            return stock_observer_fail(stock_observer_dispatch_difference());
        }
        return 1;
    }
    const sko_platform platform = {NULL,
                                   stock_observer_read,
                                   stock_observer_swap_node,
                                   stock_observer_pins,
                                   stock_observer_thread,
                                   stock_observer_call_action,
                                   stock_observer_call_state};
    if (!sko_begin(&platform, (uintptr_t)LX(SKO_OWNER_VA), (uintptr_t)LX(SKO_ACTION_VA),
                   (uintptr_t)LX(SKO_STATE_VA), 1) ||
        !stock_observer_swap_slot((uintptr_t)LX(STOCK_OBSERVER_INPUT_UPDATE),
                                  (uintptr_t)&stock_observer_input_update) ||
        !sko_install(1)) {
        stock_observer_fail("install");
        stock_observer_restore();
        return 0;
    }
    stock_observer_install_tick = stats.issued;
    stock_observer_problem = "observing_only";
    return 1;
}

static int stock_observer_request(void) {
    /* accept_ui may retain the original worker across the load. boot_adopted
     * means a thread migration happened; it does not describe worker readiness.
     */
    int ok = accept_ui() && stats.active && boot_phase == 3 && stats.issued &&
             GetCurrentThreadId() != main_thread && gate_tls_worker(gate_tls_self()) &&
             !atomic_load(&stock_observer_requested);
    if (ok) {
        stock_observer_worker_thread = GetCurrentThreadId();
        atomic_store(&stock_observer_requested, 1);
    }
    return ok;
}

static int lua_stock_keyboard_observer_request(lua_State *L) {
    int ok = stock_observer_request();
    p_lua_pushnumber(L, ok);
    return 1;
}

static int lua_stock_keyboard_observer_status(lua_State *L) {
    if (!accept_ui()) {
        p_lua_pushnil(L);
        return 1;
    }
    sko_status state = sko_get_status();
    char output[900];
    int length = snprintf(
        output, sizeof(output),
        "mode=%s requested=%u installed_tick=%llu input_tick=%llu active=%u owned=%u "
        "uncertain=%u "
        "pending_readback=%u permission_pending=%u depth=%u overflow=%u queued=%u thread_failure=%u "
        "failed=%u "
        "cleaned=%u problem=%s failure_reason=%s",
        sko_session.consume_event ? "capture_suppressed" : "observe_only",
        atomic_load(&stock_observer_requested), (unsigned long long)stock_observer_install_tick,
        (unsigned long long)__atomic_load_n(&stock_observer_input_tick, __ATOMIC_ACQUIRE), state.active,
        state.owned, state.uncertain, state.pending_readback, stock_observer_permission_pending,
        atomic_load(&stock_observer_input_depth), state.overflow, state.queued,
        atomic_load(&stock_observer_thread_failure), stock_observer_failed, stock_observer_cleaned,
        stock_observer_problem, stock_observer_failure_reason);
    if (length < 0 || (size_t)length >= sizeof(output)) {
        p_lua_pushnil(L);
    } else {
        p_lua_pushlstring(L, output, (size_t)length);
    }
    return 1;
}

static int lua_stock_keyboard_observer_drain(lua_State *L) {
    sko_event event;
    if (!accept_ui() || !stock_observer_worker_boundary() || !sko_drain(&event, 1, 1)) {
        p_lua_pushnil(L);
        return 1;
    }
    char output[180];
    int length = snprintf(output, sizeof(output), "thread=%llu caller=%llx kind=%u id=%u state=%u",
                          (unsigned long long)event.thread, (unsigned long long)event.caller, event.kind,
                          (unsigned)event.id, event.state);
    if (length < 0 || (size_t)length >= sizeof(output)) {
        p_lua_pushnil(L);
    } else {
        p_lua_pushlstring(L, output, (size_t)length);
    }
    return 1;
}

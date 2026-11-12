/* Synthetic gate integration counterexamples: no game code is called. */
#define _GNU_SOURCE
#include <sys/mman.h>
static int observer_test_mprotect(void *address, size_t length, int protection);
#define mprotect observer_test_mprotect
#define GATE_TLS_TEST
#include "gate_linux.c"
#undef mprotect
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static unsigned fail_protect_call;
static unsigned protect_calls;

static int observer_test_mprotect(void *address, size_t length, int protection) {
    ++protect_calls;
    if (protect_calls == fail_protect_call) {
        errno = EACCES;
        return -1;
    }
    return mprotect(address, length, protection);
}

static __thread unsigned char fake_tls[0x2800];

static unsigned char *gate_tls_test_base(void) {
    return fake_tls + sizeof(fake_tls);
}

static void set_worker_view(int32_t value) {
    memcpy(gate_tls_test_base() + GATE_TLS_LINUX_VIEW, &value, sizeof(value));
}

static void test_request_same_worker(void) {
    boot_mode = 1;
    boot_phase = 3;
    boot_adopted = 0;
    stats.active = 1;
    stats.issued = 1;
    ui_thread = GetCurrentThreadId();
    main_thread = ui_thread + 1;
    atomic_store(&stock_observer_requested, 0);
    set_worker_view(0);
    assert(!stock_observer_request());
    assert(!atomic_load(&stock_observer_requested));
    set_worker_view(1);
    main_thread = ui_thread;
    assert(!stock_observer_request());
    main_thread = ui_thread + 1;
    assert(stock_observer_request());
    assert(!boot_adopted && stock_observer_worker_thread == ui_thread);
    assert(atomic_load(&stock_observer_requested) && ui_tls_tick == 1);
    assert(!stock_observer_request());
}

static void valid_admission(void) {
    atomic_store(&stock_observer_requested, 1);
    stock_observer_main_boundary = 1;
    boot_phase = 3;
    boot_adopted = 0;
    stats.active = 1;
    stats.completed = 41;
    stats.issued = 42;
    ui_tls_tick = 41;
    main_thread = GetCurrentThreadId();
    ui_thread = main_thread + 1;
    stock_observer_worker_thread = ui_thread;
    ui_tls = gate_tls_test_base();
    set_worker_view(1);
    *LX(STOCK_OBSERVER_NATIVE_FLAGS) = 0;
    *LX(0x3daab00u) = 0;
    *LX(LX_IN_FLIGHT_VA) = 0;
    atomic_store(&stock_observer_input_depth, 0);
    atomic_store(&sko_session.wrapper_depth, 0);
    stock_observer_problem = "test";
    stock_observer_failed = 0;
    stock_observer_failure_reason = "none";
}

static void rejected_boundary(void) {
    assert(!stock_observer_admit());
    assert(!strcmp(stock_observer_problem, "admission_boundary"));
    assert(!sko_initialized && !stock_observer_slot_owned);
}

static void test_admission_guards(void) {
    valid_admission();
    *LX(STOCK_OBSERVER_NATIVE_FLAGS) = 2;
    rejected_boundary();
    valid_admission();
    *LX(0x3daab00u) = 1;
    rejected_boundary();
    valid_admission();
    *LX(LX_IN_FLIGHT_VA) = 1;
    rejected_boundary();
    valid_admission();
    ++main_thread;
    rejected_boundary();
    valid_admission();
    --ui_tls_tick;
    rejected_boundary();
    valid_admission();
    ++stock_observer_worker_thread;
    rejected_boundary();
    valid_admission();
    set_worker_view(0);
    rejected_boundary();
    valid_admission();
    atomic_store(&stock_observer_input_depth, 1);
    rejected_boundary();
    valid_admission();
    atomic_store(&sko_session.wrapper_depth, 1);
    rejected_boundary();
    valid_admission();
    /* The synthetic all-zero image has no valid pins: reaching install proves
     * these cases test admission itself without executing an original handler. */
    assert(!stock_observer_admit());
    assert(stock_observer_failed && !strcmp(stock_observer_failure_reason, "install"));
}

static void test_worker_quiescence(void) {
    boot_adopted = 1;
    ui_thread = GetCurrentThreadId();
    stock_observer_worker_thread = ui_thread;
    int32_t worker_view = 1;
    memcpy(gate_tls_test_base() + GATE_TLS_LINUX_VIEW, &worker_view, sizeof(worker_view));
    stats.issued = 44;
    stock_observer_input_tick = 44;
    atomic_store(&stock_observer_input_depth, 0);
    atomic_store(&sko_session.wrapper_depth, 0);
    assert(stock_observer_worker_boundary());
    atomic_store(&stock_observer_input_depth, 1);
    assert(!stock_observer_worker_boundary());
    atomic_store(&stock_observer_requested, 1);
    stock_observer_slot_owned = 1;
    stock_observer_main_boundary = 0;
    stock_observer_cleaned = 0;
    stock_observer_restore();
    assert(!stock_observer_cleaned && stock_observer_slot_owned);
    assert(!strcmp(stock_observer_problem, "cleanup_boundary"));
    atomic_store(&stock_observer_input_depth, 0);
    atomic_store(&sko_session.wrapper_depth, 1);
    assert(!stock_observer_worker_boundary());
    atomic_store(&sko_session.wrapper_depth, 0);
    ++stats.issued;
    assert(!stock_observer_worker_boundary());
}

static void test_node_mapping(void) {
    uintptr_t *page = mmap(NULL, GATE_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(page != MAP_FAILED);
    *page = 0x1111;
    assert(stock_observer_swap_node(NULL, (uintptr_t)page + 1, 0x1111, 0x2222) == SKO_CAS_REFUSED);
    assert(stock_observer_swap_node(NULL, (uintptr_t)page, 0x3333, 0x2222) == SKO_CAS_CHANGED);
    assert(*page == 0x1111);
    assert(stock_observer_swap_node(NULL, (uintptr_t)page, 0x1111, 0x2222) == SKO_CAS_APPLIED);
    assert(*page == 0x2222);
    assert(!mprotect(page, GATE_PAGE, PROT_READ));
    assert(stock_observer_swap_node(NULL, (uintptr_t)page, 0x2222, 0x3333) == SKO_CAS_REFUSED);
    assert(*page == 0x2222);
    assert(!munmap(page, GATE_PAGE));
    assert(stock_observer_swap_node(NULL, (uintptr_t)page, 0, 1) == SKO_CAS_REFUSED);
}

static void test_vtable_slot(void) {
    stock_observer_slot_owned = 0;
    stock_observer_permission_pending = 0;
    uintptr_t *slot = (uintptr_t *)LX(STOCK_OBSERVER_INPUT_SLOT);
    void *page = (void *)((uintptr_t)slot & ~(uintptr_t)(GATE_PAGE - 1));
    uintptr_t original = (uintptr_t)LX(STOCK_OBSERVER_INPUT_UPDATE);
    uintptr_t wrapper = (uintptr_t)&stock_observer_input_update;
    *slot = original;
    assert(!mprotect(page, GATE_PAGE, PROT_READ));
    protect_calls = 0;
    fail_protect_call = 1;
    assert(!stock_observer_swap_slot(original, wrapper));
    assert(*slot == original && !stock_observer_slot_owned && !stock_observer_permission_pending);
    fail_protect_call = 0;
    assert(stock_observer_swap_slot(original, wrapper));
    assert(*slot == wrapper && stock_observer_slot_owned && !stock_observer_permission_pending);
    assert(!mprotect(page, GATE_PAGE, PROT_READ | PROT_WRITE));
    *slot = 0x12345678;
    assert(!mprotect(page, GATE_PAGE, PROT_READ));
    assert(!stock_observer_swap_slot(wrapper, original));
    assert(*slot == 0x12345678 && stock_observer_slot_owned && !stock_observer_permission_pending);
    assert(!mprotect(page, GATE_PAGE, PROT_READ | PROT_WRITE));
    *slot = wrapper;
    assert(!mprotect(page, GATE_PAGE, PROT_READ));
    assert(stock_observer_swap_slot(wrapper, original));
    assert(*slot == original && !stock_observer_slot_owned && !stock_observer_permission_pending);
    assert(!mprotect(page, GATE_PAGE, PROT_READ | PROT_WRITE));
    *slot = 0x12345678;
    assert(!mprotect(page, GATE_PAGE, PROT_READ));
    assert(!stock_observer_swap_slot(original, wrapper));
    assert(*slot == 0x12345678 && !stock_observer_slot_owned);
    char permissions[5];
    assert(gate_maps_find((uintptr_t)slot, permissions, NULL, 0) && !strcmp(permissions, "r--p"));
    assert(!mprotect(page, GATE_PAGE, PROT_READ | PROT_WRITE));
    *slot = original;
    assert(!mprotect(page, GATE_PAGE, PROT_READ));
    protect_calls = 0;
    fail_protect_call = 2;
    assert(!stock_observer_swap_slot(original, wrapper));
    assert(*slot == wrapper && stock_observer_slot_owned && stock_observer_permission_pending);
    assert(gate_maps_find((uintptr_t)slot, permissions, NULL, 0) && !strcmp(permissions, "rw-p"));
    fail_protect_call = 0;
    assert(stock_observer_swap_slot(wrapper, original));
    assert(*slot == original && !stock_observer_slot_owned && !stock_observer_permission_pending);
    assert(gate_maps_find((uintptr_t)slot, permissions, NULL, 0) && !strcmp(permissions, "r--p"));

    /* A successful pointer restore does not erase a failed permission restore.
     * The next cleanup retries only the remaining page-protection obligation.
     */
    assert(stock_observer_swap_slot(original, wrapper));
    protect_calls = 0;
    fail_protect_call = 2;
    assert(!stock_observer_swap_slot(wrapper, original));
    assert(*slot == original && !stock_observer_slot_owned && stock_observer_permission_pending);
    assert(gate_maps_find((uintptr_t)slot, permissions, NULL, 0) && !strcmp(permissions, "rw-p"));
    assert(stock_observer_has_cleanup());
    fail_protect_call = 0;
    valid_admission();
    stock_observer_restore();
    assert(stock_observer_cleaned && !stock_observer_permission_pending);
    assert(!stock_observer_has_cleanup() && *slot == original);
    assert(gate_maps_find((uintptr_t)slot, permissions, NULL, 0) && !strcmp(permissions, "r--p"));
}

static unsigned synthetic_pin_calls;

static int synthetic_pin_check(void *context) {
    (void)context;
    ++synthetic_pin_calls;
    return 0;
}

static void test_installed_slot_revalidation(void) {
    uintptr_t *slot = (uintptr_t *)LX(STOCK_OBSERVER_INPUT_SLOT);
    void *page = (void *)((uintptr_t)slot & ~(uintptr_t)(GATE_PAGE - 1));
    uintptr_t wrapper = (uintptr_t)&stock_observer_input_update;
    uintptr_t original = (uintptr_t)LX(STOCK_OBSERVER_INPUT_UPDATE);
    uintptr_t replacements[] = {original, (uintptr_t)0x12345678, wrapper};
    sko_session.platform.verify_pins = synthetic_pin_check;
    for (unsigned index = 0; index < 3; ++index) {
        assert(!mprotect(page, GATE_PAGE, PROT_READ | PROT_WRITE));
        *slot = replacements[index];
        if (index != 2) {
            assert(!mprotect(page, GATE_PAGE, PROT_READ));
        }
        valid_admission();
        stock_observer_install_tick = 42;
        stock_observer_slot_owned = 1;
        stock_observer_permission_pending = 0;
        atomic_store(&sko_session.active, 1);
        synthetic_pin_calls = 0;
        assert(!stock_observer_admit());
        assert(!strcmp(stock_observer_problem, "changed_dispatch"));
        assert(!strcmp(stock_observer_failure_reason, "changed_dispatch"));
        assert(!synthetic_pin_calls && *slot == replacements[index]);
    }
    assert(!mprotect(page, GATE_PAGE, PROT_READ | PROT_WRITE));
    *slot = original;
    assert(!mprotect(page, GATE_PAGE, PROT_READ));
}

int main(void) {
    size_t size = 0x7bb7000u - LX_IMAGE_BASE;
    image = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    assert(image != MAP_FAILED);
    test_request_same_worker();
    test_admission_guards();
    test_worker_quiescence();
    test_node_mapping();
    test_vtable_slot();
    test_installed_slot_revalidation();
    assert(!munmap(image, size));
    puts("PASS: stock observer synthetic gate boundaries and ownership");
    return 0;
}

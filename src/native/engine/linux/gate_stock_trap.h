/* Private, opt-in stock diagnostics for a disposable process. The process
 * must retain exclusive ownership of SIGTRAP and every published entry byte for its
 * lifetime: no debugger, tracer, later SIGTRAP mask changes, handler changes
 * or competing patch writers. A late trap and a foreign breakpoint at the same
 * address are indistinguishable. Never use this contract as playable input. */
#include <dirent.h>
#include <linux/membarrier.h>
#include <sys/syscall.h>

static void stock_state_entry(void);
static void stock_range_entry(void);
static void ik_target_entry(void);
static void ik_target_terminal(void);
static void ik_target_order_entry(void);
static uintptr_t stock_trap_state_site, stock_trap_range_site;
static uintptr_t stock_trap_ik_entry_site, stock_trap_ik_terminal_site;
static uintptr_t stock_trap_ik_order_site;
static int stock_trap_installed, stock_trap_sync_uncertain;
static int stock_trap_state_ready, stock_trap_range_ready;
static int stock_trap_ik_entry_ready, stock_trap_ik_terminal_ready;
static int stock_trap_ik_order_ready;
#ifdef GATE_TLS_TEST
static int stock_trap_test_sync_failures;
#endif

static void stock_trap_handler(int signal_number, siginfo_t *information, void *context_pointer) {
    ucontext_t *context = context_pointer;
    uintptr_t instruction = (uintptr_t)context->uc_mcontext.gregs[REG_RIP];
    if (information && (information->si_code == SI_KERNEL || information->si_code == TRAP_BRKPT)) {
        if (__atomic_load_n(&stock_trap_state_ready, __ATOMIC_ACQUIRE) &&
            instruction == stock_trap_state_site + 1) {
            context->uc_mcontext.gregs[REG_RIP] = (greg_t)(uintptr_t)stock_state_entry;
            return;
        }
        if (__atomic_load_n(&stock_trap_range_ready, __ATOMIC_ACQUIRE) &&
            instruction == stock_trap_range_site + 1) {
            context->uc_mcontext.gregs[REG_RIP] = (greg_t)(uintptr_t)stock_range_entry;
            return;
        }
        if (__atomic_load_n(&stock_trap_ik_entry_ready, __ATOMIC_ACQUIRE) &&
            instruction == stock_trap_ik_entry_site + 1) {
            context->uc_mcontext.gregs[REG_RIP] = (greg_t)(uintptr_t)ik_target_entry;
            return;
        }
        if (__atomic_load_n(&stock_trap_ik_terminal_ready, __ATOMIC_ACQUIRE) &&
            instruction == stock_trap_ik_terminal_site + 1) {
            context->uc_mcontext.gregs[REG_RIP] = (greg_t)(uintptr_t)ik_target_terminal;
            return;
        }
        if (__atomic_load_n(&stock_trap_ik_order_ready, __ATOMIC_ACQUIRE) &&
            instruction == stock_trap_ik_order_site + 1) {
            context->uc_mcontext.gregs[REG_RIP] = (greg_t)(uintptr_t)ik_target_order_entry;
            return;
        }
    }
    /* The admission rule requires the previous disposition to be SIG_DFL.
     * Re-raise unrelated traps with their normal fatal default disposition. */
    struct sigaction action = {0};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(signal_number, &action, NULL);
    raise(signal_number);
}

static int stock_trap_handler_owned(void) {
    struct sigaction current;
    return !sigaction(SIGTRAP, NULL, &current) && (current.sa_flags & SA_SIGINFO) &&
        current.sa_sigaction == stock_trap_handler;
}

/* This is a snapshot, not a guarantee against later thread or tracer changes.
 * Those changes are excluded by the explicit lifetime ownership contract. */
static int stock_trap_proc_status(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    char line[256];
    int have_tracer = 0, have_mask = 0, valid = 1;
    while (fgets(line, sizeof(line), file)) {
        unsigned long tracer;
        unsigned long long mask;
        if (sscanf(line, "TracerPid: %lu", &tracer) == 1) {
            have_tracer = 1;
            if (tracer) valid = 0;
        } else if (sscanf(line, "SigBlk: %llx", &mask) == 1) {
            have_mask = 1;
            if (mask & (1ULL << (SIGTRAP - 1))) valid = 0;
        }
    }
    if (ferror(file)) valid = 0;
    fclose(file);
    return valid && have_tracer && have_mask;
}

static int stock_trap_preflight(void) {
    DIR *directory = opendir("/proc/self/task");
    if (!directory) return 0;
    struct dirent *entry;
    int valid = 1, found = 0;
    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        const char *digit = entry->d_name;
        if (*digit < '0' || *digit > '9') continue;
        while (*digit >= '0' && *digit <= '9') ++digit;
        if (*digit) continue;
        char path[128];
        int length = snprintf(path, sizeof(path), "/proc/self/task/%s/status", entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(path) || !stock_trap_proc_status(path)) {
            valid = 0;
            break;
        }
        ++found;
        errno = 0;
    }
    if (errno) valid = 0;
    closedir(directory);
    return valid && found;
}

static int stock_trap_prepare(void) {
    if (!stock_trap_preflight()) return 0;
    if (stock_trap_installed) return stock_trap_handler_owned();
    struct sigaction prior, action = {0};
    if (sigaction(SIGTRAP, NULL, &prior) || prior.sa_handler != SIG_DFL) return 0;
    if (syscall(SYS_membarrier, MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE, 0, 0)) return 0;
    stock_trap_state_site = (uintptr_t)LX(0x1050fa0u);
    stock_trap_range_site = (uintptr_t)LX(0xcecbd0u);
    stock_trap_ik_entry_site = (uintptr_t)LX(0x1566170u);
    stock_trap_ik_terminal_site = (uintptr_t)LX(0x15661fdu);
    stock_trap_ik_order_site = (uintptr_t)LX(0x15661fdu);
    action.sa_sigaction = stock_trap_handler;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTRAP, &action, NULL)) return 0;
    stock_trap_installed = 1;
    return stock_trap_handler_owned();
}

static int stock_trap_sync(void) {
#ifdef GATE_TLS_TEST
    if (stock_trap_test_sync_failures) {
        --stock_trap_test_sync_failures;
        stock_trap_sync_uncertain = 1;
        return 0;
    }
#endif
    int ok = syscall(SYS_membarrier, MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE, 0, 0) == 0;
    stock_trap_sync_uncertain = !ok;
    return ok;
}

/* The CAS checks ownership of all 16 bytes, but changes only byte zero. The
 * unchanged tail preserves the original instructions for a thread already
 * inside the sequence. SYNC_CORE serializes sibling cores after the store;
 * this does not prove the instruction-fetch transition during the store on
 * every processor. The diagnostic assumes x86-64 coherent single-byte code
 * modification. Old entry executions can complete without producing a row. */
static int stock_trap_swap(void *site, unsigned __int128 expected, unsigned __int128 desired) {
    int swapped = gate_code_swap128(site, expected, desired);
    int synced = stock_trap_sync();
    return swapped && synced;
}

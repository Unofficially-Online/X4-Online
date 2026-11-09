/* Offline admission checks. Run in a separate process: the trap handler stays
 * installed for process lifetime to handle traps delivered after restoration. */
#include <sys/resource.h>
#include <sys/wait.h>

static void stock_test_foreign_trap(int signal_number) {
    (void)signal_number;
}

static void test_stock_trap_contract(void) {
    struct sigaction original, foreign = {0};
    assert(!sigaction(SIGTRAP, NULL, &original));
    foreign.sa_handler = stock_test_foreign_trap;
    sigemptyset(&foreign.sa_mask);
    assert(!sigaction(SIGTRAP, &foreign, NULL));
    assert(!stock_trap_prepare() && !stock_trap_installed);
    assert(!sigaction(SIGTRAP, &original, NULL));

    sigset_t blocked, old;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGTRAP);
    assert(!pthread_sigmask(SIG_BLOCK, &blocked, &old));
    assert(!stock_trap_preflight() && !stock_trap_prepare());
    assert(!pthread_sigmask(SIG_SETMASK, &old, NULL));
    assert(stock_trap_prepare() && stock_trap_handler_owned());
    assert(stock_trap_sync());
    assert(!sigaction(SIGTRAP, &foreign, NULL));
    assert(!stock_trap_handler_owned() && !stock_trap_prepare());
    struct sigaction ours = {0};
    ours.sa_sigaction = stock_trap_handler;
    ours.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&ours.sa_mask);
    assert(!sigaction(SIGTRAP, &ours, NULL));
    assert(stock_trap_handler_owned());
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        struct rlimit no_core = {0, 0};
        setrlimit(RLIMIT_CORE, &no_core);
        raise(SIGTRAP);
        _exit(99);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGTRAP);
    /* Publish only STATE, then prove a foreign RANGE breakpoint is not ours. */
    assert(economy_rng_xstate());
    stock_state_xstate_mask = economy_rng_xstate_mask;
    stock_state_continuation = (uintptr_t)LX(STOCK_STATE_CONTINUE);
    __atomic_store_n(&stock_trap_state_ready, 1, __ATOMIC_RELEASE);

    child = fork();
    assert(child >= 0);
    if (child == 0) {
        struct rlimit no_core = {0, 0};
        setrlimit(RLIMIT_CORE, &no_core);
        unsigned char *site = LX(STOCK_RANGE_SITE);
        uintptr_t page = (uintptr_t)site & ~(uintptr_t)(GATE_PAGE - 1);
        if (mprotect((void *)page, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC)) _exit(98);
        site[0] = 0xcc;
        ((void (*)(void))(void *)site)();
        _exit(99);
    }
    status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGTRAP);
    stats.active = 1;
    ui_thread = GetCurrentThreadId();
    fs.state = FS_ARMED;
    assert(!stock_state_arm() && !strcmp(stock_state_stage, "fork_save"));
    assert(!stock_range_arm() && !strcmp(stock_range_stage, "fork_save"));
    fs.state = FS_OFF;
    assert(!fork_save_arm(2, "test", "/tmp/x4-test", 50, 600));
    puts("stock trap contract: handler, blocked signal, sync and fork exclusion PASS");
}

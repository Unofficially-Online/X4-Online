/* Offline SIGTRAP experiment; this is not a game integration. */
#define _GNU_SOURCE
#include <assert.h>
#include <linux/membarrier.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

static unsigned char *code_page, *data_page, *stub;
static _Atomic int fault_entered, fault_release, trap_entered, trap_release;
static _Atomic int hold_trap, other_traps;
static int result;
static size_t page_size;

static void fault_handler(int sig, siginfo_t *info, void *opaque) {
    (void)sig; (void)info;
    ucontext_t *context = opaque;
    if ((uintptr_t)context->uc_mcontext.gregs[REG_RIP] != (uintptr_t)code_page + 5)
        _exit(81);
    atomic_store_explicit(&fault_entered, 1, memory_order_release);
    while (!atomic_load_explicit(&fault_release, memory_order_acquire)) { }
}
static void trap_handler(int sig, siginfo_t *info, void *opaque) {
    (void)sig;
    ucontext_t *context = opaque;
    if (info->si_code > 0 && (uintptr_t)context->uc_mcontext.gregs[REG_RIP] == (uintptr_t)code_page + 1) {
        atomic_store_explicit(&trap_entered, 1, memory_order_release);
        while (atomic_load_explicit(&hold_trap, memory_order_acquire) &&
               !atomic_load_explicit(&trap_release, memory_order_acquire)) { }
        context->uc_mcontext.gregs[REG_RIP] = (greg_t)(uintptr_t)stub;
    } else {
        atomic_fetch_add_explicit(&other_traps, 1, memory_order_relaxed);
    }
}
static void sync_cores(void) {
    assert(syscall(SYS_membarrier, MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE, 0, 0) == 0);
}
static void set_entry(unsigned char value) {
    assert(mprotect(code_page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) == 0);
    atomic_store_explicit((_Atomic unsigned char *)code_page, value, memory_order_seq_cst);
    sync_cores();
    assert(mprotect(code_page, page_size, PROT_READ | PROT_EXEC) == 0);
}
static void *invoke(void *unused) {
    (void)unused;
    int (*function)(int *) = (void *)code_page;
    result = function((int *)data_page);
    return NULL;
}
int main(void) {
    page_size = (size_t)sysconf(_SC_PAGESIZE);
    assert(syscall(SYS_membarrier, MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE, 0, 0) == 0);
    code_page = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    data_page = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    stub = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(code_page != MAP_FAILED && data_page != MAP_FAILED && stub != MAP_FAILED);
    /* mov eax,17; mov edx,[rdi]; add eax,edx; ret */
    const unsigned char body[] = {0xb8, 17, 0, 0, 0, 0x8b, 0x17, 0x01, 0xd0, 0xc3};
    memcpy(code_page, body, sizeof(body));
    memcpy(stub, body, 5);
    stub[5] = 0xff; stub[6] = 0x25; memset(stub + 7, 0, 4);
    void *continuation = code_page + 5;
    memcpy(stub + 11, &continuation, sizeof(continuation));
    *(int *)data_page = 25;
    assert(mprotect(code_page, page_size, PROT_READ | PROT_EXEC) == 0);
    assert(mprotect(stub, page_size, PROT_READ | PROT_EXEC) == 0);
    struct sigaction action = {0};
    sigemptyset(&action.sa_mask); action.sa_flags = SA_SIGINFO; action.sa_sigaction = fault_handler;
    assert(sigaction(SIGSEGV, &action, NULL) == 0);
    action.sa_sigaction = trap_handler;
    assert(sigaction(SIGTRAP, &action, NULL) == 0);

    /* Park exactly at the second instruction, install INT3 and resume it. */
    assert(mprotect(data_page, page_size, PROT_NONE) == 0);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, invoke, NULL) == 0);
    while (!atomic_load_explicit(&fault_entered, memory_order_acquire)) { }
    set_entry(0xcc);
    assert(mprotect(data_page, page_size, PROT_READ | PROT_WRITE) == 0);
    atomic_store_explicit(&fault_release, 1, memory_order_release);
    assert(pthread_join(thread, NULL) == 0 && result == 42 && !atomic_load(&trap_entered));
    puts("paused second instruction survives installation: PASS");

    /* Enter through INT3, park at the second instruction, then restore. */
    atomic_store(&fault_entered, 0);
    atomic_store(&fault_release, 0);
    assert(mprotect(data_page, page_size, PROT_NONE) == 0);
    assert(pthread_create(&thread, NULL, invoke, NULL) == 0);
    while (!atomic_load_explicit(&fault_entered, memory_order_acquire)) { }
    set_entry(body[0]);
    assert(mprotect(data_page, page_size, PROT_READ | PROT_WRITE) == 0);
    atomic_store_explicit(&fault_release, 1, memory_order_release);
    assert(pthread_join(thread, NULL) == 0 && result == 42);
    puts("paused second instruction survives restoration: PASS");

    /* Trap begins, restore the first byte, then finish the late signal. */
    set_entry(0xcc);
    atomic_store(&trap_entered, 0);
    atomic_store(&hold_trap, 1);
    assert(pthread_create(&thread, NULL, invoke, NULL) == 0);
    while (!atomic_load_explicit(&trap_entered, memory_order_acquire)) { }
    set_entry(body[0]);
    atomic_store_explicit(&trap_release, 1, memory_order_release);
    assert(pthread_join(thread, NULL) == 0 && result == 42);
    puts("late trap survives first-byte restoration: PASS");
    raise(SIGTRAP);
    assert(atomic_load(&other_traps) == 1);
    puts("distinct user-generated trap reaches alternate branch: PASS");
    puts("not tested: same-site foreign trap provenance, ptrace, or game signal masks");
    return 0;
}

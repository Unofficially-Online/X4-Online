#define _GNU_SOURCE
#include <assert.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include "player_lifetime_preload_emit.h"

static _Atomic(const plp_binding *) binding;
static int ship, other_ship;
static _Atomic uint32_t replacement_count;
_Atomic uint32_t plp_entry_count;
void *plp_original_target0;
void *plp_original_target1;
#include "player_lifetime_preload_wrappers.h"
static unsigned char controller[0x28], other_controller[0x28];
static void ship_fatal(void *object) { _exit(object == &ship ? 31 : 91); }
static void controller_fatal(void *object) { _exit(object == controller ? 32 : 92); }
static void incorrect_return(void *object) { (void)object; }
static void *recursive_stub;
static uintptr_t nested_replacement(void *object, uintptr_t marker, int flags) {
    return ((uintptr_t (*)(void *, uintptr_t, int))recursive_stub)(object, marker, flags);
}
static void check_fatal(void *address, void *object, int expected) {
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        ((void (*)(void *))address)(object);
        _exit(99);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == expected);
}
int main(void) {
    unsigned char *pages = mmap(NULL, PLP_PAGE * 4, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(pages != MAP_FAILED);
    const unsigned char replacement[] = {0x41,0x55,0x41,0x54,0x41,0x89,0xd4,0x55,
                                          0x48,0x8d,0x04,0x16,0x5d,0x41,0x5c,0x41,0x5d,0xc3};
    const unsigned char destructor[] = {0x41,0x55,0x48,0x8d,0x05,0x97,0x02,0x53,0x02,0x41,0x5d,0xc3};
    memcpy(pages, replacement, sizeof replacement);
    memcpy(pages + PLP_PAGE, destructor, sizeof destructor);
    uintptr_t vtable = (uintptr_t)(pages + PLP_PAGE * 3);
    memset(pages + PLP_PAGE * 2, 0xcc, PLP_PAGE * 2);
    plp_emit(pages + PLP_PAGE * 2, (uintptr_t)&binding, (uintptr_t)&plp_entry_count, 0, (uintptr_t)pages, vtable, (uintptr_t)plp_call_original0);
    plp_emit(pages + PLP_PAGE * 3, (uintptr_t)&binding, (uintptr_t)&plp_entry_count, 1, (uintptr_t)(pages + PLP_PAGE), vtable, (uintptr_t)plp_call_original1);
    plp_original_target0 = pages + PLP_PAGE * 2 + PLP_TRAMPOLINE_OFFSET;
    plp_original_target1 = pages + PLP_PAGE * 3 + PLP_TRAMPOLINE_OFFSET;
    assert(!mprotect(pages, PLP_PAGE * 4, PROT_READ | PROT_EXEC));
    void *replace_stub = pages + PLP_PAGE * 2;
    void *destruct_stub = pages + PLP_PAGE * 3;
    uintptr_t marker = 0x12345678;
    assert(((uintptr_t (*)(void *, uintptr_t, int))replace_stub)(&ship, marker, 7) == marker + 7);
    assert(((uintptr_t (*)(void *))destruct_stub)(controller) == vtable);
    void *held_ship = &ship;
    void *unrelated_ship = &other_ship;
    memcpy(controller + 0x20, &held_ship, sizeof held_ship);
    memcpy(other_controller + 0x20, &unrelated_ship, sizeof unrelated_ship);
    plp_binding preparation = {.ship = &ship, .ship_fatal = ship_fatal,
        .controller_fatal = controller_fatal, .owner_thread = (uint32_t)syscall(SYS_gettid),
        .construction = 1, .replacement_counter = &replacement_count};
    atomic_store_explicit(&binding, &preparation, memory_order_release);
    /* The owner may perform Take; a different TID may not replace this ship. */
    assert(((uintptr_t (*)(void *, uintptr_t, int))replace_stub)(&ship, marker, 7) == marker + 7);
    assert(atomic_load(&replacement_count) == 1);
    check_fatal(replace_stub, &ship, 31);
    /* Exercise a genuinely nested owner call: the fake stock body calls the
     * same stub again before its first invocation returns. */
    pid_t nested = fork();
    assert(nested >= 0);
    if (!nested) {
        preparation.owner_thread = (uint32_t)syscall(SYS_gettid);
        atomic_store(&replacement_count, 0);
        atomic_store_explicit(&binding, &preparation, memory_order_release);
        assert(!mprotect(pages, PLP_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
        const unsigned char tail[] = {0x5d, 0x41, 0x5c, 0x41, 0x5d, 0xff, 0x25, 0,0,0,0};
        memcpy(pages + 8, tail, sizeof tail);
        uintptr_t nested_target = (uintptr_t)nested_replacement;
        memcpy(pages + 8 + sizeof tail, &nested_target, sizeof nested_target);
        assert(!mprotect(pages, PLP_PAGE, PROT_READ | PROT_EXEC));
        recursive_stub = replace_stub;
        ((uintptr_t (*)(void *, uintptr_t, int))replace_stub)(&ship, marker, 7);
        _exit(99);
    }
    int nested_status = 0;
    assert(waitpid(nested, &nested_status, 0) == nested && WIFEXITED(nested_status) &&
           WEXITSTATUS(nested_status) == 31);
    memset(controller + 0x20, 0, sizeof held_ship);
    check_fatal(destruct_stub, controller, 32);
    /* An unrelated and not-yet-bound PlayerController is vetoed too. */
    memset(other_controller + 0x20, 0, sizeof unrelated_ship);
    check_fatal(destruct_stub, other_controller, 92);
    plp_binding final = preparation;
    final.controller = controller;
    final.construction = 0;
    atomic_store_explicit(&binding, &final, memory_order_release);
    check_fatal(replace_stub, &ship, 31);
    /* Exact controller remains guarded even if its backpointer is cleared. */
    memset(controller + 0x20, 0, sizeof held_ship);
    check_fatal(destruct_stub, controller, 32);
    assert(((uintptr_t (*)(void *, uintptr_t, int))replace_stub)(&other_ship, marker, 7) == marker + 7);
    assert(((uintptr_t (*)(void *))destruct_stub)(other_controller) == vtable);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        const struct rlimit no_core = {0, 0};
        assert(!setrlimit(RLIMIT_CORE, &no_core));
        plp_binding bad = final;
        bad.ship_fatal = incorrect_return;
        atomic_store_explicit(&binding, &bad, memory_order_release);
        ((void (*)(void *))replace_stub)(&ship);
        _exit(99);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGILL);
    puts("lifetime preload construction, foreign thread and final guards PASS");
}

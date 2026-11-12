#define _GNU_SOURCE
/* Exercise the real registration routines without activating the constructor. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/wait.h>
#include "player_lifetime_preload.c"
static void fatal(void *object) { (void)object; }
static void *foreign_complete(void *controller) {
    assert(!x4mp_player_lifetime_complete(controller));
    return NULL;
}
int main(void) {
    int ship, wrong_ship;
    unsigned char controller[0x28] = {0};
    void *ship_pointer = &ship;
    memcpy(controller + 0x20, &ship_pointer, sizeof ship_pointer);
    assert(!x4mp_player_lifetime_prepare(&ship, fatal, fatal));
    information.installed = 1;
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        atomic_store(&plp_entry_count, 1);
        assert(!x4mp_player_lifetime_prepare(&ship, fatal, fatal));
        assert(atomic_load(&registration) == 1 && atomic_load(&binding_slot));
        assert(!x4mp_player_lifetime_prepare(&ship, fatal, fatal));
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    assert(!x4mp_player_lifetime_prepare(NULL, fatal, fatal));
    assert(x4mp_player_lifetime_prepare(&ship, fatal, fatal));
    const plp_binding *preparation = atomic_load_explicit(&binding_slot, memory_order_acquire);
    assert(preparation && preparation->ship == &ship && !preparation->controller && preparation->construction);
    assert(!x4mp_player_lifetime_prepare(&ship, fatal, fatal));
    pthread_t foreign;
    assert(!pthread_create(&foreign, NULL, foreign_complete, controller));
    assert(!pthread_join(foreign, NULL));
    assert(!x4mp_player_lifetime_complete(controller));
    atomic_store(&construction_replacements, 2);
    assert(!x4mp_player_lifetime_complete(controller));
    atomic_store(&construction_replacements, 1);
    ship_pointer = &wrong_ship;
    memcpy(controller + 0x20, &ship_pointer, sizeof ship_pointer);
    assert(!x4mp_player_lifetime_complete(controller));
    assert(atomic_load_explicit(&binding_slot, memory_order_acquire) == preparation);
    ship_pointer = &ship;
    memcpy(controller + 0x20, &ship_pointer, sizeof ship_pointer);
    assert(x4mp_player_lifetime_complete(controller));
    const plp_binding *final = atomic_load_explicit(&binding_slot, memory_order_acquire);
    assert(final && final != preparation && final->ship == &ship && final->controller == controller &&
           !final->construction && final->ship_fatal == fatal && final->controller_fatal == fatal);
    assert(!x4mp_player_lifetime_complete(controller));
    assert(atomic_load_explicit(&binding_slot, memory_order_acquire) == final);
    puts("lifetime preload construction ownership and final publication PASS");
}

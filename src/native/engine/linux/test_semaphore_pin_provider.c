#include "player_semaphore_original_view.h"
static player_semaphore_observer_info information;
__attribute__((visibility("default"))) uintptr_t x4mp_semaphore_observer_originals[4];
#define slots x4mp_semaphore_observer_originals
__attribute__((visibility("default"))) void test_semaphore_configure(const player_semaphore_observer_info *value) {
    information = *value;
    for (unsigned i = 0; i < 4; ++i) {
        slots[i] = value->sites[i].page + PLAYER_SEM_TRAMPOLINE_OFFSET;
        information.sites[i].original_slot = (uintptr_t)&slots[i];
    }
}
__attribute__((visibility("default"))) void test_semaphore_drift_slot(void) { ++slots[0]; }
__attribute__((visibility("default"))) unsigned x4mp_semaphore_observer_info(player_semaphore_observer_info *out) { *out = information; return 1; }
__attribute__((visibility("default"))) void x4mp_semaphore_observer_constructor(void) {}
__attribute__((visibility("default"))) void x4mp_semaphore_observer_post(void) {}
__attribute__((visibility("default"))) void x4mp_semaphore_observer_wait(void) {}
__attribute__((visibility("default"))) void x4mp_semaphore_observer_timed_wait(void) {}

#include "player_lifetime_preload_emit.h"
static void *binding_slot;
_Atomic uint32_t plp_entry_count;
void *plp_original_target0;
void *plp_original_target1;
#include "player_lifetime_preload_wrappers.h"
static plp_information information;
__attribute__((visibility("default"))) void fake_lifetime_setup(void *first, void *second) {
    plp_original_target0 = (unsigned char *)first + PLP_TRAMPOLINE_OFFSET;
    plp_original_target1 = (unsigned char *)second + PLP_TRAMPOLINE_OFFSET;
    information = (plp_information){PLP_VERSION, 1, {first, second}, &binding_slot, &plp_entry_count,
        {(void *)plp_call_original0, (void *)plp_call_original1},
        {&plp_original_target0, &plp_original_target1}};
}
__attribute__((visibility("default"))) unsigned x4mp_player_lifetime_info(plp_information *out) {
    *out = information;
    return PLP_VERSION;
}
__attribute__((visibility("default"))) int x4mp_player_lifetime_register(void *ship, void *controller,
        void (*ship_fatal)(void *), void (*controller_fatal)(void *)) {
    return ship && controller && ship_fatal && controller_fatal;
}

__attribute__((visibility("default"))) int x4mp_player_lifetime_prepare(void *ship,
        void (*ship_fatal)(void *), void (*controller_fatal)(void *)) {
    return ship && ship_fatal && controller_fatal;
}
__attribute__((visibility("default"))) int x4mp_player_lifetime_complete(void *controller) {
    return controller != 0;
}

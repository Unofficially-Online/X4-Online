#ifndef X4_PLAYER_LIFETIME_PRELOAD_PIN_H
#define X4_PLAYER_LIFETIME_PRELOAD_PIN_H
/* Include after LX, gate_read_local, gate_maps_find and hash_ok definitions.
 * This validates both entire RX stub pages, both current entry jumps, their
 * common named provider and binding storage before restoring eight original
 * bytes in a private copy. It never changes executable memory. */
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "player_lifetime_preload_emit.h"

typedef unsigned (*plp_info_fn)(plp_information *);
typedef int (*plp_register_fn)(void *, void *, void (*)(void *), void (*)(void *));
static plp_register_fn player_lifetime_provider(plp_information *information) {
    void *get_info = dlsym(RTLD_DEFAULT, "x4mp_player_lifetime_info");
    void *register_object = dlsym(RTLD_DEFAULT, "x4mp_player_lifetime_register");
    Dl_info first, second, slot, counter;
    if (!get_info || !register_object || !dladdr(get_info, &first) || !dladdr(register_object, &second) ||
        first.dli_fbase != second.dli_fbase || !first.dli_fname)
        return NULL;
    const char *name = strrchr(first.dli_fname, '/');
    name = name ? name + 1 : first.dli_fname;
    if (strcmp(name, "libx4mp_player_lifetime.so") ||
        ((plp_info_fn)get_info)(information) != PLP_VERSION || information->version != PLP_VERSION ||
        information->installed != 1 || !information->binding_slot ||
        !dladdr(information->binding_slot, &slot) || slot.dli_fbase != first.dli_fbase ||
        !information->entry_count || !dladdr(information->entry_count, &counter) ||
        counter.dli_fbase != first.dli_fbase)
        return NULL;
    char slot_permissions[5];
    if (!gate_maps_find((uintptr_t)information->binding_slot, slot_permissions, NULL, 0) ||
        strcmp(slot_permissions, "rw-p") ||
        !gate_maps_find((uintptr_t)information->entry_count, slot_permissions, NULL, 0) ||
        strcmp(slot_permissions, "rw-p"))
        return NULL;
    for (unsigned index = 0; index < 2; ++index) {
        uintptr_t page = (uintptr_t)information->pages[index];
        uintptr_t site = (uintptr_t)LX(plp_sites[index]);
        char permissions[5], path[PATH_MAX];
        uintptr_t wrapper = (uintptr_t)information->wrappers[index];
        uintptr_t slot_address = (uintptr_t)information->original_slots[index];
        Dl_info wrapper_owner, trampoline_owner;
        unsigned char expected_wrapper[22], actual_wrapper[22];
        uintptr_t trampoline = 0;
        if (!wrapper || !slot_address || !dladdr((void *)wrapper, &wrapper_owner) ||
            !dladdr((void *)slot_address, &trampoline_owner) ||
            wrapper_owner.dli_fbase != first.dli_fbase || trampoline_owner.dli_fbase != first.dli_fbase ||
            !gate_maps_find(wrapper, permissions, NULL, 0) || strcmp(permissions, "r-xp") ||
            !gate_maps_find(slot_address, permissions, NULL, 0) || strcmp(permissions, "rw-p") ||
            !gate_read_local((void *)slot_address, &trampoline, sizeof trampoline) ||
            trampoline != page + PLP_TRAMPOLINE_OFFSET ||
            !plp_wrapper_bytes(expected_wrapper, wrapper, slot_address, (uintptr_t)information->entry_count) ||
            !gate_read_local((void *)wrapper, actual_wrapper, sizeof actual_wrapper) ||
            memcmp(expected_wrapper, actual_wrapper, sizeof actual_wrapper)) {
            return NULL;
        }
        if (!page || (page & (PLP_PAGE - 1)) ||
            !gate_maps_find(site, permissions, NULL, 0) || strcmp(permissions, "r-xp") ||
            !gate_maps_find(page, permissions, path, sizeof path) || strcmp(permissions, "r-xp") || path[0])
            return NULL;
        int64_t distance = (int64_t)page - (int64_t)(site + 5);
        if (distance < INT32_MIN || distance > INT32_MAX)
            return NULL;
        unsigned char expected_word[8] = {0xe9,0,0,0,0,0x90,0x90,0x90}, actual_word[8];
        int32_t relative = (int32_t)distance;
        memcpy(expected_word + 1, &relative, 4);
        if (!gate_read_local((void *)site, actual_word, sizeof actual_word) ||
            memcmp(actual_word, expected_word, sizeof actual_word))
            return NULL;
        unsigned char expected_page[PLP_PAGE], actual_page[PLP_PAGE];
        memset(expected_page, 0xcc, sizeof expected_page);
        plp_emit(expected_page, (uintptr_t)information->binding_slot, (uintptr_t)information->entry_count,
                 index, site, (uintptr_t)LX(PLP_VTABLE), wrapper);
        if (!gate_read_local((void *)page, actual_page, sizeof actual_page) ||
            memcmp(actual_page, expected_page, sizeof expected_page))
            return NULL;
    }
    return (plp_register_fn)register_object;
}
typedef int (*plp_prepare_fn)(void *, void (*)(void *), void (*)(void *));
typedef int (*plp_complete_fn)(void *);

/* These accessors authenticate the same provider and complete stub pages as
 * the pin check. Both returned functions stay resident until process exit. */
static inline plp_prepare_fn player_lifetime_prepare_provider(plp_information *information) {
    plp_register_fn registration = player_lifetime_provider(information);
    void *prepare = dlsym(RTLD_DEFAULT, "x4mp_player_lifetime_prepare");
    Dl_info registration_owner, prepare_owner;
    if (!registration || !prepare || !dladdr((void *)registration, &registration_owner) ||
        !dladdr(prepare, &prepare_owner) || registration_owner.dli_fbase != prepare_owner.dli_fbase) {
        return NULL;
    }
    return (plp_prepare_fn)prepare;
}

static inline plp_complete_fn player_lifetime_complete_provider(plp_information *information) {
    plp_register_fn registration = player_lifetime_provider(information);
    void *complete = dlsym(RTLD_DEFAULT, "x4mp_player_lifetime_complete");
    Dl_info registration_owner, complete_owner;
    if (!registration || !complete || !dladdr((void *)registration, &registration_owner) ||
        !dladdr(complete, &complete_owner) || registration_owner.dli_fbase != complete_owner.dli_fbase) {
        return NULL;
    }
    return (plp_complete_fn)complete;
}

/* Exact FDE length and SHA remain supplied by the call site. */
static int player_lifetime_original_pin(uintptr_t va, size_t length, const char *sha) {
    if (va != PLP_REPLACE && va != PLP_DESTRUCT)
        return 0;
    if (length < 8 || length > 1268)
        return 0;
    unsigned char original[1268];
    if (!gate_read_local(LX(va), original, length))
        return 0;
    if (hash_ok(original, length, sha))
        return 1;
    plp_information information = {0};
    if (!player_lifetime_provider(&information))
        return 0;
    unsigned index = va == PLP_DESTRUCT;
    memcpy(original, &plp_original[index], 8);
    return hash_ok(original, length, sha);
}
#endif

#ifndef X4_LINUX_PLAYER_GETTER_PRELOAD_PIN_H
#define X4_LINUX_PLAYER_GETTER_PRELOAD_PIN_H

/* Original-view pin for the four exact-reciprocal preload windows in the
 * PlayerController transform getter. Only a private copy is normalized.
 * The provider, permissions, constants, site jumps, complete emitted slots,
 * return jumps and used lengths must match before any copy is changed. */
#include <stdint.h>
#include <string.h>
#include "player_factory_preload_pin.h"
#include "../../portable_math/exact_reciprocal_emit.h"

#define GETTER_PIN_VA 0x1659010u
#define GETTER_PIN_LENGTH 4479u
#define GETTER_PIN_DROP 144u
#define GETTER_PIN_FIRST_INDEX 456u
#define GETTER_PIN_WINDOW_COUNT 4u
static const char getter_pin_hash[] = "d1993184f89d0ccf09c4c075110ab8c0dafa2ff8fefd55d6232c00910235125f";
static const xrcp_window getter_pin_windows[GETTER_PIN_WINDOW_COUNT] = {
    {0x16598b6u, 7, 2, 0xef88f760b7840df9ULL, {{0, 4, XRCP_RSQRTSS, 0}, {4, 3, XRCP_COPY, 0}}},
    {0x1659b65u, 8, 2, 0xad6cacc0b17cf2feULL, {{0, 4, XRCP_RSQRTSS, 0}, {4, 4, XRCP_COPY, 0}}},
    {0x1659c95u, 8, 2, 0xad749fc0b184ae95ULL, {{0, 4, XRCP_RSQRTSS, 0}, {4, 4, XRCP_COPY, 0}}},
    {0x1659d60u, 8, 2, 0x5868443b72775743ULL, {{0, 4, XRCP_RSQRTSS, 0}, {4, 4, XRCP_COPY, 0}}},
};
static const unsigned char getter_pin_original[GETTER_PIN_WINDOW_COUNT][8] = {
    {0xf3, 0x0f, 0x52, 0xc0, 0x0f, 0x16, 0xd3},
    {0xf3, 0x0f, 0x52, 0xc0, 0x41, 0x0f, 0x14, 0xd5},
    {0xf3, 0x0f, 0x52, 0xc0, 0x41, 0x0f, 0x16, 0xd2},
    {0xf3, 0x0f, 0x52, 0xe4, 0x41, 0x0f, 0x14, 0xda}};

/* Generate the exact expected bytes in private storage. The emitter computes
 * RIP-relative operands from pointer differences, so translate the site and
 * constants by the same amount as the actual trampoline slot. It never reads
 * through these translated pointers. */
static int getter_pin_expected_slot(unsigned char expected[XRCP_SLOT], const xrcp_window *window,
                                    const unsigned char *original, uintptr_t region, uintptr_t site,
                                    uintptr_t slot, unsigned *emitted) {
    int32_t site_delta;
    if (!factory_pin_relative(slot, site, &site_delta))
        return 0;
    uintptr_t local_slot = (uintptr_t)expected;
    uintptr_t slot_offset = slot - region;
    if (local_slot < slot_offset || local_slot > INT64_MAX ||
        (site_delta < 0 && local_slot < (uintptr_t)(-(int64_t)site_delta)) ||
        (site_delta > 0 && local_slot > (uintptr_t)INT64_MAX - (uintptr_t)site_delta))
        return 0;
    uintptr_t local_site = (uintptr_t)((int64_t)local_slot + site_delta);
    uintptr_t local_constants = local_slot - slot_offset;
    memset(expected, 0xcc, XRCP_SLOT);
    return xrcp_emit_window(expected, window, original, (const unsigned char *)local_site,
                            (const unsigned char *)local_constants, GETTER_PIN_DROP, emitted);
}

static int getter_pin_normalize(unsigned char getter[GETTER_PIN_LENGTH], const unsigned char *region,
                                size_t region_size, const unsigned char *used, unsigned count) {
    if (!region || !used || region_size != FACTORY_PIN_REGION_SIZE || count != FACTORY_PIN_WINDOW_COUNT)
        return 0;
    uintptr_t base = (uintptr_t)region;
    uintptr_t used_address = (uintptr_t)used;
    if (base > UINTPTR_MAX - region_size || base > INT64_MAX - region_size || (base & (GATE_PAGE - 1)) ||
        used_address > UINTPTR_MAX - (GETTER_PIN_FIRST_INDEX + 3u))
        return 0;
    char permissions[5], path[PATH_MAX];
    for (size_t offset = 0; offset < region_size; offset += GATE_PAGE) {
        if (!gate_maps_find(base + offset, permissions, path, sizeof path) || strcmp(permissions, "r-xp") ||
            path[0])
            return 0;
    }
    unsigned char constants[FACTORY_PIN_CONSTANT_SIZE];
    if (!gate_read_local(region, constants, sizeof constants) ||
        memcmp(constants, factory_pin_constants, sizeof factory_pin_constants))
        return 0;
    for (unsigned index = sizeof factory_pin_constants; index < sizeof constants; ++index)
        if (constants[index] != 0xcc)
            return 0;

    for (unsigned index = 0; index < GETTER_PIN_WINDOW_COUNT; ++index) {
        const xrcp_window *window = &getter_pin_windows[index];
        unsigned slot_index = GETTER_PIN_FIRST_INDEX + index;
        size_t slot_offset = FACTORY_PIN_CONSTANT_SIZE + (size_t)slot_index * FACTORY_PIN_SLOT_SIZE;
        size_t getter_offset = window->rva - GETTER_PIN_VA;
        if (slot_offset > region_size || FACTORY_PIN_SLOT_SIZE > region_size - slot_offset ||
            getter_offset > GETTER_PIN_LENGTH || window->length > GETTER_PIN_LENGTH - getter_offset ||
            xrcp_fnv(getter_pin_original[index], window->length) != window->digest)
            return 0;
        uintptr_t slot = base + slot_offset;
        uintptr_t site = (uintptr_t)LX(window->rva);
        int32_t jump_to_slot;
        if (!factory_pin_relative(site + 5, slot, &jump_to_slot))
            return 0;
        unsigned char expected_site[XRCP_MAX_WINDOW];
        memset(expected_site, 0xcc, window->length);
        expected_site[0] = 0xe9;
        memcpy(expected_site + 1, &jump_to_slot, sizeof jump_to_slot);
        if (memcmp(getter + getter_offset, expected_site, window->length))
            return 0;
        unsigned char expected_slot[XRCP_SLOT], actual_slot[XRCP_SLOT], used_length;
        unsigned expected_used = 0;
        if (!getter_pin_expected_slot(expected_slot, window, getter_pin_original[index], base, site, slot,
                                      &expected_used) ||
            !gate_read_local((const void *)(used_address + slot_index), &used_length, 1) ||
            used_length != expected_used ||
            !gate_read_local((const void *)slot, actual_slot, sizeof actual_slot) ||
            memcmp(expected_slot, actual_slot, sizeof actual_slot))
            return 0;
    }
    for (unsigned index = 0; index < GETTER_PIN_WINDOW_COUNT; ++index)
        memcpy(getter + getter_pin_windows[index].rva - GETTER_PIN_VA, getter_pin_original[index],
               getter_pin_windows[index].length);
    return 1;
}

static int player_transform_getter_pin(void) {
    unsigned char getter[GETTER_PIN_LENGTH];
    if (!gate_read_local(LX(GETTER_PIN_VA), getter, sizeof getter))
        return 0;
    if (hash_ok(getter, sizeof getter, getter_pin_hash))
        return 1;
    factory_pin_region_fn provider = factory_pin_provider();
    if (!provider)
        return 0;
    const unsigned char *region = NULL, *used = NULL;
    size_t region_size = 0;
    unsigned count = provider(&region, &region_size, &used);
    return getter_pin_normalize(getter, region, region_size, used, count) &&
           hash_ok(getter, sizeof getter, getter_pin_hash);
}
#endif

#define _GNU_SOURCE
#include <assert.h>
#include <limits.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FACTORY_PIN_TEST
#define GATE_PAGE 4096u
#define FACTORY_START 0x119bd70u
static alignas(4096) unsigned char factory_image[24552];
static alignas(4096) unsigned char test_region[0x20000];
static unsigned char original_image[24552];
static unsigned char used[765];
static int region_is_rx = 1;
static unsigned provider_count = 765;
static size_t provider_size = sizeof(test_region);

#define LX(address) (factory_image + ((uintptr_t)(address) - FACTORY_START))
static int gate_read_local(const void *source, void *target, size_t size) {
    memcpy(target, source, size);
    return 1;
}
static int gate_maps_find(uintptr_t address, char permissions[5], char *path, size_t capacity) {
    if (address < (uintptr_t)test_region || address >= (uintptr_t)test_region + sizeof(test_region)) {
        return 0;
    }
    strcpy(permissions, region_is_rx ? "r-xp" : "rwxp");
    if (capacity) {
        path[0] = 0;
    }
    return 1;
}
static int hash_ok(const void *data, size_t size, const char *expected) {
    (void)expected;
    return size == sizeof(original_image) && !memcmp(data, original_image, size);
}
#include "player_factory_preload_pin.h"

static unsigned get_test_region(const unsigned char **region, size_t *size,
                                const unsigned char **used_lengths) {
    *region = test_region;
    *size = provider_size;
    *used_lengths = used;
    return provider_count;
}
factory_pin_region_fn factory_pin_test_provider = get_test_region;

static void setup_patched(void) {
    memset(factory_image, 0x55, sizeof(factory_image));
    memcpy(factory_image + FACTORY_PIN_WINDOW_VA - FACTORY_PIN_VA,
           factory_pin_original, sizeof(factory_pin_original));
    memcpy(original_image, factory_image, sizeof(original_image));
    memset(test_region, 0xcc, sizeof(test_region));
    memcpy(test_region, factory_pin_constants, sizeof(factory_pin_constants));
    memset(used, 0, sizeof(used));
    used[FACTORY_PIN_WINDOW_INDEX] = sizeof(factory_pin_prefix) + 5;
    provider_count = FACTORY_PIN_WINDOW_COUNT;
    provider_size = sizeof(test_region);
    region_is_rx = 1;

    uintptr_t site = (uintptr_t)LX(FACTORY_PIN_WINDOW_VA);
    uintptr_t slot = (uintptr_t)test_region + FACTORY_PIN_CONSTANT_SIZE +
        FACTORY_PIN_WINDOW_INDEX * FACTORY_PIN_SLOT_SIZE;
    int32_t to_slot;
    int32_t to_return;
    assert(factory_pin_relative(site + 5, slot, &to_slot));
    assert(factory_pin_relative(slot + sizeof(factory_pin_prefix) + 5,
                                site + sizeof(factory_pin_original), &to_return));
    unsigned char *patched = factory_image + FACTORY_PIN_WINDOW_VA - FACTORY_PIN_VA;
    memset(patched, 0xcc, sizeof(factory_pin_original));
    patched[0] = 0xe9;
    memcpy(patched + 1, &to_slot, sizeof(to_slot));
    unsigned char *thunk = (unsigned char *)slot;
    memcpy(thunk, factory_pin_prefix, sizeof(factory_pin_prefix));
    thunk[sizeof(factory_pin_prefix)] = 0xe9;
    memcpy(thunk + sizeof(factory_pin_prefix) + 1, &to_return, sizeof(to_return));
}

int main(void) {
    setup_patched();
    assert(factory_probe_factory_pin());

    setup_patched();
    memcpy(factory_image, original_image, sizeof(factory_image));
    factory_pin_test_provider = NULL;
    assert(factory_probe_factory_pin());
    factory_pin_test_provider = get_test_region;

    setup_patched();
    factory_image[10] ^= 1;
    assert(!factory_probe_factory_pin());

    setup_patched();
    factory_image[FACTORY_PIN_WINDOW_VA - FACTORY_PIN_VA + 1] ^= 1;
    assert(!factory_probe_factory_pin());

    setup_patched();
    test_region[FACTORY_PIN_CONSTANT_SIZE + FACTORY_PIN_WINDOW_INDEX * FACTORY_PIN_SLOT_SIZE] ^= 1;
    assert(!factory_probe_factory_pin());

    setup_patched();
    test_region[FACTORY_PIN_CONSTANT_SIZE + FACTORY_PIN_WINDOW_INDEX * FACTORY_PIN_SLOT_SIZE + 7] ^= 1;
    assert(!factory_probe_factory_pin());

    setup_patched();
    test_region[FACTORY_PIN_CONSTANT_SIZE + FACTORY_PIN_WINDOW_INDEX * FACTORY_PIN_SLOT_SIZE + 127] ^= 1;
    assert(!factory_probe_factory_pin());

    setup_patched();
    test_region[16] ^= 1;
    assert(!factory_probe_factory_pin());

    setup_patched();
    used[FACTORY_PIN_WINDOW_INDEX] = 8;
    assert(!factory_probe_factory_pin());

    setup_patched();
    region_is_rx = 0;
    assert(!factory_probe_factory_pin());

    setup_patched();
    provider_count--;
    assert(!factory_probe_factory_pin());

    setup_patched();
    provider_size--;
    assert(!factory_probe_factory_pin());
    return 0;
}

#define _GNU_SOURCE
#include <assert.h>
#include <limits.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FACTORY_PIN_TEST
#define GATE_PAGE 4096u
#define GETTER_START 0x1659010u
static alignas(4096) unsigned char getter_image[4479];
static alignas(4096) unsigned char test_region[0x20000];
static unsigned char original_image[4479];
static unsigned char used[765];
static int region_is_rx = 1;
static unsigned provider_count = 765;
static size_t provider_size = sizeof test_region;
#define LX(address) (getter_image + ((uintptr_t)(address) - GETTER_START))
static int gate_read_local(const void *source, void *target, size_t size) {
    memcpy(target, source, size);
    return 1;
}
static int gate_maps_find(uintptr_t address, char permissions[5], char *path, size_t capacity) {
    if (address < (uintptr_t)test_region || address >= (uintptr_t)test_region + sizeof test_region)
        return 0;
    strcpy(permissions, region_is_rx ? "r-xp" : "rwxp");
    if (capacity)
        path[0] = 0;
    return 1;
}
static int hash_ok(const void *data, size_t size, const char *expected) {
    (void)expected;
    return size == sizeof original_image && !memcmp(data, original_image, size);
}
#include "player_getter_preload_pin.h"

static unsigned get_test_region(const unsigned char **region, size_t *size, const unsigned char **lengths) {
    *region = test_region;
    *size = provider_size;
    *lengths = used;
    return provider_count;
}
factory_pin_region_fn factory_pin_test_provider = get_test_region;

static void setup_patched(void) {
    memset(getter_image, 0x55, sizeof getter_image);
    memset(test_region, 0xcc, sizeof test_region);
    memcpy(test_region, factory_pin_constants, sizeof factory_pin_constants);
    memset(used, 0, sizeof used);
    provider_count = FACTORY_PIN_WINDOW_COUNT;
    provider_size = sizeof test_region;
    region_is_rx = 1;
    for (unsigned index = 0; index < GETTER_PIN_WINDOW_COUNT; ++index) {
        const xrcp_window *window = &getter_pin_windows[index];
        unsigned slot_index = GETTER_PIN_FIRST_INDEX + index;
        unsigned char *site = LX(window->rva);
        unsigned char *slot = test_region + FACTORY_PIN_CONSTANT_SIZE + slot_index * FACTORY_PIN_SLOT_SIZE;
        unsigned used_length = 0;
        memcpy(site, getter_pin_original[index], window->length);
        assert(xrcp_emit_window(slot, window, site, site, test_region, GETTER_PIN_DROP, &used_length));
        used[slot_index] = (unsigned char)used_length;
    }
    memcpy(original_image, getter_image, sizeof original_image);
    for (unsigned index = 0; index < GETTER_PIN_WINDOW_COUNT; ++index) {
        const xrcp_window *window = &getter_pin_windows[index];
        unsigned slot_index = GETTER_PIN_FIRST_INDEX + index;
        unsigned char *site = LX(window->rva);
        unsigned char *slot = test_region + FACTORY_PIN_CONSTANT_SIZE + slot_index * FACTORY_PIN_SLOT_SIZE;
        int32_t displacement;
        assert(factory_pin_relative((uintptr_t)site + 5, (uintptr_t)slot, &displacement));
        memset(site, 0xcc, window->length);
        site[0] = 0xe9;
        memcpy(site + 1, &displacement, 4);
    }
}
int main(void) {
    (void)factory_probe_factory_pin;
    setup_patched();
    assert(player_transform_getter_pin());
    setup_patched();
    memcpy(getter_image, original_image, sizeof getter_image);
    factory_pin_test_provider = NULL;
    assert(player_transform_getter_pin());
    factory_pin_test_provider = get_test_region;
    setup_patched();
    getter_image[10] ^= 1;
    assert(!player_transform_getter_pin());
    for (unsigned index = 0; index < GETTER_PIN_WINDOW_COUNT; ++index) {
        const xrcp_window *window = &getter_pin_windows[index];
        size_t slot = FACTORY_PIN_CONSTANT_SIZE + (GETTER_PIN_FIRST_INDEX + index) * FACTORY_PIN_SLOT_SIZE;
        setup_patched();
        LX(window->rva)[1] ^= 1;
        assert(!player_transform_getter_pin());
        setup_patched();
        test_region[slot] ^= 1;
        assert(!player_transform_getter_pin());
        setup_patched();
        test_region[slot + used[GETTER_PIN_FIRST_INDEX + index] - 1] ^= 1;
        assert(!player_transform_getter_pin());
        setup_patched();
        test_region[slot + FACTORY_PIN_SLOT_SIZE - 1] ^= 1;
        assert(!player_transform_getter_pin());
        setup_patched();
        ++used[GETTER_PIN_FIRST_INDEX + index];
        assert(!player_transform_getter_pin());
    }
    setup_patched();
    test_region[16] ^= 1;
    assert(!player_transform_getter_pin());
    setup_patched();
    test_region[32] ^= 1;
    assert(!player_transform_getter_pin());
    setup_patched();
    region_is_rx = 0;
    assert(!player_transform_getter_pin());
    setup_patched();
    provider_count--;
    assert(!player_transform_getter_pin());
    setup_patched();
    provider_size--;
    assert(!player_transform_getter_pin());
    setup_patched();
    factory_pin_test_provider = NULL;
    assert(!player_transform_getter_pin());
    puts("four-window transform getter original-view pin PASS");
}

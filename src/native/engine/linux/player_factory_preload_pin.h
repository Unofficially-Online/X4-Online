#ifndef X4_LINUX_PLAYER_FACTORY_PRELOAD_PIN_H
#define X4_LINUX_PLAYER_FACTORY_PRELOAD_PIN_H

/* Original-view check for the one known exact-reciprocal patch inside the
 * pinned Player factory. Include after gate_platform and SHA helpers. A live
 * patch is normalized only after its provider, permissions, site, complete
 * trampoline and both jump destinations have been verified. */
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#define FACTORY_PIN_VA 0x119bd70u
#define FACTORY_PIN_LENGTH 24552u
#define FACTORY_PIN_WINDOW_VA 0x119d112u
#define FACTORY_PIN_WINDOW_INDEX 133u
#define FACTORY_PIN_REGION_SIZE 0x20000u
#define FACTORY_PIN_WINDOW_COUNT 765u
#define FACTORY_PIN_SLOT_SIZE 128u
#define FACTORY_PIN_CONSTANT_SIZE 64u

static const char factory_pin_hash[] =
    "4d9b93a1b51872447a36c0f43e05caa2099d2b7c5a85bb13798ea8dae45fb49b";
static const unsigned char factory_pin_original[8] =
    {0xf3, 0x0f, 0x52, 0xc0, 0xf3, 0x0f, 0x53, 0xc0};
static const unsigned char factory_pin_prefix[4] = {0xf3, 0x0f, 0x51, 0xc0};
static const unsigned char factory_pin_constants[32] = {
    0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0, 0, 0x80, 0x3f, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

typedef unsigned (*factory_pin_region_fn)(const unsigned char **, size_t *,
                                          const unsigned char **);

static int factory_pin_relative(uintptr_t source, uintptr_t destination, int32_t *relative) {
    if (source > INT64_MAX || destination > INT64_MAX) {
        return 0;
    }
    int64_t distance = (int64_t)destination - (int64_t)source;
    if (distance < INT32_MIN || distance > INT32_MAX) {
        return 0;
    }
    *relative = (int32_t)distance;
    return 1;
}

static factory_pin_region_fn factory_pin_provider(void) {
#ifdef FACTORY_PIN_TEST
    extern factory_pin_region_fn factory_pin_test_provider;
    if (factory_pin_test_provider) {
        return factory_pin_test_provider;
    }
#endif
    void *region = dlsym(RTLD_DEFAULT, "x4mp_exact_reciprocal_region");
    void *info = dlsym(RTLD_DEFAULT, "x4mp_portable_math_info");
    Dl_info region_owner;
    Dl_info info_owner;
    if (!region || !info || !dladdr(region, &region_owner) || !dladdr(info, &info_owner) ||
        region_owner.dli_fbase != info_owner.dli_fbase || !region_owner.dli_fname) {
        return NULL;
    }
    const char *name = strrchr(region_owner.dli_fname, '/');
    name = name ? name + 1 : region_owner.dli_fname;
    if (strcmp(name, "libx4mp_portable_math.so")) {
        return NULL;
    }
    return (factory_pin_region_fn)region;
}

static int factory_pin_normalize(unsigned char *factory, const unsigned char *region,
                                 size_t region_size, const unsigned char *used,
                                 unsigned count) {
    if (!region || !used || region_size != FACTORY_PIN_REGION_SIZE ||
        count != FACTORY_PIN_WINDOW_COUNT) {
        return 0;
    }
    uintptr_t base = (uintptr_t)region;
    uintptr_t used_address = (uintptr_t)used;
    if (base > UINTPTR_MAX - region_size || base > INT64_MAX - region_size ||
        (base & (GATE_PAGE - 1)) ||
        used_address > UINTPTR_MAX - FACTORY_PIN_WINDOW_INDEX) {
        return 0;
    }
    char permissions[5];
    char path[PATH_MAX];
    for (size_t offset = 0; offset < region_size; offset += GATE_PAGE) {
        if (!gate_maps_find(base + offset, permissions, path, sizeof(path)) ||
            strcmp(permissions, "r-xp") || path[0]) {
            return 0;
        }
    }
    unsigned char constants[FACTORY_PIN_CONSTANT_SIZE];
    if (!gate_read_local(region, constants, sizeof(constants)) ||
        memcmp(constants, factory_pin_constants, sizeof(factory_pin_constants))) {
        return 0;
    }
    for (unsigned index = sizeof(factory_pin_constants); index < sizeof(constants); ++index) {
        if (constants[index] != 0xcc) {
            return 0;
        }
    }
    unsigned char used_length;
    if (!gate_read_local((const void *)(used_address + FACTORY_PIN_WINDOW_INDEX),
                         &used_length, 1) || used_length != sizeof(factory_pin_prefix) + 5) {
        return 0;
    }
    uintptr_t site = (uintptr_t)LX(FACTORY_PIN_WINDOW_VA);
    uintptr_t slot = base + FACTORY_PIN_CONSTANT_SIZE +
        FACTORY_PIN_WINDOW_INDEX * FACTORY_PIN_SLOT_SIZE;
    int32_t to_slot;
    int32_t to_return;
    if (!factory_pin_relative(site + 5, slot, &to_slot) ||
        !factory_pin_relative(slot + sizeof(factory_pin_prefix) + 5,
                              site + sizeof(factory_pin_original), &to_return)) {
        return 0;
    }
    unsigned char expected_site[sizeof(factory_pin_original)];
    memset(expected_site, 0xcc, sizeof(expected_site));
    expected_site[0] = 0xe9;
    memcpy(expected_site + 1, &to_slot, sizeof(to_slot));
    size_t factory_offset = FACTORY_PIN_WINDOW_VA - FACTORY_PIN_VA;
    if (memcmp(factory + factory_offset, expected_site, sizeof(expected_site))) {
        return 0;
    }
    unsigned char expected_slot[FACTORY_PIN_SLOT_SIZE];
    unsigned char actual_slot[FACTORY_PIN_SLOT_SIZE];
    memset(expected_slot, 0xcc, sizeof(expected_slot));
    memcpy(expected_slot, factory_pin_prefix, sizeof(factory_pin_prefix));
    expected_slot[sizeof(factory_pin_prefix)] = 0xe9;
    memcpy(expected_slot + sizeof(factory_pin_prefix) + 1, &to_return, sizeof(to_return));
    if (!gate_read_local((const void *)slot, actual_slot, sizeof(actual_slot)) ||
        memcmp(actual_slot, expected_slot, sizeof(actual_slot))) {
        return 0;
    }
    memcpy(factory + factory_offset, factory_pin_original, sizeof(factory_pin_original));
    return 1;
}

static int factory_probe_factory_pin(void) {
    unsigned char factory[FACTORY_PIN_LENGTH];
    if (!gate_read_local(LX(FACTORY_PIN_VA), factory, sizeof(factory))) {
        return 0;
    }
    if (hash_ok(factory, sizeof(factory), factory_pin_hash)) {
        return 1;
    }
    factory_pin_region_fn get_region = factory_pin_provider();
    if (!get_region) {
        return 0;
    }
    const unsigned char *region = NULL;
    const unsigned char *used = NULL;
    size_t size = 0;
    unsigned count = get_region(&region, &size, &used);
    return factory_pin_normalize(factory, region, size, used, count) &&
        hash_ok(factory, sizeof(factory), factory_pin_hash);
}
#endif

#ifndef X4_GATE_PLAYER_SEMAPHORE_ORIGINAL_VIEW_H
#define X4_GATE_PLAYER_SEMAPHORE_ORIGINAL_VIEW_H

/* The launcher must pin the observer binary's size and SHA in its frozen
 * receipt. The provider cannot supply its own expected digest. With observer
 * disabled, native bytes are checked using the unchanged original hash path. */
#include <dlfcn.h>
#include <link.h>
#include <sys/stat.h>
#include "player_semaphore_original_view.h"
#include "player_semaphore_maps_transaction.h"

static int player_semaphore_maps_lookup(player_semaphore_maps_transaction *view,
        uintptr_t address, char permissions[5], char *path, size_t capacity) {
    return view ? player_semaphore_maps_find(view, address, permissions, path, capacity) :
        gate_maps_find(address, permissions, path, capacity);
}

static int player_semaphore_hex(const unsigned char digest[32], const char *expected) {
    char actual[65];
    hex32(digest, actual);
    return expected && strlen(expected) == 64 && !strcmp(actual, expected);
}

typedef struct {
    uintptr_t module;
    int fd;
    uint64_t file_size;
    int matched;
    int valid;
    player_semaphore_maps_transaction *maps;
} player_semaphore_module_check;

/* Authenticate all file-backed executable bytes, not just a provider-supplied
 * wrapper address. The file was independently hashed using this same open fd. */
static int player_semaphore_module_segment(struct dl_phdr_info *module, size_t size, void *opaque) {
    (void)size;
    player_semaphore_module_check *check = opaque;
    if ((uintptr_t)module->dlpi_addr != check->module) return 0;
    check->matched = 1;
    unsigned executable_segments = 0;
    for (ElfW(Half) i = 0; i < module->dlpi_phnum; ++i) {
        const ElfW(Phdr) *segment = &module->dlpi_phdr[i];
        if (segment->p_type != PT_LOAD || !(segment->p_flags & PF_X)) continue;
        ++executable_segments;
        if (segment->p_memsz != segment->p_filesz || segment->p_filesz > 16u * 1024u * 1024u ||
            segment->p_offset > check->file_size || segment->p_filesz > check->file_size - segment->p_offset ||
            segment->p_vaddr > UINTPTR_MAX - check->module) return 1;
        uintptr_t address = check->module + segment->p_vaddr;
        char permissions[5];
        if (!segment->p_filesz || address > UINTPTR_MAX - (segment->p_filesz - 1) ||
            !player_semaphore_maps_lookup(check->maps, address, permissions, NULL, 0) || strcmp(permissions, "r-xp") ||
            !player_semaphore_maps_lookup(check->maps, address + segment->p_filesz - 1, permissions, NULL, 0) ||
            strcmp(permissions, "r-xp")) return 1;
        unsigned char file[4096], memory[4096];
        for (size_t offset = 0; offset < segment->p_filesz;) {
            size_t length = segment->p_filesz - offset;
            if (length > sizeof file) length = sizeof file;
            if (pread(check->fd, file, length, (off_t)(segment->p_offset + offset)) != (ssize_t)length ||
                !gate_read_local((void *)(address + offset), memory, length) || memcmp(file, memory, length))
                return 1;
            offset += length;
        }
    }
    check->valid = executable_segments != 0;
    return 1;
}

static int player_semaphore_pinned_provider_maps(void *symbol, Dl_info *owner,
        player_semaphore_maps_transaction *maps) {
    const char *sha = getenv("X4MP_SEMAPHORE_OBSERVER_SHA256");
    const char *size_text = getenv("X4MP_SEMAPHORE_OBSERVER_SIZE");
    if (!symbol || !sha || !size_text || !*size_text || !dladdr(symbol, owner) ||
        !owner->dli_fbase || !owner->dli_fname) return 0;
    const char *name = strrchr(owner->dli_fname, '/');
    name = name ? name + 1 : owner->dli_fname;
    if (strcmp(name, "libx4mp_semaphore_observer.so")) return 0;
    uint64_t expected_size = 0;
    for (const char *p = size_text; *p; ++p) {
        if (*p < '0' || *p > '9' || expected_size > (UINT64_MAX - (unsigned)(*p - '0')) / 10) return 0;
        expected_size = expected_size * 10 + (unsigned)(*p - '0');
    }
    if (!expected_size || expected_size > 16u * 1024u * 1024u) return 0;
    int fd = open(owner->dli_fname, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    struct stat status;
    int valid = !fstat(fd, &status) && S_ISREG(status.st_mode) && (uint64_t)status.st_size == expected_size;
    lockstep_sha256_ctx hash;
    lockstep_sha256_init(&hash);
    unsigned char buffer[4096], digest[32];
    ssize_t length = 0;
    if (valid) {
        while ((length = read(fd, buffer, sizeof buffer)) > 0)
            lockstep_sha256_update(&hash, buffer, (size_t)length);
        valid = length == 0;
    }
    if (valid) {
        lockstep_sha256_final(&hash, digest);
        valid = player_semaphore_hex(digest, sha);
    }
    if (valid) {
        player_semaphore_module_check check = {(uintptr_t)owner->dli_fbase, fd, expected_size, 0, 0, maps};
        dl_iterate_phdr(player_semaphore_module_segment, &check);
        valid = check.matched && check.valid;
    }
    close(fd);
    return valid;
}

static int player_semaphore_pinned_provider(void *symbol, Dl_info *owner) {
    return player_semaphore_pinned_provider_maps(symbol, owner, NULL);
}

typedef struct { uintptr_t module; player_semaphore_maps_transaction *maps; } player_semaphore_pin_context;
static int player_semaphore_read(void *context, uintptr_t address, void *out, size_t size) {
    (void)context;
    return gate_read_local((void *)address, out, size);
}
static int player_semaphore_owned(void *opaque, uintptr_t page, uintptr_t wrapper, uintptr_t original_slot) {
    player_semaphore_pin_context *context = opaque;
    Dl_info owner, slot_owner;
    char permissions[5], path[PATH_MAX];
    return dladdr((void *)wrapper, &owner) && (uintptr_t)owner.dli_fbase == context->module &&
        dladdr((void *)original_slot, &slot_owner) && (uintptr_t)slot_owner.dli_fbase == context->module &&
        player_semaphore_maps_lookup(context->maps, original_slot, permissions, NULL, 0) && !strcmp(permissions, "rw-p") &&
        player_semaphore_maps_lookup(context->maps, wrapper, permissions, NULL, 0) && !strcmp(permissions, "r-xp") &&
        player_semaphore_maps_lookup(context->maps, page, permissions, path, sizeof path) && !strcmp(permissions, "r-xp") && !path[0] &&
        player_semaphore_maps_lookup(context->maps, page + PLAYER_SEM_PAGE_SIZE - 1, permissions, path, sizeof path) &&
        !strcmp(permissions, "r-xp") && !path[0];
}

/* One original range from an already validated transaction. Never normalize
 * arbitrary changed bytes: require the exact authenticated live detour first.
 * Keep raw bytes for the final consistency check, including the whole tail. */
static int player_semaphore_pin_range(const player_semaphore_observer_info *info,
        unsigned index, size_t length, const char *sha, unsigned char raw[512]) {
    if (index >= PLAYER_SEM_OBSERVER_SITES || length < 9 || length > 512 || !sha ||
        !gate_read_local((void *)player_semaphore_site_addresses[index], raw, length)) return 0;
    unsigned char expected[8] = {0xe9,0,0,0,0,0x90,0x90,0x90}, bytes[512];
    int32_t relative = (int32_t)((int64_t)info->sites[index].page -
        (int64_t)(player_semaphore_site_addresses[index] + 5));
    memcpy(expected + 1, &relative, 4);
    if (memcmp(raw, expected, sizeof expected)) return 0;
    memcpy(bytes, raw, length);
    memcpy(bytes, player_semaphore_original[index], sizeof expected);
    return hash_ok(bytes, length, sha);
}
static int player_semaphore_pin_range_unchanged(unsigned index, size_t length, const unsigned char *raw) {
    unsigned char now[512];
    return gate_read_local((void *)player_semaphore_site_addresses[index], now, length) &&
        !memcmp(now, raw, length);
}
static int player_semaphore_original_pin_transaction(uintptr_t site, size_t length, const char *sha,
        player_semaphore_maps_transaction *maps) {
    void *symbol = dlsym(RTLD_DEFAULT, "x4mp_semaphore_observer_info");
    Dl_info owner = {0};
    if (!player_semaphore_pinned_provider_maps(symbol, &owner, maps)) return 0;
    player_semaphore_observer_info info = {0};
    if (((unsigned (*)(player_semaphore_observer_info *))symbol)(&info) != PLAYER_SEM_OBSERVER_VERSION)
        return 0;
    static const char *const wrapper_names[PLAYER_SEM_OBSERVER_SITES] = {
        "x4mp_semaphore_observer_constructor", "x4mp_semaphore_observer_post",
        "x4mp_semaphore_observer_wait", "x4mp_semaphore_observer_timed_wait"
    };
    uintptr_t slots = (uintptr_t)dlsym(RTLD_DEFAULT, "x4mp_semaphore_observer_originals");
    if (!slots || slots > UINTPTR_MAX - PLAYER_SEM_OBSERVER_SITES * sizeof(uintptr_t)) return 0;
    for (unsigned i = 0; i < PLAYER_SEM_OBSERVER_SITES; ++i) {
        void *wrapper = dlsym(RTLD_DEFAULT, wrapper_names[i]);
        if (!wrapper || (uintptr_t)wrapper != info.sites[i].wrapper ||
            info.sites[i].original_slot != slots + i * sizeof(uintptr_t)) return 0;
    }
    player_semaphore_pin_context context = {(uintptr_t)owner.dli_fbase, maps};
    if (!player_semaphore_observer_validate(&info, player_semaphore_read, player_semaphore_owned, &context)) return 0;
    static const size_t full_lengths[PLAYER_SEM_OBSERVER_SITES] = {0x150, 0xe0, 0xd0, 0x170};
    static const char *const full_hashes[PLAYER_SEM_OBSERVER_SITES] = {
        "67068087dfa9e4c1de02561561448ca0ffd47327a46108c2015ce5fea7f30ab2",
        "853e6d16983bd5a42c26efaf075b1a47efaf4cb82ecb4feb32c1ee183b716078",
        "651efec3dd008483223ea0d44dacd25c7a30e6273062954f1c1d621f66a09c52",
        "f6f84544f89af7bfbf5a0cc43f51289a1bcc60ef92d0191682f247344ecec069"
    };
    unsigned index;
    for (index = 0; index < PLAYER_SEM_OBSERVER_SITES; ++index)
        if (site == player_semaphore_site_addresses[index]) break;
    if (index == PLAYER_SEM_OBSERVER_SITES) return 0;
    unsigned char raw[PLAYER_SEM_OBSERVER_SITES][512], requested[512];
    for (unsigned i = 0; i < PLAYER_SEM_OBSERVER_SITES; ++i)
        if (!player_semaphore_pin_range(&info, i, full_lengths[i], full_hashes[i], raw[i])) return 0;
    if (!player_semaphore_pin_range(&info, index, length, sha, requested)) return 0;
    /* No cached success: reauthenticate file and executable memory, provider
     * metadata, original slots, every detour page, and all hashed live ranges.
     * Caller then reparses maps and checks every mapping used in this call. */
    Dl_info final_owner = {0}; player_semaphore_observer_info final_info = {0};
    if (!player_semaphore_pinned_provider_maps(symbol, &final_owner, maps) ||
        final_owner.dli_fbase != owner.dli_fbase ||
        ((unsigned (*)(player_semaphore_observer_info *))symbol)(&final_info) != PLAYER_SEM_OBSERVER_VERSION ||
        memcmp(&info, &final_info, sizeof info) ||
        !player_semaphore_observer_validate(&info, player_semaphore_read, player_semaphore_owned, &context)) return 0;
    for (unsigned i = 0; i < PLAYER_SEM_OBSERVER_SITES; ++i)
        if (!player_semaphore_pin_range_unchanged(i, full_lengths[i], raw[i])) return 0;
    return player_semaphore_pin_range_unchanged(index, length, requested);
}
static int player_semaphore_original_pin(uintptr_t site, size_t length, const char *sha) {
    /* Preserve the exact original check when no observer has changed bytes. */
    const char *enabled = getenv("X4MP_SEMAPHORE_OBSERVER");
    if (!enabled) return hash_ok(LX(site), length, sha);
    if (strcmp(enabled, "1")) return 0;
    player_semaphore_maps_transaction maps;
    if (!player_semaphore_maps_begin(&maps)) return 0;
    int valid = player_semaphore_original_pin_transaction(site, length, sha, &maps);
    if (valid) valid = player_semaphore_maps_commit(&maps);
    free(maps.rows);
    return valid;
}
#endif

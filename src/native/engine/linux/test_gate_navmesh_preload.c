/* Offline compatibility test: maps a copy of the pinned ELF at its native
 * addresses and invokes the real preload's installer. No X4 code is run. */
#define main navmesh_unused_gate_test_main
#include "test_gate_linux.c"
#undef main

static void *map_copied_elf(const char *path) {
    size_t size = FAKE_END - LX_IMAGE_BASE;
    void *memory = mmap((void *)LX_IMAGE_BASE, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(memory == (void *)LX_IMAGE_BASE);
    image = memory;
    FILE *file = fopen(path, "rb");
    assert(file);
    unsigned char header[64 + 56 * 12];
    assert(fread(header, 1, sizeof(header), file) == sizeof(header));
    for (unsigned i = 0; i < 12; ++i) {
        const unsigned char *entry = header + 64 + 56 * i;
        if (rd32(entry) != 1) continue;
        uint64_t offset = rd64(entry + 8), address = rd64(entry + 16), length = rd64(entry + 32);
        assert(address >= LX_IMAGE_BASE && address + length <= FAKE_END);
        assert(!fseeko(file, (off_t)offset, SEEK_SET));
        assert(fread(LX(address), 1, length, file) == length);
    }
    fclose(file);
    return memory;
}

static void flip_test_byte(void *address) {
    uintptr_t page = (uintptr_t)address & ~(uintptr_t)(GATE_PAGE - 1);
    assert(!mprotect((void *)page, GATE_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC));
    *(unsigned char *)address ^= 1;
    assert(!mprotect((void *)page, GATE_PAGE, PROT_READ | PROT_EXEC));
}

static void test_relocated_region(const unsigned char *original_region, const unsigned char *used) {
    unsigned char *other = MAP_FAILED;
    for (uintptr_t address = 0x100000u; address + NAVMESH_RECIPROCAL_REGION_SIZE <= 0x3e0000u;
         address += NAVMESH_RECIPROCAL_REGION_SIZE) {
        other = mmap((void *)address, NAVMESH_RECIPROCAL_REGION_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (other != MAP_FAILED) break;
    }
    assert(other != MAP_FAILED && other != original_region);
    memcpy(other, original_region, NAVMESH_RECIPROCAL_REGION_SIZE);
    unsigned char worker[NAVMESH_WORKER_SIZE];
    memcpy(worker, LX(NAVMESH_WORKER_START), sizeof(worker));
    for (unsigned i = 0; i < 3; ++i) {
        const navmesh_reciprocal_window *window = &navmesh_reciprocal_windows[i];
        uintptr_t slot = (uintptr_t)other + NAVMESH_RECIPROCAL_CONSTANTS + window->index * NAVMESH_RECIPROCAL_SLOT;
        uintptr_t site = (uintptr_t)LX(window->address);
        int32_t forward, back;
        assert(navmesh_post_rel32(site + 5, slot, &forward));
        assert(navmesh_post_rel32(slot + window->prefix_length + 5, site + window->length, &back));
        memcpy(worker + window->address - NAVMESH_WORKER_START + 1, &forward, sizeof(forward));
        memcpy((void *)(slot + window->prefix_length + 1), &back, sizeof(back));
    }
    assert(!mprotect(other, NAVMESH_RECIPROCAL_REGION_SIZE, PROT_READ | PROT_EXEC));
    assert(navmesh_post_normalize_reciprocal(worker, other, NAVMESH_RECIPROCAL_REGION_SIZE,
                                             used, NAVMESH_RECIPROCAL_WINDOWS));
    assert(hash_ok(worker, sizeof(worker), navmesh_worker_hash));
    assert(!munmap(other, NAVMESH_RECIPROCAL_REGION_SIZE));
}

int main(int argc, char **argv) {
    assert(argc == 3);
    unsetenv("X4MP_PORTABLE_DECIMAL");
    unsetenv("X4MP_EXACT_RECIPROCAL");
    map_copied_elf(argv[1]);
    assert(navmesh_post_worker_pin()); /* Exact, unpatched ELF. */
    void *library = dlopen(argv[2], RTLD_NOW | RTLD_GLOBAL);
    assert(library);
    unsigned (*install)(void) = (unsigned (*)(void))dlsym(library, "x4mp_exact_reciprocal_install");
    navmesh_reciprocal_region_fn get_region = navmesh_post_region_export();
    void *info = dlsym(library, "x4mp_portable_math_info");
    assert(navmesh_post_provider_ok((void *)get_region, info));
    assert(!navmesh_post_provider_ok((void *)get_region, (void *)printf));
    assert(!navmesh_post_provider_ok((void *)printf, (void *)printf));
    assert(install && get_region && install() == NAVMESH_RECIPROCAL_WINDOWS);
    assert(navmesh_post_worker_pin()); /* Actual preload, production dlsym path. */
    const unsigned char *region = NULL, *used = NULL;
    size_t size = 0;
    assert(get_region(&region, &size, &used) == NAVMESH_RECIPROCAL_WINDOWS);
    assert(region && used && size == NAVMESH_RECIPROCAL_REGION_SIZE);
    test_relocated_region(region, used);
    /* A readable but writable interior slot page is not an owned RX region. */
    void *slot_page = (void *)(((uintptr_t)region + 0x3440u) & ~(uintptr_t)(GATE_PAGE - 1));
    assert(!mprotect(slot_page, GATE_PAGE, PROT_READ | PROT_WRITE));
    assert(!navmesh_post_worker_pin());
    assert(!mprotect(slot_page, GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(navmesh_post_worker_pin());

    unsigned char worker[NAVMESH_WORKER_SIZE], saved[NAVMESH_WORKER_SIZE];
    memcpy(saved, LX(NAVMESH_WORKER_START), sizeof(saved));
    memcpy(worker, saved, sizeof(worker));
    assert(!navmesh_post_normalize_reciprocal(worker, NULL, size, used, NAVMESH_RECIPROCAL_WINDOWS));
    assert(!navmesh_post_normalize_reciprocal(worker, region, size - 1, used, NAVMESH_RECIPROCAL_WINDOWS));
    assert(!navmesh_post_normalize_reciprocal(worker, region + 1, size, used, NAVMESH_RECIPROCAL_WINDOWS));
    assert(!navmesh_post_normalize_reciprocal(worker, region + GATE_PAGE, size, used, NAVMESH_RECIPROCAL_WINDOWS));
    assert(!navmesh_post_normalize_reciprocal(worker, region, size, (const unsigned char *)(UINTPTR_MAX - 1), NAVMESH_RECIPROCAL_WINDOWS));
    assert(!navmesh_post_normalize_reciprocal(worker, region, size, used, NAVMESH_RECIPROCAL_WINDOWS - 1));
    unsigned char used_copy[NAVMESH_RECIPROCAL_WINDOWS];
    memcpy(used_copy, used, sizeof(used_copy));
    for (unsigned i = 0; i < 3; ++i) {
        const navmesh_reciprocal_window *window = &navmesh_reciprocal_windows[i];
        memcpy(worker, saved, sizeof(worker));
        used_copy[window->index] ^= 1;
        assert(!navmesh_post_normalize_reciprocal(worker, region, size, used_copy, NAVMESH_RECIPROCAL_WINDOWS));
        used_copy[window->index] ^= 1;
        /* Changed jump or int3 padding and a mixed original/patched state. */
        worker[window->address - NAVMESH_WORKER_START + 1] ^= 1;
        assert(!navmesh_post_normalize_reciprocal(worker, region, size, used, NAVMESH_RECIPROCAL_WINDOWS));
        memcpy(worker, saved, sizeof(worker));
        worker[window->address - NAVMESH_WORKER_START + window->length - 1] ^= 1;
        assert(!navmesh_post_normalize_reciprocal(worker, region, size, used, NAVMESH_RECIPROCAL_WINDOWS));
        memcpy(worker, saved, sizeof(worker));
        memcpy(worker + window->address - NAVMESH_WORKER_START, window->original, window->length);
        assert(!navmesh_post_normalize_reciprocal(worker, region, size, used, NAVMESH_RECIPROCAL_WINDOWS));
        /* Every selected slot must be exact, including its return jump/padding. */
        unsigned char *slot = (unsigned char *)region + NAVMESH_RECIPROCAL_CONSTANTS + window->index * NAVMESH_RECIPROCAL_SLOT;
        for (unsigned j = 0; j < 3; ++j) {
            size_t offset = j == 0 ? 0 : (j == 1 ? window->prefix_length + 1 : NAVMESH_RECIPROCAL_SLOT - 1);
            flip_test_byte(slot + offset);
            assert(!navmesh_post_worker_pin());
            flip_test_byte(slot + offset);
        }
    }
    /* Unrelated changes remain visible to the final full worker hash. */
    flip_test_byte(LX(0x10a5e40u));
    assert(!navmesh_post_worker_pin());
    flip_test_byte(LX(0x10a5e40u));
    assert(navmesh_post_worker_pin());
    /* Exercise the actual observer install/restore while the real preload's
     * three verified worker patches are present on the copied image. */
    stats.active = 1;
    boot_mode = 1;
    boot_phase = 0;
    ui_thread = GetCurrentThreadId();
    assert(navmesh_post_arm(1, 60));
    assert(navmesh_post_owned() && navmesh_post_check());
    navmesh_post_restore();
    assert(navmesh_post_restored && !navmesh_post_installed && !navmesh_post_cleanup_uncertain);
    printf("navmesh real-preload copied-ELF original, patched, relocated and corrupt variants PASS\n");
    return 0;
}

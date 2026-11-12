/* Exercise actual CAS and page permissions on the synthetic image. Never call
 * a game function: the fake call bytes are data throughout this test. */
static void test_player_factory_patch(void) {
    unsigned char *address = LX(FACTORY_PROBE_WORD_VA);
    void *page = (void *)((uintptr_t)address & ~(uintptr_t)(GATE_PAGE - 1));
    uint64_t saved = 0;
    memcpy(&saved, address, sizeof(saved));
    const uint64_t original = FACTORY_PROBE_ORIGINAL_WORD;
    const uint64_t patched = original ^ (UINT64_C(1) << 16);
    const uint64_t foreign = original ^ (UINT64_C(1) << 24);
    char permissions[5];
    assert(!factory_probe_patch_owned && !factory_probe_code_permission_pending);
    factory_probe_patched_word = patched;
    memcpy(address, &original, sizeof(original));
    assert(mprotect(page, GATE_PAGE, PROT_READ | PROT_EXEC) == 0);

    assert(factory_probe_swap(original, patched));
    assert(factory_probe_patch_owned && !factory_probe_code_permission_pending);
    assert(factory_probe_word(patched));
    assert(gate_maps_find((uintptr_t)address, permissions, NULL, 0));
    assert(strcmp(permissions, "r-xp") == 0);

    /* A failed compare must still close the writable page and keep ownership. */
    assert(!factory_probe_swap(original, foreign));
    assert(factory_probe_word(patched) && factory_probe_patch_owned);
    assert(!factory_probe_code_permission_pending);
    assert(gate_maps_find((uintptr_t)address, permissions, NULL, 0));
    assert(strcmp(permissions, "r-xp") == 0);

    /* Simulate another writer. Cleanup must not overwrite its bytes. */
    assert(mprotect(page, GATE_PAGE, PROT_READ | PROT_WRITE) == 0);
    memcpy(address, &foreign, sizeof(foreign));
    assert(mprotect(page, GATE_PAGE, PROT_READ | PROT_EXEC) == 0);
    assert(!factory_probe_swap(patched, original));
    assert(factory_probe_word(foreign) && factory_probe_patch_owned);
    assert(!factory_probe_clean());

    /* Repair only the synthetic fixture, then discharge our original patch. */
    assert(mprotect(page, GATE_PAGE, PROT_READ | PROT_WRITE) == 0);
    memcpy(address, &patched, sizeof(patched));
    assert(mprotect(page, GATE_PAGE, PROT_READ | PROT_EXEC) == 0);
    assert(factory_probe_swap(patched, original));
    assert(!factory_probe_patch_owned && !factory_probe_code_permission_pending);
    assert(factory_probe_word(original) && factory_probe_clean());
    assert(gate_maps_find((uintptr_t)address, permissions, NULL, 0));
    assert(strcmp(permissions, "r-xp") == 0);

    assert(mprotect(page, GATE_PAGE, PROT_READ | PROT_WRITE) == 0);
    memcpy(address, &saved, sizeof(saved));
    factory_probe_patched_word = 0;
    puts("player factory patch: CAS, RX restoration and foreign-byte refusal PASS");
}

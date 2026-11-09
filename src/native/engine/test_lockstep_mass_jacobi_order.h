/* Pinned PE read-only mapping tests. No Jacobi/game instruction is executed. */
static void test_mass_jacobi_order_native_image(const char *path) {
    unsigned char *saved_image = image;
    image = test_pmath_map(path);
    const uint32_t saved_active = stats.active;
    const uint64_t saved_issued = stats.issued;
    const DWORD saved_ui = ui_thread;
    const int saved_boot = boot_mode;
    const int saved_phase = boot_phase;
    assert(mass_jacobi_pins() && mass_jacobi_state() == 0);
    image[MASS_JACOBI_FUNCTION_RVA + 7] ^= 1;
    assert(!mass_jacobi_pins());
    image[MASS_JACOBI_FUNCTION_RVA + 7] ^= 1;
    image[MASS_JACOBI_BLOCK_RVA] ^= 1;
    assert(!mass_jacobi_pins());
    image[MASS_JACOBI_BLOCK_RVA] ^= 1;
    DWORD previous;
    DWORD ignored;
    assert(VirtualProtect(image + MASS_JACOBI_PATCH_RVA, MASS_JACOBI_PATCH_SIZE, PAGE_EXECUTE_READ, &previous));
    stats.active = 0;
    stats.issued = 0;
    boot_mode = 1;
    boot_phase = 0;
    ui_thread = GetCurrentThreadId();
    assert(!mass_jacobi_arm());
    stats.active = 1;
    ui_thread = GetCurrentThreadId() + 1;
    assert(!mass_jacobi_arm());
    ui_thread = GetCurrentThreadId();
    stats.issued = 1;
    assert(!mass_jacobi_arm());
    stats.issued = 0;
    boot_phase = 1;
    assert(!mass_jacobi_arm());
    boot_phase = 0;
    assert(mass_jacobi_arm() && mass_jacobi_owned() && mass_jacobi_check() && mass_jacobi_state() == 1);
    assert(mass_jacobi_pins()); /* The full function pin sees original bytes. */
    unsigned char original_view[MASS_JACOBI_BLOCK_SIZE];
    assert(read_local(image + MASS_JACOBI_BLOCK_RVA, original_view, sizeof(original_view)) &&
           !memcmp(original_view, mass_jacobi_original, sizeof(original_view)));
    assert(!memcmp(image + MASS_JACOBI_BLOCK_RVA, mass_jacobi_reordered, MASS_JACOBI_BLOCK_SIZE));
    stats.issued = 2;
    boot_phase = 3;
    const unsigned attempts = mass_jacobi_attempts;
    assert(mass_jacobi_arm() && mass_jacobi_attempts == attempts); /* UI reload. */
    assert(VirtualProtect(image + MASS_JACOBI_PATCH_RVA, MASS_JACOBI_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &ignored));
    image[MASS_JACOBI_PATCH_RVA + 4] ^= 1; /* The middle instruction is checked too. */
    assert(!mass_jacobi_check());
    mass_jacobi_restore();
    assert(mass_jacobi_installed && !mass_jacobi_restored && !strcmp(mass_jacobi_stage, "restore_foreign"));
    image[MASS_JACOBI_PATCH_RVA + 4] ^= 1;
    assert(VirtualProtect(image + MASS_JACOBI_PATCH_RVA, MASS_JACOBI_PATCH_SIZE, PAGE_EXECUTE_READ, &ignored));
    mass_jacobi_restore();
    assert(mass_jacobi_restored && mass_jacobi_clean() && mass_jacobi_state() == 0 && mass_jacobi_pins());
    assert(VirtualProtect(image + MASS_JACOBI_PATCH_RVA, MASS_JACOBI_PATCH_SIZE, previous, &ignored));
    VirtualFree(image, 0, MEM_RELEASE);
    image = saved_image;
    stats.active = saved_active;
    stats.issued = saved_issued;
    ui_thread = saved_ui;
    boot_mode = saved_boot;
    boot_phase = saved_phase;
    mass_jacobi_restored = 0;
    mass_jacobi_failures = 0;
    mass_jacobi_attempts = 0;
    mass_jacobi_busy = 0;
    mass_jacobi_stage = "idle";
    printf("mass Jacobi order: full function/block pins, state refusals, original-byte view, reload, "
           "foreign middle instruction refused, RX restoration PASS\n");
}

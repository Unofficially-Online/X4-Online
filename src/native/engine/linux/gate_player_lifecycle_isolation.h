#ifndef X4_GATE_PLAYER_LIFECYCLE_ISOLATION_H
#define X4_GATE_PLAYER_LIFECYCLE_ISOLATION_H

/* A second, explicitly selected disposable experiment. This restores only the
 * measured canonical globals. It does not establish rollback of every native
 * lifecycle effect or exclude background readers; the process must still exit.
 * Include inside gate_player_lifecycle_discard.h after its read/log helpers. */
static pcb_slot lifecycle_isolation_zone_key;
static cre_token lifecycle_isolation_token;
static uintptr_t lifecycle_isolation_vtable[81]; /* Two ABI words + 79 slots. */
static input_context_census_sample lifecycle_isolation_contexts;
static uint64_t lifecycle_isolation_thread;
static unsigned lifecycle_isolation_bind_active;

static int lifecycle_isolation_authenticate(void *context, const void *controller,
                                           cre_phase phase) {
    (void)context;
    return (phase == CRE_BIND || phase == CRE_DESTRUCT) &&
           (uintptr_t)controller == lifecycle_discard_controller &&
           lifecycle_isolation_thread == (uint64_t)GetCurrentThreadId() &&
           factory_probe_joined_boundary() &&
           factory_probe_singleton(factory_probe_state.player2);
}

static void lifecycle_isolation_bind(void *controller, const void *owner) {
    cre_token *token = &lifecycle_isolation_token;
    /* Authenticate before the native helper can change the controller's owner.
     * A nested bind is a failure, even if it names the same controller. */
    lifecycle_discard_require(!lifecycle_isolation_bind_active && cre_current == token &&
        atomic_load(&token->state) == 1 && token->phase == CRE_BIND && token->hits == 0 &&
        token->object == controller && lifecycle_isolation_authenticate(NULL, controller, CRE_BIND) &&
        lifecycle_discard_pointer((uintptr_t)controller) == (uintptr_t)&lifecycle_isolation_vtable[2] &&
        lifecycle_discard_pointer((uintptr_t)controller + 0x20) == lifecycle_discard_ship &&
        lifecycle_discard_pointer((uintptr_t)owner) == 0, "bind_identity_or_reentry");
    lifecycle_isolation_bind_active = 1;
    cre_bind_wrapper(controller, owner);
    lifecycle_discard_require(cre_end(token, 1), "bind_refresh_scope");
    /* Native bind may have cleared controller+0x20. The destructor will also
     * replace its vptr; the later thunk authenticates its actual RBX=this. */
    lifecycle_discard_require(cre_begin(token, controller, CRE_DESTRUCT,
        lifecycle_isolation_authenticate, NULL), "destructor_refresh_scope");
    lifecycle_isolation_bind_active = 0;
}

static void lifecycle_isolation_prepare(void) {
    const input_context_census_operations operations = {NULL, player_seat_preflight_read};
    lifecycle_discard_require(input_context_census_collect(&operations, sko_session.owner + 0x10,
        &lifecycle_isolation_contexts) && lifecycle_isolation_contexts.tree_valid,
        "initial_contexts_missing");
    lifecycle_isolation_thread = (uint64_t)GetCurrentThreadId();
    /* Both retained references use the pinned native assignment protocol. The
     * live zone is allowed to become NULL during the native release. */
    lifecycle_discard_require(player_zone_initialize(&lifecycle_isolation_zone_key) &&
        pz_enter(&player_zone_resident.ledger, &lifecycle_isolation_zone_key) == PCB_ZONE_ACTIVE,
        "zone_scope_enter");
}

static void lifecycle_isolation_arm_release(void) {
    uintptr_t stock = (uintptr_t)LX(SEAT_PREFLIGHT_CONTROLLER_VTABLE);
    lifecycle_discard_read(stock - 16, lifecycle_isolation_vtable,
                           sizeof(lifecycle_isolation_vtable));
    lifecycle_discard_require(lifecycle_isolation_vtable[0] == 0 &&
        lifecycle_isolation_vtable[1] == (uintptr_t)LX(0x3d557b8) &&
        lifecycle_isolation_vtable[2] == (uintptr_t)LX(0x164c1f0) &&
        lifecycle_isolation_vtable[3] == (uintptr_t)LX(0x164c700) &&
        lifecycle_isolation_vtable[44] == (uintptr_t)LX(CRE_BIND_VA) &&
        lifecycle_discard_pointer(lifecycle_discard_controller) == stock &&
        cre_install(&cre_resident), "release_hook_admission");
    lifecycle_isolation_vtable[44] = (uintptr_t)lifecycle_isolation_bind;
    uintptr_t private_table = (uintptr_t)&lifecycle_isolation_vtable[2];
    memcpy((void *)lifecycle_discard_controller, &private_table, sizeof(private_table));
    lifecycle_discard_require(cre_begin(&lifecycle_isolation_token,
        (void *)lifecycle_discard_controller, CRE_BIND, lifecycle_isolation_authenticate, NULL),
        "bind_scope_begin");
}

static void lifecycle_isolation_finish_release(void) {
    /* The old controller has been freed. Do not dereference it or attempt to
     * restore its vptr. Native destruction already reset that vptr. */
    lifecycle_discard_require(cre_end(&lifecycle_isolation_token, 1) &&
        cre_restore(&cre_resident), "release_scope_or_patch_restore");
    /* "contexts_preserved" below covers tree validity, total node count and
     * these three requester-6 records, not all unrelated context payloads. */
    input_context_census_sample current;
    const input_context_census_operations operations = {NULL, player_seat_preflight_read};
    lifecycle_discard_require(input_context_census_collect(&operations, sko_session.owner + 0x10, &current) &&
        current.tree_valid && current.node_count == lifecycle_isolation_contexts.node_count &&
        current.target_present == lifecycle_isolation_contexts.target_present &&
        !memcmp(current.payloads, lifecycle_isolation_contexts.payloads, sizeof(current.payloads)),
        "canonical_contexts_changed");
}

static void lifecycle_isolation_restore_globals(void) {
    lifecycle_discard_require(pz_restore(&player_zone_resident.ledger, &lifecycle_isolation_zone_key) &&
        pz_destroy(&player_zone_resident.ledger), "zone_scope_restore");
    /* These opaque bytes are restored only for this discard comparison. They
     * are not a reviewed ownership policy for persistent simulation. */
    for (unsigned index = 0; index < PCB_REGION_COUNT; ++index) {
        const pcb_region *region = &pcb_regions[index];
        memcpy(LX(region->va), lifecycle_discard_initial_bank + region->offset, region->length);
    }
    void **destination = (void **)(factory_probe_universe + 0x238);
    void **assigned = ((void **(*)(void **, void *))LX(FACTORY_PROBE_ASSIGN_VA))(
        destination, (void *)factory_probe_player1);
    lifecycle_discard_require(assigned == destination && factory_probe_singleton(factory_probe_player1),
        "canonical_singleton_restore");
    ((void (*)(void *))LX(P1_REFERENCE_RELEASE_VA))((void *)(factory_probe_player1 + 0x38));
    lifecycle_discard_write("X4LAN LIFECYCLE_DISCARD isolation=pass bind_hits=1 destructor_hits=1 "
        "contexts_preserved=1 zone_restored=1 bank_restored=1 singleton_restored=1 patch_restored=1\n");
}
#endif

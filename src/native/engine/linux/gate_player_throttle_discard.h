#ifndef X4_GATE_PLAYER_THROTTLE_DISCARD_H
#define X4_GATE_PLAYER_THROTTLE_DISCARD_H

/* Default-off, process-discard-only STATE13 experiment. The real native
 * iterator is used because its filtering and traversal are part of the stock
 * STATE13 side effect. A bounded snapshot detects changed member identities;
 * it does not exclude other readers or establish ownership of those members. */
#define PLAYER_THROTTLE_MEMBER_LIMIT 64u

typedef struct {
    uintptr_t address;
    uint32_t value_328;
    uint32_t value_2f0;
    unsigned char value_2f4;
} player_throttle_member;

typedef struct {
    uintptr_t collection;
    unsigned count;
    player_throttle_member members[PLAYER_THROTTLE_MEMBER_LIMIT];
    unsigned char keys[2];
    float command, lower, upper, original;
    uintptr_t factor_table;
    float factor68, factor70, factor74, factor78;
    double controller_factor;
} player_throttle_snapshot;

static unsigned player_throttle_phase;
static player_throttle_snapshot player_throttle_initial;
static const lx_site player_throttle_pins[] = {
    {"throttle-jump-table", 0x2d6ddf0, 8, "40acedcf2fe81b91cfb4292395c4547a78d659abba10f30bb9986e2eaea04f37"},
    {"throttle-ship-getter", 0x1dc8d40, 91, "1d146c0784618df3db973fdc923a604dffab70849c2edbd32a4b5e8f5b4124a5"},
    {"throttle-current-ship", 0x1699f20, 107, "875a39c3fadc0608768592821830bf99016fb500cdc452f788f7e69fa2091196"},
    {"throttle-command-set", 0x1657ce0, 65, "cbd4ff96b7eb6366962a0fe5fc126f2a9186e60b4d6156d947a72553f759427a"},
    {"throttle-command-update", 0x1657d30, 1350, "fc9b722b8948f8a1424b9bb5274f32370465349cfe5814fd962daa4d8e0172f0"},
    {"throttle-iterator-begin", 0x1e09590, 187, "b5ea64e61c282c662de3d6a0e543d69faf59e280e77d8ae0b99f20b032581b90"},
    {"throttle-components", 0x147f790, 119, "367f5c551aab71da19e27ee281457909524c3033370a89d62799e011501650ad"},
    {"throttle-iterator", 0xee29b0, 314, "7e0152d9b205e3f2624298ce0cb8c746b3743faf0c70be5c4fe9cd99057b1f5c"},
    {"throttle-component-reset", 0x1480ee0, 152, "398c209c56d2a6c6b20cf39fd75c335b50792146b09b6cac63ea02c271ffe8aa"},
    {"throttle-component-action", 0x14803b0, 201, "ac66f296cf6fef16a50e36256add101c441927f41139648d35803ba4c92e11c1"},
};

static int player_throttle_enabled(void) {
    const char *mode = getenv("X4_DEV_P2_THROTTLE");
    if (!mode || !*mode) {
        return 0;
    }
    lifecycle_discard_require(!strcmp(mode, "state13-discard") ||
                              !strcmp(mode, "state13-warm-discard"), "throttle_invalid_mode");
    return 1;
}

/* Warm mode performs the first native Update without a throttle event. */
static uint64_t player_throttle_start_tick(void) {
    const char *mode = getenv("X4_DEV_P2_THROTTLE");
    return mode && !strcmp(mode, "state13-warm-discard") ? 6 : 5;
}

static void player_throttle_require_pins(void) {
    lifecycle_discard_require(lifecycle_input_pins(), "throttle_input_pin");
    for (unsigned index = 0; index < sizeof player_throttle_pins / sizeof player_throttle_pins[0]; ++index) {
        lifecycle_discard_require(site_ok(&player_throttle_pins[index]), "throttle_site_pin");
    }
}

static void player_throttle_capture(player_throttle_snapshot *snapshot, unsigned expected_key) {
    memset(snapshot, 0, sizeof *snapshot);
    unsigned char ignored = 1;
    lifecycle_discard_require(player_recurring_physics_entered() &&
        (uintptr_t)((void *(*)(void))LX(0x1dc8d40))() == lifecycle_discard_ship &&
        (uintptr_t)((void *(*)(void))LX(0x1dd19d0))() == lifecycle_discard_controller &&
        factory_probe_singleton(factory_probe_state.player2), "throttle_target_identity");
    lifecycle_discard_read((uintptr_t)LX(0x3e5b298), &ignored, 1);
    lifecycle_discard_read((uintptr_t)LX(0x3d8da7a), snapshot->keys, sizeof snapshot->keys);
    lifecycle_discard_read((uintptr_t)LX(0x3d8d9dc), &snapshot->command, sizeof snapshot->command);
    lifecycle_discard_read((uintptr_t)LX(0x3d8d9e8), &snapshot->lower, sizeof snapshot->lower);
    lifecycle_discard_read((uintptr_t)LX(0x3d8d9ec), &snapshot->upper, sizeof snapshot->upper);
    lifecycle_discard_read((uintptr_t)LX(0x3d8da00), &snapshot->original, sizeof snapshot->original);
    lifecycle_discard_read(lifecycle_discard_ship + 0xa8, &snapshot->collection, sizeof snapshot->collection);
    if (player_throttle_start_tick() == 6) {
        uintptr_t table_after = 0;
        lifecycle_discard_read((uintptr_t)LX(0x3db83c8), &snapshot->factor_table, sizeof snapshot->factor_table);
        lifecycle_discard_require(snapshot->factor_table && snapshot->factor_table <= UINTPTR_MAX - 0x7c &&
            lifecycle_discard_controller <= UINTPTR_MAX - 0x19160, "throttle_factor_identity");
        lifecycle_discard_read(snapshot->factor_table + 0x68, &snapshot->factor68, sizeof(float));
        lifecycle_discard_read(snapshot->factor_table + 0x70, &snapshot->factor70, sizeof(float));
        lifecycle_discard_read(snapshot->factor_table + 0x74, &snapshot->factor74, sizeof(float));
        lifecycle_discard_read(snapshot->factor_table + 0x78, &snapshot->factor78, sizeof(float));
        lifecycle_discard_read(lifecycle_discard_controller + 0x19158, &snapshot->controller_factor, sizeof(double));
        lifecycle_discard_read((uintptr_t)LX(0x3db83c8), &table_after, sizeof table_after);
        lifecycle_discard_require(table_after == snapshot->factor_table && isfinite(snapshot->factor68) &&
            isfinite(snapshot->factor70) && isfinite(snapshot->factor74) && isfinite(snapshot->factor78) &&
            isfinite(snapshot->controller_factor), "throttle_factor_values");
    }
    lifecycle_discard_require(!ignored && snapshot->keys[0] == expected_key && !snapshot->keys[1] &&
        isfinite(snapshot->command) && isfinite(snapshot->lower) && isfinite(snapshot->upper) &&
        isfinite(snapshot->original) && snapshot->lower <= snapshot->upper, "throttle_keys_or_values");

    /* Iterator ABI is sret40 in RDI, ship in RSI. Cursor node is at +0,
     * vector index at +0x10; stock increment consumes the cursor in RDI. */
    _Alignas(16) unsigned char cursor[40] = {0};
    ((void (*)(void *, void *))LX(0x147f790))(cursor, (void *)lifecycle_discard_ship);
    for (;;) {
        uintptr_t node = 0, start = 0, end = 0, member = 0;
        uint32_t index = 0;
        memcpy(&node, cursor, sizeof node);
        memcpy(&index, cursor + 0x10, sizeof index);
        if (!node) {
            break;
        }
        lifecycle_discard_require(snapshot->count < PLAYER_THROTTLE_MEMBER_LIMIT &&
            node <= UINTPTR_MAX - 16 && read_local((void *)node, &start, 8) &&
            read_local((void *)(node + 8), &end, 8) && start && end >= start &&
            (end - start) % 8 == 0 && index < (end - start) / 8 &&
            start <= UINTPTR_MAX - (uintptr_t)index * 8 &&
            read_local((void *)(start + (uintptr_t)index * 8), &member, 8) && member &&
            member <= UINTPTR_MAX - 0x32c, "throttle_component_cursor");
        player_throttle_member *observed = &snapshot->members[snapshot->count++];
        observed->address = member;
        lifecycle_discard_read(member + 0x328, &observed->value_328, sizeof observed->value_328);
        lifecycle_discard_read(member + 0x2f0, &observed->value_2f0, sizeof observed->value_2f0);
        lifecycle_discard_read(member + 0x2f4, &observed->value_2f4, sizeof observed->value_2f4);
        ((void (*)(void *))LX(0xee29b0))(cursor);
    }
    uintptr_t collection_after = 0;
    lifecycle_discard_read(lifecycle_discard_ship + 0xa8, &collection_after, sizeof collection_after);
    lifecycle_discard_require(collection_after == snapshot->collection, "throttle_collection_changed");
}

static void player_throttle_compare(const player_throttle_snapshot *before,
                                    const player_throttle_snapshot *after) {
    lifecycle_discard_require(before->collection == after->collection && before->count == after->count,
                              "throttle_members_changed");
    for (unsigned index = 0; index < before->count; ++index) {
        lifecycle_discard_require(before->members[index].address == after->members[index].address,
                                  "throttle_member_replaced");
    }
}

static void player_throttle_log(const char *stage, uint64_t generation,
                                const player_throttle_snapshot *snapshot) {
    char message[384];
    snprintf(message, sizeof message,
        "X4LAN LIFECYCLE_DISCARD throttle=%s tick=%llu ship=%llx controller=%llx collection=%llx "
        "members=%u keys=%u%u command=%.9g lower=%.9g upper=%.9g original=%.9g\n",
        stage, (unsigned long long)generation, (unsigned long long)lifecycle_discard_ship,
        (unsigned long long)lifecycle_discard_controller, (unsigned long long)snapshot->collection,
        snapshot->count, snapshot->keys[0], snapshot->keys[1], (double)snapshot->command,
        (double)snapshot->lower, (double)snapshot->upper, (double)snapshot->original);
    if (player_throttle_start_tick() == 6) {
        size_t length = strlen(message);
        lifecycle_discard_require(length && message[length - 1] == '\n', "throttle_factor_format");
        int written = snprintf(message + length - 1, sizeof(message) - length + 1,
            " table=%llx factor68=%.9g factor70=%.9g factor74=%.9g factor78=%.9g controller_factor=%.17g\n",
            (unsigned long long)snapshot->factor_table, (double)snapshot->factor68,
            (double)snapshot->factor70, (double)snapshot->factor74, (double)snapshot->factor78,
            snapshot->controller_factor);
        lifecycle_discard_require(written > 0 && (size_t)written < sizeof(message) - length + 1,
                                  "throttle_factor_format");
    }
    lifecycle_discard_write(message);
    for (unsigned index = 0; index < snapshot->count; ++index) {
        const player_throttle_member *member = &snapshot->members[index];
        snprintf(message, sizeof message,
            "X4LAN LIFECYCLE_DISCARD throttle_member=%s tick=%llu index=%u object=%llx "
            "field328=%08x field2f0=%08x field2f4=%02x\n", stage,
            (unsigned long long)generation, index, (unsigned long long)member->address,
            member->value_328, member->value_2f0, member->value_2f4);
        lifecycle_discard_write(message);
    }
}

static void player_throttle_apply(uint64_t generation, const stock_input_record *record) {
    uint64_t start = player_throttle_start_tick();
    if (generation != start && generation != start + 1) {
        return;
    }
    lifecycle_discard_require(record && !record->count && player_throttle_phase == generation - start,
                              "throttle_script_admission");
    player_throttle_require_pins();
    player_throttle_snapshot before, after;
    player_throttle_capture(&before, generation == start + 1);
    if (generation == start) {
        player_throttle_initial = before;
    } else {
        player_throttle_compare(&player_throttle_initial, &before);
    }
    player_throttle_log("before", generation, &before);
    ((void (*)(int, unsigned char))LX(0x166c6f0))(13, (unsigned char)(generation == start));
    player_throttle_capture(&after, generation == start);
    player_throttle_compare(&before, &after);
    player_throttle_log("applied", generation, &after);
    ++player_throttle_phase;
}

static void player_throttle_after_update(uint64_t generation) {
    uint64_t start = player_throttle_start_tick();
    if (generation != start && generation != start + 1) {
        return;
    }
    player_throttle_snapshot after;
    player_throttle_capture(&after, generation == start);
    player_throttle_compare(&player_throttle_initial, &after);
    player_throttle_log("updated", generation, &after);
}
#endif

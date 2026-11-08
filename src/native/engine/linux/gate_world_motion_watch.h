/* Default-off, read-only Nav evidence for at most four imported saved objects.
 * This deliberately does not call the census record helpers: those own mutable
 * counters, coverage and digest caches. Only the existing local Nav snapshot and
 * canonical serializer are shared. Matching reads detect observed changes; they
 * do not create a lifetime lock or exclude an unobserved ABA. */
#define WORLD_MOTION_WATCH_MAX 4u
#define WORLD_MOTION_WINDOW_MAX 18000u
#define WORLD_MOTION_ROW_BYTES 1100u
typedef struct { uint64_t saved, runtime; } world_motion_target;
typedef struct {
    uintptr_t vtable, controller, parent;
    uint64_t runtime, parent_runtime, parent_saved;
    unsigned type, flags, attention;
} world_motion_metadata;
static world_motion_target world_motion_targets[WORLD_MOTION_WATCH_MAX];
static unsigned world_motion_count, world_motion_failures;
static uint64_t world_motion_first, world_motion_last, world_motion_tick, world_motion_samples;
static const char *world_motion_problem = "inactive";
static char world_motion_rows[WORLD_MOTION_WATCH_MAX][WORLD_MOTION_ROW_BYTES];

static int world_motion_configure(const char *identities, size_t length, uint64_t first, uint64_t last) {
    world_motion_target targets[WORLD_MOTION_WATCH_MAX] = {{0}};
    unsigned count = 0;
    if (world_motion_count || !world_identity_sealed || !world_identities || !identities ||
        !first || last < first || last - first >= WORLD_MOTION_WINDOW_MAX ||
        !length || length > WORLD_MOTION_WATCH_MAX * 17 - 1) return 0;
    for (size_t offset = 0; offset < length;) {
        if (count == WORLD_MOTION_WATCH_MAX || length - offset < 16) return 0;
        uint64_t saved = 0;
        for (unsigned digit = 0; digit < 16; ++digit) {
            char character = identities[offset++];
            if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) return 0;
            saved = saved * 16 + (unsigned)(character <= '9' ? character - '0' : character - 'a' + 10);
        }
        if (!saved || (saved >> 63)) return 0; /* created IDs are outside this observer's scope */
        for (unsigned previous = 0; previous < count; ++previous)
            if (targets[previous].saved == saved) return 0;
        targets[count++].saved = saved;
        if (offset < length && (identities[offset++] != ',' || offset == length)) return 0;
    }
    /* Resolve the sealed import table once. Per-tick work keeps only runtime IDs,
     * never a cached object pointer or a repeated whole-world traversal. */
    for (unsigned bucket = 0; bucket < WORLD_IDENTITY_CAPACITY; ++bucket) {
        const world_identity_entry *entry = &world_identities[bucket];
        for (unsigned index = 0; index < count; ++index) {
            if (entry->saved != targets[index].saved) continue;
            if (!entry->runtime || targets[index].runtime) return 0;
            targets[index].runtime = entry->runtime;
        }
    }
    for (unsigned index = 0; index < count; ++index) if (!targets[index].runtime) return 0;
    memcpy(world_motion_targets, targets, sizeof(targets));
    world_motion_count = count;
    world_motion_first = first;
    world_motion_last = last;
    world_motion_problem = "ready";
    return 1;
}

static int world_motion_read_metadata(uintptr_t object, world_motion_metadata *metadata) {
    memset(metadata, 0, sizeof(*metadata));
    if (!world_nav_address(object, LXW_OBJ_SPAN) ||
        !WREAD(object, &metadata->vtable, 8) ||
        !WREAD(object + LXW_OBJ_RUNTIME, &metadata->runtime, 8) ||
        !WREAD(object + LXW_OBJ_CONTROLLER, &metadata->controller, 8) ||
        !WREAD(object + LXW_OBJ_PARENT, &metadata->parent, 8) ||
        !WREAD(object + LXW_OBJ_ATTENTION, &metadata->attention, 4)) return 0;
    const lxw_class *object_class = lxw_class_of(metadata->vtable);
    if (!object_class || object_class->family != LXW_FAMILY_NONE ||
        !(object_class->flags & 2) || (object_class->flags & 0x80000000u)) return 0;
    metadata->type = object_class->windows_rva;
    metadata->flags = 1u | 4u | ((object_class->flags & 1) ? 2u : 0u);
    if (metadata->parent) {
        if (!world_nav_address(metadata->parent, LXW_OBJ_RUNTIME + 8) ||
            !WREAD(metadata->parent + LXW_OBJ_RUNTIME, &metadata->parent_runtime, 8) ||
            !metadata->parent_runtime) return 0;
        metadata->parent_saved = world_identity_portable(metadata->parent_runtime);
        if (!metadata->parent_saved) return 0;
    }
    return 1;
}

static void world_motion_hex(const unsigned char *bytes, size_t length, char *text) {
    static const char digits[] = "0123456789abcdef";
    for (size_t index = 0; index < length; ++index) {
        text[index * 2] = digits[bytes[index] >> 4];
        text[index * 2 + 1] = digits[bytes[index] & 15];
    }
    text[length * 2] = 0;
}

static int world_motion_capture(uint64_t tick) {
    world_motion_problem = "sequence";
    if (!world_motion_count || world_motion_failures || tick < world_motion_first || tick > world_motion_last ||
        tick != (world_motion_tick ? world_motion_tick + 1 : world_motion_first)) return 0;
    void *map = NULL;
    world_motion_problem = "component_map";
    if (!WREAD(world_component_map ? world_component_map : (void **)LX(LX_COMPONENT_MAP_VA), &map, sizeof(map)) || !map) return 0;
    lxw_find_fn find = world_find ? world_find : (lxw_find_fn)(void *)LX(LX_FIND_VA);
    for (unsigned index = 0; index < world_motion_count; ++index) {
        const world_motion_target *target = &world_motion_targets[index];
        world_motion_metadata before, after;
        world_nav_snapshot snapshot;
        unsigned char motion[WORLD_NAV_MOTION_BYTES + 16], timing[WORLD_NAV_TIMING_BYTES + 16], raw[WORLD_NAV_RAW_BYTES];
        world_motion_problem = "saved_identity";
        if (world_identity_lookup(target->runtime) != target->saved) return 0;
        uintptr_t object = (uintptr_t)find(map, target->runtime, 4, 0);
        world_motion_problem = "object_metadata";
        if (!world_motion_read_metadata(object, &before) || before.runtime != target->runtime) return 0;
        if (!lxw_nav_capture(object, before.controller, target->runtime, &snapshot, &world_motion_problem)) return 0;
        world_motion_problem = "snapshot_metadata_changed";
        if (snapshot.links.object_vtable != before.vtable || snapshot.links.attention != before.attention) return 0;
        unsigned reference_flags = 0;
        world_nav_encode(&snapshot, timing, motion, raw, &reference_flags);
        world_motion_problem = "bank_reference_unknown";
        if (reference_flags) return 0;
        world_motion_problem = "identity_recheck";
        if ((uintptr_t)find(map, target->runtime, 4, 0) != object ||
            world_identity_lookup(target->runtime) != target->saved ||
            !world_motion_read_metadata(object, &after) || memcmp(&before, &after, sizeof(before))) return 0;
        /* These trailers make the bytes exactly the census motion/timing domain
         * preimages, including type, coverage flags and portable parent identity. */
        unsigned char trailer[16];
        memcpy(trailer, &before.type, 4);
        memcpy(trailer + 4, &before.flags, 4);
        memcpy(trailer + 8, &before.parent_saved, 8);
        memcpy(motion + WORLD_NAV_MOTION_BYTES, trailer, sizeof(trailer));
        memcpy(timing + WORLD_NAV_TIMING_BYTES, trailer, sizeof(trailer));
        char motion_text[sizeof(motion) * 2 + 1], timing_text[sizeof(timing) * 2 + 1];
        world_motion_hex(motion, sizeof(motion), motion_text);
        world_motion_hex(timing, sizeof(timing), timing_text);
        uint32_t cursor;
        memcpy(&cursor, snapshot.nav + 8, 4);
        int length = snprintf(world_motion_rows[index], WORLD_MOTION_ROW_BYTES,
            "version=1 tick=%llu id=%016llx type=%08x flags=%08x parent=%016llx read=%u write=%u cursor=%u motion=%s timing=%s",
            (unsigned long long)tick, (unsigned long long)target->saved, before.type, before.flags,
            (unsigned long long)before.parent_saved, snapshot.links.read, snapshot.links.write, cursor, motion_text, timing_text);
        world_motion_problem = "row_truncated";
        if (length < 0 || (size_t)length >= WORLD_MOTION_ROW_BYTES) return 0;
    }
    world_motion_tick = tick;
    world_motion_samples += world_motion_count;
    world_motion_problem = "complete";
    return 1;
}

static int lua_world_motion_config(lua_State *L) {
    size_t length = 0;
    const char *identities = p_lua_tolstring(L, 1, &length);
    double first = p_lua_tonumber(L, 2), last = p_lua_tonumber(L, 3);
    int valid = accept_ui() && stats.active && boot_mode && boot_phase == 3 && stats.issued == 1 &&
        isfinite(first) && isfinite(last) && first >= 1 && last <= 9007199254740991.0 &&
        floor(first) == first && floor(last) == last && first <= last &&
        world_motion_configure(identities, length, (uint64_t)first, (uint64_t)last);
    p_lua_pushnumber(L, valid);
    return 1;
}
static int lua_world_motion_capture(lua_State *L) {
    double tick = p_lua_tonumber(L, 1);
    if (!accept_ui() || !stats.active || !boot_mode || boot_phase != 3 || !world_identity_sealed ||
        !isfinite(tick) || tick < 1 || tick > 9007199254740991.0 || tick != (double)stats.issued ||
        !world_motion_capture((uint64_t)tick)) {
        ++world_motion_failures;
        p_lua_pushnil(L);
        p_lua_pushlstring(L, world_motion_problem, strlen(world_motion_problem));
        return 2;
    }
    p_lua_pushnumber(L, world_motion_count);
    return 1;
}
static int lua_world_motion_row(lua_State *L) {
    double index = p_lua_tonumber(L, 1);
    if (!accept_ui() || world_motion_failures || world_motion_tick != stats.issued ||
        !isfinite(index) || index < 1 || index > world_motion_count || floor(index) != index) {
        p_lua_pushnil(L); return 1;
    }
    const char *row = world_motion_rows[(unsigned)index - 1];
    p_lua_pushlstring(L, row, strlen(row));
    return 1;
}
static int lua_world_motion_status(lua_State *L) {
    char text[180];
    int length = snprintf(text, sizeof(text), "count=%u first=%llu last=%llu samples=%llu failures=%u",
        world_motion_count, (unsigned long long)world_motion_first, (unsigned long long)world_motion_last,
        (unsigned long long)world_motion_samples, world_motion_failures);
    if (!accept_ui() || length < 0 || (size_t)length >= sizeof(text)) p_lua_pushnil(L);
    else p_lua_pushlstring(L, text, (size_t)length);
    return 1;
}

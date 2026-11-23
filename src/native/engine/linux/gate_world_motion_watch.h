/* Default-off, read-only Nav evidence for at most four imported saved objects.
 * This deliberately does not call the census record helpers: those own mutable
 * counters, coverage and digest caches. Only the existing local Nav snapshot and
 * canonical serializer are shared. Matching reads detect observed changes; they
 * do not create a lifetime lock or exclude an unobserved ABA. */
#define WORLD_MOTION_WATCH_MAX 4u
#define WORLD_MOTION_WINDOW_MAX 18000u
#define WORLD_MOTION_ROW_BYTES 6144u
#define WORLD_MOTION_BEHAVIOUR_RAW 0x100u
#define WORLD_MOTION_MODEL_RAW 0x340u
/* The agent fields the Linear step reads, in Windows' agent_ext= order and sizes (lockstep_motion_watch.h
 * motion_agent_fields); Linux offsets, +0x20 from +0x6e0 on (gate_world.h's pathing agent note). */
static const struct {unsigned short at,size;} world_motion_agent_fields[]={
    {0x364,4},{0x4d0,1},{0x4d2,1},{0x4d8,4},{0x518,12},{0xf53,1},{0xf7c,1},{0xf7d,1},{0xf94,1},{0xf95,1},{0xf97,1},{0xf9a,1},
    {0xf58,8},{0xf60,8},{0x4d1,1},{0x4d3,1}};
#define WORLD_MOTION_AGENT_EXT 47u
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

/* The pathing manager's agent for a runtime ID, as UpdatePathing 0x1431270 asks 0x13d0b50 (Windows inline at 0xe8c45d):
 * the tree at manager+0x260 (Windows +0x130), a node's left +0x8, right +0x10, key +0x20, agent +0x28; 0 none. */
static uintptr_t world_motion_agent_of(uint64_t id) {
    uintptr_t manager = 0, node = 0, best, next;
    uint64_t key;
    if (!WREAD(LX(LX_PATH_MANAGER_VA), &manager, 8) || !manager) return 0;
    const uintptr_t head = manager + 0x260u;
    if (!WREAD(head + 8u, &node, 8)) return 0;
    best = head;
    for (unsigned depth = 0; node; depth++) {
        if (depth >= 64u || !WREAD(node + 0x20u, &key, 8)) return 0;
        if (key < id) { if (!WREAD(node + 0x10u, &next, 8)) return 0; }
        else { best = node; if (!WREAD(node + 8u, &next, 8)) return 0; }
        node = next;
    }
    uintptr_t agent = 0;
    if (best == head || !WREAD(best + 0x20u, &key, 8) || id < key || !WREAD(best + 0x28u, &agent, 8)) return 0;
    return agent;
}
/* pt_ref= / pt_map=: the agent's first path point's reference (agent+0x6d0 begin, its ID at +0x8; Windows +0x6a8 /
 * +0x10), and the first of it and its parents (+0x70) with an agent in the manager's tree: depth/the agent owner's
 * (+0xf10) portable ID; none/0 when none of 9 levels has one (UpdatePathing's reference agent: the point's own or, on
 * a miss, an ancestor's; agent+0xf53 (Windows +0xf33) is set when it has one). */
static void world_motion_point_agent(uintptr_t agent, void *map, lxw_find_fn find, uint64_t *ref_key, char *text, size_t cap) {
    uintptr_t begin = 0, end = 0, object, owner = 0, found = 0;
    uint64_t id = 0, owner_id = 0;
    unsigned depth = 0;
    *ref_key = 0;
    snprintf(text, cap, "none/0");
    if (!WREAD(agent + 0x6d0u, &begin, 8) || !WREAD(agent + 0x6d8u, &end, 8) || !begin || end <= begin ||
        !WREAD(begin + 8u, &id, 8) || !id) return;
    *ref_key = world_identity_portable(id);
    object = (uintptr_t)find(map, id, 4, 0);
    for (; depth <= 8u; depth++) {
        if ((found = world_motion_agent_of(id))) break;
        uintptr_t parent = 0;
        if (!object || !WREAD(object + LXW_OBJ_PARENT, &parent, 8) || !parent || !WREAD(parent + LXW_OBJ_RUNTIME, &id, 8) || !id) return;
        object = parent;
    }
    if (!found) return;
    if (WREAD(found + LXW_AGENT_OWNER, &owner, 8) && owner) WREAD(owner + LXW_OBJ_RUNTIME, &owner_id, 8);
    snprintf(text, cap, "%u/%016llx", depth, (unsigned long long)(owner_id ? world_identity_portable(owner_id) : 0));
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
        /* The agent's flight control model (agent+0xf30; Windows +0xf10): its vtable and its target, the transform at
         * +0x10..+0x50 and the reference ID at +0x50 (Windows +0x20/+0x60, lockstep_motion_watch.h target=), as read. */
        uintptr_t model = 0, model_vt = 0;
        unsigned char model_target[0x48] = {0}, model_plan[0x70] = {0};
        char model_text[sizeof(model_target) * 2 + 1], plan_text[sizeof(model_plan) * 2 + 1];
        uint64_t model_ref = 0;
        if (!WREAD(snapshot.links.agent + 0xf30u, &model, 8) || !model || !WREAD(model, &model_vt, 8) ||
            !WREAD(model + 0x10u, model_target, sizeof(model_target)) || !WREAD(model + 0x60u, model_plan, sizeof(model_plan))) model = 0;
        /* the Linear model's plan at +0x60..+0xd0 (Windows +0x70..+0xe0, model_ext=): start position and time, last step time */
        world_motion_hex(model_plan, sizeof(model_plan), plan_text);
        unsigned char agent_ext[WORLD_MOTION_AGENT_EXT] = {0};
        char agent_text[sizeof(agent_ext) * 2 + 1];
        for (unsigned i = 0, at = 0; i < sizeof(world_motion_agent_fields) / sizeof(world_motion_agent_fields[0]); i++) {
            if (!WREAD(snapshot.links.agent + world_motion_agent_fields[i].at, agent_ext + at, world_motion_agent_fields[i].size)) {
                memset(agent_ext, 0, sizeof(agent_ext));
                break;
            }
            at += world_motion_agent_fields[i].size;
        }
        world_motion_hex(agent_ext, sizeof(agent_ext), agent_text);
        memcpy(&model_ref, model_target + 0x40, 8);
        world_motion_hex(model_target, sizeof(model_target), model_text);
        uint64_t point_ref = 0;
        unsigned char model_raw[WORLD_MOTION_MODEL_RAW] = {0};
        static char model_raw_text[2 * WORLD_MOTION_MODEL_RAW + 1];
        if (model) WREAD(model, model_raw, sizeof(model_raw));
        world_motion_hex(model_raw, sizeof(model_raw), model_raw_text);
        /* the Steering model's two behaviours (+0x60/+0x68; Windows +0x70/+0x78): class vtable and first bytes */
        static char behaviour_text[2 * (16 + 2 * WORLD_MOTION_BEHAVIOUR_RAW) + 8];
        size_t behaviour_at = 0;
        for (unsigned b = 0; b < 2; b++) {
            uintptr_t behaviour = 0, behaviour_vt = 0;
            unsigned char behaviour_raw[WORLD_MOTION_BEHAVIOUR_RAW];
            if (b) behaviour_text[behaviour_at++] = ',';
            if (!model || !WREAD(model + 0x60u + 8u * b, &behaviour, 8) || !behaviour || !WREAD(behaviour, &behaviour_vt, 8) ||
                !WREAD(behaviour, behaviour_raw, sizeof(behaviour_raw))) { behaviour_text[behaviour_at++] = '-'; continue; }
            behaviour_at += (size_t)snprintf(behaviour_text + behaviour_at, 16, "%x:", (unsigned)(behaviour_vt - LX_IMAGE_BASE));
            world_motion_hex(behaviour_raw, sizeof(behaviour_raw), behaviour_text + behaviour_at);
            behaviour_at += 2 * sizeof(behaviour_raw);
        }
        behaviour_text[behaviour_at] = 0;
        char point_agent[40];
        world_motion_point_agent(snapshot.links.agent, map, find, &point_ref, point_agent, sizeof(point_agent));
        int length = snprintf(world_motion_rows[index], WORLD_MOTION_ROW_BYTES,
            "version=1 tick=%llu id=%016llx type=%08x flags=%08x parent=%016llx read=%u write=%u cursor=%u motion=%s timing=%s"
            " model=%x target_ref=%016llx target=%s plan=%s agent_ext=%s pt_ref=%016llx pt_map=%s model_raw=%s beh=%s",
            (unsigned long long)tick, (unsigned long long)target->saved, before.type, before.flags,
            (unsigned long long)before.parent_saved, snapshot.links.read, snapshot.links.write, cursor, motion_text, timing_text,
            model ? (unsigned)(model_vt - LX_IMAGE_BASE) : 0u, (unsigned long long)world_identity_portable(model_ref), model_text, plan_text, agent_text,
            (unsigned long long)point_ref, point_agent, model_raw_text, behaviour_text);
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

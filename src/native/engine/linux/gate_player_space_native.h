#ifndef X4_GATE_PLAYER_SPACE_NATIVE_H
#define X4_GATE_PLAYER_SPACE_NATIVE_H

#include "player_stock_space.h"

/* Native half of the SPACE/context-8 port. This file installs no callback and
 * admits no wire records. The coordinator must publish both validated records
 * before calling replay, and must terminate the disposable session on failure:
 * a callback may already have changed native state.
 *
 * Each operation checks a freshly read identity. Every native callback is
 * followed by that check before another cached address can be dereferenced.
 * This detects replacement; it does not create a lock or a lifetime lease.
 * The caller retains the existing parked-main/owned-worker and P2 scope.
 */
static const lx_site player_space_native_sites[] = {
    {"space-context", 0x165d950, 712, "4795aad2bc3ad6cb5588fdf5d19dd6e3ee015a0e0354fe5dd06284b345a275d7"},
    {"space-release", 0x165ab80, 735, "deee267f36edae1d229d7509531f2e327229e86d52a559e09a04a4d05d17af65"},
    {"space-handoff", 0x165d1f0, 1293, "e7fff4ba578611f3ac64f6aed94ac39f8bf391e9c618b2ac300f3d1a74a07cce"},
    {"space-authority", 0x1658d60, 86, "1a796356b0e1ca17860fbc4cff9fa7303ab57a43f136b622d1f5a6dfd160fbcf"},
    {"space-started", 0x13709d0, 239, "a3bfeb8074a100a512a9d3d9e674566219ae9ae3b75262014cf1e66ceae56229"},
    {"space-stopped", 0x1370ac0, 239, "7e3a0fdfcaa03baede936666f3754c14e0d1f60478dc4fe9de78f927f38e84ea"},
    {"space-controller", 0x1dd19d0, 137, "3cab7b30231bb96a1276459079a28e655b922186495473633288e7ede5c9f837"},
    {"space-steer_mode", 0xcec0f0, 108, "127175aadaf0a326cde96ff2089603890fac6ddf9ee6b99b70a6999006c41186"},
    {"space-steer_direct", 0xcec070, 124, "7870da4f662f12327daf012dc927db622c93c89ea5ba4ddd9652489a8922e195"},
    {"space-refresh", 0x165d860, 235, "239c4ec0e606c95d16d15966608a361b2fc25f4680cee6242936314735602233"},
    {"space-option", 0x10300c0, 412, "a063fd6b228b7395a1175c380e5f577cf4fb560fdfaffae9dc819675c3f308f9"},
};

typedef struct {
    uint64_t generation;
    uintptr_t input;
    uintptr_t player;
    uintptr_t ship;
    uintptr_t controller;
    uintptr_t options;
    unsigned slot;
    unsigned local;
} player_space_native_identity;

static int player_space_native_pins(void) {
    for (unsigned index = 0; index < sizeof player_space_native_sites / sizeof *player_space_native_sites; ++index) {
        if (!site_ok(&player_space_native_sites[index])) return 0;
    }
    static const char name[] = "persistentmousesteering";
    char observed[sizeof name];
    double delay = 0;
    return read_local(LX(0x2cb6f85), observed, sizeof observed) &&
        !memcmp(observed, name, sizeof name) &&
        read_local(LX(0x2d5f520), &delay, sizeof delay) && delay == 0.5;
}

static int player_space_native_still_valid(void *context) {
    const player_space_native_identity *identity = context;
    uintptr_t input = 0, universe = 0, player = 0, owner = 0, options = 0;
    if (!identity || !keyboard_probe_worker() || stats.issued != identity->generation ||
        !read_local(LX(0x3daaaf8), &input, sizeof input) || input != identity->input ||
        input != sko_session.owner || !read_local(LX(0x3db6948), &universe, sizeof universe) ||
        !universe || !read_local((void *)(universe + 0x238), &player, sizeof player) ||
        player != identity->player ||
        !read_local((void *)(identity->controller + 0x20), &owner, sizeof owner) ||
        owner != identity->ship) return 0;
    if (identity->local) {
        if (identity->slot != 1 || player_routes_scope ||
            atomic_load(&stock_observer_input_depth) != 1 ||
            !read_local(LX(0x3daab80), &options, sizeof options) ||
            options != identity->options) return 0;
    } else if (!player_recurring_physics_entered() ||
               player_recurring_tail_token.generation != identity->generation ||
               atomic_load(&stock_observer_input_depth) ||
               atomic_load(&sko_session.wrapper_depth) ||
               !player_physics_identity_for(identity->slot == 2)) {
        return 0;
    }
    /* These pinned getters establish current engine identities; cached values
     * are comparison values, never fallback call destinations. */
    return (uintptr_t)((void *(*)(void))LX(0x1dc8d40))() == identity->ship &&
        (uintptr_t)((void *(*)(void))LX(0x1dd19d0))() == identity->controller;
}

static int player_space_native_identity_begin(player_space_native_identity *identity,
                                              unsigned slot, uint64_t generation,
                                              unsigned local) {
    if (!identity || (slot != 1 && slot != 2) || local > 1 ||
        !player_space_native_pins()) return 0;
    memset(identity, 0, sizeof *identity);
    identity->generation = generation;
    identity->slot = slot;
    identity->local = local;
    identity->input = sko_session.owner;
    identity->player = slot == 1 ? factory_probe_player1 : factory_probe_state.player2;
    identity->ship = slot == 1 ? keyboard_probe_binding.ship : lifecycle_discard_ship;
    identity->controller = slot == 1 ? keyboard_probe_binding.controller : lifecycle_discard_controller;
    unsigned char input_copy[PLAYER_SPACE_INPUT_SIZE];
    if (!identity->input || !identity->player || !identity->ship || !identity->controller ||
        !read_local((void *)identity->input, input_copy, sizeof input_copy) ||
        !read_local(LX(0x3daab80), &identity->options, sizeof identity->options) ||
        (local && (!identity->options || identity->options > UINTPTR_MAX - 0x128))) return 0;
    return player_space_native_still_valid(identity);
}

static unsigned char player_space_native_authority(void *controller) {
    return ((unsigned char (*)(void *))LX(0x1658d60))(controller);
}
static void player_space_native_handoff(void *controller, unsigned char authority) {
    ((void (*)(void *, unsigned char))LX(0x165d1f0))(controller, authority);
}
static void player_space_native_release(void *controller) {
    ((void (*)(void *))LX(0x165ab80))(controller);
}
static void player_space_native_started(void *player) {
    ((void (*)(void *))LX(0x13709d0))(player);
}
static void player_space_native_stopped(void *player) {
    ((void (*)(void *))LX(0x1370ac0))(player);
}
static int player_space_native_persistent(void *context) {
    const player_space_native_identity *identity = context;
    if (!player_space_native_still_valid(context)) return -1;
    return ((unsigned char (*)(void *, const char *, unsigned char))LX(0x10300c0))(
        (void *)(identity->options + 0x128), (const char *)LX(0x2cb6f85), 0);
}
static void player_space_native_mouse(void *input, unsigned char enabled) {
    ((void (*)(void *, unsigned char))LX(0xcec0f0))(input, enabled);
}
static void player_space_native_direct(void *input, unsigned char enabled) {
    ((void (*)(void *, unsigned char))LX(0xcec070))(input, enabled);
}

static inline int player_space_native_local(uint64_t generation, unsigned active) {
    player_space_native_identity identity;
    if (active > 1 || !player_space_native_identity_begin(&identity, 1, generation, 1)) return 0;
    player_space_local local = {
        .input = (unsigned char *)identity.input,
        .input_size = PLAYER_SPACE_INPUT_SIZE,
        .saved_mouse_steering = LX(0x3ee2dc9),
        .saved_direct_steering = LX(0x3ee2dc8),
        .persistent_mouse_steering = player_space_native_persistent,
        .option_context = &identity,
        .set_mouse_steering = player_space_native_mouse,
        .set_direct_steering = player_space_native_direct,
        .still_valid = player_space_native_still_valid,
        .identity_context = &identity
    };
    /* Record this edge before the separate refresh call: refresh can dispatch
     * another callback. The owner controls that ordering, not this primitive. */
    return player_space_local_apply(&local, active);
}

static inline int player_space_native_refresh(uint64_t generation) {
    player_space_native_identity identity;
    if (!player_space_native_identity_begin(&identity, 1, generation, 1)) return 0;
    ((void (*)(unsigned char))LX(0x165d860))(1);
    return player_space_native_still_valid(&identity);
}

static inline int player_space_native_replay(unsigned slot, uint64_t generation, unsigned active) {
    player_space_native_identity identity;
    double now = 0;
    if (active > 1 || !player_space_native_identity_begin(&identity, slot, generation, 0) ||
        !read_local(gate_tls_time(gate_tls_self()), &now, sizeof now) || !isfinite(now)) return 0;
    player_space_simulation simulation = {
        .bank = LX(0x3d8d9a0), .bank_size = PLAYER_SPACE_BANK_SIZE,
        .input = (unsigned char *)identity.input, .input_size = PLAYER_SPACE_INPUT_SIZE,
        .controller = (void *)identity.controller, .player = (void *)identity.player,
        .authority = player_space_native_authority, .handoff = player_space_native_handoff,
        .release = player_space_native_release, .started = player_space_native_started,
        .stopped = player_space_native_stopped, .still_valid = player_space_native_still_valid,
        .identity_context = &identity
    };
    return player_space_replay(&simulation, active, now);
}
#endif

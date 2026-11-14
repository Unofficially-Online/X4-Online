#include <assert.h>
#include "player_selection_parity.h"

typedef struct {
    unsigned char players[2][0x6dc0], registry[0x160], block[32], objects[3][16];
    unsigned char manager[0x60], entry[0x58];
    uintptr_t registry_global, manager_global;
    unsigned auth_calls, reject_auth, map_available, drift_source, drift_registry;
    uintptr_t unreadable;
} fixture;

static void set64(void *p, uint64_t value) { memcpy(p, &value, 8); }
static int authenticate(void *context) {
    fixture *f = context;
    return ++f->auth_calls != f->reject_auth;
}
static int readable(fixture *f, uintptr_t at, size_t n) {
    const struct { uintptr_t base; size_t size; } regions[] = {
        {(uintptr_t)f->players, sizeof f->players}, {(uintptr_t)f->registry, sizeof f->registry},
        {(uintptr_t)f->block, sizeof f->block}, {(uintptr_t)f->objects, sizeof f->objects},
        {(uintptr_t)f->manager, sizeof f->manager}, {(uintptr_t)f->entry, sizeof f->entry},
        {(uintptr_t)&f->registry_global, 8}, {(uintptr_t)&f->manager_global, 8}
    };
    if (at == f->unreadable) return 0;
    for (unsigned i = 0; i < sizeof regions / sizeof regions[0]; ++i) {
        if (at >= regions[i].base && at - regions[i].base <= regions[i].size &&
            n <= regions[i].size - (at - regions[i].base)) return 1;
    }
    return 0;
}
static int read_value(void *context, uintptr_t at, void *out, size_t n) {
    fixture *f = context;
    if (!readable(f, at, n)) return 0;
    memcpy(out, (void *)at, n);
    if (f->drift_source && at == (uintptr_t)(f->players[0] + 0x6db8)) {
        f->drift_source = 0;
        set64(f->players[0] + 0x6db8, 0);
    }
    if (f->drift_registry && at == (uintptr_t)f->block) {
        f->drift_registry = 0;
        f->block[28] ^= 1;
    }
    return 1;
}
static int map_value(void *context, uint64_t runtime, uint64_t *saved, uint64_t *portable) {
    fixture *f = context;
    *saved = runtime == 1 ? 101 : 0;
    *portable = runtime == 1 ? 101 : runtime == 2 ? 202 : 0;
    return f->map_available;
}
static void setup(fixture *f, selection_parity_ops *ops) {
    memset(f, 0, sizeof *f);
    f->registry_global = (uintptr_t)f->registry;
    f->manager_global = (uintptr_t)f->manager;
    f->map_available = 1;
    set64(f->registry + 0x60, (uintptr_t)f->block);
    for (unsigned i = 0; i < 3; ++i) {
        set64(f->block + i * 8, (uintptr_t)f->objects[i]);
        set64(f->objects[i] + 8, i + 1);
    }
    uint32_t flags = 0x05550000;
    memcpy(f->block + 28, &flags, 4);
    set64(f->players[0] + 0x6db8, 1);
    set64(f->players[1] + 0x6db8, 2);
    set64(f->manager + 0x40, (uintptr_t)f->entry);
    f->manager[0x5c] = 1;
    set64(f->entry + 0x50, 3);
    *ops = (selection_parity_ops){f, authenticate, read_value, map_value,
        (uintptr_t)f->players[0], (uintptr_t)f->players[1],
        (uintptr_t)&f->registry_global, (uintptr_t)&f->manager_global};
}
int main(void) {
    fixture f; selection_parity_ops ops; selection_parity_snapshot out, before;
    setup(&f, &ops);
    assert(player_selection_parity_collect(&ops, &out));
    assert(out.player1.status == SELECTION_PARITY_VALID && out.player1.saved == 101 && out.player1.portable == 101);
    assert(out.player2.status == SELECTION_PARITY_VALID && !out.player2.saved && out.player2.portable == 202);
    assert(out.ui.status == SELECTION_PARITY_VALID && out.ui.mapping_available && !out.ui.portable);
    assert(out.player1.raw != out.player2.raw && out.player2.raw != out.ui.raw);
    setup(&f, &ops); set64(f.players[0] + 0x6db8, 0); set64(f.manager + 0x40, 0);
    assert(player_selection_parity_collect(&ops, &out) && out.player1.status == SELECTION_PARITY_ABSENT &&
           out.ui.status == SELECTION_PARITY_ABSENT && !out.ui.source);
    setup(&f, &ops); set64(f.players[1] + 0x6db8, UINT64_C(1) << 25);
    assert(player_selection_parity_collect(&ops, &out) && out.player2.status == SELECTION_PARITY_INVALID);
    setup(&f, &ops); set64(f.players[1] + 0x6db8, (UINT64_C(1) << 25) | 2);
    assert(player_selection_parity_collect(&ops, &out) && out.player2.status == SELECTION_PARITY_INVALID);
    setup(&f, &ops); set64(f.objects[1] + 8, 99);
    assert(player_selection_parity_collect(&ops, &out) && out.player2.status == SELECTION_PARITY_INVALID);
    setup(&f, &ops); f.unreadable = (uintptr_t)(f.players[0] + 0x6db8);
    assert(player_selection_parity_collect(&ops, &out) && out.player1.status == SELECTION_PARITY_UNREADABLE);
    setup(&f, &ops); f.manager[0x5c] = 0;
    assert(player_selection_parity_collect(&ops, &out) && out.ui.status == SELECTION_PARITY_UNAVAILABLE);
    setup(&f, &ops); f.registry_global = 0;
    assert(player_selection_parity_collect(&ops, &out) && out.ui.status == SELECTION_PARITY_UNAVAILABLE);
    setup(&f, &ops); f.map_available = 0;
    assert(player_selection_parity_collect(&ops, &out) && out.player1.status == SELECTION_PARITY_VALID &&
           !out.player1.mapping_available && !out.player1.saved && !out.player1.portable);
    setup(&f, &ops); f.drift_source = 1;
    assert(player_selection_parity_collect(&ops, &out) && out.player1.status == SELECTION_PARITY_DRIFT);
    setup(&f, &ops); f.drift_registry = 1;
    assert(player_selection_parity_collect(&ops, &out) && out.player1.status == SELECTION_PARITY_DRIFT);
    setup(&f, &ops); f.reject_auth = 2; memset(&out, 0xab, sizeof out); before = out;
    assert(!player_selection_parity_collect(&ops, &out) && !memcmp(&out, &before, sizeof out));
    setup(&f, &ops); ops.player2 = ops.player1;
    assert(!player_selection_parity_collect(&ops, &out));
    setup(&f, &ops); ops.player1 = UINTPTR_MAX - 8;
    assert(player_selection_parity_collect(&ops, &out) && out.player1.status == SELECTION_PARITY_UNREADABLE);
    return 0;
}

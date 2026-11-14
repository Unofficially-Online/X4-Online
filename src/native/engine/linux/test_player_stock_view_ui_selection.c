#include "player_stock_view_ui_selection.h"
#include <assert.h>
#include <stdio.h>
typedef struct {
    unsigned char manager[0x60], entry[0x60]; uintptr_t manager_ptr;
    player_stock_view_capture_identity identity;
    unsigned auth_calls, get_calls, active, fail_read, drift_handle, unresolved;
    uint64_t handle;
} fixture;
static int auth(void *context, const player_stock_view_capture_identity *id) {
    fixture *f = context; ++f->auth_calls;
    return f->active && id->generation == f->identity.generation && id->local_slot == f->identity.local_slot &&
        id->player == f->identity.player && id->controller == f->identity.controller;
}
static int read_value(void *context, uintptr_t at, void *out, size_t size) {
    fixture *f = context;
    if (f->fail_read || at < (uintptr_t)f || size > sizeof *f || at - (uintptr_t)f > sizeof *f - size) return 0;
    memcpy(out, (void *)at, size); return 1;
}
static int get(void *context, player_stock_view_softtarget *out) {
    fixture *f = context; ++f->get_calls;
    out->id = f->unresolved ? 0 : f->handle; out->name = ""; out->message = 7;
    if (f->drift_handle) { uint64_t other = 88; memcpy(f->entry + 0x50, &other, 8); }
    return 1;
}
static void setup(fixture *f, player_stock_view_ui_ops *ops) {
    memset(f, 0, sizeof *f); f->active = 1; f->handle = 77;
    f->identity = (player_stock_view_capture_identity){42, 0x1111, 0x2222, 0x3333, 2};
    f->manager_ptr = (uintptr_t)f->manager; f->manager[0x5c] = 1;
    uintptr_t entry = (uintptr_t)f->entry; memcpy(f->manager + 0x40, &entry, 8);
    memcpy(f->entry + 0x50, &f->handle, 8);
    *ops = (player_stock_view_ui_ops){f, auth, read_value, get, (uintptr_t)&f->manager_ptr};
}
int main(void) {
    fixture f; player_stock_view_ui_ops ops; player_stock_view_ui_selection selected; uint64_t runtime;
    setup(&f, &ops); assert(player_stock_view_ui_select(&selected, &ops, &f.identity));
    assert(f.get_calls == 1 && player_stock_view_ui_target(&selected, &f.identity, &runtime) && runtime == 77);
    assert(player_stock_view_ui_target(&selected, &f.identity, &runtime) && f.get_calls == 1);
    assert(player_stock_view_ui_revalidate(&selected) && f.get_calls == 2);
    f.unresolved = 1; assert(!player_stock_view_ui_revalidate(&selected)); f.unresolved = 0;
    player_stock_view_capture_identity wrong = f.identity; wrong.generation++;
    assert(!player_stock_view_ui_target(&selected, &wrong, &runtime));
    wrong = f.identity; wrong.local_slot = 1; assert(!player_stock_view_ui_target(&selected, &wrong, &runtime));
    f.active = 0; assert(!player_stock_view_ui_target(&selected, &f.identity, &runtime));
    setup(&f, &ops); f.unresolved = 1; assert(!player_stock_view_ui_select(&selected, &ops, &f.identity));
    setup(&f, &ops); f.drift_handle = 1; assert(!player_stock_view_ui_select(&selected, &ops, &f.identity));
    setup(&f, &ops); f.fail_read = 1; assert(!player_stock_view_ui_select(&selected, &ops, &f.identity));
    setup(&f, &ops); f.manager_ptr = 0; assert(!player_stock_view_ui_select(&selected, &ops, &f.identity));
    setup(&f, &ops); f.manager[0x5c] = 0; assert(!player_stock_view_ui_select(&selected, &ops, &f.identity));
    setup(&f, &ops); f.handle = 0; memset(f.entry + 0x50, 0, 8);
    assert(player_stock_view_ui_select(&selected, &ops, &f.identity));
    assert(player_stock_view_ui_target(&selected, &f.identity, &runtime) && runtime == 0);
    puts("local UI softtarget lease: PASS"); return 0;
}

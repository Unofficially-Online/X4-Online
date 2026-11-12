#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "player_presentation_probe.h"

static unsigned char player[0x6aac], room[0x80];
static unsigned live_calls, cached_calls, releases, fail_read, skip_increment, over_release;
static int read_test(void *ctx, uintptr_t at, void *out, size_t n) {
    (void)ctx;
    if (fail_read && at == (uintptr_t)room + 0x40 && cached_calls > releases) return 0;
    if ((at >= (uintptr_t)player && at + n <= (uintptr_t)player + sizeof player) ||
        (at >= (uintptr_t)room && at + n <= (uintptr_t)room + sizeof room)) {
        memcpy(out, (void *)at, n); return 1;
    }
    return 0;
}
static uintptr_t live_test(void *ctx, uintptr_t p) { (void)ctx; (void)p; ++live_calls; return (uintptr_t)room; }
static uintptr_t cached_test(void *ctx, uintptr_t p) {
    (void)ctx; (void)p; ++cached_calls; if (!skip_increment) ++*(uint32_t *)(room + 0x40); return (uintptr_t)room;
}
static void release_test(void *ctx, uintptr_t p) {
    (void)ctx; assert(p == (uintptr_t)room); ++releases; --*(uint32_t *)(room + 0x40); if (over_release) --*(uint32_t *)(room + 0x40);
}
static int room_test(void *ctx, uintptr_t p) { (void)ctx; return p == (uintptr_t)room; }
int main(void) {
    const presentation_probe_ops ops = {NULL, read_test, live_test, cached_test, release_test, room_test};
    presentation_probe_row row;
    uintptr_t r = (uintptr_t)room;
    memcpy(player + 0x70, &r, 8); memcpy(player + 0xe0, &r, 8);
    *(uint32_t *)(room + 0x40) = 7; *(uint32_t *)(room + 0x44) = 2;
    assert(presentation_probe_sample(&ops, (uintptr_t)player, 0, r, &row));
    assert(row.cached_called && row.live_called && row.refs_before == 7 && row.refs_acquired == 8 && row.refs_after == 7 && releases == 1);
    fail_read = 1;
    assert(!presentation_probe_sample(&ops, (uintptr_t)player, 0, r, &row));
    assert(releases == 2 && *(uint32_t *)(room + 0x40) == 7);
    fail_read = 0;
    uintptr_t zero = 0; memcpy(player + 0xe0, &zero, 8);
    assert(presentation_probe_sample(&ops, (uintptr_t)player, 0, r, &row));
    assert(row.cached_omitted && !row.cached_called && releases == 2);
    assert(!presentation_probe_sample(&ops, (uintptr_t)player, 2, r, &row));
    memcpy(player + 0xe0, &r, 8); *(uint32_t *)(room + 0x44) = 1;
    assert(!presentation_probe_sample(&ops, (uintptr_t)player, 0, r, &row));
    assert(cached_calls == 2 && releases == 2);
    *(uint32_t *)(room + 0x44) = 2;
    *(uint32_t *)(room + 0x40) = UINT32_MAX;
    assert(!presentation_probe_sample(&ops, (uintptr_t)player, 0, r, &row));
    assert(cached_calls == 2 && releases == 2);
    *(uint32_t *)(room + 0x40) = 7;
    skip_increment = 1;
    assert(!presentation_probe_sample(&ops, (uintptr_t)player, 0, r, &row));
    assert(releases == 3);
    skip_increment = 0;
    *(uint32_t *)(room + 0x40) = 7;
    over_release = 1;
    assert(!presentation_probe_sample(&ops, (uintptr_t)player, 0, r, &row));
    assert(releases == 4);
    puts("presentation probe: ok");
}

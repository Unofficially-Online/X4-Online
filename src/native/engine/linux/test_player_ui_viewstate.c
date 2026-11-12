#include "player_ui_viewstate.h"
#include <assert.h>
#include <stdio.h>

static unsigned char memory[0x5000];
static int change_length;
static unsigned length_reads;
static int fixture_read(void *unused, uintptr_t at, void *out, size_t size) {
    (void)unused;
    uintptr_t base = (uintptr_t)memory;
    if (at < base || at - base > sizeof memory || size > sizeof memory - (at - base)) return 0;
    memcpy(out, (void *)at, size);
    if (at == base + 0x13e8 && ++length_reads == 2 && change_length) *(size_t *)out = 3;
    return 1;
}
static void set_word(size_t offset, uintptr_t value) { memcpy(memory + offset, &value, 8); }
int main(void) {
    uintptr_t universe = (uintptr_t)memory, presenter = universe + 0x800;
    uintptr_t controller = universe + 0x1000, camera = universe + 0x2000, target = universe + 0x3000;
    uint32_t selector = 1, mode = 7;
    memcpy(memory + 0x868, &selector, 4);
    set_word(0x13e0, universe + 0x1800);
    set_word(0x13e8, 7);
    memcpy(memory + 0x1800, "cockpit", 7);
    set_word(0x3e8, camera);
    memcpy(memory + 0x2880, &mode, 4);
    set_word(0x2780, target);
    set_word(0x3008, 42);
    player_ui_viewstate sample;
    assert(player_ui_viewstate_sample(fixture_read, NULL, universe, presenter, controller, &sample));
    assert(sample.selector == 1 && !sample.first_person && sample.plan_length == 7 &&
           !memcmp(sample.plan, "cockpit", 7) && sample.camera == camera &&
           sample.camera_mode == 7 && sample.target == target && sample.target_id == 42);
    char row[512];
    assert(player_ui_viewstate_format(row, sizeof row, "before", 1, 2, 3, 4, &sample));
    assert(strstr(row, "stage=before status=observed manager=1 presenter=2 controller=3 universe=4 ") &&
           strstr(row, "selector=1 first=0 plan_len=7 plan=636f636b706974 "));
    assert(!player_ui_viewstate_format(row, sizeof row, "other", 1, 2, 3, 4, &sample));
    assert(!player_ui_viewstate_format(row, 8, "before", 1, 2, 3, 4, &sample));
    set_word(0x13e8, PLAYER_UI_VIEWSTATE_PLAN_MAX + 1);
    assert(!player_ui_viewstate_sample(fixture_read, NULL, universe, presenter, controller, &sample));
    set_word(0x13e8, 7);
    length_reads = 0; change_length = 1;
    assert(!player_ui_viewstate_sample(fixture_read, NULL, universe, presenter, controller, &sample));
    change_length = 0; length_reads = 0;
    set_word(0x13e0, UINTPTR_MAX);
    assert(!player_ui_viewstate_sample(fixture_read, NULL, universe, presenter, controller, &sample));
    set_word(0x13e0, universe + 0x1800);
    set_word(0x3e8, 0);
    assert(player_ui_viewstate_sample(fixture_read, NULL, universe, presenter, controller, &sample));
    assert(!sample.camera && !sample.target && !sample.target_id);
    memory[0x86c] = 2;
    assert(!player_ui_viewstate_sample(fixture_read, NULL, universe, presenter, controller, &sample));
    puts("UI view-state bounded reads PASS");
    return 0;
}

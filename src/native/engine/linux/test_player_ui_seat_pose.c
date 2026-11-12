#include "player_ui_seat_pose.h"
#include <assert.h>

static _Alignas(16) unsigned char memory[0x9000];
static int change_pose;
static unsigned pose_reads;
static int fake_read(void *unused, uintptr_t address, void *out, size_t size) {
    (void)unused;
    uintptr_t base = (uintptr_t)memory;
    if (address < base || address - base > sizeof memory || size > sizeof memory - (address - base)) return 0;
    memcpy(out, (void *)address, size);
    if (address == base + 0x8000 && ++pose_reads == 2 && change_pose) ((uint32_t *)out)[0] ^= 1;
    return 1;
}
static void set_ptr(size_t offset, uintptr_t value) { memcpy(memory + offset, &value, 8); }
int main(void) {
    uintptr_t actor = (uintptr_t)memory, controller = actor + 0x7100, table = actor + 0x7200, pose = actor + 0x8000;
    set_ptr(0x70, actor + 0x7300); set_ptr(0x200, controller); set_ptr(0x7100, table);
    set_ptr(0x188, pose); /* bank 1: 0xe0+0x80+0x28 */
    float identity[16] = {0,0,0,1, 1,0,0,0, 0,1,0,0, 0,0,1,0};
    memcpy(memory + 0x8000, identity, sizeof identity);
    player_ui_seat_pose sample;
    assert(player_ui_seat_sample(fake_read, NULL, actor, 1, &sample));
    assert(sample.controller == controller && sample.vtable == table && sample.pose == pose && sample.finite && sample.rigid);
    char row[768];
    assert(player_ui_seat_format(row, sizeof row, "before", "p1", 1, 2, 1, &sample));
    assert(strstr(row, "finite=1 rigid=1 words="));
    player_ui_seat_pose full = sample;
    full.actor = full.parent = full.controller = full.vtable = full.pose = UINT64_C(0x7fffffffffff);
    full.position = UINT32_MAX;
    assert(player_ui_seat_format(row, sizeof row, "during", "p1", UINT64_C(0x7fffffffffff),
                                 UINT64_C(0x7fffffffffff), 1, &full));
    assert(strlen(row) > 512);
    uint32_t minus_zero = 0x80000000;
    memcpy(memory + 0x8000, &minus_zero, sizeof minus_zero);
    assert(player_ui_seat_sample(fake_read, NULL, actor, 1, &sample));
    assert(sample.raw_words[0] == minus_zero && sample.words[0] == 0);
    pose_reads = 0; change_pose = 1;
    assert(!player_ui_seat_sample(fake_read, NULL, actor, 1, &sample));
    change_pose = 0; pose_reads = 0;
    uint32_t nan_bits = 0x7fc00000;
    memcpy(memory + 0x8000, &nan_bits, sizeof nan_bits);
    assert(player_ui_seat_sample(fake_read, NULL, actor, 1, &sample) && !sample.finite && !sample.rigid);
    set_ptr(0x188, 0);
    assert(player_ui_seat_sample(fake_read, NULL, actor, 1, &sample) && !sample.pose);
    assert(player_ui_seat_format(row, sizeof row, "after", "p2", 1, 2, 1, &sample));
    assert(strstr(row, "finite=-1 rigid=-1 words=- raw=-"));
    set_ptr(0x188, pose + 4);
    assert(!player_ui_seat_sample(fake_read, NULL, actor, 1, &sample));
    set_ptr(0x188, UINTPTR_MAX);
    assert(!player_ui_seat_sample(fake_read, NULL, actor, 1, &sample));
    set_ptr(0x188, pose); set_ptr(0x7100, UINTPTR_MAX);
    assert(!player_ui_seat_sample(fake_read, NULL, actor, 1, &sample));
    assert(!player_ui_seat_sample(fake_read, NULL, actor, 2, &sample));
}

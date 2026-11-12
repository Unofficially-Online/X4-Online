#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uintptr_t lifecycle_discard_controller = 0x1234;
static char recorded[384], failure[64];
static unsigned writes, exits;
static jmp_buf escape;
static uint64_t GetCurrentThreadId(void) { return UINT64_C(0x11223344); }
static void lifecycle_discard_write(const char *message) {
    ++writes;
    snprintf(recorded, sizeof recorded, "%s", message);
}
static _Noreturn void lifecycle_discard_exit(int success, const char *reason) {
    assert(success == 0);
    ++exits;
    snprintf(failure, sizeof failure, "%s", reason);
    longjmp(escape, 1);
}
#include "gate_player_callback_diagnostic.h"

int main(void) {
    for (volatile unsigned slot = 41; slot <= 43; ++slot) {
        writes = exits = 0;
        recorded[0] = failure[0] = 0;
        if (!setjmp(escape)) {
            if (slot == 41) {
                player_physics_slot41((void *)0x777, (void *)(uintptr_t)UINTPTR_MAX, (void *)1);
            } else if (slot == 42) {
                player_physics_slot42((void *)0x777, (const void *)(uintptr_t)UINTPTR_MAX);
            } else {
                player_physics_slot43((void *)0x777, (void *)(uintptr_t)UINTPTR_MAX, (void *)1, 255);
            }
            assert(!"callback returned");
        }
        char expected[384], reason[64];
        snprintf(expected, sizeof expected,
            "X4LAN LIFECYCLE_DISCARD physics=callback slot=%u thread=287454020 controller=777 "
            "expected=1234 argument1=ffffffffffffffff argument2=%x has_mode=%u mode=%u\n",
            slot, slot == 42 ? 0 : 1, slot == 43, slot == 43 ? 255 : 0);
        snprintf(reason, sizeof reason, "physics_callback_%u", slot);
        assert(writes == 1 && exits == 1 && !strcmp(recorded, expected) && !strcmp(failure, reason));
    }
    puts("callback diagnostics preserve raw arguments and fatal refusal PASS");
}

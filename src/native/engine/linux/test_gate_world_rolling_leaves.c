/* Run only the synthetic rolling census fixture, not a game. */
#define main gate_fixture_original_main
#include "test_gate_linux.c"
#undef main
int main(void) { synth_image(); world_identities = calloc(WORLD_IDENTITY_CAPACITY, sizeof(*world_identities)); assert(world_identities); test_world_rolling(); return 0; }

#ifndef X4_GATE_PLAYER_PHYSICS_FRAME_PREFIX_H
#define X4_GATE_PLAYER_PHYSICS_FRAME_PREFIX_H
/* Optional, configured before recurring-mode publication. Initial callers must
 * open and verify the lifecycle log first; later frames use the same open fd. */
static void (*player_physics_frame_prefix)(uint64_t generation);
static inline void player_physics_frame_begin_record(uint64_t generation) {
    if (player_physics_frame_prefix) player_physics_frame_prefix(generation);
    char message[160];
    snprintf(message, sizeof message,
        "X4LAN LIFECYCLE_DISCARD physics=frame tick=%llu event=begin\n",
        (unsigned long long)generation);
    lifecycle_discard_write(message);
}
#endif

#ifndef X4_PLAYER_LIVE_POSE_PRELOAD_H
#define X4_PLAYER_LIVE_POSE_PRELOAD_H
#include <stdint.h>

#define PLAYER_LIVE_POSE_VERSION 1u
#define PLAYER_LIVE_POSE_ROWS 32u

typedef struct {
    uintptr_t caller;
    uint64_t last_move, time_bits;
    uint32_t mode, move_depth, render_depth, before_valid, after_valid;
    unsigned char before[64], after[64];
} player_live_pose_row;

typedef struct {
    uint64_t started, returned, nested, overflow;
    uint64_t phase_move, phase_render, phase_both, phase_other;
    uint64_t mode_zero, mode_one, mode_other;
    uint32_t count, closed;
    player_live_pose_row rows[PLAYER_LIVE_POSE_ROWS];
} player_live_pose_snapshot;

typedef enum {
    PLAYER_LIVE_POSE_OUTSIDE,
    PLAYER_LIVE_POSE_OPEN,
    PLAYER_LIVE_POSE_PAUSED,
    PLAYER_LIVE_POSE_CHANGING,
    PLAYER_LIVE_POSE_SCOPE_COUNT
} player_live_pose_scope;

#define PLAYER_LIVE_POSE_SCOPE_ROWS 2u
typedef struct {
    uint64_t ordinal;
    player_live_pose_row row;
} player_live_pose_scope_row;
typedef struct {
    uint64_t counts[PLAYER_LIVE_POSE_SCOPE_COUNT];
    uint64_t opens, pauses, closes;
    uint32_t state, count[PLAYER_LIVE_POSE_SCOPE_COUNT];
    player_live_pose_scope_row rows[PLAYER_LIVE_POSE_SCOPE_COUNT][PLAYER_LIVE_POSE_SCOPE_ROWS];
} player_live_pose_scope_snapshot;

#define PLAYER_LIVE_CORRECTION_ROWS 2u
typedef struct {
    uint64_t ordinal, last_move, time_bits;
    uintptr_t caller;
    unsigned char raw[64], corrected[64];
} player_live_correction_row;
typedef struct {
    uint64_t calls;
    uint32_t count;
    player_live_correction_row rows[PLAYER_LIVE_CORRECTION_ROWS];
} player_live_correction_snapshot;

typedef struct {
    uint32_t version, installed;
    uintptr_t entry, stub, trampoline, dispatcher, original_slot, entry_frame;
    uint64_t original_word, patched_word;
    uint32_t stub_size, trampoline_size, entry_frame_size, page_size;
} player_live_pose_info;

unsigned x4mp_live_pose_info(player_live_pose_info *out);
int x4mp_live_pose_bind(uint32_t tid, uintptr_t secondary_controller);
int x4mp_live_pose_tick(uint64_t generation);
int x4mp_live_pose_finish(player_live_pose_snapshot *out);
int x4mp_live_pose_scope_begin(unsigned state);
int x4mp_live_pose_scope(unsigned state);
int x4mp_live_pose_scope_result(player_live_pose_scope_snapshot *out);
int x4mp_live_correction_configure(uint32_t tid, const float delta[16],
                                   uintptr_t stack_low, uintptr_t stack_high);
int x4mp_live_correction_result(player_live_correction_snapshot *out);
#endif

#ifndef X4_PLAYER_VIEW_QUERY_PRELOAD_H
#define X4_PLAYER_VIEW_QUERY_PRELOAD_H
#include <stdint.h>
#define PLAYER_VIEW_QUERY_VERSION 1u
#define PLAYER_VIEW_QUERY_ROWS 64u
typedef struct {
    uintptr_t caller;
    uint64_t result, first_tick, last_tick, count;
    uint32_t in_image, probe;
} player_view_query_row;
typedef struct {
    uint64_t ui_calls, worker_calls, other_calls, probe_calls, overflow;
    uint32_t count;
    player_view_query_row rows[PLAYER_VIEW_QUERY_ROWS];
} player_view_query_snapshot;
typedef struct {
    uint32_t version, installed;
    uintptr_t entry, entry_stub, original_trampoline, dispatcher, original_slot, entry_frame;
    uint64_t original_word, patched_word;
    uint32_t entry_stub_size, original_trampoline_size, entry_frame_size, page_size;
} player_view_query_info;
typedef struct {
    uintptr_t object;
    uint64_t entered[2], left[2], queries[2], foreign[2], nested[2], object_mismatch[2];
    uint64_t inflight[2], thread_mismatch[2];
    uint32_t first_tid[2], owner_tid;
    uintptr_t slots[2], original[2], replacement[2], original_slot[2];
    uint32_t installed, closed;
} player_ui_callback_snapshot;
unsigned x4mp_view_query_ui_info(player_ui_callback_snapshot *out);
int x4mp_view_query_ui_callbacks(player_ui_callback_snapshot *out);
int x4mp_view_query_ui_finish(void);
typedef int (*player_ui_overlay_callback)(uintptr_t object);
int x4mp_view_query_ui_bind(uint32_t tid, uintptr_t object,
                            player_ui_overlay_callback enter, player_ui_overlay_callback leave);
int x4mp_view_query_ui_bind_eight(uint32_t tid, uintptr_t object,
                                  player_ui_overlay_callback enter, player_ui_overlay_callback leave);
int x4mp_view_query_ui_bind_count(uint32_t tid, uintptr_t object,
                                  player_ui_overlay_callback enter, player_ui_overlay_callback leave,
                                  unsigned count);
unsigned x4mp_view_query_ui_overlay_progress(void);
unsigned x4mp_view_query_info(player_view_query_info *out);
int x4mp_view_query_configure(uint32_t ui_tid, uint32_t worker_tid);
int x4mp_view_query_phase(uint64_t tick, unsigned probe);
int x4mp_view_query_snapshot(player_view_query_snapshot *out);
int x4mp_view_query_finish(player_view_query_snapshot *out);
#endif

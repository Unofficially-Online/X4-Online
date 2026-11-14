#ifndef X4_PLAYER_STOCK_VIEW_POLICY_GUARD_H
#define X4_PLAYER_STOCK_VIEW_POLICY_GUARD_H
#include <limits.h>
#include <stdatomic.h>
#include <unistd.h>

/* Sequential consistency orders publication against calls that entered while
 * unbound. Callers must leave even when the policy was inactive. An escaped
 * native call conservatively prevents future binding. */
static unsigned stock_view_policy_enter(_Atomic unsigned *state, _Atomic unsigned *inflight) {
    if (atomic_fetch_add_explicit(inflight, 1, memory_order_seq_cst) == UINT_MAX) _exit(223);
    unsigned value = atomic_load_explicit(state, memory_order_seq_cst);
    if (value > 1) _exit(223);
    return value;
}
static void stock_view_policy_leave(_Atomic unsigned *inflight) {
    if (!atomic_fetch_sub_explicit(inflight, 1, memory_order_seq_cst)) _exit(223);
}
static int stock_view_policy_begin_bind(_Atomic unsigned *state, _Atomic unsigned *inflight) {
    unsigned expected = 0;
    if (!atomic_compare_exchange_strong_explicit(state, &expected, 2,
            memory_order_seq_cst, memory_order_seq_cst)) return 0;
    if (atomic_load_explicit(inflight, memory_order_seq_cst)) {
        atomic_store_explicit(state, 0, memory_order_seq_cst);
        return 0;
    }
    return 1;
}
#endif

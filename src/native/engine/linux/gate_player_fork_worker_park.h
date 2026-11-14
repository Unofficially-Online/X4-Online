#ifndef X4_GATE_PLAYER_FORK_WORKER_PARK_H
#define X4_GATE_PLAYER_FORK_WORKER_PARK_H
/* Immutable after recurring configuration publication. Only the exact movement
 * worker may replace the native futex check with its owned resident park. */
static int (*player_fork_owned_worker_park)(long worker_os_tid);
#endif

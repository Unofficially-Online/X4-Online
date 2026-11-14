#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define FS_OFF 0
#define FS_LOCKS 2
#define FS_RP_WORDS 3
static unsigned long main_thread = 7;
static unsigned long caller_thread = 7;
static unsigned long GetCurrentThreadId(void) { return caller_thread; }
static _Atomic unsigned presentation_fork_reservation, presentation_fork_state;
typedef struct { int held; } fs_lock;
static struct { int state; long worker_tid; unsigned char *worker_tls; char reason[40], blocker[64]; unsigned other_running; } fs;
static fs_lock fs_locks[FS_LOCKS];
typedef struct {
    __typeof__(fs) saved_fs;
    fs_lock saved_locks[FS_LOCKS];
    long worker_tid;
    uintptr_t worker_tls;
    uint64_t held_since;
    char quiet_reason[40], quiet_blocker[64];
    unsigned other_running;
} native_presentation_fork;
static native_presentation_fork presentation_fork;
static pthread_mutex_t actual_locks[FS_LOCKS] = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER};
static unsigned words_held, attempts, pauses, scenario, clock_fail, sleep_with_locks;
static uint64_t now_us;
static void fs_release(int words) {
    for (unsigned i=0; i<FS_LOCKS; ++i) if (fs_locks[i].held) {
        assert(!pthread_mutex_unlock(&actual_locks[i])); fs_locks[i].held=0;
    }
    if (words) { assert(words==FS_RP_WORDS && words_held); words_held=0; }
}
static void acquire(void) {
    for (unsigned i=0; i<FS_LOCKS; ++i) {
        assert(!pthread_mutex_lock(&actual_locks[i])); fs_locks[i].held=1;
    }
    words_held=1;
}
static int fs_quiet(uint64_t *held_since) {
    ++attempts;
    if (scenario==2 || (scenario==1 && attempts<3) || (scenario==4 && attempts==1) ||
        scenario==5 || scenario==6 || (scenario==10 && attempts==1)) {
        snprintf(fs.reason,sizeof fs.reason,"census");
        snprintf(fs.blocker,sizeof fs.blocker,"VideoAsset:55:running");
        return 0;
    }
    if (scenario==3) { strcpy(fs.reason,"path_readers"); return 0; }
    if (scenario==4) { strcpy(fs.reason,"census"); strcpy(fs.blocker,"Movement worker:55:not_the_armed_worker"); return 0; }
    if (scenario==10) return 0; /* no new reason: old transient must not be reused */
    if (scenario==11 && attempts==1) { strcpy(fs.reason,"lock"); return 0; }
    acquire(); *held_since=now_us;
    if (scenario==7) now_us=50001;
    if (scenario==8) clock_fail=1;
    if (scenario==13) fs.state=1;
    if (scenario==14) now_us=0;
    if (scenario==9) atomic_store(&presentation_fork_reservation,0);
    if (scenario==12 && attempts==1) fs.other_running=1;
    return 1;
}
static int fixture_clock_gettime(clockid_t clock, struct timespec *value) {
    assert(clock==CLOCK_MONOTONIC);
    if (clock_fail) return -1;
    value->tv_sec=(time_t)(now_us/1000000); value->tv_nsec=(long)(now_us%1000000)*1000;
    return 0;
}
static int fixture_nanosleep(const struct timespec *pause, struct timespec *remaining) {
    (void)remaining;
    for (unsigned i=0; i<FS_LOCKS; ++i) {
        sleep_with_locks |= fs_locks[i].held;
        assert(!pthread_mutex_trylock(&actual_locks[i])); assert(!pthread_mutex_unlock(&actual_locks[i]));
    }
    assert(!words_held && !sleep_with_locks);
    assert(now_us<50000 && pause->tv_sec==0 && pause->tv_nsec>0 && pause->tv_nsec<=200000);
    now_us+=(uint64_t)pause->tv_nsec/1000; ++pauses;
    if (scenario==5) atomic_store(&presentation_fork_reservation,0);
    if (scenario==6) atomic_store(&presentation_fork_state,3);
    return 0;
}
#define clock_gettime fixture_clock_gettime
#define nanosleep fixture_nanosleep
#include "gate_player_presentation_quiet.h"
#undef clock_gettime
#undef nanosleep
static void run(unsigned which, int success, const char *reason) {
    scenario=which; attempts=pauses=clock_fail=sleep_with_locks=0; now_us=which==14 ? 100 : 0;
    memset(&fs,0,sizeof fs); fs.worker_tid=19; strcpy(fs.reason,"saved");
    memset(fs_locks,0,sizeof fs_locks); memset(&presentation_fork,0,sizeof presentation_fork);
    presentation_fork.worker_tid=55; presentation_fork.worker_tls=0x1234;
    atomic_store(&presentation_fork_reservation,1); atomic_store(&presentation_fork_state,1);
    __typeof__(fs) saved=fs;
    fs_lock saved_locks[FS_LOCKS]; memcpy(saved_locks,fs_locks,sizeof saved_locks);
    assert(presentation_fork_quiet_enter(&presentation_fork)==success);
    if (success) {
        assert(words_held && fs_locks[0].held && fs_locks[1].held && fs.worker_tid==55);
        presentation_fork_quiet_leave(&presentation_fork);
    } else if (reason) assert(!strcmp(presentation_fork.quiet_reason,reason));
    assert(!words_held && !sleep_with_locks && !memcmp(&fs,&saved,sizeof fs));
    assert(!memcmp(fs_locks,saved_locks,sizeof fs_locks));
    if (which==2) assert(attempts>1 && now_us==50000);
    if (which==3 || which==7 || which==8 || which==9) assert(attempts==1 && !pauses);
    if (which==4 || which==10) assert(attempts==2 && pauses==1);
}
int main(void) {
    run(1,1,NULL); /* transient running census -> quiet */
    run(2,0,"census"); /* persistent running never passes budget */
    run(3,0,"path_readers"); /* predicate is not retryable */
    run(4,0,"census"); /* identity failure after transient */
    run(5,0,"reservation"); run(6,0,"quiet_state");
    run(7,0,"quiet_budget"); run(8,0,"quiet_clock"); run(9,0,"reservation");
    run(13,0,"quiet_state"); run(14,0,"quiet_clock");
    run(10,0,""); /* stale transient reason is cleared */
    run(11,1,NULL); /* lock busy -> quiet */
    run(12,1,NULL); /* other_running releases acquired words/locks before retry */
    caller_thread=8; assert(!presentation_fork_quiet_enter(&presentation_fork));
    assert(!strcmp(presentation_fork.quiet_reason,"quiet_owner"));
    caller_thread=7;
    assert(!pthread_mutex_lock(&actual_locks[0])); fs_locks[0].held=1;
    unsigned before=attempts;
    assert(!presentation_fork_quiet_enter(&presentation_fork));
    assert(!strcmp(presentation_fork.quiet_reason,"quiet_locks") && attempts==before && fs_locks[0].held);
    assert(!pthread_mutex_unlock(&actual_locks[0])); fs_locks[0].held=0;
    clock_fail=1;
    assert(!presentation_fork_quiet_enter(&presentation_fork));
    assert(!strcmp(presentation_fork.quiet_reason,"quiet_clock") && attempts==before);
    puts("presentation quiet adapter: bounded retries, hard refusals and lock-free pauses ok (synthetic)");
}

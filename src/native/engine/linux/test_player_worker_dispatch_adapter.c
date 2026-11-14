#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include "player_worker_dispatch_adapter.h"
typedef struct {
    player_worker_dispatch_adapter adapter;
    frame_worker_recurring frame;
    pthread_mutex_t mutex;
    unsigned count, pending;
    _Atomic unsigned stage;
    uint64_t ticket;
} fixture;
static uint64_t clock_ms(void *context) {
    (void)context; struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static void pause_ms(void *context) { (void)context; struct timespec t = {0,1000000}; nanosleep(&t, NULL); }
static int snapshot(void *context, unsigned *count, unsigned *maximum) {
    fixture *f = context; assert(!pthread_mutex_lock(&f->mutex));
    *count = f->count; *maximum = 1; assert(!pthread_mutex_unlock(&f->mutex)); return 1;
}
static void initialize(fixture *f) {
    *f = (fixture){.mutex=PTHREAD_MUTEX_INITIALIZER};
    f->adapter.context=f; f->adapter.snapshot=snapshot;
    f->adapter.clock_ms=clock_ms; f->adapter.pause=pause_ms; f->adapter.timeout_ms=2000;
    assert(pwda_request(&f->adapter, 1, 2, 40, &f->frame));
}
static void set_count(fixture *f, unsigned count) {
    assert(!pthread_mutex_lock(&f->mutex)); f->count=count; assert(!pthread_mutex_unlock(&f->mutex));
}
static void *worker(void *context) {
    fixture *f=context;
    atomic_store(&f->stage,1);
    for (uint64_t generation=40; generation<=41; ++generation) {
        assert(pwda_pre_wait(&f->adapter,2,&f->ticket));
        if (generation==40 && f->pending) {
            assert(!f->ticket); set_count(f,0);
            assert(pwda_wait_return(&f->adapter,2,0));
            assert(pwda_pre_wait(&f->adapter,2,&f->ticket));
        }
        assert(f->ticket==generation);
        unsigned count,maximum; assert(snapshot(f,&count,&maximum) && count==1);
        set_count(f,0); /* synthetic original wait consumes exactly one */
        assert(pwda_wait_return(&f->adapter,2,f->ticket));
        assert(pwda_join(&f->adapter,2,f->ticket));
        assert(fwrr_worker_arrive(&f->frame,generation,2)==FWRR_OK);
        fwrr_token token={0};
        while (fwrr_worker_try_enter(&f->frame,generation,2,&token)==FWRR_WAIT) pause_ms(f);
        assert(token.generation==generation);
        assert(fwrr_worker_release(&f->frame,generation,2,&token)==FWRR_OK);
        assert(pwda_tail(&f->adapter,2,&f->ticket));
    }
    atomic_store(&f->stage,2);
    return NULL;
}
static void run_fast_worker(unsigned pending) {
    fixture f; initialize(&f); f.pending=pending; set_count(&f,pending); assert(pwda_post_begin(&f.adapter,1));
    pthread_t thread; assert(!pthread_create(&thread,NULL,worker,&f));
    while (!atomic_load(&f.stage)) pause_ms(&f);
    /* Worker reaches prewait while producer has not returned. */
    pause_ms(&f); assert(pwda_phase(&f.adapter)==PWDA_STARTUP_POST);
    assert(pwda_post_return(&f.adapter,1));
    for (uint64_t generation=40; generation<=41; ++generation) {
        assert(pwda_arm(&f.adapter,generation,1));
        if (generation==40) assert(fwrr_initialize_at(&f.frame,1,2,40));
        assert(fwrr_arm(&f.frame,generation,1)==FWRR_OK); assert(pwda_publish_arm(&f.adapter));
        assert(pwda_post_begin(&f.adapter,1)); set_count(&f,1);
        assert(pwda_post_return(&f.adapter,1));
        assert(fwrr_main_park(&f.frame,generation,1)==FWRR_OK);
        fwrr_result result;
        while ((result=fwrr_main_poll(&f.frame,generation,1))==FWRR_WAIT) pause_ms(&f);
        assert(result==FWRR_MAIN_RETURN_ALLOWED);
    }
    assert(!pthread_join(thread,NULL)); assert(atomic_load(&f.stage)==2 && !f.ticket);
    assert(pwda_phase(&f.adapter)==PWDA_TAIL);
    /* Tail alone is insufficient for new arm; next prewait must acknowledge. */
    f.adapter.timeout_ms=2; assert(!pwda_arm(&f.adapter,42,1));
    pthread_mutex_destroy(&f.mutex);
}
int main(void) {
    run_fast_worker(0); run_fast_worker(1);
    fixture f; initialize(&f); set_count(&f,1);
    assert(pwda_post_begin(&f.adapter,1)); assert(pwda_post_return(&f.adapter,1));
    uint64_t ticket=0; assert(pwda_pre_wait(&f.adapter,2,&ticket) && !ticket);
    assert(pwda_phase(&f.adapter)==PWDA_DRAIN); set_count(&f,0);
    assert(pwda_wait_return(&f.adapter,2,ticket));
    assert(!pwda_post_begin(&f.adapter,1)); /* duplicate startup post */
    initialize(&f); assert(!pwda_pre_wait(&f.adapter,3,&ticket));
    initialize(&f); assert(!pwda_post_begin(&f.adapter,2));
    initialize(&f); assert(!pwda_wait_return(&f.adapter,3,0));
    initialize(&f); assert(!pwda_join(&f.adapter,2,41));
    initialize(&f); f.adapter.timeout_ms=2; assert(!pwda_arm(&f.adapter,40,1));
    initialize(&f); atomic_store(&f.adapter.phase,PWDA_PARKED); assert(!pwda_arm(&f.adapter,41,1));
    initialize(&f); assert(pwda_post_begin(&f.adapter,1));
    assert(pwda_wait_return(&f.adapter,2,0)); /* already entered native startup wait */
    f.adapter.timeout_ms=2; ticket=0; assert(!pwda_pre_wait(&f.adapter,2,&ticket));
    initialize(&f); ticket=40; assert(!pwda_pre_wait(&f.adapter,2,&ticket));
    puts("worker dispatch adapter: synthetic bootstrap, ticket, ordering and timeout ok");
}

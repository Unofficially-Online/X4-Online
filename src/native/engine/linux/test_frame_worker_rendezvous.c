#define _POSIX_C_SOURCE 200809L
#include "frame_worker_rendezvous.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

static uint64_t thread_id(void) { return (uint64_t)(uintptr_t)pthread_self(); }

enum {
    MAIN_GO=1u, WORKER_GO=2u, MAIN_ARRIVED=4u, WORKER_WAITING=8u,
    ENTER_GO=16u, WORKER_ENTERED=32u, POLL_GO=64u, MAIN_WAITING=128u,
    RELEASE_GO=256u, WORKER_RELEASED=512u, RETURN_GO=1024u, MAIN_RETURNED=2048u
};
typedef struct {
    frame_worker_rendezvous rendezvous;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    unsigned events;
    atomic_uint_fast64_t main_id, worker_id;
    atomic_uint main_exited;
    int worker_first;
    /* Written before release-publication, read after peer's acquire. */
    unsigned main_marker, worker_marker;
} fixture;

static void post(fixture *f, unsigned event) {
    assert(pthread_mutex_lock(&f->mutex)==0);
    f->events |= event;
    assert(pthread_cond_broadcast(&f->condition)==0);
    assert(pthread_mutex_unlock(&f->mutex)==0);
}
static void wait_for(fixture *f, unsigned event) {
    struct timespec deadline;
    assert(clock_gettime(CLOCK_REALTIME,&deadline)==0);
    deadline.tv_sec += 3;
    assert(pthread_mutex_lock(&f->mutex)==0);
    while ((f->events & event) != event) {
        int result=pthread_cond_timedwait(&f->condition,&f->mutex,&deadline);
        assert(result==0); /* Missing progress fails this synthetic test boundedly. */
    }
    assert(pthread_mutex_unlock(&f->mutex)==0);
}

static void *main_participant(void *argument) {
    fixture *f=argument;
    uint64_t id=thread_id();
    atomic_store_explicit(&f->main_id,id,memory_order_release);
    post(f,1u<<20);
    wait_for(f,MAIN_GO);
    f->main_marker=101;
    assert(fwr_main_park(&f->rendezvous,5,id)==FWR_ARRIVAL_RECORDED);
    post(f,MAIN_ARRIVED);
    wait_for(f,POLL_GO);
    assert(fwr_main_poll(&f->rendezvous,5,id)==FWR_WAIT);
    assert(!atomic_load_explicit(&f->main_exited,memory_order_acquire));
    post(f,MAIN_WAITING);
    wait_for(f,RETURN_GO);
    assert(fwr_main_poll(&f->rendezvous,5,id)==FWR_MAIN_RETURN_ALLOWED);
    assert(f->worker_marker==202);
    atomic_store_explicit(&f->main_exited,1,memory_order_release);
    post(f,MAIN_RETURNED);
    return NULL;
}

static void *worker_participant(void *argument) {
    fixture *f=argument;
    uint64_t id=thread_id();
    atomic_store_explicit(&f->worker_id,id,memory_order_release);
    post(f,1u<<21);
    wait_for(f,WORKER_GO);
    assert(fwr_worker_arrive(&f->rendezvous,5,id)==FWR_ARRIVAL_RECORDED);
    fwr_entered_token token={0};
    if (f->worker_first) {
        assert(fwr_worker_try_enter(&f->rendezvous,5,id,&token)==FWR_WAIT);
        assert(token.owner==NULL);
        post(f,WORKER_WAITING);
        wait_for(f,ENTER_GO);
    }
    assert(fwr_worker_try_enter(&f->rendezvous,5,id,&token)==FWR_WORKER_ENTERED);
    assert(f->main_marker==101);
    assert(!atomic_load_explicit(&f->main_exited,memory_order_acquire));
    post(f,WORKER_ENTERED);
    wait_for(f,RELEASE_GO);
    f->worker_marker=202;
    assert(fwr_worker_release(&f->rendezvous,5,id,&token)==FWR_RELEASE_PUBLISHED);
    /* Publication alone cannot claim main already returned. */
    assert(!(atomic_load_explicit(&f->rendezvous.state,memory_order_acquire)&FWR_MAIN_AUTHORIZED));
    assert(!atomic_load_explicit(&f->main_exited,memory_order_acquire));
    post(f,WORKER_RELEASED);
    return NULL;
}

static void controlled_race(int worker_first) {
    fixture f={.rendezvous=FRAME_WORKER_RENDEZVOUS_INIT,
               .mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER,
               .worker_first=worker_first};
    pthread_t main_thread,worker_thread;
    assert(pthread_create(&main_thread,NULL,main_participant,&f)==0);
    assert(pthread_create(&worker_thread,NULL,worker_participant,&f)==0);
    wait_for(&f,(1u<<20)|(1u<<21));
    uint64_t main_id=atomic_load_explicit(&f.main_id,memory_order_acquire);
    uint64_t worker_id=atomic_load_explicit(&f.worker_id,memory_order_acquire);
    assert(main_id!=worker_id);
    assert(fwr_arm(&f.rendezvous,5,main_id,worker_id)==FWR_ARMED_OK);
    if (worker_first) {
        post(&f,WORKER_GO);
        wait_for(&f,WORKER_WAITING);
        assert(!(atomic_load_explicit(&f.rendezvous.state,memory_order_acquire)&FWR_ENTERED));
        post(&f,MAIN_GO);
        wait_for(&f,MAIN_ARRIVED);
        post(&f,ENTER_GO);
    } else {
        post(&f,MAIN_GO);
        wait_for(&f,MAIN_ARRIVED);
        post(&f,WORKER_GO);
    }
    wait_for(&f,WORKER_ENTERED);
    post(&f,POLL_GO);
        wait_for(&f,MAIN_WAITING);
    assert(!atomic_load_explicit(&f.main_exited,memory_order_acquire));
    post(&f,RELEASE_GO);
        wait_for(&f,WORKER_RELEASED);
    assert(!atomic_load_explicit(&f.main_exited,memory_order_acquire));
    post(&f,RETURN_GO);
        wait_for(&f,MAIN_RETURNED);
    assert(pthread_join(main_thread,NULL)==0);
    assert(pthread_join(worker_thread,NULL)==0);
    assert(atomic_load_explicit(&f.rendezvous.failure,memory_order_acquire)==FWR_NO_FAILURE);
    assert(pthread_mutex_destroy(&f.mutex)==0);
    assert(pthread_cond_destroy(&f.condition)==0);
}

static void denied(frame_worker_rendezvous *r, uint64_t main_id, uint64_t worker_id) {
    fwr_entered_token token={0};
    assert(atomic_load_explicit(&r->state,memory_order_acquire)&FWR_POISONED);
    assert(fwr_main_poll(r,5,main_id)==FWR_FAILED);
    assert(fwr_worker_try_enter(r,5,worker_id,&token)==FWR_FAILED);
    assert(fwr_worker_release(r,5,worker_id,&token)==FWR_FAILED);
}

static void missing_peer_and_cancel(void) {
    uint64_t id=thread_id();
    frame_worker_rendezvous main_only=FRAME_WORKER_RENDEZVOUS_INIT;
    assert(fwr_arm(&main_only,5,id,id+1)==FWR_ARMED_OK);
    assert(fwr_main_park(&main_only,5,id)==FWR_ARRIVAL_RECORDED);
    assert(fwr_main_poll(&main_only,5,id)==FWR_WAIT);
    assert(fwr_cancel(&main_only,5,id,0,1)==FWR_FAILED);
    assert(atomic_load(&main_only.failure)==FWR_TIMEOUT);
    denied(&main_only,id,id+1);
    frame_worker_rendezvous worker_only=FRAME_WORKER_RENDEZVOUS_INIT;
    assert(fwr_arm(&worker_only,5,id+1,id)==FWR_ARMED_OK);
    assert(fwr_worker_arrive(&worker_only,5,id)==FWR_ARRIVAL_RECORDED);
    fwr_entered_token token={0};
    assert(fwr_worker_try_enter(&worker_only,5,id,&token)==FWR_WAIT);
    assert(fwr_cancel(&worker_only,5,id,1,0)==FWR_FAILED);
    assert(atomic_load(&worker_only.failure)==FWR_CANCELLED);
    denied(&worker_only,id+1,id);
}

static void invalid_registration_and_rearm(void) {
    const uint64_t cases[][3]={{4,11,22},{6,11,22},{5,0,22},{5,11,0},{5,11,11}};
    for (unsigned n=0;n<sizeof(cases)/sizeof(cases[0]);++n) {
        frame_worker_rendezvous r=FRAME_WORKER_RENDEZVOUS_INIT;
        assert(fwr_arm(&r,cases[n][0],cases[n][1],cases[n][2])==FWR_FAILED);
        denied(&r,11,22);
    }
    frame_worker_rendezvous r=FRAME_WORKER_RENDEZVOUS_INIT;
    assert(fwr_arm(&r,5,11,22)==FWR_ARMED_OK);
    assert(fwr_arm(&r,5,11,22)==FWR_FAILED);
    denied(&r,11,22);
}

static void wrong_ticks_threads_and_entry(void) {
    for (unsigned scenario=0;scenario<10;++scenario) {
        frame_worker_rendezvous r=FRAME_WORKER_RENDEZVOUS_INIT;
        fwr_entered_token token={&r,5,22};
        assert(fwr_arm(&r,5,11,22)==FWR_ARMED_OK);
        switch(scenario) {
        case 0: assert(fwr_main_park(&r,4,11)==FWR_FAILED); break;
        case 1: assert(fwr_worker_arrive(&r,6,22)==FWR_FAILED); break;
        case 2: assert(fwr_main_park(&r,5,22)==FWR_FAILED); break;
        case 3: assert(fwr_worker_arrive(&r,5,11)==FWR_FAILED); break;
        case 4: assert(fwr_worker_release(&r,5,22,&token)==FWR_FAILED); break;
        case 5: assert(fwr_main_poll(&r,5,11)==FWR_FAILED); break;
        case 6: assert(fwr_worker_try_enter(&r,5,22,&token)==FWR_FAILED); break;
        case 7:
            assert(fwr_main_park(&r,5,11)==FWR_ARRIVAL_RECORDED);
            assert(fwr_main_park(&r,5,11)==FWR_FAILED); break;
        case 8:
            assert(fwr_worker_arrive(&r,5,22)==FWR_ARRIVAL_RECORDED);
            assert(fwr_worker_arrive(&r,5,22)==FWR_FAILED); break;
        default:
            assert(fwr_main_park(&r,5,11)==FWR_ARRIVAL_RECORDED);
            assert(fwr_worker_arrive(&r,5,22)==FWR_ARRIVAL_RECORDED);
            assert(fwr_worker_try_enter(&r,5,22,&token)==FWR_WORKER_ENTERED);
            assert(fwr_worker_try_enter(&r,5,22,&token)==FWR_FAILED); break;
        }
        denied(&r,11,22);
    }
}

static void invalid_release(void) {
    for (unsigned scenario=0;scenario<8;++scenario) {
        frame_worker_rendezvous r=FRAME_WORKER_RENDEZVOUS_INIT;
        fwr_entered_token token={0};
        assert(fwr_arm(&r,5,11,22)==FWR_ARMED_OK);
        assert(fwr_main_park(&r,5,11)==FWR_ARRIVAL_RECORDED);
        assert(fwr_worker_arrive(&r,5,22)==FWR_ARRIVAL_RECORDED);
        assert(fwr_worker_try_enter(&r,5,22,&token)==FWR_WORKER_ENTERED);
        if (scenario==0) {
            token.owner=NULL;
        }
        if (scenario==1) {
            token.tick=6;
        }
        if (scenario==2) {
            token.worker_thread=33;
        }
        if (scenario<3) {
            assert(fwr_worker_release(&r,5,22,&token)==FWR_FAILED);
        }
        if (scenario==3) {
            assert(fwr_worker_release(&r,5,11,&token)==FWR_FAILED);
        }
        if (scenario==4) {
            assert(fwr_worker_release(&r,5,22,&token)==FWR_RELEASE_PUBLISHED);
            assert(fwr_worker_release(&r,5,22,&token)==FWR_FAILED);
        }
        if (scenario==5) {
            assert(fwr_worker_release(&r,5,22,&token)==FWR_RELEASE_PUBLISHED);
            assert(fwr_cancel(&r,5,11,0,1)==FWR_FAILED);
        }
        if (scenario==6) {
            assert(fwr_cancel(&r,5,22,1,0)==FWR_FAILED);
        }
        if (scenario==7) {
            assert(fwr_cancel(&r,5,11,0,1)==FWR_FAILED);
        }
        denied(&r,11,22);
    }
}

static void *foreign_participant(void *argument) {
    frame_worker_rendezvous *r=argument;
    assert(fwr_main_park(r,5,thread_id())==FWR_FAILED);
    return NULL;
}
static void foreign_pthread(void) {
    frame_worker_rendezvous r=FRAME_WORKER_RENDEZVOUS_INIT;
    uint64_t id=thread_id();
    assert(fwr_arm(&r,5,id,id+1)==FWR_ARMED_OK);
    pthread_t foreign;
    assert(pthread_create(&foreign,NULL,foreign_participant,&r)==0);
    assert(pthread_join(foreign,NULL)==0);
    denied(&r,id,id+1);
}

int main(void) {
    for (unsigned repeat=0;repeat<20;++repeat) {
        controlled_race(0);
        controlled_race(1);
    }
    missing_peer_and_cancel();
    invalid_registration_and_rearm();
    wrong_ticks_threads_and_entry();
    invalid_release();
    foreign_pthread();
    puts("7 rendezvous scenario groups passed (40 controlled pthread schedules)");
    return 0;
}

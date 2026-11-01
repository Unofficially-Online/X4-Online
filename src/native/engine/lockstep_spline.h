/* Fixture trace, native obstacle refresh and bounded result join for spline flight.
 * Raw model bytes contain pointers and are diagnostic, never portable hashes. */
#define SPLINE_ENTRY 0x2c4f4f0u
#define SPLINE_UPDATE 0xeb7230u
typedef struct {
    uint64_t tick,calls,time,cheap,applied_cheap,player,waited;
    uint32_t dt,model[2][0x4b0/4],slot[2][0x1a0/4],params[0x1a0/4];
    uint64_t pending_count[2];
    uint32_t pending_status[2],pending_flags[2];
} spline_sample;
static spline_sample spline_samples[ACTOR_CAPACITY];
static int spline_installed,spline_restored,spline_refresh,spline_join;
static uint64_t spline_calls,spline_failures,spline_refreshes,spline_joins,spline_waits,spline_timeouts;
/* native_gate_spline_join_all (DEFAULT OFF; needs native_gate_spline_join, so the producer pins and the pathing
 * join): every spline model's update, not only the fixtures', first waits (bounded, SPLINE_JOIN_ALL_TIMEOUT) until
 * the front of its pending request deque is published, so the native update consumes each route in the first
 * update after its request on every client. Cause B (4-client long runs: p4-long3, p4-long5, p4-causeb): courier
 * a1885 leaves station 3d876's pad and starts a path at 12812 identically on all clients; its next update (12813)
 * gives another pose on one client with target, path, agent, flight parameters, cache and previous pose equal: the
 * spline request the path started (slot 4 0xeb55a0 -> calculator queue 0xeb6760) was published before that update
 * on one client and after it on the other, which then flies its provisional segment one tick longer. The calculator
 * thread (0x1099650 -> 0xec0cb0) is one FIFO worker; four X4 processes on one PC starve it unevenly. The wait copies
 * nothing, cancels nothing and runs no job: the native calculator produces the route, the native update consumes
 * it. A timeout, an unreadable deque, an unknown status or an unavailable calculator stops the gate (reason 58). */
#define SPLINE_JOIN_ALL_TIMEOUT 2000u
static int spline_join_all;
static uint64_t spline_all_joins,spline_all_waits,spline_all_timeouts,spline_all_failures;
/* The motion watch's request rows for watched ships (native_gate_motion_watch_splines; lockstep_motion_watch.h):
 * begin matches the model's agent against the watch (pointer compare), seen reads the front request the native
 * update will see (after any join), end reads what the update left. */
typedef struct {unsigned slot;uint32_t count_in,status,flags,points,result,ordinal;unsigned char cheap,waited;} spline_watch_t;
static int motion_spline_begin(void *model,unsigned char cheap,spline_watch_t *w);
static void motion_spline_seen(void *model,spline_watch_t *w);
static void motion_spline_end(void *model,spline_watch_t *w);

/* The front of a spline model's pending request deque (+0x428: proxy, map, map size, offset, size; two request
 * pointers per block, power-of-two block map) and the front request's status (+0xa8: 0 pending, 1 published by the
 * calculator 0x1099650 or set inline by the request builder, slot 4 0xeb55a0) and flags (+0xac..+0xaf), read under
 * the native result critical section the calculator publishes under. Update 0xeb7230 consumes the front request
 * (its route replaces the provisional segments, spline progress restarts) in the first update that finds it
 * published, so the consumption tick follows wall-clock completion unless an update waits for it. */
typedef struct {uint64_t count;uint32_t status,flags;unsigned char *front;} spline_front_t;
static int spline_front(void *model,spline_front_t *f) {
    uint64_t queue[5];void *block,*request;
    f->count=0;f->status=0;f->flags=0;f->front=NULL;
    if(!read_local((unsigned char*)model+0x428,queue,sizeof(queue))) return 0;
    f->count=queue[4];
    if(!queue[4]) return 1;
    /* Native deque: two request pointers per block, power-of-two block map.
     * The pathing owner controls the deque. The result worker publishes its
     * status/flags under the same critical section used by native Update. */
    if(!queue[1] || !queue[2] || queue[2]>4096 || (queue[2]&(queue[2]-1)) || queue[4]>queue[2]*2 ||
       !read_local((void*)(uintptr_t)(queue[1]+8*((queue[3]>>1)&(queue[2]-1))),&block,8) || !block ||
       !read_local((unsigned char*)block+8*(queue[3]&1),&request,8) || !request) return 0;
    f->front=request;
    EnterCriticalSection((CRITICAL_SECTION*)(image+0x3d74938));
    int ok=read_local((unsigned char*)request+0xa8,&f->status,4) &&
           read_local((unsigned char*)request+0xac,&f->flags,4);
    LeaveCriticalSection((CRITICAL_SECTION*)(image+0x3d74938));
    return ok;
}
static int spline_pending(spline_sample *s,void *model,unsigned phase) {
    spline_front_t f;int ok=spline_front(model,&f);
    s->pending_count[phase]=f.count;s->pending_status[phase]=f.status;s->pending_flags[phase]=f.flags;
    return ok;
}

/* Wait (bounded) until the front request is published: 1 when none is pending or it is published (f holds it),
 * 0 on an unreadable deque, an unknown status, an unavailable calculator or a timeout. *waited: it had to wait. */
static int spline_join_core(void *model,unsigned timeout,spline_front_t *f,int *waited,
                            volatile LONG64 *joins,volatile LONG64 *waits,volatile LONG64 *timeouts) {
    *waited=0;
    if(!spline_front(model,f)) return 0;
    if(!f->count) return 1;
    InterlockedIncrement64(joins);
    if(f->status>1) return 0;
    if(f->status) return 1;
    void *worker,*vtable;unsigned char running;
    if(!read_local(image+0x6cf29c0,&worker,8) || !worker ||
       !read_local(worker,&vtable,8) || vtable!=image+0x2c68f98 ||
       !read_local((unsigned char*)worker+0x58,&running,1) || running!=1) return 0;
    *waited=1;InterlockedIncrement64(waits);
    ULONGLONG started=GetTickCount64();
    /* The native calculator owns result production. Release its publication
     * lock between reads and allow it to finish; never run or cancel its job.
     * The pathing owner cannot consume this request until we return. */
    while(!f->status) {
        if(GetTickCount64()-started>=timeout) {
            InterlockedIncrement64(timeouts);return 0;
        }
        Sleep(1);
        if(!spline_front(model,f) || !f->count || f->status>1) return 0;
    }
    return 1;
}
static int spline_join_pending(spline_sample *s,void *model,unsigned timeout) {
    spline_front_t f;int waited=0;
    int ok=spline_join_core(model,timeout,&f,&waited,(volatile LONG64*)&spline_joins,(volatile LONG64*)&spline_waits,(volatile LONG64*)&spline_timeouts);
    s->waited=(uint64_t)waited;s->pending_count[0]=f.count;s->pending_status[0]=f.status;s->pending_flags[0]=f.flags;
    return ok;
}

static int spline_state(void *model,void *agent,uint32_t values[4]) {
    void *vtable,*owner;
    if(!read_local(model,&vtable,8) || vtable!=image+SPLINE_ENTRY-0x70 ||
       !read_local((unsigned char*)model+0x10,&owner,8) || owner!=agent ||
       !read_local((unsigned char*)model+0xd0,values,16)) return 0;
    for(unsigned i=0;i<4;i++) {float f;memcpy(&f,values+i,4);if(!isfinite(f)) return 0;}
    return 1;
}

static void spline_observe(flight_fn original,spline_sample *s,void *self,void *agent,
                           void *slot,float dt,double time,unsigned char cheap,unsigned char player) {
    if(s->tick!=stats.issued) {s->tick=stats.issued;s->calls=0;}
    ++s->calls;s->cheap=cheap;s->applied_cheap=spline_refresh?0:cheap;s->player=player;
    s->waited=0;
    memcpy(&s->dt,&dt,4);memcpy(&s->time,&time,8);
    if(!read_local(self,s->model[0],sizeof(s->model[0])) ||
       !read_local(slot,s->slot[0],sizeof(s->slot[0])) ||
       !read_local((unsigned char*)agent+0x340,s->params,sizeof(s->params)) || !spline_pending(s,self,0))
        InterlockedIncrement64((volatile LONG64*)&spline_failures);
    if(spline_join && !spline_join_pending(s,self,1000))
        InterlockedIncrement64((volatile LONG64*)&spline_failures);
    if(spline_refresh) {
        /* Same native eaf660 helper as steering, but spline stores its obstacle
         * publication time at +108 and vector at +f0. Force fresh geometry;
         * native contact membership and all avoidance calculations stay native. */
        *(double*)((unsigned char*)self+0x108)=-1.0;
        InterlockedIncrement64((volatile LONG64*)&spline_refreshes);
    }
    original(self,slot,dt,time,(unsigned char)s->applied_cheap,player);
    if(!read_local(self,s->model[1],sizeof(s->model[1])) ||
       !read_local(slot,s->slot[1],sizeof(s->slot[1])) || !spline_pending(s,self,1))
        InterlockedIncrement64((volatile LONG64*)&spline_failures);
    InterlockedIncrement64((volatile LONG64*)&spline_calls);
}
/* Every spline update that is not a fixture's: the join for all (gated frames only) and the watch's request rows. */
static void spline_other(flight_fn original,void *self,void *slot,float dt,double time,unsigned char cheap,unsigned char player,int gated,unsigned timeout) {
    spline_watch_t w;int watched=motion_spline_begin(self,cheap,&w);
    if(gated && spline_join_all) {
        spline_front_t f;int waited=0;
        if(!spline_join_core(self,timeout,&f,&waited,(volatile LONG64*)&spline_all_joins,(volatile LONG64*)&spline_all_waits,
                             (volatile LONG64*)&spline_all_timeouts)) {
            InterlockedIncrement64((volatile LONG64*)&spline_all_failures);InterlockedIncrement64((volatile LONG64*)&spline_failures);
        }
        w.waited=(unsigned char)waited;
    }
    if(watched) motion_spline_seen(self,&w);
    world_trace_event *event=world_trace_begin(WT_SPLINE,self,slot,dt,time,cheap,player);
    original(self,slot,dt,time,cheap,player);world_trace_end(event);
    if(watched) motion_spline_end(self,&w);
}
static void spline_update(void *self,void *slot,float dt,double time,unsigned char cheap,unsigned char player) {
    void *registry_agent;
    if(registry_is_observing() && read_local((unsigned char*)self+0x10,&registry_agent,8)) registry_observe_owner(registry_agent,0xef0,REGISTRY_SPLINE);
    flight_fn original=(flight_fn)(image+SPLINE_UPDATE);
    void *agent=NULL,*model=NULL;
    int gated=stats.active && boot_phase==3;
    if(gated && read_local((unsigned char*)self+0x10,&agent,8)) {
        for(unsigned i=0;i<ACTOR_CAPACITY;i++) if(agent_samples[i].agent==agent && agent_identity(i,agent)) {
            if(!read_local((unsigned char*)agent+0xf10,&model,8) || model!=self) {
                InterlockedIncrement64((volatile LONG64*)&spline_failures);break;
            }
            spline_observe(original,&spline_samples[i],self,agent,slot,dt,time,cheap,player);return;
        }
    }
    spline_other(original,self,slot,dt,time,cheap,player,gated,SPLINE_JOIN_ALL_TIMEOUT);
}
/* Before the first admitted frame, on the MoveWorker: needs the installed spline hook with the join's pins. */
static int spline_join_all_arm(void) {
    if(spline_join_all) return 1;
    if(!spline_installed || !spline_join || !stats.active || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    spline_join_all=1;return 1;
}
static int spline_swap(void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+SPLINE_ENTRY,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)(image+SPLINE_ENTRY),value,expected);
    VirtualProtect(image+SPLINE_ENTRY,8,old,&ignored);return actual==expected;
}
static int spline_install(int refresh,int join) {
    if(!boot_mode || !agent_installed || GetModuleHandleW(L"x4lan_fixedtick_64.dll") ||
       !hash_ok(image+SPLINE_UPDATE,16379,"bf700855ab64122b1142dcf3fceda6f41ea67318f70186e0b2944d9dd5bdfcac") ||
       (refresh && (!agent_full || !hash_ok(image+0xeaf660,3671,"36298ef739653940d9d234e26cd1250fac0fc0a700b5c85ca955ba685a3ba54b"))) ||
       (join && (!agent_full || !(path_options&1) ||
        !hash_ok(image+0x1099650,629,"8a7a84c0e938f6077cf072946cb6050e594a3a0341c937dd58546e7179ce1ab9") ||
        !hash_ok(image+0xec0cb0,11611,"7e3e1114c607d8ea4717ae26dc7516471ea9aaf3dd0130b761151bda1faa7ffb") ||
        !hash_ok(image+0xeb6760,90,"06cb3e7172633ce43392cdb8921e50a8cf394c9616711b5504b91dce30ee8f73") ||
        !hash_ok(image+0xeb55a0,3420,"4966db7097c45d251774f62b51dc2896cb57157860c3b6a4c0bc21b4e88c4f21"))) ||
       !spline_swap(image+SPLINE_UPDATE,(void*)spline_update)) return 0;
    spline_refresh=refresh;spline_join=join;spline_installed=1;return 1;
}
static void spline_restore(void) {
    if(!spline_installed) return;
    spline_restored=spline_swap((void*)spline_update,image+SPLINE_UPDATE);
    if(spline_restored) spline_installed=0;
}

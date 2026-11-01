/* Census diagnostic, DEFAULT OFF: native_gate_wreck_watch = true (needs native_gate_motion_watch; Windows only).
 *
 * reg-tour20 / reg-tour19wd (tour, 2 clients): the first relay mismatch (2492) is the wreck of 176ad5, client 2's
 * slot ship (battle actor 3), dead since 2465. Its controller (object+0x200) is a U::LinearDecelerationController
 * (LDC, vtable 0x2c3f5f0, 0xc0 bytes): start transform +0x20..+0x60, start time +0x60 (double), linear and angular
 * velocity +0x80/+0x90 (vec4), duration +0xa0 (double), flag +0xa8 (angular velocity kept). Its pose is a closed form
 * of the time it is asked for (vfunc6 0xe27580) and so are its velocities (vfunc7 0xe288e0: v * (1 - tau / D),
 * the angular part too unless the flag). The death listener (KillEvent, ShipSmall slot 8 0x7cd610 -> Destructible
 * slot 8 0x516ae0) builds it from the ship's own velocity (slot 0x1450) plus keyed random kicks (lockstep_death.h).
 * From 2466 to 2491 the wreck has no physics body on either client (slot 2's view scene was released when the slot
 * died; STATE_ROW body0:none). At 2492 slot 1's tour returns to the battle sector, attention 2 -> 7, a body is made
 * and Scene::Step's pre-sync (0xba7ba -> 0x3dc040, GetVelocityRelativeTo at the MoveWorker's game time) gives it the
 * LDC's velocity: pose, linear velocity, the start time and the duration agree between clients (equal per-tick decay
 * from 2493, 150 ticks left), the angular velocity does not, and the values are bit-identical per client in
 * reg-tour19wd and reg-tour20 (client 1 3ae9c3b7:b838b008:3b14f8ce, client 2 3aeeaf4c:b93770b5:3b167a1d): a per-client
 * deterministic writer, not a race. The rows do not show the LDC itself, so the writer is not named yet; these rows
 * name it in one run.
 *
 * WRECK rows (every admitted frame in slot 6, after the motion watch): one per watched ship whose controller is an
 * LDC or whose controller object changed since the last frame (the ship's death, a new controller): tick, saved,
 * attention, ctrl (vtable rva), seq (controller objects seen for the ship, this client), ldc= (the LDC fields above:
 * 0x40 + 8 + 0x20 + 8 + 1 bytes, '-' for another controller) and body= (the physics record object+0x260: the
 * velocities the last pre-sync set +0x40..+0x60 and the last write-back's delta +0x60..+0x80; '-' without a body).
 * WRECK_WRITE rows (at once, any thread): each call of a write path of a watched ship's LDC. LDC vtable entries
 * swapped by CAS: slot 8 set_velocity (0x8c10f0, +0x80/+0x90), slot 22 physics (0xe23de0: the Jolt write-back
 * 0xbfb00 hands the body's velocity back when it changed: start transform at the thread's game time, then slot 8),
 * slot 25 force (0xe23e70, the write-back's accumulated force), slot 34 rebase (0xe28a00, start pose and velocities
 * at the thread's game time, duration shortened); the virtual base's slot 3 set_transform (0xe83c60, a vtordisp
 * thunk); and U::ShipSmall slot 829 set_controller (0x6cd8b0: a new controller for a watched ShipSmall; its caller
 * is the code that built it). Row: tick, saved, kind, depth (writes nested in another write on this thread), ui
 * (MoveWorker), now (the thread's game time TLS+0x300), chain (four image return addresses), arg (set_velocity /
 * physics: the 8 velocity floats; force: the 8 force floats and the step; set_transform: the translation and first
 * row, then the time as a double; set_controller: the new controller's vtable rva), before= and after= (LDC fields as in WRECK; set_controller: the old and the new controller).
 * A controller is matched by pointer against the watched ships' controllers as the last frame or set_controller
 * saw them (no reads for other objects). Exclusive with the transition hooks (they own ShipSmall slot 829); refused
 * when any entry is not the original. Pins: each entry's target (bytes) and the LDC's GetVelocity (the layout).
 * Restore at stop. */
#define WRECK_LDC_VTABLE 0x2c3f5f0u   /* U::LinearDecelerationController */
#define WRECK_LDC_VBASE 0x2c3f788u    /* its virtual base's vtable (offset 0xb8) */
#define WRECK_SHIP_SMALL 0x2b7feb8u   /* U::ShipSmall */
#define WRECK_LAYOUT 0xe288e0u        /* LDC vfunc7 GetVelocity: reads +0x60, +0x80, +0x90, +0xa0, +0xa8 */
#define WRECK_STATE 0x71u             /* transform 0x40, start 8, velocities 0x20, duration 8, flag 1 */
#define WRECK_RING 4096u
#define WRECK_WRITE_RING 8192u
#define WRECK_ENTRIES 6u
enum {WRECK_SET_VELOCITY,WRECK_PHYSICS,WRECK_FORCE,WRECK_REBASE,WRECK_SET_TRANSFORM,WRECK_SET_CONTROLLER};
static const struct {unsigned entry,target,size;const char *name,*sha;} wreck_entries[WRECK_ENTRIES]={
    {WRECK_LDC_VTABLE+8u*8u,0x8c10f0u,0x16u,"set_velocity","cdd3e3640a06896f392d0ec91872e3103efd95ef6990f0421efb4186ea831b5f"},
    {WRECK_LDC_VTABLE+22u*8u,0xe23de0u,0x8au,"physics","af7466b076d0e786fe148dc3cb5d2241ff59efe86c27e05d7c2dfcea401149ab"},
    {WRECK_LDC_VTABLE+25u*8u,0xe23e70u,0x15du,"force","5665e0093e7d0ca08655d17fc73bf82c404fda5f248fb1207ff42896610d3096"},
    {WRECK_LDC_VTABLE+34u*8u,0xe28a00u,0x1c5u,"rebase","8e00dccee85fdf0c4a5ce757a6b549834221e0c1147cea6690d7e089d112d468"},
    {WRECK_LDC_VBASE+3u*8u,0xe83c60u,0x10u,"set_transform","702f53843643264c74d385f02437bf8411c490a0bdeecce5bfcf97e7f15c6933"},
    {WRECK_SHIP_SMALL+0x19e8u,0x6cd8b0u,0x27u,"set_controller","9317be9e33cdf30695ab01f4dfb062d85b836a872a3019c103cf9279c4ff2a32"}};
#define WRECK_LAYOUT_SIZE 0x11eu
#define WRECK_LAYOUT_SHA "7c12a422d468ce634fde3a13a78827794a39608d4cea6b0b7daf48174f4af4e5"
typedef struct {volatile LONG64 ready;uint64_t tick,saved;int attention;uint32_t ctrl_vt,seq,flags;unsigned char ldc[WRECK_STATE],body[0x40];} wreck_row_t;
typedef struct {volatile LONG64 ready;uint64_t tick,saved,now;uint32_t kind,depth,ui,chain[4],arg_vt,flags;
    unsigned char arg[40],before[WRECK_STATE],after[WRECK_STATE];} wreck_write_t;
static void *volatile wreck_object[MOTION_WATCH_MAX],*volatile wreck_ctrl[MOTION_WATCH_MAX],*volatile wreck_vbase[MOTION_WATCH_MAX];
static uint32_t wreck_seq[MOTION_WATCH_MAX];
static wreck_row_t *wreck_ring;
static wreck_write_t *wreck_write_ring;
static volatile LONG64 wreck_rows,wreck_writes;
static unsigned wreck_swapped;
static int wreck_armed;
static uint64_t wreck_misses;
static _Thread_local unsigned wreck_depth;
/* The LDC's own fields (WRECK_STATE bytes); 0 when unreadable. */
static int wreck_state(const unsigned char *ctrl,unsigned char out[WRECK_STATE]) {
    if(ctrl && read_local(ctrl+0x20,out,0x40) && read_local(ctrl+0x60,out+0x40,8) && read_local(ctrl+0x80,out+0x48,0x20) &&
       read_local(ctrl+0xa0,out+0x68,8) && read_local(ctrl+0xa8,out+0x70,1)) return 1;
    memset(out,0,WRECK_STATE);return 0;
}
static int wreck_is_ldc(const unsigned char *ctrl) {
    void *vt=NULL;return ctrl && read_local(ctrl,&vt,8) && vt==(void*)(image+WRECK_LDC_VTABLE);
}
/* The virtual base subobject a vtordisp thunk gets: ctrl + 0x10 + [[ctrl+0x10]+4]. */
static void *wreck_vbase_of(unsigned char *ctrl) {
    unsigned char *table=NULL;int32_t offset=0;
    if(!ctrl || !read_local(ctrl+0x10,&table,8) || !table || !read_local(table+4,&offset,4)) return NULL;
    return ctrl+0x10+offset;
}
static int wreck_index_ctrl(const void *ctrl) {
    if(!ctrl) return -1;
    for(unsigned i=0;i<motion_watch_count;i++) if(wreck_ctrl[i]==ctrl) return (int)i;
    return -1;
}
static int wreck_index_vbase(const void *vbase) {
    if(!vbase) return -1;
    for(unsigned i=0;i<motion_watch_count;i++) if(wreck_vbase[i]==vbase) return (int)i;
    return -1;
}
static int wreck_index_object(const void *object) {
    if(!object) return -1;
    for(unsigned i=0;i<motion_watch_count;i++) if(wreck_object[i]==object) return (int)i;
    return -1;
}
static void wreck_track(unsigned i,unsigned char *object,unsigned char *ctrl) {
    if(ctrl!=wreck_ctrl[i]) ++wreck_seq[i];
    wreck_object[i]=object;wreck_vbase[i]=wreck_is_ldc(ctrl)?wreck_vbase_of(ctrl):NULL;wreck_ctrl[i]=ctrl;
}
/* Slot 6 of every admitted frame, after motion_watch_frame (which resolves the watched IDs). */
static int wreck_frame(uint64_t tick) {
    if(!wreck_armed) return 1;
    if(!stats.active || GetCurrentThreadId()!=ui_thread || tick!=stats.issued) return 0;
    for(unsigned i=0;i<motion_watch_count;i++) {
        uint64_t id=0;unsigned char *object=motion_watch_runtime[i]?motion_watch_lookup(motion_watch_runtime[i]):NULL,*ctrl=NULL;
        if(!object || !read_local(object+8,&id,8) || id!=motion_watch_runtime[i]) {
            if(wreck_object[i]) {wreck_object[i]=wreck_ctrl[i]=wreck_vbase[i]=NULL;}
            continue;
        }
        if(!read_local(object+0x200,&ctrl,8)) {++wreck_misses;continue;}
        const int changed=ctrl!=wreck_ctrl[i] || object!=wreck_object[i],ldc=wreck_is_ldc(ctrl);
        wreck_track(i,object,ctrl);
        if(!ldc && !changed) continue;
        LONG64 index=InterlockedIncrement64(&wreck_rows)-1;wreck_row_t *r=&wreck_ring[(uint64_t)index%WRECK_RING];
        InterlockedExchange64(&r->ready,0);
        r->tick=tick;r->saved=motion_watch_saved[i];r->seq=wreck_seq[i];r->flags=0;r->attention=0;r->ctrl_vt=0xffffffffu;
        read_local(object+0x1ec,&r->attention,4);
        void *vt=NULL;if(ctrl && read_local(ctrl,&vt,8)) r->ctrl_vt=motion_watch_rva(vt);
        if(ldc && wreck_state(ctrl,r->ldc)) r->flags|=1;else memset(r->ldc,0,WRECK_STATE);
        unsigned char *record=NULL;memset(r->body,0,sizeof(r->body));
        if(read_local(object+0x260,&record,8) && record && read_local(record+0x40,r->body,0x40)) r->flags|=2;
        InterlockedExchange64(&r->ready,index+1);
    }
    return 1;
}
/* A write row: begin before the original runs (before= and the argument), end after it (after=). */
typedef struct {wreck_write_t *row;LONG64 index;const unsigned char *ctrl;} wreck_pending;
static int wreck_begin(wreck_pending *p,unsigned kind,int watched,const unsigned char *ctrl,const void *arg,unsigned arg_size) {
    p->row=NULL;p->index=-1;p->ctrl=ctrl;
    if(watched<0 || !wreck_write_ring) return 0;
    LONG64 index=InterlockedIncrement64(&wreck_writes)-1;wreck_write_t *r=&wreck_write_ring[(uint64_t)index%WRECK_WRITE_RING];
    InterlockedExchange64(&r->ready,0);
    r->tick=stats.issued;r->saved=motion_watch_saved[watched];r->kind=kind;r->depth=wreck_depth;r->ui=GetCurrentThreadId()==ui_thread;
    r->now=0;r->arg_vt=0xffffffffu;r->flags=0;memset(r->arg,0,sizeof(r->arg));
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    if(tls && tls[0]) read_local((unsigned char*)tls[0]+0x300,&r->now,8);
    if(arg && arg_size && arg_size<=sizeof(r->arg) && read_local(arg,r->arg,arg_size)) r->flags|=4;
    if(wreck_is_ldc(ctrl) && wreck_state(ctrl,r->before)) r->flags|=1;else memset(r->before,0,WRECK_STATE);
    motion_chain(0,r->chain,4);
    p->row=r;p->index=index;++wreck_depth;return 1;
}
static void wreck_end(wreck_pending *p,const unsigned char *after) {
    if(!p->row) return;
    --wreck_depth;
    if(wreck_is_ldc(after) && wreck_state(after,p->row->after)) p->row->flags|=2;else memset(p->row->after,0,WRECK_STATE);
    InterlockedExchange64(&p->row->ready,p->index+1);
}
typedef void (*wreck_velocity_fn)(void *,const void *);
typedef void (*wreck_physics_fn)(void *,const void *,const void *);
typedef uint64_t (*wreck_force_fn)(void *,const void *,float);
typedef uint64_t (*wreck_rebase_fn)(void *,void *,void *);
typedef uint64_t (*wreck_transform_fn)(void *,const void *,double);
typedef uint64_t (*wreck_controller_fn)(void *,void *,uint64_t,uint64_t);
static void *wreck_original(unsigned k) {return image+wreck_entries[k].target;}
static void wreck_set_velocity(void *ctrl,const void *velocity) {
    wreck_pending p;wreck_begin(&p,WRECK_SET_VELOCITY,wreck_index_ctrl(ctrl),ctrl,velocity,32);
    ((wreck_velocity_fn)wreck_original(WRECK_SET_VELOCITY))(ctrl,velocity);wreck_end(&p,ctrl);
}
static void wreck_physics(void *ctrl,const void *transform,const void *velocity) {
    wreck_pending p;wreck_begin(&p,WRECK_PHYSICS,wreck_index_ctrl(ctrl),ctrl,velocity,32);
    ((wreck_physics_fn)wreck_original(WRECK_PHYSICS))(ctrl,transform,velocity);wreck_end(&p,ctrl);
}
static uint64_t wreck_force(void *ctrl,const void *force,float step) {
    wreck_pending p;
    if(wreck_begin(&p,WRECK_FORCE,wreck_index_ctrl(ctrl),ctrl,force,32)) {memcpy(p.row->arg+32,&step,4);}
    uint64_t result=((wreck_force_fn)wreck_original(WRECK_FORCE))(ctrl,force,step);wreck_end(&p,ctrl);return result;
}
static uint64_t wreck_rebase(void *ctrl,void *a,void *b) {
    wreck_pending p;wreck_begin(&p,WRECK_REBASE,wreck_index_ctrl(ctrl),ctrl,NULL,0);
    uint64_t result=((wreck_rebase_fn)wreck_original(WRECK_REBASE))(ctrl,a,b);wreck_end(&p,ctrl);return result;
}
static uint64_t wreck_set_transform(void *vbase,const void *transform,double time) {
    const int i=wreck_index_vbase(vbase);unsigned char *ctrl=i>=0?(unsigned char*)wreck_ctrl[i]:NULL;wreck_pending p;
    if(wreck_begin(&p,WRECK_SET_TRANSFORM,i,ctrl,transform,32)) {memcpy(p.row->arg+32,&time,8);}
    uint64_t result=((wreck_transform_fn)wreck_original(WRECK_SET_TRANSFORM))(vbase,transform,time);wreck_end(&p,ctrl);return result;
}
static uint64_t wreck_set_controller(void *object,void *ctrl,uint64_t a,uint64_t b) {
    const int i=wreck_index_object(object);wreck_pending p;
    unsigned char *old=i>=0?(unsigned char*)wreck_ctrl[i]:NULL;
    if(wreck_begin(&p,WRECK_SET_CONTROLLER,i,old,NULL,0)) {
        void *vt=NULL;if(ctrl && read_local(ctrl,&vt,8)) p.row->arg_vt=motion_watch_rva(vt);
    }
    uint64_t result=((wreck_controller_fn)wreck_original(WRECK_SET_CONTROLLER))(object,ctrl,a,b);
    if(i>=0) {
        unsigned char *now=NULL;if(read_local((unsigned char*)object+0x200,&now,8)) wreck_track((unsigned)i,object,now);
        wreck_end(&p,now);
    }
    return result;
}
static void *wreck_hook(unsigned k) {
    void *h[WRECK_ENTRIES]={(void*)wreck_set_velocity,(void*)wreck_physics,(void*)wreck_force,(void*)wreck_rebase,(void*)wreck_set_transform,(void*)wreck_set_controller};
    return h[k];
}
static int wreck_swap(unsigned k,void *expected,void *value) {
    unsigned char *p=image+wreck_entries[k].entry;DWORD old,ignored;
    if(!VirtualProtect(p,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)p,value,expected);
    VirtualProtect(p,8,old,&ignored);return actual==expected;
}
static int wreck_pins(void) {
    for(unsigned k=0;k<WRECK_ENTRIES;k++) if(!hash_ok(image+wreck_entries[k].target,wreck_entries[k].size,wreck_entries[k].sha)) return 0;
    return hash_ok(image+WRECK_LAYOUT,WRECK_LAYOUT_SIZE,WRECK_LAYOUT_SHA);
}
static int wreck_owned(void) {
    for(unsigned k=0;k<WRECK_ENTRIES;k++) if((wreck_swapped&(1u<<k)) && *(void**)(image+wreck_entries[k].entry)!=wreck_hook(k)) return 0;
    return 1;
}
static void wreck_restore(void) {
    for(unsigned k=0;k<WRECK_ENTRIES;k++)
        if((wreck_swapped&(1u<<k)) && wreck_swap(k,wreck_hook(k),wreck_original(k))) wreck_swapped&=~(1u<<k);
}
/* After the watch list, before the first admitted frame, on the MoveWorker. */
static int wreck_arm(void) {
    if(wreck_armed) return 1;
    if(!motion_watch_armed || !motion_watch_count || stats.issued || GetCurrentThreadId()!=ui_thread || !wreck_pins()) return 0;
    for(unsigned k=0;k<WRECK_ENTRIES;k++) if(*(void**)(image+wreck_entries[k].entry)!=wreck_original(k)) return 0;
    if(!wreck_ring) wreck_ring=VirtualAlloc(NULL,sizeof(wreck_row_t)*WRECK_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!wreck_write_ring) wreck_write_ring=VirtualAlloc(NULL,sizeof(wreck_write_t)*WRECK_WRITE_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!wreck_ring || !wreck_write_ring) return 0;
    for(unsigned k=0;k<WRECK_ENTRIES;k++) {
        if(!wreck_swap(k,wreck_original(k),wreck_hook(k))) {wreck_restore();return 0;}
        wreck_swapped|=1u<<k;
    }
    wreck_armed=1;return 1;
}
static int wreck_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&wreck_rows,0,0);int n;
    if(!wreck_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)WRECK_RING && index<(uint64_t)(count-(LONG64)WRECK_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const wreck_row_t *r=&wreck_ring[index%WRECK_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index+1) return 0;
    char ldc[2*WRECK_STATE+1],body[0x81];
    if(r->flags&1) motion_watch_hex(ldc,r->ldc,WRECK_STATE);else strcpy(ldc,"-");
    if(r->flags&2) motion_watch_hex(body,r->body,0x40);else strcpy(body,"-");
    n=snprintf(out,cap,"index=%llu tick=%llu saved=%016llx attention=%d ctrl=%08x seq=%u ldc=%s body=%s",(unsigned long long)index,
        (unsigned long long)r->tick,(unsigned long long)r->saved,r->attention,r->ctrl_vt,r->seq,ldc,body);
    return n>0 && (size_t)n<cap?n:0;
}
static int wreck_write_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&wreck_writes,0,0);int n;
    if(!wreck_write_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)WRECK_WRITE_RING && index<(uint64_t)(count-(LONG64)WRECK_WRITE_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const wreck_write_t *r=&wreck_write_ring[index%WRECK_WRITE_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index+1) return 0;
    char arg[2*40+1],before[2*WRECK_STATE+1],after[2*WRECK_STATE+1];
    if(r->kind==WRECK_SET_CONTROLLER) snprintf(arg,sizeof(arg),"%08x",r->arg_vt);
    else if(r->flags&4) motion_watch_hex(arg,r->arg,r->kind==WRECK_FORCE?36u:r->kind==WRECK_SET_TRANSFORM?40u:32u);else strcpy(arg,"-");
    if(r->flags&1) motion_watch_hex(before,r->before,WRECK_STATE);else strcpy(before,"-");
    if(r->flags&2) motion_watch_hex(after,r->after,WRECK_STATE);else strcpy(after,"-");
    n=snprintf(out,cap,"index=%llu tick=%llu saved=%016llx kind=%s depth=%u ui=%u now=%016llx chain=%08x,%08x,%08x,%08x arg=%s before=%s after=%s",
        (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->saved,r->kind<WRECK_ENTRIES?wreck_entries[r->kind].name:"?",
        r->depth,r->ui,(unsigned long long)r->now,r->chain[0],r->chain[1],r->chain[2],r->chain[3],arg,before,after);
    return n>0 && (size_t)n<cap?n:0;
}
static int wreck_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"armed=%d swapped=%x rows=%lld writes=%lld misses=%llu",wreck_armed,wreck_swapped,(long long)wreck_rows,
        (long long)wreck_writes,(unsigned long long)wreck_misses);
    return n>0 && (size_t)n<cap?n:0;
}

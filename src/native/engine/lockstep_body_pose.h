/* Determinism fix, DEFAULT OFF: native_gate_body_pose = 'sim' | 'observe' (Windows only).
 *
 * reg-tour21 (tour, 2 clients; reg-tour19wd/20 alike, the values bit-identical per client): client 2's slot ship 176ad5
 * dies at 2465 (U::LinearDecelerationController, start time 89.216333, lockstep_wreck_watch.h) and has no physics body
 * from 2466 (attention 2). At 2492 attention 7 makes its two bodies (records +0x260, +0x268) and of the 683 bodies new
 * in that step only these two have PHYSICS_BODY 'pre' states that differ between the clients, in position and
 * rotation; a bullet's impulse that step (0xd528f0 -> 0x51b050 AddImpulse -> 0xbf230 -> LDC slot 22) turns that into
 * different angular velocities (the WRECK rows' writer; relay mismatch from 2492). Each body sits on the LDC's closed
 * form at a stale game time, not at the frame's 90.149667: client 1 at 89.249667 (start + 1 tick, tick 2465's time),
 * client 2 at 89.316333 (start + 3 ticks, 2467's): rotations 1.7336 and 5.1668 degrees from the start rotation, as
 * |w| (tau - tau^2/2D) gives for those tau, centres of mass within 0.3 mm of one body-fixed offset.
 * The pose comes from U::Destructible::CreateBody 0x51b730 (from CreatePhysicsBodies 0x51bb90, ShipSmall slot 904,
 * e.g. on the attention change 0x51aa10). The matrix it hands XPhys::CreateBodyRecord 0xbe550 is either
 * U::Component::GetTransformRelativeTo 0x3db8a0(component, m, TLS game time +0x300, space, 0) at 0x51b820 (view-cache
 * flag 0x40000 set, or no class-0xf ancestor in the space's cached parent chain), or 0x3dde10(component, m, space, 0)
 * at 0x51b975: the composition from each link's PUBLISHED view-cache pose block (object+0xe0+[0x38f4ee0 on the
 * MoveWorker]*0x80, +0x28; on a miss the link's own transform at the TLS time). For an object that no step moves, that
 * block holds the pose of its last publication, which each client made at another tick, so each client creates the
 * body where its own last publication left the wreck.
 *
 * 'sim': the call at 0x51b975 goes through a stub. On the MoveWorker in an admitted frame it becomes 0x3db8a0(component,
 * m, the MoveWorker's game time TLS+0x300, space, 0): CreateBody's own other branch, the composition up the live parent
 * chain (+0x70) from each link's own transform (slot 0x1440; the LDC's closed form) at the frame's time, the same on
 * every client (diverted=). The native call still runs, into a copy, for the comparison (differs=: the bodies this
 * changes). Native call (counted): outside an admitted frame or the gate (outside=), a flag argument or a == b
 * (trivial=), another thread (other=). 'observe' computes both and keeps the native result.
 * Rows (BODY_POSE_ROW): calls where the two results differ (kind=sim or observe) and every call off the MoveWorker in an
 * admitted frame (kind=other); at most 256 per tick (dropped=). Keys are portable (saved or created ID of the component
 * or its nearest keyed ancestor; the space likewise), ctrl= the component's controller vtable RVA, sim= the result at
 * the frame's time, native= the published-view result.
 * Fail closed: pins of CreateBody (.pdata extent), 0x3dde10 and 0x3db8a0; the site must hold its original rel32; an
 * unreadable or uninitialised TLS time or an exception in the composition counts a failure (that call runs natively).
 * The site is swapped by CAS, other modules' pins see the original bytes (lockstep_gate.c fp_patch_view), owned and
 * failure-free every gate tick (stop reason 181), restored at stop. */
#define BPOSE_CREATE 0x51b730u        /* U::Destructible::CreateBody */
#define BPOSE_CREATE_SIZE 0x45au
#define BPOSE_CREATE_SHA "0bb67cf7e7bcb025d298191a4afef4d16004f33a30ab31b7f0b7dc99225f76dc"
#define BPOSE_SITE 0x51b975u          /* call 0x3dde10(component, m, space, 0): the view-cache branch */
#define BPOSE_TLS_INIT 0x10u
#define BPOSE_TLS_TIME 0x300u
#define BPOSE_RING 4096u
#define BPOSE_ROWS_PER_TICK 256u
#define BPOSE_STOP_REASON 181u        /* 180 path transforms */
enum {BPOSE_OFF,BPOSE_SIM,BPOSE_OBSERVE};
enum {BPOSE_ROW_SIM,BPOSE_ROW_OBSERVE,BPOSE_ROW_OTHER};
static ptf_hier_fn bpose_hier_native;    /* tests only: replaces 0x3dde10 */
static ptf_gtr_fn bpose_gtr_native;      /* tests only: replaces 0x3db8a0 */
static unsigned char *bpose_test_tls;    /* tests only: the calling thread's TLS block */
static int bpose_mode,bpose_installed,bpose_restored,bpose_done;
static volatile LONG bpose_active;
static unsigned char *bpose_page;
static const char *bpose_stage="idle";
static volatile LONG64 bpose_calls,bpose_outside,bpose_trivial,bpose_other,bpose_diverted,bpose_differs,bpose_failures,
    bpose_rows,bpose_dropped,bpose_tick_rows;
static double bpose_last_time;
typedef struct {volatile LONG64 ready;uint64_t tick;unsigned kind;ptf_ref_t a,b;uint32_t ctrl_vt;double time;
    float sim[16],native[16];} bpose_row_t;
static bpose_row_t *bpose_ring;
static int bpose_on(void) {return InterlockedCompareExchange(&bpose_active,0,0)!=0;}
static uint32_t bpose_ctrl_vt(const unsigned char *component) {
    unsigned char *controller=NULL;uintptr_t vt=0;
    if(!component || !read_local(component+PTF_CONTROLLER,&controller,8) || !controller || !read_local(controller,&vt,8)) return 0;
    const unsigned char *v=(const unsigned char*)pslot_canonical_vtable(vt);
    return v>=image && v<image+0x6fa9000?(uint32_t)(v-image):0xffffffffu;
}
static void bpose_row(unsigned kind,const unsigned char *a,const unsigned char *b,double time,const float *sim,const float *native) {
    if(!bpose_ring) return;
    if(InterlockedIncrement64(&bpose_tick_rows)>(LONG64)BPOSE_ROWS_PER_TICK) {InterlockedIncrement64(&bpose_dropped);return;}
    LONG64 index=InterlockedIncrement64(&bpose_rows)-1;
    bpose_row_t *r=&bpose_ring[(uint64_t)index%BPOSE_RING];
    InterlockedExchange64(&r->ready,0);
    r->tick=stats.issued;r->kind=kind;r->a=ptf_ref(a);r->b=ptf_ref(b);r->ctrl_vt=bpose_ctrl_vt(a);r->time=time;
    if(sim) memcpy(r->sim,sim,sizeof(r->sim));else memset(r->sim,0,sizeof(r->sim));
    memcpy(r->native,native,sizeof(r->native));
    InterlockedExchange64(&r->ready,index+1);
}
/* One call of the site: moveworker whether this thread is the MoveWorker, tls its TLS block. */
static void *bpose_call_using(void *a,float *out,void *b,uint64_t flag,int moveworker,const unsigned char *tls,ptf_hier_fn native,ptf_gtr_fn gtr) {
    InterlockedIncrement64(&bpose_calls);
    if(!bpose_on() || !stats.active || boot_phase!=3) {InterlockedIncrement64(&bpose_outside);return native(a,out,b,flag);}
    if((flag&0xff) || !a || !b || a==b || !out) {InterlockedIncrement64(&bpose_trivial);return native(a,out,b,flag);}
    if(!moveworker) {
        InterlockedIncrement64(&bpose_other);
        void *result=native(a,out,b,flag);
        bpose_row(BPOSE_ROW_OTHER,a,b,0,NULL,out);
        return result;
    }
    double time=0;unsigned char ready=0;
    if(!tls || !read_local(tls+BPOSE_TLS_INIT,&ready,1) || !ready || !read_local(tls+BPOSE_TLS_TIME,&time,8) || !isfinite(time)) {
        bpose_stage="tls_time";InterlockedIncrement64(&bpose_failures);return native(a,out,b,flag);
    }
    __declspec(align(16)) float sim[16];memcpy(sim,out,sizeof(sim));
    int ok=1;
    __try {gtr(a,sim,time,b,0);} __except(EXCEPTION_EXECUTE_HANDLER) {ok=0;}
    if(!ok) {bpose_stage="compose";InterlockedIncrement64(&bpose_failures);return native(a,out,b,flag);}
    bpose_last_time=time;
    if(bpose_mode==BPOSE_OBSERVE) {
        void *result=native(a,out,b,flag);
        if(memcmp(sim,out,sizeof(sim))) {InterlockedIncrement64(&bpose_differs);bpose_row(BPOSE_ROW_OBSERVE,a,b,time,sim,out);}
        return result;
    }
    __declspec(align(16)) float stock[16];memcpy(stock,out,sizeof(stock));
    native(a,stock,b,flag);
    InterlockedIncrement64(&bpose_diverted);
    memcpy(out,sim,sizeof(sim));
    if(memcmp(sim,stock,sizeof(sim))) {InterlockedIncrement64(&bpose_differs);bpose_row(BPOSE_ROW_SIM,a,b,time,sim,stock);}
    return out;
}
static void *bpose_hook(void *a,float *out,void *b,uint64_t flag) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    const unsigned char *tls=bpose_test_tls?bpose_test_tls:slots?(const unsigned char*)slots[0]:NULL;
    return bpose_call_using(a,out,b,flag,GetCurrentThreadId()==ui_thread,tls,
        bpose_hier_native?bpose_hier_native:(ptf_hier_fn)(void*)(image+PTF_HIER),bpose_gtr_native?bpose_gtr_native:(ptf_gtr_fn)(void*)(image+PTF_GTR));
}
/* The stub at page+0: jmp [rip] -> bpose_hook (the caller's return address stays CreateBody's). */
static void bpose_write_code(unsigned char *p) {
    static const unsigned char jump[6]={0xff,0x25,0,0,0,0};void *hook=(void*)bpose_hook;
    memset(p,0xcc,0x10);memcpy(p,jump,6);memcpy(p+6,&hook,8);
}
static int32_t bpose_rel(const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+BPOSE_SITE+5));}
/* 0 original, 1 ours, -1 anything else. Read raw: read_local would show the view. */
static int bpose_site_state(void) {
    unsigned char now[5];int32_t d;
    memcpy(now,(const void*)(image+BPOSE_SITE),5);
    if(now[0]!=0xe8) return -1;
    memcpy(&d,now+1,4);
    if(d==bpose_rel(image+PTF_HIER)) return 0;
    if(bpose_page && d==bpose_rel(bpose_page)) return 1;
    return -1;
}
static unsigned char *bpose_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+BPOSE_SITE)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t d=(intptr_t)p-(intptr_t)(image+BPOSE_SITE+5);
            if(d>INT32_MIN+0x10000 && d<INT32_MAX-0x10000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int bpose_site_cas(const void *expected,const void *value) {
    const int32_t e=bpose_rel(expected),v=bpose_rel(value);unsigned char eb[4],vb[4];memcpy(eb,&e,4);memcpy(vb,&v,4);
    return view_cas_bytes(BPOSE_SITE+1,eb,vb,4);
}
/* ---- original-byte view (lockstep_gate.c fp_patch_view) ---- */
static int bpose_overlaps(const void *source,size_t bytes) {
    if(!*(volatile int*)&bpose_done || !image || !source) return 0;
    const uintptr_t a=(uintptr_t)source,b=a+bytes,s=(uintptr_t)(image+BPOSE_SITE+1);
    return s<b && s+4>a;
}
static void bpose_unpatch_copy(const void *source,void *copy,size_t bytes) {
    if(!bpose_overlaps(source,bytes)) return;
    const int32_t d=bpose_rel(image+PTF_HIER);unsigned char rel[4];memcpy(rel,&d,4);
    const uintptr_t a=(uintptr_t)source,s=(uintptr_t)(image+BPOSE_SITE+1);
    for(unsigned k=0;k<4;k++) if(s+k>=a && s+k<a+bytes) ((unsigned char*)copy)[s+k-a]=rel[k];
}
static int bpose_pinned(void) {
    return hash_ok(image+BPOSE_CREATE,BPOSE_CREATE_SIZE,BPOSE_CREATE_SHA) && hash_ok(image+PTF_HIER,PTF_HIER_SIZE,PTF_HIER_SHA) &&
           hash_ok(image+PTF_GTR,PTF_GTR_SIZE,PTF_GTR_SHA);
}
static void bpose_restore(void) {
    if(!bpose_installed) return;
    InterlockedExchange(&bpose_active,0);
    if(bpose_done && bpose_site_state()==1 && bpose_site_cas(bpose_page,image+PTF_HIER)) bpose_done=0;
    if(!bpose_done) {bpose_installed=0;bpose_restored=1;bpose_stage="restored";}
    else bpose_stage="restore_foreign";
}
/* gate.body_pose(mode): the UI thread (MoveWorker), before the first admitted frame. */
static int bpose_arm(int mode) {
    if(bpose_installed) return stats.active && mode==bpose_mode; /* an identical rerun after a loading-UI reload */
    if((mode!=BPOSE_SIM && mode!=BPOSE_OBSERVE) || !stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) {
        bpose_stage="state";return 0;
    }
    if(bpose_site_state()!=0) {bpose_stage="site";return 0;}
    if(!bpose_pinned()) {bpose_stage="pins";return 0;}
    if(!bpose_ring) bpose_ring=VirtualAlloc(NULL,sizeof(bpose_row_t)*BPOSE_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!bpose_ring) {bpose_stage="ring";return 0;}
    if(!bpose_page) bpose_page=bpose_alloc_page();
    if(!bpose_page) {bpose_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(bpose_page,0x1000,PAGE_READWRITE,&old)) {bpose_stage="page";return 0;}
    bpose_write_code(bpose_page);
    if(!VirtualProtect(bpose_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {bpose_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),bpose_page,0x1000);
    bpose_mode=mode;bpose_installed=1;bpose_restored=0;bpose_done=0;InterlockedExchange(&bpose_active,1);
    if(!bpose_site_cas(image+PTF_HIER,bpose_page)) {bpose_restore();bpose_stage="cas";return 0;}
    bpose_done=1;bpose_stage="armed";return 1;
}
static int bpose_owned(void) {
    if(!bpose_installed) return 1;
    if(bpose_site_state()!=1) return 0;
    unsigned char want[0x10];bpose_write_code(want);
    return bpose_page && !memcmp(bpose_page,want,sizeof(want));
}
/* Gate tick (main thread, no frame running): any failure or a changed site stops the gate with reason 181. */
static int bpose_check(void) {
    InterlockedExchange64(&bpose_tick_rows,0);
    if(InterlockedCompareExchange64(&bpose_failures,0,0)) return 0;
    if(!bpose_owned()) {bpose_stage="site_changed";InterlockedIncrement64(&bpose_failures);return 0;}
    return 1;
}
static int bpose_row_text(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&bpose_rows,0,0);int n;
    if(!bpose_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)BPOSE_RING && index<(uint64_t)(count-(LONG64)BPOSE_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const bpose_row_t *r=&bpose_ring[index%BPOSE_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index+1) return 0;
    static const char *const kinds[3]={"sim","observe","other"};
    char a[40],b[40],sim[129],native[129];uint64_t time;memcpy(&time,&r->time,8);
    ptf_ref_text(a,r->a);ptf_ref_text(b,r->b);
    motion_watch_hex(sim,(const unsigned char*)r->sim,64);motion_watch_hex(native,(const unsigned char*)r->native,64);
    n=snprintf(out,cap,"index=%llu tick=%llu kind=%s a=%s b=%s ctrl=%08x time=%016llx sim=%s native=%s",
        (unsigned long long)index,(unsigned long long)r->tick,kinds[r->kind<3?r->kind:0],a,b,r->ctrl_vt,(unsigned long long)time,
        r->kind==BPOSE_ROW_OTHER?"-":sim,native);
    return n>0 && (size_t)n<cap?n:0;
}
static int bpose_status(char *out,size_t cap) {
    uint64_t time;memcpy(&time,&bpose_last_time,8);
    int n=snprintf(out,cap,"mode=%s installed=%d restored=%d stage=%s calls=%lld outside=%lld trivial=%lld other=%lld diverted=%lld "
        "differs=%lld rows=%lld dropped=%lld failures=%lld time=%016llx",
        bpose_mode==BPOSE_SIM?"sim":bpose_mode==BPOSE_OBSERVE?"observe":"off",bpose_installed,bpose_restored,bpose_stage,
        (long long)bpose_calls,(long long)bpose_outside,(long long)bpose_trivial,(long long)bpose_other,(long long)bpose_diverted,
        (long long)bpose_differs,(long long)bpose_rows,(long long)bpose_dropped,(long long)bpose_failures,(unsigned long long)time);
    return n>0 && (size_t)n<cap?n:0;
}

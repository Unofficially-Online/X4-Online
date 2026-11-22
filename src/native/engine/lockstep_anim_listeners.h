/* An animation's listeners are served by the simulation only (native_gate_anim_listeners, a session default; Windows gate).
 *
 * Seen once in a full mission matrix (17 Nov, tick 14084, a loaded PC): 29 ticks after player 2's ship was put next to a
 * station, client 2 alone rebuilt that station's interior walk mesh six times in one tick; the mesh's event then made
 * other objects on that client only and the two clients parted for good. Seven later full runs did not show it.
 *
 * What X4 does [static, 9.00]. Every request for such a rebuild (0x75b440) comes from an animation state listener
 * (U::Animation::DockingBayFenceStateListener, CockpitStateListener, the dock area's): a listener waits on an animation
 * controller for a state; the controller's set-state 0x116a1f0 calls 0x1169a90 (site 0x116a2ce), which for every
 * listener waiting for the new state posts a U::AnimationTickedEvent to the owner at game time + the sequence's
 * remaining time and drops the listener from the waiting list; the event's delivery calls the listener, and that asks
 * for the rebuild.
 * Measured with breakpoints on two clients over whole matrix runs (18 Nov): the event is posted about 313 times a run,
 * the same on both clients and always inside UniverseFrameUpdate. The set-state itself runs about 167,000 times a run
 * and not equally: 37,000 to 41,000 of the calls come from the render pass (GameRender::Move on the stack; 37,163
 * against 40,736 in one run) and 1,031 from the UI: a client sets the animation states of what it draws. Such a call
 * serves the waiting listeners like any other, so a listener the simulation left waiting (a fence that changed state
 * while nobody had the model) is served on the client that starts to draw the station, at the moment its model is in.
 * That this is what happened in the run above is the reading that fits (one client, the one that looked, a second
 * after it arrived); the run was not captured, so it is not proven.
 *
 * The change: 0x1169a90 (its only caller is that site) is entered through animl_serve. A call the frame thread makes in one of the client's own
 * parts of the frame (the scopes lockstep_view_points.h knows: a presentation scope, a view call, the dispatch of a
 * presentation id, the render pass, the UI's Move, a camera event) while listeners wait does not serve them: the state
 * is set as before, the listeners stay in the waiting list, as they do on a client that draws nothing there, and the
 * simulation's next set-state of that state serves them on every client at the same tick. Every other call is X4's.
 * What it costs: a state change nobody watched is not followed by its listener (a walk mesh rebuild) at the moment a
 * player first sees the object.
 * Cache builds (21 Nov, rows below): U::Component::BuildCache 0x3d4a80 runs in the simulation frame on every client
 * (Galaxy::PopulateCache 0x9fe549, FlushOrphanCacheData 0x8e7de7) but publishes what each client's own cache
 * specifications ask for, and those follow its cameras. A publication initialises the object's animations (Positional
 * slots 622 -> 672 -> SlaveAnimatedValue::Init 0x1165f99 -> set-state), so the client that looks served waiting listeners
 * its peer did not: hunt rounds hB-2/3/4 and hC-2 (full matrix, 3 of 4): player 2's client one or two posts more during
 * player 2's station scan, CockpitStateListener (0x2c78290) of two ships, with scope NONE (the scopes above did not hold
 * them); the frames 116a2d3 <- 116a03b <- 1165f99 <- 74c827 <- 745cb3 <- 3d4a57 <- 3d4d0f (BuildCache). Every post with
 * listeners waiting whose call stack holds a BuildCache frame is held now as well (held's seventh count). The posts both
 * clients serve came from the event delivery (SequenceControlUnit 0x1169f16) and the docking bay update (0x1165481 <-
 * 0x74c37a <- 0x53f535); no shared post had a BuildCache frame. native_gate_anim_listeners = 'scopes' keeps the scopes only.
 * Fail closed: the function and its caller's chunk are pinned; the entry is hooked like the other entries on the slots'
 * near page (the first instruction on a trampoline), restored on stop. The call site itself cannot be re-pointed: its
 * five bytes lie across a 16-byte block, which the gate's atomic patch does not write (the first build refused to arm,
 * why=site). Status installed= why= calls= waiting= held=present/view/dispatch/render/ui/camera last=tick:scope:state
 * served=N digest=D: the posts this client served (X4's function with listeners waiting), N of them, D a running FNV-1a
 * of (tick, state, scope, number of waiting listeners, each listener's class as a vtable RVA), all portable. Two clients'
 * per-tick status rows part at the first one-sided post (a diagnostic; nothing is changed by it). Every served post is
 * also an ANIM_SERVED_ROW (gate.anim_listeners_row(i), ring of 4096): its tick, state, the owner component
 * ([[controller+8]+0x58] is its runtime ID, the handle 0xcd480 resolves) as a portable key, scope, the listeners' count
 * and first four classes, and the first eight X4 return addresses of the call (RtlCaptureStackBackTrace, as RVAs). */
#define ANIML_SITE 0x116a2ceu            /* set-state 0x116a1f0: call 0x1169a90 (controller, state), the only caller */
#define ANIML_TARGET 0x1169a90u
#define ANIML_TARGET_SIZE 0x223u
#define ANIML_TARGET_SHA "6ca3c51ed68726e7e9ba2a89fb8983c19751e7fe4a60e49530cdfc95f3d465a9"
#define ANIML_PIN 0x116a295u             /* the set-state's last chunk, the call inside */
#define ANIML_PIN_SIZE 0x50u
#define ANIML_PIN_SHA "77d814665f4d32f3e3ef9981aee8efa52c1cbfaeeb50aa2578ae3ed69a6b8ee3"
#define ANIML_STUB 0x520u                /* on the slots' near page, after the due lookup's (0x500) */
#define ANIML_TRAMPOLINE 0x540u          /* the function's first instruction, then on at +5 */
static const unsigned char animl_head[5]={0x48,0x89,0x5c,0x24,0x10};   /* mov [rsp+0x10],rbx */
#define ANIML_WAIT_BEGIN 0x40u           /* controller: the waiting listeners, begin and end */
#define ANIML_WAIT_END 0x48u
#define ANIML_CACHE_LO 0x3d4a80u           /* U::Component::BuildCache, its runtime function [0x3d4a80, 0x3d517f) */
#define ANIML_CACHE_HI 0x3d517fu
#define ANIML_CACHE_SIZE 0x6ffu            /* the join's pin of its start (lockstep_join.h) */
#define ANIML_CACHE_SHA "c8bd534299f639285476f5738e8674a4e14dec04d8004b792eadfa11d92ec91c"
#define ANIML_FRAMES 32u
typedef void (*animl_fn)(void *controller,void *state);
static unsigned char animl_patched[5];
static int animl_installed;
static const char *animl_why="-";
static volatile LONG64 animl_calls,animl_waiting,animl_held[VIEW_UI_SCOPES],animl_held_cache;
static int animl_cache_rule=1;           /* 1: posts under a cache build are held too; 0 ('scopes'): the scopes only */
static uint64_t animl_last_tick,animl_last_state;static unsigned animl_last_scope;
static volatile LONG64 animl_served;static uint64_t animl_served_digest=0xcbf29ce484222325ULL;
#define ANIML_RING 4096u
typedef struct {uint64_t tick,state,owner;uint32_t scope,count,classes[4],frames[8];} animl_row;
static animl_row animl_ring[ANIML_RING];
/* The owner component's runtime ID: [[controller+8]+0x58] (0 when unreadable). */
static uint64_t animl_owner(unsigned char *controller) {
    unsigned char *set=NULL;uint64_t id=0;
    if(controller && read_local(controller+8,&set,8) && set && read_local(set+0x58,&id,8)) return id;
    return 0;
}
static uint64_t animl_fnv(uint64_t h,const void *data,size_t n) {const unsigned char *p=data;while(n--) {h^=*p++;h*=0x100000001b3ULL;}return h;}
/* One served post into the digest: the listeners' classes in their waiting order (vtable RVAs; 0 for one outside the image). */
static void animl_note_served(unsigned char *controller,void **begin,void **end,void *state,unsigned scope,void *const *frames,USHORT got) {
    uint64_t tick=stats.issued,s=0,n=(uint64_t)(end-begin);uint64_t h=animl_served_digest;
    animl_row row;memset(&row,0,sizeof(row));
    if(state) read_local(state,&s,8);
    h=animl_fnv(h,&tick,8);h=animl_fnv(h,&s,8);h=animl_fnv(h,&scope,4);h=animl_fnv(h,&n,8);
    unsigned c=0;
    for(void **at=begin;at<end && at<begin+64;at++) {
        void *listener=NULL;uintptr_t vt=0,rva=0;
        if(read_local(at,&listener,8) && listener && read_local(listener,&vt,8) && pnat_base && vt>=(uintptr_t)pnat_base && vt<(uintptr_t)pnat_base+0x6fa9000u)
            rva=vt-(uintptr_t)pnat_base;
        h=animl_fnv(h,&rva,8);
        if(c<4) row.classes[c++]=(uint32_t)rva;
    }
    row.tick=tick;row.state=s;row.owner=animl_owner(controller);row.scope=scope;row.count=(uint32_t)n;
    {   /* The call's X4 return addresses, outermost last; the gate's own frames skipped. */
        unsigned k=0;
        for(USHORT i=0;frames && i<got && k<8;i++) {
            uintptr_t f=(uintptr_t)frames[i];
            if(pnat_base && f>=(uintptr_t)pnat_base && f<(uintptr_t)pnat_base+0x6fa9000u) row.frames[k++]=(uint32_t)(f-(uintptr_t)pnat_base);
        }
    }
    LONG64 at=InterlockedIncrement64(&animl_served)-1;
    animl_ring[(uint64_t)at%ANIML_RING]=row;
    animl_served_digest=h;
}
static int animl_row_text(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&animl_served,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)ANIML_RING && index<(uint64_t)(count-(LONG64)ANIML_RING)) return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    animl_row r=animl_ring[index%ANIML_RING];unsigned klass=WORLD_KEY_NONE;uint64_t key=r.owner?world_identity_key(r.owner,&klass):0;
    n=snprintf(out,cap,"index=%llu tick=%llu state=%016llx key=%016llx klass=%u scope=%u n=%u classes=%x,%x,%x,%x frames=%x,%x,%x,%x,%x,%x,%x,%x",
               (unsigned long long)index,(unsigned long long)r.tick,(unsigned long long)r.state,(unsigned long long)key,klass,r.scope,r.count,
               r.classes[0],r.classes[1],r.classes[2],r.classes[3],r.frames[0],r.frames[1],r.frames[2],r.frames[3],r.frames[4],r.frames[5],r.frames[6],r.frames[7]);
    return n>0 && (size_t)n<cap?n:0;
}
static animl_fn animl_original_test;     /* tests: X4's function */
static unsigned (*animl_scope_test)(void);
/* Whether a captured stack holds a frame of the cache build. */
static int animl_under_cache(void *const *frames,USHORT got) {
    for(USHORT i=0;i<got;i++) {
        uintptr_t f=(uintptr_t)frames[i];
        if(pnat_base && f>(uintptr_t)pnat_base+ANIML_CACHE_LO && f<=(uintptr_t)pnat_base+ANIML_CACHE_HI) return 1;
    }
    return 0;
}
static USHORT (*animl_capture_test)(void **frames,unsigned capacity);   /* tests: the stack */
/* 1: served by X4's function; 0: held (the listeners keep waiting). */
static int animl_serve_impl(animl_fn original,unsigned char *controller,void *state) {
    void *begin=NULL,*end=NULL;
    InterlockedIncrement64(&animl_calls);
    if(controller && read_local(controller+ANIML_WAIT_BEGIN,&begin,8) && read_local(controller+ANIML_WAIT_END,&end,8) && begin!=end) {
        InterlockedIncrement64(&animl_waiting);
        const unsigned scope=animl_scope_test?animl_scope_test():view_ui_scope();
        if(scope!=VIEW_UI_NONE && scope<VIEW_UI_SCOPES) {
            InterlockedIncrement64(&animl_held[scope]);
            animl_last_tick=stats.issued;animl_last_scope=scope;animl_last_state=0;
            if(state) read_local(state,&animl_last_state,8);
            return 0;
        }
        void *frames[ANIML_FRAMES];
        USHORT got=animl_capture_test?animl_capture_test(frames,ANIML_FRAMES):RtlCaptureStackBackTrace(0,ANIML_FRAMES,frames,NULL);
        if(animl_cache_rule && animl_under_cache(frames,got)) {
            InterlockedIncrement64(&animl_held_cache);
            animl_last_tick=stats.issued;animl_last_scope=VIEW_UI_SCOPES;animl_last_state=0;
            if(state) read_local(state,&animl_last_state,8);
            return 0;
        }
        animl_note_served(controller,(void**)begin,(void**)end,state,scope,frames,got);
    }
    original(controller,state);
    return 1;
}
static __attribute__((noinline)) void animl_serve(void *controller,void *state) {
    animl_serve_impl(animl_original_test?animl_original_test:(animl_fn)(void*)(pslot_page+ANIML_TRAMPOLINE),controller,state);
}
/* gate.anim_listeners(1) (scopes and cache builds) or (2) (scopes only), once the pilot modules know the game's image.
 * 1: the entry is hooked (or was, with the same rule). */
static int animl_arm(int cache_rule) {
    DWORD old,ignored;int32_t rel=0;
    if(animl_installed) return animl_cache_rule==cache_rule;
    if(!pnat_base) {animl_why="no_image";return 0;}
    if(cache_rule && !hash_ok(pnat_base+ANIML_CACHE_LO,ANIML_CACHE_SIZE,ANIML_CACHE_SHA)) {animl_why="cache_pin";return 0;}
    animl_cache_rule=cache_rule;
    if(!pslot_page && !(pslot_page=pslot_alloc_page())) {animl_why="page";return 0;}
    if(!hash_ok(pnat_base+ANIML_PIN,ANIML_PIN_SIZE,ANIML_PIN_SHA) || !hash_ok(pnat_base+ANIML_TARGET,ANIML_TARGET_SIZE,ANIML_TARGET_SHA) ||
       memcmp(pnat_base+ANIML_TARGET,animl_head,sizeof(animl_head))) {animl_why="pins";return 0;}
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) {animl_why="protect";return 0;}
    pview_abs_jump(pslot_page+ANIML_STUB,(void*)animl_serve);
    memcpy(pslot_page+ANIML_TRAMPOLINE,animl_head,sizeof(animl_head));
    pview_abs_jump(pslot_page+ANIML_TRAMPOLINE+sizeof(animl_head),pnat_base+ANIML_TARGET+sizeof(animl_head));
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) {animl_why="protect";return 0;}
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    rel=(int32_t)((intptr_t)(pslot_page+ANIML_STUB)-(intptr_t)(pnat_base+ANIML_TARGET+5));
    animl_patched[0]=0xe9;memcpy(animl_patched+1,&rel,4);
    if(!pview_cas(ANIML_TARGET,animl_head,animl_patched,5)) {animl_why="entry";return 0;}
    animl_installed=1;animl_why="-";return 1;
}
static void animl_restore(void) {
    if(animl_installed && pview_cas(ANIML_TARGET,animl_patched,animl_head,5)) animl_installed=0;
}
static int animl_status(char *out,size_t capacity) {
    return snprintf(out,capacity,"installed=%d why=%s calls=%lld waiting=%lld held=%lld/%lld/%lld/%lld/%lld/%lld/%lld last=%llu:%u:%016llx served=%lld digest=%016llx",
                    animl_installed,animl_why,(long long)animl_calls,(long long)animl_waiting,
                    (long long)animl_held[VIEW_UI_PRESENT],(long long)animl_held[VIEW_UI_VIEW],(long long)animl_held[VIEW_UI_DISPATCH],
                    (long long)animl_held[VIEW_UI_RENDER],(long long)animl_held[VIEW_UI_SLOT6],(long long)animl_held[VIEW_UI_CAMERA],(long long)animl_held_cache,
                    (unsigned long long)animl_last_tick,animl_last_scope,(unsigned long long)animl_last_state,
                    (long long)animl_served,(unsigned long long)animl_served_digest);
}

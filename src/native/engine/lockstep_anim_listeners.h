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
 * Fail closed: the function and its caller's chunk are pinned; the entry is hooked like the other entries on the slots'
 * near page (the first instruction on a trampoline), restored on stop. The call site itself cannot be re-pointed: its
 * five bytes lie across a 16-byte block, which the gate's atomic patch does not write (the first build refused to arm,
 * why=site). Status installed= why= calls= waiting= held=present/view/dispatch/render/ui/camera last=tick:scope:state. */
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
typedef void (*animl_fn)(void *controller,void *state);
static unsigned char animl_patched[5];
static int animl_installed;
static const char *animl_why="-";
static volatile LONG64 animl_calls,animl_waiting,animl_held[VIEW_UI_SCOPES];
static uint64_t animl_last_tick,animl_last_state;static unsigned animl_last_scope;
static animl_fn animl_original_test;     /* tests: X4's function */
static unsigned (*animl_scope_test)(void);
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
    }
    original(controller,state);
    return 1;
}
static __attribute__((noinline)) void animl_serve(void *controller,void *state) {
    animl_serve_impl(animl_original_test?animl_original_test:(animl_fn)(void*)(pslot_page+ANIML_TRAMPOLINE),controller,state);
}
/* gate.anim_listeners(1), once the pilot modules know the game's image. 1: the entry is hooked (or was). */
static int animl_arm(void) {
    DWORD old,ignored;int32_t rel=0;
    if(animl_installed) return 1;
    if(!pnat_base) {animl_why="no_image";return 0;}
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
    return snprintf(out,capacity,"installed=%d why=%s calls=%lld waiting=%lld held=%lld/%lld/%lld/%lld/%lld/%lld last=%llu:%u:%016llx",
                    animl_installed,animl_why,(long long)animl_calls,(long long)animl_waiting,
                    (long long)animl_held[VIEW_UI_PRESENT],(long long)animl_held[VIEW_UI_VIEW],(long long)animl_held[VIEW_UI_DISPATCH],
                    (long long)animl_held[VIEW_UI_RENDER],(long long)animl_held[VIEW_UI_SLOT6],(long long)animl_held[VIEW_UI_CAMERA],
                    (unsigned long long)animl_last_tick,animl_last_scope,(unsigned long long)animl_last_state);
}

/* Determinism fix, DEFAULT OFF: attention from a deterministic simulation
 * viewpoint set instead of each client's camera (native_gate_view_points).
 * Attention (+0x1ec, 1..11) gates physics bodies, per-frame Move work, bullet
 * tests, nav upkeep and the views of AI and MD. Its only computation is the
 * vtable slot 703 (+0x15f8) that U::Component::SetAttention 0x3d5660 calls at
 * 0x3d567d (the single call site of the slot). The viewer-dependent
 * implementations read the local player object g_Universe+0x230 and the camera
 * list g_Universe+0x2c0 (count +0x3c0, player camera +0x3d0):
 *   Positional 0x753350 (interior containing the player -> 10), Object
 *   0x6d20b0 (player or camera within the nearby radius -> 9), Ship 0x7d5110
 *   (ship carrying the player -> 9), Controllable 0x4640e0, Room 0x76e520,
 *   Zone 0x843b20 (player or camera zone -> 8, within size + 50 km -> 7, else
 *   6), Sector 0x78d6a0, Cluster 0x3bb7f0, WalkableModule 0x80e590,
 *   CutsceneAnchor 0x4b9cc0 and the camera-anchor ancestry test 0x3e50e0.
 * The cameras differ per client (in multiplayer each follows its own ship), so
 * native attention differs per client. This module:
 * - points the 20 g_Universe loads of those functions (mov r64,[rip+d32]) at a
 *   pointer slot on a private page near the image; outside an evaluation the
 *   slot holds the live g_Universe, during one it holds a shadow universe whose
 *   player object is the viewpoint and whose camera list is empty;
 * - rewrites the slot-703 call in SetAttention (ff 90 f8 15 00 00) into a call
 *   through the page (ff 15 d32) to view_attention, which evaluates the
 *   object's own slot-703 implementation once per viewpoint and returns the
 *   highest level (the native rules combine viewers by maximum).
 * Viewpoints, latched each admitted frame in slot 6 (gate.view_points_frame):
 * the resolved pilot slot ships in slot order (native_gate_pilot_slots), else
 * the local player object (identical on all clients of a same-save census;
 * per client in real multiplayer, so use slots there). Until the first latch
 * the live player object is used. A changed set or viewpoint zone recomputes
 * (SetAttention(x,1), only where +0xd1 marks own attention) every ancestor of
 * the old and new viewpoints, so zones not recomputed by Zone::Move (attention
 * <= 5) follow a viewpoint deterministically. Rendering keeps the camera.
 * 'players' applies this; 'observe' evaluates both and returns the native
 * (camera) result, counting where they differ. Pins: the 11 functions and
 * SetAttention's first chunk (SHA-256 with our bytes read as the originals),
 * every site's opcode and original target. Patches are CAS of the aligned 16
 * bytes holding them, at arm before load; every gate tick re-checks them and
 * the page (stop reason 97); stop restores by CAS and leaves a foreign value
 * marked. One recursive lock serializes evaluations and the latch (the page is
 * shared); evaluations off the MoveWorker are counted (foreign=). No thunk. */
#define VIEW_UNIVERSE 0x3d198f8u
#define VIEW_SET_ATTENTION 0x3d5660u
#define VIEW_CALL 0x3d567du
#define VIEW_SITES 20u
#define VIEW_FUNCS 12u
#define VIEW_MAX 8u   /* one view point per pilot slot (PILOT_SLOTS) */
#define VIEW_PAGE_SIZE 0x10000u
#define VIEW_PAGE_CALL 0x0u     /* view_attention pointer read by the rewritten call */
#define VIEW_PAGE_UNIVERSE 0x8u /* universe pointer read by the rewritten loads */
#define VIEW_PAGE_SHADOW 0x1000u
#define VIEW_SHADOW_SIZE 0x400u
#define VIEW_LEVELS 12u
enum {VIEW_OFF,VIEW_PLAYERS,VIEW_OBSERVE};
static const struct {unsigned rva;unsigned char reg;} view_sites[VIEW_SITES]={
    {0x753387u,0x05},{0x6d2194u,0x05},{0x6d230cu,0x3d},{0x6d2361u,0x05},{0x7d51fdu,0x05},{0x4640f6u,0x05},{0x76e572u,0x05},
    {0x843b83u,0x05},{0x843c70u,0x05},{0x843d86u,0x3d},{0x843db0u,0x05},{0x78d6afu,0x05},{0x78d71cu,0x05},{0x3bb7fau,0x05},
    {0x3e50fau,0x1d},{0x3e5137u,0x05},{0x80e5d9u,0x1d},{0x80e67fu,0x3d},{0x80e709u,0x05},{0x4b9ccau,0x05}};
static const struct {unsigned rva,size;const char *sha;} view_funcs[VIEW_FUNCS]={
    {0x753350u,0x127u,"a95dac8c3bf705530eb7892fac20ee67ed612d1d5e0fc5bb60f9eb84864a82d5"},
    {0x6d20b0u,0x46eu,"9ab119383352cb4b29bc05993a4663eda0f6511aa5b9edaa8a8b950a957b8966"},
    {0x7d5110u,0x15eu,"f77821b926165edbf624dd73550b7b5aad739efda149f619fc75b9e19ea852be"},
    {0x4640e0u,0x76u,"09455658525a263330642a0bbe5ae9688f31548f3e1d6b9cdef07453d0f3588c"},
    {0x76e520u,0x180u,"61ccd72154b8d25d246bb3832ae056b500c6c40ed942ed9e44db05b8841df146"},
    {0x843b20u,0x4a8u,"b92e75128f6a0849de28ef5c913cd2e3c4d724c37edd30c87e25fd9afe099516"},
    {0x78d6a0u,0x92u,"9819542918daad8a1c37ac3a4ce2c817c3ee7d7c6db7ea4e131cbc6c240ab3de"},
    {0x3bb7f0u,0x95u,"0ea6b0eeacb40fec7cd0e8ae0afd7fd480c399a63e95ed8bc03b7ccbadce9324"},
    {0x3e50e0u,0xceu,"c47f943736e0c6a8c1e188bee513ee2757fa2faefed1a6d6f0da199a2a6e4e45"},
    {0x80e590u,0x209u,"4c05a45b34784429976b691519fca6929d80a458de0955c54a3a89c878fb2893"},
    {0x4b9cc0u,0x83u,"2a311784726ca10a6ec90e989db2f0cf3b1ebc874849b833b92451f1585974b0"},
    {VIEW_SET_ATTENTION,0x64u,"8e948a0788bbacca91415cb51e228e73bc416c608d2398b07f05312fb19b56eb"}};
static const unsigned char view_call_original[6]={0xff,0x90,0xf8,0x15,0x00,0x00};
typedef int (*view_attention_fn)(void *);
typedef void (*view_set_attention_fn)(void *,int);
static int view_mode,view_installed,view_restored,view_patched;
static volatile LONG view_active;
static unsigned char *view_page;
static unsigned char view_sites_done[VIEW_SITES],view_call_done;
static void *view_points[VIEW_MAX],*view_previous[VIEW_MAX],*view_previous_zone[VIEW_MAX];
static unsigned view_count,view_previous_count,view_latched,view_source; /* source: 0 none, 1 slots, 2 player object, 3 occupied ship */
static uint64_t view_ids[VIEW_MAX],view_previous_ids[VIEW_MAX];
static uint64_t view_tick=UINT64_MAX,view_frame_calls,view_frame_evaluations,view_frame_differ,view_frame_levels[VIEW_LEVELS];
static uint64_t view_calls,view_evaluations,view_differ,view_first_differ,view_refreshes,view_latches,view_unreadable;
static unsigned view_depth;
static volatile LONG64 view_foreign,view_failures;
static view_set_attention_fn view_set_attention_override; /* tests only; NULL calls the native SetAttention */
static const char *view_stage="idle";
static int32_t view_disp_to(unsigned rva,unsigned length,const unsigned char *target) {
    return (int32_t)((intptr_t)target-(intptr_t)(image+rva+length));
}
static int view_disp_fits(unsigned rva,unsigned length,const unsigned char *target) {
    intptr_t d=(intptr_t)target-(intptr_t)(image+rva+length);return d>=INT32_MIN && d<=INT32_MAX;
}
/* Write n bytes at rva through CAS of each aligned 16-byte block they touch;
 * every touched byte must hold 'expected' first. Blocks already rewritten are
 * put back when a later block fails. */
static int view_cas_bytes(unsigned rva,const unsigned char *expected,const unsigned char *value,unsigned n) {
    unsigned first=rva&~15u,last=(rva+n-1)&~15u,done=0;
    for(unsigned block=first;block<=last;block+=16) {
        unsigned char *p=image+block;DWORD old,ignored;
        int ok=VirtualProtect(p,16,PAGE_EXECUTE_READWRITE,&old)!=0;
        if(ok) {
            unsigned __int128 current,desired;memcpy(&current,(const void*)p,16);desired=current;
            for(unsigned i=0;i<16;i++) {
                unsigned at=block+i;if(at<rva || at>=rva+n) continue;
                if(((unsigned char*)&current)[i]!=expected[at-rva]) ok=0;
                ((unsigned char*)&desired)[i]=value[at-rva];
            }
            if(ok) ok=__sync_val_compare_and_swap((unsigned __int128*)p,current,desired)==current;
            VirtualProtect(p,16,old,&ignored);FlushInstructionCache(GetCurrentProcess(),p,16);
        }
        if(!ok) {
            for(unsigned back=first;back<block;back+=16) {
                unsigned char *q=image+back;DWORD o2,i2;if(!VirtualProtect(q,16,PAGE_EXECUTE_READWRITE,&o2)) continue;
                unsigned __int128 c2,d2;memcpy(&c2,(const void*)q,16);d2=c2;
                for(unsigned i=0;i<16;i++) {unsigned at=back+i;if(at>=rva && at<rva+n) ((unsigned char*)&d2)[i]=expected[at-rva];}
                __sync_val_compare_and_swap((unsigned __int128*)q,c2,d2);
                VirtualProtect(q,16,o2,&i2);FlushInstructionCache(GetCurrentProcess(),q,16);
            }
            return 0;
        }
        ++done;
    }
    return done==((last-first)/16+1);
}
static void view_site_bytes(unsigned i,int patched,unsigned char out[4]) {
    int32_t d=patched?view_disp_to(view_sites[i].rva,7,view_page+VIEW_PAGE_UNIVERSE):view_disp_to(view_sites[i].rva,7,image+VIEW_UNIVERSE);
    memcpy(out,&d,4);
}
static void view_call_bytes(int patched,unsigned char out[6]) {
    if(!patched) {memcpy(out,view_call_original,6);return;}
    int32_t d=view_disp_to(VIEW_CALL,6,view_page+VIEW_PAGE_CALL);out[0]=0xff;out[1]=0x15;memcpy(out+2,&d,4);
}
/* Each site's opcode and its original target or our page; returns 0 original, 1 ours, -1 other. */
static int view_site_state(unsigned i) {
    unsigned char head[3],disp[4],mine[4],orig[4];
    if(!read_local(image+view_sites[i].rva,head,3) || !read_local(image+view_sites[i].rva+3,disp,4)) return -1;
    if(head[0]!=0x48 || head[1]!=0x8b || head[2]!=view_sites[i].reg) return -1;
    view_site_bytes(i,0,orig);if(!memcmp(disp,orig,4)) return 0;
    if(view_page) {view_site_bytes(i,1,mine);if(!memcmp(disp,mine,4)) return 1;}
    return -1;
}
static int view_call_state(void) {
    unsigned char now[6],mine[6];if(!read_local(image+VIEW_CALL,now,6)) return -1;
    if(!memcmp(now,view_call_original,6)) return 0;
    if(view_page) {view_call_bytes(1,mine);if(!memcmp(now,mine,6)) return 1;}
    return -1;
}
/* Pins with every site and the call read as the originals. */
static int view_pins(void) {
    static unsigned char copy[0x4b0];
    for(unsigned f=0;f<VIEW_FUNCS;f++) {
        unsigned rva=view_funcs[f].rva,size=view_funcs[f].size;
        if(size>sizeof(copy) || !read_local(image+rva,copy,size)) return 0;
        for(unsigned i=0;i<VIEW_SITES;i++) {
            unsigned at=view_sites[i].rva;if(at<rva || at+7>rva+size) continue;
            if(view_site_state(i)<0) return 0;
            unsigned char orig[4];view_site_bytes(i,0,orig);memcpy(copy+(at-rva)+3,orig,4);
        }
        if(VIEW_CALL>=rva && VIEW_CALL+6<=rva+size) {
            if(view_call_state()<0) return 0;
            memcpy(copy+(VIEW_CALL-rva),view_call_original,6);
        }
        if(!hash_ok(copy,size,view_funcs[f].sha)) return 0;
    }
    return 1;
}
static unsigned char *view_page_near(void) {
    uintptr_t at=(uintptr_t)(image+VIEW_SET_ATTENTION)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,VIEW_PAGE_SIZE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int fits=view_disp_fits(VIEW_CALL,6,p+VIEW_PAGE_CALL);
            for(unsigned i=0;fits && i<VIEW_SITES;i++) fits=view_disp_fits(view_sites[i].rva,7,p+VIEW_PAGE_UNIVERSE);
            if(fits) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void view_roll(void) {
    if(view_tick==stats.issued) return;
    view_tick=stats.issued;view_frame_calls=view_frame_evaluations=view_frame_differ=0;
    memset(view_frame_levels,0,sizeof(view_frame_levels));
}
static CRITICAL_SECTION view_lock;
static int view_lock_ready;
#include "lockstep_view_cameras.h"   /* the simulation's own cutscene cameras as viewers */
/* Hybrid sectors (lockstep_hybrid.h, native_gate_hybrid, default off): which latched points count for an
 * object, which slots this client leaves out of the latch, where its own owned point is. */
static int hyb_point_counts(unsigned i,const void *object);
static int hyb_latch_skip(unsigned slot_index);
static int hyb_latch_own(unsigned slot_index);
static int hyb_latch_begin(uint64_t tick);
static void hyb_latch_check(void);
static void hyb_view_index_set(int index);
/* One object's attention from the viewpoint set; the native implementation runs
 * with the shadow universe, once per viewpoint (none: once without a viewer).
 * With hybrid sectors a client's own owned point counts only inside its sector;
 * no counting point at all is the evaluation without a viewer. */
static int view_evaluate(view_attention_fn native,void *object) {
    volatile uint64_t *slot=(volatile uint64_t *)(view_page+VIEW_PAGE_UNIVERSE);
    void **player=(void**)(view_page+VIEW_PAGE_SHADOW+0x230);
    uint64_t saved_slot=*slot;void *saved_player=*player;
    void *points[VIEW_MAX];unsigned n=0;
    if(view_latched) {n=view_count;memcpy(points,view_points,sizeof(points));}
    else {
        unsigned char *universe=*(unsigned char*volatile*)(image+VIEW_UNIVERSE);void *p=NULL;
        if(universe && read_local(universe+0x230,&p,8) && p) {points[0]=p;n=1;}
    }
    int best=0;unsigned counted=0;++view_depth;
    if(vcam_shown) vcam_anchors();   /* the stand-in cameras' anchors, found again by their IDs */
    *slot=(uint64_t)(uintptr_t)(view_page+VIEW_PAGE_SHADOW);
    for(unsigned i=0;i<n;i++) {
        if(!hyb_point_counts(i,object)) continue;
        *player=points[i];*slot=(uint64_t)(uintptr_t)(view_page+VIEW_PAGE_SHADOW);
        int r=native(object);++view_evaluations;++view_frame_evaluations;++counted;
        if(r>best) best=r;
    }
    if(!counted) {*player=NULL;*slot=(uint64_t)(uintptr_t)(view_page+VIEW_PAGE_SHADOW);best=native(object);++view_evaluations;++view_frame_evaluations;}
    --view_depth;*player=saved_player;*slot=saved_slot;
    return best;
}
/* Diagnostic rows, DEFAULT OFF (native_gate_view_rows): one row whenever an
 * object's evaluated level (and in 'observe' the camera level) differs from its
 * previous evaluation: tick, portable key (saved or created ID, 0 unmapped),
 * class, the vtable RVA (which slot-703 implementation ran) and the levels.
 * Comparing two clients' rows names the objects whose attention still depends
 * on something per-client, and the implementation that computed it. Rows start
 * with the first admitted frame (loading evaluations are not recorded) and name
 * the trigger: the first image return address above SetAttention 0x3d5660 on
 * the stack (which caller asked for the evaluation: a camera enter/leave
 * handler, a container's Move, the parent propagation 0x3d5880, ...).
 * cam-B4: client 2's fixtures drop from 9 to 8 at ticks 47..63 while client
 * 1's stay at 9, so evaluations or their triggers still differ per client. */
#define VIEW_ROW_RING 8192u
#define VIEW_ROW_SEEN 262144u
typedef struct {uint64_t tick,key;uint32_t vtable,trigger,chain[4];int level,camera;unsigned char klass;} view_row_t;
static view_row_t view_row_ring[VIEW_ROW_RING];
static struct {const void *object;int level,camera;} view_row_seen[VIEW_ROW_SEEN];
static volatile LONG64 view_rows;
static int view_rows_armed,view_rows_started;
static uint64_t view_row_full;
/* The caller of SetAttention: the first image frame outside 0x3d5660..0x3d5880. */
/* The first image return address above SetAttention and, when chain is given,
 * the next four image return addresses above it (who asked the caller: e.g. the
 * creator of a component whose ConnectTo evaluated it); 0xffffffff when absent. */
static uint32_t view_row_trigger_chain(uint32_t *chain) {
    void *frames[24];USHORT n=RtlCaptureStackBackTrace(1,24,frames,NULL);uint32_t trigger=0xffffffffu;unsigned k=0;
    if(chain) for(unsigned i=0;i<4;i++) chain[i]=0xffffffffu;
    for(USHORT i=0;i<n;i++) {
        unsigned char *p=frames[i];if(p<image || p>=image+0x6fa9000) continue;
        uint32_t rva=(uint32_t)(p-image);if(rva>=0x3d5660u && rva<0x3d5880u) continue;
        if(trigger==0xffffffffu) {trigger=rva;if(!chain) break;continue;}
        if(k<4) chain[k++]=rva;else break;
    }
    return trigger;
}
static uint32_t view_row_trigger(void) {return view_row_trigger_chain(NULL);}
/* Camera-triggered evaluations, DEFAULT OFF (native_gate_view_triggers =
 * 'block' | 'observe'). Camera::0x97dff0 posts CameraEnteredEvent/CameraLeftEvent
 * to the components of the camera's old and new context whenever the camera's
 * context changes, so per client. Their handlers re-evaluate attention:
 * Ship OnCameraEntered/Left (slots 366/367, 0x7cddc0/0x7cde70: itself), the Zone
 * and Highway handler 0x83a3d0 (its class-6 children, then itself) and Cluster
 * OnCameraLeft 0x3ba8b0 (itself). With view points the result no longer depends
 * on the camera, but when it is computed still does: a value refreshed on one
 * client stays stale on the other (cam-B4: client 2's fixtures 9 -> 8 at 47..63,
 * client 1's stay 9). Cutscene shots (the cockpit's target and comm monitors,
 * conversations) are per-client presentation too and re-evaluate directly: the
 * shot end 0x10d5f00 (SetAttention(shot target, 1), from Cutscenes::Shot
 * slot 2 and 0x10d5e50), the shot camera update 0x10d5f90 (re-anchors a
 * cutscene camera, posts 0x97dff0's camera events for it and re-evaluates the
 * new anchor; from ShotCamera::vfunc2 0x10d7f60 and CameraAnimated::vfunc2
 * 0x10e22f0) and those two slot-2 functions themselves. st3-P15L (3 clients,
 * clients 2 and 3 anchored in other sectors): client 1, in the cockpit, ran one
 * evaluation more than clients 2 and 3 (whose counts stayed equal) at ticks 35,
 * 35, 36, 156, ..., 691, and fighters near its camera took collision damage
 * there alone by the 900 capture. 'block' returns the object's current attention
 * for an evaluation whose SetAttention caller is one of these nine call sites
 * (no change, no propagation); 'observe' only counts them (status: triggers=
 * the five camera handlers, cutscene= the four shot sites). Other triggers
 * (viewpoint changes, container and zone Moves, the parent propagation,
 * ConnectTo) are untouched. */
#define VIEW_TRIGGERS 9u
#define VIEW_TRIGGER_CAMERA 5u /* the first five sites: camera enter/leave handlers; then cutscene shots */
enum {VIEW_TRIGGER_OFF,VIEW_TRIGGER_BLOCK,VIEW_TRIGGER_OBSERVE};
static const uint32_t view_trigger_sites[VIEW_TRIGGERS]={0x7cdde0u,0x7cde90u,0x83a47bu,0x83a49cu,0x3baabeu,
    0x10d5f5bu,0x10d686bu,0x10d831au,0x10e2b00u};
static int view_trigger_mode;
static uint64_t view_trigger_counts[VIEW_TRIGGERS],view_trigger_blocked;
/* Each site is a 5-byte call to SetAttention ending at the listed return address. */
static int view_trigger_sites_ok(void) {
    for(unsigned i=0;i<VIEW_TRIGGERS;i++) {
        unsigned char call[5];int32_t rel;
        if(!read_local(image+view_trigger_sites[i]-5,call,5) || call[0]!=0xe8) return 0;
        memcpy(&rel,call+1,4);if(view_trigger_sites[i]+(uint32_t)rel!=VIEW_SET_ATTENTION) return 0;
    }
    return 1;
}
/* Index of a camera-handler call site, or -1. */
static int view_trigger_match(uint32_t trigger) {
    for(unsigned i=0;i<VIEW_TRIGGERS;i++) if(trigger==view_trigger_sites[i]) return (int)i;
    return -1;
}
/* 1 when the evaluation is replaced by the object's current level (*result). */
static int view_trigger_apply(void *object,int index,int *result) {
    if(index<0) return 0;
    ++view_trigger_counts[index];
    if(view_trigger_mode!=VIEW_TRIGGER_BLOCK) return 0;
    vcam_note(view_trigger_sites[index]);   /* a simulation cutscene's camera shot: a viewer from the next latch on */
    int current=0;if(!read_local((unsigned char*)object+0x1ec,&current,4)) {++view_unreadable;return 0;}
    ++view_trigger_blocked;*result=current;return 1;
}
static int view_triggers_arm(int mode) {
    /* An identical re-arm (the UI reloads after the snapshot load) is accepted even after the first tick. */
    if(view_trigger_mode) return view_trigger_mode==mode;
    if((mode!=VIEW_TRIGGER_BLOCK && mode!=VIEW_TRIGGER_OBSERVE) || !view_installed || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!view_trigger_sites_ok()) {view_stage="trigger_sites";return 0;}
    memset(view_trigger_counts,0,sizeof(view_trigger_counts));view_trigger_blocked=0;view_trigger_mode=mode;return 1;
}
/* Evaluations a client's own UI asks for, DEFAULT OFF (native_gate_view_ui =
 * 'block' | 'observe'). p4-long2 (4 own cockpits, build133): one input action on
 * client 4 alone (PILOT_INPUTS dispatch 0/0/2 -> 1/0/12, a SPACE edge, uihold
 * from 14731 to the end) opened the map: AddHoloMap 0x22c270 builds U::HoloMap
 * in "the player's room" (in client 4's presentation scope that is slot 4's
 * cockpit room) from UI::CUIController::Move. In that UI frame client 4 ran
 * about 185 evaluations more than the others, two stations of sector 729
 * changed level (MD ObjectChangedAttention, signal_leaks.xml, its cue instances
 * created inside the scope: route=ui), and by the 15000 capture 614 objects of
 * tempzone 2e2ce, the canonical player 11e12 among them, held other levels.
 * None of the nine trigger sites above is on that path. With view points the
 * level an evaluation computes no longer depends on the camera, but whether it
 * runs does, and inside a presentation scope the canonical player wears slot
 * k's identity (its parent is slot k's room), so what it computes for the player
 * and its ancestors is per client too. So an evaluation on the frame thread
 * inside a client-local scope is presentation, not simulation:
 *   present  a presentation scope is open (lockstep_pilot_present.h: slot 6,
 *            slot 10, the slot-4 dispatch of presentation ids, refill, pick);
 *   view     a call this client makes alone for its own view (rid_scope);
 *   dispatch the stock slot-4 dispatch of a presentation id (any client);
 *   render   VBI slot 10, GameRender::Move (render_rng_in_slot; else its body
 *            on the stack);
 *   ui       VBI slot 6, UI::CUIController::Move (the UI replay's depth, else
 *            its body on the stack), except the probe's own frame
 *            (gate.runtime_ids_probe) and the gate's own viewpoint refresh.
 * 'block' returns such an evaluation's current level (no change, no
 * propagation, no event) unless the object is the client's own: its runtime ID
 * comes from a local list (native_gate_runtime_ids_local, blocks 30/31: the
 * HoloMap, UI elements, effects), which the UI needs evaluated as stock does
 * (ui_local=). Without the local lists every object counts as shared, so the
 * probe asks for them with 'block'. 'observe' only counts.
 *
 * camera: p4-long4 (presses: OPEN_MAP on client 4 at 3000, on client 1, which
 * presents nothing, at 9000) parted both clients although the five camera
 * handlers and all of the above were blocked. Two ticks after AddHoloMap (3001,
 * 9001) a U::Camera died on that client: the deleting destructor 0x978fd0 ->
 * ~Camera 0x9794a0 -> 0x97dff0(camera, its anchor, NULL), which posts
 * CameraLeftEvent at once (PostEvent 0x99a3f0, dispatch 0x99c5b0) to the
 * anchor's ancestors. Two of their handlers are none of the nine trigger
 * sites: 0x6b8540 (if the target is the listener: SetAttention(it, 1) by
 * tail call, so the evaluation's trigger is the dispatch 0x99c5b0 that every
 * event shares) re-evaluated the canonical player's cockpit ae79f to 9 (it
 * still held its loaded level on the other clients), and Room::vfunc701
 * 0x76e6a0 re-evaluated its children through 0x3d597a. The player 11e12, its
 * computer 11e13, its camera anchor scene ae7b8 and that scene's ui positions
 * 11e14 followed by propagation: the census differs in exactly these five
 * objects (attention only) at 3600 on client 4 and from 9600 on client 1. The
 * frame thread was in no scope above (no flag; no UI::CUIController::Move or
 * GameRender::Move frame on the captured stack), so view_ui let them through.
 * 0x97dff0 is the only function that builds CameraLeftEvent/CameraEnteredEvent
 * (all ten references to their vtables are in its body); it runs when a camera
 * is anchored, re-anchored, re-parented with its anchor or destroyed, so on the
 * clients whose camera did that. So the sixth scope:
 *   camera   a return address in 0x97dff0's body (0x97dff0..0x97ee40) on the
 *            stack: any evaluation a camera event's handler asks for, whatever
 *            the handler and however it reaches SetAttention, and whatever
 *            called the camera (UI, render, the probe's frame, the simulation;
 *            region_camera's context stub, which posts the same events on every
 *            client, then keeps them out on every client alike).
 * It is tested before the probe's-frame exemption and the slot-6/slot-10 stack
 * search. Arming checks 0x97dff0's entry and the destructor's call into it.
 * Status: ui=mode:present/view/dispatch/render/ui/camera ui_blocked= ui_local=. */
enum {VIEW_UI_OFF,VIEW_UI_BLOCK,VIEW_UI_OBSERVE};
enum {VIEW_UI_NONE,VIEW_UI_PRESENT,VIEW_UI_VIEW,VIEW_UI_DISPATCH,VIEW_UI_RENDER,VIEW_UI_SLOT6,VIEW_UI_CAMERA,VIEW_UI_SCOPES};
#define VIEW_UI_MOVE_LO 0xb3bad0u   /* UI::CUIController::Move */
#define VIEW_UI_MOVE_HI 0xb3bb9eu
#define VIEW_RENDER_LO 0x121c1f0u   /* GameRender::Move */
#define VIEW_RENDER_HI 0x121c25au
#define VIEW_CAMERA_LO 0x97dff0u    /* Camera 0x97dff0: posts CameraLeftEvent / CameraEnteredEvent (.pdata 0x97dff0..0x97ee40) */
#define VIEW_CAMERA_HI 0x97ee40u
#define VIEW_CAMERA_DTOR_CALL 0x9794d2u /* ~Camera 0x9794a0: call 0x97dff0 (camera, anchor +0x770, NULL) */
#define VIEW_UI_STACK 62u
static int view_ui_mode;
static unsigned view_refreshing; /* the gate's own SetAttention calls (view_refresh_chain) */
static uint64_t view_ui_counts[VIEW_UI_SCOPES],view_ui_blocked,view_ui_local;
/* Tests only: the image return addresses of the evaluation's stack, innermost first. */
static unsigned (*view_ui_stack_override)(uint32_t *frames,unsigned capacity);
static unsigned view_ui_stack(uint32_t *frames,unsigned capacity) {
    if(view_ui_stack_override) return view_ui_stack_override(frames,capacity);
    void *raw[VIEW_UI_STACK];unsigned n=0;
    USHORT got=RtlCaptureStackBackTrace(1,capacity<VIEW_UI_STACK?capacity:VIEW_UI_STACK,raw,NULL);
    for(USHORT i=0;i<got;i++) {
        unsigned char *p=raw[i];if(p<image || p>=image+0x6fa9000) continue;
        frames[n++]=(uint32_t)(p-image);
    }
    return n;
}
/* Which client-local scope the frame thread is in (VIEW_UI_NONE: the simulation's own). */
static unsigned view_ui_scope(void) {
    if(GetCurrentThreadId()!=ui_thread || view_refreshing) return VIEW_UI_NONE;
    if(InterlockedCompareExchange(&pres_open,0,0)) return VIEW_UI_PRESENT;
    if(rid_scope_thread && rid_scope_thread==GetCurrentThreadId()) return VIEW_UI_VIEW;
    if(InterlockedCompareExchange(&uirep_dispatch_depth,0,0)>0) return VIEW_UI_DISPATCH;
    if(InterlockedCompareExchange(&render_rng_in_slot,0,0)) return VIEW_UI_RENDER;
    uint32_t frames[VIEW_UI_STACK];const unsigned n=view_ui_stack(frames,VIEW_UI_STACK);
    for(unsigned i=0;i<n;i++) if(frames[i]>=VIEW_CAMERA_LO && frames[i]<VIEW_CAMERA_HI) return VIEW_UI_CAMERA; /* a camera event */
    if(InterlockedCompareExchange(&rid_probe_frame,0,0)) return VIEW_UI_NONE; /* the probe's frame: canonical gate work */
    if(InterlockedCompareExchange(&uirep_ui_depth,0,0)>0) return VIEW_UI_SLOT6;
    for(unsigned i=0;i<n;i++) {
        if(frames[i]>=VIEW_UI_MOVE_LO && frames[i]<VIEW_UI_MOVE_HI) return VIEW_UI_SLOT6;
        if(frames[i]>=VIEW_RENDER_LO && frames[i]<VIEW_RENDER_HI) return VIEW_UI_RENDER;
    }
    return VIEW_UI_NONE;
}
/* 1 when the evaluation is replaced by the object's current level (*result). */
static int view_ui_apply(void *object,int *result) {
    const unsigned scope=view_ui_scope();
    if(scope==VIEW_UI_NONE) return 0;
    ++view_ui_counts[scope];
    uint64_t runtime=0;
    if(read_local((unsigned char*)object+8,&runtime,8) && rid_local_of((uint32_t)runtime&0x1ffffffu)) {++view_ui_local;return 0;}
    if(view_ui_mode!=VIEW_UI_BLOCK) return 0;
    int current=0;if(!read_local((unsigned char*)object+0x1ec,&current,4)) {++view_unreadable;return 0;}
    ++view_ui_blocked;*result=current;return 1;
}
/* The camera scope's range: 0x97dff0's entry (it spills rdx and rcx), the ret that ends its body at
 * 0x97ee3f, and ~Camera's call into it. */
static int view_ui_camera_ok(void) {
    static const unsigned char entry[10]={0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08};
    unsigned char head[10],last=0,call[5];int32_t rel;
    if(!read_local(image+VIEW_CAMERA_LO,head,10) || memcmp(head,entry,10) || !read_local(image+VIEW_CAMERA_HI-1,&last,1) || last!=0xc3) return 0;
    if(!read_local(image+VIEW_CAMERA_DTOR_CALL,call,5) || call[0]!=0xe8) return 0;
    memcpy(&rel,call+1,4);return VIEW_CAMERA_DTOR_CALL+5+(uint32_t)rel==VIEW_CAMERA_LO;
}
/* Before the first admitted frame, on the frame thread, with view points installed. */
static int view_ui_arm(int mode) {
    if(view_ui_mode) return view_ui_mode==mode; /* identical re-arm after a UI reload */
    if((mode!=VIEW_UI_BLOCK && mode!=VIEW_UI_OBSERVE) || !view_installed || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!view_ui_camera_ok()) {view_stage="ui_camera";return 0;}
    memset(view_ui_counts,0,sizeof(view_ui_counts));view_ui_blocked=view_ui_local=0;view_ui_mode=mode;return 1;
}
static void view_row_note(void *object,int level,int camera) {
    if(boot_mode && boot_phase!=3) return; /* loading evaluations are not compared */
    if(!view_rows_started) {memset(view_row_seen,0,sizeof(view_row_seen));view_rows_started=1;}
    unsigned h=(unsigned)(((uintptr_t)object*0x9e3779b97f4a7c15ULL)>>46)&(VIEW_ROW_SEEN-1),n=0;
    for(;n<VIEW_ROW_SEEN;n++,h=(h+1)&(VIEW_ROW_SEEN-1)) if(!view_row_seen[h].object || view_row_seen[h].object==object) break;
    if(n==VIEW_ROW_SEEN) {++view_row_full;return;}
    if(view_row_seen[h].object==object && view_row_seen[h].level==level && view_row_seen[h].camera==camera) return;
    view_row_seen[h].object=object;view_row_seen[h].level=level;view_row_seen[h].camera=camera;
    view_row_t r;memset(&r,0,sizeof(r));r.tick=stats.issued;r.level=level;r.camera=camera;r.vtable=0xffffffffu;r.trigger=view_row_trigger_chain(r.chain);
    void *vt=NULL;uint64_t runtime=0;unsigned klass=WORLD_KEY_NONE;
    if(read_local(object,&vt,8) && (unsigned char*)vt>=image && (unsigned char*)vt<image+0x6fa9000) r.vtable=(uint32_t)((unsigned char*)vt-image);
    if(read_local((unsigned char*)object+8,&runtime,8) && runtime) r.key=world_identity_key(runtime,&klass);
    r.klass=(unsigned char)klass;
    LONG64 index=InterlockedIncrement64(&view_rows)-1;view_row_ring[(uint64_t)index%VIEW_ROW_RING]=r;
}
static int view_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&view_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)VIEW_ROW_RING && index<(uint64_t)(count-(LONG64)VIEW_ROW_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const view_row_t *r=&view_row_ring[index%VIEW_ROW_RING];
    n=snprintf(out,cap,"index=%llu tick=%llu key=%016llx klass=%u vtable=%08x level=%d camera=%d trigger=%08x chain=%08x,%08x,%08x,%08x",
        (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->key,(unsigned)r->klass,r->vtable,r->level,r->camera,r->trigger,
        r->chain[0],r->chain[1],r->chain[2],r->chain[3]);
    return n>0 && (size_t)n<cap?n:0;
}
/* Evaluation callers (diagnostic, with native_gate_view_rows): every admitted
 * evaluation counted by its trigger (the first image return address above
 * SetAttention) and the trigger's own caller (chain[0]), so two clients' tables
 * name the source of an evaluation one of them ran alone even when it changed
 * no level (st3-P15L: total_calls parted by one on client 1 at irregular ticks
 * while the levels stayed equal). Status: callers= sorted trigger/caller:count,
 * at most VIEW_CALLERS pairs; later new pairs are counted as callers_other=. */
#define VIEW_CALLERS 24u
static struct {uint32_t trigger,caller;uint64_t count;} view_callers[VIEW_CALLERS];
static unsigned view_caller_count;
static uint64_t view_caller_other;
static void view_caller_add(uint32_t trigger,uint32_t caller) {
    unsigned i=0;
    while(i<view_caller_count && (view_callers[i].trigger<trigger || (view_callers[i].trigger==trigger && view_callers[i].caller<caller))) ++i;
    if(i<view_caller_count && view_callers[i].trigger==trigger && view_callers[i].caller==caller) {++view_callers[i].count;return;}
    if(view_caller_count==VIEW_CALLERS) {++view_caller_other;return;}
    memmove(view_callers+i+1,view_callers+i,(view_caller_count-i)*sizeof(*view_callers));
    view_callers[i].trigger=trigger;view_callers[i].caller=caller;view_callers[i].count=1;++view_caller_count;
}
static void view_caller_note(void) {
    if(boot_mode && boot_phase!=3) return; /* loading evaluations are not compared */
    uint32_t chain[4];const uint32_t trigger=view_row_trigger_chain(chain);view_caller_add(trigger,chain[0]);
}
/* The callers table as text (at least "-"); stops before a pair that would not fit. */
static void view_callers_text(char *out,size_t cap) {
    size_t k=0;out[0]='-';out[1]=0;
    for(unsigned i=0;i<view_caller_count;i++) {
        char item[48];int n=snprintf(item,sizeof(item),"%s%x/%x:%llu",k?",":"",view_callers[i].trigger,view_callers[i].caller,
                                     (unsigned long long)view_callers[i].count);
        if(n<=0 || k+(size_t)n+1>cap) break;
        memcpy(out+k,item,(size_t)n+1);k+=(size_t)n;
    }
}
static int view_rows_arm(void) {
    if(!view_installed || GetCurrentThreadId()!=ui_thread) return 0;
    if(!view_rows_armed) {
        memset(view_row_seen,0,sizeof(view_row_seen));view_rows=0;view_row_full=0;view_rows_started=0;
        view_caller_count=0;view_caller_other=0;view_rows_armed=1;
    }
    return 1;
}
static int view_attention_locked(view_attention_fn native,void *object) {
    unsigned char *universe=*(unsigned char*volatile*)(image+VIEW_UNIVERSE);
    volatile uint64_t *slot=(volatile uint64_t *)(view_page+VIEW_PAGE_UNIVERSE);
    if(!view_depth) *slot=(uint64_t)(uintptr_t)universe; /* the live universe between evaluations */
    if(!InterlockedCompareExchange(&view_active,0,0)) return native(object);
    view_roll();++view_calls;++view_frame_calls;
    if(view_rows_armed) view_caller_note();
    if(view_trigger_mode && (!boot_mode || boot_phase==3)) {
        int kept;if(view_trigger_apply(object,view_trigger_match(view_row_trigger()),&kept)) return kept;
    }
    if(view_ui_mode && (!boot_mode || boot_phase==3)) {
        int kept;if(view_ui_apply(object,&kept)) return kept;
    }
    int result=view_evaluate(native,object),camera=-1,evaluated=result;
    if(view_mode==VIEW_OBSERVE) {
        uint64_t saved=*slot;*slot=(uint64_t)(uintptr_t)universe;
        camera=native(object);*slot=saved;
        if(camera!=result) {++view_differ;++view_frame_differ;if(!view_first_differ) view_first_differ=stats.issued;}
        result=camera;
    }
    if(view_rows_armed) view_row_note(object,evaluated,camera);
    ++view_frame_levels[result>=0 && result<(int)VIEW_LEVELS?result:0];
    return result;
}
/* The rewritten slot-703 call. One recursive lock serializes every evaluation
 * (the page slot and the shadow player are shared); calls off the MoveWorker
 * are counted (loading may compute attention elsewhere) but evaluated alike. */
static int view_attention(void *object) {
    view_attention_fn native=(view_attention_fn)(*(void***)object)[703];
    int result=0;
    EnterCriticalSection(&view_lock);
    __try {
        if(GetCurrentThreadId()!=ui_thread) InterlockedIncrement64(&view_foreign);
        result=view_attention_locked(native,object);
    } __finally {
        LeaveCriticalSection(&view_lock);
    }
    return result;
}
static int view_install_patches(void) {
    unsigned char orig[6],mine[6];
    for(unsigned i=0;i<VIEW_SITES;i++) {
        view_site_bytes(i,0,orig);view_site_bytes(i,1,mine);
        if(!view_cas_bytes(view_sites[i].rva+3,orig,mine,4)) return 0;
        view_sites_done[i]=1;
    }
    view_call_bytes(0,orig);view_call_bytes(1,mine);
    if(!view_cas_bytes(VIEW_CALL,orig,mine,6)) return 0;
    view_call_done=1;return 1;
}
/* Restore every site still holding our bytes; returns 1 when none is left patched. */
static int view_remove_patches(void) {
    unsigned char orig[6],mine[6];int all=1;
    if(view_call_done) {
        view_call_bytes(0,orig);view_call_bytes(1,mine);
        if(view_call_state()==1 && view_cas_bytes(VIEW_CALL,mine,orig,6)) view_call_done=0;
        else if(view_call_state()==0) view_call_done=0;
        else all=0;
    }
    for(unsigned i=0;i<VIEW_SITES;i++) if(view_sites_done[i]) {
        view_site_bytes(i,0,orig);view_site_bytes(i,1,mine);
        if(view_site_state(i)==1 && view_cas_bytes(view_sites[i].rva+3,mine,orig,4)) view_sites_done[i]=0;
        else if(view_site_state(i)==0) view_sites_done[i]=0;
        else all=0;
    }
    return all;
}
static int view_arm(int mode) {
    if(view_installed) return stats.active && mode==view_mode; /* Identical rerun after a loading-UI reload. */
    if((mode!=VIEW_PLAYERS && mode!=VIEW_OBSERVE) || !stats.active || !boot_mode || boot_phase || stats.issued ||
       GetCurrentThreadId()!=ui_thread) return 0;
    if(GetModuleHandleW(L"x4lan_fixedtick_64.dll")) {view_stage="foreign_module";return 0;}
    if(!view_pins()) {view_stage="pins";return 0;}
    if(view_call_state()!=0) {view_stage="site";return 0;}
    for(unsigned i=0;i<VIEW_SITES;i++) if(view_site_state(i)!=0) {view_stage="site";return 0;}
    if(!view_lock_ready) {InitializeCriticalSection(&view_lock);view_lock_ready=1;}
    if(!view_page) view_page=view_page_near();
    if(!view_page) {view_stage="page";return 0;}
    memset(view_page,0,VIEW_PAGE_SHADOW+VIEW_SHADOW_SIZE);
    void *entry=(void*)view_attention;memcpy(view_page+VIEW_PAGE_CALL,&entry,8);
    unsigned char *universe=*(unsigned char*volatile*)(image+VIEW_UNIVERSE);
    *(volatile uint64_t *)(view_page+VIEW_PAGE_UNIVERSE)=(uint64_t)(uintptr_t)universe;
    view_tick=UINT64_MAX;view_calls=view_evaluations=view_differ=view_first_differ=view_refreshes=view_latches=view_unreadable=0;
    view_latched=view_count=view_previous_count=view_source=0;view_restored=0;view_foreign=0;view_depth=0;
    memset(view_points,0,sizeof(view_points));memset(view_previous,0,sizeof(view_previous));memset(view_previous_zone,0,sizeof(view_previous_zone));
    view_mode=mode;InterlockedExchange(&view_active,1);
    if(!view_install_patches()) {InterlockedExchange(&view_active,0);view_remove_patches();view_stage="cas";return 0;}
    view_patched=1;view_installed=1;view_stage="armed";return 1;
}
static int view_owned(void) {
    if(!view_patched) return 1;
    if(view_call_state()!=1) return 0;
    for(unsigned i=0;i<VIEW_SITES;i++) if(view_site_state(i)!=1) return 0;
    void *entry=NULL;memcpy(&entry,view_page+VIEW_PAGE_CALL,8);return entry==(void*)view_attention;
}
/* Main-thread tick check; any failure stops the gate with reason 97. */
static int view_check(void) {
    if(InterlockedCompareExchange64(&view_failures,0,0)) return 0;
    const char *why=GetModuleHandleW(L"x4lan_fixedtick_64.dll")?"foreign_module":!view_owned()?"site_changed":
        !view_pins()?"block_changed":NULL;
    if(why) {view_stage=why;InterlockedIncrement64(&view_failures);return 0;}
    return 1;
}
static void view_restore(void) {
    InterlockedExchange(&view_active,0);
    vcam_stop();
    if(!view_installed) return;
    if(view_patched) {
        if(!view_remove_patches()) return;
        view_patched=0;
    }
    view_restored=1;view_installed=0;
}
/* Zone (class 0x6d) of a component, for the latch's change test; NULL when none. */
static void *view_zone_of(void *object) {
    unsigned char *p=object;
    for(unsigned depth=0;p && depth<16;depth++) {
        void **vtable=NULL;if(!read_local(p,&vtable,8) || !vtable) return NULL;
        int (*is_a)(void *,int)=NULL;if(!read_local((unsigned char*)vtable+0x11e8,&is_a,8) || !is_a) return NULL;
        if(is_a(p,0x6d)) return p;
        if(!read_local(p+0x70,&p,8)) return NULL;
    }
    return NULL;
}
/* Recompute the attention of the viewpoint and every ancestor that keeps its
 * own attention (+0xd1; SetAttention(x,1) would otherwise set level 1). */
static void view_refresh_chain(void *object) {
    unsigned char *p=object;
    for(unsigned depth=0;p && depth<17;depth++) {
        unsigned char own=0;if(!read_local(p+0xd1,&own,1)) return;
        if(own) {
            ++view_refreshing; /* the gate's own refresh: never a client-local scope (native_gate_view_ui) */
            (view_set_attention_override?view_set_attention_override:(view_set_attention_fn)(image+VIEW_SET_ATTENTION))(p,1);
            --view_refreshing;++view_refreshes;
        }
        if(!read_local(p+0x70,&p,8)) return;
    }
}
/* Viewpoint source without pilot slots, DEFAULT 'player'
 * (native_gate_view_points_source): 'ship' latches the container the local
 * player occupies instead of the player object g_Universe+0x230. The player
 * object is not simulation-only: with client 2's camera held 70 km off its
 * anchor (cam-B5, cam-B6) the Zone implementation 0x843b20 already evaluated
 * zones 13e764/157b4d/157b53/157b59 to 6 instead of 7 at tick 2 and
 * 13e758/13e75e at tick 3 on that client alone (Zone::Move trigger 0x83497c),
 * although under view points its only per-client input is the player object's
 * position relative to the zone (0x3db8a0 through the object's +0x200 movement
 * controller), so that position follows the camera. The occupied container is
 * resolved as U::GetPlayerOccupiedShip 0x9f7a40 / Entity 0x55b150 do, read-only:
 * the first non-null entry of player+0x308..+0x310 that is also an ancestor
 * (+0x70 chain) of the player. Its position is simulation state. Trade-off,
 * identical on every client: the player's own interior (Positional 0x753350,
 * level 10) and the "ship carrying the player" rule (Ship 0x7d5110) no longer
 * see the player inside, so that ship evaluates by distance (level 9 at
 * distance 0) and its rooms follow it. No container (player outside a ship or
 * station): the player object is used and counted (ship_misses=). */
enum {VIEW_SOURCE_PLAYER,VIEW_SOURCE_SHIP};
static int view_source_mode,view_source_armed;
static uint64_t view_ship_misses;
#define VIEW_OCCUPIED_SCAN 64u
static unsigned char *view_occupied(unsigned char *player) {
    unsigned char *begin=NULL,*end=NULL,*entry=NULL,*a=NULL;
    if(!player || !read_local(player+0x308,&begin,8) || !read_local(player+0x310,&end,8) || !begin || end<begin) return NULL;
    for(unsigned i=0;!entry && i<VIEW_OCCUPIED_SCAN && begin+i*8<end;i++) if(!read_local(begin+i*8,&entry,8)) return NULL;
    if(!entry || !read_local(player+0x70,&a,8)) return NULL;
    for(unsigned depth=0;a && depth<17;depth++) {
        if(a==entry) return entry;
        if(!read_local(a+0x70,&a,8)) return NULL;
    }
    return NULL;
}
/* Before the first admitted frame, on the MoveWorker. */
static int view_source_arm(const char *name) {
    int mode=!name?-1:!strcmp(name,"player")?VIEW_SOURCE_PLAYER:!strcmp(name,"ship")?VIEW_SOURCE_SHIP:-1;
    if(mode<0) return 0;
    /* After the first tick or from the reloaded UI, only an identical re-arm is accepted. */
    if(stats.issued || GetCurrentThreadId()!=ui_thread) return view_source_armed && view_source_mode==mode;
    view_source_mode=mode;view_source_armed=1;
    view_ship_misses=0;return 1;
}
/* Measurement knob, DEFAULT OFF (native_gate_view_points_mask = 0..15, attention
 * study C9, reports/LOCKSTEP-ATTENTION-STUDY-20261101.txt): which pilot slots count
 * as view points, bit k-1 = slot k (without pilot slots bit 0 = the player object
 * or its ship). A masked slot is left out of the latched list on every client
 * alike, so its surroundings take the attention they would have without that
 * player (low attention away from the other view points: no physics scene, the
 * linear flight model, scripted combat), and every module that follows the latched
 * list (scenes, stand-in cameras, policy) follows the reduced list. The universe
 * stays identical between clients; it is a different universe than the unmasked
 * run, which is the point: the A/B difference in frame time is the price of that
 * slot's high-attention bubble. Mask 0: no view point, every evaluation runs
 * without a viewer. At the first latch the live player object, which answered
 * the evaluations before it, has its chain recomputed (as a former view point
 * would). Not a play mode: a masked player's ship flies the low-attention model
 * and cannot fire (shoot_at needs 'visible'). players mode only; before the first
 * admitted frame, on the MoveWorker. Status: mask= masked= (slots left out, summed
 * over latches). */
#define VIEW_MASK_ALL 0xfu
static unsigned view_slot_mask=VIEW_MASK_ALL;
static uint64_t view_masked;
static int view_mask_arm(double mask) {
    if(!view_installed || view_mode!=VIEW_PLAYERS || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !isfinite(mask) || mask<0 || mask>VIEW_MASK_ALL || floor(mask)!=mask) return 0;
    view_slot_mask=(unsigned)mask;view_masked=0;return 1;
}
/* Slot 6 of every admitted frame: latch the viewpoints for the next frames. */
static int view_frame_locked(uint64_t tick);
static int view_frame_latch(uint64_t tick,int refresh);
static int view_frame(uint64_t tick) {
    if(!view_installed || !view_lock_ready || !stats.active || GetCurrentThreadId()!=ui_thread || tick!=stats.issued) {view_stage="frame_guard";InterlockedIncrement64(&view_failures);return 0;}
    int ok=0;EnterCriticalSection(&view_lock);
    __try {ok=view_frame_locked(tick);} __finally {LeaveCriticalSection(&view_lock);}
    return ok;
}
/* refresh 0 (lockstep_join_passes.h: a joiner's latch at its admission, the main thread with the MoveWorker parked): the latch
 * without the attention refresh a change brings, which the session made long before; the latch at T+1 then finds the same
 * view points, as the session's does. */
static int view_frame_latch(uint64_t tick,int refresh) {
    void *points[VIEW_MAX];uint64_t ids[VIEW_MAX];unsigned n=0,source=0;const int first=!view_latched;
    /* Hybrid sectors: the split starts at this latch (every client alike); another client's owned slot is
     * no view point here, this client's own owned slot is, and evaluates only inside its sector. */
    const int hybrid_start=hyb_latch_begin(tick);int own_index=-1;
    if(pilot_slot_count) {
        source=1;
        for(unsigned i=0;i<pilot_slot_count && i<VIEW_MAX;i++) {
            pilot_slot *s=&pilot_slots[i];uint64_t id=0;
            if(!s->resolved || !s->object) continue;
            if(!((view_slot_mask>>i)&1u)) {++view_masked;continue;}
            if(hyb_latch_skip(i)) continue;
            if(!read_local(s->object+8,&id,8) || id!=s->runtime) {++view_unreadable;continue;}
            if(hyb_latch_own(i)) own_index=(int)n;
            points[n]=s->object;ids[n]=id;++n;
        }
    } else {
        unsigned char *universe=*(unsigned char*volatile*)(image+VIEW_UNIVERSE);void *p=NULL;uint64_t id=0;
        source=2;
        if(universe && read_local(universe+0x230,&p,8) && p && view_source_mode==VIEW_SOURCE_SHIP) {
            unsigned char *ship=view_occupied(p);
            if(ship) {p=ship;source=3;} else ++view_ship_misses;
        }
        if(p && !(view_slot_mask&1u)) {++view_masked;p=NULL;}
        if(p && read_local((unsigned char*)p+8,&id,8)) {points[0]=p;ids[0]=id;n=1;}
    }
    int changed=!view_latched || n!=view_count || hybrid_start;
    void *zones[VIEW_MAX];
    for(unsigned i=0;i<n;i++) {zones[i]=view_zone_of(points[i]);if(!changed && (points[i]!=view_points[i] || zones[i]!=view_previous_zone[i])) changed=1;}
    if(changed) {
        for(unsigned i=0;i<view_count;i++) {view_previous[i]=view_points[i];view_previous_ids[i]=view_ids[i];}
        view_previous_count=view_count;
    }
    memcpy(view_points,points,sizeof(points));memcpy(view_ids,ids,sizeof(ids));view_count=n;view_source=source;
    memcpy(view_previous_zone,zones,sizeof(zones));view_latched=1;++view_latches;
    hyb_view_index_set(own_index); /* before the refreshes: they evaluate with the split */
    if(refresh && changed && view_mode==VIEW_PLAYERS) {
        if(first && view_slot_mask!=VIEW_MASK_ALL) { /* the live player object answered before this latch */
            unsigned char *universe=*(unsigned char*volatile*)(image+VIEW_UNIVERSE);void *p=NULL;
            if(universe && read_local(universe+0x230,&p,8) && p) view_refresh_chain(p);
        }
        for(unsigned i=0;i<view_previous_count;i++) { /* only a still-live former viewpoint */
            uint64_t id=0;if(read_local((unsigned char*)view_previous[i]+8,&id,8) && id==view_previous_ids[i]) view_refresh_chain(view_previous[i]);
        }
        for(unsigned i=0;i<n;i++) view_refresh_chain(points[i]);
    }
    vcam_latch(); /* cutscene camera shots noted or ended since the last latch */
    hyb_latch_check(); /* the H1 boundary: owners inside their sectors, nobody else in them */
    return 1;
}
static int view_frame_locked(uint64_t tick) {return view_frame_latch(tick,1);}
static int view_status(char *out,size_t capacity) {
    int fresh=view_tick==stats.issued;char ids[VIEW_MAX*18+2]="-";int k=0;
    char callers[640];view_callers_text(callers,sizeof(callers));
    for(unsigned i=0;i<view_count && i<VIEW_MAX;i++) k+=snprintf(ids+k,sizeof(ids)-(size_t)k,"%s%llx",i?",":"",(unsigned long long)view_ids[i]);
    char levels[VIEW_LEVELS*21+1];int m=0;
    for(unsigned i=1;i<VIEW_LEVELS;i++) m+=snprintf(levels+m,sizeof(levels)-(size_t)m,"%s%llu",i>1?"/":"",(unsigned long long)(fresh?view_frame_levels[i]:0));
    int n=snprintf(out,capacity,"mode=%s armed=%ld installed=%d frame=%llu source=%s viewpoints=%u ids=%s calls=%llu evaluations=%llu "
        "levels=%s differ=%llu total_calls=%llu total_evaluations=%llu total_differ=%llu first_differ=%llu refreshes=%llu latches=%llu "
        "unreadable=%llu foreign=%lld restored=%d stage=%s failures=%lld triggers=%s:%llu/%llu/%llu/%llu/%llu blocked=%llu "
        "source_mode=%s ship_misses=%llu cutscene=%llu/%llu/%llu/%llu callers=%s callers_other=%llu ui=%s:%llu/%llu/%llu/%llu/%llu/%llu ui_blocked=%llu ui_local=%llu mask=%x masked=%llu",
        view_mode==VIEW_PLAYERS?"players":view_mode==VIEW_OBSERVE?"observe":"off",view_active,view_installed,
        (unsigned long long)(view_tick==UINT64_MAX?0:view_tick),view_source==1?"slots":view_source==2?"player":view_source==3?"ship":"none",view_count,
        view_count?ids:"-",(unsigned long long)(fresh?view_frame_calls:0),(unsigned long long)(fresh?view_frame_evaluations:0),levels,
        (unsigned long long)(fresh?view_frame_differ:0),(unsigned long long)view_calls,(unsigned long long)view_evaluations,
        (unsigned long long)view_differ,(unsigned long long)view_first_differ,(unsigned long long)view_refreshes,
        (unsigned long long)view_latches,(unsigned long long)view_unreadable,(long long)view_foreign,view_restored,view_stage,(long long)view_failures,
        view_trigger_mode==VIEW_TRIGGER_BLOCK?"block":view_trigger_mode==VIEW_TRIGGER_OBSERVE?"observe":"off",
        (unsigned long long)view_trigger_counts[0],(unsigned long long)view_trigger_counts[1],(unsigned long long)view_trigger_counts[2],
        (unsigned long long)view_trigger_counts[3],(unsigned long long)view_trigger_counts[4],(unsigned long long)view_trigger_blocked,
        view_source_mode==VIEW_SOURCE_SHIP?"ship":"player",(unsigned long long)view_ship_misses,
        (unsigned long long)view_trigger_counts[5],(unsigned long long)view_trigger_counts[6],(unsigned long long)view_trigger_counts[7],
        (unsigned long long)view_trigger_counts[8],view_rows_armed?callers:"-",(unsigned long long)view_caller_other,
        view_ui_mode==VIEW_UI_BLOCK?"block":view_ui_mode==VIEW_UI_OBSERVE?"observe":"off",
        (unsigned long long)view_ui_counts[VIEW_UI_PRESENT],(unsigned long long)view_ui_counts[VIEW_UI_VIEW],(unsigned long long)view_ui_counts[VIEW_UI_DISPATCH],
        (unsigned long long)view_ui_counts[VIEW_UI_RENDER],(unsigned long long)view_ui_counts[VIEW_UI_SLOT6],(unsigned long long)view_ui_counts[VIEW_UI_CAMERA],
        (unsigned long long)view_ui_blocked,(unsigned long long)view_ui_local,view_slot_mask,(unsigned long long)view_masked);
    return n>0 && (size_t)n<capacity?n:0;
}

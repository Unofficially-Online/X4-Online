/* Physics near every view point, DEFAULT OFF: native_gate_view_scenes = true (needs
 * native_gate_physics_scenes = 'viewpoints'; a group of lockstep_region_camera.h,
 * stop reason 165).
 *
 * A zone's physics scene comes from vtable slot 943 (U::Zone 0x83fe70, U::Highway
 * 0x5f9680): A ("Main") for the player object's sector, B ("Cutscene") for the active
 * camera's, which the scenes group makes view point 2's, otherwise none. Bodies are
 * created by Destructible slot 904 from that scene, relative to its anchor (+0x40), and
 * rays and shape casts convert through the same anchor. Natively a scene's anchor is set
 * in one place: 0x97dff0, when the player camera or a cutscene camera changes zone, calls
 * 0xbd410(scene, &zone, rebase). So two things are missing away from slot 1:
 *  - Scene B has no anchor. The stand-in camera never changes parent, so +0x40 stays NULL:
 *    st-C2Ra's slot 2 ship is at 0,0,0 in scene 1 on its first tick and then in coordinates
 *    of its own zone, and bodies of two zones overlap in one frame.
 *  - A third sector has no scene at all.
 *
 * With this group:
 *  - Scenes C and D are made at the first frame like A and B (0x83f7d0: SMem::Allocate 0x50,
 *    0xba090 with the name and three times 0x10000). The constructor registers them where
 *    Scene::Step and the asset loader find every scene. With more than four pilot slots, E..H
 *    too, as many as the slots can need (pilot_slot_count - 2: every client has the same slots).
 *  - B, C and D belong to sectors, not to slots: a table of (sector, scene), kept in rc_frame
 *    in slot order. A sector keeps its scene while a view point is in it, because a body
 *    stays in the scene it was made in; view point 1's sector is A's and never in the table.
 *    A sector no view point is in gives its scene back, and that scene is free again once the
 *    bodies of the old sector have died with their attention: as soon as its PhysicsSystem
 *    holds no body (counted, early=), at the latest VSC_GRACE ticks after the release. The
 *    body counts are the lockstep's own (the physics trace's num= and bodies= agree between
 *    clients per step), so every client frees the scene at the same tick. No scene free:
 *    counted (blocked=), that view point stays without one this frame.
 *  - Each held scene is anchored like A: the zone of the first view point in its sector, by
 *    the same call with the same rule (rebase when the old and new anchor share a cluster).
 *  - Slot 943 of U::Zone and U::Highway (data CAS, no thunk) answers from the table first,
 *    then natively, and a native B that the table does not hold is no scene.
 *  - UniverseFrameUpdate's step of B (0x9fe48b) goes through a stub that also steps C and D.
 *
 * Stop: rc_restore clears vsc_active first, so a stub still in place steps nothing more and
 * slot 943 answers natively, then the entries go back by CAS. C and D stay allocated and are
 * not destroyed: bodies still in them are released by their components' own teardown through
 * record+0x18, which needs the scene alive, and 0x841b70 (universe teardown) only knows A, B
 * and Navigation. They hold memory until the process ends.
 *
 * A view point that changes sector (17 Nov, found with a real mission's mines in a sector only
 * player 2 had entered):
 *  - The table is brought up to the new latch BEFORE the attention refresh that latch runs
 *    (vsc_before_refresh, called by the view points' latch; rc_frame's own call later in the
 *    frame then finds nothing to do). Before, the refresh ran first: X4 asked for the new
 *    sector's scene while the table did not hold that sector yet, so everything whose level
 *    rose around the arrival got no body, for as long as its level stayed.
 *  - A view point that held its scene alone and is now in a sector without one takes the scene
 *    along (follows=), as X4's scene A goes with its player: the entry changes its sector, the
 *    anchor moves to the new zone, the ship's own bodies stay valid in it and nothing waits for
 *    a grace. The bodies of the sector it left die with their attention, which the latch now
 *    refreshes in the same frame (lockstep_view_points.h); what is left of them for the next
 *    second is where X4's own scene A is after its player took a gate.
 *
 * View point 1 and the others (17 Nov, measured with both players' ships: a ship that arrives
 * is given its bodies again in the scene its new zone answers, slot 1's at the re-parent
 * itself, another slot's at the next latch's refresh):
 *  - View point 1 arrives in a sector that has a table scene: the sector keeps that scene, view
 *    point 1 is one of its holders, and A idles (keeps=). Before, the table gave the sector up
 *    because it was "home" now: the ships and everything already there stayed in the table's
 *    scene and whatever came afterwards went to A.
 *  - View point 1 leaves a sector another view point stays in, whose bodies are in A: A stays
 *    with that sector (pins=). X4 moves A's anchor to slot 1's new zone inside the re-parent
 *    (0x97dff0, the call at 0x97e4f9); that call goes through vsc_anchor_call, which skips it
 *    and pins A when another latched view point is still in A's anchor sector. While A is
 *    pinned X4's anchor call for A is skipped, the latch anchors A at the first view point in
 *    its sector like a table scene, and a sector for which X4 answers A (the universe player's)
 *    is given a table scene at that moment if a view point is in it (lazy=): slot 1's ship
 *    asks at its re-parent, a frame before the next latch. A is X4's own again when nobody is
 *    left in its sector or view point 1 is back in it (unpins=).
 *
 * Not here: a sector that has A because view point 1 was alone in it, which another view point
 * then enters and view point 1 leaves in the same tick. Traffic and highway parameters still
 * answer for two sectors (uncovered= in the status). */
#define VSC_ENTRIES (VIEW_MAX-1u)   /* B and the new scenes: one per sector other than view point 1's */
_Static_assert(VSC_EXTRA==VSC_ENTRIES-1u,"VSC_EXTRA (lockstep_contact.h): the new scenes C, D (always) and E..H (more than four pilot slots)");
#define VSC_GRACE 600u
#define VSC_BODIES 0x10000u
#define VSC_SCENE_A 0x38f0748u
#define VSC_SCENE_B 0x38f0740u
#define VSC_ZONE 0x83fe70u
#define VSC_ZONE_SLOT 0x2b67df8u
#define VSC_HIGHWAY 0x5f9680u
#define VSC_HIGHWAY_SLOT 0x2b0af58u
#define VSC_CTOR 0xba090u
#define VSC_ANCHOR 0xbd410u
#define VSC_STEP 0xba600u
typedef void *(*vsc_ctor_fn)(void *,void *,uint64_t,uint32_t,uint32_t,uint32_t);
typedef void (*vsc_anchor_fn)(void *,void **,unsigned char);
typedef void (*vsc_step_fn)(void *,double);
typedef void *(*vsc_zone_fn)(void *);
typedef struct {char text[16];uint64_t size,capacity;} vsc_name_t; /* std::string, inline */
typedef struct {void *scene;void *volatile sector;void *point;uint64_t released;uint32_t view;} vsc_entry_t;
static vsc_ctor_fn vsc_ctor_native;     /* tests only: replaces 0xba090 */
static vsc_anchor_fn vsc_anchor_native; /* tests only: replaces 0xbd410 */
static vsc_step_fn vsc_step_native;     /* tests only: replaces 0xba600 */
static vsc_zone_fn vsc_zone_native,vsc_highway_native; /* tests only: replace slot 943's natives */
static vsc_entry_t vsc_entries[VSC_ENTRIES];
static volatile LONG vsc_active;
static int vsc_armed,vsc_created,vsc_installed;
static uint64_t vsc_steps,vsc_moves,vsc_rebases,vsc_assigns,vsc_releases,vsc_early,vsc_blocked_ticks,vsc_follows,vsc_pre;
/* Scene A while it is pinned to a sector view point 1 left (sector NULL: X4's own, with view point 1). */
static vsc_entry_t vsc_a;
static uint64_t vsc_pins,vsc_unpins,vsc_lazy,vsc_keeps,vsc_anchor_skips;
/* Which entry served each view point's sector at the last latch (-1 none), and whose view point that was. */
static int vsc_view_entry[VIEW_MAX];
static uint64_t vsc_view_id[VIEW_MAX];
static void vsc_view_forget(void) {for(unsigned i=0;i<VIEW_MAX;i++) {vsc_view_entry[i]=-1;vsc_view_id[i]=0;}}
static unsigned vsc_blocked_now;
static int vsc_read_ok(void *p,uint64_t id) {uint64_t now=0;return p && read_local((unsigned char*)p+8,&now,8) && now==id;}
/* How many new scenes: C and D, or one per further slot beyond four (B and the new ones serve every sector but slot 1's). */
static unsigned vsc_extra_count(void) {const unsigned n=pilot_slot_count>4u?pilot_slot_count-2u:2u;return n<VSC_EXTRA?n:VSC_EXTRA;}
/* B from the image, C, D (E..) new. Frame thread, first frame. */
static void *vsc_new_scene(void) {
    void *self=rid_allocate(0x50);if(!self) return NULL;
    vsc_name_t name;memset(&name,0,sizeof(name));memcpy(name.text,"Cutscene",8);name.size=8;name.capacity=15;
    return (vsc_ctor_native?vsc_ctor_native:(vsc_ctor_fn)(void *)(image+VSC_CTOR))(self,&name,0,VSC_BODIES,VSC_BODIES,VSC_BODIES);
}
static int vsc_create(void) {
    void *b=NULL;if(!read_local(image+VSC_SCENE_B,&b,8) || !b) return 0;
    void *made[VSC_EXTRA];const unsigned n=vsc_extra_count();
    for(unsigned i=0;i<n;i++) if(!(made[i]=vsc_new_scene())) return 0;
    memset(vsc_entries,0,sizeof(vsc_entries));vsc_entries[0].scene=b;vsc_view_forget();
    memset(&vsc_a,0,sizeof(vsc_a));read_local(image+VSC_SCENE_A,&vsc_a.scene,8);
    for(unsigned i=0;i<n;i++) {vsc_entries[1+i].scene=made[i];vsc_extra[i]=made[i];}
    vsc_created=1;return 1;
}
/* No body left in a scene: its PhysicsSystem (scene+0x20) counts none (+0x28) and has none in its
 * active list (+0x680), the physics trace's num= and bodies=. Unreadable is not empty. */
static int vsc_empty(void *scene) {
    unsigned char *system=NULL;uint32_t num=1;uint64_t active=1;
    return scene && read_local((unsigned char*)scene+0x20,&system,8) && system &&
           read_local(system+0x28,&num,4) && read_local(system+0x680,&active,8) && !num && !active;
}
/* 0 held, still waiting or no scene (an entry beyond vsc_extra_count), 1 free (never used or the grace over), 2 free early
 * (released and empty). */
static int vsc_free(const vsc_entry_t *e,uint64_t tick) {
    if(e->sector || !e->scene) return 0;
    if(!e->released || tick>=e->released+VSC_GRACE) return 1;
    return vsc_empty(e->scene)?2:0;
}
/* The table for this frame, from the latched view points; home is view point 1's sector (A's). */
static void vsc_latch_home(uint64_t tick,void *const home) {
    void *points[VIEW_MAX],*sectors[VIEW_MAX],*own[VIEW_MAX];
    memset(points,0,sizeof(points));memset(sectors,0,sizeof(sectors));memset(own,0,sizeof(own));
    for(unsigned k=0;k<VSC_ENTRIES;k++) vsc_entries[k].point=NULL;
    vsc_a.point=NULL;
    /* View point 1's sector is X4's own A's, unless the table holds that sector (it had a scene before view point 1
     * came) or A is pinned to another sector (then view point 1's needs a table scene like any other). */
    int home_native=home!=NULL;
    for(unsigned k=0;home && k<VSC_ENTRIES;k++) if(vsc_entries[k].sector==home) home_native=0;
    if(vsc_a.sector && vsc_a.sector!=home) home_native=0;
    /* Who still holds a scene: the first view point in slot order. The others in a held sector need nothing more. */
    for(unsigned i=0;i<view_count && i<VIEW_MAX;i++) {
        points[i]=view_points[i];sectors[i]=i?(vsc_read_ok(points[i],view_ids[i])?rc_ancestor(points[i],0x58):NULL):home;
        if(sectors[i] && vsc_a.sector && sectors[i]==vsc_a.sector) { /* A's, pinned */
            if(!vsc_a.point) {vsc_a.point=points[i];vsc_a.view=i;}
            sectors[i]=NULL;
        }
        if(home_native && sectors[i]==home) sectors[i]=NULL;
        own[i]=sectors[i];
        if(!sectors[i]) continue;
        for(unsigned k=0;k<VSC_ENTRIES;k++) if(vsc_entries[k].sector==sectors[i]) {
            if(!vsc_entries[k].point) {vsc_entries[k].point=points[i];vsc_entries[k].view=i;if(!i && vsc_view_entry[0]!=(int)k) ++vsc_keeps;}
            sectors[i]=NULL;break;
        }
    }
    /* A goes back to X4 when nobody is left in its sector, or when view point 1 is in it again. */
    if(vsc_a.sector && (!vsc_a.point || vsc_a.sector==home)) {vsc_a.sector=NULL;vsc_a.point=NULL;++vsc_unpins;}
    /* A view point whose sector's scene nobody holds any more and whose new sector has none: the scene goes with it. */
    for(unsigned i=1;i<view_count && i<VIEW_MAX;i++) {
        const int k=vsc_view_entry[i];
        if(!sectors[i] || k<0 || (unsigned)k>=VSC_ENTRIES || vsc_view_id[i]!=view_ids[i]) continue;
        vsc_entry_t *const e=&vsc_entries[k];int held=0;
        if(!e->sector || e->point) continue;
        for(unsigned j=0;j<VSC_ENTRIES;j++) held|=vsc_entries[j].sector==sectors[i];
        if(held) continue;
        e->point=points[i];e->view=i;e->released=0;MemoryBarrier();e->sector=sectors[i];++vsc_follows;
    }
    /* Nobody in it any more: back to the pool once empty, after the grace at the latest. */
    for(unsigned k=0;k<VSC_ENTRIES;k++) if(vsc_entries[k].sector && !vsc_entries[k].point) {
        vsc_entries[k].sector=NULL;vsc_entries[k].released=tick;++vsc_releases;
    }
    /* New sectors, in slot order, to the first free scene (B first). */
    vsc_blocked_now=0;
    for(unsigned i=0;i<view_count && i<VIEW_MAX;i++) {
        if(!sectors[i]) continue;
        int held=0;
        for(unsigned k=0;k<VSC_ENTRIES;k++) held|=vsc_entries[k].sector==sectors[i];
        if(held) continue;
        unsigned k=0;int free=0;
        while(k<VSC_ENTRIES && !(free=vsc_free(&vsc_entries[k],tick))) ++k;
        if(k==VSC_ENTRIES) {++vsc_blocked_now;continue;}
        if(free==2) ++vsc_early;
        vsc_entries[k].point=points[i];vsc_entries[k].view=i;vsc_entries[k].released=0;MemoryBarrier();vsc_entries[k].sector=sectors[i];++vsc_assigns;
    }
    if(vsc_blocked_now) ++vsc_blocked_ticks;
    vsc_view_forget();
    for(unsigned i=0;i<view_count && i<VIEW_MAX;i++) for(unsigned k=0;own[i] && k<VSC_ENTRIES;k++) if(vsc_entries[k].sector==own[i]) {
        vsc_view_entry[i]=(int)k;vsc_view_id[i]=view_ids[i];break;
    }
}
static void vsc_latch(uint64_t tick) {vsc_latch_home(tick,rc_view_sector58);}
/* The native anchor call for one held scene, when its view point's zone is not the anchor any more. */
static void vsc_anchor(vsc_entry_t *e) {
    unsigned char *scene=e->scene,*zone=rc_ancestor(e->point,0x6d);if(!scene || !zone) return;
    unsigned char *now=*(unsigned char *volatile *)(scene+0x40);if(now==zone) return;
    const unsigned char rebase=now && rc_ancestor(now,0xf)==rc_ancestor(zone,0xf);
    /* As 0x97e4xx holds the zone across the call; the scene takes its own reference, so this one is never the last. */
    volatile LONG *state=(volatile LONG *)(zone+0x44),*count=(volatile LONG *)(zone+0x40);
    InterlockedCompareExchange(state,2,1);InterlockedIncrement(count);
    void *held=zone;(vsc_anchor_native?vsc_anchor_native:(vsc_anchor_fn)(void *)(image+VSC_ANCHOR))(scene,&held,rebase);
    if(!InterlockedDecrement(count)) rc_fail("scene_anchor_ref");
    ++vsc_moves;vsc_rebases+=rebase;
}
/* The anchors of every held scene, pinned A's too. */
static void vsc_anchors(void) {
    for(unsigned k=0;k<VSC_ENTRIES;k++) if(vsc_entries[k].sector && vsc_entries[k].point) vsc_anchor(&vsc_entries[k]);
    if(vsc_a.sector && vsc_a.point) vsc_anchor(&vsc_a);
}
/* rc_frame, after view point 1 is latched. */
static int vsc_frame(uint64_t tick) {
    if(!vsc_created) {
        if(!vsc_create()) return 0;
        InterlockedExchange(&vsc_active,1);
    }
    vsc_latch(tick);
    vsc_anchors();
    return 1;
}
/* The view points' latch, before its attention refresh (header): the table and the anchors for the view points it
 * just latched. Not before the scenes exist (the first frame's creation stays vsc_frame's) and not without view
 * point 1, whose sector is A's. */
static void vsc_before_refresh(uint64_t tick) {
    uint64_t id=0;
    if(!vsc_armed || !vsc_created || !InterlockedCompareExchange(&vsc_active,0,0) || !view_count || !view_points[0] ||
       !read_local((unsigned char*)view_points[0]+8,&id,8) || id!=view_ids[0]) return;
    vsc_latch_home(tick,rc_ancestor(view_points[0],0x58));
    vsc_anchors();
    ++vsc_pre;
}
/* X4's anchor call for a scene (0x97e4f9 in 0x97dff0: the player camera's anchor changed zone; with the context group
 * that is slot 1's ship on every client). For A: skipped while A is pinned; and when the new zone is in another
 * sector than A's anchor and another latched view point is still in that sector, whose bodies are in A (the table
 * does not hold it), A is pinned to it instead of going along. Everything else is X4's call. */
static void vsc_anchor_call(void *scene,void **zone,unsigned char rebase) {
    const vsc_anchor_fn native=vsc_anchor_native?vsc_anchor_native:(vsc_anchor_fn)(void *)(image+VSC_ANCHOR);
    InterlockedIncrement64(&rc_stub_calls[RC_VIEW_ANCHOR_CALL]);
    if(InterlockedCompareExchange(&vsc_active,0,0) && vsc_a.scene && scene==vsc_a.scene && zone && *zone) {
        if(vsc_a.sector) {++vsc_anchor_skips;return;}
        unsigned char *const now=*(unsigned char *volatile *)((unsigned char*)scene+0x40);
        void *const was=now?rc_ancestor(now,0x58):NULL,*const next=rc_ancestor(*zone,0x58);
        int tabled=0;
        for(unsigned k=0;was && k<VSC_ENTRIES;k++) tabled|=vsc_entries[k].sector==was;
        for(unsigned i=1;was && next && was!=next && !tabled && i<view_count && i<VIEW_MAX;i++) {
            if(!vsc_read_ok(view_points[i],view_ids[i]) || rc_ancestor(view_points[i],0x58)!=was) continue;
            vsc_a.point=view_points[i];vsc_a.view=i;vsc_a.released=0;MemoryBarrier();vsc_a.sector=was;++vsc_pins;++vsc_anchor_skips;
            return;
        }
    }
    native(scene,zone,rebase);
}
/* A is pinned elsewhere and X4 answers A for this sector (the universe player's): a table scene now, if a latched
 * view point is in the sector and a scene is free. Not in a client's own presentation scope: what a client asks for
 * its own display must not change the table. */
static void *vsc_lazy_scene(void *sector) {
    if(InterlockedCompareExchange(&pres_open,0,0)) return NULL;
    for(unsigned i=0;i<view_count && i<VIEW_MAX;i++) {
        if(!vsc_read_ok(view_points[i],view_ids[i]) || rc_ancestor(view_points[i],0x58)!=sector) continue;
        for(unsigned k=0;k<VSC_ENTRIES;k++) {
            vsc_entry_t *const e=&vsc_entries[k];
            if(!vsc_free(e,stats.issued)) continue;
            e->point=view_points[i];e->view=i;e->released=0;MemoryBarrier();e->sector=sector;++vsc_lazy;++vsc_assigns;
            vsc_anchor(e);
            return e->scene;
        }
        return NULL;
    }
    return NULL;
}
/* Slot 943: the table, pinned A, then the native answer; a native B the table does not hold is no scene, and a
 * native A while A is pinned to another sector is a table scene made now, or none. */
static void *vsc_answer(void *zone,vsc_zone_fn native) {
    if(!InterlockedCompareExchange(&vsc_active,0,0)) return native(zone);
    void *sector=rc_ancestor(zone,0x58);
    for(unsigned k=0;sector && k<VSC_ENTRIES;k++) if(vsc_entries[k].sector==sector) return vsc_entries[k].scene;
    void *const pinned=vsc_a.sector;
    if(sector && pinned==sector) return vsc_a.scene;
    void *scene=native(zone);
    if(scene==vsc_entries[0].scene) return NULL;
    if(scene && pinned && scene==vsc_a.scene) return sector?vsc_lazy_scene(sector):NULL;
    return scene;
}
static void *vsc_zone_scene(void *zone) {return vsc_answer(zone,vsc_zone_native?vsc_zone_native:(vsc_zone_fn)(void *)(image+VSC_ZONE));}
static void *vsc_highway_scene(void *zone) {return vsc_answer(zone,vsc_highway_native?vsc_highway_native:(vsc_zone_fn)(void *)(image+VSC_HIGHWAY));}
/* UniverseFrameUpdate's step of B, then C, D (E..). */
static void vsc_step(void *scene,double time) {
    const vsc_step_fn step=vsc_step_native?vsc_step_native:(vsc_step_fn)(void *)(image+VSC_STEP);
    InterlockedIncrement64(&rc_stub_calls[RC_VIEW_STEP_CALL]);
    step(scene,time);
    if(!InterlockedCompareExchange(&vsc_active,0,0)) return;
    for(unsigned i=0;i<VSC_EXTRA;i++) if(vsc_extra[i]) {step(vsc_extra[i],time);++vsc_steps;}
}
static int vsc_swap(unsigned slot,void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+slot,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void *volatile *)(image+slot),value,expected);
    VirtualProtect(image+slot,8,old,&ignored);return actual==expected;
}
static int vsc_slots_native(void) {
    return *(void *volatile *)(image+VSC_ZONE_SLOT)==(void *)(image+VSC_ZONE) && *(void *volatile *)(image+VSC_HIGHWAY_SLOT)==(void *)(image+VSC_HIGHWAY);
}
static int vsc_install(void) {
    if(!vsc_swap(VSC_ZONE_SLOT,image+VSC_ZONE,(void *)vsc_zone_scene)) return 0;
    if(!vsc_swap(VSC_HIGHWAY_SLOT,image+VSC_HIGHWAY,(void *)vsc_highway_scene)) {vsc_swap(VSC_ZONE_SLOT,(void *)vsc_zone_scene,image+VSC_ZONE);return 0;}
    vsc_installed=1;return 1;
}
static int vsc_owned(void) {
    return !vsc_installed || (*(void *volatile *)(image+VSC_ZONE_SLOT)==(void *)vsc_zone_scene && *(void *volatile *)(image+VSC_HIGHWAY_SLOT)==(void *)vsc_highway_scene);
}
static int vsc_remove(void) {
    if(!vsc_installed) return 1;
    const int zone=vsc_swap(VSC_ZONE_SLOT,(void *)vsc_zone_scene,image+VSC_ZONE),highway=vsc_swap(VSC_HIGHWAY_SLOT,(void *)vsc_highway_scene,image+VSC_HIGHWAY);
    if(zone && highway) vsc_installed=0;
    return zone && highway;
}
static void vsc_status(char *out,size_t capacity) {
    uint64_t anchors[VSC_ENTRIES];
    for(unsigned k=0;k<VSC_ENTRIES;k++) {
        void *zone=NULL;anchors[k]=0;
        if(vsc_entries[k].scene && read_local((unsigned char*)vsc_entries[k].scene+0x40,&zone,8) && zone) read_local((unsigned char*)zone+8,&anchors[k],8);
    }
    snprintf(out,capacity,"%d,slots:%u/%u/%u,anchors:%llx/%llx/%llx,steps:%llu,moves:%llu,rebases:%llu,assigns:%llu,releases:%llu,early:%llu,blocked:%llu/%u,follows:%llu,pre:%llu,a:%u/%llu/%llu/%llu/%llu/%llu",(int)vsc_active,
        vsc_entries[0].sector?vsc_entries[0].view+1u:0u,vsc_entries[1].sector?vsc_entries[1].view+1u:0u,vsc_entries[2].sector?vsc_entries[2].view+1u:0u,
        (unsigned long long)anchors[0],(unsigned long long)anchors[1],(unsigned long long)anchors[2],(unsigned long long)vsc_steps,(unsigned long long)vsc_moves,
        (unsigned long long)vsc_rebases,(unsigned long long)vsc_assigns,(unsigned long long)vsc_releases,(unsigned long long)vsc_early,(unsigned long long)vsc_blocked_ticks,vsc_blocked_now,
        (unsigned long long)vsc_follows,(unsigned long long)vsc_pre,vsc_a.sector?vsc_a.view+1u:0u,(unsigned long long)vsc_pins,(unsigned long long)vsc_unpins,
        (unsigned long long)vsc_lazy,(unsigned long long)vsc_keeps,(unsigned long long)vsc_anchor_skips);
}

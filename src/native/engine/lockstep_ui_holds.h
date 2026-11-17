/* A client's UI does not hold temporary components in the simulation (native_gate_ui_holds = true, a session default;
 * no stop reason: a pin that does not match leaves X4 as it is and says so in the status).
 *
 * Seen 17 Nov (two scripted clients at a station, the undock test, while the PC was short of memory): from tick 3129
 * client 2 had one small ship more than client 1, for good: one more Ship (ShipSmall::vfunc612), its engine and
 * weapon, a pilot made by a script, one more instance of every NPC state machine cue, all through
 * U::MT::MassTrafficBehavior::vfunc20 (0x897440) <- 0x8bfb40, the routine that makes a mass traffic member a real
 * ship. The world part differed at the next census (one object more), the runtime ID word from that tick on.
 *
 * What X4 does [F, pinned 9.00]:
 *   - 0x8bfb40 takes a ship of the member's macro out of a reuse cache (a tree at image 0x2f5f190, macro -> ship)
 *     and only builds one (0x133d70) when the cache has none. A real traffic ship that leaves goes back into the
 *     cache: U::Ship::vfunc336 (0x7cda50, the handler of U::RemoveTempComponentEvent) takes a hold on it, puts it
 *     into the tree and takes it off its parent.
 *   - Zone::Move's temp component update 0x8be450 decides how a real traffic ship ends. One that is in the
 *     network's set of held components (network +0x2d0, ID -> count), or that the player camera is anchored to, stays
 *     until its time is up and is then KILLED (vfunc 0x1548, 'removed'). Any other is sent the remove event (so it
 *     goes into the cache) as soon as it is out of range of the camera, or when its time is up.
 *   - The set is counted by 0x6e54c0(component, on) -> 0x8bef90. Its callers: scripts (SetObjectPersistenceAction,
 *     tow, tug: the simulation, alike everywhere) and the UI: 0x8e8070 / 0x8e8210 (a counted map at the Universe
 *     +0x430), called by the UI's messages about a component (UI::ICom::Message, 0xaef620, 0xaea660, 0xade3f0) for
 *     every class 0x49 component they refer to. For a component from another temporary source (its source's class
 *     0xe) 0x6e54c0 counts on the source instead; the same two UI callers.
 * So what a client's own UI happens to refer to (what its HUD or a menu shows, each client's own, at each client's
 * own time) decides in the simulation how long a traffic ship lives and whether it is killed or kept for reuse; the
 * caches then differ, and the next ship of that macro is reused on one client and built on the other. Seen with an
 * execute breakpoint on 0x8bef90 in a client of an ordinary run: two pairs of calls in 100 seconds, both from
 * 0xaef620 (stack 6e5572 8e81ce aef668 addcce).
 *
 * Here: the two calls of 0x6e54c0 inside 0x8e8070 (0x8e81c9, on) and 0x8e8210 (0x8e829b, off) point at a wrapper
 * that does nothing while the module is on: the UI's own map is counted as before, but nothing of it reaches the
 * traffic network or a temporary source. Holds X4 took before the arming (read from the UI's map then) are given
 * back natively when the UI lets go of them.
 * What it costs: a traffic ship that a player has as its target, or that a menu shows, can leave while it is shown
 * (X4 kept it until its time was up); the target is then lost as for any ship that leaves.
 * Not covered: the player camera anchored to a traffic ship (a target view of one).
 * Status holds=installed/on/adds/drops/passed_adds/passed_drops/native. */
#define UIH_KEEP 0x6e54c0u
#define UIH_KEEP_SIZE 0x142u
#define UIH_KEEP_SHA "5945adcf1a71df19845afecb5b40d9b0046dda6c2e505ab9d8d1d046eede0840"
#define UIH_ADD 0x8e8070u
#define UIH_ADD_SIZE 0x19cu
#define UIH_ADD_SHA "9158d38d408675e7c23b75c7bc9e71eb43f71553d1d7a2ce59dca218e08c7162"
#define UIH_DROP 0x8e8210u
#define UIH_DROP_SIZE 0x23du
#define UIH_DROP_SHA "0b492ddbbb23b0e0545c09d9cdb7904c836393ab47dd49dd8209f6b1aea91942"
#define UIH_SITE_ADD 0x8e81c9u
#define UIH_SITE_DROP 0x8e829bu
#define UIH_STUB_ADD 0xf00u               /* on the slots' near page, far behind the others */
#define UIH_STUB_DROP 0xf20u
#define UIH_MAP 0x430u                    /* Universe: the UI's counted map of components (node +0x20 component, +0x28 count) */
#define UIH_NATIVE 256u
typedef uint64_t (*uih_keep_fn)(void *,unsigned char);
static uih_keep_fn uih_native_keep;       /* the image's 0x6e54c0, a test's */
static int uih_installed;
static volatile LONG uih_on;
static unsigned char uih_patched[2][5];
static struct {void *component;unsigned count;} uih_held[UIH_NATIVE];   /* holds X4 took natively before the arming */
static volatile LONG uih_lock;
static volatile LONG64 uih_adds,uih_drops,uih_passed_adds,uih_passed_drops;
static unsigned uih_native_count;
static void uih_enter(void) {while(InterlockedCompareExchange(&uih_lock,1,0)) YieldProcessor();}
static void uih_leave(void) {InterlockedExchange(&uih_lock,0);}
/* A hold X4 took natively for this component is given back natively; 1 when there was one. */
static int uih_take_native(void *component) {
    int found=0;
    uih_enter();
    for(unsigned i=0;i<UIH_NATIVE;i++) if(uih_held[i].component==component && uih_held[i].count) {
        if(!--uih_held[i].count) uih_held[i].component=NULL;
        found=1;break;
    }
    uih_leave();
    return found;
}
static void uih_note_native(void *component,unsigned count) {
    if(!component || !count) return;
    uih_enter();
    unsigned free_at=UIH_NATIVE;
    for(unsigned i=0;i<UIH_NATIVE;i++) {
        if(uih_held[i].component==component) {uih_held[i].count+=count;free_at=UIH_NATIVE+1;break;}
        if(!uih_held[i].component && free_at==UIH_NATIVE) free_at=i;
    }
    if(free_at<UIH_NATIVE) {uih_held[free_at].component=component;uih_held[free_at].count=count;}
    uih_leave();
}
static uint64_t uih_keep(void *component,unsigned char on) {
    if(!InterlockedCompareExchange(&uih_on,0,0)) {   /* off: X4's own, and remembered so it is given back as taken */
        if(on) {uih_note_native(component,1);InterlockedIncrement64(&uih_passed_adds);}
        else {(void)uih_take_native(component);InterlockedIncrement64(&uih_passed_drops);}
        return uih_native_keep?uih_native_keep(component,on):0;
    }
    if(on) {InterlockedIncrement64(&uih_adds);return 0;}
    if(uih_take_native(component)) {InterlockedIncrement64(&uih_passed_drops);return uih_native_keep?uih_native_keep(component,0):0;}
    InterlockedIncrement64(&uih_drops);
    return 0;
}
static uint64_t uih_keep_add(void *component,unsigned char on) {return uih_keep(component,on);}
static uint64_t uih_keep_drop(void *component,unsigned char on) {return uih_keep(component,on);}
/* The UI's map as it is at the arming: every component it holds now, with its count (in-order walk, bounded). */
static void uih_read_map(unsigned char *node,unsigned char *head,unsigned depth) {
    unsigned char *left=NULL,*right=NULL,*component=NULL;int count=0;
    if(!node || node==head || depth>48 || uih_native_count>=4096) return;
    if(!read_local(node+8,&left,8) || !read_local(node+0x10,&right,8) || !read_local(node+0x20,&component,8) || !read_local(node+0x28,&count,4)) return;
    uih_read_map(left,head,depth+1);
    if(component && count>0) {uih_note_native(component,(unsigned)count);++uih_native_count;}
    uih_read_map(right,head,depth+1);
}
static int uih_arm(void) {
    if(uih_installed) {InterlockedExchange(&uih_on,1);return 1;}
    if(stats.issued || GetCurrentThreadId()!=ui_thread || !pnat_base) return 0;
    if(!hash_ok(image+UIH_KEEP,UIH_KEEP_SIZE,UIH_KEEP_SHA) || !hash_ok(image+UIH_ADD,UIH_ADD_SIZE,UIH_ADD_SHA) ||
       !hash_ok(image+UIH_DROP,UIH_DROP_SIZE,UIH_DROP_SHA)) return 0;
    if(!pslot_page && !(pslot_page=pslot_alloc_page())) return 0;
    uih_native_keep=(uih_keep_fn)(image+UIH_KEEP);
    memset(uih_held,0,sizeof(uih_held));uih_native_count=0;uih_adds=uih_drops=uih_passed_adds=uih_passed_drops=0;
    {   unsigned char *universe=NULL,*root=NULL;
        if(read_local(pnat_base+VPL_UNIVERSE,&universe,8) && universe && read_local(universe+UIH_MAP+8,&root,8)) uih_read_map(root,universe+UIH_MAP,0);
    }
    if(!pres_site_install(UIH_SITE_ADD,UIH_KEEP,UIH_STUB_ADD,(void*)uih_keep_add,uih_patched[0])) return 0;
    if(!pres_site_install(UIH_SITE_DROP,UIH_KEEP,UIH_STUB_DROP,(void*)uih_keep_drop,uih_patched[1])) {
        (void)pres_site_uninstall(UIH_SITE_ADD,UIH_KEEP,uih_patched[0]);return 0;
    }
    uih_installed=1;InterlockedExchange(&uih_on,1);return 1;
}
/* The sites stay ours when X4's calls cannot be put back (the wrapper then passes everything through). */
static void uih_restore(void) {
    InterlockedExchange(&uih_on,0);
    if(!uih_installed) return;
    if(pres_site_uninstall(UIH_SITE_ADD,UIH_KEEP,uih_patched[0]) && pres_site_uninstall(UIH_SITE_DROP,UIH_KEEP,uih_patched[1])) uih_installed=0;
}
static int uih_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"holds=%d/%ld/%lld/%lld/%lld/%lld/%u",uih_installed,(long)uih_on,(long long)uih_adds,(long long)uih_drops,
        (long long)uih_passed_adds,(long long)uih_passed_drops,uih_native_count);
    return n>0 && (size_t)n<capacity?n:0;
}

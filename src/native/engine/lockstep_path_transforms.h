/* Determinism fix, DEFAULT OFF: native_gate_path_transforms = 'sim' | 'observe'.
 *
 * reg-tour19wd (tour, 2 clients): at 2467 the player's teleport drops the battle to attention 2 and three
 * ships take the Linear model on both clients alike. From 2468 176c95's SetTarget (UpdatePathing 0xe8c380,
 * [model+0x18] at 0xe8d774, return 0xe8d777) gets a target 0.25 m and about 1 degree apart on the two
 * clients; relay mismatch from 2468. Its path point is relative to 176ad5, client 2's dead slot ship. At that
 * tick 176ad5 has no Agent (object+0x200 is a LinearDecelerationController 0x2c3f5f0, the wreck's drift), so
 * the conversion of the point into the agent's frame (0xe8d60f -> 0xe8bd20 -> 0xe8a4a0 -> the hierarchy walk
 * 0xe8a110) finds no pathing state for it (0xeadc70: the manager's map +0x130 has no entry) and converts the
 * span with 0x3dde10(176ad5, matrix, zone, 0) at 0xe8a416. Off the MoveWorker (TLS+0x314 0, every pathing
 * batch job) 0x3dde10 composes each link from the PUBLISHED component view cache (object+0xe0+[0x38f4eb4]*0x80,
 * pose block +0x28): the render's copy, current only where and when a client renders, whose index the thread
 * without a view switches at 0xef428f (it waits for UpdateBatch's reader count 0x3d79110, which a batch holds
 * only while it runs, so between two batches of one pass). On a cache miss it takes the link's own transform
 * (slot 0x1440) at the job thread's TLS game time, which jobs never sync (0: the wreck at its start pose).
 * Either way the reference's pose is client-local and timing-dependent. Agents read each other only through
 * the pathing read banks (0xe8a740 / 0xe8acc0 via 0xeadc70, index 0x38f4ee4), which the pass does not write;
 * the order of the SetTarget calls (job scheduling) is harmless, each call writes its own agent's model.
 * Both flight models take this path: the Linear branch's conversions (0xe8d502, 0xe8d60f, 0xe8d6bc -> 0xe8bd20,
 * the point's space at 0xe8d5ea) and the Spline's (0xe8cf11 -> 0xe8be60 -> 0xe8bd20) all end in 0xe8a110's or
 * their own call of 0x3dde10.
 *
 * 'sim': the five calls of 0x3dde10 in the pathing code that batch jobs reach (the walk's two: 0xe8a221 up to
 * the next agent, 0xe8a416 up to the common ancestor; UpdatePathing's point space 0xe8d5ea; 0xe93fd0's agent
 * space 0xe94842; GetTargetPositionForWingMan's 0xe9a713) go through stubs. On a batch job inside a joined
 * pass (path_thread_class PASS, TLS view 0, admitted frame), when a's live chain up to b holds a class-0x49
 * object (the class the walk stops at, e.g. a ship or its wreck) without an Agent, the call becomes
 * U::Component::GetTransformRelativeTo 0x3db8a0(a, matrix, pass time, b, 0): the same composition up the live
 * parent chain (+0x70) from each link's own transform (slot 0x1440), at the pass's game time (manager+0x200,
 * stored by the pathing thread at 0xeac06f and handed by UpdateBatch to every Agent update at 0xeaed5b). The
 * serial pathing join keeps the MoveWorker parked while the jobs run, so those transforms stand still for the
 * pass. Native call instead (counted): outside a joined pass or on a thread with a view (outside=), a flag
 * argument or a == b (trivial=), b not a live ancestor of a within 32 links (not_ancestor=), a link that is a
 * class-0x49 object with a NavigationController (nav=: its slot 0x1440 refreshes the controller from the
 * banks, a write; the walk reads agents through the banks anyway), and spans of spaces only (spaces=: zones
 * and sectors are static, their cached and own transforms agree on every client, so stock's bits stay).
 * Diverted calls: diverted=. 'observe' computes both for every eligible call (spaces too), keeps the native
 * result and counts the calls where they differ (differs=).
 * Rows (PATH_TRANSFORMS_ROW): calls whose span holds a class-0x49 object without an Agent (objects=), and in
 * 'observe' also every differing call; at most 256 per tick (dropped=). Keys are portable (saved or created ID
 * of the object or its nearest keyed ancestor). Formation window (gate.path_transforms(mode, from, to), at
 * most 600 ticks): every call of the formation site (GetTargetPositionForWingMan) in it is a row too, whatever
 * happens to it (outcome=outside|trivial|nav|not_ancestor|spaces|failure|observe|diverted), with the result
 * matrix as sim= and the image return addresses above the hook as chain= (up to eight RVAs).
 * The steering step's dock sites (10 Nov, hjs4 after a far-sector join: fighter 0x6da76 parted from the session by 0.0003 m
 * in one tick, at another tick each run, with every input of its model equal; found by the hot-join session's write
 * watch): the Steering model's step 0xec7200 has a branch for a ship that flies to a dock (agent +0xf5c and +0xac8, a
 * time gate at model +0x378). It looks the dock object up (0x86fc70, class 0x4d) and asks 0x3dde10 twice for its
 * transform in the frame of a second component (0xec7cfe: the dock's position, compared with a range; 0xec7e1b: its
 * frame for the approach), flag 0, the matrix set to the identity first (0x97010). The same published-cache read as
 * above; hjs4's dock is a bay of a carrier, so the chain holds a flying ship (its cached pose is the last one this
 * client published for drawing) and animated parts. Sites dock_point and dock_frame: on a batch job in a joined pass,
 * with b a live ancestor of a, the call becomes X4's own hierarchy walk 0xe8a110(matrix, no velocity, &a, &b), the
 * conversion the pathing code uses for a path point: up to each object with an Agent through that Agent's pathing
 * read bank (0xeadc70, read-only, the pass does not write the banks), the spans between through the walk's two
 * calls of 0x3dde10, which are sites of this module. Inside a dock site's walk (a thread-local depth) those two
 * divert spans of spaces and parts too (their cached and own transforms need not agree here) and always compose, in
 * 'observe' as well, so 'observe' compares the dock sites' whole result with the cache's. dock_agents= counts the
 * dock calls whose chain holds an Agent's object. Every call of a dock site inside the row window is a row with its
 * outcome.
 * 'all' (10 Nov, an experiment for far-sector joins; 'sim' stays the default until a session has shown it): two more
 * cases leave the published caches. (1) A span of spaces and parts only is composed like the others: a client that never
 * drew a zone has no cached parents for its objects (lockstep_traffic_rows.h, the lane build), so "static, the same on
 * every client either way" does not hold there. (2) b is not above a: 0x3dde10 with flag 0 then lists b's cached parents,
 * composes a's chain up to the first of them and applies the inverse of b's chain from there. That is the formation site's
 * usual case (a wingman's slot from its leader's zone into its own; an ns8 session: formation:173409 calls,
 * not_ancestor=173838, all native) and both far-join cases still open are a formation leader that parts with its escorts a
 * tick behind. Here: the first live parent of b (+0x70) that is also above a is the common ancestor c; a's chain up to c is
 * composed into the matrix, b's into an identity, and the result is the first in the second's frame (position: the
 * difference turned by b's rows; rows: a's times b's transposed). An object with a NavigationController on either chain:
 * native (nav=). siblings= counts the composed calls. 'observe' computes this case too, keeps the native result and rows
 * the calls that differ, so one run shows whether the cache and the simulation disagree where a ship parts.
 * 'observe' also measures the hierarchy itself (parents=calls/differing in the status): for every call of a site on a pass
 * job it walks a's and b's chains and compares each link's published parent (link + 0xe0 + bank * 0x80, the bank the thread
 * reads: 0x38f4eb4) with the live one (+0x70). The pathing code walks published parents inline in 98 places (four in
 * GetTargetPositionForWingMan before this site's call); whether those can differ from the live parents on a client, e.g.
 * for a zone it never drew, was a guess: this counts it, per client.
 * Not covered: the velocity variant 0x3df3f0 (0xe8a214, 0xe8a40c; the target path passes no velocity) and the
 * walk's own choice of stops and common ancestor (0xf231e0, 0x3d8f90), which read the published view's cached
 * parents (the result in b's frame does not depend on it while cached and live parents agree).
 * Fail closed: pins of 0x3dde10, 0x3db8a0 and 0xe8a110 (.pdata extents), the argument setup of the three other
 * calls, the pass time's store and UpdateBatch's read; every site must hold its original rel32 (step chain's
 * FR20 stub at 0xe8d5ea excludes this and the reverse); an unreadable pass time or an exception in the
 * composition counts a failure (that call runs natively). Sites swapped by CAS, other modules' pins see the
 * original bytes (lockstep_gate.c fp_patch_view), owned and failure-free every gate tick (stop reason 180),
 * restored at stop. Windows only. */
#define PTF_HIER 0x3dde10u
#define PTF_HIER_SIZE 0x811u
#define PTF_HIER_SHA "3b757e4ac925248a081c1a675a3491b28a368289fb7434ced473c75dd073d0eb"
#define PTF_GTR 0x3db8a0u
#define PTF_GTR_SIZE 0x465u
#define PTF_GTR_SHA "6e28847e1ab6eb2e206ec564645b32dee94a0fdd2eefe0884bdedad46da1ad90"
#define PTF_WALK 0xe8a110u
#define PTF_WALK_SIZE 0x383u
#define PTF_WALK_SHA "39ddd07f5a13f364e7931d8795985f638fb050e9e29d4173c7e4a403bfbbfdfa"
#define PTF_MANAGER 0x6cf21c8u   /* the PathingManager */
#define PTF_PASS_TIME 0x200u     /* manager+0x200: the pass's game time (double) */
#define PTF_NAV_VTABLE 0x2c51158u
#define PTF_TLS_VIEW 0x314u
#define PTF_PARENT 0x70u
#define PTF_CONTROLLER 0x200u
#define PTF_CLASS_SLOT 0x11e8u   /* IsOfClass(int) */
#define PTF_CLASS_OBJECT 0x49    /* the class 0xf231e0 stops at: an object with a controller */
#define PTF_DEPTH 32u
#define PTF_SITES 7u
#define PTF_SITE_DOCK 5u         /* 5, 6: the steering step's dock sites; spans of spaces and parts are diverted too */
#define PTF_RING 4096u
#define PTF_ROWS_PER_TICK 256u
#define PTF_STOP_REASON 180u     /* 179 fp redirect */
enum {PTF_OFF,PTF_SIM,PTF_OBSERVE,PTF_ALL};
static const struct {uint32_t site;const char *name;} ptf_sites[PTF_SITES]={
    {0xe8a221u,"walk"},{0xe8a416u,"walk_end"},{0xe8d5eau,"point_space"},{0xe94842u,"agent_space"},{0xe9a713u,"formation"},
    {0xec7cfeu,"dock_point"},{0xec7e1bu,"dock_frame"}};
/* The calls' argument setup (xor r9d,r9d .. call), and the pass time's store and read; the two dock sites'
 * (xor r9d,r9d; mov r8,[rbp-0x60]; lea rdx,[rbp+0x90 | 0xd0]; mov rcx,[rbp-0x80]; call). */
static const struct {uint32_t rva,size;const char *sha;} ptf_pins[7]={
    {0xec7cecu,0x17u,"91dd7f0f0ee28de47dab983b64ffdbafb0085cfd4822d01b0ce5888de99d23c8"},
    {0xec7e09u,0x17u,"782801d67c931ef7d5976d0135282bd91004f06f7a8731d9ad607f913b5cf93f"},
    {0xe8d5dcu,0x18u,"75692118a89a6db8b99b2dde7b3e44499d4fb01baac1fec01485a98156f982c7"},
    {0xe94835u,0x12u,"2334ab86dcfba3ce89d82a2440c24251b4d80934b58ad03e252e19db98235df2"},
    {0xe9a707u,0x11u,"a40eb0b04de0975e01a194ad850b2575735d9d111075af12d982af36ceac4c88"},
    {0xeac058u,0x37u,"a5c25711b52de8952de8feb587bde3d87496287a827081cec64691712f072d79"},
    {0xeaed5bu,0x10u,"179d0a6f39d24ed6350e6308459f7381075a8d99461e62941297eaee8d5a3502"}};
typedef void *(*ptf_hier_fn)(void *,float *,void *,uint64_t);
typedef void (*ptf_gtr_fn)(void *,float *,double,void *,unsigned char);
typedef char (*ptf_class_fn)(void *,int);
typedef void (*ptf_walk_fn)(float *,void *,void **,void **);
static ptf_walk_fn ptf_walk_native; /* tests only: replaces 0xe8a110 */
static _Thread_local unsigned ptf_dock_depth; /* this thread is inside a dock site's walk */
static volatile LONG64 ptf_dock_agents,ptf_siblings,ptf_parent_calls,ptf_parent_differs;
#define PTF_PUBLISHED_BANK 0x38f4eb4u /* the bank index a thread without a view reads */
#define PTF_PUBLISHED_PARENT 0xe0u    /* + bank * 0x80: the published parent */
static const uint32_t *ptf_test_bank; /* tests only: the bank index */
/* 1 when a link of x's live chain has another published parent than its live one. */
static int ptf_published_differs(const unsigned char *x) {
    uint32_t bank=0;
    if(ptf_test_bank) bank=*ptf_test_bank;else if(!read_local(image+PTF_PUBLISHED_BANK,&bank,4)) return 0;
    if(bank>1u) return 0;
    for(unsigned depth=0;x && depth<PTF_DEPTH;depth++) {
        unsigned char *live=NULL,*published=NULL;
        if(!read_local(x+PTF_PARENT,&live,8) || !read_local(x+PTF_PUBLISHED_PARENT+0x80u*bank,&published,8)) return 0;
        if(live!=published) return 1;
        x=live;
    }
    return 0;
}
static ptf_hier_fn ptf_hier_native; /* tests only: replaces 0x3dde10 */
static ptf_gtr_fn ptf_gtr_native;   /* tests only: replaces 0x3db8a0 */
static unsigned char *ptf_test_tls; /* tests only: the calling thread's TLS block */
static int ptf_mode,ptf_installed,ptf_restored;
static unsigned ptf_done;
static volatile LONG ptf_active;
static unsigned char *ptf_page;
static const char *ptf_stage="idle";
static volatile LONG64 ptf_site_calls[PTF_SITES],ptf_outside,ptf_trivial,ptf_spaces,ptf_diverted,ptf_objects,ptf_nav,ptf_not_ancestor,
    ptf_differs,ptf_failures,ptf_rows,ptf_dropped,ptf_tick_rows;
static double ptf_last_time;
typedef struct {uint64_t key;unsigned klass,depth;} ptf_ref_t;
typedef struct {volatile LONG64 ready;uint64_t tick;unsigned site,mode,differs,object;ptf_ref_t a,b,obj;uint32_t ctrl_vt;
    double time;float sim[16],native[16];unsigned win;ptf_ref_t ship;unsigned dock;} ptf_row_t;
/* win: formation window row, outcome + 1. dock: a dock row (below), native[] holds its inputs, ship the stepping ship. */
enum {PTF_OUT_OUTSIDE,PTF_OUT_TRIVIAL,PTF_OUT_NAV,PTF_OUT_NOT_ANCESTOR,PTF_OUT_SPACES,PTF_OUT_FAILURE,PTF_OUT_OBSERVE,PTF_OUT_DIVERTED};
static const char *const ptf_outcomes[8]={"outside","trivial","nav","not_ancestor","spaces","failure","observe","diverted"};
#define PTF_SITE_FORMATION 4u
static uint64_t ptf_window_from,ptf_window_to; /* formation calls of these ticks are rows; 0/0 none */
static ptf_row_t *ptf_ring;
static int ptf_on(void) {return InterlockedCompareExchange(&ptf_active,0,0)!=0;}
/* The portable key of a component or of its nearest keyed ancestor (+0x70), depth walked; 0 none. */
static ptf_ref_t ptf_ref(const unsigned char *component) {
    ptf_ref_t r={0,WORLD_KEY_NONE,0};
    for(unsigned depth=0;component && depth<=8u;depth++) {
        uint64_t id=0;unsigned klass;unsigned char *parent=NULL;
        if(!read_local(component+8,&id,8)) break;
        uint64_t key=id?world_identity_key(id,&klass):0;
        if(key) {r.key=key;r.klass=klass;r.depth=depth;return r;}
        if(!read_local(component+PTF_PARENT,&parent,8)) break;
        component=parent;
    }
    r.depth=0xffu;return r;
}
/* a's live chain up to b. 1: b is an ancestor within PTF_DEPTH links and no link is an agent's object;
 * 0: not an ancestor; -1: a link with a NavigationController. *object: the first class-0x49 link without one
 * (its controller's vtable RVA in *ctrl_vt), else NULL. */
static int ptf_chain(unsigned char *a,const unsigned char *b,unsigned char **object,uint32_t *ctrl_vt) {
    *object=NULL;*ctrl_vt=0;
    unsigned char *link=a;
    for(unsigned depth=0;depth<PTF_DEPTH && link;depth++) {
        if(link==b) return 1;
        void **vtable=NULL;ptf_class_fn is_of=NULL;char is_object=0;
        if(!read_local(link,&vtable,8) || !vtable || !read_local((unsigned char*)vtable+PTF_CLASS_SLOT,&is_of,8) || !is_of) return 0;
        __try {is_object=is_of(link,PTF_CLASS_OBJECT);} __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
        if(is_object) {
            unsigned char *controller=NULL,*back=NULL;uintptr_t vt=0;
            if(read_local(link+PTF_CONTROLLER,&controller,8) && controller && read_local(controller,&vt,8)) {
                if(pslot_canonical_vtable(vt)==(uintptr_t)(image+PTF_NAV_VTABLE) && read_local(controller+0x28,&back,8) && back==link) return -1;
                if(!*object) {
                    *object=link;
                    const unsigned char *v=(const unsigned char*)pslot_canonical_vtable(vt);
                    *ctrl_vt=v>=image && v<image+0x6fa9000?(uint32_t)(v-image):0xffffffffu;
                }
            } else if(!*object) *object=link;
        }
        unsigned char *parent=NULL;
        if(!read_local(link+PTF_PARENT,&parent,8)) return 0;
        link=parent;
    }
    return 0;
}
static int ptf_dock_rows; /* native_gate_path_transforms_dock_rows: the dock rows (below), and every row names its ship */
/* The ship whose update this thread runs: the Agent update's (lockstep_agent.h), else the Steering update's model's
 * agent (lockstep_flight.h); NULL outside both. */
static unsigned char *ptf_stepping_ship(void) {
    unsigned char *agent=agent_current,*ship=NULL;
    if(!agent && flight_current_model && !read_local((const unsigned char*)flight_current_model+0x10,&agent,8)) agent=NULL;
    if(!agent || !read_local(agent+0xef0,&ship,8)) return NULL;
    return ship;
}
static void ptf_row(unsigned site,const unsigned char *a,const unsigned char *b,const unsigned char *object,uint32_t ctrl_vt,
                    double time,const float *sim,const float *native,int differs,unsigned win) {
    if(!ptf_ring) return;
    if(InterlockedIncrement64(&ptf_tick_rows)>(LONG64)PTF_ROWS_PER_TICK) {InterlockedIncrement64(&ptf_dropped);return;}
    LONG64 index=InterlockedIncrement64(&ptf_rows)-1;
    ptf_row_t *r=&ptf_ring[(uint64_t)index%PTF_RING];
    InterlockedExchange64(&r->ready,0);
    r->tick=stats.issued;r->site=site;r->mode=(unsigned)ptf_mode;r->differs=(unsigned)differs;r->object=object!=NULL;
    r->a=ptf_ref(a);r->b=ptf_ref(b);r->obj=ptf_ref(object);r->ctrl_vt=ctrl_vt;r->time=time;r->win=win;r->dock=0;memset(&r->ship,0,sizeof(r->ship));
    if(ptf_dock_rows) {r->dock=2;r->ship=ptf_ref(ptf_stepping_ship());} /* the dock rows' option: every row names the stepping ship */
    memcpy(r->sim,sim,sizeof(r->sim));
    if(native) memcpy(r->native,native,sizeof(r->native));else memset(r->native,0,sizeof(r->native));
    InterlockedExchange64(&r->ready,index+1);
}
/* b is a live ancestor of a within PTF_DEPTH links. */
static int ptf_is_ancestor(const unsigned char *a,const unsigned char *b) {
    const unsigned char *link=a;
    for(unsigned depth=0;depth<PTF_DEPTH && link;depth++) {
        if(link==b) return 1;
        unsigned char *parent=NULL;
        if(!read_local(link+PTF_PARENT,&parent,8)) return 0;
        link=parent;
    }
    return 0;
}
/* A dock site's call, the chain known to reach b (agent: it holds an Agent's object): X4's hierarchy walk. */
static void *ptf_dock_core(unsigned site,void *a,float *out,void *b,uint64_t flag,ptf_hier_fn native,const unsigned char *object,uint32_t ctrl_vt,
                           int agent,const unsigned char *manager,unsigned *outcome) {
    double time=0;
    if(!manager || !read_local(manager+PTF_PASS_TIME,&time,8) || !isfinite(time)) {
        *outcome=PTF_OUT_FAILURE;ptf_stage="pass_time";InterlockedIncrement64(&ptf_failures);return native(a,out,b,flag);
    }
    __declspec(align(16)) float sim[16];memcpy(sim,out,sizeof(sim));
    void *from=a,*to=b;int ok=1;const ptf_walk_fn walk=ptf_walk_native?ptf_walk_native:(ptf_walk_fn)(void*)(image+PTF_WALK);
    ++ptf_dock_depth;
    __try {walk(sim,NULL,&from,&to);} __except(EXCEPTION_EXECUTE_HANDLER) {ok=0;}
    --ptf_dock_depth;
    if(!ok) {*outcome=PTF_OUT_FAILURE;ptf_stage="dock_walk";InterlockedIncrement64(&ptf_failures);return native(a,out,b,flag);}
    ptf_last_time=time;
    if(agent) InterlockedIncrement64(&ptf_dock_agents);
    if(object) InterlockedIncrement64(&ptf_objects);
    if(ptf_mode==PTF_OBSERVE) {
        *outcome=PTF_OUT_OBSERVE;void *result=native(a,out,b,flag);
        const int differs=memcmp(sim,out,sizeof(sim))!=0;
        if(differs) InterlockedIncrement64(&ptf_differs);
        if(object || agent || differs) ptf_row(site,a,b,object,ctrl_vt,time,sim,out,differs,0);
        return result;
    }
    *outcome=PTF_OUT_DIVERTED;InterlockedIncrement64(&ptf_diverted);
    memcpy(out,sim,sizeof(sim));
    if(object) ptf_row(site,a,b,object,ctrl_vt,time,sim,NULL,0,0);
    return out;
}
/* b is not above a ('all', 'observe'): through the common live ancestor, see the head. */
static void *ptf_sibling_core(unsigned site,unsigned char *a,float *out,unsigned char *b,uint64_t flag,ptf_hier_fn native,ptf_gtr_fn gtr,
                              const unsigned char *manager,unsigned *outcome) {
    unsigned char *above[PTF_DEPTH],*c=NULL,*link=a;unsigned n=0;
    for(;link && n<PTF_DEPTH;n++) {above[n]=link;unsigned char *parent=NULL;if(!read_local(link+PTF_PARENT,&parent,8)) break;link=parent;}
    link=b;
    for(unsigned depth=0;link && depth<PTF_DEPTH && !c;depth++) {
        for(unsigned i=0;i<n;i++) if(above[i]==link) {c=link;break;}
        unsigned char *parent=NULL;if(c || !read_local(link+PTF_PARENT,&parent,8)) break;
        link=parent;
    }
    unsigned char *oa=NULL,*ob=NULL;uint32_t va=0,vb=0;
    const int ca=!c?0:a==c?1:ptf_chain(a,c,&oa,&va),cb=!c?0:b==c?1:ptf_chain(b,c,&ob,&vb);
    if(ca<0 || cb<0) {*outcome=PTF_OUT_NAV;InterlockedIncrement64(&ptf_nav);return native(a,out,b,flag);}
    if(!ca || !cb) {*outcome=PTF_OUT_NOT_ANCESTOR;InterlockedIncrement64(&ptf_not_ancestor);return native(a,out,b,flag);}
    double time=0;
    if(!manager || !read_local(manager+PTF_PASS_TIME,&time,8) || !isfinite(time)) {
        *outcome=PTF_OUT_FAILURE;ptf_stage="pass_time";InterlockedIncrement64(&ptf_failures);return native(a,out,b,flag);
    }
    static const float identity[16]={0,0,0,0, 1,0,0,0, 0,1,0,0, 0,0,1,0};
    __declspec(align(16)) float ta[16],tb[16],sim[16];memcpy(ta,out,sizeof(ta));memcpy(tb,identity,sizeof(tb));
    int ok=1;
    __try {if(a!=c) gtr(a,ta,time,c,0);if(b!=c) gtr(b,tb,time,c,0);} __except(EXCEPTION_EXECUTE_HANDLER) {ok=0;}
    if(!ok) {*outcome=PTF_OUT_FAILURE;ptf_stage="compose";InterlockedIncrement64(&ptf_failures);return native(a,out,b,flag);}
    const float d[3]={ta[0]-tb[0],ta[1]-tb[1],ta[2]-tb[2]};
    for(unsigned j=0;j<3;j++) sim[j]=d[0]*tb[4+4*j]+d[1]*tb[5+4*j]+d[2]*tb[6+4*j];
    sim[3]=ta[3];
    for(unsigned i=0;i<3;i++) {
        for(unsigned j=0;j<3;j++) sim[4+4*i+j]=ta[4+4*i]*tb[4+4*j]+ta[5+4*i]*tb[5+4*j]+ta[6+4*i]*tb[6+4*j];
        sim[7+4*i]=ta[7+4*i];
    }
    ptf_last_time=time;
    const unsigned char *object=oa?oa:ob;const uint32_t ctrl_vt=oa?va:vb;
    if(ptf_mode==PTF_OBSERVE) {
        *outcome=PTF_OUT_OBSERVE;void *result=native(a,out,b,flag);
        const int differs=memcmp(sim,out,sizeof(sim))!=0;
        if(differs) {InterlockedIncrement64(&ptf_differs);ptf_row(site,a,b,object,ctrl_vt,time,sim,out,1,0);}
        return result;
    }
    *outcome=PTF_OUT_DIVERTED;InterlockedIncrement64(&ptf_diverted);InterlockedIncrement64(&ptf_siblings);
    memcpy(out,sim,sizeof(sim));
    return out;
}
/* One call of a site: cls the thread's path_thread_class, tls its TLS block, manager the PathingManager. */
static void *ptf_call_core(unsigned site,void *a,float *out,void *b,uint64_t flag,unsigned cls,const unsigned char *tls,
                           ptf_hier_fn native,ptf_gtr_fn gtr,const unsigned char *manager,unsigned *outcome) {
    uint32_t view=1;
    if(!ptf_on() || !stats.active || boot_phase!=3 || cls!=PATH_THREAD_PASS || !tls || !read_local(tls+PTF_TLS_VIEW,&view,4) || view) {
        *outcome=PTF_OUT_OUTSIDE;InterlockedIncrement64(&ptf_outside);return native(a,out,b,flag);
    }
    if((flag&0xff) || !a || !b || a==b || !out) {*outcome=PTF_OUT_TRIVIAL;InterlockedIncrement64(&ptf_trivial);return native(a,out,b,flag);}
    unsigned char *object;uint32_t ctrl_vt;
    const int chain=ptf_chain(a,b,&object,&ctrl_vt);
    const unsigned nested=ptf_dock_depth; /* a call of the walk's sites from inside a dock site's walk */
    if(ptf_mode==PTF_OBSERVE && !nested) {
        InterlockedIncrement64(&ptf_parent_calls);
        if(ptf_published_differs(a) || ptf_published_differs(b)) InterlockedIncrement64(&ptf_parent_differs);
    }
    if(site>=PTF_SITE_DOCK && chain && (chain>0 || ptf_is_ancestor(a,b)))
        return ptf_dock_core(site,a,out,b,flag,native,object,ctrl_vt,chain<0,manager,outcome);
    if(site>=PTF_SITE_DOCK) {*outcome=PTF_OUT_NOT_ANCESTOR;InterlockedIncrement64(&ptf_not_ancestor);return native(a,out,b,flag);}
    if(chain<0) {*outcome=PTF_OUT_NAV;InterlockedIncrement64(&ptf_nav);return native(a,out,b,flag);}
    if(!chain) {
        if(ptf_mode==PTF_SIM || nested) {*outcome=PTF_OUT_NOT_ANCESTOR;InterlockedIncrement64(&ptf_not_ancestor);return native(a,out,b,flag);}
        return ptf_sibling_core(site,a,out,b,flag,native,gtr,manager,outcome);
    }
    /* Spaces only (zones, sectors, parts): 'sim' keeps stock's bits there ('all' composes them too, see the head). */
    if(!object && ptf_mode==PTF_SIM && !nested) {*outcome=PTF_OUT_SPACES;InterlockedIncrement64(&ptf_spaces);return native(a,out,b,flag);}
    double time=0;
    if(!manager || !read_local(manager+PTF_PASS_TIME,&time,8) || !isfinite(time)) {
        *outcome=PTF_OUT_FAILURE;ptf_stage="pass_time";InterlockedIncrement64(&ptf_failures);return native(a,out,b,flag);
    }
    __declspec(align(16)) float sim[16];memcpy(sim,out,sizeof(sim));
    int ok=1;
    __try {gtr(a,sim,time,b,0);} __except(EXCEPTION_EXECUTE_HANDLER) {ok=0;}
    if(!ok) {*outcome=PTF_OUT_FAILURE;ptf_stage="compose";InterlockedIncrement64(&ptf_failures);return native(a,out,b,flag);}
    ptf_last_time=time;
    if(object) InterlockedIncrement64(&ptf_objects);
    if(ptf_mode==PTF_OBSERVE && !nested) {
        *outcome=PTF_OUT_OBSERVE;void *result=native(a,out,b,flag);
        const int differs=memcmp(sim,out,sizeof(sim))!=0;
        if(differs) InterlockedIncrement64(&ptf_differs);
        if(object || differs) ptf_row(site,a,b,object,ctrl_vt,time,sim,out,differs,0);
        return result;
    }
    *outcome=PTF_OUT_DIVERTED;InterlockedIncrement64(&ptf_diverted);
    memcpy(out,sim,sizeof(sim));
    if(object) ptf_row(site,a,b,object,ctrl_vt,time,sim,NULL,0,0);
    return out;
}
/* Dock rows (native_gate_path_transforms_dock_rows = true, diagnostic, every tick; 19 Nov). The refit and trade splits
 * (police mass traffic ships near a wharf or station) all part first in these sites' counts: one client makes the
 * dock_point call two or three steps more before the dock_frame call that starts a dock path (and the keyed timer
 * draw after it, ACTOR_RNG_ALL agent_drawn). In the step 0xec7200 that is the range test between the two calls:
 * the ship's position (the step's slot, +0) against the approach point (the agent's vector at +0xaa0 + bank*0x10 put
 * through dock_point's result), within the length of the model's +0x2c0; then the dock's navigation octree, whose
 * lookups NAV_OCTREE_LOOKUP rows already compare. The counts do not say whose step it was or which input parted.
 * A dock row per call of either dock site made inside a Steering update (lockstep_flight.h keeps the update's model and
 * slot per thread): the ship (portable key of agent+0xef0), the call's outcome and thread class, the result matrix
 * (sim=), and raw: slot= the ship's position, local= the approach vector, range= the model's +0x2c0..+0x2cb, timer= the
 * model's next-check time +0x378, flags= agent+0xf5c, agent+0xac8, model +0x360 == +0x368, the bank, the range test
 * redone in the step's order (1 within), model+0x388 set. Two clients' rows compare as sequences per ship; the first
 * differing row names the ship and the input. With the option every other row (object rows, window rows) names the
 * stepping ship too (ship=: the Agent update's, lockstep_agent.h, else the Steering update's), and the probe writes
 * every row on every tick, not only on status ticks: the trade split 20261119-154538 parted first in walk_end calls
 * against a station between two status ticks, whose rows were never written. Read-only. */
#define PTF_BANK_INDEX 0x38f4ee4u /* g_PathingStateBufferIndex: the read bank */
static const uint32_t *ptf_test_read_bank; /* tests only: the read bank */
static void *ptf_gates_alloc(void);
static int ptf_dock_rows_arm(void) {
    if(ptf_installed) return ptf_dock_rows;
    if(!ptf_gates_alloc()) return 0;
    ptf_dock_rows=1;return 1;
}
static void ptf_dock_row(unsigned site,const unsigned char *a,const unsigned char *b,const float *out,unsigned cls,unsigned outcome) {
    const unsigned char *model=flight_current_model,*slot=flight_current_slot;
    unsigned char *agent=NULL,*ship=NULL;
    if(!ptf_ring || !model || !slot || !read_local(model+0x10,&agent,8) || !agent || !read_local(agent+0xef0,&ship,8)) return;
    if(InterlockedIncrement64(&ptf_tick_rows)>(LONG64)PTF_ROWS_PER_TICK) {InterlockedIncrement64(&ptf_dropped);return;}
    unsigned char in[64];memset(in,0,sizeof(in));
    uint32_t bank=0;
    if(ptf_test_read_bank) bank=*ptf_test_read_bank;else if(!read_local(image+PTF_BANK_INDEX,&bank,4)) bank=0xffu;
    float p[4]={0},v[4]={0},r[3]={0};uint64_t pending=0,list[2]={0,0};
    (void)read_local(slot,p,16);
    if(bank<=1u) (void)read_local(agent+0xaa0+0x10u*bank,v,16);
    (void)read_local(model+0x2c0,r,12);
    memcpy(in,p,16);memcpy(in+16,v,16);memcpy(in+32,r,12);
    (void)read_local(agent+0xf5c,in+44,1);(void)read_local(agent+0xac8,in+45,1);
    in[46]=read_local(model+0x360,list,16) && list[0]==list[1];in[47]=(unsigned char)bank;
    (void)read_local(model+0x378,in+48,8);
    /* The step's test: the approach point m*v (m: dock_point's result, translation at 0..2, axes 4.., 8.., 12..). */
    if(site==PTF_SITE_DOCK) {
        float q[3];
        for(unsigned i=0;i<3;i++) q[i]=v[2]*out[12+i]+v[0]*out[4+i]+v[1]*out[8+i]+out[i];
        const float d=(q[0]-p[0])*(q[0]-p[0])+(q[1]-p[1])*(q[1]-p[1])+(q[2]-p[2])*(q[2]-p[2]);
        in[56]=d<r[0]*r[0]+r[1]*r[1]+r[2]*r[2];
    }
    in[57]=read_local(model+0x388,&pending,8) && pending;
    LONG64 index=InterlockedIncrement64(&ptf_rows)-1;
    ptf_row_t *row=&ptf_ring[(uint64_t)index%PTF_RING];
    InterlockedExchange64(&row->ready,0);
    row->tick=stats.issued;row->site=site;row->mode=(unsigned)ptf_mode;row->differs=0;row->object=0;
    row->a=ptf_ref(a);row->b=ptf_ref(b);memset(&row->obj,0,sizeof(row->obj));row->ctrl_vt=cls;row->time=ptf_last_time;
    row->win=outcome+1u;row->dock=1;row->ship=ptf_ref(ship);
    memcpy(row->sim,out,sizeof(row->sim));memcpy(row->native,in,sizeof(row->native));
    InterlockedExchange64(&row->ready,index+1);
}
/* Dock gate rows (with the dock rows' option; 19 Nov, kx1 on the laptop, refit, XS ships traced): at 10320 client 2 made
 * a ship's dock_point call (and its dock_frame: the check passed at once) against wharf 1b5af and client 1 none, while
 * both clients' pathing walks (PATH_DETERMINISM walk=) agreed; 35 ticks later a police XS ship near it flew apart. Client
 * 1's step did not enter the dock branch that tick, so no dock row says why. The branch's entry in 0xec7200 (not a cheap
 * update): no dock path pending (model+0x388), agent+0xf5c, the dock frame, agent+0xac8, the dock's ID at agent+0xa90 +
 * read bank * 8, model+0x68, and model+0x360 == +0x368 or the step's time past model+0x378, the next check. Read at the
 * start of every Steering update (lockstep_flight.h): one row per ship whenever that tuple changes (dock=3), from its first
 * update with agent+0xf5c set (new=1) to the first without (f5c=0, the ship is forgotten), with the full and cheap updates
 * the ship had since then (the cheap flag is not in the tuple: past the pathing budget it changes every few ticks). Keyed
 * by the ship and its runtime ID, so a model rebuilt or an address reused does not show; ships with no update for 600
 * ticks are forgotten at the next multiple of 600 (a docked ship keeps the flag), the same on every client. Read-only. */
#define PTF_GATES 16384u
#define PTF_GATE_FORGET 600u
typedef struct {const unsigned char *ship;uint64_t id,seen,full,cheap;unsigned char tuple[32];} ptf_gate_t;
static ptf_gate_t *ptf_gates;
static unsigned ptf_gate_count;
static uint64_t ptf_gate_full;   /* ships not kept: the table was full */
static SRWLOCK ptf_gate_lock=SRWLOCK_INIT;
static void *ptf_gates_alloc(void) {
    if(!ptf_gates) ptf_gates=VirtualAlloc(NULL,sizeof(ptf_gate_t)*PTF_GATES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    return ptf_gates;
}
static unsigned ptf_gate_slot(const void *ship) {return (unsigned)(((uint64_t)(uintptr_t)ship*0x9e3779b97f4a7c15ull)>>50)&(PTF_GATES-1u);}
static ptf_gate_t *ptf_gate_find(const unsigned char *ship) {
    for(unsigned i=ptf_gate_slot(ship),n=0;n<PTF_GATES;i=(i+1)&(PTF_GATES-1u),n++) {
        if(!ptf_gates[i].ship) return NULL;
        if(ptf_gates[i].ship==ship) return &ptf_gates[i];
    }
    return NULL;
}
static ptf_gate_t *ptf_gate_insert(const unsigned char *ship) {
    if(ptf_gate_count>=PTF_GATES*3u/4u) {++ptf_gate_full;return NULL;}
    unsigned i=ptf_gate_slot(ship);
    while(ptf_gates[i].ship) i=(i+1)&(PTF_GATES-1u);
    memset(&ptf_gates[i],0,sizeof(ptf_gates[i]));ptf_gates[i].ship=ship;++ptf_gate_count;
    return &ptf_gates[i];
}
/* Linear probing: the backward shift keeps every chain whole. */
static void ptf_gate_erase(ptf_gate_t *e) {
    unsigned i=(unsigned)(e-ptf_gates),j=i;
    memset(&ptf_gates[i],0,sizeof(ptf_gates[i]));--ptf_gate_count;
    for(;;) {
        j=(j+1)&(PTF_GATES-1u);
        if(!ptf_gates[j].ship) return;
        const unsigned home=ptf_gate_slot(ptf_gates[j].ship);
        if((j>i && (home<=i || home>j)) || (j<i && home<=i && home>j)) {ptf_gates[i]=ptf_gates[j];memset(&ptf_gates[j],0,sizeof(ptf_gates[j]));i=j;}
    }
}
/* Gate tick (main thread, no pass running): forget the ships not updated for PTF_GATE_FORGET ticks. */
static void ptf_gate_sweep(uint64_t now) {
    if(!ptf_gates) return;
    AcquireSRWLockExclusive(&ptf_gate_lock);
    for(int again=1;again;) {
        again=0;
        for(unsigned i=0;i<PTF_GATES;i++) if(ptf_gates[i].ship && ptf_gates[i].seen+PTF_GATE_FORGET<=now) {ptf_gate_erase(&ptf_gates[i]);again=1;}
    }
    ReleaseSRWLockExclusive(&ptf_gate_lock);
}
static void ptf_gate_note(const unsigned char *model,double time,unsigned char cheap) {
    unsigned char *agent=NULL,*ship=NULL;uint64_t id=0;unsigned char f5c=0;
    if(!ptf_dock_rows || !ptf_gates || !ptf_ring || !model || !read_local(model+0x10,&agent,8) || !agent ||
       !read_local(agent+0xef0,&ship,8) || !ship || !read_local(ship+8,&id,8)) return;
    (void)read_local(agent+0xf5c,&f5c,1);
    unsigned char t[32],in[64];memset(t,0,sizeof(t));memset(in,0,sizeof(in));
    uint32_t bank=0;uint64_t pending=0,list[2]={0,0},dock=0;int32_t m68=0;double timer=0.0;
    if(ptf_test_read_bank) bank=*ptf_test_read_bank;else if(!read_local(image+PTF_BANK_INDEX,&bank,4)) bank=0xffu;
    t[0]=read_local(model+0x388,&pending,8) && pending;t[1]=f5c;(void)read_local(agent+0xac8,t+2,1);
    (void)read_local(model+0x68,&m68,4);t[3]=m68!=0;
    t[4]=read_local(model+0x360,list,16) && list[0]==list[1];
    (void)read_local(model+0x378,&timer,8);t[5]=!(time<=timer);   /* the step skips the check while time <= timer (NaN: checks) */
    if(bank<=1u) (void)read_local(agent+0xa90+8u*bank,&dock,8);
    unsigned klass=WORLD_KEY_NONE;const uint64_t key=dock?world_identity_key(dock,&klass):0;
    memcpy(t+8,&timer,8);memcpy(t+16,&key,8);memcpy(t+24,&klass,4);t[28]=dock!=0;
    AcquireSRWLockExclusive(&ptf_gate_lock);
    ptf_gate_t *e=ptf_gate_find(ship);
    if(e && e->id!=id) {ptf_gate_erase(e);e=NULL;}   /* a new ship at a dead one's address */
    const int fresh=!e;
    if(!e && (!f5c || !(e=ptf_gate_insert(ship)))) {ReleaseSRWLockExclusive(&ptf_gate_lock);return;}
    e->id=id;e->seen=stats.issued;if(cheap) ++e->cheap;else ++e->full;
    if(!fresh && !memcmp(e->tuple,t,sizeof(t))) {ReleaseSRWLockExclusive(&ptf_gate_lock);return;}
    memcpy(e->tuple,t,sizeof(t));
    memcpy(in,t,sizeof(t));memcpy(in+32,&m68,4);in[36]=(unsigned char)fresh;in[37]=cheap!=0;in[38]=(unsigned char)bank;
    memcpy(in+40,&e->full,8);memcpy(in+48,&e->cheap,8);
    if(!f5c) ptf_gate_erase(e);
    ReleaseSRWLockExclusive(&ptf_gate_lock);
    if(InterlockedIncrement64(&ptf_tick_rows)>(LONG64)PTF_ROWS_PER_TICK) {InterlockedIncrement64(&ptf_dropped);return;}
    LONG64 index=InterlockedIncrement64(&ptf_rows)-1;
    ptf_row_t *row=&ptf_ring[(uint64_t)index%PTF_RING];
    InterlockedExchange64(&row->ready,0);
    row->tick=stats.issued;row->site=PTF_SITE_DOCK;row->mode=(unsigned)ptf_mode;row->differs=0;row->object=0;
    memset(&row->a,0,sizeof(row->a));memset(&row->b,0,sizeof(row->b));memset(&row->obj,0,sizeof(row->obj));row->ctrl_vt=0;row->time=time;
    row->win=0;row->dock=3;row->ship=ptf_ref(ship);
    memset(row->sim,0,sizeof(row->sim));memcpy(row->native,in,sizeof(row->native));
    InterlockedExchange64(&row->ready,index+1);
}
static void *ptf_call_using(unsigned site,void *a,float *out,void *b,uint64_t flag,unsigned cls,const unsigned char *tls,
                            ptf_hier_fn native,ptf_gtr_fn gtr,const unsigned char *manager) {
    InterlockedIncrement64(&ptf_site_calls[site]);
    unsigned outcome=PTF_OUT_OUTSIDE;
    void *result=ptf_call_core(site,a,out,b,flag,cls,tls,native,gtr,manager,&outcome);
    if(site>=PTF_SITE_DOCK && ptf_dock_rows && out) ptf_dock_row(site,a,b,out,cls,outcome);
    if((site==PTF_SITE_FORMATION || site>=PTF_SITE_DOCK) && ptf_window_to && stats.issued>=ptf_window_from && stats.issued<=ptf_window_to && out) {
        __declspec(align(16)) float m[16];memcpy(m,out,sizeof(m));
        uint32_t chain[16];memset(chain,0xff,sizeof(chain));void *frames[24];USHORT n=RtlCaptureStackBackTrace(1,24,frames,NULL);unsigned k=0;
        for(USHORT j=0;j<n && k<8;j++) {const unsigned char *f=frames[j];if(f>=image && f<image+0x6fa9000) chain[k++]=(uint32_t)(f-image);}
        ptf_row(site,a,b,NULL,cls,ptf_last_time,m,(const float*)chain,0,outcome+1u);
    }
    return result;
}
/* The formation row window; an identical rerun is accepted. Before arming. */
static int ptf_window(uint64_t from,uint64_t to) {
    if(ptf_installed) return from==ptf_window_from && to==ptf_window_to;
    if(to && (from<1 || to<from || to-from>600)) return 0;
    ptf_window_from=from;ptf_window_to=to;return 1;
}
static void *ptf_call(unsigned site,void *a,float *out,void *b,uint64_t flag) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    const unsigned char *tls=ptf_test_tls?ptf_test_tls:slots?(const unsigned char*)slots[0]:NULL;
    return ptf_call_using(site,a,out,b,flag,path_thread_class(),tls,ptf_hier_native?ptf_hier_native:(ptf_hier_fn)(void*)(image+PTF_HIER),
        ptf_gtr_native?ptf_gtr_native:(ptf_gtr_fn)(void*)(image+PTF_GTR),*(const unsigned char*const*)(image+PTF_MANAGER));
}
static void *ptf_hook0(void *a,float *out,void *b,uint64_t flag) {return ptf_call(0,a,out,b,flag);}
static void *ptf_hook1(void *a,float *out,void *b,uint64_t flag) {return ptf_call(1,a,out,b,flag);}
static void *ptf_hook2(void *a,float *out,void *b,uint64_t flag) {return ptf_call(2,a,out,b,flag);}
static void *ptf_hook3(void *a,float *out,void *b,uint64_t flag) {return ptf_call(3,a,out,b,flag);}
static void *ptf_hook4(void *a,float *out,void *b,uint64_t flag) {return ptf_call(4,a,out,b,flag);}
static void *ptf_hook5(void *a,float *out,void *b,uint64_t flag) {return ptf_call(5,a,out,b,flag);}
static void *ptf_hook6(void *a,float *out,void *b,uint64_t flag) {return ptf_call(6,a,out,b,flag);}
static void *ptf_hook(unsigned i) {
    switch(i) {case 0: return (void*)ptf_hook0;case 1: return (void*)ptf_hook1;case 2: return (void*)ptf_hook2;case 3: return (void*)ptf_hook3;
    case 4: return (void*)ptf_hook4;case 5: return (void*)ptf_hook5;default: return (void*)ptf_hook6;}
}
_Static_assert(PTF_SITES==7u,"one hook per site");
/* Stub i at page+0x10*i: jmp [rip] -> its hook (the caller's return address stays the X4 site's). */
static void ptf_write_code(unsigned char *p) {
    memset(p,0xcc,0x10u*PTF_SITES);
    for(unsigned i=0;i<PTF_SITES;i++) {
        static const unsigned char jump[6]={0xff,0x25,0,0,0,0};void *hook=ptf_hook(i);
        memcpy(p+0x10u*i,jump,6);memcpy(p+0x10u*i+6,&hook,8);
    }
}
static int32_t ptf_rel(unsigned i,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+ptf_sites[i].site+5));}
/* 0 original, 1 ours, -1 anything else. Read raw: read_local would show the view. */
static int ptf_site_state(unsigned i) {
    unsigned char now[5];int32_t d;
    memcpy(now,(const void*)(image+ptf_sites[i].site),5);
    if(now[0]!=0xe8) return -1;
    memcpy(&d,now+1,4);
    if(d==ptf_rel(i,image+PTF_HIER)) return 0;
    if(ptf_page && d==ptf_rel(i,ptf_page+0x10u*i)) return 1;
    return -1;
}
static unsigned char *ptf_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+PTF_WALK)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int reach=1;
            for(unsigned i=0;i<PTF_SITES;i++) {
                intptr_t d=(intptr_t)p-(intptr_t)(image+ptf_sites[i].site+5);
                if(d<=INT32_MIN+0x10000 || d>=INT32_MAX-0x10000) reach=0;
            }
            if(reach) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int ptf_site_cas(unsigned i,const void *expected,const void *value) {
    const int32_t e=ptf_rel(i,expected),v=ptf_rel(i,value);unsigned char eb[4],vb[4];memcpy(eb,&e,4);memcpy(vb,&v,4);
    return view_cas_bytes(ptf_sites[i].site+1,eb,vb,4);
}
/* ---- original-byte view (lockstep_gate.c fp_patch_view) ---- */
static int ptf_overlaps(const void *source,size_t bytes) {
    if(!*(volatile unsigned*)&ptf_done || !image || !source) return 0;
    const uintptr_t a=(uintptr_t)source,b=a+bytes;
    for(unsigned i=0;i<PTF_SITES;i++) {
        const uintptr_t s=(uintptr_t)(image+ptf_sites[i].site+1);
        if((ptf_done>>i)&1u && s<b && s+4>a) return 1;
    }
    return 0;
}
static void ptf_unpatch_copy(const void *source,void *copy,size_t bytes) {
    if(!ptf_overlaps(source,bytes)) return;
    const uintptr_t a=(uintptr_t)source;
    for(unsigned i=0;i<PTF_SITES;i++) {
        if(!((ptf_done>>i)&1u)) continue;
        const int32_t d=ptf_rel(i,image+PTF_HIER);unsigned char rel[4];memcpy(rel,&d,4);
        const uintptr_t s=(uintptr_t)(image+ptf_sites[i].site+1);
        for(unsigned k=0;k<4;k++) if(s+k>=a && s+k<a+bytes) ((unsigned char*)copy)[s+k-a]=rel[k];
    }
}
static int ptf_pinned(void) {
    if(!hash_ok(image+PTF_HIER,PTF_HIER_SIZE,PTF_HIER_SHA) || !hash_ok(image+PTF_GTR,PTF_GTR_SIZE,PTF_GTR_SHA) ||
       !hash_ok(image+PTF_WALK,PTF_WALK_SIZE,PTF_WALK_SHA)) return 0;
    for(unsigned i=0;i<sizeof(ptf_pins)/sizeof(*ptf_pins);i++) if(!hash_ok(image+ptf_pins[i].rva,ptf_pins[i].size,ptf_pins[i].sha)) return 0;
    return 1;
}
static void ptf_restore(void) {
    if(!ptf_installed) return;
    InterlockedExchange(&ptf_active,0);
    for(unsigned i=0;i<PTF_SITES;i++)
        if((ptf_done>>i)&1u && ptf_site_state(i)==1 && ptf_site_cas(i,ptf_page+0x10u*i,image+PTF_HIER)) ptf_done&=~(1u<<i);
    if(!ptf_done) {ptf_installed=0;ptf_restored=1;ptf_stage="restored";}
    else ptf_stage="restore_foreign";
}
/* gate.path_transforms(mode): the UI thread, before the first admitted frame, with the serial pathing join. */
static int ptf_arm(int mode) {
    if(ptf_installed) return stats.active && mode==ptf_mode; /* an identical rerun after a loading-UI reload */
    if((mode!=PTF_SIM && mode!=PTF_OBSERVE && mode!=PTF_ALL) || !stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !path_installed || !(path_options&1)) {ptf_stage="state";return 0;}
    for(unsigned i=0;i<PTF_SITES;i++) if(ptf_site_state(i)!=0) {ptf_stage="sites";return 0;}
    if(!ptf_pinned()) {ptf_stage="pins";return 0;}
    if(!ptf_ring) ptf_ring=VirtualAlloc(NULL,sizeof(ptf_row_t)*PTF_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ptf_ring) {ptf_stage="ring";return 0;}
    if(!ptf_page) ptf_page=ptf_alloc_page();
    if(!ptf_page) {ptf_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(ptf_page,0x1000,PAGE_READWRITE,&old)) {ptf_stage="page";return 0;}
    ptf_write_code(ptf_page);
    if(!VirtualProtect(ptf_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {ptf_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),ptf_page,0x1000);
    ptf_mode=mode;ptf_installed=1;ptf_restored=0;ptf_done=0;InterlockedExchange(&ptf_active,1);
    for(unsigned i=0;i<PTF_SITES;i++) {
        if(!ptf_site_cas(i,image+PTF_HIER,ptf_page+0x10u*i)) {ptf_restore();ptf_stage="cas";return 0;}
        ptf_done|=1u<<i;
    }
    ptf_stage="armed";return 1;
}
static int ptf_owned(void) {
    if(!ptf_installed) return 1;
    for(unsigned i=0;i<PTF_SITES;i++) if(ptf_site_state(i)!=1) return 0;
    unsigned char want[0x10u*PTF_SITES];ptf_write_code(want);
    return ptf_page && !memcmp(ptf_page,want,sizeof(want));
}
/* Gate tick (main thread, no pass running): any failure or a changed site stops the gate with reason 180. */
static int ptf_check(void) {
    InterlockedExchange64(&ptf_tick_rows,0);
    if(ptf_dock_rows && stats.issued && stats.issued%PTF_GATE_FORGET==0) ptf_gate_sweep(stats.issued);
    if(InterlockedCompareExchange64(&ptf_failures,0,0)) return 0;
    if(!ptf_owned()) {ptf_stage="site_changed";InterlockedIncrement64(&ptf_failures);return 0;}
    return 1;
}
static void ptf_ref_text(char out[40],ptf_ref_t r) {snprintf(out,40,"%016llx/%u/%u",(unsigned long long)r.key,r.klass,r.depth);}
static int ptf_row_text(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&ptf_rows,0,0);int n;
    if(!ptf_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)PTF_RING && index<(uint64_t)(count-(LONG64)PTF_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const ptf_row_t *r=&ptf_ring[index%PTF_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index+1) return 0;
    char a[40],b[40],o[40],sim[129],native[129];uint64_t time;memcpy(&time,&r->time,8);
    if(r->dock==3) { /* a dock gate row: native[] holds the tuple, model+0x68, new, cheap, bank and the update counts */
        const unsigned char *in=(const unsigned char*)r->native;char ship[40],timer[17];uint64_t key,full,cheap;unsigned klass;int32_t m68;
        ptf_ref_text(ship,r->ship);motion_watch_hex(timer,in+8,8);
        memcpy(&key,in+16,8);memcpy(&klass,in+24,4);memcpy(&m68,in+32,4);memcpy(&full,in+40,8);memcpy(&cheap,in+48,8);
        n=snprintf(out,cap,"index=%llu tick=%llu site=dock_gate dock=3 ship=%s new=%u pending=%u f5c=%u ac8=%u m68=%08x list=%u open=%u "
            "dock_id=%u target=%016llx/%u timer=%s cheap_now=%u full=%llu cheap=%llu bank=%u time=%016llx",(unsigned long long)index,
            (unsigned long long)r->tick,ship,in[36],in[0],in[1],in[2],(unsigned)m68,in[4],in[5],in[28],(unsigned long long)key,klass,timer,
            in[37],(unsigned long long)full,(unsigned long long)cheap,in[38],(unsigned long long)time);
        return n>0 && (size_t)n<cap?n:0;
    }
    ptf_ref_text(a,r->a);ptf_ref_text(b,r->b);ptf_ref_text(o,r->obj);
    motion_watch_hex(sim,(const unsigned char*)r->sim,64);motion_watch_hex(native,(const unsigned char*)r->native,64);
    const int show_native=r->mode==PTF_OBSERVE && r->dock!=1;
    n=snprintf(out,cap,"index=%llu tick=%llu site=%s mode=%s a=%s b=%s object=%s ctrl=%08x time=%016llx differs=%u sim=%s%s%s",
        (unsigned long long)index,(unsigned long long)r->tick,ptf_sites[r->site<PTF_SITES?r->site:0].name,r->mode==PTF_OBSERVE?"observe":"sim",
        a,b,r->object?o:"-",r->ctrl_vt,(unsigned long long)time,r->differs,sim,show_native?" native=":"",show_native?native:"");
    if(n>0 && (size_t)n<cap && r->dock==2) { /* a row made with the dock rows' option: the stepping ship */
        char ship[40];ptf_ref_text(ship,r->ship);
        int m=snprintf(out+n,cap-(size_t)n," ship=%s",ship);
        n=m>0 && (size_t)m<cap-(size_t)n?n+m:-1;
    }
    if(n>0 && (size_t)n<cap && r->dock==1) { /* dock rows: ctrl= is the thread class, native[] holds the range test's inputs */
        const unsigned char *in=(const unsigned char*)r->native;char ship[40],slot[33],local[33],range[25],timer[17],flags[13];
        ptf_ref_text(ship,r->ship);motion_watch_hex(slot,in,16);motion_watch_hex(local,in+16,16);motion_watch_hex(range,in+32,12);
        motion_watch_hex(timer,in+48,8);snprintf(flags,sizeof(flags),"%02x%02x%02x%02x%02x%02x",in[44],in[45],in[46],in[47],in[56],in[57]);
        int m=snprintf(out+n,cap-(size_t)n," outcome=%s dock=1 ship=%s slot=%s local=%s range=%s timer=%s flags=%s",ptf_outcomes[(r->win-1u)&7u],
                       ship,slot,local,range,timer,flags);
        n=m>0 && (size_t)m<cap-(size_t)n?n+m:-1;
    } else if(n>0 && (size_t)n<cap && r->win) { /* window rows: ctrl= is the thread class, native[] holds the chain */
        const uint32_t *c=(const uint32_t*)r->native;
        int m=snprintf(out+n,cap-(size_t)n," outcome=%s chain=%x,%x,%x,%x,%x,%x,%x,%x",ptf_outcomes[(r->win-1u)&7u],c[0],c[1],c[2],c[3],c[4],c[5],c[6],c[7]);
        n=m>0 && (size_t)m<cap-(size_t)n?n+m:-1;
    }
    return n>0 && (size_t)n<cap?n:0;
}
static int ptf_status(char *out,size_t cap) {
    uint64_t time;memcpy(&time,&ptf_last_time,8);
    int n=snprintf(out,cap,"mode=%s installed=%d restored=%d stage=%s outside=%lld trivial=%lld spaces=%lld diverted=%lld objects=%lld nav=%lld "
        "not_ancestor=%lld differs=%lld rows=%lld dropped=%lld failures=%lld dock_agents=%lld siblings=%lld parents=%lld/%lld time=%016llx sites=",
        ptf_mode==PTF_SIM?"sim":ptf_mode==PTF_OBSERVE?"observe":ptf_mode==PTF_ALL?"all":"off",ptf_installed,ptf_restored,ptf_stage,(long long)ptf_outside,(long long)ptf_trivial,(long long)ptf_spaces,
        (long long)ptf_diverted,(long long)ptf_objects,(long long)ptf_nav,(long long)ptf_not_ancestor,(long long)ptf_differs,(long long)ptf_rows,
        (long long)ptf_dropped,(long long)ptf_failures,(long long)ptf_dock_agents,(long long)ptf_siblings,(long long)ptf_parent_calls,(long long)ptf_parent_differs,(unsigned long long)time);
    if(n<0 || (size_t)n>=cap) return 0;
    for(unsigned i=0;i<PTF_SITES;i++) {
        int m=snprintf(out+n,cap-(size_t)n,"%s%s:%lld",i?",":"",ptf_sites[i].name,(long long)InterlockedCompareExchange64(&ptf_site_calls[i],0,0));
        if(m<0 || (size_t)m>=cap-(size_t)n) return 0;
        n+=m;
    }
    return n;
}

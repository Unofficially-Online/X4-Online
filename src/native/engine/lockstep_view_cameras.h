/* The cameras of the simulation's own cutscenes as viewers (native_gate_view_cameras, a session default; with view points
 * 'players' and view triggers 'block'; Windows gate; no stop reason of its own). Included by lockstep_view_points.h.
 * Runs of 11 Nov (two clients, a story call for player 2, twice): the speaker in her cockpit on both clients' monitors,
 * the shot noted and ended at the same ticks on both, 0 mismatches, world checks and runtime IDs equal.
 *
 * A person's session of 11 Nov: the comm monitor of a story call ("Incoming call", cutscene key ShowNPCFace) showed a
 * bare hull instead of the speaker. Natively a cutscene camera is in the universe's camera list (+0x2c0, count +0x3c0),
 * and the attention implementations raise what the camera is anchored in: the ancestry test 0x3e50e0 (CutsceneAnchor
 * 11, the interiors and the ship around the anchor), Object 0x6d20b0 (nearby 9), Zone 0x843b20 (8 / 7). So the speaker's
 * ship gets its interior and the speaker stands in it. Under view points the shadow universe has no camera at all (the
 * cameras are each client's own), and view triggers refuse the shot's own re-evaluations (the counts cutscene=8/0/8/0 of
 * that session: eight calls, each refused at the shot's first update and at its end). Nobody raised the ship.
 *
 * What a camera is to those implementations (read from all of them, build 9.00): the byte +0x870 (active), the anchor
 * +0x770, its vtable slot 1 (the cutscene camera's is `mov al,1`), and its position through 0x97a300, which needs +0x778
 * and, with the mode +0x868 zero and +0x20 zero, answers the anchor's own transform. So a stand-in works: 0x940 zero
 * bytes with the cutscene camera's vtable, +0x770 = +0x778 = the anchor and +0x870 = 1.
 *
 * Which anchors: the camera shots of cutscenes in the manager's list +0x30 (the simulation's cutscenes: MD starts them
 * on every client alike; lockstep_cutscene_ready.h rests on the same list). A camera shot's update makes its camera at
 * its first pass and calls SetAttention(target, 1) (trigger site 0x10d831a, the shot in r14); the shot's end calls it
 * again (0x10d5f5b, the shot in rbx). Both are refused as before, but noted here: the shot, its cutscene
 * ([[shot+0x50]+0x70], with its ID +8) and its target's component ID (shot+0x20). The frame's register is read by
 * unwinding to the site's own frame (the image's unwind data, as the back trace the trigger test already takes).
 * A shot of a cutscene that is not in the list (a client's own: a menu's preview) is counted and left alone.
 *
 * At the next latch (every client's same tick) the noted shots become viewers and the ended ones stop: the stand-ins
 * go into the shadow universe's camera list and the anchor's chain is recomputed as for a changed view point. A shot
 * whose cutscene has left the list, or whose ID is another's now, stops at that latch too. The anchor is found again by
 * its ID before every evaluation while a stand-in is shown (a component that is gone leaves the stand-in inactive).
 * Rendering and X4's cutscene manager are untouched. Not covered: CameraAnimated shots (their site is still refused and
 * not noted: none seen in a session), and a player who joins while a call runs (the join package does not carry the
 * shots; the call's viewers are missing on the joiner until it ends). */
#define VCAM_MAX 8u
#define VCAM_PAGE 0x2000u            /* the stand-ins on the view page, 0x1000 apart */
#define VCAM_SIZE 0x940u             /* a camera object (the shot allocates 0x940) */
#define VCAM_VTABLE 0x2bae0b8u       /* the cutscene camera's vtable; the implementations call slot 1 only */
#define VCAM_SLOT1 0x9c980u          /* mov al,1; ret */
#define VCAM_MANAGER 0x3d19900u      /* the cutscene manager; +0x30 the list of the simulation's cutscenes */
#define VCAM_MAP 0x6cf1500u
#define VCAM_FIND 0xced70u
#define VCAM_SITE_END 0x10d5f5bu     /* the shot's end: SetAttention(target, 1), the shot in rbx */
#define VCAM_SITE_SHOT 0x10d831au    /* a camera shot's update after it made its camera, the shot in r14 */
#define VCAM_LIST_MAX 256u
typedef void *(*vcam_find_fn)(void *,uint64_t,int);
typedef struct {unsigned char *shot,*cutscene;uint64_t id;int cutscene_id;unsigned char used,ended,shown;} vcam_t;
static vcam_t vcams[VCAM_MAX];
static int vcam_armed,vcam_pins_checked;
static unsigned vcam_shown;          /* stand-ins in the shadow's camera list */
static uint64_t vcam_shown_ids[VCAM_MAX];
static uint64_t vcam_adds,vcam_ends,vcam_dropped,vcam_local,vcam_other,vcam_full,vcam_no_shot,vcam_gone,vcam_chains,vcam_peak;
static const char *vcam_why="-";
static unsigned char *vcam_base;                                  /* the image, or a test region */
static vcam_find_fn vcam_find;                                    /* tests replace it */
static void *(*vcam_frame_override)(uint32_t site,int r14);       /* tests: the shot in the frame at a site */
static void view_refresh_chain(void *object);
static unsigned char *vcam_record(unsigned i) {return view_page+VCAM_PAGE+i*0x1000u;}
static int vcam_pins(void) {
    if(!vcam_pins_checked) {
        void *slot1=NULL;
        int ok=hash_ok(image+0x97a300u,0x3beu,"1f4e10df76e0f90efd7d31a28830dea4a3de889d8787e9cc054f82b24763f068") &&      /* camera position */
            hash_ok(image+0x10d5f00u,0x85u,"81bacaef84f062d193923e40c5592bf7803d7ad9c759c9fb6363befcb29825c3") &&          /* the shot's end */
            hash_ok(image+0x10d7f60u,0x3bau,"458fe887303d402680726b1aee22589433744afdc312659fc9f6f2ee1e190495") &&         /* a camera shot's update, to its SetAttention */
            hash_ok(image+0x1102ae0u,0x6au,"fafdac4254aebf99734cd9035a8ade695bdd53331af021231d462cb6daef5c10") &&          /* the shot's target from its ID */
            hash_ok(image+VCAM_SLOT1,3u,"e18aacce29affcdddaa5f641ac1ec12552e0fe9d066e6408941b1792818b8619") &&
            read_local(image+VCAM_VTABLE+8,&slot1,8) && slot1==(void*)(image+VCAM_SLOT1);
        vcam_pins_checked=ok?1:-1;
    }
    return vcam_pins_checked==1;
}
/* 1: the cutscene is in the manager's list of the simulation's cutscenes; 2: in its other list; 0: in neither. */
static int vcam_listed(const unsigned char *cutscene) {
    unsigned char *manager=NULL;
    if(!cutscene || !read_local(vcam_base+VCAM_MANAGER,&manager,8) || !manager) return 0;
    for(unsigned which=0;which<2;which++) {
        unsigned char *sentinel=NULL,*node=NULL;
        if(!read_local(manager+(which?0x50u:0x30u),&sentinel,8) || !sentinel || !read_local(sentinel,&node,8)) continue;
        for(unsigned steps=0;node && node!=sentinel && steps<VCAM_LIST_MAX;steps++) {
            unsigned char *at=NULL;
            if(!read_local(node+0x10,&at,8)) break;
            if(at==cutscene) return (int)which+1;
            if(!read_local(node,&node,8)) break;
        }
    }
    return 0;
}
/* A non-volatile register of the image frame that returns to `site`: the shot of the cutscene function that asked. */
static void *vcam_frame_register(uint32_t site,int r14) {
    if(vcam_frame_override) return vcam_frame_override(site,r14);
    CONTEXT c;RtlCaptureContext(&c);
    for(unsigned i=0;i<48 && c.Rip;i++) {
        if(c.Rip==(DWORD64)(uintptr_t)(image+site)) return (void*)(uintptr_t)(r14?c.R14:c.Rbx);
        DWORD64 base=0;PRUNTIME_FUNCTION entry=RtlLookupFunctionEntry(c.Rip,&base,NULL);
        if(!entry) {   /* a leaf: its return address is on top */
            DWORD64 back=0;if(!read_local((void*)(uintptr_t)c.Rsp,&back,8)) return NULL;
            c.Rip=back;c.Rsp+=8;continue;
        }
        void *handler=NULL;DWORD64 establisher=0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,c.Rip,entry,&c,&handler,&establisher,NULL);
    }
    return NULL;
}
/* A refused evaluation of one of the cutscene shots' sites (under the evaluation's lock): noted for the next latch. */
static void vcam_note(uint32_t site) {
    if(!vcam_armed || (site!=VCAM_SITE_END && site!=VCAM_SITE_SHOT)) return;
    unsigned char *shot=vcam_frame_register(site,site==VCAM_SITE_SHOT);
    if(!shot) {++vcam_no_shot;return;}
    if(site==VCAM_SITE_END) {
        for(unsigned i=0;i<VCAM_MAX;i++) if(vcams[i].used && vcams[i].shot==shot && !vcams[i].ended) {vcams[i].ended=1;++vcam_ends;}
        return;
    }
    unsigned char *owner=NULL,*cutscene=NULL;uint64_t id=0;int cutscene_id=0;
    if(!read_local(shot+0x50,&owner,8) || !owner || !read_local(owner+0x70,&cutscene,8) || !cutscene ||
       !read_local(cutscene+8,&cutscene_id,4) || !read_local(shot+0x20,&id,8) || !id) {++vcam_no_shot;return;}
    const int listed=vcam_listed(cutscene);
    if(listed!=1) {if(listed) ++vcam_local;else ++vcam_other;return;}
    vcam_t *e=NULL;
    for(unsigned i=0;i<VCAM_MAX;i++) if(vcams[i].used && vcams[i].shot==shot && vcams[i].cutscene==cutscene && vcams[i].cutscene_id==cutscene_id) return;
    for(unsigned i=0;i<VCAM_MAX && !e;i++) if(!vcams[i].used) e=&vcams[i];
    if(!e) {++vcam_full;return;}
    memset(e,0,sizeof(*e));e->shot=shot;e->cutscene=cutscene;e->cutscene_id=cutscene_id;e->id=id;e->used=1;++vcam_adds;
}
static void *vcam_resolve(uint64_t id) {
    void *map=NULL;
    if(!id || !vcam_find || !read_local(vcam_base+VCAM_MAP,&map,8) || !map) return NULL;
    return vcam_find(map,id,4);
}
/* Before an evaluation that sees the stand-ins: every anchor found again by its ID; one that is gone shows nothing. */
static void vcam_anchors(void) {
    for(unsigned i=0;i<vcam_shown;i++) {
        unsigned char *record=vcam_record(i);void *anchor=vcam_resolve(vcam_shown_ids[i]);
        memcpy(record+0x770,&anchor,8);memcpy(record+0x778,&anchor,8);record[0x870]=anchor?1:0;
    }
}
/* The latch (the frame thread, under the lock, every client's same tick): ended and orphaned shots stop, noted ones
 * start; the shadow's camera list is written and every changed anchor's chain recomputed. */
static void vcam_latch(void) {
    if(!vcam_armed || !view_page) return;
    void *changed[VCAM_MAX*2];unsigned n=0,shown=0;
    for(unsigned i=0;i<VCAM_MAX;i++) {
        vcam_t *e=&vcams[i];int cutscene_id=0;
        if(!e->used) continue;
        void *anchor=vcam_resolve(e->id);
        const int alive=!e->ended && vcam_listed(e->cutscene)==1 && read_local(e->cutscene+8,&cutscene_id,4) && cutscene_id==e->cutscene_id;
        if(!alive || !anchor) {
            if(!e->ended) ++vcam_dropped;
            if(!anchor) ++vcam_gone;
            if(e->shown && anchor && n<VCAM_MAX*2) changed[n++]=anchor;
            memset(e,0,sizeof(*e));continue;
        }
        if(!e->shown) {e->shown=1;if(n<VCAM_MAX*2) changed[n++]=anchor;}
        unsigned char *record=vcam_record(shown);void *vtable=image+VCAM_VTABLE;
        memset(record,0,VCAM_SIZE);memcpy(record,&vtable,8);
        vcam_shown_ids[shown]=e->id;
        memcpy(view_page+VIEW_PAGE_SHADOW+0x2c0+shown*8u,&record,8);++shown;
    }
    for(unsigned i=shown;i<VCAM_MAX;i++) memset(view_page+VIEW_PAGE_SHADOW+0x2c0+i*8u,0,8);
    const uint64_t count=shown;memcpy(view_page+VIEW_PAGE_SHADOW+0x3c0,&count,8);
    vcam_shown=shown;if(shown>vcam_peak) vcam_peak=shown;
    vcam_anchors();
    for(unsigned i=0;i<n;i++) {view_refresh_chain(changed[i]);++vcam_chains;}
}
/* gate.view_cameras(1): after view points and view triggers, before the first tick. */
static int vcam_arm(void) {
    if(vcam_armed) return stats.active!=0;   /* an identical re-arm after a loading-UI reload */
    if(!stats.active || stats.issued || GetCurrentThreadId()!=ui_thread) {vcam_why="late_or_thread";return 0;}
    if(view_mode!=VIEW_PLAYERS || !view_installed || !view_page) {vcam_why="view_points";return 0;}
    if(!vcam_pins()) {vcam_why="pins";return 0;}
    memset(vcams,0,sizeof(vcams));vcam_shown=0;memset(vcam_shown_ids,0,sizeof(vcam_shown_ids));
    vcam_adds=vcam_ends=vcam_dropped=vcam_local=vcam_other=vcam_full=vcam_no_shot=vcam_gone=vcam_chains=vcam_peak=0;
    vcam_base=image;vcam_find=(vcam_find_fn)(image+VCAM_FIND);
    memset(view_page+VIEW_PAGE_SHADOW+0x2c0,0,0x108);
    vcam_armed=1;vcam_why="-";return 1;
}
/* The session's end: no stand-in stays in the shadow's list. */
static void vcam_stop(void) {
    if(!vcam_armed) return;
    if(view_page) memset(view_page+VIEW_PAGE_SHADOW+0x2c0,0,0x108);
    vcam_shown=0;vcam_armed=0;memset(vcams,0,sizeof(vcams));
}
static int vcam_status(char *out,size_t capacity) {
    unsigned noted=0;char ids[VCAM_MAX*18+2]="-";int k=0;
    for(unsigned i=0;i<VCAM_MAX;i++) noted+=vcams[i].used!=0;
    for(unsigned i=0;i<vcam_shown;i++) k+=snprintf(ids+k,sizeof(ids)-(size_t)k,"%s%llx",i?",":"",(unsigned long long)vcam_shown_ids[i]);
    int n=snprintf(out,capacity,"armed=%d shown=%u ids=%s noted=%u adds=%llu ends=%llu dropped=%llu gone=%llu local=%llu other=%llu "
        "no_shot=%llu full=%llu chains=%llu peak=%llu why=%s",vcam_armed,vcam_shown,ids,noted,(unsigned long long)vcam_adds,
        (unsigned long long)vcam_ends,(unsigned long long)vcam_dropped,(unsigned long long)vcam_gone,(unsigned long long)vcam_local,
        (unsigned long long)vcam_other,(unsigned long long)vcam_no_shot,(unsigned long long)vcam_full,(unsigned long long)vcam_chains,
        (unsigned long long)vcam_peak,vcam_why);
    return n>0 && (size_t)n<capacity?n:0;
}

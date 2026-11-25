/* A slot's player's sector, zone and cluster changes for the scripts nobody owns (lockstep_pilot_death.h pvehk_place,
 * lockstep_mission_owners.h 'a place event's first player'), slots 2, 3 and 8, planted cues and objects:
 *   own   X4's post on P_k: the listeners of player 1's object nobody owns get it once, as player 1's object's (the
 *         object form), inside slot k's scope; the call's frame is slot k's (owned, shown on client k), a root instance
 *         it makes is slot k's, a static cue it makes fire is slot k's until it waits again; a denied script's cue, an
 *         owned cue and another slot's scope get nothing;
 *   body  X4's post on the object P_k is in while P_k is in no children table: the same event on P_k (vtable, the two
 *         IDs), after the forward; not on an object further up, not while P_k is listed;
 *   not   another event, another thread, the rule off, no player 1;
 *   join  a first player's claim rides the join line with its mark and comes back the same. */
#define TPP_P1 0
static unsigned char tpp_entry[0x30];
static unsigned char *tpp_listeners[4];
static unsigned tpp_calls,tpp_call_scope[8],tpp_call_place[8],tpp_call_shown[8];static void *tpp_call_cue[8],*tpp_call_source[8];
static unsigned char *tpp_fire;          /* the cue the listener makes fire (state 1 -> 3) */
static unsigned char *tpp_instance;      /* ...or a root instance it makes */
static unsigned char *tpp_lookup(void *source,unsigned type) {(void)source;(void)type;return tpp_entry;}
static unsigned tpp_type(void *event) {(void)event;return 0x55u;}
static void tpp_listener(void *cue,void *source,double now,unsigned type,void *event) {
    (void)now;(void)type;(void)event;
    const unsigned code=mpp_frame_enter(cue);   /* the script RNG's frame of the cue's event check */
    if(tpp_calls<8) {
        tpp_call_cue[tpp_calls]=cue;tpp_call_source[tpp_calls]=source;tpp_call_scope[tpp_calls]=pstk_scope_slot;
        tpp_call_place[tpp_calls]=mpp_place_slot;tpp_call_shown[tpp_calls]=mpp_show_slot();
    }
    ++tpp_calls;
    if(cue==tpp_fire) {const uint32_t complete=MPP_STATE_COMPLETE;memcpy((unsigned char*)cue+MPP_CUE_STATE,&complete,4);}
    if(tpp_instance) mpp_created_root(tpp_instance);
    mpp_frame_leave(code);
}
static unsigned char *tpp_objects[8];static unsigned tpp_object_count;
static int tpp_is_object(const unsigned char *c) {for(unsigned i=0;i<tpp_object_count;i++) if(tpp_objects[i]==c) return 1;return 0;}
static int tpp_listed_answer;static int tpp_listed(const unsigned char *player) {(void)player;return tpp_listed_answer;}
static unsigned tpp_posts;static void *tpp_post_source;
static uintptr_t tpp_post(void *source,void *event,double time,unsigned char immediate) {
    (void)event;assert(time==4.5 && immediate==1);tpp_post_source=source;++tpp_posts;return 0x77;
}
static unsigned tpp_tells;static unsigned char *tpp_told_player;static unsigned char tpp_told[0x28];
static void tpp_tell(unsigned char *player,const unsigned char *event) {tpp_told_player=player;memcpy(tpp_told,event,0x28);++tpp_tells;}
/* A static cue (its definition is itself) of a script with this name, waiting. */
static unsigned char *tpp_cue(unsigned char *at,uint64_t id,unsigned char *script) {
    const uintptr_t vt=(uintptr_t)image+SCRIPT_RNG_CUE_VT,self=(uintptr_t)at;const uint32_t waiting=MPP_STATE_WAITING;
    memset(at,0,0x200);memcpy(at,&vt,8);memcpy(at+MPP_CUE_ID,&id,8);memcpy(at+CUE_PATH_DEF,&self,8);memcpy(at+MPP_CUE_STATE,&waiting,4);
    memcpy(at+CUE_PATH_SCRIPT,&script,8);return at;
}
static void tpp_script(unsigned char *at,const char *name) {
    const uint64_t size=strlen(name),capacity=15;assert(size<=15);
    memset(at,0,0x80);memcpy(at+CUE_PATH_SCRIPT_NAME,name,size);memcpy(at+CUE_PATH_SCRIPT_NAME+0x10,&size,8);memcpy(at+CUE_PATH_SCRIPT_NAME+0x18,&capacity,8);
}
static void test_pilot_place(void) {
    _Alignas(16) static unsigned char cues[4][0x200],scripts[2][0x80],players[PILOT_SLOTS+1][0x100],rooms[3][0x100],ships[3][0x100],made[0x200];
    _Alignas(16) static unsigned char event[0x28],other_event[0x28];
    pslot_slot kept[PILOT_SLOTS+1];memcpy(kept,pslots,sizeof(kept));
    unsigned char *const base=pnat_base,*const first=pslot_p1;const unsigned scope_was=pstk_scope_slot;const DWORD ui_was=ui_thread;
    const int on_was=pvehk_forward_on,more_was=pvehk_events_more,armed_was=mpp_armed;void *slot_was=NULL;
    memcpy(&slot_was,image+SCRIPT_RNG_CUE_VT+0x20,8);
    pnat_base=image;ui_thread=GetCurrentThreadId();pvehk_forward_on=1;pvehk_place_installed=1;pvehk_events_more=0;
    mpp_armed=1;mpp_count=0;mpp_once_count=0;mpp_depth=0;mpp_depth1=0;mpp_first_count=0;mpp_notices_on=0;mpp_generating=0;
    memset(mpp_active,0,sizeof(mpp_active));mpp_active_open=0;mpp_story_slot=0;
    memset(mpp_place_scripts_denied,0,sizeof(mpp_place_scripts_denied));memset(mpp_place_scripts_allowed,0,sizeof(mpp_place_scripts_allowed));
    for(unsigned k=0;k<=PILOT_SLOTS;k++) memset(&pslots[k],0,sizeof(pslots[k]));
    pslot_p1=players[TPP_P1];pslots[2].player=players[2];pslots[3].player=players[3];pslots[8].player=players[8];
    tpp_script(scripts[0],"Story_Plot");tpp_script(scripts[1],"Guidance");
    unsigned char *const plot=tpp_cue(cues[0],0x7100,scripts[0]),*const manager=tpp_cue(cues[1],0x7101,scripts[1]),
                  *const owned=tpp_cue(cues[2],0x7102,scripts[0]),*const instancing=tpp_cue(cues[3],0x7103,scripts[0]);
    {const uintptr_t vt=(uintptr_t)image+SCRIPT_RNG_CUE_VT;const uint64_t id=0x7200;memset(made,0,sizeof(made));memcpy(made,&vt,8);memcpy(made+MPP_CUE_ID,&id,8);}
    mpp_claim_id(4,0x7102);   /* a mission slot 4 accepted */
    tpp_listeners[0]=plot;tpp_listeners[1]=manager;tpp_listeners[2]=owned;tpp_listeners[3]=instancing;
    {void *b0=tpp_listeners,*b1=tpp_listeners+4;memcpy(tpp_entry+8,&b0,8);memcpy(tpp_entry+0x10,&b1,8);}
    {void *fn=(void*)tpp_listener;memcpy(image+SCRIPT_RNG_CUE_VT+0x20,&fn,8);}
    pvehk_forward_lookup_test=tpp_lookup;pvehk_forward_owner_test=NULL;pvehk_place_post_test=tpp_post;
    {void *vt=image+PVEHK_EVENT_SECTOR;const uint64_t was=0x6001,is=0x6002;memset(event,0,sizeof(event));memcpy(event,&vt,8);memcpy(event+0x18,&was,8);memcpy(event+0x20,&is,8);}
    {void *vt=image+PVEHK_EVENT_SECTOR+0x40;memset(other_event,0,sizeof(other_event));memcpy(other_event,&vt,8);}
    /* The forward's event type comes from the event's vtable slot 1 (X4's); a planted one answers 0x55. */
    void *type_was=NULL;memcpy(&type_was,image+PVEHK_EVENT_SECTOR+8,8);{void *fn=(void*)tpp_type;memcpy(image+PVEHK_EVENT_SECTOR+8,&fn,8);}
    assert(pvehk_place_kind(event)==1 && !pvehk_place_kind(other_event) && !pvehk_place_kind(NULL));
    pvehk_place_seen=pvehk_place_own=pvehk_place_body=pvehk_place_forwarded=pvehk_place_unscoped=pvehk_place_denied=0;
    mpp_place_frames=mpp_place_claims=mpp_place_released=mpp_place_denials=0;
    /* own, slot 2 (its scope open): the plot's cue and the cue that makes an instance are called once each with player 1's
     * object, in slot 2's frame; the client's guidance and slot 4's mission are not; X4's post after it. */
    tpp_calls=0;tpp_posts=0;tpp_fire=plot;tpp_instance=NULL;pstk_scope_slot=2;
    assert(pvehk_place_post(players[2],event,4.5,1)==0x77 && tpp_posts==1 && tpp_post_source==players[2]);
    assert(tpp_calls==2 && tpp_call_cue[0]==plot && tpp_call_cue[1]==instancing && tpp_call_source[0]==players[TPP_P1] && tpp_call_source[1]==players[TPP_P1]);
    assert(tpp_call_scope[0]==2 && tpp_call_place[0]==2 && tpp_call_shown[0]==2 && tpp_call_shown[1]==2);   /* slot 2's frame: shown on client 2 */
    assert(!mpp_place_slot && !mpp_place_cue && !mpp_depth && !pvehk_place_mode && pstk_scope_slot==2);
    assert(pvehk_place_seen==1 && pvehk_place_own==1 && pvehk_place_forwarded==1 && pvehk_place_denied==1 && mpp_place_frames==2);
    /* the plot's cue fired: slot 2's until it waits again, with the mark; the one that heard and did not fire stays nobody's */
    assert(mpp_owner_of(0x7100)==2 && mpp_owners[mpp_count-1].slot==(2u|MPP_SLOT_ONCE) && mpp_once_count==1 && mpp_place_claims==1);
    assert(!mpp_owner_of(0x7103) && !mpp_owner_of(0x7101));
    /* ...so the next one skips it (owned), and a root instance made in slot 3's call is slot 3's */
    tpp_calls=0;tpp_fire=NULL;tpp_instance=made;pstk_scope_slot=3;
    pvehk_place_post(players[3],event,4.5,1);
    assert(tpp_calls==1 && tpp_call_cue[0]==instancing && tpp_call_scope[0]==3 && mpp_owner_of(0x7200)==3 && mpp_owner_of(0x7100)==2);
    {const uint64_t made_id=0x7200;for(unsigned i=0;i<mpp_count;i++) if(mpp_owners[i].cue==made_id) assert(mpp_owners[i].slot==3u);}   /* an instance: a plain claim */
    tpp_instance=NULL;
    /* slot 8 (its scope open), the same rule */
    tpp_calls=0;pstk_scope_slot=8;pvehk_place_post(players[8],event,4.5,1);assert(tpp_calls==1 && tpp_call_scope[0]==8 && tpp_call_place[0]==8);
    /* another slot's scope open: nothing forwarded, X4's post as ever */
    tpp_calls=0;tpp_posts=0;pstk_scope_slot=3;pvehk_place_post(players[2],event,4.5,1);
    assert(!tpp_calls && tpp_posts==1 && pvehk_place_unscoped==1);
    /* not a place event, another thread, the rule off, no player 1, not installed: X4's post alone */
    {const LONG64 seen=pvehk_place_seen;tpp_posts=0;pstk_scope_slot=2;
     pvehk_place_post(players[2],other_event,4.5,1);
     ui_thread=GetCurrentThreadId()+1;pvehk_place_post(players[2],event,4.5,1);ui_thread=GetCurrentThreadId();
     pvehk_forward_on=0;pvehk_place_post(players[2],event,4.5,1);pvehk_forward_on=1;
     pslot_p1=NULL;pvehk_place_post(players[2],event,4.5,1);pslot_p1=players[TPP_P1];
     pvehk_place_installed=0;pvehk_place_post(players[2],event,4.5,1);pvehk_place_installed=1;
     assert(tpp_posts==5 && !tpp_calls && pvehk_place_seen==seen);}
    /* body: slot 3 sits in room 1 of ship 1, which is docked at ship 0 (both objects); P_3 in no children table */
    {uintptr_t v=(uintptr_t)rooms[1];memcpy(players[3]+0x70,&v,8);v=(uintptr_t)ships[1];memcpy(rooms[1]+0x70,&v,8);v=(uintptr_t)ships[0];memcpy(ships[1]+0x70,&v,8);}
    tpp_object_count=0;tpp_objects[tpp_object_count++]=ships[0];tpp_objects[tpp_object_count++]=ships[1];
    pvehk_place_object_test=tpp_is_object;pvehk_place_listed_test=tpp_listed;pvehk_place_tell_test=tpp_tell;tpp_listed_answer=0;
    assert(pvehk_place_carrier(players[3])==ships[1] && !pvehk_place_carrier(players[2]) && !pvehk_place_carrier(NULL));
    tpp_calls=0;tpp_tells=0;tpp_posts=0;pstk_scope_slot=3;
    pvehk_place_post(ships[1],event,4.5,1);
    assert(tpp_tells==1 && tpp_told_player==players[3] && !memcmp(tpp_told,event,8) && !memcmp(tpp_told+0x18,event+0x18,16));
    assert(tpp_calls==1 && tpp_call_scope[0]==3 && tpp_call_source[0]==players[TPP_P1] && tpp_posts==1 && tpp_post_source==ships[1] && pvehk_place_body==1);
    pvehk_place_post(ships[0],event,4.5,1);assert(tpp_tells==1);                                            /* the carrier above: X4's post on ship 1 tells it */
    tpp_listed_answer=1;pvehk_place_post(ships[1],event,4.5,1);assert(tpp_tells==1);tpp_listed_answer=0;    /* in the tree: X4 posts on P_3 itself */
    pstk_scope_slot=2;tpp_calls=0;pvehk_place_post(ships[1],event,4.5,1);                                  /* another scope: told, not forwarded */
    assert(tpp_tells==2 && !tpp_calls && pvehk_place_unscoped==2);
    pvehk_place_object_test=NULL;pvehk_place_listed_test=NULL;pvehk_place_tell_test=NULL;
    /* the first player's claim on the join line: marked, and back the same */
    {   static char line[MPP_OWNERS*24+256];const uint64_t h=mpp_owners_hash();
        assert(mpp_state(line,sizeof(line))>0 && strstr(line,"18:7100"));
        const unsigned count=mpp_count;mpp_count=0;mpp_once_count=0;
        assert(mpp_restore(line,strlen(line)) && mpp_count==count && mpp_owners_hash()==h && mpp_once_count==1 && mpp_owner_of(0x7100)==2);
        mpp_count=0;assert(!mpp_restore("v2|9:5|",7) && !mpp_restore("v2|25:5|",8) && !mpp_restore("v2|16:5|",8) && !mpp_restore("v2||18:5",8));
        assert(mpp_restore("v2|24:5|",8) && mpp_owner_of(5)==8);mpp_count=0;
        assert(mpp_restore(line,strlen(line)));}
    /* the cue waits again (its script reset it): the claim goes at its next frame; a plain claim of a waiting cue stays
     * (no slot has a player here: an owned frame is refused before any scope, as the mission owners' test does it) */
    {   pstk_scope_slot=0;const uint32_t waiting=MPP_STATE_WAITING;
        for(unsigned k=2;k<=PILOT_SLOTS;k++) pslots[k].player=NULL;
        assert(!mpp_frame_enter(plot) && mpp_owner_of(0x7100)==2);   /* still complete: kept */
        memcpy(plot+MPP_CUE_STATE,&waiting,4);
        assert(!mpp_frame_enter(plot) && !mpp_owner_of(0x7100) && !mpp_once_count && mpp_place_released==1);
        mpp_claim_id(3,0x7103);assert(!mpp_frame_enter(instancing) && mpp_owner_of(0x7103)==3);}   /* not a first player's: stays */
    {char text[2048];assert(pvehk_place_status(text,sizeof(text))>0 && !strcmp(text," place=1/8/4/2/4/2/4"));
     assert(mpp_status(text,sizeof(text))>0 && strstr(text," place=5/1/1/4"));}
    memcpy(image+PVEHK_EVENT_SECTOR+8,&type_was,8);memcpy(image+SCRIPT_RNG_CUE_VT+0x20,&slot_was,8);
    pvehk_forward_lookup_test=NULL;pvehk_place_post_test=NULL;pvehk_place_installed=0;pvehk_forward_on=on_was;pvehk_events_more=more_was;
    memcpy(pslots,kept,sizeof(kept));pnat_base=base;pslot_p1=first;pstk_scope_slot=scope_was;ui_thread=ui_was;
    mpp_count=0;mpp_once_count=0;mpp_armed=armed_was;mpp_depth=0;
    pvehk_place_seen=pvehk_place_own=pvehk_place_body=pvehk_place_forwarded=pvehk_place_unscoped=pvehk_place_denied=0;
    mpp_place_frames=mpp_place_claims=mpp_place_released=mpp_place_denials=0;
    printf("pilot place events: slots 2, 3 and 8 heard by the scripts nobody owns in their scopes, the first player's claims, the body post, the join line; tests PASS\n");
}
/* The pinned image: the eight posts are PostEvent calls in one block each, behind the three events' builders; the
 * vtables are the three events'; installed and taken back on a copy of the image. */
static void test_pilot_place_native_image(const char *path) {
    unsigned char *const prior=image,*const base=pnat_base,*const page=pslot_page;const int on=pvehk_forward_on;
    image=test_pmath_map(path);pnat_base=image;
    assert(hash_ok(image+PVEHK_PLACE_PIN,PVEHK_PLACE_PIN_SIZE,PVEHK_PLACE_PIN_SHA));
    for(unsigned i=0;i<PVEHK_PLACE_SITES;i++) {
        const unsigned s=pvehk_place_sites[i];int32_t rel;memcpy(&rel,image+s+1,4);
        assert(image[s]==0xe8 && s+5u+(uint32_t)rel==PVEHK_EVENT_POST_NOW && ((s+4u)&~15u)==(s&~15u) && s>=PVEHK_PLACE_PIN && s+5u<=PVEHK_PLACE_PIN+PVEHK_PLACE_PIN_SIZE);
    }
    /* each post's event: built from the event's vtable (lea rax/rcx, its vtable) in the lines before it */
    {   static const unsigned vt_at[PVEHK_PLACE_SITES]={0x6cf12eu,0x6cf1bdu,0x6cf26cu,0x6cef2eu,0x6cefbdu,0x6cf06cu,0x6cedb8u,0x6cee80u};
        static const unsigned vt_is[PVEHK_PLACE_SITES]={PVEHK_EVENT_CLUSTER,PVEHK_EVENT_SECTOR,PVEHK_EVENT_ZONE,PVEHK_EVENT_CLUSTER,PVEHK_EVENT_SECTOR,PVEHK_EVENT_ZONE,PVEHK_EVENT_SECTOR,PVEHK_EVENT_ZONE};
        for(unsigned i=0;i<PVEHK_PLACE_SITES;i++) {
            int32_t rel;assert(image[vt_at[i]]==0x48 && image[vt_at[i]+1]==0x8d);memcpy(&rel,image+vt_at[i]+3,4);
            assert(vt_at[i]+7u+(uint32_t)rel==vt_is[i] && vt_at[i]<pvehk_place_sites[i] && pvehk_place_sites[i]-vt_at[i]<0x80u);
        }
    }
    {   static const char *const names[3]={".?AVChangedSectorEvent@U@@",".?AVChangedZoneEvent@U@@",".?AVChangedClusterEvent@U@@"};
        static const unsigned vts[3]={PVEHK_EVENT_SECTOR,PVEHK_EVENT_ZONE,PVEHK_EVENT_CLUSTER};
        for(unsigned i=0;i<3;i++) {
            unsigned char *col=NULL;memcpy(&col,image+vts[i]-8,8);uint32_t td=0;memcpy(&td,col+12,4);
            assert(!strcmp((const char*)image+td+16,names[i]));
        }
    }
    pvehk_place_installed=0;pvehk_forward_on=1;pslot_page=pslot_alloc_page();assert(pslot_page);
    unsigned char before[PVEHK_PLACE_PIN_SIZE];memcpy(before,image+PVEHK_PLACE_PIN,sizeof(before));
    pvehk_place_install();assert(pvehk_place_installed);
    for(unsigned i=0;i<PVEHK_PLACE_SITES;i++) {
        int32_t rel;memcpy(&rel,image+pvehk_place_sites[i]+1,4);assert(image+pvehk_place_sites[i]+5+rel==pslot_page+PVEHK_PLACE_STUB);
    }
    {void *to=NULL;memcpy(&to,pslot_page+PVEHK_PLACE_STUB+6,8);assert(to==(void*)pvehk_place_post);}
    pvehk_place_restore();assert(!pvehk_place_installed && !memcmp(before,image+PVEHK_PLACE_PIN,sizeof(before)));
    image[PVEHK_PLACE_PIN+0x10]^=1;pvehk_place_install();assert(!pvehk_place_installed);image[PVEHK_PLACE_PIN+0x10]^=1;   /* another build: X4's posts */
    VirtualFree(pslot_page,0,MEM_RELEASE);pslot_page=page;pvehk_forward_on=on;pnat_base=base;image=prior;
    printf("pinned native image place events verified (the eight posts and their events, installed and back)\n");
}

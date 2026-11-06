/* native_gate_join_specdrop (diagnostic, default off; with PEND, native_gate_join_pend): the joiner's first frame without the
 * load's one-shot view cache specifications.
 * The load leaves every component a pending cache-specification list (+0xd8: AddCacheSpecification 0x3d5180, the one-shot kind 7
 * with the flags 0x19: the cached parent, the pose, 0x10) besides its live flags +0x1e0 0x19. The joiner's first BuildCache
 * 0x3d4a80 applies each list to its subtree (PropagateCacheFlags 0x3d46b0 with the ancestors' due flags | the child's +0x1e0,
 * setting +0x1e0 again) and frees it, so PEND's words are overwritten in the same frame and the joiner republishes every posed
 * component. [F: w1z14 and w1z15 (PEND on), the hot join's JOIN_WATCHPCF: 425,464 PropagateCacheFlags calls in step T+1 against the
 * session's 6,163; 800259fc's navigation controller, new in that step, refreshed by the republication's transform query a step
 * before the session's (report LOCKSTEP-JOIN-IN-WINDOW 24).]
 * With specdrop the joiner clears the flags of its kind 7 entries on every component PEND would write (both views posed) whose
 * session record holds no kind 7 entry (SPEC): the engine's BuildCache still takes and frees the list and keeps +0x1e8 (the
 * subtree's count) as its own, and with PEND's +0x1e0 nothing of the load is republished. Never frees, never moves an entry,
 * never touches +0x1e8 or the list's count. Runs in the admission after SPEC and VIEWK, before PEND (main thread). Status
 * specdrop=on:lists:cleared:session:unposed:unread. */
#ifndef LOCKSTEP_JOIN_SPECDROP_H
#define LOCKSTEP_JOIN_SPECDROP_H
#include <stddef.h>
#define JOIN_SPECDROP_KIND 7u
static int join_specdrop_on;
static uint64_t join_specdrop_lists,join_specdrop_cleared,join_specdrop_session,join_specdrop_unposed,join_specdrop_unread;
/* Whether the session's SPEC records (sorted by portable ID, then kind) hold this kind for this component. */
static int join_specdrop_session_has(const join_tables *s,uint64_t portable,uint32_t kind) {
    uint32_t lo=0,hi=s->nspec;
    while(lo<hi) {
        const uint32_t mid=lo+(hi-lo)/2u;const join_spec_rec *r=&s->spec[mid];
        if(r->portable<portable || (r->portable==portable && r->kind<kind)) lo=mid+1u;else hi=mid;
    }
    return lo<s->nspec && s->spec[lo].portable==portable && s->spec[lo].kind==kind;
}
static void join_admit_specdrop(const join_tables *s,const join_tables *mine) {
    join_specdrop_lists=join_specdrop_cleared=join_specdrop_session=join_specdrop_unposed=join_specdrop_unread=0;
    if(!join_specdrop_on || s->version<13) return;
    for(uint32_t i=0;i<mine->nids;i++) {
        unsigned char *object=mine->ids[i].portable?join_object(mine->ids[i].runtime):NULL,*at=NULL,*p0=NULL,*p1=NULL;
        join_spec_entry e[JOIN_SPEC_MAX];
        if(!object) continue;
        const int n=join_spec_read(object,e,&at);
        if(n<0) {++join_specdrop_unread;continue;}
        int due=0;
        for(int m=0;m<n;m++) due|=e[m].kind==JOIN_SPECDROP_KIND && e[m].flags;
        if(!due) continue;
        ++join_specdrop_lists;
        if(join_specdrop_session_has(s,mine->ids[i].portable,JOIN_SPECDROP_KIND)) {++join_specdrop_session;continue;}
        if(!join_read(object+JOIN_PEND_VIEW_POSE,&p0,8) || !join_read(object+JOIN_PEND_VIEW_POSE+0x80u,&p1,8) || !p0 || !p1)
            {++join_specdrop_unposed;continue;}   /* a view the engine has yet to publish once keeps its publication */
        for(int m=0;m<n;m++) {
            const uint32_t zero=0;
            if(e[m].kind!=JOIN_SPECDROP_KIND || !e[m].flags) continue;
            if(join_poke((uintptr_t)(at+(size_t)m*sizeof(join_spec_entry)+offsetof(join_spec_entry,flags)),&zero,4)) ++join_specdrop_cleared;
        }
    }
}
static int join_specdrop_status(char *out,size_t capacity) {
    return snprintf(out,capacity," specdrop=%d:%llu:%llu:%llu:%llu:%llu",join_specdrop_on,(unsigned long long)join_specdrop_lists,
                    (unsigned long long)join_specdrop_cleared,(unsigned long long)join_specdrop_session,
                    (unsigned long long)join_specdrop_unposed,(unsigned long long)join_specdrop_unread);
}
#endif

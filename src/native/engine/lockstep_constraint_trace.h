/* native_gate_constraint_diag (diagnostic, default off; both gates, the same text): the contact constraints of the
 * fixture ships as Jolt's velocity solve takes them, one CONSTRAINT row each. Included by lockstep_island_order.h
 * (Windows) and linux/gate_island_order.h (Linux): their sort stub calls ctrace_capture after the island's sort.
 *
 * Why: the battle's tick-133 state difference between Windows and Linux (W36r against lx71a) is the collision of
 * actors 4 and 11, whose two contacts reach the listener alike on both (CONTACT rows: manifolds, settings, bodies),
 * solved a few ulps apart. A constraint here holds what AddContactConstraint made of a contact before the warm start
 * (normal, friction, inverse masses and inertia scales; per point the three axis parts: r1 + u and r2 cross the axis,
 * both times the inverse inertia, the effective mass, the spring's bias and softness, the warm-start lambda), and the
 * contact cache point it points at holds the lambdas the solve stored at the end of the step. Equal setups with other
 * stored lambdas put the difference in the iterations; other setups in AddContactConstraint.
 *
 * Jolt's ContactConstraint (0x360 bytes on both builds): body 1 +0, body 2 +8, the sort key +0x10, the world normal
 * +0x18, friction, inverse mass 1, inverse inertia scale 1, inverse mass 2, inverse inertia scale 2 (to +0x38), the
 * point count +0x38, up to four points of 0xc8 from +0x40: three AxisConstraintParts of 0x40 (non-penetration, two
 * friction axes: r1 + u x axis, r2 x axis, invI1 (r1 + u x axis), invI2 (r2 x axis), effective mass, bias, softness,
 * total lambda) and the CachedContactPoint pointer +0xc0 (positions 1 and 2, then the non-penetration lambda +0x18
 * and the two friction lambdas +0x1c, +0x20, written by StoreAppliedImpulses). The rows are kept raw; the text reads
 * the cache point when the probe drains (after the frame, the step done: the cache it points into lives until the
 * step after next). Pointers and padding never reach the text.
 *
 * The including gate sets ctrace_actor (a fixture actor 1..12 for a body, else 0); rows are claimed atomically (a
 * job thread may solve). A failure (an unreadable constraint, the rows full) counts in ctrace_failures; the gate
 * stops with its island-order reason (215) at the next tick check. */
#define CTRACE_STRIDE 0x360u
#define CTRACE_POINTS 4u
#define CTRACE_POINT 0xc8u
#define CTRACE_ROWS 512u
typedef struct {
    uint64_t tick,ordinal;uint32_t index,size,actor[2],body_id[2];
    unsigned char c[CTRACE_STRIDE];
} ctrace_row;
static ctrace_row ctrace_rows[CTRACE_ROWS];
static volatile LONG64 ctrace_claimed,ctrace_ordinal,ctrace_failures,ctrace_seen;
static uint32_t ctrace_count;   /* rows readable, set on the UI thread from the claims */
static int ctrace_on;
static unsigned (*ctrace_actor)(const unsigned char *body);
/* After the island's sort: every constraint of [begin, end) with a fixture body. */
static void ctrace_capture(const uint32_t *begin,const uint32_t *end,const unsigned char *array,uint64_t tick) {
    if(!ctrace_on || !ctrace_actor || !begin || end<begin || !array) return;
    for(const uint32_t *i=begin;i<end;i++) {
        const unsigned char *c=array+(size_t)*i*CTRACE_STRIDE;unsigned char *body[2]={NULL,NULL};unsigned actor[2];
        InterlockedIncrement64(&ctrace_seen);
        if(!read_local(c,body,16)) {InterlockedIncrement64(&ctrace_failures);continue;}
        actor[0]=body[0]?ctrace_actor(body[0]):0u;actor[1]=body[1]?ctrace_actor(body[1]):0u;
        if(!actor[0] && !actor[1]) continue;
        const LONG64 slot=InterlockedIncrement64(&ctrace_claimed)-1;
        if(slot<0 || slot>=(LONG64)CTRACE_ROWS) {InterlockedIncrement64(&ctrace_failures);continue;}
        ctrace_row *r=&ctrace_rows[slot];memset(r,0,sizeof(*r));
        r->tick=tick;r->ordinal=(uint64_t)InterlockedIncrement64(&ctrace_ordinal);
        r->index=(uint32_t)(i-begin);r->size=(uint32_t)(end-begin);r->actor[0]=actor[0];r->actor[1]=actor[1];
        if(!read_local(c,r->c,CTRACE_STRIDE) || (body[0] && !read_local(body[0]+0x70,&r->body_id[0],4)) ||
           (body[1] && !read_local(body[1]+0x70,&r->body_id[1],4))) {memset(r->c,0,sizeof(r->c));InterlockedIncrement64(&ctrace_failures);}
    }
}
static int ctrace_put_words(char *out,size_t size,int n,const unsigned char *p,unsigned words) {
    for(unsigned i=0;i<words && n>0 && (size_t)n<size;i++) {uint32_t w;memcpy(&w,p+4u*i,4);n+=snprintf(out+n,size-(size_t)n,"%08x",w);}
    return n;
}
/* "tick=T ordinal=O index=I size=S body1=ACTOR:BODYID body2=.. key=K count=C head=<9 words: +0x18..+0x3c>
 *  point0=<48 words: the three parts> cached0=<9 words: positions, lambdas | none> point1=..". */
static int ctrace_text(const ctrace_row *r,char *out,size_t size) {
    uint64_t key;uint32_t count;memcpy(&key,r->c+0x10,8);memcpy(&count,r->c+0x38,4);
    if(count>CTRACE_POINTS) count=0;
    int n=snprintf(out,size,"tick=%llu ordinal=%llu index=%u size=%u body1=%u:%08x body2=%u:%08x key=%016llx count=%u head=",
        (unsigned long long)r->tick,(unsigned long long)r->ordinal,r->index,r->size,r->actor[0],r->body_id[0],r->actor[1],
        r->body_id[1],(unsigned long long)key,count);
    n=ctrace_put_words(out,size,n,r->c+0x18,9);
    for(uint32_t k=0;k<count && n>0 && (size_t)n<size;k++) {
        const unsigned char *point=r->c+0x40+CTRACE_POINT*k;unsigned char *cached=NULL,cache[36];
        n+=snprintf(out+n,size-(size_t)n," point%u=",k);
        n=ctrace_put_words(out,size,n,point,48);
        if(n<=0 || (size_t)n>=size) break;
        memcpy(&cached,point+0xc0,8);
        n+=snprintf(out+n,size-(size_t)n," cached%u=",k);
        if(cached && read_local(cached,cache,sizeof(cache))) n=ctrace_put_words(out,size,n,cache,9);
        else if(n>0 && (size_t)n<size) n+=snprintf(out+n,size-(size_t)n,"none");
    }
    return n>0 && (size_t)n<size?n:0;
}
/* The probe's drain: index 0 the rows held (the claims so far, at most CTRACE_ROWS), 1.. a row's text. */
static int ctrace_row_text(uint64_t index,char *out,size_t size) {
    if(index==0 || index>ctrace_count) return 0;
    return ctrace_text(&ctrace_rows[index-1],out,size);
}
static uint32_t ctrace_rows_held(void) {
    const LONG64 claimed=InterlockedCompareExchange64(&ctrace_claimed,0,0);
    ctrace_count=claimed<(LONG64)CTRACE_ROWS?(uint32_t)claimed:CTRACE_ROWS;
    return ctrace_count;
}
/* Between ticks only (the probe drains after the frame): no step is running then. */
static void ctrace_clear(void) {InterlockedExchange64(&ctrace_claimed,0);ctrace_count=0;}
static int ctrace_status(char *out,size_t size) {
    const int n=snprintf(out,size,"on=%d seen=%lld claimed=%lld failures=%lld",ctrace_on,(long long)ctrace_seen,
        (long long)ctrace_claimed,(long long)ctrace_failures);
    return n>0 && (size_t)n<size?n:0;
}

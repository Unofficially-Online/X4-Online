/* BODY (package v13, section 50): the physics bodies of the destructible objects, read and written at the Jolt body.
 *
 * j1-120/j1-121 at T+1, with every census row equal: the declared state still parted on two fixture actors' physics bodies,
 * which the census does not read. Actor 4's first body has the session's inverse mass but another inverse inertia diagonal
 * and inertia rotation (about 1%: the body's mass properties, made once from the compound shape where its parts were when it
 * was created, on the session at boot, on the joiner at its load); the wreck actor 8 has another pose (z 2 ulp, the
 * rotation), other velocities and another record pre transform. A body the joiner's load made from the component's
 * transform sits where the shape and the pose were at the load, not where the session's integration took it.
 *
 * Capture: every U::Destructible (its census class has a hull) with an XPhys record at +0x260 / +0x268 (which 0 / 1;
 * lockstep_body.h body_state's chain: record owner, scene, system +0x1b8, interface +8, manager, body by BodyID, the body's
 * ID and user data checked) gives one record: the body's position and rotation (body +0x00, 32 bytes), the motion
 * properties' first 92 bytes (linear and angular velocity, inverse inertia diagonal, inertia rotation, force, torque,
 * inverse mass; the process-local active and island indices after them are left out) and the record's previous and delta
 * transforms (+0x40, +0x60, 32 bytes each). Sorted by (portable, which).
 * Admission (main thread, phase 3, no physics step running): each record's body is found the same way on the joiner's
 * object and written where it differs: pose, motion, the record's transforms (counted apart). The body's broad phase bounds
 * stay the joiner's: the writes move a body by ulps, inside every bound. No body on the joiner: missing; a body without
 * motion properties on one side only: shape. Status JOIN_STATUS body=written:equal:pose:motion:record:missing:shape
 * body_cap=. */
#define JOIN_BODY_RECORD 0x260u   /* U::Destructible: two XPhys records */
#define JOIN_BODY_MOTION_BYTES 92u
static void join_world_row(unsigned *rows,const char *format,...);   /* lockstep_join_world.h, below */
static uint64_t join_body_written,join_body_equal,join_body_pose,join_body_motion,join_body_record,join_body_missing,join_body_shape;
static uint32_t join_body_cap;
/* The Jolt body of an object's record `which`: 1 with the record, the body and its motion properties (NULL: a static body); 0 no
 * record (or none valid). */
static int join_body_locate(unsigned char *object,unsigned which,unsigned char **record,unsigned char **body,unsigned char **motion) {
    unsigned char *r,*xbody,*scene,*system,*iface,*manager,*bodies,*b,*m;void *owner,*userdata;uint32_t id,actual;uint64_t count;
    if(which>1 || !join_read(object+JOIN_BODY_RECORD+which*8u,&r,8) || !r) return 0;
    if(!join_read(r,&owner,8) || owner!=object || !join_read(r+0x10,&xbody,8) || !xbody || !join_read(xbody+8,&id,4) || id==0xffffffffu ||
       !join_read(r+0x18,&scene,8) || !scene || !join_read(scene+0x20,&system,8) || !system || !join_read(system+0x1b8,&iface,8) || !iface ||
       !join_read(iface+8,&manager,8) || !manager || !join_read(manager,&count,8) || (id&0x7fffffu)>=count || count>0x800000u ||
       !join_read(manager+0x10,&bodies,8) || !bodies || !join_read(bodies+(id&0x7fffffu)*8u,&b,8) || !b || ((uintptr_t)b&1u) ||
       !join_read(b+0x70,&actual,4) || actual!=id || !join_read(b+0x50,&userdata,8) || userdata!=(void*)r || !join_read(b+0x48,&m,8)) return 0;
    *record=r;*body=b;*motion=m;return 1;
}
static int join_body_read(unsigned char *object,unsigned which,uint64_t portable,join_body_rec *out) {
    unsigned char *record,*body,*motion;
    if(!join_body_locate(object,which,&record,&body,&motion)) return 0;
    memset(out,0,sizeof(*out));out->portable=portable;out->which=which;
    if(!join_read(record+0x38,&out->flags,4) || !join_read(body,out->pose,32) || !join_read(record+0x40,out->pre,32) ||
       !join_read(record+0x60,out->delta,32)) return 0;
    if(motion && !join_read(motion,out->motion,JOIN_BODY_MOTION_BYTES)) return 0;
    out->pad=motion?1u:0u;   /* 1: a moving body (the motion properties were read) */
    return 1;
}
static int join_body_cmp(const void *a,const void *b) {
    const join_body_rec *x=a,*y=b;
    return x->portable!=y->portable?(x->portable<y->portable?-1:1):x->which!=y->which?(x->which<y->which?-1:1):0;
}
/* Capture, inside the world loop: an object with a hull (U::Destructible). */
static int join_capture_body(join_tables *t,unsigned char *object,uint64_t portable,uint32_t *cap) {
    for(unsigned which=0;which<2;which++) {
        join_body_rec r;if(!join_body_read(object,which,portable,&r)) continue;
        if(!JOIN_PUSH(t->body,t->nbody,*cap,r)) return join_note("memory");
    }
    return 1;
}
static int join_body_sort(join_tables *t) {
    if(t->nbody) qsort(t->body,t->nbody,sizeof(*t->body),join_body_cmp);
    for(uint32_t i=1;i<t->nbody;i++) if(!join_body_cmp(&t->body[i],&t->body[i-1]))
        return join_note("body_duplicate,id=%llx",(unsigned long long)t->body[i].portable);
    join_body_cap=t->nbody;return 1;
}
/* Admission: the session's body states on the joiner's bodies. */
static void join_admit_body(const join_tables *s,const join_tables *mine) {
    unsigned rows=0;
    for(uint32_t i=0;i<s->nbody;i++) {
        const join_body_rec *r=&s->body[i];unsigned char *object=join_object(join_runtime_of(mine,r->portable));
        unsigned char *record,*body,*motion;
        if(!object || !join_body_locate(object,r->which,&record,&body,&motion)) {
            ++join_body_missing;
            join_world_row(&rows,"body side=session id=%016llx which=%u why=missing",(unsigned long long)r->portable,r->which);
            continue;
        }
        if(!motion!=!r->pad) {
            ++join_body_shape;
            join_world_row(&rows,"body side=joiner id=%016llx which=%u why=motion joiner=%d session=%u",(unsigned long long)r->portable,r->which,motion!=NULL,r->pad);
            continue;
        }
        join_body_rec own;if(!join_body_read(object,r->which,r->portable,&own)) {++join_body_missing;continue;}
        int changed=0;
        if(memcmp(own.pose,r->pose,32)) {memcpy(body,r->pose,32);++join_body_pose;changed=1;}
        if(motion && memcmp(own.motion,r->motion,JOIN_BODY_MOTION_BYTES)) {memcpy(motion,r->motion,JOIN_BODY_MOTION_BYTES);++join_body_motion;changed=1;}
        if(memcmp(own.pre,r->pre,32) || memcmp(own.delta,r->delta,32)) {memcpy(record+0x40,r->pre,32);memcpy(record+0x60,r->delta,32);++join_body_record;changed=1;}
        if(changed) {
            ++join_body_written;
            join_world_row(&rows,"body side=joiner id=%016llx which=%u why=written pose=%d motion=%d record=%d",(unsigned long long)r->portable,r->which,
                           memcmp(own.pose,r->pose,32)!=0,motion && memcmp(own.motion,r->motion,JOIN_BODY_MOTION_BYTES),
                           memcmp(own.pre,r->pre,32) || memcmp(own.delta,r->delta,32));
        } else ++join_body_equal;
    }
}

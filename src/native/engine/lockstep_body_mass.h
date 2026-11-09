/* Second bodies' mass properties, read only: a census to verify the value native_gate_body1_mass gives these bodies
 * as they are created. Shared by both gates; this census reads, never writes.
 *
 * A ship's two physics bodies are dynamic (Jolt's EMotionType at Body +0x78 = 2; +0x76 is the body type, +0x77 the
 * broadphase layer) and their shape is X4's defined-mass shape (a Jolt decorated shape, type 2 subtype 0x11, Shape
 * +0x18/+0x19) over a mutable compound (type 1 subtype 8, the decorated shape's inner shape at +0x20). The body's user
 * data (+0x50) is its physics record; record +0x3a = 1 marks the second body, whose mass properties are fixed when it is
 * created (the parts' NotifyShapeChanged updates the mass only when +0x3a is 0, as for the first body). Created while
 * the compound is empty, Jolt's SetMassProperties falls back to a sphere of radius 1 at the body's mass: the inverse
 * inertia 2.5 * mInvMass (MotionProperties +0x58) on four lanes (+0x20..+0x2c) and the identity rotation (+0x30..+0x3c,
 * x y z w = 0 0 0 1). Bit-exact words, not float equality. The lane is computed as 2.5f * mInvMass, which matched
 * Windows' real words for the seven fixture ships (W36i, tick 2); should a census ever show differ > 0, first check
 * whether that body's words are another rounding of the same sphere (1 / (0.4 * m), say) before suspecting a race.
 * The 2026-11-09 mixed capture found detailed, non-fallback inertia on Windows too; W36i was not a general guarantee.
 *
 * The census walks every body of a Jolt BodyManager (mBodies: size at +0, elements at +0x10; a free slot holds an odd
 * value) and counts the bodies, the dynamic ones, the fixed-mass bodies of that shape (seen) and, of those, the ones
 * with and without the fallback for their own mass (fallback, differ), and the bodies of that shape whose mass follows
 * their parts (updated: not compared). On Windows differ is 0 when every second body was created empty; on Linux the
 * key keeps it 0. Each includer passes its own checked reader. */
typedef int (*body_mass_read_fn)(const void *source,void *dest,size_t bytes);
typedef struct {uint64_t bodies,dynamic,seen,fallback,differ,updated,unreadable;} body_mass_census_t;
/* 1 when the inverse-inertia and rotation words (MotionProperties +0x20..+0x3c) are the fallback for that inverse mass. */
static int body_mass_is_fallback(const uint32_t words[8],uint32_t inv_mass_bits) {
    float inv_mass,f;uint32_t lane;
    memcpy(&inv_mass,&inv_mass_bits,4);f=2.5f*inv_mass;memcpy(&lane,&f,4);
    return words[0]==lane && words[1]==lane && words[2]==lane && words[3]==lane &&
           words[4]==0u && words[5]==0u && words[6]==0u && words[7]==0x3f800000u;
}
/* The BodyManager behind a ship's physics record (body_state's chain): record +0 the owner, +0x18 the scene, scene
 * +0x20 the physics system, the system's no-lock body interface (Windows +0x1b8, Linux +0x140), its +8 the manager. */
static unsigned char *body_mass_manager(body_mass_read_fn read,const void *object,const unsigned char *record,unsigned iface_offset) {
    unsigned char *scene=NULL,*system=NULL,*iface=NULL,*manager=NULL;const void *owner=NULL;
    if(!record || !read(record,&owner,8) || owner!=object || !read(record+0x18,&scene,8) || !scene ||
       !read(scene+0x20,&system,8) || !system || !read(system+iface_offset,&iface,8) || !iface ||
       !read(iface+8,&manager,8)) return NULL;
    return manager;
}
static int body_mass_census(body_mass_read_fn read,const unsigned char *manager,body_mass_census_t *c) {
    uint64_t count=0;unsigned char *bodies=NULL;
    memset(c,0,sizeof(*c));
    if(!manager || !read(manager,&count,8) || count>0x800000u || !read(manager+0x10,&bodies,8) || (count && !bodies)) return 0;
    for(uint64_t i=0;i<count;i++) {
        unsigned char *body=NULL,*shape=NULL,*inner=NULL,*motion=NULL,*record=NULL,kind=0,fixed=0,ts[2],is[2];uint32_t words[8],inv_mass;
        if(!read(bodies+i*8u,&body,8)) {c->unreadable++;continue;}
        if(!body || ((uintptr_t)body&1u)) continue;
        c->bodies++;
        if(!read(body+0x78,&kind,1)) {c->unreadable++;continue;}
        if(kind!=2) continue; /* EMotionType::Dynamic */
        c->dynamic++;
        if(!read(body+0x40,&shape,8) || !shape || !read(shape+0x18,ts,2)) {c->unreadable++;continue;}
        if(ts[0]!=2 || ts[1]!=0x11) continue;
        if(!read(shape+0x20,&inner,8) || !inner || !read(inner+0x18,is,2)) {c->unreadable++;continue;}
        if(is[0]!=1 || is[1]!=8) continue;
        if(!read(body+0x50,&record,8) || !record || !read(record+0x3a,&fixed,1)) {c->unreadable++;continue;}
        if(fixed!=1) {c->updated++;continue;}
        if(!read(body+0x48,&motion,8) || !motion || !read(motion+0x20,words,32) || !read(motion+0x58,&inv_mass,4)) {c->unreadable++;continue;}
        c->seen++;
        if(body_mass_is_fallback(words,inv_mass)) c->fallback++; else c->differ++;
    }
    return 1;
}
static int body_mass_census_text(const body_mass_census_t *c,char *out,size_t cap) {
    return snprintf(out,cap,"bodies=%llu dynamic=%llu seen=%llu fallback=%llu differ=%llu updated=%llu unreadable=%llu",
        (unsigned long long)c->bodies,(unsigned long long)c->dynamic,(unsigned long long)c->seen,
        (unsigned long long)c->fallback,(unsigned long long)c->differ,(unsigned long long)c->updated,(unsigned long long)c->unreadable);
}

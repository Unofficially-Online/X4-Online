/* The mining group's shared distance core (lockstep_mining_near.h) around the 50 km boundary, in an identity frame
 * and in a moved and rotated one; included by both gates' tests. */
static float tmn_next(float x,int up) {uint32_t u;memcpy(&u,&x,4);u+=up?1u:(uint32_t)-1;memcpy(&x,&u,4);return x;}
static void test_mining_near_core(void) {
    const float identity[16]={0,0,0,0, 1,0,0,0, 0,1,0,0, 0,0,1,0},origin[3]={0,0,0};
    /* 50000 squared is exactly 2.5e9 in float: on the boundary is not near; one ulp inside is, one ulp outside is not. */
    float v[16];memcpy(v,identity,sizeof(v));v[0]=50000.0f;
    assert(rc_mining_dist2(identity,v,origin)==2.5e9f && !rc_mining_is_near(rc_mining_dist2(identity,v,origin)));
    v[0]=tmn_next(50000.0f,0);assert(rc_mining_dist2(identity,v,origin)<2.5e9f && rc_mining_is_near(rc_mining_dist2(identity,v,origin)));
    v[0]=tmn_next(50000.0f,1);assert(rc_mining_dist2(identity,v,origin)>2.5e9f && !rc_mining_is_near(rc_mining_dist2(identity,v,origin)));
    v[0]=0;v[2]=49999.0f;assert(rc_mining_is_near(rc_mining_dist2(identity,v,origin)));
    v[2]=50001.0f;assert(!rc_mining_is_near(rc_mining_dist2(identity,v,origin)));
    /* A frame at (1000, 20, -300) turned a quarter about y (rows (0,0,-1), (0,1,0), (1,0,0)), the position 30 km along
     * the frame's x: a point 30 km + 49999 m along it is near, 30 km + 50001 m is not, and the same offsets across. */
    const float f[16]={1000,20,-300,0, 0,0,-1,0, 0,1,0,0, 1,0,0,0},pos[3]={30000,0,0};
    float w[16];memcpy(w,identity,sizeof(w));
    w[0]=1000;w[1]=20;w[2]=-300-(30000+49999.0f); /* the frame's x axis is world -z */
    assert(rc_mining_is_near(rc_mining_dist2(f,w,pos)));
    w[2]=-300-(30000+50001.0f);assert(!rc_mining_is_near(rc_mining_dist2(f,w,pos)));
    w[2]=-300-30000.0f;w[1]=20+49999.0f;assert(rc_mining_is_near(rc_mining_dist2(f,w,pos)));
    w[1]=20+50001.0f;assert(!rc_mining_is_near(rc_mining_dist2(f,w,pos)));
    /* No candidate: never near. */
    assert(!rc_mining_is_near(-1.0f) && rc_mining_is_near(0.0f));
    printf("mining near core: 50 km boundary (on, one ulp in and out), moved and turned frame PASS\n");
}

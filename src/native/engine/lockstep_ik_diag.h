/* native_gate_ik_diag (diagnostic, read only, outside the digest): the skeleton IK joints of a fixture ship and of its
 * weapons, as IKJOINT rows in one text on both gates (linux/gate_ik_diag.h).
 *
 * Why: the battle's tick-28 split against Linux is a bullet hit (W36k against lx51a: the second impulse of the tick,
 * from the bullet hit path near 0xd5360e, has another point and direction). A bullet leaves along its gun's frame, and a
 * fighter's gun frame comes through the weapon's skeleton (frame slot 0x1ec8 = 0x81c960 -> 0xe22b70 -> 0xe20370), whose
 * gimbal joints move toward the aim target at a limited rate (0xe1f840). If those joints part between the platforms,
 * these rows show when.
 *
 * gate.ik_joints(runtime): the object's joints through the hot join's walk (lockstep_join_world.h join_ikv_walk: +0x208
 * -> +0x48, groups at +0x10, 0x680-byte joints, each checked against the vbtable 0x2c450c0), per joint
 * "group,index,value,time,rate,target" (the +0x08 float, the +0x10 double, the +0x18 and +0x1c floats, as hex words),
 * space separated; "" without a skeleton; "refused why=W seen=S" when the walk refuses the skeleton; nil without the
 * object. The probe turns each joint into "IKJOINT id=<actor> obj=<ship|w<slot>> tick=<t> group=<g> index=<i>
 * value=<hex> time=<hex> rate=<hex> target=<hex>". */
#define IKD_TEXT 65536u
static char ikd_text[IKD_TEXT];
static size_t ikd_used;
static int ikd_failed;
static void ikd_visit(void *ctx,uint16_t group,uint16_t joint,unsigned char *at) {
    (void)ctx;uint32_t value=0,rate=0,target=0;uint64_t time=0;
    if(ikd_failed) return;
    if(!join_read(at+0x08,&value,4) || !join_read(at+0x10,&time,8) || !join_read(at+0x18,&rate,4) || !join_read(at+0x1c,&target,4)) {ikd_failed=1;return;}
    const int n=snprintf(ikd_text+ikd_used,IKD_TEXT-ikd_used,"%s%u,%u,%08x,%016llx,%08x,%08x",ikd_used?" ":"",(unsigned)group,(unsigned)joint,
        value,(unsigned long long)time,rate,target);
    if(n<0 || (size_t)n>=IKD_TEXT-ikd_used) {ikd_failed=1;return;}
    ikd_used+=(size_t)n;
}
/* 1 walked (ikd_text holds the joints), 0 no skeleton, -1 refused (join_ikv_why/seen), -2 a joint unreadable or the text full. */
static int ikd_joints(unsigned char *object) {
    ikd_used=0;ikd_failed=0;ikd_text[0]=0;
    const int walked=join_ikv_walk(object,ikd_visit,NULL);
    if(walked<0) return -1;
    return ikd_failed?-2:walked;
}
static int lua_ik_joints(lua_State *L) {
    const double number=p_lua_tonumber(L,1);
    if(!accept_ui() || !isfinite(number) || number<1 || number>2199023255551.0 || floor(number)!=number) {p_lua_pushnil(L);return 1;}
    unsigned char *object=join_object((uint64_t)number);
    if(!object) {p_lua_pushnil(L);return 1;}
    const int walked=ikd_joints(object);char out[96];
    if(walked==-1) {const int n=snprintf(out,sizeof(out),"refused why=%u seen=%llx",join_ikv_why,(unsigned long long)join_ikv_seen);p_lua_pushlstring(L,out,(size_t)n);}
    else if(walked==-2) p_lua_pushlstring(L,"failed",6);
    else p_lua_pushlstring(L,ikd_text,ikd_used);
    return 1;
}

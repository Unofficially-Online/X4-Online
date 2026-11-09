/* native_gate_ik_diag on the Linux gate (diagnostic, read only, outside the digest): Windows' lockstep_ik_diag.h, the
 * skeleton IK joints of a fixture ship and of its weapons, in the same text. Included by gate_linux.c.
 *
 * Linux layout (the skeleton destructor at 0x135b235 walks it): the object's attachment container +0x208, its skeleton
 * +0x48, the group vector at skeleton +0x10..+0x18 ({begin, end, cap}, 0x18-byte groups, each {begin, end, cap} of
 * joints), joints 0x668 bytes apart (Windows 0x680: MSVC's vbtable pointer and the virtual base's own), each with its vtable
 * pointer at +0 (destroyed through slot 0). The four words are read at Windows' offsets, +0x08 value, +0x10 time (a
 * double), +0x18 rate, +0x1c target; gate.ik_status() names the first joint's vtable and that the time word looks like a
 * game time, so a layout that differs shows itself. Limits as Windows: 16 groups, 64 joints in a group. */
#define LXIK_CONTAINER 0x208u
#define LXIK_SKELETON 0x48u
#define LXIK_GROUP_VEC 0x10u
#define LXIK_JOINT 0x668u
#define LXIK_GROUPS 16u
#define LXIK_JOINTS 64u
#define LXIK_TEXT 65536u
static char lxik_text[LXIK_TEXT];
static size_t lxik_used;
static unsigned lxik_why;static uintptr_t lxik_seen;
static uint64_t lxik_walks,lxik_joints_seen,lxik_vptr;   /* the first joint's vtable (Linux VA) */
static int lxik_walk(unsigned char *object) {
    unsigned char *container=NULL,*skeleton=NULL;uintptr_t gv[2]={0,0};
    lxik_used=0;lxik_text[0]=0;
    if(!object || !gate_read_local(object+LXIK_CONTAINER,&container,8) || !container ||
       !gate_read_local(container+LXIK_SKELETON,&skeleton,8) || !skeleton) return 0;
    if(!gate_read_local(skeleton+LXIK_GROUP_VEC,gv,16)) {lxik_why=1;lxik_seen=0;return -1;}
    if(gv[0]==gv[1]) return 0;
    if(gv[1]<gv[0] || (gv[1]-gv[0])%0x18u || (gv[1]-gv[0])/0x18u>LXIK_GROUPS) {lxik_why=2;lxik_seen=gv[1]-gv[0];return -1;}
    for(int pass=0;pass<2;pass++)   /* the first pass checks the whole skeleton, the second writes */
        for(uintptr_t g=gv[0];g<gv[1];g+=0x18u) {
            uintptr_t jb[2]={0,0};
            if(!gate_read_local((void*)g,jb,16) || jb[1]<jb[0] || (jb[1]-jb[0])%LXIK_JOINT || (jb[1]-jb[0])/LXIK_JOINT>LXIK_JOINTS)
                {lxik_why=3;lxik_seen=jb[1]-jb[0];return -1;}
            for(uintptr_t j=jb[0];j<jb[1];j+=LXIK_JOINT) {
                uint64_t vptr=0;uint32_t value=0,rate=0,target=0;uint64_t time=0;
                if(!gate_read_local((void*)j,&vptr,8) || !vptr) {lxik_why=4;lxik_seen=0;return -1;}
                if(!pass) continue;
                if(!lxik_vptr) lxik_vptr=(uint64_t)(uintptr_t)((unsigned char*)(uintptr_t)vptr-image)+LX_IMAGE_BASE;
                if(!gate_read_local((unsigned char*)j+0x08,&value,4) || !gate_read_local((unsigned char*)j+0x10,&time,8) ||
                   !gate_read_local((unsigned char*)j+0x18,&rate,4) || !gate_read_local((unsigned char*)j+0x1c,&target,4)) return -2;
                const int n=snprintf(lxik_text+lxik_used,LXIK_TEXT-lxik_used,"%s%u,%u,%08x,%016llx,%08x,%08x",lxik_used?" ":"",
                    (unsigned)((g-gv[0])/0x18u),(unsigned)((j-jb[0])/LXIK_JOINT),value,(unsigned long long)time,rate,target);
                if(n<0 || (size_t)n>=LXIK_TEXT-lxik_used) return -2;
                lxik_used+=(size_t)n;lxik_joints_seen++;
            }
        }
    lxik_walks++;
    return 1;
}
static unsigned char *lxik_object(uint64_t runtime) {
    lxw_find_fn find=world_find?world_find:(lxw_find_fn)(void*)LX(LX_FIND_VA);void *map=NULL;uint64_t id=0;
    if(!gate_read_local(world_component_map?(void*)world_component_map:(void*)LX(LX_COMPONENT_MAP_VA),&map,8) || !map || !runtime) return NULL;
    unsigned char *object=find(map,runtime,4,0);
    return object && gate_read_local(object+8,&id,8) && id==runtime?object:NULL;
}
static int lua_ik_joints(lua_State *L) {
    const double number=p_lua_tonumber(L,1);
    if(!accept_ui() || !isfinite(number) || number<1 || number>2199023255551.0 || floor(number)!=number) {p_lua_pushnil(L);return 1;}
    unsigned char *object=lxik_object((uint64_t)number);
    if(!object) {p_lua_pushnil(L);return 1;}
    const int walked=lxik_walk(object);char out[96];
    if(walked==-1) {const int n=snprintf(out,sizeof(out),"refused why=%u seen=%llx",lxik_why,(unsigned long long)lxik_seen);p_lua_pushlstring(L,out,(size_t)n);}
    else if(walked==-2) p_lua_pushlstring(L,"failed",6);
    else p_lua_pushlstring(L,lxik_text,lxik_used);
    return 1;
}
static int lua_ik_status(lua_State *L) {
    char out[160];const int n=snprintf(out,sizeof(out),"walks=%llu joints=%llu vtable=%llx last_why=%u",(unsigned long long)lxik_walks,
        (unsigned long long)lxik_joints_seen,(unsigned long long)lxik_vptr,lxik_why);
    if(!accept_ui() || n<=0 || (size_t)n>=sizeof(out)) {p_lua_pushnil(L);return 1;}
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}

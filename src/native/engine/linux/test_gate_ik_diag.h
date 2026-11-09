/* native_gate_ik_diag (gate_ik_diag.h): a planted object, container, skeleton and two groups of 0x668-byte joints: the
 * joint text in Windows' form, no skeleton, a joint without a vtable refused, the first joint's vtable in the status.
 * Included by test_gate_linux.c. */
static void test_ik_diag_linux(void) {
    static unsigned char object[0x300] __attribute__((aligned(16))),container[0x60],skeleton[0x40],joints[3][LXIK_JOINT] __attribute__((aligned(16)));
    memset(object,0,sizeof(object));memset(container,0,sizeof(container));memset(skeleton,0,sizeof(skeleton));memset(joints,0,sizeof(joints));
    uintptr_t groups[2][3];
    groups[0][0]=(uintptr_t)joints[0];groups[0][1]=(uintptr_t)joints[2];groups[0][2]=groups[0][1];
    groups[1][0]=(uintptr_t)joints[2];groups[1][1]=(uintptr_t)joints[2]+LXIK_JOINT;groups[1][2]=groups[1][1];
    void *p=container;memcpy(object+LXIK_CONTAINER,&p,8);p=skeleton;memcpy(container+LXIK_SKELETON,&p,8);
    uintptr_t vec[2]={(uintptr_t)groups[0],(uintptr_t)groups[2]};memcpy(skeleton+LXIK_GROUP_VEC,vec,16);
    for(unsigned j=0;j<3;j++) {
        const void *vt=LX(0x3b00010u);memcpy(joints[j],&vt,8);
        const float value=0.25f*(float)(j+1),rate=1.5f,target=-0.5f;const double time=38.5+j;
        memcpy(joints[j]+0x08,&value,4);memcpy(joints[j]+0x10,&time,8);memcpy(joints[j]+0x18,&rate,4);memcpy(joints[j]+0x1c,&target,4);
    }
    lxik_walks=lxik_joints_seen=lxik_vptr=0;
    assert(lxik_walk(object)==1 && lxik_walks==1 && lxik_joints_seen==3 && lxik_vptr==0x3b00010u);
    assert(!strcmp(lxik_text,"0,0,3e800000,4043400000000000,3fc00000,bf000000 0,1,3f000000,4043c00000000000,3fc00000,bf000000 "
                             "1,0,3f400000,4044400000000000,3fc00000,bf000000"));
    {static unsigned char none[0x300];assert(lxik_walk(none)==0 && lxik_used==0);}
    {memset(joints[1],0,8);assert(lxik_walk(object)==-1 && lxik_why==4 && lxik_used==0);const void *vt=LX(0x3b00010u);memcpy(joints[1],&vt,8);}
    groups[0][1]=(uintptr_t)joints[0]+0x680;assert(lxik_walk(object)==-1 && lxik_why==3);groups[0][1]=(uintptr_t)joints[2]; /* Windows' stride refused */
    assert(lxik_walk(object)==1);
    lxik_walks=lxik_joints_seen=lxik_vptr=0;
    printf("ik diag: the joints of a planted skeleton in Windows' text, none, a joint without a vtable and another stride refused PASS\n");
}

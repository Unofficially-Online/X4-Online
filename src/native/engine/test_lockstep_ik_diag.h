/* native_gate_ik_diag (lockstep_ik_diag.h): a planted object, container, skeleton and two groups of joints through the
 * hot join's walk: the joint text (group, index, value, time, rate, target), no skeleton, a joint with a foreign vbtable
 * refused. */
static void test_ik_diag(void) {
    _Alignas(16) static unsigned char object[0x300],container[0x60],skeleton[0x40],joints[3][0x680];
    memset(object,0,sizeof(object));memset(container,0,sizeof(container));memset(skeleton,0,sizeof(skeleton));memset(joints,0,sizeof(joints));
    uintptr_t groups[2][3];
    groups[0][0]=(uintptr_t)joints[0];groups[0][1]=(uintptr_t)joints[2];groups[0][2]=groups[0][1];   /* group 0: joints 0 and 1 */
    groups[1][0]=(uintptr_t)joints[2];groups[1][1]=(uintptr_t)joints[2]+0x680;groups[1][2]=groups[1][1]; /* group 1: joint 2 */
    void *p=container;memcpy(object+0x208,&p,8);p=skeleton;memcpy(container+0x48,&p,8);
    uintptr_t vec[2]={(uintptr_t)groups[0],(uintptr_t)groups[2]};memcpy(skeleton+0x10,vec,16);
    for(unsigned j=0;j<3;j++) {
        const uintptr_t vbt=(uintptr_t)image+JOIN_IKV_VBT;memcpy(joints[j],&vbt,8);
        const float value=0.25f*(float)(j+1),rate=1.5f,target=-0.5f;const double time=38.5+j;
        memcpy(joints[j]+0x08,&value,4);memcpy(joints[j]+0x10,&time,8);memcpy(joints[j]+0x18,&rate,4);memcpy(joints[j]+0x1c,&target,4);
    }
    assert(ikd_joints(object)==1);
    assert(!strcmp(ikd_text,"0,0,3e800000,4043400000000000,3fc00000,bf000000 0,1,3f000000,4043c00000000000,3fc00000,bf000000 "
                            "1,0,3f400000,4044400000000000,3fc00000,bf000000"));
    {unsigned char none[0x300];memset(none,0,sizeof(none));assert(ikd_joints(none)==0 && ikd_used==0);}  /* no container */
    {const uintptr_t other=(uintptr_t)image+JOIN_IKV_VBT+8;memcpy(joints[1],&other,8);
     assert(ikd_joints(object)==-1 && join_ikv_why==4 && ikd_used==0);                                     /* refused before any text */
     const uintptr_t vbt=(uintptr_t)image+JOIN_IKV_VBT;memcpy(joints[1],&vbt,8);}
    assert(ikd_joints(object)==1 && ikd_used>0);
    printf("ik diag: the joints of a planted skeleton (group, index, value, time, rate, target), none, a foreign vbtable refused PASS\n");
}

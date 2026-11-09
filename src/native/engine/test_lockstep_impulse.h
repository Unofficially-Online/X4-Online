/* native_gate_impulse_diag (lockstep_impulse.h): a planted fixture ship and a fake Body and MotionProperties through the
 * hook (the body by its record, the impulse, the point, the pose and motion before, the velocities after; another body,
 * a tick outside the window and another thread pass through untraced; the row in the Linux gate's text); the trampoline
 * code and its unwind data; on the executable's bytes the pin (tamper refused), the window/state refusals, the entry
 * armed and owned, the view keeping the pin, a foreign entry failing the tick check, restored. The hook's call into
 * the game is not executed here. */
static unsigned char twai_motion_after_x;
static int twai_native_calls;
static void twai_native(void *body,const void *impulse,const void *point) {
    (void)point;twai_native_calls++;unsigned char *motion=NULL;memcpy(&motion,(unsigned char*)body+0x48,8);
    float v[4],j[4];memcpy(v,motion,16);memcpy(j,impulse,16);for(unsigned i=0;i<3;i++) v[i]+=j[i];memcpy(motion,v,16);
    twai_motion_after_x=motion[0];
}
static void test_impulse_diag(void) {
    _Alignas(16) static unsigned char object[0x300],record0[0x40],record1[0x40],body[0x80],motion[0x80],other_body[0x80],other_record[0x40];
    memset(object,0,sizeof(object));memset(body,0,sizeof(body));memset(motion,0,sizeof(motion));memset(other_body,0,sizeof(other_body));
    unsigned char *saved_component=actor_components[1];const uint64_t saved_id=actor_ids[1];
    const uint32_t sa=stats.active;const uint64_t si=stats.issued;const DWORD su=ui_thread;
    const uint64_t id=0x00000000000b0002ull;memcpy(object+8,&id,8);actor_components[1]=object;actor_ids[1]=id;
    void *p=record0;memcpy(object+0x260,&p,8);p=record1;memcpy(object+0x268,&p,8);
    p=record1;memcpy(body+0x50,&p,8);p=motion;memcpy(body+0x48,&p,8);
    p=other_record;memcpy(other_body+0x50,&p,8);p=motion;memcpy(other_body+0x48,&p,8);
    for(unsigned i=0;i<0x20;i++) body[i]=(unsigned char)(0x10+i);
    for(unsigned i=0;i<0x60;i++) motion[i]=(unsigned char)(0x40+i);
    {const float zero[4]={0};memcpy(motion,zero,16);}
    unsigned char motion_before[0x60];memcpy(motion_before,motion,0x60);
    memset(wai_traces,0,sizeof(wai_traces));wai_calls=wai_recorded=wai_failures=wai_seq=0;
    stats.active=1;stats.issued=28;ui_thread=GetCurrentThreadId();wai_from=27;wai_to=28;wai_original_override=twai_native;
    _Alignas(16) const float impulse[4]={1.0f,2.0f,3.0f,0.0f},point[4]={-394.5f,36000.0f,600.0f,0.0f};
    unsigned actor=0,which=9;assert(wai_record_of(body,&actor,&which) && actor==2 && which==1);
    assert(!wai_record_of(other_body,&actor,&which));
    wai_hook(body,impulse,point);
    assert(twai_native_calls==1 && wai_calls==1 && wai_recorded==1 && !wai_failures && wai_traces[1].tick==28 && wai_traces[1].count==1);
    const wai_record *r=&wai_traces[1].records[0];
    assert(r->which==1 && r->seq==1 && r->source && !memcmp(r->impulse,impulse,16) && !memcmp(r->point,point,16));
    assert(!memcmp(r->pose,body,32) && !memcmp(r->motion,motion_before,0x60));
    {float after[4];memcpy(after,r->after,16);assert(after[0]==1.0f && after[1]==2.0f && after[2]==3.0f);}
    wai_hook(other_body,impulse,point);assert(twai_native_calls==2 && wai_recorded==1);              /* another body */
    stats.issued=29;wai_hook(body,impulse,point);assert(twai_native_calls==3 && wai_recorded==1);     /* outside the window */
    stats.issued=28;ui_thread=GetCurrentThreadId()+1;wai_hook(body,impulse,point);assert(twai_native_calls==4 && wai_recorded==1);
    ui_thread=GetCurrentThreadId();
    char text[1024];
    assert(wai_text(2,1,text,sizeof(text))>0 && !strncmp(text,"which=1 tick=28 seq=1 source=",29));
    assert(strstr(text," impulse=3f8000004000000040400000") && strstr(text," after=3f8000004000000040400000") && strstr(text," caller="));
    assert(!wai_text(2,2,text,sizeof(text)) && !wai_text(1,1,text,sizeof(text)) && !wai_text(13,1,text,sizeof(text)));
    /* The trampoline: the two relocated instructions, the jump back to +10; the unwind info of the 0xd8 allocation. */
    {unsigned char code[WAI_CODE];unsigned char *prior=wai_page;wai_page=(unsigned char*)code;wai_code(code);
     assert(code[WAI_STUB]==0xff && code[WAI_STUB+1]==0x25 && !memcmp(code+WAI_TRAMP,wai_relocated,10) && code[WAI_TRAMP+10]==0xff && code[WAI_TRAMP+11]==0x25);
     const void *back;memcpy(&back,code+WAI_TRAMP+16,8);assert(back==(const void*)(image+WAI_RESUME));
     const void *hook;memcpy(&hook,code+WAI_STUB+6,8);assert(hook==(const void*)wai_hook);
     assert(!memcmp(code+WAI_UNWIND,"\x01\x0a\x02\x00\x0a\x01\x1b\x00",8) && WAI_TRAMP+WAI_TRAMP_SIZE<=WAI_UNWIND && 0xd8/8==27);
     wai_page=prior;}
    wai_original_override=NULL;twai_native_calls=0;memset(wai_traces,0,sizeof(wai_traces));wai_calls=wai_recorded=wai_failures=wai_seq=0;
    wai_from=wai_to=0;actor_components[1]=saved_component;actor_ids[1]=saved_id;stats.active=sa;stats.issued=si;ui_thread=su;
    {char out[300];assert(wai_status(out,sizeof(out))>0 && strstr(out," installed=0 restored=0 ") && strstr(out," stage=idle window=0:0"));}
    printf("impulse diag: a fixture body by its record, the impulse/point/pose/motion before and velocities after, other bodies/ticks/threads untraced, the row, the trampoline and its unwind PASS\n");
}
static void test_impulse_diag_native_image(const char *path) {
    unsigned char *prior=image,*saved=wai_page;image=test_pmath_map(path);wai_page=NULL;
    assert(wai_pins() && wai_state()==0 && !memcmp(image+WAI_ENTRY,wai_relocated,10));
    image[WAI_ENTRY+0x18]^=1;assert(!wai_pins());image[WAI_ENTRY+0x18]^=1;
    /* the facts the capture reads: rcx+0x48 the motion properties, [rdx] the impulse, [r8] the point, rcx+0x10 the rotation */
    assert(!memcmp(image+WAI_ENTRY+10,"\x4c\x8b\x49\x48\x4c\x8b\xd1",7));
    const uint32_t sa=stats.active;const uint64_t si=stats.issued;const DWORD su=ui_thread;const int sb=boot_mode;
    stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();boot_mode=0;
    assert(!wai_arm(0,3) && !strcmp(wai_stage,"window"));
    stats.issued=4;assert(!wai_arm(27,28) && !strcmp(wai_stage,"state"));stats.issued=0;
    assert(wai_arm(27,28) && wai_installed && wai_state()==1 && wai_owned() && wai_check() && !strcmp(wai_stage,"armed"));
    assert(wai_arm(27,28) && wai_installed && !strcmp(wai_stage,"armed"));     /* again, same window: a reload, nothing changes */
    assert(!wai_arm(27,29) && !wai_arm(26,28) && wai_installed && wai_from==27 && wai_to==28 && !strcmp(wai_stage,"armed")); /* another window */
    assert(wai_pins() && image[WAI_ENTRY]==0xe9);               /* the view keeps the pin */
    {DWORD64 base=0;assert(RtlLookupFunctionEntry((DWORD64)(uintptr_t)(wai_page+WAI_TRAMP+12),&base,NULL)==&wai_unwind_function);}
    {char out[300];assert(wai_status(out,sizeof(out))>0 && strstr(out," installed=1 restored=0 ") && strstr(out," stage=armed window=27:28"));}
    {   unsigned char ours[5];memcpy(ours,image+WAI_ENTRY,5);unsigned char other[5]={0xe9,1,0,0,0};
        assert(view_cas_bytes(WAI_ENTRY,ours,other,5) && !wai_owned() && !wai_check() && !strcmp(wai_stage,"site_changed"));
        assert(view_cas_bytes(WAI_ENTRY,other,ours,5) && wai_owned() && !wai_check());wai_failures=0;assert(wai_check());
    }
    wai_restore();assert(!wai_installed && wai_restored && wai_state()==0 && !strcmp(wai_stage,"restored") && !wai_any && wai_pins());
    stats.active=sa;stats.issued=si;ui_thread=su;boot_mode=sb;
    assert(RtlDeleteFunctionTable(&wai_unwind_function));wai_unwind_added=0; /* the test frees the page; the gate never does */
    VirtualFree(wai_page,0,MEM_RELEASE);wai_page=saved;wai_restored=0;wai_stage="idle";wai_from=wai_to=0;
    wai_calls=wai_recorded=wai_failures=wai_seq=0;
    VirtualFree(image,0,MEM_RELEASE);image=prior;
    printf("impulse diag native image: Body::AddImpulse 0x1405910 pinned (tamper refused), window/state refusals, armed/owned "
           "(the view keeps the pin, the trampoline's unwind entry found), a foreign entry fails the tick check, restored PASS\n");
}

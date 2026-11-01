/* native_gate_wreck_watch: the LinearDecelerationController fields, frame rows, write rows, entry swaps and pins. */
static __declspec(align(16)) unsigned char test_ww_object[0x300],test_ww_nav[0x100],test_ww_ldc[0xc0],test_ww_ldc2[0xc0],test_ww_table[0x10],test_ww_record[0x80];
static void *test_ww_lookup(uint64_t runtime) {return runtime==0x6c6ad?test_ww_object:NULL;}
static void test_ww_fill_ldc(unsigned char *ctrl,unsigned char seed) {
    memset(ctrl,0,0xc0);void *vt=image+WRECK_LDC_VTABLE;memcpy(ctrl,&vt,8);
    for(unsigned i=0x20;i<0xa9;i++) ctrl[i]=(unsigned char)(seed+i);
    void *table=test_ww_table;memcpy(ctrl+0x10,&table,8);
}
static void test_wreck_watch(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;
    motion_watch_lookup_fn prior_lookup=motion_watch_lookup;unsigned prior_count=motion_watch_count,prior_armed=motion_watch_armed;
    uint64_t prior_saved=motion_watch_saved[0],prior_runtime=motion_watch_runtime[0];
    stats.active=1;ui_thread=GetCurrentThreadId();motion_watch_lookup=test_ww_lookup;
    motion_watch_count=1;motion_watch_saved[0]=0x176ad5;motion_watch_runtime[0]=0x6c6ad;
    /* The LDC's own fields: transform +0x20..+0x60, start +0x60, velocities +0x80..+0xa0, duration +0xa0, flag +0xa8. */
    int32_t vbase_offset=0xa8;memset(test_ww_table,0,sizeof(test_ww_table));memcpy(test_ww_table+4,&vbase_offset,4);
    test_ww_fill_ldc(test_ww_ldc,0);unsigned char state[WRECK_STATE];
    assert(wreck_is_ldc(test_ww_ldc) && wreck_state(test_ww_ldc,state));
    assert(state[0]==0x20 && state[0x3f]==0x5f && state[0x40]==0x60 && state[0x47]==0x67 && state[0x48]==0x80 && state[0x67]==0x9f);
    assert(state[0x68]==0xa0 && state[0x6f]==0xa7 && state[0x70]==0xa8);
    assert(wreck_vbase_of(test_ww_ldc)==test_ww_ldc+0xb8 && !wreck_vbase_of(NULL));
    memset(test_ww_nav,0,sizeof(test_ww_nav));void *vt=image+MOTION_WATCH_NAV_VTABLE;memcpy(test_ww_nav,&vt,8);
    assert(!wreck_is_ldc(test_ww_nav) && !wreck_is_ldc(NULL) && !wreck_state(NULL,state) && !state[0]);
    /* Frame rows: a changed controller once, an LDC every frame; the physics record's velocities and delta. */
    static wreck_row_t ring[8];wreck_row_t *saved_ring=wreck_ring;LONG64 saved_rows=wreck_rows;
    memset(ring,0,sizeof(ring));wreck_ring=ring;wreck_rows=0;wreck_armed=1;
    memset(test_ww_object,0,sizeof(test_ww_object));uint64_t id=0x6c6ad;memcpy(test_ww_object+8,&id,8);
    int attention=9;memcpy(test_ww_object+0x1ec,&attention,4);void *p=test_ww_nav;memcpy(test_ww_object+0x200,&p,8);
    memset((void*)wreck_object,0,sizeof(wreck_object));memset((void*)wreck_ctrl,0,sizeof(wreck_ctrl));memset((void*)wreck_vbase,0,sizeof(wreck_vbase));
    memset(wreck_seq,0,sizeof(wreck_seq));
    stats.issued=2464;assert(wreck_frame(2464) && wreck_rows==1 && ring[0].seq==1 && ring[0].ctrl_vt==MOTION_WATCH_NAV_VTABLE && ring[0].flags==0);
    char out[1024];assert(wreck_row(0,out,sizeof(out))>0 &&
        !strcmp(out,"index=0 tick=2464 saved=0000000000176ad5 attention=9 ctrl=02c51158 seq=1 ldc=- body=-"));
    stats.issued=2465;assert(wreck_frame(2465) && wreck_rows==1); /* same NavigationController: nothing */
    attention=2;memcpy(test_ww_object+0x1ec,&attention,4);p=test_ww_ldc;memcpy(test_ww_object+0x200,&p,8);
    for(unsigned i=0;i<0x40;i++) test_ww_record[0x40+i]=(unsigned char)(0xc0+i);p=test_ww_record;memcpy(test_ww_object+0x260,&p,8);
    stats.issued=2466;assert(wreck_frame(2466) && wreck_rows==2 && ring[1].seq==2 && ring[1].flags==3 && wreck_ctrl[0]==test_ww_ldc && wreck_vbase[0]==test_ww_ldc+0xb8);
    assert(wreck_row(1,out,sizeof(out))>0 && strstr(out,"index=1 tick=2466 saved=0000000000176ad5 attention=2 ctrl=02c3f5f0 seq=2 ldc=202122") &&
           strstr(out,"a6a7a8 body=c0c1c2") && strstr(out,"fdfeff") && !strstr(out,"a6a7a8a9"));
    memset(test_ww_object+0x260,0,8);stats.issued=2467;assert(wreck_frame(2467) && wreck_rows==3 && ring[2].seq==2 && ring[2].flags==1);
    assert(wreck_row(2,out,sizeof(out))>0 && strstr(out," body=-"));
    ring[2].ready=0;assert(!wreck_row(2,out,sizeof(out)));ring[2].ready=3; /* still being written */
    assert(!wreck_row(3,out,sizeof(out)));
    /* Frames refuse off the MoveWorker or for another tick; nothing armed: nothing to do. */
    assert(!wreck_frame(2466));ui_thread=GetCurrentThreadId()+1;assert(!wreck_frame(2467));ui_thread=GetCurrentThreadId();
    wreck_armed=0;assert(wreck_frame(2467) && wreck_rows==3);wreck_armed=1;
    /* The ship gone: the cache clears, no row. */
    id=0x6c6ae;memcpy(test_ww_object+8,&id,8);assert(wreck_frame(2467) && wreck_rows==3 && !wreck_ctrl[0] && !wreck_object[0]);
    id=0x6c6ad;memcpy(test_ww_object+8,&id,8);assert(wreck_frame(2467) && wreck_rows==4 && ring[3].seq==3);
    /* Write rows: only the watched controllers (pointer compare), before and after the original, nesting depth. */
    static wreck_write_t writes[8];wreck_write_t *saved_writes=wreck_write_ring;LONG64 saved_count=wreck_writes;
    memset(writes,0,sizeof(writes));wreck_write_ring=writes;wreck_writes=0;
    assert(wreck_index_ctrl(test_ww_ldc)==0 && wreck_index_ctrl(test_ww_nav)<0 && wreck_index_vbase(test_ww_ldc+0xb8)==0 && wreck_index_object(test_ww_object)==0);
    float velocity[8]={1,2,3,0,0.25f,0.5f,0.75f,0};wreck_pending outer,inner,none;
    assert(!wreck_begin(&none,WRECK_SET_VELOCITY,wreck_index_ctrl(test_ww_nav),test_ww_nav,velocity,32) && !wreck_writes);wreck_end(&none,test_ww_nav);
    assert(wreck_begin(&outer,WRECK_PHYSICS,wreck_index_ctrl(test_ww_ldc),test_ww_ldc,velocity,32) && writes[0].depth==0 && !writes[0].ready);
    assert(wreck_begin(&inner,WRECK_SET_VELOCITY,wreck_index_ctrl(test_ww_ldc),test_ww_ldc,velocity,32) && writes[1].depth==1);
    memcpy(test_ww_ldc+0x80,velocity,32); /* what slot 8 writes */
    wreck_end(&inner,test_ww_ldc);wreck_end(&outer,test_ww_ldc);assert(wreck_depth==0 && writes[0].ready==1 && writes[1].ready==2);
    assert(writes[0].flags==7 && writes[0].saved==0x176ad5 && writes[0].kind==WRECK_PHYSICS && writes[0].ui==1 && writes[0].tick==2467);
    assert(writes[0].before[0x48]==0x80 && !memcmp(writes[0].after+0x48,velocity,32));
    assert(wreck_write_row(0,out,sizeof(out))>0 && strstr(out,"index=0 tick=2467 saved=0000000000176ad5 kind=physics depth=0 ui=1 now=") &&
           strstr(out," arg=0000803f000000400000404000000000") && strstr(out," before=202122") && strstr(out," after=202122"));
    assert(wreck_write_row(1,out,sizeof(out))>0 && strstr(out," kind=set_velocity depth=1 "));
    /* A force carries its step; a transform its time; a new controller its vtable, before the old and after the new one. */
    float step=1.0f/30;assert(wreck_begin(&outer,WRECK_FORCE,0,test_ww_ldc,velocity,32));memcpy(outer.row->arg+32,&step,4);wreck_end(&outer,test_ww_ldc);
    assert(wreck_write_row(2,out,sizeof(out))>0 && strstr(out," kind=force ") && strstr(out,"000000008988083d before="));
    test_ww_fill_ldc(test_ww_ldc2,0x10);
    assert(wreck_begin(&outer,WRECK_SET_CONTROLLER,0,test_ww_nav,NULL,0));outer.row->arg_vt=WRECK_LDC_VTABLE;
    wreck_track(0,test_ww_object,test_ww_ldc2);wreck_end(&outer,test_ww_ldc2);
    assert(wreck_ctrl[0]==test_ww_ldc2 && wreck_seq[0]==4 && wreck_vbase[0]==test_ww_ldc2+0xb8 && wreck_index_ctrl(test_ww_ldc)<0);
    assert(wreck_write_row(3,out,sizeof(out))>0 && strstr(out," kind=set_controller ") && strstr(out," arg=02c3f5f0 before=- after=303132"));
    writes[3].ready=0;assert(!wreck_write_row(3,out,sizeof(out)));writes[3].ready=4;
    wreck_writes=WRECK_WRITE_RING+2;assert(wreck_write_row(0,out,sizeof(out))>0 && !strcmp(out,"index=0 evicted=1"));wreck_writes=4;
    char status[256];assert(wreck_status(status,sizeof(status))>0 && !strcmp(status,"armed=1 swapped=0 rows=4 writes=4 misses=0"));
    /* Entries: swap, ownership, restore (the plain image); arming refuses on the zero image (pins) and without the watch. */
    wreck_armed=0;stats.issued=0;
    assert(!wreck_pins() && !wreck_arm());
    motion_watch_armed=0;assert(!wreck_arm());motion_watch_armed=prior_armed;
    for(unsigned k=0;k<WRECK_ENTRIES;k++) *(void**)(image+wreck_entries[k].entry)=image+wreck_entries[k].target;
    for(unsigned k=0;k<WRECK_ENTRIES;k++) {assert(wreck_swap(k,wreck_original(k),wreck_hook(k)));wreck_swapped|=1u<<k;}
    assert(wreck_owned());*(void**)(image+wreck_entries[2].entry)=(void*)0x1234;assert(!wreck_owned());
    *(void**)(image+wreck_entries[2].entry)=wreck_hook(2);assert(wreck_owned());
    wreck_restore();assert(!wreck_swapped && wreck_owned());
    for(unsigned k=0;k<WRECK_ENTRIES;k++) {assert(*(void**)(image+wreck_entries[k].entry)==wreck_original(k));*(void**)(image+wreck_entries[k].entry)=NULL;}
    wreck_ring=saved_ring;wreck_rows=saved_rows;wreck_write_ring=saved_writes;wreck_writes=saved_count;wreck_armed=0;
    memset((void*)wreck_object,0,sizeof(wreck_object));memset((void*)wreck_ctrl,0,sizeof(wreck_ctrl));memset((void*)wreck_vbase,0,sizeof(wreck_vbase));
    memset(wreck_seq,0,sizeof(wreck_seq));
    motion_watch_lookup=prior_lookup;motion_watch_count=prior_count;motion_watch_saved[0]=prior_saved;motion_watch_runtime[0]=prior_runtime;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;
    printf("wreck watch state/rows/writes/swap tests PASS\n");
}
static void test_wreck_watch_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    /* Each entry holds its target (unrelocated file image: absolute VAs), the targets and the LDC layout are pinned. */
    for(unsigned k=0;k<WRECK_ENTRIES;k++) assert(*(uint64_t*)(image+wreck_entries[k].entry)==0x140000000ULL+wreck_entries[k].target);
    assert(wreck_pins());
    image[0xe23e10]^=1;assert(!wreck_pins());image[0xe23e10]^=1;
    image[WRECK_LAYOUT+0x80]^=1;assert(!wreck_pins());image[WRECK_LAYOUT+0x80]^=1;assert(wreck_pins());
    /* The LDC's GetVelocity reads the fields the rows print: +0x60 start, +0xa0 duration, +0x80/+0x90 velocities, +0xa8 flag. */
    static const unsigned char start[5]={0xf2,0x0f,0x10,0x59,0x60},duration[8]={0xf2,0x0f,0x10,0x89,0xa0,0x00,0x00,0x00};
    assert(!memcmp(image+WRECK_LAYOUT,start,5) && !memcmp(image+WRECK_LAYOUT+0x15,duration,8));
    /* The LDC's vtable and its virtual base's (RTTI complete object locators at -8). */
    assert(*(uint64_t*)(image+WRECK_LDC_VTABLE-8)==0x140000000ULL+0x2d4bfe8u && *(uint64_t*)(image+WRECK_LDC_VBASE-8)==0x140000000ULL+0x2d4c060u);
    VirtualFree(image,0,MEM_RELEASE);image=prior;
    printf("wreck watch native image pins PASS\n");
}

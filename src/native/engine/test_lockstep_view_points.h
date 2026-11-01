/* native_gate_view_points: per-viewpoint evaluation, observe, nesting, foreign
 * threads, latch and refresh, patch CAS/rollback, checks and the pinned build. */
typedef struct {void **vtable;uint64_t id;unsigned char pad[0x60];void *parent;unsigned char rest[0x100];} test_vp_object;
static void *test_vp_vtable[704];
static unsigned char test_vp_universe[0x400];
static test_vp_object test_vp_a,test_vp_b,test_vp_player,test_vp_zone,test_vp_zone2,test_vp_root,test_vp_outer,test_vp_inner;
static unsigned test_vp_calls,test_vp_shadow_seen,test_vp_refreshed;
static void *test_vp_refresh_log[16];
static int test_vp_native(void *object) {
    ++test_vp_calls;
    unsigned char *universe=*(unsigned char**)(view_page+VIEW_PAGE_UNIVERSE); /* what the rewritten loads read */
    if(object==&test_vp_outer) {
        assert(universe==view_page+VIEW_PAGE_SHADOW);
        int inner=view_attention(&test_vp_inner);assert(inner==9);
        assert(*(unsigned char**)(view_page+VIEW_PAGE_UNIVERSE)==view_page+VIEW_PAGE_SHADOW); /* nesting keeps the outer shadow */
    }
    void *player=*(void**)(universe+0x230);
    if(universe==test_vp_universe) return *(uint64_t*)(universe+0x3c0)?10:3; /* camera result */
    assert(universe==view_page+VIEW_PAGE_SHADOW && !*(uint64_t*)(universe+0x3c0) && !*(void**)(universe+0x3d0));
    ++test_vp_shadow_seen;
    return player==&test_vp_a?9:player==&test_vp_b?7:player==NULL?2:4;
}
static int test_vp_is_a(void *object,int cls) {return cls==0x6d && (object==&test_vp_zone || object==&test_vp_zone2);}
static void test_vp_set_attention(void *object,int level) {assert(level==1);if(test_vp_refreshed<16) test_vp_refresh_log[test_vp_refreshed]=object;++test_vp_refreshed;}
static DWORD WINAPI test_vp_foreign(LPVOID object) {return (DWORD)view_attention(object);}
static void test_vp_objects(void) {
    test_vp_vtable[703]=(void*)test_vp_native;test_vp_vtable[0x11e8/8]=(void*)test_vp_is_a;
    test_vp_object *all[]={&test_vp_a,&test_vp_b,&test_vp_player,&test_vp_zone,&test_vp_zone2,&test_vp_root,&test_vp_outer,&test_vp_inner};
    for(unsigned i=0;i<sizeof(all)/sizeof(*all);i++) {memset(all[i],0,sizeof(test_vp_object));all[i]->vtable=test_vp_vtable;all[i]->id=0x100+i;}
    test_vp_a.parent=&test_vp_zone;test_vp_b.parent=&test_vp_zone;test_vp_zone.parent=&test_vp_root;test_vp_zone2.parent=&test_vp_root;
    ((unsigned char*)&test_vp_a)[0xd1]=1;((unsigned char*)&test_vp_zone)[0xd1]=1;((unsigned char*)&test_vp_zone2)[0xd1]=1;
    memset(test_vp_universe,0,sizeof(test_vp_universe));*(void**)(test_vp_universe+0x230)=&test_vp_player;*(uint64_t*)(test_vp_universe+0x3c0)=1;
    *(void**)(image+VIEW_UNIVERSE)=test_vp_universe;
}
static void test_vp_reset(void) {
    view_mode=VIEW_OFF;view_installed=view_restored=view_patched=0;view_active=0;view_failures=0;view_foreign=0;view_stage="idle";
    view_tick=UINT64_MAX;view_calls=view_evaluations=view_differ=view_first_differ=view_refreshes=view_latches=view_unreadable=0;
    view_latched=view_count=view_previous_count=view_source=0;view_depth=0;
    memset(view_points,0,sizeof(view_points));memset(view_previous,0,sizeof(view_previous));memset(view_previous_zone,0,sizeof(view_previous_zone));
    memset(view_sites_done,0,sizeof(view_sites_done));view_call_done=0;
    if(view_page) {memset(view_page,0,VIEW_PAGE_SHADOW+VIEW_SHADOW_SIZE);*(uint64_t*)(view_page+VIEW_PAGE_UNIVERSE)=(uint64_t)(uintptr_t)test_vp_universe;}
}
static void test_vp_write_original_sites(void) {
    for(unsigned i=0;i<VIEW_SITES;i++) {
        unsigned char *p=image+view_sites[i].rva;p[0]=0x48;p[1]=0x8b;p[2]=view_sites[i].reg;view_site_bytes(i,0,p+3);
    }
    memcpy(image+VIEW_CALL,view_call_original,6);
}
static void test_view_points(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    unsigned saved_pilots=pilot_slot_count;
    if(!view_page) view_page=view_page_near();assert(view_page);
    if(!view_lock_ready) {InitializeCriticalSection(&view_lock);view_lock_ready=1;}
    test_vp_objects();test_vp_reset();ui_thread=GetCurrentThreadId();stats.issued=4;
    /* Inactive: the live universe (camera result), nothing counted. */
    assert(view_attention(&test_vp_a)==10 && !view_calls && !test_vp_shadow_seen);
    /* Active before the first latch: the live player object is the viewpoint. */
    view_mode=VIEW_PLAYERS;view_active=1;test_vp_calls=0;
    assert(view_attention(&test_vp_a)==4 && view_calls==1 && view_evaluations==1 && test_vp_calls==1);
    assert(*(unsigned char**)(view_page+VIEW_PAGE_UNIVERSE)==test_vp_universe && !*(void**)(view_page+VIEW_PAGE_SHADOW+0x230));
    /* Latched viewpoints: one evaluation each, highest level wins; none: no viewer. */
    view_latched=1;view_count=2;view_points[0]=&test_vp_b;view_points[1]=&test_vp_a;
    assert(view_attention(&test_vp_zone)==9 && view_evaluations==3);
    view_count=0;assert(view_attention(&test_vp_zone)==2 && view_evaluations==4);
    view_count=1;view_points[0]=&test_vp_b;assert(view_attention(&test_vp_zone)==7);
    /* Nested SetAttention inside an evaluation keeps the outer shadow. */
    view_count=1;view_points[0]=&test_vp_a;assert(view_attention(&test_vp_outer)==9);
    assert(*(unsigned char**)(view_page+VIEW_PAGE_UNIVERSE)==test_vp_universe && !view_depth);
    assert(view_frame_calls==6 && view_frame_levels[4]==1 && view_frame_levels[9]==3 && view_frame_levels[2]==1 && view_frame_levels[7]==1);
    /* Observe: the camera result is returned, differences counted. */
    view_mode=VIEW_OBSERVE;stats.issued=5;view_points[0]=&test_vp_b;
    assert(view_attention(&test_vp_zone)==10 && view_differ==1 && view_first_differ==5 && view_frame_differ==1 && view_frame_calls==1);
    *(uint64_t*)(test_vp_universe+0x3c0)=0;view_points[0]=&test_vp_player;
    assert(view_attention(&test_vp_zone)==3 && view_differ==2); /* 4 vs 3 */
    *(uint64_t*)(test_vp_universe+0x3c0)=1;view_mode=VIEW_PLAYERS;
    /* Off the MoveWorker: evaluated alike under the lock, and counted. */
    HANDLE h=CreateThread(NULL,0,test_vp_foreign,&test_vp_zone,0,NULL);assert(h);WaitForSingleObject(h,5000);
    DWORD code=0;GetExitCodeThread(h,&code);CloseHandle(h);assert(code==4 && view_foreign==1);
    view_foreign=0;
    /* Latch: resolved slot ships in slot order; refresh on change only. */
    view_set_attention_override=test_vp_set_attention;test_vp_refreshed=0;view_latched=0;view_count=0;
    pilot_slot_count=2;memset(pilot_slots,0,sizeof(pilot_slots));
    pilot_slots[0].resolved=1;pilot_slots[0].object=(unsigned char*)&test_vp_a;pilot_slots[0].runtime=test_vp_a.id;
    pilot_slots[1].resolved=0;pilot_slots[1].object=(unsigned char*)&test_vp_b;pilot_slots[1].runtime=test_vp_b.id;
    assert(!view_frame(5)); /* not installed */
    view_failures=0;view_installed=1;stats.active=1;
    assert(!view_frame(4) && view_failures==1);view_failures=0; /* tick must be the admitted one */
    assert(view_frame(5) && view_latched && view_count==1 && view_points[0]==&test_vp_a && view_source==1 && view_previous_zone[0]==&test_vp_zone);
    assert(test_vp_refreshed==2 && test_vp_refresh_log[0]==&test_vp_a && test_vp_refresh_log[1]==&test_vp_zone); /* root has no own attention */
    stats.issued=6;assert(view_frame(6) && test_vp_refreshed==2);   /* unchanged: no refresh */
    pilot_slots[1].resolved=1;stats.issued=7;assert(view_frame(7) && view_count==2 && test_vp_refreshed==2+2+3); /* old A chain, new A and B chains */
    test_vp_a.parent=&test_vp_zone2;stats.issued=8;assert(view_frame(8) && view_previous_zone[0]==&test_vp_zone2 && test_vp_refreshed>7);
    pilot_slots[1].runtime=0x999;unsigned before=test_vp_refreshed;stats.issued=9;
    assert(view_frame(9) && view_count==1 && view_unreadable==1 && test_vp_refreshed>before); /* a mismatched slot is skipped */
    /* A dead slot (lockstep_pilot_death.h: not resolved, no object), in its suit or not, is no viewpoint, and the latch reads the
     * slots only: the client's own slot and what it presents change nothing (missile1: viewpoints=1 ids=1658d6 on both clients
     * from slot 2's death on; reports/LOCKSTEP-SUIT-VIEW-20261101.txt). */
    {unsigned saved_slot=stats.slot,saved_pres=pres_slot;pslot_slot saved_seat=pslots[2];void *sets[2][VIEW_MAX];unsigned counts[2];
     pilot_slots[1].runtime=test_vp_b.id;pilot_slots[1].resolved=0;pilot_slots[1].object=NULL;pilot_slots[1].dead=1;
     for(unsigned client=0;client<2;client++) {
         stats.slot=client+1;pres_slot=client?2u:0u;memset(&pslots[2],0,sizeof(pslots[2]));
         if(client) {pslots[2].seated=1;pslots[2].roomless=1;pslots[2].ship=pslots[2].room=(unsigned char*)&test_vp_b;}  /* seated in its suit */
         assert(view_frame(9));counts[client]=view_count;memcpy(sets[client],view_points,sizeof(view_points));
     }
     assert(counts[0]==1 && counts[1]==1 && sets[0][0]==&test_vp_a && sets[1][0]==&test_vp_a && view_unreadable==1);
     stats.slot=saved_slot;pres_slot=saved_pres;pslots[2]=saved_seat;pilot_slots[1].dead=0;}
    view_mode=VIEW_OBSERVE;pilot_slot_count=0;before=test_vp_refreshed;stats.issued=10;
    assert(view_frame(10) && view_count==1 && view_points[0]==&test_vp_player && view_source==2 && test_vp_refreshed==before); /* observe never refreshes */
    view_set_attention_override=NULL;pilot_slot_count=saved_pilots;memset(pilot_slots,0,sizeof(pilot_slots));
    char status[1280];int n=view_status(status,sizeof(status));
    assert(n>0 && strstr(status,"mode=observe armed=1 installed=1 frame=5 source=player viewpoints=1 ids=102 calls=0 evaluations=0 levels=0/0/0/0/0/0/0/0/0/0/0"));
    /* Aligned-block CAS: a straddling write lands in both blocks or in neither. */
    unsigned char want[4]={0xaa,0xbb,0xcc,0xdd},had[4]={1,2,3,4};memcpy(image+0x100e,had,4);
    assert(view_cas_bytes(0x100e,had,want,4) && !memcmp(image+0x100e,want,4));
    image[0x1010]=0x77;assert(!view_cas_bytes(0x100e,want,had,4) && !memcmp(image+0x100e,"\xaa\xbb\x77\xdd",4)); /* first block rolled back */
    memset(image+0x1000,0,0x20);
    /* Patches: install, ownership, a foreign site stays marked, remove. */
    test_vp_reset();test_vp_write_original_sites();
    for(unsigned i=0;i<VIEW_SITES;i++) assert(view_site_state(i)==0);
    assert(view_call_state()==0 && view_install_patches());
    for(unsigned i=0;i<VIEW_SITES;i++) assert(view_site_state(i)==1);
    assert(view_call_state()==1 && image[VIEW_CALL]==0xff && image[VIEW_CALL+1]==0x15);
    void *entry=(void*)view_attention;memcpy(view_page+VIEW_PAGE_CALL,&entry,8);view_patched=1;view_installed=1;
    assert(view_owned());
    unsigned char saved_byte=image[view_sites[7].rva+4];image[view_sites[7].rva+4]^=0x55;
    assert(!view_owned() && !view_check() && !strcmp(view_stage,"site_changed"));view_failures=0;
    view_restore();assert(view_installed && view_patched && !view_restored && view_sites_done[7] && !view_sites_done[0] && !view_call_done);
    image[view_sites[7].rva+4]=saved_byte;view_restore();
    assert(view_restored && !view_installed && !view_patched && view_call_state()==0);
    for(unsigned i=0;i<VIEW_SITES;i++) assert(view_site_state(i)==0);
    /* finish() restores; a foreign call site keeps stats.restored at 0. */
    test_vp_reset();assert(view_install_patches());view_patched=view_installed=1;view_active=1;
    uint32_t saved_reason=stats.reason;stats.active=1;stats.restored=1;finish(97);
    assert(stats.reason==97 && stats.restored==1 && !view_installed && view_call_state()==0 && !view_active);
    test_vp_reset();assert(view_install_patches());view_patched=view_installed=1;image[VIEW_CALL+5]^=1;
    stats.active=1;stats.restored=1;finish(97);assert(!stats.restored && view_installed);
    image[VIEW_CALL+5]^=1;view_restore();assert(!view_installed && view_call_state()==0);
    /* Arming refusals on the unpinned zero image. */
    test_vp_reset();stats.active=0;assert(!view_arm(VIEW_PLAYERS));
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId()+1;assert(!view_arm(VIEW_PLAYERS));
    ui_thread=GetCurrentThreadId();boot_phase=1;assert(!view_arm(VIEW_PLAYERS));boot_phase=0;
    assert(!view_arm(VIEW_OFF) && !view_arm(3));
    assert(!view_arm(VIEW_PLAYERS) && !strcmp(view_stage,"pins") && !view_installed && view_call_state()==0);
    /* Status bounds at the widest values. */
    view_count=VIEW_MAX;for(unsigned i=0;i<VIEW_MAX;i++) view_ids[i]=UINT64_MAX;view_source=1;view_mode=VIEW_OBSERVE;
    view_tick=stats.issued=UINT64_MAX-1;view_frame_calls=view_frame_evaluations=view_frame_differ=UINT64_MAX;
    for(unsigned i=0;i<VIEW_LEVELS;i++) view_frame_levels[i]=UINT64_MAX;
    view_calls=view_evaluations=view_differ=view_first_differ=view_refreshes=view_latches=view_unreadable=UINT64_MAX;
    view_foreign=view_failures=INT64_MAX;view_stage="frame_guard";view_ship_misses=UINT64_MAX;view_masked=UINT64_MAX;
    n=view_status(status,sizeof(status));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE VIEW_POINTS tick=18446744073709551615 \n")<2048);
    view_masked=0;
    for(unsigned i=0;i<VIEW_SITES;i++) memset(image+view_sites[i].rva,0,7);
    memset(image+VIEW_CALL,0,6);*(void**)(image+VIEW_UNIVERSE)=NULL;test_vp_reset();
    VirtualFree(view_page,0,MEM_RELEASE);view_page=NULL;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;stats.reason=saved_reason;stats.restored=0;
    boot_mode=saved_boot;boot_phase=saved_phase;
    printf("view points evaluation/observe/nesting/foreign/latch/refresh/CAS/check/finish/status tests PASS\n");
}
static void test_view_points_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        assert(section.VirtualAddress<=nt.OptionalHeader.SizeOfImage && section.SizeOfRawData<=nt.OptionalHeader.SizeOfImage-section.VirtualAddress);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    unsigned char *saved_page=view_page;view_page=NULL;
    assert(view_pins() && view_call_state()==0);
    for(unsigned i=0;i<VIEW_SITES;i++) assert(view_site_state(i)==0); /* every load is mov r64,[g_Universe] */
    /* SetAttention calls slot 703 through the object's vtable at the one site. */
    assert(!memcmp(image+VIEW_CALL-3,"\x48\x8b\x01\xff\x90\xf8\x15\x00\x00",9) && 0x15f8==703*8);
    /* The loads feed the player object (+0x230) and the camera list (+0x2c0, count +0x3c0, player camera +0x3d0). */
    assert(!memcmp(image+0x6d21c0,"\x48\x8d\xa8\x30\x02\x00\x00",7) && !memcmp(image+0x6d2313,"\x48\x81\xc7\xc0\x02\x00\x00",7) &&
           !memcmp(image+0x6d2322,"\x48\x8b\x87\x00\x01\x00\x00",7) && !memcmp(image+0x6d236d,"\x48\x8b\x88\xd0\x03\x00\x00",7));
    assert(!memcmp(image+0x843c82,"\x4c\x8d\xb0\x30\x02\x00\x00",7) && !memcmp(image+0x843d8d,"\x48\x81\xc7\xc0\x02\x00\x00",7) &&
           !memcmp(image+0x843d94,"\x48\x8b\x87\x00\x01\x00\x00",7) && !memcmp(image+0x843dbf,"\x48\x8b\x88\xd0\x03\x00\x00",7));
    assert(!memcmp(image+0x3e5104,"\x48\x81\xc3\xc0\x02\x00\x00",7) && !memcmp(image+0x3e510e,"\x48\x8b\x83\x00\x01\x00\x00",7) &&
           !memcmp(image+0x3e5143,"\x48\x8b\x88\xd0\x03\x00\x00",7));
    /* Tampering anywhere in a pinned function, or a site's opcode, refuses. */
    for(unsigned f=0;f<VIEW_FUNCS;f++) {
        unsigned at=view_funcs[f].rva+view_funcs[f].size/2;image[at]^=1;assert(!view_pins());image[at]^=1;
    }
    for(unsigned i=0;i<VIEW_SITES;i+=5) {image[view_sites[i].rva+2]^=8;assert(!view_pins() && view_site_state(i)<0);image[view_sites[i].rva+2]^=8;}
    assert(view_pins());
    /* Arm/check/restore on the mapped image. */
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    test_vp_reset();stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();boot_mode=1;boot_phase=0;
    assert(view_arm(VIEW_PLAYERS) && view_installed && view_patched && view_active && view_page);
    assert(view_call_state()==1 && image[VIEW_CALL]==0xff && image[VIEW_CALL+1]==0x15);
    for(unsigned i=0;i<VIEW_SITES;i++) assert(view_site_state(i)==1);
    int32_t d;memcpy(&d,image+VIEW_CALL+2,4);assert(image+VIEW_CALL+6+d==view_page+VIEW_PAGE_CALL);
    void *entry;memcpy(&entry,view_page+VIEW_PAGE_CALL,8);assert(entry==(void*)view_attention);
    memcpy(&d,image+view_sites[0].rva+3,4);assert(image+view_sites[0].rva+7+d==view_page+VIEW_PAGE_UNIVERSE);
    assert(view_pins() && view_owned() && view_check());
    assert(view_arm(VIEW_PLAYERS) && !view_arm(VIEW_OBSERVE)); /* identical rerun only */
    image[view_funcs[5].rva+0x40]^=1;assert(!view_check() && !strcmp(view_stage,"block_changed"));image[view_funcs[5].rva+0x40]^=1;
    view_failures=0;assert(view_check());
    view_restore();assert(view_restored && !view_installed && view_call_state()==0 && view_pins());
    for(unsigned i=0;i<VIEW_SITES;i++) assert(view_site_state(i)==0);
    test_vp_reset();image[VIEW_CALL+1]=0x15;assert(!view_arm(VIEW_PLAYERS) && !strcmp(view_stage,"pins"));image[VIEW_CALL+1]=0x90;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    VirtualFree(view_page,0,MEM_RELEASE);view_page=saved_page;test_vp_reset();
    assert(view_trigger_sites_ok()); /* nine calls to SetAttention: five camera enter/leave handlers, four cutscene shot sites */
    /* native_gate_view_ui's camera scope: 0x97dff0's .pdata entry is exactly 0x97dff0..0x97ee40, its body holds every
     * reference to the CameraLeftEvent (0x2bafca8) and CameraEnteredEvent (0x2bafce0) vtables in .text and its calls of
     * PostEvent 0x99a3f0 (p4-long4's return address 0x97e65d among them), and ~Camera calls it at 0x9794d2. The handler
     * 0x6b8540 that re-evaluated the cockpit tail-calls SetAttention (cmp rdx,rcx; jne; mov edx,1; jmp). */
    assert(view_ui_camera_ok());
    {   const IMAGE_DATA_DIRECTORY *pd=&nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];int found=0;
        assert(pd->VirtualAddress && pd->Size>=12);
        for(uint32_t at=pd->VirtualAddress;at+12<=pd->VirtualAddress+pd->Size;at+=12) {
            uint32_t begin,end;memcpy(&begin,image+at,4);memcpy(&end,image+at+4,4);
            if(begin<=0x97e65du && 0x97e65du<end) {assert(begin==VIEW_CAMERA_LO && end==VIEW_CAMERA_HI);found=1;}
        }
        assert(found);
        unsigned leas=0,posts=0;
        for(uint32_t at=0x1000;at+7<=0x1bc070cu;at++) { /* .text */
            const unsigned char *p=image+at;
            if((p[0]==0x48 || p[0]==0x4c) && p[1]==0x8d && (p[2]&0xc7)==0x05) {
                int32_t d;memcpy(&d,p+3,4);uint32_t target=at+7+(uint32_t)d;
                if(target==0x2bafca8u || target==0x2bafce0u) {assert(at>=VIEW_CAMERA_LO && at<VIEW_CAMERA_HI);++leas;}
            }
            if(at>=VIEW_CAMERA_LO && at+5<=VIEW_CAMERA_HI && p[0]==0xe8) {
                int32_t d;memcpy(&d,p+1,4);if(at+5+(uint32_t)d==0x99a3f0u) {++posts;if(at+5==0x97e65du) found=2;}
            }
        }
        assert(leas==10 && posts>=2 && found==2);
        assert(!memcmp(image+0x6b8540,"\x48\x3b\xd1\x75\x0a\xba\x01\x00\x00\x00\xe9",11));
        int32_t d;memcpy(&d,image+0x6b854b,4);assert(0x6b854fu+(uint32_t)d==VIEW_SET_ATTENTION);
        assert(image[0x99c5ad]==0xff && image[0x99c5ae]==0x50 && image[0x99c5af]==0x20); /* the dispatch: call [rax+0x20] */
    }
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("view points native pins, slot-703 call, universe loads, tamper refusals and arm/restore verified");
}
/* native_gate_view_rows: a row per change of an object's (level, camera) pair, with key, class and vtable. */
static void test_view_rows(void) {
    DWORD saved_ui=ui_thread;uint64_t saved_issued=stats.issued;int saved_installed=view_installed;
    ui_thread=GetCurrentThreadId();view_installed=0;view_rows_armed=0;assert(!view_rows_arm());
    view_installed=1;assert(view_rows_arm() && view_rows_armed && !view_rows);
    static struct {void *vtable;uint64_t runtime;} a,b;a.vtable=image+0x2b66080;a.runtime=0x51;b.vtable=(void*)1;b.runtime=0;
    int saved_boot=boot_mode,saved_phase=boot_phase;boot_mode=1;boot_phase=2;
    view_row_note(&a,6,-1);assert(!view_rows && !view_rows_started); /* loading: not recorded */
    boot_phase=3;stats.issued=2;view_row_note(&a,6,-1);view_row_note(&a,6,-1);assert(view_rows==1 && view_rows_started);
    view_row_note(&a,7,-1);view_row_note(&b,5,8);assert(view_rows==3);
    char row[256];
    assert(view_row(0,row,sizeof(row))>0 && !strncmp(row,"index=0 tick=2 key=0000000000000000 klass=2 vtable=02b66080 level=6 camera=-1 trigger=",86));
    assert(view_row_ring[0].trigger==0xffffffffu && view_row_ring[0].chain[3]==0xffffffffu); /* the test process has no image frames */
    assert(strstr(row," trigger=ffffffff chain=ffffffff,ffffffff,ffffffff,ffffffff"));
    assert(view_row(2,row,sizeof(row))>0 && strstr(row," vtable=ffffffff level=5 camera=8"));
    assert(!view_row(3,row,sizeof(row)));
    view_rows=VIEW_ROW_RING+4;assert(view_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1"));
    view_rows_armed=0;view_rows=0;view_rows_started=0;memset(view_row_seen,0,sizeof(view_row_seen));boot_mode=saved_boot;boot_phase=saved_phase;
    view_installed=saved_installed;ui_thread=saved_ui;stats.issued=saved_issued;
    /* Camera-handler and cutscene triggers: matching, block keeps the current level, observe only counts, arming. */
    assert(view_trigger_match(0x7cdde0u)==0 && view_trigger_match(0x3baabeu)==4 && view_trigger_match(0x3d597au)==-1 && view_trigger_match(0xffffffffu)==-1);
    assert(view_trigger_match(0x10d5f5bu)==5 && view_trigger_match(0x10d686bu)==6 && view_trigger_match(0x10d831au)==7 && view_trigger_match(0x10e2b00u)==8);
    static unsigned char obj[0x200];memset(obj,0,sizeof(obj));*(int*)(obj+0x1ec)=9;int kept=-1;
    view_trigger_mode=VIEW_TRIGGER_OFF;memset(view_trigger_counts,0,sizeof(view_trigger_counts));view_trigger_blocked=0;
    assert(!view_trigger_apply(obj,-1,&kept) && kept==-1);
    view_trigger_mode=VIEW_TRIGGER_OBSERVE;assert(!view_trigger_apply(obj,1,&kept) && view_trigger_counts[1]==1 && !view_trigger_blocked);
    view_trigger_mode=VIEW_TRIGGER_BLOCK;assert(view_trigger_apply(obj,2,&kept) && kept==9 && view_trigger_counts[2]==1 && view_trigger_blocked==1);
    kept=-1;assert(view_trigger_apply(obj,7,&kept) && kept==9 && view_trigger_counts[7]==1 && view_trigger_blocked==2); /* a cutscene shot */
    view_trigger_mode=VIEW_TRIGGER_OFF;view_installed=0;assert(!view_triggers_arm(VIEW_TRIGGER_BLOCK));
    view_installed=1;assert(!view_triggers_arm(VIEW_TRIGGER_OFF) && !view_triggers_arm(7));
    assert(!view_triggers_arm(VIEW_TRIGGER_BLOCK) && !strcmp(view_stage,"trigger_sites")); /* zero image: no calls at the sites */
    view_trigger_mode=VIEW_TRIGGER_OFF;view_stage="idle";memset(view_trigger_counts,0,sizeof(view_trigger_counts));view_trigger_blocked=0;
    char status[2048];view_installed=saved_installed;assert(view_status(status,sizeof(status))>0 && strstr(status," triggers=off:0/0/0/0/0 blocked=0"));
    assert(strstr(status," cutscene=0/0/0/0 callers=- callers_other=0"));
    /* Evaluation callers: sorted (trigger, caller) pairs with counts, a full table counts new pairs apart. */
    view_caller_count=0;view_caller_other=0;
    view_caller_add(0x83497cu,0x7cc9a0u);view_caller_add(0x3d6fc2u,0x5c1000u);view_caller_add(0x83497cu,0x7cc9a0u);view_caller_add(0x3d6fc2u,0x41a4ffu);
    assert(view_caller_count==3 && view_callers[0].trigger==0x3d6fc2u && view_callers[0].caller==0x41a4ffu && view_callers[2].count==2);
    view_rows_armed=1;assert(view_status(status,sizeof(status))>0 && strstr(status," callers=3d6fc2/41a4ff:1,3d6fc2/5c1000:1,83497c/7cc9a0:2 callers_other=0"));
    for(uint32_t t=0;t<VIEW_CALLERS;t++) view_caller_add(0x100000u+t,0);
    assert(view_caller_count==VIEW_CALLERS && view_caller_other==3 && view_callers[0].trigger==0x100000u);
    char text[34];view_callers_text(text,sizeof(text));assert(!strcmp(text,"100000/0:1,100001/0:1,100002/0:1")); /* stops before a pair that does not fit */
    view_caller_count=0;view_caller_other=0;view_rows_armed=0;
    int saved_boot2=boot_mode,saved_phase2=boot_phase;boot_mode=1;boot_phase=2;view_caller_note();assert(!view_caller_count); /* loading */
    boot_phase=3;view_caller_note();assert(view_caller_count==1 && view_callers[0].trigger==0xffffffffu && view_callers[0].caller==0xffffffffu);
    view_caller_count=0;boot_mode=saved_boot2;boot_phase=saved_phase2;
    printf("view rows change/dedupe/format/eviction tests PASS\n");
}
/* native_gate_view_ui: an evaluation inside a client-local scope keeps a shared object's level. */
static uint32_t test_vui_frames[4];static unsigned test_vui_frame_count,test_vui_native_calls;
static unsigned test_vui_stack(uint32_t *frames,unsigned capacity) {
    unsigned n=test_vui_frame_count<capacity?test_vui_frame_count:capacity;memcpy(frames,test_vui_frames,n*sizeof(*frames));return n;
}
static int test_vui_native(void *object) {(void)object;++test_vui_native_calls;return 7;}
static int test_vui_refresh_scope=-1;
static void test_vui_set_attention(void *object,int level) {(void)object;(void)level;test_vui_refresh_scope=(int)view_ui_scope();}
static void test_view_ui(void) {
    DWORD saved_ui=ui_thread;uint64_t saved_issued=stats.issued;int saved_installed=view_installed,saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_built=rid_local_built;
    ui_thread=GetCurrentThreadId();stats.issued=0;view_ui_mode=VIEW_UI_OFF;view_ui_stack_override=test_vui_stack;test_vui_frame_count=0;
    /* Arming: view points installed, before the first admitted frame, on the frame thread, the camera range as pinned
     * (0x97dff0's entry, the ret ending its body, ~Camera's call into it); a rerun keeps the mode. */
    view_installed=0;assert(!view_ui_arm(VIEW_UI_BLOCK));view_installed=1;
    assert(!view_ui_arm(VIEW_UI_OFF) && !view_ui_arm(9));
    stats.issued=3;assert(!view_ui_arm(VIEW_UI_BLOCK));stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!view_ui_arm(VIEW_UI_BLOCK));ui_thread=GetCurrentThreadId();
    view_stage="idle";assert(!view_ui_arm(VIEW_UI_BLOCK) && !view_ui_mode && !strcmp(view_stage,"ui_camera")); /* zero image */
    memcpy(image+VIEW_CAMERA_LO,"\x48\x89\x54\x24\x10\x48\x89\x4c\x24\x08",10);image[VIEW_CAMERA_HI-1]=0xc3;
    {int32_t rel=(int32_t)(VIEW_CAMERA_LO-(VIEW_CAMERA_DTOR_CALL+5));image[VIEW_CAMERA_DTOR_CALL]=0xe8;memcpy(image+VIEW_CAMERA_DTOR_CALL+1,&rel,4);}
    assert(view_ui_camera_ok());
    image[VIEW_CAMERA_HI-1]=0xcc;assert(!view_ui_camera_ok());image[VIEW_CAMERA_HI-1]=0xc3;
    image[VIEW_CAMERA_DTOR_CALL+1]^=1;assert(!view_ui_camera_ok());image[VIEW_CAMERA_DTOR_CALL+1]^=1;
    image[VIEW_CAMERA_LO+4]^=1;assert(!view_ui_camera_ok());image[VIEW_CAMERA_LO+4]^=1;
    view_stage="idle";
    assert(view_ui_arm(VIEW_UI_BLOCK) && view_ui_mode==VIEW_UI_BLOCK && view_ui_arm(VIEW_UI_BLOCK) && !view_ui_arm(VIEW_UI_OBSERVE));
    /* Scopes, in order: presentation scope, a client-only view call, the dispatch of presentation ids, slot 10, then (not
     * in the probe's frame) slot 6 by the UI replay's depth or the stack; another thread or the gate's refresh: none. */
    assert(view_ui_scope()==VIEW_UI_NONE);
    InterlockedExchange(&pres_open,1);assert(view_ui_scope()==VIEW_UI_PRESENT);
    view_refreshing=1;assert(view_ui_scope()==VIEW_UI_NONE);view_refreshing=0;
    ui_thread=GetCurrentThreadId()+1;assert(view_ui_scope()==VIEW_UI_NONE);ui_thread=GetCurrentThreadId();
    InterlockedExchange(&pres_open,0);
    rid_scope(1);assert(view_ui_scope()==VIEW_UI_VIEW);rid_scope(0);
    InterlockedExchange(&uirep_dispatch_depth,1);assert(view_ui_scope()==VIEW_UI_DISPATCH);InterlockedExchange(&uirep_dispatch_depth,0);
    InterlockedExchange(&render_rng_in_slot,1);assert(view_ui_scope()==VIEW_UI_RENDER);InterlockedExchange(&render_rng_in_slot,0);
    InterlockedExchange(&uirep_ui_depth,1);assert(view_ui_scope()==VIEW_UI_SLOT6);
    InterlockedExchange(&rid_probe_frame,1);assert(view_ui_scope()==VIEW_UI_NONE);InterlockedExchange(&uirep_ui_depth,0);
    test_vui_frames[0]=0x3d6fc2u;test_vui_frames[1]=0xb3bb3cu;test_vui_frame_count=2;assert(view_ui_scope()==VIEW_UI_NONE); /* the probe's frame */
    InterlockedExchange(&rid_probe_frame,0);assert(view_ui_scope()==VIEW_UI_SLOT6);
    test_vui_frames[1]=0xb3bb9eu;assert(view_ui_scope()==VIEW_UI_NONE); /* past the body */
    test_vui_frames[1]=0x121c1f0u;assert(view_ui_scope()==VIEW_UI_RENDER);
    test_vui_frames[1]=0x83497cu;assert(view_ui_scope()==VIEW_UI_NONE);
    /* A camera event (p4-long4: ~Camera 0x9794a0 -> 0x97dff0 -> PostEvent -> dispatch 0x99c5b0 -> 0x6b8540's tail call):
     * any return address in 0x97dff0's body, before the probe's-frame exemption and the slot-6/slot-10 search; the flags
     * above still name their own scope; another thread or the gate's refresh: none. */
    test_vui_frames[0]=0x99c5b0u;test_vui_frames[1]=0x99a4edu;test_vui_frames[2]=0x97e65du;test_vui_frames[3]=0x9794d7u;test_vui_frame_count=4;
    assert(view_ui_scope()==VIEW_UI_CAMERA);
    test_vui_frames[3]=0xb3bb3cu;assert(view_ui_scope()==VIEW_UI_CAMERA); /* inside UI::CUIController::Move */
    InterlockedExchange(&rid_probe_frame,1);assert(view_ui_scope()==VIEW_UI_CAMERA); /* inside the probe's frame */
    InterlockedExchange(&uirep_ui_depth,1);assert(view_ui_scope()==VIEW_UI_CAMERA);
    InterlockedExchange(&uirep_ui_depth,0);InterlockedExchange(&rid_probe_frame,0);
    InterlockedExchange(&pres_open,1);assert(view_ui_scope()==VIEW_UI_PRESENT);InterlockedExchange(&pres_open,0);
    view_refreshing=1;assert(view_ui_scope()==VIEW_UI_NONE);view_refreshing=0;
    ui_thread=GetCurrentThreadId()+1;assert(view_ui_scope()==VIEW_UI_NONE);ui_thread=GetCurrentThreadId();
    test_vui_frames[2]=VIEW_CAMERA_LO;assert(view_ui_scope()==VIEW_UI_CAMERA);
    test_vui_frames[2]=VIEW_CAMERA_HI-1;assert(view_ui_scope()==VIEW_UI_CAMERA);
    test_vui_frames[2]=VIEW_CAMERA_HI;assert(view_ui_scope()==VIEW_UI_SLOT6); /* the next function; Move's frame remains */
    test_vui_frames[3]=0x83497cu;assert(view_ui_scope()==VIEW_UI_NONE);
    test_vui_frames[2]=VIEW_CAMERA_LO-1;assert(view_ui_scope()==VIEW_UI_NONE);
    test_vui_frame_count=0;
    /* Apply: shared objects keep their level ('block') or are counted ('observe'); the UI's own objects (local lists) evaluate. */
    static unsigned char shared[0x200],own[0x200];memset(shared,0,sizeof(shared));memset(own,0,sizeof(own));
    *(uint64_t*)(shared+8)=0x10042;*(int*)(shared+0x1ec)=6;*(uint64_t*)(own+8)=(3ULL<<25)|0x1f00001u;*(int*)(own+0x1ec)=2;
    memset(view_ui_counts,0,sizeof(view_ui_counts));view_ui_blocked=view_ui_local=0;rid_local_built=1;int kept=-1;
    assert(!view_ui_apply(shared,&kept) && kept==-1 && !view_ui_counts[VIEW_UI_PRESENT]); /* no scope */
    InterlockedExchange(&pres_open,1);
    assert(view_ui_apply(shared,&kept) && kept==6 && view_ui_counts[VIEW_UI_PRESENT]==1 && view_ui_blocked==1);
    kept=-1;assert(!view_ui_apply(own,&kept) && kept==-1 && view_ui_counts[VIEW_UI_PRESENT]==2 && view_ui_local==1 && view_ui_blocked==1);
    *(uint64_t*)(own+8)=(3ULL<<25)|0x1e00001u;assert(!view_ui_apply(own,&kept) && view_ui_local==2); /* block 30, the view list */
    rid_local_built=0;assert(view_ui_apply(own,&kept) && kept==2 && view_ui_blocked==2); /* no local lists: shared */
    rid_local_built=1;view_ui_mode=VIEW_UI_OBSERVE;assert(!view_ui_apply(shared,&kept) && view_ui_blocked==2 && view_ui_counts[VIEW_UI_PRESENT]==5);
    InterlockedExchange(&pres_open,0);
    /* The camera scope alike: the cockpit keeps its level, the HoloMap (local lists) evaluates; 'observe' counts. */
    view_ui_mode=VIEW_UI_BLOCK;test_vui_frames[0]=0x99c5b0u;test_vui_frames[1]=0x97e65du;test_vui_frame_count=2;kept=-1;
    assert(view_ui_apply(shared,&kept) && kept==6 && view_ui_counts[VIEW_UI_CAMERA]==1 && view_ui_blocked==3);
    kept=-1;assert(!view_ui_apply(own,&kept) && kept==-1 && view_ui_counts[VIEW_UI_CAMERA]==2 && view_ui_local==3);
    view_ui_mode=VIEW_UI_OBSERVE;assert(!view_ui_apply(shared,&kept) && kept==-1 && view_ui_counts[VIEW_UI_CAMERA]==3 && view_ui_blocked==3);
    test_vui_frame_count=0;
    /* Through the rewritten call: blocked in the scope without running the native evaluation, as stock outside it. */
    if(!view_page) view_page=view_page_near();assert(view_page);
    if(!view_lock_ready) {InitializeCriticalSection(&view_lock);view_lock_ready=1;}
    test_vp_objects();test_vp_reset();view_installed=1;
    static void *vtable[704];vtable[703]=(void*)test_vui_native;*(void***)shared=vtable;*(void***)own=vtable;
    view_mode=VIEW_PLAYERS;view_active=1;view_ui_mode=VIEW_UI_BLOCK;boot_mode=1;boot_phase=3;stats.issued=40;test_vui_native_calls=0;
    memset(view_ui_counts,0,sizeof(view_ui_counts));view_ui_blocked=view_ui_local=0;
    assert(view_attention(shared)==7 && test_vui_native_calls==1 && !view_ui_blocked);
    InterlockedExchange(&pres_open,1);
    assert(view_attention(shared)==6 && test_vui_native_calls==1 && view_ui_blocked==1 && view_calls==2);
    assert(view_attention(own)==7 && test_vui_native_calls==2 && view_ui_local==1);
    InterlockedExchange(&pres_open,0);
    test_vui_frames[0]=0x99c5b0u;test_vui_frames[1]=0x97e65du;test_vui_frame_count=2; /* a camera event, no flag */
    assert(view_attention(shared)==6 && test_vui_native_calls==2 && view_ui_blocked==2 && view_ui_counts[VIEW_UI_CAMERA]==1);
    test_vui_frame_count=0;assert(view_attention(shared)==7 && test_vui_native_calls==3 && view_ui_blocked==2);
    boot_phase=2;InterlockedExchange(&pres_open,1);assert(view_attention(shared)==7 && view_ui_blocked==2);InterlockedExchange(&pres_open,0); /* loading */
    char status[2048];assert(view_status(status,sizeof(status))>0 && strstr(status," ui=block:2/0/0/0/0/1 ui_blocked=2 ui_local=1"));
    /* The gate's own refresh of a viewpoint chain is never a client-local scope. */
    {   view_set_attention_fn saved_override=view_set_attention_override;
        InterlockedExchange(&pres_open,1);boot_phase=3;shared[0xd1]=1;*(void**)(shared+0x70)=NULL;
        view_set_attention_override=test_vui_set_attention;test_vui_refresh_scope=-1;view_refresh_chain(shared);
        assert(test_vui_refresh_scope==VIEW_UI_NONE && !view_refreshing && view_ui_scope()==VIEW_UI_PRESENT);
        InterlockedExchange(&pres_open,0);shared[0xd1]=0;view_set_attention_override=saved_override;}
    view_ui_mode=VIEW_UI_OFF;assert(view_status(status,sizeof(status))>0 && strstr(status," ui=off:"));
    test_vp_reset();view_ui_stack_override=NULL;rid_local_built=saved_built;memset(view_ui_counts,0,sizeof(view_ui_counts));view_ui_blocked=view_ui_local=0;
    ui_thread=saved_ui;stats.issued=saved_issued;view_installed=saved_installed;boot_mode=saved_boot;boot_phase=saved_phase;
    *(void**)(image+VIEW_UNIVERSE)=NULL;view_stage="idle";
    memset(image+VIEW_CAMERA_LO,0,10);image[VIEW_CAMERA_HI-1]=0;memset(image+VIEW_CAMERA_DTOR_CALL,0,5);
    printf("view ui scopes/camera events/block/observe/local objects tests PASS\n");
}
/* native_gate_view_points_source='ship': the occupied container (first non-null
 * entry of player+0x308..+0x310 that is an ancestor) replaces the player object. */
static void test_view_source(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_installed=view_installed;
    unsigned saved_pilots=pilot_slot_count;
    test_vp_objects();if(!view_lock_ready) {InitializeCriticalSection(&view_lock);view_lock_ready=1;}
    static __declspec(align(16)) unsigned char player[0x400];static void *seats[4];
    memset(player,0,sizeof(player));*(uint64_t*)(player+8)=0x4242;void *ship=&test_vp_a;memcpy(player+0x70,&ship,8);
    seats[0]=NULL;seats[1]=&test_vp_a;seats[2]=&test_vp_b;void *begin=seats,*end=seats+3;
    memcpy(player+0x308,&begin,8);memcpy(player+0x310,&end,8);
    *(void**)(test_vp_universe+0x230)=player;
    /* Arming: before the first admitted frame, on the MoveWorker; names player and ship only. */
    ui_thread=GetCurrentThreadId();stats.issued=3;assert(!view_source_arm("ship"));stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!view_source_arm("ship"));ui_thread=GetCurrentThreadId();
    assert(!view_source_arm("camera") && !view_source_arm("") && !view_source_arm(NULL));
    assert(view_source_arm("ship") && view_source_mode==VIEW_SOURCE_SHIP);
    view_installed=1;stats.active=1;view_mode=VIEW_OBSERVE;pilot_slot_count=0;view_latched=0;view_count=0;
    /* The seat entry is an ancestor: the ship is the viewpoint. */
    stats.issued=20;assert(view_frame(20) && view_count==1 && view_points[0]==&test_vp_a && view_ids[0]==test_vp_a.id && view_source==3 && !view_ship_misses);
    char status[1280];assert(view_status(status,sizeof(status))>0 && strstr(status," source=ship viewpoints=1 ") && strstr(status," source_mode=ship ship_misses=0"));
    /* Through a room: still found up the +0x70 chain. */
    void *room=&test_vp_inner;memcpy(player+0x70,&room,8);test_vp_inner.parent=&test_vp_a;
    stats.issued=21;assert(view_frame(21) && view_points[0]==&test_vp_a && view_source==3);
    /* The first non-null entry is not an ancestor: the player object, counted. */
    seats[1]=&test_vp_b;stats.issued=22;assert(view_frame(22) && view_points[0]==player && view_ids[0]==0x4242 && view_source==2 && view_ship_misses==1);
    /* Empty or inverted range: the player object. */
    end=seats;memcpy(player+0x310,&end,8);stats.issued=23;assert(view_frame(23) && view_points[0]==player && view_ship_misses==2);
    begin=seats+2;end=seats;memcpy(player+0x308,&begin,8);memcpy(player+0x310,&end,8);stats.issued=24;assert(view_frame(24) && view_ship_misses==3);
    /* Mode player: the player object, nothing counted. */
    begin=seats;end=seats+3;seats[1]=&test_vp_a;memcpy(player+0x308,&begin,8);memcpy(player+0x310,&end,8);
    stats.issued=0;assert(view_source_arm("player") && !view_ship_misses);stats.issued=25;
    assert(view_frame(25) && view_points[0]==player && view_source==2 && !view_ship_misses);
    assert(view_status(status,sizeof(status))>0 && strstr(status," source=player ") && strstr(status," source_mode=player ship_misses=0"));
    view_source_mode=VIEW_SOURCE_PLAYER;test_vp_inner.parent=NULL;test_vp_objects();test_vp_reset();
    pilot_slot_count=saved_pilots;stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;view_installed=saved_installed;
    *(void**)(image+VIEW_UNIVERSE)=NULL;
    printf("view points ship source tests PASS\n");
}
/* native_gate_view_points_mask: masked slots leave the latched list on every client alike; the
 * live player object's chain is recomputed at the first latch; mask 0 evaluates without a viewer. */
static void test_view_mask(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;
    unsigned saved_pilots=pilot_slot_count;int saved_trigger=view_trigger_mode,saved_ui_mode=view_ui_mode,saved_rows=view_rows_armed;
    view_trigger_mode=VIEW_TRIGGER_OFF;view_ui_mode=VIEW_UI_OFF;view_rows_armed=0;
    if(!view_page) view_page=view_page_near();assert(view_page);
    if(!view_lock_ready) {InitializeCriticalSection(&view_lock);view_lock_ready=1;}
    test_vp_objects();test_vp_reset();ui_thread=GetCurrentThreadId();stats.active=1;stats.issued=0;
    test_vp_player.parent=&test_vp_zone2;
    /* Arming: installed, players mode, before the first admitted frame, on the MoveWorker, an integer 0..15. */
    view_mode=VIEW_PLAYERS;assert(!view_mask_arm(2));view_installed=1;
    view_mode=VIEW_OBSERVE;assert(!view_mask_arm(2));view_mode=VIEW_PLAYERS;
    stats.issued=3;assert(!view_mask_arm(2));stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!view_mask_arm(2));ui_thread=GetCurrentThreadId();
    assert(!view_mask_arm(-1) && !view_mask_arm(16) && !view_mask_arm(1.5) && !view_mask_arm(NAN) && view_slot_mask==VIEW_MASK_ALL);
    assert(view_mask_arm(2) && view_slot_mask==2 && !view_masked);
    /* Slots 1 (a) and 2 (b) resolved, mask 2: only b is a view point; the first latch recomputes the live
     * player object's chain (zone2, which keeps its own attention) before b's (zone). */
    view_active=1;view_set_attention_override=test_vp_set_attention;test_vp_refreshed=0;
    pilot_slot_count=2;memset(pilot_slots,0,sizeof(pilot_slots));
    pilot_slots[0].resolved=1;pilot_slots[0].object=(unsigned char*)&test_vp_a;pilot_slots[0].runtime=test_vp_a.id;
    pilot_slots[1].resolved=1;pilot_slots[1].object=(unsigned char*)&test_vp_b;pilot_slots[1].runtime=test_vp_b.id;
    stats.issued=5;assert(view_frame(5) && view_count==1 && view_points[0]==&test_vp_b && view_ids[0]==test_vp_b.id && view_masked==1);
    assert(test_vp_refreshed==2 && test_vp_refresh_log[0]==&test_vp_zone2 && test_vp_refresh_log[1]==&test_vp_zone);
    assert(view_attention(&test_vp_zone)==7); /* b's level, never a's 9 */
    stats.issued=6;assert(view_frame(6) && view_count==1 && view_masked==2 && test_vp_refreshed==2); /* unchanged: no refresh */
    char status[2048];assert(view_status(status,sizeof(status))>0 && strstr(status," mask=2 masked=2"));
    /* Mask 0: no view point at all; every evaluation runs without a viewer. */
    view_latched=0;view_count=0;test_vp_refreshed=0;view_slot_mask=0;
    stats.issued=7;assert(view_frame(7) && view_count==0 && view_masked==4 && test_vp_refreshed==1 && test_vp_refresh_log[0]==&test_vp_zone2);
    assert(view_attention(&test_vp_zone)==2);
    /* All slots (the default): both, and no extra refresh of the player object at the first latch. */
    view_latched=0;view_count=0;test_vp_refreshed=0;view_slot_mask=VIEW_MASK_ALL;
    stats.issued=8;assert(view_frame(8) && view_count==2 && view_masked==4 && test_vp_refreshed==2+1);
    assert(test_vp_refresh_log[0]==&test_vp_a && test_vp_refresh_log[1]==&test_vp_zone);
    assert(view_attention(&test_vp_zone)==9);
    /* Without pilot slots bit 0 is the player object. */
    pilot_slot_count=0;view_latched=0;view_count=0;view_slot_mask=0;stats.issued=9;
    assert(view_frame(9) && view_count==0 && view_masked==5);
    view_slot_mask=1;stats.issued=10;assert(view_frame(10) && view_count==1 && view_points[0]==&test_vp_player && view_masked==5);
    view_set_attention_override=NULL;view_slot_mask=VIEW_MASK_ALL;view_masked=0;view_active=0;
    pilot_slot_count=saved_pilots;memset(pilot_slots,0,sizeof(pilot_slots));test_vp_player.parent=NULL;test_vp_objects();test_vp_reset();
    view_trigger_mode=saved_trigger;view_ui_mode=saved_ui_mode;view_rows_armed=saved_rows;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;*(void**)(image+VIEW_UNIVERSE)=NULL;
    VirtualFree(view_page,0,MEM_RELEASE);view_page=NULL;
    printf("view points mask tests PASS\n");
}

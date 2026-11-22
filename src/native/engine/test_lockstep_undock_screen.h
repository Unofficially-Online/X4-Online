/* An undock's UI events on the undocking player's screen only (lockstep_undock_screen.h): which raises go to which
 * client's UI, the counters, the stubs' place, and the pins against X4's image. No native code runs; the open scope is
 * given by the test. */
static unsigned test_udscr_raised,test_udscr_scope;
static void *test_udscr_events,*test_udscr_name;
static uint64_t test_udscr_original(void *events,void *name) {++test_udscr_raised;test_udscr_events=events;test_udscr_name=name;return 7;}
static unsigned test_udscr_scope_of(void) {return test_udscr_scope;}
static void test_undock_screen(void) {
    static unsigned char fake[0x40];char name[]="playerUndock";void *const events=(void*)fake;
    unsigned char *const saved_base=pnat_base;const unsigned saved_slot=stats.slot;
    pnat_base=fake;udscr_scope_test=test_udscr_scope_of;test_udscr_raised=0;
    memset((void*)udscr_shown,0,sizeof(udscr_shown));memset((void*)udscr_held,0,sizeof(udscr_held));
    const uintptr_t base=(uintptr_t)fake,undock=base+UDSCR_UNDOCK_RET,undocked=base+UDSCR_UNDOCKED_RET;
    assert(udscr_site(undock,base)==UDSCR_UNDOCK && udscr_site(undocked,base)==UDSCR_UNDOCKED && udscr_site(base+0x1234,base)==UDSCR_OTHER);
    /* Any other caller's event is X4's: on every client, whatever scope is open, not counted. */
    for(unsigned own=1;own<=3;own++) for(unsigned scope=0;scope<=3;scope++) {
        stats.slot=own;test_udscr_scope=scope;
        assert(udscr_raise_impl(test_udscr_original,base+0x1234,events,name)==7 && test_udscr_events==events && test_udscr_name==name);
    }
    assert(test_udscr_raised==12 && !udscr_shown[UDSCR_UNDOCK] && !udscr_held[UDSCR_UNDOCK] && !udscr_shown[UDSCR_OTHER] && !udscr_held[UDSCR_OTHER]);
    /* Slot 1's undock runs with no scope open: its client shows it, the others hold it (player 2's dock menu stays). */
    test_udscr_raised=0;test_udscr_scope=0;
    stats.slot=1;assert(udscr_raise_impl(test_udscr_original,undock,events,name)==7 && test_udscr_raised==1);
    stats.slot=2;assert(udscr_raise_impl(test_udscr_original,undock,events,name)==0 && test_udscr_raised==1);
    stats.slot=3;assert(udscr_raise_impl(test_udscr_original,undock,events,name)==0 && test_udscr_raised==1);
    /* Slot 2's undock runs in slot 2's scope: only slot 2's client shows it. The undock's end alike. */
    test_udscr_scope=2;
    stats.slot=2;assert(udscr_raise_impl(test_udscr_original,undock,events,name)==7 && test_udscr_raised==2);
    stats.slot=1;assert(udscr_raise_impl(test_udscr_original,undock,events,name)==0 && test_udscr_raised==2);
    stats.slot=1;assert(udscr_raise_impl(test_udscr_original,undocked,events,name)==0 && test_udscr_raised==2);
    stats.slot=2;assert(udscr_raise_impl(test_udscr_original,undocked,events,name)==7 && test_udscr_raised==3);
    test_udscr_scope=0;stats.slot=1;assert(udscr_raise_impl(test_udscr_original,undocked,events,name)==7 && test_udscr_raised==4);
    stats.slot=2;assert(udscr_raise_impl(test_udscr_original,undocked,events,name)==0 && test_udscr_raised==4);
    assert(udscr_shown[UDSCR_UNDOCK]==2 && udscr_held[UDSCR_UNDOCK]==3 && udscr_shown[UDSCR_UNDOCKED]==2 && udscr_held[UDSCR_UNDOCKED]==2);
    char status[160];assert(udscr_status(status,sizeof(status))>0 && !strcmp(status,"installed=0 why=- undock=2/3 undocked=2/2"));
    /* The wrapper with the test's raise in X4's place: called from here, another caller's event. */
    udscr_original_test=test_udscr_original;stats.slot=2;test_udscr_scope=0;
    assert(udscr_raise(events,name)==7 && test_udscr_raised==5);
    /* The raise's entry lies in one 16-byte block (the atomic patch writes no more); stub and trampoline in their slots
     * of the near page; each site's call inside its pinned chunk. */
    assert(((UDSCR_RAISE+4u)&~15u)==(UDSCR_RAISE&~15u) && sizeof(udscr_head)==5);
    assert(UDSCR_TRAMPOLINE==UDSCR_STUB+0x20 && UDSCR_TRAMPOLINE+0x20<=PVIEW_PAGE);
    assert(UDSCR_UNDOCK_RET-5u>=UDSCR_UNDOCK_PIN && UDSCR_UNDOCK_RET<=UDSCR_UNDOCK_PIN+UDSCR_UNDOCK_PIN_SIZE);
    assert(UDSCR_UNDOCKED_RET-5u>=UDSCR_UNDOCKED_PIN && UDSCR_UNDOCKED_RET<=UDSCR_UNDOCKED_PIN+UDSCR_UNDOCKED_PIN_SIZE);
    udscr_original_test=NULL;udscr_scope_test=NULL;pnat_base=saved_base;stats.slot=saved_slot;
    memset((void*)udscr_shown,0,sizeof(udscr_shown));memset((void*)udscr_held,0,sizeof(udscr_held));
    printf("undock screen: an undock's UI events on the undocking slot's client only; tests PASS\n");
}
/* The pins against X4's image: as shipped they hold; one byte changed in a chunk, a name, or the raise's head: refused. */
static void test_undock_screen_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *at=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(at);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(at+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    assert(udscr_pins(at));
    static const uint32_t touched[4]={UDSCR_UNDOCK_PIN+7u,UDSCR_UNDOCKED_PIN+0x30u,UDSCR_UNDOCKED_NAME+6u,UDSCR_RAISE+2u};
    for(unsigned i=0;i<4;i++) {at[touched[i]]^=0x20;assert(!udscr_pins(at));at[touched[i]]^=0x20;}
    assert(udscr_pins(at));
    VirtualFree(at,0,MEM_RELEASE);
    printf("undock screen native image: the raise, both sites and both names pinned; tests PASS\n");
}

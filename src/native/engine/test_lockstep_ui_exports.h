/* native_gate_ui_exports: query names skipped, export table entries redirected to stubs and restored, the
 * common handler passes integer, float and stack arguments through unchanged, counting by phase and relay. */
static int64_t test_ux_seen_a,test_ux_seen_c,test_ux_seen_e;static double test_ux_seen_b,test_ux_seen_d;
static int64_t test_ux_target(int64_t a,double b,int64_t c,double d,int64_t e) {
    test_ux_seen_a=a;test_ux_seen_b=b;test_ux_seen_c=c;test_ux_seen_d=d;test_ux_seen_e=e;return a+c+e;
}
typedef int64_t (*test_ux_fn)(int64_t,double,int64_t,double,int64_t);
static void test_ui_exports(void) {
    assert(ux_is_query("GetFoo") && ux_is_query("IsShip") && ux_is_query("Get") && !ux_is_query("IssueOrder") &&
           !ux_is_query("SetBar") && !ux_is_query("CreateOrder") && !ux_is_query("Getaway"));
    /* A fake image: headers, an export directory with three names, code-less function RVAs. */
    const size_t size=0x10000;unsigned char *base=VirtualAlloc(NULL,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(base);
    IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER*)base;dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
    IMAGE_NT_HEADERS64 *nt=(IMAGE_NT_HEADERS64*)(base+0x80);
    nt->FileHeader.NumberOfSections=1;nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
    IMAGE_SECTION_HEADER *text=IMAGE_FIRST_SECTION(nt);text->VirtualAddress=0x2000;text->Misc.VirtualSize=0x18; /* 0x2020 lies outside */
    text->Characteristics=IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_CNT_CODE;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress=0x1000;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size=0x200;
    IMAGE_EXPORT_DIRECTORY *e=(IMAGE_EXPORT_DIRECTORY*)(base+0x1000);
    e->NumberOfFunctions=3;e->NumberOfNames=3;e->AddressOfFunctions=0x1100;e->AddressOfNames=0x1200;e->AddressOfNameOrdinals=0x1300;
    uint32_t *functions=(uint32_t*)(base+0x1100),*names=(uint32_t*)(base+0x1200);uint16_t *ordinals=(uint16_t*)(base+0x1300);
    strcpy((char*)base+0x1400,"GetFoo");strcpy((char*)base+0x1410,"SetBar");strcpy((char*)base+0x1420,"IssueOrder");
    for(unsigned i=0;i<3;i++) {names[i]=0x1400+0x10*i;ordinals[i]=(uint16_t)i;functions[i]=0x2000+0x10*i;}
    unsigned char *prior=image;uint32_t saved_active=stats.active;int saved_phase=boot_phase;uint64_t saved_issued=stats.issued;
    unsigned char *saved_page=ux_page;ux_page=NULL;
    image=base;
    /* 0x2020 (IssueOrder) is outside the code section first: a data export, left alone. */
    assert(ux_arm_using(base,size) && ux_count==1 && functions[2]==0x2020);ux_restore();assert(functions[1]==0x2010);ux_restored=0;
    text->Misc.VirtualSize=0x1000;
    assert(ux_arm_using(base,size) && ux_installed && ux_count==2 && !strcmp(ux_stage,"armed"));
    assert(functions[0]==0x2000); /* the query stays */
    assert(functions[1]!=0x2010 && functions[2]!=0x2020 && ux_check());
    assert(!strcmp(ux_name[0],"SetBar") && !strcmp(ux_name[1],"IssueOrder") && ux_target[0]==base+0x2010);
    /* Call the second hooked export through its table entry, as GetProcAddress would hand it out. */
    ux_target[1]=(void*)test_ux_target;
    test_ux_fn call=(test_ux_fn)(void*)(base+functions[2]);
    for(unsigned i=0;i<UX_MAX;i++) ux_calls[i]=0;
    ux_boot_calls=ux_relayed=ux_rows=ux_dropped=ux_tick_rows=0;
    stats.active=1;boot_phase=2;
    assert(call(11,2.5,22,-7.25,33)==66 && test_ux_seen_b==2.5 && test_ux_seen_d==-7.25 && test_ux_seen_e==33 && ux_boot_calls==1 && !ux_calls[1]);
    boot_phase=3;stats.issued=4711;
    assert(call(1,0.5,2,0.25,3)==6 && ux_calls[1]==1 && ux_rows==1);
    char out[256];assert(ux_row_text(0,out,sizeof(out))>0 && strstr(out,"tick=4711 name=IssueOrder thread="));
    InterlockedIncrement(&ux_relay_depth);assert(call(1,0,1,0,1)==3 && ux_relayed==1 && ux_calls[1]==1);InterlockedDecrement(&ux_relay_depth);
    for(unsigned i=0;i<UX_ROWS_PER_EXPORT+2;i++) call(0,0,0,0,0);
    assert(ux_calls[1]==UX_ROWS_PER_EXPORT+3 && ux_rows==UX_ROWS_PER_EXPORT); /* rows for the first calls only */
    assert(ux_status(out,sizeof(out))>0 && strstr(out,"installed=1 hooked=2 calls=") && strstr(out," exports_called=1 boot=1 relayed=1 "));
    /* A changed entry fails the tick check; restore puts the originals back. */
    uint32_t mine=functions[1];functions[1]=0x2010;assert(!ux_check());functions[1]=mine;assert(ux_check());
    ux_restore();assert(!ux_installed && ux_restored && functions[1]==0x2010 && functions[2]==0x2020 && functions[0]==0x2000);
    /* A malformed directory refuses. */
    e->AddressOfFunctions=(DWORD)size;assert(!ux_arm_using(base,size) && !strcmp(ux_stage,"directory"));
    VirtualFree(ux_page,0,MEM_RELEASE);ux_page=saved_page;ux_count=0;ux_restored=0;ux_stage="idle";
    image=prior;stats.active=saved_active;boot_phase=saved_phase;stats.issued=saved_issued;
    VirtualFree(base,0,MEM_RELEASE);
    puts("UI exports: query names, table redirect/check/restore, argument passthrough, phase/relay counting, rows PASS");
}

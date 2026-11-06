/* native_gate_ui_exports: query names skipped, export table entries redirected to stubs and restored, the
 * common handler passes integer, float and stack arguments through unchanged, counting by phase and relay. */
static int64_t test_ux_seen_a,test_ux_seen_c,test_ux_seen_e;static double test_ux_seen_b,test_ux_seen_d;
static int64_t test_ux_target(int64_t a,double b,int64_t c,double d,int64_t e) {
    test_ux_seen_a=a;test_ux_seen_b=b;test_ux_seen_c=c;test_ux_seen_d=d;test_ux_seen_e=e;return a+c+e;
}
typedef int64_t (*test_ux_fn)(int64_t,double,int64_t,double,int64_t);
static uint64_t test_ux_portable(uint64_t runtime) {return runtime==0x77?0xabcu:0;}
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
    assert(ux_status(out,sizeof(out))>0 && strstr(out,"installed=1 mode=off hooked=2 marked=0 relay=0 captured=0 ") && strstr(out," exports_called=1 blocked=0 boot=1 relayed=1 "));
    /* 'block': a marked export is refused (returns 0, the target is not called); relayed calls still pass. */
    assert(ux_mark_block("IssueOrder",10) && !ux_mark_block("GetFoo",6) && !ux_mark_block("Issue",5));
    ux_mode=UX_OBSERVE;test_ux_seen_a=0;assert(call(5,0,5,0,5)==15 && test_ux_seen_a==5 && !ux_blocked);
    ux_mode=UX_BLOCK;test_ux_seen_a=0;assert(call(7,0,7,0,7)==0 && test_ux_seen_a==0 && ux_blocked==1);
    InterlockedIncrement(&ux_relay_depth);assert(call(1,0,1,0,1)==3 && ux_blocked==1);InterlockedDecrement(&ux_relay_depth);
    boot_phase=2;assert(call(2,0,2,0,2)==6);boot_phase=3;
    assert(ux_status(out,sizeof(out))>0 && strstr(out," mode=block hooked=2 marked=1 ") && strstr(out," blocked=1 "));
    /* Relay: a kind string makes 'block' capture the call (refused here, queued for the relay). */
    assert(ux_mark_relay("IssueOrder",10,"qdqdq",5) && !ux_mark_relay("IssueOrder",10,"qx",2) && !ux_mark_relay("Nope",4,"q",1));
    LONG64 blocked=ux_blocked;test_ux_seen_a=0;
    assert(call(0x1122,1.5,0x3344,-2.0,0x5566)==0 && test_ux_seen_a==0 && ux_blocked==blocked+1 && ux_captured==1);
    {char hex[2*UX_RECORD+1];int n=ux_take(hex,sizeof(hex));assert(n==2*(2+5*8));
     double b=1.5,d=-2.0;unsigned char want[42]={1,0};uint64_t v;
     v=0x1122;memcpy(want+2,&v,8);memcpy(want+10,&b,8);v=0x3344;memcpy(want+18,&v,8);memcpy(want+26,&d,8);v=0x5566;memcpy(want+34,&v,8);
     for(unsigned i=0;i<42;i++) {char two[3];snprintf(two,3,"%02x",want[i]);assert(!memcmp(hex+2*i,two,2));}
     assert(!ux_take(hex,sizeof(hex)));}
    /* A UniverseID becomes its portable key; an object without one refuses the call (nothing queued). */
    assert(ux_mark_relay("IssueOrder",10,"Udqdq",5));
    ux_portable_fn=test_ux_portable;
    assert(call(0x77,0,0,0,0)==0 && ux_captured==2);
    {char hex[2*UX_RECORD+1];assert(ux_take(hex,sizeof(hex))>0 && !memcmp(hex,"0100bc0a000000000000",20));}
    assert(call(0x78,0,0,0,0)==0 && ux_captured==2 && ux_unportable==1);
    ux_portable_fn=NULL;
    /* Strings: length-prefixed copies, NULL as 0xff, over-long refused. */
    {unsigned char frame[0x100];memset(frame,0,sizeof(frame));const char *text="hello";uint64_t p=(uint64_t)(uintptr_t)text;
     memcpy(frame+0x80,&p,8);memcpy(ux_kinds[0],"s",2);unsigned char out[UX_RECORD];
     assert(ux_encode(0,frame,out)==2+1+5 && out[2]==5 && !memcmp(out+3,"hello",5));
     p=0;memcpy(frame+0x80,&p,8);assert(ux_encode(0,frame,out)==3 && out[2]==0xff);
     static char longtext[UX_STRING+2];memset(longtext,'x',UX_STRING+1);p=(uint64_t)(uintptr_t)longtext;memcpy(frame+0x80,&p,8);
     assert(!ux_encode(0,frame,out) && ux_too_long==1);ux_kinds[0][0]=0;}
    assert(ux_status(out,sizeof(out))>0 && strstr(out," relay=1 captured=2 unportable=1 too_long=1 queue_full=0 "));
    ux_mode=UX_OFF;
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

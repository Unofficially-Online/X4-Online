/* Golden text of the synthetic world (test_lockstep_world_scenario.h) from the Windows
 * world hash: WORLD_IMPORTED_2 and _1 captures over three ticks with their WORLD_OBJECT
 * pages, WORLD_DETAIL rows, WORLD_BANKS lines and (FR28) WORLD_HEALTH rows. The Linux gate prints the same world in
 * its layout (linux/test_gate_linux.c) and must match this text byte for byte.
 *   test_world_golden.exe X4.exe [out.txt]
 * Needs the pinned X4.exe for the layout pins the capture verifies; the file is read into
 * owned memory, never executed. Objects live in owned memory. */
#include "lockstep_gate.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include "test_lockstep_world_scenario.h"
static unsigned checks;
#define TEST(x) do {++checks;assert(x);} while(0)
static char exported[16384];static double lua_argument,lua_result;
static double number_arg(lua_State *L,int index) {(void)L;(void)index;return lua_argument;}
static void number_result(lua_State *L,double n) {(void)L;lua_result=n;}
static void nil_result(lua_State *L) {(void)L;strcpy(exported,"nil");}
static void text_result(lua_State *L,const char *s,size_t n) {(void)L;TEST(n<sizeof(exported));memcpy(exported,s,n);exported[n]=0;}
static void *golden_find(void *map,uint64_t runtime,unsigned mode) {
    TEST(map==(void*)wscen_agent && mode==4);return wscen_find(runtime);
}
static char text[262144];static size_t text_used;
static void emit(const char *s) {size_t n=strlen(s);TEST(text_used+n+1<sizeof(text));memcpy(text+text_used,s,n);text_used+=n;text[text_used]=0;}
static void run(unsigned schema,const wscen_layout *layout) {
    char line[1024];
    wscen_build(layout);
    memset(world_identities,0,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY);world_identity_count=0;world_identity_sealed=0;
    for(unsigned i=0;i<WSCEN_IDENTITIES;i++) TEST(world_identity_record(wscen_saved_runtime[i],wscen_saved[i]));
    world_identity_seal();
    *(void**)(image+0x6cf1500)=wscen_agent;world_hash_items=NULL;world_hash_changed=NULL;world_hash_count=0;world_hash_tick=0;
    world_hash_scans=world_hash_failures=0;world_hash_watch_count=0;world_hash_schema=1;
    TEST(world_hash_watch_add("000000000000004d",16) && world_hash_watch_add("000000000000004f",16));
    world_health_find=golden_find;lua_argument=1;TEST(lua_world_health(NULL)==1 && !strcmp(exported,"nil")); /* no capture yet */
    for(unsigned tick=1;tick<=3;tick++) {
        wscen_step(tick);
        TEST(world_hash_capture_with(tick,schema,golden_find));
        snprintf(line,sizeof(line),"WORLD_HASH %s\n",world_hash_summary);emit(line);
        TEST(lua_world_banks(NULL)==1);snprintf(line,sizeof(line),"WORLD_BANKS %s\n",exported);emit(line);
        lua_argument=0;TEST(lua_world_page(NULL)==1);unsigned pages=(unsigned)lua_result;
        for(unsigned page=1;page<=pages;page++) {lua_argument=page;TEST(lua_world_page(NULL)==1 && strcmp(exported,"nil"));emit(exported);}
        lua_argument=0;TEST(lua_world_detail(NULL)==1);unsigned rows=(unsigned)lua_result;TEST(rows==2*WORLD_HASH_DOMAINS);
        for(unsigned row=1;row<=rows;row++) {lua_argument=row;TEST(lua_world_detail(NULL)==1 && strcmp(exported,"nil"));emit("WORLD_DETAIL ");emit(exported);emit("\n");}
        /* FR28: WORLD_HEALTH of both watches at this tick, then the second as if its object were gone. */
        double now=7.25+tick;memcpy(image+GAME_RVA,&now,8);stats.issued=tick;
        lua_argument=0;TEST(lua_world_health(NULL)==1 && lua_result==2);
        for(unsigned row=1;row<=2;row++) {lua_argument=row;TEST(lua_world_health(NULL)==1 && strstr(exported," present=1 "));emit("WORLD_HEALTH ");emit(exported);emit("\n");}
        lua_argument=3;TEST(lua_world_health(NULL)==1 && !strcmp(exported,"nil"));
        uint64_t kept=world_hash_watches[1].runtime;world_hash_watches[1].runtime=170; /* 84's runtime: never present */
        lua_argument=2;TEST(lua_world_health(NULL)==1 && strstr(exported," present=0 "));emit("WORLD_HEALTH ");emit(exported);emit("\n");
        world_hash_watches[1].runtime=kept;
    }
}
/* Spread census (lockstep_world_hash.h world_spread_step, native_gate_world_hash_spread): no golden text, checks only. */
static void wscen_member_note(uint64_t tick,const world_hash_item *item,unsigned char *object) {(void)tick;(void)item;(void)object;}
static void spread_reset(unsigned schema,const wscen_layout *layout) {
    wscen_build(layout);
    memset(world_identities,0,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY);world_identity_count=0;world_identity_sealed=0;
    for(unsigned i=0;i<WSCEN_IDENTITIES;i++) TEST(world_identity_record(wscen_saved_runtime[i],wscen_saved[i]));
    world_identity_seal();
    *(void**)(image+0x6cf1500)=wscen_agent;world_hash_items=NULL;world_hash_changed=NULL;world_hash_count=0;world_hash_tick=0;
    world_hash_scans=world_hash_failures=0;world_hash_watch_count=0;world_hash_schema=schema;world_capture_is_open=0;
    TEST(world_hash_watch_add("000000000000004d",16) && world_hash_watch_add("000000000000004f",16));
    TEST(world_hash_capture_with(1,schema,golden_find));   /* the probe's tick 1: always a full capture */
    wscen_step(2);
}
/* Census record (lockstep_world_hash.h world_record_text/world_record_write, native_gate_world_record): after a published
 * capture, one fixed-width row per changed item with the page's key, type, flags, parent and hash prefixes; the file of the
 * capture's slot holds the same bytes; the first capture and an open cycle write nothing. */
static void record_checks(unsigned schema,const wscen_layout *layout) {
    static char record[1u<<20];unsigned rows=0;int published=0;
    spread_reset(schema,layout);
    TEST(!world_record_write(1) && !strcmp(world_record_stage,"not_armed"));
    char dir[MAX_PATH];DWORD n=GetTempPathA(sizeof(dir),dir);TEST(n>0 && n<sizeof(dir)-40);
    char prefix[MAX_PATH];snprintf(prefix,sizeof(prefix),"%sx4lan-record-test-%lu-",dir,(unsigned long)GetCurrentProcessId());
    TEST(!world_record_arm(prefix,0) && !world_record_arm(prefix,WORLD_RECORD_KEEP_MAX+1) && world_record_arm(prefix,2));
    TEST(!world_record_write(1) && !strcmp(world_record_stage,"no_capture"));             /* the first capture: every object */
    TEST(world_spread_step(2,2,5,0,schema,golden_find,&published) && !published);
    TEST(!world_record_write(2) && !strcmp(world_record_stage,"no_capture"));             /* a cycle still open */
    for(uint64_t tick=3;tick<=5;tick++) TEST(world_spread_step(tick,2,5,0,schema,golden_find,&published));
    TEST(published && world_hash_tick==5);
    const size_t length=world_record_text(record,sizeof(record),&rows);
    TEST(length && rows==world_hash_changes && rows>0);
    char head[512];snprintf(head,sizeof(head),"WORLD_RECORD_1 tick=5 objects=%u changed=%u rows=%u state=%s\n",
        world_hash_count+world_hash_created_count,world_hash_changes,rows,world_hash_state);
    TEST(!strncmp(record,head,strlen(head)) && length==strlen(head)+(size_t)rows*WORLD_RECORD_ROW);
    for(unsigned i=0;i<rows;i++) {
        const world_hash_item *item=world_hash_item_at(world_hash_changed[i]);char want[WORLD_RECORD_ROW+1],h[WORLD_HASH_DOMAINS][65];
        for(unsigned d=0;d<WORLD_HASH_DOMAINS;d++) hex32(item->hash[d],h[d]);
        snprintf(want,sizeof(want),"%016llx %08x %08x %016llx %.16s %.16s %.16s %.16s\n",(unsigned long long)item->saved,item->type,
            item->flags,(unsigned long long)item->parent,h[0],h[1],h[2],h[3]);
        TEST(!memcmp(record+strlen(head)+(size_t)i*WORLD_RECORD_ROW,want,WORLD_RECORD_ROW));
    }
    static char back[1u<<20];unsigned none=0;
    TEST(world_record_text(back,strlen(head)+(size_t)rows*WORLD_RECORD_ROW-1,&none)==0 && !none);   /* does not fit: nothing */
    TEST(world_record_write(5) && !strcmp(world_record_stage,"written") && world_record_last_rows==world_hash_changes);
    char path[MAX_PATH+8];snprintf(path,sizeof(path),"%s%u.txt",prefix,(unsigned)((world_hash_scans-2)%2));
    FILE *f=fopen(path,"rb");TEST(f);const size_t got=fread(back,1,sizeof(back),f);fclose(f);
    TEST(got==length && !memcmp(back,record,length));
    remove(path);
    char status[256];TEST(world_record_status(status,sizeof(status))>0 && strstr(status,"keep=2 writes=") && strstr(status,"stage=written"));
    world_record_keep=0;world_record_prefix[0]=0;world_record_stage="off";
}
static void spread_checks(unsigned schema,const wscen_layout *layout) {
    char full_state[384],full_summary[768],banks[160],detail[2][2300];int published=0;
    /* A full capture at 5 of the world after step 2. */
    spread_reset(schema,layout);
    TEST(world_hash_capture_with(5,schema,golden_find));
    strcpy(full_state,world_hash_state);strcpy(full_summary,world_hash_summary);
    lua_argument=0;TEST(lua_world_page(NULL)==1);const double full_pages=lua_result;
    TEST(lua_world_banks(NULL)==1);strcpy(banks,exported);
    for(unsigned row=1;row<=2;row++) {lua_argument=row;TEST(lua_world_detail(NULL)==1);strcpy(detail[row-1],exported);}
    /* The same world read in parts over 2..5: the same state, summary, pages, banks and watch bytes, published at 5. */
    spread_reset(schema,layout);
    const uint64_t total=world_hash_count+world_hash_created_count;
    for(uint64_t tick=2;tick<=5;tick++) {
        TEST(world_spread_step(tick,2,5,0,schema,golden_find,&published));
        TEST(published==(tick==5));
        if(tick<5) TEST(world_capture_is_open && world_capture_next==(unsigned)(total*(tick-1)/4) && world_hash_tick==1);
    }
    TEST(!world_capture_is_open && world_hash_tick==5 && !strcmp(world_hash_state,full_state) && !strcmp(world_hash_summary,full_summary));
    lua_argument=0;TEST(lua_world_page(NULL)==1 && lua_result==full_pages);
    TEST(lua_world_banks(NULL)==1 && !strcmp(exported,banks));
    for(unsigned row=1;row<=2;row++) {lua_argument=row;TEST(lua_world_detail(NULL)==1 && !strcmp(exported,detail[row-1]));}
    /* A join's save tick finishes the cycle at once: published at 4 as a full capture at 4 would be. */
    spread_reset(schema,layout);
    TEST(world_hash_capture_with(4,schema,golden_find));strcpy(full_state,world_hash_state);
    spread_reset(schema,layout);
    const uint64_t early_before=world_spread_early;
    TEST(world_spread_step(3,3,9,0,schema,golden_find,&published) && !published && world_capture_next==(unsigned)(total/7));
    TEST(world_spread_step(4,3,9,1,schema,golden_find,&published) && published && !strcmp(world_hash_state,full_state));
    TEST(world_spread_early==early_before+1);
    TEST(!world_spread_step(5,3,9,0,schema,golden_find,&published) && !strcmp(world_hash_stage,"spread_cycle"));   /* no cycle open */
    /* A change between two parts: the parts read after it see it; the same schedule gives the same state every time. */
    char changed[2][384];
    for(unsigned run=0;run<2;run++) {
        spread_reset(schema,layout);
        TEST(world_spread_step(2,2,5,0,schema,golden_find,&published) && world_spread_step(3,2,5,0,schema,golden_find,&published));
        wscen_step(3);
        TEST(world_spread_step(4,2,5,0,schema,golden_find,&published) && world_spread_step(5,2,5,0,schema,golden_find,&published) && published);
        strcpy(changed[run],world_hash_state);
    }
    TEST(!strcmp(changed[0],changed[1]));
    /* Refusals: a part out of its cycle, another cycle's ticks, a part before the first, the per-item observers. */
    spread_reset(schema,layout);
    TEST(!world_spread_step(3,2,5,0,schema,golden_find,&published));                       /* no open cycle */
    TEST(world_spread_step(2,2,5,0,schema,golden_find,&published));
    TEST(!world_spread_step(3,2,6,0,schema,golden_find,&published) && world_capture_is_open); /* another publish tick */
    TEST(!world_spread_step(6,2,5,0,schema,golden_find,&published));                       /* after its publish tick */
    TEST(!world_spread_step(1,1,5,0,schema,golden_find,&published));                       /* tick 1 is a full capture */
    /* A full capture inside a cycle drops it; the next cycle reads normally. */
    const uint64_t cancels=world_spread_cancels;
    TEST(world_hash_capture_with(3,schema,golden_find) && world_spread_cancels==cancels+1 && !world_capture_is_open);
    TEST(!world_spread_step(4,2,5,0,schema,golden_find,&published));
    TEST(world_spread_step(4,4,6,0,schema,golden_find,&published) && world_spread_step(5,4,6,0,schema,golden_find,&published) &&
         world_spread_step(6,4,6,0,schema,golden_find,&published) && published && world_hash_tick==6);
    world_hash_member_note=wscen_member_note;
    TEST(!world_spread_step(7,7,9,0,schema,golden_find,&published) && !strcmp(world_hash_stage,"spread_observers") && !world_capture_is_open);
    world_hash_member_note=NULL;
    char status[320];TEST(world_spread_status(status,sizeof(status))>0 && strstr(status,"cycles=") && strstr(status," open=0 "));
    record_checks(schema,layout);
}
int main(int argc,char **argv) {
    TEST(argc>=2);
    FILE *f=fopen(argv[1],"rb");TEST(f);TEST(!fseek(f,0,SEEK_END));long length=ftell(f);TEST(length==55412768);rewind(f);
    unsigned char *file=malloc((size_t)length);TEST(file && fread(file,1,(size_t)length,f)==(size_t)length);fclose(f);
    TEST(hash_ok(file,(ULONG)length,EXE_SHA));
    IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER*)file;IMAGE_NT_HEADERS64 *nt=(IMAGE_NT_HEADERS64*)(file+dos->e_lfanew);
    image=VirtualAlloc(NULL,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);TEST(image);
    memcpy(image,file,nt->OptionalHeader.SizeOfHeaders);IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) memcpy(image+sections[i].VirtualAddress,file+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    /* Base relocations, as the loader applies them: vtable slots then point into this copy,
     * so world_class_flags decodes each class from its own IsRealClass stub. */
    IMAGE_DATA_DIRECTORY relocs=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    uint64_t delta=(uint64_t)(uintptr_t)image-nt->OptionalHeader.ImageBase;
    for(uint32_t at=0;at<relocs.Size;) {
        IMAGE_BASE_RELOCATION *block=(IMAGE_BASE_RELOCATION*)(image+relocs.VirtualAddress+at);
        TEST(block->SizeOfBlock>=8);
        const uint16_t *entry=(const uint16_t*)(block+1);
        for(unsigned i=0;i<(block->SizeOfBlock-8)/2;i++) {
            if((entry[i]>>12)==IMAGE_REL_BASED_DIR64) *(uint64_t*)(image+block->VirtualAddress+(entry[i]&0xfff))+=delta;
            else TEST((entry[i]>>12)==IMAGE_REL_BASED_ABSOLUTE);
        }
        at+=block->SizeOfBlock;
    }
    free(file);
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);TEST(world_identities);
    ui_thread=GetCurrentThreadId();p_lua_tonumber=number_arg;p_lua_pushnumber=number_result;p_lua_pushnil=nil_result;p_lua_pushlstring=text_result;
    const wscen_layout windows={
        .obj_runtime=0x8,.obj_mode=0x68,.obj_parent=0x70,.obj_state=0xd3,.obj_attention=0x1ec,.obj_controller=0x200,.obj_hull=0x280,.obj_shield=0x290,
        .sg_begin=0x10,.sg_end=0x18,.sg_rate=0x28,.sg_maximum=0x40,.sg_delay=0x48,.sg_factor=0x50,.sg_value=0x68,.sg_changed=0x70,
        .sg_source_vtable=0x90,.sg_source_group=0x98,.sg_modifiers=0xa0,
        .static_transform=0x10,.movement_transform=0x20,
        .dock_poses=0x20,.dock_time=0x60,.dock_owner=0x70,.dock_more=0x80,.dock_last=0x100,.dock_animation=0x120,.dock_flags=0x124,
        .undock_animation=0xc0,.undock_flag=0xc4,
        .nav_owner=0x28,.nav_slots=0x30,.nav_params=0x38,.nav_agent=0x40,.nav_commands=0x48,.nav_pending=0x50,.nav_time=0xc0,.nav_deadline=0xd0,
        .agent_commands=0x6e0,.agent_slots=0xad0,.agent_params=0xe10,.agent_pending=0xed0,.agent_owner=0xef0,
        .ship_vtable=image+0x2b7feb8,.turret_vtable=image+0x2b6e910,.zone_vtable=image+0x2b66080,.processor_vtable=image+0x2aaa9d8,
        .nav_vtable=image+0x2c51158,.static_vtable=image+0x2a34b38,.movement_vtable=image+0x2abd888,.dock_vtable=image+0x2c3fdc0,
        .undock_vtable=image+0x2c3e568,.other_vtable=image+0x2b17c08,.shield_source_vtable=image+0x2b19700,
        .bank_read=(uint32_t*)(image+0x38f4ee4),.bank_write=(uint32_t*)(image+0x2f4b6f4)};
    emit("# WORLD_IMPORTED_2\n");run(2,&windows);
    emit("# WORLD_IMPORTED_1\n");run(1,&windows);
    spread_checks(2,&windows);spread_checks(1,&windows);
    if(argc>=3) {FILE *out=fopen(argv[2],"wb");TEST(out);TEST(fwrite(text,1,text_used,out)==text_used);fclose(out);}
    else fputs(text,stdout);
    fprintf(stderr,"PASS world golden: %u checks, %zu bytes\n",checks,text_used);
    return 0;
}

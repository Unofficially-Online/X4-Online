/* Offline tests for the static cue path (lockstep_cue_name.h), one implementation for the hot join's static key, the
 * script RNG's cue identity and the NPC traces. A synthetic story script larger than the old name map walk's budget
 * (4,453 static cues; the walk found ~2,700 keys, the late ones read as unnamed), library copies with the same own
 * names, the raw key and the encoded text, every refusal with the join's text (counted for the modules), and the pins on
 * the pinned image. The script's name map is poisoned: no read may touch it. Synthetic cues live in owned memory. */
#define TCP_ACTS 42u
#define TCP_STEPS 100u
#define TCP_CHARS 100u
#define TCP_CUES (2u+2u*TCP_CHARS+TCP_ACTS*(1u+TCP_STEPS)+9u)
typedef struct {unsigned char b[0x1c0];} tcp_cue_t;
typedef struct {
    tcp_cue_t cue[TCP_CUES];unsigned char script[0x100],other[0x100];char names[TCP_CUES][48],raw[TCP_CUES][160];unsigned n;
    unsigned start,setup,chr,leaf,lib,lib_a,user_a,user_b,copy_a,copy_b,act0;
} tcp_story_t;
static tcp_story_t tcp_story;
static char tcp_long_name[600],tcp_spaces[101];
static uintptr_t tcp_poison_lo,tcp_poison_hi;
static unsigned tcp_poison_reads;
/* The cue path's reads, refused inside the script's name map (+0x60..+0x90). */
static int tcp_read(const void *source,void *dest,size_t bytes) {
    const uintptr_t at=(uintptr_t)source;
    if(at<tcp_poison_hi && at+bytes>tcp_poison_lo) {++tcp_poison_reads;return 0;}
    return read_local(source,dest,bytes);
}
/* An MSVC std::string as X4 holds a name: inline below 16 bytes, else behind the pointer (the text outlives it). */
static void tcp_string(unsigned char *at,const char *text) {
    uint64_t size=strlen(text),cap=size<16?15:size;memset(at,0,32);
    if(size<16) memcpy(at,text,size);else {const char *p=text;memcpy(at,&p,8);}
    memcpy(at+16,&size,8);memcpy(at+24,&cap,8);
}
static uint64_t tcp_fnv(const char *text) {return cue_fnv(CUE_FNV_BASIS,text,strlen(text));}
static unsigned char *tcp_cue(unsigned i) {return tcp_story.cue[i].b;}
/* A static cue of the story script below parent (UINT32_MAX: a root); its raw path is kept. */
static unsigned tcp_add(unsigned parent,const char *name) {
    tcp_story_t *w=&tcp_story;const unsigned i=w->n++;unsigned char *c=tcp_cue(i);assert(i<TCP_CUES);
    void *vt=image+CUE_PATH_VT,*self=c,*script=w->script,*up=parent==UINT32_MAX?NULL:tcp_cue(parent);
    memcpy(c,&vt,8);memcpy(c+CUE_PATH_DEF,&self,8);memcpy(c+CUE_PATH_SCRIPT,&script,8);memcpy(c+CUE_PATH_PARENT,&up,8);
    snprintf(w->names[i],sizeof(w->names[i]),"%s",name);tcp_string(c+CUE_PATH_NAME,w->names[i]);
    snprintf(w->raw[i],sizeof(w->raw[i]),"%s.%s",parent==UINT32_MAX?"Story_Yaki":w->raw[parent],name);
    return i;
}
static void tcp_build(void) {
    tcp_story_t *w=&tcp_story;memset(w,0,sizeof(*w));char name[48];
    tcp_string(w->script+CUE_PATH_SCRIPT_NAME,"Story_Yaki");tcp_string(w->other+CUE_PATH_SCRIPT_NAME,"Story_Paranid");
    memset(w->script+0x60,0xcd,0x30);tcp_poison_lo=(uintptr_t)w->script+0x60;tcp_poison_hi=tcp_poison_lo+0x30;
    /* Act_* first: in the name map's byte order they come before every name below, 4,242 entries of them. */
    for(unsigned k=0;k<TCP_ACTS;k++) {
        if(k==7) snprintf(name,sizeof(name),"Act %u",k);else snprintf(name,sizeof(name),"Act_%u",k);   /* one with a space */
        const unsigned act=tcp_add(UINT32_MAX,name);
        if(!k) w->act0=act;
        for(unsigned j=0;j<TCP_STEPS;j++) {snprintf(name,sizeof(name),"Act_%u_Step_%u",k,j);tcp_add(act,name);}
    }
    w->lib=tcp_add(UINT32_MAX,"Lib");w->lib_a=tcp_add(w->lib,"Sub_A");tcp_add(w->lib,"Sub_B");
    w->user_a=tcp_add(UINT32_MAX,"UserA");w->copy_a=tcp_add(w->user_a,"Sub_A");tcp_add(w->user_a,"Sub_B");  /* library copies */
    w->user_b=tcp_add(UINT32_MAX,"UserB");w->copy_b=tcp_add(w->user_b,"Sub_A");tcp_add(w->user_b,"Sub_B");
    /* j1-7's chain, Start > Setup > Setup_Character_* > PATCH_AddStoryMentors_*: past the walk's budget at every level. */
    w->start=tcp_add(UINT32_MAX,"Start");w->setup=tcp_add(w->start,"Setup");
    for(unsigned i=0;i<TCP_CHARS;i++) {
        snprintf(name,sizeof(name),"Setup_Character_%u",i);const unsigned chr=tcp_add(w->setup,name);
        snprintf(name,sizeof(name),"PATCH_AddStoryMentors_%u",i);const unsigned leaf=tcp_add(chr,name);
        if(i==TCP_CHARS-1) {w->chr=chr;w->leaf=leaf;}
    }
    assert(w->n==TCP_CUES);
}
/* raw with ' ' encoded (the only character the story's names need encoded). */
static void tcp_encoded(const char *raw,char *out,size_t capacity) {
    size_t n=0;
    for(;*raw;raw++) {if(*raw==' ') {assert(n+3<capacity);memcpy(out+n,"%20",3);n+=3;} else {assert(n+1<capacity);out[n++]=*raw;}}
    out[n]=0;
}
static int tcp_key_cmp(const void *a,const void *b) {const uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b;return x<y?-1:x>y;}
/* cue i refused with text (cue_path_with on the story's reader; the hot join the same; script_rng_md unnamed and counted
 * once, or why when it stops before the path: a static cue without a script is unreadable to it, counted there). */
static void tcp_refused_as(unsigned i,unsigned check,const char *text,unsigned why) {
    cue_path p;char got[256],out[CUE_PATH_TEXT];uint64_t key=0;join_engine e;memset(&e,0,sizeof(e));e.cue_vtable=(uintptr_t)(image+CUE_PATH_VT);
    assert(!cue_path_with(tcp_read,(uintptr_t)(image+CUE_PATH_VT),(uintptr_t)tcp_cue(i),out,sizeof(out),&p) && p.check==check);
    assert(cue_path_text(&p,got,sizeof(got))>0);
    if(strcmp(got,text)) {printf("cue path refusal: %s\n  expected %s\n",got,text);assert(0);}
    join_keys_reset();assert(!join_static_key(&e,(uintptr_t)tcp_cue(i),&key) && !strcmp(join_fault,text));
    const LONG64 before=cue_path_refusals;memset(script_rng_names,0,sizeof(script_rng_names));script_rng_name_count=0;
    script_rng_identity id;memset(&id,0,sizeof(id));id.local=(uintptr_t)tcp_cue(i);
    const unsigned got_why=script_rng_md(&id);
    if(got_why!=why || cue_path_refusals!=before+(why==SR_UNNAMED)) {printf("cue path refusal %s: script_rng_md %u, refusals %lld\n",text,got_why,(long long)(cue_path_refusals-before));assert(0);}
}
static void tcp_refused(unsigned i,unsigned check,const char *text) {tcp_refused_as(i,check,text,SR_UNNAMED);}
static void test_cue_path(void) {
    tcp_story_t *w=&tcp_story;tcp_build();
    static uint64_t keys[TCP_CUES],owners[TCP_CUES];char text[CUE_PATH_TEXT],encoded[CUE_PATH_TEXT];cue_path p;
    join_engine e;memset(&e,0,sizeof(e));e.cue_vtable=(uintptr_t)(image+CUE_PATH_VT);join_keys_reset();
    memset(script_rng_names,0,sizeof(script_rng_names));script_rng_name_count=0;cue_path_refusals=0;tcp_poison_reads=0;
    /* Every static cue: the raw key (the join's, equal), the encoded text (script_rng_md's key and name), no map read. */
    for(unsigned i=0;i<TCP_CUES;i++) {
        const uintptr_t c=(uintptr_t)tcp_cue(i);uint64_t key=0;
        assert(cue_path_with(tcp_read,(uintptr_t)(image+CUE_PATH_VT),c,text,sizeof(text),&p) && p.key==tcp_fnv(w->raw[i]));
        tcp_encoded(w->raw[i],encoded,sizeof(encoded));assert(!strcmp(text,encoded) && p.length==strlen(encoded));
        assert(join_static_key(&e,c,&key) && key==p.key);
        script_rng_identity id;memset(&id,0,sizeof(id));id.local=c;
        assert(script_rng_md(&id)==SR_OK && !id.instance && id.owner==tcp_fnv(encoded) && !strncmp(id.name,encoded,sizeof(id.name)-1));
        keys[i]=p.key;owners[i]=id.owner;
    }
    assert(!tcp_poison_reads && !cue_path_refusals);
    qsort(keys,TCP_CUES,sizeof(*keys),tcp_key_cmp);qsort(owners,TCP_CUES,sizeof(*owners),tcp_key_cmp);
    for(unsigned i=1;i<TCP_CUES;i++) assert(keys[i]!=keys[i-1] && owners[i]!=owners[i-1]);   /* every cue its own key */
    /* j1-7's leaf and the library copies by their paths; a raw key and its encoded text where a name has a space. */
    assert(!strcmp(w->raw[w->leaf],"Story_Yaki.Start.Setup.Setup_Character_99.PATCH_AddStoryMentors_99"));
    assert(!strcmp(w->raw[w->lib_a],"Story_Yaki.Lib.Sub_A") && !strcmp(w->raw[w->copy_a],"Story_Yaki.UserA.Sub_A") &&
           !strcmp(w->raw[w->copy_b],"Story_Yaki.UserB.Sub_A"));
    assert(cue_path_with(tcp_read,(uintptr_t)(image+CUE_PATH_VT),(uintptr_t)tcp_cue(w->act0+7*(1+TCP_STEPS)+3),text,sizeof(text),&p) &&
           !strcmp(text,"Story_Yaki.Act%207.Act_7_Step_2") && p.key==tcp_fnv("Story_Yaki.Act 7.Act_7_Step_2"));
    /* An instance's identity is its static cue's; an NPC trace names the same path (the exec's cue value, any module). */
    {unsigned char instance[0x1c0]={0},exec[0x50]={0};char name[256]="unreadable";void *vt=image+CUE_PATH_VT,*def=tcp_cue(w->leaf);
     memcpy(instance,&vt,8);memcpy(instance+CUE_PATH_DEF,&def,8);
     script_rng_identity id;memset(&id,0,sizeof(id));id.local=(uintptr_t)instance;
     assert(script_rng_md(&id)==SR_OK && id.instance && id.owner==tcp_fnv(w->raw[w->leaf]));
     *(uint32_t*)(exec+0x38)=0x5d;*(void**)(exec+0x40)=instance+0xb0;
     assert(npc_cue_name_with(tcp_read,exec,name,0)==1 && !strcmp(name,w->raw[w->leaf]) && npc_cue_name_with(tcp_read,exec,name,1)==2);}
    /* A nameless cue below its root (the loader makes none): "#<slot>" in its parent's sub-cue slots, raw in the key. */
    {uintptr_t slots[8]={0},vec[3];slots[5]=(uintptr_t)tcp_cue(w->chr);vec[0]=(uintptr_t)slots;vec[1]=vec[2]=(uintptr_t)(slots+8);
     memcpy(tcp_cue(w->setup)+CUE_PATH_SLOTS,vec,24);tcp_string(tcp_cue(w->chr)+CUE_PATH_NAME,"");
     assert(cue_path_with(tcp_read,(uintptr_t)(image+CUE_PATH_VT),(uintptr_t)tcp_cue(w->leaf),text,sizeof(text),&p) &&
            !strcmp(text,"Story_Yaki.Start.Setup.%235.PATCH_AddStoryMentors_99") &&
            p.key==tcp_fnv("Story_Yaki.Start.Setup.#5.PATCH_AddStoryMentors_99"));
     /* Not in its parent's slots, or the slots unreadable: refused. */
     slots[5]=0;char want[256];
     snprintf(want,sizeof(want),"check=static_slot,static=%llx,at=%llx,parent=%llx,slots=8,why=-1",CUE_P(tcp_cue(w->leaf)),CUE_P(tcp_cue(w->chr)),
              CUE_P(tcp_cue(w->setup)));
     tcp_refused(w->leaf,CUE_PATH_BAD_SLOT,want);
     vec[1]=vec[0]+4;memcpy(tcp_cue(w->setup)+CUE_PATH_SLOTS,vec,24);
     snprintf(want,sizeof(want),"check=static_slot,static=%llx,at=%llx,parent=%llx,slots=0,why=-2",CUE_P(tcp_cue(w->leaf)),CUE_P(tcp_cue(w->chr)),
              CUE_P(tcp_cue(w->setup)));
     tcp_refused(w->leaf,CUE_PATH_BAD_SLOT,want);
     memset(tcp_cue(w->setup)+CUE_PATH_SLOTS,0,24);tcp_string(tcp_cue(w->chr)+CUE_PATH_NAME,w->names[w->chr]);}
    /* Refusals: the check and its fields, the hot join's text; script_rng_md unnamed and counted. */
    {char want[256];const unsigned L=w->leaf,C=w->chr,S=w->setup,R=w->start;void *bad=image+CUE_PATH_VT+8,*good=image+CUE_PATH_VT;
     uintptr_t vl=(uintptr_t)tcp_cue(L);const LONG64 before=cue_path_refusals;
     memcpy(tcp_cue(S),&bad,8);
     snprintf(want,sizeof(want),"check=static_parent,static=%llx,at=%llx,depth=2,vtable=%llx,def=%llx,script=0,expected=%llx",CUE_P(vl),
              CUE_P(tcp_cue(S)),CUE_P(bad),CUE_P(0),CUE_P(w->script));
     tcp_refused(L,CUE_PATH_BAD_LEVEL,want);memcpy(tcp_cue(S),&good,8);
     void *other=tcp_cue(L),*self=tcp_cue(C);memcpy(tcp_cue(C)+CUE_PATH_DEF,&other,8);
     snprintf(want,sizeof(want),"check=static_parent,static=%llx,at=%llx,depth=1,vtable=%llx,def=%llx,script=0,expected=%llx",CUE_P(vl),
              CUE_P(tcp_cue(C)),CUE_P(good),CUE_P(other),CUE_P(w->script));
     tcp_refused(L,CUE_PATH_BAD_LEVEL,want);memcpy(tcp_cue(C)+CUE_PATH_DEF,&self,8);
     void *paranid=w->other,*yaki=w->script;memcpy(tcp_cue(R)+CUE_PATH_SCRIPT,&paranid,8);
     snprintf(want,sizeof(want),"check=static_parent,static=%llx,at=%llx,depth=3,vtable=%llx,def=%llx,script=%llx,expected=%llx",CUE_P(vl),
              CUE_P(tcp_cue(R)),CUE_P(good),CUE_P(tcp_cue(R)),CUE_P(paranid),CUE_P(yaki));
     tcp_refused(L,CUE_PATH_BAD_LEVEL,want);memcpy(tcp_cue(R)+CUE_PATH_SCRIPT,&yaki,8);
     void *none=NULL;memcpy(tcp_cue(L)+CUE_PATH_SCRIPT,&none,8);
     snprintf(want,sizeof(want),"check=static_script,static=%llx,script=0,size=0",CUE_P(vl));
     tcp_refused_as(L,CUE_PATH_BAD_SCRIPT,want,SR_UNREADABLE);memcpy(tcp_cue(L)+CUE_PATH_SCRIPT,&yaki,8);
     tcp_string(w->script+CUE_PATH_SCRIPT_NAME,"");
     snprintf(want,sizeof(want),"check=static_script,static=%llx,script=%llx,size=0",CUE_P(vl),CUE_P(yaki));
     tcp_refused(L,CUE_PATH_BAD_SCRIPT,want);tcp_string(w->script+CUE_PATH_SCRIPT_NAME,"Story_Yaki");
     {uint64_t cap=3;memcpy(tcp_cue(C)+CUE_PATH_NAME+24,&cap,8);}
     snprintf(want,sizeof(want),"check=static_string,static=%llx,at=%llx,size=%u",CUE_P(vl),CUE_P(tcp_cue(C)),(unsigned)strlen(w->names[C]));
     tcp_refused(L,CUE_PATH_BAD_STRING,want);tcp_string(tcp_cue(C)+CUE_PATH_NAME,w->names[C]);
     memset(tcp_long_name,'x',CUE_PATH_NAME_MAX);tcp_long_name[CUE_PATH_NAME_MAX]=0;tcp_string(tcp_cue(C)+CUE_PATH_NAME,tcp_long_name);
     snprintf(want,sizeof(want),"check=static_string,static=%llx,at=%llx,size=%u",CUE_P(vl),CUE_P(tcp_cue(C)),CUE_PATH_NAME_MAX);
     tcp_refused(L,CUE_PATH_BAD_STRING,want);
     tcp_long_name[CUE_PATH_NAME_MAX-1]=0;tcp_string(tcp_cue(C)+CUE_PATH_NAME,tcp_long_name);   /* 511 bytes: still a name */
     assert(cue_path_with(tcp_read,(uintptr_t)(image+CUE_PATH_VT),vl,NULL,0,&p) && p.key!=tcp_fnv(w->raw[L]));
     tcp_string(tcp_cue(C)+CUE_PATH_NAME,w->names[C]);
     memcpy(tcp_cue(R)+CUE_PATH_PARENT,&vl,8);
     snprintf(want,sizeof(want),"check=static_depth,static=%llx,at=%llx,depth=64",CUE_P(vl),CUE_P(vl));
     tcp_refused(L,CUE_PATH_TOO_DEEP,want);memcpy(tcp_cue(R)+CUE_PATH_PARENT,&none,8);
     tcp_string(tcp_cue(R)+CUE_PATH_NAME,"");
     snprintf(want,sizeof(want),"check=static_root,static=%llx,root=%llx,depth=4",CUE_P(vl),CUE_P(tcp_cue(R)));
     tcp_refused(L,CUE_PATH_NAMELESS_ROOT,want);tcp_string(tcp_cue(R)+CUE_PATH_NAME,"Start");
     /* The modules keep the first refusal's text and count every one (status cue_refused=, cue_refusal=). */
     assert(cue_path_refusals==before+8);
     /* A path longer than a module's text: the NPC traces' 256 bytes refuse (static_long), the script RNG's 1,024 name it. */
     memset(tcp_spaces,' ',100);tcp_spaces[100]=0;tcp_string(tcp_cue(C)+CUE_PATH_NAME,tcp_spaces);
     unsigned char exec[0x50]={0};char name[256]="unreadable";*(uint32_t*)(exec+0x38)=0x5d;*(uintptr_t*)(exec+0x40)=vl+0xb0;
     assert(!npc_cue_name_with(tcp_read,exec,name,0) && !strcmp(name,"unreadable") && cue_path_refusals==before+9);
     assert(!cue_path_with(tcp_read,(uintptr_t)(image+CUE_PATH_VT),vl,text,256,&p) && p.check==CUE_PATH_TOO_LONG && p.at==(uintptr_t)tcp_cue(C));
     script_rng_identity id;memset(&id,0,sizeof(id));id.local=vl;memset(script_rng_names,0,sizeof(script_rng_names));
     assert(script_rng_md(&id)==SR_OK && strlen(id.name)==sizeof(id.name)-1 && !strncmp(id.name,"Story_Yaki.Start.Setup.%20%20",29));
     tcp_string(tcp_cue(C)+CUE_PATH_NAME,w->names[C]);}
    assert(!tcp_poison_reads);
    memset(script_rng_names,0,sizeof(script_rng_names));script_rng_name_count=0;join_keys_reset();
    cue_path_refusals=0;snprintf(cue_path_refusal,sizeof(cue_path_refusal),"-");
    puts("static cue path: 4,453 cues of one story script (past the name map walk's budget) each named and keyed apart, the "
         "join's key the same, library copies apart, encoded text and raw key, '#<slot>', no read of the name map; refusals "
         "static_parent/script/string/depth/root/slot/long with the join's text, counted PASS");
}
/* The pinned image: the modules' live pins of the path fields are the join's file pins, each range writes the fields the
 * path reads (the constants), and one changed byte in any of them refuses. */
static void test_cue_path_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        assert(section.VirtualAddress<=nt.OptionalHeader.SizeOfImage && section.SizeOfRawData<=nt.OptionalHeader.SizeOfImage-section.VirtualAddress);
        if(section.SizeOfRawData) {assert(!fseek(file,section.PointerToRawData,SEEK_SET));assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);}
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    static const join_pin pins[]={CUE_PATH_PINS};
    assert(cue_path_pinned() && npc_layout_pinned());
    for(unsigned i=0;i<sizeof(pins)/sizeof(*pins);i++) {
        int joined=0;
        for(unsigned j=0;j<JOIN_PINS;j++) joined|=join_pin_table[j].rva==pins[i].rva && join_pin_table[j].size==pins[i].size && !strcmp(join_pin_table[j].sha,pins[i].sha);
        assert(joined);
        image[pins[i].rva+pins[i].size/2]^=1;assert(!cue_path_pinned());image[pins[i].rva+pins[i].size/2]^=1;
    }
    assert(cue_path_pinned() && CUE_PATH_VT==SCRIPT_RNG_CUE_VT && CUE_PATH_VT==JOIN_CUE_VTABLE);
    /* The writes, inside the pinned ranges: MD::Cue's vtable (XML constructor, library copy, import), the name, script,
     * parent and static cue of each, the sub-cue slots, the script's name (XML, save). */
    static const struct {unsigned rva;const char *op;unsigned n;uint32_t disp;} layout[]={
        {0x11111eb,"\x48\x8d\x0d",3,UINT32_MAX},{0x1111e4b,"\x48\x8d\x05",3,UINT32_MAX},{0x1112260,"\x48\x8d\x05",3,UINT32_MAX},
        {0x1111203,"\x4c\x8d\xa6",3,CUE_PATH_NAME},{0x1111317,"\x4c\x89\xae",3,CUE_PATH_SCRIPT},{0x1111326,"\x48\x89\x86",3,CUE_PATH_PARENT},
        {0x111144a,"\x48\x89\xb6",3,CUE_PATH_DEF},{0x111135f,"\x4c\x8d\xbe",3,CUE_PATH_SLOTS},
        {0x1111e63,"\x48\x8d\x93",3,CUE_PATH_NAME},{0x1111efd,"\x48\x89\x86",3,CUE_PATH_SCRIPT},{0x1111f04,"\x4c\x89\xb6",3,CUE_PATH_PARENT},
        {0x1112041,"\x48\x89\xb6",3,CUE_PATH_DEF},{0x111227a,"\x49\x8d\xb4\x24",4,CUE_PATH_NAME},{0x111233e,"\x4d\x89\xb4\x24",4,CUE_PATH_SCRIPT},
        {0x111234d,"\x4d\x89\x84\x24",4,CUE_PATH_PARENT},{0x11125f5,"\x4d\x89\xa4\x24",4,CUE_PATH_DEF},
        {0x1118f1d,"\x49\x8d\x8f",3,CUE_PATH_NAME},{0x110e04d,"\x48\x83\xc1",3,UINT32_MAX},{0x110e1e1,"\x48\x8d\x79",3,UINT32_MAX}};
    for(unsigned i=0;i<sizeof(layout)/sizeof(*layout);i++) {
        uint32_t disp=0;int32_t rel=0;int inside=0;memcpy(&disp,image+layout[i].rva+layout[i].n,4);memcpy(&rel,image+layout[i].rva+3,4);
        for(unsigned j=0;j<sizeof(pins)/sizeof(*pins);j++) inside|=layout[i].rva>=pins[j].rva && layout[i].rva+layout[i].n+4<=pins[j].rva+pins[j].size;
        const int vtable=layout[i].rva==0x11111eb || layout[i].rva==0x1111e4b || layout[i].rva==0x1112260;
        if(!inside || memcmp(image+layout[i].rva,layout[i].op,layout[i].n) || (layout[i].disp!=UINT32_MAX && disp!=layout[i].disp) ||
           (vtable && layout[i].rva+7+rel!=CUE_PATH_VT) ||
           (layout[i].rva>=0x110e000 && layout[i].rva<0x110f000 && image[layout[i].rva+3]!=CUE_PATH_SCRIPT_NAME)) {
            printf("cue path layout %x\n",layout[i].rva);assert(0);
        }
    }
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("static cue path native pins: the join's file pins, MD::Cue's vtable, name, script, parent, static cue and slots, "
         "the script's name, a changed byte refused PASS");
}

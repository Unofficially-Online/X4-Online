/* native_gate_swarm_seed on the pinned image: X4's own RandomizedFormationShape offset function, run from the mapped file
 * bytes (a leaf: no calls, its constants inside the image), as shipped and with the one-byte patch. Not the live game. */
typedef void *(*test_swseed_fn)(void *self,void *out,void *unused,uint64_t index,double time);
static void test_swarm_seed_native_image(const char *path) {
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
    DWORD old;assert(VirtualProtect(image+(SWSEED_FN&~0xfffu),0x1000,PAGE_EXECUTE_READWRITE,&old));
    test_swseed_fn offset=(test_swseed_fn)(image+SWSEED_FN);
    /* Two shapes of one size at two addresses: what two clients hold for the same swarm. */
    unsigned char *shapes=VirtualAlloc(NULL,0x2000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(shapes);
    unsigned char *a=shapes+0x40,*b=shapes+0x1040;const float size[4]={60.0f,40.0f,20.0f,0.0f};
    memcpy(a+0x20,size,sizeof(size));memcpy(b+0x20,size,sizeof(size));
    float oa[16],ob[16],oc[16];
    swseed_installed=0;swseed_why="-";
    assert(swseed_pin() && swseed_site()==SWSEED_ORIGINAL);
    /* As shipped: the address decides where member 3 flies. */
    offset(a,oa,NULL,3,26.0);offset(b,ob,NULL,3,26.0);assert(memcmp(oa,ob,12));
    /* Patched: the same place for both, inside the shape's size, another one for another member and another time. */
    assert(swseed_arm() && swseed_installed && swseed_site()==SWSEED_PATCHED && swseed_pin() && swseed_arm());
    offset(a,oa,NULL,3,26.0);offset(b,ob,NULL,3,26.0);assert(!memcmp(oa,ob,sizeof(oa)));
    for(unsigned i=0;i<3;i++) assert(oa[i]>=-size[i] && oa[i]<=size[i]);
    assert(oa[0]!=0.0f || oa[1]!=0.0f || oa[2]!=0.0f);
    offset(a,oc,NULL,4,26.0);assert(memcmp(oa,oc,12));
    offset(a,oc,NULL,3,27.5);assert(memcmp(oa,oc,12));offset(b,ob,NULL,3,27.5);assert(!memcmp(ob,oc,sizeof(oc)));
    char status[96];assert(swseed_status(status,sizeof(status))>0 && !strcmp(status,"installed=1 why=- site=4d"));
    /* A changed function is no longer ours to judge; stop gives X4 its byte back and the addresses decide again. */
    image[SWSEED_FN+0x40]^=1;assert(!swseed_pin());image[SWSEED_FN+0x40]^=1;assert(swseed_pin());
    swseed_restore();assert(!swseed_installed && swseed_site()==SWSEED_ORIGINAL);
    offset(a,oa,NULL,3,26.0);offset(b,ob,NULL,3,26.0);assert(memcmp(oa,ob,12));
    /* A foreign byte at the site: refused and left alone. */
    image[SWSEED_SITE]=0x90;assert(!swseed_arm() && !strcmp(swseed_why,"pin") && image[SWSEED_SITE]==0x90 && !swseed_installed);
    image[SWSEED_SITE]=SWSEED_ORIGINAL;swseed_why="-";
    VirtualFree(shapes,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);image=prior;
    puts("pinned native image swarm seed: X4's formation offset follows the shape's address as shipped, the member index and time alone when patched; pin, restore, foreign byte verified");
}

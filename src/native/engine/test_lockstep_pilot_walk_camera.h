/* A walking slot's camera (lockstep_pilot_walk_camera.h): in the scope the camera's chain is empty while the eye presents
 * a walk and every link is one of the two seated controllers; slot 1's chain back at the exit; other links, a seated
 * slot, a chain X4 changed in the scope, a long chain; slots 2 and 3 alike. The image test reads what the module relies on. */
static void test_pilot_walk_camera(void) {
    static unsigned char camera[0x900],head[0x100],cockpit[0x100],anim[0x100],links[PWCAM_LINKS+1][0x100];
    static void *vt_anchor[8],*vt_head[8],*vt_anim[8];
    static unsigned char fpc[0x40];
    const void *saved_vt[2]={pwcam_vtables[0],pwcam_vtables[1]};int saved_on=pwcam_on;unsigned char *saved_walk=peye_walk_fpc;int saved_published=peye_published;
    pwcam_reset();pwcam_vtables[0]=&vt_anchor[1];pwcam_vtables[1]=&vt_head[1];pwcam_on=1;
    void *a=&vt_anchor[1],*h=&vt_head[1],*x=&vt_anim[1],*none=NULL;
    unsigned char *link=cockpit;
    /* slot 1's seated chain: the UI anchor controller, then the cockpit head controller */
    memcpy(head,&a,8);memcpy(head+PWCAM_NEXT,&link,8);memcpy(cockpit,&h,8);memcpy(cockpit+PWCAM_NEXT,&none,8);
    link=head;memcpy(camera+PWCAM_CHAIN,&link,8);
    unsigned char *const first_link=head;   /* the chain as X4 holds it: a pointer to the first link */
    for(unsigned k=2;k<=3;k++) {   /* slots 2 and 3: the same scope rules */
        pslot_slot s;memset(&s,0,sizeof(s));s.seated=1;s.walking=1;
        peye_walk_fpc=fpc;peye_published=1;
        const LONG64 scopes=pwcam_scopes,lost0=pwcam_lost;
        pwcam_enter(camera,&s);
        unsigned char *now=NULL;memcpy(&now,camera+PWCAM_CHAIN,8);
        assert(pwcam_open && !now && pwcam_scopes==scopes+1);   /* as after X4's own stand-up: no chain */
        pwcam_leave();memcpy(&now,camera+PWCAM_CHAIN,8);
        assert(!pwcam_open && now==head && pwcam_lost==lost0);          /* slot 1's chain back, nothing unlinked */
        memcpy(&now,head+PWCAM_NEXT,8);assert(now==cockpit);
        /* seated (the eye's stage 'seat'): X4's seated path skips the chain, nothing changes */
        s.walking=0;pwcam_enter(camera,&s);memcpy(&now,camera+PWCAM_CHAIN,8);assert(!pwcam_open && now==head);
        s.walking=1;peye_walk_fpc=NULL;pwcam_enter(camera,&s);assert(!pwcam_open);peye_walk_fpc=fpc;
        peye_published=0;pwcam_enter(camera,&s);assert(!pwcam_open);peye_published=1;
        /* a cutscene's animation controller on the chain: left as it is, named */
        {   unsigned char *third=anim;memcpy(anim,&x,8);memcpy(anim+PWCAM_NEXT,&none,8);memcpy(cockpit+PWCAM_NEXT,&third,8);
            const LONG64 other=pwcam_other;pwcam_enter(camera,&s);memcpy(&now,camera+PWCAM_CHAIN,8);
            assert(!pwcam_open && now==head && pwcam_other==other+1);
            memcpy(cockpit+PWCAM_NEXT,&none,8);}
        /* X4 wrote the chain inside the scope: its value stays, counted */
        {   pwcam_enter(camera,&s);assert(pwcam_open);unsigned char *mine=anim;memcpy(camera+PWCAM_CHAIN,&mine,8);
            const LONG64 lost=pwcam_lost;pwcam_leave();memcpy(&now,camera+PWCAM_CHAIN,8);assert(now==anim && pwcam_lost==lost+1);
            memcpy(camera+PWCAM_CHAIN,&first_link,8);}
        /* off, no chain, no camera */
        pwcam_on=0;pwcam_enter(camera,&s);assert(!pwcam_open);pwcam_on=1;
        memcpy(camera+PWCAM_CHAIN,&none,8);pwcam_enter(camera,&s);assert(!pwcam_open);memcpy(camera+PWCAM_CHAIN,&first_link,8);
        pwcam_enter(NULL,&s);assert(!pwcam_open);
        (void)k;
    }
    /* a chain longer than X4's two seated links (or a loop) is not taken off */
    {   for(unsigned i=0;i<=PWCAM_LINKS;i++) {unsigned char *next=i<PWCAM_LINKS?links[i+1]:NULL;memcpy(links[i],&a,8);memcpy(links[i]+PWCAM_NEXT,&next,8);}
        unsigned char *first=links[0],*now=NULL;memcpy(camera+PWCAM_CHAIN,&first,8);
        pslot_slot s;memset(&s,0,sizeof(s));s.seated=1;s.walking=1;peye_walk_fpc=fpc;peye_published=1;
        pwcam_enter(camera,&s);memcpy(&now,camera+PWCAM_CHAIN,8);assert(!pwcam_open && now==first);}
    {char row[128];assert(pwcam_status(row,sizeof(row)) && !strncmp(row," walk_cam=1/4/3/2",17) && strstr(row," seat_cam=0/0/0"));}
    /* seated (the eye's stage 'seat'): the head link off the chain, nothing written into a link */
    {   static unsigned char hl[0x100],al[0x100];
        const int saved_seat_on=pwcam_seat_on,saved_seat_mode=peye_seat_mode;
        pwcam_reset();pwcam_seat_on=1;
        unsigned char *now=NULL,*p_hl=hl,*p_al=al;
        for(unsigned k=2;k<=3;k++) {   /* slots 2 and 3 */
            pslot_slot s;memset(&s,0,sizeof(s));s.seated=1;
            peye_walk_fpc=NULL;peye_seat_mode=1;peye_published=1;
            /* X4's seated chain: the CockpitHead link first, then the UIAnchorCamera link */
            memcpy(hl,&h,8);memcpy(hl+PWCAM_NEXT,&p_al,8);memcpy(al,&a,8);memcpy(al+PWCAM_NEXT,&none,8);memcpy(camera+PWCAM_CHAIN,&p_hl,8);
            const LONG64 seat0=pwcam_seat_scopes,whole0=pwcam_seat_whole,lost0=pwcam_lost,other0=pwcam_other;
            pwcam_enter(camera,&s);memcpy(&now,camera+PWCAM_CHAIN,8);
            assert(pwcam_open && now==al && pwcam_seat_scopes==seat0+1);   /* the chain from the head's next link */
            pwcam_leave();memcpy(&now,camera+PWCAM_CHAIN,8);assert(!pwcam_open && now==hl && pwcam_lost==lost0);
            memcpy(&now,hl+PWCAM_NEXT,8);assert(now==al);memcpy(&now,al+PWCAM_NEXT,8);assert(!now);
            /* the head link second: the whole chain off, then back */
            memcpy(al+PWCAM_NEXT,&p_hl,8);memcpy(hl+PWCAM_NEXT,&none,8);memcpy(camera+PWCAM_CHAIN,&p_al,8);
            pwcam_enter(camera,&s);memcpy(&now,camera+PWCAM_CHAIN,8);assert(pwcam_open && !now && pwcam_seat_whole==whole0+1);
            pwcam_leave();memcpy(&now,camera+PWCAM_CHAIN,8);assert(now==al);
            /* no head link: nothing to take off */
            memcpy(al+PWCAM_NEXT,&none,8);pwcam_enter(camera,&s);memcpy(&now,camera+PWCAM_CHAIN,8);assert(!pwcam_open && now==al);
            /* the head alone: the chain empty */
            memcpy(camera+PWCAM_CHAIN,&p_hl,8);pwcam_enter(camera,&s);memcpy(&now,camera+PWCAM_CHAIN,8);
            assert(pwcam_open && !now && pwcam_seat_scopes==seat0+2);pwcam_leave();memcpy(&now,camera+PWCAM_CHAIN,8);assert(now==hl);
            memcpy(hl+PWCAM_NEXT,&p_al,8);
            /* a zoom or a cutscene on the chain: left as it is */
            {   unsigned char *third=anim;memcpy(anim,&x,8);memcpy(anim+PWCAM_NEXT,&none,8);memcpy(al+PWCAM_NEXT,&third,8);
                pwcam_enter(camera,&s);memcpy(&now,camera+PWCAM_CHAIN,8);assert(!pwcam_open && now==hl && pwcam_other==other0+1);
                memcpy(al+PWCAM_NEXT,&none,8);}
            /* X4 wrote the chain in the scope: its value stays, counted */
            pwcam_enter(camera,&s);assert(pwcam_open);{unsigned char *mine=anim;memcpy(camera+PWCAM_CHAIN,&mine,8);}
            pwcam_leave();memcpy(&now,camera+PWCAM_CHAIN,8);assert(now==anim && pwcam_lost==lost0+1);memcpy(camera+PWCAM_CHAIN,&p_hl,8);
            /* off; the eye with X4's head movement (the D pose); not published: X4's chain */
            pwcam_seat_on=0;pwcam_enter(camera,&s);assert(!pwcam_open);pwcam_seat_on=1;
            peye_seat_mode=0;pwcam_enter(camera,&s);assert(!pwcam_open);peye_seat_mode=1;
            peye_published=0;pwcam_enter(camera,&s);assert(!pwcam_open);peye_published=1;
            memcpy(&now,camera+PWCAM_CHAIN,8);assert(now==hl);
            /* walking with both on: the walking rule, the whole chain */
            s.walking=1;peye_seat_mode=0;peye_walk_fpc=fpc;
            pwcam_enter(camera,&s);memcpy(&now,camera+PWCAM_CHAIN,8);assert(pwcam_open && !now);pwcam_leave();
            memcpy(&now,camera+PWCAM_CHAIN,8);assert(now==hl);
            (void)k;
        }
        {char row[128];assert(pwcam_status(row,sizeof(row)) && !strncmp(row," walk_cam=1/2/2/2",17) && strstr(row," seat_cam=1/6/2"));}   /* head first: 3 per slot */
        pwcam_seat_on=saved_seat_on;peye_seat_mode=saved_seat_mode;
    }
    pwcam_reset();pwcam_vtables[0]=saved_vt[0];pwcam_vtables[1]=saved_vt[1];pwcam_on=saved_on;peye_walk_fpc=saved_walk;peye_published=saved_published;
    printf("pilot walk camera: a walking slot's camera without slot 1's seated chain, a seated slot's without the head link, in the scope; every pointer back\n");
}
/* The image: the pinned routines, the two controllers' classes and slots, and GetOffset's test of the chain. */
static void test_pilot_walk_camera_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    unsigned count=nt.FileHeader.NumberOfSections;
    assert(fread(sections,sizeof(*sections),count,f)==count);
    static unsigned char bytes[0x400];
    for(unsigned i=0;i<PWCAM_PIN_COUNT;i++)
        assert(test_pnat_file_bytes(f,sections,count,pwcam_pin_table[i].rva,pwcam_pin_table[i].size,bytes) && hash_ok(bytes,pwcam_pin_table[i].size,pwcam_pin_table[i].sha));
    /* the two vtables: their class names (the column object's type descriptor), IsOfClass and the pose */
    #define TEST_PWCAM_CLASS(vt_va,name) do { uint64_t col_va=0;unsigned td=0;char text[64];         assert(test_pnat_file_bytes(f,sections,count,(unsigned)((vt_va)-0x140000000ULL)-8,8,bytes));memcpy(&col_va,bytes,8);         assert(test_pnat_file_bytes(f,sections,count,(unsigned)(col_va-0x140000000ULL)+12,4,bytes));memcpy(&td,bytes,4);         assert(test_pnat_file_bytes(f,sections,count,td+16,sizeof(text),(unsigned char*)text));text[sizeof(text)-1]=0;         assert(!strcmp(text,name)); } while(0)
    TEST_PWCAM_CLASS(0x140000000ULL+PWCAM_UIANCHOR_VTABLE,".?AVUIAnchorCameraMovementController@U@@");
    TEST_PWCAM_CLASS(0x140000000ULL+PWCAM_HEAD_VTABLE,".?AVCockpitHeadCameraMovementController@U@@");
    {   uint64_t v;
        assert(test_pnat_file_bytes(f,sections,count,PWCAM_UIANCHOR_VTABLE,8*8,bytes));
        memcpy(&v,bytes+8*PWCAM_ISA_SLOT,8);assert(v==0x140000000ULL+0x6f7d90u);memcpy(&v,bytes+8*PWCAM_POSE_SLOT,8);assert(v==0x140000000ULL+PWCAM_POSE_FN);
        assert(test_pnat_file_bytes(f,sections,count,PWCAM_HEAD_VTABLE,8*8,bytes));
        memcpy(&v,bytes+8*PWCAM_ISA_SLOT,8);assert(v==0x140000000ULL+0x9778e0u);memcpy(&v,bytes+8*PWCAM_POSE_SLOT,8);assert(v==0x140000000ULL+PWCAM_POSE_FN);}
    /* class 0xc and class 7: the classes 7176f0 and d89840 take off (IsOfClass: bt imm64 with the class) */
    {   uint64_t mask;assert(test_pnat_file_bytes(f,sections,count,0x6f7d90u,0x1b,bytes));
        unsigned at=0;for(;at+10<=0x1b;at++) if(bytes[at]==0x48 && bytes[at+1]==0xb9) break;
        assert(at+10<=0x1b);memcpy(&mask,bytes+at+2,8);assert(mask==0x3004001028ULL && (mask>>0xc&1));
        assert(test_pnat_file_bytes(f,sections,count,0x9778e0u,0x1b,bytes));
        for(at=0;at+10<=0x1b;at++) if(bytes[at]==0x48 && bytes[at+1]==0xb9) break;
        assert(at+10<=0x1b);memcpy(&mask,bytes+at+2,8);assert(mask==0x30040000a8ULL && (mask>>7&1));}
    /* GetOffset: cmp qword [rsi+0x20],0 / je (no chain: the anchor's pose only), then d7d2e0's answer */
    {   static const unsigned char test[]={0x48,0x83,0x7e,0x20,0x00,0x0f,0x84};
        assert(test_pnat_file_bytes(f,sections,count,0x97a413u,sizeof(test),bytes) && !memcmp(bytes,test,sizeof(test)));
        assert(test_pnat_file_bytes(f,sections,count,0x97a468u,5,bytes) && bytes[0]==0xe8);
        int32_t rel;memcpy(&rel,bytes+1,4);assert(0x97a46du+(unsigned)rel==0xd7d2e0u);}
    /* 9828a0 unlinks through the camera's +0x20 (mov rax,[rcx+0x20]) and each link's +0x80 (lea rbx,[rax+0x80]) */
    {   static const unsigned char head[]={0x48,0x8b,0x41,0x20},next[]={0x48,0x8d,0x98,0x80,0x00,0x00,0x00};
        assert(test_pnat_file_bytes(f,sections,count,0x9828b9u,sizeof(head),bytes) && !memcmp(bytes,head,sizeof(head)));
        assert(test_pnat_file_bytes(f,sections,count,0x9828e0u,sizeof(next),bytes) && !memcmp(bytes,next,sizeof(next)));}
    /* d83220, a seated head: the camera's +0x590 (movups xmm0,[rax+0x590]), then its chain (mov rsi,[rax+0x20]);
     * 574ef0, the HUD scene seated: the seat's UI anchor (two calls of 75d110), GetOffset otherwise */
    {   static const unsigned char local[]={0x0f,0x10,0x80,0x90,0x05,0x00,0x00},chain[]={0x48,0x8b,0x70,0x20};
        assert(test_pnat_file_bytes(f,sections,count,0xd83244u,sizeof(local),bytes) && !memcmp(bytes,local,sizeof(local)));
        assert(test_pnat_file_bytes(f,sections,count,0xd83280u,sizeof(chain),bytes) && !memcmp(bytes,chain,sizeof(chain)));
        static const unsigned calls[][2]={{0x57506bu,0x97a300u},{0x5750fau,0x75d110u},{0x575138u,0x75d110u}};
        for(unsigned i=0;i<3;i++) {
            int32_t rel;assert(test_pnat_file_bytes(f,sections,count,calls[i][0],5,bytes) && bytes[0]==0xe8);
            memcpy(&rel,bytes+1,4);assert(calls[i][0]+5u+(unsigned)rel==calls[i][1]);
        }}
    #undef TEST_PWCAM_CLASS
    fclose(f);
    printf("pilot walk camera image: GetOffset's chain test, the two seated controllers' classes, the seated head offset and pins\n");
}

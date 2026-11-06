/* native_gate_speech_clock: arming refusals, the one forward set under the lock, pin checks and the status. */
static void test_sclock_reset(void) {
    sclock_armed=sclock_set=0;sclock_set_tick=sclock_before_bits=0;sclock_failures=0;sclock_stage="idle";
}
/* The four clock instructions as 9.00 has them: opcode bytes and a displacement naming SCLOCK_CLOCK. */
static void test_sclock_write_sites(void) {
    for(unsigned i=0;i<SCLOCK_SITES;i++) {
        int32_t d=(int32_t)((int64_t)SCLOCK_CLOCK-(int64_t)sclock_site[i]-8);
        memcpy(image+sclock_site[i],sclock_code[i],4);memcpy(image+sclock_site[i]+4,&d,4);
    }
}
static void test_sclock_cycle(double before) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;
    InitializeCriticalSection((CRITICAL_SECTION*)(image+SCLOCK_LOCK));
    *(double*)(image+SCLOCK_CLOCK)=before;
    test_sclock_reset();stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();
    assert(sclock_arm() && sclock_armed && !sclock_set && sclock_read()==before); /* arming does not touch the clock */
    stats.issued=7;assert(sclock_tick() && sclock_set && sclock_set_tick==7 && sclock_read()==SCLOCK_VALUE);
    *(double*)(image+SCLOCK_CLOCK)=SCLOCK_VALUE+1.0/30;
    stats.issued=8;assert(sclock_tick() && sclock_read()==SCLOCK_VALUE+1.0/30); /* set once: the clock runs on */
    char status[256];assert(sclock_status(status,sizeof(status))>0 && strstr(status,"armed=1 set=1 set_tick=7 ") && strstr(status," stage=set failures=0"));
    uint64_t bits;memcpy(&bits,&before,8);assert(sclock_before_bits==bits);
    DeleteCriticalSection((CRITICAL_SECTION*)(image+SCLOCK_LOCK));memset(image+SCLOCK_LOCK,0,sizeof(CRITICAL_SECTION));
    test_sclock_reset();stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;
}
/* Hot join (lockstep_join.h join_admit_unpaused): the saver's clock at T rides the GATE record; the admission writes it and
 * marks the clock set, so the joiner's first gated frame neither sets the start again nor refuses past it. */
static void test_speech_clock_join(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;
    const uint32_t saved_version=join_rx.t.version;const uint64_t saved_unpaused=join_rx.t.gate.unpaused;
    InitializeCriticalSection((CRITICAL_SECTION*)(image+SCLOCK_LOCK));test_sclock_write_sites();
    const double session=SCLOCK_VALUE+633.0/30,own=4.25;uint64_t want,got;memcpy(&want,&session,8);
    *(double*)(image+SCLOCK_CLOCK)=session;assert(join_unpaused_get()==want);   /* the saver's capture */
    test_sclock_reset();stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();assert(sclock_arm());
    *(double*)(image+SCLOCK_CLOCK)=own;join_rx.t.version=13;join_rx.t.gate.unpaused=0;join_unpaused_before=0;
    join_admit_unpaused();assert(sclock_read()==own && !sclock_set && !join_unpaused_before);   /* not carried: untouched */
    join_rx.t.gate.unpaused=want;stats.issued=633;join_admit_unpaused();
    memcpy(&got,image+SCLOCK_CLOCK,8);assert(got==want && sclock_set && sclock_set_tick==633 && !strcmp(sclock_stage,"join"));
    memcpy(&got,&own,8);assert(join_unpaused_before==got && sclock_before_bits==got);
    stats.issued=634;assert(sclock_tick() && sclock_read()==session);   /* the first gated frame after it: no set, no refusal */
    DeleteCriticalSection((CRITICAL_SECTION*)(image+SCLOCK_LOCK));memset(image+SCLOCK_LOCK,0,sizeof(CRITICAL_SECTION));
    for(unsigned i=0;i<SCLOCK_SITES;i++) memset(image+sclock_site[i],0,8);
    memset(image+SCLOCK_CLOCK,0,8);assert(!join_unpaused_get());   /* an uninitialized lock (a mapped image): not carried */
    join_rx.t.version=saved_version;join_rx.t.gate.unpaused=saved_unpaused;join_unpaused_before=0;
    test_sclock_reset();stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;
    printf("speech clock hot join admission tests PASS\n");
}
static void test_speech_clock(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;
    test_sclock_reset();
    /* Arming refuses without an active gate, after tick 0, off the UI thread and on unpinned bytes. */
    stats.active=0;stats.issued=0;ui_thread=GetCurrentThreadId();assert(!sclock_arm());
    stats.active=1;stats.issued=1;assert(!sclock_arm());stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!sclock_arm());ui_thread=GetCurrentThreadId();
    assert(!sclock_arm() && !strcmp(sclock_stage,"pins") && !sclock_armed);
    test_sclock_write_sites();assert(sclock_pins());
    /* A displacement naming another global, or another instruction, is not the clock. */
    image[sclock_site[1]+4]^=8;assert(!sclock_pins());image[sclock_site[1]+4]^=8;
    image[sclock_site[3]+2]=0x10;assert(!sclock_pins());image[sclock_site[3]+2]=0x11;assert(sclock_pins());
    test_sclock_cycle(123.456);
    /* Forward only: a clock at or past the value (or not finite) refuses and stops. */
    InitializeCriticalSection((CRITICAL_SECTION*)(image+SCLOCK_LOCK));
    test_sclock_reset();stats.active=1;stats.issued=0;assert(sclock_arm());
    *(double*)(image+SCLOCK_CLOCK)=SCLOCK_VALUE;assert(!sclock_tick() && !strcmp(sclock_stage,"past_value") && sclock_read()==SCLOCK_VALUE && sclock_failures==1);
    assert(!sclock_tick()); /* a failure stays */
    test_sclock_reset();stats.issued=0;assert(sclock_arm());
    *(double*)(image+SCLOCK_CLOCK)=NAN;assert(!sclock_tick() && !strcmp(sclock_stage,"past_value"));
    /* A changed clock instruction after the set stops the gate. */
    test_sclock_reset();stats.issued=0;assert(sclock_arm());*(double*)(image+SCLOCK_CLOCK)=5.0;assert(sclock_tick() && sclock_set);
    image[sclock_site[2]]^=1;assert(!sclock_tick() && !strcmp(sclock_stage,"pins_changed"));image[sclock_site[2]]^=1;
    DeleteCriticalSection((CRITICAL_SECTION*)(image+SCLOCK_LOCK));memset(image+SCLOCK_LOCK,0,sizeof(CRITICAL_SECTION));
    for(unsigned i=0;i<SCLOCK_SITES;i++) memset(image+sclock_site[i],0,8);
    memset(image+SCLOCK_CLOCK,0,8);
    test_sclock_reset();stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;
    printf("speech clock arming, forward set, pins and status tests PASS\n");
    test_speech_clock_join();
}
static void test_speech_clock_native_image(const char *path) {
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
    assert(sclock_pins());
    /* The update adds the frame's dt to the clock under the lock: lea rbx,[lock] at 0xfa3315 (EnterCriticalSection(rbx)
     * follows), movsd xmm0,[clock]; addsd xmm0,xmm6; movsd [clock],xmm0 at 0xfa3352. */
    int32_t d;memcpy(&d,image+0xfa3315+3,4);
    assert(!memcmp(image+0xfa3315,"\x48\x8d\x1d",3) && 0xfa3315+7+d==(int64_t)SCLOCK_LOCK);
    assert(!memcmp(image+0xfa335a,"\xf2\x0f\x58\xc6",4));
    /* SpeakNextLine takes the start under the same lock: lea rcx,[lock] at 0x561385. */
    memcpy(&d,image+0x561385+3,4);assert(!memcmp(image+0x561385,"\x48\x8d\x0d",3) && 0x561385+7+d==(int64_t)SCLOCK_LOCK);
    test_sclock_cycle(812.25);
    for(unsigned i=0;i<SCLOCK_SITES;i++) {image[sclock_site[i]+5]^=1;assert(!sclock_pins());image[sclock_site[i]+5]^=1;}
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("speech clock native pins, the update and start reads under the lock, and the set cycle verified");
}

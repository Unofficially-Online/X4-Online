/* lockstep_periodic_save.h: the arming and the boundary with the engine's side replaced (synthetic: the checks and the save
 * are stand-ins that write a file where X4 would; nothing here shows what X4's save does). */
static int test_psave_checks_ok=1,test_psave_save_ok=1,test_psave_write=1,test_psave_saves;
static char test_psave_file[64],test_psave_title[64];
static WCHAR test_psave_dir[MAX_PATH];
static int test_psave_checks(char *why,size_t capacity) {
    if(!test_psave_checks_ok) snprintf(why,capacity,"is_saving_possible");
    return test_psave_checks_ok;
}
static int test_psave_save(const char *file,const char *title) {
    snprintf(test_psave_file,sizeof(test_psave_file),"%s",file);snprintf(test_psave_title,sizeof(test_psave_title),"%s",title);
    ++test_psave_saves;
    if(test_psave_save_ok && test_psave_write) {
        WCHAR path[MAX_PATH];_snwprintf(path,MAX_PATH,L"%ls%hs.xml.gz",test_psave_dir,file);
        static const unsigned char body[]="periodic save";
        assert(join_file_write(path,body,sizeof(body)-1));
    }
    return test_psave_save_ok;
}
static void test_periodic_save(void) {
    WCHAR temp[MAX_PATH],path[MAX_PATH];char out[256],sha[65],expected[65];
    assert(GetTempPathW(MAX_PATH,temp));
    assert(_snwprintf(test_psave_dir,MAX_PATH,L"%lsx4lan-psave-%lu\\",temp,(unsigned long)GetCurrentProcessId())>0);
    assert(CreateDirectoryW(test_psave_dir,NULL) || GetLastError()==ERROR_ALREADY_EXISTS);
    int (*const checks)(char *,size_t)=psave_checks;int (*const save)(const char *,const char *)=psave_save;
    psave_checks=test_psave_checks;psave_save=test_psave_save;memset(&psave,0,sizeof(psave));psave_saves=psave_failures=0;
    assert(psave_result(out,sizeof(out)) && !strcmp(out,"idle|0|0|-|0|-"));
    psave_boundary(5);assert(psave.state==PSAVE_IDLE && !test_psave_saves);   /* nothing armed */
    /* Armed for 1800 (the folder without its separator): nothing before it, the save right after step 1800. */
    {WCHAR folder[MAX_PATH];wcscpy(folder,test_psave_dir);folder[wcslen(folder)-1]=0;psave_arm_at(1800,0,folder,0);}
    assert(psave.state==PSAVE_ARMED && !strcmp(psave.name,"s1800") && psave.folder[wcslen(psave.folder)-1]==L'\\');
    assert(psave_result(out,sizeof(out)) && !strcmp(out,"armed|1800|0|-|0|-"));
    psave_boundary(1798);psave_boundary(1799);assert(psave.state==PSAVE_ARMED && !test_psave_saves);
    psave_boundary(1800);
    assert(psave.state==PSAVE_DONE && test_psave_saves==1 && psave_saves==1 && !psave_failures && psave.bytes==13);
    assert(!strcmp(test_psave_file,"mpsave\\s1800") && !strcmp(test_psave_title,"MP save 1800"));
    {static const unsigned char body[]="periodic save";assert(digest_hex(body,sizeof(body)-1,expected));}
    hex32(psave.sha,sha);assert(!strcmp(sha,expected));
    assert(psave_result(out,sizeof(out)) && !strncmp(out,"done|1800|13|",13) && !strncmp(out+13,expected,64) && !strcmp(out+strlen(out)-2,"|-"));
    psave_boundary(1800);psave_boundary(1801);assert(test_psave_saves==1 && psave.state==PSAVE_DONE);   /* once */
    /* Refusals: the session runs on, the result names the reason. */
    psave_arm_at(3600,0,test_psave_dir,0);test_psave_checks_ok=0;psave_boundary(3600);test_psave_checks_ok=1;
    assert(psave.state==PSAVE_FAILED && !strcmp(psave.why,"is_saving_possible") && test_psave_saves==1 && psave_failures==1);
    assert(psave_result(out,sizeof(out)) && !strcmp(out,"failed|3600|0|-|0|is_saving_possible"));
    psave_arm_at(5400,0,test_psave_dir,0);test_psave_save_ok=0;psave_boundary(5400);test_psave_save_ok=1;
    assert(psave.state==PSAVE_FAILED && !strcmp(psave.why,"engine_save") && test_psave_saves==2);
    psave_arm_at(7200,0,test_psave_dir,0);test_psave_write=0;psave_boundary(7200);test_psave_write=1;
    assert(psave.state==PSAVE_FAILED && !strcmp(psave.why,"save_file") && test_psave_saves==3);
    psave_arm_at(9000,0,test_psave_dir,0);psave_boundary(9001);   /* its tick went by without the boundary */
    assert(psave.state==PSAVE_FAILED && !strcmp(psave.why,"tick_passed") && test_psave_saves==3 && psave_failures==4 && psave_saves==1);
    /* The next arming after a failure saves again. */
    psave_arm_at(10800,1800,test_psave_dir,0);psave_boundary(10800);assert(psave.state==PSAVE_DONE && psave_saves==2 && !psave.why[0]);
    /* ...and the save named as drop went with it (a drop that is not older is none). */
    _snwprintf(path,MAX_PATH,L"%lsmpsave\\s1800.xml.gz",test_psave_dir);assert(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES);
    _snwprintf(path,MAX_PATH,L"%lsmpsave\\s10800.xml.gz",test_psave_dir);assert(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES);
    psave_arm_at(12600,12600,test_psave_dir,0);assert(!psave.drop);psave.state=PSAVE_IDLE;
    _snwprintf(path,MAX_PATH,L"%lsmpsave\\s1800.xml.gz",test_psave_dir);DeleteFileW(path);
    _snwprintf(path,MAX_PATH,L"%lsmpsave\\s10800.xml.gz",test_psave_dir);DeleteFileW(path);
    _snwprintf(path,MAX_PATH,L"%lsmpsave",test_psave_dir);RemoveDirectoryW(path);RemoveDirectoryW(test_psave_dir);
    psave_checks=checks;psave_save=save;memset(&psave,0,sizeof(psave));psave_saves=psave_failures=0;
    puts("periodic save: arming, the save after step T, once, the older save dropped; refusals named (checks, engine save, file, tick passed) PASS");
}

/* Periodic save (native_gate_save_every; play overlays: tools/build_play_overlay.py --save-every).
 * Every client of a session saves the world at the same tick boundaries while the session runs on, so that a split
 * that shows after an hour can be approached from a save a few minutes before it instead of from the session's start
 * (two idle two-client sessions of 15 Nov split near ticks 88,000 and 111,000, an hour in; a rerun from the start
 * costs that hour each time).
 * The save is the join save's (lockstep_join.h, report section 3): GameClass::Save called at the gate's frame boundary
 * after step T, on the main thread with the MoveWorker's clock, view and RNG word, after the same checks (the pinned
 * routines, no engine save pending, IsSavingPossible). No side table and no package: the file is X4's own save,
 * <save folder>/mpsave/s<T>.xml.gz, with its SHA-256 in the result.
 * A save has effects on the world that saved it (the join module carries them to the clients that do not save). Here
 * every client saves at the same T, so the effects are the same everywhere; a client that could not save says so
 * (state failed and why) and the session runs on, and that row then names a difference between the clients.
 *   gate.save_arm(T, save_folder[, drop])   UI thread, T at least 2 ticks ahead; 1 armed. drop: the tick of an older
 *                                   periodic save whose file goes once this one is written (0 or absent: none)
 *   gate.save_result()              'state|T|bytes|sha256|save_ms|why' of the last arming (idle, armed, done, failed)
 * Windows only; the Linux gate has the fork save (native_gate_fork_save), which does not stop its client. */
enum {PSAVE_IDLE,PSAVE_ARMED,PSAVE_DONE,PSAVE_FAILED};
typedef struct {
    int state;uint64_t tick,drop,bytes,save_us;uintptr_t mw_tls;WCHAR folder[MAX_PATH];char name[32],why[64];unsigned char sha[32];
} psave_state;
static psave_state psave;
static unsigned psave_saves,psave_failures;
/* The engine's side of the boundary; tests put their own in. */
static int psave_checks_native(char *why,size_t capacity) {
    if(!join_pins_check(why,capacity)) return 0;
    if(*(volatile int *)(image+JOIN_SAVE_COUNTDOWN)) {snprintf(why,capacity,"engine_save_pending");return 0;}
    if((unsigned)((join_possible_fn)(image+JOIN_SAVE_POSSIBLE))(image+JOIN_SAVE_GAME,0)!=1u) {snprintf(why,capacity,"is_saving_possible");return 0;}
    if(*(volatile unsigned char *)(image+JOIN_SAVE_MODIFIED)) *(volatile unsigned char *)(image+JOIN_SAVE_MODIFIED)=0;   /* as the join save */
    return 1;
}
static int psave_save_native(const char *file,const char *title) {return join_save_with_tls(file,title,psave.mw_tls);}
static int (*psave_checks)(char *,size_t)=psave_checks_native;
static int (*psave_save)(const char *,const char *)=psave_save_native;
static void psave_fail(const char *why) {
    psave.state=PSAVE_FAILED;snprintf(psave.why,sizeof(psave.why),"%s",why);++psave_failures;
}
/* The arming itself (folder: the profile's save folder, wide). */
static void psave_arm_at(uint64_t tick,uint64_t drop,const WCHAR *folder,uintptr_t worker_tls) {
    memset(&psave,0,sizeof(psave));
    psave.tick=tick;psave.drop=drop<tick?drop:0;psave.mw_tls=worker_tls;snprintf(psave.name,sizeof(psave.name),"s%llu",(unsigned long long)tick);
    wcsncpy(psave.folder,folder,MAX_PATH-1);
    size_t n=wcslen(psave.folder);
    if(n && n<MAX_PATH-1 && psave.folder[n-1]!=L'\\' && psave.folder[n-1]!=L'/') wcscat(psave.folder,L"\\");
    psave.state=PSAVE_ARMED;
}
static int psave_arm(uint64_t tick,const char *folder,uint64_t drop) {
    if(!stats.active || !accept_ui() || psave.state==PSAVE_ARMED || (boot_mode && boot_phase<3) || tick<=stats.issued+1 || tick>0xffffffffu) return 0;
    WCHAR wide[MAX_PATH];
    if(!folder || !join_wide(folder,wide,MAX_PATH-48)) return 0;
    psave_arm_at(tick,drop,wide,(uintptr_t)join_tls_block());
    (void)join_pins_pristine();   /* the file read here, on the UI thread; the boundary answers from it */
    if(!join_pins_baseline()) psave_fail("memory");
    return 1;
}
/* At the frame boundary after every step (beside join_boundary); the save after step T. */
static void psave_boundary(uint64_t completed) {
    if(psave.state!=PSAVE_ARMED) return;
    if(completed!=psave.tick) {if(completed>psave.tick) psave_fail("tick_passed");return;}
    LARGE_INTEGER t0,t1,f;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&t0);
    char why[64];WCHAR dir[MAX_PATH],path[MAX_PATH];
    if(!psave_checks(why,sizeof(why))) {psave_fail(why);return;}
    if(_snwprintf(dir,MAX_PATH,L"%lsmpsave",psave.folder)<0 || (!CreateDirectoryW(dir,NULL) && GetLastError()!=ERROR_ALREADY_EXISTS)) {psave_fail("mpsave_folder");return;}
    char file[48],title[48];
    snprintf(file,sizeof(file),"mpsave\\%s",psave.name);snprintf(title,sizeof(title),"MP save %llu",(unsigned long long)completed);
    const int saved=psave_save(file,title);
    QueryPerformanceCounter(&t1);psave.save_us=(uint64_t)((t1.QuadPart-t0.QuadPart)*1000000/f.QuadPart);
    if(!saved) {psave_fail("engine_save");return;}
    int found=0;
    for(int gz=1;gz>=0 && !found;gz--) {
        if(_snwprintf(path,MAX_PATH,L"%ls\\%hs.%ls",dir,psave.name,gz?L"xml.gz":L"xml")<0) break;
        found=join_file_sha(path,psave.sha,&psave.bytes);
    }
    if(!found) {psave_fail("save_file");return;}
    if(psave.drop) for(int gz=1;gz>=0;gz--)   /* the save K saves back (the launcher may have collected it already) */
        if(_snwprintf(path,MAX_PATH,L"%ls\\s%llu.%ls",dir,(unsigned long long)psave.drop,gz?L"xml.gz":L"xml")>0) DeleteFileW(path);
    psave.state=PSAVE_DONE;++psave_saves;
}
static size_t psave_result(char *out,size_t capacity) {
    static const char *const states[]={"idle","armed","done","failed"};char sha[65];
    hex32(psave.sha,sha);
    const int n=snprintf(out,capacity,"%s|%llu|%llu|%s|%llu|%s",states[psave.state],(unsigned long long)psave.tick,(unsigned long long)psave.bytes,
                         psave.state==PSAVE_DONE?sha:"-",(unsigned long long)(psave.save_us/1000u),psave.why[0]?psave.why:"-");
    return n>0 && (size_t)n<capacity?(size_t)n:0;
}

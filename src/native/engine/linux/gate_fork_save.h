/* Non-blocking save by fork (roadmap D14) on the Linux X4 9.00 executable, DEFAULT OFF
 * (native_gate_fork_save). Included by gate_linux.c only. Findings, risks and the live-test spec:
 * reports/LOCKSTEP-NONBLOCKING-SAVE-20261101.txt.
 *
 * At an agreed tick T the gate's frame hook (main thread, between two steps, FrameTick not yet called)
 * forks X4. The child writes a save of the world after T steps from its copy-on-write memory with the
 * engine's own serializer and _exits; the parent returns to the frame at once and later reaps the child.
 * A joining player loads that save and replays the relay's input journal from tick T (D14 design).
 *
 * After fork the child has only the forking thread. The serializer therefore must not wait for another
 * thread, and no lock it takes may be held by a thread that is gone. The parent forks only at a quiet
 * point, fail closed, within a budget of milliseconds:
 *  1. Thread census (/proc/self/task/<tid>/comm and /syscall): the MoveWorker (the thread that armed
 *     this, by kernel TID) is blocked in the futex of the semaphore it waits on between frames; every
 *     'JobThread #nn' in the job semaphore's; the Pathing Manager in its kick semaphore's; MassTraffic and
 *     RegionManager in their wake conditions; every other engine-named thread is blocked, not running.
 *  2. Engine predicates: no engine save pending, no ConstructionSequence job pending, no queued job, no
 *     pathing reader, no XML writer opening a file, MassTraffic and RegionManager parked.
 *  3. Held across fork (trylock, never wait): eleven XLib::Mutex (recursive pthread mutexes, kind
 *     checked) the serializer or a parked thread's path takes, then rpmalloc's 64 global spin words.
 *     The predicates and the census are repeated under the locks. The parent releases everything right
 *     after fork; the child re-initialises the mutexes (they belong to the parent's TID) and clears the
 *     spin words. glibc's fork itself makes glibc malloc and stdio consistent in the child.
 * In the child, before any engine code: default handlers for fatal signals, all signals unblocked,
 * alarm(watchdog); every inherited descriptor except read-only files and directories is replaced by
 * /dev/null with dup2 (the relay socket, the log, X11/Vulkan/audio/Steam channels), so a stray write
 * reaches no shared channel and no descriptor number is reused; the MoveWorker's thread words (time,
 * previous time, view index 1, RNG word) are copied into the child's own thread block; then, in the
 * child's private copy only: both save threading flags off (the serializer runs synchronously, as with
 * -nosavemultithreading), job queueing off (a stray QueueJob is dropped, not queued for a worker that is
 * gone), the one-time 'gameModified' dialog off, the online send and the post-save online mode off,
 * sound play off, and the folder, file and display names set (libstdc++ short strings, at most 15
 * characters; the folder is save\mpjoin\ under the profile). Then IsSavingPossible(0) and
 * GameClass::Save(0); the child hashes the file and writes fixed-size records to a pipe at every stage,
 * and _exits. It never returns into the engine and never calls exit().
 * The parent polls the pipe and waitpid each frame; past watchdog + 5 s it kills the child. It never
 * stops the session: a refused or failed fork only fails this request (the relay can ask again).
 * Records the parent's step intervals before the fork and while the child runs, and the parent's minor
 * page faults while the child runs (the copy-on-write cost). */
#include <ctype.h>
#include <dirent.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#define FS_MAGIC 0x46534156u /* 'FSAV' */
#define FS_NAME_MAX 15u      /* a libstdc++ short string */
#define FS_DIR "save\\mpjoin\\"
#define FS_SUBDIR "/mpjoin/"
#define FS_LOCKS 11
#define FS_RP_WORDS ((int)(1+LX_FS_RP_CACHES))
#define FS_FUTEX 202 /* x86_64 SYS_futex */
enum {FS_OFF,FS_ARMED,FS_RUNNING,FS_DONE,FS_FAILED};
static const char *const fs_state_names[]={"off","armed","running","done","failed"};
enum {FS_STAGE_START=1,FS_STAGE_POSSIBLE,FS_STAGE_SAVE,FS_STAGE_HASH,FS_STAGE_DONE};
typedef struct {
    uint32_t magic,stage;int32_t possible,saved,error,fds;uint32_t flags,pad;
    uint64_t bytes,save_us,child_us;unsigned char sha[32];
} fs_result;
typedef int (*fs_possible_fn)(unsigned char online);
typedef unsigned char (*fs_save_fn)(unsigned char online);
typedef struct {const char *name;unsigned char *mutex;int held;} fs_lock;
static struct {
    int state;uint64_t tick;char name[FS_NAME_MAX+1];char folder[400];unsigned budget_ms,watchdog_s;
    pid_t worker_tid;unsigned char *worker_tls;
    int pipe_fd;pid_t pid;uint64_t forked_us;int reaped,exit_code,signal_number;long child_maxrss_kb,child_minflt;
    unsigned attempts,threads,engine_threads,other_running;
    uint64_t wait_us,census_us,hold_us,fork_us;
    char reason[40],blocker[64];
    fs_result result;int have_result;
    long faults_at_fork,faults;
    uint64_t last_entry_us,before_n,before_sum,before_max,during_n,during_sum,during_max;
    uint64_t ring[64];unsigned ring_n;
} fs={.pipe_fd=-1};
static fs_lock fs_locks[FS_LOCKS];
/* The diagnostic is armed by the same UI worker that can arm a save. */
static _Atomic unsigned presentation_fork_reservation;
/* Tests replace the engine calls and the task folder; production resolves them on the image. */
static fs_possible_fn fs_possible_hook;
static fs_save_fn fs_save_hook;
static const char *fs_task_root="/proc/self/task";
static uint64_t fs_now_us(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;}
static void fs_set(char *out,size_t cap,const char *text) {snprintf(out,cap,"%s",text);}
static unsigned char *fs_pointer(uintptr_t va) {
    unsigned char *p=NULL;
    if(!read_local(LX(va),&p,sizeof(p))) return NULL;
    return p;
}
static int fs_read32(const unsigned char *at,int32_t *out) {return at && read_local(at,out,4);}
static int fs_read8(const unsigned char *at,unsigned char *out) {return at && read_local(at,out,1);}
/* XLib::Mutex: its pthread_mutexattr_t at +0, a recursive pthread_mutex_t at +8 (ctor 0x1a23520, pinned
 * x4_mutex_recursive). glibc keeps the kind at +16 of the pthread mutex. */
static int fs_mutex_ok(unsigned char *m) {
    int32_t kind;return m && fs_read32(m+8+16,&kind) && (kind&0x7f)==PTHREAD_MUTEX_RECURSIVE;
}
static void fs_locks_build(void) {
    unsigned char *csg=fs_pointer(LX_FS_CSG_VA),*jtm=fs_pointer(LX_FS_JTM_VA),*path=fs_pointer(LX_PATH_MANAGER_VA);
    unsigned char *mass=fs_pointer(LX_FS_MASS_VA),*region=fs_pointer(LX_FS_REGION_VA);
    fs_lock l[FS_LOCKS]={{"timerbase",LX(LX_FS_TIMERBASE_VA),0},{"event_queue",LX(LX_FS_EVENT_LOCK_VA),0},
        {"md_registry",LX(LX_FS_MD_REGISTRY_VA),0},{"region_lock",LX(LX_FS_REGION_LOCK_VA),0},{"xml_files",LX(LX_FS_XML_LOCK_VA),0},
        {"construction",csg?csg+8:NULL,0},{"job_queue",jtm?jtm+0x5c0:NULL,0},{"path_writer",path?path+0xf8:NULL,0},
        {"path_gate",path?path+0x128:NULL,0},{"mass_traffic",mass?mass+0x70:NULL,0},{"region_manager",region?region+0x138:NULL,0}};
    memcpy(fs_locks,l,sizeof(l));
}
static void fs_release(int words) {
    for(int i=FS_LOCKS-1;i>=0;i--) if(fs_locks[i].held) {pthread_mutex_unlock((pthread_mutex_t*)(fs_locks[i].mutex+8));fs_locks[i].held=0;}
    for(int i=0;i<words;i++) {
        uint32_t *w=(uint32_t*)LX(i?LX_FS_RP_CACHE_VA+(uintptr_t)(i-1)*LX_FS_RP_CACHE_STRIDE:LX_FS_RP_GLOBAL_VA);
        __atomic_store_n(w,0,__ATOMIC_RELEASE);
    }
}
/* Every lock or none; a busy one names itself. MassTraffic and RegionManager may be absent (null). */
static int fs_acquire(void) {
    for(int i=0;i<FS_LOCKS;i++) {
        fs_lock *l=&fs_locks[i];
        if(!l->mutex && (i==9 || i==10)) continue;
        if(!fs_mutex_ok(l->mutex)) {snprintf(fs.reason,sizeof(fs.reason),"lock_kind");fs_set(fs.blocker,sizeof(fs.blocker),l->name);fs_release(0);return 0;}
        if(pthread_mutex_trylock((pthread_mutex_t*)(l->mutex+8))) {snprintf(fs.reason,sizeof(fs.reason),"lock");fs_set(fs.blocker,sizeof(fs.blocker),l->name);fs_release(0);return 0;}
        l->held=1;
    }
    return 1;
}
static int fs_acquire_words(void) {
    for(int i=0;i<FS_RP_WORDS;i++) {
        uint32_t *w=(uint32_t*)LX(i?LX_FS_RP_CACHE_VA+(uintptr_t)(i-1)*LX_FS_RP_CACHE_STRIDE:LX_FS_RP_GLOBAL_VA),zero=0;
        if(!__atomic_compare_exchange_n(w,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) {
            for(int j=0;j<i;j++) __atomic_store_n((uint32_t*)LX(j?LX_FS_RP_CACHE_VA+(uintptr_t)(j-1)*LX_FS_RP_CACHE_STRIDE:LX_FS_RP_GLOBAL_VA),0,__ATOMIC_RELEASE);
            snprintf(fs.reason,sizeof(fs.reason),"rpmalloc");snprintf(fs.blocker,sizeof(fs.blocker),"word_%d",i);return 0;
        }
    }
    return 1;
}
/* The engine's own state at the quiet point; 0 with fs.reason set when not quiet. */
static int fs_predicates(void) {
    int32_t v;unsigned char b;
    unsigned char *csg=fs_pointer(LX_FS_CSG_VA),*jtm=fs_pointer(LX_FS_JTM_VA),*path=fs_pointer(LX_PATH_MANAGER_VA);
    unsigned char *mass=fs_pointer(LX_FS_MASS_VA),*region=fs_pointer(LX_FS_REGION_VA);
    const char *why=NULL;
    if(!fs_pointer(LX_FS_UNIVERSE_VA) || !fs_pointer(LX_FS_GAME_CLIENT_VA)) why="no_universe";
    else if(!csg || !jtm || !path) why="no_manager";
    else if(!fs_read32(LX(LX_FS_COUNTDOWN_VA),&v) || v) why="engine_save_pending";
    else {
        uint64_t first=1,last=2;
        if(!read_local(csg+0x68,&first,8) || !read_local(csg+0x70,&last,8) || first!=last) why="construction_jobs";
        else if(!fs_read32(jtm+0x5b0,&v) || v) why="jobs_queued";
        else if(!fs_read32(jtm+0x5bc,&v) || !v) why="jobs_stopped";
        else if(!fs_read32(path+0xf0,&v) || v) why="path_readers";
        else if(!fs_read8(LX(LX_FS_XML_LOCK_VA+0x48),&b) || b) why="xml_busy";
        else if(mass && (!fs_read8(mass+0xa0,&b) || b!=1)) why="mass_traffic_running";
        else if(region && (!fs_read8(region+0x168,&b) || b!=1)) why="region_manager_running";
        else if(!fs_read32(LX(LX_FS_SIM_START_VA+0x58),&v) || v) why="sim_start_posted";
    }
    if(why) {fs_set(fs.reason,sizeof(fs.reason),why);fs.blocker[0]=0;return 0;}
    return 1;
}
/* Threads: a name rule and the wait object its futex must lie in (lo==hi: any blocked state). */
typedef struct {const char *prefix;uintptr_t lo,hi;int required;unsigned seen;} fs_rule;
static int fs_read_text(const char *path,char *out,size_t cap) {
    int f=open(path,O_RDONLY|O_CLOEXEC);if(f<0) return 0;
    ssize_t n=read(f,out,cap-1);close(f);
    if(n<=0) return 0;
    out[n]=0;char *end=strchr(out,'\n');if(end) *end=0;
    return 1;
}
static int fs_census(void) {
    unsigned char *jtm=fs_pointer(LX_FS_JTM_VA),*path=fs_pointer(LX_PATH_MANAGER_VA),*mass=fs_pointer(LX_FS_MASS_VA),*region=fs_pointer(LX_FS_REGION_VA);
    uintptr_t sim=(uintptr_t)LX(LX_FS_SIM_START_VA)+0x28;
    fs_rule rules[]={
        {"Movement worker",sim,sim+0x30,1,0},
        {"JobThread #",jtm?(uintptr_t)jtm+0x558+0x28:0,jtm?(uintptr_t)jtm+0x558+0x58:0,0,0},
        {"Pathing Manager",path?(uintptr_t)path+0x70+0x28:0,path?(uintptr_t)path+0x70+0x58:0,0,0},
        {"MassTrafficUpda",mass?(uintptr_t)mass+0xd8:0,mass?(uintptr_t)mass+0xd8+0x30:0,0,0},
        {"RegionManager",region?(uintptr_t)region+0x1a0:0,region?(uintptr_t)region+0x1a0+0x30:0,0,0},
        {"Gravidar",0,0,0,0},{"NavMesh calc",0,0,0,0},{"Nav spline",0,0,0,0},{"Async File ",0,0,0,0},{"Asset Enabling",0,0,0,0},
        {"VideoAsset",0,0,0,0},{"WorkshopSync",0,0,0,0},{"DLC Sync",0,0,0,0},{"OpenTrackThread",0,0,0,0}};
    const unsigned count=sizeof(rules)/sizeof(*rules);
    DIR *d=opendir(fs_task_root);
    if(!d) {fs_set(fs.reason,sizeof(fs.reason),"census_unavailable");fs.blocker[0]=0;return 0;}
    pid_t self=(pid_t)syscall(SYS_gettid);
    fs.threads=fs.engine_threads=fs.other_running=0;
    const char *why=NULL;char blocker[64]="";
    struct dirent *e;
    while(!why && (e=readdir(d))) {
        if(e->d_name[0]<'0' || e->d_name[0]>'9') continue;
        pid_t tid=(pid_t)atoi(e->d_name);++fs.threads;
        if(tid==self) continue;
        char path_text[300],comm[64],call[256];
        snprintf(path_text,sizeof(path_text),"%s/%s/comm",fs_task_root,e->d_name);
        if(!fs_read_text(path_text,comm,sizeof(comm))) continue; /* gone meanwhile */
        snprintf(path_text,sizeof(path_text),"%s/%s/syscall",fs_task_root,e->d_name);
        int have=fs_read_text(path_text,call,sizeof(call));
        /* "running", or "-1 sp pc" for a thread preempted outside a system call: both are executing. */
        int running=!have || !strncmp(call,"running",7) || call[0]=='-';
        fs_rule *rule=NULL;
        for(unsigned i=0;i<count && !rule;i++) if(!strncmp(comm,rules[i].prefix,strlen(rules[i].prefix))) rule=&rules[i];
        if(!rule) {if(running) ++fs.other_running;continue;}
        ++fs.engine_threads;++rule->seen;
        if(rule==&rules[0] && tid!=fs.worker_tid) {why="census";snprintf(blocker,sizeof(blocker),"%.15s:%d:not_the_armed_worker",comm,(int)tid);break;}
        if(!have) {why="census_unavailable";snprintf(blocker,sizeof(blocker),"%.15s:%d",comm,(int)tid);break;}
        if(running) {why="census";snprintf(blocker,sizeof(blocker),"%.15s:%d:running",comm,(int)tid);break;}
        if(rule->lo!=rule->hi || rule==&rules[0]) {
            char *end=NULL;long nr=strtol(call,&end,10);
            unsigned long long address=end?strtoull(end,NULL,16):0;
            if(nr!=FS_FUTEX || address<rule->lo || address>=rule->hi) {
                why="census";snprintf(blocker,sizeof(blocker),"%.15s:%d:wait_%ld",comm,(int)tid,nr);break;
            }
        }
    }
    closedir(d);
    if(!why && rules[0].seen!=1) {why="census";snprintf(blocker,sizeof(blocker),"Movement worker:missing");}
    if(why) {fs_set(fs.reason,sizeof(fs.reason),why);fs_set(fs.blocker,sizeof(fs.blocker),blocker);return 0;}
    return 1;
}
/* ---- the child: nothing below returns ---- */
static void fs_child_write(int out,const fs_result *r) {
    const char *p=(const char*)r;size_t left=sizeof(*r);
    while(left) {ssize_t n=write(out,p,left);if(n<0 && errno==EINTR) continue;if(n<=0) return;p+=n;left-=(size_t)n;}
}
static int fs_child_fds(int keep) {
    int null=open("/dev/null",O_RDWR);
    if(null<0) return -1;
    struct rlimit rl;int max=4096;
    if(!getrlimit(RLIMIT_NOFILE,&rl)) max=rl.rlim_cur>65536?65536:(int)rl.rlim_cur;
    int count=0;
    for(int fd=0;fd<max;fd++) {
        if(fd==keep || fd==null) continue;
        int flags=fcntl(fd,F_GETFL);if(flags<0) continue;
        struct stat st;if(fstat(fd,&st)) continue;
        if((S_ISREG(st.st_mode) || S_ISDIR(st.st_mode)) && (flags&O_ACCMODE)==O_RDONLY) continue;
        if(fd==0 && (flags&O_ACCMODE)==O_RDONLY) continue;
        if(dup2(null,fd)==fd) ++count;
    }
    return count;
}
static void fs_child_tls(void) {
    unsigned char *mine=gate_tls_self(),*worker=fs.worker_tls;
    if(!mine || !worker || mine==worker) return;
    memcpy(mine+GATE_TLS_LINUX_TIME,worker+GATE_TLS_LINUX_TIME,8);
    memcpy(mine+GATE_TLS_LINUX_PREVIOUS_TIME,worker+GATE_TLS_LINUX_PREVIOUS_TIME,8);
    memcpy(mine+GATE_TLS_LINUX_VIEW,worker+GATE_TLS_LINUX_VIEW,4);
    memcpy(mine+GATE_TLS_LINUX_RNG,worker+GATE_TLS_LINUX_RNG,8);
}
static void fs_short_string(unsigned char *s,const char *text) {
    size_t n=strlen(text);if(n>FS_NAME_MAX) n=FS_NAME_MAX;
    unsigned char *buffer=s+16;memcpy(buffer,text,n);buffer[n]=0;
    uintptr_t p=(uintptr_t)buffer;memcpy(s,&p,8);memcpy(s+8,&n,8);
}
static uint32_t fs_child_configure(void) {
    uint32_t flags=0;
    *LX(LX_FS_FORMATTER_JOB_VA)=0;*LX(LX_FS_WRITER_THREAD_VA)=0;*LX(LX_FS_MODIFIED_WARN_VA)=0;*LX(LX_FS_ONLINE_DONE_VA)=1;
    unsigned char *jtm=fs_pointer(LX_FS_JTM_VA);
    if(jtm) {int32_t zero=0;memcpy(jtm+0x5bc,&zero,4);flags|=1;}
    unsigned char *client=fs_pointer(LX_FS_GAME_CLIENT_VA),*online=NULL,*sound=fs_pointer(LX_FS_SOUND_VA),b=0;
    if(client && read_local(client+0x3e0,&online,8) && online && fs_read8(online+0x1851c,&b) && b && fs_read8(online+0x1851d,&b) && !b) {
        online[0x1851d]=1;flags|=2;
    }
    if(sound && fs_read8(sound+0x78,&b) && b) {sound[0x78]=0;flags|=4;}
    fs_short_string(LX(LX_FS_SAVE_DIR_VA),FS_DIR);fs_short_string(LX(LX_FS_FILE_NAME_VA),fs.name);fs_short_string(LX(LX_FS_SAVE_NAME_VA),fs.name);
    return flags;
}
static int fs_child_hash(fs_result *r) {
    char file[sizeof(fs.folder)+64];
    snprintf(file,sizeof(file),"%s%s%s%s",fs.folder,FS_SUBDIR,fs.name,*LX(LX_FS_COMPRESS_VA)?".xml.gz":".xml");
    int f=open(file,O_RDONLY|O_CLOEXEC);if(f<0) {r->error=errno;return 0;}
    static unsigned char buffer[1<<16];lockstep_sha256_ctx c;lockstep_sha256_init(&c);uint64_t total=0;
    for(;;) {
        ssize_t n=read(f,buffer,sizeof(buffer));
        if(n<0 && errno==EINTR) continue;
        if(n<0) {r->error=errno;close(f);return 0;}
        if(!n) break;
        lockstep_sha256_update(&c,buffer,(size_t)n);total+=(uint64_t)n;
    }
    close(f);lockstep_sha256_final(&c,r->sha);r->bytes=total;return total>0;
}
static __attribute__((noreturn)) void fs_child(int out) {
    fs_result r;memset(&r,0,sizeof(r));r.magic=FS_MAGIC;r.stage=FS_STAGE_START;r.possible=-1;
    uint64_t start=fs_now_us();
    struct sigaction action;memset(&action,0,sizeof(action));action.sa_handler=SIG_DFL;
    static const int fatal[]={SIGSEGV,SIGBUS,SIGFPE,SIGILL,SIGABRT,SIGALRM,SIGTERM,SIGTRAP,SIGSYS,SIGXCPU,SIGXFSZ,SIGHUP,SIGINT,SIGQUIT,SIGUSR1,SIGUSR2};
    for(unsigned i=0;i<sizeof(fatal)/sizeof(*fatal);i++) sigaction(fatal[i],&action,NULL);
    action.sa_handler=SIG_IGN;sigaction(SIGPIPE,&action,NULL);
    sigset_t none;sigemptyset(&none);sigprocmask(SIG_SETMASK,&none,NULL);
    alarm(fs.watchdog_s);
    if(setpriority(PRIO_PROCESS,0,10)) r.error=errno; /* below the parent's threads for the CPU (Linux: this thread only, the child's one) */
    for(int i=0;i<FS_LOCKS;i++) if(fs_locks[i].held) pthread_mutex_init((pthread_mutex_t*)(fs_locks[i].mutex+8),(const pthread_mutexattr_t*)fs_locks[i].mutex);
    for(int i=0;i<FS_RP_WORDS;i++) *(volatile uint32_t*)LX(i?LX_FS_RP_CACHE_VA+(uintptr_t)(i-1)*LX_FS_RP_CACHE_STRIDE:LX_FS_RP_GLOBAL_VA)=0;
    r.fds=fs_child_fds(out);
    fs_child_tls();
    r.flags=fs_child_configure();
    fs_child_write(out,&r);
    fs_possible_fn possible=fs_possible_hook?fs_possible_hook:(fs_possible_fn)(void*)LX(LX_FS_POSSIBLE_VA);
    fs_save_fn save=fs_save_hook?fs_save_hook:(fs_save_fn)(void*)LX(LX_FS_SAVE_VA);
    r.stage=FS_STAGE_POSSIBLE;fs_child_write(out,&r);
    r.possible=possible(0);
    if(r.possible!=1) {r.child_us=fs_now_us()-start;fs_child_write(out,&r);_exit(3);}
    r.stage=FS_STAGE_SAVE;fs_child_write(out,&r);
    uint64_t save_start=fs_now_us();
    r.saved=save(0)!=0;
    r.save_us=fs_now_us()-save_start;
    r.stage=FS_STAGE_HASH;fs_child_write(out,&r);
    int hashed=r.saved && fs_child_hash(&r);
    r.stage=FS_STAGE_DONE;r.child_us=fs_now_us()-start;fs_child_write(out,&r);
    _exit(hashed?0:1);
}
/* ---- the parent ---- */
static long fs_minor_faults(void) {struct rusage u;return getrusage(RUSAGE_SELF,&u)?-1:u.ru_minflt;}
static void fs_finish(int state,const char *why) {
    fs.state=state;if(why) fs_set(fs.reason,sizeof(fs.reason),why);
    if(fs.pipe_fd>=0) {close(fs.pipe_fd);fs.pipe_fd=-1;}
    long now=fs_minor_faults();if(now>=0 && fs.faults_at_fork>=0) fs.faults=now-fs.faults_at_fork;
}
static void fs_poll(void) {
    if(fs.state!=FS_RUNNING) return;
    fs_result r;int eof=0;
    for(;;) {
        ssize_t n=read(fs.pipe_fd,&r,sizeof(r));
        if(n==(ssize_t)sizeof(r)) {if(r.magic==FS_MAGIC) {fs.result=r;fs.have_result=1;}continue;}
        if(n==0) eof=1;
        break; /* EAGAIN: nothing more now; a short record cannot happen below PIPE_BUF */
    }
    if(!fs.reaped) {
        int status=0;struct rusage u;memset(&u,0,sizeof(u));
        pid_t w=wait4(fs.pid,&status,WNOHANG,&u);
        if(w==fs.pid) {
            fs.reaped=1;fs.child_maxrss_kb=u.ru_maxrss;fs.child_minflt=u.ru_minflt;
            if(WIFEXITED(status)) fs.exit_code=WEXITSTATUS(status);
            if(WIFSIGNALED(status)) fs.signal_number=WTERMSIG(status);
        } else if(w<0 && errno==ECHILD) fs.reaped=2; /* reaped elsewhere (SIGCHLD ignored): the pipe decides */
    }
    if(fs.reaped && eof) {
        int ok=fs.have_result && fs.result.stage==FS_STAGE_DONE && fs.result.saved && fs.result.bytes && !fs.signal_number;
        const char *why=ok?"ok":fs.signal_number==SIGALRM?"watchdog":fs.signal_number?"child_signal":
            !fs.have_result?"no_record":fs.result.stage==FS_STAGE_POSSIBLE||fs.result.possible!=1?"not_possible":
            !fs.result.saved?"save_failed":"no_file";
        fs_finish(ok?FS_DONE:FS_FAILED,why);
        return;
    }
    if(fs_now_us()-fs.forked_us>(uint64_t)fs.watchdog_s*1000000u+5000000u) {
        if(!fs.reaped) {
            kill(fs.pid,SIGKILL);
            int status;if(waitpid(fs.pid,&status,0)==fs.pid) {fs.reaped=1;fs.signal_number=SIGKILL;}
        }
        fs_finish(FS_FAILED,"killed"); /* reaped elsewhere and no end of the pipe: never kill a pid that may be reused */
    }
}
/* All checks, then every lock and the spin words, then all checks again under them; 0 releases everything. */
static int fs_quiet(uint64_t *held) {
    if(!fs_predicates() || !fs_census()) return 0;
    fs_locks_build();
    if(!fs_acquire()) return 0;
    *held=fs_now_us();
    if(!fs_predicates() || !fs_census()) {fs_release(0);return 0;}
    if(!fs_acquire_words()) {fs_release(0);return 0;}
    return 1;
}
/* At tick fs.tick: reach the quiet point within the budget, fork, release, return. */
static void fs_attempt(void) {
    uint64_t start=fs_now_us(),census_start,held=0;
    int pipes[2];
    if(pipe2(pipes,O_CLOEXEC)) {fs_finish(FS_FAILED,"pipe");return;}
    if(fcntl(pipes[0],F_SETFL,O_NONBLOCK)) {close(pipes[0]);close(pipes[1]);fs_finish(FS_FAILED,"pipe");return;}
    for(;;) {
        ++fs.attempts;census_start=fs_now_us();
        int quiet=fs_quiet(&held);
        fs.census_us=fs_now_us()-census_start;
        if(quiet) break;
        if(fs_now_us()-start>=(uint64_t)fs.budget_ms*1000u) {close(pipes[0]);close(pipes[1]);fs.wait_us=fs_now_us()-start;fs_finish(FS_FAILED,NULL);return;}
        struct timespec pause={0,200000};nanosleep(&pause,NULL);
    }
    fs.faults_at_fork=fs_minor_faults();
    uint64_t before=fs_now_us();
    pid_t pid=fork();
    if(pid==0) {close(pipes[0]);fs_child(pipes[1]);}
    uint64_t after=fs_now_us();
    fs_release(FS_RP_WORDS);
    fs.hold_us=fs_now_us()-held;fs.fork_us=after-before;fs.wait_us=before-start;
    close(pipes[1]);
    if(pid<0) {close(pipes[0]);fs.blocker[0]=0;fs_finish(FS_FAILED,"fork");return;}
    fs.pid=pid;fs.pipe_fd=pipes[0];fs.forked_us=after;fs.state=FS_RUNNING;fs_set(fs.reason,sizeof(fs.reason),"running");fs.blocker[0]=0;
}
/* Step intervals at the gate's hook: a ring before the fork, a sum and maximum while the child runs. */
static void fs_frame_interval(void) {
    uint64_t now=fs_now_us(),interval=fs.last_entry_us?now-fs.last_entry_us:0;fs.last_entry_us=now;
    if(!interval) return;
    if(fs.state==FS_ARMED) {fs.ring[fs.ring_n++%64]=interval;return;}
    if(fs.state==FS_RUNNING) {++fs.during_n;fs.during_sum+=interval;if(interval>fs.during_max) fs.during_max=interval;}
}
/* From gate_tick, main thread, after the completed frame's bookkeeping and before its report. */
static void fork_save_frame(uint64_t completed) {
    if(fs.state==FS_OFF || fs.state==FS_DONE || fs.state==FS_FAILED) return;
    fs_frame_interval();
    if(fs.state==FS_RUNNING) {fs_poll();return;}
    if(completed<fs.tick) return;
    if(completed>fs.tick) {fs_finish(FS_FAILED,"tick_passed");return;}
    unsigned n=fs.ring_n<64?fs.ring_n:64;fs.before_n=n;fs.before_sum=fs.before_max=0;
    for(unsigned i=0;i<n;i++) {fs.before_sum+=fs.ring[i];if(fs.ring[i]>fs.before_max) fs.before_max=fs.ring[i];}
    fs_attempt();
}
static void fork_save_stop(void) {
    if(fs.state==FS_ARMED) fs_finish(FS_FAILED,"gate_stopped");
    else if(fs.state==FS_RUNNING) fs_poll();
}
static int fs_name_ok(const char *name) {
    size_t n=name?strlen(name):0;
    if(!n || n>FS_NAME_MAX) return 0;
    for(size_t i=0;i<n;i++) if(!isalnum((unsigned char)name[i]) && name[i]!='_' && name[i]!='-') return 0;
    return 1;
}
static int fs_folder_ok(const char *folder) {
    size_t n=folder?strlen(folder):0;
    return n>1 && n<sizeof(fs.folder) && folder[0]=='/' && folder[n-1]!='/' && !strstr(folder,"..") && !strpbrk(folder,"\n\r");
}
/* Arm (UI thread = MoveWorker, admitted session): records this thread's kernel TID and thread block. A
 * finished request may be followed by another one for a later tick. */
static int fork_save_arm(uint64_t tick,const char *name,const char *folder,unsigned budget_ms,unsigned watchdog_s) {
    if(!stats.active || (boot_mode && boot_phase!=3) || GetCurrentThreadId()!=ui_thread ||
       atomic_load_explicit(&presentation_fork_reservation,memory_order_acquire) ||
       fs.state==FS_ARMED || fs.state==FS_RUNNING) return 0;
    unsigned char *tls=gate_tls_self();
    if(!gate_tls_worker(tls) || tick<=stats.issued || !fs_name_ok(name) || !fs_folder_ok(folder) ||
       budget_ms<1 || budget_ms>2000 || watchdog_s<2 || watchdog_s>3600) return 0;
    memset(&fs,0,sizeof(fs));fs.pipe_fd=-1;
    fs.tick=tick;fs_set(fs.name,sizeof(fs.name),name);fs_set(fs.folder,sizeof(fs.folder),folder);
    fs.budget_ms=budget_ms;fs.watchdog_s=watchdog_s;fs.worker_tid=(pid_t)syscall(SYS_gettid);fs.worker_tls=tls;
    fs.faults_at_fork=-1;fs.state=FS_ARMED;fs_set(fs.reason,sizeof(fs.reason),"armed");
    return 1;
}
static int fork_save_status(char *out,size_t cap) {
    char sha[65]="-";if(fs.have_result && fs.result.bytes) hex32(fs.result.sha,sha);
    int n=snprintf(out,cap,"state=%s tick=%llu name=%s reason=%s blocker=%s attempts=%u threads=%u engine=%u other_running=%u "
        "wait_us=%llu census_us=%llu hold_us=%llu fork_us=%llu pid=%d stage=%u possible=%d saved=%d bytes=%llu sha=%s save_us=%llu "
        "child_us=%llu exit=%d signal=%d fds=%d flags=%u child_maxrss_kb=%ld child_minflt=%ld parent_minflt=%ld "
        "before_frames=%llu before_mean_us=%llu before_max_us=%llu during_frames=%llu during_mean_us=%llu during_max_us=%llu",
        fs_state_names[fs.state],(unsigned long long)fs.tick,fs.name[0]?fs.name:"-",fs.reason[0]?fs.reason:"-",fs.blocker[0]?fs.blocker:"-",
        fs.attempts,fs.threads,fs.engine_threads,fs.other_running,(unsigned long long)fs.wait_us,(unsigned long long)fs.census_us,
        (unsigned long long)fs.hold_us,(unsigned long long)fs.fork_us,(int)fs.pid,fs.result.stage,fs.result.possible,fs.result.saved,
        (unsigned long long)fs.result.bytes,sha,(unsigned long long)fs.result.save_us,(unsigned long long)fs.result.child_us,
        fs.exit_code,fs.signal_number,fs.result.fds,fs.result.flags,fs.child_maxrss_kb,fs.child_minflt,fs.faults,
        (unsigned long long)fs.before_n,(unsigned long long)(fs.before_n?fs.before_sum/fs.before_n:0),(unsigned long long)fs.before_max,
        (unsigned long long)fs.during_n,(unsigned long long)(fs.during_n?fs.during_sum/fs.during_n:0),(unsigned long long)fs.during_max);
    return n>0 && (size_t)n<cap?n:0;
}
/* gate.fork_save(tick, name, save_folder [, budget_ms=50 [, watchdog_s=600]]) -> 1 armed, 0 refused. */
static int lua_fork_save(lua_State *L) {
    double tick=p_lua_tonumber(L,1),budget=p_lua_tonumber(L,4),watchdog=p_lua_tonumber(L,5);
    const char *name=p_lua_tolstring(L,2,NULL),*folder=p_lua_tolstring(L,3,NULL);
    int ok=isfinite(tick) && tick>=1 && tick<=1e12 && floor(tick)==tick && (budget==0 || (budget>=1 && floor(budget)==budget)) &&
           (watchdog==0 || (watchdog>=1 && floor(watchdog)==watchdog)) && accept_ui() &&
           fork_save_arm((uint64_t)tick,name,folder,budget?(unsigned)budget:50u,watchdog?(unsigned)watchdog:600u);
    p_lua_pushnumber(L,ok);return 1;
}
/* After the gate stopped the main thread no longer polls: the UI thread reaps here. */
static int lua_fork_save_status(lua_State *L) {
    char out[1024];
    if(!stats.active && fs.state==FS_RUNNING) fs_poll();
    int n=accept_ui() || !stats.active?fork_save_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}

/* White-box tests of gate_fork_save.h on the synthetic image, included by test_gate_linux.c. Real threads
 * named as X4 names its own wait on X4-shaped semaphores, conditions and recursive mutexes inside the fake
 * image and fake managers, so the census reads the real /proc/self/task; the real fork() runs, and engine
 * stand-ins in the child check what the child must have done before the serializer runs (descriptors,
 * thread words, re-initialised locks, cleared spin words, the private flags and names), then write a file
 * the parent's record must hash. Not a native X4 run. */
#include <sys/stat.h>
#include <sys/wait.h>
#define TFS_TICK 40u
static unsigned char *tfs_csg,*tfs_jtm,*tfs_path,*tfs_mass,*tfs_region,*tfs_client,*tfs_online,*tfs_sound;
static int tfs_possible_value=1,tfs_hang,tfs_relay[2]={-1,-1};
static char tfs_folder[256];
static void tfs_put_ptr(uintptr_t va,void *p) {memcpy(LX(va),&p,8);}
static void tfs_xmutex(unsigned char *m) {
    pthread_mutexattr_init((pthread_mutexattr_t*)m);pthread_mutexattr_settype((pthread_mutexattr_t*)m,PTHREAD_MUTEX_RECURSIVE);
    assert(!pthread_mutex_init((pthread_mutex_t*)(m+8),(pthread_mutexattr_t*)m));
}
/* X4's XLib::Semaphore: pthread mutex +0, condition +0x28, count +0x58 (pinned fs_sem_wait). */
static void tfs_sem_init(unsigned char *s) {
    assert(!pthread_mutex_init((pthread_mutex_t*)s,NULL) && !pthread_cond_init((pthread_cond_t*)(s+0x28),NULL));memset(s+0x58,0,8);
}
static void tfs_sem_wait(unsigned char *s) {
    pthread_mutex_lock((pthread_mutex_t*)s);
    while(!*(volatile int32_t*)(s+0x58)) pthread_cond_wait((pthread_cond_t*)(s+0x28),(pthread_mutex_t*)s);
    --*(int32_t*)(s+0x58);pthread_mutex_unlock((pthread_mutex_t*)s);
}
static void tfs_sem_post(unsigned char *s) {
    pthread_mutex_lock((pthread_mutex_t*)s);++*(int32_t*)(s+0x58);pthread_cond_signal((pthread_cond_t*)(s+0x28));pthread_mutex_unlock((pthread_mutex_t*)s);
}
static unsigned char *tfs_object(size_t size) {unsigned char *p=aligned_alloc(64,size);assert(p);memset(p,0,size);return p;}
static unsigned char *tfs_xmutexes[FS_LOCKS];
static void tfs_world(void) {
    tfs_csg=tfs_object(0x100);tfs_jtm=tfs_object(0x700);tfs_path=tfs_object(0x200);tfs_mass=tfs_object(0x140);tfs_region=tfs_object(0x200);
    tfs_client=tfs_object(0x400);tfs_online=tfs_object(0x18600);tfs_sound=tfs_object(0x100);
    tfs_put_ptr(LX_FS_CSG_VA,tfs_csg);tfs_put_ptr(LX_FS_JTM_VA,tfs_jtm);tfs_put_ptr(LX_PATH_MANAGER_VA,tfs_path);tfs_put_ptr(LX_FS_MASS_VA,tfs_mass);
    tfs_put_ptr(LX_FS_REGION_VA,tfs_region);tfs_put_ptr(LX_FS_GAME_CLIENT_VA,tfs_client);tfs_put_ptr(LX_FS_SOUND_VA,tfs_sound);
    tfs_put_ptr(LX_FS_UNIVERSE_VA,tfs_csg); /* any object */
    memcpy(tfs_client+0x3e0,&tfs_online,8);tfs_online[0x1851c]=1;tfs_sound[0x78]=1;
    unsigned char *m[FS_LOCKS]={LX(LX_FS_TIMERBASE_VA),LX(LX_FS_EVENT_LOCK_VA),LX(LX_FS_MD_REGISTRY_VA),LX(LX_FS_REGION_LOCK_VA),LX(LX_FS_XML_LOCK_VA),
        tfs_csg+8,tfs_jtm+0x5c0,tfs_path+0xf8,tfs_path+0x128,tfs_mass+0x70,tfs_region+0x138};
    for(int i=0;i<FS_LOCKS;i++) {tfs_xmutexes[i]=m[i];tfs_xmutex(m[i]);}
    tfs_sem_init(LX(LX_FS_SIM_START_VA));tfs_sem_init(tfs_jtm+0x558);tfs_sem_init(tfs_path+0x70);
    int32_t running=1;memcpy(tfs_jtm+0x5bc,&running,4);
    assert(!pthread_cond_init((pthread_cond_t*)(tfs_mass+0xd8),NULL) && !pthread_cond_init((pthread_cond_t*)(tfs_region+0x1a0),NULL));
    *LX(LX_FS_FORMATTER_JOB_VA)=1;*LX(LX_FS_WRITER_THREAD_VA)=1;*LX(LX_FS_MODIFIED_WARN_VA)=1;*LX(LX_FS_ONLINE_DONE_VA)=0;*LX(LX_FS_COMPRESS_VA)=1;
    fs_short_string(LX(LX_FS_SAVE_DIR_VA),"save\\");fs_short_string(LX(LX_FS_FILE_NAME_VA),"");fs_short_string(LX(LX_FS_SAVE_NAME_VA),"");
}
/* Engine threads. */
enum {TFS_SEM,TFS_PARK,TFS_SPIN,TFS_SLEEP,TFS_WORKER};
typedef struct {const char *name;int kind;unsigned char *object;unsigned mutex_at,flag_at,cond_at;volatile int quit,started;pid_t tid;pthread_t thread;} tfs_thread;
static volatile int tfs_worker_command; /* 1: arm, 2: quit */
static volatile int tfs_worker_armed;
static struct {uint64_t tick;const char *name;unsigned budget_ms,watchdog_s;} tfs_arm;
static void *tfs_thread_main(void *arg) {
    tfs_thread *t=arg;pthread_setname_np(pthread_self(),t->name);t->tid=(pid_t)syscall(SYS_gettid);
    __atomic_store_n(&t->started,1,__ATOMIC_SEQ_CST);
    if(t->kind==TFS_SEM) {for(;;) {tfs_sem_wait(t->object);if(t->quit) return NULL;}}
    if(t->kind==TFS_PARK) {
        pthread_mutex_t *m=(pthread_mutex_t*)(t->object+t->mutex_at+8);
        pthread_mutex_lock(m);t->object[t->flag_at]=1;
        while(!t->quit) pthread_cond_wait((pthread_cond_t*)(t->object+t->cond_at),m);
        pthread_mutex_unlock(m);return NULL;
    }
    if(t->kind==TFS_SPIN) {while(!t->quit) __asm__ volatile("pause");return NULL;}
    if(t->kind==TFS_SLEEP) {while(!t->quit) {struct timespec p={0,50000000};nanosleep(&p,NULL);}return NULL;}
    /* The MoveWorker: its thread words, then it arms from Lua's thread and parks between frames. */
    unsigned char *mine=gate_tls_self();int32_t view=1;double time=123.5,previous=123.0;uint64_t rng=0xabcull;
    memcpy(mine+GATE_TLS_LINUX_VIEW,&view,4);memcpy(mine+GATE_TLS_LINUX_TIME,&time,8);memcpy(mine+GATE_TLS_LINUX_PREVIOUS_TIME,&previous,8);
    memcpy(mine+GATE_TLS_LINUX_RNG,&rng,8);
    for(;;) {
        tfs_sem_wait(LX(LX_FS_SIM_START_VA));
        if(tfs_worker_command==2) return NULL;
        if(tfs_worker_command==1) {
            ui_thread=GetCurrentThreadId();
            tfs_worker_armed=fork_save_arm(tfs_arm.tick,tfs_arm.name,tfs_folder,tfs_arm.budget_ms,tfs_arm.watchdog_s);
            /* Refusals: another thread's call, names, folders, ticks, budgets. */
            tfs_worker_command=0;
        }
    }
}
static int tfs_waiting_in(pid_t tid,uintptr_t lo,uintptr_t hi) {
    char path[96],call[256];snprintf(path,sizeof(path),"/proc/self/task/%d/syscall",(int)tid);
    if(!fs_read_text(path,call,sizeof(call)) || !strncmp(call,"running",7)) return 0;
    char *end;long nr=strtol(call,&end,10);unsigned long long address=strtoull(end,NULL,16);
    return lo==hi?1:nr==FS_FUTEX && address>=lo && address<hi;
}
static void tfs_wait_blocked(tfs_thread *t,uintptr_t lo,uintptr_t hi) {
    while(!__atomic_load_n(&t->started,__ATOMIC_SEQ_CST)) sched_yield();
    for(int i=0;i<5000 && !tfs_waiting_in(t->tid,lo,hi);i++) {struct timespec p={0,1000000};nanosleep(&p,NULL);}
    assert(tfs_waiting_in(t->tid,lo,hi));
}
static void tfs_start(tfs_thread *t) {assert(!pthread_create(&t->thread,NULL,tfs_thread_main,t));}
static uintptr_t tfs_cond_lo(tfs_thread *t) {return t->kind==TFS_SEM?(uintptr_t)t->object+0x28:(uintptr_t)t->object+t->cond_at;}
static void tfs_stop(tfs_thread *t) {
    t->quit=1;
    if(t->kind==TFS_SEM) tfs_sem_post(t->object);
    if(t->kind==TFS_PARK) {
        pthread_mutex_t *m=(pthread_mutex_t*)(t->object+t->mutex_at+8);
        pthread_mutex_lock(m);pthread_cond_broadcast((pthread_cond_t*)(t->object+t->cond_at));pthread_mutex_unlock(m);
    }
    pthread_join(t->thread,NULL);
}
/* Asks the parked worker to arm, then waits until it is parked again. */
static int tfs_arm_now(tfs_thread *worker,uint64_t tick,const char *name,unsigned budget_ms,unsigned watchdog_s) {
    tfs_arm.tick=tick;tfs_arm.name=name;tfs_arm.budget_ms=budget_ms;tfs_arm.watchdog_s=watchdog_s;tfs_worker_armed=-1;
    tfs_worker_command=1;tfs_sem_post(LX(LX_FS_SIM_START_VA));
    while(tfs_worker_command) sched_yield();
    tfs_wait_blocked(worker,(uintptr_t)LX(LX_FS_SIM_START_VA)+0x28,(uintptr_t)LX(LX_FS_SIM_START_VA)+0x58);
    return tfs_worker_armed;
}
/* ---- engine stand-ins, called in the child ---- */
static int tfs_possible(unsigned char online) {return online?-1:tfs_possible_value;}
static void tfs_diag(const char *text) {
    char path[300];snprintf(path,sizeof(path),"%s/diag.txt",tfs_folder);
    int f=open(path,O_WRONLY|O_CREAT|O_APPEND,0644);if(f>=0) {ssize_t ignored=write(f,text,strlen(text));(void)ignored;close(f);}
}
static int tfs_string_is(uintptr_t va,const char *text) {
    unsigned char *s=LX(va);uintptr_t p;uint64_t n;memcpy(&p,s,8);memcpy(&n,s+8,8);
    return p==(uintptr_t)(s+16) && n==strlen(text) && !memcmp(s+16,text,n+1);
}
#define TFS_CHECK(condition) do {if(!(condition)) {tfs_diag("child check failed: " #condition "\n");ok=0;}} while(0)
static unsigned char tfs_save(unsigned char online) {
    int ok=1;
    if(tfs_hang) for(;;) pause();
    TFS_CHECK(!online);
    TFS_CHECK(!*LX(LX_FS_FORMATTER_JOB_VA) && !*LX(LX_FS_WRITER_THREAD_VA) && !*LX(LX_FS_MODIFIED_WARN_VA) && *LX(LX_FS_ONLINE_DONE_VA)==1);
    int32_t running;memcpy(&running,tfs_jtm+0x5bc,4);TFS_CHECK(!running);
    TFS_CHECK(tfs_online[0x1851d]==1 && !tfs_sound[0x78]);
    TFS_CHECK(tfs_string_is(LX_FS_SAVE_DIR_VA,"save\\mpjoin\\") && tfs_string_is(LX_FS_FILE_NAME_VA,fs.name) && tfs_string_is(LX_FS_SAVE_NAME_VA,fs.name));
    /* The MoveWorker's thread words in the forking thread's own block. */
    unsigned char *mine=gate_tls_self();double time,previous;int32_t view;uint64_t rng;
    memcpy(&time,mine+GATE_TLS_LINUX_TIME,8);memcpy(&previous,mine+GATE_TLS_LINUX_PREVIOUS_TIME,8);memcpy(&view,mine+GATE_TLS_LINUX_VIEW,4);memcpy(&rng,mine+GATE_TLS_LINUX_RNG,8);
    TFS_CHECK(time==123.5 && previous==123.0 && view==1 && rng==0xabcull);
    /* Every lock held across fork belongs to the parent's thread ID: usable only once re-initialised. */
    for(int i=0;i<FS_LOCKS;i++) {
        pthread_mutex_t *m=(pthread_mutex_t*)(tfs_xmutexes[i]+8);int busy=pthread_mutex_trylock(m);
        TFS_CHECK(!busy);if(!busy) pthread_mutex_unlock(m);
    }
    for(int i=0;i<FS_RP_WORDS;i++) TFS_CHECK(!*(volatile uint32_t*)LX(i?LX_FS_RP_CACHE_VA+(uintptr_t)(i-1)*LX_FS_RP_CACHE_STRIDE:LX_FS_RP_GLOBAL_VA));
    /* The inherited relay socket and stdout reach /dev/null, not the parent's channels. */
    struct stat null_st,st;TFS_CHECK(!stat("/dev/null",&null_st));
    TFS_CHECK(!fstat(tfs_relay[0],&st) && S_ISCHR(st.st_mode) && st.st_rdev==null_st.st_rdev);
    TFS_CHECK(write(tfs_relay[0],"child",5)==5);
    TFS_CHECK(!fstat(1,&st) && S_ISCHR(st.st_mode) && st.st_rdev==null_st.st_rdev);
    /* The engine's own write: folder/mpjoin/name.xml.gz. */
    char path[400],text[64];snprintf(path,sizeof(path),"%s/mpjoin",tfs_folder);mkdir(path,0755);
    snprintf(path,sizeof(path),"%s/mpjoin/%s.xml.gz",tfs_folder,fs.name);
    int f=open(path,O_WRONLY|O_CREAT|O_TRUNC,0644);int n=snprintf(text,sizeof(text),"X4 save of tick %llu",(unsigned long long)fs.tick);
    TFS_CHECK(f>=0 && write(f,text,(size_t)n)==n);if(f>=0) close(f);
    return (unsigned char)ok;
}
/* Runs one request from arming to its end; returns the final state. */
static int tfs_run(tfs_thread *worker,const char *name,unsigned budget_ms,unsigned watchdog_s) {
    stats.issued=stats.completed=TFS_TICK-3;
    assert(tfs_arm_now(worker,TFS_TICK,name,budget_ms,watchdog_s)==1 && fs.state==FS_ARMED);
    for(uint64_t tick=TFS_TICK-3;tick<TFS_TICK;tick++) {stats.completed=tick;fork_save_frame(tick);struct timespec p={0,1000000};nanosleep(&p,NULL);}
    stats.completed=TFS_TICK;stats.issued=TFS_TICK;
    fork_save_frame(TFS_TICK);
    for(int i=0;i<20000 && fs.state==FS_RUNNING;i++) {struct timespec p={0,1000000};nanosleep(&p,NULL);fork_save_frame(TFS_TICK+1+(uint64_t)i);}
    return fs.state;
}
static void *tfs_try_locks(void *arg) {
    (void)arg;long free_count=0;
    for(int i=0;i<FS_LOCKS;i++) {pthread_mutex_t *m=(pthread_mutex_t*)(tfs_xmutexes[i]+8);if(!pthread_mutex_trylock(m)) {++free_count;pthread_mutex_unlock(m);}}
    return (void*)free_count;
}
static int tfs_locks_free(void) {pthread_t t;void *count;assert(!pthread_create(&t,NULL,tfs_try_locks,NULL));pthread_join(t,&count);return (long)count==FS_LOCKS;}
static void *tfs_hold_timerbase(void *arg) {
    volatile int *release=arg;pthread_mutex_t *m=(pthread_mutex_t*)(LX(LX_FS_TIMERBASE_VA)+8);
    pthread_mutex_lock(m);release[1]=1;while(!release[0]) sched_yield();pthread_mutex_unlock(m);return NULL;
}
static void test_fork_save_linux(void) {
    reset_session();
    snprintf(tfs_folder,sizeof(tfs_folder),"/tmp/x4lan-fork-save-%d",(int)getpid());mkdir(tfs_folder,0755);
    tfs_world();
    fs_possible_hook=tfs_possible;fs_save_hook=tfs_save;
    assert(!socketpair(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0,tfs_relay));
    /* The forking thread's own words differ from the worker's: the child must take the worker's. Put back at the end. */
    unsigned char saved_words[0x110],*mine=gate_tls_self();memcpy(saved_words,mine+GATE_TLS_LINUX_TIME,sizeof(saved_words));
    {double t=999.0;int32_t v=0;memcpy(mine+GATE_TLS_LINUX_TIME,&t,8);memcpy(mine+GATE_TLS_LINUX_VIEW,&v,4);}
    tfs_thread threads[]={
        {"Movement worker",TFS_WORKER,NULL,0,0,0,0,0,0,0},{"JobThread #00",TFS_SEM,tfs_jtm+0x558,0,0,0,0,0,0,0},
        {"JobThread #01",TFS_SEM,tfs_jtm+0x558,0,0,0,0,0,0,0},{"Pathing Manager",TFS_SEM,tfs_path+0x70,0,0,0,0,0,0,0},
        {"MassTrafficUpda",TFS_PARK,tfs_mass,0x70,0xa0,0xd8,0,0,0,0},{"RegionManager",TFS_PARK,tfs_region,0x138,0x168,0x1a0,0,0,0,0},
        {"Gravidar",TFS_SLEEP,NULL,0,0,0,0,0,0,0},{"Async File Save",TFS_SLEEP,NULL,0,0,0,0,0,0,0},{"SDL audio",TFS_SPIN,NULL,0,0,0,0,0,0,0}};
    const unsigned count=sizeof(threads)/sizeof(*threads);
    threads[0].object=LX(LX_FS_SIM_START_VA);
    stats.active=1;boot_mode=0;
    for(unsigned i=0;i<count;i++) tfs_start(&threads[i]);
    tfs_wait_blocked(&threads[0],(uintptr_t)LX(LX_FS_SIM_START_VA)+0x28,(uintptr_t)LX(LX_FS_SIM_START_VA)+0x58);
    for(unsigned i=1;i<6;i++) tfs_wait_blocked(&threads[i],tfs_cond_lo(&threads[i]),tfs_cond_lo(&threads[i])+0x30);
    for(unsigned i=6;i<8;i++) tfs_wait_blocked(&threads[i],0,0);
    /* Refusals leave it off: not the worker's thread, names, folders, ticks, budgets, watchdogs. */
    ui_thread=GetCurrentThreadId()+1;
    assert(!fork_save_arm(TFS_TICK,"join","/tmp",50,60) && fs.state==FS_OFF); /* not on the UI thread */
    ui_thread=GetCurrentThreadId();
    assert(!fork_save_arm(TFS_TICK,"join","/tmp",50,60)); /* the UI thread, but not the MoveWorker (view 0) */
    stats.issued=stats.completed=TFS_TICK-3;
    assert(tfs_arm_now(&threads[0],TFS_TICK,"a/b",50,60)==0 && tfs_arm_now(&threads[0],TFS_TICK,"sixteen_chars_xx",50,60)==0);
    assert(tfs_arm_now(&threads[0],TFS_TICK,"",50,60)==0 && tfs_arm_now(&threads[0],TFS_TICK-3,"join",50,60)==0);
    assert(tfs_arm_now(&threads[0],TFS_TICK,"join",0,60)==0 && tfs_arm_now(&threads[0],TFS_TICK,"join",50,1)==0);
    {char saved[sizeof(tfs_folder)];memcpy(saved,tfs_folder,sizeof(saved));
     strcpy(tfs_folder,"relative/save");assert(tfs_arm_now(&threads[0],TFS_TICK,"join",50,60)==0);
     strcpy(tfs_folder,"/tmp/../etc");assert(tfs_arm_now(&threads[0],TFS_TICK,"join",50,60)==0);
     memcpy(tfs_folder,saved,sizeof(saved));}
    stats.active=0;assert(tfs_arm_now(&threads[0],TFS_TICK,"join",50,60)==0);stats.active=1;
    assert(fs.state==FS_OFF);
    /* A tick that passed without its frame fails the request. */
    assert(tfs_arm_now(&threads[0],TFS_TICK,"join",50,60)==1);
    fork_save_frame(TFS_TICK+1);assert(fs.state==FS_FAILED && !strcmp(fs.reason,"tick_passed") && !fs.pid);
    /* Not quiet within the budget: nothing forks, every lock and word is free again, the reason names it. */
    threads[6].quit=1;pthread_join(threads[6].thread,NULL);threads[6].quit=0;threads[6].kind=TFS_SPIN;tfs_start(&threads[6]);
    assert(tfs_run(&threads[0],"join_busy",5,60)==FS_FAILED && !fs.pid && !strcmp(fs.reason,"census") && strstr(fs.blocker,"Gravidar") && strstr(fs.blocker,":running"));
    assert(fs.attempts>1 && fs.wait_us>=5000 && tfs_locks_free());
    threads[6].quit=1;pthread_join(threads[6].thread,NULL);threads[6].quit=0;threads[6].kind=TFS_SLEEP;tfs_start(&threads[6]);tfs_wait_blocked(&threads[6],0,0);
    volatile int hold[2]={0,0};pthread_t holder;assert(!pthread_create(&holder,NULL,tfs_hold_timerbase,(void*)hold));
    while(!hold[1]) sched_yield();
    assert(tfs_run(&threads[0],"join_lock",3,60)==FS_FAILED && !fs.pid && !strcmp(fs.reason,"lock") && !strcmp(fs.blocker,"timerbase"));
    hold[0]=1;pthread_join(holder,NULL);assert(tfs_locks_free());
    int32_t one=1,zero=0;memcpy(tfs_path+0xf0,&one,4);
    assert(tfs_run(&threads[0],"join_path",3,60)==FS_FAILED && !strcmp(fs.reason,"path_readers"));memcpy(tfs_path+0xf0,&zero,4);
    tfs_mass[0xa0]=0;assert(tfs_run(&threads[0],"join_mass",3,60)==FS_FAILED && !strcmp(fs.reason,"mass_traffic_running"));tfs_mass[0xa0]=1;
    memcpy(LX(LX_FS_COUNTDOWN_VA),&one,4);assert(tfs_run(&threads[0],"join_engine",3,60)==FS_FAILED && !strcmp(fs.reason,"engine_save_pending"));
    memcpy(LX(LX_FS_COUNTDOWN_VA),&zero,4);
    uint32_t *word=(uint32_t*)LX(LX_FS_RP_CACHE_VA+4*LX_FS_RP_CACHE_STRIDE);*word=1;
    assert(tfs_run(&threads[0],"join_rp",3,60)==FS_FAILED && !strcmp(fs.reason,"rpmalloc") && !strcmp(fs.blocker,"word_5") && tfs_locks_free());
    for(int i=0;i<5;i++) assert(!*(uint32_t*)LX(i?LX_FS_RP_CACHE_VA+(uintptr_t)(i-1)*LX_FS_RP_CACHE_STRIDE:LX_FS_RP_GLOBAL_VA));
    *word=0;
    /* The save: the parent goes on at once; the child's record hashes the file it wrote. */
    uint64_t started=fs_now_us();
    assert(tfs_run(&threads[0],"join_000040",50,60)==FS_DONE);
    char diag[300];snprintf(diag,sizeof(diag),"%s/diag.txt",tfs_folder);
    struct stat st;if(!stat(diag,&st)) {char text[2048]={0};fs_read_text(diag,text,sizeof(text));printf("%s",text);assert(!"child checks failed");}
    char text[64];int n=snprintf(text,sizeof(text),"X4 save of tick %u",TFS_TICK);
    unsigned char sha[32];lockstep_sha256(text,(size_t)n,sha);
    assert(fs.result.stage==FS_STAGE_DONE && fs.result.possible==1 && fs.result.saved==1 && fs.result.bytes==(uint64_t)n && !memcmp(fs.result.sha,sha,32));
    assert(fs.reaped==1 && fs.exit_code==0 && !fs.signal_number && fs.result.flags==7 && fs.result.fds>0 && !strcmp(fs.reason,"ok"));
    assert(fs.before_n==3 && fs.during_n>=1 && fs.fork_us>0 && fs.hold_us>=fs.fork_us && fs.engine_threads==8 && fs.other_running>=1);
    char status[1024];assert(fork_save_status(status,sizeof(status)) && strstr(status,"state=done tick=40 name=join_000040 reason=ok") && strstr(status," saved=1 "));
    {char hex[65];hex32(sha,hex);assert(strstr(status,hex));}
    /* The parent's channels were never touched; every lock and word is free; the parent's flags are its own. */
    char got[16];assert(recv(tfs_relay[1],got,sizeof(got),0)<0 && errno==EAGAIN);
    assert(send(tfs_relay[0],"parent",6,0)==6 && recv(tfs_relay[1],got,sizeof(got),0)==6);
    assert(tfs_locks_free() && *LX(LX_FS_FORMATTER_JOB_VA)==1 && *LX(LX_FS_WRITER_THREAD_VA)==1 && tfs_sound[0x78]==1 && !tfs_online[0x1851d]);
    assert(tfs_string_is(LX_FS_SAVE_DIR_VA,"save\\") && tfs_string_is(LX_FS_FILE_NAME_VA,""));
    {int32_t r;memcpy(&r,tfs_jtm+0x5bc,4);assert(r==1);}
    printf("fork save PASS (fork %llu us, hold %llu us, census %llu us, child %llu us, %u threads, run %llu ms)\n",(unsigned long long)fs.fork_us,
           (unsigned long long)fs.hold_us,(unsigned long long)fs.census_us,(unsigned long long)fs.result.child_us,fs.threads,(unsigned long long)((fs_now_us()-started)/1000));
    /* Not possible, then a serializer that never returns: the watchdog ends the child, the parent reports it. */
    tfs_possible_value=3;
    assert(tfs_run(&threads[0],"join_np",50,60)==FS_FAILED && !strcmp(fs.reason,"not_possible") && fs.result.possible==3 && fs.exit_code==3);
    tfs_possible_value=1;tfs_hang=1;
    assert(tfs_run(&threads[0],"join_hang",50,2)==FS_FAILED && !strcmp(fs.reason,"watchdog") && fs.signal_number==SIGALRM && fs.result.stage==FS_STAGE_SAVE);
    tfs_hang=0;
    printf("fork save refusals PASS (busy thread, held lock, readers, parked flag, engine save, spin word, not possible, watchdog)\n");
    /* Stop it all. */
    tfs_worker_command=2;tfs_sem_post(LX(LX_FS_SIM_START_VA));pthread_join(threads[0].thread,NULL);
    for(unsigned i=1;i<count;i++) tfs_stop(&threads[i]);
    close(tfs_relay[0]);close(tfs_relay[1]);
    char cleanup[400];snprintf(cleanup,sizeof(cleanup),"%s/mpjoin/join_000040.xml.gz",tfs_folder);unlink(cleanup);
    snprintf(cleanup,sizeof(cleanup),"%s/mpjoin",tfs_folder);rmdir(cleanup);rmdir(tfs_folder);
    fs_possible_hook=NULL;fs_save_hook=NULL;memset(&fs,0,sizeof(fs));fs.pipe_fd=-1;stats.active=0;reset_session();
    memcpy(mine+GATE_TLS_LINUX_TIME,saved_words,sizeof(saved_words)); /* time .. view: -0x2728 .. -0x2620+4 */
}

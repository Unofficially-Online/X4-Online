/* Diagnostic, DEFAULT OFF: native_gate_sample_profile = sample period in ms (1..50).
 * Where the MoveWorker's frame goes (attention study C9,
 * reports/LOCKSTEP-ATTENTION-STUDY-20261101.txt section 8; also roadmap C10).
 * FRAME_TIMING says how long the simulation thread's frame takes (sim_us); this says
 * inside which engine functions that time is spent.
 *
 * A sampler thread, created at arm, wakes every period. While the gate is active and
 * has admitted a frame it suspends the MoveWorker (ui_thread: the thread that armed, or
 * the one the gate adopts later in snapshot boot; the sampler follows it, retargets=),
 * reads its context and walks up to SPROF_DEPTH frames with RtlVirtualUnwind over the
 * function tables it located at arm (X4.exe, this DLL and every module loaded then),
 * checking every stack pointer against the thread's own stack bounds (its TEB, read at
 * each sample) and under SEH. Nothing that takes a loader, heap or gate lock runs while
 * the thread is suspended. It then resumes the thread and counts, in fixed tables:
 *   mark k  samples with a frame inside engine function k (inclusive time; the table
 *           below), matched by the start of the function that owns the frame (cold
 *           fragments through their chained unwind data);
 *   inner   the innermost X4 code of the sample: the start of the leaf's function when
 *           the leaf is in X4, else the X4 return address that called out (a wait, a
 *           gate hook, the CRT: site=1), the SPROF_TOP largest as rows;
 *   leaf    x4 / gate (this DLL) / other module / unknown code (JIT, no module);
 *   step    the same, only for samples taken while gate_sim_started is set (the
 *           simulation window from the pathing kick to the next FrameTick wait, which
 *           the mass traffic wait hook closes; without that hook it never closes).
 * It only reads: no game memory is written and no code is patched, so no simulated
 * value can change. The MoveWorker is paused while it is sampled (pause_us,
 * pause_max_us), a wall-clock effect of the same kind as waiting for the relay. The
 * marks name addresses of the pinned build (the gate checks EXE_SHA at start); a wrong
 * one would mislabel a count, never touch the game. Known risk of any suspending sampler:
 * an exception during a walk is dispatched while the MoveWorker is suspended, so if that
 * thread held the loader's function-table lock exclusively (a module load) the process
 * would hang; every stack pointer is validated first and faults= counts what still
 * happens. Diagnostic runs only. Status SAMPLE_PROFILE; rows
 * SAMPLE_PROFILE_ROW kind=mark|inner at stop, read by tools/dev/census_profile.py. */
#define SPROF_MODULES 512u
#define SPROF_DEPTH 48u
#define SPROF_INNER 4096u /* power of two */
#define SPROF_TOP 48u
#define SPROF_SITE 0x80000000u
typedef struct {uintptr_t base,end;const RUNTIME_FUNCTION *table;DWORD count;} sprof_module;
typedef struct {uint32_t rva;const char *label;uint64_t count,step;} sprof_mark;
/* Engine functions of the pinned build, by .pdata start (labels are ours). */
static sprof_mark sprof_marks[]={
    {0xef4400u,"moveworker_run",0,0},      /* U::MoveWorker::Run: the whole thread */
    {0x122b360u,"gameclient_move",0,0},    /* VBI slot 3: events, MD, AI, universe update */
    {0x9fdef0u,"universe_update",0,0},     /* UniverseFrameUpdate */
    {0xba600u,"physics_step",0,0},         /* XPhys::Scene::Step (every scene; Jolt jobs run inside it) */
    {0x13d3460u,"jolt_update",0,0},        /* JPH::PhysicsSystem::Update */
    {0x106d250u,"ai_director",0,0},        /* AIDirector::Move: AI scripts */
    {0x1121f80u,"md_scheduler",0,0},       /* MDScheduler_Update: mission director */
    {0x834950u,"zone_move",0,0},           /* U::Zone::Move: the per-zone Move tree */
    {0x7cc810u,"ship_move",0,0},           /* U::Ship::Move */
    {0x6ccf90u,"object_move",0,0},         /* U::Object::Move */
    {0x748120u,"positional_move",0,0},     /* U::Positional::Move */
    {0x3d5660u,"set_attention",0,0},       /* U::Component::SetAttention (and its change handlers) */
    {0x8185b0u,"weapon_fire",0,0},         /* U::Weapon slot 998: a shot */
    {0xd73e00u,"spawn_effect",0,0},        /* SpawnEffect */
    {0x767410u,"region_move",0,0},         /* U::Region::Move: asteroid/debris fields */
    {0xe8f250u,"agent_pathing",0,0},       /* Agent_UpdatePathing */
    {0x83e960u,"body_pass",0,0},           /* UniverseFrameUpdate's body pass */
    {0x97dff0u,"camera_context",0,0},      /* camera enter/leave events */
    {0xb3bad0u,"ui_move",0,0},             /* VBI slot 6: UI::CUIController::Move */
    {0x122bfc0u,"render_cameras",0,0},     /* VBI slot 10: GameClient render bank, every camera */
    {0xf0b870u,"job_engine",0,0}};         /* VBI slot 11: U::JobEngine */
#define SPROF_MARKS (sizeof(sprof_marks)/sizeof(*sprof_marks))
static sprof_module sprof_modules[SPROF_MODULES];
static unsigned sprof_module_count,sprof_x4=UINT_MAX,sprof_self=UINT_MAX;
typedef struct {uint32_t key;uint64_t count,step;} sprof_count;
static sprof_count sprof_inner[SPROF_INNER],sprof_top[SPROF_TOP];
static unsigned sprof_top_count;
static volatile uint64_t sprof_samples,sprof_step,sprof_walked,sprof_leaf_x4,sprof_leaf_gate,sprof_leaf_other,sprof_leaf_unknown,
    sprof_truncated,sprof_unwalked,sprof_faults,sprof_failed,sprof_dropped,sprof_frames,sprof_pause_us,sprof_pause_max_us;
static unsigned sprof_period_ms;
static int sprof_armed;
static const char *sprof_stage="idle";
static HANDLE sprof_thread,sprof_stop_event,sprof_target;
static unsigned char *sprof_teb;
static CONTEXT sprof_context; /* the sampler thread's only; CONTEXT is 16-byte aligned */
static uintptr_t sprof_pcs[SPROF_DEPTH];
typedef struct {LONG exit_status;PVOID teb;PVOID process,thread;ULONG_PTR affinity;LONG priority,base_priority;} sprof_thread_info;
typedef LONG (NTAPI *sprof_query_fn)(HANDLE,int,PVOID,ULONG,PULONG);
static sprof_query_fn sprof_query;
static DWORD sprof_target_id;
static volatile uint64_t sprof_retargets;
/* Follow ui_thread: snapshot boot adopts the in-game MoveWorker as ui_thread at the first
 * admitted frame (accept_ui), after the probe armed on the loading thread. On the sampler
 * thread, with nothing suspended: a handle and the TEB (ThreadBasicInformation) of the new id. */
static int sprof_follow(DWORD id) {
    if(id==sprof_target_id && sprof_target) return 1;
    HANDLE h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,id);
    sprof_thread_info info;memset(&info,0,sizeof(info));
    if(!h || !sprof_query || sprof_query(h,0,&info,sizeof(info),NULL)<0 || !info.teb) {if(h) CloseHandle(h);return 0;}
    if(sprof_target) CloseHandle(sprof_target);
    sprof_target=h;sprof_teb=(unsigned char *)info.teb;sprof_target_id=id;++sprof_retargets;return 1;
}
/* A mapped image's function table, read from its own headers. */
static int sprof_module_fill(sprof_module *m,const unsigned char *base) {
    const IMAGE_DOS_HEADER *dos=(const void *)base;
    if(!base || dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>4096) return 0;
    const IMAGE_NT_HEADERS64 *nt=(const void *)(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
    const IMAGE_DATA_DIRECTORY *d=&nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    m->base=(uintptr_t)base;m->end=m->base+nt->OptionalHeader.SizeOfImage;
    m->table=d->VirtualAddress && d->Size>=sizeof(RUNTIME_FUNCTION) && d->VirtualAddress+d->Size<=nt->OptionalHeader.SizeOfImage?
        (const RUNTIME_FUNCTION *)(base+d->VirtualAddress):NULL;
    m->count=m->table?d->Size/(DWORD)sizeof(RUNTIME_FUNCTION):0;
    return 1;
}
static int sprof_module_order(const void *a,const void *b) {
    const sprof_module *x=a,*y=b;return x->base<y->base?-1:x->base>y->base;
}
/* Every module loaded now (K32EnumProcessModules), sorted; x4 is the module at x4_base. */
typedef BOOL (WINAPI *sprof_enum_fn)(HANDLE,HMODULE *,DWORD,LPDWORD);
static int sprof_tables(const unsigned char *x4_base) {
    static HMODULE handles[SPROF_MODULES];DWORD bytes=0;
    sprof_enum_fn enumerate=(sprof_enum_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32EnumProcessModules");
    if(!enumerate || !enumerate(GetCurrentProcess(),handles,sizeof(handles),&bytes)) return 0;
    unsigned n=0;HMODULE self=NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)(void *)sprof_tables,&self);
    for(DWORD i=0;i<bytes/sizeof(HMODULE) && i<SPROF_MODULES;i++) if(sprof_module_fill(&sprof_modules[n],(const unsigned char *)handles[i])) ++n;
    qsort(sprof_modules,n,sizeof(*sprof_modules),sprof_module_order);
    sprof_module_count=n;sprof_x4=sprof_self=UINT_MAX;
    for(unsigned i=0;i<n;i++) {
        if(sprof_modules[i].base==(uintptr_t)x4_base) sprof_x4=i;
        if(sprof_modules[i].base==(uintptr_t)self) sprof_self=i;
    }
    return sprof_x4!=UINT_MAX && sprof_modules[sprof_x4].table;
}
static unsigned sprof_module_of(uintptr_t pc) {
    unsigned lo=0,hi=sprof_module_count;
    while(lo<hi) {unsigned mid=(lo+hi)/2;if(sprof_modules[mid].base<=pc) lo=mid+1;else hi=mid;}
    return lo && pc<sprof_modules[lo-1].end?lo-1:UINT_MAX;
}
static const RUNTIME_FUNCTION *sprof_entry(const sprof_module *m,uintptr_t pc) {
    if(!m->table || pc<m->base || pc>=m->end) return NULL;
    uint32_t rva=(uint32_t)(pc-m->base);DWORD lo=0,hi=m->count;
    while(lo<hi) {
        DWORD mid=(lo+hi)/2;const RUNTIME_FUNCTION *f=&m->table[mid];
        if(rva<f->BeginAddress) hi=mid;else if(rva>=f->EndAddress) lo=mid+1;else return f;
    }
    return NULL;
}
/* The start of the function that owns an entry: a chained (cold) fragment's unwind data
 * ends with its parent's RUNTIME_FUNCTION. */
static uint32_t sprof_owner(const sprof_module *m,const RUNTIME_FUNCTION *f) {
    for(unsigned depth=0;depth<8;depth++) {
        uintptr_t info=m->base+(f->UnwindData&~1u);
        if(info+4>m->end) break;
        const unsigned char *u=(const unsigned char *)info;
        if(!((u[0]>>3)&UNW_FLAG_CHAININFO)) break;
        uintptr_t parent=info+4+(((uintptr_t)u[2]+1u)&~(uintptr_t)1u)*2u;
        if(parent+sizeof(RUNTIME_FUNCTION)>m->end) break;
        f=(const RUNTIME_FUNCTION *)parent;
    }
    return f->BeginAddress;
}
/* Walk a suspended thread's context (modified). Returns the number of pcs; *why: 0 end of
 * stack, 1 depth limit, 2 code outside every module, 3 stack pointer out of bounds. */
static unsigned sprof_walk(CONTEXT *c,uintptr_t lo,uintptr_t hi,uintptr_t *pcs,unsigned cap,int *why) {
    unsigned n=0;*why=1;
    while(n<cap) {
        uintptr_t pc=(uintptr_t)c->Rip;
        if(!pc) {*why=0;return n;}
        pcs[n]=pc;
        const unsigned index=sprof_module_of(pc);
        if(index==UINT_MAX) {*why=2;return n+1;}
        if(c->Rsp<lo || c->Rsp+8>hi) {*why=3;return n+1;}
        const sprof_module *m=&sprof_modules[index];
        /* A return address may follow a call that ends its function: look it up one byte back. */
        const RUNTIME_FUNCTION *f=sprof_entry(m,n?pc-1:pc);
        ++n;
        if(!f) {c->Rip=*(DWORD64 *)c->Rsp;c->Rsp+=8;} /* a leaf without unwind data */
        else {PVOID data=NULL;DWORD64 frame=0;RtlVirtualUnwind(UNW_FLAG_NHANDLER,m->base,pc,(PRUNTIME_FUNCTION)f,c,&data,&frame,NULL);}
        if(c->Rip && (c->Rsp<lo || c->Rsp>hi)) {*why=3;return n;}
    }
    return n;
}
static void sprof_inner_add(uint32_t key,int step) {
    for(unsigned n=0,h=(unsigned)(lockstep_mix(key)&(SPROF_INNER-1));n<SPROF_INNER;n++,h=(h+1)&(SPROF_INNER-1)) {
        if(sprof_inner[h].count && sprof_inner[h].key!=key) continue;
        sprof_inner[h].key=key;++sprof_inner[h].count;if(step) ++sprof_inner[h].step;return;
    }
    ++sprof_dropped;
}
/* After the thread runs again: classify and count one sample. */
static void sprof_tally(const uintptr_t *pcs,unsigned n,int why,int step) {
    ++sprof_samples;if(step) ++sprof_step;
    if(!n) {++sprof_unwalked;return;}
    ++sprof_walked;sprof_frames+=n;if(why==1) ++sprof_truncated;
    const unsigned leaf=sprof_module_of(pcs[0]);
    if(leaf==UINT_MAX) ++sprof_leaf_unknown;else if(leaf==sprof_x4) ++sprof_leaf_x4;else if(leaf==sprof_self) ++sprof_leaf_gate;else ++sprof_leaf_other;
    if(sprof_x4==UINT_MAX) return;
    const sprof_module *x4=&sprof_modules[sprof_x4];
    uint64_t seen=0;int inner=0;
    for(unsigned i=0;i<n;i++) {
        if(pcs[i]<x4->base || pcs[i]>=x4->end) continue;
        const RUNTIME_FUNCTION *f=sprof_entry(x4,i?pcs[i]-1:pcs[i]);
        const uint32_t owner=f?sprof_owner(x4,f):0;
        if(!inner) {inner=1;sprof_inner_add(i?SPROF_SITE|(uint32_t)(pcs[i]-x4->base):owner,step);}
        if(owner) for(unsigned k=0;k<SPROF_MARKS && k<64;k++) if(sprof_marks[k].rva==owner) seen|=1ULL<<k;
    }
    for(unsigned k=0;k<SPROF_MARKS && k<64;k++) if((seen>>k)&1u) {++sprof_marks[k].count;if(step) ++sprof_marks[k].step;}
}
/* One sample of the target thread. */
static void sprof_sample(void) {
    LARGE_INTEGER a,b,f;QueryPerformanceCounter(&a);
    if(SuspendThread(sprof_target)==(DWORD)-1) {++sprof_failed;return;}
    unsigned n=0;int why=0,step=0;
    memset(&sprof_context,0,sizeof(sprof_context));sprof_context.ContextFlags=CONTEXT_FULL;
    if(GetThreadContext(sprof_target,&sprof_context)) {
        step=gate_sim_started.QuadPart!=0;
        __try {
            const uintptr_t lo=*(volatile uintptr_t *)(sprof_teb+0x10),hi=*(volatile uintptr_t *)(sprof_teb+0x08); /* NT_TIB StackLimit, StackBase */
            n=sprof_walk(&sprof_context,lo,hi,sprof_pcs,SPROF_DEPTH,&why);
        } __except(EXCEPTION_EXECUTE_HANDLER) {++sprof_faults;n=0;}
    } else ++sprof_failed;
    ResumeThread(sprof_target);
    QueryPerformanceCounter(&b);QueryPerformanceFrequency(&f);
    const uint64_t us=f.QuadPart?(uint64_t)((b.QuadPart-a.QuadPart)*1000000/f.QuadPart):0;
    sprof_pause_us+=us;if(us>sprof_pause_max_us) sprof_pause_max_us=us;
    sprof_tally(sprof_pcs,n,why,step);
}
static DWORD WINAPI sprof_main(LPVOID unused) {
    (void)unused;
    HANDLE timer=CreateWaitableTimerExW(NULL,NULL,0x00000002u /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,0x1F0003u /* TIMER_ALL_ACCESS */);
    if(timer) {
        LARGE_INTEGER due;due.QuadPart=-(LONGLONG)sprof_period_ms*10000;
        if(!SetWaitableTimer(timer,&due,(LONG)sprof_period_ms,NULL,NULL,FALSE)) {CloseHandle(timer);timer=NULL;}
    }
    HANDLE waits[2]={sprof_stop_event,timer};
    for(;;) {
        DWORD w=timer?WaitForMultipleObjects(2,waits,FALSE,1000):WaitForSingleObject(sprof_stop_event,sprof_period_ms);
        if(w==WAIT_OBJECT_0 || w==WAIT_FAILED) break;
        if(!InterlockedCompareExchange((volatile LONG *)&stats.active,0,0) || !stats.issued) continue;
        if(!sprof_follow(ui_thread)) {++sprof_failed;continue;}
        sprof_sample();
    }
    if(timer) CloseHandle(timer);
    return 0;
}
/* On the MoveWorker (the thread to sample), gate active. Production: x4_base = image. */
static int sprof_arm_on(unsigned period,const unsigned char *x4_base) {
    if(sprof_armed) return sprof_period_ms==period;
    if(period<1 || period>50 || !stats.active || GetCurrentThreadId()!=ui_thread) return 0;
    if(!sprof_tables(x4_base)) {sprof_stage="tables";return 0;}
    for(unsigned k=0;k<SPROF_MARKS;k++) sprof_marks[k].count=sprof_marks[k].step=0;
    memset(sprof_inner,0,sizeof(sprof_inner));sprof_top_count=0;
    sprof_samples=sprof_step=sprof_walked=sprof_leaf_x4=sprof_leaf_gate=sprof_leaf_other=sprof_leaf_unknown=0;
    sprof_truncated=sprof_unwalked=sprof_faults=sprof_failed=sprof_dropped=sprof_frames=sprof_pause_us=sprof_pause_max_us=0;
    sprof_teb=(unsigned char *)NtCurrentTeb();sprof_target_id=GetCurrentThreadId();sprof_retargets=0;
    sprof_query=(sprof_query_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationThread");
    if(!DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&sprof_target,
                        THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,0)) {sprof_stage="handle";return 0;}
    sprof_stop_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    sprof_period_ms=period;
    sprof_thread=sprof_stop_event?CreateThread(NULL,0,sprof_main,NULL,0,NULL):NULL;
    if(!sprof_thread) {
        if(sprof_stop_event) CloseHandle(sprof_stop_event);
        CloseHandle(sprof_target);sprof_stop_event=sprof_target=NULL;sprof_stage="thread";return 0;
    }
    SetThreadPriority(sprof_thread,THREAD_PRIORITY_HIGHEST);
    sprof_armed=1;sprof_stage="sampling";return 1;
}
static int sprof_arm(unsigned period) {return sprof_arm_on(period,image);}
/* Gate stop: the sampler ends after its current sample (the target is never left suspended). */
static void sprof_restore(void) {
    if(!sprof_thread) return;
    SetEvent(sprof_stop_event);
    if(WaitForSingleObject(sprof_thread,5000)==WAIT_OBJECT_0) {
        CloseHandle(sprof_thread);CloseHandle(sprof_stop_event);CloseHandle(sprof_target);
        sprof_thread=sprof_stop_event=sprof_target=NULL;sprof_target_id=0;sprof_stage="stopped";
    } else sprof_stage="stop_timeout";
}
static int sprof_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%d period_ms=%u modules=%u samples=%llu step=%llu walked=%llu leaf=%llu/%llu/%llu/%llu "
        "truncated=%llu unwalked=%llu faults=%llu failed=%llu dropped=%llu frames=%llu pause_us=%llu pause_max_us=%llu retargets=%llu stage=%s",
        sprof_armed,sprof_period_ms,sprof_module_count,(unsigned long long)sprof_samples,(unsigned long long)sprof_step,
        (unsigned long long)sprof_walked,(unsigned long long)sprof_leaf_x4,(unsigned long long)sprof_leaf_gate,
        (unsigned long long)sprof_leaf_other,(unsigned long long)sprof_leaf_unknown,(unsigned long long)sprof_truncated,
        (unsigned long long)sprof_unwalked,(unsigned long long)sprof_faults,(unsigned long long)sprof_failed,
        (unsigned long long)sprof_dropped,(unsigned long long)sprof_frames,(unsigned long long)sprof_pause_us,
        (unsigned long long)sprof_pause_max_us,(unsigned long long)sprof_retargets,sprof_stage);
    return n>0 && (size_t)n<capacity?n:0;
}
static int sprof_top_order(const void *a,const void *b) {
    const sprof_count *x=a,*y=b;
    if(x->count!=y->count) return x->count>y->count?-1:1;
    return x->key<y->key?-1:x->key>y->key;
}
/* Rows: 0..SPROF_MARKS-1 the marks, then the largest inner entries (the list is taken
 * when row SPROF_MARKS is asked for). NULL past the end. */
static int sprof_row(unsigned index,char *out,size_t capacity) {
    int n=0;
    if(index<SPROF_MARKS) {
        const sprof_mark *m=&sprof_marks[index];
        n=snprintf(out,capacity,"index=%u kind=mark label=%s rva=%x count=%llu step=%llu",index,m->label,m->rva,
                   (unsigned long long)m->count,(unsigned long long)m->step);
    } else {
        unsigned k=index-(unsigned)SPROF_MARKS;
        if(!k) {
            sprof_top_count=0;
            for(unsigned i=0;i<SPROF_INNER;i++) {
                if(!sprof_inner[i].count) continue;
                if(sprof_top_count<SPROF_TOP) {sprof_top[sprof_top_count++]=sprof_inner[i];continue;}
                unsigned low=0;for(unsigned j=1;j<SPROF_TOP;j++) if(sprof_top_order(&sprof_top[j],&sprof_top[low])>0) low=j;
                if(sprof_top_order(&sprof_inner[i],&sprof_top[low])<0) sprof_top[low]=sprof_inner[i];
            }
            qsort(sprof_top,sprof_top_count,sizeof(*sprof_top),sprof_top_order);
        }
        if(k>=sprof_top_count) return 0;
        n=snprintf(out,capacity,"index=%u kind=inner rva=%x site=%d count=%llu step=%llu",index,sprof_top[k].key&~SPROF_SITE,
                   (sprof_top[k].key&SPROF_SITE)!=0,(unsigned long long)sprof_top[k].count,(unsigned long long)sprof_top[k].step);
    }
    return n>0 && (size_t)n<capacity?n:0;
}

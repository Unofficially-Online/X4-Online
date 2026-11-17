/* Background jobs of the simulation on the tick (native_gate_area_jobs, a session default; Windows gate).
 *
 * Found with the periodic saves of a two-client session that split 47 minutes in (16 Nov): the saves of minute 20, 30
 * and 40, all taken while the session still counted as in sync, already differed in one thing. A sector's resource
 * areas (the mining regions' yield) had their next activation scheduled a few ticks apart on the two clients, in both
 * directions:
 *     <event event="resourceareaactivated" time="4405.459" area="[..]"/>   against   time="4405.392"
 * one area at minute 20, four at minute 30, eight at minute 40. The world census does not cover those records, so the
 * session ran on; miners then find different resources, FindClosestResource makes a temp zone on one client that the
 * other does not need, and the runtime IDs part (the split the relay finally names; the same four allocation stacks
 * under Sector::CreateTempZone in two sessions).
 *
 * Mechanism (X4 9.00, static). A sector asks for an area's content through 0x7801c0, which ends by queuing a job on
 * the JobThreadManager (0x78057a: QueueJob 0x14bc2d0, callback thunk 0x7bbc70, payload {query object, body 0x77e200,
 * .., cleanup}). The body runs the spatial query on a job thread and ends by posting an event on the sector through
 * the deferred post (0x77e693: 0x99a720, event vtable 0x2ba5cd8). A job thread never syncs its TLS game time, so that
 * event is due "now": the simulation takes it in whichever tick the job happened to finish before. The sector's
 * handler (0x7805b0, through its vtable entry 0x785170) then stamps the area with that tick's time (+0x90) and posts
 * U::ResourceAreaActivatedEvent with it. A few milliseconds of job time decide the tick; two clients on one PC, or
 * one of them under load, differ.
 *
 * The fix: that QueueJob call is answered here and the job runs at once on the asking thread, the way the pathing
 * batch jobs do (lockstep_travel_determinism.h): the callback thunk on the caller's own payload, with a job thread's
 * TLS context (game time 0, the published view, a random stream keyed by the tick), so what it posts is there at the
 * tick of the request on every client. With it the saves of minute 10 to 60 of a session were equal, and the session
 * passed minute 47.
 *
 * The second job (the same session, split at minute 61, like two sessions the night before): the construction ID
 * counter (0x38f4dd0, in the pilot digest's UIBUILD field) stood 7 apart at one tick with the world's saves equal 49
 * seconds earlier. One of the counter's users is the construction sequence generator 0xd5c6f0, and the script action
 * create_construction_sequence (0xc24970) either calls it or QUEUES it (0xc25003: the same QueueJob, thunk 0xe77630,
 * the same payload shape): a station's planned modules then get their construction IDs, and their random picks, on a
 * job thread at whatever tick it runs. The same answer: the job runs at the request. With both sites a two-client
 * session ran its 100 minutes in sync (82 area jobs and 25 construction jobs on each client; the saves of minute 90
 * equal but for the listing order of the economy log's removed stations, whose IDs agree).
 *
 * The third job (found by reading every QueueJob call of the image, not seen in a session): a resource probe's scan.
 * The probe asks through 0x76af50, which queues at 0x76b288 (thunk 0x7bb5d0, body 0x76ae40); the body measures how
 * much of a resource area the probe covers and posts U::SetDetectedResourceAreaOverlapFractionEvent (vtable
 * 0x2ba5ca0) on the sector with the job thread's game time, 0, the same way as the first job: the area's detected
 * fraction changes in whichever tick the job finished before. It runs at the request as well. (The image's other
 * QueueJob calls are physics, pathing - lockstep_travel_determinism.h - graphics, XML output and the save list's
 * directory scan 0x9e5fc0; none of those post into the simulation this way.) Run live with the script op probe_far
 * (a probe at the first ore of the galaxy; a probe in a sector without resource areas asks for nothing): each probe
 * asked for four scans, and both clients ran them at the request (inlined=../../8 after two probes).
 *
 * Any other job through these calls, or a payload that is not that site's job, is queued as X4 queues it, and so is
 * a job asked by a thread without the game's thread context. Each site is pinned (the asking block, the thunk, the
 * body) and goes in alone; a build that differs keeps X4's call.
 * Status installed=<sites>/3 why= inlined=<area>/<construction>/<probe> forwarded= failed=. */
#define AREAJ_QUEUE 0x14bc2d0u
#define AREAJ_DOMAIN 0x58344c415245414aULL /* "X4LAREAJ" */
#define AREAJ_SITES 3u
typedef int (*areaj_queue_fn)(void *,void *,void *,unsigned,unsigned);
typedef void (*areaj_job_fn)(void *,void *);
typedef struct {
    const char *name;
    unsigned site,stub;                    /* the QueueJob call; its stub on the slots' near page */
    unsigned ask,ask_size;const char *ask_sha;       /* the code that builds the payload and queues */
    unsigned thunk,thunk_size;const char *thunk_sha; /* the callback: body(object), then the cleanup */
    unsigned body,body_size;const char *body_sha;
    int installed;unsigned char patched[5];volatile LONG64 inlined;
} areaj_site;
static areaj_site areaj_sites[AREAJ_SITES]={
    {"area",0x78057au,0x4a0u,
     0x7801c0u,0x3e0u,"227707011b5f879eb1e91ac0a76578e6fc226f32318ea24575b627389c43bdcd",
     0x7bbc70u,0x57u,"a281241342e75fb3de7910e0a0ddf72110e1cb0e2046d2eafada5b0774a1ae03",
     0x77e200u,0x55cu,"b0ed49645b0749e767e322ccf9557eb6feee811a2cc46173df8869ce2e7bf29d",0,{0},0},
    {"construction",0xc25003u,0x4c0u,
     0xc24fb0u,0x60u,"e51b5a71d9ebaf72477cb7ca5af0277b0e7a7594e3c79cb5c4134e51d6732037",
     0xe77630u,0x61u,"3f98153845f025bf797323d26b2e1aa47a04ceba99cf5c2a90fe77449a0ce65b",
     0xd5c6f0u,0x3080u,"f12d5b8271f1b47c01266fadca61322e98b50a08f66ce361ac1f0329327169bc",0,{0},0},
    {"probe",0x76b288u,0x4e0u,
     0x76af50u,0x3ebu,"a0fdd6338defd38262a1e52276e5c351116f7d5339870d09f9570ef4bef3bcfe",
     0x7bb5d0u,0x7bu,"3a40f020fab2bafd1e44bbfa831a7ddf3ff5f40641391bca5f990c5f5597e2e7",
     0x76ae40u,0x10du,"655a4bcee772c939282bf480f614ff31e329ab0deadf939f78fc9d59d731a944",0,{0},0},
};
static const char *const areaj_pin_why[AREAJ_SITES]={"pin_area","pin_construction","pin_probe"};
static const char *const areaj_site_why[AREAJ_SITES]={"site_area","site_construction","site_probe"};
static const char *areaj_why="-";
static volatile LONG64 areaj_forwarded,areaj_failed;
static volatile LONG areaj_ordinal;
static uint64_t areaj_tick=UINT64_MAX;
/* tests: in place of the job's thunk */
static areaj_job_fn areaj_job_test;
/* A site's job: its callback, and a payload {object, the body's address, ...} (0x780540.., 0xc24fc7.. build them). */
static areaj_site *areaj_site_of(const void *cb,const void *payload) {
    void *object=NULL,*fn=NULL;
    if(!cb || !payload || !read_local(payload,&object,8) || !read_local((const unsigned char*)payload+8,&fn,8) || !object) return NULL;
    for(unsigned i=0;i<AREAJ_SITES;i++)
        if(areaj_sites[i].installed && cb==(const void*)(image+areaj_sites[i].thunk) && fn==(void*)(image+areaj_sites[i].body)) return &areaj_sites[i];
    return NULL;
}
/* The job on the asking thread with a job thread's TLS context. 1: it ran. */
static int areaj_run(areaj_job_fn job,void *payload,unsigned char *tls) {
    uint64_t saved_time,saved_previous,saved_rng,zero=0;uint32_t saved_view,zero_view=0;int ok=1;
    if(!tls) return 0;
    if(stats.issued!=areaj_tick) {areaj_tick=stats.issued;InterlockedExchange(&areaj_ordinal,0);}
    const uint64_t key=lockstep_seed(AREAJ_DOMAIN,stats.issued,(uint64_t)InterlockedIncrement(&areaj_ordinal),0,0,0);
    memcpy(&saved_time,tls+0x300,8);memcpy(&saved_previous,tls+0x328,8);memcpy(&saved_rng,tls+0x318,8);memcpy(&saved_view,tls+0x314,4);
    __try {
        __try {
            /* A JobThread never syncs TLS game time and reads the published view. */
            memcpy(tls+0x300,&zero,8);memcpy(tls+0x328,&zero,8);memcpy(tls+0x314,&zero_view,4);memcpy(tls+0x318,&key,8);
            job(payload,NULL);
        } __finally {
            memcpy(tls+0x300,&saved_time,8);memcpy(tls+0x328,&saved_previous,8);memcpy(tls+0x314,&saved_view,4);memcpy(tls+0x318,&saved_rng,8);
        }
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {ok=0;}
    return ok;
}
static int areaj_queue_impl(areaj_queue_fn queue,unsigned char *tls,void *mgr,void *cb,void *payload,unsigned cat,unsigned prio) {
    areaj_site *s=tls?areaj_site_of(cb,payload):NULL;
    if(!s) {InterlockedIncrement64(&areaj_forwarded);return queue(mgr,cb,payload,cat,prio);}
    /* A job that broke off has run in part: it is not queued a second time, only counted. */
    if(areaj_run(areaj_job_test?areaj_job_test:(areaj_job_fn)cb,payload,tls)) InterlockedIncrement64(&s->inlined);
    else InterlockedIncrement64(&areaj_failed);
    return 0;
}
static __attribute__((noinline)) int areaj_queue(void *mgr,void *cb,void *payload,unsigned cat,unsigned prio) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return areaj_queue_impl((areaj_queue_fn)(void*)(image+AREAJ_QUEUE),slots?(unsigned char*)slots[0]:NULL,mgr,cb,payload,cat,prio);
}
static int areaj_installed_count(void) {int n=0;for(unsigned i=0;i<AREAJ_SITES;i++) n+=areaj_sites[i].installed!=0;return n;}
/* gate.area_jobs(1), once the pilot modules know the game's image. 1: every site is in (or was). */
static int areaj_arm(void) {
    if(areaj_installed_count()==(int)AREAJ_SITES) return 1;
    if(!pnat_base) {areaj_why="no_image";return 0;}
    /* The slots' near page is made by whichever module needs it first (the first try, 16 Nov: the arming was refused). */
    if(!pslot_page && !(pslot_page=pslot_alloc_page())) {areaj_why="page";return 0;}
    areaj_why="-";
    for(unsigned i=0;i<AREAJ_SITES;i++) {
        areaj_site *s=&areaj_sites[i];
        if(s->installed) continue;
        if(!hash_ok(image+s->ask,s->ask_size,s->ask_sha) || !hash_ok(image+s->thunk,s->thunk_size,s->thunk_sha) ||
           !hash_ok(image+s->body,s->body_size,s->body_sha)) {areaj_why=areaj_pin_why[i];continue;}
        if(pres_site_install(s->site,AREAJ_QUEUE,s->stub,(void*)areaj_queue,s->patched)) s->installed=1;
        else areaj_why=areaj_site_why[i];
    }
    return areaj_installed_count()==(int)AREAJ_SITES;
}
static void areaj_restore(void) {
    for(unsigned i=0;i<AREAJ_SITES;i++)
        if(areaj_sites[i].installed && pres_site_uninstall(areaj_sites[i].site,AREAJ_QUEUE,areaj_sites[i].patched)) areaj_sites[i].installed=0;
}
static int areaj_status(char *out,size_t capacity) {
    return snprintf(out,capacity,"installed=%d/%u why=%s inlined=%lld/%lld/%lld forwarded=%lld failed=%lld",areaj_installed_count(),AREAJ_SITES,areaj_why,
                    (long long)areaj_sites[0].inlined,(long long)areaj_sites[1].inlined,(long long)areaj_sites[2].inlined,
                    (long long)areaj_forwarded,(long long)areaj_failed);
}

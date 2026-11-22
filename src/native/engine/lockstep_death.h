/* KillEvent is deferred beyond the bullet update. Its native listener creates
 * the wreck controller and draws random linear/angular velocity for it. */
#define DEATH_ENTRY 0x2b7fef8u
#define DEATH_XL_ENTRY 0x2b99ac8u
/* 'all' (lockstep_death_all.h): the same listener in ShipMedium's and ShipLarge's slot 8. */
#define DEATH_M_ENTRY (0x2b82440u+0x40u)
#define DEATH_L_ENTRY (0x2b9bff8u+0x40u)
#include "lockstep_death_all.h"
#define DEATH_LISTENER 0x7cd610u
#define DEATH_EVENTS 64u
typedef void (*death_fn)(void *,void *,double,unsigned,void *);
typedef struct {uint64_t tick,call,time,before,after;unsigned actor,reason;} death_event;
static death_event death_events[DEATH_EVENTS];
static uint64_t death_actor_calls[ACTOR_CAPACITY],death_calls,death_failures;
static unsigned death_event_count;
static uint64_t death_dropped;
static int death_installed,death_restored,death_seeded;
static unsigned death_owned_slots;
static volatile LONG death_all;
static volatile LONG64 death_all_keyed,death_all_unkeyed;
static void death_execute(death_fn original,death_event *e,uint64_t *rng,
                          void *self,void *source,double time,unsigned reason,void *killer) {
    uint64_t saved=rng?*rng:0;
    const int restore_rng=death_seeded && rng;
    __try {
        if(restore_rng) *rng=actor_seed(0x44454154485f3031ULL,e->tick,e->actor,0,e->call,0);
        e->before=rng?*rng:0;original(self,source,time,reason,killer);e->after=rng?*rng:0;
    } __finally {
        DWORD error=GetLastError();
        if(restore_rng) *rng=saved;
        if(AbnormalTermination()) InterlockedIncrement64((volatile LONG64*)&death_failures);
        SetLastError(error);
    }
}
static void death_listener(void *self,void *source,double time,unsigned reason,void *killer) {
    registry_observe_object(self,REGISTRY_DEATH);
    death_fn original=(death_fn)(image+DEATH_LISTENER);unsigned actor=0;
    if(stats.active && boot_phase==3 && source==self && GetCurrentThreadId()==ui_thread) {
        for(unsigned i=0;i<ACTOR_CAPACITY;i++) {
            uint64_t id;if(self==actor_components[i] && read_local((unsigned char*)self+8,&id,8) && id==actor_ids[i]) {actor=i+1;break;}
        }
    }
    if(!actor || InterlockedCompareExchange(&death_all,0,0)) {
        /* 'all': every ship, actors too, on a word from the tick, its portable key and the reason. */
        if(InterlockedCompareExchange(&death_all,0,0)) {
            uint64_t runtime=0,key=0;unsigned klass;
            void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
            uint64_t *word=slots && slots[0]?(uint64_t*)((unsigned char*)slots[0]+0x318):NULL;
            if(stats.active && boot_phase==3 && source==self && GetCurrentThreadId()==ui_thread && isfinite(time) && word &&
               read_local((unsigned char*)self+8,&runtime,8)) key=world_identity_key(runtime,&klass);
            if(key) {
                const uint64_t saved=*word;
                {uint64_t clock=0;memcpy(&clock,(unsigned char*)slots[0]+0x300,8);death_all_note(stats.issued,key,reason,time,clock);}
                __try {*word=death_all_seed(stats.issued,key,reason);original(self,source,time,reason,killer);}
                __finally {DWORD error=GetLastError();*word=saved;if(AbnormalTermination()) InterlockedIncrement64((volatile LONG64*)&death_failures);SetLastError(error);}
                InterlockedIncrement64(&death_all_keyed);++death_calls;return;
            }
            InterlockedIncrement64(&death_all_unkeyed);
        }
        original(self,source,time,reason,killer);return;
    }
    /* The key needs the event's tick, actor and call; the table is rows, drained only with native_gate_death_diag. */
    static death_event scratch;death_event *e=&scratch;
    if(!gate_play_mode) {
        if(death_event_count==DEATH_EVENTS) ++death_dropped;
        else e=&death_events[death_event_count++];
    }
    memset(e,0,sizeof(*e));
    e->actor=actor;e->tick=stats.issued;e->call=++death_actor_calls[actor-1];e->reason=reason;memcpy(&e->time,&time,8);++death_calls;
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    uint64_t *rng=tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
    if(!rng || !isfinite(time)) ++death_failures;
    death_execute(original,e,rng,self,source,time,reason,killer);
}
static int death_swap_at(unsigned entry,void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+entry,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)(image+entry),value,expected);
    VirtualProtect(image+entry,8,old,&ignored);return actual==expected;
}
static int death_swap(void *expected,void *value) {return death_swap_at(DEATH_ENTRY,expected,value);}
static void wreck_clock_restore(void);
static void death_restore(void) {
    wreck_clock_restore();
    if(!death_installed) return;
    if((death_owned_slots&1) && death_swap((void*)death_listener,image+DEATH_LISTENER)) death_owned_slots&=~1u;
    if((death_owned_slots&2) && death_swap_at(DEATH_XL_ENTRY,(void*)death_listener,image+DEATH_LISTENER)) death_owned_slots&=~2u;
    if((death_owned_slots&4) && death_swap_at(DEATH_M_ENTRY,(void*)death_listener,image+DEATH_LISTENER)) death_owned_slots&=~4u;
    if((death_owned_slots&8) && death_swap_at(DEATH_L_ENTRY,(void*)death_listener,image+DEATH_LISTENER)) death_owned_slots&=~8u;
    InterlockedExchange(&death_all,0);
    death_restored=death_owned_slots==0;death_installed=!death_restored;
}
static int death_install_slots(void) {
    if(death_owned_slots || !death_swap(image+DEATH_LISTENER,(void*)death_listener)) return 0;
    death_owned_slots=1;death_installed=1;death_restored=0;
    if(!death_swap_at(DEATH_XL_ENTRY,image+DEATH_LISTENER,(void*)death_listener)) {death_restore();return 0;}
    death_owned_slots|=2;return 1;
}
static int death_install(int seed) {
    if(!boot_mode || GetModuleHandleW(L"x4lan_fixedtick_64.dll") || GetModuleHandleW(L"x4mp_combat.dll") ||
       !hash_ok(image+DEATH_LISTENER,180,"acf5deebe3cd68de95084938ccf71d601f4c5f1d1f18b2ecf4826ee85d43a99e") ||
       !death_install_slots()) return 0;
    death_seeded=seed;return 1;
}
/* 'all' after the boot install (UI thread, before the first admitted frame): the M and L slots join, every ship keyed. */
static int death_all_arm(void) {
    if(InterlockedCompareExchange(&death_all,0,0)) return 1;
    if(!death_installed || death_restored || (death_owned_slots&3)!=3 || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!(death_owned_slots&4)) {
        if(!death_swap_at(DEATH_M_ENTRY,image+DEATH_LISTENER,(void*)death_listener)) return 0;
        death_owned_slots|=4;
    }
    if(!(death_owned_slots&8)) {
        if(!death_swap_at(DEATH_L_ENTRY,image+DEATH_LISTENER,(void*)death_listener)) return 0;
        death_owned_slots|=8;
    }
    InterlockedExchange(&death_all,1);return 1;
}
/* The four slots still ours (a tick check with 'all'). */
static int death_all_owned(void) {
    if(!InterlockedCompareExchange(&death_all,0,0)) return 1;
    return *(void**)(image+DEATH_ENTRY)==(void*)death_listener && *(void**)(image+DEATH_XL_ENTRY)==(void*)death_listener &&
           *(void**)(image+DEATH_M_ENTRY)==(void*)death_listener && *(void**)(image+DEATH_L_ENTRY)==(void*)death_listener;
}
static int death_all_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity,"all=%d slots=%x keyed=%lld unkeyed=%lld calls=%llu failures=%llu installed=%d restored=%d",
        (int)death_all,death_owned_slots,(long long)death_all_keyed,(long long)death_all_unkeyed,(unsigned long long)death_calls,
        (unsigned long long)death_failures,death_installed,death_restored);
    return n>0 && (size_t)n<capacity?n:0;
}
/* Wreck clock rows (lockstep_death_all.h): U::LinearDecelerationController's vtable slot 6 swapped to a recorder. */
#define WRECK_CLOCK_SLOT (0x2c3f5f0u+6u*8u)
#define WRECK_CLOCK_POSE 0xe27580u
typedef uint64_t (*wreck_pose_fn)(void *,void *,double,uint64_t);
static int wreck_clock_installed;
static uint64_t wreck_clock_pose(void *self,void *out,double time,uint64_t flag) {
    uint32_t chain[4]={0,0,0,0};void *frames[8];const USHORT got=RtlCaptureStackBackTrace(1,8,frames,NULL);unsigned k=0;
    for(USHORT i=0;i<got && k<4;i++) if((unsigned char*)frames[i]>=image && (unsigned char*)frames[i]<image+0x6fa9000u) chain[k++]=(uint32_t)((unsigned char*)frames[i]-image);
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);uint64_t cur=0,prev=0;
    if(slots && slots[0]) {memcpy(&cur,(unsigned char*)slots[0]+0x300,8);memcpy(&prev,(unsigned char*)slots[0]+0x328,8);}
    const DWORD thread=GetCurrentThreadId();
    wreck_clock_note(self,time,cur,prev,thread==ui_thread?0u:thread==main_thread?1u:2u,chain);
    return ((wreck_pose_fn)(void *)(image+WRECK_CLOCK_POSE))(self,out,time,flag);
}
static int wreck_clock_arm(uint64_t first,uint64_t last) {
    if(wreck_clock_installed) return first==wreck_clock_first && last==wreck_clock_last;
    if(!first || last<first || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    wreck_clock_first=first;wreck_clock_last=last;
    if(!death_swap_at(WRECK_CLOCK_SLOT,image+WRECK_CLOCK_POSE,(void*)wreck_clock_pose)) {wreck_clock_last=0;return 0;}
    wreck_clock_installed=1;return 1;
}
static void wreck_clock_restore(void) {
    if(wreck_clock_installed && death_swap_at(WRECK_CLOCK_SLOT,(void*)wreck_clock_pose,image+WRECK_CLOCK_POSE)) wreck_clock_installed=0;
}

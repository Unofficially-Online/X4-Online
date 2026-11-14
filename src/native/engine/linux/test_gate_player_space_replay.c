#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "gate_player_space_replay.h"
#define ORIGINAL UINT64_C(0x165d950)
#define WRAPPER UINT64_C(0x10000200)
typedef struct {
    _Alignas(8) unsigned char owner[0xd00];
    uint64_t entries[3]; uintptr_t owner_ptr;
    unsigned local, cf, slot[2], phase, original_calls, local_calls, queue_calls, refresh_calls;
    unsigned order, recursion, replace_owner, rebind_registry, change_cf_local, fail_queue;
    player_space_replay_state *state; player_space_replay_ops *ops;
} fixture;
static int range(void *v,uintptr_t p,size_t n,unsigned writable) {
 fixture *f=v;(void)writable;
 uintptr_t owner=(uintptr_t)f->owner, entries=(uintptr_t)f->entries;
 return (p>=owner && p-owner<=sizeof f->owner && n<=sizeof f->owner-(p-owner)) ||
        (p>=entries && p-entries<=sizeof f->entries && n<=sizeof f->entries-(p-entries));
}
static int read_mem(void *v,uintptr_t p,void *out,size_t n) { if(!range(v,p,n,0))return 0;memcpy(out,(void*)p,n);return 1; }
static player_space_vector_swap swap(void *v,uintptr_t p,uint64_t old,uint64_t value) {
 if(!range(v,p,8,1))return PLAYER_SPACE_VECTOR_SWAP_REJECTED;
 uint64_t *slot=(uint64_t*)p;if(*slot!=old)return PLAYER_SPACE_VECTOR_SWAP_REJECTED;*slot=value;return PLAYER_SPACE_VECTOR_SWAP_APPLIED;
}
static int owner(void *v,uintptr_t *out) { fixture *f=v;*out=f->replace_owner?f->owner_ptr+8:f->owner_ptr;return 1; }
static int postjoin(void *v,uint64_t tick,uintptr_t ptr) {fixture *f=v;return tick==38 && ptr==f->owner_ptr;}
static int phase(void *v,uint64_t *generation,player_space_replay_phase *p) {fixture *f=v;*generation=38;*p=f->phase;return 1;}
static int local_state(void *v,unsigned *out) { *out=((fixture*)v)->local;return 1; }
static int slot_state(void *v,unsigned slot,unsigned *out) { if(slot<1||slot>2)return 0;*out=((fixture*)v)->slot[slot-1];return 1; }
static int cf0(void *v,uintptr_t p,unsigned *out) { fixture *f=v;if(p!=f->owner_ptr)return 0;*out=f->cf;return 1; }
static int local(void *v,uint64_t tick,unsigned active) {
 fixture *f=v;assert(tick==38 && f->order==0 && f->cf!=active);f->order=1;f->local_calls++;
 if(f->recursion) player_space_replay_callback(f->state,f->ops,9,1);
 if(f->replace_owner) f->owner_ptr+=8;
 if(f->change_cf_local) f->cf=active;
 return 1;
}
static int queue(void *v,uint64_t tick,unsigned active) { fixture *f=v;assert(tick==38 && f->order==1 && f->cf!=active);f->order=2;f->queue_calls++;if(f->fail_queue)return 0;f->local=active;return 1; }
static int refresh(void *v,uint64_t tick) {fixture *f=v;assert(tick==38 && f->order==2 && f->cf!=f->local);f->order=3;f->refresh_calls++;if(f->fail_queue)_exit(88);if(f->rebind_registry)f->entries[0]++;return 1;}
static void original(void *v,unsigned context,unsigned char active) { fixture *f=v;(void)context;(void)active;f->original_calls++; }
static void fatal(void *v,const char *why) { (void)v;(void)why;_exit(99); }
static void setup(fixture *f,player_space_replay_state *s,player_space_replay_ops *o) {
 memset(f,0,sizeof *f);memset(s,0,sizeof *s);memset(o,0,sizeof *o);
 f->owner_ptr=(uintptr_t)f->owner;f->entries[0]=0x700;f->entries[1]=ORIGINAL;f->entries[2]=0x900;
 uint64_t hdr[3]={(uintptr_t)f->entries,(uintptr_t)(f->entries+3),(uintptr_t)(f->entries+3)};
 memcpy(f->owner+0x10,hdr,sizeof hdr);f->local=f->cf=f->slot[0]=f->slot[1]=1;f->phase=PLAYER_SPACE_PHASE_CAPTURE;
 o->context=f;o->vector=(player_space_vector_ops){f,range,read_mem,swap};o->owner=owner;o->postjoin=postjoin;
 o->callback_phase=phase;o->local_state=local_state;o->slot_state=slot_state;o->cf0=cf0;o->local=local;
 o->queue_edge=queue;o->refresh=refresh;o->original=original;o->fatal=fatal;f->state=s;f->ops=o;
}
static player_space_vector_swap uncertain_swap(void *v,uintptr_t p,uint64_t old,uint64_t value) {
 (void)v;(void)p;(void)old;(void)value;return PLAYER_SPACE_VECTOR_SWAP_UNKNOWN;
}
static void expect_install_fatal(unsigned variant) {
 pid_t p=fork();assert(p>=0);if(!p) {
  fixture f;player_space_replay_state s;player_space_replay_ops o;setup(&f,&s,&o);
  assert(player_space_replay_request(&s,"context8",1,1,1,38));
  if(variant==0)f.cf=0;
  if(variant==1)o.vector.cas=uncertain_swap;
  player_space_replay_mainboundary(&s,&o,variant==2?39:38,ORIGINAL,WRAPPER);_exit(1);
 }
 int status=0;assert(waitpid(p,&status,0)==p && WIFEXITED(status) && WEXITSTATUS(status)==99);
}
static void expect_fatal(fixture *f,player_space_replay_state *s,player_space_replay_ops *o,unsigned which) {
 pid_t p=fork();assert(p>=0);if(!p) {
  if(which==0)f->phase=PLAYER_SPACE_PHASE_INVALID;
  if(which==1)f->recursion=1;
  if(which==2)f->entries[0]++;
  if(which==3)f->rebind_registry=1;
  if(which==4)f->cf=0;
  if(which==5)f->replace_owner=1;
  if(which==6)f->change_cf_local=1;
  if(which==7)f->fail_queue=1;
  player_space_replay_callback(s,o,8,0);_exit(1);
 }
 int status=0;assert(waitpid(p,&status,0)==p && WIFEXITED(status) && WEXITSTATUS(status)==99);
}
int main(void) {
 fixture f;player_space_replay_state s;player_space_replay_ops o;setup(&f,&s,&o);
 assert(player_space_replay_request(&s,NULL,0,0,0,0));assert(player_space_replay_mainboundary(&s,NULL,38,ORIGINAL,WRAPPER));assert(f.entries[1]==ORIGINAL);
 for(unsigned i=0;i<3;i++)expect_install_fatal(i);
 setup(&f,&s,&o);assert(!player_space_replay_request(&s,"wrong",1,1,1,38));
 assert(player_space_replay_request(&s,"context8",1,1,1,38));
 assert(player_space_replay_mainboundary(&s,&o,37,ORIGINAL,WRAPPER)&&f.entries[1]==ORIGINAL);
 assert(player_space_replay_mainboundary(&s,&o,38,ORIGINAL,WRAPPER)&&f.entries[1]==WRAPPER);
 player_space_replay_callback(&s,&o,5,1);assert(f.original_calls==1);
 f.phase=PLAYER_SPACE_PHASE_PREACTIVE;player_space_replay_callback(&s,&o,8,1);assert(f.original_calls==2);
 f.phase=PLAYER_SPACE_PHASE_CAPTURE;for(unsigned i=0;i<8;i++)expect_fatal(&f,&s,&o,i);
 player_space_replay_callback(&s,&o,8,0);
 assert(f.local_calls==1 && f.queue_calls==1 && f.refresh_calls==1 && f.order==3 && f.local==0 && f.cf==1 && f.original_calls==2);
 f.cf=0; /* native dispatcher writes CF0 only after callbacks return */
 unsigned value=9;assert(player_space_replay_expected_local(&o,&value) && !value);
 assert(player_space_replay_expected_slot(&o,2,&value) && value==1);
 assert(player_space_replay_mainboundary(&s,&o,39,ORIGINAL,WRAPPER));
 return 0;
}

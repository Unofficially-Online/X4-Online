#define _GNU_SOURCE
#include "player_stock_view_wire_handoff.h"
#include "player_stock_view_mailbox.h"
#include <stdatomic.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <assert.h>
static keyboard_replay keyboard_probe_queue;
static player_stock_view_wire_handoff keyboard_view_wire;
static unsigned keyboard_probe_requested;
static struct {uint64_t issued; unsigned active,slot;} stats={38,1,1};
static struct {uint64_t generation;} player_recurring_tail_token={38};
static int authenticated=1;
static int keyboard_probe_worker(void){return authenticated;}
static int player_recurring_physics_entered(void){return authenticated;}
static int accept_ui(void){return authenticated;}
static void *gate_tls_self(void){return (void *)1;}
static int gate_tls_worker(void *p){return p && authenticated;}
static uint64_t GetCurrentThreadId(void){return (uint64_t)syscall(SYS_gettid);}
static uint64_t stock_observer_worker_thread;
static player_stock_view_mailbox host_view_mailbox,render_view_mailbox;
static player_stock_view_capture_sample host_view_sealed,render_view_sealed;
static _Atomic unsigned host_view_enabled=1,render_view_enabled=1;
static _Atomic uint64_t host_view_owner_tid,render_view_owner_tid;
static unsigned host_view_sealed_present=1,render_view_sealed_present=1;
#include "gate_player_stock_view_wire_handoff.h"
static int ready(void *context,const keyboard_replay_config *cfg,uint64_t generation){
    (void)context;(void)cfg;(void)generation;return authenticated;
}
int main(void){
    stock_observer_worker_thread=GetCurrentThreadId();
    host_view_owner_tid=render_view_owner_tid=GetCurrentThreadId();
    keyboard_probe_queue.config=(keyboard_replay_config){.first_capture=38,.last_capture=41};
    keyboard_view_wire.first=38;keyboard_view_wire.last=40;
    assert(player_stock_view_mailbox_configure(&host_view_mailbox,38,41,38,40,1,2));
    assert(player_stock_view_mailbox_configure(&render_view_mailbox,38,41,38,40,2,2));
    host_view_mailbox.last_sealed=render_view_mailbox.last_sealed=38;
    host_view_sealed=(player_stock_view_capture_sample){1,0.75f,111};
    render_view_sealed=(player_stock_view_capture_sample){2,1.25f,222};
    player_stock_view_wire_sealed output;
    assert(player_stock_view_wire_read_sealed(38,1,&output)&&output.view.target==111);
    stats.slot=2;
    assert(player_stock_view_wire_read_sealed(38,2,&output)&&output.view.target==222);
    for(unsigned failure=0;failure<5;++failure){
        if(failure==0)render_view_mailbox.last_sealed=37;
        if(failure==1)render_view_mailbox.pending=1;
        if(failure==2)render_view_owner_tid=GetCurrentThreadId()+1;
        if(failure==3)render_view_enabled=0;
        if(failure==4)authenticated=0;
        output.view.target=999;
        assert(!player_stock_view_wire_read_sealed(38,2,&output)&&output.view.target==999);
        render_view_mailbox.last_sealed=38;render_view_mailbox.pending=0;
        render_view_owner_tid=GetCurrentThreadId();render_view_enabled=1;authenticated=1;
    }
    assert(!player_stock_view_wire_read_sealed(38,1,&output));
    assert(!player_stock_view_wire_enable(ready,NULL));
    stats.issued=1;
    assert(player_stock_view_wire_enable(ready,NULL));
    assert(!player_stock_view_wire_enable(ready,NULL));
    return 0;
}

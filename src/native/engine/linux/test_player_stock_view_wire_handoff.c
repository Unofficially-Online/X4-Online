#include "player_stock_view_wire_handoff.h"
#include <assert.h>

static unsigned ready_calls, ready_allowed = 1;
static int ready(void *context, const keyboard_replay_config *config, uint64_t generation) {
    assert(context == &ready_calls && config->independent_players && config->cursor_active_only);
    ++ready_calls;
    return (generation == 0 || (generation >= 38 && generation <= 41)) ? (int)ready_allowed : 0;
}
static keyboard_replay_config config(void) {
    return (keyboard_replay_config){.own_slot=1, .delay=2, .first_capture=38,
        .last_capture=41, .saved_ship_ids={111,222}, .independent_players=1, .cursor_active_only=1};
}
static void sample_refusal(unsigned failure) {
    player_stock_view_wire_handoff handoff = {0};
    keyboard_replay replay = {.config=config(), .tick=38};
    assert(player_stock_view_wire_bind(&handoff, ready, &ready_calls));
    assert(player_stock_view_wire_configure(&handoff, &replay.config, 38, 40));
    player_stock_view_wire_sealed sealed = {.generation=38, .slot=1, .present=1,
        .view={3,0.75f,UINT64_C(0x123456789abcdef0)}};
    switch (failure) {
        case 0: sealed.generation=37; break;
        case 1: sealed.slot=2; break;
        case 2: sealed.present=0; break;
        case 3: sealed.present=2; break;
        case 4: sealed.view.bits=4; break;
        case 5: sealed.view.fov=NAN; break;
        case 6: ready_allowed=0; break;
        case 7: replay.config.view_capture=0; break;
    }
    linux_stock_view_sample output={2,2.0f,9};
    unsigned present=77;
    assert(!player_stock_view_wire_sample(&handoff,&replay,38,&sealed,&output,&present));
    assert(output.bits==2 && output.fov==2.0f && output.target==9 && present==77);
    ready_allowed=1;
}
int main(void) {
    player_stock_view_wire_handoff off={0};
    keyboard_replay_config unchanged=config();
    assert(player_stock_view_wire_configure(&off,&unchanged,0,0) && !unchanged.view_capture);
    assert(!ready_calls);
    for (unsigned failure=0;failure<8;++failure) sample_refusal(failure);
    for (unsigned failure=0;failure<4;++failure) {
        player_stock_view_wire_handoff handoff={0};
        keyboard_replay_config invalid=config();
        assert(player_stock_view_wire_bind(&handoff,ready,&ready_calls));
        assert(!player_stock_view_wire_bind(&handoff,ready,&ready_calls));
        if (failure==0) invalid.independent_players=0;
        if (failure==1) invalid.cursor_active_only=0;
        if (failure==2) invalid.delay=3;
        if (failure==3) invalid.lookahead=1;
        assert(!player_stock_view_wire_configure(&handoff,&invalid,38,40));
        assert(!invalid.view_capture && !handoff.admitted);
    }
    for (unsigned absent=0;absent<2;++absent) {
        player_stock_view_wire_handoff handoff={0};
        keyboard_replay replay;
        keyboard_replay_config cfg=config();
        assert(player_stock_view_wire_bind(&handoff,ready,&ready_calls));
        assert(player_stock_view_wire_configure(&handoff,&cfg,absent?39:38,40));
        assert(keyboard_replay_init(&replay));
        assert(keyboard_replay_configure(&replay,&cfg)==KEYBOARD_REPLAY_OK);
        assert(keyboard_replay_begin(&replay,38)==KEYBOARD_REPLAY_OK);
        assert(keyboard_replay_publish_slots(&replay,38,NULL,NULL)==KEYBOARD_REPLAY_NOT_DUE);
        assert(keyboard_replay_consume(&replay,STOCK_INPUT_STATE,2,1)==KEYBOARD_REPLAY_OK);
        player_stock_view_wire_sealed sealed={.generation=38,.slot=1,.present=!absent};
        if (!absent) sealed.view=(linux_stock_view_sample){3,0.75f,UINT64_C(0x123456789abcdef0)};
        linux_stock_view_sample view;
        unsigned present;
        assert(player_stock_view_wire_sample(&handoff,&replay,38,&sealed,&view,&present));
        assert(present==!absent);
        float cursor[3]={0.25f,-0.5f,0.0f};
        unsigned char bytes[STOCK_INPUT_MAX_BYTES]; unsigned length=0;
        assert(keyboard_replay_seal_presentation(&replay,cursor,present?&view:NULL,
            bytes,sizeof bytes,&length)==KEYBOARD_REPLAY_OK);
        stock_input_record record;
        assert(stock_input_decode(bytes,length,&record));
        assert(record.count==1 && record.events[0].id==2 && record.cursor[0]==cursor[0]);
        assert(!!(record.flags&STOCK_INPUT_VIEW)==!absent);
        if (!absent) assert(record.view==3 && record.fov==0.75f && record.target==sealed.view.target);
        else assert(!record.view && !record.target && !stock_input_float_bits(record.fov));
        /* Out-of-window final absence must be genuine, never stale payload. */
        replay.tick=41; sealed.generation=41; sealed.present=0;
        replay.config.view_capture=1;
        sealed.view=(linux_stock_view_sample){0};
        assert(player_stock_view_wire_sample(&handoff,&replay,41,&sealed,&view,&present) && !present);
        sealed.view.target=999;
        assert(!player_stock_view_wire_sample(&handoff,&replay,41,&sealed,&view,&present));
        sealed.view.target=0;
        sealed.present=1;
        assert(!player_stock_view_wire_sample(&handoff,&replay,41,&sealed,&view,&present));
        keyboard_replay_stop(&replay);
        assert(keyboard_replay_destroy(&replay));
    }
    /* SPACE context toggle and VIEW are one sealed row. Inactive cursor is
     * genuinely absent, and CONTEXT8 is never a STATE key-bit operation. */
    player_stock_view_wire_handoff handoff={0};
    keyboard_replay replay;
    keyboard_replay_config cfg=config();
    cfg.space_context8=1;
    assert(player_stock_view_wire_bind(&handoff,ready,&ready_calls));
    assert(player_stock_view_wire_configure(&handoff,&cfg,38,40));
    assert(keyboard_replay_init(&replay));
    assert(keyboard_replay_configure(&replay,&cfg)==KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(&replay,38)==KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish_slots(&replay,38,NULL,NULL)==KEYBOARD_REPLAY_NOT_DUE);
    assert(keyboard_replay_consume(&replay,STOCK_INPUT_CONTEXT,8,0)==KEYBOARD_REPLAY_OK);
    player_stock_view_wire_sealed sealed={.generation=38,.slot=1,.present=1,.view={1,1.0f,7}};
    linux_stock_view_sample view;
    unsigned present;
    assert(player_stock_view_wire_sample(&handoff,&replay,38,&sealed,&view,&present));
    unsigned char bytes[STOCK_INPUT_MAX_BYTES];unsigned length;
    assert(keyboard_replay_seal_presentation(&replay,NULL,&view,bytes,sizeof bytes,&length)==KEYBOARD_REPLAY_OK);
    stock_input_record record;
    assert(stock_input_decode(bytes,length,&record));
    assert(record.flags==STOCK_INPUT_VIEW && record.count==1 && record.events[0].kind==STOCK_INPUT_CONTEXT &&
        record.events[0].id==8 && !record.events[0].value && record.target==7);
    for(unsigned axis=0;axis<3;++axis) assert(!stock_input_float_bits(record.cursor[axis]));
    keyboard_replay_stop(&replay);assert(keyboard_replay_destroy(&replay));
    return 0;
}

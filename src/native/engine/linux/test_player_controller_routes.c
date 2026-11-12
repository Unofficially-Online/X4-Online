#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include "gate_player_controller_routes.h"
typedef struct {
    unsigned char flag;
} ship;
typedef struct {
    void **vt;
    int marker;
    ship *controlled_ship;
} object;
static int verified_ship_flag(void *controller, void *held_ship, unsigned char *flag) {
    object *controlled = controller;
    if (controlled->controlled_ship != held_ship)
        return 0;
    *flag = controlled->controlled_ship->flag;
    return 1;
}
static int called;
static double seen_time;
static unsigned seen_mode;
static void *seen_self;
static void motion(void *controller, void *output, unsigned char mode, double elapsed_time, int kind) {
    called = kind;
    seen_self = controller;
    seen_mode = mode;
    seen_time = elapsed_time;
    for (unsigned index = 0; index < 4; index++)
        ((double *)output)[index] = elapsed_time + kind + index;
}
static void stock_motion(void *controller, void *output, unsigned char mode, double elapsed_time) {
    motion(controller, output, mode, elapsed_time, 1);
}
static void agent_motion(void *controller, void *output, unsigned char mode, double elapsed_time) {
    motion(controller, output, mode, elapsed_time, 2);
}
static void player_motion(void *controller, void *output, unsigned char mode, double elapsed_time) {
    motion(controller, output, mode, elapsed_time, 3);
}
static unsigned char assist(void *controller, unsigned char argument) {
    (void)controller;
    return argument ? 8 : 7;
}
static unsigned char agent_mode(void *snapshot) {
    (void)snapshot;
    return 9;
}
static unsigned char no_highway(void *snapshot) {
    (void)snapshot;
    return 10;
}
static float throttle(void *snapshot) {
    (void)snapshot;
    return 0.75f;
}
static void nothing(void) {
}
static void *foreign_reader(void *arg) {
    object *controller = arg;
    double output[4];
    ((player_route_motion)controller->vt[13])(controller, output, 12, 9.25);
    assert(called == 2 && seen_mode == 12 && seen_time == 9.25);
    return NULL;
}
int main(void) {
    void *raw[81];
    for (unsigned index = 0; index < 81; index++)
        raw[index] = (void *)nothing;
    raw[0] = (void *)0x1234;
    raw[1] = (void *)0x4567;
    void **table = raw + 2;
    table[13] = table[14] = table[17] = (void *)stock_motion;
    table[21] = (void *)throttle;
    table[30] = (void *)no_highway;
    table[56] = (void *)assist;
    table[57] = (void *)agent_mode;
    ship held_ship = {1}, wrong_ship = {1};
    object canonical = {table, 1, &held_ship}, p2 = {table, 2, &held_ship};
    player_controller_routes routes;
    player_route_motion agents[3] = {agent_motion, agent_motion, agent_motion};
    player_route_motion players[3] = {player_motion, player_motion, player_motion};
    player_route_snapshot snapshot = {1, 0, 1, 1, 0, 0.125f};
    assert(player_routes_install(&routes, &p2, table, agents, players, &held_ship, verified_ship_flag,
                                 snapshot));
    assert(canonical.vt == table && p2.vt != table && p2.vt[-2] == raw[0] && p2.vt[-1] == raw[1]);
    for (unsigned index = 40; index <= 43; index++)
        assert(p2.vt[index] == table[index]);
    double output[4];
    for (unsigned index = 0; index < 3; index++) {
        unsigned slot = player_route_motion_slots[index];
        ((player_route_motion)p2.vt[slot])(&p2, output, 173, 42.625);
        assert(called == 2 && seen_self == &p2 && seen_mode == 173 && seen_time == 42.625 &&
               output[3] == 47.625);
    }
    assert(((player_route_assist)p2.vt[56])(&p2, 0) == 0 && ((player_route_assist)p2.vt[56])(&p2, 4) == 1);
    assert(((player_route_bool)p2.vt[57])(&p2) == 1 && ((player_route_bool)p2.vt[30])(&p2) == 0);
    assert(((player_route_float)p2.vt[21])(&p2) == 0.125f);
    ((player_route_motion)canonical.vt[13])(&canonical, output, 25, 3.5);
    assert(called == 1 && seen_self == &canonical);
    assert(player_routes_enter(&routes) && !player_routes_enter(&routes));
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, foreign_reader, &p2));
    assert(!pthread_join(thread, NULL));
    ((player_route_motion)p2.vt[13])(&p2, output, 254, 123.25);
    assert(called == 1 && seen_mode == 254 && seen_time == 123.25);
    assert(((player_route_assist)p2.vt[56])(&p2, 1) == 8 && ((player_route_bool)p2.vt[57])(&p2) == 9);
    assert(((player_route_bool)p2.vt[30])(&p2) == 10 && ((player_route_float)p2.vt[21])(&p2) == 0.75f);
    snapshot = (player_route_snapshot){0, 1, 0, 0, 1, -2.5f};
    assert(player_routes_leave(&routes, snapshot));
    for (unsigned index = 0; index < 3; index++) {
        ((player_route_motion)p2.vt[player_route_motion_slots[index]])(&p2, output, 37, 0.0625);
        assert(called == 3 && seen_mode == 37 && seen_time == 0.0625);
    }
    held_ship.flag = 0;
    ((player_route_motion)p2.vt[13])(&p2, output, 37, 0.0625);
    assert(called == 2);
    held_ship.flag = 1;
    ((player_route_motion)p2.vt[13])(&p2, output, 37, 0.0625);
    assert(called == 3);
    p2.controlled_ship = &wrong_ship;
    called = 0;
    ((player_route_motion)p2.vt[13])(&p2, output, 37, 0.0625);
    assert(called == 0 && atomic_load(&routes.access_failures) == 1);
    p2.controlled_ship = &held_ship;
    assert(((player_route_float)p2.vt[21])(&p2) == -2.5f && ((player_route_bool)p2.vt[30])(&p2) == 1);
    assert(player_routes_restore(&routes) && p2.vt == table && canonical.vt == table);
    assert(!player_routes_restore(&routes));
    players[2] = NULL;
    assert(!player_routes_install(&routes, &p2, table, agents, players, &held_ship, verified_ship_flag,
                                  snapshot));
    puts("synthetic P2 routing ABI, snapshots, canonical and restore PASS");
    return 0;
}

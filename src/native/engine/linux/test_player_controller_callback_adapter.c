#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include "player_controller_callback_adapter.h"

typedef struct {
    void **table;
    int identity_valid;
} fake_controller;
typedef struct {
    player_controller_routes *routes;
    fake_controller *controller;
    uint64_t thread;
    int scope_owned;
    unsigned opens;
    unsigned closes;
    unsigned upkeep_calls;
    unsigned environment_calls;
    unsigned change_calls;
    double observed_time;
    void *first;
    void *second;
    unsigned char mode;
} fixture;
static fixture *current_fixture;

static uint64_t fixture_thread(void *context) {
    return ((fixture *)context)->thread;
}
static int fixture_identity(void *context, void *controller) {
    fixture *state = context;
    return state->controller == controller && state->controller->identity_valid;
}
static pcc_scope_state fixture_scope(void *context, void *controller) {
    fixture *state = context;
    (void)controller;
    return state->scope_owned ? PCC_SCOPE_OWN : PCC_SCOPE_OUTSIDE;
}
static int fixture_open(void *context, void *controller) {
    fixture *state = context;
    (void)controller;
    if (!player_routes_enter(state->routes))
        return 0;
    state->scope_owned = 1;
    ++state->opens;
    return 1;
}
static int fixture_close(void *context, void *controller) {
    fixture *state = context;
    (void)controller;
    player_route_snapshot snapshot = {0};
    if (!player_routes_leave(state->routes, snapshot))
        return 0;
    state->scope_owned = 0;
    ++state->closes;
    return 1;
}
static void fixture_report(void *context, const char *reason) {
    (void)context;
    (void)reason;
}
static void fixture_fatal(const char *reason) {
    (void)reason;
    /* Deliberately return: wrappers must still terminate on failure. */
}
static int fixture_ship_flag(void *controller, void *ship, unsigned char *flag) {
    (void)controller;
    (void)ship;
    *flag = 1;
    return 1;
}
static void fixture_motion(void *controller, void *output, unsigned char mode, double time) {
    (void)controller;
    (void)output;
    (void)mode;
    (void)time;
}
static unsigned char fixture_assist(void *controller, unsigned char argument) {
    (void)controller;
    return argument;
}
static unsigned char fixture_bool(void *controller) {
    (void)controller;
    return 1;
}
static float fixture_throttle(void *controller) {
    (void)controller;
    return 0.5f;
}
static void fixture_upkeep(void *controller, double time) {
    assert(current_fixture->scope_owned && player_routes_scope == current_fixture->routes);
    assert(controller == current_fixture->controller);
    void **table = *(void ***)controller;
    assert(((float (*)(void *))table[21])(controller) == 0.5f);
    ++current_fixture->upkeep_calls;
    current_fixture->observed_time = time;
}
static void fixture_environment(void *controller, void *first, void *second) {
    assert(current_fixture->scope_owned && player_routes_scope == current_fixture->routes);
    assert(controller == current_fixture->controller);
    ++current_fixture->environment_calls;
    current_fixture->first = first;
    current_fixture->second = second;
}
static void fixture_change(void *controller, void *first, void *second, unsigned char mode) {
    assert(current_fixture->scope_owned && player_routes_scope == current_fixture->routes);
    ++current_fixture->change_calls;
    current_fixture->mode = mode;
    if (mode)
        ((void (*)(void *, void *, void *))(*(void ***)controller)[41])(controller, first, second);
}
static void fixture_unused(void) {
}
static void fixture_slot42(void) {
}
static void expect_fatal_wrong_thread(fake_controller *controller, fixture *state) {
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        state->thread = 99;
        ((void (*)(void *, double))controller->table[40])(controller, 12.5);
        _Exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 126);
}
int main(void) {
    void *raw[81];
    for (unsigned index = 0; index < 81; ++index)
        raw[index] = (void *)fixture_unused;
    void **stock = raw + 2;
    stock[13] = stock[14] = stock[17] = (void *)fixture_motion;
    stock[21] = (void *)fixture_throttle;
    stock[30] = stock[57] = (void *)fixture_bool;
    stock[56] = (void *)fixture_assist;
    stock[40] = (void *)fixture_upkeep;
    stock[41] = (void *)fixture_environment;
    stock[43] = (void *)fixture_change;
    fake_controller controller = {stock, 1};
    player_controller_routes routes = {0};
    player_callback_adapter adapter = {0};
    fixture state = {.routes = &routes, .controller = &controller, .thread = 11};
    current_fixture = &state;
    pcc_operations operations = {&state,       fixture_thread, fixture_identity, fixture_scope,
                                 fixture_open, fixture_close,  fixture_report};
    pcc_originals originals = {fixture_upkeep, fixture_environment, fixture_change};
    player_route_motion motions[3] = {fixture_motion, fixture_motion, fixture_motion};
    player_route_snapshot snapshot = {0};
    int held_ship = 1;
    void *slot42 = (void *)fixture_slot42;
    assert(player_callback_install(&adapter, &routes, &controller, stock, &held_ship, fixture_ship_flag,
                                   motions, motions, snapshot, &operations, &originals, 11, fixture_fatal,
                                   slot42));
    assert(controller.table == &routes.table[2] && controller.table[-2] == stock[-2] &&
           controller.table[-1] == stock[-1] && controller.table[42] == slot42);
    assert(controller.table[13] == (void *)player_routes_transform);
    assert(((float (*)(void *))controller.table[21])(&controller) == 0.0f);
    ((void (*)(void *, double))controller.table[40])(&controller, 7.25);
    assert(state.upkeep_calls == 1 && state.observed_time == 7.25 && state.opens == 1 && state.closes == 1);
    ((void (*)(void *, void *, void *, unsigned char))controller.table[43])(&controller, (void *)0x1234,
                                                                            (void *)0x4567, 1);
    assert(state.change_calls == 1 && state.environment_calls == 1 && state.mode == 1 &&
           state.first == (void *)0x1234 && state.second == (void *)0x4567 && state.opens == 2 &&
           state.closes == 2);
    assert(fixture_open(&state, &controller));
    ((void (*)(void *, void *, void *))controller.table[41])(&controller, NULL, NULL);
    assert(state.scope_owned && state.opens == 3 && state.closes == 2 && state.environment_calls == 2);
    assert(fixture_close(&state, &controller));
    expect_fatal_wrong_thread(&controller, &state);
    assert(player_callback_restore(&adapter) && controller.table == stock);
    puts("synthetic private callback adapter and getter scope PASS");
}

#ifndef X4_LINUX_PLAYER_STOCK_SPACE_H
#define X4_LINUX_PLAYER_STOCK_SPACE_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Split SPACE/context 8 into this client's UI state and the delayed simulation
 * event. The native adapter must authenticate fresh pointers, function pins,
 * the worker and the selected player's bank before calling either half. These
 * helpers alone do not install a hook or admit CONTEXT records to the queue.
 *
 * Bank offsets below are relative to Linux X4 9.00's 0x3d8d9a0 control bank.
 * memcpy avoids alignment assumptions for the native vector at offset 0x74.
 */
enum {
    PLAYER_SPACE_BANK_SIZE = 0x278,
    PLAYER_SPACE_AUTOPILOT = 0xb8,
    PLAYER_SPACE_TIMERS = 0xe0,
    PLAYER_SPACE_FLIGHT = 0x108,
    PLAYER_SPACE_UI_FIRST = 0x9f42,
    PLAYER_SPACE_UI_SECOND = 0x9f44,
    PLAYER_SPACE_INPUT_SIZE = 0x9f45
};

typedef struct {
    unsigned char *bank;
    size_t bank_size;
    unsigned char *input;
    size_t input_size;
    void *controller;
    void *player;
    unsigned char (*authority)(void *controller);
    void (*handoff)(void *controller, unsigned char authority);
    void (*release)(void *controller);
    void (*started)(void *player);
    void (*stopped)(void *player);
    int (*still_valid)(void *context);
    void *identity_context;
} player_space_simulation;

static inline int player_space_simulation_valid(const player_space_simulation *target) {
    return target && target->bank && target->bank_size >= PLAYER_SPACE_BANK_SIZE &&
        target->input && target->input_size >= PLAYER_SPACE_INPUT_SIZE &&
        target->controller && target->player && target->authority && target->handoff &&
        target->release && target->started && target->stopped && target->still_valid;
}

/* One ordered replay event. A native callback can already have changed the
 * world: the owner must fail-stop on an exception or subsequent identity drift,
 * never retry the event or claim that it rolled back. The two UI bytes belong
 * to the local window and are restored after the simulation callbacks return.
 */
static inline int player_space_replay(const player_space_simulation *target,
                                     unsigned active, double game_time) {
    if (!player_space_simulation_valid(target) || active > 1 ||
        !isfinite(game_time) || !isfinite(game_time + 0.5) ||
        !target->still_valid(target->identity_context)) return 0;

    unsigned char first = target->input[PLAYER_SPACE_UI_FIRST];
    unsigned char second = target->input[PLAYER_SPACE_UI_SECOND];
    uint64_t autopilot;
    if (active) {
        double deadline = game_time + 0.5;
        for (unsigned index = 0; index < 5; ++index) {
            memcpy(target->bank + PLAYER_SPACE_TIMERS + index * sizeof deadline,
                   &deadline, sizeof deadline);
        }
        unsigned char authority = target->authority(target->controller);
        if (!target->still_valid(target->identity_context)) return 0;
        /* Read after the authority call, in the native function's order. */
        memcpy(&autopilot, target->bank + PLAYER_SPACE_AUTOPILOT, sizeof autopilot);
        if (!autopilot) {
            target->handoff(target->controller, authority);
            if (!target->still_valid(target->identity_context)) return 0;
        }
        target->bank[PLAYER_SPACE_FLIGHT] = 1;
        target->started(target->player);
        if (!target->still_valid(target->identity_context)) return 0;
    } else {
        target->bank[PLAYER_SPACE_FLIGHT] = 0;
        static const size_t vector_offsets[] = {0x30, 0x68, 0, 0x74};
        for (unsigned index = 0; index < sizeof vector_offsets / sizeof *vector_offsets; ++index) {
            memset(target->bank + vector_offsets[index], 0, sizeof(uint64_t));
        }
        memset(target->bank + 0x38, 0, sizeof(uint32_t));
        memset(target->bank + 0x08, 0, sizeof(uint32_t));
        memcpy(&autopilot, target->bank + PLAYER_SPACE_AUTOPILOT, sizeof autopilot);
        if (!autopilot) {
            target->release(target->controller);
            if (!target->still_valid(target->identity_context)) return 0;
        }
        target->stopped(target->player);
        if (!target->still_valid(target->identity_context)) return 0;
    }
    target->input[PLAYER_SPACE_UI_FIRST] = first;
    target->input[PLAYER_SPACE_UI_SECOND] = second;
    return 1;
}

typedef struct {
    unsigned char *input;
    size_t input_size;
    unsigned char *saved_mouse_steering;
    unsigned char *saved_direct_steering;
    int (*persistent_mouse_steering)(void *context);
    void *option_context;
    void (*set_mouse_steering)(void *input, unsigned char enabled);
    void (*set_direct_steering)(void *input, unsigned char enabled);
    int (*still_valid)(void *context);
    void *identity_context;
} player_space_local;

/* Local half only. No flight-bank, controller or player callback is reachable
 * here. The adapter records the edge and refreshes the context tree afterward,
 * in the dispatcher's original order. Values outside bool are rejected before
 * any setter; negative option results mean an unavailable native option read.
 */
static inline int player_space_local_apply(const player_space_local *local, unsigned active) {
    if (!local || active > 1 || !local->input || local->input_size < PLAYER_SPACE_INPUT_SIZE ||
        !local->saved_mouse_steering || !local->saved_direct_steering ||
        !local->persistent_mouse_steering || !local->set_mouse_steering ||
        !local->set_direct_steering || !local->still_valid ||
        !local->still_valid(local->identity_context) || local->input[0x9f33] > 1 ||
        local->input[0x9f36] > 1 || local->input[0x9f38] > 1 ||
        *local->saved_mouse_steering > 1 || *local->saved_direct_steering > 1) return 0;

    if (active) {
        if (local->input[0x9f33]) {
            int persistent = local->persistent_mouse_steering(local->option_context);
            if (!local->still_valid(local->identity_context) ||
                (persistent != 0 && persistent != 1)) return 0;
            if (persistent) {
                if (local->input[0x9f36] != *local->saved_mouse_steering) {
                    local->set_mouse_steering(local->input, *local->saved_mouse_steering);
                    if (!local->still_valid(local->identity_context)) return 0;
                }
                if (local->input[0x9f38] != *local->saved_direct_steering) {
                    local->set_direct_steering(local->input, *local->saved_direct_steering);
                    if (!local->still_valid(local->identity_context)) return 0;
                }
            }
            *local->saved_mouse_steering = 0;
            *local->saved_direct_steering = 0;
        }
    } else {
        *local->saved_mouse_steering = local->input[0x9f36];
        if (*local->saved_mouse_steering) {
            local->set_mouse_steering(local->input, 0);
            if (!local->still_valid(local->identity_context)) return 0;
        }
        *local->saved_direct_steering = local->input[0x9f38];
        if (*local->saved_direct_steering) {
            local->set_direct_steering(local->input, 0);
            if (!local->still_valid(local->identity_context)) return 0;
        }
    }
    local->input[PLAYER_SPACE_UI_FIRST] = (unsigned char)active;
    local->input[PLAYER_SPACE_UI_SECOND] = (unsigned char)active;
    return 1;
}

#endif

/* Read-only, last-call Steering boundary diagnostic. These snapshots bracket an
 * update; they do not establish which RequestVelocity branches ran or capture
 * the arguments at those call sites. Values remain raw physical lane bits.
 *
 * Offsets are paired individually from the pinned pilot-request-map evidence
 * and lockstep_request.h, rather than by applying a global layout delta. */
#ifndef LOCKSTEP_FLIGHT_BOUNDARY_H
#define LOCKSTEP_FLIGHT_BOUNDARY_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    FLIGHT_BOUNDARY_LINEAR,
    FLIGHT_BOUNDARY_ANGULAR,
    FLIGHT_BOUNDARY_GROUPS
};
enum {
    FLIGHT_BOUNDARY_POINTER_READ = 1u,
    FLIGHT_BOUNDARY_POINTER_RECHECK = 2u,
    FLIGHT_BOUNDARY_POINTER_STABLE = 4u,
    FLIGHT_BOUNDARY_SOLVER_PRESENT = 8u
};
enum {
    FLIGHT_BOUNDARY_ROUNDED = 1u,
    FLIGHT_BOUNDARY_SCALED = 2u,
    FLIGHT_BOUNDARY_GAIN = 4u,
    FLIGHT_BOUNDARY_DIRTY0 = 8u,
    FLIGHT_BOUNDARY_DIRTY1 = 16u
};
typedef struct {
    uint32_t valid;
    uint64_t rounded[3];
    uint64_t scaled[3];
    uint64_t gain[3];
    unsigned char dirty[2];
} flight_boundary_group;
typedef struct {
    uint32_t valid;
    uintptr_t solver;
    uintptr_t solver_recheck;
    flight_boundary_group group[FLIGHT_BOUNDARY_GROUPS];
} flight_boundary_request;
typedef struct {
    /* Bits 0/1 mean the linear/angular three-lane command read succeeded. */
    uint32_t command_valid;
    uint32_t command_after[FLIGHT_BOUNDARY_GROUPS][3];
    flight_boundary_request request[2]; /* before / after original */
    uint32_t solver_same_valid;
    uint32_t solver_same;
} flight_boundary_sample;
typedef struct {
    size_t agent_solver;
    size_t command[FLIGHT_BOUNDARY_GROUPS];
    size_t group[FLIGHT_BOUNDARY_GROUPS];
    size_t rounded;
    size_t scaled;
    size_t gain;
    size_t dirty[2];
} flight_boundary_layout;
/* Windows command address: ec8477/ec848b retain model+1d0 in R15; the
 * ec91a9..ec9274 callers read R15+[0,4,8] and R15+[10,14,18]. */
#define FLIGHT_BOUNDARY_LINUX_LAYOUT \
    {0xfa0u, {0x1c0u, 0x1d0u}, {0x69e0u, 0x12c60u}, 0x36c8u, 0x36e0u, 0x3848u, {0x3830u, 0x3838u}}
#define FLIGHT_BOUNDARY_WINDOWS_LAYOUT \
    {0xf80u, {0x1d0u, 0x1e0u}, {0x6a00u, 0x12cd0u}, 0x36d8u, 0x36f0u, 0x3858u, {0x3840u, 0x3848u}}
typedef int (*flight_boundary_reader)(const void *, void *, size_t);

/* A failed reader may have copied a prefix: clear it and leave validity unset.
 * Numeric zeros have meaning only when their corresponding valid bit is set. */
static int flight_boundary_read(
    flight_boundary_reader reader,
    uintptr_t base,
    size_t offset,
    void *out,
    size_t bytes
) {
    if (base && offset <= UINTPTR_MAX - base && bytes <= UINTPTR_MAX - (base + offset) &&
        reader((const void *)(base + offset), out, bytes)) {
        return 1;
    }
    memset(out, 0, bytes);
    return 0;
}

static void flight_boundary_request_read(
    flight_boundary_reader reader,
    const flight_boundary_layout *layout,
    const void *agent,
    flight_boundary_request *out
) {
    memset(out, 0, sizeof(*out));
    if (!flight_boundary_read(reader, (uintptr_t)agent, layout->agent_solver, &out->solver, sizeof(out->solver))) {
        return;
    }
    out->valid = FLIGHT_BOUNDARY_POINTER_READ;
    if (out->solver) {
        out->valid |= FLIGHT_BOUNDARY_SOLVER_PRESENT;
        for (unsigned i = 0; i < FLIGHT_BOUNDARY_GROUPS; i++) {
            flight_boundary_group *g = &out->group[i];
            if (layout->group[i] > UINTPTR_MAX - out->solver) {
                continue;
            }
            uintptr_t self = out->solver + layout->group[i];
            if (flight_boundary_read(reader, self, layout->rounded, g->rounded, sizeof(g->rounded))) {
                g->valid |= FLIGHT_BOUNDARY_ROUNDED;
            }
            if (flight_boundary_read(reader, self, layout->scaled, g->scaled, sizeof(g->scaled))) {
                g->valid |= FLIGHT_BOUNDARY_SCALED;
            }
            if (flight_boundary_read(reader, self, layout->gain, g->gain, sizeof(g->gain))) {
                g->valid |= FLIGHT_BOUNDARY_GAIN;
            }
            if (flight_boundary_read(reader, self, layout->dirty[0], &g->dirty[0], 1)) {
                g->valid |= FLIGHT_BOUNDARY_DIRTY0;
            }
            if (flight_boundary_read(reader, self, layout->dirty[1], &g->dirty[1], 1)) {
                g->valid |= FLIGHT_BOUNDARY_DIRTY1;
            }
        }
    }
    if (flight_boundary_read(reader, (uintptr_t)agent, layout->agent_solver,
                             &out->solver_recheck, sizeof(out->solver_recheck))) {
        out->valid |= FLIGHT_BOUNDARY_POINTER_RECHECK;
        if (out->solver == out->solver_recheck) {
            out->valid |= FLIGHT_BOUNDARY_POINTER_STABLE;
        }
    }
    /* A changed/unreadable pointer invalidates all numerical fields. The two
     * raw pointer observations remain available to explain the rejection. */
    if (!(out->valid & FLIGHT_BOUNDARY_POINTER_STABLE)) {
        for (unsigned i = 0; i < FLIGHT_BOUNDARY_GROUPS; i++) {
            out->group[i].valid = 0;
        }
    }
}

static void flight_boundary_begin(
    flight_boundary_reader reader,
    const flight_boundary_layout *layout,
    const void *agent,
    flight_boundary_sample *out
) {
    memset(out, 0, sizeof(*out));
    flight_boundary_request_read(reader, layout, agent, &out->request[0]);
}

static void flight_boundary_end(
    flight_boundary_reader reader,
    const flight_boundary_layout *layout,
    const void *model,
    const void *agent,
    flight_boundary_sample *out
) {
    out->command_valid = 0;
    for (unsigned i = 0; i < FLIGHT_BOUNDARY_GROUPS; i++) {
        if (flight_boundary_read(reader, (uintptr_t)model, layout->command[i],
                                 out->command_after[i], sizeof(out->command_after[i]))) {
            out->command_valid |= 1u << i;
        }
    }
    flight_boundary_request_read(reader, layout, agent, &out->request[1]);
    const uint32_t stable = FLIGHT_BOUNDARY_POINTER_READ | FLIGHT_BOUNDARY_POINTER_RECHECK |
                            FLIGHT_BOUNDARY_POINTER_STABLE;
    out->solver_same_valid = (out->request[0].valid & stable) == stable &&
                             (out->request[1].valid & stable) == stable;
    out->solver_same = out->solver_same_valid && out->request[0].solver == out->request[1].solver;
}
#endif

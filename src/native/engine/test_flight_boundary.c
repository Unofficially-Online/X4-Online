/* Portable fake-layout test; no engine, process attachment, hook or network.
 * cc -std=c11 -Wall -Wextra -Werror test_flight_boundary.c -o /tmp/flight-boundary-test
 */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include "lockstep_flight_boundary.h"

static unsigned char agent[0x1000], model[0x400], solver[0x17000];
static unsigned char agent_copy[sizeof(agent)], model_copy[sizeof(model)], solver_copy[sizeof(solver)];
static uintptr_t fail_address, pointer_address;
static unsigned pointer_reads, change_pointer_at, fail_pointer_at;

static int in_region(uintptr_t address, size_t bytes, const void *base, size_t size) {
    uintptr_t start = (uintptr_t)base;
    return address >= start && address - start <= size && bytes <= size - (address - start);
}

static int fake_read(const void *source, void *dest, size_t bytes) {
    uintptr_t address = (uintptr_t)source;
    if (address == pointer_address) {
        ++pointer_reads;
        if (pointer_reads == fail_pointer_at) {
            memset(dest, 0xcd, bytes);
            return 0;
        }
        if (pointer_reads == change_pointer_at) {
            uintptr_t changed = (uintptr_t)solver + 16;
            assert(bytes == sizeof(changed));
            memcpy(dest, &changed, bytes);
            return 1;
        }
    }
    if (address == fail_address ||
        !(in_region(address, bytes, agent, sizeof(agent)) ||
          in_region(address, bytes, model, sizeof(model)) ||
          in_region(address, bytes, solver, sizeof(solver)))) {
        memset(dest, 0xcd, bytes); /* a partial/dirty failed destination */
        return 0;
    }
    memcpy(dest, source, bytes);
    return 1;
}

static void set_solver(const flight_boundary_layout *layout, uintptr_t pointer) {
    memcpy(agent + layout->agent_solver, &pointer, sizeof(pointer));
}

static void reset(const flight_boundary_layout *layout) {
    for (size_t i = 0; i < sizeof(solver); i++) {
        solver[i] = (unsigned char)(i * 17u + (i >> 8) * 31u);
    }
    memset(agent, 0x93, sizeof(agent));
    memset(model, 0xa5, sizeof(model));
    set_solver(layout, (uintptr_t)solver);
    fail_address = 0;
    pointer_address = (uintptr_t)agent + layout->agent_solver;
    pointer_reads = change_pointer_at = fail_pointer_at = 0;
}

static void save_bytes(void) {
    memcpy(agent_copy, agent, sizeof(agent));
    memcpy(model_copy, model, sizeof(model));
    memcpy(solver_copy, solver, sizeof(solver));
}

static void unchanged(void) {
    assert(!memcmp(agent_copy, agent, sizeof(agent)));
    assert(!memcmp(model_copy, model, sizeof(model)));
    assert(!memcmp(solver_copy, solver, sizeof(solver)));
}

static void expected_group(
    const flight_boundary_group *g,
    size_t group,
    size_t rounded,
    size_t scaled,
    size_t gain,
    size_t d0,
    size_t d1
) {
    assert(g->valid == 31);
    assert(!memcmp(g->rounded, solver + group + rounded, 24));
    assert(!memcmp(g->scaled, solver + group + scaled, 24));
    assert(!memcmp(g->gain, solver + group + gain, 24));
    assert(g->dirty[0] == solver[group + d0] && g->dirty[1] == solver[group + d1]);
}

static void mapped_bytes(const flight_boundary_layout *layout, int windows) {
    reset(layout);
    /* Four lanes per stored vector: the fourth must never be captured. Include
     * nonfinite/negative-zero raw patterns to verify this performs no math. */
    const uint32_t linear[4] = {0x7fc12345u, 0x80000000u, 0x3f123456u, 0xdeadbeefu};
    const uint32_t angular[4] = {0xff800000u, 0x7f800000u, 0xbf001234u, 0xcafef00du};
    memcpy(model + (windows ? 0x1d0 : 0x1c0), linear, sizeof(linear));
    memcpy(model + (windows ? 0x1e0 : 0x1d0), angular, sizeof(angular));
    save_bytes();
    flight_boundary_sample s;
    memset(&s, 0xff, sizeof(s));
    flight_boundary_begin(fake_read, layout, agent, &s);
    assert(!s.command_valid && !s.solver_same_valid && !s.solver_same);
    assert(s.request[0].valid == 15 && !s.request[1].valid);
    /* Change only fixture bytes between captures, as the original may do. */
    unsigned char *rounded = solver + (windows ? 0x6a00 + 0x36d8 : 0x69e0 + 0x36c8);
    unsigned char old = rounded[0];
    rounded[0] ^= 0x5a;
    flight_boundary_end(fake_read, layout, model, agent, &s);
    assert(s.command_valid == 3 && s.solver_same_valid && s.solver_same);
    assert(!memcmp(s.command_after[0], linear, 12) && !memcmp(s.command_after[1], angular, 12));
    assert(s.request[0].solver == (uintptr_t)solver && s.request[1].valid == 15);
    assert(((unsigned char *)s.request[0].group[0].rounded)[0] == old);
    for (unsigned phase = 0; phase < 2; phase++) {
        /* Assert physical offsets independently of the sampler's layout. */
        rounded[0] = phase ? old ^ 0x5a : old;
        if (windows) {
            expected_group(&s.request[phase].group[0], 0x6a00, 0x36d8, 0x36f0, 0x3858, 0x3840, 0x3848);
            expected_group(&s.request[phase].group[1], 0x12cd0, 0x36d8, 0x36f0, 0x3858, 0x3840, 0x3848);
        } else {
            expected_group(&s.request[phase].group[0], 0x69e0, 0x36c8, 0x36e0, 0x3848, 0x3830, 0x3838);
            expected_group(&s.request[phase].group[1], 0x12c60, 0x36c8, 0x36e0, 0x3848, 0x3830, 0x3838);
        }
    }
    rounded[0] = old;
    unchanged();
}

static void failures(const flight_boundary_layout *layout) {
    flight_boundary_sample s;
    reset(layout);
    save_bytes();
    fail_address = (uintptr_t)solver + layout->group[0] + layout->scaled;
    flight_boundary_begin(fake_read, layout, agent, &s);
    assert(s.request[0].valid == 15 && s.request[0].group[0].valid == (31 ^ FLIGHT_BOUNDARY_SCALED));
    const uint64_t zero[3] = {0};
    assert(!memcmp(s.request[0].group[0].scaled, zero, sizeof(zero)));
    fail_address = (uintptr_t)model + layout->command[0];
    flight_boundary_end(fake_read, layout, model, agent, &s);
    assert(s.command_valid == 2 && !memcmp(s.command_after[0], zero, 12));
    unchanged();

    reset(layout);
    save_bytes();
    fail_pointer_at = 1;
    flight_boundary_begin(fake_read, layout, agent, &s);
    flight_boundary_end(fake_read, layout, model, agent, &s);
    assert(!s.request[0].valid && !s.solver_same_valid && !s.solver_same);
    unchanged();

    reset(layout);
    save_bytes();
    change_pointer_at = 2;
    flight_boundary_begin(fake_read, layout, agent, &s);
    assert(s.request[0].valid == 11 && s.request[0].solver != s.request[0].solver_recheck);
    assert(!s.request[0].group[0].valid && !s.request[0].group[1].valid);
    flight_boundary_end(fake_read, layout, model, agent, &s);
    assert(!s.solver_same_valid && !s.solver_same);
    unchanged();

    reset(layout);
    save_bytes();
    fail_pointer_at = 2;
    flight_boundary_begin(fake_read, layout, agent, &s);
    assert(s.request[0].valid == 9 && !s.request[0].group[0].valid && !s.request[0].group[1].valid);
    unchanged();

    reset(layout);
    set_solver(layout, 0);
    save_bytes();
    flight_boundary_begin(fake_read, layout, agent, &s);
    flight_boundary_end(fake_read, layout, model, agent, &s);
    assert(s.request[0].valid == 7 && s.request[1].valid == 7 && s.solver_same_valid && s.solver_same);
    assert(!s.request[0].group[0].valid && !s.request[1].group[1].valid);
    unchanged();
    /* Original may create the absent solver: retain that before/after fact,
     * rather than calling an allocating getter to obtain the before value. */
    flight_boundary_begin(fake_read, layout, agent, &s);
    set_solver(layout, (uintptr_t)solver);
    save_bytes();
    flight_boundary_end(fake_read, layout, model, agent, &s);
    assert(s.request[0].valid == 7 && s.request[1].valid == 15 && s.solver_same_valid && !s.solver_same);
    unchanged();

    reset(layout);
    set_solver(layout, 1);
    save_bytes();
    flight_boundary_begin(fake_read, layout, agent, &s);
    assert(s.request[0].valid == 15 && !s.request[0].group[0].valid && !s.request[0].group[1].valid);
    unchanged();

    reset(layout);
    set_solver(layout, UINTPTR_MAX - 4);
    save_bytes();
    flight_boundary_begin(fake_read, layout, agent, &s);
    assert(!s.request[0].group[0].valid && !s.request[0].group[1].valid);
    unchanged();
}

int main(void) {
    const flight_boundary_layout linux_layout = FLIGHT_BOUNDARY_LINUX_LAYOUT;
    const flight_boundary_layout windows_layout = FLIGHT_BOUNDARY_WINDOWS_LAYOUT;
    mapped_bytes(&linux_layout, 0);
    mapped_bytes(&windows_layout, 1);
    failures(&linux_layout);
    failures(&windows_layout);
    puts("flight boundary fake-layout read-only capture PASS");
    return 0;
}

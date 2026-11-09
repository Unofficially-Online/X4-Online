/* Text for the read-only flight boundary diagnostic. Local pointer addresses
 * never leave the sample; presence, validity and identity comparisons do.
 * Invalid values are printed for inspection but must not count as evidence of
 * equality. Callers retain the matching tick and last-call ordinal. */
#ifndef LOCKSTEP_FLIGHT_BOUNDARY_FORMAT_H
#define LOCKSTEP_FLIGHT_BOUNDARY_FORMAT_H

#include <stdarg.h>
#include <stdio.h>
#include "lockstep_flight_boundary.h"

typedef struct {
    char *output;
    size_t capacity;
    size_t length;
    int failed;
} flight_boundary_text;

static void flight_boundary_append(flight_boundary_text *text, const char *format, ...) {
    if (text->failed) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(text->output + text->length, text->capacity - text->length,
                            format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= text->capacity - text->length) {
        text->failed = 1;
        return;
    }
    text->length += (size_t)written;
}

/* A zero return rejects the entire row, including any truncated prefix. */
static int flight_boundary_format(const flight_boundary_sample *sample,
                                 uint64_t tick, uint64_t calls,
                                 char *output, size_t capacity) {
    if (!sample || !output || !capacity) {
        return 0;
    }
    flight_boundary_text text = {output, capacity, 0, 0};
    flight_boundary_append(&text, "tick=%llu calls=%llu capture=last command_valid=%u",
                           (unsigned long long)tick, (unsigned long long)calls,
                           sample->command_valid);
    static const char *const groups[] = {"linear", "angular"};
    static const char *const phases[] = {"before", "after"};
    for (unsigned group = 0; group < FLIGHT_BOUNDARY_GROUPS; ++group) {
        flight_boundary_append(&text, " command_%s=", groups[group]);
        for (unsigned axis = 0; axis < 3; ++axis) {
            flight_boundary_append(&text, "%08x", sample->command_after[group][axis]);
        }
    }
    flight_boundary_append(&text, " solver_same_valid=%u solver_same=%u",
                           sample->solver_same_valid, sample->solver_same);
    for (unsigned phase = 0; phase < 2; ++phase) {
        const flight_boundary_request *request = &sample->request[phase];
        flight_boundary_append(&text, " %s_valid=%u", phases[phase], request->valid);
        for (unsigned group = 0; group < FLIGHT_BOUNDARY_GROUPS; ++group) {
            const flight_boundary_group *values = &request->group[group];
            flight_boundary_append(&text, " %s_%s_valid=%u", phases[phase], groups[group],
                                   values->valid);
            const uint64_t *vectors[] = {values->rounded, values->scaled, values->gain};
            static const char *const names[] = {"rounded", "scaled", "gain"};
            for (unsigned vector = 0; vector < 3; ++vector) {
                flight_boundary_append(&text, " %s_%s_%s=", phases[phase], groups[group], names[vector]);
                for (unsigned axis = 0; axis < 3; ++axis) {
                    flight_boundary_append(&text, "%016llx", (unsigned long long)vectors[vector][axis]);
                }
            }
            flight_boundary_append(&text, " %s_%s_dirty=%02x%02x", phases[phase], groups[group],
                                   (unsigned)values->dirty[0], (unsigned)values->dirty[1]);
        }
    }
    if (text.failed) {
        output[0] = '\0';
        return 0;
    }
    return (int)text.length;
}

#endif

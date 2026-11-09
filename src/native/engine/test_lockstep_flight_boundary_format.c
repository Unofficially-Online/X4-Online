/* Formatter boundaries are tested without X4 or a native image. */
#include "lockstep_flight_boundary_format.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static int copy_bytes(const void *source, void *destination, size_t size) {
    memcpy(destination, source, size);
    return 1;
}

int main(void) {
    unsigned char agent[0x1000] = {0};
    unsigned char model[0x400] = {0};
    const flight_boundary_layout layout = FLIGHT_BOUNDARY_LINUX_LAYOUT;
    flight_boundary_sample sample;
    flight_boundary_begin(copy_bytes, &layout, agent, &sample);
    flight_boundary_end(copy_bytes, &layout, model, agent, &sample);

    char full[4096];
    int length = flight_boundary_format(&sample, 98, 1, full, sizeof(full));
    assert(length > 0 && (size_t)length == strlen(full));
    assert(strstr(full, "capture=last command_valid=3 "));
    assert(strstr(full, "solver_same_valid=1 solver_same=1 before_valid=7 "));
    assert(strstr(full, "before_linear_valid=0 "));

    /* An unread value remains visibly invalid, even if its raw bits are zero. */
    sample.command_valid = 0;
    sample.request[0].solver = (uintptr_t)UINT64_C(0x123456789abcdef0);
    assert(flight_boundary_format(&sample, 98, 1, full, sizeof(full)) > 0);
    assert(strstr(full, "command_valid=0 "));
    assert(!strstr(full, "123456789abcdef0"));
    length = (int)strlen(full);

    /* Check every truncation point, including capacity zero and exactly the
     * payload length. No truncated row may be mistaken for a complete one. */
    unsigned char guarded[4098];
    for (size_t capacity = 0; capacity <= (size_t)length; ++capacity) {
        memset(guarded, 0xa5, sizeof(guarded));
        int result = flight_boundary_format(&sample, 98, 1, (char *)guarded + 1, capacity);
        assert(result == 0);
        assert(guarded[0] == 0xa5 && guarded[capacity + 1] == 0xa5);
        if (capacity) {
            assert(guarded[1] == '\0');
        }
    }
    assert(flight_boundary_format(&sample, 98, 1, (char *)guarded + 1, (size_t)length + 1) == length);
    assert(!strcmp((char *)guarded + 1, full));
    assert(!flight_boundary_format(NULL, 98, 1, full, sizeof(full)));
    assert(!flight_boundary_format(&sample, 98, 1, NULL, sizeof(full)));
    puts("flight boundary formatter: validity, pointer privacy and truncation PASS");
    return 0;
}

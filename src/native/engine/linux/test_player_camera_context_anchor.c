/* Engine-free, adversarial checks for the adopted full context snapshot. */
#include <assert.h>
#include <stdio.h>
#include "gate_player_camera_context_anchor.h"

static void make_entry(unsigned char *p, unsigned context, unsigned requester, unsigned priority, unsigned enabled) {
    memset(p, 0, 16); p[0] = context; p[4] = requester; p[8] = priority; p[12] = enabled;
    p[13] = (unsigned char)(context ^ 0x5a); /* opaque bytes are preserved, not interpreted */
}
static void setup(input_context_census_sample *s, input_context_census_entries *e, int after) {
    static const unsigned before[] = {0,1,2,3,4,5,7,8,9,10,12,15,16};
    static const unsigned later[] = {0,1,2,3,4,5,7,8,9,11,12,13,14,15,16};
    const unsigned *keys = after ? later : before;
    memset(s, 0, sizeof *s); memset(e, 0, sizeof *e);
    e->count = after ? 15 : 13;
    s->complete = s->tree_valid = 1; s->target_present = 7; s->node_count = e->count; s->manager = 0x1010;
    for (unsigned i = 0; i < e->count; ++i) {
        unsigned c = keys[i]; unsigned requester = c == 13 || c == 14 ? 20 : 6;
        make_entry(e->payloads[i], c, requester, requester == 20 ? 5 : 0, requester == 20 ? 0 : 1);
        for (unsigned j = 0; j < 3; ++j) if (c == (unsigned[]){8,15,16}[j]) memcpy(s->payloads[j], e->payloads[i], 16);
    }
}
int main(void) {
    (void)player_camera_anchor;
    input_context_census_sample before, after, altered_sample;
    input_context_census_entries old, now, altered;
    player_camera_context_anchor anchor = {0};
    setup(&before, &old, 0); setup(&after, &now, 1);
    assert(camera_context_transition(&before, &old, &after, &now));
    /* New opaque tails may be nonzero. Existing bytes must remain exact. */
    now.payloads[9][15] = 0xef;
    assert(camera_context_transition(&before, &old, &after, &now));
    altered = now; altered.payloads[0][15] ^= 1;
    assert(!camera_context_transition(&before, &old, &after, &altered));
    altered = now; altered.payloads[9][12] = 0;
    assert(!camera_context_transition(&before, &old, &after, &altered));
    altered = now; altered.payloads[11][8] = 6;
    assert(!camera_context_transition(&before, &old, &after, &altered));
    altered = now; altered.payloads[12][4] = 21;
    assert(!camera_context_transition(&before, &old, &after, &altered));
    altered = old; altered.payloads[9][12] = 0;
    assert(!camera_context_transition(&before, &altered, &after, &now));
    altered = now; memcpy(altered.payloads[1], altered.payloads[0], 16);
    assert(!camera_context_transition(&before, &old, &after, &altered));
    altered_sample = after; altered_sample.manager++;
    assert(!camera_context_transition(&before, &old, &altered_sample, &now));
    altered_sample = after; altered_sample.tree_valid = 0;
    assert(!camera_context_transition(&before, &old, &altered_sample, &now));
    altered_sample = after; altered_sample.payloads[0][15] ^= 1;
    assert(!camera_context_transition(&before, &old, &altered_sample, &now));
    assert(camera_context_anchor_adopt(&anchor, 77, 0x1000, 0x1234, 0x5678, &before, &old, &after, &now));
    assert(!camera_context_anchor_adopt(&anchor, 77, 0x1000, 0x1234, 0x5678, &before, &old, &after, &now));
    assert(camera_context_anchor_matches(&anchor, 77, 0x1000, 0x1234, 0x5678, &after, &now));
    assert(!camera_context_anchor_matches(&anchor, 78, 0x1000, 0x1234, 0x5678, &after, &now));
    assert(!camera_context_anchor_matches(&anchor, 77, 0x1001, 0x1234, 0x5678, &after, &now));
    assert(!camera_context_anchor_matches(&anchor, 77, 0x1000, 0x1235, 0x5678, &after, &now));
    assert(!camera_context_anchor_matches(&anchor, 77, 0x1000, 0x1234, 0x5679, &after, &now));
    altered = now; altered.payloads[0][15] ^= 1;
    assert(!camera_context_anchor_matches(&anchor, 77, 0x1000, 0x1234, 0x5678, &after, &altered));
    altered_sample = after; altered_sample.manager++;
    assert(!camera_context_anchor_matches(&anchor, 77, 0x1000, 0x1234, 0x5678, &altered_sample, &now));
    puts("camera full context anchor PASS");
    return 0;
}
